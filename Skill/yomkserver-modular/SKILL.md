---
name: yomkserver-modular
description: 基于YomkServer框架的模块化C++17工程编程。核心理念"一切皆服务，一切皆请求"。用于创建YomkService功能模块、使用YomkContext全局状态管理、YomkEventLoop事件循环、YomkFunctionPool公共函数池和YomkLogger日志系统。当用户需要编写YomkServer模块化代码、创建服务、管理全局状态、处理事件队列、注册公共函数或搭建工程结构时使用。**当用户提到"创建工程"、"新建项目"、"create a project"等关键词时，必须立即使用示例0生成完整可编译的工程骨架，不要先询问需求。** 仅用于基于YomkServer框架的开发，不适用于与YomkServer无关的普通C++工程或问题。
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
   - 所有文件必须完整生成，确保 `source build_ubuntu.sh` 可直接编译运行

3. **告知用户**：
   - 工程已生成在当前位置的 `ProjectName/` 目录下
   - 如何编译：`source ProjectName/build_ubuntu.sh`（交互式询问前置路径，默认取环境变量 `YOMK_PREFIX_PATH`，可修改；该变量由 YomkServer 的 `build_ubuntu.sh` 安装时自动配置）
   - 如何启动：运行 `./install/bin/ProjectName`；或先 `source install/setup.bash`（PATH 已注入 install/bin）后直接运行 `ProjectName`

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
├── build_ubuntu.sh       // 一键编译（交互式，source执行）
├── setup.bash.in         // 环境脚本模板
├── CMakeLists.txt
└── README.md
```

### 关键约定

1. `MyBoot.before()`：通过 `/proc/self/exe` 推导配置路径，存入 Context（`CTX_CONFIG_PATH`）
2. `MyBoot.start()`：服务创建器映射表 + `m_startSrvNames` 按需启动
3. `MyBoot.after()`：调用 `/ConfigService/load` 加载配置
4. 安装固定到源码下 `install/`（`bin/ + config/ + setup.bash`）
5. `build_ubuntu.sh` 用 `source` 执行，交互式询问前置路径（默认 `YOMK_PREFIX_PATH`），自动编译+安装+加载环境
6. `build_ubuntu.sh`、`setup.bash.in` 不含工程名；`CMakeLists.txt`、`main.cpp`、`README.md` 使用用户指定工程名
7. **版本号传递**：`project()` 的 `VERSION` 通过 `target_compile_definitions(${PROJECT_NAME} PRIVATE APP_VERSION="${PROJECT_VERSION}")` 编译期注入，由 `ConfigService` 的 `/version` 接口返回（不放 config.txt），`MyBoot.after()` 与 `main()` 请求该接口并输出版本号

### 生成规则

- 优先复制模板：本 skill 所在 `Skill/` 目录的上层即 YomkServer 仓库根；若其下存在 `Template/Project/`（创建工程）或 `Template/Extension/`（创建扩展）目录，优先复制并替换工程名/类名/项目名占位符——与模板零漂移且省时；目录不存在时按 [examples.md](examples.md) 逐文件生成
- 完整文件内容参见 [examples.md](examples.md) 示例0
- 将 `ProjectName` 替换为用户指定名称
- 所有文件必须完整生成，确保 `source build_ubuntu.sh` 可直接编译运行

## 任务二：扩展业务服务

在已有工程中添加新服务的标准步骤：

### 步骤

1. **定义消息包**（`msgs/YomkMsgs.h`）：
```cpp
struct MyData { std::string field1; int field2; };
YomkMsg(MyData, MyData, d)  // 参数：数据类, 消息名称, 成员变量名
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
    YomkUnPackPkgResponse(pkg, MyData, data);  // 解包（失败自动返回eNo）
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
YomkResponse resp = YOMK_REQUEST("/XxxService/my_func", YomkMkPtr(MyData, MyData{"hello", 1}));
// 异步
YOMK_ASYNC_REQUEST("/XxxService/my_func", YomkMkPtr(MyData, MyData{"hello", 1}), [](YomkResponse resp) { });
```

### 服务删除与弱绑定回调

```cpp
// 删除服务：后续请求返回 service not found，在途请求安全执行完毕后才析构
int ret = YOMK_DEL_SERVICE("/XxxService");
```

服务成员函数注册到**外部子系统**（FunctionPool / EventLoop / Context checker·monitor / 异步响应回调）时，**必须**用 `YomkBindWeakSelf` 弱绑定，否则服务删除后回调悬垂 this 崩溃；弱绑定判活机制与各回调签名的适配细节见 [reference.md](reference.md) "YomkInstallFunc / YomkBindWeakSelf"：

```cpp
int XxxService::init() {
    YomkInstallFunc("/my_func", XxxService::myFunc);  // 本服务 funcMap 已自动弱绑定
    // 注册到 FunctionPool：弱绑定，服务删除后回调自动失效
    YOMK_FUNCTIONPOOL_REGISTER("my_work", YomkBindWeakSelf(XxxService::myFunc));
    // Context checker/monitor 签名不同，同一个宏自动适配：
    // 服务删除后 checker 默认放行（eAccept），monitor 丢弃
    YOMK_CONTEXT_SET_CHECKER("key", YomkBindWeakSelf(XxxService::myCheck));
    YOMK_CONTEXT_SET_MONITOR("key", YomkBindWeakSelf(XxxService::myMonitor));
    // 异步响应回调同样适用
    YOMK_ASYNC_REQUEST("/OtherService/func", pkg, YomkBindWeakSelf(XxxService::onResp));
    return 0;
}
```

- 丢弃语义与判活细节（引用计数 + 注销标志双层判活、`YOMK_DEL_SERVICE`/同名替换置位注销标志即停、`YOMK_SHUTDOWN` 走排空不置位）见 [reference.md](reference.md) "YomkInstallFunc / YomkBindWeakSelf" 与 `YOMK_DEL_SERVICE`/`YOMK_SHUTDOWN` 行
- 覆写 `virtual void deinit()` 停止非弱绑定路径的生产者（线程/定时器/外部注册），删除服务时由框架自动调用
- **异步响应回调的生命周期要求**：`YOMK_ASYNC_REQUEST` 的 `func` 框架不自动弱绑定（区别于 `YomkInstallFunc`），优先用服务成员函数配合 `YomkBindWeakSelf`（见上例）；若必须用普通 lambda，捕获对象的生命周期必须覆盖整个异步执行期，不得捕获即将销毁的局部对象引用/指针，尽量值捕获自包含数据

### 优雅关闭（shutdown）

```cpp
// 退出前关闭服务器：逐个服务调用 deinit() 并清空服务表、释放单例，幂等，假定在主线程调用；
// 关闭后请求返回 service not found，且不支持二次初始化（init 依赖 std::call_once）
YOMK_SHUTDOWN();
```

- `deinit()` 的四个触发时机：`YOMK_DEL_SERVICE` 删除单个服务、`YOMK_SHUTDOWN` 关闭全部服务、忘记关闭时服务器析构兜底（避免服务持有的 joinable 线程随析构触发 `std::terminate`）、同名服务被 `YOMK_ADD_SERVICE` 替换（旧服务先被锁外 `deinit`，再安装新服务）
- 关闭时先停请求池（拒新 → 排空 → join 工作线程）再逐服务 `deinit`，`shutdown` 返回后无任何异步任务在执行，之后再提交的异步请求被拒绝；异步任务应保持轻量，重活经 EventLoop 或业务自建线程处理；请求池大小经 `YOMK_INIT(n)` 配置（仅首次初始化生效），未显式关闭时退出经 atexit 兜底——排空语义、双池分离与单例清理细节见 [reference.md](reference.md) `YOMK_SHUTDOWN` 行
- 覆写了 `deinit()` 的服务需保证幂等语义友好（重复触发只来自异常使用，但停止线程/释放资源应可重复执行不崩溃）

### 服务内省（调试）

框架内置 `/YomkServerInfo` 服务（随 `YOMK_INIT` 自动启动），提供调试内省：服务列表、函数列表、单函数类型查询。功能函数安装时用三参宏声明期望消息类型（仅作内省元数据，不参与运行时校验；两参旧写法零改动）：

```cpp
int XxxService::init() {
    YomkInstallFunc("/my_func", XxxService::myFunc, MyData);  // 内省可见类型 MyData
    return 0;
}

