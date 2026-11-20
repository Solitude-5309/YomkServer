---
name: yomkserver-modular
description: 基于 YomkServer 框架的模块化 C++17 编程，核心理念"一切皆服务，一切皆请求"，API 为七个链式域入口（yomk::server/srv_info/log/log_mgr/ctx/event_loop/fn_pool）。当用户需要创建 YomkServer 工程（"创建工程"、"新建项目"、"create a project"等任何与创建工程相关的请求）、创建扩展库、在工程中编写服务、管理全局状态（Context）、处理事件循环（EventLoop）、注册公共函数（FunctionPool）、使用日志（Logger）或内省调试时使用。仅用于基于 YomkServer 框架的开发，不适用于与 YomkServer 无关的普通 C++ 工程或问题。
---

# YomkServer 模块化编程框架

## 框架概述

基于 C++17 的模块化服务框架，核心理念：**「一切皆服务，一切皆请求」**。

| 组件 | 职责 | 域入口 |
|------|------|--------|
| **YomkServer** | 服务容器，管理生命周期 | `yomk::server()` |
| **YomkServerInfo** | 服务拓扑内省（服务/函数元信息查询） | `yomk::srv_info()` |
| **YomkService** | 功能模块单元，通过 URL `/服务名/功能名` 访问 | （经 server 域注册/请求） |
| **YomkContext** | 全局 K-V 状态管理（Checker 防非法迁移、Monitor 监听变更） | `yomk::ctx()` |
| **YomkEventLoop** | 线程隔离事件循环（同循环顺序执行、不同循环并行） | `yomk::event_loop()` |
| **YomkFunctionPool** | 动态函数池（运行时注册/热替换） | `yomk::fn_pool()` |
| **YomkLogger** | 多级别、多输出日志 | 发射 `yomk::log()` / 管理 `yomk::log_mgr()` |

- 按模块头导入（无伞形聚合头）：`<yomk/server/YomkServerApi.h>`（`yomk::server()`）、`<yomk/srv_info/YomkServerInfoApi.h>`（`yomk::srv_info()`）、`<yomk/log/YomkLoggerApi.h>`（`yomk::log()` / `yomk::log_mgr()`）、`<yomk/ctx/YomkContextApi.h>`（`yomk::ctx()`）、`<yomk/event_loop/YomkEventLoopApi.h>`（`yomk::event_loop()`）、`<yomk/fn_pool/YomkFunctionPoolApi.h>`（`yomk::fn_pool()`）
- 消息：`<yomk/msg/YomkMsg.h>` 提供 `YOMK_DEFINE_MSG` 与 `yomk::msgs::unpack`；标准类型见 `<yomk/msg/YomkStdMsg.h>`，核心载荷消息见 `<yomk/server/YomkServerMsg.h>`，模块专属消息/载荷见 `<yomk/{ctx,event_loop,fn_pool,log,srv_info}/Yomk{Module}Msg.h>` / `Yomk{Module}Payload.h`
- API 形态为**链式调用**：累积环节返回 builder 自身、终结环节执行并返回结果；单条链在一个表达式内内联使用（builder 持 `string_view`，缓存跨语句即悬垂）
- 命名空间：载荷类型（`Response`/`ResStatus`/`LogLevel` 等）位于 `yomk::payload`，服务实现内惯用 `using namespace yomk::payload;`；`YOMK_DEFINE_MSG` 生成类型位于 `yomk::msgs`

各环节完整签名、返回值与失败契约见 [reference.md](reference.md)。

## 任务一：生成工程模板

**触发条件**：当用户说"创建工程"、"新建项目"、"create a project"等任何与创建工程相关的请求时。

**强制行为**：**立即**生成完整可编译的工程骨架，**不要询问需求**、**不要列选项**、**不要讨论方案**。

### 步骤

1. **确认工程名**：
   - 如果用户指定了名称（如"创建Yomk工程"），使用该名称
   - 如果用户未指定，使用 `MyProject` 作为默认名

2. **生成完整工程**：
   - 优先复制模板（见下方生成规则），目录不存在时按 [examples.md](examples.md) 示例0 逐文件生成
   - 将 `ProjectName` 替换为用户指定的名称（或 `MyProject`）
   - 所有文件必须完整生成，确保 `source build_ubuntu.sh` 可直接编译运行

