#include "MyBoot.h"

#include <filesystem>

#include "services/ConfigService.h"
#include "typedefine/TypeDefine.h"

int MyBoot::before()
{
    yomk::log().tag("MyBoot::before").info("starting...");

    // 通过 /proc/self/exe 获取可执行文件绝对路径
    std::filesystem::path exePath = std::filesystem::read_symlink("/proc/self/exe");
    std::filesystem::path configPath = exePath.parent_path().parent_path() / "config" / "config.txt";
    yomk::ctx().key(CTX_CONFIG_PATH).value(yomk::msgs::String::shared(configPath.string())).create();
    yomk::log().tag("MyBoot::before").info("config path: ", configPath.string());
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
    yomk::payload::Response resp = yomk::server().url("/ConfigService/load").request();
    if (resp.m_status != yomk::payload::ResStatus::Succeeded)
    {
        yomk::log().tag("MyBoot::after").error("load config failed: ", resp.m_msg);
        return -1;
    }
    yomk::log().tag("MyBoot::after").info("started successfully.");

    resp = yomk::server().url("/ConfigService/get").msg(yomk::msgs::ConfigKey::shared(ConfigKey{"name"})).request();
    if (resp.m_status != yomk::payload::ResStatus::Succeeded)
    {
        yomk::log().tag("MyBoot::after").error("get config name failed: ", resp.m_msg);
        return -1;
    }

    auto name = yomk::msgs::unPack<yomk::msgs::String>(resp.m_data);
    yomk::log().tag("MyBoot::after").info("config name: ", name->d);

    // 测试版本号接口：获取并输出工程版本号（来自 CMake project() 定义的 VERSION，编译期注入）
    resp = yomk::server().url("/ConfigService/version").request();
    if (resp.m_status != yomk::payload::ResStatus::Succeeded)
    {
        yomk::log().tag("MyBoot::after").error("get version failed: ", resp.m_msg);
        return -1;
    }

    auto version = yomk::msgs::unPack<yomk::msgs::String>(resp.m_data);
    yomk::log().tag("MyBoot::after").info("project version: ", version->d);

    resp = yomk::server().url("/ConfigService/get").msg(yomk::msgs::ConfigKey::shared(ConfigKey{"description"})).request();
    if (resp.m_status != yomk::payload::ResStatus::Succeeded)
    {
        yomk::log().tag("MyBoot::after").error("get config description failed: ", resp.m_msg);
        return -1;
    }

    auto description = yomk::msgs::unPack<yomk::msgs::String>(resp.m_data);
    yomk::log().tag("MyBoot::after").info("config description: ", description->d);

    return 0;
}
