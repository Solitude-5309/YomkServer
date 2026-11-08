# YomkEventLoop(线程隔离事件循环)

为特定业务提供具名单线程 FIFO 执行队列，避免锁竞争。同一循环内事件按入队顺序在专用线程中执行，不同循环之间彼此并行。支持停启续跑与队列积压观测。随 `yomk::server().init()` 自动启动。全部操作经链式域入口 `yomk::event_loop()`：循环名由 `.loop(name)` 定位，回调 / 消息名 / 事件数据 / tag 一律前置累积，终结动作全部无参。

## 链式语法速查

| 场景 | 链 | 终结与返回 |
|---|---|---|
| 启动 | `yomk::event_loop().loop(name).handle_fn(fn).msg_name(m)` | `.start()` → `ResStatus`；`handle_fn` 为循环默认处理函数、`msg_name` 为期望消息类型名（均仅内省元数据），可省略 |
| 停止 | `yomk::event_loop().loop(name)` | `.stop()` → `ResStatus`（停线程但**保留队列事件**） |
| 销毁 | `yomk::event_loop().loop(name)` | `.destroy()` → `ResStatus`（停线程 + 清空队列 + 移出循环表，不可续跑） |
| 非阻塞投递 | `yomk::event_loop().msg(data).loop(name).handle_fn(fn).tag(t)` | `.post()` → `ResStatus`（**仅表示入队结果，非执行结果**）；`handle_fn`/`tag` 可省略 |
| 阻塞投递 | 同上 | `.post_wait()` → `ResStatus`（阻塞等待事件执行完成） |
| 内省 | `yomk::event_loop().loop(name).tag_count(n)` / — | `.get_info()` 单循环状态 + 队列头 n 条 tag 采样（n 省略 = 缺省 3）/ `.list()` 循环名清单（StringArray 字典序）/ `.all()` 全量状态，均 → `Response` |

## 使用示例

```cpp
yomk::server().init();

// 启动事件循环，绑定默认处理函数
yomk::event_loop().loop("TaskLoop").handle_fn([](YomkMsgPtr pkg) {
    // 在 TaskLoop 专用线程中顺序执行
    return yomk::payload::Response{yomk::payload::ResStatus::Succeeded};
}).start();

// 非阻塞投递（走循环默认处理函数；tag 可选，仅内省可见，用于观察队列）
yomk::event_loop().msg(yomk::msgs::String::shared("task1")).loop("TaskLoop").tag("billing").post();

// 单次事件指定临时处理器（优先于默认处理函数）
yomk::event_loop().msg(yomk::msgs::String::shared("task2")).loop("TaskLoop")
    .handle_fn([](YomkMsgPtr pkg) { return yomk::payload::Response{yomk::payload::ResStatus::Succeeded}; })
    .post();

// 阻塞投递
yomk::event_loop().loop("TaskLoop").post_wait();

// 查询状态与队列积压（携带队列头 5 条 tag 采样）
yomk::event_loop().loop("TaskLoop").tag_count(5).get_info();

// 停止（保留队列）→ 再次 start() 续跑；销毁（清空队列）不可续跑
yomk::event_loop().loop("TaskLoop").stop();
yomk::event_loop().loop("TaskLoop").start();
yomk::event_loop().loop("TaskLoop").destroy();
```

## 注意事项

- 同一循环内事件按 FIFO 顺序执行，不同循环之间彼此并行。
- **同名 `start()` 不重建**：复用既有实例并触发其启动（用于停止后续跑），本次累积的 `handle_fn`/`msg_name` **不生效**；仅新循环名会创建循环并绑定默认处理函数。
- 投递事件未携带 `handle_fn` 时回落到循环默认处理函数；两者皆空则该事件不执行任何回调、直接出队。
- 失败契约：循环不存在 `Failed` "event loop not exist"；已停止（未销毁）时投递 `Failed` "event loop not running, post rejected"（`post_wait` 同族消息）。
- `post()` 的三态仅表示入队/拒绝；执行结果只体现在处理函数内部，取回结果请配合共享上下文或回调投递到其他通道。
- 单查 `get_info()` 返回 `EventLoopInfo`：`m_running`（worker 运行中）、`m_pending`（积压事件数）、`m_hasDefaultFunc`、`m_msgName`、`m_nextTags`（队列头起最多 n 条 tag，空串保留）。
- 服务成员函数注册为事件处理函数时须用 `weakFunc(&Class::Method)` 弱绑定，见 [YomkServer.md](YomkServer.md) 弱绑定一节。

## 完整示例

完整可运行演示见 `examples/ExampleYomkEventLoop.cpp`（8 个机械臂产线场景小示例 `showArmXXX` 组合覆盖全部事件循环 API，日志统一走 YomkLogger 并按 tag 分层，读运行输出即可理解每个调用）。
