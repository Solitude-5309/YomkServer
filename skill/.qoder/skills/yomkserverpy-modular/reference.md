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
    def add_service(self, service: YomkService) -> None
    def request(self, url: str, pkg=None) -> YomkResponse
    def async_request(self, url: str, pkg=None, callback=None) -> None

class YomkBoot(ABC):
    def before(self) -> int   # 服务启动前：创建资源
    def start(self) -> int    # 注册并启动服务
    def after(self) -> int    # 服务启动后：初始化调用

class YomkService:
    def __init__(self, server: YomkServer)
    def get_name(self) -> str
    def set_name(self, name: str)
    def init(self) -> None                              # 子类必须实现
    def install_func(self, name: str, function) -> None # 注册功能函数
```

## 数据结构

```python
class Context:
    def __init__(self, key="", value=None)

class ContextChecker:
    def __init__(self, key="", check_func=None)   # check_func: (Context) -> CheckStatus

class ContextMonitor:
    def __init__(self, key="", monitor_func=None) # monitor_func: (Context) -> None

class Function:
    def __init__(self, name="", func=None)        # func: (Any) -> YomkResponse

class CallFunction:
    def __init__(self, name="", pkg=None)

class Event:
    def __init__(self, event_loop_name="", pkg=None, func=None)
    # 属性：m_eventLoopName, m_pkg, m_serviceFunc, m_eventId, m_response

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

### 服务管理
| API | 说明 |
|-----|------|
| `YomkApi.new_service(Class, name)` | 注册服务（传类，自动实例化） |
| `YomkApi.add_service(instance, name)` | 注册服务（传实例） |

### 请求通信
| API | 说明 |
|-----|------|
| `YomkApi.request(url, pkg)` | 同步请求 → YomkResponse |
| `YomkApi.async_request(url, pkg, callback)` | 异步请求 |

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
| `YomkApi.context_set_monitor(key, func)` | 设置监控函数 |

### EventLoop
| API | 说明 |
|-----|------|
| `YomkApi.event_loop_start(name, handler)` | 启动事件循环 |
| `YomkApi.event_loop_stop(name)` | 停止 |
| `YomkApi.event_loop_post(name, pkg, handler)` | 异步投递 |
| `YomkApi.event_loop_post_wait(name, pkg, handler)` | 同步投递 |
| `YomkApi.event_loop_destroy(name)` | 销毁 |

### FunctionPool
| API | 说明 |
|-----|------|
| `YomkApi.function_pool_register(name, func)` | 注册函数 |
| `YomkApi.function_pool_call(name, pkg)` | 调用函数 |

## URL 格式

所有请求使用统一 URL：`/服务名/功能函数名`

示例：`/ConfigService/load`、`/UserService/get_user`、`/YomkContext/create`
