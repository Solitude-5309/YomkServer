import weakref
from typing import TYPE_CHECKING

from yomk.msgs.server import ResStatus, Response
from yomk.msgs.srv_info import ServerServiceInfo, ServerServiceInfoList
from yomk.msgs.std_msg import String, StringArray, type_mismatch
from yomk.msgs.fn_pool import FuncInfoList
from yomk.msgs.YomkMsg import unpack
from yomk.server import YomkService
from yomk._diag import error as diag_error
from yomk._diag import warn as diag_warn

if TYPE_CHECKING:
    from yomk.server import YomkServer


class YomkServerInfo(YomkService):
    # 服务表内省服务：四端点 /services /functions /function /all；
    # 经 weakref 判活（server 销毁后四端点均返 eNo）。
    # 诊断契约：WARN = 空参/url 解析失败/服务未命中，ERROR = server 已销毁；
    # 失败路径不再重复打诊断（Response.msg 已携带同信息），pkg 类型错配无诊断。
    def __init__(self, server: "YomkServer"):
        super().__init__(server)
        # 清除基类对 server 的强引用，仅保留弱引用：
        # 否则本实例存活会连带 server 永不销毁，weakref 判活契约失效
        self.server = None
        self.m_weakServer = weakref.ref(server) if server else None
        self.set_name("/YomkServerInfo")

    def init(self):
        self.install_func("/services", self.list_services)
        self.install_func("/functions", self.list_functions, String.kName)
        self.install_func("/function", self.function_info, String.kName)
        self.install_func("/all", self.list_all)
        return 0

    def _locked_server(self):
        # server 已销毁时返回 None，由各端点统一走 destroyed 契约
        return self.m_weakServer() if self.m_weakServer else None

    def list_services(self, pkg):
        server = self._locked_server()
        if server is None:
            diag_error("server has been destroyed, list services ignored")
            return Response(ResStatus.eNo, "server has been destroyed")
        # 服务名按字典序输出
        return Response(ResStatus.eOk, "ok",
                        StringArray(sorted(server.service_names())))

    def list_functions(self, pkg):
        if unpack(pkg, String) is None:
            return Response(ResStatus.eNo, type_mismatch(pkg, String.kName))
        srv_name = pkg.value
        if not srv_name:
            # 空参误用：WARN 后返 eInvalid
            diag_warn("srvName is empty, please check String.d")
            return Response(ResStatus.eInvalid, "srvName is empty")

        server = self._locked_server()
        if server is None:
            diag_error("server has been destroyed, list functions ignored")
            return Response(ResStatus.eNo, "server has been destroyed")

        infos = server.service_func_infos(srv_name)
        # 结构化清单：FuncInfo 三字段与 /function 单查、/all 聚合同族
        # （func_name 为带 / 前缀 url 段）；显式 sorted 输出；服务不存在时返回空表（eOk）
        funcs = [infos[name] for name in sorted(infos.keys())]
        return Response(ResStatus.eOk, "ok", FuncInfoList(funcs))

    def function_info(self, pkg):
        if unpack(pkg, String) is None:
            return Response(ResStatus.eNo, type_mismatch(pkg, String.kName))
        url = pkg.value
        if not url or url[0] != "/":
            # url 解析失败：WARN + 同串 Response
            diag_warn("url parse error: " + url + ", please start with /")
            return Response(ResStatus.eNo,
                            "url parse error: " + url + ", please start with /")

        pos_end = url.find("/", 1)
        if pos_end == -1:
            diag_warn("url parse error: " + url + ", no function name")
            return Response(ResStatus.eNo,
                            "url parse error: " + url + ", no function name")

        srv_name = url[:pos_end]
        func_name = url[pos_end:]

        server = self._locked_server()
        if server is None:
            diag_error("server has been destroyed, function info ignored")
            return Response(ResStatus.eNo, "server has been destroyed")

        # 命中：结构化包恒挂，数据全部走 data（msg 恒 "ok"）；
        # 复用 FuncInfo，与函数池 /name 单查端点同族
        for name in sorted(server.service_names()):
            if name == srv_name:
                info = server.service_func_infos(srv_name).get(func_name)
                if info is None:
                    # 函数未命中无诊断（Response.msg 已携带同信息）
                    return Response(ResStatus.eNo, "function not found: " + url)
                return Response(ResStatus.eOk, "ok", info)

        # 服务未命中：WARN + 同串 Response
        diag_warn("service not found: " + srv_name)
        return Response(ResStatus.eNo, "service not found: " + srv_name)

    def list_all(self, pkg):
        server = self._locked_server()
        if server is None:
            diag_error("server has been destroyed, list all ignored")
            return Response(ResStatus.eNo, "server has been destroyed")

        # 同现实现取数序：service_names() 一次，再逐服务 service_func_infos()；
        # FuncInfo 三字段与 /function 单查同族整体复用（func_name 为带 / 前缀 url 段）
        infos = []
        for srv_name in sorted(server.service_names()):
            func_infos = server.service_func_infos(srv_name)
            infos.append(ServerServiceInfo(
                srv_name, [func_infos[name] for name in sorted(func_infos.keys())]))
        # msg 恒 "ok"，数据走 data（结构化层级列表：服务 → 函数元信息）
        return Response(ResStatus.eOk, "ok", ServerServiceInfoList(infos))
