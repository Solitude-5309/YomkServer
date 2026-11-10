#pragma once
#include <string>
#include <string_view>
#include <utility>

#include "yomk/fn_pool/YomkFunctionPoolMsg.h"
#include "yomk/fn_pool/YomkFunctionPoolPayload.h"
#include "yomk/fn_pool/YomkFunctionPoolUrl.h"
#include "yomk/msg/YomkStdMsg.h"
#include "yomk/server/YomkServerApi.h"
#include "yomk/server/YomkServerPayload.h"
#include "yomk/srv_info/YomkServerInfoMsg.h"  // FuncInfo/FuncInfoList:fn_pool().get_info()/all() 返回数据

/**
 * @brief Function pool chained builder: registration, call and introspection
 *
 * @par Registration
 * yomk::fn_pool().fn_name(name).fn(fn).msg_name(msgName).description(desc).install()
 * @li fn_name(name) carries the function name; omitting msg_name / description means default ("")
 * @li install() terminates the chain with no argument and returns a tri-state ResStatus; returns Invalid if uninitialized
 *
 * @par Unregistration
 * yomk::fn_pool().fn_name(name).uninstall()
 * @li Unregisters the function
 *
 * @par Call
 * yomk::fn_pool().fn_name(name).msg(data).call()
 * @li msg(data) carries the call data; call() returns a Response (the function execution result)
 *
 * @par Introspection
 * @li yomk::fn_pool().list(): list of all function names (StringArray)
 * @li yomk::fn_pool().fn_name(name).get_info(): metadata of the given function
 * @li yomk::fn_pool().all(): metadata of all functions
 *
 * @warning Receive the builder with auto throughout, use it only inline within a single expression, never cache and reuse it!
 */
// @zh: 函数池链式 Builder：注册 / 调用 / 内省
// @zh: par 注册
// @zh: li fn_name(name) 承载函数名；msg_name / description 省略 = 缺省（""）
// @zh: li install() 无参终结，返回三态 ResStatus；未初始化返回 Invalid
// @zh: par 注销
// @zh: li 注销函数
// @zh: par 调用
// @zh: li msg(data) 承载调用数据；call() 返回 Response（函数执行结果）
// @zh: par 内省
// @zh: li yomk::fn_pool().list()：全部函数名清单（StringArray）
// @zh: li yomk::fn_pool().fn_name(name).get_info()：指定函数元信息
// @zh: li yomk::fn_pool().all()：全部函数元信息
// @zh: warning builder 全程 auto 接收，仅限单表达式内联使用，禁止缓存复用！
// 公开面仅暴露域入口, builder 机制内敛, 调用方全程以 auto 接收,从不具名引用类型
namespace yomk::detail
{
class FnPoolBuilder;  // 前置声明,定义见下方机制段
}
namespace yomk
{
/**
 * @brief Function pool domain entry: starting point for registration, call and introspection chains
 * @return FnPoolBuilder (by value, for single-expression chained calls only)
 * @note Thread-safe, non-blocking
 */
// @zh: 函数池域入口：注册 / 调用 / 内省链式调用的起点
// @zh: return FnPoolBuilder（按值，仅用于单表达式链式调用）
// @zh: note 线程安全，非阻塞
inline detail::FnPoolBuilder fn_pool();  // 按值返回需完整类型,定义在机制段之后
}

