#pragma once
#include <yomk/server/YomkServerApi.h>

using namespace yomk::payload;

// 扩展服务类落 yomk::service::ext 子命名空间，不占各根命名空间；
// yomk::ext 为链式入口函数（函数/命名空间不可同名），故服务域取 yomk::service::ext。
namespace yomk::service::ext
{
class ExtensionService : public YomkService
{
public:
    ExtensionService(YomkServer* server);
    virtual ~ExtensionService() {}
    virtual int init() override;

private:
    yomk::payload::Response version(YomkMsgPtr pkg);
};
}  // namespace yomk::service::ext
