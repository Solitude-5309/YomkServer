# 框架内置服务 installFunc 调用清单

- 框架版本：Yomk 3.7.10（`project(Yomk VERSION 3.7.10)`）
- 范围：仅 5 个框架内置服务（YomkContext / YomkEventLoop / YomkFunctionPool / YomkLogger / YomkServerInfo），共 48 处 `installFunc` 调用
- 说明：`msgName` 列为 installFunc 第三参实参，实际代码一律传 `yomk::msgs::Xxx::kName` 常量（下表简写为 `Xxx::kName`），`""` 表示显式传空、不声明期望消息类型
- `response.data`：功能函数返回的 `yomk::payload::Response{status, msg, data}` 中的 `data`（类型 `YomkMsgPtr`），即响应携带的返回数据包

## 约定

| 字段 | 含义 |
|---|---|
| 路由 | 服务名 + 功能名，完整 URL |
| 目标函数 | 注册进本服务 funcMap 的成员函数 |
| msgName | 期望消息类型名（仅内省元数据，不拦截调用） |
| response.data | 成功响应携带的数据包类型；"无" 表示 data 为空 |
| 理由 | 端点语义（取自 installFunc 第四参语义描述常量的 Semantic 行） |

---

## 1. YomkContext（14 处）

来源：[YomkContext.cpp](../src/ctx/YomkContext.cpp) · 描述常量：[YomkContextUrl.h](../include/yomk/ctx/YomkContextUrl.h)

| 路由 | 目标函数 | msgName（第三参） | response.data | 理由 |
|---|---|---|---|---|
| `/YomkContext/create` | `create` | `Context::kName` | 无 | 在指定组（缺省 main）创建上下文键并写入值包 |
| `/YomkContext/destroy` | `destroy` | `ContextGroupKey::kName` | 无 | 删除指定组中的上下文键 |
| `/YomkContext/get` | `get` | `Context::kName` | 键的值包（未命中回填默认值包） | 读取上下文键当前值 |
| `/YomkContext/set` | `set` | `Context::kName` | 无 | 更新已存在上下文键的值（类型须与原值一致） |
| `/YomkContext/turn_on_checker` | `turnOnChecker` | `String::kName` | 无 | 开启指定组 checker 池总闸（组级写前校验生效） |
| `/YomkContext/turn_off_checker` | `turnOffChecker` | `String::kName` | 无 | 关闭指定组 checker 池总闸 |
| `/YomkContext/turn_on_monitor` | `turnOnMonitor` | `String::kName` | 无 | 开启指定组 monitor 池总闸（写后监视生效） |
| `/YomkContext/turn_off_monitor` | `turnOffMonitor` | `String::kName` | 无 | 关闭指定组 monitor 池总闸 |
| `/YomkContext/set_checker` | `setChecker` | `ContextChecker::kName` | 无 | 为已存在键安装写前校验回调 |
| `/YomkContext/set_monitor` | `setMonitor` | `ContextMonitor::kName` | 无 | 为已存在键安装写后监视回调 |
| `/YomkContext/groups` | `groups` | `""`（未声明） | `StringArray` 包 | 返回全部组名列表（main 恒首位，自定义组字典序） |
| `/YomkContext/keys` | `keys` | `String::kName` | `StringArray` 包 | 返回指定组全部键名列表（字典序） |
| `/YomkContext/key` | `keyInfo` | `ContextGroupKey::kName` | `ContextKeyInfo` 包 | 查询单键内省元信息快照 |
| `/YomkContext/all` | `listAll` | `""`（未声明） | `ContextGroupInfoList` 包 | 全部组及其全部键的内省元信息快照 |

---

## 2. YomkEventLoop（8 处）

来源：[YomkEventLoop.cpp](../src/event_loop/YomkEventLoop.cpp) · 描述常量：[YomkEventLoopUrl.h](../include/yomk/event_loop/YomkEventLoopUrl.h)

| 路由 | 目标函数 | msgName（第三参） | response.data | 理由 |
|---|---|---|---|---|
| `/YomkEventLoop/start` | `start` | `EventLoop::kName` | 无 | 创建事件循环并启动专用消费线程（同名已存在则复用） |
| `/YomkEventLoop/stop` | `stop` | `String::kName` | 无 | 停止事件循环消费线程（实例保留，可重新 start） |
| `/YomkEventLoop/post` | `post` | `Event::kName` | 无 | 向事件循环投递事件，FIFO 入队异步执行 |
| `/YomkEventLoop/post_wait` | `postWait` | `Event::kName` | `Event` 包（携带执行结果） | 投递事件并阻塞至其执行完成 |
| `/YomkEventLoop/destroy` | `destroy` | `String::kName` | 无 | 销毁事件循环并释放资源（不可重启） |
| `/YomkEventLoop/loops` | `loops` | `""`（未声明） | `StringArray` 包 | 返回全部事件循环名列表 |
| `/YomkEventLoop/loop` | `loopInfo` | `EventLoopQuery::kName` | `EventLoopInfo` 包 | 查询单循环内省快照（运行态/积压数/类型名/tag 采样） |
| `/YomkEventLoop/all` | `listAll` | `""`（未声明） | `EventLoopInfoList` 包 | 全部事件循环内省快照（tag 采样 3 条） |

---

## 3. YomkFunctionPool（6 处）

来源：[YomkFunctionPool.cpp](../src/fn_pool/YomkFunctionPool.cpp) · 描述常量：[YomkFunctionPoolUrl.h](../include/yomk/fn_pool/YomkFunctionPoolUrl.h)

