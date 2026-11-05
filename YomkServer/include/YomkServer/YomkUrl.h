#pragma once
#include <string>

// 内置服务 URL 常量池：函数级 static 惰性构造、全程复用，消除每次调用的临时
// std::string 堆分配。访问器名 = 服务前缀 + 路由段驼峰化；server 层不持有引用，
// 静态生命周期无析构顺序风险。
// 独立成头：URL 是协议层常量而非 API 逻辑，外部可直接包含本头引用内置服务路由，
// 无需拉入整个 YomkApi.h
struct YomkUrl
{
    // /YomkLogger/*
    static const std::string& loggerSetConsoleProxy() { static const std::string u("/YomkLogger/set_console_log_proxy"); return u; }
    static const std::string& loggerSetFileLogProxy() { static const std::string u("/YomkLogger/set_file_log_proxy"); return u; }
    static const std::string& loggerConsoleLog() { static const std::string u("/YomkLogger/console_log"); return u; }
    static const std::string& loggerCreateFileLogger() { static const std::string u("/YomkLogger/create_file_logger"); return u; }
    static const std::string& loggerFileLog() { static const std::string u("/YomkLogger/file_log"); return u; }
    static const std::string& loggerWriteFileLog() { static const std::string u("/YomkLogger/write_file_log"); return u; }
    static const std::string& loggerDeleteLogger() { static const std::string u("/YomkLogger/delete_logger"); return u; }
    static const std::string& loggerOnConsoleLogByLevel() { static const std::string u("/YomkLogger/on_console_log_by_level"); return u; }
    static const std::string& loggerOffConsoleLogByLevel() { static const std::string u("/YomkLogger/off_console_log_by_level"); return u; }
    static const std::string& loggerOnFileLogByLevel() { static const std::string u("/YomkLogger/on_file_log_by_level"); return u; }
    static const std::string& loggerOffFileLogByLevel() { static const std::string u("/YomkLogger/off_file_log_by_level"); return u; }
    static const std::string& loggerLoggers() { static const std::string u("/YomkLogger/loggers"); return u; }
    static const std::string& loggerLogger() { static const std::string u("/YomkLogger/logger"); return u; }
    static const std::string& loggerAll() { static const std::string u("/YomkLogger/all"); return u; }
    static const std::string& loggerFileGlobalState() { static const std::string u("/YomkLogger/file_global_state"); return u; }
    static const std::string& loggerConsoleGlobalState() { static const std::string u("/YomkLogger/console_global_state"); return u; }
    // /YomkContext/*
    static const std::string& ctxCreate() { static const std::string u("/YomkContext/create"); return u; }
    static const std::string& ctxGet() { static const std::string u("/YomkContext/get"); return u; }
    static const std::string& ctxSet() { static const std::string u("/YomkContext/set"); return u; }
    static const std::string& ctxTurnOnChecker() { static const std::string u("/YomkContext/turn_on_checker"); return u; }
    static const std::string& ctxTurnOffChecker() { static const std::string u("/YomkContext/turn_off_checker"); return u; }
    static const std::string& ctxSetChecker() { static const std::string u("/YomkContext/set_checker"); return u; }
    static const std::string& ctxTurnOnMonitor() { static const std::string u("/YomkContext/turn_on_monitor"); return u; }
    static const std::string& ctxTurnOffMonitor() { static const std::string u("/YomkContext/turn_off_monitor"); return u; }
    static const std::string& ctxSetMonitor() { static const std::string u("/YomkContext/set_monitor"); return u; }
    static const std::string& ctxDestroy() { static const std::string u("/YomkContext/destroy"); return u; }
    static const std::string& ctxKeys() { static const std::string u("/YomkContext/keys"); return u; }
    static const std::string& ctxKey() { static const std::string u("/YomkContext/key"); return u; }
    static const std::string& ctxGroups() { static const std::string u("/YomkContext/groups"); return u; }
    static const std::string& ctxAll() { static const std::string u("/YomkContext/all"); return u; }
    // /YomkEventLoop/*
    static const std::string& loopStart() { static const std::string u("/YomkEventLoop/start"); return u; }
    static const std::string& loopStop() { static const std::string u("/YomkEventLoop/stop"); return u; }
    static const std::string& loopPost() { static const std::string u("/YomkEventLoop/post"); return u; }
    static const std::string& loopPostWait() { static const std::string u("/YomkEventLoop/post_wait"); return u; }
    static const std::string& loopDestroy() { static const std::string u("/YomkEventLoop/destroy"); return u; }
    static const std::string& loopLoops() { static const std::string u("/YomkEventLoop/loops"); return u; }
    static const std::string& loopLoop() { static const std::string u("/YomkEventLoop/loop"); return u; }
    static const std::string& loopAll() { static const std::string u("/YomkEventLoop/all"); return u; }
    // /YomkFunctionPool/*
    static const std::string& fpRegister() { static const std::string u("/YomkFunctionPool/register"); return u; }
    static const std::string& fpUnregister() { static const std::string u("/YomkFunctionPool/unregister"); return u; }
    static const std::string& fpCall() { static const std::string u("/YomkFunctionPool/call"); return u; }
    static const std::string& fpNames() { static const std::string u("/YomkFunctionPool/names"); return u; }
    static const std::string& fpName() { static const std::string u("/YomkFunctionPool/name"); return u; }
    static const std::string& fpAll() { static const std::string u("/YomkFunctionPool/all"); return u; }
    // /YomkServerInfo/*
    static const std::string& srvServices() { static const std::string u("/YomkServerInfo/services"); return u; }
    static const std::string& srvFunctions() { static const std::string u("/YomkServerInfo/functions"); return u; }
    static const std::string& srvFunction() { static const std::string u("/YomkServerInfo/function"); return u; }
    static const std::string& srvAll() { static const std::string u("/YomkServerInfo/all"); return u; }
};
