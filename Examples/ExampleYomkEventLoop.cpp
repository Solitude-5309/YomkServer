/**
 * @file ExampleYomkEventLoop.cpp
 * @brief YomkEventLoop 事件循环示例
 *
 * 演示内容：
 * 1. 启动事件循环（YOMK_EVENTLOOP_START）
 * 2. 异步投递事件（YOMK_EVENTLOOP_POST）
 * 3. 同步投递事件并等待（YOMK_EVENTLOOP_POST_WAIT）
 * 4. 停止和销毁事件循环
 *
 * EventLoop 特性：
 * - 独立线程运行
 * - 同循环内事件顺序执行
 * - 不同循环间并行执行
 * - 支持非阻塞/阻塞两种投递方式
 */

#include <iostream>
#include <thread>
#include "YomkAPI.h"

/**
 * @brief 事件处理函数
 *
 * 当投递事件到事件循环时，此函数会被调用处理事件
 * 可以设置为默认处理函数（启动时指定）或临时处理函数（投递时指定）
 *
 * @param pkg 事件数据
 * @return YomkResponse 处理结果
 */
YomkResponse eventHandle(YomkPkgPtr pkg)
{
    // 打印当前线程ID，验证事件在独立线程中执行
    YOMK_DEBUG_TAG("eventHandle", "eventHandle called by thread: ", std::this_thread::get_id());

    // 解包事件数据
    YomkUnPackPkgResponse(pkg, String, str);

    // 打印收到的数据
    YOMK_DEBUG_TAG("eventHandle", "eventHandle called with data: ", str->d);

    // 演示：在事件处理函数中再次投递事件（递归投递）
    // 最多递归投递 3 次
    static int i = 0;
    if (i++ < 3)
    {
        // 异步投递：不等待结果，立即返回
        YOMK_EVENTLOOP_POST("event_loop_1", YomkMkPtr(String, "requestEventHandle_data_" + std::to_string(i)));
    }

    return {YomkResponse::eOk, "eventHandle success. "};
}

/**
 * @brief 程序入口
 *
 * 演示 EventLoop 的完整生命周期：
 * 启动 -> 投递事件 -> 同步等待 -> 停止 -> 销毁
 */
int main(int argc, char *argv[])
{
    // 初始化框架
    YOMK_INIT();

    // 测试 YOMK_VERSION：获取并输出框架版本号（对应 project(Yomk VERSION x.x.x) 定义的 VERSION）
    YOMK_INFO_TAG("main", "YomkServer version: ", YOMK_VERSION);

    /**
     * 步骤1：启动事件循环
     *
     * 参数：
     * - "event_loop_1": 事件循环名称（全局唯一）
     * - eventHandle: 默认事件处理函数
     *
     * 启动后会在独立线程中运行，等待事件投递
     */
    YomkResponse response = YOMK_EVENTLOOP_START(
        "event_loop_1",
        eventHandle);
    if (response.m_status == YomkResponse::eOk)
    {
        YOMK_DEBUG_TAG("main", "start event_loop_1 success");
    }
    else
    {
        YOMK_ERROR_TAG("main", "start event_loop_1 failed: ", response.m_msg);
    }

    /**
     * 步骤2：异步投递事件（不等待结果）
     *
     * YOMK_EVENTLOOP_POST:
     * - 将事件投递到事件循环队列
     * - 立即返回，不阻塞
     * - 事件由事件循环异步处理
     */
    response = YOMK_EVENTLOOP_POST("event_loop_1", YomkMkPtr(String, "requestEventHandle_data"));
    if (response.m_status == YomkResponse::eOk)
    {
        YOMK_DEBUG_TAG("main", "post to event_loop_1 success");
    }
    else
    {
        YOMK_ERROR_TAG("main", "post to event_loop_1 failed: ", response.m_msg);
    }

    /**
     * 步骤3：同步投递事件并等待结果
     *
     * YOMK_EVENTLOOP_POST_WAIT:
     * - 将事件投递到事件循环队列
     * - 阻塞等待事件处理完成
     * - 返回处理结果
     *
     * 响应中的 m_data 包含 Event 对象，可获取：
     * - m_eventId: 事件ID
     * - m_eventLoopName: 处理事件的事件循环名称
     * - m_response: 事件处理函数的返回值
     */
    response = YOMK_EVENTLOOP_POST_WAIT("event_loop_1", YomkMkPtr(String, "requestEventHandle_data_wait"));
    if (response.m_status == YomkResponse::eOk)
    {
        YOMK_DEBUG_TAG("main", "post_wait to event_loop_1 success");
        // 解包响应数据，获取 Event 对象
        YomkUnPackPkg(response.m_data, Event, event);
        if (event)
        {
            // 打印事件详细信息
            YOMK_DEBUG_TAG(
                "main",
                "post_wait response eventId: ",
                event->d.m_eventId,
                " eventLoopName: ",
                event->d.m_eventLoopName,
                " response: ", event->d.m_response.m_msg);
        }
    }
    else
    {
        YOMK_ERROR_TAG("main", "post_wait to event_loop_1 failed: ", response.m_msg);
    }

    // 等待用户输入，观察异步投递的事件处理情况
    YOMK_DEBUG_TAG("main", "enter any key to stop event_loop_1");
    getchar();

    /**
     * 步骤4：停止事件循环
     *
     * 停止后不再接受新的事件投递；未执行的排队事件保留在队列中，
     * 再次 START 可重启续跑（事件不丢失）
     */
    response = YOMK_EVENTLOOP_STOP("event_loop_1");
    if (response.m_status == YomkResponse::eOk)
    {
        YOMK_DEBUG_TAG("main", "stop event_loop_1 success");
    }
    else
    {
        YOMK_ERROR_TAG("main", "stop event_loop_1 failed: ", response.m_msg);
    }

    // 等待用户输入
    YOMK_DEBUG_TAG("main", "enter any key to destroy event_loop_1");
    getchar();

    /**
     * 步骤5：销毁事件循环
     *
     * 先停止退出工作线程，再清空未执行的排队事件（不可续跑），彻底释放资源
     */
    response = YOMK_EVENTLOOP_DESTROY("event_loop_1");
    if (response.m_status == YomkResponse::eOk)
    {
        YOMK_DEBUG_TAG("main", "destroy event_loop_1 success");
    }
    else
    {
        YOMK_ERROR_TAG("main", "destroy event_loop_1 failed: ", response.m_msg);
    }

    YOMK_DEBUG_TAG("main", "test YomkEventLoop completed, any key to continue...");

    getchar();

    return 0;
}
