# YomkContext(共享上下文)

跨服务强类型键值存储，内置变更前校验门控（Checker）与变更后监听（Monitor，同步/异步），支持运行期自省与销毁。随 `YOMK_INIT()` 自动启动。

## API 速查

| API | 说明 |
|---|---|
| `YOMK_CONTEXT_CREATE(key, pkg)` | 创建上下文键值 |
| `YOMK_CONTEXT_SET(key, pkg)` | 设置上下文值（整体替换） |
| `YOMK_CONTEXT_GET(MsgName, key, defaultValue)` | 读取只读快照；key 不存在时返回兜底默认值 |
| `YOMK_CONTEXT_SET_CHECKER(key, func)` | 注册变更前校验门控 |
| `YOMK_CONTEXT_ON_CHECKER()` / `YOMK_CONTEXT_OFF_CHECKER()` | 开关全局校验器（checker 保留但不生效） |
| `YOMK_CONTEXT_SET_MONITOR(key, func, async)` | 注册变更后监听器（追加式可注册多个） |
| `YOMK_CONTEXT_ON_MONITOR()` / `YOMK_CONTEXT_OFF_MONITOR()` | 开关全局监控（monitor 保留但不执行） |
| `YOMK_CONTEXT_INFO_KEYS()` / `YOMK_CONTEXT_INFO_KEY(key)` / `YOMK_CONTEXT_INFO_ALL()` | 运行期自省 |
| `YOMK_CONTEXT_DESTROY(key)` | 删除键 |

## 使用示例

```cpp
// 1. 创建与设置上下文键值
YOMK_CONTEXT_CREATE("config", YomkMkPtr(String, "init_value"));
YOMK_CONTEXT_SET("config", YomkMkPtr(String, "new_value"));

// 2. 读取上下文（只读快照；key 不存在时返回兜底默认值）
auto val = YOMK_CONTEXT_GET(String, "config", nullptr);

// 3. 注册变更前校验门控（Checker）
YOMK_CONTEXT_SET_CHECKER("config", [](const yomk::Context &ctx) {
    // 返回 eAccept 放行修改；返回 eReject 拦截（set 返回 eNo=1 "checker reject set context"，monitor 不触发）
    return yomk::ContextChecker::eAccept;
});
YOMK_CONTEXT_ON_CHECKER();  // 开启全局校验器；YOMK_CONTEXT_OFF_CHECKER() 关闭（checker 保留但不生效）

// 4. 注册变更后监听器（Monitor）
YOMK_CONTEXT_SET_MONITOR("config", [](const yomk::Context &ctx) {
    // 收到变更通知（本次 set 的键值快照）
}, /*async=*/false);
YOMK_CONTEXT_ON_MONITOR();  // 开启全局监控；YOMK_CONTEXT_OFF_MONITOR() 关闭（monitor 保留但不执行）

// 5. 运行期自省与销毁
YOMK_CONTEXT_INFO_KEYS();        // 全部键名清单
YOMK_CONTEXT_INFO_KEY("config"); // 单键元信息：结构化 ContextKeyInfo 包（m_msg 恒 "ok"）
YOMK_CONTEXT_INFO_ALL();         // 全量键元信息：ContextKeyInfoList 结构化包
YOMK_CONTEXT_DESTROY("config");  // 删除键
```

## 注意事项

- **创建与设置**：key 已存在返回 `eNo=1` "key already exists"；value 为空返回 `eNo=1` "value is empty"；值类型须与现值一致，否则 `eNo` "context type not match"。
- **读取快照勿原地修改**：`YOMK_CONTEXT_GET` 返回只读快照，改动请构造新对象再 `YOMK_CONTEXT_SET`，勿原地修改（否则与并发读竞争、可能崩溃）。
- **Checker 生效条件**：全局开关 ON 且该 key 已设置时才生效；在写锁内门控，回调内重入 set 会死锁。
- **Monitor 执行语义**：`async=false` 锁外即时执行，`async=true` 单线程池按提交序送达；仅通知发生了一次 set，并回传该次 set 的键值快照，不保证快照实时性。
- **销毁后行为**：删除键后操作返回 `eNo=1`（key is not exist）。
- 服务成员函数注册为 checker/monitor 时须用 `YomkBindWeakSelf` 弱绑定，见 [YomkServer.md](YomkServer.md) 弱绑定一节。

## 完整示例

完整可运行演示见 `Examples/ExampleYomkContext.cpp`（8 步覆盖全部上下文 API，读运行输出即可理解每个调用）。
