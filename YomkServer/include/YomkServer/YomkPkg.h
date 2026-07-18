#pragma once
#include <memory>
#include <map>
#include <functional>
#include <string>

#define YomkInstallFunc(FuncName, Func) installFunc(FuncName, std::bind(&Func, this, std::placeholders::_1))

#define YomkUnPackPkgResponse(pkg, MsgName, ptrName)                             \
    if (!pkg || pkg->name() != #MsgName)                                         \
        return {YomkResponse::eNo, " pkg is null or pkg is not " #MsgName ". "}; \
    YomkPtr(MsgName) ptrName = std::dynamic_pointer_cast<Yomk(MsgName)>(pkg);  \
    if (!ptrName)                                                                  \
        return {YomkResponse::eNo, " pkg[" #MsgName "] is dynamic_pointer_cast failed. "};

#define YomkUnPackPkgVoid(pkg, MsgName, ptrName)                                \
    if (!pkg || pkg->name() != #MsgName)                                        \
        return;                                                                 \
    YomkPtr(MsgName) ptrName = std::dynamic_pointer_cast<Yomk(MsgName)>(pkg);   \
    if (!ptrName)                                                               \
        return;

#define YomkUnPackPkg(pkg, MsgName, ptrName)                     \
    YomkPtr(MsgName) ptrName = nullptr;                          \
    if (pkg && pkg->name() == #MsgName)                          \
    {                                                            \
        ptrName = std::dynamic_pointer_cast<Yomk(MsgName)>(pkg); \
    }

#define YomkUnPackPkgT(pkg, MsgName, ClassName, ptrName)     \
    std::shared_ptr<ClassName> ptrName = nullptr;            \
    if (pkg && pkg->name() == MsgName)                       \
    {                                                        \
        ptrName = std::dynamic_pointer_cast<ClassName>(pkg); \
    }

#define YomkMsg(DataType, MsgName, VarName)                 \
    namespace yomk                                          \
    {                                                       \
        class MsgName##_ : public YomkPkg                   \
        {                                                   \
        public:                                             \
            MsgName##_() { m_name = #MsgName; }             \
            MsgName##_(const DataType &value)               \
                : VarName(value) { m_name = #MsgName; }     \
            virtual ~MsgName##_() {}                        \
                                                            \
        public:                                             \
            DataType VarName;                               \
        };                                                  \
        typedef std::shared_ptr<MsgName##_> MsgName##Ptr;   \
    }

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

namespace yomk
{

    struct Function
    {
        std::string m_funcName;
        YomkServiceFunc m_func;
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
        Event()
            : m_eventLoopName(""), m_pkg(nullptr), m_serviceFunc(nullptr), m_waitCallback(nullptr) {}
        Event(const std::string &eventLoopName, YomkPkgPtr pkg, YomkServiceFunc serviceFunc)
            : m_eventLoopName(eventLoopName), m_pkg(pkg), m_serviceFunc(serviceFunc), m_waitCallback(nullptr) {}
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

// std msg
YomkMsg(bool, Bool, d)
YomkMsg(std::vector<bool>, BoolArray, d)
YomkMsg(signed char, Char, d)
YomkMsg(std::vector<char>, CharArray, d)
YomkMsg(unsigned char, UChar, d)
YomkMsg(std::vector<unsigned char>, UCharArray, d)
YomkMsg(unsigned char, Byte, d)
YomkMsg(std::vector<unsigned char>, ByteArray, d)
YomkMsg(signed char, Int8, d)
YomkMsg(std::vector<signed char>, Int8Array, d)
YomkMsg(unsigned char, Uint8, d)
YomkMsg(std::vector<unsigned char>, Uint8Array, d)
YomkMsg(signed short, Int16, d)
YomkMsg(std::vector<signed short>, Int16Array, d)
YomkMsg(unsigned short, Uint16, d)
YomkMsg(std::vector<unsigned short>, Uint16Array, d)
YomkMsg(signed int, Int32, d)
YomkMsg(std::vector<signed int>, Int32Array, d)
YomkMsg(unsigned int, Uint32, d)
YomkMsg(std::vector<unsigned int>, Uint32Array, d)
YomkMsg(signed long int, Int64, d)
YomkMsg(std::vector<signed long int>, Int64Array, d)
YomkMsg(unsigned long int, Uint64, d)
YomkMsg(std::vector<unsigned long int>, Uint64Array, d)
YomkMsg(float, Float32, d)
YomkMsg(std::vector<float>, Float32Array, d)
YomkMsg(double, Float64, d)
YomkMsg(std::vector<double>, Float64Array, d)
YomkMsg(std::string, String, d)
YomkMsg(std::vector<std::string>, StringArray, d)