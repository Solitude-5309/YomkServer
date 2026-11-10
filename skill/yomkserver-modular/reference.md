# YomkServer API 参考

## 链式域入口（七个）

框架全部操作经七个链式域入口暴露，累积方法（`url/msg/srv/key/loop/fn_name/tag/dir` 等）前置、任意顺序组合，终结动作（`request/create/set/start/call/remove/list/get_info` 等）一律无参：

| 入口 | 模块 | 职责 |
|------|------|------|
| `yomk::server()` | YomkServer | 请求通信、服务器生命周期、服务管理 |
| `yomk::srv_info()` | YomkServerInfo | 拓扑内省：服务清单 / 函数清单 / 单函数元信息 / 全量拓扑 |
| `yomk::log()` | YomkLogger | 日志发射（六级 × 模板参数包） |
| `yomk::log_mgr()` | YomkLogger | 日志管理：门控 / 代理 / 位置前缀 / 文件日志器生命周期 / 内省 |
| `yomk::ctx()` | YomkContext | 跨服务强类型键值存储 + Checker/Monitor |
| `yomk::event_loop()` | YomkEventLoop | 具名单线程 FIFO 事件循环 |
| `yomk::fn_pool()` | YomkFunctionPool | 动态函数池（注册 / 调用 / 热替换） |

**builder 通用规则（必须遵守）**：

- builder 按值传递，链式表达式须**单表达式内联**使用，接收变量一律 `auto` 或不接收；内部持 `string_view`，缓存 builder 复用即悬垂。
- `yomk::log()` 域入口在**调用行**捕获位置前缀，缓存 builder 还会导致位置错误。
- 按模块头导入（无伞形聚合头）：`<yomk/server/YomkServerApi.h>`、`YomkServerInfoApi.h`、`YomkLoggerApi.h`、`YomkContextApi.h`、`YomkEventLoopApi.h`、`YomkFunctionPoolApi.h`；自定义消息包含 `<yomk/msg/YomkMsg.h>` 使用 `YOMK_DEFINE_MSG` 与 `yomk::msgs::unpack`，标准类型见 `<yomk/msg/YomkStdMsg.h>`，核心载荷消息见 `<yomk/server/YomkServerMsg.h>`，模块专属消息/载荷见 `<yomk/{ctx,event_loop,fn_pool,log,srv_info}/Yomk{Module}Msg.h>` / `Yomk{Module}Payload.h`（对应 `*Api.h` 已按需包含）；`YOMK_DEFINE_MSG` 生成类型位于 `yomk::msgs` 命名空间。

## 状态码约定

| 状态 | 值 | 语义 |
|------|----|------|
| `ResStatus::Succeeded` | 0 | 调用成功 |
| `ResStatus::Failed` | 1 | not-found / 目标不存在 / 被拒绝 / 条件不满足 |
| `ResStatus::Invalid` | -1 | 入参非法（空名等）或服务器未初始化 |

未初始化守卫（全框架统一约定）：

| 入口形态 | 未初始化行为 |
|---|---|
| request / status / 各域写与内省终结 | `ResStatus::Invalid` |
| create / add / remove（服务注册类终结） | `-1` |
| `.async()` 异步请求 | 静默丢弃，回调不触发 |
| `ctx().msg<T>().get()` 类型化读 | 原样返回调用方默认值 |
| `.version()` / `.instance()` / `.shutdown()` | 免初始化安全 |

均记录错误诊断日志，不抛异常。判断"目标是否存在"用 `Failed`，判断"参数是否合法"用 `Invalid`，不要混用。

## 核心类型

```cpp
using YomkMsgPtr = std::shared_ptr<YomkMsg>;  // 消息包基类指针，跨边界统一流转

// yomk::payload::Response —— 统一响应（三态状态码）
struct Response {
    ResStatus status;   // 本次请求的状态
    std::string msg;    // 对状态的说明（成功多为 "ok" 或描述文本，失败为契约消息；不携带返回数据）
    YomkMsgPtr data;    // 携带的返回数据（结构化消息包）
};

using YomkServiceFunc  = std::function<yomk::payload::Response(YomkMsgPtr pkg)>;
using YomkResponseFunc = std::function<void(yomk::payload::Response response)>;
```

