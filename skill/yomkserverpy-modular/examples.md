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
YomkApi.console_log_info_tag("main", "running, press Enter to exit.")
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
        YomkApi.console_log_info_tag("MyBoot::before", "starting...")

        # 通过 __file__ 推导配置文件路径
        project_dir = Path(__file__).resolve().parent.parent
        config_path = str(project_dir / "config" / "config.json")
        YomkApi.context_create(CTX_CONFIG_PATH, config_path)
        YomkApi.console_log_info_tag(
            "MyBoot::before", "config path: ", config_path)
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
                YomkApi.console_log_info_tag(
                    "MyBoot::start", "service ", srv_name, " done")

        return 0

    def after(self):
        resp = YomkApi.request("/ConfigService/load", None)
        if resp.status != YomkApi.ResStatus.eOk:
            YomkApi.console_log_error_tag(
                "MyBoot::after", "load config failed: ", resp.msg)
            return -1
        YomkApi.console_log_info_tag("MyBoot::after", "started successfully.")

        resp = YomkApi.request("/ConfigService/get", ConfigKey("name"))
        if resp.status != YomkApi.ResStatus.eOk:
            YomkApi.console_log_error_tag(
                "MyBoot::after", "get config name failed: ", resp.msg)
            return -1
        YomkApi.console_log_info_tag(
            "MyBoot::after", "config name: ", resp.data)

        # 测试版本号接口：获取并输出工程版本号（typedefine/TypeDefine.py 定义的 APP_VERSION 常量）
        resp = YomkApi.request("/ConfigService/version", None)
        if resp.status != YomkApi.ResStatus.eOk:
            YomkApi.console_log_error_tag(
                "MyBoot::after", "get version failed: ", resp.msg)
            return -1
        YomkApi.console_log_info_tag(
            "MyBoot::after", "project version: ", resp.data)

        resp = YomkApi.request("/ConfigService/get", ConfigKey("description"))
        if resp.status != YomkApi.ResStatus.eOk:
            YomkApi.console_log_error_tag(
                "MyBoot::after", "get config description failed: ", resp.msg)
            return -1
        YomkApi.console_log_info_tag(
            "MyBoot::after", "config description: ", resp.data)

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

# 工程版本号（单一来源：本文件）
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
        YomkApi.console_log_info_tag("ConfigService::init",
                                     "install func [ /load /get /set /reload /version ] to ", self.get_name())

    def load_config(self, pkg):
        self.config_path = YomkApi.context_get(CTX_CONFIG_PATH, "")
        if not self.config_path:
            return YomkApi.YomkResponse(YomkApi.ResStatus.eNo, "config_path not found in context")
        try:
            with open(self.config_path, 'r', encoding='utf-8') as f:
                self.config_json = json.load(f)
        except Exception as e:
            return YomkApi.YomkResponse(YomkApi.ResStatus.eNo, f"failed to open: {self.config_path}, {e}")
        YomkApi.console_log_info_tag(
            "ConfigService::load_config", "loaded: ", self.config_path)
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
        YomkApi.console_log_info_tag(
            "ConfigService::set_config", "set ", pkg.key, " = ", pkg.value)
        return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "ok")

    def reload_config(self, pkg):
        try:
            with open(self.config_path, 'r', encoding='utf-8') as f:
                self.config_json = json.load(f)
        except Exception as e:
            return YomkApi.YomkResponse(YomkApi.ResStatus.eNo, f"failed to reload: {self.config_path}, {e}")
        YomkApi.console_log_info_tag(
            "ConfigService::reload_config", "reloaded: ", self.config_path)
        return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "ok")

    def version(self, pkg):
        # 版本号来自 typedefine/TypeDefine.py 定义的 APP_VERSION 常量
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
````markdown
# ProjectName

