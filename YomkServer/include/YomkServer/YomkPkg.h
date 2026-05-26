#pragma once
#include <memory>
#include <map>
#include <functional>
#include <string>

class YomkServer;

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

class YomkResponse
{
public:
    enum EResStatus{
        eInvalid = -1,
        eOk = 0,
        eErr = 1,
    };
public:
    YomkResponse() 
        : m_resStatus(eInvalid)
        , m_data(nullptr) {}
    YomkResponse(
        EResStatus status, 
        const std::string& msg="",
        std::shared_ptr<YomkPkg> d = nullptr ) 
        : m_resStatus(status)
        , m_msg(msg)
        , m_data(d) { }
    virtual ~YomkResponse() {}
public:
    EResStatus m_resStatus;
    std::string m_msg;
    YomkPkgPtr m_data;
};
typedef std::shared_ptr<YomkResponse> YomkResponsePtr;

typedef std::function<YomkResponse (YomkPkgPtr pkg)> YomkServiceFunc;
typedef std::function<void (YomkResponse response)> YomkResponseFunc;

#define YomkMsg(IType, OType)                   \
namespace yomk                                  \
{                                               \
class OType##_ : public YomkPkg                 \
{                                               \
public:                                         \
    OType##_() { m_name = #OType; }             \
    OType##_(const IType& d)                    \
        : d(d) { m_name = #OType; }             \
    virtual ~OType##_() {}                      \
public:                                         \
    IType d;                                    \
};                                              \
typedef std::shared_ptr<OType##_> OType##Ptr;   \
}

#define Yomk(Type) yomk::Type##_
#define YomkPtr(Type) yomk::Type##Ptr
#define YomkMkPtr(Type, ...) std::make_shared<yomk::Type##_>(__VA_ARGS__)


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
        : m_eventLoopName("")
        , m_pkg(nullptr)
        , m_serviceFunc(nullptr)
        , m_waitCallback(nullptr){}
    Event(const std::string& eventLoopName
        , YomkPkgPtr pkg
        , YomkServiceFunc serviceFunc)
        : m_eventLoopName(eventLoopName)
        , m_pkg(pkg)
        , m_serviceFunc(serviceFunc)
        , m_waitCallback(nullptr){}
    virtual void handle()
    {
        if(m_serviceFunc)
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
    typedef std::function<ECheckStatus (YomkPkgPtr pkg)> ContextCheckFunc;
    std::string m_key;
    ContextCheckFunc m_checkFunc; 
};  

struct ContextMonitor
{
    std::string m_key;
    std::function<void (Context ctx)> m_contextMonitorFunc;
};

}

YomkMsg(Function, Function)
YomkMsg(CallFunction, CallFunction)
YomkMsg(Event, Event)
YomkMsg(Eventloop, Eventloop)
YomkMsg(LogFile, LogFile)
YomkMsg(Log, Log)
YomkMsg(Context, Context)
YomkMsg(ContextChecker, ContextChecker)
YomkMsg(ContextMonitor, ContextMonitor)
YomkMsg(std::string, string)

typedef std::function<void (const yomk::Context& ctx)> YomkContextMonitorFunc;
