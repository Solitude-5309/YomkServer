/**
 * @file TestYomkEventLoopInfo.cpp
 * @brief YomkEventLoop 模块内省接口示例
 *
 * 演示内容：
 * 1. 事件循环名列表 / 单循环元信息 / 全量 dump 三个内省接口
 * 2. Event 的 tag 字段：POST 时可传 tag（末位可选，缺省空），内省行以 nextNEventTag 列出队首 tag
 * 3. YomkEventLoop 既有功能函数补齐类型名后，服务器层内省可见期望类型
 * 4. 生命周期边界：destroy 后循环不再出现在内省结果中
 */

#include <iostream>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include "YomkAPI.h"

// 阻塞事件用的同步原语：冻结事件循环消费，便于观察队列中的 tag
static std::mutex g_blockMtx;
static std::condition_variable g_blockCv;
static bool g_blockReleased = false;
static std::atomic<bool> g_blockHandled(false);

// 阻塞事件处理函数：占住工作线程，直到主线程释放
YomkResponse blockHandle(YomkPkgPtr pkg)
{
    std::unique_lock<std::mutex> lock(g_blockMtx);
    g_blockCv.wait(lock, []()
                   { return g_blockReleased; });
    g_blockHandled.store(true);
    return YomkResponse(YomkResponse::eOk, "block event handled");
}

// 普通事件处理函数：空实现（内省仅关心队列中的 tag）
YomkResponse idleHandle(YomkPkgPtr pkg)
{
    return YomkResponse(YomkResponse::eOk, "idle event handled");
}

// loop_b 的默认处理函数（内省仅关心 defaultFunc 是否设置）
YomkResponse defaultHandle(YomkPkgPtr pkg)
{
    return YomkResponse(YomkResponse::eOk, "default event handled");
}

// 从 StringArray 响应中取出字符串列表
static bool unpackLines(const YomkResponse &response, std::vector<std::string> &lines)
{
    YomkUnPackPkg(response.m_data, StringArray, arr);
    if (!arr)
    {
        return false;
    }
    lines = arr->d;
    return true;
}

// 检查字符串列表中是否包含指定行
static bool hasLine(const std::vector<std::string> &lines, const std::string &line)
{
    return std::find(lines.begin(), lines.end(), line) != lines.end();
}

// 检查 msg 是否包含指定子串
static bool msgContains(const YomkResponse &response, const std::string &sub)
{
    return response.m_msg.find(sub) != std::string::npos;
}

