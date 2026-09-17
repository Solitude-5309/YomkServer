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

## 状态码约定

框架统一遵循以下惯例（源码 `YomkFunctionPool.cpp` 等处注释确立）：

| 状态 | 值 | 语义 |
|------|----|------|
| `eOk` | 0 | 调用成功 |
| `eNo` | 1 | not-found / 目标不存在 / 被拒绝 / 条件不满足 |
| `eInvalid` | -1 | 入参非法（空名、nullptr 等）或服务器未初始化 |

- 未初始化（未 `YOMK_INIT`）时调用任意 `YOMK_*` 宏：同步请求返回 `eInvalid`，msg 为 `"YomkServer is not init"`；异步请求直接丢弃（框架测试 TestYomkAPINotInit 覆盖此行为）。
- 判断"目标是否存在"用 `eNo`，判断"参数是否合法"用 `eInvalid`，不要混用。

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

> **入包语义**：`YomkMkPtr` 生成的包类型同时提供 `const DataType &` 与 `DataType &&` 两种构造，传 `std::move(data)` 即移动入包（大对象可避免拷贝）；传左值时重载决议仍选中 `const&` 版本，行为与逐值拷贝一致。

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
YomkInstallFunc(FuncName, Func, MsgName)    // 同上，额外声明期望消息类型（可选末位参数）
// MsgName 字符串化后仅作内省元数据，不参与运行时校验；两参旧调用零改动
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

**内置标准类型消息包**（成员名均为 `d`，共 31 种，标量与对应 Array 成对提供）：

| 类别 | 类型 |
|------|------|
| 布尔 | `Bool` |
| 字节/字符 | `Char`, `UChar`, `Byte`, `Int8`, `Uint8` |
| 整数 | `Int16`, `Uint16`, `Int32`, `Uint32`, `Int64`, `Uint64` |
| 浮点 | `Float32`, `Float64` |
| 字符串 | `String` |

每个标量均有对应 `XxxArray`（如 `Int32Array`, `StringArray`），完整清单见 `YomkServer/include/YomkServer/YomkPkg.h` 末段。

**回调签名：**
```cpp
typedef std::function<void(const yomk::Context& ctx)> YomkContextMonitorFunc;
typedef std::function<yomk::ContextChecker::ECheckStatus(const yomk::Context& ctx)> YomkContextCheckFunc;
typedef std::function<bool(const yomk::Log& log)> YomkConsoleLogProxyFunc;
```

## 核心类

```cpp
class YomkServer {
    static std::shared_ptr<YomkServer> create(std::size_t asyncThreadCount = 0);
    // 唯一构造入口，必须 shared_ptr 持有（栈/裸 new 编译期拒绝）。
    // asyncThreadCount：异步请求池线程数（默认 0 取框架默认值）。
    // 异步监控池归 Context 模块自持（固定单线程 FIFO：写锁内入队，恒按 set 提交序保序，含并发 set）。
    // 线程池仅内部使用，不对用户暴露。
    template<typename T> int newService(const std::string& srvName = "");  // 返回注册结果（0 成功 / -1 失败）
    int startService(std::vector<std::string> srvNames);
    int addService(YomkService* srv);  // init 失败返回 -1 并自动回滚
    int delService(const std::string& srvName);  // 删除服务（锁外调 deinit 后析构）
    void shutdown();  // 优雅关闭：停请求池（拒新->排空->join）后锁外逐个 deinit（Context 在其中自停监控池），幂等
    std::vector<std::string> serviceNames();  // 内省：全部服务名
    std::map<std::string, YomkFuncInfo> serviceFuncInfos(const std::string& srvName);  // 内省：指定服务的函数元信息
    YomkResponse request(const std::string& url, YomkPkgPtr pkg = nullptr);
    void asyncRequest(const std::string& url, YomkPkgPtr pkg = nullptr, YomkResponseFunc func = nullptr);
};

class YomkBoot {
    virtual ~YomkBoot() {}     // YOMK_BOOT 以 unique_ptr 接管传入实例的所有权，勿再手动 delete
    virtual int before() = 0;  // 服务启动前：创建资源
    virtual int start() = 0;   // 注册并启动服务
    virtual int after() = 0;   // 服务启动后：初始化调用
};

class YomkService {
    YomkService(YomkServer* server);
    void name(const std::string& name);
    std::string name();
    bool deleted() const;  // 服务是否已被标记注销（YOMK_DEL_SERVICE / 同名替换置位）
    virtual int init() = 0;
    virtual void deinit() {}  // 删除服务时由框架在锁外调用，覆写用于停线程/注销外部资源
    template<typename Func> auto weakFunc(Func func);  // 泛型弱绑定守卫（适配签名清单见上方 YomkBindWeakSelf 注释）
    void installFunc(const std::string& funcName, YomkServiceFunc func, const std::string& msgName = "");
    std::map<std::string, YomkFuncInfo> funcInfos();  // 内省：本服务函数元信息（funcName 为键）
    YomkResponse invoke(const std::string& funcName, YomkPkgPtr pkg = nullptr);  // 直接调用本服务的功能函数（不经 URL 路由）
    YomkResponse request(const std::string& url, YomkPkgPtr pkg = nullptr);
    void asyncRequest(const std::string& url, YomkPkgPtr pkg = nullptr, YomkResponseFunc func = nullptr);
};
```

