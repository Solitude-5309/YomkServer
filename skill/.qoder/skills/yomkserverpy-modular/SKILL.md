---
name: yomkserverpy-modular
description: 基于YomkServerPy框架的模块化Python工程编程。核心理念"一切皆服务，一切皆请求"。用于创建YomkService功能模块、使用YomkContext全局状态管理、YomkEventLoop事件循环、YomkFunctionPool公共函数池。当用户需要编写YomkServerPy模块化代码、创建服务、管理全局状态、处理事件队列、注册公共函数或搭建工程结构时使用。包含进阶实践：控制流与数据流分离、配置驱动、服务职责分离、Context键生命周期管理、FunctionPool回调函数无状态化等。
---

# YomkServerPy 模块化编程框架

## 框架概述

YomkServerPy 是基于 Python 的模块化高性能服务开发框架，核心设计理念：**「一切皆服务，一切皆请求」**。
通过标准化的 Request/Response 通信接口和灵活的模块机制，实现系统组件的高度解耦和动态组合。

### 设计哲学

1. **关注点分离**：每个服务专注于单一职责
2. **约定优于配置**：合理的默认值减少配置复杂度
3. **渐进式复杂度**：从简单单体到复杂系统的平滑演进
4. **开发者友好**：直观的API设计，开箱即用的基础组件

### 两级模块化模型

| 层级 | 概念 | 说明 |
|------|------|------|
| **Service层** | YomkService | 基础服务模块，封装独立业务域或技术组件 |
| **Function层** | Function | 服务内具体功能单元，通过唯一URL路径标识和访问 |

### 基础服务组件

| 组件 | 职责 | 核心特性 |
|------|------|----------|
| **YomkServer** | 服务容器，管理所有服务的生命周期 | 程序入口初始化 |
| **YomkService** | 功能模块单元，注册功能函数 | 高内聚、松耦合、支持独立扩展 |
| **YomkContext** | 全局K-V状态机 | 状态安全检查(防非法迁移)、变更监控、全生命周期管理 |
| **YomkEventLoop** | 线程隔离的事件循环 | 独立线程运行、同循环内顺序执行、不同循环间并行、支持非阻塞/阻塞投递 |
| **YomkFunctionPool** | 动态函数池 | 统一注册调度、支持运行时注册/更新/热替换、面向过程开发范式 |

所有组件通过统一的 **Request/Response 模型** 通信，URL格式：`/服务名/功能函数名`

## 工程安装与导入

### 安装方式
```bash
# 通过CMake构建安装
mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=~/YomkServerPy/install
cmake --build . --target install

# 设置环境变量
export PYTHONPATH=~/YomkServerPy/install/lib/python3.10/site-packages:$PYTHONPATH
```

### 模块导入

**方式1：使用 YomkApi 便捷 API（推荐）**
```python
import YomkApi
from typing import Any

# 初始化
YomkApi.init(YomkApi.YomkServer(), ["YomkContext", "YomkFunctionPool", "YomkEventLoop"])

# 创建服务
YomkApi.new_service(MyService, "/MyService")

# 请求
resp = YomkApi.request("/MyService/func", pkg_data)
```

**方式2：直接使用底层 API**
```python
from YomkServerPy import (
    YomkServer, YomkService, YomkResponse, ResStatus,
    Context, ContextChecker, ContextMonitor, CheckStatus,
    Function, CallFunction, 
    Event, EventLoop, EventLoopPkg
)

# 初始化
server = YomkServer()
server.start_service(["YomkContext", "YomkFunctionPool", "YomkEventLoop"])

# 创建服务
my_service = MyService(server)
my_service.init()
server.add_service(my_service)

# 请求
resp = server.request("/MyService/func", pkg_data)
```

## 编程规范

### 1. YomkService 编写模板

```python
class MyService(YomkService):
    def __init__(self, server):
        super().__init__(server)
        self.set_name("/MyService")  # 服务名必须唯一，以/开头
    
    def init(self):
        # 安装功能函数
        self.install_func("/my_func", self.my_func)
        print("install func [ /my_func ] to", self.get_name())
    
    def my_func(self, pkg):
        # 1. pkg直接使用
        if not pkg:
            return YomkResponse(ResStatus.eInvalid, "pkg is null")
        
        # 2. 业务逻辑
        # ...
        
        # 3. 返回结果
        return YomkResponse(ResStatus.eOk, "success")
```

### 2. 程序初始化模板

```python
import YomkApi

def main():
    # 初始化服务器，启动内置服务
    YomkApi.init(YomkApi.YomkServer(), ["YomkFunctionPool", "YomkContext", "YomkEventLoop"])
    
    # 注册自定义服务
    YomkApi.new_service(MyService, "/MyService")
    
    # 启动事件循环
    YomkApi.event_loop_start("worker_loop", default_handler)
    
    # 同步请求
    resp = YomkApi.request("/MyService/my_func", pkg_data)
    
    # 异步请求
    YomkApi.async_request("/MyService/my_func", pkg_data, lambda resp: print(resp.msg))
    
    input("Press Enter to exit...")

if __name__ == "__main__":
    main()
```

