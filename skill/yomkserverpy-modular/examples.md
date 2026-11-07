# YomkServerPy 工程示例

创建新工程**优先复制仓库根 `template/project/`**（扩展库复制 `template/extension/`）并替换占位符——与模板零漂移且省时；以下示例0/示例7 即模板的等价逐文件生成形态，模板目录不存在时按此逐文件生成。所有 API 均为六域链式形态（`YomkApi.server()/ctx()/fn_pool()/event_loop()/log()/log_mgr()`，累积前置、无参终结）。

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
├── check_env.sh
└── README.md
```

### main.py
```python
import YomkApi
from boot.MyBoot import MyBoot
from YomkApi import log, server

server().boot_ptr(MyBoot(["/ConfigService"])).run()
log().tag("main").info("running, press Enter to exit.")
input()
```

### boot/MyBoot.py
```python
import YomkApi
from pathlib import Path
from typedefine.TypeDefine import CTX_CONFIG_PATH
from services.ConfigService import ConfigService
from msgs.YomkMsgs import *
from YomkApi import log, ctx, server

class MyBoot(YomkApi.YomkBoot):
    def __init__(self, srv_names):
        super().__init__()
        self.srv_names = srv_names

    def before(self):
        log().tag("MyBoot::before").info("starting...")

        # 通过 __file__ 推导配置文件路径
        project_dir = Path(__file__).resolve().parent.parent
        config_path = str(project_dir / "config" / "config.json")
        ctx().key(CTX_CONFIG_PATH).value(config_path).create()
        log().tag("MyBoot::before").info("config path: ", config_path)
        return 0

    def start(self):
        # 服务创建器映射表
        cur_srvs = {
            "/ConfigService": lambda: ConfigService(server().instance()),
        }

        # 根据 srv_names 按需启动服务
        for srv_name in self.srv_names:
            if srv_name in cur_srvs:
                if server().srv_ptr(cur_srvs[srv_name]()).srv_name(srv_name).add() != 0:
                    return -1

        return 0

    def after(self):
        resp = server().url("/ConfigService/load").request()
        if resp.status != YomkApi.ResStatus.eOk:
            log().tag("MyBoot::after").error("load config failed: ", resp.msg)
            return -1
        log().tag("MyBoot::after").info("started successfully.")

        resp = server().url("/ConfigService/get").msg(ConfigKey("name")).request()
        if resp.status != YomkApi.ResStatus.eOk:
            log().tag("MyBoot::after").error("get config name failed: ", resp.msg)
            return -1
        log().tag("MyBoot::after").info("config name: ", resp.data)

        # 测试版本号接口：获取并输出工程版本号（typedefine/TypeDefine.py 定义的 APP_VERSION 常量）
        resp = server().url("/ConfigService/version").request()
        if resp.status != YomkApi.ResStatus.eOk:
            log().tag("MyBoot::after").error("get version failed: ", resp.msg)
            return -1
        log().tag("MyBoot::after").info("project version: ", resp.data)

        resp = server().url("/ConfigService/get").msg(ConfigKey("description")).request()
        if resp.status != YomkApi.ResStatus.eOk:
            log().tag("MyBoot::after").error("get config description failed: ", resp.msg)
            return -1
        log().tag("MyBoot::after").info("config description: ", resp.data)

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
from YomkApi import log, ctx


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
        log().tag("ConfigService::init").info("install func [ /load /get /set /reload /version ] to ", self.get_name())

    def load_config(self, pkg):
        self.config_path = ctx().key(CTX_CONFIG_PATH).msg().with_default("").get()
        if not self.config_path:
            return YomkApi.YomkResponse(YomkApi.ResStatus.eNo, "config_path not found in context")
        try:
            with open(self.config_path, 'r', encoding='utf-8') as f:
                self.config_json = json.load(f)
        except Exception as e:
            return YomkApi.YomkResponse(YomkApi.ResStatus.eNo, f"failed to open: {self.config_path}, {e}")
        log().tag("ConfigService::load_config").info("loaded: ", self.config_path)
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
        log().tag("ConfigService::set_config").info("set ", pkg.key, " = ", pkg.value)
        return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "ok")

    def reload_config(self, pkg):
        # reload：先回取 context 中的 config_path，缺失即 eNo
        self.config_path = ctx().key(CTX_CONFIG_PATH).msg().with_default("").get()
        if not self.config_path:
            return YomkApi.YomkResponse(YomkApi.ResStatus.eNo, "config_path not found in context")
        try:
            with open(self.config_path, 'r', encoding='utf-8') as f:
                self.config_json = json.load(f)
        except Exception as e:
            return YomkApi.YomkResponse(YomkApi.ResStatus.eNo, f"failed to reload: {self.config_path}, {e}")
        log().tag("ConfigService::reload_config").info("reloaded: ", self.config_path)
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

