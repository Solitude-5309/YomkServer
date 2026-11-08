from pathlib import Path
from typedefine.TypeDefine import CTX_CONFIG_PATH
from services.ConfigService import ConfigService
from msgs.YomkMsgs import *
from yomk.boot import YomkBoot
from yomk.server import server
from yomk.ctx import ctx
from yomk.log import log
from yomk.msg.server import ResStatus

class MyBoot(YomkBoot):
    def __init__(self, srv_names):
        super().__init__()
        self.srv_names = srv_names

    def before(self):
        log().tag("MyBoot::before").info("starting...")

        # 通过 __file__ 推导配置文件路径
        project_dir = Path(__file__).resolve().parent.parent
        config_path = str(project_dir / "config" / "config.json")
        ctx().key(CTX_CONFIG_PATH).value(config_path).create()
        log().tag("MyBoot::before").info("config path: ", config_path)
        return 0

    def start(self):
        # 服务创建器映射表
        cur_srvs = {
            "/ConfigService": lambda: ConfigService(server().instance()),
        }

        # 根据 srv_names 按需启动服务
        for srv_name in self.srv_names:
            if srv_name in cur_srvs:
                if server().srv_ptr(cur_srvs[srv_name]()).srv_name(srv_name).add() != 0:
                    return -1

        return 0

    def after(self):
        resp = server().url("/ConfigService/load").request()
        if resp.status != ResStatus.eOk:
            log().tag("MyBoot::after").error("load config failed: ", resp.msg)
            return -1
        log().tag("MyBoot::after").info("started successfully.")

        resp = server().url("/ConfigService/get").msg(ConfigKey("name")).request()
        if resp.status != ResStatus.eOk:
            log().tag("MyBoot::after").error("get config name failed: ", resp.msg)
            return -1
        log().tag("MyBoot::after").info("config name: ", resp.data)

        # 测试版本号接口：获取并输出工程版本号（typedefine/TypeDefine.py 定义的 APP_VERSION 常量）
        resp = server().url("/ConfigService/version").request()
        if resp.status != ResStatus.eOk:
            log().tag("MyBoot::after").error("get version failed: ", resp.msg)
            return -1
        log().tag("MyBoot::after").info("project version: ", resp.data)

        resp = server().url("/ConfigService/get").msg(ConfigKey("description")).request()
        if resp.status != ResStatus.eOk:
            log().tag("MyBoot::after").error("get config description failed: ", resp.msg)
            return -1
        log().tag("MyBoot::after").info("config description: ", resp.data)

        return 0
