// YomkLogger 压力测试
//
// 覆盖测试要求第 7 类（压力/吞吐/大数据量），骨架同 TestYomkFunctionPoolStress：
// - S1：console 侧规模与热路径（N 个唯一 tag 惰性创建 / 1000 行内容守恒抽样 /
// 命中写 / 级别 OFF 短路 / proxy 短路 / 热 tag 重复写）；Phase A 分段计时并在
// 1k/10k/100k/N 四个规模点测 ALL 延迟（console 表线性增长是全程
// 唯一能覆盖中小规模的窗口，S2 起条目数只增不减）
// - S2：file 侧激进规模（N 个 logger 分片 1000/目录 + N/10 单目录对照相 + 磁盘足迹守恒）
// - S3：文件路径写入与大缓冲落盘（N 行缓冲分布 K 个 logger / 2N 行单 logger 大缓冲一次落盘 /
// K 个 logger 逐个 flush 行数守恒 / 空缓冲重复 flush 文件不增长）
// - S4：内省端点在 2N+ 条目下的响应（全规模 LOGGERS/ALL 延迟 + ALL 数据段与 LOGGERS 逐行一致 /
// 单查 N/100 次 / 内省与 createFileLogger 并发相互阻塞量化）
// - S5：8 线程混合负载 churn（console 写 + file 建/写 + ALL 内省 + 删除 + 删除后单查）
// + 全量删除回收（分组 ResStatus::Succeeded 守恒 → 残量扫除 → LOGGERS 行数为 0 硬断言）
// - S6：收尾（清理前磁盘足迹守恒 / yomk::server().shutdown() 计时 / remove_all /
// 临时目录不存在硬断言（用户口径：测试后临时文件全部清理干净）/ RSS 峰值）
//
// 规模经 YOMK_TEST_STRESS_SCALE 参数化（缺省 100000，clamp 下限 1000），对称激进口径：
// console tag = N；file logger = N（分片）+ N/10（单目录对照）+ 2×N/100（S4-D）+ 1（S3 大缓冲）
// + N/500×8（S5 churn，建后即删但磁盘文件保留）→ 峰值临时根（YOMK_TEST_TMP_ROOT 注入，缺省 /tmp）下约 1.11N + 0.016N 个真实 .log
// 文件（FileLogger::init 语义：创建即建空文件并建目录；删除接口不删磁盘文件）。
// 每 Section 记录 [BASELINE] 吞吐/延迟但不断言 timing（环境相关），仅断言守恒与正确性。
// 断言总数随 N 变化（S1A 扫描点个数 = 去重后的 {1000,10000,100000,N} 交集），N=1000 时 61 条。
//
// 各构建降档：默认 N=100000；cov/ASan N=10000；TSan N=1000（clamp 下限）。
//
// 断言可靠性约束：
// - cout 重定向一律用 RAII 作用域，所有 YOMK_TEST_CHECK 与 [BASELINE] 打印都在作用域之外（否则被吞没）；
// 计时值先存局部变量，出作用域后再打印。
// - 唯一 tag 用 log().tag(t).info(...) 链式累积（运行时拼名，tag 为普通变量即可）。
// - 库内部不使用 log().info 发射终端（YOMK_DIAG_ERROR 直写 cout），故 console 表起点恰为
// 构造函数预置的 main，各 Section 可用绝对条目数守恒断言（起点已断言）。
//
// 风格：纯 main() + 失败计数，返回非 0 表示存在失败用例（零第三方依赖）

#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "yomk/log/YomkLoggerApi.h"
#include "yomk/YomkDiag.h"

namespace fs = std::filesystem;

static int g_failed = 0;
static int g_total = 0;

#define YOMK_TEST_CHECK(cond, msg)                                                          \
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
// 压力规模参数化
// ============================================================================
static uint64_t stressScale()
{
    const char* env = std::getenv("YOMK_TEST_STRESS_SCALE");
    uint64_t n = env ? std::strtoull(env, nullptr, 10) : 100000;
    return n < 1000 ? 1000 : n;
}

// ============================================================================
// [BASELINE] 记录辅助
// ============================================================================
using Clock = std::chrono::steady_clock;

static double msSince(const Clock::time_point& t0)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

static void recordBaseline(const char* section, uint64_t ops, double elapsedMs)
{
    double opsPerSec = (elapsedMs > 0.0) ? (ops / (elapsedMs / 1000.0)) : 0.0;
    std::cout << "[BASELINE] " << section << ": " << std::fixed << std::setprecision(0) << opsPerSec << " ops/s ("
              << ops << " ops in " << std::setprecision(1) << elapsedMs << " ms)" << std::endl;
}

// 延迟分布（内省端点/单次 create 用）
struct LatencyStat
{
    uint64_t count = 0;
    double minMs = 0.0;
    double avgMs = 0.0;
    double maxMs = 0.0;
};

static void recordLatency(const char* section, const LatencyStat& stat)
{
    std::cout << "[BASELINE] " << section << ": avg " << std::fixed << std::setprecision(3) << stat.avgMs << " ms (min "
              << stat.minMs << " / max " << stat.maxMs << " over " << stat.count << " calls)" << std::endl;
}

static void feedLatency(LatencyStat& stat, double& totalMs, double ms)
{
    if (stat.count == 0)
    {
        stat.minMs = ms;
        stat.maxMs = ms;
    }
    else
    {
        if (ms < stat.minMs)
            stat.minMs = ms;
        if (ms > stat.maxMs)
            stat.maxMs = ms;
    }
    totalMs += ms;
    ++stat.count;
    stat.avgMs = totalMs / static_cast<double>(stat.count);
}

// 进程 RSS 峰值（Linux: /proc/self/status VmHWM；其他平台 n/a）
static std::string readVmHWM()
{
#ifdef __linux__
    std::ifstream ifs("/proc/self/status");
    std::string line;
    while (std::getline(ifs, line))
    {
        if (line.size() > 6 && line.compare(0, 6, "VmHWM:") == 0)
        {
            return line.substr(6);
        }
    }
    return " unknown";
#else
    return " n/a";
#endif
}

// 采样环境快照：把 CPU 核数、系统负载、可用内存随每次运行一同打印，使 [BASELINE]
// 性能数据自带环境标注、可按环境归因（绝对值跨环境不可直接复现）。
// 纯信息行、非断言：读不到时降级为 "n/a"，任何情况下都不影响测试结果与断言计数。
static std::string readEnvSnapshot()
{
    std::string env = "cpu=" + std::to_string(std::thread::hardware_concurrency());
#ifdef __linux__
    // /proc/loadavg 前三列为 1/5/15 分钟平均 runnable 数，远超 cpu 数即表明采样期存在
    // 资源竞争；按原始文本提取（不经 double 转换）以保留精度
    std::string load = "n/a";
    {
        std::ifstream ifs("/proc/loadavg");
        std::string line;
        if (std::getline(ifs, line))
        {
            std::istringstream iss(line);
            std::string l1;
            std::string l5;
            std::string l15;
            if (iss >> l1 >> l5 >> l15)
                load = l1 + "/" + l5 + "/" + l15;
        }
    }
    env += " loadavg=" + load;
    // MemAvailable 是内核对“可供新分配用”的估计（含可回收缓存），比 MemFree 更能
    // 反映真实内存压力（本测试全规模 VmHWM 达 491 MB，可用内存直接决定缺页频率）
    std::string memAvail = "n/a";
    {
        std::ifstream ifs("/proc/meminfo");
        std::string line;
        while (std::getline(ifs, line))
        {
            if (line.size() > 13 && line.compare(0, 13, "MemAvailable:") == 0)
            {
                // /proc/meminfo 的数值列是右对齐的，冒号后带若干前导空格；跳过它们才是
                // 可直接写进 doc 头的干净值。找不到非空白字符时保持 "n/a" 降级
                const size_t valuePos = line.find_first_not_of(" \t", 13);
                if (valuePos != std::string::npos)
                    memAvail = line.substr(valuePos);
                break;
            }
        }
    }
    env += " memAvail=" + memAvail;
#else
    env += " loadavg=n/a memAvail=n/a";
#endif
    return env;
}

