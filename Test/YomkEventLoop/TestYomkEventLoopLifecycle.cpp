/**
 * @file TestYomkEventLoopLifecycle.cpp
 * @brief EventLoop 三段语义（start 重启续跑 / stop 保留队列 / destroy 停后清空）白盒回归测试
 *
 * 覆盖内容（ELC2 缺陷修复后 12 个 Section）：
 * 1. stop 保留性：停止仅退出工作线程，未执行事件保留在队列（不执行、pending 不减）
 * 2. start 续跑性：对已停止未销毁的循环再次 start，积压按原 FIFO 顺序续跑（事件不丢失）
 * 3. destroy 清空性：销毁先停止退出线程再清空队列——排队事件永不执行、重启后无任何执行（不可续跑）
 * 4. start 幂等性：运行中再次 start 不新建线程（事件仍在原工作线程执行）
 * 5. postWait 同步语义：返回即事件已执行完（响应已填充、eventId 已赋）；null 事件返回 1
 * 6. worker 内 postWait 死锁防护：防护路径直接执行内层事件，不自等待
 * 7. run() 异常吞噬与存活：handler 抛 std::exception/未知异常被隔离，循环存活、postWait 不挂
 * 8. 未运行幂等 / 停止态拒收（post/postWait 返回 2）/ 默认处理函数 / infoLine 分支（defaultFunc:on [类型名]、
 *    空 tag "-" 占位、tagCount=0 与超队列长度）
 * 9. API 层路径：YomkAPI::EVENTLOOP_* 宏下停止保留 / 重启续跑 / 销毁移除 / POST_WAIT 同步 / 不存在名 eNo /
 *    停止态投递 eNo / INFO_LOOP 数字路径与越界回退 / INFO_LOOPS 存活列表与销毁后移除 / INFO_ALL /
 *    空循环名与超长循环名边界（ELC6 覆盖率回填：loops() 遍历路径需在 map 非空时调用）
 * 10. postWait×destroy 等待者释放：销毁清空队列时触发被丢弃事件的等待回调，等待者不永久挂起（看门狗）
 * 11. delService("/YomkEventLoop") 残留 loop 收尾与重注册（P4）：运行中删除内置服务——残留 loop 经
 *     析构兜底链收尾（无 deinit override，~YomkEventLoop -> m_loops 析构 -> ~EventLoop -> destroy），
 *     后续 EVENTLOOP API 服务级 eNo；无参重注册（new T(this) 注入）后 START/POST_WAIT 全链路恢复
 * 12. ~EventLoop 析构兜底释放等待者（P4，白盒，看门狗）：不调 destroy 直接析构——~EventLoop 内部
 *     destroy 兜底同样触发被丢弃事件的等待回调，析构入口与显式入口共享释放契约（S10 的入口级补全）
 *
 * 说明：白盒段直连内部类 EventLoop（include Modules/EventLoop/EventLoop.h，CMake 已追加 src 目录），
 *       不触 YomkAPI::init()（类级测试与请求路由无关）；API 段使用 YomkAPI::init() 单例拉起内置 /YomkEventLoop，
 *       末尾 YomkAPI::shutdown() 收尾（call_once 约束，shutdown 后不可再初始化）。
 *       停止/销毁经辅助线程调用（内部 join 等待在途事件收尾）；主线程轮询 infoLine 至 running:off
 *       确认 m_running 已置位后才释放阻塞事件，杜绝工作线程抢跑后续排队事件的竞态（确定性断言）。
 *       阻塞门与观测变量全部为文件级（同 g_asyncMutex 既有 TSan-clean 风格）：阻塞门用原子自旋实现，
 *       规避 std::condition_variable::wait_for 在 TSan 下的已知误报路径与各 section 局部互斥量
 *       栈槽地址复用导致的元数据串扰；工作线程自旋以 1ms 粒度休眠，开销可忽略。
 *       ELC2 已修复并回归：停止态投递可区分返回码（类 API 返回 2 / 宏层 eNo）、越界数字 tagCount 回退默认 3、
 *       postWait×destroy 等待者释放。
 *
 * 风格：纯 main() + 失败计数，返回非 0 表示存在失败用例（零第三方依赖）
 */

#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

// YomkEventLoop.h 传递包含 EventLoop.h（两模块头均无 include guard，不可双包含）
#include "Modules/EventLoop/YomkEventLoop.h"
#include "YomkAPI.h"

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

// ---- 文件级观测变量（文件级生命周期稳定，TSan 下无栈槽复用串扰）----
static std::mutex g_orderMutex;           // 保护事件执行顺序记录
static std::vector<std::string> g_order;  // 事件执行顺序（tag）

static void recordOrder(const std::string& tag)
{
    std::lock_guard<std::mutex> lk(g_orderMutex);
    g_order.push_back(tag);
}

static std::vector<std::string> orderSnapshot()
{
    std::lock_guard<std::mutex> lk(g_orderMutex);
    return g_order;
}

static void resetOrder()
{
    std::lock_guard<std::mutex> lk(g_orderMutex);
    g_order.clear();
}

// ---- 文件级原子阻塞门：占住工作线程制造确定性排队 ----
// 进入（started=true）后自旋等待释放（release=true）；用原子+1ms 休眠自旋，
// 不用 mutex+condition_variable（后者 wait_for 路径在本工具链 TSan 下有已知误报）
static std::atomic<bool> g_gateStarted{false};
static std::atomic<bool> g_gateRelease{false};

static void resetGate()
{
    g_gateStarted.store(false);
    g_gateRelease.store(false);
}

