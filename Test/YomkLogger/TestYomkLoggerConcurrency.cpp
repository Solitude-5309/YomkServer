/**
 * @file TestYomkLoggerConcurrency.cpp
 * @brief Logger 模块多线程并发正确性验证（LG3，5 Section / 72 断言）
 *
 * 覆盖测试要求第 5/10 类（TSan 数据竞争 + 多线程并发正确性），5 个 Section：
 * - S1：并发 console_log 守恒 + 行完整性（只读命中路径，验证 ConsoleLogger::m_mutex 整行互斥）
 * - S2：并发新 tag 自动创建（P1-a 双检锁核心验证）——S2a 最大争用同一新 tag、S2b 分散唯一 tag
 * - S3：并发 proxy set × console_log × 内省（P1-b 专属锁核心验证，镜像 FPC3-S2 写读交错）
 * - S4：并发 file logger 创建 × 写入 × 落盘（S4a 同名争用恰一胜、S4c 2000 行不丢不重守恒）
 * - S5：全量 churn + P3-a 内省原子快照判别（成对创建 + C>=F 判别式量化实证修复有效性）
 *
 * P3-a（本闭环登记、已修复并双向量化实证）：loggers/listAll 原分段持两把锁（console 段
 *   + file 段），跨段非原子快照——file 段时刻 T2 晚于 console 段时刻 T1，并发创建时返回
 *   的行数组合可能从未真实存在过。已修为嵌套双锁（锁序 console -> file，锁序约定见
 *   YomkLogger.h 成员声明处注释），行数成为原子快照。
 *   判别式：churn 线程每轮"先 console 后 file"成对创建同名 logger，则任意真实时刻 T
 *   必有 C(T) >= F(T)；非原子分段快照会出现 F > C 违例。S5 断言违例数 == 0。
 *   实测（1800 对成对创建 / 331 快照）：修复前 LOGGERS 违例 8 个、ALL 违例 7 个；
 *   修复后连跑 5 次违例均为 0。
 *   灵敏度前提（关键）：违例要求"快照 console 段遍历耗时 W"完整覆盖"churn 一轮
 *   console→file 间隔 G"。实测 120 对时 W << G → 0 违例（假阴性，不可据此判定无缺陷），
 *   900 对时 2 违例，1800 对时 15 违例 → S5 规模不得下调至百级。
 *
 * P1-a/P1-b/P1-c（LG2 已修）的并发侧实证结论：S2a 八线程争用同一新 tag（1600 次）与
 *   S5（1800 个新 console logger 并发创建）下无重复实例、无丢日志；S3 写读交错
 *   v1Hits + v2Hits == log 总数（无 torn std::function）；TSan 构建下本二进制与
 *   Logger 其余两个二进制 + TestYomkConcurrency 均 0 race。
 *
 * P3-b（LG3 观察项 → LG6 已处置）：consoleLog 的 !result.second 防御分支（原 YomkLogger.cpp
 *   L82-83）——双检锁下进入独占锁后已二次查找确认 key 不存在，emplace 必成功，S2a/S2b
 *   的最大争用是其唯一可能触达场景。实测结论：在 S2a（1600 次争用同一新 tag）+
 *   S2b（8 个新 tag）+ S5（1800 个新 console logger 并发创建）全覆盖后，gcov 仍显示
 *   L82-83 为 #####（未执行）→ 确证为不可达的防御性死代码。LG6 已删除该 4 行（现为
 *   itLogger = m_consoleLoggers.emplace(...).first，仍复用 emplace 返回的迭代器，控制流拓扑
 *   与修复前逐字对应），双检锁本身与本文件的并发用例语义均不变。
 *
 * 设计边界（契约内使用，非缺陷）：
 * - 本测试全程不调用 /delete_logger（LG4 新增的删除端点由 Lifecycle S8 与 Stress S5
 *   覆盖）→ 不存在 FPC3-S3 那类"注销 × 在途调用"竞态，等价风险面由 S4a"同名创建争用"
 *   + S4c"并发写入 × 落盘交接"覆盖；
 * - proxy 安装后本测试全程不卸载（LG6/P4-e 起可传 nullptr 卸载，卸载语义由 Lifecycle S3
 *   覆盖）→ S3 置于 S1/S2 之后，且 v1/v2 均恒返回 true（穿透态），
 *   不影响 S4/S5 的输出与落盘断言；
 * - ALL 首行的 4 个级别开关是独立 atomic，其组合快照无原子保证（既有设计）→ S5 仅断言
 *   其格式合法与收尾精确串，不断言开关组合的瞬时原子性；
 * - 不同 console logger 并发写 std::cout 会交错撕裂（cout 无跨 logger 全局锁，设计现状）
 *   → 行完整性断言仅在"同一 logger"段（S1/S2a）做，跨 logger 段只断言 marker 计数守恒。
 *
 * 装置说明：
 * - ThreadSafeCoutCapture（本文件专用）：LG1 的 CoutCapture 把 std::cout 的 rdbuf 换成
 *   std::stringstream，而 stringbuf 无内部同步，多线程并发写会被 TSan 判为 data race
 *   （装置自身污染被测代码的 race 验收）。此处改用自带 mutex 的 streambuf 累积文本；
 *   rdbuf 替换由主线程单点完成（线程启动前构造、join 后析构）。
 *   断言可靠性依据：单次 std::cout << str 对应一次 xsputn（装置内加锁）→ marker 作为
 *   整体写入不被切断，故 marker 计数守恒在所有并发段可用；整行格式完整性依赖
 *   ConsoleLogger::m_mutex，仅同一 logger 段可用。
 * - 全部 CHECK 在捕获作用域外（LG1 教训：捕获域内断言输出被 rdbuf 重定向吞没）。
 * - 其余沿用 FPC3 模式：文件级 atomic 计数/停止门/起始屏障（TSan-clean），
 *   std::mutex + std::vector 收集内省快照，临时目录收尾 remove_all。
 *
 * 规模与降档后备：S1/S2a 8×200、S4c 8×250、S5 churn 3×600（S5 规模是 P3-a 判别式的
 *   灵敏度前提，不可随意下调；其余对齐 FPC3 先例）。若 TSan 构建单次超 300s，仅下调
 *   I/O 密集段：S1/S2a 6×120、S4c 6×150，S5 churn 保持 3×600 不低于 3×300，
 *   并在本注释记录实际值。
 *
 * 风格：纯 main() + 失败计数，返回非 0 表示存在失败用例（零第三方依赖）
 */

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

#include "YomkAPI.h"

namespace fs = std::filesystem;

static int g_failed = 0;
static int g_total = 0;

#define CHECK(cond, msg)                                                          \
    do                                                                            \
    {                                                                             \
        ++g_total;                                                                \
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

// ============================================================================
// 线程安全 cout 捕获（并发段专用，替代 LG1 的 stringstream 版 CoutCapture）
// ============================================================================
class ThreadSafeCoutCapture
{
    class Buf : public std::streambuf
    {
    public:
        std::string take()
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_text;
        }

    protected:
        int overflow(int ch) override
        {
            if (ch != traits_type::eof())
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_text.push_back(static_cast<char>(ch));
            }
            return ch;
        }
        std::streamsize xsputn(const char *s, std::streamsize n) override
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_text.append(s, static_cast<size_t>(n));
            return n;
        }

    private:
        std::mutex m_mutex;
        std::string m_text;
    };

public:
    ThreadSafeCoutCapture() : m_old(std::cout.rdbuf(&m_buf)) {}
    ~ThreadSafeCoutCapture() { std::cout.rdbuf(m_old); }
    ThreadSafeCoutCapture(const ThreadSafeCoutCapture &) = delete;
    ThreadSafeCoutCapture &operator=(const ThreadSafeCoutCapture &) = delete;
    std::string str() { return m_buf.take(); }

private:
    Buf m_buf;
    std::streambuf *m_old;
};

// ============================================================================
// 文件级观测装置（TSan-clean：跨线程共享状态一律为原子或互斥保护）
// ============================================================================

// 起始屏障（放大争用窗口）
static std::atomic<int> g_readyCount{0};
static std::atomic<bool> g_gateGo{false};

