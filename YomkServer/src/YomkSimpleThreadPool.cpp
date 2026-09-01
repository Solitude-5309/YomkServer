#include "YomkSimpleThreadPool.h"
#include "YomkDefine.h"
#include <iostream>

YomkSimpleThreadPool::YomkSimpleThreadPool(std::size_t threadCount)
{
    if (threadCount == 0)
    {
        // 取硬件并发数的一半（向上取整），异步请求多为轻量转发，避免占用过多核心；为 0 时兜底 2
        unsigned int hw = std::thread::hardware_concurrency();
        threadCount = (hw == 0) ? 2 : static_cast<std::size_t>((hw + 1) / 2);
    }

    m_workers.reserve(threadCount);
    for (std::size_t i = 0; i < threadCount; ++i)
    {
        m_workers.emplace_back(std::bind(&YomkSimpleThreadPool::workerLoop, this));
    }
}

YomkSimpleThreadPool::~YomkSimpleThreadPool()
{
    stop();
}

bool YomkSimpleThreadPool::post(std::function<void()> task)
{
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        // 与 stop() 在同一把锁下互斥：停止置位后不会再有新任务入队
        if (m_stop.load())
        {
            return false;
        }
        m_queue.push_back(std::move(task));
    }
    m_cv.notify_one();
    return true;
}

void YomkSimpleThreadPool::stop()
{
    // 幂等：重复调用（如 shutdown 与析构兜底）直接返回，不会二次 join
    if (m_stop.exchange(true))
    {
        return;
    }

    m_cv.notify_all();
    for (auto &worker : m_workers)
    {
        if (worker.joinable())
        {
            // 自 join 防护：最后引用若在池任务内释放（任务持有快照副本），
            // dispose 会落在工作线程上，此时 join 自身触发 resource_deadlock 终止，
            // 改为分离自己：进程已在退出路径，残余工作线程随进程终止回收。
            if (worker.get_id() == std::this_thread::get_id())
            {
                worker.detach();
                continue;
            }
            worker.join();
        }
    }
}

void YomkSimpleThreadPool::workerLoop()
{
    while (true)
    {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(m_mtx);
            m_cv.wait(lock, [this]()
                      { return m_stop.load() || !m_queue.empty(); });

            // 停止后仍排空存量队列：队列空且已停止才退出
            if (m_queue.empty())
            {
                return;
            }

            task = std::move(m_queue.front());
            m_queue.pop_front();
        }

        // 锁外执行，异常捕获记日志，避免任务抛异常导致进程终止
        try
        {
            task();
        }
        catch (const std::exception &e)
        {
            YOMK_ERR_POS_LOG("async task exception caught: " + std::string(e.what()));
        }
        catch (...)
        {
            YOMK_ERR_POS_LOG("async task unknown exception caught");
        }
    }
}
