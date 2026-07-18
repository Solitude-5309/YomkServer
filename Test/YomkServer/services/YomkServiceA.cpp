#include "YomkServiceA.h"

/**
 * @brief 构造函数
 * 
 * 调用基类构造函数传入 server 指针
 * 设置本服务的名称，必须以 / 开头，全局唯一
 */
YomkServiceA::YomkServiceA(YomkServer *server)
    : YomkService(server)
{
    name("/YomkServiceA");  // 服务名，用于 URL 路由：/YomkServiceA/功能函数名
}

/**
 * @brief 服务初始化
 * 
 * 在此注册服务提供的功能函数：
 * - /call_skill_a: 映射到 callSkillA 方法
 * 
 * YomkInstallFunc 宏展开为：
 * installFunc("/call_skill_a", std::bind(&YomkServiceA::callSkillA, this, std::placeholders::_1))
 */
int YomkServiceA::init()
{
    // 安装功能函数，功能函数名称在服务中必须唯一
    YomkInstallFunc("/call_skill_a", YomkServiceA::callSkillA);
    // 日志：记录安装的功能函数名和服务名
    YOMK_INFO_TAG("YomkServiceA::init", "install func [ /call_skill_a ] to", name());
    return 0;  // 返回 0 表示初始化成功
}

/**
 * @brief 功能函数 A 的实现
 * 
 * 演示完整的请求处理流程：
 * 1. 解包输入消息
 * 2. 执行业务逻辑
 * 3. 跨服务调用其他服务
 * 4. 检查调用结果
 * 5. 返回响应
 */
YomkResponse YomkServiceA::callSkillA(YomkPkgPtr pkg)
{
    // 解包数据：将 YomkPkgPtr 转换为具体的消息类型
    // YomkUnPackPkgResponse 宏已自动判空，失败自动返回 {eNo, "错误信息"}
    // 解包后可通过 myServiceMsg->msg.content 访问数据
    YomkUnPackPkgResponse(pkg, YMyServiceMsg, myServiceMsg);

    // 日志：打印收到的消息内容
    YOMK_INFO_TAG("YomkServiceA::callSkillA", name(), " exec skill a, with msg: ", myServiceMsg->msg.content);

    // 跨服务调用：调用 YomkServiceB 的 call_skill_b 功能
    // URL 格式：/服务名/功能函数名
    // 使用 YOMK_REQUEST 进行同步调用，等待返回结果
    YomkResponse response = YOMK_REQUEST("/YomkServiceB/call_skill_b", YomkMkPtr(YMyServiceMsg, MyServiceMsg{"hello world b"}));

    // 检查调用结果：如果失败则返回错误
    if (response.m_status != YomkResponse::eOk)
    {
        YOMK_ERROR_TAG("YomkServiceA::callSkillA", name(), " call /YomkServiceB/call_skill_b, response: ", response.m_msg);
        return YomkResponse(YomkResponse::eNo, name() + " exec skill a failed");
    }

    // 日志：打印跨服务调用的成功结果
    YOMK_INFO_TAG("YomkServiceA::callSkillA", name(), " call /YomkServiceB/call_skill_b, response: ", response.m_msg);

    // 返回成功结果
    return YomkResponse(YomkResponse::eOk, name() + " exec skill a success");
}
