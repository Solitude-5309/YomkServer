"""Built-in URL constants of the /YomkServerInfo service.

/YomkServerInfo 内置服务 URL 常量。新增域时建立自己的命名空间子包并导出常量即可。
"""

__all__ = ["SRV_ALL", "SRV_FUNCTION", "SRV_FUNCTIONS", "SRV_SERVICES"]

SRV_SERVICES = "/YomkServerInfo/services"
SRV_FUNCTIONS = "/YomkServerInfo/functions"
SRV_FUNCTION = "/YomkServerInfo/function"
SRV_ALL = "/YomkServerInfo/all"
