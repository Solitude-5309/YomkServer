#pragma once
#include <memory>
#include <string>

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

#include "YomkMsgs.h"