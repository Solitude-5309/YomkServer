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

yomk::Response ConfigService::loadConfig(YomkPkgPtr pkg)
{
    // 从 Context 获取配置文件路径
    auto ctxVal = YomkAPI::getContext<yomkmsg::String>(CTX_CONFIG_PATH, nullptr);
    if (!ctxVal)
        return yomk::Response(yomk::ResStatus::Failed, "config_path not found in context");
    m_configPath = ctxVal->d;

    // 加载配置文件（纯文本 key: value 格式）
    std::ifstream ifs(m_configPath);
    if (!ifs.is_open())
        return yomk::Response(yomk::ResStatus::Failed, "failed to open: " + m_configPath);

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
    return yomk::Response(yomk::ResStatus::Succeeded, "ok");
}

yomk::Response ConfigService::getConfig(YomkPkgPtr pkg)
{
    auto data = yomk::unPack<yomkmsg::ConfigKey>(pkg);
    if (!data)
    {
        return {yomk::ResStatus::Failed, "pkg is null or pkg is not ConfigKey"};
    }

    auto it = m_configMap.find(data->req.key);
    if (it == m_configMap.end())
    {
        return yomk::Response(yomk::ResStatus::Failed, "key not found: " + data->req.key);
    }

    return yomk::Response(yomk::ResStatus::Succeeded, "ok", yomkmsg::String::shared(it->second));
}

yomk::Response ConfigService::setConfig(YomkPkgPtr pkg)
{
    auto data = yomk::unPack<yomkmsg::ConfigKeyValue>(pkg);
    if (!data)
    {
        return {yomk::ResStatus::Failed, "pkg is null or pkg is not ConfigKeyValue"};
    }

    m_configMap[data->req.key] = data->req.value;
    YOMK_INFO_TAG("ConfigService::setConfig", "set ", data->req.key, " = ", data->req.value);
    return yomk::Response(yomk::ResStatus::Succeeded, "ok");
}

yomk::Response ConfigService::reloadConfig(YomkPkgPtr pkg)
{
    m_configMap.clear();
    return loadConfig(nullptr);
}

yomk::Response ConfigService::version(YomkPkgPtr pkg)
{
    // 版本号来自 CMake project() 定义的 VERSION，编译期通过 APP_VERSION 宏注入
    return yomk::Response(yomk::ResStatus::Succeeded, "ok", yomkmsg::String::shared(std::string(APP_VERSION)));
}
