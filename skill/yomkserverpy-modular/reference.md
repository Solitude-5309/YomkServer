# YomkServerPy API 参考

## 核心类型

```python
class ResStatus(Enum):
    eInvalid = -1   # 无效
    eOk = 0         # 成功
    eNo = 1         # 失败

class CheckStatus(Enum):
    eAccept = 0     # 接受
    eReject = 1     # 拒绝

class YomkResponse:
    status: ResStatus   # 本次请求的状态
    msg: str            # 对状态的说明（不携带返回数据）
    data: Any           # 携带的返回数据
    def __init__(self, status=ResStatus.eInvalid, msg="", data=None)
```

字段语义（固化口径）：`status` 表示本次请求的状态；`msg` 仅作对状态的说明（成功 `"ok"`、错误为契约消息），**返回数据一律放 `data`**。

## 核心类

```python
class YomkServer:
    def add_service(self, service: YomkService) -> int   # 0 成功 / -1 失败（内部自动 init，失败回滚）
    def request(self, url: str, pkg) -> YomkResponse
    def async_request(self, url: str, pkg, callback) -> None

class YomkBoot(ABC):
    def before(self) -> int   # 服务启动前：创建资源
    def start(self) -> int    # 注册并启动服务
    def after(self) -> int    # 服务启动后：初始化调用

class YomkService:
    def __init__(self, server: YomkServer)
    def get_name(self) -> str
    def set_name(self, name: str)
    def init(self) -> None                              # 子类必须实现
    def install_func(self, name: str, function, msg_name="", description="") -> None # 注册功能函数（msg_name 为期望消息类型元数据，description 为自定义函数说明，均仅作内省）
```

## 数据结构

```python
class Context:
    def __init__(self, key="", value=None)

class ContextChecker:
    def __init__(self, key="", check_func=None)   # check_func: (Context) -> CheckStatus

class ContextMonitor:
    def __init__(self, key="", monitor_func=None, async_monitor=False) # monitor_func: (Context) -> None；async_monitor=True 时变更通知异步送达

class Function:
    def __init__(self, name="", func=None)        # func: (Any) -> YomkResponse

class CallFunction:
    def __init__(self, name="", pkg=None)

class Event:
    def __init__(self, event_loop_name="", pkg=None, func=None, tag="")
    # 属性：m_eventLoopName, m_pkg, m_serviceFunc, m_eventId, m_response, m_waitCallback, m_tag

class EventLoopPkg:
    def __init__(self, event_loop_name="", default_service_func=None)
```

## YomkApi 便捷 API 速查

### 生命周期
| API | 说明 |
|-----|------|
| `YomkApi.boot(boot_instance)` | Boot 生命周期启动（含 init） |
| `YomkApi.init()` | 初始化（启动内置服务） |
| `YomkApi.server()` | 获取 Server 实例 |
| `YomkApi.server_instance()` | 获取 Server 实例（server 的别名） |
| `YomkApi.shutdown()` | 销毁 Server（幂等；进程退出时由 atexit 兜底调用） |
| `YomkApi.version()` | 框架版本号（字符串） |
| `YomkApi.__version__` | 框架版本号（源自 YomkServerPy 的 project(VERSION)，随库生成的版本模块提供） |

### 服务管理
| API | 说明 |
|-----|------|
| `YomkApi.new_service(Class, name)` | 注册服务（传类，自动实例化） |
| `YomkApi.add_service(instance, name)` | 注册服务（传实例） |
| `YomkApi.del_service(name)` | 删除服务（触发 deinit） |

### 请求通信
| API | 说明 |
|-----|------|
| `YomkApi.request(url, pkg)` | 同步请求 → YomkResponse |
| `YomkApi.async_request(url, pkg, callback)` | 异步请求 |

### 日志
| API | 说明 |
|-----|------|
| `YomkApi.console_log_info_tag(tag, *args)` | 控制台 Info 日志（同族 warn/error/debug；内容头自动注入 `[行号]`） |
| `YomkApi.console_log_info(*args)` | 控制台 Info 日志便捷版（缺省 tag "MainLogger"；同族 warn/error/debug） |
| `YomkApi.file_log_create(log_dir, log_file)` | 创建文件日志器 |
| `YomkApi.file_log_info_tag(log_file, tag, *args)` | 文件 Info 日志（同族 warn/error/debug） |
| `YomkApi.file_log_info(log_file, *args)` | 文件 Info 日志便捷版（缺省 tag "MainLogger"；同族 warn/error/debug） |
| `YomkApi.file_log_write(log_file)` | 手动落盘 |
| `YomkApi.file_log_delete(logger_name)` | 删除日志器（最后引用释放时触发析构落盘） |
| `YomkApi.on_console_log_info()` / `YomkApi.off_console_log_info()` | 开/关控制台 Info 输出（同族 warn/error/debug） |
| `YomkApi.set_console_log_proxy(func)` | 设置日志代理（func 返回 False 消费该条日志） |
| `YomkApi.logger_info_loggers(logger_type)` | 按类型获取日志器纯名列表（type 为 `"console"`/`"file"`，仅返回该类型的日志器名字典序清单；其他值返回 eNo `"unknown logger type."`） |
| `YomkApi.logger_info_logger(logger_name, logger_type)` | 单日志器元信息结构化查询（type: "console"/"file"；两表允许同名，按类型单表精确查找不 fallback；命中 msg 恒 "ok"，data 为 LoggerInfo 包：logger_name/type/dir，file 时 dir 为日志目录；未注册或非法 type 返回 eNo） |
| `YomkApi.logger_info_all()` | 全量日志器及控制台状态（命中 msg 恒 `"ok"`，data 为 `list[LoggerInfo]` 清单，每条内嵌 `logger_state` 状态段（`LoggerState` 五开关 debug_on/info_on/warn_on/error_on/proxy_on）；console 条目为真实全局状态、file 条目级别恒 true/proxy_on 恒 false） |

