/**
 * @file TestYomkLoggerInfo.cpp
 * @brief YomkLogger 模块内省接口示例
 *
 * 演示内容：
 * 1. 日志器列表 / 单日志器元信息 / 全量 dump 三个内省接口
 * 2. 控制台日志器按需自动创建后出现在内省结果中
 * 3. 控制台级别开关与代理状态在全量 dump 首行联动展示
 * 4. YomkLogger 既有功能函数补齐类型名后，服务器层内省可见期望类型
 */

#include <iostream>
#include <algorithm>
#include "YomkAPI.h"

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
    // 初始化框架（自动启动内置服务，含 /YomkLogger，构造时预置 MainLogger）
    YOMK_INIT();

    int failed = 0;

    // 准备数据：创建一个文件日志器；直接以固定名发一条控制台日志，触发控制台日志器按需自动创建
    YomkResponse response = YOMK_FILE_LOG_CREATE("/tmp", "info_logger");
    if (response.m_status != YomkResponse::eOk)
    {
        YOMK_ERROR_TAG("main", "create file logger info_logger failed: ", response.m_msg);
        ++failed;
    }
    // 直接调用底层接口保持日志器名为 auto_logger（YOMK_INFO_TAG 宏会追加行号后缀）
    response = YomkAPI::CONSOLE_LOG_INFO_TAG("auto_logger", "trigger auto create console logger");
    if (response.m_status != YomkResponse::eOk)
    {
        YOMK_ERROR_TAG("main", "console log to auto_logger failed: ", response.m_msg);
        ++failed;
    }

    // 1. 日志器列表：控制台在前（含预置 MainLogger 与自动创建的 auto_logger），文件日志器在后（含 dir）
    {
        YomkResponse resp = YOMK_LOGGER_INFO_LOGGERS();
        std::vector<std::string> lines;
        if (resp.m_status != YomkResponse::eOk || !unpackLines(resp, lines) ||
            !hasLine(lines, "MainLogger [console]") || !hasLine(lines, "auto_logger [console]") ||
            !hasLine(lines, "info_logger [file] dir:/tmp"))
        {
            YOMK_ERROR_TAG("main", "LOGGER_INFO_LOGGERS check failed.");
            ++failed;
        }
        else
        {
            YOMK_INFO_TAG("main", "LOGGER_INFO_LOGGERS ok, size: ", lines.size());
        }
    }

    // 2. 单日志器元信息：控制台无 dir，文件含 dir，未注册返回 eNo
    {
        YomkResponse resp = YOMK_LOGGER_INFO_LOGGER("MainLogger");
        if (resp.m_status != YomkResponse::eOk || resp.m_msg != "MainLogger [console]")
        {
            YOMK_ERROR_TAG("main", "LOGGER_INFO_LOGGER MainLogger check failed, msg: ", resp.m_msg);
            ++failed;
        }
        else
        {
            YOMK_INFO_TAG("main", "LOGGER_INFO_LOGGER MainLogger ok, msg: ", resp.m_msg);
        }

        resp = YOMK_LOGGER_INFO_LOGGER("info_logger");
        if (resp.m_status != YomkResponse::eOk || resp.m_msg != "info_logger [file] dir:/tmp")
        {
            YOMK_ERROR_TAG("main", "LOGGER_INFO_LOGGER info_logger check failed, msg: ", resp.m_msg);
            ++failed;
        }

        resp = YOMK_LOGGER_INFO_LOGGER("not_exist");
        if (resp.m_status != YomkResponse::eNo)
        {
            YOMK_ERROR_TAG("main", "LOGGER_INFO_LOGGER not_exist check failed.");
            ++failed;
        }
    }

    // 3. 全量 dump：首行控制台级别开关与代理状态，其余为日志器行，行数 == 1 + 日志器个数
    {
        YomkResponse resp = YOMK_LOGGER_INFO_LOGGERS();
        std::vector<std::string> loggerLines;
        if (resp.m_status != YomkResponse::eOk || !unpackLines(resp, loggerLines))
        {
            YOMK_ERROR_TAG("main", "LOGGER_INFO_LOGGERS for size check failed.");
            ++failed;
        }

        resp = YOMK_LOGGER_INFO_ALL();
        std::vector<std::string> lines;
        if (resp.m_status != YomkResponse::eOk || !unpackLines(resp, lines) ||
            lines.empty() ||
            lines[0] != "console:debug:on info:on warn:on error:on proxy:off" ||
            lines.size() != 1 + loggerLines.size())
        {
            YOMK_ERROR_TAG("main", "LOGGER_INFO_ALL check failed.");
            ++failed;
        }
        else
        {
            YOMK_INFO_TAG("main", "LOGGER_INFO_ALL ok:");
            for (auto &line : lines)
            {
                YOMK_INFO_TAG("main", line);
            }
        }
    }

    // 4. 状态联动：关闭 debug 级别后首行 debug:off，恢复后 debug:on
    {
        YomkResponse resp = YOMK_OFF_CONSOLE_LOG_DEBUG();
        if (resp.m_status != YomkResponse::eOk)
        {
            YOMK_ERROR_TAG("main", "OFF_CONSOLE_LOG_DEBUG failed: ", resp.m_msg);
            ++failed;
        }

        resp = YOMK_LOGGER_INFO_ALL();
        std::vector<std::string> lines;
        if (resp.m_status != YomkResponse::eOk || !unpackLines(resp, lines) ||
            lines.empty() || lines[0].find("debug:off") == std::string::npos)
        {
            YOMK_ERROR_TAG("main", "LOGGER_INFO_ALL should show debug:off after OFF_CONSOLE_LOG_DEBUG.");
            ++failed;
        }
        else
        {
            YOMK_INFO_TAG("main", "LOGGER_INFO_ALL after OFF_CONSOLE_LOG_DEBUG, head: ", lines[0]);
        }

        resp = YOMK_ON_CONSOLE_LOG_DEBUG();
        if (resp.m_status != YomkResponse::eOk)
        {
            YOMK_ERROR_TAG("main", "ON_CONSOLE_LOG_DEBUG failed: ", resp.m_msg);
            ++failed;
        }

        resp = YOMK_LOGGER_INFO_ALL();
        if (resp.m_status != YomkResponse::eOk || !unpackLines(resp, lines) ||
            lines.empty() || lines[0].find("debug:on") == std::string::npos)
        {
            YOMK_ERROR_TAG("main", "LOGGER_INFO_ALL should show debug:on after ON_CONSOLE_LOG_DEBUG.");
            ++failed;
        }
        else
        {
            YOMK_INFO_TAG("main", "LOGGER_INFO_ALL after ON_CONSOLE_LOG_DEBUG, head: ", lines[0]);
        }
    }

    // 5. 服务器层联动：/YomkLogger 既有功能函数与内省接口的期望类型在服务器层内省可见
    {
        YomkResponse resp = YOMK_SERVER_INFO_FUNCTIONS("/YomkLogger");
        std::vector<std::string> lines;
        if (resp.m_status != YomkResponse::eOk || !unpackLines(resp, lines) ||
            !hasLine(lines, "/console_log [Log]") || !hasLine(lines, "/create_file_logger [LogFile]") ||
            !hasLine(lines, "/write_file_log [String]") || !hasLine(lines, "/logger [String]"))
        {
            YOMK_ERROR_TAG("main", "SERVER_INFO_FUNCTIONS /YomkLogger check failed.");
            ++failed;
        }
        else
        {
            YOMK_INFO_TAG("main", "SERVER_INFO_FUNCTIONS /YomkLogger ok: /console_log [Log], /create_file_logger [LogFile], /write_file_log [String], /logger [String]");
        }

        resp = YOMK_SERVER_INFO_SERVICES();
        if (resp.m_status != YomkResponse::eOk || !unpackLines(resp, lines) ||
            !hasLine(lines, "/YomkLogger"))
        {
            YOMK_ERROR_TAG("main", "SERVER_INFO_SERVICES should contain /YomkLogger.");
            ++failed;
        }
        else
        {
            YOMK_INFO_TAG("main", "SERVER_INFO_SERVICES contains /YomkLogger.");
        }
    }

    if (failed > 0)
    {
        YOMK_ERROR_TAG("main", "TestYomkLoggerInfo failed, count: ", failed);
        return 1;
    }
    YOMK_INFO_TAG("main", "TestYomkLoggerInfo all check passed.");
    return 0;
}
