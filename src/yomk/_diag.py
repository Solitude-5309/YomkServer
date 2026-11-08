import sys
import time


def _timestamp() -> str:
    now = time.time()
    millis = int((now - int(now)) * 1000)
    return time.strftime("[%Y-%m-%d %H:%M:%S", time.localtime(now)) + f".{millis:03d}]"


def _emit(level: str, msg: str, frame) -> None:
    sys.stdout.write(
        f"{_timestamp()} [{level}] [Yomk] "
        f"[{frame.f_code.co_filename}:{frame.f_lineno}] "
        f"[{frame.f_code.co_name}] {msg}\n"
    )
    sys.stdout.flush()


def trace(msg: str) -> None:
    _emit("TRACE", msg, sys._getframe(1))


def debug(msg: str) -> None:
    _emit("DEBUG", msg, sys._getframe(1))


def info(msg: str) -> None:
    _emit("INFO ", msg, sys._getframe(1))


def warn(msg: str) -> None:
    _emit("WARN ", msg, sys._getframe(1))


def error(msg: str) -> None:
    _emit("ERROR", msg, sys._getframe(1))


def fatal(msg: str) -> None:
    _emit("FATAL", msg, sys._getframe(1))