# YomkServer API 参考

## 核心类型

```cpp
typedef std::shared_ptr<YomkPkg> YomkPkgPtr;  // 消息包指针

class YomkResponse {
public:
    enum EResStatus { eInvalid = -1, eOk = 0, eNo = 1 };
    EResStatus m_status;
    std::string m_msg;
    YomkPkgPtr m_data;
};

typedef std::function<YomkResponse(YomkPkgPtr pkg)> YomkServiceFunc;
typedef std::function<void(YomkResponse response)> YomkResponseFunc;
```

## YomkMsg 宏

```cpp
YomkMsg(DataType, MsgName, VarName)
// DataType：数据类 | MsgName：消息名称（辅助宏使用此名） | VarName：成员变量名
```

**辅助宏：**
```cpp
YomkMkPtr(MsgName, ...)   // 创建消息包 shared_ptr
Yomk(MsgName)             // → yomk::MsgName##_（类名）
YomkPtr(MsgName)          // → yomk::MsgName##Ptr（指针类型）
YomkMk(MsgName, ...)      // → 构造实例
```

**解包宏：**
```cpp
YomkUnPackPkgResponse(pkg, MsgName, ptr)  // 失败自动 return {eNo, ...}
YomkUnPackPkgVoid(pkg, MsgName, ptr)      // 失败自动 return（void函数）
YomkUnPackPkg(pkg, MsgName, ptr)          // 不自动 return，需手动判空
YomkUnPackPkgT(pkg, MsgName, ClassName, ptr) // MsgName为运行时字符串
```

**YomkInstallFunc / YomkBindWeakSelf：**
```cpp
YomkInstallFunc(FuncName, Func)  // 弱绑定成员函数并装入本服务 funcMap
YomkBindWeakSelf(Func)           // 弱绑定成员函数（不装入 funcMap），供注册到外部子系统使用
// 两者均展开为 weakFunc(bind(&Func, this, _1))：回调触发时先 weak_ptr.lock() 判活，
// 服务已删除则安全丢弃，不会悬垂 this 崩溃。
// weakFunc 是泛型模板，返回的泛型 lambda 按调用处目标 std::function 类型隐式转换，
// 同一个宏自动适配全部回调签名：功能函数/FunctionPool/EventLoop（YomkResponse(YomkPkgPtr)，
// 删除后返回 eNo）、异步响应（void(YomkResponse)，丢弃）、Context checker
// （ECheckStatus(const Context&)，删除后默认放行 eAccept）、Context monitor（void，丢弃）。
```

## 内置数据结构

```cpp
namespace yomk {
    struct Event {
        std::string m_eventLoopName; YomkPkgPtr m_pkg;
        YomkServiceFunc m_serviceFunc; std::uint64_t m_eventId;
        YomkResponse m_response; std::function<void()> m_waitCallback;
    };
    struct Log { enum ELogLevel{eDebug,eInfo,eWarn,eError}; ELogLevel m_level; std::string m_log; std::string m_logger; };
    struct Context { std::string m_key; YomkPkgPtr m_value; };
    struct ContextChecker {
        enum ECheckStatus{eAccept, eReject};
        std::string m_key; std::function<ECheckStatus(const Context&)> m_checkFunc;
    };
    struct ContextMonitor { std::string m_key; std::function<void(Context)> m_contextMonitorFunc; bool m_asyncMonitor; };
}
```

**内置标准类型消息包**（成员名均为 `d`）：
`Bool`, `Int32`, `Uint32`, `Int64`, `Uint64`, `Float32`, `Float64`, `String` 及对应 `Array` 类型

**回调签名：**
```cpp
typedef std::function<void(const yomk::Context& ctx)> YomkContextMonitorFunc;
typedef std::function<yomk::ContextChecker::ECheckStatus(const yomk::Context& ctx)> YomkContextCheckFunc;
typedef std::function<bool(const yomk::Log& log)> YomkConsoleLogProxyFunc;
```

## 核心类

