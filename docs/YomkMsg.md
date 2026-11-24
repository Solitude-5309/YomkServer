# YomkMsg(消息包)

强类型消息包：业务数据通过 `YomkMsg` 及其派生类安全封装与传递，支持拷贝与移动语义，用于在服务请求、上下文、事件循环与函数池之间传递数据。

**一条分界线（ROS 2 式所有权契约）**：生产侧自己构造并交出可变指针 `YomkMsgPtr`（`std::shared_ptr<YomkMsg>`）；一旦交给框架，传输字段、存储字段、回调形参与返回值一律为只读 `YomkConstMsgPtr`（`std::shared_ptr<const YomkMsg>`）。需要修改消息时用 `clone()` 拷出可变副本再提交，不存在原地修改存储对象的逃生口。接收端用 `unpack` 按运行期类型名还原具体类型，解包结果恒为 `const`。

消息基类、`YOMK_DEFINE_MSG` / `YOMK_DEFINE_MSG_NS` 宏与 `yomk::msgs::unpack` 位于 `<yomk/msg/YomkMsg.h>`；内置 30 种标准类型位于 `<yomk/msg/YomkStdMsg.h>`；核心载荷消息包（`Response`）位于 `<yomk/server/YomkServerMsg.h>`；模块专属载荷/消息分别位于 `YomkContext{Payload,Msg}.h`、`YomkLogger{Payload,Msg}.h`、`YomkEventLoop{Payload,Msg}.h`、`YomkFunctionPool{Payload,Msg}.h`、`YomkServerInfo{Payload,Msg}.h`（对应的 `*Api.h` 已按需包含）。

## 适用与不适用

✅ 适合：
- 各域间传强类型数据：请求负载、上下文值、事件数据、函数池参数
- 运行期按类型名安全还原（`unpack` 判空即可，不抛异常）

❌ 不适合：
- 跨进程/网络序列化传输（无 JSON/protobuf 类编解码层）
- 访问控制（"期望消息类型名"仅内省元数据，不拦截调用）

## API 速查

