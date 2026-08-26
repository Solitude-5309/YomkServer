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

> **命名约定**：消息名称由用户自定义（PascalCase），无固定前缀要求，可与数据类同名。消息类型名是“宏词汇”而非“类型词汇”——只能在 `Yomk()` / `YomkPtr()` / `YomkMkPtr()` / `YomkUnPackPkg*` / `YomkInstallFunc` 第三参等宏的参数位置出现，不能当裸类型名使用。`YomkMsg` 展开生成的真实类型是 `yomk::MsgName_`（类）与 `yomk::MsgNamePtr`（指针），裸写 `MsgName` 会报 `'MsgName' has not been declared`。

**解包宏：**
```cpp
YomkUnPackPkgResponse(pkg, MsgName, ptr)  // 失败自动 return {eNo, ...}
YomkUnPackPkgVoid(pkg, MsgName, ptr)      // 失败自动 return（void函数）
YomkUnPackPkg(pkg, MsgName, ptr)          // 不自动 return，需手动判空
YomkUnPackPkgT(pkg, MsgName, ClassName, ptr) // MsgName为运行时字符串
```

**YomkInstallFunc / YomkBindWeakSelf：**
```cpp
YomkInstallFunc(FuncName, Func)             // 弱绑定成员函数并装入本服务 funcMap
YomkInstallFunc(FuncName, Func, MsgName)    // 同上，额外声明期望消息类型（字符串化后仅作内省元数据，
                                            // 不参与运行时校验；可选末位参数，两参旧调用零改动）
YomkBindWeakSelf(Func)           // 弱绑定成员函数（不装入 funcMap），供注册到外部子系统使用
// 两者均展开为 weakFunc(bind(&Func, this, _1))：回调触发时先 weak_ptr.lock() 判活，
// 服务已删除则安全丢弃，不会悬垂 this 崩溃。
// weakFunc 是泛型模板，返回的泛型 lambda 按调用处目标 std::function 类型隐式转换，
// 同一个宏自动适配全部回调签名：功能函数/FunctionPool/EventLoop（YomkResponse(YomkPkgPtr)，
// 删除后返回 eNo）、异步响应（void(YomkResponse)，丢弃）、Context checker
// （ECheckStatus(const Context&)，删除后默认放行 eAccept）、Context monitor（void，丢弃）。

struct YomkFuncInfo {  // 功能函数元信息（调试内省用），预留扩展（如安全性校验）
    std::string m_funcName;  // 功能函数名（/开头）
    std::string m_msgName;   // 期望消息类型名（YomkInstallFunc 三参声明，可为空）
};
```

## 内置数据结构

```cpp
namespace yomk {
    struct Event {
        std::string m_eventLoopName; YomkPkgPtr m_pkg;
        YomkServiceFunc m_serviceFunc; std::uint64_t m_eventId;
        YomkResponse m_response; std::function<void()> m_waitCallback;
        std::string m_tag; // 用户事件标记，POST 时可选传入，仅用于内省展示
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
    std::vector<std::string> serviceNames();  // 内省：全部服务名
    std::map<std::string, YomkFuncInfo> serviceFuncInfos(const std::string& srvName);  // 内省：指定服务的函数元信息
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
    void installFunc(const std::string& funcName, YomkServiceFunc func, const std::string& msgName = "");
    std::map<std::string, YomkFuncInfo> funcInfos();  // 内省：本服务函数元信息（funcName 为键）
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
| `YOMK_CONTEXT_INFO_KEYS()` | 内省：key 列表（返回 StringArray） |
| `YOMK_CONTEXT_INFO_KEY(key)` | 内省：单 key 元信息（msg 格式 `key [类型名] checker:on\|off monitors:N(async:M)`） |
| `YOMK_CONTEXT_INFO_ALL()` | 内省：全量 dump（每行同单 key 元信息格式） |

### EventLoop
| 宏 | 说明 |
|----|------|
| `YOMK_EVENTLOOP_START(name, defaultFunc)` / `(name, defaultFunc, MsgName)` | 启动（可选末位 MsgName 声明默认处理函数期望的消息类型，字符串化后仅作内省元数据，一参/两参旧调用零改动） |
| `YOMK_EVENTLOOP_STOP(name)` | 停止 |
| `YOMK_EVENTLOOP_POST(name, pkg, handle, tag="")` | 异步投递（handle/tag 均可省略，tag 仅作内省标记） |
| `YOMK_EVENTLOOP_POST_WAIT(name, pkg, handle, tag="")` | 同步投递（tag 同上） |
| `YOMK_EVENTLOOP_DESTROY(name)` | 销毁 |
| `YOMK_EVENTLOOP_INFO_LOOPS()` | 内省：事件循环名列表（返回 StringArray） |
| `YOMK_EVENTLOOP_INFO_LOOP(name)` / `(name, n)` | 内省：单循环元信息（msg 格式 `name running:on\|off pending:N defaultFunc:on\|off [类型名] nextNEventTag(n): tag1, tag2, ...`，默认处理函数声明过类型时附加 `[类型名]`，n 缺省 3，队列不足 n 时全部列出，空 tag 显示 `-`） |
| `YOMK_EVENTLOOP_INFO_ALL()` | 内省：全量 dump（每行同单循环元信息格式） |

### FunctionPool
| 宏 | 说明 |
|----|------|
| `YOMK_FUNCTIONPOOL_REGISTER(name, func)` / `(name, func, MsgName)` | 注册（三参形式声明期望消息类型，字符串化后仅作内省元数据，不参与运行时校验；两参旧调用零改动） |
| `YOMK_FUNCTIONPOOL_UNREGISTER(name)` | 注销（未注册返回 eInvalid） |
| `YOMK_FUNCTIONPOOL_CALL(name, pkg)` | 调用 |
| `YOMK_FUNCTIONPOOL_INFO_NAMES()` | 内省：注册函数名列表（返回 StringArray） |
| `YOMK_FUNCTIONPOOL_INFO_NAME(name)` | 内省：单函数存在性查询（命中 eOk 且 msg 为 `funcName [类型名]`，未声明类型时无括号后缀，未注册 eNo） |
| `YOMK_FUNCTIONPOOL_INFO_ALL()` | 内省：全量 dump（首行 `functions:N`，其余每行 `funcName [类型名]`） |

### ServerInfo（调试内省）
| 宏 | 说明 |
|----|------|
| `YOMK_SERVER_INFO_SERVICES()` | 服务列表（返回 StringArray） |
| `YOMK_SERVER_INFO_FUNCTIONS(srvName)` | 指定服务的函数列表（每行 `funcName [msgName]`） |
| `YOMK_SERVER_INFO_FUNCTION(url)` | 单函数类型查询（入参 `/srvName/funcName`，命中返回 msg 为类型名，未声明为空串） |
| `YOMK_SERVER_INFO_ALL()` | 全量 dump：服务名行 + 缩进函数行 |

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
| `YOMK_LOGGER_INFO_LOGGERS()` | 内省：日志器列表（返回 StringArray，控制台行 `name [console]`，文件行 `name [file] dir:路径`） |
| `YOMK_LOGGER_INFO_LOGGER(name)` | 内省：单日志器元信息（msg 同上格式，未注册 eNo） |
| `YOMK_LOGGER_INFO_ALL()` | 内省：全量 dump（首行 `console:debug:on\|off info:... warn:... error:... proxy:on\|off`，其余为日志器行） |