3. **告知用户**：
   - 工程已生成在当前位置的 `ProjectName/` 目录下
   - 如何编译：`source ProjectName/build_ubuntu.sh`（交互式询问前置路径，默认取环境变量 `YOMK_PREFIX_PATH`，可修改；该变量由 YomkServer 的 `build_ubuntu.sh` 安装时自动配置）
   - 如何启动：运行 `./install/bin/ProjectName`；或先 `source install/setup.bash`（PATH 已注入 install/bin）后直接运行 `ProjectName`

### 目录结构

```
ProjectName/
├── main.cpp              // 入口：yomk::server().boot_ptr(new MyBoot(...)).run()
├── boot/
│   ├── MyBoot.h          // 生命周期：before/start/after
│   └── MyBoot.cpp
├── config/
│   └── config.txt        // 配置文件（纯文本 key: value 格式）
├── interfaces/            // 工程对外接口声明：载荷 + 消息包 + URL/描述常量
│   ├── ProjectPayload.h   // 载荷结构体（namespace yomk::payload::prj）
│   ├── ProjectMsg.h       // 消息包注册（YOMK_DEFINE_MSG_NS(prj, ...)，落 yomk::msgs::prj）
│   └── ConfigServiceUrl.h // URL 常量与语义描述常量（installFunc 第三/四参引用）
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

1. `MyBoot.before()`：通过 `/proc/self/exe` 推导配置路径，存入 Context（`yomk::ctx().key(CTX_CONFIG_PATH).value(...).create()`）
2. `MyBoot.start()`：服务创建器映射表 + `m_startSrvNames` 按需启动（`yomk::server().srv_name(srvName).srv_ptr(...).add()`）
3. `MyBoot.after()`：调用 `/ConfigService/load` 加载配置
4. 安装固定到源码下 `install/`（`bin/ + config/ + setup.bash`）
5. `build_ubuntu.sh` 用 `source` 执行，交互式询问前置路径（默认 `YOMK_PREFIX_PATH`），自动编译+安装+加载环境
6. `build_ubuntu.sh`、`setup.bash.in` 不含工程名；`CMakeLists.txt`、`main.cpp`、`README.md` 使用用户指定工程名
7. **版本号传递**：`project()` 的 `VERSION` 通过 `target_compile_definitions(${PROJECT_NAME} PRIVATE APP_VERSION="${PROJECT_VERSION}")` 编译期注入，由 `ConfigService` 的 `/version` 接口返回（不放 config.txt），`MyBoot.after()` 请求该接口并输出版本号

### 生成规则

- 优先复制模板：本 skill 所在 `skills/` 目录的上层即 YomkServer 仓库根；若其下存在 `template/project/`（创建工程）或 `template/extension/`（创建扩展）目录，优先复制并替换工程名/类名/项目名占位符——与模板零漂移且省时；目录不存在时按 [examples.md](examples.md) 逐文件生成
- 完整文件内容参见 [examples.md](examples.md) 示例0
- 将 `ProjectName` 替换为用户指定名称
- 所有文件必须完整生成，确保 `source build_ubuntu.sh` 可直接编译运行

## 任务二：扩展业务服务

**触发条件**：当用户要求在已有工程中添加服务或新功能时。

在已有工程中添加新服务的标准步骤：

### 步骤

1. **定义载荷与消息包**（`interfaces/ProjectPayload.h` + `interfaces/ProjectMsg.h`）：
```cpp
// ProjectPayload.h：载荷结构体，落 yomk::payload::prj（工程本地，非 SDK 头）
namespace yomk::payload::prj
{
struct MyData { std::string field1; int field2; };
}  // namespace yomk::payload::prj

// ProjectMsg.h：统一注册消息包，类落 yomk::msgs::prj（kName 全限定 "yomk::msgs::prj::MyData"）
YOMK_DEFINE_MSG_NS(prj, yomk::payload::prj::MyData, MyData, d);  // 参数: NS, 数据类, 消息名称, 成员变量名
```

2. **定义 URL 与语义描述常量**（`interfaces/XxxServiceUrl.h`，与 SDK 各域 XxxUrl.h 同风格）：
```cpp
namespace yomk::url
{
inline constexpr std::string_view cfgMyFunc = "/XxxService/my_func";
inline constexpr const char* cfgMyFuncDesc = R"(
Semantic: 端点功能语义一句话
Returns: data 负载包类型（无负载写 data 无负载）
Idempotent: true/false —— false 时说明重调后果
SideEffect: 可观察副作用（纯读写 无）
Fail: Invalid=入参非法；Failed=业务未命中/冲突
)";
}  // namespace yomk::url
```
3. **创建服务文件**（`services/XxxService.h` + `services/XxxService.cpp`）：
```cpp
// .h
class XxxService : public YomkService {
public:
    XxxService(YomkServer *server);
    virtual ~XxxService() {}
    virtual int init() override;
private:
    yomk::payload::Response myFunc(YomkMsgPtr pkg);
};

