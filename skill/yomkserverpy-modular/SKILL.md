---
name: yomkserverpy-modular
description: 基于YomkServerPy框架的模块化Python工程编程。核心理念"一切皆服务，一切皆请求"。API 为七个链式域入口（server()/ctx()/fn_pool()/event_loop()/log()/log_mgr()/srv_info()），前置累积、无参终结，屏蔽 URL 路由。用于创建YomkService功能模块、使用YomkContext全局状态管理、YomkEventLoop事件循环、YomkFunctionPool公共函数池、YomkLogger日志系统与YomkBoot启动编排。当用户需要编写YomkServerPy模块化代码、创建服务、管理全局状态、处理事件队列、注册公共函数、使用日志或搭建工程结构时使用；当用户提到"创建工程"或搭建YomkServerPy骨架时，必须立即使用任务一生成完整工程骨架，不要先询问需求。
---

# YomkServerPy 模块化编程框架

## 框架概述

基于 Python 的模块化服务框架，核心理念：**「一切皆服务，一切皆请求」**。

| 组件 | 职责 | 链式域入口 |
|------|------|------|
| **YomkServer** | 服务容器，管理生命周期 | `server()` |
| **YomkService** | 功能模块单元，通过 URL `/服务名/功能名` 访问 | `server().srv(cls, name)` |
| **YomkBoot** | 三段式启动编排（before/start/after） | `server().boot_ptr(boot).run()` |
| **YomkContext** | 全局 K-V 状态管理（Checker防非法迁移、Monitor监听变更、分组隔离） | `ctx()` |
| **YomkEventLoop** | 线程隔离事件循环（同循环顺序执行、不同循环并行） | `event_loop()` |
| **YomkFunctionPool** | 动态函数池（运行时注册/热替换） | `fn_pool()` |
| **YomkLogger** | 六级日志（控制台/文件、两级门控、代理、内省） | 发射 `log()` / 管理 `log_mgr()` |
| **YomkServerInfo** | 服务内省（服务清单/函数清单/单函数元信息/全量拓扑） | 独立内省域入口 `srv_info()`（`services/functions/fn_url/all`） |

唯一形态：按域显式导入（`from yomk.server import server` 等，无伞门面）。全部能力经七个链式域入口暴露（内省读族在 `srv_info()` 域）：**累积环节**（`url/msg/srv/key/group/value/fn_name/fn/loop/handle_fn/msg/tag` 等）定位目标并返回 builder 自身，**终结环节**（`request/init/create/add/remove/set/install/uninstall/call/post/post_wait/start/stop/destroy/get/apply/run` 与内省读族）无参执行并返回结果。链须单表达式内联使用；`log()` 域入口在调用行捕获位置前缀，**禁止缓存 builder 对象**。三态返回统一口径 `Succeeded/Failed/Invalid`（`ResStatus.eOk/eNo/eInvalid` 为同值别名）。

### 触发条件与强制行为

当用户提到"创建工程"、搭建 YomkServerPy 工程骨架或新增扩展库时，**必须立即使用任务一（或任务三）生成完整可运行的工程骨架，不要先询问需求、不要列选项、不要讨论方案**；骨架落地后再按任务二补业务服务。

## 任务一：生成工程模板

当用户要求创建新工程时，**必须**生成完整可运行的工程骨架。

### 目录结构

```
ProjectName/
├── main.py               // 入口：server().boot_ptr(MyBoot(...)).run() 启动
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

1. `MyBoot.before()`：输出 starting 提示，通过 `Path(__file__)` 推导配置路径，存入 Context（`ctx().key(CTX_CONFIG_PATH).value(path).create()`）
2. `MyBoot.start()`：服务创建器映射表（lambda 惰性创建 `XxxService(server().instance())`）+ `srv_names` 按需启动，`server().srv(cls, name).create()` 失败返回 -1
3. `MyBoot.after()`：调用 `/ConfigService/load` 加载配置，随后请求 `/get`（name、description）与 `/version`（工程版本号）并输出
4. 消息包为普通 Python 类，集中定义在 `msgs/YomkMsgs.py`
5. 运行方式：`bash check_env.sh` 检查环境（PYTHONPATH 由框架安装环节配置）→ `python main.py`
6. **版本号传递**：工程版本在 `typedefine/TypeDefine.py` 定义 `APP_VERSION` 常量（单一来源：本文件），由 `ConfigService` 的 `/version` 接口返回（不放 config.json），`MyBoot.after()` 请求该接口并输出版本号

### 生成规则

- **优先复制模板**：本 skill 所在 `skill/` 目录的上层即 YomkServerPy 仓库根；若其下存在 `template/project/` 目录，**优先整体复制并替换 `ProjectName` 占位符**——与模板零漂移且省时；目录不存在时按 [examples.md](examples.md) 示例0 逐文件生成
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
from yomk.msg.server import ResStatus, Response
from yomk.server import YomkService, server
from yomk.ctx import ctx
from yomk.event_loop import event_loop
from yomk.fn_pool import fn_pool
from yomk.log import log, log_mgr

class XxxService(YomkService):
    def __init__(self, server):
        super().__init__(server)
        self.set_name("/XxxService")

    def init(self):
        self.install_func("/my_func", self.my_func)  # 注册即自动弱绑定
        log().tag("XxxService").info("install func [ /my_func ]")
        return 0

    def my_func(self, pkg):
        # pkg 即调用方传入的消息对象
        # 业务逻辑...
        return Response(ResStatus.eOk, "success")
```