// 服务列表（返回 StringArray）
YomkResponse resp = YOMK_SERVER_INFO_SERVICES();
// 指定服务的函数列表（每行 "funcName [msgName]"）
resp = YOMK_SERVER_INFO_FUNCTIONS("/XxxService");
// 单函数类型查询（命中返回 msg 为类型名）
resp = YOMK_SERVER_INFO_FUNCTION("/XxxService/my_func");
// 全量 dump（服务名行 + 缩进函数行）
resp = YOMK_SERVER_INFO_ALL();
```

服务器层内省之外，四个内置模块（Context/EventLoop/FunctionPool/Logger）均提供模块内层内省（各自的 `YOMK_*_INFO_*()` 宏：key/循环/函数/日志器列表、单条元信息、全量 dump），输出格式与示例见 [reference.md](reference.md) 对应小节与 [examples.md](examples.md) 示例5。

## 任务三：创建扩展库

当用户要求创建独立扩展时，**必须**生成完整可编译运行的扩展骨架。扩展编译为共享库（`.so`），支持 CMake `find_package()` 被其他工程引用。

### 目录结构

```
ExtensionName/
├── include/
│   └── XxxService.h        // 对外接口头文件（服务类声明；内部头文件放 src/）
├── src/
│   └── XxxService.cpp      // 服务实现
├── test/
│   ├── CMakeLists.txt      // 测试程序构建
│   └── TestXxx.cpp         // 测试程序
├── cmake/
│   └── ProjectConfig.cmake.in  // CMake 导出配置模板
├── build_ubuntu.sh         // 一键编译（交互式，支持编译测试）
├── CMakeLists.txt
└── README.md
```

### 关键约定

1. 编译为 `SHARED` 库，实现放 `src/`。**头文件分层（推荐规则，默认遵循）**：`include/` 只放导出给下游的对外接口头文件（服务类声明），内部头文件（辅助类、内部数据结构等实现细节）直接放 `src/`——`install(DIRECTORY include/ ...)` 只安装 `include/` 内容，内部细节对下游不可见；项目习惯统一放 `include/` 亦可
2. CMake 使用 `configure_package_config_file` + `install(EXPORT ...)` 导出配置。
   - **Config 模板防污染**：`ProjectConfig.cmake.in` 必须在 `find_dependency()` **之前**用 `@PACKAGE_INCLUDE_INSTALL_DIR@`/`@PACKAGE_LIB_INSTALL_DIR@` 把路径固化到私有变量；路径检查用内联 `foreach` 而非 `set_and_check` 宏。否则依赖包配置会覆盖全局 `PACKAGE_PREFIX_DIR` 与同名宏，导致 `find_package` 报路径不存在或静默指向错误前缀（模板见 examples.md 示例7）
   - **第三方依赖传递**：扩展以 PUBLIC 链接的额外第三方库分两类——自带 CMake 包的，Config 模板必须在 `find_dependency(YomkServer)` 后追加 `find_dependency(<第三方包>)`，否则下游链接扩展 target 会因找不到库而失败；不带包、仅以裸库名 `-l` 链接的，导出配置不记录它，由下游/测试工程用 `link_directories(${ExtensionName_LIB_DIR})` 补库搜索路径
3. 安装后其他工程可通过 `find_package(ExtensionName)` 引用。
   - **编译验证只走 `source build_ubuntu.sh`**（交互式询问前置路径与安装路径，默认均取 `$YOMK_PREFIX_PATH`，把扩展安装进 YomkServer 的安装目录）；README 编译章节只保留这条交互式命令，不得提供非交互式的单路径安装命令
   - **include 路径说明**：导出 target 不含 include 路径是设计如此，头文件路径由 `YomkServer::YomkServer` 的 INTERFACE include 统一提供；若把扩展安装到扩展自己的 install/，测试程序会因 `#include <ExtensionName/XxxService.h>` 找不到头文件而编译失败
