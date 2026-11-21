"""Built-in URL constants of the /YomkFunctionPool service.

/YomkFunctionPool 内置服务 URL 常量。新增域时建立自己的命名空间子包并导出常量即可。
"""

__all__ = [
    "FP_ALL",
    "FP_CALL",
    "FP_NAME",
    "FP_NAMES",
    "FP_REGISTER",
    "FP_UNREGISTER",
]

FP_REGISTER = "/YomkFunctionPool/register"
FP_UNREGISTER = "/YomkFunctionPool/unregister"
FP_CALL = "/YomkFunctionPool/call"
FP_NAMES = "/YomkFunctionPool/names"
FP_NAME = "/YomkFunctionPool/name"
FP_ALL = "/YomkFunctionPool/all"
