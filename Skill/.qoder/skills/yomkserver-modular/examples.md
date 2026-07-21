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
│   └── config.json
├── msgs/
│   └── YomkMsgs.h
├── services/
│   ├── ConfigService.h
│   └── ConfigService.cpp
├── typedefine/
│   └── TypeDefine.h
├── build.sh
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
    YOMK_INFO_TAG("MyBoot::before", "ProjectName starting...");

    // 通过 /proc/self/exe 推导配置文件路径
    std::filesystem::path exePath = std::filesystem::read_symlink("/proc/self/exe");
    std::filesystem::path configPath = exePath.parent_path().parent_path() / "config" / "config.json";
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

// ConfigService 消息包
struct ConfigKey { std::string key; };
YomkMsg(ConfigKey, YConfigKey, req)

struct ConfigKeyValue { std::string key; std::string value; };
YomkMsg(ConfigKeyValue, YConfigKeyValue, req)
```

### services/ConfigService.h
```cpp
#pragma once
#include <YomkServer/YomkAPI.h>
#include <nlohmann/json.hpp>
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

    std::string m_configPath;
    nlohmann::json m_json;
};
```

### services/ConfigService.cpp
```cpp
#include "ConfigService.h"
#include <fstream>
#include <sstream>
#include "typedefine/TypeDefine.h"

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
    YOMK_INFO_TAG("ConfigService::init", "install func [ /load /get /set /reload ] to", name());
    return 0;
}

YomkResponse ConfigService::loadConfig(YomkPkgPtr pkg)
{
    auto ctxVal = YOMK_CONTEXT_GET(String, CTX_CONFIG_PATH, nullptr);
    if (!ctxVal)
        return YomkResponse(YomkResponse::eNo, "config_path not found in context");
    m_configPath = ctxVal->d;

    std::ifstream ifs(m_configPath);
    if (!ifs.is_open())
        return YomkResponse(YomkResponse::eNo, "failed to open: " + m_configPath);
    m_json = nlohmann::json::parse(ifs);
    ifs.close();
    YOMK_INFO_TAG("ConfigService::loadConfig", "loaded: ", m_configPath);
    return YomkResponse(YomkResponse::eOk, "ok");
}

YomkResponse ConfigService::getConfig(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, YConfigKey, data);

    std::istringstream ss(data->req.key);
    std::string token;
    nlohmann::json *current = &m_json;
    while (std::getline(ss, token, '.'))
    {
        if (!current->is_object() || !current->contains(token))
            return YomkResponse(YomkResponse::eNo, "key not found: " + data->req.key);
        current = &(*current)[token];
    }

    std::string value;
    if (current->is_string())
        value = current->get<std::string>();
    else
        value = current->dump();

    return YomkResponse(YomkResponse::eOk, "ok", YomkMkPtr(String, value));
}

YomkResponse ConfigService::setConfig(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, YConfigKeyValue, data);

    std::istringstream ss(data->req.key);
    std::string token;
    nlohmann::json *current = &m_json;
    while (std::getline(ss, token, '.'))
    {
        if (!current->is_object())
            return YomkResponse(YomkResponse::eNo, "invalid key path: " + data->req.key);
        current = &(*current)[token];
    }

    *current = data->req.value;
    YOMK_INFO_TAG("ConfigService::setConfig", "set ", data->req.key, " = ", data->req.value);
    return YomkResponse(YomkResponse::eOk, "ok");
}

YomkResponse ConfigService::reloadConfig(YomkPkgPtr pkg)
{
    std::ifstream ifs(m_configPath);
    if (!ifs.is_open())
        return YomkResponse(YomkResponse::eNo, "failed to open config file: " + m_configPath);
    m_json = nlohmann::json::parse(ifs);
    ifs.close();
    YOMK_INFO_TAG("ConfigService::reloadConfig", "reloaded config file: ", m_configPath);
    return YomkResponse(YomkResponse::eOk, "ok");
}
```

### config/config.json
```json
{
    "server": {
        "name": "ProjectName",
        "port": 8080
    },
    "log": {
        "level": "info"
    }
}
```

### CMakeLists.txt
```cmake
cmake_minimum_required(VERSION 3.14)
project(ProjectName LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 固定安装到工程源码目录下的 install/
set(CMAKE_INSTALL_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/install" CACHE PATH "Install path" FORCE)

find_package(YomkServer REQUIRED)
find_package(nlohmann_json REQUIRED)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_executable(${PROJECT_NAME}
    main.cpp
    boot/MyBoot.cpp
    services/ConfigService.cpp
)
target_link_libraries(${PROJECT_NAME} PRIVATE
    YomkServer::YomkServer
    nlohmann_json::nlohmann_json
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

### build.sh
```bash
#!/bin/bash
# 用法: source build.sh [额外的cmake参数...]
# 示例: source build.sh -DCMAKE_PREFIX_PATH=/path/to/YomkServer/install

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
_ORIG_DIR="$(pwd)"

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}" || return 1

cmake "${SCRIPT_DIR}" "$@"
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
```markdown
# ProjectName

基于 [YomkServer](https://github.com/Solitude-5309/YomkServer) 模块化框架的工程。  
YomkServer 是基于 C++17 的模块化高性能服务开发框架，核心设计理念：**「一切皆服务，一切皆请求」**。

## 前置条件

- C++17 编译器
- CMake >= 3.14
- YomkServer 已安装

## 编译与运行

source build.sh -DCMAKE_PREFIX_PATH=~/YomkServer/install
ProjectName

## 工程结构

| 目录 | 职责 |
|------|------|
| `boot/` | 生命周期管理（before/start/after） |
| `config/` | 配置文件 |
| `msgs/` | 消息包定义 |
| `services/` | 服务实现 |
| `typedefine/` | 公共常量/宏/类型定义 |
```

---

## 示例1：扩展业务服务（在已有工程中添加新服务）

演示在示例0基础上添加 `UserService`，展示完整的扩展流程。

### 1. 添加消息包（msgs/YomkMsgs.h 追加）
```cpp
// UserService 消息包
struct UserQuery { std::string userId; };
YomkMsg(UserQuery, YUserQuery, req)

struct UserInfo { std::string userId; std::string name; int age; };
YomkMsg(UserInfo, YUserInfo, d)
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
    YomkUnPackPkgResponse(pkg, YUserQuery, query);
    YOMK_INFO_TAG("UserService::getUser", "query user: ", query->req.userId);

    // 业务逻辑...
    UserInfo info{query->req.userId, "Alice", 25};
    return YomkResponse(YomkResponse::eOk, "ok", YomkMkPtr(YUserInfo, info));
}

YomkResponse UserService::createUser(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, YUserInfo, user);
    YOMK_INFO_TAG("UserService::createUser", "create user: ", user->d.name);

    // 跨服务调用示例：读取配置
    YomkResponse cfgResp = YOMK_REQUEST("/ConfigService/get",
        YomkMkPtr(YConfigKey, ConfigKey{"server.name"}));

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
YOMK_CONTEXT_SET_MONITOR("config", changeLogger);

YOMK_CONTEXT_SET("config", YomkMkPtr(String, "new_value")); // 通过检查并触发监控
YOMK_CONTEXT_SET("config", YomkMkPtr(String, ""));          // 被拒绝
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
```

## 示例5：文件日志

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
