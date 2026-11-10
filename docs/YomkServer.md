# YomkServer / YomkService（进程内服务总线）

服务总线核心：服务（`YomkService`）注册到服务器（`YomkServer`），通过 URL 格式 `"/服务名/功能函数名"` 进行同步或异步调用，并统一管理服务生命周期（注册、删除、优雅关闭）。全部操作经链式域入口 `server()` 暴露——`url/msg/srv` 等方法前置累积，`request/create/add` 等无参动作终结。启动编排基类 `YomkBoot` 独立 boot 域（`from yomk.boot import YomkBoot`），见文末。

## 链式语法速查

Python 链对象按值持有、无悬垂风险，但 `log()` 域入口在调用行捕获位置前缀——链仍建议单表达式内联使用。

### 请求与响应

| 链 | 终结与返回 |
|---|---|
| `server().url(u).msg(pkg)` | `.request()` → `Response`，阻塞至服务应答；`.status()` → 三态 `ResStatus`（失败细节进诊断日志） |
| `server().url(u).async_(cb)` | 切入异步分面，继续 `.msg(pkg)` 后 `.request()` → `None`：投递到框架内置线程池后立即返回，结果经 `cb` 送达；`cb` 传 `None` 仅投递不取回；`.msg()` 可放 `.async_()` 之前或之后 |

`.msg(pkg)` 为任意 Python 对象（消息即普通类，见 [消息包](YomkMsg.md)），缺省为空消息（`None`）。

返回值 `Response` 字段：

| 字段 | 说明 |
|---|---|
| `status` | `Succeeded (0)`：调用成功；`Failed (1)`：服务端执行失败，含目标服务/函数不存在等 not-found；`Invalid (-1)`：入参非法或服务器未初始化（Python 常量 `ResStatus.eOk / eNo / eInvalid` 为同值别名） |
| `msg` | 状态描述文本 |
| `data` | 可选的负载消息（任意 Python 对象，直接访问属性） |

### 生命周期与服务管理

| 链 | 终结与返回 |
|---|---|
| `server().async_thread(n)` | `.init()`：初始化单例并自动启动内置服务（Logger / Context / EventLoop / FunctionPool / ServerInfo），无返回值；`n` 为异步请求线程池大小（省略 = 0 自动检测），仅首次生效；进程内幂等，并挂 atexit 兜底销毁。派生服务的 `init()` 建议显式 `return 0` 表示初始化成功 |
| — | `.version()`：版本字符串，免初始化可用。C++ 与 Python 双侧实现为独立分包，各自维护版本号、独立演进（字符串仅作展示识别，不承载跨侧兼容语义） |
| — | `.instance()`：全局单例 `YomkServer`；未初始化返回 `None`（`shutdown()` 后亦为 `None`，见"注意事项"） |
| — | `.shutdown()`：关闭服务器（见"注意事项"）；未初始化空转 |
| `server().srv(cls, name)` | `.create()` → `int`：创建并注册 `YomkService` 派生服务；`name` 省略则沿用构造时的默认名；0 成功，-1 失败（未初始化 / 未先调 `srv()` / `init()` 非 0） |
| `server().srv_name(n).srv_ptr(s)` | `.add()` → `int`：注册已有实例；累积了非空 `srv_name` 时注册前先改名；守卫拒绝时实例仍归调用方 |
| `server().srv_name(n)` | `.remove()` → `int`：按名注销并触发 `deinit`；-1 = 不存在 / 名称为空 / 未初始化 |
| `server().boot_ptr(boot)` | `.run()` → `int`：先 init 再按 before → start → after 执行编排（见文末）；`boot` 省略 = 仅 init 返回 0 |

### 拓扑内省（读终结，返回 `Response`）

`srv_info().services()`（服务名清单）/ `.srv_name(n).functions()`（指定服务函数清单）/ `.fn_url(url).get_info()`（单函数元信息）/ `.all()`（全量拓扑），详见 [服务自省](YomkServerInfo.md)。

## 使用示例

```python
from yomk.msg.server import ResStatus, Response
from yomk.boot import YomkBoot
from yomk.server import YomkService, server
from yomk.ctx import ctx
from yomk.fn_pool import fn_pool
from yomk.log import log

class MyService(YomkService):
    def __init__(self, server):
        super().__init__(server)
        self.set_name("/MyService")

    def init(self):
        self.install_func("/hello", self.hello)  # 注册即自动弱绑定

    def hello(self, pkg):
        if not isinstance(pkg, str):
            return Response(ResStatus.eNo, "pkg is not str")
        return Response(ResStatus.eOk, "hello " + pkg)

server().init()                                  # 框架初始化
server().srv(MyService).create()                 # 创建并注册服务

ret = server().url("/MyService/hello").msg("yomk").request()  # 同步请求
log().info("Response:", ret.msg)

# 异步请求：立即返回，回包到达时在回调线程执行
(server().url("/MyService/hello")
    .async_(lambda r: log().info("Async:", r.msg))
    .msg("yomk").request())

server().srv_name("/MyService").remove()         # 删除服务（触发 deinit）
server().shutdown()                              # 优雅关闭
```

## 弱绑定（防失效回调）

服务成员函数注册到**外部子系统**（函数池 / 事件循环 / Context checker·monitor / 异步响应回调）时必须用 `self.weak_func(方法)` 弱绑定——服务删除后回调自动判活失效，避免访问已注销服务的 `self`。`install_func("/x", self.method)` 注册到本服务函数表已自动弱绑定，无需手动处理：