### Context
| API | 说明 |
|-----|------|
| `YomkApi.context_create(key, value)` | 创建 K-V |
| `YomkApi.context_get(key, default)` | 获取值 |
| `YomkApi.context_set(key, value)` | 设置值 |
| `YomkApi.context_destroy(key)` | 销毁 |
| `YomkApi.context_turn_on_checker()` | 开启检查器 |
| `YomkApi.context_turn_off_checker()` | 关闭检查器 |
| `YomkApi.context_set_checker(key, func)` | 设置检查函数 |
| `YomkApi.context_turn_on_monitor()` | 开启监控器 |
| `YomkApi.context_turn_off_monitor()` | 关闭监控器 |
| `YomkApi.context_set_monitor(key, func, async_monitor=False)` | 设置监控函数（async_monitor=True 时异步回调监控函数） |
| `YomkApi.context_info_keys()` | 枚举全部 key |
| `YomkApi.context_info_key(key)` | 单键元信息结构化查询（命中 msg 恒 `"ok"`，data 为 `ContextKeyInfo` 包：key/type_name/checker_on/monitor_count/async_monitor_count；空 key 与不存在 key 返回 eNo） |
| `YomkApi.context_info_all()` | 全量键元信息结构化列表（命中 msg 恒 `"ok"`，data 为 `ContextKeyInfo` 列表，元素字段与单查一致，键序为字典序） |

### EventLoop
| API | 说明 |
|-----|------|
| `YomkApi.event_loop_start(name, handler)` | 启动事件循环 |
| `YomkApi.event_loop_stop(name)` | 停止 |
| `YomkApi.event_loop_post(name, pkg, handler=None, tag="")` | 异步投递（handler 可选，缺省用启动时绑定的处理函数） |
| `YomkApi.event_loop_post_wait(name, pkg, handler=None, tag="")` | 同步投递（handler 可选，缺省用启动时绑定的处理函数） |
| `YomkApi.event_loop_destroy(name)` | 销毁 |
| `YomkApi.event_loop_info_loops()` | 枚举全部事件循环名 |
| `YomkApi.event_loop_info_loop(name, tag_count=None)` | 单循环元信息结构化查询（命中 msg 恒 `"ok"`，data 为 `EventLoopInfo` 包：loop_name/running/pending/has_default_func/msg_name/next_tags；tag_count 限制 next_tags 条数；不存在循环返回 eNo） |
| `YomkApi.event_loop_info_all()` | 全量循环元信息结构化列表（命中 msg 恒 `"ok"`，data 为 `EventLoopInfo` 列表，元素字段与单查一致，next_tags 取 3 条） |

### FunctionPool
| API | 说明 |
|-----|------|
| `YomkApi.function_pool_register(name, func, msg_name="", description="")` | 注册函数（三参声明期望消息类型、四参附函数说明，均仅内省元数据；说明经 info_name 结构化包透出；同名重复注册即运行时热替换） |
| `YomkApi.function_pool_unregister(name)` | 注销函数 |
| `YomkApi.function_pool_call(name, pkg)` | 调用函数 |
| `YomkApi.function_pool_info_names()` | 枚举全部函数名 |
| `YomkApi.function_pool_info_name(name)` | 单函数元信息结构化查询（命中 msg 恒 "ok"，data 为 FuncInfo 包：func_name/msg_name/description；未注册返回 eNo） |
| `YomkApi.function_pool_info_all()` | 全量函数元信息结构化列表（命中 msg 恒 `"ok"`，data 为 `FuncInfo` 列表，元素字段与单查一致，总数由列表长度承载） |

### ServerInfo
| API | 说明 |
|-----|------|
| `YomkApi.server_info_services()` | 服务名列表（data 为字符串列表，字典序） |
| `YomkApi.server_info_functions(srv_name)` | 指定服务的功能函数结构化清单（data 为 `FuncInfo` 列表：func_name/msg_name/description 三字段，与 /function 单查、/all 聚合同族；不存在服务返回空表） |
| `YomkApi.server_info_function(url)` | 单函数元信息结构化查询（url 为完整 `/服务名/功能名`，命中 msg 恒 "ok"，data 为 FuncInfo 包：func_name/msg_name/description；不存在返回 eNo） |
| `YomkApi.server_info_all()` | 全量服务拓扑（结构化层级列表：每服务一个条目 service_name + funcs（FuncInfo 列表，与 /function 单查同族），msg 恒 "ok"） |

## URL 格式

所有请求使用统一 URL：`/服务名/功能函数名`

示例：`/ConfigService/load`、`/UserService/get_user`、`/YomkContext/create`
