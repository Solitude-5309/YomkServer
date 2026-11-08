from yomk.event_loop.EventLoop import Event
from yomk.msg.event_loop import EventLoopPkg, EventLoopQuery
from yomk.server import _facade


class EventLoopBuilder:
    # event_loop 域 builder：loop 分面定位 + 参数前置累积 + 无参终结
    def __init__(self):
        self._name = ""
        self._fn = None
        self._msg_name = ""
        self._data = None
        self._tag = ""
        self._tag_count = 3

    def loop(self, n):
        self._name = n
        return self

    def handle_fn(self, f):
        self._fn = f
        return self

    def msg_name(self, m):
        self._msg_name = m
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
            "/YomkEventLoop/start",
            EventLoopPkg(self._name, self._fn, self._msg_name)))

    def stop(self):
        return _facade.chain_status(
            _facade.api_request("/YomkEventLoop/stop", self._name))

    def post(self):
        # 异步投递，返回入队三态
        return _facade.chain_status(_facade.api_request(
            "/YomkEventLoop/post",
            Event(self._name, self._data, self._fn, self._tag)))

    def post_wait(self):
        # 同步投递（阻塞等待执行完成）
        return _facade.chain_status(_facade.api_request(
            "/YomkEventLoop/post_wait",
            Event(self._name, self._data, self._fn, self._tag)))

    def destroy(self):
        return _facade.chain_status(
            _facade.api_request("/YomkEventLoop/destroy", self._name))

    def list(self):
        return _facade.api_request("/YomkEventLoop/loops", None)

    def get_info(self):
        return _facade.api_request(
            "/YomkEventLoop/loop", EventLoopQuery(self._name, self._tag_count))

    def all(self):
        return _facade.api_request("/YomkEventLoop/all", None)


def event_loop():
    # event_loop 域链式入口
    return EventLoopBuilder()