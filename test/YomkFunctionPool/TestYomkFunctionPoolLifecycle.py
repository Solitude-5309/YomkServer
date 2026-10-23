"""
TestYomkFunctionPoolLifecycle.py —— FunctionPool 模块生命周期与 CRUD 基线测试

（8 节 53 断言，零剔除）：
1. CRUD 闭环：REGISTER echo 函数 → CALL 回传消息 → UNREGISTER → CALL eNo
2. 注册边界：空函数名 / None func 均 eInvalid；重复注册为"更新"语义（CALL 走新实现，msg 含 update）
3. 注销边界：空名 eInvalid / 未注册名 eNo
4. 调用边界：空名 eInvalid / 未注册 eNo / pkg=None 原样透传给函数（探针计数）
5. 内省：info_names 存活列表包含注册名（空表陷阱教训）/ info_name 命中返回结构化 FuncInfo 包
   （msg 恒 ok）与未注册 eNo、空名 eInvalid / info_all 结构化 FuncInfoList 相对计数与字段断言
6. 注册 2 参/3 参/4 参分发映射：无类型（字段空）vs 声明类型（msg_name 字段）vs
   附函数说明（description 字段）；2 参同名热替换清除类型与说明
7. 异常契约：CALL 链（request→invoke→callFunction→用户函数）无 try/catch，
   用户函数异常穿透直达调用方——测试侧捕获验证 what；随后正常 CALL 验证池存活
8. 超长名：65536 字符函数名全生命周期（REGISTER/CALL/INFO_NAME/UNREGISTER）eOk 且完整回显

语言形态说明：
- Function 三形态：Function(name, func) 无类型 / Function(name, func, "String") 声明类型 /
  Function(name, func, "String", "说明") 附函数说明（经门面 function_pool_register 的
  msg_name 与 description 形参承载）
- 内省清单端点 info_names data 为 list[str]；info_name 与 info_all 命中均为 FuncInfo
  结构化（info_all 为 FuncInfo 列表，总数由列表长度承载）
- poolCount 的 SIZE_MAX 哨兵值以 None 承载
- 探针计数用 list.append（GIL 原子，T 轮既有模式）
- 全部经门面 API 测试，同时覆盖 invoke 路由链，无覆盖损失
"""

import sys

from YomkApi import (
    YomkResponse,
    ResStatus,
    FuncInfo,
    init,
    function_pool_register,
    function_pool_unregister,
    function_pool_call,
    function_pool_info_names,
    function_pool_info_name,
    function_pool_info_all,
)

PASS = 0
FAIL = 0


def check(cond, name):
    global PASS, FAIL
    if cond:
        PASS += 1
        print(f"[ OK ] {name}")
    else:
        FAIL += 1
        print(f"[FAIL] {name}")


# ---- 测试装置函数 ----

def echo_func(pkg):
    # echo：回传入参 pkg（验证 CALL 消息透传）
    return YomkResponse(ResStatus.eOk, "echo", pkg)


null_pkg_seen = []  # 探针观测容器（list.append GIL 原子，替代 std::atomic）


def null_probe_func(pkg):
    # probe：记录是否收到 None 入参（验证 pkg=None 原样透传）
    if pkg is None:
        null_pkg_seen.append(1)
    return YomkResponse(ResStatus.eOk, "probe")


def v1_func(pkg):
    # 版本函数：区分重复注册更新前的实现
    return YomkResponse(ResStatus.eOk, "v1")


def v2_func(pkg):
    # 版本函数：区分重复注册更新后的实现
    return YomkResponse(ResStatus.eOk, "v2")


def throw_func(pkg):
    # 抛异常函数：验证 CALL 链异常穿透契约
    raise RuntimeError("fp_boom")


def alive_func(pkg):
    # 正常存活函数
    return YomkResponse(ResStatus.eOk, "alive")


def pool_count():
    # 从 info_all 结构化列表取当前函数总数（列表长度承载）；失败返回 None（SIZE_MAX 哨兵映射）
    r = function_pool_info_all()
    if r.status != ResStatus.eOk or not isinstance(r.data, list):
        return None
    return len(r.data)


server = init(1)
check(server is not None, "init 返回非空服务器")

# ============ Section 1: CRUD 闭环 ============
r = function_pool_register("fp_echo", echo_func)
check(r.status == ResStatus.eOk, "REGISTER 注册函数返回 eOk")
r = function_pool_call("fp_echo", "hello_fp")
check(r.status == ResStatus.eOk, "CALL 已注册函数返回 eOk")
check(r.data == "hello_fp" and r.msg == "echo", "CALL 回传消息一致（echo 语义）")
r = function_pool_unregister("fp_echo")
check(r.status == ResStatus.eOk, "UNREGISTER 注销函数返回 eOk")
r = function_pool_call("fp_echo", "x")
check(r.status == ResStatus.eNo, "注销后 CALL 返回 eNo（not-found 统一 eNo）")

