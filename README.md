# YomkServer

C++ Modular Fast Development Framework (C++17)

## 简介

YomkServer 是一个基于现代 C++17 标准构建的模块化、高性能服务开发框架。它采用"一切皆服务，一切皆请求"的设计理念，通过标准化的通信接口和灵活的模块机制，实现系统组件的高度解耦和动态组合。

- **进程内服务总线**：服务（`YomkService`）注册到服务器（`YomkServer`），通过 URL 格式 `"/服务名/功能函数名"` 进行同步或异步调用。
- **强类型化消息包**：业务数据通过 `YomkPkg` 及其派生类安全封装与传递，支持移动语义与动态类型转换。
- **开箱即用内置能力**：框架初始化即自动装配日志系统、共享上下文、隔离事件循环、动态函数池以及服务自省机制。

### 模块清单

| 模块 | 简述 |
|---|---|
| [YomkServer / YomkService](Docs/YomkServer.md) | 服务总线核心：服务注册与 `"/服务名/函数名"` URL 路由、同步/异步请求分发、服务生命周期管理（deinit / 优雅关闭） |
| [YomkPkg](Docs/YomkPkg.md) | 强类型消息包：`YomkMsg` 宏声明自定义消息，内置 30 种标量/数组类型，支持移动语义与动态类型转换 |
| [YomkBoot](Docs/YomkServer.md) | 启动编排：before / start / after 三段式引导，封装从框架初始化到业务就绪的完整流程 |
| [YomkLogger](Docs/YomkLogger.md) | 日志系统：控制台与文件双输出、四级级别开关、自定义 tag、日志代理拦截与日志器自省 |
| [YomkContext](Docs/YomkContext.md) | 共享上下文：跨服务强类型键值存储，内置变更前校验门控（Checker）与变更后监听（Monitor，同步/异步） |
| [YomkEventLoop](Docs/YomkEventLoop.md) | 线程隔离事件循环：具名单线程 FIFO 执行队列，循环间彼此并行，支持停启续跑与队列积压观测 |
| [YomkFunctionPool](Docs/YomkFunctionPool.md) | 动态函数池：跨服务扁平函数注册中心，同名重注册即运行时热替换，按名直接调用 |
| [YomkServerInfo](Docs/YomkServerInfo.md) | 服务自省：运行期查询服务拓扑、功能函数清单与期望消息类型 |

核心层（YomkServer / YomkService / YomkPkg / YomkBoot）构成框架骨架；YomkLogger、YomkContext、YomkEventLoop、YomkFunctionPool、YomkServerInfo 均以内置服务形式随 `YOMK_INIT()` 自动启动、开箱即用，全部能力经 `YOMK_*` 宏统一调用。

---

## 快速上手

### 1. 最小完整示例

```cpp
#include <YomkServer/YomkAPI.h>
#include <iostream>

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
        // 注册功能函数，第三个参数可选提供期望消息类型名称供自省识别
        YomkInstallFunc("/hello", MyService::hello, String);
        return 0; // 返回 0 表示服务初始化成功
    }

    // 功能函数统一签名：YomkResponse(YomkPkgPtr pkg)
    YomkResponse hello(YomkPkgPtr pkg)
    {
        YomkUnPackPkg(pkg, String, msg); // 类型校验与解包
        if (!msg)
        {
            return {YomkResponse::eNo, "pkg is not String"};
        }
        return {YomkResponse::eOk, "hello " + msg->d};
    }
};

int main()
{
    // 1. 初始化框架单例（自动启动内置服务：Logger, Context, EventLoop, FunctionPool, ServerInfo）
    YOMK_INIT();

    // 2. 注册自定义服务
    YOMK_NEW_SERVICE(MyService);

    // 3. 同步请求服务
    YomkResponse ret = YOMK_REQUEST("/MyService/hello", YomkMkPtr(String, "yomk"));
    if (ret.m_status == YomkResponse::eOk)
    {
        YOMK_INFO("Response received:", ret.m_msg);
    }

    // 4. 优雅关闭框架
    YOMK_SHUTDOWN();
    return 0;
}
```

---

## 核心概念

以下概念贯穿框架各模块，便于快速建立整体认知；每个模块的完整 API 速查、使用示例与注意事项见 [Docs](Docs/) 目录对应文档。

### 1. 请求与响应模型 (Request / Response)

所有服务间交互均遵循统一的请求响应抽象：

- **URL 寻址**：格式为 `"/服务名/函数名"`，如 `"/MyService/hello"`。
- **返回值 `YomkResponse`**：`m_status`（`eOk (0)` 调用成功；`eNo (1)` 目标服务或函数不存在、被拒绝或条件不满足；`eInvalid (-1)` 入参非法或服务器未初始化）、`m_msg` 状态描述文本、`m_data` 可选负载消息包。
- **同步调用** `YOMK_REQUEST` 阻塞返回；**异步调用** `YOMK_ASYNC_REQUEST` 投递到框架内置线程池并发执行，不阻塞当前线程。

详见 [服务总线](Docs/YomkServer.md)。

### 2. 消息包 (YomkPkg)

服务与事件循环间传递强类型数据：`YomkMsg(DataType, MsgName, VarName)` 宏声明自定义消息，`YomkMkPtr` 构造消息包，三种解包宏做类型校验（`YomkUnPackPkg` 失败置空指针 / `YomkUnPackPkgResponse` 失败返回 eNo / `YomkUnPackPkgVoid` 失败直接 return）；内置 30 种标量/数组类型。

详见 [消息包](Docs/YomkPkg.md)。

