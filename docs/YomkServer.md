# YomkServer / YomkService(进程内服务总线)

服务总线核心：服务（`YomkService`）注册到服务器（`YomkServer`），通过 URL 格式 `"/服务名/功能函数名"` 进行同步或异步调用，并统一管理服务生命周期（注册、删除、优雅关闭）。全部操作经链式域入口 `yomk::server()` 暴露——`url/msg/srv` 等方法前置累积，`request/create/add` 等无参动作终结。同属核心层的 `YomkBoot` 启动编排见文末。

## 适用与不适用

✅ 适合：
- 进程内服务拆分与调用：注册 `YomkService` 派生类，经 URL 同步/异步调用
- 服务生命周期统一管理：注册、同名替换、删除、优雅关闭
- 以 URL 语义组织自研业务功能（含弱绑定防悬垂回调）

❌ 不适合：
- 跨进程/跨机通信（进程内总线，无网络传输层）
- 单队列串行任务（用[事件循环](YomkEventLoop.md)）
- 按名热替换的轻量函数（用[函数池](YomkFunctionPool.md)）
- 键值配置共享与变更联动（用[共享上下文](YomkContext.md)）

> 前置概念：[消息包](YomkMsg.md)——服务函数的入参与返回负载均为强类型消息包；请求/响应三态语义见下文「请求与响应」。

## 链式语法速查

builder 按值传递、全程 `auto` 接收，仅限单表达式内联使用（内部持 `string_view`，缓存复用即悬垂）。

### 请求与响应

| 链 | 终结与返回 |
|---|---|
| `yomk::server().url(u).msg(msg)` | `.request()` → `Response`，阻塞至服务应答；`.status()` → 三态 `ResStatus`（失败细节进诊断日志） |
| `yomk::server().url(u).async(cb)` | 切入异步分面，继续 `.msg(msg)` 后 `.request()` → `void`：投递到框架内置线程池后立即返回，结果经 `cb` 送达；`cb` 传 `nullptr` 仅投递不取回；`.msg()` 可放 `.async()` 之前或之后 |

`.msg(msg)` 为 `YOMK_DEFINE_MSG` 消息对象（见 [消息包](YomkMsg.md)），缺省为空消息。

返回值 `yomk::payload::Response` 字段：

| 字段 | 说明 |
|---|---|
| `status` | `ResStatus::Succeeded (0)`：调用成功；`ResStatus::Failed (1)`：服务端执行失败，含目标服务/函数不存在等 not-found；`ResStatus::Invalid (-1)`：入参非法或服务器未初始化 |
| `msg` | 状态描述文本 |
| `data` | 可选的负载消息包（`YomkMsgPtr`） |

### 生命周期与服务管理

| 链 | 终结与返回 |
|---|---|
| `yomk::server().async_thread(n)` | `.init()`：初始化单例并自动启动内置服务（Logger / Context / EventLoop / FunctionPool / ServerInfo）；`n` 为异步请求线程池大小（省略 = 0 自动检测），仅首次生效；进程内幂等，并挂 atexit 兜底销毁 |
| — | `.version()`：框架版本字符串，免初始化可用；各域入口（`ctx()` / `event_loop()` / `fn_pool()` / `log_mgr()` / `srv_info()`）同样提供 `.version()` 模块版本终结（编译期常量直返，版本号集中定义于顶层 CMakeLists，支持按域独立 bump） |
| — | `.instance()`：`shared_ptr<YomkServer>` 全局快照；未初始化返回 `nullptr` |
| — | `.shutdown()`：关闭服务器（见"注意事项"）；未初始化空转 |
| `yomk::server().srv<T>(name)` | `.create()` → `int`：创建并注册 `YomkService` 派生服务；`name` 省略则沿用 T 构造时的默认名；0 成功，-1 失败（未初始化 / 未先调 `srv<T>` / `T::init()` 非 0） |
| `yomk::server().srv_name(n).srv_ptr(s)` | `.add()` → `int`：注册已有实例，所有权移交框架；累积了非空 `srv_name` 时注册前先改名；守卫拒绝时指针仍归调用方 |
| `yomk::server().srv_name(n)` | `.remove()` → `int`：按名注销并释放所有权（终结定名 `remove`，`delete` 为 C++ 保留字）；-1 = 不存在 / 名称为空 / 未初始化 |
| `yomk::server().boot_ptr(cfg)` | `.run()` → `int`：先 init 再按 before → start → after 执行编排（见文末）；`cfg` 省略 = 仅 init 返回 0 |

