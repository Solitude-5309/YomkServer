/**
 * @file TestYomkService.cpp
 * @brief YomkService 命名/函数注册/调用白盒单元测试（正式全面测试，MC2）
 *
 * 覆盖内容：
 * 1. 命名：注册前设置/读取、空名拒绝、YOMK_ADD_SERVICE 两参形式注册前改名
 * 2. 注册锁定：addService 成功后改名被拒（名字与注册键一致承诺）
 * 3. installFunc：合法名、非法名（空串/无 / 开头）拒绝、空函数拒绝、
 *    三参元数据（m_msgName）、两参覆盖清除残留元数据、三参覆盖恢复
 * 4. funcInfos 内省
 * 5. invoke：命中（自身响应 + pkg 透传回显）、非法名/未安装返回 eNo 契约消息
 * 6. 空服务器守卫：server==nullptr 构造后全部 !m_p 分支
 * 7. markDeleted/deleted 标志位语义（弱绑定回调丢弃语义归 MC3）
 *
 * 风格：纯 main() + 失败计数，返回非 0 表示存在失败用例（零第三方依赖）
 */

#include <iostream>
#include <map>
#include <string>

#include "YomkAPI.h"

static int g_failed = 0;

#define CHECK(cond, msg)                                                          \
    do                                                                            \
    {                                                                             \
        if (!(cond))                                                              \
        {                                                                         \
            std::cout << "[FAIL] [line " << __LINE__ << "] " << msg << std::endl; \
            ++g_failed;                                                           \
        }                                                                         \
        else                                                                      \
        {                                                                         \
            std::cout << "[ OK ] [line " << __LINE__ << "] " << msg << std::endl; \
        }                                                                         \
    } while (0)

// 内省辅助：在 funcInfos 中查找 funcName，命中可选返回其 m_msgName
static bool findFuncInfo(const std::map<std::string, YomkFuncInfo> &infos,
                         const std::string &funcName, std::string *msgName = nullptr)
{
    auto iter = infos.find(funcName);
    if (iter == infos.end())
    {
        return false;
    }
    if (msgName)
    {
        *msgName = iter->second.m_msgName;
    }
    return true;
}

/**
 * @brief 主演示服务：注册/调用/内省用例载体
 */
class DemoService : public YomkService
{
public:
    DemoService(YomkServer *server)
        : YomkService(server)
    {
        name("/DemoService");
    }

public:
    virtual int init() override
    {
        YomkInstallFunc("/echo", DemoService::echo, String);           // 三参：内省元数据 String
        YomkInstallFunc("/plain", DemoService::plain);                 // 两参：无元数据
        YomkInstallFunc("/reinstall", DemoService::reinstall, String); // 先三参
        YomkInstallFunc("/reinstall", DemoService::reinstall);         // 两参覆盖：清残留元数据
        YomkInstallFunc("/restore", DemoService::restore);             // 先两参
        YomkInstallFunc("/restore", DemoService::restore, String);     // 三参覆盖：恢复元数据
        return 0;
    }

private:
    // String 解包回显：验证 pkg 透传往返
    YomkResponse echo(YomkPkgPtr pkg)
    {
        YomkUnPackPkgResponse(pkg, String, data);
        return {YomkResponse::eOk, "echo:" + data->d};
    }
    YomkResponse plain(YomkPkgPtr pkg)
    {
        return {YomkResponse::eOk, "plain"};
    }
    YomkResponse reinstall(YomkPkgPtr pkg)
    {
        return {YomkResponse::eOk, "reinstall"};
    }
    YomkResponse restore(YomkPkgPtr pkg)
    {
        return {YomkResponse::eOk, "restore"};
    }
};

/**
 * @brief 空服务器服务：以 nullptr 构造，验证全部空实现守卫分支（不注册）
 */
class NullSrv : public YomkService
{
public:
    NullSrv()
        : YomkService(nullptr)
    {
    }

public:
    virtual int init() override
    {
        return 0;
    }
};

