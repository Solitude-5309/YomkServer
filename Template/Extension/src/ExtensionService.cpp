#include "ExtensionService.h"

ExtensionService::ExtensionService(YomkServer *server)
    : YomkService(server)
{
    name("/ExtensionService");
}

int ExtensionService::init()
{
    YomkInstallFunc("/version", ExtensionService::version);
    YOMK_INFO_TAG("ExtensionService::init", "install func [ /version ] to", name());
    return 0;
}

YomkResponse ExtensionService::version(YomkPkgPtr pkg)
{
    std::string version = "YomkExtension v" EXTENSION_VERSION;
    return YomkResponse(YomkResponse::eOk, "ok", YomkMkPtr(String, version));
}