### 拓扑内省（读终结，返回 `Response`）

`yomk::srv_info().services()`（服务名清单）/ `.srv_name(n).functions()`（指定服务函数清单）/ `.fn_url(url).get_info()`（单函数元信息）/ `.all()`（全量拓扑），详见 [服务自省](YomkServerInfo.md)。

## 使用示例

```cpp
#include <yomk/server/YomkServerApi.h>
#include <yomk/srv_info/YomkServerInfoApi.h>
#include <yomk/log/YomkLoggerApi.h>

class MyService : public YomkService
{
public:
    MyService(YomkServer* server) : YomkService(server) { name("/MyService"); }
    int init() override
    {
        installFunc("/hello", &MyService::hello, yomk::msgs::String::kTypeName); // 注册即自动弱绑定
        return 0;
    }
    yomk::payload::Response hello(YomkMsgPtr msg)
    {
        auto data = yomk::msgs::unpack<yomk::msgs::String>(msg);
        if (!data)
            return {yomk::payload::ResStatus::Failed, data.error()};
        return {yomk::payload::ResStatus::Succeeded, "hello " + data->d};
    }
};

int main()
{
    yomk::server().init();                                             // 框架初始化
    yomk::server().srv<MyService>().create();                          // 创建并注册服务

    auto ret = yomk::server().url("/MyService/hello")                  // 同步请求
                   .msg(yomk::msgs::String::shared("yomk")).request();
    yomk::log().info("Response:", ret.msg);

    yomk::server().url("/MyService/hello")                             // 异步请求：立即返回
        .async([](yomk::payload::Response r) { yomk::log().info("Async:", r.msg); })
        .msg(yomk::msgs::String::shared("yomk")).request();

    yomk::server().srv_name("/MyService").remove();                    // 删除服务（触发 deinit）
    yomk::server().shutdown();                                         // 优雅关闭
}
```

## 内省描述惯例（第四参 description）

installFunc 第四参是端点的**功能语义澄清**——非强制约束，仅为让内省消费者（主要是 coding agent）不读源码即可理解端点行为，越清晰越好。全部内置服务端点均已按此惯例填写，自定义服务遵循同一格式：

- **布局**：描述常量与 URL 常量**同置各域 `XxxUrl.h`**——URL 常量集中在前、语义描述常量集中在后（按序一一对应），命名对称（`kCtxCreate` / `kCtxCreateDesc`），类型用 `inline constexpr const char*`（installFunc 第四参收 `const std::string&`，C 串可隐式转换；URL 常量仍为 `string_view` 供请求链透传）。installFunc 第四参仅引用常量，安装表一行一端点，一眼可扫；
- **格式**：一律多行 `R"(...)"` 块，固定五章节且顺序不变——`# Overview`（做什么、语义边界）、`# Inputs`（请求载荷字段与前置参数）、`# Outputs`（返回的 status/msg/data 与回调载荷）、`# Behavior`（同步/异步、阻塞语义、副作用、线程上下文）、`# Notes`（约束、前置条件、线程安全、生命周期陷阱、失败归因）。标题行逐字固定（`# ` 后接英文节名），正文用中文；某章节无内容时**保留标题、正文留空**，不得删除章节；
- **失败归因写在 Notes**：按框架三态归因（Invalid=入参非法，Failed=业务未命中/冲突），并写清哪种失败走哪一态。

文案必须与实现相符（依据处理函数真实分支写，不确定的分支不写）。内省数据的消费入口为 `yomk::srv_info()`，模板工程 `MyBoot::after()` 末尾提供完整消费演示（遍历 `/functions` 逐端点输出语义）。

