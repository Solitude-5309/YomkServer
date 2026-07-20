# YomkServer 完整工程示例

## 示例0：标准空白工程模板（创建新工程时必须生成）

当用户要求创建基于 YomkServer 的工程时，必须生成以下完整工程骨架。将 `ProjectName` 替换为用户指定的工程名。

### 工程目录结构
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

    // 通过 /proc/self/exe 获取可执行文件绝对路径
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

    // 根据 m_startSrvNames 按需启动服务
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
    // 服务启动后加载配置文件
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

/*
 *  Type Define
 *  统一存放常量定义、宏定义、类型定义等可复用公共定义
 */

// Context
constexpr const char *const CTX_CONFIG_PATH = "config_path";
```

### msgs/YomkMsgs.h
```cpp
#pragma once
#include <YomkServer/YomkAPI.h>

// ConfigService 消息包
// 配置键：用于 /get 和 /reload
struct ConfigKey { std::string key; };
YomkMsg(ConfigKey, YConfigKey, req)
// 访问: ptr->req.key

// 配置键值：用于 /set
struct ConfigKeyValue { std::string key; std::string value; };
YomkMsg(ConfigKeyValue, YConfigKeyValue, req)
// 访问: ptr->req.key, ptr->req.value
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

# 安装可执行文件
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

# 安装配置文件
install(DIRECTORY config/
    DESTINATION config
)

# 生成 setup.bash
get_target_property(YomkServer_LIB_DIR YomkServer::YomkServer LOCATION)
get_filename_component(YomkServer_LIB_DIR ${YomkServer_LIB_DIR} DIRECTORY)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/setup.bash.in
    ${CMAKE_CURRENT_BINARY_DIR}/setup.bash
    @ONLY
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/setup.bash
    DESTINATION .
)
```

### build.sh
```bash
#!/bin/bash
# 一键编译脚本
# 用法: source build.sh [额外的cmake参数...]
# 示例: source build.sh -DCMAKE_PREFIX_PATH=/path/to/YomkServer/install

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
_ORIG_DIR="$(pwd)"

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}" || return 1

cmake "${SCRIPT_DIR}" "$@"
if [ $? -ne 0 ]; then
    echo "[BUILD] cmake 配置失败"
    cd "${_ORIG_DIR}"
    return 1
fi

cmake --build . --config Release --target install
if [ $? -ne 0 ]; then
    echo "[BUILD] 编译失败"
    cd "${_ORIG_DIR}"
    return 1
fi

source "${SCRIPT_DIR}/install/setup.bash"

cd "${_ORIG_DIR}"
unset _ORIG_DIR

echo "[BUILD] 编译完成"
```

### setup.bash.in
```bash
#!/bin/bash
# 环境配置脚本
# 用法: source install/setup.bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

export PATH="${SCRIPT_DIR}/bin:$PATH"
export LD_LIBRARY_PATH="@YomkServer_LIB_DIR@:$LD_LIBRARY_PATH"

echo "Environment loaded."
echo "  bin: ${SCRIPT_DIR}/bin"
echo "  lib: @YomkServer_LIB_DIR@"
```

### README.md
```markdown
# ProjectName

基于 YomkServer 模块化框架的工程。

## 前置条件

- C++17 编译器
- CMake >= 3.14
- YomkServer 已安装到系统

## 工程结构

| 目录 | 职责 |
|------|------|
| `boot/` | 程序生命周期管理（before/start/after） |
| `config/` | 配置文件 |
| `msgs/` | 消息包定义 |
| `services/` | 服务实现 |
| `typedefine/` | 公共常量/宏/类型定义 |

## 编译与运行

source build.sh -DCMAKE_PREFIX_PATH=~/YomkServer/install  
ProjectName  


## 生命周期

| 阶段 | 方法 | 用途 |
|------|------|------|
| 启动前 | `before()` | 路径推导、创建 Context、EventLoop、注册 FunctionPool |
| 启动中 | `start()` | 注册并启动服务 |
| 启动后 | `after()` | 调用服务接口做初始化 |
```

---

## 示例1：模块化多服务工程（基于TestYomkServer）

展示一个典型的模块化工程结构，包含多个Service协作、Boot生命周期管理、跨服务调用。

### 工程目录结构
```
MyProject/
├── main.cpp
├── boot/
│   ├── MyBoot.h              // 生命周期管理
│   └── MyBoot.cpp
├── msgs/
│   └── YomkMsgs.h            // 所有消息包定义
├── services/                 // 所有服务按类别分子目录存放
│   ├── YomkServiceA.h
│   ├── YomkServiceA.cpp
│   ├── YomkServiceB.h
│   └── YomkServiceB.cpp
└── CMakeLists.txt
```

### msgs/YomkMsgs.h — 消息包定义
```cpp
#pragma once
#include <YomkServer/YomkAPI.h>

