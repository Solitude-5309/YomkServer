from yomk.event_loop.EventLoop import Event
from yomk.msg.event_loop import EventLoopPkg, EventLoopQuery
from yomk.server import _facade
from yomk.server.YomkService import _yomk_weak_slot_fill
from yomk.url.event_loop import (
    LOOP_ALL,
    LOOP_DESTROY,
    LOOP_LOOP,
    LOOP_LOOPS,
    LOOP_POST,
    LOOP_POST_WAIT,
    LOOP_START,
    LOOP_STOP,
)


class EventLoopBuilder:
    # event_loop 域 builder：loop 分面定位 + 参数前置累积 + 无参终结；
    # 形参名与 C++ Api.h builder 逐字一致（loop(name)/handle_fn(fn)/
    # msg_name(msg_name)/msg(data)/tag(t)/tag_count(n)）
    def __init__(self):
        self._name = ""
        self._fn = None
        self._msg_name = ""
        self._data = None
        self._tag = ""
        self._tag_count = 3

    def loop(self, name):
        self._name = name
        return self

    def handle_fn(self, fn):
        # event_loop 槽期望 Response（响应供 post_wait 等待者读取）：未声明 discard
        # 按 "response" 填充；显式填错注册期即诊断（U-08 落点）
        self._fn = _yomk_weak_slot_fill(fn, "event_loop", "response")
        return self

    def msg_name(self, msg_name):
        self._msg_name = msg_name
        return self

    def msg(self, data):
        self._data = data
        return self

    def tag(self, t):
        self._tag = t
        return self

    def tag_count(self, n):
        self._tag_count = n
        return self

    def start(self):
        # 同名循环不重建：复用既有实例，本次累积的 handle_fn/msg_name 不生效
        return _facade.chain_status(_facade.api_request(
            LOOP_START,
            EventLoopPkg(self._name, self._fn, self._msg_name)))

    def stop(self):
        return _facade.chain_status(
            _facade.api_request(LOOP_STOP, self._name))

    def post(self):
        # 异步投递，返回入队三态
        return _facade.chain_status(_facade.api_request(
            LOOP_POST,
            Event(self._name, self._data, self._fn, self._tag)))

    def post_wait(self):
        # 同步投递（阻塞等待执行完成）
        return _facade.chain_status(_facade.api_request(
            LOOP_POST_WAIT,
            Event(self._name, self._data, self._fn, self._tag)))

    def destroy(self):
        return _facade.chain_status(
            _facade.api_request(LOOP_DESTROY, self._name))

    def list(self):
        return _facade.api_request(LOOP_LOOPS, None)

    def get_info(self):
        return _facade.api_request(
            LOOP_LOOP, EventLoopQuery(self._name, self._tag_count))

    def all(self):
        return _facade.api_request(LOOP_ALL, None)


def event_loop():
    # event_loop 域链式入口
    return EventLoopBuilder()