3. **注册到 Boot**（`boot/MyBoot.py` 的 `start()` 映射表中添加）：
```python
from services.XxxService import XxxService
# start() 中 cur_srvs 字典添加（lambda 惰性创建，与示例0约定一致）：
"/XxxService": lambda: XxxService(server().instance()),
```

4. **启动列表添加**（`main.py` 中 MyBoot 参数）：
```python
server().boot_ptr(MyBoot(["/ConfigService", "/XxxService"])).run()
```

### 跨服务调用

```python
# 同步（阻塞至服务应答）
resp = server().url("/XxxService/my_func").msg(MyData("hello", 1)).request()
# 异步（投递内置线程池立即返回，结果经回调送达）
(server().url("/XxxService/my_func")
    .async_(lambda r: log().info("resp:", r.msg))
    .msg(MyData("hello", 1)).request())
```

服务成员函数作异步回调时**必须** `self.weak_func(...)`（见"服务删除与弱绑定回调"节）；普通 lambda 须自包含数据。

### 服务内省（调试）

框架内置 `/YomkServerInfo` 服务（随 `server().init()` 自动启动），经服务器域内省读终结查询：服务列表、函数列表、单函数元信息、全量拓扑。功能函数安装时用 `install_func` 三参形式声明期望消息类型、四参附函数说明（均仅作内省元数据，不参与运行时校验；说明经 `fn_url(...).get_info()` 结构化包透出；两参写法兼容，内省无类型）：

```python
def init(self):
    self.install_func("/my_func", self.my_func, "MyData")  # 三参：内省可见类型 MyData
    self.install_func("/my_desc", self.my_desc, "MyData", "说明")  # 四参：类型 + 函数说明
    return 0

srv_info().services()                              # 服务列表（data 为服务名列表）
srv_info().srv_name("/XxxService").functions()     # 函数结构化清单（data 为 FuncInfo 列表：func_name/msg_name/description）
srv_info().fn_url("/XxxService/my_desc").get_info()  # 单函数元信息（结构化 FuncInfo 包，msg 恒 "ok"；未命中 Failed）
srv_info().all()                                   # 全量拓扑（ServerServiceInfo 层级列表：service_name + funcs）
```

服务器层内省之外，四个内置模块均提供模块内层内省链（Context 的 `ctx().keys()/key(k).info()/groups()/all()`、EventLoop 的 `event_loop().list()/loop(n).get_info()/all()`、FunctionPool 的 `fn_pool().list()/fn_name(n).get_info()/all()`、Logger 的 `log_mgr().console()/file().list()/get_info()/status()` 与 `log_mgr().all()`——`.all()` 仅 `log_mgr()` 直调可达），见 [reference.md](reference.md) 对应小节与 [examples.md](examples.md) 示例5。

## 任务三：创建扩展库

当用户要求创建独立扩展时，**必须**生成完整可运行的扩展骨架。扩展为 yomk 命名空间并入形态：独立仓库源树只写 `src/yomk/<域>/`（运行时服务）、`src/yomk/msg/<域>/`（消息类型）与 `src/yomk/url/<域>/`（URL 常量池，并入框架 `yomk.url` 常量池）三个叶子目录，通过 install.sh 合并安装到 YomkServerPy 的 site-packages/yomk/ 下（PEP 420 命名空间与框架子包平级合并，不动框架文件），安装后任意终端可 `from yomk.<域> import ...` 引用。

### 目录结构（模板实名）

