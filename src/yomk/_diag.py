import sys
import time

# diag 级别常量（对齐 C++ YOMK_DIAG_LEVEL_*，值一致：YomkDiag.h:71-76）
TRACE = 0
DEBUG = 1
INFO = 2
WARN = 3
ERROR = 4
FATAL = 5

_LEVEL_LABELS = {
    TRACE: "TRACE",
    DEBUG: "DEBUG",
    INFO: "INFO ",
    WARN: "WARN ",
    ERROR: "ERROR",
    FATAL: "FATAL",
}

# 进程级最小输出级别（对齐 C++ yomkDiagSetMinLevel，默认 WARN：YomkDiag.h:81-96）
_min_level = WARN


def set_min_level(level: int) -> None:
    # 设置进程级 diag 最小输出级别，低于阈值的消息整条丢弃；
    # 任意线程随时可调（int 赋值在 GIL 下原子，对齐 C++ atomic<int> 语义）
    global _min_level
    _min_level = level


def enabled(level: int) -> bool:
    # 阈值查询：级别 >= 当前最小级别时才会输出（热路径自查用）
    return level >= _min_level


def _timestamp() -> str:
    now = time.time()
    millis = int((now - int(now)) * 1000)
    return time.strftime("[%Y-%m-%d %H:%M:%S", time.localtime(now)) + f".{millis:03d}]"


def _emit(level: int, msg: str) -> None:
    # 门控：低于最小级别的消息整条丢弃。
    # 语言差异说明：C++ 宏在阈值判断后才求值消息表达式（抑制时零字符串构造开销）；
    # Python 调用点已构造完字符串，无法完全复刻零开销，
    # 热路径可先 `if diag.enabled(diag.TRACE):` 再构造消息。
    if level < _min_level:
        return
    # 取调用点帧：_emit <- trace/debug/... <- 调用点。
    # 本模块经 Cython 编译后帧链不可靠（.so 调用点无 Python 帧，栈浅时 _getframe
    # 直接抛 ValueError），取不到帧则省略位置段，绝不因诊断输出崩溃
    try:
        frame = sys._getframe(2)
    except ValueError:
        frame = None
    loc = (f"[{frame.f_code.co_filename}:{frame.f_lineno}] "
           f"[{frame.f_code.co_name}] ") if frame is not None else ""
    sys.stdout.write(
        f"{_timestamp()} [{_LEVEL_LABELS[level]}] [Yomk] {loc}{msg}\n"
    )
    sys.stdout.flush()


def trace(msg: str) -> None:
    _emit(TRACE, msg)


def debug(msg: str) -> None:
    _emit(DEBUG, msg)


def info(msg: str) -> None:
    _emit(INFO, msg)


def warn(msg: str) -> None:
    _emit(WARN, msg)


def error(msg: str) -> None:
    _emit(ERROR, msg)


def fatal(msg: str) -> None:
    _emit(FATAL, msg)
