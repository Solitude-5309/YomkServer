# YomkServerPy 完整工程示例

## 示例1：模块化多服务工程

展示一个典型的模块化工程结构，包含多个Service协作、Context共享状态、EventLoop异步处理。

### 工程目录结构
```
src/
├── main.py
├── services/
│   ├── __init__.py
│   ├── user_service.py
│   ├── order_service.py
│   └── notification_service.py
├── utils/
│   └── common_functions.py   # FunctionPool公共函数
└── config/
    └── tasks.yaml
```

### user_service.py — 用户服务
```python
import YomkApi
from typing import Any

class UserService(YomkApi.YomkService):
    def __init__(self, server):
        super().__init__(server)
        self.set_name("/UserService")
    
    def init(self):
        self.install_func("/create_user", self.create_user)
        self.install_func("/get_user", self.get_user)
        
        # 初始化用户计数Context
        YomkApi.context_create("user_count", "0")
        print("UserService initialized")
    
    def create_user(self, pkg: Any) -> YomkApi.YomkResponse:
        # pkg为包含用户信息的字典
        username = pkg.get("username")
        email = pkg.get("email")
        
        print(f"creating user: {username}")
        
        # 更新用户计数（通过Context）
        count = int(YomkApi.context_get("user_count", "0")) + 1
        YomkApi.context_set("user_count", str(count))
        
        # 异步发送通知（通过EventLoop）
        YomkApi.event_loop_post("notification_loop",
            {"email": email, "msg": "Welcome!"}, None)
        
        return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "user created",
            {"user_id": "U001", "success": True})
    
    def get_user(self, pkg: Any) -> YomkApi.YomkResponse:
        # 实现获取用户逻辑
        return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "user found")
```

### order_service.py — 订单服务（跨服务调用）
```python
import YomkApi
from typing import Any

class OrderService(YomkApi.YomkService):
    def __init__(self, server):
        super().__init__(server)
        self.set_name("/OrderService")
    
    def init(self):
        self.install_func("/create_order", self.create_order)
        print("OrderService initialized")
    
    def create_order(self, pkg: Any) -> YomkApi.YomkResponse:
        user_id = pkg.get("user_id")
        product = pkg.get("product")
        amount = pkg.get("amount")
        
        # 跨服务调用：调用公共函数验证
        valid_resp = YomkApi.function_pool_call("validate_amount", str(amount))
        
        if valid_resp.status != YomkApi.ResStatus.eOk:
            return YomkApi.YomkResponse(YomkApi.ResStatus.eNo, "amount validation failed")
        
        print(f"order created for user: {user_id} amount: {amount}")
        
        return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "order created")
```

### common_functions.py — 公共函数池
```python
import YomkApi
from typing import Any

def validate_amount(pkg: Any) -> YomkApi.YomkResponse:
    try:
        amount = float(pkg)
        if amount <= 0:
            return YomkApi.YomkResponse(YomkApi.ResStatus.eNo, "invalid amount")
        return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "valid")
    except:
        return YomkApi.YomkResponse(YomkApi.ResStatus.eInvalid, "null amount")

def register_common_functions():
    YomkApi.function_pool_register("validate_amount", validate_amount)
```

### main.py — 程序入口
```python
import YomkApi
from services.user_service import UserService
from services.order_service import OrderService
from utils.common_functions import register_common_functions
from typing import Any

# 通知事件循环的默认处理函数
def notification_handler(pkg: Any) -> YomkApi.YomkResponse:
    target = pkg.get("email")
    content = pkg.get("msg")
    print(f"notify to: {target} content: {content}")
    return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "notified")

def main():
    # 1. 初始化服务器
    YomkApi.init(YomkApi.YomkServer(), ["YomkFunctionPool", "YomkContext", "YomkEventLoop"])
    
    # 2. 注册公共函数
    register_common_functions()
    
    # 3. 启动通知事件循环
    YomkApi.event_loop_start("notification_loop", notification_handler)
    
    # 4. 注册服务
    YomkApi.new_service(UserService, "/UserService")
    YomkApi.new_service(OrderService, "/OrderService")
    
    # 5. 设置Context监控
    YomkApi.context_turn_on_monitor()
    
    def monitor_user_count(ctx):
        print(f"user_count changed to: {ctx.value}")
    
    YomkApi.context_set_monitor("user_count", monitor_user_count)
    
    # 6. 业务调用
    resp = YomkApi.request("/UserService/create_user",
        {"username": "alice", "email": "alice@example.com"})
    
    if resp.status == YomkApi.ResStatus.eOk:
        print(f"User created: {resp.msg}")
    
    YomkApi.request("/OrderService/create_order",
        {"user_id": "U001", "product": "Widget", "amount": 99.99})
    
    input("Press Enter to exit...")
    
    # 清理事件循环
    YomkApi.event_loop_stop("notification_loop")
    YomkApi.event_loop_destroy("notification_loop")

if __name__ == "__main__":
    main()
```

## 示例2：配置驱动的服务设计（最佳实践）

