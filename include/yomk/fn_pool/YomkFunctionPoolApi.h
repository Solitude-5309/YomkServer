#pragma once
#include <string>
#include <string_view>
#include <utility>

#include "yomk/fn_pool/YomkFunctionPoolMsgs.h"
#include "yomk/fn_pool/YomkFunctionPoolPayload.h"
#include "yomk/fn_pool/YomkFunctionPoolUrl.h"
#include "yomk/YomkStdMsgs.h"
#include "yomk/server/YomkServerApi.h"
#include "yomk/srv_info/YomkServerInfoMsgs.h"  // FuncInfo/FuncInfoList：fn_pool().get_info()/all() 返回数据
#include "yomk/server/YomkServerPayload.h"

// 函数池域：单入口 yomk::fn_pool()。函数名由 .fn_name(name) 承载，服务函数 / 消息名 /
// 说明 / 调用数据一律前置累积，终结 op 全部无参——install / uninstall 经请求域 .status()
// 收敛三态，call / list / get_info / all 返回 Response。
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
// 公开面仅暴露域入口, builder 机制内敛, 调用方全程以 auto 接收、从不具名引用类型
namespace yomk::detail
{
class FnPoolBuilder;  // 前置声明，定义见下方机制段
}
namespace yomk
{
/**
 * @brief 函数池域入口：注册 / 调用 / 内省链式调用的起点
 * @return FnPoolBuilder（按值, 仅用于单表达式链式调用）
 */
inline detail::FnPoolBuilder fn_pool();  // 按值返回需完整类型，定义在机制段之后
}

// —— 机制段: builder ——
namespace yomk::detail
{
/**
 * @brief 函数池域 builder：fn_name 定位 + 参数前置累积 + 无参终结
 */
class FnPoolBuilder
{
    std::string m_name;      // fn_name() 累积, 全终结共用
    YomkServiceFunc m_fn;    // fn() 累积, install 用
    std::string m_msgName;   // msg_name() 累积, install 用（期望消息类型, 缺省 ""）
    std::string m_desc;      // description() 累积, install 用（缺省 ""）
    YomkMsgPtr m_data;       // msg() 累积, call 用
public:
    /**
     * @brief 定位目标函数名（注册 / 注销 / 调用 / 单查共用）
     * @param name 函数名
     * @return 自身链式引用
     */
    FnPoolBuilder& fn_name(std::string_view name)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        m_name = name;
        return *this;
    }
    /**
     * @brief 累积服务函数（install 用）
     * @param func 服务函数
     * @return 自身链式引用
     */
    FnPoolBuilder& fn(YomkServiceFunc func)
    {
        m_fn = std::move(func);
        return *this;
    }
    /**
     * @brief 累积期望消息类型（install 用）
     * @param msgName 消息类型名, 可省 = ""
     * @return 自身链式引用
     */
    FnPoolBuilder& msg_name(std::string_view msgName)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        m_msgName = msgName;
        return *this;
    }
    /**
     * @brief 累积函数说明（install 用，内省展示）
     * @param desc 说明文本, 可省 = ""
     * @return 自身链式引用
     */
    FnPoolBuilder& description(std::string_view desc)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        m_desc = desc;
        return *this;
    }
    /**
     * @brief 累积调用数据（call 用）
     * @param data 打包后的消息对象
     * @return 自身链式引用
     */
    FnPoolBuilder& msg(YomkMsgPtr data)
    {
        m_data = std::move(data);
        return *this;
    }
    /**
     * @brief 注册函数到全局函数池终结（读 fn_name/fn/msg_name/description 累积）
     * @return 三态 ResStatus; 未初始化返回 Invalid
     * @note 终结定名 install（register 为 C++ 保留字）; 同名函数覆盖更新并返回 Succeeded
     */
    yomk::payload::ResStatus install()
    {
        return yomk::server().url(yomk::url::fpRegister).msg(yomk::msgs::Function::shared(yomk::payload::Function{m_name, m_fn, m_msgName, m_desc})).status();
    }
    /**
     * @brief 从函数池注销函数终结
     * @return 三态 ResStatus; 函数不存在返回 Failed
     */
    yomk::payload::ResStatus uninstall()
    {
        return yomk::server().url(yomk::url::fpUnregister).msg(yomk::msgs::String::shared(m_name)).status();
    }
    /**
     * @brief 调用函数池中注册的函数终结（读 fn_name/msg 累积）
     * @return Response 携带函数执行结果; 未初始化返回 Invalid
     * @note 阻塞至函数执行完成; 函数不存在返回 Failed
     */
    yomk::payload::Response call()
    {
        if (!detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return yomk::server().url(yomk::url::fpCall).msg(yomk::msgs::CallFunction::shared(yomk::payload::CallFunction{m_name, m_data})).request();
    }
    /**
     * @brief 查询全部函数名清单
     * @return Response 携带 StringArray; 未初始化返回 Invalid
     */
    yomk::payload::Response list()
    {
        if (!detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return yomk::server().url(yomk::url::fpNames).request();
    }
    /**
     * @brief 查询指定函数元信息
     * @return Response; 未初始化返回 Invalid
     */
    yomk::payload::Response get_info()
    {
        if (!detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return yomk::server().url(yomk::url::fpName).msg(yomk::msgs::String::shared(m_name)).request();
    }
    /**
     * @brief 查询全部函数元信息
     * @return Response; 未初始化返回 Invalid
     */
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
