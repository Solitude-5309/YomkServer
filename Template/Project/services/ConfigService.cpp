#include "ConfigService.h"
#include <fstream>
#include <algorithm>
#include "typedefine/TypeDefine.h"

// 去除首尾空白符
static std::string trim(const std::string &str)
{
    auto first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return "";
    auto last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, last - first + 1);
}

ConfigService::ConfigService(YomkServer *server)
    : YomkService(server)
{
    name("/ConfigService");
}

int ConfigService::init()
{
    // 安装功能函数
    YomkInstallFunc("/load", ConfigService::loadConfig);
    YomkInstallFunc("/get", ConfigService::getConfig);
    YomkInstallFunc("/set", ConfigService::setConfig);
    YomkInstallFunc("/reload", ConfigService::reloadConfig);
    YOMK_INFO_TAG("ConfigService::init", "install func [ /load /get /set /reload ] to", name());
    return 0;
}

YomkResponse ConfigService::loadConfig(YomkPkgPtr pkg)
{
    // 从 Context 获取配置文件路径
    auto ctxVal = YOMK_CONTEXT_GET(String, CTX_CONFIG_PATH, nullptr);
    if (!ctxVal)
        return YomkResponse(YomkResponse::eNo, "config_path not found in context");
    m_configPath = ctxVal->d;

    // 加载配置文件（纯文本 key: value 格式）
    std::ifstream ifs(m_configPath);
    if (!ifs.is_open())
        return YomkResponse(YomkResponse::eNo, "failed to open: " + m_configPath);

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
    return YomkResponse(YomkResponse::eOk, "ok");
}

YomkResponse ConfigService::getConfig(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, YConfigKey, data);

    auto it = m_configMap.find(data->req.key);
    if (it == m_configMap.end())
    {
        return YomkResponse(YomkResponse::eNo, "key not found: " + data->req.key);
    }

    return YomkResponse(YomkResponse::eOk, "ok", YomkMkPtr(String, it->second));
}

YomkResponse ConfigService::setConfig(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, YConfigKeyValue, data);

    m_configMap[data->req.key] = data->req.value;
    YOMK_INFO_TAG("ConfigService::setConfig", "set ", data->req.key, " = ", data->req.value);
    return YomkResponse(YomkResponse::eOk, "ok");
}

YomkResponse ConfigService::reloadConfig(YomkPkgPtr pkg)
{
    m_configMap.clear();
    return loadConfig(nullptr);
}
