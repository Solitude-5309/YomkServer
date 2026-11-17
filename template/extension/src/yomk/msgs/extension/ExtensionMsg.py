# 扩展消息类型定义（与框架 msg 子包同风格：结构化数据，框架按消息包/持有语义传递）


class ExtensionRequest:
    """扩展请求包：payload 为调用方自定义数据（裸 str / 结构化对象均可）"""

    def __init__(self, payload):
        self.payload = payload

    def __repr__(self):
        return f"ExtensionRequest(payload={self.payload!r})"


class ExtensionResponse:
    """扩展应答包：作为 Response.data 承载强类型回包（强类型回包形态）"""

    def __init__(self, status, msg, data=None):
        self.status = status
        self.msg = msg
        self.data = data

    def __repr__(self):
        return f"ExtensionResponse(status={self.status}, msg={self.msg!r}, data={self.data!r})"
