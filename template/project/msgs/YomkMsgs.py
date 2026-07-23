# ConfigService 消息包
class ConfigKey:
    def __init__(self, key: str):
        self.key = key

class ConfigKeyValue:
    def __init__(self, key: str, value: str):
        self.key = key
        self.value = value
