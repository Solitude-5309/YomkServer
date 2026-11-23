# YomkEventLoop(线程隔离事件循环)

为特定业务提供具名单线程 FIFO 执行队列，避免锁竞争。同一循环内事件按入队顺序在专用线程中执行，不同循环之间彼此并行。支持停启续跑与队列积压观测。随 `yomk::server().init()` 自动启动。全部操作经链式域入口 `yomk::event_loop()`：循环名由 `.loop(name)` 定位，回调 / 消息名 / 事件数据 / tag 一律前置累积，终结动作全部无参。

## 适用与不适用

✅ 适合：
- 把一类事件串行化到专用线程（FIFO 保序、无锁竞争）
- 多队列并行分域：不同业务用不同循环，彼此互不阻塞
- 停止后续跑、队列积压观测（`get_info()` 的 `pending`）

❌ 不适合：
- 同一队列内并行处理（设计即串行）
- 在循环线程内等待或阻塞：处理函数与 `.on_done()` 回调都在循环线程执行，阻塞即堵住整条队列

> 前置概念：[消息包](YomkMsg.md)——事件数据以 `YomkMsgPtr` 流转；服务成员函数注册处理函数须弱绑定（见 [服务总线](YomkServer.md)「弱绑定」）。

## 链式语法速查

| 场景 | 链 | 终结与返回 |
|---|---|---|
| 启动 | `yomk::event_loop().loop(name).handle_fn(fn).msg_name(m)` | `.start()` → `ResStatus`；`handle_fn` 为循环默认处理函数（实际参与事件执行，省略后 post 需自带 handle），`msg_name` 为期望消息类型名（仅内省元数据），可省略 |
| 停止 | `yomk::event_loop().loop(name)` | `.stop()` → `ResStatus`（停线程但**保留队列事件**） |
| 销毁 | `yomk::event_loop().loop(name)` | `.destroy()` → `ResStatus`（停线程 + 清空队列 + 移出循环表，不可续跑） |
| 非阻塞投递 | `yomk::event_loop().msg(data).loop(name).handle_fn(fn).tag(t)` | `.post()` → `ResStatus`（**仅表示入队结果，非执行结果**）；`handle_fn`/`tag` 可省略 |
| 异步取结果 | 同上 + `.on_done(cb)` | 链 `.on_done(cb)` 后 `.post()` → 事件执行完毕回调一次，参数即处理函数结果（回调在循环线程执行，四条硬约束见「异步完成回调」） |
| 阻塞投递 | 同上 | `.post_wait()` → `Response`（阻塞等待执行完成，**返回值即处理函数的执行结果**；与 `.on_done()` 互斥） |
| 内省 | `yomk::event_loop().loop(name).tag_count(n)` / — | `.get_info()` 单循环状态 + 队列头 n 条 tag 采样（n 省略 = 缺省 3）/ `.list()` 循环名清单（StringArray 字典序）/ `.all()` 全量状态，均 → `Response` |

> 以下为用法片段（假设框架已 `init()`）；完整可编译、带 main 的程序见文末「完整示例」。

## 使用示例

```cpp
yomk::server().init();

// 启动事件循环，绑定默认处理函数
yomk::event_loop().loop("TaskLoop").handle_fn([](YomkMsgPtr msg) {
    // 在 TaskLoop 专用线程中顺序执行
    return yomk::payload::Response{yomk::payload::ResStatus::Succeeded};
}).start();

// 非阻塞投递（走循环默认处理函数；tag 可选，仅内省可见，用于观察队列）
yomk::event_loop().msg(yomk::msgs::String::shared("task1")).loop("TaskLoop").tag("billing").post();

// 异步取结果：on_done 在事件执行完毕后回调一次，参数即处理函数返回的 Response
// 回调在循环线程执行：只做轻量处理，不得阻塞、不得在回调内对同一循环 post_wait()/stop()/destroy()
yomk::event_loop().msg(yomk::msgs::String::shared("task2")).loop("TaskLoop").tag("billing")
    .on_done([](yomk::payload::Response result) {
        // 生命周期须覆盖执行：成员函数用 weakFunc 弱绑定，对象用 weak_ptr / 值捕获
    })
    .post();

// 单次事件指定临时处理器（优先于默认处理函数）
yomk::event_loop().msg(yomk::msgs::String::shared("task3")).loop("TaskLoop")
    .handle_fn([](YomkMsgPtr msg) { return yomk::payload::Response{yomk::payload::ResStatus::Succeeded}; })
    .post();

// 阻塞投递：返回值即处理函数的执行结果（status/msg/data 原样透传，语义同 request()）
yomk::payload::Response result =
    yomk::event_loop().msg(yomk::msgs::String::shared("task4")).loop("TaskLoop").post_wait();

// 查询状态与队列积压（携带队列头 5 条 tag 采样）
yomk::event_loop().loop("TaskLoop").tag_count(5).get_info();

// 停止（保留队列）→ 再次 start() 续跑；销毁（清空队列）不可续跑
yomk::event_loop().loop("TaskLoop").stop();
yomk::event_loop().loop("TaskLoop").start();
yomk::event_loop().loop("TaskLoop").destroy();
```

