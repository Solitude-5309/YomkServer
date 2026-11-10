# YomkContext(共享上下文)

跨服务强类型键值存储，内置变更前校验门控（Checker）与变更后监听（Monitor，同步/异步），支持分组隔离与运行期内省。随 `yomk::server().init()` 自动启动。全部操作经链式域入口 `yomk::ctx()`：`key/group/value` 累积定位目标，写路径终结（`create/set/destroy` 等）返回三态 `ResStatus`，类型化读终结 `get()` 返回 `shared_ptr<T>`。

## 适用与不适用

✅ 适合：
- 跨服务共享强类型键值配置（创建/替换/类型化读）
- 变更前校验（Checker）与变更后联动（Monitor，同步/异步保序）

❌ 不适合：
- 需要销毁分组（组表只增不减，不支持销毁组）
- 局部字段更新（写路径是整体替换，无增量写）

> 前置概念：[消息包](YomkMsg.md)——上下文值与类型化读均基于强类型消息包；服务成员函数注册回调须弱绑定，见 [服务总线](YomkServer.md)「弱绑定」。

## 链式语法速查

| 场景 | 链 | 终结与返回 |
|---|---|---|
| 创建 | `yomk::ctx().key(k).group(g).value(pkg)` | `.create()` → `ResStatus`（g 省略 = 缺省 `"main"`） |
| 设置（整体替换） | `yomk::ctx().key(k).group(g).value(pkg)` | `.set()` → `ResStatus` |
| 类型化读 | `yomk::ctx().key(k).group(g).msg<T>()` | `.with_default(d).get()` → `shared_ptr<T>`；`.with_default` 可省略（未命中回退 `nullptr`） |
| 销毁键 | `yomk::ctx().key(k).group(g)` | `.destroy()` → `ResStatus` |
| 键级校验门控 | `yomk::ctx().key(k).group(g).checker_fn(fn)` | `.apply()` → `ResStatus` |
| 键级变更监听 | `yomk::ctx().key(k).group(g).monitor_fn(fn).async(on)` | `.apply()` → `ResStatus`；`.async(on)` 可省略 = 同步 |
| 组级开关 | `yomk::ctx().group(g).checker()` / `.monitor()` | `.enable()` / `.disable()` → `ResStatus`（**建组默认关**，须显式开启才生效；回调保留不删除） |
| 内省 | `yomk::ctx().group(g)` / `.key(k).group(g)` / — | `.keys()` 组内键名 / `.info()` 单键元信息 / `.groups()` 全部组名 / `.all()` 跨组聚合，均 → `Response` |

键级回调（`checker_fn` / `monitor_fn`）与组级开关分面（`checker()` / `monitor()`）按命名分层，`apply()` 未累积任何回调时返回 `Invalid`。

> 以下为用法片段（假设框架已 `init()`）；完整可编译、带 main 的程序见文末「完整示例」。

## 使用示例

```cpp
yomk::server().init();

// 1. 创建与设置
yomk::ctx().key("config").value(yomk::msgs::String::shared("init_value")).create();
yomk::ctx().key("config").value(yomk::msgs::String::shared("new_value")).set();

// 2. 类型化读（只读快照；未命中/类型不符/未初始化回退默认值）
auto val = yomk::ctx().key("config").msg<yomk::msgs::String>().with_default(nullptr).get();

// 3. 变更前校验门控：只能注册到已存在的键；组开关默认关，注册后须显式 enable
yomk::ctx().key("config").checker_fn([](const yomk::payload::Context& ctx) {
    // Accept 放行本次 set；Reject 拦截（set 返回 Failed "checker reject set context"，monitor 不触发）
    return yomk::payload::CheckStatus::Accept;
}).apply();
yomk::ctx().group("main").checker().enable();

// 4. 变更后监听（可注册多个，追加式）
yomk::ctx().key("config").monitor_fn([](const yomk::payload::Context& ctx) {
    // 收到变更通知（本次 set 的键值快照）
}).apply();                                            // 缺省同步执行
yomk::ctx().group("main").monitor().enable();

// 5. 内省与销毁
yomk::ctx().keys();                                    // 组内键名清单（StringArray，缺省 main 组）
yomk::ctx().key("config").info();                      // 单键元信息（ContextKeyInfo，含所属组 group）
yomk::ctx().groups();                                  // 全部组名（StringArray）
yomk::ctx().all();                                     // 跨组聚合（ContextGroupInfoList：组 → 键元信息）
yomk::ctx().key("config").destroy();                   // 删除键
```

