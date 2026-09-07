/**
 * @file ExampleYomkLogger.cpp
 * @brief YomkLogger 日志系统示例
 *
 * 演示内容：
 * 1. 控制台日志（INFO/WARN/ERROR/DEBUG）
 * 2. 日志级别控制（开启/关闭特定级别）
 * 3. 自定义日志代理
 * 4. 文件日志（创建、写入、刷新）
 * 5. 自定义 Tag 日志
 * 6. 日志器内省（清单 / 单查 / 全量 dump）
 * 7. 卸载日志代理（恢复框架默认输出）
 * 8. 删除日志器（console/file 两表）
 *
 * Logger 特性：
 * - 多级别：DEBUG < INFO < WARN < ERROR
 * - 多输出：控制台 + 文件
 * - 可配置：动态开关各级别
 * - 可扩展：自定义日志代理函数（可安装亦可卸载）
 * - 可内省：只读端点查看日志器清单与控制台级别/代理状态
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
    // 返回 true 表示继续传递日志，返回 false 表示停止传递日志
    return true; // 本示例放行：代理先打印一行自定义格式，框架随后仍走默认输出
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
 * 6. 内省日志器清单与控制台级别/代理状态
 * 7. 卸载日志代理并验证恢复默认输出
 * 8. 删除日志器
 */
