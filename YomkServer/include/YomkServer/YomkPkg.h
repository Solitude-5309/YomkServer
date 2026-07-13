#pragma once
#include <memory>
#include <map>
#include <functional>
#include <string>

#define YomkInstallFunc(FuncName, Func) installFunc(FuncName, std::bind(&Func, this, std::placeholders::_1))

#define YomkUnPackPkgResponse(pkg, ClassName, ptrName)                              \
    if (!pkg || pkg->name() != #ClassName)                                          \
        return {YomkResponse::eErr, " pkg is null or pkg is not " #ClassName ". "}; \
    YomkPtr(ClassName) ptrName = std::dynamic_pointer_cast<Yomk(ClassName)>(pkg);   \
    if (!ptrName)                                                                   \
        return {YomkResponse::eErr, " pkg[" #ClassName "] is dynamic_pointer_cast failed. "};

#define YomkUnPackPkgVoid(pkg, ClassName, ptrName)                                \
    if (!pkg || pkg->name() != #ClassName)                                        \
        return;                                                                   \
    YomkPtr(ClassName) ptrName = std::dynamic_pointer_cast<Yomk(ClassName)>(pkg); \
    if (!ptrName)                                                                 \
        return;

#define YomkUnPackPkg(pkg, ClassName, ptrName)                     \
    YomkPtr(ClassName) ptrName = nullptr;                          \
    if (pkg && pkg->name() == #ClassName)                          \
    {                                                              \
        ptrName = std::dynamic_pointer_cast<Yomk(ClassName)>(pkg); \
    }

#define YomkUnPackPkgT(pkg, pkgName, ClassName, ptrName)     \
    std::shared_ptr<ClassName> ptrName = nullptr;            \
    if (pkg && pkg->name() == pkgName)                       \
    {                                                        \
        ptrName = std::dynamic_pointer_cast<ClassName>(pkg); \
    }

#define YomkMsg(IType, OType, VarName)                \
    namespace yomk                                    \
    {                                                 \
        class OType##_ : public YomkPkg               \
        {                                             \
        public:                                       \
            OType##_() { m_name = #OType; }           \
            OType##_(const IType &value)              \
                : VarName(value) { m_name = #OType; } \
            virtual ~OType##_() {}                    \
                                                      \
        public:                                       \
            IType VarName;                            \
        };                                            \
        typedef std::shared_ptr<OType##_> OType##Ptr; \
    }

#define Yomk(Type) yomk::Type##_
#define YomkPtr(Type) yomk::Type##Ptr
#define YomkMkPtr(Type, ...) std::make_shared<yomk::Type##_>(__VA_ARGS__)

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
        eErr = 1,
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
YomkMsg(bool, bool, d)
YomkMsg(std::vector<bool>, boolarray, d)
YomkMsg(signed char, char, d)
YomkMsg(std::vector<char>, chararray, d)
YomkMsg(unsigned char, uchar, d)
YomkMsg(std::vector<unsigned char>, uchararray, d)
YomkMsg(unsigned char, byte, d)
YomkMsg(std::vector<unsigned char>, bytearray, d)
YomkMsg(signed char, int8, d)
YomkMsg(std::vector<signed char>, int8array, d)
YomkMsg(unsigned char, uint8, d)
YomkMsg(std::vector<unsigned char>, uint8array, d)
YomkMsg(signed short, int16, d)
YomkMsg(std::vector<signed short>, int16array, d)
YomkMsg(unsigned short, uint16, d)
YomkMsg(std::vector<unsigned short>, uint16array, d)
YomkMsg(signed int, int32, d)
YomkMsg(std::vector<signed int>, int32array, d)
YomkMsg(unsigned int, uint32, d)
YomkMsg(std::vector<unsigned int>, uint32array, d)
YomkMsg(signed long int, int64, d)
YomkMsg(std::vector<signed long int>, int64array, d)
YomkMsg(unsigned long int, uint64, d)
YomkMsg(std::vector<unsigned long int>, uint64array, d)
YomkMsg(float, float32, d)
YomkMsg(std::vector<float>, float32array, d)
YomkMsg(double, float64, d)
YomkMsg(std::vector<double>, float64array, d)
YomkMsg(std::string, string, d)
YomkMsg(std::vector<std::string>, stringarray, d)