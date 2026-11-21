import sys
import time

# diag 级别常量：数值越大越严重，与日志六级一一对应
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

# 进程级最小输出级别，默认 WARN
_min_level = WARN


def set_min_level(level: int) -> None:
    # 任意线程可调；int 赋值在 GIL 下原子
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
    # 门控：低于最小级别整条丢弃；调用点已构造字符串，热路径可先用 enabled() 自查
    if level < _min_level:
        return
    # 取调用点帧；Cython 编译后帧链不可靠，取不到则省略位置段，避免诊断本身崩溃
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
