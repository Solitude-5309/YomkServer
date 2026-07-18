#include "YomkServiceB.h"

/**
 * @brief 构造函数
 * 
 * 设置服务名称为 /YomkServiceB
 */
YomkServiceB::YomkServiceB(YomkServer *server)
    : YomkService(server)
{
    name("/YomkServiceB");  // 服务名，用于 URL 路由
}

/**
 * @brief 服务初始化
 * 
 * 注册功能函数 /call_skill_b
 */
int YomkServiceB::init()
{
    // 安装功能函数，功能函数名称在服务中必须唯一
    YomkInstallFunc("/call_skill_b", YomkServiceB::callSkillB);
    // 日志：记录安装的功能函数名和服务名
    YOMK_INFO_TAG("YomkServiceB::init", "install func [ /call_skill_b ] to", name());
    return 0;  // 返回 0 表示初始化成功
}

/**
 * @brief 功能函数 B 的实现
 * 
 * 演示简单的请求处理流程：
 * 1. 解包输入消息
 * 2. 处理业务逻辑
 * 3. 返回响应
 */
YomkResponse YomkServiceB::callSkillB(YomkPkgPtr pkg)
{
    // 解包数据：将 YomkPkgPtr 转换为 YMyServiceMsg 类型
    // 解包后可通过 myServiceMsg->msg.content 访问数据
    YomkUnPackPkgResponse(pkg, YMyServiceMsg, myServiceMsg);

    // 日志：打印收到的消息内容
    YOMK_INFO_TAG("YomkServiceB::callSkillB", name(), " exec skill b, with msg: ", myServiceMsg->msg.content);

    // 返回成功结果
    return YomkResponse(YomkResponse::eOk, name() + " exec skill b success");
}