// 定义自定义数据类
struct MyServiceMsg
{
    std::string content;
};
// 将自定义数据类映射为消息包
// 参数：自定义数据类, 消息名称, 数据成员变量名
// 消息名称 YMyServiceMsg 用于框架类型识别，辅助宏均使用此名称
YomkMsg(MyServiceMsg, YMyServiceMsg, msg)
// 访问: ptr->msg.content
```

### services/YomkServiceA.h — 服务A
```cpp
#pragma once
#include <YomkServer/YomkAPI.h>
#include "msgs/YomkMsgs.h"

// 创建一个服务A，用于编写功能集合
class YomkServiceA : public YomkService
{
public:
    YomkServiceA(YomkServer *server);
    virtual ~YomkServiceA() {}

public:
    virtual int init();

private:
    YomkResponse callSkillA(YomkPkgPtr pkg);
};
```

### services/YomkServiceA.cpp — 服务A实现
```cpp
#include "YomkServiceA.h"

YomkServiceA::YomkServiceA(YomkServer *server)
    : YomkService(server)
{
    name("/YomkServiceA");
}

int YomkServiceA::init()
{
    // 安装功能函数，功能函数名称在服务中必须唯一
    YomkInstallFunc("/call_skill_a", YomkServiceA::callSkillA);
    // 日志
    YOMK_INFO_TAG("YomkServiceA::init", "install func [ /call_skill_a ] to", name());
    return 0;
}

YomkResponse YomkServiceA::callSkillA(YomkPkgPtr pkg)
{
    // 解包数据
    YomkUnPackPkgResponse(pkg, YMyServiceMsg, myServiceMsg);

    // 日志
    YOMK_INFO_TAG("YomkServiceA::callSkillA", name(), " exec skill a, with msg: ", myServiceMsg->msg.content);

    // 调用服务B中的方法（跨服务调用）
    YomkResponse response = YOMK_REQUEST("/YomkServiceB/call_skill_b", YomkMkPtr(YMyServiceMsg, MyServiceMsg{"hello world b"}));

    // 检查调用结果
    if (response.m_status != YomkResponse::eOk)
    {
        YOMK_ERROR_TAG("YomkServiceA::callSkillA", name(), " call /YomkServiceB/call_skill_b, response: ", response.m_msg);
        return YomkResponse(YomkResponse::eNo, name() + " exec skill a failed");
    }

    // 日志
    YOMK_INFO_TAG("YomkServiceA::callSkillA", name(), " call /YomkServiceB/call_skill_b, response: ", response.m_msg);

    // 返回结果
    return YomkResponse(YomkResponse::eOk, name() + " exec skill a success");
}
```

### services/YomkServiceB.h — 服务B
```cpp
#pragma once
#include <YomkServer/YomkAPI.h>
#include "msgs/YomkMsgs.h"

class YomkServiceB : public YomkService
{
public:
    YomkServiceB(YomkServer *server);
    virtual ~YomkServiceB() {}

public:
    virtual int init();

private:
    YomkResponse callSkillB(YomkPkgPtr pkg);
};
```

### services/YomkServiceB.cpp — 服务B实现
```cpp
#include "YomkServiceB.h"

YomkServiceB::YomkServiceB(YomkServer *server)
    : YomkService(server)
{
    name("/YomkServiceB");
}

int YomkServiceB::init()
{
    // 安装功能函数，功能函数名称在服务中必须唯一
    YomkInstallFunc("/call_skill_b", YomkServiceB::callSkillB);
    // 日志
    YOMK_INFO_TAG("YomkServiceB::init", "install func [ /call_skill_b ] to", name());
    return 0;
}

