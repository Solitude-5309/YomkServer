# YomkServer 工程示例

> 示例0 / 示例7 以仓库内 `template/project/`、`template/extension/` 为蓝本（模板中 `TemplateProject`/`extension` 即占位符实例），核心代码与模板一致，README 为删节版。**生成工程/扩展时优先复制模板目录**并替换工程名；模板目录不存在时按下文逐文件生成。

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
│   └── YomkMsg.h
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
#include <yomk/server/YomkServerApi.h>
#include <yomk/log/YomkLoggerApi.h>

#include "boot/MyBoot.h"

using namespace yomk::payload;

int main(int argc, char* argv[])
{
    yomk::server().boot_ptr(new MyBoot(argc, argv, {"/ConfigService"})).run();
    yomk::log().info("running, press Enter to exit.");
    getchar();
    return 0;
}
```

### boot/MyBoot.h
```cpp
#pragma once
#include <yomk/server/YomkServerApi.h>

using namespace yomk::payload;

class MyBoot : public YomkBoot
{
public:
    MyBoot(int argc, char* argv[], const std::vector<std::string>& startSrvNames = {})
        : m_argc(argc), m_argv(argv), m_startSrvNames(startSrvNames)
    {
    }
    int before() override;
    int start() override;
    int after() override;

private:
    int m_argc;
    char** m_argv;
    std::vector<std::string> m_startSrvNames;
};
```

### boot/MyBoot.cpp
```cpp
#include <yomk/ctx/YomkContextApi.h>
#include <yomk/log/YomkLoggerApi.h>

#include "MyBoot.h"

#include <filesystem>

#include "services/ConfigService.h"
#include "typedefine/TypeDefine.h"

int MyBoot::before()
{
    yomk::log().tag("MyBoot::before").info("starting...");

    // 通过 /proc/self/exe 获取可执行文件绝对路径
    std::filesystem::path exePath = std::filesystem::read_symlink("/proc/self/exe");
    std::filesystem::path configPath = exePath.parent_path().parent_path() / "config" / "config.txt";
    yomk::ctx().key(CTX_CONFIG_PATH).value(yomk::msgs::String::shared(configPath.string())).create();
    yomk::log().tag("MyBoot::before").info("config path: ", configPath.string());
    return 0;
}

int MyBoot::start()
{
    // 服务创建器映射表
    static const std::map<std::string, std::function<YomkService*()>> serviceCreators = {
        {"/ConfigService", []() { return new ConfigService(yomk::server().instance().get()); }},
    };

    // 根据 m_startSrvNames 按需启动服务
    for (const auto& srvName : m_startSrvNames)
    {
        auto it = serviceCreators.find(srvName);
        if (it != serviceCreators.end())
        {
            if (yomk::server().srv_name(srvName).srv_ptr(it->second()).add() != 0)
                return -1;
        }
    }
    return 0;
}

int MyBoot::after()
{
    // 服务启动后加载配置文件
    yomk::payload::Response resp = yomk::server().url("/ConfigService/load").request();
    if (resp.status != yomk::payload::ResStatus::Succeeded)
    {
        yomk::log().tag("MyBoot::after").error("load config failed: ", resp.msg);
        return -1;
    }
    yomk::log().tag("MyBoot::after").info("started successfully.");

    resp = yomk::server().url("/ConfigService/get").msg(yomk::msgs::ConfigKey::shared(ConfigKey{"name"})).request();
    if (resp.status != yomk::payload::ResStatus::Succeeded)
    {
        yomk::log().tag("MyBoot::after").error("get config name failed: ", resp.msg);
        return -1;
    }

    auto name = yomk::msgs::unpack<yomk::msgs::String>(resp.data);
    yomk::log().tag("MyBoot::after").info("config name: ", name->d);

    // 测试版本号接口：获取并输出工程版本号（来自 CMake project() 定义的 VERSION，编译期注入）
    resp = yomk::server().url("/ConfigService/version").request();
    if (resp.status != yomk::payload::ResStatus::Succeeded)
    {
        yomk::log().tag("MyBoot::after").error("get version failed: ", resp.msg);
        return -1;
    }

    auto version = yomk::msgs::unpack<yomk::msgs::String>(resp.data);
    yomk::log().tag("MyBoot::after").info("project version: ", version->d);

    resp = yomk::server().url("/ConfigService/get").msg(yomk::msgs::ConfigKey::shared(ConfigKey{"description"})).request();
    if (resp.status != yomk::payload::ResStatus::Succeeded)
    {
        yomk::log().tag("MyBoot::after").error("get config description failed: ", resp.msg);
        return -1;
    }

    auto description = yomk::msgs::unpack<yomk::msgs::String>(resp.data);
    yomk::log().tag("MyBoot::after").info("config description: ", description->d);

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
constexpr const char* const CTX_CONFIG_PATH = "config_path";
```

### msgs/YomkMsg.h
```cpp
#pragma once
#include <yomk/msg/YomkMsg.h>

// ConfigService 消息包
// 配置键：用于 /get 和 /reload
struct ConfigKey
{
    std::string key;
};
YOMK_DEFINE_MSG(::ConfigKey, ConfigKey, req);
// 访问: ptr->req.key

// 配置键值：用于 /set
struct ConfigKeyValue
{
    std::string key;
    std::string value;
};
YOMK_DEFINE_MSG(::ConfigKeyValue, ConfigKeyValue, req);
// 访问: ptr->req.key, ptr->req.value
```

### services/ConfigService.h
```cpp
#pragma once
#include <yomk/server/YomkServerApi.h>

#include <unordered_map>

#include "msgs/YomkMsg.h"

using namespace yomk::payload;

class ConfigService : public YomkService
{
public:
    ConfigService(YomkServer* server);
    virtual ~ConfigService() {}
    virtual int init() override;

private:
    yomk::payload::Response loadConfig(YomkMsgPtr pkg);
    yomk::payload::Response getConfig(YomkMsgPtr pkg);
    yomk::payload::Response setConfig(YomkMsgPtr pkg);
    yomk::payload::Response reloadConfig(YomkMsgPtr pkg);
    yomk::payload::Response version(YomkMsgPtr pkg);

    std::string m_configPath;
    std::unordered_map<std::string, std::string> m_configMap;
};
```

### services/ConfigService.cpp
```cpp
#include <yomk/ctx/YomkContextApi.h>
#include <yomk/log/YomkLoggerApi.h>

#include "ConfigService.h"

#include <algorithm>
#include <fstream>

#include "typedefine/TypeDefine.h"

// 去除首尾空白符
static std::string trim(const std::string& str)
{
    auto first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return "";
    auto last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, last - first + 1);
}

