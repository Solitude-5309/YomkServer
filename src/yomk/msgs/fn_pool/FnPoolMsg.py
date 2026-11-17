class FuncInfo:
    # 功能函数元信息（/YomkFunctionPool/name 与 /all、/YomkServerInfo/function
    # 单查与 /all 清单段返回数据），预留扩展（如安全性校验）
    def __init__(self, func_name="", msg_name="", description=""):
        self.func_name = func_name
        self.msg_name = msg_name
        self.description = description


class Function:
    def __init__(self, name, func, msg_name="", description=""):
        self.name = name
        self.func = func
        self.msg_name = msg_name
        # 函数说明，内容使用者自定义，与 msg_name 同为仅内省元数据
        self.description = description


class CallFunction:
    def __init__(self, name, pkg):
        self.name = name
        self.pkg = pkg