字段口径固化：返回数据一律放 `data`，`msg` 仅作状态说明。

**内置数据结构**：

```cpp
namespace yomk {
    struct Event {
        std::string eventLoopName; YomkMsgPtr pkg;
        YomkServiceFunc serviceFunc; std::uint64_t eventId;
        yomk::payload::Response response; std::function<void()> waitCallback;
        std::string tag;   // 用户事件标记，post 时可选，仅内省展示
    };
    // logger=日志器名（服务端按其选择/惰性创建）；tag=记录标签（随记录显示 [tag]）
    // LogLevel 为命名空间级 enum class（见 YomkLoggerPayload.h），非 Log 内嵌枚举
    struct Log { payload::LogLevel level; std::string log; std::string logger; std::string tag; };
    struct Context { std::string key; YomkMsgPtr value; std::string group = "main"; };  // group=key 所属组名（缺省 main）
    enum class CheckStatus{Accept, Reject};   // checker 门控结论（定义见 YomkContextPayload.h）
}
namespace yomk { struct FuncInfo {           // 功能函数元信息（内省用，定义见 YomkServerInfoPayload.h）
    std::string funcName;      // 路由名（/ 开头）
    std::string msgName;       // 期望消息类型名（声明处第三参 / msg_name()，可为空）
    std::string description;   // 函数说明（第四参 / description()，自由字符串，可为空）
}; }
```

**回调签名（Context / Logger）**：

```cpp
using YomkContextMonitorFunc  = std::function<void(const yomk::payload::Context& ctx)>;
using YomkContextCheckerFunc  = std::function<yomk::payload::CheckStatus(const yomk::payload::Context& ctx)>;
using YomkConsoleLogProxyFunc = std::function<bool(const yomk::payload::Log& log)>;
```

## YOMK_DEFINE_MSG 与解包

```cpp
struct MyData { std::string field1; int field2; };   // 数据类先在命名空间外定义
YOMK_DEFINE_MSG(MyData, MyData, d);   // 生成 yomk::msgs::MyData / MyDataPtr，成员名约定 d
```

- `yomk::msgs::MyData::shared(args...)` — 类内静态工厂，转发构造参数 → `shared_ptr` 消息包。
  **注意**：模板形参 `A&&...` 无法从花括号初始化列表推导，传聚合对象须带类型：`MyData::shared(MyData{"a", 1})`、`Int32Array::shared(std::vector<std::int32_t>{1,2,3})`。
- `yomk::msgs::MyData::shared(std::move(data))` — 右值重载移动入包，大对象免拷贝。
- `yomk::msgs::unpack<yomk::msgs::MyData>(pkg)` — 按运行期类型名解包，pkg 为空或类型不匹配返回 `nullptr`（不自动 return，调用方手动判空），不抛异常。
- 消息名（宏第二参）即运行期类型名 `kName`；各处声明的"期望消息类型名"字符串（`installFunc` 第三参、`fn_pool().msg_name()`、`event_loop().msg_name()`）与其保持一致，仅作内省元数据、不拦截调用。

**内置标准类型**（成员名均为 `d`，共 30 种）：`Bool`、`Char`、`UChar`、`Byte`、`Int8`、`Uint8`、`Int16`、`Uint16`、`Int32`、`Uint32`、`Int64`、`Uint64`、`Float32`、`Float64`、`String` 及对应 `XxxArray`（如 `StringArray`、`Int32Array`）。

## installFunc / weakFunc

```cpp
installFunc("/x", &C::m)                         // 成员函数直装本服务 funcMap（内部自动 weakFunc 包装）
installFunc("/x", &C::m, "MsgName")              // 第三参：期望消息类型名（字符串字面量，仅内省元数据）
installFunc("/x", &C::m, "MsgName", "Description")  // 第四参：函数说明（自由字符串，透传不解析）
weakFunc(&Class::Method)                         // 弱绑定成员函数（不装 funcMap），注册到外部子系统必用
```

