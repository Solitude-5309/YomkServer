# YomkServerPy API 详细参考

## YomkApi 便捷 API（推荐）

YomkApi 是 YomkServerPy 的高层封装 API，提供更简洁的调用方式。

### 初始化与服务管理
```python
import YomkApi

# 初始化服务器
YomkApi.init(YomkApi.YomkServer(), ["YomkContext", "YomkFunctionPool", "YomkEventLoop"])

# 注册服务
YomkApi.new_service(MyService, "/MyService")

# 同步请求
resp = YomkApi.request("/MyService/func", pkg)

# 异步请求
YomkApi.async_request("/MyService/func", pkg, callback)
```

### Context API
```python
# 创建
YomkApi.context_create("key", value)

# 获取
value = YomkApi.context_get("key", default_value)

# 设置
YomkApi.context_set("key", value)

# 销毁
YomkApi.context_destroy("key")

# 检查器
YomkApi.context_turn_on_checker()
YomkApi.context_turn_off_checker()
YomkApi.context_set_checker("key", check_func)

# 监控器
YomkApi.context_turn_on_monitor()
YomkApi.context_turn_off_monitor()
YomkApi.context_set_monitor("key", monitor_func)
```

### FunctionPool API
```python
# 注册
YomkApi.function_pool_register("func_name", func)

# 调用
resp = YomkApi.function_pool_call("func_name", pkg)
```

### EventLoop API
```python
# 启动
YomkApi.event_loop_start("loop_name", handler_func)

# 投递事件（异步）
YomkApi.event_loop_post("loop_name", pkg, handler_func)

# 投递事件（同步等待）
resp = YomkApi.event_loop_post_wait("loop_name", pkg, handler_func)
event = resp.data  # 返回包含结果的Event对象

# 停止
YomkApi.event_loop_stop("loop_name")

# 销毁
YomkApi.event_loop_destroy("loop_name")
```

## 核心类型

### YomkResponse — 响应对象
```python
class YomkResponse:
    def __init__(self, status: ResStatus, msg: str = "", data: Any = None):
        self.status = status            # 响应状态枚举
        self.msg = msg                  # 响应消息
        self.data = data                # 响应数据
```

### ResStatus — 响应状态枚举
```python
class ResStatus(Enum):
    eInvalid = -1   # 无效/错误
    eOk = 0         # 成功
    eNo = 1        # 错误
```

### YomkServiceFunc — 功能函数签名
```python
# 功能函数接收pkg参数，返回YomkResponse
def service_func(pkg) -> YomkResponse:
    pass

# 回调函数接收YomkResponse
def callback_func(response: YomkResponse):
    pass
```

## 数据结构

### Context — 上下文键值对
```python
class Context:
    def __init__(self, key="", value=None):
        self.key = key      # 键名
        self.value = value  # 值
```

### ContextChecker — 上下文检查器
```python
class ContextChecker:
    def __init__(self, key="", check_func=None):
        self.key = key              # 要检查的键
        self.check_func = check_func  # 检查函数

# CheckStatus 枚举
class CheckStatus(Enum):
    eAccept = 0   # 接受
    eReject = 1   # 拒绝
```

**检查函数签名：**
```python
def checker(ctx: Context) -> CheckStatus:
    # ctx.key = 检查的键
    # ctx.value = 新值
    # 返回 CheckStatus.eAccept 允许设置，CheckStatus.eReject 拒绝设置
    return CheckStatus.eAccept
```

### ContextMonitor — 上下文监控器
```python
class ContextMonitor:
    def __init__(self, key="", monitor_func=None):
        self.key = key                # 要监控的键
        self.monitor_func = monitor_func  # 监控函数
```

**监控函数签名：**
```python
def monitor(ctx: Context):
    # ctx.key = 变化的键
    # ctx.value = 新值
    print(f"key={ctx.key} new_value={ctx.value}")
```

### Function — 函数注册
```python
class Function:
    def __init__(self, name, func):
        self.name = name  # 函数名
        self.func = func  # 函数对象
```

**YomkApi 便捷方式：**
```python
YomkApi.function_pool_register("name", func)  # 自动创建Function对象
```

### CallFunction — 函数调用
```python
class CallFunction:
    def __init__(self, name, pkg):
        self.name = name  # 函数名
        self.pkg = pkg    # 参数包
```

