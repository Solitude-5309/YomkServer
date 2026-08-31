#pragma once
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

// 轻量线程池（库内部使用，不对外导出）：固定工作线程 + 任务队列。
// 排空式停止：stop() 后拒绝新任务，存量任务全部执行完毕才 join 工作线程；
// 任务异常被捕获并记日志，不会传播导致进程终止
class YomkSimpleThreadPool
{
public:
    // threadCount 为 0 时取 hardware_concurrency() 的一半（向上取整），再兜底 2；构造即启动工作线程
    explicit YomkSimpleThreadPool(std::size_t threadCount = 0);
    // 未 stop 时兜底执行 stop()，保证工作线程在对象销毁前退出
    ~YomkSimpleThreadPool();

public:
    // 投递任务：已停止返回 false，否则入队并唤醒工作线程
    bool post(std::function<void()> task);
    // 停止：拒新任务 -> 排空存量任务 -> join 全部工作线程；幂等，重复调用直接返回
    void stop();

private:
    void workerLoop();

private:
    std::vector<std::thread> m_workers;
    std::deque<std::function<void()>> m_queue;
    std::mutex m_mtx;
    std::condition_variable m_cv;
    std::atomic<bool> m_stop{false};
};
