// Logger 模块生命周期与契约基线测试
//
// 覆盖内容（11 个 Section + 1 个前置注入段，断言计数以运行实测为准）：
// 1. 控制台日志基线：六级别链式发射输出格式（时间戳+[级别]+[logger]+[行号]）；
// 级别开关 OFF→无输出仍 ResStatus::Succeeded→ON→恢复输出，六级别全覆盖
// 2. 自定义 tag：新 tag 自动创建 console logger；65536 超长 tag；空内容日志
// 2.5 非法级别 default 降级：proxy 设置前 raw 注入 level=99 →
// console_log default 降级 Info 输出 + ResStatus::Succeeded（S3 的 proxy 处于拦截态时会短路一切
// console_log，故该 default 分支在本窗口触达）
// 3. proxy 契约：console().proxy 拦截（false→ResStatus::Succeeded+"console log proxy is success."+无输出）
// 与穿透（true→默认输出）；原子计数验证；all 内省 proxy:on；
// 卸载：传 nullptr → ResStatus::Succeeded、all 首行回 proxy:off、恢复框架默认输出、
// 代理不再被调用（原子命中数不增）；重装可逆。用例末尾重装并回到穿透态，
// 以维持 S4-S7 依赖的「proxy:on + 穿透」不变量
// 4. 文件日志闭环：CREATE 建空文件→四级别入缓冲（write 前文件仍空）→WRITE 落盘读回断言
// 行数与行格式（含 FILE_INFO_TAG 自定义 tag 变体）；重复创建 ResStatus::Failed；未创建 logger 的
// to_sink 发射/flush ResStatus::Failed（空名/空 dir ResStatus::Invalid、不可建 dir ResStatus::Failed
// 且不注册幽灵 logger、失败后同名重试成功）
// 5. 内省三端点：LOGGERS 按类型（console/file）纯名列表 / LOGGER(name) console/file 两格式与未注册 ResStatus::Failed /
// ALL 控制台状态段五 bool 与开关翻转联动 + 日志器清单段
// 6. 边界值：变参 fold 传 INT_MAX/INT_MIN/0/-1/NaN/Inf/-Inf/-0.0；1MB 超大内容 console+file
// 双路径；非法级别注入（99 为合法值）：file_log/off/on 三处 default
// 降级与无副作用实证
// 7. 异常路径：console_log 收 nullptr 包 / 错类型包（String）均 ResStatus::Failed；console_log 空
// logger 名回退 main / file_log 空名 ResStatus::Failed（raw 注入触达）；LOGGER 空名 ResStatus::Failed
// （空名注册已被堵死）；未知端点 ResStatus::Failed
// 8. logger 删除接口（/delete_logger）：空名 ResStatus::Invalid、两表未命中 ResStatus::Failed +
// "logger not found."；console-only / file-only / 同名双表三种命中形态的 msg 计数
// （"deleted console:c file:f"）；file 侧删除释放最后引用 → ~FileLogger 锁外自动落盘
// （写 2 行未 flush 即删 → 文件恰 2 行，数据不丢）；重复删除幂等 ResStatus::Failed；删除后单查 ResStatus::Failed
// 与 LOGGERS/ALL 行数递减守恒；console 惰性重建；main 无特殊保护（可删，重建须走
// 空 logger 名回退路径——log().info 链的 logger 名是裸 "main"，[行号] 拼在内容头部）；
// delete_logger 空包/错类型包契约分支
// 9. 服务级具名开关与全局态端点（两级主闸并联）：具名 OFF/ON 逐器隔离与恢复、未知名 ResStatus::Failed
// （不惰性创建）、主闸×逐器组合两层归因（console().status() 报主闸 off 与条目逐器 on
// 各自真实呈现）、条目逐器真值与两层正交、console().status() 全局主闸态基线与主闸翻转联动
// 10. 文件日志两级开关与全局态端点（与控制台同构）：具名 OFF/ON 文件逐器隔离与恢复（落盘文件实证）、
// 未知名 ResStatus::Failed（不惰性创建）、文件主闸×逐器两层归因（file().status() 报文件主闸 off
// 与条目逐器 on 各自真实呈现、两层 AND 不落盘）、具名关断不波及文件主闸（两层正交）、
// file().status()/console().status()）两套主闸态独立直证与主闸翻转联动
// 11. 文件日志全局代理契约（与控制台 S3 同构，仅单一全局代理无逐器代理）：设置前基线
// （file().status() proxyOff 且 console().status() proxyOn 两套代理独立直证）、
// file().proxy 拦截（false→ResStatus::Succeeded+"file log proxy is success."+落盘 marker 缺席+原子计数）
// 与穿透（true→默认落盘回归 + file().status() proxyOn）、卸载（nullptr→ResStatus::Succeeded、
// file().status() 回 proxyOff、恢复默认落盘、代理不再被调用原子命中数不增）、重装可逆；
// 用例末尾重装并回到穿透态
//
// 说明：YomkLogger 是 YomkService 子类（构造绑定 server->weak_from_this()），无法脱离
// yomk::server().init() 单例白盒直连；全部经 API 接口/raw request 测试，同时覆盖 invoke 路由链，
// 无覆盖损失。ConsoleLogger/FileLogger 类直连白盒见 TestYomkLoggerDirect.cpp。
// 控制台输出断言采用"唯一 marker + cout rdbuf 捕获"（框架自身日志构成背景噪声，
// 正向断言查 marker 命中、负向断言查 marker 缺席，均不受噪声影响）。
//
//
// 风格：纯 main() + 失败计数，返回非 0 表示存在失败用例（零第三方依赖）

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

#include "yomk/log/YomkLoggerApi.h"

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
static std::vector<std::string> unpackLines(const yomk::payload::Response& resp)
{
    std::vector<std::string> lines;
    auto arr = yomk::msgs::unpack<yomk::msgs::StringArray>(resp.m_data);
    if (arr != nullptr)
    {
        lines = arr->d;
    }
    return lines;
}

// 双表合计行数（console/file 端点各取一次，行内容即 logger 纯名）
static size_t loggersTotal()
{
    return unpackLines(yomk::log_mgr().console().list()).size() +
           unpackLines(yomk::log_mgr().file().list()).size();
}

// 从 ALL 清单取首条 console 条目的控制台状态段（无 console 条目返回 nullptr）
static const yomk::payload::LoggerState* firstConsoleState(yomk::msgs::LoggerInfoListPtr listPkg)
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

// 测试临时根：run_tests.sh 注入（test/test_logs/tmp），独立运行回退 /tmp
static fs::path testTmpRoot()
{
    const char* env = std::getenv("YOMK_TEST_TMP_ROOT");
    return (env && *env) ? fs::path(env) : fs::temp_directory_path();
}

