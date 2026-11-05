#pragma once
#include <memory>
#include <utility>

#include "YomkMsg.h"
#include "YomkPayload.h"

#define YOMK_DEFINE_MSG(DataType, MsgName, VarName)                               \
    namespace yomk                                                                \
    {                                                                             \
    namespace msgs                                                                \
    {                                                                             \
    class MsgName : public ::YomkMsg                                              \
    {                                                                             \
    public:                                                                       \
        static constexpr const char* kName = #MsgName;                            \
        MsgName() { m_name = kName; }                                             \
        MsgName(const DataType& value) : VarName(value) { m_name = kName; }       \
        MsgName(DataType&& value) : VarName(std::move(value)) { m_name = kName; } \
        virtual ~MsgName() {}                                                     \
                                                                                  \
    public:                                                                       \
        DataType VarName{};                                                       \
                                                                                  \
        template <typename... A>                                                  \
        static std::shared_ptr<MsgName> shared(A&&... a)                          \
        {                                                                         \
            return std::make_shared<MsgName>(std::forward<A>(a)...);              \
        }                                                                         \
    };                                                                            \
    using MsgName##Ptr = std::shared_ptr<MsgName>;                                \
    }                                                                             \
    }

namespace yomk
{
namespace msgs
{

template <class Msg>
std::shared_ptr<Msg> unPack(const YomkMsgPtr& pkg)
{
    if (pkg && pkg->name() == Msg::kName)
    {
        return std::dynamic_pointer_cast<Msg>(pkg);
    }
    return nullptr;
}

}  // namespace msgs
}  // namespace yomk

// SDK 消息包目录：框架载荷包 + 内置标准类型包（成员名均为 d）。
// 自定义消息不在此定义，经本文件的 YOMK_DEFINE_MSG 宏自行声明。

// ============ 框架载荷消息包 ============
// 服务响应载荷包（功能函数返回值的包形态，供响应作为消息存储/传递）
YOMK_DEFINE_MSG(yomk::payload::Response, Response, d);
YOMK_DEFINE_MSG(yomk::payload::Function, Function, d);
YOMK_DEFINE_MSG(yomk::payload::CallFunction, CallFunction, d);
YOMK_DEFINE_MSG(yomk::payload::FuncInfo, FuncInfo, d);
// FunctionPool 全量函数内省（getAllFunctionInfo 返回数据；函数总数由 d.size() 承载）
YOMK_DEFINE_MSG(std::vector<yomk::payload::FuncInfo>, FuncInfoList, d);
YOMK_DEFINE_MSG(yomk::payload::Event, Event, d);
YOMK_DEFINE_MSG(yomk::payload::EventLoop, EventLoop, d);
YOMK_DEFINE_MSG(yomk::payload::EventLoopInfo, EventLoopInfo, d);
// EventLoop 全量循环内省（getAllEventLoopInfo 返回数据；m_nextTags 取 3 条与单查缺省一致）
YOMK_DEFINE_MSG(std::vector<yomk::payload::EventLoopInfo>, EventLoopInfoList, d);
YOMK_DEFINE_MSG(yomk::payload::LogFile, LogFile, d);
YOMK_DEFINE_MSG(yomk::payload::Log, Log, d);
YOMK_DEFINE_MSG(yomk::payload::ConsoleLogProxy, ConsoleLogProxy, d);
YOMK_DEFINE_MSG(yomk::payload::FileLogProxy, FileLogProxy, d);
YOMK_DEFINE_MSG(yomk::payload::LoggerQuery, LoggerQuery, d);
YOMK_DEFINE_MSG(yomk::payload::LoggerInfo, LoggerInfo, d);
// Logger 控制台全局主闸态（getGlobalLoggerLevel 返回数据：六级别主闸开关 + proxy）
YOMK_DEFINE_MSG(yomk::payload::LoggerState, LoggerState, d);
// Logger 全量内省（getAllLoggerInfo 返回数据；元素为单查同款 LoggerInfo，每条内嵌 LoggerState 状态段）
YOMK_DEFINE_MSG(std::vector<yomk::payload::LoggerInfo>, LoggerInfoList, d);
YOMK_DEFINE_MSG(yomk::payload::Context, Context, d);
// Context 组寻址载体（/destroy 与 /key 请求报文：定位某组下的某键，组名缺省 main）
YOMK_DEFINE_MSG(yomk::payload::ContextGroupKey, ContextGroupKey, d);
YOMK_DEFINE_MSG(yomk::payload::ContextKeyInfo, ContextKeyInfo, d);
// Context 键元信息清单（ContextGroupInfo.m_keys 的元素类型）
YOMK_DEFINE_MSG(std::vector<yomk::payload::ContextKeyInfo>, ContextKeyInfoList, d);
// Context 组级内省条目（ContextGroupInfoList 元素；层级为组 → 键元信息）
YOMK_DEFINE_MSG(yomk::payload::ContextGroupInfo, ContextGroupInfo, d);
// Context 组级全量内省（getAllContextInfo 返回数据）
YOMK_DEFINE_MSG(std::vector<yomk::payload::ContextGroupInfo>, ContextGroupInfoList, d);
// ServerInfo 全量拓扑内省（getAllServerInfo 返回数据；层级为服务 → 函数元信息）
YOMK_DEFINE_MSG(std::vector<yomk::payload::ServerServiceInfo>, ServerServiceInfoList, d);
YOMK_DEFINE_MSG(yomk::payload::ContextChecker, ContextChecker, d);
YOMK_DEFINE_MSG(yomk::payload::ContextMonitor, ContextMonitor, d);
YOMK_DEFINE_MSG(yomk::payload::VoidPointer, VoidPointer, d);

