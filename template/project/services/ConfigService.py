import json
from typedefine.TypeDefine import CTX_CONFIG_PATH, APP_VERSION
from yomk.server import YomkService
from yomk.ctx import ctx
from yomk.log import log
from yomk.msgs.server import ResStatus, Response
from yomk.msgs.YomkMsg import unpack
from yomk.msgs.std_msg import String, type_mismatch
from msgs.YomkMsgs import ConfigKey, ConfigKeyValue


class ConfigService(YomkService):
    def __init__(self, server):
        super().__init__(server)
        self.set_name("/ConfigService")
        self.config_path = ""
        self.config_json = {}

    def init(self):
        # 端点声明第三参传 msg.TYPE_NAME（禁止手写字面量，与框架服务同规）
        self.install_func("/load", self.load_config)
        self.install_func("/get", self.get_config, ConfigKey.TYPE_NAME)
        self.install_func("/set", self.set_config, ConfigKeyValue.TYPE_NAME)
        self.install_func("/reload", self.reload_config)
        self.install_func("/version", self.version)
        log().tag("ConfigService::init").info("install func [ /load /get /set /reload /version ] to ", self.get_name())

    def load_config(self, msg):
        # 类型化读：msg(String) 声明期望类型，with_default 传同类型消息或 None
        cfg = ctx().key(CTX_CONFIG_PATH).msg(String).with_default(String("")).get()
        self.config_path = cfg.value
        if not self.config_path:
            return Response(ResStatus.eNo, "config_path not found in context")
        try:
            with open(self.config_path, 'r', encoding='utf-8') as f:
                self.config_json = json.load(f)
        except Exception as e:
            return Response(ResStatus.eNo, f"failed to open: {self.config_path}, {e}")
        log().tag("ConfigService::load_config").info("loaded: ", self.config_path)
        return Response(ResStatus.eOk, "ok")

    def get_config(self, msg):
        # 端点体内 unpack 精确校验（责任在端点作者，框架不做分发校验）；
        # 失败文案走 type_mismatch——期望/实际双侧均为 TYPE_NAME，零手写报文名
        if unpack(msg, ConfigKey) is None:
            return Response(ResStatus.eNo, type_mismatch(msg, ConfigKey.TYPE_NAME))
        tokens = msg.key.split('.')
        current = self.config_json
        for token in tokens:
            if not isinstance(current, dict) or token not in current:
                return Response(ResStatus.eNo, "key not found: " + msg.key)
            current = current[token]

        if isinstance(current, str):
            value = current
        else:
            value = json.dumps(current, ensure_ascii=False)
        return Response(ResStatus.eOk, "ok", value)

    def set_config(self, msg):
        if unpack(msg, ConfigKeyValue) is None:
            return Response(ResStatus.eNo,
                            type_mismatch(msg, ConfigKeyValue.TYPE_NAME))
        tokens = msg.key.split('.')
        current = self.config_json
        for token in tokens[:-1]:
            if not isinstance(current, dict):
                return Response(ResStatus.eNo, "invalid key path: " + msg.key)
            current = current.setdefault(token, {})
        current[tokens[-1]] = msg.value
        log().tag("ConfigService::set_config").info("set ", msg.key, " = ", msg.value)
        return Response(ResStatus.eOk, "ok")

    def reload_config(self, msg):
        # reload 先回取 context 中的 config_path，缺失即 eNo
        cfg = ctx().key(CTX_CONFIG_PATH).msg(String).with_default(String("")).get()
        self.config_path = cfg.value
        if not self.config_path:
            return Response(ResStatus.eNo, "config_path not found in context")
        try:
            with open(self.config_path, 'r', encoding='utf-8') as f:
                self.config_json = json.load(f)
        except Exception as e:
            return Response(ResStatus.eNo, f"failed to reload: {self.config_path}, {e}")
        log().tag("ConfigService::reload_config").info("reloaded: ", self.config_path)
        return Response(ResStatus.eOk, "ok")

    def version(self, msg):
        # 版本号来自 typedefine/TypeDefine.py 定义的 APP_VERSION 常量
        return Response(ResStatus.eOk, "ok", APP_VERSION)
