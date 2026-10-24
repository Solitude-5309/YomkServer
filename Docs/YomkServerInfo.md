# YomkServerInfo(服务自省)

服务自省：运行期查询服务拓扑、功能函数清单与期望消息类型，方便调试与运维。随 `YOMK_INIT()` 自动启动，开箱即用。

## API 速查

| API | 说明 |
|---|---|
| `YOMK_SERVER_INFO_SERVICES()` | 查询所有服务名称 |
| `YOMK_SERVER_INFO_FUNCTIONS("/服务名")` | 查询指定服务的全部功能函数 |
| `YOMK_SERVER_INFO_FUNCTION("/服务名/函数名")` | 单查指定功能函数元信息（结构化 FuncInfo 包：函数名/期望消息类型名/函数说明） |
| `YOMK_SERVER_INFO_ALL()` | 查询框架全量服务及功能函数拓扑（ServerServiceInfoList 结构化包：m_serviceName 服务名 + m_funcs 函数元信息列表） |

清单类端点（SERVICES/FUNCTIONS）以文本行经 `m_data`（StringArray 包）返回；单查端点 `FUNCTION` 命中返回结构化 FuncInfo 包（`m_msg` 恒 "ok"，数据在 `m_data` 字段）；聚合端点 `ALL` 返回 ServerServiceInfoList 结构化层级包（`m_msg` 恒 "ok"）。

## 使用示例

```cpp
YOMK_SERVER_INFO_SERVICES();                   // 全部服务名清单
YOMK_SERVER_INFO_FUNCTIONS("/MyService");      // 指定服务的功能函数清单
YOMK_SERVER_INFO_FUNCTION("/MyService/hello"); // 单个功能函数元信息
YOMK_SERVER_INFO_ALL();                        // 全量服务拓扑
```

## 说明

- 期望消息类型名通过三参宏声明：`YomkInstallFunc(funcName, func, MsgName)`、`YOMK_FUNCTIONPOOL_REGISTER(name, func, MsgName)`、`YOMK_EVENTLOOP_START(name, func, MsgName)`，仅作内省元数据，不影响调用。
- 函数说明通过四参宏声明（第四参自由字符串，内容使用者自定义，框架透传不解析）：`YomkInstallFunc` 与 `YOMK_FUNCTIONPOOL_REGISTER` 均支持。`/function` 与函数池 `/YomkFunctionPool/name` 单查端点命中均返回结构化 FuncInfo 包（`m_msg` 恒 "ok"）：`m_msgName` 承载期望消息类型名、`m_description` 承载函数说明（未声明时为空）。
- 各内置模块另有模块级自省 API，详见各模块文档：

| 模块 | 自省 API |
|---|---|
| 日志 | `YOMK_LOGGER_INFO_LOGGERS("console")`、`YOMK_LOGGER_INFO_LOGGER("app")`、`YOMK_LOGGER_INFO_ALL()`（见 [YomkLogger.md](YomkLogger.md)） |
| 上下文 | `YOMK_CONTEXT_INFO_KEYS()`、`YOMK_CONTEXT_INFO_KEY("config")`、`YOMK_CONTEXT_INFO_ALL()`（见 [YomkContext.md](YomkContext.md)） |
| 事件循环 | `YOMK_EVENTLOOP_INFO_LOOPS()`、`YOMK_EVENTLOOP_INFO_LOOP("TaskLoop")`、`YOMK_EVENTLOOP_INFO_ALL()`（见 [YomkEventLoop.md](YomkEventLoop.md)） |
| 函数池 | `YOMK_FUNCTIONPOOL_INFO_NAMES()`、`YOMK_FUNCTIONPOOL_INFO_NAME("calcSum")`、`YOMK_FUNCTIONPOOL_INFO_ALL()`（见 [YomkFunctionPool.md](YomkFunctionPool.md)） |

## 完整示例

完整可运行演示见 `Examples/ExampleYomkServerInfo.cpp`（7 步覆盖服务清单、函数清单与注册元数据、单查元信息、全量拓扑、运行期实时视图与关闭后守卫，读运行输出即可理解每个调用）。