基于 [YomkServerPy](https://github.com/Solitude-5309/YomkServerPy) 模块化框架的空白工程模板。

YomkServerPy 是基于 Python 的模块化服务开发框架，核心设计理念：**「一切皆服务，一切皆请求」**。

## 前置条件

- Python >= 3.10
- YomkServerPy 已安装到系统

### 安装 YomkServerPy

```bash
# 在 YomkServerPy 源码目录下，一键编译安装（默认安装到 /opt/yomk，可交互自定义路径）
./build_ubuntu.sh
```

安装后会自动配置环境变量 `YOMK_PREFIX_PATH` 指向安装路径，并将 `PYTHONPATH` 指向安装路径下的 site-packages（任意终端可 `import YomkApi`）。

## 工程结构

```
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
```

| 目录 | 职责 |
|------|------|
| `boot/` | 程序生命周期管理（before/start/after），路径推导、Context 创建、按需启动服务 |
| `config/` | 配置文件目录 |
| `msgs/` | 所有服务间通信的消息包定义集中管理 |
| `services/` | 所有服务实现，按业务类别分子目录存放 |
| `typedefine/` | 常量定义、宏定义、类型别名等公共定义 |

## Linux 运行

```bash
bash check_env.sh          # 检查 YomkServerPy 运行环境（PYTHONPATH 由框架安装环节配置）
python main.py
```

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
| YomkLogger | 多级别日志系统 | `console_log_info` / `console_log_error` / `file_log_create` |
````

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

# 注册 + 调用（三参声明期望消息类型、四参附函数说明，均仅内省元数据）
YomkApi.function_pool_register("validate_amount", validate_amount)
YomkApi.function_pool_register("calc_sum", calc_sum, "String", "计算输入之和")  # 四参：类型 + 说明
resp = YomkApi.function_pool_call("validate_amount", "100.5")

# 单函数内省：命中 msg 恒 "ok"，data 为 FuncInfo 包（func_name/msg_name/description）
resp = YomkApi.function_pool_info_name("calc_sum")  # eOk, msg_name = "String"，description = "计算输入之和"
```

## 示例5：服务内省（调试）

期望消息类型由 `install_func` 三参形式声明、函数说明由四参声明（均仅作内省元数据，不参与运行时校验；说明经 `/function` 结构化包透出；两参写法兼容，内省无类型）：

```python
class DemoService(YomkApi.YomkService):
    def init(self):
        self.install_func("/with_type", self.with_type, "MyData")  # 三参：内省可见类型 MyData
        self.install_func("/with_desc", self.with_desc, "MyData", "函数说明")  # 四参：类型 + 说明
        self.install_func("/no_type", self.no_type)                # 兼容写法，内省无类型
        return 0

# 服务列表（data 为字符串列表）
resp = YomkApi.server_info_services()
# resp.data: ["/DemoService", "/YomkContext", "/YomkEventLoop", ...]

# 指定服务的函数结构化清单（data 为 FuncInfo 列表，与单查同族）
resp = YomkApi.server_info_functions("/DemoService")
# resp.data: [FuncInfo("/no_type"), FuncInfo("/with_type", "MyData"),
#             FuncInfo("/with_desc", "MyData", "函数说明")]

# 单函数元信息（命中 msg 恒 "ok"，data 为 FuncInfo 包：func_name/msg_name/description）
resp = YomkApi.server_info_function("/DemoService/with_desc")  # eOk, msg_name = "MyData", description = "函数说明"
resp = YomkApi.server_info_function("/DemoService/with_type")  # eOk, msg_name = "MyData"
resp = YomkApi.server_info_function("/DemoService/no_type")    # eOk, msg_name 为空串（未声明），description 为空
resp = YomkApi.server_info_function("/DemoService/not_exist")  # eNo

# 全量拓扑：ServerServiceInfo 层级列表（service_name 服务名 + funcs 函数元信息列表，与单查同族）
resp = YomkApi.server_info_all()
```

说明：自省读的是运行期服务表，注册/删除即时反映到下一次查询；FunctionPool 动态注册函数的类型元数据由 `function_pool_register` 第三参声明，未声明内省显示为空。四个内置模块另提供模块内层内省三件套（Context 的 `context_info_keys/key/all`、EventLoop 的 `event_loop_info_loops/loop/all`、FunctionPool 的 `function_pool_info_names/name/all`、Logger 的 `logger_info_loggers/logger/all`），API 见 [reference.md](reference.md)。完整可运行示例见仓库 `examples/ExampleYomkServerInfo.py`。

## 示例6：日志

```python
# 控制台四级别（无 tag 便捷版缺省 tag "MainLogger"；tag 版 tag 随记录显示并兼作日志器名，首次使用自动创建）
YomkApi.console_log_info("application started")
YomkApi.console_log_warn_tag("Security", "suspicious activity")

# 级别开关（全局，只影响控制台；文件日志不受影响）
YomkApi.off_console_log_debug()   # 同族 on/off_console_log_info/warn/error

# 文件日志：创建日志器 → 写内存缓冲 → 显式落盘为 dir/name.log
YomkApi.file_log_create("/var/log/myapp", "app")
YomkApi.file_log_info("app", "application started")   # 同族 warn/error/debug；_tag 版可自定义 tag
YomkApi.file_log_write("app")

# 删除日志器：单端点同时清理 console/file 两表；未落盘的缓冲自动落盘，
# 但不删除磁盘上的 .log 文件（数据保全，清理归调用方）
resp = YomkApi.file_log_delete("app")   # eOk, msg: "deleted console:0 file:1"
YomkApi.file_log_delete("app")          # eNo（已删除）
# 注："MainLogger" 无特殊保护，删除后下次写控制台日志会惰性重建

# 控制台日志代理：每条控制台日志先过代理，返回 False 消费该条（不再输出）
def my_log_proxy(log: YomkApi.Log) -> bool:
    print(f"[{log.m_tag}] {log.m_log}")
    return False

YomkApi.set_console_log_proxy(my_log_proxy)
# 卸载代理：传 None 恢复框架默认控制台输出，
# logger_info_all() 状态段 proxy_on 随之由 True 回到 False
YomkApi.set_console_log_proxy(None)
```

完整可运行示例见仓库 `examples/ExampleYomkLogger.py`（四级别、级别开关、代理消费/放行、文件日志回读与删除后惰性重建）。

---

## 示例7：创建扩展库（独立扩展包）

扩展为普通 Python 包（包名与扩展名同名，如 `TemplateExtension`），仅含 `/version` 接口；`install.sh` 将扩展包装入 YomkServerPy 的 site-packages，安装后任意终端可 `import TemplateExtension`。占位符替换：`ExtensionName` → 用户指定名（包目录、import、版本字符串前缀同步替换）。

### 目录结构

```
ExtensionName/
├── ExtensionName/
│   ├── __init__.py          // 包入口（re-export 扩展版本号与服务类）
│   ├── version.py           // 扩展版本号 EXTENSION_VERSION（单一来源）
│   └── xxx_service.py       // 服务实现
├── test/
│   └── test_xxx.py          // 测试程序（就地运行，不随扩展安装）
├── install.sh               // 扩展安装脚本（交互式）
└── README.md
```

### ExtensionName/__init__.py
```python
"""TemplateExtension 扩展包"""

from .version import EXTENSION_VERSION
from .extension_service import ExtensionService
```

### ExtensionName/version.py
```python
"""TemplateExtension 版本定义"""

# 扩展版本号（单一来源：本文件）
EXTENSION_VERSION = "0.0.1"
```

### ExtensionName/xxx_service.py
```python
import YomkApi
from .version import EXTENSION_VERSION


class ExtensionService(YomkApi.YomkService):
    def __init__(self, server):
        super().__init__(server)
        self.set_name("/ExtensionService")

    def init(self):
        self.install_func("/version", self.version)
        YomkApi.console_log_info_tag("ExtensionService::init",
                                     "install func [ /version ] to ", self.get_name())
        return 0

    def version(self, pkg):
        # 版本号：EXTENSION_VERSION 常量（定义于 version.py）
        return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "ok", f"TemplateExtension v{EXTENSION_VERSION}")
