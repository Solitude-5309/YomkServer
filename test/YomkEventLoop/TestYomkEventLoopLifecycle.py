"""
TestYomkEventLoopLifecycle.py - EventLoop 三段语义（start 重启续跑 / stop 保留队列 / destroy 停后清空）白盒回归测试

对齐 main Test/YomkEventLoop/TestYomkEventLoopLifecycle.cpp（781 行 12 节），节序同 main 源码。

覆盖内容（12 个 Section）：
1+2. stop 保留 + start 续跑：停止仅退出工作线程，未执行事件保留（pending 不减）；重启按 FIFO 续跑
3.   destroy 清空：先停止再清空——排队事件永不执行、重启无任何执行（不可续跑）
4.   start 幂等：运行中再次 start 不新建线程（两事件同一工作线程执行）
5.   postWait 同步语义：返回即事件已执行完（响应已填充、eventId 已赋）；null 事件返回 1
6.   worker 内 postWait 死锁防护：防护路径直接执行内层事件（inner -> outer 序，不自等待）
7.   run() 异常吞噬与存活：handler 抛异常被隔离，循环存活、postWait 不挂
8.   未运行幂等 / 停止态拒收（rc=2）/ 默认处理函数 / infoLine 分支（[MsgX]、空 tag "-"、tagCount=0 与超队列）
9.   API 层路径（门面 event_loop_*）：停止保留 / 重启续跑 / 销毁移除 / POST_WAIT 同步回传 / 不存在名 eNo /
     停止态投递 eNo / INFO_LOOP 数字路径与越界回退 / INFO_LOOPS / INFO_ALL / 空名与超长名边界
11.  delService("/YomkEventLoop") 残留 loop 收尾与重注册（deinit override 承载析构兜底链；
     new_service 无参重注册后全链路恢复）
10.  postWait×destroy 等待者释放：销毁清空队列时触发被丢弃事件的等待回调（3s 看门狗）
12.  析构兜底释放等待者：不调 destroy 直接失去引用——del + gc.collect() 触发 __del__ 兜底，共享释放契约
     （被丢弃事件的等待回调被触发，stop 哨兵消除竞态，weakref 归零断言析构完成；映射差异：main 等待者为
     阻塞 postWait 线程且裸指针观测不阻止析构——python 方法栈帧持 self 使"阻塞等待者存在期间析构"不可达，
     以排队事件 waitCallback 标记回调等价承载同一契约）

说明：白盒段直连 YomkServerPy.EventLoop 类；API 段 YomkApi.init(1) 单例拉起内置 /YomkEventLoop，
      末尾 YomkApi.shutdown() 收尾（进程级幂等，shutdown 后不可再初始化）。
      停止/销毁经辅助线程调用（内部 join 等待在途事件收尾）；主线程轮询 info_line 至 running:off
      确认 m_running 已置位后才释放阻塞事件，杜绝工作线程抢跑后续排队事件的竞态（确定性断言）。
      阻塞门用 threading.Event 实现（main 原子自旋门的语言映射，1ms 轮询粒度由 wait/Event.wait 内化）。

风格：脚本式 main + 失败计数，退出码 0/1（零第三方依赖）
"""

import gc
import sys
import threading
import time
import weakref

import YomkServerPy as ysp
from YomkServerPy import Event, EventLoop

FAILED = 0

# ---- 文件级观测变量（main TSan-clean 风格映射：互斥量保护 + 全部访问持锁）----
_ORDER_LOCK = threading.Lock()
_ORDER = []  # 事件执行顺序（tag）

_IDS_LOCK = threading.Lock()
_IDS = []  # 工作线程 id 记录（Section 4 幂等性观测）


def record_order(tag):
    with _ORDER_LOCK:
        _ORDER.append(tag)


def order_snapshot():
    with _ORDER_LOCK:
        return list(_ORDER)


def reset_order():
    with _ORDER_LOCK:
        del _ORDER[:]


def record_thread_id():
    with _IDS_LOCK:
        _IDS.append(threading.get_ident())


def ids_snapshot():
    with _IDS_LOCK:
        return list(_IDS)


