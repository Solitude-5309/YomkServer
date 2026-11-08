import sys
import threading

from yomk.log._logSourceLoc import _yomk_err_pos_log, _yomk_log_local_time_formatted
from yomk.msg.log import Log


class ConsoleLogger:
    # 控制台日志器，六级 stdout 输出、空 tag 回退日志器名、
    # 非法级别降级 Info、按器级别门控（关断级别 log 入口直接丢弃）
    def __init__(self):
        self.m_name = "main"
        self.m_mutex = threading.Lock()
        # 级别门控随 sink 自持（每实例独立、默认全开）：两级主闸并联架构的第二层
        # （逐器门控），第一层为 YomkLogger 全局主闸（经 /console_global_state
        # 内省），输出取两层 AND
        self.m_showTraceLog = True
        self.m_showDebugLog = True
        self.m_showInfoLog = True
        self.m_showWarnLog = True
        self.m_showErrorLog = True
        self.m_showFatalLog = True

    def get_name(self):
        return self.m_name

    def set_name(self, name):
        self.m_name = name

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

    def log(self, log_level, tag, log_msg):
        # 级别门控在类级锁外判定（GIL 下 bool 读原子，无新锁）：关断级别直接丢弃
        # 不输出（unknown 级别按 Info 门控，与下方 default 降级语义一致）
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

        with self.m_mutex:
            # tag 优先显示；空 tag 回退日志器名，保持旧行为
            tag_shown = tag if tag else self.m_name
            if log_level == Log.eTrace:
                self._emit(time_str, "[TRACE]", tag_shown, log_msg)
            elif log_level == Log.eDebug:
                self._emit(time_str, "[DEBUG]", tag_shown, log_msg)
            elif log_level == Log.eInfo:
                self._emit(time_str, "[INFO ]", tag_shown, log_msg)
            elif log_level == Log.eWarn:
                self._emit(time_str, "[WARN ]", tag_shown, log_msg)
            elif log_level == Log.eError:
                self._emit(time_str, "[ERROR]", tag_shown, log_msg)
            elif log_level == Log.eFatal:
                self._emit(time_str, "[FATAL]", tag_shown, log_msg)
            else:
                _yomk_err_pos_log("Unknown log level: " + str(log_level) +
                              ", using Info level instead.")
                self._emit(time_str, "[INFO ]", tag_shown, log_msg)

    @staticmethod
    def _emit(time_str, level_tag, tag_shown, log_msg):
        # 直写 stdout 并 flush
        sys.stdout.write(time_str + " " + level_tag +
                         " [" + tag_shown + "] " + log_msg + "\n")
        sys.stdout.flush()