// .cpp
XxxService::XxxService(YomkServer *server) : YomkService(server) { name("/XxxService"); }
int XxxService::init() {
    installFunc("/my_func", &XxxService::myFunc, yomk::msgs::prj::MyData::kName, yomk::url::cfgMyFuncDesc);
    yomk::log().tag("XxxService::init").info("install func [ /my_func ] to", name());
    return 0;
}
yomk::payload::Response XxxService::myFunc(YomkMsgPtr pkg) {
    auto data = yomk::msgs::unpack<yomk::msgs::MyData>(pkg);  // 解包（失败返回 nullptr，需判空）
    if (!data) { return {yomk::payload::ResStatus::Failed,
        std::string("pkg is not ") + yomk::msgs::prj::MyData::kName + ", got '" + yomk::msgs::typeOf(pkg) + "'"}; }
    // 业务逻辑...
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "success");
}
```

4. **注册到 Boot**（`boot/MyBoot.cpp` 的 `start()` 映射表中添加）：
```cpp
{"/XxxService", []() { return new XxxService(yomk::server().instance().get()); }},
```

5. **启动列表添加**（`main.cpp` 中 `run()` 的服务名参数）：
```cpp
yomk::server().boot_ptr(new MyBoot(argc, argv, {"/ConfigService", "/XxxService"})).run();
```

6. **CMakeLists.txt 添加源文件**：
```cmake
add_executable(${PROJECT_NAME}
    ...
    services/XxxService.cpp
)
```

### 跨服务调用

```cpp
// 同步：url → msg → request，单表达式内联
yomk::payload::Response resp = yomk::server().url("/XxxService/my_func")
                                     .msg(yomk::msgs::MyData::shared(MyData{"hello", 1})).request();
// 异步：.async(cb) 切入异步分面，.request() 返回 void，结果经 cb 送达
yomk::server().url("/XxxService/my_func").async([](yomk::payload::Response resp) { })
              .msg(yomk::msgs::MyData::shared(MyData{"hello", 1})).request();
