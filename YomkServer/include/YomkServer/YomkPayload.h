#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "YomkMsg.h"

namespace yomk
{
namespace payload
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
    Response(ResStatus status, const std::string& msg = "", YomkMsgPtr d = nullptr)
        : m_status(status), m_msg(msg), m_data(d)
    {
    }
    ResStatus m_status = ResStatus::Invalid;  // 本次请求的状态
    std::string m_msg;                        // 对状态的说明（不携带返回数据）
    YomkMsgPtr m_data = nullptr;              // 携带的返回数据
};

}  // namespace payload
}  // namespace yomk

using YomkServiceFunc = std::function<yomk::payload::Response(YomkMsgPtr pkg)>;
using YomkResponseFunc = std::function<void(yomk::payload::Response response)>;

namespace yomk
{
namespace payload
{

enum class CheckStatus
{
    Accept,
    Reject
};

enum class LogLevel : int
{
    Debug,
    Info,
    Warn,
    Error,
};

struct Context
{
    std::string m_key;
    YomkMsgPtr m_value;
    std::string m_group = "main";  // key 所属组名（缺省 main）
};

struct Log
{
    LogLevel m_level;
    std::string m_log;
    std::string m_logger;  // 日志器名：服务端按其查找/惰性创建日志器实例
    std::string m_tag;  // 日志标签：随记录显示 [tag] 段，区别于 m_logger（末位追加，旧位置初始化兼容）
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
    YomkMsgPtr m_pkg;
};

struct Event
{
    virtual ~Event() = default;
    std::string m_eventLoopName;
    YomkMsgPtr m_pkg;
    YomkServiceFunc m_serviceFunc;
    std::uint64_t m_eventId = 0;
    Response m_response;
    std::function<void()> m_waitCallback;
    std::string m_tag;
    Event() : m_eventLoopName(""), m_pkg(nullptr), m_serviceFunc(nullptr), m_waitCallback(nullptr), m_tag("") {}
    Event(const std::string& eventLoopName, YomkMsgPtr pkg, YomkServiceFunc serviceFunc)
        : m_eventLoopName(eventLoopName), m_pkg(pkg), m_serviceFunc(serviceFunc), m_waitCallback(nullptr), m_tag("")
    {
    }
    Event(const std::string& eventLoopName, YomkMsgPtr pkg, YomkServiceFunc serviceFunc, const std::string& tag)
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

struct LogFile
{
    std::string m_logger;
    std::string m_dir;
};

struct ConsoleLogProxy
{
    std::function<bool(const Log& log)> m_consoleLogProxyFunc;
};

struct FileLogProxy
{
    std::function<bool(const Log& log)> m_fileLogProxyFunc;
};

struct LoggerQuery
{
    std::string m_loggerName;
    std::string m_type;
};

struct ContextChecker
{
    std::string m_key;
    std::function<CheckStatus(const yomk::payload::Context& ctx)> m_checkFunc;
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

struct VoidPointer
{
    void* m_ptr = nullptr;
};

struct FuncInfo
{
    std::string m_funcName;     // 功能函数名（/开头）
    std::string m_msgName;      // 期望消息类型名（installFunc 第三参声明，可为空）
    std::string m_description;  // 函数说明（installFunc 第四参声明，内容使用者自定义，可为空）
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

struct LoggerState
{
    bool m_debugOn = false;  // debug 级别开关
    bool m_infoOn = false;   // info 级别开关
    bool m_warnOn = false;   // warn 级别开关
    bool m_errorOn = false;  // error 级别开关
    bool m_proxyOn = false;
};

struct LoggerInfo
{
    std::string m_loggerName;  // 日志器名
    std::string m_type;        // 类型："console" 或 "file"（与 listLoggers/getLoggerInfo 的类型参数同名）
    std::string m_dir;         // file 类型的日志目录（console 时为空串）
    LoggerState m_loggerState;
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

}  // namespace payload
}  // namespace yomk

// Context monitor 回调：仅通知发生了 set 并回传该次快照（不保证实时性；异步按提交序保序、同步并发下不保证序）；详见 YomkAPI::setContextMonitor
using YomkContextMonitorFunc = std::function<void(const yomk::payload::Context& ctx)>;
// Context checker 回调：返回 CheckStatus::Accept/CheckStatus::Reject 门控 set；详见 YomkAPI::setContextChecker
using YomkContextCheckerFunc = std::function<yomk::payload::CheckStatus(const yomk::payload::Context& ctx)>;
using YomkConsoleLogProxyFunc = std::function<bool(const yomk::payload::Log& log)>;
// 文件日志代理回调：返回 false 拦截该条不落盘、true 穿透走默认落盘；详见 YomkAPI::setFileLogProxy
using YomkFileLogProxyFunc = std::function<bool(const yomk::payload::Log& log)>;