## 异步完成回调 `.on_done()`

`post()` 默认丢弃执行结果（只回入队三态）；需要异步获知结果时链 `.on_done(cb)`：事件执行完毕后回调**恰好调用一次**，参数即处理函数返回的 `Response`。处理函数抛出的异常、以及循环销毁时被丢弃的排队事件，也经该回调以 `Failed` 终结（分别为 "event handler threw: ..." 与 "event loop destroyed, pending event discarded"）。`cb` 可空（空 = 不关心结果），仅 `post()` 路径生效。

四条硬约束（违反后果同样是契约的一部分）：

| 约束 | 违反后果 |
|---|---|
| 回调在事件循环的工作线程执行：只做轻量处理，不得阻塞，不得在回调内对同一循环 `post_wait()` / `stop()` / `destroy()` | 阻塞即堵住整条队列（同循环其余事件全部等待）；worker 线程内自等待由框架检测并降级为就地执行 |
| 回调不得抛异常（框架已 try/catch 并记错误日志，用户仍须自约） | 异常被吞掉只留日志，业务错误可能被掩盖 |
| 回调捕获的对象生命周期须覆盖执行：成员函数推荐 `weakFunc` 弱绑定，对象引用用 `weak_ptr` 或值捕获 | 对象先于回调销毁即为悬垂引用（未定义行为） |
| 不得与 `post_wait()` 同时使用 | 同时设置时 `post_wait()` 不发请求，直接返回 `Invalid` "post_wait() takes no on_done(), read the returned Response" |

## 注意事项

- 同一循环内事件按 FIFO 顺序执行，不同循环之间彼此并行。
- **同名 `start()` 不重建**：复用既有实例并触发其启动（用于停止后续跑），本次累积的 `handle_fn`/`msg_name` **不生效**；仅新循环名会创建循环并绑定默认处理函数。
- 投递事件未携带 `handle_fn` 时回落到循环默认处理函数；两者皆空则该事件不执行任何处理函数，以 `Failed` "event has no handler bound" 终结（有 `.on_done()` 时回调收到该结果，`post_wait()` 直接返回该结果）。
- 失败契约：循环不存在 `Failed` "event loop '<循环名>' does not exist"；已停止（未销毁）时投递 `Failed` "event loop '<循环名>' not running, post rejected"（`post_wait` 同族消息）；`destroy()` 丢弃在队事件时，等待者 / `.on_done()` 回调收到 `Failed` "event loop destroyed, pending event discarded"。
- `post()` 的三态仅表示入队/拒绝；执行结果用 `.on_done(cb)`（异步回调）或 `.post_wait()`（同步返回）取回，二者互斥。
- 处理函数抛出的异常由框架收敛为 `Failed` 结果（"event handler threw: ..."）并记错误日志，循环继续存活、后续事件照常执行。
- 单查 `get_info()` 返回 `EventLoopInfo`：`running`（worker 运行中）、`pending`（积压事件数）、`hasDefaultFunc`、`msgName`、`nextTags`（队列头起最多 n 条 tag，空串保留）。
- 服务成员函数注册为事件处理函数时须用 `weakFunc(&Class::Method)` 弱绑定，见 [YomkServer.md](YomkServer.md) 弱绑定一节。

## 常见问题 FAQ

- **`post()` 返回 `Succeeded` 但回调没执行？** 该事件未携带 `handle_fn` 且循环无默认处理函数——两者皆空则该事件不执行任何回调、直接出队（入队三态与执行无关）。
- **`stop()` 后队列里的事件去哪了？** 保留队列，`start()` 续跑；`destroy()` 才清空队列并移出循环表（不可续跑）。
- **同名 `start()` 为什么新配置不生效？** 复用既有实例并触发其启动，本次累积的 `handle_fn` / `msg_name` 不生效；仅新循环名才创建循环并绑定默认处理函数。
- **队列堵了怎么办？** `.loop(name).tag_count(n).get_info()` 观测 `pending` 积压数与队列头 tag 采样，按 tag 定位投递方。
- **`.on_done()` 和 `post_wait()` 能一起用吗？** 不能：二者互斥，`post_wait()` 直接返回 `Invalid` 且不发请求——同步等待读返回值即可。
- **处理函数抛异常会怎样？** 框架捕获并收敛为 `Failed` "event handler threw: <what>"（记错误日志），循环继续存活，`.on_done()` / `post_wait()` 都能收到该结果。

## 相关跳转

- [服务总线](YomkServer.md)：弱绑定（`handle_fn` 为成员函数时注册 `weakFunc`）
- [共享上下文](YomkContext.md)：事件处理结果跨域传递的常用通道

## 完整示例

完整可运行演示见 `examples/ExampleYomkEventLoop.cpp`（9 个机械臂产线场景小示例 `showArmXXX` 组合覆盖全部事件循环 API，日志统一走 YomkLogger 并按 tag 分层，读运行输出即可理解每个调用）。
