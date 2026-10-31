# YomkServerInfo(服务自省)

服务自省：运行期查询服务拓扑、功能函数清单与期望消息类型，方便调试与运维。随 `YomkAPI::init()` 自动启动，开箱即用。

## API 速查

| API | 说明 |
|---|---|
| `YomkAPI::listServices()` | 查询所有服务名称 |
| `YomkAPI::listServiceFunctions("/服务名")` | 查询指定服务的全部功能函数（FuncInfoList 结构化清单：m_funcName/m_msgName/m_description） |
| `YomkAPI::getServiceFunctionInfoByUrl("/服务名/函数名")` | 单查指定功能函数元信息（结构化 FuncInfo 包：函数名/期望消息类型名/函数说明） |
| `YomkAPI::getAllServerInfo()` | 查询框架全量服务及功能函数拓扑（ServerServiceInfoList 结构化包：m_serviceName 服务名 + m_funcs 函数元信息列表） |

清单类端点 `SERVICES` 以文本行经 `m_data`（StringArray 包）返回；结构化端点 `FUNCTIONS` 返回 FuncInfoList 清单（`m_msg` 恒 "ok"，不存在服务为空清单）；单查端点 `FUNCTION` 命中返回结构化 FuncInfo 包（`m_msg` 恒 "ok"，数据在 `m_data` 字段）；聚合端点 `ALL` 返回 ServerServiceInfoList 结构化层级包（`m_msg` 恒 "ok"）。

## 使用示例

```cpp
YomkAPI::listServices();                   // 全部服务名清单
YomkAPI::listServiceFunctions("/MyService");      // 指定服务的功能函数清单（FuncInfoList）
YomkAPI::getServiceFunctionInfoByUrl("/MyService/hello"); // 单个功能函数元信息
YomkAPI::getAllServerInfo();                        // 全量服务拓扑
```

## 说明

- 期望消息类型名通过末位参数声明：`installFunc("/x", &C::m, "MsgName")`、`YomkAPI::FUNCTIONPOOL_REGISTER(name, func, "MsgName")`、`YomkAPI::EVENTLOOP_START(name, func, "MsgName")`，仅作内省元数据，不影响调用。
- 函数说明通过第四参声明（末参自由字符串，内容使用者自定义，框架透传不解析）：`installFunc("/x", &C::m, "MsgName", "说明")` 与 `YomkAPI::FUNCTIONPOOL_REGISTER` 四参形式均支持。`/function` 与函数池 `/YomkFunctionPool/name` 单查端点命中均返回结构化 FuncInfo 包（`m_msg` 恒 "ok"）：`m_msgName` 承载期望消息类型名、`m_description` 承载函数说明（未声明时为空）。
- 各内置模块另有模块级自省 API，详见各模块文档：

| 模块 | 自省 API |
|---|---|
| 日志 | `YomkAPI::listLoggers("console")`、`YomkAPI::getLoggerInfo("app", "file")`、`YomkAPI::getAllLoggerInfo()`（见 [YomkLogger.md](YomkLogger.md)） |
| 上下文 | `YomkAPI::listContextKeys()`、`YomkAPI::getContextInfo("config")`、`YomkAPI::listContextGroups()`、`YomkAPI::getAllContextInfo()`（见 [YomkContext.md](YomkContext.md)） |
| 事件循环 | `YomkAPI::listEventLoops()`、`YomkAPI::getEventLoopInfo("TaskLoop")`、`YomkAPI::getAllEventLoopInfo()`（见 [YomkEventLoop.md](YomkEventLoop.md)） |
| 函数池 | `YomkAPI::listFunctionNames()`、`YomkAPI::getFunctionInfo("calcSum")`、`YomkAPI::getAllFunctionInfo()`（见 [YomkFunctionPool.md](YomkFunctionPool.md)） |

## 完整示例

完整可运行演示见 `Examples/ExampleYomkServerInfo.cpp`（6 个机械臂产线场景小示例 showArmXXX 覆盖全部服务自省 API，读运行输出即可理解每个调用）。