# ============ Section 2: 注册边界与重复注册更新语义 ============
r = function_pool_register("", echo_func)
check(r.status == ResStatus.eInvalid, "REGISTER 空函数名返回 eInvalid")
r = function_pool_register("fp_null", None)
check(r.status == ResStatus.eInvalid, "REGISTER null 函数返回 eInvalid")

# 重复注册 = 更新：CALL 走新实现
r = function_pool_register("fp_upd", v1_func)
check(r.status == ResStatus.eOk, "REGISTER v1 返回 eOk")
r = function_pool_call("fp_upd", None)
check(r.msg == "v1", "CALL 走 v1 实现")
r = function_pool_register("fp_upd", v2_func)
check(r.status == ResStatus.eOk, "重复 REGISTER 返回 eOk（更新语义）")
check("update" in r.msg, "重复 REGISTER 消息说明更新语义")
r = function_pool_call("fp_upd", None)
check(r.msg == "v2", "更新后 CALL 走 v2 实现")
r = function_pool_unregister("fp_upd")
check(r.status == ResStatus.eOk, "清理 fp_upd")

# ============ Section 3: 注销边界 ============
r = function_pool_unregister("")
check(r.status == ResStatus.eInvalid, "UNREGISTER 空函数名返回 eInvalid")
r = function_pool_unregister("fp_never_registered")
check(r.status == ResStatus.eNo, "UNREGISTER 未注册名返回 eNo（not-found 统一 eNo）")

# ============ Section 4: 调用边界与 pkg=None 透传 ============
r = function_pool_call("", "x")
check(r.status == ResStatus.eInvalid, "CALL 空函数名返回 eInvalid")
r = function_pool_call("fp_never_registered", "x")
check(r.status == ResStatus.eNo, "CALL 未注册名返回 eNo（not-found 统一 eNo）")

del null_pkg_seen[:]
r = function_pool_register("fp_probe", null_probe_func)
check(r.status == ResStatus.eOk, "REGISTER 探针函数返回 eOk")
r = function_pool_call("fp_probe", None)
check(r.status == ResStatus.eOk, "CALL pkg=None 正常执行")
check(len(null_pkg_seen) == 1, "函数收到 None 入参（pkg 原样透传）")
r = function_pool_unregister("fp_probe")
check(r.status == ResStatus.eOk, "清理 fp_probe")

# ============ Section 5: 内省三接口 ============
r = function_pool_register("fp_plain", alive_func)
check(r.status == ResStatus.eOk, "REGISTER 无类型函数 fp_plain")

# info_names：存活列表包含已注册名（空表陷阱教训：必须有存活条目时调用）
r = function_pool_info_names()
check(r.status == ResStatus.eOk, "INFO_NAMES 返回 eOk")
check(isinstance(r.data, list) and "fp_plain" in r.data,
      "INFO_NAMES 列表包含 fp_plain")

# info_name：结构化返回（FuncInfo 包恒挂）与未注册/空名
r = function_pool_info_name("fp_plain")
check(r.status == ResStatus.eOk, "INFO_NAME 已注册名返回 eOk")
check(r.msg == "ok", "INFO_NAME 命中 msg 恒为 ok")
check(isinstance(r.data, FuncInfo), "INFO_NAME 数据为 FuncInfo 包")
check(r.data is not None and r.data.func_name == "fp_plain",
      "INFO_NAME 字段 func_name 一致")
check(r.data is not None and r.data.msg_name == "" and r.data.description == "",
      "2 参注册 msg_name/description 均为空")
r = function_pool_info_name("fp_never_registered")
check(r.status == ResStatus.eNo, "INFO_NAME 未注册名返回 eNo（与 unregister/call 全接口统一）")
r = function_pool_info_name("")
check(r.status == ResStatus.eInvalid, "INFO_NAME 空函数名返回 eInvalid")

# info_all：结构化 FuncInfoList 相对计数（新注册一个 → 长度 +1）与字段断言
before = pool_count()
check(before is not None, "INFO_ALL FuncInfoList 可解包")
r = function_pool_register("fp_count_probe", alive_func)
check(r.status == ResStatus.eOk, "REGISTER 计数探针函数")
after = pool_count()
check(after == before + 1, "INFO_ALL 计数随注册 +1（共享池相对计数）")
r = function_pool_info_all()
check(r.msg == "ok", "INFO_ALL 成功消息恒为 ok")
all_found = any(isinstance(i, FuncInfo) and i.func_name == "fp_plain"
                and i.msg_name == "" and i.description == "" for i in r.data)
check(all_found, "INFO_ALL 列表包含 fp_plain（msg_name/description 均空）")
r = function_pool_unregister("fp_count_probe")
check(r.status == ResStatus.eOk, "清理 fp_count_probe")

