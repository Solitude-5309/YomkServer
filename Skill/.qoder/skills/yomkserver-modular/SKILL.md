---
name: yomkserver-modular
description: 基于YomkServer框架的模块化C++17工程编程。核心理念"一切皆服务，一切皆请求"。用于创建YomkService功能模块、使用YomkContext全局状态管理、YomkEventLoop事件循环、YomkFunctionPool公共函数池和YomkLogger日志系统。当用户需要编写YomkServer模块化代码、创建服务、管理全局状态、处理事件队列、注册公共函数或搭建工程结构时使用。
---

# YomkServer 模块化编程框架

## 框架概述

YomkServer 是基于 C++17 的模块化高性能服务开发框架，核心设计理念：**「一切皆服务，一切皆请求」**。
通过标准化的 Request/Response 通信接口和灵活的模块机制，实现系统组件的高度解耦和动态组合。

### 设计哲学

1. **关注点分离**：每个服务专注于单一职责
2. **约定优于配置**：合理的默认值减少配置复杂度
3. **渐进式复杂度**：从简单单体到复杂系统的平滑演进
4. **开发者友好**：直观的API设计，开箱即用的基础组件

### 两级模块化模型

| 层级 | 概念 | 说明 |
|------|------|------|
| **Service层** | YomkService | 基础服务模块，封装独立业务域或技术组件 |
| **Function层** | Function | 服务内具体功能单元，通过唯一URL路径标识和访问 |

### 基础服务组件

| 组件 | 职责 | 核心特性 |
|------|------|----------|
| **YomkServer** | 服务容器，管理所有服务的生命周期 | 程序入口初始化 |
| **YomkService** | 功能模块单元，注册功能函数 | 高内聚、松耦合、支持独立扩展 |
| **YomkContext** | 全局K-V状态机 | 状态安全检查(防非法迁移)、变更监控、全生命周期管理 |
| **YomkEventLoop** | 线程隔离的事件循环 | 独立线程运行、同循环内顺序执行、不同循环间并行、支持非阻塞/阻塞投递 |
| **YomkFunctionPool** | 动态函数池 | 统一注册调度、支持运行时注册/更新/热替换、面向过程开发范式 |
| **YomkLogger** | 可配置日志系统 | 多级别(INFO/DEBUG/WARN/ERROR)、多输出(文件+控制台) |

所有组件通过统一的 **Request/Response 模型** 通信，URL格式：`/服务名/功能函数名`

## 工程构建与链接

### CMake 链接
```cmake
find_package(YomkServer REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE YomkServer)
```

### 头文件引入
```cpp
#include <YomkServer/YomkAPI.h>  // 唯一需要引入的头文件
using namespace yomk;             // 框架命名空间
```

### 编译安装
```bash
mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=~/YomkServer/install
cmake --build . --target install --config Release
```

## 编程规范

### 1. 消息包定义（YomkMsg）

每个服务间通信的数据结构必须用 `YomkMsg` 宏注册（3个参数：自定义数据类、消息名称、数据成员变量名）：

```cpp
// 1. 定义普通结构体
struct MyData {
    std::string field1;
    int field2;
};
// 2. 用YomkMsg宏注册为消息包（在命名空间外）
// 参数：自定义数据类, 消息名称（用于框架类型识别和映射）, 数据成员变量名
YomkMsg(MyData, MyData, d)
// 消息名称为 MyData，访问: ptr->d.field1

// 也可以自定义消息名称和成员名：
// YomkMsg(MyData, YMyData, msg)  → 消息名称为 YMyData，访问: ptr->msg.field1
```

注册后可用（**辅助宏均使用消息名称**）：
- `YomkMkPtr(消息名称, 数据类实例)` — 创建消息包，如 `YomkMkPtr(YMyData, MyData{"val", 42})`
- `YomkUnPackPkgResponse(pkg, 消息名称, ptr)` — 解包（函数返回Response时使用，**宏已自动判空，失败自动返回eNo**）
- `YomkUnPackPkgVoid(pkg, 消息名称, ptr)` — 解包（void函数时使用，**宏已自动判空，失败自动return**）
- `YomkUnPackPkg(pkg, 消息名称, ptr)` — 解包（**不自动return**，ptr为nullptr需手动检查）

框架内置标准类型消息包（成员名均为 `d`）：
- `String`(std::string), `Bool`, `Int32`, `Int64`, `Uint32`, `Uint64`, `Float32`, `Float64` 等
- 对应数组类型：`StringArray`, `BoolArray`, `Int32Array` 等

