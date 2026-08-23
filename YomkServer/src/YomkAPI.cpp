#include "YomkAPI.h"

std::shared_ptr<YomkServer> YomkAPI::m_pServer = nullptr;

std::string YomkAPI::version()
{
    return YOMKSERVER_VERSION;
}