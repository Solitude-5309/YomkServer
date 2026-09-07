#pragma once
#include "YomkServer.h"
#include "YomkDefine.h"
#include "ConsoleLogger.h"
#include "FileLogger.h"
#include <map>
#include <shared_mutex>
#include <mutex>
#include <atomic>
using namespace yomk;
class YomkLogger : public YomkService
{
public:
    YomkLogger(YomkServer *server);
    virtual ~YomkLogger();

public:
    // LG5：补 override。基类 YomkService::init() 为纯虚（YomkService.h:31），本函数是其覆盖；
    // FunctionPool/Context/EventLoop/ServerInfo 四个同级模块均已写 override，Logger 是唯一例外，
    // 故 cppcheck 的 missingOverride 仅在 Logger 模块报出。override 是纯编译期检查、零行为变化，
    // 补齐后 Logger 与其余模块的 cppcheck 告警口径一致（均为 0）。
    virtual int init() override;

private:
    YomkResponse consoleLog(YomkPkgPtr pkg);
    YomkResponse setConsoleLogProxy(YomkPkgPtr pkg);
    YomkResponse createFileLogger(YomkPkgPtr pkg);
    YomkResponse fileLog(YomkPkgPtr pkg);
    YomkResponse writeFileLog(YomkPkgPtr pkg);
    YomkResponse deleteLogger(YomkPkgPtr pkg);
    YomkResponse offConsoleLogByLevel(YomkPkgPtr pkg);
    YomkResponse onConsoleLogByLevel(YomkPkgPtr pkg);

private:
    YomkResponse loggers(YomkPkgPtr pkg);
    YomkResponse loggerInfo(YomkPkgPtr pkg);
    YomkResponse listAll(YomkPkgPtr pkg);
    std::string consoleLevelLine();

private:
    // 锁序约定（LG3/P3-a）：需要同时读取两张 logger 表时（loggers/listAll 内省快照），
    // 一律按 m_consoleLoggersMutex -> m_fileLoggersMutex 的顺序嵌套持有，全仓无反向嵌套点
    // （consoleLog 的 proxy 锁在独立作用域释放后才取 console 锁；loggerInfo/fileLog/
    // writeFileLog/createFileLogger 均单锁）；m_consoleLogProxyMutex 为叶子锁，
    // 其临界区内不得获取任何其他锁。后续维护新增嵌套时须沿用此序，否则有死锁风险。
    // 容器选型（LG6/P4-d 实测否决，故两表保持 std::map）：LG6 曾按 LG4 的登记意图改为
    // std::unordered_map，并在 loggers/listAll 的锁外分段排序以保住内省输出的字典序契约。
    // 四方消融实测（变体 A=map 无 move、D'=map+move、E=unordered_map+move 无排序、
    // B=unordered_map+move+排序；N=20000 四轮拉丁方交错 + N=100000 两轮交错，每变体独立构建
    // .so 并以 LD_LIBRARY_PATH 隔离，各变体内部方差 <15%）结论为：仅换容器即令 S4A_loggers
    // 由 3.05 ms 升至 6.60 ms（+116%）、S5B_delete_all 由 1534050 降至 919038 ops/s（-40%）、
    // S5A_mixed_churn 由 4702 降至 1976 ops/s（-58%）；再叠加保序排序后 S4A_loggers 达 12.48 ms、
    // S1A_scan_ALL 由 0.90 ms 升至 3.71 ms、S4C_logger_single_query 由约 1.55M 降至约 1.20M ops/s
    // （-10.6%，未过 V2 主判据「不慢于修复前」），N=100000 下同向且更剧烈（S4A_loggers
    // 22.7 → 90.0 ms）。而 S4_VmHWM/S5_VmHWM 与 S1A/S1B/S1E/S1D/S2A/S3A 六条热路径在换容器前后
    // 差异均在噪声内——O(log n) → O(1) 摊还的理论收益，在本模块的实际规模与访问模式下换不到
    // 任何可测收益，却付出了内省与删除路径数倍退化，故回退。P4-d 以「实测否决」结论收口。
    // 退化机制未在本闭环内进一步定位（消融已排除「排序是唯一因素」：不排序时仍慢约 2 倍），
    // 连同本组数据登记于 TestYomkLoggerStress.cpp doc 头备查。
    // 上述锁序约定与容器类型无关，选型变化不影响死锁分析。
    std::map<std::string, ConsoleLoggerPtr> m_consoleLoggers;
    std::shared_mutex m_consoleLoggersMutex;
    std::map<std::string, FileLoggerPtr> m_fileLoggers;
    std::shared_mutex m_fileLoggersMutex;
    std::atomic<bool> m_showConsoleDebugLog;
    std::atomic<bool> m_showConsoleInfoLog;
    std::atomic<bool> m_showConsoleWarningLog;
    std::atomic<bool> m_showConsoleErrorLog;
    // proxy 字段专属锁（LG2/P1-b）：setConsoleLogProxy 写与 consoleLog 读均持锁，
    // 回调在锁外执行（锁内拷贝、锁外调用，避免持锁重入）
    std::mutex m_consoleLogProxyMutex;
    bool m_consoleLogProxy;
    YomkConsoleLogProxyFunc m_consoleLogProxyFunc;
};