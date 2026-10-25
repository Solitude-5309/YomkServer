#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// 弱绑定服务成员函数，用于安全挂接外部回调
#define YomkBindWeakSelf(Func) weakFunc(std::bind(&Func, this, std::placeholders::_1))

// 注册服务成员功能函数（3 参声明期望消息类型；4 参追加函数说明，内容使用者自定义）
#define YOMK_INSTALL_FUNC_SELECT(_1, _2, _3, _4, NAME, ...) NAME
#define YomkInstallFunc(...)                                                                                      \
    YOMK_INSTALL_FUNC_SELECT(__VA_ARGS__, YOMK_INSTALL_FUNC_4, YOMK_INSTALL_FUNC_3, YOMK_INSTALL_FUNC_2) \
    (__VA_ARGS__)
#define YOMK_INSTALL_FUNC_2(FuncName, Func) installFunc(FuncName, YomkBindWeakSelf(Func))
#define YOMK_INSTALL_FUNC_3(FuncName, Func, MsgName) installFunc(FuncName, YomkBindWeakSelf(Func), #MsgName)
#define YOMK_INSTALL_FUNC_4(FuncName, Func, MsgName, Description) \
    installFunc(FuncName, YomkBindWeakSelf(Func), #MsgName, Description)

// 校验消息类型并解包，类型不匹配时直接返回 YomkResponse::eNo
#define YomkUnPackPkgResponse(pkg, MsgName, ptrName)                             \
    if (!pkg || pkg->name() != #MsgName)                                         \
        return {YomkResponse::eNo, " pkg is null or pkg is not " #MsgName ". "}; \
    YomkPtr(MsgName) ptrName = std::dynamic_pointer_cast<Yomk(MsgName)>(pkg);    \
    if (!ptrName)                                                                \
        return {YomkResponse::eNo, " pkg[" #MsgName "] is dynamic_pointer_cast failed. "};

// 校验消息类型并解包，类型不匹配时直接 return void
#define YomkUnPackPkgVoid(pkg, MsgName, ptrName)                              \
    if (!pkg || pkg->name() != #MsgName)                                      \
        return;                                                               \
    YomkPtr(MsgName) ptrName = std::dynamic_pointer_cast<Yomk(MsgName)>(pkg); \
    if (!ptrName)                                                             \
        return;

// 校验消息类型并解包，类型不匹配时不提前返回（指针为 nullptr）
#define YomkUnPackPkg(pkg, MsgName, ptrName)                     \
    YomkPtr(MsgName) ptrName = nullptr;                          \
    if (pkg && pkg->name() == #MsgName)                          \
    {                                                            \
        ptrName = std::dynamic_pointer_cast<Yomk(MsgName)>(pkg); \
    }

// 按运行时类型名解包（框架内部使用）
#define YomkUnPackPkgT(pkg, MsgName, ClassName, ptrName)     \
    std::shared_ptr<ClassName> ptrName = nullptr;            \
    if (pkg && pkg->name() == MsgName)                       \
    {                                                        \
        ptrName = std::dynamic_pointer_cast<ClassName>(pkg); \
    }

// 消息包类型定义宏
#define YomkMsg(DataType, MsgName, VarName)                                             \
    namespace yomk                                                                      \
    {                                                                                   \
    class MsgName##_ : public YomkPkg                                                   \
    {                                                                                   \
    public:                                                                             \
        MsgName##_() { m_name = #MsgName; }                                             \
        MsgName##_(const DataType& value) : VarName(value) { m_name = #MsgName; }       \
        MsgName##_(DataType&& value) : VarName(std::move(value)) { m_name = #MsgName; } \
        virtual ~MsgName##_() {}                                                        \
                                                                                        \
    public:                                                                             \
        DataType VarName{};                                                             \
    };                                                                                  \
    typedef std::shared_ptr<MsgName##_> MsgName##Ptr;                                   \
    }

// 消息包构造与类型辅助宏
#define Yomk(MsgName) yomk::MsgName##_
#define YomkMk(MsgName, ...) yomk::MsgName##_(__VA_ARGS__)
#define YomkPtr(MsgName) yomk::MsgName##Ptr
#define YomkMkPtr(MsgName, ...) std::make_shared<yomk::MsgName##_>(__VA_ARGS__)

class YomkServer;

// 消息包基类
class YomkPkg
{
public:
    YomkPkg() {}
    virtual ~YomkPkg() {}

public:
    void name(const std::string& name) { m_name = name; }
    std::string name() { return m_name; }

protected:
    std::string m_name;
};
typedef std::shared_ptr<YomkPkg> YomkPkgPtr;

// 请求调用结果对象（m_status 表示本次请求的状态；m_msg 对状态的说明；m_data 携带的返回数据）
class YomkResponse
{
public:
    enum EResStatus
    {
        eInvalid = -1,
        eOk = 0,
        eNo = 1,
    };

public:
    YomkResponse() : m_status(eInvalid), m_data(nullptr) {}
    YomkResponse(EResStatus status, const std::string& msg = "", std::shared_ptr<YomkPkg> d = nullptr)
        : m_status(status), m_msg(msg), m_data(d)
    {
    }
    virtual ~YomkResponse() {}

public:
    EResStatus m_status;  // 本次请求的状态
    std::string m_msg;    // 对状态的说明（不携带返回数据）
    YomkPkgPtr m_data;    // 携带的返回数据
};
typedef std::shared_ptr<YomkResponse> YomkResponsePtr;

// 功能函数签名：入参为请求消息包（可为 nullptr），返回 YomkResponse
typedef std::function<YomkResponse(YomkPkgPtr pkg)> YomkServiceFunc;
// 异步响应回调签名：request 完成后回传结果
typedef std::function<void(YomkResponse response)> YomkResponseFunc;

namespace yomk
{

// 功能函数元信息（调试内省用）
struct FuncInfo
{
    std::string m_funcName;    // 功能函数名（/开头）
    std::string m_msgName;     // 期望消息类型名（三参宏声明，可为空）
    std::string m_description; // 函数说明（四参宏声明，内容使用者自定义，可为空）
};

struct Function
{
    std::string m_funcName;
    YomkServiceFunc m_func;
    std::string m_msgName;
    std::string m_description;  // 函数说明（四参宏声明，内容使用者自定义，可为空）
};

struct CallFunction
{
    std::string m_funcName;
    YomkPkgPtr m_pkg;
};

struct Event
{
    virtual ~Event() = default;
    std::string m_eventLoopName;
    YomkPkgPtr m_pkg;
    YomkServiceFunc m_serviceFunc;
    std::uint64_t m_eventId = 0;
    YomkResponse m_response;
    std::function<void()> m_waitCallback;
    std::string m_tag;
    Event() : m_eventLoopName(""), m_pkg(nullptr), m_serviceFunc(nullptr), m_waitCallback(nullptr), m_tag("") {}
    Event(const std::string& eventLoopName, YomkPkgPtr pkg, YomkServiceFunc serviceFunc)
        : m_eventLoopName(eventLoopName), m_pkg(pkg), m_serviceFunc(serviceFunc), m_waitCallback(nullptr), m_tag("")
    {
    }
    Event(const std::string& eventLoopName, YomkPkgPtr pkg, YomkServiceFunc serviceFunc, const std::string& tag)
        : m_eventLoopName(eventLoopName), m_pkg(pkg), m_serviceFunc(serviceFunc), m_waitCallback(nullptr), m_tag(tag)
    {
    }
    virtual void handle()
    {
        if (m_serviceFunc)
        {
            m_response = m_serviceFunc(m_pkg);
        }
    }
};

struct Eventloop
{
    std::string m_eventloopName;
    YomkServiceFunc m_defaultServiceFunc;
    std::string m_msgName;  // 默认处理函数期望的消息类型名（仅内省元数据，可为空）
};

// 事件循环内省元信息（EVENTLOOP_INFO_LOOP 返回数据）
struct EventLoopInfo
{
    std::string m_loopName;               // 循环名
    bool m_running = false;               // worker 是否运行中
    std::size_t m_pending = 0;            // 积压事件数
    bool m_hasDefaultFunc = false;        // 是否注册默认处理函数
    std::string m_msgName;                // 默认处理函数期望消息类型名（可空）
    std::vector<std::string> m_nextTags;  // 队列头起最多 tagCount 条事件 tag（存原值，空串保留）
};

struct LogFile
{
    std::string m_logger;
    std::string m_dir;
};

struct Log
{
    enum ELogLevel : int
    {
        eDebug,
        eInfo,
        eWarn,
        eError,
    };
    ELogLevel m_level;
    std::string m_log;
    std::string m_logger;  // 日志器名：服务端按其查找/惰性创建日志器实例
    std::string m_tag;     // 日志标签：随记录显示 [tag] 段，区别于 m_logger（末位追加，旧位置初始化兼容）
};

struct ConsoleLogProxy
{
    std::function<bool(const Log& log)> m_consoleLogProxyFunc;
};

// Logger 控制台状态（LoggerInfo 内嵌状态段：debug/info/warn/error 四级别开关 + proxy；
// console 条目承载真实全局状态，file 条目暂无开关概念：四级别恒 true、proxyOn 恒 false）
struct LoggerState
{
    bool m_debugOn = false;  // debug 级别开关
    bool m_infoOn = false;   // info 级别开关
    bool m_warnOn = false;   // warn 级别开关
    bool m_errorOn = false;  // error 级别开关
    bool m_proxyOn = false;  // 控制台代理已安装
};

// 日志器单查请求（LOGGER_INFO_LOGGER 请求数据：名 + 类型，两表允许同名须按类型区分）
struct LoggerQuery
{
    std::string m_loggerName;
    std::string m_type;  // 类型："console" 或 "file"
};

// 日志器内省元信息（LOGGER_INFO_LOGGER 单查返回数据，LOGGER_INFO_ALL 清单元素）
struct LoggerInfo
{
    std::string m_loggerName;  // 日志器名
    std::string m_type;        // 类型："console" 或 "file"（与 INFO_LOGGERS 的类型参数同名）
    std::string m_dir;         // file 类型的日志目录（console 时为空串）
    // 控制台状态段：console 条目承载真实全局状态；file 条目暂无开关概念（级别恒 true、proxyOn 恒 false）
    LoggerState m_loggerState;
};

struct Context
{
    std::string m_key;
    YomkPkgPtr m_value;
    std::string m_group = "main";  // key 所属组名（缺省 main）
};

// Context 键内省元信息（CONTEXT_INFO_KEY 单查返回数据；同时是 ContextGroupInfo.m_keys 清单元素）
struct ContextKeyInfo
{
    std::string m_key;                    // 键名
    std::string m_typeName;               // 值消息包类型名（值为空时为空串）
    bool m_checkerOn = false;             // 门控是否开启
    std::size_t m_monitorCount = 0;       // monitor 总数
    std::size_t m_asyncMonitorCount = 0;  // 其中 async monitor 数
    std::string m_group = "main";         // key 所属组名
};

struct ContextChecker
{
    enum ECheckStatus
    {
        eAccept,
        eReject
    };
    std::string m_key;
    std::function<ECheckStatus(const yomk::Context& ctx)> m_checkFunc;
    std::string m_group = "main";  // 目标 key 所属组名（缺省 main）
};

struct ContextMonitor
{
    std::string m_key;
    std::function<void(Context ctx)> m_contextMonitorFunc;
    bool m_asyncMonitor = false;
    std::string m_group = "main";  // 目标 key 所属组名（缺省 main）
};

// Context 组寻址载体（/destroy 与 /key 请求报文：定位某组下的某键）
struct ContextGroupKey
{
    std::string m_group = "main";  // 组名（缺省 main）
    std::string m_key;             // 键名
};

// Context 组级内省条目（CONTEXT_INFO_ALL 返回数据元素：组名 + 该组下全部键元信息，对齐 ServerServiceInfo）
struct ContextGroupInfo
{
    std::string m_groupName;                   // 组名
    std::vector<ContextKeyInfo> m_keys;        // 该组全部键的内省元信息（map 字典序）
};

// 服务级内省条目（SERVER_INFO_ALL 返回数据元素：服务名 + 该服务注册的函数元信息列表）
struct ServerServiceInfo
{
    std::string m_serviceName;           // 服务名（带 / 前缀，如 "/FmtSrv"）
    std::vector<FuncInfo> m_funcs;       // 函数元信息（与 /function 单查同族，m_funcName 带 / 前缀 url 段）
};

struct VoidPointer
{
    void* m_ptr = nullptr;
};

}  // namespace yomk

// clang-format off
YomkMsg(Function, Function, d)
YomkMsg(CallFunction, CallFunction, d)
YomkMsg(FuncInfo, FuncInfo, d)
// FunctionPool 全量函数内省（FUNCTIONPOOL_INFO_ALL 返回数据；函数总数由 d.size() 承载）
YomkMsg(std::vector<yomk::FuncInfo>, FuncInfoList, d)
YomkMsg(Event, Event, d)
YomkMsg(Eventloop, Eventloop, d)
YomkMsg(EventLoopInfo, EventLoopInfo, d)
// EventLoop 全量循环内省（EVENTLOOP_INFO_ALL 返回数据；m_nextTags 取 3 条与单查缺省一致）
YomkMsg(std::vector<yomk::EventLoopInfo>, EventLoopInfoList, d)
YomkMsg(LogFile, LogFile, d)
YomkMsg(Log, Log, d)
YomkMsg(ConsoleLogProxy, ConsoleLogProxy, d)
YomkMsg(LoggerQuery, LoggerQuery, d)
YomkMsg(LoggerInfo, LoggerInfo, d)
// Logger 全量内省（LOGGER_INFO_ALL 返回数据；元素为单查同款 LoggerInfo，每条内嵌 LoggerState 状态段）
YomkMsg(std::vector<yomk::LoggerInfo>, LoggerInfoList, d)
YomkMsg(Context, Context, d)
// Context 组寻址载体（/destroy 与 /key 请求报文：定位某组下的某键，组名缺省 main）
YomkMsg(ContextGroupKey, ContextGroupKey, d)
YomkMsg(ContextKeyInfo, ContextKeyInfo, d)
// Context 键元信息清单（ContextGroupInfo.m_keys 的元素类型）
YomkMsg(std::vector<yomk::ContextKeyInfo>, ContextKeyInfoList, d)
// Context 组级内省条目（ContextGroupInfoList 元素；层级为组 → 键元信息）
YomkMsg(ContextGroupInfo, ContextGroupInfo, d)
// Context 组级全量内省（CONTEXT_INFO_ALL 返回数据）
YomkMsg(std::vector<yomk::ContextGroupInfo>, ContextGroupInfoList, d)
// ServerInfo 全量拓扑内省（SERVER_INFO_ALL 返回数据；层级为服务 → 函数元信息）
YomkMsg(std::vector<yomk::ServerServiceInfo>, ServerServiceInfoList, d)
YomkMsg(ContextChecker, ContextChecker, d)
YomkMsg(ContextMonitor, ContextMonitor, d)
YomkMsg(VoidPointer, VoidPointer, d)

// Context monitor 回调：仅通知发生了 set 并回传该次快照（不保证实时性；异步按提交序保序、同步并发下不保证序）；详见 YomkAPI::CONTEXT_SET_MONITOR
typedef std::function<void(const yomk::Context &ctx)> YomkContextMonitorFunc;
// Context checker 回调：返回 eAccept/eReject 门控 set；详见 YomkAPI::CONTEXT_SET_CHECKER
typedef std::function<yomk::ContextChecker::ECheckStatus(const yomk::Context &ctx)> YomkContextCheckFunc;
typedef std::function<bool(const yomk::Log &log)> YomkConsoleLogProxyFunc;

// 内置标准类型消息包（成员名均为 d）
YomkMsg(bool, Bool, d)
YomkMsg(std::vector<bool>, BoolArray, d)
YomkMsg(signed char, Char, d)
YomkMsg(std::vector<char>, CharArray, d)
YomkMsg(unsigned char, UChar, d)
YomkMsg(std::vector<unsigned char>, UCharArray, d)
YomkMsg(unsigned char, Byte, d)
YomkMsg(std::vector<unsigned char>, ByteArray, d)
YomkMsg(std::int8_t, Int8, d)
YomkMsg(std::vector<signed char>, Int8Array, d)
YomkMsg(std::uint8_t, Uint8, d)
YomkMsg(std::vector<unsigned char>, Uint8Array, d)
YomkMsg(std::int16_t, Int16, d)
YomkMsg(std::vector<std::int16_t>, Int16Array, d)
YomkMsg(std::uint16_t, Uint16, d)
YomkMsg(std::vector<std::uint16_t>, Uint16Array, d)
YomkMsg(std::int32_t, Int32, d)
YomkMsg(std::vector<std::int32_t>, Int32Array, d)
YomkMsg(std::uint32_t, Uint32, d)
YomkMsg(std::vector<std::uint32_t>, Uint32Array, d)
YomkMsg(std::int64_t, Int64, d)
YomkMsg(std::vector<std::int64_t>, Int64Array, d)
YomkMsg(std::uint64_t, Uint64, d)
YomkMsg(std::vector<std::uint64_t>, Uint64Array, d)
YomkMsg(float, Float32, d)
YomkMsg(std::vector<float>, Float32Array, d)
YomkMsg(double, Float64, d)
YomkMsg(std::vector<double>, Float64Array, d)
YomkMsg(std::string, String, d)
YomkMsg(std::vector<std::string>, StringArray, d)