---
name: yomkserver-modular
description: 基于YomkServer框架的模块化C++17工程编程。核心理念“一切皆服务，一切皆请求”。用于创建YomkService功能模块、使用YomkContext全局状态管理、YomkEventLoop事件循环、YomkFunctionPool公共函数池和YomkLogger日志系统。当用户需要编写YomkServer模块化代码、创建服务、管理全局状态、处理事件队列、注册公共函数或搭建工程结构时使用。包含进阶实践：控制流与数据流分离、配置驱动、服务职责分离、Context键生命周期管理、FunctionPool回调函数无状态化等。
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

每个服务间通信的数据结构必须用 `YomkMsg` 宏注册：

```cpp
// 1. 定义普通结构体
struct MyData {
    std::string field1;
    int field2;
};
// 2. 用YomkMsg宏注册为消息包（在命名空间外）
YomkMsg(MyData, MyData)
```

注册后可用：
- `YomkMkPtr(MyData, MyData{"val", 42})` — 创建消息包
- `YomkUnPackPkgResponse(pkg, MyData, ptr)` — 解包（函数返回Response时使用）
- `YomkUnPackPkg(pkg, MyData, ptr)` — 解包（不返回时使用）
- `YomkUnPackPkgVoid(pkg, MyData, ptr)` — 解包（void函数时使用）

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
        // 1. 解包
        YomkUnPackPkgResponse(pkg, MyData, data);
        if(!data) {
            return YomkResponse(YomkResponse::eInvalid, "data is null");
        }
        // 2. 业务逻辑
        // ...
        // 3. 返回结果
        return YomkResponse(YomkResponse::eOk, "success");
    }
};
```

### 3. 程序初始化模板

```cpp
int main(int argc, char* argv[]) {
    // 初始化服务器，启动内置服务
    YOMK_INIT(std::make_shared<YomkServer>(), {
        "/YomkFunctionPool",
        "/YomkContext",
        "/YomkEventLoop",
        "/YomkLogger"
    });

    // 注册自定义服务
    YOMK_NEW_SERVICE(MyService, "/MyService");

    // 同步请求
    YomkResponse resp = YOMK_REQUEST("/MyService/my_func", YomkMkPtr(MyData, MyData{"hello", 1}));

    // 异步请求
    YOMK_ASYNC_REQUEST("/MyService/my_func", YomkMkPtr(MyData, MyData{"hello", 1}), [](YomkResponse resp) {
        // 回调处理
    });

    getchar();
    return 0;
}
```

## 宏API速查

### 请求通信
```cpp
YOMK_INIT(server, srvNames)           // 初始化服务器
YOMK_NEW_SERVICE(ClassName, srvName)  // 注册服务
YOMK_REQUEST(url, pkg)                // 同步请求 → YomkResponse
YOMK_ASYNC_REQUEST(url, pkg, callback)// 异步请求
```

### Context 全局状态
```cpp
YOMK_CONTEXT_CREATE(key, value)                    // 创建
YOMK_CONTEXT_GET(Yomk(Type), key, default_value)   // 获取（模板）
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

## 进阶实践：YomkServer 设计理念深度理解

### 1. API 风格 vs 状态风格：数据传递的层次选择

YomkServer 支持多种数据传递方式，应根据场景选择最合适的层次：

| 方式 | 适用场景 | 示例 |
|------|----------|------|
| **pkg 参数** | API 风格调用，一次性传递控制参数 | `YOMK_REQUEST("/process", YomkMkPtr(string, "task_id"))` |
| **Context** | 共享状态、跨请求的数据流 | `YOMK_CONTEXT_SET("status", val)` |
| **FunctionPool** | 无状态工具函数，热插拔 | `YOMK_FUNCTIONPOOL_CALL("validate", pkg)` |

**设计原则：pkg 用于控制流（告诉系统"做什么"），Context 用于数据流（传递"用什么数据做"）。**

```cpp
// ✅ API 风格：通过 pkg 传控制参数
YOMK_REQUEST("/TaskService/process", YomkMkPtr(string, "task_001"));

// ✅ 状态风格：数据通过 Context 传递
YomkPtr(string) status = YOMK_CONTEXT_GET(Yomk(string), "task_status");

// ❌ 反模式：把控制参数塞进 Context
YOMK_CONTEXT_SET("current_task_id", YomkMkPtr(string, "task_001"));  // 应该用 pkg
```

### 2. 服务职责分离：init() 只注册接口，业务逻辑延迟到请求时

`init()` 是服务初始化阶段，应仅用于：
- 注册功能函数（`YomkInstallFunc`）
- 创建服务内部必需的 Context 键（如状态标识、计数器）
- 启动依赖的事件循环

**不应在 init() 中：**
- 加载业务配置（应由独立的 `/create` 或 `/load` 接口处理）
- 执行耗时操作（应通过 EventLoop 异步处理）
- 注册业务数据键（应由配置驱动，按需创建）

```cpp
class TaskService : public YomkService {
    virtual int init() override {
        // ✅ 只注册接口
        YomkInstallFunc("/load", TaskService::load);
        YomkInstallFunc("/process", TaskService::process);

        // ✅ 只创建服务内部必需的键
        YOMK_CONTEXT_CREATE("task:current_id", YomkMkPtr(string, ""));
        YOMK_CONTEXT_CREATE("task:counter", YomkMkPtr(string, "0"));

        return 0;
    }
};
```

### 3. 配置驱动：用户修改配置，引擎自动响应

当业务逻辑依赖可变配置时，应将创建逻辑移入服务，而非硬编码在 main.cpp：

