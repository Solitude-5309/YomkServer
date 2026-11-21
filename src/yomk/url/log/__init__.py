"""Built-in URL constants and introspection descriptions of the /YomkLogger service.

/YomkLogger 内置服务 URL 常量与内省描述（格式惯例见 docs/YomkServer.md「内省描述惯例」）。
"""

__all__ = [
    "LOG_ALL", "LOG_CONSOLE_GLOBAL_STATE", "LOG_CONSOLE_LOG",
    "LOG_CREATE_FILE_LOGGER", "LOG_DELETE_LOGGER", "LOG_FILE_GLOBAL_STATE",
    "LOG_FILE_LOG", "LOG_LOGGER", "LOG_LOGGERS", "LOG_OFF_CONSOLE_LOG_BY_LEVEL",
    "LOG_OFF_FILE_LOG_BY_LEVEL", "LOG_ON_CONSOLE_LOG_BY_LEVEL",
    "LOG_ON_FILE_LOG_BY_LEVEL", "LOG_SET_CONSOLE_PROXY", "LOG_SET_FILE_LOG_PROXY",
    "LOG_WRITE_FILE_LOG",
    "LOG_ALL_DESC", "LOG_CONSOLE_GLOBAL_STATE_DESC", "LOG_CONSOLE_LOG_DESC",
    "LOG_CREATE_FILE_LOGGER_DESC", "LOG_DELETE_LOGGER_DESC",
    "LOG_FILE_GLOBAL_STATE_DESC", "LOG_FILE_LOG_DESC", "LOG_LOGGER_DESC",
    "LOG_LOGGERS_DESC", "LOG_OFF_CONSOLE_LOG_BY_LEVEL_DESC",
    "LOG_OFF_FILE_LOG_BY_LEVEL_DESC", "LOG_ON_CONSOLE_LOG_BY_LEVEL_DESC",
    "LOG_ON_FILE_LOG_BY_LEVEL_DESC", "LOG_SET_CONSOLE_PROXY_DESC",
    "LOG_SET_FILE_LOG_PROXY_DESC", "LOG_WRITE_FILE_LOG_DESC",
]

LOG_SET_CONSOLE_PROXY = "/YomkLogger/set_console_log_proxy"
LOG_SET_FILE_LOG_PROXY = "/YomkLogger/set_file_log_proxy"
LOG_CONSOLE_LOG = "/YomkLogger/console_log"
LOG_CREATE_FILE_LOGGER = "/YomkLogger/create_file_logger"
LOG_FILE_LOG = "/YomkLogger/file_log"
LOG_WRITE_FILE_LOG = "/YomkLogger/write_file_log"
LOG_DELETE_LOGGER = "/YomkLogger/delete_logger"
LOG_ON_CONSOLE_LOG_BY_LEVEL = "/YomkLogger/on_console_log_by_level"
LOG_OFF_CONSOLE_LOG_BY_LEVEL = "/YomkLogger/off_console_log_by_level"
LOG_ON_FILE_LOG_BY_LEVEL = "/YomkLogger/on_file_log_by_level"
LOG_OFF_FILE_LOG_BY_LEVEL = "/YomkLogger/off_file_log_by_level"
LOG_LOGGERS = "/YomkLogger/loggers"
LOG_LOGGER = "/YomkLogger/logger"
LOG_ALL = "/YomkLogger/all"
LOG_FILE_GLOBAL_STATE = "/YomkLogger/file_global_state"
LOG_CONSOLE_GLOBAL_STATE = "/YomkLogger/console_global_state"

