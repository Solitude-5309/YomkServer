/**
 * @file TestYomkLoggerStress.cpp
 * @brief YomkLogger 压力测试（LG4）
 *
 * 覆盖测试要求第 7 类（压力/吞吐/大数据量），镜像 FPC4（TestYomkFunctionPoolStress）骨架：
 * - S1：console 侧规模与热路径（N 个唯一 tag 惰性创建 / 1000 行内容守恒抽样 /
 *       命中写 / 级别 OFF 短路 / proxy 短路 / 热 tag 重复写）；Phase A 分段计时并在
 *       1k/10k/100k/N 四个规模点测 ALL 延迟（P4-c 中小规模数据点：console 表线性增长是全程
 *       唯一能覆盖中小规模的窗口，S2 起条目数只增不减）
 * - S2：file 侧激进规模（N 个 logger 分片 1000/目录 + N/10 单目录对照相 + 磁盘足迹守恒）
 * - S3：文件路径写入与大缓冲落盘（N 行缓冲分布 K 个 logger / 2N 行单 logger 大缓冲一次落盘 /
 *       K 个 logger 逐个 flush 行数守恒 / 空缓冲重复 flush 文件不增长）——P4-a 验证点
 * - S4：内省端点在 2N+ 条目下的响应（全规模 LOGGERS/ALL 延迟 + ALL 数据段与 LOGGERS 逐行一致 /
 *       单查 N/100 次 / 内省与 createFileLogger 并发相互阻塞量化）——P4-c 验证点
 * - S5：8 线程混合负载 churn（console 写 + file 建/写 + ALL 内省 + 删除 + 删除后单查）
 *       + 全量删除回收（分组 eOk 守恒 → 残量扫除 → LOGGERS 行数为 0 硬断言）
 * - S6：收尾（清理前磁盘足迹守恒 / YOMK_SHUTDOWN 计时 / remove_all /
 *       临时目录不存在硬断言（用户口径：测试后临时文件全部清理干净）/ RSS 峰值）
 *
 * 规模经 YOMK_TEST_STRESS_SCALE 参数化（缺省 100000，clamp 下限 1000），对称激进口径：
 * console tag = N；file logger = N（分片）+ N/10（单目录对照）+ 2×N/100（S4-D）+ 1（S3 大缓冲）
 * + N/500×8（S5 churn，建后即删但磁盘文件保留）→ 峰值 /tmp 下约 1.11N + 0.016N 个真实 .log
 * 文件（FileLogger::init 语义：创建即建空文件并建目录；删除接口不删磁盘文件）。
 * 每 Section 记录 [BASELINE] 吞吐/延迟但不断言 timing（环境相关），仅断言守恒与正确性。
 * 断言总数随 N 变化（S1A 扫描点个数 = 去重后的 {1000,10000,100000,N} 交集），N=1000 时 61 条。
 *
 * 各构建降档：默认 N=100000；cov/ASan N=10000；TSan N=1000（clamp 下限，见 V6/V7/V8）。
 *
 * LG4 审查结论与实测数据（未注明规模者为默认构建 N=100000；修复前 1 次、修复后 3 次，
 * 列全部 3 次值；标注 N=20000 者取 3 次中位数）：
 *
 * 【采样环境口径——读下方绝对值前必看】本机为 2 CPU / 3.8 GB 内存的虚机，而本测试 S5 相
 * 自身就起 8 线程（4× 超额订阅）、全规模 VmHWM 达 491 MB。V2 采样期间伴随并发构建/测试
 * 负载，因此下文的绝对值普遍慢于空闲环境；2026-09-06 18:40 在**同一二进制、同一 N=100000、
 * 同一测试代码**下于空闲环境复测，得到：S1B 1.14-1.30 M ops/s（本文记 101-114 k）、
 * S1E 0.88-1.30 M（本文 107-112 k）、S3A 1.12-1.32 M（本文 100-110 k）、
 * S2A 81-93 k（本文 24-29 k）、S4A_all 28.6-31.2 ms（本文 60.3-64.1）、
 * S4A_loggers 33.3 ms（本文 68.7-73.4）、S1A 扫描@100001 条目 13.9 ms（本文 24.9-32.9）、
 * @10001 1.07 ms（本文 2.83-3.30）；而与资源竞争无关的量精确吻合：
 * S3B_flush 4.1-4.8 ms / 2177-2517 MB/s（本文 4.0-9.1 ms / 1143-2593）、S5A 2051-3353 ms
 * （本文 3038-3141）、S4_VmHWM 209004 kB（本文 210 MB）、S5_VmHWM 491308 kB（本文 492 MB）、
 * S4A 条目守恒 210002/210003。退化倍数与相位的资源类型强相关（分配密集相 ~11×、
 * 文件创建相 ~3×、内存快照相 ~2×、单次顺序落盘 ~1×），符合 2 核下 CPU/内存带宽/页分配
 * 竞争的特征，非代码差异（已用 char[96] vs std::array<char,96> 单变量 A/B 各 2 轮交叉验证：
 * 两变体所有指标均在噪声内重叠）。
 * 因此：**下文绝对值仅在当时采样环境内有效，不可跨环境直接复现；作为 V2 判据的是
 * “修复前/修复后”背靠背同环境采样的比值**（每组 3 次、方差 ±3-6%，说明当时负载稳定）。
 * 原始日志已按用户“测试完把临时文件全部清理干净”的口径删除，故竞争成因无法进一步取证。
 * 后续闭环如需跨环境可比的绝对值，应在空闲机器上单进程采样并记录 nproc/loadavg/free。
 * 【LG5 已实施】该环境快照已落地为 readEnvSnapshot()：输出 hardware_concurrency +
 * /proc/loadavg 前三值 + /proc/meminfo 的 MemAvailable，在 main 开头以一行 [ENV] 先于一切
 * [BASELINE] 打印，使每次采样自带环境标注、无需事后归因；纯信息行非断言，不影响断言计数，
 * 非 Linux 分支降级为 n/a。LG5 的 V2 实测（N=20000、共 3 次采样；第 3 次为 §1.3 初始化
 * 列表改回计划的多行形态后的最终二进制，前 2 次为其单行形态，两者目标码等价）：
 *   第 1 次 [ENV] cpu=2 loadavg=0.17/0.26/0.35 memAvail=1917000 kB
 *           S1B 1260413 ops/s、S3A 1312613 ops/s
 *   第 2 次 [ENV] cpu=2 loadavg=0.54/0.50/0.40 memAvail=1849648 kB
 *           S1B 1303704 ops/s、S3A 1350544 ops/s
 *   第 3 次 [ENV] cpu=2 loadavg=0.60/0.46/0.45 memAvail=1964596 kB
 *           S1B 1362453 ops/s、S3A 1351815 ops/s
 * 对照上文 LG4 空闲环境复测区间（S1B 1.14-1.30 M、S3A 1.12-1.32 M）：三次 S1B 落在
 * 1.26-1.36 M、S3A 落在 1.31-1.35 M，即区间内或高出上界 5.2%/2.5% 以内。V2 判据是
 * 「同量级」且只防抽取致慢，偏高不构成回归 → 通过，即 P4-f 的 inline 抽取零开销
 * （另由 objdump 实证：Release/-O3 下 yomkLogLocalTimeFormatted 在 libYomkServer.so 中出现
 * 0 次 —— 完全内联、无 call 无 PLT，故调用点开销与原 static 逐指令等价）。
 * 方法论收获：三次里 loadavg 最高的第 3 次（0.60）反而 S1B 最快（1.362 M），说明该量级下
 * ±5% 的次间波动并非由负载单独决定，这正是把 [ENV] 与数据同记的价值 —— 可当场判定
 * 各次采样是否同环境，而不必事后猜测。
 * - P4-a（本闭环修复）：FileLogger 的缓冲成员由 std::stringstream 改为 std::string。
 *   原实现落盘必须 m_logStream.str() 取快照、且取了 2 次（.size() 判空 + << 写盘）= 2 次
 *   全量拷贝；改后判空与写盘直接复用成员本体 → 落盘路径零拷贝、零大块瞬时分配。
 *   为何不能只把 2 次 str() 减为 1 次（本闭环最重要的实测教训）：str() 产生的 MB 级临时串
 *   超 glibc 动态 mmap 阈值，走 mmap → 逐页缺页 + 释放时 munmap，该开销远大于拷贝本身。
 *   进程隔离微基准（40000 行 ≈2.05 MB，进程内首次 flush）：原 2 次拷贝 9.26 ms /
 *   单次快照 5.25 ms / std::string 缓冲 1.02 ms —— 即“减到 1 次”是负优化（in-situ
 *   S3-B @N=20000 由 1.0 ms 退化到 5.1 ms）；旧代码的两次分配恰好获得“第一次释放抬高
 *   mmap 阈值、第二次落堆”的意外红利。微基准必须进程隔离：同进程顺序跑三个变体会被
 *   前一个变体的大块释放预热阈值，得出与 in-situ 相反的结论（本闭环实际踩过）。
 *   S3-B（2N 行大缓冲一次落盘）：修复前 6.5 ms（1601 MB/s）→ 修复后 4.0/4.3/9.1 ms
 *   （2593/2393/1143 MB/s），中位数 -34%（吞吐中位数 +49%）；N=20000：1.0 → 0.9 ms。
 *   S3-B 填充（log() 的逐行追加路径同步改写）：94042 → 109967/104977/101874 ops/s，中位 +12%。
 * - P4-b（本闭环修复）：localTimeFormatted() 每行构造 std::stringstream + std::put_time
 *   （ConsoleLogger.cpp / FileLogger.cpp 两处同构重复）→ 改定长缓冲 snprintf，格式逐字节保持。
 *   S1-B（console 命中写）：102607 → 113702/109934/101447 ops/s，中位 +7%
 *   （N=20000：100236 → 109390，+9%）；
 *   S1-E（console 热 tag）：100756 → 112334/110713/106834 ops/s，中位 +10%；
 *   S3-A（file 缓冲写）：98622 → 109663/102100/99709 ops/s，中位 +4%
 *   （N=20000：99274 → 109668，+10%）。
 *   实施坑：kTimeBufSize 必须取 96 而非“最长 25+1=26”——std::tm 字段是无约束 int，GCC 按
 *   %d 最宽 11 字节推算，缓冲 <86 字节必报 2 条 -Wformat-truncation（违反 V1 零告警）；
 *   96 仍是栈上定长缓冲、不引入任何堆分配。
 *   格式等价性由 S1-A2 的逐行时间戳校验 + 既有四处 checkTimeFormat 断言共同兜底（V3）。
 * - P4-c（本闭环修复）：loggers/listAll 的 P3-a 双锁快照窗口随条目数线性增长（该窗口会阻塞
 *   createFileLogger 的独占锁）→ 取锁后立即 lines.reserve 预分配缩短窗口，原子性语义不变。
 *   S4-A 全规模 210002 条目：ALL 100.178 → 60.330/62.968/64.055 ms（中位 -37%）、
 *   LOGGERS 98.693 → 68.668/72.531/73.437 ms（中位 -27%）；
 *   N=20000（42002 条目）ALL 16.284 → 12.189 ms（-25%）。
 *   S5-A（8 线程混合 churn，含并发内省）：4166.8 → 3131.1/3038.1/3141.1 ms（中位 -25%）。
 *   单变量 A/B（同含 P4-a/b，仅开关两处 reserve，各 3 次，N=100000，no-reserve → reserve）：
 *     S4-A ALL 99.5 → 63.0 ms（-37%）、S4-A LOGGERS 95.2 → 72.5 ms（-24%）、
 *     S5-A 3832.7 → 3131.1 ms（-18%）、S1-A 扫描@10001 条目 3.30 → 2.83 ms（-14%）。
 *   唯一反向数据点（如实登记，违反 V2 “不慢于修复前”的字面口径）：S1-A 扫描@100001 条目
 *   修复前 24.908 / no-reserve 25.922·26.017·24.942 / reserve 32.456·31.650·32.860 ms（+25%）。
 *   两组各 3 次均值都很紧且区间不重叠 → 是真实效应而非噪声。归因：该点是 console-only
 *   10 万条短串的瞬时中间态（file 表尚空，S2 起才建 file logger），单次调用分布双峰
 *   （no-reserve min 9.0 / max 52.7 ms，reserve min 16.7 / max 47.0 ms）：reserve 消去了
 *   no-reserve 偶发的“几何增长路径把堆预热后终块落 warm brk”快模，稳定落在中间；而
 *   S4-A 处两组均单峰、reserve 稳定 -37%。取舍依据：P4-c 的目标场景是系统长期驻留的
 *   全规模状态（S4/S5 口径，也是双锁窗口阻塞 createFileLogger 的真实痛点），且按 glibc
 *   分配器怪癖去调 reserve 大小既脆弱又不可移植（本工程同时支持 Windows）→ 保留 reserve。
 *   S4-D（内省并发下 create 延迟比）：修复前 ratio 3.21 → 修复后 4.30/7.25/3.20（其中一次
 *   与修复前同值），全规模下快照仅 3-5 次（每次 ALL 60-100 ms），该指标被采样噪声主导
 *   （N=20000 时反向：4.36 → 3.24），故只作方向性记录、不作 P4-c 判据；主判据为 S4-A
 *   （方差紧）与 S5-A。
 * - 覆盖率副作用（V8 实测：纯分母效应，不是覆盖退化）：P4-a/P4-b 移除 stringstream 与
 *   std::put_time 后，FileLogger.cpp 的 gcov 分支分母由 266 缩到 210（-56），已执行分支同减
 *   56（242 → 186），未执行分支恒为 24 → 百分比 90.98% 降到 88.57%；行侧同构：分母
 *   75 → 84（+9），已执行 71 → 80（+9），未执行恒为 4 → 94.67% 升到 95.24%。这 24 个未执行
 *   分支与 4 个未执行行全部落在同一处不可达 catch（FileLogger.cpp L99/L101/L102/L103，24 个
 *   分支编号 100% 在 L101 单行，源自 3 次 string operator+ + e.what() 虚调用 + 宏展开的异常
 *   隐藏分支），该 catch 由 P2-b（LG2）引入、LG4 未触碰；排除后有效分支覆盖 186/186 = 100%，
 *   即覆盖洞集合与 LG3 逐位相同、仅分母收缩。行号为 V9 的 std::array + NOLINT 注释扩充后
 *   实测值（扩充前为 L91/L93/L94/L95，分支全在 L93——注释增行使同一处代码整体下移 8 行，
 *   两次采样洞集合逐位相同）。V8 三文件结果：ConsoleLogger 100%/100%（持平
 *   基线）、YomkLogger 98.87%/90.73%（双双高于基线 98.74%/90.18%，未执行仅剩 P3-b 两行）、
 *   FileLogger 95.24%/88.57%（行升、分支见上）；deleteLogger 全区间无 #####/=====（行覆盖
 *   100%），五条分支计数齐全：空名 L231=1、未命中 L261/L262=2、console 表 erase L241=21370、
 *   file 表命中 L253-255=11364、eOk 返回路径 = L259(21370) - 2 = 21368（L268/L269 因
 *   operator+ 串接按子表达式计为 42736/85472，不是调用次数）。
 *   测量学教训一：gcov 的 “Branches executed” 分母含 C++ 异常处理的隐藏 branch/call，消除
 *   流/格式化设施会同时缩减分子与分母，百分比下降 ≠ 覆盖退化，收口须比对“未执行洞集合”
 *   （本闭环用 266/242/24 与 210/186/24 的算术闭环完成论证，无需回退源码做对照）。
 *   测量学教训二：23 个二进制顺序累积同一 .gcda 时，5 次全量采样中观测到 2 次单点计数丢失
 *   （第 2 次：YomkLogger L109/L110 的 default 降级分支报 #####，但 Lifecycle S2.5 的对应
 *   CHECK 在 CoutCapture 作用域外且 rc=0，证明该行确已执行；V9 重测第 1 次：L398 即
 *   listAll 的 return 报 #####，而该行实际计数 1574）。两次均在紧邻的重跑中自行恢复，且
 *   丢失位置互不重复、不可二分定位（只跑 Lifecycle 时 L109/L110 恒为 1，增量追加后续
 *   二进制也不擦除）→ 覆盖率结论须以“未执行行明细可解释 + 可复现”为准，不能只看单次
 *   百分比，出现单点 ##### 时应先重跑再定性。
 * - P4-d（LG6 实测否决，两张表保持 std::map）：LG4 的登记意图是把两张表改为
 *   std::unordered_map，以消除 10 万条目下 O(log n) 次字符串比较的查找退化。LG6 先把 S4-C 的
 *   probes 由 N/100 提升为 N（原口径 @N=100000 仅 1000 次、总耗时 0.6-1.2 ms，低于计时分辨率，
 *   故 860123 → 1626783 ops/s 不可解读；新口径单次采样进入十毫秒量级，N=20000 修复前实测
 *   11.3-11.9 ms、三次方差 ±2.3%），使 S4-C 成为两张表查找热路径的可解读量化口径（loggerInfo
 *   先取 console 表 shared_lock 查找、miss 后再取 file 表 shared_lock 查找），再实施
 *   unordered_map + loggers/listAll 锁外分段排序（排序置于双锁作用域之外以免重新拉长 P4-c
 *   缩短的快照窗口；分段而非整体排序以保住 console 段字典序在前、file 段在后的既有可读契约），
 *   随后以四方消融实测否决并回退。
 *   消融设计：A=map 无 move（修复前）、D'=map+move、E=unordered_map+move 无排序、
 *   B=unordered_map+move+排序（LG6 中间态全量）；每变体独立构建 .so 并以 LD_LIBRARY_PATH 隔离
 *   —— worktree 二进制的 RUNPATH 硬编码指向各自 build/bin，把二进制拷到别处运行会静默加载原
 *   目录的 .so 而使消融结论全部作废，故逐变体用 ldd 验证实际解析路径并以 md5sum 确认 4 个 .so
 *   互异，同时把每次运行解析到的 .so 路径打进日志留证。N=20000 四轮拉丁方交错，各变体内部方差 <15%。
 *   结论（中位数）：仅换容器（D'→E）即令 S4A_loggers 3.05 → 6.60 ms（+116%）、S5B_delete_all
 *   1534050 → 919038 ops/s（-40%）、S5A_mixed_churn 4702 → 1976 ops/s（-58%）；再叠加保序排序
 *   （E→B）S4A_loggers 达 12.48 ms、S1A_scan_ALL@20001 0.90 → 3.71 ms、S4C 约 1.55M → 约 1.20M
 *   ops/s（-10.6%，未过 V2 主判据「不慢于修复前」）。N=100000 三方同向且更剧烈（S4A_loggers
 *   22.7 → 90.0 ms、S4C 66 → 105/118 ms）。而 S4_VmHWM/S5_VmHWM 与 S1A/S1B/S1E/S1D/S2A/S3A
 *   六条热路径在换容器前后差异均在噪声内 —— O(log n) → O(1) 摊还的理论收益，在本模块的实际
 *   规模与访问模式下换不到任何可测收益，却付出内省与删除路径数倍退化，故回退。退化机制未在
 *   本闭环内进一步定位（消融已排除「排序是唯一因素」：不排序时仍慢约 2 倍），YomkLogger.h 的
 *   容器选型注释同批登记。
 *   口径不可比警告：probes 由 N/100 改为 N 后 S4 阶段的分配行为随之改变，故本批 A 侧
 *   S4_VmHWM=256516 kB / S5_VmHWM=526444 kB（N=100000）高于上方 LG4 登记的修复前值
 *   209004 / 491308 kB，两组绝对值不可直接比对；LG6 的一切 VmHWM 结论均取同批交错的 A/F 对照。
 * - P4-e（LG6 已修复）：setConsoleLogProxy 原恒置 m_consoleLogProxy = true，proxy 一旦安装进程内
 *   不可卸载 —— YOMK_SET_CONSOLE_LOG_PROXY(nullptr) 仍报 proxy:on，而 m_consoleLogProxyFunc 为空使
 *   consoleLog 的三重条件短路，行为等同未安装却在内省里谎报 proxy:on。LG6 改为
 *   m_consoleLogProxy = (m_consoleLogProxyFunc != nullptr)，两字段由同一把叶子锁内一致更新，传空
 *   回调即卸载并恢复框架默认控制台输出（签名与包结构均未改，ConsoleLogProxy{func} 可从空
 *   std::function 构造）。实测：Lifecycle 新增卸载用例 11 条断言全绿（含「卸载后 ALL 首行回到
 *   proxy:off」「卸载后 console 日志恢复框架默认输出且非空」「重装可逆」），全量 28 二进制绿；
 *   既有 proxy:on 断言（Lifecycle 5 处、Concurrency 3 处、本测试 S1 Phase D 1 处）均在安装之后且
 *   不调用卸载，未受影响。V2 侧 S1D_console_proxy_shortcut 修复前后 2593300 → 2801100 ops/s
 *   （+8.0%），即开关由恒 true 改为按回调判定未给短路路径带来可测代价。本测试仍把 proxy 短路相
 *   置于 S1 Phase D、其后由测试侧 g_proxyReturn 恒返回 true 穿透；S1-D 之后的各 Section 只断言
 *   响应码与落盘内容、不校验 console 输出，该编排无需随本修复调整。
 * - P4-f（LG5 已修复）：localTimeFormatted() 原为 ConsoleLogger.cpp 与 FileLogger.cpp 中两处
 *   逐字同构的 static 实现（LG4 修 P4-b 时被迫两处同改，即重复代码的真实维护成本），LG5 抽取为
 *   模块内部头 YomkServer/src/Modules/Logger/YomkLogTime.h 的单一 inline 实现
 *   yomkLogLocalTimeFormatted()：函数体逐字搬迁未改一行；用 inline 而非 static 是为跨 TU 的
 *   单一定义（ODR），两 .cpp 各自内联展开 → 调用点开销与原 static 等价，且不新增编译单元
 *   （无需改 YomkServer/CMakeLists.txt 的源列表）；因 Logger 类均在全局命名空间而 inline 具
 *   外部链接，函数名加 yomkLog 前缀避免符号冲突。等价性由 V3 的 6 处 checkTimeFormat 调用点
 *   （Lifecycle:220/445、Direct:164/242、Concurrency:549、本测试 S1-A2 的 :847）+ V2 吞吐同量级
 *   共同兜底。注：LG5 计划沿用 LG4 的行号记为「4 处」，实测调用点为 6 处；其中 Lifecycle:220
 *   与 Direct:164 位于 CoutCapture 作用域内，CHECK 的 [ OK ] 行被写进捕获缓冲而不出现在
 *   终端（Direct.cpp:167-168 的注释已说明此性质），故这两处只能由整体 0 failed 佐证。
 *   同批的无用包含清理亦已完成，并**纠正 LG4 本条目的登记错误**：LG4 写作「ConsoleLogger.cpp /
 *   FileLogger.cpp 的 <sstream> 与 <iomanip>」，实测 FileLogger.cpp 从无 <sstream> —— P4-a 前
 *   它由 FileLogger.h 为 std::stringstream 成员提供，P4-a 改成员为 std::string 后失效并残留在
 *   FileLogger.h。LG5 实际移除 14 行 include：8 行随抽取迁入 YomkLogTime.h（<chrono>/<cstdio>/
 *   <ctime>/<array> 各 2 份），6 行为纯无用包含 —— ConsoleLogger.cpp 的 <sstream>+<iomanip>、
 *   FileLogger.cpp 的 <iomanip>、FileLogger.h 的 <sstream>+<map>、ConsoleLogger.h 的 <map>
 *   （两个 .h 自身均未用 map，而包含它们的 YomkLogger.h 自带 <map>）。每项以「移除后全量
 *   0 warning + 28/28 绿」为唯一判据，实测无一处暴露间接依赖、无一回退。
 * - P4-g（LG6 已修复）：内省端点原在 21 万条目下每次调用存在三重拷贝 —— listAll 的
 *   vector<string> → StringArray 包 → 测试侧 unpackLines 再拷一份，单次 ALL 的瞬时分配达数十 MB。
 *   LG6 两处收口：① YomkPkg.h 的 YomkMsg 宏补右值构造重载（向后兼容，左值实参仍逐字选中
 *   const& 版本，既有调用点行为不变），使 YomkMkPtr(Msg, std::move(data)) 真正移动入包而非静默
 *   退化为拷贝；YomkLogger::loggers 与 listAll 的返回语句改为 std::move(lines)。② 本测试
 *   unpackLines 的 lines = arr->d 改为 std::move(arr->d)，消除第三重拷贝（resp.m_data 是
 *   shared_ptr，其 const 不约束所指对象，故此处有意掏空包内 vector；11 个调用点已逐一核实
 *   解包后不再复用同一 resp 的数据段）。流式分页需改 YomkResponse 与包结构，LG6 未采纳。
 *   V2 实测（N=20000；A=修复前、F=LG6 最终，各 n=18 两批交错中位；同批另设 G=F 仅移除右值
 *   构造，用于把收益归因到 ① 本身）：
 *     S4_VmHWM   55400 → 39504 kB（-28.7%），G=49996（-9.8%）→ 降幅主体来自右值构造；
 *     S5_VmHWM  131304 → 77540 kB（-40.9%），G=125922（-4.1%）→ 同上；
 *     S1_VmHWM   14590 → 13070 kB（-10.4%）、S2_VmHWM 30218 → 24830 kB（-17.8%）、
 *     S3_VmHWM   30218 → 26758 kB（-11.5%）；
 *     S4A_loggers  6.162 → 3.829 ms（-37.9%），G=5.814（-5.6%）；
 *     S4A_all      5.604 → 3.520 ms（-37.2%），G=5.671（+1.2%）→ 内省提速几乎全部来自右值构造；
 *     S1A_scan_ALL@1001/10001/20001  0.0745/1.080/2.492 → 0.0380/0.379/0.927 ms（-49.0%/-64.9%/-62.8%）；
 *     S5A_mixed_churn 2320 → 3066 ops/s（+32.2%）、S4C 1357600 → 1418600 ops/s（+4.5%）。
 *   N=100000（A/F 各 n=3）同向：S4_VmHWM 256516 → 177136 kB（-30.9%）、S5_VmHWM
 *   526444 → 366684 kB（-30.3%）、S4A_loggers 30.485 → 23.665 ms（-22.4%）、S4A_all
 *   25.019 → 23.024 ms（-8.0%）、S1A_scan_ALL@100001 13.928 → 6.626 ms（-52.4%）、
 *   S5A 2344 → 3607 ops/s（+53.9%）、S4C 1361670 → 1504480 ops/s（+10.5%）、
 *   S1E 1127600 → 1274850 ops/s（+13.1%）。VmHWM 属资源竞争无关量（本文档上方已论证该类量
 *   跨环境精确吻合），故 -28.7% 与 -30.9% 在两个规模上一致即为强证据。对比上方 LG4 登记的
 *   「S5-A 使 VmHWM 由 S4 末的 210 MB 冲到 492 MB」，LG6 后同口径为 177 MB → 367 MB，既降了
 *   绝对值也降了 S4→S5 的涨幅（原本 +134%，现 +107%）；剩余部分为 vector<string> 本身的必要驻留。
 *   采样环境（[ENV]，如实登记）：N=20000 池化 54 次均 cpu=2，loadavg1 最低 0.27 / 中位 2.23 /
 *   峰值 7.44，memAvail 最低 255400 / 中位 1626388 / 最高 2238120 kB；N=100000 三次为
 *   cpu=2 loadavg=0.17/0.61/0.58 memAvail=1820504 kB 至 loadavg=2.74/1.28/0.81 memAvail=1664696 kB。
 *   本机仅 2 核 / 4 GB，loadavg1 中位 2.23 即已达满载，峰值 7.44 为 3.7 倍超载且已触发换页
 *   （SwapCached 278432 kB）—— 此环境直接决定了下方 S1A 未定项的可判定下限。
 * - S1A 反向数据点（LG6 如实登记，未定项）：S1A_console_create_unique_tag 修复前后
 *   864960 → 798390 ops/s（-7.7%，N=20000 n=18 交错中位；另两批独立交错为 -10.2% 与 -14.4%，
 *   N=100000 n=3 为 833854 → 672791 即 -19.3%），未过 V2 主判据「不慢于修复前」。已排除语义性
 *   退化，三条独立证据：① 归因变体 G（F 仅移除右值构造）在同批反而比 A 快 +6.9%、比 F 快
 *   +15.8%，即「加右值构造变慢」与「不加就变快」两个方向都无机制可解释（P3-b 只省一个必然
 *   可预测的分支，move 只会比 copy 便宜）；② 静态汇编对照（同一最小复现 TU 只切换 YomkPkg.h
 *   是否含右值构造，-O3 -S，免疫于机器噪声）：含右值构造版 memcpy/memmove 调用 5 → 3、
 *   call 指令 143 → 136、汇编 2462 → 2353 行、operator new 恒为 5，即该改动使这条路径严格少做
 *   2 次字符串拷贝，只可能更快；③ 同一份从未修改的 A 二进制跨批中位数漂移
 *   909125 → 948986 → 864960（±9%），即 S1A 在本机的可复现极限就在 ±10% 量级，-7.7% 落在其中。
 *   结论：登记为代码布局层面的未定项，不回退右值构造（回退将连带失去上述内省 -37% 与
 *   VmHWM -28.7%/-40.9% 的全部收益，而 G 列已证明那些收益正是它带来的）。同批其余热路径无
 *   一致方向：S1B -4.9%、S1C -6.0%、S3A -3.2%、S5B -1.6% 均在 ±10% 内，而 S1E +18.5%、
 *   S2A +13.6%、S1D +8.0%、S4C +4.5%、S5A +32.2% 同向改善；N=100000 下除 S1A（见下条）外
 *   仅 S1B -3.5% 为反向，其余六项均同向改善。
 *   本条与上方 LG4 登记的 S1-A 扫描@100001 反向数据点同属一类：均在 2 核 VM 上测得、均无可
 *   解释的语义机制、均以「如实登记 + 不回退」收口。
 * - P3-b（LG6 已修复）：YomkLogger::consoleLog 的 if (!result.second) 防御分支不可达（死代码）
 *   —— 该块持 m_consoleLoggersMutex 独占锁，且上方 find 已确认 key 为 miss，容器 emplace 在
 *   「独占 + key 确认不存在」下必然插入成功。LG4/LG5 的 gcov 实测印证：那两行是 YomkLogger.cpp
 *   中仅有的 ##### 零计数可执行行。LG6 删除该 4 行，控制流拓扑保持与修复前逐字对应（块外仍
 *   统一 consoleLogger = itLogger->second，未按原计划改为 if/else 两路径各自赋值 —— 实测那样会凭空
 *   新增一个 28 二进制累积后计数恒为 0 的 ##### 洞，旧版根本无 else 分支）。修复后 V9 复测：
 *   YomkLogger.cpp 的 ##### 洞由 2 归零、===== 仍仅 1 处（consoleLevelLine 末尾 } 的异常清理
 *   伪洞）、行覆盖 98.87% → 99.61%（未执行行由 3 降至 1）、never-executed 分支 88 → 66（死分支
 *   自带的异常隐藏分支随删缩减）；FileLogger.cpp 洞集合与 LG5 逐位相同（===== L56/L58/L59/L60、
 *   24 个 never-executed 分支全在 L58）；ConsoleLogger.cpp 100%/100%；YomkLogTime.h 经
 *   ConsoleLogger TU 100%/100%、经 FileLogger TU 0.00%（COMDAT 折叠，非覆盖洞）。
 *   新增的 move 行（loggers/listAll 的 return）均有计数（522 / 1046）。
 * - 与本次修复无关的对照：S2-A 文件建器吞吐修复前 26887 → 修复后 28561/27782/23658 ops/s
 *   （跨次波动 ±10%，无系统性变化）——该路径由 create_directories + ofstream 的真实文件
 *   系统开销主导，不是三项修复的目标；S2 均摊耗时修复前 sharded 37.2 us vs singledir
 *   36.9 us，修复后 35.0 vs 39.3 / 36.0 vs 36.0 / 42.3 vs 38.3 us —— 分片 100 目录与单目录
 *   基本同价，即本规模（单目录 1 万个 .log）下未观察到单目录 inode 膨胀退化。
 *   S6 shutdown 修复前后均 0.3-0.5 ms，证明 S5 已全量回收、10 万 FileLogger 析构无落盘 I/O 拖累。
 *
 * 断言可靠性约束（LG1/LG3 教训）：
 * - cout 重定向一律用 RAII 作用域，所有 CHECK 与 [BASELINE] 打印都在作用域之外（否则被吞没）；
 *   计时值先存局部变量，出作用域后再打印。
 * - 唯一 tag 必须直调 YomkAPI::CONSOLE_LOG_INFO_TAG（运行时拼名）：YOMK_INFO_TAG 宏会把 tag
 *   拼上调用点行号，循环体内所有轮次将共用同一 logger 名，构造不出 N 个唯一 tag。
 * - 库内部不使用 YOMK_INFO 家族宏（YOMK_ERR_POS_LOG 直写 cout），故 console 表起点恰为
 *   构造函数预置的 MainLogger，各 Section 可用绝对条目数守恒断言（起点已断言）。
 *
 * 风格：纯 main() + 失败计数，返回非 0 表示存在失败用例（零第三方依赖）
 */

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
// 压力规模参数化（FPC4 同惯例）
// ============================================================================
static uint64_t stressScale()
{
    const char *env = std::getenv("YOMK_TEST_STRESS_SCALE");
    uint64_t n = env ? std::strtoull(env, nullptr, 10) : 100000;
    return n < 1000 ? 1000 : n;
}

