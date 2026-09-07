#pragma once
#include <string>
#include <memory>
#include <mutex>
class ConsoleLogger
{
public:
    // P2-c（LG2 修复）：固定底层类型，使外部注入任意 int 值不再是 UB，
    // switch default 降级分支转为可合法触达的活分支
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
    void setName(const std::string &name) { m_name = name; }

public:
    void log(ELogLevel logLevel, const std::string &log);

private:
    std::string m_name;
    std::mutex m_mutex;
};
typedef std::shared_ptr<ConsoleLogger> ConsoleLoggerPtr;
