"""
TestYomkWeakFunc.py —— YomkService.weak_func 弱绑定生命周期语义测试

覆盖内容（5 节）：
1. 存活路径：四类丢弃形态回调均真实执行（"none"/"response"/"check"/默认返回值），
   install_func 自动弱绑定经 invoke 命中执行
2. 销毁丢弃（引用计数路径）：服务实例失去引用（del + gc.collect()）后回调丢弃——
   "none" 不执行且返回 None（默认返回值形态映射为 None）、"response" 返回
   eNo+契约消息、"check" 默认放行 eAccept
3. 删除即停（注销标志路径）：持引用不释放经框架 del_service 置位注销标志，回调与
   自动弱绑定 invoke 立即丢弃（双层判活标志层独立于引用计数生效）
4. 槽位适配（U-08 落点）：discard 未声明时由消费槽位按 API 形态填充缺省
   （checker→"check"、monitor→"none"、fn_pool/event_loop→"response"），未过槽位
   直调兜底 "response"；显式声明与槽位不符注册期立即诊断；裸回调无元数据不校验；
   槽位填充后服务死亡：checker 丢弃返回 eAccept（修复前误返 Response）
5. 丢弃不执行实证：所有丢弃用例均以副作用计数未变化断言，而非仅看返回值

语言机制说明：
- 不存在构造期告警分支：python 无 shared_from_this 类机制，
  weakref 在 __init__ 内即生效，不存在"构造期绑定永久失效"场景；"绑定后对象销毁"
  契约由第 2 节引用计数路径承载
- 丢弃形态不设按静态返回类型的分支（python 动态类型）：缺省由消费槽位按 API 形态
  填充（第 4 节），调用方也可经 weak_func 的 discard 参数显式声明（误配注册期诊断）

说明：不依赖单例与服务器生命周期，YomkServer.create 直接持有，服务由测试本地变量
控制销毁时机（避开 init 单例约束，单进程即可覆盖全部场景）

风格：纯 main() + 失败计数，返回非 0 表示存在失败用例（零第三方依赖）
"""

import gc
import io
import contextlib
import sys

from yomk.msg.server import ResStatus, Response
from yomk.msg.ctx import CheckStatus, Context
from yomk.server import YomkServer, YomkService
from yomk.ctx.builder import ContextBuilder  # 白盒：直用内部 builder 验证槽位适配
from yomk.fn_pool.builder import FnPoolBuilder
from yomk.event_loop.builder import EventLoopBuilder

g_failed = 0

# 弱绑定丢弃契约消息片段（四种丢弃形态共用）
DISCARD_MSG = "deleted or unregistered"


def check(cond, msg):
    global g_failed
    line = sys._getframe(1).f_lineno
    if cond:
        print(f"[ OK ] [line {line}] {msg}")
    else:
        print(f"[FAIL] [line {line}] {msg}")
        g_failed += 1


class BindSrv(YomkService):
    """弱绑定演示服务：供存活/销毁/注销场景绑定回调"""

    def __init__(self, server):
        super().__init__(server)
        self.set_name("/BindSrv")
        self.macro_count = 0

    def init(self):
        # install_func 自动弱绑定：注销后经 invoke 返回丢弃契约消息
        self.install_func("/installed", self.work_resp)
        return 0

    def work_resp(self, pkg):
        return Response(ResStatus.eOk, "alive")

    def macro_work(self, pkg):
        self.macro_count += 1
        return Response(ResStatus.eOk, "macro")

    def make_macro_cb(self):
        # weak_func 显式成员绑定（外流注册主路径）
        return self.weak_func(self.macro_work)


