# /YomkFunctionPool/* 内置服务 URL 常量（对齐 C++ include/yomk/fn_pool/YomkFunctionPoolUrl.h）。
# yomk.url 为纯命名空间层（与 yomk.msg 同构，无 __init__）：用户新增模块安装到 yomk 体系下时，
# 只需新增自己的命名空间子包并自带 __init__.py 即可并入本常量池。
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
