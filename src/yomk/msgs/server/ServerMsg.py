"""Server domain payloads: result status and response.

服务器域消息：结果三态与请求响应。
"""

from enum import Enum
from typing import Any
from yomk.msgs.YomkMsg import YomkMsg


class ResStatus(Enum):
    """Request result status.

    请求结果三态：Invalid=-1 / Succeeded=0 / Failed=1。
    """

    eInvalid = -1
    eOk = 0
    eNo = 1
    # Succeeded/Failed/Invalid 与 eOk/eNo/eInvalid 同值；新代码用命名口径
    Succeeded = 0
    Failed = 1
    Invalid = -1


class Response(YomkMsg):
    """Response payload: status, message and optional data.

    请求响应载荷：状态、说明与数据。
    """

    def __init__(
        self,
        status: ResStatus = ResStatus.eInvalid,
        msg: str = "",
        data: Any = None,
    ):
        self.status = status  # 本次请求的状态
        self.msg = msg        # 状态说明，不携带返回数据
        self.data = data      # 携带的返回数据