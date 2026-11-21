#pragma once
#include <string_view>

// /YomkEventLoop/* 内置服务 URL 常量:inline constexpr std::string_view 编译期常量,
// 请求链全程 string_view 透传,调用路径零堆分配.
// yomk::url 为开放命名空间:用户自定义服务的 url 常量可按同风格直接追加.
namespace yomk::url
{
// /YomkEventLoop/*
inline constexpr std::string_view loopStart = "/YomkEventLoop/start";
inline constexpr std::string_view loopStop = "/YomkEventLoop/stop";
inline constexpr std::string_view loopPost = "/YomkEventLoop/post";
inline constexpr std::string_view loopPostWait = "/YomkEventLoop/post_wait";
inline constexpr std::string_view loopDestroy = "/YomkEventLoop/destroy";
inline constexpr std::string_view loopLoops = "/YomkEventLoop/loops";
inline constexpr std::string_view loopLoop = "/YomkEventLoop/loop";
inline constexpr std::string_view loopAll = "/YomkEventLoop/all";

// 语义描述常量：与上方 URL 常量按序一一对应（installFunc 第四参引用；格式惯例见 docs/YomkServer.md「内省描述惯例」）
inline constexpr const char* loopStartDesc = R"(
# Overview
创建事件循环并启动专用消费线程；同名循环已存在则复用既有实例并再次触发启动。

# Inputs
EventLoop 包：eventLoopName 循环名；defaultServiceFunc 默认处理函数（事件未携带 serviceFunc 时使用）；msgName 期望消息类型名（仅内省元数据，可空）。

# Outputs
data 无负载，msg "event loop start success"。

# Behavior
同步执行：新名创建实例、绑定默认处理函数、启动 worker 线程并登记；同名复用既有实例并 start；已在运行则幂等早退。

# Notes
循环名不做非空校验（空名会创建空名循环）；同名复用时本次携带的 defaultServiceFunc/msgName 不生效；pkg 非 EventLoop → Failed。
)";
inline constexpr const char* loopStopDesc = R"(
# Overview
停止事件循环的消费线程，实例与其事件队列保留。

# Inputs
String 包，d 为循环名。

# Outputs
data 无负载，msg "event loop stop success"。

# Behavior
同步执行并等待 worker join；未运行时直接返回成功（幂等）；队列不清空——未执行事件保留，再次 start 续跑。

# Notes
循环不存在 → Failed；停机期间 /post 与 /post_wait 均被拒（Failed）；pkg 非 String → Failed。
)";
inline constexpr const char* loopPostDesc = R"(
# Overview
向事件循环投递事件：FIFO 入队后立即返回，由消费线程异步执行。

# Inputs
Event 包：eventLoopName 目标循环；pkg 事件负载；serviceFunc 处理函数（空则用循环默认处理函数）；tag 标签（内省采样用）。

# Outputs
data 无负载，msg "event loop post success"。

# Behavior
异步非阻塞：入队即返回；事件由 worker 线程按 FIFO 顺序执行，每次投递各执行一次（非幂等）。

# Notes
循环不存在或未运行 → Failed（未入队）；执行结果不回传，需要结果请用 /post_wait；pkg 非 Event → Failed。
)";
inline constexpr const char* loopPostWaitDesc = R"(
# Overview
投递事件并阻塞调用线程，直到该事件在消费线程执行完毕。

# Inputs
Event 包：eventLoopName 目标循环；pkg 事件负载；serviceFunc 处理函数（空则用循环默认处理函数）；tag 标签。

# Outputs
data 为执行后的 Event 包（携带 Response），msg "event loop post wait success"。

# Behavior
同步阻塞：入队后等待执行完成；在目标 worker 线程内调用会被识别为自锁，改为就地执行该事件并 ERROR 留痕；非幂等。

# Notes
循环不存在或未运行 → Failed（未入队未执行）；循环被 /destroy 丢弃事件时等待者会被唤醒；pkg 非 Event → Failed。
)";
inline constexpr const char* loopDestroyDesc = R"(
# Overview
销毁事件循环：停止 worker、清空未执行队列并释放实例（不可重启）。

# Inputs
String 包，d 为循环名。

# Outputs
data 无负载，msg "event loop destroy success"。

# Behavior
同步执行：先 stop 并 join worker，再清空队列；被丢弃事件的等待回调逐个触发，以释放 /post_wait 等待者。

# Notes
循环不存在 → Failed；销毁后同名需重新 /start 创建新实例；pkg 非 String → Failed。
)";
inline constexpr const char* loopLoopsDesc = R"(
# Overview
返回全部事件循环名清单。

# Inputs
无（不带请求包）。

# Outputs
data 为 StringArray 包（map 字典序），msg "ok"。

# Behavior
同步快照读取，持循环表读锁拷贝；幂等。

# Notes
未登记过的循环不出现在清单中（stop 后仍保留）。
)";
inline constexpr const char* loopLoopDesc = R"(
# Overview
查询单个事件循环的内省快照：运行态、积压数、默认处理函数元信息与队列头 tag 采样。

# Inputs
EventLoopQuery 包：loopName 循环名（按字面量精确匹配，不做切分）；tagCount 队列头采样条数（缺省 3）。

# Outputs
data 为 EventLoopInfo 包（loopName/running/pending/hasDefaultFunc/msgName/nextTags），msg "ok"。

# Behavior
同步快照读取，持队列锁拷贝后逐项填充；幂等。

# Notes
tagCount 为 0 时 nextTags 为空；采样存 tag 原值（空串保留）；循环不存在 → Failed；pkg 非 EventLoopQuery → Failed。
)";
inline constexpr const char* loopAllDesc = R"(
# Overview
全量事件循环内省快照。

# Inputs
无（不带请求包）。

# Outputs
data 为 EventLoopInfoList 包，msg "ok"。

# Behavior
同步执行：锁内拷贝循环指针，锁外逐循环取快照（每个固定采样队列头 3 条 tag）；幂等。

# Notes
采样条数固定 3，需自定义请用 /loop 的 tagCount；跨循环快照非原子一致；无循环时返回空表。
)";
}  // namespace yomk::url
