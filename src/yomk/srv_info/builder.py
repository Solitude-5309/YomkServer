from yomk.server import _facade
from yomk.url.srv_info import (
    SRV_ALL,
    SRV_FUNCTION,
    SRV_FUNCTIONS,
    SRV_SERVICES,
)


class SrvInfoBuilder:
    # server_info 域 builder：服务名/函数 URL 累积 + 四类内省终结
    # （从旧 ServerBuilder 迁出的 services/functions/fn_url/get_info/all）
    def __init__(self):
        self._srv_name = ""
        self._fn_url = ""

    def srv_name(self, srv_name):
        # 形参名对齐 C++ SrvInfoBuilder::srv_name(srvName)（YomkServerInfoApi.h）
        self._srv_name = srv_name
        return self

    def fn_url(self, url):
        # 形参名对齐 C++ SrvInfoBuilder::fn_url(url)
        self._fn_url = url
        return self

    def services(self):
        # 查询所有服务名称
        return _facade.api_request(SRV_SERVICES, None)

    def functions(self):
        # 查询指定服务的功能函数清单（用 srv_name 累积）
        return _facade.api_request(SRV_FUNCTIONS, self._srv_name)

    def get_info(self):
        # 查询指定功能函数元信息（用 fn_url 累积）
        return _facade.api_request(SRV_FUNCTION, self._fn_url)

    def all(self):
        # 查询框架全量服务及功能函数拓扑
        return _facade.api_request(SRV_ALL, None)


def srv_info():
    # server_info 域链式入口
    return SrvInfoBuilder()
