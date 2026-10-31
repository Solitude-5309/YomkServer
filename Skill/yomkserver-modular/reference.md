# YomkServer API 参考

## 核心类型

```cpp
typedef std::shared_ptr<YomkPkg> YomkPkgPtr;  // 消息包指针

class YomkResponse {
public:
    enum EResStatus { eInvalid = -1, eOk = 0, eNo = 1 };
    EResStatus m_status;  // 本次请求的状态
    std::string m_msg;    // 对状态的说明（不携带返回数据）
    YomkPkgPtr m_data;    // 携带的返回数据
};

typedef std::function<YomkResponse(YomkPkgPtr pkg)> YomkServiceFunc;
typedef std::function<void(YomkResponse response)> YomkResponseFunc;
```

字段语义（固化口径）：`m_status` 表示本次请求的状态；`m_msg` 仅作对状态的说明（成功 `"ok"`、错误为契约消息），**返回数据一律放 `m_data`**。

## 状态码约定

框架统一遵循以下惯例（源码 `YomkFunctionPool.cpp` 等处注释确立）：

| 状态 | 值 | 语义 |
|------|----|------|
| `eOk` | 0 | 调用成功 |
| `eNo` | 1 | not-found / 目标不存在 / 被拒绝 / 条件不满足 |
| `eInvalid` | -1 | 入参非法（空名、nullptr 等）或服务器未初始化 |

- 未初始化（未 `YomkAPI::init()`）时调用任意 `YomkAPI::` 接口：同步请求返回 `eInvalid`，msg 为 `"YomkServer is not init"`；异步请求直接丢弃（框架测试 TestYomkAPINotInit 覆盖此行为）。
- 判断"目标是否存在"用 `eNo`，判断"参数是否合法"用 `eInvalid`，不要混用。

## YomkMsg 宏

```cpp
YomkMsg(DataType, MsgName, VarName);
// DataType：数据类 | MsgName：消息名称（解包与全名引用使用此名） | VarName：成员变量名
```

**构造与类型引用（全名，无辅助宏）：**
```cpp
yomkmsg::MsgName::shared(...)  // 创建消息包 shared_ptr（类内静态工厂）
yomkmsg::MsgName               // 消息包类名（独立 yomkmsg 命名空间）
yomkmsg::MsgNamePtr            // 指针类型 typedef
yomkmsg::MsgName(...)          // 栈对象直接构造
```

> **入包语义**：`yomkmsg::MsgName` 包类型同时提供 `const DataType &` 与 `DataType &&` 两种构造，`yomkmsg::MsgName::shared(std::move(data))` 即移动入包（大对象可避免拷贝）；传左值时重载决议仍选中 `const&` 版本，行为与逐值拷贝一致。

> **命名约定**：消息名称由用户自定义（PascalCase），无固定前缀要求，可与数据类同名。`YomkMsg` 展开生成的真实类型是独立命名空间中的 `yomkmsg::MsgName`（类）与 `yomkmsg::MsgNamePtr`（指针），与 `yomk` 中的载荷/框架类型隔离以避免重名，引用一律写全名；裸写 `MsgName` 会报 `'MsgName' has not been declared`。消息名称作为字符串字面量仍出现在 `installFunc` 第三参位置（`installFunc` 第三参为手写字符串字面量）；解包由 `yomk::unPack<yomkmsg::MsgName>` 模板实参承担（类型即名字，无需字符串）；框架内运行时字符串解包已内联展开（原 `YomkUnPackPkgT` 宏随闭环 7 删除）。

**解包：**
```cpp
yomk::unPack<yomkmsg::MsgName>(pkg)          // 按消息类型解包模板：返回 shared_ptr，失败返回 nullptr（不自动 return，需手动判空）
```

