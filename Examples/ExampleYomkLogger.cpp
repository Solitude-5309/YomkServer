/**
 * @file ExampleYomkLogger.cpp
 * @brief YomkLogger 日志系统快速上手示例（面向初学者）
 *
 * 本示例按 8 个步骤演示日志系统的全部用法。每一步先打印横幅说明
 * "接下来做什么、预期看到什么"，再调用 API，随后用框架日志与内省
 * 结果自证行为——只读运行输出即可明白每个 API 调用发生了什么。
 *
 * 步骤总览：
 * 1. 控制台日志四级别（默认 tag）
 * 2. 自定义 Tag（tag 即控制台日志器名）
 * 3. 级别开关（全局，只影响控制台）
 * 4. 控制台日志代理（消费与放行两种返回值）
 * 5. 卸载代理（恢复默认输出）
 * 6. 文件日志（创建、写入、刷盘、回读验证）
 * 7. 内省三件套（清单 / 单查 / 全量状态）
 * 8. 删除日志器（两表清理、磁盘文件保留、惰性重建）
 *
 * 涉及的 API 宏（YomkAPI.h）：
 *   YOMK_VERSION
 *   控制台：YOMK_INFO/WARN/ERROR/DEBUG、YOMK_INFO_TAG/WARN_TAG/ERROR_TAG/DEBUG_TAG
 *   开关：  YOMK_ON/OFF_CONSOLE_LOG_INFO/WARN/ERROR/DEBUG
 *   代理：  YOMK_SET_CONSOLE_LOG_PROXY
 *   文件：  YOMK_FILE_LOG_CREATE、YOMK_FILE_INFO/WARN/ERROR/DEBUG(_TAG)、YOMK_FILE_LOG_WRITE
 *   内省：  YOMK_LOGGER_INFO_LOGGERS、YOMK_LOGGER_INFO_LOGGER、YOMK_LOGGER_INFO_ALL
 *   删除：  YOMK_FILE_LOG_DELETE
 */

#include <fstream>
#include <iostream>

#include <filesystem>
namespace fs = std::filesystem;

#include "YomkAPI.h"

// ---------------------------------------------------------------------------
// 辅助输出：横幅与返回值打印。
// 横幅走 std::cout 而非框架日志：级别关闭/代理拦截时叙事仍然可见，
// 这样"被吞掉的日志"才能被解释。
// ---------------------------------------------------------------------------

static void printStep(int n, const std::string &title, const std::string &explain)
{
    std::cout << "\n====== 步骤" << n << "：" << title << " ======" << std::endl;
    std::cout << ">> " << explain << std::endl;
}

static void printResp(const std::string &prefix, const YomkResponse &resp)
{
    // YomkResponse 三态：eOk=0 成功；eNo=1 不存在或被拒绝；eInvalid=-1 参数无效或未初始化
    std::cout << "[" << prefix << "] status=" << resp.m_status << ", msg=\"" << resp.m_msg << "\""
              << std::endl;
}

// 解包并打印内省返回的 StringArray（YomkLoggerInfoLoggers / All 的返回形态）
static void dumpLines(const std::string &prefix, const YomkResponse &resp)
{
    printResp(prefix, resp);
    YomkUnPackPkg(resp.m_data, StringArray, arr);
    if (!arr)
    {
        std::cout << ">> (no data)" << std::endl;
        return;
    }
    for (const auto &line : arr->d)
    {
        std::cout << ">> | " << line << std::endl;
    }
}

/**
 * @brief 自定义控制台日志代理
 *
 * 框架每写一条控制台日志，都会先调用本函数：
 * - 返回 false：日志被"消费"，框架不再默认输出（可据此接入外部日志库、上报系统等）；
 * - 返回 true ：放行，日志继续走框架默认输出（本代理额外加前缀演示）。
 */
bool consoleLogProxy(const yomk::Log &log)
{
    // 演示"消费"：DEBUG 日志被代理吞掉，不再出现在控制台
    if (log.m_level == yomk::Log::eDebug)
    {
        return false;
    }
    // 演示"放行"：其余级别加自定义前缀后交给框架默认输出
    std::cout << "[LogProxy] " << log.m_log << " (tag=" << log.m_logger << ")" << std::endl;
    return true;
}

