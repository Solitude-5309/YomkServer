# YomkServerPy 文档

每个分片自包含：可单独打开、单独理解、单独验证，无需按顺序阅读。本文档讲场景、组合与踩坑，源码 docstring 讲 API 精确契约，两者配套使用。

## 模块分片

| 分片 | 一句话摘要 |
|---|---|
| [YomkServer / YomkService](YomkServer.md) | 进程内服务总线：URL 路由同步/异步调用、服务生命周期、弱绑定、启动编排 |
| [YomkMsg](YomkMsg.md) | 轻量消息包：继承 `YomkMsg` 即接入 `kName` 类型系统、`unpack` 校验解包 |
| [YomkLogger](YomkLogger.md) | 日志系统：控制台/文件双路输出、六级门控、代理拦截、位置前缀 |
| [YomkContext](YomkContext.md) | 共享上下文：跨服务键值、Checker 校验门控、Monitor 变更监听 |
| [YomkEventLoop](YomkEventLoop.md) | 事件循环：具名单线程 FIFO 队列，循环间并行、停启续跑 |
| [YomkFunctionPool](YomkFunctionPool.md) | 函数池：按名注册与调用，同名重注册即热替换 |
| [YomkServerInfo](YomkServerInfo.md) | 服务自省：运行期查询服务拓扑、函数清单与期望消息类型 |

## 规范与配套

- [yomk注释原则](yomk注释原则.md)：公开 API 双语 docstring（PEP 257 + Google 风格）、内部注释单语中文——内容参考 C++ 规范，形式按 Python 主流
- [内省描述惯例](YomkServer.md#内省描述惯例第四参-description)：`install_func` 第四参的五章节固定格式与 URL 常量池布局（全部内置端点均适用）
- [skills/yomkserverpy-modular](../skills/yomkserverpy-modular/SKILL.md)：模块化编程规范（扩展开发）
- API 精确契约：源码 docstring（`src/yomk/` 与其 `.pyi` 类型桩），逐参数与返回码以之为准
