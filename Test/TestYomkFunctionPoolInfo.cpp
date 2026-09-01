/**
 * @file TestYomkFunctionPoolInfo.cpp
 * @brief YomkFunctionPool 模块内省接口示例
 *
 * 演示内容：
 * 1. 注册函数名列表 / 单函数存在性查询 / 全量 dump 三个内省接口
 * 2. 三参注册宏声明期望消息类型（仅作内省元数据），两参旧写法零改动
 * 3. 生命周期边界：注销后函数不再出现在内省结果中
 * 4. YomkFunctionPool 既有功能函数补齐类型名后，服务器层内省可见期望类型
 */

#include <iostream>
#include <algorithm>
#include "YomkAPI.h"

// 池中函数：简单返回 eOk（内省仅关心注册表，不涉及调用行为）
YomkResponse funcA(YomkPkgPtr pkg)
{
    return YomkResponse(YomkResponse::eOk, "func_a called");
}

YomkResponse funcB(YomkPkgPtr pkg)
{
    return YomkResponse(YomkResponse::eOk, "func_b called");
}

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
    // 初始化框架（自动启动内置服务，含 /YomkFunctionPool）
    YOMK_INIT();

    int failed = 0;

    // 准备数据：注册两个函数，func_a 用三参宏声明期望类型，func_b 保持两参旧写法（零改动直接编译）
    YomkResponse response = YOMK_FUNCTIONPOOL_REGISTER("func_a", funcA, String);
    if (response.m_status != YomkResponse::eOk)
    {
        YOMK_ERROR_TAG("main", "register func_a failed: ", response.m_msg);
        ++failed;
    }
    response = YOMK_FUNCTIONPOOL_REGISTER("func_b", funcB);
    if (response.m_status != YomkResponse::eOk)
    {
        YOMK_ERROR_TAG("main", "register func_b failed: ", response.m_msg);
        ++failed;
    }

    // 1. 注册函数名列表
    {
        YomkResponse resp = YOMK_FUNCTIONPOOL_INFO_NAMES();
        std::vector<std::string> lines;
        if (resp.m_status != YomkResponse::eOk || !unpackLines(resp, lines) ||
            !hasLine(lines, "func_a") || !hasLine(lines, "func_b"))
        {
            YOMK_ERROR_TAG("main", "FUNCTIONPOOL_INFO_NAMES check failed.");
            ++failed;
        }
        else
        {
            YOMK_INFO_TAG("main", "FUNCTIONPOOL_INFO_NAMES ok, size: ", lines.size());
        }
    }

    // 2. 单函数存在性查询：命中 msg 为 funcName [类型名]，未声明类型无括号后缀，未注册返回 eNo
    {
        YomkResponse resp = YOMK_FUNCTIONPOOL_INFO_NAME("func_a");
        if (resp.m_status != YomkResponse::eOk || resp.m_msg != "func_a [String]")
        {
            YOMK_ERROR_TAG("main", "FUNCTIONPOOL_INFO_NAME func_a check failed, msg: ", resp.m_msg);
            ++failed;
        }
        else
        {
            YOMK_INFO_TAG("main", "FUNCTIONPOOL_INFO_NAME func_a ok, msg: ", resp.m_msg);
        }

        resp = YOMK_FUNCTIONPOOL_INFO_NAME("func_b");
        if (resp.m_status != YomkResponse::eOk || resp.m_msg != "func_b")
        {
            YOMK_ERROR_TAG("main", "FUNCTIONPOOL_INFO_NAME func_b check failed, msg: ", resp.m_msg);
            ++failed;
        }

        resp = YOMK_FUNCTIONPOOL_INFO_NAME("not_exist");
        if (resp.m_status != YomkResponse::eNo)
        {
            YOMK_ERROR_TAG("main", "FUNCTIONPOOL_INFO_NAME not_exist check failed.");
            ++failed;
        }
    }

    // 3. 全量 dump：首行 functions:N + 每行 funcName [类型名]
    {
        YomkResponse resp = YOMK_FUNCTIONPOOL_INFO_ALL();
        std::vector<std::string> lines;
        if (resp.m_status != YomkResponse::eOk || !unpackLines(resp, lines) ||
            lines.size() != 3 || !hasLine(lines, "functions:2") ||
            !hasLine(lines, "func_a [String]") || !hasLine(lines, "func_b"))
        {
            YOMK_ERROR_TAG("main", "FUNCTIONPOOL_INFO_ALL check failed.");
            ++failed;
        }
        else
        {
            YOMK_INFO_TAG("main", "FUNCTIONPOOL_INFO_ALL ok:");
            for (auto &line : lines)
            {
                YOMK_INFO_TAG("main", line);
            }
        }
    }

    // 4. 生命周期边界：注销后不再出现在内省结果中
    {
        YomkResponse resp = YOMK_FUNCTIONPOOL_UNREGISTER("func_b");
        if (resp.m_status != YomkResponse::eOk)
        {
            YOMK_ERROR_TAG("main", "FUNCTIONPOOL_UNREGISTER func_b failed: ", resp.m_msg);
            ++failed;
        }

        resp = YOMK_FUNCTIONPOOL_INFO_NAMES();
        std::vector<std::string> lines;
        if (resp.m_status != YomkResponse::eOk || !unpackLines(resp, lines) || hasLine(lines, "func_b"))
        {
            YOMK_ERROR_TAG("main", "func_b should not appear in FUNCTIONPOOL_INFO_NAMES.");
            ++failed;
        }

        resp = YOMK_FUNCTIONPOOL_INFO_NAME("func_b");
        if (resp.m_status != YomkResponse::eNo)
        {
            YOMK_ERROR_TAG("main", "FUNCTIONPOOL_INFO_NAME func_b after unregister should be eNo.");
            ++failed;
        }

        resp = YOMK_FUNCTIONPOOL_INFO_ALL();
        if (resp.m_status != YomkResponse::eOk || !unpackLines(resp, lines) || !hasLine(lines, "functions:1"))
        {
            YOMK_ERROR_TAG("main", "FUNCTIONPOOL_INFO_ALL after unregister should be functions:1.");
            ++failed;
        }
        else
        {
            YOMK_INFO_TAG("main", "FUNCTIONPOOL_UNREGISTER func_b ok, removed from introspection.");
        }
    }

    // 5. 服务器层联动：/YomkFunctionPool 既有功能函数的期望类型在服务器层内省可见
    {
        YomkResponse resp = YOMK_SERVER_INFO_FUNCTIONS("/YomkFunctionPool");
        std::vector<std::string> lines;
        if (resp.m_status != YomkResponse::eOk || !unpackLines(resp, lines) ||
            !hasLine(lines, "/register [Function]") || !hasLine(lines, "/unregister [String]") ||
            !hasLine(lines, "/call [CallFunction]") || !hasLine(lines, "/name [String]"))
        {
            YOMK_ERROR_TAG("main", "SERVER_INFO_FUNCTIONS /YomkFunctionPool check failed.");
            ++failed;
        }
        else
        {
            YOMK_INFO_TAG("main", "SERVER_INFO_FUNCTIONS /YomkFunctionPool ok: /register [Function], /unregister [String], /call [CallFunction], /name [String]");
        }

        // 服务名归一化验证：注册名带 / 前缀
        resp = YOMK_SERVER_INFO_SERVICES();
        if (resp.m_status != YomkResponse::eOk || !unpackLines(resp, lines) ||
            !hasLine(lines, "/YomkFunctionPool"))
        {
            YOMK_ERROR_TAG("main", "SERVER_INFO_SERVICES should contain /YomkFunctionPool.");
            ++failed;
        }
        else
        {
            YOMK_INFO_TAG("main", "SERVER_INFO_SERVICES contains /YomkFunctionPool.");
        }
    }

    if (failed > 0)
    {
        YOMK_ERROR_TAG("main", "TestYomkFunctionPoolInfo failed, count: ", failed);
        return 1;
    }
    YOMK_INFO_TAG("main", "TestYomkFunctionPoolInfo all check passed.");
    return 0;
}
