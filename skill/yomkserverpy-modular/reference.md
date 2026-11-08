# YomkServerPy API 参考

API 形态为**七个链式域入口**：`server()` / `ctx()` / `fn_pool()` / `event_loop()` / `log()` / `log_mgr()` / `srv_info()`。累积环节（`url/msg/srv/key/group/value/fn_name/fn/loop/handle_fn/msg/tag/...`）前置定位并返回 builder 自身，终结环节（`request/init/create/add/remove/set/install/call/post/get/apply/run` 与内省读族）无参执行并返回结果。链须单表达式内联（`log()` 域在调用行捕获位置前缀，禁止缓存 builder）。

## 状态码约定

```python
class ResStatus(Enum):
    eInvalid = -1   # 无效：入参非法 / 未初始化守卫（别名 Invalid）
    eOk = 0         # 成功（别名 Succeeded）
    eNo = 1         # 失败：含服务/键/函数/循环等存在性未命中（别名 Failed）

class CheckStatus(Enum):
    eAccept = 0     # 接受
    eReject = 1     # 拒绝

class Response:
    status: ResStatus   # 本次请求的状态
    msg: str            # 对状态的说明（不携带返回数据）
    data: Any           # 携带的返回数据
    def __init__(self, status=ResStatus.eInvalid, msg="", data=None)
```

三态口径（全框架一致）：

| 态 | 别名口径 | e 口径 | 语义 |
|----|----|----|----|
| 成功 | `Succeeded` | `eOk (0)` | 调用/写入成功 |
| 失败 | `Failed` | `eNo (1)` | 执行失败与**存在性未命中**（not-found 恒归此态，不得归 Invalid）：服务/键/函数/循环/日志器不存在、weak_func 失效丢弃、停止后投递被拒 |
| 无效 | `Invalid` | `eInvalid (-1)` | **入参非法**（空名/空值/空函数）与**服务器未初始化守卫** |

字段语义（固化口径）：`status` 表示本次请求的状态；`msg` 仅作对状态的说明（成功 `"ok"`、错误为契约消息），**返回数据一律放 `data`**。各域写路径链式终结收敛为 `ResStatus`（不带 msg），契约细节经裸链 `server().url(u).msg(pkg).request()` 查 `Response.msg`。

**未初始化守卫（全框架统一约定）**：

| 入口形态 | 未初始化时行为 |
|---|---|
| `server().url(u).request()` / `.status()` / 各域写与内省终结 | `Invalid`，守卫消息 `"YomkServer is not init"`（裸链可查），记录错误诊断日志，不抛异常 |
| `server().srv(cls).create()` / `.add()` / `.remove()` | `-1` |
| `server().url(u).async_(cb).request()` | 静默丢弃，回调不触发 |
| `ctx().key(k)...get()` | 原样返回调用方默认值 |
| `log()...` 发射终结 | `Invalid` |
| `version()` / `instance()` / `shutdown()` | **免初始化安全**（`instance()` 未初始化返回 `None`） |

类型化读 `get()` 未命中回退默认值（链式形态直接得默认值本体；对应裸链 `status` 为 `Failed`）：键名为空 `"key is empty, return default value."`、键不存在 `"key is not exist, return default value."`。

## 核心类

```python
class YomkServer:                            # 底层单例类（业务代码经 server() 链消费，不直接持有）
    def add_service(self, service: YomkService) -> int   # 0 成功 / -1 失败（内部自动 init，失败回滚注销）
    def request(self, url: str, pkg) -> Response
    def async_request(self, url: str, pkg, callback) -> None

class YomkBoot(ABC):                         # 独立 boot 域：from yomk.boot import YomkBoot
    def before(self) -> int   # 服务启动前：创建资源
    def start(self) -> int    # 注册并启动服务
    def after(self) -> int    # 服务启动后：初始化调用

class YomkService:
    def __init__(self, server: YomkServer)
    def get_name(self) -> str
    def set_name(self, name: str)            # 服务名以 / 开头
    def init(self) -> int                    # 子类实现（注册时由框架调用），非 0 注册失败
    def deinit(self) -> None                 # 注销/shutdown 时由框架调用（默认空实现，可覆盖停线程/注销外部资源）
    def install_func(self, name: str, function, msg_name="", description="") -> None
        # 注册本服务功能函数（name 须以 / 开头）；msg_name/description 仅内省元数据；
        # 成员函数注册即自动 weak_func 弱绑定
    def weak_func(self, func, discard="response") -> callable
        # 弱绑定包装：服务删除/注销后回调自动判活失效，见下表
    @property
    def func_infos(self) -> dict[str, FuncInfo]
        # 本服务函数表内省快照（读锁保护，键为函数名）
    def invoke(self, name: str, pkg) -> Response
        # 本服务功能函数直调（name 须以 / 开头）；空名/不带 / 前缀 Failed
        # "function name parse error: {name}, please start with /"；函数不存在 Failed
    def request(self, url: str, pkg=None) -> Response    # 跨服务调用（转发所属 Server）
    def async_request(self, url: str, pkg=None, callback=None) -> None
```

