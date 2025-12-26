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
    ~YomkPkg() {}
public:
    void name(const std::string& name) { m_name = name; }
    std::string name() { return m_name; }
public:
    virtual std::shared_ptr<YomkPkg> clone() const = 0;
protected:
    std::string m_name;
};
typedef std::shared_ptr<YomkPkg> YomkPkgPtr;

class YomkEvent : public YomkPkg
{
public:
    YomkEvent() { m_name = "YomkEvent"; m_eventHandleFinished = false; m_eventHandleFinishedFunc = nullptr; }
    ~YomkEvent() {}
public:
    virtual void handle() = 0;
    virtual void handleFinished(std::shared_ptr<YomkEvent> eventPtr) = 0;
public:
    std::uint64_t m_eventId;
    std::string m_eventLoopName;
    bool m_eventHandleFinished;
    std::function<void(std::shared_ptr<YomkEvent> eventPtr)> m_eventHandleFinishedFunc;
};
typedef std::shared_ptr<YomkEvent> YomkEventPtr;

class YomkRespond : public YomkPkg
{
public:
    enum EResStatus{
        eInvalid = -1,
        eOk = 0,
        eErr = 1,
    };
public:
    YomkRespond() 
        : m_resStatus(eInvalid)
        , m_data(nullptr) {  
            m_name = "YomkRespond"; }
    YomkRespond(
        EResStatus status, 
        const std::string& msg="",
        std::shared_ptr<YomkPkg> d = nullptr ) 
        : m_resStatus(status)
        , m_msg(msg)
        , m_data(d) { 
            m_name = "YomkRespond"; }
    ~YomkRespond() {}
public:
    virtual std::shared_ptr<YomkPkg> clone() const
    {
        YomkRespond* nd = new YomkRespond();
        nd->m_name = m_name;
        nd->m_resStatus = m_resStatus;
        nd->m_data = m_data->clone();
        nd->m_msg = m_msg;
        std::shared_ptr<YomkPkg> ptr;
        ptr.reset(nd);
        return ptr;
    }
public:
    EResStatus m_resStatus;
    std::string m_msg;
    std::shared_ptr<YomkPkg> m_data;
};
typedef std::shared_ptr<YomkRespond> YomkRespondPtr;

typedef std::function<YomkRespond (YomkPkgPtr pkg)> YomkServiceFunc;
typedef std::function<void (YomkRespond respond)> YomkRespondFunc;
typedef std::function<YomkRespond (YomkServer* server, YomkPkgPtr pkg)> YomkFunction;

