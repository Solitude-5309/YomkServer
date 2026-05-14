#include "YomkAPI.h"

// 创建一个消息包，用于服务间通信
class MyServiceMsg : public YomkPkg
{
public:
    MyServiceMsg() { m_name = "MyServiceMsg"; }
    MyServiceMsg(const std::string& msg) : msg(msg) { m_name = "MyServiceMsg"; }
    virtual ~MyServiceMsg() {}
public:
    // 克隆数据，深拷贝
    virtual std::shared_ptr<YomkPkg> clone() const
    {
        MyServiceMsg* nd = new MyServiceMsg();
        nd->m_name = m_name;
        nd->msg = msg;
        std::shared_ptr<YomkPkg> ptr;
        ptr.reset(nd);
        return ptr;
    }
public:
    std::string msg;
};
// 智能指针
typedef std::shared_ptr<MyServiceMsg> MyServiceMsgPtr;
// 创建MyServiceMsg的智能指针
#define YomkMkMyServiceMsgPtr(msg) std::make_shared<MyServiceMsg>(msg)

// 创建一个服务B，用于编写功能集合
class YomkServiceB : public YomkService
{
public:
    YomkServiceB(YomkServer* server)
        : YomkService(server)
    {
        // 设置服务名称，服务名称在服务器中必须唯一
        name("/YomkServiceB");
    }
    virtual ~YomkServiceB() {}
public:
    virtual int init()
    {
        // 安装功能函数，功能函数名称在服务中必须唯一
        YomkInstallFunc("/call_skill_b", YomkServiceB::callSkillB);
        // 日志
        YOMK_INFO_TAG("YomkServiceB::init", "install func [ /call_skill_b ] to", name());
        return 0;
    }
private:
    YomkResponse callSkillB(YomkPkgPtr pkg)
    {
        // 解包数据
        YomkUnPackPkgresponse(pkg, "MyServiceMsg", MyServiceMsg, myServiceMsg);
        if(!myServiceMsg)
        {
            YOMK_ERROR_TAG("YomkServiceB::callSkillB", name(), " myServiceMsg is empty");
            return YomkResponse(YomkResponse::eInvalid, name() + " MyServiceMsg is empty");
        }

        // 日志
        YOMK_INFO_TAG("YomkServiceB::callSkillB", name(), " exec skill b, with msg: ", myServiceMsg->msg);

        // 返回结果
        return YomkResponse(YomkResponse::eOk, name() + " exec skill b success");
    }
};

// 创建一个服务A，用于编写功能集合
class YomkServiceA : public YomkService
{
public:
    YomkServiceA(YomkServer* server)
        : YomkService(server)
    {
        // 设置服务名称，服务名称在服务器中必须唯一
        name("/YomkServiceA");
    }
    virtual ~YomkServiceA() {}
public:
    virtual int init()
    {
        // 安装功能函数，功能函数名称在服务中必须唯一
        YomkInstallFunc("/call_skill_a", YomkServiceA::callSkillA);
        // 日志
        YOMK_INFO_TAG("YomkServiceA::init", "install func [ /call_skill_a ] to", name());
        return 0;
    }
private:
    YomkResponse callSkillA(YomkPkgPtr pkg)
    {
        // 解包数据
        YomkUnPackPkgresponse(pkg, "MyServiceMsg", MyServiceMsg, myServiceMsg);
        if(!myServiceMsg)
        {
            YOMK_ERROR_TAG("YomkServiceA::callSkillA", name(), " myServiceMsg is empty");
            return YomkResponse(YomkResponse::eInvalid, name() + " MyServiceMsg is empty");
        }

        // 日志
        YOMK_INFO_TAG("YomkServiceA::callSkillA", name(), " exec skill a, with msg: ", myServiceMsg->msg);

        // 调用服务B中的方法
        YomkResponse response = YOMK_REQUEST("/YomkServiceB/call_skill_b", YomkMkMyServiceMsgPtr("hello world b"));

        // 检查调用结果
        if(response.m_resStatus != YomkResponse::eOk)
        {
            YOMK_ERROR_TAG("YomkServiceA::callSkillA", name(), " call /YomkServiceB/call_skill_b, response: ", response.m_msg);
            return YomkResponse(YomkResponse::eErr, name() + " exec skill a failed");
        }

        // 日志
        YOMK_INFO_TAG("YomkServiceA::callSkillA", name(), " call /YomkServiceB/call_skill_b, response: ", response.m_msg);

        // 返回结果
        return YomkResponse(YomkResponse::eOk, name() + " exec skill a success");
    }
};

int main(int argc, char *argv[])
{    
    // 全局宏，一个程序只能有一个服务器，用于初始化服务器
    YOMK_INIT(std::make_shared<YomkServer>(), { 
        "/YomkSettings", 
        "/YomkFunctionPool", 
        "/YomkContext",
        "/YomkEventLoop",
        "/YomkLogger"
    });
    // 创建服务A，实例化YomkServiceA，并调用init()方法
    YOMK_NEW_SERVICE(YomkServiceA, "/YomkServiceA");
    // 创建服务B，实例化YomkServiceB，并调用init()方法
    YOMK_NEW_SERVICE(YomkServiceB, "/YomkServiceB");

    // 同步调用服务A中的方法
    YomkResponse response = YOMK_REQUEST("/YomkServiceA/call_skill_a", YomkMkMyServiceMsgPtr("hello world a"));
    if(response.m_resStatus == YomkResponse::eOk)
    {
        YOMK_INFO_TAG("main", "request /YomkServiceA/call_skill_a, with response.msg: ", response.m_msg);
    }
    else
    {
        YOMK_ERROR_TAG("main", "request /YomkServiceA/call_skill_a, with response.msg: ", response.m_msg);
    }

    YOMK_INFO_TAG("main", "request /YomkServiceA/call_skill_a send finished.");

    // 异步调用服务A中的方法
    YOMK_ASYNC_REQUEST("/YomkServiceA/call_skill_a", YomkMkMyServiceMsgPtr("hello world a"), [](YomkResponse response) {
        if(response.m_resStatus == YomkResponse::eOk)
        {
            YOMK_INFO_TAG("main", "async request /YomkServiceA/call_skill_a, with response.msg: ", response.m_msg);
        }
        else
        {
            YOMK_ERROR_TAG("main", "async request /YomkServiceA/call_skill_a, with response.msg: ", response.m_msg);
        }
    });

    YOMK_INFO_TAG("main", "async request /YomkServiceA/call_skill_a send finished.");

    getchar();

    return 0;
}