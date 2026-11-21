#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
TestYomkMsg.py
@brief 标准工具层与标准内置集测试（闭环 1：YomkMsg / unpack / std_msg）

覆盖内容（7 个 Section）：
S1. TYPE_NAME 生成契约：规则 B 自动生成（父包.限定名）、顶层模块回退（无父包
    用完整 __module__）、显式定义覆盖自动值、孙类再生成（子类必然获得
    与父类不同的 TYPE_NAME）
S2. std 类结构契约：9 个 std 类 TYPE_NAME 字面断言（yomk.msgs.String 等全限定
    形态）、标量字段 value 默认值、数组字段 items 默认值、构造透传
S3. unpack 语义：命中返回原对象、跨类型未命中返回 None、子类不冒充父类
    （子类自动 TYPE_NAME 不同）、裸 str 对 String 被拒（A 方案精确身份语义）、
    体系外 cls 退化纯 isinstance、bool 是 int 子类的 Pythonic 放行
S4. type_of 两态：YomkMsg 实例报 TYPE_NAME、None 及体系外对象报 "null"
    （对齐 C++ typeOf：非消息即无运行时类型名，无第三态）
S5. type_mismatch 文案字面：标准格式 "pkg is not <expected>, got '<actual>'"，
    expected 传 cls.TYPE_NAME、actual 走 type_of 两态
S6. 框架 msg TYPE_NAME 全查（闭环 2）：22 类改继承 + 6 结构化 List，28 类
    TYPE_NAME 字面逐一断言；Event 的 yomk.event_loop re-export 同一性
S7. 结构化 List msg 结构契约（闭环 2）：6 类 items 默认/None 归一/透传，
    元素为 msg 类实例

语言机制说明（非功能缺口）：
- 测试脚本以顶层脚本运行，__module__ 为 "__main__"（无父包），本地定义
  的 msg 类 TYPE_NAME 走顶层回退路径（"__main__.Xxx"），本身即回退用例
- 动态 type() 构造可注入自定义 __module__，用于精确断言规则 B 的父包
  拼接行为
- bool 是 int 的子类：unpack(True, int) 放行为 Pythonic 行为（文档化，
  不加特判）

