#pragma once
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

/**
 * @brief Base class of message types: accessor of the runtime type name (compared against
 *        kTypeName)
 * @note Custom messages are derived via the YOMK_DEFINE_MSG macro, not by inheriting this class
 *       and naming it manually
 * @note Abstract: each generated class exposes its static kTypeName through typeName(), so the
 *       type name is read from static storage and no per-instance copy is stored
 * @note clone() is the only way to obtain a modifiable copy: framework storage and callbacks
 *       only ever hold shared_ptr<const YomkMsg>
 */
// @zh: 消息类型基类：运行时类型名访问口（kTypeName 比对对象）
// @zh: note 自定义消息经 YOMK_DEFINE_MSG 宏派生，不直接继承本类手工命名
// @zh: note 抽象基类：类型名由各生成类的静态 kTypeName 经纯虚 typeName() 取得，实例内不存副本
// @zh: note clone() 是取得可改副本的唯一途径：框架存储与回调只持有 shared_ptr<const YomkMsg>
class YomkMsg
{
public:
    YomkMsg() {}
    virtual ~YomkMsg() {}

public:
    virtual std::string_view typeName() const = 0;
    /** @brief Deep copy of this message, for write-in (context storage, snapshot publishing) */
    // @zh: 本消息的深拷贝，供写时拷入（context 存储、快照发布）
    virtual std::shared_ptr<const YomkMsg> clone() const = 0;
};
using YomkMsgPtr = std::shared_ptr<YomkMsg>;              // 生产侧：构造消息并交出所有权
using YomkConstMsgPtr = std::shared_ptr<const YomkMsg>;   // 消费侧：传输/存储/回调只读视图

/**
 * @brief Define a message type: generate the YomkMsg derived class MsgName inside yomk::msgs
 * @param DataType Payload data type
 * @param MsgName Message class name (kTypeName is "yomk::msgs::MsgName", the fully
 *        qualified type identifier used for unpack and dynamic lookup comparison)
 * @param VarName Payload member name (framework convention fixes it to d, e.g. msg->d)
 * @note The generated class provides the shared(...) factory; custom messages are declared only
 *       by this macro, see YomkStdMsg.h for the standard type message catalog, YomkServerMsg.h
 *       for the core payload message catalog, and each Yomk{Module}Msg.h for module-specific
 *       message catalogs
 * @note The payload's namespace is independent of the message class namespace; MsgName shares
 *       the single yomk::msgs root — use YOMK_DEFINE_MSG_NS when isolation is needed
 */
// @zh: 定义消息类型：在 yomk::msgs 内生成 YomkMsg 派生类 MsgName
// @zh: DataType 载荷数据类型
// @zh: MsgName 消息类名（kTypeName 为 "yomk::msgs::MsgName"，全限定类型标识，供解包/动态查找比对）
// @zh: VarName 载荷成员名（框架约定统一为 d，如 msg->d）
// @zh: note 生成类提供 shared(...) 工厂；自定义消息仅此宏声明，标准类型消息目录见 YomkStdMsg.h，核心载荷消息目录见 YomkServerMsg.h，模块专属消息目录见各 Yomk{Module}Msg.h
// @zh: note 载荷类型命名空间与消息类命名空间相互独立；MsgName 全局共用 yomk::msgs 单一根，需隔离时改用 YOMK_DEFINE_MSG_NS
#define YOMK_DEFINE_MSG(DataType, MsgName, VarName)                               \
    YOMK_DEFINE_MSG_IMPL(yomk::msgs, "yomk::msgs::", DataType, MsgName, VarName)

/**
 * @brief Define a namespaced message type: generate the YomkMsg derived class MsgName
 *        inside yomk::msgs::NS, with a fully qualified kTypeName
 * @param NS Sub-namespace under yomk::msgs (C++17 nested-namespace tokens, e.g. my::mod;
 *        must contain no whitespace, kTypeName embeds the stringized form verbatim)
 * @param DataType Payload data type; write it root-qualified (leading ::) when it shares a
 *        name with NS or an ancestor namespace, since the macro expands inside yomk::msgs::NS
 * @param MsgName Message class name
 * @param VarName Payload member name (framework convention fixes it to d, e.g. msg->d)
 * @note kTypeName is "yomk::msgs::NS::MsgName", matching the C++ qualified name, so namespace
 *       isolation and runtime type-ID uniqueness are guaranteed by the same mechanism.
 *       Use this form for custom messages that must not sit at the yomk::msgs root
 */