LOG_SET_CONSOLE_PROXY_DESC = """
# Overview
安装控制台输出代理：全局唯一，任意控制台日志器发射的每条记录都先过该回调。

# Inputs
ConsoleLogProxy 包：m_consoleLogProxyFunc 回调（Log → bool）；传 None 即卸载代理并恢复默认 stdout 输出。

# Outputs
data 无负载，msg "success"。

# Behavior
同步安装：持代理锁整体替换回调与开关两个字段；回调在日志发射线程同步执行（锁内取快照、锁外调用），返回 False 拦截该条，返回 True 穿透默认输出。

# Notes
代理是全局而非逐器；回调应快速返回，勿在回调内再发日志以免递归；重复安装为覆盖（幂等）；pkg 非 ConsoleLogProxy → Failed。
"""
LOG_SET_FILE_LOG_PROXY_DESC = """
# Overview
安装文件输出代理：全局唯一，任意 file 日志器发射的每条记录都先过该回调。

# Inputs
FileLogProxy 包：m_fileLogProxyFunc 回调（Log → bool）；传 None 即卸载代理并恢复默认落盘。

# Outputs
data 无负载，msg "success"。

# Behavior
同步安装：持代理锁整体替换回调与开关两个字段；回调在日志发射线程同步执行（锁内取快照、锁外调用），返回 False 拦截该条，返回 True 穿透默认落盘。

# Notes
代理先于文件表查找，故拦截态下未知名日志器的记录同样被短路；与控制台代理互相独立；重复安装为覆盖（幂等）；pkg 非 FileLogProxy → Failed。
"""
LOG_CONSOLE_LOG_DESC = """
# Overview
写一条控制台日志：先过控制台代理，再经"全局主闸 × 逐器级别门控"两级 AND 决定是否输出到 stdout。

# Inputs
Log 包：m_level 级别；m_log 正文；m_tag 标签（空串回退日志器名显示）；m_logger 日志器名（空串按 "main" 处理并 WARN）。

# Outputs
data 无负载；被代理拦截时 msg "ok"，否则 msg "success"。

# Behavior
同步执行：代理判定 → 日志器查找（读锁未命中升写锁双检，未命中按名惰性创建控制台日志器并登记）→ 主闸级别判定 → stdout 输出（跨实例行锁保证多器行不交错）。

# Notes
未知 m_level 回退 Info 并 WARN；控制台与文件两套代理、两套门控互相独立；pkg 非 Log → Failed。
"""
LOG_CREATE_FILE_LOGGER_DESC = """
# Overview
创建文件日志器并初始化落盘文件：注册后该名即可用于 /file_log 缓冲写入。

# Inputs
LogFile 包：m_logger 日志器名；m_dir 落盘目录（多级不存在时递归创建）。

# Outputs
data 无负载，msg "success"。

# Behavior
同步执行：创建目录与 <m_dir>/<m_logger>.log（truncate 语义建空文件）并登记到文件日志器表；日志先入内存缓冲，落盘由 /write_file_log 触发。

# Notes
非幂等——重名创建必 Failed；Invalid=m_logger/m_dir 为空；Failed=重名/建目录或建文件 IO 失败；pkg 非 LogFile → Failed。
"""
LOG_FILE_LOG_DESC = """
# Overview
向指定文件日志器缓冲写入一条日志；不落盘，需 /write_file_log 触发。

# Inputs
Log 包：m_logger 日志器名（不可为空）；m_level/m_log/m_tag 语义同控制台侧，m_tag 为空串时不加前缀。

# Outputs
data 无负载；被代理拦截时 msg "ok"，否则 msg "success"。

# Behavior
同步执行：文件代理判定 → 表查找（不惰性创建）→ 主闸 × 逐器门控 → 追加到该日志器内存缓冲。

# Notes
日志器不存在或 m_logger 为空 → Failed；未知 m_level 回退 Info 并 WARN；pkg 非 Log → Failed。
"""
LOG_WRITE_FILE_LOG_DESC = """
# Overview
把指定文件日志器的内存缓冲追加落盘并清空缓冲。

# Inputs
String 包，value 为日志器名。

# Outputs
data 无负载，msg "success"。

# Behavior
同步执行：打开 <dir>/<名>.log 追加写入缓冲后清空；空缓冲重复调用无副作用（幂等）。

# Notes
日志器不存在 → Failed；落盘 I/O 在端点调用线程内、持文件表读锁完成（期间阻塞其它文件日志写入与查找）；打开文件失败仅 ERROR 留痕且缓冲保留；pkg 非 String → Failed。
"""
LOG_DELETE_LOGGER_DESC = """
# Overview
删除指定名字的日志器：控制台表与文件表各删一次同名项。

# Inputs
String 包，value 为日志器名。

# Outputs
data 无负载，msg "deleted console:<N> file:<M>"（N/M 为各表实际删除条数）。

# Behavior
同步执行：分段持两把写锁不嵌套；文件日志器最后引用锁外释放，析构时自动把缓冲落盘（缓冲内容不丢）；两表都不存在同名项 → Failed。

# Notes
不删除磁盘上的 .log 文件（清理归调用方）；名空 → Invalid；"main" 控制台日志器也可被删除，之后再次发日志会按名重建；pkg 非 String → Failed。
"""
LOG_ON_CONSOLE_LOG_BY_LEVEL_DESC = """
# Overview
打开 Log 包指定级别的控制台输出门控：m_logger 为空改全局主闸，非空改指定控制台日志器的逐器门控。

# Inputs
Log 包：m_level 目标级别（必填）；m_logger 日志器名（空=全局主闸路径，非空=逐器路径）；m_log/m_tag 忽略。

# Outputs
data 无负载，msg "success"（未知 m_level 亦返回 Succeeded，仅 WARN 留痕）。

# Behavior
同步执行：全局路径改写进程级开关；逐器路径表读锁取引用后锁外 set_level_on；幂等。

# Notes
逐器路径不惰性创建，日志器不存在 → Failed；两级 AND 生效，任一级关闭即不输出；pkg 非 Log → Failed。
"""
LOG_OFF_CONSOLE_LOG_BY_LEVEL_DESC = """
# Overview
关闭 Log 包指定级别的控制台输出门控：m_logger 为空改全局主闸，非空改指定控制台日志器的逐器门控。

# Inputs
Log 包：m_level 目标级别（必填）；m_logger 日志器名（空=全局主闸路径，非空=逐器路径）；m_log/m_tag 忽略。

# Outputs
data 无负载，msg "success"（未知 m_level 亦返回 Succeeded，仅 WARN 留痕）。

# Behavior
同步执行：全局路径改写进程级开关；逐器路径表读锁取引用后锁外 set_level_on；幂等。

# Notes
逐器路径不惰性创建，日志器不存在 → Failed；两级 AND 生效，任一级关闭即不输出；pkg 非 Log → Failed。
"""
LOG_ON_FILE_LOG_BY_LEVEL_DESC = """
# Overview
打开 Log 包指定级别的文件输出门控：m_logger 为空改全局主闸，非空改指定文件日志器的逐器门控。

# Inputs
Log 包：m_level 目标级别（必填）；m_logger 日志器名（空=全局主闸路径，非空=逐器路径）；m_log/m_tag 忽略。

# Outputs
data 无负载，msg "success"（未知 m_level 亦返回 Succeeded，仅 WARN 留痕）。

# Behavior
同步执行：全局路径改写进程级开关；逐器路径表读锁取引用后锁外 set_level_on；幂等。

# Notes
文件侧主闸与控制台侧互相独立；逐器路径不惰性创建，日志器不存在 → Failed；pkg 非 Log → Failed。
"""
LOG_OFF_FILE_LOG_BY_LEVEL_DESC = """
# Overview
关闭 Log 包指定级别的文件输出门控：m_logger 为空改全局主闸，非空改指定文件日志器的逐器门控。

# Inputs
Log 包：m_level 目标级别（必填）；m_logger 日志器名（空=全局主闸路径，非空=逐器路径）；m_log/m_tag 忽略。

# Outputs
data 无负载，msg "success"（未知 m_level 亦返回 Succeeded，仅 WARN 留痕）。

# Behavior
同步执行：全局路径改写进程级开关；逐器路径表读锁取引用后锁外 set_level_on；幂等。

# Notes
文件侧主闸与控制台侧互相独立；逐器路径不惰性创建，日志器不存在 → Failed；pkg 非 Log → Failed。
"""
LOG_LOGGERS_DESC = """
# Overview
按类型列出日志器名清单（控制台表或文件表）。

# Inputs
String 包，value 必须是 "console" 或 "file"。

# Outputs
data 为 StringArray 包（字典序），msg "ok"。

# Behavior
同步快照读取，持对应表读锁拷贝排序；幂等。

# Notes
其他取值 → Failed（msg 带实际类型值）；pkg 非 String → Failed。
"""
LOG_LOGGER_DESC = """
# Overview
按类型 + 名查询单日志器内省快照：逐器六级别门控态、proxy_on、文件日志器目录。

# Inputs
LoggerQuery 包：m_logger_name 日志器名；m_type "console" 或 "file"（两表允许同名，由 m_type 消歧）。

# Outputs
data 为 LoggerInfo 包（logger_name/type/dir/logger_state），msg "ok"。

# Behavior
同步执行：按类型单表精确查找，锁内取引用、锁外构造快照；幂等。

# Notes
logger_state 六级别为逐器门控真值（非全局主闸），proxy_on 为该侧全局代理态且控制台/文件各自独立；m_type 未知或名未命中 → Failed；pkg 非 LoggerQuery → Failed。
"""
LOG_ALL_DESC = """
# Overview
全量日志器内省快照：控制台段在前、文件段在后。

# Inputs
无（不带请求包）。

# Outputs
data 为 LoggerInfoList 包（段内按字典序），msg "ok"。

# Behavior
同步执行：先在表锁外分别取两侧 proxy_on，再按 console→file 锁序嵌套双读锁构造快照；幂等。

# Notes
跨两表快照非原子一致；任一侧为空时仅少该段，不返回 Failed；条目状态为逐器门控真值，全局主闸请用 /console_global_state 与 /file_global_state。
"""
LOG_FILE_GLOBAL_STATE_DESC = """
# Overview
文件全局主闸态内省：六级别开关 + 文件代理是否已装。

# Inputs
无（不带请求包）。

# Outputs
data 为 LoggerState 包（trace_on/debug_on/info_on/warn_on/error_on/fatal_on/proxy_on），msg "ok"。

# Behavior
同步读取进程级开关与文件代理锁；幂等。

# Notes
六级别为独立字段，组合结果不构成原子快照（并发切换中可能读到新旧混合）；与控制台主闸互相独立。
"""
LOG_CONSOLE_GLOBAL_STATE_DESC = """
# Overview
控制台全局主闸态内省：六级别开关 + 控制台代理是否已装。

# Inputs
无（不带请求包）。

# Outputs
data 为 LoggerState 包（trace_on/debug_on/info_on/warn_on/error_on/fatal_on/proxy_on），msg "ok"。

# Behavior
同步读取进程级开关与控制台代理锁；幂等。

# Notes
六级别为独立字段，组合结果不构成原子快照（并发切换中可能读到新旧混合）；与文件主闸互相独立。
"""
