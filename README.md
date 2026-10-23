# YomkServerPy

Python Modular Fast Development Framework (Cython)

## 简介

YomkServerPy 是 YomkServer 的 Python 版本：核心服务逻辑以 Cython 编译为 C 扩展加速，对外提供纯 Python API。遵循关注点分离、约定优于配置的设计哲学，沿用"一切皆服务，一切皆请求"的设计理念，通过标准化的通信接口和灵活的模块机制，实现系统组件的高度解耦和动态组合。

- **进程内服务总线**：服务（`YomkService`）注册到服务器（`YomkServer`），通过 URL 格式 `"/服务名/功能函数名"` 进行同步或异步调用。两级模块化——`YomkService` 封装独立业务域或技术组件，`Function` 为服务内以唯一 URL 路径标识的功能单元
- **轻量消息包**：普通 Python 类即可定义消息，实例直接作为请求负载传入、经回包 `data` 返回，属性直接访问
- **开箱即用内置能力**：`YomkApi.init()` 即自动装配共享上下文、线程隔离事件循环、动态函数池、分级日志与服务内省五个内置模块，无需额外配置

### 模块清单

框架核心模块与职责一览（详见[核心概念与使用文档](#核心概念与使用文档)；其中共享上下文、事件循环、函数池、日志、服务内省为 `YomkApi.init()` 自动装配的内置模块）：

| 模块 | 简述 |
|---|---|
| 服务（`YomkService` / `Function`） | 两级模块化的载体：服务封装独立业务域或技术组件，`install_func()` 安装服务内功能函数 |
| 共享上下文（`YomkContext`） | 全局键值状态增删改查，支持变更前校验门控（Checker）与变更监控（Monitor） |
| 线程隔离事件循环（`YomkEventLoop`） | 单线程 FIFO 执行队列：同循环内保序、跨循环并行，适配 I/O 密集与高并发调度 |
| 动态函数池（`YomkFunctionPool`） | 函数统一注册与调度，支持同名热替换，面向过程范式快速迭代 |
| 分级日志（`YomkLogger`） | 控制台/文件双输出四级别日志，支持级别开关、日志代理、文件落盘与日志器内省 |
| 服务内省（`YomkServerInfo`） | 运行期服务表内省：服务清单、功能函数清单、单函数元信息与全量拓扑，调试运维用 |
| 启动编排（`YomkBoot`） | 以 before → start → after 三阶段编排多服务启动，任一阶段失败即终止 |

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
YomkApi.init()

# 3. 注册自定义服务
YomkApi.new_service(MyService, "/MyService")

# 4. 同步请求
resp = YomkApi.request("/MyService/hello", "yomk")
print(resp.status, resp.msg)     # ResStatus.eOk hello yomk

# 5. 异步请求：立即返回，回包到达时在回调线程输出
YomkApi.async_request("/MyService/hello", "yomk2", lambda res: print("async:", res.msg))
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
  resp = YomkApi.request("/服务名/函数名", pkg)
  ```
- **异步调用**：投递后立即返回，回包到达时在回调线程执行：
  ```python
  YomkApi.async_request("/服务名/函数名", pkg, lambda resp: print(resp.msg))
  ```
- **跨服务调用**：`YomkService` 基类无成员级请求方法，服务实现内部用全局 `YomkApi.request()` 调用其他服务（见 `examples/services/CalcService.py`）。

完整可运行演示见 `examples/ExampleYomkService.py`（Boot 三阶段编排、同步/异步请求、跨服务调用与失败传播，读运行输出即可理解每个调用）。

### 2. 消息包 (Message)

用于在服务与事件循环间传递数据。Python 版无需宏或类型注册：普通类即消息定义，实例直接传递，属性直接访问：

```python
# 定义消息（普通类）
class AddReq:
    def __init__(self, a, b):
        self.a = a
        self.b = b

# 请求侧构造，功能函数内直接读取属性；回包 data 直接携带对象
resp = YomkApi.request("/CalcService/add", AddReq(1, 2))
print(resp.data.result)
```

消息定义可集中在独立模块管理（见 `examples/msgs/DemoMsgs.py`）。

### 3. 共享上下文 (YomkContext)

统一管理框架运行时的全局键值状态，支持变更前校验门控（Checker）与变更监控（Monitor）：

```python
# 1. 创建 / 读取 / 更新 / 销毁（key 不存在时 get 返回兜底默认值，永不返回 None 崩溃）
YomkApi.context_create("config", "init_value")
val = YomkApi.context_get("config", None)
YomkApi.context_set("config", "new_value")
YomkApi.context_destroy("config")

# 2. 变更前校验门控（Checker）：回调返回 eReject 拦截本次 set，返回 eAccept 放行
YomkApi.context_set_checker("config", lambda ctx: YomkApi.CheckStatus.eAccept)
YomkApi.context_turn_on_checker()              # context_turn_off_checker() 关闭

# 3. 变更后监听（Monitor）：async_monitor=True 时按提交序异步送达
YomkApi.context_set_monitor("config", lambda ctx: print(ctx.value))
YomkApi.context_turn_on_monitor()              # context_turn_off_monitor() 关闭
```

完整可运行演示见 `examples/ExampleYomkContext.py`（创建/读取/更新/销毁、checker 拒绝/接受、异步监控）。

### 4. 线程隔离事件循环 (YomkEventLoop)

为特定业务提供单线程 FIFO 执行队列：同一循环内事件按入队顺序在专用线程中执行，不同循环之间彼此并行，适用于 I/O 密集型和高并发任务调度：

```python
# 启动事件循环（绑定处理函数，运行在独立线程）
YomkApi.event_loop_start("TaskLoop", event_handle)

# 非阻塞投递：立即返回，事件由循环线程异步执行
YomkApi.event_loop_post("TaskLoop", "task1")

# 阻塞投递：等待执行完成；回包 data 携带事件 id、循环名与处理结果
res = YomkApi.event_loop_post_wait("TaskLoop", "task2")
print(res.data.m_eventId, res.data.m_eventLoopName, res.data.m_response.msg)

# 停止（不再消费事件，循环对象仍存在）与销毁（彻底释放资源）
YomkApi.event_loop_stop("TaskLoop")
YomkApi.event_loop_destroy("TaskLoop")
```

完整可运行演示见 `examples/ExampleYomkEventLoop.py`（start/post/post_wait/stop/destroy 全生命周期）。

### 5. 动态函数池 (YomkFunctionPool)

统一函数注册与调度中心，面向过程开发范式，支持运行时注册、更新和热替换，业务逻辑快速迭代：

```python
# 注册函数（函数签名：(pkg) -> YomkResponse；三参声明期望消息类型、四参附函数说明，均仅内省元数据）
YomkApi.function_pool_register("calc", func_v1)
YomkApi.function_pool_register("greet", greet, "String", "向指定的人问好")

# 调用（未注册的名字返回 eNo）
resp = YomkApi.function_pool_call("calc", "data")

# 单函数内省：命中 msg 为 "函数名 [类型名]"，四参函数说明挂 data 段
resp = YomkApi.function_pool_info_name("greet")

# 同名重复注册即运行时热替换，下一次调用执行新实现，无需重启
YomkApi.function_pool_register("calc", func_v2)
```

完整可运行演示见 `examples/ExampleYomkFunctionPool.py`（注册/调用、类型与说明声明、同名热替换）。

### 6. 服务生命周期与启动编排 (YomkService / YomkBoot)

服务定义（见快速上手）与注册：

- **注册**：`YomkApi.new_service(MyService, "/MyService")`（内部完成构造、设名、init、注册）；同一服务类注册多个实例用 `YomkApi.add_service(srv, name)`。
- **启动编排**：多服务工程继承 `YomkApi.YomkBoot` 组织三阶段启动——`before()`（服务启动前准备资源）→ `start()`（创建并注册服务）→ `after()`（启动后善后），任一阶段返回非 0 即终止启动；`YomkApi.boot(boot_obj)` 内部先完成框架初始化（自动调用 init）再依次执行三阶段（见 `examples/boot/DemoBoot.py`）。
- 服务名与功能函数名均以 `/` 开头，共同构成请求 URL。

### 7. 分级日志 (YomkLogger)

控制台/文件双输出、四级别（info/warn/error/debug）分级日志，支持级别开关、日志代理与文件落盘：

```python
# 1. 控制台日志：无 tag 便捷版（缺省日志器名 "MainLogger"）与 tag 版
#    （tag 随记录显示、兼作日志器名，首次使用自动创建同名日志器）
YomkApi.console_log_info("service started")
YomkApi.console_log_info_tag("biz", "user login, id=", 7)   # 同族 warn/error/debug

# 2. 级别开关（全局，只影响控制台；文件日志不受影响）
YomkApi.off_console_log_debug()    # 同族 on/off_console_log_info/warn/error

# 3. 日志代理：每条控制台日志先过代理，返回 False 消费该条（不再输出）
YomkApi.set_console_log_proxy(lambda log: False)            # 传 None 卸载，恢复默认输出

# 4. 文件日志：创建日志器 → 写内存缓冲 → 显式落盘为 dir/name.log
YomkApi.file_log_create("./YomkLog", "app")
YomkApi.file_log_info_tag("app", "MainLogger", "order=", 1) # 同族 warn/error/debug 与无 tag 便捷版
YomkApi.file_log_write("app")

# 5. 删除日志器：同时清理 console/file 两表；未落盘缓冲自动落盘，磁盘 .log 保留
YomkApi.file_log_delete("app")
```

完整可运行演示见 `examples/ExampleYomkLogger.py`（四级别、级别开关、代理消费/放行、文件日志回读与删除后惰性重建）。

### 8. 服务内省 (YomkServerInfo)

内置内省服务，随 `YomkApi.init()` 自动启动，运行期查询"框架里现在有什么、每个功能函数期望什么消息类型"：

```python
# 服务清单（data 为服务名列表）
resp = YomkApi.server_info_services()

# 指定服务的功能函数清单（每行 "函数名 [期望消息类型]"，未声明类型的函数无括号后缀）
resp = YomkApi.server_info_functions("/CalcService")

# 单函数元信息：命中时 msg 为期望消息类型名（未声明为空串），
# 四参函数说明经 data 段透出；不存在返回 eNo
resp = YomkApi.server_info_function("/CalcService/add")

# 全量拓扑：服务名一行、函数行缩进两格
resp = YomkApi.server_info_all()
```

说明：期望消息类型由 `install_func()` 三参形式声明、四参附自定义函数说明（均仅作内省元数据，不参与运行时校验；说明经 `/function` 响应 data 段透出）；自省读的是运行期服务表，注册/删除即时反映到下一次查询。完整可运行演示见 `examples/ExampleYomkServerInfo.py`（四类查询、注册/删除实时视图、关闭后返回值契约）。

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
pip install cython readerwriterlock

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
pip install cython readerwriterlock

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
| ExampleYomkService.py | 服务定义与调用：Boot 三阶段编排、同步/异步请求、跨服务调用与失败传播（多文件工程结构） |
| ExampleYomkContext.py | 共享上下文：创建/读取/更新/销毁 + 状态检查器（拒绝/接受）+ 异步监控 |
| ExampleYomkFunctionPool.py | 动态函数池：注册/调用 + 同名热替换 |
| ExampleYomkEventLoop.py | 线程隔离事件循环：启动/非阻塞投递/阻塞投递/停止/销毁 |
| ExampleYomkLogger.py | 分级日志：控制台四级别与级别开关、日志代理（消费/放行）、文件日志创建/写入/落盘/回读/删除 |
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
├── boot/DemoBoot.py          # 启动编排（before -> start -> after 三阶段）
├── services/                 # 服务实现（AuditService / CalcService）
└── msgs/DemoMsgs.py          # 消息包定义
```

---

## 测试

一键测试脚本 `./test/run_tests.sh` 已就绪：

```bash
./test/run_tests.sh                      # 使用 $YOMK_PREFIX_PATH（缺省 /opt/yomk）
./test/run_tests.sh --prefix /opt/yomk   # 指定安装路径
./test/run_tests.sh --timeout 600        # 单测试超时秒数（默认 300）
```

说明：

- **当前 `test/` 暂无用例**：目录预留用于全面测试集合，无 `Test*.py` 时脚本提示并正常退出
- 将来补充测试后即插即用：自动探测 `lib/pythonX.Y/site-packages` 并设置 PYTHONPATH、依次运行 `Test*.py`、**失败即停**、`timeout` 超时保护、日志落盘 `test/test_logs/<时间戳>/`

---

## Python 项目集成

安装完成后 PYTHONPATH 已持久化（Linux 由 `scripts/install_env.sh` 写入 profile），业务代码直接导入即可使用：

```python
import YomkApi   # 门面 API：自动加载 Cython 扩展 YomkServerPy，核心类型（YomkService / YomkResponse / ResStatus 等）均可经 YomkApi 直接使用
```

- 新开任意终端免配置；未持久化的环境（如 CI、Windows）手动设置：`export PYTHONPATH=${YOMK_PREFIX_PATH}/lib/pythonX.Y/site-packages:$PYTHONPATH`
- `YomkServerPy.pyi` 随库安装于同一目录，IDE 可据此提供类型提示与补全
- 版本号经 `YomkApi.__version__` 查询（源于 CMakeLists 的 `project(VERSION)` 单一来源，与 `share/YomkServerPy/abi_report.txt` 同源）
