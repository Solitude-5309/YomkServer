#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
class FileLogger
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
    FileLogger();
    ~FileLogger();
    std::string getName() { return m_name; }
    void setName(const std::string& name) { m_name = name; }
    std::string getDir() { return m_dir; }
    void setDir(const std::string& dir) { m_dir = dir; }
    // 按器级别门控：setLevelOn 关断的级别在 log() 入口直接丢弃；unknown 级别按 Info 门控
    void setLevelOn(ELogLevel logLevel, bool on);
    bool isLevelOn(ELogLevel logLevel) const;
    bool init();

public:
    void log(ELogLevel logLevel, const std::string& tag, const std::string& log);
    void write();

private:
    std::string m_name;
    std::string m_dir;
    // 级别门控随 sink 自持（每实例独立、默认全开）：两级主闸并联架构的第二层（逐器门控），
    // 第一层为 YomkLogger 文件全局主闸（后续闭环补齐），输出取两层 AND
    std::atomic<bool> m_showDebugLog;
    std::atomic<bool> m_showInfoLog;
    std::atomic<bool> m_showWarnLog;
    std::atomic<bool> m_showErrorLog;
    std::string m_logBuffer;
    std::mutex m_logBufferMutex;
};
typedef std::shared_ptr<FileLogger> FileLoggerPtr;
