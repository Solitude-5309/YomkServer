/**
 * @file TestYomkLoggerLifecycle.cpp
 * @brief Logger 模块生命周期与契约基线测试（LG1 从零新建，LG2 审计处置后回填，LG4 补删除接口，LG6 补 proxy 卸载）
 *
 * 覆盖内容（11 个 Section + 1 个前置注入段，断言计数以运行实测为准）：
 * 1. 控制台日志基线：四级别宏输出格式（时间戳+[级别]+[logger]+[行号]）；
 *    级别开关 OFF→无输出仍 eOk→ON→恢复输出，四级别全覆盖
 * 2. 自定义 tag：新 tag 自动创建 console logger；65536 超长 tag；空内容日志
 * 2.5 非法级别 default 降级（P2-c 修复后）：proxy 设置前 raw 注入 level=99 →
 *    console_log default 降级 Info 输出 + eOk（S3 的 proxy 处于拦截态时会短路一切
 *    console_log，故该 default 分支在本窗口触达）
 * 3. proxy 契约：SET_CONSOLE_LOG_PROXY 拦截（false→eOk+"console log proxy is success."+无输出）
 *    与穿透（true→默认输出）；原子计数验证；all 内省 proxy:on；
 *    卸载（LG6/P4-e）：传 nullptr → eOk、all 首行回 proxy:off、恢复框架默认输出、
 *    代理不再被调用（原子命中数不增）；重装可逆。用例末尾重装并回到穿透态，
 *    以维持 S4-S7 依赖的「proxy:on + 穿透」不变量
 * 4. 文件日志闭环：CREATE 建空文件→四级别入缓冲（write 前文件仍空）→WRITE 落盘读回断言
 *    行数与行格式（含 FILE_INFO_TAG 自定义 tag 变体）；重复创建 eNo；未创建 logger 的
 *    FILE_LOG/WRITE eNo；P2-a/P2-b 处置验证（空名/空 dir eInvalid、不可建 dir eNo
 *    且不注册幽灵 logger、失败后同名重试成功）
 * 5. 内省三端点：LOGGERS 按类型（console/file）纯名列表 / LOGGER(name) console/file 两格式与未注册 eNo /
 *    ALL 控制台状态段五 bool 与开关翻转联动 + 日志器清单段
 * 6. 边界值：变参 fold 传 INT_MAX/INT_MIN/0/-1/NaN/Inf/-Inf/-0.0；1MB 超大内容 console+file
 *    双路径；非法级别注入回填（P2-c 修复后 99 合法）：file_log/off/on 三处 default
 *    降级与无副作用实证
 * 7. 异常路径：console_log 收 nullptr 包 / 错类型包（String）均 eNo；console_log 空
 *    logger 名回退 MainLogger / file_log 空名 eNo（raw 注入触达）；LOGGER 空名 eNo
 *    （LG2 堵死空名注册后回填）；未知端点 eNo
 * 8. logger 删除接口（LG4 新增 /delete_logger）：空名 eInvalid、两表未命中 eNo +
 *    "logger not found."；console-only / file-only / 同名双表三种命中形态的 msg 计数
 *    （"deleted console:c file:f"）；file 侧删除释放最后引用 → ~FileLogger 锁外自动落盘
 *    （写 2 行未 flush 即删 → 文件恰 2 行，数据不丢）；重复删除幂等 eNo；删除后单查 eNo
 *    与 LOGGERS/ALL 行数递减守恒；console 惰性重建；MainLogger 无特殊保护（可删，重建须走
 *    空 logger 名回退路径——YOMK_INFO 宏的 logger 名是裸 "MainLogger"，[行号] 拼在内容头部）；
 *    delete_logger 空包/错类型包契约分支
 * 9. 服务级具名开关与全局态端点（两级主闸并联）：具名 OFF/ON 逐器隔离与恢复、未知名 eNo
 *    （不惰性创建）、主闸×逐器组合两层归因（LOGGER_INFO_GLOBAL 报主闸 off 与条目逐器 on
 *    各自真实呈现）、条目逐器真值与两层正交、LOGGER_INFO_GLOBAL 全局主闸态基线与无参宏翻转联动
 * 10. 文件日志两级开关与全局态端点（与控制台同构）：具名 OFF/ON 文件逐器隔离与恢复（落盘文件实证）、
 *     未知名 eNo（不惰性创建）、文件主闸×逐器两层归因（LOGGER_INFO_GLOBAL("file") 报文件主闸 off
 *     与条目逐器 on 各自真实呈现、两层 AND 不落盘）、具名关断不波及文件主闸（两层正交）、
 *     LOGGER_INFO_GLOBAL("file")/"console"）两套主闸态独立直证与无参宏翻转联动、未知 type eNo
 * 11. 文件日志全局代理契约（与控制台 S3 同构，仅单一全局代理无逐器代理）：设置前基线
 *     （GLOBAL("file") proxyOff 且 GLOBAL("console") proxyOn 两套代理独立直证）、
 *     SET_FILE_LOG_PROXY 拦截（false→eOk+"file log proxy is success."+落盘 marker 缺席+原子计数）
 *     与穿透（true→默认落盘回归 + GLOBAL("file") proxyOn）、卸载（LG6 镜像：nullptr→eOk、
 *     GLOBAL("file") 回 proxyOff、恢复默认落盘、代理不再被调用原子命中数不增）、重装可逆；
 *     用例末尾重装并回到穿透态
 *
 * 说明：YomkLogger 是 YomkService 子类（构造绑定 server->weak_from_this()），无法脱离
 *       YomkAPI::init() 单例白盒直连；全部经 API 接口/raw request 测试，同时覆盖 invoke 路由链，
 *       无覆盖损失。ConsoleLogger/FileLogger 类直连白盒见 TestYomkLoggerDirect.cpp。
 *       控制台输出断言采用"唯一 marker + cout rdbuf 捕获"（框架自身日志构成背景噪声，
 *       正向断言查 marker 命中、负向断言查 marker 缺席，均不受噪声影响）。
 *
 * LG1 审计登记 → LG2 处置状态（本文件断言已同步回填）：
 * - P1-a consoleLog 持 shared_lock emplace → 已修复：双检锁（shared 查找 miss 后升级
 *   unique 二次查找），写 map 仅在独占锁下；并发竞态验证归 LG3
 * - P1-b setConsoleLogProxy 无锁写 proxy 字段 → 已修复：专属 m_consoleLogProxyMutex，
 *   读侧锁内拷贝快照、锁外调用回调
 * - P1-c std::localtime 线程不安全 → 已修复：localtime_r（_WIN32 分支 localtime_s）
 * - P2-a createFileLogger 空名/空 dir 无校验 → 已修复：两者均 eInvalid（"logger name is
 *   empty."/"logger dir is empty."），"" key 幽灵注册与 "/.log" 根路径拼接彻底堵死（S4 回填）
 * - P2-b FileLogger::init fs 异常穿透 → 已修复：init 改 bool 返回，异常捕获后 false，
 *   createFileLogger 对失败 eNo 且不注册（S4 回填；Direct 测试同步改写）
 * - P2-c ELogLevel 枚举无固定底层类型（注入 99 即 UB，6 处 default 为死分支）→
 *   已修复：三处枚举加 : int，非法值注入合法化，default 降级 Info 分支转活
 *   （S2.5/S6 回填注入断言；console_log 的 default 由 S2.5 在 S3 安装代理之前触达）
 *
 * 风格：纯 main() + 失败计数，返回非 0 表示存在失败用例（零第三方依赖）
 */

#include <unistd.h>

#include <atomic>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "YomkAPI.h"

namespace fs = std::filesystem;

static int g_failed = 0;

#define YOMK_TEST_CHECK(cond, msg)                                                          \
    do                                                                            \
    {                                                                             \
        if (!(cond))                                                              \
        {                                                                         \
            std::cout << "[FAIL] [line " << __LINE__ << "] " << msg << std::endl; \
            ++g_failed;                                                           \
        }                                                                         \
        else                                                                      \
        {                                                                         \
            std::cout << "[ OK ] [line " << __LINE__ << "] " << msg << std::endl; \
        }                                                                         \
    } while (0)

// ---- 文件级观测装置（TSan-clean 既有模式）----
static std::atomic<long> g_proxyHits{0};           // proxy 被调用次数
static std::atomic<bool> g_proxyIntercept{false};  // true：proxy 返回 false 拦截；false：返回 true 穿透
static std::atomic<long> g_fileProxyHits{0};           // 文件 proxy 被调用次数
static std::atomic<bool> g_fileProxyIntercept{false};  // true：文件 proxy 返回 false 拦截；false：返回 true 穿透

// RAII 捕获 std::cout：构造换 rdbuf，析构还原（同进程内对库侧 std::cout 同样生效）
class CoutCapture
{
public:
    CoutCapture() : m_old(std::cout.rdbuf(m_ss.rdbuf())) {}
    ~CoutCapture()
    {
        std::cout.rdbuf(m_old);
        // 捕获作用域内的 YOMK_TEST_CHECK 失败时 [FAIL] 行会落入缓冲被丢弃，导致日志无失败痕迹；
        // 析构时若缓冲含 [FAIL] 则转储到真实输出，保证失败可诊断
        const std::string captured = m_ss.str();
        if (captured.find("[FAIL]") != std::string::npos)
        {
            std::cout << captured << std::flush;
        }
    }
    std::string str() const { return m_ss.str(); }

private:
    std::stringstream m_ss;
    std::streambuf* m_old;
};

// 读取文件全部内容；打开失败返回 nullopt 语义（以 bool 出参区分）
static std::string readFile(const fs::path& path, bool& ok)
{
    std::ifstream ifs(path);
    ok = ifs.is_open();
    std::stringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

// 统计非空行数
static size_t countLines(const std::string& text)
{
    size_t lines = 0;
    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line))
    {
        if (!line.empty())
        {
            ++lines;
        }
    }
    return lines;
}

// 解包 StringArray 内省响应为字符串向量（空包/错类型返回空向量）
static std::vector<std::string> unpackLines(const YomkResponse& resp)
{
    std::vector<std::string> lines;
    auto arr = yomk::unPack<yomkmsg::StringArray>(resp.m_data);
    if (arr != nullptr)
    {
        lines = arr->d;
    }
    return lines;
}

// 双表合计行数（console/file 端点各取一次，行内容即 logger 纯名）
static size_t loggersTotal()
{
    return unpackLines(YomkAPI::LOGGER_INFO_LOGGERS("console")).size() +
           unpackLines(YomkAPI::LOGGER_INFO_LOGGERS("file")).size();
}

// 从 ALL 清单取首条 console 条目的控制台状态段（无 console 条目返回 nullptr）
static const yomk::LoggerState* firstConsoleState(yomkmsg::LoggerInfoListPtr listPkg)
{
    if (!listPkg)
    {
        return nullptr;
    }
    for (const auto& li : listPkg->d)
    {
        if (li.m_type == "console")
        {
            return &li.m_loggerState;
        }
    }
    return nullptr;
}

// 时间戳格式粗校验："dddd-dd-dd dd:dd:dd.ddd]"（不锁定具体日期值）
static bool checkTimeFormat(const std::string& line)
{
    auto isDigit = [](char c) { return c >= '0' && c <= '9'; };
    // 形如 [2026-09-06 12:34:56.789]，起点对齐行首 '['
    if (line.size() < 25 || line[0] != '[')
    {
        return false;
    }
    for (int i = 1; i <= 23; ++i)
    {
        switch (i)
        {
            case 5:
            case 8:
                if (line[i] != '-')
                    return false;
                break;
            case 11:
                if (line[i] != ' ')
                    return false;
                break;
            case 14:
            case 17:
                if (line[i] != ':')
                    return false;
                break;
            case 20:
                if (line[i] != '.')
                    return false;
                break;
            case 24:
                if (line[i] != ']')
                    return false;
                break;
            default:
                if (!isDigit(line[i]))
                    return false;
                break;
        }
    }
    return true;
}