def reset_ids():
    with _IDS_LOCK:
        del _IDS[:]


# ---- 文件级阻塞门：占住工作线程制造确定性排队（main 原子自旋门 -> threading.Event）----
class Gate:
    def __init__(self):
        self.started = threading.Event()
        self.release = threading.Event()

    def reset(self):
        self.started.clear()
        self.release.clear()


GATE = Gate()


def gate_handler():
    def _handler(pkg):
        GATE.started.set()
        GATE.release.wait()
        return ysp.YomkResponse(ysp.ResStatus.eOk, "gate done")
    return _handler


def wait_gate_started(timeout_s):
    return GATE.started.wait(timeout_s)


def open_gate():
    GATE.release.set()


def wait_until(pred, timeout_s):
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        if pred():
            return True
        time.sleep(0.001)
    return pred()


def counting_handler(tag):
    def _handler(pkg):
        record_order(tag)
        return ysp.YomkResponse(ysp.ResStatus.eOk, "counted: " + tag)
    return _handler


def check(cond, msg):
    global FAILED
    if not cond:
        print("[FAIL]", msg)
        FAILED += 1
    else:
        print("[ OK ]", msg)


def line_of(loop, name, tag_count=3):
    line = loop.info_line(name, tag_count)
    return line if line else ""


def line_of_api(name, tag_count=None):
    import YomkApi
    if tag_count is not None:
        r = YomkApi.event_loop_info_loop(name, tag_count)
    else:
        r = YomkApi.event_loop_info_loop(name)
    return r.msg if r.status == ysp.ResStatus.eOk else ""


# ============ Section 1+2: stop 保留 + start 续跑（白盒）============
def section_1_2():
    reset_order()
    GATE.reset()
    loop = EventLoop()

    check(loop.start() == 0, "start 返回 0")

    check(loop.post(Event("wb", None, gate_handler(), "e1"))
          == 0, "post 阻塞事件 e1 返回 0")
    check(wait_gate_started(2.0), "e1 已进入执行（阻塞占住工作线程）")

    check(loop.post(Event("wb", None, counting_handler("e2"), "e2"))
          == 0, "post 计数事件 e2 返回 0")
    check(loop.post(Event("wb", None, counting_handler("e3"), "e3"))
          == 0, "post 计数事件 e3 返回 0")

    # 辅助线程停止：stop 内部 join 等待 e1 收尾，须与主线程释放阻塞门配合
    stopper = threading.Thread(target=loop.stop)
    stopper.start()
    # 轮询确认 m_running 已置 false：e1 收尾后工作线程必从 while 条件退出，不会抢跑 e2/e3
    stopped = wait_until(lambda: "running:off" in line_of(loop, "wb"), 2.0)
    check(stopped, "停止过程中 info_line 观察到 running:off（m_running 已置位）")

    open_gate()
    stopper.join()  # stop 返回：工作线程已退出

    check(order_snapshot() == [], "停止后排队事件 e2/e3 未被执行")
    line = line_of(loop, "wb")
    check("running:off" in line, "停止后 running:off")
    check("pending:2" in line, "停止后 pending:2（未执行事件保留在队列）")

    # 续跑：对已停止未销毁的循环再次 start，积压按原 FIFO 顺序执行
    check(loop.start() == 0, "再次 start 返回 0（重启）")
    drained = wait_until(
        lambda: (lambda o: len(o) == 2 and o[0] == "e2" and o[1] == "e3")(order_snapshot()), 2.0)
    check(drained, "重启后积压按 FIFO 顺序续跑完成（e2 -> e3，事件不丢失）")
    idle = wait_until(lambda: "pending:0" in line_of(loop, "wb"), 2.0)
    check(idle, "续跑完成后 pending:0")

    loop.destroy()  # 显式销毁收尾；__del__ 将再次进入 destroy（幂等，无害）


