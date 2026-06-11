# YomkServer 完整工程示例

## 示例1：模块化多服务工程

展示一个典型的模块化工程结构，包含多个Service协作、Context共享状态、EventLoop异步处理。

### 工程目录结构
```
src/
├── main.cpp
├── services/
│   ├── UserService.h
│   ├── UserService.cpp
│   ├── OrderService.h
│   ├── OrderService.cpp
│   └── NotificationService.h
│   └── NotificationService.cpp
├── messages/
│   └── Messages.h          // 所有消息包定义
├── utils/
│   └── CommonFunctions.h   // FunctionPool公共函数
└── CMakeLists.txt
```

### Messages.h — 消息包定义
```cpp
#pragma once
#include "YomkAPI.h"
using namespace yomk;

// 用户相关消息
struct UserCreateReq {
    std::string username;
    std::string email;
};
YomkMsg(UserCreateReq, UserCreateReq)

struct UserCreateResp {
    std::string userId;
    bool success;
};
YomkMsg(UserCreateResp, UserCreateResp)

// 订单相关消息
struct OrderCreateReq {
    std::string userId;
    std::string product;
    double amount;
};
YomkMsg(OrderCreateReq, OrderCreateReq)

// 通知消息
struct NotifyMsg {
    std::string target;
    std::string content;
};
YomkMsg(NotifyMsg, NotifyMsg)
```

### UserService.h — 用户服务
```cpp
#pragma once
#include "Messages.h"

class UserService : public YomkService
{
public:
    UserService(YomkServer* server) : YomkService(server) {
        name("/UserService");
    }
    virtual ~UserService() {}

    virtual int init() {
        YomkInstallFunc("/create_user", UserService::createUser);
        YomkInstallFunc("/get_user", UserService::getUser);

        // 初始化用户计数Context
        YOMK_CONTEXT_CREATE("user_count", YomkMkPtr(string, "0"));
        YOMK_INFO_TAG("UserService::init", "UserService initialized");
        return 0;
    }

private:
    YomkResponse createUser(YomkPkgPtr pkg) {
        YomkUnPackPkgResponse(pkg, UserCreateReq, req);
        if(!req) {
            return YomkResponse(YomkResponse::eInvalid, "invalid request");
        }

        YOMK_INFO_TAG("UserService::createUser", "creating user: ", req->d.username);

        // 更新用户计数（通过Context）
        YomkPtr(string) count = YOMK_CONTEXT_GET(Yomk(string), "user_count", YomkMkPtr(string, "0"));
        int newCount = std::stoi(count->d) + 1;
        YOMK_CONTEXT_SET("user_count", YomkMkPtr(string, std::to_string(newCount)));

        // 异步发送通知（通过EventLoop）
        YOMK_EVENTLOOP_POST("notification_loop",
            YomkMkPtr(NotifyMsg, NotifyMsg{req->d.email, "Welcome!"}));

        return YomkResponse(YomkResponse::eOk, "user created",
            YomkMkPtr(UserCreateResp, UserCreateResp{"U001", true}));
    }

    YomkResponse getUser(YomkPkgPtr pkg) {
        // 实现获取用户逻辑
        return YomkResponse(YomkResponse::eOk, "user found");
    }
};
```

### OrderService.h — 订单服务（跨服务调用）
```cpp
#pragma once
#include "Messages.h"

class OrderService : public YomkService
{
public:
    OrderService(YomkServer* server) : YomkService(server) {
        name("/OrderService");
    }
    virtual ~OrderService() {}

    virtual int init() {
        YomkInstallFunc("/create_order", OrderService::createOrder);
        YOMK_INFO_TAG("OrderService::init", "OrderService initialized");
        return 0;
    }

private:
    YomkResponse createOrder(YomkPkgPtr pkg) {
        YomkUnPackPkgResponse(pkg, OrderCreateReq, req);
        if(!req) {
            return YomkResponse(YomkResponse::eInvalid, "invalid request");
        }

        // 跨服务调用：调用公共函数验证
        YomkResponse validResp = YOMK_FUNCTIONPOOL_CALL("validate_amount",
            YomkMkPtr(string, std::to_string(req->d.amount)));
        if(validResp.m_resStatus != YomkResponse::eOk) {
            return YomkResponse(YomkResponse::eErr, "amount validation failed");
        }

        YOMK_INFO_TAG("OrderService::createOrder",
            "order created for user: ", req->d.userId, " amount: ", req->d.amount);

        return YomkResponse(YomkResponse::eOk, "order created");
    }
};
```