// ============================================================================
// [BASELINE] 记录辅助
// ============================================================================
using Clock = std::chrono::steady_clock;

static double msSince(const Clock::time_point &t0)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

static void recordBaseline(const char *section, uint64_t ops, double elapsedMs)
{
    double opsPerSec = (elapsedMs > 0.0) ? (ops / (elapsedMs / 1000.0)) : 0.0;
    std::cout << "[BASELINE] " << section << ": "
              << std::fixed << std::setprecision(0) << opsPerSec << " ops/s ("
              << ops << " ops in " << std::setprecision(1) << elapsedMs << " ms)"
              << std::endl;
}

// 延迟分布（内省端点/单次 create 用）
struct LatencyStat
{
    uint64_t count = 0;
    double minMs = 0.0;
    double avgMs = 0.0;
    double maxMs = 0.0;
};

static void recordLatency(const char *section, const LatencyStat &stat)
{
    std::cout << "[BASELINE] " << section << ": avg " << std::fixed << std::setprecision(3)
              << stat.avgMs << " ms (min " << stat.minMs << " / max " << stat.maxMs
              << " over " << stat.count << " calls)" << std::endl;
}

static void feedLatency(LatencyStat &stat, double &totalMs, double ms)
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

// LG5：采样环境快照（兑现 LG4 doc 头登记的约定）。LG4 的 V2 绝对值因采样期存在并发
// 负载而不可跨环境复现，退化倍数与相位的资源类型强相关，而原始日志已按用户口径清理
// → 事后无法取证。本函数把 CPU 核数、系统负载、可用内存随每次运行一同打印，使
// [BASELINE] 数据自带环境标注，后续闭环可直接判定“两次采样是否同环境”。
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
// cout 重定向装置（LG3 同款 + 丢弃态）
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
        std::streamsize xsputn(const char *s, std::streamsize n) override
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
    explicit ThreadSafeCoutCapture(bool discard = false)
        : m_buf(discard), m_old(std::cout.rdbuf(&m_buf)) {}
    ~ThreadSafeCoutCapture() { std::cout.rdbuf(m_old); }
    ThreadSafeCoutCapture(const ThreadSafeCoutCapture &) = delete;
    ThreadSafeCoutCapture &operator=(const ThreadSafeCoutCapture &) = delete;
    std::string str() { return m_buf.take(); }

