/**
 * @file TestYomkServerInfo.cpp
 * @brief YomkServerInfo 调试内省接口示例
 *
 * 演示内容：
 * 1. YomkInstallFunc 三参形式声明功能函数期望的消息类型（内省元数据）
 * 2. YomkInstallFunc 两参形式零改动兼容旧写法
 * 3. 同名覆盖安装：两参覆盖后残留类型元数据被清除（内省与注册一致）
 * 4. 服务列表 / 函数列表 / 单函数类型查询 / 全量 dump 四个内省接口
 *
 */

#include <iostream>
#include <algorithm>
#include "YomkAPI.h"

/**
 * @brief 演示服务：/DemoService
 *
 * - /with_type: 三参宏安装，声明消息类型 String（内省可见）
 * - /no_type:   两参宏安装，验证旧写法兼容（内省无类型）
 * - /reinstall: 先三参声明类型再两参覆盖安装，验证残留类型元数据被清除
 */
class DemoService : public YomkService
{
public:
    DemoService(YomkServer *server)
        : YomkService(server)
    {
        name("/DemoService");
    }

public:
    virtual int init() override
    {
        YomkInstallFunc("/with_type", DemoService::withType, String);
        YomkInstallFunc("/no_type", DemoService::noType);
        // 覆盖安装：先三参声明类型，再两参覆盖，残留类型元数据应被清除
        YomkInstallFunc("/reinstall", DemoService::reinstall, String);
        YomkInstallFunc("/reinstall", DemoService::reinstall);
        return 0;
    }

private:
    YomkResponse withType(YomkPkgPtr pkg)
    {
        return {YomkResponse::eOk, "with_type"};
    }
    YomkResponse noType(YomkPkgPtr pkg)
    {
        return {YomkResponse::eOk, "no_type"};
    }
    YomkResponse reinstall(YomkPkgPtr pkg)
    {
        return {YomkResponse::eOk, "reinstall"};
    }
};

// 从 StringArray 响应中取出字符串列表
static bool unpackLines(const YomkResponse &response, std::vector<std::string> &lines)
{
    YomkUnPackPkg(response.m_data, StringArray, arr);
    if (!arr)
    {
        return false;
    }
    lines = arr->d;
    return true;
}

// 检查字符串列表中是否包含指定行
static bool hasLine(const std::vector<std::string> &lines, const std::string &line)
{
    return std::find(lines.begin(), lines.end(), line) != lines.end();
}