int main()
{
    yomk::server().async_thread(1).init();
    auto server = yomk::server().instance();
    YOMK_TEST_CHECK(server != nullptr, "yomk::server().init() 返回非空服务器");

    // 临时目录（收尾 remove_all）
    fs::path tmpDir = testTmpRoot() / ("yomk_logger_lg1_" + std::to_string(::getpid()));
    std::error_code ec;
    fs::create_directories(tmpDir, ec);
    YOMK_TEST_CHECK(!ec, "临时目录创建成功: " + tmpDir.string());

    // ============ Section 1: 控制台日志基线（四级别 + 开关） ============
    {// 四级别各一发（链式发射把 "[行号]" 拼在内容头部），marker 唯一化断言
     {CoutCapture cap;
    auto r = yomk::log().info("lg1_s1_info_marker ", 42);
    YOMK_TEST_CHECK(r == yomk::payload::ResStatus::Succeeded, "log().info 返回 ResStatus::Succeeded");
    std::string out = cap.str();
    YOMK_TEST_CHECK(out.find("lg1_s1_info_marker  42") != std::string::npos, "INFO 输出含 marker 与整数变参");
    YOMK_TEST_CHECK(out.find("[INFO ] [main] [") != std::string::npos, "INFO 输出格式 [INFO ] [main] [行号]");
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
    YOMK_TEST_CHECK(yomk::log().warn("lg1_s1_warn_marker") == yomk::payload::ResStatus::Succeeded, "log().warn 返回 ResStatus::Succeeded");
    YOMK_TEST_CHECK(
        (cap.str().find("[WARN ] [main]") != std::string::npos &&
         cap.str().find("lg1_s1_warn_marker") != std::string::npos),
        "WARN 输出格式与 marker");
}
{
    CoutCapture cap;
    YOMK_TEST_CHECK(yomk::log().error("lg1_s1_error_marker") == yomk::payload::ResStatus::Succeeded, "log().error 返回 ResStatus::Succeeded");
    YOMK_TEST_CHECK(
        (cap.str().find("[ERROR] [main]") != std::string::npos &&
         cap.str().find("lg1_s1_error_marker") != std::string::npos),
        "ERROR 输出格式与 marker");
}
{
    CoutCapture cap;
    YOMK_TEST_CHECK(yomk::log().debug("lg1_s1_debug_marker") == yomk::payload::ResStatus::Succeeded, "log().debug 返回 ResStatus::Succeeded");
    YOMK_TEST_CHECK(
        (cap.str().find("[DEBUG] [main]") != std::string::npos &&
         cap.str().find("lg1_s1_debug_marker") != std::string::npos),
        "DEBUG 输出格式与 marker");
}
{
    // TRACE/FATAL：链式发射输出格式与四级同构
    CoutCapture cap;
    YOMK_TEST_CHECK(yomk::log().trace("lg1_s1_trace_marker") == yomk::payload::ResStatus::Succeeded, "log().trace 返回 ResStatus::Succeeded");
    YOMK_TEST_CHECK(
        (cap.str().find("[TRACE] [main]") != std::string::npos &&
         cap.str().find("lg1_s1_trace_marker") != std::string::npos),
        "TRACE 输出格式与 marker");
}
{
    CoutCapture cap;
    YOMK_TEST_CHECK(yomk::log().fatal("lg1_s1_fatal_marker") == yomk::payload::ResStatus::Succeeded, "log().fatal 返回 ResStatus::Succeeded");
    YOMK_TEST_CHECK(
        (cap.str().find("[FATAL] [main]") != std::string::npos &&
         cap.str().find("lg1_s1_fatal_marker") != std::string::npos),
        "FATAL 输出格式与 marker");
}

// 级别开关：OFF→无输出仍 ResStatus::Succeeded→ON→恢复输出（六级别逐一）
{
    YOMK_TEST_CHECK(yomk::log_mgr().console().level(yomk::payload::LogLevel::Info).disable() == yomk::payload::ResStatus::Succeeded, "disableConsoleLogInfo 返回 ResStatus::Succeeded");
    CoutCapture cap;
    YOMK_TEST_CHECK(yomk::log().info("lg1_s1_off_info_marker") == yomk::payload::ResStatus::Succeeded, "INFO 级别关闭后调用仍 ResStatus::Succeeded");
    YOMK_TEST_CHECK(cap.str().find("lg1_s1_off_info_marker") == std::string::npos, "INFO 级别关闭后无输出");
    YOMK_TEST_CHECK(yomk::log_mgr().console().level(yomk::payload::LogLevel::Info).enable() == yomk::payload::ResStatus::Succeeded, "console().level(Info).enable() 返回 ResStatus::Succeeded");
    CoutCapture cap2;
    YOMK_TEST_CHECK(yomk::log().info("lg1_s1_on_info_marker") == yomk::payload::ResStatus::Succeeded, "INFO 级别重开后调用 ResStatus::Succeeded");
    YOMK_TEST_CHECK(cap2.str().find("lg1_s1_on_info_marker") != std::string::npos, "INFO 级别重开后恢复输出");
}
{
    YOMK_TEST_CHECK(yomk::log_mgr().console().level(yomk::payload::LogLevel::Warn).disable() == yomk::payload::ResStatus::Succeeded, "disableConsoleLogWarn 返回 ResStatus::Succeeded");
    CoutCapture cap;
    yomk::log().warn("lg1_s1_off_warn_marker");
    YOMK_TEST_CHECK(cap.str().find("lg1_s1_off_warn_marker") == std::string::npos, "WARN 级别关闭后无输出");
    yomk::log_mgr().console().level(yomk::payload::LogLevel::Warn).enable();
    CoutCapture cap2;
    yomk::log().warn("lg1_s1_on_warn_marker");
    YOMK_TEST_CHECK(cap2.str().find("lg1_s1_on_warn_marker") != std::string::npos, "WARN 级别重开后恢复输出");
}
{
    YOMK_TEST_CHECK(yomk::log_mgr().console().level(yomk::payload::LogLevel::Error).disable() == yomk::payload::ResStatus::Succeeded, "disableConsoleLogError 返回 ResStatus::Succeeded");
    CoutCapture cap;
    yomk::log().error("lg1_s1_off_error_marker");
    YOMK_TEST_CHECK(cap.str().find("lg1_s1_off_error_marker") == std::string::npos, "ERROR 级别关闭后无输出");
    yomk::log_mgr().console().level(yomk::payload::LogLevel::Error).enable();
    CoutCapture cap2;
    yomk::log().error("lg1_s1_on_error_marker");
    YOMK_TEST_CHECK(cap2.str().find("lg1_s1_on_error_marker") != std::string::npos, "ERROR 级别重开后恢复输出");
}
{
    YOMK_TEST_CHECK(yomk::log_mgr().console().level(yomk::payload::LogLevel::Debug).disable() == yomk::payload::ResStatus::Succeeded, "disableConsoleLogDebug 返回 ResStatus::Succeeded");
    CoutCapture cap;
    yomk::log().debug("lg1_s1_off_debug_marker");
    YOMK_TEST_CHECK(cap.str().find("lg1_s1_off_debug_marker") == std::string::npos, "DEBUG 级别关闭后无输出");
    yomk::log_mgr().console().level(yomk::payload::LogLevel::Debug).enable();
    CoutCapture cap2;
    yomk::log().debug("lg1_s1_on_debug_marker");
    YOMK_TEST_CHECK(cap2.str().find("lg1_s1_on_debug_marker") != std::string::npos, "DEBUG 级别重开后恢复输出");
}
{
    // TRACE 主闸关断隔离：Trace 静默，其余五级不受影响（六等级扩展新增）
    YOMK_TEST_CHECK(yomk::log_mgr().console().level(yomk::payload::LogLevel::Trace).disable() == yomk::payload::ResStatus::Succeeded, "disableConsoleLogTrace 返回 ResStatus::Succeeded");
    CoutCapture cap;
    yomk::log().trace("lg1_s1_off_trace_marker");
    yomk::log().debug("lg1_s1_off_trace_debug_marker");
    YOMK_TEST_CHECK(cap.str().find("lg1_s1_off_trace_marker") == std::string::npos, "TRACE 级别关闭后无输出");
    YOMK_TEST_CHECK(cap.str().find("lg1_s1_off_trace_debug_marker") != std::string::npos, "关断 TRACE 不影响 DEBUG 等其余五级");
    yomk::log_mgr().console().level(yomk::payload::LogLevel::Trace).enable();
    CoutCapture cap2;
    yomk::log().trace("lg1_s1_on_trace_marker");
    YOMK_TEST_CHECK(cap2.str().find("lg1_s1_on_trace_marker") != std::string::npos, "TRACE 级别重开后恢复输出");
}
{
    CoutCapture cap;
    YOMK_TEST_CHECK(yomk::log_mgr().console().level(yomk::payload::LogLevel::Fatal).disable() == yomk::payload::ResStatus::Succeeded, "disableConsoleLogFatal 返回 ResStatus::Succeeded");
    yomk::log().fatal("lg1_s1_off_fatal_marker");
    YOMK_TEST_CHECK(cap.str().find("lg1_s1_off_fatal_marker") == std::string::npos, "FATAL 级别关闭后无输出");
    yomk::log_mgr().console().level(yomk::payload::LogLevel::Fatal).enable();
    CoutCapture cap2;
    yomk::log().fatal("lg1_s1_on_fatal_marker");
    YOMK_TEST_CHECK(cap2.str().find("lg1_s1_on_fatal_marker") != std::string::npos, "FATAL 级别重开后恢复输出");
}
}

// ============ Section 2: 自定义 tag（自动创建 console logger） ============
{{CoutCapture cap;
YOMK_TEST_CHECK(
    yomk::log().tag("lg1_tag_logger").info("lg1_s2_tag_marker") == yomk::payload::ResStatus::Succeeded,
    "log().tag(...).info 新 tag 返回 ResStatus::Succeeded");
YOMK_TEST_CHECK(
    (cap.str().find("[lg1_tag_logger]") != std::string::npos &&
     cap.str().find("lg1_s2_tag_marker") != std::string::npos),
    "新 tag 自动创建 console logger 并以其名输出");
}
{
    // 链式 tag 参数字面量传入（tag ":" 行号），运行时 string tag 走底层 API 直连
    std::string longTag(65536, 'T');
    CoutCapture cap;
    YOMK_TEST_CHECK(
        yomk::log().tag(longTag).info("lg1_s2_longtag_marker") == yomk::payload::ResStatus::Succeeded,
        "65536 字符超长 tag 返回 ResStatus::Succeeded");
    YOMK_TEST_CHECK(cap.str().find("lg1_s2_longtag_marker") != std::string::npos, "超长 tag 日志正常输出");
    YOMK_TEST_CHECK(cap.str().find("[" + longTag + "]") != std::string::npos, "超长 tag 完整回显到输出行");
}
{
    CoutCapture cap;
    YOMK_TEST_CHECK(yomk::log().tag("lg1_tag_empty").info("") == yomk::payload::ResStatus::Succeeded, "空内容日志返回 ResStatus::Succeeded");
    YOMK_TEST_CHECK(cap.str().find("[lg1_tag_empty]") != std::string::npos, "空内容日志仍输出 tag 头（内容为空）");
}
}

// ============ Section 2.5: 非法级别 default 降级（须在 S3 设置 proxy 前） ============
{
    // proxy 处于拦截态时会短路一切 console_log（含 switch 的 default 分支），故本节置于
    // S3 安装代理之前，与代理后续处于何种态无关；代理可传 nullptr 卸载，
    // 但本节顺序保持不变。枚举已加 : int 底层类型，99 为合法值非 UB
    std::string out;
    yomk::payload::Response r(yomk::payload::ResStatus::Invalid);
    {
        CoutCapture cap;
        r = yomk::server().url("/YomkLogger/console_log").msg(yomk::msgs::Log::shared(yomk::payload::Log{static_cast<yomk::payload::LogLevel>(99), "lg1_s2p5_badlevel_marker", "main"})).request();
        out = cap.str();
    }
    YOMK_TEST_CHECK(r.m_status == yomk::payload::ResStatus::Succeeded, "非法级别 99 console_log 返回 ResStatus::Succeeded（P2-c 修复后 default 活分支）");
    YOMK_TEST_CHECK(
        out.find("unknown log level, use Info") != std::string::npos, "console_log default 分支错误提示（降级 Info）");
    YOMK_TEST_CHECK(
        out.find("[INFO ] [main] lg1_s2p5_badlevel_marker") != std::string::npos,
        "非法级别降级 Info 输出 marker");
}

// ============ Section 3: proxy 契约（含卸载；末尾重装复位，S4-S7 仍为穿透态） ============
{
    // 设置前 all 内省 proxy:off
    {
        auto allResp = yomk::log_mgr().all();
        auto listPkg = yomk::msgs::unpack<yomk::msgs::LoggerInfoList>(allResp.m_data);
        const yomk::payload::LoggerState* st = firstConsoleState(listPkg);
        YOMK_TEST_CHECK(
            (st != nullptr && st->m_traceOn && st->m_debugOn && st->m_infoOn && st->m_warnOn && st->m_errorOn &&
             st->m_fatalOn && !st->m_proxyOn),
            "proxy 设置前 ALL 控制台状态六级全 on（m_traceOn/m_fatalOn 默认 true）+ proxyOff");
    }

    g_proxyHits.store(0);
    g_proxyIntercept.store(true);
    auto proxy = [](const yomk::payload::Log&) -> bool
    {
        g_proxyHits.fetch_add(1);
        return !g_proxyIntercept.load();  // 拦截态返回 false，穿透态返回 true
    };
    YOMK_TEST_CHECK(yomk::log_mgr().console().proxy(proxy).apply() == yomk::payload::ResStatus::Succeeded, "console().proxy().apply() 返回 ResStatus::Succeeded");

    // 拦截态：proxy 返回 false → ResStatus::Succeeded + 专属消息 + 无控制台输出
    {
        CoutCapture cap;
        auto r = yomk::log().info("lg1_s3_intercept_marker");
        YOMK_TEST_CHECK(r == yomk::payload::ResStatus::Succeeded, "拦截态 console_log 返回 ResStatus::Succeeded");
        YOMK_TEST_CHECK(cap.str().find("lg1_s3_intercept_marker") == std::string::npos, "拦截态无默认控制台输出");
        YOMK_TEST_CHECK(g_proxyHits.load() == 1, "proxy 被调用 1 次（原子计数）");
    }

    // 穿透态：proxy 返回 true → 走默认输出
    {
        g_proxyIntercept.store(false);
        CoutCapture cap;
        auto r = yomk::log().info("lg1_s3_pass_marker");
        YOMK_TEST_CHECK(r == yomk::payload::ResStatus::Succeeded, "穿透态 console_log 返回 ResStatus::Succeeded + success.");
        YOMK_TEST_CHECK(cap.str().find("lg1_s3_pass_marker") != std::string::npos, "穿透态恢复默认控制台输出");
        YOMK_TEST_CHECK(g_proxyHits.load() == 2, "proxy 累计被调用 2 次");
    }

    // 设置后 all 内省 proxy:on
    {
        auto allResp = yomk::log_mgr().all();
        auto listPkg = yomk::msgs::unpack<yomk::msgs::LoggerInfoList>(allResp.m_data);
        const yomk::payload::LoggerState* st = firstConsoleState(listPkg);
        YOMK_TEST_CHECK(st != nullptr && st->m_proxyOn, "proxy 设置后 ALL 状态 proxyOn");
    }

    // ---- proxy 可卸载 ----
    // 卸载前先把代理切回拦截态，确认它确实在链路上——否则「卸载后恢复默认输出」
    // 与「本来就是穿透态」无从区分
    {
        g_proxyIntercept.store(true);
        std::string out;
        auto r = yomk::payload::ResStatus::Invalid;
        {
            CoutCapture cap;
            r = yomk::log().info("lg6_s3_pre_unload_marker");
            out = cap.str();
        }
        YOMK_TEST_CHECK(
            r == yomk::payload::ResStatus::Succeeded,
            "卸载前拦截态契约消息（代理确实在链路上）");
        YOMK_TEST_CHECK(out.find("lg6_s3_pre_unload_marker") == std::string::npos, "卸载前拦截态无默认控制台输出");
    }

    // 卸载：传 nullptr（等价于空 std::function）
    {
        YOMK_TEST_CHECK(
            yomk::log_mgr().console().proxy(nullptr).apply() == yomk::payload::ResStatus::Succeeded,
            "console().proxy(nullptr).apply() 卸载返回 ResStatus::Succeeded");
        auto allResp = yomk::log_mgr().all();
        auto listPkg = yomk::msgs::unpack<yomk::msgs::LoggerInfoList>(allResp.m_data);
        const yomk::payload::LoggerState* st = firstConsoleState(listPkg);
        YOMK_TEST_CHECK(
            st != nullptr && !st->m_proxyOn,
            "卸载后 ALL 状态回到 proxyOff（P4-e 前恒报 proxyOn）");
    }

    // 卸载后：恢复框架默认输出，且代理不再被调用
    {
        const long hitsBefore = g_proxyHits.load();
        std::string out;
        auto r = yomk::payload::ResStatus::Invalid;
        {
            CoutCapture cap;
            r = yomk::log().info("lg6_s3_post_unload_marker");
            out = cap.str();
        }
        YOMK_TEST_CHECK(
            r == yomk::payload::ResStatus::Succeeded,
            "卸载后 console_log 返回 ResStatus::Succeeded + success.（走默认路径而非代理短路）");
        YOMK_TEST_CHECK(out.find("lg6_s3_post_unload_marker") != std::string::npos, "卸载后恢复框架默认控制台输出");
        YOMK_TEST_CHECK(
            out.find("[INFO ] [main] [") != std::string::npos,
            "卸载后输出为框架默认格式 <时间戳> [INFO ] [logger] msg");
        YOMK_TEST_CHECK(g_proxyHits.load() == hitsBefore, "卸载后代理不再被调用（原子命中数不增）");
    }

    // 复位：重装同一代理并回到穿透态，维持 S4-S7 依赖的「proxy:on + 穿透」不变量
    {
        g_proxyIntercept.store(false);
        YOMK_TEST_CHECK(yomk::log_mgr().console().proxy(proxy).apply() == yomk::payload::ResStatus::Succeeded, "重装代理返回 ResStatus::Succeeded（卸载可逆）");
        auto allResp = yomk::log_mgr().all();
        auto listPkg = yomk::msgs::unpack<yomk::msgs::LoggerInfoList>(allResp.m_data);
        const yomk::payload::LoggerState* st = firstConsoleState(listPkg);
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

    // 创建：ResStatus::Succeeded + 空文件已建（init 语义）
    YOMK_TEST_CHECK(
        yomk::log_mgr().dir(logDir.string()).file("lg1_file_logger").create() == yomk::payload::ResStatus::Succeeded,
        "file().create() 返回 ResStatus::Succeeded");
    YOMK_TEST_CHECK((fs::exists(logFile) && fs::file_size(logFile, ec) == 0), "CREATE 后 <dir>/<name>.log 空文件已建");

    // 六级别入缓冲：write 前文件仍空（缓冲语义）
    YOMK_TEST_CHECK(
        yomk::log().to_sink("lg1_file_logger").trace("lg1_s4_trace_marker") == yomk::payload::ResStatus::Succeeded,
        "FILE_TRACE 返回 ResStatus::Succeeded（六等级扩展新增）");
    YOMK_TEST_CHECK(
        yomk::log().to_sink("lg1_file_logger").info("lg1_s4_info_marker ", 7) == yomk::payload::ResStatus::Succeeded,
        "FILE_INFO 返回 ResStatus::Succeeded");
    YOMK_TEST_CHECK(yomk::log().to_sink("lg1_file_logger").warn("lg1_s4_warn_marker") == yomk::payload::ResStatus::Succeeded, "FILE_WARN 返回 ResStatus::Succeeded");
    YOMK_TEST_CHECK(
        yomk::log().to_sink("lg1_file_logger").error("lg1_s4_error_marker") == yomk::payload::ResStatus::Succeeded, "FILE_ERROR 返回 ResStatus::Succeeded");
    YOMK_TEST_CHECK(
        yomk::log().to_sink("lg1_file_logger").debug("lg1_s4_debug_marker") == yomk::payload::ResStatus::Succeeded, "FILE_DEBUG 返回 ResStatus::Succeeded");
    YOMK_TEST_CHECK(
        yomk::log().to_sink("lg1_file_logger").fatal("lg1_s4_fatal_marker") == yomk::payload::ResStatus::Succeeded,
        "FILE_FATAL 返回 ResStatus::Succeeded（六等级扩展新增）");
    YOMK_TEST_CHECK(fs::file_size(logFile, ec) == 0, "WRITE 前文件仍为空（stringstream 缓冲语义）");

    // 落盘：读回断言行数/行格式/marker/tag
    YOMK_TEST_CHECK(yomk::log_mgr().file("lg1_file_logger").flush() == yomk::payload::ResStatus::Succeeded, "file().flush() 返回 ResStatus::Succeeded");
    bool readOk = false;
    std::string content = readFile(logFile, readOk);
    YOMK_TEST_CHECK(readOk, "落盘后日志文件可读");
    YOMK_TEST_CHECK(countLines(content) == 6, "落盘内容恰 6 行（六级别各一）");
    YOMK_TEST_CHECK(
        (content.find("[INFO ] [main]") != std::string::npos &&
         content.find("[INFO ] [main] [") != std::string::npos &&
         content.find("lg1_s4_info_marker  7") != std::string::npos),
        "Info 行格式（时间戳+[INFO ]+[main]+[行号] 内容）——FILE_INFO 默认内容 tag 为 main，行号独立成段，"
        "logger 名仅作路由 key");
    // 自定义内容 tag 变体：FILE_INFO_TAG 的 tag 进内容行
    YOMK_TEST_CHECK(
        yomk::log().to_sink("lg1_file_logger").tag("lg1CustomTag").info("lg1_s4_customtag_marker") == yomk::payload::ResStatus::Succeeded,
        "FILE_INFO_TAG 自定义 tag 返回 ResStatus::Succeeded");
    YOMK_TEST_CHECK(yomk::log_mgr().file("lg1_file_logger").flush() == yomk::payload::ResStatus::Succeeded, "自定义 tag 内容 WRITE ResStatus::Succeeded");
    bool readOkTag = false;
    std::string contentTag = readFile(logFile, readOkTag);
    YOMK_TEST_CHECK(
        (readOkTag && contentTag.find("[INFO ] [lg1CustomTag] [") != std::string::npos &&
         contentTag.find("lg1_s4_customtag_marker") != std::string::npos),
        "自定义 tag 行格式（[INFO ] [tag] [行号] 内容）");
    YOMK_TEST_CHECK(
        (content.find("[WARN ]") != std::string::npos && content.find("[ERROR]") != std::string::npos &&
         content.find("[DEBUG]") != std::string::npos && content.find("[TRACE]") != std::string::npos &&
         content.find("[FATAL]") != std::string::npos),
        "Warn/Error/Debug/Trace/Fatal 五级别行均在（Info 行已单独断言）");
    YOMK_TEST_CHECK(checkTimeFormat(content), "文件日志行首时间戳格式 [YYYY-MM-DD HH:MM:SS.mmm]");

    // 落盘后缓冲清空：再次 WRITE 文件不增长
    auto sizeBefore = fs::file_size(logFile, ec);
    YOMK_TEST_CHECK(yomk::log_mgr().file("lg1_file_logger").flush() == yomk::payload::ResStatus::Succeeded, "二次 file().flush() 返回 ResStatus::Succeeded");
    YOMK_TEST_CHECK(fs::file_size(logFile, ec) == sizeBefore, "二次 WRITE 无重复内容（缓冲已清空）");

    // 重复创建 ResStatus::Failed
    auto dup = yomk::log_mgr().dir(logDir.string()).file("lg1_file_logger").create();
    YOMK_TEST_CHECK(dup == yomk::payload::ResStatus::Failed, "重复创建同名 file logger 返回 ResStatus::Failed");

    // 未创建 logger：to_sink 发射 / file().flush() 均 ResStatus::Failed
    auto noLogger = yomk::log().to_sink("lg1_never_created").info("x");
    YOMK_TEST_CHECK(noLogger == yomk::payload::ResStatus::Failed, "to_sink 发射未创建 logger 返回 ResStatus::Failed");
    auto noWrite = yomk::log_mgr().file("lg1_never_created").flush();
    YOMK_TEST_CHECK(noWrite == yomk::payload::ResStatus::Failed, "file().flush() 未创建 logger 返回 ResStatus::Failed");

    // 空名/空 dir 一律 ResStatus::Invalid，"" key 注册与
    // "/.log" 根路径拼接彻底堵死
    auto emptyBoth = yomk::log_mgr().dir("").file("").create();
    YOMK_TEST_CHECK(
        emptyBoth == yomk::payload::ResStatus::Invalid,
        "空名+空 dir 创建返回 ResStatus::Invalid + logger name is empty.（P2-a 已处置）");
    auto emptyName = yomk::log_mgr().dir(tmpDir.string()).file("").create();
    YOMK_TEST_CHECK(
        emptyName == yomk::payload::ResStatus::Invalid,
        "空名+有效 dir 创建返回 ResStatus::Invalid（P2-a 已处置）");
    auto emptyDir = yomk::log_mgr().dir("").file("lg1_empty_dir_name").create();
    YOMK_TEST_CHECK(
        emptyDir == yomk::payload::ResStatus::Invalid,
        "有效名+空 dir 创建返回 ResStatus::Invalid + logger dir is empty.（P2-a 已处置）");

    // 不可创建 dir → init 返回 false → ResStatus::Failed 且不注册幽灵 logger
    auto badDir = yomk::log_mgr().dir("/proc/lg1_impossible/sub").file("lg1_bad_dir_logger").create();
    YOMK_TEST_CHECK(
        badDir == yomk::payload::ResStatus::Failed,
        "不可创建 dir 返回 ResStatus::Failed + init file logger failed.（P2-b 已处置，异常不穿透）");
    auto ghostInfo = yomk::log_mgr().file("lg1_bad_dir_logger").get_info();
    YOMK_TEST_CHECK(
        (ghostInfo.m_status == yomk::payload::ResStatus::Failed && ghostInfo.m_msg == "logger not found."),
        "init 失败的 logger 未注册（无幽灵 key，内省 not found）");
    // init 失败不占用名字：同名换有效 dir 重试应成功
    fs::path retryDir = tmpDir / "s4_retry";
    auto retry = yomk::log_mgr().dir(retryDir.string()).file("lg1_bad_dir_logger").create();
    YOMK_TEST_CHECK(
        (retry == yomk::payload::ResStatus::Succeeded && fs::exists(retryDir / "lg1_bad_dir_logger.log")),
        "init 失败后同名换有效 dir 重试成功（名字未被幽灵占用）");
}

// ============ Section 5: 内省三端点 ============
{
    // LOGGERS：按类型查询，行内容即 logger 纯名
    auto consoleResp = yomk::log_mgr().console().list();
    YOMK_TEST_CHECK(consoleResp.m_status == yomk::payload::ResStatus::Succeeded, "console().list() 返回 ResStatus::Succeeded");
    auto consoleArr = yomk::msgs::unpack<yomk::msgs::StringArray>(consoleResp.m_data);
    bool hasMain = false;
    bool hasTag = false;
    if (consoleArr)
    {
        for (const auto& line : consoleArr->d)
        {
            if (line == "main")
            {
                hasMain = true;
            }
            if (line.rfind("lg1_tag_logger", 0) == 0)
            {
                hasTag = true;
            }
        }
    }
    YOMK_TEST_CHECK((consoleArr != nullptr && hasMain), "LOGGERS(console) 含 main 行");
    YOMK_TEST_CHECK(hasTag, "LOGGERS(console) 含 S2 自建 tag logger 行");
    auto fileResp = yomk::log_mgr().file().list();
    YOMK_TEST_CHECK(fileResp.m_status == yomk::payload::ResStatus::Succeeded, "file().list() 返回 ResStatus::Succeeded");
    auto fileArr = yomk::msgs::unpack<yomk::msgs::StringArray>(fileResp.m_data);
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
    auto consoleInfo = yomk::log_mgr().console("main").get_info();
    YOMK_TEST_CHECK(
        (consoleInfo.m_status == yomk::payload::ResStatus::Succeeded && consoleInfo.m_msg == "ok"),
        "LOGGER(main) 命中 m_msg 恒为 ok");
    auto consolePkg = yomk::msgs::unpack<yomk::msgs::LoggerInfo>(consoleInfo.m_data);
    YOMK_TEST_CHECK(consolePkg != nullptr, "LOGGER(main) 数据为 LoggerInfo 包");
    YOMK_TEST_CHECK(consolePkg && consolePkg->d.m_loggerName == "main" && consolePkg->d.m_type == "console",
          "LOGGER(main) 字段 loggerName/type 一致");
    YOMK_TEST_CHECK(consolePkg && consolePkg->d.m_dir.empty(), "console 命中 m_dir 为空");
    auto fileInfo = yomk::log_mgr().file("lg1_file_logger").get_info();
    YOMK_TEST_CHECK(
        (fileInfo.m_status == yomk::payload::ResStatus::Succeeded && fileInfo.m_msg == "ok"),
        "LOGGER(lg1_file_logger) 命中 m_msg 恒为 ok");
    auto filePkg = yomk::msgs::unpack<yomk::msgs::LoggerInfo>(fileInfo.m_data);
    YOMK_TEST_CHECK(filePkg != nullptr, "LOGGER(lg1_file_logger) 数据为 LoggerInfo 包");
    YOMK_TEST_CHECK(filePkg && filePkg->d.m_loggerName == "lg1_file_logger" && filePkg->d.m_type == "file",
          "LOGGER(lg1_file_logger) 字段 loggerName/type 一致");
    YOMK_TEST_CHECK(filePkg && filePkg->d.m_dir == (tmpDir / "s4").string(), "file 命中 m_dir 为创建目录");
    auto noInfo = yomk::log_mgr().console("lg1_never_registered").get_info();
    YOMK_TEST_CHECK(
        (noInfo.m_status == yomk::payload::ResStatus::Failed && noInfo.m_msg == "logger not found."),
        "LOGGER 未注册名返回 ResStatus::Failed + logger not found.");

    // ALL：控制台状态字段与开关翻转联动（逐字段断言）
    {
        auto allResp = yomk::log_mgr().all();
        auto listPkg = yomk::msgs::unpack<yomk::msgs::LoggerInfoList>(allResp.m_data);
        const yomk::payload::LoggerState* st = firstConsoleState(listPkg);
        YOMK_TEST_CHECK(
            (st != nullptr && st->m_traceOn && st->m_debugOn && st->m_infoOn && st->m_warnOn && st->m_errorOn &&
             st->m_fatalOn && st->m_proxyOn),
            "ALL 控制台状态六级全 on（S3 已设 proxy）");
    }
    {
        yomk::log_mgr().console().level(yomk::payload::LogLevel::Info).disable();
        // 主闸翻转与条目解耦后，翻转联动取数迁移至主闸 status()（log_mgr().console().status()）；
        // 条目改报逐器层真值，不受无参翻转影响（正交性在 Section 9 直证）
        auto gResp = yomk::log_mgr().console().status();
        auto gs = yomk::msgs::unpack<yomk::msgs::LoggerState>(gResp.m_data);
        YOMK_TEST_CHECK(
            (gs != nullptr && gs->d.m_traceOn && gs->d.m_debugOn && !gs->d.m_infoOn && gs->d.m_warnOn &&
             gs->d.m_errorOn && gs->d.m_fatalOn && gs->d.m_proxyOn),
            "OFF INFO 后全局主闸状态 infoOff（六级其余字段不受影响，开关联动逐字段）");
        yomk::log_mgr().console().level(yomk::payload::LogLevel::Info).enable();
        auto gResp2 = yomk::log_mgr().console().status();
        auto gs2 = yomk::msgs::unpack<yomk::msgs::LoggerState>(gResp2.m_data);
        YOMK_TEST_CHECK(
            (gs2 != nullptr && gs2->d.m_traceOn && gs2->d.m_debugOn && gs2->d.m_infoOn && gs2->d.m_warnOn &&
             gs2->d.m_errorOn && gs2->d.m_fatalOn && gs2->d.m_proxyOn),
            "ON INFO 复原后全局主闸状态 infoOn（六级全 on）");
    }
}

// ============ Section 6: 边界值（数值特殊值 / 超大内容 / 非法级别） ============
{// 变参 fold：整数边界与浮点特殊值（ostringstream 默认格式字符串化）
 {CoutCapture cap;
YOMK_TEST_CHECK(
    yomk::log().tag("lg1_s6_num").info("i=", INT_MAX, " j=", INT_MIN, " z=", 0, " n=", -1) == yomk::payload::ResStatus::Succeeded,
    "INT_MAX/INT_MIN/0/-1 变参日志返回 ResStatus::Succeeded");
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
        yomk::log().tag("lg1_s6_flt").info("a=", nanV, " b=", infV, " c=", -infV, " d=", -0.0) == yomk::payload::ResStatus::Succeeded,
        "NaN/Inf/-Inf/-0.0 变参日志返回 ResStatus::Succeeded");
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
    YOMK_TEST_CHECK(yomk::log().tag("lg1_s6_big").info(big) == yomk::payload::ResStatus::Succeeded, "1MB 超大内容控制台日志返回 ResStatus::Succeeded");
    YOMK_TEST_CHECK(cap.str().find(big) != std::string::npos, "1MB 内容完整输出到控制台");

    fs::path bigDir = tmpDir / "s6";
    fs::create_directories(bigDir, ec);
    YOMK_TEST_CHECK(
        yomk::log_mgr().dir(bigDir.string()).file("lg1_big_logger").create() == yomk::payload::ResStatus::Succeeded,
        "超大内容用 file logger 创建 ResStatus::Succeeded");
    YOMK_TEST_CHECK(yomk::log().to_sink("lg1_big_logger").info(big) == yomk::payload::ResStatus::Succeeded, "1MB 超大内容文件日志返回 ResStatus::Succeeded");
    YOMK_TEST_CHECK(yomk::log_mgr().file("lg1_big_logger").flush() == yomk::payload::ResStatus::Succeeded, "超大内容 WRITE 返回 ResStatus::Succeeded");
    bool readOk = false;
    std::string content = readFile(bigDir / "lg1_big_logger.log", readOk);
    YOMK_TEST_CHECK((readOk && content.find(big) != std::string::npos), "1MB 内容完整落盘");
}

// 非法级别注入：枚举已加 : int，99 为合法值；
// file_log/off/on 三处 default 降级与无副作用实证
// （console_log 的 default 已在 S2.5 于 proxy 设置前触达）
{
    fs::path badLevelDir = tmpDir / "s6_badlevel";
    fs::create_directories(badLevelDir, ec);
    YOMK_TEST_CHECK(
        yomk::log_mgr().dir(badLevelDir.string()).file("lg1_s6_badlevel").create() == yomk::payload::ResStatus::Succeeded,
        "非法级别注入用 file logger 创建 ResStatus::Succeeded");
    std::string out;
    yomk::payload::Response r(yomk::payload::ResStatus::Invalid);
    {
        CoutCapture cap;
        r = yomk::server().url("/YomkLogger/file_log").msg(yomk::msgs::Log::shared(
                yomk::payload::Log{static_cast<yomk::payload::LogLevel>(99), "lg1_s6_badlevel_marker", "lg1_s6_badlevel"})).request();
        out = cap.str();
    }
    YOMK_TEST_CHECK(
        (r.m_status == yomk::payload::ResStatus::Succeeded && r.m_msg == "success."),
        "file_log 非法级别 99 返回 ResStatus::Succeeded（P2-c 修复后 default 活分支）");
    YOMK_TEST_CHECK(out.find("unknown log level, use Info") != std::string::npos, "file_log default 分支错误提示（降级 Info）");
    YOMK_TEST_CHECK(yomk::log_mgr().file("lg1_s6_badlevel").flush() == yomk::payload::ResStatus::Succeeded, "非法级别落盘 WRITE ResStatus::Succeeded");
    bool readOk = false;
    std::string content = readFile(badLevelDir / "lg1_s6_badlevel.log", readOk);
    YOMK_TEST_CHECK(
        (readOk && countLines(content) == 1 && content.find("[INFO ] lg1_s6_badlevel_marker") != std::string::npos),
        "非法级别降级 Info 落盘恰 1 行");

    // off/on 非法级别：default 仅记录错误，无副作用仍 ResStatus::Succeeded，开关状态不变
    std::string outOff;
    yomk::payload::Response rOff(yomk::payload::ResStatus::Invalid);
    {
        CoutCapture cap;
        rOff = yomk::server().url("/YomkLogger/off_console_log_by_level").msg(yomk::msgs::Log::shared(yomk::payload::Log{static_cast<yomk::payload::LogLevel>(99), "", ""})).request();
        outOff = cap.str();
    }
    YOMK_TEST_CHECK(
        (rOff.m_status == yomk::payload::ResStatus::Succeeded && outOff.find("unknown log level, turn off failed.") != std::string::npos),
        "off 非法级别 ResStatus::Succeeded + 错误提示（default 活分支）");
    std::string outOn;
    yomk::payload::Response rOn(yomk::payload::ResStatus::Invalid);
    {
        CoutCapture cap;
        rOn = yomk::server().url("/YomkLogger/on_console_log_by_level").msg(yomk::msgs::Log::shared(yomk::payload::Log{static_cast<yomk::payload::LogLevel>(99), "", ""})).request();
        outOn = cap.str();
    }
    YOMK_TEST_CHECK(
        (rOn.m_status == yomk::payload::ResStatus::Succeeded && outOn.find("unknown log level, turn on failed.") != std::string::npos),
        "on 非法级别 ResStatus::Succeeded + 错误提示（default 活分支）");
    auto allResp = yomk::log_mgr().all();
    auto listPkg = yomk::msgs::unpack<yomk::msgs::LoggerInfoList>(allResp.m_data);
    const yomk::payload::LoggerState* st = firstConsoleState(listPkg);
    YOMK_TEST_CHECK(
        (st != nullptr && st->m_debugOn && st->m_infoOn && st->m_warnOn && st->m_errorOn && st->m_proxyOn),
        "非法级别 off/on 无副作用（ALL 控制台状态仍全 on）");
}
}

// ============ Section 7: 异常路径（空包 / 错类型包 / 未知端点） ============
{
    // console_log 收 nullptr 包 → unpack 空包分支 ResStatus::Failed
    {
        CoutCapture cap;
        auto r = yomk::server().url("/YomkLogger/console_log").request();
        YOMK_TEST_CHECK(r.m_status == yomk::payload::ResStatus::Failed, "console_log nullptr 包返回 ResStatus::Failed");
        YOMK_TEST_CHECK(r.m_msg == "pkg is null or pkg is not Log", "console_log 空包契约消息");
    }
    // console_log 收错类型包（String）→ 同分支 ResStatus::Failed
    {
        auto r = yomk::server().url("/YomkLogger/console_log").msg(yomk::msgs::String::shared(std::string("wrong"))).request();
        YOMK_TEST_CHECK(r.m_status == yomk::payload::ResStatus::Failed, "console_log 错类型包返回 ResStatus::Failed");
        YOMK_TEST_CHECK(r.m_msg == "pkg is null or pkg is not Log", "console_log 错类型包契约消息");
    }
    // file_log / write_file_log / create_file_logger / logger 空包
    YOMK_TEST_CHECK(yomk::server().url("/YomkLogger/file_log").request().m_status == yomk::payload::ResStatus::Failed, "file_log nullptr 包返回 ResStatus::Failed");
    YOMK_TEST_CHECK(
        yomk::server().url("/YomkLogger/write_file_log").request().m_status == yomk::payload::ResStatus::Failed,
        "write_file_log nullptr 包返回 ResStatus::Failed");
    YOMK_TEST_CHECK(
        yomk::server().url("/YomkLogger/create_file_logger").request().m_status == yomk::payload::ResStatus::Failed,
        "create_file_logger nullptr 包返回 ResStatus::Failed");
    YOMK_TEST_CHECK(yomk::server().url("/YomkLogger/logger").request().m_status == yomk::payload::ResStatus::Failed, "logger nullptr 包返回 ResStatus::Failed");
    // console_log 空 logger 名 → 回退 main 分支（链式发射 tag 恒非空, raw 注入才可触达）
    // 注：YOMK_TEST_CHECK 须在捕获作用域外（否则断言输出被 rdbuf 重定向吞没）
    {
        std::string out;
        yomk::payload::Response r(yomk::payload::ResStatus::Invalid);
        {
            CoutCapture cap;
            r = yomk::server().url("/YomkLogger/console_log").msg(yomk::msgs::Log::shared(yomk::payload::Log{yomk::payload::LogLevel::Info, "lg1_s7_empty_logger_marker", ""})).request();
            out = cap.str();
        }
        YOMK_TEST_CHECK(r.m_status == yomk::payload::ResStatus::Succeeded, "console_log 空 logger 名返回 ResStatus::Succeeded（回退分支）");
        YOMK_TEST_CHECK(
            out.find("console logger name is empty, use main") != std::string::npos,
            "空 logger 名错误提示（回退 main）");
        YOMK_TEST_CHECK(
            out.find("[main] lg1_s7_empty_logger_marker") != std::string::npos,
            "空 logger 名回退 main 输出");
    }
    // file_log 空 logger 名 → ResStatus::Failed 分支（链式 file 参可传空串, raw 注入等价触达）
    {
        std::string out;
        yomk::payload::Response r(yomk::payload::ResStatus::Invalid);
        {
            CoutCapture cap;
            r = yomk::server().url("/YomkLogger/file_log").msg(yomk::msgs::Log::shared(yomk::payload::Log{yomk::payload::LogLevel::Info, "x", ""})).request();
            out = cap.str();
        }
        YOMK_TEST_CHECK(
            (r.m_status == yomk::payload::ResStatus::Failed && r.m_msg == "file logger name is empty."),
            "file_log 空 logger 名返回 ResStatus::Failed + 契约消息");
        YOMK_TEST_CHECK(out.find("file logger name is empty.") != std::string::npos, "file_log 空名错误提示");
    }
    // get_info 空名 → ResStatus::Failed（空名注册不可达，幽灵 logger 命中路径已堵死）
    auto emptyNameInfo = yomk::log_mgr().console("").get_info();
    YOMK_TEST_CHECK(
        (emptyNameInfo.m_status == yomk::payload::ResStatus::Failed && emptyNameInfo.m_msg == "logger not found."),
        "LOGGER 空名返回 ResStatus::Failed + logger not found.（P2-a 处置后无幽灵 key）");
    // 未知端点 → invoke 路由层 ResStatus::Failed（服务前缀被路由剥离，消息仅含端点名）
    auto unknown = yomk::server().url("/YomkLogger/no_such_func").request();
    YOMK_TEST_CHECK(unknown.m_status == yomk::payload::ResStatus::Failed, "未知端点返回 ResStatus::Failed（invoke not found）");
    YOMK_TEST_CHECK(unknown.m_msg == "function not found: /no_such_func", "未知端点契约消息（路由剥离服务前缀）");
}

// ============ Section 8: logger 删除接口（/delete_logger 全语义） ============
{
    // 控制台写入一律经此 lambda：捕获作用域内调用、作用域外取响应，
    // 既避免输出噪声，也规避 YOMK_TEST_CHECK 在捕获域内被吞没
    auto consoleInfo = [](const std::string& logger, const std::string& marker)
    {
        auto r = yomk::payload::ResStatus::Invalid;
        CoutCapture cap;
        r = yomk::log().tag(logger).info(marker);
        return r;
    };

    // 8.1 空名 → ResStatus::Invalid（与 createFileLogger 空名惯例同口径）
    {
        std::string out;
        auto r = yomk::payload::ResStatus::Succeeded;
        {
            CoutCapture cap;
            r = yomk::log_mgr().file("").remove();
            out = cap.str();
        }
        YOMK_TEST_CHECK(
            r == yomk::payload::ResStatus::Invalid,
            "S8: 删除空名返回 ResStatus::Invalid + 契约消息");
        YOMK_TEST_CHECK(out.find("logger name is empty.") != std::string::npos, "S8: 空名删除错误提示");
    }

    // 8.2 两表均未命中 → ResStatus::Failed + not-found 契约消息（对齐 writeFileLog/loggerInfo）
    {
        std::string out;
        auto r = yomk::payload::ResStatus::Succeeded;
        {
            CoutCapture cap;
            r = yomk::log_mgr().file("lg1_s8_never_created").remove();
            out = cap.str();
        }
        YOMK_TEST_CHECK(
            r == yomk::payload::ResStatus::Failed,
            "S8: 删除未注册 logger 返回 ResStatus::Failed + logger not found.");
        YOMK_TEST_CHECK(
            out.find("logger: lg1_s8_never_created not found.") != std::string::npos, "S8: 未命中错误提示含 logger 名");
    }

    // 8.3 console-only：msg 计数 1/0；删除后单查 ResStatus::Failed；同名再写惰性重建
    {
        const std::string name = "lg1_s8_console_only";
        YOMK_TEST_CHECK(
            consoleInfo(name, "lg1_s8_console_marker") == yomk::payload::ResStatus::Succeeded, "S8: console-only 预热创建 ResStatus::Succeeded");
        YOMK_TEST_CHECK(yomk::log_mgr().console(name).get_info().m_status == yomk::payload::ResStatus::Succeeded, "S8: 创建后单查命中 ResStatus::Succeeded");

        auto del = yomk::log_mgr().file(name).remove();
        YOMK_TEST_CHECK(
            del == yomk::payload::ResStatus::Succeeded,
            "S8: console-only 删除 ResStatus::Succeeded（msg 计数 deleted console:1 file:0）");
        YOMK_TEST_CHECK(yomk::log_mgr().console(name).get_info().m_status == yomk::payload::ResStatus::Failed, "S8: console 删除后单查 ResStatus::Failed");
        // 惰性重建：同名再写一条 → consoleLog 双检锁重建路径
        YOMK_TEST_CHECK(
            consoleInfo(name, "lg1_s8_rebuild_marker") == yomk::payload::ResStatus::Succeeded,
            "S8: 删除后同名再写 ResStatus::Succeeded（惰性重建）");
        YOMK_TEST_CHECK(yomk::log_mgr().console(name).get_info().m_status == yomk::payload::ResStatus::Succeeded, "S8: 惰性重建后单查恢复 ResStatus::Succeeded");
        YOMK_TEST_CHECK(yomk::log_mgr().file(name).remove() == yomk::payload::ResStatus::Succeeded, "S8: 清理重建 logger ResStatus::Succeeded");
    }

    // 8.4 file-only：写 2 行不 flush → 删除释放最后引用 → ~FileLogger 锁外自动落盘
    {
        fs::path dir = tmpDir / "s8_file";
        fs::create_directories(dir, ec);
        const std::string name = "lg1_s8_file_only";
        YOMK_TEST_CHECK(yomk::log_mgr().dir(dir.string()).file(name).create() == yomk::payload::ResStatus::Succeeded, "S8: file logger 创建 ResStatus::Succeeded");
        YOMK_TEST_CHECK(
            yomk::log().to_sink(name).tag("s8Tag").info("lg1_s8_file_marker_1") == yomk::payload::ResStatus::Succeeded,
            "S8: file 写入第 1 行 ResStatus::Succeeded");
        YOMK_TEST_CHECK(
            yomk::log().to_sink(name).tag("s8Tag").info("lg1_s8_file_marker_2") == yomk::payload::ResStatus::Succeeded,
            "S8: file 写入第 2 行 ResStatus::Succeeded");
        // 前置对照：未 flush 前 init 建的空文件仍为空
        bool okBefore = false;
        std::string contentBefore = readFile(dir / (name + ".log"), okBefore);
        YOMK_TEST_CHECK((okBefore && contentBefore.empty()), "S8: 删除前未 flush，文件仍为空");

        auto del = yomk::log_mgr().file(name).remove();
        YOMK_TEST_CHECK(
            del == yomk::payload::ResStatus::Succeeded,
            "S8: file-only 删除 ResStatus::Succeeded（msg 计数 deleted console:0 file:1）");
        bool okAfter = false;
        std::string content = readFile(dir / (name + ".log"), okAfter);
        YOMK_TEST_CHECK((okAfter && countLines(content) == 2), "S8: 删除触发析构自动落盘恰 2 行");
        YOMK_TEST_CHECK(
            (content.find("lg1_s8_file_marker_1") != std::string::npos &&
             content.find("lg1_s8_file_marker_2") != std::string::npos),
            "S8: 落盘内容含两条 marker（数据不因删除丢失）");
        YOMK_TEST_CHECK(yomk::log_mgr().file(name).flush() == yomk::payload::ResStatus::Failed, "S8: 删除后 WRITE 同名返回 ResStatus::Failed");
    }

    // 8.5 同名双表：一次删除同时清理两表（msg 计数 1/1），重复删除幂等 ResStatus::Failed
    {
        fs::path dir = tmpDir / "s8_both";
        fs::create_directories(dir, ec);
        const std::string name = "lg1_s8_both_tables";
        YOMK_TEST_CHECK(
            consoleInfo(name, "lg1_s8_both_console_marker") == yomk::payload::ResStatus::Succeeded,
            "S8: 双表用例 console 侧创建 ResStatus::Succeeded");
        YOMK_TEST_CHECK(yomk::log_mgr().dir(dir.string()).file(name).create() == yomk::payload::ResStatus::Succeeded, "S8: 双表用例 file 侧创建 ResStatus::Succeeded");

        auto del = yomk::log_mgr().file(name).remove();
        YOMK_TEST_CHECK(
            del == yomk::payload::ResStatus::Succeeded,
            "S8: 同名双表一次删除 ResStatus::Succeeded（msg 计数 deleted console:1 file:1）");
        YOMK_TEST_CHECK(
            (yomk::log_mgr().console(name).get_info().m_status == yomk::payload::ResStatus::Failed &&
             yomk::log_mgr().file(name).get_info().m_status == yomk::payload::ResStatus::Failed),
            "S8: 双表删除后按类型单查均 ResStatus::Failed（两表均已清理）");
        std::string out2;
        auto del2 = yomk::payload::ResStatus::Succeeded;
        {
            CoutCapture cap;
            del2 = yomk::log_mgr().file(name).remove();
            out2 = cap.str();
        }
        YOMK_TEST_CHECK(
            del2 == yomk::payload::ResStatus::Failed, "S8: 重复删除返回 ResStatus::Failed（幂等）");
        YOMK_TEST_CHECK(
            out2.find("logger: lg1_s8_both_tables not found.") != std::string::npos,
            "S8: 重复删除错误提示含 logger 名");
    }

    // 8.6 内省行数递减守恒 + ALL 与 LOGGERS 行数关系 + main 删除后惰性重建
    {
        fs::path dir = tmpDir / "s8_count";
        fs::create_directories(dir, ec);
        const std::string name = "lg1_s8_count";
        const size_t before = loggersTotal();
        YOMK_TEST_CHECK(
            consoleInfo(name, "lg1_s8_count_marker") == yomk::payload::ResStatus::Succeeded, "S8: 计数用例 console 侧创建 ResStatus::Succeeded");
        YOMK_TEST_CHECK(yomk::log_mgr().dir(dir.string()).file(name).create() == yomk::payload::ResStatus::Succeeded, "S8: 计数用例 file 侧创建 ResStatus::Succeeded");
        YOMK_TEST_CHECK(loggersTotal() == before + 2, "S8: 双表各建一个 → LOGGERS 行数 +2");
        YOMK_TEST_CHECK(yomk::log_mgr().file(name).remove() == yomk::payload::ResStatus::Succeeded, "S8: 计数用例删除 ResStatus::Succeeded");
        YOMK_TEST_CHECK(loggersTotal() == before, "S8: 删除后 LOGGERS 行数回落（递减守恒）");
        auto listPkg = yomk::msgs::unpack<yomk::msgs::LoggerInfoList>(yomk::log_mgr().all().m_data);
        const yomk::payload::LoggerState* st = firstConsoleState(listPkg);
        YOMK_TEST_CHECK(
            (listPkg != nullptr && listPkg->d.size() == before && st != nullptr && st->m_traceOn && st->m_debugOn &&
             st->m_infoOn && st->m_warnOn && st->m_errorOn && st->m_fatalOn && st->m_proxyOn),
            "S8: ALL 日志器数 = LOGGERS 行数，控制台状态六级全 on 不变");

        // main 无特殊保护：可删；重建须走"空 logger 名回退"路径
        // （log().info 链的 logger 名是裸 "main"，[行号] 拼在内容头部）
        YOMK_TEST_CHECK(
            yomk::log_mgr().file("main").remove() == yomk::payload::ResStatus::Succeeded,
            "S8: main 可删除 ResStatus::Succeeded（无特殊保护）");
        YOMK_TEST_CHECK(yomk::log_mgr().console("main").get_info().m_status == yomk::payload::ResStatus::Failed, "S8: main 删除后单查 ResStatus::Failed");
        std::string rebuildOut;
        yomk::payload::Response rebuild(yomk::payload::ResStatus::Invalid);
        {
            CoutCapture cap;
            rebuild = yomk::server().url("/YomkLogger/console_log").msg(yomk::msgs::Log::shared(yomk::payload::Log{yomk::payload::LogLevel::Info, "lg1_s8_main_rebuild_marker", ""})).request();
            rebuildOut = cap.str();
        }
        YOMK_TEST_CHECK(rebuild.m_status == yomk::payload::ResStatus::Succeeded, "S8: main 删除后空名回退写入 ResStatus::Succeeded");
        YOMK_TEST_CHECK(
            yomk::log_mgr().console("main").get_info().m_status == yomk::payload::ResStatus::Succeeded,
            "S8: main 惰性重建后单查恢复 ResStatus::Succeeded");
        YOMK_TEST_CHECK(
            rebuildOut.find("[main] lg1_s8_main_rebuild_marker") != std::string::npos,
            "S8: 重建对象名确为 main（输出行 [main]）");
        YOMK_TEST_CHECK(
            loggersTotal() == before,
            "S8: main 删除+重建后行数回到基线（无泄漏无重复）");
    }

    // 8.7 异常路径：空包 / 错类型包（unpack 契约分支）
    {
        auto rNull = yomk::server().url("/YomkLogger/delete_logger").request();
        YOMK_TEST_CHECK(
            (rNull.m_status == yomk::payload::ResStatus::Failed && rNull.m_msg == "pkg is null or pkg is not String"),
            "S8: delete_logger nullptr 包返回 ResStatus::Failed + 契约消息");
        auto rWrong = yomk::server().url("/YomkLogger/delete_logger").msg(yomk::msgs::Log::shared(yomk::payload::Log{yomk::payload::LogLevel::Info, "x", "y"})).request();
        YOMK_TEST_CHECK(
            (rWrong.m_status == yomk::payload::ResStatus::Failed && rWrong.m_msg == "pkg is null or pkg is not String"),
            "S8: delete_logger 错类型包（Log）返回 ResStatus::Failed + 契约消息");
    }
}

// ============ Section 9: 服务级具名开关与全局态端点（两级主闸并联） ============
{
    const std::string tagA = "lg1_s9_tagA";
    const std::string tagB = "lg1_s9_tagB";

    // 9.1 具名 OFF 隔离：关 tagA 的 info，tagB 输出不受影响
    {
        CoutCapture cap;
        YOMK_TEST_CHECK(yomk::log().tag(tagA).info("lg1_s9a_pre_a_marker") == yomk::payload::ResStatus::Succeeded, "S9a: tagA 预写 ResStatus::Succeeded");
        YOMK_TEST_CHECK(yomk::log().tag(tagB).info("lg1_s9a_pre_b_marker") == yomk::payload::ResStatus::Succeeded, "S9a: tagB 预写 ResStatus::Succeeded");
    }
    YOMK_TEST_CHECK(
        yomk::log_mgr().console(tagA).level(yomk::payload::LogLevel::Info).disable() == yomk::payload::ResStatus::Succeeded,
        "S9a: 具名 console(tagA).level(Info).disable() 返回 ResStatus::Succeeded");
    {
        CoutCapture cap;
        YOMK_TEST_CHECK(
            yomk::log().tag(tagA).info("lg1_s9a_off_a_marker") == yomk::payload::ResStatus::Succeeded,
            "S9a: 具名关断后 tagA 调用仍 ResStatus::Succeeded");
        YOMK_TEST_CHECK(
            yomk::log().tag(tagB).info("lg1_s9a_off_b_marker") == yomk::payload::ResStatus::Succeeded,
            "S9a: 具名关断后 tagB 调用 ResStatus::Succeeded");
        YOMK_TEST_CHECK(cap.str().find("lg1_s9a_off_a_marker") == std::string::npos, "S9a: 具名关断后 tagA 无输出");
        YOMK_TEST_CHECK(cap.str().find("lg1_s9a_off_b_marker") != std::string::npos, "S9a: 具名关断不波及 tagB（逐器隔离）");
    }

    // 9.2 具名 ON 恢复
    YOMK_TEST_CHECK(
        yomk::log_mgr().console(tagA).level(yomk::payload::LogLevel::Info).enable() == yomk::payload::ResStatus::Succeeded,
        "S9b: 具名 console(tagA).level(Info).enable() 返回 ResStatus::Succeeded");
    {
        CoutCapture cap;
        YOMK_TEST_CHECK(yomk::log().tag(tagA).info("lg1_s9b_on_a_marker") == yomk::payload::ResStatus::Succeeded, "S9b: 恢复后 tagA 调用 ResStatus::Succeeded");
        YOMK_TEST_CHECK(cap.str().find("lg1_s9b_on_a_marker") != std::string::npos, "S9b: 具名恢复后 tagA 输出回归");
    }

    // 9.3 具名未知名 ResStatus::Failed（不惰性创建）
    {
        auto offNo = yomk::log_mgr().console("lg1_s9_never_created").level(yomk::payload::LogLevel::Info).disable();
        YOMK_TEST_CHECK(
            offNo == yomk::payload::ResStatus::Failed,
            "S9c: 具名 OFF 未知名返回 ResStatus::Failed + console logger not found.");
        auto onNo = yomk::log_mgr().console("lg1_s9_never_created").level(yomk::payload::LogLevel::Info).enable();
        YOMK_TEST_CHECK(
            onNo == yomk::payload::ResStatus::Failed,
            "S9c: 具名 ON 未知名返回 ResStatus::Failed");
        YOMK_TEST_CHECK(
            yomk::log_mgr().console("lg1_s9_never_created").get_info().m_status == yomk::payload::ResStatus::Failed,
            "S9c: 未知名未被惰性创建（单查 ResStatus::Failed）");
    }

    // 9.4 主闸×逐器组合 + 两层归因：全局 OFF + 逐器 ON → 仍无输出（两层 AND），
    // 全局态与条目逐器态各自真实呈现、归因唯一
    {
        YOMK_TEST_CHECK(yomk::log_mgr().console().level(yomk::payload::LogLevel::Info).disable() == yomk::payload::ResStatus::Succeeded, "S9d: 无参 OFF（全局主闸）返回 ResStatus::Succeeded");
        {
            CoutCapture cap;
            YOMK_TEST_CHECK(
                yomk::log().tag(tagA).info("lg1_s9d_global_off_a_marker") == yomk::payload::ResStatus::Succeeded,
                "S9d: 主闸关断后 tagA 调用仍 ResStatus::Succeeded");
            YOMK_TEST_CHECK(
                cap.str().find("lg1_s9d_global_off_a_marker") == std::string::npos,
                "S9d: 主闸 OFF + 逐器 ON → 仍无输出（两层 AND）");
        }
        auto gResp = yomk::log_mgr().console().status();
        auto gs = yomk::msgs::unpack<yomk::msgs::LoggerState>(gResp.m_data);
        YOMK_TEST_CHECK(gs != nullptr && !gs->d.m_infoOn, "S9d: console().status() 报主闸 infoOff");
        auto liResp = yomk::log_mgr().console(tagA).get_info();
        auto lp = yomk::msgs::unpack<yomk::msgs::LoggerInfo>(liResp.m_data);
        YOMK_TEST_CHECK(
            lp != nullptr && lp->d.m_loggerState.m_infoOn,
            "S9d: 条目报逐器 infoOn（与主闸态相反，两层归因唯一）");
        YOMK_TEST_CHECK(yomk::log_mgr().console().level(yomk::payload::LogLevel::Info).enable() == yomk::payload::ResStatus::Succeeded, "S9d: 无参 ON（全局主闸）恢复 ResStatus::Succeeded");
        {
            CoutCapture cap;
            YOMK_TEST_CHECK(
                yomk::log().tag(tagA).info("lg1_s9d_global_on_a_marker") == yomk::payload::ResStatus::Succeeded,
                "S9d: 主闸恢复后 tagA 调用 ResStatus::Succeeded");
            YOMK_TEST_CHECK(
                cap.str().find("lg1_s9d_global_on_a_marker") != std::string::npos,
                "S9d: 主闸恢复后输出回归（逐器仍 on）");
        }
    }

    // 9.5 条目逐器真值与两层正交：具名关断直接反映在条目、不波及全局态
    {
        auto liAResp = yomk::log_mgr().console(tagA).get_info();
        auto la = yomk::msgs::unpack<yomk::msgs::LoggerInfo>(liAResp.m_data);
        YOMK_TEST_CHECK(la != nullptr && la->d.m_loggerState.m_infoOn, "S9e: tagA 条目 infoOn（具名恢复后逐器态）");
        auto liBResp = yomk::log_mgr().console(tagB).get_info();
        auto lb = yomk::msgs::unpack<yomk::msgs::LoggerInfo>(liBResp.m_data);
        YOMK_TEST_CHECK(
            lb != nullptr && lb->d.m_loggerState.m_infoOn,
            "S9e: tagB 条目 infoOn（未被具名触碰恒 true）");
        YOMK_TEST_CHECK(yomk::log_mgr().console(tagA).level(yomk::payload::LogLevel::Info).disable() == yomk::payload::ResStatus::Succeeded, "S9e: 再次具名关断 tagA 返回 ResStatus::Succeeded");
        auto gResp = yomk::log_mgr().console().status();
        auto gs = yomk::msgs::unpack<yomk::msgs::LoggerState>(gResp.m_data);
        YOMK_TEST_CHECK(gs != nullptr && gs->d.m_infoOn, "S9e: 具名关断不影响全局主闸（两层正交）");
        auto liA2Resp = yomk::log_mgr().console(tagA).get_info();
        auto la2 = yomk::msgs::unpack<yomk::msgs::LoggerInfo>(liA2Resp.m_data);
        YOMK_TEST_CHECK(
            la2 != nullptr && !la2->d.m_loggerState.m_infoOn,
            "S9e: tagA 条目报逐器 infoOff（具名关断直接反映）");
        YOMK_TEST_CHECK(yomk::log_mgr().console(tagA).level(yomk::payload::LogLevel::Info).enable() == yomk::payload::ResStatus::Succeeded, "S9e: 具名恢复 tagA 返回 ResStatus::Succeeded（状态复原）");
    }

    // 9.6 全局态端点基线：console().status() 解 LoggerState 包 + 主闸翻转联动
    {
        auto gResp = yomk::log_mgr().console().status();
        YOMK_TEST_CHECK(
            (gResp.m_status == yomk::payload::ResStatus::Succeeded && gResp.m_msg == "ok"),
            "S9f: console().status() 返回 ResStatus::Succeeded + ok");
        auto gs = yomk::msgs::unpack<yomk::msgs::LoggerState>(gResp.m_data);
        YOMK_TEST_CHECK(
            (gs != nullptr && gs->d.m_traceOn && gs->d.m_debugOn && gs->d.m_infoOn && gs->d.m_warnOn &&
             gs->d.m_errorOn && gs->d.m_fatalOn),
            "S9f: 全局主闸六级全 on（m_traceOn/m_fatalOn 初值 true）");
        YOMK_TEST_CHECK(gs != nullptr && gs->d.m_proxyOn, "S9f: 全局态含 S3 已设 proxyOn");
        YOMK_TEST_CHECK(yomk::log_mgr().console().level(yomk::payload::LogLevel::Debug).disable() == yomk::payload::ResStatus::Succeeded, "S9f: 无参 OFF_DEBUG 返回 ResStatus::Succeeded");
        auto gResp2 = yomk::log_mgr().console().status();
        auto gs2 = yomk::msgs::unpack<yomk::msgs::LoggerState>(gResp2.m_data);
        YOMK_TEST_CHECK(gs2 != nullptr && !gs2->d.m_debugOn, "S9f: 主闸 OFF_DEBUG 后全局态 debugOff");
        YOMK_TEST_CHECK(yomk::log_mgr().console().level(yomk::payload::LogLevel::Debug).enable() == yomk::payload::ResStatus::Succeeded, "S9f: 无参 ON_DEBUG 恢复 ResStatus::Succeeded");
        auto gResp3 = yomk::log_mgr().console().status();
        auto gs3 = yomk::msgs::unpack<yomk::msgs::LoggerState>(gResp3.m_data);
        YOMK_TEST_CHECK(gs3 != nullptr && gs3->d.m_debugOn, "S9f: 主闸 ON_DEBUG 后全局态 debugOn");
        // TRACE 主闸翻转联动（六等级扩展新增）
        YOMK_TEST_CHECK(yomk::log_mgr().console().level(yomk::payload::LogLevel::Trace).disable() == yomk::payload::ResStatus::Succeeded, "S9f: 无参 OFF_TRACE 返回 ResStatus::Succeeded");
        auto gResp4 = yomk::log_mgr().console().status();
        auto gs4 = yomk::msgs::unpack<yomk::msgs::LoggerState>(gResp4.m_data);
        YOMK_TEST_CHECK(gs4 != nullptr && !gs4->d.m_traceOn, "S9f: 主闸 OFF_TRACE 后全局态 traceOff");
        YOMK_TEST_CHECK(yomk::log_mgr().console().level(yomk::payload::LogLevel::Trace).enable() == yomk::payload::ResStatus::Succeeded, "S9f: 无参 ON_TRACE 恢复 ResStatus::Succeeded");
        auto gResp5 = yomk::log_mgr().console().status();
        auto gs5 = yomk::msgs::unpack<yomk::msgs::LoggerState>(gResp5.m_data);
        YOMK_TEST_CHECK(gs5 != nullptr && gs5->d.m_traceOn, "S9f: 主闸 ON_TRACE 后全局态 traceOn");
    }

    // 9.7 具名 Trace 门控：关 tagA 的 trace，tagB 不受影响，条目逐器真值翻转（六等级扩展新增）
    {
        YOMK_TEST_CHECK(
            yomk::log_mgr().console(tagA).level(yomk::payload::LogLevel::Trace).disable() == yomk::payload::ResStatus::Succeeded,
            "S9g: 具名 console(tagA).level(Trace).disable() 返回 ResStatus::Succeeded");
        {
            CoutCapture cap;
            YOMK_TEST_CHECK(
                yomk::log().tag(tagA).trace("lg1_s9g_off_a_marker") == yomk::payload::ResStatus::Succeeded,
                "S9g: 具名关断后 tagA 调用仍 ResStatus::Succeeded");
            YOMK_TEST_CHECK(
                yomk::log().tag(tagB).trace("lg1_s9g_off_b_marker") == yomk::payload::ResStatus::Succeeded,
                "S9g: 具名关断后 tagB 调用 ResStatus::Succeeded");
            YOMK_TEST_CHECK(cap.str().find("lg1_s9g_off_a_marker") == std::string::npos, "S9g: 具名关断后 tagA Trace 无输出");
            YOMK_TEST_CHECK(cap.str().find("lg1_s9g_off_b_marker") != std::string::npos, "S9g: 具名关断不波及 tagB（逐器隔离）");
        }
        auto liResp = yomk::log_mgr().console(tagA).get_info();
        auto lp = yomk::msgs::unpack<yomk::msgs::LoggerInfo>(liResp.m_data);
        YOMK_TEST_CHECK(lp != nullptr && !lp->d.m_loggerState.m_traceOn, "S9g: 条目报逐器 traceOff（具名关断直接反映）");
        YOMK_TEST_CHECK(
            yomk::log_mgr().console(tagA).level(yomk::payload::LogLevel::Trace).enable() == yomk::payload::ResStatus::Succeeded,
            "S9g: 具名 console(tagA).level(Trace).enable() 恢复 ResStatus::Succeeded");
        {
            CoutCapture cap;
            yomk::log().tag(tagA).trace("lg1_s9g_on_a_marker");
            YOMK_TEST_CHECK(cap.str().find("lg1_s9g_on_a_marker") != std::string::npos, "S9g: 具名恢复后 tagA Trace 输出回归");
        }
    }
}

// ============ Section 10: 文件日志两级开关与全局态端点 ============
{
    const std::string flName = "lg1_s10_fl";
    fs::path s10Dir = tmpDir / "s10";
    fs::create_directories(s10Dir, ec);
    YOMK_TEST_CHECK(
        yomk::log_mgr().dir(s10Dir.string()).file(flName).create() == yomk::payload::ResStatus::Succeeded,
        "S10: 建文件日志器 lg1_s10_fl 成功");
    fs::path s10LogFile = s10Dir / (flName + ".log");

    // 10.1 具名 OFF 隔离：关 lg1_s10_fl 的 info，warn 落盘不受影响
    YOMK_TEST_CHECK(
        yomk::log_mgr().file(flName).level(yomk::payload::LogLevel::Info).disable() == yomk::payload::ResStatus::Succeeded,
        "S10a: 具名 disableFileLogInfo(fl) 返回 ResStatus::Succeeded");
    {
        auto liResp = yomk::log_mgr().file(flName).get_info();
        auto lp = yomk::msgs::unpack<yomk::msgs::LoggerInfo>(liResp.m_data);
        YOMK_TEST_CHECK(
            lp != nullptr && !lp->d.m_loggerState.m_infoOn && lp->d.m_loggerState.m_warnOn,
            "S10a: 单查条目 infoOff 且 warnOn（文件逐器门控真值）");
    }
    YOMK_TEST_CHECK(
        yomk::log().to_sink(flName).tag("lg1s10").info("lg1_s10a_off_marker") == yomk::payload::ResStatus::Succeeded,
        "S10a: 关断后 FILE_INFO 仍 ResStatus::Succeeded");
    YOMK_TEST_CHECK(
        yomk::log().to_sink(flName).tag("lg1s10").warn("lg1_s10a_warn_marker") == yomk::payload::ResStatus::Succeeded,
        "S10a: WARN 写入 ResStatus::Succeeded");
    YOMK_TEST_CHECK(yomk::log_mgr().file(flName).flush() == yomk::payload::ResStatus::Succeeded, "S10a: WRITE ResStatus::Succeeded");
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
        yomk::log_mgr().file(flName).level(yomk::payload::LogLevel::Info).enable() == yomk::payload::ResStatus::Succeeded,
        "S10b: 具名 file(fl).level(Info).enable() 返回 ResStatus::Succeeded");
    {
        auto liResp = yomk::log_mgr().file(flName).get_info();
        auto lp = yomk::msgs::unpack<yomk::msgs::LoggerInfo>(liResp.m_data);
        YOMK_TEST_CHECK(lp != nullptr && lp->d.m_loggerState.m_infoOn, "S10b: 条目 infoOn 恢复");
    }
    YOMK_TEST_CHECK(
        yomk::log().to_sink(flName).tag("lg1s10").info("lg1_s10b_on_marker") == yomk::payload::ResStatus::Succeeded,
        "S10b: 恢复后 FILE_INFO ResStatus::Succeeded");
    YOMK_TEST_CHECK(yomk::log_mgr().file(flName).flush() == yomk::payload::ResStatus::Succeeded, "S10b: WRITE ResStatus::Succeeded");
    {
        bool ok = false;
        std::string content = readFile(s10LogFile, ok);
        YOMK_TEST_CHECK(ok && content.find("lg1_s10b_on_marker") != std::string::npos, "S10b: 恢复后落盘回归");
    }

    // 10.3 具名未知名 ResStatus::Failed（不惰性创建）
    {
        auto offNo = yomk::log_mgr().file("lg1_s10_never_created").level(yomk::payload::LogLevel::Info).disable();
        YOMK_TEST_CHECK(
            offNo == yomk::payload::ResStatus::Failed,
            "S10c: 具名 OFF 未知名返回 ResStatus::Failed + file logger not found.");
        auto onNo = yomk::log_mgr().file("lg1_s10_never_created").level(yomk::payload::LogLevel::Info).enable();
        YOMK_TEST_CHECK(
            onNo == yomk::payload::ResStatus::Failed,
            "S10c: 具名 ON 未知名返回 ResStatus::Failed");
        auto arr = yomk::msgs::unpack<yomk::msgs::StringArray>(yomk::log_mgr().file().list().m_data);
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
        YOMK_TEST_CHECK(yomk::log_mgr().file().level(yomk::payload::LogLevel::Info).disable() == yomk::payload::ResStatus::Succeeded, "S10d: 无参 OFF（文件主闸）返回 ResStatus::Succeeded");
        auto gResp = yomk::log_mgr().file().status();
        auto gs = yomk::msgs::unpack<yomk::msgs::LoggerState>(gResp.m_data);
        YOMK_TEST_CHECK(gs != nullptr && !gs->d.m_infoOn, "S10d: file().status() 报文件主闸 infoOff");
        auto liResp = yomk::log_mgr().file(flName).get_info();
        auto lp = yomk::msgs::unpack<yomk::msgs::LoggerInfo>(liResp.m_data);
        YOMK_TEST_CHECK(
            lp != nullptr && lp->d.m_loggerState.m_infoOn,
            "S10d: 条目报逐器 infoOn（与文件主闸态相反，两层归因唯一）");
        YOMK_TEST_CHECK(
            yomk::log().to_sink(flName).tag("lg1s10").info("lg1_s10d_gate_marker") == yomk::payload::ResStatus::Succeeded,
            "S10d: 主闸关断后 FILE_INFO 仍 ResStatus::Succeeded");
        YOMK_TEST_CHECK(yomk::log_mgr().file(flName).flush() == yomk::payload::ResStatus::Succeeded, "S10d: WRITE ResStatus::Succeeded");
        {
            bool ok = false;
            std::string content = readFile(s10LogFile, ok);
            YOMK_TEST_CHECK(
                ok && content.find("lg1_s10d_gate_marker") == std::string::npos,
                "S10d: 主闸 OFF + 逐器 ON → 仍不落盘（两层 AND）");
        }
        YOMK_TEST_CHECK(yomk::log_mgr().file().level(yomk::payload::LogLevel::Info).enable() == yomk::payload::ResStatus::Succeeded, "S10d: 无参 ON（文件主闸）恢复 ResStatus::Succeeded");
        YOMK_TEST_CHECK(
            yomk::log().to_sink(flName).tag("lg1s10").info("lg1_s10d_on_marker") == yomk::payload::ResStatus::Succeeded,
            "S10d: 主闸恢复后 FILE_INFO ResStatus::Succeeded");
        YOMK_TEST_CHECK(yomk::log_mgr().file(flName).flush() == yomk::payload::ResStatus::Succeeded, "S10d: WRITE ResStatus::Succeeded");
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
        YOMK_TEST_CHECK(yomk::log_mgr().file(flName).level(yomk::payload::LogLevel::Info).disable() == yomk::payload::ResStatus::Succeeded, "S10e: 再次具名关断返回 ResStatus::Succeeded");
        auto gResp = yomk::log_mgr().file().status();
        auto gs = yomk::msgs::unpack<yomk::msgs::LoggerState>(gResp.m_data);
        YOMK_TEST_CHECK(gs != nullptr && gs->d.m_infoOn, "S10e: 具名关断不影响文件主闸（两层正交）");
        YOMK_TEST_CHECK(yomk::log_mgr().file(flName).level(yomk::payload::LogLevel::Info).enable() == yomk::payload::ResStatus::Succeeded, "S10e: 具名恢复 ResStatus::Succeeded（状态复原）");
    }

    // 10.6 全局态端点基线：file().status()/console().status() 两套主闸独立 + 主闸翻转联动
    {
        auto gResp = yomk::log_mgr().file().status();
        YOMK_TEST_CHECK(
            (gResp.m_status == yomk::payload::ResStatus::Succeeded && gResp.m_msg == "ok"),
            "S10f: file().status() 返回 ResStatus::Succeeded + ok");
        auto gs = yomk::msgs::unpack<yomk::msgs::LoggerState>(gResp.m_data);
        YOMK_TEST_CHECK(
            (gs != nullptr && gs->d.m_traceOn && gs->d.m_debugOn && gs->d.m_infoOn && gs->d.m_warnOn &&
             gs->d.m_errorOn && gs->d.m_fatalOn),
            "S10f: 文件主闸六级全 on（m_traceOn/m_fatalOn 初值 true）");
        YOMK_TEST_CHECK(
            gs != nullptr && !gs->d.m_proxyOn,
            "S10f: 文件态 proxyOn=false（文件代理未设置时恒 false）");
        YOMK_TEST_CHECK(yomk::log_mgr().file().level(yomk::payload::LogLevel::Debug).disable() == yomk::payload::ResStatus::Succeeded, "S10f: 无参 disableFileLogDebug 返回 ResStatus::Succeeded");
        auto gResp2 = yomk::log_mgr().file().status();
        auto gs2 = yomk::msgs::unpack<yomk::msgs::LoggerState>(gResp2.m_data);
        YOMK_TEST_CHECK(gs2 != nullptr && !gs2->d.m_debugOn, "S10f: 文件主闸 OFF_DEBUG 后文件态 debugOff");
        // 两套主闸独立直证：文件主闸翻转不波及控制台主闸
        auto gResp3 = yomk::log_mgr().console().status();
        auto gc = yomk::msgs::unpack<yomk::msgs::LoggerState>(gResp3.m_data);
        YOMK_TEST_CHECK(
            (gc != nullptr && gc->d.m_traceOn && gc->d.m_debugOn && gc->d.m_infoOn && gc->d.m_warnOn &&
             gc->d.m_errorOn && gc->d.m_fatalOn),
            "S10f: 控制台主闸六级仍全 on（与文件主闸独立）");
        YOMK_TEST_CHECK(yomk::log_mgr().file().level(yomk::payload::LogLevel::Debug).enable() == yomk::payload::ResStatus::Succeeded, "S10f: 主闸 file().level(Debug).enable() 恢复 ResStatus::Succeeded");
        auto gResp4 = yomk::log_mgr().file().status();
        auto gs4 = yomk::msgs::unpack<yomk::msgs::LoggerState>(gResp4.m_data);
        YOMK_TEST_CHECK(gs4 != nullptr && gs4->d.m_debugOn, "S10f: 文件主闸 ON_DEBUG 后文件态 debugOn");
        // FATAL 文件主闸翻转联动（六等级扩展新增）
        YOMK_TEST_CHECK(yomk::log_mgr().file().level(yomk::payload::LogLevel::Fatal).disable() == yomk::payload::ResStatus::Succeeded, "S10f: 无参 disableFileLogFatal 返回 ResStatus::Succeeded");
        auto gResp5 = yomk::log_mgr().file().status();
        auto gs5 = yomk::msgs::unpack<yomk::msgs::LoggerState>(gResp5.m_data);
        YOMK_TEST_CHECK(gs5 != nullptr && !gs5->d.m_fatalOn, "S10f: 文件主闸 OFF_FATAL 后文件态 fatalOff");
        YOMK_TEST_CHECK(yomk::log_mgr().file().level(yomk::payload::LogLevel::Fatal).enable() == yomk::payload::ResStatus::Succeeded, "S10f: 主闸 file().level(Fatal).enable() 恢复 ResStatus::Succeeded");
        auto gResp6 = yomk::log_mgr().file().status();
        auto gs6 = yomk::msgs::unpack<yomk::msgs::LoggerState>(gResp6.m_data);
        YOMK_TEST_CHECK(gs6 != nullptr && gs6->d.m_fatalOn, "S10f: 文件主闸 ON_FATAL 后文件态 fatalOn");
        // 未知类型不可表达：type 由分面身份承载
    }

    // 10.7 文件主闸 FATAL 关断隔离：Fatal 不落盘，其余级别不受影响，恢复后落盘回归（六等级扩展新增）
    {
        YOMK_TEST_CHECK(yomk::log_mgr().file().level(yomk::payload::LogLevel::Fatal).disable() == yomk::payload::ResStatus::Succeeded, "S10g: 无参 disableFileLogFatal 返回 ResStatus::Succeeded");
        YOMK_TEST_CHECK(
            yomk::log().to_sink(flName).tag("lg1s10").fatal("lg1_s10g_off_fatal_marker") == yomk::payload::ResStatus::Succeeded,
            "S10g: 主闸关断后 FILE_FATAL 仍 ResStatus::Succeeded");
        YOMK_TEST_CHECK(
            yomk::log().to_sink(flName).tag("lg1s10").error("lg1_s10g_error_marker") == yomk::payload::ResStatus::Succeeded,
            "S10g: 关断 FATAL 后 FILE_ERROR 不受影响");
        YOMK_TEST_CHECK(yomk::log_mgr().file(flName).flush() == yomk::payload::ResStatus::Succeeded, "S10g: WRITE ResStatus::Succeeded");
        {
            bool ok = false;
            std::string content = readFile(s10LogFile, ok);
            YOMK_TEST_CHECK(ok && content.find("lg1_s10g_off_fatal_marker") == std::string::npos, "S10g: 主闸 FATAL 关断后不落盘");
            YOMK_TEST_CHECK(ok && content.find("lg1_s10g_error_marker") != std::string::npos, "S10g: ERROR 落盘不受影响");
        }
        YOMK_TEST_CHECK(yomk::log_mgr().file().level(yomk::payload::LogLevel::Fatal).enable() == yomk::payload::ResStatus::Succeeded, "S10g: 主闸 file().level(Fatal).enable() 恢复 ResStatus::Succeeded");
        YOMK_TEST_CHECK(
            yomk::log().to_sink(flName).tag("lg1s10").fatal("lg1_s10g_on_fatal_marker") == yomk::payload::ResStatus::Succeeded,
            "S10g: 主闸恢复后 FILE_FATAL ResStatus::Succeeded");
        YOMK_TEST_CHECK(yomk::log_mgr().file(flName).flush() == yomk::payload::ResStatus::Succeeded, "S10g: 恢复后 WRITE ResStatus::Succeeded");
        {
            bool ok = false;
            std::string content = readFile(s10LogFile, ok);
            YOMK_TEST_CHECK(ok && content.find("lg1_s10g_on_fatal_marker") != std::string::npos, "S10g: 主闸 FATAL 恢复后落盘回归");
        }
    }
}

