# YomkPkg(消息包)

强类型消息包：业务数据通过 `YomkPkg` 及其派生类安全封装与传递，支持移动语义与动态类型转换，用于在服务与事件循环间传递数据。

## API 速查

| API | 说明 |
|---|---|
| `YomkMsg(DataType, MsgName, VarName)` | 声明自定义消息类型（建议放置在命名空间之外） |
| `YomkMkPtr(MsgName, ...)` | 创建 `shared_ptr` 包装的消息包 |
| `YomkUnPackPkg(pkg, MsgName, ptr)` | 校验并解包；类型不匹配时指针置为 `nullptr`，不提前返回，适合需要自定义错误处理的场景 |
| `YomkUnPackPkgResponse(pkg, MsgName, ptr)` | 类型不匹配时直接以 `YomkResponse::eNo` 从当前函数返回，适合功能函数 |
| `YomkUnPackPkgVoid(pkg, MsgName, ptr)` | 类型不匹配时直接 `return`，适用于 `void` 回调 |

## 内置标准类型

内置 30 种标量/数组类型消息包（成员名均为 `d`），可直接用 `YomkMkPtr(String, ...)` 等方式构造：

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
YomkMsg(UserInfo, UserInfoMsg, data);

// 2. 构造与传递消息包（创建 shared_ptr 包装的消息包）
auto pkg = YomkMkPtr(UserInfoMsg, UserInfo{1001, "Alice"});

// 3. 功能函数中校验并解包
YomkUnPackPkg(pkg, UserInfoMsg, info); // 类型校验与解包
if (!info)
{
    return {YomkResponse::eNo, "pkg is not UserInfoMsg"};
}
return {YomkResponse::eOk, "hello " + info->d.name};
```

## 注意事项

- 消息按类型名（宏的 `MsgName` 参数）在运行期匹配；`installFunc("/x", &C::m, "MsgName")` 第三参、`YOMK_FUNCTIONPOOL_REGISTER`、`YOMK_EVENTLOOP_START` 三参形式声明的期望消息类型名与此一致，供自省识别（`installFunc` 另有第四参为函数说明字符串，仅内省透传不解析）。
- `YomkMsg` 生成的消息类支持拷贝与移动构造（移动语义）。

## 完整示例

最小完整示例见根目录 [README](../README.md) 的"快速上手"一节；内置类型的封包/解包行为验证见 `Test/YomkServer/TestYomkPkg.cpp`。