4. `build_ubuntu.sh` 支持可选编译测试程序（`test/` 有独立 CMakeLists）
5. **扩展库注册系统动态库缓存**：`build_ubuntu.sh` 安装完成后必须将 `${INSTALL_DIR}/lib` 幂等注册到 `/etc/ld.so.conf.d/yomk.conf`（扩展属于 yomk，复用同一 conf 文件，`grep -qxF` 判重后追加），并执行 `sudo ldconfig` 刷新缓存。新增的 so 不会自动进入 ld.so.cache，不刷新则新开任意终端都找不到扩展 so。禁止只用会话级 `export LD_LIBRARY_PATH` 代替（新终端即失效）。写 `/etc` 与 `ldconfig` 永远需要 sudo，与 `INSTALL_DIR` 是否可写无关
6. 测试程序通过 `YOMK_NEW_SERVICE` 注册服务并验证功能
7. **测试程序只编译不安装**：测试程序仅供扩展开发验证使用，由 `build_ubuntu.sh` 编译到 `test/build/`（不加 `--target install`、不设 `CMAKE_INSTALL_PREFIX`），不随扩展安装；test/CMakeLists.txt 不添加 install 规则
8. **模板接口最小化**：模板服务默认只包含一个 `/version` 接口（方法名为 `version`，不带 `get` 前缀），不生成任何示例业务接口（如加减乘除）；业务功能通过「继续扩展」添加
9. **数据源无关原则**：扩展只负责处理逻辑，不关心数据来源。所有外部数据（如文件内容、路径等）必须通过请求参数传入，扩展内部不硬编码任何数据源
10. **版本号传递**：`project()` 的 `VERSION` 通过 `target_compile_definitions(${PROJECT_NAME} PRIVATE XXX_VERSION="${PROJECT_VERSION}")` 编译期注入版本宏，`/version` 接口内以字符串拼接返回，如 `"ExtensionName v" EXTENSION_VERSION`
11. **README 使用示例完整可复制**：README 使用示例必须提供完整的 main.cpp（含 include、`YOMK_INIT()`、`YOMK_NEW_SERVICE` 注册扩展服务、请求与输出），用户复制后即可编译运行；不得只提供请求代码片段

