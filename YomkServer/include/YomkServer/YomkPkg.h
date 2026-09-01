#pragma once
#include <memory>
#include <map>
#include <functional>
#include <string>
#include <cstdint>

// 弱绑定服务成员函数：把服务成员函数注册到外部子系统（功能函数、FunctionPool、
// EventLoop、Context checker/monitor、异步响应）的回调时必须使用。
// 服务被删除后回调自动安全丢弃，不会悬垂 this 崩溃；同一个宏适配全部回调签名，无需按签名区分。
#define YomkBindWeakSelf(Func) weakFunc(std::bind(&Func, this, std::placeholders::_1))

// 弱绑定服务成员函数并装入本服务 funcMap（推荐在 init() 中使用）：
// 两参形式 YomkInstallFunc(FuncName, Func)；
// 三参形式 YomkInstallFunc(FuncName, Func, MsgName) 额外声明该函数期望的消息类型，
// 字符串化后仅作内省元数据（/YomkServerInfo 可见），不参与运行时校验。
#define YOMK_INSTALL_FUNC_SELECT(_1, _2, _3, NAME, ...) NAME
#define YomkInstallFunc(...) YOMK_INSTALL_FUNC_SELECT(__VA_ARGS__, YOMK_INSTALL_FUNC_3, YOMK_INSTALL_FUNC_2)(__VA_ARGS__)
#define YOMK_INSTALL_FUNC_2(FuncName, Func) installFunc(FuncName, YomkBindWeakSelf(Func))
#define YOMK_INSTALL_FUNC_3(FuncName, Func, MsgName) installFunc(FuncName, YomkBindWeakSelf(Func), #MsgName)

