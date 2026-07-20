# YomkServer 完整工程示例

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