```

### 服务删除与弱绑定回调

```cpp
// 删除服务：后续请求返回 service not found，在途请求安全执行完毕后才析构
int ret = yomk::server().srv_name("/XxxService").remove();   // -1 = 不存在/名称为空/未初始化
```

服务成员函数注册到**外部子系统**（FunctionPool / EventLoop / Context checker·monitor / 异步响应回调）时，**必须**用 `weakFunc(&Class::Method)` 弱绑定，否则服务删除后回调悬垂 this 崩溃；弱绑定判活机制与各回调签名的适配细节见 [reference.md](reference.md) "installFunc / weakFunc"：

```cpp
int XxxService::init() {
    installFunc("/my_func", &XxxService::myFunc, yomk::msgs::prj::MyData::kName, yomk::url::cfgMyFuncDesc);  // 本服务 funcMap 已自动弱绑定
    // 注册到 FunctionPool：弱绑定，服务删除后回调自动失效
    yomk::fn_pool().fn_name("my_work").fn(weakFunc(&XxxService::myFunc)).install();
    // Context checker/monitor 签名不同，同一 weakFunc 自动适配（键须已存在）：
    // 服务删除后 checker 默认放行（CheckStatus::Accept），monitor 丢弃
    yomk::ctx().key("key").checker_fn(weakFunc(&XxxService::myCheck)).apply();
    yomk::ctx().key("key").monitor_fn(weakFunc(&XxxService::myMonitor)).apply();
    // 异步响应回调同样适用
    yomk::server().url("/OtherService/func").async(weakFunc(&XxxService::onResp)).msg(pkg).request();
    return 0;
}
```

- 丢弃语义与判活细节（引用计数 + 注销标志双层判活、`remove()`/同名替换置位注销标志即停、`shutdown()` 走排空不置位）见 [reference.md](reference.md) "installFunc / weakFunc" 与 server 域 `.remove()`/`.shutdown()` 行
- 覆写 `virtual void deinit()` 停止非弱绑定路径的生产者（线程/定时器/外部注册），删除服务时由框架自动调用
- **异步响应回调的生命周期要求**：`.async(cb)` 的 `cb` 框架不自动弱绑定（区别于 `installFunc`），优先用服务成员函数配合 `weakFunc(&Class::Method)`（见上例）；若必须用普通 lambda，捕获对象的生命周期必须覆盖整个异步执行期，不得捕获即将销毁的局部对象引用/指针，尽量值捕获自包含数据

### 优雅关闭（shutdown）

```cpp
// 退出前关闭服务器：逐个服务调用 deinit() 并清空服务表、释放单例，幂等，假定在主线程调用；
// 关闭后请求返回 service not found，且不支持二次初始化（init 依赖 std::call_once）
yomk::server().shutdown();
```

- `deinit()` 的四个触发时机：`.srv_name(n).remove()` 删除单个服务、`.shutdown()` 关闭全部服务、忘记关闭时服务器析构兜底（避免服务持有的 joinable 线程随析构触发 `std::terminate`）、同名服务被 `.srv_name(n).srv_ptr(s).add()` 替换（旧服务先被锁外 `deinit`，再安装新服务）
- 关闭顺序：先停请求池（拒新 → 排空 → join 工作线程）再逐服务 `deinit`；`shutdown` 返回后无任何异步任务在执行，之后再提交的异步请求被拒绝
- 异步任务应保持轻量，重活经 EventLoop 或业务自建线程处理；请求池大小经 `yomk::server().async_thread(n).init()` 配置（仅首次初始化生效）
- 未显式关闭时退出经 atexit 兜底——排空语义、双池分离与单例清理细节见 [reference.md](reference.md) server 域生命周期表
- 覆写了 `deinit()` 的服务需保证幂等语义友好（重复触发只来自异常使用，但停止线程/释放资源应可重复执行不崩溃）

### 服务内省（调试）

框架内置 `/YomkServerInfo` 服务（随 `.init()` 自动启动），提供调试内省：服务列表、函数列表、单函数类型查询。功能函数安装时第三参声明期望消息类型（一律引用 `T::kName`、禁止手写消息名字符串），**第四参写功能语义描述**——非强制约束，是让内省消费者不读源码即可理解端点行为的语义澄清。描述常量与 URL 常量同置各自 `XxxUrl.h`（URL 常量集中在前、语义描述常量集中在后（按序一一对应），命名对称，如 `ctxCreate`/`ctxCreateDesc`，类型 `inline constexpr const char*`），installFunc 第四参仅引用常量：复杂端点（副作用/状态变更/失败模式多样）用六字段 R"(...)" 块（`Semantic → Returns → Precond → Idempotent → SideEffect → Fail`），纯查询端点单行 `"Semantic: …；Returns: …"`；文案须与实现相符，`Fail` 按三态归因（Invalid=入参非法，Failed=业务未命中/冲突）。框架全部内置端点均已填写，自定义服务遵循同一格式。内省数据的消费入口为 `yomk::srv_info()`（services/functions/get_info/all），模板工程 `MyBoot::after()` 末尾提供了完整的消费演示（遍历 `/functions` 输出各端点语义）：

```cpp
int XxxService::init() {
    installFunc("/my_func", &XxxService::myFunc, yomk::msgs::prj::MyData::kName);  // 内省可见类型 MyData
    installFunc("/my_desc", &XxxService::myFunc, yomk::msgs::prj::MyData::kName, "对函数的说明");  // 四参：内省可见类型 + 说明（内容自定义）
    return 0;
}

