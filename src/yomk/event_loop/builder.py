"""Event loop domain chained builder: lifecycle, post and introspection.

事件循环域链式 Builder：生命周期、投递与内省。
"""

from yomk.msgs.event_loop import Event, EventLoopInfoList, EventLoopOpts, EventLoopQuery
from yomk.msgs.server import ResStatus
from yomk.msgs.std_msg import String, StringArray
from yomk.msgs.YomkMsg import unpack
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
    """Event loop domain builder: lifecycle, post and introspection.

    事件循环域链式 Builder：生命周期、投递与内省。

    Usage:
        Start:  event_loop().loop(n).handle_fn(fn).msg_name(m).start()
        Post:   event_loop().msg(d).loop(n).handle_fn(fn).tag(t).post() / .post_wait()
        Query:  list() / loop(n).tag_count(k).get_info() / all()

        start 对同名循环只重启、不更换默认函数；post 异步入队并立即返回，
        post_wait 阻塞至执行完成；handle_fn 为单次临时处理器，优先于默认函数。
    """

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
        # 槽期望 Response（供 post_wait 等待者读取）；缺省按 "response" 填充
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
        """Start the named loop, or restart it if it already exists.

        启动具名循环；同名已存在时只重启，不更换默认函数，返回三态 ResStatus。
        """
        return _facade.chain_status(_facade.api_request(
            LOOP_START,
            EventLoopOpts(self._name, self._fn, self._msg_name)))

    def stop(self):
        """Stop the loop thread; queued events are kept.

        停止循环线程，队列保留，返回三态 ResStatus；循环不存在返回 No。
        """
        return _facade.chain_status(
            _facade.api_request(LOOP_STOP, String(self._name)))

    def post(self):
        """Enqueue an event asynchronously and return immediately.

        异步投递事件并立即返回入队三态；循环未运行或不存在返回 No。
        """
        return _facade.chain_status(_facade.api_request(
            LOOP_POST,
            Event(self._name, self._data, self._fn, self._tag)))

    def post_wait(self):
        """Enqueue an event and block until it has been executed.

        同步投递事件，阻塞至执行完成并返回三态；循环未运行或不存在返回 No。
        """
        return _facade.chain_status(_facade.api_request(
            LOOP_POST_WAIT,
            Event(self._name, self._data, self._fn, self._tag)))

    def destroy(self):
        """Destroy the loop: stop, drop queued events and release waiters.

        销毁循环并移出注册表：停止线程、清空队列、释放等待者，返回三态 ResStatus。
        """
        return _facade.chain_status(
            _facade.api_request(LOOP_DESTROY, String(self._name)))

    def list(self):
        """Return all loop names as a plain list.

        返回全部循环名（裸 list）；未初始化返回 Invalid。
        """
        res = _facade.api_request(LOOP_LOOPS, None)
        if res.status == ResStatus.eOk:
            res.data = unpack(res.data, StringArray).items
        return res

    def get_info(self):
        """Return introspection metadata of the target loop.

        返回指定循环的内省元信息；循环不存在返回 No。
        """
        return _facade.api_request(
            LOOP_LOOP, EventLoopQuery(self._name, self._tag_count))

    def all(self):
        """Return introspection metadata of all loops as a plain list.

        返回全部循环的内省元信息（裸 list）；未初始化返回 Invalid。
        """
        res = _facade.api_request(LOOP_ALL, None)
        if res.status == ResStatus.eOk:
            res.data = unpack(res.data, EventLoopInfoList).items
        return res


def event_loop():
    """Return the event loop domain chained entry.

    返回事件循环域链式入口。
    """
    return EventLoopBuilder()
