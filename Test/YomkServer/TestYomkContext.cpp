/**
 * @file TestYomkContext.cpp
 * @brief YomkContext 全局状态管理示例
 *
 * 演示内容：
 * 1. Context 的创建、获取、设置、销毁
 * 2. Checker 机制：在设置前校验，防止非法修改
 * 3. Monitor 机制：在设置成功后监控，接收变更通知，设置失败，则不会发出变更通知
 *
 * Context 是全局共享的 K-V 状态机，用于：
 * - 跨服务共享状态
 * - 配置管理
 * - 运行时数据传递
 */

#include <iostream>
#include "YomkAPI.h"

// 定点引入所需类型，避免在头文件中 using namespace（第十一轮）
using yomk::ContextChecker;

/**
 * @brief Checker 函数 - 接受模式
 *
 * 在 Context 设置前被调用，用于校验新值是否合法
 * 返回 eAccept 允许设置，返回 eReject 拒绝设置
 *
 * 本函数接受所有非空字符串
 */
ContextChecker::ECheckStatus checkerAcceptFunc(const yomk::Context &ctx)
{
    // 解包新值：使用 YomkUnPackPkg（不自动 return，需手动检查）
    YomkUnPackPkg(ctx.m_value, String, str);
    if (!str)
    {
        YOMK_ERROR_TAG("checkerAcceptFunc", "checker accept func called. value is null");
        return ContextChecker::eReject; // 值为空，拒绝
    }
    YOMK_DEBUG_TAG("checkerAcceptFunc", "checker accept func called. ctx: key = ", ctx.m_key, ", value = ", str->d);
    return ContextChecker::eAccept; // 接受设置
}

/**
 * @brief Checker 函数 - 拒绝模式
 *
 * 演示如何拒绝所有设置请求
 * 用于保护关键配置不被修改
 */
ContextChecker::ECheckStatus checkerRejectFunc(const yomk::Context &ctx)
{
    YomkUnPackPkg(ctx.m_value, String, str);
    if (!str)
    {
        YOMK_ERROR_TAG("checkerRejectFunc", "checker reject func called. value is null");
        return ContextChecker::eReject;
    }
    YOMK_DEBUG_TAG("checkerRejectFunc", "checker reject func called. ctx: key = ", ctx.m_key, ", value = ", str->d);

    return ContextChecker::eReject; // 拒绝所有设置
}

/**
 * @brief Monitor 函数
 *
 * 在 Context 设置成功后被调用，用于监控状态变化
 * 可以记录日志、触发其他操作等
 */
void monitorFunc(const yomk::Context &ctx)
{
    // 解包新值：使用 YomkUnPackPkgVoid（void 函数，失败自动 return）
    YomkUnPackPkgVoid(ctx.m_value, String, str);
    // 打印变更的 key 和新值
    YOMK_DEBUG_TAG("monitorFunc", "monitor func called. ctx: key= ", ctx.m_key, ", value = ", str->d);
}

/**
 * @brief 程序入口
 *
 * 演示 Context 的完整生命周期：
 * 创建 -> 获取 -> 设置 -> 开启 Checker/Monitor -> 带校验的设置 -> 拒绝设置 -> 销毁
 */