## 宏 API 速查

### 请求通信
| 宏 | 说明 |
|----|------|
| `YOMK_INIT()` | 初始化（自动启动内置服务）；可选参数透传至 `init(asyncThreadCount)` 配置异步线程池大小 |
| `YOMK_BOOT(boot)` | Boot 生命周期初始化 |
| `YOMK_NEW_SERVICE(T, name)` | 注册服务（模板）；返回 0 成功 / -1 失败（如 init 失败自动回滚） |
| `YOMK_ADD_SERVICE(srv, name)` | 注册服务（实例）；服务指针所有权移交框架（注册成功后以 shared_ptr 持有），同一指针禁止重复注册（双重释放）；同名替换时旧服务先被锁外 deinit 再安装新服务；返回 0 成功 / -1 失败（如 init 失败自动回滚） |
| `YOMK_DEL_SERVICE(name)` | 删除服务（后续请求返回 service not found，外流弱绑定回调立即失效：置位注销标志后即使强引用未归零也丢弃，同名替换同语义；YOMK_SHUTDOWN 走排空语义不置位） |
| `YOMK_SHUTDOWN()` | 关闭服务器：先排空在途异步请求（拒新 -> 存量执行完 -> join 工作线程），再逐个服务调用 deinit() 并清空服务表、释放单例；幂等，关闭后不支持二次初始化；未显式调用时退出清理经 atexit 兜底（锁内置空快照、锁外释放，永生单例锁保证在途任务安全取空返回，无挂起/段错误） |
| `YOMK_REQUEST(url, pkg)` | 同步请求 |
| `YOMK_ASYNC_REQUEST(url, pkg, cb)` | 异步请求（异步请求池执行，并发有界；回调异常被框架捕获记日志；`YOMK_SHUTDOWN` 后提交被拒绝） |
| `YOMK_SERVER_P` / `YOMK_SERVER_PTR` | 获取 Server 指针 |

### Context
| 宏 | 说明 |
|----|------|
| `YOMK_CONTEXT_CREATE(key, val)` | 创建 K-V（key 已存在返回 eNo；val 为 nullptr 拒绝创建返回 eNo；值类型与同 key 现值不一致返回 eNo "context type not match"） |
| `YOMK_CONTEXT_GET(MsgName, key, def)` | 获取值（**返回值对象发布后只读**：改动请新建对象再 `YOMK_CONTEXT_SET`，勿原地改；key 不存在时返回兜底默认值 def，调用方永不拿到空指针） |
| `YOMK_CONTEXT_SET(key, val)` | 设置值 |
| `YOMK_CONTEXT_DESTROY(key)` | 销毁（key 不存在返回 eNo） |
| `YOMK_CONTEXT_ON/OFF_CHECKER()` | 开关检查器（全局开关 ON 且该 key 已设置 checker 时才生效） |
| `YOMK_CONTEXT_SET_CHECKER(key, func)` | 设置检查函数（返回 eAccept 放行 / eReject 拦截，拦截后 set 返回 eNo 且 monitor 不触发）。**checker 在写锁内门控：回调内重入 set（含跨键环）会死锁**，区别于 monitor 的递归栈溢出 |
| `YOMK_CONTEXT_ON/OFF_MONITOR()` | 开关监控器 |
| `YOMK_CONTEXT_SET_MONITOR(key, func, async)` | 设置监控函数（async 省略默认 false）。**语义**：仅通知发生了一次 set，并回传该次键值快照（不保证实时）；顺序上异步恒按 set 提交序保序，同步并发下不保证跨线程序。**回调约束**：快照仅回调期有效（留存请拷贝）；值对象只读勿原地改；需最新值在回调内 `YOMK_CONTEXT_GET` 重读；异常被吞不影响 set；校验/拒绝用 checker。**两种模式**：同步（默认）——锁外内联、及时，回调内重入 set 会递归（长链栈溢出）；异步——写锁内入单线程监控池、set 返回后按提交序执行、deinit/`YOMK_SHUTDOWN` 排空不丢，适合耗时回调与状态机回写 |
| `YOMK_CONTEXT_INFO_KEYS()` | 内省：key 列表（返回 StringArray） |
| `YOMK_CONTEXT_INFO_KEY(key)` | 内省：单 key 元信息（msg 格式 `key [类型名] checker:on\|off monitors:N(async:M)`） |
| `YOMK_CONTEXT_INFO_ALL()` | 内省：全量 dump（每行同单 key 元信息格式） |