### weak_func 与 discard 三形态

成员函数注册到**外部子系统**（函数池 / 事件循环 / Context checker·monitor / 异步响应回调）时必须 `self.weak_func(方法)`；`install_func` 注册到**本服务**函数表已自动弱绑。**双层判活**：包装层查 `mark_deleted` 注销标志（`deleted()`），调用层查服务表命中，两层独立生效；服务删除即刻失效。失效时按 `discard` 声明返回契约值（三种形态均同时打错误日志）：

| discard | 失效返回值 | 适用回调 |
|---|---|---|
| `"response"`（缺省） | `Response(Failed, "service has been deleted or unregistered, callback ignored.")` | 函数池函数、事件循环处理函数、异步响应回调 |
| `"check"` | `CheckStatus.eAccept`（放行，不拦截写入） | Context checker |
| `"none"` | `None` | Context monitor、void 语义回调 |

## 数据结构

```python
class Log:                                   # 日志代理回调收的包
    m_level: int        # 级别：eTrace=0 / eDebug=1 / eInfo=2 / eWarn=3 / eError=4 / eFatal=5
    m_log: str          # 日志内容（变参拼接后的文本）
    m_logger: str       # 日志器名
    m_tag: str          # tag

class Context:                               # monitor 回调收的键值快照
    def __init__(self, key="", value=None, group="main")

class ContextChecker:
    def __init__(self, key="", check_func=None, group="main")   # check_func: (Context) -> CheckStatus

class ContextMonitor:
    def __init__(self, key="", monitor_func=None, async_monitor_func=None, group="main")
        # monitor_func: (Context) -> None；async 形态经 ctx().monitor_fn(fn).async_(True).apply() 设置

class ContextKeyInfo:                        # /YomkContext/key 单查数据；亦是 ContextGroupInfo.keys 元素
    key / type_name / checker_on / monitor_count / async_monitor_count / group
    # type_name：值类型名（值为 None 时空串）；group：键所属组名

class ContextGroupKey:                       # 组寻址载体（/destroy 与 /key 请求报文）
    def __init__(self, group="main", key="")

class ContextGroupInfo:                      # /YomkContext/all 元素：组 → 键元信息清单
    group_name: str; keys: list[ContextKeyInfo]   # 键按字典序

class Function:                              # 函数池条目
    def __init__(self, name, func, msg_name="", description="")  # func: (Any) -> Response

class CallFunction:
    def __init__(self, name, pkg)

class FuncInfo:                              # 功能函数元信息（函数池/服务内省单查与清单同族）
    func_name: str      # 路由名，带 "/" 前缀
    msg_name: str       # 期望消息类型名（未声明为空，仅内省元数据）
    description: str    # 函数说明（使用者自定义，框架透传不解析）

class Event:                                 # 事件循环投递包
    def __init__(self, event_loop_name="", pkg=None, func=None, tag="")
    # 属性：m_eventLoopName, m_pkg, m_serviceFunc, m_eventId, m_response, m_waitCallback, m_tag

class EventLoopPkg:                          # start 请求报文
    def __init__(self, event_loop_name="", default_service_func=None, msg_name="")

class EventLoopInfo:                         # /YomkEventLoop/loop 单查与 /all 元素
    loop_name: str      # 循环名
    running: bool       # worker 是否运行中
    pending: int        # 积压事件数
    has_default_func: bool
    msg_name: str       # 默认处理函数期望消息类型名（可空）
    next_tags: list[str]  # 队列头起最多 tag_count 条事件 tag（存原值，空串保留）

class LoggerInfo:                            # /YomkLogger/logger 单查与 /all 元素
    logger_name: str; type: str             # type："console" 或 "file"（两表允许同名，按类型精确单查）
    dir: str                                # file 类型的日志目录（console 时空串）
    logger_state: LoggerState               # 该器门控层真值（见下）

class LoggerState:                           # 日志器状态：六级别开关 + 代理位（共七开关）
    trace_on / debug_on / info_on / warn_on / error_on / fatal_on: bool
    proxy_on: bool                          # 对应侧（console/file）代理是否挂载

class ServerServiceInfo:                     # /YomkServerInfo/all 元素
    service_name: str                       # 带 "/" 前缀
    funcs: list[FuncInfo]                   # 与 /function 单查同族
```

