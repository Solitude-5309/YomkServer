# YomkContext（共享上下文）

跨服务强类型键值存储，内置变更前校验门控（Checker）与变更后监听（Monitor，同步/异步），支持分组隔离与运行期内省。随 `server().init()` 自动启动。全部操作经链式域入口 `ctx()`：`key/group/value` 累积定位目标，写路径终结（`create/set/destroy` 等）返回三态 `ResStatus`，类型化读终结 `get()` 直接返回值本体。

## 链式语法速查

| 场景 | 链 | 终结与返回 |
|---|---|---|
| 创建 | `ctx().key(k).group(g).value(v)` | `.create()` → `ResStatus`（g 省略 = 缺省 `"main"`） |
| 设置（整体替换） | `ctx().key(k).group(g).value(v)` | `.set()` → `ResStatus` |
| 类型化读 | `ctx().key(k).group(g).msg(cls)` | `.with_default(d).get()` → 值本体；`.with_default` 可省略（未命中回退 `None`）；`cls` 为期望类型，命中后做类型比对（对齐 C++ `msg<T>()` kName 语义）：不符记 WARN `typed read context key '<key>' type mismatch, expect '<期望>', got '<实际>', return default value` 并回退默认值；`cls` 省略时不校验（Python 动态超集） |
| 销毁键 | `ctx().key(k).group(g)` | `.destroy()` → `ResStatus` |
| 键级校验门控 | `ctx().key(k).group(g).checker_fn(fn)` | `.apply()` → `ResStatus` |
| 键级变更监听 | `ctx().key(k).group(g).monitor_fn(fn).async_(on)` | `.apply()` → `ResStatus`；`.async_(on)` 可省略 = 同步 |
| 组级开关 | `ctx().group(g).checker()` / `.monitor()` | `.enable()` / `.disable()` → `ResStatus`（**建组默认关**，须显式开启才生效；回调保留不删除） |
| 内省 | `ctx().group(g)` / `.key(k).group(g)` / — | `.keys()` 组内键名 / `.info()` 单键元信息 / `.groups()` 全部组名 / `.all()` 跨组聚合，均 → `Response` |

键级回调（`checker_fn` / `monitor_fn`）与组级开关分面（`checker()` / `monitor()`）按命名分层，`apply()` 未累积任何回调时返回 `Invalid`。

## 使用示例

```python
from yomk.msg.ctx import CheckStatus
from yomk.server import server
from yomk.ctx import ctx
from yomk.log import log

server().init()

# 1. 创建与设置
ctx().key("config").value("init_value").create()
ctx().key("config").value("new_value").set()

# 2. 类型化读（只读快照；未命中/未初始化回退默认值）
val = ctx().key("config").msg(str).with_default("fallback").get()

# 3. 变更前校验门控：只能注册到已存在的键；组开关默认关，注册后须显式 enable
ctx().key("config").checker_fn(
    lambda ctx: CheckStatus.eAccept   # Accept 放行；Reject 拦截 set
).apply()
ctx().group("main").checker().enable()

# 4. 变更后监听（可注册多个，追加式）
ctx().key("config").monitor_fn(
    lambda ctx: log().info("changed:", ctx.value)   # 收到本次 set 的键值快照
).apply()                                            # 缺省同步执行
ctx().group("main").monitor().enable()

# 5. 内省与销毁
ctx().keys()                           # 组内键名清单（缺省 main 组）
ctx().key("config").info()             # 单键元信息 ContextKeyInfo（含所属组 group）
ctx().groups()                         # 全部组名
ctx().all()                            # 跨组聚合：组 → 键元信息
ctx().key("config").destroy()          # 删除键
```

## 注意事项

