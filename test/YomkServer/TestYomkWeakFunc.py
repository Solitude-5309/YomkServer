"""
TestYomkWeakFunc.py —— YomkService.weak_func 弱绑定生命周期语义测试（正式全面测试）

覆盖内容（对齐 main TestYomkWeakFunc.cpp 4 节）：
1. 存活路径：四类丢弃形态回调均真实执行（"none"/"response"/"check"/默认返回值），
   install_func 自动弱绑定经 invoke 命中执行
2. 销毁丢弃（引用计数路径）：服务实例失去引用（del + gc.collect()）后回调丢弃——
   "none" 不执行且返回 None（main Ret{} 默认值的 None 形态映射）、"response" 返回
   eNo+契约消息、"check" 默认放行 eAccept
3. 删除即停（注销标志路径）：持引用不释放经框架 del_service 置位注销标志，回调与
   自动弱绑定 invoke 立即丢弃（双层判活标志层独立于引用计数生效）
4. 丢弃不执行实证：所有丢弃用例均以副作用计数未变化断言，而非仅看返回值

语言机制说明（非功能缺口）：
- main 第 4 节"构造期告警分支"剔除：python 无 enable_shared_from_this 机制，
  weakref 在 __init__ 内即生效，不存在"构造期绑定永久失效"场景；"绑定后对象销毁"
  契约由第 2 节引用计数路径等价承载
- main 按静态返回类型的四分支丢弃契约（void/YomkResponse/ECheckStatus/Ret{}）映射为
  weak_func 的 discard 声明参数（"none"/"response"/"check"），python 动态类型下由
  调用方声明丢弃形态

说明：不依赖单例与服务器生命周期，YomkServer.create 直接持有，服务由测试本地变量
控制销毁时机（避开 init 单例约束，单进程即可覆盖全部场景）

风格：纯 main() + 失败计数，返回非 0 表示存在失败用例（零第三方依赖）
"""

import gc
import sys

from YomkServerPy import (
    CheckStatus,
    Context,
    ResStatus,
    YomkResponse,
    YomkServer,
    YomkService,
)

g_failed = 0

# 弱绑定丢弃契约消息片段（四种丢弃形态共用，对齐 main DISCARD_MSG）
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
    """弱绑定演示服务：供存活/销毁/注销场景绑定回调（对齐 main BindSrv）"""

    def __init__(self, server):
        super().__init__(server)
        self.set_name("/BindSrv")
        self.macro_count = 0

    def init(self):
        # install_func 自动弱绑定：注销后经 invoke 返回丢弃契约消息
        self.install_func("/installed", self.work_resp)
        return 0

    def work_resp(self, pkg):
        return YomkResponse(ResStatus.eOk, "alive")

    def macro_work(self, pkg):
        self.macro_count += 1
        return YomkResponse(ResStatus.eOk, "macro")

    def make_macro_cb(self):
        # weak_func 显式成员绑定（外流注册主路径，对齐 main YomkBindWeakSelf 宏）
        return self.weak_func(self.macro_work)


# 1. 存活路径：四类丢弃形态回调与自动弱绑定回调均真实执行
def test_alive_callbacks(server):
    counts = {"void": 0, "resp": 0, "check": 0, "int": 0}

    srv = BindSrv(server)
    cb_void = srv.weak_func(lambda: counts.__setitem__("void", counts["void"] + 1),
                            discard="none")
    cb_resp = srv.weak_func(
        lambda pkg: (counts.__setitem__("resp", counts["resp"] + 1),
                     YomkResponse(ResStatus.eOk, "alive"))[1])
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
                     YomkResponse(ResStatus.eOk, "alive"))[1])
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
          "销毁：默认丢弃形态回调丢弃，返回 None（main Ret{} 默认值形态映射）")
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
                     YomkResponse(ResStatus.eOk, "alive"))[1])

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


def main():
    server = YomkServer.create(1)

    test_alive_callbacks(server)
    test_destroy_discard(server)
    test_mark_deleted_discard(server)

    if g_failed > 0:
        print(f"TestYomkWeakFunc failed, count: {g_failed}")
        return 1
    print("TestYomkWeakFunc all check passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
