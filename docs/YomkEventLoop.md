# YomkEventLoop（线程隔离事件循环）

为特定业务提供具名单线程 FIFO 执行队列，避免锁竞争。同一循环内事件按入队顺序在专用线程中执行，不同循环之间彼此并行。支持停启续跑与队列积压观测。随 `YomkApi.server().init()` 自动启动。全部操作经链式域入口 `YomkApi.event_loop()`：循环名由 `.loop(name)` 定位，回调 / 消息名 / 事件数据 / tag 一律前置累积，终结动作全部无参。

## 链式语法速查

| 场景 | 链 | 终结与返回 |
|---|---|---|
| 启动 | `YomkApi.event_loop().loop(name).handle_fn(fn).msg_name(m)` | `.start()` → `ResStatus`；`handle_fn` 为循环默认处理函数、`msg_name` 为期望消息类型名（均仅内省元数据），可省略 |
| 停止 | `YomkApi.event_loop().loop(name)` | `.stop()` → `ResStatus`（停线程但**保留队列事件**） |
| 销毁 | `YomkApi.event_loop().loop(name)` | `.destroy()` → `ResStatus`（停线程 + 清空队列 + 移出循环表，不可续跑） |
| 非阻塞投递 | `YomkApi.event_loop().msg(data).loop(name).handle_fn(fn).tag(t)` | `.post()` → `ResStatus`（**仅表示入队结果，非执行结果**）；`handle_fn`/`tag` 可省略 |
| 阻塞投递 | 同上 | `.post_wait()` → `ResStatus`（阻塞等待事件执行完成） |
| 内省 | `YomkApi.event_loop().loop(name).tag_count(n)` / — | `.get_info()` 单循环状态 + 队列头 n 条 tag 采样（n 省略 = 缺省 3，`EventLoopQuery` 结构化请求包）/ `.list()` 循环名清单 / `.all()` 全量状态，均 → `YomkResponse` |

## 使用示例

```python
import YomkApi

YomkApi.server().init()

# 启动事件循环，绑定默认处理函数
YomkApi.event_loop().loop("TaskLoop").handle_fn(
    lambda pkg: YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "done")
).start()

# 非阻塞投递（走循环默认处理函数；tag 可选，仅内省可见，用于观察队列）
YomkApi.event_loop().msg("task1").loop("TaskLoop").tag("billing").post()

# 单次事件指定临时处理器（优先于默认处理函数）
YomkApi.event_loop().msg("task2").loop("TaskLoop").handle_fn(
    lambda pkg: YomkApi.YomkResponse(YomkApi.ResStatus.eOk)
).post()

# 阻塞投递
YomkApi.event_loop().loop("TaskLoop").post_wait()

# 查询状态与队列积压（携带队列头 5 条 tag 采样）
YomkApi.event_loop().loop("TaskLoop").tag_count(5).get_info()

# 停止（保留队列）→ 再次 start() 续跑；销毁（清空队列）不可续跑
YomkApi.event_loop().loop("TaskLoop").stop()
YomkApi.event_loop().loop("TaskLoop").start()
YomkApi.event_loop().loop("TaskLoop").destroy()
```

## 注意事项

- 同一循环内事件按 FIFO 顺序执行，不同循环之间彼此并行。
- **同名 `start()` 不重建**：复用既有实例并触发其启动（用于停止后续跑），本次累积的 `handle_fn`/`msg_name` **不生效**；仅新循环名会创建循环并绑定默认处理函数。
- 投递事件未携带 `handle_fn` 时回落到循环默认处理函数；两者皆空则该事件不执行任何回调、直接出队。
- 失败契约（`Failed`）：循环不存在 `"event loop not exist"`；已停止（未销毁）时投递 `"event loop not running, post rejected"`，`post_wait` 同族消息 `"event loop not running, post wait rejected"`。
- `post()` / `post_wait()` 链式终结收敛为三态 `ResStatus`（仅入队/完成标记，非执行结果）；执行结果体现在处理函数返回值中，需要取回时用裸链 `YomkApi.server().url("/YomkEventLoop/post_wait").msg(Event(...)).request()`——`data` 为该 `Event` 对象（`m_eventId` / `m_response` 携带处理响应），或配合共享上下文中转。
- 单查 `get_info()` 的 `data` 为 `EventLoopInfo`：`loop_name`、`running`（worker 运行中）、`pending`（积压事件数）、`has_default_func`、`msg_name`、`next_tags`（队列头起最多 n 条 tag，空串保留）。
- 工作线程内调用 `post_wait` 会触发死锁自检：直接在本线程执行当前事件并返回（诊断日志 `"EventLoop deadlock: post wait in worker thread..."`）。
- 服务成员函数注册为事件处理函数时须用 `self.weak_func(...)` 弱绑定，见 [YomkServer.md](YomkServer.md) 弱绑定一节。

## 完整示例

完整可运行演示见 `examples/ExampleYomkEventLoop.py`（机械臂产线场景组合覆盖 start/post/post_wait/stop/destroy 全生命周期与内省，读运行输出即可理解每个调用）。