- **写路径失败契约**（终结收敛为 `ResStatus`，msg 经裸链 `server().url("/YomkContext/...").msg(pkg).request()` 可查；入参非法 `Invalid`、业务未命中/冲突 `Failed`）：键名为空 `Invalid "context key is empty"`（`set` 为 `Invalid "set context key is empty"`）；`create` 时 value 为空 `Invalid "create context key '<key>' failed: value is empty"`、键已存在 `Failed "context key '<key>' already exists"`；`set` 时键不存在 `Failed "context key '<key>' does not exist"`、值类型与现值不一致 `Failed "context type not match: key '<key>' expect '<存量>', got '<提交>'"`、value 为空 `Invalid`、被 checker 拦截 `Failed "set context key '<key>' rejected by checker"`；`destroy` 空 key `Invalid "context key is empty"`、键不存在 `Failed "context key '<key>' does not exist"`。
- **组名约定**：`.group()` 省略即缺省 `"main"`；空组名 `Invalid "group name is empty"`（WARN 诊断）。非 main 组首次寻址即惰性自动创建（组名唯一、同名寻址复用同组、**不支持销毁组**，组表只增不减），异步监控池每组自持（单线程）。
- **诊断契约**（经 `yomk._diag` 输出，与 C++ 对齐）：空组名/空键/空值/键不存在/类型错配/checker 拒绝/监控池停止后投递丢弃走 `WARN`（文案携具体键值与期望/实际类型名）；同步 monitor 回调异常 `ERROR`；裸链误用载荷不打诊断，统一返 `Failed "pkg is not Context/ContextGroupKey/String/ContextChecker/ContextMonitor, got '<type>'"`。
- **URL 常量**：ctx 域 14 个端点常量与 C++ `YomkContextUrl.h` 一一对应，从 `yomk.url.ctx` 导入（`CTX_CREATE`/`CTX_GET`/`CTX_SET`/`CTX_DESTROY`/`CTX_KEYS`/`CTX_KEY`/`CTX_GROUPS`/`CTX_ALL`/`CTX_TURN_ON_CHECKER` 等）；builder 内部即用这些常量组链，用户自定义注册亦可直接复用。
- **读终结未初始化守卫**：`get()`/`keys()`/`info()`/`groups()`/`all()` 在 server 未初始化时记 `ERROR` 诊断，`get()` 回退默认值、其余返回 `Invalid "YomkServer is not initialized"`；写路径未初始化由请求域收敛（`Invalid "YomkServer is not initialized"`）。
- **读取快照勿原地修改**：`get()` 返回存储对象的引用快照，改动请构造新值再 `.set()`；原地修改与并发读竞争、可能崩溃。
- **键级回调注册前提**：`checker_fn`/`monitor_fn` 只能注册到**已存在**的键（键不存在返回 `Failed "context key '<key>' does not exist"`），即先 `create()` 再注册。
- **Checker 生效条件**：组级开关 ON **且**该键已注册 checker 才参与门控；门控在写锁内执行，**回调内重入 set 会死锁**。
- **Monitor 执行语义**：`.async_(True)` 经该组单线程池按 set 提交序保序送达；同步形态在写锁外即时执行，并发 set 下不保证跨线程序。仅通知"发生了一次 set"并回传该次键值快照（`Context` 包，`key/value/group` 字段），**不保证快照实时性**（读最新值请再走 `get()`）。
- 服务成员函数注册为 checker/monitor 时须用 `self.weak_func(...)` 弱绑定（checker 用 `discard="check"`、monitor 用 `discard="none"`），见 [YomkServer.md](YomkServer.md) 弱绑定一节。

## 内部结构

Context 功能承载在 `ContextGroup`（底层实例组，不继承 `YomkService`）上：键存储、checker/monitor 组级开关与单线程异步监控池均为每组自持，内省快照携带所属组名（`ContextKeyInfo.group`）。`YomkContext` 服务持默认组成员 `m_main`（组名 "main"，成员直取免查找）与自定义组表（dict + 读写锁），负责 14 个端点的装配与组路由：处理函数统一"解出组名 → `find_group` 定位 → 组实例委托"。`find_group` 三段式：空组名返回 `Invalid "group name is empty"`；main 成员直取；非 main 读锁快查自定义组表、未命中写锁内惰性自动创建（组不销毁、表只增不减，裸引用长期安全）。

## 完整示例

完整可运行演示见 `examples/ExampleYomkContext.py`（机械臂场景组合覆盖创建/读取/更新/销毁、checker 拒绝/接受、同步/异步监控、分组与内省，读运行输出即可理解每个调用）。
