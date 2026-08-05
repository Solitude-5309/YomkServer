#include <YomkServer/YomkAPI.h>
#include <YomkMath/MathService.h>
#include <iostream>
#include <cmath>

using namespace yomk;

static int g_pass = 0;
static int g_fail = 0;

void check(const char *name, double actual, double expected)
{
    if (std::abs(actual - expected) < 1e-9)
    {
        std::cout << "[PASS] " << name << ": " << actual << std::endl;
        g_pass++;
    }
    else
    {
        std::cout << "[FAIL] " << name << ": expected " << expected << ", got " << actual << std::endl;
        g_fail++;
    }
}

int main(int argc, char *argv[])
{
    YOMK_INIT();

    YOMK_NEW_SERVICE(MathService);

    // 测试加法
    YomkResponse resp = YOMK_REQUEST("/MathService/add", YomkMkPtr(YMathOp, MathOp{"add", 10.5, 3.2}));
    if (resp.m_status == YomkResponse::eOk)
    {
        YomkUnPackPkg(resp.m_data, Float64, result);
        check("add(10.5, 3.2)", result->d, 13.7);
    }
    else
    {
        std::cout << "[FAIL] add request failed: " << resp.m_msg << std::endl;
        g_fail++;
    }

    // 测试减法
    resp = YOMK_REQUEST("/MathService/sub", YomkMkPtr(YMathOp, MathOp{"sub", 10.5, 3.2}));
    if (resp.m_status == YomkResponse::eOk)
    {
        YomkUnPackPkg(resp.m_data, Float64, result);
        check("sub(10.5, 3.2)", result->d, 7.3);
    }
    else
    {
        std::cout << "[FAIL] sub request failed: " << resp.m_msg << std::endl;
        g_fail++;
    }

    // 测试乘法
    resp = YOMK_REQUEST("/MathService/mul", YomkMkPtr(YMathOp, MathOp{"mul", 10.5, 3.2}));
    if (resp.m_status == YomkResponse::eOk)
    {
        YomkUnPackPkg(resp.m_data, Float64, result);
        check("mul(10.5, 3.2)", result->d, 33.6);
    }
    else
    {
        std::cout << "[FAIL] mul request failed: " << resp.m_msg << std::endl;
        g_fail++;
    }

    // 测试除法
    resp = YOMK_REQUEST("/MathService/div", YomkMkPtr(YMathOp, MathOp{"div", 10.0, 2.0}));
    if (resp.m_status == YomkResponse::eOk)
    {
        YomkUnPackPkg(resp.m_data, Float64, result);
        check("div(10.0, 2.0)", result->d, 5.0);
    }
    else
    {
        std::cout << "[FAIL] div request failed: " << resp.m_msg << std::endl;
        g_fail++;
    }

    // 测试除零
    resp = YOMK_REQUEST("/MathService/div", YomkMkPtr(YMathOp, MathOp{"div", 10.0, 0.0}));
    if (resp.m_status == YomkResponse::eNo)
    {
        std::cout << "[PASS] div(10.0, 0.0) correctly returned error" << std::endl;
        g_pass++;
    }
    else
    {
        std::cout << "[FAIL] div(10.0, 0.0) should return error" << std::endl;
        g_fail++;
    }

    std::cout << "\n========== Test Summary ==========" << std::endl;
    std::cout << "PASS: " << g_pass << std::endl;
    std::cout << "FAIL: " << g_fail << std::endl;

    return g_fail > 0 ? 1 : 0;
}