# 1. 存活路径：四类丢弃形态回调与自动弱绑定回调均真实执行
def test_alive_callbacks(server):
    counts = {"void": 0, "resp": 0, "check": 0, "int": 0}

    srv = BindSrv(server)
    cb_void = srv.weak_func(lambda: counts.__setitem__("void", counts["void"] + 1),
                            discard="none")
    cb_resp = srv.weak_func(
        lambda pkg: (counts.__setitem__("resp", counts["resp"] + 1),
                     Response(ResStatus.eOk, "alive"))[1])
    cb_check = srv.weak_func(
        lambda ctx: (counts.__setitem__("check", counts["check"] + 1),
                     CheckStatus.eReject)[1],
        discard="check")
    cb_int = srv.weak_func(
        lambda pkg: (counts.__setitem__("int", counts["int"] + 1), 42)[1],
        discard="none")
    cb_macro = srv.make_macro_cb()

    cb_void()
    ctx = Context("key", None)
    resp = cb_resp(None)
    status = cb_check(ctx)
    int_ret = cb_int(None)
    macro_resp = cb_macro(None)

    check(counts["void"] == 1, "存活：void 形态回调真实执行")
    check(counts["resp"] == 1 and resp.status == ResStatus.eOk and resp.msg == "alive",
          "存活：response 形态回调正常执行")
    check(counts["check"] == 1 and status == CheckStatus.eReject,
          "存活：check 形态回调返回真实值")
    check(counts["int"] == 1 and int_ret == 42, "存活：默认丢弃形态回调返回真实值")
    check(srv.macro_count == 1 and macro_resp.status == ResStatus.eOk
          and macro_resp.msg == "macro",
          "存活：weak_func 成员绑定回调真实执行")


# 2. 销毁丢弃（引用计数路径）：服务实例失去引用后四类回调全部丢弃
def test_destroy_discard(server):
    counts = {"void": 0, "resp": 0, "check": 0, "int": 0}

    srv = BindSrv(server)
    cb_void = srv.weak_func(lambda: counts.__setitem__("void", counts["void"] + 1),
                            discard="none")
    cb_resp = srv.weak_func(
        lambda pkg: (counts.__setitem__("resp", counts["resp"] + 1),
                     Response(ResStatus.eOk, "alive"))[1])
    cb_check = srv.weak_func(
        lambda ctx: (counts.__setitem__("check", counts["check"] + 1),
                     CheckStatus.eReject)[1],
        discard="check")
    cb_int = srv.weak_func(
        lambda pkg: (counts.__setitem__("int", counts["int"] + 1), 42)[1],
        discard="none")
    cb_macro = srv.make_macro_cb()
    del srv  # 本地唯一强引用释放：引用计数归零，弱绑定全部失效

    gc.collect()
    cb_void()
    ctx = Context("key", None)
    resp = cb_resp(None)
    status = cb_check(ctx)
    int_ret = cb_int(None)
    macro_resp = cb_macro(None)

    check(counts["void"] == 0, "销毁：void 形态回调丢弃，副作用计数未变化")
    check(counts["resp"] == 0 and resp.status == ResStatus.eNo
          and DISCARD_MSG in resp.msg,
          "销毁：response 形态回调丢弃，返回 eNo + 契约消息")
    check(counts["check"] == 0 and status == CheckStatus.eAccept,
          "销毁：check 形态回调丢弃，默认放行 eAccept")
    check(counts["int"] == 0 and int_ret is None,
          "销毁：默认丢弃形态回调丢弃，int 形态调用返回 None（丢弃调用无返回值，默认 None）")
    check(macro_resp.status == ResStatus.eNo and DISCARD_MSG in macro_resp.msg,
          "销毁：weak_func 绑定回调丢弃")


# 3. 删除即停（注销标志路径）：持引用不释放经框架删除置位注销标志，
#    回调与自动弱绑定 invoke 立即丢弃
def test_mark_deleted_discard(server):
    counts = {"void": 0, "resp": 0}

    srv = BindSrv(server)
    check(server.add_service(srv) == 0, "注册成功（框架 init 安装 /installed）")

    # 注销前：自动弱绑定 invoke 命中执行
    alive_resp = srv.invoke("/installed", None)
    check(alive_resp.status == ResStatus.eOk and alive_resp.msg == "alive",
          "注销前：install_func 自动弱绑定 invoke 命中执行")

    cb_void = srv.weak_func(lambda: counts.__setitem__("void", counts["void"] + 1),
                            discard="none")
    cb_resp = srv.weak_func(
        lambda pkg: (counts.__setitem__("resp", counts["resp"] + 1),
                     Response(ResStatus.eOk, "alive"))[1])

    # 测试持 srv 引用不释放，经框架删除置位注销标志（标志层独立于引用计数）
    check(server.del_service("/BindSrv") == 0, "框架删除成功")

    cb_void()
    resp = cb_resp(None)
    invoke_resp = srv.invoke("/installed", None)

    check(counts["void"] == 0, "删除即停：void 形态回调立即丢弃，副作用计数未变化")
    check(counts["resp"] == 0 and resp.status == ResStatus.eNo
          and DISCARD_MSG in resp.msg,
          "删除即停：response 形态回调立即丢弃（注销标志层独立生效）")
    check(invoke_resp.status == ResStatus.eNo and DISCARD_MSG in invoke_resp.msg,
          "删除即停：自动弱绑定 invoke 返回丢弃契约消息")
    check(srv.deleted() is True, "删除即停：deleted() 查询返回 True")


