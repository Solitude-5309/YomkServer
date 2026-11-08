#include <yomk/log/YomkLoggerApi.h>

#include "extension/ExtensionService.h"

ExtensionService::ExtensionService(YomkServer* server) : YomkService(server)
{
    name("/ExtensionService");
}

int ExtensionService::init()
{
    installFunc("/version", &ExtensionService::version);
    yomk::log().tag("ExtensionService::init").info("install func [ /version ] to", name());
    return 0;
}

yomk::payload::Response ExtensionService::version(YomkMsgPtr pkg)
{
    std::string version = "extension v" EXTENSION_VERSION;
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::String::shared(version));
}
