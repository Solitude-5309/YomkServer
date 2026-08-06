/**
 * @file TestYomkLogger.cpp
 * @brief YomkLogger 日志系统示例
 *
 * 演示内容：
 * 1. 控制台日志（INFO/WARN/ERROR/DEBUG）
 * 2. 日志级别控制（开启/关闭特定级别）
 * 3. 自定义日志代理
 * 4. 文件日志（创建、写入、刷新）
 * 5. 自定义 Tag 日志
 *
 * Logger 特性：
 * - 多级别：DEBUG < INFO < WARN < ERROR
 * - 多输出：控制台 + 文件
 * - 可配置：动态开关各级别
 * - 可扩展：自定义日志代理函数
 */

#include <iostream>
#include "YomkAPI.h"

#include <filesystem>
namespace fs = std::filesystem;

/**
 * @brief 自定义控制台日志代理函数
 *
 * 当设置日志代理后，所有控制台日志都会先经过此函数
 * 可以自定义日志格式、输出到其他地方等
 *
 * @param log 日志对象，包含级别、内容、标签等信息
 * @return true 继续传递给默认输出
 * @return false 停止传递（日志不再输出到控制台）
 */
bool consoleLogProxy(const yomk::Log &log)
{
    // 根据日志级别自定义输出格式
    switch (log.m_level)
    {
    case yomk::Log::eInfo:
        std::cout << "[LogProxy] [INFO ] " << "[" << log.m_logger << "] " << log.m_log << std::endl;
        break;
    case yomk::Log::eWarn:
        std::cout << "[LogProxy] [WARN ] " << "[" << log.m_logger << "] " << log.m_log << std::endl;
        break;
    case yomk::Log::eError:
        std::cout << "[LogProxy] [ERROR] " << "[" << log.m_logger << "] " << log.m_log << std::endl;
        break;
    case yomk::Log::eDebug:
        std::cout << "[LogProxy] [DEBUG] " << "[" << log.m_logger << "] " << log.m_log << std::endl;
        break;
    default:
        break;
    }
    // 返回true表示继续传递日志，返回false表示停止传递日志
    return true; // 这里返回 false，表示不再传递给默认输出
}

/**
 * @brief 程序入口
 *
 * 演示日志系统的完整使用：
 * 1. 关闭所有日志级别
 * 2. 开启所有日志级别
 * 3. 设置自定义日志代理
 * 4. 使用不同 Tag 输出日志
 * 5. 创建和写入文件日志
 */