ConfigService::ConfigService(YomkServer* server) : YomkService(server)
{
    name("/ConfigService");
}

int ConfigService::init()
{
    // 安装功能函数
    installFunc("/load", &ConfigService::loadConfig);
    installFunc("/get", &ConfigService::getConfig);
    installFunc("/set", &ConfigService::setConfig);
    installFunc("/reload", &ConfigService::reloadConfig);
    installFunc("/version", &ConfigService::version);
    yomk::log().tag("ConfigService::init").info("install func [ /load /get /set /reload /version ] to", name());
    return 0;
}

yomk::payload::Response ConfigService::loadConfig(YomkMsgPtr pkg)
{
    // 从 Context 获取配置文件路径
    auto ctxVal = yomk::ctx().key(CTX_CONFIG_PATH).msg<yomk::msgs::String>().with_default(nullptr).get();
    if (!ctxVal)
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "config_path not found in context");
    m_configPath = ctxVal->d;

    // 加载配置文件（纯文本 key: value 格式）
    std::ifstream ifs(m_configPath);
    if (!ifs.is_open())
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "failed to open: " + m_configPath);

    std::string line;
    while (std::getline(ifs, line))
    {
        if (line.empty())
            continue;
        // 按第一个冒号分割 key:value
        auto colonPos = line.find(':');
        if (colonPos == std::string::npos)
            continue;
        std::string key = trim(line.substr(0, colonPos));
        std::string value = trim(line.substr(colonPos + 1));
        if (!key.empty())
            m_configMap[key] = value;
    }
    ifs.close();
    yomk::log().tag("ConfigService::loadConfig").info("loaded: ", m_configPath);
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok");
}

yomk::payload::Response ConfigService::getConfig(YomkMsgPtr pkg)
{
    auto data = yomk::msgs::unpack<yomk::msgs::ConfigKey>(pkg);
    if (!data)
    {
        return {yomk::payload::ResStatus::Failed, "pkg is null or pkg is not ConfigKey"};
    }

    auto it = m_configMap.find(data->req.key);
    if (it == m_configMap.end())
    {
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "key not found: " + data->req.key);
    }

    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::String::shared(it->second));
}

yomk::payload::Response ConfigService::setConfig(YomkMsgPtr pkg)
{
    auto data = yomk::msgs::unpack<yomk::msgs::ConfigKeyValue>(pkg);
    if (!data)
    {
        return {yomk::payload::ResStatus::Failed, "pkg is null or pkg is not ConfigKeyValue"};
    }

    m_configMap[data->req.key] = data->req.value;
    yomk::log().tag("ConfigService::setConfig").info("set ", data->req.key, " = ", data->req.value);
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok");
}

yomk::payload::Response ConfigService::reloadConfig(YomkMsgPtr pkg)
{
    m_configMap.clear();
    return loadConfig(nullptr);
}

yomk::payload::Response ConfigService::version(YomkMsgPtr pkg)
{
    // 版本号来自 CMake project() 定义的 VERSION，编译期通过 APP_VERSION 宏注入
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::String::shared(std::string(APP_VERSION)));
}
```

### config/config.txt
```text
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
# 编译期注入工程版本号（对应 project() 定义的 VERSION），供 /ConfigService/version 接口返回
target_compile_definitions(${PROJECT_NAME} PRIVATE APP_VERSION="${PROJECT_VERSION}")
target_link_libraries(${PROJECT_NAME} PRIVATE
    YomkServer::YomkServer
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
# PROGRAMS 形态安装（默认 755）：FILES 形态默认 644，setup.bash 装出后无执行位无法直接执行
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/setup.bash
    DESTINATION .
)
```

### build_ubuntu.sh
```bash
#!/bin/bash
# 一键编译脚本（交互式）
# 用法: source build_ubuntu.sh（也兼容直接执行 ./build_ubuntu.sh，两种方式失败均正确退出）
# 交互询问 YomkServer 安装路径（前置路径），默认取环境变量 YOMK_PREFIX_PATH，可修改
# 安装路径固定为工程源码目录下的 install/（CMakeLists.txt 已强制）

# 兼容直接执行：脚本内部以 return 表达失败（source 语义），直接执行时 return 在顶层非法
# 且不会终止脚本（失败后继续打印成功横幅）——转交 source 方式运行并透传退出码
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    bash -c "source '${BASH_SOURCE[0]}'"
    exit $?
fi

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

# 创建并进入 build 目录
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}" || return 1

# 配置
cmake "${SCRIPT_DIR}" -DCMAKE_PREFIX_PATH="${YOMK_SERVER_PATH}"
if [ $? -ne 0 ]; then
    echo "cmake 配置失败"
    cd "${_ORIG_DIR}"
    return 1
fi

# 编译并安装
cmake --build . --config Release --target install
if [ $? -ne 0 ]; then
    echo "编译失败"
    cd "${_ORIG_DIR}"
    return 1
fi

# 加载运行环境
source "${SCRIPT_DIR}/install/setup.bash"

# 恢复原始目录
cd "${_ORIG_DIR}"
unset _ORIG_DIR

echo "编译完成"
```

### setup.bash.in
```bash
#!/bin/bash
# 环境配置脚本
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
- YomkServer 已安装（`build_ubuntu.sh` 安装后自动配置环境变量 `YOMK_PREFIX_PATH` 指向安装路径）

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

