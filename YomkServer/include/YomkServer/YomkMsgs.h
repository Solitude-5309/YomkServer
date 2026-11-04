#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <utility>
#include <vector>

#include "YomkPkg.h"

#define YomkMsg(DataType, MsgName, VarName)                                       \
    namespace yomkmsg                                                             \
    {                                                                             \
    class MsgName : public ::YomkPkg                                              \
    {                                                                             \
    public:                                                                       \
        static constexpr const char* kName = #MsgName;                            \
        MsgName() { m_name = kName; }                                             \
        MsgName(const DataType& value) : VarName(value) { m_name = kName; }       \
        MsgName(DataType&& value) : VarName(std::move(value)) { m_name = kName; } \
        virtual ~MsgName() {}                                                     \
                                                                                  \
    public:                                                                       \
        DataType VarName{};                                                       \
                                                                                  \
        template <typename... A>                                                  \
        static std::shared_ptr<MsgName> shared(A&&... a)                          \
        {                                                                         \
            return std::make_shared<MsgName>(std::forward<A>(a)...);              \
        }                                                                         \
    };                                                                            \
    using MsgName##Ptr = std::shared_ptr<MsgName>;                                \
    }

namespace yomk
{

enum class ResStatus
{
    Invalid = -1,
    Succeeded = 0,
    Failed = 1,
};

struct Response
{
    Response() = default;
    Response(ResStatus status, const std::string& msg = "", YomkPkgPtr d = nullptr)
        : m_status(status), m_msg(msg), m_data(d)
    {
    }
    ResStatus m_status = ResStatus::Invalid;  // 本次请求的状态
    std::string m_msg;                        // 对状态的说明（不携带返回数据）
    YomkPkgPtr m_data = nullptr;              // 携带的返回数据
};

}  // namespace yomk

using YomkServiceFunc = std::function<yomk::Response(YomkPkgPtr pkg)>;
using YomkResponseFunc = std::function<void(yomk::Response response)>;

namespace yomk
{

template <class Msg>
std::shared_ptr<Msg> unPack(const YomkPkgPtr& pkg)
{
    if (pkg && pkg->name() == Msg::kName)
    {
        return std::dynamic_pointer_cast<Msg>(pkg);
    }
    return nullptr;
}

struct FuncInfo
{
    std::string m_funcName;     // 功能函数名（/开头）
    std::string m_msgName;      // 期望消息类型名（installFunc 第三参声明，可为空）
    std::string m_description;  // 函数说明（installFunc 第四参声明，内容使用者自定义，可为空）
};

struct Function
{
    std::string m_funcName;
    YomkServiceFunc m_func;
    std::string m_msgName;
    std::string m_description;  
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
    Response m_response;
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

struct EventLoop
{
    std::string m_eventLoopName;
    YomkServiceFunc m_defaultServiceFunc;
    std::string m_msgName;  // 默认处理函数期望的消息类型名（仅内省元数据，可为空）
};

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
    enum class LogLevel : int
    {
        Debug,
        Info,
        Warn,
        Error,
    };
    LogLevel m_level;
    std::string m_log;
    std::string m_logger;  // 日志器名：服务端按其查找/惰性创建日志器实例
    std::string m_tag;  // 日志标签：随记录显示 [tag] 段，区别于 m_logger（末位追加，旧位置初始化兼容）
};

struct ConsoleLogProxy
{
    std::function<bool(const Log& log)> m_consoleLogProxyFunc;
};

struct FileLogProxy
{
    std::function<bool(const Log& log)> m_fileLogProxyFunc;
};

struct LoggerState
{
    bool m_debugOn = false;  // debug 级别开关
    bool m_infoOn = false;   // info 级别开关
    bool m_warnOn = false;   // warn 级别开关
    bool m_errorOn = false;  // error 级别开关
    bool m_proxyOn = false;
};

struct LoggerQuery
{
    std::string m_loggerName;
    std::string m_type;
};

struct LoggerInfo
{
    std::string m_loggerName;  // 日志器名
    std::string m_type;        // 类型："console" 或 "file"（与 listLoggers/getLoggerInfo 的类型参数同名）
    std::string m_dir;         // file 类型的日志目录（console 时为空串）
    LoggerState m_loggerState;
};

struct Context
{
    std::string m_key;
    YomkPkgPtr m_value;
    std::string m_group = "main";  // key 所属组名（缺省 main）
};

struct ContextKeyInfo
{
    std::string m_key;                    // 键名
    std::string m_typeName;               // 值消息包类型名（值为空时为空串）
    bool m_checkerOn = false;             // 门控是否开启
    std::size_t m_monitorCount = 0;       // monitor 总数
    std::size_t m_asyncMonitorCount = 0;  // 其中 async monitor 数
    std::string m_group = "main";         // key 所属组名
};

enum class CheckStatus
{
    Accept,
    Reject
};

struct ContextChecker
{
    std::string m_key;
    std::function<CheckStatus(const yomk::Context& ctx)> m_checkFunc;
    std::string m_group = "main";  // 目标 key 所属组名（缺省 main）
};

struct ContextMonitor
{
    std::string m_key;
    std::function<void(Context ctx)> m_contextMonitorFunc;
    bool m_asyncMonitor = false;
    std::string m_group = "main";  // 目标 key 所属组名（缺省 main）
};

struct ContextGroupKey
{
    std::string m_group = "main";  // 组名（缺省 main）
    std::string m_key;             // 键名
};

struct ContextGroupInfo
{
    std::string m_groupName;             // 组名
    std::vector<ContextKeyInfo> m_keys;  // 该组全部键的内省元信息（map 字典序）
};

struct ServerServiceInfo
{
    std::string m_serviceName;      // 服务名（带 / 前缀，如 "/FmtSrv"）
    std::vector<FuncInfo> m_funcs;  // 函数元信息（与 /function 单查同族，m_funcName 带 / 前缀 url 段）
};

struct VoidPointer
{
    void* m_ptr = nullptr;
};

}  // namespace yomk

