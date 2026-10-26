#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
class ConsoleLogger
{
public:
    enum ELogLevel : int
    {
        eDebug,
        eInfo,
        eWarn,
        eError,
    };

public:
    ConsoleLogger();
    ~ConsoleLogger();
    std::string getName() { return m_name; }
    void setName(const std::string& name) { m_name = name; }
    // 按器级别门控：setLevelOn 关断的级别在 log() 入口直接丢弃；unknown 级别按 Info 门控
    void setLevelOn(ELogLevel logLevel, bool on);
    bool isLevelOn(ELogLevel logLevel) const;

public:
    void log(ELogLevel logLevel, const std::string& tag, const std::string& log);

private:
    std::string m_name;
    // 级别门控随 sink 自持（每实例独立、默认全开）：现网开关真源仍为 YomkLogger 全局
    // atomic（服务层先行短路），本类门控为其后置二道防线
    std::atomic<bool> m_showDebugLog;
    std::atomic<bool> m_showInfoLog;
    std::atomic<bool> m_showWarnLog;
    std::atomic<bool> m_showErrorLog;
    // 类级共享锁：全部 ConsoleLogger 实例（每 tag 惰性创建一个）共用一把，
    // 跨实例（跨 tag）并发输出仍整行原子；实例可被删除重建（consoleLog 惰性重建路径），
    // static 锁不受实例生命周期影响。临界区内无其他锁获取，为叶子锁
    static std::mutex s_mutex;
};
typedef std::shared_ptr<ConsoleLogger> ConsoleLoggerPtr;
