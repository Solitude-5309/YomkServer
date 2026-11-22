#include <yomk/log/YomkLoggerApi.h>

#include "extension/ExtensionService.h"

#include "extension/ExtensionUrl.h"

namespace yomk::service::ext
{
ExtensionService::ExtensionService(YomkServer* server) : YomkService(server)
{
    name("/ExtensionService");
}

int ExtensionService::init()
{
    installFunc("/version", &ExtensionService::version, "", yomk::url::ext::kVersionDesc);
    yomk::log().tag("ExtensionService::init").info("install func [ /version ] to", name());
    return 0;
}

yomk::payload::Response ExtensionService::version(YomkMsgPtr msg)
{
    std::string version = "extension v" EXTENSION_VERSION;
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::String::shared(version));
}
}  // namespace yomk::service::ext
