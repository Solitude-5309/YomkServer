#pragma once
#include <string>
#include <map>
#include <memory>
#include <sstream>

class FileLogger
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
    FileLogger();
    ~FileLogger();
    std::string getName() { return m_name; }
    void setName(const std::string& name) { m_name = name; }
    std::string getDir() { return m_dir; }
    void setDir(const std::string& dir) { m_dir = dir; }
    uint32_t getMaxLineCount() { return m_maxLineCount; }
    void setMaxLineCount(uint32_t maxLineCount) { m_maxLineCount = maxLineCount; }
    void init();
public:
    void log(ELogLevel logLevel, const std::string& log);
    void write();
private:
    std::string m_name;
    std::string m_dir;
    std::uint32_t m_lineCount;
    std::uint32_t m_maxLineCount;
    std::stringstream m_logStream;
};
typedef std::shared_ptr<FileLogger> FileLoggerPtr;
