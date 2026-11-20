# ConfigService 消息包（标准形态：继承 YomkMsg 免费得 kName——运行期类型名
# 自动生成、随类重命名跟随，禁止手写；unpack 精确比对 / install_func 声明 /
# 错误文案统一消费它，无需任何手写报文名）

from yomk.msgs import YomkMsg


class ConfigKey(YomkMsg):
    def __init__(self, key: str):
        self.key = key


class ConfigKeyValue(YomkMsg):
    def __init__(self, key: str, value: str):
        self.key = key
        self.value = value
