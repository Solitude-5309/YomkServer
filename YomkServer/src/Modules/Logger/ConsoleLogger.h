#pragma once
#include <string>
#include <map>
#include <memory>

class ConsoleLogger
{
public:
    enum ELogLevel
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
    void log(ELogLevel logLevel, const std::string& log);
private:
    std::string m_name;
};
typedef std::shared_ptr<ConsoleLogger> ConsoleLoggerPtr;
