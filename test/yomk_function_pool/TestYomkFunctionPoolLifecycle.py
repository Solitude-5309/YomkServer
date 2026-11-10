"""
TestYomkFunctionPoolLifecycle.py —— FunctionPool 模块生命周期与 CRUD 测试

（9 节 57 断言）：
1. CRUD 闭环：REGISTER echo 函数 → CALL 回传消息 → UNREGISTER → CALL eNo
2. 注册边界：空函数名 / None func 均 eInvalid；重复注册为热替换语义（CALL 走新实现，msg 含 hot-swap）
3. 注销边界：空名 eInvalid / 未注册名 eNo
4. 调用边界：空名 eInvalid / 未注册 eNo / pkg=None 原样透传给函数（探针计数）
5. 内省：info_names 存活列表包含注册名（注意空表陷阱：须在有存活条目时调用）/ info_name 命中返回结构化 FuncInfo 包
   （msg 恒 ok）与未注册 eNo、空名 eInvalid / info_all 结构化 FuncInfoList 相对计数与字段断言
6. 无类型/声明类型/类型+说明三种安装形态分发映射：无类型（字段空）vs 声明类型（msg_name 字段）vs
   附函数说明（description 字段）；无声明形态同名覆盖清除类型与说明
7. 异常契约：CALL 链（request→invoke→callFunction→用户函数）无 try/catch，
   用户函数异常穿透直达调用方——测试侧捕获验证 what；随后正常 CALL 验证池存活
8. 超长名：65536 字符函数名全生命周期（REGISTER/CALL/INFO_NAME/UNREGISTER）eOk 且完整回显
9. 载荷类型错配（对齐 C++ 0032fc7）：裸链误用载荷不再抛 TypeError，统一 Failed +
   "pkg is not X, got '<type>'"（register/call/unregister/name 四端点）

语言形态说明：
- Function 三形态：Function(name, func) 无类型 / Function(name, func, "String") 声明类型 /
  Function(name, func, "String", "说明") 附函数说明（经 fn_pool().install() 的
  msg_name 与 description 形参承载）
- 内省清单端点 info_names data 为 list[str]；info_name 与 info_all 命中均为 FuncInfo
  结构化（info_all 为 FuncInfo 列表，总数由列表长度承载）
- 池总数不可得时以 None 作哨兵值承载
- 探针计数用 list.append（GIL 原子）
- 全部经门面 API 测试，同时覆盖 invoke 路由链
"""

import io
import contextlib
import sys

from yomk.msg.server import ResStatus, Response
from yomk.msg.fn_pool import FuncInfo, Function
from yomk.server import server as server_api
from yomk.fn_pool import fn_pool

PASS = 0
FAIL = 0

def raw_fn(endpoint, pkg):
    # 写路径链式终结收敛为 ResStatus（无 msg）；契约消息断言经 server 域
    # url/msg/request 裸链直取 Response（端点行为与链式终结一致）
    return server_api().url("/YomkFunctionPool/" + endpoint).msg(pkg).request()

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
    return Response(ResStatus.eOk, "echo", pkg)

null_pkg_seen = []  # 探针观测容器（list.append GIL 原子）

def null_probe_func(pkg):
    # probe：记录是否收到 None 入参（验证 pkg=None 原样透传）
    if pkg is None:
        null_pkg_seen.append(1)
    return Response(ResStatus.eOk, "probe")

def v1_func(pkg):
    # 版本函数：区分重复注册更新前的实现
    return Response(ResStatus.eOk, "v1")

def v2_func(pkg):
    # 版本函数：区分重复注册更新后的实现
    return Response(ResStatus.eOk, "v2")

def throw_func(pkg):
    # 抛异常函数：验证 CALL 链异常穿透契约
    raise RuntimeError("fp_boom")

def alive_func(pkg):
    # 正常存活函数
    return Response(ResStatus.eOk, "alive")

def pool_count():
    # 从 info_all 结构化列表取当前函数总数（列表长度承载）；失败返回 None（哨兵值）
    r = fn_pool().all()
    if r.status != ResStatus.eOk or not isinstance(r.data, list):
        return None
    return len(r.data)

server_api().async_thread(1).init()
server = server_api().instance()
check(server is not None, "init 返回非空服务器")