static YomkServiceFunc gateHandler()
{
    return [](YomkPkgPtr)
    {
        g_gateStarted.store(true);
        while (!g_gateRelease.load())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return YomkResponse(YomkResponse::eOk, "gate done");
    };
}

static bool waitGateStarted(int timeoutMs)
{
    for (int i = 0; i < timeoutMs; ++i)
    {
        if (g_gateStarted.load())
        {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return g_gateStarted.load();
}

static void openGate()
{
    g_gateRelease.store(true);
}

// ---- 文件级线程 id 记录（Section 4 幂等性观测）----
static std::mutex g_idsMutex;
static std::vector<std::thread::id> g_ids;

static void recordThreadId()
{
    std::lock_guard<std::mutex> lk(g_idsMutex);
    g_ids.push_back(std::this_thread::get_id());
}

static std::vector<std::thread::id> idsSnapshot()
{
    std::lock_guard<std::mutex> lk(g_idsMutex);
    return g_ids;
}

static void resetIds()
{
    std::lock_guard<std::mutex> lk(g_idsMutex);
    g_ids.clear();
}

// 轮询等待谓词成立，超时返回是否达标
static bool waitUntil(const std::function<bool()>& pred, int timeoutMs)
{
    for (int i = 0; i < timeoutMs; ++i)
    {
        if (pred())
        {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return pred();
}

// 计数事件处理器：记录执行顺序
static YomkServiceFunc countingHandler(const std::string& tag)
{
    return [tag](YomkPkgPtr)
    {
        recordOrder(tag);
        return YomkResponse(YomkResponse::eOk, "counted: " + tag);
    };
}

int main()
{
    // ============ Section 1+2: stop 保留 + start 续跑（白盒）============
    {
        resetOrder();
        resetGate();
        EventLoop loop;

        YOMK_TEST_CHECK(loop.start() == 0, "start 返回 0");

        auto e1 = yomkmsg::Event::shared(yomk::Event("wb", nullptr, gateHandler(), "e1"));
        YOMK_TEST_CHECK(loop.post(e1) == 0, "post 阻塞事件 e1 返回 0");
        YOMK_TEST_CHECK(waitGateStarted(2000), "e1 已进入执行（阻塞占住工作线程）");

        auto e2 = yomkmsg::Event::shared(yomk::Event("wb", nullptr, countingHandler("e2"), "e2"));
        auto e3 = yomkmsg::Event::shared(yomk::Event("wb", nullptr, countingHandler("e3"), "e3"));
        YOMK_TEST_CHECK(loop.post(e2) == 0, "post 计数事件 e2 返回 0");
        YOMK_TEST_CHECK(loop.post(e3) == 0, "post 计数事件 e3 返回 0");

        // 辅助线程停止：stop 内部 join 等待 e1 收尾，须与主线程释放阻塞门配合
        std::thread stopper([&loop] { loop.stop(); });
        // 轮询确认 m_running 已置 false：e1 收尾后工作线程必从 while 条件退出，不会抢跑 e2/e3
        bool stopped =
            waitUntil([&loop] { return loop.infoLine("wb", 3).find("running:off") != std::string::npos; }, 2000);
        YOMK_TEST_CHECK(stopped, "停止过程中 infoLine 观察到 running:off（m_running 已置位）");

        openGate();      // 释放 e1
        stopper.join();  // stop 返回：工作线程已退出

        YOMK_TEST_CHECK(orderSnapshot().empty(), "停止后排队事件 e2/e3 未被执行");
        std::string line = loop.infoLine("wb", 3);
        YOMK_TEST_CHECK(line.find("running:off") != std::string::npos, "停止后 running:off");
        YOMK_TEST_CHECK(line.find("pending:2") != std::string::npos, "停止后 pending:2（未执行事件保留在队列）");

        // 续跑：对已停止未销毁的循环再次 start，积压按原 FIFO 顺序执行
        YOMK_TEST_CHECK(loop.start() == 0, "再次 start 返回 0（重启）");
        bool drained = waitUntil(
            []
            {
                auto o = orderSnapshot();
                return o.size() == 2 && o[0] == "e2" && o[1] == "e3";
            },
            2000);
        YOMK_TEST_CHECK(drained, "重启后积压按 FIFO 顺序续跑完成（e2 -> e3，事件不丢失）");
        bool idle = waitUntil([&loop] { return loop.infoLine("wb", 3).find("pending:0") != std::string::npos; }, 2000);
        YOMK_TEST_CHECK(idle, "续跑完成后 pending:0");

        loop.destroy();  // 显式销毁收尾；析构将再次进入 destroy（幂等，无害）
    }

    // ============ Section 3: destroy 清空（白盒）============
    {
        resetOrder();
        resetGate();
        EventLoop loop;

        YOMK_TEST_CHECK(loop.start() == 0, "start 返回 0");

        auto e1 = yomkmsg::Event::shared(yomk::Event("wb", nullptr, gateHandler(), "b1"));
        YOMK_TEST_CHECK(loop.post(e1) == 0, "post 阻塞事件 b1 返回 0");
        YOMK_TEST_CHECK(waitGateStarted(2000), "阻塞事件 b1 已进入执行");

        auto e2 = yomkmsg::Event::shared(yomk::Event("wb", nullptr, countingHandler("c1"), "c1"));
        auto e3 = yomkmsg::Event::shared(yomk::Event("wb", nullptr, countingHandler("c2"), "c2"));
        YOMK_TEST_CHECK(loop.post(e2) == 0, "post 排队事件 c1 返回 0");
        YOMK_TEST_CHECK(loop.post(e3) == 0, "post 排队事件 c2 返回 0");

        // 辅助线程销毁：destroy = 先 stop（join 等在途收尾）再清空队列
        std::thread destroyer([&loop] { loop.destroy(); });
        bool stopped =
            waitUntil([&loop] { return loop.infoLine("wb", 3).find("running:off") != std::string::npos; }, 2000);
        YOMK_TEST_CHECK(stopped, "销毁过程中 infoLine 观察到 running:off（先停止）");

        openGate();
        destroyer.join();

        YOMK_TEST_CHECK(orderSnapshot().empty(), "销毁后排队事件 c1/c2 永不执行");
        std::string line = loop.infoLine("wb", 3);
        YOMK_TEST_CHECK(line.find("running:off") != std::string::npos, "销毁后 running:off");
        YOMK_TEST_CHECK(line.find("pending:0") != std::string::npos, "销毁后 pending:0（队列已清空）");

        // 不可续跑：destroy 后再 start，无任何事件执行
        YOMK_TEST_CHECK(loop.start() == 0, "destroy 后再 start 返回 0（空转循环）");
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        YOMK_TEST_CHECK(orderSnapshot().empty(), "destroy 后重启无任何事件执行（不可续跑）");

        loop.destroy();
    }

    // ============ Section 4: start 幂等（白盒）============
    {
        resetIds();
        EventLoop loop;
        YOMK_TEST_CHECK(loop.start() == 0, "start 返回 0");

        auto recordId = [](YomkPkgPtr)
        {
            recordThreadId();
            return YomkResponse(YomkResponse::eOk, "id recorded");
        };

        auto e1 = yomkmsg::Event::shared(yomk::Event("wb", nullptr, recordId, "i1"));
        YOMK_TEST_CHECK(loop.post(e1) == 0, "post 事件 i1 返回 0");
        YOMK_TEST_CHECK(waitUntil([] { return idsSnapshot().size() >= 1; }, 2000), "事件 i1 已执行");

        YOMK_TEST_CHECK(loop.start() == 0, "运行中再次 start 返回 0（幂等早退，不新建线程）");

        auto e2 = yomkmsg::Event::shared(yomk::Event("wb", nullptr, recordId, "i2"));
        YOMK_TEST_CHECK(loop.post(e2) == 0, "post 事件 i2 返回 0");
        YOMK_TEST_CHECK(waitUntil([] { return idsSnapshot().size() >= 2; }, 2000), "事件 i2 已执行");

        auto ids = idsSnapshot();
        YOMK_TEST_CHECK(ids.size() == 2 && ids[0] == ids[1], "幂等 start 不换线程（两事件同一工作线程执行）");

        loop.destroy();
    }

    // ============ Section 5: postWait 同步语义（白盒）============
    {
        resetOrder();
        EventLoop loop;
        YOMK_TEST_CHECK(loop.start() == 0, "start 返回 0");

        auto ev = yomkmsg::Event::shared(yomk::Event("wb", nullptr, countingHandler("pw1"), "pw1"));
        YOMK_TEST_CHECK(loop.postWait(ev) == 0, "postWait 返回 0");

        auto order = orderSnapshot();
        YOMK_TEST_CHECK(order.size() == 1 && order.back() == "pw1", "postWait 返回即事件已执行（同步性）");
        YOMK_TEST_CHECK(ev->d.m_response.m_status == YomkResponse::eOk, "postWait 返回时响应已填充");
        YOMK_TEST_CHECK(ev->d.m_eventId > 0, "postWait 事件已赋递增 eventId");
        YOMK_TEST_CHECK(loop.infoLine("wb", 3).find("pending:0") != std::string::npos, "postWait 完成后 pending:0");

        // 异常路径：null 事件
        YOMK_TEST_CHECK(loop.post(nullptr) == 1, "post(null) 返回 1");
        YOMK_TEST_CHECK(loop.postWait(nullptr) == 1, "postWait(null) 返回 1");

        loop.destroy();
    }

    // ============ Section 6: worker 内 postWait 死锁防护（白盒）============
    {
        resetOrder();
        EventLoop loop;
        YOMK_TEST_CHECK(loop.start() == 0, "start 返回 0");

        // 外层事件 handler 在 worker 线程内调用 postWait（内层事件）：
        // 防护路径识别 worker 线程后直接执行内层事件，避免自等待死锁
        auto inner = yomkmsg::Event::shared(yomk::Event("wb", nullptr, countingHandler("inner"), "inner"));
        auto outer = yomkmsg::Event::shared(
            yomk::Event(
                "wb",
                nullptr,
                [&loop, inner](YomkPkgPtr)
                {
                    loop.postWait(inner);
                    recordOrder("outer");
                    return YomkResponse(YomkResponse::eOk, "outer done");
                },
                "outer"));
        YOMK_TEST_CHECK(loop.post(outer) == 0, "post 外层事件返回 0");

        bool done = waitUntil(
            []
            {
                auto o = orderSnapshot();
                return o.size() == 2 && o[0] == "inner" && o[1] == "outer";
            },
            3000);
        YOMK_TEST_CHECK(done, "worker 内 postWait 防护路径依序完成（inner -> outer，不挂起）");
        YOMK_TEST_CHECK(loop.infoLine("wb", 3).find("pending:0") != std::string::npos, "防护路径完成后 pending:0");

        loop.destroy();
    }

    // ============ Section 7: run() 异常吞噬与存活（白盒）============
    {
        resetOrder();
        EventLoop loop;
        YOMK_TEST_CHECK(loop.start() == 0, "start 返回 0");

        // std::exception 分支：postWait 的 handler 抛出，run() 捕获吞噬后仍触发 waitCallback，
        // postWait 不挂起
        auto thrower = yomkmsg::Event::shared(
            yomk::Event(
                "wb", nullptr, [](YomkPkgPtr) -> YomkResponse { throw std::runtime_error("boom"); }, "thr1"));
        YOMK_TEST_CHECK(loop.postWait(thrower) == 0, "handler 抛 std::exception 时 postWait 仍返回 0（异常被吞噬）");

        // 未知异常分支（非 std::exception）
        auto thrower2 = yomkmsg::Event::shared(
            yomk::Event(
                "wb", nullptr, [](YomkPkgPtr) -> YomkResponse { throw 42; }, "thr2"));
        YOMK_TEST_CHECK(loop.post(thrower2) == 0, "post 抛未知异常事件返回 0");

        // 循环存活：后续正常事件仍执行
        auto normal = yomkmsg::Event::shared(yomk::Event("wb", nullptr, countingHandler("alive"), "alive"));
        YOMK_TEST_CHECK(loop.post(normal) == 0, "post 正常事件返回 0");
        YOMK_TEST_CHECK(
            waitUntil([] { return !orderSnapshot().empty() && orderSnapshot().back() == "alive"; }, 2000),
            "异常吞噬后循环存活（后续事件正常执行）");
        YOMK_TEST_CHECK(loop.infoLine("wb", 3).find("pending:0") != std::string::npos, "异常用例后 pending:0");

        loop.destroy();
    }

    // ============ Section 8: 未运行幂等 / 停止态拒收 / 默认函数 / infoLine 分支（白盒）============
    {
        resetOrder();
        resetGate();
        EventLoop loop;

        // 未启动：stop/destroy 幂等早退（无工作线程可 join）
        YOMK_TEST_CHECK(loop.stop() == 0, "未启动时 stop 返回 0（幂等早退）");
        YOMK_TEST_CHECK(loop.destroy() == 0, "未启动时 destroy 返回 0（幂等早退）");

        // 停止态拒收：stop 后投递被拒（返回码 2，与入队成功 0/事件为空 1 区分），事件不入队不执行
        YOMK_TEST_CHECK(loop.start() == 0, "start 返回 0");
        loop.stop();
        auto rejected = yomkmsg::Event::shared(yomk::Event("wb", nullptr, countingHandler("rej"), "rej"));
        YOMK_TEST_CHECK(loop.post(rejected) == 2, "停止态 post 返回 2（投递被拒）");
        auto rejected2 = yomkmsg::Event::shared(yomk::Event("wb", nullptr, countingHandler("rej2"), "rej2"));
        YOMK_TEST_CHECK(loop.postWait(rejected2) == 2, "停止态 postWait 返回 2（被拒且不等待）");
        YOMK_TEST_CHECK(loop.start() == 0, "重启返回 0");
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        YOMK_TEST_CHECK(orderSnapshot().empty(), "停止态投递的事件重启后仍未执行（停止期间投递被拒）");

        // 默认处理函数：事件无 serviceFunc 时由 setDefaultServiceFunc 兜底
        loop.setDefaultServiceFunc(countingHandler("dflt"), "MsgX");
        auto noFunc = yomkmsg::Event::shared(yomk::Event("wb", nullptr, nullptr, ""));
        YOMK_TEST_CHECK(loop.post(noFunc) == 0, "post 无 serviceFunc 事件返回 0");
        YOMK_TEST_CHECK(
            waitUntil([] { return !orderSnapshot().empty() && orderSnapshot().back() == "dflt"; }, 2000),
            "无 serviceFunc 事件由默认处理函数执行");

        // infoLine 分支：defaultFunc:on [MsgX]、空 tag "-" 占位、tagCount=0 与超队列长度
        auto blocker = yomkmsg::Event::shared(yomk::Event("wb", nullptr, gateHandler(), "blk"));
        YOMK_TEST_CHECK(loop.post(blocker) == 0, "post 阻塞事件 blk 返回 0");
        YOMK_TEST_CHECK(waitGateStarted(2000), "阻塞事件 blk 已进入执行");

        YOMK_TEST_CHECK(
            loop.post(yomkmsg::Event::shared(yomk::Event("wb", nullptr, countingHandler("t2"), "t2"))) == 0,
            "post 事件 t2 返回 0");
        YOMK_TEST_CHECK(
            loop.post(yomkmsg::Event::shared(yomk::Event("wb", nullptr, countingHandler("t3"), ""))) == 0,
            "post 空 tag 事件返回 0");

        std::string line = loop.infoLine("wb", 3);
        YOMK_TEST_CHECK(line.find("defaultFunc:on") != std::string::npos, "infoLine 显示 defaultFunc:on");
        YOMK_TEST_CHECK(line.find("[MsgX]") != std::string::npos, "infoLine 附加默认函数类型名 [MsgX]");
        YOMK_TEST_CHECK(line.find("nextNEventTag(3): t2, -") != std::string::npos, "infoLine 空 tag 以 - 占位（t2, -）");
        YOMK_TEST_CHECK(loop.infoLine("wb", 0).find("nextNEventTag(0):") != std::string::npos, "tagCount=0 输出空 tag 集");
        YOMK_TEST_CHECK(
            loop.infoLine("wb", 1000).find("nextNEventTag(1000): t2, -") != std::string::npos,
            "tagCount 超队列长度时全列出");

        openGate();
        loop.destroy();
    }

    // ============ Section 9: API 层停止保留 / 重启续跑 / 销毁移除 / 契约与边界 ============
    {
        auto server = YomkAPI::init(1);
        YOMK_TEST_CHECK(server != nullptr, "YomkAPI::init() 返回非空服务器");

        resetOrder();
        resetGate();

        auto startResp = YomkAPI::EVENTLOOP_START("api_loop", nullptr);
        YOMK_TEST_CHECK(startResp.m_status == YomkResponse::eOk, "START api_loop 返回 eOk");

        auto postResp = YomkAPI::EVENTLOOP_POST("api_loop", yomkmsg::String::shared(std::string("block")), gateHandler(), "b1");
        YOMK_TEST_CHECK(postResp.m_status == YomkResponse::eOk, "POST 阻塞事件 b1 返回 eOk");
        YOMK_TEST_CHECK(waitGateStarted(2000), "阻塞事件 b1 已进入执行");

        YOMK_TEST_CHECK(
            YomkAPI::EVENTLOOP_POST("api_loop", yomkmsg::String::shared(std::string("c1")), countingHandler("c1"), "c1")
                    .m_status == YomkResponse::eOk,
            "POST 计数事件 c1 返回 eOk");
        YOMK_TEST_CHECK(
            YomkAPI::EVENTLOOP_POST("api_loop", yomkmsg::String::shared(std::string("c2")), countingHandler("c2"), "c2")
                    .m_status == YomkResponse::eOk,
            "POST 计数事件 c2 返回 eOk");

        // 辅助线程 STOP：服务层 stop 内部 join 等待在途事件收尾
        std::thread stopper([] { YomkAPI::EVENTLOOP_STOP("api_loop"); });
        auto infoOf = []()
        {
            auto r = YomkAPI::getEventLoopInfo("api_loop");
            auto info = yomk::unPack<yomkmsg::EventLoopInfo>(r.m_data);
            return info;
        };
        YOMK_TEST_CHECK(
            waitUntil([&infoOf] { auto info = infoOf(); return info && !info->d.m_running; }, 2000),
            "API 停止过程中 INFO_LOOP 观察到 running:off");

        openGate();
        stopper.join();

        YOMK_TEST_CHECK(orderSnapshot().empty(), "API 停止后排队事件未执行");
        auto infoStopped = infoOf();
        YOMK_TEST_CHECK(infoStopped && !infoStopped->d.m_running, "API 停止后 running:off");
        YOMK_TEST_CHECK(infoStopped && infoStopped->d.m_pending == 2, "API 停止后 pending:2（未执行事件保留）");

        // 停止态投递被拒（契约回归）：服务层映射 eNo，调用方可区分"已入队"与"被拒"
        YOMK_TEST_CHECK(
            YomkAPI::EVENTLOOP_POST("api_loop", yomkmsg::String::shared(std::string("rx")), countingHandler("rx"), "rx")
                    .m_status == YomkResponse::eNo,
            "停止态 POST 返回 eNo（投递被拒）");
        YOMK_TEST_CHECK(
            YomkAPI::EVENTLOOP_POST_WAIT("api_loop", yomkmsg::String::shared(std::string("rx2")), countingHandler("rx2"), "rx2")
                    .m_status == YomkResponse::eNo,
            "停止态 POST_WAIT 返回 eNo（被拒且不等待）");
        auto infoRejected = infoOf();
        YOMK_TEST_CHECK(infoRejected && infoRejected->d.m_pending == 2, "被拒投递未入队（pending 仍为 2）");

        // 越界数字 tagCount（D3 回归）：stoul 越界不再崩溃，回退默认 3；
        // 冻结窗口队列 2 条（c1/c2），回退 tagCount=3 >= 2 截断不生效全部列出
        auto oob = YomkAPI::getEventLoopInfo("api_loop 99999999999999999999");
        YOMK_TEST_CHECK(oob.m_status == YomkResponse::eOk, "越界数字 tagCount 返回 eOk（不崩溃）");
        YOMK_TEST_CHECK(oob.m_msg == "ok", "越界数字 tagCount 命中 m_msg 恒为 ok");
        auto oobInfo = yomk::unPack<yomkmsg::EventLoopInfo>(oob.m_data);
        YOMK_TEST_CHECK(oobInfo != nullptr, "越界数字 tagCount 解包 EventLoopInfo 成功");
        YOMK_TEST_CHECK(oobInfo && oobInfo->d.m_pending == 2, "越界数字串不影响循环状态（pending 仍为 2）");
        YOMK_TEST_CHECK(oobInfo && oobInfo->d.m_nextTags.size() == 2, "越界数字 tagCount 回退默认 3（队列 2 条全部列出）");
        YOMK_TEST_CHECK(oobInfo && oobInfo->d.m_nextTags[0] == "c1" && oobInfo->d.m_nextTags[1] == "c2",
              "越界数字 tagCount 快照 tag 原值一致（c1/c2）");

        // tagCount 截断语义：N < 队列条数时截断队列头 N 条（冻结窗口队列 2 条）
        auto trunc = YomkAPI::getEventLoopInfo("api_loop", 1);
        auto truncInfo = yomk::unPack<yomkmsg::EventLoopInfo>(trunc.m_data);
        YOMK_TEST_CHECK(truncInfo && truncInfo->d.m_nextTags.size() == 1 && truncInfo->d.m_nextTags[0] == "c1",
              "tagCount=1 截断队列头 1 条 tag");

        // 重启续跑：服务层 START 命中已存在条目即重启，积压按 FIFO 续跑
        YOMK_TEST_CHECK(YomkAPI::EVENTLOOP_START("api_loop", nullptr).m_status == YomkResponse::eOk, "再次 START 返回 eOk（重启）");
        YOMK_TEST_CHECK(
            waitUntil(
                []
                {
                    auto o = orderSnapshot();
                    return o.size() == 2 && o[0] == "c1" && o[1] == "c2";
                },
                2000),
            "API 重启后积压按 FIFO 顺序续跑完成（c1 -> c2）");

        // POST_WAIT 同步路径：返回即事件已执行完，回传 Event 携带执行结果
        auto pw =
            YomkAPI::EVENTLOOP_POST_WAIT("api_loop", yomkmsg::String::shared(std::string("pw1")), countingHandler("pw1"), "pw1");
        YOMK_TEST_CHECK(pw.m_status == YomkResponse::eOk, "POST_WAIT 返回 eOk");
        auto pwEv = yomk::unPack<yomkmsg::Event>(pw.m_data);
        YOMK_TEST_CHECK(pwEv != nullptr, "POST_WAIT 回传 Event 可解包");
        YOMK_TEST_CHECK(pwEv->d.m_tag == "pw1", "POST_WAIT 回传事件 tag 一致");
        YOMK_TEST_CHECK(pwEv->d.m_response.m_status == YomkResponse::eOk, "POST_WAIT 回传事件响应已填充");
        YOMK_TEST_CHECK(!orderSnapshot().empty() && orderSnapshot().back() == "pw1", "POST_WAIT 返回即事件已执行（同步性）");

        // 不存在循环名：五个操作接口均 eNo（不影响既有循环与执行顺序观测）
        YOMK_TEST_CHECK(YomkAPI::EVENTLOOP_STOP("no_such").m_status == YomkResponse::eNo, "STOP 不存在循环返回 eNo");
        YOMK_TEST_CHECK(
            YomkAPI::EVENTLOOP_POST("no_such", yomkmsg::String::shared(std::string("x")), nullptr, "").m_status ==
                YomkResponse::eNo,
            "POST 不存在循环返回 eNo");
        YOMK_TEST_CHECK(
            YomkAPI::EVENTLOOP_POST_WAIT("no_such", yomkmsg::String::shared(std::string("x")), nullptr, "").m_status ==
                YomkResponse::eNo,
            "POST_WAIT 不存在循环返回 eNo");
        YOMK_TEST_CHECK(YomkAPI::EVENTLOOP_DESTROY("no_such").m_status == YomkResponse::eNo, "DESTROY 不存在循环返回 eNo");
        YOMK_TEST_CHECK(YomkAPI::getEventLoopInfo("no_such").m_status == YomkResponse::eNo, "INFO_LOOP 不存在循环返回 eNo");

        // INFO_LOOP 数字 tagCount 路径："name N" 字符串解析与类型化重载、大数值（合法范围）；
        // 结构化返回：解析健壮性体现为 eOk 且 m_msg 恒 "ok"（该窗口队列已清空，截断语义已在冻结窗口验证）
        YOMK_TEST_CHECK(YomkAPI::getEventLoopInfo("api_loop 5").m_msg == "ok", "字符串数字路径解析 tagCount=5");
        YOMK_TEST_CHECK(YomkAPI::getEventLoopInfo("api_loop", 7).m_msg == "ok", "类型化重载解析 tagCount=7");
        YOMK_TEST_CHECK(YomkAPI::getEventLoopInfo("api_loop 1000000").m_msg == "ok", "大数值 tagCount 正常解析（合法范围内）");

        // INFO_LOOPS：api_loop 存活时列表包含其名（loops() 遍历路径；销毁后移除验证见段尾）
        auto loopsAliveResp = YomkAPI::listEventLoops();
        YOMK_TEST_CHECK(loopsAliveResp.m_status == YomkResponse::eOk, "INFO_LOOPS 返回 eOk");
        auto loopsAliveArr = yomk::unPack<yomkmsg::StringArray>(loopsAliveResp.m_data);
        bool loopsFound = false;
        if (loopsAliveArr)
        {
            for (const auto& n : loopsAliveArr->d)
            {
                if (n == "api_loop")
                {
                    loopsFound = true;
                }
            }
        }
        YOMK_TEST_CHECK(loopsAliveArr != nullptr && loopsFound, "INFO_LOOPS 列表包含 api_loop");

        // INFO_ALL：结构化 EventLoopInfoList，包含 api_loop 条目
        auto allResp = YomkAPI::getAllEventLoopInfo();
        YOMK_TEST_CHECK(allResp.m_status == YomkResponse::eOk, "INFO_ALL 返回 eOk");
        YOMK_TEST_CHECK(allResp.m_msg == "ok", "INFO_ALL 成功消息恒为 ok");
        auto allArr = yomk::unPack<yomkmsg::EventLoopInfoList>(allResp.m_data);
        bool allFound = false;
        if (allArr)
        {
            for (const auto& info : allArr->d)
            {
                if (info.m_loopName == "api_loop")
                {
                    allFound = true;
                }
            }
        }
        YOMK_TEST_CHECK(allArr != nullptr && allFound, "INFO_ALL 列表包含 api_loop 条目");

        // 空循环名边界：空串为合法 map 键，全生命周期 eOk
        YOMK_TEST_CHECK(YomkAPI::EVENTLOOP_START("", nullptr).m_status == YomkResponse::eOk, "START 空循环名返回 eOk");
        YOMK_TEST_CHECK(
            YomkAPI::EVENTLOOP_POST("", yomkmsg::String::shared(std::string("x")), countingHandler("empty"), "").m_status ==
                YomkResponse::eOk,
            "POST 空循环名返回 eOk");
        YOMK_TEST_CHECK(
            waitUntil([] { return !orderSnapshot().empty() && orderSnapshot().back() == "empty"; }, 2000),
            "空循环名事件正常执行");
        YOMK_TEST_CHECK(YomkAPI::getEventLoopInfo("").m_status == YomkResponse::eOk, "INFO_LOOP 空循环名返回 eOk");
        YOMK_TEST_CHECK(YomkAPI::EVENTLOOP_DESTROY("").m_status == YomkResponse::eOk, "DESTROY 空循环名返回 eOk");

        // 超长循环名边界（65536 字符）：生命周期各接口 eOk 且结构化返回完整回显
        std::string longName(65536, 'L');
        YOMK_TEST_CHECK(YomkAPI::EVENTLOOP_START(longName, nullptr).m_status == YomkResponse::eOk, "START 超长循环名返回 eOk");
        auto longInfo = YomkAPI::getEventLoopInfo(longName);
        YOMK_TEST_CHECK(longInfo.m_status == YomkResponse::eOk, "INFO_LOOP 超长循环名返回 eOk");
        auto longLoopInfo = yomk::unPack<yomkmsg::EventLoopInfo>(longInfo.m_data);
        YOMK_TEST_CHECK(longLoopInfo && longLoopInfo->d.m_loopName == longName, "INFO_LOOP 完整回显超长循环名");
        YOMK_TEST_CHECK(YomkAPI::EVENTLOOP_DESTROY(longName).m_status == YomkResponse::eOk, "DESTROY 超长循环名返回 eOk");

        // 销毁移除条目
        YOMK_TEST_CHECK(YomkAPI::EVENTLOOP_DESTROY("api_loop").m_status == YomkResponse::eOk, "DESTROY api_loop 返回 eOk");
        auto loopsResp = YomkAPI::listEventLoops();
        YOMK_TEST_CHECK(loopsResp.m_status == YomkResponse::eOk, "INFO_LOOPS 返回 eOk");
        auto loopsArr = yomk::unPack<yomkmsg::StringArray>(loopsResp.m_data);
        bool removed = true;
        if (loopsArr)
        {
            for (const auto& n : loopsArr->d)
            {
                if (n == "api_loop")
                {
                    removed = false;
                }
            }
        }
        YOMK_TEST_CHECK(loopsArr != nullptr && removed, "销毁后 INFO_LOOPS 不再包含 api_loop");
    }

    // ============ Section 11: delService("/YomkEventLoop") 残留 loop 收尾与重注册（API 段，P4）============
    {
        YOMK_TEST_CHECK(YomkAPI::EVENTLOOP_START("/L1").m_status == YomkResponse::eOk, "启动 /L1（运行中）");

        // 运行中删除内置服务：残留 /L1 loop 经析构兜底链收尾（YomkEventLoop 无 deinit override，
        // delService 锁外释放 -> ~YomkEventLoop -> m_loops 析构 -> ~EventLoop -> destroy -> join），无挂起即证
        YOMK_TEST_CHECK(YomkAPI::delService("/YomkEventLoop") == 0, "删除内置 /YomkEventLoop 返回 0");
        auto afterDel = YomkAPI::EVENTLOOP_START("/L2");
        YOMK_TEST_CHECK(afterDel.m_status == YomkResponse::eNo && afterDel.m_msg == "service not found: /YomkEventLoop",
              "删除后 EVENTLOOP API 服务级 eNo + 精确消息");
        auto srvArr = yomk::unPack<yomkmsg::StringArray>(YomkAPI::listServices().m_data);
        bool evtLoopGone = true;
        if (srvArr)
        {
            for (const auto& n : srvArr->d)
            {
                if (n == "/YomkEventLoop")
                {
                    evtLoopGone = false;
                }
            }
        }
        YOMK_TEST_CHECK(srvArr != nullptr && evtLoopGone, "服务表快照不再含 /YomkEventLoop");

        YOMK_TEST_CHECK(YomkAPI::newService<YomkEventLoop>() == 0, "newService 无参重注册返回 0（构造函数固定名生效）");
        YOMK_TEST_CHECK(YomkAPI::EVENTLOOP_START("/L2").m_status == YomkResponse::eOk, "重注册后 EVENTLOOP_START 恢复可用");
        auto pwRelink =
            YomkAPI::EVENTLOOP_POST_WAIT("/L2", yomkmsg::String::shared(std::string("relink")), countingHandler("relink"), "relink");
        YOMK_TEST_CHECK(pwRelink.m_status == YomkResponse::eOk, "重注册后 YomkAPI::EVENTLOOP_POST_WAIT 全链路 eOk");

        YomkAPI::shutdown();
    }

    // ============ Section 10: postWait×destroy 释放等待者（白盒，看门狗防挂起）============
    {
        resetOrder();
        resetGate();
        EventLoop loop;
        YOMK_TEST_CHECK(loop.start() == 0, "start 返回 0");

        // g1 占住工作线程，制造确定性排队窗口
        auto g1 = yomkmsg::Event::shared(yomk::Event("wb", nullptr, gateHandler(), "g1"));
        YOMK_TEST_CHECK(loop.post(g1) == 0, "post 阻塞事件 g1 返回 0");
        YOMK_TEST_CHECK(waitGateStarted(2000), "g1 已进入执行（阻塞占住工作线程）");

        // T1：postWait 计数事件 e2——入队成功（rc1 将为 0）后阻塞等待；e2 排队不执行
        std::atomic<int> rc1{0};
        std::atomic<bool> t1done{false};
        auto e2 = yomkmsg::Event::shared(yomk::Event("wb", nullptr, countingHandler("e2"), "e2"));
        std::thread waiter(
            [&loop, &rc1, &t1done, &e2]
            {
                rc1 = loop.postWait(e2);
                t1done = true;
            });
        YOMK_TEST_CHECK(
            waitUntil([&loop] { return loop.infoLine("wb", 3).find("pending:1") != std::string::npos; }, 2000),
            "e2 已入队（pending:1，T1 进入等待）");
        std::this_thread::sleep_for(std::chrono::milliseconds(
            100));  // 确保 T1 已进入 wait（谓词式 wait 对早触发亦免疫，此处确定性覆盖"唤醒"分支）

        // T2：destroy——stop 置位后 join 等待 g1 收尾，需主线程确认停止置位再释放 gate
        std::thread destroyer([&loop] { loop.destroy(); });
        YOMK_TEST_CHECK(
            waitUntil([&loop] { return loop.infoLine("wb", 3).find("running:off") != std::string::npos; }, 2000),
            "destroy 过程中观察到 running:off（先停止）");

        // 释放 g1：worker 退出 -> destroy 清队列丢弃 e2 并触发其等待回调 -> T1 解除阻塞
        openGate();

        bool released = waitUntil([&t1done] { return t1done.load(); }, 3000);
        YOMK_TEST_CHECK(released, "postWait 等待者被 destroy 释放（3s 看门狗内返回，不挂起）");
        YOMK_TEST_CHECK(rc1.load() == 0, "被丢弃事件入队时已成功（rc1==0，丢弃不改变入队结果）");

        waiter.join();
        destroyer.join();

        bool e2executed = false;
        for (const auto& tag : orderSnapshot())
        {
            if (tag == "e2")
            {
                e2executed = true;
            }
        }
        YOMK_TEST_CHECK(!e2executed, "被丢弃事件 e2 未执行");
        YOMK_TEST_CHECK(loop.infoLine("wb", 3).find("pending:0") != std::string::npos, "destroy 后 pending:0（队列已清空）");

        loop.destroy();  // 幂等收尾（空队列再清一次无害）
    }

    // ============ Section 12: ~EventLoop 析构兜底释放等待者（白盒，看门狗，P4）============
    {
        resetOrder();
        resetGate();
        auto loopHolder = std::make_unique<EventLoop>();
        // 观测句柄：openGate 前 resetter 必然阻塞在 destroy 的 join 上（g1 占住 worker），
        // 析构未完成则对象恒存活，raw 观测无悬垂；openGate 后主线程不再触碰 raw
        EventLoop* raw = loopHolder.get();
        YOMK_TEST_CHECK(raw->start() == 0, "start 返回 0");

        // g1 占住工作线程，制造确定性排队窗口
        auto g1 = yomkmsg::Event::shared(yomk::Event("wb", nullptr, gateHandler(), "g1"));
        YOMK_TEST_CHECK(raw->post(g1) == 0, "post 阻塞事件 g1 返回 0");
        YOMK_TEST_CHECK(waitGateStarted(2000), "g1 已进入执行（阻塞占住工作线程）");

        // T1：postWait 计数事件 e2——入队成功（rc1 将为 0）后阻塞等待；e2 排队不执行
        std::atomic<int> rc1{0};
        std::atomic<bool> t1done{false};
        auto e2 = yomkmsg::Event::shared(yomk::Event("wb", nullptr, countingHandler("e2"), "e2"));
        std::thread waiter(
            [&raw, &rc1, &t1done, &e2]
            {
                rc1 = raw->postWait(e2);
                t1done = true;
            });
        YOMK_TEST_CHECK(
            waitUntil([raw] { return raw->infoLine("wb", 3).find("pending:1") != std::string::npos; }, 2000),
            "e2 已入队（pending:1，T1 进入等待）");
        std::this_thread::sleep_for(std::chrono::milliseconds(
            100));  // 确保 T1 已进入 wait（谓词式 wait 对早触发亦免疫，此处确定性覆盖"唤醒"分支）

        // T2：不调 destroy 直接析构——~EventLoop 内部 destroy 兜底（先停止置位再 join，join 等 g1 收尾）
        std::thread resetter([&loopHolder] { loopHolder.reset(); });
        YOMK_TEST_CHECK(
            waitUntil([raw] { return raw->infoLine("wb", 3).find("running:off") != std::string::npos; }, 2000),
            "析构过程中观察到 running:off（~EventLoop 先走 destroy 停止）");

        // 释放 g1：worker 退出 -> 析构清队列丢弃 e2 并触发其等待回调 -> T1 解除阻塞 -> 析构完成
        openGate();

        bool released = waitUntil([&t1done] { return t1done.load(); }, 3000);
        YOMK_TEST_CHECK(released, "postWait 等待者被析构兜底释放（3s 看门狗内返回，不挂起）");
        YOMK_TEST_CHECK(rc1.load() == 0, "被丢弃事件入队时已成功（rc1==0，丢弃不改变入队结果）");

        waiter.join();
        resetter.join();

        bool e2executed = false;
        for (const auto& tag : orderSnapshot())
        {
            if (tag == "e2")
            {
                e2executed = true;
            }
        }
        YOMK_TEST_CHECK(!e2executed, "被丢弃事件 e2 未执行");
    }

    if (g_failed == 0)
    {
        std::cout << "TestYomkEventLoopLifecycle all check passed." << std::endl;
        return 0;
    }
    else
    {
        std::cout << "TestYomkEventLoopLifecycle FAILED (" << g_failed << " checks failed)." << std::endl;
        return 1;
    }
}