int main(int argc, char *argv[])
{
    // 初始化框架（自动启动内置 Logger 服务）
    YOMK_INIT();

    printStep(0, "框架就绪", "YOMK_INIT 已启动内置日志服务；YOMK_VERSION 返回框架版本号。");
    printResp("YOMK_VERSION", YomkResponse(YomkResponse::eOk, YOMK_VERSION));

    // 日志目录：可执行文件同目录下的 YomkLog/（步骤6 创建文件日志器使用）
    fs::path exePath = fs::canonical(argv[0]);
    fs::path logDir = exePath.parent_path() / "YomkLog";

    /**
     * 步骤1：控制台日志四级别（默认 tag）
     *
     * YOMK_INFO/WARN/ERROR/DEBUG 使用默认日志器名 "MainLogger"。
     * 控制台输出格式四段：[时间] [级别] [tag] [行号] 内容，
     * 例如：[2026-09-08 17:54:59.165] [Info ] [MainLogger] [95] hello
     * 宏会自动把 "[行号]" 拼到内容开头，方便定位调用处。
     */
    printStep(1, "控制台日志四级别（默认 tag）",
              "下面 4 条按 DEBUG/INFO/WARN/ERROR 输出，注意级别字段与 [行号] 定位。");
    printResp("YOMK_INFO", YOMK_INFO("hello yomk logger, this is", "info"));
    printResp("YOMK_WARN", YOMK_WARN("low disk space, free=", 1024));
    printResp("YOMK_ERROR", YOMK_ERROR("connect refused, code=", -1));
    printResp("YOMK_DEBUG", YOMK_DEBUG("trace id:", 12345));

    /**
     * 步骤2：自定义 Tag
     *
     * YOMK_INFO_TAG(tag, ...) 的 tag 就是控制台日志器名，
     * 首次使用时框架自动创建同名日志器（无需手动注册）。
     * 用内省 LOGGERS 清单自证：能看到 "MainLogger [console]" 和 "user.service [console]"。
     */
    printStep(2, "自定义 Tag",
              "tag 即控制台日志器名，首次使用自动创建；随后打印日志器清单自证。");
    printResp("YOMK_INFO_TAG", YOMK_INFO_TAG("user.service", "user login, id=", 7));
    printResp("YOMK_WARN_TAG", YOMK_WARN_TAG("user.service", "retry count=", 2));
    printResp("YOMK_ERROR_TAG", YOMK_ERROR_TAG("user.service", "session expired"));
    printResp("YOMK_DEBUG_TAG", YOMK_DEBUG_TAG("user.service", "cache hit, key=token"));
    dumpLines("YOMK_LOGGER_INFO_LOGGERS", YOMK_LOGGER_INFO_LOGGERS());

    /**
     * 步骤3：级别开关（全局，只影响控制台）
     *
     * 4 个 OFF 宏逐级关闭控制台输出——注意级别是框架全局开关，
     * 不区分日志器；关闭后对应的日志调用仍返回 eOk，只是不再打印。
     * 文件日志不受影响（步骤6 会回指这一点）。
     */
    printStep(3, "级别开关（全局，只影响控制台）",
              "先全关：接下来 4 条日志一行都不会出现（调用仍成功返回 eOk）。");
    printResp("OFF_INFO", YOMK_OFF_CONSOLE_LOG_INFO());
    printResp("OFF_WARN", YOMK_OFF_CONSOLE_LOG_WARN());
    printResp("OFF_ERROR", YOMK_OFF_CONSOLE_LOG_ERROR());
    printResp("OFF_DEBUG", YOMK_OFF_CONSOLE_LOG_DEBUG());

    std::cout << ">> 上面一行框架日志都没有？——四个级别已全关，调用本身仍返回 eOk" << std::endl;
    printResp("YOMK_INFO(被关闭)", YOMK_INFO("you cannot see me: info is off"));
    printResp("YOMK_WARN(被关闭)", YOMK_WARN("you cannot see me: warn is off"));
    printResp("YOMK_ERROR(被关闭)", YOMK_ERROR("you cannot see me: error is off"));
    printResp("YOMK_DEBUG(被关闭)", YOMK_DEBUG("you cannot see me: debug is off"));

    // 用内省 ALL 自证开关状态：首行 console:debug:off info:off warn:off error:off proxy:off
    dumpLines("YOMK_LOGGER_INFO_ALL(全关)", YOMK_LOGGER_INFO_ALL());

    printStep(3, "级别开关（续）：恢复", "4 个 ON 宏重新打开，同样的日志立刻出现。");
    printResp("ON_INFO", YOMK_ON_CONSOLE_LOG_INFO());
    printResp("ON_WARN", YOMK_ON_CONSOLE_LOG_WARN());
    printResp("ON_ERROR", YOMK_ON_CONSOLE_LOG_ERROR());
    printResp("ON_DEBUG", YOMK_ON_CONSOLE_LOG_DEBUG());
    printResp("YOMK_INFO(已恢复)", YOMK_INFO("info is back"));
    printResp("YOMK_DEBUG(已恢复)", YOMK_DEBUG("debug is back"));

    /**
     * 步骤4：控制台日志代理（消费与放行）
     *
     * 安装 consoleLogProxy 后所有控制台日志先过代理：
     * - DEBUG 被 return false 消费掉——控制台不再出现；
     * - 其余被加 [LogProxy] 前缀后 return true 放行——正常输出。
     * 内省 ALL 首行 proxy:on 标记代理已安装。
     */
    printStep(4, "控制台日志代理",
              "代理消费 DEBUG（不再输出）、放行其他级别（加 [LogProxy] 前缀）；观察下面 4 条的差异。");
    printResp("SET_PROXY", YOMK_SET_CONSOLE_LOG_PROXY(consoleLogProxy));
    std::cout << ">> DEBUG 这条将被代理吞掉，不会出现；其余 3 条带 [LogProxy] 前缀" << std::endl;
    printResp("YOMK_INFO(代理放行)", YOMK_INFO("passed through proxy"));
    printResp("YOMK_WARN(代理放行)", YOMK_WARN("passed through proxy"));
    printResp("YOMK_ERROR(代理放行)", YOMK_ERROR("passed through proxy"));
    printResp("YOMK_DEBUG(代理消费)", YOMK_DEBUG("consumed by proxy, never printed"));
    dumpLines("YOMK_LOGGER_INFO_ALL(代理开启)", YOMK_LOGGER_INFO_ALL());

    /**
     * 步骤5：卸载代理
     *
     * 传 nullptr 即卸载，恢复框架默认输出格式（无 [LogProxy] 前缀），
     * 内省首行 proxy 回到 off。
     */
    printStep(5, "卸载代理", "传 nullptr 恢复默认输出；对比上一条 [LogProxy] 前缀消失。");
    printResp("SET_PROXY(nullptr)", YOMK_SET_CONSOLE_LOG_PROXY(nullptr));
    printResp("YOMK_INFO(默认输出)", YOMK_INFO("default format again, no proxy prefix"));
    dumpLines("YOMK_LOGGER_INFO_ALL(代理已卸载)", YOMK_LOGGER_INFO_ALL());

    /**
     * 步骤6：文件日志
     *
     * 文件日志器独立于控制台：先 CREATE（目录 + 日志器名），
     * 再用 FILE 系列宏写入内存缓冲，最后 WRITE 显式刷盘到 dir/name.log。
     * 注意：级别开关只管控制台——即使步骤3 全关过，文件日志四级全记。
     */
    printStep(6, "文件日志",
              "创建 app 日志器，写 8 条（默认 tag + 自定义 tag），刷盘后回读文件验证；文件日志不受级别开关影响。");
    printResp("FILE_LOG_CREATE", YOMK_FILE_LOG_CREATE(logDir.string(), "app"));
    // 默认 tag：内容为 "[行号] 内容"
    printResp("YOMK_FILE_INFO", YOMK_FILE_INFO("app", "file log info, order=", 1));
    printResp("YOMK_FILE_WARN", YOMK_FILE_WARN("app", "file log warn, order=", 2));
    printResp("YOMK_FILE_ERROR", YOMK_FILE_ERROR("app", "file log error, order=", 3));
    printResp("YOMK_FILE_DEBUG", YOMK_FILE_DEBUG("app", "file log debug, order=", 4));
    // 自定义 tag：内容为 "[tag] [行号] 内容"，便于在文件里区分模块
    printResp("YOMK_FILE_INFO_TAG", YOMK_FILE_INFO_TAG("app", "biz", "biz event, order=", 5));
    printResp("YOMK_FILE_WARN_TAG", YOMK_FILE_WARN_TAG("app", "biz", "biz warning, order=", 6));
    printResp("YOMK_FILE_ERROR_TAG", YOMK_FILE_ERROR_TAG("app", "biz", "biz failure, order=", 7));
    printResp("YOMK_FILE_DEBUG_TAG", YOMK_FILE_DEBUG_TAG("app", "biz", "biz detail, order=", 8));
    printResp("FILE_LOG_WRITE", YOMK_FILE_LOG_WRITE("app"));

    // 回读 .log 文件：眼见为实，8 条全部在磁盘上
    const fs::path logFilePath = logDir / "app.log";
    std::cout << ">> ---- 回读 " << logFilePath.string() << " ----" << std::endl;
    {
        std::ifstream logFile(logFilePath);
        std::string line;
        while (std::getline(logFile, line))
        {
            std::cout << ">> | " << line << std::endl;
        }
    }

    /**
     * 步骤7：内省三件套
     *
     * LOGGERS：日志器清单（console 段 + file 段，各自按名称字典序）；
     * LOGGER(name)：单查，命中返回元信息行，未注册返回 eNo=1；
     * ALL：全量状态，首行为控制台级别与代理开关。
     */
    printStep(7, "内省三件套",
              "清单看全部、单查看一个、ALL 看总状态；单查一个从未创建的名字演示 eNo 惯例。");
    dumpLines("YOMK_LOGGER_INFO_LOGGERS", YOMK_LOGGER_INFO_LOGGERS());
    printResp("LOGGER_INFO_LOGGER(app)", YOMK_LOGGER_INFO_LOGGER("app"));
    printResp("LOGGER_INFO_LOGGER(ghost)", YOMK_LOGGER_INFO_LOGGER("ghost"));
    std::cout << ">> ghost 未注册 → eNo=1，not-found 是框架统一惯例" << std::endl;
    dumpLines("YOMK_LOGGER_INFO_ALL", YOMK_LOGGER_INFO_ALL());

    /**
     * 步骤8：删除日志器
     *
     * FILE_LOG_DELETE 按名字同时清理 console/file 两张表：
     * - "app" 只在 file 表 → "deleted console:0 file:1"；
     *   删除时未刷盘的缓冲会自动落盘，但磁盘 .log 文件保留（数据保全）；
     * - "ghost" 两表均未命中 → eNo=1；
     * - "MainLogger" 可删（console:1 file:0），删除后再写日志会自动重建。
     */
    printStep(8, "删除日志器",
              "删除 app / ghost / MainLogger 观察返回值；删除后磁盘 .log 仍在，MainLogger 再写自动重建。");
    printResp("FILE_LOG_DELETE(app)", YOMK_FILE_LOG_DELETE("app"));
    printResp("FILE_LOG_DELETE(ghost)", YOMK_FILE_LOG_DELETE("ghost"));
    printResp("FILE_LOG_DELETE(MainLogger)", YOMK_FILE_LOG_DELETE("MainLogger"));

    // 磁盘文件保留验证：app.log 仍可读
    std::cout << ">> ---- 删除后回读 " << logFilePath.string() << "（磁盘文件保留） ----" << std::endl;
    {
        std::ifstream logFile(logFilePath);
        std::string firstLine;
        if (std::getline(logFile, firstLine))
        {
            std::cout << ">> | " << firstLine << " ...（内容仍在）" << std::endl;
        }
    }

    // MainLogger 惰性重建：删除后首次写日志，框架自动重建同名日志器
    printResp("YOMK_INFO(重建后)", YOMK_INFO("MainLogger recreated on demand"));
    dumpLines("YOMK_LOGGER_INFO_LOGGERS(收尾)", YOMK_LOGGER_INFO_LOGGERS());

    std::cout << "\n====== 示例结束：按回车退出 ======" << std::endl;
    getchar();

    return 0;
}