# ============ Section 6: REGISTER 2 参 / 3 参 / 4 参分发映射 ============
r = function_pool_register("fp_two", alive_func)
check(r.status == ResStatus.eOk, "REGISTER 2 参宏（无类型）返回 eOk")
r = function_pool_info_name("fp_two")
check(r.msg == "ok" and isinstance(r.data, FuncInfo)
      and r.data.func_name == "fp_two" and r.data.msg_name == ""
      and r.data.description == "",
      "2 参注册 INFO_NAME 结构化字段（纯名无类型无说明）")

r = function_pool_register("fp_three", echo_func, "String")
check(r.status == ResStatus.eOk, "REGISTER 3 参宏（String 类型）返回 eOk")
r = function_pool_info_name("fp_three")
check(r.msg == "ok" and r.data is not None and r.data.msg_name == "String",
      "3 参注册 INFO_NAME 字段 msg_name == \"String\"")

# 4 参：追加函数说明，结构化包恒挂、说明走 description 字段
r = function_pool_register("fp_desc", echo_func, "String", "fp description")
check(r.status == ResStatus.eOk, "REGISTER 4 参宏（String 类型 + 说明）返回 eOk")
r = function_pool_info_name("fp_desc")
check(r.status == ResStatus.eOk and r.msg == "ok",
      "4 参注册 INFO_NAME 命中 msg 恒为 ok")
check(r.data is not None and r.data.func_name == "fp_desc"
      and r.data.msg_name == "String",
      "4 参注册字段 func_name/msg_name 一致")
check(r.data is not None and r.data.description == "fp description",
      "字段 description 承载函数说明")
r = function_pool_info_name("fp_three")
check(r.data is not None and r.data.description == "",
      "3 参注册（无说明）description 为空（结构化包恒挂）")

# 覆盖语义：2 参同名热替换 → 类型与说明均清除
r = function_pool_register("fp_desc", alive_func)
check(r.status == ResStatus.eOk, "2 参覆盖 4 参注册返回 eOk")
r = function_pool_info_name("fp_desc")
check(r.status == ResStatus.eOk and r.msg == "ok" and r.data is not None
      and r.data.func_name == "fp_desc" and r.data.msg_name == ""
      and r.data.description == "",
      "2 参热替换清除类型与说明（结构化字段为空）")

r = function_pool_unregister("fp_two")
check(r.status == ResStatus.eOk, "清理 fp_two")
r = function_pool_unregister("fp_three")
check(r.status == ResStatus.eOk, "清理 fp_three")
r = function_pool_unregister("fp_desc")
check(r.status == ResStatus.eOk, "清理 fp_desc")

# ============ Section 7: CALL 异常穿透契约 ============
r = function_pool_register("fp_throw", throw_func)
check(r.status == ResStatus.eOk, "REGISTER 抛异常函数返回 eOk")

# CALL 链（request→invoke→callFunction→用户函数）无 try/catch：
# 异常穿透直达调用方（区别于 EventLoop run() 的吞噬设计）——契约行为
caught = False
try:
    function_pool_call("fp_throw", None)
except RuntimeError as e:
    caught = str(e) == "fp_boom"
check(caught, "用户函数异常穿透 CALL 链直达调用方（what 一致）——契约行为")

# 穿透后池存活：正常注册与调用不受影响
r = function_pool_register("fp_alive", alive_func)
check(r.status == ResStatus.eOk, "异常穿透后 REGISTER 仍正常")
r = function_pool_call("fp_alive", None)
check(r.msg == "alive", "异常穿透后 CALL 正常函数执行")
r = function_pool_unregister("fp_throw")
check(r.status == ResStatus.eOk, "清理 fp_throw")
r = function_pool_unregister("fp_alive")
check(r.status == ResStatus.eOk, "清理 fp_alive")

# ============ Section 8: 超长函数名边界 ============
long_name = "F" * 65536
r = function_pool_register(long_name, alive_func)
check(r.status == ResStatus.eOk, "REGISTER 65536 字节函数名返回 eOk")
r = function_pool_call(long_name, None)
check(r.status == ResStatus.eOk, "CALL 超长函数名返回 eOk")
r = function_pool_info_name(long_name)
check(r.status == ResStatus.eOk, "INFO_NAME 超长函数名返回 eOk")
check(r.data is not None and r.data.func_name == long_name,
      "INFO_NAME 完整回显超长函数名")
r = function_pool_unregister(long_name)
check(r.status == ResStatus.eOk, "UNREGISTER 超长函数名返回 eOk")

# ============ 收尾清理（不污染后续测试的共享池）============
r = function_pool_unregister("fp_plain")
check(r.status == ResStatus.eOk, "清理 fp_plain")

print()
if FAIL == 0:
    print(f"TestYomkFunctionPoolLifecycle all check passed. ({PASS} checks)")
    sys.exit(0)
print(
    f"TestYomkFunctionPoolLifecycle FAILED ({FAIL} checks failed / {PASS} passed).")
sys.exit(1)
