/**
 * @file TestYomkLoggerLoc.cpp
 * @brief 日志位置捕获机制测试（SourceLoc + emit 签名改造 + 宏注入 {}）
 *
 * 覆盖内容（4 个 Section）：
 * 1. 默认配置（仅行号开）：宏注入行号与真实调用行逐行核对；四级 + _TAG 变体；
 *    宏路径与 emit 直调路径同捕获窗输出结构一致（字节级兼容基线）；零正文/多参折叠
 * 2. 文件日志双路径：log_mgr().dir().file().create() → log().to_sink(name) 发射与 emit 直调同源
 *    → flush() → 落盘读回断言 [N] 前缀、marker 与 tag
 * 3. 开关矩阵：3 开关 × 开/关 8 组合逐组断言精确前缀
 *    （[N] / [file:N] / [file:N:main] / [N:main] / [file] / [main] / 全关无前缀），收尾恢复默认
 * 4. emit 直调隔离：模拟 Concurrency/Stress 直调形态（{} 实参 + 显式 tag），
 *    自定义级别全覆盖；开关不影响直调显式内容；快拼接逐类型护栏
 *    （bool/char/负整型/uint64/浮点/自定义 fallback 类型输出精确匹配）
 *
 * 说明：位置捕获机制 = emit 函数 pack 之前的固定参数 SourceLoc（聚合 + NSDMI），
 *       调用点 {} 在调用行聚合初始化完成捕获；宏在调用行展开注入 {}，
 *       故宏调用与直调的行号语义一致。默认输出与改造前旧宏（"[N]" 字面量拼接）字节级一致。
 *
 * 风格：纯 main() + 失败计数，返回非 0 表示存在失败用例（零第三方依赖），
 *       CoutCapture / marker 断言 / 临时目录模式同 TestYomkLoggerLifecycle.cpp
 */

#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "YomkApi.h"

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

