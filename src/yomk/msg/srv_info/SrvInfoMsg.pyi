from typing import List, Optional

from yomk.msg.fn_pool import FuncInfo


class ServerServiceInfo:
    # 服务级内省条目（/YomkServerInfo/all 返回数据元素：服务名 + 该服务注册的函数元信息列表）
    service_name: str   # 服务名（带 / 前缀，如 "/FmtSrv"）
    # 函数元信息（与 /function 单查同族 FuncInfo，func_name 为带 / 前缀 url 段）
    funcs: List[FuncInfo]

    def __init__(self, service_name: str = "",
                 funcs: Optional[List[FuncInfo]] = None) -> None: ...