```
ExtensionName/                        // 扩展仓库根（域名 extension → 用户指定名）
├── src/yomk/
│   ├── msg/<域>/             // 消息子包（并入框架 yomk.msg 命名空间段）
│   │   ├── __init__.py       //   re-export 扩展消息类
│   │   ├── ExtensionMsg.py   //   消息类型定义（ExtensionRequest/ExtensionResponse）
│   │   └── py.typed
│   ├── <域>/                 // 运行时域子包（并入框架 yomk 命名空间段）
│   │   ├── __init__.py       //   __version__ + re-export 服务类与 builder
│   │   ├── __init__.pyi      //   类型桩
│   │   ├── builder.py        //   <域> 域 builder（可选件：只封装本模块自有接口）
│   │   ├── builder.pyi       //   类型桩
│   │   ├── ExtensionService.py  // 服务实现（YomkService 子类）
│   │   └── py.typed
│   └── url/<域>/             // URL 常量池子包（并入框架 yomk.url 常量池，与 yomk/url/ctx 同构）
│       ├── __init__.py       //   <域> 端点常量（大写蛇形，与框架 url 域同风格）
│       ├── __init__.pyi      //   类型桩
│       └── py.typed
├── test/
│   └── test_extension.py  // 测试程序（就地运行，不随扩展安装）
├── install.sh               // 扩展安装脚本（交互式，含命名空间守卫）
└── README.md
```

### 关键约定

1. **域名占位符替换**：域目录 `extension` → 用户指定域名（snake_case，如 `payment`），import 语句、服务类 `ExtensionService`/`XxxService`、消息类 `ExtensionRequest`/`ExtensionResponse` 同步替换，不做大小写转换
2. **命名空间纪律（必须遵守）**：框架 `yomk/`、`yomk/msg/`、`yomk/url/` 三层是 PEP 420 命名空间段（无 `__init__.py`），扩展仓库**不得创建** `yomk/__init__.py`、`yomk/msg/__init__.py` 或 `yomk/url/__init__.py`（会遮蔽框架其它子包，install.sh 安装前做守卫检查），只新增自己的叶子目录
3. **版本常量定义在运行时域 `__init__.py`**：`__version__` 常量为版本单一来源，与 re-export 语句同文件；子模块**不得**反向 `from . import __version__` 取用：该写法依赖 `__init__` 的语句顺序，IDE/linter 的 import 自动重排会把子模块导入挪到常量定义之前，安装后即触发 circular import（实测踩坑）；`__init__.py` 仅含 re-export 语句 + 常量定义，任何 import 顺序都正确
4. **模板接口最小化**：模板服务默认只包含 `/echo` 与 `/version` 两个接口，`/echo` 演示扩展消息的强类型往返（`ExtensionRequest` 作请求包、`ExtensionResponse` 承载在 `Response.data` 位），`/version` 供 extension 域 builder 版本查询终结使用，不生成任何示例业务接口（如加减乘除）；业务功能通过「继续扩展」添加
5. **扩展 API 形态纪律**：`yomk/<域>/builder.py` 为可选件——builder 是模块自己的 API，**只封装本模块自有接口**，不重复框架通用请求链路；可以不写 builder，但不提供时**须在运行时域 `__init__` 或 URL 常量池注释中写明统一请求调用形态并说明各 URL 常量的功能作用**，保证使用方无 builder 也有文档可循；调用方**推荐优先**使用框架封装好的统一请求接口 `server().url(EXTENSION_ECHO).msg(ExtensionRequest("hello ext")).request()`，builder 终结仅作模块自有便捷入口
6. **数据源无关原则**：扩展只负责处理逻辑，不关心数据来源。所有外部数据（如文件内容、路径等）必须通过请求参数传入，扩展内部不硬编码任何数据源
7. **安装走 install.sh**：交互询问 YomkServerPy 安装路径（默认取 `$YOMK_PREFIX_PATH`），校验框架 yomk 命名空间包就位 → 命名空间守卫（扩展源树不得含 `yomk/__init__.py`/`yomk/msg/__init__.py`/`yomk/url/__init__.py`）→ 将 `src/yomk/<域>/`、`src/yomk/msg/<域>/` 与 `src/yomk/url/<域>/` 幂等合并复制到 site-packages/yomk/ 下（仅落扩展自身文件，不删框架文件）；安装目录不可写时用 sudo；装后以仅含 site-packages 的 PYTHONPATH 验证 `import yomk.<域>`（模拟新终端环境）
8. **测试程序就地运行**：测试脚本不含路径自举，直接 `python3` 运行，依赖扩展已安装，不随扩展安装；通过 `server().srv(ExtensionService).create()` **显式注册**（框架不做隐式发现）并以统一请求接口验证功能，退出码 0 = 通过
9. **README 使用示例完整可复制**：使用示例必须提供完整的 main.py（含按域 import、`server().init()`、`server().srv(...).create()` 显式注册扩展服务、`url(...).msg(...).request()` 与输出），用户复制后即可运行；不得只提供请求代码片段
10. **不适用项（语言差异）**：无 CMake 包导出（原生 import 即引用）、无 ldconfig 注册（site-packages 已在 PYTHONPATH 内）、无 include/src 头文件分层（域 `__init__.py` re-export 即对外接口）

