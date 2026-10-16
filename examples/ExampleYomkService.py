#!/usr/bin/env python3
"""
ExampleYomkService —— YomkService 服务定义与调用快速上手示例（面向初学者）

"一切皆服务"：把业务逻辑封装为 YomkService 子类，注册到框架后
任意位置用 URL "/服务名/函数名" 同步或异步调用。

多服务工程结构（组织方式本身也是教程，对齐 main 分支 Examples 布局）：
- msgs/DemoMsgs.py        消息包定义（服务间传输的数据结构）
- services/CalcService    计算服务（被本脚本直接请求，内部跨服务调审计）
- services/AuditService   审计服务（被 CalcService 调用）
- boot/DemoBoot.py        启动编排（before -> start -> after 三阶段）

步骤总览：
0. 框架就绪（BOOT 多阶段编排，注册 AuditService 与 CalcService）
1. 同步请求（request 等待回包，回包 data 携带数据）
2. 异步请求（async_request 立即返回，回调线程自证）
3. 跨服务调用与失败传播（CalcService.add 内部请求 AuditService.audit）
"""

import time
import threading

import YomkApi
from boot.DemoBoot import DemoBoot
from msgs.DemoMsgs import AddReq, EchoMsg


def print_step(n, title, explain):
    print(f"\n====== 步骤{n}：{title} ======")
    print(f">> {explain}")


def async_callback(res: YomkApi.YomkResponse) -> None:
    tid = threading.get_ident()
    print(f"[thread {tid}] async_callback: status={res.status}, msg={res.msg}")


def main():
    tid = threading.get_ident()

    print_step(
        0, "框架就绪", "YomkApi.boot 按 before -> start -> after 三阶段完成启动编排，DemoBoot 注册 AuditService 与 CalcService")
    ret = YomkApi.boot(DemoBoot(["/AuditService", "/CalcService"]))
    print(f"[thread {tid}] boot ret: {ret}（0 = 成功）")

    print_step(
        1, "同步请求", 'request 按 URL "/CalcService/echo" 调用服务并等待回包，回包 data 携带数据')
    res = YomkApi.request("/CalcService/echo", EchoMsg("hello yomk"))
    print(
        f"[thread {tid}] request /CalcService/echo: status={res.status}, msg={res.msg}, data={res.data}")

    print_step(2, "异步请求", "async_request 立即返回，回包到达时在回调线程输出（对比线程 ID 自证）")
    YomkApi.async_request("/CalcService/echo",
                          EchoMsg("hello async"), async_callback)
    print(f"[thread {tid}] async_request submitted")
    time.sleep(0.5)  # 等待异步回调完成（演示用途，业务代码按需选择同步原语）

    print_step(
        3, "跨服务调用", "CalcService.add 内部请求 /AuditService/audit 审计后再返回 AddResp 强类型回包；审计失败会传播为整体失败")
    res = YomkApi.request("/CalcService/add", AddReq(1, 2))
    print(f"[thread {tid}] request /CalcService/add: status={res.status}, msg={res.msg}, data.result={res.data.result}")


if __name__ == "__main__":
    main()