int main(int argc, char *argv[])
{
    // 初始化框架
    YOMK_INIT();

    // 获取可执行文件路径，用于构造日志文件目录
    fs::path exePath = fs::canonical(argv[0]);
    fs::path logDir = exePath.parent_path().parent_path() / "Test" / "YomkServer" / "YomkLog";
    YOMK_DEBUG_TAG("main", "Log dir: ", logDir);

    /**
     * 步骤1：关闭所有控制台日志级别
     *
     * 用于演示日志级别控制
     * 关闭后，对应级别的日志不会输出
     */
    // 关闭控制台INFO日志
    YOMK_OFF_CONSOLE_LOG_INFO();
    // 关闭控制台WARN日志
    YOMK_OFF_CONSOLE_LOG_WARN();
    // 关闭控制台ERROR日志
    YOMK_OFF_CONSOLE_LOG_ERROR();
    // 关闭控制台DEBUG日志
    YOMK_OFF_CONSOLE_LOG_DEBUG();

    /**
     * 步骤2：尝试输出日志（此时全部被禁用）
     *
     * 由于所有级别都已关闭，这些日志不会输出到控制台
     */
    YomkResponse response;
    response = YOMK_INFO("test", " console log info. ", 1);   // 不会输出
    response = YOMK_WARN("test", " console log warn. ", 2);   // 不会输出
    response = YOMK_ERROR("test", " console log error. ", 3); // 不会输出
    response = YOMK_DEBUG("test", " console log debug. ", 4); // 不会输出

    /**
     * 步骤3：开启所有控制台日志级别
     *
     * 重新启用各级别日志输出
     */
    // 开启控制台INFO日志
    YOMK_ON_CONSOLE_LOG_INFO();
    // 开启控制台WARN日志
    YOMK_ON_CONSOLE_LOG_WARN();
    // 开启控制台ERROR日志
    YOMK_ON_CONSOLE_LOG_ERROR();
    // 开启控制台DEBUG日志
    YOMK_ON_CONSOLE_LOG_DEBUG();

    /**
     * 步骤4：设置自定义日志代理
     *
     * 设置后，所有日志会先经过 consoleLogProxy 函数
     * 可以在函数中自定义输出格式
     */
    response = YOMK_SET_CONSOLE_LOG_PROXY(consoleLogProxy);

    /**
     * 步骤5：使用默认 Tag 输出日志
     *
     * 默认 Tag 为 "MainLogger"
     * 日志会通过自定义代理函数输出
     */
    response = YOMK_INFO("test", " console log info. ", 5);
    response = YOMK_WARN("test", " console log warn. ", 6);
    response = YOMK_ERROR("test", " console log error. ", 7);
    response = YOMK_DEBUG("test", " console log debug. ", 8);

    /**
     * 步骤6：使用自定义 Tag 输出日志
     *
     * YOMK_INFO_TAG / YOMK_WARN_TAG 等宏允许指定自定义 Tag
     * Tag 会显示在日志中，便于区分不同模块的日志
     */
    response = YOMK_INFO_TAG("new_console_logger", "test", " new_console_logger log info. ", 1);
    response = YOMK_WARN_TAG("new_console_logger", "test", " new_console_logger log warn. ", 2);
    response = YOMK_ERROR_TAG("new_console_logger", "test", " new_console_logger log error. ", 3);
    response = YOMK_DEBUG_TAG("new_console_logger", "test", " new_console_logger log debug. ", 4);

    /**
     * 步骤7：创建文件日志
     *
     * YOMK_FILE_LOG_CREATE:
     * - 参数1: 日志目录路径
     * - 参数2: 日志文件名（不含扩展名）
     *
     * 创建后可使用 YOMK_FILE_INFO 等宏写入日志
     */
    response = YOMK_FILE_LOG_CREATE(logDir.string(), "new_file_logger");

    /**
     * 步骤8：写入文件日志（默认 Tag）
     *
     * YOMK_FILE_INFO / YOMK_FILE_WARN 等宏用于写入文件日志
     * 默认 Tag 为 "MainLogger"
     */
    response = YOMK_FILE_INFO("new_file_logger", "test", " new_file_logger log info. ", 1);
    response = YOMK_FILE_WARN("new_file_logger", "test", " new_file_logger log warn. ", 2);
    response = YOMK_FILE_ERROR("new_file_logger", "test", " new_file_logger log error. ", 3);
    response = YOMK_FILE_DEBUG("new_file_logger", "test", " new_file_logger log debug. ", 4);

    /**
     * 步骤9：写入文件日志（自定义 Tag）
     *
     * YOMK_FILE_INFO_TAG 等宏允许指定自定义 Tag
     * Tag 会包含在日志内容中
     */
    response = YOMK_FILE_INFO_TAG("new_file_logger", "TestLogger", "test", " new_file_logger log info. ", 1);
    response = YOMK_FILE_WARN_TAG("new_file_logger", "TestLogger", "test", " new_file_logger log warn. ", 2);
    response = YOMK_FILE_ERROR_TAG("new_file_logger", "TestLogger", "test", " new_file_logger log error. ", 3);
    response = YOMK_FILE_DEBUG_TAG("new_file_logger", "TestLogger", "test", " new_file_logger log debug. ", 4);

    /**
     * 步骤10：刷新文件日志到磁盘
     *
     * YOMK_FILE_LOG_WRITE 将缓冲区中的日志写入磁盘
     * 建议：
     * - 程序退出前调用
     * - 重要日志写入后立即调用
     */
    response = YOMK_FILE_LOG_WRITE("new_file_logger");

    YOMK_DEBUG_TAG("main", "test YomkLogger completed, any key to continue...");

    getchar();

    return 0;
}