```

### test/test_xxx.py
```python
#!/usr/bin/env python3
"""TestTemplateExtension —— 扩展功能测试程序（就地运行，不随扩展安装）"""

import sys

from TemplateExtension import ExtensionService
import YomkApi

g_pass = 0
g_fail = 0


def main():
    global g_pass, g_fail

    YomkApi.init()

    # 注册 ExtensionService（扩展服务需先注册才能使用）
    YomkApi.new_service(ExtensionService, "/ExtensionService")

    # 测试版本查询
    resp = YomkApi.request("/ExtensionService/version", None)
    if resp.status == YomkApi.ResStatus.eOk:
        print(f"[PASS] version: {resp.data}")
        g_pass += 1
    else:
        print(f"[FAIL] version request failed: {resp.msg}")
        g_fail += 1

    print("\n========== Test Summary ==========")
    print(f"PASS: {g_pass}")
    print(f"FAIL: {g_fail}")

    sys.exit(1 if g_fail > 0 else 0)


if __name__ == "__main__":
    main()
```

### install.sh
```bash
#!/bin/bash
# 扩展安装脚本（交互式）
# 用法: bash install.sh
# 交互询问 YomkServerPy 安装路径（前置路径），默认取环境变量 YOMK_PREFIX_PATH，可修改
# 将扩展包安装到框架 site-packages（与框架安装到一起），安装后任意终端可 import TemplateExtension

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EXTENSION_NAME="TemplateExtension"

