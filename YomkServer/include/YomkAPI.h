#pragma once
#include "YomkServer.h"
#include "YomkDefine.h"

class YomkAPI
{
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