// ============ Section 11: 文件日志全局代理契约（与控制台 S3 同构） ============
{
    const std::string flName11 = "lg1_s11_fl";
    fs::path s11Dir = tmpDir / "s11";
    fs::create_directories(s11Dir, ec);
    YOMK_TEST_CHECK(
        yomk::log_mgr().dir(s11Dir.string()).file(flName11).create() == yomk::payload::ResStatus::Succeeded,
        "S11: 建文件日志器 lg1_s11_fl 成功");
    fs::path s11LogFile = s11Dir / (flName11 + ".log");

    // 11.1 设置前基线：文件代理未设置 + 两套代理独立直证
    {
        auto gRespF = yomk::log_mgr().file().status();
        auto gsF = yomk::msgs::unpack<yomk::msgs::LoggerState>(gRespF.m_data);
        YOMK_TEST_CHECK(gsF != nullptr && !gsF->d.m_proxyOn, "S11a: 设置前 status()(\"file\") proxyOff");
        auto gRespC = yomk::log_mgr().console().status();
        auto gsC = yomk::msgs::unpack<yomk::msgs::LoggerState>(gRespC.m_data);
        YOMK_TEST_CHECK(
            gsC != nullptr && gsC->d.m_proxyOn,
            "S11a: 设置前 status()(\"console\") proxyOn（S3 重装态），两套代理独立直证");
    }

    // 11.2 拦截态：文件 proxy 返回 false → ResStatus::Succeeded + 专属消息 + 不落盘
    g_fileProxyHits.store(0);
    g_fileProxyIntercept.store(true);
    auto fileProxy = [](const yomk::payload::Log&) -> bool
    {
        g_fileProxyHits.fetch_add(1);
        return !g_fileProxyIntercept.load();  // 拦截态返回 false，穿透态返回 true
    };
    YOMK_TEST_CHECK(yomk::log_mgr().file().proxy(fileProxy).apply() == yomk::payload::ResStatus::Succeeded, "S11b: file().proxy().apply() 返回 ResStatus::Succeeded");
    {
        auto r = yomk::log().to_sink(flName11).tag("lg1s11").info("lg1_s11b_intercept_marker");
        YOMK_TEST_CHECK(r == yomk::payload::ResStatus::Succeeded, "S11b: 拦截态 FILE_INFO 返回 ResStatus::Succeeded");
        YOMK_TEST_CHECK(yomk::log_mgr().file(flName11).flush() == yomk::payload::ResStatus::Succeeded, "S11b: WRITE ResStatus::Succeeded");
        {
            bool ok = false;
            std::string content = readFile(s11LogFile, ok);
            YOMK_TEST_CHECK(
                ok && content.find("lg1_s11b_intercept_marker") == std::string::npos,
                "S11b: 拦截态不落盘（marker 缺席）");
        }
        YOMK_TEST_CHECK(g_fileProxyHits.load() == 1, "S11b: 文件 proxy 被调用 1 次（原子计数）");
    }

    // 11.3 穿透态：文件 proxy 返回 true → 走默认落盘；file().status() proxy 联动
    {
        g_fileProxyIntercept.store(false);
        auto r = yomk::log().to_sink(flName11).tag("lg1s11").info("lg1_s11c_pass_marker");
        YOMK_TEST_CHECK(r == yomk::payload::ResStatus::Succeeded, "S11c: 穿透态 FILE_INFO ResStatus::Succeeded + success.");
        YOMK_TEST_CHECK(yomk::log_mgr().file(flName11).flush() == yomk::payload::ResStatus::Succeeded, "S11c: WRITE ResStatus::Succeeded");
        {
            bool ok = false;
            std::string content = readFile(s11LogFile, ok);
            YOMK_TEST_CHECK(
                ok && content.find("lg1_s11c_pass_marker") != std::string::npos,
                "S11c: 穿透态恢复默认落盘（marker 命中）");
        }
        YOMK_TEST_CHECK(g_fileProxyHits.load() == 2, "S11c: 文件 proxy 累计被调用 2 次");
        auto gRespF = yomk::log_mgr().file().status();
        auto gsF = yomk::msgs::unpack<yomk::msgs::LoggerState>(gRespF.m_data);
        YOMK_TEST_CHECK(gsF != nullptr && gsF->d.m_proxyOn, "S11c: 设置后 status()(\"file\") proxyOn");
    }

    // 11.4 卸载：先把代理切回拦截态确认在链路上，再传 nullptr 卸载
    {
        g_fileProxyIntercept.store(true);
        auto r = yomk::log().to_sink(flName11).tag("lg1s11").info("lg1_s11d_pre_unload_marker");
        YOMK_TEST_CHECK(
            r == yomk::payload::ResStatus::Succeeded,
            "S11d: 卸载前拦截态契约消息（文件代理确实在链路上）");
        YOMK_TEST_CHECK(
            yomk::log_mgr().file().proxy(nullptr).apply() == yomk::payload::ResStatus::Succeeded,
            "S11d: file().proxy(nullptr).apply() 卸载返回 ResStatus::Succeeded");
        auto gRespF = yomk::log_mgr().file().status();
        auto gsF = yomk::msgs::unpack<yomk::msgs::LoggerState>(gRespF.m_data);
        YOMK_TEST_CHECK(gsF != nullptr && !gsF->d.m_proxyOn, "S11d: 卸载后 status()(\"file\") 回 proxyOff");
        const long hitsBefore = g_fileProxyHits.load();
        YOMK_TEST_CHECK(
            yomk::log().to_sink(flName11).tag("lg1s11").info("lg1_s11d_post_unload_marker") == yomk::payload::ResStatus::Succeeded,
            "S11d: 卸载后 FILE_INFO ResStatus::Succeeded（走默认路径而非代理短路）");
        YOMK_TEST_CHECK(yomk::log_mgr().file(flName11).flush() == yomk::payload::ResStatus::Succeeded, "S11d: WRITE ResStatus::Succeeded");
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
        YOMK_TEST_CHECK(yomk::log_mgr().file().proxy(fileProxy).apply() == yomk::payload::ResStatus::Succeeded, "S11e: 重装文件代理返回 ResStatus::Succeeded（卸载可逆）");
        auto gRespF = yomk::log_mgr().file().status();
        auto gsF = yomk::msgs::unpack<yomk::msgs::LoggerState>(gRespF.m_data);
        YOMK_TEST_CHECK(gsF != nullptr && gsF->d.m_proxyOn, "S11e: 重装后 status()(\"file\") 恢复 proxyOn");
    }
}

// ============ 收尾清理 ============
yomk::server().shutdown();
fs::remove_all(tmpDir, ec);

if (g_failed == 0)
{
    std::cout << "TestYomkLoggerLifecycle all check passed." << std::endl;
    return 0;
}
std::cout << "TestYomkLoggerLifecycle FAILED (" << g_failed << " checks failed)." << std::endl;
return 1;
}
