class ServerServiceInfo:
    # 服务级内省条目（/YomkServerInfo/all 返回数据元素：服务名 + 该服务注册的函数元信息列表）
    def __init__(self, service_name="", funcs=None):
        self.service_name = service_name   # 服务名（带 / 前缀，如 "/FmtSrv"）
        # 函数元信息（与 /function 单查同族 FuncInfo，func_name 为带 / 前缀 url 段）
        self.funcs = funcs if funcs is not None else []