## 链式域 API 速查

### server() 域——生命周期 / 请求 / 服务管理 / 拓扑内省

| 链 | 终结与返回 |
|-----|------|
| `server().async_thread(n)` | `.init()`：初始化单例并自动启动内置服务（Logger/Context/EventLoop/FunctionPool/ServerInfo），返回服务器实例；`n` 异步线程池大小（省略 = 0 自动检测），仅首次生效；进程内幂等 + atexit 兜底 |
| — | `.version()` → `str` 框架版本（免初始化）；`.instance()` → `YomkServer` 单例（未初始化/shutdown 后 `None`）；`.shutdown()` → 优雅关闭（免初始化空转） |
| `server().url(u).msg(pkg)` | `.request()` → `Response`（阻塞至应答）；`.status()` → 三态 `ResStatus` |
| `server().url(u).async_(cb)` | 切入异步分面继续 `.msg(pkg).request()` → `None`：投递内置线程池立即返回，结果经 `cb` 送达；**`cb` 传 `None` 仅投递不取回**；`.msg()` 可放 `.async_()` 前后任意侧 |
| `server().srv(cls, name="")` | `.create()` → `int`：创建并注册派生服务（`name` 省略沿用构造默认名）；0 成功，-1 失败（未初始化 / `init()` 非 0 回滚注销） |
| `server().srv_name(n).srv_ptr(s)` | `.add()` → `int`：注册已有实例；累积非空 `srv_name` 时注册前先改名；守卫拒绝时实例仍归调用方 |
| `server().srv_name(n)` | `.remove()` → `int`：按名注销并触发 `deinit`；-1 三因：不存在 / 名称为空 / 未初始化 |
| `server().boot_ptr(boot)` | `.run()` → `int`：先 init 再 before → start → after；任一阶段非 0 立即中止并**原样传播**；`boot` 省略 = 仅 init 返回 0 |
| `srv_info()` | `.services()` 服务名清单 / `.srv_name(n).functions()` 函数清单 / `.fn_url(u).get_info()` 单函数元信息 / `.all()` 全量拓扑（`ServerServiceInfo`），均 → `Response` |

服务管理语义：

- **`add()`/`create()` 同名热替换**：同名服务被再次注册时，旧服务先置位注销标志并 `deinit`，新服务随后 `init`（任意时刻同名服务至多一次活跃生命周期；诊断日志 `"service already exists -> {name}, update to current service"`）。注册成功后实例归框架服务表持有，调用方不得再复用。
- **deinit 四触发时机**：`srv_name(n).remove()`、`shutdown()`、同名被替换、进程退出 atexit 兜底。
- **shutdown 排空序列**：拒新 → 排空请求池 → join 异步请求池 → 清空服务表逐个 `deinit`；幂等，假定主线程调用。**关闭后不支持二次 init**（once 标志把关，全部入口回到守卫态）；`instance()` 在 shutdown 后置空返回 `None`。

拓扑内省注记：`functions()/get_info()/all()` 命中 `msg` 恒 `"ok"`；`functions()` 服务不存在返回**空清单**（非 Failed），`fn_url(...).get_info()` 未命中 `Failed`。

### log()/log_mgr() 域——发射 / 门控 / 代理 / 前缀 / 文件日志器 / 内省

