/**
 * @file TestYomkService.cpp
 * @brief YomkServer 多服务工程示例
 *
 * 演示内容：
 * 1. 使用 YomkBoot 生命周期管理启动多个服务
 * 2. 同步请求（YOMK_REQUEST）
 * 3. 异步请求（YOMK_ASYNC_REQUEST）
 * 4. 跨服务调用（服务A调用服务B）
 * 5. 服务删除安全验证（YOMK_DEL_SERVICE + 弱绑定回调）
 *
 * 工程结构：
 * - msgs/YomkMsgs.h: 消息包定义
 * - services/YomkServiceA: 服务A（调用服务B）
 * - services/YomkServiceB: 服务B（被调用）
 * - boot/MyBoot: 生命周期管理
 */

#include "YomkAPI.h"       // YomkServer 框架 API
#include "boot/MyBoot.h"   // 生命周期管理类
#include "msgs/YomkMsgs.h" // 消息包定义

/**
 * @brief 程序入口
 *
 * 执行流程：
 * 1. YOMK_BOOT: 初始化框架并按顺序执行 before() -> start() -> after()
 * 2. 同步调用服务A
 * 3. 异步调用服务A
 * 4. 等待用户输入退出
 */
int main(int argc, char *argv[])
{
    /**
     * 第一阶段：启动框架和服务
     *
     * YOMK_BOOT 宏执行流程：
     * 1. 调用 YOMK_INIT() 初始化框架（启动内置服务：FunctionPool、Context、EventLoop、Logger）
     * 2. 调用 MyBoot::before() 创建启动前资源
     * 3. 调用 MyBoot::start() 注册并启动服务（调用各服务的 init()）
     * 4. 调用 MyBoot::after() 执行启动后操作
     *
     * 参数：服务列表 {"/YomkServiceA", "/YomkServiceB"}
     */
    YOMK_BOOT(new MyBoot({"/YomkServiceA", "/YomkServiceB"}));

    // 测试 YOMK_VERSION：获取并输出框架版本号（对应 project(Yomk VERSION x.x.x) 定义的 VERSION）
    YOMK_INFO_TAG("main", "YomkServer version: ", YOMK_VERSION);

    /**
     * 第二阶段：同步调用服务A
     *
     * YOMK_REQUEST 同步请求：
     * - URL: /YomkServiceA/call_skill_a
     * - 参数: YomkMkPtr 创建消息包指针
     * - 返回: YomkResponse 响应对象
     *
     * 调用链路：main -> YomkServiceA::callSkillA -> YomkServiceB::callSkillB
     */
    YomkResponse response = YOMK_REQUEST("/YomkServiceA/call_skill_a", YomkMkPtr(YMyServiceMsg, MyServiceMsg{"hello world a"}));

    // 检查响应状态：eOk 表示成功
    if (response.m_status == YomkResponse::eOk)
    {
        // 日志：打印成功消息
        YOMK_INFO_TAG("main", "request /YomkServiceA/call_skill_a, with response.msg: ", response.m_msg);
    }
    else
    {
        // 日志：打印错误消息
        YOMK_ERROR_TAG("main", "request /YomkServiceA/call_skill_a, with response.msg: ", response.m_msg);
    }

    // 日志：标记同步请求完成
    YOMK_INFO_TAG("main", "request /YomkServiceA/call_skill_a send finished.");

    /**
     * 第三阶段：异步调用服务A
     *
     * YOMK_ASYNC_REQUEST 异步请求：
     * - URL: /YomkServiceA/call_skill_a
     * - 参数: 消息包
     * - 回调: lambda 函数处理响应
     *
     * 特点：
     * - 不阻塞主线程
     * - 响应在回调函数中处理
     * - 适合耗时操作
     */
    YOMK_ASYNC_REQUEST("/YomkServiceA/call_skill_a", YomkMkPtr(YMyServiceMsg, MyServiceMsg{"hello world a"}), [](YomkResponse response)
                       {
        // 异步回调函数：在请求完成后被调用
        if(response.m_status == YomkResponse::eOk)
        {
            YOMK_INFO_TAG("main", "async request /YomkServiceA/call_skill_a, with response.msg: ", response.m_msg);
        }
        else
        {
            YOMK_ERROR_TAG("main", "async request /YomkServiceA/call_skill_a, with response.msg: ", response.m_msg);
        } });

    // 日志：标记异步请求已发送（注意：此时回调可能还未执行）
    YOMK_INFO_TAG("main", "async request /YomkServiceA/call_skill_a send finished.");

    /**
     * 第四阶段：服务删除安全验证（复用服务B）
     *
     * 验证流程：
     * 1. 删除前：请求服务B 与 FunctionPool 调用其弱绑定回调均成功
     * 2. YOMK_DEL_SERVICE 删除服务B
     * 3. 删除后再请求 -> 返回 service not found（eNo），不崩溃
     * 4. 删除后再调 FunctionPool 中弱绑定的成员回调 -> 返回 service has been deleted，不崩溃
     */
    YomkResponse delRespBefore = YOMK_REQUEST("/YomkServiceB/call_skill_b", YomkMkPtr(YMyServiceMsg, MyServiceMsg{"hello world b"}));
    YomkResponse poolRespBefore = YOMK_FUNCTIONPOOL_CALL("service_b_work", YomkMkPtr(YMyServiceMsg, MyServiceMsg{"hello world b"}));
    if (delRespBefore.m_status == YomkResponse::eOk && poolRespBefore.m_status == YomkResponse::eOk)
    {
        YOMK_INFO_TAG("main", "before delete: request and functionpool call both ok.");
    }
    else
    {
        YOMK_ERROR_TAG("main", "before delete: unexpected failure. ", delRespBefore.m_msg, " ", poolRespBefore.m_msg);
    }

    // 删除服务B
    int delRet = YOMK_DEL_SERVICE("/YomkServiceB");
    YOMK_INFO_TAG("main", "YOMK_DEL_SERVICE ret: ", delRet);

    // 删除后请求：应返回 eNo，不崩溃
    YomkResponse delRespAfter = YOMK_REQUEST("/YomkServiceB/call_skill_b", YomkMkPtr(YMyServiceMsg, MyServiceMsg{"hello world b"}));
    if (delRespAfter.m_status == YomkResponse::eNo)
    {
        YOMK_INFO_TAG("main", "after delete: request rejected as expected, msg: ", delRespAfter.m_msg);
    }
    else
    {
        YOMK_ERROR_TAG("main", "after delete: unexpected status: ", delRespAfter.m_status);
    }

    // 删除后调用 FunctionPool 中弱绑定的回调：应返回 eNo，不崩溃
    YomkResponse poolRespAfter = YOMK_FUNCTIONPOOL_CALL("service_b_work", YomkMkPtr(YMyServiceMsg, MyServiceMsg{"hello world b"}));
    if (poolRespAfter.m_status == YomkResponse::eNo)
    {
        YOMK_INFO_TAG("main", "after delete: weak bound callback rejected as expected, msg: ", poolRespAfter.m_msg);
    }
    else
    {
        YOMK_ERROR_TAG("main", "after delete: unexpected functionpool status: ", poolRespAfter.m_status);
    }

    // 等待用户输入，防止程序立即退出
    getchar();

    return 0;
}