**YomkApi 便捷方式：**
```python
YomkApi.function_pool_call("name", pkg)  # 自动创建CallFunction对象
```

### Event — 事件
```python
class Event:
    def __init__(self, event_loop_name="", pkg=None, func=None):
        self.m_eventLoopName = event_loop_name  # 事件循环名称
        self.m_pkg = pkg                        # 事件参数包
        self.m_serviceFunc = func               # 事件处理函数（可选）
        self.m_eventId = 0                      # 事件ID（自动生成）
        self.m_response = None                  # 事件响应结果
        self.m_waitCallback = None              # 等待回调（内部使用）
    
    def handle(self):
        """执行事件处理函数"""
```

**YomkApi 便捷方式：**
```python
YomkApi.event_loop_post("loop_name", pkg, handler)  # 自动创建Event对象
YomkApi.event_loop_post_wait("loop_name", pkg, handler)  # 自动创建Event对象
```

### EventLoopPkg — 事件循环配置包
```python
class EventLoopPkg:
    def __init__(self, event_loop_name="", default_service_func=None):
        self.m_eventloopName = event_loop_name          # 事件循环名称
        self.m_defaultServiceFunc = default_service_func  # 默认事件处理函数
```

**YomkApi 便捷方式：**
```python
YomkApi.event_loop_start("loop_name", handler)  # 自动创建EventLoopPkg对象
```

## YomkServer 类

```python
class YomkServer:
    def __init__(self, max_thread=8):
        """初始化服务器，max_thread为线程池大小"""
    
    def start_service(self, srv_names: list[str]):
        """启动内置服务，srv_names可选：["YomkFunctionPool", "YomkContext"]"""
    
    def add_service(self, service: YomkService):
        """注册自定义服务"""
    
    def request(self, url: str, pkg=None) -> YomkResponse:
        """同步请求，URL格式：/服务名/功能函数名"""
    
    def async_request(self, url: str, pkg=None, callback=None):
        """异步请求，callback为回调函数"""
```

## YomkService 类

```python
class YomkService:
    def __init__(self, server: YomkServer):
        """初始化服务，传入服务器实例"""
    
    def get_name(self) -> str:
        """获取服务名"""
    
    def set_name(self, name: str):
        """设置服务名，必须以/开头"""
    
    def init(self):
        """服务初始化，必须实现此方法"""
        pass
    
    def install_func(self, name: str, function):
        """注册功能函数"""
    
    def invoke(self, name: str, pkg=None) -> YomkResponse:
        """调用服务内部函数"""
```

## EventLoop 类

```python
class EventLoop:
    def __init__(self):
        """创建事件循环实例"""
    
    def set_default_service_func(self, func):
        """设置默认事件处理函数"""
    
    def start(self):
        """启动事件循环"""
    
    def stop(self) -> int:
        """停止事件循环，返回0表示成功"""
    
    def post(self, event: Event) -> int:
        """投递事件（异步），返回0表示成功，1表示失败"""
    
    def post_wait(self, event: Event) -> int:
        """投递事件并同步等待完成，返回0表示成功，1表示失败"""
    
    def run(self):
        """事件循环主线程（内部方法）"""
```

**EventLoop 特性：**
- 独立线程运行，与主线程隔离
- 同循环内事件顺序执行
- 支持非阻塞投递（`post`）和阻塞投递（`post_wait`）
- 自动分配事件ID（递增）
- 支持死锁检测（在工作线程中调用`post_wait`会直接执行）

## YomkEventLoop 内置服务

YomkEventLoop 是管理多个 EventLoop 实例的服务。

### 启动事件循环
```python
server.start_service(["YomkEventLoop"])  # 先启动服务

# 启动事件循环（自动创建如果不存在）
server.request("/YomkEventLoop/start", 
    EventLoopPkg("worker_loop", default_handler))
```

### 投递事件（异步）
```python
server.request("/YomkEventLoop/post", 
    Event("worker_loop", pkg_data, handler_func))
```

### 投递事件（同步等待）
```python
resp = server.request("/YomkEventLoop/post_wait", 
    Event("worker_loop", pkg_data, handler_func))
if resp.status == ResStatus.eOk:
    event = resp.data  # 返回包含结果的Event对象
    result = event.m_response
```

