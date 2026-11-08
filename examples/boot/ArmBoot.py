"""
启动编排样例：继承 YomkBoot 组织机械臂多服务工程启动

server().boot_ptr(boot).run() 按固定顺序执行三阶段，任一阶段返回非 0 即终止启动：
1. before()：服务启动前准备资源（Context、EventLoop、公共函数等）
2. start() ：创建并注册服务（各服务 init() 由框架在此阶段调用）
3. after() ：启动后善后（预热、通知、自启动任务等）

start() 用 service_creators 映射表管理"服务名 -> 服务类"：
全部服务入清单、按需启动；同一个类也可注册为多个实例。
"""

from yomk.boot import YomkBoot
from yomk.server import server
from yomk.log import log

from services.MotionService import MotionService
from services.TelemetryService import TelemetryService

class ArmBoot(YomkBoot):
    def __init__(self, start_srv_names=None):
        super().__init__()
        self.start_srv_names = start_srv_names or []  # 本次要启动的服务清单
        # 服务清单：服务名（/ 开头）-> 服务类（全部服务入清单、按需启动）
        self.service_creators = {
            "/MotionService": MotionService,
            "/TelemetryService": TelemetryService,
        }

    def before(self) -> int:
        # 服务启动前的资源准备：创建 Context、EventLoop、注册公共函数、日志代理等
        log().tag("arm.boot").info("before: prepare resources before services (none needed here)")
        return 0

    def start(self) -> int:
        log().tag("arm.boot").info("start: register services from creator map")
        for srv_name in self.start_srv_names:
            srv_cls = self.service_creators.get(srv_name)
            if srv_cls is None:
                log().tag("arm.boot").error("unknown service in start list: " + srv_name)
                return -1  # 启动清单中出现未知服务，终止启动
            # srv_ptr().add() 移交实例并调用服务 init()
            if server().srv_ptr(srv_cls(server().instance())).srv_name(srv_name).add() != 0:
                return -1
        return 0

    def after(self) -> int:
        # 启动后善后：调用服务接口预热、自启动任务、发送启动完成通知等
        log().tag("arm.boot").info("after: post-start tasks (warmup, notify, ...)")
        return 0
