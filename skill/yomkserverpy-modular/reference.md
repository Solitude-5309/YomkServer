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
    status: ResStatus   # 响应状态
    msg: str            # 响应消息
    data: Any           # 响应数据
    def __init__(self, status=ResStatus.eInvalid, msg="", data=None)
```

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
    def install_func(self, name: str, function, msg_name="") -> None # 注册功能函数（msg_name 为期望消息类型元数据，仅作内省）
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
| `YomkApi.logger_info_loggers()` | 枚举全部日志器名 |
| `YomkApi.logger_info_logger(logger_name)` | 查询单日志器信息 |
| `YomkApi.logger_info_all()` | 查询全部日志器信息 |

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
| `YomkApi.context_info_key(key)` | 查询单 key 元信息行 |
| `YomkApi.context_info_all()` | 查询全部 key 元信息行 |

### EventLoop
| API | 说明 |
|-----|------|
| `YomkApi.event_loop_start(name, handler)` | 启动事件循环 |
| `YomkApi.event_loop_stop(name)` | 停止 |
| `YomkApi.event_loop_post(name, pkg, handler=None, tag="")` | 异步投递（handler 可选，缺省用启动时绑定的处理函数） |
| `YomkApi.event_loop_post_wait(name, pkg, handler=None, tag="")` | 同步投递（handler 可选，缺省用启动时绑定的处理函数） |
| `YomkApi.event_loop_destroy(name)` | 销毁 |
| `YomkApi.event_loop_info_loops()` | 枚举全部事件循环名 |
| `YomkApi.event_loop_info_loop(name, tag_count=None)` | 查询单循环信息（tag_count 限制回显的 tag 条数） |
| `YomkApi.event_loop_info_all()` | 查询全部循环信息 |

### FunctionPool
| API | 说明 |
|-----|------|
| `YomkApi.function_pool_register(name, func, msg_name="")` | 注册函数（msg_name 为函数消息类型元数据，可缺省） |
| `YomkApi.function_pool_unregister(name)` | 注销函数 |
| `YomkApi.function_pool_call(name, pkg)` | 调用函数 |
| `YomkApi.function_pool_info_names()` | 枚举全部函数名 |
| `YomkApi.function_pool_info_name(name)` | 查询单函数信息 |
| `YomkApi.function_pool_info_all()` | 查询全部函数信息 |

### ServerInfo
| API | 说明 |
|-----|------|
| `YomkApi.server_info_services()` | 服务名列表（data 为字符串列表，字典序） |
| `YomkApi.server_info_functions(srv_name)` | 指定服务的功能函数列表（每行 `func [msgName]`，未声明类型无括号后缀；不存在服务返回空表） |
| `YomkApi.server_info_function(url)` | 单函数元信息查询（url 为完整 `/服务名/功能名`，命中返回 msg 为期望消息类型名，未声明为空串；不存在返回 eNo） |
| `YomkApi.server_info_all()` | 全量拓扑：服务名行 + 两空格缩进函数行 |

## URL 格式

所有请求使用统一 URL：`/服务名/功能函数名`

示例：`/ConfigService/load`、`/UserService/get_user`、`/YomkContext/create`