private:
    Buf m_buf;
    std::streambuf *m_old;
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
static std::atomic<uint64_t> g_s4dSnapNonMono{0}; // 快照条目数递减次数（P3-a 语义违例）
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

// 带零填充的唯一名（FPC4 同惯例）
static std::string makeName(const char *prefix, uint64_t index)
{
    std::ostringstream oss;
    oss << prefix << std::setfill('0') << std::setw(8) << index;
    return oss.str();
}

// 解包 LOGGERS/ALL 的 StringArray；失败返回空 vector
// LG6：消除内省数据链路上的第三重拷贝（第一重在服务端建 lines、第二重是 lines 入包——
// 已由 P4-g 降为移动，第三重就是此处的出包）。resp.m_data 是 shared_ptr，其 const 只约束
// 指针本身、不约束所指对象，故此处有意掏空包内 vector；调用方不得在解包后复用同一 resp
// 的数据段（11 个调用点已逐一核实：8 处传就地临时对象，3 处传变量者在解包前只读
// m_status、解包后不再触及该 resp）
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
        lines = std::move(arr->d);
    }
    return lines;
}

// 统计 LOGGERS/ALL 数据行中的 console/file 段行数
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

// 从内省数据行解析 logger 名（"name [console]" / "name [file] dir:..."）
static std::string parseLoggerName(const std::string &line)
{
    auto pos = line.find(" [file] dir:");
    if (pos != std::string::npos)
    {
        return line.substr(0, pos);
    }
    static const std::string kConsoleSuffix = " [console]";
    if (line.size() > kConsoleSuffix.size() &&
        line.compare(line.size() - kConsoleSuffix.size(), kConsoleSuffix.size(), kConsoleSuffix) == 0)
    {
        return line.substr(0, line.size() - kConsoleSuffix.size());
    }
    return line;
}

