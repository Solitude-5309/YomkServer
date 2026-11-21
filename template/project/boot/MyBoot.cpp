#include <yomk/ctx/YomkContextApi.h>
#include <yomk/log/YomkLoggerApi.h>
#include <yomk/srv_info/YomkServerInfoApi.h>

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
        {"/ConfigService", []() { return new ConfigService(yomk::server().instance().get()); }},
    };

    // 根据 m_startSrvNames 按需启动服务
    for (const auto& srvName : m_startSrvNames)
    {
        auto it = serviceCreators.find(srvName);
        if (it != serviceCreators.end())
        {
            if (yomk::server().srv_name(srvName).srv_ptr(it->second()).add() != 0)
                return -1;
        }
    }
    return 0;
}

int MyBoot::after()
{
    // 服务启动后加载配置文件
    yomk::payload::Response resp = yomk::server().url("/ConfigService/load").request();
    if (resp.status != yomk::payload::ResStatus::Succeeded)
    {
        yomk::log().tag("MyBoot::after").error("load config failed: ", resp.msg);
        return -1;
    }
    yomk::log().tag("MyBoot::after").info("started successfully.");

    resp = yomk::server().url("/ConfigService/get").msg(yomk::msgs::prj::ConfigKey::shared(yomk::payload::prj::ConfigKey{"name"})).request();
    if (resp.status != yomk::payload::ResStatus::Succeeded)
    {
        yomk::log().tag("MyBoot::after").error("get config name failed: ", resp.msg);
        return -1;
    }

    auto name = yomk::msgs::unpack<yomk::msgs::String>(resp.data);
    yomk::log().tag("MyBoot::after").info("config name: ", name->d);

    // 测试版本号接口：获取并输出工程版本号（来自 CMake project() 定义的 VERSION，编译期注入）
    resp = yomk::server().url("/ConfigService/version").request();
    if (resp.status != yomk::payload::ResStatus::Succeeded)
    {
        yomk::log().tag("MyBoot::after").error("get version failed: ", resp.msg);
        return -1;
    }

    auto version = yomk::msgs::unpack<yomk::msgs::String>(resp.data);
    yomk::log().tag("MyBoot::after").info("project version: ", version->d);

    resp = yomk::server().url("/ConfigService/get").msg(yomk::msgs::prj::ConfigKey::shared(yomk::payload::prj::ConfigKey{"description"})).request();
    if (resp.status != yomk::payload::ResStatus::Succeeded)
    {
        yomk::log().tag("MyBoot::after").error("get config description failed: ", resp.msg);
        return -1;
    }

    auto description = yomk::msgs::unpack<yomk::msgs::String>(resp.data);
    yomk::log().tag("MyBoot::after").info("config description: ", description->d);

    // 内省演示：经 /YomkServerInfo 查询 ConfigService 函数清单——
    // 不读服务源码即可获得每个端点的期望消息类型与语义描述（installFunc 第三/四参，
    // 描述常量见 interfaces/ConfigServiceUrl.h）。agent/运维据此理解端点契约、自纠调用行为。
    resp = yomk::srv_info().srv_name("/ConfigService").functions();
    if (resp.status != yomk::payload::ResStatus::Succeeded)
    {
        yomk::log().tag("MyBoot::after").error("introspect functions failed: ", resp.msg);
        return -1;
    }
    auto funcs = yomk::msgs::unpack<yomk::msgs::FuncInfoList>(resp.data);
    if (!funcs)
    {
        yomk::log().tag("MyBoot::after").error("introspect functions: unexpected data type");
        return -1;
    }
    yomk::log().tag("MyBoot::after").info("---- ConfigService 内省（/functions，", funcs->d.size(), " 个端点）----");
    for (const auto& fn : funcs->d)
    {
        // description 为多行语义块（五章节：# Overview / # Inputs / # Outputs / # Behavior / # Notes），原样透出
        yomk::log().tag("MyBoot::after").info(fn.funcName, "  期望消息: ", fn.msgName.empty() ? "(无)" : fn.msgName, "\n", fn.description);
    }

    return 0;
}