# ============ Section 1: CRUD 闭环 ============
r = fn_pool().fn_name("fp_echo").fn(echo_func).install()
check(r == ResStatus.eOk, "REGISTER 注册函数返回 eOk")
r = fn_pool().fn_name("fp_echo").msg("hello_fp").call()
check(r.status == ResStatus.eOk, "CALL 已注册函数返回 eOk")
check(r.data == "hello_fp" and r.msg == "echo", "CALL 回传消息一致（echo 语义）")
r = fn_pool().fn_name("fp_echo").uninstall()
check(r == ResStatus.eOk, "UNREGISTER 注销函数返回 eOk")
r = fn_pool().fn_name("fp_echo").msg("x").call()
check(r.status == ResStatus.eNo, "注销后 CALL 返回 eNo（not-found 统一 eNo）")

# ============ Section 2: 注册边界与重复注册更新语义 ============
r = fn_pool().fn_name("").fn(echo_func).install()
check(r == ResStatus.eInvalid, "REGISTER 空函数名返回 eInvalid")
r = fn_pool().fn_name("fp_null").fn(None).install()
check(r == ResStatus.eInvalid, "REGISTER null 函数返回 eInvalid")

# 重复注册 = 更新：CALL 走新实现
r = fn_pool().fn_name("fp_upd").fn(v1_func).install()
check(r == ResStatus.eOk, "REGISTER v1 返回 eOk")
r = fn_pool().fn_name("fp_upd").call()
check(r.msg == "v1", "CALL 走 v1 实现")
# C-5 已采纳：热替换即覆盖既有注册，诊断级别 WARN（默认门控下可见）
buf = io.StringIO()
with contextlib.redirect_stdout(buf):
    r = raw_fn("register", Function("fp_upd", v2_func, "", ""))
check(r.status == ResStatus.eOk, "重复 REGISTER 返回 eOk（热替换语义）")
check("hot-swap" in r.msg, "重复 REGISTER 消息说明热替换语义（对齐 C++ 0032fc7 文案）")
check("function 'fp_upd' already exists, hot-swap to current function" in buf.getvalue(),
      "重复 REGISTER 记 WARN（C-5）")
r = fn_pool().fn_name("fp_upd").call()
check(r.msg == "v2", "更新后 CALL 走 v2 实现")
r = fn_pool().fn_name("fp_upd").uninstall()
check(r == ResStatus.eOk, "清理 fp_upd")

# ============ Section 3: 注销边界 ============
r = fn_pool().fn_name("").uninstall()
check(r == ResStatus.eInvalid, "UNREGISTER 空函数名返回 eInvalid")
r = fn_pool().fn_name("fp_never_registered").uninstall()
check(r == ResStatus.eNo, "UNREGISTER 未注册名返回 eNo（not-found 统一 eNo）")

# ============ Section 4: 调用边界与 pkg=None 透传 ============
r = fn_pool().fn_name("").msg("x").call()
check(r.status == ResStatus.eInvalid, "CALL 空函数名返回 eInvalid")
r = fn_pool().fn_name("fp_never_registered").msg("x").call()
check(r.status == ResStatus.eNo, "CALL 未注册名返回 eNo（not-found 统一 eNo）")

del null_pkg_seen[:]
r = fn_pool().fn_name("fp_probe").fn(null_probe_func).install()
check(r == ResStatus.eOk, "REGISTER 探针函数返回 eOk")
r = fn_pool().fn_name("fp_probe").call()
check(r.status == ResStatus.eOk, "CALL pkg=None 正常执行")
check(len(null_pkg_seen) == 1, "函数收到 None 入参（pkg 原样透传）")
r = fn_pool().fn_name("fp_probe").uninstall()
check(r == ResStatus.eOk, "清理 fp_probe")

# ============ Section 5: 内省三接口 ============
r = fn_pool().fn_name("fp_plain").fn(alive_func).install()
check(r == ResStatus.eOk, "REGISTER 无类型函数 fp_plain")

# info_names：存活列表包含已注册名（空表陷阱：必须有存活条目时调用）
r = fn_pool().list()
check(r.status == ResStatus.eOk, "INFO_NAMES 返回 eOk")
check(isinstance(r.data, list) and "fp_plain" in r.data,
      "INFO_NAMES 列表包含 fp_plain")

