#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

#include "YomkServer.h"

// 进程级 YomkServer 持有器：epoch 失效 + thread_local 快照的单一实现。
// 独立成头：生命周期机制与 YomkAPI 表面无耦合，可独立测试；
// server 注入与置空统一经 setServer，仅依赖 YomkServer.h，不引入 diag 依赖
struct YomkServerRef
{
    // 全局版本号：server 注入与置空统一经 setServer，写后 fetch_add(1, release)
    // 失效所有线程的 thread_local 缓存
    static std::atomic<uint64_t>& epoch()
    {
        static std::atomic<uint64_t> e(0);
        return e;
    }
    static std::shared_ptr<YomkServer> serverSnapshot()
    {
        // 每线程缓存快照，热路径仅 1 次 epoch acquire load + 比对；慢路径仅首调与
        // 失效后进入。!cached 使空快照永不作为有效命中：未初始化期每次调用仍走慢
        // 路径，契约消息语义不变。并发 shutdown 的陈旧窗口内至多一调用命中缓存
        // 保活的 server，返回服务级失败，无悬垂
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
    // 堆上 holder：规避静态析构顺序风险，destroy 由 atexit 显式触发
    struct Holder
    {
        void destroy()
        {
            std::shared_ptr<YomkServer> local;
            {
                std::lock_guard<std::mutex> lock(mtx());
                local = std::move(holder().server);
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
