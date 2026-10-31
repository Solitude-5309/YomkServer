#include "MyBoot.h"

#include <filesystem>

#include "services/ConfigService.h"
#include "typedefine/TypeDefine.h"

int MyBoot::before()
{
    YOMK_INFO_TAG("MyBoot::before", "starting...");

    // 通过 /proc/self/exe 获取可执行文件绝对路径
    std::filesystem::path exePath = std::filesystem::read_symlink("/proc/self/exe");
    std::filesystem::path configPath = exePath.parent_path().parent_path() / "config" / "config.txt";
    YomkAPI::CONTEXT_CREATE(CTX_CONFIG_PATH, yomkmsg::String::shared(configPath.string()));
    YOMK_INFO_TAG("MyBoot::before", "config path: ", configPath.string());
    return 0;
}

int MyBoot::start()
{
    // 服务创建器映射表
    static const std::map<std::string, std::function<YomkService*()>> serviceCreators = {
        {"/ConfigService", []() { return new ConfigService(YomkAPI::serverInstance().get()); }},
    };

    // 根据 m_startSrvNames 按需启动服务
    for (const auto& srvName : m_startSrvNames)
    {
        auto it = serviceCreators.find(srvName);
        if (it != serviceCreators.end())
        {
            if (YomkAPI::addService(it->second(), srvName) != 0)
                return -1;
        }
    }
    return 0;
}

int MyBoot::after()
{
    // 服务启动后加载配置文件
    YomkResponse resp = YomkAPI::request("/ConfigService/load", nullptr);
    if (resp.m_status != YomkResponse::eOk)
    {
        YOMK_ERROR_TAG("MyBoot::after", "load config failed: ", resp.m_msg);
        return -1;
    }
    YOMK_INFO_TAG("MyBoot::after", "started successfully.");

    resp = YomkAPI::request("/ConfigService/get", yomkmsg::ConfigKey::shared(ConfigKey{"name"}));
    if (resp.m_status != YomkResponse::eOk)
    {
        YOMK_ERROR_TAG("MyBoot::after", "get config name failed: ", resp.m_msg);
        return -1;
    }

    auto name = yomk::unPack<yomkmsg::String>(resp.m_data);
    YOMK_INFO_TAG("MyBoot::after", "config name: ", name->d);

    // 测试版本号接口：获取并输出工程版本号（来自 CMake project() 定义的 VERSION，编译期注入）
    resp = YomkAPI::request("/ConfigService/version", nullptr);
    if (resp.m_status != YomkResponse::eOk)
    {
        YOMK_ERROR_TAG("MyBoot::after", "get version failed: ", resp.m_msg);
        return -1;
    }

    auto version = yomk::unPack<yomkmsg::String>(resp.m_data);
    YOMK_INFO_TAG("MyBoot::after", "project version: ", version->d);

    resp = YomkAPI::request("/ConfigService/get", yomkmsg::ConfigKey::shared(ConfigKey{"description"}));
    if (resp.m_status != YomkResponse::eOk)
    {
        YOMK_ERROR_TAG("MyBoot::after", "get config description failed: ", resp.m_msg);
        return -1;
    }

    auto description = yomk::unPack<yomkmsg::String>(resp.m_data);
    YOMK_INFO_TAG("MyBoot::after", "config description: ", description->d);

    return 0;
}