### check_env.sh
```bash
#!/bin/bash
# 环境检查脚本
# 用法: bash check_env.sh
# 检查 YomkServerPy 运行环境（PYTHONPATH 由框架安装环节配置，本脚本只做检查）

echo "== YomkServerPy 环境检查 =="

if ! command -v python3 >/dev/null 2>&1; then
    echo "错误: 未找到 python3"
    exit 1
fi
echo "-- python3: $(python3 --version 2>&1)"

if python3 -c "import YomkApi" 2>/dev/null; then
    echo "-- YomkApi 导入成功: $(python3 -c 'import YomkApi; print(YomkApi.__file__)')"
    echo "-- 框架版本: $(python3 -c 'import YomkApi; print(YomkApi.__version__)')"
    echo "环境检查通过"
else
    echo "错误: 无法导入 YomkApi（PYTHONPATH 未指向 YomkServerPy 安装路径下的 site-packages）"
    echo "提示: 确认框架已安装；若 YOMK_PREFIX_PATH 已配置，检查 PYTHONPATH 是否包含:"
    echo "  ${YOMK_PREFIX_PATH:-/opt/yomk}/lib/pythonX.Y/site-packages"
    exit 1
fi
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

`MyBoot` 管理程序三阶段生命周期（经 `server().boot_ptr(MyBoot(...)).run()` 消费，内部先完成框架 init）：

| 阶段 | 方法 | 用途 |
|------|------|------|
| 启动前 | `before()` | 创建 Context、EventLoop、注册 FunctionPool、设置日志代理等 |
| 启动中 | `start()` | 注册并启动服务 |
| 启动后 | `after()` | 调用服务接口做初始化、自启动任务等 |

## 核心组件速查

| 组件 | 用途 | 链式域入口 |
|------|------|--------------|
| YomkServer / YomkService | 服务注册与请求通信 | `server().srv(cls).create()` / `server().url(u).msg(pkg).request()` |
| YomkContext | 全局 K-V 状态管理 | `ctx().key(k).value(v).create()` / `.set()` / `.msg(cls).with_default(d).get()` |
| YomkEventLoop | 线程隔离事件循环 | `event_loop().loop(n).handle_fn(f).start()` / `.post()` / `.post_wait()` |
| YomkFunctionPool | 动态公共函数池 | `fn_pool().fn_name(n).fn(f).install()` / `.call()` |
| YomkLogger | 六级别日志系统 | `log().tag(t).info(*args)` / 管理走 `log_mgr()` |
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
from YomkApi import log
from msgs.YomkMsgs import *

class UserService(YomkApi.YomkService):
    def __init__(self, server):
        super().__init__(server)
        self.set_name("/UserService")

    def init(self):
        self.install_func("/get_user", self.get_user)
        self.install_func("/create_user", self.create_user)
        log().tag("UserService::init").info("install func [ /get_user /create_user ] to ", self.get_name())
        return 0

    def get_user(self, pkg):
        log().tag("UserService").info("query user: ", pkg.user_id)   # 服务体内走日志通道，不用 print
        # 业务逻辑...
        info = UserInfo(pkg.user_id, "Alice", 25)
        return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "ok", info)

    def create_user(self, pkg):
        log().tag("UserService").info("create user: ", pkg.name)
        # 跨服务调用示例：读取配置
        cfg_resp = YomkApi.server().url("/ConfigService/get").msg(ConfigKey("name")).request()
        return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "user created")
```

