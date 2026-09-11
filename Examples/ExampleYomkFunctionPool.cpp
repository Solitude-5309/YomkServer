/**
 * @file ExampleYomkFunctionPool.cpp
 * @brief YomkFunctionPool 动态函数池快速上手示例（面向初学者）
 *
 * 函数池是一个扁平的公共函数中心：把符合 YomkResponse(YomkPkgPtr)
 * 签名的函数按名字注册进池子，之后在任意位置按名字调用——
 * 适合跨服务共享的无状态工具函数，支持运行时注册、热替换、注销。
 *
 * 本示例按 6 个步骤演示全部用法。每一步先打印横幅说明
 * "接下来做什么、预期看到什么"，再调用 API，随后用返回值与内省
 * 结果自证行为——只读运行输出即可明白每个 API 调用发生了什么。
 *
 * 步骤总览：
 * 0. 框架就绪（函数池服务随 YOMK_INIT 自动启动）
 * 1. 注册并调用（最小数据流闭环）
 * 2. 带消息类型名的注册（MsgName 内省元数据）
 * 3. 无参调用与空参守卫（pkg 可为 nullptr）
 * 4. 同名热替换（重复注册即更新，无需注销）
 * 5. 内省三件套（清单 / 单查 / 全量状态）
 * 6. 注销与错误分支（注销后调用、重复注销）
 *
 * 涉及的 API 宏（YomkAPI.h）：
 *   YOMK_FUNCTIONPOOL_REGISTER        注册函数（2 参 / 3 参带 MsgName）
 *   YOMK_FUNCTIONPOOL_CALL            按名调用
 *   YOMK_FUNCTIONPOOL_UNREGISTER      注销函数
 *   YOMK_FUNCTIONPOOL_INFO_NAMES      清单：全部函数名
 *   YOMK_FUNCTIONPOOL_INFO_NAME       单查：函数元信息
 *   YOMK_FUNCTIONPOOL_INFO_ALL        全量：数量 + 全部函数元信息
 */

#include <iostream>

#include "YomkAPI.h"

// ---------------------------------------------------------------------------
// 辅助输出：横幅与返回值打印。
// 横幅走 std::cout 而非框架日志，保证叙事在任何日志开关状态下可见。
// ---------------------------------------------------------------------------

static void printStep(int n, const std::string& title, const std::string& explain)
{
    std::cout << "\n====== 步骤" << n << "：" << title << " ======" << std::endl;
    std::cout << ">> " << explain << std::endl;
}

// 打印 YomkResponse 三要素：status/msg/m_data，展示调用契约
static void printResp(const std::string& prefix, const YomkResponse& resp)
{
    // 三态：eOk=0 成功；eNo=1 名字不存在或被拒绝；eInvalid=-1 参数无效或未初始化
    std::cout << "[" << prefix << "] status=" << resp.m_status << ", msg=\"" << resp.m_msg << "\"";
    if (resp.m_data)
    {
        YomkUnPackPkg(resp.m_data, String, data);
        if (data)
        {
            std::cout << ", data=\"" << data->d << "\"";
        }
    }
    std::cout << std::endl;
}

// 解包并打印内省返回的 StringArray（INFO_NAMES / INFO_ALL 的返回形态）
static void dumpLines(const std::string& prefix, const YomkResponse& resp)
{
    printResp(prefix, resp);
    YomkUnPackPkg(resp.m_data, StringArray, arr);
    if (!arr)
    {
        std::cout << ">> (no data)" << std::endl;
        return;
    }
    for (const auto& line : arr->d)
    {
        std::cout << ">> | " << line << std::endl;
    }
}

// ---------------------------------------------------------------------------
// 可注册进函数池的函数：签名统一为 YomkResponse(YomkPkgPtr)
// ---------------------------------------------------------------------------

/**
 * @brief 求和函数：演示完整数据流
 *
 * 入参：StringArray 消息包（一组数字字符串）
 * 出参：求和结果同时放进 msg 与 m_data，CALL 调用方原样拿到
 */
YomkResponse calcSum(YomkPkgPtr pkg)
{
    // 解包入参：宏已自动判空，失败自动返回 {eNo, "错误信息"}
    YomkUnPackPkgResponse(pkg, StringArray, nums);

    YOMK_INFO_TAG("pool.func", "calcSum called,收到 ", nums->d.size(), " 个数字");

    int sum = 0;
    for (const auto& num : nums->d)
    {
        sum += std::stoi(num);
    }
    YOMK_INFO_TAG("pool.func", "calcSum finished, sum=", sum);

    // msg 描述结果，m_data 携带结果数据——调用方两者都能拿到
    return {YomkResponse::eOk, "sum=" + std::to_string(sum), YomkMkPtr(String, std::to_string(sum))};
}

/**
 * @brief 问候函数：演示带 MsgName 的注册与空参守卫
 *
 * 入参：String 消息包（名字）
 * 出参：问候语
 */
YomkResponse greet(YomkPkgPtr pkg)
{
    // pkg 为 nullptr 时宏自动返回 {eNo, ...}——空参守卫是统一契约
    YomkUnPackPkgResponse(pkg, String, name);
    YOMK_INFO_TAG("pool.func", "greet called, name=", name->d);
    return {YomkResponse::eOk, "hello, " + name->d};
}