### 停止事件循环
```python
server.request("/YomkEventLoop/stop", "worker_loop")
```

### 销毁事件循环
```python
server.request("/YomkEventLoop/destroy", "worker_loop")
```

## YomkContext 内置服务

### 创建Context键值对
```python
server.request("/YomkContext/create", Context("key", value))
```

### 获取值
```python
resp = server.request("/YomkContext/get", Context("key"))
value = resp.data
```

### 设置值
```python
server.request("/YomkContext/set", Context("key", new_value))
```

### 销毁键
```python
server.request("/YomkContext/destroy", "key")
```

### 检查器机制
```python
# 全局开启/关闭检查器
server.request("/YomkContext/turn_on_checker", None)
server.request("/YomkContext/turn_off_checker", None)

# 设置特定键的检查函数
server.request("/YomkContext/set_checker", ContextChecker("key", check_func))
```

### 监控器机制
```python
# 全局开启/关闭监控器
server.request("/YomkContext/turn_on_monitor", None)
server.request("/YomkContext/turn_off_monitor", None)

# 设置特定键的监控函数
server.request("/YomkContext/set_monitor", ContextMonitor("key", monitor_func))
```

## YomkFunctionPool 内置服务

### 注册函数
```python
server.request("/YomkFunctionPool/register", Function("func_name", func))
```

### 调用函数
```python
resp = server.request("/YomkFunctionPool/call", CallFunction("func_name", pkg))
```

## URL格式规范

所有请求使用统一的URL格式：
- **格式**：`/服务名/功能函数名`
- **示例**：
  - `/UserService/create_user`
  - `/YomkContext/create`
  - `/YomkFunctionPool/register`

## 最佳实践

### 1. Context 键命名规范
```python
# ✅ 使用命名空间隔离
"moduleA:data"
"moduleB:data"

# ✅ 使用层级结构
"task:current_id"
"task:counter"
"task:status"
```

### 2. 检查器函数模板
```python
def my_checker(ctx) -> YomkApi.CheckStatus:
    # ctx.key - 被检查的键
    # ctx.value - 尝试设置的新值
    if not ctx.value or str(ctx.value).strip() == "":
        return YomkApi.CheckStatus.eReject
    return YomkApi.CheckStatus.eAccept

# 注册
YomkApi.context_set_checker("key", my_checker)
```

### 3. 监控器函数模板
```python
def my_monitor(ctx):
    # ctx.key - 变化的键
    # ctx.value - 新值
    print(f"Context changed: {ctx.key} = {ctx.value}")

# 注册
YomkApi.context_set_monitor("key", my_monitor)
```

### 4. EventLoop 使用模板

**方式1：通过 YomkEventLoop 服务管理（推荐）**
```python
# 启动 YomkEventLoop 服务
server.start_service(["YomkEventLoop"])

# 启动事件循环
server.request("/YomkEventLoop/start", 
    EventLoopPkg("worker_loop", default_handler))

# 投递事件（异步）
server.request("/YomkEventLoop/post", Event("worker_loop", pkg_data, None))

# 投递事件（同步等待）
resp = server.request("/YomkEventLoop/post_wait", 
    Event("worker_loop", pkg_data, None))
if resp.status == ResStatus.eOk:
    event = resp.data
    result = event.m_response

# 停止和销毁
server.request("/YomkEventLoop/stop", "worker_loop")
server.request("/YomkEventLoop/destroy", "worker_loop")
```

**方式2：直接使用 EventLoop 类**
```python
# 创建并启动
event_loop = EventLoop()
event_loop.set_default_service_func(default_handler)
event_loop.start()

# 投递事件
event_loop.post(Event("loop_name", pkg_data, handler_func))

# 同步等待
event = Event("loop_name", pkg_data, handler_func)
event_loop.post_wait(event)
result = event.m_response

# 停止
event_loop.stop()
```

### 5. 异步请求模板
```python
def callback(response: YomkApi.YomkResponse) -> None:
    if response.status == YomkApi.ResStatus.eOk:
        print(f"Success: {response.msg}")
    else:
        print(f"Error: {response.msg}")

YomkApi.async_request("/ServiceName/func_name", pkg_data, callback)
```