### 1. 添加消息包（msgs/YomkMsg.h 追加）
```cpp
// 定义完所有的结构体后，统一注册 YOMK_DEFINE_MSG
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

YOMK_DEFINE_MSG(UserQuery, UserQuery, req);
YOMK_DEFINE_MSG(UserInfo, UserInfo, d);
```

### 2. 创建服务（services/UserService.h）
```cpp
#pragma once
#include <yomk/server/YomkServerApi.h>

#include "msgs/YomkMsg.h"

using namespace yomk::payload;

class UserService : public YomkService
{
public:
    UserService(YomkServer* server);
    virtual ~UserService() {}
    virtual int init() override;

private:
    yomk::payload::Response getUser(YomkMsgPtr pkg);
    yomk::payload::Response createUser(YomkMsgPtr pkg);
};
```

### 3. 实现服务（services/UserService.cpp）
```cpp
#include "UserService.h"

UserService::UserService(YomkServer* server) : YomkService(server)
{
    name("/UserService");
}

int UserService::init()
{
    installFunc("/get_user", &UserService::getUser);
    installFunc("/create_user", &UserService::createUser);
    yomk::log().tag("UserService::init").info("install func [ /get_user /create_user ] to", name());
    return 0;
}

yomk::payload::Response UserService::getUser(YomkMsgPtr pkg)
{
    auto query = yomk::msgs::unpack<yomk::msgs::UserQuery>(pkg);
    if (!query)
    {
        return {yomk::payload::ResStatus::Failed, "pkg is null or pkg is not UserQuery"};
    }
    yomk::log().tag("UserService::getUser").info("query user: ", query->req.userId);

    // 业务逻辑...
    UserInfo info{query->req.userId, "Alice", 25};
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::UserInfo::shared(info));
}

yomk::payload::Response UserService::createUser(YomkMsgPtr pkg)
{
    auto user = yomk::msgs::unpack<yomk::msgs::UserInfo>(pkg);
    if (!user)
    {
        return {yomk::payload::ResStatus::Failed, "pkg is null or pkg is not UserInfo"};
    }
    yomk::log().tag("UserService::createUser").info("create user: ", user->d.name);

    // 跨服务调用示例：读取配置（请求域链，单表达式内联）
    yomk::payload::Response cfgResp = yomk::server().url("/ConfigService/get")
                                         .msg(yomk::msgs::ConfigKey::shared(ConfigKey{"server.name"})).request();

    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "user created");
}
```

### 4. 注册到 Boot（boot/MyBoot.cpp 修改）
```cpp
// 头部添加
#include "services/UserService.h"

// start() 映射表中添加
{"/UserService", []() { return new UserService(yomk::server().instance().get()); }},
```

### 5. 启动列表（main.cpp 修改）
```cpp
yomk::server().boot_ptr(new MyBoot(argc, argv, {"/ConfigService", "/UserService"})).run();
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
yomk::payload::CheckStatus nonEmptyChecker(const yomk::payload::Context& ctx)
{
    auto val = yomk::msgs::unpack<yomk::msgs::String>(ctx.value);
    if (!val || val->d.empty())
        return yomk::payload::CheckStatus::Reject;
    return yomk::payload::CheckStatus::Accept;
}

// 监控函数：记录变更（仅通知发生了一次 set，回传该次键值快照）
void changeLogger(const yomk::payload::Context& ctx)
{
    auto val = yomk::msgs::unpack<yomk::msgs::String>(ctx.value);
    if (!val)
    {
        return;
    }
    yomk::log().tag("CtxMonitor").info("key=", ctx.key, " new_value=", val->d);
}

// 使用：键级回调只能注册到已存在的键，先 create 再注册
yomk::ctx().key("config").value(yomk::msgs::String::shared("default")).create();
yomk::ctx().key("config").checker_fn(nonEmptyChecker).apply();
yomk::ctx().key("config").monitor_fn(changeLogger).async(true).apply();   // .async(true) 省略 = 同步

// 组级开关建组默认关，须显式开启（缺省组名 "main"）
yomk::ctx().group("main").checker().enable();
yomk::ctx().group("main").monitor().enable();

yomk::ctx().key("config").value(yomk::msgs::String::shared("new_value")).set();  // 通过检查并触发监控
yomk::ctx().key("config").value(yomk::msgs::String::shared("")).set();            // 被拒绝（Failed "set context key '{k}' rejected by checker"）

// 类型化读（只读快照；未命中回退 with_default 的默认值）
auto val = yomk::ctx().key("config").msg<yomk::msgs::String>().with_default(nullptr).get();
```

checker/monitor 使用**服务成员函数**时必须弱绑定，直接用 `weakFunc(&Class::Method)`（按目标类型自动适配），服务删除后 checker 默认放行、monitor 丢弃：

```cpp
int MyService::init() {
    // 前提：键已存在（不存在时 apply 返回 Failed "context key '{k}' does not exist"）
    yomk::ctx().key("config").value(yomk::msgs::String::shared("v")).create();
    yomk::ctx().key("config").checker_fn(weakFunc(&MyService::myChecker)).apply();
    yomk::ctx().key("config").monitor_fn(weakFunc(&MyService::myMonitor)).apply();
    return 0;
}
```

同步/异步 monitor 的值语义相同（都回传该次 set 的临时快照），区别在阻塞性与顺序保证：

- **共同语义**：
  - monitor 仅通知发生了一次 set 事件，并回传该次 set 的键值快照——**不保证快照是最新值**；快照仅回调期有效，需留存请在回调内拷贝 ctx。
  - **值对象一经 set 发布即只读**：get/monitor 拿到的都别名同一对象；改动请构造新对象再 `.set()`，勿原地修改（否则与并发读竞争、可能崩溃）。
  - 需最新值请在回调内用 `.msg<T>().with_default(...).get()` 重读；校验/拒绝请用 checker，勿用 monitor。
