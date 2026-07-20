#include "MyBoot.h"
#include "services/ConfigService.h"
#include "typedefine/TypeDefine.h"

#include <filesystem>

int MyBoot::before()
{
    YOMK_INFO_TAG("MyBoot::before", "starting...");

    // 通过 /proc/self/exe 获取可执行文件绝对路径
    std::filesystem::path exePath = std::filesystem::read_symlink("/proc/self/exe");
    std::filesystem::path configPath = exePath.parent_path().parent_path() / "config" / "config.json";
    YOMK_CONTEXT_CREATE(CTX_CONFIG_PATH, YomkMkPtr(String, configPath.string()));
    YOMK_INFO_TAG("MyBoot::before", "config path: ", configPath.string());
    return 0;
}

int MyBoot::start()
{
    // 服务创建器映射表
    static const std::map<std::string, std::function<YomkService *()>> serviceCreators = {
        {"/ConfigService", []()
         { return new ConfigService(YOMK_SERVER_P); }},
    };

    // 根据 m_startSrvNames 按需启动服务
    for (const auto &srvName : m_startSrvNames)
    {
        auto it = serviceCreators.find(srvName);
        if (it != serviceCreators.end())
        {
            if (YOMK_ADD_SERVICE(it->second(), srvName) != 0)
                return -1;
        }
    }
    return 0;
}

int MyBoot::after()
{
    // 服务启动后加载配置文件
    YomkResponse resp = YOMK_REQUEST("/ConfigService/load", nullptr);
    if (resp.m_status != YomkResponse::eOk)
    {
        YOMK_ERROR_TAG("MyBoot::after", "load config failed: ", resp.m_msg);
        return -1;
    }
    YOMK_INFO_TAG("MyBoot::after", "started successfully.");
    return 0;
}