**installFunc / weakFunc：**
```cpp
installFunc("/x", &C::m)             // 弱绑定成员函数并装入本服务 funcMap
installFunc("/x", &C::m, "MsgName")    // 同上，额外声明期望消息类型（可选末位参数）
installFunc("/x", &C::m, "MsgName", "Description")  // 同上，追加函数说明（自由字符串，框架透传不解析）
// MsgName 以手写字符串字面量传入，仅作内省元数据，不参与运行时校验
// weakFunc 已收进 installFunc 成员函数指针重载内部，调用点无需手写
weakFunc(&Class::Method)         // 弱绑定成员函数（不装入 funcMap），供注册到外部子系统使用
// installFunc 内部经 weakFunc 绑定成员函数（等价 weakFunc(bind(&Func, this, _1))）：
// 回调触发时先 weak_ptr.lock() 判活，服务已删除则安全丢弃，不会悬垂 this 崩溃。
// weakFunc 是泛型模板，返回的泛型 lambda 按调用处目标 std::function 类型隐式转换，
// 同一函数自动适配全部回调签名：功能函数/FunctionPool/EventLoop（YomkResponse(YomkPkgPtr)，
// 删除后返回 eNo）、异步响应（void(YomkResponse)，丢弃）、Context checker
// （ECheckStatus(const Context&)，删除后默认放行 eAccept）、Context monitor（void，丢弃）。

namespace yomk {
struct FuncInfo {  // 功能函数元信息（调试内省用），预留扩展（如安全性校验）
    std::string m_funcName;     // 功能函数名（/开头）
    std::string m_msgName;      // 期望消息类型名（installFunc 第三参声明，可为空）
    std::string m_description;  // 函数说明（installFunc 第四参声明，内容使用者自定义，可为空）
};
}  // namespace yomk
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
    // m_logger=日志器名（服务端按其选择/惰性创建日志器实例）；m_tag=记录标签（随记录显示 [tag]）
    // 控制台 TAG 宏兼容双填（m_logger=tag 且 m_tag=tag）；文件 TAG 宏 m_logger=文件名、m_tag=tag
    struct Log { enum ELogLevel{eDebug,eInfo,eWarn,eError}; ELogLevel m_level; std::string m_log; std::string m_logger; std::string m_tag; };
    struct Context { std::string m_key; YomkPkgPtr m_value; };
    struct ContextChecker {
        enum ECheckStatus{eAccept, eReject};
        std::string m_key; std::function<ECheckStatus(const Context&)> m_checkFunc;
    };
    struct ContextMonitor { std::string m_key; std::function<void(Context)> m_contextMonitorFunc; bool m_asyncMonitor; };
}
```

**内置标准类型消息包**（成员名均为 `d`，共 30 种，标量与对应 Array 成对提供）：

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
    std::map<std::string, yomk::FuncInfo> serviceFuncInfos(const std::string& srvName);  // 内省：指定服务的函数元信息
    YomkResponse request(const std::string& url, YomkPkgPtr pkg = nullptr);
    void asyncRequest(const std::string& url, YomkPkgPtr pkg = nullptr, YomkResponseFunc func = nullptr);
};

class YomkBoot {
    virtual ~YomkBoot() {}     // YomkAPI::boot() 以 unique_ptr 接管传入实例的所有权，勿再手动 delete
    virtual int before() = 0;  // 服务启动前：创建资源
    virtual int start() = 0;   // 注册并启动服务
    virtual int after() = 0;   // 服务启动后：初始化调用
};