// 服务列表（返回 StringArray）
yomk::payload::Response resp = yomk::srv_info().services();
// 指定服务的函数列表（FuncInfoList 结构化清单：funcName/msgName/description，msg 恒 "ok"）
resp = yomk::srv_info().srv_name("/XxxService").functions();
// 单函数元信息查询（命中返回结构化 FuncInfo 包：msgName=类型名、description=四参说明，msg 恒 "ok"）
resp = yomk::srv_info().fn_url("/XxxService/my_func").get_info();
// 全量拓扑（ServerServiceInfoList 层级包：serviceName 服务名 + funcs 函数元信息列表）
resp = yomk::srv_info().all();
```

服务器层内省之外，四个内置模块（Context/EventLoop/FunctionPool/Logger）均在各域入口上提供模块内层内省（列表/单条元信息/全量——均返回结构化包），返回数据形态与示例见 [reference.md](reference.md) 对应小节与 [examples.md](examples.md) 示例5。

## 任务三：创建扩展库

当用户要求创建独立扩展时，**必须**生成完整可编译运行的扩展骨架。扩展编译为共享库（`.so`），支持 CMake `find_package()` 被其他工程引用。

### 目录结构

```
ExtensionName/
├── include/
│   └── xxx/                 // 小写扩展名目录，安装后对外引用统一 <yomk/<扩展名小写>/...>（与 SDK 各域共用 <prefix>/include 同一 include 根）
│       ├── XxxService.h     // 对外接口头文件（服务类声明，落 yomk::service::ext；内部头文件放 src/）
│       ├── XxxApi.h         // 模块 API：链式域入口（四件套，参考 include/yomk 各域设计）
│       ├── XxxMsg.h         // 模块消息包目录（YOMK_DEFINE_MSG_NS，消息落 yomk::msgs::ext）
│       ├── XxxPayload.h     // 模块专属载荷目录（namespace yomk::payload::ext）
│       └── XxxUrl.h         // 模块 URL 常量（namespace yomk::url::ext）
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
   - **include 路径说明**：导出 target 不含 include 路径是设计如此，头文件路径由 `YomkServer::YomkServer` 的 INTERFACE include 统一提供；若把扩展安装到扩展自己的 install/，测试程序会因 `#include <yomk/xxx/XxxService.h>`（小写扩展名目录）找不到头文件而编译失败
4. `build_ubuntu.sh` 支持可选编译测试程序（`test/` 有独立 CMakeLists）
5. **扩展库注册系统动态库缓存**：`build_ubuntu.sh` 安装完成后必须将 `${INSTALL_DIR}/lib` 幂等注册到 `/etc/ld.so.conf.d/yomk.conf`（扩展属于 yomk，复用同一 conf 文件，`grep -qxF` 判重后追加），并执行 `sudo ldconfig` 刷新缓存。新增的 so 不会自动进入 ld.so.cache，不刷新则新开任意终端都找不到扩展 so。禁止只用会话级 `export LD_LIBRARY_PATH` 代替（新终端即失效）。写 `/etc` 与 `ldconfig` 永远需要 sudo，与 `INSTALL_DIR` 是否可写无关
6. 测试程序经 `yomk::server().init()` 初始化后，用 `yomk::server().srv<XxxService>().create()` 注册扩展服务（或 `.srv_name(n).srv_ptr(new XxxService(...)).add()`）并验证功能
7. **测试程序只编译不安装**：测试程序仅供扩展开发验证使用，由 `build_ubuntu.sh` 编译到 `test/build/`（不加 `--target install`、不设 `CMAKE_INSTALL_PREFIX`），不随扩展安装；test/CMakeLists.txt 不添加 install 规则
8. **模板接口最小化**：模板服务默认只包含一个 `/version` 接口（方法名为 `version`，不带 `get` 前缀），不生成任何示例业务接口（如加减乘除）；业务功能通过「继续扩展」添加
9. **数据源无关原则**：扩展只负责处理逻辑，不关心数据来源。所有外部数据（如文件内容、路径等）必须通过请求参数传入，扩展内部不硬编码任何数据源
10. **版本号传递**：`project()` 的 `VERSION` 通过 `target_compile_definitions(${PROJECT_NAME} PRIVATE XXX_VERSION="${PROJECT_VERSION}")` 编译期注入版本宏，`/version` 接口内以字符串拼接返回（用小写扩展名），如 `"extension v" EXTENSION_VERSION`
11. **README 使用示例完整可复制**：README 使用示例必须提供完整的 main.cpp（含 include、`yomk::server().init()`、`yomk::server().srv<XxxService>().create()` 注册扩展服务、请求与输出），用户复制后即可编译运行；不得只提供请求代码片段
12. **模块四件套**：对外头文件按 SDK `include/yomk/` 各域同一设计组织，四件 + 服务类同落扩展专属子命名空间：
    - `XxxUrl.h`：`namespace yomk::url::ext` 子命名空间 URL 常量，`inline constexpr std::string_view`；域已由命名空间表达，常量名不再自我前缀
    - `XxxPayload.h`：扩展专属载荷，`namespace yomk::payload::ext`
    - `XxxMsg.h`：扩展专属消息包，`YOMK_DEFINE_MSG_NS` 声明，消息类落 `yomk::msgs::ext` 子命名空间、kName 全限定
    - `XxxApi.h`：链式域入口 `yomk::ext()` 形态，Builder 落 `yomk::detail::ext`；Api 头是模块自己的 API，只写本模块业务的终结方法，不重复封装通用请求链；可以完全不写 API 方法，但须在 Api 文件注释写清统一调用方式及 url 的功能作用（如 `yomk::server().url(yomk::url::ext::version).request()`）；模板仅内置无参终结 `version()`
    - 服务类落 `yomk::service::ext`（如 `yomk::service::ext::ExtensionService`）；不落 `yomk::ext`——其为链式入口函数，函数与命名空间不可同名，故服务域独立取名
    - 完整形态见 `template/extension/include/extension/Extension{Url,Payload,Msg,Api}.h`；模板内置 `ExtTask` 载荷与同名消息作形态示例（新增业务时替换之）
    - 请求一律优先使用统一请求接口 `yomk::server().url(<url>).request()`，扩展终结方法仅是其上的薄封装，屏蔽裸 URL、不另造请求通道。**请求链一律引用 url 常量、不裸写 URL 字符串字面量**（含测试程序与使用示例；SDK 内置域常量在 `yomk::url` 根，扩展常量落 `yomk::url::ext` 子命名空间）