YomkResponse YomkServiceB::callSkillB(YomkPkgPtr pkg)
{
    // 解包数据
    YomkUnPackPkgResponse(pkg, YMyServiceMsg, myServiceMsg);

    // 日志
    YOMK_INFO_TAG("YomkServiceB::callSkillB", name(), " exec skill b, with msg: ", myServiceMsg->msg.content);

    // 返回结果
    return YomkResponse(YomkResponse::eOk, name() + " exec skill b success");
}
```

### boot/MyBoot.h — 生命周期管理
```cpp
#pragma once
#include <YomkServer/YomkAPI.h>

class MyBoot : public YomkBoot
{
public:
    MyBoot(const std::vector<std::string> &startSrvNames = {}) : m_startSrvNames(startSrvNames) {}
    int before();
    int start();
    int after();

private:
    std::vector<std::string> m_startSrvNames; // 将要启动的服务清单，实际业务按需启动
};
```

### boot/MyBoot.cpp — 生命周期实现
```cpp
#include "MyBoot.h"
#include "services/YomkServiceA.h"
#include "services/YomkServiceB.h"

int MyBoot::before()
{
    // 服务启动前的初始化操作
    // 服务启动前创建CONTEXT，确保在服务启动时能够访问上下文
    // 服务启动前创建EVENTLOOP，确保在服务启动时能够使用特定的事件循环
    // 服务启动前注册功能函数到FUNCTION_POOL，确保在服务启动时能够访问功能函数
    // 服务启动前创建YOMK_SET_CONSOLE_LOG_PROXY，确保在服务启动时能够触发日志代理
    // 服务启动前创建其他必要的资源，确保在服务启动时能够使用
    return 0;
}

int MyBoot::start()
{
    // 现有的全部服务清单
    // 使用服务创建器映射表管理服务实例，同一个类可以注册多个实例
    static const std::map<std::string, std::function<YomkService *()>> serviceCreators = {
        {"/YomkServiceA", []()
         { return new YomkServiceA(YOMK_SERVER_P); }},
        {"/YomkServiceAA", []()
         { return new YomkServiceA(YOMK_SERVER_P); }},  // 同一个类可以注册多个实例
        {"/YomkServiceB", []()
         { return new YomkServiceB(YOMK_SERVER_P); }},
        {"/YomkServiceBB", []()
         { return new YomkServiceB(YOMK_SERVER_P); }}};  // 同一个类可以注册多个实例

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
    // 服务启动后的善后操作
    // 调用服务接口进行服务内部初始化操作
    // 调用服务接口自启动某个任务
    return 0;
}
```

### main.cpp — 程序入口
```cpp
#include <YomkServer/YomkAPI.h>
#include "boot/MyBoot.h"
#include "msgs/YomkMsgs.h"

