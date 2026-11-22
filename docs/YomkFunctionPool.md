# YomkFunctionPool（动态函数池）

跨服务、动态注册与调用的扁平函数中心：按名注册函数、按名直接调用，同名重复注册即运行时热替换。随 `server().init()` 自动启动。全部操作经链式域入口 `fn_pool()`：函数名由 `.fn_name(name)` 定位，回调 / 期望消息类型名 / 说明 / 调用数据前置累积，终结动作全部无参。

## 链式语法速查

| 场景 | 链 | 终结与返回 |
|---|---|---|
| 注册 | `fn_pool().fn_name(name).fn(f).msg_name(m).description(d)` | `.install()` → `ResStatus`；`msg_name`（期望消息类型名，**一律传 `msg.TYPE_NAME`**，内省值为全限定 TYPE_NAME）与 `description`（函数说明）均为内省元数据，可省略 |
| 注销 | `fn_pool().fn_name(name)` | `.uninstall()` → `ResStatus`（名字不存在 `Failed`） |
| 调用 | `fn_pool().fn_name(name).msg(msg)` | `.call()` → `Response`（即函数返回值）；`msg` 必须是具体消息实例，省略 = 空载荷 `None`（裸值在 `msg()` 入口当场 `TypeError`）；**阻塞至函数执行完成**；名字不存在 `Failed` |
| 内省 | `fn_pool().fn_name(name)` / — | `.get_info()` 单函数元信息 / `.list()` 函数名清单 / `.all()` 全量元信息（`FuncInfo` 列表），均 → `Response` |

## 使用示例

```python
from yomk.msgs import IntArray, type_mismatch, unpack
from yomk.msgs import IntArray, type_mismatch, unpack
from yomk.msgs.server import ResStatus, Response
from yomk.server import server
from yomk.fn_pool import fn_pool

server().init()

# 注册函数（msg_name 一律传 msg.TYPE_NAME，description 可选，均仅作内省元数据；类型校验由函数体内 unpack 承担，见 docs/YomkMsg.md）
def calc_sum(msg):
    nums = unpack(msg, IntArray)
    if nums is None:
        return Response(ResStatus.eNo, type_mismatch(msg, IntArray.TYPE_NAME))
    return Response(ResStatus.eOk, "ok", sum(nums.items))

fn_pool().fn_name("calcSum").fn(calc_sum) \
    .msg_name(IntArray.TYPE_NAME).description("对整数列表求和").install()

# 调用（返回值即函数返回值；未注册的名字返回 ResStatus.Failed）
resp = fn_pool().fn_name("calcSum").msg(IntArray([1, 2, 3])).call()
print(resp.data)                                    # 结果在 data

# 同名重复 install 即运行时热替换，无需先注销（返回 Succeeded）
fn_pool().fn_name("calcSum").fn(new_func).install()

# 注销与内省
fn_pool().fn_name("calcSum").uninstall()
fn_pool().list()
```

## 注意事项

- 同名重复注册即运行时热替换（覆盖旧函数与 msg_name/description 元数据并返回 `Succeeded`），无需先注销；诊断 WARN 与 `Response.msg` 同串 `"function '<name>' already exists, hot-swap to current function"`（覆盖既有注册默认门控下可见）。
- 未注册的名字：`call()` / `uninstall()` / `get_info()` 均返回 `Failed` + `"function '<name>' is not registered"`（not-found 归 `Failed` 不归 `Invalid`，与全框架三态约定一致；失败路径不打诊断日志，`Response.msg` 已携带同信息）。
- 注册/注销的终结动词定名为 `install()` / `uninstall()`。
- 入参非法（`install` 空名/空函数等）返回 `Invalid`（服务端契约消息 `"funcName or func is empty"`）。
- 端点收错类型包（如向 `/register` 传非 `Function` 消息）返回 `Failed` + `"msg is not X, got '<实际类型>'"`，不抛异常（对齐 C++ unpack 失败契约）；调用入口 `.msg()` 传裸值则是 `TypeError`，见 [YomkMsg.md](YomkMsg.md#载荷契约跨边界一律消息实例)。
- 内置服务端点 URL 常量集中定义于 `yomk.url` 常量池（`from yomk.url.fn_pool import FP_CALL` 等，命名 `FP_` 前缀对齐 C++ `yomk::url::fpXxx`）；`yomk.url` 为纯命名空间层（与 `yomk.msgs` 同构），用户新增模块只需自带 `yomk/url/<模块>/__init__.py` 即可并入。`fn_pool()` 链式入口与用户裸链请求均可引用。
- 函数体收到的 `msg` 即调用侧 `.msg(msg)` 累积的消息实例（省略即 `None` 空载荷）；函数返回的 `Response` 整体作为 `call()` 的返回值回传（`data` 携带结果对象）。
- 服务成员函数注册到函数池时须用 `self.weak_func(...)` 弱绑定——服务删除后回调自动失效（返回 `Failed`），见 [YomkServer.md](YomkServer.md) 弱绑定一节。
- 单函数内省 `get_info()` 的 `data` 为 `FuncInfo`（`func_name` / `msg_name` / `description`，见 [服务自省](YomkServerInfo.md)）。

## 完整示例

完整可运行演示见 `examples/ExampleYomkFunctionPool.py`（机械臂产线场景覆盖注册/调用、类型与说明声明、同名热替换与内省，读运行输出即可理解每个调用）。