// ============================================================================
// cout 重定向装置（含丢弃态）
// 丢弃态不加锁、无共享状态：既避免 10 万行输出占用内存，也不干扰被测热路径的并发度
// ============================================================================
class ThreadSafeCoutCapture
{
    class Buf : public std::streambuf
    {
    public:
        explicit Buf(bool discard) : m_discard(discard) {}
        std::string take()
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_text;
        }

    protected:
        int overflow(int ch) override
        {
            if (m_discard || ch == traits_type::eof())
            {
                return ch;
            }
            std::lock_guard<std::mutex> lock(m_mutex);
            m_text.push_back(static_cast<char>(ch));
            return ch;
        }
        std::streamsize xsputn(const char* s, std::streamsize n) override
        {
            if (m_discard)
            {
                return n;
            }
            std::lock_guard<std::mutex> lock(m_mutex);
            m_text.append(s, static_cast<size_t>(n));
            return n;
        }

    private:
        const bool m_discard;
        std::mutex m_mutex;
        std::string m_text;
    };

public:
    explicit ThreadSafeCoutCapture(bool discard = false) : m_buf(discard), m_old(std::cout.rdbuf(&m_buf)) {}
    ~ThreadSafeCoutCapture() { std::cout.rdbuf(m_old); }
    ThreadSafeCoutCapture(const ThreadSafeCoutCapture&) = delete;
    ThreadSafeCoutCapture& operator=(const ThreadSafeCoutCapture&) = delete;
    std::string str() { return m_buf.take(); }

private:
    Buf m_buf;
    std::streambuf* m_old;
};

// ============================================================================
// 文件级观测装置（TSan-clean：跨线程共享状态一律原子或互斥保护）
// ============================================================================
static std::atomic<uint64_t> g_proxyHits{0};
static std::atomic<bool> g_proxyReturn{true};

// S4-D：内省与 createFileLogger 并发相互阻塞量化
static std::atomic<uint64_t> g_s4dCreateOk{0};
static std::atomic<uint64_t> g_s4dCreateNs{0};
static std::atomic<uint64_t> g_s4dSnapCount{0};
static std::atomic<uint64_t> g_s4dSnapNonMono{0};  // 快照条目数递减次数（原子快照语义违例）
static std::atomic<uint64_t> g_s4dSnapBadStatus{0};
static std::atomic<bool> g_s4dStop{false};

// S5：8 线程混合负载守恒
static std::atomic<uint64_t> g_s5ConsoleOk{0};
static std::atomic<uint64_t> g_s5FileOk{0};
static std::atomic<uint64_t> g_s5LogOk{0};
static std::atomic<uint64_t> g_s5IntrospectOk{0};
static std::atomic<uint64_t> g_s5DeleteOk{0};
static std::atomic<uint64_t> g_s5PostDeleteNo{0};
static std::atomic<uint64_t> g_s5BadStatus{0};

// 起始屏障
static std::atomic<int> g_readyCount{0};
static std::atomic<bool> g_gateGo{false};

// ============================================================================
// 辅助函数
// ============================================================================

// 带零填充的唯一名
static std::string makeName(const char* prefix, uint64_t index)
{
    std::ostringstream oss;
    oss << prefix << std::setfill('0') << std::setw(8) << index;
    return oss.str();
}

// 解包 LOGGERS/ALL 的 StringArray；失败返回空 vector
// 出包即掏空（第三重拷贝消除：服务端建 lines 与入包两处已是移动）。resp.m_data 是
// shared_ptr，其 const 只约束
// 指针本身、不约束所指对象，故此处有意掏空包内 vector；调用方不得在解包后复用同一 resp
// 的数据段（11 个调用点已逐一核实：8 处传就地临时对象，3 处传变量者在解包前只读
// m_status、解包后不再触及该 resp）
static std::vector<std::string> unpackLines(const yomk::payload::Response& resp)
{
    std::vector<std::string> lines;
    if (resp.m_status != yomk::payload::ResStatus::Succeeded || !resp.m_data)
    {
        return lines;
    }
    auto arr = yomk::msgs::unpack<yomk::msgs::StringArray>(resp.m_data);
    if (arr)
    {
        lines = std::move(arr->d);
    }
    return lines;
}

// 状态码是否属于框架合法集合 {ResStatus::Invalid, ResStatus::Succeeded, ResStatus::Failed}
static bool isLegalStatus(const yomk::payload::Response& resp)
{
    return resp.m_status == yomk::payload::ResStatus::Invalid || resp.m_status == yomk::payload::ResStatus::Succeeded ||
           resp.m_status == yomk::payload::ResStatus::Failed;
}

// LOG_API ResStatus 化后的三态重载（emit/create/flush/delete 直接返回三态）
static bool isLegalStatus(yomk::payload::ResStatus status)
{
    return status == yomk::payload::ResStatus::Invalid || status == yomk::payload::ResStatus::Succeeded ||
           status == yomk::payload::ResStatus::Failed;
}

