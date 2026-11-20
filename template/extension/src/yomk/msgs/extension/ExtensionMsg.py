# 扩展消息类型定义（标准形态：继承 YomkMsg 免费得 kName——运行期类型名
# 自动生成、随类重命名跟随，禁止手写；unpack 精确比对 / install_func 声明
# / 错误文案统一消费它，无需任何手写报文名）

from yomk.msgs import YomkMsg


class ExtensionRequest(YomkMsg):
    """扩展请求包：payload 为调用方自定义数据（裸 str / 结构化对象均可）"""

    def __init__(self, payload):
        self.payload = payload

    def __repr__(self):
        return f"ExtensionRequest(payload={self.payload!r})"


class ExtensionResponse(YomkMsg):
    """扩展应答包：作为 Response.data 承载强类型回包（强类型回包形态）"""

    def __init__(self, status, msg, data=None):
        self.status = status
        self.msg = msg
        self.data = data

    def __repr__(self):
        return f"ExtensionResponse(status={self.status}, msg={self.msg!r}, data={self.data!r})"