> 服务体内的运行信息一律走日志链（`log().tag(...).info(...)`），`print` 只用于测试驱动程序与叙事横幅。

### 3. 注册到 Boot（boot/MyBoot.py 修改）
```python
# 头部添加
from services.UserService import UserService

# start() 映射表中添加（lambda 惰性创建 server().instance()，与示例0一致）
cur_srvs = {
    "/ConfigService": lambda: ConfigService(server().instance()),
    "/UserService": lambda: UserService(server().instance()),
}
```

### 4. 启动列表（main.py 修改）
```python
server().boot_ptr(MyBoot(["/ConfigService", "/UserService"])).run()
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

# 监控函数：记录变更（收到的是本次 set 的键值快照，不保证实时——读最新值请再 get()）
def change_logger(ctx):
    YomkApi.log().tag("monitor").info("key=", ctx.key, " new_value=", ctx.value)

YomkApi.server().init()

# 键级回调只能注册到已存在的键：先 create 再注册（键不存在 Failed "key is not exist"）
YomkApi.ctx().key("config").value("default").create()
YomkApi.ctx().key("config").checker_fn(non_empty_checker).apply()          # 同步 checker
YomkApi.ctx().key("config").monitor_fn(change_logger).apply()              # 同步 monitor（写锁外即时执行）
YomkApi.ctx().key("config").monitor_fn(change_logger).async_(True).apply() # 异步 monitor：该组单线程池按提交序保序送达

# 组开关：建组默认关，注册回调后必须显式 enable 才生效（disable 只关断、回调保留不删除）
YomkApi.ctx().group("main").checker().enable()
YomkApi.ctx().group("main").monitor().enable()

YomkApi.ctx().key("config").value("new_value").set()   # checker 放行 + monitor 触发
YomkApi.ctx().key("config").value("").set()            # 被拒：Failed（契约 "checker reject set context"）

# 自定义组：非 main 组首次寻址即惰性自动创建（组不支持销毁、组表只增不减；异步监控池每组自持单线程）
YomkApi.ctx().group("motion").key("state").value("idle").create()
YomkApi.ctx().group("motion").key("state").monitor_fn(change_logger).async_(True).apply()
YomkApi.ctx().group("motion").monitor().enable()                          # 自定义组同样默认关，须显式开
YomkApi.ctx().group("motion").key("state").value("running").set()

# 类型化读（msg(cls) 仅类型指定位，不做运行期校验；未命中回退默认值）
val = YomkApi.ctx().key("config").msg(str).with_default("fallback").get()

# 写路径失败契约（裸链可查 msg，status 均 Failed）：
# "key is empty" / "value is empty" / "key already exists"（create）
# "key is not exist" / "context type not match" / "checker reject set context"（set）
# "group name is empty"（空组名）
```

服务成员函数作 checker/monitor 时必须弱绑定（服务删除后 checker 自动放行、monitor 自动丢弃，不影响他人）：

```python
class GuardService(YomkApi.YomkService):
    def init(self):
        YomkApi.ctx().key("guard").value("on").create()
        YomkApi.ctx().key("guard").checker_fn(
            self.weak_func(self.check, discard="check")).apply()          # 失效形态：放行 CheckStatus.eAccept
        YomkApi.ctx().key("guard").monitor_fn(
            self.weak_func(self.on_change, discard="none")).apply()       # 失效形态：返回 None（void）
        YomkApi.ctx().group("main").checker().enable()
        return 0

    def check(self, ctx):
        return YomkApi.CheckStatus.eAccept

    def on_change(self, ctx):
        YomkApi.log().info("guard changed:", ctx.value)
```