- **同步（默认，`.async()` 省略或 false）**：
  - 在 set 调用线程上锁外内联执行、通知及时；顺序 set 下按调用序通知，**并发 set 下不保证跨线程顺序**。
  - 回调内可安全调用 `get()`/`.keys()` 等，但**重入 `.set()`（含跨键环）会递归**，不收敛长链将栈溢出。
- **异步（`.async(true)`）**：
  - 在 set 写锁内投入该组自持的单线程监控池、set 返回后延迟执行；**入队序=提交序 ⇒ 恒按 set 提交序送达（并发 set 亦然）**，随服务 deinit / `yomk::server().shutdown()` 排空停止、不丢。
  - 适合耗时回调（跨服务/落盘/通知）解耦；池线程迭代执行、平栈不溢出，**更适合会在回调内再 set 的状态机/级联回写**（仍需保证收敛，否则任务自续活锁）。

> 提示：正式工程建议 Context key 用 typedefine 常量定义（示例0 的 `CTX_CONFIG_PATH` 即范例），避免散落的裸字符串。

## 示例3：EventLoop 异步事件处理

```cpp
// 事件处理函数
yomk::payload::Response taskHandler(YomkMsgPtr pkg)
{
    auto taskData = yomk::msgs::unpack<yomk::msgs::String>(pkg);
    if (!taskData)
    {
        return {yomk::payload::ResStatus::Failed, "pkg is null or pkg is not String"};
    }
    yomk::log().tag("TaskHandler").debug("processing in thread: ", std::this_thread::get_id());
    return {yomk::payload::ResStatus::Succeeded, "task done"};
}

// 启动事件循环（handle_fn = 循环默认处理函数；msg_name 可加声明期望类型，仅内省元数据）
yomk::event_loop().loop("worker_loop").handle_fn(taskHandler).msg_name("String").start();

// 异步投递（走默认处理函数；tag 可选，仅内省可见）
yomk::event_loop().msg(yomk::msgs::String::shared("task_1")).loop("worker_loop").post();
yomk::event_loop().msg(yomk::msgs::String::shared("task_tagged")).loop("worker_loop").tag("billing").post();

// 单次事件指定临时处理器（优先于默认处理函数）
yomk::event_loop().msg(yomk::msgs::String::shared("task_2")).loop("worker_loop")
    .handle_fn([](YomkMsgPtr pkg) { return yomk::payload::Response{yomk::payload::ResStatus::Succeeded, "one-shot done"}; })
    .post();

// 同步投递（阻塞等待事件执行完成；返回 ResStatus 仅表示受理/完成状态，
// 执行结果体现在处理函数内部——取回结果写共享 Context 或在处理函数内回调投递）
yomk::payload::ResStatus rs = yomk::event_loop().msg(yomk::msgs::String::shared("task_3"))
                                  .loop("worker_loop").post_wait();

// 内省：状态 + 队列头 5 条 tag 采样
yomk::event_loop().loop("worker_loop").tag_count(5).get_info();

// 停止：仅退出工作线程，未执行的排队事件保留在队列（再次 start() 续跑，事件不丢失；
// 注意同名 start() 复用既有实例，本次累积的 handle_fn/msg_name 不生效）
yomk::event_loop().loop("worker_loop").stop();

// 销毁：先停止退出工作线程、再清空未执行的排队事件（不可续跑），彻底释放
yomk::event_loop().loop("worker_loop").destroy();
```

失败契约：循环不存在 `Failed` "event loop '{n}' does not exist"；已停止（未销毁）时投递 `Failed` "event loop '{n}' not running, post rejected"（post_wait 同款 "...post wait rejected"）。

## 示例4：FunctionPool 公共函数池

```cpp
// 定义公共函数
yomk::payload::Response validateAmount(YomkMsgPtr pkg)
{
    auto amountStr = yomk::msgs::unpack<yomk::msgs::String>(pkg);
    if (!amountStr)
    {
        return {yomk::payload::ResStatus::Failed, "pkg is null or pkg is not String"};
    }
    double amount = std::stod(amountStr->d);
    if (amount <= 0)
        return {yomk::payload::ResStatus::Failed, "invalid amount"};
    return {yomk::payload::ResStatus::Succeeded, "valid"};
}

// 注册（msg_name/description 可省略，仅内省元数据）+ 调用（阻塞至函数执行完成）
yomk::fn_pool().fn_name("validate_amount").fn(validateAmount).msg_name("String").install();
yomk::payload::Response resp = yomk::fn_pool().fn_name("validate_amount").msg(yomk::msgs::String::shared("100.5")).call();

// 同名重复 install 即运行时热替换，无需先注销（返回 Succeeded）
yomk::fn_pool().fn_name("validate_amount").fn(newValidator).install();

// 注销（注销后调用返回 ResStatus::Failed）
yomk::fn_pool().fn_name("validate_amount").uninstall();
```

服务成员函数注册到 FunctionPool 时必须用 `weakFunc(&Class::Method)` 弱绑定，服务删除（`srv_name().remove()`）后回调自动失效（返回 `Failed`）而非悬垂 this 崩溃：

```cpp
int MyService::init() {
    yomk::fn_pool().fn_name("my_work").fn(weakFunc(&MyService::doWork)).install();
    return 0;
}

void MyService::deinit() {
    // 可选：删除服务时框架自动调用，也可主动注销外部注册的回调
    yomk::fn_pool().fn_name("my_work").uninstall();
}
```

## 示例5：服务内省（调试）

内省类型元数据由 installFunc 第三参声明、函数说明由第四参声明（手写字符串字面量，仅作内省元数据，不参与运行时校验）：

