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

class YomkResponse : public YomkPkg
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
        , m_data(nullptr) {  
            m_name = "YomkResponse"; }
    YomkResponse(
        EResStatus status, 
        const std::string& msg="",
        std::shared_ptr<YomkPkg> d = nullptr ) 
        : m_resStatus(status)
        , m_msg(msg)
        , m_data(d) { 
            m_name = "YomkResponse"; }
    virtual ~YomkResponse() {}
public:
    EResStatus m_resStatus;
    std::string m_msg;
    std::shared_ptr<YomkPkg> m_data;
};
typedef std::shared_ptr<YomkResponse> YomkResponsePtr;

typedef std::function<YomkResponse (YomkPkgPtr pkg)> YomkServiceFunc;
typedef std::function<void (YomkResponse response)> YomkResponseFunc;

class YomkEvent : public YomkPkg
{
public:
    YomkEvent() { m_name = "YomkEvent"; }
    YomkEvent(
        const std::string& eventLoopName,
        YomkPkgPtr pkg, 
        YomkServiceFunc serviceFunc)
        : m_pkg(pkg)
        , m_serviceFunc(serviceFunc){ 
            m_name = "YomkEvent";
            m_eventLoopName = eventLoopName; 
            m_eventId = 0;
        }
    virtual ~YomkEvent() {}
public:
    virtual void handle()
    {
        if(m_serviceFunc)
        {
            m_response = m_serviceFunc(m_pkg);
        }
    }
public:
    std::uint64_t m_eventId;
    std::string m_eventLoopName;
    YomkPkgPtr m_pkg;
    YomkResponse m_response;
    YomkServiceFunc m_serviceFunc;
};
typedef std::shared_ptr<YomkEvent> YomkEventPtr;
#define YomkMkYomkEventPtr(eventLoopName, pkg, serviceFunc) std::make_shared<YomkEvent>(eventLoopName, pkg, serviceFunc)

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

YomkMsg(std::string, string)

struct Function
{
    std::string m_funcName;
    YomkServiceFunc m_func;
};
YomkMsg(Function, Function)

struct CallFunction
{
    std::string m_funcName;
    YomkPkgPtr m_pkg;
};
YomkMsg(CallFunction, CallFunction)

struct Eventloop
{
    std::string m_eventloopName;
    YomkServiceFunc m_defaultServiceFunc;
};
YomkMsg(Eventloop, Eventloop)

struct LogFile
{
    std::string m_logger;
    std::string m_dir;
};
YomkMsg(LogFile, LogFile)

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
YomkMsg(Log, Log)

struct Context
{
    std::string m_key;
    YomkPkgPtr m_value;
};
YomkMsg(Context, Context)
typedef std::function<void (YomkPtr(Context) ctx)> YomkContextMonitorFunc;

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
YomkMsg(ContextChecker, ContextChecker)

struct ContextMonitor
{
    std::string m_key;
    YomkContextMonitorFunc m_contextMonitorFunc;
};
YomkMsg(ContextMonitor, ContextMonitor)