# ============ Section 3: destroy 清空（白盒）============
def section_3():
    reset_order()
    GATE.reset()
    loop = EventLoop()

    check(loop.start() == 0, "start 返回 0")

    check(loop.post(Event("wb", None, gate_handler(), "b1"))
          == 0, "post 阻塞事件 b1 返回 0")
    check(wait_gate_started(2.0), "阻塞事件 b1 已进入执行")

    check(loop.post(Event("wb", None, counting_handler("c1"), "c1"))
          == 0, "post 排队事件 c1 返回 0")
    check(loop.post(Event("wb", None, counting_handler("c2"), "c2"))
          == 0, "post 排队事件 c2 返回 0")

    # 辅助线程销毁：destroy = 先 stop（join 等在途收尾）再清空队列
    destroyer = threading.Thread(target=loop.destroy)
    destroyer.start()
    stopped = wait_until(lambda: "running:off" in line_of(loop, "wb"), 2.0)
    check(stopped, "销毁过程中 info_line 观察到 running:off（先停止）")

    open_gate()
    destroyer.join()

    check(order_snapshot() == [], "销毁后排队事件 c1/c2 永不执行")
    line = line_of(loop, "wb")
    check("running:off" in line, "销毁后 running:off")
    check("pending:0" in line, "销毁后 pending:0（队列已清空）")

    # 不可续跑：destroy 后再 start，无任何事件执行
    check(loop.start() == 0, "destroy 后再 start 返回 0（空转循环）")
    time.sleep(0.3)
    check(order_snapshot() == [], "destroy 后重启无任何事件执行（不可续跑）")

    loop.destroy()


# ============ Section 4: start 幂等（白盒）============
def section_4():
    reset_ids()
    loop = EventLoop()
    check(loop.start() == 0, "start 返回 0")

    def record_id(pkg):
        record_thread_id()
        return ysp.YomkResponse(ysp.ResStatus.eOk, "id recorded")

    check(loop.post(Event("wb", None, record_id, "i1")) == 0, "post 事件 i1 返回 0")
    check(wait_until(lambda: len(ids_snapshot()) >= 1, 2.0), "事件 i1 已执行")

    check(loop.start() == 0, "运行中再次 start 返回 0（幂等早退，不新建线程）")

    check(loop.post(Event("wb", None, record_id, "i2")) == 0, "post 事件 i2 返回 0")
    check(wait_until(lambda: len(ids_snapshot()) >= 2, 2.0), "事件 i2 已执行")

    ids = ids_snapshot()
    check(len(ids) == 2 and ids[0] == ids[1], "幂等 start 不换线程（两事件同一工作线程执行）")

    loop.destroy()


# ============ Section 5: postWait 同步语义（白盒）============
def section_5():
    reset_order()
    loop = EventLoop()
    check(loop.start() == 0, "start 返回 0")

    ev = Event("wb", None, counting_handler("pw1"), "pw1")
    check(loop.post_wait(ev) == 0, "postWait 返回 0")

    order = order_snapshot()
    check(len(order) == 1 and order[-1] == "pw1", "postWait 返回即事件已执行（同步性）")
    check(ev.m_response is not None and ev.m_response.status ==
          ysp.ResStatus.eOk, "postWait 返回时响应已填充")
    check(ev.m_eventId > 0, "postWait 事件已赋递增 eventId")
    check("pending:0" in line_of(loop, "wb"), "postWait 完成后 pending:0")

    # 异常路径：null 事件
    check(loop.post(None) == 1, "post(null) 返回 1")
    check(loop.post_wait(None) == 1, "postWait(null) 返回 1")

    loop.destroy()


# ============ Section 6: worker 内 postWait 死锁防护（白盒）============
def section_6():
    reset_order()
    loop = EventLoop()
    check(loop.start() == 0, "start 返回 0")

    # 外层事件 handler 在 worker 线程内调用 post_wait（内层事件）：
    # 防护路径识别 worker 线程后直接执行内层事件，避免自等待死锁
    inner = Event("wb", None, counting_handler("inner"), "inner")

    def outer_handler(pkg):
        loop.post_wait(inner)
        record_order("outer")
        return ysp.YomkResponse(ysp.ResStatus.eOk, "outer done")

    outer = Event("wb", None, outer_handler, "outer")
    check(loop.post(outer) == 0, "post 外层事件返回 0")

    done = wait_until(
        lambda: (lambda o: len(o) == 2 and o[0] == "inner" and o[1] == "outer")(order_snapshot()), 3.0)
    check(done, "worker 内 postWait 防护路径依序完成（inner -> outer，不挂起）")
    check("pending:0" in line_of(loop, "wb"), "防护路径完成后 pending:0")

    loop.destroy()