弱绑定判活：回调触发时先 `weak_ptr.lock()`，服务已删除则安全丢弃，不会悬垂 `this`。同一 `weakFunc` 是泛型模板，按调用处目标 `std::function` 类型自动适配全部回调签名，丢弃行为按返回类型：功能函数/FunctionPool/EventLoop（`Response(YomkMsgPtr)` → `Failed`）、异步响应（`void(Response)` → 丢弃 + 错误日志 "service has been deleted or unregistered, callback ignored"）、checker（`CheckStatus(const Context&)` → 放行 `Accept`）、void monitor（→ 丢弃）。其他返回类型 → `Ret{}` 默认值。构造期绑定（init 之前）永久无效。

`YomkService` 还提供便捷成员：`invoke(funcName, pkg)`（不经 URL 路由直调本服务函数）、`request(url, pkg)` / `asyncRequest(url, pkg, cb)`（转调 server 域链），以及 `funcInfos()` 本服务元信息。

## 服务器域（yomk::server()）

### 请求与响应

| 链 | 终结与返回 |
|---|---|
| `.url(u).msg(pkg)` | `.request()` → `Response` 阻塞至服务应答；`.status()` → 三态 `ResStatus`（失败细节进诊断日志） |
| `.url(u).async(cb)` | 切入异步分面，`.msg(pkg)`（可在 `.async()` 前或后）后 `.request()` → `void`：投递到内置线程池立即返回，结果经 `cb` 送达；`cb` 传 `nullptr` 仅投递不取回 |

`.msg(pkg)` 可省略 = 空消息。

### 生命周期与服务管理

| 链 | 终结与返回 |
|---|---|
| `.async_thread(n)` | `.init()`：初始化单例并自动启动内置服务（Logger/Context/EventLoop/FunctionPool/ServerInfo）；`n` 异步线程池大小（省略 = 0 自动检测），仅首次生效；进程内幂等（call_once），挂 atexit 兜底销毁 |
| — | `.version()` 版本字符串（各域入口 `srv_info()/log_mgr()/ctx()/event_loop()/fn_pool()` 同样提供 `.version()` 模块版本终结，编译期常量直返）/ `.instance()` `shared_ptr<YomkServer>` 快照（未初始化 `nullptr`）/ `.shutdown()` 优雅关闭（未初始化空转）——三者免初始化安全 |
| `.srv<T>(name)` | `.create()` → `int`：创建并注册 `YomkService` 派生服务；`name` 省略沿用 T 构造默认名；0 成功，-1 失败（未初始化 / 未先调 `srv<T>` / `T::init()` 非 0 自动回滚） |
| `.srv_name(n).srv_ptr(s)` | `.add()` → `int`：注册已有实例，所有权移交框架；累积非空 `srv_name` 时注册前先改名；同名替换 = 旧服务置注销标志 + deinit 后安装新服务；守卫拒绝时指针仍归调用方 |
| `.srv_name(n)` | `.remove()` → `int`：按名注销并释放所有权；-1 = 不存在 / 名称为空 / 未初始化 |
| `.boot_ptr(cfg)` | `.run()` → `int`：先 init 再按 before → start → after 执行 `YomkBoot` 编排；`cfg` 裸指针所有权移交 `run`；任一阶段非 0 立即中止并传播；`boot_ptr` 省略 = 仅 init 返回 0 |

`shutdown()`：拒新 → 排空 → join 异步请求池，再清空服务表逐个 `deinit`；幂等，假定主线程；**关闭后不支持二次 init**。`deinit()` 四触发时机：`remove()` 单删、`shutdown()` 全关、`add()` 同名替换、进程退出 atexit 兜底。

### 拓扑内省（读终结，返回 `Response`）

`.services()` 服务名清单（StringArray）/ `.srv_name(n).functions()` 指定服务函数清单（FuncInfoList）/ `.fn_url(u).get_info()` 单函数元信息（FuncInfo）/ `.all()` 全量拓扑（ServerServiceInfoList：`serviceName` + `funcs`）。服务不存在时 `functions()` 为空清单。

## Context 域（yomk::ctx()）

