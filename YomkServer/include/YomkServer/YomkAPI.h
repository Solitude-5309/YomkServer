#pragma once
#include "YomkServer.h"
#include "YomkDefine.h"

class YomkAPI
{
// REQ_API
public:
    static void init(std::shared_ptr<YomkServer> pServer){
        m_pServer = pServer;
    }
    static void asyncRequest(const std::string& url, YomkPkgPtr pkg, YomkResponseFunc func){
        if(!m_pServer){
            if(func) func(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
            return;
        }
        return m_pServer->asyncRequest(url, pkg, func);
    }
    static YomkResponse request(const std::string& url, YomkPkgPtr pkg){
        if(!m_pServer){
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return m_pServer->request(url, pkg);
    }
// LOG_API
public:
    static YomkResponse CONSOLE_LOG_INFO(const std::string& log, const std::string& tag="MainLogger"){
        if(!m_pServer){
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        request("/YomkLogger/create_console_logger", YomkMkYStringPtr(tag));
        return request("/YomkLogger/console_log", YomkMkYLogPtr(YLog::eInfo, log, tag));
    }
    static YomkResponse CONSOLE_LOG_WARN(const std::string& log, const std::string& tag="MainLogger"){
        if(!m_pServer){
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        request("/YomkLogger/create_console_logger", YomkMkYStringPtr(tag));
        return request("/YomkLogger/console_log", YomkMkYLogPtr(YLog::eWarn, log, tag));
    }
    static YomkResponse CONSOLE_LOG_ERROR(const std::string& log, const std::string& tag="MainLogger"){
        if(!m_pServer){
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        request("/YomkLogger/create_console_logger", YomkMkYStringPtr(tag));
        return request("/YomkLogger/console_log", YomkMkYLogPtr(YLog::eError, log, tag));
    }
    static YomkResponse CONSOLE_LOG_DEBUG(const std::string& log, const std::string& tag="MainLogger"){
        if(!m_pServer){
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        request("/YomkLogger/create_console_logger", YomkMkYStringPtr(tag));
        return request("/YomkLogger/console_log", YomkMkYLogPtr(YLog::eDebug, log, tag));
    }
    static YomkResponse FILE_LOG_CREATE(const std::string& logDir, const std::string& logFile){
        if(!m_pServer){
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkLogger/create_file_logger", YomkMkYLogFilePtr(logDir, logFile));
    }
    static YomkResponse FILE_LOG_WRITE(const std::string& logFile){
        if(!m_pServer){
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkLogger/write_file_log", YomkMkYStringPtr(logFile));
    }
    static YomkResponse FILE_LOG_INFO(const std::string& logFile, const std::string& log, const std::string& tag="MainLogger"){
        if(!m_pServer){
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkLogger/file_log", YomkMkYLogPtr(YLog::eInfo, "[" + tag + "] " + log, logFile));
    }
    static YomkResponse FILE_LOG_WARN(const std::string& logFile, const std::string& log, const std::string& tag="MainLogger"){
        if(!m_pServer){
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkLogger/file_log", YomkMkYLogPtr(YLog::eWarn, "[" + tag + "] " + log, logFile));
    }
    static YomkResponse FILE_LOG_ERROR(const std::string& logFile, const std::string& log, const std::string& tag="MainLogger"){
        if(!m_pServer){
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkLogger/file_log", YomkMkYLogPtr(YLog::eError, "[" + tag + "] " + log, logFile));
    }
    static YomkResponse FILE_LOG_DEBUG(const std::string& logFile, const std::string& log, const std::string& tag="MainLogger"){
        if(!m_pServer){
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkLogger/file_log", YomkMkYLogPtr(YLog::eDebug, "[" + tag + "] " + log, logFile));
    }
// SETTINGS_API
public:
    static YomkResponse SETTINGS_LOAD(const std::string& settingsPath){
        if(!m_pServer){
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkSettings/load", YomkMkYStringPtr(settingsPath));
    }
    static YomkResponse SETTINGS_SAVE(const std::string& settingsPath){
        if(!m_pServer){
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkSettings/save", YomkMkYStringPtr(settingsPath));
    }
    static bool SETTINGS_GET_BOOL(const std::string& key){
        if(!m_pServer){
            return false;
        }
        YomkResponse response = request("/YomkSettings/get", YomkMkYStringPtr(key));
        if(response.m_resStatus == YomkResponse::eOk)
        {
            YomkUnPackPkg(response.m_data, "YSettingBool", YSettingBool, yBool);
            return yBool->d;
        }
        return false;
    }
    static std::vector<bool> SETTINGS_GET_BOOL_ARRAY(const std::string& key){
        if(!m_pServer){
            return std::vector<bool>();
        }
        YomkResponse response = request("/YomkSettings/get", YomkMkYStringPtr(key));
        if(response.m_resStatus == YomkResponse::eOk)
        {
            YomkUnPackPkg(response.m_data, "YSettingBoolArray", YSettingBoolArray, yBoolArray);
            return yBoolArray->d;
        }
        return std::vector<bool>();
    }
    static double SETTINGS_GET_DOUBLE(const std::string& key){
        if(!m_pServer){
            return 0.0;
        }
        YomkResponse response = request("/YomkSettings/get", YomkMkYStringPtr(key));
        if(response.m_resStatus == YomkResponse::eOk)
        {
            YomkUnPackPkg(response.m_data, "YSettingDouble", YSettingDouble, yDouble);
            return yDouble->d;
        }
        return 0.0;
    }
    static std::vector<double> SETTINGS_GET_DOUBLE_ARRAY(const std::string& key){
        if(!m_pServer){
            return std::vector<double>();
        }
        YomkResponse response = request("/YomkSettings/get", YomkMkYStringPtr(key));
        if(response.m_resStatus == YomkResponse::eOk)
        {
            YomkUnPackPkg(response.m_data, "YSettingDoubleArray", YSettingDoubleArray, yDoubleArray);
            return yDoubleArray->d;
        }
        return std::vector<double>();
    }
    static int64_t SETTINGS_GET_INT(const std::string& key){
        if(!m_pServer){
            return 0;
        }
        YomkResponse response = request("/YomkSettings/get", YomkMkYStringPtr(key));
        if(response.m_resStatus == YomkResponse::eOk)
        {
            YomkUnPackPkg(response.m_data, "YSettingInt", YSettingInt, yInt);
            return yInt->d;
        }
        return 0;
    }
    static std::vector<int> SETTINGS_GET_INT_ARRAY(const std::string& key){
        if(!m_pServer){
            return std::vector<int>();
        }
        YomkResponse response = request("/YomkSettings/get", YomkMkYStringPtr(key));
        if(response.m_resStatus == YomkResponse::eOk)
        {
            YomkUnPackPkg(response.m_data, "YSettingIntArray", YSettingIntArray, yIntArray);
            return yIntArray->d;
        }
        return std::vector<int>();
    }
    static std::string SETTINGS_GET_STRING(const std::string& key){
        if(!m_pServer){
            return std::string();
        }
        YomkResponse response = request("/YomkSettings/get", YomkMkYStringPtr(key));
        if(response.m_resStatus == YomkResponse::eOk)
        {
            YomkUnPackPkg(response.m_data, "YSettingString", YSettingString, yStr);
            return yStr->d;
        }
        return std::string();
    }
    static std::vector<std::string> SETTINGS_GET_STRING_ARRAY(const std::string& key){
        if(!m_pServer){
            return std::vector<std::string>();
        }
        YomkResponse response = request("/YomkSettings/get", YomkMkYStringPtr(key));
        if(response.m_resStatus == YomkResponse::eOk)
        {
            YomkUnPackPkg(response.m_data, "YSettingStringArray", YSettingStringArray, yStrArray);
            return yStrArray->d;
        }
        return std::vector<std::string>();
    }
    static YomkResponse SETTINGS_SET_BOOL(const std::string& key, bool value){
        if(!m_pServer){
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkSettings/set", YomkMkYSettingBoolPtr(key, value));
    }
    static YomkResponse SETTINGS_SET_BOOL_ARRAY(const std::string& key, const std::vector<bool>& value){
        if(!m_pServer){
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkSettings/set", YomkMkYSettingBoolArrayPtr(key, value));
    }
    static YomkResponse SETTINGS_SET_DOUBLE(const std::string& key, double value){
        if(!m_pServer){
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkSettings/set", YomkMkYSettingDoublePtr(key, value));
    }
    static YomkResponse SETTINGS_SET_DOUBLE_ARRAY(const std::string& key, const std::vector<double>& value){
        if(!m_pServer){
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkSettings/set", YomkMkYSettingDoubleArrayPtr(key, value));
    }
    static YomkResponse SETTINGS_SET_INT(const std::string& key, int value){
        if(!m_pServer){
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkSettings/set", YomkMkYSettingIntPtr(key, value));
    }
    static YomkResponse SETTINGS_SET_INT_ARRAY(const std::string& key, const std::vector<int>& value){
        if(!m_pServer){
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkSettings/set", YomkMkYSettingIntArrayPtr(key, value));
    }
    static YomkResponse SETTINGS_SET_STRING(const std::string& key, const std::string& value){
        if(!m_pServer){
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkSettings/set", YomkMkYSettingStringPtr(key, value));
    }
    static YomkResponse SETTINGS_SET_STRING_ARRAY(const std::string& key, const std::vector<std::string>& value){
        if(!m_pServer){
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkSettings/set", YomkMkYSettingStringArrayPtr(key, value));
    }
// CONTEXT_API
public:
    static YomkResponse CONTEXT_CREATE(const std::string& ctx_name, YomkPkgPtr ctx){
        if(!m_pServer){
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkContext/create", YomkMkYContextPtr(ctx_name, ctx));
    }
    template<typename T>
    static std::shared_ptr<T> CONTEXT_GET(const std::string& ctx_name, std::shared_ptr<T> ctx_default){
        if(!m_pServer){
            std::cout << "YomkServer is not init" << std::endl;
            return ctx_default;
        }
        YomkResponse response = request("/YomkContext/get", YomkMkYContextPtr(ctx_name, ctx_default));
        if(response.m_resStatus == YomkResponse::eOk)
        {
            YomkUnPackPkg(response.m_data, ctx_default->name(), T, ctx_data);
            return ctx_data;
        }
        else
        {
            std::cout << "get context failed" << std::endl;
            return ctx_default;
        }
    }
    static YomkResponse CONTEXT_SET(const std::string& ctx_name, YomkPkgPtr ctx){
        if(!m_pServer){
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkContext/set", YomkMkYContextPtr(ctx_name, ctx));
    }
    static YomkResponse CONTEXT_ON_CHECHER(){
        if(!m_pServer){
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkContext/turn_on_checker", nullptr);
    }
    static YomkResponse CONTEXT_OFF_CHECHER(){
        if(!m_pServer){
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkContext/turn_off_checker", nullptr);
    }
    static YomkResponse CONTEXT_SET_CHECKER(const std::string& ctx_name, YContextSetChecker::YomkContextCheckFunc checker){
        if(!m_pServer){
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkContext/set_checker", YomkMkYContextSetCheckerPtr(ctx_name, checker));
    }
    static YomkResponse CONTEXT_ON_MONITOR(){
        if(!m_pServer){
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkContext/turn_on_monitor", nullptr);
    }
    static YomkResponse CONTEXT_OFF_MONITOR(){
        if(!m_pServer){
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkContext/turn_off_monitor", nullptr);
    }
    static YomkResponse CONTEXT_SET_MONITOR(const std::string& ctx_name, YomkContextMonitorFunc monitor){
        if(!m_pServer){
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkContext/set_monitor", YomkMkYContextMonitorPtr(ctx_name, monitor));
    }
    static YomkResponse CONTEXT_DESTROY(const std::string& ctx_name){
        if(!m_pServer){
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkContext/destroy", YomkMkYStringPtr(ctx_name));
    }
// EVENTLOOP_API
public:
    static YomkResponse EVENTLOOP_START(const std::string& event_loop_name){
        if(!m_pServer){
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkEventLoop/start", YomkMkYStringPtr(event_loop_name));
    }
    static YomkResponse EVENTLOOP_STOP(const std::string& event_loop_name){
        if(!m_pServer){
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkEventLoop/stop", YomkMkYStringPtr(event_loop_name));
    }
    static YomkResponse EVENTLOOP_POST(const std::string& event_loop_name, YomkPkgPtr event_data, YomkServiceFunc event_handle, std::function<void(std::shared_ptr<YomkEvent>)> event_handle_finished){
        if(!m_pServer){
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkEventLoop/post", YomkMkYResquestEventPtr(event_loop_name, event_data, event_handle, event_handle_finished));
    }
    static YomkResponse EVENTLOOP_POST_WAIT(const std::string& event_loop_name, YomkPkgPtr event_data, YomkServiceFunc event_handle, std::function<void(std::shared_ptr<YomkEvent>)> event_handle_finished){
        if(!m_pServer){
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkEventLoop/post_wait", YomkMkYResquestEventPtr(event_loop_name, event_data, event_handle, event_handle_finished));
    }
    static YomkResponse EVENTLOOP_DESTROY(const std::string& event_loop_name){
        if(!m_pServer){
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkEventLoop/destroy", YomkMkYStringPtr(event_loop_name));
    }
// FUNCTIONPOOL_API
public:
    static YomkResponse FUNCTIONPOOL_REGISTER(const std::string& func_name, YomkServiceFunc func){
        if(!m_pServer){
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkFunctionPool/register", YomkMkYFunctionPtr(func_name, func));
    }
    static YomkResponse FUNCTIONPOOL_CALL(const std::string& func_name, YomkPkgPtr call_data){
        if(!m_pServer){
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkFunctionPool/call", YomkMkYCallFunctionPtr(func_name, call_data));
    }
private:
    static std::shared_ptr<YomkServer> m_pServer;
};

std::shared_ptr<YomkServer> YomkAPI::m_pServer = nullptr;

#define YOMK_INIT(...) YomkAPI::init(__VA_ARGS__)
#define YOMK_REQUEST(...) YomkAPI::request(__VA_ARGS__)
#define YOMK_ASYNC_REQUEST(...) YomkAPI::asyncRequest(__VA_ARGS__)
#define YOMK_INFO(...) YomkAPI::CONSOLE_LOG_INFO(__VA_ARGS__)
#define YOMK_WARN(...) YomkAPI::CONSOLE_LOG_WARN(__VA_ARGS__)
#define YOMK_ERROR(...) YomkAPI::CONSOLE_LOG_ERROR(__VA_ARGS__)
#define YOMK_DEBUG(...) YomkAPI::CONSOLE_LOG_DEBUG(__VA_ARGS__)
#define YOMK_FILE_INFO(...) YomkAPI::FILE_LOG_INFO(__VA_ARGS__)
#define YOMK_FILE_WARN(...) YomkAPI::FILE_LOG_WARN(__VA_ARGS__)
#define YOMK_FILE_ERROR(...) YomkAPI::FILE_LOG_ERROR(__VA_ARGS__)
#define YOMK_FILE_DEBUG(...) YomkAPI::FILE_LOG_DEBUG(__VA_ARGS__)
#define YOMK_SETTINGS_LOAD(...) YomkAPI::SETTINGS_LOAD(__VA_ARGS__)
#define YOMK_SETTINGS_GET_BOOL(...) YomkAPI::SETTINGS_GET_BOOL(__VA_ARGS__)
#define YOMK_SETTINGS_GET_BOOL_ARRAY(...) YomkAPI::SETTINGS_GET_BOOL_ARRAY(__VA_ARGS__)
#define YOMK_SETTINGS_GET_DOUBLE(...) YomkAPI::SETTINGS_GET_DOUBLE(__VA_ARGS__)
#define YOMK_SETTINGS_GET_DOUBLE_ARRAY(...) YomkAPI::SETTINGS_GET_DOUBLE_ARRAY(__VA_ARGS__)
#define YOMK_SETTINGS_GET_INT(...) YomkAPI::SETTINGS_GET_INT(__VA_ARGS__)
#define YOMK_SETTINGS_GET_INT_ARRAY(...) YomkAPI::SETTINGS_GET_INT_ARRAY(__VA_ARGS__)
#define YOMK_SETTINGS_GET_STRING(...) YomkAPI::SETTINGS_GET_STRING(__VA_ARGS__)
#define YOMK_SETTINGS_GET_STRING_ARRAY(...) YomkAPI::SETTINGS_GET_STRING_ARRAY(__VA_ARGS__)
#define YOMK_SETTINGS_SET_BOOL(...) YomkAPI::SETTINGS_SET_BOOL(__VA_ARGS__)
#define YOMK_SETTINGS_SET_BOOL_ARRAY(...) YomkAPI::SETTINGS_SET_BOOL_ARRAY(__VA_ARGS__)
#define YOMK_SETTINGS_SET_DOUBLE(...) YomkAPI::SETTINGS_SET_DOUBLE(__VA_ARGS__)
#define YOMK_SETTINGS_SET_DOUBLE_ARRAY(...) YomkAPI::SETTINGS_SET_DOUBLE_ARRAY(__VA_ARGS__)
#define YOMK_SETTINGS_SET_INT(...) YomkAPI::SETTINGS_SET_INT(__VA_ARGS__)
#define YOMK_SETTINGS_SET_INT_ARRAY(...) YomkAPI::SETTINGS_SET_INT_ARRAY(__VA_ARGS__)
#define YOMK_SETTINGS_SET_STRING(...) YomkAPI::SETTINGS_SET_STRING(__VA_ARGS__)
#define YOMK_SETTINGS_SET_STRING_ARRAY(...) YomkAPI::SETTINGS_SET_STRING_ARRAY(__VA_ARGS__)
#define YOMK_SETTINGS_SAVE(...) YomkAPI::SETTINGS_SAVE(__VA_ARGS__)
#define YOMK_CONTEXT_CREATE(...) YomkAPI::CONTEXT_CREATE(__VA_ARGS__)
#define YOMK_CONTEXT_GET(ClassName, ...) YomkAPI::CONTEXT_GET<ClassName>(__VA_ARGS__)
#define YOMK_CONTEXT_SET(...) YomkAPI::CONTEXT_SET(__VA_ARGS__)
#define YOMK_CONTEXT_ON_CHECHER() YomkAPI::CONTEXT_ON_CHECHER()
#define YOMK_CONTEXT_OFF_CHECHER() YomkAPI::CONTEXT_OFF_CHECHER()
#define YOMK_CONTEXT_SET_CHECKER(...) YomkAPI::CONTEXT_SET_CHECKER(__VA_ARGS__)
#define YOMK_CONTEXT_ON_MONITOR() YomkAPI::CONTEXT_ON_MONITOR()
#define YOMK_CONTEXT_OFF_MONITOR() YomkAPI::CONTEXT_OFF_MONITOR()
#define YOMK_CONTEXT_SET_MONITOR(...) YomkAPI::CONTEXT_SET_MONITOR(__VA_ARGS__)
#define YOMK_CONTEXT_DESTROY(...) YomkAPI::CONTEXT_DESTROY(__VA_ARGS__)
#define YOMK_EVENTLOOP_START(...) YomkAPI::EVENTLOOP_START(__VA_ARGS__)
#define YOMK_EVENTLOOP_STOP(...) YomkAPI::EVENTLOOP_STOP(__VA_ARGS__)
#define YOMK_EVENTLOOP_POST(...) YomkAPI::EVENTLOOP_POST(__VA_ARGS__)
#define YOMK_EVENTLOOP_POST_WAIT(...) YomkAPI::EVENTLOOP_POST_WAIT(__VA_ARGS__)
#define YOMK_EVENTLOOP_DESTROY(...) YomkAPI::EVENTLOOP_DESTROY(__VA_ARGS__)
#define YOMK_FUNCTIONPOOL_REGISTER(...) YomkAPI::FUNCTIONPOOL_REGISTER(__VA_ARGS__)
#define YOMK_FUNCTIONPOOL_CALL(...) YomkAPI::FUNCTIONPOOL_CALL(__VA_ARGS__)
