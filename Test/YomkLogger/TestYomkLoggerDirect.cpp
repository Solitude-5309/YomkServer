/**
 * @file TestYomkLoggerDirect.cpp
 * @brief Logger 模块 ConsoleLogger/FileLogger 类直连白盒测试（LG1 新建，LG2 回填）
 *
 * 覆盖内容（2 个类，14 组用例 26 断言）：
 * ConsoleLogger：
 * 1. 默认名 "MainLogger"、setName/getName
 * 2. 四级别输出（级别标签 [Debug]/[Info ]/[Warn ]/[Error] + [名] + 内容）
 * 3. 非法级别 default 降级（P2-c 修复后回填）：log(ELogLevel(99)) →
 *    "Unknown log level: 99" 提示 + 降级 Info 输出
 * 4. 时间戳格式 [YYYY-MM-DD HH:MM:SS.mmm]
 * 5. 超长名（4096）与空内容
 * FileLogger：
 * 6. 默认名、setName/setDir/getDir
 * 7. init 建多级目录 + 空日志文件，返回 true（P2-b 修复后 bool 语义）
 * 8. log 四级别入缓冲（write 前不落盘）；非法级别 default 降级 Info 入缓冲（P2-c 回填）
 * 9. write 追加落盘后清空流（二次 write 文件不增长）
 * 10. 空流 write 不触碰文件；空内容 log 仍成行；1MB 超长内容往返
 * 11. 析构自动 write（缓冲落盘）
 * 12. write 打不开文件分支（dir 为普通文件路径）：错误提示 + 流保留不丢，
 *     setDir 恢复有效目录后补写成功
 * 13. P2-b 处置验证（LG2 回填）：init 对不可创建目录（/proc 下嵌套路径）
 *     返回 false 不抛异常，错误提示含 fs error 信息；dir 为普通文件时
 *     走 ofstream 打开失败分支（false + create log file failed）
 *
 * 说明：ConsoleLogger/FileLogger 为库内部实现类，符号已从 libYomkServer.so 导出
 *       （nm -D 核实），测试侧经源码模块 include 路径直连构造，不经框架单例
 *       （无 YOMK_INIT，两自包含类不依赖服务器）。cout 断言用"唯一 marker +
 *       rdbuf 捕获"。异常安全说明：两类无裸资源（string/stringstream/mutex
 *       自管理），bad_alloc 注入书面豁免（见 LG1 计划第 1 节）。
 *
 * LG1 审计登记 → LG2 处置状态（本文件断言已同步回填）：
 * P1-c（log 内 std::localtime → 已改 localtime_r/_WIN32 localtime_s，时间戳格式断言不变）、
 * P2-b（init fs 异常穿透 → 已改 bool 返回 + 异常捕获，用例 13 回填）、
 * P2-c（ELogLevel 无固定底层类型 → 三处枚举已加 : int，99 为合法值，
 * 两类 log() 的 default 降级分支转活，用例 3/8 回填注入断言）。
 *
 * 风格：纯 main() + 失败计数，返回非 0 表示存在失败用例（零第三方依赖）
 */

#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "ConsoleLogger.h"
#include "FileLogger.h"

namespace fs = std::filesystem;

static int g_failed = 0;

#define CHECK(cond, msg)                                                          \
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

// RAII 捕获 std::cout：构造换 rdbuf，析构还原（对库侧 std::cout 同样生效）
class CoutCapture
{
public:
    CoutCapture() : m_old(std::cout.rdbuf(m_ss.rdbuf())) {}
    ~CoutCapture() { std::cout.rdbuf(m_old); }
    std::string str() const { return m_ss.str(); }

private:
    std::stringstream m_ss;
    std::streambuf* m_old;
};