---

## 示例3：EventLoop 异步事件处理

```python
import YomkApi
import threading

def task_handler(pkg) -> YomkApi.YomkResponse:
    YomkApi.log().tag("worker").info("processing: ", pkg, " in thread ", threading.get_ident())
    return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "task done")

YomkApi.server().init()

# 启动事件循环（msg_name 为默认处理函数期望消息类型，仅内省元数据；同名再次 start = 续跑不重建，
# 本次累积的 handle_fn/msg_name 不生效）
YomkApi.event_loop().loop("worker_loop").handle_fn(task_handler).msg_name("str").start()

# 非阻塞投递（走默认处理函数；tag 仅内省可见，用于观察队列积压）
YomkApi.event_loop().msg("task_1").loop("worker_loop").tag("billing").post()

# 单次事件临时 handler（优先于默认处理函数）+ 同步投递（阻塞至执行完成）
(YomkApi.event_loop().msg("task_2").loop("worker_loop")
    .handle_fn(lambda pkg: YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "ad-hoc done")).post_wait())

# 取回执行结果：链式 post_wait 终结仅收敛三态，需要 Event 回包时用裸链
resp = (YomkApi.server().url("/YomkEventLoop/post_wait")
        .msg(YomkApi.Event("worker_loop", "task_3", task_handler, "tag3")).request())
if resp.status == YomkApi.ResStatus.eOk:
    ev = resp.data                       # Event 对象：m_eventId / m_response 携带处理响应
    YomkApi.log().info("event result: ", ev.m_response.msg)

# 内省：单循环状态 + 队列头 n 条 tag 采样（EventLoopInfo：loop_name/running/pending/has_default_func/msg_name/next_tags）
YomkApi.event_loop().loop("worker_loop").tag_count(5).get_info()
YomkApi.event_loop().list()              # 全部循环名

# 停止：停线程但保留队列事件 → 再次 start() 续跑；期间投递被拒（契约 Failed）：
# "event loop not exist"（循环不存在）；"event loop not running, post rejected" / "post wait rejected"（已停止）
YomkApi.event_loop().loop("worker_loop").stop()
YomkApi.event_loop().msg("task_4").loop("worker_loop").post()   # Failed "event loop not running, post rejected"
YomkApi.event_loop().loop("worker_loop").start()                # 续跑，task_4 补跑

# 销毁：停线程 + 清空队列 + 移出循环表，不可续跑
YomkApi.event_loop().loop("worker_loop").destroy()
```

注：投递终结返回的三态**仅表示入队/完成标记，非执行结果**；事件未带 handler 时回落默认处理函数，两者皆空静默出队；工作线程内调用 `post_wait` 触发死锁自检（本线程直接执行该事件，诊断 `"EventLoop deadlock: post wait in worker thread..."`）；服务成员函数作处理函数须 `self.weak_func(...)`。

---

## 示例4：FunctionPool 公共函数池