// 测试临时根：run_tests.sh 注入（Test/test_logs/tmp），独立运行回退 /tmp
static fs::path testTmpRoot()
{
    const char* env = std::getenv("YOMK_TEST_TMP_ROOT");
    return (env && *env) ? fs::path(env) : fs::temp_directory_path();
}

int main()
{
    auto server = YomkAPI::init(1);
    YOMK_TEST_CHECK(server != nullptr, "YomkAPI::init() 返回非空服务器");

    // 临时目录（收尾 remove_all）
    fs::path tmpDir = testTmpRoot() / ("yomk_logger_lg1_" + std::to_string(::getpid()));
    std::error_code ec;
    fs::create_directories(tmpDir, ec);
    YOMK_TEST_CHECK(!ec, "临时目录创建成功: " + tmpDir.string());

    // ============ Section 1: 控制台日志基线（四级别 + 开关） ============
    {// 四级别各一发（YOMK_* 宏把 "[行号]" 拼在内容头部），marker 唯一化断言
     {CoutCapture cap;
    auto r = YOMK_INFO("lg1_s1_info_marker ", 42);
    YOMK_TEST_CHECK(r.m_status == YomkResponse::eOk, "YOMK_INFO 返回 eOk");
    YOMK_TEST_CHECK(r.m_msg == "success.", "YOMK_INFO 契约消息 success.");
    std::string out = cap.str();
    YOMK_TEST_CHECK(out.find("lg1_s1_info_marker  42") != std::string::npos, "INFO 输出含 marker 与整数变参");
    YOMK_TEST_CHECK(out.find("[Info ] [MainLogger] [") != std::string::npos, "INFO 输出格式 [Info ] [MainLogger] [行号]");
    bool fmtOk = false;
    std::istringstream iss(out);
    std::string line;
    while (std::getline(iss, line))
    {
        if (line.find("lg1_s1_info_marker") != std::string::npos)
        {
            fmtOk = checkTimeFormat(line);
        }
    }
    YOMK_TEST_CHECK(fmtOk, "INFO 输出行首时间戳格式 [YYYY-MM-DD HH:MM:SS.mmm]");
}
{
    CoutCapture cap;
    YOMK_TEST_CHECK(YOMK_WARN("lg1_s1_warn_marker").m_status == YomkResponse::eOk, "YOMK_WARN 返回 eOk");
    YOMK_TEST_CHECK(
        (cap.str().find("[Warn ] [MainLogger]") != std::string::npos &&
         cap.str().find("lg1_s1_warn_marker") != std::string::npos),
        "WARN 输出格式与 marker");
}
{
    CoutCapture cap;
    YOMK_TEST_CHECK(YOMK_ERROR("lg1_s1_error_marker").m_status == YomkResponse::eOk, "YOMK_ERROR 返回 eOk");
    YOMK_TEST_CHECK(
        (cap.str().find("[Error] [MainLogger]") != std::string::npos &&
         cap.str().find("lg1_s1_error_marker") != std::string::npos),
        "ERROR 输出格式与 marker");
}
{
    CoutCapture cap;
    YOMK_TEST_CHECK(YOMK_DEBUG("lg1_s1_debug_marker").m_status == YomkResponse::eOk, "YOMK_DEBUG 返回 eOk");
    YOMK_TEST_CHECK(
        (cap.str().find("[Debug] [MainLogger]") != std::string::npos &&
         cap.str().find("lg1_s1_debug_marker") != std::string::npos),
        "DEBUG 输出格式与 marker");
}

// 级别开关：OFF→无输出仍 eOk→ON→恢复输出（四级别逐一）
{
    YOMK_TEST_CHECK(YomkAPI::OFF_CONSOLE_LOG_INFO().m_status == YomkResponse::eOk, "OFF_CONSOLE_LOG_INFO 返回 eOk");
    CoutCapture cap;
    YOMK_TEST_CHECK(YOMK_INFO("lg1_s1_off_info_marker").m_status == YomkResponse::eOk, "INFO 级别关闭后调用仍 eOk");
    YOMK_TEST_CHECK(cap.str().find("lg1_s1_off_info_marker") == std::string::npos, "INFO 级别关闭后无输出");
    YOMK_TEST_CHECK(YomkAPI::ON_CONSOLE_LOG_INFO().m_status == YomkResponse::eOk, "ON_CONSOLE_LOG_INFO 返回 eOk");
    CoutCapture cap2;
    YOMK_TEST_CHECK(YOMK_INFO("lg1_s1_on_info_marker").m_status == YomkResponse::eOk, "INFO 级别重开后调用 eOk");
    YOMK_TEST_CHECK(cap2.str().find("lg1_s1_on_info_marker") != std::string::npos, "INFO 级别重开后恢复输出");
}
{
    YOMK_TEST_CHECK(YomkAPI::OFF_CONSOLE_LOG_WARN().m_status == YomkResponse::eOk, "OFF_CONSOLE_LOG_WARN 返回 eOk");
    CoutCapture cap;
    YOMK_WARN("lg1_s1_off_warn_marker");
    YOMK_TEST_CHECK(cap.str().find("lg1_s1_off_warn_marker") == std::string::npos, "WARN 级别关闭后无输出");
    YomkAPI::ON_CONSOLE_LOG_WARN();
    CoutCapture cap2;
    YOMK_WARN("lg1_s1_on_warn_marker");
    YOMK_TEST_CHECK(cap2.str().find("lg1_s1_on_warn_marker") != std::string::npos, "WARN 级别重开后恢复输出");
}
{
    YOMK_TEST_CHECK(YomkAPI::OFF_CONSOLE_LOG_ERROR().m_status == YomkResponse::eOk, "OFF_CONSOLE_LOG_ERROR 返回 eOk");
    CoutCapture cap;
    YOMK_ERROR("lg1_s1_off_error_marker");
    YOMK_TEST_CHECK(cap.str().find("lg1_s1_off_error_marker") == std::string::npos, "ERROR 级别关闭后无输出");
    YomkAPI::ON_CONSOLE_LOG_ERROR();
    CoutCapture cap2;
    YOMK_ERROR("lg1_s1_on_error_marker");
    YOMK_TEST_CHECK(cap2.str().find("lg1_s1_on_error_marker") != std::string::npos, "ERROR 级别重开后恢复输出");
}
{
    YOMK_TEST_CHECK(YomkAPI::OFF_CONSOLE_LOG_DEBUG().m_status == YomkResponse::eOk, "OFF_CONSOLE_LOG_DEBUG 返回 eOk");
    CoutCapture cap;
    YOMK_DEBUG("lg1_s1_off_debug_marker");
    YOMK_TEST_CHECK(cap.str().find("lg1_s1_off_debug_marker") == std::string::npos, "DEBUG 级别关闭后无输出");
    YomkAPI::ON_CONSOLE_LOG_DEBUG();
    CoutCapture cap2;
    YOMK_DEBUG("lg1_s1_on_debug_marker");
    YOMK_TEST_CHECK(cap2.str().find("lg1_s1_on_debug_marker") != std::string::npos, "DEBUG 级别重开后恢复输出");
}
}

// ============ Section 2: 自定义 tag（自动创建 console logger） ============
{{CoutCapture cap;
YOMK_TEST_CHECK(
    YOMK_INFO_TAG("lg1_tag_logger", "lg1_s2_tag_marker").m_status == YomkResponse::eOk,
    "YOMK_INFO_TAG 新 tag 返回 eOk");
YOMK_TEST_CHECK(
    (cap.str().find("[lg1_tag_logger]") != std::string::npos &&
     cap.str().find("lg1_s2_tag_marker") != std::string::npos),
    "新 tag 自动创建 console logger 并以其名输出");
}
{
    // 宏形态 tag 参数字面量拼接（tag ":" 行号），运行时 string tag 走底层 API 直连
    std::string longTag(65536, 'T');
    CoutCapture cap;
    YOMK_TEST_CHECK(
        YomkAPI::CONSOLE_LOG_INFO_TAG(longTag, "lg1_s2_longtag_marker").m_status == YomkResponse::eOk,
        "65536 字符超长 tag 返回 eOk");
    YOMK_TEST_CHECK(cap.str().find("lg1_s2_longtag_marker") != std::string::npos, "超长 tag 日志正常输出");
    YOMK_TEST_CHECK(cap.str().find("[" + longTag + "]") != std::string::npos, "超长 tag 完整回显到输出行");
}
{
    CoutCapture cap;
    YOMK_TEST_CHECK(YOMK_INFO_TAG("lg1_tag_empty", "").m_status == YomkResponse::eOk, "空内容日志返回 eOk");
    YOMK_TEST_CHECK(cap.str().find("[lg1_tag_empty]") != std::string::npos, "空内容日志仍输出 tag 头（内容为空）");
}
}

// ============ Section 2.5: 非法级别 default 降级（P2-c 修复后回填，须在 S3 设置 proxy 前） ============
{
    // proxy 处于拦截态时会短路一切 console_log（含 switch 的 default 分支），故本节置于
    // S3 安装代理之前，与代理后续处于何种态无关；LG6/P4-e 起代理可传 nullptr 卸载，
    // 但本节顺序保持不变。枚举已加 : int 底层类型，99 为合法值非 UB
    std::string out;
    YomkResponse r(YomkResponse::eInvalid);
    {
        CoutCapture cap;
        r = YomkAPI::request(
            "/YomkLogger/console_log",
            yomkmsg::Log::shared(yomk::Log{static_cast<yomk::Log::ELogLevel>(99), "lg1_s2p5_badlevel_marker", "MainLogger"}));
        out = cap.str();
    }
    YOMK_TEST_CHECK(r.m_status == YomkResponse::eOk, "非法级别 99 console_log 返回 eOk（P2-c 修复后 default 活分支）");
    YOMK_TEST_CHECK(
        out.find("unknown log level, use Info") != std::string::npos, "console_log default 分支错误提示（降级 Info）");
    YOMK_TEST_CHECK(
        out.find("[Info ] [MainLogger] lg1_s2p5_badlevel_marker") != std::string::npos,
        "非法级别降级 Info 输出 marker");
}

// ============ Section 3: proxy 契约（含 LG6/P4-e 卸载；末尾重装复位，S4-S7 仍为穿透态） ============
{
    // 设置前 all 内省 proxy:off
    {
        auto allResp = YomkAPI::LOGGER_INFO_ALL();
        auto listPkg = yomk::unPack<yomkmsg::LoggerInfoList>(allResp.m_data);
        const yomk::LoggerState* st = firstConsoleState(listPkg);
        YOMK_TEST_CHECK(
            (st != nullptr && st->m_debugOn && st->m_infoOn && st->m_warnOn && st->m_errorOn && !st->m_proxyOn),
            "proxy 设置前 ALL 控制台状态全 on + proxyOff");
    }

    g_proxyHits.store(0);
    g_proxyIntercept.store(true);
    auto proxy = [](const yomk::Log&) -> bool
    {
        g_proxyHits.fetch_add(1);
        return !g_proxyIntercept.load();  // 拦截态返回 false，穿透态返回 true
    };
    YOMK_TEST_CHECK(YomkAPI::SET_CONSOLE_LOG_PROXY(proxy).m_status == YomkResponse::eOk, "SET_CONSOLE_LOG_PROXY 返回 eOk");

    // 拦截态：proxy 返回 false → eOk + 专属消息 + 无控制台输出
    {
        CoutCapture cap;
        auto r = YOMK_INFO("lg1_s3_intercept_marker");
        YOMK_TEST_CHECK(r.m_status == YomkResponse::eOk, "拦截态 console_log 返回 eOk");
        YOMK_TEST_CHECK(r.m_msg == "console log proxy is success.", "拦截态契约消息 console log proxy is success.");
        YOMK_TEST_CHECK(cap.str().find("lg1_s3_intercept_marker") == std::string::npos, "拦截态无默认控制台输出");
        YOMK_TEST_CHECK(g_proxyHits.load() == 1, "proxy 被调用 1 次（原子计数）");
    }

    // 穿透态：proxy 返回 true → 走默认输出
    {
        g_proxyIntercept.store(false);
        CoutCapture cap;
        auto r = YOMK_INFO("lg1_s3_pass_marker");
        YOMK_TEST_CHECK((r.m_status == YomkResponse::eOk && r.m_msg == "success."), "穿透态 console_log 返回 eOk + success.");
        YOMK_TEST_CHECK(cap.str().find("lg1_s3_pass_marker") != std::string::npos, "穿透态恢复默认控制台输出");
        YOMK_TEST_CHECK(g_proxyHits.load() == 2, "proxy 累计被调用 2 次");
    }

    // 设置后 all 内省 proxy:on
    {
        auto allResp = YomkAPI::LOGGER_INFO_ALL();
        auto listPkg = yomk::unPack<yomkmsg::LoggerInfoList>(allResp.m_data);
        const yomk::LoggerState* st = firstConsoleState(listPkg);
        YOMK_TEST_CHECK(st != nullptr && st->m_proxyOn, "proxy 设置后 ALL 状态 proxyOn");
    }

    // ---- LG6（P4-e）：proxy 可卸载 ----
    // 卸载前先把代理切回拦截态，确认它确实在链路上——否则「卸载后恢复默认输出」
    // 与「本来就是穿透态」无从区分
    {
        g_proxyIntercept.store(true);
        std::string out;
        YomkResponse r(YomkResponse::eInvalid);
        {
            CoutCapture cap;
            r = YOMK_INFO("lg6_s3_pre_unload_marker");
            out = cap.str();
        }
        YOMK_TEST_CHECK(
            (r.m_status == YomkResponse::eOk && r.m_msg == "console log proxy is success."),
            "卸载前拦截态契约消息（代理确实在链路上）");
        YOMK_TEST_CHECK(out.find("lg6_s3_pre_unload_marker") == std::string::npos, "卸载前拦截态无默认控制台输出");
    }

    // 卸载：传 nullptr（等价于空 std::function）
    {
        YOMK_TEST_CHECK(
            YomkAPI::SET_CONSOLE_LOG_PROXY(nullptr).m_status == YomkResponse::eOk,
            "SET_CONSOLE_LOG_PROXY(nullptr) 卸载返回 eOk");
        auto allResp = YomkAPI::LOGGER_INFO_ALL();
        auto listPkg = yomk::unPack<yomkmsg::LoggerInfoList>(allResp.m_data);
        const yomk::LoggerState* st = firstConsoleState(listPkg);
        YOMK_TEST_CHECK(
            st != nullptr && !st->m_proxyOn,
            "卸载后 ALL 状态回到 proxyOff（P4-e 前恒报 proxyOn）");
    }

    // 卸载后：恢复框架默认输出，且代理不再被调用
    {
        const long hitsBefore = g_proxyHits.load();
        std::string out;
        YomkResponse r(YomkResponse::eInvalid);
        {
            CoutCapture cap;
            r = YOMK_INFO("lg6_s3_post_unload_marker");
            out = cap.str();
        }
        YOMK_TEST_CHECK(
            (r.m_status == YomkResponse::eOk && r.m_msg == "success."),
            "卸载后 console_log 返回 eOk + success.（走默认路径而非代理短路）");
        YOMK_TEST_CHECK(out.find("lg6_s3_post_unload_marker") != std::string::npos, "卸载后恢复框架默认控制台输出");
        YOMK_TEST_CHECK(
            out.find("[Info ] [MainLogger] [") != std::string::npos,
            "卸载后输出为框架默认格式 <时间戳> [Info ] [logger] msg");
        YOMK_TEST_CHECK(g_proxyHits.load() == hitsBefore, "卸载后代理不再被调用（原子命中数不增）");
    }

    // 复位：重装同一代理并回到穿透态，维持 S4-S7 依赖的「proxy:on + 穿透」不变量
    {
        g_proxyIntercept.store(false);
        YOMK_TEST_CHECK(YomkAPI::SET_CONSOLE_LOG_PROXY(proxy).m_status == YomkResponse::eOk, "重装代理返回 eOk（卸载可逆）");
        auto allResp = YomkAPI::LOGGER_INFO_ALL();
        auto listPkg = yomk::unPack<yomkmsg::LoggerInfoList>(allResp.m_data);
        const yomk::LoggerState* st = firstConsoleState(listPkg);
        YOMK_TEST_CHECK(
            st != nullptr && st->m_proxyOn,
            "重装后 ALL 状态恢复 proxyOn（S4-S7 不变量已复位）");
    }
}

// ============ Section 4: 文件日志闭环 ============
{
    fs::path logDir = tmpDir / "s4";
    fs::create_directories(logDir, ec);
    fs::path logFile = logDir / "lg1_file_logger.log";

    // 创建：eOk + 空文件已建（init 语义）
    YOMK_TEST_CHECK(
        YomkAPI::FILE_LOG_CREATE(logDir.string(), "lg1_file_logger").m_status == YomkResponse::eOk,
        "FILE_LOG_CREATE 返回 eOk");
    YOMK_TEST_CHECK((fs::exists(logFile) && fs::file_size(logFile, ec) == 0), "CREATE 后 <dir>/<name>.log 空文件已建");

    // 四级别入缓冲：write 前文件仍空（缓冲语义）
    YOMK_TEST_CHECK(
        YOMK_FILE_INFO("lg1_file_logger", "lg1_s4_info_marker ", 7).m_status == YomkResponse::eOk,
        "FILE_INFO 返回 eOk");
    YOMK_TEST_CHECK(YOMK_FILE_WARN("lg1_file_logger", "lg1_s4_warn_marker").m_status == YomkResponse::eOk, "FILE_WARN 返回 eOk");
    YOMK_TEST_CHECK(
        YOMK_FILE_ERROR("lg1_file_logger", "lg1_s4_error_marker").m_status == YomkResponse::eOk, "FILE_ERROR 返回 eOk");
    YOMK_TEST_CHECK(
        YOMK_FILE_DEBUG("lg1_file_logger", "lg1_s4_debug_marker").m_status == YomkResponse::eOk, "FILE_DEBUG 返回 eOk");
    YOMK_TEST_CHECK(fs::file_size(logFile, ec) == 0, "WRITE 前文件仍为空（stringstream 缓冲语义）");

    // 落盘：读回断言行数/行格式/marker/tag
    YOMK_TEST_CHECK(YomkAPI::FILE_LOG_WRITE("lg1_file_logger").m_status == YomkResponse::eOk, "FILE_LOG_WRITE 返回 eOk");
    bool readOk = false;
    std::string content = readFile(logFile, readOk);
    YOMK_TEST_CHECK(readOk, "落盘后日志文件可读");
    YOMK_TEST_CHECK(countLines(content) == 4, "落盘内容恰 4 行（四级别各一）");
    YOMK_TEST_CHECK(
        (content.find("[Info ] [MainLogger]") != std::string::npos &&
         content.find("[Info ] [MainLogger] [") != std::string::npos &&
         content.find("lg1_s4_info_marker  7") != std::string::npos),
        "Info 行格式（时间戳+[Info ]+[MainLogger]+[行号] 内容）——FILE_INFO 默认内容 tag 为 MainLogger，行号独立成段，"
        "logger 名仅作路由 key");
    // 自定义内容 tag 变体：FILE_INFO_TAG 的 tag 进内容行
    YOMK_TEST_CHECK(
        YOMK_FILE_INFO_TAG("lg1_file_logger", "lg1CustomTag", "lg1_s4_customtag_marker").m_status == YomkResponse::eOk,
        "FILE_INFO_TAG 自定义 tag 返回 eOk");
    YOMK_TEST_CHECK(YomkAPI::FILE_LOG_WRITE("lg1_file_logger").m_status == YomkResponse::eOk, "自定义 tag 内容 WRITE eOk");
    bool readOkTag = false;
    std::string contentTag = readFile(logFile, readOkTag);
    YOMK_TEST_CHECK(
        (readOkTag && contentTag.find("[Info ] [lg1CustomTag] [") != std::string::npos &&
         contentTag.find("lg1_s4_customtag_marker") != std::string::npos),
        "自定义 tag 行格式（[Info ] [tag] [行号] 内容）");
    YOMK_TEST_CHECK(
        (content.find("[Warn ]") != std::string::npos && content.find("[Error]") != std::string::npos &&
         content.find("[Debug]") != std::string::npos),
        "Warn/Error/Debug 三级别行均在");
    YOMK_TEST_CHECK(checkTimeFormat(content), "文件日志行首时间戳格式 [YYYY-MM-DD HH:MM:SS.mmm]");

    // 落盘后缓冲清空：再次 WRITE 文件不增长
    auto sizeBefore = fs::file_size(logFile, ec);
    YOMK_TEST_CHECK(YomkAPI::FILE_LOG_WRITE("lg1_file_logger").m_status == YomkResponse::eOk, "二次 FILE_LOG_WRITE 返回 eOk");
    YOMK_TEST_CHECK(fs::file_size(logFile, ec) == sizeBefore, "二次 WRITE 无重复内容（缓冲已清空）");

    // 重复创建 eNo
    auto dup = YomkAPI::FILE_LOG_CREATE(logDir.string(), "lg1_file_logger");
    YOMK_TEST_CHECK((dup.m_status == YomkResponse::eNo), "重复创建同名 file logger 返回 eNo");
    YOMK_TEST_CHECK(dup.m_msg == "logger name already exists.", "重复创建契约消息");

    // 未创建 logger：FILE_LOG / FILE_LOG_WRITE 均 eNo
    auto noLogger = YOMK_FILE_INFO("lg1_never_created", "x");
    YOMK_TEST_CHECK((noLogger.m_status == YomkResponse::eNo), "FILE_LOG 未创建 logger 返回 eNo");
    YOMK_TEST_CHECK(noLogger.m_msg == "file logger not found.", "FILE_LOG not-found 契约消息");
    auto noWrite = YomkAPI::FILE_LOG_WRITE("lg1_never_created");
    YOMK_TEST_CHECK(noWrite.m_status == YomkResponse::eNo, "FILE_LOG_WRITE 未创建 logger 返回 eNo");
    YOMK_TEST_CHECK(noWrite.m_msg == "logger not found.", "FILE_LOG_WRITE not-found 契约消息");

    // P2-a 处置验证（LG2 回填）：空名/空 dir 一律 eInvalid，"" key 注册与
    // "/.log" 根路径拼接彻底堵死
    auto emptyBoth = YomkAPI::FILE_LOG_CREATE("", "");
    YOMK_TEST_CHECK(
        (emptyBoth.m_status == YomkResponse::eInvalid && emptyBoth.m_msg == "logger name is empty."),
        "空名+空 dir 创建返回 eInvalid + logger name is empty.（P2-a 已处置）");
    auto emptyName = YomkAPI::FILE_LOG_CREATE(tmpDir.string(), "");
    YOMK_TEST_CHECK(
        (emptyName.m_status == YomkResponse::eInvalid && emptyName.m_msg == "logger name is empty."),
        "空名+有效 dir 创建返回 eInvalid（P2-a 已处置）");
    auto emptyDir = YomkAPI::FILE_LOG_CREATE("", "lg1_empty_dir_name");
    YOMK_TEST_CHECK(
        (emptyDir.m_status == YomkResponse::eInvalid && emptyDir.m_msg == "logger dir is empty."),
        "有效名+空 dir 创建返回 eInvalid + logger dir is empty.（P2-a 已处置）");

    // P2-b 处置验证（LG2 回填）：不可创建 dir → init 返回 false → eNo 且不注册幽灵 logger
    auto badDir = YomkAPI::FILE_LOG_CREATE("/proc/lg1_impossible/sub", "lg1_bad_dir_logger");
    YOMK_TEST_CHECK(
        (badDir.m_status == YomkResponse::eNo && badDir.m_msg == "init file logger failed."),
        "不可创建 dir 返回 eNo + init file logger failed.（P2-b 已处置，异常不穿透）");
    auto ghostInfo = YomkAPI::LOGGER_INFO_LOGGER("lg1_bad_dir_logger", "file");
    YOMK_TEST_CHECK(
        (ghostInfo.m_status == YomkResponse::eNo && ghostInfo.m_msg == "logger not found."),
        "init 失败的 logger 未注册（无幽灵 key，内省 not found）");
    // init 失败不占用名字：同名换有效 dir 重试应成功
    fs::path retryDir = tmpDir / "s4_retry";
    auto retry = YomkAPI::FILE_LOG_CREATE(retryDir.string(), "lg1_bad_dir_logger");
    YOMK_TEST_CHECK(
        (retry.m_status == YomkResponse::eOk && fs::exists(retryDir / "lg1_bad_dir_logger.log")),
        "init 失败后同名换有效 dir 重试成功（名字未被幽灵占用）");
}

// ============ Section 5: 内省三端点 ============
{
    // LOGGERS：按类型查询，行内容即 logger 纯名
    auto consoleResp = YomkAPI::LOGGER_INFO_LOGGERS("console");
    YOMK_TEST_CHECK(consoleResp.m_status == YomkResponse::eOk, "LOGGER_INFO_LOGGERS(console) 返回 eOk");
    auto consoleArr = yomk::unPack<yomkmsg::StringArray>(consoleResp.m_data);
    bool hasMain = false;
    bool hasTag = false;
    if (consoleArr)
    {
        for (const auto& line : consoleArr->d)
        {
            if (line == "MainLogger")
            {
                hasMain = true;
            }
            if (line.rfind("lg1_tag_logger", 0) == 0)
            {
                hasTag = true;
            }
        }
    }
    YOMK_TEST_CHECK((consoleArr != nullptr && hasMain), "LOGGERS(console) 含 MainLogger 行");
    YOMK_TEST_CHECK(hasTag, "LOGGERS(console) 含 S2 自建 tag logger 行");
    auto fileResp = YomkAPI::LOGGER_INFO_LOGGERS("file");
    YOMK_TEST_CHECK(fileResp.m_status == YomkResponse::eOk, "LOGGER_INFO_LOGGERS(file) 返回 eOk");
    auto fileArr = yomk::unPack<yomkmsg::StringArray>(fileResp.m_data);
    bool hasFile = false;
    if (fileArr)
    {
        for (const auto& line : fileArr->d)
        {
            if (line == "lg1_file_logger")
            {
                hasFile = true;
            }
        }
    }
    YOMK_TEST_CHECK(hasFile, "LOGGERS(file) 含 lg1_file_logger 行（dir 经 LOGGER 单查 m_dir 校验）");

    // LOGGER(name)：console/file 命中结构化返回（LoggerInfo 包，m_msg 恒 "ok"）与未注册
    auto consoleInfo = YomkAPI::LOGGER_INFO_LOGGER("MainLogger", "console");
    YOMK_TEST_CHECK(
        (consoleInfo.m_status == YomkResponse::eOk && consoleInfo.m_msg == "ok"),
        "LOGGER(MainLogger) 命中 m_msg 恒为 ok");
    auto consolePkg = yomk::unPack<yomkmsg::LoggerInfo>(consoleInfo.m_data);
    YOMK_TEST_CHECK(consolePkg != nullptr, "LOGGER(MainLogger) 数据为 LoggerInfo 包");
    YOMK_TEST_CHECK(consolePkg && consolePkg->d.m_loggerName == "MainLogger" && consolePkg->d.m_type == "console",
          "LOGGER(MainLogger) 字段 loggerName/type 一致");
    YOMK_TEST_CHECK(consolePkg && consolePkg->d.m_dir.empty(), "console 命中 m_dir 为空");
    auto fileInfo = YomkAPI::LOGGER_INFO_LOGGER("lg1_file_logger", "file");
    YOMK_TEST_CHECK(
        (fileInfo.m_status == YomkResponse::eOk && fileInfo.m_msg == "ok"),
        "LOGGER(lg1_file_logger) 命中 m_msg 恒为 ok");
    auto filePkg = yomk::unPack<yomkmsg::LoggerInfo>(fileInfo.m_data);
    YOMK_TEST_CHECK(filePkg != nullptr, "LOGGER(lg1_file_logger) 数据为 LoggerInfo 包");
    YOMK_TEST_CHECK(filePkg && filePkg->d.m_loggerName == "lg1_file_logger" && filePkg->d.m_type == "file",
          "LOGGER(lg1_file_logger) 字段 loggerName/type 一致");
    YOMK_TEST_CHECK(filePkg && filePkg->d.m_dir == (tmpDir / "s4").string(), "file 命中 m_dir 为创建目录");
    auto noInfo = YomkAPI::LOGGER_INFO_LOGGER("lg1_never_registered", "console");
    YOMK_TEST_CHECK(
        (noInfo.m_status == YomkResponse::eNo && noInfo.m_msg == "logger not found."),
        "LOGGER 未注册名返回 eNo + logger not found.");

    // ALL：控制台状态字段与开关翻转联动（逐字段断言）
    {
        auto allResp = YomkAPI::LOGGER_INFO_ALL();
        auto listPkg = yomk::unPack<yomkmsg::LoggerInfoList>(allResp.m_data);
        const yomk::LoggerState* st = firstConsoleState(listPkg);
        YOMK_TEST_CHECK(
            (st != nullptr && st->m_debugOn && st->m_infoOn && st->m_warnOn && st->m_errorOn && st->m_proxyOn),
            "ALL 控制台状态全 on（S3 已设 proxy）");
    }
    {
        YomkAPI::OFF_CONSOLE_LOG_INFO();
        // 主闸翻转与条目解耦后，翻转联动取数迁移至 LOGGER_INFO_GLOBAL（全局主闸态）；
        // 条目改报逐器层真值，不受无参翻转影响（正交性在 Section 9 直证）
        auto gResp = YomkAPI::LOGGER_INFO_GLOBAL();
        auto gs = yomk::unPack<yomkmsg::LoggerState>(gResp.m_data);
        YOMK_TEST_CHECK(
            (gs != nullptr && gs->d.m_debugOn && !gs->d.m_infoOn && gs->d.m_warnOn && gs->d.m_errorOn && gs->d.m_proxyOn),
            "OFF INFO 后全局主闸状态 infoOff（开关联动逐字段）");
        YomkAPI::ON_CONSOLE_LOG_INFO();
        auto gResp2 = YomkAPI::LOGGER_INFO_GLOBAL();
        auto gs2 = yomk::unPack<yomkmsg::LoggerState>(gResp2.m_data);
        YOMK_TEST_CHECK(
            (gs2 != nullptr && gs2->d.m_debugOn && gs2->d.m_infoOn && gs2->d.m_warnOn && gs2->d.m_errorOn && gs2->d.m_proxyOn),
            "ON INFO 复原后全局主闸状态 infoOn");
    }
}

// ============ Section 6: 边界值（数值特殊值 / 超大内容 / 非法级别） ============
{// 变参 fold：整数边界与浮点特殊值（ostringstream 默认格式字符串化）
 {CoutCapture cap;
YOMK_TEST_CHECK(
    YOMK_INFO_TAG("lg1_s6_num", "i=", INT_MAX, " j=", INT_MIN, " z=", 0, " n=", -1).m_status == YomkResponse::eOk,
    "INT_MAX/INT_MIN/0/-1 变参日志返回 eOk");
std::string out = cap.str();
YOMK_TEST_CHECK(
    (out.find("i= 2147483647") != std::string::npos && out.find("j= -2147483648") != std::string::npos &&
     out.find("z= 0") != std::string::npos && out.find("n= -1") != std::string::npos),
    "整数边界值字符串化正确（变参以空格分隔，ostringstream 默认格式）");
}
{
    CoutCapture cap;
    double nanV = std::nan("");
    double infV = std::numeric_limits<double>::infinity();
    YOMK_TEST_CHECK(
        YOMK_INFO_TAG("lg1_s6_flt", "a=", nanV, " b=", infV, " c=", -infV, " d=", -0.0).m_status == YomkResponse::eOk,
        "NaN/Inf/-Inf/-0.0 变参日志返回 eOk");
    std::string out = cap.str();
    YOMK_TEST_CHECK(
        (out.find("a= nan") != std::string::npos && out.find("b= inf") != std::string::npos &&
         out.find("c= -inf") != std::string::npos && out.find("d= -0") != std::string::npos),
        "浮点特殊值字符串化正确（nan/inf/-inf/-0，变参以空格分隔）");
}

// 1MB 超大内容：console + file 双路径
{
    std::string big(1024 * 1024, 'B');
    CoutCapture cap;
    YOMK_TEST_CHECK(YOMK_INFO_TAG("lg1_s6_big", big).m_status == YomkResponse::eOk, "1MB 超大内容控制台日志返回 eOk");
    YOMK_TEST_CHECK(cap.str().find(big) != std::string::npos, "1MB 内容完整输出到控制台");

    fs::path bigDir = tmpDir / "s6";
    fs::create_directories(bigDir, ec);
    YOMK_TEST_CHECK(
        YomkAPI::FILE_LOG_CREATE(bigDir.string(), "lg1_big_logger").m_status == YomkResponse::eOk,
        "超大内容用 file logger 创建 eOk");
    YOMK_TEST_CHECK(YOMK_FILE_INFO("lg1_big_logger", big).m_status == YomkResponse::eOk, "1MB 超大内容文件日志返回 eOk");
    YOMK_TEST_CHECK(YomkAPI::FILE_LOG_WRITE("lg1_big_logger").m_status == YomkResponse::eOk, "超大内容 WRITE 返回 eOk");
    bool readOk = false;
    std::string content = readFile(bigDir / "lg1_big_logger.log", readOk);
    YOMK_TEST_CHECK((readOk && content.find(big) != std::string::npos), "1MB 内容完整落盘");
}

// 非法级别注入回填（P2-c 修复后）：枚举已加 : int，99 为合法值；
// file_log/off/on 三处 default 降级与无副作用实证
// （console_log 的 default 已在 S2.5 于 proxy 设置前触达）
{
    fs::path badLevelDir = tmpDir / "s6_badlevel";
    fs::create_directories(badLevelDir, ec);
    YOMK_TEST_CHECK(
        YomkAPI::FILE_LOG_CREATE(badLevelDir.string(), "lg1_s6_badlevel").m_status == YomkResponse::eOk,
        "非法级别注入用 file logger 创建 eOk");
    std::string out;
    YomkResponse r(YomkResponse::eInvalid);
    {
        CoutCapture cap;
        r = YomkAPI::request(
            "/YomkLogger/file_log",
            yomkmsg::Log::shared(
                yomk::Log{static_cast<yomk::Log::ELogLevel>(99), "lg1_s6_badlevel_marker", "lg1_s6_badlevel"}));
        out = cap.str();
    }
    YOMK_TEST_CHECK(
        (r.m_status == YomkResponse::eOk && r.m_msg == "success."),
        "file_log 非法级别 99 返回 eOk（P2-c 修复后 default 活分支）");
    YOMK_TEST_CHECK(out.find("unknown log level, use Info") != std::string::npos, "file_log default 分支错误提示（降级 Info）");
    YOMK_TEST_CHECK(YomkAPI::FILE_LOG_WRITE("lg1_s6_badlevel").m_status == YomkResponse::eOk, "非法级别落盘 WRITE eOk");
    bool readOk = false;
    std::string content = readFile(badLevelDir / "lg1_s6_badlevel.log", readOk);
    YOMK_TEST_CHECK(
        (readOk && countLines(content) == 1 && content.find("[Info ] lg1_s6_badlevel_marker") != std::string::npos),
        "非法级别降级 Info 落盘恰 1 行");

    // off/on 非法级别：default 仅记录错误，无副作用仍 eOk，开关状态不变
    std::string outOff;
    YomkResponse rOff(YomkResponse::eInvalid);
    {
        CoutCapture cap;
        rOff = YomkAPI::request(
            "/YomkLogger/off_console_log_by_level",
            yomkmsg::Log::shared(yomk::Log{static_cast<yomk::Log::ELogLevel>(99), "", ""}));
        outOff = cap.str();
    }
    YOMK_TEST_CHECK(
        (rOff.m_status == YomkResponse::eOk && outOff.find("unknown log level, turn off failed.") != std::string::npos),
        "off 非法级别 eOk + 错误提示（default 活分支）");
    std::string outOn;
    YomkResponse rOn(YomkResponse::eInvalid);
    {
        CoutCapture cap;
        rOn = YomkAPI::request(
            "/YomkLogger/on_console_log_by_level",
            yomkmsg::Log::shared(yomk::Log{static_cast<yomk::Log::ELogLevel>(99), "", ""}));
        outOn = cap.str();
    }
    YOMK_TEST_CHECK(
        (rOn.m_status == YomkResponse::eOk && outOn.find("unknown log level, turn on failed.") != std::string::npos),
        "on 非法级别 eOk + 错误提示（default 活分支）");
    auto allResp = YomkAPI::LOGGER_INFO_ALL();
    auto listPkg = yomk::unPack<yomkmsg::LoggerInfoList>(allResp.m_data);
    const yomk::LoggerState* st = firstConsoleState(listPkg);
    YOMK_TEST_CHECK(
        (st != nullptr && st->m_debugOn && st->m_infoOn && st->m_warnOn && st->m_errorOn && st->m_proxyOn),
        "非法级别 off/on 无副作用（ALL 控制台状态仍全 on）");
}
}

// ============ Section 7: 异常路径（空包 / 错类型包 / 未知端点） ============
{
    // console_log 收 nullptr 包 → unPack 空包分支 eNo
    {
        CoutCapture cap;
        auto r = YomkAPI::request("/YomkLogger/console_log", nullptr);
        YOMK_TEST_CHECK(r.m_status == YomkResponse::eNo, "console_log nullptr 包返回 eNo");
        YOMK_TEST_CHECK(r.m_msg == "pkg is null or pkg is not Log", "console_log 空包契约消息");
    }
    // console_log 收错类型包（String）→ 同分支 eNo
    {
        auto r = YomkAPI::request("/YomkLogger/console_log", yomkmsg::String::shared(std::string("wrong")));
        YOMK_TEST_CHECK(r.m_status == YomkResponse::eNo, "console_log 错类型包返回 eNo");
        YOMK_TEST_CHECK(r.m_msg == "pkg is null or pkg is not Log", "console_log 错类型包契约消息");
    }
    // file_log / write_file_log / create_file_logger / logger 空包
    YOMK_TEST_CHECK(YomkAPI::request("/YomkLogger/file_log", nullptr).m_status == YomkResponse::eNo, "file_log nullptr 包返回 eNo");
    YOMK_TEST_CHECK(
        YomkAPI::request("/YomkLogger/write_file_log", nullptr).m_status == YomkResponse::eNo,
        "write_file_log nullptr 包返回 eNo");
    YOMK_TEST_CHECK(
        YomkAPI::request("/YomkLogger/create_file_logger", nullptr).m_status == YomkResponse::eNo,
        "create_file_logger nullptr 包返回 eNo");
    YOMK_TEST_CHECK(YomkAPI::request("/YomkLogger/logger", nullptr).m_status == YomkResponse::eNo, "logger nullptr 包返回 eNo");
    // console_log 空 logger 名 → 回退 MainLogger 分支（宏形态 tag 恒非空，raw 注入才可触达）
    // 注：YOMK_TEST_CHECK 须在捕获作用域外（否则断言输出被 rdbuf 重定向吞没）
    {
        std::string out;
        YomkResponse r(YomkResponse::eInvalid);
        {
            CoutCapture cap;
            r = YomkAPI::request(
                "/YomkLogger/console_log",
                yomkmsg::Log::shared(yomk::Log{yomk::Log::eInfo, "lg1_s7_empty_logger_marker", ""}));
            out = cap.str();
        }
        YOMK_TEST_CHECK(r.m_status == YomkResponse::eOk, "console_log 空 logger 名返回 eOk（回退分支）");
        YOMK_TEST_CHECK(
            out.find("console logger name is empty, use MainLogger") != std::string::npos,
            "空 logger 名错误提示（回退 MainLogger）");
        YOMK_TEST_CHECK(
            out.find("[MainLogger] lg1_s7_empty_logger_marker") != std::string::npos,
            "空 logger 名回退 MainLogger 输出");
    }
    // file_log 空 logger 名 → eNo 分支（宏形态 file 参可传空串，raw 注入等价触达）
    {
        std::string out;
        YomkResponse r(YomkResponse::eInvalid);
        {
            CoutCapture cap;
            r = YomkAPI::request("/YomkLogger/file_log", yomkmsg::Log::shared(yomk::Log{yomk::Log::eInfo, "x", ""}));
            out = cap.str();
        }
        YOMK_TEST_CHECK(
            (r.m_status == YomkResponse::eNo && r.m_msg == "file logger name is empty."),
            "file_log 空 logger 名返回 eNo + 契约消息");
        YOMK_TEST_CHECK(out.find("file logger name is empty.") != std::string::npos, "file_log 空名错误提示");
    }
    // LOGGER_INFO_LOGGER 空名 → eNo（LG2 堵死空名注册后回填；
    // LG1 时为 P2-a 幽灵 logger 命中 eOk 的现状固化）
    auto emptyNameInfo = YomkAPI::LOGGER_INFO_LOGGER("", "console");
    YOMK_TEST_CHECK(
        (emptyNameInfo.m_status == YomkResponse::eNo && emptyNameInfo.m_msg == "logger not found."),
        "LOGGER 空名返回 eNo + logger not found.（P2-a 处置后无幽灵 key）");
    // 未知端点 → invoke 路由层 eNo（服务前缀被路由剥离，消息仅含端点名）
    auto unknown = YomkAPI::request("/YomkLogger/no_such_func", nullptr);
    YOMK_TEST_CHECK(unknown.m_status == YomkResponse::eNo, "未知端点返回 eNo（invoke not found）");
    YOMK_TEST_CHECK(unknown.m_msg == "function not found: /no_such_func", "未知端点契约消息（路由剥离服务前缀）");
}

// ============ Section 8: logger 删除接口（LG4 新增 /delete_logger 全语义） ============
{
    // 控制台写入一律经此 lambda：捕获作用域内调用、作用域外取响应，
    // 既避免输出噪声，也规避"YOMK_TEST_CHECK 在捕获域内被吞没"（LG1 教训）
    auto consoleInfo = [](const std::string& logger, const std::string& marker)
    {
        YomkResponse r(YomkResponse::eInvalid);
        CoutCapture cap;
        r = YomkAPI::CONSOLE_LOG_INFO_TAG(logger, marker);
        return r;
    };

    // 8.1 空名 → eInvalid（与 createFileLogger 的 P2-a 空名惯例同口径）
    {
        std::string out;
        YomkResponse r(YomkResponse::eOk);
        {
            CoutCapture cap;
            r = YomkAPI::FILE_LOG_DELETE("");
            out = cap.str();
        }
        YOMK_TEST_CHECK(
            (r.m_status == YomkResponse::eInvalid && r.m_msg == "logger name is empty."),
            "S8: 删除空名返回 eInvalid + 契约消息");
        YOMK_TEST_CHECK(out.find("logger name is empty.") != std::string::npos, "S8: 空名删除错误提示");
    }

    // 8.2 两表均未命中 → eNo + not-found 契约消息（对齐 writeFileLog/loggerInfo）
    {
        std::string out;
        YomkResponse r(YomkResponse::eOk);
        {
            CoutCapture cap;
            r = YomkAPI::FILE_LOG_DELETE("lg1_s8_never_created");
            out = cap.str();
        }
        YOMK_TEST_CHECK(
            (r.m_status == YomkResponse::eNo && r.m_msg == "logger not found."),
            "S8: 删除未注册 logger 返回 eNo + logger not found.");
        YOMK_TEST_CHECK(
            out.find("logger: lg1_s8_never_created not found.") != std::string::npos, "S8: 未命中错误提示含 logger 名");
    }

    // 8.3 console-only：msg 计数 1/0；删除后单查 eNo；同名再写惰性重建
    {
        const std::string name = "lg1_s8_console_only";
        YOMK_TEST_CHECK(
            consoleInfo(name, "lg1_s8_console_marker").m_status == YomkResponse::eOk, "S8: console-only 预热创建 eOk");
        YOMK_TEST_CHECK(YomkAPI::LOGGER_INFO_LOGGER(name, "console").m_status == YomkResponse::eOk, "S8: 创建后单查命中 eOk");

        auto del = YomkAPI::FILE_LOG_DELETE(name);
        YOMK_TEST_CHECK(
            (del.m_status == YomkResponse::eOk && del.m_msg == "deleted console:1 file:0"),
            "S8: console-only 删除 eOk + msg 计数: " + del.m_msg);
        YOMK_TEST_CHECK(YomkAPI::LOGGER_INFO_LOGGER(name, "console").m_status == YomkResponse::eNo, "S8: console 删除后单查 eNo");
        // 惰性重建：同名再写一条 → consoleLog 双检锁重建路径
        YOMK_TEST_CHECK(
            consoleInfo(name, "lg1_s8_rebuild_marker").m_status == YomkResponse::eOk,
            "S8: 删除后同名再写 eOk（惰性重建）");
        YOMK_TEST_CHECK(YomkAPI::LOGGER_INFO_LOGGER(name, "console").m_status == YomkResponse::eOk, "S8: 惰性重建后单查恢复 eOk");
        YOMK_TEST_CHECK(YomkAPI::FILE_LOG_DELETE(name).m_status == YomkResponse::eOk, "S8: 清理重建 logger eOk");
    }

    // 8.4 file-only：写 2 行不 flush → 删除释放最后引用 → ~FileLogger 锁外自动落盘
    {
        fs::path dir = tmpDir / "s8_file";
        fs::create_directories(dir, ec);
        const std::string name = "lg1_s8_file_only";
        YOMK_TEST_CHECK(YomkAPI::FILE_LOG_CREATE(dir.string(), name).m_status == YomkResponse::eOk, "S8: file logger 创建 eOk");
        YOMK_TEST_CHECK(
            YOMK_FILE_INFO_TAG(name, "s8Tag", "lg1_s8_file_marker_1").m_status == YomkResponse::eOk,
            "S8: file 写入第 1 行 eOk");
        YOMK_TEST_CHECK(
            YOMK_FILE_INFO_TAG(name, "s8Tag", "lg1_s8_file_marker_2").m_status == YomkResponse::eOk,
            "S8: file 写入第 2 行 eOk");
        // 前置对照：未 flush 前 init 建的空文件仍为空
        bool okBefore = false;
        std::string contentBefore = readFile(dir / (name + ".log"), okBefore);
        YOMK_TEST_CHECK((okBefore && contentBefore.empty()), "S8: 删除前未 flush，文件仍为空");

        auto del = YomkAPI::FILE_LOG_DELETE(name);
        YOMK_TEST_CHECK(
            (del.m_status == YomkResponse::eOk && del.m_msg == "deleted console:0 file:1"),
            "S8: file-only 删除 eOk + msg 计数: " + del.m_msg);
        bool okAfter = false;
        std::string content = readFile(dir / (name + ".log"), okAfter);
        YOMK_TEST_CHECK((okAfter && countLines(content) == 2), "S8: 删除触发析构自动落盘恰 2 行");
        YOMK_TEST_CHECK(
            (content.find("lg1_s8_file_marker_1") != std::string::npos &&
             content.find("lg1_s8_file_marker_2") != std::string::npos),
            "S8: 落盘内容含两条 marker（数据不因删除丢失）");
        YOMK_TEST_CHECK(YomkAPI::FILE_LOG_WRITE(name).m_status == YomkResponse::eNo, "S8: 删除后 WRITE 同名返回 eNo");
    }

    // 8.5 同名双表：一次删除同时清理两表（msg 计数 1/1），重复删除幂等 eNo
    {
        fs::path dir = tmpDir / "s8_both";
        fs::create_directories(dir, ec);
        const std::string name = "lg1_s8_both_tables";
        YOMK_TEST_CHECK(
            consoleInfo(name, "lg1_s8_both_console_marker").m_status == YomkResponse::eOk,
            "S8: 双表用例 console 侧创建 eOk");
        YOMK_TEST_CHECK(YomkAPI::FILE_LOG_CREATE(dir.string(), name).m_status == YomkResponse::eOk, "S8: 双表用例 file 侧创建 eOk");

        auto del = YomkAPI::FILE_LOG_DELETE(name);
        YOMK_TEST_CHECK(
            (del.m_status == YomkResponse::eOk && del.m_msg == "deleted console:1 file:1"),
            "S8: 同名双表一次删除 eOk + msg 计数: " + del.m_msg);
        YOMK_TEST_CHECK(
            (YomkAPI::LOGGER_INFO_LOGGER(name, "console").m_status == YomkResponse::eNo &&
             YomkAPI::LOGGER_INFO_LOGGER(name, "file").m_status == YomkResponse::eNo),
            "S8: 双表删除后按类型单查均 eNo（两表均已清理）");
        std::string out2;
        YomkResponse del2(YomkResponse::eOk);
        {
            CoutCapture cap;
            del2 = YomkAPI::FILE_LOG_DELETE(name);
            out2 = cap.str();
        }
        YOMK_TEST_CHECK(
            (del2.m_status == YomkResponse::eNo && del2.m_msg == "logger not found."), "S8: 重复删除返回 eNo（幂等）");
        YOMK_TEST_CHECK(
            out2.find("logger: lg1_s8_both_tables not found.") != std::string::npos,
            "S8: 重复删除错误提示含 logger 名");
    }

    // 8.6 内省行数递减守恒 + ALL 与 LOGGERS 行数关系 + MainLogger 删除后惰性重建
    {
        fs::path dir = tmpDir / "s8_count";
        fs::create_directories(dir, ec);
        const std::string name = "lg1_s8_count";
        const size_t before = loggersTotal();
        YOMK_TEST_CHECK(
            consoleInfo(name, "lg1_s8_count_marker").m_status == YomkResponse::eOk, "S8: 计数用例 console 侧创建 eOk");
        YOMK_TEST_CHECK(YomkAPI::FILE_LOG_CREATE(dir.string(), name).m_status == YomkResponse::eOk, "S8: 计数用例 file 侧创建 eOk");
        YOMK_TEST_CHECK(loggersTotal() == before + 2, "S8: 双表各建一个 → LOGGERS 行数 +2");
        YOMK_TEST_CHECK(YomkAPI::FILE_LOG_DELETE(name).m_status == YomkResponse::eOk, "S8: 计数用例删除 eOk");
        YOMK_TEST_CHECK(loggersTotal() == before, "S8: 删除后 LOGGERS 行数回落（递减守恒）");
        auto listPkg = yomk::unPack<yomkmsg::LoggerInfoList>(YomkAPI::LOGGER_INFO_ALL().m_data);
        const yomk::LoggerState* st = firstConsoleState(listPkg);
        YOMK_TEST_CHECK(
            (listPkg != nullptr && listPkg->d.size() == before && st != nullptr && st->m_debugOn &&
             st->m_infoOn && st->m_warnOn && st->m_errorOn && st->m_proxyOn),
            "S8: ALL 日志器数 = LOGGERS 行数，控制台状态全 on 不变");

        // MainLogger 无特殊保护：可删；重建须走"空 logger 名回退"路径
        // （YOMK_INFO 宏的 logger 名是裸 "MainLogger"，[行号] 拼在内容头部）
        YOMK_TEST_CHECK(
            YomkAPI::FILE_LOG_DELETE("MainLogger").m_status == YomkResponse::eOk,
            "S8: MainLogger 可删除 eOk（无特殊保护）");
        YOMK_TEST_CHECK(YomkAPI::LOGGER_INFO_LOGGER("MainLogger", "console").m_status == YomkResponse::eNo, "S8: MainLogger 删除后单查 eNo");
        std::string rebuildOut;
        YomkResponse rebuild(YomkResponse::eInvalid);
        {
            CoutCapture cap;
            rebuild = YomkAPI::request(
                "/YomkLogger/console_log",
                yomkmsg::Log::shared(yomk::Log{yomk::Log::eInfo, "lg1_s8_mainlogger_rebuild_marker", ""}));
            rebuildOut = cap.str();
        }
        YOMK_TEST_CHECK(rebuild.m_status == YomkResponse::eOk, "S8: MainLogger 删除后空名回退写入 eOk");
        YOMK_TEST_CHECK(
            YomkAPI::LOGGER_INFO_LOGGER("MainLogger", "console").m_status == YomkResponse::eOk,
            "S8: MainLogger 惰性重建后单查恢复 eOk");
        YOMK_TEST_CHECK(
            rebuildOut.find("[MainLogger] lg1_s8_mainlogger_rebuild_marker") != std::string::npos,
            "S8: 重建对象名确为 MainLogger（输出行 [MainLogger]）");
        YOMK_TEST_CHECK(
            loggersTotal() == before,
            "S8: MainLogger 删除+重建后行数回到基线（无泄漏无重复）");
    }

    // 8.7 异常路径：空包 / 错类型包（unPack 契约分支）
    {
        auto rNull = YomkAPI::request("/YomkLogger/delete_logger", nullptr);
        YOMK_TEST_CHECK(
            (rNull.m_status == YomkResponse::eNo && rNull.m_msg == "pkg is null or pkg is not String"),
            "S8: delete_logger nullptr 包返回 eNo + 契约消息");
        auto rWrong = YomkAPI::request("/YomkLogger/delete_logger", yomkmsg::Log::shared(yomk::Log{yomk::Log::eInfo, "x", "y"}));
        YOMK_TEST_CHECK(
            (rWrong.m_status == YomkResponse::eNo && rWrong.m_msg == "pkg is null or pkg is not String"),
            "S8: delete_logger 错类型包（Log）返回 eNo + 契约消息");
    }
}

// ============ Section 9: 服务级具名开关与全局态端点（两级主闸并联） ============
{
    const std::string tagA = "lg1_s9_tagA";
    const std::string tagB = "lg1_s9_tagB";

    // 9.1 具名 OFF 隔离：关 tagA 的 info，tagB 输出不受影响
    {
        CoutCapture cap;
        YOMK_TEST_CHECK(YOMK_INFO_TAG(tagA, "lg1_s9a_pre_a_marker").m_status == YomkResponse::eOk, "S9a: tagA 预写 eOk");
        YOMK_TEST_CHECK(YOMK_INFO_TAG(tagB, "lg1_s9a_pre_b_marker").m_status == YomkResponse::eOk, "S9a: tagB 预写 eOk");
    }
    YOMK_TEST_CHECK(
        YomkAPI::OFF_CONSOLE_LOG_INFO(tagA).m_status == YomkResponse::eOk,
        "S9a: 具名 OFF_CONSOLE_LOG_INFO(tagA) 返回 eOk");
    {
        CoutCapture cap;
        YOMK_TEST_CHECK(
            YOMK_INFO_TAG(tagA, "lg1_s9a_off_a_marker").m_status == YomkResponse::eOk,
            "S9a: 具名关断后 tagA 调用仍 eOk");
        YOMK_TEST_CHECK(
            YOMK_INFO_TAG(tagB, "lg1_s9a_off_b_marker").m_status == YomkResponse::eOk,
            "S9a: 具名关断后 tagB 调用 eOk");
        YOMK_TEST_CHECK(cap.str().find("lg1_s9a_off_a_marker") == std::string::npos, "S9a: 具名关断后 tagA 无输出");
        YOMK_TEST_CHECK(cap.str().find("lg1_s9a_off_b_marker") != std::string::npos, "S9a: 具名关断不波及 tagB（逐器隔离）");
    }

    // 9.2 具名 ON 恢复
    YOMK_TEST_CHECK(
        YomkAPI::ON_CONSOLE_LOG_INFO(tagA).m_status == YomkResponse::eOk,
        "S9b: 具名 ON_CONSOLE_LOG_INFO(tagA) 返回 eOk");
    {
        CoutCapture cap;
        YOMK_TEST_CHECK(YOMK_INFO_TAG(tagA, "lg1_s9b_on_a_marker").m_status == YomkResponse::eOk, "S9b: 恢复后 tagA 调用 eOk");
        YOMK_TEST_CHECK(cap.str().find("lg1_s9b_on_a_marker") != std::string::npos, "S9b: 具名恢复后 tagA 输出回归");
    }

    // 9.3 具名未知名 eNo（不惰性创建）
    {
        auto offNo = YomkAPI::OFF_CONSOLE_LOG_INFO("lg1_s9_never_created");
        YOMK_TEST_CHECK(
            (offNo.m_status == YomkResponse::eNo && offNo.m_msg == "console logger not found."),
            "S9c: 具名 OFF 未知名返回 eNo + console logger not found.");
        auto onNo = YomkAPI::ON_CONSOLE_LOG_INFO("lg1_s9_never_created");
        YOMK_TEST_CHECK(
            (onNo.m_status == YomkResponse::eNo && onNo.m_msg == "console logger not found."),
            "S9c: 具名 ON 未知名返回 eNo");
        YOMK_TEST_CHECK(
            YomkAPI::LOGGER_INFO_LOGGER("lg1_s9_never_created", "console").m_status == YomkResponse::eNo,
            "S9c: 未知名未被惰性创建（单查 eNo）");
    }

    // 9.4 主闸×逐器组合 + 两层归因：全局 OFF + 逐器 ON → 仍无输出（两层 AND），
    // 全局态与条目逐器态各自真实呈现、归因唯一
    {
        YOMK_TEST_CHECK(YomkAPI::OFF_CONSOLE_LOG_INFO().m_status == YomkResponse::eOk, "S9d: 无参 OFF（全局主闸）返回 eOk");
        {
            CoutCapture cap;
            YOMK_TEST_CHECK(
                YOMK_INFO_TAG(tagA, "lg1_s9d_global_off_a_marker").m_status == YomkResponse::eOk,
                "S9d: 主闸关断后 tagA 调用仍 eOk");
            YOMK_TEST_CHECK(
                cap.str().find("lg1_s9d_global_off_a_marker") == std::string::npos,
                "S9d: 主闸 OFF + 逐器 ON → 仍无输出（两层 AND）");
        }
        auto gResp = YomkAPI::LOGGER_INFO_GLOBAL();
        auto gs = yomk::unPack<yomkmsg::LoggerState>(gResp.m_data);
        YOMK_TEST_CHECK(gs != nullptr && !gs->d.m_infoOn, "S9d: LOGGER_INFO_GLOBAL 报主闸 infoOff");
        auto liResp = YomkAPI::LOGGER_INFO_LOGGER(tagA, "console");
        auto lp = yomk::unPack<yomkmsg::LoggerInfo>(liResp.m_data);
        YOMK_TEST_CHECK(
            lp != nullptr && lp->d.m_loggerState.m_infoOn,
            "S9d: 条目报逐器 infoOn（与主闸态相反，两层归因唯一）");
        YOMK_TEST_CHECK(YomkAPI::ON_CONSOLE_LOG_INFO().m_status == YomkResponse::eOk, "S9d: 无参 ON（全局主闸）恢复 eOk");
        {
            CoutCapture cap;
            YOMK_TEST_CHECK(
                YOMK_INFO_TAG(tagA, "lg1_s9d_global_on_a_marker").m_status == YomkResponse::eOk,
                "S9d: 主闸恢复后 tagA 调用 eOk");
            YOMK_TEST_CHECK(
                cap.str().find("lg1_s9d_global_on_a_marker") != std::string::npos,
                "S9d: 主闸恢复后输出回归（逐器仍 on）");
        }
    }

    // 9.5 条目逐器真值与两层正交：具名关断直接反映在条目、不波及全局态
    {
        auto liAResp = YomkAPI::LOGGER_INFO_LOGGER(tagA, "console");
        auto la = yomk::unPack<yomkmsg::LoggerInfo>(liAResp.m_data);
        YOMK_TEST_CHECK(la != nullptr && la->d.m_loggerState.m_infoOn, "S9e: tagA 条目 infoOn（具名恢复后逐器态）");
        auto liBResp = YomkAPI::LOGGER_INFO_LOGGER(tagB, "console");
        auto lb = yomk::unPack<yomkmsg::LoggerInfo>(liBResp.m_data);
        YOMK_TEST_CHECK(
            lb != nullptr && lb->d.m_loggerState.m_infoOn,
            "S9e: tagB 条目 infoOn（未被具名触碰恒 true）");
        YOMK_TEST_CHECK(YomkAPI::OFF_CONSOLE_LOG_INFO(tagA).m_status == YomkResponse::eOk, "S9e: 再次具名关断 tagA 返回 eOk");
        auto gResp = YomkAPI::LOGGER_INFO_GLOBAL();
        auto gs = yomk::unPack<yomkmsg::LoggerState>(gResp.m_data);
        YOMK_TEST_CHECK(gs != nullptr && gs->d.m_infoOn, "S9e: 具名关断不影响全局主闸（两层正交）");
        auto liA2Resp = YomkAPI::LOGGER_INFO_LOGGER(tagA, "console");
        auto la2 = yomk::unPack<yomkmsg::LoggerInfo>(liA2Resp.m_data);
        YOMK_TEST_CHECK(
            la2 != nullptr && !la2->d.m_loggerState.m_infoOn,
            "S9e: tagA 条目报逐器 infoOff（具名关断直接反映）");
        YOMK_TEST_CHECK(YomkAPI::ON_CONSOLE_LOG_INFO(tagA).m_status == YomkResponse::eOk, "S9e: 具名恢复 tagA 返回 eOk（状态复原）");
    }

    // 9.6 全局态端点基线：LOGGER_INFO_GLOBAL 解 LoggerState 包 + 无参宏翻转联动
    {
        auto gResp = YomkAPI::LOGGER_INFO_GLOBAL();
        YOMK_TEST_CHECK(
            (gResp.m_status == YomkResponse::eOk && gResp.m_msg == "ok"),
            "S9f: LOGGER_INFO_GLOBAL 返回 eOk + ok");
        auto gs = yomk::unPack<yomkmsg::LoggerState>(gResp.m_data);
        YOMK_TEST_CHECK(
            (gs != nullptr && gs->d.m_debugOn && gs->d.m_infoOn && gs->d.m_warnOn && gs->d.m_errorOn),
            "S9f: 全局主闸四级全 on");
        YOMK_TEST_CHECK(gs != nullptr && gs->d.m_proxyOn, "S9f: 全局态含 S3 已设 proxyOn");
        YOMK_TEST_CHECK(YomkAPI::OFF_CONSOLE_LOG_DEBUG().m_status == YomkResponse::eOk, "S9f: 无参 OFF_DEBUG 返回 eOk");
        auto gResp2 = YomkAPI::LOGGER_INFO_GLOBAL();
        auto gs2 = yomk::unPack<yomkmsg::LoggerState>(gResp2.m_data);
        YOMK_TEST_CHECK(gs2 != nullptr && !gs2->d.m_debugOn, "S9f: 主闸 OFF_DEBUG 后全局态 debugOff");
        YOMK_TEST_CHECK(YomkAPI::ON_CONSOLE_LOG_DEBUG().m_status == YomkResponse::eOk, "S9f: 无参 ON_DEBUG 恢复 eOk");
        auto gResp3 = YomkAPI::LOGGER_INFO_GLOBAL();
        auto gs3 = yomk::unPack<yomkmsg::LoggerState>(gResp3.m_data);
        YOMK_TEST_CHECK(gs3 != nullptr && gs3->d.m_debugOn, "S9f: 主闸 ON_DEBUG 后全局态 debugOn");
    }
}

// ============ Section 10: 文件日志两级开关与全局态端点 ============
{
    const std::string flName = "lg1_s10_fl";
    fs::path s10Dir = tmpDir / "s10";
    fs::create_directories(s10Dir, ec);
    YOMK_TEST_CHECK(
        YomkAPI::FILE_LOG_CREATE(s10Dir.string(), flName).m_status == YomkResponse::eOk,
        "S10: 建文件日志器 lg1_s10_fl 成功");
    fs::path s10LogFile = s10Dir / (flName + ".log");

    // 10.1 具名 OFF 隔离：关 lg1_s10_fl 的 info，warn 落盘不受影响
    YOMK_TEST_CHECK(
        YomkAPI::OFF_FILE_LOG_INFO(flName).m_status == YomkResponse::eOk,
        "S10a: 具名 OFF_FILE_LOG_INFO(fl) 返回 eOk");
    {
        auto liResp = YomkAPI::LOGGER_INFO_LOGGER(flName, "file");
        auto lp = yomk::unPack<yomkmsg::LoggerInfo>(liResp.m_data);
        YOMK_TEST_CHECK(
            lp != nullptr && !lp->d.m_loggerState.m_infoOn && lp->d.m_loggerState.m_warnOn,
            "S10a: 单查条目 infoOff 且 warnOn（文件逐器门控真值）");
    }
    YOMK_TEST_CHECK(
        YOMK_FILE_INFO_TAG(flName, "lg1s10", "lg1_s10a_off_marker").m_status == YomkResponse::eOk,
        "S10a: 关断后 FILE_INFO 仍 eOk");
    YOMK_TEST_CHECK(
        YOMK_FILE_WARN_TAG(flName, "lg1s10", "lg1_s10a_warn_marker").m_status == YomkResponse::eOk,
        "S10a: WARN 写入 eOk");
    YOMK_TEST_CHECK(YomkAPI::FILE_LOG_WRITE(flName).m_status == YomkResponse::eOk, "S10a: WRITE eOk");
    {
        bool ok = false;
        std::string content = readFile(s10LogFile, ok);
        YOMK_TEST_CHECK(
            ok && content.find("lg1_s10a_off_marker") == std::string::npos,
            "S10a: 关断级别不落盘（marker 缺席）");
        YOMK_TEST_CHECK(
            ok && content.find("lg1_s10a_warn_marker") != std::string::npos,
            "S10a: 未关断级别落盘不受影响");
    }

    // 10.2 具名 ON 恢复
    YOMK_TEST_CHECK(
        YomkAPI::ON_FILE_LOG_INFO(flName).m_status == YomkResponse::eOk,
        "S10b: 具名 ON_FILE_LOG_INFO(fl) 返回 eOk");
    {
        auto liResp = YomkAPI::LOGGER_INFO_LOGGER(flName, "file");
        auto lp = yomk::unPack<yomkmsg::LoggerInfo>(liResp.m_data);
        YOMK_TEST_CHECK(lp != nullptr && lp->d.m_loggerState.m_infoOn, "S10b: 条目 infoOn 恢复");
    }
    YOMK_TEST_CHECK(
        YOMK_FILE_INFO_TAG(flName, "lg1s10", "lg1_s10b_on_marker").m_status == YomkResponse::eOk,
        "S10b: 恢复后 FILE_INFO eOk");
    YOMK_TEST_CHECK(YomkAPI::FILE_LOG_WRITE(flName).m_status == YomkResponse::eOk, "S10b: WRITE eOk");
    {
        bool ok = false;
        std::string content = readFile(s10LogFile, ok);
        YOMK_TEST_CHECK(ok && content.find("lg1_s10b_on_marker") != std::string::npos, "S10b: 恢复后落盘回归");
    }

    // 10.3 具名未知名 eNo（不惰性创建）
    {
        auto offNo = YomkAPI::OFF_FILE_LOG_INFO("lg1_s10_never_created");
        YOMK_TEST_CHECK(
            (offNo.m_status == YomkResponse::eNo && offNo.m_msg == "file logger not found."),
            "S10c: 具名 OFF 未知名返回 eNo + file logger not found.");
        auto onNo = YomkAPI::ON_FILE_LOG_INFO("lg1_s10_never_created");
        YOMK_TEST_CHECK(
            (onNo.m_status == YomkResponse::eNo && onNo.m_msg == "file logger not found."),
            "S10c: 具名 ON 未知名返回 eNo");
        auto arr = yomk::unPack<yomkmsg::StringArray>(YomkAPI::LOGGER_INFO_LOGGERS("file").m_data);
        bool ghostFound = false;
        if (arr)
        {
            for (const auto& n : arr->d)
            {
                if (n == "lg1_s10_never_created")
                {
                    ghostFound = true;
                }
            }
        }
        YOMK_TEST_CHECK(!ghostFound, "S10c: 未知名未被惰性创建（LOGGERS 无该名）");
    }

    // 10.4 主闸×逐器组合 + 两层归因：文件主闸 OFF + 逐器 ON → 仍不落盘（两层 AND），
    // 文件全局态与条目逐器态各自真实呈现、归因唯一
    {
        YOMK_TEST_CHECK(YomkAPI::OFF_FILE_LOG_INFO().m_status == YomkResponse::eOk, "S10d: 无参 OFF（文件主闸）返回 eOk");
        auto gResp = YomkAPI::LOGGER_INFO_GLOBAL("file");
        auto gs = yomk::unPack<yomkmsg::LoggerState>(gResp.m_data);
        YOMK_TEST_CHECK(gs != nullptr && !gs->d.m_infoOn, "S10d: LOGGER_INFO_GLOBAL(\"file\") 报文件主闸 infoOff");
        auto liResp = YomkAPI::LOGGER_INFO_LOGGER(flName, "file");
        auto lp = yomk::unPack<yomkmsg::LoggerInfo>(liResp.m_data);
        YOMK_TEST_CHECK(
            lp != nullptr && lp->d.m_loggerState.m_infoOn,
            "S10d: 条目报逐器 infoOn（与文件主闸态相反，两层归因唯一）");
        YOMK_TEST_CHECK(
            YOMK_FILE_INFO_TAG(flName, "lg1s10", "lg1_s10d_gate_marker").m_status == YomkResponse::eOk,
            "S10d: 主闸关断后 FILE_INFO 仍 eOk");
        YOMK_TEST_CHECK(YomkAPI::FILE_LOG_WRITE(flName).m_status == YomkResponse::eOk, "S10d: WRITE eOk");
        {
            bool ok = false;
            std::string content = readFile(s10LogFile, ok);
            YOMK_TEST_CHECK(
                ok && content.find("lg1_s10d_gate_marker") == std::string::npos,
                "S10d: 主闸 OFF + 逐器 ON → 仍不落盘（两层 AND）");
        }
        YOMK_TEST_CHECK(YomkAPI::ON_FILE_LOG_INFO().m_status == YomkResponse::eOk, "S10d: 无参 ON（文件主闸）恢复 eOk");
        YOMK_TEST_CHECK(
            YOMK_FILE_INFO_TAG(flName, "lg1s10", "lg1_s10d_on_marker").m_status == YomkResponse::eOk,
            "S10d: 主闸恢复后 FILE_INFO eOk");
        YOMK_TEST_CHECK(YomkAPI::FILE_LOG_WRITE(flName).m_status == YomkResponse::eOk, "S10d: WRITE eOk");
        {
            bool ok = false;
            std::string content = readFile(s10LogFile, ok);
            YOMK_TEST_CHECK(
                ok && content.find("lg1_s10d_on_marker") != std::string::npos,
                "S10d: 主闸恢复后落盘回归（逐器仍 on）");
        }
    }

    // 10.5 两层正交：具名关断直接反映在条目、不波及文件主闸态
    {
        YOMK_TEST_CHECK(YomkAPI::OFF_FILE_LOG_INFO(flName).m_status == YomkResponse::eOk, "S10e: 再次具名关断返回 eOk");
        auto gResp = YomkAPI::LOGGER_INFO_GLOBAL("file");
        auto gs = yomk::unPack<yomkmsg::LoggerState>(gResp.m_data);
        YOMK_TEST_CHECK(gs != nullptr && gs->d.m_infoOn, "S10e: 具名关断不影响文件主闸（两层正交）");
        YOMK_TEST_CHECK(YomkAPI::ON_FILE_LOG_INFO(flName).m_status == YomkResponse::eOk, "S10e: 具名恢复 eOk（状态复原）");
    }

    // 10.6 全局态端点基线：LOGGER_INFO_GLOBAL("file")/("console") 两套主闸独立 + 无参宏翻转联动
    {
        auto gResp = YomkAPI::LOGGER_INFO_GLOBAL("file");
        YOMK_TEST_CHECK(
            (gResp.m_status == YomkResponse::eOk && gResp.m_msg == "ok"),
            "S10f: LOGGER_INFO_GLOBAL(\"file\") 返回 eOk + ok");
        auto gs = yomk::unPack<yomkmsg::LoggerState>(gResp.m_data);
        YOMK_TEST_CHECK(
            (gs != nullptr && gs->d.m_debugOn && gs->d.m_infoOn && gs->d.m_warnOn && gs->d.m_errorOn),
            "S10f: 文件主闸四级全 on");
        YOMK_TEST_CHECK(
            gs != nullptr && !gs->d.m_proxyOn,
            "S10f: 文件态 proxyOn=false（文件代理未设置时恒 false）");
        YOMK_TEST_CHECK(YomkAPI::OFF_FILE_LOG_DEBUG().m_status == YomkResponse::eOk, "S10f: 无参 OFF_FILE_LOG_DEBUG 返回 eOk");
        auto gResp2 = YomkAPI::LOGGER_INFO_GLOBAL("file");
        auto gs2 = yomk::unPack<yomkmsg::LoggerState>(gResp2.m_data);
        YOMK_TEST_CHECK(gs2 != nullptr && !gs2->d.m_debugOn, "S10f: 文件主闸 OFF_DEBUG 后文件态 debugOff");
        // 两套主闸独立直证：文件主闸翻转不波及控制台主闸
        auto gResp3 = YomkAPI::LOGGER_INFO_GLOBAL("console");
        auto gc = yomk::unPack<yomkmsg::LoggerState>(gResp3.m_data);
        YOMK_TEST_CHECK(
            (gc != nullptr && gc->d.m_debugOn && gc->d.m_infoOn && gc->d.m_warnOn && gc->d.m_errorOn),
            "S10f: 控制台主闸四级仍全 on（与文件主闸独立）");
        YOMK_TEST_CHECK(YomkAPI::ON_FILE_LOG_DEBUG().m_status == YomkResponse::eOk, "S10f: 无参 ON_FILE_LOG_DEBUG 恢复 eOk");
        auto gResp4 = YomkAPI::LOGGER_INFO_GLOBAL("file");
        auto gs4 = yomk::unPack<yomkmsg::LoggerState>(gResp4.m_data);
        YOMK_TEST_CHECK(gs4 != nullptr && gs4->d.m_debugOn, "S10f: 文件主闸 ON_DEBUG 后文件态 debugOn");
        // 未知 type：客户端直返 eNo 不发请求
        auto badType = YomkAPI::LOGGER_INFO_GLOBAL("badtype");
        YOMK_TEST_CHECK(
            (badType.m_status == YomkResponse::eNo && badType.m_msg == "unknown logger type."),
            "S10f: 未知 type 返回 eNo unknown logger type.");
    }
}

// ============ Section 11: 文件日志全局代理契约（与控制台 S3 同构） ============
{
    const std::string flName11 = "lg1_s11_fl";
    fs::path s11Dir = tmpDir / "s11";
    fs::create_directories(s11Dir, ec);
    YOMK_TEST_CHECK(
        YomkAPI::FILE_LOG_CREATE(s11Dir.string(), flName11).m_status == YomkResponse::eOk,
        "S11: 建文件日志器 lg1_s11_fl 成功");
    fs::path s11LogFile = s11Dir / (flName11 + ".log");

    // 11.1 设置前基线：文件代理未设置 + 两套代理独立直证
    {
        auto gRespF = YomkAPI::LOGGER_INFO_GLOBAL("file");
        auto gsF = yomk::unPack<yomkmsg::LoggerState>(gRespF.m_data);
        YOMK_TEST_CHECK(gsF != nullptr && !gsF->d.m_proxyOn, "S11a: 设置前 GLOBAL(\"file\") proxyOff");
        auto gRespC = YomkAPI::LOGGER_INFO_GLOBAL("console");
        auto gsC = yomk::unPack<yomkmsg::LoggerState>(gRespC.m_data);
        YOMK_TEST_CHECK(
            gsC != nullptr && gsC->d.m_proxyOn,
            "S11a: 设置前 GLOBAL(\"console\") proxyOn（S3 重装态），两套代理独立直证");
    }

    // 11.2 拦截态：文件 proxy 返回 false → eOk + 专属消息 + 不落盘
    g_fileProxyHits.store(0);
    g_fileProxyIntercept.store(true);
    auto fileProxy = [](const yomk::Log&) -> bool
    {
        g_fileProxyHits.fetch_add(1);
        return !g_fileProxyIntercept.load();  // 拦截态返回 false，穿透态返回 true
    };
    YOMK_TEST_CHECK(YomkAPI::SET_FILE_LOG_PROXY(fileProxy).m_status == YomkResponse::eOk, "S11b: SET_FILE_LOG_PROXY 返回 eOk");
    {
        auto r = YOMK_FILE_INFO_TAG(flName11, "lg1s11", "lg1_s11b_intercept_marker");
        YOMK_TEST_CHECK(r.m_status == YomkResponse::eOk, "S11b: 拦截态 FILE_INFO 返回 eOk");
        YOMK_TEST_CHECK(r.m_msg == "file log proxy is success.", "S11b: 拦截态契约消息 file log proxy is success.");
        YOMK_TEST_CHECK(YomkAPI::FILE_LOG_WRITE(flName11).m_status == YomkResponse::eOk, "S11b: WRITE eOk");
        {
            bool ok = false;
            std::string content = readFile(s11LogFile, ok);
            YOMK_TEST_CHECK(
                ok && content.find("lg1_s11b_intercept_marker") == std::string::npos,
                "S11b: 拦截态不落盘（marker 缺席）");
        }
        YOMK_TEST_CHECK(g_fileProxyHits.load() == 1, "S11b: 文件 proxy 被调用 1 次（原子计数）");
    }

    // 11.3 穿透态：文件 proxy 返回 true → 走默认落盘；GLOBAL("file") proxy 联动
    {
        g_fileProxyIntercept.store(false);
        auto r = YOMK_FILE_INFO_TAG(flName11, "lg1s11", "lg1_s11c_pass_marker");
        YOMK_TEST_CHECK((r.m_status == YomkResponse::eOk && r.m_msg == "success."), "S11c: 穿透态 FILE_INFO eOk + success.");
        YOMK_TEST_CHECK(YomkAPI::FILE_LOG_WRITE(flName11).m_status == YomkResponse::eOk, "S11c: WRITE eOk");
        {
            bool ok = false;
            std::string content = readFile(s11LogFile, ok);
            YOMK_TEST_CHECK(
                ok && content.find("lg1_s11c_pass_marker") != std::string::npos,
                "S11c: 穿透态恢复默认落盘（marker 命中）");
        }
        YOMK_TEST_CHECK(g_fileProxyHits.load() == 2, "S11c: 文件 proxy 累计被调用 2 次");
        auto gRespF = YomkAPI::LOGGER_INFO_GLOBAL("file");
        auto gsF = yomk::unPack<yomkmsg::LoggerState>(gRespF.m_data);
        YOMK_TEST_CHECK(gsF != nullptr && gsF->d.m_proxyOn, "S11c: 设置后 GLOBAL(\"file\") proxyOn");
    }

    // 11.4 卸载（LG6 镜像）：先把代理切回拦截态确认在链路上，再传 nullptr 卸载
    {
        g_fileProxyIntercept.store(true);
        auto r = YOMK_FILE_INFO_TAG(flName11, "lg1s11", "lg1_s11d_pre_unload_marker");
        YOMK_TEST_CHECK(
            (r.m_status == YomkResponse::eOk && r.m_msg == "file log proxy is success."),
            "S11d: 卸载前拦截态契约消息（文件代理确实在链路上）");
        YOMK_TEST_CHECK(
            YomkAPI::SET_FILE_LOG_PROXY(nullptr).m_status == YomkResponse::eOk,
            "S11d: SET_FILE_LOG_PROXY(nullptr) 卸载返回 eOk");
        auto gRespF = YomkAPI::LOGGER_INFO_GLOBAL("file");
        auto gsF = yomk::unPack<yomkmsg::LoggerState>(gRespF.m_data);
        YOMK_TEST_CHECK(gsF != nullptr && !gsF->d.m_proxyOn, "S11d: 卸载后 GLOBAL(\"file\") 回 proxyOff");
        const long hitsBefore = g_fileProxyHits.load();
        YOMK_TEST_CHECK(
            YOMK_FILE_INFO_TAG(flName11, "lg1s11", "lg1_s11d_post_unload_marker").m_status == YomkResponse::eOk,
            "S11d: 卸载后 FILE_INFO eOk（走默认路径而非代理短路）");
        YOMK_TEST_CHECK(YomkAPI::FILE_LOG_WRITE(flName11).m_status == YomkResponse::eOk, "S11d: WRITE eOk");
        {
            bool ok = false;
            std::string content = readFile(s11LogFile, ok);
            YOMK_TEST_CHECK(
                ok && content.find("lg1_s11d_post_unload_marker") != std::string::npos,
                "S11d: 卸载后恢复默认落盘（marker 命中）");
        }
        YOMK_TEST_CHECK(g_fileProxyHits.load() == hitsBefore, "S11d: 卸载后代理不再被调用（原子命中数不增）");
    }

    // 11.5 重装可逆：重装回穿透态（Section 11 为末段，收尾无代理态依赖）
    {
        g_fileProxyIntercept.store(false);
        YOMK_TEST_CHECK(YomkAPI::SET_FILE_LOG_PROXY(fileProxy).m_status == YomkResponse::eOk, "S11e: 重装文件代理返回 eOk（卸载可逆）");
        auto gRespF = YomkAPI::LOGGER_INFO_GLOBAL("file");
        auto gsF = yomk::unPack<yomkmsg::LoggerState>(gRespF.m_data);
        YOMK_TEST_CHECK(gsF != nullptr && gsF->d.m_proxyOn, "S11e: 重装后 GLOBAL(\"file\") 恢复 proxyOn");
    }
}

// ============ 收尾清理 ============
YomkAPI::shutdown();
fs::remove_all(tmpDir, ec);

if (g_failed == 0)
{
    std::cout << "TestYomkLoggerLifecycle all check passed." << std::endl;
    return 0;
}
std::cout << "TestYomkLoggerLifecycle FAILED (" << g_failed << " checks failed)." << std::endl;
return 1;
}