```cpp
class YomkServer {
    template<typename T> int newService(const std::string& srvName = "");
    int startService(std::vector<std::string> srvNames);
    void addService(YomkService* srv);
    int delService(const std::string& srvName);  // 删除服务（锁外调 deinit 后析构）
    YomkResponse request(const std::string& url, YomkPkgPtr pkg = nullptr);
    void asyncRequest(const std::string& url, YomkPkgPtr pkg = nullptr, YomkResponseFunc func = nullptr);
};

class YomkBoot {
    virtual int before() = 0;  // 服务启动前：创建资源
    virtual int start() = 0;   // 注册并启动服务
    virtual int after() = 0;   // 服务启动后：初始化调用
};

class YomkService {
    YomkService(YomkServer* server);
    void name(const std::string& name);
    std::string name();
    virtual int init() = 0;
    virtual void deinit() {}  // 删除服务时由框架在锁外调用，覆写用于停线程/注销外部资源
    template<typename Func> auto weakFunc(Func func);  // 泛型弱绑定守卫：泛型 lambda 按目标 std::function 隐式转换，
                                                       // 覆盖功能函数/FunctionPool/EventLoop/异步响应/Context checker·monitor
    void installFunc(const std::string& funcName, YomkServiceFunc func);
    YomkResponse request(const std::string& url, YomkPkgPtr pkg = nullptr);
    void asyncRequest(const std::string& url, YomkPkgPtr pkg = nullptr, YomkResponseFunc func = nullptr);
};
```

## 宏 API 速查

### 请求通信
| 宏 | 说明 |
|----|------|
| `YOMK_INIT()` | 初始化（自动启动内置服务） |
| `YOMK_BOOT(boot)` | Boot 生命周期初始化 |
| `YOMK_NEW_SERVICE(T, name)` | 注册服务（模板） |
| `YOMK_ADD_SERVICE(srv, name)` | 注册服务（实例） |
| `YOMK_DEL_SERVICE(name)` | 删除服务（后续请求返回 service not found，外流弱绑定回调自动失效） |
| `YOMK_REQUEST(url, pkg)` | 同步请求 |
| `YOMK_ASYNC_REQUEST(url, pkg, cb)` | 异步请求 |
| `YOMK_SERVER_P` / `YOMK_SERVER_PTR` | 获取 Server 指针 |

### Context
| 宏 | 说明 |
|----|------|
| `YOMK_CONTEXT_CREATE(key, val)` | 创建 K-V |
| `YOMK_CONTEXT_GET(MsgName, key, def)` | 获取值 |
| `YOMK_CONTEXT_SET(key, val)` | 设置值 |
| `YOMK_CONTEXT_DESTROY(key)` | 销毁 |
| `YOMK_CONTEXT_ON/OFF_CHECKER()` | 开关检查器 |
| `YOMK_CONTEXT_SET_CHECKER(key, func)` | 设置检查函数 |
| `YOMK_CONTEXT_ON/OFF_MONITOR()` | 开关监控器 |
| `YOMK_CONTEXT_SET_MONITOR(key, func, async)` | 设置监控函数（async=true异步回调监控函数） |

### EventLoop
| 宏 | 说明 |
|----|------|
| `YOMK_EVENTLOOP_START(name, defaultFunc)` | 启动 |
| `YOMK_EVENTLOOP_STOP(name)` | 停止 |
| `YOMK_EVENTLOOP_POST(name, pkg, handle)` | 异步投递 |
| `YOMK_EVENTLOOP_POST_WAIT(name, pkg, handle)` | 同步投递 |
| `YOMK_EVENTLOOP_DESTROY(name)` | 销毁 |

### FunctionPool
| 宏 | 说明 |
|----|------|
| `YOMK_FUNCTIONPOOL_REGISTER(name, func)` | 注册 |
| `YOMK_FUNCTIONPOOL_UNREGISTER(name)` | 注销（未注册返回 eInvalid） |
| `YOMK_FUNCTIONPOOL_CALL(name, pkg)` | 调用 |

### 日志
| 宏 | 说明 |
|----|------|
| `YOMK_INFO/WARN/ERROR/DEBUG(...)` | 控制台日志 |
| `YOMK_INFO_TAG(tag, ...)` | 自定义 tag 日志 |
| `YOMK_FILE_LOG_CREATE(dir, file)` | 创建文件日志 |
| `YOMK_FILE_INFO(file, ...)` | 文件日志 |
| `YOMK_FILE_LOG_WRITE(file)` | 刷新到磁盘 |
| `YOMK_ON/OFF_CONSOLE_LOG_INFO()` | 开关控制台级别 |
| `YOMK_SET_CONSOLE_LOG_PROXY(func)` | 日志代理 |