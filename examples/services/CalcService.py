"""
计算服务：演示功能注册、回包携带数据与跨服务调用（对齐 main 分支 Examples/services/CalcService）

- /echo：最简请求回环（收到的消息原样放入回包 data）
- /add ：收 AddReq 消息包，先跨服务调用 /AuditService/audit 审计，
         再将计算结果以 AddResp 放入回包 data

python 版说明：服务内跨服务调用可用成员 self.server.request(...)（对齐 main
的成员 request / YOMK_REQUEST），也可使用全局 YomkApi.request()（两者等价）。
跨服务失败时返回 eNo，向调用方传播失败。
"""

import YomkApi
from typing import Any

from msgs.DemoMsgs import AddReq, AddResp


class CalcService(YomkApi.YomkService):
    def __init__(self, server):
        super().__init__(server)
        self.set_name("/CalcService")

    def init(self):
        self.install_func("/echo", self.echo)
        self.install_func("/add", self.add)

    def echo(self, pkg: Any) -> YomkApi.YomkResponse:
        print(f"[CalcService] echo called, with msg: {pkg}")
        return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "echo success", pkg)

    def add(self, pkg: Any) -> YomkApi.YomkResponse:
        req: AddReq = pkg
        print(f"[CalcService] add called, a={req.a} b={req.b}")

        # 跨服务调用：先请求审计服务（python 版用全局 YomkApi.request）
        audit_res = YomkApi.request(
            "/AuditService/audit", f"calc add {req.a}+{req.b}")
        if audit_res.status != YomkApi.ResStatus.eOk:
            # 失败传播：审计失败则本次加法直接失败
            return YomkApi.YomkResponse(YomkApi.ResStatus.eNo, "audit failed: " + audit_res.msg)

        resp = AddResp(req.a + req.b)
        print(f"[CalcService] add result: {resp.result}")
        return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "add success", resp)