### 2. YomkService 编写模板

```cpp
class MyService : public YomkService
{
public:
    MyService(YomkServer* server) : YomkService(server) {
        name("/MyService");  // 服务名必须唯一，以/开头
    }
    virtual ~MyService() {}

    virtual int init() {
        // 安装功能函数
        YomkInstallFunc("/my_func", MyService::myFunc);
        YOMK_INFO_TAG("MyService::init", "install func [ /my_func ] to", name());
        return 0;
    }

private:
    YomkResponse myFunc(YomkPkgPtr pkg) {
        // 1. 解包（宏已自动判空，失败自动返回 eNo）
        // 注意：这里使用消息名称 YMyData，而非数据类名 MyData
        YomkUnPackPkgResponse(pkg, YMyData, data);
        // 2. 业务逻辑
        // ...
        // 3. 返回结果
        return YomkResponse(YomkResponse::eOk, "success");
    }
};
```

### 3. 程序初始化模板

**方式一：直接初始化（简单场景）**
```cpp
int main(int argc, char* argv[]) {
    // 初始化服务器，自动启动全部内置服务
    YOMK_INIT();

    // 注册自定义服务
    YOMK_NEW_SERVICE(MyService, "/MyService");

    // 同步请求（注意：YomkMkPtr 第一个参数是消息名称，第二个是数据类实例）
    YomkResponse resp = YOMK_REQUEST("/MyService/my_func", YomkMkPtr(YMyData, MyData{"hello", 1}));

    // 异步请求
    YOMK_ASYNC_REQUEST("/MyService/my_func", YomkMkPtr(YMyData, MyData{"hello", 1}), [](YomkResponse resp) {
        // 回调处理
    });

    getchar();
    return 0;
}
```

**方式二：YomkBoot 生命周期管理（推荐，复杂场景）**
```cpp
// 1. 定义 Boot 类
class MyBoot : public YomkBoot {
public:
    MyBoot(const std::vector<std::string>& srvNames) : m_srvNames(srvNames) {}
    int before() override;  // 服务启动前：创建Context、EventLoop、注册FunctionPool等
    int start() override;   // 注册并启动服务
    int after() override;   // 服务启动后：调用服务接口做初始化
private:
    std::vector<std::string> m_srvNames;
};

// 2. 实现 start()：使用 YOMK_ADD_SERVICE 注册服务实例
// 使用服务创建器映射表管理服务实例，同一个类可以注册多个实例
int MyBoot::start() {
    static const std::map<std::string, std::function<YomkService*()>> creators = {
        {"/MyService", []() { return new MyService(YOMK_SERVER_P); }},
        {"/MyServiceAA", []() { return new MyService(YOMK_SERVER_P); }},  // 同一个类可以注册多个实例
    };
    for (const auto& name : m_srvNames) {
        auto it = creators.find(name);
        if (it != creators.end()) {
            if (YOMK_ADD_SERVICE(it->second(), name) != 0) return -1;
        }
    }
    return 0;
}

// 3. 入口
int main() {
    YOMK_BOOT(new MyBoot({"/MyService"}));
    getchar();
    return 0;
}
```

## 宏API速查

### 请求通信
```cpp
YOMK_INIT()                           // 初始化服务器（自动启动全部内置服务）
YOMK_BOOT(boot)                       // 通过 YomkBoot 生命周期初始化
YOMK_NEW_SERVICE(ClassName, srvName)  // 注册服务（模板方式）
YOMK_ADD_SERVICE(srvPtr, srvName)     // 注册服务（实例方式）
YOMK_REQUEST(url, pkg)                // 同步请求 → YomkResponse
YOMK_ASYNC_REQUEST(url, pkg, callback)// 异步请求
YOMK_SERVER_P                         // 获取 YomkServer 原始指针
YOMK_SERVER_PTR                       // 获取 YomkServer shared_ptr
```

### Context 全局状态
```cpp
YOMK_CONTEXT_CREATE(key, value)                    // 创建
YOMK_CONTEXT_GET(MsgName, key, default_value)   // 获取（模板）
YOMK_CONTEXT_SET(key, value)                       // 设置
YOMK_CONTEXT_DESTROY(key)                          // 销毁
YOMK_CONTEXT_ON_CHECKER() / YOMK_CONTEXT_OFF_CHECKER()  // 开关检查器
YOMK_CONTEXT_SET_CHECKER(key, checkFunc)            // 设置检查函数
YOMK_CONTEXT_ON_MONITOR() / YOMK_CONTEXT_OFF_MONITOR()  // 开关监控器
YOMK_CONTEXT_SET_MONITOR(key, monitorFunc)          // 设置监控函数
```