```cpp
int DemoService::init()
{
    installFunc("/with_type", &DemoService::withType, "String");  // 内省可见类型 String
    installFunc("/with_desc", &DemoService::withType, "String", "对函数的说明");  // 四参：内省可见类型 + 说明
    installFunc("/no_type", &DemoService::noType);                // 不声明类型，内省无类型
    return 0;
}

// 全部服务名清单（StringArray）
yomk::payload::Response resp = yomk::srv_info().services();
auto arr = yomk::msgs::unpack<yomk::msgs::StringArray>(resp.data);  // arr->d: ["/DemoService", "/YomkServerInfo", ...]

// 指定服务的函数清单（FuncInfoList 结构化清单：funcName/msgName/description，msg 恒 "ok"）
resp = yomk::srv_info().srv_name("/DemoService").functions();

// 单函数元信息（结构化 FuncInfo 包，msg 恒 "ok"）
resp = yomk::srv_info().fn_url("/DemoService/with_type").get_info();  // Succeeded，msgName = "String"
resp = yomk::srv_info().fn_url("/DemoService/with_desc").get_info();  // Succeeded，msgName = "String"，description = "对函数的说明"
resp = yomk::srv_info().fn_url("/DemoService/not_exist").get_info();  // Failed

// 全量拓扑（ServerServiceInfoList 层级包：serviceName 服务名 + funcs 函数元信息列表）
resp = yomk::srv_info().all();
```

说明：FunctionPool 动态注册的函数无类型元数据时内省显示为空；模块内层内省由各模块自行实现（四个内置模块均已完成，见下）。

### Context 模块内省

```cpp
yomk::ctx().key("str_key").value(yomk::msgs::String::shared("v1")).create();
yomk::ctx().key("str_key").checker_fn(myChecker).apply();
yomk::ctx().key("str_key").monitor_fn(myMonitor).async(true).apply();

// 键名清单（StringArray；末位可选组名，缺省 main）
yomk::payload::Response resp = yomk::ctx().keys();                // arr->d: ["str_key"]

// 单键元信息：结构化 ContextKeyInfo 包（key/typeName/checkerOn/monitorCount/asyncMonitorCount/group，msg 恒 "ok"）
resp = yomk::ctx().key("str_key").info();     // Succeeded
resp = yomk::ctx().key("not_exist").info();   // Failed

// 组名清单（StringArray）与跨组聚合（ContextGroupInfoList：组名 groupName + 该组全部键元信息 keys）
resp = yomk::ctx().groups();
resp = yomk::ctx().all();
```

注意：`.create()` 值为空被拒绝（`Failed` "create context key '{k}' failed: value is empty"），该键不会出现在内省结果中；内省只读，取实际值走 `.msg<T>().with_default(...).get()`。

### EventLoop 模块内省

Event 可携带 tag 标记（`post` 链上 `.tag(t)` 可选，缺省空）；启动时可用 `.msg_name("String")` 声明默认处理函数期望的消息类型（仅内省元数据）：

```cpp
yomk::event_loop().loop("loop_a").start();
yomk::event_loop().loop("loop_b").handle_fn(defaultHandle).msg_name("String").start();

// 投递事件时可选打 tag（仅内省标记，不参与路由/处理）
yomk::event_loop().msg(yomk::msgs::String::shared("data")).loop("loop_a").handle_fn(myHandle).tag("tag1").post();

// 循环名清单（StringArray 字典序）
yomk::payload::Response resp = yomk::event_loop().list();   // arr->d: ["loop_a", "loop_b"]

// 单循环元信息：EventLoopInfo（loopName/running/pending/hasDefaultFunc/msgName/nextTags）
// nextTags 存原值取 n 条（队列不足 n 时全部列出），n 缺省 3；msg 恒 "ok"
resp = yomk::event_loop().loop("loop_a").get_info();
resp = yomk::event_loop().loop("loop_a").tag_count(5).get_info();
resp = yomk::event_loop().loop("not_exist").get_info();     // Failed

// 全量循环元信息（EventLoopInfoList，元素为单查同款）
resp = yomk::event_loop().all();
```

注意：内省只读；观察队列积压 tag 需保持循环运行（`stop()` 保留队列，`destroy()` 清空）。

### FunctionPool 模块内省

注册时可用 `.msg_name(m)` 声明期望消息类型、`.description(d)` 追加函数说明（与 installFunc 第三/四参同款语义，仅内省元数据）：

```cpp
yomk::fn_pool().fn_name("func_a").fn(funcA).msg_name("String").install();
yomk::fn_pool().fn_name("func_desc").fn(funcA).msg_name("String").description("函数说明").install();
yomk::fn_pool().fn_name("func_b").fn(funcB).install();

// 注册函数名清单（StringArray）
yomk::payload::Response resp = yomk::fn_pool().list();     // arr->d: ["func_a", "func_b", "func_desc"]

// 单函数元信息（结构化 FuncInfo 包，msg 恒 "ok"；未注册 Failed）
resp = yomk::fn_pool().fn_name("func_a").get_info();       // funcName="func_a" msgName="String"
resp = yomk::fn_pool().fn_name("not_exist").get_info();    // Failed

// 全量函数元信息（FuncInfoList）
resp = yomk::fn_pool().all();
```

注意：内省只读；注销后函数立即从内省结果中消失。

### Logger 模块内省

```cpp
yomk::log_mgr().dir("/tmp").file("info_logger").create();          // 创建文件日志器
yomk::log().tag("auto_logger").info("hello");  // tag 即控制台日志器名：按需自动创建并显示 [auto_logger]

// 日志器名清单：按类型查询（StringArray，纯名、字典序）
yomk::payload::Response resp = yomk::log_mgr().console().list();
// arr->d: ["auto_logger", "main"]（main 为框架预创建的默认日志器）
resp = yomk::log_mgr().file().list();
// arr->d: ["info_logger"]

// 单器状态（LoggerInfo：loggerName/type/dir，msg 恒 "ok"；两表允许同名，须按类型精确单查）
resp = yomk::log_mgr().console("main").get_info();       // Succeeded，type="console"，dir 空串
resp = yomk::log_mgr().file("info_logger").get_info();   // Succeeded，type="file"，dir="/tmp"
resp = yomk::log_mgr().console("not_exist").get_info();  // Failed

// 全量清单（LoggerInfoList，每条内嵌 LoggerState 状态段=逐器门控层真值 + proxyOn 报对应侧代理态）
resp = yomk::log_mgr().all();

// 全局主闸状态单查（LoggerState：六级别开关 + proxyOn；独立于逐器门控层，两层归因）
resp = yomk::log_mgr().console().status();   // 控制台侧主闸 + 控制台代理态
resp = yomk::log_mgr().file().status();      // 文件侧主闸 + 文件代理态（两套独立）

// 逐器门控（.level(lv) 缺省 Info；具名未命中 Failed 不惰性创建）
yomk::log_mgr().console("auto_logger").level(yomk::payload::LogLevel::Info).disable();  // 仅关该器 INFO
yomk::log_mgr().console("auto_logger").level(yomk::payload::LogLevel::Info).enable();   // 恢复
```