### 生成规则

- 优先复制模板：本 skill 所在 `Skill/` 目录的上层即 YomkServer 仓库根；若其下存在 `Template/Extension/` 目录，优先复制并替换扩展名/类名/项目名占位符；目录不存在时按 [examples.md](examples.md) 逐文件生成
- 完整文件内容参见 [examples.md](examples.md) 示例7
- 将 `ExtensionName` 替换为用户指定名称
- 所有文件必须完整生成，确保 `source build_ubuntu.sh` 可直接编译运行

### 继续扩展

在已有扩展中添加新功能：

1. **对外接口头文件添加消息包 + 方法声明**（`include/XxxService.h`；仅内部使用的声明放 `src/` 内部头文件）
2. **实现添加功能函数**（`src/XxxService.cpp`）：`YomkInstallFunc` + 实现
3. **测试程序添加测试用例**（`test/TestXxx.cpp`）

## 编程规范

### YomkMsg 消息包

```cpp
YomkMsg(数据类, 消息名称, 成员名)  // 在命名空间外定义
```
消息名称由用户自定义（PascalCase），无固定前缀要求，可与数据类同名（如 `YomkMsg(MyData, MyData, d)`）。
- `YomkMkPtr(消息名称, 数据类实例)` — 创建消息包
- `YomkUnPackPkgResponse(pkg, 消息名称, ptr)` — 解包，失败自动返回 eNo
- `YomkUnPackPkgVoid(pkg, 消息名称, ptr)` — 解包，失败自动 return
- `YomkUnPackPkg(pkg, 消息名称, ptr)` — 解包，不自动 return，需手动判空

