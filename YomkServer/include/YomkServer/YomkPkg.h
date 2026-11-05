#pragma once
#include <memory>
#include <string>

class YomkMsg
{
public:
    YomkMsg() {}
    virtual ~YomkMsg() {}

public:
    void name(const std::string& name) { m_name = name; }
    std::string name() { return m_name; }

protected:
    std::string m_name;
};
using YomkMsgPtr = std::shared_ptr<YomkMsg>;

#include "YomkMsgs.h"