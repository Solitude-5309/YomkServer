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
    static void asyncRequest(const std::string& path, YomkPkgPtr pkg, YomkResponseFunc func){
        if(!m_pServer){
            if(func) func(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
            return;
        }
        return m_pServer->asyncRequest(path, pkg, func);
    }
    static YomkResponse request(const std::string& path, YomkPkgPtr pkg){
        if(!m_pServer){
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return m_pServer->request(path, pkg);
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
private:
    static std::shared_ptr<YomkServer> m_pServer;
};

std::shared_ptr<YomkServer> YomkAPI::m_pServer = nullptr;

#define YOMK_INIT(...) YomkAPI::init(__VA_ARGS__)
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