// S1 守恒
static std::atomic<uint64_t> g_s1OkCount{0};

// S2 守恒
static std::atomic<uint64_t> g_s2aOkCount{0};
static std::atomic<uint64_t> g_s2bOkCount{0};

// S3 守恒（proxy 写读交错）
static std::atomic<uint64_t> g_s3V1Hits{0};
static std::atomic<uint64_t> g_s3V2Hits{0};
static std::atomic<uint64_t> g_s3LogTotal{0};
static std::atomic<uint64_t> g_s3LogOk{0};
static std::atomic<uint64_t> g_s3SetOk{0};
static std::atomic<bool> g_s3Stop{false};

// S4 守恒
static std::atomic<uint64_t> g_s4aCreateOk{0};
static std::atomic<uint64_t> g_s4aCreateNo{0};
static std::atomic<uint64_t> g_s4bCreateOk{0};
static std::atomic<uint64_t> g_s4cLogOk{0};
static std::atomic<uint64_t> g_s4cWriteOk{0};
static std::atomic<uint64_t> g_s4dWriteOk{0};
static std::atomic<bool> g_s4cStop{false};

// S5 守恒
static std::atomic<uint64_t> g_s5PairConsoleOk{0};
static std::atomic<uint64_t> g_s5PairFileOk{0};
static std::atomic<uint64_t> g_s5PairFileNo{0};
static std::atomic<uint64_t> g_s5ConsoleOk{0};
static std::atomic<uint64_t> g_s5FileOk{0};
static std::atomic<uint64_t> g_s5WriteOk{0};
static std::atomic<uint64_t> g_s5SwitchOk{0};
static std::atomic<uint64_t> g_s5IntrospectOk{0};
static std::atomic<uint64_t> g_s5BadStatus{0};           // 状态码不属于 {eOk,eNo,eInvalid} 的次数
static std::atomic<uint64_t> g_s5AtomicityViolations{0}; // P3-a 判别式违例（F > C）计数
static std::atomic<uint64_t> g_s5BadLevelLine{0};        // ALL 首行格式非法计数
static std::atomic<bool> g_s5Stop{false};                // 内省线程停止门（churn 完成后置位）

// 内省快照收集（S5）
struct IntrospectSnapshot
{
    uint64_t consoleLines; // LOGGERS 中 " [console]" 结尾行数
    uint64_t fileLines;    // LOGGERS 中含 " [file] dir:" 行数
    uint64_t allLines;     // ALL 返回总行数（首行 + console 段 + file 段）
    uint64_t allConsole;   // ALL 中 console 段行数
    uint64_t allFile;      // ALL 中 file 段行数
};
static std::mutex g_s5SnapMutex;
static std::vector<std::vector<IntrospectSnapshot>> g_s5SnapsByThread;

// ============================================================================
// 辅助函数
// ============================================================================

// 统计非空行数
static size_t countLines(const std::string &text)
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

// 统计文本中"行尾最后一段"（末个空格之后）等于 prefix 开头的 marker 出现次数
// 用途：console 捕获文本 / file 落盘文本的 marker 守恒（不丢不重）判定
static std::map<std::string, uint64_t> collectTailMarkers(const std::string &text,
                                                          const std::string &prefix)
{
    std::map<std::string, uint64_t> markers;
    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line))
    {
        if (line.empty())
        {
            continue;
        }
        auto pos = line.rfind(' ');
        std::string tail = (pos == std::string::npos) ? line : line.substr(pos + 1);
        if (tail.compare(0, prefix.size(), prefix) == 0)
        {
            ++markers[tail];
        }
    }
    return markers;
}

// 统计 markers 中出现次数 != 1 的条目数（0 即"不丢不重"）
static uint64_t countBadOccurrence(const std::map<std::string, uint64_t> &markers)
{
    uint64_t bad = 0;
    for (const auto &item : markers)
    {
        if (item.second != 1)
        {
            ++bad;
        }
    }
    return bad;
}

