/**
 * @file TestYomkFunctionPool.cpp
 * @brief YomkFunctionPool 公共函数池示例
 *
 * 演示内容：
 * 1. 注册公共函数到函数池（YOMK_FUNCTIONPOOL_REGISTER）
 * 2. 调用已注册的函数（YOMK_FUNCTIONPOOL_CALL）
 * 3. 注销函数池中的函数（YOMK_FUNCTIONPOOL_UNREGISTER）
 * 4. 注销后调用/重复注销的异常分支验证
 *
 * FunctionPool 特性：
 * - 统一的函数注册和调度机制
 * - 支持运行时注册、更新、热替换
 * - 适合无状态工具函数
 * - 面向过程开发范式
 */

#include <iostream>
#include "YomkAPI.h"
#include <filesystem>
namespace fs = std::filesystem;

/**
 * @brief 公共函数示例
 *
 * 注册到 FunctionPool 的函数必须遵循统一签名：
 * YomkResponse funcName(YomkPkgPtr pkg)
 *
 * 本函数演示：
 * - 解包输入参数
 * - 处理业务逻辑
 * - 返回响应结果
 *
 * @param pkg 输入消息包
 * @return YomkResponse 处理结果
 */
YomkResponse func1(YomkPkgPtr pkg)
{
    // 解包输入数据：将 YomkPkgPtr 转换为 String 类型
    // YomkUnPackPkgResponse 宏已自动判空，失败自动返回 {eNo, "错误信息"}
    YomkUnPackPkgResponse(pkg, String, str);

    // 打印收到的数据
    YOMK_DEBUG_TAG("func1", "func1 called with data: ", str->d);

    // 返回成功结果
    return {YomkResponse::eOk, "func1 success. "};
}

/**
 * @brief 程序入口
 *
 * 演示 FunctionPool 的基本使用：
 * 1. 初始化框架
 * 2. 注册公共函数
 * 3. 调用公共函数
 * 4. 注销公共函数，并验证注销后调用/重复注销均被拒绝
 */
int main(int argc, char *argv[])
{
    // 获取可执行文件路径，用于构造配置文件路径
    fs::path exePath = fs::absolute(argv[0]);
    // 构造 settings.json 的路径
    fs::path settingsPath = exePath.parent_path().parent_path() / "Test" / "YomkServer" / "Settings" / "settings.json";

    // 初始化框架
    YOMK_INIT();

    // 测试 YOMK_VERSION：获取并输出框架版本号（对应 project(Yomk VERSION x.x.x) 定义的 VERSION）
    YOMK_INFO_TAG("main", "YomkServer version: ", YOMK_VERSION);

    /**
     * 步骤1：注册公共函数
     *
     * YOMK_FUNCTIONPOOL_REGISTER:
     * - 参数1: 函数名称（全局唯一，用于后续调用）
     * - 参数2: 函数指针（必须符合 YomkResponse(YomkPkgPtr) 签名）
     *
     * 注册后可在任意位置通过函数名调用
     */
    YomkResponse response = YOMK_FUNCTIONPOOL_REGISTER("func1", func1);
    if (response.m_status == YomkResponse::eOk)
    {
        YOMK_DEBUG_TAG("main", "register func1 success");
    }
    else
    {
        YOMK_ERROR_TAG("main", "register func1 failed: ", response.m_msg);
    }

    /**
     * 步骤2：调用公共函数
     *
     * YOMK_FUNCTIONPOOL_CALL:
     * - 参数1: 函数名称（必须与注册时一致）
     * - 参数2: 消息包指针（传递给函数的参数）
     *
     * 返回 YomkResponse 响应对象
     */
    response = YOMK_FUNCTIONPOOL_CALL("func1", YomkMkPtr(String, settingsPath.string()));
    if (response.m_status == YomkResponse::eOk)
    {
        YOMK_DEBUG_TAG("main", "call func1 success");
    }
    else
    {
        YOMK_ERROR_TAG("main", "call func1 failed: ", response.m_msg);
    }

    /**
     * 步骤3：注销公共函数
     *
     * YOMK_FUNCTIONPOOL_UNREGISTER:
     * - 参数: 函数名称（必须与注册时一致）
     *
     * 注销后该函数名不可再被调用
     */
    response = YOMK_FUNCTIONPOOL_UNREGISTER("func1");
    if (response.m_status == YomkResponse::eOk)
    {
        YOMK_DEBUG_TAG("main", "unregister func1 success");
    }
    else
    {
        YOMK_ERROR_TAG("main", "unregister func1 failed: ", response.m_msg);
    }

    /**
     * 步骤4：注销后调用（预期失败）
     *
     * 函数已注销，调用应返回 eInvalid（funcName is not register）
     */
    response = YOMK_FUNCTIONPOOL_CALL("func1", YomkMkPtr(String, settingsPath.string()));
    if (response.m_status != YomkResponse::eOk)
    {
        YOMK_DEBUG_TAG("main", "call after unregister rejected as expected: ", response.m_msg);
    }
    else
    {
        YOMK_ERROR_TAG("main", "call after unregister should have been rejected");
    }

    /**
     * 步骤5：重复注销（预期失败）
     *
     * 函数已不在函数池中，重复注销应返回 eInvalid
     */
    response = YOMK_FUNCTIONPOOL_UNREGISTER("func1");
    if (response.m_status != YomkResponse::eOk)
    {
        YOMK_DEBUG_TAG("main", "unregister again rejected as expected: ", response.m_msg);
    }
    else
    {
        YOMK_ERROR_TAG("main", "unregister again should have been rejected");
    }

    YOMK_DEBUG_TAG("main", "test YomkFunctionPool completed, any key to continue...");

    getchar();

    return 0;
}
