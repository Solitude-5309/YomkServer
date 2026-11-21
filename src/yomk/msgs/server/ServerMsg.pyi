from enum import Enum
from typing import Any
from yomk.msgs.YomkMsg import YomkMsg


class ResStatus(Enum):
    eInvalid = -1
    eOk = 0
    eNo = 1
    Succeeded = 0
    Failed = 1
    Invalid = -1


class Response(YomkMsg):
    status: ResStatus
    msg: str
    data: Any

    def __init__(
        self,
        status: ResStatus = ResStatus.eInvalid,
        msg: str = "",
        data: Any = None,
    ) -> None: ...