### 3. 日志系统 (YomkLogger)

控制台与文件双输出、四级（INFO / WARN / ERROR / DEBUG）级别开关、自定义 tag、控制台日志代理拦截；`YOMK_INFO`、`YOMK_FILE_LOG_CREATE` 等宏开箱即用。

详见 [日志系统](Docs/YomkLogger.md)。

### 4. 共享上下文 (YomkContext)

跨服务强类型键值存储：创建/读取/设置/销毁，内置变更前校验门控（Checker，支持 eAccept / eReject 拦截）与变更后监听（Monitor，同步/异步），支持运行期自省。

详见 [共享上下文](Docs/YomkContext.md)。

### 5. 线程隔离事件循环 (YomkEventLoop)

具名单线程 FIFO 执行队列：同一循环内事件按入队顺序在专用线程中执行，不同循环之间彼此并行；支持阻塞/非阻塞投递、停止后续跑与队列积压观测。

详见 [事件循环](Docs/YomkEventLoop.md)。

### 6. 动态函数池 (YomkFunctionPool)

跨服务扁平函数注册中心：按名注册与调用，同名重注册即运行时热替换，无需注销。

详见 [函数池](Docs/YomkFunctionPool.md)。

### 7. 运行期自省 (YomkServerInfo)

运行期查询服务拓扑、功能函数清单与期望消息类型；Logger / Context / EventLoop / FunctionPool 亦各有模块级自省 API（`YOMK_*_INFO_*`）。

详见 [服务自省](Docs/YomkServerInfo.md)。

### 8. 服务生命周期与弱绑定

- **deinit() 的四个触发时机**：`YOMK_DEL_SERVICE` 删除单个服务、`YOMK_SHUTDOWN` 关闭全部服务、同名服务被 `YOMK_ADD_SERVICE` 替换（旧服务先 deinit 再装新服务）、忘记关闭时框架析构兜底；覆写 `virtual void deinit()` 用于停止线程、注销外部资源。
- **弱绑定（防悬垂回调）**：服务成员函数注册到**外部子系统**（FunctionPool / EventLoop / Context checker·monitor / 异步响应回调）时必须用 `YomkBindWeakSelf` 弱绑定——服务删除后回调自动判活失效；`YomkInstallFunc` 注册到本服务 funcMap 已自动弱绑定，无需手动处理。

详见 [服务总线](Docs/YomkServer.md)。

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

构建产物为 23 个测试可执行（输出到 `<仓库根>/bin/`），按模块划分：

| 模块 | 测试目标 |
|---|---|
| YomkServer (9) | TestYomkSimpleThreadPool、TestYomkWeakFunc、TestYomkAPINotInit、TestYomkAPILifecycle、TestYomkBoot、TestYomkServer、TestYomkService、TestYomkShutdownSeq、TestYomkConcurrency |
| FunctionPool (3) | TestYomkFunctionPoolLifecycle、TestYomkFunctionPoolConcurrency、TestYomkFunctionPoolStress |
| Context (4) | TestYomkContextCRUD、TestYomkContextCheckerMonitor、TestYomkContextLifecycle、TestYomkContextConcurrency |
| EventLoop (3) | TestYomkEventLoopLifecycle、TestYomkEventLoopConcurrency、TestYomkEventLoopStress |
| Logger (4) | TestYomkLoggerLifecycle、TestYomkLoggerDirect、TestYomkLoggerConcurrency、TestYomkLoggerStress |

### 2. 一键运行全量测试

```bash
./Test/run_tests.sh              # 快速模式：压测规模缩为 10000，约 10s
./Test/run_tests.sh --full       # 完整规模（10 万级投递）
./Test/run_tests.sh --bin DIR    # 指定测试可执行目录（默认 <仓库根>/bin）
./Test/run_tests.sh --timeout N  # 单测试超时秒数（默认 300，--full 默认 900）
```

运行器行为：

- **失败即停**：任一测试退出码非 0（含超时被杀）立即终止，终端输出该测试的 `[FAIL]` 用例摘要与完整日志路径
- **日志落盘**：`Test/test_logs/<时间戳>/` 下每个测试一份日志 + `summary.log` 汇总
- **现场清理**：每个测试在独立临时目录运行（隔离 CWD，防意外写文件污染仓库）；运行前清理上次遗留的 `/tmp/yomk_logger_*`，结束后复查残留，发现则清理并判定整体失败
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

cppcheck + clang-tidy 双工具零告警门禁（任一告警即非零退出），全量扫描 `YomkServer/src`（含全部 Modules）与 `YomkServer/include`：

```bash
./Test/run_static_checks.sh              # 默认双跑
./Test/run_static_checks.sh --cppcheck   # 仅跑 cppcheck
./Test/run_static_checks.sh --tidy       # 仅跑 clang-tidy
```

- **日志落盘**：`Test/test_logs/<时间戳>/` 下每次运行一份 `cppcheck.log` + `clang-tidy.log` + `summary.log` 汇总

依赖：cppcheck、clang-tidy，以及仓库根 `compile_commands.json`（CMake 配置阶段自动导出，缺失时脚本会给出构建提示）。检查集与 `Test/YomkServer/CMakeLists.txt` 的 `YOMK_TEST_STATIC_ANALYZE` CMake 目标口径一致，可配合 `cmake .. -DYOMK_TEST_STATIC_ANALYZE=ON` 在构建侧运行同名 target。

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

在源码中引用公共头文件：

```cpp
#include <YomkServer/YomkAPI.h>
```
