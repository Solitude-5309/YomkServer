# YomkServer API 详细参考

## 核心类型

### YomkPkg — 消息包基类
```cpp
class YomkPkg {
    void name(const std::string& name);
    std::string name();
};
typedef std::shared_ptr<YomkPkg> YomkPkgPtr;
```

### YomkResponse — 响应对象
```cpp
class YomkResponse {
public:
    enum EResStatus { eInvalid = -1, eOk = 0, eNo = 1 };
    EResStatus m_status;
    std::string m_msg;
    YomkPkgPtr m_data;
};
typedef std::shared_ptr<YomkResponse> YomkResponsePtr;
```

### YomkServiceFunc — 功能函数签名
```cpp
typedef std::function<YomkResponse(YomkPkgPtr pkg)> YomkServiceFunc;
typedef std::function<void(YomkResponse response)> YomkResponseFunc;
```

## YomkMsg 宏 — 消息包注册

```cpp
YomkMsg(IType, OType, VarName)
```
生成 `yomk::OType_` 类（继承 YomkPkg）和 `yomk::OType##Ptr` 类型别名。
- `IType`：实际数据类型
- `OType`：生成的包装类名后缀
- `VarName`：数据成员变量名（用户自定义）

**辅助宏：**
```cpp
Yomk(Type)         // → yomk::Type##_        （类名）
YomkPtr(Type)      // → yomk::Type##Ptr       （指针类型）
YomkMk(Type, ...)  // → yomk::Type##_(...)    （构造实例）
YomkMkPtr(Type, ...)// → std::make_shared<yomk::Type##_>(...)  （创建共享指针）
```

**解包宏：**
```cpp
// 用于返回YomkResponse的函数，解包失败自动return {eNo, "错误信息"}，后续代码无需判空
YomkUnPackPkgResponse(pkg, ClassName, ptrName)

// 用于void函数，解包失败自动return，后续代码无需判空
YomkUnPackPkgVoid(pkg, ClassName, ptrName)

// 不解包失败不return，ptrName为nullptr需手动检查
YomkUnPackPkg(pkg, ClassName, ptrName)

// 按pkg->name()匹配解包，不解包失败不return，需手动检查
YomkUnPackPkgT(pkg, pkgName, ClassName, ptrName)
```

**YomkInstallFunc 宏：**
```cpp
YomkInstallFunc(FuncName, Func)
// 展开为: installFunc(FuncName, std::bind(&Func, this, std::placeholders::_1))
```

## yomk 命名空间内置数据结构

```cpp
namespace yomk {
    struct Function { std::string m_funcName; YomkServiceFunc m_func; };
    struct CallFunction { std::string m_funcName; YomkPkgPtr m_pkg; };
    struct Event {
        std::string m_eventLoopName;
        YomkPkgPtr m_pkg;
        YomkServiceFunc m_serviceFunc;
        std::uint64_t m_eventId;
        YomkResponse m_response;
        std::function<void()> m_waitCallback;
        void handle();  // 调用 m_serviceFunc(m_pkg) 并将结果存入 m_response
    };
    struct Eventloop { std::string m_eventloopName; YomkServiceFunc m_defaultServiceFunc; };
    struct LogFile { std::string m_logger; std::string m_dir; };
    struct Log { enum ELogLevel{eDebug,eInfo,eWarn,eError}; ELogLevel m_level; std::string m_log; std::string m_logger; };
    struct ConsoleLogProxy { std::function<bool(const Log&)> m_consoleLogProxyFunc; };
    struct Context { std::string m_key; YomkPkgPtr m_value; };
    struct ContextChecker {
        enum ECheckStatus{eAccept, eReject};
        std::string m_key;
        std::function<ECheckStatus(const Context&)> m_checkFunc;
    };
    struct ContextMonitor { std::string m_key; std::function<void(Context)> m_contextMonitorFunc; };
    struct VoidPointer { void* m_ptr; };
}
```

**内置标准类型消息包**（成员名均为 `d`）：
```cpp
// 基础类型
Bool, Char, UChar/Byte, Int8, Uint8, Int16, Uint16,
Int32, Uint32, Int64, Uint64, Float32, Float64, String
// 数组类型
BoolArray, CharArray, UCharArray/ByteArray, Int8Array, Uint8Array,
Int16Array, Uint16Array, Int32Array, Uint32Array, Int64Array, Uint64Array,
Float32Array, Float64Array, StringArray
```

**回调函数类型别名：**
```cpp
typedef std::function<void(const yomk::Context& ctx)> YomkContextMonitorFunc;
typedef std::function<yomk::ContextChecker::ECheckStatus(const yomk::Context& ctx)> YomkContextCheckFunc;
typedef std::function<bool(const yomk::Log& log)> YomkConsoleLogProxyFunc;
```

## YomkServer 类

```cpp
class YomkServer {
    template<typename T> int newService(const std::string& srvName = "");
    int startService(std::vector<std::string> srvNames);
    void addService(YomkService* srv);
    YomkResponse request(const std::string& url, YomkPkgPtr pkg = nullptr);
    void asyncRequest(const std::string& url, YomkPkgPtr pkg = nullptr, YomkResponseFunc func = nullptr);
};
```

## YomkBoot 类 — 生命周期管理

```cpp
class YomkBoot {
    virtual int before() = 0;  // 服务启动前：创建资源
    virtual int start() = 0;   // 注册并启动服务
    virtual int after() = 0;   // 服务启动后：初始化调用
};
```

## YomkService 类