```python
import YomkApi

def validate_amount(pkg) -> YomkApi.YomkResponse:
    try:
        amount = float(pkg)
        if amount <= 0:
            return YomkApi.YomkResponse(YomkApi.ResStatus.eNo, "invalid amount")
        return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "valid")
    except Exception:
        return YomkApi.YomkResponse(YomkApi.ResStatus.eNo, "invalid amount")

def calc_sum(pkg) -> YomkApi.YomkResponse:
    if not isinstance(pkg, list):
        return YomkApi.YomkResponse(YomkApi.ResStatus.eNo, "pkg is not list")
    return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "ok", sum(pkg))

def calc_sum_v2(pkg) -> YomkApi.YomkResponse:      # 热替换后的新版本实现
    return calc_sum(pkg)

YomkApi.server().init()

# 注册（msg_name/description 仅内省元数据）+ 调用（call 阻塞至函数执行完成，返回值整体进 data）
YomkApi.fn_pool().fn_name("validate_amount").fn(validate_amount).install()
YomkApi.fn_pool().fn_name("calc_sum").fn(calc_sum).msg_name("list").description("对列表求和").install()
resp = YomkApi.fn_pool().fn_name("validate_amount").msg("100.5").call()

# 同名重复 install = 运行时热替换（返回 Succeeded，无需先注销；诊断 "find function name is already exist, update to current function"）
YomkApi.fn_pool().fn_name("calc_sum").fn(calc_sum_v2).install()

# 单函数内省：命中 msg 恒 "ok"，data 为 FuncInfo 包（func_name/msg_name/description）
resp = YomkApi.fn_pool().fn_name("calc_sum").get_info()   # msg_name="list", description="对列表求和"

# 注销与未命中口径（not-found 归 Failed 不归 Invalid）
YomkApi.fn_pool().fn_name("calc_sum").uninstall()          # Succeeded
YomkApi.fn_pool().fn_name("calc_sum").uninstall()          # Failed（已注销）
YomkApi.fn_pool().fn_name("calc_sum").msg([1, 2]).call()   # Failed（名字不存在）

# 入参非法归 Invalid：空名/空函数（契约 "funcName or func is empty"）
YomkApi.fn_pool().fn_name("").fn(validate_amount).install()     # Invalid
YomkApi.fn_pool().fn_name("bad").fn(None).install()             # Invalid
```

服务成员函数注册进函数池必须弱绑定，并在 `deinit` 主动注销外部登记：

```python
class PoolUserService(YomkApi.YomkService):
    def init(self):
        YomkApi.fn_pool().fn_name("pool_task").fn(self.weak_func(self.pool_task)).install()
        return 0

    def pool_task(self, pkg):
        return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "done")

    def deinit(self):
        # 服务删除后弱绑定自动失效（call 得 Failed + "service has been deleted or unregistered, callback ignored."）；
        # 如需即刻清理表项，可主动注销
        YomkApi.fn_pool().fn_name("pool_task").uninstall()
```

---

## 示例5：服务内省（调试）

期望消息类型由 `install_func` 三参形式声明、函数说明由四参声明（均仅作内省元数据，不参与运行时校验；说明经 `fn_url(...).get_info()` 结构化包透出；两参写法兼容，内省无类型）：

```python
class DemoService(YomkApi.YomkService):
    def init(self):
        self.install_func("/with_type", self.with_type, "MyData")  # 三参：内省可见类型 MyData
        self.install_func("/with_desc", self.with_desc, "MyData", "函数说明")  # 四参：类型 + 说明
        self.install_func("/no_type", self.no_type)                # 兼容写法，内省无类型
        return 0

YomkApi.server().init()

# 服务列表（data 为字符串列表，字典序）
resp = YomkApi.server().services()
# resp.data: ["/DemoService", "/YomkContext", "/YomkEventLoop", ...]

# 指定服务的函数结构化清单（data 为 FuncInfo 列表，与单查同族；服务不存在返回空清单）
resp = YomkApi.server().srv_name("/DemoService").functions()
# resp.data: [FuncInfo("/no_type"), FuncInfo("/with_type", "MyData"),
#             FuncInfo("/with_desc", "MyData", "函数说明")]

# 单函数元信息（命中 msg 恒 "ok"，data 为 FuncInfo 包：func_name/msg_name/description）
YomkApi.server().fn_url("/DemoService/with_desc").get_info()  # ok, msg_name="MyData", description="函数说明"
YomkApi.server().fn_url("/DemoService/with_type").get_info()  # ok, msg_name="MyData"
YomkApi.server().fn_url("/DemoService/no_type").get_info()    # ok, msg_name 空串（未声明）
YomkApi.server().fn_url("/DemoService/not_exist").get_info()  # Failed

# 全量拓扑：ServerServiceInfo 层级列表（service_name 服务名 + funcs 函数元信息列表，与单查同族）
resp = YomkApi.server().all()
```

