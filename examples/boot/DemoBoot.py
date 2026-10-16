"""
启动编排样例：继承 YomkBoot 组织多服务工程启动（对齐 main 分支 Examples/boot/DemoBoot）

YomkApi.boot(boot) 按固定顺序执行三阶段，任一阶段返回非 0 即终止启动：
1. before()：服务启动前准备资源（Context、EventLoop、公共函数等）
2. start() ：创建并注册服务（各服务 init() 由 YomkApi.add_service 内部调用）
3. after() ：启动后善后（预热、通知、自启动任务等）

start() 用 service_creators 映射表管理"服务名 -> 服务类"：
全部服务入清单、按需启动；同一个类也可注册为多个实例。
"""

import YomkApi

from services.AuditService import AuditService
from services.CalcService import CalcService


class DemoBoot(YomkApi.YomkBoot):
    def __init__(self, start_srv_names=None):
        super().__init__()
        self.start_srv_names = start_srv_names or []
        # 服务映射表：服务名 -> 服务类（全部服务入清单、按需启动）
        self.service_creators = {
            "/AuditService": AuditService,
            "/CalcService": CalcService,
        }

    def before(self) -> int:
        print("[DemoBoot] before: 服务启动前准备资源（创建 CONTEXT / EVENTLOOP / 注册公共函数等，本例无需额外资源）")
        return 0

    def start(self) -> int:
        print(f"[DemoBoot] start: 注册服务 {self.start_srv_names}")
        for srv_name in self.start_srv_names:
            srv_cls = self.service_creators.get(srv_name)
            if srv_cls is None:
                print(f"[DemoBoot] unknown service: {srv_name}")
                return -1
            YomkApi.add_service(srv_cls(YomkApi.server()), srv_name)
        return 0

    def after(self) -> int:
        print("[DemoBoot] after: 启动后善后（服务内部初始化、自启动任务等，本例无）")
        return 0