```cpp
// yomk/ctx/YomkContextUrl.h —— URL 先集中、描述后集中（自描述路由表）
inline constexpr std::string_view kCtxCreate = "/YomkContext/create";
inline constexpr std::string_view kCtxGroups = "/YomkContext/groups";

// 语义描述常量：与上方 URL 常量按序一一对应
inline constexpr const char* kCtxCreateDesc = R"(
# Overview
在指定组（缺省 main）创建上下文键并写入值包；create 是键的唯一创建入口，set 只能更新已存在的键。

# Inputs
Context 包：key 键名；value 值包（非空）；group 组名（缺省 "main"）。

# Outputs
data 无负载，成功 msg "create context success"。

# Behavior
同步执行，在组存储写入键值对；非幂等——键已存在时重调必 Failed。

# Notes
key/value 为空 → Invalid；group 名为空 → Invalid；自定义组不存在时按名惰性创建；请求消息非 Context → Failed。
)";
inline constexpr const char* kCtxGroupsDesc = R"(
# Overview
返回全部组名清单（main 恒首位，自定义组按 map 字典序追加）。

# Inputs
无（不带请求包）。

# Outputs
data 为 StringArray 包，msg "ok"。

# Behavior
同步快照读取，持组表读锁拷贝；幂等。

# Notes
组清单只增不减——自定义组一经创建不会消失。
)";
```

```cpp
// src/ctx/YomkContext.cpp —— 安装表仅引用常量
installFunc("/create", &YomkContext::create, yomk::msgs::Context::kTypeName, yomk::url::kCtxCreateDesc);
installFunc("/groups", &YomkContext::groups, "", yomk::url::kCtxGroupsDesc);
```

## 弱绑定（防悬垂回调）

服务成员函数注册到**外部子系统**（函数池 / 事件循环 / Context checker·monitor / 异步响应回调）时必须用 `weakFunc(&Class::Method)` 弱绑定——服务删除后回调自动判活失效，否则访问已析构的 `this` 导致崩溃。`installFunc("/x", &Class::Method)` 注册到本服务 funcMap 已自动弱绑定，无需手动处理：

```cpp
int MyService::init()
{
    yomk::fn_pool().fn_name("my_work").fn(weakFunc(&MyService::myWork)).install();

    // checker/monitor 签名不同，同一 weakFunc 自动适配；前提是键已存在（先 create 再注册）
    yomk::ctx().key("k").value(yomk::msgs::String::shared("v")).create();
    yomk::ctx().key("k").checker_fn(weakFunc(&MyService::myCheck)).apply();
    yomk::ctx().key("k").monitor_fn(weakFunc(&MyService::myMonitor)).apply();

    // 异步响应回调：框架不自动弱绑定，同样用 weakFunc（普通 lambda 须值捕获自包含数据）
    yomk::server().url("/OtherService/func").async(weakFunc(&MyService::onResp)).request();
    return 0;
}
```

失效后的丢弃行为（按回调返回类型）：`Response` 回调得到 `ResStatus::Failed` + "service has been deleted or unregistered, callback ignored"；checker 回调放行（`CheckStatus::Accept`）；void 回调直接丢弃；其他返回类型得到 `Ret{}` 默认值；均同时打错误日志。

## 注意事项

- **服务名以 `/` 开头**：服务名即 URL 前缀，如 `name("/MyService")`；请求 URL 为 `"/服务名/函数名"`。内置服务 URL 常量化于 `yomk::url` 命名空间，自定义服务的 URL 常量可按同风格追加——请求链一律引用常量（`yomk::server().url(yomk::url::kFpCall)`），不裸写 URL 字符串字面量。
- **deinit() 的四个触发时机**：`srv_name(n).remove()` 删除单个服务、`shutdown()` 关闭全部服务、同名服务被 `add()` 替换（旧服务先置位注销标志并 deinit，新服务随后 init，任意时刻同名服务至多一次活跃生命周期）、忘记关闭时进程退出经 atexit 兜底。覆写 `virtual void deinit()` 用于停止线程、注销外部资源。
- **删除即停**：服务删除/同名替换后弱绑定回调立即丢弃；`shutdown()` 走排空语义（拒新 → 排空 → join 异步请求池，再清空服务表逐个 deinit），在途请求安全执行完毕后服务才析构；幂等，假定在主线程调用。
- **关闭后不支持二次 init**：init 进程内幂等由 call_once 把关，`shutdown()` 之后再次 `init()` 不会重建服务器，全部入口回到未初始化守卫态。
- **未初始化守卫（全框架统一约定）**：终结动作返回 `ResStatus::Invalid`（request / status / 各域写与内省终结）或 `-1`（create / add / remove），异步 `.request()` 静默丢弃、回调不触发，类型化读 `get()` 原样返回调用方默认值；均记录错误诊断日志，不抛异常。`version()` / `instance()` / `shutdown()` 免初始化安全。
- **所有权**：`create()` / `add()` 注册成功后实例所有权移交框架，禁止再自行 delete；`T::init()` 失败时框架回滚注销。`instance()` 返回的 `shared_ptr` 快照可保服务器在调用期存活。

