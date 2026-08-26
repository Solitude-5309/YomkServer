# YomkServer 工程示例

## 示例0：标准工程模板（创建新工程时必须生成）

将 `ProjectName` 替换为用户指定的工程名，所有文件必须完整生成。

### 目录结构
```
ProjectName/
├── main.cpp
├── boot/
│   ├── MyBoot.h
│   └── MyBoot.cpp
├── config/
│   └── config.txt
├── msgs/
│   └── YomkMsgs.h
├── services/
│   ├── ConfigService.h
│   └── ConfigService.cpp
├── typedefine/
│   └── TypeDefine.h
├── test/
├── scripts/
├── build_ubuntu.sh
├── setup.bash.in
├── CMakeLists.txt
└── README.md
```

### main.cpp
```cpp
#include <YomkServer/YomkAPI.h>
#include "boot/MyBoot.h"

using namespace yomk;

int main(int argc, char *argv[])
{
    YOMK_BOOT(new MyBoot(argc, argv, {"/ConfigService"}));
    YOMK_INFO_TAG("main", "ProjectName is running, press Enter to exit.");
    getchar();
    return 0;
}
```

### boot/MyBoot.h
```cpp
#pragma once
#include <YomkServer/YomkAPI.h>

using namespace yomk;

class MyBoot : public YomkBoot
{
public:
    MyBoot(int argc, char *argv[], const std::vector<std::string> &startSrvNames = {})
        : m_argc(argc), m_argv(argv), m_startSrvNames(startSrvNames) {}
    int before() override;
    int start() override;
    int after() override;

private:
    int m_argc;
    char **m_argv;
    std::vector<std::string> m_startSrvNames;
};
```

### boot/MyBoot.cpp
```cpp
#include "MyBoot.h"
#include "services/ConfigService.h"
#include "typedefine/TypeDefine.h"

#include <filesystem>

int MyBoot::before()
{
    YOMK_INFO_TAG("MyBoot::before", "ProjectName v" APP_VERSION " starting...");

    // 通过 /proc/self/exe 推导配置文件路径
    std::filesystem::path exePath = std::filesystem::read_symlink("/proc/self/exe");
    std::filesystem::path configPath = exePath.parent_path().parent_path() / "config" / "config.txt";
    YOMK_CONTEXT_CREATE(CTX_CONFIG_PATH, YomkMkPtr(String, configPath.string()));
    YOMK_INFO_TAG("MyBoot::before", "config path: ", configPath.string());
    return 0;
}

int MyBoot::start()
{
    // 服务创建器映射表
    static const std::map<std::string, std::function<YomkService *()>> serviceCreators = {
        {"/ConfigService", []()
         { return new ConfigService(YOMK_SERVER_P); }},
    };

    // 按需启动
    for (const auto &srvName : m_startSrvNames)
    {
        auto it = serviceCreators.find(srvName);
        if (it != serviceCreators.end())
        {
            if (YOMK_ADD_SERVICE(it->second(), srvName) != 0)
                return -1;
        }
    }
    return 0;
}

int MyBoot::after()
{
    YomkResponse resp = YOMK_REQUEST("/ConfigService/load", nullptr);
    if (resp.m_status != YomkResponse::eOk)
    {
        YOMK_ERROR_TAG("MyBoot::after", "load config failed: ", resp.m_msg);
        return -1;
    }
    YOMK_INFO_TAG("MyBoot::after", "ProjectName started successfully.");

    resp = YOMK_REQUEST("/ConfigService/get", YomkMkPtr(ConfigKey, ConfigKey{"name"}));
    if (resp.m_status != YomkResponse::eOk)
    {
        YOMK_ERROR_TAG("MyBoot::after", "get config name failed: ", resp.m_msg);
        return -1;
    }

    YomkUnPackPkg(resp.m_data, String, name);
    YOMK_INFO_TAG("MyBoot::after", "config name: ", name->d);

    // 测试版本号接口：获取并输出工程版本号（来自 CMake project() 定义的 VERSION，编译期注入）
    resp = YOMK_REQUEST("/ConfigService/version", nullptr);
    if (resp.m_status != YomkResponse::eOk)
    {
        YOMK_ERROR_TAG("MyBoot::after", "get version failed: ", resp.m_msg);
        return -1;
    }

    YomkUnPackPkg(resp.m_data, String, version);
    YOMK_INFO_TAG("MyBoot::after", "project version: ", version->d);

    resp = YOMK_REQUEST("/ConfigService/get", YomkMkPtr(ConfigKey, ConfigKey{"description"}));
    if (resp.m_status != YomkResponse::eOk)
    {
        YOMK_ERROR_TAG("MyBoot::after", "get config description failed: ", resp.m_msg);
        return -1;
    }

    YomkUnPackPkg(resp.m_data, String, description);
    YOMK_INFO_TAG("MyBoot::after", "config description: ", description->d);

    return 0;
}
```

### typedefine/TypeDefine.h
```cpp
#pragma once
#include <string>

// Context Keys
constexpr const char *const CTX_CONFIG_PATH = "config_path";
```

### msgs/YomkMsgs.h
```cpp
#pragma once
#include <YomkServer/YomkAPI.h>

// 定义完所有的结构体后，统一注册YomkMsg
// ConfigService 消息包
struct ConfigKey 
{ 
    std::string key; 
};

struct ConfigKeyValue 
{ 
    std::string key; 
    std::string value; 
};

// clang-format off
YomkMsg(ConfigKey, ConfigKey, req)
YomkMsg(ConfigKeyValue, ConfigKeyValue, req)
```