// —— 机制段: builder ——
namespace yomk::detail
{
/**
 * @brief Function pool domain builder: fn_name targeting, upfront parameter accumulation and no-argument chain terminators
 */
// @zh: 函数池域 builder：fn_name 定位 + 参数前置累积 + 无参终结
class FnPoolBuilder
{
    std::string m_name;      // fn_name() 累积, 全终结共用
    YomkServiceFunc m_fn;    // fn() 累积, install 用
    std::string m_msgName;   // msg_name() 累积, install 用(期望消息类型, 缺省 "")
    std::string m_desc;      // description() 累积, install 用(缺省 "")
    YomkMsgPtr m_data;       // msg() 累积, call 用
public:
    /**
     * @brief Target the function name (shared by registration, unregistration, call and single query)
     * @param name Function name
     * @return Reference to self for chaining
     */
    // @zh: 定位目标函数名（注册 / 注销 / 调用 / 单查共用）
    // @zh: name 函数名
    // @zh: return 自身链式引用
    FnPoolBuilder& fn_name(std::string_view name)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        m_name = name;
        return *this;
    }
    /**
     * @brief Accumulate the service function (used by install)
     * @param func Service function
     * @return Reference to self for chaining
     */
    // @zh: 累积服务函数（install 用）
    // @zh: func 服务函数
    // @zh: return 自身链式引用
    FnPoolBuilder& fn(YomkServiceFunc func)
    {
        m_fn = std::move(func);
        return *this;
    }
    /**
     * @brief Accumulate the expected message type (used by install)
     * @param msgName Message type name, optional = ""
     * @return Reference to self for chaining
     */
    // @zh: 累积期望消息类型（install 用）
    // @zh: msgName 消息类型名，可省 = ""
    // @zh: return 自身链式引用
    FnPoolBuilder& msg_name(std::string_view msgName)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        m_msgName = msgName;
        return *this;
    }
    /**
     * @brief Accumulate the function description (used by install, shown by introspection)
     * @param desc Description text, optional = ""
     * @return Reference to self for chaining
     */
    // @zh: 累积函数说明（install 用，内省展示）
    // @zh: desc 说明文本，可省 = ""
    // @zh: return 自身链式引用
    FnPoolBuilder& description(std::string_view desc)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        m_desc = desc;
        return *this;
    }
    /**
     * @brief Accumulate the call data (used by call)
     * @param data Packed message object
     * @return Reference to self for chaining
     */
    // @zh: 累积调用数据（call 用）
    // @zh: data 打包后的消息对象
    // @zh: return 自身链式引用
    FnPoolBuilder& msg(YomkMsgPtr data)
    {
        m_data = std::move(data);
        return *this;
    }
    /**
     * @brief Terminate the chain by registering the function into the global function pool (reads fn_name / fn / msg_name / description accumulations)
     * @return Tri-state ResStatus; returns Invalid if uninitialized
     * @note The terminator is named install (register is a C++ reserved word); a function with the same name is overwritten and updated, returning Succeeded
     */
    // @zh: 注册函数到全局函数池终结（读 fn_name / fn / msg_name / description 累积）
    // @zh: return 三态 ResStatus；未初始化返回 Invalid
    // @zh: note 终结定名 install（register 为 C++ 保留字）；同名函数覆盖更新并返回 Succeeded
    yomk::payload::ResStatus install()
    {
        return yomk::server().url(yomk::url::fpRegister).msg(yomk::msgs::Function::shared(yomk::payload::Function{m_name, m_fn, m_msgName, m_desc})).status();
    }
    /**
     * @brief Terminate the chain by unregistering the function from the function pool
     * @return Tri-state ResStatus; returns Failed if the function does not exist
     */
    // @zh: 从函数池注销函数终结
    // @zh: return 三态 ResStatus；函数不存在返回 Failed
    yomk::payload::ResStatus uninstall()
    {
        return yomk::server().url(yomk::url::fpUnregister).msg(yomk::msgs::String::shared(m_name)).status();
    }
    /**
     * @brief Terminate the chain by calling a function registered in the function pool (reads fn_name / msg accumulations)
     * @return Response carrying the function execution result; returns Invalid if uninitialized
     * @note Blocks until the function execution completes; returns Failed if the function does not exist
     */
    // @zh: 调用函数池中注册的函数终结（读 fn_name / msg 累积）
    // @zh: return Response 携带函数执行结果；未初始化返回 Invalid
    // @zh: note 阻塞至函数执行完成；函数不存在返回 Failed
    yomk::payload::Response call()
    {
        if (!detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not initialized");
        }
        return yomk::server().url(yomk::url::fpCall).msg(yomk::msgs::CallFunction::shared(yomk::payload::CallFunction{m_name, m_data})).request();
    }
    /**
     * @brief Query the list of all function names
     * @return Response carrying a StringArray; returns Invalid if uninitialized
     */
    // @zh: 查询全部函数名清单
    // @zh: return Response 携带 StringArray；未初始化返回 Invalid
    yomk::payload::Response list()
    {
        if (!detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not initialized");
        }
        return yomk::server().url(yomk::url::fpNames).request();
    }
    /**
     * @brief Query the metadata of the given function
     * @return Response; returns Invalid if uninitialized
     */
    // @zh: 查询指定函数元信息
    // @zh: return Response；未初始化返回 Invalid
    yomk::payload::Response get_info()
    {
        if (!detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not initialized");
        }
        return yomk::server().url(yomk::url::fpName).msg(yomk::msgs::String::shared(m_name)).request();
    }
    /**
     * @brief Query the metadata of all functions
     * @return Response; returns Invalid if uninitialized
     */
    // @zh: 查询全部函数元信息
    // @zh: return Response；未初始化返回 Invalid
    yomk::payload::Response all()
    {
        if (!detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not initialized");
        }
        return yomk::server().url(yomk::url::fpAll).request();
    }
};
}  // namespace yomk::detail

namespace yomk
{
inline detail::FnPoolBuilder fn_pool()
{
    return detail::FnPoolBuilder{};
}
}  // namespace yomk
