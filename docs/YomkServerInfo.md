# YomkServerInfo(服务自省)

服务自省：运行期查询服务拓扑、功能函数清单与期望消息类型，方便调试与运维。内置服务，随 `yomk::server().init()` 自动启动、开箱即用；查询全部经服务器域入口 `yomk::server()` 的内省读终结链发出。

## 链式语法速查

| 场景 | 链 | 返回 |
|---|---|---|
| 全部服务名 | `yomk::server().services()` | `Response`，`m_data` 为 StringArray 名清单 |
| 指定服务的函数清单 | `yomk::server().srv_name("/服务名").functions()` | `Response` 携带 FuncInfoList 结构化清单（`m_msg` 恒 "ok"；服务不存在时为空清单） |
| 单函数元信息 | `yomk::server().fn_url("/服务名/函数名").get_info()` | `Response` 携带结构化 FuncInfo 包（`m_msg` 恒 "ok"，数据在 `m_data`） |
| 全量服务拓扑 | `yomk::server().all()` | `Response` 携带 ServerServiceInfoList 层级包（`m_msg` 恒 "ok"）：每项 `m_serviceName` 服务名 + `m_funcs` 函数元信息列表 |

结构化函数元信息 `FuncInfo` 字段：`m_funcName`（路由名，带 `/` 前缀）、`m_msgName`（期望消息类型名，未声明为空）、`m_description`（函数说明，未声明为空）。

## 使用示例

```cpp
yomk::server().init();
yomk::server().srv<MyService>().create();

yomk::server().services();                                   // 全部服务名清单
yomk::server().srv_name("/MyService").functions();           // 指定服务的函数清单（FuncInfoList）
yomk::server().fn_url("/MyService/hello").get_info();        // 单个函数元信息（FuncInfo）
yomk::server().all();                                        // 全量服务拓扑（服务 → 函数）
```

## 说明

- 期望消息类型名通过声明处元数据进入内省：`installFunc("/x", &C::m, "MsgName")` 第三参、`yomk::fn_pool().msg_name(m)`、`yomk::event_loop().msg_name(m)`，仅作内省元数据，不影响调用。
- 函数说明通过 `installFunc` 第四参或 `yomk::fn_pool().description(d)` 声明（自由字符串，内容使用者自定义，框架透传不解析）；`fn_url(...).get_info()` 与函数池单查 `fn_pool().fn_name(name).get_info()` 命中均返回结构化 FuncInfo 包。
- 各内置模块另有模块级自省链：

| 模块 | 自省链 |
|---|---|
| 日志 | `yomk::log_mgr().console()/file().list()`、`.console(name)/.file(name).get_info()`、`.status()`、`yomk::log_mgr().all()`（见 [YomkLogger.md](YomkLogger.md)） |
| 上下文 | `yomk::ctx().keys()`、`.key(k).info()`、`.groups()`、`.all()`（见 [YomkContext.md](YomkContext.md)） |
| 事件循环 | `yomk::event_loop().list()`、`.loop(name).get_info()`、`.all()`（见 [YomkEventLoop.md](YomkEventLoop.md)） |
| 函数池 | `yomk::fn_pool().list()`、`.fn_name(name).get_info()`、`.all()`（见 [YomkFunctionPool.md](YomkFunctionPool.md)） |

## 完整示例

完整可运行演示见 `examples/ExampleYomkServerInfo.cpp`（6 个机械臂产线场景小示例 showArmXXX 覆盖全部服务自省 API，读运行输出即可理解每个调用）。