// SDK 消息包目录：框架载荷包 + 内置标准类型包（成员名均为 d）。
// 自定义消息不在此定义，经 YomkPkg.h 的 YomkMsg 宏自行声明。

// ============ 框架载荷消息包 ============
// 服务响应载荷包（功能函数返回值的包形态，供响应作为消息存储/传递）
YomkMsg(yomk::Response, Response, d);
YomkMsg(yomk::Function, Function, d);
YomkMsg(yomk::CallFunction, CallFunction, d);
YomkMsg(yomk::FuncInfo, FuncInfo, d);
// FunctionPool 全量函数内省（getAllFunctionInfo 返回数据；函数总数由 d.size() 承载）
YomkMsg(std::vector<yomk::FuncInfo>, FuncInfoList, d);
YomkMsg(yomk::Event, Event, d);
YomkMsg(yomk::EventLoop, EventLoop, d);
YomkMsg(yomk::EventLoopInfo, EventLoopInfo, d);
// EventLoop 全量循环内省（getAllEventLoopInfo 返回数据；m_nextTags 取 3 条与单查缺省一致）
YomkMsg(std::vector<yomk::EventLoopInfo>, EventLoopInfoList, d);
YomkMsg(yomk::LogFile, LogFile, d);
YomkMsg(yomk::Log, Log, d);
YomkMsg(yomk::ConsoleLogProxy, ConsoleLogProxy, d);
YomkMsg(yomk::FileLogProxy, FileLogProxy, d);
YomkMsg(yomk::LoggerQuery, LoggerQuery, d);
YomkMsg(yomk::LoggerInfo, LoggerInfo, d);
// Logger 控制台全局主闸态（getGlobalLoggerLevel 返回数据：四级别主闸开关 + proxy）
YomkMsg(yomk::LoggerState, LoggerState, d);
// Logger 全量内省（getAllLoggerInfo 返回数据；元素为单查同款 LoggerInfo，每条内嵌 LoggerState 状态段）
YomkMsg(std::vector<yomk::LoggerInfo>, LoggerInfoList, d);
YomkMsg(yomk::Context, Context, d);
// Context 组寻址载体（/destroy 与 /key 请求报文：定位某组下的某键，组名缺省 main）
YomkMsg(yomk::ContextGroupKey, ContextGroupKey, d);
YomkMsg(yomk::ContextKeyInfo, ContextKeyInfo, d);
// Context 键元信息清单（ContextGroupInfo.m_keys 的元素类型）
YomkMsg(std::vector<yomk::ContextKeyInfo>, ContextKeyInfoList, d);
// Context 组级内省条目（ContextGroupInfoList 元素；层级为组 → 键元信息）
YomkMsg(yomk::ContextGroupInfo, ContextGroupInfo, d);
// Context 组级全量内省（getAllContextInfo 返回数据）
YomkMsg(std::vector<yomk::ContextGroupInfo>, ContextGroupInfoList, d);
// ServerInfo 全量拓扑内省（getAllServerInfo 返回数据；层级为服务 → 函数元信息）
YomkMsg(std::vector<yomk::ServerServiceInfo>, ServerServiceInfoList, d);
YomkMsg(yomk::ContextChecker, ContextChecker, d);
YomkMsg(yomk::ContextMonitor, ContextMonitor, d);
YomkMsg(yomk::VoidPointer, VoidPointer, d);