```cpp
// ❌ 硬编码：每次改配置都要改代码
YOMK_CONTEXT_CREATE("greeting", YomkMkPtr(string, "Hello"));
YOMK_CONTEXT_CREATE("task_name", YomkMkPtr(string, "demo"));

// ✅ 配置驱动：配置在 YAML/JSON 中，服务自动创建
// main.cpp 中只需：
YOMK_REQUEST("/TaskService/load", YomkMkPtr(string, "config/tasks.yaml"));
```

**服务加载配置的标准流程：**
1. 解析配置文件（YAML/JSON）
2. 根据配置定义创建 Context 键
3. 构建服务内部数据结构（map、vector 等）
4. 注册到服务内部状态

### 4. Context 键生命周期管理：去重与命名空间隔离

Context 是全局共享的，多模块加载时需防止重复创建和键冲突：

```cpp
// ✅ 使用集合去重
if (m_createdKeys.insert(key).second) {
    YOMK_CONTEXT_CREATE(key, defaultValue);
}

// ✅ 命名空间隔离：不同模块使用不同的键前缀
YOMK_CONTEXT_CREATE("moduleA:data", val);
YOMK_CONTEXT_CREATE("moduleB:data", val);
```

### 5. 接口设计的兼容性：新旧共存

提供新接口时保留旧接口，通过内部统一实现降低维护成本：

```cpp
// 新接口：/load_batch 批量加载
YomkResponse loadBatch(YomkPkgPtr pkg) {
    YAML::Node config = YAML::LoadFile(batchPath);
    for (const auto& item : config["items"]) {
        loadSingle(item.as<std::string>(), "");
    }
}

// 旧接口：/load_single 单个加载（向后兼容）
YomkResponse loadSingle(YomkPkgPtr pkg) {
    loadSingle(configPath, overrideName);
}

// 统一实现
bool loadSingle(const std::string& path, const std::string& name);
```

### 6. FunctionPool 回调函数的数据契约

通过 FunctionPool 注册的回调函数应该是**无状态的**，所有数据通过 pkg 参数或 Context 传递：

```cpp
// ✅ 回调函数从 pkg 或 Context 获取数据
YomkResponse processData(YomkPkgPtr pkg) {
    // 从 pkg 获取输入
    YomkPtr(string) input = YOMK_CONTEXT_GET(Yomk(string), "in:data");
    
    // 纯业务逻辑
    std::string result = transform(input->d);
    
    // 写入输出
    YOMK_CONTEXT_SET("out:result", YomkMkPtr(string, result));
    return {YomkResponse::eOk, "done"};
}

// ❌ 反模式：直接读写业务键（耦合配置）
YomkPtr(string) data = YOMK_CONTEXT_GET(Yomk(string), "my_specific_data");
```

**调用方负责数据中转：**
- 将业务键的值复制到 `in:` 前缀键
- 回调函数读写 `in:`/`out:` 键
- 将 `out:` 键的值写回业务键

### 7. 渐进式复杂度：从简单到复杂的平滑演进

YomkServer 支持从单体到多服务的平滑演进：

```cpp
// 阶段1：单体，直接调用
int main() {
    processData();  // 直接函数调用
}

// 阶段2：FunctionPool，支持热替换
YOMK_FUNCTIONPOOL_REGISTER("process", processData);
YOMK_FUNCTIONPOOL_CALL("process", pkg);

// 阶段3：Service，独立模块
class MyService : public YomkService { ... };
YOMK_NEW_SERVICE(MyService, "/MyService");
YOMK_REQUEST("/MyService/process", pkg);

// 阶段4：多服务 + EventLoop，异步处理
YOMK_EVENTLOOP_START("worker_loop", handler);
YOMK_ASYNC_REQUEST("/MyService/process", pkg, callback);
```

## 模块化工程设计原则

1. **一切皆服务，一切皆请求**：所有功能模块统一为Service，所有交互统一为Request/Response
2. **两级模块化**：Service封装业务域，Function封装具体功能，URL路径唯一标识
3. **关注点分离**：每个Service只负责一个领域的功能，服务间依赖最小化
4. **服务间通过请求通信**：使用 `YOMK_REQUEST("/ServiceName/func", pkg)` 跨服务调用，支持同步与异步
5. **共享状态用 Context**：避免全局变量，使用 YomkContext 管理共享状态，配合Checker防非法迁移、Monitor监听变更
6. **耗时操作用 EventLoop**：IO密集型和高并发任务投递到独立事件循环，线程隔离保证顺序执行
7. **公共函数用 FunctionPool**：无状态工具函数注册到函数池，支持运行时热替换
8. **每个服务独立文件**：`XxxService.h` + `XxxService.cpp`，支持并行开发
9. **渐进式演进**：从单体应用平滑演进到复杂多服务系统，无需重构架构
10. **控制流用 pkg，数据流用 Context**：API 参数通过 pkg 传递，共享状态通过 Context 传递
11. **配置驱动而非硬编码**：业务配置通过配置文件定义，服务自动解析创建，main.cpp 保持极简
12. **init() 只注册，不执行业务**：服务初始化仅注册接口和创建内部必需键，业务逻辑延迟到请求时
13. **FunctionPool 回调函数无状态化**：回调函数只读写 `in:`/`out:` 标准键或从 pkg 获取数据，不直接读写业务键
14. **Context 键去重管理**：多模块加载时使用集合跟踪已创建键，防止重复创建
15. **接口向后兼容**：新接口通过内部统一实现复用旧逻辑，旧接口保留不破坏现有调用

## 详细参考

- API详细参考：[reference.md](reference.md)
- 完整工程示例：[examples.md](examples.md)
