---
name: yomkserverpy-modular
description: 基于YomkServerPy框架的模块化Python工程编程。核心理念"一切皆服务，一切皆请求"。用于创建YomkService功能模块、使用YomkContext全局状态管理、YomkEventLoop事件循环、YomkFunctionPool公共函数池和YomkLogger日志系统。当用户需要编写YomkServerPy模块化代码、创建服务、管理全局状态、处理事件队列、注册公共函数、使用日志或搭建工程结构时使用。
---

# YomkServerPy 模块化编程框架

## 框架概述

基于 Python 的模块化服务框架，核心理念：**「一切皆服务，一切皆请求」**。

| 组件 | 职责 |
|------|------|
| **YomkServer** | 服务容器，管理生命周期 |
| **YomkService** | 功能模块单元，通过 URL `/服务名/功能名` 访问 |
| **YomkContext** | 全局 K-V 状态管理（Checker防非法迁移、Monitor监听变更） |
| **YomkEventLoop** | 线程隔离事件循环（同循环顺序执行、不同循环并行） |
| **YomkFunctionPool** | 动态函数池（运行时注册/热替换） |
| **YomkLogger** | 分级日志（控制台/文件四级别、代理、内省） |
| **YomkServerInfo** | 服务内省（服务清单/函数清单/单函数元信息/全量拓扑，调试运维） |

唯一入口：`import YomkApi`

## 任务一：生成工程模板

当用户要求创建新工程时，**必须**生成完整可运行的工程骨架。

### 目录结构

```
ProjectName/
├── main.py               // 入口：YomkApi.boot 启动
├── boot/
│   └── MyBoot.py         // 生命周期：before/start/after
├── config/
│   └── config.json       // 配置文件
├── msgs/
│   └── YomkMsgs.py       // 所有消息包定义
├── services/
│   └── ConfigService.py  // 内置配置服务
├── typedefine/
│   └── TypeDefine.py     // 公共常量定义
├── test/                 // 单元测试脚本
├── scripts/              // 项目辅助脚本
├── check_env.sh          // 环境检查脚本
└── README.md
```

### 关键约定

1. `MyBoot.before()`：输出 starting 提示，通过 `Path(__file__)` 推导配置路径，存入 Context（`CTX_CONFIG_PATH`）
2. `MyBoot.start()`：服务创建器映射表（lambda 惰性创建）+ `srv_names` 按需启动，add_service 失败返回 -1
3. `MyBoot.after()`：调用 `/ConfigService/load` 加载配置，随后请求 `/get`（name）与 `/version`（工程版本号）并输出
4. 消息包为普通 Python 类，集中定义在 `msgs/YomkMsgs.py`
5. 运行方式：`bash check_env.sh` 检查环境（PYTHONPATH 由框架安装环节配置）→ `python main.py`
6. **版本号传递**：工程版本在 `typedefine/TypeDefine.py` 定义 `APP_VERSION` 常量（单一来源：本文件），由 `ConfigService` 的 `/version` 接口返回（不放 config.json），`MyBoot.after()` 请求该接口并输出版本号

### 生成规则

- 完整文件内容参见 [examples.md](examples.md) 示例0
- 将 `ProjectName` 替换为用户指定名称
- 所有文件必须完整生成，确保 `python main.py` 可直接运行

## 任务二：扩展业务服务

在已有工程中添加新服务的标准步骤：

### 步骤

1. **定义消息包**（`msgs/YomkMsgs.py`）：
```python
class MyData:
    def __init__(self, field1: str, field2: int):
        self.field1 = field1
        self.field2 = field2
```

2. **创建服务文件**（`services/XxxService.py`）：
```python
import YomkApi

class XxxService(YomkApi.YomkService):
    def __init__(self, server):
        super().__init__(server)
        self.set_name("/XxxService")

    def init(self):
        self.install_func("/my_func", self.my_func)
        print(f"install func [ /my_func ] to {self.get_name()}")

    def my_func(self, pkg):
        # pkg 即调用方传入的消息对象
        # 业务逻辑...
        return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "success")
```

3. **注册到 Boot**（`boot/MyBoot.py` 的 `start()` 映射表中添加）：
```python
from services.XxxService import XxxService
# start() 中 cur_srvs 字典添加：
"/XxxService": XxxService(YomkApi.server()),
```

4. **启动列表添加**（`main.py` 的 `MyBoot` 参数中）：
```python
YomkApi.boot(MyBoot(["/ConfigService", "/XxxService"]))
```

### 跨服务调用

```python
# 同步
resp = YomkApi.request("/XxxService/my_func", MyData("hello", 1))
# 异步
YomkApi.async_request("/XxxService/my_func", MyData("hello", 1), lambda resp: print(resp.msg))
```

### 服务内省（调试）

框架内置 `/YomkServerInfo` 服务（随 `YomkApi.init()` 自动启动），提供调试内省：服务列表、函数列表、单函数元信息、全量拓扑。功能函数安装时用 `install_func` 三参形式声明期望消息类型、四参附函数说明（均仅作内省元数据，不参与运行时校验；说明经 `/function` 结构化包透出；两参写法兼容，内省无类型）：

```python
def init(self):
    self.install_func("/my_func", self.my_func, "MyData")  # 三参：内省可见类型 MyData
    self.install_func("/my_desc", self.my_desc, "MyData", "说明")  # 四参：类型 + 函数说明
    return 0

resp = YomkApi.server_info_services()                      # 服务列表（data 为服务名列表）
resp = YomkApi.server_info_functions("/XxxService")        # 函数结构化清单（data 为 FuncInfo 列表：func_name/msg_name/description，与单查同族）
resp = YomkApi.server_info_function("/XxxService/my_desc") # 单函数元信息（命中返回结构化 FuncInfo 包：msg_name=类型名、description=四参说明，msg 恒 "ok"）
resp = YomkApi.server_info_all()                           # 全量拓扑（ServerServiceInfo 层级列表：service_name + funcs）
```

