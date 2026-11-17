from enum import Enum
from typing import Any


class ResStatus(Enum):
    eInvalid = -1
    eOk = 0
    eNo = 1
    # 命名口径别名（Invalid=-1/Succeeded=0/Failed=1），
    # 与 eInvalid/eOk/eNo 逐一同值；新代码一律用命名口径，eOk/eNo 保留向后兼容
    Succeeded = 0
    Failed = 1
    Invalid = -1


class Response:
    status: ResStatus   # 本次请求的状态
    msg: str            # 对状态的说明（不携带返回数据）
    data: Any           # 携带的返回数据

    def __init__(
        self,
        status: ResStatus = ResStatus.eInvalid,
        msg: str = "",
        data: Any = None,
    ) -> None: ...