展示如何使用配置驱动、API 风格调用、数据中转等 YomkServerPy 进阶理念。

### 场景：动态数据处理管道

用户需求：数据处理流程可通过配置文件定义，服务自动加载并执行，无需修改代码。

### 配置文件 pipeline.yaml

```yaml
# 数据处理管道定义
pipelines:
  - name: data_etl
    steps:
      - callback: extract
        input_ports:
          source:
            type: string
            value: "database"
        output_ports:
          raw_data:
            type: string

      - callback: transform
        input_ports:
          data:
            type: string
            value: ""
        output_ports:
          processed:
            type: string

      - callback: load
        input_ports:
          data:
            type: string
            value: ""
        output_ports:
          result:
            type: string
```

### pipeline_service.py

```python
import YomkApi
import yaml
from typing import Any

class PipelineService(YomkApi.YomkService):
    def __init__(self, server):
        super().__init__(server)
        self.set_name("/PipelineService")
        self.pipelines = {}
        self.created_keys = set()
    
    def init(self):
        # ✅ 只注册接口，不加载业务配置
        self.install_func("/load", self.load)
        self.install_func("/run", self.run)
        
        # ✅ 只创建服务内部必需的键
        YomkApi.context_create("pipeline:current", "")
        
        print("installed /load, /run")
    
    def load(self, pkg: Any) -> YomkApi.YomkResponse:
        path = pkg  # pkg为配置文件路径
        
        # 解析配置文件，构建管道定义
        with open(path, 'r') as f:
            config = yaml.safe_load(f)
        
        for pipe_yaml in config.get('pipelines', []):
            pipe_def = {
                'name': pipe_yaml['name'],
                'steps': []
            }
            
            # 加载步骤并自动创建端口键
            for step_yaml in pipe_yaml.get('steps', []):
                step_def = {
                    'callback': step_yaml['callback'],
                    'input_ports': {},
                    'output_ports': {}
                }
                
                for port_name, port_config in step_yaml.get('input_ports', {}).items():
                    step_def['input_ports'][port_name] = {
                        'type': port_config['type'],
                        'value': port_config['value']
                    }
                    
                    # ✅ 自动创建 Context 键（去重）
                    if port_name not in self.created_keys:
                        YomkApi.context_create(port_name, port_config['value'])
                        self.created_keys.add(port_name)
                
                for port_name, port_config in step_yaml.get('output_ports', {}).items():
                    step_def['output_ports'][port_name] = {
                        'type': port_config['type']
                    }
                
                pipe_def['steps'].append(step_def)
            
            self.pipelines[pipe_def['name']] = pipe_def
        
        return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, f"loaded {len(self.pipelines)} pipelines")
    
    def run(self, pkg: Any) -> YomkApi.YomkResponse:
        # ✅ API 风格：通过 pkg 传递管道名
        pipeline_name = pkg
        
        if pipeline_name not in self.pipelines:
            return YomkApi.YomkResponse(YomkApi.ResStatus.eNo, "pipeline not found")
        
        pipeline = self.pipelines[pipeline_name]
        
        # 执行管道步骤
        for step in pipeline['steps']:
            # 数据中转：业务键 → in: 键
            for port_name, port_def in step['input_ports'].items():
                value = YomkApi.context_get(port_name, port_def['value'])
                YomkApi.context_set(f"in:{port_name}", value)
            
            # 执行回调函数
            YomkApi.function_pool_call(step['callback'], None)
            
            # 数据中转：out: 键 → 业务键
            for port_name in step['output_ports'].keys():
                value = YomkApi.context_get(f"out:{port_name}", "")
                YomkApi.context_set(port_name, value)
        
        return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, f"pipeline {pipeline_name} completed")
```

### main.py

```python
import YomkApi
from typing import Any
from pipeline_service import PipelineService

# 定义回调函数
def extract_data(pkg: Any) -> YomkApi.YomkResponse:
    source = YomkApi.context_get("in:source", "")
    print(f"extracting from: {source}")
    YomkApi.context_set("out:raw_data", f"raw_data_from_{source}")
    return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "extracted")

def transform_data(pkg: Any) -> YomkApi.YomkResponse:
    data = YomkApi.context_get("in:data", "")
    print(f"transforming: {data}")
    YomkApi.context_set("out:processed", f"processed_{data}")
    return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "transformed")

def load_data(pkg: Any) -> YomkApi.YomkResponse:
    data = YomkApi.context_get("in:data", "")
    print(f"loading: {data}")
    YomkApi.context_set("out:result", f"loaded_{data}")
    return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "loaded")

def main():
    YomkApi.init(YomkApi.YomkServer(), ["YomkFunctionPool", "YomkContext"])
    
    # 注册回调函数
    YomkApi.function_pool_register("extract", extract_data)
    YomkApi.function_pool_register("transform", transform_data)
    YomkApi.function_pool_register("load", load_data)
    
    # 注册服务
    YomkApi.new_service(PipelineService, "/PipelineService")
    
    # ✅ 一行加载所有管道配置
    YomkApi.request("/PipelineService/load", "config/pipeline.yaml")
    
    # ✅ API 风格执行管道
    YomkApi.request("/PipelineService/run", "data_etl")
    
    input("Press Enter to exit...")

if __name__ == "__main__":
    main()
```