// 解包：校验消息类型并转为具体消息指针，失败自动 return {eNo, ...}（用于返回 YomkResponse 的函数）
#define YomkUnPackPkgResponse(pkg, MsgName, ptrName)                             \
    if (!pkg || pkg->name() != #MsgName)                                         \
        return {YomkResponse::eNo, " pkg is null or pkg is not " #MsgName ". "}; \
    YomkPtr(MsgName) ptrName = std::dynamic_pointer_cast<Yomk(MsgName)>(pkg);    \
    if (!ptrName)                                                                \
        return {YomkResponse::eNo, " pkg[" #MsgName "] is dynamic_pointer_cast failed. "};

// 解包：校验消息类型并转为具体消息指针，失败自动 return（用于 void 函数）
#define YomkUnPackPkgVoid(pkg, MsgName, ptrName)                              \
    if (!pkg || pkg->name() != #MsgName)                                      \
        return;                                                               \
    YomkPtr(MsgName) ptrName = std::dynamic_pointer_cast<Yomk(MsgName)>(pkg); \
    if (!ptrName)                                                             \
        return;

// 解包：校验消息类型并转为具体消息指针，失败不 return（指针为空），需手动判空
#define YomkUnPackPkg(pkg, MsgName, ptrName)                     \
    YomkPtr(MsgName) ptrName = nullptr;                          \
    if (pkg && pkg->name() == #MsgName)                          \
    {                                                            \
        ptrName = std::dynamic_pointer_cast<Yomk(MsgName)>(pkg); \
    }

// 解包：按运行时字符串名匹配消息类型（供框架内部使用）
#define YomkUnPackPkgT(pkg, MsgName, ClassName, ptrName)     \
    std::shared_ptr<ClassName> ptrName = nullptr;            \
    if (pkg && pkg->name() == MsgName)                       \
    {                                                        \
        ptrName = std::dynamic_pointer_cast<ClassName>(pkg); \
    }

// 定义消息包：为数据类 DataType 生成可传输的消息类型，建议放在命名空间外、所有结构体定义之后统一声明：
// 生成类型 yomk::MsgName_（类）与 yomk::MsgNamePtr（shared_ptr），
// 消息名只能出现在 Yomk/YomkPtr/YomkMkPtr/YomkUnPackPkg* 等宏的参数位置，不能当裸类型名使用；
// VarName 为消息包中携带数据的成员变量名，取数据时经 ptr->VarName 访问
#define YomkMsg(DataType, MsgName, VarName)               \
    namespace yomk                                        \
    {                                                     \
        class MsgName##_ : public YomkPkg                 \
        {                                                 \
        public:                                           \
            MsgName##_() { m_name = #MsgName; }           \
            MsgName##_(const DataType &value)             \
                : VarName(value) { m_name = #MsgName; }   \
            virtual ~MsgName##_() {}                      \
                                                          \
        public:                                           \
            DataType VarName;                             \
        };                                                \
        typedef std::shared_ptr<MsgName##_> MsgName##Ptr; \
    }

// 消息包辅助宏：消息名 → 类型 / 指针类型 / 构造实例 / 创建消息包 shared_ptr（请求入参常用 YomkMkPtr）
#define Yomk(MsgName) yomk::MsgName##_
#define YomkMk(MsgName, ...) yomk::MsgName##_(__VA_ARGS__)
#define YomkPtr(MsgName) yomk::MsgName##Ptr
#define YomkMkPtr(MsgName, ...) std::make_shared<yomk::MsgName##_>(__VA_ARGS__)

class YomkServer;

class YomkPkg
{
public:
    YomkPkg() {}
    virtual ~YomkPkg() {}

public:
    void name(const std::string &name) { m_name = name; }
    std::string name() { return m_name; }

protected:
    std::string m_name;
};
typedef std::shared_ptr<YomkPkg> YomkPkgPtr;

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
    YomkResponse()
        : m_status(eInvalid), m_data(nullptr) {}
    YomkResponse(
        EResStatus status,
        const std::string &msg = "",
        std::shared_ptr<YomkPkg> d = nullptr)
        : m_status(status), m_msg(msg), m_data(d) {}
    virtual ~YomkResponse() {}

public:
    EResStatus m_status;
    std::string m_msg;
    YomkPkgPtr m_data;
};
typedef std::shared_ptr<YomkResponse> YomkResponsePtr;

typedef std::function<YomkResponse(YomkPkgPtr pkg)> YomkServiceFunc;
typedef std::function<void(YomkResponse response)> YomkResponseFunc;

// 功能函数元信息（调试内省用）
struct YomkFuncInfo
{
    std::string m_funcName; // 功能函数名（/开头）
    std::string m_msgName;  // 期望消息类型名（三参宏声明，可为空）
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
        std::string m_eventLoopName;
        YomkPkgPtr m_pkg;
        YomkServiceFunc m_serviceFunc;
        std::uint64_t m_eventId;
        YomkResponse m_response;
        std::function<void()> m_waitCallback;
        std::string m_tag;
        Event()
            : m_eventLoopName(""), m_pkg(nullptr), m_serviceFunc(nullptr), m_waitCallback(nullptr), m_tag("") {}
        Event(const std::string &eventLoopName, YomkPkgPtr pkg, YomkServiceFunc serviceFunc)
            : m_eventLoopName(eventLoopName), m_pkg(pkg), m_serviceFunc(serviceFunc), m_waitCallback(nullptr), m_tag("") {}
        Event(const std::string &eventLoopName, YomkPkgPtr pkg, YomkServiceFunc serviceFunc, const std::string &tag)
            : m_eventLoopName(eventLoopName), m_pkg(pkg), m_serviceFunc(serviceFunc), m_waitCallback(nullptr), m_tag(tag) {}
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
        std::string m_msgName; // 默认处理函数期望的消息类型名（仅内省元数据，可为空）
    };

    struct LogFile
    {
        std::string m_logger;
        std::string m_dir;
    };

    struct Log
    {
        enum ELogLevel
        {
            eDebug,
            eInfo,
            eWarn,
            eError,
        };
        ELogLevel m_level;
        std::string m_log;
        std::string m_logger;
    };

    struct ConsoleLogProxy
    {
        std::function<bool(const Log &log)> m_consoleLogProxyFunc;
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
        std::function<ECheckStatus(const yomk::Context &ctx)> m_checkFunc;
    };

    struct ContextMonitor
    {
        std::string m_key;
        std::function<void(Context ctx)> m_contextMonitorFunc;
        bool m_asyncMonitor;
    };

    struct VoidPointer
    {
        void *m_ptr;
    };

}
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

typedef std::function<void(const yomk::Context &ctx)> YomkContextMonitorFunc;
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