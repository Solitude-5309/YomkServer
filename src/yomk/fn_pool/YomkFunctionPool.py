from readerwriterlock import rwlock

from yomk._diag import warn as diag_warn
from yomk.msgs.fn_pool import CallFunction, FuncInfo, FuncInfoList, Function
from yomk.msgs.server import ResStatus, Response
from yomk.msgs.std_msg import String, StringArray
from yomk.msgs.YomkMsg import type_mismatch, unpack
from yomk.server import YomkService
from yomk.url.fn_pool import (
    FP_ALL_DESC,
    FP_CALL_DESC,
    FP_NAME_DESC,
    FP_NAMES_DESC,
    FP_REGISTER_DESC,
    FP_UNREGISTER_DESC,
)


class YomkFunctionPool(YomkService):
    def __init__(self, server):
        super().__init__(server)
        self.set_name("/YomkFunctionPool")
        self.m_functions = {}     # {name: Function}（func+msg_name 同存）
        self.m_functionsMutex = rwlock.RWLockFair()

    def init(self):
        # URL 装配顺序/register /unregister /call /names /name /all
        self.install_func("/register", self.registerFunction, Function.TYPE_NAME,
                          FP_REGISTER_DESC)
        self.install_func("/unregister", self.unRegisterFunction, String.TYPE_NAME,
                          FP_UNREGISTER_DESC)
        self.install_func("/call", self.callFunction, CallFunction.TYPE_NAME,
                          FP_CALL_DESC)
        # 调试内省接口，端点挂在本服务 funcMap
        self.install_func("/names", self.funcNames, "", FP_NAMES_DESC)
        self.install_func("/name", self.funcInfo, String.TYPE_NAME, FP_NAME_DESC)
        self.install_func("/all", self.listAll, "", FP_ALL_DESC)

    def registerFunction(self, function):
        # 端点载荷形参一律不带类型注解：Cython 会把注解编译为边界 TypeError，
        # 使下方运行时守卫失效（pyi 同理标注 Any）
        if unpack(function, Function) is None:
            return Response(ResStatus.eNo,
                            type_mismatch(function, Function.TYPE_NAME))

        # 合一校验：空名或空函数体均视为参数无效；
        # eNo 为 not-found/业务拒绝框架惯例，eInvalid 仅用于参数无效
        if not function.name or not function.func:
            return Response(ResStatus.eInvalid, "funcName or func is empty")

        with self.m_functionsMutex.gen_wlock():
            if function.name in self.m_functions:
                # 重复名=热替换语义：整对象替换覆盖 func、msg_name、description
                # 三字段（未在替换对象上声明的元数据即被清除）；WARN 可见 + 同串
                # Response.msg，误覆盖默认不静默。
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
        if unpack(func_name, String) is None:
            return Response(ResStatus.eNo,
                            type_mismatch(func_name, String.TYPE_NAME))
        func_name = func_name.value
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
        if unpack(call_func, CallFunction) is None:
            return Response(ResStatus.eNo,
                            type_mismatch(call_func, CallFunction.TYPE_NAME))

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
        return Response(ResStatus.eOk, "ok", StringArray(names))

    def funcInfo(self, func_name):
        # 内省：单个函数元信息查询，命中返回结构化 FuncInfo 包（msg 恒 "ok"，数据走 data）
        if unpack(func_name, String) is None:
            return Response(ResStatus.eNo,
                            type_mismatch(func_name, String.TYPE_NAME))
        func_name = func_name.value
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
        return Response(ResStatus.eOk, "ok", FuncInfoList(infos))
