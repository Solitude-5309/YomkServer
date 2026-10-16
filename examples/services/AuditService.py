"""
审计服务：被其他服务调用的最小服务样例（对齐 main 分支 Examples/services/AuditService）

定义一个服务只需三步：
1. 继承 YomkService，构造函数中 set_name("/AuditService") 起名
   （必须 / 开头、全局唯一，作为该服务所有功能函数的 URL 前缀）
2. 实现 init()，用 install_func 注册功能函数（函数名同样必须 / 开头）
3. 功能函数签名统一为 YomkResponse(pkg)

URL：/AuditService/audit —— 记录一条消息并返回
"""

import YomkApi
from typing import Any


class AuditService(YomkApi.YomkService):
    def __init__(self, server):
        super().__init__(server)
        self.set_name("/AuditService")

    def init(self):
        self.install_func("/audit", self.audit)

    def audit(self, pkg: Any) -> YomkApi.YomkResponse:
        print(f"[AuditService] audit called, with msg: {pkg}")
        return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "audit success")