// Context monitor 回调：仅通知发生了 set 并回传该次快照（不保证实时性；异步按提交序保序、同步并发下不保证序）；详见 YomkAPI::setContextMonitor
using YomkContextMonitorFunc = std::function<void(const yomk::Context& ctx)>;
// Context checker 回调：返回 CheckStatus::Accept/CheckStatus::Reject 门控 set；详见 YomkAPI::setContextChecker
using YomkContextCheckerFunc = std::function<yomk::CheckStatus(const yomk::Context& ctx)>;
using YomkConsoleLogProxyFunc = std::function<bool(const yomk::Log& log)>;
// 文件日志代理回调：返回 false 拦截该条不落盘、true 穿透走默认落盘；详见 YomkAPI::setFileLogProxy
using YomkFileLogProxyFunc = std::function<bool(const yomk::Log& log)>;

// 内置标准类型消息包（成员名均为 d）
YomkMsg(bool, Bool, d);
YomkMsg(std::vector<bool>, BoolArray, d);
YomkMsg(signed char, Char, d);
YomkMsg(std::vector<char>, CharArray, d);
YomkMsg(unsigned char, UChar, d);
YomkMsg(std::vector<unsigned char>, UCharArray, d);
YomkMsg(unsigned char, Byte, d);
YomkMsg(std::vector<unsigned char>, ByteArray, d);
YomkMsg(std::int8_t, Int8, d);
YomkMsg(std::vector<signed char>, Int8Array, d);
YomkMsg(std::uint8_t, Uint8, d);
YomkMsg(std::vector<unsigned char>, Uint8Array, d);
YomkMsg(std::int16_t, Int16, d);
YomkMsg(std::vector<std::int16_t>, Int16Array, d);
YomkMsg(std::uint16_t, Uint16, d);
YomkMsg(std::vector<std::uint16_t>, Uint16Array, d);
YomkMsg(std::int32_t, Int32, d);
YomkMsg(std::vector<std::int32_t>, Int32Array, d);
YomkMsg(std::uint32_t, Uint32, d);
YomkMsg(std::vector<std::uint32_t>, Uint32Array, d);
YomkMsg(std::int64_t, Int64, d);
YomkMsg(std::vector<std::int64_t>, Int64Array, d);
YomkMsg(std::uint64_t, Uint64, d);
YomkMsg(std::vector<std::uint64_t>, Uint64Array, d);
YomkMsg(float, Float32, d);
YomkMsg(std::vector<float>, Float32Array, d);
YomkMsg(double, Float64, d);
YomkMsg(std::vector<double>, Float64Array, d);
YomkMsg(std::string, String, d);
YomkMsg(std::vector<std::string>, StringArray, d);
