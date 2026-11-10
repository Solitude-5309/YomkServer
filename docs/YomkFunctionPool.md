# YomkFunctionPool(动态函数池)

跨服务、动态注册与调用的扁平函数中心：按名注册函数、按名直接调用，同名重复注册即运行时热替换。随 `yomk::server().init()` 自动启动。全部操作经链式域入口 `yomk::fn_pool()`：函数名由 `.fn_name(name)` 定位，回调 / 期望消息类型名 / 说明 / 调用数据前置累积，终结动作全部无参。

## 适用与不适用

✅ 适合：
- 跨服务按名直接调用工具函数（无服务类骨架，注册即用）
- 运行期热替换实现：同名重注册即覆盖，无需注销

❌ 不适合：
- 需要 URL 路由与服务生命周期管理（用[服务总线](YomkServer.md)）
- 异步/队列化调用（`call()` 同步阻塞至执行完成；异步取回用服务总线 `.async()` 分面）

> 前置概念：[消息包](YomkMsg.md)——函数入参与返回负载均为强类型消息包；服务成员函数注册须弱绑定（见 [服务总线](YomkServer.md)「弱绑定」）。

## 链式语法速查

| 场景 | 链 | 终结与返回 |
|---|---|---|
| 注册 | `yomk::fn_pool().fn_name(name).fn(f).msg_name(m).description(d)` | `.install()` → `ResStatus`；`msg_name`（期望消息类型名）与 `description`（函数说明）均为内省元数据，可省略 |
| 注销 | `yomk::fn_pool().fn_name(name)` | `.uninstall()` → `ResStatus`（名字不存在 `Failed`） |
| 调用 | `yomk::fn_pool().fn_name(name).msg(pkg)` | `.call()` → `Response`（即函数返回值；`msg` 可省略 = 空消息；**阻塞至函数执行完成**；名字不存在 `Failed`） |
| 内省 | `yomk::fn_pool().fn_name(name)` / — | `.get_info()` 单函数元信息 / `.list()` 函数名清单（StringArray）/ `.all()` 全量元信息（FuncInfoList），均 → `Response` |

> 以下为用法片段（假设框架已 `init()`）；完整可编译、带 main 的程序见文末「完整示例」。

## 使用示例

```cpp
yomk::server().init();

// 注册函数（msg_name/description 可选，仅作内省元数据）
yomk::fn_pool().fn_name("calcSum")
    .fn([](YomkMsgPtr pkg) {
        auto arr = yomk::msgs::unpack<yomk::msgs::Int32Array>(pkg);
        if (!arr)
            return yomk::payload::Response{yomk::payload::ResStatus::Failed, "pkg is not Int32Array"};
        int sum = 0;
        for (int v : arr->d) sum += v;
        return yomk::payload::Response{yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::Int32::shared(sum)};
    })
    .msg_name("Int32Array").description("对 Int32Array 求和")
    .install();

// 调用（返回值即函数返回值；未注册的名字返回 ResStatus::Failed）
auto resp = yomk::fn_pool().fn_name("calcSum").msg(yomk::msgs::Int32Array::shared(std::vector<std::int32_t>{1, 2, 3})).call();
auto sum = yomk::msgs::unpack<yomk::msgs::Int32>(resp.data);   // 结果在 data

// 同名重复 install 即运行时热替换，无需先注销（返回 Succeeded）
yomk::fn_pool().fn_name("calcSum").fn(newFunc).install();

// 注销与内省
yomk::fn_pool().fn_name("calcSum").uninstall();
yomk::fn_pool().list();
```

## 注意事项

- 同名重复注册即运行时热替换（覆盖旧函数并返回 `Succeeded`），无需先注销。
- 未注册的名字：`call()` / `uninstall()` 均返回 `Failed`（not-found 归 `Failed` 不归 `Invalid`，与全框架三态约定一致）。
- `install()` 终结定名（`register` 为 C++ 保留字）。
- 函数体收到的 `pkg` 即调用侧 `.msg()` 累积的消息包；返回值整体作为 `call()` 的 `Response` 回传（`data` 携带结果消息）。
- 服务成员函数注册到函数池时须用 `weakFunc(&Class::Method)` 弱绑定——服务删除后回调自动失效（返回 `Failed`），见 [YomkServer.md](YomkServer.md) 弱绑定一节。

## 常见问题 FAQ

- **`call()` 阻塞吗？** 阻塞至函数执行完成，返回值即函数自身的 `Response`（结果消息在 `data`）。
- **热替换要先注销吗？** 不用：同名 `install()` 即覆盖并返回 `Succeeded`，无需先 `uninstall()`。
- **未注册的名字返回什么？** `call()` / `uninstall()` 均返回 `Failed`（not-found 归 `Failed` 不归 `Invalid`，与全框架三态约定一致）。

## 相关跳转

- [服务总线](YomkServer.md)：需要 URL 路由、服务生命周期管理时的对位能力
- [服务自省](YomkServerInfo.md)：`msg_name` / `description` 元数据如何进入内省

## 完整示例

完整可运行演示见 `examples/ExampleYomkFunctionPool.cpp`（6 个机械臂产线场景小示例 showArmXXX 覆盖全部函数池 API，读运行输出即可理解每个调用）。
