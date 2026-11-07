# YomkFunctionPool（动态函数池）

跨服务、动态注册与调用的扁平函数中心：按名注册函数、按名直接调用，同名重复注册即运行时热替换。随 `YomkApi.server().init()` 自动启动。全部操作经链式域入口 `YomkApi.fn_pool()`：函数名由 `.fn_name(name)` 定位，回调 / 期望消息类型名 / 说明 / 调用数据前置累积，终结动作全部无参。

## 链式语法速查

| 场景 | 链 | 终结与返回 |
|---|---|---|
| 注册 | `YomkApi.fn_pool().fn_name(name).fn(f).msg_name(m).description(d)` | `.install()` → `ResStatus`；`msg_name`（期望消息类型名）与 `description`（函数说明）均为内省元数据，可省略 |
| 注销 | `YomkApi.fn_pool().fn_name(name)` | `.uninstall()` → `ResStatus`（名字不存在 `Failed`） |
| 调用 | `YomkApi.fn_pool().fn_name(name).msg(data)` | `.call()` → `YomkResponse`（即函数返回值；`msg` 可省略 = 空消息；**阻塞至函数执行完成**；名字不存在 `Failed`） |
| 内省 | `YomkApi.fn_pool().fn_name(name)` / — | `.get_info()` 单函数元信息 / `.list()` 函数名清单 / `.all()` 全量元信息（`FuncInfo` 列表），均 → `YomkResponse` |

## 使用示例

```python
import YomkApi

YomkApi.server().init()

# 注册函数（msg_name/description 可选，仅作内省元数据）
def calc_sum(pkg):
    if not isinstance(pkg, list):
        return YomkApi.YomkResponse(YomkApi.ResStatus.eNo, "pkg is not list")
    return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "ok", sum(pkg))

YomkApi.fn_pool().fn_name("calcSum").fn(calc_sum) \
    .msg_name("list").description("对列表求和").install()

# 调用（返回值即函数返回值；未注册的名字返回 ResStatus.Failed）
resp = YomkApi.fn_pool().fn_name("calcSum").msg([1, 2, 3]).call()
print(resp.data)                                    # 结果在 data

# 同名重复 install 即运行时热替换，无需先注销（返回 Succeeded）
YomkApi.fn_pool().fn_name("calcSum").fn(new_func).install()

# 注销与内省
YomkApi.fn_pool().fn_name("calcSum").uninstall()
YomkApi.fn_pool().list()
```

## 注意事项

- 同名重复注册即运行时热替换（覆盖旧函数并返回 `Succeeded`），无需先注销；服务端错误日志实串 `"find function name is already exist, update to current function"`。
- 未注册的名字：`call()` / `uninstall()` 均返回 `Failed`（not-found 归 `Failed` 不归 `Invalid`，与全框架三态约定一致）。
- 注册/注销的终结动词定名为 `install()` / `uninstall()`。
- 入参非法（`install` 空名/空函数等）返回 `Invalid`（服务端契约消息 `"funcName or func is empty"`）。
- 函数体收到的 `pkg` 即调用侧 `.msg()` 累积的对象；函数返回的 `YomkResponse` 整体作为 `call()` 的返回值回传（`data` 携带结果对象）。
- 服务成员函数注册到函数池时须用 `self.weak_func(...)` 弱绑定——服务删除后回调自动失效（返回 `Failed`），见 [YomkServer.md](YomkServer.md) 弱绑定一节。
- 单函数内省 `get_info()` 的 `data` 为 `FuncInfo`（`func_name` / `msg_name` / `description`，见 [服务自省](YomkServerInfo.md)）。

## 完整示例

完整可运行演示见 `examples/ExampleYomkFunctionPool.py`（机械臂产线场景覆盖注册/调用、类型与说明声明、同名热替换与内省，读运行输出即可理解每个调用）。