| 路由 | 目标函数 | msgName（第三参） | response.data | 理由 |
|---|---|---|---|---|
| `/YomkFunctionPool/register` | `registerFunction` | `Function::kName` | 无 | 向函数池注册可调用函数（同名重复注册为热替换） |
| `/YomkFunctionPool/unregister` | `unRegisterFunction` | `String::kName` | 无 | 注销函数池中的函数 |
| `/YomkFunctionPool/call` | `callFunction` | `CallFunction::kName` | 由目标函数决定（响应原样回传） | 调用已注册函数，透传其携带的负载包 |
| `/YomkFunctionPool/names` | `funcNames` | `""`（未声明） | `StringArray` 包 | 返回全部注册函数名列表（字典序） |
| `/YomkFunctionPool/name` | `funcInfo` | `String::kName` | `FuncInfo` 包 | 查询单函数元信息（期望消息类型 + 函数说明） |
| `/YomkFunctionPool/all` | `listAll` | `""`（未声明） | `FuncInfoList` 包 | 全量函数元信息快照 |

---

## 4. YomkLogger（16 处）

来源：[YomkLogger.cpp](../src/log/YomkLogger.cpp) · 描述常量：[YomkLoggerUrl.h](../include/yomk/log/YomkLoggerUrl.h)

| 路由 | 目标函数 | msgName（第三参） | response.data | 理由 |
|---|---|---|---|---|
| `/YomkLogger/set_console_log_proxy` | `setConsoleLogProxy` | `ConsoleLogProxy::kName` | 无 | 安装控制台输出代理（回调返回 true 短路默认输出） |
| `/YomkLogger/set_file_log_proxy` | `setFileLogProxy` | `FileLogProxy::kName` | 无 | 安装文件输出代理（回调返回 true 短路默认落盘） |
| `/YomkLogger/console_log` | `consoleLog` | `Log::kName` | 无 | 写控制台日志（代理短路优先，否则按级别门控输出） |
| `/YomkLogger/create_file_logger` | `createFileLogger` | `LogFile::kName` | 无 | 创建文件日志器并初始化落盘文件（四级日志先入缓冲，WRITE 落盘） |
| `/YomkLogger/file_log` | `fileLog` | `Log::kName` | 无 | 向指定文件日志器缓冲写入一条日志（WRITE 前不落盘） |
| `/YomkLogger/write_file_log` | `writeFileLog` | `String::kName` | 无 | 将指定文件日志器的缓冲内容落盘并清空缓冲 |
| `/YomkLogger/delete_logger` | `deleteLogger` | `String::kName` | 无 | 删除指定文件日志器 |
| `/YomkLogger/off_console_log_by_level` | `offConsoleLogByLevel` | `Log::kName` | 无 | 关闭 Log 包指定级别的控制台输出门控 |
| `/YomkLogger/on_console_log_by_level` | `onConsoleLogByLevel` | `Log::kName` | 无 | 恢复 Log 包指定级别的控制台输出门控 |
| `/YomkLogger/loggers` | `loggers` | `String::kName` | `StringArray` 包 | 查询日志器名列表（入参 "console" 或 "file" 选表） |
| `/YomkLogger/logger` | `loggerInfo` | `LoggerQuery::kName` | `LoggerInfo` 包 | 按类型 + 名查询单日志器内省 |
| `/YomkLogger/all` | `listAll` | `""`（未声明） | `LoggerInfoList` 包 | 全量日志器内省（console 段在前、file 段在后） |
| `/YomkLogger/console_global_state` | `consoleGlobalState` | `""`（未声明） | `LoggerState` 包 | 控制台全局主闸态内省 |
| `/YomkLogger/off_file_log_by_level` | `offFileLogByLevel` | `Log::kName` | 无 | 关闭 Log 包指定级别的文件输出门控 |
| `/YomkLogger/on_file_log_by_level` | `onFileLogByLevel` | `Log::kName` | 无 | 恢复 Log 包指定级别的文件输出门控 |
| `/YomkLogger/file_global_state` | `fileGlobalState` | `""`（未声明） | `LoggerState` 包 | 文件全局主闸态内省 |

---

## 5. YomkServerInfo（4 处）

来源：[YomkServerInfo.cpp](../src/srv_info/YomkServerInfo.cpp) · 描述常量：[YomkServerInfoUrl.h](../include/yomk/srv_info/YomkServerInfoUrl.h)

| 路由 | 目标函数 | msgName（第三参） | response.data | 理由 |
|---|---|---|---|---|
| `/YomkServerInfo/services` | `listServices` | `""`（未声明） | `StringArray` 包 | 返回全部服务名列表（含框架内置服务） |
| `/YomkServerInfo/functions` | `listFunctions` | `String::kName` | `FuncInfoList` 包 | 查询指定服务的函数元信息清单 |
| `/YomkServerInfo/function` | `functionInfo` | `String::kName` | `FuncInfo` 包 | 按函数 URL 查询单函数元信息 |
| `/YomkServerInfo/all` | `listAll` | `""`（未声明） | `ServerServiceInfoList` 包 | 全拓扑快照（服务 → 函数元信息） |

---

## 汇总

| 服务 | installFunc 调用数 | 有返回 data 的端点 |
|---|---|---|
| YomkContext | 14 | 5（/get、/groups、/keys、/key、/all） |
| YomkEventLoop | 8 | 4（/post_wait、/loops、/loop、/all） |
| YomkFunctionPool | 6 | 4（/call、/names、/name、/all） |
| YomkLogger | 16 | 5（/loggers、/logger、/all、/console_global_state、/file_global_state） |
| YomkServerInfo | 4 | 4（全部） |
| **合计** | **48** | **22** |

> 注：`msgName` 仅为内省元数据，不参与运行期类型校验；类型校验发生在 `unpack` / 上下文 `get()`。响应数据统一挂在 `Response.data`（`YomkMsgPtr`），失败时通常为空。
