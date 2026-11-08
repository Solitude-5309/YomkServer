import os
import threading

from yomk.log._diag import _yomk_err_pos_log, _yomk_log_local_time_formatted
from yomk.msg.log import Log


class FileLogger:
    # 文件日志器，log 入内存缓冲、write 追加落盘清缓冲、
    # 析构自动 write 兜底；init 建目录与空文件（异常捕获转 bool 不穿透）、
    # 六级门控（关断级别 log 入口直接丢弃不入缓冲）
    def __init__(self):
        self.m_name = "main"
        self.m_dir = ""
        # 级别门控随 sink 自持（每实例独立、默认全开）：两级主闸并联架构的第二层
        # （逐器门控），第一层为 YomkLogger 文件全局主闸（经 /file_global_state
        # 内省），输出取两层 AND
        self.m_showTraceLog = True
        self.m_showDebugLog = True
        self.m_showInfoLog = True
        self.m_showWarnLog = True
        self.m_showErrorLog = True
        self.m_showFatalLog = True
        self.m_logBuffer = ""
        self.m_logBufferMutex = threading.Lock()

    def __del__(self):
        # 析构兜底：缓冲未落盘内容不丢；
        # try 防护解释器关闭期属性缺失
        try:
            self.write()
        except Exception:
            pass

    def get_name(self):
        return self.m_name

    def set_name(self, name):
        self.m_name = name

    def get_dir(self):
        return self.m_dir

    def set_dir(self, dir):
        self.m_dir = dir

    def set_level_on(self, log_level, on):
        # 按器级别门控：关断的级别在 log() 入口直接丢弃；unknown 级别按 Info 门控
        if log_level == Log.eTrace:
            self.m_showTraceLog = on
        elif log_level == Log.eDebug:
            self.m_showDebugLog = on
        elif log_level == Log.eInfo:
            self.m_showInfoLog = on
        elif log_level == Log.eWarn:
            self.m_showWarnLog = on
        elif log_level == Log.eError:
            self.m_showErrorLog = on
        elif log_level == Log.eFatal:
            self.m_showFatalLog = on
        else:
            # unknown 级别按 Info 门控（与 log() 降级语义一致）
            self.m_showInfoLog = on

    def is_level_on(self, log_level):
        # 反查级别门控状态；unknown 级别返回 Info 门控值
        if log_level == Log.eTrace:
            return self.m_showTraceLog
        elif log_level == Log.eDebug:
            return self.m_showDebugLog
        elif log_level == Log.eInfo:
            return self.m_showInfoLog
        elif log_level == Log.eWarn:
            return self.m_showWarnLog
        elif log_level == Log.eError:
            return self.m_showErrorLog
        elif log_level == Log.eFatal:
            return self.m_showFatalLog
        else:
            return self.m_showInfoLog

    def init(self):
        log_file_path = self.m_dir + "/" + self.m_name + ".log"
        dir_path = os.path.dirname(log_file_path)
        try:
            if dir_path and not os.path.exists(dir_path):
                os.makedirs(dir_path)   # 多级递归创建
        except OSError as e:
            _yomk_err_pos_log("init file logger fs error: " +
                              log_file_path + ", " + str(e))
            return False
        try:
            with open(log_file_path, "w"):
                pass   # 建空文件（ofstream 默认 truncate 语义）
        except OSError:
            _yomk_err_pos_log("create log file failed: " + log_file_path)
            return False
        return True

    def log(self, log_level, tag, log_msg):
        # 级别门控在缓冲锁外判定（GIL 下 bool 读原子，无新锁）：关断级别直接丢弃
        # 不入缓冲（unknown 级别按 Info 门控，与下方 default 降级语义一致）；
        # 位置在取时间戳之前避免白算
        if log_level == Log.eTrace:
            if not self.m_showTraceLog:
                return
        elif log_level == Log.eDebug:
            if not self.m_showDebugLog:
                return
        elif log_level == Log.eInfo:
            if not self.m_showInfoLog:
                return
        elif log_level == Log.eWarn:
            if not self.m_showWarnLog:
                return
        elif log_level == Log.eError:
            if not self.m_showErrorLog:
                return
        elif log_level == Log.eFatal:
            if not self.m_showFatalLog:
                return
        else:
            if not self.m_showInfoLog:
                return

        time_str = _yomk_log_local_time_formatted()

        # tag 由日志器统一拼接：空 tag 不加前缀
        tag_prefix = ("[" + tag + "] ") if tag else ""

        with self.m_logBufferMutex:
            if log_level == Log.eTrace:
                self._append_line(time_str, tag_prefix, log_msg, " [TRACE] ")
            elif log_level == Log.eDebug:
                self._append_line(time_str, tag_prefix, log_msg, " [DEBUG] ")
            elif log_level == Log.eInfo:
                self._append_line(time_str, tag_prefix, log_msg, " [INFO ] ")
            elif log_level == Log.eWarn:
                self._append_line(time_str, tag_prefix, log_msg, " [WARN ] ")
            elif log_level == Log.eError:
                self._append_line(time_str, tag_prefix, log_msg, " [ERROR] ")
            elif log_level == Log.eFatal:
                self._append_line(time_str, tag_prefix, log_msg, " [FATAL] ")
            else:
                _yomk_err_pos_log("Unknown log level: " + str(log_level) +
                                  ", using Info level instead.")
                self._append_line(time_str, tag_prefix, log_msg, " [INFO ] ")

    def _append_line(self, time_str, tag_prefix, log_msg, level_tag):
        # 缓冲行格式时间戳 + " [级别] " + tag 前缀 + 内容 + 换行
        self.m_logBuffer += time_str + level_tag + tag_prefix + log_msg + "\n"

    def write(self):
        with self.m_logBufferMutex:
            # 空缓冲不触碰文件
            if not self.m_logBuffer:
                return
            try:
                with open(self.m_dir + "/" + self.m_name + ".log", "a") as log_file:
                    log_file.write(self.m_logBuffer)
            except OSError:
                _yomk_err_pos_log("open log file failed: " +
                                  self.m_dir + "/" + self.m_name + ".log")
                return
            self.m_logBuffer = ""