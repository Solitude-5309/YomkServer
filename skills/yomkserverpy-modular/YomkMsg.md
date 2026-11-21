# YomkMsg 标准消息形态（agent 必读规则）

权威用户文档：[docs/YomkMsg.md](../../docs/YomkMsg.md)。本页是同一套规则面向 agent 的无歧义浓缩，写代码时**必须**逐条遵守，防止旧口径残留。

## 三条铁律

1. **定义**：消息类必须 `class Xxx(YomkMsg)`（`from yomk.msgs import YomkMsg`）。`TYPE_NAME` 自动生成（最近一级父包 + "." + `__qualname__`，如 `yomk.msgs.ctx.Context`），随重命名自动跟随，**禁止手写**——类体手写 `TYPE_NAME` 仅限跨语言字面对齐场景。
2. **声明**：`install_func` 第三参一律传 `Xxx.TYPE_NAME`（std 类同规，如 `String.TYPE_NAME`），**禁止手写报文串**（旧写法 `install_func("/x", fn, "Xxx")` 已废弃）。
3. **校验**：端点体内 `if unpack(pkg, Xxx) is None: return Response(ResStatus.eNo, type_mismatch(pkg, Xxx.TYPE_NAME))`。**框架不做分发校验**（责任在端点作者）；失败文案期望/实际双侧均为 TYPE_NAME——体系外对象 got 侧报 `"null"`，体系内对象报自身 TYPE_NAME。

## unpack 语义速查

| 表达式 | 结果 |
|---|---|
| `unpack(String("x"), String)` | 命中返回原对象 |
| `unpack(裸str, String)` | `None`（A 方案：裸标量不是消息） |
| 子类实例解包为父类 | `None`（子类自动获得不同 TYPE_NAME，精确身份匹配） |
| `unpack(x, 无TYPE_NAME的类)` | 退化纯 isinstance |
| `type_of(msg实例)` | TYPE_NAME |
| `type_of(None / 裸str / 任意非消息对象)` | `"null"`（无第三态） |

## std 类与 List

- 标量 `String/Bool/Int64/Float64/Bytes`（字段 `value`），数组 `BoolArray/IntArray/FloatArray/StringArray`（字段 `items`），定义于 `yomk.msgs.std_msg`；
- 结构化清单用各域 List msg（`ContextKeyInfoList`、`FuncInfoList` 等，字段 `items`）；
- 内省端点（`/all` 等）返回 **List msg 实例**（裸链直调时）；链式门面读路径已解包、返回裸 list——两种形态不要混认。

## 实战陷阱（已发生，勿重蹈）

- **别名陷阱**：运行时类与消息类同域同名（EventLoop 双胞胎）时，`install_func`/`unpack` 的 TYPE_NAME 引用必须盯住 import 别名——错绑到运行时类即 init 崩溃。**正解：消息类命名带语义后缀消歧（现名 `EventLoopOpts`），不留别名。**
- 函数内定义的消息类 `__qualname__` 带 `<locals>` 段，如实入 TYPE_NAME；msg 类应定义在模块文件里。

## 写代码自查清单

- [ ] 消息类继承 `YomkMsg`？
- [ ] `install_func` 三参写的是 `Xxx.TYPE_NAME` 而非手写串？
- [ ] 端点体有 `unpack` + `type_mismatch` 校验行（报文名取 TYPE_NAME）？
- [ ] 标量端点取值用 `pkg.value`？内省返回用 `.items`？
- [ ] 断言契约文案时用的是 TYPE_NAME 字面（`"yomk.msgs.xxx"`）而非旧手写串？
