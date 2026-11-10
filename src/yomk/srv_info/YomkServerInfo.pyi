from typing import Any

from yomk.msg.server import Response
from yomk.server import YomkService


class YomkServerInfo(YomkService):
    # 服务信息内省服务：server 仅保留弱引用（weakref 判活契约），
    # server 已销毁时各端点统一走 destroyed 契约
    server: Any
    m_weakServer: Any

    def __init__(self, server: Any) -> None: ...
    def init(self) -> None: ...
    # 端点载荷形参一律 Any（无运行时守卫意义）：Cython 会把注解编译为边界
    # TypeError，先于运行时 unpack 守卫抛异常，误用载荷须返回契约 Response
    def list_services(self, pkg: Any) -> Response: ...
    def list_functions(self, pkg: Any) -> Response: ...
    def function_info(self, pkg: Any) -> Response: ...
    def list_all(self, pkg: Any) -> Response: ...