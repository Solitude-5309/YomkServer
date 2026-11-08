#pragma once
#include <string>

#include "yomk/server/YomkServerPayload.h"

namespace yomk::payload
{

struct Function
{
    std::string m_funcName;
    YomkServiceFunc m_func;
    std::string m_msgName;
    std::string m_description;
};

struct CallFunction
{
    std::string m_funcName;
    YomkMsgPtr m_pkg;
};

}  // namespace yomk::payload