| 场景 | 链 | 终结与返回 |
|---|---|---|
| 创建 | `.key(k).group(g).value(pkg)` | `.create()` → `ResStatus`（g 省略 = 缺省组 `"main"`） |
| 整体替换 | `.key(k).group(g).value(pkg)` | `.set()` → `ResStatus` |
| 类型化读 | `.key(k).group(g).msg<T>()` | `.with_default(d).get()` → `shared_ptr<T>`；`.with_default` 可省略 = 未命中回退 `nullptr` |
| 销毁键 | `.key(k).group(g)` | `.destroy()` → `ResStatus` |
| 键级校验门控 | `.key(k).group(g).checker_fn(fn)` | `.apply()` → `ResStatus` |
| 键级变更监听 | `.key(k).group(g).monitor_fn(fn).async(on)` | `.apply()` → `ResStatus`；`.async(on)` 可省略 = 同步 |
| 组级开关 | `.group(g).checker()` / `.monitor()` | `.enable()` / `.disable()` → `ResStatus`（**建组默认关**，须显式开启；回调保留不删除） |
| 内省 | `.group(g)` / `.key(k).group(g)` / — | `.keys()` 组内键名 / `.info()` 单键元信息 / `.groups()` 全部组名 / `.all()` 跨组聚合，均 → `Response` |

- 键级回调（`checker_fn`/`monitor_fn`）只能注册到**已存在**的键（否则 `Failed` "context key '{k}' does not exist"），即先 `create()` 再注册；monitors 为追加式（可注册多个）。
- Checker 生效条件：组开关 ON **且**该键已注册 checker；门控在写锁内执行，**回调内重入 set 会死锁**。Reject 拦截 set（`Failed` "set context key '{k}' rejected by checker"），monitor 不触发。
- Monitor 仅通知"发生了一次 set"并回传该次键值快照（不保证实时，留存请拷贝；需最新值回调内重读 `get()`）；`.async(true)` 经该组单线程池按 set 提交序保序送达；同步形态锁外即时执行、并发下不保证跨线程序、回调内重入 set 会递归。
- 组名约定：`.group()` 省略即 `"main"`；空组名 `Failed` "group name is empty"；非 main 组首次寻址惰性自动创建，组名唯一、**不支持销毁组**，异步监控池每组自持。
- 写路径失败契约：空键名 `Failed` "context key is empty"；create 值空 `Failed` "create context key '{k}' failed: value is empty"、键已存在 `Failed` "context key '{k}' already exists"；set 键不存在 `Failed` "context key '{k}' does not exist"、类型不符 `Failed` "context type not match: ..."。
- `get()` 返回只读快照，值对象发布后勿原地修改；类型化读按 `kName` 运行期比对，`T` 须为 `YomkMsg` 派生。

## EventLoop 域（yomk::event_loop()）

| 场景 | 链 | 终结与返回 |
|---|---|---|
| 启动 | `.loop(name).handle_fn(fn).msg_name(m)` | `.start()` → `ResStatus`；`handle_fn` 为循环默认处理函数、`msg_name` 期望类型名（均仅内省元数据），可省略 |
| 停止 | `.loop(name)` | `.stop()` → `ResStatus`（停线程**保留队列**） |
| 销毁 | `.loop(name)` | `.destroy()` → `ResStatus`（停线程 + 清空队列 + 移出循环表，不可续跑） |
| 非阻塞投递 | `.msg(data).loop(name).handle_fn(fn).tag(t)` | `.post()` → `ResStatus`（仅入队结果）；`handle_fn`/`tag` 可省略 |
| 阻塞投递 | 同上 | `.post_wait()` → `ResStatus`（阻塞等待事件执行完成） |
| 内省 | `.loop(name).tag_count(n)` / — | `.get_info()` 单循环状态 + 队列头 n 条 tag 采样（n 省略 = 3）/ `.list()` 循环名清单 / `.all()` 全量，均 → `Response` |

- **同名 `start()` 不重建**：复用既有实例并触发启动（停止后续跑），本次累积的 `handle_fn`/`msg_name` 不生效；仅新循环名创建并绑定默认处理函数。
- 投递事件未带 `handle_fn` 时回落循环默认处理函数；两者皆空则事件不执行任何回调直接出队。
- 失败契约：循环不存在 `Failed` "event loop '{n}' does not exist"；已停止未销毁时投递 `Failed` "event loop '{n}' not running, post rejected"（post_wait 同款 "...post wait rejected"）。
- `EventLoopInfo`：`loopName/running/pending/hasDefaultFunc/msgName/nextTags`。