int main(int argc, char *argv[])
{
    // 初始化框架
    YOMK_INIT();

    // 测试 YOMK_VERSION：获取并输出框架版本号（对应 project(Yomk VERSION x.x.x) 定义的 VERSION）
    YOMK_INFO_TAG("main", "YomkServer version: ", YOMK_VERSION);

    YomkResponse response;

    /**
     * 步骤1：创建 Context
     *
     * 创建键值对：key="ctx", value="ctx_data"
     * 值必须是 YomkPkgPtr 类型
     */
    response = YOMK_CONTEXT_CREATE("ctx", YomkMkPtr(String, "ctx_data"));
    if (response.m_status == YomkResponse::eOk)
    {
        YOMK_DEBUG_TAG("main", "create context [ ctx = ctx_data ] success");
    }
    else
    {
        YOMK_ERROR_TAG("main", "create context [ ctx = ctx_data ] failed");
    }

    /**
     * 步骤2：获取 Context
     *
     * 获取键 "ctx" 的值，如果不存在则返回默认值 "ctx_data_default"
     * 模板参数 String 指定返回类型
     */
    YomkPtr(String) ctx_data = YOMK_CONTEXT_GET(String, "ctx", YomkMkPtr(String, "ctx_data_default"));
    YOMK_DEBUG_TAG("main", "get ctx: ", ctx_data->d); // 输出: ctx_data

    /**
     * 步骤3：设置 Context（无 Checker 校验）
     *
     * 更新键 "ctx" 的值为 "ctx_data_set"
     * 此时未开启 Checker，直接设置成功
     */
    response = YOMK_CONTEXT_SET("ctx", YomkMkPtr(String, "ctx_data_set"));
    if (response.m_status == YomkResponse::eOk)
    {
        YOMK_DEBUG_TAG("main", "set context [ ctx = ctx_data_set ] success");
    }
    else
    {
        YOMK_ERROR_TAG("main", "set context [ ctx = ctx_data_set ] failed");
    }

    // 再次获取，验证更新成功
    ctx_data = YOMK_CONTEXT_GET(String, "ctx", YomkMkPtr(String, "ctx_data_default"));
    YOMK_DEBUG_TAG("main", "get ctx: ", ctx_data->d); // 输出: ctx_data_set

    /**
     * 步骤4：开启 Checker 机制
     *
     * 开启后，每次 CONTEXT_SET 都会先调用 Checker 函数校验
     * 只有 Checker 返回 eAccept 才允许设置
     */
    response = YOMK_CONTEXT_ON_CHECKER();
    if (response.m_status == YomkResponse::eOk)
    {
        YOMK_DEBUG_TAG("main", "turn on checker success");
    }
    else
    {
        YOMK_ERROR_TAG("main", "turn on checker failed");
    }

    /**
     * 步骤5：为键 "ctx" 设置 Checker 函数
     *
     * 使用 checkerAcceptFunc，接受所有非空字符串
     */
    response = YOMK_CONTEXT_SET_CHECKER("ctx", checkerAcceptFunc);
    if (response.m_status == YomkResponse::eOk)
    {
        YOMK_DEBUG_TAG("main", "set accept checker success");
    }
    else
    {
        YOMK_ERROR_TAG("main", "set accept checker failed");
    }

    /**
     * 步骤6：开启 Monitor 机制
     *
     * 开启后，每次 CONTEXT_SET 成功后都会调用 Monitor 函数
     * 用于监控状态变化
     */
    response = YOMK_CONTEXT_ON_MONITOR();
    if (response.m_status == YomkResponse::eOk)
    {
        YOMK_DEBUG_TAG("main", "turn on monitor success");
    }
    else
    {
        YOMK_ERROR_TAG("main", "turn on monitor failed");
    }

    /**
     * 步骤7：为键 "ctx" 设置 Monitor 函数
     */
    response = YOMK_CONTEXT_SET_MONITOR("ctx", monitorFunc, true);
    if (response.m_status == YomkResponse::eOk)
    {
        YOMK_DEBUG_TAG("main", "set monitor success");
    }
    else
    {
        YOMK_ERROR_TAG("main", "set monitor failed");
    }

    /**
     * 步骤8：带 Checker 和 Monitor 的设置
     *
     * 执行流程：
     * 1. 调用 checkerAcceptFunc 校验 -> 接受
     * 2. 真正设置新值
     * 3. 调用 monitorFunc 通知变更
     */
    response = YOMK_CONTEXT_SET("ctx", YomkMkPtr(String, "ctx_data_set_2"));
    if (response.m_status == YomkResponse::eOk)
    {
        YOMK_DEBUG_TAG("main", "set context [ ctx = ctx_data_set_2 ] success");
    }
    else
    {
        YOMK_ERROR_TAG("main", "set context [ ctx = ctx_data_set_2 ] failed");
    }

    // 获取更新后的值
    ctx_data = YOMK_CONTEXT_GET(String, "ctx", YomkMkPtr(String, "ctx_data_default"));
    YOMK_DEBUG_TAG("main", "get ctx: ", ctx_data->d); // 输出: ctx_data_set_2

    /**
     * 步骤9：切换为拒绝模式的 Checker
     *
     * 使用 checkerRejectFunc，拒绝所有设置请求
     */
    response = YOMK_CONTEXT_SET_CHECKER("ctx", checkerRejectFunc);
    if (response.m_status == YomkResponse::eOk)
    {
        YOMK_DEBUG_TAG("main", "set reject checker success");
    }
    else
    {
        YOMK_ERROR_TAG("main", "set reject checker failed");
    }

    /**
     * 步骤10：被拒绝的设置
     *
     * 执行流程：
     * 1. 调用 checkerRejectFunc 校验 -> 拒绝
     * 2. 设置失败，不会调用 Monitor
     */
    response = YOMK_CONTEXT_SET("ctx", YomkMkPtr(String, "ctx_data_set_3"));
    if (response.m_status == YomkResponse::eOk)
    {
        YOMK_ERROR_TAG("main", "set context [ ctx = ctx_data_set_3 ] success");
    }
    else
    {
        YOMK_ERROR_TAG("main", "set context [ ctx = ctx_data_set_3 ] failed"); // 预期输出
    }

    // 获取值，验证未被更新
    ctx_data = YOMK_CONTEXT_GET(String, "ctx", YomkMkPtr(String, "ctx_data_default"));
    YOMK_DEBUG_TAG("main", "get ctx: ", ctx_data->d); // 输出: ctx_data_set_2（未变）

    /**
     * 步骤11：销毁 Context
     *
     * 删除键 "ctx" 及其值
     */
    response = YOMK_CONTEXT_DESTROY("ctx");
    if (response.m_status == YomkResponse::eOk)
    {
        YOMK_DEBUG_TAG("main", "destroy context [ ctx ] success");
    }
    else
    {
        YOMK_ERROR_TAG("main", "destroy context [ ctx ] failed");
    }

    /**
     * 步骤12：获取已销毁的 Context
     *
     * 键已不存在，返回默认值 "ctx_data_default"
     */
    ctx_data = YOMK_CONTEXT_GET(String, "ctx", YomkMkPtr(String, "ctx_data_default"));
    YOMK_DEBUG_TAG("main", "get ctx: ", ctx_data->d); // 输出: ctx_data_default

    YOMK_DEBUG_TAG("main", "test YomkContext completed, any key to continue...");

    getchar();

    return 0;
}
