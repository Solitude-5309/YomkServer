#include "ConfigService.h"

#include <algorithm>
#include <fstream>

#include "typedefine/TypeDefine.h"

// 去除首尾空白符
static std::string trim(const std::string& str)
{
    auto first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return "";
    auto last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, last - first + 1);
}

ConfigService::ConfigService(YomkServer* server) : YomkService(server)
{
    name("/ConfigService");
}

int ConfigService::init()
{
    // 安装功能函数
    installFunc("/load", &ConfigService::loadConfig);
    installFunc("/get", &ConfigService::getConfig);
    installFunc("/set", &ConfigService::setConfig);
    installFunc("/reload", &ConfigService::reloadConfig);
    installFunc("/version", &ConfigService::version);
    YOMK_INFO_TAG("ConfigService::init", "install func [ /load /get /set /reload /version ] to", name());
    return 0;
}

yomk::payload::Response ConfigService::loadConfig(YomkMsgPtr pkg)
{
    // 从 Context 获取配置文件路径
    auto ctxVal = yomk::ctx().key(CTX_CONFIG_PATH).msg<yomk::msgs::String>().with_default(nullptr).get();
    if (!ctxVal)
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "config_path not found in context");
    m_configPath = ctxVal->d;

    // 加载配置文件（纯文本 key: value 格式）
    std::ifstream ifs(m_configPath);
    if (!ifs.is_open())
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "failed to open: " + m_configPath);

    std::string line;
    while (std::getline(ifs, line))
    {
        if (line.empty())
            continue;
        // 按第一个冒号分割 key:value
        auto colonPos = line.find(':');
        if (colonPos == std::string::npos)
            continue;
        std::string key = trim(line.substr(0, colonPos));
        std::string value = trim(line.substr(colonPos + 1));
        if (!key.empty())
            m_configMap[key] = value;
    }
    ifs.close();
    YOMK_INFO_TAG("ConfigService::loadConfig", "loaded: ", m_configPath);
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok");
}

yomk::payload::Response ConfigService::getConfig(YomkMsgPtr pkg)
{
    auto data = yomk::msgs::unPack<yomk::msgs::ConfigKey>(pkg);
    if (!data)
    {
        return {yomk::payload::ResStatus::Failed, "pkg is null or pkg is not ConfigKey"};
    }

    auto it = m_configMap.find(data->req.key);
    if (it == m_configMap.end())
    {
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "key not found: " + data->req.key);
    }

    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::String::shared(it->second));
}

yomk::payload::Response ConfigService::setConfig(YomkMsgPtr pkg)
{
    auto data = yomk::msgs::unPack<yomk::msgs::ConfigKeyValue>(pkg);
    if (!data)
    {
        return {yomk::payload::ResStatus::Failed, "pkg is null or pkg is not ConfigKeyValue"};
    }

    m_configMap[data->req.key] = data->req.value;
    YOMK_INFO_TAG("ConfigService::setConfig", "set ", data->req.key, " = ", data->req.value);
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok");
}

yomk::payload::Response ConfigService::reloadConfig(YomkMsgPtr pkg)
{
    m_configMap.clear();
    return loadConfig(nullptr);
}

yomk::payload::Response ConfigService::version(YomkMsgPtr pkg)
{
    // 版本号来自 CMake project() 定义的 VERSION，编译期通过 APP_VERSION 宏注入
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::String::shared(std::string(APP_VERSION)));
}
