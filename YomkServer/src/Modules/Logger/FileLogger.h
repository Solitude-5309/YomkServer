#pragma once
#include <string>
#include <memory>
#include <mutex>
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
    void setName(const std::string &name) { m_name = name; }
    std::string getDir() { return m_dir; }
    void setDir(const std::string &dir) { m_dir = dir; }
    bool init();

public:
    void log(ELogLevel logLevel, const std::string &log);
    void write();

private:
    std::string m_name;
    std::string m_dir;
    std::string m_logBuffer;
    std::mutex m_logBufferMutex;
};
typedef std::shared_ptr<FileLogger> FileLoggerPtr;
