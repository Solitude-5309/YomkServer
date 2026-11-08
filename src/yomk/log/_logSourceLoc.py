# log 域专属诊断扩展（与共享 yomk._diag 的差异部分）：
# 位置前缀三开关、受控位置前缀、错误位置日志、本地时间格式化。
# 仅 import 标准库（sys/time）；六级共享诊断见 yomk._diag。
import sys
import time

# 位置前缀三开关（进程级；缺省 file=off / line=on / func=off，
# GIL 下 bool 读写原子）
_yomk_loc_show_file = False
_yomk_loc_show_line = True
_yomk_loc_show_func = False


def _yomk_loc_base_name(path):
    # 取路径 basename（兼容 '/' 与 '\\'），与主流日志风格一致
    pos = max(path.rfind("/"), path.rfind("\\"))
    return path[pos + 1:] if pos != -1 else path


def _yomk_source_loc_prefix(filename, lineno, funcname):
    # 按进程级三开关拼位置前缀 "[file:line:func]"（file 取 basename）；
    # 三项全关返回空串——无位置前缀
    if not (_yomk_loc_show_file or _yomk_loc_show_line or _yomk_loc_show_func):
        return ""
    parts = []
    if _yomk_loc_show_file:
        parts.append(_yomk_loc_base_name(filename))
    if _yomk_loc_show_line:
        parts.append(str(lineno))
    if _yomk_loc_show_func:
        parts.append(funcname)
    return "[" + ":".join(parts) + "]"


def _yomk_log_local_time_formatted():
    # 本地时间戳 "[YYYY-MM-DD HH:MM:SS.mmm]"
    now = time.time()
    lt = time.localtime(now)
    ms = int(round((now - int(now)) * 1000)) % 1000
    # python 的 tm_mon 已是 1-12 基，无需再加 1
    return "[%04d-%02d-%02d %02d:%02d:%02d.%03d]" % (
        lt.tm_year, lt.tm_mon, lt.tm_mday,
        lt.tm_hour, lt.tm_min, lt.tm_sec, ms)


def _yomk_err_pos_log(msg):
    # 框架自诊断通道（ERROR 用途）：直写 stdout，固定携带 file:line/func，
    # 不受位置三开关控制；保留 sys._getframe(1)，不要在其上加 wrapper
    frame = sys._getframe(1)
    sys.stdout.write(_yomk_log_local_time_formatted() + " [ERROR] [Yomk] [" +
                     frame.f_code.co_filename + ":" + str(frame.f_lineno) +
                     "] [" + frame.f_code.co_name + "] " + msg + "\n")
    sys.stdout.flush()
