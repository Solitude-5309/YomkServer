from readerwriterlock import rwlock

from yomk.fn_pool._diag import error as diag_error
from yomk.fn_pool._diag import info as diag_info
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

    def registerFunction(self, function: Function):
        if not isinstance(function, Function):
            raise TypeError("function must be Function")

        # 合一校验：空名或空函数体均视为参数无效；
        # eNo 为 not-found/业务拒绝框架惯例，eInvalid 仅用于参数无效
        if not function.name or not function.func:
            diag_error(
                "funcName or func is empty, please check Function.m_funcName")
            return Response(ResStatus.eInvalid, "funcName or func is empty")

        with self.m_functionsMutex.gen_wlock():
            if function.name in self.m_functions:
                # 重复名=更新语义：整对象替换覆盖 func、msg_name、description 三字段
                # （未在替换对象上声明的元数据即被清除），先更新再返回显式区分的文案
                diag_info(
                    f"function name: {function.name} is already exist, update to current function")
                self.m_functions[function.name] = function
                return Response(
                    ResStatus.eOk,
                    "find function name is already exist, update to current function")
            self.m_functions[function.name] = function

        return Response(ResStatus.eOk, "register function success")

    def unRegisterFunction(self, func_name: str):
        # 注销已注册函数：参数无效 eInvalid、
        # 未注册 eNo（not-found 框架惯例；eInvalid 仅用于参数无效）
        if not func_name:
            diag_error("funcName is empty, please check String.")
            return Response(ResStatus.eInvalid, "funcName is empty")

        with self.m_functionsMutex.gen_wlock():
            if func_name not in self.m_functions:
                diag_error(
                    f"funcName: {func_name} is not register, can not unregister")
                return Response(ResStatus.eNo, "funcName is not register")
            del self.m_functions[func_name]

        return Response(ResStatus.eOk, "unregister function success")

    def callFunction(self, call_func: CallFunction):
        if not isinstance(call_func, CallFunction):
            raise TypeError("call_func must be CallFunction")

        if not call_func.name:
            diag_error("funcName is empty, please check CallFunction.m_funcName.")
            return Response(ResStatus.eInvalid, "funcName is empty")

        with self.m_functionsMutex.gen_rlock():
            if call_func.name not in self.m_functions:
                diag_error(
                    f"funcName: {call_func.name} is not register, please check CallFunction.m_funcName.")
                return Response(ResStatus.eNo, "funcName is not register")
            else:
                func = self.m_functions[call_func.name].func

        return func(call_func.pkg)

    def funcNames(self, pkg):
        # 内省：列出全部注册函数名（读锁内排序，:map 字典序）
        with self.m_functionsMutex.gen_rlock():
            names = sorted(self.m_functions)
        return Response(ResStatus.eOk, "ok", names)

    def funcInfo(self, func_name: str):
        # 内省：单个函数元信息查询，命中返回结构化 FuncInfo 包（msg 恒 "ok"，数据走 data）
        if not func_name:
            diag_error("funcName is empty, please check String.")
            return Response(ResStatus.eInvalid, "funcName is empty")

        with self.m_functionsMutex.gen_rlock():
            entry = self.m_functions.get(func_name)
            if entry is None:
                diag_error(
                    f"funcName: {func_name} is not register, please check funcName")
                return Response(ResStatus.eNo, "funcName is not register")
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