// 轮询等待 pending 归零（事件全部消费完毕）
static bool waitPendingZero(const std::string &loopName, int timeoutMs)
{
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline)
    {
        YomkResponse resp = YOMK_EVENTLOOP_INFO_LOOP(loopName);
        if (resp.m_status == YomkResponse::eOk && msgContains(resp, "pending:0"))
        {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
}

int main(int argc, char *argv[])
{
    // 初始化框架（自动启动内置服务，含 /YomkEventLoop）
    YOMK_INIT();

    int failed = 0;

    // 准备数据：loop_a 无默认处理函数，loop_b 设置默认处理函数
    YomkResponse response = YOMK_EVENTLOOP_START("loop_a", nullptr);
    if (response.m_status != YomkResponse::eOk)
    {
        YOMK_ERROR_TAG("main", "start loop_a failed: ", response.m_msg);
        ++failed;
    }
    response = YOMK_EVENTLOOP_START("loop_b", defaultHandle);
    if (response.m_status != YomkResponse::eOk)
    {
        YOMK_ERROR_TAG("main", "start loop_b failed: ", response.m_msg);
        ++failed;
    }

    // 1. 事件循环名列表
    {
        YomkResponse resp = YOMK_EVENTLOOP_INFO_LOOPS();
        std::vector<std::string> lines;
        if (resp.m_status != YomkResponse::eOk || !unpackLines(resp, lines) ||
            !hasLine(lines, "loop_a") || !hasLine(lines, "loop_b"))
        {
            YOMK_ERROR_TAG("main", "EVENTLOOP_INFO_LOOPS check failed.");
            ++failed;
        }
        else
        {
            YOMK_INFO_TAG("main", "EVENTLOOP_INFO_LOOPS ok, size: ", lines.size());
        }
    }

    // 2. 单循环元信息：loop_a 无默认处理函数，loop_b 有默认处理函数
    {
        YomkResponse resp = YOMK_EVENTLOOP_INFO_LOOP("loop_a");
        if (resp.m_status != YomkResponse::eOk || !msgContains(resp, "running:on") ||
            !msgContains(resp, "pending:0") || !msgContains(resp, "defaultFunc:off"))
        {
            YOMK_ERROR_TAG("main", "EVENTLOOP_INFO_LOOP loop_a check failed, msg: ", resp.m_msg);
            ++failed;
        }
        else
        {
            YOMK_INFO_TAG("main", "EVENTLOOP_INFO_LOOP loop_a ok, msg: ", resp.m_msg);
        }

        resp = YOMK_EVENTLOOP_INFO_LOOP("loop_b");
        if (resp.m_status != YomkResponse::eOk || !msgContains(resp, "defaultFunc:on"))
        {
            YOMK_ERROR_TAG("main", "EVENTLOOP_INFO_LOOP loop_b check failed, msg: ", resp.m_msg);
            ++failed;
        }
        else
        {
            YOMK_INFO_TAG("main", "EVENTLOOP_INFO_LOOP loop_b ok, msg: ", resp.m_msg);
        }
    }

    // 3. tag 内省：先投递阻塞事件冻结消费，再投递带 tag / 不带 tag 的事件观察队列
    {
        YOMK_EVENTLOOP_POST("loop_a", YomkMkPtr(String, "block"), blockHandle);
        // 等待阻塞事件被工作线程取走（取走后 pending 归零且工作线程被占住）
        if (!waitPendingZero("loop_a", 2000))
        {
            YOMK_ERROR_TAG("main", "wait block event picked up timeout.");
            ++failed;
        }

        // 带 tag 与不带 tag（tag 缺省为空，旧调用方式直接编译）
        YOMK_EVENTLOOP_POST("loop_a", YomkMkPtr(String, "data1"), idleHandle, "tag1");
        YOMK_EVENTLOOP_POST("loop_a", YomkMkPtr(String, "data2"), idleHandle);

        YomkResponse resp = YOMK_EVENTLOOP_INFO_LOOP("loop_a", 5);
        if (resp.m_status != YomkResponse::eOk || !msgContains(resp, "pending:2") ||
            !msgContains(resp, "nextNEventTag(5): tag1, -"))
        {
            YOMK_ERROR_TAG("main", "EVENTLOOP_INFO_LOOP tag check failed, msg: ", resp.m_msg);
            ++failed;
        }
        else
        {
            YOMK_INFO_TAG("main", "EVENTLOOP_INFO_LOOP tag ok, msg: ", resp.m_msg);
        }

        // 限制只列 1 个 tag
        resp = YOMK_EVENTLOOP_INFO_LOOP("loop_a", 1);
        if (resp.m_status != YomkResponse::eOk || !msgContains(resp, "nextNEventTag(1): tag1") ||
            msgContains(resp, "-"))
        {
            YOMK_ERROR_TAG("main", "EVENTLOOP_INFO_LOOP tagCount=1 check failed, msg: ", resp.m_msg);
            ++failed;
        }
        else
        {
            YOMK_INFO_TAG("main", "EVENTLOOP_INFO_LOOP tagCount=1 ok, msg: ", resp.m_msg);
        }

        // 释放阻塞事件，等待队列消费完毕恢复现场
        {
            std::lock_guard<std::mutex> lock(g_blockMtx);
            g_blockReleased = true;
        }
        g_blockCv.notify_all();
        if (!waitPendingZero("loop_a", 2000) || !g_blockHandled.load())
        {
            YOMK_ERROR_TAG("main", "wait event queue drain timeout.");
            ++failed;
        }
    }

    // 4. 循环不存在返回 eNo
    {
        YomkResponse resp = YOMK_EVENTLOOP_INFO_LOOP("not_exist");
        if (resp.m_status != YomkResponse::eNo)
        {
            YOMK_ERROR_TAG("main", "EVENTLOOP_INFO_LOOP not_exist check failed.");
            ++failed;
        }
    }

    // 5. 全量 dump：两行元信息
    {
        YomkResponse resp = YOMK_EVENTLOOP_INFO_ALL();
        std::vector<std::string> lines;
        if (resp.m_status != YomkResponse::eOk || !unpackLines(resp, lines) || lines.size() != 2)
        {
            YOMK_ERROR_TAG("main", "EVENTLOOP_INFO_ALL check failed.");
            ++failed;
        }
        else
        {
            YOMK_INFO_TAG("main", "EVENTLOOP_INFO_ALL ok:");
            for (auto &line : lines)
            {
                YOMK_INFO_TAG("main", line);
            }
        }
    }

    // 6. 生命周期边界：destroy 后不再出现在内省结果中
    {
        YomkResponse resp = YOMK_EVENTLOOP_DESTROY("loop_b");
        if (resp.m_status != YomkResponse::eOk)
        {
            YOMK_ERROR_TAG("main", "EVENTLOOP_DESTROY loop_b failed: ", resp.m_msg);
            ++failed;
        }
        resp = YOMK_EVENTLOOP_INFO_LOOPS();
        std::vector<std::string> lines;
        if (resp.m_status != YomkResponse::eOk || !unpackLines(resp, lines) || hasLine(lines, "loop_b"))
        {
            YOMK_ERROR_TAG("main", "loop_b should not appear in EVENTLOOP_INFO_LOOPS.");
            ++failed;
        }
        resp = YOMK_EVENTLOOP_INFO_LOOP("loop_b");
        if (resp.m_status != YomkResponse::eNo)
        {
            YOMK_ERROR_TAG("main", "EVENTLOOP_INFO_LOOP loop_b after destroy should be eNo.");
            ++failed;
        }
        else
        {
            YOMK_INFO_TAG("main", "EVENTLOOP_DESTROY loop_b ok, removed from introspection.");
        }
    }

    // 7. 服务器层联动：/YomkEventLoop 既有功能函数的期望类型在服务器层内省可见
    {
        YomkResponse resp = YOMK_SERVER_INFO_FUNCTIONS("/YomkEventLoop");
        std::vector<std::string> lines;
        if (resp.m_status != YomkResponse::eOk || !unpackLines(resp, lines) ||
            !hasLine(lines, "/start [Eventloop]") || !hasLine(lines, "/post [Event]") ||
            !hasLine(lines, "/stop [String]") || !hasLine(lines, "/loop [String]"))
        {
            YOMK_ERROR_TAG("main", "SERVER_INFO_FUNCTIONS /YomkEventLoop check failed.");
            ++failed;
        }
        else
        {
            YOMK_INFO_TAG("main", "SERVER_INFO_FUNCTIONS /YomkEventLoop ok: /start [Eventloop], /post [Event], /stop [String], /loop [String]");
        }
    }

    YOMK_EVENTLOOP_DESTROY("loop_a");

    if (failed > 0)
    {
        YOMK_ERROR_TAG("main", "TestYomkEventLoopInfo failed, count: ", failed);
        return 1;
    }
    YOMK_INFO_TAG("main", "TestYomkEventLoopInfo all check passed.");
    return 0;
}