# ============ Section 7: run() 异常吞噬与存活（白盒）============
def section_7():
    reset_order()
    loop = EventLoop()
    check(loop.start() == 0, "start 返回 0")

    # Exception 分支：postWait 的 handler 抛出，run() 捕获吞噬后仍触发 waitCallback，postWait 不挂起
    def thrower(pkg):
        raise RuntimeError("boom")

    check(loop.post_wait(Event("wb", None, thrower, "thr1")) == 0,
          "handler 抛 Exception 时 postWait 仍返回 0（异常被吞噬）")

    # 未知异常分支（非 Exception，main throw 42 映射 BaseException 分支）
    def thrower2(pkg):
        raise BaseException("non-exception")

    check(loop.post(Event("wb", None, thrower2, "thr2")) == 0, "post 抛未知异常事件返回 0")

    # 循环存活：后续正常事件仍执行
    check(loop.post(Event("wb", None, counting_handler(
        "alive"), "alive")) == 0, "post 正常事件返回 0")
    check(wait_until(lambda: bool(order_snapshot()) and order_snapshot()[-1] == "alive", 2.0),
          "异常吞噬后循环存活（后续事件正常执行）")
    check("pending:0" in line_of(loop, "wb"), "异常用例后 pending:0")

    loop.destroy()


# ============ Section 8: 未运行幂等 / 停止态拒收 / 默认函数 / infoLine 分支（白盒）============
def section_8():
    reset_order()
    GATE.reset()
    loop = EventLoop()

    # 未启动：stop/destroy 幂等早退（无工作线程可 join）
    check(loop.stop() == 0, "未启动时 stop 返回 0（幂等早退）")
    check(loop.destroy() == 0, "未启动时 destroy 返回 0（幂等早退）")

    # 停止态拒收：stop 后投递被拒（返回码 2，与入队成功 0/事件为空 1 区分），事件不入队不执行
    check(loop.start() == 0, "start 返回 0")
    loop.stop()
    check(loop.post(Event("wb", None, counting_handler("rej"), "rej"))
          == 2, "停止态 post 返回 2（投递被拒）")
    check(loop.post_wait(Event("wb", None, counting_handler("rej2"), "rej2")) == 2,
          "停止态 postWait 返回 2（被拒且不等待）")
    check(loop.start() == 0, "重启返回 0")
    time.sleep(0.3)
    check(order_snapshot() == [], "停止态投递的事件重启后仍未执行（停止期间投递被拒）")

    # 默认处理函数：事件无 serviceFunc 时由 set_default_service_func 兜底
    loop.set_default_service_func(counting_handler("dflt"), "MsgX")
    check(loop.post(Event("wb", None, None, ""))
          == 0, "post 无 serviceFunc 事件返回 0")
    check(wait_until(lambda: bool(order_snapshot()) and order_snapshot()[-1] == "dflt", 2.0),
          "无 serviceFunc 事件由默认处理函数执行")

    # infoLine 分支：defaultFunc:on [MsgX]、空 tag "-" 占位、tagCount=0 与超队列长度
    check(loop.post(Event("wb", None, gate_handler(), "blk"))
          == 0, "post 阻塞事件 blk 返回 0")
    check(wait_gate_started(2.0), "阻塞事件 blk 已进入执行")

    check(loop.post(Event("wb", None, counting_handler("t2"), "t2"))
          == 0, "post 事件 t2 返回 0")
    check(loop.post(Event("wb", None, counting_handler("t3"), ""))
          == 0, "post 空 tag 事件返回 0")

    line = line_of(loop, "wb")
    check("defaultFunc:on" in line, "infoLine 显示 defaultFunc:on")
    check("[MsgX]" in line, "infoLine 附加默认函数类型名 [MsgX]")
    check("nextNEventTag(3): t2, -" in line, "infoLine 空 tag 以 - 占位（t2, -）")
    check("nextNEventTag(0):" in line_of(
        loop, "wb", 0), "tagCount=0 输出空 tag 集")
    check("nextNEventTag(1000): t2, -" in line_of(loop,
          "wb", 1000), "tagCount 超队列长度时全列出")

    open_gate()
    loop.destroy()


