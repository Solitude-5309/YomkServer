// EventLoop 三段语义（start 重启续跑 / stop 保留队列 / destroy 停后清空）白盒回归测试
//
// 覆盖内容（12 个 Section）：
// 1. stop 保留性：停止仅退出工作线程，未执行事件保留在队列（不执行、pending 不减）
// 2. start 续跑性：对已停止未销毁的循环再次 start，积压按原 FIFO 顺序续跑（事件不丢失）
// 3. destroy 清空性：销毁先停止退出线程再清空队列——排队事件永不执行、重启后无任何执行（不可续跑）
// 4. start 幂等性：运行中再次 start 不新建线程（事件仍在原工作线程执行）
// 5. postWait 同步语义：返回即事件已执行完（响应已填充、eventId 已赋）；null 事件返回 1
// 6. worker 内 postWait 死锁防护：防护路径直接执行内层事件，不自等待
// 7. run() 异常吞噬与存活：handler 抛 std::exception/未知异常被隔离，循环存活、postWait 不挂
// 8. 未运行幂等 / 停止态拒收（post/postWait 返回 2）/ 默认处理函数 / infoSnapshot 类型化分支（默认函数标记与
// 期望类型名、空 tag 原值保留、tagCount=0 与超队列长度）
// 9. API 层路径：event_loop() 域下停止保留 / 重启续跑 / 销毁移除 / post_wait() 同步 / 不存在名 ResStatus::Failed /
// 停止态投递 ResStatus::Failed / get_info() 数字路径与越界回退 / list() 存活列表与销毁后移除 / all() /
// 空循环名与超长循环名边界（loops() 遍历路径需在 map 非空时调用）
// 10. postWait×destroy 等待者释放：销毁清空队列时触发被丢弃事件的等待回调，等待者不永久挂起（看门狗）
// 11. remove("/YomkEventLoop") 残留 loop 收尾与重注册（P4）：运行中删除内置服务——残留 loop 经
// 析构兜底链收尾（无 deinit override，~YomkEventLoop -> m_loops 析构 -> ~EventLoop -> destroy），
// 后续 EVENTLOOP API 服务级 ResStatus::Failed；无参重注册（new T(this) 注入）后 start()/post_wait() 全链路恢复
// 12. ~EventLoop 析构兜底释放等待者（P4，白盒，看门狗）：不调 destroy 直接析构——~EventLoop 内部
// destroy 兜底同样触发被丢弃事件的等待回调，析构入口与显式入口共享释放契约（S10 的入口级补全）
//
// 说明：白盒段直连内部类 EventLoop（include event_loop/EventLoop.h，CMake 已追加 src 目录），
// 不触 yomk::server().init()（类级测试与请求路由无关）；API 段使用 yomk::server().init() 单例拉起内置 /YomkEventLoop，
// 末尾 yomk::server().shutdown() 收尾（call_once 约束，shutdown 后不可再初始化）。
// 停止/销毁经辅助线程调用（内部 join 等待在途事件收尾）；主线程轮询 infoSnapshot 至 m_running==false
// 确认 m_running 已置位后才释放阻塞事件，杜绝工作线程抢跑后续排队事件的竞态（确定性断言）。
// 阻塞门与观测变量全部为文件级（同 g_asyncMutex 既有 TSan-clean 风格）：阻塞门用原子自旋实现，
// 规避 std::condition_variable::wait_for 在 TSan 下的已知误报路径与各 section 局部互斥量
// 栈槽地址复用导致的元数据串扰；工作线程自旋以 1ms 粒度休眠，开销可忽略。
// 契约关键点：停止态投递可区分返回码（内部 EventLoop::post 返回 2 / API 层收敛为 ResStatus::Failed）、
// 大值 tagCount 无回退（类型化直传, 截断不生效全部列出）、postWait×destroy 等待者释放。
//
// 风格：纯 main() + 失败计数，返回非 0 表示存在失败用例（零第三方依赖）

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
#include "event_loop/YomkEventLoop.h"
#include "yomk/event_loop/YomkEventLoopApi.h"
#include "yomk/srv_info/YomkServerInfoApi.h"

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
    return [](YomkMsgPtr)
    {
        g_gateStarted.store(true);
        while (!g_gateRelease.load())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "gate done");
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
// 类型化观测快照（替代 infoLine 人读状态行解析通道）：与内省终结同一锁域取数
static yomk::payload::EventLoopInfo loopSnap(EventLoop& loop, std::size_t tagCount = 3)
{
    yomk::payload::EventLoopInfo info;
    loop.infoSnapshot(info, tagCount);
    return info;
}

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
    return [tag](YomkMsgPtr)
    {
        recordOrder(tag);
        return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "counted: " + tag);
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

        auto e1 = yomk::msgs::Event::shared(yomk::payload::Event("wb", nullptr, gateHandler(), "e1"));
        YOMK_TEST_CHECK(loop.post(e1) == 0, "post 阻塞事件 e1 返回 0");
        YOMK_TEST_CHECK(waitGateStarted(2000), "e1 已进入执行（阻塞占住工作线程）");

        auto e2 = yomk::msgs::Event::shared(yomk::payload::Event("wb", nullptr, countingHandler("e2"), "e2"));
        auto e3 = yomk::msgs::Event::shared(yomk::payload::Event("wb", nullptr, countingHandler("e3"), "e3"));
        YOMK_TEST_CHECK(loop.post(e2) == 0, "post 计数事件 e2 返回 0");
        YOMK_TEST_CHECK(loop.post(e3) == 0, "post 计数事件 e3 返回 0");

        // 辅助线程停止：stop 内部 join 等待 e1 收尾，须与主线程释放阻塞门配合
        std::thread stopper([&loop] { loop.stop(); });
        // 轮询确认 m_running 已置 false：e1 收尾后工作线程必从 while 条件退出，不会抢跑 e2/e3
        bool stopped =
            waitUntil([&loop] { return !loopSnap(loop).running; }, 2000);
        YOMK_TEST_CHECK(stopped, "停止过程中快照观察到 m_running==false");

        openGate();      // 释放 e1
        stopper.join();  // stop 返回：工作线程已退出

        YOMK_TEST_CHECK(orderSnapshot().empty(), "停止后排队事件 e2/e3 未被执行");
        auto stopSnap = loopSnap(loop);
        YOMK_TEST_CHECK(!stopSnap.running, "停止后 m_running==false");
        YOMK_TEST_CHECK(stopSnap.pending == 2, "停止后 pending==2（未执行事件保留在队列）");

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
        bool idle = waitUntil([&loop] { return loopSnap(loop).pending == 0; }, 2000);
        YOMK_TEST_CHECK(idle, "续跑完成后 pending==0");

        loop.destroy();  // 显式销毁收尾；析构将再次进入 destroy（幂等，无害）
    }

    // ============ Section 3: destroy 清空（白盒）============
    {
        resetOrder();
        resetGate();
        EventLoop loop;

        YOMK_TEST_CHECK(loop.start() == 0, "start 返回 0");

        auto e1 = yomk::msgs::Event::shared(yomk::payload::Event("wb", nullptr, gateHandler(), "b1"));
        YOMK_TEST_CHECK(loop.post(e1) == 0, "post 阻塞事件 b1 返回 0");
        YOMK_TEST_CHECK(waitGateStarted(2000), "阻塞事件 b1 已进入执行");

        auto e2 = yomk::msgs::Event::shared(yomk::payload::Event("wb", nullptr, countingHandler("c1"), "c1"));
        auto e3 = yomk::msgs::Event::shared(yomk::payload::Event("wb", nullptr, countingHandler("c2"), "c2"));
        YOMK_TEST_CHECK(loop.post(e2) == 0, "post 排队事件 c1 返回 0");
        YOMK_TEST_CHECK(loop.post(e3) == 0, "post 排队事件 c2 返回 0");

        // 辅助线程销毁：destroy = 先 stop（join 等在途收尾）再清空队列
        std::thread destroyer([&loop] { loop.destroy(); });
        bool stopped =
            waitUntil([&loop] { return !loopSnap(loop).running; }, 2000);
        YOMK_TEST_CHECK(stopped, "销毁过程中快照观察到 m_running==false（先停止）");

        openGate();
        destroyer.join();

        YOMK_TEST_CHECK(orderSnapshot().empty(), "销毁后排队事件 c1/c2 永不执行");
        auto destroySnap = loopSnap(loop);
        YOMK_TEST_CHECK(!destroySnap.running, "销毁后 m_running==false");
        YOMK_TEST_CHECK(destroySnap.pending == 0, "销毁后 pending==0（队列已清空）");

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

        auto recordId = [](YomkMsgPtr)
        {
            recordThreadId();
            return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "id recorded");
        };

        auto e1 = yomk::msgs::Event::shared(yomk::payload::Event("wb", nullptr, recordId, "i1"));
        YOMK_TEST_CHECK(loop.post(e1) == 0, "post 事件 i1 返回 0");
        YOMK_TEST_CHECK(waitUntil([] { return idsSnapshot().size() >= 1; }, 2000), "事件 i1 已执行");

        YOMK_TEST_CHECK(loop.start() == 0, "运行中再次 start 返回 0（幂等早退，不新建线程）");

        auto e2 = yomk::msgs::Event::shared(yomk::payload::Event("wb", nullptr, recordId, "i2"));
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

        auto ev = yomk::msgs::Event::shared(yomk::payload::Event("wb", nullptr, countingHandler("pw1"), "pw1"));
        YOMK_TEST_CHECK(loop.postWait(ev) == 0, "postWait 返回 0");

        auto order = orderSnapshot();
        YOMK_TEST_CHECK(order.size() == 1 && order.back() == "pw1", "postWait 返回即事件已执行（同步性）");
        YOMK_TEST_CHECK(ev->d.response.status == yomk::payload::ResStatus::Succeeded, "postWait 返回时响应已填充");
        YOMK_TEST_CHECK(ev->d.eventId > 0, "postWait 事件已赋递增 eventId");
        YOMK_TEST_CHECK(loopSnap(loop).pending == 0, "postWait 完成后 pending==0");

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
        auto inner = yomk::msgs::Event::shared(yomk::payload::Event("wb", nullptr, countingHandler("inner"), "inner"));
        auto outer = yomk::msgs::Event::shared(
            yomk::payload::Event(
                "wb",
                nullptr,
                [&loop, inner](YomkMsgPtr)
                {
                    loop.postWait(inner);
                    recordOrder("outer");
                    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "outer done");
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
        YOMK_TEST_CHECK(loopSnap(loop).pending == 0, "防护路径完成后 pending==0");

        loop.destroy();
    }

    // ============ Section 7: run() 异常吞噬与存活（白盒）============
    {
        resetOrder();
        EventLoop loop;
        YOMK_TEST_CHECK(loop.start() == 0, "start 返回 0");

        // std::exception 分支：postWait 的 handler 抛出，run() 捕获吞噬后仍触发 waitCallback，
        // postWait 不挂起
        auto thrower = yomk::msgs::Event::shared(
            yomk::payload::Event(
                "wb", nullptr, [](YomkMsgPtr) -> yomk::payload::Response { throw std::runtime_error("boom"); }, "thr1"));
        YOMK_TEST_CHECK(loop.postWait(thrower) == 0, "handler 抛 std::exception 时 postWait 仍返回 0（异常被吞噬）");

        // 未知异常分支（非 std::exception）
        auto thrower2 = yomk::msgs::Event::shared(
            yomk::payload::Event(
                "wb", nullptr, [](YomkMsgPtr) -> yomk::payload::Response { throw 42; }, "thr2"));
        YOMK_TEST_CHECK(loop.post(thrower2) == 0, "post 抛未知异常事件返回 0");

        // 循环存活：后续正常事件仍执行
        auto normal = yomk::msgs::Event::shared(yomk::payload::Event("wb", nullptr, countingHandler("alive"), "alive"));
        YOMK_TEST_CHECK(loop.post(normal) == 0, "post 正常事件返回 0");
        YOMK_TEST_CHECK(
            waitUntil([] { return !orderSnapshot().empty() && orderSnapshot().back() == "alive"; }, 2000),
            "异常吞噬后循环存活（后续事件正常执行）");
        YOMK_TEST_CHECK(loopSnap(loop).pending == 0, "异常用例后 pending==0");

        loop.destroy();
    }

    // ============ Section 8: 未运行幂等 / 停止态拒收 / 默认函数 / infoSnapshot 类型化分支（白盒）============
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
        auto rejected = yomk::msgs::Event::shared(yomk::payload::Event("wb", nullptr, countingHandler("rej"), "rej"));
        YOMK_TEST_CHECK(loop.post(rejected) == 2, "停止态 post 返回 2（投递被拒）");
        auto rejected2 = yomk::msgs::Event::shared(yomk::payload::Event("wb", nullptr, countingHandler("rej2"), "rej2"));
        YOMK_TEST_CHECK(loop.postWait(rejected2) == 2, "停止态 postWait 返回 2（被拒且不等待）");
        YOMK_TEST_CHECK(loop.start() == 0, "重启返回 0");
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        YOMK_TEST_CHECK(orderSnapshot().empty(), "停止态投递的事件重启后仍未执行（停止期间投递被拒）");

        // 默认处理函数：事件无 serviceFunc 时由 setDefaultServiceFunc 兜底
        loop.setDefaultServiceFunc(countingHandler("dflt"), "MsgX");
        auto noFunc = yomk::msgs::Event::shared(yomk::payload::Event("wb", nullptr, nullptr, ""));
        YOMK_TEST_CHECK(loop.post(noFunc) == 0, "post 无 serviceFunc 事件返回 0");
        YOMK_TEST_CHECK(
            waitUntil([] { return !orderSnapshot().empty() && orderSnapshot().back() == "dflt"; }, 2000),
            "无 serviceFunc 事件由默认处理函数执行");

        // infoSnapshot 分支：defaultFunc 标记与期望类型名 MsgX、空 tag 原值保留、tagCount=0 与超队列长度
        auto blocker = yomk::msgs::Event::shared(yomk::payload::Event("wb", nullptr, gateHandler(), "blk"));
        YOMK_TEST_CHECK(loop.post(blocker) == 0, "post 阻塞事件 blk 返回 0");
        YOMK_TEST_CHECK(waitGateStarted(2000), "阻塞事件 blk 已进入执行");

        YOMK_TEST_CHECK(
            loop.post(yomk::msgs::Event::shared(yomk::payload::Event("wb", nullptr, countingHandler("t2"), "t2"))) == 0,
            "post 事件 t2 返回 0");
        YOMK_TEST_CHECK(
            loop.post(yomk::msgs::Event::shared(yomk::payload::Event("wb", nullptr, countingHandler("t3"), ""))) == 0,
            "post 空 tag 事件返回 0");

        // 花括号初始化列表的逗号对预处理器不可见，期望集先落局部变量再入宏
        const std::vector<std::string> expectTags{"t2", ""};
        auto s8Snap = loopSnap(loop);
        YOMK_TEST_CHECK(s8Snap.hasDefaultFunc, "快照显示已挂默认处理函数");
        YOMK_TEST_CHECK(s8Snap.msgName == "MsgX", "快照携带默认函数期望类型名 MsgX");
        YOMK_TEST_CHECK(s8Snap.nextTags == expectTags, "空 tag 以原值保留（t2, 空串）");
        YOMK_TEST_CHECK(loopSnap(loop, 0).nextTags.empty(), "tagCount=0 采集空 tag 集");
        YOMK_TEST_CHECK(loopSnap(loop, 1000).nextTags == expectTags, "tagCount 超队列长度时全列出");

        openGate();
        loop.destroy();
    }

    // ============ Section 9: API 层停止保留 / 重启续跑 / 销毁移除 / 契约与边界 ============
    {
        yomk::server().async_thread(1).init();
        auto server = yomk::server().instance();
        YOMK_TEST_CHECK(server != nullptr, "yomk::server().init() 返回非空服务器");

        resetOrder();
        resetGate();

        YOMK_TEST_CHECK(yomk::event_loop().loop("api_loop").handle_fn(nullptr).start() == yomk::payload::ResStatus::Succeeded, "START api_loop 返回 ResStatus::Succeeded");

        YOMK_TEST_CHECK(
            yomk::event_loop().msg(yomk::msgs::String::shared(std::string("block"))).loop("api_loop").handle_fn(gateHandler()).tag("b1").post() ==
                yomk::payload::ResStatus::Succeeded,
            "POST 阻塞事件 b1 返回 ResStatus::Succeeded");
        YOMK_TEST_CHECK(waitGateStarted(2000), "阻塞事件 b1 已进入执行");

        YOMK_TEST_CHECK(
            yomk::event_loop().msg(yomk::msgs::String::shared(std::string("c1"))).loop("api_loop").handle_fn(countingHandler("c1")).tag("c1").post() ==
                yomk::payload::ResStatus::Succeeded,
            "POST 计数事件 c1 返回 ResStatus::Succeeded");
        YOMK_TEST_CHECK(
            yomk::event_loop().msg(yomk::msgs::String::shared(std::string("c2"))).loop("api_loop").handle_fn(countingHandler("c2")).tag("c2").post() ==
                yomk::payload::ResStatus::Succeeded,
            "POST 计数事件 c2 返回 ResStatus::Succeeded");

        // 辅助线程 STOP：服务层 stop 内部 join 等待在途事件收尾
        std::thread stopper([] { yomk::event_loop().loop("api_loop").stop(); });
        auto infoOf = []()
        {
            auto r = yomk::event_loop().loop("api_loop").get_info();
            auto info = yomk::msgs::unpack<yomk::msgs::EventLoopInfo>(r.data);
            return info;
        };
        YOMK_TEST_CHECK(
            waitUntil([&infoOf] { auto info = infoOf(); return info && !info->d.running; }, 2000),
            "API 停止过程中 get_info() 观察到 m_running==false");

        openGate();
        stopper.join();

        YOMK_TEST_CHECK(orderSnapshot().empty(), "API 停止后排队事件未执行");
        auto infoStopped = infoOf();
        YOMK_TEST_CHECK(infoStopped && !infoStopped->d.running, "API 停止后 m_running==false");
        YOMK_TEST_CHECK(infoStopped && infoStopped->d.pending == 2, "API 停止后 pending==2（未执行事件保留）");

        // 停止态投递被拒（契约回归）：服务层映射 ResStatus::Failed，调用方可区分"已入队"与"被拒"
        YOMK_TEST_CHECK(
            yomk::event_loop().msg(yomk::msgs::String::shared(std::string("rx"))).loop("api_loop").handle_fn(countingHandler("rx")).tag("rx").post() ==
                yomk::payload::ResStatus::Failed,
            "停止态 POST 返回 ResStatus::Failed（投递被拒）");
        YOMK_TEST_CHECK(
            yomk::event_loop().msg(yomk::msgs::String::shared(std::string("rx2"))).loop("api_loop").handle_fn(countingHandler("rx2")).tag("rx2").post_wait() ==
                yomk::payload::ResStatus::Failed,
            "停止态 post_wait() 返回 ResStatus::Failed（被拒且不等待）");
        auto infoRejected = infoOf();
        YOMK_TEST_CHECK(infoRejected && infoRejected->d.pending == 2, "被拒投递未入队（pending 仍为 2）");

        // 类型化 tagCount：size_t 传参（1e18 在合法范围内），无越界回退路径；
        // 冻结窗口队列 2 条（c1/c2），大 tagCount >= 2 截断不生效全部列出
        auto oob = yomk::event_loop().loop("api_loop").tag_count(1000000000000000000ULL).get_info();
        YOMK_TEST_CHECK(oob.status == yomk::payload::ResStatus::Succeeded, "越界数字 tagCount 返回 ResStatus::Succeeded（不崩溃）");
        YOMK_TEST_CHECK(oob.msg == "ok", "越界数字 tagCount 命中 m_msg 恒为 ok");
        auto oobInfo = yomk::msgs::unpack<yomk::msgs::EventLoopInfo>(oob.data);
        YOMK_TEST_CHECK(oobInfo != nullptr, "越界数字 tagCount 解包 EventLoopInfo 成功");
        YOMK_TEST_CHECK(oobInfo && oobInfo->d.pending == 2, "越界数字串不影响循环状态（pending 仍为 2）");
        YOMK_TEST_CHECK(oobInfo && oobInfo->d.nextTags.size() == 2, "越界数字 tagCount 回退默认 3（队列 2 条全部列出）");
        YOMK_TEST_CHECK(oobInfo && oobInfo->d.nextTags[0] == "c1" && oobInfo->d.nextTags[1] == "c2",
              "越界数字 tagCount 快照 tag 原值一致（c1/c2）");

        // tagCount 截断语义：N < 队列条数时截断队列头 N 条（冻结窗口队列 2 条）
        auto trunc = yomk::event_loop().loop("api_loop").tag_count(1).get_info();
        auto truncInfo = yomk::msgs::unpack<yomk::msgs::EventLoopInfo>(trunc.data);
        YOMK_TEST_CHECK(truncInfo && truncInfo->d.nextTags.size() == 1 && truncInfo->d.nextTags[0] == "c1",
              "tagCount=1 截断队列头 1 条 tag");

        // 重启续跑：服务层 START 命中已存在条目即重启，积压按 FIFO 续跑
        YOMK_TEST_CHECK(yomk::event_loop().loop("api_loop").handle_fn(nullptr).start() == yomk::payload::ResStatus::Succeeded, "再次 START 返回 ResStatus::Succeeded（重启）");
        YOMK_TEST_CHECK(
            waitUntil(
                []
                {
                    auto o = orderSnapshot();
                    return o.size() == 2 && o[0] == "c1" && o[1] == "c2";
                },
                2000),
            "API 重启后积压按 FIFO 顺序续跑完成（c1 -> c2）");

        // post_wait() 同步路径：14-4 起 API 仅回 ResStatus（回传 Event 载荷不再经 API 暴露），返回即事件已执行完
        YOMK_TEST_CHECK(
            yomk::event_loop().msg(yomk::msgs::String::shared(std::string("pw1"))).loop("api_loop").handle_fn(countingHandler("pw1")).tag("pw1").post_wait() ==
                yomk::payload::ResStatus::Succeeded,
            "post_wait() 返回 ResStatus::Succeeded");
        YOMK_TEST_CHECK(!orderSnapshot().empty() && orderSnapshot().back() == "pw1", "post_wait() 返回即事件已执行（同步性）");

        // 不存在循环名：五个操作接口均 ResStatus::Failed（不影响既有循环与执行顺序观测）
        YOMK_TEST_CHECK(yomk::event_loop().loop("no_such").stop() == yomk::payload::ResStatus::Failed, "STOP 不存在循环返回 ResStatus::Failed");
        YOMK_TEST_CHECK(
            yomk::event_loop().msg(yomk::msgs::String::shared(std::string("x"))).loop("no_such").handle_fn(nullptr).tag("").post() ==
                yomk::payload::ResStatus::Failed,
            "POST 不存在循环返回 ResStatus::Failed");
        YOMK_TEST_CHECK(
            yomk::event_loop().msg(yomk::msgs::String::shared(std::string("x"))).loop("no_such").handle_fn(nullptr).tag("").post_wait() ==
                yomk::payload::ResStatus::Failed,
            "post_wait() 不存在循环返回 ResStatus::Failed");
        YOMK_TEST_CHECK(yomk::event_loop().loop("no_such").destroy() == yomk::payload::ResStatus::Failed, "destroy() 不存在循环返回 ResStatus::Failed");
        YOMK_TEST_CHECK(yomk::event_loop().loop("no_such").get_info().status == yomk::payload::ResStatus::Failed, "get_info() 不存在循环返回 ResStatus::Failed");

        // get_info() 类型化查询：载荷为 EventLoopQuery
        YOMK_TEST_CHECK(yomk::event_loop().loop("api_loop").tag_count(7).get_info().msg == "ok", "类型化重载 tagCount=7");
        YOMK_TEST_CHECK(
            yomk::event_loop().loop("api_loop 5").get_info().status == yomk::payload::ResStatus::Failed,
            "数字后缀名称按字面量精确查询（不再解析空格编码）");

        // list()：api_loop 存活时列表包含其名（loops() 遍历路径；销毁后移除验证见段尾）
        auto loopsAliveResp = yomk::event_loop().list();
        YOMK_TEST_CHECK(loopsAliveResp.status == yomk::payload::ResStatus::Succeeded, "list() 返回 ResStatus::Succeeded");
        auto loopsAliveArr = yomk::msgs::unpack<yomk::msgs::StringArray>(loopsAliveResp.data);
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
        YOMK_TEST_CHECK(loopsAliveArr != nullptr && loopsFound, "list() 列表包含 api_loop");

        // all()：结构化 EventLoopInfoList，包含 api_loop 条目
        auto allResp = yomk::event_loop().all();
        YOMK_TEST_CHECK(allResp.status == yomk::payload::ResStatus::Succeeded, "all() 返回 ResStatus::Succeeded");
        YOMK_TEST_CHECK(allResp.msg == "ok", "all() 成功消息恒为 ok");
        auto allArr = yomk::msgs::unpack<yomk::msgs::EventLoopInfoList>(allResp.data);
        bool allFound = false;
        if (allArr)
        {
            for (const auto& info : allArr->d)
            {
                if (info.loopName == "api_loop")
                {
                    allFound = true;
                }
            }
        }
        YOMK_TEST_CHECK(allArr != nullptr && allFound, "all() 列表包含 api_loop 条目");

        // 空循环名边界：空串为合法 map 键，全生命周期 ResStatus::Succeeded
        YOMK_TEST_CHECK(yomk::event_loop().loop("").handle_fn(nullptr).start() == yomk::payload::ResStatus::Succeeded, "START 空循环名返回 ResStatus::Succeeded");
        YOMK_TEST_CHECK(
            yomk::event_loop().msg(yomk::msgs::String::shared(std::string("x"))).loop("").handle_fn(countingHandler("empty")).tag("").post() ==
                yomk::payload::ResStatus::Succeeded,
            "POST 空循环名返回 ResStatus::Succeeded");
        YOMK_TEST_CHECK(
            waitUntil([] { return !orderSnapshot().empty() && orderSnapshot().back() == "empty"; }, 2000),
            "空循环名事件正常执行");
        YOMK_TEST_CHECK(yomk::event_loop().loop("").get_info().status == yomk::payload::ResStatus::Succeeded, "get_info() 空循环名返回 ResStatus::Succeeded");
        YOMK_TEST_CHECK(yomk::event_loop().loop("").destroy() == yomk::payload::ResStatus::Succeeded, "destroy() 空循环名返回 ResStatus::Succeeded");

        // 超长循环名边界（65536 字符）：生命周期各接口 ResStatus::Succeeded 且结构化返回完整回显
        std::string longName(65536, 'L');
        YOMK_TEST_CHECK(yomk::event_loop().loop(longName).handle_fn(nullptr).start() == yomk::payload::ResStatus::Succeeded, "START 超长循环名返回 ResStatus::Succeeded");
        auto longInfo = yomk::event_loop().loop(longName).get_info();
        YOMK_TEST_CHECK(longInfo.status == yomk::payload::ResStatus::Succeeded, "get_info() 超长循环名返回 ResStatus::Succeeded");
        auto longLoopInfo = yomk::msgs::unpack<yomk::msgs::EventLoopInfo>(longInfo.data);
        YOMK_TEST_CHECK(longLoopInfo && longLoopInfo->d.loopName == longName, "get_info() 完整回显超长循环名");
        YOMK_TEST_CHECK(yomk::event_loop().loop(longName).destroy() == yomk::payload::ResStatus::Succeeded, "destroy() 超长循环名返回 ResStatus::Succeeded");

        // 销毁移除条目
        YOMK_TEST_CHECK(yomk::event_loop().loop("api_loop").destroy() == yomk::payload::ResStatus::Succeeded, "destroy() api_loop 返回 ResStatus::Succeeded");
        auto loopsResp = yomk::event_loop().list();
        YOMK_TEST_CHECK(loopsResp.status == yomk::payload::ResStatus::Succeeded, "list() 返回 ResStatus::Succeeded");
        auto loopsArr = yomk::msgs::unpack<yomk::msgs::StringArray>(loopsResp.data);
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
        YOMK_TEST_CHECK(loopsArr != nullptr && removed, "销毁后 list() 不再包含 api_loop");
    }

    // ============ Section 11: remove("/YomkEventLoop") 残留 loop 收尾与重注册（API 段，P4）============
    {
        YOMK_TEST_CHECK(yomk::event_loop().loop("/L1").start() == yomk::payload::ResStatus::Succeeded, "启动 /L1（运行中）");

        // 运行中删除内置服务：残留 /L1 loop 经析构兜底链收尾（YomkEventLoop 无 deinit override，
        // remove() 锁外释放 -> ~YomkEventLoop -> m_loops 析构 -> ~EventLoop -> destroy -> join），无挂起即证
        YOMK_TEST_CHECK(yomk::server().srv_name("/YomkEventLoop").remove() == 0, "删除内置 /YomkEventLoop 返回 0");
        YOMK_TEST_CHECK(yomk::event_loop().loop("/L2").start() == yomk::payload::ResStatus::Failed,
              "删除后 EVENTLOOP API 服务级 ResStatus::Failed（失败原因经错误日志承载）");
        auto srvArr = yomk::msgs::unpack<yomk::msgs::StringArray>(yomk::srv_info().services().data);
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

        YOMK_TEST_CHECK(yomk::server().srv<YomkEventLoop>().create() == 0, "create() 无参重注册返回 0（构造函数固定名生效）");
        YOMK_TEST_CHECK(yomk::event_loop().loop("/L2").start() == yomk::payload::ResStatus::Succeeded, "重注册后 start() 恢复可用");
        YOMK_TEST_CHECK(
            yomk::event_loop().msg(yomk::msgs::String::shared(std::string("relink"))).loop("/L2").handle_fn(countingHandler("relink")).tag("relink").post_wait() ==
                yomk::payload::ResStatus::Succeeded,
            "重注册后 post_wait() 全链路 ResStatus::Succeeded");

        yomk::server().shutdown();
    }

    // ============ Section 10: postWait×destroy 释放等待者（白盒，看门狗防挂起）============
    {
        resetOrder();
        resetGate();
        EventLoop loop;
        YOMK_TEST_CHECK(loop.start() == 0, "start 返回 0");

        // g1 占住工作线程，制造确定性排队窗口
        auto g1 = yomk::msgs::Event::shared(yomk::payload::Event("wb", nullptr, gateHandler(), "g1"));
        YOMK_TEST_CHECK(loop.post(g1) == 0, "post 阻塞事件 g1 返回 0");
        YOMK_TEST_CHECK(waitGateStarted(2000), "g1 已进入执行（阻塞占住工作线程）");

        // T1：postWait 计数事件 e2——入队成功（rc1 将为 0）后阻塞等待；e2 排队不执行
        std::atomic<int> rc1{0};
        std::atomic<bool> t1done{false};
        auto e2 = yomk::msgs::Event::shared(yomk::payload::Event("wb", nullptr, countingHandler("e2"), "e2"));
        std::thread waiter(
            [&loop, &rc1, &t1done, &e2]
            {
                rc1 = loop.postWait(e2);
                t1done = true;
            });
        YOMK_TEST_CHECK(
            waitUntil([&loop] { return loopSnap(loop).pending == 1; }, 2000),
            "e2 已入队（pending==1，T1 进入等待）");
        std::this_thread::sleep_for(std::chrono::milliseconds(
            100));  // 确保 T1 已进入 wait（谓词式 wait 对早触发亦免疫，此处确定性覆盖"唤醒"分支）

        // T2：destroy——stop 置位后 join 等待 g1 收尾，需主线程确认停止置位再释放 gate
        std::thread destroyer([&loop] { loop.destroy(); });
        YOMK_TEST_CHECK(
            waitUntil([&loop] { return !loopSnap(loop).running; }, 2000),
            "destroy 过程中观察到 m_running==false（先停止）");

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
        YOMK_TEST_CHECK(loopSnap(loop).pending == 0, "destroy 后 pending==0（队列已清空）");

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
        auto g1 = yomk::msgs::Event::shared(yomk::payload::Event("wb", nullptr, gateHandler(), "g1"));
        YOMK_TEST_CHECK(raw->post(g1) == 0, "post 阻塞事件 g1 返回 0");
        YOMK_TEST_CHECK(waitGateStarted(2000), "g1 已进入执行（阻塞占住工作线程）");

        // T1：postWait 计数事件 e2——入队成功（rc1 将为 0）后阻塞等待；e2 排队不执行
        std::atomic<int> rc1{0};
        std::atomic<bool> t1done{false};
        auto e2 = yomk::msgs::Event::shared(yomk::payload::Event("wb", nullptr, countingHandler("e2"), "e2"));
        std::thread waiter(
            [&raw, &rc1, &t1done, &e2]
            {
                rc1 = raw->postWait(e2);
                t1done = true;
            });
        YOMK_TEST_CHECK(
            waitUntil([raw] { return loopSnap(*raw).pending == 1; }, 2000),
            "e2 已入队（pending==1，T1 进入等待）");
        std::this_thread::sleep_for(std::chrono::milliseconds(
            100));  // 确保 T1 已进入 wait（谓词式 wait 对早触发亦免疫，此处确定性覆盖"唤醒"分支）

        // T2：不调 destroy 直接析构——~EventLoop 内部 destroy 兜底（先停止置位再 join，join 等 g1 收尾）
        std::thread resetter([&loopHolder] { loopHolder.reset(); });
        YOMK_TEST_CHECK(
            waitUntil([raw] { return !loopSnap(*raw).running; }, 2000),
            "析构过程中观察到 m_running==false（~EventLoop 先走 destroy 停止）");

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