# info_name：结构化返回（FuncInfo 包恒挂）与未注册/空名
r = fn_pool().fn_name("fp_plain").get_info()
check(r.status == ResStatus.eOk, "INFO_NAME 已注册名返回 eOk")
check(r.msg == "ok", "INFO_NAME 命中 msg 恒为 ok")
check(isinstance(r.data, FuncInfo), "INFO_NAME 数据为 FuncInfo 包")
check(r.data is not None and r.data.func_name == "fp_plain",
      "INFO_NAME 字段 func_name 一致")
check(r.data is not None and r.data.msg_name == "" and r.data.description == "",
      "无声明安装 msg_name/description 均为空")
r = fn_pool().fn_name("fp_never_registered").get_info()
check(r.status == ResStatus.eNo, "INFO_NAME 未注册名返回 eNo（与 unregister/call 全接口统一）")
r = fn_pool().fn_name("").get_info()
check(r.status == ResStatus.eInvalid, "INFO_NAME 空函数名返回 eInvalid")

# info_all：结构化 FuncInfoList 相对计数（新注册一个 → 长度 +1）与字段断言
before = pool_count()
check(before is not None, "INFO_ALL FuncInfoList 可解包")
r = fn_pool().fn_name("fp_count_probe").fn(alive_func).install()
check(r == ResStatus.eOk, "REGISTER 计数探针函数")
after = pool_count()
check(after == before + 1, "INFO_ALL 计数随注册 +1（共享池相对计数）")
r = fn_pool().all()
check(r.msg == "ok", "INFO_ALL 成功消息恒为 ok")
all_found = any(isinstance(i, FuncInfo) and i.func_name == "fp_plain"
                and i.msg_name == "" and i.description == "" for i in r.data)
check(all_found, "INFO_ALL 列表包含 fp_plain（msg_name/description 均空）")
r = fn_pool().fn_name("fp_count_probe").uninstall()
check(r == ResStatus.eOk, "清理 fp_count_probe")

# ============ Section 6: 三种安装形态分发映射 ============
r = fn_pool().fn_name("fp_two").fn(alive_func).install()
check(r == ResStatus.eOk, "安装无类型声明的函数返回 eOk")
r = fn_pool().fn_name("fp_two").get_info()
check(r.msg == "ok" and isinstance(r.data, FuncInfo)
      and r.data.func_name == "fp_two" and r.data.msg_name == ""
      and r.data.description == "",
      "无声明安装 INFO_NAME 结构化字段（纯名无类型无说明）")

r = fn_pool().fn_name("fp_three").fn(echo_func).msg_name("String").install()
check(r == ResStatus.eOk, "声明 String 类型安装返回 eOk")
r = fn_pool().fn_name("fp_three").get_info()
check(r.msg == "ok" and r.data is not None and r.data.msg_name == "String",
      "声明类型安装 INFO_NAME 字段 msg_name == \"String\"")

# 类型+说明：追加函数说明，结构化包恒挂、说明走 description 字段
r = fn_pool().fn_name("fp_desc").fn(echo_func).msg_name("String").description("fp description").install()
check(r == ResStatus.eOk, "声明类型+说明安装返回 eOk")
r = fn_pool().fn_name("fp_desc").get_info()
check(r.status == ResStatus.eOk and r.msg == "ok",
      "类型+说明安装 INFO_NAME 命中 msg 恒为 ok")
check(r.data is not None and r.data.func_name == "fp_desc"
      and r.data.msg_name == "String",
      "类型+说明安装字段 func_name/msg_name 一致")
check(r.data is not None and r.data.description == "fp description",
      "字段 description 承载函数说明")
r = fn_pool().fn_name("fp_three").get_info()
check(r.data is not None and r.data.description == "",
      "仅声明类型（无说明）description 为空（结构化包恒挂）")

# 覆盖语义：无声明形态同名覆盖 → 类型与说明均清除
r = fn_pool().fn_name("fp_desc").fn(alive_func).install()
check(r == ResStatus.eOk, "以无声明形态覆盖带声明安装返回 eOk")
r = fn_pool().fn_name("fp_desc").get_info()
check(r.status == ResStatus.eOk and r.msg == "ok" and r.data is not None
      and r.data.func_name == "fp_desc" and r.data.msg_name == ""
      and r.data.description == "",
      "无声明形态覆盖清除类型与说明（结构化字段为空）")