int main(int argc, char *argv[])
{
    YOMK_BOOT(new MyBoot({"/YomkServiceA", "/YomkServiceB"}));

    // 同步调用服务A中的方法
    YomkResponse response = YOMK_REQUEST("/YomkServiceA/call_skill_a", YomkMkPtr(YMyServiceMsg, MyServiceMsg{"hello world a"}));
    if (response.m_status == YomkResponse::eOk)
    {
        YOMK_INFO_TAG("main", "request /YomkServiceA/call_skill_a, with response.msg: ", response.m_msg);
    }
    else
    {
        YOMK_ERROR_TAG("main", "request /YomkServiceA/call_skill_a, with response.msg: ", response.m_msg);
    }

    YOMK_INFO_TAG("main", "request /YomkServiceA/call_skill_a send finished.");

    // 异步调用服务A中的方法
    YOMK_ASYNC_REQUEST("/YomkServiceA/call_skill_a", YomkMkPtr(YMyServiceMsg, MyServiceMsg{"hello world a"}), [](YomkResponse response)
                       {
        if(response.m_status == YomkResponse::eOk)
        {
            YOMK_INFO_TAG("main", "async request /YomkServiceA/call_skill_a, with response.msg: ", response.m_msg);
        }
        else
        {
            YOMK_ERROR_TAG("main", "async request /YomkServiceA/call_skill_a, with response.msg: ", response.m_msg);
        } });

    YOMK_INFO_TAG("main", "async request /YomkServiceA/call_skill_a send finished.");

    getchar();

    return 0;
}
```

### CMakeLists.txt — 构建配置
```cmake
cmake_minimum_required(VERSION 3.14)
project(MyProject LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(YomkServer REQUIRED)

set(MyProject_SOURCES
    main.cpp
    boot/MyBoot.cpp
    services/YomkServiceA.cpp
    services/YomkServiceB.cpp
)
add_executable(MyProject ${MyProject_SOURCES})
target_link_libraries(MyProject PRIVATE YomkServer::YomkServer)
```

## 示例2：Context Checker/Monitor 完整流程

```cpp
// 检查函数：只允许非空字符串
ContextChecker::ECheckStatus nonEmptyChecker(const yomk::Context& ctx) {
    YomkUnPackPkg(ctx.m_value, String, val);
    if(!val || val->d.empty()) return ContextChecker::eReject;
    return ContextChecker::eAccept;
}

// 监控函数：记录所有变更
void changeLogger(const yomk::Context& ctx) {
    YomkUnPackPkgVoid(ctx.m_value, String, val); // 宏已自动判空
    YOMK_INFO_TAG("CtxMonitor", "key=", ctx.m_key, " new_value=", val->d);
}

// 使用
YOMK_CONTEXT_CREATE("config", YomkMkPtr(String, "default"));
YOMK_CONTEXT_ON_CHECKER();
YOMK_CONTEXT_SET_CHECKER("config", nonEmptyChecker);
YOMK_CONTEXT_ON_MONITOR();
YOMK_CONTEXT_SET_MONITOR("config", changeLogger);

YOMK_CONTEXT_SET("config", YomkMkPtr(String, "new_value")); // 通过检查并触发监控
YOMK_CONTEXT_SET("config", YomkMkPtr(String, ""));          // 被检查器拒绝
```

## 示例3：EventLoop 异步事件处理

```cpp
// 事件处理函数
YomkResponse taskHandler(YomkPkgPtr pkg) {
    YomkUnPackPkgResponse(pkg, String, taskData); // 宏已自动判空
    YOMK_DEBUG_TAG("TaskHandler", "processing in thread: ", std::this_thread::get_id());
    // 处理耗时任务...
    return {YomkResponse::eOk, "task done"};
}

// 启动事件循环
YOMK_EVENTLOOP_START("worker_loop", taskHandler);

// 异步投递（不等待结果）
YOMK_EVENTLOOP_POST("worker_loop", YomkMkPtr(String, "task_1"));

// 同步投递（等待结果）
YomkResponse resp = YOMK_EVENTLOOP_POST_WAIT("worker_loop", YomkMkPtr(String, "task_2"));
if(resp.m_status == YomkResponse::eOk) {
    YomkUnPackPkg(resp.m_data, Event, event);
    if(event) {
        YOMK_INFO("event result: ", event->d.m_response.m_msg);
    }
}

// 清理
YOMK_EVENTLOOP_STOP("worker_loop");
YOMK_EVENTLOOP_DESTROY("worker_loop");
```

## 示例4：FunctionPool 公共函数池

```cpp
// 定义公共函数
YomkResponse validateAmount(YomkPkgPtr pkg) {
    YomkUnPackPkgResponse(pkg, String, amountStr); // 宏已自动判空
    
    double amount = std::stod(amountStr->d);
    if(amount <= 0) return {YomkResponse::eNo, "invalid amount"};
    return {YomkResponse::eOk, "valid"};
}

// 注册到函数池
YOMK_FUNCTIONPOOL_REGISTER("validate_amount", validateAmount);

// 调用函数
YomkResponse resp = YOMK_FUNCTIONPOOL_CALL("validate_amount", YomkMkPtr(String, "100.5"));
if(resp.m_status == YomkResponse::eOk) {
    YOMK_INFO("Amount is valid");
}
```

## 示例5：文件日志系统

```cpp
// 创建文件日志
YOMK_FILE_LOG_CREATE("/var/log/myapp", "app_log");

// 写日志
YOMK_FILE_INFO("app_log", "Application started");
YOMK_FILE_WARN_TAG("app_log", "Security", "Suspicious activity detected");
YOMK_FILE_ERROR("app_log", "Failed to connect: ", errorMsg);

// 刷新到磁盘
YOMK_FILE_LOG_WRITE("app_log");

// 自定义控制台日志代理
bool myLogProxy(const yomk::Log& log) {
    std::cout << "[" << log.m_logger << "] " << log.m_log << std::endl;
    return false; // 不再传递给默认输出
}
YOMK_SET_CONSOLE_LOG_PROXY(myLogProxy);
```