说明：自省读的是运行期服务表，注册/删除即时反映到下一次查询；函数池动态注册函数的类型元数据由 `fn_pool().msg_name(m)` 声明，未声明内省显示为空。四个内置模块另提供模块内层内省链（Context 的 `ctx().keys()/key(k).info()/groups()/all()`、EventLoop 的 `event_loop().list()/loop(n).tag_count(n).get_info()/all()`、FunctionPool 的 `fn_pool().list()/fn_name(n).get_info()/all()`、Logger 的 `log_mgr().console()/file().list()/get_info()/status()/all()`），API 见 [reference.md](reference.md)。完整可运行示例见仓库 `examples/ExampleYomkServerInfo.py`。

## 示例6：日志

六级别 `debug/info/warn/error/trace/fatal`（枚举 `Log.eTrace=0 … eFatal=5`）；**tag 即控制台日志器名，首次使用惰性创建，缺省名 `main`**。

```python
import YomkApi
from YomkServerPy import Log

YomkApi.server().init()

# 控制台发射（六级别 × 变参；无 tag 归属缺省日志器 "main"；tag 版兼作日志器名，惰性创建）
YomkApi.log().info("application started")
YomkApi.log().tag("Security").warn("suspicious activity")
YomkApi.log().tag("Security").trace("enter scan")     # trace/fatal 六级齐全
YomkApi.log().error("failed to connect:", False)      # bool 输出 false

# 输出行格式：[YYYY-MM-DD HH:MM:SS.mmm] [级别(定宽5)] [tag] [位置] 内容（位置默认仅行号）
# [2026-11-07 10:00:00.000] [INFO ] [main] [42] application started

# 级别门控两级主闸并联（AND）：无具名 = 全局主闸，具名 = 逐器门控（未命中 Failed 不惰性创建、不触碰主闸）；
# 控制台与文件两套主闸/两套开关完全独立
YomkApi.log_mgr().console().level(Log.eDebug).disable()             # 控制台主闸关 DEBUG
YomkApi.log_mgr().console("biz").level(Log.eInfo).disable()         # 逐器：仅 biz 的 INFO 无输出
YomkApi.log_mgr().console("biz").level(Log.eInfo).enable()          # 恢复（info_on 回归 true）

# 内省：单查/清单/全局主闸（get_info/all 条目报逐器门控层真值；条目 on 但无输出 → 查 status() 主闸层归因）
resp = YomkApi.log_mgr().console("main").get_info()    # LoggerInfo：logger_name/type/dir/logger_state
resp = YomkApi.log_mgr().console().list()              # 控制台日志器名清单（字典序）
resp = YomkApi.log_mgr().console().status()            # 全局主闸 LoggerState：trace_on…fatal_on 六开关 + proxy_on
resp = YomkApi.log_mgr().file().status()               # 文件侧主闸（proxy_on 报文件代理态；与控制台独立）
resp = YomkApi.log_mgr().all()                         # 全部日志器及逐器门控状态

# 文件日志器生命周期：dir 须先于 file 累积；同名重复创建 Failed；remove 两表并清、不删磁盘 .log
YomkApi.log_mgr().dir("/var/log/myapp").file("app").create()
YomkApi.log().to_sink("app").info("application started")     # 写具名文件日志器；未知名 Failed 不惰性创建
YomkApi.log_mgr().file("app").flush()                         # 显式落盘 d/name.log
YomkApi.log_mgr().file("app").remove()                        # 删除（console+file 两表一并清除，缓冲自动落盘）
YomkApi.log_mgr().file("app").remove()                        # Failed（两表皆未命中）
# 注：缺省 "main" 无特殊保护，删除后下次写控制台日志会惰性重建

# 控制台代理：每条先过代理，收 Log 包（m_level/m_log/m_logger/m_tag），False 消费不再输出、True 穿透；None 卸载
def my_log_proxy(log: Log) -> bool:
    print(f"[{log.m_tag}] {log.m_log}")
    return False

YomkApi.log_mgr().console().proxy(my_log_proxy).apply()
YomkApi.log_mgr().console().proxy(None).apply()      # 卸载；status()/all() 的 proxy_on 随之回到 False

# 文件代理与控制台代理相互独立：False 该条不落盘、True 穿透默认落盘
YomkApi.log_mgr().file().proxy(lambda log: log.m_level != Log.eDebug).apply()   # 拦 DEBUG
YomkApi.log_mgr().file().proxy(None).apply()

# 位置前缀三开关（进程级，控制台与文件同时生效；默认关/开/关 = 仅行号；apply() 恒 Succeeded）
YomkApi.log_mgr().location().show_file(True).show_func(True).apply()   # [main.py:42:<module>] 形态
```