# ============ Section 9: API 层停止保留 / 重启续跑 / 销毁移除 / 契约与边界 ============
def section_9():
    import YomkApi
    server = YomkApi.init(1)
    check(server is not None, "YomkApi.init 返回非空服务器")

    reset_order()
    GATE.reset()

    start_resp = YomkApi.event_loop_start("api_loop")
    check(start_resp.status == ysp.ResStatus.eOk, "START api_loop 返回 eOk")

    post_resp = YomkApi.event_loop_post(
        "api_loop", "block", gate_handler(), "b1")
    check(post_resp.status == ysp.ResStatus.eOk, "POST 阻塞事件 b1 返回 eOk")
    check(wait_gate_started(2.0), "阻塞事件 b1 已进入执行")

    check(YomkApi.event_loop_post("api_loop", "c1", counting_handler("c1"), "c1").status == ysp.ResStatus.eOk,
          "POST 计数事件 c1 返回 eOk")
    check(YomkApi.event_loop_post("api_loop", "c2", counting_handler("c2"), "c2").status == ysp.ResStatus.eOk,
          "POST 计数事件 c2 返回 eOk")

    # 辅助线程 STOP：服务层 stop 内部 join 等待在途事件收尾
    stopper = threading.Thread(
        target=YomkApi.event_loop_stop, args=("api_loop",))
    stopper.start()
    check(wait_until(lambda: "running:off" in line_of_api("api_loop"), 2.0),
          "API 停止过程中 INFO_LOOP 观察到 running:off")

    open_gate()
    stopper.join()

    check(order_snapshot() == [], "API 停止后排队事件未执行")
    line = line_of_api("api_loop")
    check("running:off" in line, "API 停止后 running:off")
    check("pending:2" in line, "API 停止后 pending:2（未执行事件保留）")

    # 停止态投递被拒（契约回归）：服务层映射 eNo，调用方可区分"已入队"与"被拒"
    check(YomkApi.event_loop_post("api_loop", "rx", counting_handler("rx"), "rx").status == ysp.ResStatus.eNo,
          "停止态 POST 返回 eNo（投递被拒）")
    check(YomkApi.event_loop_post_wait("api_loop", "rx2", counting_handler("rx2"), "rx2").status == ysp.ResStatus.eNo,
          "停止态 POST_WAIT 返回 eNo（被拒且不等待）")
    check("pending:2" in line_of_api("api_loop"), "被拒投递未入队（pending 仍为 2）")

    # 越界数字 tagCount（D3 回归）：int 越界回退不再崩溃，回退默认 3
    oob = YomkApi.event_loop_info_loop("api_loop 99999999999999999999")
    check(oob.status == ysp.ResStatus.eOk, "越界数字 tagCount 返回 eOk（不崩溃）")
    check("nextNEventTag(3)" in oob.msg, "越界数字 tagCount 回退默认 3")
    check("99999999999999999999" not in oob.msg, "越界数字串不出现在回显中")

    # 重启续跑：服务层 START 命中已存在条目即重启，积压按 FIFO 续跑
    check(YomkApi.event_loop_start("api_loop").status ==
          ysp.ResStatus.eOk, "再次 START 返回 eOk（重启）")
    check(wait_until(
        lambda: (lambda o: len(o) == 2 and o[0] == "c1" and o[1] == "c2")(order_snapshot()), 2.0),
        "API 重启后积压按 FIFO 顺序续跑完成（c1 -> c2）")

    # POST_WAIT 同步路径：返回即事件已执行完，回传 Event 携带执行结果
    pw = YomkApi.event_loop_post_wait(
        "api_loop", "pw1", counting_handler("pw1"), "pw1")
    check(pw.status == ysp.ResStatus.eOk, "POST_WAIT 返回 eOk")
    pw_ev = pw.data
    check(pw_ev is not None, "POST_WAIT 回传 Event 可用")
    check(pw_ev.m_tag == "pw1", "POST_WAIT 回传事件 tag 一致")
    check(pw_ev.m_response is not None and pw_ev.m_response.status == ysp.ResStatus.eOk,
          "POST_WAIT 回传事件响应已填充")
    check(bool(order_snapshot()) and order_snapshot()
          [-1] == "pw1", "POST_WAIT 返回即事件已执行（同步性）")

    # 不存在循环名：五个操作接口均 eNo（不影响既有循环与执行顺序观测）
    check(YomkApi.event_loop_stop("no_such").status ==
          ysp.ResStatus.eNo, "STOP 不存在循环返回 eNo")
    check(YomkApi.event_loop_post("no_such", "x", None, "").status == ysp.ResStatus.eNo,
          "POST 不存在循环返回 eNo")
    check(YomkApi.event_loop_post_wait("no_such", "x", None, "").status == ysp.ResStatus.eNo,
          "POST_WAIT 不存在循环返回 eNo")
    check(YomkApi.event_loop_destroy("no_such").status ==
          ysp.ResStatus.eNo, "DESTROY 不存在循环返回 eNo")
    check(YomkApi.event_loop_info_loop("no_such").status ==
          ysp.ResStatus.eNo, "INFO_LOOP 不存在循环返回 eNo")

    # INFO_LOOP 数字 tagCount 路径："name N" 字符串解析与类型化重载、大数值（合法范围）
    check("nextNEventTag(5)" in YomkApi.event_loop_info_loop("api_loop 5").msg,
          "字符串数字路径解析 tagCount=5")
    check("nextNEventTag(7)" in YomkApi.event_loop_info_loop("api_loop", 7).msg,
          "类型化重载解析 tagCount=7")
    check("nextNEventTag(1000000)" in YomkApi.event_loop_info_loop("api_loop 1000000").msg,
          "大数值 tagCount 正常解析（合法范围内）")

    # INFO_LOOPS：api_loop 存活时列表包含其名（loops() 遍历路径；销毁后移除验证见段尾）
    loops_alive_resp = YomkApi.event_loop_info_loops()
    check(loops_alive_resp.status == ysp.ResStatus.eOk, "INFO_LOOPS 返回 eOk")
    loops_alive = loops_alive_resp.data
    check(loops_alive is not None and "api_loop" in loops_alive,
          "INFO_LOOPS 列表包含 api_loop")

    # INFO_ALL：列表包含 api_loop 信息行
    all_resp = YomkApi.event_loop_info_all()
    check(all_resp.status == ysp.ResStatus.eOk, "INFO_ALL 返回 eOk")
    all_lines = all_resp.data
    check(all_lines is not None and any("api_loop" in l for l in all_lines),
          "INFO_ALL 列表包含 api_loop 信息行")

    # 空循环名边界：空串为合法 map 键，全生命周期 eOk
    check(YomkApi.event_loop_start("").status ==
          ysp.ResStatus.eOk, "START 空循环名返回 eOk")
    check(YomkApi.event_loop_post("", "x", counting_handler("empty"), "").status == ysp.ResStatus.eOk,
          "POST 空循环名返回 eOk")
    check(wait_until(lambda: bool(order_snapshot()) and order_snapshot()[-1] == "empty", 2.0),
          "空循环名事件正常执行")
    check(YomkApi.event_loop_info_loop("").status ==
          ysp.ResStatus.eOk, "INFO_LOOP 空循环名返回 eOk")
    check(YomkApi.event_loop_destroy("").status ==
          ysp.ResStatus.eOk, "DESTROY 空循环名返回 eOk")

    # 超长循环名边界（65536 字符）：生命周期各接口 eOk 且 infoLine 完整回显
    long_name = "L" * 65536
    check(YomkApi.event_loop_start(long_name).status ==
          ysp.ResStatus.eOk, "START 超长循环名返回 eOk")
    long_info = YomkApi.event_loop_info_loop(long_name)
    check(long_info.status == ysp.ResStatus.eOk, "INFO_LOOP 超长循环名返回 eOk")
    check(long_name in long_info.msg, "INFO_LOOP 完整回显超长循环名")
    check(YomkApi.event_loop_destroy(long_name).status ==
          ysp.ResStatus.eOk, "DESTROY 超长循环名返回 eOk")

    # 销毁移除条目
    check(YomkApi.event_loop_destroy("api_loop").status ==
          ysp.ResStatus.eOk, "DESTROY api_loop 返回 eOk")
    loops_resp = YomkApi.event_loop_info_loops()
    check(loops_resp.status == ysp.ResStatus.eOk, "INFO_LOOPS 返回 eOk")
    loops_arr = loops_resp.data
    check(loops_arr is not None and "api_loop" not in loops_arr,
          "销毁后 INFO_LOOPS 不再包含 api_loop")


