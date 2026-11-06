#pragma once
#include <string>
#include <string_view>
#include <utility>

#include "YomkMsgs.h"
#include "YomkPayload.h"
#include "YomkServerApi.h"
#include "YomkUrl.h"

// 函数池域：单入口 yomk::fn_pool()。函数名由 .fn_name(name) 承载，服务函数 / 消息名 /
// 说明 / 调用数据一律前置累积（约定 5），终结 op 全部无参——install / uninstall 经请求域
// .status() 收敛三态，call / list / get_info / all 返回 Response。原 6 个 FUNCTIONPOOL_API
// 垫片随本域落地退役（register/unregister 更名 install/uninstall：register 是 C++ 保留字）。
/**
 * @brief 函数池链式 Builder：注册 / 调用 / 内省
 *
 * @par 注册
 * yomk::fn_pool().fn_name(name).fn(fn).msg_name(msgName).description(desc).install()
 * @li fn_name(name) 承载函数名；msg_name / description 省略 = 缺省（""）
 * @li install() 无参终结，返回三态 ResStatus；未初始化返回 Invalid
 * 
 * @par 注销
 * yomk::fn_pool().fn_name(name).uninstall()
 * @li 注销函数
 *
 * @par 调用
 * yomk::fn_pool().fn_name(name).msg(data).call()
 * @li msg(data) 承载调用数据；call() 返回 Response（函数执行结果）
 *
 * @par 内省
 * @li yomk::fn_pool().list()：全部函数名清单（StringArray）
 * @li yomk::fn_pool().fn_name(name).get_info()：指定函数元信息
 * @li yomk::fn_pool().all()：全部函数元信息
 *
 * @warning builder 全程 auto 接收、仅限单表达式内联使用，禁止缓存复用！
 */
// 公开面：仅域入口（约定 8：入口暴露、机制内敛）
namespace yomk::detail
{
class FnPoolBuilder;  // 前置声明，定义见下方机制段
}
namespace yomk
{
// 函数池域入口：注册 / 调用 / 内省
inline detail::FnPoolBuilder fn_pool();  // 按值返回需完整类型，定义在机制段之后
}

// —— 机制段：builder（用户无需感知）——
namespace yomk::detail
{
class FnPoolBuilder
{
    std::string m_name;      // fn_name() 累积，全终结共用
    YomkServiceFunc m_fn;    // fn() 累积，install 用
    std::string m_msgName;   // msg_name() 累积，install 用（期望消息类型，缺省 ""）
    std::string m_desc;      // description() 累积，install 用（缺省 ""）
    YomkMsgPtr m_data;       // msg() 累积，call 用
public:
    // 函数名（注册 / 注销 / 调用 / 单查共用）
    FnPoolBuilder& fn_name(std::string_view name)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        m_name = name;
        return *this;
    }
    // 累积服务函数（install 用）
    FnPoolBuilder& fn(YomkServiceFunc func)
    {
        m_fn = std::move(func);
        return *this;
    }
    // 累积期望消息类型（install 用，可省 = ""）
    FnPoolBuilder& msg_name(std::string_view msgName)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        m_msgName = msgName;
        return *this;
    }
    // 累积函数说明（install 用，可省 = ""）
    FnPoolBuilder& description(std::string_view desc)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        m_desc = desc;
        return *this;
    }
    // 累积调用数据（call 用）
    FnPoolBuilder& msg(YomkMsgPtr data)
    {
        m_data = std::move(data);
        return *this;
    }
    // 注册函数到全局函数池（无参动作，读 fn_name/fn/msg_name/description 累积）
    yomk::payload::ResStatus install()
    {
        return yomk::server().url(yomk::url::fpRegister).msg(yomk::msgs::Function::shared(yomk::payload::Function{m_name, m_fn, m_msgName, m_desc})).status();
    }
    // 从函数池注销函数
    yomk::payload::ResStatus uninstall()
    {
        return yomk::server().url(yomk::url::fpUnregister).msg(yomk::msgs::String::shared(m_name)).status();
    }
    // 调用函数池中注册的函数（无参动作，读 fn_name/msg 累积）
    yomk::payload::Response call()
    {
        if (!detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return yomk::server().url(yomk::url::fpCall).msg(yomk::msgs::CallFunction::shared(yomk::payload::CallFunction{m_name, m_data})).request();
    }
    // 全部函数名清单（StringArray）
    yomk::payload::Response list()
    {
        if (!detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return yomk::server().url(yomk::url::fpNames).request();
    }
    // 指定函数元信息
    yomk::payload::Response get_info()
    {
        if (!detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return yomk::server().url(yomk::url::fpName).msg(yomk::msgs::String::shared(m_name)).request();
    }
    // 全部函数元信息
    yomk::payload::Response all()
    {
        if (!detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
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
