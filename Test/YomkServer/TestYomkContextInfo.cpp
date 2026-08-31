/**
 * @file TestYomkContextInfo.cpp
 * @brief YomkContext 模块内省接口示例
 *
 * 演示内容：
 * 1. Context key 列表 / 单 key 元信息 / 全量 dump 三个内省接口
 * 2. YomkContext 既有功能函数补齐类型名后，服务器层内省可见期望类型
 * 3. 空值边界：创建值为空的 Context 被拒绝，且不出现在内省结果中
 */

#include <iostream>
#include <algorithm>
#include "YomkAPI.h"

// 定点引入所需类型，避免在头文件中 using namespace（第十一轮）
using yomk::ContextChecker;

// checker：接受所有设置（内省仅关心 checker 是否已设置）
ContextChecker::ECheckStatus infoChecker(const yomk::Context &ctx)
{
    return ContextChecker::eAccept;
}

// monitor：空实现（内省仅关心 monitor 个数与异步个数）
void infoMonitor(const yomk::Context &ctx)
{
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

// 检查 msg 是否包含指定子串
static bool msgContains(const YomkResponse &response, const std::string &sub)
{
    return response.m_msg.find(sub) != std::string::npos;
}

int main(int argc, char *argv[])
{
    // 初始化框架（自动启动内置服务，含 /YomkContext）
    YOMK_INIT();

    int failed = 0;

    // 准备数据：两个 Context，str_key 额外设置 checker 与一个异步 monitor
    YomkResponse response = YOMK_CONTEXT_CREATE("str_key", YomkMkPtr(String, "v1"));
    if (response.m_status != YomkResponse::eOk)
    {
        YOMK_ERROR_TAG("main", "create str_key failed: ", response.m_msg);
        ++failed;
    }
    response = YOMK_CONTEXT_CREATE("int_key", YomkMkPtr(Int32, 42));
    if (response.m_status != YomkResponse::eOk)
    {
        YOMK_ERROR_TAG("main", "create int_key failed: ", response.m_msg);
        ++failed;
    }
    YOMK_CONTEXT_SET_CHECKER("str_key", infoChecker);
    YOMK_CONTEXT_SET_MONITOR("str_key", infoMonitor, true);

    // 1. key 列表
    {
        YomkResponse resp = YOMK_CONTEXT_INFO_KEYS();
        std::vector<std::string> lines;
        if (resp.m_status != YomkResponse::eOk || !unpackLines(resp, lines) ||
            !hasLine(lines, "str_key") || !hasLine(lines, "int_key"))
        {
            YOMK_ERROR_TAG("main", "CONTEXT_INFO_KEYS check failed.");
            ++failed;
        }
        else
        {
            YOMK_INFO_TAG("main", "CONTEXT_INFO_KEYS ok, size: ", lines.size());
        }
    }

    // 2. 单 key 元信息：str_key 带 checker 与异步 monitor
    {
        YomkResponse resp = YOMK_CONTEXT_INFO_KEY("str_key");
        if (resp.m_status != YomkResponse::eOk || !msgContains(resp, "[String]") ||
            !msgContains(resp, "checker:on") || !msgContains(resp, "monitors:1(async:1)"))
        {
            YOMK_ERROR_TAG("main", "CONTEXT_INFO_KEY str_key check failed, msg: ", resp.m_msg);
            ++failed;
        }
        else
        {
            YOMK_INFO_TAG("main", "CONTEXT_INFO_KEY str_key ok, msg: ", resp.m_msg);
        }
    }

    // 3. 单 key 元信息：int_key 无 checker、无 monitor
    {
        YomkResponse resp = YOMK_CONTEXT_INFO_KEY("int_key");
        if (resp.m_status != YomkResponse::eOk || !msgContains(resp, "[Int32]") ||
            !msgContains(resp, "checker:off") || !msgContains(resp, "monitors:0(async:0)"))
        {
            YOMK_ERROR_TAG("main", "CONTEXT_INFO_KEY int_key check failed, msg: ", resp.m_msg);
            ++failed;
        }
        else
        {
            YOMK_INFO_TAG("main", "CONTEXT_INFO_KEY int_key ok, msg: ", resp.m_msg);
        }
    }

    // 4. key 不存在返回 eNo
    {
        YomkResponse resp = YOMK_CONTEXT_INFO_KEY("not_exist");
        if (resp.m_status != YomkResponse::eNo)
        {
            YOMK_ERROR_TAG("main", "CONTEXT_INFO_KEY not_exist check failed.");
            ++failed;
        }
    }

    // 5. 全量 dump：两行元信息
    {
        YomkResponse resp = YOMK_CONTEXT_INFO_ALL();
        std::vector<std::string> lines;
        if (resp.m_status != YomkResponse::eOk || !unpackLines(resp, lines) ||
            lines.size() != 2 ||
            !hasLine(lines, "int_key [Int32] checker:off monitors:0(async:0)") ||
            !hasLine(lines, "str_key [String] checker:on monitors:1(async:1)"))
        {
            YOMK_ERROR_TAG("main", "CONTEXT_INFO_ALL check failed.");
            ++failed;
        }
        else
        {
            YOMK_INFO_TAG("main", "CONTEXT_INFO_ALL ok:");
            for (auto &line : lines)
            {
                YOMK_INFO_TAG("main", line);
            }
        }
    }

    // 6. 空值边界：值为空创建被拒绝，且不出现在 key 列表中
    {
        YomkResponse resp = YOMK_CONTEXT_CREATE("null_key", nullptr);
        if (resp.m_status != YomkResponse::eNo)
        {
            YOMK_ERROR_TAG("main", "CONTEXT_CREATE null value check failed, expect eNo.");
            ++failed;
        }
        resp = YOMK_CONTEXT_INFO_KEYS();
        std::vector<std::string> lines;
        if (resp.m_status != YomkResponse::eOk || !unpackLines(resp, lines) || hasLine(lines, "null_key"))
        {
            YOMK_ERROR_TAG("main", "null_key should not appear in CONTEXT_INFO_KEYS.");
            ++failed;
        }
        else
        {
            YOMK_INFO_TAG("main", "CONTEXT_CREATE null value rejected, null_key not in keys.");
        }
    }

    // 7. 服务器层联动：/YomkContext 既有功能函数的期望类型在服务器层内省可见
    {
        YomkResponse resp = YOMK_SERVER_INFO_FUNCTIONS("/YomkContext");
        std::vector<std::string> lines;
        if (resp.m_status != YomkResponse::eOk || !unpackLines(resp, lines) ||
            !hasLine(lines, "/create [Context]") || !hasLine(lines, "/destroy [String]") ||
            !hasLine(lines, "/key [String]") || !hasLine(lines, "/turn_on_checker"))
        {
            YOMK_ERROR_TAG("main", "SERVER_INFO_FUNCTIONS /YomkContext check failed.");
            ++failed;
        }
        else
        {
            YOMK_INFO_TAG("main", "SERVER_INFO_FUNCTIONS /YomkContext ok: /create [Context], /destroy [String], /key [String], /turn_on_checker(no type)");
        }
    }

    if (failed > 0)
    {
        YOMK_ERROR_TAG("main", "TestYomkContextInfo failed, count: ", failed);
        return 1;
    }
    YOMK_INFO_TAG("main", "TestYomkContextInfo all check passed.");
    return 0;
}