完整可运行示例见仓库 `examples/ExampleYomkLogger.py`（六级别、两级开关、代理消费/放行、文件日志回读与删除后惰性重建）。

---

## 示例7：创建扩展库（独立扩展包）

扩展为普通 Python 包（包名与扩展名同名，如 `TemplateExtension`），仅含 `/version` 接口；`install.sh` 将扩展包装入 YomkServerPy 的 site-packages，安装后任意终端可 `import TemplateExtension`。占位符替换：`TemplateExtension`/`ExtensionName` → 用户指定名（包目录、import、版本字符串前缀、测试文件名同步替换），服务类 `ExtensionService`。

### 目录结构（模板实名）

```
TemplateExtension/
├── TemplateExtension/
│   ├── __init__.py          // 包入口（re-export 扩展版本号与服务类）
│   ├── version.py           // 扩展版本号 EXTENSION_VERSION（单一来源）
│   └── extension_service.py // 服务实现
├── test/
│   └── test_template_extension.py  // 测试程序（就地运行，不随扩展安装）
├── install.sh               // 扩展安装脚本（交互式）
└── README.md
```

### TemplateExtension/__init__.py
```python
"""TemplateExtension 扩展包"""

from .version import EXTENSION_VERSION
from .extension_service import ExtensionService
```

### TemplateExtension/version.py
```python
"""TemplateExtension 版本定义"""

# 扩展版本号（单一来源：本文件）
EXTENSION_VERSION = "0.0.1"
```

### TemplateExtension/extension_service.py
```python
import YomkApi
from .version import EXTENSION_VERSION
from YomkApi import log


class ExtensionService(YomkApi.YomkService):
    def __init__(self, server):
        super().__init__(server)
        self.set_name("/ExtensionService")

    def init(self):
        self.install_func("/version", self.version)
        log().tag("ExtensionService::init").info("install func [ /version ] to ", self.get_name())
        return 0

    def version(self, pkg):
        # 版本号：EXTENSION_VERSION 常量（定义于 version.py）
        return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "ok", f"TemplateExtension v{EXTENSION_VERSION}")
```

### test/test_template_extension.py
```python
#!/usr/bin/env python3
"""TestTemplateExtension —— 扩展功能测试程序（就地运行，不随扩展安装）"""

import sys

from TemplateExtension import ExtensionService
import YomkApi
from YomkApi import server

g_pass = 0
g_fail = 0

def main():
    global g_pass, g_fail

    server().init()

    # 注册 ExtensionService（扩展服务需先注册才能使用）
    server().srv(ExtensionService, "/ExtensionService").create()

    # 测试版本查询
    resp = server().url("/ExtensionService/version").request()
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

YomkApi.server().init()

# 注册 ExtensionService（扩展服务需先注册才能使用）
YomkApi.server().srv(ExtensionService, "/ExtensionService").create()

# 版本查询请求
resp = YomkApi.server().url("/ExtensionService/version").request()
if resp.status == YomkApi.ResStatus.eOk:
    print(f"version: {resp.data}")  # 输出: TemplateExtension v0.0.1
```
````
