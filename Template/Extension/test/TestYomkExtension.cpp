#include <YomkServer/YomkAPI.h>
#include <YomkExtension/ExtensionService.h>
#include <iostream>

using namespace yomk;

static int g_pass = 0;
static int g_fail = 0;

int main(int argc, char *argv[])
{
    YOMK_INIT();

    YOMK_NEW_SERVICE(ExtensionService);

    // 测试版本查询
    YomkResponse resp = YOMK_REQUEST("/ExtensionService/version", nullptr);
    if (resp.m_status == YomkResponse::eOk)
    {
        YomkUnPackPkg(resp.m_data, String, version);
        std::cout << "[PASS] version: " << version->d << std::endl;
        g_pass++;
    }
    else
    {
        std::cout << "[FAIL] version request failed: " << resp.m_msg << std::endl;
        g_fail++;
    }

    std::cout << "\n========== Test Summary ==========" << std::endl;
    std::cout << "PASS: " << g_pass << std::endl;
    std::cout << "FAIL: " << g_fail << std::endl;

    return g_fail > 0 ? 1 : 0;
}