## 注意事项

> ⚠️ **Warning**：`get()` 返回只读快照——原地修改与并发读竞争、可能崩溃；改动请构造新消息对象再 `.set()`。

- **写路径失败契约**（文案携带具体键名与期望/实际类型名；三态约定：入参非法= `Invalid`，业务未命中/冲突= `Failed`）：`create` 键名为空 `Invalid` "context key is empty"、value 为空 `Invalid` "create context key '<键>' failed: value is empty"、键已存在 `Failed` "context key '<键>' already exists"；`set` 键不存在 `Failed` "context key '<键>' does not exist"、类型不匹配 `Failed` "context type not match: key '<键>' expect '<期望类型>', got '<实际类型>'"、被 checker 拦截 `Failed` "set context key '<键>' rejected by checker"、value 为空 `Invalid` "set context key '<键>' failed: value is empty"。
- **组名约定**：`.group()` 省略即缺省 `"main"`；空组名 `Invalid` "group name is empty"（入参非法）。非 main 组首次寻址即惰性自动创建（组名唯一、同名寻址复用同组、**不支持销毁组**，组表只增不减），异步监控池每组自持（单线程）。
- **读取快照勿原地修改**：`get()` 返回只读快照，改动请构造新消息对象再 `.set()`；原地修改与并发读竞争、可能崩溃。
- **键级回调注册前提**：`checker_fn`/`monitor_fn` 只能注册到**已存在**的键（键不存在返回 `Failed` "context key '<键>' does not exist"），即先 `create()` 再注册。
- **Checker 生效条件**：组级开关 ON **且**该键已注册 checker 才参与门控；门控在写锁内执行，**回调内重入 set 会死锁**。
- **Monitor 执行语义**：`.async(true)` 经该组单线程池按 set 提交序保序送达；同步形态在写锁外即时执行，并发 set 下不保证跨线程序。仅通知"发生了一次 set"并回传该次键值快照，**不保证快照实时性**（读最新值请再走 `get()`）。
- **类型化读自带校验**：`get()` 按运行期类型名比对 + 指针转换，`T` 须为 `YomkMsg` 派生（`static_assert` 编译期把关）。
- 服务成员函数注册为 checker/monitor 时须用 `weakFunc(&Class::Method)` 弱绑定，见 [YomkServer.md](YomkServer.md) 弱绑定一节。

## 内部结构

Context 功能承载在 `ContextGroup`（底层实例组，不继承 YomkService）上：键存储、checker/monitor 组级开关与单线程异步监控池均为每组自持，内省快照携带所属组名（`ContextKeyInfo.group`）。`YomkContext` 服务持默认组成员 `m_main`（组名 "main"，成员直取免查找）与自定义组表（map + 读写锁），负责 14 个端点的装配与组路由：处理函数统一"解出组名 → `findGroup` 定位 → 组实例委托"。`findGroup` 三段式：空组名返回 `Invalid` "group name is empty"（入参非法）；main 成员直取；非 main 读锁快查自定义组表、未命中写锁内惰性自动创建（锁外裸指针长期安全——组不销毁、表只增不减）。

## 常见问题 FAQ

- **checker 注册了却不生效？** 需组级开关 ON **且**该键已注册 checker，两者同时满足才参与门控（组开关建组默认关，须显式 enable）。
- **monitor 同步回调里能再 set 吗？** 不能：同步回调虽在写锁外执行，但回调内重入 set 会递归不收敛（栈溢出）；级联状态机/回写请改用 `.async(true)`——经该组单线程池按提交序保序送达。
- **checker 回调内能读上下文吗？** 不能：门控在写锁内执行，回调内重入 get/set 等写路径会死锁。
- **组什么时候创建？** 非 main 组首次寻址即惰性自动创建；`.group()` 省略即缺省 `"main"` 组。

## 相关跳转

- [服务总线](YomkServer.md)：弱绑定（checker/monitor 回调注册 `weakFunc`）、服务删除后回调判活丢弃
- [消息包](YomkMsg.md)：上下文值与类型化读的消息类型约定
- 内部实现机制（组路由、锁序、监控池）：见上文「内部结构」一节

## 完整示例

完整可运行演示见 `examples/ExampleYomkContext.cpp`（8 个机械臂场景小示例 `showArmXXX` 组合覆盖全部上下文 API，日志统一走 YomkLogger 并按 tag 分层，读运行输出即可理解每个调用）。
