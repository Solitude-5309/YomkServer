# YomkContext(共享上下文)

跨服务强类型键值存储，内置变更前校验门控（Checker）与变更后监听（Monitor，同步/异步），支持运行期自省与销毁。随 `YomkAPI::init()` 自动启动。

## API 速查

| API | 说明 |
|---|---|
| `YomkAPI::CONTEXT_CREATE(key, pkg)` | 创建上下文键值 |
| `YomkAPI::CONTEXT_SET(key, pkg)` | 设置上下文值（整体替换） |
| `YomkAPI::CONTEXT_GET<yomkmsg::MsgName>(key, defaultValue)` | 读取只读快照；key 不存在时返回兜底默认值 |
| `YomkAPI::CONTEXT_SET_CHECKER(key, func)` | 注册变更前校验门控 |
| `YomkAPI::CONTEXT_ON_CHECKER()` / `YomkAPI::CONTEXT_OFF_CHECKER()` | 开关全局校验器（checker 保留但不生效） |
| `YomkAPI::CONTEXT_SET_MONITOR(key, func, async)` | 注册变更后监听器（追加式可注册多个） |
| `YomkAPI::CONTEXT_ON_MONITOR()` / `YomkAPI::CONTEXT_OFF_MONITOR()` | 开关全局监控（monitor 保留但不执行） |
| `YomkAPI::CONTEXT_INFO_KEYS()` / `YomkAPI::CONTEXT_INFO_KEY(key)` / `YomkAPI::CONTEXT_INFO_ALL()` | 运行期自省 |
| `YomkAPI::CONTEXT_DESTROY(key)` | 删除键 |

## 使用示例

```cpp
// 1. 创建与设置上下文键值
YomkAPI::CONTEXT_CREATE("config", yomkmsg::String::shared("init_value"));
YomkAPI::CONTEXT_SET("config", yomkmsg::String::shared("new_value"));

// 2. 读取上下文（只读快照；key 不存在时返回兜底默认值）
auto val = YomkAPI::CONTEXT_GET<yomkmsg::String>("config", nullptr);

// 3. 注册变更前校验门控（Checker）
YomkAPI::CONTEXT_SET_CHECKER("config", [](const yomk::Context &ctx) {
    // 返回 eAccept 放行修改；返回 eReject 拦截（set 返回 eNo=1 "checker reject set context"，monitor 不触发）
    return yomk::ContextChecker::eAccept;
});
YomkAPI::CONTEXT_ON_CHECKER();  // 开启全局校验器；YomkAPI::CONTEXT_OFF_CHECKER() 关闭（checker 保留但不生效）

// 4. 注册变更后监听器（Monitor）
YomkAPI::CONTEXT_SET_MONITOR("config", [](const yomk::Context &ctx) {
    // 收到变更通知（本次 set 的键值快照）
}, /*async=*/false);
YomkAPI::CONTEXT_ON_MONITOR();  // 开启全局监控；YomkAPI::CONTEXT_OFF_MONITOR() 关闭（monitor 保留但不执行）

// 5. 运行期自省与销毁
YomkAPI::CONTEXT_INFO_KEYS();        // 组内全部键名清单（缺省 main 组）
YomkAPI::CONTEXT_INFO_KEY("config"); // 单键元信息：结构化 ContextKeyInfo 包（m_msg 恒 "ok"；含所属组 m_group）
YomkAPI::CONTEXT_INFO_GROUPS();      // 全部组名清单（StringArray）
YomkAPI::CONTEXT_INFO_ALL();         // 跨组聚合：ContextGroupInfoList 两层结构（组名 + 该组全部键元信息）
YomkAPI::CONTEXT_DESTROY("config");  // 删除键
```

## 注意事项

- **创建与设置**：key 已存在返回 `eNo=1` "key already exists"；value 为空返回 `eNo=1` "value is empty"；值类型须与现值一致，否则 `eNo` "context type not match"。
- **组名约定**：所有 Context API 的最后一个参数为组名，缺省 `"main"`；键所属组随 `ContextKeyInfo.m_group` 一并内省返回。空组名返回 `eNo=1` "group name is empty"；非 main 组首次寻址即自动创建（惰性、不支持销毁、组名唯一，同名寻址复用同组），监控池每组自持（每组一个单线程池，随建组只增不减）。
- **读取快照勿原地修改**：`YomkAPI::CONTEXT_GET` 返回只读快照，改动请构造新对象再 `YomkAPI::CONTEXT_SET`，勿原地修改（否则与并发读竞争、可能崩溃）。
- **Checker 生效条件**：全局开关 ON 且该 key 已设置时才生效；在写锁内门控，回调内重入 set 会死锁。
- **Monitor 执行语义**：`async=false` 锁外即时执行，`async=true` 单线程池按提交序送达；仅通知发生了一次 set，并回传该次 set 的键值快照，不保证快照实时性。
- **销毁后行为**：删除键后操作返回 `eNo=1`（key is not exist）。
- 服务成员函数注册为 checker/monitor 时须用 `weakFunc(&Class::Method)` 弱绑定，见 [YomkServer.md](YomkServer.md) 弱绑定一节。

## 内部结构

Context 功能承载在 `ContextGroup`（底层实例组，不继承 YomkService）上：键存储、checker/monitor 门控开关与单线程异步监控池均为每组自持，内省快照携带所属组名（`ContextKeyInfo.m_group`）。`YomkContext` 服务持默认组成员 `m_main`（组名 "main"，成员直取免查找）与自定义组表 `m_customGroup`（`map<string, ContextGroupPtr>` + 读写锁），负责 14 个端点的装配与组路由：处理函数统一"解出组名 → `findGroup` 定位 → 组实例委托"。`findGroup` 三段式：空组名返回 `eNo "group name is empty"`；main 成员直取；非 main 读锁快查自定义组表、未命中写锁内惰性自动创建（组不支持销毁，map 只增不减，同名寻址复用同组，锁外裸指针长期安全）。

## 完整示例

完整可运行演示见 `Examples/ExampleYomkContext.cpp`（8 个机械臂场景小示例 `showArmXXX` 组合覆盖全部上下文 API，日志统一走 YomkLogger 并按 tag 分层，读运行输出即可理解每个调用）。