### 生成规则

- 优先复制模板：本 skill 所在 `skills/` 目录的上层即 YomkServer 仓库根；若其下存在 `template/extension/` 目录，优先复制并替换扩展名/类名/项目名占位符；目录不存在时按 [examples.md](examples.md) 逐文件生成
- 完整文件内容参见 [examples.md](examples.md) 示例7
- 将 `ExtensionName` 替换为用户指定名称
- 所有文件必须完整生成，确保 `source build_ubuntu.sh` 可直接编译运行

### 继续扩展

在已有扩展中添加新功能：

1. **模块四件套补声明**：`XxxUrl.h` 加 URL 常量与语义描述常量（URL 集中在前、描述集中在后按序一一对应，命名对称 `<常量名>Desc`，格式见下文内省小节；请求链一律引用常量，不裸写 URL 字符串字面量）、`XxxPayload.h`/`XxxMsg.h` 加载荷/消息包（模板内置 `ExtTask` 为形态示例，新增业务时替换）、`XxxApi.h` 加终结方法（如 `yomk::ext().task(req).submit()` 形态）；不写终结方法时，须在 Api 文件注释写清 `yomk::server().url(<url>).request()` 统一调用方式及 url 功能作用
2. **对外接口头文件添加方法声明**（`include/XxxService.h`，类落 `yomk::service::ext`；仅内部使用的声明放 `src/` 内部头文件）
3. **实现添加功能函数**（`src/XxxService.cpp`）：`installFunc` + 实现
4. **测试程序添加测试用例**（`test/TestXxx.cpp`）

## 编程规范

### YOMK_DEFINE_MSG 消息包

```cpp
YOMK_DEFINE_MSG(数据类, 消息名称, 成员名);  // 在命名空间外定义
```
消息名称由用户自定义（PascalCase），无固定前缀要求，可与数据类同名（如 `YOMK_DEFINE_MSG(MyData, MyData, d)`）。
- **命名空间策略**：工程自有消息（`interfaces/ProjectPayload.h` + `interfaces/ProjectMsg.h`，工程本地头）用 `YOMK_DEFINE_MSG_NS(prj, 数据类, 消息名称, d)`，载荷落 `yomk::payload::prj`、消息类落 `yomk::msgs::prj` 子命名空间（与框架消息隔离）；扩展库消息（共享 include 根，须与其他扩展隔离）用 `YOMK_DEFINE_MSG_NS(ext, 数据类, 消息名称, d)`，消息类落 `yomk::msgs::ext` 子命名空间；两种形式下声明期望消息类型名一律引用 `T::kName`、禁止手写字符串
- `yomk::msgs::消息名称::shared(数据类实例)` — 创建消息包（类内静态工厂）
- `yomk::msgs::unpack<yomk::msgs::消息名称>(pkg)` — 解包模板，返回 shared_ptr，失败返回 nullptr（不自动 return，需手动判空处理）

