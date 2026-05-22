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
        YomkServiceFunc serviceFunc,
        std::function<void(std::shared_ptr<YomkEvent> eventPtr)> eventHandleFinishedFunc)
        : m_pkg(pkg)
        , m_serviceFunc(serviceFunc){ 
            m_name = "YomkEvent";
            m_eventLoopName = eventLoopName; 
            m_eventHandleFinishedFunc = eventHandleFinishedFunc; 
            m_eventId = 0;
            m_eventHandleFinished = false;
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
    virtual void handleFinished(std::shared_ptr<YomkEvent> eventPtr)
    {
        if(m_eventHandleFinishedFunc)
        {
            m_eventHandleFinishedFunc(eventPtr);
        }
    }
public:
    std::uint64_t m_eventId;
    std::string m_eventLoopName;
    bool m_eventHandleFinished;
    std::function<void(std::shared_ptr<YomkEvent> eventPtr)> m_eventHandleFinishedFunc;
    YomkPkgPtr m_pkg;
    YomkResponse m_response;
    YomkServiceFunc m_serviceFunc;
};
typedef std::shared_ptr<YomkEvent> YomkEventPtr;
#define YomkMkYomkEventPtr(eventLoopName, pkg, serviceFunc, eventHandleFinished) std::make_shared<YomkEvent>(eventLoopName, pkg, serviceFunc, eventHandleFinished)


class YFunction : public YomkPkg
{
public:
    YFunction() { m_name = "YFunction"; }
    YFunction(const std::string& funcName, YomkServiceFunc func)
        : m_funcName(funcName)
        , m_func(func)
    {
        m_name = "YFunction";
    }
    virtual ~YFunction() {}
public:
    std::string m_funcName;
    YomkServiceFunc m_func;
};
typedef std::shared_ptr<YFunction> YFunctionPtr;
#define YomkMkYFunctionPtr(funcName, func) std::make_shared<YFunction>(funcName, func)

class YCallFunction : public YomkPkg
{
public:
    YCallFunction() { m_name = "YCallFunction"; }
    YCallFunction(const std::string& funcName, YomkPkgPtr pkg)
        : m_funcName(funcName)
        , m_pkg(pkg)
    {
        m_name = "YCallFunction";
    }
    virtual ~YCallFunction() {}
public:
    std::string m_funcName;
    YomkPkgPtr m_pkg;
};
typedef std::shared_ptr<YCallFunction> YCallFunctionPtr;
#define YomkMkYCallFunctionPtr(funcName, pkg) std::make_shared<YCallFunction>(funcName, pkg)

class YContext : public YomkPkg
{
public:
    YContext() { m_name = "YContext"; }
    YContext(const std::string& key, YomkPkgPtr value)
        : m_key(key)
        , m_value(value) { m_name = "YContext"; }
    virtual ~YContext() {}
public:
    std::string m_key;
    YomkPkgPtr m_value;
};
typedef std::shared_ptr<YContext> YContextPtr;
#define YomkMkYContextPtr(key, value) std::make_shared<YContext>(key, value)
typedef std::function<void (YContextPtr ctx)> YomkContextMonitorFunc;

class YContextMonitor : public YomkPkg
{
public:
    YContextMonitor() { m_name = "YContextMonitor"; }
    YContextMonitor(const std::string& key, YomkContextMonitorFunc contextMonitorFunc)
        : m_key(key)
        , m_contextMonitorFunc(contextMonitorFunc) { m_name = "YContextMonitor"; }
    virtual ~YContextMonitor() {}
public:
    std::string m_key;
    YomkContextMonitorFunc m_contextMonitorFunc;
};
typedef std::shared_ptr<YContextMonitor> YContextMonitorPtr;
#define YomkMkYContextMonitorPtr(key, contextMonitorFunc) std::make_shared<YContextMonitor>(key, contextMonitorFunc)

class YContextSetChecker : public YomkPkg
{
public:
    enum ECheckStatus
    {
        eAccept,
        eReject
    };
    typedef std::function<ECheckStatus (YomkPkgPtr pkg)> YomkContextCheckFunc;
public:
    YContextSetChecker() { m_name = "YContextSetChecker"; }
    YContextSetChecker(
        const std::string& key, 
        YomkContextCheckFunc checkFunc)
        : m_key(key)
        , m_checkFunc(checkFunc) { m_name = "YContextSetChecker"; }
    virtual ~YContextSetChecker() {}
public:
    std::string m_key;
    YomkContextCheckFunc m_checkFunc; 
};
typedef std::shared_ptr<YContextSetChecker> YContextSetCheckerPtr;
#define YomkMkYContextSetCheckerPtr(key, checkFunc) std::make_shared<YContextSetChecker>(key, checkFunc)

