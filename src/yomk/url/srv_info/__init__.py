# /YomkServerInfo/* 内置服务 URL 常量（对齐 C++ include/yomk/srv_info/YomkServerInfoUrl.h）。
# yomk.url 为纯命名空间层（与 yomk.msg 同构，无 __init__）：用户新增模块安装到 yomk 体系下时，
# 只需新增自己的命名空间子包并自带 __init__.py 即可并入本常量池。
__all__ = ["SRV_ALL", "SRV_FUNCTION", "SRV_FUNCTIONS", "SRV_SERVICES"]

SRV_SERVICES = "/YomkServerInfo/services"
SRV_FUNCTIONS = "/YomkServerInfo/functions"
SRV_FUNCTION = "/YomkServerInfo/function"
SRV_ALL = "/YomkServerInfo/all"
