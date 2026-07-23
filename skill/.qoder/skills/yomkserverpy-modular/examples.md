# YomkServerPy 工程示例

## 示例0：标准工程模板（创建新工程时必须生成）

将 `ProjectName` 替换为用户指定的工程名，所有文件必须完整生成。

### 目录结构
```
ProjectName/
├── main.py
├── boot/
│   └── MyBoot.py
├── config/
│   └── config.json
├── msgs/
│   └── YomkMsgs.py
├── services/
│   └── ConfigService.py
├── typedefine/
│   └── TypeDefine.py
├── test/
├── scripts/
└── README.md
```

### main.py
```python
import YomkApi
from pathlib import Path
import sys
sys.path.append(Path.cwd())
from boot.MyBoot import MyBoot

YomkApi.boot(MyBoot(["/ConfigService"]))
input("ProjectName is running, press Enter to exit.")
```

### boot/MyBoot.py
```python
import YomkApi
from pathlib import Path
from typedefine.TypeDefine import CTX_CONFIG_PATH
from services.ConfigService import ConfigService
from msgs.YomkMsgs import *

class MyBoot(YomkApi.YomkBoot):
    def __init__(self, srv_names):
        super().__init__()
        self.srv_names = srv_names

    def before(self):
        # 通过 __file__ 推导配置文件路径
        project_dir = Path(__file__).resolve().parent.parent
        config_path = str(project_dir / "config" / "config.json")
        YomkApi.context_create(CTX_CONFIG_PATH, config_path)
        print(f"MyBoot::before config path: {config_path}")
        return 0

    def start(self):
        # 服务创建器映射表
        cur_srvs = {
            "/ConfigService": ConfigService(YomkApi.server()),
        }

        # 按需启动
        for srv_name in self.srv_names:
            if srv_name in cur_srvs:
                YomkApi.add_service(cur_srvs[srv_name], srv_name)
                print(f"MyBoot::start service {srv_name} done")

        return 0

    def after(self):
        resp = YomkApi.request("/ConfigService/load", None)
        if resp.status != YomkApi.ResStatus.eOk:
            print(f"MyBoot::after load config failed: {resp.msg}")
            return -1
        print("MyBoot::after ProjectName started successfully.")

        resp = YomkApi.request("/ConfigService/get", ConfigKey("name"))
        if resp.status != YomkApi.ResStatus.eOk:
            print(f"MyBoot::after get config name failed: {resp.msg}")
            return -1
        print(f"MyBoot::after config name: {resp.data}")

        resp = YomkApi.request("/ConfigService/get", ConfigKey("version"))
        if resp.status != YomkApi.ResStatus.eOk:
            print(f"MyBoot::after get config version failed: {resp.msg}")
            return -1
        print(f"MyBoot::after config version: {resp.data}")

        resp = YomkApi.request("/ConfigService/get", ConfigKey("description"))
        if resp.status != YomkApi.ResStatus.eOk:
            print(f"MyBoot::after get config description failed: {resp.msg}")
            return -1
        print(f"MyBoot::after config description: {resp.data}")

        return 0
```

### typedefine/TypeDefine.py
```python
# Context Keys
CTX_CONFIG_PATH = "config_path"
```

### msgs/YomkMsgs.py
```python
# ConfigService 消息包
class ConfigKey:
    def __init__(self, key: str):
        self.key = key

class ConfigKeyValue:
    def __init__(self, key: str, value: str):
        self.key = key
        self.value = value
```

### services/ConfigService.py
```python
import YomkApi
import json
from typedefine.TypeDefine import CTX_CONFIG_PATH

class ConfigService(YomkApi.YomkService):
    def __init__(self, server):
        super().__init__(server)
        self.set_name("/ConfigService")
        self.config_path = ""
        self.config_json = {}

    def init(self):
        self.install_func("/load", self.load_config)
        self.install_func("/get", self.get_config)
        self.install_func("/set", self.set_config)
        self.install_func("/reload", self.reload_config)
        print(f"ConfigService::init install func [ /load /get /set /reload ] to {self.get_name()}")

    def load_config(self, pkg):
        self.config_path = YomkApi.context_get(CTX_CONFIG_PATH, "")
        if not self.config_path:
            return YomkApi.YomkResponse(YomkApi.ResStatus.eNo, "config_path not found in context")
        try:
            with open(self.config_path, 'r', encoding='utf-8') as f:
                self.config_json = json.load(f)
        except Exception as e:
            return YomkApi.YomkResponse(YomkApi.ResStatus.eNo, f"failed to open: {self.config_path}, {e}")
        print(f"ConfigService::load_config loaded: {self.config_path}")
        return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "ok")

    def get_config(self, pkg):
        tokens = pkg.key.split('.')
        current = self.config_json
        for token in tokens:
            if not isinstance(current, dict) or token not in current:
                return YomkApi.YomkResponse(YomkApi.ResStatus.eNo, "key not found: " + pkg.key)
            current = current[token]

        if isinstance(current, str):
            value = current
        else:
            value = json.dumps(current, ensure_ascii=False)
        return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "ok", value)

    def set_config(self, pkg):
        tokens = pkg.key.split('.')
        current = self.config_json
        for token in tokens[:-1]:
            if not isinstance(current, dict):
                return YomkApi.YomkResponse(YomkApi.ResStatus.eNo, "invalid key path: " + pkg.key)
            current = current.setdefault(token, {})
        current[tokens[-1]] = pkg.value
        print(f"ConfigService::set_config set {pkg.key} = {pkg.value}")
        return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "ok")

    def reload_config(self, pkg):
        try:
            with open(self.config_path, 'r', encoding='utf-8') as f:
                self.config_json = json.load(f)
        except Exception as e:
            return YomkApi.YomkResponse(YomkApi.ResStatus.eNo, f"failed to reload: {self.config_path}, {e}")
        print(f"ConfigService::reload_config reloaded: {self.config_path}")
        return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "ok")
```