# 4. 槽位适配（U-08 落点）：discard 未声明由消费槽位按 API 形态填充缺省；
#    显式误配注册期立即诊断；裸回调不校验；填充后丢弃返回类型正确
def test_slot_fill(server):
    srv = BindSrv(server)

    # 未声明 discard：各槽位按 API 形态填充缺省
    cb_check = srv.weak_func(lambda c: CheckStatus.eReject)
    ContextBuilder().key("k").checker_fn(cb_check)
    check(cb_check.__yomk_discard__ == "check", "槽位适配：checker 槽未声明填充 'check'")

    cb_monitor = srv.weak_func(lambda c: None)
    ContextBuilder().key("k").monitor_fn(cb_monitor)
    check(cb_monitor.__yomk_discard__ == "none", "槽位适配：monitor 槽未声明填充 'none'")

    cb_fn = srv.weak_func(lambda p: Response(ResStatus.eOk, "ok"))
    FnPoolBuilder().fn(cb_fn)
    check(cb_fn.__yomk_discard__ == "response", "槽位适配：fn_pool 槽未声明填充 'response'")

    cb_el = srv.weak_func(lambda p: Response(ResStatus.eOk, "ok"))
    EventLoopBuilder().handle_fn(cb_el)
    check(cb_el.__yomk_discard__ == "response", "槽位适配：event_loop 槽未声明填充 'response'")

    # 显式声明与槽位一致：注册期无诊断
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        ContextBuilder().key("k").checker_fn(
            srv.weak_func(lambda c: CheckStatus.eAccept, discard="check"))
    check("mismatches" not in buf.getvalue(), "槽位适配：显式 'check' 与 checker 槽一致无诊断")

    # 显式声明与槽位不符：注册期立即诊断（早于回调真正被调用），携带具体值
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        ContextBuilder().key("k").checker_fn(
            srv.weak_func(lambda c: None, discard="response"))
    check("weak_func discard 'response' mismatches checker slot (expects 'check')"
          in buf.getvalue(), "槽位适配：checker 槽显式误配注册期诊断并携带具体值")

    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        ContextBuilder().key("k").monitor_fn(
            srv.weak_func(lambda c: None, discard="response"))
    check("mismatches monitor slot (expects 'none')" in buf.getvalue(),
          "槽位适配：monitor 槽显式误配注册期诊断")

    # 裸回调（非 weak_func 产物，无元数据）：不校验原样透传
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        ContextBuilder().key("k").checker_fn(lambda c: CheckStatus.eReject)
    check("mismatches" not in buf.getvalue(), "槽位适配：裸回调无元数据不校验")

    # 槽位填充后的丢弃行为：服务死亡 checker 丢弃返回 eAccept（修复前误返 Response）
    cb2 = srv.weak_func(lambda c: CheckStatus.eReject)
    ContextBuilder().key("k").checker_fn(cb2)
    del cb_check, cb_monitor, cb_fn, cb_el
    del srv  # 释放强引用走引用计数丢弃路径
    gc.collect()
    status = cb2(None)
    check(status == CheckStatus.eAccept,
          "槽位适配：填充后服务死亡 checker 丢弃返回 eAccept（修复前误返 Response）")

    # 兼容性：未过槽位直调的未声明回调兜底 "response"（旧默认不变）
    srv2 = BindSrv(server)
    cb_direct = srv2.weak_func(lambda p: Response(ResStatus.eOk, "alive"))
    del srv2
    gc.collect()
    resp = cb_direct(None)
    check(resp.status == ResStatus.eNo and DISCARD_MSG in resp.msg,
          "槽位适配：未过槽位直调兜底 'response'（旧默认兼容）")


def main():
    server = YomkServer.create(1)

    test_alive_callbacks(server)
    test_destroy_discard(server)
    test_mark_deleted_discard(server)
    test_slot_fill(server)

    if g_failed > 0:
        print(f"TestYomkWeakFunc failed, count: {g_failed}")
        return 1
    print("TestYomkWeakFunc all check passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
