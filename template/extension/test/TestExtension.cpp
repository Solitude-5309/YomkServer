#include <yomk/extension/ExtensionApi.h>
#include <yomk/extension/ExtensionService.h>
#include <yomk/server/YomkServerApi.h>

#include <iostream>

using namespace yomk::payload;

static int g_pass = 0;
static int g_fail = 0;

int main(int argc, char* argv[])
{
    yomk::server().init();

    yomk::server().srv<ExtensionService>().create();

    // 测试版本查询（url 常量见 ExtensionUrl.h，经 ExtensionApi.h 已引入）
    yomk::payload::Response resp = yomk::server().url(yomk::url::extVersion).request();
    if (resp.status == yomk::payload::ResStatus::Succeeded)
    {
        auto version = yomk::msgs::unpack<yomk::msgs::String>(resp.data);
        std::cout << "[PASS] version: " << version->d << std::endl;
        g_pass++;
    }
    else
    {
        std::cout << "[FAIL] version request failed: " << resp.msg << std::endl;
        g_fail++;
    }

    // 测试扩展链式域入口（四件套 ExtensionApi.h 的 yomk::ext()）
    yomk::payload::Response extResp = yomk::ext().version();
    if (extResp.status == yomk::payload::ResStatus::Succeeded)
    {
        auto extVersion = yomk::msgs::unpack<yomk::msgs::String>(extResp.data);
        std::cout << "[PASS] ext().version(): " << extVersion->d << std::endl;
        g_pass++;
    }
    else
    {
        std::cout << "[FAIL] ext().version() request failed: " << extResp.msg << std::endl;
        g_fail++;
    }

    std::cout << "\n========== Test Summary ==========" << std::endl;
    std::cout << "PASS: " << g_pass << std::endl;
    std::cout << "FAIL: " << g_fail << std::endl;

    return g_fail > 0 ? 1 : 0;
}
