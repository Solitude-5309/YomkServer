#include "ConfigService.h"
#include <fstream>
#include <sstream>
#include "typedefine/TypeDefine.h"

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

    // 加载配置文件
    std::ifstream ifs(m_configPath);
    if (!ifs.is_open())
        return YomkResponse(YomkResponse::eNo, "failed to open: " + m_configPath);
    m_json = nlohmann::json::parse(ifs);
    ifs.close();
    YOMK_INFO_TAG("ConfigService::loadConfig", "loaded: ", m_configPath);
    return YomkResponse(YomkResponse::eOk, "ok");
}

YomkResponse ConfigService::getConfig(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, YConfigKey, data);

    // 按点分路径逐层访问
    std::istringstream ss(data->req.key);
    std::string token;
    nlohmann::json *current = &m_json;
    while (std::getline(ss, token, '.'))
    {
        if (!current->is_object() || !current->contains(token))
        {
            return YomkResponse(YomkResponse::eNo, "key not found: " + data->req.key);
        }
        current = &(*current)[token];
    }

    // 将值转为字符串返回
    std::string value;
    if (current->is_string())
        value = current->get<std::string>();
    else
        value = current->dump();

    return YomkResponse(YomkResponse::eOk, "ok", YomkMkPtr(String, value));
}

YomkResponse ConfigService::setConfig(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, YConfigKeyValue, data);

    // 按点分路径逐层访问
    std::istringstream ss(data->req.key);
    std::string token;
    nlohmann::json *current = &m_json;
    while (std::getline(ss, token, '.'))
    {
        if (!current->is_object())
        {
            return YomkResponse(YomkResponse::eNo, "invalid key path: " + data->req.key);
        }
        current = &(*current)[token];
    }

    *current = data->req.value;
    YOMK_INFO_TAG("ConfigService::setConfig", "set ", data->req.key, " = ", data->req.value);
    return YomkResponse(YomkResponse::eOk, "ok");
}

YomkResponse ConfigService::reloadConfig(YomkPkgPtr pkg)
{
    std::ifstream ifs(m_configPath);
    if (!ifs.is_open())
    {
        return YomkResponse(YomkResponse::eNo, "failed to open config file: " + m_configPath);
    }
    m_json = nlohmann::json::parse(ifs);
    ifs.close();
    YOMK_INFO_TAG("ConfigService::reloadConfig", "reloaded config file: ", m_configPath);
    return YomkResponse(YomkResponse::eOk, "ok");
}