// 状态码是否属于框架合法集合 {eInvalid, eOk, eNo}
static bool isLegalStatus(const YomkResponse &resp)
{
    return resp.m_status == YomkResponse::eInvalid ||
           resp.m_status == YomkResponse::eOk ||
           resp.m_status == YomkResponse::eNo;
}

// 时间戳格式精确校验：行首 25 字符须为 "[dddd-dd-dd dd:dd:dd.ddd]"（V3：P4-b 等价性）
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

// 统计"行尾最后一段"以 prefix 开头的 marker 出现次数（内容守恒判定）
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

// markers 中出现次数 != 1 的条目数（0 即"不丢不重"）
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

// 统计文件行数（ifstream getline，避免整文件读入内存）
static uint64_t countFileLines(const fs::path &path, bool &ok)
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
static uint64_t countDiskFiles(const fs::path &root)
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

// 对内省端点做 rounds 次调用，返回延迟分布；非 eOk 计入 badStatus
static LatencyStat measureIntrospection(uint64_t rounds, bool useAll,
                                        uint64_t &badStatus, uint64_t &lastLines)
{
    LatencyStat stat;
    double totalMs = 0.0;
    for (uint64_t i = 0; i < rounds; ++i)
    {
        auto t0 = Clock::now();
        auto resp = useAll ? YOMK_LOGGER_INFO_ALL() : YOMK_LOGGER_INFO_LOGGERS();
        double ms = msSince(t0);
        if (resp.m_status != YomkResponse::eOk)
        {
            ++badStatus;
            continue;
        }
        lastLines = unpackLines(resp).size();
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
// （P4-e：proxy 安装后本测试全程不卸载，故此后恒置 true 穿透，不影响后续 Section；
//  LG6/P4-e 起可传 nullptr 卸载，卸载语义由 Lifecycle S3 覆盖）
// ============================================================================
static bool lg4Proxy(const yomk::Log & /*log*/)
{
    g_proxyHits.fetch_add(1, std::memory_order_relaxed);
    return g_proxyReturn.load(std::memory_order_relaxed);
}

// ============================================================================
// main
// ============================================================================
int main()
{
    std::cout << "=== TestYomkLoggerStress (LG4) ===" << std::endl;

    const uint64_t N = stressScale();
    std::cout << "[CONFIG] YOMK_TEST_STRESS_SCALE = " << N << std::endl;
    // LG5：环境标注先于一切 [BASELINE] 输出，使本次所有性能数据可按环境归因
    std::cout << "[ENV] " << readEnvSnapshot() << std::endl;

    auto server = YOMK_INIT(1);
    CHECK(server != nullptr, "YOMK_INIT 返回非空服务器");

    fs::path tmpDir = fs::temp_directory_path() / ("yomk_logger_lg4_" + std::to_string(::getpid()));
    std::error_code ec;
    fs::create_directories(tmpDir, ec);
    CHECK(!ec, "临时目录创建成功: " + tmpDir.string());

    // ---- 规模派生量（各 Section 共享同一口径：S5 全量回收与 S6 磁盘守恒都按此推算）----
    const uint64_t kShardSize = 1000; // S2 分片粒度（每目录 1000 个 .log）
    const uint64_t shardCount = (N + kShardSize - 1) / kShardSize;
    const uint64_t M = N / 10;                                // S2 单目录对照相规模
    const uint64_t K = (N < 100) ? N : 100;                   // S3 缓冲相 sink logger 数（复用 S2 产物）
    const uint64_t D = (N / 100 == 0) ? 1 : N / 100;          // S4-D 并发 create 规模
    const uint64_t churnRounds = (N / 500 < 2) ? 2 : N / 500; // S5 每线程 churn 轮数
    const uint64_t introspectEvery = (churnRounds / 10 < 1) ? 1 : churnRounds / 10;
    const uint64_t introspectPerThread = (churnRounds + introspectEvery - 1) / introspectEvery;
    const uint64_t kThreads = 8;
    const uint64_t churnTotal = churnRounds * kThreads;
    const fs::path s2Dir = tmpDir / "s2";
    const fs::path s2Single = tmpDir / "s2single";
    std::cout << "[CONFIG] N=" << N << " shard=" << shardCount << " M=" << M << " K=" << K
              << " D=" << D << " churn=" << churnTotal << "(" << kThreads << "x" << churnRounds
              << ", introspect every " << introspectEvery << ")" << std::endl;

    // 分片目录路径预构造（仅 shardCount 条），排除循环内字符串拼接开销
    std::vector<std::string> shardDirs;
    shardDirs.reserve(static_cast<size_t>(shardCount));
    for (uint64_t s = 0; s < shardCount; ++s)
    {
        shardDirs.push_back((s2Dir / ("shard_" + std::to_string(s))).string());
    }

    // 起点基线：库内部不使用 YOMK_INFO 家族宏 → console 表恰为构造函数预置的 MainLogger
    {
        uint64_t c = 0;
        uint64_t f = 0;
        countLoggerLines(unpackLines(YOMK_LOGGER_INFO_LOGGERS()), c, f);
        CHECK((c == 1 && f == 0),
              "起点内省基线 console=1(MainLogger)/file=0（实测 " + std::to_string(c) +
                  "/" + std::to_string(f) + "）");
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

        // 规模-延迟扫描点（P4-c）：console 表从 1 条线性增长到 N+1 条，是全程唯一能覆盖
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
                    if (YomkAPI::CONSOLE_LOG_INFO_TAG(tags[static_cast<size_t>(i)],
                                                      "lg4s1a_marker")
                            .m_status == YomkResponse::eOk)
                    {
                        ++okA;
                    }
                }
                elapsedA += msSince(t0);
            }
            createdA = cp;

            // 扫描数据点：条目 = createdA 个 tag + MainLogger（此刻 file 侧仍为 0）；
            // ALL 首行为级别状态行，故总行数 = 条目数 + 1
            const uint64_t entries = createdA + 1;
            uint64_t badAll = 0;
            uint64_t linesAll = 0;
            auto statAll = measureIntrospection(introspectionRounds(entries), true, badAll, linesAll);
            std::cout << "[BASELINE] S1A_scan_ALL@entries=" << entries << ": avg " << std::fixed
                      << std::setprecision(3) << statAll.avgMs << " ms (min " << statAll.minMs
                      << " / max " << statAll.maxMs << " over " << statAll.count
                      << " calls, lines " << linesAll << ")" << std::endl;
            CHECK((badAll == 0 && linesAll == entries + 1),
                  "S1A: 扫描点 entries=" + std::to_string(entries) + " 内省 eOk + 行数守恒");
        }
        recordBaseline("S1A_console_create_unique_tag", N, elapsedA);
        CHECK(okA == N, "S1A: 唯一 tag 写入 eOk 守恒 " + std::to_string(okA) + "/" + std::to_string(N));

        uint64_t consoleEntries = 0;
        uint64_t fileEntries = 0;
        countLoggerLines(unpackLines(YOMK_LOGGER_INFO_LOGGERS()), consoleEntries, fileEntries);
        CHECK(consoleEntries == N + 1,
              "S1A: console 表条目 = N + MainLogger（实测 " + std::to_string(consoleEntries) + "）");

        // ---- Phase A2：内容守恒抽样（1000 行累积捕获）+ 时间戳格式精确校验（V3）----
        {
            const uint64_t kSample = 1000;
            const uint64_t sample = (kSample < N) ? kSample : N;
            std::string out;
            {
                ThreadSafeCoutCapture cap;
                for (uint64_t i = 0; i < sample; ++i)
                {
                    YomkAPI::CONSOLE_LOG_INFO_TAG(tags[static_cast<size_t>(i)],
                                                  "lg4s1a2_" + std::to_string(i));
                }
                out = cap.str();
            }
            auto markers = collectTailMarkers(out, "lg4s1a2_");
            CHECK(markers.size() == sample,
                  "S1A2: 抽样 marker 全数命中（不丢）" + std::to_string(markers.size()) +
                      "/" + std::to_string(sample));
            CHECK(countBadOccurrence(markers) == 0, "S1A2: 抽样 marker 无重复（不重）");

            uint64_t sampledLines = 0;
            uint64_t badFormat = 0;
            std::istringstream iss(out);
            std::string line;
            while (std::getline(iss, line))
            {
                if (line.find("lg4s1a2_") == std::string::npos)
                {
                    continue; // 框架自身噪声行不参与判定
                }
                ++sampledLines;
                if (!checkTimeFormat(line))
                {
                    ++badFormat;
                }
            }
            CHECK((sampledLines == sample && badFormat == 0),
                  "S1A2: 抽样行时间戳格式全部合法（P4-b 改写等价性，非法 " +
                      std::to_string(badFormat) + " 条）");
        }

        // ---- Phase B：命中路径写入（N 个既有 tag 各 1 次，含时间戳构造 + cout）----
        // P4-b 主验证点：每行都要过一次 localTimeFormatted()
        uint64_t okB = 0;
        double elapsedB = 0.0;
        {
            ThreadSafeCoutCapture cap(true);
            auto t0 = Clock::now();
            for (uint64_t i = 0; i < N; ++i)
            {
                if (YomkAPI::CONSOLE_LOG_INFO_TAG(tags[static_cast<size_t>(i)], "lg4s1b_marker").m_status ==
                    YomkResponse::eOk)
                {
                    ++okB;
                }
            }
            elapsedB = msSince(t0);
        }
        recordBaseline("S1B_console_write_hit", N, elapsedB);
        CHECK(okB == N, "S1B: 命中写 eOk 守恒 " + std::to_string(okB) + "/" + std::to_string(N));

        // ---- Phase C：级别 OFF 短路（开关关断后仍 eOk，但不产生输出）----
        CHECK(YOMK_OFF_CONSOLE_LOG_INFO().m_status == YomkResponse::eOk, "S1C: OFF info 返回 eOk");
        {
            const uint64_t kProbe = 100;
            const uint64_t probe = (kProbe < N) ? kProbe : N;
            std::string outOff;
            {
                ThreadSafeCoutCapture cap;
                for (uint64_t i = 0; i < probe; ++i)
                {
                    YomkAPI::CONSOLE_LOG_INFO_TAG(tags[static_cast<size_t>(i)], "lg4s1c_off_marker");
                }
                outOff = cap.str();
            }
            CHECK(outOff.find("lg4s1c_off_marker") == std::string::npos,
                  "S1C: 级别 OFF 期间无输出（抽样 " + std::to_string(probe) + " 行）");
        }
        uint64_t okC = 0;
        double elapsedC = 0.0;
        {
            ThreadSafeCoutCapture cap(true);
            auto t0 = Clock::now();
            for (uint64_t i = 0; i < N; ++i)
            {
                if (YomkAPI::CONSOLE_LOG_INFO_TAG(tags[static_cast<size_t>(i)], "lg4s1c_marker").m_status ==
                    YomkResponse::eOk)
                {
                    ++okC;
                }
            }
            elapsedC = msSince(t0);
        }
        recordBaseline("S1C_console_level_off_shortcut", N, elapsedC);
        CHECK(okC == N, "S1C: 级别 OFF 短路仍 eOk 守恒 " + std::to_string(okC) + "/" + std::to_string(N));
        CHECK(YOMK_ON_CONSOLE_LOG_INFO().m_status == YomkResponse::eOk, "S1C: 恢复 ON info 返回 eOk");

        // ---- Phase D：proxy 短路（置于 console 末相：本测试安装后不卸载，靠恒置 true 穿透
        // 避免影响后续 Section；P4-e 的卸载语义由 Lifecycle S3 覆盖）----
        CHECK(YOMK_SET_CONSOLE_LOG_PROXY(lg4Proxy).m_status == YomkResponse::eOk,
              "S1D: 安装计数 proxy 返回 eOk");
        g_proxyReturn.store(false);
        g_proxyHits.store(0);
        uint64_t okD = 0;
        double elapsedD = 0.0;
        {
            ThreadSafeCoutCapture cap(true);
            auto t0 = Clock::now();
            for (uint64_t i = 0; i < N; ++i)
            {
                if (YomkAPI::CONSOLE_LOG_INFO_TAG(tags[static_cast<size_t>(i)], "lg4s1d_marker").m_status ==
                    YomkResponse::eOk)
                {
                    ++okD;
                }
            }
            elapsedD = msSince(t0);
        }
        recordBaseline("S1D_console_proxy_shortcut", N, elapsedD);
        CHECK(okD == N, "S1D: proxy 短路仍 eOk 守恒 " + std::to_string(okD) + "/" + std::to_string(N));
        CHECK(g_proxyHits.load() == N,
              "S1D: proxy 命中守恒 " + std::to_string(g_proxyHits.load()) + "/" + std::to_string(N));
        // 恢复穿透态：此后 console 日志仍会真实输出（后续 Section 不校验其内容）
        g_proxyReturn.store(true);
        {
            std::string outThrough;
            {
                ThreadSafeCoutCapture cap;
                YomkAPI::CONSOLE_LOG_INFO_TAG(hotTag, "lg4s1d_through_marker");
                outThrough = cap.str();
            }
            CHECK(outThrough.find("lg4s1d_through_marker") != std::string::npos,
                  "S1D: proxy 穿透态恢复默认输出（P4-e 登记依据）");
        }

        // ---- Phase E：热 tag 重复写（单 logger，ConsoleLogger::m_mutex 恒定同一把）----
        // P4-d 量化点：std::map 在 N 条目下的命中查找开销
        uint64_t okE = 0;
        double elapsedE = 0.0;
        {
            ThreadSafeCoutCapture cap(true);
            auto t0 = Clock::now();
            for (uint64_t i = 0; i < N; ++i)
            {
                if (YomkAPI::CONSOLE_LOG_INFO_TAG(hotTag, "lg4s1e_marker").m_status == YomkResponse::eOk)
                {
                    ++okE;
                }
            }
            elapsedE = msSince(t0);
        }
        recordBaseline("S1E_console_hot_tag", N, elapsedE);
        CHECK(okE == N, "S1E: 热 tag 写 eOk 守恒 " + std::to_string(okE) + "/" + std::to_string(N));

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
                const std::string &dir = shardDirs[static_cast<size_t>(i / kShardSize)];
                if (YOMK_FILE_LOG_CREATE(dir, makeName("lg4s2_fl_", i)).m_status == YomkResponse::eOk)
                {
                    ++okA;
                }
            }
            elapsedA = msSince(t0);
        }
        recordBaseline("S2A_file_create_sharded", N, elapsedA);
        CHECK(okA == N, "S2A: 分片创建 eOk 守恒 " + std::to_string(okA) + "/" + std::to_string(N));

        // ---- Phase B：单目录对照相（M 个 logger 全在同一目录）----
        // 与 Phase A 的每条均摊耗时对比，暴露单目录 inode 膨胀 / create_directories 退化
        uint64_t okB = 0;
        double elapsedB = 0.0;
        {
            ThreadSafeCoutCapture cap(true);
            auto t0 = Clock::now();
            for (uint64_t i = 0; i < M; ++i)
            {
                if (YOMK_FILE_LOG_CREATE(s2Single.string(), makeName("lg4s2_single_", i)).m_status ==
                    YomkResponse::eOk)
                {
                    ++okB;
                }
            }
            elapsedB = msSince(t0);
        }
        recordBaseline("S2B_file_create_singledir", M, elapsedB);
        CHECK(okB == M, "S2B: 单目录创建 eOk 守恒 " + std::to_string(okB) + "/" + std::to_string(M));
        std::cout << "[BASELINE] S2_per_create_us: sharded " << std::fixed << std::setprecision(1)
                  << (elapsedA * 1000.0 / static_cast<double>(N)) << " us vs singledir "
                  << (elapsedB * 1000.0 / static_cast<double>(M)) << " us (" << shardCount
                  << " shards vs 1 dir)" << std::endl;

        // ---- Phase C：磁盘足迹与内省条目双守恒 ----
        const uint64_t diskFiles = countDiskFiles(s2Dir) + countDiskFiles(s2Single);
        CHECK(diskFiles == N + M,
              "S2C: 磁盘 .log 文件数守恒 = N + N/10（实测 " + std::to_string(diskFiles) + "/" +
                  std::to_string(N + M) + "）");

        uint64_t c = 0;
        uint64_t f = 0;
        countLoggerLines(unpackLines(YOMK_LOGGER_INFO_LOGGERS()), c, f);
        CHECK((c == N + 1 && f == N + M),
              "S2C: 内省条目守恒 console=N+1 file=N+N/10（实测 " + std::to_string(c) + "/" +
                  std::to_string(f) + "）");
        fileTotal = f;

        std::cout << "[BASELINE] S2_VmHWM:" << readVmHWM() << std::endl;
    }

    // ========================================================================
    // S3：文件路径写入与大缓冲落盘（P4-a 验证点：write() 单次快照改写）
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
        // P4-b 副验证点：file 侧每行也过一次 localTimeFormatted()
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
                    if (YomkAPI::FILE_LOG_INFO_TAG(sinks[static_cast<size_t>(k)], "lg4s3a",
                                                   "line_" + std::to_string(r))
                            .m_status == YomkResponse::eOk)
                    {
                        ++okA;
                    }
                }
            }
            elapsedA = msSince(t0);
        }
        recordBaseline("S3A_file_buffer_write", totalLines, elapsedA);
        CHECK(okA == totalLines,
              "S3A: 缓冲写 eOk 守恒 " + std::to_string(okA) + "/" + std::to_string(totalLines));
        {
            bool openOk = false;
            const uint64_t before = countFileLines(fs::path(sinkDir) / (sinks[0] + ".log"), openOk);
            CHECK((openOk && before == 0), "S3A: 未 flush 前 sink0 磁盘文件仍 0 行（缓冲语义）");
        }

        // ---- Phase B：单 logger 大缓冲一次落盘（P4-a 主验证点）----
        const fs::path s3Dir = tmpDir / "s3";
        fs::create_directories(s3Dir, ec);
        const std::string bigName = "lg4s3_big";
        CHECK(YOMK_FILE_LOG_CREATE(s3Dir.string(), bigName).m_status == YomkResponse::eOk,
              "S3B: 大缓冲 logger 创建 eOk");
        const fs::path bigPath = s3Dir / (bigName + ".log");
        const uint64_t bigLines = 2 * N;
        uint64_t okB = 0;
        double elapsedB = 0.0;
        {
            ThreadSafeCoutCapture cap(true);
            auto t0 = Clock::now();
            for (uint64_t i = 0; i < bigLines; ++i)
            {
                if (YomkAPI::FILE_LOG_INFO_TAG(bigName, "lg4s3b", "line_" + std::to_string(i))
                        .m_status == YomkResponse::eOk)
                {
                    ++okB;
                }
            }
            elapsedB = msSince(t0);
        }
        recordBaseline("S3B_file_buffer_fill_big", bigLines, elapsedB);
        CHECK(okB == bigLines,
              "S3B: 大缓冲填充 eOk 守恒 " + std::to_string(okB) + "/" + std::to_string(bigLines));

        YomkResponse flushBig(YomkResponse::eInvalid);
        double elapsedFlush = 0.0;
        {
            ThreadSafeCoutCapture cap(true);
            auto t0 = Clock::now();
            flushBig = YOMK_FILE_LOG_WRITE(bigName);
            elapsedFlush = msSince(t0);
        }
        CHECK(flushBig.m_status == YomkResponse::eOk, "S3B: 大缓冲一次落盘 eOk");
        {
            std::error_code sizeEc;
            const uintmax_t bytes = fs::file_size(bigPath, sizeEc);
            const double mb = sizeEc ? 0.0 : static_cast<double>(bytes) / (1024.0 * 1024.0);
            const double mbPerSec = (elapsedFlush > 0.0) ? (mb / (elapsedFlush / 1000.0)) : 0.0;
            std::cout << "[BASELINE] S3B_flush_big: " << std::fixed << std::setprecision(1)
                      << elapsedFlush << " ms / " << std::setprecision(2) << mb << " MB / "
                      << mbPerSec << " MB/s (" << bigLines << " lines)" << std::endl;
        }
        {
            bool openOk = false;
            const uint64_t readBack = countFileLines(bigPath, openOk);
            CHECK((openOk && readBack == bigLines),
                  "S3B: 大缓冲读回行数守恒 " + std::to_string(readBack) + "/" +
                      std::to_string(bigLines));
        }

        // ---- Phase C：K 个 logger 逐个 flush，逐文件行数守恒 ----
        uint64_t okC = 0;
        double elapsedC = 0.0;
        {
            ThreadSafeCoutCapture cap(true);
            auto t0 = Clock::now();
            for (uint64_t k = 0; k < K; ++k)
            {
                if (YOMK_FILE_LOG_WRITE(sinks[static_cast<size_t>(k)]).m_status == YomkResponse::eOk)
                {
                    ++okC;
                }
            }
            elapsedC = msSince(t0);
        }
        recordBaseline("S3C_file_flush_k_loggers", K, elapsedC);
        CHECK(okC == K, "S3C: K 个 logger flush eOk 守恒 " + std::to_string(okC) + "/" + std::to_string(K));
        {
            uint64_t sumLines = 0;
            uint64_t badFiles = 0;
            for (uint64_t k = 0; k < K; ++k)
            {
                bool openOk = false;
                const uint64_t n = countFileLines(fs::path(sinkDir) / (sinks[static_cast<size_t>(k)] + ".log"),
                                                  openOk);
                if (!openOk || n != linesPerSink)
                {
                    ++badFiles;
                }
                sumLines += n;
            }
            CHECK(badFiles == 0,
                  "S3C: 逐 sink 文件行数 = N/K（异常文件 " + std::to_string(badFiles) + " 个）");
            CHECK(sumLines == totalLines,
                  "S3C: K 个文件行数总和守恒 " + std::to_string(sumLines) + "/" +
                      std::to_string(totalLines));
        }

        // ---- Phase D：空缓冲重复 flush（既有语义：文件不增长，writeFileLog 仍 eOk）----
        std::error_code sizeEc;
        const uintmax_t sizeBefore = fs::file_size(bigPath, sizeEc);
        uint64_t okD = 0;
        double elapsedD = 0.0;
        {
            ThreadSafeCoutCapture cap(true);
            auto t0 = Clock::now();
            for (uint64_t i = 0; i < N; ++i)
            {
                if (YOMK_FILE_LOG_WRITE(bigName).m_status == YomkResponse::eOk)
                {
                    ++okD;
                }
            }
            elapsedD = msSince(t0);
        }
        recordBaseline("S3D_file_flush_empty_buffer", N, elapsedD);
        CHECK(okD == N, "S3D: 空缓冲重复 flush 仍 eOk 守恒 " + std::to_string(okD) + "/" + std::to_string(N));
        const uintmax_t sizeAfter = fs::file_size(bigPath, sizeEc);
        CHECK((!sizeEc && sizeAfter == sizeBefore),
              "S3D: 空缓冲 flush 文件不增长（" + std::to_string(sizeAfter) + " == " +
                  std::to_string(sizeBefore) + "）");

        std::cout << "[BASELINE] S3_VmHWM:" << readVmHWM() << std::endl;
    }

    // ========================================================================
    // S4：内省端点在 2N+ 条目下的响应（P4-c 验证点：双锁快照窗口）
    // ========================================================================
    std::cout << "\n--- S4: 内省端点规模响应（P4-c 验证点） ---" << std::endl;
    {
        auto loggersResp = YOMK_LOGGER_INFO_LOGGERS();
        const std::vector<std::string> loggersLines = unpackLines(loggersResp);
        uint64_t c = 0;
        uint64_t f = 0;
        countLoggerLines(loggersLines, c, f);
        const uint64_t entries = c + f;
        std::cout << "[CONFIG] S4 entries = " << entries << " (console " << c << " / file " << f
                  << ") rounds = " << introspectionRounds(entries) << std::endl;
        CHECK((c == N + 1 && f == fileTotal + 1),
              "S4: 全规模条目守恒 console=N+1 file=N+N/10+1（实测 " + std::to_string(c) + "/" +
                  std::to_string(f) + "）");

        // ---- Phase A：全规模延迟（LOGGERS / ALL 各一轮扫描）----
        const uint64_t rounds = introspectionRounds(entries);
        uint64_t badL = 0;
        uint64_t linesL = 0;
        const LatencyStat statL = measureIntrospection(rounds, false, badL, linesL);
        recordLatency("S4A_loggers", statL);
        CHECK((badL == 0 && linesL == entries),
              "S4A: LOGGERS eOk + 行数守恒（" + std::to_string(linesL) + "/" + std::to_string(entries) + "）");
        uint64_t badA = 0;
        uint64_t linesA = 0;
        const LatencyStat statA = measureIntrospection(rounds, true, badA, linesA);
        recordLatency("S4A_all", statA);
        CHECK((badA == 0 && linesA == entries + 1),
              "S4A: ALL eOk + 行数守恒（含状态行，" + std::to_string(linesA) + "/" +
                  std::to_string(entries + 1) + "）");

        // ---- Phase B：ALL 首行精确串 + 数据段与 LOGGERS 逐行一致（P3-a 原子快照可观测等价）----
        const std::vector<std::string> allLines = unpackLines(YOMK_LOGGER_INFO_ALL());
        CHECK((!allLines.empty() &&
               allLines.front() == "console:debug:on info:on warn:on error:on proxy:on"),
              "S4B: ALL 首行状态串精确匹配（proxy 已在 S1D 安装）");
        bool identical = (allLines.size() == entries + 1 && loggersLines.size() == entries);
        for (size_t i = 1; identical && i < allLines.size(); ++i)
        {
            if (allLines[i] != loggersLines[i - 1])
            {
                identical = false;
            }
        }
        CHECK(identical, "S4B: ALL 数据段与 LOGGERS 逐行一致（嵌套双锁原子快照）");

        // ---- Phase C：单查 N 次（偶数查 console 名、奇数查 file 名；两表查找热路径量化点）----
        // LG6（P4-d 量化口径修正）：原 probes = N/100，@N=100000 仅 1000 次、总耗时 0.6-1.2 ms，
        // 低于计时分辨率（doc 头已登记其 860123 → 1626783 ops/s 不可解读）。提升为 N 次后单次
        // 采样进入十毫秒量级（N=20000 修复前实测 11.3-11.9 ms、三次方差 ±2.3%，已远高于计时
        // 分辨率），S4C 遂成为两张表查找热路径的可解读量化口径（LG6 正是以它实测否决了
        // P4-d 的 std::map → std::unordered_map，详见 doc 头与 YomkLogger.h 容器选型注释）：
        // loggerInfo 先取 console 表 shared_lock 查找、miss 后再取 file 表 shared_lock 查找，
        // 恰是两张表的查找热路径。名字域校验：偶数 i 的 lg4s1_tag_i 由 S1A 创建（i ∈ 0..N-1）、
        // 奇数 i 的 lg4s2_fl_i 由 S2A 创建（i ∈ 0..N-1），故 probes 提升到 N 后仍全部命中，
        // CHECK(okProbe == probes) 的守恒语义不变。口径变更须与修复前基线同批采样才可比。
        const uint64_t probes = N;
        uint64_t okProbe = 0;
        double elapsedProbe = 0.0;
        {
            ThreadSafeCoutCapture cap(true);
            auto t0 = Clock::now();
            for (uint64_t i = 0; i < probes; ++i)
            {
                const std::string name = (i % 2 == 0) ? makeName("lg4s1_tag_", i)
                                                      : makeName("lg4s2_fl_", i);
                if (YOMK_LOGGER_INFO_LOGGER(name).m_status == YomkResponse::eOk)
                {
                    ++okProbe;
                }
            }
            elapsedProbe = msSince(t0);
        }
        recordBaseline("S4C_logger_single_query", probes, elapsedProbe);
        CHECK(okProbe == probes,
              "S4C: 单查 eOk 守恒 " + std::to_string(okProbe) + "/" + std::to_string(probes));

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
                if (YOMK_FILE_LOG_CREATE(s4Solo.string(), makeName("lg4s4d_solo_", i)).m_status ==
                    YomkResponse::eOk)
                {
                    ++soloOk;
                }
            }
            soloMs = msSince(t0);
        }
        CHECK(soloOk == D, "S4D: 对照相 create eOk 守恒 " + std::to_string(soloOk) + "/" + std::to_string(D));

        g_s4dStop.store(false);
        g_s4dCreateOk.store(0);
        g_s4dCreateNs.store(0);
        g_s4dSnapCount.store(0);
        g_s4dSnapNonMono.store(0);
        g_s4dSnapBadStatus.store(0);
        std::thread snapThread([]()
                               {
            uint64_t prevLines = 0;
            while (!g_s4dStop.load(std::memory_order_acquire))
            {
                auto resp = YOMK_LOGGER_INFO_ALL();
                if (resp.m_status != YomkResponse::eOk)
                {
                    g_s4dSnapBadStatus.fetch_add(1, std::memory_order_relaxed);
                    continue;
                }
                const uint64_t lines = unpackLines(resp).size();
                g_s4dSnapCount.fetch_add(1, std::memory_order_relaxed);
                if (lines < prevLines)
                {
                    g_s4dSnapNonMono.fetch_add(1, std::memory_order_relaxed);
                }
                prevLines = lines;
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            } });
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
                if (YOMK_FILE_LOG_CREATE(s4Conc.string(), makeName("lg4s4d_fl_", i)).m_status ==
                    YomkResponse::eOk)
                {
                    g_s4dCreateOk.fetch_add(1, std::memory_order_relaxed);
                }
                g_s4dCreateNs.fetch_add(
                    static_cast<uint64_t>(
                        std::chrono::duration<double, std::nano>(Clock::now() - c0).count()),
                    std::memory_order_relaxed);
            }
            concMs = msSince(t0);
        }
        g_s4dStop.store(true, std::memory_order_release);
        snapThread.join();

        const double soloPerUs = soloMs * 1000.0 / static_cast<double>(D);
        const double concPerUs = (static_cast<double>(g_s4dCreateNs.load()) / 1000.0) /
                                 static_cast<double>(D);
        const double ratio = (soloPerUs > 0.0) ? (concPerUs / soloPerUs) : 0.0;
        std::cout << "[BASELINE] S4D_create_latency_us: solo " << std::fixed << std::setprecision(1)
                  << soloPerUs << " vs under_introspection " << concPerUs << " (ratio "
                  << std::setprecision(2) << ratio << ", snapshots " << g_s4dSnapCount.load()
                  << ", wall " << std::setprecision(1) << concMs << " ms)" << std::endl;
        CHECK(g_s4dCreateOk.load() == D,
              "S4D: 并发相 create eOk 守恒 " + std::to_string(g_s4dCreateOk.load()) + "/" +
                  std::to_string(D));
        CHECK(g_s4dSnapBadStatus.load() == 0,
              "S4D: 内省快照无非 eOk（异常 " + std::to_string(g_s4dSnapBadStatus.load()) + " 次）");
        CHECK(g_s4dSnapCount.load() > 0,
              "S4D: 内省线程确实产生快照（" + std::to_string(g_s4dSnapCount.load()) + " 次）");
        CHECK(g_s4dSnapNonMono.load() == 0,
              "S4D: 快照条目数单调不减（P3-a 语义在规模下仍成立，违例 " +
                  std::to_string(g_s4dSnapNonMono.load()) + " 次）");

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
                workers.emplace_back([t, s5DirStr, churnRounds, introspectEvery]()
                                     {
                    waitGate();
                    for (uint64_t r = 0; r < churnRounds; ++r)
                    {
                        const std::string name =
                            "lg4s5_t" + std::to_string(t) + "_r" + std::to_string(r);

                        auto rc = YomkAPI::CONSOLE_LOG_INFO_TAG(name, "lg4s5_console");
                        if (rc.m_status == YomkResponse::eOk)
                            g_s5ConsoleOk.fetch_add(1, std::memory_order_relaxed);
                        else if (!isLegalStatus(rc))
                            g_s5BadStatus.fetch_add(1, std::memory_order_relaxed);

                        auto rf = YOMK_FILE_LOG_CREATE(s5DirStr, name);
                        if (rf.m_status == YomkResponse::eOk)
                            g_s5FileOk.fetch_add(1, std::memory_order_relaxed);
                        else if (!isLegalStatus(rf))
                            g_s5BadStatus.fetch_add(1, std::memory_order_relaxed);

                        auto rl = YomkAPI::FILE_LOG_INFO_TAG(name, "lg4s5", "file_line");
                        if (rl.m_status == YomkResponse::eOk)
                            g_s5LogOk.fetch_add(1, std::memory_order_relaxed);
                        else if (!isLegalStatus(rl))
                            g_s5BadStatus.fetch_add(1, std::memory_order_relaxed);

                        if ((r % introspectEvery) == 0)
                        {
                            auto ri = YOMK_LOGGER_INFO_ALL();
                            if (ri.m_status == YomkResponse::eOk)
                                g_s5IntrospectOk.fetch_add(1, std::memory_order_relaxed);
                            else if (!isLegalStatus(ri))
                                g_s5BadStatus.fetch_add(1, std::memory_order_relaxed);
                        }

                        // 同名同时命中两表：单端点一次删除 console + file
                        auto rd = YOMK_FILE_LOG_DELETE(name);
                        if (rd.m_status == YomkResponse::eOk)
                            g_s5DeleteOk.fetch_add(1, std::memory_order_relaxed);
                        else if (!isLegalStatus(rd))
                            g_s5BadStatus.fetch_add(1, std::memory_order_relaxed);

                        auto rn = YOMK_LOGGER_INFO_LOGGER(name);
                        if (rn.m_status == YomkResponse::eNo)
                            g_s5PostDeleteNo.fetch_add(1, std::memory_order_relaxed);
                        else if (!isLegalStatus(rn))
                            g_s5BadStatus.fetch_add(1, std::memory_order_relaxed);
                    } });
            }
            releaseGate(static_cast<int>(kThreads));
            for (auto &worker : workers)
            {
                worker.join();
            }
            elapsedChurn = msSince(t0);
        }
        const uint64_t introspectTotal = kThreads * introspectPerThread;
        recordBaseline("S5A_mixed_churn", churnTotal * 5 + introspectTotal, elapsedChurn);
        CHECK(g_s5ConsoleOk.load() == churnTotal,
              "S5A: console 写 eOk 守恒 " + std::to_string(g_s5ConsoleOk.load()) + "/" +
                  std::to_string(churnTotal));
        CHECK(g_s5FileOk.load() == churnTotal,
              "S5A: file 建 eOk 守恒 " + std::to_string(g_s5FileOk.load()) + "/" +
                  std::to_string(churnTotal));
        CHECK(g_s5LogOk.load() == churnTotal,
              "S5A: file 写 eOk 守恒 " + std::to_string(g_s5LogOk.load()) + "/" +
                  std::to_string(churnTotal));
        CHECK(g_s5IntrospectOk.load() == introspectTotal,
              "S5A: 内省 eOk 守恒 " + std::to_string(g_s5IntrospectOk.load()) + "/" +
                  std::to_string(introspectTotal));
        CHECK(g_s5DeleteOk.load() == churnTotal,
              "S5A: 删除 eOk 守恒 " + std::to_string(g_s5DeleteOk.load()) + "/" +
                  std::to_string(churnTotal));
        CHECK(g_s5PostDeleteNo.load() == churnTotal,
              "S5A: 删除后单查 eNo 守恒 " + std::to_string(g_s5PostDeleteNo.load()) + "/" +
                  std::to_string(churnTotal));
        CHECK(g_s5BadStatus.load() == 0,
              "S5A: 无非法状态码（异常 " + std::to_string(g_s5BadStatus.load()) + " 次）");
        {
            uint64_t c = 0;
            uint64_t f = 0;
            countLoggerLines(unpackLines(YOMK_LOGGER_INFO_LOGGERS()), c, f);
            CHECK((c == N + 1 && f == fileTotal + 1 + 2 * D),
                  "S5A: churn 后条目净变化 0（实测 console " + std::to_string(c) + " / file " +
                      std::to_string(f) + "）");
        }

        // ---- Phase B：全量回收 S1/S2/S3/S4 遗留的所有 logger ----
        uint64_t delConsoleOk = 0;
        uint64_t delFileOk = 0;
        uint64_t delBad = 0;
        double elapsedDel = 0.0;
        {
            ThreadSafeCoutCapture cap(true);
            auto t0 = Clock::now();
            auto recycle = [&delConsoleOk, &delFileOk, &delBad](const std::string &name,
                                                                bool expectFile)
            {
                auto r = YOMK_FILE_LOG_DELETE(name);
                if (r.m_status == YomkResponse::eOk)
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
        CHECK(delConsoleOk == N,
              "S5B: console 组回收 eOk 守恒 " + std::to_string(delConsoleOk) + "/" + std::to_string(N));
        CHECK(delFileOk == N + M + 1 + 2 * D,
              "S5B: file 组回收 eOk 守恒 " + std::to_string(delFileOk) + "/" +
                  std::to_string(N + M + 1 + 2 * D));
        CHECK(delBad == 0, "S5B: 全量回收无非 eOk（异常 " + std::to_string(delBad) + " 次）");

        // ---- Phase C：残量扫除（MainLogger）→ LOGGERS 行数为 0 ----
        uint64_t residualDeleted = 0;
        {
            ThreadSafeCoutCapture cap(true);
            for (int round = 0; round < 3; ++round)
            {
                const std::vector<std::string> lines = unpackLines(YOMK_LOGGER_INFO_LOGGERS());
                if (lines.empty())
                {
                    break;
                }
                for (const auto &line : lines)
                {
                    const std::string name = parseLoggerName(line);
                    if (name.empty())
                    {
                        continue;
                    }
                    if (YOMK_FILE_LOG_DELETE(name).m_status == YomkResponse::eOk)
                    {
                        ++residualDeleted;
                    }
                }
            }
        }
        const std::vector<std::string> finalLines = unpackLines(YOMK_LOGGER_INFO_LOGGERS());
        CHECK(finalLines.empty(),
              "S5C: 全量回收后 LOGGERS 行数为 0（残量扫除 " + std::to_string(residualDeleted) +
                  " 个，实剩 " + std::to_string(finalLines.size()) + "）");
        {
            const uint64_t probes = (N / 100 == 0) ? 1 : N / 100;
            uint64_t goneConsole = 0;
            uint64_t goneFile = 0;
            {
                ThreadSafeCoutCapture cap(true);
                for (uint64_t i = 0; i < probes; ++i)
                {
                    if (YOMK_LOGGER_INFO_LOGGER(makeName("lg4s1_tag_", i)).m_status == YomkResponse::eNo)
                    {
                        ++goneConsole;
                    }
                    if (YOMK_LOGGER_INFO_LOGGER(makeName("lg4s2_fl_", i)).m_status == YomkResponse::eNo)
                    {
                        ++goneFile;
                    }
                }
            }
            CHECK((goneConsole == probes && goneFile == probes),
                  "S5C: 抽样单查全部 eNo（console " + std::to_string(goneConsole) + " / file " +
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
        CHECK(disk == expectDisk,
              "S6: 清理前磁盘 .log 文件数守恒（实测 " + std::to_string(disk) + "/" +
                  std::to_string(expectDisk) + "）");

        double elapsedShutdown = 0.0;
        {
            ThreadSafeCoutCapture cap(true);
            auto t0 = Clock::now();
            YOMK_SHUTDOWN();
            elapsedShutdown = msSince(t0);
        }
        std::cout << "[BASELINE] S6_shutdown: " << std::fixed << std::setprecision(1)
                  << elapsedShutdown << " ms（规模已在 S5 回收，析构无落盘 I/O 拖累）" << std::endl;

        std::error_code rmEc;
        const uintmax_t removed = fs::remove_all(tmpDir, rmEc);
        CHECK(!rmEc, "S6: remove_all 无错误码（" + rmEc.message() + "）");
        std::error_code existEc;
        CHECK(!fs::exists(tmpDir, existEc),
              "S6: 临时目录已彻底清理（删除条目 " + std::to_string(removed) + "）");

        std::cout << "[BASELINE] S6_VmHWM:" << readVmHWM() << std::endl;
    }

    std::cout << "\n=== Result: " << (g_total - g_failed) << "/" << g_total << " passed, "
              << g_failed << " failed ===" << std::endl;
    return g_failed == 0 ? 0 : 1;
}