# ============ Section 11: delService("/YomkEventLoop") 残留 loop 收尾与重注册（API 段）============
def section_11():
    import YomkApi
    check(YomkApi.event_loop_start("/L1").status ==
          ysp.ResStatus.eOk, "启动 /L1（运行中）")

    # 运行中删除内置服务：残留 /L1 loop 经 deinit override 收尾（E4 落地的析构兜底链语言映射），
    # del_service 锁外同步调用 deinit——无挂起即证
    check(YomkApi.del_service("/YomkEventLoop")
          == 0, "删除内置 /YomkEventLoop 返回 0")
    after_del = YomkApi.event_loop_start("/L2")
    check(after_del.status == ysp.ResStatus.eNo and after_del.msg == "service not found: /YomkEventLoop",
          "删除后 EVENTLOOP API 服务级 eNo + 精确消息")
    srv_names = YomkApi.server().service_names()
    check("/YomkEventLoop" not in srv_names, "服务表快照不再含 /YomkEventLoop")

    check(YomkApi.new_service(ysp.YomkEventLoop)
          == 0, "new_service 无参重注册返回 0（构造函数固定名生效）")
    check(YomkApi.event_loop_start("/L2").status ==
          ysp.ResStatus.eOk, "重注册后 EVENTLOOP_START 恢复可用")
    pw_relink = YomkApi.event_loop_post_wait(
        "/L2", "relink", counting_handler("relink"), "relink")
    check(pw_relink.status == ysp.ResStatus.eOk,
          "重注册后 EVENTLOOP_POST_WAIT 全链路 eOk")

    YomkApi.shutdown()