## 启动编排（YomkBoot）

`YomkBoot`（`<yomk/boot/YomkBoot.h>`）提供 before / start / after 三段式纯虚契约（`before()` 前置准备如读配置、`start()` 主体如创建注册服务、`after()` 后置动作如挂载监听），封装从框架初始化到业务就绪的完整流程。经 `yomk::server().boot_ptr(cfg).run()` 消费：内部先完成框架 init，再按序执行三段；任一阶段返回非 0 立即中止并原样传播该返回值，`cfg` 所有权移交 `run`。参考实现见 `examples/boot/ArmBoot.h`。

## 扩展模块四件套

自定义扩展的对外头文件按 SDK `include/yomk/` 各域的同一设计组织为四件套：`XxxUrl.h`（`namespace yomk::url::ext` 子命名空间 URL 常量，`inline constexpr std::string_view`；常量名一律 `k` 前缀 + PascalCase（`kVersion`），域由命名空间表达、不再自我前缀；URL 常量集中在前、语义描述常量集中在后（`inline constexpr const char*` 同名 + `Desc`，如 `kVersionDesc`，按序一一对应；内省描述惯例见「注意事项」）；请求侧一律取常量、不裸写字符串字面量）、`XxxPayload.h`（扩展专属载荷，`namespace yomk::payload::ext`）、`XxxMsg.h`（扩展专属消息包，`YOMK_DEFINE_MSG_NS`，消息落 `yomk::msgs::ext` 子命名空间、kTypeName 全限定）、`XxxApi.h`（链式域入口 `yomk::ext()` 形态，Builder 落 `yomk::detail::ext`。约定：①只写本模块业务的终结方法，不重复封装通用请求链；②可不写 API 方法，但须在 Api 文件注释写清统一调用方式及 url 的功能作用，如 `yomk::server().url(yomk::url::ext::kVersion).request()`；③请求一律优先使用统一请求接口 `yomk::server().url(<url>).request()`，扩展终结方法仅是其上的薄封装。模板仅内置 `version()` 终结）。扩展服务类落 `yomk::service::ext`（如 `yomk::service::ext::ExtensionService`；`yomk::ext` 为链式入口函数，函数与命名空间不可同名）。完整可编译模板见 `template/extension/`（`ExtensionApi.h` 提供 `yomk::ext().version()` 终结）。

## 常见问题 FAQ

- **`shutdown()` 之后还能再次 `init()` 吗？** 不能：init 的进程内幂等由 call_once 把关，关闭后全部入口回到未初始化守卫态（见「注意事项」）。
- **`create()` / `add()` 返回 -1 有哪些原因？** 未初始化 / 未先调 `srv<T>()` 累积 / `T::init()` 非 0；`add()` 另有守卫拒绝（此时指针仍归调用方）。
- **服务被删除后，还挂在别处的回调会怎样？** `weakFunc` 弱绑定回调自动判活丢弃：`Response` 回调得 `Failed` + 固定文案、checker 放行、void 回调直接丢弃，均打错误日志（见「弱绑定」）。
- **`shutdown()` 时在途请求怎么办？** 接到新请求先拒绝，再排空队列并 join 异步请求池，在途请求安全执行完毕后服务才析构（见「注意事项」）。

## 相关跳转

- [消息包](YomkMsg.md)：请求负载的封装与解包约定
- [服务自省](YomkServerInfo.md)：运行期查询注册了哪些服务与函数
- 扩展模块四件套设计与完整模板：见上文「扩展模块四件套」与 [template/extension/](../template/extension/)

## 完整示例

完整可运行演示见 `examples/ExampleYomkService.cpp`（8 个机械臂控制场景小示例 showArmXXX 覆盖服务定义、注册、同步/异步请求、跨服务调用、自省、删除与优雅关闭，读运行输出即可理解每个调用）。