# 路径规范化：展开 ~ 、相对路径补全
_normalize_path() {
    local p="$1"
    p="${p/#\~/$HOME}"
    if [[ -n "${p}" && "${p}" != /* ]]; then
        p="$(pwd)/${p}"
    fi
    echo "${p}"
}

if [ -z "${YOMK_PREFIX_PATH}" ]; then
    echo "警告: 未检测到环境变量 YOMK_PREFIX_PATH，可能未通过 build_ubuntu.sh 安装 YomkServerPy，请手动输入安装路径"
fi

# 交互询问 YomkServerPy 安装路径（前置路径），默认取环境变量 YOMK_PREFIX_PATH
read -r -p "请输入 YomkServerPy 安装路径 [默认: ${YOMK_PREFIX_PATH:-无}]: " _INPUT_PREFIX
YOMK_SERVER_PATH="$(_normalize_path "${_INPUT_PREFIX:-${YOMK_PREFIX_PATH}}")"
unset _INPUT_PREFIX
if [ -z "${YOMK_SERVER_PATH}" ]; then
    echo "错误: 未指定 YomkServerPy 安装路径"
    exit 1
fi
echo "-- YomkServerPy 安装路径: ${YOMK_SERVER_PATH}"

# 探测 site-packages 目录（lib/pythonX.Y/site-packages）
SITE_PACKAGES="$(ls -d "${YOMK_SERVER_PATH}"/lib/python*/site-packages 2>/dev/null | head -n 1)"
if [ -z "${SITE_PACKAGES}" ]; then
    echo "错误: 未找到 site-packages 目录: ${YOMK_SERVER_PATH}/lib/pythonX.Y/site-packages"
    exit 1
fi

# 校验框架模块（扩展 YomkServerPy*.so/.pyd + 门面 YomkApi.py）
shopt -s nullglob
_EXT_FILES=("${SITE_PACKAGES}"/YomkServerPy*.so "${SITE_PACKAGES}"/YomkServerPy*.pyd)
shopt -u nullglob
if [ ${#_EXT_FILES[@]} -eq 0 ] || [ ! -f "${SITE_PACKAGES}/YomkApi.py" ]; then
    echo "错误: site-packages 中未找到 YomkServerPy 扩展模块或 YomkApi.py: ${SITE_PACKAGES}"
    unset _EXT_FILES
    exit 1
fi
unset _EXT_FILES
echo "-- 框架模块校验通过: ${SITE_PACKAGES}"

# 安装目录不可写时（如 /opt/yomk）使用 sudo 执行安装
SUDO=""
if [ ! -w "${SITE_PACKAGES}" ]; then
    SUDO="sudo"
fi

# 安装扩展包（幂等覆盖旧版本）
if [ -d "${SITE_PACKAGES}/${EXTENSION_NAME}" ]; then
    echo "-- 移除旧版本: ${SITE_PACKAGES}/${EXTENSION_NAME}"
    ${SUDO} rm -rf "${SITE_PACKAGES}/${EXTENSION_NAME}"
fi
${SUDO} cp -r "${SCRIPT_DIR}/${EXTENSION_NAME}" "${SITE_PACKAGES}/"

# 安装后验证：新终端等价环境（仅 PYTHONPATH 指向 site-packages）下导入扩展
if PYTHONPATH="${SITE_PACKAGES}" python3 -c "import ${EXTENSION_NAME}; print('扩展版本:', ${EXTENSION_NAME}.EXTENSION_VERSION)" 2>/dev/null; then
    echo "-- 扩展导入验证通过"
else
    echo "错误: 扩展安装后无法导入，错误详情:"
    PYTHONPATH="${SITE_PACKAGES}" python3 -c "import ${EXTENSION_NAME}"
    exit 1
fi

echo "安装完成，扩展已装入框架 site-packages，新开任意终端即可 import ${EXTENSION_NAME}"
echo "可运行验证: python3 ${SCRIPT_DIR}/test/test_template_extension.py"
```

### README.md
````markdown
# TemplateExtension 扩展

基于 [YomkServerPy](https://github.com/Solitude-5309/YomkServerPy) 框架的扩展模板，提供版本查询接口，可作为新扩展的开发起点。

## 功能

| URL | 功能 | 说明 |
|-----|------|------|
| `/ExtensionService/version` | 版本查询 | 返回扩展版本信息 |

## 前置条件

- Python >= 3.10
- YomkServerPy 已安装到系统

## 安装

```bash
bash install.sh            # 交互式，前置路径默认取 $YOMK_PREFIX_PATH，可交互修改
```

> 扩展与 YomkServerPy 安装到一起：扩展包装入框架安装路径下的 site-packages（已在 PYTHONPATH 内），安装后任意终端可 `import TemplateExtension`。
> 测试程序不随扩展安装，就地在模板目录下 `test/` 运行验证。
> 若未配置 `YOMK_PREFIX_PATH` 环境变量，脚本会提示警告，此时手动输入 YomkServerPy 安装路径即可。

## 工程结构

```
TemplateExtension/
├── TemplateExtension/
│   ├── __init__.py          # 包入口（re-export 扩展版本号与服务类）
│   ├── version.py           # 扩展版本号 EXTENSION_VERSION（单一来源）
│   └── extension_service.py # 服务实现
├── test/
│   └── test_template_extension.py  # 测试程序（就地运行，不随扩展安装）
├── install.sh               # 扩展安装脚本（交互式）
└── README.md
```

## 使用示例

将以下完整程序拷贝为 main.py，安装扩展后可直接运行：

```python
import YomkApi
from TemplateExtension import ExtensionService

YomkApi.init()

# 注册 ExtensionService（扩展服务需先注册才能使用）
YomkApi.new_service(ExtensionService, "/ExtensionService")

# 版本查询请求
resp = YomkApi.request("/ExtensionService/version", None)
if resp.status == YomkApi.ResStatus.eOk:
    print(f"version: {resp.data}")  # 输出: TemplateExtension v0.0.1
```
````