class YSetting : public YomkPkg
{
public:
    YSetting() { m_name = "YSetting"; }
    YSetting(
        const std::string& key) 
        : m_key(key){ m_name = "YSetting"; }
    virtual ~YSetting() {}
public:
    std::string m_key;
};
typedef std::shared_ptr<YSetting> YSettingPtr;

class YSettingString : public YSetting
{
public:
    YSettingString() { m_name = "YSettingString"; }
    YSettingString(
        const std::string& key, 
        const std::string& value) 
        : YSetting(key) 
        , d(value) { m_name = "YSettingString"; }
    virtual ~YSettingString() {}
public:
    std::string d;
};
typedef std::shared_ptr<YSettingString> YSettingStringPtr;
#define YomkMkYSettingStringPtr(key, value) std::make_shared<YSettingString>(key, value)

class YSettingDouble : public YSetting
{
public:
    YSettingDouble() { m_name = "YSettingDouble"; }
    YSettingDouble(
        const std::string& key, 
        const double& value) 
        : YSetting(key) 
        , d(value) { m_name = "YSettingDouble"; }
    virtual ~YSettingDouble() {}
public:
    double d;
};
typedef std::shared_ptr<YSettingDouble> YSettingDoublePtr;
#define YomkMkYSettingDoublePtr(key, value) std::make_shared<YSettingDouble>(key, value)

class YSettingInt : public YSetting
{
public:
    YSettingInt() { m_name = "YSettingInt"; }
    YSettingInt(
        const std::string& key, 
        const int& value) 
        : YSetting(key) 
        , d(value) { m_name = "YSettingInt"; }
    virtual ~YSettingInt() {}
public:
    int64_t d;
};
typedef std::shared_ptr<YSettingInt> YSettingIntPtr;
#define YomkMkYSettingIntPtr(key, value) std::make_shared<YSettingInt>(key, value)

class YSettingBool : public YSetting
{
public:
    YSettingBool() { m_name = "YSettingBool"; }
    YSettingBool(
        const std::string& key, 
        const bool& value) 
        : YSetting(key) 
        , d(value) { m_name = "YSettingBool"; }
    virtual ~YSettingBool() {}
public:
    bool d;
};
typedef std::shared_ptr<YSettingBool> YSettingBoolPtr;
#define YomkMkYSettingBoolPtr(key, value) std::make_shared<YSettingBool>(key, value)

class YSettingBoolArray : public YSetting
{
public:
    YSettingBoolArray() { m_name = "YSettingBoolArray"; }
    YSettingBoolArray(
        const std::string& key, 
        const std::vector<bool>& value) 
        : YSetting(key) 
        , d(value) { m_name = "YSettingBoolArray"; }
    virtual ~YSettingBoolArray() {}
public:
    std::vector<bool> d;
};
typedef std::shared_ptr<YSettingBoolArray> YSettingBoolArrayPtr;
#define YomkMkYSettingBoolArrayPtr(key, ...) std::make_shared<YSettingBoolArray>(key, std::vector<bool>({__VA_ARGS__}))

class YSettingIntArray : public YSetting
{
public:
    YSettingIntArray() { m_name = "YSettingIntArray"; }
    YSettingIntArray(
        const std::string& key, 
        const std::vector<int>& value) 
        : YSetting(key) 
        , d(value) { m_name = "YSettingIntArray"; }
    virtual ~YSettingIntArray() {}
public:
    std::vector<int> d;
};
typedef std::shared_ptr<YSettingIntArray> YSettingIntArrayPtr;
#define YomkMkYSettingIntArrayPtr(key, ...) std::make_shared<YSettingIntArray>(key, std::vector<int>({__VA_ARGS__}))

