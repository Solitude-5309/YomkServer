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
   - 所有文件必须完整生成，确保 `source build_ubuntu.sh` 可直接编译运行

3. **告知用户**：
   - 工程已生成在当前位置的 `ProjectName/` 目录下
   - 如何编译：`source ProjectName/build_ubuntu.sh`（交互式询问前置路径，默认取环境变量 `YOMK_PREFIX_PATH`，可修改；该变量由 YomkServer 的 `build_ubuntu.sh` 安装时自动配置）
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

- 完整文件内容参见 [examples.md](examples.md) 示例0
- 将 `ProjectName` 替换为用户指定名称
- 所有文件必须完整生成，确保 `source build_ubuntu.sh` 可直接编译运行

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

### 服务删除与弱绑定回调

```cpp
// 删除服务：后续请求返回 service not found，在途请求安全执行完毕后才析构
int ret = YOMK_DEL_SERVICE("/XxxService");
```

服务成员函数注册到**外部子系统**（FunctionPool / EventLoop / Context checker·monitor / 异步响应回调）时，**必须**用 `YomkBindWeakSelf` 弱绑定，否则服务删除后回调悬垂 this 崩溃。`weakFunc` 是泛型模板，返回的泛型 lambda 按调用处目标 `std::function` 类型隐式转换，同一个宏自动适配全部回调签名：

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

服务删除后的丢弃语义：功能函数/FunctionPool 返回 `{eNo, "service has been deleted, callback ignored."}`，Context checker 默认放行 `eAccept`，void 回调（monitor/异步响应）直接丢弃。

需要停止自身线程/注销外部资源的服务覆写 `virtual void deinit()`，删除服务时由框架自动调用。

## 任务三：创建扩展库

当用户要求创建独立扩展时，**必须**生成完整可编译运行的扩展骨架。扩展编译为共享库（`.so`），支持 CMake `find_package()` 被其他工程引用。

### 目录结构

```
ExtensionName/
├── include/
│   └── XxxService.h        // 服务头文件（类声明）
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

1. 编译为 `SHARED` 库，头文件放 `include/`，实现放 `src/`
2. CMake 使用 `configure_package_config_file` + `install(EXPORT ...)` 导出配置。**Config 模板防污染**：`ProjectConfig.cmake.in` 必须在 `find_dependency()` **之前**用 `@PACKAGE_INCLUDE_INSTALL_DIR@`/`@PACKAGE_LIB_INSTALL_DIR@` 把路径固化到私有变量，路径检查用内联 `foreach` 而非 `set_and_check` 宏——否则依赖包配置会覆盖全局 `PACKAGE_PREFIX_DIR` 与同名宏，导致 `find_package` 报路径不存在或静默指向错误前缀（模板见 examples.md 示例6）。**第三方依赖传递**：若扩展以 PUBLIC 链接了额外第三方库（导出接口中仅记录裸名），模板必须在 `find_dependency(YomkServer)` 后追加 `find_dependency(<第三方包>)`，否则下游链接扩展 target 时会因找不到库而失败；无导出包的伴生库（裸库名链接）由测试工程用 `link_directories(${ExtensionName_LIB_DIR})` 补 -L
3. 安装后其他工程可通过 `find_package(ExtensionName)` 引用。**编译验证必须用 README 中的命令**：`source build_ubuntu.sh`（交互式询问前置路径与安装路径，默认均取 `$YOMK_PREFIX_PATH`，把扩展安装进 YomkServer 的安装目录）——导出 target 不含 include 路径是设计如此，头文件路径由 `YomkServer::YomkServer` 的 INTERFACE include 统一提供；若把扩展安装到扩展自己的 install/，测试程序会因 `#include <ExtensionName/XxxService.h>` 找不到头文件而编译失败。README 编译章节只保留这条交互式命令，不得提供非交互式的单路径安装命令
4. `build_ubuntu.sh` 支持可选编译测试程序（`test/` 有独立 CMakeLists）
5. **扩展库注册系统动态库缓存**：`build_ubuntu.sh` 安装完成后必须将 `${INSTALL_DIR}/lib` 幂等注册到 `/etc/ld.so.conf.d/yomk.conf`（扩展属于 yomk，复用同一 conf 文件，`grep -qxF` 判重后追加）并执行 `sudo ldconfig` 刷新缓存——新增的 so 不会自动进入 ld.so.cache，不刷新则新开任意终端都找不到扩展 so；禁止只用会话级 `export LD_LIBRARY_PATH` 代替（新终端即失效）；写 `/etc` 与 `ldconfig` 永远需要 sudo，与 `INSTALL_DIR` 是否可写无关
6. 测试程序通过 `YOMK_NEW_SERVICE` 注册服务并验证功能
7. **测试程序随扩展安装**：测试程序必须随扩展一并安装到 `<安装路径>/bin`（test/CMakeLists.txt 添加 install 规则，build_ubuntu.sh 以 `--target install` 构建 test），用户安装后可在任意终端直接运行测试程序验证扩展是否安装成功。install 规则必须**显式列出全部测试目标名**（测试程序可能有多个，且目标名不一定等于项目名），禁止使用 `${PROJECT_NAME}` 占位，写法参照 `Test/YomkServer/CMakeLists.txt`：
   ```cmake
   install(TARGETS
       TestExtensionName
       RUNTIME DESTINATION bin
   )
   ```
8. **模板接口最小化**：模板服务默认只包含一个 `/version` 接口（方法名为 `version`，不带 `get` 前缀），不生成任何示例业务接口（如加减乘除）；业务功能通过「继续扩展」添加
9. **数据源无关原则**：扩展只负责处理逻辑，不关心数据来源。所有外部数据（如文件内容、路径等）必须通过请求参数传入，扩展内部不硬编码任何数据源
10. **版本号传递**：`project()` 的 `VERSION` 通过 `target_compile_definitions(${PROJECT_NAME} PRIVATE XXX_VERSION="${PROJECT_VERSION}")` 编译期注入版本宏，`/version` 接口内以字符串拼接返回，如 `"YomkRpc v" YOMKRPC_VERSION " (WIP)"`
11. **README 使用示例完整可复制**：README 使用示例必须提供完整的 main.cpp（含 include、`YOMK_INIT()`、`YOMK_NEW_SERVICE` 注册扩展服务、请求与输出），用户复制后即可编译运行；不得只提供请求代码片段

### 生成规则

- 完整文件内容参见 [examples.md](examples.md) 示例6
- 将 `ExtensionName` 替换为用户指定名称
- 所有文件必须完整生成，确保 `source build_ubuntu.sh` 可直接编译运行

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
4. 服务成员函数注册到外部子系统（FunctionPool/EventLoop/Context checker·monitor/异步响应）必须用 `YomkBindWeakSelf` 弱绑定，服务删除后回调自动失效；本服务 funcMap（`YomkInstallFunc`）已自动弱绑定
5. 消息定义集中 `msgs/`，服务实现放 `services/`（按业务分子目录）
6. 配置路径通过 Context 传递，不用构造参数
7. 扩展库只负责处理逻辑，不关心数据来源，所有外部数据必须通过请求参数传入

## 详细参考

- 完整工程代码：[examples.md](examples.md)
- API 详细参考：[reference.md](reference.md)
