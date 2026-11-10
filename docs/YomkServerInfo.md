# YomkServerInfo（服务自省）

服务自省：运行期查询服务拓扑、功能函数清单与期望消息类型，方便调试与运维。内置服务，随 `server().init()` 自动启动、开箱即用；查询全部经内省域入口 `srv_info()` 的读终结链发出。

## 链式语法速查

| 场景 | 链 | 返回 |
|---|---|---|
| 全部服务名 | `srv_info().services()` | `Response`，`data` 为服务名列表 |
| 指定服务的函数清单 | `srv_info().srv_name("/服务名").functions()` | `Response` 携带 `FuncInfo` 结构化清单（`msg` 恒 `"ok"`；服务不存在时为空清单） |
| 单函数元信息 | `srv_info().fn_url("/服务名/函数名").get_info()` | `Response` 携带结构化 `FuncInfo` 包（`msg` 恒 `"ok"`，数据在 `data`） |
| 全量服务拓扑 | `srv_info().all()` | `Response` 携带 `ServerServiceInfo` 层级列表（`msg` 恒 `"ok"`）：每项 `service_name` 服务名 + `funcs` 函数元信息列表 |

结构化函数元信息 `FuncInfo` 字段：`func_name`（路由名，带 `/` 前缀）、`msg_name`（期望消息类型名，未声明为空）、`description`（函数说明，未声明为空，自由字符串框架透传不解析）。

## 使用示例

```python
from yomk.server import server
from yomk.srv_info import srv_info
from yomk.ctx import ctx
from yomk.event_loop import event_loop
from yomk.fn_pool import fn_pool
from yomk.log import log_mgr

server().init()
server().srv(MyService).create()

srv_info().services()                            # 全部服务名清单
srv_info().srv_name("/MyService").functions()    # 指定服务的函数清单
srv_info().fn_url("/MyService/hello").get_info() # 单个函数元信息（FuncInfo）
srv_info().all()                                 # 全量服务拓扑（服务 → 函数）
```

## 说明

- 内省域 URL 常量与 C++ `YomkServerInfoUrl.h` 一一对应，从 `yomk.url.srv_info` 导入：`SRV_SERVICES`（`/YomkServerInfo/services`）、`SRV_FUNCTIONS`（`/YomkServerInfo/functions`）、`SRV_FUNCTION`（`/YomkServerInfo/function`）、`SRV_ALL`（`/YomkServerInfo/all`）；`builder` 内部即用这些常量组链，用户自定义注册亦可直接复用。
- 诊断契约（经 `yomk._diag` 输出，与 C++ 对齐）：空 `srv_name`、URL 解析失败（无 `/` 前缀、无函数名）、服务未命中走 `WARN`；server 已销毁走 `ERROR`（各 ignored 文案不带句号）；失败路径与载荷类型错配不打诊断。
- 期望消息类型名通过声明处元数据进入内省：`install_func("/x", fn, "MsgName")` 第三参、`fn_pool().msg_name(m)`、`event_loop().msg_name(m)`，仅作内省元数据，不影响调用。
- 函数说明通过 `install_func` 第四参或 `fn_pool().description(d)` 声明；`srv_info().fn_url(...).get_info()` 与函数池单查 `fn_pool().fn_name(name).get_info()` 命中均返回结构化 `FuncInfo` 包；未命中返回 `Failed`。
- 各内置模块另有模块级自省链：

| 模块 | 自省链 |
|---|---|
| 日志 | `log_mgr().console()/file().list()`、`.console(name)/.file(name).get_info()`、`.status()`、`.all()`（见 [YomkLogger.md](YomkLogger.md)） |
| 上下文 | `ctx().keys()`、`.key(k).info()`、`.groups()`、`.all()`（见 [YomkContext.md](YomkContext.md)） |
| 事件循环 | `event_loop().list()`、`.loop(name).get_info()`、`.all()`（见 [YomkEventLoop.md](YomkEventLoop.md)） |
| 函数池 | `fn_pool().list()`、`.fn_name(name).get_info()`、`.all()`（见 [YomkFunctionPool.md](YomkFunctionPool.md)） |

## 完整示例

完整可运行演示见 `examples/ExampleYomkServerInfo.py`（机械臂产线场景覆盖四类查询、注册/删除的实时视图与关闭后返回值契约，读运行输出即可理解每个调用）。
