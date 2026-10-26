#pragma once
#include <atomic>
#include <map>
#include <mutex>
#include <shared_mutex>

#include "ConsoleLogger.h"
#include "FileLogger.h"
#include "YomkDefine.h"
#include "YomkServer.h"
using namespace yomk;
class YomkLogger : public YomkService
{
public:
    YomkLogger(YomkServer* server);
    virtual ~YomkLogger();

public:
    virtual int init() override;

private:
    YomkResponse consoleLog(YomkPkgPtr pkg);
    YomkResponse setConsoleLogProxy(YomkPkgPtr pkg);
    YomkResponse setFileLogProxy(YomkPkgPtr pkg);
    YomkResponse createFileLogger(YomkPkgPtr pkg);
    YomkResponse fileLog(YomkPkgPtr pkg);
    YomkResponse writeFileLog(YomkPkgPtr pkg);
    YomkResponse deleteLogger(YomkPkgPtr pkg);
    YomkResponse offConsoleLogByLevel(YomkPkgPtr pkg);
    YomkResponse onConsoleLogByLevel(YomkPkgPtr pkg);
    // 具名开关唯一写入口：按名查表逐器门控（未命中 eNo 不惰性创建，不触碰全局主闸）
    YomkResponse setNamedConsoleLevel(const std::string& loggerName, Log::ELogLevel level, bool on);

    YomkResponse offFileLogByLevel(YomkPkgPtr pkg);
    YomkResponse onFileLogByLevel(YomkPkgPtr pkg);
    // 文件侧具名开关唯一写入口：语义与 setNamedConsoleLevel 对称（未命中 eNo 不惰性创建）
    YomkResponse setNamedFileLevel(const std::string& loggerName, Log::ELogLevel level, bool on);

private:
    YomkResponse loggers(YomkPkgPtr pkg);
    YomkResponse loggerInfo(YomkPkgPtr pkg);
    YomkResponse listAll(YomkPkgPtr pkg);
    YomkResponse consoleGlobalState(YomkPkgPtr pkg);
    YomkResponse fileGlobalState(YomkPkgPtr pkg);
    // 取控制台全局主闸态：四个级别开关独立 atomic + proxy 叶子锁，均在表锁外独立作用域取
    // （/console_global_state 端点返回体）
    LoggerState currentConsoleState();
    // 取单条 console 条目状态：四级别 = logger.isLevelOn 逐器层真值（条目不做两层 AND 代算），
    // proxyOn 由调用点在表锁外取好传入
    LoggerState namedConsoleState(const ConsoleLoggerPtr& consoleLogger, bool proxyOn);
    // 取文件全局主闸态（/file_global_state 端点返回体）：四个级别开关独立 atomic，
    // 与控制台主闸相互独立；proxyOn 为文件代理态（独立叶子锁，与控制台代理各自独立）
    LoggerState currentFileState();
    // 取单条 file 条目状态：四级别 = logger.isLevelOn 逐器层真值（条目不做两层 AND 代算），
    // proxyOn 由调用点传入（文件代理态）
    LoggerState namedFileState(const FileLoggerPtr& fileLogger, bool proxyOn);

private:
    // 锁序约定：需要同时读取两张 logger 表时（listAll 内省快照），
    // 一律按 m_consoleLoggersMutex -> m_fileLoggersMutex 的顺序嵌套持有，全仓无反向嵌套点
    // （consoleLog/fileLog 的 proxy 锁在独立作用域释放后才取 logger 表锁；loggerInfo/
    // writeFileLog/createFileLogger/setNamedConsoleLevel/setNamedFileLevel/setFileLogProxy 均单锁；
    // currentConsoleState/namedConsoleState 的 proxyOn 与 currentFileState/loggerInfo/listAll 的
    // 文件 proxyOn 均在表锁外先独立取 proxy 锁，沿用 proxy 锁独立作用域先释放的既有模式）；
    // m_consoleLogProxyMutex 与 m_fileLogProxyMutex 为叶子锁，其临界区内不得获取任何其他锁。
    // 后续维护新增嵌套时须沿用此序，否则有死锁风险。
    // 容器选型：两表保持 std::map——曾实测改 unordered_map 致内省/删除路径数倍退化
    // （S4A_loggers +116%、S5B_delete_all -40%、S5A_mixed_churn -58%），已回退；
    // 完整消融数据见 TestYomkLoggerStress.cpp doc 头。map 中序遍历天然字典序，内省输出无需排序。
    // 锁序约定与容器类型无关，选型变化不影响死锁分析。
    std::map<std::string, ConsoleLoggerPtr> m_consoleLoggers;
    std::shared_mutex m_consoleLoggersMutex;
    std::map<std::string, FileLoggerPtr> m_fileLoggers;
    std::shared_mutex m_fileLoggersMutex;
    std::atomic<bool> m_showConsoleDebugLog;
    std::atomic<bool> m_showConsoleInfoLog;
    std::atomic<bool> m_showConsoleWarningLog;
    std::atomic<bool> m_showConsoleErrorLog;
    // 文件日志全局主闸（第一层）：与控制台主闸相互独立，输出取主闸×逐器两层 AND；
    // 逐器门控随 FileLogger sink 自持（第二层）
    std::atomic<bool> m_showFileDebugLog;
    std::atomic<bool> m_showFileInfoLog;
    std::atomic<bool> m_showFileWarningLog;
    std::atomic<bool> m_showFileErrorLog;
    // proxy 字段专属锁：锁内拷贝快照、锁外调用回调，避免持锁重入；
    // 控制台与文件两套代理互不共锁，各自独立
    std::mutex m_consoleLogProxyMutex;
    bool m_consoleLogProxy;
    YomkConsoleLogProxyFunc m_consoleLogProxyFunc;
    // 文件日志全局代理（与控制台代理相互独立，语义对称）
    std::mutex m_fileLogProxyMutex;
    bool m_fileLogProxy;
    YomkFileLogProxyFunc m_fileLogProxyFunc;
};