### CommonFunctions.h — 公共函数池
```cpp
#pragma once
#include "YomkAPI.h"

YomkResponse validateAmount(YomkPkgPtr pkg) {
    YomkUnPackPkgResponse(pkg, string, amountStr);
    if(!amountStr) return {YomkResponse::eInvalid, "null amount"};

    double amount = std::stod(amountStr->d);
    if(amount <= 0) return {YomkResponse::eErr, "invalid amount"};
    return {YomkResponse::eOk, "valid"};
}

void registerCommonFunctions() {
    YOMK_FUNCTIONPOOL_REGISTER("validate_amount", validateAmount);
}
```

### main.cpp — 程序入口
```cpp
#include "UserService.h"
#include "OrderService.h"
#include "CommonFunctions.h"

// 通知事件循环的默认处理函数
YomkResponse notificationHandler(YomkPkgPtr pkg) {
    YomkUnPackPkgResponse(pkg, NotifyMsg, msg);
    if(!msg) return {YomkResponse::eInvalid, "null msg"};
    YOMK_INFO_TAG("NotificationHandler", "notify to: ", msg->d.target, " content: ", msg->d.content);
    return {YomkResponse::eOk, "notified"};
}

int main(int argc, char* argv[]) {
    // 1. 初始化服务器
    YOMK_INIT(std::make_shared<YomkServer>(), {
        "/YomkFunctionPool",
        "/YomkContext",
        "/YomkEventLoop",
        "/YomkLogger"
    });

    // 2. 注册公共函数
    registerCommonFunctions();

    // 3. 启动通知事件循环
    YOMK_EVENTLOOP_START("notification_loop", notificationHandler);

    // 4. 注册服务
    YOMK_NEW_SERVICE(UserService, "/UserService");
    YOMK_NEW_SERVICE(OrderService, "/OrderService");

    // 5. 设置Context监控
    YOMK_CONTEXT_ON_MONITOR();
    YOMK_CONTEXT_SET_MONITOR("user_count", [](const yomk::Context& ctx) {
        YomkUnPackPkgVoid(ctx.m_value, string, val);
        if(val) YOMK_INFO_TAG("Monitor", "user_count changed to: ", val->d);
    });

    // 6. 业务调用
    YomkResponse resp = YOMK_REQUEST("/UserService/create_user",
        YomkMkPtr(UserCreateReq, UserCreateReq{"alice", "alice@example.com"}));

    if(resp.m_resStatus == YomkResponse::eOk) {
        YOMK_INFO("User created: ", resp.m_msg);
    }

    YOMK_REQUEST("/OrderService/create_order",
        YomkMkPtr(OrderCreateReq, OrderCreateReq{"U001", "Widget", 99.99}));

    getchar();
    return 0;
}
```

## 示例5：配置驱动的服务设计（最佳实践）

展示如何使用配置驱动、API 风格调用、数据中转等 YomkServer 进阶理念。

### 场景：动态数据处理管道

用户需求：数据处理流程可通过配置文件定义，服务自动加载并执行，无需修改代码。

### 配置文件 pipeline.yaml

```yaml
# 数据处理管道定义
pipelines:
  - name: data_etl
    steps:
      - callback: extract
        input_ports:
          source:
            type: string
            value: "database"
        output_ports:
          raw_data:
            type: string

      - callback: transform
        input_ports:
          data:
            type: string
            value: ""
        output_ports:
          processed:
            type: string

      - callback: load
        input_ports:
          data:
            type: string
            value: ""
        output_ports:
          result:
            type: string
```

