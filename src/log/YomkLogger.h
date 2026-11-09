#pragma once
#include <atomic>
#include <map>
#include <mutex>
#include <shared_mutex>

#include "ConsoleLogger.h"
#include "FileLogger.h"
#include "yomk/log/YomkLoggerPayload.h"
#include "yomk/server/YomkServer.h"
class YomkLogger : public YomkService
{
public:
    YomkLogger(YomkServer* server);
    virtual ~YomkLogger();

public:
    virtual int init() override;

private:
    yomk::payload::Response consoleLog(YomkMsgPtr pkg);
    yomk::payload::Response setConsoleLogProxy(YomkMsgPtr pkg);
    yomk::payload::Response setFileLogProxy(YomkMsgPtr pkg);
    yomk::payload::Response createFileLogger(YomkMsgPtr pkg);
    yomk::payload::Response fileLog(YomkMsgPtr pkg);
    yomk::payload::Response writeFileLog(YomkMsgPtr pkg);
    yomk::payload::Response deleteLogger(YomkMsgPtr pkg);
    yomk::payload::Response offConsoleLogByLevel(YomkMsgPtr pkg);
    yomk::payload::Response onConsoleLogByLevel(YomkMsgPtr pkg);
    // 具名开关唯一写入口:按名查表逐器门控(未命中 ResStatus::Failed 不惰性创建,不触碰全局主闸)
    yomk::payload::Response setNamedConsoleLevel(const std::string& loggerName, yomk::payload::LogLevel level, bool on);

    yomk::payload::Response offFileLogByLevel(YomkMsgPtr pkg);
    yomk::payload::Response onFileLogByLevel(YomkMsgPtr pkg);
    // 文件侧具名开关唯一写入口:语义与 setNamedConsoleLevel 对称(未命中 ResStatus::Failed 不惰性创建)
    yomk::payload::Response setNamedFileLevel(const std::string& loggerName, yomk::payload::LogLevel level, bool on);

private:
    yomk::payload::Response loggers(YomkMsgPtr pkg);
    yomk::payload::Response loggerInfo(YomkMsgPtr pkg);
    yomk::payload::Response listAll(YomkMsgPtr pkg);
    yomk::payload::Response consoleGlobalState(YomkMsgPtr pkg);
    yomk::payload::Response fileGlobalState(YomkMsgPtr pkg);
    // 取控制台全局主闸态:六级别开关独立 atomic + proxy 叶子锁,均在表锁外独立作用域取
    // (/console_global_state 端点返回体)
    yomk::payload::LoggerState currentConsoleState();
    // 取单条 console 条目状态:六级别 = logger.isLevelOn 逐器层真值(条目不做两层 AND 代算),
    // proxyOn 由调用点在表锁外取好传入
    yomk::payload::LoggerState namedConsoleState(const ConsoleLoggerPtr& consoleLogger, bool proxyOn);
    // 取文件全局主闸态(/file_global_state 端点返回体):六级别开关独立 atomic,
    // 与控制台主闸相互独立;proxyOn 为文件代理态(独立叶子锁,与控制台代理各自独立)
    yomk::payload::LoggerState currentFileState();
    // 取单条 file 条目状态:六级别 = logger.isLevelOn 逐器层真值(条目不做两层 AND 代算),
    // proxyOn 由调用点传入(文件代理态)
    yomk::payload::LoggerState namedFileState(const FileLoggerPtr& fileLogger, bool proxyOn);

private:
    // 锁序约定:双表并发读取一律按 m_consoleLoggersMutex -> m_fileLoggersMutex 顺序嵌套;
    // 两个 proxy 互斥量为叶子锁(临界区内不得取其他锁),proxyOn 一律在表锁外独立取.
    // 容器选型:std::map,改 unordered_map 实测内省/删除路径数倍退化(见 TestYomkLoggerStress.cpp doc 头)
    std::map<std::string, ConsoleLoggerPtr> m_consoleLoggers;
    std::shared_mutex m_consoleLoggersMutex;
    std::map<std::string, FileLoggerPtr> m_fileLoggers;
    std::shared_mutex m_fileLoggersMutex;
    std::atomic<bool> m_showConsoleTraceLog;
    std::atomic<bool> m_showConsoleDebugLog;
    std::atomic<bool> m_showConsoleInfoLog;
    std::atomic<bool> m_showConsoleWarningLog;
    std::atomic<bool> m_showConsoleErrorLog;
    std::atomic<bool> m_showConsoleFatalLog;
    // 文件日志全局主闸(第一层):与控制台主闸相互独立,输出取主闸×逐器两层 AND;
    // 逐器门控随 FileLogger sink 自持(第二层)
    std::atomic<bool> m_showFileTraceLog;
    std::atomic<bool> m_showFileDebugLog;
    std::atomic<bool> m_showFileInfoLog;
    std::atomic<bool> m_showFileWarningLog;
    std::atomic<bool> m_showFileErrorLog;
    std::atomic<bool> m_showFileFatalLog;
    // proxy 字段专属锁:锁内拷贝快照,锁外调用回调,避免持锁重入;
    // 控制台与文件两套代理互不共锁,各自独立
    std::mutex m_consoleLogProxyMutex;
    bool m_consoleLogProxy;
    YomkConsoleLogProxyFunc m_consoleLogProxyFunc;
    // 文件日志全局代理(与控制台代理相互独立,语义对称)
    std::mutex m_fileLogProxyMutex;
    bool m_fileLogProxy;
    YomkFileLogProxyFunc m_fileLogProxyFunc;
};