### config/config.json
```json
{
    "name": "ProjectName",
    "version": "2.2.9",
    "description": "Create a new project based on YomkServerPy"
}
```

### README.md
```markdown
# ProjectName

基于 [YomkServerPy](https://github.com/Solitude-5309/YomkServerPy) 模块化框架的工程。  
YomkServerPy 是基于 Python 的模块化服务开发框架，核心设计理念：**「一切皆服务，一切皆请求」**。

## 前置条件

- Python >= 3.10
- YomkServerPy 已安装（`PYTHONPATH` 指向安装路径下的 `lib/pythonX.Y/site-packages`）

## 运行

export PYTHONPATH=~/YomkServer/install/lib/python3.10/site-packages:$PYTHONPATH
python main.py

## 工程结构

| 目录 | 职责 |
|------|------|
| `boot/` | 生命周期管理（before/start/after） |
| `config/` | 配置文件 |
| `msgs/` | 消息包定义 |
| `services/` | 服务实现 |
| `typedefine/` | 公共常量定义 |
| `test/` | 单元测试脚本 |
| `scripts/` | 项目辅助脚本 |
```

---

## 示例1：扩展业务服务（在已有工程中添加新服务）

演示在示例0基础上添加 `UserService`，展示完整的扩展流程。

### 1. 添加消息包（msgs/YomkMsgs.py 追加）
```python
# UserService 消息包
class UserQuery:
    def __init__(self, user_id: str):
        self.user_id = user_id

class UserInfo:
    def __init__(self, user_id: str, name: str, age: int):
        self.user_id = user_id
        self.name = name
        self.age = age
```

### 2. 创建服务（services/UserService.py）
```python
import YomkApi
from msgs.YomkMsgs import *

class UserService(YomkApi.YomkService):
    def __init__(self, server):
        super().__init__(server)
        self.set_name("/UserService")

    def init(self):
        self.install_func("/get_user", self.get_user)
        self.install_func("/create_user", self.create_user)
        print(f"UserService::init install func [ /get_user /create_user ] to {self.get_name()}")

    def get_user(self, pkg):
        print(f"UserService::get_user query user: {pkg.user_id}")
        # 业务逻辑...
        info = UserInfo(pkg.user_id, "Alice", 25)
        return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "ok", info)

    def create_user(self, pkg):
        print(f"UserService::create_user create user: {pkg.name}")
        # 跨服务调用示例：读取配置
        cfg_resp = YomkApi.request("/ConfigService/get", ConfigKey("name"))
        return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "user created")
```

### 3. 注册到 Boot（boot/MyBoot.py 修改）
```python
# 头部添加
from services.UserService import UserService

# start() 映射表中添加
cur_srvs = {
    "/ConfigService": ConfigService(YomkApi.server()),
    "/UserService": UserService(YomkApi.server()),
}
```

### 4. 启动列表（main.py 修改）
```python
YomkApi.boot(MyBoot(["/ConfigService", "/UserService"]))
```

---

## 示例2：Context Checker/Monitor

```python
import YomkApi

# 检查函数：只允许非空字符串
def non_empty_checker(ctx) -> YomkApi.CheckStatus:
    if not ctx.value or str(ctx.value).strip() == "":
        return YomkApi.CheckStatus.eReject
    return YomkApi.CheckStatus.eAccept

# 监控函数：记录变更
def change_logger(ctx):
    print(f"key={ctx.key} new_value={ctx.value}")

# 使用
YomkApi.context_create("config", "default")
YomkApi.context_turn_on_checker()
YomkApi.context_set_checker("config", non_empty_checker)
YomkApi.context_turn_on_monitor()
YomkApi.context_set_monitor("config", change_logger)

YomkApi.context_set("config", "new_value")  # 通过检查并触发监控
YomkApi.context_set("config", "")           # 被拒绝
```

## 示例3：EventLoop 异步事件处理

```python
import YomkApi
import threading

# 事件处理函数
def task_handler(pkg) -> YomkApi.YomkResponse:
    print(f"[thread {threading.get_ident()}] processing: {pkg}")
    return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "task done")

# 启动事件循环
YomkApi.event_loop_start("worker_loop", task_handler)

# 异步投递
YomkApi.event_loop_post("worker_loop", "task_1", None)

# 同步投递（等待结果）
resp = YomkApi.event_loop_post_wait("worker_loop", "task_2", None)
if resp.status == YomkApi.ResStatus.eOk:
    event = resp.data
    print(f"event result: {event.m_response.msg}")

# 清理
YomkApi.event_loop_stop("worker_loop")
YomkApi.event_loop_destroy("worker_loop")
```

## 示例4：FunctionPool 公共函数池

```python
import YomkApi

# 定义公共函数
def validate_amount(pkg) -> YomkApi.YomkResponse:
    try:
        amount = float(pkg)
        if amount <= 0:
            return YomkApi.YomkResponse(YomkApi.ResStatus.eNo, "invalid amount")
        return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "valid")
    except:
        return YomkApi.YomkResponse(YomkApi.ResStatus.eNo, "invalid amount")

# 注册 + 调用
YomkApi.function_pool_register("validate_amount", validate_amount)
resp = YomkApi.function_pool_call("validate_amount", "100.5")
```