发射链（终结返回三态 `ResStatus`；六级别方法 `debug/info/warn/error/trace/fatal(*args)` 变参逐个拼接，`bool` 输出 `true/false`）：

| 链 | 行为 |
|---|---|
| `log().info(*args)` | 主控制台输出，tag 缺省 `"main"`；**tag 即控制台日志器名，首次使用惰性创建** |
| `log().tag(t).info(*args)` | 指定 tag |
| `log().to_sink(name).info(*args)` | 写具名文件日志器（`.tag(t)` 可叠加）；未知名 `Failed`、**不惰性创建**——须先 `log_mgr().dir(d).file(name).create()` |

发射链同步阻塞：内部经 `/YomkLogger` 以请求域投递至 Logger 内置服务，返回态表示该条已被受理（门控/代理在受理内生效）。输出行格式 `[YYYY-MM-DD HH:MM:SS.mmm] [级别] [tag] [位置] 内容`（级别标签定宽 5，如 `[INFO ]`；位置前缀默认仅行号）。

管理链：

| 链 | 终结与说明 |
|---|---|
| `log_mgr().console(name="").level(lv)` | `.enable()` / `.disable()` → `ResStatus`；控制台级别门控，`.level(lv)` 缺省 `Log.eInfo`（六级之一）；`name` 省略/空 = **全局主闸**，具名 = 逐器门控（未命中 `Failed` `"console logger not found."`，不惰性创建、不触碰主闸） |
| `log_mgr().file(name="").level(lv)` | `.enable()` / `.disable()`；文件侧同构（未命中 `"file logger not found."`）；控制台与文件两套主闸**相互独立**，输出/落盘取**主闸 × 逐器两层 AND** |
| `log_mgr().console()/file().proxy(fn)` | `.apply()`；代理回调收 `Log` 包：返回 `False` 消费该条（控制台不输出/文件不落盘）、`True` 穿透默认行为；传 `None` 卸载恢复默认；两侧代理各自独立 |
| `log_mgr().location().show_file(b).show_line(b).show_func(b)` | `.apply()` 恒 `Succeeded`；位置前缀三开关（进程级、默认关/开/关 = 仅行号），前缀按 `[file:line:func]` 以 `:` 拼接、关断段整体省略 |
| `log_mgr().dir(d).file(name)` | `.create()` → `ResStatus`（落盘 `d/name.log`）；`.dir()` 须先于 `.file()` 累积；空名/空目录 `Invalid`，同名重复创建 `Failed` |
| `log_mgr().file(name)` | `.flush()` 缓冲落盘（未知名 `Failed`）；`.remove()` 删除日志器：同名 console+file **两表一并清除**（两表皆未命中 `Failed`），释放时未落盘缓冲自动落盘，**不删除磁盘 `.log` 文件** |
| `log_mgr().console()/file()` | `.list()` 该类型日志器名（字典序）/ `.console(name).get_info()` / `.file(name).get_info()` 单器 `LoggerInfo` / `.console().status()` / `.file().status()` 全局主闸 `LoggerState`；`.all()` 全部日志器（每条内嵌 `LoggerState` 门控层真值），均 → `Response` |

内省归因：`get_info`/`all()` 报**逐器门控层**真值（未具名门控恒全 on）；条目 on 但无输出时查 `status()` 确认对应侧**全局主闸**——两层独立、可唯一归因。空名 console 具名门控回退缺省名（诊断 `"console logger name is empty, use main"`）。未知级别数值降级 Info 输出。

### ctx() 域——创建 / 读写 / 回调 / 组开关 / 内省

| 链 | 终结与返回 |
|---|---|
| `ctx().key(k).group(g="main").value(v)` | `.create()` → `ResStatus` |
| `ctx().key(k).group(g).value(v)` | `.set()` → `ResStatus`（整体替换） |
| `ctx().key(k).group(g)` | `.destroy()` → `ResStatus` |
| `ctx().key(k).group(g).msg(cls)` | 切入类型化读分面，`.with_default(d).get()` → 值本体（`with_default` 省略回退 `None`）；**`cls` 仅类型指定位，不做运行期校验**（Python 适配） |
| `ctx().key(k).group(g).checker_fn(fn)` | `.apply()` → `ResStatus`；fn: `(Context) -> CheckStatus`；未累积任何回调直接 `apply()` 返回 `Invalid` |
| `ctx().key(k).group(g).monitor_fn(fn).async_(on=True)` | `.apply()` → `ResStatus`；`.async_()` 可省略 = 同步 |
| `ctx().group(g).checker()` / `.monitor()` | 组级开关分面 `GroupSwitch`：`.enable()` / `.disable()` → `ResStatus`（**建组默认关**，须显式开启；开关只关断不删回调） |
| `ctx().group(g)` / `.key(k).group(g)` / — | `.keys()` 组内键名 / `.info()` 单键 `ContextKeyInfo` / `.groups()` 全部组名 / `.all()` 跨组聚合 `ContextGroupInfo`，均 → `Response` |