// 时间戳格式粗校验："dddd-dd-dd dd:dd:dd.ddd]"（不锁定具体日期值）
static bool checkTimeFormat(const std::string &line)
{
    auto isDigit = [](char c)
    { return c >= '0' && c <= '9'; };
    if (line.size() < 25 || line[0] != '[')
    {
        return false;
    }
    for (int i = 1; i <= 24; ++i)
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

// 统计含指定子串的行数（用于 LOGGERS 中某 tag 的实例行数判定）
static uint64_t countLinesContaining(const std::vector<std::string> &lines, const std::string &needle)
{
    uint64_t count = 0;
    for (const auto &line : lines)
    {
        if (line.find(needle) != std::string::npos)
        {
            ++count;
        }
    }
    return count;
}

// 解包 LOGGERS/ALL 的 StringArray；失败返回空 vector
static std::vector<std::string> unpackLines(const YomkResponse &resp)
{
    std::vector<std::string> lines;
    if (resp.m_status != YomkResponse::eOk || !resp.m_data)
    {
        return lines;
    }
    YomkUnPackPkg(resp.m_data, StringArray, arr);
    if (arr)
    {
        lines = arr->d;
    }
    return lines;
}

// 统计 LOGGERS 行中的 console/file 段行数
static void countLoggerLines(const std::vector<std::string> &lines, uint64_t &consoleLines,
                             uint64_t &fileLines)
{
    consoleLines = 0;
    fileLines = 0;
    for (const auto &line : lines)
    {
        static const std::string kConsoleSuffix = " [console]";
        if (line.size() >= kConsoleSuffix.size() &&
            line.compare(line.size() - kConsoleSuffix.size(), kConsoleSuffix.size(), kConsoleSuffix) == 0)
        {
            ++consoleLines;
            continue;
        }
        if (line.find(" [file] dir:") != std::string::npos)
        {
            ++fileLines;
        }
    }
}

// 校验 ALL 首行格式：console:debug:(on|off) info:(on|off) warn:(on|off) error:(on|off) proxy:(on|off)
static bool checkLevelLineFormat(const std::string &line)
{
    auto matchField = [&line](size_t &pos, const std::string &key) -> bool
    {
        if (line.compare(pos, key.size(), key) != 0)
        {
            return false;
        }
        pos += key.size();
        if (line.compare(pos, 2, "on") == 0)
        {
            pos += 2;
            return true;
        }
        if (line.compare(pos, 3, "off") == 0)
        {
            pos += 3;
            return true;
        }
        return false;
    };

    size_t pos = 0;
    if (!matchField(pos, "console:debug:"))
        return false;
    if (pos >= line.size() || line[pos] != ' ')
        return false;
    ++pos;
    if (!matchField(pos, "info:"))
        return false;
    if (pos >= line.size() || line[pos] != ' ')
        return false;
    ++pos;
    if (!matchField(pos, "warn:"))
        return false;
    if (pos >= line.size() || line[pos] != ' ')
        return false;
    ++pos;
    if (!matchField(pos, "error:"))
        return false;
    if (pos >= line.size() || line[pos] != ' ')
        return false;
    ++pos;
    if (!matchField(pos, "proxy:"))
        return false;
    return pos == line.size();
}

// 状态码是否属于框架合法集合 {eInvalid, eOk, eNo}
static bool isLegalStatus(const YomkResponse &resp)
{
    return resp.m_status == YomkResponse::eInvalid ||
           resp.m_status == YomkResponse::eOk ||
           resp.m_status == YomkResponse::eNo;
}

// 起始屏障：线程内调用，等待主线程放行（放大并发争用窗口）
static void waitGate(int threadCount)
{
    g_readyCount.fetch_add(1, std::memory_order_release);
    while (!g_gateGo.load(std::memory_order_acquire))
    {
        std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
    (void)threadCount;
}

// 主线程侧：等待 threadCount 个线程就绪后放行
static void releaseGate(int threadCount)
{
    int waitMs = 0;
    while (g_readyCount.load(std::memory_order_acquire) < threadCount && waitMs < 10000)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        ++waitMs;
    }
    g_gateGo.store(true, std::memory_order_release);
}

// 重置屏障状态
static void resetGate()
{
    g_readyCount.store(0);
    g_gateGo.store(false);
}

// ============================================================================
// S3 用 proxy 函数（v1/v2 交替，均恒返回 true 穿透态）
// ============================================================================
static bool s3ProxyV1(const yomk::Log & /*log*/)
{
    g_s3V1Hits.fetch_add(1, std::memory_order_relaxed);
    return true; // 穿透：继续走默认控制台输出，不影响后续 Section
}

static bool s3ProxyV2(const yomk::Log & /*log*/)
{
    g_s3V2Hits.fetch_add(1, std::memory_order_relaxed);
    return true;
}

// ============================================================================
// main
// ============================================================================
int main()
{
    auto server = YOMK_INIT(1);
    CHECK(server != nullptr, "YOMK_INIT 返回非空服务器");

    fs::path tmpDir = fs::temp_directory_path() / ("yomk_logger_lg3_" + std::to_string(::getpid()));
    std::error_code ec;
    fs::create_directories(tmpDir, ec);
    CHECK(!ec, "临时目录创建成功: " + tmpDir.string());

    // ========================================================================
    // Section 1：并发 console_log 守恒 + 行完整性（只读命中路径）
    // ========================================================================
    std::cout << "\n--- S1: 并发 console_log 守恒 + 行完整性 ---" << std::endl;
    {
        constexpr int kThreads = 8;
        constexpr int kLogsPerThread = 200;
        constexpr uint64_t kTotal = kThreads * kLogsPerThread;

        // 直接调 YomkAPI 静态函数而非 YOMK_INFO_TAG 宏：宏会把 tag 拼上调用点行号，
        // 预热与循环体分处两行将产生两个不同 logger 名，破坏"同一 logger 并发"前提
        const std::string kTag = "lg3_s1_shared";

        g_s1OkCount.store(0);

        // 预热：主线程先创建该 tag 的 console logger，使并发段全部走 shared_lock 命中路径
        CHECK(YomkAPI::CONSOLE_LOG_INFO_TAG(kTag, "lg3s1_warmup").m_status == YomkResponse::eOk,
              "S1: 预热创建 console logger " + kTag);

        std::string out;
        {
            ThreadSafeCoutCapture cap;
            resetGate();
            std::vector<std::thread> workers;
            workers.reserve(kThreads);
            for (int t = 0; t < kThreads; ++t)
            {
                workers.emplace_back([t, &kTag]()
                                     {
                    waitGate(kThreads);
                    for (int i = 0; i < kLogsPerThread; ++i)
                    {
                        auto resp = YomkAPI::CONSOLE_LOG_INFO_TAG(
                            kTag, "lg3s1_t" + std::to_string(t) + "_i" + std::to_string(i));
                        if (resp.m_status == YomkResponse::eOk)
                        {
                            g_s1OkCount.fetch_add(1, std::memory_order_relaxed);
                        }
                    } });
            }
            releaseGate(kThreads);
            for (auto &th : workers)
            {
                th.join();
            }
            out = cap.str();
        }

        CHECK(g_s1OkCount.load() == kTotal,
              "S1: 响应守恒 eOk == " + std::to_string(kTotal) + "（实际 " +
                  std::to_string(g_s1OkCount.load()) + "）");

        auto markers = collectTailMarkers(out, "lg3s1_t");
        CHECK(markers.size() == kTotal,
              "S1: 唯一 marker 数 == " + std::to_string(kTotal) + "（实际 " +
                  std::to_string(markers.size()) + "）——并发输出不丢");
        CHECK(countBadOccurrence(markers) == 0,
              "S1: 全部 marker 恰出现 1 次（无重复、无撕裂）——ConsoleLogger::m_mutex 整行互斥");

        // 行完整性：同一 logger 的每一行均满足时间戳 + 级别 + tag 前缀格式（无跨行撕裂）
        uint64_t intactLines = 0;
        uint64_t brokenLines = 0;
        {
            std::istringstream iss(out);
            std::string line;
            while (std::getline(iss, line))
            {
                if (line.find("lg3s1_t") == std::string::npos)
                {
                    continue; // 框架自身噪声行不参与判定
                }
                if (checkTimeFormat(line) && line.find("[Info ] [" + kTag + "]") != std::string::npos)
                {
                    ++intactLines;
                }
                else
                {
                    ++brokenLines;
                }
            }
        }
        CHECK(brokenLines == 0,
              "S1: 含 marker 的行全部完整（时间戳+[Info ]+[tag] 格式，撕裂行 " +
                  std::to_string(brokenLines) + " 条）");
        CHECK(intactLines == kTotal,
              "S1: 完整行数 == " + std::to_string(kTotal) + "（实际 " + std::to_string(intactLines) + "）");

        // 同一 tag 仅一个 logger 实例（并发命中路径不重复创建）
        auto loggersLines = unpackLines(YOMK_LOGGER_INFO_LOGGERS());
        CHECK(countLinesContaining(loggersLines, kTag + " [console]") == 1,
              "S1: LOGGERS 中 " + kTag + " 恰 1 行（无重复实例）");
    }

    // ========================================================================
    // Section 2：并发新 tag 自动创建（P1-a 双检锁核心验证）
    // ========================================================================
    std::cout << "\n--- S2: 并发新 tag 自动创建（P1-a 双检锁）---" << std::endl;
    {
        // ---- S2a：最大争用——8 线程同时对同一全新 tag 打日志 ----
        constexpr int kThreads = 8;
        constexpr int kLogsPerThread = 200;
        constexpr uint64_t kTotal = kThreads * kLogsPerThread;
        const std::string kRaceTag = "lg3_s2_race";

        g_s2aOkCount.store(0);

        std::string out;
        {
            ThreadSafeCoutCapture cap;
            resetGate();
            std::vector<std::thread> workers;
            workers.reserve(kThreads);
            for (int t = 0; t < kThreads; ++t)
            {
                workers.emplace_back([t, &kRaceTag]()
                                     {
                    waitGate(kThreads); // 屏障对齐：8 线程同时 miss → 同时升级独占锁
                    for (int i = 0; i < kLogsPerThread; ++i)
                    {
                        auto resp = YomkAPI::CONSOLE_LOG_INFO_TAG(
                            kRaceTag, "lg3s2a_t" + std::to_string(t) + "_i" + std::to_string(i));
                        if (resp.m_status == YomkResponse::eOk)
                        {
                            g_s2aOkCount.fetch_add(1, std::memory_order_relaxed);
                        }
                    } });
            }
            releaseGate(kThreads);
            for (auto &th : workers)
            {
                th.join();
            }
            out = cap.str();
        }

        CHECK(g_s2aOkCount.load() == kTotal,
              "S2a: 争用创建全部 eOk == " + std::to_string(kTotal) + "（实际 " +
                  std::to_string(g_s2aOkCount.load()) + "）");

        auto loggersLines = unpackLines(YOMK_LOGGER_INFO_LOGGERS());
        CHECK(countLinesContaining(loggersLines, kRaceTag + " [console]") == 1,
              "S2a: LOGGERS 中 " + kRaceTag + " 恰 1 行——双检锁下并发 miss 仅创建一个实例");

        auto raceMarkers = collectTailMarkers(out, "lg3s2a_t");
        CHECK(raceMarkers.size() == kTotal,
              "S2a: 唯一 marker 数 == " + std::to_string(kTotal) + "（实际 " +
                  std::to_string(raceMarkers.size()) + "）——同一实例被并发复用无丢失");
        CHECK(countBadOccurrence(raceMarkers) == 0, "S2a: 全部 marker 恰出现 1 次（不丢不重）");

        auto raceInfo = YOMK_LOGGER_INFO_LOGGER(kRaceTag);
        CHECK((raceInfo.m_status == YomkResponse::eOk && raceInfo.m_msg == kRaceTag + " [console]"),
              "S2a: LOGGER(" + kRaceTag + ") 命中 console 格式");

        // ---- S2b：分散创建——8 线程各自唯一 tag ----
        constexpr int kSpreadThreads = 8;
        constexpr int kSpreadPerThread = 50;
        constexpr uint64_t kSpreadTotal = kSpreadThreads * kSpreadPerThread;

        g_s2bOkCount.store(0);
        uint64_t consoleBefore = 0;
        uint64_t fileBefore = 0;
        countLoggerLines(unpackLines(YOMK_LOGGER_INFO_LOGGERS()), consoleBefore, fileBefore);

        {
            ThreadSafeCoutCapture cap;
            resetGate();
            std::vector<std::thread> workers;
            workers.reserve(kSpreadThreads);
            for (int t = 0; t < kSpreadThreads; ++t)
            {
                workers.emplace_back([t]()
                                     {
                    std::string tag = "lg3_s2_t" + std::to_string(t);
                    waitGate(kSpreadThreads);
                    for (int i = 0; i < kSpreadPerThread; ++i)
                    {
                        auto resp = YomkAPI::CONSOLE_LOG_INFO_TAG(
                            tag, "lg3s2b_t" + std::to_string(t) + "_i" + std::to_string(i));
                        if (resp.m_status == YomkResponse::eOk)
                        {
                            g_s2bOkCount.fetch_add(1, std::memory_order_relaxed);
                        }
                    } });
            }
            releaseGate(kSpreadThreads);
            for (auto &th : workers)
            {
                th.join();
            }
        }

        CHECK(g_s2bOkCount.load() == kSpreadTotal,
              "S2b: 分散创建全部 eOk == " + std::to_string(kSpreadTotal) + "（实际 " +
                  std::to_string(g_s2bOkCount.load()) + "）");

        uint64_t consoleAfter = 0;
        uint64_t fileAfter = 0;
        auto spreadLines = unpackLines(YOMK_LOGGER_INFO_LOGGERS());
        countLoggerLines(spreadLines, consoleAfter, fileAfter);
        CHECK(consoleAfter - consoleBefore == kSpreadThreads,
              "S2b: console logger 新增恰 " + std::to_string(kSpreadThreads) + " 个（实际新增 " +
                  std::to_string(consoleAfter - consoleBefore) + "）");
        CHECK(fileAfter == fileBefore, "S2b: file logger 数量未受影响");

        bool allSpreadHit = true;
        for (int t = 0; t < kSpreadThreads; ++t)
        {
            std::string tag = "lg3_s2_t" + std::to_string(t);
            auto info = YOMK_LOGGER_INFO_LOGGER(tag);
            if (info.m_status != YomkResponse::eOk || info.m_msg != tag + " [console]")
            {
                allSpreadHit = false;
            }
        }
        CHECK(allSpreadHit, "S2b: 8 个唯一 tag 的 LOGGER 内省全部命中 console 格式");

        // ---- S2c：P3-b 判定登记（LG6 已收口）----
        // S2a（同一新 tag 8 线程争用）与 S2b（8 个新 tag 并发创建）是 consoleLog 中
        // !result.second 防御分支的唯一可能触达场景；双检锁在独占锁内已二次查找确认 key 不存在，
        // emplace 必成功 → 该分支不可达，LG6 已删除该 4 行死代码，gcov 复查印证 YomkLogger.cpp 的
        // ##### 洞由 2 归零；本组 S2a/S2b 即该删除的行为学佐证（删前删后均全绿、无 eNo 返回）。
        std::cout << "[INFO] S2c: P3-b(!result.second) 最大争用场景已覆盖，该死分支已于 LG6 删除"
                  << std::endl;
    }

    // ========================================================================
    // Section 3：并发 proxy set × console_log × 内省（P1-b 专属锁核心验证）
    // ========================================================================
    std::cout << "\n--- S3: 并发 proxy set × console_log × 内省 ---" << std::endl;
    {
        constexpr int kCallerThreads = 6;
        constexpr int kSetRounds = 50;
        constexpr int kIntrospectThreads = 2;
        const std::string kTag = "lg3_s3_proxy";

        g_s3V1Hits.store(0);
        g_s3V2Hits.store(0);
        g_s3LogTotal.store(0);
        g_s3LogOk.store(0);
        g_s3SetOk.store(0);
        g_s5BadLevelLine.store(0);
        g_s3Stop.store(false);

        // 首次设置为 v1（此后进程内 proxy 恒为 on，v1/v2 交替更新 func）
        CHECK(YOMK_SET_CONSOLE_LOG_PROXY(s3ProxyV1).m_status == YomkResponse::eOk,
              "S3: SET_CONSOLE_LOG_PROXY(v1) 返回 eOk");

        // caller 线程：持续 console_log 直到 g_s3Stop
        std::vector<std::thread> callers;
        callers.reserve(kCallerThreads);
        for (int t = 0; t < kCallerThreads; ++t)
        {
            callers.emplace_back([t, &kTag]()
                                 {
                int i = 0;
                while (!g_s3Stop.load(std::memory_order_acquire))
                {
                    auto resp = YomkAPI::CONSOLE_LOG_INFO_TAG(
                        kTag, "lg3s3_t" + std::to_string(t) + "_i" + std::to_string(i));
                    g_s3LogTotal.fetch_add(1, std::memory_order_relaxed);
                    if (resp.m_status == YomkResponse::eOk)
                    {
                        g_s3LogOk.fetch_add(1, std::memory_order_relaxed);
                    }
                    ++i;
                } });
        }

        // 内省线程：并发轮询 ALL，校验首行格式（proxy:on 恒定 + 无撕裂）
        std::vector<std::thread> introspectors;
        introspectors.reserve(kIntrospectThreads);
        for (int t = 0; t < kIntrospectThreads; ++t)
        {
            introspectors.emplace_back([]()
                                       {
                while (!g_s3Stop.load(std::memory_order_acquire))
                {
                    auto lines = unpackLines(YOMK_LOGGER_INFO_ALL());
                    if (!lines.empty())
                    {
                        if (!checkLevelLineFormat(lines.front()) ||
                            lines.front().find("proxy:on") == std::string::npos)
                        {
                            g_s5BadLevelLine.fetch_add(1, std::memory_order_relaxed);
                        }
                    }
                    std::this_thread::sleep_for(std::chrono::microseconds(200));
                } });
        }

        // 主线程：50 轮交替 SET v1/v2（写侧与 caller 的读侧并发交错）
        for (int round = 0; round < kSetRounds; ++round)
        {
            auto resp = (round % 2 == 0) ? YOMK_SET_CONSOLE_LOG_PROXY(s3ProxyV2)
                                         : YOMK_SET_CONSOLE_LOG_PROXY(s3ProxyV1);
            if (resp.m_status == YomkResponse::eOk)
            {
                g_s3SetOk.fetch_add(1, std::memory_order_relaxed);
            }
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }

        g_s3Stop.store(true, std::memory_order_release);
        for (auto &th : callers)
        {
            th.join();
        }
        for (auto &th : introspectors)
        {
            th.join();
        }

        uint64_t v1 = g_s3V1Hits.load();
        uint64_t v2 = g_s3V2Hits.load();
        uint64_t total = g_s3LogTotal.load();

        CHECK(g_s3SetOk.load() == kSetRounds,
              "S3: SET 交替 " + std::to_string(kSetRounds) + " 轮全部 eOk");
        CHECK(v1 + v2 == total,
              "S3: 写读交错守恒 v1(" + std::to_string(v1) + ") + v2(" + std::to_string(v2) +
                  ") == log 总数(" + std::to_string(total) + ")——无 torn std::function（锁内拷贝快照生效）");
        CHECK(v1 > 0, "S3: v1 实现被观测到");
        CHECK(v2 > 0, "S3: v2 实现被观测到（更新确实生效）");
        CHECK(total > 0, "S3: caller 线程确实执行了日志调用");
        CHECK(g_s3LogOk.load() == total,
              "S3: 全部 console_log 返回 eOk（穿透态不影响响应码）");
        CHECK(g_s5BadLevelLine.load() == 0,
              "S3: 并发内省 ALL 首行格式全部合法且含 proxy:on（非法 " +
                  std::to_string(g_s5BadLevelLine.load()) + " 次）");
    }

    // 读文件全部内容（S4/S5 落盘守恒断言共用；main 作用域 lambda）
    auto readFile = [](const fs::path &path) -> std::string
    {
        std::ifstream ifs(path);
        std::ostringstream ss;
        ss << ifs.rdbuf();
        return ss.str();
    };

    // ========================================================================
    // Section 4：并发 file logger 创建 × 写入 × 落盘（FileLogger 守恒）
    // ========================================================================
    std::cout << "\n--- S4: 并发 file logger 创建 × 写入 × 落盘 ---" << std::endl;
    {
        // ---- S4a：同名争用——恰一胜，其余重名 eNo ----
        constexpr int kThreads = 8;
        fs::path sameDir = tmpDir / "s4a";
        const std::string kSameName = "lg3_s4_same";

        g_s4aCreateOk.store(0);
        g_s4aCreateNo.store(0);

        uint64_t fileBeforeA = 0;
        uint64_t consoleBeforeA = 0;
        countLoggerLines(unpackLines(YOMK_LOGGER_INFO_LOGGERS()), consoleBeforeA, fileBeforeA);

        resetGate();
        {
            std::vector<std::thread> workers;
            workers.reserve(kThreads);
            for (int t = 0; t < kThreads; ++t)
            {
                workers.emplace_back([&sameDir, &kSameName]()
                                     {
                    waitGate(kThreads); // 屏障对齐：8 线程同时争抢 unique_lock
                    auto resp = YOMK_FILE_LOG_CREATE(sameDir.string(), kSameName);
                    if (!isLegalStatus(resp))
                    {
                        g_s5BadStatus.fetch_add(1, std::memory_order_relaxed);
                    }
                    if (resp.m_status == YomkResponse::eOk)
                    {
                        g_s4aCreateOk.fetch_add(1, std::memory_order_relaxed);
                    }
                    else if (resp.m_status == YomkResponse::eNo &&
                             resp.m_msg == "logger name already exists.")
                    {
                        g_s4aCreateNo.fetch_add(1, std::memory_order_relaxed);
                    } });
            }
            releaseGate(kThreads);
            for (auto &th : workers)
            {
                th.join();
            }
        }

        CHECK(g_s4aCreateOk.load() == 1,
              "S4a: 8 线程并发同名创建恰 1 个 eOk（实际 " + std::to_string(g_s4aCreateOk.load()) + "）");
        CHECK(g_s4aCreateNo.load() == kThreads - 1,
              "S4a: 其余 " + std::to_string(kThreads - 1) + " 个 eNo \"logger name already exists.\"（实际 " +
                  std::to_string(g_s4aCreateNo.load()) + "）——unique_lock 互斥下重名检查守恒");
        CHECK(fs::exists(sameDir / (kSameName + ".log")), "S4a: 胜出者日志文件已落盘");

        uint64_t fileAfterA = 0;
        uint64_t consoleAfterA = 0;
        countLoggerLines(unpackLines(YOMK_LOGGER_INFO_LOGGERS()), consoleAfterA, fileAfterA);
        CHECK(fileAfterA - fileBeforeA == 1,
              "S4a: file logger 仅新增 1 个（无重复注册，实际新增 " +
                  std::to_string(fileAfterA - fileBeforeA) + "）");
        CHECK(consoleAfterA == consoleBeforeA, "S4a: console logger 数量未受影响");

        // ---- S4b：异名并发——全部 eOk ----
        fs::path spreadDir = tmpDir / "s4b";
        g_s4bCreateOk.store(0);

        resetGate();
        {
            std::vector<std::thread> workers;
            workers.reserve(kThreads);
            for (int t = 0; t < kThreads; ++t)
            {
                workers.emplace_back([t, &spreadDir]()
                                     {
                    waitGate(kThreads);
                    auto resp = YOMK_FILE_LOG_CREATE(spreadDir.string(), "lg3_s4_t" + std::to_string(t));
                    if (!isLegalStatus(resp))
                    {
                        g_s5BadStatus.fetch_add(1, std::memory_order_relaxed);
                    }
                    if (resp.m_status == YomkResponse::eOk)
                    {
                        g_s4bCreateOk.fetch_add(1, std::memory_order_relaxed);
                    } });
            }
            releaseGate(kThreads);
            for (auto &th : workers)
            {
                th.join();
            }
        }

        CHECK(g_s4bCreateOk.load() == kThreads,
              "S4b: 8 线程异名并发创建全部 eOk（实际 " + std::to_string(g_s4bCreateOk.load()) + "）");

        uint64_t fileAfterB = 0;
        uint64_t consoleAfterB = 0;
        countLoggerLines(unpackLines(YOMK_LOGGER_INFO_LOGGERS()), consoleAfterB, fileAfterB);
        CHECK(fileAfterB - fileAfterA == kThreads,
              "S4b: file logger 新增恰 " + std::to_string(kThreads) + " 个（实际新增 " +
                  std::to_string(fileAfterB - fileAfterA) + "）");

        // ---- S4c：并发写入 × 并发落盘守恒（2000 行不丢不重）----
        constexpr int kLogThreads = 8;
        constexpr int kLogsPerThread = 250;
        constexpr uint64_t kSinkTotal = kLogThreads * kLogsPerThread;
        constexpr int kWriterThreads = 2;
        fs::path sinkDir = tmpDir / "s4c";
        const std::string kSinkName = "lg3_s4_sink";

        g_s4cLogOk.store(0);
        g_s4cWriteOk.store(0);
        g_s4cStop.store(false);

        CHECK(YOMK_FILE_LOG_CREATE(sinkDir.string(), kSinkName).m_status == YomkResponse::eOk,
              "S4c: 创建 sink file logger 成功");

        resetGate();
        std::vector<std::thread> logWorkers;
        logWorkers.reserve(kLogThreads);
        for (int t = 0; t < kLogThreads; ++t)
        {
            logWorkers.emplace_back([t, &kSinkName]()
                                    {
                waitGate(kLogThreads);
                for (int i = 0; i < kLogsPerThread; ++i)
                {
                    auto resp = YomkAPI::FILE_LOG_INFO_TAG(
                        kSinkName, "lg3s4c", "lg3s4c_t" + std::to_string(t) + "_i" + std::to_string(i));
                    if (!isLegalStatus(resp))
                    {
                        g_s5BadStatus.fetch_add(1, std::memory_order_relaxed);
                    }
                    if (resp.m_status == YomkResponse::eOk)
                    {
                        g_s4cLogOk.fetch_add(1, std::memory_order_relaxed);
                    }
                } });
        }

        // writer 线程：log 进行中持续 WRITE（缓冲交接竞态窗口）
        std::vector<std::thread> writers;
        writers.reserve(kWriterThreads);
        for (int t = 0; t < kWriterThreads; ++t)
        {
            writers.emplace_back([&kSinkName]()
                                 {
                while (!g_s4cStop.load(std::memory_order_acquire))
                {
                    auto resp = YOMK_FILE_LOG_WRITE(kSinkName);
                    if (!isLegalStatus(resp))
                    {
                        g_s5BadStatus.fetch_add(1, std::memory_order_relaxed);
                    }
                    if (resp.m_status == YomkResponse::eOk)
                    {
                        g_s4cWriteOk.fetch_add(1, std::memory_order_relaxed);
                    }
                    std::this_thread::sleep_for(std::chrono::microseconds(200));
                } });
        }

        releaseGate(kLogThreads);
        for (auto &th : logWorkers)
        {
            th.join();
        }
        g_s4cStop.store(true, std::memory_order_release);
        for (auto &th : writers)
        {
            th.join();
        }

        // 收尾：把残余缓冲全部落盘
        CHECK(YOMK_FILE_LOG_WRITE(kSinkName).m_status == YomkResponse::eOk, "S4c: 收尾 WRITE 返回 eOk");

        CHECK(g_s4cLogOk.load() == kSinkTotal,
              "S4c: 并发写入响应守恒 eOk == " + std::to_string(kSinkTotal) + "（实际 " +
                  std::to_string(g_s4cLogOk.load()) + "）");
        CHECK(g_s4cWriteOk.load() > 0,
              "S4c: writer 线程确实执行了落盘（" + std::to_string(g_s4cWriteOk.load()) + " 次 eOk）");

        std::string sinkContent = readFile(sinkDir / (kSinkName + ".log"));
        uint64_t sinkLines = countLines(sinkContent);
        CHECK(sinkLines == kSinkTotal,
              "S4c: 落盘总行数 == " + std::to_string(kSinkTotal) + "（实际 " + std::to_string(sinkLines) +
                  "）——log/write 共用 m_logBufferMutex（P4-a 后缓冲为 std::string），缓冲交接不丢行不切行");

        auto sinkMarkers = collectTailMarkers(sinkContent, "lg3s4c_t");
        CHECK(sinkMarkers.size() == kSinkTotal,
              "S4c: 落盘唯一 marker 数 == " + std::to_string(kSinkTotal) + "（实际 " +
                  std::to_string(sinkMarkers.size()) + "）");
        CHECK(countBadOccurrence(sinkMarkers) == 0,
              "S4c: 全部 marker 恰落盘 1 次（不丢不重）——并发写落盘守恒核心证明");

        // ---- S4d：多写者并发 WRITE（缓冲清空后幂等，无重复落盘）----
        constexpr int kWriterThreadsD = 4;
        constexpr int kWriteRoundsD = 10;
        constexpr uint64_t kMultiLines = 100;
        fs::path multiDir = tmpDir / "s4d";
        const std::string kMultiName = "lg3_s4_multi";

        g_s4dWriteOk.store(0);

        CHECK(YOMK_FILE_LOG_CREATE(multiDir.string(), kMultiName).m_status == YomkResponse::eOk,
              "S4d: 创建 multi file logger 成功");
        for (uint64_t i = 0; i < kMultiLines; ++i)
        {
            YomkAPI::FILE_LOG_INFO_TAG(kMultiName, "lg3s4d", "lg3s4d_i" + std::to_string(i));
        }

        resetGate();
        {
            std::vector<std::thread> workers;
            workers.reserve(kWriterThreadsD);
            for (int t = 0; t < kWriterThreadsD; ++t)
            {
                workers.emplace_back([&kMultiName]()
                                     {
                    waitGate(kWriterThreadsD);
                    for (int r = 0; r < kWriteRoundsD; ++r)
                    {
                        auto resp = YOMK_FILE_LOG_WRITE(kMultiName);
                        if (!isLegalStatus(resp))
                        {
                            g_s5BadStatus.fetch_add(1, std::memory_order_relaxed);
                        }
                        if (resp.m_status == YomkResponse::eOk)
                        {
                            g_s4dWriteOk.fetch_add(1, std::memory_order_relaxed);
                        }
                    } });
            }
            releaseGate(kWriterThreadsD);
            for (auto &th : workers)
            {
                th.join();
            }
        }

        CHECK(g_s4dWriteOk.load() == kWriterThreadsD * kWriteRoundsD,
              "S4d: " + std::to_string(kWriterThreadsD) + " 线程 × " + std::to_string(kWriteRoundsD) +
                  " 轮并发 WRITE 全部 eOk（实际 " + std::to_string(g_s4dWriteOk.load()) + "）");

        std::string multiContent = readFile(multiDir / (kMultiName + ".log"));
        CHECK(countLines(multiContent) == kMultiLines,
              "S4d: 落盘行数 == " + std::to_string(kMultiLines) + "（实际 " +
                  std::to_string(countLines(multiContent)) + "）——缓冲清空后 WRITE 幂等，无重复落盘");
        auto multiMarkers = collectTailMarkers(multiContent, "lg3s4d_i");
        CHECK(countBadOccurrence(multiMarkers) == 0, "S4d: 全部 marker 恰落盘 1 次（多写者无撕裂）");
    }

    // ========================================================================
    // Section 5：全量 churn 混合 + P3-a 内省原子快照判别
    // ========================================================================
    std::cout << "\n--- S5: 全量 churn + P3-a 内省原子快照判别 ---" << std::endl;
    {
        constexpr int kChurnThreads = 3;
        // 1800 对（而非 40 轮×3）：判别式的灵敏度取决于“快照 console 段遍历耗时 W”与
        // “churn 一轮 console→file 间隔 G”的相对量级——违例需 W 完整覆盖 G。
        // 小规模（百级）下 W 仅十几微秒而 G 含文件 I/O 达百微秒级，W << G 几乎不可能触发；
        // console map 提到 1800+ 项后 W 与 G 同量级，违例成为可稳定观测的事件
        // （实测违例数：120 对 → 0（假阴性），900 对 → 2，1800 对 → LOGGERS 8 + ALL 7）
        constexpr int kChurnRounds = 600;
        constexpr uint64_t kPairTotal = kChurnThreads * kChurnRounds; // 1800 对 logger
        constexpr int kIntrospectThreads = 2;
        constexpr int kSwitchThreads = 2;
        constexpr int kSwitchRounds = 100;
        constexpr int kConsoleThreads = 2;
        constexpr int kConsoleRounds = 200;
        constexpr int kFileThreads = 2;
        constexpr int kFileRounds = 200;

        fs::path pairDir = tmpDir / "s5";
        const std::string kConsoleSink = "lg3_s5_sink";
        const std::string kFileSink = "lg3_s5_sink_file";

        // 四级别调用分发（级别号 0=debug 1=info 2=warn 3=error）
        auto consoleLevel = [](int level, const std::string &tag, const std::string &msg) -> YomkResponse
        {
            switch (level)
            {
            case 0:
                return YomkAPI::CONSOLE_LOG_DEBUG_TAG(tag, msg);
            case 1:
                return YomkAPI::CONSOLE_LOG_INFO_TAG(tag, msg);
            case 2:
                return YomkAPI::CONSOLE_LOG_WARN_TAG(tag, msg);
            default:
                return YomkAPI::CONSOLE_LOG_ERROR_TAG(tag, msg);
            }
        };
        auto fileLevel = [](int level, const std::string &name, const std::string &msg) -> YomkResponse
        {
            switch (level)
            {
            case 0:
                return YomkAPI::FILE_LOG_DEBUG_TAG(name, "lg3s5", msg);
            case 1:
                return YomkAPI::FILE_LOG_INFO_TAG(name, "lg3s5", msg);
            case 2:
                return YomkAPI::FILE_LOG_WARN_TAG(name, "lg3s5", msg);
            default:
                return YomkAPI::FILE_LOG_ERROR_TAG(name, "lg3s5", msg);
            }
        };
        auto switchLevel = [](int level, bool turnOn) -> YomkResponse
        {
            if (turnOn)
            {
                switch (level)
                {
                case 0:
                    return YOMK_ON_CONSOLE_LOG_DEBUG();
                case 1:
                    return YOMK_ON_CONSOLE_LOG_INFO();
                case 2:
                    return YOMK_ON_CONSOLE_LOG_WARN();
                default:
                    return YOMK_ON_CONSOLE_LOG_ERROR();
                }
            }
            switch (level)
            {
            case 0:
                return YOMK_OFF_CONSOLE_LOG_DEBUG();
            case 1:
                return YOMK_OFF_CONSOLE_LOG_INFO();
            case 2:
                return YOMK_OFF_CONSOLE_LOG_WARN();
            default:
                return YOMK_OFF_CONSOLE_LOG_ERROR();
            }
        };

        // 预热 sink：churn 之外的所有日志调用一律使用已存在 logger，
        // 确保新 logger 只由 churn 线程“先 console 后 file”成对产生（判别式前提）
        CHECK(YomkAPI::CONSOLE_LOG_INFO_TAG(kConsoleSink, "lg3s5_warmup").m_status == YomkResponse::eOk,
              "S5: 预热 console sink logger 成功");
        CHECK(YOMK_FILE_LOG_CREATE(pairDir.string(), kFileSink).m_status == YomkResponse::eOk,
              "S5: 预热 file sink logger 成功");

        // 基线快照
        uint64_t baseC = 0;
        uint64_t baseF = 0;
        countLoggerLines(unpackLines(YOMK_LOGGER_INFO_LOGGERS()), baseC, baseF);
        CHECK((baseC > 0 && baseF > 0),
              "S5: 基线非空（console " + std::to_string(baseC) + " / file " + std::to_string(baseF) + "）");

        g_s5PairConsoleOk.store(0);
        g_s5PairFileOk.store(0);
        g_s5PairFileNo.store(0);
        g_s5ConsoleOk.store(0);
        g_s5FileOk.store(0);
        g_s5WriteOk.store(0);
        g_s5SwitchOk.store(0);
        g_s5IntrospectOk.store(0);
        g_s5BadStatus.store(0);
        g_s5BadLevelLine.store(0);
        g_s5AtomicityViolations.store(0);
        g_s5Stop.store(false);
        {
            std::lock_guard<std::mutex> lock(g_s5SnapMutex);
            g_s5SnapsByThread.assign(kIntrospectThreads, std::vector<IntrospectSnapshot>());
        }

        {
            // S5 不断言 console 输出内容，捕获仅为降噪（避免上千行日志涌向终端）
            ThreadSafeCoutCapture cap;

            // churn 线程：每轮先 console 后 file 成对创建同名 logger
            std::vector<std::thread> churnThreads;
            churnThreads.reserve(kChurnThreads);
            for (int t = 0; t < kChurnThreads; ++t)
            {
                churnThreads.emplace_back([t, &pairDir]()
                                          {
                    for (int r = 0; r < kChurnRounds; ++r)
                    {
                        std::string name = "lg3_s5_pair_t" + std::to_string(t) + "_r" + std::to_string(r);
                        auto consoleResp = YomkAPI::CONSOLE_LOG_INFO_TAG(name, "lg3s5_pair");
                        if (!isLegalStatus(consoleResp))
                        {
                            g_s5BadStatus.fetch_add(1, std::memory_order_relaxed);
                        }
                        if (consoleResp.m_status == YomkResponse::eOk)
                        {
                            g_s5PairConsoleOk.fetch_add(1, std::memory_order_relaxed);
                        }
                        // 成对语义：console 先于 file（任意真实时刻 C(T) >= F(T)）
                        auto fileResp = YOMK_FILE_LOG_CREATE(pairDir.string(), name);
                        if (!isLegalStatus(fileResp))
                        {
                            g_s5BadStatus.fetch_add(1, std::memory_order_relaxed);
                        }
                        if (fileResp.m_status == YomkResponse::eOk)
                        {
                            g_s5PairFileOk.fetch_add(1, std::memory_order_relaxed);
                        }
                        else if (fileResp.m_status == YomkResponse::eNo)
                        {
                            g_s5PairFileNo.fetch_add(1, std::memory_order_relaxed);
                        }
                    } });
            }

            // 内省线程：紧密轮询 LOGGERS/ALL 直到 churn 完成（采样贯穿整个变更窗口）
            std::vector<std::thread> introspectThreads;
            introspectThreads.reserve(kIntrospectThreads);
            for (int t = 0; t < kIntrospectThreads; ++t)
            {
                introspectThreads.emplace_back([t]()
                                               {
                    std::vector<IntrospectSnapshot> local;
                    while (!g_s5Stop.load(std::memory_order_acquire))
                    {
                        IntrospectSnapshot snap{};
                        auto loggersLines = unpackLines(YOMK_LOGGER_INFO_LOGGERS());
                        countLoggerLines(loggersLines, snap.consoleLines, snap.fileLines);

                        auto allResp = YOMK_LOGGER_INFO_ALL();
                        if (!isLegalStatus(allResp))
                        {
                            g_s5BadStatus.fetch_add(1, std::memory_order_relaxed);
                        }
                        auto allLines = unpackLines(allResp);
                        if (!allLines.empty())
                        {
                            if (!checkLevelLineFormat(allLines.front()) ||
                                allLines.front().find("proxy:on") == std::string::npos)
                            {
                                g_s5BadLevelLine.fetch_add(1, std::memory_order_relaxed);
                            }
                            snap.allLines = allLines.size();
                            countLoggerLines(allLines, snap.allConsole, snap.allFile);
                            g_s5IntrospectOk.fetch_add(1, std::memory_order_relaxed);
                            local.push_back(snap);
                        }
                        std::this_thread::sleep_for(std::chrono::microseconds(10));
                    }
                    {
                        std::lock_guard<std::mutex> lock(g_s5SnapMutex);
                        g_s5SnapsByThread[static_cast<size_t>(t)] = std::move(local);
                    } });
            }

            // 级别开关翻转线程
            std::vector<std::thread> switchThreads;
            switchThreads.reserve(kSwitchThreads);
            for (int t = 0; t < kSwitchThreads; ++t)
            {
                switchThreads.emplace_back([t, &switchLevel]()
                                           {
                    for (int r = 0; r < kSwitchRounds; ++r)
                    {
                        int level = (r + t) % 4;
                        bool turnOn = ((r / 4) % 2) == 1;
                        auto resp = switchLevel(level, turnOn);
                        if (!isLegalStatus(resp))
                        {
                            g_s5BadStatus.fetch_add(1, std::memory_order_relaxed);
                        }
                        if (resp.m_status == YomkResponse::eOk)
                        {
                            g_s5SwitchOk.fetch_add(1, std::memory_order_relaxed);
                        }
                        std::this_thread::sleep_for(std::chrono::microseconds(50));
                    } });
            }

            // console 日志线程（四级别轮转，已存在 sink tag）
            std::vector<std::thread> consoleThreads;
            consoleThreads.reserve(kConsoleThreads);
            for (int t = 0; t < kConsoleThreads; ++t)
            {
                consoleThreads.emplace_back([t, &kConsoleSink, &consoleLevel]()
                                            {
                    for (int r = 0; r < kConsoleRounds; ++r)
                    {
                        auto resp = consoleLevel((r + t) % 4, kConsoleSink,
                                                 "lg3s5_c" + std::to_string(t) + "_r" + std::to_string(r));
                        if (!isLegalStatus(resp))
                        {
                            g_s5BadStatus.fetch_add(1, std::memory_order_relaxed);
                        }
                        if (resp.m_status == YomkResponse::eOk)
                        {
                            g_s5ConsoleOk.fetch_add(1, std::memory_order_relaxed);
                        }
                    } });
            }

            // file 日志线程（四级别轮转 + 周期性 WRITE）
            std::vector<std::thread> fileThreads;
            fileThreads.reserve(kFileThreads);
            for (int t = 0; t < kFileThreads; ++t)
            {
                fileThreads.emplace_back([t, &kFileSink, &fileLevel]()
                                         {
                    for (int r = 0; r < kFileRounds; ++r)
                    {
                        auto resp = fileLevel((r + t) % 4, kFileSink,
                                              "lg3s5_f" + std::to_string(t) + "_r" + std::to_string(r));
                        if (!isLegalStatus(resp))
                        {
                            g_s5BadStatus.fetch_add(1, std::memory_order_relaxed);
                        }
                        if (resp.m_status == YomkResponse::eOk)
                        {
                            g_s5FileOk.fetch_add(1, std::memory_order_relaxed);
                        }
                        if (r % 20 == 19)
                        {
                            auto writeResp = YOMK_FILE_LOG_WRITE(kFileSink);
                            if (!isLegalStatus(writeResp))
                            {
                                g_s5BadStatus.fetch_add(1, std::memory_order_relaxed);
                            }
                            if (writeResp.m_status == YomkResponse::eOk)
                            {
                                g_s5WriteOk.fetch_add(1, std::memory_order_relaxed);
                            }
                        }
                    } });
            }

            // churn 先结束，再停内省线程（保证采样贯穿整个变更窗口）
            for (auto &th : churnThreads)
            {
                th.join();
            }
            g_s5Stop.store(true, std::memory_order_release);
            for (auto &th : introspectThreads)
            {
                th.join();
            }
            for (auto &th : switchThreads)
            {
                th.join();
            }
            for (auto &th : consoleThreads)
            {
                th.join();
            }
            for (auto &th : fileThreads)
            {
                th.join();
            }
        }

        // ---- 收尾：四级别全 ON + file sink 落盘 ----
        CHECK(YOMK_ON_CONSOLE_LOG_DEBUG().m_status == YomkResponse::eOk, "S5: 收尾 ON debug eOk");
        CHECK(YOMK_ON_CONSOLE_LOG_INFO().m_status == YomkResponse::eOk, "S5: 收尾 ON info eOk");
        CHECK(YOMK_ON_CONSOLE_LOG_WARN().m_status == YomkResponse::eOk, "S5: 收尾 ON warn eOk");
        CHECK(YOMK_ON_CONSOLE_LOG_ERROR().m_status == YomkResponse::eOk, "S5: 收尾 ON error eOk");
        CHECK(YOMK_FILE_LOG_WRITE(kFileSink).m_status == YomkResponse::eOk, "S5: 收尾 file sink WRITE eOk");

        // ---- P3-a 判别式：对每个快照验证 C >= F（相对基线增量）----
        uint64_t finalC = 0;
        uint64_t finalF = 0;
        countLoggerLines(unpackLines(YOMK_LOGGER_INFO_LOGGERS()), finalC, finalF);
        const uint64_t finalTotal = finalC + finalF;

        uint64_t loggersViolations = 0; // LOGGERS 端点 F > C 违例
        uint64_t allViolations = 0;     // ALL 端点 F > C 违例
        uint64_t nonMonotonic = 0;      // 同线程快照序列总行数递减违例
        uint64_t exceedFinal = 0;       // 快照行数超过最终值违例
        uint64_t snapshotCount = 0;
        {
            std::lock_guard<std::mutex> lock(g_s5SnapMutex);
            for (const auto &snaps : g_s5SnapsByThread)
            {
                uint64_t prevTotal = 0;
                bool first = true;
                for (const auto &snap : snaps)
                {
                    ++snapshotCount;
                    int64_t deltaC = static_cast<int64_t>(snap.consoleLines) - static_cast<int64_t>(baseC);
                    int64_t deltaF = static_cast<int64_t>(snap.fileLines) - static_cast<int64_t>(baseF);
                    if (deltaF > deltaC)
                    {
                        ++loggersViolations;
                    }
                    int64_t allDeltaC = static_cast<int64_t>(snap.allConsole) - static_cast<int64_t>(baseC);
                    int64_t allDeltaF = static_cast<int64_t>(snap.allFile) - static_cast<int64_t>(baseF);
                    if (allDeltaF > allDeltaC)
                    {
                        ++allViolations;
                    }
                    uint64_t total = snap.consoleLines + snap.fileLines;
                    if (!first && total < prevTotal)
                    {
                        ++nonMonotonic;
                    }
                    first = false;
                    prevTotal = total;
                    if (total > finalTotal)
                    {
                        ++exceedFinal;
                    }
                }
            }
        }
        g_s5AtomicityViolations.store(loggersViolations + allViolations);

        CHECK(snapshotCount > 0,
              "S5: 收集到 " + std::to_string(snapshotCount) + " 个内省快照");
        CHECK(loggersViolations == 0,
              "S5: LOGGERS 快照 C>=F 恒成立（违例 " + std::to_string(loggersViolations) +
                  " 个）——P3-a 嵌套双锁原子快照实证（分段锁下 file 段晚于 console 段会产生 F>C）");
        CHECK(allViolations == 0,
              "S5: ALL 快照 C>=F 恒成立（违例 " + std::to_string(allViolations) + " 个）——listAll 同为原子快照");
        CHECK(nonMonotonic == 0,
              "S5: 同线程快照序列总行数单调不减（递减 " + std::to_string(nonMonotonic) +
                  " 次）——logger 只增不减且快照不丢行");
        CHECK(exceedFinal == 0,
              "S5: 全部快照行数 <= 最终值 " + std::to_string(finalTotal) + "（超出 " +
                  std::to_string(exceedFinal) + " 次）——无幻觉行");
        CHECK(g_s5BadLevelLine.load() == 0,
              "S5: 并发下 ALL 首行格式全部合法且含 proxy:on（非法 " +
                  std::to_string(g_s5BadLevelLine.load()) + " 次）");
        CHECK(g_s5BadStatus.load() == 0,
              "S5: 全部响应码属于 {eInvalid,eOk,eNo}（非法状态码 " +
                  std::to_string(g_s5BadStatus.load()) + " 次）");

        // ---- churn 守恒 ----
        CHECK(g_s5PairConsoleOk.load() == kPairTotal,
              "S5: 成对创建 console 侧守恒 eOk == " + std::to_string(kPairTotal) + "（实际 " +
                  std::to_string(g_s5PairConsoleOk.load()) + "）");
        CHECK(g_s5PairFileOk.load() == kPairTotal,
              "S5: 成对创建 file 侧守恒 eOk == " + std::to_string(kPairTotal) + "（实际 " +
                  std::to_string(g_s5PairFileOk.load()) + "）");
        CHECK(g_s5PairFileNo.load() == 0, "S5: 成对创建无重名失败（唯一名设计）");
        CHECK(finalC - baseC == kPairTotal,
              "S5: console logger 最终新增 == " + std::to_string(kPairTotal) + "（实际 " +
                  std::to_string(finalC - baseC) + "）——并发创建无丢失");
        CHECK(finalF - baseF == kPairTotal + 0,
              "S5: file logger 最终新增 == " + std::to_string(kPairTotal) + "（实际 " +
                  std::to_string(finalF - baseF) + "）——并发创建无丢失");

        CHECK(g_s5ConsoleOk.load() == kConsoleThreads * kConsoleRounds,
              "S5: console 日志守恒 eOk == " + std::to_string(kConsoleThreads * kConsoleRounds) +
                  "（实际 " + std::to_string(g_s5ConsoleOk.load()) + "）——级别开关并发翻转不影响响应码");
        CHECK(g_s5FileOk.load() == kFileThreads * kFileRounds,
              "S5: file 日志守恒 eOk == " + std::to_string(kFileThreads * kFileRounds) + "（实际 " +
                  std::to_string(g_s5FileOk.load()) + "）");
        CHECK(g_s5SwitchOk.load() == kSwitchThreads * kSwitchRounds,
              "S5: 级别开关翻转守恒 eOk == " + std::to_string(kSwitchThreads * kSwitchRounds) +
                  "（实际 " + std::to_string(g_s5SwitchOk.load()) + "）");
        CHECK(g_s5WriteOk.load() > 0,
              "S5: 周期性 WRITE 确实执行（" + std::to_string(g_s5WriteOk.load()) + " 次 eOk）");
        CHECK(g_s5IntrospectOk.load() > 0,
              "S5: 内省线程确实观测到 ALL（" + std::to_string(g_s5IntrospectOk.load()) + " 次）");

        // ---- 最终一致性：ALL 首行精确串 + 行数跨端点一致 + file sink 落盘守恒 ----
        auto finalAll = unpackLines(YOMK_LOGGER_INFO_ALL());
        CHECK((!finalAll.empty() &&
               finalAll.front() == "console:debug:on info:on warn:on error:on proxy:on"),
              "S5: 收尾四级别全 ON 后 ALL 首行为精确串（含 S3 已设的 proxy:on）");
        CHECK(finalAll.size() == finalTotal + 1,
              "S5: ALL 行数 == LOGGERS 行数 + 1 首行（" + std::to_string(finalAll.size()) + " vs " +
                  std::to_string(finalTotal + 1) + "）——跨端点最终一致");

        std::string sinkContent = readFile(pairDir / (kFileSink + ".log"));
        uint64_t sinkLines = countLines(sinkContent);
        CHECK(sinkLines == kFileThreads * kFileRounds,
              "S5: file sink 落盘行数 == " + std::to_string(kFileThreads * kFileRounds) + "（实际 " +
                  std::to_string(sinkLines) + "）——级别翻转与并发 WRITE 下不丢不重");
    }

    YOMK_SHUTDOWN();

    std::error_code rmEc;
    fs::remove_all(tmpDir, rmEc);

    std::cout << "\n=== Result: " << (g_total - g_failed) << "/" << g_total
              << " passed, " << g_failed << " failed ===" << std::endl;
    return g_failed == 0 ? 0 : 1;
}
