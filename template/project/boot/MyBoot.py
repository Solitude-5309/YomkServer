import YomkApi
from pathlib import Path
from typedefine.TypeDefine import CTX_CONFIG_PATH
from services.ConfigService import ConfigService
from msgs.YomkMsgs import *


class MyBoot(YomkApi.YomkBoot):
    def __init__(self, srv_names):
        super().__init__()
        self.srv_names = srv_names

    def before(self):
        YomkApi.console_log_info_tag("MyBoot::before", "starting...")

        # 通过 __file__ 推导配置文件路径
        project_dir = Path(__file__).resolve().parent.parent
        config_path = str(project_dir / "config" / "config.json")
        YomkApi.context_create(CTX_CONFIG_PATH, config_path)
        YomkApi.console_log_info_tag(
            "MyBoot::before", "config path: ", config_path)
        return 0

    def start(self):
        # 服务创建器映射表
        cur_srvs = {
            "/ConfigService": lambda: ConfigService(YomkApi.server()),
        }

        # 根据 srv_names 按需启动服务
        for srv_name in self.srv_names:
            if srv_name in cur_srvs:
                if YomkApi.add_service(cur_srvs[srv_name](), srv_name) != 0:
                    return -1
                YomkApi.console_log_info_tag(
                    "MyBoot::start", "service ", srv_name, " done")

        return 0

    def after(self):
        resp = YomkApi.request("/ConfigService/load", None)
        if resp.status != YomkApi.ResStatus.eOk:
            YomkApi.console_log_error_tag(
                "MyBoot::after", "load config failed: ", resp.msg)
            return -1
        YomkApi.console_log_info_tag("MyBoot::after", "started successfully.")

        resp = YomkApi.request("/ConfigService/get", ConfigKey("name"))
        if resp.status != YomkApi.ResStatus.eOk:
            YomkApi.console_log_error_tag(
                "MyBoot::after", "get config name failed: ", resp.msg)
            return -1
        YomkApi.console_log_info_tag(
            "MyBoot::after", "config name: ", resp.data)

        # 测试版本号接口：获取并输出工程版本号（typedefine/TypeDefine.py 定义的 APP_VERSION 常量）
        resp = YomkApi.request("/ConfigService/version", None)
        if resp.status != YomkApi.ResStatus.eOk:
            YomkApi.console_log_error_tag(
                "MyBoot::after", "get version failed: ", resp.msg)
            return -1
        YomkApi.console_log_info_tag(
            "MyBoot::after", "project version: ", resp.data)

        resp = YomkApi.request("/ConfigService/get", ConfigKey("description"))
        if resp.status != YomkApi.ResStatus.eOk:
            YomkApi.console_log_error_tag(
                "MyBoot::after", "get config description failed: ", resp.msg)
            return -1
        YomkApi.console_log_info_tag(
            "MyBoot::after", "config description: ", resp.data)

        return 0
