# YomkServer

C++ Modular Fast Development Framework (C++17)

## 简介

YomkServer 是一个基于现代 C++17 标准构建的模块化、高性能服务开发框架。它采用"一切皆服务，一切皆请求"的设计理念，通过标准化的通信接口和灵活的模块机制，实现系统组件的高度解耦和动态组合。

- **进程内服务总线**：服务（`YomkService`）注册到服务器（`YomkServer`），通过 URL 格式 `"/服务名/功能函数名"` 进行同步或异步调用。
- **强类型化消息包**：业务数据通过 `YomkPkg` 及其派生类安全封装与传递，支持移动语义与动态类型转换。
- **开箱即用内置能力**：框架初始化即自动装配日志系统、共享上下文、隔离事件循环、动态函数池以及服务自省机制。

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

## 核心概念与使用文档

### 1. 请求与响应模型 (Request / Response)

所有服务间交互均遵循统一的请求响应抽象：

- **URL 寻址**：格式为 `"/服务名/函数名"`，如 `"/MyService/hello"`。
- **返回值 `YomkResponse`**：
  - `m_status`：
    - `eOk (0)`：调用成功。
    - `eNo (1)`：目标服务或函数不存在、被拒绝或条件不满足。
    - `eInvalid (-1)`：入参非法或服务器未初始化。
  - `m_msg`：状态描述文本。
  - `m_data`：可选的负载消息包（`YomkPkgPtr`）。
- **同步调用**：
  ```cpp
  YomkResponse resp = YOMK_REQUEST("/服务名/函数名", pkg);
  ```
- **异步调用**：投递到框架内置线程池并发执行，不阻塞当前线程：
  ```cpp
  YOMK_ASYNC_REQUEST("/服务名/函数名", pkg, [](YomkResponse resp) {
      // 异步回调逻辑
  });
  ```

### 2. 消息包 (YomkPkg)

用于在服务与事件循环间传递强类型数据。

- **声明自定义消息**：使用 `YomkMsg(DataType, MsgName, VarName)` 宏（建议放置在命名空间之外）：
  ```cpp
  struct UserInfo {
      int id;
      std::string name;
  };
  YomkMsg(UserInfo, UserInfoMsg, data);
  ```
- **构造与传递消息包**：
  ```cpp
  // 创建 shared_ptr 包装的消息包
  auto pkg = YomkMkPtr(UserInfoMsg, UserInfo{1001, "Alice"});
  ```
- **解包宏**：
  - `YomkUnPackPkg(pkg, MsgName, ptr)`：解包失败时指针置为 `nullptr`。
  - `YomkUnPackPkgResponse(pkg, MsgName, ptr)`：类型不匹配时直接以 `YomkResponse::eNo` 从当前函数返回。
  - `YomkUnPackPkgVoid(pkg, MsgName, ptr)`：类型不匹配时直接 `return`（适用于 `void` 回调）。

### 3. 日志系统 (YomkLogger)

框架内置了开箱即用的控制台与文件日志输出：

- **控制台日志**：
  ```cpp
  YOMK_INFO("server port:", 8080);
  YOMK_WARN("resource running low");
  YOMK_ERROR("failed to connect");
  YOMK_DEBUG("trace id:", 12345);
  // 指定 Tag：
  YOMK_INFO_TAG("Network", "connected to remote host");
  ```
- **开关指定日志级别**：
  ```cpp
  YOMK_OFF_CONSOLE_LOG_DEBUG();
  YOMK_ON_CONSOLE_LOG_DEBUG();
  ```
- **文件日志**：
  ```cpp
  YOMK_FILE_LOG_CREATE("./logs", "app"); // 创建日志器
  YOMK_FILE_INFO("app", "app started");  // 写入内存缓冲
  YOMK_FILE_LOG_WRITE("app");            // 显式刷盘（日志器释放时亦会自动落盘）
  ```
- **控制台日志拦截/代理**：
  ```cpp
  YOMK_SET_CONSOLE_LOG_PROXY([](const yomk::Log &log) {
      // 返回 false 表示消费此日志不再默认输出；返回 true 则继续走框架默认打印
      return false;
  });
  ```

### 4. 共享上下文 (YomkContext)

支持跨服务存储与共享强类型键值状态，内置安全校验门控（Checker）与变更监控（Monitor）：