```python
class MyService(YomkService):
    def init(self):
        fn_pool().fn_name("my_work").fn(self.weak_func(self.my_work)).install()

        # checker/monitor 同一 weak_func 适配，discard 可省略——消费槽位按 API 形态
        # 自动填充缺省（checker→"check"、monitor→"none"）；前提是键已存在（先 create 再注册）
        ctx().key("k").value("v").create()
        ctx().key("k").checker_fn(self.weak_func(self.my_check)).apply()
        ctx().key("k").monitor_fn(self.weak_func(self.my_monitor)).apply()

        # 异步响应回调：框架不自动弱绑定，同样用 weak_func（普通 lambda 须自包含数据）
        server().url("/OtherService/func").async_(self.weak_func(self.on_resp)).request()
        return 0
```

`weak_func(func, discard=None)` 的 `discard` 声明失效丢弃形态：`None`（缺省，未显式声明）由**消费槽位注册期按 API 形态自动填充**——checker 槽填 `"check"`、monitor 槽填 `"none"`、fn_pool/event_loop 槽填 `"response"`，未过槽位直调兜底 `"response"`；也可显式声明：`"response"` 得 `eNo` + 契约消息 `"service has been deleted or unregistered, callback ignored."`、`"check"`（checker 用）放行 `CheckStatus.eAccept`、`"none"`（void 语义回调）得 `None`，三种形态均同时打错误日志。显式声明与槽位不符时**注册期立即打错误诊断**（早于回调真正被调用），携带具体值告知问题所在；裸回调（非 weak_func 产物）无丢弃契约，不做校验。

## 注意事项

- **服务名以 `/` 开头**：服务名即 URL 前缀，如 `set_name("/MyService")`；请求 URL 为 `"/服务名/函数名"`。内置服务的端点 URL 常量集中于 `yomk.url` 常量池（`src/yomk/url/<域>/__init__.py`，逐域扩充）；`yomk.url` 为纯命名空间层（与 `yomk.msg` 同构），用户自定义模块自带 `__init__.py` 即可并入。
- **deinit() 的四个触发时机**：`srv_name(n).remove()` 删除单个服务、`shutdown()` 关闭全部服务、同名服务被 `add()` 替换（旧服务先置位注销标志并 deinit，新服务随后 init，任意时刻同名服务至多一次活跃生命周期）、忘记关闭时进程退出经 atexit 兜底。覆写 `deinit()` 用于停止线程、注销外部资源。
- **删除即停**：服务删除/同名替换后弱绑定回调立即丢弃；`shutdown()` 走排空语义（拒新 → 排空 → join 异步请求池，再清空服务表逐个 deinit），在途请求安全执行完毕后服务才释放；幂等，假定在主线程调用。
- **关闭后不支持二次 init**：init 进程内幂等由 once 标志把关，`shutdown()` 之后再次 `init()` 不会重建服务器，全部入口回到未初始化守卫态；此时再调 `init()` 补 WARN 诊断 `"server already shut down, init ignored (re-init is not supported)"` 指出实际状态（幂等语义不变，对齐 C++）。**`shutdown()` 置空单例**：门面在 `shutdown()` 时置空单例，`instance()` 返回 `None`，不保留已关闭实例快照。
- **未初始化守卫（全框架统一约定）**：终结动作返回 `ResStatus.Invalid`（request / status / 各域写与内省终结）或 `-1`（create / add / remove），异步 `.request()` 静默丢弃、回调不触发，类型化读 `get()` 原样返回调用方默认值；守卫消息 `"YomkServer is not initialized"` 经裸链 `server().url(u).msg(pkg).request()` 可查（写路径链式终结收敛为 `ResStatus`，不带 msg）；均记录错误诊断日志，不抛异常。`version()` / `instance()` / `shutdown()` 免初始化安全。
- **所有权**：`create()` / `add()` 注册成功后实例交由框架服务表持有，调用方不应再将其作为活跃对象复用；`init()` 失败时框架回滚注销。
- **服务对 server 为弱引用**（对齐 C++ weak_ptr 模型）：服务存活不阻碍 server 销毁；server 销毁后服务内 `request()` 返 `Failed` + `"server has been destroyed"`，`async_request()` 丢弃并记日志。派生服务读 `self.server` 得到活的 server 或 `None`，语义与旧强引用版一致。
- **diag 门控**：诊断通道默认只输出 `WARN` 及以上（对齐 C++ 默认行为）。`from yomk import _diag` 后用 `_diag.set_min_level(_diag.TRACE)` 调整进程级阈值、`_diag.enabled(_diag.TRACE)` 做热路径自查；级别常量 TRACE=0 … FATAL=5。热路径建议先 `enabled()` 判断再构造消息字符串（C++ 侧为宏级零构造开销，Python 无法完全复刻）。诊断输出取不到调用点位置时（`.so` 编译调用点无 Python 帧、栈过浅）自动省略位置段，不会因此异常。

## 启动编排（YomkBoot）

`YomkBoot`（`yomk.boot` 域，ABC 纯虚）提供 before / start / after 三段式契约（`before()` 前置准备如读配置、`start()` 主体如创建注册服务、`after()` 后置动作如挂载监听），封装从框架初始化到业务就绪的完整流程。经 `server().boot_ptr(boot).run()` 消费：内部先完成框架 init，再按序执行三段；任一阶段返回非 0 立即中止并原样传播该返回值。参考实现见 `examples/boot/ArmBoot.py`。

## 完整示例

完整可运行演示见 `examples/ExampleYomkService.py`（控制柜一次上电会话 8 个场景覆盖服务定义、注册、同步/异步请求、跨服务调用、自省、热替换、删除断链与优雅关闭，读运行输出即可理解每个调用）。