# ============ Section 10: postWait×destroy 释放等待者（白盒，看门狗防挂起）============
def section_10():
    reset_order()
    GATE.reset()
    loop = EventLoop()
    check(loop.start() == 0, "start 返回 0")

    # g1 占住工作线程，制造确定性排队窗口
    check(loop.post(Event("wb", None, gate_handler(), "g1"))
          == 0, "post 阻塞事件 g1 返回 0")
    check(wait_gate_started(2.0), "g1 已进入执行（阻塞占住工作线程）")

    # T1：postWait 计数事件 e2——入队成功（rc1 将为 0）后阻塞等待；e2 排队不执行
    rc1 = [0]
    t1done = threading.Event()
    e2 = Event("wb", None, counting_handler("e2"), "e2")

    def waiter():
        rc1[0] = loop.post_wait(e2)
        t1done.set()

    t_waiter = threading.Thread(target=waiter)
    t_waiter.start()
    check(wait_until(lambda: "pending:1" in line_of(
        loop, "wb"), 2.0), "e2 已入队（pending:1，T1 进入等待）")
    time.sleep(0.1)  # 确保 T1 已进入 wait（谓词式 wait 对早触发亦免疫，此处确定性覆盖"唤醒"分支）

    # T2：destroy——stop 置位后 join 等待 g1 收尾，需主线程确认停止置位再释放 gate
    destroyer = threading.Thread(target=loop.destroy)
    destroyer.start()
    check(wait_until(lambda: "running:off" in line_of(loop, "wb"), 2.0),
          "destroy 过程中观察到 running:off（先停止）")

    # 释放 g1：worker 退出 -> destroy 清队列丢弃 e2 并触发其等待回调 -> T1 解除阻塞
    open_gate()

    released = wait_until(t1done.is_set, 3.0)
    check(released, "postWait 等待者被 destroy 释放（3s 看门狗内返回，不挂起）")
    check(rc1[0] == 0, "被丢弃事件入队时已成功（rc1==0，丢弃不改变入队结果）")

    t_waiter.join()
    destroyer.join()

    check("e2" not in order_snapshot(), "被丢弃事件 e2 未执行")
    check("pending:0" in line_of(loop, "wb"), "destroy 后 pending:0（队列已清空）")

    loop.destroy()  # 幂等收尾（空队列再清一次无害）


