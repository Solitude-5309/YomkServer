#!/usr/bin/env python3
"""
ExampleYomkFunctionPool —— 动态函数池示例

YomkFunctionPool 统一函数注册与调度中心：
- 注册：function_pool_register(name, func)，面向过程开发范式，业务逻辑快速迭代；
  三参声明期望消息类型、四参附函数说明（均仅内省元数据）
- 调用：function_pool_call(name, pkg)
- 热替换：重复注册同名函数即覆盖，下一次调用执行新实现（运行时更新，无需重启）
"""

import threading
from typing import Any

import YomkApi


def print_step(n, title, explain):
    print(f"\n====== 步骤{n}：{title} ======")
    print(f">> {explain}")


def func_v1(pkg: Any) -> YomkApi.YomkResponse:
    YomkApi.console_log_info_tag("pool.func", "exec func_v1, with msg: ", pkg)
    return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "exec func_v1 success")


def func_v2(pkg: Any) -> YomkApi.YomkResponse:
    YomkApi.console_log_info_tag("pool.func", "exec func_v2, with msg: ", pkg)
    return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "exec func_v2 success")


def greet(pkg: Any) -> YomkApi.YomkResponse:
    return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "hello " + str(pkg))


def main():
    tid = threading.get_ident()

    print_step(
        1, "框架就绪", "YomkApi.init 启动内置服务（YomkContext / YomkFunctionPool / YomkEventLoop）；YomkApi.__version__ 返回框架版本号")
    YomkApi.init()
    print(f"[thread {tid}] YomkApi.__version__: {YomkApi.__version__}")

    print_step(2, "注册与调用", "function_pool_register 注册函数；function_pool_call 按函数名调用")
    res = YomkApi.function_pool_register("func", func_v1)
    print(f"[thread {tid}] register func: {res.msg}")
    res = YomkApi.function_pool_call("func", "func_data")
    print(f"[thread {tid}] call func: {res.msg}")

    print_step(3, "带类型与函数说明的注册",
               "三参声明期望消息类型、四参附函数说明（均仅内省元数据，调用不做类型校验）；info_name 单查返回结构化 FuncInfo（类型与说明在 data 字段）")
    res = YomkApi.function_pool_register("greet", greet, "String", "向指定的人问好")
    print(f"[thread {tid}] register greet: {res.msg}")
    res = YomkApi.function_pool_info_name("greet")
    fi = res.data
    print(f"[thread {tid}] info greet: msg={res.msg}"
          + (f", func_name={fi.func_name}, msg_name={fi.msg_name}, description={fi.description}"
             if fi is not None else ""))
    res = YomkApi.function_pool_call("greet", "yomk")
    print(f"[thread {tid}] call greet: {res.msg}")

    print_step(4, "热替换", "重新注册同名函数即覆盖，下一次调用执行新实现（运行时更新，无需重启）")
    res = YomkApi.function_pool_register("func", func_v2)
    print(f"[thread {tid}] re-register func: {res.msg}")
    res = YomkApi.function_pool_call("func", "func_data")
    print(f"[thread {tid}] call func again: {res.msg}")


if __name__ == "__main__":
    main()