### EventLoop
| 宏 | 说明 |
|----|------|
| `YOMK_EVENTLOOP_START(name, defaultFunc)` / `(name, defaultFunc, MsgName)` | 启动（运行中幂等；对已停止未销毁的循环为重启：新开线程按 FIFO 续跑保留的积压事件、不丢失；可选末位 MsgName 声明默认处理函数期望的消息类型，字符串化后仅作内省元数据，一参/两参旧调用零改动） |
| `YOMK_EVENTLOOP_STOP(name)` | 停止（仅退出工作线程、不清队列：未执行事件保留至下次 START 续跑；停止期间投递被拒） |
| `YOMK_EVENTLOOP_POST(name, pkg, handle, tag="")` | 异步投递（handle/tag 均可省略，tag 仅作内省标记；循环未运行投递被拒返 eNo） |
| `YOMK_EVENTLOOP_POST_WAIT(name, pkg, handle, tag="")` | 同步投递（tag 同上；未运行被拒返 eNo，销毁丢弃排队事件时等待者同样被释放） |
| `YOMK_EVENTLOOP_DESTROY(name)` | 销毁（先停止退出线程、再清空未执行事件——不可续跑，并移除循环条目） |
| `YOMK_EVENTLOOP_INFO_LOOPS()` | 内省：事件循环名列表（返回 StringArray） |
| `YOMK_EVENTLOOP_INFO_LOOP(name)` / `(name, n)` | 内省：单循环元信息（msg 格式 `name running:on\|off pending:N defaultFunc:on\|off [类型名] nextNEventTag(n): tag1, tag2, ...`，默认处理函数声明过类型时附加 `[类型名]`，n 缺省 3，队列不足 n 时全部列出，空 tag 显示 `-`） |
| `YOMK_EVENTLOOP_INFO_ALL()` | 内省：全量 dump（每行同单循环元信息格式） |

### FunctionPool
| 宏 | 说明 |
|----|------|
| `YOMK_FUNCTIONPOOL_REGISTER(name, func)` / `(name, func, MsgName)` | 注册（三参形式声明期望消息类型，字符串化后仅作内省元数据，不参与运行时校验；两参旧调用零改动）。**同名重复注册即运行时热替换，无需先注销** |
| `YOMK_FUNCTIONPOOL_UNREGISTER(name)` | 注销（未注册返回 eNo，空名 eInvalid） |
| `YOMK_FUNCTIONPOOL_CALL(name, pkg)` | 调用（未注册 eNo，空名 eInvalid；pkg 原样透传可为 nullptr；用户函数异常原样穿透到调用方，需自行 try/catch） |
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
| `YOMK_INFO_TAG/WARN_TAG/ERROR_TAG/DEBUG_TAG(tag, ...)` | 自定义 tag 日志 |
| `YOMK_FILE_LOG_CREATE(dir, file)` | 创建文件日志 |
| `YOMK_FILE_INFO/WARN/ERROR/DEBUG(file, ...)` | 文件日志 |
| `YOMK_FILE_INFO_TAG/WARN_TAG/ERROR_TAG/DEBUG_TAG(file, tag, ...)` | 文件日志自定义 tag |
| `YOMK_FILE_LOG_WRITE(file)` | 刷新到磁盘 |
| `YOMK_ON/OFF_CONSOLE_LOG_INFO/WARN/ERROR/DEBUG()` | 开关控制台级别 |
| `YOMK_SET_CONSOLE_LOG_PROXY(func)` | 日志代理（回调返回 false 拦截该条日志、框架不做默认输出，返回 true 则放行；传 nullptr 或空 std::function 即卸载代理、恢复框架默认输出，内省首行随之显示 `proxy:off`） |
| `YOMK_LOGGER_INFO_LOGGERS()` | 内省：日志器列表（返回 StringArray，控制台行 `name [console]`，文件行 `name [file] dir:路径`） |
| `YOMK_LOGGER_INFO_LOGGER(name)` | 内省：单日志器元信息（msg 同上格式，未注册 eNo） |
| `YOMK_LOGGER_INFO_ALL()` | 内省：全量 dump（首行 `console:debug:on\|off info:... warn:... error:... proxy:on\|off`，其余为日志器行） |
| `YOMK_FILE_LOG_DELETE(name)` | 删除日志器（同时清理 console/file 两表，msg `deleted console:c file:f`，均未命中 eNo；文件日志器移除时析构自动落盘，不删磁盘 .log 文件） |