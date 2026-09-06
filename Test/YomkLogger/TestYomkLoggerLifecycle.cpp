/**
 * @file TestYomkLoggerLifecycle.cpp
 * @brief Logger 模块生命周期与契约基线测试（LG1 从零新建，LG2 审计处置后回填，LG4 补删除接口）
 *
 * 覆盖内容（8 个 Section + 1 个前置注入段，113 断言）：
 * 1. 控制台日志基线：四级别宏输出格式（时间戳+[级别]+[MainLogger:行号] tag）；
 *    级别开关 OFF→无输出仍 eOk→ON→恢复输出，四级别全覆盖
 * 2. 自定义 tag：新 tag 自动创建 console logger；65536 超长 tag；空内容日志
 * 2.5 非法级别 default 降级（P2-c 修复后）：proxy 设置前 raw 注入 level=99 →
 *    console_log default 降级 Info 输出 + eOk（S3 后 proxy 拦截一切 console_log，
 *    该 default 分支仅在此窗口可触达）
 * 3. proxy 契约：SET_CONSOLE_LOG_PROXY 拦截（false→eOk+"console log proxy is success."+无输出）
 *    与穿透（true→默认输出）；原子计数验证；all 内省 proxy:on
 * 4. 文件日志闭环：CREATE 建空文件→四级别入缓冲（write 前文件仍空）→WRITE 落盘读回断言
 *    行数与行格式（含 FILE_INFO_TAG 自定义 tag 变体）；重复创建 eNo；未创建 logger 的
 *    FILE_LOG/WRITE eNo；P2-a/P2-b 处置验证（空名/空 dir eInvalid、不可建 dir eNo
 *    且不注册幽灵 logger、失败后同名重试成功）
 * 5. 内省三端点：LOGGERS 列表行格式 / LOGGER(name) console/file 两格式与未注册 eNo /
 *    ALL 首行 consoleLevelLine 精确格式与开关翻转联动
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
 *    空 logger 名回退路径——YOMK_INFO 宏的 logger 名是 "MainLogger:行号" 而非裸 MainLogger）；
 *    delete_logger 空包/错类型包契约分支
 *
 * 说明：YomkLogger 是 YomkService 子类（构造绑定 server->weak_from_this()），无法脱离
 *       YOMK_INIT 单例白盒直连；全部经 API 宏/raw request 测试，同时覆盖 invoke 路由链，
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
 *   （S2.5/S6 回填注入断言；console_log 的 default 因 proxy 不可撤销仅能在 S3 前触达）
 *
 * 风格：纯 main() + 失败计数，返回非 0 表示存在失败用例（零第三方依赖）
 */

#include <atomic>
#include <climits>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include <unistd.h>

#include "YomkAPI.h"

namespace fs = std::filesystem;

static int g_failed = 0;

#define CHECK(cond, msg)                                                                \
      do                                                                                \
      {                                                                                 \
            if (!(cond))                                                                \
            {                                                                           \
                  std::cout << "[FAIL] [line " << __LINE__ << "] " << msg << std::endl; \
                  ++g_failed;                                                           \
            }                                                                           \
            else                                                                        \
            {                                                                           \
                  std::cout << "[ OK ] [line " << __LINE__ << "] " << msg << std::endl; \
            }                                                                           \
      } while (0)

// ---- 文件级观测装置（TSan-clean 既有模式）----
static std::atomic<long> g_proxyHits{0};          // proxy 被调用次数
static std::atomic<bool> g_proxyIntercept{false}; // true：proxy 返回 false 拦截；false：返回 true 穿透

// RAII 捕获 std::cout：构造换 rdbuf，析构还原（同进程内对库侧 std::cout 同样生效）
class CoutCapture
{
public:
      CoutCapture() : m_old(std::cout.rdbuf(m_ss.rdbuf())) {}
      ~CoutCapture() { std::cout.rdbuf(m_old); }
      std::string str() const { return m_ss.str(); }

private:
      std::stringstream m_ss;
      std::streambuf *m_old;
};

// 读取文件全部内容；打开失败返回 nullopt 语义（以 bool 出参区分）
static std::string readFile(const fs::path &path, bool &ok)
{
      std::ifstream ifs(path);
      ok = ifs.is_open();
      std::stringstream ss;
      ss << ifs.rdbuf();
      return ss.str();
}

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

// 解包 StringArray 内省响应为字符串向量（空包/错类型返回空向量）
static std::vector<std::string> unpackLines(const YomkResponse &resp)
{
      std::vector<std::string> lines;
      YomkUnPackPkg(resp.m_data, StringArray, arr);
      if (arr != nullptr)
      {
            lines = arr->d;
      }
      return lines;
}

