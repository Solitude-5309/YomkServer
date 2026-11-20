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
Semantic: 创建事件循环并启动专用消费线程（同名循环已存在则复用并再次触发启动）
Returns: data 无负载，成功 msg "event loop start success"
Precond: 循环名非空
Idempotent: true —— 重复 start 复用既有实例，本次累积配置不生效
SideEffect: 创建消费线程、置运行态
Fail: Failed=pkg 非 EventLoop
)";
inline constexpr const char* loopStopDesc = "Semantic: 停止事件循环消费线程（实例保留，可重新 start）；Returns: data 无负载；Failed=循环不存在";
inline constexpr const char* loopPostDesc = R"(
Semantic: 向事件循环投递事件，FIFO 入队由消费线程异步执行
Returns: data 无负载，成功 msg "event loop post success"
Precond: 目标循环存在且运行中
Idempotent: false —— 每次投递都执行一次
SideEffect: 事件入队；消费时执行事件携带的处理函数
Fail: Failed=循环不存在/未运行
)";
inline constexpr const char* loopPostWaitDesc = R"(
Semantic: 投递事件并阻塞至其执行完成
Returns: data 为执行后的 Event 包（携带执行结果）
Precond: 目标循环存在且运行中
Idempotent: false —— 每次投递都执行一次
SideEffect: 同 post；另阻塞调用线程直至事件执行完毕
Fail: Failed=循环不存在/未运行（未入队未执行）
)";
inline constexpr const char* loopDestroyDesc = "Semantic: 销毁事件循环并释放资源（不可重启）；Returns: data 无负载；Failed=循环不存在";
inline constexpr const char* loopLoopsDesc = "Semantic: 返回全部事件循环名列表；Returns: StringArray 包";
inline constexpr const char* loopLoopDesc = "Semantic: 查询单循环内省快照（运行态/积压数/期望类型名/队列头 tag 采样）；Returns: EventLoopInfo 包；Failed=循环不存在";
inline constexpr const char* loopAllDesc = "Semantic: 全部事件循环内省快照（tag 采样 3 条）；Returns: EventLoopInfoList 包";
}  // namespace yomk::url