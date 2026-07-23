import YomkApi
import json
from typedefine.TypeDefine import CTX_CONFIG_PATH

class ConfigService(YomkApi.YomkService):
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
        print(f"ConfigService::init install func [ /load /get /set /reload ] to {self.get_name()}")

    def load_config(self, pkg):
        self.config_path = YomkApi.context_get(CTX_CONFIG_PATH, "")
        if not self.config_path:
            return YomkApi.YomkResponse(YomkApi.ResStatus.eNo, "config_path not found in context")
        try:
            with open(self.config_path, 'r', encoding='utf-8') as f:
                self.config_json = json.load(f)
        except Exception as e:
            return YomkApi.YomkResponse(YomkApi.ResStatus.eNo, f"failed to open: {self.config_path}, {e}")
        print(f"ConfigService::load_config loaded: {self.config_path}")
        return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "ok")

    def get_config(self, pkg):
        tokens = pkg.key.split('.')
        current = self.config_json
        for token in tokens:
            if not isinstance(current, dict) or token not in current:
                return YomkApi.YomkResponse(YomkApi.ResStatus.eNo, "key not found: " + pkg.key)
            current = current[token]

        if isinstance(current, str):
            value = current
        else:
            value = json.dumps(current, ensure_ascii=False)
        return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "ok", value)

    def set_config(self, pkg):
        tokens = pkg.key.split('.')
        current = self.config_json
        for token in tokens[:-1]:
            if not isinstance(current, dict):
                return YomkApi.YomkResponse(YomkApi.ResStatus.eNo, "invalid key path: " + pkg.key)
            current = current.setdefault(token, {})
        current[tokens[-1]] = pkg.value
        print(f"ConfigService::set_config set {pkg.key} = {pkg.value}")
        return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "ok")

    def reload_config(self, pkg):
        try:
            with open(self.config_path, 'r', encoding='utf-8') as f:
                self.config_json = json.load(f)
        except Exception as e:
            return YomkApi.YomkResponse(YomkApi.ResStatus.eNo, f"failed to reload: {self.config_path}, {e}")
        print(f"ConfigService::reload_config reloaded: {self.config_path}")
        return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "ok")