/**
 * @brief 标志位服务：有效构造但不注册（栈对象），验证 markDeleted/deleted
 */
class FlagSrv : public YomkService
{
public:
    FlagSrv(YomkServer *server)
        : YomkService(server)
    {
    }

public:
    virtual int init() override
    {
        return 0;
    }
};

// 1. 命名：注册前设置/读取一致、空名拒绝
static void testNameBeforeRegister(DemoService *demo)
{
    CHECK(demo->name() == "/DemoService", "注册前 name() 读取与构造设置一致");
    demo->name(""); // 空名设置应被拒绝
    CHECK(demo->name() == "/DemoService", "空名设置被拒，名字保持不变");
}

// 2. 注册与注册锁定：addService 成功返回 0，注册后改名被拒
static void testRegisterAndLockName(DemoService *demo)
{
    CHECK(YOMK_ADD_SERVICE(demo) == 0, "addService 注册成功返回 0");
    demo->name("/NewName"); // 注册后改名应被拒绝
    CHECK(demo->name() == "/DemoService", "注册后改名被拒，名字与注册键一致（锁名承诺）");
}

// 3. YOMK_ADD_SERVICE 两参形式：注册前改名生效
static void testAddServiceWithName()
{
    auto *renamed = new DemoService(YOMK_SERVER_P);
    CHECK(YOMK_ADD_SERVICE(renamed, "/RenamedDemo") == 0, "两参 YOMK_ADD_SERVICE 注册成功返回 0");
    CHECK(renamed->name() == "/RenamedDemo", "注册前经两参形式改名生效");
}

// 4. installFunc：正常/非法名/空函数 + 三参元数据 + 覆盖安装元数据清除/恢复
static void testInstallFunc(DemoService *demo)
{
    // 正常路径：运行时追加安装合法名
    demo->installFunc("/manual", [](YomkPkgPtr pkg)
                      { return YomkResponse{YomkResponse::eOk, "manual"}; });

    // 非法名：空串与无 / 开头均被拒
    demo->installFunc("", [](YomkPkgPtr pkg)
                      { return YomkResponse{YomkResponse::eOk, ""}; });
    demo->installFunc("no_slash", [](YomkPkgPtr pkg)
                      { return YomkResponse{YomkResponse::eOk, ""}; });

    // 空函数对象：安装被拒（守卫修复后）
    demo->installFunc("/null_func", nullptr);

    std::map<std::string, YomkFuncInfo> infos = demo->funcInfos();
    std::string msgName;

    CHECK(findFuncInfo(infos, "/manual"), "合法 / 开头名安装成功，funcInfos 命中");
    CHECK(!findFuncInfo(infos, ""), "空串函数名安装被拒，funcInfos 不含");
    CHECK(!findFuncInfo(infos, "no_slash"), "无 / 开头函数名安装被拒，funcInfos 不含");
    CHECK(!findFuncInfo(infos, "/null_func"), "空函数对象安装被拒，funcInfos 不含");

    // 三参元数据与覆盖安装
    CHECK(findFuncInfo(infos, "/echo", &msgName) && msgName == "String", "三参安装声明元数据 m_msgName == String");
    CHECK(findFuncInfo(infos, "/plain", &msgName) && msgName.empty(), "两参安装无元数据");
    CHECK(findFuncInfo(infos, "/reinstall", &msgName) && msgName.empty(), "两参覆盖安装清除残留元数据");
    CHECK(findFuncInfo(infos, "/restore", &msgName) && msgName == "String", "三参覆盖安装恢复元数据");
}

