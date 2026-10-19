#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
TestYomkPkg.py
@brief 数据模型层独立测试（对齐 main Test/YomkServer/TestYomkPkg.cpp，基础轮子集）

覆盖内容（3 个 Section）：
S3. Event / EventLoopPkg 结构契约：默认构造字段、带参构造透传、handle() 有无
    serviceFunc、m_eventId 初值（入队时由框架分配）、m_waitCallback 赋值执行、tag 透传
S4. YomkResponse 契约：默认构造（eInvalid/空 msg/None data）、枚举值序
    （eInvalid=-1/eOk=0/eNo=1）、双参构造、三参构造 data 透传
S5. 框架结构体默认值（python 对应子集）：Function/CallFunction/Context/
    ContextChecker/ContextMonitor/YomkFuncInfo 默认构造与字段

剔除说明（语言机制剔除口径，非缺陷）：
- main S1（15 标量 + 15 Array 共 30 个标准类型消息包）与 S2（四种解包宏全分支）
  依赖 YomkMsg 宏与 dynamic_pointer_cast 体系：python 方针明确消息包为普通类、
  宏体系不复刻，无框架侧被测对象，不设对应用例
- main S5 中 LogFile/Log/ConsoleLogProxy/VoidPointer 为 Logger 模块与 C 语言特有
  结构，python 无对应类型，留模块轮
- main Event 默认构造 m_response 为 YomkResponse()（status=eInvalid）；python
  既有定稿 m_response 初始为 None，语义同为"未执行时 response 为空"

风格：纯脚本 + 失败计数，退出码非 0 表示存在失败用例（零第三方依赖）
"""

import sys

from YomkServerPy import (
    CallFunction,
    Context,
    ContextChecker,
    ContextMonitor,
    Event,
    EventLoopPkg,
    Function,
    ResStatus,
    YomkFuncInfo,
    YomkResponse,
)

g_failed = 0


def check(cond, msg):
    # 对齐 main CHECK 宏：[ OK ]/[FAIL] [行号] 行输出 + 失败计数
    global g_failed
    line = sys._getframe(1).f_lineno
    if cond:
        print(f"[ OK ] [line {line}] {msg}")
    else:
        print(f"[FAIL] [line {line}] {msg}")
        g_failed += 1


# ============ S3. Event / EventLoopPkg 结构契约 ============
def s3_event_and_pkg():
    print("===== S3. Event / EventLoopPkg 结构契约 =====")
    ev = Event()
    check(ev.m_eventLoopName == "" and ev.m_pkg is None and ev.m_serviceFunc is None,
          "Event 默认构造：loop 名空/pkg None/func None")
    check(ev.m_eventId == 0 and ev.m_tag == "" and ev.m_response is None and ev.m_waitCallback is None,
          "Event 默认构造：eventId 0/tag 空/response None/waitCallback None")

    handled = {"count": 0}

    def func(pkg):
        handled["count"] += 1
        return YomkResponse(ResStatus.eOk, "handled")

    ev3 = Event("/L1", "p", func)  # 三参形态（tag 走默认参，对齐 main 三参构造重载）
    check(ev3.m_eventLoopName == "/L1" and ev3.m_pkg == "p" and ev3.m_eventId == 0 and ev3.m_tag == "",
          "Event 三参构造：字段透传，eventId 保持 0（入队时由框架分配）")
    ev3.handle()
    check(handled["count"] == 1 and ev3.m_response.status == ResStatus.eOk and ev3.m_response.msg == "handled",
          "Event handle：执行 serviceFunc 并填充 response")

    ev4 = Event("/L2", None, None, "tag1")  # 四参形态（对齐 main 四参构造重载）
    check(ev4.m_tag == "tag1" and ev4.m_serviceFunc is None,
          "Event 四参形态：tag 透传，func 可空")

    ev_silent = Event()
    ev_silent.handle()
    check(ev_silent.m_response is None,
          "Event 默认 handle：无 serviceFunc 静默，response 不变")

    waited = {"flag": False}

    def wait_cb():
        waited["flag"] = True

    ev3.m_waitCallback = wait_cb
    ev3.m_waitCallback()
    check(waited["flag"], "Event waitCallback：可赋值并执行")

    pkg = EventLoopPkg("/LP", func, "String")
    check(pkg.m_eventloopName == "/LP" and pkg.m_defaultServiceFunc is func and pkg.m_msgName == "String",
          "EventLoopPkg 构造：loop 名/默认 func/msg 名透传")
    pkg_def = EventLoopPkg()
    check(pkg_def.m_eventloopName == "" and pkg_def.m_defaultServiceFunc is None and pkg_def.m_msgName == "",
          "EventLoopPkg 默认构造：loop 名空/func None/msg 名空")


# ============ S4. YomkResponse 契约 ============
def s4_yomk_response():
    print("===== S4. YomkResponse 契约 =====")
    r = YomkResponse()
    check(r.status == ResStatus.eInvalid and r.msg == "" and r.data is None,
          "YomkResponse 默认构造：eInvalid/msg 空/data None")
    check(ResStatus.eInvalid.value == -1 and ResStatus.eOk.value == 0 and ResStatus.eNo.value == 1,
          "枚举值序契约：eInvalid=-1/eOk=0/eNo=1")
    two = YomkResponse(ResStatus.eNo, "no msg")
    check(two.status == ResStatus.eNo and two.msg == "no msg" and two.data is None,
          "双参构造：status/msg 透传，data 默认 None")
    payload = {"k": "v"}
    three = YomkResponse(ResStatus.eOk, "ok", payload)
    check(three.status == ResStatus.eOk and three.msg == "ok" and three.data is payload,
          "三参构造：status/msg/data 透传")


# ============ S5. 框架结构体默认值 ============
def s5_struct_defaults():
    print("===== S5. 框架结构体默认值 =====")
    # python Function/CallFunction 无默认构造（参数必填），以空值构造对齐 main 结构体默认值断言
    f = Function("", None)
    check(f.name == "" and f.msg_name == "" and f.func is None,
          "Function 空值构造：名称/消息名空、func None")
    cf = CallFunction("", None)
    check(cf.name == "" and cf.pkg is None,
          "CallFunction 空值构造：名称空、pkg None")
    ctx = Context()
    check(ctx.key == "" and ctx.value is None,
          "Context 默认：key 空、value None")
    cck = ContextChecker()
    check(cck.key == "" and cck.check_func is None,
          "ContextChecker 默认：key 空、check func None")
    cmo = ContextMonitor()
    check(cmo.key == "" and cmo.monitor_func is None and cmo.async_monitor_func is False,
          "ContextMonitor 默认：key 空、func None、async False")
    fi = YomkFuncInfo()
    check(fi.func_name == "" and fi.msg_name == "",
          "YomkFuncInfo 默认：func_name/msg_name 空")


def main():
    s3_event_and_pkg()
    s4_yomk_response()
    s5_struct_defaults()

    print(("ALL PASSED" if g_failed == 0 else "SOME FAILED") +
          f" ({g_failed} failed)")
    return 0 if g_failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