int main(int argc, char* argv[])
{
    // 初始化框架（函数池服务 /YomkFunctionPool 随之自动启动）
    YOMK_INIT();

    printStep(0, "框架就绪", "YOMK_INIT 已自动启动函数池服务；YOMK_VERSION 返回框架版本号。");
    printResp("YOMK_VERSION", YomkResponse(YomkResponse::eOk, YOMK_VERSION));

    /**
     * 步骤1：注册并调用（最小闭环）
     *
     * REGISTER 把函数按名字放进池子；CALL 按名字调用并把入参消息包传给它，
     * CALL 的返回值就是被调函数的返回值——入参流进函数、结果流回调用方。
     */
    printStep(
        1,
        "注册并调用（最小闭环）",
        "注册 tool.calcSum 后按名调用，传 3/4/5 求和；注意 pool.func 的函数内日志与返回的 data=sum。");
    printResp("REGISTER(tool.calcSum)", YOMK_FUNCTIONPOOL_REGISTER("tool.calcSum", calcSum));
    printResp(
        "CALL(tool.calcSum)",
        YOMK_FUNCTIONPOOL_CALL("tool.calcSum", YomkMkPtr(StringArray, std::vector<std::string>{"3", "4", "5"})));

    /**
     * 步骤2：带消息类型名的注册（3 参）
     *
     * 第三个参数 MsgName 声明该函数期望的消息类型名，仅作内省元数据，
     * 调用时框架不做类型校验；单查 INFO_NAME 会显示 "tool.greet [String]"。
     */
    printStep(
        2, "带消息类型名的注册", "用 3 参形式注册 tool.greet 并传名调用；INFO_NAME 单查可见 [String] 元数据后缀。");
    printResp("REGISTER(tool.greet,String)", YOMK_FUNCTIONPOOL_REGISTER("tool.greet", greet, String));
    printResp("INFO_NAME(tool.greet)", YOMK_FUNCTIONPOOL_INFO_NAME("tool.greet"));
    printResp("CALL(tool.greet)", YOMK_FUNCTIONPOOL_CALL("tool.greet", YomkMkPtr(String, "yomk")));

    /**
     * 步骤3：无参调用与空参守卫
     *
     * CALL 的入参消息包可为 nullptr；被调函数内的解包宏判空后
     * 自动返回 eNo——空参守卫是框架统一契约，无需函数自己写 if。
     */
    printStep(
        3, "无参调用与空参守卫", "传 nullptr 调用 tool.greet：函数内解包宏自动返回 eNo，调用方拿到统一契约消息。");
    printResp("CALL(tool.greet,nullptr)", YOMK_FUNCTIONPOOL_CALL("tool.greet", nullptr));

    /**
     * 步骤4：同名热替换
     *
     * 对已注册的名字再次 REGISTER，框架直接更新函数本体并返回
     * "find function name is already exist, update to current function"——
     * 运行时热替换，无需先注销；MsgName 元数据也随新注册一并刷新。
     */
    printStep(4, "同名热替换", "对 tool.greet 再注册一个 lambda 版本；再次调用返回新实现的结果。");
    // lambda 先赋给具名变量再注册：lambda 体内含逗号时，
    // 直接内联进宏会让预处理器把逗号误当实参分隔符
    YomkServiceFunc greetV2 = [](YomkPkgPtr pkg) -> YomkResponse
    {
        YomkUnPackPkgResponse(pkg, String, name);
        YOMK_INFO_TAG("pool.func", "greetV2 called, name=", name->d);
        return {YomkResponse::eOk, "hello V2, " + name->d};
    };
    printResp("REGISTER(tool.greet,V2,String)", YOMK_FUNCTIONPOOL_REGISTER("tool.greet", greetV2, String));
    printResp("CALL(tool.greet,V2)", YOMK_FUNCTIONPOOL_CALL("tool.greet", YomkMkPtr(String, "yomk")));

    /**
     * 步骤5：内省三件套
     *
     * INFO_NAMES：全部函数名（字典序）；
     * INFO_NAME：单查，命中返回元信息，未注册返回 eNo=1（not-found 惯例）；
     * INFO_ALL：首行 functions:N，其后每行 "函数名 [MsgName]"。
     */
    printStep(5, "内省三件套", "清单看全部、单查看一个、ALL 看数量与全量元信息；单查未注册名字演示 eNo。");
    dumpLines("INFO_NAMES", YOMK_FUNCTIONPOOL_INFO_NAMES());
    printResp("INFO_NAME(tool.greet)", YOMK_FUNCTIONPOOL_INFO_NAME("tool.greet"));
    printResp("INFO_NAME(ghost)", YOMK_FUNCTIONPOOL_INFO_NAME("ghost"));
    std::cout << ">> ghost 未注册 → eNo=1，not-found 是框架统一惯例" << std::endl;
    dumpLines("INFO_ALL", YOMK_FUNCTIONPOOL_INFO_ALL());

    /**
     * 步骤6：注销与错误分支
     *
     * UNREGISTER 把函数移出池子；注销后再调用返回 eNo（not-found），
     * 重复注销同样 eNo——注意：not-found 是 eNo=1，参数无效才是 eInvalid=-1。
     */
    printStep(
        6,
        "注销与错误分支",
        "注销 tool.calcSum 后再调用应 eNo；重复注销也应 eNo；INFO_ALL 的 functions 计数随之减少。");
    printResp("UNREGISTER(tool.calcSum)", YOMK_FUNCTIONPOOL_UNREGISTER("tool.calcSum"));
    printResp("CALL(tool.calcSum,已注销)", YOMK_FUNCTIONPOOL_CALL("tool.calcSum", nullptr));
    printResp("UNREGISTER(tool.calcSum,重复)", YOMK_FUNCTIONPOOL_UNREGISTER("tool.calcSum"));
    dumpLines("INFO_ALL(收尾)", YOMK_FUNCTIONPOOL_INFO_ALL());

    std::cout << "\n====== 示例结束：按回车退出 ======" << std::endl;
    getchar();

    return 0;
}