## API速查

### 请求通信
```python
server.start_service(srv_names)           # 初始化服务器，启动内置服务
server.add_service(service)               # 注册服务
server.request(url, pkg)                  # 同步请求 → YomkResponse
server.async_request(url, pkg, callback)  # 异步请求
```

### Context 全局状态

**方式1：使用 YomkApi 便捷 API（推荐）**
```python
# 创建
YomkApi.context_create("key", value)

# 获取
value = YomkApi.context_get("key", default_value)

# 设置
YomkApi.context_set("key", new_value)

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

**方式2：直接使用底层 API**
```python
server.request("/YomkContext/create", Context("key", value))
resp = server.request("/YomkContext/get", Context("key"))
server.request("/YomkContext/set", Context("key", new_value))
server.request("/YomkContext/destroy", "key")
server.request("/YomkContext/set_checker", ContextChecker("key", check_func))
server.request("/YomkContext/set_monitor", ContextMonitor("key", monitor_func))
```

### EventLoop 事件循环

**方式1：使用 YomkApi 便捷 API（推荐）**
```python
# 启动
YomkApi.event_loop_start("worker_loop", default_handler)

# 投递事件（异步）
YomkApi.event_loop_post("worker_loop", pkg_data, handler)

# 投递事件（同步等待）
resp = YomkApi.event_loop_post_wait("worker_loop", pkg_data, handler)
event = resp.data

# 停止和销毁
YomkApi.event_loop_stop("worker_loop")
YomkApi.event_loop_destroy("worker_loop")
```

**方式2：直接使用底层 API**
```python
server.request("/YomkEventLoop/start", EventLoopPkg("worker_loop", default_handler))
server.request("/YomkEventLoop/post", Event("worker_loop", pkg, handler))
server.request("/YomkEventLoop/post_wait", Event("worker_loop", pkg, handler))
server.request("/YomkEventLoop/stop", "worker_loop")
server.request("/YomkEventLoop/destroy", "worker_loop")
```

### FunctionPool 函数池

**方式1：使用 YomkApi 便捷 API（推荐）**
```python
# 注册函数
YomkApi.function_pool_register("func_name", func)

# 调用函数
resp = YomkApi.function_pool_call("func_name", pkg)
```

**方式2：直接使用底层 API**
```python
server.request("/YomkFunctionPool/register", Function("func_name", func))
resp = server.request("/YomkFunctionPool/call", CallFunction("func_name", pkg))
```

## 进阶实践：YomkServerPy 设计理念深度理解

### 1. API 风格 vs 状态风格：数据传递的层次选择

YomkServerPy 支持多种数据传递方式，应根据场景选择最合适的层次：

| 方式 | 适用场景 | 示例 |
|------|----------|------|
| **pkg 参数** | API 风格调用，一次性传递控制参数 | `server.request("/process", "task_id")` |
| **Context** | 共享状态、跨请求的数据流 | `server.request("/YomkContext/set", Context("status", val))` |
| **FunctionPool** | 无状态工具函数，热插拔 | `server.request("/YomkFunctionPool/call", CallFunction("validate", pkg))` |

**设计原则：pkg 用于控制流（告诉系统"做什么"），Context 用于数据流（传递"用什么数据做"）。**

```python
# ✅ API 风格：通过 pkg 传控制参数
server.request("/TaskService/process", "task_001")

# ✅ 状态风格：数据通过 Context 传递
resp = server.request("/YomkContext/get", Context("task_status"))
status = resp.data

# ❌ 反模式：把控制参数塞进 Context
server.request("/YomkContext/set", Context("current_task_id", "task_001"))  # 应该用 pkg
```

### 2. 服务职责分离：init() 只注册接口，业务逻辑延迟到请求时

`init()` 是服务初始化阶段，应仅用于：
- 注册功能函数（`install_func`）
- 创建服务内部必需的 Context 键（如状态标识、计数器）
- 启动依赖的事件循环

**不应在 init() 中：**
- 加载业务配置（应由独立的 `/create` 或 `/load` 接口处理）
- 执行耗时操作（应通过 EventLoop 异步处理）
- 注册业务数据键（应由配置驱动，按需创建）

```python
class TaskService(YomkService):
    def __init__(self, server):
        super().__init__(server)
        self.set_name("/TaskService")
    
    def init(self):
        # ✅ 只注册接口
        self.install_func("/load", self.load)
        self.install_func("/process", self.process)
        
        # ✅ 只创建服务内部必需的键
        self.server.request("/YomkContext/create", Context("task:current_id", ""))
        self.server.request("/YomkContext/create", Context("task:counter", "0"))
```

### 3. 配置驱动：用户修改配置，引擎自动响应

当业务逻辑依赖可变配置时，应将创建逻辑移入服务，而非硬编码在 main.py：

```python
# ❌ 硬编码：每次改配置都要改代码
server.request("/YomkContext/create", Context("greeting", "Hello"))
server.request("/YomkContext/create", Context("task_name", "demo"))

