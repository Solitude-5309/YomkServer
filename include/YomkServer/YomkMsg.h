#pragma once
#include <memory>
#include <string>
#include <utility>

/**
 * @brief 消息类型基类：运行时类型名载体（kName 比对对象）
 * @note 自定义消息经 YOMK_DEFINE_MSG 宏派生, 不直接继承本类手工命名
 */
class YomkMsg
{
public:
    YomkMsg() {}
    virtual ~YomkMsg() {}

public:
    void name(const std::string& name) { m_name = name; }
    std::string name() { return m_name; }

protected:
    std::string m_name;
};
using YomkMsgPtr = std::shared_ptr<YomkMsg>;

/**
 * @brief 定义消息类型：在 yomk::msgs 内生成 YomkMsg 派生类 MsgName
 * @param DataType 载荷数据类型
 * @param MsgName 消息类名（同时作为类型标识 kName, 供 unpack/动态查找比对）
 * @param VarName 载荷成员名（框架约定统一为 d, 如 pkg->d）
 * @note 生成类提供 shared(...) 工厂; 自定义消息仅此宏声明, 内置消息目录见 YomkMsgs.h
 */
#define YOMK_DEFINE_MSG(DataType, MsgName, VarName)                               \
    namespace yomk::msgs                                                          \
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
    }

namespace yomk::msgs
{

/**
 * @brief 按运行时类型名解包消息
 * @tparam Msg 目标消息类（YOMK_DEFINE_MSG 生成）
 * @param pkg 待解包消息指针
 * @return 命中返回 shared_ptr<Msg>; pkg 为空或类型不符返回 nullptr
 * @note 判空即用, 不抛异常
 */
template <class Msg>
std::shared_ptr<Msg> unpack(const YomkMsgPtr& pkg)
{
    if (pkg && pkg->name() == Msg::kName)
    {
        return std::dynamic_pointer_cast<Msg>(pkg);
    }
    return nullptr;
}

}  // namespace yomk::msgs
