class FuncInfo:
    # 功能函数元信息（/YomkFunctionPool/name 与 /all、/YomkServerInfo/function
    # 单查与 /all 清单段返回数据），预留扩展（如安全性校验）
    func_name: str
    msg_name: str
    description: str

    def __init__(
        self,
        func_name: str = "",
        msg_name: str = "",
        description: str = "",
    ) -> None: ...