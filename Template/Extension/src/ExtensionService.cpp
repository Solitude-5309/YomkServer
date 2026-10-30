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

YomkResponse ExtensionService::version(YomkPkgPtr pkg)
{
    std::string version = "TemplateExtension v" EXTENSION_VERSION;
    return YomkResponse(YomkResponse::eOk, "ok", YomkMkPtr(String, version));
}
