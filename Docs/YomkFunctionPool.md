# YomkFunctionPool(动态函数池)

跨服务、动态注册与调用的扁平函数中心：按名注册函数、按名直接调用，同名重复注册即运行时热替换。随 `YOMK_INIT()` 自动启动。

## API 速查

| API | 说明 |
|---|---|
| `YOMK_FUNCTIONPOOL_REGISTER(name, func)` | 注册函数 |
| `YOMK_FUNCTIONPOOL_REGISTER(name, func, MsgName)` | 注册并声明期望的消息类型名（仅作内省元数据） |
| `YOMK_FUNCTIONPOOL_CALL(name, pkg)` | 按名调用函数（返回值即函数返回值）；未注册的名字返回 `eNo=1` |
| `YOMK_FUNCTIONPOOL_UNREGISTER(name)` | 按名字从函数表移除 |
| `YOMK_FUNCTIONPOOL_INFO_NAMES()` | 获取全部函数名 |
| `YOMK_FUNCTIONPOOL_INFO_NAME(name)` | 查询单个已注册函数元信息 |
| `YOMK_FUNCTIONPOOL_INFO_ALL()` | 查询全部已注册函数元信息 |

## 使用示例

```cpp
// 注册函数（第三个参数可选：声明期望的消息类型名，仅作内省元数据）
YOMK_FUNCTIONPOOL_REGISTER("calcSum", [](YomkPkgPtr pkg) {
    return YomkResponse{YomkResponse::eOk, "result"};
});

// 调用函数（返回值即函数返回值；未注册的名字返回 eNo=1）
YomkResponse resp = YOMK_FUNCTIONPOOL_CALL("calcSum", nullptr);

// 同名重复注册即运行时热替换，无需注销
YOMK_FUNCTIONPOOL_REGISTER("calcSum", newFunc);

// 注销（按名字从函数表移除）
YOMK_FUNCTIONPOOL_UNREGISTER("calcSum");
```

## 注意事项

- 同名重复注册即运行时热替换，无需先注销。
- 未注册的名字调用返回 `eNo=1`。
- 服务成员函数注册到函数池时须用 `YomkBindWeakSelf` 弱绑定——服务删除后回调自动失效，见 [YomkServer.md](YomkServer.md) 弱绑定一节。

## 完整示例

完整可运行演示见 `Examples/ExampleYomkFunctionPool.cpp`（6 步覆盖全部函数池 API，读运行输出即可理解每个调用）。
