/**
 * @file TestYomkShutdown.cpp
 * @brief YomkServer 优雅关闭（shutdown）测试
 *
 * 验证内容：
 * 1. shutdown() 逐个服务调用 deinit()：init 中启动的 joinable 后台线程被安全停止
 * 2. shutdown() 幂等：重复调用不崩溃、不重复 deinit
 * 3. shutdown() 后 request 返回 service not found、serviceNames() 为空
 * 4. 析构兜底：未显式 shutdown 直接销毁服务器，服务 deinit 仍被调用且不崩溃
 * 5. YOMK_SHUTDOWN 宏路径：单例关闭后 serverInstance() 为空
 *
 * 独立实例用例使用 std::make_shared<YomkServer>()，避免污染 YomkAPI 单例状态
 */

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include "YomkAPI.h"

// 记录 ThreadedService::deinit 被调用的次数（独立实例用例与析构兜底用例共用）
static std::atomic<int> g_deinitCount{0};

/**
 * @brief 测试服务：init 中启动 joinable 后台线程，deinit 中停止并 join
 *
 * 若退出路径绕过 deinit（修复前的问题），线程对象随析构触发 std::terminate，
 * 进程直接崩溃 —— 本测试能正常退出即证明关闭路径生效
 */
class ThreadedService : public YomkService
{
public:
    ThreadedService(YomkServer *server)
        : YomkService(server)
    {
    }

public:
    virtual int init() override
    {
        m_stop.store(false);
        m_worker = std::thread([this]()
                               {
            while (!m_stop.load())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            } });
        return 0;
    }

    virtual void deinit() override
    {
        m_stop.store(true);
        if (m_worker.joinable())
        {
            m_worker.join();
        }
        g_deinitCount.fetch_add(1);
    }

private:
    std::atomic<bool> m_stop{false};
    std::thread m_worker;
};

int main(int argc, char *argv[])
{
    int failed = 0;

    // 用例 1/2/3：独立实例显式 shutdown —— deinit 生效、幂等、关闭后拒绝请求
    {
        auto server = std::make_shared<YomkServer>();
        server->newService<ThreadedService>("/ThreadedService");

        // 关闭前：服务在表中可见
        if (server->serviceNames().size() != 1)
        {
            std::cout << "[FAIL] before shutdown: serviceNames size != 1" << std::endl;
            failed++;
        }

        // 显式关闭：后台线程应被 deinit 停止并 join
        server->shutdown();
        if (g_deinitCount.load() != 1)
        {
            std::cout << "[FAIL] shutdown did not call deinit exactly once, count: " << g_deinitCount.load() << std::endl;
            failed++;
        }
        else
        {
            std::cout << "[OK] shutdown called service deinit, background thread stopped." << std::endl;
        }

        // 幂等：重复关闭不应重复 deinit、不崩溃
        server->shutdown();
        if (g_deinitCount.load() != 1)
        {
            std::cout << "[FAIL] shutdown not idempotent, deinit count: " << g_deinitCount.load() << std::endl;
            failed++;
        }
        else
        {
            std::cout << "[OK] shutdown is idempotent." << std::endl;
        }

        // 关闭后：请求返回 service not found，服务表为空
        YomkResponse resp = server->request("/ThreadedService/any_func", nullptr);
        if (resp.m_status != YomkResponse::eNo)
        {
            std::cout << "[FAIL] request after shutdown should be eNo, got: " << resp.m_status << std::endl;
            failed++;
        }
        else
        {
            std::cout << "[OK] request after shutdown rejected: " << resp.m_msg << std::endl;
        }

        if (!server->serviceNames().empty())
        {
            std::cout << "[FAIL] serviceNames after shutdown should be empty." << std::endl;
            failed++;
        }
        else
        {
            std::cout << "[OK] serviceNames empty after shutdown." << std::endl;
        }
    }

    // 用例 4：析构兜底 —— 未显式 shutdown，直接销毁服务器也应先 deinit
    {
        int before = g_deinitCount.load();
        auto server = std::make_shared<YomkServer>();
        server->newService<ThreadedService>("/ThreadedService");
        server.reset(); // 未调用 shutdown，走 ~YomkServerPrivate 兜底

        if (g_deinitCount.load() != before + 1)
        {
            std::cout << "[FAIL] destructor fallback did not call deinit." << std::endl;
            failed++;
        }
        else
        {
            std::cout << "[OK] destructor fallback called deinit, no terminate." << std::endl;
        }
    }

    // 用例 5：YOMK_SHUTDOWN 宏路径 —— 单例关闭后 serverInstance 为空
    {
        YOMK_INIT();
        if (!YOMK_SERVER_PTR)
        {
            std::cout << "[FAIL] server instance null after YOMK_INIT." << std::endl;
            failed++;
        }

        YOMK_SHUTDOWN();
        if (YOMK_SERVER_PTR)
        {
            std::cout << "[FAIL] server instance should be null after YOMK_SHUTDOWN." << std::endl;
            failed++;
        }
        else
        {
            std::cout << "[OK] YOMK_SHUTDOWN released singleton." << std::endl;
        }

        // 幂等：重复关闭不崩溃
        YOMK_SHUTDOWN();
        std::cout << "[OK] YOMK_SHUTDOWN is idempotent." << std::endl;
    }

    std::cout << (failed == 0 ? "TestYomkShutdown PASS" : "TestYomkShutdown FAIL") << std::endl;
    return failed == 0 ? 0 : 1;
}