// 5. invoke：命中（含 pkg 透传）与异常路径契约
static void testInvoke(DemoService *demo)
{
    // 命中：返回函数自身响应
    YomkResponse resp = demo->invoke("/plain");
    CHECK(resp.m_status == YomkResponse::eOk && resp.m_msg == "plain", "invoke 命中，返回函数自身响应");

    // 命中：pkg 透传与解包回显（数据往返）
    resp = demo->invoke("/echo", YomkMkPtr(String, std::string("hi")));
    CHECK(resp.m_status == YomkResponse::eOk && resp.m_msg == "echo:hi", "invoke 命中，pkg 透传解包回显正确");

    // 命中：函数内解包失败（pkg 为 nullptr）返回 eNo
    resp = demo->invoke("/echo", nullptr);
    CHECK(resp.m_status == YomkResponse::eNo && resp.m_msg.find("pkg is null") != std::string::npos,
          "invoke 命中但 pkg 为空，函数解包守卫返回 eNo");

    // 异常路径：空名/无 / 开头返回 eNo + parse error 契约消息
    resp = demo->invoke("");
    CHECK(resp.m_status == YomkResponse::eNo && resp.m_msg.find("parse error") != std::string::npos,
          "invoke 空函数名返回 eNo 且 msg 含 parse error");
    resp = demo->invoke("no_slash");
    CHECK(resp.m_status == YomkResponse::eNo && resp.m_msg.find("parse error") != std::string::npos,
          "invoke 无 / 开头函数名返回 eNo 且 msg 含 parse error");

    // 异常路径：未安装函数返回 eNo + not found 契约消息
    resp = demo->invoke("/not_exist");
    CHECK(resp.m_status == YomkResponse::eNo && resp.m_msg.find("not found") != std::string::npos,
          "invoke 未安装函数返回 eNo 且 msg 含 not found");

    // 空函数被拒后按未安装处理（对应守卫修复）
    resp = demo->invoke("/null_func");
    CHECK(resp.m_status == YomkResponse::eNo && resp.m_msg.find("not found") != std::string::npos,
          "invoke 被拒安装的空函数按未安装返回 eNo");
}

// 6. 空服务器守卫：server==nullptr 构造后全部空实现分支
static void testNullServerGuards()
{
    NullSrv srv;
    srv.name("x");        // 不应崩溃（空实现分支）
    srv.markRegistered(); // 不应崩溃
    srv.markDeleted();    // 不应崩溃
    srv.installFunc("/x", [](YomkPkgPtr pkg)
                    { return YomkResponse{}; }); // 不应崩溃
    srv.asyncRequest("/SomeService/func");       // 不应崩溃

    CHECK(srv.name().empty(), "空实现 name() 返回空串");
    CHECK(srv.deleted() == true, "空实现 deleted() 返回 true（视同已失效）");
    CHECK(srv.funcInfos().empty(), "空实现 funcInfos() 返回空表");

    YomkResponse resp = srv.invoke("/x");
    CHECK(resp.m_status == YomkResponse::eNo && resp.m_msg == "service is null",
          "空实现 invoke 返回 eNo + service is null");
    resp = srv.request("/SomeService/func");
    CHECK(resp.m_status == YomkResponse::eNo && resp.m_msg == "service is null",
          "空实现 request 返回 eNo + service is null");
}

// 7. markDeleted/deleted 标志位语义（弱绑定回调语义归 MC3）
static void testDeletedFlag()
{
    FlagSrv srv(YOMK_SERVER_P);
    CHECK(srv.deleted() == false, "新构造服务 deleted() 初始为 false");
    srv.markDeleted();
    CHECK(srv.deleted() == true, "markDeleted() 置位后 deleted() 为 true");
}

int main()
{
    YOMK_INIT();

    auto *demo = new DemoService(YOMK_SERVER_P);
    testNameBeforeRegister(demo);
    testRegisterAndLockName(demo);
    testAddServiceWithName();
    testInstallFunc(demo);
    testInvoke(demo);
    testNullServerGuards();
    testDeletedFlag();

    if (g_failed > 0)
    {
        std::cout << "TestYomkService failed, count: " << g_failed << std::endl;
        return 1;
    }
    std::cout << "TestYomkService all check passed." << std::endl;
    return 0;
}