// 时间戳格式粗校验："dddd-dd-dd dd:dd:dd.ddd]"（不锁定具体日期值）
static bool checkTimeFormat(const std::string &line)
{
      auto isDigit = [](char c)
      { return c >= '0' && c <= '9'; };
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

int main()
{
      auto server = YOMK_INIT(1);
      CHECK(server != nullptr, "YOMK_INIT 返回非空服务器");

      // 临时目录（收尾 remove_all）
      fs::path tmpDir = fs::temp_directory_path() / ("yomk_logger_lg1_" + std::to_string(::getpid()));
      std::error_code ec;
      fs::create_directories(tmpDir, ec);
      CHECK(!ec, "临时目录创建成功: " + tmpDir.string());

      // ============ Section 1: 控制台日志基线（四级别 + 开关） ============
      {
            // 四级别各一发（YOMK_* 宏 tag 为 "MainLogger:行号"），marker 唯一化断言
            {
                  CoutCapture cap;
                  auto r = YOMK_INFO("lg1_s1_info_marker ", 42);
                  CHECK(r.m_status == YomkResponse::eOk, "YOMK_INFO 返回 eOk");
                  CHECK(r.m_msg == "success.", "YOMK_INFO 契约消息 success.");
                  std::string out = cap.str();
                  CHECK(out.find("lg1_s1_info_marker 42") != std::string::npos, "INFO 输出含 marker 与整数变参");
                  CHECK(out.find("[Info ] [MainLogger:") != std::string::npos, "INFO 输出格式 [Info ] [MainLogger:行号]");
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
                  CHECK(fmtOk, "INFO 输出行首时间戳格式 [YYYY-MM-DD HH:MM:SS.mmm]");
            }
            {
                  CoutCapture cap;
                  CHECK(YOMK_WARN("lg1_s1_warn_marker").m_status == YomkResponse::eOk, "YOMK_WARN 返回 eOk");
                  CHECK((cap.str().find("[Warn ] [MainLogger:") != std::string::npos &&
                         cap.str().find("lg1_s1_warn_marker") != std::string::npos),
                        "WARN 输出格式与 marker");
            }
            {
                  CoutCapture cap;
                  CHECK(YOMK_ERROR("lg1_s1_error_marker").m_status == YomkResponse::eOk, "YOMK_ERROR 返回 eOk");
                  CHECK((cap.str().find("[Error] [MainLogger:") != std::string::npos &&
                         cap.str().find("lg1_s1_error_marker") != std::string::npos),
                        "ERROR 输出格式与 marker");
            }
            {
                  CoutCapture cap;
                  CHECK(YOMK_DEBUG("lg1_s1_debug_marker").m_status == YomkResponse::eOk, "YOMK_DEBUG 返回 eOk");
                  CHECK((cap.str().find("[Debug] [MainLogger:") != std::string::npos &&
                         cap.str().find("lg1_s1_debug_marker") != std::string::npos),
                        "DEBUG 输出格式与 marker");
            }

            // 级别开关：OFF→无输出仍 eOk→ON→恢复输出（四级别逐一）
            {
                  CHECK(YOMK_OFF_CONSOLE_LOG_INFO().m_status == YomkResponse::eOk, "OFF_CONSOLE_LOG_INFO 返回 eOk");
                  CoutCapture cap;
                  CHECK(YOMK_INFO("lg1_s1_off_info_marker").m_status == YomkResponse::eOk,
                        "INFO 级别关闭后调用仍 eOk");
                  CHECK(cap.str().find("lg1_s1_off_info_marker") == std::string::npos,
                        "INFO 级别关闭后无输出");
                  CHECK(YOMK_ON_CONSOLE_LOG_INFO().m_status == YomkResponse::eOk, "ON_CONSOLE_LOG_INFO 返回 eOk");
                  CoutCapture cap2;
                  CHECK(YOMK_INFO("lg1_s1_on_info_marker").m_status == YomkResponse::eOk,
                        "INFO 级别重开后调用 eOk");
                  CHECK(cap2.str().find("lg1_s1_on_info_marker") != std::string::npos,
                        "INFO 级别重开后恢复输出");
            }
            {
                  CHECK(YOMK_OFF_CONSOLE_LOG_WARN().m_status == YomkResponse::eOk, "OFF_CONSOLE_LOG_WARN 返回 eOk");
                  CoutCapture cap;
                  YOMK_WARN("lg1_s1_off_warn_marker");
                  CHECK(cap.str().find("lg1_s1_off_warn_marker") == std::string::npos, "WARN 级别关闭后无输出");
                  YOMK_ON_CONSOLE_LOG_WARN();
                  CoutCapture cap2;
                  YOMK_WARN("lg1_s1_on_warn_marker");
                  CHECK(cap2.str().find("lg1_s1_on_warn_marker") != std::string::npos, "WARN 级别重开后恢复输出");
            }
            {
                  CHECK(YOMK_OFF_CONSOLE_LOG_ERROR().m_status == YomkResponse::eOk, "OFF_CONSOLE_LOG_ERROR 返回 eOk");
                  CoutCapture cap;
                  YOMK_ERROR("lg1_s1_off_error_marker");
                  CHECK(cap.str().find("lg1_s1_off_error_marker") == std::string::npos, "ERROR 级别关闭后无输出");
                  YOMK_ON_CONSOLE_LOG_ERROR();
                  CoutCapture cap2;
                  YOMK_ERROR("lg1_s1_on_error_marker");
                  CHECK(cap2.str().find("lg1_s1_on_error_marker") != std::string::npos, "ERROR 级别重开后恢复输出");
            }
            {
                  CHECK(YOMK_OFF_CONSOLE_LOG_DEBUG().m_status == YomkResponse::eOk, "OFF_CONSOLE_LOG_DEBUG 返回 eOk");
                  CoutCapture cap;
                  YOMK_DEBUG("lg1_s1_off_debug_marker");
                  CHECK(cap.str().find("lg1_s1_off_debug_marker") == std::string::npos, "DEBUG 级别关闭后无输出");
                  YOMK_ON_CONSOLE_LOG_DEBUG();
                  CoutCapture cap2;
                  YOMK_DEBUG("lg1_s1_on_debug_marker");
                  CHECK(cap2.str().find("lg1_s1_on_debug_marker") != std::string::npos, "DEBUG 级别重开后恢复输出");
            }
      }

      // ============ Section 2: 自定义 tag（自动创建 console logger） ============
      {
            {
                  CoutCapture cap;
                  CHECK(YOMK_INFO_TAG("lg1_tag_logger", "lg1_s2_tag_marker").m_status == YomkResponse::eOk,
                        "YOMK_INFO_TAG 新 tag 返回 eOk");
                  CHECK((cap.str().find("[lg1_tag_logger:") != std::string::npos &&
                         cap.str().find("lg1_s2_tag_marker") != std::string::npos),
                        "新 tag 自动创建 console logger 并以其名输出");
            }
            {
                  // 宏形态 tag 参数字面量拼接（tag ":" 行号），运行时 string tag 走底层 API 直连
                  std::string longTag(65536, 'T');
                  CoutCapture cap;
                  CHECK(YomkAPI::CONSOLE_LOG_INFO_TAG(longTag, "lg1_s2_longtag_marker").m_status == YomkResponse::eOk,
                        "65536 字符超长 tag 返回 eOk");
                  CHECK(cap.str().find("lg1_s2_longtag_marker") != std::string::npos,
                        "超长 tag 日志正常输出");
                  CHECK(cap.str().find("[" + longTag + "]") != std::string::npos,
                        "超长 tag 完整回显到输出行");
            }
            {
                  CoutCapture cap;
                  CHECK(YOMK_INFO_TAG("lg1_tag_empty", "").m_status == YomkResponse::eOk,
                        "空内容日志返回 eOk");
                  CHECK(cap.str().find("[lg1_tag_empty:") != std::string::npos,
                        "空内容日志仍输出 tag 头（内容为空）");
            }
      }

      // ============ Section 2.5: 非法级别 default 降级（P2-c 修复后回填，须在 S3 设置 proxy 前） ============
      {
            // proxy 一旦设置即拦截一切 console_log（进程内不可撤销），console_log 的 switch
            // default 分支仅在此窗口可触达；枚举已加 : int 底层类型，99 为合法值非 UB
            std::string out;
            YomkResponse r(YomkResponse::eInvalid);
            {
                  CoutCapture cap;
                  r = YOMK_REQUEST("/YomkLogger/console_log",
                                   YomkMkPtr(Log, yomk::Log{static_cast<yomk::Log::ELogLevel>(99),
                                                            "lg1_s2p5_badlevel_marker", "MainLogger"}));
                  out = cap.str();
            }
            CHECK(r.m_status == YomkResponse::eOk, "非法级别 99 console_log 返回 eOk（P2-c 修复后 default 活分支）");
            CHECK(out.find("unknown log level, use Info") != std::string::npos,
                  "console_log default 分支错误提示（降级 Info）");
            CHECK(out.find("[Info ] [MainLogger] lg1_s2p5_badlevel_marker") != std::string::npos,
                  "非法级别降级 Info 输出 marker");
      }

      // ============ Section 3: proxy 契约（设置后进程内不可撤销，S4-S7 保持穿透态） ============
      {
            // 设置前 all 内省 proxy:off
            {
                  auto allResp = YOMK_LOGGER_INFO_ALL();
                  YomkUnPackPkg(allResp.m_data, StringArray, arr);
                  CHECK((arr != nullptr && !arr->d.empty() &&
                         arr->d.front() == "console:debug:on info:on warn:on error:on proxy:off"),
                        "proxy 设置前 ALL 首行 consoleLevelLine 全 on + proxy:off");
            }

            g_proxyHits.store(0);
            g_proxyIntercept.store(true);
            auto proxy = [](const yomk::Log &) -> bool
            {
                  g_proxyHits.fetch_add(1);
                  return !g_proxyIntercept.load(); // 拦截态返回 false，穿透态返回 true
            };
            CHECK(YOMK_SET_CONSOLE_LOG_PROXY(proxy).m_status == YomkResponse::eOk,
                  "SET_CONSOLE_LOG_PROXY 返回 eOk");

            // 拦截态：proxy 返回 false → eOk + 专属消息 + 无控制台输出
            {
                  CoutCapture cap;
                  auto r = YOMK_INFO("lg1_s3_intercept_marker");
                  CHECK(r.m_status == YomkResponse::eOk, "拦截态 console_log 返回 eOk");
                  CHECK(r.m_msg == "console log proxy is success.", "拦截态契约消息 console log proxy is success.");
                  CHECK(cap.str().find("lg1_s3_intercept_marker") == std::string::npos,
                        "拦截态无默认控制台输出");
                  CHECK(g_proxyHits.load() == 1, "proxy 被调用 1 次（原子计数）");
            }

            // 穿透态：proxy 返回 true → 走默认输出
            {
                  g_proxyIntercept.store(false);
                  CoutCapture cap;
                  auto r = YOMK_INFO("lg1_s3_pass_marker");
                  CHECK((r.m_status == YomkResponse::eOk && r.m_msg == "success."),
                        "穿透态 console_log 返回 eOk + success.");
                  CHECK(cap.str().find("lg1_s3_pass_marker") != std::string::npos,
                        "穿透态恢复默认控制台输出");
                  CHECK(g_proxyHits.load() == 2, "proxy 累计被调用 2 次");
            }

            // 设置后 all 内省 proxy:on
            {
                  auto allResp = YOMK_LOGGER_INFO_ALL();
                  YomkUnPackPkg(allResp.m_data, StringArray, arr);
                  CHECK((arr != nullptr && !arr->d.empty() &&
                         arr->d.front().find("proxy:on") != std::string::npos),
                        "proxy 设置后 ALL 首行 proxy:on");
            }
      }

      // ============ Section 4: 文件日志闭环 ============
      {
            fs::path logDir = tmpDir / "s4";
            fs::create_directories(logDir, ec);
            fs::path logFile = logDir / "lg1_file_logger.log";

            // 创建：eOk + 空文件已建（init 语义）
            CHECK(YOMK_FILE_LOG_CREATE(logDir.string(), "lg1_file_logger").m_status == YomkResponse::eOk,
                  "FILE_LOG_CREATE 返回 eOk");
            CHECK((fs::exists(logFile) && fs::file_size(logFile, ec) == 0),
                  "CREATE 后 <dir>/<name>.log 空文件已建");

            // 四级别入缓冲：write 前文件仍空（缓冲语义）
            CHECK(YOMK_FILE_INFO("lg1_file_logger", "lg1_s4_info_marker ", 7).m_status == YomkResponse::eOk,
                  "FILE_INFO 返回 eOk");
            CHECK(YOMK_FILE_WARN("lg1_file_logger", "lg1_s4_warn_marker").m_status == YomkResponse::eOk,
                  "FILE_WARN 返回 eOk");
            CHECK(YOMK_FILE_ERROR("lg1_file_logger", "lg1_s4_error_marker").m_status == YomkResponse::eOk,
                  "FILE_ERROR 返回 eOk");
            CHECK(YOMK_FILE_DEBUG("lg1_file_logger", "lg1_s4_debug_marker").m_status == YomkResponse::eOk,
                  "FILE_DEBUG 返回 eOk");
            CHECK(fs::file_size(logFile, ec) == 0, "WRITE 前文件仍为空（stringstream 缓冲语义）");

            // 落盘：读回断言行数/行格式/marker/tag
            CHECK(YOMK_FILE_LOG_WRITE("lg1_file_logger").m_status == YomkResponse::eOk,
                  "FILE_LOG_WRITE 返回 eOk");
            bool readOk = false;
            std::string content = readFile(logFile, readOk);
            CHECK(readOk, "落盘后日志文件可读");
            CHECK(countLines(content) == 4, "落盘内容恰 4 行（四级别各一）");
            CHECK((content.find("[Info ] [MainLogger:") != std::string::npos &&
                   content.find("lg1_s4_info_marker 7") != std::string::npos),
                  "Info 行格式（时间戳+[Info ]+[MainLogger:行号] 内容）——FILE_INFO 默认内容 tag 为 MainLogger，logger 名仅作路由 key");
            // 自定义内容 tag 变体：FILE_INFO_TAG 的 tag 进内容行
            CHECK(YOMK_FILE_INFO_TAG("lg1_file_logger", "lg1CustomTag", "lg1_s4_customtag_marker").m_status == YomkResponse::eOk,
                  "FILE_INFO_TAG 自定义 tag 返回 eOk");
            CHECK(YOMK_FILE_LOG_WRITE("lg1_file_logger").m_status == YomkResponse::eOk,
                  "自定义 tag 内容 WRITE eOk");
            bool readOkTag = false;
            std::string contentTag = readFile(logFile, readOkTag);
            CHECK((readOkTag && contentTag.find("[Info ] [lg1CustomTag:") != std::string::npos &&
                   contentTag.find("lg1_s4_customtag_marker") != std::string::npos),
                  "自定义 tag 行格式（[Info ] [tag:行号] 内容）");
            CHECK((content.find("[Warn ]") != std::string::npos &&
                   content.find("[Error]") != std::string::npos &&
                   content.find("[Debug]") != std::string::npos),
                  "Warn/Error/Debug 三级别行均在");
            CHECK(checkTimeFormat(content), "文件日志行首时间戳格式 [YYYY-MM-DD HH:MM:SS.mmm]");

            // 落盘后缓冲清空：再次 WRITE 文件不增长
            auto sizeBefore = fs::file_size(logFile, ec);
            CHECK(YOMK_FILE_LOG_WRITE("lg1_file_logger").m_status == YomkResponse::eOk,
                  "二次 FILE_LOG_WRITE 返回 eOk");
            CHECK(fs::file_size(logFile, ec) == sizeBefore, "二次 WRITE 无重复内容（缓冲已清空）");

            // 重复创建 eNo
            auto dup = YOMK_FILE_LOG_CREATE(logDir.string(), "lg1_file_logger");
            CHECK((dup.m_status == YomkResponse::eNo), "重复创建同名 file logger 返回 eNo");
            CHECK(dup.m_msg == "logger name already exists.", "重复创建契约消息");

            // 未创建 logger：FILE_LOG / FILE_LOG_WRITE 均 eNo
            auto noLogger = YOMK_FILE_INFO("lg1_never_created", "x");
            CHECK((noLogger.m_status == YomkResponse::eNo), "FILE_LOG 未创建 logger 返回 eNo");
            CHECK(noLogger.m_msg == "file logger not found.", "FILE_LOG not-found 契约消息");
            auto noWrite = YOMK_FILE_LOG_WRITE("lg1_never_created");
            CHECK(noWrite.m_status == YomkResponse::eNo, "FILE_LOG_WRITE 未创建 logger 返回 eNo");
            CHECK(noWrite.m_msg == "logger not found.", "FILE_LOG_WRITE not-found 契约消息");

            // P2-a 处置验证（LG2 回填）：空名/空 dir 一律 eInvalid，"" key 注册与
            // "/.log" 根路径拼接彻底堵死
            auto emptyBoth = YOMK_FILE_LOG_CREATE("", "");
            CHECK((emptyBoth.m_status == YomkResponse::eInvalid && emptyBoth.m_msg == "logger name is empty."),
                  "空名+空 dir 创建返回 eInvalid + logger name is empty.（P2-a 已处置）");
            auto emptyName = YOMK_FILE_LOG_CREATE(tmpDir.string(), "");
            CHECK((emptyName.m_status == YomkResponse::eInvalid && emptyName.m_msg == "logger name is empty."),
                  "空名+有效 dir 创建返回 eInvalid（P2-a 已处置）");
            auto emptyDir = YOMK_FILE_LOG_CREATE("", "lg1_empty_dir_name");
            CHECK((emptyDir.m_status == YomkResponse::eInvalid && emptyDir.m_msg == "logger dir is empty."),
                  "有效名+空 dir 创建返回 eInvalid + logger dir is empty.（P2-a 已处置）");

            // P2-b 处置验证（LG2 回填）：不可创建 dir → init 返回 false → eNo 且不注册幽灵 logger
            auto badDir = YOMK_FILE_LOG_CREATE("/proc/lg1_impossible/sub", "lg1_bad_dir_logger");
            CHECK((badDir.m_status == YomkResponse::eNo && badDir.m_msg == "init file logger failed."),
                  "不可创建 dir 返回 eNo + init file logger failed.（P2-b 已处置，异常不穿透）");
            auto ghostInfo = YOMK_LOGGER_INFO_LOGGER("lg1_bad_dir_logger");
            CHECK((ghostInfo.m_status == YomkResponse::eNo && ghostInfo.m_msg == "logger not found."),
                  "init 失败的 logger 未注册（无幽灵 key，内省 not found）");
            // init 失败不占用名字：同名换有效 dir 重试应成功
            fs::path retryDir = tmpDir / "s4_retry";
            auto retry = YOMK_FILE_LOG_CREATE(retryDir.string(), "lg1_bad_dir_logger");
            CHECK((retry.m_status == YomkResponse::eOk && fs::exists(retryDir / "lg1_bad_dir_logger.log")),
                  "init 失败后同名换有效 dir 重试成功（名字未被幽灵占用）");
      }

      // ============ Section 5: 内省三端点 ============
      {
            // LOGGERS：console/file 列表行
            auto loggersResp = YOMK_LOGGER_INFO_LOGGERS();
            CHECK(loggersResp.m_status == YomkResponse::eOk, "LOGGER_INFO_LOGGERS 返回 eOk");
            YomkUnPackPkg(loggersResp.m_data, StringArray, loggersArr);
            bool hasMain = false;
            bool hasTag = false;
            bool hasFile = false;
            if (loggersArr)
            {
                  for (const auto &line : loggersArr->d)
                  {
                        if (line == "MainLogger [console]")
                        {
                              hasMain = true;
                        }
                        if (line.rfind("lg1_tag_logger:", 0) == 0 &&
                            line.find(" [console]") != std::string::npos)
                        {
                              hasTag = true;
                        }
                        if (line == "lg1_file_logger [file] dir:" + (tmpDir / "s4").string())
                        {
                              hasFile = true;
                        }
                  }
            }
            CHECK((loggersArr != nullptr && hasMain), "LOGGERS 含 MainLogger [console] 行");
            CHECK(hasTag, "LOGGERS 含 S2 自建 tag logger 行（tag:行号 [console]）");
            CHECK(hasFile, "LOGGERS 含 file logger 行（名 [file] dir:路径）");

            // LOGGER(name)：console/file 命中格式与未注册
            auto consoleInfo = YOMK_LOGGER_INFO_LOGGER("MainLogger");
            CHECK((consoleInfo.m_status == YomkResponse::eOk && consoleInfo.m_msg == "MainLogger [console]"),
                  "LOGGER(MainLogger) 命中 console 格式");
            auto fileInfo = YOMK_LOGGER_INFO_LOGGER("lg1_file_logger");
            CHECK((fileInfo.m_status == YomkResponse::eOk &&
                   fileInfo.m_msg == "lg1_file_logger [file] dir:" + (tmpDir / "s4").string()),
                  "LOGGER(lg1_file_logger) 命中 file 格式（含 dir）");
            auto noInfo = YOMK_LOGGER_INFO_LOGGER("lg1_never_registered");
            CHECK((noInfo.m_status == YomkResponse::eNo && noInfo.m_msg == "logger not found."),
                  "LOGGER 未注册名返回 eNo + logger not found.");

            // ALL：首行 consoleLevelLine 与开关翻转联动（精确串断言）
            {
                  auto allResp = YOMK_LOGGER_INFO_ALL();
                  YomkUnPackPkg(allResp.m_data, StringArray, allArr);
                  CHECK((allArr != nullptr && !allArr->d.empty() &&
                         allArr->d.front() == "console:debug:on info:on warn:on error:on proxy:on"),
                        "ALL 首行全 on + proxy:on（S3 已设 proxy）");
            }
            {
                  YOMK_OFF_CONSOLE_LOG_INFO();
                  auto allResp = YOMK_LOGGER_INFO_ALL();
                  YomkUnPackPkg(allResp.m_data, StringArray, allArr);
                  CHECK((allArr != nullptr && !allArr->d.empty() &&
                         allArr->d.front() == "console:debug:on info:off warn:on error:on proxy:on"),
                        "OFF INFO 后 ALL 首行 info:off（开关联动精确串）");
                  YOMK_ON_CONSOLE_LOG_INFO();
                  auto allResp2 = YOMK_LOGGER_INFO_ALL();
                  YomkUnPackPkg(allResp2.m_data, StringArray, allArr2);
                  CHECK((allArr2 != nullptr && !allArr2->d.empty() &&
                         allArr2->d.front() == "console:debug:on info:on warn:on error:on proxy:on"),
                        "ON INFO 复原后 ALL 首行 info:on");
            }
      }

      // ============ Section 6: 边界值（数值特殊值 / 超大内容 / 非法级别） ============
      {
            // 变参 fold：整数边界与浮点特殊值（ostringstream 默认格式字符串化）
            {
                  CoutCapture cap;
                  CHECK(YOMK_INFO_TAG("lg1_s6_num", "i=", INT_MAX, " j=", INT_MIN, " z=", 0, " n=", -1)
                                .m_status == YomkResponse::eOk,
                        "INT_MAX/INT_MIN/0/-1 变参日志返回 eOk");
                  std::string out = cap.str();
                  CHECK((out.find("i=2147483647") != std::string::npos &&
                         out.find("j=-2147483648") != std::string::npos &&
                         out.find("z=0") != std::string::npos &&
                         out.find("n=-1") != std::string::npos),
                        "整数边界值字符串化正确");
            }
            {
                  CoutCapture cap;
                  double nanV = std::nan("");
                  double infV = std::numeric_limits<double>::infinity();
                  CHECK(YOMK_INFO_TAG("lg1_s6_flt", "a=", nanV, " b=", infV, " c=", -infV, " d=", -0.0)
                                .m_status == YomkResponse::eOk,
                        "NaN/Inf/-Inf/-0.0 变参日志返回 eOk");
                  std::string out = cap.str();
                  CHECK((out.find("a=nan") != std::string::npos &&
                         out.find("b=inf") != std::string::npos &&
                         out.find("c=-inf") != std::string::npos &&
                         out.find("d=-0") != std::string::npos),
                        "浮点特殊值字符串化正确（nan/inf/-inf/-0）");
            }

            // 1MB 超大内容：console + file 双路径
            {
                  std::string big(1024 * 1024, 'B');
                  CoutCapture cap;
                  CHECK(YOMK_INFO_TAG("lg1_s6_big", big).m_status == YomkResponse::eOk,
                        "1MB 超大内容控制台日志返回 eOk");
                  CHECK(cap.str().find(big) != std::string::npos, "1MB 内容完整输出到控制台");

                  fs::path bigDir = tmpDir / "s6";
                  fs::create_directories(bigDir, ec);
                  CHECK(YOMK_FILE_LOG_CREATE(bigDir.string(), "lg1_big_logger").m_status == YomkResponse::eOk,
                        "超大内容用 file logger 创建 eOk");
                  CHECK(YOMK_FILE_INFO("lg1_big_logger", big).m_status == YomkResponse::eOk,
                        "1MB 超大内容文件日志返回 eOk");
                  CHECK(YOMK_FILE_LOG_WRITE("lg1_big_logger").m_status == YomkResponse::eOk,
                        "超大内容 WRITE 返回 eOk");
                  bool readOk = false;
                  std::string content = readFile(bigDir / "lg1_big_logger.log", readOk);
                  CHECK((readOk && content.find(big) != std::string::npos),
                        "1MB 内容完整落盘");
            }

            // 非法级别注入回填（P2-c 修复后）：枚举已加 : int，99 为合法值；
            // file_log/off/on 三处 default 降级与无副作用实证
            // （console_log 的 default 已在 S2.5 于 proxy 设置前触达）
            {
                  fs::path badLevelDir = tmpDir / "s6_badlevel";
                  fs::create_directories(badLevelDir, ec);
                  CHECK(YOMK_FILE_LOG_CREATE(badLevelDir.string(), "lg1_s6_badlevel").m_status == YomkResponse::eOk,
                        "非法级别注入用 file logger 创建 eOk");
                  std::string out;
                  YomkResponse r(YomkResponse::eInvalid);
                  {
                        CoutCapture cap;
                        r = YOMK_REQUEST("/YomkLogger/file_log",
                                         YomkMkPtr(Log, yomk::Log{static_cast<yomk::Log::ELogLevel>(99),
                                                                  "lg1_s6_badlevel_marker", "lg1_s6_badlevel"}));
                        out = cap.str();
                  }
                  CHECK((r.m_status == YomkResponse::eOk && r.m_msg == "success."),
                        "file_log 非法级别 99 返回 eOk（P2-c 修复后 default 活分支）");
                  CHECK(out.find("unknown log level, use Info") != std::string::npos,
                        "file_log default 分支错误提示（降级 Info）");
                  CHECK(YOMK_FILE_LOG_WRITE("lg1_s6_badlevel").m_status == YomkResponse::eOk,
                        "非法级别落盘 WRITE eOk");
                  bool readOk = false;
                  std::string content = readFile(badLevelDir / "lg1_s6_badlevel.log", readOk);
                  CHECK((readOk && countLines(content) == 1 &&
                         content.find("[Info ] lg1_s6_badlevel_marker") != std::string::npos),
                        "非法级别降级 Info 落盘恰 1 行");

                  // off/on 非法级别：default 仅记录错误，无副作用仍 eOk，开关状态不变
                  std::string outOff;
                  YomkResponse rOff(YomkResponse::eInvalid);
                  {
                        CoutCapture cap;
                        rOff = YOMK_REQUEST("/YomkLogger/off_console_log_by_level",
                                            YomkMkPtr(Log, yomk::Log{static_cast<yomk::Log::ELogLevel>(99), "", ""}));
                        outOff = cap.str();
                  }
                  CHECK((rOff.m_status == YomkResponse::eOk &&
                         outOff.find("unknown log level, turn off failed.") != std::string::npos),
                        "off 非法级别 eOk + 错误提示（default 活分支）");
                  std::string outOn;
                  YomkResponse rOn(YomkResponse::eInvalid);
                  {
                        CoutCapture cap;
                        rOn = YOMK_REQUEST("/YomkLogger/on_console_log_by_level",
                                           YomkMkPtr(Log, yomk::Log{static_cast<yomk::Log::ELogLevel>(99), "", ""}));
                        outOn = cap.str();
                  }
                  CHECK((rOn.m_status == YomkResponse::eOk &&
                         outOn.find("unknown log level, turn on failed.") != std::string::npos),
                        "on 非法级别 eOk + 错误提示（default 活分支）");
                  auto allResp = YOMK_LOGGER_INFO_ALL();
                  YomkUnPackPkg(allResp.m_data, StringArray, allArr);
                  CHECK((allArr != nullptr && !allArr->d.empty() &&
                         allArr->d.front() == "console:debug:on info:on warn:on error:on proxy:on"),
                        "非法级别 off/on 无副作用（ALL 首行仍全 on）");
            }
      }

      // ============ Section 7: 异常路径（空包 / 错类型包 / 未知端点） ============
      {
            // console_log 收 nullptr 包 → YomkUnPackPkgResponse 空包分支 eNo
            {
                  CoutCapture cap;
                  auto r = YOMK_REQUEST("/YomkLogger/console_log", nullptr);
                  CHECK(r.m_status == YomkResponse::eNo, "console_log nullptr 包返回 eNo");
                  CHECK(r.m_msg == " pkg is null or pkg is not Log. ", "console_log 空包契约消息");
            }
            // console_log 收错类型包（String）→ 同分支 eNo
            {
                  auto r = YOMK_REQUEST("/YomkLogger/console_log", YomkMkPtr(String, std::string("wrong")));
                  CHECK(r.m_status == YomkResponse::eNo, "console_log 错类型包返回 eNo");
                  CHECK(r.m_msg == " pkg is null or pkg is not Log. ", "console_log 错类型包契约消息");
            }
            // file_log / write_file_log / create_file_logger / logger 空包
            CHECK(YOMK_REQUEST("/YomkLogger/file_log", nullptr).m_status == YomkResponse::eNo,
                  "file_log nullptr 包返回 eNo");
            CHECK(YOMK_REQUEST("/YomkLogger/write_file_log", nullptr).m_status == YomkResponse::eNo,
                  "write_file_log nullptr 包返回 eNo");
            CHECK(YOMK_REQUEST("/YomkLogger/create_file_logger", nullptr).m_status == YomkResponse::eNo,
                  "create_file_logger nullptr 包返回 eNo");
            CHECK(YOMK_REQUEST("/YomkLogger/logger", nullptr).m_status == YomkResponse::eNo,
                  "logger nullptr 包返回 eNo");
            // console_log 空 logger 名 → 回退 MainLogger 分支（宏形态 tag 恒非空，raw 注入才可触达）
            // 注：CHECK 须在捕获作用域外（否则断言输出被 rdbuf 重定向吞没）
            {
                  std::string out;
                  YomkResponse r(YomkResponse::eInvalid);
                  {
                        CoutCapture cap;
                        r = YOMK_REQUEST("/YomkLogger/console_log",
                                         YomkMkPtr(Log, yomk::Log{yomk::Log::eInfo,
                                                                  "lg1_s7_empty_logger_marker", ""}));
                        out = cap.str();
                  }
                  CHECK(r.m_status == YomkResponse::eOk, "console_log 空 logger 名返回 eOk（回退分支）");
                  CHECK(out.find("console logger name is empty, use MainLogger") != std::string::npos,
                        "空 logger 名错误提示（回退 MainLogger）");
                  CHECK(out.find("[MainLogger] lg1_s7_empty_logger_marker") != std::string::npos,
                        "空 logger 名回退 MainLogger 输出");
            }
            // file_log 空 logger 名 → eNo 分支（宏形态 file 参可传空串，raw 注入等价触达）
            {
                  std::string out;
                  YomkResponse r(YomkResponse::eInvalid);
                  {
                        CoutCapture cap;
                        r = YOMK_REQUEST("/YomkLogger/file_log",
                                         YomkMkPtr(Log, yomk::Log{yomk::Log::eInfo, "x", ""}));
                        out = cap.str();
                  }
                  CHECK((r.m_status == YomkResponse::eNo && r.m_msg == "file logger name is empty."),
                        "file_log 空 logger 名返回 eNo + 契约消息");
                  CHECK(out.find("file logger name is empty.") != std::string::npos,
                        "file_log 空名错误提示");
            }
            // LOGGER_INFO_LOGGER 空名 → eNo（LG2 堵死空名注册后回填；
            // LG1 时为 P2-a 幽灵 logger 命中 eOk 的现状固化）
            auto emptyNameInfo = YOMK_LOGGER_INFO_LOGGER("");
            CHECK((emptyNameInfo.m_status == YomkResponse::eNo && emptyNameInfo.m_msg == "logger not found."),
                  "LOGGER 空名返回 eNo + logger not found.（P2-a 处置后无幽灵 key）");
            // 未知端点 → invoke 路由层 eNo（服务前缀被路由剥离，消息仅含端点名）
            auto unknown = YOMK_REQUEST("/YomkLogger/no_such_func", nullptr);
            CHECK(unknown.m_status == YomkResponse::eNo, "未知端点返回 eNo（invoke not found）");
            CHECK(unknown.m_msg == "function not found: /no_such_func", "未知端点契约消息（路由剥离服务前缀）");
      }

      // ============ Section 8: logger 删除接口（LG4 新增 /delete_logger 全语义） ============
      {
            // 控制台写入一律经此 lambda：捕获作用域内调用、作用域外取响应，
            // 既避免输出噪声，也规避"CHECK 在捕获域内被吞没"（LG1 教训）
            auto consoleInfo = [](const std::string &logger, const std::string &marker)
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
                        r = YOMK_LOGGER_DELETE("");
                        out = cap.str();
                  }
                  CHECK((r.m_status == YomkResponse::eInvalid && r.m_msg == "logger name is empty."),
                        "S8: 删除空名返回 eInvalid + 契约消息");
                  CHECK(out.find("logger name is empty.") != std::string::npos,
                        "S8: 空名删除错误提示");
            }

            // 8.2 两表均未命中 → eNo + not-found 契约消息（对齐 writeFileLog/loggerInfo）
            {
                  std::string out;
                  YomkResponse r(YomkResponse::eOk);
                  {
                        CoutCapture cap;
                        r = YOMK_LOGGER_DELETE("lg1_s8_never_created");
                        out = cap.str();
                  }
                  CHECK((r.m_status == YomkResponse::eNo && r.m_msg == "logger not found."),
                        "S8: 删除未注册 logger 返回 eNo + logger not found.");
                  CHECK(out.find("logger: lg1_s8_never_created not found.") != std::string::npos,
                        "S8: 未命中错误提示含 logger 名");
            }

            // 8.3 console-only：msg 计数 1/0；删除后单查 eNo；同名再写惰性重建
            {
                  const std::string name = "lg1_s8_console_only";
                  CHECK(consoleInfo(name, "lg1_s8_console_marker").m_status == YomkResponse::eOk,
                        "S8: console-only 预热创建 eOk");
                  CHECK(YOMK_LOGGER_INFO_LOGGER(name).m_status == YomkResponse::eOk,
                        "S8: 创建后单查命中 eOk");

                  auto del = YOMK_LOGGER_DELETE(name);
                  CHECK((del.m_status == YomkResponse::eOk && del.m_msg == "deleted console:1 file:0"),
                        "S8: console-only 删除 eOk + msg 计数: " + del.m_msg);
                  CHECK(YOMK_LOGGER_INFO_LOGGER(name).m_status == YomkResponse::eNo,
                        "S8: console 删除后单查 eNo");
                  // 惰性重建：同名再写一条 → consoleLog 双检锁重建路径
                  CHECK(consoleInfo(name, "lg1_s8_rebuild_marker").m_status == YomkResponse::eOk,
                        "S8: 删除后同名再写 eOk（惰性重建）");
                  CHECK(YOMK_LOGGER_INFO_LOGGER(name).m_status == YomkResponse::eOk,
                        "S8: 惰性重建后单查恢复 eOk");
                  CHECK(YOMK_LOGGER_DELETE(name).m_status == YomkResponse::eOk,
                        "S8: 清理重建 logger eOk");
            }

            // 8.4 file-only：写 2 行不 flush → 删除释放最后引用 → ~FileLogger 锁外自动落盘
            {
                  fs::path dir = tmpDir / "s8_file";
                  fs::create_directories(dir, ec);
                  const std::string name = "lg1_s8_file_only";
                  CHECK(YOMK_FILE_LOG_CREATE(dir.string(), name).m_status == YomkResponse::eOk,
                        "S8: file logger 创建 eOk");
                  CHECK(YOMK_FILE_INFO_TAG(name, "s8Tag", "lg1_s8_file_marker_1").m_status == YomkResponse::eOk,
                        "S8: file 写入第 1 行 eOk");
                  CHECK(YOMK_FILE_INFO_TAG(name, "s8Tag", "lg1_s8_file_marker_2").m_status == YomkResponse::eOk,
                        "S8: file 写入第 2 行 eOk");
                  // 前置对照：未 flush 前 init 建的空文件仍为空
                  bool okBefore = false;
                  std::string contentBefore = readFile(dir / (name + ".log"), okBefore);
                  CHECK((okBefore && contentBefore.empty()),
                        "S8: 删除前未 flush，文件仍为空");

                  auto del = YOMK_LOGGER_DELETE(name);
                  CHECK((del.m_status == YomkResponse::eOk && del.m_msg == "deleted console:0 file:1"),
                        "S8: file-only 删除 eOk + msg 计数: " + del.m_msg);
                  bool okAfter = false;
                  std::string content = readFile(dir / (name + ".log"), okAfter);
                  CHECK((okAfter && countLines(content) == 2),
                        "S8: 删除触发析构自动落盘恰 2 行");
                  CHECK((content.find("lg1_s8_file_marker_1") != std::string::npos &&
                         content.find("lg1_s8_file_marker_2") != std::string::npos),
                        "S8: 落盘内容含两条 marker（数据不因删除丢失）");
                  CHECK(YOMK_FILE_LOG_WRITE(name).m_status == YomkResponse::eNo,
                        "S8: 删除后 WRITE 同名返回 eNo");
            }

            // 8.5 同名双表：一次删除同时清理两表（msg 计数 1/1），重复删除幂等 eNo
            {
                  fs::path dir = tmpDir / "s8_both";
                  fs::create_directories(dir, ec);
                  const std::string name = "lg1_s8_both_tables";
                  CHECK(consoleInfo(name, "lg1_s8_both_console_marker").m_status == YomkResponse::eOk,
                        "S8: 双表用例 console 侧创建 eOk");
                  CHECK(YOMK_FILE_LOG_CREATE(dir.string(), name).m_status == YomkResponse::eOk,
                        "S8: 双表用例 file 侧创建 eOk");

                  auto del = YOMK_LOGGER_DELETE(name);
                  CHECK((del.m_status == YomkResponse::eOk && del.m_msg == "deleted console:1 file:1"),
                        "S8: 同名双表一次删除 eOk + msg 计数: " + del.m_msg);
                  CHECK(YOMK_LOGGER_INFO_LOGGER(name).m_status == YomkResponse::eNo,
                        "S8: 双表删除后单查 eNo（两表均已清理）");
                  std::string out2;
                  YomkResponse del2(YomkResponse::eOk);
                  {
                        CoutCapture cap;
                        del2 = YOMK_LOGGER_DELETE(name);
                        out2 = cap.str();
                  }
                  CHECK((del2.m_status == YomkResponse::eNo && del2.m_msg == "logger not found."),
                        "S8: 重复删除返回 eNo（幂等）");
                  CHECK(out2.find("logger: lg1_s8_both_tables not found.") != std::string::npos,
                        "S8: 重复删除错误提示含 logger 名");
            }

            // 8.6 内省行数递减守恒 + ALL 与 LOGGERS 行数关系 + MainLogger 删除后惰性重建
            {
                  fs::path dir = tmpDir / "s8_count";
                  fs::create_directories(dir, ec);
                  const std::string name = "lg1_s8_count";
                  const size_t before = unpackLines(YOMK_LOGGER_INFO_LOGGERS()).size();
                  CHECK(consoleInfo(name, "lg1_s8_count_marker").m_status == YomkResponse::eOk,
                        "S8: 计数用例 console 侧创建 eOk");
                  CHECK(YOMK_FILE_LOG_CREATE(dir.string(), name).m_status == YomkResponse::eOk,
                        "S8: 计数用例 file 侧创建 eOk");
                  CHECK(unpackLines(YOMK_LOGGER_INFO_LOGGERS()).size() == before + 2,
                        "S8: 双表各建一个 → LOGGERS 行数 +2");
                  CHECK(YOMK_LOGGER_DELETE(name).m_status == YomkResponse::eOk, "S8: 计数用例删除 eOk");
                  CHECK(unpackLines(YOMK_LOGGER_INFO_LOGGERS()).size() == before,
                        "S8: 删除后 LOGGERS 行数回落（递减守恒）");
                  auto allLines = unpackLines(YOMK_LOGGER_INFO_ALL());
                  CHECK((allLines.size() == before + 1 &&
                         allLines.front() == "console:debug:on info:on warn:on error:on proxy:on"),
                        "S8: ALL 行数 = LOGGERS 行数 + 状态行，首行精确串不变");

                  // MainLogger 无特殊保护：可删；重建须走"空 logger 名回退"路径
                  // （YOMK_INFO 宏把 "MainLogger:行号" 作为 logger 名，并不会重建裸 MainLogger）
                  CHECK(YOMK_LOGGER_DELETE("MainLogger").m_status == YomkResponse::eOk,
                        "S8: MainLogger 可删除 eOk（无特殊保护）");
                  CHECK(YOMK_LOGGER_INFO_LOGGER("MainLogger").m_status == YomkResponse::eNo,
                        "S8: MainLogger 删除后单查 eNo");
                  std::string rebuildOut;
                  YomkResponse rebuild(YomkResponse::eInvalid);
                  {
                        CoutCapture cap;
                        rebuild = YOMK_REQUEST("/YomkLogger/console_log",
                                               YomkMkPtr(Log, yomk::Log{yomk::Log::eInfo,
                                                                        "lg1_s8_mainlogger_rebuild_marker", ""}));
                        rebuildOut = cap.str();
                  }
                  CHECK(rebuild.m_status == YomkResponse::eOk,
                        "S8: MainLogger 删除后空名回退写入 eOk");
                  CHECK(YOMK_LOGGER_INFO_LOGGER("MainLogger").m_status == YomkResponse::eOk,
                        "S8: MainLogger 惰性重建后单查恢复 eOk");
                  CHECK(rebuildOut.find("[MainLogger] lg1_s8_mainlogger_rebuild_marker") != std::string::npos,
                        "S8: 重建对象名确为 MainLogger（输出行 [MainLogger]）");
                  CHECK(unpackLines(YOMK_LOGGER_INFO_LOGGERS()).size() == before,
                        "S8: MainLogger 删除+重建后行数回到基线（无泄漏无重复）");
            }

            // 8.7 异常路径：空包 / 错类型包（YomkUnPackPkgResponse 契约分支）
            {
                  auto rNull = YOMK_REQUEST("/YomkLogger/delete_logger", nullptr);
                  CHECK((rNull.m_status == YomkResponse::eNo && rNull.m_msg == " pkg is null or pkg is not String. "),
                        "S8: delete_logger nullptr 包返回 eNo + 契约消息");
                  auto rWrong = YOMK_REQUEST("/YomkLogger/delete_logger",
                                             YomkMkPtr(Log, yomk::Log{yomk::Log::eInfo, "x", "y"}));
                  CHECK((rWrong.m_status == YomkResponse::eNo && rWrong.m_msg == " pkg is null or pkg is not String. "),
                        "S8: delete_logger 错类型包（Log）返回 eNo + 契约消息");
            }
      }

      // ============ 收尾清理 ============
      YOMK_SHUTDOWN();
      fs::remove_all(tmpDir, ec);

      if (g_failed == 0)
      {
            std::cout << "TestYomkLoggerLifecycle all check passed." << std::endl;
            return 0;
      }
      std::cout << "TestYomkLoggerLifecycle FAILED (" << g_failed << " checks failed)." << std::endl;
      return 1;
}