## 示例6：文件日志与门控/代理

```cpp
// 创建文件日志器（.dir() 须先于 .file() 累积），再经 to_sink 写入，最后显式刷盘
yomk::log_mgr().dir("./logs").file("app").create();
yomk::log().to_sink("app").info("Application started");
yomk::log().to_sink("app").tag("Security").warn("Suspicious activity");   // .tag 可与 to_sink 叠加
yomk::log_mgr().file("app").flush();

// 文件门控两级 AND：无具名 = 全局主闸，具名 = 仅该日志器（未命中 Failed 不惰性创建）
yomk::log_mgr().file("app").level(yomk::payload::LogLevel::Info).disable();
yomk::log().to_sink("app").info("hidden while off");    // 被门控吞掉，缓冲无此行
yomk::log_mgr().file("app").level(yomk::payload::LogLevel::Info).enable();

// 删除日志器：同名 console/file 两表一并清除（均无命中才 Failed）；移除时析构自动落盘，
// 但不删除磁盘上的 .log 文件（数据保全，清理归调用方）
yomk::payload::Response del = yomk::log_mgr().file("app").remove();   // Succeeded, msg: "deleted console:0 file:1"
yomk::log_mgr().file("app").remove();                                 // Failed, msg: "logger not found."
// 注：默认日志器 "main" 无特殊保护，删除后下次写控制台日志惰性重建

// 控制台日志代理
bool myLogProxy(const yomk::payload::Log& log)
{
    std::cout << "[" << log.logger << "] " << log.log << std::endl;
    return false;   // 消费该条，不再走框架默认输出
}
yomk::log_mgr().console().proxy(myLogProxy).apply();
yomk::log_mgr().console().proxy(nullptr).apply();   // 卸载代理恢复默认输出；内省状态段 proxyOn 随之回 false

// 文件日志代理：与控制台代理相互独立，false 拦截该条不落盘、true 穿透默认落盘
yomk::log_mgr().file().proxy([](const yomk::payload::Log& log) {
    return log.level != yomk::payload::LogLevel::Debug;   // 拦截 DEBUG 不落盘，其余放行
}).apply();
yomk::log_mgr().file().proxy(nullptr).apply();

// 位置前缀三开关（进程级，控制台与文件同时生效；初值承接当前状态，apply 一次性落定）
yomk::log_mgr().location().show_file(true).show_func(true).apply();   // [file:line:func] 以 ':' 拼接
yomk::log_mgr().location().show_line(false).apply();                  // 关断段整体省略
```

输出行格式：`[YYYY-MM-DD HH:MM:SS.mmm] [INFO ] [tag] [42] 内容`（级别定宽 5；默认前缀仅行号）。

---

## 示例7：标准扩展模板（创建新扩展时必须生成）

将 `ExtensionName` 替换为用户指定的扩展名，所有文件必须完整生成。

### 目录结构
```
ExtensionName/
├── include/
│   └── extension/            // 小写扩展名目录，安装后对外引用统一 <yomk/<扩展名小写>/...>
│       ├── ExtensionService.h
│       ├── ExtensionApi.h    // 模块 API：链式域入口 yomk::ext()（四件套，参考 include/yomk 各域设计）
│       ├── ExtensionMsg.h    // 模块消息包目录（YOMK_DEFINE_MSG，模板为注释占位）
│       ├── ExtensionPayload.h // 模块专属载荷目录（namespace yomk::payload，模板为注释占位）
│       └── ExtensionUrl.h    // 模块 URL 常量（yomk::url 命名空间）
├── src/
│   └── ExtensionService.cpp
├── test/
│   ├── CMakeLists.txt
│   └── TestExtensionName.cpp
├── cmake/
│   └── ProjectConfig.cmake.in
├── build_ubuntu.sh
├── CMakeLists.txt
└── README.md
```

头文件分层（推荐规则，默认遵循，不强制）：`include/` 只放导出给下游的对外接口头文件（服务类声明）；内部头文件（辅助类、内部数据结构等实现细节）直接放 `src/`，`install(DIRECTORY include/ ...)` 只安装 `include/` 内容，内部头文件天然不安装、对下游不可见。对外头文件放 `include/<扩展名小写>/`（安装后 `<yomk/<扩展名小写>/...>` 引用，与 SDK 各域共用 `<prefix>/include` 同一 include 根）。

### include/extension/ExtensionUrl.h
```cpp
#pragma once
#include <string_view>

// /ExtensionService/* 扩展服务 URL 常量：与 SDK 内置模块同风格（参考 yomk/fn_pool/YomkFunctionPoolUrl.h），
// inline constexpr std::string_view 编译期常量，请求链全程 string_view 透传，调用路径零堆分配。
// yomk::url 为开放命名空间：扩展的 url 常量按同风格直接追加。
namespace yomk::url
{
inline constexpr std::string_view extVersion = "/ExtensionService/version";
}  // namespace yomk::url
```