## FunctionPool 域（yomk::fn_pool()）

| 场景 | 链 | 终结与返回 |
|---|---|---|
| 注册 | `.fn_name(name).fn(f).msg_name(m).description(d)` | `.install()` → `ResStatus`；`msg_name/description` 可省略；**同名重复 install 即运行时热替换**（返回 `Succeeded`，无需先注销） |
| 注销 | `.fn_name(name)` | `.uninstall()` → `ResStatus`（名字不存在 `Failed`） |
| 调用 | `.fn_name(name).msg(pkg)` | `.call()` → `Response`（即函数返回值，`data` 携带结果；**阻塞**；msg 省略 = 空消息；名字不存在 `Failed`） |
| 内省 | `.fn_name(name)` / — | `.get_info()` / `.list()` / `.all()`，均 → `Response` |

## Logger 域

### 发射链（yomk::log()）

| 链 | 行为 |
|---|---|
| `.info(args...)`（六级：`trace/debug/info/warn/error/fatal`） | 主控制台输出，tag 缺省 `"main"`（tag 即控制台日志器名，首次使用惰性创建）；参数包逐个追加无上限，`bool` 输出 `true/false`；终结返回 `ResStatus` |
| `.tag(t).info(args...)` | 指定 tag |
| `.to_sink(name).info(args...)` | 写入具名**文件**日志器；`.tag(t)` 可叠加；未知名 `Failed` 不惰性创建——文件日志器须先经 `log_mgr().dir().file().create()` |

输出行格式：`[YYYY-MM-DD HH:MM:SS.mmm] [级别] [tag] [位置前缀] 内容`，级别定宽 5（`[INFO ]`），位置前缀默认仅行号 `[N]`。发射同步阻塞（经请求域投递至内置 Logger 服务）。

### 管理链（yomk::log_mgr()）

| 场景 | 链 |
|---|---|
| 级别门控 | `.console(name).level(lv).enable()/.disable()`、`.file(name).level(lv).enable()/.disable()`——`name` 省略 = 全局主闸，具名 = 仅该日志器（未命中 `Failed` 不惰性创建）；`.level()` 缺省 `Info`；控制台/文件两套完全独立，两级 AND |
| 代理拦截 | `.console().proxy(fn).apply()` / `.file().proxy(fn).apply()`——回调收 `const Log&`，`false` 消费该条（不输出/不落盘）、`true` 穿透默认；`nullptr` 卸载；两侧独立 |
| 位置前缀 | `.location().show_file(on).show_line(on).show_func(on).apply()`——进程级、控制台与文件同时生效；前缀按 `[file:line:func]` 以 `:` 拼接、关断段整体省略（默认 `[N]`；三项全关无前缀） |
| 文件日志器生命周期 | `.dir(d).file(name).create()`（`.dir()` 先于 `.file()`；空名/空目录 `Invalid`，同名重复 `Failed`）；`.file(name).flush()`；`.file(name).remove()`（同名 console+file 两表一并清除、两表皆未命中 `Failed`；**不删磁盘 `.log` 文件**，释放时自动落盘） |
| 内省 | `.console().list()/.file().list()` 名清单（字典序）；`.console(name).get_info()/.file(name).get_info()` 单器 `LoggerInfo`（两表允许同名须按类型精确查）；`.console().status()/.file().status()` 全局主闸 `LoggerState`；`.all()` 全量 LoggerInfoList（每条内嵌逐器门控层真值状态段） |

内省归因：`get_info`/`all()` 条目报**逐器门控层**；条目 on 但无输出时查 `.status()` 确认**全局主闸**——两层各自独立。

## 常用失败契约字符串（断言/排障用）

`"context key '{k}' does not exist"` / `"context key '{k}' already exists"` / `"context type not match: ..."` / `"set context key '{k}' rejected by checker"` / `"group name is empty"` / `"event loop '{n}' does not exist"` / `"event loop '{n}' not running, post rejected"` / `"logger '{n}' already exists"` / `"service has been deleted or unregistered, callback ignored"`（弱绑定丢弃；`{k}`/`{n}` 为实际名值）