### 设计理念总结

| 实践 | 说明 |
|------|------|
| **配置驱动** | 处理流程在 YAML 中定义，服务自动解析创建 |
| **API 风格** | `/run` 通过 pkg 传管道名，简洁明了 |
| **数据中转** | 回调函数只读写 `in:`/`out:` 键，与配置解耦 |
| **init() 纯净** | 只注册接口，不加载业务配置 |
| **键去重** | `created_keys` 防止重复创建 |
| **向后兼容** | 保留 `/load_single` 单个加载接口 |

## 示例3：Context Checker/Monitor 完整流程

```python
import YomkApi
from typing import Any

def main():
    # 初始化
    YomkApi.init(YomkApi.YomkServer(), ["YomkContext"])
    
    # 创建Context
    YomkApi.context_create("config", "default")
    
    # 定义检查函数：只允许非空字符串
    def non_empty_checker(ctx) -> YomkApi.CheckStatus:
        if not ctx.value or str(ctx.value).strip() == "":
            return YomkApi.CheckStatus.eReject
        return YomkApi.CheckStatus.eAccept
    
    # 定义监控函数：记录所有变更
    def change_logger(ctx):
        print(f"key={ctx.key} new_value={ctx.value}")
    
    # 启用检查器
    YomkApi.context_turn_on_checker()
    YomkApi.context_set_checker("config", non_empty_checker)
    
    # 启用监控器
    YomkApi.context_turn_on_monitor()
    YomkApi.context_set_monitor("config", change_logger)
    
    # 测试
    YomkApi.context_set("config", "new_value")  # 通过检查并触发监控
    YomkApi.context_set("config", "")          # 被检查器拒绝
    
    input("Press Enter to exit...")

if __name__ == "__main__":
    main()
```

## 示例4：EventLoop 异步事件处理

```python
import YomkApi
import threading
from typing import Any

g_count = 0

# 事件处理函数
def task_handler(pkg: Any) -> YomkApi.YomkResponse:
    tid = threading.get_ident()
    print(f"[thread {tid}] processing in thread with pkg: {pkg}")
    
    global g_count
    g_count += 1
    
    # 模拟事件链式投递
    if g_count < 4:
        YomkApi.event_loop_post("worker_loop", f"task_{g_count}", None)
    
    # 模拟耗时任务
    import time
    time.sleep(0.1)
    
    return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "task done")

def main():
    tid = threading.get_ident()
    
    # 初始化
    YomkApi.init(YomkApi.YomkServer(), ["YomkContext", "YomkFunctionPool", "YomkEventLoop"])
    
    # 启动事件循环
    res = YomkApi.event_loop_start("worker_loop", task_handler)
    print(f"[thread {tid}] start event loop: {res.msg}")
    
    # 异步投递（不等待结果）
    res = YomkApi.event_loop_post("worker_loop", "task_1", None)
    print(f"[thread {tid}] post event: {res.msg}")
    
    # 同步投递（等待结果）
    res = YomkApi.event_loop_post_wait("worker_loop", "task_wait", None)
    print(f"[thread {tid}] post_wait event: {res.msg}")
    print(f"[thread {tid}] event id: {res.data.m_eventId}, "
          f"event loop name: {res.data.m_eventLoopName}, "
          f"response: {res.data.m_response.msg}")
    
    # 停止和销毁
    input("Press Enter to stop event loop: ")
    res = YomkApi.event_loop_stop("worker_loop")
    print(f"[thread {tid}] stop event loop: {res.msg}")
    
    input("Press Enter to destroy event loop: ")
    res = YomkApi.event_loop_destroy("worker_loop")
    print(f"[thread {tid}] destroy event loop: {res.msg}")

if __name__ == "__main__":
    main()
```

## 示例5：异步请求处理

```python
import YomkApi
from typing import Any
import time

class TaskService(YomkApi.YomkService):
    def __init__(self, server):
        super().__init__(server)
        self.set_name("/TaskService")
    
    def init(self):
        self.install_func("/long_task", self.long_task)
    
    def long_task(self, pkg: Any) -> YomkApi.YomkResponse:
        # 模拟长时间运行的任务
        time.sleep(1)
        return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "task completed")

def callback(res: YomkApi.YomkResponse) -> None:
    tid = threading.get_ident()
    print(f"[thread {tid}] async task result: {res.msg}")

def main():
    import threading
    tid = threading.get_ident()
    
    YomkApi.init(YomkApi.YomkServer(), ["YomkFunctionPool", "YomkContext"])
    
    YomkApi.new_service(TaskService, "/TaskService")
    
    # 异步请求
    YomkApi.async_request("/TaskService/long_task", None, callback)
    
    print(f"[thread {tid}] main thread continues...")
    input("Press Enter to exit...")

if __name__ == "__main__":
    main()
```
