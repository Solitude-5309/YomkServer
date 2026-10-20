#!/usr/bin/env python3
"""
ExampleYomkEventLoop —— 线程隔离的事件循环示例

YomkEventLoop：每个循环运行在独立线程中
- 同一循环内保证顺序执行，不同循环间并行处理
- 适用于 I/O 密集型和高并发任务调度

本示例演示单个循环的完整生命周期：
start（绑定处理函数）-> post（非阻塞投递）-> post_wait（阻塞投递，
回包携带事件 id 与执行结果）-> stop -> destroy
"""

import threading
from typing import Any

import YomkApi


def print_step(n, title, explain):
    print(f"\n====== 步骤{n}：{title} ======")
    print(f">> {explain}")


def event_handle(pkg: Any) -> YomkApi.YomkResponse:
    tid = threading.get_ident()
    YomkApi.console_log_info_tag("loop.work", "exec event: ", pkg,
                                 " (thread: ", tid, ")")
    return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "exec event_handle success")


def main():
    tid = threading.get_ident()

    print_step(
        1, "框架就绪", "YomkApi.init 启动内置服务（YomkContext / YomkFunctionPool / YomkEventLoop）；YomkApi.__version__ 返回框架版本号")
    YomkApi.init()
    print(f"[thread {tid}] YomkApi.__version__: {YomkApi.__version__}")

    print_step(2, "启动事件循环", "event_loop_start 创建独立线程循环并绑定处理函数（处理函数与主线程线程 ID 不同）")
    res = YomkApi.event_loop_start("loop_1", event_handle)
    print(f"[thread {tid}] event_loop_start: {res.msg}")

    print_step(3, "非阻塞投递", "event_loop_post 立即返回，事件由循环线程异步执行")
    res = YomkApi.event_loop_post("loop_1", "event_data")
    print(f"[thread {tid}] event_loop_post: {res.msg}")

    print_step(4, "阻塞投递", "event_loop_post_wait 等待执行完成，回包 data 携带事件 id、循环名与处理结果")
    res = YomkApi.event_loop_post_wait("loop_1", "event_data_wait")
    print(f"[thread {tid}] event_loop_post_wait: {res.msg}, event id: {res.data.m_eventId}, "
          f"loop: {res.data.m_eventLoopName}, res: {res.data.m_response.msg}")

    print_step(5, "停止", "event_loop_stop 停止循环（不再消费事件，循环对象仍存在）")
    res = YomkApi.event_loop_stop("loop_1")
    print(f"[thread {tid}] event_loop_stop: {res.msg}")

    print_step(6, "销毁", "event_loop_destroy 销毁循环与线程，彻底释放资源")
    res = YomkApi.event_loop_destroy("loop_1")
    print(f"[thread {tid}] event_loop_destroy: {res.msg}")


if __name__ == "__main__":
    main()