### services/ConfigService.h
```cpp
#pragma once
#include <YomkServer/YomkAPI.h>
#include <unordered_map>
#include "msgs/YomkMsgs.h"

using namespace yomk;

class ConfigService : public YomkService
{
public:
    ConfigService(YomkServer *server);
    virtual ~ConfigService() {}
    virtual int init() override;

private:
    YomkResponse loadConfig(YomkPkgPtr pkg);
    YomkResponse getConfig(YomkPkgPtr pkg);
    YomkResponse setConfig(YomkPkgPtr pkg);
    YomkResponse reloadConfig(YomkPkgPtr pkg);
    YomkResponse version(YomkPkgPtr pkg);

    std::string m_configPath;
    std::unordered_map<std::string, std::string> m_configMap;
};
```

### services/ConfigService.cpp
```cpp
#include "ConfigService.h"
#include <fstream>
#include <algorithm>
#include "typedefine/TypeDefine.h"

// 去除首尾空白符
static std::string trim(const std::string &str) {
    auto first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    auto last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, last - first + 1);
}

ConfigService::ConfigService(YomkServer *server)
    : YomkService(server)
{
    name("/ConfigService");
}

int ConfigService::init()
{
    YomkInstallFunc("/load", ConfigService::loadConfig);
    YomkInstallFunc("/get", ConfigService::getConfig);
    YomkInstallFunc("/set", ConfigService::setConfig);
    YomkInstallFunc("/reload", ConfigService::reloadConfig);
    YomkInstallFunc("/version", ConfigService::version);
    YOMK_INFO_TAG("ConfigService::init", "install func [ /load /get /set /reload /version ] to", name());
    return 0;
}

YomkResponse ConfigService::loadConfig(YomkPkgPtr pkg)
{
    auto ctxVal = YOMK_CONTEXT_GET(String, CTX_CONFIG_PATH, nullptr);
    if (!ctxVal)
        return YomkResponse(YomkResponse::eNo, "config_path not found in context");
    m_configPath = ctxVal->d;

    // 加载配置文件（纯文本 key: value 格式）
    std::ifstream ifs(m_configPath);
    if (!ifs.is_open())
        return YomkResponse(YomkResponse::eNo, "failed to open: " + m_configPath);

    std::string line;
    while (std::getline(ifs, line))
    {
        if (line.empty()) continue;
        // 按第一个冒号分割 key:value
        auto colonPos = line.find(':');
        if (colonPos == std::string::npos) continue;
        std::string key = trim(line.substr(0, colonPos));
        std::string value = trim(line.substr(colonPos + 1));
        if (!key.empty())
            m_configMap[key] = value;
    }
    ifs.close();
    YOMK_INFO_TAG("ConfigService::loadConfig", "loaded: ", m_configPath);
    return YomkResponse(YomkResponse::eOk, "ok");
}

YomkResponse ConfigService::getConfig(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, ConfigKey, data);

    auto it = m_configMap.find(data->req.key);
    if (it == m_configMap.end())
        return YomkResponse(YomkResponse::eNo, "key not found: " + data->req.key);

    return YomkResponse(YomkResponse::eOk, "ok", YomkMkPtr(String, it->second));
}

YomkResponse ConfigService::setConfig(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, ConfigKeyValue, data);

    m_configMap[data->req.key] = data->req.value;
    YOMK_INFO_TAG("ConfigService::setConfig", "set ", data->req.key, " = ", data->req.value);
    return YomkResponse(YomkResponse::eOk, "ok");
}

YomkResponse ConfigService::reloadConfig(YomkPkgPtr pkg)
{
    m_configMap.clear();
    return loadConfig(nullptr);
}

YomkResponse ConfigService::version(YomkPkgPtr pkg)
{
    // 版本号来自 CMake project() 定义的 VERSION，编译期通过 APP_VERSION 宏注入
    return YomkResponse(YomkResponse::eOk, "ok", YomkMkPtr(String, std::string(APP_VERSION)));
}
```

### config/config.txt
```
name: ProjectName
description: Create a new project based on YomkServer
```

### CMakeLists.txt
```cmake
cmake_minimum_required(VERSION 3.14)
project(ProjectName VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 固定安装到工程源码目录下的 install/
set(CMAKE_INSTALL_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/install" CACHE PATH "Install path" FORCE)

find_package(YomkServer REQUIRED)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_executable(${PROJECT_NAME}
    main.cpp
    boot/MyBoot.cpp
    services/ConfigService.cpp
)
target_compile_definitions(${PROJECT_NAME} PRIVATE APP_VERSION="${PROJECT_VERSION}")  # 编译期注入工程版本号，供 /ConfigService/version 接口返回
target_link_libraries(${PROJECT_NAME} PRIVATE
    YomkServer::YomkServer
    $<$<AND:$<CXX_COMPILER_ID:GNU>,$<VERSION_LESS:$<CXX_COMPILER_VERSION>,9.0>>:stdc++fs>
)

# 安装
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
install(DIRECTORY config/ DESTINATION config)

# 生成 setup.bash
get_target_property(YomkServer_LIB_DIR YomkServer::YomkServer LOCATION)
get_filename_component(YomkServer_LIB_DIR ${YomkServer_LIB_DIR} DIRECTORY)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/setup.bash.in
    ${CMAKE_CURRENT_BINARY_DIR}/setup.bash
    @ONLY
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/setup.bash DESTINATION .)
```