```cpp
class YomkService {
    YomkService(YomkServer* server);
    void name(const std::string& name);
    std::string name();
    virtual int init() = 0;  // 必须实现，返回0表示成功
    void installFunc(const std::string& funcName, YomkServiceFunc func);
    YomkResponse invoke(const std::string& funcName, YomkPkgPtr pkg = nullptr);
    YomkResponse request(const std::string& url, YomkPkgPtr pkg = nullptr);
    void asyncRequest(const std::string& url, YomkPkgPtr pkg = nullptr, YomkResponseFunc func = nullptr);
};
```

## YomkAPI 静态接口（宏背后的实际调用）

### 请求通信
| 宏 | 对应API | 说明 |
|----|---------|------|
| `YOMK_INIT()` | `YomkAPI::init()` | 初始化并自动启动全部内置服务 |
| `YOMK_BOOT(boot)` | `YomkAPI::boot(boot)` | 通过 YomkBoot 生命周期初始化 |
| `YOMK_NEW_SERVICE(T, name)` | `YomkAPI::newService<T>(name)` | 注册自定义服务（模板方式） |
| `YOMK_ADD_SERVICE(srv, name)` | `YomkAPI::addService(srv, name)` | 注册自定义服务（实例方式） |
| `YOMK_REQUEST(url, pkg)` | `YomkAPI::request(url, pkg)` | 同步请求 |
| `YOMK_ASYNC_REQUEST(url, pkg, cb)` | `YomkAPI::asyncRequest(url, pkg, cb)` | 异步请求 |
| `YOMK_SERVER_P` | `YomkAPI::serverInstance().get()` | 获取 YomkServer 原始指针 |
| `YOMK_SERVER_PTR` | `YomkAPI::serverInstance()` | 获取 YomkServer shared_ptr |

### Context 全局状态
| 宏 | 说明 |
|----|------|
| `YOMK_CONTEXT_CREATE(key, val)` | 创建K-V，val为YomkPkgPtr |
| `YOMK_CONTEXT_GET(Yomk(T), key, def)` | 获取值，返回`shared_ptr<T>`，不存在返回def |
| `YOMK_CONTEXT_SET(key, val)` | 设置值，若开启checker会先校验 |
| `YOMK_CONTEXT_DESTROY(key)` | 销毁K-V |
| `YOMK_CONTEXT_ON_CHECKER()` | 全局开启checker机制 |
| `YOMK_CONTEXT_OFF_CHECKER()` | 全局关闭checker机制 |
| `YOMK_CONTEXT_SET_CHECKER(key, func)` | 为指定key设置checker函数 |
| `YOMK_CONTEXT_ON_MONITOR()` | 全局开启monitor机制 |
| `YOMK_CONTEXT_OFF_MONITOR()` | 全局关闭monitor机制 |
| `YOMK_CONTEXT_SET_MONITOR(key, func)` | 为指定key设置monitor函数 |

**Checker函数签名：**
```cpp
ContextChecker::ECheckStatus myChecker(const yomk::Context& ctx)
// 返回 eAccept 允许设置，eReject 拒绝设置
```

**Monitor函数签名：**
```cpp
void myMonitor(const yomk::Context& ctx)
// ctx.m_key = 变化的key, ctx.m_value = 新值
```

### EventLoop 事件循环
| 宏 | 说明 |
|----|------|
| `YOMK_EVENTLOOP_START(name, defaultFunc)` | 启动事件循环，defaultFunc为默认处理函数 |
| `YOMK_EVENTLOOP_STOP(name)` | 停止事件循环 |
| `YOMK_EVENTLOOP_POST(name, pkg, handle)` | 投递事件（异步，handle可选覆盖defaultFunc） |
| `YOMK_EVENTLOOP_POST_WAIT(name, pkg, handle)` | 投递事件并同步等待完成 |
| `YOMK_EVENTLOOP_DESTROY(name)` | 销毁事件循环 |

**事件处理函数签名：**
```cpp
YomkResponse eventHandle(YomkPkgPtr pkg)
```

### FunctionPool 函数池
| 宏 | 说明 |
|----|------|
| `YOMK_FUNCTIONPOOL_REGISTER(name, func)` | 注册公共函数 |
| `YOMK_FUNCTIONPOOL_CALL(name, pkg)` | 调用已注册函数 |

### 日志系统
| 宏 | 说明 |
|----|------|
| `YOMK_INFO(args...)` | 控制台INFO日志（默认tag: MainLogger） |
| `YOMK_INFO_TAG(tag, args...)` | 控制台INFO日志（自定义tag） |
| `YOMK_WARN / YOMK_ERROR / YOMK_DEBUG` | 同理 |
| `YOMK_FILE_LOG_CREATE(dir, file)` | 创建文件日志 |
| `YOMK_FILE_LOG_WRITE(file)` | 写入文件日志到磁盘 |
| `YOMK_FILE_INFO(file, args...)` | 文件INFO日志 |
| `YOMK_FILE_INFO_TAG(file, tag, args...)` | 文件INFO日志（自定义tag） |
| `YOMK_ON_CONSOLE_LOG_INFO()` | 开启控制台INFO级别 |
| `YOMK_OFF_CONSOLE_LOG_INFO()` | 关闭控制台INFO级别 |
| `YOMK_SET_CONSOLE_LOG_PROXY(func)` | 设置日志代理函数 |

**日志代理函数签名：**
```cpp
bool consoleLogProxy(const yomk::Log& log)
// 返回true继续传递，返回false停止传递
```
