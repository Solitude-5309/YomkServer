---
name: yomkserver-modular
description: 基于YomkServer框架的模块化C++17工程编程。核心理念"一切皆服务，一切皆请求"。用于创建YomkService功能模块、使用YomkContext全局状态管理、YomkEventLoop事件循环、YomkFunctionPool公共函数池和YomkLogger日志系统。当用户需要编写YomkServer模块化代码、创建服务、管理全局状态、处理事件队列、注册公共函数或搭建工程结构时使用。**当用户提到"创建工程"、"新建项目"、"create project"等关键词时，必须立即使用示例0生成完整可编译的工程骨架，不要先询问需求。**
---

# YomkServer 模块化编程框架

## 框架概述

基于 C++17 的模块化服务框架，核心理念：**「一切皆服务，一切皆请求」**。

| 组件 | 职责 |
|------|------|
| **YomkServer** | 服务容器，管理生命周期 |
| **YomkService** | 功能模块单元，通过 URL `/服务名/功能名` 访问 |
| **YomkContext** | 全局 K-V 状态管理（Checker防非法迁移、Monitor监听变更） |
| **YomkEventLoop** | 线程隔离事件循环（同循环顺序执行、不同循环并行） |
| **YomkFunctionPool** | 动态函数池（运行时注册/热替换） |
| **YomkLogger** | 多级别、多输出日志 |

唯一头文件：`#include <YomkServer/YomkAPI.h>`，命名空间：`using namespace yomk;`

## 任务一：生成工程模板

**触发条件**：当用户说"创建工程"、"新建项目"、"create a project"等任何与创建工程相关的请求时。

**强制行为**：**立即**使用示例0生成完整可编译的工程骨架，**不要询问需求**、**不要列选项**、**不要讨论方案**。

### 步骤

1. **确认工程名**：
   - 如果用户指定了名称（如"创建Yomk工程"），使用该名称
   - 如果用户未指定，使用 `MyProject` 作为默认名

2. **生成完整工程**：
   - 按照下方目录结构生成所有文件
   - 将 `ProjectName` 替换为用户指定的名称（或 `MyProject`）
   - 所有文件必须完整生成，确保 `source build.sh` 可直接编译运行

3. **告知用户**：
   - 工程已生成在当前位置的 `ProjectName/` 目录下
   - 如何编译：`source ProjectName/build.sh -DCMAKE_PREFIX_PATH=~/YomkServer/install`
   - 如何启动：进入 `build/` 目录运行 `./ProjectName`

### 目录结构

```
ProjectName/
├── main.cpp              // 入口：YOMK_BOOT 启动
├── boot/
│   ├── MyBoot.h          // 生命周期：before/start/after
│   └── MyBoot.cpp
├── config/
│   └── config.txt        // 配置文件（纯文本 key: value 格式）
├── msgs/
│   └── YomkMsgs.h        // 所有消息包定义
├── services/
│   └── ConfigService.h/.cpp  // 内置配置服务
├── typedefine/
│   └── TypeDefine.h      // 公共常量/宏/类型
├── test/                 // 单元测试文件
├── scripts/              // 项目辅助脚本
├── build.sh              // 一键编译（source执行）
├── setup.bash.in         // 环境脚本模板
├── CMakeLists.txt
└── README.md
```

### 关键约定

1. `MyBoot.before()`：通过 `/proc/self/exe` 推导配置路径，存入 Context（`CTX_CONFIG_PATH`）
2. `MyBoot.start()`：服务创建器映射表 + `m_startSrvNames` 按需启动
3. `MyBoot.after()`：调用 `/ConfigService/load` 加载配置
4. 安装固定到源码下 `install/`（`bin/ + config/ + setup.bash`）
5. `build.sh` 用 `source` 执行，自动编译+安装+加载环境
6. `build.sh`、`setup.bash.in` 不含工程名；`CMakeLists.txt`、`main.cpp`、`README.md` 使用用户指定工程名

### 生成规则

- 完整文件内容参见 [examples.md](examples.md) 示例0
- 将 `ProjectName` 替换为用户指定名称
- 所有文件必须完整生成，确保 `source build.sh` 可直接编译运行

## 任务二：扩展业务服务

在已有工程中添加新服务的标准步骤：

### 步骤

1. **定义消息包**（`msgs/YomkMsgs.h`）：
```cpp
struct MyData { std::string field1; int field2; };
YomkMsg(MyData, YMyData, d)  // 参数：数据类, 消息名称, 成员变量名
```

