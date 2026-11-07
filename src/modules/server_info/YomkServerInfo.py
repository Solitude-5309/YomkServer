# 源片段：src/modules/server_info/YomkServerInfo.py（构建期由 scripts/assemble_py.py 合并，请勿单独导入）



class ServerServiceInfo:
    # 服务级内省条目（/YomkServerInfo/all 返回数据元素：服务名 + 该服务注册的函数元信息列表）
    def __init__(self, service_name="", funcs=None):
        self.service_name = service_name   # 服务名（带 / 前缀，如 "/FmtSrv"）
        # 函数元信息（与 /function 单查同族 FuncInfo，func_name 为带 / 前缀 url 段）
        self.funcs = funcs if funcs is not None else []


class YomkServerInfo(YomkService):
    # 服务表内省服务：四端点 /services /functions /function /all；
    # 经 weakref 判活（server 销毁后四端点均返 eNo）
    def __init__(self, server: "YomkServer"):
        super().__init__(server)
        # 清除基类对 server 的强引用，仅保留弱引用：
        # 否则本实例存活会连带 server 永不销毁，weakref 判活契约失效
        self.server = None
        self.m_weakServer = weakref.ref(server) if server else None
        self.set_name("/YomkServerInfo")

    def init(self):
        self.install_func("/services", self.list_services)
        self.install_func("/functions", self.list_functions, "String")
        self.install_func("/function", self.function_info, "String")
        self.install_func("/all", self.list_all)
        return 0

    def _locked_server(self):
        # server 已销毁时返回 None，由各端点统一走 destroyed 契约
        return self.m_weakServer() if self.m_weakServer else None

    def list_services(self, pkg):
        server = self._locked_server()
        if server is None:
            log.error("server has been destroyed, list services ignored.")
            return YomkResponse(ResStatus.eNo, "server has been destroyed")
        # :map）：服务名字典序输出
        return YomkResponse(ResStatus.eOk, "ok", sorted(server.service_names()))

    def list_functions(self, pkg):
        if pkg is None:
            log.error("pkg is null, please check String.d")
            return YomkResponse(ResStatus.eNo, "pkg is null")
        srv_name = pkg
        if not srv_name:
            log.error("srvName is empty, please check String.d")
            return YomkResponse(ResStatus.eInvalid, "srvName is empty")

        server = self._locked_server()
        if server is None:
            log.error("server has been destroyed, list functions ignored.")
            return YomkResponse(ResStatus.eNo, "server has been destroyed")

        infos = server.service_func_infos(srv_name)
        # 结构化清单：FuncInfo 三字段与 /function 单查、/all 聚合同族
        # （func_name 为带 / 前缀 url 段）；显式 sorted :map 字典序（python dict
        # 为插入序，类头注释先例）；不存在的服务返回空表（eOk）
        funcs = [infos[name] for name in sorted(infos.keys())]
        return YomkResponse(ResStatus.eOk, "ok", funcs)

    def function_info(self, pkg):
        if pkg is None:
            log.error("pkg is null, please check String.d")
            return YomkResponse(ResStatus.eNo, "pkg is null")
        url = pkg
        if not url or url[0] != "/":
            log.error("url parse error: " + url + ", please start with /")
            return YomkResponse(ResStatus.eNo,
                                "url parse error: " + url + ", please start with /")

        pos_end = url.find("/", 1)
        if pos_end == -1:
            log.error("url parse error: " + url + ", not found function name.")
            return YomkResponse(ResStatus.eNo,
                                "url parse error: " + url + ", not found function name.")

        srv_name = url[:pos_end]
        func_name = url[pos_end:]

        server = self._locked_server()
        if server is None:
            log.error("server has been destroyed, function info ignored.")
            return YomkResponse(ResStatus.eNo, "server has been destroyed")

        # 命中：结构化包恒挂，明确字段承载全部返回数据（msg 恒 "ok"，数据走 data）；
        # 复用 FuncInfo 与函数池 /name 单查端点同族
        for name in sorted(server.service_names()):
            if name == srv_name:
                info = server.service_func_infos(srv_name).get(func_name)
                if info is None:
                    log.error("function not found -> " + url)
                    return YomkResponse(ResStatus.eNo, "function not found: " + url)
                return YomkResponse(ResStatus.eOk, "ok", info)

        log.error("service not found -> " + srv_name)
        return YomkResponse(ResStatus.eNo, "service not found: " + srv_name)

    def list_all(self, pkg):
        server = self._locked_server()
        if server is None:
            log.error("server has been destroyed, list all ignored.")
            return YomkResponse(ResStatus.eNo, "server has been destroyed")

        # 同现实现取数序：service_names() 一次，再逐服务 service_func_infos()；
        # FuncInfo 三字段与 /function 单查同族整体复用（func_name 为带 / 前缀 url 段）
        infos = []
        for srv_name in sorted(server.service_names()):
            func_infos = server.service_func_infos(srv_name)
            infos.append(ServerServiceInfo(
                srv_name, [func_infos[name] for name in sorted(func_infos.keys())]))
        # msg 恒 "ok"，数据走 data（结构化层级列表：服务 → 函数元信息）
        return YomkResponse(ResStatus.eOk, "ok", infos)