// @zh: 定义带命名空间的消息类型：在 yomk::msgs::NS 内生成 YomkMsg 派生类 MsgName，kTypeName 全限定
// @zh: NS yomk::msgs 下的子命名空间（C++17 嵌套命名空间 token，如 my::mod；不得含空格，kTypeName 按字面串化嵌入）
// @zh: DataType 载荷数据类型；与 NS 或其祖先命名空间同名时写根限定名（:: 开头），宏体在 yomk::msgs::NS 内展开
// @zh: MsgName 消息类名
// @zh: VarName 载荷成员名（框架约定统一为 d，如 msg->d）
// @zh: note kTypeName 为 "yomk::msgs::NS::MsgName"，与 C++ 全限定名一致，命名空间隔离与运行期类型 ID 唯一由同一机制保证；不宜落在 yomk::msgs 根的自定义消息用本形式
#define YOMK_DEFINE_MSG_NS(NS, DataType, MsgName, VarName)                        \
    YOMK_DEFINE_MSG_IMPL(yomk::msgs::NS, "yomk::msgs::" #NS "::", DataType, MsgName, VarName)

/**
 * @brief Shared implementation of YOMK_DEFINE_MSG / YOMK_DEFINE_MSG_NS
 * @param NSOpen Namespace tokens to open, e.g. yomk::msgs or yomk::msgs::my::mod (C++17)
 * @param NamePrefix String-literal prefix for kTypeName; "yomk::msgs::" for the root-level form
 * @note Internal use only — declare messages via YOMK_DEFINE_MSG / YOMK_DEFINE_MSG_NS
 */
// @zh: YOMK_DEFINE_MSG / YOMK_DEFINE_MSG_NS 共用实现
// @zh: NSOpen 要打开的命名空间 token，如 yomk::msgs 或 yomk::msgs::my::mod（C++17 嵌套命名空间）
// @zh: NamePrefix kTypeName 的字符串字面量前缀；根级形式传 "yomk::msgs::"
// @zh: note 仅供内部使用——消息一律经 YOMK_DEFINE_MSG / YOMK_DEFINE_MSG_NS 声明
#define YOMK_DEFINE_MSG_IMPL(NSOpen, NamePrefix, DataType, MsgName, VarName)      \
    namespace NSOpen                                                              \
    {                                                                             \
    class MsgName : public ::YomkMsg                                              \
    {                                                                             \
    public:                                                                       \
        static constexpr const char* kTypeName = NamePrefix #MsgName;             \
        std::string_view typeName() const override { return kTypeName; }          \
        static_assert(std::is_copy_constructible<DataType>::value,                \
            "message payload must be copyable for clone()");                      \
        MsgName() {}                                                              \
        MsgName(const DataType& value) : VarName(value) {}                        \
        MsgName(DataType&& value) : VarName(std::move(value)) {}                  \
        virtual ~MsgName() {}                                                     \
        std::shared_ptr<const YomkMsg> clone() const override                     \
        {                                                                         \
            return std::make_shared<MsgName>(*this);                              \
        }                                                                         \
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
    using MsgName##ConstPtr = std::shared_ptr<const MsgName>;                     \
    }

namespace yomk::msgs
{

/**
 * @brief Result of unpack<Msg>: the decoded message plus the failure reason
 * @note Read-only view: the decoded message is always const; modify it by clone() + resubmit
 * @note A data carrier only: no combinators, no error mapping, not a general Result type
 */
// @zh: unpack<Msg> 的结果：解包出的消息 + 失败原因
// @zh: note 只读视图：解包出的消息恒为 const；需要修改请 clone() 出新对象再提交
// @zh: note 仅作数据载体，不提供组合子/错误映射等通用 Result 能力
template <class Msg>
class UnpackResult
{
public:
    /**
     * @brief Construct from the decoded pointer and the failure reason (one of them is empty)
     * @param msg Decoded message pointer, nullptr on failure
     * @param error Failure reason, empty on success
     */
    // @zh: 由解包指针与失败原因构造（二者其一为空）
    // @zh: msg 解包出的消息只读指针，失败时为 nullptr
    // @zh: error 失败原因，成功时为空
    UnpackResult(std::shared_ptr<const Msg> msg, std::string error)
        : m_msg(std::move(msg)), m_error(std::move(error))
    {
    }

    /**
     * @brief Whether the unpack succeeded; usable directly in if / ! conditions
     * @return true when the decoded pointer is non-empty
     */
    // @zh: 解包是否成功，可直接用于 if / ! 条件
    // @zh: return 解包指针非空时为 true
    explicit operator bool() const noexcept { return m_msg != nullptr; }

    /**
     * @brief Access the decoded payload through the message pointer, e.g. result->d.key
     * @return The decoded read-only message pointer; never used when the result is false
     */
    // @zh: 经消息指针访问载荷，如 result->d.key
    // @zh: return 解包出的只读消息指针；结果判假时不得使用
    const Msg* operator->() const noexcept { return m_msg.get(); }

    /**
     * @brief The decoded message reference
     * @return Reference to the decoded message (access its payload via d)
     */
    // @zh: 解包出的消息只读引用
    // @zh: return 解包出的消息对象（经 d 访问载荷字段）
    const Msg& operator*() const noexcept { return *m_msg; }

    /**
     * @brief The decoded shared pointer, for forwarding to APIs taking shared_ptr<const Msg>
     * @return shared_ptr<const Msg>; empty on failure
     */
    // @zh: 解包出的只读共享指针，供需要 shared_ptr<const Msg> 的接口转发
    // @zh: return shared_ptr<const Msg>，失败时为空
    std::shared_ptr<const Msg> msg() const noexcept { return m_msg; }

    /**
     * @brief The failure reason, meaningful only when the result is false
     * @return The standard mismatch message, non-empty on failure
     */
    // @zh: 失败原因，仅在结果判假时有意义
    // @zh: return 标准不符文案，失败时必非空
    const std::string& error() const noexcept { return m_error; }

private:
    std::shared_ptr<const Msg> m_msg;
    std::string m_error;
};

/**
 * @brief Unpack a message by its runtime type name
 * @tparam Msg Target message class (generated by YOMK_DEFINE_MSG)
 * @param msg Pointer to the message to unpack
 * @return The result object: non-empty on hit; on miss carries "msg is not <expected>, got '<actual>'"
 *         (an empty message reports "null" as the actual type)
 * @note Usable directly as a bool check, throws no exceptions; the mismatch text is generated here
 *       so the expected type name is written only once at the call site
 */
// @zh: 按运行时类型名解包消息
// @zh: Msg 目标消息类（YOMK_DEFINE_MSG 生成）
// @zh: msg 待解包消息只读指针（YomkMsgPtr / shared_ptr<T> 隐式转换）
// @zh: return 结果对象：命中时非空；未命中携带 "msg is not <期望类型>, got '<实际类型>'"（空消息报实际类型为 "null"）
// @zh: note 判空即用，不抛异常；不符文案在此生成，调用点的期望类型名只写一次
template <class Msg>
UnpackResult<Msg> unpack(const YomkConstMsgPtr& msg)
{
    if (msg && msg->typeName() == Msg::kTypeName)
    {
        return {std::dynamic_pointer_cast<const Msg>(msg), std::string()};
    }
    // Null-safe actual type name: an empty message reports "null"
    // @zh: 判空取实际类型名，空消息报 "null"
    const std::string actual = msg ? std::string(msg->typeName()) : "null";
    return {nullptr, std::string("msg is not ") + Msg::kTypeName + ", got '" + actual + "'"};
}

}  // namespace yomk::msgs