int main(int argc, char *argv[])
{
    // 初始化框架
    YOMK_INIT();

    // 测试 YOMK_VERSION：获取并输出框架版本号（对应 project(Yomk VERSION x.x.x) 定义的 VERSION）
    YOMK_INFO_TAG("main", "YomkServer version: ", YOMK_VERSION);

    // 获取可执行文件路径，用于构造日志文件目录
    fs::path exePath = fs::canonical(argv[0]);
    fs::path logDir = exePath.parent_path() / "YomkLog";
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
    response = YOMK_FILE_INFO_TAG("new_file_logger", "ExampleLogger", "example", " new_file_logger log info. ", 1);
    response = YOMK_FILE_WARN_TAG("new_file_logger", "ExampleLogger", "example", " new_file_logger log warn. ", 2);
    response = YOMK_FILE_ERROR_TAG("new_file_logger", "ExampleLogger", "example", " new_file_logger log error. ", 3);
    response = YOMK_FILE_DEBUG_TAG("new_file_logger", "ExampleLogger", "example", " new_file_logger log debug. ", 4);

    /**
     * 步骤10：刷新文件日志到磁盘
     *
     * YOMK_FILE_LOG_WRITE 将缓冲区中的日志写入磁盘
     * 建议：
     * - 程序退出前调用
     * - 重要日志写入后立即调用
     */
    response = YOMK_FILE_LOG_WRITE("new_file_logger");

    /**
     * 步骤11：日志器内省（只读调试端点，不改任何状态）
     *
     * YOMK_LOGGER_INFO_LOGGERS()：日志器清单，返回 StringArray，
     *   控制台行格式 "name [console]"，文件行格式 "name [file] dir:路径"
     * YOMK_LOGGER_INFO_LOGGER(name)：单查，命中 eOk 且 msg 即元信息行，未注册 eNo
     * YOMK_LOGGER_INFO_ALL()：全量 dump，首行是控制台级别与代理状态行
     *   "console:debug:on|off info:... warn:... error:... proxy:on|off"，其余为日志器行
     *
     * 清单按段有序：console 段字典序在前、file 段字典序在后（两表底层为 std::map，中序遍历
     * 天然字典序；LG6 曾试 unordered_map + 锁外分段排序，实测内省端点退化约 4 倍已回退）
     */
    auto dumpLines = [](const char *title, const YomkResponse &resp)
    {
        std::cout << "---- " << title << " (status=" << resp.m_status
                  << ", msg=" << resp.m_msg << ") ----" << std::endl;
        // 解包 StringArray：YomkUnPackPkg 失败不 return，指针为空需手动判空
        YomkUnPackPkg(resp.m_data, StringArray, arr);
        if (!arr)
        {
            std::cout << "  (no data)" << std::endl;
            return;
        }
        for (const auto &line : arr->d)
        {
            std::cout << "  " << line << std::endl;
        }
    };

    dumpLines("YOMK_LOGGER_INFO_LOGGERS", YOMK_LOGGER_INFO_LOGGERS());

    // 单查：文件日志器命中（eOk=0）；从未创建的名字返回 eNo=1（not-found 框架惯例）
    response = YOMK_LOGGER_INFO_LOGGER("new_file_logger");
    std::cout << "---- YOMK_LOGGER_INFO_LOGGER(\"new_file_logger\") ----" << std::endl;
    std::cout << "  status=" << response.m_status << ", msg=" << response.m_msg << std::endl;
    response = YOMK_LOGGER_INFO_LOGGER("never_created_logger");
    std::cout << "---- YOMK_LOGGER_INFO_LOGGER(\"never_created_logger\") ----" << std::endl;
    std::cout << "  status=" << response.m_status << ", msg=" << response.m_msg
              << "（未注册 → eNo=1）" << std::endl;

    // 全量 dump：此时步骤4 安装的代理仍在，首行应显示 proxy:on
    dumpLines("YOMK_LOGGER_INFO_ALL", YOMK_LOGGER_INFO_ALL());

    /**
     * 步骤12：卸载日志代理（LG6/P4-e）
     *
     * 传 nullptr（或空 std::function）即卸载代理，恢复框架默认的控制台输出。
     * 此前代理一旦安装便进程内不可撤销（传空回调仍报 proxy:on 却实际不生效）；
     * 卸载后内省首行的 proxy 由 on 回到 off，日志不再带 "[LogProxy]" 前缀
     */
    response = YOMK_SET_CONSOLE_LOG_PROXY(nullptr);
    std::cout << "---- 卸载日志代理 status=" << response.m_status << " ----" << std::endl;
    // 卸载后再写一条：输出为框架默认格式（对比步骤5-6 的 [LogProxy] 前缀）
    response = YOMK_INFO("test", " console log info after proxy unloaded. ", 12);
    dumpLines("YOMK_LOGGER_INFO_ALL (proxy unloaded)", YOMK_LOGGER_INFO_ALL());

    /**
     * 步骤13：删除日志器
     *
     * YOMK_FILE_LOG_DELETE(name)（LG6 由 YOMK_LOGGER_DELETE 改名）：单端点同时清理
     * console/file 两张表，命中数写入 msg "deleted console:c file:f"；
     * 空名 eInvalid=-1，两表均未命中 eNo=1；
     * 文件日志器移除时由 ~FileLogger 自动落盘（未 flush 的缓冲内容不丢），
     * 但不删除磁盘上的 .log 文件（数据保全，清理归调用方）
     */
    response = YOMK_FILE_LOG_DELETE("new_file_logger");
    std::cout << "---- 删除 new_file_logger: status=" << response.m_status
              << ", msg=" << response.m_msg << " ----" << std::endl;

    response = YOMK_FILE_LOG_DELETE("never_created_logger");
    std::cout << "---- 删除 never_created_logger: status=" << response.m_status
              << ", msg=" << response.m_msg << "（两表均未命中 → eNo=1） ----" << std::endl;

    // "MainLogger" 无特殊保护：可删；删除后下次写控制台日志按双检锁惰性重建。
    // 注意 YOMK_INFO 系列宏把 "tag:行号" 作为 logger 名，故步骤5-6 产生的控制台日志器
    // 名形如 "MainLogger:<行号>"、"new_console_logger:<行号>"，需删除时先经步骤11 的内省
    // 取到确切名字，不能写硬编码行号
    response = YOMK_FILE_LOG_DELETE("MainLogger");
    std::cout << "---- 删除 MainLogger: status=" << response.m_status
              << ", msg=" << response.m_msg << " ----" << std::endl;

    dumpLines("YOMK_LOGGER_INFO_LOGGERS (after delete)", YOMK_LOGGER_INFO_LOGGERS());

    YOMK_DEBUG_TAG("main", "example YomkLogger completed, any key to continue...");

    getchar();

    return 0;
}