class YomkService {
    YomkService(YomkServer* server);
    void name(const std::string& name);
    std::string name();
    bool deleted() const;  // 服务是否已被标记注销（YomkAPI::delService() / 同名替换置位）
    virtual int init() = 0;
    virtual void deinit() {}  // 删除服务时由框架在锁外调用，覆写用于停线程/注销外部资源
    template<typename Func> auto weakFunc(Func func);  // 泛型弱绑定守卫（适配签名清单见上方 weakFunc 注释）
    template<typename Class, typename Ret, typename Arg> auto weakFunc(Ret (Class::*)(Arg));  // 成员函数指针弱绑定（判活/丢弃行为同泛型版）
    void installFunc(const std::string& funcName, YomkServiceFunc func, const std::string& msgName = "",
                     const std::string& description = "");  // 装入 funcMap（弱绑定已内置）
    template<typename Class, typename Res, typename Arg>
    void installFunc(const std::string& funcName, Res (Class::*func)(Arg), const std::string& msgName = "",
                     const std::string& description = "");  // 成员函数指针直装："/x", &C::m[, "Msg"][, "desc"]
    std::map<std::string, yomk::FuncInfo> funcInfos();  // 内省：本服务函数元信息（funcName 为键）
    YomkResponse invoke(const std::string& funcName, YomkPkgPtr pkg = nullptr);  // 直接调用本服务的功能函数（不经 URL 路由）
    YomkResponse request(const std::string& url, YomkPkgPtr pkg = nullptr);
    void asyncRequest(const std::string& url, YomkPkgPtr pkg = nullptr, YomkResponseFunc func = nullptr);
};
```

## API 速查

### 请求通信
| API | 说明 |
|----|------|
| `YomkAPI::init()` | 初始化（自动启动内置服务）；可选参数透传至 `init(asyncThreadCount)` 配置异步线程池大小 |
| `YomkAPI::boot(boot)` | Boot 生命周期初始化 |
| `YomkAPI::newService<T>(name)` | 注册服务（模板）；返回 0 成功 / -1 失败（如 init 失败自动回滚） |
| `YomkAPI::addService(srv, name)` | 注册服务（实例）；服务指针所有权移交框架（注册成功后以 shared_ptr 持有），同一指针禁止重复注册（双重释放）；同名替换时旧服务先被锁外 deinit 再安装新服务；返回 0 成功 / -1 失败（如 init 失败自动回滚） |
| `YomkAPI::delService(name)` | 删除服务（后续请求返回 service not found，外流弱绑定回调立即失效：置位注销标志后即使强引用未归零也丢弃，同名替换同语义；YomkAPI::shutdown() 走排空语义不置位） |
| `YomkAPI::shutdown()` | 关闭服务器：先排空在途异步请求（拒新 -> 存量执行完 -> join 工作线程），再逐个服务调用 deinit() 并清空服务表、释放单例；幂等，关闭后不支持二次初始化；未显式调用时退出清理经 atexit 兜底（锁内置空快照、锁外释放，永生单例锁保证在途任务安全取空返回，无挂起/段错误） |
| `YomkAPI::request(url, pkg)` | 同步请求 |
| `YomkAPI::asyncRequest(url, pkg, cb)` | 异步请求（异步请求池执行，并发有界；回调异常被框架捕获记日志；`YomkAPI::shutdown()` 后提交被拒绝） |
| `YomkAPI::serverInstance().get()` / `YomkAPI::serverInstance()` | 获取 Server 指针 |

### Context
| API | 说明 |
|----|------|
| `YomkAPI::CONTEXT_CREATE(key, val[, group])` | 创建 K-V（key 已存在返回 eNo；val 为 nullptr 拒绝创建返回 eNo；值类型与同 key 现值不一致返回 eNo "context type not match"） |
| `YomkAPI::CONTEXT_GET<yomkmsg::MsgName>(key, def[, group])` | 获取值（消息类型经 `T::kName` 内省匹配 key，类型只写一次；**返回值对象发布后只读**：改动请新建对象再 `YomkAPI::CONTEXT_SET`，勿原地改；key 不存在时返回兜底默认值 def，调用方永不拿到空指针） |
| `YomkAPI::CONTEXT_SET(key, val[, group])` | 设置值 |
| `YomkAPI::CONTEXT_DESTROY(key[, group])` | 销毁（key 不存在返回 eNo） |
| `YomkAPI::CONTEXT_ON/OFF_CHECKER([group])` | 开关检查器（全局开关 ON 且该 key 已设置 checker 时才生效） |
| `YomkAPI::CONTEXT_SET_CHECKER(key, func[, group])` | 设置检查函数（返回 eAccept 放行 / eReject 拦截，拦截后 set 返回 eNo 且 monitor 不触发）。**checker 在写锁内门控：回调内重入 set（含跨键环）会死锁**，区别于 monitor 的递归栈溢出 |
| `YomkAPI::CONTEXT_ON/OFF_MONITOR([group])` | 开关监控器 |
| `YomkAPI::CONTEXT_SET_MONITOR(key, func, async[, group])` | 设置监控函数（async 省略默认 false）。**语义**：仅通知发生了一次 set，并回传该次键值快照（不保证实时）；顺序上异步恒按 set 提交序保序，同步并发下不保证跨线程序。**回调约束**：快照仅回调期有效（留存请拷贝）；值对象只读勿原地改；需最新值在回调内 `YomkAPI::CONTEXT_GET` 重读；异常被吞不影响 set；校验/拒绝用 checker。**两种模式**：同步（默认）——锁外内联、及时，回调内重入 set 会递归（长链栈溢出）；异步——写锁内入单线程监控池、set 返回后按提交序执行、deinit/`YomkAPI::shutdown()` 排空不丢，适合耗时回调与状态机回写 |
| `YomkAPI::CONTEXT_INFO_KEYS([group])` | 内省：组内 key 列表（返回 StringArray） |
| `YomkAPI::CONTEXT_INFO_KEY(key[, group])` | 内省：单 key 元信息（结构化 ContextKeyInfo 包：m_key/m_typeName/m_checkerOn/m_monitorCount/m_asyncMonitorCount/m_group，m_msg 恒 "ok"） |
| `YomkAPI::CONTEXT_INFO_GROUPS()` | 内省：全部组名清单（返回 StringArray） |
| `YomkAPI::CONTEXT_INFO_ALL()` | 内省：跨组聚合（ContextGroupInfoList 两层结构化包：组名 m_groupName + 该组全部键元信息 m_keys，m_msg 恒 "ok"） |

组名约定：上表所有 `[group]` 为可选组名尾参，**组名永远是最后一个参数、缺省 `"main"`**；空组名返回 eNo "group name is empty"，非 main 组首次寻址即自动创建（惰性、不支持销毁、组名唯一，监控池每组自持）。

### EventLoop
| API | 说明 |
|----|------|
| `YomkAPI::EVENTLOOP_START(name[, defaultFunc][, "MsgName"])` | 启动（运行中幂等；对已停止未销毁的循环为重启：新开线程按 FIFO 续跑保留的积压事件、不丢失；可选末位 "MsgName" 字符串字面量声明默认处理函数期望的消息类型，仅作内省元数据） |
| `YomkAPI::EVENTLOOP_STOP(name)` | 停止（仅退出工作线程、不清队列：未执行事件保留至下次 START 续跑；停止期间投递被拒） |
| `YomkAPI::EVENTLOOP_POST(name, pkg, handle, tag="")` | 异步投递（handle/tag 均可省略，tag 仅作内省标记；循环未运行投递被拒返 eNo） |
| `YomkAPI::EVENTLOOP_POST_WAIT(name, pkg, handle, tag="")` | 同步投递（tag 同上；未运行被拒返 eNo，销毁丢弃排队事件时等待者同样被释放） |
| `YomkAPI::EVENTLOOP_DESTROY(name)` | 销毁（先停止退出线程、再清空未执行事件——不可续跑，并移除循环条目） |
| `YomkAPI::EVENTLOOP_INFO_LOOPS()` | 内省：事件循环名列表（返回 StringArray） |
| `YomkAPI::EVENTLOOP_INFO_LOOP(name)` / `(name, n)` | 内省：单循环元信息（结构化 EventLoopInfo 包：m_loopName/m_running/m_pending/m_hasDefaultFunc/m_msgName/m_nextTags，m_nextTags 存原值取 n 条，n 缺省 3，m_msg 恒 "ok"，未命中返 eNo） |
| `YomkAPI::EVENTLOOP_INFO_ALL()` | 内省：全量循环元信息（EventLoopInfoList 结构化包，元素为单查同款 EventLoopInfo，m_nextTags 取 3 条，m_msg 恒 "ok"） |

### FunctionPool
| API | 说明 |
|----|------|
| `YomkAPI::FUNCTIONPOOL_REGISTER(name, func[, "MsgName"][, "Description"])` | 注册（三参声明期望消息类型、四参追加函数说明，字符串字面量仅作内省元数据，不参与运行时校验；说明经 INFO_NAME 数据段透出）。**同名重复注册即运行时热替换，无需先注销** |
| `YomkAPI::FUNCTIONPOOL_UNREGISTER(name)` | 注销（未注册返回 eNo，空名 eInvalid） |
| `YomkAPI::FUNCTIONPOOL_CALL(name, pkg)` | 调用（未注册 eNo，空名 eInvalid；pkg 原样透传可为 nullptr；用户函数异常原样穿透到调用方，需自行 try/catch） |
| `YomkAPI::FUNCTIONPOOL_INFO_NAMES()` | 内省：注册函数名列表（返回 StringArray） |
| `YomkAPI::FUNCTIONPOOL_INFO_NAME(name)` | 内省：单函数元信息查询（结构化 FuncInfo 包：m_funcName/m_msgName/m_description，m_msg 恒 "ok"，未注册 eNo） |
| `YomkAPI::FUNCTIONPOOL_INFO_ALL()` | 内省：全量函数元信息（FuncInfoList 结构化包，元素为单查同款 FuncInfo，总数由 d.size() 承载，m_msg 恒 "ok"） |

### ServerInfo（调试内省）
| API | 说明 |
|----|------|
| `YomkAPI::SERVER_INFO_SERVICES()` | 服务列表（返回 StringArray） |
| `YomkAPI::SERVER_INFO_FUNCTIONS(srvName)` | 指定服务的函数列表（FuncInfoList 结构化清单：m_funcName/m_msgName/m_description，m_msg 恒 "ok"；不存在服务为空清单） |
| `YomkAPI::SERVER_INFO_FUNCTION(url)` | 单函数元信息查询（入参 `/srvName/funcName`，命中返回结构化 FuncInfo 包：m_funcName/m_msgName/m_description，`m_msg` 恒 "ok"） |
| `YomkAPI::SERVER_INFO_ALL()` | 全量服务拓扑（ServerServiceInfoList 结构化包，元素含 m_serviceName 与 m_funcs（FuncInfo 列表，与 /function 单查同族），m_msg 恒 "ok"） |

### 日志
| API | 说明 |
|----|------|
| `YOMK_INFO/WARN/ERROR/DEBUG(...)` | 控制台日志（默认 tag=MainLogger） |
| `YOMK_INFO_TAG/WARN_TAG/ERROR_TAG/DEBUG_TAG(tag, ...)` | 自定义 tag 日志（tag 兼容双填 Log::m_tag 与 Log::m_logger，随记录显示 [tag]） |
| `YomkAPI::createFileLogger(dir, file)` | 创建文件日志 |
| `YOMK_FILE_INFO/WARN/ERROR/DEBUG(file, ...)` | 文件日志（默认 tag=MainLogger） |
| `YOMK_FILE_INFO_TAG/WARN_TAG/ERROR_TAG/DEBUG_TAG(file, tag, ...)` | 文件日志自定义 tag（Log::m_logger=file、Log::m_tag=tag，由日志器统一拼接 [tag] 段） |
| `YomkAPI::flushFileLog(file)` | 刷新到磁盘 |
| `YomkAPI::ON/disableConsoleLogInfo/WARN/ERROR/DEBUG()` | 开关控制台级别：无参 = 全局主闸（框架全局），传日志器名 = 仅门控该器（具名未命中 eNo "console logger not found."，不惰性创建） |
| `YomkAPI::ON/disableFileLogInfo/WARN/ERROR/DEBUG(name = "")` | 开关文件日志级别：无参 = 文件全局主闸（框架全局），传文件日志器名 = 仅门控该器（具名未命中 eNo "file logger not found."，不惰性创建；两层 AND 后才写入内存缓冲） |
| `YomkAPI::setFileLogProxy(func)` | 文件日志代理（回调返回 false 拦截该条不落盘、返回 true 穿透走默认落盘；传 nullptr 或空 std::function 即卸载代理、恢复框架默认落盘，GLOBAL("file") 单查 proxyOn 随之为 false；与控制台代理相互独立） |
| `YomkAPI::setConsoleLogProxy(func)` | 日志代理（回调返回 false 拦截该条日志、框架不做默认输出，返回 true 则放行；传 nullptr 或空 std::function 即卸载代理、恢复框架默认输出，内省状态段 m_loggerState.m_proxyOn 随之为 false） |
| `YomkAPI::LOGGER_INFO_LOGGERS(type)` | 内省：按类型查日志器名列表（type 取 `"console"` 或 `"file"`，返回 StringArray，行内容即纯名字典序；非法类型 eNo） |
| `YomkAPI::LOGGER_INFO_LOGGER(name, type)` | 内省：按类型单表查日志器元信息（type 取 `"console"`/`"file"`；结构化 LoggerInfo 包：m_loggerName/m_type/m_dir，m_msg 恒 "ok"，未注册或非法 type 均 eNo；两表允许同名，不 fallback） |
| `YomkAPI::LOGGER_INFO_ALL()` | 内省：全量日志器清单（LoggerInfoList：vector<LoggerInfo>，元素为单查同款，每条内嵌 LoggerState 状态段——console 条目承载控制台逐器门控层真值（未做具名开关恒全 on）且 proxyOn 报控制台代理态，file 条目承载文件逐器门控层真值（未做具名开关恒全 on）且 proxyOn 报文件代理态，m_msg 恒 "ok"） |
| `YomkAPI::LOGGER_INFO_GLOBAL(type = "console")` | 内省：全局主闸状态单查（type="file" 查文件侧、type="console" 或缺省查控制台侧，两套主闸/两套代理各自独立；未知 type 客户端直返 eNo。LoggerState 独立包：四级别主闸开关 + proxyOn，m_msg "ok"；独立于逐器门控层，与 ALL/INFO_LOGGER 条目配合两层归因） |
| `YomkAPI::deleteFileLogger(name)` | 删除日志器（同时清理 console/file 两表，msg `deleted console:c file:f`，均未命中 eNo；文件日志器移除时析构自动落盘，不删磁盘 .log 文件） |