#pragma once
#include <string>
#include <map>
#include <memory>
#include <sstream>
#include <mutex>
class FileLogger
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
    FileLogger();
    ~FileLogger();
    std::string getName() { return m_name; }
    void setName(const std::string &name) { m_name = name; }
    std::string getDir() { return m_dir; }
    void setDir(const std::string &dir) { m_dir = dir; }
    // P2-b（LG2 修复）：目录/文件创建失败返回 false，不再异常穿透
    bool init();

public:
    void log(ELogLevel logLevel, const std::string &log);
    void write();

private:
    std::string m_name;
    std::string m_dir;
    std::stringstream m_logStream;
    std::mutex m_logStreamMutex;
};
typedef std::shared_ptr<FileLogger> FileLoggerPtr;