class YResquestEvent : public YomkEvent
{
public:
    YResquestEvent() { m_name = "YResquestEvent"; }
    YResquestEvent(
        const std::string& eventLoopName,
        YomkPkgPtr pkg, 
        YomkServiceFunc serviceFunc,
        std::function<void(std::shared_ptr<YomkEvent> eventPtr)> eventHandleFinishedFunc)
        : m_pkg(pkg)
        , m_serviceFunc(serviceFunc){ 
            m_name = "YResquestEvent";
            m_eventLoopName = eventLoopName; 
            m_eventHandleFinishedFunc = eventHandleFinishedFunc; }
    ~YResquestEvent() {}
public:
    virtual std::shared_ptr<YomkPkg> clone() const
    {
        YResquestEvent* nd = new YResquestEvent();
        std::shared_ptr<YomkPkg> ptr;
        nd->m_name = m_name;
        nd->m_eventId = m_eventId;
        nd->m_eventLoopName = m_eventLoopName;
        nd->m_eventHandleFinished = m_eventHandleFinished;
        nd->m_pkg = m_pkg->clone();
        nd->m_respond.m_resStatus = m_respond.m_resStatus;
        nd->m_respond.m_msg = m_respond.m_msg;
        nd->m_respond.m_data = m_respond.m_data->clone();
        nd->m_serviceFunc = m_serviceFunc;
        ptr.reset(nd);
        return ptr;
    }
    virtual void handle()
    {
        if(m_serviceFunc)
        {
            m_respond = m_serviceFunc(m_pkg);
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
    YomkPkgPtr m_pkg;
    YomkRespond m_respond;
    YomkServiceFunc m_serviceFunc;
};
typedef std::shared_ptr<YResquestEvent> YResquestEventPtr;
#define YomkMkYResquestEventPtr(eventLoopName, pkg, serviceFunc, eventHandleFinished) std::make_shared<YResquestEvent>(eventLoopName, pkg, serviceFunc, eventHandleFinished)

class YFunction : public YomkPkg
{
public:
    YFunction() { m_name = "YFunction"; }
    YFunction(const std::string& funcName, YomkFunction func)
        : m_funcName(funcName)
        , m_func(func)
    {
        m_name = "YFunction";
    }
    ~YFunction() {}
public:
    virtual std::shared_ptr<YomkPkg> clone() const
    {
        YFunction* nd = new YFunction();
        nd->m_name = m_name;
        nd->m_funcName = m_funcName;
        nd->m_func = m_func;
        std::shared_ptr<YomkPkg> ptr;
        ptr.reset(nd);
        return ptr;
    }
public:
    std::string m_funcName;
    YomkFunction m_func;
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
    ~YCallFunction() {}
public:
    virtual std::shared_ptr<YomkPkg> clone() const
    {
        YCallFunction* nd = new YCallFunction();
        nd->m_name = m_name;
        nd->m_funcName = m_funcName;
        nd->m_pkg = m_pkg;
        std::shared_ptr<YomkPkg> ptr;
        ptr.reset(nd);
        return ptr;
    }
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
    ~YContext() {}
public:
    virtual std::shared_ptr<YomkPkg> clone() const
    {
        YContext* nd = new YContext();
        nd->m_name = m_name;
        nd->m_key = m_key;
        nd->m_value = m_value->clone();
        std::shared_ptr<YomkPkg> ptr;
        ptr.reset(nd);
        return ptr;
    }
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
    ~YContextMonitor() {}
public:
    virtual std::shared_ptr<YomkPkg> clone() const
    {
        YContextMonitor* nd = new YContextMonitor();
        nd->m_name = m_name;
        nd->m_key = m_key;
        nd->m_contextMonitorFunc = m_contextMonitorFunc;
        std::shared_ptr<YomkPkg> ptr;
        ptr.reset(nd);
        return ptr;
    }
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
    ~YContextSetChecker() {}
public:
    virtual std::shared_ptr<YomkPkg> clone() const
    {
        YContextSetChecker* nd = new YContextSetChecker();
        nd->m_name = m_name;
        nd->m_key = m_key;
        nd->m_checkFunc = m_checkFunc;
        std::shared_ptr<YomkPkg> ptr;
        ptr.reset(nd);
        return ptr;
    }
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
    ~YSetting() {}
public:
    virtual std::shared_ptr<YomkPkg> clone() const = 0;
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
    ~YSettingString() {}
public:
    virtual std::shared_ptr<YomkPkg> clone() const
    {
        YSettingString* nd = new YSettingString();
        nd->m_name = m_name;
        nd->m_key = m_key;
        nd->d = d;
        std::shared_ptr<YomkPkg> ptr;
        ptr.reset(nd);
        return ptr;
    }
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
    ~YSettingDouble() {}
public:
    virtual std::shared_ptr<YomkPkg> clone() const
    {
        YSettingDouble* nd = new YSettingDouble();
        nd->m_name = m_name;
        nd->m_key = m_key;
        nd->d = d;
        std::shared_ptr<YomkPkg> ptr;
        ptr.reset(nd);
        return ptr;
    }
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
    ~YSettingInt() {}
public:
    virtual std::shared_ptr<YomkPkg> clone() const
    {
        YSettingInt* nd = new YSettingInt();
        nd->m_name = m_name;
        nd->m_key = m_key;
        nd->d = d;
        std::shared_ptr<YomkPkg> ptr;
        ptr.reset(nd);
        return ptr;
    }
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
    ~YSettingBool() {}
public:
    virtual std::shared_ptr<YomkPkg> clone() const
    {
        YSettingBool* nd = new YSettingBool();
        nd->m_name = m_name;
        nd->m_key = m_key;
        nd->d = d;
        std::shared_ptr<YomkPkg> ptr;
        ptr.reset(nd);
        return ptr;
    }
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
    ~YSettingBoolArray() {}
public:
    virtual std::shared_ptr<YomkPkg> clone() const
    {
        YSettingBoolArray* nd = new YSettingBoolArray();
        nd->m_name = m_name;
        nd->m_key = m_key;
        nd->d = d;
        std::shared_ptr<YomkPkg> ptr;
        ptr.reset(nd);
        return ptr;
    }
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
    ~YSettingIntArray() {}
public:
    virtual std::shared_ptr<YomkPkg> clone() const
    {
        YSettingIntArray* nd = new YSettingIntArray();
        nd->m_name = m_name;
        nd->m_key = m_key;
        nd->d = d;
        std::shared_ptr<YomkPkg> ptr;
        ptr.reset(nd);
        return ptr;
    }
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
    ~YSettingDoubleArray() {}
public:
    virtual std::shared_ptr<YomkPkg> clone() const
    {
        YSettingDoubleArray* nd = new YSettingDoubleArray();
        nd->m_name = m_name;
        nd->m_key = m_key;
        nd->d = d;
        std::shared_ptr<YomkPkg> ptr;
        ptr.reset(nd);
        return ptr;
    }
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
    ~YSettingStringArray() {}
public:
    virtual std::shared_ptr<YomkPkg> clone() const
    {
        YSettingStringArray* nd = new YSettingStringArray();
        nd->m_name = m_name;
        nd->m_key = m_key;
        nd->d = d;
        std::shared_ptr<YomkPkg> ptr;
        ptr.reset(nd);
        return ptr;
    }
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
    YLog() { m_name = "YConsoleLog"; }
    YLog(
        ELogLevel level, 
        const std::string& log,
        const std::string& logger = "") 
        : m_logger(logger)
        , m_level(level)
        , m_log(log) { m_name = "YLog"; }
public:
    virtual std::shared_ptr<YomkPkg> clone() const
    {
        YLog* nd = new YLog();
        nd->m_name = m_name;
        nd->m_log = m_log;
        nd->m_level = m_level;
        nd->m_logger = m_logger;
        std::shared_ptr<YomkPkg> ptr;
        ptr.reset(nd);
        return ptr;
    }
public:
    std::string m_logger;
    ELogLevel m_level;
    std::string m_log;
};
typedef std::shared_ptr<YLog> YLogPtr;
#define YomkMkYLogPtr(level, log, ...) std::make_shared<YLog>(level, log, __VA_ARGS__)

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
    virtual std::shared_ptr<YomkPkg> clone() const
    {
        YLogFile* nd = new YLogFile();
        nd->m_name = m_name;
        nd->m_dir = m_dir;
        nd->m_logger = m_logger;
        std::shared_ptr<YomkPkg> ptr;
        ptr.reset(nd);
        return ptr;
    }
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
    ~YString() {}
public:
    virtual std::shared_ptr<YomkPkg> clone() const
    {
        YString* nd = new YString();
        nd->m_name = m_name;
        nd->d = d;
        std::shared_ptr<YomkPkg> ptr;
        ptr.reset(nd);
        return ptr;
    }
public:
    std::string d;
};
typedef std::shared_ptr<YString> YStringPtr;
#define YomkMkYStringPtr(d) std::make_shared<YString>(d)