### build_ubuntu.sh
```bash
#!/bin/bash
# 一键编译脚本（交互式）
# 用法: source build_ubuntu.sh
# 交互询问 YomkServer 安装路径（前置路径），默认取环境变量 YOMK_PREFIX_PATH，可修改
# 安装路径固定为工程源码目录下的 install/（CMakeLists.txt 已强制）

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
_ORIG_DIR="$(pwd)"

# 交互询问 YomkServer 安装路径（前置路径），默认取环境变量 YOMK_PREFIX_PATH
if [ -z "${YOMK_PREFIX_PATH}" ]; then
    echo "警告: 未检测到环境变量 YOMK_PREFIX_PATH，可能未通过 build_ubuntu.sh 安装 YomkServer，请手动输入安装路径"
fi
read -r -p "请输入 YomkServer 安装路径 [默认: ${YOMK_PREFIX_PATH:-无}]: " _INPUT_PREFIX
YOMK_SERVER_PATH="${_INPUT_PREFIX:-${YOMK_PREFIX_PATH}}"
unset _INPUT_PREFIX

# 展开路径开头的 ~
YOMK_SERVER_PATH="${YOMK_SERVER_PATH/#\~/$HOME}"
# 非绝对路径自动补全为基于当前目录的绝对路径
if [[ -n "${YOMK_SERVER_PATH}" && "${YOMK_SERVER_PATH}" != /* ]]; then
    YOMK_SERVER_PATH="$(pwd)/${YOMK_SERVER_PATH}"
fi

if [ -z "${YOMK_SERVER_PATH}" ]; then
    echo "错误: 未指定 YomkServer 安装路径"
    return 1
fi
echo "-- YomkServer 安装路径: ${YOMK_SERVER_PATH}"

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}" || return 1

cmake "${SCRIPT_DIR}" -DCMAKE_PREFIX_PATH="${YOMK_SERVER_PATH}"
if [ $? -ne 0 ]; then
    echo "cmake 配置失败"
    cd "${_ORIG_DIR}"
    return 1
fi

cmake --build . --config Release --target install
if [ $? -ne 0 ]; then
    echo "编译失败"
    cd "${_ORIG_DIR}"
    return 1
fi

source "${SCRIPT_DIR}/install/setup.bash"

cd "${_ORIG_DIR}"
unset _ORIG_DIR

echo "编译完成"
```

### setup.bash.in
```bash
#!/bin/bash
# 用法: source install/setup.bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

export PATH="${SCRIPT_DIR}/bin:$PATH"
export LD_LIBRARY_PATH="@YomkServer_LIB_DIR@:$LD_LIBRARY_PATH"

echo "environment loaded."
echo "  bin: ${SCRIPT_DIR}/bin"
echo "  lib: @YomkServer_LIB_DIR@"
```

### README.md
````markdown
# ProjectName

