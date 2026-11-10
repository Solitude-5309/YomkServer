__all__ = [
    "CTX_ALL",
    "CTX_CREATE",
    "CTX_DESTROY",
    "CTX_GET",
    "CTX_GROUPS",
    "CTX_KEY",
    "CTX_KEYS",
    "CTX_SET",
    "CTX_SET_CHECKER",
    "CTX_SET_MONITOR",
    "CTX_TURN_OFF_CHECKER",
    "CTX_TURN_OFF_MONITOR",
    "CTX_TURN_ON_CHECKER",
    "CTX_TURN_ON_MONITOR",
]
# 与 C++ YomkContextUrl.h 逐字一致（ctxCreate 等 camelCase → CTX_* 大写蛇形）
CTX_CREATE = "/YomkContext/create"
CTX_GET = "/YomkContext/get"
CTX_SET = "/YomkContext/set"
CTX_TURN_ON_CHECKER = "/YomkContext/turn_on_checker"
CTX_TURN_OFF_CHECKER = "/YomkContext/turn_off_checker"
CTX_SET_CHECKER = "/YomkContext/set_checker"
CTX_TURN_ON_MONITOR = "/YomkContext/turn_on_monitor"
CTX_TURN_OFF_MONITOR = "/YomkContext/turn_off_monitor"
CTX_SET_MONITOR = "/YomkContext/set_monitor"
CTX_DESTROY = "/YomkContext/destroy"
CTX_KEYS = "/YomkContext/keys"
CTX_KEY = "/YomkContext/key"
CTX_GROUPS = "/YomkContext/groups"
CTX_ALL = "/YomkContext/all"
