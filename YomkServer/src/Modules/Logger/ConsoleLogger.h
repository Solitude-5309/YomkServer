#pragma once
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

public:
    void log(ELogLevel logLevel, const std::string& tag, const std::string& log);

private:
    std::string m_name;
    // 类级共享锁：全部 ConsoleLogger 实例（每 tag 惰性创建一个）共用一把，
    // 跨实例（跨 tag）并发输出仍整行原子；实例可被删除重建（consoleLog 惰性重建路径），
    // static 锁不受实例生命周期影响。临界区内无其他锁获取，为叶子锁
    static std::mutex s_mutex;
};
typedef std::shared_ptr<ConsoleLogger> ConsoleLoggerPtr;
