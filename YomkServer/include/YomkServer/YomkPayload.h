#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "YomkMsg.h"

namespace yomk::payload
{

/**
 * @brief 请求/操作三态收敛结果
 * @li Invalid：参数无效或未初始化（框架层拦截, 未到服务端）
 * @li Succeeded：成功
 * @li Failed：服务端执行失败, 含 not-found（资源不存在按本值, Invalid 仅用于参数无效）
 */
enum class ResStatus
{
    Invalid = -1,
    Succeeded = 0,
    Failed = 1,
};

/**
 * @brief 统一应答包：状态 + 文字说明 + 数据载体
 */
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

}  // namespace yomk::payload

using YomkServiceFunc = std::function<yomk::payload::Response(YomkMsgPtr pkg)>;
using YomkResponseFunc = std::function<void(yomk::payload::Response response)>;

namespace yomk::payload
{

enum class CheckStatus
{
    Accept,
    Reject
};

enum class LogLevel : int
{
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Fatal,
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
    std::string m_tag;  // 日志标签：随记录显示 [tag] 段，区别于 m_logger
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

// 事件循环单查载荷（/YomkEventLoop/loop 专用）：名称 + 队列头 tag 采样数, 类型化字段无解析歧义
struct EventLoopQuery
{
    std::string m_loopName;      // 循环名（按字面量精确匹配，不做任何切分）
    std::size_t m_tagCount = 3;  // 队列头采样条数（缺省 3，与全量内省缺省一致）
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
    bool m_traceOn = false;  // trace 级别开关
    bool m_debugOn = false;  // debug 级别开关
    bool m_infoOn = false;   // info 级别开关
    bool m_warnOn = false;   // warn 级别开关
    bool m_errorOn = false;  // error 级别开关
    bool m_fatalOn = false;  // fatal 级别开关
    bool m_proxyOn = false;
};

struct LoggerInfo
{
    std::string m_loggerName;  // 日志器名
    std::string m_type;        // 类型："console" 或 "file"（对应 log_mgr() 的 console/file 分面身份）
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

}  // namespace yomk::payload

/**
 * @brief Context 变更监听回调：仅通知发生了 set 并回传该次快照
 * @param ctx 该次 set 的键值快照
 * @note 不保证快照实时性；异步形态按提交序保序、同步形态并发下不保证序。详见 ctx().monitor_fn(fn).apply()
 */
using YomkContextMonitorFunc = std::function<void(const yomk::payload::Context& ctx)>;
/**
 * @brief Context 校验门控回调：门控 set 是否放行
 * @param ctx 待写入的键值
 * @return Accept 放行 / Reject 拒绝
 * @note 详见 ctx().checker_fn(fn).apply()
 */
using YomkContextCheckerFunc = std::function<yomk::payload::CheckStatus(const yomk::payload::Context& ctx)>;
/**
 * @brief 控制台日志代理回调
 * @param log 待输出的日志条目
 * @return false 拦截该条不输出、true 穿透走默认输出
 * @note 详见 yomk::log_mgr().console().proxy(fn).apply()
 */
using YomkConsoleLogProxyFunc = std::function<bool(const yomk::payload::Log& log)>;
/**
 * @brief 文件日志代理回调
 * @param log 待落盘的日志条目
 * @return false 拦截该条不落盘、true 穿透走默认落盘
 * @note 详见 yomk::log_mgr().file().proxy(fn).apply()
 */
using YomkFileLogProxyFunc = std::function<bool(const yomk::payload::Log& log)>;