基于 [YomkServer](https://github.com/Solitude-5309/YomkServer) 模块化框架的工程。  
YomkServer 是基于 C++17 的模块化高性能服务开发框架，核心设计理念：**「一切皆服务，一切皆请求」**。

## 前置条件

- C++17 编译器
- CMake >= 3.14
- YomkServer 已安装（通过 `build_ubuntu.sh` 安装后会自动配置环境变量 `YOMK_PREFIX_PATH` 指向安装路径）

## 编译与运行

```bash
source build_ubuntu.sh
ProjectName
```

## 工程结构

| 目录 | 职责 |
|------|------|
| `boot/` | 生命周期管理（before/start/after） |
| `config/` | 配置文件 |
| `msgs/` | 消息包定义 |
| `services/` | 服务实现 |
| `typedefine/` | 公共常量/宏/类型定义 |
| `test/` | 单元测试文件 |
| `scripts/` | 项目辅助脚本 |
````

---

## 示例1：扩展业务服务（在已有工程中添加新服务）

演示在示例0基础上添加 `UserService`，展示完整的扩展流程。

### 1. 添加消息包（msgs/YomkMsgs.h 追加）
```cpp

// 定义完所有的结构体后，统一注册YomkMsg
// UserService 消息包
struct UserQuery 
{ 
    std::string userId; 
};

struct UserInfo 
{ 
    std::string userId; 
    std::string name; 
    int age; 
};

// clang-format off
YomkMsg(UserQuery, UserQuery, req)
YomkMsg(UserInfo, UserInfo, d)
```

### 2. 创建服务（services/UserService.h）
```cpp
#pragma once
#include <YomkServer/YomkAPI.h>
#include "msgs/YomkMsgs.h"

using namespace yomk;

class UserService : public YomkService
{
public:
    UserService(YomkServer *server);
    virtual ~UserService() {}
    virtual int init() override;

private:
    YomkResponse getUser(YomkPkgPtr pkg);
    YomkResponse createUser(YomkPkgPtr pkg);
};
```

### 3. 实现服务（services/UserService.cpp）
```cpp
#include "UserService.h"

UserService::UserService(YomkServer *server)
    : YomkService(server)
{
    name("/UserService");
}

int UserService::init()
{
    YomkInstallFunc("/get_user", UserService::getUser);
    YomkInstallFunc("/create_user", UserService::createUser);
    YOMK_INFO_TAG("UserService::init", "install func [ /get_user /create_user ] to", name());
    return 0;
}

YomkResponse UserService::getUser(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, UserQuery, query);
    YOMK_INFO_TAG("UserService::getUser", "query user: ", query->req.userId);

    // 业务逻辑...
    UserInfo info{query->req.userId, "Alice", 25};
    return YomkResponse(YomkResponse::eOk, "ok", YomkMkPtr(UserInfo, info));
}

YomkResponse UserService::createUser(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, UserInfo, user);
    YOMK_INFO_TAG("UserService::createUser", "create user: ", user->d.name);

    // 跨服务调用示例：读取配置
    YomkResponse cfgResp = YOMK_REQUEST("/ConfigService/get",
        YomkMkPtr(ConfigKey, ConfigKey{"server.name"}));

    return YomkResponse(YomkResponse::eOk, "user created");
}
```

### 4. 注册到 Boot（boot/MyBoot.cpp 修改）
```cpp
// 头部添加
#include "services/UserService.h"

// start() 映射表中添加
{"/UserService", []() { return new UserService(YOMK_SERVER_P); }},
```

### 5. 启动列表（main.cpp 修改）
```cpp
YOMK_BOOT(new MyBoot(argc, argv, {"/ConfigService", "/UserService"}));
```

### 6. CMakeLists.txt 添加源文件
```cmake
add_executable(${PROJECT_NAME}
    main.cpp
    boot/MyBoot.cpp
    services/ConfigService.cpp
    services/UserService.cpp
)
```

---

## 示例2：Context Checker/Monitor

```cpp
// 检查函数：只允许非空字符串
ContextChecker::ECheckStatus nonEmptyChecker(const yomk::Context& ctx) {
    YomkUnPackPkg(ctx.m_value, String, val);
    if(!val || val->d.empty()) return ContextChecker::eReject;
    return ContextChecker::eAccept;
}

// 监控函数：记录变更
void changeLogger(const yomk::Context& ctx) {
    YomkUnPackPkgVoid(ctx.m_value, String, val);
    YOMK_INFO_TAG("CtxMonitor", "key=", ctx.m_key, " new_value=", val->d);
}

// 使用
YOMK_CONTEXT_CREATE("config", YomkMkPtr(String, "default"));
YOMK_CONTEXT_ON_CHECKER();
YOMK_CONTEXT_SET_CHECKER("config", nonEmptyChecker);
YOMK_CONTEXT_ON_MONITOR();
YOMK_CONTEXT_SET_MONITOR("config", changeLogger, true);

YOMK_CONTEXT_SET("config", YomkMkPtr(String, "new_value")); // 通过检查并触发监控
YOMK_CONTEXT_SET("config", YomkMkPtr(String, ""));          // 被拒绝
```

checker/monitor 使用**服务成员函数**时必须弱绑定，直接用 `YomkBindWeakSelf`（泛型模板按目标类型自动适配），服务删除后 checker 默认放行、monitor 丢弃：

```cpp
int MyService::init() {
    YOMK_CONTEXT_SET_CHECKER("config", YomkBindWeakSelf(MyService::myChecker));
    YOMK_CONTEXT_SET_MONITOR("config", YomkBindWeakSelf(MyService::myMonitor));
    return 0;
}
```

## 示例3：EventLoop 异步事件处理

```cpp
// 事件处理函数
YomkResponse taskHandler(YomkPkgPtr pkg) {
    YomkUnPackPkgResponse(pkg, String, taskData);
    YOMK_DEBUG_TAG("TaskHandler", "processing in thread: ", std::this_thread::get_id());
    return {YomkResponse::eOk, "task done"};
}

// 启动事件循环
YOMK_EVENTLOOP_START("worker_loop", taskHandler);

// 异步投递
YOMK_EVENTLOOP_POST("worker_loop", YomkMkPtr(String, "task_1"));

// 同步投递（等待结果）
YomkResponse resp = YOMK_EVENTLOOP_POST_WAIT("worker_loop", YomkMkPtr(String, "task_2"));
if(resp.m_status == YomkResponse::eOk) {
    YomkUnPackPkg(resp.m_data, Event, event);
    if(event) YOMK_INFO("event result: ", event->d.m_response.m_msg);
}

// 清理
YOMK_EVENTLOOP_STOP("worker_loop");
YOMK_EVENTLOOP_DESTROY("worker_loop");
```

## 示例4：FunctionPool 公共函数池

```cpp
// 定义公共函数
YomkResponse validateAmount(YomkPkgPtr pkg) {
    YomkUnPackPkgResponse(pkg, String, amountStr);
    double amount = std::stod(amountStr->d);
    if(amount <= 0) return {YomkResponse::eNo, "invalid amount"};
    return {YomkResponse::eOk, "valid"};
}

// 注册 + 调用
YOMK_FUNCTIONPOOL_REGISTER("validate_amount", validateAmount);
YomkResponse resp = YOMK_FUNCTIONPOOL_CALL("validate_amount", YomkMkPtr(String, "100.5"));

// 注销（注销后调用返回 eInvalid: funcName is not register）
YOMK_FUNCTIONPOOL_UNREGISTER("validate_amount");
```

服务成员函数注册到 FunctionPool 时必须用 `YomkBindWeakSelf` 弱绑定，服务删除（`YOMK_DEL_SERVICE`）后回调自动失效而非悬垂 this 崩溃：

```cpp
int MyService::init() {
    YOMK_FUNCTIONPOOL_REGISTER("my_work", YomkBindWeakSelf(MyService::doWork));
    return 0;
}

void MyService::deinit() {
    // 可选：删除服务时框架自动调用，也可主动注销外部注册的回调
    YOMK_FUNCTIONPOOL_UNREGISTER("my_work");
}
```

## 示例5：服务内省（调试）

内省类型元数据由 `YomkInstallFunc` 三参形式声明（不参与运行时校验，两参旧写法零改动）：

```cpp
int DemoService::init()
{
    YomkInstallFunc("/with_type", DemoService::withType, String);  // 内省可见类型 String
    YomkInstallFunc("/no_type", DemoService::noType);              // 旧写法兼容，内省无类型
    return 0;
}

// 服务列表（返回 StringArray）
YomkResponse resp = YOMK_SERVER_INFO_SERVICES();
YomkUnPackPkg(resp.m_data, StringArray, arr);  // arr->d: ["/DemoService", "/YomkServerInfo", ...]

// 指定服务的函数列表（每行 "funcName [msgName]"）
resp = YOMK_SERVER_INFO_FUNCTIONS("/DemoService");
// arr->d: ["/no_type", "/with_type [String]"]

// 单函数类型查询（service type）
resp = YOMK_SERVER_INFO_FUNCTION("/DemoService/with_type");  // eOk, msg = "String"
resp = YOMK_SERVER_INFO_FUNCTION("/DemoService/no_type");    // eOk, msg 为空串（未声明）
resp = YOMK_SERVER_INFO_FUNCTION("/DemoService/not_exist");  // eNo

// 全量 dump：服务名行 + 缩进的 "srvName + funcName [msgName]" 行
resp = YOMK_SERVER_INFO_ALL();
```

说明：FunctionPool 动态注册的函数无类型信息，内省显示为空；模块内层内省由各模块自行实现（四个内置模块均已完成，见下）。完整验证见 `Test/YomkServer/TestYomkServerInfo.cpp`。

### Context 模块内省

`/YomkContext` 服务自身提供 key 级内省（既有功能函数均已用三参宏补齐类型名，服务器层内省同步可见）：

```cpp
YOMK_CONTEXT_CREATE("str_key", YomkMkPtr(String, "v1"));
YOMK_CONTEXT_SET_CHECKER("str_key", myChecker);
YOMK_CONTEXT_SET_MONITOR("str_key", myMonitor, true);

// key 列表（返回 StringArray）
YomkResponse resp = YOMK_CONTEXT_INFO_KEYS();   // arr->d: ["str_key"]

// 单 key 元信息：key [类型名] checker:on|off monitors:N(async:M)
resp = YOMK_CONTEXT_INFO_KEY("str_key");        // eOk, msg: "str_key [String] checker:on monitors:1(async:1)"
resp = YOMK_CONTEXT_INFO_KEY("not_exist");      // eNo

// 全量 dump：每行同单 key 元信息格式
resp = YOMK_CONTEXT_INFO_ALL();
```

注意：`YOMK_CONTEXT_CREATE(key, nullptr)` 值为空时创建被拒绝（eNo），该 key 不会出现在内省结果中；内省只读，取实际值仍走 `YOMK_CONTEXT_GET`。完整验证见 `Test/YomkServer/TestYomkContextInfo.cpp`。

### EventLoop 模块内省

`/YomkEventLoop` 服务自身提供循环级内省（既有功能函数均已用三参宏补齐类型名，服务器层内省同步可见）。Event 可携带 tag 标记（POST 末位可选参数，缺省空，旧调用零改动）；启动时也可用三参宏声明默认处理函数期望的消息类型：

```cpp
YOMK_EVENTLOOP_START("loop_a", nullptr);
YOMK_EVENTLOOP_START("loop_b", defaultHandle, String);  // 声明默认处理函数期望类型，内省可见 [String]

// 投递事件时可选打 tag（tag 仅作内省标记，不参与路由/处理）
YOMK_EVENTLOOP_POST("loop_a", YomkMkPtr(String, "data"), myHandle, "tag1");
YOMK_EVENTLOOP_POST("loop_a", YomkMkPtr(String, "data"), myHandle);  // 不打 tag，旧写法直接编译

// 循环名列表（返回 StringArray）
YomkResponse resp = YOMK_EVENTLOOP_INFO_LOOPS();   // arr->d: ["loop_a", "loop_b"]

// 单循环元信息：name running:on|off pending:N defaultFunc:on|off [类型名] nextNEventTag(n): tag1, ...
// 默认处理函数声明过类型时附加 [类型名]，未声明无括号后缀
resp = YOMK_EVENTLOOP_INFO_LOOP("loop_a");          // n 缺省 3
resp = YOMK_EVENTLOOP_INFO_LOOP("loop_a", 5);       // 队列不足 5 个时全部列出，空 tag 显示 -
resp = YOMK_EVENTLOOP_INFO_LOOP("not_exist");       // eNo

// 全量 dump：每行同单循环元信息格式
resp = YOMK_EVENTLOOP_INFO_ALL();
```

注意：自动生成的事件 id 对用户无意义，内省不展示；内省只读，`stop()` 会清空队列，观察排队 tag 需保持循环运行。完整验证见 `Test/YomkServer/TestYomkEventLoopInfo.cpp`。

### FunctionPool 模块内省

`/YomkFunctionPool` 服务自身提供注册表级内省（既有功能函数均已用三参宏补齐类型名，服务器层内省同步可见）。注册时可用三参宏声明期望消息类型（与 `YomkInstallFunc` 同款，字符串化后仅作内省元数据，不参与运行时校验）：

```cpp
YOMK_FUNCTIONPOOL_REGISTER("func_a", funcA, String);  // 声明期望类型，内省可见 [String]
YOMK_FUNCTIONPOOL_REGISTER("func_b", funcB);          // 两参旧写法零改动，内省无括号后缀

// 注册函数名列表（返回 StringArray）
YomkResponse resp = YOMK_FUNCTIONPOOL_INFO_NAMES();    // arr->d: ["func_a", "func_b"]

// 单函数存在性查询：命中 eOk 且 msg 为 funcName [类型名]，未注册 eNo
resp = YOMK_FUNCTIONPOOL_INFO_NAME("func_a");           // eOk, msg: "func_a [String]"
resp = YOMK_FUNCTIONPOOL_INFO_NAME("func_b");           // eOk, msg: "func_b"
resp = YOMK_FUNCTIONPOOL_INFO_NAME("not_exist");        // eNo

// 全量 dump：首行 functions:N，其余每行 funcName [类型名]
resp = YOMK_FUNCTIONPOOL_INFO_ALL();
```

注意：内省只读；注销后函数立即从内省结果中消失。完整验证见 `Test/YomkServer/TestYomkFunctionPoolInfo.cpp`。

### Logger 模块内省

`/YomkLogger` 服务自身提供日志器级内省（既有功能函数均已用三参宏补齐类型名，服务器层内省同步可见）：

```cpp
YOMK_FILE_LOG_CREATE("/tmp", "info_logger");                          // 创建文件日志器
YomkAPI::CONSOLE_LOG_INFO_TAG("auto_logger", "hello");                // 按需自动创建控制台日志器

// 日志器列表（返回 StringArray，控制台在前、文件在后）
YomkResponse resp = YOMK_LOGGER_INFO_LOGGERS();
// arr->d: ["MainLogger [console]", "auto_logger [console]", "info_logger [file] dir:/tmp"]

// 单日志器元信息：命中 eOk 且 msg 为元信息行，未注册 eNo
resp = YOMK_LOGGER_INFO_LOGGER("MainLogger");      // eOk, msg: "MainLogger [console]"
resp = YOMK_LOGGER_INFO_LOGGER("info_logger");     // eOk, msg: "info_logger [file] dir:/tmp"
resp = YOMK_LOGGER_INFO_LOGGER("not_exist");       // eNo

// 全量 dump：首行为控制台级别开关与代理状态，其余为日志器行
resp = YOMK_LOGGER_INFO_ALL();
// 首行: "console:debug:on info:on warn:on error:on proxy:off"
```

注意：内省只读；`YOMK_INFO_TAG(tag, ...)` 宏的 tag 会追加行号后缀，按需创建的控制台日志器名为 `tag:行号`；级别开关用既有 `YOMK_ON/OFF_CONSOLE_LOG_*()` 切换后立即在 `/all` 首行生效。完整验证见 `Test/YomkServer/TestYomkLoggerInfo.cpp`。

## 示例6：文件日志

```cpp
YOMK_FILE_LOG_CREATE("/var/log/myapp", "app_log");
YOMK_FILE_INFO("app_log", "Application started");
YOMK_FILE_WARN_TAG("app_log", "Security", "Suspicious activity");
YOMK_FILE_LOG_WRITE("app_log");  // 刷新到磁盘

// 自定义控制台日志代理
bool myLogProxy(const yomk::Log& log) {
    std::cout << "[" << log.m_logger << "] " << log.m_log << std::endl;
    return false; // 不再传递给默认输出
}
YOMK_SET_CONSOLE_LOG_PROXY(myLogProxy);
```

---

## 示例7：标准扩展模板（创建新扩展时必须生成）

将 `ExtensionName` 替换为用户指定的扩展名，所有文件必须完整生成。

### 目录结构
```
ExtensionName/
├── include/
│   └── XxxService.h
├── src/
│   └── XxxService.cpp
├── test/
│   ├── CMakeLists.txt
│   └── TestExtensionName.cpp
├── cmake/
│   └── ProjectConfig.cmake.in
├── build_ubuntu.sh
├── CMakeLists.txt
└── README.md
```

头文件分层（推荐规则，默认遵循，不强制）：`include/` 只放导出给下游的对外接口头文件（服务类声明）；内部头文件（辅助类、内部数据结构等实现细节）直接放 `src/`，`install(DIRECTORY include/ ...)` 只安装 `include/` 内容，内部头文件天然不安装、对下游不可见。

### include/XxxService.h
```cpp
#pragma once
#include <YomkServer/YomkAPI.h>

using namespace yomk;

class XxxService : public YomkService
{
public:
    XxxService(YomkServer *server);
    virtual ~XxxService() {}
    virtual int init() override;

private:
    YomkResponse version(YomkPkgPtr pkg);
};
```

### src/XxxService.cpp
```cpp
#include "XxxService.h"

XxxService::XxxService(YomkServer *server)
    : YomkService(server)
{
    name("/XxxService");
}

int XxxService::init()
{
    YomkInstallFunc("/version", XxxService::version);
    YOMK_INFO_TAG("XxxService::init", "install func [ /version ] to", name());
    return 0;
}

YomkResponse XxxService::version(YomkPkgPtr pkg)
{
    std::string version = "ExtensionName v" EXTENSION_VERSION " (WIP)";
    return YomkResponse(YomkResponse::eOk, "ok", YomkMkPtr(String, version));
}
```

### test/CMakeLists.txt
```cmake
cmake_minimum_required(VERSION 3.14)
project(TestExtensionName LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(YomkServer REQUIRED)
find_package(ExtensionName REQUIRED)

message(STATUS "ExtensionName version: ${ExtensionName_VERSION}")
message(STATUS "ExtensionName include dirs: ${ExtensionName_INCLUDE_DIRS}")
message(STATUS "ExtensionName lib dir: ${ExtensionName_LIB_DIR}")
message(STATUS "ExtensionName libraries: ${ExtensionName_LIBRARIES}")

add_executable(TestExtensionName TestExtensionName.cpp)
target_link_libraries(TestExtensionName PRIVATE ExtensionName::ExtensionName YomkServer::YomkServer)

# 测试程序随扩展一并安装到 <安装路径>/bin，供用户在任意终端直接运行验证
# 测试程序可能有多个，此处显式列出全部测试目标名（不能用 ${PROJECT_NAME} 占位）
install(TARGETS
    TestExtensionName
    RUNTIME DESTINATION bin
)
```

### test/TestExtensionName.cpp
```cpp
#include <YomkServer/YomkAPI.h>
#include <ExtensionName/XxxService.h>
#include <iostream>

using namespace yomk;

static int g_pass = 0;
static int g_fail = 0;

int main(int argc, char *argv[])
{
    YOMK_INIT();
    YOMK_NEW_SERVICE(XxxService);

    // 测试版本查询
    YomkResponse resp = YOMK_REQUEST("/XxxService/version", nullptr);
    if (resp.m_status == YomkResponse::eOk)
    {
        YomkUnPackPkg(resp.m_data, String, version);
        std::cout << "[PASS] version: " << version->d << std::endl;
        g_pass++;
    }
    else
    {
        std::cout << "[FAIL] version request failed: " << resp.m_msg << std::endl;
        g_fail++;
    }

    std::cout << "\n========== Test Summary ==========" << std::endl;
    std::cout << "PASS: " << g_pass << std::endl;
    std::cout << "FAIL: " << g_fail << std::endl;

    return g_fail > 0 ? 1 : 0;
}
```

### cmake/ProjectConfig.cmake.in
```cmake
@PACKAGE_INIT@

# 立即固化安装路径：find_dependency() 递归加载的依赖包(如 YomkServer)会覆盖全局
# 变量 PACKAGE_PREFIX_DIR 及 set_and_check 宏，导致后续路径计算/检查出错。
set(_@PROJECT_NAME@_INCLUDE_DIR "@PACKAGE_INCLUDE_INSTALL_DIR@")
set(_@PROJECT_NAME@_LIB_DIR "@PACKAGE_LIB_INSTALL_DIR@")

include(CMakeFindDependencyMacro)
find_dependency(YomkServer)

include("${CMAKE_CURRENT_LIST_DIR}/@PROJECT_NAME@Targets.cmake")

set(@PROJECT_NAME@_INCLUDE_DIRS "${_@PROJECT_NAME@_INCLUDE_DIR}")
set(@PROJECT_NAME@_LIB_DIR "${_@PROJECT_NAME@_LIB_DIR}")

# 路径存在性检查：内联实现，不依赖可能被依赖包覆盖的 set_and_check 宏
foreach(_@PROJECT_NAME@_dir ${@PROJECT_NAME@_INCLUDE_DIRS} ${@PROJECT_NAME@_LIB_DIR})
    if(NOT EXISTS "${_@PROJECT_NAME@_dir}")
        message(FATAL_ERROR "File or directory ${_@PROJECT_NAME@_dir} does not exist !")
    endif()
endforeach()
unset(_@PROJECT_NAME@_dir)

set(@PROJECT_NAME@_LIBRARIES @PROJECT_NAME@::@PROJECT_NAME@)
set(@PROJECT_NAME@_VERSION "@PROJECT_VERSION@")
```

> **补充第三方依赖**：模板默认只有 YomkServer 一个依赖。若扩展以 PUBLIC 链接了额外第三方库（导出接口中仅记录裸名），必须在 `find_dependency(YomkServer)` 之后（同样处于路径固化之后的区域）追加 `find_dependency(<第三方包>)`，使下游的裸名解析为带全路径的导入 target，否则任何下游工程链接 `ExtensionName::ExtensionName` 都会因找不到库而失败；无导出包的伴生库（fastddsgen 生成库等，测试工程以裸库名链接）则由测试工程用 `link_directories(${ExtensionName_LIB_DIR})` 补 -L 搜索路径。

### build_ubuntu.sh
```bash
#!/bin/bash
# 一键编译脚本（交互式）
# 用法: source build_ubuntu.sh
# 依次交互询问 YomkServer 安装路径（前置路径）与扩展安装路径，默认均取环境变量 YOMK_PREFIX_PATH，可修改
# 扩展库与 YomkServer 安装到一起（头文件由 YomkServer::YomkServer 的 INTERFACE include 统一提供）
# 安装后将扩展 lib 注册到系统动态库搜索路径（复用 yomk.conf）并刷新 ldconfig 缓存，新开任意终端即可找到扩展 so

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_NAME="ExtensionName"
BUILD_DIR="${SCRIPT_DIR}/build"
TEST_DIR="${SCRIPT_DIR}/test"
TEST_BUILD_DIR="${TEST_DIR}/build"
_ORIG_DIR="$(pwd)"

# 路径规范化：展开 ~ 、相对路径补全
_normalize_path() {
    local p="$1"
    p="${p/#\~/$HOME}"
    if [[ -n "${p}" && "${p}" != /* ]]; then
        p="$(pwd)/${p}"
    fi
    echo "${p}"
}

if [ -z "${YOMK_PREFIX_PATH}" ]; then
    echo "警告: 未检测到环境变量 YOMK_PREFIX_PATH，可能未通过 build_ubuntu.sh 安装 YomkServer，请手动输入安装路径"
fi

# 交互询问 YomkServer 安装路径（前置路径），默认取环境变量 YOMK_PREFIX_PATH
read -r -p "请输入 YomkServer 安装路径 [默认: ${YOMK_PREFIX_PATH:-无}]: " _INPUT_PREFIX
YOMK_SERVER_PATH="$(_normalize_path "${_INPUT_PREFIX:-${YOMK_PREFIX_PATH}}")"
if [ -z "${YOMK_SERVER_PATH}" ]; then
    echo "错误: 未指定 YomkServer 安装路径"
    return 1
fi
echo "-- YomkServer 安装路径: ${YOMK_SERVER_PATH}"

# 交互询问扩展安装路径，默认装入 YomkServer 安装目录（与 YomkServer 安装到一起）
read -r -p "请输入扩展安装路径 [默认: ${YOMK_PREFIX_PATH:-无}]: " _INPUT_INSTALL
INSTALL_DIR="$(_normalize_path "${_INPUT_INSTALL:-${YOMK_PREFIX_PATH}}")"
unset _INPUT_PREFIX _INPUT_INSTALL
if [ -z "${INSTALL_DIR}" ]; then
    echo "错误: 未指定扩展安装路径"
    return 1
fi
echo "-- 扩展安装路径: ${INSTALL_DIR}"

# 安装目录不可写时（如 /opt/yomk）使用 sudo 执行安装
SUDO=""
if [ ! -w "${INSTALL_DIR}" ]; then
    SUDO="sudo"
fi

# 询问是否编译 test
read -p "编译测试程序? [Y/n]: " BUILD_TEST
BUILD_TEST=${BUILD_TEST:-y}
if [[ "${BUILD_TEST}" =~ ^[Yy]$ ]]; then
    BUILD_TEST="ON"
else
    BUILD_TEST="OFF"
fi

# 编译安装主库
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}" || return 1

cmake "${SCRIPT_DIR}" -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}" -DCMAKE_PREFIX_PATH="${YOMK_SERVER_PATH}"
if [ $? -ne 0 ]; then
    echo "cmake 配置失败"
    cd "${_ORIG_DIR}"
    return 1
fi

${SUDO} cmake --build . --config Release --target install
if [ $? -ne 0 ]; then
    echo "编译失败"
    cd "${_ORIG_DIR}"
    return 1
fi

# 注册扩展库路径到系统动态库搜索路径（扩展属于 yomk，复用 yomk.conf，幂等追加）
YOMK_LDCONF_FILE="/etc/ld.so.conf.d/yomk.conf"
if ! grep -qxF "${INSTALL_DIR}/lib" "${YOMK_LDCONF_FILE}" 2>/dev/null; then
    echo "-- 注册动态库搜索路径: ${YOMK_LDCONF_FILE}"
    echo "${INSTALL_DIR}/lib" | sudo tee -a "${YOMK_LDCONF_FILE}" >/dev/null
fi
# 刷新动态库缓存：新增的 so 不会自动进入 ld.so.cache，必须重新执行 ldconfig
echo "-- 刷新动态库缓存 (ldconfig)..."
sudo ldconfig
if [ $? -ne 0 ]; then
    echo "ldconfig 执行失败"
    cd "${_ORIG_DIR}"
    return 1
fi

# 编译测试程序
if [ "${BUILD_TEST}" = "ON" ]; then
    mkdir -p "${TEST_BUILD_DIR}"
    cd "${TEST_BUILD_DIR}" || return 1

    cmake "${TEST_DIR}" -DCMAKE_PREFIX_PATH="${INSTALL_DIR};${YOMK_SERVER_PATH}" -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}"
    if [ $? -ne 0 ]; then
        echo "测试程序 cmake 配置失败"
        cd "${_ORIG_DIR}"
        return 1
    fi

    ${SUDO} cmake --build . --config Release --target install
    if [ $? -ne 0 ]; then
        echo "测试程序编译失败"
        cd "${_ORIG_DIR}"
        return 1
    fi
fi

cd "${_ORIG_DIR}"
unset _ORIG_DIR

echo "编译完成，扩展库已注册到系统动态库缓存，新开任意终端即可使用"
ldconfig -p | grep -i "${PROJECT_NAME}" || true
if [ "${BUILD_TEST}" = "ON" ]; then
    echo "测试程序已安装到 ${INSTALL_DIR}/bin，可直接运行 TestExtensionName 验证"
fi
```

### CMakeLists.txt
```cmake
cmake_minimum_required(VERSION 3.14)
project(ExtensionName VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(YomkServer REQUIRED)
message(STATUS "YomkServer version: ${YomkServer_VERSION}")  
message(STATUS "YomkServer include dirs: ${YomkServer_INCLUDE_DIRS}")  
message(STATUS "YomkServer lib dir: ${YomkServer_LIB_DIR}")  
message(STATUS "YomkServer libraries: ${YomkServer_LIBRARIES}") 
include_directories(${YomkServer_INCLUDE_DIRS})
link_directories(${YomkServer_LIB_DIR})

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# 编译为动态库
add_library(${PROJECT_NAME} SHARED
    src/XxxService.cpp
)
target_link_libraries(${PROJECT_NAME} PRIVATE YomkServer::YomkServer)
target_compile_definitions(${PROJECT_NAME} PRIVATE EXTENSION_VERSION="${PROJECT_VERSION}")

# 安装规则
set(INCLUDE_INSTALL_DIR "include")
set(LIB_INSTALL_DIR "lib")

install(TARGETS ${PROJECT_NAME}
    EXPORT ${PROJECT_NAME}Targets
    LIBRARY DESTINATION ${LIB_INSTALL_DIR}
)
install(DIRECTORY include/ DESTINATION include/${PROJECT_NAME})

# CMake export 导出配置
include(CMakePackageConfigHelpers)

install(EXPORT ${PROJECT_NAME}Targets
    FILE ${PROJECT_NAME}Targets.cmake
    NAMESPACE ${PROJECT_NAME}::
    DESTINATION lib/cmake/${PROJECT_NAME}
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ProjectConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
    INSTALL_DESTINATION lib/cmake/${PROJECT_NAME}
    PATH_VARS INCLUDE_INSTALL_DIR LIB_INSTALL_DIR
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
    DESTINATION lib/cmake/${PROJECT_NAME}
)
```

### README.md
````markdown
# ExtensionName 扩展

基于 [YomkServer](https://github.com/Solitude-5309/YomkServer) 框架的扩展服务。

## 功能

| URL | 功能 | 说明 |
|-----|------|------|
| `/XxxService/version` | 版本查询 | 返回扩展版本信息 |

## 前置条件

- C++17 编译器
- CMake >= 3.14
- YomkServer 已安装（通过 `build_ubuntu.sh` 安装后会自动配置环境变量 `YOMK_PREFIX_PATH` 指向安装路径）

## 编译

```bash
source build_ubuntu.sh
```

> 交互式编译：依次询问 YomkServer 安装路径（前置路径）与扩展安装路径，默认均取 `$YOMK_PREFIX_PATH`，可修改。扩展库与 YomkServer 安装到一起（头文件由 `YomkServer::YomkServer` 的 INTERFACE include 统一提供）。测试程序随扩展安装到 `<安装路径>/bin`，安装后可直接运行 `TestExtensionName` 验证。

## 工程结构

```
ExtensionName/
├── include/
│   └── XxxService.h        # 对外接口头文件（服务类声明；内部头文件放 src/）
├── src/
│   └── XxxService.cpp      # 服务实现
├── CMakeLists.txt            # CMake 构建配置
├── build_ubuntu.sh           # 一键编译脚本（交互式）
└── README.md
```

## 使用示例

将以下完整程序拷贝为 main.cpp，安装扩展后可直接编译运行：

```cpp
#include <YomkServer/YomkAPI.h>
#include <ExtensionName/XxxService.h>
#include <iostream>

using namespace yomk;

int main(int argc, char *argv[])
{
    YOMK_INIT();

    // 注册 XxxService（扩展服务需先注册才能使用）
    YOMK_NEW_SERVICE(XxxService);

    // 版本查询请求
    YomkResponse resp = YOMK_REQUEST("/XxxService/version", nullptr);
    if (resp.m_status == YomkResponse::eOk)
    {
        YomkUnPackPkg(resp.m_data, String, version);
        std::cout << "version: " << version->d << std::endl; // 输出: ExtensionName v0.0.1 (WIP)
    }

    return 0;
}
```
````
