# YomkMsg(消息包)

强类型消息包：业务数据通过 `YomkMsg` 及其派生类安全封装与传递，支持拷贝与移动语义，用于在服务请求、上下文、事件循环与函数池之间传递数据。跨边界统一以基类指针 `YomkMsgPtr`（`std::shared_ptr<YomkMsg>`）流转，接收端用 `unpack` 按运行期类型名还原具体类型。

消息基类与 `YOMK_DEFINE_MSG` 宏位于 `<YomkServer/YomkMsg.h>`；内置 30 种标准类型位于 `<YomkServer/YomkMsgs.h>`。

## API 速查

| API | 说明 |
|---|---|
| `YOMK_DEFINE_MSG(DataType, MsgName, VarName)` | 声明自定义消息类型：在 `yomk::msgs` 命名空间内生成 `YomkMsg` 派生类 `MsgName`（载荷成员 `VarName`，框架约定统一取 `d`）与别名 `MsgNamePtr`；宏自带命名空间注入，建议放置在任意命名空间之外 |
| `yomk::msgs::MsgName::shared(args...)` | 类内静态工厂，转发构造参数创建 `shared_ptr<MsgName>` 包装的消息包 |
| `yomk::msgs::unpack<yomk::msgs::MsgName>(pkg)` | 按消息类型解包模板：返回 `shared_ptr<MsgName>`；pkg 为空或类型名不匹配时返回 `nullptr`，不抛异常；失败处理由调用方手写（如 `if (!ptr) return {...}`） |

## 内置标准类型

内置 30 种标量/数组类型消息包（成员名均为 `d`），可直接用 `yomk::msgs::String::shared(...)` 等方式构造：

| 标量 | 数组 |
|---|---|
| `Bool`、`Char`、`UChar`、`Byte`、`Int8`、`Uint8`、`Int16`、`Uint16`、`Int32`、`Uint32`、`Int64`、`Uint64`、`Float32`、`Float64`、`String` | `BoolArray`、`CharArray`、`UCharArray`、`ByteArray`、`Int8Array`、`Uint8Array`、`Int16Array`、`Uint16Array`、`Int32Array`、`Uint32Array`、`Int64Array`、`Uint64Array`、`Float32Array`、`Float64Array`、`StringArray` |

## 使用示例

```cpp
// 1. 声明自定义消息（建议放置在命名空间之外）
struct UserInfo {
    int id;
    std::string name;
};
YOMK_DEFINE_MSG(UserInfo, UserInfoMsg, d);

// 2. 构造消息包
auto pkg = yomk::msgs::UserInfoMsg::shared(UserInfo{1001, "Alice"});

// 3. 功能函数中校验并解包（类型不匹配时 info 为 nullptr，需自行判空）
auto info = yomk::msgs::unpack<yomk::msgs::UserInfoMsg>(pkg);
if (!info)
{
    return {yomk::payload::ResStatus::Failed, "pkg is not UserInfoMsg"};
}
return {yomk::payload::ResStatus::Succeeded, "hello " + info->d.name};
```

消息包即插即用于各域链：

```cpp
yomk::server().url("/MyService/register").msg(pkg).request();                                  // 请求负载
yomk::ctx().key("user").value(pkg).create();                                                   // 上下文存储
yomk::event_loop().msg(pkg).loop("TaskLoop").post();                                           // 事件数据
yomk::fn_pool().fn_name("save").msg(pkg).call();                                               // 函数池调用
```

## 注意事项

- 类型匹配以运行期类型名为准（宏的 `MsgName` 参数即 `kName`）；各处声明的"期望消息类型名"——`installFunc("/x", &C::m, "MsgName")` 第三参、`yomk::fn_pool().msg_name(...)`、`yomk::event_loop().msg_name(...)`——与该字符串保持一致，仅作内省元数据，不拦截调用。
- 期望类型名与说明字符串均供自省读取（函数池与函数单查返回 `FuncInfo`：`m_funcName` / `m_msgName` / `m_description`，见 [服务自省](YomkServerInfo.md)）。
- 上下文类型化读自带同类校验：`yomk::ctx().key(k).msg<MsgName>()...get()` 内部按 `kName` 比对 + 指针转换，与 `unpack` 语义一致。

## 完整示例

最小完整示例见根目录 [README](../README.md) 的"快速上手"一节；内置类型的封包/解包行为验证见 `test/yomk_server/TestYomkMsg.cpp`。
