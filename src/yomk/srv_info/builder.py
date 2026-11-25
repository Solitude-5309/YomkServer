"""Server info domain chained builder: service, function and aggregate introspection.

服务器信息域链式 Builder：服务、函数与聚合内省。
"""

from yomk.msgs.fn_pool import FuncInfoList
from yomk.msgs.server import ResStatus, Response
from yomk.msgs.srv_info import ServerServiceInfoList
from yomk.msgs.std_msg import String, StringArray
from yomk.msgs.YomkMsg import unpack
from yomk.server import _facade
from yomk.url.srv_info import (
    SRV_ALL,
    SRV_FUNCTION,
    SRV_FUNCTIONS,
    SRV_SERVICES,
)


class SrvInfoBuilder:
    """Server info builder: read-only introspection of services and functions.

    服务器信息 Builder：服务与函数的只读内省。

    Usage:
        services()                         # 全部服务名
        srv_name(n).functions()            # 指定服务的函数清单
        fn_url(u).get_info()               # 指定函数元信息
        all()                              # 全部服务及其函数

        四个终结均返回 Response，data 为裸 list 或单条元信息；
        服务不存在时 functions() 返回空表，get_info() 返回 No。
    """

    def __init__(self):
        self._srv_name = ""
        self._fn_url = ""

    def srv_name(self, srv_name):
        self._srv_name = srv_name
        return self

    def fn_url(self, url):
        self._fn_url = url
        return self

    def services(self):
        """Return all registered service names as a plain list.

        返回全部已注册服务名（裸 list）；服务器已销毁返回 No，类型不符返回 No。
        """
        res = _facade.api_request(SRV_SERVICES, None)
        if res.status == ResStatus.eOk:
            r = unpack(res.data, StringArray)
            if not r:
                return Response(ResStatus.eNo, r.error)
            res.data = r.msg.items
        return res

    def functions(self):
        """Return the function list of the accumulated service.

        返回累积服务的函数清单（裸 list）；服务不存在返回空表，类型不符返回 No。
        """
        res = _facade.api_request(SRV_FUNCTIONS, String(self._srv_name))
        if res.status == ResStatus.eOk:
            r = unpack(res.data, FuncInfoList)
            if not r:
                return Response(ResStatus.eNo, r.error)
            res.data = r.msg.items
        return res

    def get_info(self):
        """Return introspection metadata of the accumulated function URL.

        返回累积函数 URL 的元信息；服务或函数不存在返回 No。
        """
        return _facade.api_request(SRV_FUNCTION, String(self._fn_url))

    def all(self):
        """Return all services with their functions as a plain list.

        返回全部服务及其函数的内省快照（裸 list）；服务器已销毁返回 No，类型不符返回 No。
        """
        res = _facade.api_request(SRV_ALL, None)
        if res.status == ResStatus.eOk:
            r = unpack(res.data, ServerServiceInfoList)
            if not r:
                return Response(ResStatus.eNo, r.error)
            res.data = r.msg.items
        return res


def srv_info():
    """Return the server info domain chained entry.

    返回服务器信息域链式入口。
    """
    return SrvInfoBuilder()
