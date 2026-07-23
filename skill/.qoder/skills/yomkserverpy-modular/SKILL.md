---
name: yomkserverpy-modular
description: 基于YomkServerPy框架的模块化Python工程编程。核心理念"一切皆服务，一切皆请求"。用于创建YomkService功能模块、使用YomkContext全局状态管理、YomkEventLoop事件循环、YomkFunctionPool公共函数池。当用户需要编写YomkServerPy模块化代码、创建服务、管理全局状态、处理事件队列、注册公共函数或搭建工程结构时使用。
---

# YomkServerPy 模块化编程框架

## 框架概述

基于 Python 的模块化服务框架，核心理念：**「一切皆服务，一切皆请求」**。

| 组件 | 职责 |
|------|------|
| **YomkServer** | 服务容器，管理生命周期 |
| **YomkService** | 功能模块单元，通过 URL `/服务名/功能名` 访问 |
| **YomkContext** | 全局 K-V 状态管理（Checker防非法迁移、Monitor监听变更） |
| **YomkEventLoop** | 线程隔离事件循环（同循环顺序执行、不同循环并行） |
| **YomkFunctionPool** | 动态函数池（运行时注册/热替换） |

唯一入口：`import YomkApi`

## 任务一：生成工程模板

当用户要求创建新工程时，**必须**生成完整可运行的工程骨架。

### 目录结构

```
ProjectName/
├── main.py               // 入口：YomkApi.boot 启动
├── boot/
│   └── MyBoot.py         // 生命周期：before/start/after
├── config/
│   └── config.json       // 配置文件
├── msgs/
│   └── YomkMsgs.py       // 所有消息包定义
├── services/
│   └── ConfigService.py  // 内置配置服务
├── typedefine/
│   └── TypeDefine.py     // 公共常量定义
├── test/                 // 单元测试脚本
├── scripts/              // 项目辅助脚本
└── README.md
```

### 关键约定

1. `MyBoot.before()`：通过 `Path(__file__)` 推导配置路径，存入 Context（`CTX_CONFIG_PATH`）
2. `MyBoot.start()`：服务创建器映射表 + `srv_names` 按需启动
3. `MyBoot.after()`：调用 `/ConfigService/load` 加载配置
4. 消息包为普通 Python 类，集中定义在 `msgs/YomkMsgs.py`
5. 运行方式：`python main.py`（需设置 PYTHONPATH 指向 YomkServerPy 安装路径）

### 生成规则

- 完整文件内容参见 [examples.md](examples.md) 示例0
- 将 `ProjectName` 替换为用户指定名称
- 所有文件必须完整生成，确保 `python main.py` 可直接运行

## 任务二：扩展业务服务

在已有工程中添加新服务的标准步骤：

### 步骤

1. **定义消息包**（`msgs/YomkMsgs.py`）：
```python
class MyData:
    def __init__(self, field1: str, field2: int):
        self.field1 = field1
        self.field2 = field2
```

2. **创建服务文件**（`services/XxxService.py`）：
```python
import YomkApi

class XxxService(YomkApi.YomkService):
    def __init__(self, server):
        super().__init__(server)
        self.set_name("/XxxService")

    def init(self):
        self.install_func("/my_func", self.my_func)
        print(f"install func [ /my_func ] to {self.get_name()}")

    def my_func(self, pkg):
        # pkg 即调用方传入的消息对象
        # 业务逻辑...
        return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "success")
```

3. **注册到 Boot**（`boot/MyBoot.py` 的 `start()` 映射表中添加）：
```python
from services.XxxService import XxxService
# start() 中 cur_srvs 字典添加：
"/XxxService": XxxService(YomkApi.server()),
```

4. **启动列表添加**（`main.py` 的 `MyBoot` 参数中）：
```python
YomkApi.boot(MyBoot(["/ConfigService", "/XxxService"]))
```

### 跨服务调用

```python
# 同步
resp = YomkApi.request("/XxxService/my_func", MyData("hello", 1))
# 异步
YomkApi.async_request("/XxxService/my_func", MyData("hello", 1), lambda resp: print(resp.msg))
```

## 编程规范

### 消息包

- 消息包为普通 Python 类，定义在 `msgs/YomkMsgs.py`
- 作为 `pkg` 参数传入服务函数，直接通过属性访问字段
- 返回值通过 `YomkResponse.data` 携带任意数据

### 设计原则

1. 每个 Service 只负责单一业务域
2. 服务间通过 `YomkApi.request` 通信，不直接引用
3. 共享状态用 Context，耗时操作用 EventLoop，公共函数用 FunctionPool
4. 消息定义集中 `msgs/`，服务实现放 `services/`
5. 配置路径通过 Context 传递，不用构造参数

## 详细参考

- 完整工程代码：[examples.md](examples.md)
- API 详细参考：[reference.md](reference.md)
