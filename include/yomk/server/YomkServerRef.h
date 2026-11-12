#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

#include "yomk/server/YomkServer.h"

// 进程级 YomkServer 持有器:epoch 失效 + thread_local 快照的单一实现.
// 独立成头:生命周期机制与表面层无耦合,可独立测试;
// server 注入与置空统一经 setServer,仅依赖 YomkServer.h,不引入 diag 依赖
//
// 单例生命周期设计注记(供框架维护者参考):
// 1. 永生单例存储:mtx() 与 holder() 的锁/持有器经堆分配故意泄漏,不注册析构,
//    进程退出前始终有效.背景:常规静态成员在退出期析构后,在途池任务再取快照会对
//    已销毁的锁加锁(挂起,实证:worker 栈卡于 serverSnapshot 内 pthread_mutex_lock);
//    静态跨线程销毁窗口无法靠定义顺序根治.函数局部 static 保证首次使用即构造,
//    未 init 时调用也安全(C++11 magic statics 线程安全).
// 2. 退出清理:yomk::server().init() 内经 atexit 注册 holder().destroy()(晚于库内
//    普通静态析构执行),幂等(显式 shutdown 后再调仅空转).destroy() 先在锁内置空
//    快照并取出服务器,锁外再释放:置空后在途池任务(如异步 monitor 回调发起的迟到
//    快照读)取空返回,避免释放路径(触发池排空 -> join)与任务并发拷贝同一 shared_ptr
//    (段错误);若持锁释放,则排空期间在途任务取快照抢同一把锁,与释放方互锁挂起.
//    锁与持有器本体永不析构,在途任务的迟到快照读永远拿到可用锁与空快照,
//    根除静态析构窗口内对已销毁锁加锁的挂起与对控制块的并发拷贝段错误.
// 3. 退出闸门:thread_local cached 是普通 TLS 对象,其析构早于 atexit 回调执行
//    (退出链 LIFO),destroy() 期间快照若走缓存即命中已析构 TLS 的残留悬垂指针,
//    在已释放控制块上加减引用计数,deinit 返回时二次 dispose 触发 double free.
//    故 destroy() 首步置位 destroyed() 闸门,serverSnapshot() 首行短路直接返回空,
//    全程不触碰已析构缓存;destroyed() 亦堆分配永生——若为普通函数局部 static,
//    其构造晚于 atexit 注册,退出链 LIFO 下会在被读取之前先析构,同构陷阱.
//    显式 shutdown() 路径不受影响:走 setServer(nullptr),不置闸门,进程未退出.
//    另:destroy() 显式调 local->shutdown() 同步排空而非依赖 reset 触发析构——
//    在途快照持有者会推迟 delete(shared_ptr 语义),极端下进程消亡时 deinit sweep
//    被整体跳过;显式排空保证 deinit 必然在 atexit 处理器内完成.
struct YomkServerRef
{
    // 全局版本号:server 注入与置空统一经 setServer,写后 fetch_add(1, release)
    // 失效所有线程的 thread_local 缓存
    static std::atomic<uint64_t>& epoch()
    {
        static std::atomic<uint64_t> e(0);
        return e;
    }
    // 退出闸门:atexit destroy() 置位后,所有快照首行短路,不再触碰已析构的
    // thread_local 缓存(其析构早于 atexit 回调,残留指针悬垂).堆分配永生,
    // 与 mtx()/holder() 同风格,不参与静态析构顺序
    static std::atomic<bool>& destroyed()
    {
        static std::atomic<bool>* const d = new std::atomic<bool>(false);
        return *d;
    }
    static std::shared_ptr<YomkServer> serverSnapshot()
    {
        if (destroyed().load(std::memory_order_acquire))
        {
            return {};
        }
        // 每线程缓存快照,热路径仅 1 次 epoch acquire load + 比对;慢路径仅首调与
        // 失效后进入.!cached 使空快照永不作为有效命中:未初始化期每次调用仍走慢
        // 路径,契约消息语义不变.并发 shutdown 的陈旧窗口内至多一调用命中缓存
        // 保活的 server,返回服务级失败,无悬垂
        thread_local std::shared_ptr<YomkServer> cached;
        thread_local uint64_t cachedEpoch = 0;
        const uint64_t e = epoch().load(std::memory_order_acquire);
        if (cachedEpoch != e || !cached)
        {
            std::lock_guard<std::mutex> lock(mtx());
            cached = holder().server;
            cachedEpoch = e;
        }
        return cached;
    }
    static void setServer(std::shared_ptr<YomkServer> server)
    {
        {
            std::lock_guard<std::mutex> lock(mtx());
            holder().server = std::move(server);
        }
        epoch().fetch_add(1, std::memory_order_release); // 锁外失效全部线程缓存
    }
    // 堆上 holder:规避静态析构顺序风险,destroy 由 atexit 显式触发
    struct Holder
    {
        void destroy()
        {
            // 闸门必须先于排空置位:deinit 内的域调用必须立刻短路;release/acquire 跨线程可见
            destroyed().store(true, std::memory_order_release);
            std::shared_ptr<YomkServer> local;
            {
                std::lock_guard<std::mutex> lock(mtx());
                local = std::move(holder().server);
            }
            // 失效所有线程缓存(纵深防御):即使有路径在闸门置位前读缓存,
            // cachedEpoch != epoch() 仍强制走慢路径取到空 holder
            epoch().fetch_add(1, std::memory_order_release);
            // 显式同步排空,不依赖 reset 恰为最后引用:退出期若有在途快照持有者
            // (分离线程等),reset 不触发析构,delete 连带 deinit sweep 被推迟到其
            // 释放时——极端下进程消亡、线程被杀,deinit 整体跳过.先显式 shutdown
            // (幂等,析构兜底的再调仅空转)保证 deinit 必然在本 atexit 处理器内
            // 同步完成,reset 仅回收壳对象
            if (local)
            {
                local->shutdown();
            }
            local.reset();
        }
        std::shared_ptr<YomkServer> server;
    };
    static std::mutex& mtx()
    {
        static std::mutex* const m = new std::mutex;
        return *m;
    }
    static Holder& holder()
    {
        static Holder* const h = new Holder;
        return *h;
    }
};
