# YomkEventLoop(线程隔离事件循环)

为特定业务提供具名单线程 FIFO 执行队列，避免锁竞争。同一循环内事件按入队顺序在专用线程中执行，不同循环之间彼此并行。支持停启续跑与队列积压观测。随 `YOMK_INIT()` 自动启动。

## API 速查

| API | 说明 |
|---|---|
| `YOMK_EVENTLOOP_START(name)` | 启动事件循环 |
| `YOMK_EVENTLOOP_START(name, defaultFunc, MsgName)` | 启动并指定默认处理函数与消息类型名元数据 |
| `YOMK_EVENTLOOP_POST(name, pkg, func, tag)` | 非阻塞投递异步任务；tag 可选，仅内省可见，用于观察队列 |
| `YOMK_EVENTLOOP_POST_WAIT(name, pkg, func, tag)` | 阻塞投递任务，等待任务在循环中执行完毕返回结果 |
| `YOMK_EVENTLOOP_INFO_LOOP(name)` | 查询循环状态与队列积压 |
| `YOMK_EVENTLOOP_INFO_LOOP(name, n)` | 同上，第二个参数可选：列出队首 N 个事件的 tag |
| `YOMK_EVENTLOOP_INFO_LOOPS()` / `YOMK_EVENTLOOP_INFO_ALL()` | 循环清单 / 全量状态自省（EventLoopInfoList 结构化包） |
| `YOMK_EVENTLOOP_STOP(name)` | 停止（停线程但保留队列事件，再次 START 续跑） |
| `YOMK_EVENTLOOP_DESTROY(name)` | 销毁（停线程并清空队列、移出循环表，不可续跑） |

## 使用示例

```cpp
// 启动事件循环（对已存在名称再次 START 为幂等重启）
YOMK_EVENTLOOP_START("TaskLoop");

// 非阻塞投递异步任务（tag 可选：仅内省可见，用于观察队列）
YOMK_EVENTLOOP_POST("TaskLoop", YomkMkPtr(String, "task1"), [](YomkPkgPtr pkg) {
    // 在 TaskLoop 专用线程中顺序执行
    return YomkResponse{YomkResponse::eOk};
}, "billing");

// 阻塞投递任务（等待任务在循环中执行完毕返回结果）
YomkResponse resp = YOMK_EVENTLOOP_POST_WAIT("TaskLoop", nullptr, [](YomkPkgPtr) {
    return YomkResponse{YomkResponse::eOk, "done"};
});

// 查询循环状态与队列积压（第二个参数可选：列出队首 N 个事件的 tag）
YOMK_EVENTLOOP_INFO_LOOP("TaskLoop");  // msg 如 "TaskLoop running:on pending:0 defaultFunc:on nextNEventTag(3): "

// 停止（停线程但保留队列事件，再次 START 续跑）与销毁（停线程并清空队列、移出循环表，不可续跑）
YOMK_EVENTLOOP_STOP("TaskLoop");
YOMK_EVENTLOOP_DESTROY("TaskLoop");
```

## 注意事项

- 同一循环内事件按 FIFO 顺序执行，不同循环之间彼此并行。
- 对已存在名称再次 `YOMK_EVENTLOOP_START` 为幂等重启。
- 停止后投递返回 `eNo=1`（event loop not running）；销毁后操作返回 `eNo=1`（event loop not exist）。
- 服务成员函数注册为事件处理函数时须用 `weakFunc(&Class::Method)` 弱绑定，见 [YomkServer.md](YomkServer.md) 弱绑定一节。

## 完整示例

完整可运行演示见 `Examples/ExampleYomkEventLoop.cpp`（8 个机械臂产线场景小示例 `showArmXXX` 组合覆盖全部事件循环 API，日志统一走 YomkLogger 并按 tag 分层，读运行输出即可理解每个调用）。
