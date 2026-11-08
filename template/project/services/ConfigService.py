
import json
from typedefine.TypeDefine import CTX_CONFIG_PATH, APP_VERSION
from yomk.server import YomkService
from yomk.ctx import ctx
from yomk.log import log
from yomk.msg.server import ResStatus, Response


class ConfigService(YomkService):
    def __init__(self, server):
        super().__init__(server)
        self.set_name("/ConfigService")
        self.config_path = ""
        self.config_json = {}

    def init(self):
        self.install_func("/load", self.load_config)
        self.install_func("/get", self.get_config)
        self.install_func("/set", self.set_config)
        self.install_func("/reload", self.reload_config)
        self.install_func("/version", self.version)
        log().tag("ConfigService::init").info("install func [ /load /get /set /reload /version ] to ", self.get_name())

    def load_config(self, pkg):
        self.config_path = ctx().key(CTX_CONFIG_PATH).msg().with_default("").get()
        if not self.config_path:
            return Response(ResStatus.eNo, "config_path not found in context")
        try:
            with open(self.config_path, 'r', encoding='utf-8') as f:
                self.config_json = json.load(f)
        except Exception as e:
            return Response(ResStatus.eNo, f"failed to open: {self.config_path}, {e}")
        log().tag("ConfigService::load_config").info("loaded: ", self.config_path)
        return Response(ResStatus.eOk, "ok")

    def get_config(self, pkg):
        tokens = pkg.key.split('.')
        current = self.config_json
        for token in tokens:
            if not isinstance(current, dict) or token not in current:
                return Response(ResStatus.eNo, "key not found: " + pkg.key)
            current = current[token]

        if isinstance(current, str):
            value = current
        else:
            value = json.dumps(current, ensure_ascii=False)
        return Response(ResStatus.eOk, "ok", value)

    def set_config(self, pkg):
        tokens = pkg.key.split('.')
        current = self.config_json
        for token in tokens[:-1]:
            if not isinstance(current, dict):
                return Response(ResStatus.eNo, "invalid key path: " + pkg.key)
            current = current.setdefault(token, {})
        current[tokens[-1]] = pkg.value
        log().tag("ConfigService::set_config").info("set ", pkg.key, " = ", pkg.value)
        return Response(ResStatus.eOk, "ok")

    def reload_config(self, pkg):
        # reload 先回取 context 中的 config_path，缺失即 eNo
        self.config_path = ctx().key(CTX_CONFIG_PATH).msg().with_default("").get()
        if not self.config_path:
            return Response(ResStatus.eNo, "config_path not found in context")
        try:
            with open(self.config_path, 'r', encoding='utf-8') as f:
                self.config_json = json.load(f)
        except Exception as e:
            return Response(ResStatus.eNo, f"failed to reload: {self.config_path}, {e}")
        log().tag("ConfigService::reload_config").info("reloaded: ", self.config_path)
        return Response(ResStatus.eOk, "ok")

    def version(self, pkg):
        # 版本号来自 typedefine/TypeDefine.py 定义的 APP_VERSION 常量
        return Response(ResStatus.eOk, "ok", APP_VERSION)