风格：纯脚本 + 失败计数，退出码非 0 表示存在失败用例（零第三方依赖）
"""

import sys

from yomk.msgs import (
    Bool,
    BoolArray,
    Bytes,
    Float64,
    FloatArray,
    Int64,
    IntArray,
    String,
    StringArray,
    YomkMsg,
    type_mismatch,
    type_of,
    unpack,
)
from yomk.msgs.ctx import (
    Context,
    ContextChecker,
    ContextGroupInfo,
    ContextGroupInfoList,
    ContextGroupKey,
    ContextKeyInfo,
    ContextKeyInfoList,
    ContextMonitor,
)
from yomk.msgs.event_loop import Event, EventLoopInfo, EventLoopInfoList, EventLoopOpts, EventLoopQuery
from yomk.msgs.fn_pool import CallFunction, FuncInfo, FuncInfoList, Function
from yomk.msgs.log import (
    ConsoleLogProxy,
    FileLogProxy,
    Log,
    LogFile,
    LoggerInfo,
    LoggerInfoList,
    LoggerQuery,
    LoggerState,
)
from yomk.msgs.server import Response
from yomk.msgs.srv_info import ServerServiceInfo, ServerServiceInfoList

g_failed = 0


def check(cond, msg):
    # [ OK ]/[FAIL] [行号] 行输出 + 失败计数
    global g_failed
    line = sys._getframe(1).f_lineno
    if cond:
        print(f"[ OK ] [line {line}] {msg}")
    else:
        print(f"[FAIL] [line {line}] {msg}")
        g_failed += 1


# ============ S1. TYPE_NAME 生成契约（模块级类：qualname 无作用域段） ============
class S1AutoMsg(YomkMsg):
    pass


class S1OverrideMsg(YomkMsg):
    TYPE_NAME = "override::name"         # 显式定义覆盖自动值（类体字面量优先）


class S1ChildMsg(String):
    pass


def s1_type_name_rules():
    print("===== S1. TYPE_NAME 生成契约 =====")

    # 规则 B + 顶层回退：脚本顶层运行，__module__ = "__main__"（无父包）
    # → 回退用完整 __module__ 拼接
    check(S1AutoMsg.TYPE_NAME == "__main__.S1AutoMsg",
          f"规则 B 顶层回退: {S1AutoMsg.TYPE_NAME!r}")

    # 动态 type() 注入自定义 __module__：取最近一级父包（最后一段是模块名，剥掉）
    widget = type("Widget", (YomkMsg,), {"__module__": "app.widget"})
    check(widget.TYPE_NAME == "app.Widget", f"规则 B 父包拼接: {widget.TYPE_NAME!r}")

    # 深层父包：同理剥掉末段模块名
    deep = type("Deep", (YomkMsg,), {"__module__": "a.b.c.mod"})
    check(deep.TYPE_NAME == "a.b.c.Deep", f"深层父包剥文件段: {deep.TYPE_NAME!r}")

    # 显式定义覆盖自动值
    check(S1OverrideMsg.TYPE_NAME == "override::name",
          f"显式定义覆盖: {S1OverrideMsg.TYPE_NAME!r}")

    # 孙类再生成：未显式定义的子类自动获得与父类不同的 TYPE_NAME
    check(S1ChildMsg.TYPE_NAME == "__main__.S1ChildMsg",
          f"孙类再生成: {S1ChildMsg.TYPE_NAME!r}")
    check(S1ChildMsg.TYPE_NAME != String.TYPE_NAME, "子类 TYPE_NAME 必然不同于父类")

    # qualname 保真：函数内定义的类带作用域段（<locals>），如实入名
    class S1LocalMsg(YomkMsg):
        pass

    check(".<locals>." in S1LocalMsg.TYPE_NAME,
          f"函数作用域 qualname 保真: {S1LocalMsg.TYPE_NAME!r}")

    # 基类自身 TYPE_NAME 为空字面量（不参与比对）
    check(YomkMsg.TYPE_NAME == "", "基类 TYPE_NAME 为空")


# ============ S2. std 类结构契约 ============
def s2_std_classes():
    print("===== S2. std 类结构契约 =====")

    # 9 个 std 类 TYPE_NAME 字面断言（全限定形态，对齐 C++ 全限定口径）
    expected_type_names = {
        Bool: "yomk.msgs.Bool",
        Int64: "yomk.msgs.Int64",
        Float64: "yomk.msgs.Float64",
        String: "yomk.msgs.String",
        Bytes: "yomk.msgs.Bytes",
        BoolArray: "yomk.msgs.BoolArray",
        IntArray: "yomk.msgs.IntArray",
        FloatArray: "yomk.msgs.FloatArray",
        StringArray: "yomk.msgs.StringArray",
    }
    for cls, type_name in expected_type_names.items():
        check(cls.TYPE_NAME == type_name, f"{cls.__name__}.TYPE_NAME == {type_name!r}")

    # 标量字段 value：默认值 + 构造透传
    check(Bool().value is False, "Bool 默认 value=False")
    check(Bool(True).value is True, "Bool(True) 透传")
    check(Int64().value == 0, "Int64 默认 value=0")
    check(Int64(42).value == 42, "Int64(42) 透传")
    check(Float64().value == 0.0, "Float64 默认 value=0.0")
    check(Float64(1.5).value == 1.5, "Float64(1.5) 透传")
    check(String().value == "", "String 默认 value=''")
    check(String("hello").value == "hello", "String('hello') 透传")
    check(Bytes().value == b"", "Bytes 默认 value=b''")
    check(Bytes(b"\x00\x01").value == b"\x00\x01", "Bytes 透传")

    # 数组字段 items：默认空 list + 构造透传（None 归一为空 list）
    for cls in (BoolArray, IntArray, FloatArray, StringArray):
        check(cls().items == [], f"{cls.__name__}() 默认 items==[]")
        check(cls(None).items == [], f"{cls.__name__}(None) 归一为 []")

    check(BoolArray([True, False]).items == [True, False], "BoolArray 透传")
    check(IntArray([1, 2]).items == [1, 2], "IntArray 透传")
    check(FloatArray([0.5]).items == [0.5], "FloatArray 透传")
    check(StringArray(["a"]).items == ["a"], "StringArray 透传")


# ============ S3. unpack 语义 ============
def s3_unpack():
    print("===== S3. unpack 语义 =====")

    s = String("hit")

    # 命中：返回原对象（同一实例，非拷贝）
    check(unpack(s, String) is s, "命中返回原对象")

    # 跨类型未命中：None（对齐 C++ nullptr）
    check(unpack(Int64(1), String) is None, "跨类型未命中 → None")
    check(unpack(BoolArray([True]), StringArray) is None, "数组跨类型未命中 → None")

    # 子类不冒充父类：子类自动 TYPE_NAME 与父类不同（精确身份匹配）
    class S3SubString(String):
        pass

    check(unpack(S3SubString("sub"), String) is None,
          "子类实例解包为父类 → None（精确身份匹配）")
    check(unpack(S3SubString("sub"), S3SubString) is not None,
          "子类对自身解包命中")

    # A 方案精确身份语义：裸 str 不是 String 消息
    check(unpack("bare", String) is None, "裸 str 对 String 被拒")

    # None 入参
    check(unpack(None, String) is None, "None → None")

    # 体系外 cls 退化纯 isinstance（无 TYPE_NAME 的类不拒绝子类）
    check(unpack("text", str) == "text", "体系外 cls 退化: str 命中")
    check(unpack(123, str) is None, "体系外 cls 退化: 非 str 未命中")
    check(unpack(5, int) == 5, "体系外 cls 退化: int 命中")

    # bool 是 int 子类：Pythonic 放行（文档化行为，不加特判）
    check(unpack(True, int) is True, "bool 对 int 放行（Pythonic）")


# ============ S4. type_of 两态 ============
def s4_type_of():
    print("===== S4. type_of 两态 =====")

    # 第一态：YomkMsg 实例 → TYPE_NAME
    check(type_of(String("x")) == "yomk.msgs.String", "消息实例 → TYPE_NAME")
    check(type_of(Int64(1)) == "yomk.msgs.Int64", "Int64 实例 → TYPE_NAME")
    check(type_of(StringArray(["a"])) == "yomk.msgs.StringArray", "数组实例 → TYPE_NAME")

    # 第二态：None 及体系外对象 → "null"（对齐 C++ typeOf：非消息即无运行时类型名）
    check(type_of(None) == "null", "None → 'null'")
    check(type_of("bare") == "null", "裸 str → 'null'")
    check(type_of(123) == "null", "int → 'null'")
    check(type_of(["raw"]) == "null", "裸 list → 'null'")
    check(type_of(type("Loose", (), {})) == "null", "体系外类 → 'null'")


# ============ S5. type_mismatch 文案字面 ============
def s5_type_mismatch():
    print("===== S5. type_mismatch 文案字面 =====")

    # 标准格式："pkg is not <expected>, got '<actual>'"
    check(type_mismatch(None, String.TYPE_NAME)
          == "pkg is not yomk.msgs.String, got 'null'",
          "None 入参文案")
    check(type_mismatch(Bool(True), String.TYPE_NAME)
          == "pkg is not yomk.msgs.String, got 'yomk.msgs.Bool'",
          "消息实例入参文案（got 侧报 TYPE_NAME）")
    check(type_mismatch("bare", String.TYPE_NAME)
          == "pkg is not yomk.msgs.String, got 'null'",
          "体系外对象文案（非消息 → 'null'）")
    check(type_mismatch(Int64(1), Bool.TYPE_NAME)
          == "pkg is not yomk.msgs.Bool, got 'yomk.msgs.Int64'",
          "期望/实际双侧均为 TYPE_NAME")


# ============ S6. 框架 msg TYPE_NAME 全查（闭环 2） ============
def s6_framework_type_names():
    print("===== S6. 框架 msg TYPE_NAME 全查 =====")

    # 22 类改继承 + 6 个结构化 List，TYPE_NAME 全部规则 B 自动生成，字面逐一钉死
    expected = {
        # server
        Response: "yomk.msgs.server.Response",
        # ctx
        Context: "yomk.msgs.ctx.Context",
        ContextGroupKey: "yomk.msgs.ctx.ContextGroupKey",
        ContextKeyInfo: "yomk.msgs.ctx.ContextKeyInfo",
        ContextKeyInfoList: "yomk.msgs.ctx.ContextKeyInfoList",
        ContextGroupInfo: "yomk.msgs.ctx.ContextGroupInfo",
        ContextGroupInfoList: "yomk.msgs.ctx.ContextGroupInfoList",
        ContextChecker: "yomk.msgs.ctx.ContextChecker",
        ContextMonitor: "yomk.msgs.ctx.ContextMonitor",
        # event_loop（Event 迁入 msgs/event_loop；start 载荷定名 EventLoopOpts）
        Event: "yomk.msgs.event_loop.Event",
        EventLoopOpts: "yomk.msgs.event_loop.EventLoopOpts",
        EventLoopQuery: "yomk.msgs.event_loop.EventLoopQuery",
        EventLoopInfo: "yomk.msgs.event_loop.EventLoopInfo",
        EventLoopInfoList: "yomk.msgs.event_loop.EventLoopInfoList",
        # fn_pool
        Function: "yomk.msgs.fn_pool.Function",
        CallFunction: "yomk.msgs.fn_pool.CallFunction",
        FuncInfo: "yomk.msgs.fn_pool.FuncInfo",
        FuncInfoList: "yomk.msgs.fn_pool.FuncInfoList",
        # log
        LogFile: "yomk.msgs.log.LogFile",
        Log: "yomk.msgs.log.Log",
        ConsoleLogProxy: "yomk.msgs.log.ConsoleLogProxy",
        FileLogProxy: "yomk.msgs.log.FileLogProxy",
        LoggerQuery: "yomk.msgs.log.LoggerQuery",
        LoggerInfo: "yomk.msgs.log.LoggerInfo",
        LoggerInfoList: "yomk.msgs.log.LoggerInfoList",
        LoggerState: "yomk.msgs.log.LoggerState",
        # srv_info
        ServerServiceInfo: "yomk.msgs.srv_info.ServerServiceInfo",
        ServerServiceInfoList: "yomk.msgs.srv_info.ServerServiceInfoList",
    }
    for cls, type_name in expected.items():
        check(cls.TYPE_NAME == type_name, f"{cls.__name__}.TYPE_NAME == {type_name!r}")

    # Event 兼容路径：from yomk.event_loop import Event 与 msgs 路径同一类对象
    import yomk.event_loop
    check(yomk.event_loop.Event is Event,
          "yomk.event_loop.Event re-export 与 msgs 路径同一类")


# ============ S7. 结构化 List msg 结构契约（闭环 2） ============
def s7_structured_lists():
    print("===== S7. 结构化 List msg 结构契约 =====")

    # 统一 items 字段：默认空 list、None 归一、构造透传
    samples = {
        ContextKeyInfoList: ContextKeyInfoList([ContextKeyInfo("k")]),
        ContextGroupInfoList: ContextGroupInfoList([ContextGroupInfo("g")]),
        EventLoopInfoList: EventLoopInfoList([EventLoopInfo("loop")]),
        LoggerInfoList: LoggerInfoList([LoggerInfo("lg")]),
        FuncInfoList: FuncInfoList([FuncInfo("fn")]),
        ServerServiceInfoList: ServerServiceInfoList([ServerServiceInfo("/svc")]),
    }
    for cls, inst in samples.items():
        check(cls().items == [], f"{cls.__name__}() 默认 items==[]")
        check(cls(None).items == [], f"{cls.__name__}(None) 归一为 []")
        check(len(inst.items) == 1, f"{cls.__name__} 构造透传")
        check(isinstance(inst.items[0], YomkMsg),
              f"{cls.__name__} 元素为 msg 类实例")


def main():
    s1_type_name_rules()
    s2_std_classes()
    s3_unpack()
    s4_type_of()
    s5_type_mismatch()
    s6_framework_type_names()
    s7_structured_lists()

    print("===== summary =====")
    if g_failed:
        print(f"[FAIL] TestYomkMsg: {g_failed} failed")
        return 1
    print("[PASS] TestYomkMsg: all passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