r = fn_pool().fn_name("fp_two").uninstall()
check(r == ResStatus.eOk, "清理 fp_two")
r = fn_pool().fn_name("fp_three").uninstall()
check(r == ResStatus.eOk, "清理 fp_three")
r = fn_pool().fn_name("fp_desc").uninstall()
check(r == ResStatus.eOk, "清理 fp_desc")

# ============ Section 7: CALL 异常穿透契约 ============
r = fn_pool().fn_name("fp_throw").fn(throw_func).install()
check(r == ResStatus.eOk, "REGISTER 抛异常函数返回 eOk")

# CALL 链（request→invoke→callFunction→用户函数）无 try/catch：
# 异常穿透直达调用方（区别于 EventLoop run() 的吞噬设计）——契约行为
caught = False
try:
    fn_pool().fn_name("fp_throw").call()
except RuntimeError as e:
    caught = str(e) == "fp_boom"
check(caught, "用户函数异常穿透 CALL 链直达调用方（what 一致）——契约行为")

# 穿透后池存活：正常注册与调用不受影响
r = fn_pool().fn_name("fp_alive").fn(alive_func).install()
check(r == ResStatus.eOk, "异常穿透后 REGISTER 仍正常")
r = fn_pool().fn_name("fp_alive").call()
check(r.msg == "alive", "异常穿透后 CALL 正常函数执行")
r = fn_pool().fn_name("fp_throw").uninstall()
check(r == ResStatus.eOk, "清理 fp_throw")
r = fn_pool().fn_name("fp_alive").uninstall()
check(r == ResStatus.eOk, "清理 fp_alive")

# ============ Section 8: 超长函数名边界 ============
long_name = "F" * 65536
r = fn_pool().fn_name(long_name).fn(alive_func).install()
check(r == ResStatus.eOk, "REGISTER 65536 字节函数名返回 eOk")
r = fn_pool().fn_name(long_name).call()
check(r.status == ResStatus.eOk, "CALL 超长函数名返回 eOk")
r = fn_pool().fn_name(long_name).get_info()
check(r.status == ResStatus.eOk, "INFO_NAME 超长函数名返回 eOk")
check(r.data is not None and r.data.func_name == long_name,
      "INFO_NAME 完整回显超长函数名")
r = fn_pool().fn_name(long_name).uninstall()
check(r == ResStatus.eOk, "UNREGISTER 超长函数名返回 eOk")

# ============ Section 9: 载荷类型错配契约（裸链误用 → Failed Response，不再抛 TypeError） ============
r = raw_fn("register", "not_a_Function")
check(r.status == ResStatus.eNo and r.msg == "pkg is not Function, got 'str'",
      "REGISTER 非 Function 载荷返回 eNo + pkg is not Function, got 'str'")
r = raw_fn("call", 123)
check(r.status == ResStatus.eNo and r.msg == "pkg is not CallFunction, got 'int'",
      "CALL 非 CallFunction 载荷返回 eNo + pkg is not CallFunction, got 'int'")
r = raw_fn("unregister", 123)
check(r.status == ResStatus.eNo and r.msg == "pkg is not String, got 'int'",
      "UNREGISTER 非 String 载荷返回 eNo + pkg is not String, got 'int'")
r = raw_fn("name", ["x"])
check(r.status == ResStatus.eNo and r.msg == "pkg is not String, got 'list'",
      "INFO_NAME 非 String 载荷返回 eNo + pkg is not String, got 'list'")

# ============ 收尾清理（不污染后续测试的共享池）============
r = fn_pool().fn_name("fp_plain").uninstall()
check(r == ResStatus.eOk, "清理 fp_plain")

print()
if FAIL == 0:
    print(f"TestYomkFunctionPoolLifecycle all check passed. ({PASS} checks)")
    sys.exit(0)
print(
    f"TestYomkFunctionPoolLifecycle FAILED ({FAIL} checks failed / {PASS} passed).")
sys.exit(1)