// 内置标准类型消息包（成员名均为 d）
YOMK_DEFINE_MSG(bool, Bool, d);
YOMK_DEFINE_MSG(std::vector<bool>, BoolArray, d);
YOMK_DEFINE_MSG(signed char, Char, d);
YOMK_DEFINE_MSG(std::vector<char>, CharArray, d);
YOMK_DEFINE_MSG(unsigned char, UChar, d);
YOMK_DEFINE_MSG(std::vector<unsigned char>, UCharArray, d);
YOMK_DEFINE_MSG(unsigned char, Byte, d);
YOMK_DEFINE_MSG(std::vector<unsigned char>, ByteArray, d);
YOMK_DEFINE_MSG(std::int8_t, Int8, d);
YOMK_DEFINE_MSG(std::vector<signed char>, Int8Array, d);
YOMK_DEFINE_MSG(std::uint8_t, Uint8, d);
YOMK_DEFINE_MSG(std::vector<unsigned char>, Uint8Array, d);
YOMK_DEFINE_MSG(std::int16_t, Int16, d);
YOMK_DEFINE_MSG(std::vector<std::int16_t>, Int16Array, d);
YOMK_DEFINE_MSG(std::uint16_t, Uint16, d);
YOMK_DEFINE_MSG(std::vector<std::uint16_t>, Uint16Array, d);
YOMK_DEFINE_MSG(std::int32_t, Int32, d);
YOMK_DEFINE_MSG(std::vector<std::int32_t>, Int32Array, d);
YOMK_DEFINE_MSG(std::uint32_t, Uint32, d);
YOMK_DEFINE_MSG(std::vector<std::uint32_t>, Uint32Array, d);
YOMK_DEFINE_MSG(std::int64_t, Int64, d);
YOMK_DEFINE_MSG(std::vector<std::int64_t>, Int64Array, d);
YOMK_DEFINE_MSG(std::uint64_t, Uint64, d);
YOMK_DEFINE_MSG(std::vector<std::uint64_t>, Uint64Array, d);
YOMK_DEFINE_MSG(float, Float32, d);
YOMK_DEFINE_MSG(std::vector<float>, Float32Array, d);
YOMK_DEFINE_MSG(double, Float64, d);
YOMK_DEFINE_MSG(std::vector<double>, Float64Array, d);
YOMK_DEFINE_MSG(std::string, String, d);
YOMK_DEFINE_MSG(std::vector<std::string>, StringArray, d);
