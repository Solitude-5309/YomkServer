#pragma once
#include <string>
#include <string_view>

#include "yomk/msg/YomkStdMsg.h"
#include "yomk/server/YomkServerApi.h"
#include "yomk/srv_info/YomkServerInfoMsg.h"
#include "yomk/srv_info/YomkServerInfoPayload.h"
#include "yomk/srv_info/YomkServerInfoUrl.h"

// ServerInfo 域设计:内省自 server 域拆出——server() 域收敛请求/生命周期/服务管理,拓扑读路径统一走本域;
// 依赖单向,可独立包含.
/**
 * @brief Chained builder for service topology introspection: services / functions / get_info / all read terminators
 *
 * @par Introspection read terminators (all return Response; return Invalid if uninitialized)
 * @li yomk::srv_info().services(): list of all registered service names (StringArray, lexicographic order)
 * @li yomk::srv_info().srv_name(name).functions(): all functions of the given service
 *     (FuncInfoList structured list); when the service does not exist, m_msg is always "ok" and the list is empty
 * @li yomk::srv_info().fn_url("/service name/function name").get_info(): metadata of a single function
 *     (FuncInfo packet, m_msg is always "ok")
 * @li yomk::srv_info().all(): full topology of services and their functions (ServerServiceInfoList,
 *     hierarchy is service -> function metadata)
 *
 * @note Accumulated names/urls are copied into internal std::string storage; arguments carry no lifetime constraint beyond the call
 */
// @zh: 服务拓扑内省链式 Builder：services / functions / get_info / all 读终结
// @zh: par 内省读终结（均返回 Response；未初始化返回 Invalid）
// @zh: li yomk::srv_info().services()：全部已注册服务名清单（StringArray，字典序）
// @zh: li yomk::srv_info().srv_name(name).functions()：指定服务全部功能函数（FuncInfoList 结构化清单）；服务不存在时 m_msg 恒 "ok" 且清单为空
// @zh: li yomk::srv_info().fn_url("/服务名/函数名").get_info()：单函数元信息（FuncInfo 包，m_msg 恒 "ok"）
// @zh: li yomk::srv_info().all()：全量服务及功能函数拓扑（ServerServiceInfoList，层级为服务 → 函数元信息）
// @zh: note 累积的名称/url 拷贝进内部 std::string 存储，参数无本次调用之外的生命周期约束
// 公开面仅暴露域入口 yomk::srv_info(), builder 机制内敛, 调用方全程以 auto 接收,从不具名引用类型
namespace yomk::detail
{
/**
 * @brief ServerInfo domain builder: srv_name/fn_url accumulation + introspection read terminators
 */
// @zh: ServerInfo 域 builder：srv_name/fn_url 累积 + 内省读终结
class SrvInfoBuilder
{
    std::string m_srvName;  // srv_name() 累积, functions 用
    std::string m_fnUrl;    // fn_url() 累积, get_info 用

public:
    /**
     * @brief Accumulate the target service name (used by functions)
     * @param srvName Service registration name
     * @return Reference to self for chaining
     * @note srvName is copied into internal std::string storage, no lifetime constraint beyond this call
     */
    // @zh: 累积目标服务名（functions 用）
    // @zh: srvName 服务注册名
    // @zh: return 自身链式引用
    // @zh: note srvName 拷贝进内部 std::string 存储，无本次调用之外的生命周期约束
    SrvInfoBuilder& srv_name(std::string_view srvName)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        m_srvName = srvName;
        return *this;
    }
    /**
     * @brief Accumulate the function url (used by get_info)
     * @param url Full function url (/service name/function name)
     * @return Reference to self for chaining
     * @note url is copied into internal std::string storage, no lifetime constraint beyond this call
     */
    // @zh: 累积功能函数 url（get_info 用）
    // @zh: url 完整功能函数 url（/服务名/函数名）
    // @zh: return 自身链式引用
    // @zh: note url 拷贝进内部 std::string 存储，无本次调用之外的生命周期约束
    SrvInfoBuilder& fn_url(std::string_view url)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        m_fnUrl = url;
        return *this;
    }
    /**
     * @brief Query all registered service names (introspection read terminator)
     * @return Response carrying a StringArray; returns Invalid if uninitialized
     */
    // @zh: 查询所有已注册服务名称（内省读终结）
    // @zh: return Response 携带 StringArray；未初始化返回 Invalid
    yomk::payload::Response services()
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return yomk::server().url(yomk::url::srvServices).request();
    }
    /**
     * @brief Query all functions of the service given by srv_name() (introspection read terminator)
     * @return Response carrying a FuncInfoList structured list (m_funcName/m_msgName/m_description);
     *     returns Invalid if uninitialized; when the service does not exist, m_msg is always "ok" and the list is empty
     */
    // @zh: 查询 srv_name() 指定服务的全部功能函数（内省读终结）
    // @zh: return Response 携带 FuncInfoList 结构化清单（m_funcName/m_msgName/m_description）；未初始化返回 Invalid；服务不存在时 m_msg 恒 "ok" 且清单为空
    yomk::payload::Response functions()
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return yomk::server().url(yomk::url::srvFunctions).msg(yomk::msgs::String::shared(m_srvName)).request();
    }
    /**
     * @brief Query the metadata of the function given by fn_url() (introspection read terminator)
     * @return Response carrying a FuncInfo packet; returns Invalid if uninitialized
     */
    // @zh: 查询 fn_url() 指定功能函数的元信息（内省读终结）
    // @zh: return Response 携带 FuncInfo 包；未初始化返回 Invalid
    yomk::payload::Response get_info()
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return yomk::server().url(yomk::url::srvFunction).msg(yomk::msgs::String::shared(m_fnUrl)).request();
    }
    /**
     * @brief Query the full topology of framework services and their functions (introspection read terminator)
     * @return Response carrying a ServerServiceInfoList (hierarchy is service -> function metadata); returns Invalid if uninitialized
     */
    // @zh: 查询框架全量服务及功能函数拓扑（内省读终结）
    // @zh: return Response 携带 ServerServiceInfoList（层级为服务 → 函数元信息）；未初始化返回 Invalid
    yomk::payload::Response all()
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return yomk::server().url(yomk::url::srvAll).request();
    }
};
}  // namespace yomk::detail

namespace yomk
{
/**
 * @brief ServerInfo domain entry: starting point for service topology introspection chained calls
 * @return SrvInfoBuilder (by value, for single-expression chained calls only)
 * @note Thread-safe, non-blocking
 */
// @zh: ServerInfo 域入口：服务拓扑内省链式调用的起点
// @zh: return SrvInfoBuilder（按值，仅用于单表达式链式调用）
// @zh: note 线程安全，非阻塞
inline detail::SrvInfoBuilder srv_info()
{
    return detail::SrvInfoBuilder{};
}
}  // namespace yomk
