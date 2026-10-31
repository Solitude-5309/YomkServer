# YomkServer / YomkService(服务总线)

服务总线核心：服务（`YomkService`）注册到服务器（`YomkServer`），通过 URL 格式 `"/服务名/功能函数名"` 进行同步或异步调用，并统一管理服务生命周期（注册、删除、优雅关闭）。同属核心层的 `YomkBoot` 启动编排见文末。

## API 速查

### 框架入口与服务管理

| API | 说明 |
|---|---|
| `YomkAPI::init(n)` | 初始化框架单例，自动启动内置服务（Logger、Context、EventLoop、FunctionPool、ServerInfo）；`n` 为异步请求线程池大小（0 表示自动检测），仅首次初始化生效 |
| `YomkAPI::shutdown()` | 关闭服务器：先停异步请求池（拒新 → 排空 → join），再清空服务表并逐个调用 deinit()；幂等 |
| `YomkAPI::newService<ClassName>()` | 创建并注册指定类型的服务 |
| `YomkAPI::addService(srv)` | 注册已有服务实例（所有权移交给框架） |
| `YomkAPI::delService("/服务名")` | 按名称删除单个服务 |
| `installFunc("/x", &C::m)` | 在 `init()` 中注册功能函数（自动弱绑定）；可加第三参 `"MsgName"` 声明期望消息类型名供自省识别，第四参 `"Description"` 附函数说明（自由字符串，`/function` 单查数据段可读） |

### 请求与响应

| API | 说明 |
|---|---|
| `YomkAPI::request(url, pkg)` | 同步请求 |
| `YomkAPI::asyncRequest(url, pkg, callback)` | 异步请求，投递到框架内置线程池并发执行，不阻塞当前线程 |

返回值 `YomkResponse` 字段：

| 字段 | 说明 |
|---|---|
| `m_status` | `eOk (0)`：调用成功；`eNo (1)`：目标服务或函数不存在、被拒绝或条件不满足；`eInvalid (-1)`：入参非法或服务器未初始化 |
| `m_msg` | 状态描述文本 |
| `m_data` | 可选的负载消息包（`YomkPkgPtr`） |

## 使用示例

### 同步与异步请求

```cpp
// URL 寻址：格式为 "/服务名/函数名"，如 "/MyService/hello"

// 同步调用
YomkResponse resp = YomkAPI::request("/MyService/hello", pkg);
if (resp.m_status == YomkResponse::eOk)
{
    YOMK_INFO("Response received:", resp.m_msg);
}

// 异步调用：投递到框架内置线程池并发执行，不阻塞当前线程
YomkAPI::asyncRequest("/MyService/hello", pkg, [](YomkResponse resp) {
    // 异步回调逻辑
});
```

### 服务生命周期

```cpp
// 删除单个服务：从服务表移除，后续请求返回 eNo=1 "service not found: <服务名>"；名字不存在返回 -1
int ret = YomkAPI::delService("/MyService");

// 关闭服务器：先停异步请求池（拒新 → 排空 → join），再清空服务表并逐个调用 deinit()
// 幂等，假定在主线程调用；关闭后不支持二次初始化；未显式调用时退出经 atexit 析构兜底
YomkAPI::shutdown();
```

### 弱绑定（防悬垂回调）

服务成员函数注册到**外部子系统**（FunctionPool / EventLoop / Context checker·monitor / 异步响应回调）时必须用 `weakFunc(&Class::Method)` 弱绑定——服务删除后回调自动判活失效，否则访问已析构的 this 导致崩溃。`installFunc("/x", &Class::Method)` 注册到本服务 funcMap 已自动弱绑定，无需手动处理：

```cpp
int MyService::init()
{
    // FunctionPool：服务删除后回调失效（返回 eNo）
    YomkAPI::FUNCTIONPOOL_REGISTER("my_work", weakFunc(&MyService::myWork));

    // Context checker/monitor 签名不同，同一 weakFunc 自动适配：删除后 checker 放行（eAccept）、monitor 丢弃
    YomkAPI::setContextChecker("key", weakFunc(&MyService::myCheck));
    YomkAPI::setContextMonitor("key", weakFunc(&MyService::myMonitor));

    // 异步响应回调：框架不自动弱绑定，同样用 weakFunc(&MyService::onResp)（普通 lambda 须值捕获自包含数据）
    YomkAPI::asyncRequest("/OtherService/func", nullptr, weakFunc(&MyService::onResp));
    return 0;
}
```

## 注意事项

- **服务名以 `/` 开头**：服务名即 URL 前缀，如 `name("/MyService")`；请求 URL 为 `"/服务名/函数名"`。
- **deinit() 的四个触发时机**：`YomkAPI::delService()` 删除单个服务、`YomkAPI::shutdown()` 关闭全部服务、同名服务被 `YomkAPI::addService()` 替换（旧服务先 deinit 再装新服务）、忘记关闭时框架析构兜底。覆写 `virtual void deinit()` 用于停止线程、注销外部资源。
- **删除即停**：服务删除/同名替换后弱绑定回调立即丢弃；`YomkAPI::shutdown()` 走排空语义，排空期回调照常执行；在途请求安全执行完毕后服务才析构。
- **异步请求池大小**：`YomkAPI::init(n)` 的 `n` 为异步请求线程池大小（0 表示自动检测），仅首次初始化生效。
- **弱绑定失效行为**：服务删除后，返回 `YomkResponse` 的回调得到 `eNo`，checker 回调放行（`eAccept`），void 回调直接丢弃，并打错误日志。
- **弱绑定依赖服务的 `shared_ptr` 所有权**：在构造函数内调用（服务尚未注册到服务器）回调永不触发并打错误日志。

## 启动编排（YomkBoot）

`YomkBoot` 提供 before / start / after 三段式引导，封装从框架初始化到业务就绪的完整流程。`YomkAPI::boot(bootPtr)` 内部先完成框架初始化，再按 before → start → after 顺序执行；任一步返回非 0 立即中止并打错误日志。参考实现见 `Examples/boot/ArmBoot.h`。

## 完整示例

完整可运行演示见 `Examples/ExampleYomkService.cpp`（8 个机械臂控制场景小示例 showArmXXX 覆盖服务定义、注册、同步/异步请求、跨服务调用、自省、删除与优雅关闭，读运行输出即可理解每个调用）。