### 生成规则

- **优先复制模板**：本 skill 所在 `skill/` 目录的上层即 YomkServerPy 仓库根；若其下存在 `template/extension/` 目录，优先复制并替换域名/类名占位符（模板实名：`src/yomk/extension/` + `src/yomk/msg/extension/` + `src/yomk/url/extension/`、服务文件 `ExtensionService.py`、消息文件 `ExtensionMsg.py`、测试 `test/test_extension.py`）；目录不存在时按 [examples.md](examples.md) 示例7 逐文件生成
- 将扩展名（域名）替换为用户指定名称
- 所有文件必须完整生成，确保 `bash install.sh` 可直接安装运行

### 继续扩展

在已有扩展中添加新功能：

1. **服务类添加功能函数**（`src/yomk/<域>/ExtensionService.py`）：`install_func` + 实现
2. **测试程序添加测试用例**（`test/test_extension_name.py`，与包内测试文件同步改名）
3. **README 功能表添加行**

## 服务删除与弱绑定回调

`srv_name(n).remove()` 按名注销并触发该服务 `deinit`，返回 -1 的三种场景：服务不存在 / 名称为空 / 服务器未初始化。**服务删除后成员回调不得再被调用**——框架以弱绑定判活兜底：

- 成员函数注册到**外部子系统**时必须 `self.weak_func(方法)` 弱绑定，共四类场景：

```python
class MyService(YomkService):
    def init(self):
        # 1) 函数池：服务删除后 call() 得 Failed + 契约消息
        fn_pool().fn_name("my_work").fn(self.weak_func(self.my_work)).install()
        # 2) Context checker/monitor：前提是键已存在（先 create 再注册）
        ctx().key("k").value("v").create()
        ctx().key("k").checker_fn(self.weak_func(self.my_check, discard="check")).apply()
        ctx().key("k").monitor_fn(self.weak_func(self.my_monitor, discard="none")).apply()
        # 3) 事件循环处理函数
        event_loop().loop("L").handle_fn(self.weak_func(self.on_event)).start()
        # 4) 异步响应回调（框架不自动弱绑定）
        server().url("/OtherService/func").async_(self.weak_func(self.on_resp)).request()
        return 0
```

- `weak_func(func, discard=None)` 的 `discard` 声明失效丢弃形态（三种形态均同时打错误日志）：`"response"` → `Failed` + 契约消息 `"service has been deleted or unregistered, callback ignored."`；`"check"`（checker 用）→ 放行 `CheckStatus.eAccept`；`"none"`（void 语义回调）→ `None`。`discard=None` 未显式声明时由**消费槽位注册期按 API 形态自动填充缺省**（checker→`"check"`、monitor→`"none"`、函数池/事件循环→`"response"`，未过槽位直调兜底 `"response"`）；显式声明与槽位不符时**注册期立即 diag_error**（早于回调真正被调用），携带具体值。
- **双层判活**：包装层查服务注销标志，调用层查服务表命中；服务删除即刻失效。
- `install_func("/x", self.method)` 注册到**本服务**函数表已自动弱绑定，无需手动处理。
- 覆写 `deinit()` 主动停止**非弱绑定路径**的残留引用（如自持线程、外部订阅句柄）并注销外部子系统登记。
- 异步回调生命周期：`.async_(cb)` 的 `cb` 送达时服务可能已删除，故成员方法回调必须走 `weak_func`；普通 lambda 须自包含数据、不引用服务 `self`。

## 优雅关闭（shutdown）

`server().shutdown()` 关闭服务器：**幂等**，假定主线程调用。序列为——拒绝新请求并排空/join 异步请求池 → 清空服务表逐个 `deinit`，在途请求安全执行完毕后服务才释放（排空路径不置位注销标志、不记删除 WARN）。