### include/extension/ExtensionPayload.h
```cpp
#pragma once
#include <string>

#include "yomk/server/YomkServerPayload.h"

// Extension 扩展专属载荷（namespace yomk::payload，与 SDK 各 Yomk{Module}Payload.h 同一设计）。
// 当前模板仅 /version 接口（返回值复用标准类型 msgs::String），暂无专属载荷；
// 新增业务数据结构在此定义，形态参考 yomk/fn_pool/YomkFunctionPoolPayload.h：
//
// struct ExtTask
// {
//     std::string taskName;
//     YomkMsgPtr pkg;
// };
```

### include/extension/ExtensionMsg.h
```cpp
#pragma once

#include "ExtensionPayload.h"
#include "yomk/msg/YomkMsg.h"

// Extension 扩展专属消息包（成员名均为 d，与 SDK 各 Yomk{Module}Msg.h 同一设计）。
// 当前模板 /version 返回值复用标准类型 msgs::String，暂无专属消息；
// 新增业务消息在此用 YOMK_DEFINE_MSG 声明，参考 yomk/fn_pool/YomkFunctionPoolMsg.h：
//
// YOMK_DEFINE_MSG(yomk::payload::ExtTask, ExtTask, d);
```

### include/extension/ExtensionApi.h
```cpp
#pragma once
#include "ExtensionPayload.h"
#include "ExtensionUrl.h"
#include "yomk/server/YomkServerApi.h"

// Extension 模块 API 头（四件套之 Api，与 SDK 各 Yomk{Module}Api.h 同一链式域入口设计）。
// 设计约定：
// 1. Api 头是模块自己的 API，只写本模块业务的终结方法（模板仅 version()），
//    不重复封装通用请求链（url/msg/request 归 yomk::server()）；
// 2. 也可以完全不写 API 方法，但须在本文件注释写清统一调用方式及 url 的功能作用，
//    如：yomk::server().url(yomk::url::extVersion).request() —— extVersion 即 /ExtensionService/version 版本查询；
// 3. 请求一律优先使用统一请求接口 yomk::server().url(<url>).request()，
//    扩展终结方法只是其上的薄封装（如 yomk::ext().version()），屏蔽裸 URL、不另造请求通道。
namespace yomk::detail
{
class ExtBuilder
{
public:
    /**
     * @brief 版本查询终结
     * @return Response 应答包; 未初始化返回 Invalid
     */
    yomk::payload::Response version()
    {
        return yomk::server().url(yomk::url::extVersion).request();
    }
};
}  // namespace yomk::detail

namespace yomk
{
/**
 * @brief Extension 扩展域链式入口
 * @return ExtBuilder（按值, 仅用于单表达式链式调用）
 */
inline detail::ExtBuilder ext()
{
    return detail::ExtBuilder{};
}
}  // namespace yomk
```

### include/extension/ExtensionService.h
```cpp
#pragma once
#include <yomk/server/YomkServerApi.h>

using namespace yomk::payload;

class ExtensionService : public YomkService
{
public:
    ExtensionService(YomkServer* server);
    virtual ~ExtensionService() {}
    virtual int init() override;

private:
    yomk::payload::Response version(YomkMsgPtr pkg);
};
```

### src/ExtensionService.cpp
```cpp
#include <yomk/log/YomkLoggerApi.h>

#include "ExtensionService.h"

ExtensionService::ExtensionService(YomkServer* server) : YomkService(server)
{
    name("/ExtensionService");
}

int ExtensionService::init()
{
    installFunc("/version", &ExtensionService::version);
    yomk::log().tag("ExtensionService::init").info("install func [ /version ] to", name());
    return 0;
}

yomk::payload::Response ExtensionService::version(YomkMsgPtr pkg)
{
    std::string version = "ExtensionName v" EXTENSION_VERSION;
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::String::shared(version));
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

# 测试程序仅供扩展开发验证使用，仅编译到 test/build/，不随扩展安装
```

### test/TestExtensionName.cpp
```cpp
#include <yomk/extension/ExtensionApi.h>
#include <yomk/extension/ExtensionService.h>
#include <yomk/server/YomkServerApi.h>

#include <iostream>

using namespace yomk::payload;

static int g_pass = 0;
static int g_fail = 0;

int main(int argc, char* argv[])
{
    yomk::server().init();

    yomk::server().srv<ExtensionService>().create();

    // 测试版本查询（url 常量见 ExtensionUrl.h，经 ExtensionApi.h 已引入）
    yomk::payload::Response resp = yomk::server().url(yomk::url::extVersion).request();
    if (resp.status == yomk::payload::ResStatus::Succeeded)
    {
        auto version = yomk::msgs::unpack<yomk::msgs::String>(resp.data);
        std::cout << "[PASS] version: " << version->d << std::endl;
        g_pass++;
    }
    else
    {
        std::cout << "[FAIL] version request failed: " << resp.msg << std::endl;
        g_fail++;
    }

    // 测试扩展链式域入口（四件套 ExtensionApi.h 的 yomk::ext()）
    yomk::payload::Response extResp = yomk::ext().version();
    if (extResp.status == yomk::payload::ResStatus::Succeeded)
    {
        auto extVersion = yomk::msgs::unpack<yomk::msgs::String>(extResp.data);
        std::cout << "[PASS] ext().version(): " << extVersion->d << std::endl;
        g_pass++;
    }
    else
    {
        std::cout << "[FAIL] ext().version() request failed: " << extResp.msg << std::endl;
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

> **补充第三方依赖**：模板默认只有 YomkServer 一个依赖。扩展以 PUBLIC 链接的额外第三方库分两类——自带 CMake 包的：必须在 `find_dependency(YomkServer)` 之后（同样处于路径固化之后的区域）追加 `find_dependency(<第三方包>)`，使下游的裸库名解析为带全路径的导入 target，否则任何下游工程链接 `ExtensionName::ExtensionName` 都会因找不到库而失败；不带包、仅以裸库名 `-l` 链接的（如 fastddsgen 生成库，测试工程以裸名链接）：导出配置不记录它，由测试工程用 `link_directories(${ExtensionName_LIB_DIR})` 补库搜索路径。

### build_ubuntu.sh
```bash
#!/bin/bash
# 一键编译脚本（交互式）
# 用法: source build_ubuntu.sh（也兼容直接执行 ./build_ubuntu.sh，两种方式失败均正确退出）
# 依次交互询问 YomkServer 安装路径（前置路径）与扩展安装路径，默认均取环境变量 YOMK_PREFIX_PATH，可修改
# 扩展库与 YomkServer 安装到一起（头文件由 YomkServer::YomkServer 的 INTERFACE include 统一提供）
# 安装后将扩展 lib 注册到系统动态库搜索路径（复用 yomk.conf）并刷新 ldconfig 缓存，新开任意终端即可找到扩展 so

