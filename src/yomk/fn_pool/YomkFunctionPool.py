from readerwriterlock import rwlock

from yomk._diag import warn as diag_warn
from yomk.msg.fn_pool import CallFunction, FuncInfo, Function
from yomk.msg.server import ResStatus, Response
from yomk.server import YomkService


class YomkFunctionPool(YomkService):
    def __init__(self, server):
        super().__init__(server)
        self.set_name("/YomkFunctionPool")
        self.m_functions = {}     # {name: Function}（func+msg_name 同存）
        self.m_functionsMutex = rwlock.RWLockFair()

    def init(self):
        # URL 装配顺序/register /unregister /call /names /name /all
        self.install_func("/register", self.registerFunction, "Function")
        self.install_func("/unregister", self.unRegisterFunction, "String")
        self.install_func("/call", self.callFunction, "CallFunction")
        # 调试内省接口，端点挂在本服务 funcMap
        self.install_func("/names", self.funcNames)
        self.install_func("/name", self.funcInfo, "String")
        self.install_func("/all", self.listAll)

    def registerFunction(self, function):
        # 载荷类型错配：Failed + 携带实际类型（对齐 C++ unpack 失败路径，零 diag）。
        # 端点载荷形参一律不带类型注解：Cython 会把注解编译为边界 TypeError，
        # 使下方运行时守卫失效（pyi 同理标注 Any）
        if not isinstance(function, Function):
            return Response(
                ResStatus.eNo,
                "pkg is not Function, got '" + type(function).__name__ + "'")

        # 合一校验：空名或空函数体均视为参数无效；
        # eNo 为 not-found/业务拒绝框架惯例，eInvalid 仅用于参数无效
        if not function.name or not function.func:
            return Response(ResStatus.eInvalid, "funcName or func is empty")

        with self.m_functionsMutex.gen_wlock():
            if function.name in self.m_functions:
                # 重复名=热替换语义：整对象替换覆盖 func、msg_name、description 三字段
                # （未在替换对象上声明的元数据即被清除），diag 与 Response.msg 同串；
                # C-5 已采纳：覆盖既有注册，级别 WARN（误覆盖默认可见，Response.msg 通道不变）
                msg = ("function '" + function.name +
                       "' already exists, hot-swap to current function")
                diag_warn(msg)
                self.m_functions[function.name] = function
                return Response(ResStatus.eOk, msg)
            self.m_functions[function.name] = function

        return Response(ResStatus.eOk, "register function success")

    def unRegisterFunction(self, func_name):
        # 注销已注册函数：参数无效 eInvalid、
        # 未注册 eNo（not-found 框架惯例；eInvalid 仅用于参数无效）
        if not isinstance(func_name, str):
            return Response(
                ResStatus.eNo,
                "pkg is not String, got '" + type(func_name).__name__ + "'")
        if not func_name:
            return Response(ResStatus.eInvalid, "funcName is empty")

        with self.m_functionsMutex.gen_wlock():
            if func_name not in self.m_functions:
                # 名字有效但未注册:ResStatus::Failed(与 funcInfo/Context/EventLoop 的
                # not-found 惯例一致;ResStatus::Invalid 仅用于参数无效)
                return Response(
                    ResStatus.eNo,
                    "function '" + func_name + "' is not registered")
            del self.m_functions[func_name]

        return Response(ResStatus.eOk, "unregister function success")

    def callFunction(self, call_func):
        if not isinstance(call_func, CallFunction):
            return Response(
                ResStatus.eNo,
                "pkg is not CallFunction, got '" + type(call_func).__name__ + "'")

        if not call_func.name:
            return Response(ResStatus.eInvalid, "funcName is empty")

        with self.m_functionsMutex.gen_rlock():
            entry = self.m_functions.get(call_func.name)
            if entry is None:
                # 未注册同 unregister:ResStatus::Failed(not-found 框架惯例)
                return Response(
                    ResStatus.eNo,
                    "function '" + call_func.name + "' is not registered")
            func = entry.func

        return func(call_func.pkg)

    def funcNames(self, pkg):
        # 内省：列出全部注册函数名（读锁内排序，:map 字典序）
        with self.m_functionsMutex.gen_rlock():
            names = sorted(self.m_functions)
        return Response(ResStatus.eOk, "ok", names)

    def funcInfo(self, func_name):
        # 内省：单个函数元信息查询，命中返回结构化 FuncInfo 包（msg 恒 "ok"，数据走 data）
        if not isinstance(func_name, str):
            return Response(
                ResStatus.eNo,
                "pkg is not String, got '" + type(func_name).__name__ + "'")
        if not func_name:
            return Response(ResStatus.eInvalid, "funcName is empty")

        with self.m_functionsMutex.gen_rlock():
            entry = self.m_functions.get(func_name)
            if entry is None:
                return Response(
                    ResStatus.eNo,
                    "function '" + func_name + "' is not registered")
            # 命中：结构化包恒挂，明确字段承载全部返回数据（msg 恒 "ok"，数据走 data）
            info = FuncInfo(func_name, entry.msg_name, entry.description)

        return Response(ResStatus.eOk, "ok", info)

    def listAll(self, pkg):
        # 内省：全量函数元信息，读锁内构造结构化快照列表
        # （msg 恒 "ok"，数据走 data；函数总数由列表长度承载）
        with self.m_functionsMutex.gen_rlock():
            infos = [FuncInfo(name, entry.msg_name, entry.description)
                     for name, entry in sorted(self.m_functions.items())]
        return Response(ResStatus.eOk, "ok", infos)
