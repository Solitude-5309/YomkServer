#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// 弱绑定服务成员函数，用于安全挂接外部回调
#define YomkBindWeakSelf(Func) weakFunc(std::bind(&Func, this, std::placeholders::_1))

// 注册服务成员功能函数（支持可选 MsgName 参数声明期望消息类型）
#define YOMK_INSTALL_FUNC_SELECT(_1, _2, _3, NAME, ...) NAME
#define YomkInstallFunc(...)                                                        \
    YOMK_INSTALL_FUNC_SELECT(__VA_ARGS__, YOMK_INSTALL_FUNC_3, YOMK_INSTALL_FUNC_2) \
    (__VA_ARGS__)
#define YOMK_INSTALL_FUNC_2(FuncName, Func) installFunc(FuncName, YomkBindWeakSelf(Func))
#define YOMK_INSTALL_FUNC_3(FuncName, Func, MsgName) installFunc(FuncName, YomkBindWeakSelf(Func), #MsgName)

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

// 请求调用结果对象
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
    EResStatus m_status;
    std::string m_msg;
    YomkPkgPtr m_data;
};
typedef std::shared_ptr<YomkResponse> YomkResponsePtr;

// 功能函数签名：入参为请求消息包（可为 nullptr），返回 YomkResponse
typedef std::function<YomkResponse(YomkPkgPtr pkg)> YomkServiceFunc;
// 异步响应回调签名：request 完成后回传结果
typedef std::function<void(YomkResponse response)> YomkResponseFunc;

// 功能函数元信息（调试内省用）
struct YomkFuncInfo
{
    std::string m_funcName;  // 功能函数名（/开头）
    std::string m_msgName;   // 期望消息类型名（三参宏声明，可为空）
};

namespace yomk
{

struct Function
{
    std::string m_funcName;
    YomkServiceFunc m_func;
    std::string m_msgName;
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

struct Context
{
    std::string m_key;
    YomkPkgPtr m_value;
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
};

struct ContextMonitor
{
    std::string m_key;
    std::function<void(Context ctx)> m_contextMonitorFunc;
    bool m_asyncMonitor = false;
};

struct VoidPointer
{
    void* m_ptr = nullptr;
};

}  // namespace yomk

// clang-format off
YomkMsg(Function, Function, d)
YomkMsg(CallFunction, CallFunction, d)
YomkMsg(Event, Event, d)
YomkMsg(Eventloop, Eventloop, d)
YomkMsg(LogFile, LogFile, d)
YomkMsg(Log, Log, d)
YomkMsg(ConsoleLogProxy, ConsoleLogProxy, d)
YomkMsg(Context, Context, d)
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