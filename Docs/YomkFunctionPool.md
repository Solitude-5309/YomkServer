# YomkFunctionPool(动态函数池)

跨服务、动态注册与调用的扁平函数中心：按名注册函数、按名直接调用，同名重复注册即运行时热替换。随 `YomkAPI::init()` 自动启动。

## API 速查

| API | 说明 |
|---|---|
| `YomkAPI::FUNCTIONPOOL_REGISTER(name, func)` | 注册函数 |
| `YomkAPI::FUNCTIONPOOL_REGISTER(name, func, "MsgName")` | 注册并声明期望的消息类型名（仅作内省元数据） |
| `YomkAPI::FUNCTIONPOOL_REGISTER(name, func, "MsgName", "Description")` | 同上，追加函数说明（自由字符串，INFO_NAME 数据段可读） |
| `YomkAPI::FUNCTIONPOOL_CALL(name, pkg)` | 按名调用函数（返回值即函数返回值）；未注册的名字返回 `eNo=1` |
| `YomkAPI::FUNCTIONPOOL_UNREGISTER(name)` | 按名字从函数表移除 |
| `YomkAPI::FUNCTIONPOOL_INFO_NAMES()` | 获取全部函数名 |
| `YomkAPI::FUNCTIONPOOL_INFO_NAME(name)` | 查询单个已注册函数元信息 |
| `YomkAPI::FUNCTIONPOOL_INFO_ALL()` | 查询全部已注册函数元信息（FuncInfoList 结构化包） |

## 使用示例

```cpp
// 注册函数（第三参可选：声明期望的消息类型名；第四参可选：函数说明，均仅作内省元数据）
YomkAPI::FUNCTIONPOOL_REGISTER("calcSum", [](YomkPkgPtr pkg) {
    return YomkResponse{YomkResponse::eOk, "result"};
});

// 调用函数（返回值即函数返回值；未注册的名字返回 eNo=1）
YomkResponse resp = YomkAPI::FUNCTIONPOOL_CALL("calcSum", nullptr);

// 同名重复注册即运行时热替换，无需注销
YomkAPI::FUNCTIONPOOL_REGISTER("calcSum", newFunc);

// 注销（按名字从函数表移除）
YomkAPI::FUNCTIONPOOL_UNREGISTER("calcSum");
```

## 注意事项

- 同名重复注册即运行时热替换，无需先注销。
- 未注册的名字调用返回 `eNo=1`。
- 服务成员函数注册到函数池时须用 `weakFunc(&Class::Method)` 弱绑定——服务删除后回调自动失效，见 [YomkServer.md](YomkServer.md) 弱绑定一节。

## 完整示例

完整可运行演示见 `Examples/ExampleYomkFunctionPool.cpp`（6 个机械臂产线场景小示例 showArmXXX 覆盖全部函数池 API，读运行输出即可理解每个调用）。
