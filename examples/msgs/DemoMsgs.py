"""
消息包定义：服务间传输的数据结构
"""


class EchoMsg:
    """回显消息：原样传输的演示载荷"""

    def __init__(self, msg: str):
        self.msg = msg


class AddReq:
    """加法请求：两个加数"""

    def __init__(self, a: int, b: int):
        self.a = a
        self.b = b


class AddResp:
    """加法回包：计算结果（以强类型数据放入 YomkResponse.data）"""

    def __init__(self, result: int):
        self.result = result