### PipelineService.h

```cpp
#pragma once
#include <YomkServer/YomkAPI.h>
#include <unordered_map>
#include <set>

class PipelineService : public YomkService {
public:
    PipelineService(YomkServer* server) : YomkService(server) {
        name("/PipelineService");
    }

    virtual int init() override {
        // ✅ 只注册接口，不加载业务配置
        YomkInstallFunc("/load", PipelineService::load);
        YomkInstallFunc("/run", PipelineService::run);

        // ✅ 只创建服务内部必需的键
        YOMK_CONTEXT_CREATE("pipeline:current", YomkMkPtr(string, ""));

        YOMK_INFO_TAG("PipelineService::init", "installed /load, /run");
        return 0;
    }

private:
    struct PipelineDef {
        std::string name;
        std::vector<StepDef> steps;
    };

    struct StepDef {
        std::string callback;  // FunctionPool 函数名
        std::unordered_map<std::string, PortDef> input_ports;
        std::unordered_map<std::string, PortDef> output_ports;
    };

    struct PortDef {
        std::string type;
        std::string value;
    };

    std::unordered_map<std::string, PipelineDef> m_pipelines;
    std::set<std::string> m_createdKeys;

    YomkResponse load(YomkPkgPtr pkg) {
        YomkUnPackPkgResponse(pkg, string, pathPtr);
        if (!pathPtr) return {YomkResponse::eInvalid, "no path"};

        // 解析配置文件，构建管道定义
        YAML::Node config = YAML::LoadFile(pathPtr->d);
        for (const auto& pipeYaml : config["pipelines"]) {
            PipelineDef pipe;
            pipe.name = pipeYaml["name"].as<std::string>();

            // 加载步骤并自动创建端口键
            for (const auto& stepYaml : pipeYaml["steps"]) {
                StepDef step;
                step.callback = stepYaml["callback"].as<std::string>();

                for (auto it = stepYaml["input_ports"].begin();
                     it != stepYaml["input_ports"].end(); ++it) {
                    PortDef def;
                    def.type = it->second["type"].as<std::string>();
                    def.value = it->second["value"].as<std::string>();
                    step.input_ports[it->first.as<std::string>()] = def;

                    // ✅ 自动创建 Context 键（去重）
                    if (m_createdKeys.insert(it->first.as<std::string>()).second) {
                        YOMK_CONTEXT_CREATE(it->first.as<std::string>(),
                            YomkMkPtr(string, def.value));
                    }
                }

                pipe.steps.push_back(step);
            }

            m_pipelines[pipe.name] = pipe;
        }

        return {YomkResponse::eOk, "loaded " + std::to_string(m_pipelines.size()) + " pipelines"};
    }

    YomkResponse run(YomkPkgPtr pkg) {
        // ✅ API 风格：通过 pkg 传递管道名
        YomkUnPackPkgResponse(pkg, string, namePtr);
        if (!namePtr) return {YomkResponse::eInvalid, "no pipeline name"};

        auto it = m_pipelines.find(namePtr->d);
        if (it == m_pipelines.end()) return {YomkResponse::eErr, "pipeline not found"};

        // 执行管道步骤
        for (const auto& step : it->second.steps) {
            // 数据中转：业务键 → in: 键
            for (const auto& [portName, def] : step.input_ports) {
                auto val = YOMK_CONTEXT_GET(Yomk(string), portName,
                    YomkMkPtr(string, def.value));
                YOMK_CONTEXT_SET("in:" + portName, val);
            }

            // 执行回调函数
            YOMK_FUNCTIONPOOL_CALL(step.callback, nullptr);

            // 数据中转：out: 键 → 业务键
            for (const auto& [portName, def] : step.output_ports) {
                auto val = YOMK_CONTEXT_GET(Yomk(string), "out:" + portName);
                YOMK_CONTEXT_SET(portName, val);
            }
        }

        return {YomkResponse::eOk, "pipeline " + namePtr->d + " completed"};
    }
};
```