# ============ Section 12: 析构兜底释放等待者（白盒，看门狗）============
def section_12():
    reset_order()
    GATE.reset()
    holder = []
    marker = threading.Event()

    loop = EventLoop()
    probe_ref = weakref.ref(loop)
    holder.append(loop)
    check(holder[0].start() == 0, "start 返回 0")

    # g1 占住工作线程，制造确定性排队窗口
    check(holder[0].post(Event("wb", None, gate_handler(), "g1"))
          == 0, "post 阻塞事件 g1 返回 0")
    check(wait_gate_started(2.0), "g1 已进入执行（阻塞占住工作线程）")

    # T1 等待者映射：排队事件 e2 携带标记等待回调（main 为阻塞 postWait 线程——映射差异见文件头；
    # 若 e2 被执行则 waitCallback 亦触发，由"未执行"断言区分）
    e2 = Event("wb", None, counting_handler("e2"), "e2")
    e2.m_waitCallback = marker.set
    check(holder[0].post(e2) == 0, "post 事件 e2 返回 0（携带等待回调）")
    check(wait_until(lambda: "pending:1" in holder[0].info_line(
        "wb", 3), 2.0), "e2 已入队（pending:1）")
    time.sleep(0.1)  # 确保 e2 稳定排队（对齐 main 确定性窗口意图）

    # 前置：显式 stop 退出 worker（E1 语义：stop 仅退线程不清队列——e2 保留）。
    # python 运行中栈帧持 self 强引用，"worker 执行中析构"不可达；stop 后帧消亡，失去引用方可触发 __del__
    stopper = threading.Thread(target=holder[0].stop)
    stopper.start()
    check(wait_until(lambda: "running:off" in holder[0].info_line("wb", 3), 2.0),
          "停止置位（worker 将退出，e2 保留在队列）")
    open_gate()
    stopper.join()

    # T2：不调 destroy 直接析构——主线程放弃引用、清空唯一外部引用 + gc.collect() 触发 __del__ 兜底
    # （Thread(target=self.run) 引用环经 gc 回收；python 无 C++ 析构确定性，映射差异见文件头）
    loop = None

    def resetter():
        holder.clear()
        gc.collect()

    t_resetter = threading.Thread(target=resetter)
    t_resetter.start()

    # 析构兜底：__del__ -> destroy 清队列丢弃 e2 并触发其等待回调（destroy 全程未经显式调用）
    released = wait_until(marker.is_set, 3.0)
    check(released, "被丢弃事件的等待回调被析构兜底触发（3s 看门狗内，不挂起）")

    t_resetter.join()

    check("e2" not in order_snapshot(), "被丢弃事件 e2 未执行")
    check(wait_until(lambda: probe_ref() is None, 3.0), "析构完成（weakref 归零，对象已回收）")


def main():
    section_1_2()
    section_3()
    section_4()
    section_5()
    section_6()
    section_7()
    section_8()
    section_9()
    section_11()
    section_10()
    section_12()

    print("TestYomkEventLoopLifecycle all check passed." if FAILED == 0
          else f"TestYomkEventLoopLifecycle FAILED ({FAILED} checks failed).")
    sys.exit(0 if FAILED == 0 else 1)


if __name__ == "__main__":
    main()