# ✅ 配置驱动：配置在 YAML/JSON 中，服务自动创建
# main.py 中只需：
server.request("/TaskService/load", "config/tasks.yaml")
```

**服务加载配置的标准流程：**
1. 解析配置文件（YAML/JSON）
2. 根据配置定义创建 Context 键
3. 构建服务内部数据结构（dict、list 等）
4. 注册到服务内部状态

### 4. Context 键生命周期管理：去重与命名空间隔离

Context 是全局共享的，多模块加载时需防止重复创建和键冲突：

```python
# ✅ 使用集合去重
if key not in self.created_keys:
    self.server.request("/YomkContext/create", Context(key, default_value))
    self.created_keys.add(key)

# ✅ 命名空间隔离：不同模块使用不同的键前缀
self.server.request("/YomkContext/create", Context("moduleA:data", val))
self.server.request("/YomkContext/create", Context("moduleB:data", val))
```

### 5. 接口设计的兼容性：新旧共存

提供新接口时保留旧接口，通过内部统一实现降低维护成本：

```python
# 新接口：/load_batch 批量加载
def load_batch(self, pkg):
    with open(pkg, 'r') as f:
        config = yaml.safe_load(f)
    for item in config['items']:
        self._load_single(item, "")

# 旧接口：/load_single 单个加载（向后兼容）
def load_single(self, pkg):
    config_path, override_name = pkg
    self._load_single(config_path, override_name)

# 统一实现
def _load_single(self, path, name):
    # 实际加载逻辑
    pass
```

### 6. FunctionPool 回调函数的数据契约

通过 FunctionPool 注册的回调函数应该是**无状态的**，所有数据通过 pkg 参数或 Context 传递：

```python
# ✅ 回调函数从 pkg 或 Context 获取数据
def process_data(pkg):
    # 从 Context 获取输入
    resp = server.request("/YomkContext/get", Context("in:data"))
    input_data = resp.data
    
    # 纯业务逻辑
    result = transform(input_data)
    
    # 写入输出
    server.request("/YomkContext/set", Context("out:result", result))
    return YomkResponse(ResStatus.eOk, "done")

# ❌ 反模式：直接读写业务键（耦合配置）
resp = server.request("/YomkContext/get", Context("my_specific_data"))
```

**调用方负责数据中转：**
- 将业务键的值复制到 `in:` 前缀键
- 回调函数读写 `in:`/`out:` 键
- 将 `out:` 键的值写回业务键

### 7. 渐进式复杂度：从简单到复杂的平滑演进

YomkServerPy 支持从单体到多服务的平滑演进：

```python
# 阶段1：单体，直接调用
def main():
    process_data()  # 直接函数调用

# 阶段2：FunctionPool，支持热替换
server.request("/YomkFunctionPool/register", Function("process", process_data))
server.request("/YomkFunctionPool/call", CallFunction("process", pkg))

# 阶段3：Service，独立模块
class MyService(YomkService):
    pass
my_service = MyService(server)
my_service.init()
server.add_service(my_service)
server.request("/MyService/process", pkg)

# 阶段4：多服务 + EventLoop，异步处理
server.start_service(["YomkEventLoop"])
server.request("/YomkEventLoop/start", EventLoopPkg("worker_loop", handler))
server.async_request("/MyService/process", pkg, callback)
```

## 模块化工程设计原则

1. **一切皆服务，一切皆请求**：所有功能模块统一为Service，所有交互统一为Request/Response
2. **两级模块化**：Service封装业务域，Function封装具体功能，URL路径唯一标识
3. **关注点分离**：每个Service只负责一个领域的功能，服务间依赖最小化
4. **服务间通过请求通信**：使用 `server.request("/ServiceName/func", pkg)` 跨服务调用，支持同步与异步
5. **共享状态用 Context**：避免全局变量，使用 YomkContext 管理共享状态，配合Checker防非法迁移、Monitor监听变更
6. **耗时操作用 EventLoop**：IO密集型和高并发任务投递到独立事件循环，线程隔离保证顺序执行
7. **公共函数用 FunctionPool**：无状态工具函数注册到函数池，支持运行时热替换
8. **每个服务独立文件**：`XxxService.py`，支持并行开发
9. **渐进式演进**：从单体应用平滑演进到复杂多服务系统，无需重构架构
10. **控制流用 pkg，数据流用 Context**：API 参数通过 pkg 传递，共享状态通过 Context 传递
11. **配置驱动而非硬编码**：业务配置通过配置文件定义，服务自动解析创建，main.py 保持极简
12. **init() 只注册，不执行业务**：服务初始化仅注册接口和创建内部必需键，业务逻辑延迟到请求时
13. **FunctionPool 回调函数无状态化**：回调函数只读写 `in:`/`out:` 标准键或从 pkg 获取数据，不直接读写业务键
14. **Context 键去重管理**：多模块加载时使用集合跟踪已创建键，防止重复创建
15. **接口向后兼容**：新接口通过内部统一实现复用旧逻辑，旧接口保留不破坏现有调用

## 详细参考

- API详细参考：[reference.md](reference.md)
- 完整工程示例：[examples.md](examples.md)