// 时间戳格式精确校验：行首 25 字符须为 "[dddd-dd-dd dd:dd:dd.ddd]"
static bool checkTimeFormat(const std::string& line)
{
    auto isDigit = [](char c) { return c >= '0' && c <= '9'; };
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

// 统计"行尾最后一段"以 prefix 开头的 marker 出现次数（内容守恒判定）
static std::map<std::string, uint64_t> collectTailMarkers(const std::string& text, const std::string& prefix)
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

// markers 中出现次数 != 1 的条目数（0 即"不丢不重"）
static uint64_t countBadOccurrence(const std::map<std::string, uint64_t>& markers)
{
    uint64_t bad = 0;
    for (const auto& item : markers)
    {
        if (item.second != 1)
        {
            ++bad;
        }
    }
    return bad;
}

// 统计文件行数（ifstream getline，避免整文件读入内存）
static uint64_t countFileLines(const fs::path& path, bool& ok)
{
    std::ifstream ifs(path);
    ok = ifs.is_open();
    if (!ok)
    {
        return 0;
    }
    uint64_t lines = 0;
    std::string line;
    while (std::getline(ifs, line))
    {
        if (!line.empty())
        {
            ++lines;
        }
    }
    return lines;
}

// 递归统计目录下普通文件数（全程 error_code，不抛异常；S6 清理前的磁盘足迹守恒用）
static uint64_t countDiskFiles(const fs::path& root)
{
    std::error_code ec;
    if (!fs::exists(root, ec))
    {
        return 0;
    }
    uint64_t files = 0;
    fs::recursive_directory_iterator iter(root, fs::directory_options::none, ec);
    for (fs::recursive_directory_iterator end; iter != end; iter.increment(ec))
    {
        if (ec)
        {
            break;
        }
        if (iter->is_regular_file(ec))
        {
            ++files;
        }
    }
    return files;
}

// 内省轮次策略：条目越多轮次越少（控制单次运行时长，数据点仍具统计意义）
static uint64_t introspectionRounds(uint64_t entries)
{
    if (entries <= 2000)
        return 200;
    if (entries <= 20000)
        return 100;
    if (entries <= 200000)
        return 20;
    return 5;
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

// 对内省端点做 rounds 次调用，返回延迟分布；非 ResStatus::Succeeded 计入 badStatus
static LatencyStat measureIntrospection(uint64_t rounds, bool useAll, uint64_t& badStatus, uint64_t& lastLines)
{
    LatencyStat stat;
    double totalMs = 0.0;
    for (uint64_t i = 0; i < rounds; ++i)
    {
        auto t0 = Clock::now();
        auto resp = useAll ? yomk::log_mgr().all() : yomk::log_mgr().console().list();
        double ms = msSince(t0);
        if (resp.m_status != yomk::payload::ResStatus::Succeeded)
        {
            ++badStatus;
            continue;
        }
        if (useAll)
        {
            auto listPkg = yomk::msgs::unpack<yomk::msgs::LoggerInfoList>(resp.m_data);
            lastLines = listPkg ? listPkg->d.size() : 0;
        }
        else
        {
            lastLines = unpackLines(resp).size();
        }
        feedLatency(stat, totalMs, ms);
    }
    return stat;
}

// 起始屏障：线程内调用，等待主线程放行
static void waitGate()
{
    g_readyCount.fetch_add(1, std::memory_order_release);
    while (!g_gateGo.load(std::memory_order_acquire))
    {
        std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
}

// 主线程侧：等待 threadCount 个线程就绪后放行
static void releaseGate(int threadCount)
{
    int waitMs = 0;
    while (g_readyCount.load(std::memory_order_acquire) < threadCount && waitMs < 30000)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        ++waitMs;
    }
    g_gateGo.store(true, std::memory_order_release);
}

static void resetGate()
{
    g_readyCount.store(0);
    g_gateGo.store(false);
}

// ============================================================================
// S1 Phase D 用 proxy：命中计数，返回值由 g_proxyReturn 控制
// （proxy 安装后本测试全程不卸载，故此后恒置 true 穿透，不影响后续 Section；
//  可传 nullptr 卸载，卸载语义由 Lifecycle S3 覆盖）
// ============================================================================
static bool lg4Proxy(const yomk::payload::Log& /*log*/)
{
    g_proxyHits.fetch_add(1, std::memory_order_relaxed);
    return g_proxyReturn.load(std::memory_order_relaxed);
}

// 测试临时根：run_tests.sh 注入（test/test_logs/tmp），独立运行回退 /tmp
static fs::path testTmpRoot()
{
    const char* env = std::getenv("YOMK_TEST_TMP_ROOT");
    return (env && *env) ? fs::path(env) : fs::temp_directory_path();
}

// ============================================================================
// main
// ============================================================================
int main()
{
    std::cout << "=== TestYomkLoggerStress (LG4) ===" << std::endl;

    const uint64_t N = stressScale();
    std::cout << "[CONFIG] YOMK_TEST_STRESS_SCALE = " << N << std::endl;
    // 环境标注先于一切 [BASELINE] 输出，使本次所有性能数据可按环境归因
    std::cout << "[ENV] " << readEnvSnapshot() << std::endl;

    yomk::server().async_thread(1).init();
    auto server = yomk::server().instance();
    YOMK_TEST_CHECK(server != nullptr, "yomk::server().init() 返回非空服务器");

    fs::path tmpDir = testTmpRoot() / ("yomk_logger_lg4_" + std::to_string(::getpid()));
    std::error_code ec;
    fs::create_directories(tmpDir, ec);
    YOMK_TEST_CHECK(!ec, "临时目录创建成功: " + tmpDir.string());

    // ---- 规模派生量（各 Section 共享同一口径：S5 全量回收与 S6 磁盘守恒都按此推算）----
    const uint64_t kShardSize = 1000;  // S2 分片粒度（每目录 1000 个 .log）
    const uint64_t shardCount = (N + kShardSize - 1) / kShardSize;
    const uint64_t M = N / 10;                                 // S2 单目录对照相规模
    const uint64_t K = (N < 100) ? N : 100;                    // S3 缓冲相 sink logger 数（复用 S2 产物）
    const uint64_t D = (N / 100 == 0) ? 1 : N / 100;           // S4-D 并发 create 规模
    const uint64_t churnRounds = (N / 500 < 2) ? 2 : N / 500;  // S5 每线程 churn 轮数
    const uint64_t introspectEvery = (churnRounds / 10 < 1) ? 1 : churnRounds / 10;
    const uint64_t introspectPerThread = (churnRounds + introspectEvery - 1) / introspectEvery;
    const uint64_t kThreads = 8;
    const uint64_t churnTotal = churnRounds * kThreads;
    const fs::path s2Dir = tmpDir / "s2";
    const fs::path s2Single = tmpDir / "s2single";
    std::cout << "[CONFIG] N=" << N << " shard=" << shardCount << " M=" << M << " K=" << K << " D=" << D
              << " churn=" << churnTotal << "(" << kThreads << "x" << churnRounds << ", introspect every "
              << introspectEvery << ")" << std::endl;

    // 分片目录路径预构造（仅 shardCount 条），排除循环内字符串拼接开销
    std::vector<std::string> shardDirs;
    shardDirs.reserve(static_cast<size_t>(shardCount));
    for (uint64_t s = 0; s < shardCount; ++s)
    {
        shardDirs.push_back((s2Dir / ("shard_" + std::to_string(s))).string());
    }

    // 起点基线：库内部不使用 log().info 链式发射 → console 表恰为构造函数预置的 main
    {
        const uint64_t c = unpackLines(yomk::log_mgr().console().list()).size();
        const uint64_t f = unpackLines(yomk::log_mgr().file().list()).size();
        YOMK_TEST_CHECK(
            (c == 1 && f == 0),
            "起点内省基线 console=1(main)/file=0（实测 " + std::to_string(c) + "/" + std::to_string(f) + "）");
    }

    // ========================================================================
    // S1：console 侧规模与热路径
    // ========================================================================
    std::cout << "\n--- S1: console 侧规模与热路径（N = " << N << "） ---" << std::endl;
    {
        // 名字表预构造：排除 makeName 的字符串构造开销，使各相基线可比
        std::vector<std::string> tags;
        tags.reserve(static_cast<size_t>(N));
        for (uint64_t i = 0; i < N; ++i)
        {
            tags.push_back(makeName("lg4s1_tag_", i));
        }
        const std::string hotTag = tags.empty() ? std::string("lg4s1_hot") : tags[0];

        // 规模-延迟扫描点：console 表从 1 条线性增长到 N+1 条，是全程唯一能覆盖
        // 1k/10k/100k 中小规模的窗口——S2 起 file 表叠加，条目数只增不减，无法再降规模
        std::vector<uint64_t> scanPoints;
        {
            const uint64_t kRawScan[4] = {1000, 10000, 100000, N};
            for (uint64_t raw : kRawScan)
            {
                const uint64_t cp = (raw > N) ? N : raw;
                if (scanPoints.empty() || scanPoints.back() != cp)
                {
                    scanPoints.push_back(cp);
                }
            }
        }

        // ---- Phase A：N 个唯一 tag 首次写入（惰性创建 N 个 ConsoleLogger）----
        // 分段计时：内省扫描落在创建计时区间之外，不污染 S1A 吞吐基线
        uint64_t okA = 0;
        double elapsedA = 0.0;
        uint64_t createdA = 0;
        for (uint64_t cp : scanPoints)
        {
            {
                ThreadSafeCoutCapture cap(true);
                auto t0 = Clock::now();
                for (uint64_t i = createdA; i < cp; ++i)
                {
                    if (yomk::log().tag(tags[static_cast<size_t>(i)]).info("lg4s1a_marker") ==
                        yomk::payload::ResStatus::Succeeded)
                    {
                        ++okA;
                    }
                }
                elapsedA += msSince(t0);
            }
            createdA = cp;

            // 扫描数据点：条目 = createdA 个 tag + main（此刻 file 侧仍为 0）
            const uint64_t entries = createdA + 1;
            uint64_t badAll = 0;
            uint64_t linesAll = 0;
            auto statAll = measureIntrospection(introspectionRounds(entries), true, badAll, linesAll);
            std::cout << "[BASELINE] S1A_scan_ALL@entries=" << entries << ": avg " << std::fixed << std::setprecision(3)
                      << statAll.avgMs << " ms (min " << statAll.minMs << " / max " << statAll.maxMs << " over "
                      << statAll.count << " calls, lines " << linesAll << ")" << std::endl;
            YOMK_TEST_CHECK(
                (badAll == 0 && linesAll == entries),
                "S1A: 扫描点 entries=" + std::to_string(entries) + " 内省 ResStatus::Succeeded + 日志器数守恒");
        }
        recordBaseline("S1A_console_create_unique_tag", N, elapsedA);
        YOMK_TEST_CHECK(okA == N, "S1A: 唯一 tag 写入 ResStatus::Succeeded 守恒 " + std::to_string(okA) + "/" + std::to_string(N));

        const uint64_t consoleEntries = unpackLines(yomk::log_mgr().console().list()).size();
        YOMK_TEST_CHECK(
            consoleEntries == N + 1,
            "S1A: console 表条目 = N + main（实测 " + std::to_string(consoleEntries) + "）");

        // ---- Phase A2：内容守恒抽样（1000 行累积捕获）+ 时间戳格式精确校验----
        {
            const uint64_t kSample = 1000;
            const uint64_t sample = (kSample < N) ? kSample : N;
            std::string out;
            {
                ThreadSafeCoutCapture cap;
                for (uint64_t i = 0; i < sample; ++i)
                {
                    yomk::log().tag(tags[static_cast<size_t>(i)]).info("lg4s1a2_" + std::to_string(i));
                }
                out = cap.str();
            }
            auto markers = collectTailMarkers(out, "lg4s1a2_");
            YOMK_TEST_CHECK(
                markers.size() == sample,
                "S1A2: 抽样 marker 全数命中（不丢）" + std::to_string(markers.size()) + "/" + std::to_string(sample));
            YOMK_TEST_CHECK(countBadOccurrence(markers) == 0, "S1A2: 抽样 marker 无重复（不重）");

            uint64_t sampledLines = 0;
            uint64_t badFormat = 0;
            std::istringstream iss(out);
            std::string line;
            while (std::getline(iss, line))
            {
                if (line.find("lg4s1a2_") == std::string::npos)
                {
                    continue;  // 框架自身噪声行不参与判定
                }
                ++sampledLines;
                if (!checkTimeFormat(line))
                {
                    ++badFormat;
                }
            }
            YOMK_TEST_CHECK(
                (sampledLines == sample && badFormat == 0),
                "S1A2: 抽样行时间戳格式全部合法（P4-b 改写等价性，非法 " + std::to_string(badFormat) + " 条）");
        }

        // ---- Phase B：命中路径写入（N 个既有 tag 各 1 次，含时间戳构造 + cout）----
        // 每行都要过一次 localTimeFormatted()
        uint64_t okB = 0;
        double elapsedB = 0.0;
        {
            ThreadSafeCoutCapture cap(true);
            auto t0 = Clock::now();
            for (uint64_t i = 0; i < N; ++i)
            {
                if (yomk::log().tag(tags[static_cast<size_t>(i)]).info("lg4s1b_marker") ==
                    yomk::payload::ResStatus::Succeeded)
                {
                    ++okB;
                }
            }
            elapsedB = msSince(t0);
        }
        recordBaseline("S1B_console_write_hit", N, elapsedB);
        YOMK_TEST_CHECK(okB == N, "S1B: 命中写 ResStatus::Succeeded 守恒 " + std::to_string(okB) + "/" + std::to_string(N));

        // ---- Phase C：级别 OFF 短路（开关关断后仍 ResStatus::Succeeded，但不产生输出）----
        YOMK_TEST_CHECK(yomk::log_mgr().console().level(yomk::payload::LogLevel::Info).disable() == yomk::payload::ResStatus::Succeeded, "S1C: OFF info 返回 ResStatus::Succeeded");
        {
            const uint64_t kProbe = 100;
            const uint64_t probe = (kProbe < N) ? kProbe : N;
            std::string outOff;
            {
                ThreadSafeCoutCapture cap;
                for (uint64_t i = 0; i < probe; ++i)
                {
                    yomk::log().tag(tags[static_cast<size_t>(i)]).info("lg4s1c_off_marker");
                }
                outOff = cap.str();
            }
            YOMK_TEST_CHECK(
                outOff.find("lg4s1c_off_marker") == std::string::npos,
                "S1C: 级别 OFF 期间无输出（抽样 " + std::to_string(probe) + " 行）");
        }
        uint64_t okC = 0;
        double elapsedC = 0.0;
        {
            ThreadSafeCoutCapture cap(true);
            auto t0 = Clock::now();
            for (uint64_t i = 0; i < N; ++i)
            {
                if (yomk::log().tag(tags[static_cast<size_t>(i)]).info("lg4s1c_marker") ==
                    yomk::payload::ResStatus::Succeeded)
                {
                    ++okC;
                }
            }
            elapsedC = msSince(t0);
        }
        recordBaseline("S1C_console_level_off_shortcut", N, elapsedC);
        YOMK_TEST_CHECK(okC == N, "S1C: 级别 OFF 短路仍 ResStatus::Succeeded 守恒 " + std::to_string(okC) + "/" + std::to_string(N));
        YOMK_TEST_CHECK(yomk::log_mgr().console().level(yomk::payload::LogLevel::Info).enable() == yomk::payload::ResStatus::Succeeded, "S1C: 恢复 ON info 返回 ResStatus::Succeeded");

        // ---- Phase D：proxy 短路（置于 console 末相：本测试安装后不卸载，靠恒置 true 穿透
        // 避免影响后续 Section；卸载语义由 Lifecycle S3 覆盖）----
        YOMK_TEST_CHECK(yomk::log_mgr().console().proxy(lg4Proxy).apply() == yomk::payload::ResStatus::Succeeded, "S1D: 安装计数 proxy 返回 ResStatus::Succeeded");
        g_proxyReturn.store(false);
        g_proxyHits.store(0);
        uint64_t okD = 0;
        double elapsedD = 0.0;
        {
            ThreadSafeCoutCapture cap(true);
            auto t0 = Clock::now();
            for (uint64_t i = 0; i < N; ++i)
            {
                if (yomk::log().tag(tags[static_cast<size_t>(i)]).info("lg4s1d_marker") ==
                    yomk::payload::ResStatus::Succeeded)
                {
                    ++okD;
                }
            }
            elapsedD = msSince(t0);
        }
        recordBaseline("S1D_console_proxy_shortcut", N, elapsedD);
        YOMK_TEST_CHECK(okD == N, "S1D: proxy 短路仍 ResStatus::Succeeded 守恒 " + std::to_string(okD) + "/" + std::to_string(N));
        YOMK_TEST_CHECK(
            g_proxyHits.load() == N,
            "S1D: proxy 命中守恒 " + std::to_string(g_proxyHits.load()) + "/" + std::to_string(N));
        // 恢复穿透态：此后 console 日志仍会真实输出（后续 Section 不校验其内容）
        g_proxyReturn.store(true);
        {
            std::string outThrough;
            {
                ThreadSafeCoutCapture cap;
                yomk::log().tag(hotTag).info("lg4s1d_through_marker");
                outThrough = cap.str();
            }
            YOMK_TEST_CHECK(
                outThrough.find("lg4s1d_through_marker") != std::string::npos,
                "S1D: proxy 穿透态恢复默认输出（P4-e 登记依据）");
        }

        // ---- Phase E：热 tag 重复写（ConsoleLogger::s_mutex 全实例同一把）----
        // std::map 在 N 条目下的命中查找开销
        uint64_t okE = 0;
        double elapsedE = 0.0;
        {
            ThreadSafeCoutCapture cap(true);
            auto t0 = Clock::now();
            for (uint64_t i = 0; i < N; ++i)
            {
                if (yomk::log().tag(hotTag).info("lg4s1e_marker") == yomk::payload::ResStatus::Succeeded)
                {
                    ++okE;
                }
            }
            elapsedE = msSince(t0);
        }
        recordBaseline("S1E_console_hot_tag", N, elapsedE);
        YOMK_TEST_CHECK(okE == N, "S1E: 热 tag 写 ResStatus::Succeeded 守恒 " + std::to_string(okE) + "/" + std::to_string(N));

        std::cout << "[BASELINE] S1_VmHWM:" << readVmHWM() << std::endl;
    }

    // ========================================================================
    // S2：file 侧激进规模（真实建目录 + 建 .log 文件，全程持 m_fileLoggersMutex 独占锁）
    // ========================================================================
    std::cout << "\n--- S2: file logger 激进规模（N = " << N << "） ---" << std::endl;
    uint64_t fileTotal = 0;
    {
        fs::create_directories(s2Dir, ec);
        fs::create_directories(s2Single, ec);

        // ---- Phase A：N 个 file logger 分片创建（目录由 FileLogger::init 自行创建）----
        uint64_t okA = 0;
        double elapsedA = 0.0;
        {
            ThreadSafeCoutCapture cap(true);
            auto t0 = Clock::now();
            for (uint64_t i = 0; i < N; ++i)
            {
                const std::string& dir = shardDirs[static_cast<size_t>(i / kShardSize)];
                if (yomk::log_mgr().dir(dir).file(makeName("lg4s2_fl_", i)).create() == yomk::payload::ResStatus::Succeeded)
                {
                    ++okA;
                }
            }
            elapsedA = msSince(t0);
        }
        recordBaseline("S2A_file_create_sharded", N, elapsedA);
        YOMK_TEST_CHECK(okA == N, "S2A: 分片创建 ResStatus::Succeeded 守恒 " + std::to_string(okA) + "/" + std::to_string(N));

        // ---- Phase B：单目录对照相（M 个 logger 全在同一目录）----
        // 与 Phase A 的每条均摊耗时对比，暴露单目录 inode 膨胀 / create_directories 退化
        uint64_t okB = 0;
        double elapsedB = 0.0;
        {
            ThreadSafeCoutCapture cap(true);
            auto t0 = Clock::now();
            for (uint64_t i = 0; i < M; ++i)
            {
                if (yomk::log_mgr().dir(s2Single.string()).file(makeName("lg4s2_single_", i)).create() == yomk::payload::ResStatus::Succeeded)
                {
                    ++okB;
                }
            }
            elapsedB = msSince(t0);
        }
        recordBaseline("S2B_file_create_singledir", M, elapsedB);
        YOMK_TEST_CHECK(okB == M, "S2B: 单目录创建 ResStatus::Succeeded 守恒 " + std::to_string(okB) + "/" + std::to_string(M));
        std::cout << "[BASELINE] S2_per_create_us: sharded " << std::fixed << std::setprecision(1)
                  << (elapsedA * 1000.0 / static_cast<double>(N)) << " us vs singledir "
                  << (elapsedB * 1000.0 / static_cast<double>(M)) << " us (" << shardCount << " shards vs 1 dir)"
                  << std::endl;

        // ---- Phase C：磁盘足迹与内省条目双守恒 ----
        const uint64_t diskFiles = countDiskFiles(s2Dir) + countDiskFiles(s2Single);
        YOMK_TEST_CHECK(
            diskFiles == N + M,
            "S2C: 磁盘 .log 文件数守恒 = N + N/10（实测 " + std::to_string(diskFiles) + "/" + std::to_string(N + M) +
                "）");

        const uint64_t c = unpackLines(yomk::log_mgr().console().list()).size();
        const uint64_t f = unpackLines(yomk::log_mgr().file().list()).size();
        YOMK_TEST_CHECK(
            (c == N + 1 && f == N + M),
            "S2C: 内省条目守恒 console=N+1 file=N+N/10（实测 " + std::to_string(c) + "/" + std::to_string(f) + "）");
        fileTotal = f;

        std::cout << "[BASELINE] S2_VmHWM:" << readVmHWM() << std::endl;
    }

    // ========================================================================
    // S3：文件路径写入与大缓冲落盘（write() 单次快照改写）
    // ========================================================================
    std::cout << "\n--- S3: 文件写入与大缓冲落盘（P4-a 验证点） ---" << std::endl;
    {
        // sink 复用 S2 Phase A 的前 K 个 file logger（均落在 shard_0）
        const std::string sinkDir = shardDirs.empty() ? s2Dir.string() : shardDirs[0];
        std::vector<std::string> sinks;
        sinks.reserve(static_cast<size_t>(K));
        for (uint64_t i = 0; i < K; ++i)
        {
            sinks.push_back(makeName("lg4s2_fl_", i));
        }

        // ---- Phase A：N 行分散写入 K 个 logger 的内存缓冲（不落盘）----
        // file 侧每行也过一次 localTimeFormatted()
        const uint64_t linesPerSink = N / K;
        const uint64_t totalLines = linesPerSink * K;
        uint64_t okA = 0;
        double elapsedA = 0.0;
        {
            ThreadSafeCoutCapture cap(true);
            auto t0 = Clock::now();
            for (uint64_t r = 0; r < linesPerSink; ++r)
            {
                for (uint64_t k = 0; k < K; ++k)
                {
                    if (yomk::log().to_sink(sinks[static_cast<size_t>(k)]).tag("lg4s3a").info("line_" + std::to_string(r)) ==
                        yomk::payload::ResStatus::Succeeded)
                    {
                        ++okA;
                    }
                }
            }
            elapsedA = msSince(t0);
        }
        recordBaseline("S3A_file_buffer_write", totalLines, elapsedA);
        YOMK_TEST_CHECK(okA == totalLines, "S3A: 缓冲写 ResStatus::Succeeded 守恒 " + std::to_string(okA) + "/" + std::to_string(totalLines));
        {
            bool openOk = false;
            const uint64_t before = countFileLines(fs::path(sinkDir) / (sinks[0] + ".log"), openOk);
            YOMK_TEST_CHECK((openOk && before == 0), "S3A: 未 flush 前 sink0 磁盘文件仍 0 行（缓冲语义）");
        }

        // ---- Phase B：单 logger 大缓冲一次落盘----
        const fs::path s3Dir = tmpDir / "s3";
        fs::create_directories(s3Dir, ec);
        const std::string bigName = "lg4s3_big";
        YOMK_TEST_CHECK(
            yomk::log_mgr().dir(s3Dir.string()).file(bigName).create() == yomk::payload::ResStatus::Succeeded, "S3B: 大缓冲 logger 创建 ResStatus::Succeeded");
        const fs::path bigPath = s3Dir / (bigName + ".log");
        const uint64_t bigLines = 2 * N;
        uint64_t okB = 0;
        double elapsedB = 0.0;
        {
            ThreadSafeCoutCapture cap(true);
            auto t0 = Clock::now();
            for (uint64_t i = 0; i < bigLines; ++i)
            {
                if (yomk::log().to_sink(bigName).tag("lg4s3b").info("line_" + std::to_string(i)) ==
                    yomk::payload::ResStatus::Succeeded)
                {
                    ++okB;
                }
            }
            elapsedB = msSince(t0);
        }
        recordBaseline("S3B_file_buffer_fill_big", bigLines, elapsedB);
        YOMK_TEST_CHECK(okB == bigLines, "S3B: 大缓冲填充 ResStatus::Succeeded 守恒 " + std::to_string(okB) + "/" + std::to_string(bigLines));

        auto flushBig = yomk::payload::ResStatus::Invalid;
        double elapsedFlush = 0.0;
        {
            ThreadSafeCoutCapture cap(true);
            auto t0 = Clock::now();
            flushBig = yomk::log_mgr().file(bigName).flush();
            elapsedFlush = msSince(t0);
        }
        YOMK_TEST_CHECK(flushBig == yomk::payload::ResStatus::Succeeded, "S3B: 大缓冲一次落盘 ResStatus::Succeeded");
        {
            std::error_code sizeEc;
            const uintmax_t bytes = fs::file_size(bigPath, sizeEc);
            const double mb = sizeEc ? 0.0 : static_cast<double>(bytes) / (1024.0 * 1024.0);
            const double mbPerSec = (elapsedFlush > 0.0) ? (mb / (elapsedFlush / 1000.0)) : 0.0;
            std::cout << "[BASELINE] S3B_flush_big: " << std::fixed << std::setprecision(1) << elapsedFlush << " ms / "
                      << std::setprecision(2) << mb << " MB / " << mbPerSec << " MB/s (" << bigLines << " lines)"
                      << std::endl;
        }
        {
            bool openOk = false;
            const uint64_t readBack = countFileLines(bigPath, openOk);
            YOMK_TEST_CHECK(
                (openOk && readBack == bigLines),
                "S3B: 大缓冲读回行数守恒 " + std::to_string(readBack) + "/" + std::to_string(bigLines));
        }

        // ---- Phase C：K 个 logger 逐个 flush，逐文件行数守恒 ----
        uint64_t okC = 0;
        double elapsedC = 0.0;
        {
            ThreadSafeCoutCapture cap(true);
            auto t0 = Clock::now();
            for (uint64_t k = 0; k < K; ++k)
            {
                if (yomk::log_mgr().file(sinks[static_cast<size_t>(k)]).flush() == yomk::payload::ResStatus::Succeeded)
                {
                    ++okC;
                }
            }
            elapsedC = msSince(t0);
        }
        recordBaseline("S3C_file_flush_k_loggers", K, elapsedC);
        YOMK_TEST_CHECK(okC == K, "S3C: K 个 logger flush ResStatus::Succeeded 守恒 " + std::to_string(okC) + "/" + std::to_string(K));
        {
            uint64_t sumLines = 0;
            uint64_t badFiles = 0;
            for (uint64_t k = 0; k < K; ++k)
            {
                bool openOk = false;
                const uint64_t n = countFileLines(fs::path(sinkDir) / (sinks[static_cast<size_t>(k)] + ".log"), openOk);
                if (!openOk || n != linesPerSink)
                {
                    ++badFiles;
                }
                sumLines += n;
            }
            YOMK_TEST_CHECK(badFiles == 0, "S3C: 逐 sink 文件行数 = N/K（异常文件 " + std::to_string(badFiles) + " 个）");
            YOMK_TEST_CHECK(
                sumLines == totalLines,
                "S3C: K 个文件行数总和守恒 " + std::to_string(sumLines) + "/" + std::to_string(totalLines));
        }

        // ---- Phase D：空缓冲重复 flush（既有语义：文件不增长，writeFileLog 仍 ResStatus::Succeeded）----
        std::error_code sizeEc;
        const uintmax_t sizeBefore = fs::file_size(bigPath, sizeEc);
        uint64_t okD = 0;
        double elapsedD = 0.0;
        {
            ThreadSafeCoutCapture cap(true);
            auto t0 = Clock::now();
            for (uint64_t i = 0; i < N; ++i)
            {
                if (yomk::log_mgr().file(bigName).flush() == yomk::payload::ResStatus::Succeeded)
                {
                    ++okD;
                }
            }
            elapsedD = msSince(t0);
        }
        recordBaseline("S3D_file_flush_empty_buffer", N, elapsedD);
        YOMK_TEST_CHECK(okD == N, "S3D: 空缓冲重复 flush 仍 ResStatus::Succeeded 守恒 " + std::to_string(okD) + "/" + std::to_string(N));
        const uintmax_t sizeAfter = fs::file_size(bigPath, sizeEc);
        YOMK_TEST_CHECK(
            (!sizeEc && sizeAfter == sizeBefore),
            "S3D: 空缓冲 flush 文件不增长（" + std::to_string(sizeAfter) + " == " + std::to_string(sizeBefore) + "）");

        std::cout << "[BASELINE] S3_VmHWM:" << readVmHWM() << std::endl;
    }

    // ========================================================================
    // S4：内省端点在 2N+ 条目下的响应（双锁快照窗口）
    // ========================================================================
    std::cout << "\n--- S4: 内省端点规模响应（P4-c 验证点） ---" << std::endl;
    {
        const std::vector<std::string> loggersLines = unpackLines(yomk::log_mgr().console().list());
        const uint64_t c = loggersLines.size();
        const uint64_t f = unpackLines(yomk::log_mgr().file().list()).size();
        const uint64_t entries = c + f;
        std::cout << "[CONFIG] S4 entries = " << entries << " (console " << c << " / file " << f
                  << ") rounds = " << introspectionRounds(entries) << std::endl;
        YOMK_TEST_CHECK(
            (c == N + 1 && f == fileTotal + 1),
            "S4: 全规模条目守恒 console=N+1 file=N+N/10+1（实测 " + std::to_string(c) + "/" + std::to_string(f) + "）");

        // ---- Phase A：全规模延迟（LOGGERS / ALL 各一轮扫描）----
        const uint64_t rounds = introspectionRounds(entries);
        uint64_t badL = 0;
        uint64_t linesL = 0;
        const LatencyStat statL = measureIntrospection(rounds, false, badL, linesL);
        recordLatency("S4A_loggers", statL);
        YOMK_TEST_CHECK(
            (badL == 0 && linesL == c),
            "S4A: LOGGERS ResStatus::Succeeded + console 行数守恒（" + std::to_string(linesL) + "/" + std::to_string(c) + "）");
        uint64_t badA = 0;
        uint64_t linesA = 0;
        const LatencyStat statA = measureIntrospection(rounds, true, badA, linesA);
        recordLatency("S4A_all", statA);
        YOMK_TEST_CHECK(
            (badA == 0 && linesA == entries),
            "S4A: ALL ResStatus::Succeeded + 日志器数守恒（" + std::to_string(linesA) + "/" + std::to_string(entries) + "）");

        // ---- Phase B：全局主闸状态 + ALL 条目逐器真值 + 与 LOGGERS 的数量/类型计数等价（原子快照可观测等价）----
        // 结构化后数据段形态不同（LoggerInfo vs 纯名行），逐行字符串比对改为数量与类型计数比对；
        // S4 阶段表内条目不再增删，console/file 两端点各自单锁快照后计数等价；
        // 条目改报逐器门控层真值后，"全 on" 验证语义迁移至主闸 status()（log_mgr().console().status()）
        auto gsPkg = yomk::msgs::unpack<yomk::msgs::LoggerState>(yomk::log_mgr().console().status().m_data);
        YOMK_TEST_CHECK(
            (gsPkg != nullptr && gsPkg->d.m_debugOn && gsPkg->d.m_infoOn && gsPkg->d.m_warnOn &&
             gsPkg->d.m_errorOn && gsPkg->d.m_proxyOn),
            "S4B: 全局主闸状态全 on（proxy 已在 S1D 安装）");
        auto listPkg = yomk::msgs::unpack<yomk::msgs::LoggerInfoList>(yomk::log_mgr().all().m_data);
        const yomk::payload::LoggerState* st = firstConsoleState(listPkg);
        YOMK_TEST_CHECK(
            (st != nullptr && st->m_debugOn && st->m_infoOn && st->m_warnOn && st->m_errorOn && st->m_proxyOn),
            "S4B: ALL 首条 console 条目逐器门控真值全 on（未做具名开关，含 proxyOn）");
        uint64_t ac = 0;
        uint64_t af = 0;
        if (listPkg)
        {
            for (const auto& li : listPkg->d)
            {
                if (li.m_type == "console")
                {
                    ++ac;
                }
                else
                {
                    ++af;
                }
            }
        }
        const bool identical =
            (listPkg != nullptr && listPkg->d.size() == entries && loggersLines.size() == c &&
             ac == c && af == f);
        YOMK_TEST_CHECK(identical, "S4B: ALL 与 LOGGERS 数量及类型计数一致（嵌套双锁原子快照）");

        // ---- Phase C：单查 N 次（偶数查 console 名、奇数查 file 名；两表查找热路径量化点）----
        // probes = N：单次采样进入十毫秒量级、高于计时分辨率，S4C 才是两张表查找
        // 热路径的可解读量化口径（容器选型依据见 YomkLogger.h 注释）：
        // loggerInfo 按类型单表精确查找（console/file 各自 shared_lock），偶奇交替恰覆盖两表热路径。
        // 名字域校验：偶数 i 的 lg4s1_tag_i 由 S1A 创建（i ∈ 0..N-1）、奇数 i 的 lg4s2_fl_i 由
        // S2A 创建（i ∈ 0..N-1），故 probes 提升到 N 后仍全部命中，YOMK_TEST_CHECK(okProbe == probes) 的
        // 守恒语义不变。口径变更（单查改带 type）须与基线同批采样才可比。
        const uint64_t probes = N;
        uint64_t okProbe = 0;
        double elapsedProbe = 0.0;
        {
            ThreadSafeCoutCapture cap(true);
            auto t0 = Clock::now();
            for (uint64_t i = 0; i < probes; ++i)
            {
                const std::string name = (i % 2 == 0) ? makeName("lg4s1_tag_", i) : makeName("lg4s2_fl_", i);
                if ((i % 2 == 0 ? yomk::log_mgr().console(name).get_info()
                                : yomk::log_mgr().file(name).get_info())
                        .m_status
                    == yomk::payload::ResStatus::Succeeded)
                {
                    ++okProbe;
                }
            }
            elapsedProbe = msSince(t0);
        }
        recordBaseline("S4C_logger_single_query", probes, elapsedProbe);
        YOMK_TEST_CHECK(okProbe == probes, "S4C: 单查 ResStatus::Succeeded 守恒 " + std::to_string(okProbe) + "/" + std::to_string(probes));

        // ---- Phase D：内省快照与 createFileLogger 独占锁相互阻塞量化 ----
        const fs::path s4Solo = tmpDir / "s4dsolo";
        const fs::path s4Conc = tmpDir / "s4d";
        fs::create_directories(s4Solo, ec);
        fs::create_directories(s4Conc, ec);

        // 对照相：无内省并发时 D 次 create 的均摊延迟
        uint64_t soloOk = 0;
        double soloMs = 0.0;
        {
            ThreadSafeCoutCapture cap(true);
            auto t0 = Clock::now();
            for (uint64_t i = 0; i < D; ++i)
            {
                if (yomk::log_mgr().dir(s4Solo.string()).file(makeName("lg4s4d_solo_", i)).create() == yomk::payload::ResStatus::Succeeded)
                {
                    ++soloOk;
                }
            }
            soloMs = msSince(t0);
        }
        YOMK_TEST_CHECK(soloOk == D, "S4D: 对照相 create ResStatus::Succeeded 守恒 " + std::to_string(soloOk) + "/" + std::to_string(D));

        g_s4dStop.store(false);
        g_s4dCreateOk.store(0);
        g_s4dCreateNs.store(0);
        g_s4dSnapCount.store(0);
        g_s4dSnapNonMono.store(0);
        g_s4dSnapBadStatus.store(0);
        std::thread snapThread(
            []()
            {
                uint64_t prevLines = 0;
                while (!g_s4dStop.load(std::memory_order_acquire))
                {
                    auto resp = yomk::log_mgr().all();
                    if (resp.m_status != yomk::payload::ResStatus::Succeeded)
                    {
                        g_s4dSnapBadStatus.fetch_add(1, std::memory_order_relaxed);
                        continue;
                    }
                    auto listPkg = yomk::msgs::unpack<yomk::msgs::LoggerInfoList>(resp.m_data);
                    const uint64_t lines = listPkg ? listPkg->d.size() : 0;
                    g_s4dSnapCount.fetch_add(1, std::memory_order_relaxed);
                    if (lines < prevLines)
                    {
                        g_s4dSnapNonMono.fetch_add(1, std::memory_order_relaxed);
                    }
                    prevLines = lines;
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                }
            });
        // 等首个快照落地再开始创建，保证两个窗口真实重叠（否则 create 可能先跑完）
        int waitMs = 0;
        while (g_s4dSnapCount.load(std::memory_order_acquire) == 0 && waitMs < 5000)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            ++waitMs;
        }
        double concMs = 0.0;
        {
            ThreadSafeCoutCapture cap(true);
            auto t0 = Clock::now();
            for (uint64_t i = 0; i < D; ++i)
            {
                const auto c0 = Clock::now();
                if (yomk::log_mgr().dir(s4Conc.string()).file(makeName("lg4s4d_fl_", i)).create() == yomk::payload::ResStatus::Succeeded)
                {
                    g_s4dCreateOk.fetch_add(1, std::memory_order_relaxed);
                }
                g_s4dCreateNs.fetch_add(
                    static_cast<uint64_t>(std::chrono::duration<double, std::nano>(Clock::now() - c0).count()),
                    std::memory_order_relaxed);
            }
            concMs = msSince(t0);
        }
        g_s4dStop.store(true, std::memory_order_release);
        snapThread.join();

        const double soloPerUs = soloMs * 1000.0 / static_cast<double>(D);
        const double concPerUs = (static_cast<double>(g_s4dCreateNs.load()) / 1000.0) / static_cast<double>(D);
        const double ratio = (soloPerUs > 0.0) ? (concPerUs / soloPerUs) : 0.0;
        std::cout << "[BASELINE] S4D_create_latency_us: solo " << std::fixed << std::setprecision(1) << soloPerUs
                  << " vs under_introspection " << concPerUs << " (ratio " << std::setprecision(2) << ratio
                  << ", snapshots " << g_s4dSnapCount.load() << ", wall " << std::setprecision(1) << concMs << " ms)"
                  << std::endl;
        YOMK_TEST_CHECK(
            g_s4dCreateOk.load() == D,
            "S4D: 并发相 create ResStatus::Succeeded 守恒 " + std::to_string(g_s4dCreateOk.load()) + "/" + std::to_string(D));
        YOMK_TEST_CHECK(
            g_s4dSnapBadStatus.load() == 0,
            "S4D: 内省快照无非 ResStatus::Succeeded（异常 " + std::to_string(g_s4dSnapBadStatus.load()) + " 次）");
        YOMK_TEST_CHECK(
            g_s4dSnapCount.load() > 0, "S4D: 内省线程确实产生快照（" + std::to_string(g_s4dSnapCount.load()) + " 次）");
        YOMK_TEST_CHECK(
            g_s4dSnapNonMono.load() == 0,
            "S4D: 快照条目数单调不减（P3-a 语义在规模下仍成立，违例 " + std::to_string(g_s4dSnapNonMono.load()) +
                " 次）");

        std::cout << "[BASELINE] S4_VmHWM:" << readVmHWM() << std::endl;
    }

    // ========================================================================
    // S5：8 线程混合负载 churn + 全量删除回收
    // ========================================================================
    std::cout << "\n--- S5: 多线程混合负载 + 全量回收 ---" << std::endl;
    {
        const fs::path s5Dir = tmpDir / "s5";
        fs::create_directories(s5Dir, ec);
        const std::string s5DirStr = s5Dir.string();

        // ---- Phase A：8 线程 churn（console 写 + file 建/写 + 内省 + 删除 + 删除后单查）----
        resetGate();
        double elapsedChurn = 0.0;
        {
            ThreadSafeCoutCapture cap(true);
            auto t0 = Clock::now();
            std::vector<std::thread> workers;
            workers.reserve(static_cast<size_t>(kThreads));
            for (uint64_t t = 0; t < kThreads; ++t)
            {
                workers.emplace_back(
                    [t, s5DirStr, churnRounds, introspectEvery]()
                    {
                        waitGate();
                        for (uint64_t r = 0; r < churnRounds; ++r)
                        {
                            const std::string name = "lg4s5_t" + std::to_string(t) + "_r" + std::to_string(r);

                            auto rc = yomk::log().tag(name).info("lg4s5_console");
                            if (rc == yomk::payload::ResStatus::Succeeded)
                                g_s5ConsoleOk.fetch_add(1, std::memory_order_relaxed);
                            else if (!isLegalStatus(rc))
                                g_s5BadStatus.fetch_add(1, std::memory_order_relaxed);

                            auto rf = yomk::log_mgr().dir(s5DirStr).file(name).create();
                            if (rf == yomk::payload::ResStatus::Succeeded)
                                g_s5FileOk.fetch_add(1, std::memory_order_relaxed);
                            else if (!isLegalStatus(rf))
                                g_s5BadStatus.fetch_add(1, std::memory_order_relaxed);

                            auto rl = yomk::log().to_sink(name).tag("lg4s5").info("file_line");
                            if (rl == yomk::payload::ResStatus::Succeeded)
                                g_s5LogOk.fetch_add(1, std::memory_order_relaxed);
                            else if (!isLegalStatus(rl))
                                g_s5BadStatus.fetch_add(1, std::memory_order_relaxed);

                            if ((r % introspectEvery) == 0)
                            {
                                auto ri = yomk::log_mgr().all();
                                if (ri.m_status == yomk::payload::ResStatus::Succeeded)
                                    g_s5IntrospectOk.fetch_add(1, std::memory_order_relaxed);
                                else if (!isLegalStatus(ri))
                                    g_s5BadStatus.fetch_add(1, std::memory_order_relaxed);
                            }

                            // 同名同时命中两表：单端点一次删除 console + file
                            auto rd = yomk::log_mgr().file(name).remove();
                            if (rd == yomk::payload::ResStatus::Succeeded)
                                g_s5DeleteOk.fetch_add(1, std::memory_order_relaxed);
                            else if (!isLegalStatus(rd))
                                g_s5BadStatus.fetch_add(1, std::memory_order_relaxed);

                            auto rn = yomk::log_mgr().console(name).get_info();
                            if (rn.m_status == yomk::payload::ResStatus::Failed)
                                g_s5PostDeleteNo.fetch_add(1, std::memory_order_relaxed);
                            else if (!isLegalStatus(rn))
                                g_s5BadStatus.fetch_add(1, std::memory_order_relaxed);
                        }
                    });
            }
            releaseGate(static_cast<int>(kThreads));
            for (auto& worker : workers)
            {
                worker.join();
            }
            elapsedChurn = msSince(t0);
        }
        const uint64_t introspectTotal = kThreads * introspectPerThread;
        recordBaseline("S5A_mixed_churn", churnTotal * 5 + introspectTotal, elapsedChurn);
        YOMK_TEST_CHECK(
            g_s5ConsoleOk.load() == churnTotal,
            "S5A: console 写 ResStatus::Succeeded 守恒 " + std::to_string(g_s5ConsoleOk.load()) + "/" + std::to_string(churnTotal));
        YOMK_TEST_CHECK(
            g_s5FileOk.load() == churnTotal,
            "S5A: file 建 ResStatus::Succeeded 守恒 " + std::to_string(g_s5FileOk.load()) + "/" + std::to_string(churnTotal));
        YOMK_TEST_CHECK(
            g_s5LogOk.load() == churnTotal,
            "S5A: file 写 ResStatus::Succeeded 守恒 " + std::to_string(g_s5LogOk.load()) + "/" + std::to_string(churnTotal));
        YOMK_TEST_CHECK(
            g_s5IntrospectOk.load() == introspectTotal,
            "S5A: 内省 ResStatus::Succeeded 守恒 " + std::to_string(g_s5IntrospectOk.load()) + "/" + std::to_string(introspectTotal));
        YOMK_TEST_CHECK(
            g_s5DeleteOk.load() == churnTotal,
            "S5A: 删除 ResStatus::Succeeded 守恒 " + std::to_string(g_s5DeleteOk.load()) + "/" + std::to_string(churnTotal));
        YOMK_TEST_CHECK(
            g_s5PostDeleteNo.load() == churnTotal,
            "S5A: 删除后单查 ResStatus::Failed 守恒 " + std::to_string(g_s5PostDeleteNo.load()) + "/" + std::to_string(churnTotal));
        YOMK_TEST_CHECK(g_s5BadStatus.load() == 0, "S5A: 无非法状态码（异常 " + std::to_string(g_s5BadStatus.load()) + " 次）");
        {
            const uint64_t c = unpackLines(yomk::log_mgr().console().list()).size();
            const uint64_t f = unpackLines(yomk::log_mgr().file().list()).size();
            YOMK_TEST_CHECK(
                (c == N + 1 && f == fileTotal + 1 + 2 * D),
                "S5A: churn 后条目净变化 0（实测 console " + std::to_string(c) + " / file " + std::to_string(f) + "）");
        }

        // ---- Phase B：全量回收 S1/S2/S3/S4 遗留的所有 logger ----
        uint64_t delConsoleOk = 0;
        uint64_t delFileOk = 0;
        uint64_t delBad = 0;
        double elapsedDel = 0.0;
        {
            ThreadSafeCoutCapture cap(true);
            auto t0 = Clock::now();
            auto recycle = [&delConsoleOk, &delFileOk, &delBad](const std::string& name, bool expectFile)
            {
                auto r = yomk::log_mgr().file(name).remove();
                if (r == yomk::payload::ResStatus::Succeeded)
                {
                    if (expectFile)
                        ++delFileOk;
                    else
                        ++delConsoleOk;
                }
                else
                {
                    ++delBad;
                }
            };
            for (uint64_t i = 0; i < N; ++i)
            {
                recycle(makeName("lg4s1_tag_", i), false);
            }
            for (uint64_t i = 0; i < N; ++i)
            {
                recycle(makeName("lg4s2_fl_", i), true);
            }
            for (uint64_t i = 0; i < M; ++i)
            {
                recycle(makeName("lg4s2_single_", i), true);
            }
            recycle("lg4s3_big", true);
            for (uint64_t i = 0; i < D; ++i)
            {
                recycle(makeName("lg4s4d_solo_", i), true);
                recycle(makeName("lg4s4d_fl_", i), true);
            }
            elapsedDel = msSince(t0);
        }
        recordBaseline("S5B_delete_all", N + N + M + 1 + 2 * D, elapsedDel);
        YOMK_TEST_CHECK(
            delConsoleOk == N,
            "S5B: console 组回收 ResStatus::Succeeded 守恒 " + std::to_string(delConsoleOk) + "/" + std::to_string(N));
        YOMK_TEST_CHECK(
            delFileOk == N + M + 1 + 2 * D,
            "S5B: file 组回收 ResStatus::Succeeded 守恒 " + std::to_string(delFileOk) + "/" + std::to_string(N + M + 1 + 2 * D));
        YOMK_TEST_CHECK(delBad == 0, "S5B: 全量回收无非 ResStatus::Succeeded（异常 " + std::to_string(delBad) + " 次）");

        // ---- Phase C：残量扫除（main）→ LOGGERS 行数为 0 ----
        uint64_t residualDeleted = 0;
        {
            ThreadSafeCoutCapture cap(true);
            for (int round = 0; round < 3; ++round)
            {
                // 行内容即 logger 纯名，按类型端点各扫一份
                const std::vector<std::string> consoleLines = unpackLines(yomk::log_mgr().console().list());
                const std::vector<std::string> fileLines = unpackLines(yomk::log_mgr().file().list());
                if (consoleLines.empty() && fileLines.empty())
                {
                    break;
                }
                for (const auto& name : consoleLines)
                {
                    if (yomk::log_mgr().file(name).remove() == yomk::payload::ResStatus::Succeeded)
                    {
                        ++residualDeleted;
                    }
                }
                for (const auto& name : fileLines)
                {
                    if (yomk::log_mgr().file(name).remove() == yomk::payload::ResStatus::Succeeded)
                    {
                        ++residualDeleted;
                    }
                }
            }
        }
        const size_t finalLeft = unpackLines(yomk::log_mgr().console().list()).size() +
                                 unpackLines(yomk::log_mgr().file().list()).size();
        YOMK_TEST_CHECK(
            finalLeft == 0,
            "S5C: 全量回收后 LOGGERS 行数为 0（残量扫除 " + std::to_string(residualDeleted) + " 个，实剩 " +
                std::to_string(finalLeft) + "）");
        {
            const uint64_t probes = (N / 100 == 0) ? 1 : N / 100;
            uint64_t goneConsole = 0;
            uint64_t goneFile = 0;
            {
                ThreadSafeCoutCapture cap(true);
                for (uint64_t i = 0; i < probes; ++i)
                {
                    if (yomk::log_mgr().console(makeName("lg4s1_tag_", i)).get_info().m_status == yomk::payload::ResStatus::Failed)
                    {
                        ++goneConsole;
                    }
                    if (yomk::log_mgr().file(makeName("lg4s2_fl_", i)).get_info().m_status == yomk::payload::ResStatus::Failed)
                    {
                        ++goneFile;
                    }
                }
            }
            YOMK_TEST_CHECK(
                (goneConsole == probes && goneFile == probes),
                "S5C: 抽样单查全部 ResStatus::Failed（console " + std::to_string(goneConsole) + " / file " +
                    std::to_string(goneFile) + "，共 " + std::to_string(probes) + " 组）");
        }

        std::cout << "[BASELINE] S5_VmHWM:" << readVmHWM() << std::endl;
    }

    // ========================================================================
    // S6：收尾与磁盘清理
    // ========================================================================
    std::cout << "\n--- S6: 收尾与清理 ---" << std::endl;
    {
        // 清理前磁盘足迹守恒：churn/S4D 的 .log 仍在（删除接口不删磁盘文件，清理归调用方）
        const uint64_t expectDisk = N + M + 1 + 2 * D + churnTotal;
        const uint64_t disk = countDiskFiles(tmpDir);
        YOMK_TEST_CHECK(
            disk == expectDisk,
            "S6: 清理前磁盘 .log 文件数守恒（实测 " + std::to_string(disk) + "/" + std::to_string(expectDisk) + "）");

        double elapsedShutdown = 0.0;
        {
            ThreadSafeCoutCapture cap(true);
            auto t0 = Clock::now();
            yomk::server().shutdown();
            elapsedShutdown = msSince(t0);
        }
        std::cout << "[BASELINE] S6_shutdown: " << std::fixed << std::setprecision(1) << elapsedShutdown
                  << " ms（规模已在 S5 回收，析构无落盘 I/O 拖累）" << std::endl;

        std::error_code rmEc;
        const uintmax_t removed = fs::remove_all(tmpDir, rmEc);
        YOMK_TEST_CHECK(!rmEc, "S6: remove_all 无错误码（" + rmEc.message() + "）");
        std::error_code existEc;
        YOMK_TEST_CHECK(!fs::exists(tmpDir, existEc), "S6: 临时目录已彻底清理（删除条目 " + std::to_string(removed) + "）");

        std::cout << "[BASELINE] S6_VmHWM:" << readVmHWM() << std::endl;
    }

    std::cout << "\n=== Result: " << (g_total - g_failed) << "/" << g_total << " passed, " << g_failed
              << " failed ===" << std::endl;
    return g_failed == 0 ? 0 : 1;
}
