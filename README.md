# YomkServer

C++ Modular Fast Development Framework (C++17)

## 简介

YomkServer 是一个基于现代 C++17 标准构建的模块化、高性能服务开发框架。它采用"一切皆服务，一切皆请求"的设计理念，通过标准化的通信接口和灵活的模块机制，实现系统组件的高度解耦和动态组合。

- **进程内服务总线**：服务（`YomkService`）注册到服务器（`YomkServer`），通过 URL 格式 `"/服务名/功能函数名"` 进行同步或异步调用。
- **强类型化消息包**：业务数据通过 `YomkMsg` 及其派生类安全封装与传递，支持移动语义与动态类型转换。
- **开箱即用的内置能力**：框架初始化即自动装配日志系统、共享上下文、隔离事件循环、动态函数池与服务自省机制。
- **链式域入口**：六个域入口——`yomk::server()` / `yomk::log()` / `yomk::log_mgr()` / `yomk::ctx()` / `yomk::event_loop()` / `yomk::fn_pool()`——以"前置累积 + 无参终结"的链式形态暴露全部能力，屏蔽底层 URL 路由；API 是壳而非墙，需要时可直接取 `yomk::server().instance()` 裸调 `request()`。

### 模块清单

| 模块 | 简述 |
|---|---|
| [YomkServer / YomkService](docs/YomkServer.md) | 服务总线核心：服务注册与 `"/服务名/函数名"` URL 路由、同步/异步请求分发、服务生命周期管理（deinit / 优雅关闭） |
| [YomkMsg](docs/YomkMsg.md) | 强类型消息包：`YOMK_DEFINE_MSG` 宏声明自定义消息，内置 30 种标量/数组类型，支持移动语义与动态类型转换 |
| [YomkBoot](docs/YomkServer.md#启动编排yomkboot) | 启动编排：before / start / after 三段式引导，封装从框架初始化到业务就绪的完整流程（`yomk::server().boot_ptr(cfg).run()`） |
| [YomkLogger](docs/YomkLogger.md) | 日志系统：控制台与文件双输出、六级级别开关、自定义 tag、日志代理拦截、位置前缀开关与日志器自省 |
| [YomkContext](docs/YomkContext.md) | 共享上下文：跨服务强类型键值存储，内置变更前校验门控（Checker）与变更后监听（Monitor，同步/异步） |
| [YomkEventLoop](docs/YomkEventLoop.md) | 线程隔离事件循环：具名单线程 FIFO 执行队列，循环间彼此并行，支持停启续跑与队列积压观测 |
| [YomkFunctionPool](docs/YomkFunctionPool.md) | 动态函数池：跨服务扁平函数注册中心，同名重注册即运行时热替换，按名直接调用 |
| [YomkServerInfo](docs/YomkServerInfo.md) | 服务自省：运行期查询服务拓扑、功能函数清单与期望消息类型 |

核心层（YomkServer / YomkService / YomkMsg / YomkBoot）构成框架骨架；YomkLogger、YomkContext、YomkEventLoop、YomkFunctionPool、YomkServerInfo 均以内置服务形式随 `yomk::server().init()` 自动启动、开箱即用，能力分别经对应域入口链式调用。

---

## 快速上手

### 1. 最小完整示例

```cpp
#include <YomkServer/YomkServerApi.h>
#include <YomkServer/YomkLogApi.h>

// 自定义服务
class MyService : public YomkService
{
public:
    MyService(YomkServer *server) : YomkService(server)
    {
        name("/MyService"); // 设定服务名称（URL 前缀）
    }

    int init() override
    {
        // 注册功能函数；第三个参数可选，声明期望消息类型名供自省识别
        installFunc("/hello", &MyService::hello, "String");
        return 0; // 返回 0 表示服务初始化成功
    }

    // 功能函数统一签名：yomk::payload::Response(YomkMsgPtr pkg)
    yomk::payload::Response hello(YomkMsgPtr pkg)
    {
        auto msg = yomk::msgs::unpack<yomk::msgs::String>(pkg); // 类型校验与解包；不匹配时为 nullptr，需自行判空
        if (!msg)
        {
            return {yomk::payload::ResStatus::Failed, "pkg is not String"};
        }
        return {yomk::payload::ResStatus::Succeeded, "hello " + msg->d};
    }
};

int main()
{
    // 1. 初始化框架单例（自动启动内置服务：Logger、Context、EventLoop、FunctionPool、ServerInfo）
    yomk::server().init();

    // 2. 创建并注册自定义服务（名字省略则沿用构造函数内的 name）
    yomk::server().srv<MyService>().create();

    // 3. 同步请求：url/msg 累积，request() 终结
    yomk::payload::Response ret = yomk::server().url("/MyService/hello").msg(yomk::msgs::String::shared("yomk")).request();
    if (ret.m_status == yomk::payload::ResStatus::Succeeded)
    {
        yomk::log().info("Response received:", ret.m_msg);
    }

    // 4. 优雅关闭框架
    yomk::server().shutdown();
    return 0;
}
```

预期控制台输出（时间戳与行号按实际运行变化）：

```text
[2026-11-06 10:00:00.000] [INFO ] [main] [52] Response received: hello yomk
```

异步请求只需在链中插入 `.async(回调)` 分面，投递后立即返回、结果经回调送达：

```cpp
yomk::server().url("/MyService/hello").async([](yomk::payload::Response resp) {
    yomk::log().info("Async result:", resp.m_msg);
}).msg(yomk::msgs::String::shared("yomk")).request();
```

---

## 核心概念

以下概念贯穿框架各模块，便于快速建立整体认知；每个模块的完整链式语法、使用示例与注意事项见 [docs](docs/) 目录对应文档，逐个参数与返回码的精确规格以头文件 Doxygen 注释为准（`include/YomkServer/`）。

### 1. 链式调用形态（累积 → 终结）

所有域入口返回一个按值传递的 builder，参数方法（`url()`、`msg()`、`key()` 等）**累积**状态，无参动作（`request()`、`create()`、`get()` 等）**终结**并真正执行；分面方法（`async()`、`to_sink()`、`msg<T>()` 等）切换到子形态继续链。

```cpp
yomk::ctx().key("config").value(yomk::msgs::String::shared("v")).create();  // 累积 key/value → create() 终结
```

- builder 全程用 `auto` 接收，**仅限单表达式内联使用、禁止缓存复用**（内部持有 `string_view`，缓存即悬垂）。
- **未初始化守卫**：`init()` 之前调用任何终结动作不抛异常——返回 `ResStatus::Invalid`（或整型 `-1`、类型化读的调用方默认值）、异步请求静默丢弃，并打错误诊断日志；`version()` 除外，免初始化可用。

### 2. 请求与响应模型 (Request / Response)

- **URL 寻址**：格式为 `"/服务名/函数名"`，如 `"/MyService/hello"`；内置服务 URL 常量见 `yomk::url` 命名空间。
- **返回值 `yomk::payload::Response`**：`m_status`（`ResStatus::Succeeded (0)` 调用成功；`ResStatus::Failed (1)` 服务端执行失败，含服务/函数不存在等 not-found；`ResStatus::Invalid (-1)` 入参非法或服务器未初始化）、`m_msg` 状态描述文本、`m_data` 可选负载消息包。
- **同步调用** `.request()` 阻塞返回完整应答；`.status()` 只保留三态收敛（细节进诊断日志）；**异步调用** `.async(func).request()` 投递到框架内置线程池并发执行，不阻塞当前线程，`func` 传 `nullptr` 即只投递不取回。

详见 [服务总线](docs/YomkServer.md)。

### 3. 消息包 (YomkMsg)

服务与事件循环间传递强类型数据：`YOMK_DEFINE_MSG(DataType, MsgName, VarName)` 宏声明自定义消息（生成类位于 `yomk::msgs`，载荷成员名约定为 `d`），`yomk::msgs::消息名::shared(...)` 构造消息包，`yomk::msgs::unpack<yomk::msgs::消息名>(pkg)` 解包模板做类型校验（pkg 为空或类型名不匹配时返回 `nullptr`，失败处理由调用方完成）；内置 30 种标量/数组类型。

详见 [消息包](docs/YomkMsg.md)。

### 4. 日志系统 (YomkLogger)

发射与管理分域：`yomk::log().info(...)` 等六级发射（控制台），`yomk::log().to_sink(name).info(...)` 写具名文件日志器；`yomk::log_mgr()` 承担级别两级门控（全局主闸 × 逐器）、控制台/文件代理拦截、位置前缀开关与日志器自省。

详见 [日志系统](docs/YomkLogger.md)。

### 5. 共享上下文 (YomkContext)

跨服务强类型键值存储：`yomk::ctx().key(k).value(pkg).create()/set()` 写、`.key(k).msg<T>().with_default(d).get()` 类型化读；内置变更前校验门控（Checker，`CheckStatus::Accept` 放行 / `Reject` 拦截）与变更后监听（Monitor，同步/异步），支持分组与运行期内省。

详见 [共享上下文](docs/YomkContext.md)。

### 6. 线程隔离事件循环 (YomkEventLoop)

具名单线程 FIFO 执行队列：`yomk::event_loop().loop(name).start()` 启动，`.msg(data).loop(name).post()` 非阻塞投递 / `.post_wait()` 阻塞投递；同一循环内事件按入队顺序在专用线程中执行，不同循环之间彼此并行；支持停止后续跑与队列积压观测。

详见 [事件循环](docs/YomkEventLoop.md)。

### 7. 动态函数池 (YomkFunctionPool)

跨服务扁平函数注册中心：`yomk::fn_pool().fn_name(name).fn(func).install()` 注册、`.fn_name(name).msg(pkg).call()` 调用，同名重注册即运行时热替换，无需注销。

详见 [函数池](docs/YomkFunctionPool.md)。

### 8. 运行期内省 (YomkServerInfo)

`yomk::server().services()` / `.srv_name(name).functions()` / `.fn_url(url).get_info()` / `.all()` 查询服务拓扑、功能函数清单与期望消息类型；Logger / Context / EventLoop / FunctionPool 亦各有模块级内省链（各域的 `list()` / `get_info()` / `all()` 家族）。

详见 [服务自省](docs/YomkServerInfo.md)。

### 9. 服务生命周期与弱绑定

- **deinit() 的四个触发时机**：`yomk::server().srv_name("/MyService").remove()` 删除单个服务、`yomk::server().shutdown()` 关闭全部服务、同名服务被再次 `add()` 替换（旧服务先 deinit 再装新服务）、忘记关闭时进程退出经 atexit 兜底；覆写 `virtual void deinit()` 用于停止线程、注销外部资源。
- **弱绑定（防悬垂回调）**：服务成员函数注册到**外部子系统**（函数池 / 事件循环 / Context checker·monitor / 异步响应回调）时必须用 `weakFunc(&Class::Method)` 弱绑定——服务删除后回调自动判活失效；`installFunc("/x", &Class::Method)` 注册到本服务 funcMap 已自动弱绑定，无需手动处理。

详见 [服务总线](docs/YomkServer.md)。

---

## 编译与安装

### Linux

#### 方式 1：自动安装脚本（推荐）

```bash
./build_ubuntu.sh
```

脚本将自动检查依赖并交互式完成配置、编译、安装（默认安装路径 `/opt/yomk`），并设置环境变量 `YOMK_PREFIX_PATH`。

#### 方式 2：手动 CMake 构建

与方式 1 产生完全一致的安装结果（同一套 install 规则 + 同一份安装后配置脚本）：

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/opt/yomk
cmake --build . -j
sudo cmake --install .
cd ..
./scripts/install_env.sh /opt/yomk   # 安装校验 + YOMK_PREFIX_PATH 持久化 + 动态库搜索路径注册（与方式 1 共用）
```

说明：

- `-DCMAKE_BUILD_TYPE=Release` 必须显式传入：Linux Makefiles 单配置生成器不识别 `--config Release`，缺失时将以无优化参数编译，产物与方式 1 不同
- 构建目录使用 `build`（如上命令所示）：示例程序的 GNU build-id 与构建目录路径相关，两种方式使用同一 `<仓库>/build` 时产物字节级一致；核心库 `libYomkServer.so` 不受构建目录影响
- 请勿设置 `$CXX` 环境变量：方式 1 固定选用 g++（手动构建在未设置时同样自动预选 g++），设置了 `$CXX` 的产物与方式 1 存在 ABI 分叉风险；可比对安装产物 `share/YomkServer/abi_report.txt` 确认编译环境一致
- `abi_report.txt` 的"生成时间"行每次构建必然不同，属预期差异；其余内容两种方式完全一致

#### 编译内部单元测试（可选）

默认仅编译核心库与 Examples 示例程序。如需编译内部测试，见下文[测试](#测试)章节。

### Windows

```powershell
mkdir build
cd build
cmake .. -DCMAKE_INSTALL_PREFIX="C:/YomkServer/install"
cmake --build . --target install --config Release
```

---

## 测试

### 1. 编译测试

默认仅编译核心库与 Examples，测试需显式开启：

```bash
mkdir build && cd build
cmake .. -DYOMK_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build . -j
```

构建产物为 27 个测试可执行（输出到 `<仓库根>/bin/`），按模块划分：

| 模块 | 测试目标 |
|---|---|
| YomkServer (11) | TestYomkSimpleThreadPool、TestYomkWeakFunc、TestYomkAPINotInit、TestYomkAPILifecycle、TestYomkBoot、TestYomkServer、TestYomkService、TestYomkShutdownSeq、TestYomkConcurrency、TestYomkMsg、TestYomkIntegration |
| YomkServerInfo (1) | TestYomkServerInfoLifecycle |
| FunctionPool (3) | TestYomkFunctionPoolLifecycle、TestYomkFunctionPoolConcurrency、TestYomkFunctionPoolStress |
| Context (4) | TestYomkContextCRUD、TestYomkContextCheckerMonitor、TestYomkContextLifecycle、TestYomkContextConcurrency |
| EventLoop (3) | TestYomkEventLoopLifecycle、TestYomkEventLoopConcurrency、TestYomkEventLoopStress |
| Logger (5) | TestYomkLoggerLifecycle、TestYomkLoggerDirect、TestYomkLoggerLoc、TestYomkLoggerConcurrency、TestYomkLoggerStress |

### 2. 一键运行全量测试

```bash
./test/run_tests.sh              # 快速模式：压测规模缩为 10000，约 10s
./test/run_tests.sh --full       # 完整规模（10 万级投递）
./test/run_tests.sh --bin DIR    # 指定测试可执行目录（默认 <仓库根>/bin）
./test/run_tests.sh --timeout N  # 单测试超时秒数（默认 300，--full 默认 900）
```

运行器行为：

- **失败即停**：任一测试退出码非 0（含超时被杀）立即终止，终端输出该测试的 `[FAIL]` 用例摘要与完整日志路径
- **日志落盘**：`test/test_logs/<时间戳>/` 下每个测试一份日志 + `summary.log` 汇总
- **现场清理**：每个测试在独立临时目录运行（隔离 CWD，防意外写文件污染仓库）；测试临时产物统一落 `test/test_logs/tmp`（经 `YOMK_TEST_TMP_ROOT` 注入测试进程，独立单独运行时回退 `/tmp`），启动时整目录重建（兜底上次崩溃遗留）、退出时整目录删除（含失败/中断/超时路径，经 EXIT trap 兜底）
- **超时保护**：每个测试由 `timeout` 包裹，防卡死

### 3. 单独运行与压测规模

每个测试为独立可执行（纯 `main()` + `CHECK` 断言，返回 0 = 全部通过，非 0 = 存在失败用例），可直接单独运行：

```bash
./bin/TestYomkLoggerLifecycle
```

3 个 stress 测试（EventLoop / FunctionPool / Logger）规模由环境变量 `YOMK_TEST_STRESS_SCALE` 参数化（缺省 100000，下限 1000）；`run_tests.sh` 快速模式自动设为 10000，`--full` 用缺省值。单独运行时可自行指定：

```bash
YOMK_TEST_STRESS_SCALE=50000 ./bin/TestYomkEventLoopStress
```

### 4. 一键静态代码检查

cppcheck + clang-tidy 双工具零告警门禁（任一告警即非零退出），全量扫描 `src`（含全部 Modules）与 `include`：

```bash
./test/run_static_checks.sh              # 默认双跑
./test/run_static_checks.sh --cppcheck   # 仅跑 cppcheck
./test/run_static_checks.sh --tidy       # 仅跑 clang-tidy
```

- **日志落盘**：`test/test_logs/<时间戳>/` 下每次运行一份 `cppcheck.log` + `clang-tidy.log` + `summary.log` 汇总

依赖：cppcheck、clang-tidy，以及仓库根 `compile_commands.json`（CMake 配置阶段自动导出，缺失时脚本会给出构建提示）。检查集与 `test/yomk_server/CMakeLists.txt` 的 `YOMK_TEST_STATIC_ANALYZE` CMake 目标口径一致，可配合 `cmake .. -DYOMK_TEST_STATIC_ANALYZE=ON` 在构建侧运行同名 target。

---

## CMake 项目集成

在下游业务项目的 `CMakeLists.txt` 中引入：

```cmake
find_package(YomkServer REQUIRED)

message(STATUS "YomkServer version: ${YomkServer_VERSION}")
message(STATUS "YomkServer include dirs: ${YomkServer_INCLUDE_DIRS}")
message(STATUS "YomkServer libraries: ${YomkServer_LIBRARIES}")

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE YomkServer::YomkServer)
```

在源码中按模块引用公共头文件（不提供伞形聚合头，按需导入）：

```cpp
#include <YomkServer/YomkServerApi.h>     // 服务总线：yomk::server()
#include <YomkServer/YomkLogApi.h>        // 日志：yomk::log() / yomk::log_mgr()
#include <YomkServer/YomkContextApi.h>    // 共享上下文：yomk::ctx()
#include <YomkServer/YomkEventLoopApi.h>  // 事件循环：yomk::event_loop()
#include <YomkServer/YomkFunctionPoolApi.h> // 函数池：yomk::fn_pool()
```