```cpp
// 1. 创建与设置上下文键值
YOMK_CONTEXT_CREATE("config", YomkMkPtr(String, "init_value"));
YOMK_CONTEXT_SET("config", YomkMkPtr(String, "new_value"));

// 2. 读取上下文（只读快照）
auto val = YOMK_CONTEXT_GET(String, "config", nullptr);

// 3. 注册变更前校验门控（Checker：在写锁内门控，重入会死锁）
YOMK_CONTEXT_SET_CHECKER("config", [](const yomk::Context &ctx) {
    // 返回 eAccept 放行修改，返回 eReject 拦截并拒绝写入
    return yomk::ContextChecker::eAccept;
});
YOMK_CONTEXT_ON_CHECKER(); // 开启全局校验器

// 4. 注册变更后监听器（Monitor）
YOMK_CONTEXT_SET_MONITOR("config", [](const yomk::Context &ctx) {
    // 收到变更通知（支持同步通知或异步通知池）
}, /*async=*/false);
YOMK_CONTEXT_ON_MONITOR(); // 开启全局监控
```

### 5. 线程隔离事件循环 (YomkEventLoop)

为特定业务提供单线程 FIFO 执行队列，避免锁竞争：

```cpp
// 启动事件循环
YOMK_EVENTLOOP_START("TaskLoop");

// 非阻塞投递异步任务
YOMK_EVENTLOOP_POST("TaskLoop", YomkMkPtr(String, "task1"), [](YomkPkgPtr pkg) {
    // 在 TaskLoop 专用线程中顺序执行
    return YomkResponse{YomkResponse::eOk};
});

// 阻塞投递任务（等待任务在循环中执行完毕返回结果）
YomkResponse resp = YOMK_EVENTLOOP_POST_WAIT("TaskLoop", nullptr, [](YomkPkgPtr) {
    return YomkResponse{YomkResponse::eOk, "done"};
});

// 停止与销毁
YOMK_EVENTLOOP_STOP("TaskLoop");
YOMK_EVENTLOOP_DESTROY("TaskLoop");
```

### 6. 动态函数池 (YomkFunctionPool)

用于跨服务、动态注册与调用的扁平函数中心：

```cpp
// 注册函数
YOMK_FUNCTIONPOOL_REGISTER("calcSum", [](YomkPkgPtr pkg) {
    return YomkResponse{YomkResponse::eOk, "result"};
});

// 调用函数
YomkResponse resp = YOMK_FUNCTIONPOOL_CALL("calcSum", nullptr);
```

### 7. 运行期自省 API

框架提供内置自省接口，方便在运行时查询拓扑和调试：

- **服务自省**：`YOMK_SERVER_INFO_SERVICES()`、`YOMK_SERVER_INFO_FUNCTIONS("/MyService")`、`YOMK_SERVER_INFO_ALL()`
- **日志自省**：`YOMK_LOGGER_INFO_LOGGERS()`、`YOMK_LOGGER_INFO_ALL()`
- **上下文自省**：`YOMK_CONTEXT_INFO_KEYS()`、`YOMK_CONTEXT_INFO_ALL()`
- **事件循环自省**：`YOMK_EVENTLOOP_INFO_LOOPS()`、`YOMK_EVENTLOOP_INFO_ALL()`
- **函数池自省**：`YOMK_FUNCTIONPOOL_INFO_NAMES()`、`YOMK_FUNCTIONPOOL_INFO_ALL()`

---

## 编译与安装

### Linux

#### 方式 1：自动安装脚本（推荐）

```bash
./build_ubuntu.sh
```

脚本将自动检查依赖并交互式完成配置、编译、安装（默认安装路径 `/opt/yomk`），并设置环境变量 `YOMK_PREFIX_PATH`。

#### 方式 2：手动 CMake 构建

```bash
mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=~/YomkServer/install
cmake --build . --target install --config Release
```

#### 编译内部单元测试（可选）

默认仅编译核心库与 Examples 示例程序。如需编译内部测试：
```bash
cmake .. -DYOMK_BUILD_TESTS=ON
```

### Windows

```powershell
mkdir build
cd build
cmake .. -DCMAKE_INSTALL_PREFIX="C:/Users/solit/Env/YomkServer/install"
cmake --build . --target install --config Release
```

---

## CMake 项目集成

在下游业务项目的 `CMakeLists.txt` 中引入：

```cmake
find_package(YomkServer REQUIRED)

message(STATUS "YomkServer version: ${YomkServer_VERSION}")
message(STATUS "YomkServer include dirs: ${YomkServer_INCLUDE_DIRS}")
message(STATUS "YomkServer libraries: ${YomkServer_LIBRARIES}")

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE YomkServer)
```

在源码中引用公共头文件：

```cpp
#include <YomkServer/YomkAPI.h>
```