语义注记：

- **写路径失败契约**（裸链查 msg，`status` 均 `Failed`）：键名为空 `"key is empty"`；`create` 时 value 为空 `"value is empty"`、键已存在 `"key already exists"`；`set`/回调注册时键不存在 `"key is not exist"`；值类型与现值不一致 `"context type not match"`（服务端 set 侧仍校验）；被门控拦截 `"checker reject set context"`；组名为空 `"group name is empty"`。
- **键级回调前提**：`checker_fn`/`monitor_fn` 只能注册到**已存在**的键（先 `create()` 再注册，未命中 `"key is not exist"`）。
- **Checker 生效条件**：组开关 ON **且**该键已注册 checker 才参与门控；门控在**写锁内**执行，回调内重入 set 会死锁。
- **Monitor 执行语义**：`async_(True)` 经该组单线程池按 set 提交序**保序**送达并排空；同步形态在写锁外即时执行（并发 set 不保证跨线程序）。回调收本次 set 的 `Context` 键值快照，**不保证快照实时性**（读最新值请再 `get()`）；原地修改快照与并发读竞争，禁止。
- **组约定**：`.group()` 省略 = 缺省 `"main"`；非 main 组首次寻址惰性自动创建（组名唯一、同名复用、**不支持销毁组**、组表只增不减）；异步监控池每组自持（单线程）。
- 内省：`info()` 命中 `msg` 恒 `"ok"`、`data` 为 `ContextKeyInfo`（含所属 `group`）；空 key 与不存在 key 返回 `Failed`。

### event_loop() 域——启停 / 投递 / 内省

| 链 | 终结与返回 |
|---|---|
| `event_loop().loop(name).handle_fn(fn).msg_name(m)` | `.start()` → `ResStatus`；`handle_fn` 循环默认处理函数、`msg_name` 期望类型名（内省元数据），均可省略；**同名 start 不重建**：复用既有实例触发启动（停止后续跑），本次累积的 `handle_fn`/`msg_name` **不生效** |
| `event_loop().loop(name)` | `.stop()` → `ResStatus`（停线程、**保留队列事件**）；`.destroy()` → `ResStatus`（停线程 + 清空队列 + 移出循环表，不可续跑） |
| `event_loop().msg(data).loop(name).handle_fn(fn).tag(t)` | `.post()` → `ResStatus`（**仅入队结果，非执行结果**）；`.post_wait()` → `ResStatus`（阻塞至事件执行完成）；`handle_fn`/`tag` 可省略 |
| `event_loop().loop(name).tag_count(n)` / — | `.get_info()` 单循环 `EventLoopInfo`（含队列头 n 条 tag 采样，n 省略 = 3）/ `.list()` 循环名清单 / `.all()` 全量，均 → `Response` |

语义注记：

- 同循环 FIFO 单线程、跨循环并行；随 `server().init()` 内置启动。
- 事件未带 `handle_fn` 回落循环默认处理函数；两者皆空该事件**静默出队**不执行。
- 失败契约（`Failed`）：循环不存在 `"event loop not exist"`；已停止（未销毁）投递 `"event loop not running, post rejected"`、`post_wait` 同族 `"event loop not running, post wait rejected"`。
- 需要取回执行结果：裸链 `server().url("/YomkEventLoop/post_wait").msg(Event(...)).request()`，`data` 为 `Event` 对象（`m_eventId`/`m_response`）；或经 Context 中转。
- 工作线程内 `post_wait` 触发死锁自检：直接本线程执行当前事件并返回（诊断 `"EventLoop deadlock: post wait in worker thread..."`）。
- 服务成员函数作处理函数须 `self.weak_func(...)`。