// 读取文件全部内容；打开失败以 ok 出参标记
static std::string readFile(const fs::path& path, bool& ok)
{
    std::ifstream ifs(path);
    ok = ifs.is_open();
    std::stringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

// 时间戳格式粗校验："[dddd-dd-dd dd:dd:dd.ddd]"（不锁定具体日期值）
static bool checkTimeFormat(const std::string& line)
{
    auto isDigit = [](char c) { return c >= '0' && c <= '9'; };
    if (line.size() < 25 || line[0] != '[' || line[24] != ']')
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
            default:
                if (!isDigit(line[i]))
                    return false;
                break;
        }
    }
    return true;
}

int main()
{
    fs::path tmpDir = fs::temp_directory_path() / ("yomk_logger_direct_lg1_" + std::to_string(::getpid()));
    std::error_code ec;
    fs::create_directories(tmpDir, ec);
    CHECK(!ec, "临时目录创建成功: " + tmpDir.string());

    // ============ ConsoleLogger 直连 ============
    {
        ConsoleLogger logger;
        CHECK(logger.getName() == "MainLogger", "ConsoleLogger 默认名 MainLogger");
        logger.setName("lg1_direct_console");
        CHECK(logger.getName() == "lg1_direct_console", "setName/getName 往返一致");

        // 四级别输出：级别标签 + [名] + 内容
        {
            CoutCapture cap;
            logger.log(ConsoleLogger::eDebug, "lg1_dc_debug_marker");
            logger.log(ConsoleLogger::eInfo, "lg1_dc_info_marker");
            logger.log(ConsoleLogger::eWarn, "lg1_dc_warn_marker");
            logger.log(ConsoleLogger::eError, "lg1_dc_error_marker");
            std::string out = cap.str();
            CHECK(
                out.find("[Debug] [lg1_direct_console] lg1_dc_debug_marker") != std::string::npos,
                "eDebug 输出行格式（[Debug] [名] 内容）");
            CHECK(out.find("[Info ] [lg1_direct_console] lg1_dc_info_marker") != std::string::npos, "eInfo 输出行格式");
            CHECK(out.find("[Warn ] [lg1_direct_console] lg1_dc_warn_marker") != std::string::npos, "eWarn 输出行格式");
            CHECK(
                out.find("[Error] [lg1_direct_console] lg1_dc_error_marker") != std::string::npos, "eError 输出行格式");
            CHECK(checkTimeFormat(out), "控制台行首时间戳格式 [YYYY-MM-DD HH:MM:SS.mmm]");
        }

        // 非法级别 default 降级（P2-c 修复后回填）：枚举已加 : int，99 合法非 UB；
        // YOMK_ERR_POS_LOG 直写 cout 可被捕获（CHECK 须在捕获作用域外）
        {
            std::string out;
            {
                CoutCapture cap;
                logger.log(static_cast<ConsoleLogger::ELogLevel>(99), "lg1_dc_badlevel_marker");
                out = cap.str();
            }
            CHECK(
                out.find("Unknown log level: 99") != std::string::npos,
                "非法级别 99 走 default 错误提示（P2-c 修复后活分支）");
            CHECK(
                out.find("[Info ] [lg1_direct_console] lg1_dc_badlevel_marker") != std::string::npos,
                "非法级别降级 Info 输出 marker");
        }

        // 超长名（4096）与空内容
        {
            ConsoleLogger longLogger;
            std::string longName(4096, 'L');
            longLogger.setName(longName);
            CHECK(longLogger.getName() == longName, "4096 字符超长名 setName/getName 一致");
            CoutCapture cap;
            longLogger.log(ConsoleLogger::eInfo, "");
            CHECK(cap.str().find("[Info ] [" + longName + "] ") != std::string::npos, "超长名 + 空内容日志正常输出");
        }
    }

    // ============ FileLogger 直连 ============
    {
        // 默认名与 setter/getter
        FileLogger fl;
        CHECK(fl.getName() == "MainLogger", "FileLogger 默认名 MainLogger");
        fl.setName("lg1_direct_file");
        fl.setDir((tmpDir / "fl_basic").string());
        CHECK(
            fl.getName() == "lg1_direct_file" && fl.getDir() == (tmpDir / "fl_basic").string(),
            "setName/setDir/getDir 往返一致");

        // init：多级目录 + 空日志文件，返回 true（P2-b 修复后 bool 语义）
        CHECK(fl.init(), "init 成功返回 true（P2-b 修复后 bool 语义）");
        fs::path logFile = tmpDir / "fl_basic" / "lg1_direct_file.log";
        CHECK(
            fs::exists(tmpDir / "fl_basic") && fs::exists(logFile) && fs::file_size(logFile, ec) == 0,
            "init 建多级目录与空日志文件");

        // log 四级别：入缓冲不落盘
        fl.log(FileLogger::eDebug, "lg1_df_debug_marker");
        fl.log(FileLogger::eInfo, "lg1_df_info_marker");
        fl.log(FileLogger::eWarn, "lg1_df_warn_marker");
        fl.log(FileLogger::eError, "lg1_df_error_marker");
        // 非法级别 default 降级 Info 入缓冲（P2-c 修复后回填，随下方 write 一并落盘；
        // CHECK 须在捕获作用域外，否则断言输出被 rdbuf 重定向吞没）
        {
            std::string outBad;
            {
                CoutCapture cap;
                fl.log(static_cast<FileLogger::ELogLevel>(99), "lg1_df_badlevel_marker");
                outBad = cap.str();
            }
            CHECK(
                outBad.find("Unknown log level: 99") != std::string::npos,
                "FileLogger 非法级别 99 走 default 错误提示（P2-c 修复后活分支）");
        }
        CHECK(fs::file_size(logFile, ec) == 0, "write 前缓冲内容不落盘（文件仍空）");

        // write：5 行落盘（四级别各一 + 非法级别降级 Info 一行），行格式与时间戳
        fl.write();
        bool readOk = false;
        std::string content = readFile(logFile, readOk);
        CHECK(readOk, "write 后日志文件可读");
        CHECK(
            (content.find("[Debug] lg1_df_debug_marker") != std::string::npos &&
             content.find("[Info ] lg1_df_info_marker") != std::string::npos &&
             content.find("[Warn ] lg1_df_warn_marker") != std::string::npos &&
             content.find("[Error] lg1_df_error_marker") != std::string::npos),
            "四级别行格式（时间戳+[级别]+内容）落盘");
        CHECK(
            content.find("[Info ] lg1_df_badlevel_marker") != std::string::npos,
            "非法级别降级 Info 行落盘（P2-c 回填）");
        CHECK(checkTimeFormat(content), "文件日志行首时间戳格式");

        // 流已清空：二次 write 文件不增长
        auto sizeAfter = fs::file_size(logFile, ec);
        fl.write();
        CHECK(fs::file_size(logFile, ec) == sizeAfter, "二次 write 无重复内容（缓冲已清空）");

        // 空内容 log 仍成行（追加 1 行）
        fl.log(FileLogger::eInfo, "");
        fl.write();
        bool readOk2 = false;
        std::string content2 = readFile(logFile, readOk2);
        CHECK(
            readOk2 && content2.size() > sizeAfter && content2.substr(sizeAfter).find("[Info ] ") != std::string::npos,
            "空内容日志追加成行（[Info ] 空内容）");

        // 1MB 超长内容往返
        {
            std::string big(1024 * 1024, 'B');
            fl.log(FileLogger::eInfo, big);
            fl.write();
            bool readOk3 = false;
            std::string content3 = readFile(logFile, readOk3);
            CHECK(readOk3 && content3.find(big) != std::string::npos, "1MB 超长内容完整落盘");
        }

        // write 打不开文件分支：dir 指向普通文件路径 → 错误提示 + 流保留不丢
        {
            fs::path regularFile = tmpDir / "fl_regular";
            std::ofstream(regularFile.string()) << "x";
            FileLogger failFl;
            failFl.setName("lg1_writefail");
            failFl.setDir(regularFile.string());  // <普通文件>/lg1_writefail.log 无法打开
            failFl.log(FileLogger::eInfo, "lg1_df_writefail_marker");
            // CHECK 须在捕获作用域外（否则断言输出被 rdbuf 重定向吞没）
            std::string outFail;
            {
                CoutCapture cap;
                failFl.write();
                outFail = cap.str();
            }
            CHECK(
                outFail.find("open log file failed: " + regularFile.string() + "/lg1_writefail.log") !=
                    std::string::npos,
                "write 打不开文件走错误提示分支");
            // 流保留：恢复有效目录后补写成功（内容不丢）
            fs::path recoverDir = tmpDir / "fl_recover";
            fs::create_directories(recoverDir, ec);
            failFl.setDir(recoverDir.string());
            failFl.write();
            bool readOk4 = false;
            std::string content4 = readFile(recoverDir / "lg1_writefail.log", readOk4);
            CHECK(
                readOk4 && content4.find("lg1_df_writefail_marker") != std::string::npos,
                "write 失败后流保留，恢复目录补写内容不丢");
        }

        // 析构自动 write：缓冲内容落盘
        {
            fs::path dtorDir = tmpDir / "fl_dtor";
            fs::create_directories(dtorDir, ec);
            {
                FileLogger dtorFl;
                dtorFl.setName("lg1_dtor");
                dtorFl.setDir(dtorDir.string());
                dtorFl.init();
                dtorFl.log(FileLogger::eInfo, "lg1_df_dtor_marker");
                // 不显式 write，作用域结束析构触发
            }
            bool readOk5 = false;
            std::string content5 = readFile(dtorDir / "lg1_dtor.log", readOk5);
            CHECK(readOk5 && content5.find("lg1_df_dtor_marker") != std::string::npos, "析构自动 write，缓冲内容落盘");
        }

        // P2-b 处置验证（LG2 回填）：init 对不可创建目录返回 false 不抛异常
        // /proc 下嵌套新目录：非 root create_directories 必抛 fs_error（EACCES/EPERM），
        // 修复后被 init 内部捕获并转 false + 错误提示（CHECK 在捕获作用域外）
        {
            FileLogger badFl;
            badFl.setName("lg1_badinit");
            badFl.setDir("/proc/lg1_impossible_dir/sub");
            bool initRet = true;
            std::string out;
            {
                CoutCapture cap;
                initRet = badFl.init();
                out = cap.str();
            }
            CHECK(!initRet, "init 不可创建目录返回 false（P2-b 已处置，异常不穿透）");
            CHECK(out.find("init file logger fs error") != std::string::npos, "init 失败错误提示含 fs error 信息");
        }

        // P2-b 第二条失败路径：dir 为普通文件（exists 为 true 不走 create_directories，
        // 但 ofstream 打开 <普通文件>/<名>.log 必失败 ENOTDIR）→ false + create log file failed
        {
            fs::path regularDir = tmpDir / "fl_init_regular";
            std::ofstream(regularDir.string()) << "x";
            FileLogger regFl;
            regFl.setName("lg1_initfail");
            regFl.setDir(regularDir.string());
            bool initRet = true;
            std::string out;
            {
                CoutCapture cap;
                initRet = regFl.init();
                out = cap.str();
            }
            CHECK(!initRet, "init 目录为普通文件时返回 false（ofstream 打开失败分支）");
            CHECK(
                out.find("create log file failed: " + regularDir.string() + "/lg1_initfail.log") != std::string::npos,
                "init 打开失败错误提示含完整日志文件路径");
        }
    }

    // ============ 收尾清理 ============
    fs::remove_all(tmpDir, ec);

    if (g_failed == 0)
    {
        std::cout << "TestYomkLoggerDirect all check passed." << std::endl;
        return 0;
    }
    std::cout << "TestYomkLoggerDirect FAILED (" << g_failed << " checks failed)." << std::endl;
    return 1;
}
