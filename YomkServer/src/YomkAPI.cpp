#include "YomkAPI.h"

std::shared_ptr<YomkServer> YomkAPI::m_pServer = nullptr;
std::mutex YomkAPI::m_serverMtx;

std::string YomkAPI::version()
{
    return YOMKSERVER_VERSION;
}