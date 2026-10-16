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
from boot.MyBoot import MyBoot

YomkApi.boot(MyBoot(["/ConfigService"]))
print("main: running, press Enter to exit.")
input()
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
        print("MyBoot::before starting...")

        # 通过 __file__ 推导配置文件路径
        project_dir = Path(__file__).resolve().parent.parent
        config_path = str(project_dir / "config" / "config.json")
        YomkApi.context_create(CTX_CONFIG_PATH, config_path)
        print(f"MyBoot::before config path: {config_path}")
        return 0

    def start(self):
        # 服务创建器映射表
        cur_srvs = {
            "/ConfigService": lambda: ConfigService(YomkApi.server()),
        }

        # 根据 srv_names 按需启动服务
        for srv_name in self.srv_names:
            if srv_name in cur_srvs:
                if YomkApi.add_service(cur_srvs[srv_name](), srv_name) != 0:
                    return -1
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

        # 测试版本号接口：获取并输出工程版本号（typedefine/TypeDefine.py 定义的 APP_VERSION 常量）
        resp = YomkApi.request("/ConfigService/version", None)
        if resp.status != YomkApi.ResStatus.eOk:
            print(f"MyBoot::after get version failed: {resp.msg}")
            return -1
        print(f"MyBoot::after project version: {resp.data}")

        resp = YomkApi.request("/ConfigService/get", ConfigKey("description"))
        if resp.status != YomkApi.ResStatus.eOk:
            print(f"MyBoot::after get config description failed: {resp.msg}")
            return -1
        print(f"MyBoot::after config description: {resp.data}")

        return 0
```

### typedefine/TypeDefine.py
```python
"""
Type Define
统一存放常量定义、宏定义、类型定义等可复用公共定义
"""

# Context
CTX_CONFIG_PATH = "config_path"

# 工程版本号（单一来源：本文件；python 工程无编译期注入，对应 main 分支 project(VERSION) 编译期注入的 APP_VERSION 宏）
APP_VERSION = "0.0.1"
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
from typedefine.TypeDefine import CTX_CONFIG_PATH, APP_VERSION

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
        self.install_func("/version", self.version)
        print(f"ConfigService::init install func [ /load /get /set /reload /version ] to {self.get_name()}")

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

    def version(self, pkg):
        # 版本号来自 typedefine/TypeDefine.py 定义的 APP_VERSION 常量（对应 main 分支 project(VERSION) 编译期注入）
        return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "ok", APP_VERSION)
```

### config/config.json
```json
{
    "name": "ProjectName",
    "description": "Create a new project based on YomkServerPy"
}
```

### README.md
```markdown
# ProjectName

基于 [YomkServerPy](https://github.com/Solitude-5309/YomkServerPy) 模块化框架的空白工程模板。

YomkServerPy 是基于 Python 的模块化服务开发框架，核心设计理念：**「一切皆服务，一切皆请求」**。

## 前置条件

- Python >= 3.10
- YomkServerPy 已安装到系统

### 安装 YomkServerPy

# 在 YomkServerPy 源码目录下，一键编译安装（默认安装到 /opt/yomk，可交互自定义路径）
./build_ubuntu.sh

安装后会自动配置环境变量 `YOMK_PREFIX_PATH` 指向安装路径，并将 `PYTHONPATH` 指向安装路径下的 site-packages（任意终端可 `import YomkApi`）。

## 工程结构

ProjectName/
├── main.py                 # 程序入口
├── boot/
│   └── MyBoot.py           # 生命周期管理
├── config/
│   └── config.json         # 配置文件
├── msgs/
│   └── YomkMsgs.py         # 消息包定义
├── services/               # 服务实现（按业务分子目录）
│   └── ConfigService.py
├── typedefine/
│   └── TypeDefine.py       # 公共常量定义（含工程版本号 APP_VERSION）
├── test/                   # 单元测试文件
├── scripts/                # 项目辅助脚本
├── check_env.sh            # 环境检查脚本
└── README.md

| 目录 | 职责 |
|------|------|
| `boot/` | 程序生命周期管理（before/start/after），路径推导、Context 创建、按需启动服务 |
| `config/` | 配置文件目录 |
| `msgs/` | 所有服务间通信的消息包定义集中管理 |
| `services/` | 所有服务实现，按业务类别分子目录存放 |
| `typedefine/` | 常量定义、宏定义、类型别名等公共定义 |

## Linux 运行

bash check_env.sh          # 检查 YomkServerPy 运行环境（PYTHONPATH 由框架安装环节配置）
python main.py

`check_env.sh` 检查 python3 可用性并验证 `import YomkApi`，输出框架版本；失败时提示 PYTHONPATH 应指向的 site-packages 路径。

## 生命周期说明

`MyBoot` 管理程序三阶段生命周期：

| 阶段 | 方法 | 用途 |
|------|------|------|
| 启动前 | `before()` | 创建 Context、EventLoop、注册 FunctionPool、设置日志代理等 |
| 启动中 | `start()` | 注册并启动服务 |
| 启动后 | `after()` | 调用服务接口做初始化、自启动任务等 |

## 核心组件速查

| 组件 | 用途 | YomkApi 接口 |
|------|------|--------------|
| YomkContext | 全局 K-V 状态管理 | `context_create` / `context_get` / `context_set` / `context_destroy` |
| YomkEventLoop | 线程隔离事件循环 | `event_loop_start` / `event_loop_post` / `event_loop_post_wait` |
| YomkFunctionPool | 动态公共函数池 | `function_pool_register` / `function_pool_call` |
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
