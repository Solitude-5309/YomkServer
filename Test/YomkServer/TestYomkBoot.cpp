/**
 * @file TestYomkBoot.cpp
 * @brief YomkAPI::boot 三段失败短路/错误码透传/所有权测试（正式全面测试，MC6）
 *
 * 覆盖内容：
 * 1. boot(nullptr)：内部隐式 init 路径，返回 0 且单例被初始化
 * 2. 三段失败短路：before/start/after 任一非 0 立即返回原值，后续段不执行
 *    （-1/-2/-3 三种错误码原值透传实证，验证非 -1 错误码同样穿透）
 * 3. 执行序：全部成功时严格 before → start → after
 * 4. 所有权：boot 以 unique_ptr 接管 YomkBoot*，调用结束后析构（dtor 计数实证）
 *
 * 说明：boot(nullptr) 消耗单例的 call_once，后续注入用例的 init 调用空转，无需隔离
 *
 * 风格：纯 main() + 失败计数，返回非 0 表示存在失败用例（零第三方依赖）
 */

#include <iostream>
#include <string>
#include <vector>

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

// ---- 文件级观测变量（boot 全程主线程调用，无并发访问） ----
static int g_beforeRet = 0;              // before() 注入返回值（0 成功）
static int g_startRet = 0;               // start() 注入返回值
static int g_afterRet = 0;               // after() 注入返回值
static int g_beforeCount = 0;            // before 真实执行次数
static int g_startCount = 0;             // start 真实执行次数
static int g_afterCount = 0;             // after 真实执行次数
static int g_bootDtorCount = 0;          // boot 对象析构计数（所有权实证）
static std::vector<std::string> g_order; // 三段执行序记录

static void resetGlobals()
{
    g_beforeRet = g_startRet = g_afterRet = 0;
    g_beforeCount = g_startCount = g_afterCount = g_bootDtorCount = 0;
    g_order.clear();
}

/**
 * @brief 三段记录器：各段计数与执行序入表，返回值由全局开关注入
 */
class BootRecorder : public YomkBoot
{
public:
    int before() override
    {
        ++g_beforeCount;
        g_order.push_back("before");
        return g_beforeRet;
    }
    int start() override
    {
        ++g_startCount;
        g_order.push_back("start");
        return g_startRet;
    }
    int after() override
    {
        ++g_afterCount;
        g_order.push_back("after");
        return g_afterRet;
    }
    ~BootRecorder() override { ++g_bootDtorCount; }
};

int main()
{
    std::cout << "===== 1. boot(nullptr) 隐式 init =====" << std::endl;
    {
        CHECK(YomkAPI::boot() == 0, "boot(nullptr) 返回 0（不经过三段）");
        CHECK(YomkAPI::serverInstance() != nullptr, "boot(nullptr) 内部隐式 init 路径，单例被初始化");
    }

    std::cout << "===== 2. before 失败短路 =====" << std::endl;
    {
        resetGlobals();
        g_beforeRet = -1;
        int ret = YomkAPI::boot(new BootRecorder);
        CHECK(ret == -1, "before 失败返回原值 -1");
        CHECK(g_beforeCount == 1, "before 执行 1 次");
        CHECK(g_startCount == 0, "start 被短路未执行");
        CHECK(g_afterCount == 0, "after 被短路未执行");
        CHECK(g_bootDtorCount == 1, "boot 对象用后即析构（所有权接管）");
    }

    std::cout << "===== 3. start 失败短路 =====" << std::endl;
    {
        resetGlobals();
        g_startRet = -2;
        int ret = YomkAPI::boot(new BootRecorder);
        CHECK(ret == -2, "start 失败返回原值 -2（非 -1 错误码同样透传）");
        CHECK(g_beforeCount == 1, "before 执行 1 次");
        CHECK(g_startCount == 1, "start 执行 1 次");
        CHECK(g_afterCount == 0, "after 被短路未执行");
        CHECK(g_bootDtorCount == 1, "boot 对象用后即析构（所有权接管）");
    }

    std::cout << "===== 4. after 失败透传 =====" << std::endl;
    {
        resetGlobals();
        g_afterRet = -3;
        int ret = YomkAPI::boot(new BootRecorder);
        CHECK(ret == -3, "after 失败返回原值 -3");
        CHECK(g_beforeCount == 1 && g_startCount == 1 && g_afterCount == 1,
              "三段各执行 1 次（after 为末段无短路语义）");
        CHECK(g_bootDtorCount == 1, "boot 对象用后即析构（所有权接管）");
    }

    std::cout << "===== 5. 全部成功执行序 =====" << std::endl;
    {
        resetGlobals();
        int ret = YomkAPI::boot(new BootRecorder);
        CHECK(ret == 0, "三段全部成功返回 0");
        CHECK(g_order.size() == 3 && g_order[0] == "before" && g_order[1] == "start" && g_order[2] == "after",
              "执行序严格 before -> start -> after");
        CHECK(g_bootDtorCount == 1, "boot 对象用后即析构（所有权接管）");
    }

    std::cout << (g_failed == 0 ? "ALL PASSED" : "SOME FAILED") << " (" << g_failed << " failed)" << std::endl;
    return g_failed == 0 ? 0 : 1;
}
