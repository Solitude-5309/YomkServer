from yomk.server import _facade


class SrvInfoBuilder:
    # server_info 域 builder：服务名/函数 URL 累积 + 四类内省终结
    # （从旧 ServerBuilder 迁出的 services/functions/fn_url/get_info/all）
    def __init__(self):
        self._srv_name = ""
        self._fn_url = ""

    def srv_name(self, n):
        self._srv_name = n
        return self

    def fn_url(self, u):
        self._fn_url = u
        return self

    def services(self):
        # 查询所有服务名称
        return _facade.api_request("/YomkServerInfo/services", None)

    def functions(self):
        # 查询指定服务的功能函数清单（用 srv_name 累积）
        return _facade.api_request("/YomkServerInfo/functions", self._srv_name)

    def get_info(self):
        # 查询指定功能函数元信息（用 fn_url 累积）
        return _facade.api_request("/YomkServerInfo/function", self._fn_url)

    def all(self):
        # 查询框架全量服务及功能函数拓扑
        return _facade.api_request("/YomkServerInfo/all", None)


def srv_info():
    # server_info 域链式入口
    return SrvInfoBuilder()