注意：消息名称是“宏词汇”而非“类型词汇”——只能在 `Yomk()` / `YomkPtr()` / `YomkMkPtr()` / `YomkUnPackPkg*` / `YomkInstallFunc` 第三参等宏的参数位置出现，不能当裸类型名使用（`YomkMsg` 展开生成的真实类型是带后缀的 `yomk::消息名称_` 与 `yomk::消息名称Ptr`，裸写消息名会报 not declared）。

内置标准类型（成员名均为 `d`）：`Bool`, `Char`, `UChar`, `Byte`, `Int8`, `Uint8`, `Int16`, `Uint16`, `Int32`, `Uint32`, `Int64`, `Uint64`, `Float32`, `Float64`, `String` 及对应 `XxxArray` 类型（完整清单见 `YomkPkg.h` 末段）。

### 设计原则

1. 每个 Service 只负责单一业务域
2. 服务间通过 `YOMK_REQUEST` 通信，不直接引用
3. 共享状态用 Context，耗时操作用 EventLoop，公共函数用 FunctionPool
4. 服务成员函数注册到外部子系统（FunctionPool/EventLoop/Context checker·monitor/异步响应）必须用 `YomkBindWeakSelf` 弱绑定，服务删除后回调自动失效；本服务 funcMap（`YomkInstallFunc`）已自动弱绑定
5. 消息定义集中 `msgs/`，服务实现放 `services/`（按业务分子目录）
6. 配置路径通过 Context 传递，不用构造参数
7. 扩展库只负责处理逻辑，不关心数据来源，所有外部数据必须通过请求参数传入
8. 返回值语义遵循框架状态码约定：not-found/拒绝用 eNo，参数非法用 eInvalid（详见 [reference.md](reference.md) "状态码约定"）

## 验证与质量

生成或修改工程/扩展后，必须自证可编译可运行：

1. **编译验证**：`source build_ubuntu.sh` 走通配置 → 编译 → 安装；运行可执行文件确认内置服务与业务服务日志正常
2. **测试程序**：扩展模板自带 `test/`（独立 main + `[PASS]/[FAIL]` 断言 + Test Summary 汇总，退出码 0/1）；新增业务接口时同步在测试程序中追加用例，`build_ubuntu.sh` 选择编译测试后在 `test/build/` 运行验证
3. **框架质量工具**（框架仓库内，可为生成的工程参考搭建同类能力）：
   - `Test/run_tests.sh`：一键全量测试（`--full` 完整规模 / `--timeout N` 单测试超时；压测规模由环境变量 `YOMK_TEST_STRESS_SCALE` 参数化）
   - `Test/run_static_checks.sh`：cppcheck + clang-tidy 零告警门禁（`--cppcheck` / `--tidy` 单独运行；依赖仓库根 `compile_commands.json`）
   - `YOMK_TEST_SANITIZER=off|asan|tsan`：CMake 测试构建的 sanitizer 开关
   - 测试组织范式：`Test/` 按模块分子目录，每个被测主题一个独立可执行程序（纯 main + CHECK 断言）

## 详细参考

- 完整工程代码：[examples.md](examples.md)
- API 详细参考：[reference.md](reference.md)
- 可运行示例程序：`Examples/ExampleYomk{Service,Context,EventLoop,FunctionPool,Logger}.cpp`（8 步式覆盖各模块全部 API，读运行输出即可理解每个调用）
