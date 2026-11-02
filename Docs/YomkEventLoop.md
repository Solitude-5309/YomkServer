# YomkEventLoop(线程隔离事件循环)

为特定业务提供具名单线程 FIFO 执行队列，避免锁竞争。同一循环内事件按入队顺序在专用线程中执行，不同循环之间彼此并行。支持停启续跑与队列积压观测。随 `YomkAPI::init()` 自动启动。

## API 速查

| API | 说明 |
|---|---|
| `YomkAPI::startEventLoop(name)` | 启动事件循环 |
| `YomkAPI::startEventLoop(name, defaultFunc, "MsgName")` | 启动并指定默认处理函数与消息类型名元数据 |
| `YomkAPI::postEvent(name, pkg, func, tag)` | 非阻塞投递异步任务；tag 可选，仅内省可见，用于观察队列 |
| `YomkAPI::postEventAndWait(name, pkg, func, tag)` | 阻塞投递任务，等待任务在循环中执行完毕返回结果 |
| `YomkAPI::getEventLoopInfo(name)` | 查询循环状态与队列积压 |
| `YomkAPI::getEventLoopInfo(name, n)` | 同上，第二个参数可选：列出队首 N 个事件的 tag |
| `YomkAPI::listEventLoops()` / `YomkAPI::getAllEventLoopInfo()` | 循环清单 / 全量状态自省（EventLoopInfoList 结构化包） |
| `YomkAPI::stopEventLoop(name)` | 停止（停线程但保留队列事件，再次 START 续跑） |
| `YomkAPI::destroyEventLoop(name)` | 销毁（停线程并清空队列、移出循环表，不可续跑） |

## 使用示例

```cpp
// 启动事件循环（对已存在名称再次 START 为幂等重启）
YomkAPI::startEventLoop("TaskLoop");

// 非阻塞投递异步任务（tag 可选：仅内省可见，用于观察队列）
YomkAPI::postEvent("TaskLoop", yomkmsg::String::shared("task1"), [](YomkPkgPtr pkg) {
    // 在 TaskLoop 专用线程中顺序执行
    return YomkResponse{YomkResponse::ResStatus::Successed};
}, "billing");

// 阻塞投递任务（等待任务在循环中执行完毕返回结果）
YomkResponse resp = YomkAPI::postEventAndWait("TaskLoop", nullptr, [](YomkPkgPtr) {
    return YomkResponse{YomkResponse::ResStatus::Successed, "done"};
});

// 查询循环状态与队列积压（第二个参数可选：列出队首 N 个事件的 tag）
YomkAPI::getEventLoopInfo("TaskLoop");  // msg 如 "TaskLoop running:on pending:0 defaultFunc:on nextNEventTag(3): "

// 停止（停线程但保留队列事件，再次 START 续跑）与销毁（停线程并清空队列、移出循环表，不可续跑）
YomkAPI::stopEventLoop("TaskLoop");
YomkAPI::destroyEventLoop("TaskLoop");
```

## 注意事项

- 同一循环内事件按 FIFO 顺序执行，不同循环之间彼此并行。
- 对已存在名称再次 `YomkAPI::startEventLoop` 为幂等重启。
- 停止后投递返回 `ResStatus::Failed=1`（event loop not running）；销毁后操作返回 `ResStatus::Failed=1`（event loop not exist）。
- 服务成员函数注册为事件处理函数时须用 `weakFunc(&Class::Method)` 弱绑定，见 [YomkServer.md](YomkServer.md) 弱绑定一节。

## 完整示例

完整可运行演示见 `Examples/ExampleYomkEventLoop.cpp`（8 个机械臂产线场景小示例 `showArmXXX` 组合覆盖全部事件循环 API，日志统一走 YomkLogger 并按 tag 分层，读运行输出即可理解每个调用）。