### EventLoop 事件循环
```cpp
YOMK_EVENTLOOP_START(name, defaultFunc)      // 启动
YOMK_EVENTLOOP_STOP(name)                    // 停止
YOMK_EVENTLOOP_POST(name, pkg, handleFunc)   // 投递事件（异步）
YOMK_EVENTLOOP_POST_WAIT(name, pkg, handle)  // 投递事件（同步等待）
YOMK_EVENTLOOP_DESTROY(name)                 // 销毁
```

### FunctionPool 函数池
```cpp
YOMK_FUNCTIONPOOL_REGISTER(funcName, func)  // 注册函数
YOMK_FUNCTIONPOOL_CALL(funcName, pkg)       // 调用函数
```

### 日志
```cpp
YOMK_INFO(...)           / YOMK_INFO_TAG(tag, ...)
YOMK_WARN(...)           / YOMK_WARN_TAG(tag, ...)
YOMK_ERROR(...)          / YOMK_ERROR_TAG(tag, ...)
YOMK_DEBUG(...)          / YOMK_DEBUG_TAG(tag, ...)
YOMK_FILE_LOG_CREATE(dir, file)
YOMK_FILE_LOG_WRITE(file)
YOMK_FILE_INFO(file, ...)  / YOMK_FILE_INFO_TAG(file, tag, ...)
// 同理 FILE_WARN / FILE_ERROR / FILE_DEBUG
YOMK_ON_CONSOLE_LOG_INFO()  / YOMK_OFF_CONSOLE_LOG_INFO()
// 同理 WARN / ERROR / DEBUG
YOMK_SET_CONSOLE_LOG_PROXY(proxyFunc)
```

## 模块化工程设计原则

### 推荐工程目录结构

```
MyProject/
├── main.cpp                    // 程序入口
├── boot/
│   ├── MyBoot.h                // 生命周期管理
│   └── MyBoot.cpp
├── msgs/
│   └── YomkMsgs.h              // 所有消息包定义
├── services/                   // 所有服务统一放在 services/ 下
│   ├── user/                   // 按类别分类存放
│   │   ├── UserService.h
│   │   └── UserService.cpp
│   ├── order/
│   │   ├── OrderService.h
│   │   └── OrderService.cpp
│   └── notification/
│       ├── NotificationService.h
│       └── NotificationService.cpp
└── CMakeLists.txt
```

**目录职责：**
| 目录 | 职责 |
|------|------|
| `boot/` | 程序生命周期管理（before/start/after），负责资源初始化的编排 |
| `msgs/` | 所有服务间通信的消息包定义集中管理 |
| `services/` | 所有服务实现，按业务类别分子目录存放 |

### 设计原则

1. **一切皆服务，一切皆请求**：所有功能模块统一为Service，所有交互统一为Request/Response
2. **两级模块化**：Service封装业务域，Function封装具体功能，URL路径唯一标识
3. **关注点分离**：每个Service只负责一个领域的功能，服务间依赖最小化
4. **服务间通过请求通信**：使用 `YOMK_REQUEST("/ServiceName/func", pkg)` 跨服务调用，支持同步与异步
5. **共享状态用 Context**：避免全局变量，使用 YomkContext 管理共享状态，配合Checker防非法迁移、Monitor监听变更
6. **耗时操作用 EventLoop**：IO密集型和高并发任务投递到独立事件循环，线程隔离保证顺序执行
7. **公共函数用 FunctionPool**：无状态工具函数注册到函数池，支持运行时热替换
8. **服务统一存放 services/ 目录**：每个服务 `XxxService.h` + `XxxService.cpp`，按业务类别分子目录组织，支持并行开发
9. **消息定义集中 msgs/**：所有 `YomkMsg` 注册的消息结构体统一在 `msgs/YomkMsgs.h` 中定义
10. **生命周期管理在 boot/**：通过 `YomkBoot` 子类管理 before/start/after 三阶段初始化
11. **渐进式演进**：从单体应用平滑演进到复杂多服务系统，无需重构架构

## 详细参考

- API详细参考：[reference.md](reference.md)
- 完整工程示例：[examples.md](examples.md)