2. **创建服务文件**（`services/XxxService.h` + `services/XxxService.cpp`）：
```cpp
// .h
class XxxService : public YomkService {
public:
    XxxService(YomkServer *server);
    virtual ~XxxService() {}
    virtual int init() override;
private:
    YomkResponse myFunc(YomkPkgPtr pkg);
};

// .cpp
XxxService::XxxService(YomkServer *server) : YomkService(server) { name("/XxxService"); }
int XxxService::init() {
    YomkInstallFunc("/my_func", XxxService::myFunc);
    YOMK_INFO_TAG("XxxService::init", "install func [ /my_func ] to", name());
    return 0;
}
YomkResponse XxxService::myFunc(YomkPkgPtr pkg) {
    YomkUnPackPkgResponse(pkg, YMyData, data);  // 解包（失败自动返回eNo）
    // 业务逻辑...
    return YomkResponse(YomkResponse::eOk, "success");
}
```

3. **注册到 Boot**（`boot/MyBoot.cpp` 的 `start()` 映射表中添加）：
```cpp
{"/XxxService", []() { return new XxxService(YOMK_SERVER_P); }},
```

4. **启动列表添加**（`main.cpp` 的 `YOMK_BOOT` 参数中）：
```cpp
YOMK_BOOT(new MyBoot(argc, argv, {"/ConfigService", "/XxxService"}));
```

5. **CMakeLists.txt 添加源文件**：
```cmake
add_executable(${PROJECT_NAME}
    ...
    services/XxxService.cpp
)
```

### 跨服务调用

```cpp
// 同步
YomkResponse resp = YOMK_REQUEST("/XxxService/my_func", YomkMkPtr(YMyData, MyData{"hello", 1}));
// 异步
YOMK_ASYNC_REQUEST("/XxxService/my_func", YomkMkPtr(YMyData, MyData{"hello", 1}), [](YomkResponse resp) { });
```

## 任务三：创建扩展库

当用户要求创建独立扩展时，**必须**生成完整可编译运行的扩展骨架。扩展编译为共享库（`.so`），支持 CMake `find_package()` 被其他工程引用。

### 目录结构

```
ExtensionName/
├── include/
│   └── XxxService.h        // 服务头文件（消息包 + 类声明）
├── src/
│   └── XxxService.cpp      // 服务实现
├── test/
│   ├── CMakeLists.txt      // 测试程序构建
│   └── TestXxx.cpp         // 测试程序
├── cmake/
│   └── ProjectConfig.cmake.in  // CMake 导出配置模板
├── build.sh                // 一键编译（支持编译测试）
├── CMakeLists.txt
└── README.md
```

### 关键约定

1. 编译为 `SHARED` 库，头文件放 `include/`，实现放 `src/`
2. CMake 使用 `configure_package_config_file` + `install(EXPORT ...)` 导出配置
3. 安装后其他工程可通过 `find_package(ExtensionName)` 引用
4. `build.sh` 支持可选编译测试程序（`test/` 有独立 CMakeLists）
5. 测试程序通过 `YOMK_NEW_SERVICE` 注册服务并验证功能
6. **数据源无关原则**：扩展只负责处理逻辑，不关心数据来源。所有外部数据（如文件内容、路径等）必须通过请求参数传入，扩展内部不硬编码任何数据源

### 生成规则

- 完整文件内容参见 [examples.md](examples.md) 示例6
- 将 `ExtensionName` 替换为用户指定名称
- 所有文件必须完整生成，确保 `source build.sh` 可直接编译运行

### 继续扩展

在已有扩展中添加新功能：

1. **头文件添加消息包 + 方法声明**（`include/XxxService.h`）
2. **实现添加功能函数**（`src/XxxService.cpp`）：`YomkInstallFunc` + 实现
3. **测试程序添加测试用例**（`test/TestXxx.cpp`）

## 编程规范

### YomkMsg 消息包

```cpp
YomkMsg(数据类, 消息名称, 成员名)  // 在命名空间外定义
```
- `YomkMkPtr(消息名称, 数据类实例)` — 创建消息包
- `YomkUnPackPkgResponse(pkg, 消息名称, ptr)` — 解包，失败自动返回 eNo
- `YomkUnPackPkgVoid(pkg, 消息名称, ptr)` — 解包，失败自动 return
- `YomkUnPackPkg(pkg, 消息名称, ptr)` — 解包，不自动 return，需手动判空

内置标准类型（成员名均为 `d`）：`String`, `Bool`, `Int32`, `Int64`, `Float64` 及对应 Array 类型。

### 设计原则

1. 每个 Service 只负责单一业务域
2. 服务间通过 `YOMK_REQUEST` 通信，不直接引用
3. 共享状态用 Context，耗时操作用 EventLoop，公共函数用 FunctionPool
4. 消息定义集中 `msgs/`，服务实现放 `services/`（按业务分子目录）
5. 配置路径通过 Context 传递，不用构造参数
6. 扩展库只负责处理逻辑，不关心数据来源，所有外部数据必须通过请求参数传入

## 详细参考

- 完整工程代码：[examples.md](examples.md)
- API 详细参考：[reference.md](reference.md)
