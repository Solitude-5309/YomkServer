# YomkServerPy

Python Modular Fast Development Framework (Cython)

## 简介

YomkServerPy 是 YomkServer 的 Python 版本：核心服务逻辑以 Cython 编译为 C 扩展加速，对外提供纯 Python API。遵循关注点分离、约定优于配置的设计哲学，沿用"一切皆服务，一切皆请求"的设计理念，通过标准化的通信接口和灵活的模块机制，实现系统组件的高度解耦和动态组合。

- **进程内服务总线**：服务（`YomkService`）注册到服务器（`YomkServer`），通过 URL 格式 `"/服务名/功能函数名"` 进行同步或异步调用。两级模块化——`YomkService` 封装独立业务域或技术组件，`Function` 为服务内以唯一 URL 路径标识的功能单元
- **轻量消息包**：普通 Python 类即可定义消息，实例直接作为请求负载传入、经回包 `data` 返回，属性直接访问
- **开箱即用内置能力**：`YomkApi.server().init()` 即自动装配共享上下文、线程隔离事件循环、动态函数池、分级日志与服务内省五个内置模块，无需额外配置
- **链式域入口**：六个域入口——`YomkApi.server()` / `YomkApi.log()` / `YomkApi.log_mgr()` / `YomkApi.ctx()` / `YomkApi.event_loop()` / `YomkApi.fn_pool()`——以"前置累积 + 无参终结"的链式形态暴露全部能力，屏蔽底层 URL 路由；API 是壳而非墙，需要时可直接取 `YomkApi.server().instance()` 裸调 `request()`

### 模块清单