### fn_pool() 域——注册 / 调用 / 内省

| 链 | 终结与返回 |
|---|---|
| `fn_pool().fn_name(name).fn(f).msg_name(m).description(d)` | `.install()` → `ResStatus`；`msg_name`/`description` 内省元数据可省略；**同名重复 install 即运行时热替换**（`Succeeded`，诊断 `"find function name is already exist, update to current function"`） |
| `fn_pool().fn_name(name)` | `.uninstall()` → `ResStatus`（名字不存在 `Failed`） |
| `fn_pool().fn_name(name).msg(data)` | `.call()` → `Response`（**阻塞至函数执行完成**；`msg` 可省略 = 空消息；名字不存在 `Failed`） |
| `fn_pool().fn_name(name)` / — | `.get_info()` 单函数 `FuncInfo` / `.list()` 函数名清单 / `.all()` 全量 `FuncInfo`，均 → `Response` |

语义注记：

- 入参非法（空名/空函数）`Invalid`，契约 `"funcName or func is empty"`；not-found 归 `Failed` 不归 `Invalid`（三态口径一致）。
- 函数体收到的 `pkg` 即调用侧 `.msg()` 对象；函数返回的 `Response` 整体作为 `call()` 返回值（结果在 `data`）。
- 成员函数注册须 `self.weak_func(...)`——服务删除后 `call()` 得 `Failed` + 契约消息。
- 注册/注销的终结动词定名为 `install/uninstall`。

## URL 格式

所有请求使用统一 URL：`/服务名/功能函数名`

示例：`/ConfigService/load`、`/UserService/get_user`、`/YomkContext/create`

## 常用失败契约字符串

契约消息原文（与 `src/yomk/` 源实串一致；测试与排障以此为准）：

| 场景 | status | 契约 msg |
|---|---|---|
| 任意链式终结 / 请求，服务器未初始化 | `Invalid` | `"YomkServer is not init"` |
| weak_func 失效丢弃（discard="response"） | `Failed` | `"service has been deleted or unregistered, callback ignored."` |
| Context create/注册/读取，键名为空 | `Failed` | `"key is empty"` |
| Context create，value 为空 | `Failed` | `"value is empty"` |
| Context create，键已存在 | `Failed` | `"key already exists"` |
| Context set / 键级回调注册，键不存在 | `Failed` | `"key is not exist"` |
| Context set，值类型与现值不一致 | `Failed` | `"context type not match"` |
| Context set，被 checker 拦截 | `Failed` | `"checker reject set context"` |
| Context 组名为空 | `Failed` | `"group name is empty"` |
| Context get 未命中回退（键名为空 / 键不存在） | `Failed` | `"key is empty, return default value."` / `"key is not exist, return default value."` |
| EventLoop 投递/销毁/查询，循环不存在 | `Failed` | `"event loop not exist"` |
| EventLoop 已停止时投递 | `Failed` | `"event loop not running, post rejected"` |
| EventLoop 已停止时同步投递 | `Failed` | `"event loop not running, post wait rejected"` |
| FunctionPool install 空名/空函数 | `Invalid` | `"funcName or func is empty"` |
| 日志具名门控/查询未命中（console / file） | `Failed` | `"console logger not found."` / `"file logger not found."` |
| 日志按类型查询，type 非 `"console"/"file"` | `Failed` | `"unknown logger type."` |

诊断日志（非返回值契约）：服务同名热替换 `"service already exists -> {name}, update to current service"`、函数池热替换 `"find function name is already exist, update to current function"`、空 console 名回退 `"console logger name is empty, use main"`、死锁自检 `"EventLoop deadlock: post wait in worker thread..."`。

语义细节与更多注记以仓库根 `docs/` 七篇为权威源：[YomkServer](../../docs/YomkServer.md)、[YomkMsg](../../docs/YomkMsg.md)、[YomkLogger](../../docs/YomkLogger.md)、[YomkContext](../../docs/YomkContext.md)、[YomkEventLoop](../../docs/YomkEventLoop.md)、[YomkFunctionPool](../../docs/YomkFunctionPool.md)、[YomkServerInfo](../../docs/YomkServerInfo.md)。
