#!/usr/bin/env python3
"""
ExampleYomkContext —— 共享上下文示例

YomkContext 统一管理框架运行时的全局状态：
- 生命周期：context_create / context_get / context_set / context_destroy
  （context_get 支持默认值：key 不存在时返回默认值而不报错）
- 状态安全检查（Checker）：状态变更前执行验证逻辑，返回 eReject 拒绝本次迁移
- 状态监控（Monitor）：状态变化时触发回调，支持异步监控（async_monitor=True）
- 内省三件套：context_info_keys / context_info_key / context_info_all
  （key 单查命中为结构化 ContextKeyInfo 包：msg 恒 "ok"，数据走 data）
"""

import time

import YomkApi


def print_step(n, title, explain):
    print(f"\n====== 步骤{n}：{title} ======")
    print(f">> {explain}")


def checker_reject(ctx: YomkApi.Context) -> YomkApi.CheckStatus:
    YomkApi.console_log_info_tag(
        "ctx.checker", "reject check: key=", ctx.key, " -> eReject")
    return YomkApi.CheckStatus.eReject


def checker_accept(ctx: YomkApi.Context) -> YomkApi.CheckStatus:
    YomkApi.console_log_info_tag("ctx.checker", "accept check: key=", ctx.key,
                                 ", value=", ctx.value, " -> eAccept")
    return YomkApi.CheckStatus.eAccept


def monitor_func(ctx: YomkApi.Context) -> None:
    time.sleep(1)
    YomkApi.console_log_info_tag("ctx.monitor", "sync got snapshot: key=", ctx.key,
                                 ", value=", ctx.value)


def main():
    print_step(
        1, "框架就绪", "YomkApi.init 启动内置服务（YomkContext / YomkFunctionPool / YomkEventLoop）；YomkApi.__version__ 返回框架版本号")
    YomkApi.init()
    print(f"YomkApi.__version__: {YomkApi.__version__}")

    print_step(
        2, "创建与读取", "context_create 创建 key=ctx 的上下文；context_get 读取（key 不存在时返回默认值）")
    res = YomkApi.context_create("ctx", "ctx_data")
    print(f"context_create: {res.msg}")
    print(f"context_get: {YomkApi.context_get('ctx', 'ctx_data_default')}")

    print_step(3, "更新", "context_set 覆盖上下文数据")
    res = YomkApi.context_set("ctx", "ctx_data_set")
    print(f"context_set: {res.msg}")
    print(f"context_get: {YomkApi.context_get('ctx', 'ctx_data_default')}")

    print_step(4, "状态检查器（拒绝分支）与监控",
               "开启 checker/monitor 并设置校验函数；返回 eReject 时本次 set 被拒绝，值保持不变")
    YomkApi.context_turn_on_checker()
    YomkApi.context_set_checker("ctx", checker_reject)
    YomkApi.context_turn_on_monitor()
    YomkApi.context_set_monitor("ctx", monitor_func, True)

    res = YomkApi.context_set("ctx", "ctx_data_set_reject")
    print(f"context_set(被拒绝): {res.msg}")
    print(
        f"context_get(应仍为旧值): {YomkApi.context_get('ctx', 'ctx_data_default')}")
    time.sleep(1.5)  # 等待可能的异步监控回调输出

    print_step(5, "状态检查器（接受分支）", "更换校验函数为 eAccept，变更通过并触发监控回调")
    YomkApi.context_set_checker("ctx", checker_accept)
    res = YomkApi.context_set("ctx", "ctx_data_set_accept")
    print(f"context_set(通过): {res.msg}")
    print(f"context_get: {YomkApi.context_get('ctx', 'ctx_data_default')}")
    time.sleep(1.5)  # 等待异步监控回调输出

    print_step(6, "内省",
               "内省三件套看键清单与注册状态；key 单查命中为结构化返回：msg 恒 \"ok\"，data 为 ContextKeyInfo 包（键名/值类型名/门控/monitor 计数），不存在 key 返回 eNo")
    res = YomkApi.context_info_keys()
    print(f"context_info_keys: {res.data}")
    res = YomkApi.context_info_key("ctx")
    info = res.data
    print(f"context_info_key: msg={res.msg}, key={info.key}, type_name={info.type_name}, "
          f"checker_on={info.checker_on}, "
          f"monitor_count={info.monitor_count}(async:{info.async_monitor_count})")
    res = YomkApi.context_info_key("ghost")
    print(f"context_info_key(ghost): {res.msg}")
    res = YomkApi.context_info_all()
    print(f"context_info_all: {res.data}")

    print_step(7, "销毁", "context_destroy 删除上下文，此后读取返回默认值")
    res = YomkApi.context_destroy("ctx")
    print(f"context_destroy: {res.msg}")
    print(
        f"context_get(销毁后，返回默认值): {YomkApi.context_get('ctx', 'ctx_data_default')}")


if __name__ == "__main__":
    main()
