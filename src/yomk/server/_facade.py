import atexit
import threading
from typing import Any, Optional

from yomk.msg.server import ResStatus, Response
from yomk._diag import error as diag_error
from yomk.server.YomkServer import YomkServer

# 门面共享状态：只允许经模块属性访问（from yomk.server import _facade），
# 禁止 from yomk.server._facade import g_server（会捕获 import 时的 None）
g_server: Optional[YomkServer] = None
_init_done = False
_init_lock = threading.Lock()


def current_server() -> Optional[YomkServer]:
    # 全局单例读取入口（未 init 时为 None）
    return g_server


def api_request(url: str, pkg: Any = None) -> Response:
    # 统一门面请求入口：未 init 记诊断并返回 eInvalid
    server = g_server
    if server is None:
        diag_error("YomkServer is not init")
        return Response(ResStatus.eInvalid, "YomkServer is not init")
    return server.request(url, pkg)


def init_locked(async_thread_count: int = 0) -> Optional[YomkServer]:
    # 初始化框架单例并启动 5 个内置服务；进程级幂等，shutdown 后不再重建
    global g_server, _init_done
    with _init_lock:
        if not _init_done:
            srv = YomkServer.create(async_thread_count)
            srv.start_service(
                ["/YomkFunctionPool", "/YomkContext", "/YomkEventLoop",
                 "/YomkLogger", "/YomkServerInfo"])
            g_server = srv
            _init_done = True
            atexit.register(shutdown_locked)
    return g_server


def shutdown_locked() -> None:
    # 关闭服务器并释放所有服务；未 init 直接返回
    global g_server
    with _init_lock:
        if g_server is None:
            return
        g_server.shutdown()
        g_server = None


def boot_locked(boot: Any = None) -> int:
    # 初始化并执行启动编排流程（before -> start -> after）；
    # 钩子未显式返回（None）等价成功 0
    init_locked()
    if boot is None:
        return 0

    ret = boot.before()
    if ret is not None and ret != 0:
        diag_error("YomkBoot before failed!")
        return ret

    ret = boot.start()
    if ret is not None and ret != 0:
        diag_error("YomkBoot start failed!")
        return ret

    ret = boot.after()
    if ret is not None and ret != 0:
        diag_error("YomkBoot after failed!")
        return ret

    return 0


def chain_status(res: Response) -> ResStatus:
    # 请求域 .status() 三态收敛：非成功记框架自诊断
    if res.status != ResStatus.Succeeded:
        diag_error(res.msg if res.msg else "request failed")
    return res.status