注意：`YOMK_DEFINE_MSG` 展开生成的真实类型是独立命名空间中的 `yomk::msgs::消息名称` 与 `yomk::msgs::消息名称Ptr`，与 `yomk` 中的载荷/框架类型隔离以避免重名；引用须写全名，裸写消息名会报 not declared；`installFunc` 第三参声明期望消息类型一律引用 `yomk::msgs::消息名称::kName`、禁止手写字符串；解包由 `yomk::msgs::unpack<yomk::msgs::消息名称>` 模板实参承担（类型即名字）。
`shared()` 无法从花括号初始化列表推导模板参数，聚合体入参须显式构造：`shared(MyData{...})`。

内置标准类型（成员名均为 `d`）：`Bool`, `Char`, `UChar`, `Byte`, `Int8`, `Uint8`, `Int16`, `Uint16`, `Int32`, `Uint32`, `Int64`, `Uint64`, `Float32`, `Float64`, `String` 及对应 `XxxArray` 类型（完整清单见 SDK `<yomk/msg/YomkStdMsg.h>` 末段；模块专属消息包见各 `Yomk{Module}Msg.h`）。

### 设计原则

1. 每个 Service 只负责单一业务域
2. 服务间通过 `yomk::server().url(...).request()` 通信，不直接引用
3. 共享状态用 Context，耗时操作用 EventLoop，公共函数用 FunctionPool
4. 服务成员函数注册到外部子系统（FunctionPool/EventLoop/Context checker·monitor/异步响应）**必须**用 `weakFunc(&Class::Method)` 弱绑定，服务删除后回调自动失效；本服务 funcMap（`installFunc`）已自动弱绑定——判活机制与各回调签名适配见任务二「服务删除与弱绑定回调」与 [reference.md](reference.md) "installFunc / weakFunc"，此处不重复
5. 载荷/消息包/URL 常量集中 `interfaces/`（ProjectPayload.h + ProjectMsg.h + 按服务分子文件的 XxxUrl.h，installFunc 第三/四参引用），服务实现放 `services/`（按业务分子目录）
6. 配置路径通过 Context 传递，不用构造参数
7. 扩展库只负责处理逻辑，不关心数据来源，所有外部数据必须通过请求参数传入
8. 返回值语义遵循框架状态码约定：not-found/拒绝用 ResStatus::Failed，参数非法用 ResStatus::Invalid（详见 [reference.md](reference.md) "状态码约定"）

## 验证与质量

生成或修改工程/扩展后，必须自证可编译可运行：

1. **编译验证**：`source build_ubuntu.sh` 走通配置 → 编译 → 安装；运行可执行文件确认内置服务与业务服务日志正常
2. **测试程序**：扩展模板自带 `test/`（独立 main + `[PASS]/[FAIL]` 断言 + Test Summary 汇总，退出码 0/1）；新增业务接口时同步在测试程序中追加用例，`build_ubuntu.sh` 选择编译测试后在 `test/build/` 运行验证
3. **框架质量工具**（框架仓库内，可为生成的工程参考搭建同类能力）：`test/run_tests.sh`（一键全量测试）、`test/run_example.sh`（一键运行 examples/ 全部示例）、`test/run_static_checks.sh`（cppcheck + clang-tidy 零告警门禁）、环境变量 `YOMK_TEST_SANITIZER=off|asan|tsan`（测试构建 sanitizer 开关）；测试组织范式：`test/` 按模块分子目录，每个被测主题一个独立可执行程序（纯 main + CHECK 断言）。各脚本参数细节见仓库 `test/` 内脚本头注释

## 详细参考

- 完整工程代码与场景示例：[examples.md](examples.md)
- 六域链式 API 速查（签名/返回值/失败契约）：[reference.md](reference.md)
- 可运行示例程序：`examples/ExampleYomk{Service,Context,EventLoop,FunctionPool,Logger,ServerInfo}.cpp`（分步覆盖各模块全部 API，读运行输出即可理解每个调用）