框架核心模块与职责一览（完整链式语法、使用示例与注意事项见 [docs](docs/) 目录对应文档，另见[核心概念与使用文档](#核心概念与使用文档)；其中共享上下文、事件循环、函数池、日志、服务内省为 `YomkApi.server().init()` 自动装配的内置模块）：

| 模块 | 简述 |
|---|---|
| [服务（`YomkService` / `Function`）](docs/YomkServer.md) | 两级模块化的载体：服务封装独立业务域或技术组件，`install_func()` 安装服务内功能函数 |
| [共享上下文（`YomkContext`）](docs/YomkContext.md) | 全局键值状态增删改查，支持变更前校验门控（Checker）与变更监控（Monitor） |
| [线程隔离事件循环（`YomkEventLoop`）](docs/YomkEventLoop.md) | 单线程 FIFO 执行队列：同循环内保序、跨循环并行，适配 I/O 密集与高并发调度 |
| [动态函数池（`YomkFunctionPool`）](docs/YomkFunctionPool.md) | 函数统一注册与调度，支持同名热替换，面向过程范式快速迭代 |
| [分级日志（`YomkLogger`）](docs/YomkLogger.md) | 控制台/文件双输出六级别日志，支持级别开关、日志代理、位置前缀、文件落盘与日志器内省 |
| [服务内省（`YomkServerInfo`）](docs/YomkServerInfo.md) | 运行期服务表内省：服务清单、功能函数清单、单函数元信息与全量拓扑，调试运维用 |
| [启动编排（`YomkBoot`）](docs/YomkServer.md#启动编排yomkboot) | 以 before → start → after 三阶段编排多服务启动，任一阶段失败即终止 |

仓库目录模块：

| 位置 | 简述 |
|---|---|
| `src/` | 核心库源码：`YomkApi.py` 门面、Cython 扩展 `YomkServerPy`、`.pyi` 类型提示与版本模块 |
| `examples/` | 可运行示例：服务调用、共享上下文、事件循环、函数池、日志、服务内省六大主题 |
| `template/` | 脚手架：`project/` 新工程模板、`extension/` 扩展库模板 |
| `scripts/` | `install_env.sh` 安装校验与 `PYTHONPATH` / `YOMK_PREFIX_PATH` 持久化 |
| `test/` | 测试集合与一键测试脚本 `run_tests.sh` |
| `CMakeLists.txt`、`cmake/`、`build_ubuntu.sh` | 构建安装体系：Cython 编译、install 规则与环境指纹报告（`abi_report.txt`） |

---

## 快速上手

### 1. 最小完整示例

```python
import YomkApi

# 1. 自定义服务：继承 YomkService，init() 中安装功能函数
class MyService(YomkApi.YomkService):
    def __init__(self, server):
        super().__init__(server)

    def init(self):
        self.install_func("/hello", self.hello)  # 功能函数名以 / 开头

    # 功能函数统一签名：(pkg) -> YomkResponse
    def hello(self, pkg):
        return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, f"hello {pkg}")

# 2. 初始化框架（自动启动内置服务：Context / FunctionPool / EventLoop / Logger / ServerInfo）
YomkApi.server().init()

# 3. 注册自定义服务
YomkApi.server().srv(MyService, "/MyService").create()

# 4. 同步请求：url/msg 前置累积，request() 无参终结
resp = YomkApi.server().url("/MyService/hello").msg("yomk").request()
YomkApi.log().info("Response received:", resp.msg)

# 5. 异步请求：投递立即返回，回包到达时在回调线程输出；
#    shutdown() 排空异步请求池，保证回调送达
(YomkApi.server().url("/MyService/hello")
    .async_(lambda r: YomkApi.log().info("Async:", r.msg))
    .msg("yomk2").request())

YomkApi.server().shutdown()
```

预期控制台输出（时间戳与行号按实际运行变化）：

```text
[2026-11-07 12:14:58.358] [INFO ] [main] [23] Response received: hello yomk
[2026-11-07 12:14:58.358] [INFO ] [main] [28] Async: hello yomk2
```

---

## 核心概念与使用文档

### 1. 请求与响应模型 (Request / Response)

所有服务间交互均遵循统一的请求响应抽象：

- **URL 寻址**：格式为 `"/服务名/函数名"`，如 `"/MyService/hello"`。
- **返回值 `YomkResponse`**：
  - `status`：
    - `eOk (0)`：调用成功。
    - `eNo (1)`：目标服务或函数不存在、被拒绝或条件不满足。
    - `eInvalid (-1)`：入参非法或服务器未初始化。
  - `msg`：状态描述文本。
  - `data`：可选的负载消息（任意 Python 对象）。
- **同步调用**：
  ```python
  resp = YomkApi.server().url("/服务名/函数名").msg(pkg).request()
  ```
- **异步调用**：投递后立即返回，回包到达时在回调线程执行：
  ```python
  (YomkApi.server().url("/服务名/函数名")
      .async_(lambda resp: YomkApi.log().info(resp.msg))
      .msg(pkg).request())
  ```
- **跨服务调用**：服务实现内部用成员 `self.request(url, pkg)` 调用其他服务（`YomkService` 基类提供，与 `YomkApi.server().url(url).msg(pkg).request()` 裸链等价；见 `examples/services/MotionService.py`）。

完整可运行演示见 `examples/ExampleYomkService.py`（控制柜一次上电会话 8 个场景：上电自检与工位注册、通讯自检与寻址异常、异步下发、运动调用链、自省读取、工位热替换、工位下线断链传播、收班断电，读运行输出即可理解每个调用）。

详见 [服务总线 YomkServer.md](docs/YomkServer.md)。

### 2. 消息包 (Message)

用于在服务与事件循环间传递数据。Python 版无需宏或类型注册：普通类即消息定义，实例直接传递，属性直接访问：

```python
# 定义消息（普通类）
class MoveCmd:
    def __init__(self, x, y, z, speed_ratio):
        self.x = x
        self.y = y
        self.z = z
        self.speed_ratio = speed_ratio

# 请求侧构造，功能函数内直接读取属性；回包 data 直接携带对象（如强类型 MoveResp）
resp = YomkApi.server().url("/MotionService/move").msg(MoveCmd(100.0, 50.0, 200.0, 0.8)).request()
print(resp.data.reach_x)
```

消息定义可集中在独立模块管理（见 `examples/msgs/ArmMsgs.py`）。

详见 [消息包 YomkMsg.md](docs/YomkMsg.md)。

### 3. 共享上下文 (YomkContext)

统一管理框架运行时的全局键值状态，支持变更前校验门控（Checker）与变更监控（Monitor）：

```python
# 1. 创建 / 读取 / 更新 / 销毁（读终结 get() 未命中回 with_default 兜底值）
YomkApi.ctx().key("config").value("init_value").create()
val = YomkApi.ctx().key("config").msg(str).with_default(None).get()
YomkApi.ctx().key("config").value("new_value").set()
YomkApi.ctx().key("config").destroy()

# 2. 变更前校验门控（Checker）：回调返回 eReject 拦截本次 set，返回 eAccept 放行；
#    键级回调先注册（键须已存在），组开关默认关须显式开启
YomkApi.ctx().key("config").checker_fn(lambda ctx: YomkApi.CheckStatus.eAccept).apply()
YomkApi.ctx().group("main").checker().enable()   # .disable() 关闭

# 3. 变更后监听（Monitor）：async_(True) 时按提交序异步送达
YomkApi.ctx().key("config").monitor_fn(lambda ctx: print(ctx.value)).apply()
YomkApi.ctx().group("main").monitor().enable()   # .disable() 关闭

# 4. 运行期自省
YomkApi.ctx().keys()                             # 组内键名清单
YomkApi.ctx().key("config").info()               # 单键元信息（含所属组 group 字段）
YomkApi.ctx().groups()                           # 全部组名清单
YomkApi.ctx().all()                              # 跨组聚合：组 → 键元信息两层结构

# 5. 自定义组：非 main 组名首次寻址即自动创建（惰性、不支持销毁、组名唯一）
YomkApi.ctx().key("speed_ratio").group("robot_a").value(1.0).create()
```

组名约定：组名经 `.group(g)` 分面指定，缺省 `"main"`；键所属组随 `ContextKeyInfo.group` 一并内省返回。空组名返回 `eNo` `"group name is empty"`；非 main 组首次寻址即自动创建，监控池每组自持（每组一个单线程池，随建组只增不减）。

内部结构：Context 功能承载在 `ContextGroup`（底层实例组，不继承 `YomkService`）上——键存储、checker/monitor 门控开关与单线程异步监控池均为每组自持，内省快照携带所属组名。`YomkContext` 服务持默认组成员 `m_main`（组名 "main"，成员直取免查找）与自定义组表 `m_customGroup`（dict + 读写锁），负责 14 个端点的装配与组路由：处理函数统一"解出组名 → `find_group` 定位 → 组实例委托"。`find_group` 三段式：空组名返回 `eNo "group name is empty"`；main 成员直取；非 main 读锁快查自定义组表、未命中写锁内惰性自动创建（组不支持销毁，只增不减，同名寻址复用同组）。

完整可运行演示见 `examples/ExampleYomkContext.py`（创建/读取/更新/销毁、checker 拒绝/接受、异步监控）。

详见 [共享上下文 YomkContext.md](docs/YomkContext.md)。

### 4. 线程隔离事件循环 (YomkEventLoop)

为特定业务提供单线程 FIFO 执行队列：同一循环内事件按入队顺序在专用线程中执行，不同循环之间彼此并行，适用于 I/O 密集型和高并发任务调度：

```python
# 启动事件循环（绑定默认处理函数，运行在独立线程）
YomkApi.event_loop().loop("TaskLoop").handle_fn(event_handle).start()

# 非阻塞投递：立即返回（三态仅表示入队结果），事件由循环线程异步执行
YomkApi.event_loop().msg("task1").loop("TaskLoop").post()

# 阻塞投递：等待执行完成，链式终结返回 ResStatus；
# 需要事件执行结果（Event 对象含 m_eventId / m_response）时用 server 域裸链
from YomkServerPy import Event
res = YomkApi.server().url("/YomkEventLoop/post_wait").msg(Event("TaskLoop", "task2")).request()
print(res.data.m_eventId, res.data.m_eventLoopName, res.data.m_response.msg)

# 停止（保留队列事件，可再次 start 续跑）与销毁（清空队列，不可续跑）
YomkApi.event_loop().loop("TaskLoop").stop()
YomkApi.event_loop().loop("TaskLoop").destroy()
```

完整可运行演示见 `examples/ExampleYomkEventLoop.py`（start/post/post_wait/stop/destroy 全生命周期）。

详见 [事件循环 YomkEventLoop.md](docs/YomkEventLoop.md)。

### 5. 动态函数池 (YomkFunctionPool)

统一函数注册与调度中心，面向过程开发范式，支持运行时注册、更新和热替换，业务逻辑快速迭代：

```python
# 注册函数（函数签名：(pkg) -> YomkResponse；msg_name 声明期望消息类型、description 附函数说明，均仅内省元数据）
YomkApi.fn_pool().fn_name("calc").fn(func_v1).install()
YomkApi.fn_pool().fn_name("greet").fn(greet).msg_name("String").description("向指定的人问好").install()

# 调用（未注册的名字返回 eNo；阻塞至函数执行完成）
resp = YomkApi.fn_pool().fn_name("calc").msg("data").call()

# 单函数内省：命中 msg 恒 "ok"，data 为 FuncInfo 包（func_name/msg_name/description）
resp = YomkApi.fn_pool().fn_name("greet").get_info()

# 同名重复 install 即运行时热替换，下一次调用执行新实现，无需重启
YomkApi.fn_pool().fn_name("calc").fn(func_v2).install()
```

完整可运行演示见 `examples/ExampleYomkFunctionPool.py`（注册/调用、类型与说明声明、同名热替换）。

详见 [函数池 YomkFunctionPool.md](docs/YomkFunctionPool.md)。

### 6. 服务生命周期与启动编排 (YomkService / YomkBoot)

服务定义（见快速上手）与注册：

- **注册**：`YomkApi.server().srv(MyService, "/MyService").create()`（内部完成构造、设名、init、注册）；注册已有实例用 `YomkApi.server().srv_ptr(srv).srv_name(name).add()`（同名 `add()` 即热替换：旧服务先 deinit 再注册新服务）。
- **删除**：`YomkApi.server().srv_name("/MyService").remove()` 按名注销并触发 `deinit`；`YomkApi.server().shutdown()` 优雅关闭全部服务（排空在途请求后逐个 deinit，进程内幂等）。
- **启动编排**：多服务工程继承 `YomkApi.YomkBoot` 组织三阶段启动——`before()`（服务启动前准备资源）→ `start()`（创建并注册服务）→ `after()`（启动后善后），任一阶段返回非 0 即终止启动；`YomkApi.server().boot_ptr(boot).run()` 内部先完成框架初始化（自动调用 init）再依次执行三阶段（见 `examples/boot/ArmBoot.py`）。
- 服务名与功能函数名均以 `/` 开头，共同构成请求 URL。

详见 [服务总线 YomkServer.md](docs/YomkServer.md)。

### 7. 分级日志 (YomkLogger)

控制台/文件双输出、六级别（trace/debug/info/warn/error/fatal）分级日志，支持级别开关、日志代理、位置前缀与文件落盘：

```python
from YomkServerPy import Log   # 六级常量 eTrace..eFatal 与代理回调 Log 包

# 1. 控制台发射：无 tag 便捷版（缺省日志器名 "main"）与 tag 版
#    （tag 随记录显示、兼作日志器名，首次使用自动创建同名日志器）
YomkApi.log().info("service started")
YomkApi.log().tag("biz").info("user login, id=", 7)   # 同族 warn/error/debug/trace/fatal

# 2. 级别开关两级主闸并联：console()/file() 空名 = 全局主闸，具名 = 逐器门控单个日志器，
#    输出取两层 AND；控制台与文件两套主闸相互独立
YomkApi.log_mgr().console().level(Log.eDebug).disable()      # 控制台主闸关 DEBUG
YomkApi.log_mgr().console("biz").level(Log.eInfo).disable()  # 具名逐器关断：仅 biz 的 INFO 无输出（未命中 eNo 不惰性创建）
resp = YomkApi.log_mgr().console().status()      # 查控制台主闸状态（LoggerState：六开关 + proxy_on）
resp = YomkApi.log_mgr().file().status()         # 查文件主闸状态（逐器真值条目走 .get_info()/.all()）
YomkApi.log_mgr().console("biz").level(Log.eInfo).enable()   # 具名恢复
YomkApi.log_mgr().file().level(Log.eDebug).disable()         # 文件主闸关 DEBUG（与控制台独立）

# 3. 日志代理：每条控制台日志先过代理，返回 False 消费该条（不再输出）；文件代理与之相互独立
YomkApi.log_mgr().console().proxy(lambda log: False).apply()               # 传 None 卸载，恢复默认输出
YomkApi.log_mgr().file().proxy(lambda log: log.m_level != Log.eDebug).apply()  # 文件代理拦 DEBUG（False 不落盘）

# 4. 位置前缀三开关（进程级，控制台与文件同时生效；默认仅行号 [N]，apply() 恒 eOk）
YomkApi.log_mgr().location().show_file(True).show_func(True).apply()

# 5. 文件日志：创建日志器 → to_sink 写内存缓冲 → 显式落盘为 dir/name.log
YomkApi.log_mgr().dir("./logs").file("app").create()
YomkApi.log().to_sink("app").tag("biz").info("order=", 1)    # tag 可叠加；未知名 eNo 不惰性创建
YomkApi.log_mgr().file("app").flush()

# 6. 删除日志器：同时清理 console/file 两表；未落盘缓冲自动落盘，磁盘 .log 保留
YomkApi.log_mgr().file("app").remove()
```

完整可运行演示见 `examples/ExampleYomkLogger.py`（六级别、两级开关、代理消费/放行、位置前缀、文件日志回读与删除后惰性重建）。

详见 [分级日志 YomkLogger.md](docs/YomkLogger.md)。

### 8. 服务内省 (YomkServerInfo)

内置内省服务，随 `YomkApi.server().init()` 自动启动，运行期查询"框架里现在有什么、每个功能函数期望什么消息类型"：

```python
# 服务清单（data 为服务名列表）
resp = YomkApi.server().services()

# 指定服务的功能函数结构化清单（data 为 FuncInfo 列表：func_name/msg_name/description，
# 与单查、全量拓扑同族；不存在服务返回空表）
resp = YomkApi.server().srv_name("/MotionService").functions()

# 单函数元信息：命中返回结构化 FuncInfo 包（msg 恒 "ok"，
# msg_name=期望消息类型名、description=函数说明）；不存在返回 eNo
resp = YomkApi.server().fn_url("/MotionService/move").get_info()

# 全量拓扑：结构化层级列表（每服务条目 service_name + funcs）
resp = YomkApi.server().all()
```

说明：期望消息类型由 `install_func()` 三参形式声明（函数池/事件循环侧为 `.msg_name()` 分面），自定义函数说明经第四参或 `.description()` 声明（均仅作内省元数据，不参与运行时校验）；自省读的是运行期服务表，注册/删除即时反映到下一次查询。完整可运行演示见 `examples/ExampleYomkServerInfo.py`（四类查询、注册/删除实时视图、关闭后返回值契约）。

详见 [服务自省 YomkServerInfo.md](docs/YomkServerInfo.md)。

### 9. 服务生命周期与弱绑定 (Weak Binding)

- **deinit 的四个触发时机**：`server().srv_name(n).remove()` 删除单个服务、`server().shutdown()` 关闭全部服务、同名服务被 `add()` 热替换（旧服务先 deinit 再注册新服务）、忘记关闭时进程退出经 atexit 兜底。覆写 `deinit()` 用于停止线程、注销外部资源。
- **弱绑定**：服务成员函数注册到**外部子系统**（函数池 / 事件循环 / Context checker·monitor / 异步响应回调）时必须经 `self.weak_func(方法)` 包装——服务删除后回调自动判活失效（按 `discard` 声明给出契约返回值），避免访问已注销服务的 `self`；`install_func()` 注册到本服务函数表已自动弱绑定，无需手动处理。

详见 [服务总线 YomkServer.md](docs/YomkServer.md)。

---

## 编译与安装

### Linux

#### 方式 1：自动安装脚本（推荐）

```bash
./build_ubuntu.sh
```

脚本将自动检查依赖（cmake/gcc/python3/python3-dev/cython/readerwriterlock，缺失时可选自动安装）并交互式完成配置、编译、安装（默认安装路径 `/opt/yomk`），随后调用 `scripts/install_env.sh` 设置环境变量 `YOMK_PREFIX_PATH` 与 `PYTHONPATH`——新开任意终端即可直接 `import YomkServerPy`（无需手动 source 或 export）。

#### 方式 2：手动 CMake 构建

与方式 1 产生完全一致的安装结果（同一套 install 规则 + 同一份安装后配置脚本）：

```bash
# 依赖
sudo apt update && sudo apt install gcc python3-dev cmake
python3 -m pip install cython readerwriterlock

# 编译安装
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/opt/yomk
cmake --build . -j
sudo cmake --install .
cd ..
./scripts/install_env.sh /opt/yomk   # 安装校验 + YOMK_PREFIX_PATH/PYTHONPATH 持久化（与方式 1 共用）
```

说明：

- `-DCMAKE_BUILD_TYPE=Release` 必须显式传入：Linux Makefiles 单配置生成器不识别 `--config Release`，缺失时将以无优化参数编译，产物与方式 1 不同
- Python C 扩展与构建时解释器主次版本绑定，请确保运行解释器版本一致；可比对安装产物 `share/YomkServerPy/abi_report.txt` 确认构建环境一致（记录 Python/Cython/gcc/发行版等指纹，随库安装）

测试为运行时脚本，随用例补充后按[测试](#测试)章节使用，无需编译开关。

### Windows

依赖与编译：

```
python3
python3 -m pip install cython readerwriterlock

mkdir build
cd build
cmake .. -DCMAKE_INSTALL_PREFIX="C:/Users/solit/Env/YomkServer/install"
cmake --build . --target install --config Release
```

运行示例：示例位于源码树 `examples/` 目录，将含 `YomkApi.py` 的目录加入 `PYTHONPATH` 后直接运行：

```powershell
cd <源码目录>\examples
python ExampleYomkContext.py
python ExampleYomkFunctionPool.py
python ExampleYomkEventLoop.py
python ExampleYomkService.py
python ExampleYomkLogger.py
python ExampleYomkServerInfo.py
```

---

## 示例

面向用户的可运行示例：

| 示例 | 演示内容 |
|---|---|
| ExampleYomkService.py | 服务定义与调用：上电注册/BOOT 三阶段编排、同步/异步请求、跨服务调用与失败传播、工位热替换与下线断链（多文件工程结构） |
| ExampleYomkContext.py | 共享上下文：创建/读取/更新/销毁 + 状态检查器（拒绝/接受）+ 异步监控 |
| ExampleYomkFunctionPool.py | 动态函数池：注册/调用 + 同名热替换 |
| ExampleYomkEventLoop.py | 线程隔离事件循环：启动/非阻塞投递/阻塞投递/停止/销毁 |
| ExampleYomkLogger.py | 分级日志：控制台六级别与级别开关、日志代理（消费/放行）、位置前缀、文件日志创建/写入/落盘/回读/删除 |
| ExampleYomkServerInfo.py | 服务内省：服务清单/函数清单/单函数元信息/全量拓扑，注册与删除的运行期实时视图 |

运行方式（进入源码目录直接运行；PYTHONPATH 已由 `scripts/install_env.sh` 持久化，新终端免配置。Windows 需手动将含 `YomkApi.py` 的目录加入 `PYTHONPATH`）：

```bash
cd <源码目录>
./examples/ExampleYomkContext.py
# 或：python3 examples/ExampleYomkContext.py
```

多文件示例的组织方式本身也是教程：

```
examples/
├── ExampleYomkService.py     # 入口
├── boot/ArmBoot.py           # 启动编排（before -> start -> after 三阶段）
├── services/                 # 服务实现（MotionService / TelemetryService）
└── msgs/ArmMsgs.py           # 消息包定义
```

---

## 测试

一键测试脚本 `./test/run_tests.sh` 已就绪，测试按模块子目录组织（YomkServer / YomkContext / YomkEventLoop / YomkFunctionPool / YomkLogger / YomkServerInfo），覆盖生命周期、并发与压力场景：

```bash
./test/run_tests.sh                      # 快速模式（压测规模缩为 10000）
./test/run_tests.sh --full               # 完整规模（测试内部默认，10 万级投递；超时默认 900s）
./test/run_tests.sh --prefix DIR         # 指定测试库产物目录（缺省 <仓库根>/build/prefix）
./test/run_tests.sh --timeout N          # 单测试超时秒数（默认 300）
```

说明：

- **只消费仓库内本地产物**：默认 `<仓库根>/build/prefix`，自动探测其下 `lib/pythonX.Y/site-packages` 并设置 PYTHONPATH；产物缺失时报错并给出构建安装命令，不静默回退系统安装（避免多份产物不同步时跑旧库）
- 逐个运行 TESTS 清单（快→慢，stress 类压尾），每个测试在独立临时工作目录运行；任一测试失败（退出码非 0 / 超时）→ **立即停止**，输出 `[FAIL]` 行摘要与日志路径
- 日志落盘 `test/test_logs/<时间戳>/`（含 `summary.log`）
- **现场清理**：每个测试在独立临时工作目录运行（隔离 CWD，防意外写文件污染仓库）；测试临时产物统一落 `test/test_logs/tmp`（经 `YOMK_TEST_TMP_ROOT` 注入测试进程，独立单独运行时回退系统临时目录），启动时整目录重建（兜底上次崩溃遗留）、退出时整目录删除（含失败/中断/超时路径，经 EXIT trap 兜底）
- 测试程序风格约定：退出码 0 = 全部通过，非 0 = 存在失败用例（`[FAIL]` 行输出）
- 静态检查：独立于测试套件的仓库级静态检查见下节

### 仓库级静态检查（test/run_static_checks.sh）

与 `run_tests.sh` 成对（动态测试 + 静态检测），对 `src/` 库源码做**零告警门禁**：

```bash
./test/run_static_checks.sh            # 默认双跑 ruff + mypy
./test/run_static_checks.sh --ruff     # 仅 ruff（规则集 E4/E7/E9/F，--isolated 消除用户级配置干扰）
./test/run_static_checks.sh --mypy     # 仅 mypy（YomkApi.py / YomkServerPy.py 类型检查）
```

- 依赖：`python3 -m pip install --user ruff mypy`（缺失时报错并给出安装提示）
- 日志落盘 `test/test_logs/<时间戳>/`（`ruff.log` / `mypy.log` / `summary.log`），与测试日志同层
- 白名单（书面债务，与 C++ 侧 cppcheck `--suppress` 清单同构）：ruff 豁免 F401（伞门面 re-export）；mypy 豁免 assignment / misc / attr-defined / var-annotated 四个错误码（源码未回填类型注解），明细见脚本头部行为注记
- 任一档检出告警 → `[FAIL]` 摘要 + 非零退出码；全部零告警 → `[PASS]`

### 示例冒烟门禁（test/run_examples.sh）

`examples/` 六枚示例是各文档"完整示例"节指向的用户第一入口，独立于单元回归设运行级冒烟闸（S29；C++ 安装回归只校验示例二进制存在性、不运行，Python 示例不随库安装故以"真跑通"为等价更强口径）：

```bash
./test/run_examples.sh                   # 默认单例超时 300s
./test/run_examples.sh --prefix DIR      # 指定产物目录（与 run_tests.sh 同口径）
./test/run_examples.sh --timeout N       # 单示例超时秒数
```

- 逐例在独立临时目录全量运行（stdin=/dev/null，示例末尾 `input()` 有 `isatty` 守卫）；任一例非零退出/超时 → **立即停止**并尾打日志
- 日志落盘 `test/test_logs/<时间戳>_examples/`（含 `summary.log`）；清单带总数守卫（6 项声明）
- 闭环验收口径 = `run_tests.sh` **27/27** 且 `run_examples.sh` **6/6**

---

## Python 项目集成

安装完成后 PYTHONPATH 已持久化（Linux 由 `scripts/install_env.sh` 写入 profile），业务代码直接导入即可使用：

```python
import YomkApi   # 门面 API：自动加载 Cython 扩展 YomkServerPy，核心类型（YomkService / YomkResponse / ResStatus 等）均可经 YomkApi 直接使用
```

- 新开任意终端免配置；未持久化的环境（如 CI、Windows）手动设置：`export PYTHONPATH=${YOMK_PREFIX_PATH}/lib/pythonX.Y/site-packages:$PYTHONPATH`
- `YomkServerPy.pyi` 随库安装于同一目录，IDE 可据此提供类型提示与补全
- 版本号经 `YomkApi.__version__` 查询（源于 CMakeLists 的 `project(VERSION)` 单一来源，与 `share/YomkServerPy/abi_report.txt` 同源）