- **deinit 的四个触发时机**：`srv_name(n).remove()` 删除单个服务；`shutdown()` 关闭全部服务；同名服务被 `add()` 热替换（旧服务先置位注销标志并 deinit，新服务随后 init）；进程退出 atexit 兜底（忘记 shutdown 时）。
- **关闭后不支持二次 init**：init 进程内幂等由 once 标志把关，`shutdown()` 之后再次 `init()` 不会重建服务器，全部入口回到未初始化守卫态（终结动作 `Invalid`/`-1`、异步丢弃并记诊断、类型化读回退默认值；守卫消息 `"YomkServer is not initialized"`）。
- **`shutdown()` 置空单例**：门面在 `shutdown()` 时置空单例，`instance()` 返回 `None`，不保留已关闭实例快照。
- `version()` / `instance()` / `shutdown()` 免初始化安全。

## 编程规范

### 消息包

- 消息包为普通 Python 类，定义在 `msgs/YomkMsgs.py`
- 作为 `pkg` 参数传入服务函数，直接通过属性访问字段
- 返回值通过 `Response.data` 携带任意数据

### 设计原则

1. 每个 Service 只负责单一业务域
2. 服务间通过 `server().url(u).msg(pkg).request()` 通信，不直接引用
3. 共享状态用 Context，耗时操作用 EventLoop，公共函数用 FunctionPool
4. 消息定义集中 `msgs/`，服务实现放 `services/`
5. 配置路径通过 Context 传递，不用构造参数
6. **注册到外部子系统（函数池 / 事件循环 / Context checker·monitor / 异步响应回调）的服务成员函数必须 `self.weak_func(...)`**——服务删除或 shutdown 后回调自动失效，不得访问已注销服务的 `self`
7. **状态码口径**：成功 `Succeeded`；存在性未命中（服务/键/函数/循环不存在）用 `Failed`（`eNo`）归 not-found；入参非法用 `Invalid`（`eInvalid`）——三态口径全框架一致，not-found 不得归 Invalid

### 日志

- 业务体内日志用发射链：`log().tag(tag).info(*args)`（六级别 `debug/info/warn/error/trace/fatal`，级别枚举 `Log.eTrace=0..eFatal=5`），位置前缀 `[行号]` 自动注入调用处
- 无独立 tag 需求时省略 `.tag()`（缺省日志器名 `"main"`；tag 即日志器名，首次使用惰性创建）
- 文件日志先注册再发射：`log_mgr().dir(d).file(name).create()` → `log().to_sink(name).tag(tag).info(*args)`（`to_sink` 未知名 `Failed`、不惰性创建）
- 输出行格式：`[YYYY-MM-DD HH:MM:SS.mmm] [INFO ] [tag] [行号] 内容`
- 叙事横幅/演示输出走 `print`，不占日志通道
- 详细语义（两级门控、代理、内省归因）见仓库根 [docs/YomkLogger.md](../../docs/YomkLogger.md)

## 验证与质量

工程生成后按下述口径自证：

1. **运行验证**：工程目录 `bash check_env.sh`（环境/依赖就绪）→ `python main.py`；观察六级别日志行与业务请求响应输出，进程退出无未捕获异常
2. **测试约定**：工程 `test/` 内为独立可执行脚本，逐用例输出 `[PASS]/[FAIL]` 与 `Test Summary`，存在失败时退出码为 1；驱动侧 `print`，服务体内走日志通道
3. **框架全量回归**（改动框架源码后）：仓库根 `./build_ubuntu.sh` 构建 → `./test/run_tests.sh` 全量用例全绿
4. **静态检查**（改动框架源码后）：仓库根 `bash test/run_static_checks.sh`——ruff（E4/E7/E9/F 规则集，零告警）+ mypy（零告警）双门禁，输出 `[PASS]` 与日志目录

## 详细参考

- 完整工程代码与逐场景示例：[examples.md](examples.md)（示例0–7）
- API 与数据结构详细参考：[reference.md](reference.md)
- 语义文档（契约实串与注意事项权威源，仓库根 `docs/`）：[YomkServer](../../docs/YomkServer.md)、[YomkMsg](../../docs/YomkMsg.md)、[YomkLogger](../../docs/YomkLogger.md)、[YomkContext](../../docs/YomkContext.md)、[YomkEventLoop](../../docs/YomkEventLoop.md)、[YomkFunctionPool](../../docs/YomkFunctionPool.md)、[YomkServerInfo](../../docs/YomkServerInfo.md)
- 可运行示例（仓库根 `examples/`）：`ExampleYomkService.py`、`ExampleYomkContext.py`、`ExampleYomkEventLoop.py`、`ExampleYomkFunctionPool.py`、`ExampleYomkLogger.py`、`ExampleYomkServerInfo.py`
