import threading
from typing import TYPE_CHECKING

from readerwriterlock import rwlock

from yomk.log._logSourceLoc import _yomk_err_pos_log
from yomk.log.ConsoleLogger import ConsoleLogger
from yomk.log.FileLogger import FileLogger
from yomk.msg.log import (
    ConsoleLogProxy,
    FileLogProxy,
    Log,
    LogFile,
    LoggerInfo,
    LoggerQuery,
    LoggerState,
)
from yomk.msg.server import ResStatus, Response
from yomk.server import YomkService

if TYPE_CHECKING:
    from yomk.server import YomkServer


class YomkLogger(YomkService):
    # 日志模块服务：16 端点装配序与契约消息均为固定对外契约。
    # 锁序约定：嵌套双读仅 list_all 内省快照，一律按 m_consoleLoggersMutex ->
    # m_fileLoggersMutex 的顺序持有，全仓无反向嵌套点（console_log/file_log 的
    # proxy 锁在独立作用域释放后才取 logger 表锁；logger_info/write_file_log/
    # create_file_logger/set_named_console_level/set_named_file_level/
    # set_file_log_proxy 均单锁；current_console_state/named_console_state 的
    # console proxyOn 与 current_file_state/logger_info/list_all 的 file proxyOn
    # 均在表锁外先独立取 proxy 锁）；m_consoleLogProxyMutex 与 m_fileLogProxyMutex
    # 为叶子锁，其临界区内不得获取任何其他锁。
    # 后续维护新增嵌套时须沿用此序，否则有死锁风险。两表保持 dict 但内省输出显式
    # sorted :map 字典序（python dict 为插入序，ServerInfo 轮先例）。
    def __init__(self, server: "YomkServer"):
        super().__init__(server)
        self.set_name("/YomkLogger")
        self.m_consoleLoggers = {"main": ConsoleLogger()}
        self.m_consoleLoggersMutex = rwlock.RWLockFair()
        self.m_fileLoggers = {}
        self.m_fileLoggersMutex = rwlock.RWLockFair()
        self.m_showConsoleTraceLog = True
        self.m_showConsoleDebugLog = True
        self.m_showConsoleInfoLog = True
        self.m_showConsoleWarningLog = True
        self.m_showConsoleErrorLog = True
        self.m_showConsoleFatalLog = True
        # 文件日志全局主闸（第一层）：与控制台主闸相互独立，输出取主闸×逐器两层 AND；
        # 逐器门控随 FileLogger sink 自持（第二层）
        self.m_showFileTraceLog = True
        self.m_showFileDebugLog = True
        self.m_showFileInfoLog = True
        self.m_showFileWarningLog = True
        self.m_showFileErrorLog = True
        self.m_showFileFatalLog = True
        # proxy 字段专属锁：锁内拷贝快照、锁外调用回调，避免持锁重入；控制台与文件
        # 两套代理互不共锁，各自独立
        self.m_consoleLogProxyMutex = threading.Lock()
        self.m_consoleLogProxy = False
        self.m_consoleLogProxyFunc = None
        # 文件日志全局代理（与控制台代理相互独立，语义对称）；proxy 字段专属锁同上
        self.m_fileLogProxyMutex = threading.Lock()
        self.m_fileLogProxy = False
        self.m_fileLogProxyFunc = None

    def init(self):
        self.install_func("/set_console_log_proxy",
                          self.set_console_log_proxy, "ConsoleLogProxy")
        self.install_func("/set_file_log_proxy",
                          self.set_file_log_proxy, "FileLogProxy")
        self.install_func("/console_log", self.console_log, "Log")
        self.install_func("/create_file_logger",
                          self.create_file_logger, "LogFile")
        self.install_func("/file_log", self.file_log, "Log")
        self.install_func("/write_file_log", self.write_file_log, "String")
        self.install_func("/delete_logger", self.delete_logger, "String")
        self.install_func("/off_console_log_by_level",
                          self.off_console_log_by_level, "Log")
        self.install_func("/on_console_log_by_level",
                          self.on_console_log_by_level, "Log")
        # 调试内省接口，端点挂在本服务 funcMap；/loggers、/logger 均传类型参数
        self.install_func("/loggers", self.loggers, "String")
        self.install_func("/logger", self.logger_info, "LoggerQuery")
        self.install_func("/all", self.list_all)
        self.install_func("/console_global_state", self.console_global_state)
        self.install_func("/off_file_log_by_level",
                          self.off_file_log_by_level, "Log")
        self.install_func("/on_file_log_by_level",
                          self.on_file_log_by_level, "Log")
        self.install_func("/file_global_state", self.file_global_state)
        return 0

    @staticmethod
    def _unpack(pkg, cls):
        # eNo + "pkg is null or pkg is not <Type>"（裸串无首尾空格/句号；
        # String 端点包为裸 str）
        if isinstance(pkg, cls):
            return None
        type_name = "String" if cls is str else cls.__name__
        _yomk_err_pos_log("pkg is null or pkg is not " + type_name)
        return Response(ResStatus.eNo, "pkg is null or pkg is not " + type_name)

    def console_log(self, pkg: Log):
        bad = self._unpack(pkg, Log)
        if bad:
            return bad
        if not pkg.m_logger:
            _yomk_err_pos_log("console logger name is empty, use main")
            pkg.m_logger = "main"

        # 锁内拷贝 proxy 快照，锁外调用回调（避免用户回调持锁重入）
        with self.m_consoleLogProxyMutex:
            proxy_enabled = self.m_consoleLogProxy
            proxy_func = self.m_consoleLogProxyFunc
        if proxy_enabled and proxy_func and not proxy_func(pkg):
            return Response(ResStatus.eOk, "console log proxy is success.")

        # 双检锁：读锁 miss 后升级写锁二次查找，logger 引用拷出锁外使用
        console_logger = None
        with self.m_consoleLoggersMutex.gen_rlock():
            found = self.m_consoleLoggers.get(pkg.m_logger)
            if found is not None:
                console_logger = found
        if console_logger is None:
            with self.m_consoleLoggersMutex.gen_wlock():
                console_logger = self.m_consoleLoggers.get(pkg.m_logger)
                if console_logger is None:
                    new_logger = ConsoleLogger()
                    new_logger.set_name(pkg.m_logger)
                    self.m_consoleLoggers[pkg.m_logger] = new_logger
                    console_logger = new_logger

        if pkg.m_level == Log.eTrace:
            if self.m_showConsoleTraceLog:
                console_logger.log(pkg.m_level, pkg.m_tag, pkg.m_log)
        elif pkg.m_level == Log.eDebug:
            if self.m_showConsoleDebugLog:
                console_logger.log(pkg.m_level, pkg.m_tag, pkg.m_log)
        elif pkg.m_level == Log.eInfo:
            if self.m_showConsoleInfoLog:
                console_logger.log(pkg.m_level, pkg.m_tag, pkg.m_log)
        elif pkg.m_level == Log.eWarn:
            if self.m_showConsoleWarningLog:
                console_logger.log(pkg.m_level, pkg.m_tag, pkg.m_log)
        elif pkg.m_level == Log.eError:
            if self.m_showConsoleErrorLog:
                console_logger.log(pkg.m_level, pkg.m_tag, pkg.m_log)
        elif pkg.m_level == Log.eFatal:
            if self.m_showConsoleFatalLog:
                console_logger.log(pkg.m_level, pkg.m_tag, pkg.m_log)
        else:
            _yomk_err_pos_log("unknown log level, use Info")
            if self.m_showConsoleInfoLog:
                console_logger.log(Log.eInfo, pkg.m_tag, pkg.m_log)

        return Response(ResStatus.eOk, "success.")

    def set_console_log_proxy(self, pkg: ConsoleLogProxy):
        bad = self._unpack(pkg, ConsoleLogProxy)
        if bad:
            return bad
        # 与 console_log 读侧互斥；传 None 即卸载 proxy、恢复默认输出；两字段同锁一致更新
        with self.m_consoleLogProxyMutex:
            self.m_consoleLogProxyFunc = pkg.m_consoleLogProxyFunc
            self.m_consoleLogProxy = self.m_consoleLogProxyFunc is not None
        return Response(ResStatus.eOk, "success.")

    def set_file_log_proxy(self, pkg: FileLogProxy):
        bad = self._unpack(pkg, FileLogProxy)
        if bad:
            return bad
        # 与 file_log 读侧互斥；传 None 即卸载 proxy、恢复默认落盘；两字段同锁一致更新
        with self.m_fileLogProxyMutex:
            self.m_fileLogProxyFunc = pkg.m_fileLogProxyFunc
            self.m_fileLogProxy = self.m_fileLogProxyFunc is not None
        return Response(ResStatus.eOk, "success.")

    def create_file_logger(self, pkg: LogFile):
        bad = self._unpack(pkg, LogFile)
        if bad:
            return bad

        # 空名/空目录拒绝：eInvalid（参数无效惯例）
        if not pkg.m_logger:
            _yomk_err_pos_log("logger name is empty.")
            return Response(ResStatus.eInvalid, "logger name is empty.")
        if not pkg.m_dir:
            _yomk_err_pos_log("logger dir is empty.")
            return Response(ResStatus.eInvalid, "logger dir is empty.")

        # 全程持独占锁
        with self.m_fileLoggersMutex.gen_wlock():
            if pkg.m_logger in self.m_fileLoggers:
                return Response(ResStatus.eNo, "logger name already exists.")

            file_logger = FileLogger()
            file_logger.set_name(pkg.m_logger)
            file_logger.set_dir(pkg.m_dir)
            if not file_logger.init():
                _yomk_err_pos_log("init file logger failed: " + pkg.m_logger)
                return Response(ResStatus.eNo, "init file logger failed.")
            self.m_fileLoggers[pkg.m_logger] = file_logger

        return Response(ResStatus.eOk, "success.")

    def file_log(self, pkg: Log):
        bad = self._unpack(pkg, Log)
        if bad:
            return bad

        if not pkg.m_logger:
            _yomk_err_pos_log("file logger name is empty.")
            return Response(ResStatus.eNo, "file logger name is empty.")

        # 锁内拷贝文件代理快照，锁外调用回调（与 console_log 同款模式，避免用户回调持锁重入）；
        # 代理先于表查找、主闸与逐器门控（未知名在拦截态下同样被短路，语义与 console 一致）
        with self.m_fileLogProxyMutex:
            proxy_enabled = self.m_fileLogProxy
            proxy_func = self.m_fileLogProxyFunc
        if proxy_enabled and proxy_func and not proxy_func(pkg):
            return Response(ResStatus.eOk, "file log proxy is success.")

        with self.m_fileLoggersMutex.gen_rlock():
            file_logger = self.m_fileLoggers.get(pkg.m_logger)
            if file_logger is None:
                _yomk_err_pos_log("file logger: " +
                                  pkg.m_logger + " not found.")
                return Response(ResStatus.eNo, "file logger not found.")

            if pkg.m_level == Log.eTrace:
                if self.m_showFileTraceLog:
                    file_logger.log(pkg.m_level, pkg.m_tag, pkg.m_log)
            elif pkg.m_level == Log.eDebug:
                if self.m_showFileDebugLog:
                    file_logger.log(pkg.m_level, pkg.m_tag, pkg.m_log)
            elif pkg.m_level == Log.eInfo:
                if self.m_showFileInfoLog:
                    file_logger.log(pkg.m_level, pkg.m_tag, pkg.m_log)
            elif pkg.m_level == Log.eWarn:
                if self.m_showFileWarningLog:
                    file_logger.log(pkg.m_level, pkg.m_tag, pkg.m_log)
            elif pkg.m_level == Log.eError:
                if self.m_showFileErrorLog:
                    file_logger.log(pkg.m_level, pkg.m_tag, pkg.m_log)
            elif pkg.m_level == Log.eFatal:
                if self.m_showFileFatalLog:
                    file_logger.log(pkg.m_level, pkg.m_tag, pkg.m_log)
            else:
                _yomk_err_pos_log("unknown log level, use Info")
                if self.m_showFileInfoLog:
                    file_logger.log(Log.eInfo, pkg.m_tag, pkg.m_log)

        return Response(ResStatus.eOk, "success.")

    def write_file_log(self, pkg):
        bad = self._unpack(pkg, str)
        if bad:
            return bad
        with self.m_fileLoggersMutex.gen_rlock():
            file_logger = self.m_fileLoggers.get(pkg)
            if file_logger is None:
                _yomk_err_pos_log("logger: " + pkg + " not found.")
                return Response(ResStatus.eNo, "logger not found.")
            file_logger.write()

        return Response(ResStatus.eOk, "success.")

    def delete_logger(self, pkg):
        bad = self._unpack(pkg, str)
        if bad:
            return bad

        # 空名拒绝（同 createFileLogger 惯例）
        if not pkg:
            _yomk_err_pos_log("logger name is empty.")
            return Response(ResStatus.eInvalid, "logger name is empty.")

        # 分段持锁不嵌套：删除无需跨表原子快照，同时持两把写锁会阻塞全部日志写入
        deleted_console = 0
        with self.m_consoleLoggersMutex.gen_wlock():
            if pkg in self.m_consoleLoggers:
                del self.m_consoleLoggers[pkg]
                deleted_console = 1

        # 锁内拷出、锁外释放最后引用：FileLogger __del__ 落盘 I/O 不阻塞日志写入
        deleted_file = 0
        removed_file = None
        with self.m_fileLoggersMutex.gen_wlock():
            removed_file = self.m_fileLoggers.pop(pkg, None)
            if removed_file is not None:
                deleted_file = 1
        removed_file = None   # 显式释放触发 __del__ 自动 write（缓冲内容不丢）

        if deleted_console == 0 and deleted_file == 0:
            _yomk_err_pos_log("logger: " + pkg + " not found.")
            return Response(ResStatus.eNo, "logger not found.")

        # 不删除磁盘上的 .log 文件（清理归调用方）
        return Response(
            ResStatus.eOk,
            "deleted console:" + str(deleted_console) + " file:" + str(deleted_file))

    def off_console_log_by_level(self, pkg: Log):
        bad = self._unpack(pkg, Log)
        if bad:
            return bad

        # 具名请求：逐器门控路径（写第二层，不触碰全局主闸；未命中 eNo 不惰性创建）
        if pkg.m_logger:
            return self.set_named_console_level(pkg.m_logger, pkg.m_level, False)

        # 空名 = 全局主闸路径（与既有语义一致）
        if pkg.m_level == Log.eTrace:
            self.m_showConsoleTraceLog = False
        elif pkg.m_level == Log.eInfo:
            self.m_showConsoleInfoLog = False
        elif pkg.m_level == Log.eWarn:
            self.m_showConsoleWarningLog = False
        elif pkg.m_level == Log.eError:
            self.m_showConsoleErrorLog = False
        elif pkg.m_level == Log.eFatal:
            self.m_showConsoleFatalLog = False
        elif pkg.m_level == Log.eDebug:
            self.m_showConsoleDebugLog = False
        else:
            _yomk_err_pos_log("unknown log level, turn off failed.")

        return Response(ResStatus.eOk, "success.")

    def on_console_log_by_level(self, pkg: Log):
        bad = self._unpack(pkg, Log)
        if bad:
            return bad

        # 具名请求：逐器门控路径（写第二层，不触碰全局主闸；未命中 eNo 不惰性创建）
        if pkg.m_logger:
            return self.set_named_console_level(pkg.m_logger, pkg.m_level, True)

        # 空名 = 全局主闸路径（与既有语义一致）
        if pkg.m_level == Log.eTrace:
            self.m_showConsoleTraceLog = True
        elif pkg.m_level == Log.eInfo:
            self.m_showConsoleInfoLog = True
        elif pkg.m_level == Log.eWarn:
            self.m_showConsoleWarningLog = True
        elif pkg.m_level == Log.eError:
            self.m_showConsoleErrorLog = True
        elif pkg.m_level == Log.eFatal:
            self.m_showConsoleFatalLog = True
        elif pkg.m_level == Log.eDebug:
            self.m_showConsoleDebugLog = True
        else:
            _yomk_err_pos_log("unknown log level, turn on failed.")
        return Response(ResStatus.eOk, "success.")

    def set_named_console_level(self, logger_name, level, on):
        # 具名开关唯一写入口：表读锁内查找拷引用出锁、锁外 set_level_on
        # （单锁；未命中 eNo 不惰性创建，不触碰全局主闸）
        with self.m_consoleLoggersMutex.gen_rlock():
            console_logger = self.m_consoleLoggers.get(logger_name)
            if console_logger is None:
                _yomk_err_pos_log("console logger: " +
                                  logger_name + " not found.")
                return Response(ResStatus.eNo, "console logger not found.")

        if level == Log.eTrace:
            console_logger.set_level_on(Log.eTrace, on)
        elif level == Log.eInfo:
            console_logger.set_level_on(Log.eInfo, on)
        elif level == Log.eWarn:
            console_logger.set_level_on(Log.eWarn, on)
        elif level == Log.eError:
            console_logger.set_level_on(Log.eError, on)
        elif level == Log.eFatal:
            console_logger.set_level_on(Log.eFatal, on)
        elif level == Log.eDebug:
            console_logger.set_level_on(Log.eDebug, on)
        else:
            _yomk_err_pos_log("unknown log level, turn " +
                              ("on" if on else "off") + " failed.")
        return Response(ResStatus.eOk, "success.")

    def off_file_log_by_level(self, pkg: Log):
        bad = self._unpack(pkg, Log)
        if bad:
            return bad

        # 具名请求：逐器门控路径（写第二层，不触碰全局主闸；未命中 eNo 不惰性创建）
        if pkg.m_logger:
            return self.set_named_file_level(pkg.m_logger, pkg.m_level, False)

        # 空名 = 全局主闸路径（与控制台侧双模式语义对称）
        if pkg.m_level == Log.eTrace:
            self.m_showFileTraceLog = False
        elif pkg.m_level == Log.eInfo:
            self.m_showFileInfoLog = False
        elif pkg.m_level == Log.eWarn:
            self.m_showFileWarningLog = False
        elif pkg.m_level == Log.eError:
            self.m_showFileErrorLog = False
        elif pkg.m_level == Log.eFatal:
            self.m_showFileFatalLog = False
        elif pkg.m_level == Log.eDebug:
            self.m_showFileDebugLog = False
        else:
            _yomk_err_pos_log("unknown log level, turn off failed.")

        return Response(ResStatus.eOk, "success.")

    def on_file_log_by_level(self, pkg: Log):
        bad = self._unpack(pkg, Log)
        if bad:
            return bad

        # 具名请求：逐器门控路径（写第二层，不触碰全局主闸；未命中 eNo 不惰性创建）
        if pkg.m_logger:
            return self.set_named_file_level(pkg.m_logger, pkg.m_level, True)

        # 空名 = 全局主闸路径（与控制台侧双模式语义对称）
        if pkg.m_level == Log.eTrace:
            self.m_showFileTraceLog = True
        elif pkg.m_level == Log.eInfo:
            self.m_showFileInfoLog = True
        elif pkg.m_level == Log.eWarn:
            self.m_showFileWarningLog = True
        elif pkg.m_level == Log.eError:
            self.m_showFileErrorLog = True
        elif pkg.m_level == Log.eFatal:
            self.m_showFileFatalLog = True
        elif pkg.m_level == Log.eDebug:
            self.m_showFileDebugLog = True
        else:
            _yomk_err_pos_log("unknown log level, turn on failed.")
        return Response(ResStatus.eOk, "success.")

    def set_named_file_level(self, logger_name, level, on):
        # 文件侧具名开关唯一写入口：表读锁内查找拷引用出锁、锁外 set_level_on
        # （单锁；未命中 eNo 不惰性创建，不触碰全局主闸）
        with self.m_fileLoggersMutex.gen_rlock():
            file_logger = self.m_fileLoggers.get(logger_name)
            if file_logger is None:
                _yomk_err_pos_log("file logger: " +
                                  logger_name + " not found.")
                return Response(ResStatus.eNo, "file logger not found.")

        if level == Log.eTrace:
            file_logger.set_level_on(Log.eTrace, on)
        elif level == Log.eInfo:
            file_logger.set_level_on(Log.eInfo, on)
        elif level == Log.eWarn:
            file_logger.set_level_on(Log.eWarn, on)
        elif level == Log.eError:
            file_logger.set_level_on(Log.eError, on)
        elif level == Log.eFatal:
            file_logger.set_level_on(Log.eFatal, on)
        elif level == Log.eDebug:
            file_logger.set_level_on(Log.eDebug, on)
        else:
            _yomk_err_pos_log("unknown log level, turn " +
                              ("on" if on else "off") + " failed.")
        return Response(ResStatus.eOk, "success.")

    def loggers(self, pkg):
        bad = self._unpack(pkg, str)
        if bad:
            return bad
        if pkg == "console":
            # 单读锁遍历 console 表：纯名清单，不再嵌套双锁
            with self.m_consoleLoggersMutex.gen_rlock():
                names = sorted(self.m_consoleLoggers)
        elif pkg == "file":
            # 单读锁遍历 file 表：纯名清单，不再嵌套双锁
            with self.m_fileLoggersMutex.gen_rlock():
                names = sorted(self.m_fileLoggers)
        else:
            _yomk_err_pos_log("unknown logger type: " + pkg + ".")
            return Response(ResStatus.eNo, "unknown logger type.")
        # 单表显式 sorted :map 中序字典序（python dict 为插入序，类头注释先例）
        return Response(ResStatus.eOk, "ok", names)

    def current_console_state(self):
        # 取控制台全局主闸态（/console_global_state 返回体）：六个级别开关为独立
        # 原子读（组合本无原子快照保证），proxy 状态读侧与写侧互斥（叶子锁，
        # 独立作用域），不持任何表锁
        state = LoggerState(
            bool(self.m_showConsoleTraceLog), bool(self.m_showConsoleDebugLog),
            bool(self.m_showConsoleInfoLog), bool(self.m_showConsoleWarningLog),
            bool(self.m_showConsoleErrorLog), bool(self.m_showConsoleFatalLog))
        with self.m_consoleLogProxyMutex:
            state.proxy_on = self.m_consoleLogProxy
        return state

    def named_console_state(self, console_logger, proxy_on):
        # 取单条 console 条目状态：六级别为逐器门控层真值（is_level_on），
        # proxyOn 由调用点在表锁外取好传入（条目不做两层 AND 代算）
        return LoggerState(
            console_logger.is_level_on(Log.eTrace),
            console_logger.is_level_on(Log.eDebug),
            console_logger.is_level_on(Log.eInfo),
            console_logger.is_level_on(Log.eWarn),
            console_logger.is_level_on(Log.eError),
            console_logger.is_level_on(Log.eFatal),
            proxy_on)

    def current_file_state(self):
        # 取文件全局主闸态（/file_global_state 返回体）：六个级别开关为独立
        # 原子读（组合本无原子快照保证），与控制台主闸相互独立，不持任何表锁；
        # proxy 状态读侧与写侧互斥（叶子锁独立作用域，与控制台 proxyOn 各自独立）
        state = LoggerState(
            bool(self.m_showFileTraceLog), bool(self.m_showFileDebugLog),
            bool(self.m_showFileInfoLog), bool(self.m_showFileWarningLog),
            bool(self.m_showFileErrorLog), bool(self.m_showFileFatalLog))
        with self.m_fileLogProxyMutex:
            state.proxy_on = self.m_fileLogProxy
        return state

    def named_file_state(self, file_logger, proxy_on):
        # 取单条 file 条目状态：六级别为逐器门控层真值（is_level_on），
        # proxyOn 由调用点传入（文件代理态，条目不做两层 AND 代算）
        return LoggerState(
            file_logger.is_level_on(Log.eTrace),
            file_logger.is_level_on(Log.eDebug),
            file_logger.is_level_on(Log.eInfo),
            file_logger.is_level_on(Log.eWarn),
            file_logger.is_level_on(Log.eError),
            file_logger.is_level_on(Log.eFatal),
            proxy_on)

    def logger_info(self, pkg):
        bad = self._unpack(pkg, LoggerQuery)
        if bad:
            return bad
        # 按类型单表精确查找：两表允许同名，不再先 console 后 file 双表 fallback（同名歧义由 type 消除）；
        # 命中：锁内构造结构化快照，明确字段承载全部返回数据（msg 恒 "ok"，数据走 data）
        if pkg.m_type == "console":
            # 锁内查找拷引用出锁外使用（与 console_log 双检锁同款模式）
            console_logger = None
            with self.m_consoleLoggersMutex.gen_rlock():
                console_logger = self.m_consoleLoggers.get(pkg.m_logger_name)
                if console_logger is None:
                    _yomk_err_pos_log("console logger: " +
                                      pkg.m_logger_name + " not found.")
                    return Response(ResStatus.eNo, "logger not found.")
            # 状态段：条目报逐器门控层真值；proxyOn 在表锁外独立取（proxy 叶子锁独立作用域）
            with self.m_consoleLogProxyMutex:
                proxy_on = self.m_consoleLogProxy
            return Response(
                ResStatus.eOk, "ok",
                LoggerInfo(pkg.m_logger_name, "console", "",
                           self.named_console_state(console_logger, proxy_on)))
        if pkg.m_type == "file":
            # 锁内查找拷引用出锁外使用（与 console 分支同款模式）
            file_logger = None
            with self.m_fileLoggersMutex.gen_rlock():
                file_logger = self.m_fileLoggers.get(pkg.m_logger_name)
                if file_logger is None:
                    _yomk_err_pos_log("file logger: " +
                                      pkg.m_logger_name + " not found.")
                    return Response(ResStatus.eNo, "logger not found.")
            # 状态段：条目报文件逐器门控层真值；proxyOn 在表锁外独立取
            # （proxy 叶子锁独立作用域）
            with self.m_fileLogProxyMutex:
                proxy_on = self.m_fileLogProxy
            return Response(
                ResStatus.eOk, "ok",
                LoggerInfo(pkg.m_logger_name, "file", file_logger.get_dir(),
                           self.named_file_state(file_logger, proxy_on)))
        _yomk_err_pos_log("unknown logger type: " + pkg.m_type + ".")
        return Response(ResStatus.eNo, "unknown logger type.")

    def list_all(self, pkg):
        # LoggerState 内嵌每条 LoggerInfo：console 条目报控制台逐器门控层真值
        # （named_console_state 逐条求值），file 条目报文件逐器门控层真值
        # （named_file_state 逐条求值）；
        # 全局主闸态迁出条目、由 /console_global_state 与 /file_global_state 承载
        # console/file 两个 proxyOn 各自独立取（两把 proxy 叶子锁独立作用域），随后传入逐条求值
        with self.m_consoleLogProxyMutex:
            proxy_on = self.m_consoleLogProxy
        with self.m_fileLogProxyMutex:
            file_proxy_on = self.m_fileLogProxy
        # 同 loggers：按锁序嵌套双读锁取跨段快照，逐条构造 LoggerInfo
        # （与 logger_info 命中分支同构；console 段在前、file 段在后，各段字典序）
        with self.m_consoleLoggersMutex.gen_rlock():
            with self.m_fileLoggersMutex.gen_rlock():
                loggers = [
                    LoggerInfo(
                        name, "console", "",
                        self.named_console_state(
                            self.m_consoleLoggers[name], proxy_on))
                    for name in sorted(self.m_consoleLoggers)]
                loggers += [
                    LoggerInfo(
                        name, "file", self.m_fileLoggers[name].get_dir(),
                        self.named_file_state(self.m_fileLoggers[name],
                                              file_proxy_on))
                    for name in sorted(self.m_fileLoggers)]
        # msg 恒 "ok"，数据走 data（清单本身即 data）
        return Response(ResStatus.eOk, "ok", loggers)

    def console_global_state(self, pkg):
        # 控制台全局主闸态内省（无参形态同 /all；msg 恒 "ok"，LoggerState 包走 data）
        return Response(ResStatus.eOk, "ok", self.current_console_state())

    def file_global_state(self, pkg):
        # 文件全局主闸态内省（无参形态同 /all；msg 恒 "ok"，LoggerState 包走 data）
        return Response(ResStatus.eOk, "ok", self.current_file_state())