int main(int argc, char *argv[])
{
    // 初始化框架（自动启动内置服务，含 /YomkServerInfo）
    YOMK_INIT();

    // 注册演示服务
    YOMK_ADD_SERVICE(new DemoService(YOMK_SERVER_P));

    int failed = 0;

    // 1. 服务列表：/DemoService 与 /YomkServerInfo 均应在列
    {
        YomkResponse response = YOMK_SERVER_INFO_SERVICES();
        std::vector<std::string> lines;
        if (response.m_status != YomkResponse::eOk || !unpackLines(response, lines) ||
            !hasLine(lines, "/DemoService") || !hasLine(lines, "/YomkServerInfo"))
        {
            YOMK_ERROR_TAG("main", "SERVER_INFO_SERVICES check failed.");
            ++failed;
        }
        else
        {
            YOMK_INFO_TAG("main", "SERVER_INFO_SERVICES ok, size: ", lines.size());
        }
    }

    // 2. 函数列表：三参宏带类型、两参宏无类型
    {
        YomkResponse response = YOMK_SERVER_INFO_FUNCTIONS("/DemoService");
        std::vector<std::string> lines;
        if (response.m_status != YomkResponse::eOk || !unpackLines(response, lines) ||
            !hasLine(lines, "/with_type [String]") || !hasLine(lines, "/no_type"))
        {
            YOMK_ERROR_TAG("main", "SERVER_INFO_FUNCTIONS check failed.");
            ++failed;
        }
        else
        {
            YOMK_INFO_TAG("main", "SERVER_INFO_FUNCTIONS ok: /with_type [String], /no_type");
        }
    }

    // 3. 单函数类型查询：命中返回类型名
    {
        YomkResponse response = YOMK_SERVER_INFO_FUNCTION("/DemoService/with_type");
        if (response.m_status != YomkResponse::eOk || response.m_msg != "String")
        {
            YOMK_ERROR_TAG("main", "SERVER_INFO_FUNCTION hit check failed, msg: ", response.m_msg);
            ++failed;
        }
        else
        {
            YOMK_INFO_TAG("main", "SERVER_INFO_FUNCTION hit ok, msg: ", response.m_msg);
        }
    }

    // 4. 单函数类型查询：未声明类型返回空串
    {
        YomkResponse response = YOMK_SERVER_INFO_FUNCTION("/DemoService/no_type");
        if (response.m_status != YomkResponse::eOk || !response.m_msg.empty())
        {
            YOMK_ERROR_TAG("main", "SERVER_INFO_FUNCTION no type check failed, msg: ", response.m_msg);
            ++failed;
        }
        else
        {
            YOMK_INFO_TAG("main", "SERVER_INFO_FUNCTION no type ok, msg is empty.");
        }
    }

    // 5. 单函数类型查询：函数不存在 / 服务不存在均返回 eNo
    {
        YomkResponse response = YOMK_SERVER_INFO_FUNCTION("/DemoService/not_exist");
        if (response.m_status != YomkResponse::eNo)
        {
            YOMK_ERROR_TAG("main", "SERVER_INFO_FUNCTION not found function check failed.");
            ++failed;
        }
        response = YOMK_SERVER_INFO_FUNCTION("/NoService/func");
        if (response.m_status != YomkResponse::eNo)
        {
            YOMK_ERROR_TAG("main", "SERVER_INFO_FUNCTION not found service check failed.");
            ++failed;
        }
    }

    // 6. 全量 dump：含服务名行与缩进函数行
    {
        YomkResponse response = YOMK_SERVER_INFO_ALL();
        std::vector<std::string> lines;
        if (response.m_status != YomkResponse::eOk || !unpackLines(response, lines) ||
            !hasLine(lines, "/DemoService") || !hasLine(lines, "  /DemoService/with_type [String]"))
        {
            YOMK_ERROR_TAG("main", "SERVER_INFO_ALL check failed.");
            ++failed;
        }
        else
        {
            YOMK_INFO_TAG("main", "SERVER_INFO_ALL ok, size: ", lines.size());
            for (auto &line : lines)
            {
                YOMK_INFO_TAG("main", line);
            }
        }
    }

    // 7. 覆盖安装：先三参声明类型再两参覆盖，残留类型元数据应被清除
    {
        YomkResponse response = YOMK_SERVER_INFO_FUNCTIONS("/DemoService");
        std::vector<std::string> lines;
        if (response.m_status != YomkResponse::eOk || !unpackLines(response, lines) ||
            !hasLine(lines, "/reinstall") || hasLine(lines, "/reinstall [String]"))
        {
            YOMK_ERROR_TAG("main", "SERVER_INFO_FUNCTIONS reinstall check failed.");
            ++failed;
        }
        else
        {
            YOMK_INFO_TAG("main", "SERVER_INFO_FUNCTIONS reinstall ok, no residual type metadata.");
        }

        response = YOMK_SERVER_INFO_FUNCTION("/DemoService/reinstall");
        if (response.m_status != YomkResponse::eOk || !response.m_msg.empty())
        {
            YOMK_ERROR_TAG("main", "SERVER_INFO_FUNCTION reinstall check failed, msg: ", response.m_msg);
            ++failed;
        }
        else
        {
            YOMK_INFO_TAG("main", "SERVER_INFO_FUNCTION reinstall ok, msg is empty.");
        }
    }

    if (failed > 0)
    {
        YOMK_ERROR_TAG("main", "TestYomkServerInfo failed, count: ", failed);
        return 1;
    }
    YOMK_INFO_TAG("main", "TestYomkServerInfo all check passed.");
    return 0;
}