| API | 说明 |
|---|---|
| `YOMK_DEFINE_MSG(DataType, MsgName, VarName)` | 声明自定义消息类型：在 `yomk::msgs` 命名空间内生成 `YomkMsg` 派生类 `MsgName`（载荷成员 `VarName`，框架约定统一取 `d`）与别名 `MsgNamePtr` / `MsgNameConstPtr`（只读），并自动实现 `clone()`；宏自带命名空间注入，建议放置在任意命名空间之外 |
| `YOMK_DEFINE_MSG_NS(NS, DataType, MsgName, VarName)` | 同上，但生成类落 `yomk::msgs::NS` 子命名空间、`kTypeName` 为 `"yomk::msgs::NS::MsgName"`；工程/扩展自有消息用它，避免占用 `yomk::msgs` 根（`NS` 为 C++17 嵌套命名空间 token，不得含空格；载荷类型与 NS 或祖先同名时写根限定名 `::` 开头） |
| `yomk::msgs::MsgName::shared(args...)` | 类内静态工厂，转发构造参数创建 `shared_ptr<MsgName>` 包装的消息包 |
| `msg->typeName()` | 运行期类型名读取：基类纯虚 `typeName()` 返回各类型静态 `kTypeName`（`std::string_view`，零分配）；期望类型直接用 `T::kTypeName`，反查实际类型用本接口（空包需调用方自行判空） |
| `msg->clone()` | 拷贝构造出可变副本，返回 `YomkConstMsgPtr`（`shared_ptr<const YomkMsg>`）；写时拷入的唯一途径（Context 存储、快照发布），宏为每个消息类型自动实现；副本隔离深度取决于载荷自身拷贝语义，含指针成员时见「[载荷拷贝语义](#载荷拷贝语义指针成员与深拷贝)」 |
| `yomk::msgs::unpack<yomk::msgs::MsgName>(msg)` | 按消息类型解包模板：返回 `UnpackResult<MsgName>`；命中时 `if (r)` 判真、`r->d` 访问载荷（`operator->` / `operator*` / `r.msg()` 出口均为 `const`）；未命中时结果判假，`r.error()` 给出标准文案 `"msg is not <期望类型>, got '<实际类型>'"`（空包报 `"null"`）；不抛异常，失败处理由调用方手写（如 `if (!r) return {...}`）；需把消息指针交给其他接口时取 `r.msg()`（`shared_ptr<const Msg>`） |

## 内置标准类型

内置 30 种标量/数组类型消息包（成员名均为 `d`），可直接用 `yomk::msgs::String::shared(...)` 等方式构造：

| 标量 | 数组 |
|---|---|
| `Bool`、`Char`、`UChar`、`Byte`、`Int8`、`Uint8`、`Int16`、`Uint16`、`Int32`、`Uint32`、`Int64`、`Uint64`、`Float32`、`Float64`、`String` | `BoolArray`、`CharArray`、`UCharArray`、`ByteArray`、`Int8Array`、`Uint8Array`、`Int16Array`、`Uint16Array`、`Int32Array`、`Uint32Array`、`Int64Array`、`Uint64Array`、`Float32Array`、`Float64Array`、`StringArray` |

> 以下为用法片段；最小完整程序（含头文件与 main）见根目录 [README](../README.md)「快速上手」，封包/解包行为验证见 `test/yomk_server/TestYomkMsg.cpp`。

## 使用示例

```cpp
// 1. 声明自定义消息（建议放置在命名空间之外）
struct UserInfo {
    int id;
    std::string name;
};
YOMK_DEFINE_MSG(UserInfo, UserInfoMsg, d);

// 2. 构造消息包
auto msg = yomk::msgs::UserInfoMsg::shared(UserInfo{1001, "Alice"});

// 3. 功能函数中校验并解包（类型不匹配时结果判假，失败原因见 info.error()）
auto info = yomk::msgs::unpack<yomk::msgs::UserInfoMsg>(msg);
if (!info)
{
    return {yomk::payload::ResStatus::Failed, info.error()};
}
return {yomk::payload::ResStatus::Succeeded, "hello " + info->d.name};
```

消息包即插即用于各域链：

```cpp
yomk::server().url("/MyService/register").msg(msg).request();                                  // 请求负载
yomk::ctx().key("user").value(msg).create();                                                   // 上下文存储
yomk::event_loop().msg(msg).loop("TaskLoop").post();                                           // 事件数据
yomk::fn_pool().fn_name("save").msg(msg).call();                                               // 函数池调用
```

## 载荷拷贝语义（指针成员与深拷贝）

宏生成的 `clone()` 固定为 `std::make_shared<MsgName>(*this)`：**整条消息按拷贝构造重建**，逐一拷贝构造载荷各成员。它从不调用载荷上任何名为 `clone()` 的成员函数——在 `DataType` 里写 `clone()` 方法不会有任何调用点。副本隔离到什么深度，完全由 `DataType` 自己的拷贝构造决定：

| 载荷成员 | 拷贝行为 | 隔离性 |
|---|---|---|
| 标量、`std::string`、`std::vector` 等值成员 | 拷贝构造即深拷贝 | 完全隔离（内置 30 种标准类型均属此类） |
| 裸指针/句柄成员（`T*`、`void*`、fd 等） | 只拷贝指针值（浅拷贝） | 克隆件与原件共享底层对象 |
| `std::shared_ptr<T>` | 只拷贝控制块引用 | 共享，但类型安全且生命周期有保障 |

宏内 `static_assert(std::is_copy_constructible<DataType>)` 只把关“拷贝构造**存在**”（拦得住 `std::unique_ptr` 成员、引用成员、`= delete` 拷贝这类编不过 clone 的载荷）；**拷贝语义深浅由载荷作者负责**。含指针成员且需要真隔离时，在载荷上自定义拷贝构造/拷贝赋值（Rule of Three）：

```cpp
struct Image {
    uint8_t* pixels = nullptr;
    size_t   size   = 0;

    Image() = default;                                   // 必须保留：宏里 DataType VarName{}; 是值初始化
    Image(const Image& o) : size(o.size), pixels(new uint8_t[o.size])
    {
        std::memcpy(pixels, o.pixels, size);             // 深拷贝写在这里
    }
    Image& operator=(const Image& o) { /* 深拷贝赋值 */ }
    ~Image() { delete[] pixels; }
};
YOMK_DEFINE_MSG(Image, Image, d);
// msg->clone() → make_shared<ImageMsg>(*this) → Image(const Image&) → 自动深拷贝
```

要点：

- 写好载荷拷贝构造后，`clone()`、Context `create/set` 入库克隆、`get_copy()` 全部自动变深拷贝，无需改框架或宏。
- `void*` 无法深拷贝：不知道指向元素的类型与长度，`memcpy` 尺寸无从得知；需深拷贝的指针载荷必须自带类型化指针与长度（如上 `Image`），或直接用值类型成员。
- 反向用法：句柄语义、明确不许拷贝的消息，把载荷拷贝构造 `= delete`，宏展开处的 `static_assert`（"message payload must be copyable for clone()"）随即报错，倒逼显式决策。
- 浅拷贝与只读分界线正交：`YomkConstMsgPtr` 只让消息对象为 const（`msg->d.ptr` 是 `T* const`，指针不可重绑），指向对象仍可经指针写穿透——句柄语义下这是约定，不是框架保证。

## 注意事项

- 类型匹配以运行期类型名为准：`kTypeName` 为全限定类型标识（根级 `"yomk::msgs::MsgName"`、NS 形式 `"yomk::msgs::NS::MsgName"`），恒等于 C++ 全限定名。各处声明的"期望消息类型名"——`installFunc("/x", &C::m, yomk::msgs::Msg::kTypeName)` 第三参、`yomk::fn_pool().msg_name(...)`、`yomk::event_loop().msg_name(...)`——一律引用 `T::kTypeName`，**禁止手写消息名字符串**；仅作内省元数据，不拦截调用。
- `kTypeName` 是类型身份的唯一权威常量；`typeName()` 为基类纯虚函数，直接返回各类型的静态 `kTypeName`（`std::string_view`，零分配、不可伪造），是运行期反查实际类型的唯一入口（判空与自有 `std::string` 拼接由调用方负责，框架内 `unpack` 已封装）；`unpack` 以“`typeName()` 比对 + `dynamic_pointer_cast<const Msg>`”两步完成解包（项目内唯一保留的 cast），失败文案随结果对象的 `error()` 返回。
- 期望类型名与说明字符串均供自省读取（函数池与函数单查返回 `FuncInfo`：`funcName` / `msgName` / `description`，见 [服务自省](YomkServerInfo.md)）。
- 上下文类型化读自带同类校验：`yomk::ctx().key(k).msg<MsgName>()...get()` 内部就是 `unpack<MsgName>(response.data)`，与 `unpack` 语义一致，返回 `shared_ptr<const MsgName>`。

## 常见问题 FAQ

- **`unpack` 类型不匹配返回什么？** 结果对象判假（`if (!r)`），不抛异常；失败处理由调用方手写（如 `if (!r) return {...}`），具体文案取 `r.error()`。
- **期望消息类型名会拦截调用吗？** 不会：`installFunc` 第三参、`fn_pool().msg_name(m)`、`event_loop().msg_name(m)` 等仅作内省元数据，调用不做类型检查，类型校验只发生在 `unpack` / 上下文 `get()`。
- **载荷成员必须叫 `d` 吗？** 宏第三参自定义，框架约定统一取 `d`，跟随约定可读性最好。
- **载荷里有指针，`clone()` 会拷贝指向的对象吗？** 不会默认拷贝：宏 clone 走载荷拷贝构造，裸指针成员只拷贝指针值（克隆件与原件共享底层对象）；需要真隔离就在载荷上自定义拷贝构造/拷贝赋值，或改用值类型成员（详见「载荷拷贝语义」）。

## 相关跳转

- [服务总线](YomkServer.md)：消息包作为请求负载的消费端
- [服务自省](YomkServerInfo.md)：期望消息类型名与函数说明如何进入自省
- 内置类型封包/解包行为验证：`test/yomk_server/TestYomkMsg.cpp`

## 完整示例

最小完整示例见根目录 [README](../README.md) 的"快速上手"一节；内置类型的封包/解包行为验证见 `test/yomk_server/TestYomkMsg.cpp`。