# 兼容直接执行：脚本内部以 return 表达失败（source 语义），直接执行时 return 在顶层非法
# 且不会终止脚本（失败后继续打印成功横幅）——转交 source 方式运行并透传退出码
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    bash -c "source '${BASH_SOURCE[0]}'"
    exit $?
fi

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
# 清除历史运行可能遗留的暂存安装目录（如 build/install/）——其中陈旧的包配置/头文件
# 会经 find_package 缓存（ExtensionName_DIR）或目录搜索遮蔽真实安装路径，导致用旧 API 编译
rm -rf "${BUILD_DIR}/install"
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
    if ! echo "${INSTALL_DIR}/lib" | sudo tee -a "${YOMK_LDCONF_FILE}" >/dev/null; then
        echo "错误: 动态库搜索路径注册失败"
        cd "${_ORIG_DIR}"
        return 1
    fi
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

    # -U 清除上次运行缓存下来的包定位（ExtensionName_DIR/YomkServer_DIR），
    # 强制按本次 CMAKE_PREFIX_PATH 重新查找，避免 find_package 钉死在陈旧安装目录
    cmake "${TEST_DIR}" -DCMAKE_PREFIX_PATH="${INSTALL_DIR};${YOMK_SERVER_PATH}" \
        -UExtensionName_DIR -UYomkServer_DIR
    if [ $? -ne 0 ]; then
        echo "测试程序 cmake 配置失败"
        cd "${_ORIG_DIR}"
        return 1
    fi

    ${SUDO} cmake --build . --config Release
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
    echo "测试程序已编译到 ${TEST_BUILD_DIR}，可在该目录运行 TestExtensionName 验证"
fi
```

### CMakeLists.txt
```cmake
cmake_minimum_required(VERSION 3.14)
project(ExtensionName VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(YomkServer REQUIRED)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# 编译为动态库
add_library(${PROJECT_NAME} SHARED
    src/ExtensionService.cpp
)
target_link_libraries(${PROJECT_NAME} PRIVATE YomkServer::YomkServer)
target_compile_definitions(${PROJECT_NAME} PRIVATE EXTENSION_VERSION="${PROJECT_VERSION}")

# 安装规则
# INCLUDE_INSTALL_DIR 是导出 target 的 INTERFACE include 根（<prefix>/include），与 YomkServer::YomkServer 同根，
# 对外引用统一 <yomk/<扩展名小写>/...>；头文件物理安装位置由下方 install(DIRECTORY) 单独定为 include/yomk/<扩展名小写>/
set(INCLUDE_INSTALL_DIR "include")
set(LIB_INSTALL_DIR "lib")

install(TARGETS ${PROJECT_NAME}
    EXPORT ${PROJECT_NAME}Targets
    LIBRARY DESTINATION ${LIB_INSTALL_DIR}
    INCLUDES DESTINATION ${INCLUDE_INSTALL_DIR}
)
install(DIRECTORY include/ DESTINATION include/yomk)

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
| `/ExtensionService/version` | 版本查询 | 返回扩展版本信息 |

## 前置条件

- C++17 编译器
- CMake >= 3.14
- YomkServer 已安装（通过 `build_ubuntu.sh` 安装后会自动配置环境变量 `YOMK_PREFIX_PATH` 指向安装路径）

## 编译

```bash
source build_ubuntu.sh
```

> 交互式编译：依次询问 YomkServer 安装路径（前置路径）与扩展安装路径，默认均取 `$YOMK_PREFIX_PATH`，可修改。扩展库与 YomkServer 安装到一起（头文件由 `YomkServer::YomkServer` 的 INTERFACE include 统一提供）。测试程序仅编译到 `test/build/` 供验证，不随扩展安装。

## 工程结构

```
ExtensionName/
├── include/
│   └── extension/           # 小写扩展名目录（安装后对外引用统一 <yomk/<扩展名小写>/...>）
│       └── ExtensionService.h  # 对外接口头文件（服务类声明；内部头文件放 src/）
├── src/
│   └── ExtensionService.cpp
├── test/
│   ├── CMakeLists.txt      # 测试程序构建
│   └── TestExtensionName.cpp
├── cmake/
│   └── ProjectConfig.cmake.in
├── CMakeLists.txt
├── build_ubuntu.sh         # 一键编译脚本（交互式）
└── README.md
```

## 使用示例

将以下完整程序拷贝为 main.cpp，安装扩展后可直接编译运行：

```cpp
#include <yomk/server/YomkServerApi.h>
#include <yomk/extension/ExtensionUrl.h>
#include <yomk/extension/ExtensionService.h>
#include <iostream>

using namespace yomk::payload;

int main(int argc, char* argv[])
{
    yomk::server().init();

    // 注册 ExtensionService（扩展服务需先注册才能使用）
    yomk::server().srv<ExtensionService>().create();

    // 版本查询请求（url 常量见 ExtensionUrl.h）
    yomk::payload::Response resp = yomk::server().url(yomk::url::extVersion).request();
    if (resp.status == yomk::payload::ResStatus::Succeeded)
    {
        auto version = yomk::msgs::unpack<yomk::msgs::String>(resp.data);
        std::cout << "version: " << version->d << std::endl;   // 输出: ExtensionName v0.0.1
    }

    return 0;
}
```
````
