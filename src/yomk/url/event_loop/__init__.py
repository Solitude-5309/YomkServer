__all__ = [
    "LOOP_ALL",
    "LOOP_DESTROY",
    "LOOP_LOOP",
    "LOOP_LOOPS",
    "LOOP_POST",
    "LOOP_POST_WAIT",
    "LOOP_START",
    "LOOP_STOP",
]
# 与 C++ YomkEventLoopUrl.h 逐字一致（loopStart 等 camelCase → LOOP_* 大写蛇形）
LOOP_START = "/YomkEventLoop/start"
LOOP_STOP = "/YomkEventLoop/stop"
LOOP_POST = "/YomkEventLoop/post"
LOOP_POST_WAIT = "/YomkEventLoop/post_wait"
LOOP_DESTROY = "/YomkEventLoop/destroy"
LOOP_LOOPS = "/YomkEventLoop/loops"
LOOP_LOOP = "/YomkEventLoop/loop"
LOOP_ALL = "/YomkEventLoop/all"
