# YomkProject

基于 [YomkServer](https://github.com/YomkServer) 模块化框架的空白工程模板。

YomkServer 是基于 C++17 的模块化高性能服务开发框架，核心设计理念：**「一切皆服务，一切皆请求」**。

## 前置条件

- C++17 编译器
- CMake >= 3.14
- YomkServer 已安装到系统

### 安装 YomkServer

```bash
# 在 YomkServer 源码目录下
mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build . --target install --config Release
```

## 工程结构

```
YomkProject/
├── main.cpp                // 程序入口
├── boot/
│   ├── MyBoot.h            // 生命周期管理
│   └── MyBoot.cpp
├── msgs/
│   └── YomkMsgs.h          // 消息包定义
├── services/               // 服务实现（按业务类别分子目录）
└── CMakeLists.txt
```

| 目录 | 职责 |
|------|------|
| `boot/` | 程序生命周期管理（before/start/after），负责资源初始化的编排 |
| `msgs/` | 所有服务间通信的消息包定义集中管理 |
| `services/` | 所有服务实现，按业务类别分子目录存放 |

## 编译

```bash
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

> 若 YomkServer 安装在非默认路径，需指定：
> `cmake .. -DCMAKE_PREFIX_PATH=/your/install/prefix`

## 生命周期说明

`MyBoot` 管理程序三阶段生命周期：

| 阶段 | 方法 | 用途 |
|------|------|------|
| 启动前 | `before()` | 创建 Context、EventLoop、注册 FunctionPool、设置日志代理等 |
| 启动中 | `start()` | 注册并启动服务 |
| 启动后 | `after()` | 调用服务接口做初始化、自启动任务等 |

## 核心组件速查

| 组件 | 用途 | 关键宏 |
|------|------|--------|
| YomkContext | 全局 K-V 状态管理 | `YOMK_CONTEXT_CREATE` / `YOMK_CONTEXT_GET` / `YOMK_CONTEXT_SET` |
| YomkEventLoop | 线程隔离事件循环 | `YOMK_EVENTLOOP_START` / `YOMK_EVENTLOOP_POST` |
| YomkFunctionPool | 动态公共函数池 | `YOMK_FUNCTIONPOOL_REGISTER` / `YOMK_FUNCTIONPOOL_CALL` |
| YomkLogger | 多级别日志系统 | `YOMK_INFO` / `YOMK_ERROR` / `YOMK_FILE_LOG_CREATE` |
