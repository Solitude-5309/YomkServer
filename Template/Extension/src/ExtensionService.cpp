#include "ExtensionService.h"

ExtensionService::ExtensionService(YomkServer* server) : YomkService(server)
{
    name("/ExtensionService");
}

int ExtensionService::init()
{
    installFunc("/version", &ExtensionService::version);
    YOMK_INFO_TAG("ExtensionService::init", "install func [ /version ] to", name());
    return 0;
}

yomk::payload::Response ExtensionService::version(YomkPkgPtr pkg)
{
    std::string version = "TemplateExtension v" EXTENSION_VERSION;
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::String::shared(version));
}
