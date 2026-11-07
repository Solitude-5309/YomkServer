# YomkMsg（消息包）

业务数据的安全封装与传递：Python 版**普通类即消息定义**——实例直接作为请求负载传入功能函数、经回包 `data` 原样返回，属性直接访问，无序列化封套、无指针封送。消息在请求域、共享上下文、事件循环与函数池四条链上流转，形态完全一致。

## API 速查

| 操作 | Python 形态 |
|---|---|
| 定义消息 | 普通类（构造函数即字段声明），可集中在独立模块管理（见 `examples/msgs/ArmMsgs.py`） |
| 构造消息包 | 直接实例化：`MoveCmd(100.0, 50.0, 200.0, 0.8)` |
| 接收端取数 | 功能函数体内直接读参数属性；需要类型收窄时自行 `isinstance` 判断 |
| 请求负载 | `YomkApi.server().url(u).msg(pkg).request()`，`pkg` 任意对象，缺省 `None` |

```python
# 1. 定义消息（普通类）
class UserInfo:
    def __init__(self, uid, name):
        self.uid = uid
        self.name = name

# 2. 构造并传入
pkg = UserInfo(1001, "Alice")

# 3. 功能函数中取数（类型不符的处理由调用方手写）
def register(self, pkg):
    if not isinstance(pkg, UserInfo):
        return YomkApi.YomkResponse(YomkApi.ResStatus.eNo, "pkg is not UserInfo")
    return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "hello " + pkg.name)
```

消息包即插即用，各域链的 `.msg()` / `.value()` 分面同形：

```python
YomkApi.server().url("/MyService/register").msg(pkg).request()      # 请求负载
YomkApi.ctx().key("user").value(pkg).create()                        # 上下文存储
YomkApi.event_loop().msg(pkg).loop("TaskLoop").post()                # 事件数据
YomkApi.fn_pool().fn_name("save").msg(pkg).call()                    # 函数池调用
```

## 语言设计说明

- 无需宏：普通类即消息定义。
- 无需静态工厂与指针基类流转：直接实例化，对象引用天然多态。
- 无需解包：属性直接访问；类型守卫自行 `isinstance`。
- 无内置类型表：Python 原生类型（`str/int/float/bool/list/dict`）与自定义类均可直传。
- `.msg(cls)` 收运行时类对象参数（仅作定位，见下）。

## 注意事项

- **期望消息类型名仅作内省元数据，不拦截调用**：`install_func("/x", fn, "MsgName")` 第三参、`YomkApi.fn_pool().msg_name(m)`、`YomkApi.event_loop().msg_name(m)` 声明的字符串与 `FuncInfo.msg_name` 保持一致，供自省读取，运行期不做校验。
- 期望类型名与函数说明均供自省读取：函数池与函数单查返回 `FuncInfo`（`func_name` / `msg_name` / `description` 三字段，见 [服务自省](YomkServerInfo.md)）。
- 上下文类型化读 `YomkApi.ctx().key(k).msg(cls).with_default(d).get()` 中的 `cls` 为类型指定位语法糖；**`get()` 不做类型校验**，未命中/失败一律回退 `with_default` 的兜底值。
- 事件循环 `post_wait` 的执行结果经事件对象回传：链式终结收敛为 `ResStatus`；需要取回事件 `data`/处理响应时用裸链 `YomkApi.server().url("/YomkEventLoop/post_wait").msg(...).request()`，`data` 为该 `Event` 对象（含 `m_eventId` / `m_response`）。

## 完整示例

最小完整示例见根目录 [README](../README.md) 的"快速上手"一节；消息直传与属性读取的行为验证见 `test/YomkServer/TestYomkPkg.py`，多文件工程的集中管理范式见 `examples/msgs/ArmMsgs.py`。
