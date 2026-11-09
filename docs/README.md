# YomkServer 文档

每个分片自包含：可单独打开、单独理解、单独验证，无需按顺序阅读。结构与写法遵循 [yomk文档原则](yomk文档原则.md)，与源码注释（[yomk注释原则](yomk注释原则.md)）配套——头文件 Doxygen 是 API 精确契约，本文档讲场景、组合与踩坑。

## 模块分片

| 分片 | 一句话摘要 |
|---|---|
| [YomkServer / YomkService](YomkServer.md) | 进程内服务总线：URL 路由同步/异步调用、服务生命周期、弱绑定 |
| [YomkMsg](YomkMsg.md) | 强类型消息包：`YOMK_DEFINE_MSG` 声明、`shared` 构造、`unpack` 校验解包 |
| [YomkLogger](YomkLogger.md) | 日志系统：控制台/文件双路输出、六级门控、代理拦截、位置前缀 |
| [YomkContext](YomkContext.md) | 共享上下文：跨服务强类型键值、Checker 校验门控、Monitor 变更监听 |
| [YomkEventLoop](YomkEventLoop.md) | 事件循环：具名单线程 FIFO 队列，循环间并行、停启续跑 |
| [YomkFunctionPool](YomkFunctionPool.md) | 函数池：按名注册与调用，同名重注册即热替换 |
| [YomkServerInfo](YomkServerInfo.md) | 服务自省：运行期查询服务拓扑、函数清单与期望消息类型 |

## 规范与配套

- [yomk注释原则](yomk注释原则.md)：头文件英文 Doxygen + `@zh:` 双语注释、内部 STL 极简注释
- [yomk文档原则](yomk文档原则.md)：本文档体系的分片结构、导航与示例规范
- [skill/yomkserver-modular](../skill/yomkserver-modular/SKILL.md)：模块化编程规范（扩展开发）
- API 精确契约：头文件 Doxygen 注释（`include/yomk/`），逐参数与返回码以之为准