### main.cpp

```cpp
#include "PipelineService.h"

int main() {
    YOMK_INIT(std::make_shared<YomkServer>(), {
        "/YomkFunctionPool",
        "/YomkContext",
        "/YomkLogger"
    });

    // 注册回调函数
    YOMK_FUNCTIONPOOL_REGISTER("extract", extractData);
    YOMK_FUNCTIONPOOL_REGISTER("transform", transformData);
    YOMK_FUNCTIONPOOL_REGISTER("load", loadData);

    // 注册服务
    YOMK_NEW_SERVICE(PipelineService, "/PipelineService");

    // ✅ 一行加载所有管道配置
    YOMK_REQUEST("/PipelineService/load",
        YomkMkPtr(string, "config/pipeline.yaml"));

    // ✅ API 风格执行管道
    YOMK_REQUEST("/PipelineService/run",
        YomkMkPtr(string, "data_etl"));

    getchar();
    return 0;
}
```

### 设计理念总结

| 实践 | 说明 |
|------|------|
| **配置驱动** | 处理流程在 YAML 中定义，服务自动解析创建 |
| **API 风格** | `/run` 通过 pkg 传管道名，简洁明了 |
| **数据中转** | 回调函数只读写 `in:`/`out:` 键，与配置解耦 |
| **init() 纯净** | 只注册接口，不加载业务配置 |
| **键去重** | `m_createdKeys` 防止重复创建 |
| **向后兼容** | 保留 `/load_single` 单个加载接口 |

## 示例2：Context Checker/Monitor 完整流程

```cpp
// 检查函数：只允许非空字符串
ContextChecker::ECheckStatus nonEmptyChecker(const yomk::Context& ctx) {
    YomkUnPackPkg(ctx.m_value, string, val);
    if(!val || val->d.empty()) return ContextChecker::eReject;
    return ContextChecker::eAccept;
}

// 监控函数：记录所有变更
void changeLogger(const yomk::Context& ctx) {
    YomkUnPackPkgVoid(ctx.m_value, string, val);
    YOMK_INFO_TAG("CtxMonitor", "key=", ctx.m_key, " new_value=", val ? val->d : "null");
}

// 使用
YOMK_CONTEXT_CREATE("config", YomkMkPtr(string, "default"));
YOMK_CONTEXT_ON_CHECKER();
YOMK_CONTEXT_SET_CHECKER("config", nonEmptyChecker);
YOMK_CONTEXT_ON_MONITOR();
YOMK_CONTEXT_SET_MONITOR("config", changeLogger);

YOMK_CONTEXT_SET("config", YomkMkPtr(string, "new_value")); // 通过检查并触发监控
YOMK_CONTEXT_SET("config", YomkMkPtr(string, ""));          // 被检查器拒绝
```

## 示例3：EventLoop 异步事件处理

```cpp
// 事件处理函数
YomkResponse taskHandler(YomkPkgPtr pkg) {
    YomkUnPackPkgResponse(pkg, string, taskData);
    YOMK_DEBUG_TAG("TaskHandler", "processing in thread: ", std::this_thread::get_id());
    // 处理耗时任务...
    return {YomkResponse::eOk, "task done"};
}

// 启动事件循环
YOMK_EVENTLOOP_START("worker_loop", taskHandler);

// 异步投递（不等待结果）
YOMK_EVENTLOOP_POST("worker_loop", YomkMkPtr(string, "task_1"));

// 同步投递（等待结果）
YomkResponse resp = YOMK_EVENTLOOP_POST_WAIT("worker_loop", YomkMkPtr(string, "task_2"));
if(resp.m_resStatus == YomkResponse::eOk) {
    YomkUnPackPkg(resp.m_data, Event, event);
    if(event) {
        YOMK_INFO("event result: ", event->d.m_response.m_msg);
    }
}

// 清理
YOMK_EVENTLOOP_STOP("worker_loop");
YOMK_EVENTLOOP_DESTROY("worker_loop");
```

## 示例4：文件日志系统

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