class YSettingDoubleArray : public YSetting
{
public:
    YSettingDoubleArray() { m_name = "YSettingDoubleArray"; }
    YSettingDoubleArray(
        const std::string& key, 
        const std::vector<double>& value) 
        : YSetting(key) 
        , d(value) { m_name = "YSettingDoubleArray"; }
    virtual ~YSettingDoubleArray() {}
public:
    std::vector<double> d;
};
typedef std::shared_ptr<YSettingDoubleArray> YSettingDoubleArrayPtr;
#define YomkMkYSettingDoubleArrayPtr(key, ...) std::make_shared<YSettingDoubleArray>(key, std::vector<double>({__VA_ARGS__}))

class YSettingStringArray : public YSetting
{
public:
    YSettingStringArray() { m_name = "YSettingStringArray"; }
    YSettingStringArray(
        const std::string& key, 
        const std::vector<std::string>& value) 
        : YSetting(key) 
        , d(value) { m_name = "YSettingStringArray"; }
    virtual ~YSettingStringArray() {}
public:
    std::vector<std::string> d;
};
typedef std::shared_ptr<YSettingStringArray> YSettingStringArrayPtr;
#define YomkMkYSettingStringArrayPtr(key, ...) std::make_shared<YSettingStringArray>(key, std::vector<std::string>({__VA_ARGS__}))

class YLog : public YomkPkg
{
public:
    enum ELogLevel
    {
        eDebug,
        eInfo,
        eWarn,
        eError,
    };
public:
    YLog() { m_name = "YLog"; }
    YLog(
        ELogLevel level, 
        const std::string& log = "",
        const std::string& logger = "MainLogger") 
        : m_logger(logger)
        , m_level(level)
        , m_log(log) { m_name = "YLog"; }
public:
    std::string m_logger;
    ELogLevel m_level;
    std::string m_log;
};
typedef std::shared_ptr<YLog> YLogPtr;
#define YomkMkYLogPtr(...) std::make_shared<YLog>(__VA_ARGS__)

class YLogFile : public YomkPkg
{
public:
    YLogFile() { m_name = "YLogFile"; }
    YLogFile(
        const std::string& dir,
        const std::string& logger = "") 
        : m_logger(logger)
        , m_dir(dir) { m_name = "YLogFile"; }
public:
    std::string m_logger;
    std::string m_dir;
};
typedef std::shared_ptr<YLogFile> YLogFilePtr;
#define YomkMkYLogFilePtr(dir, ...) std::make_shared<YLogFile>(dir, __VA_ARGS__)

class YString : public YomkPkg
{
public:
    YString() { m_name = "YString"; }
    YString(const std::string& d) : d(d) { m_name = "YString"; }
    virtual ~YString() {}
public:
    std::string d;
};
typedef std::shared_ptr<YString> YStringPtr;
#define YomkMkYStringPtr(d) std::make_shared<YString>(d)

class YBool : public YomkPkg
{
public:
    YBool() { m_name = "YBool"; }
    YBool(bool b) : b(b) { m_name = "YBool"; }
    virtual ~YBool() {}
public:
    bool b;
};
typedef std::shared_ptr<YBool> YBoolPtr;
#define YomkMkYBoolPtr(b) std::make_shared<YBool>(b)

class YEventloop : public YomkPkg
{
public:
    YEventloop() { m_name = "YEventloop"; }
    YEventloop(
        const std::string& eventloopName,
        YomkServiceFunc serviceFunc = nullptr,
        std::function<void(std::shared_ptr<YomkEvent> eventPtr)> eventHandleFinishedFunc = nullptr
    ) : m_eventloopName(eventloopName)
        , m_defaultEventHandleFinishedFunc(eventHandleFinishedFunc)
        , m_defaultServiceFunc(serviceFunc) { m_name = "YEventloop"; }
    virtual ~YEventloop() {}
public:
    std::string m_eventloopName;
    std::function<void(std::shared_ptr<YomkEvent> eventPtr)> m_defaultEventHandleFinishedFunc;
    YomkServiceFunc m_defaultServiceFunc;
};
typedef std::shared_ptr<YEventloop> YEventloopPtr;
#define YomkMkYEventloopPtr(eventloopName, ...) \
    std::make_shared<YEventloop>(eventloopName, __VA_ARGS__)


namespace yomk
{

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

}

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

#define YomkPtr(Type) yomk::Type##Ptr
#define YomkMsgPtr(Type, ...) std::make_shared<yomk::Type##_>(__VA_ARGS__)


YomkMsg(std::string, string)