服务器层内省之外，四个内置模块均提供模块内层内省三件套（Context 的 `context_info_keys/key/all`、EventLoop 的 `event_loop_info_loops/loop/all`、FunctionPool 的 `function_pool_info_names/name/all`、Logger 的 `logger_info_loggers/logger/all`），见 [reference.md](reference.md) 对应小节与 [examples.md](examples.md) 示例5。

## 任务三：创建扩展库

当用户要求创建独立扩展时，**必须**生成完整可运行的扩展骨架。扩展为普通 Python 包（目录即包），通过 install.sh 安装到 YomkServerPy 的 site-packages，安装后任意终端可 `import` 引用。

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

### 关键约定

1. **包名与扩展名同名**：包目录与 `import` 名直接用扩展名（如 `TemplateExtension`）；占位符替换规则：`ExtensionName` → 用户指定名（包目录、import 语句、版本字符串前缀同步替换），服务类 `XxxService`，不做大小写转换
2. **版本常量独立 `version.py`（顺序无关，防循环导入）**：`EXTENSION_VERSION` 常量为版本单一来源，放包内独立 `version.py`；服务模块以 `from .version import EXTENSION_VERSION` 导入。**禁止**在 `__init__.py` 内定义常量再被子模块 `from . import EXTENSION_VERSION` 取用：该写法依赖 `__init__` 的语句顺序，IDE/linter 的 import 自动重排会把子模块导入挪到常量定义之前，安装后即触发 circular import（实测踩坑）；独立模块后 `__init__.py` 仅剩 re-export 语句，任何 import 顺序都正确
3. **模板接口最小化**：模板服务默认只包含一个 `/version` 接口，版本以字符串拼接返回（`f"ExtensionName v{EXTENSION_VERSION}"`），不生成任何示例业务接口（如加减乘除）；业务功能通过「继续扩展」添加
4. **数据源无关原则**：扩展只负责处理逻辑，不关心数据来源。所有外部数据（如文件内容、路径等）必须通过请求参数传入，扩展内部不硬编码任何数据源
5. **安装走 install.sh**：交互询问 YomkServerPy 安装路径（默认取 `$YOMK_PREFIX_PATH`），探测 site-packages 并校验框架模块后，将扩展包幂等复制到 site-packages（与框架安装到一起）；安装目录不可写时用 sudo；装后以仅含 site-packages 的 PYTHONPATH 验证 `import`（模拟新终端环境）
6. **测试程序就地运行**：测试脚本不含路径自举，直接 `python3` 运行，依赖扩展已安装，不随扩展安装；通过 `YomkApi.new_service` 注册服务并验证功能，输出 `[PASS]`/`[FAIL]` 与 Test Summary，存在失败时退出码为 1
7. **README 使用示例完整可复制**：使用示例必须提供完整的 main.py（含 import、`YomkApi.init()`、`new_service` 注册扩展服务、请求与输出），用户复制后即可运行；不得只提供请求代码片段
8. **不适用项（语言差异）**：无 CMake 包导出（原生 import 即引用）、无 ldconfig 注册（site-packages 已在 PYTHONPATH 内）、无 include/src 头文件分层（`__init__.py` re-export 即对外接口）

### 生成规则

- 优先复制模板：本 skill 所在 `skill/yomkserverpy-modular/` 目录的上层即 YomkServerPy 仓库根；若其下存在 `template/extension/` 目录，优先复制并替换扩展名/类名占位符；目录不存在时按 [examples.md](examples.md) 示例7 逐文件生成
- 将 `ExtensionName` 替换为用户指定名称
- 所有文件必须完整生成，确保 `bash install.sh` 可直接安装运行

### 继续扩展

在已有扩展中添加新功能：

1. **服务类添加功能函数**（`ExtensionName/xxx_service.py`）：`install_func` + 实现
2. **测试程序添加测试用例**（`test/test_xxx.py`）
3. **README 功能表添加行**

## 编程规范

### 消息包

- 消息包为普通 Python 类，定义在 `msgs/YomkMsgs.py`
- 作为 `pkg` 参数传入服务函数，直接通过属性访问字段
- 返回值通过 `YomkResponse.data` 携带任意数据

### 设计原则

1. 每个 Service 只负责单一业务域
2. 服务间通过 `YomkApi.request` 通信，不直接引用
3. 共享状态用 Context，耗时操作用 EventLoop，公共函数用 FunctionPool
4. 消息定义集中 `msgs/`，服务实现放 `services/`
5. 配置路径通过 Context 传递，不用构造参数

### 日志

- 业务体内日志用门面：`YomkApi.console_log_info_tag(tag, *args)`（同族 warn/error/debug），内容头自动注入 `[行号]` 定位调用处
- 无独立 tag 需求时用无 tag 便捷函数 `console_log_info(*args)`（缺省 tag "MainLogger"）；文件日志先 `file_log_create(log_dir, log_file)` 再 `file_log_info_tag(log_file, tag, *args)`（同族便捷版）
- 叙事横幅/演示输出走 `print`，不占日志通道

## 详细参考

- 完整工程代码：[examples.md](examples.md)
- API 详细参考：[reference.md](reference.md)
