#pragma once
#include <memory>
#include <string>

// 消息包基类
class YomkPkg
{
public:
    YomkPkg() {}
    virtual ~YomkPkg() {}

public:
    void name(const std::string& name) { m_name = name; }
    std::string name() { return m_name; }

protected:
    std::string m_name;
};
using YomkPkgPtr = std::shared_ptr<YomkPkg>;

// 消息包目录（伞形包含：所有 yomkmsg:: 消息包实例化见 YomkMsgs.h；保持 YomkPkg.h 单头入口不变）
#include "YomkMsgs.h"