// cout 捕获（析构转储 [FAIL]，模式同 TestYomkLoggerLifecycle.cpp）
class CoutCapture
{
public:
    CoutCapture() : m_old(std::cout.rdbuf(m_ss.rdbuf())) {}
    ~CoutCapture()
    {
        std::cout.rdbuf(m_old);
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

// S4 快拼接 fallback 护栏：可流输出自定义类型（非算术、不可转 const char*/std::string，
// 走 emit 层的单参 ostringstream 回退路径）
struct LgLStreamable
{
    int v;
    friend std::ostream& operator<<(std::ostream& os, const LgLStreamable& s)
    {
        return os << "cust<" << s.v << ">";
    }
};

static fs::path testTmpRoot()
{
    const char* env = std::getenv("YOMK_TEST_TMP_ROOT");
    return (env && *env) ? fs::path(env) : fs::temp_directory_path();
}

static std::string readFile(const fs::path& path, bool& ok)
{
    std::ifstream ifs(path);
    ok = ifs.is_open();
    std::stringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

// 剥离日志行的时间戳与 [MainLogger] 后的 "[行号] " 段，用于宏路径与直调路径的结构等价对比
// （时间戳毫秒级可能跨值，不参与对比）
static std::string stripLineNo(const std::string& line)
{
    const size_t lvl = line.find("[INFO ]");
    if (lvl == std::string::npos)
    {
        return line;
    }
    const std::string head = "[MainLogger] [";
    const size_t h = line.find(head, lvl);
    if (h == std::string::npos)
    {
        return line;
    }
    const size_t close = line.find(']', h + head.size());
    if (close == std::string::npos)
    {
        return line;
    }
    return "[INFO ] [MainLogger] " + line.substr(close + 1);
}

int main()
{
    yomk::server().async_thread(1).init();
    YOMK_TEST_CHECK(yomk::server().instance() != nullptr, "yomk::server().init() 返回非空服务器");

    // ============ Section 1: 默认配置（仅行号开） ============
    {
        // 行号精确捕获：expectLine 推算宏调用所在行
        {
            CoutCapture cap;
            const unsigned expectLine = __LINE__ + 1;
            yomk::log().info("lgL_s1_line_marker");
            const std::string out = cap.str();
            YOMK_TEST_CHECK(out.find("[" + std::to_string(expectLine) + "] lgL_s1_line_marker") !=
                                std::string::npos,
                "宏注入行号与真实调用行一致 (" + std::to_string(expectLine) + ")");
        }

        // 四级 + _TAG 变体：返回值与格式
        {
            CoutCapture cap;
            const unsigned infoLine = __LINE__ + 1;
            auto ri = yomk::log().info("lgL_s1_info");
            auto riT = yomk::log().tag("lgLTag").info("lgL_s1_info_tag");
            auto rw = yomk::log().warn("lgL_s1_warn");
            auto rwT = yomk::log().tag("lgLTag").warn("lgL_s1_warn_tag");
            auto re = yomk::log().error("lgL_s1_error");
            auto reT = yomk::log().tag("lgLTag").error("lgL_s1_error_tag");
            auto rd = yomk::log().debug("lgL_s1_debug");
            auto rdT = yomk::log().tag("lgLTag").debug("lgL_s1_debug_tag");
            YOMK_TEST_CHECK(ri == yomk::payload::ResStatus::Succeeded && riT == yomk::payload::ResStatus::Succeeded &&
                                rw == yomk::payload::ResStatus::Succeeded && rwT == yomk::payload::ResStatus::Succeeded &&
                                re == yomk::payload::ResStatus::Succeeded && reT == yomk::payload::ResStatus::Succeeded &&
                                rd == yomk::payload::ResStatus::Succeeded && rdT == yomk::payload::ResStatus::Succeeded,
                "四级 + _TAG 变体全部返回 ResStatus::Succeeded");
            const std::string out = cap.str();
            YOMK_TEST_CHECK(out.find("[" + std::to_string(infoLine) + "] lgL_s1_info") != std::string::npos,
                "INFO 输出含 [行号] 前缀与 marker");
            YOMK_TEST_CHECK(out.find(" lgL_s1_info_tag") != std::string::npos, "INFO_TAG 自定义 tag 输出 marker");
            YOMK_TEST_CHECK(out.find(" lgL_s1_warn") != std::string::npos, "WARN 输出 marker");
            YOMK_TEST_CHECK(out.find(" lgL_s1_warn_tag") != std::string::npos, "WARN_TAG 输出 marker");
            YOMK_TEST_CHECK(out.find(" lgL_s1_error") != std::string::npos, "ERROR 输出 marker");
            YOMK_TEST_CHECK(out.find(" lgL_s1_error_tag") != std::string::npos, "ERROR_TAG 输出 marker");
            YOMK_TEST_CHECK(out.find(" lgL_s1_debug") != std::string::npos, "DEBUG 输出 marker");
            YOMK_TEST_CHECK(out.find(" lgL_s1_debug_tag") != std::string::npos, "DEBUG_TAG 输出 marker");
        }

        // 宏路径 vs 直调路径：同 marker 双发，剥离行号后两行应逐字节一致
        {
            CoutCapture cap;
            const unsigned macroLine = __LINE__ + 1;
            yomk::log().info("lgL_s1_cmp");
            const unsigned directLine = __LINE__ + 1;
            yomk::log().tag("MainLogger").info("lgL_s1_cmp");
            const std::string out = cap.str();
            YOMK_TEST_CHECK(out.find("[" + std::to_string(macroLine) + "] lgL_s1_cmp") != std::string::npos,
                "宏路径行号正确");
            YOMK_TEST_CHECK(out.find("[" + std::to_string(directLine) + "] lgL_s1_cmp") != std::string::npos,
                "直调路径 {} 行号正确");
            std::string lineMacro;
            std::string lineDirect;
            std::istringstream iss(out);
            std::string ln;
            while (std::getline(iss, ln))
            {
                if (ln.find("lgL_s1_cmp") != std::string::npos)
                {
                    if (lineMacro.empty())
                    {
                        lineMacro = ln; // 第一条 = 宏路径
                    }
                    else
                    {
                        lineDirect = ln; // 第二条 = 直调路径
                    }
                }
            }
            YOMK_TEST_CHECK(!lineDirect.empty() && stripLineNo(lineMacro) == stripLineNo(lineDirect),
                "宏路径与直调路径剥离行号后输出逐字节一致 [macro=" + stripLineNo(lineMacro) +
                    "] [direct=" + stripLineNo(lineDirect) + "]");
        }

        // 零正文与多参折叠
        {
            CoutCapture cap;
            const unsigned emptyLine = __LINE__ + 1;
            auto r0 = yomk::log().info("");
            auto rM = yomk::log().info("i=", 42, "j=", 3.5, "end");
            YOMK_TEST_CHECK(r0 == yomk::payload::ResStatus::Succeeded, "零正文返回 Succeeded");
            YOMK_TEST_CHECK(rM == yomk::payload::ResStatus::Succeeded, "多参折叠返回 Succeeded");
            const std::string out = cap.str();
            YOMK_TEST_CHECK(out.find("[" + std::to_string(emptyLine) + "] ") != std::string::npos,
                "零正文输出仅 [行号] 前缀");
            YOMK_TEST_CHECK(out.find("i= 42 j= 3.5 end") != std::string::npos, "多参按序折叠输出");
        }
    }

    // ============ Section 2: 文件日志双路径（宏 + 直调） ============
    {
        const std::string logName = "lgL_s2"; // file().create() 落盘为 <dir>/<name>.log（init 补扩展名）
        fs::path logFile =
            testTmpRoot() / ("yomk_logger_lgL_" + std::to_string(::getpid())) / (logName + ".log");
        fs::path logDir = logFile.parent_path();
        std::error_code ec;
        fs::create_directories(logDir, ec);
        YOMK_TEST_CHECK(!ec, "S2 临时目录创建成功");

        YOMK_TEST_CHECK(yomk::log_mgr().dir(logDir.string()).file(logName).create() == yomk::payload::ResStatus::Succeeded,
            "file().create() 成功");

        const unsigned infoLine = __LINE__ + 1;
        YOMK_TEST_CHECK(yomk::log().to_sink(logName).info("lgL_s2_info") == yomk::payload::ResStatus::Succeeded,
            "log().to_sink(...).info 返回 Succeeded");
        YOMK_TEST_CHECK(yomk::log().to_sink(logName).tag("lgLTag").info("lgL_s2_info_tag") == yomk::payload::ResStatus::Succeeded,
            "log().to_sink(...).tag(...).info 返回 Succeeded");
        YOMK_TEST_CHECK(yomk::log().to_sink(logName).warn("lgL_s2_warn") == yomk::payload::ResStatus::Succeeded, "log().to_sink(...).warn");
        YOMK_TEST_CHECK(yomk::log().to_sink(logName).error("lgL_s2_error") == yomk::payload::ResStatus::Succeeded,
            "log().to_sink(...).error");
        YOMK_TEST_CHECK(yomk::log().to_sink(logName).debug("lgL_s2_debug") == yomk::payload::ResStatus::Succeeded,
            "log().to_sink(...).debug");
        YOMK_TEST_CHECK(
            yomk::log().to_sink(logName).tag("MainLogger").info("lgL_s2_direct") == yomk::payload::ResStatus::Succeeded,
            "to_sink({}) 发射返回 Succeeded");
        YOMK_TEST_CHECK(
            yomk::log().to_sink(logName).tag("lgLTag").info("lgL_s2_direct_tag") == yomk::payload::ResStatus::Succeeded,
            "to_sink + 自定义 tag 发射返回 Succeeded");

        YOMK_TEST_CHECK(yomk::log_mgr().file(logName).flush() == yomk::payload::ResStatus::Succeeded, "file().flush() 成功");

        bool readOk = false;
        const std::string content = readFile(logFile, readOk);
        YOMK_TEST_CHECK(readOk, "落盘文件可读");
        YOMK_TEST_CHECK(content.find("[" + std::to_string(infoLine) + "] lgL_s2_info") != std::string::npos,
            "文件宏路径行号正确且含 marker (" + std::to_string(infoLine) + ")");
        YOMK_TEST_CHECK(content.find(" lgL_s2_info_tag") != std::string::npos, "文件 INFO_TAG marker 落盘");
        YOMK_TEST_CHECK(content.find(" lgL_s2_warn") != std::string::npos, "文件 WARN marker 落盘");
        YOMK_TEST_CHECK(content.find(" lgL_s2_error") != std::string::npos, "文件 ERROR marker 落盘");
        YOMK_TEST_CHECK(content.find(" lgL_s2_debug") != std::string::npos, "文件 DEBUG marker 落盘");
        YOMK_TEST_CHECK(content.find(" lgL_s2_direct") != std::string::npos, "直调 marker 落盘");
        YOMK_TEST_CHECK(content.find(" lgL_s2_direct_tag") != std::string::npos, "直调自定义 tag marker 落盘");

        yomk::log_mgr().file(logName).remove();
        fs::remove_all(logDir, ec);
    }

    // ============ Section 3: 开关矩阵（3 开关 × 开/关 = 8 组合） ============
    {
        // (file, line, func) → 期望前缀（__FUNCTION__ 在 main 内恒为 main）
        struct Combo
        {
            bool file;
            bool line;
            bool func;
            const char* name;
        };
        const Combo combos[] = {
            {false, true, false, "默认（仅行号）"},
            {true, true, false, "file+line"},
            {true, true, true, "file+line+func"},
            {false, true, true, "line+func"},
            {true, false, false, "仅 file"},
            {false, false, true, "仅 func"},
            {true, false, true, "file+func"},
            {false, false, false, "全关"},
        };
        for (const auto& c : combos)
        {
            yomk::log_mgr().location().show_file(c.file).apply();
            yomk::log_mgr().location().show_line(c.line).apply();
            yomk::log_mgr().location().show_func(c.func).apply();

            CoutCapture cap;
            const std::string marker = std::string("lgL_s3_") + (c.file ? "f" : "-") +
                                       (c.line ? "l" : "-") + (c.func ? "n" : "-");
            const unsigned expectLine = __LINE__ + 1;
            yomk::log().info(marker);
            const std::string out = cap.str();

            bool ok = false;
            std::string expect;
            if (c.file && c.line && c.func)
            {
                expect = "[TestYomkLoggerLoc.cpp:" + std::to_string(expectLine) + ":main] lgL_s3_fln";
                ok = out.find(expect) != std::string::npos;
            }
            else if (c.file && c.line)
            {
                expect = "[TestYomkLoggerLoc.cpp:" + std::to_string(expectLine) + "] lgL_s3_fl-";
                ok = out.find(expect) != std::string::npos;
            }
            else if (c.line && c.func)
            {
                expect = "[" + std::to_string(expectLine) + ":main] lgL_s3_-ln";
                ok = out.find(expect) != std::string::npos;
            }
            else if (c.file && c.func)
            {
                expect = "[TestYomkLoggerLoc.cpp:main] lgL_s3_f-n"; // file:func（无行号段）
                ok = out.find(expect) != std::string::npos;
            }
            else if (c.file)
            {
                expect = "[TestYomkLoggerLoc.cpp] lgL_s3_f--";
                ok = out.find(expect) != std::string::npos;
            }
            else if (c.line)
            {
                expect = "[" + std::to_string(expectLine) + "] lgL_s3_-l-";
                ok = out.find(expect) != std::string::npos;
            }
            else if (c.func)
            {
                expect = "[main] lgL_s3_--n";
                ok = out.find(expect) != std::string::npos;
            }
            else
            {
                // 全关：无位置前缀，content 以空格开头（appendTo 不追加 + 分隔空格）
                expect = "[MainLogger]  lgL_s3_---";
                ok = out.find(expect) != std::string::npos;
            }
            YOMK_TEST_CHECK(ok, "开关组合 (" + std::string(c.name) + ") 前缀精确匹配: " + expect);
        }

        // 收尾恢复默认
        yomk::log_mgr().location().show_file(false).apply();
        yomk::log_mgr().location().show_line(true).apply();
        yomk::log_mgr().location().show_func(false).apply();
        YOMK_TEST_CHECK(true, "开关矩阵收尾恢复默认 (file=关/line=开/func=关)");
    }

    // ============ Section 4: emit 直调隔离（Concurrency/Stress 同形态） ============
    {
        CoutCapture cap;
        auto rd = yomk::log().tag("lgLTag").debug("lgL_s4_debug");
        auto ri = yomk::log().tag("lgLTag").info("lgL_s4_info");
        auto rw = yomk::log().tag("lgLTag").warn("lgL_s4_warn");
        auto re = yomk::log().tag("lgLTag").error("lgL_s4_error");
        YOMK_TEST_CHECK(rd == yomk::payload::ResStatus::Succeeded && ri == yomk::payload::ResStatus::Succeeded &&
                            rw == yomk::payload::ResStatus::Succeeded && re == yomk::payload::ResStatus::Succeeded,
            "直调四级（显式 tag + {}）全部返回 Succeeded");
        const std::string out = cap.str();
        YOMK_TEST_CHECK(out.find(" lgL_s4_debug") != std::string::npos, "直调 DEBUG marker 输出");
        YOMK_TEST_CHECK(out.find(" lgL_s4_info") != std::string::npos, "直调 INFO marker 输出");
        YOMK_TEST_CHECK(out.find(" lgL_s4_warn") != std::string::npos, "直调 WARN marker 输出");
        YOMK_TEST_CHECK(out.find(" lgL_s4_error") != std::string::npos, "直调 ERROR marker 输出");

        // 快拼接逐类型护栏：bool（true/false，用户定版语义）、char（字符本体）、
        // 负整型（含 INT_MIN）、无符号 64 位上界、浮点（%g/%Lg）、自定义类型（fallback）
        {
            CoutCapture cap;
            const unsigned typesLine = __LINE__ + 1;
            yomk::log().tag("lgLTag").info("lgL_s4_types", true, false, 'a', -42, -2147483648, 18446744073709551615ULL, 3.5, 1.5L, LgLStreamable{7});
            const std::string out = cap.str();
            YOMK_TEST_CHECK(out.find("[" + std::to_string(typesLine) + "] lgL_s4_types true false"
                                     " a -42 -2147483648 18446744073709551615 3.5 1.5 cust<7>")
                                    != std::string::npos,
                "逐类型快拼接输出精确匹配 (bool/char/int/uint64/double/long double/fallback)");
        }
    }

    // ============ 收尾清理 ============
    yomk::server().shutdown();

    if (g_failed == 0)
    {
        std::cout << "TestYomkLoggerLoc all check passed." << std::endl;
        return 0;
    }
    std::cout << "TestYomkLoggerLoc FAILED (" << g_failed << " checks failed)." << std::endl;
    return 1;
}
