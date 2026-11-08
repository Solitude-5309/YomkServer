#pragma once
#include <string>
#include <string_view>

#include "yomk/msg/YomkStdMsgs.h"
#include "yomk/server/YomkServerApi.h"
#include "yomk/srv_info/YomkServerInfoMsgs.h"
#include "yomk/srv_info/YomkServerInfoPayload.h"
#include "yomk/srv_info/YomkServerInfoUrl.h"

// ServerInfo 域（yomk::srv_info()）：服务拓扑内省的独立链式入口——services() 全部服务名、
// srv_name(name).functions() 指定服务函数清单、fn_url(url).get_info() 单函数元信息、
// all() 全量拓扑。内省自 server 域拆出：server() 域收敛请求/生命周期/服务管理，
// 拓扑读路径统一走本域，URL 屏蔽与依赖单向规则与各模块 Api 头一致。
/**
 * @brief 服务拓扑内省链式 Builder：services / functions / get_info / all 读终结
 *
 * @par 内省读终结（均返回 Response; 未初始化返回 Invalid）
 * @li yomk::srv_info().services()：全部已注册服务名清单（StringArray，字典序）
 * @li yomk::srv_info().srv_name(name).functions()：指定服务全部功能函数
 *     （FuncInfoList 结构化清单）；服务不存在时 m_msg 恒 "ok" 且清单为空
 * @li yomk::srv_info().fn_url("/服务名/函数名").get_info()：单函数元信息
 *     （FuncInfo 包，m_msg 恒 "ok"）
 * @li yomk::srv_info().all()：全量服务及功能函数拓扑（ServerServiceInfoList，
 *     层级为服务 → 函数元信息）
 *
 * @warning Builder 持名称视图, 仅限单表达式内联使用, 禁止缓存复用, 否则悬垂引用!
 */
// 公开面仅暴露域入口 yomk::srv_info(), builder 机制内敛, 调用方全程以 auto 接收、从不具名引用类型
namespace yomk::detail
{
/**
 * @brief ServerInfo 域 builder：srv_name/fn_url 累积 + 内省读终结
 */
class SrvInfoBuilder
{
    std::string m_srvName;  // srv_name() 累积, functions 用
    std::string m_fnUrl;    // fn_url() 累积, get_info 用

public:
    /**
     * @brief 累积目标服务名（functions 用）
     * @param srvName 服务注册名
     * @return 自身链式引用
     * @warning 仅持有视图不拷贝, 调用方须保证 srvName 在同表达式终结前有效
     */
    SrvInfoBuilder& srv_name(std::string_view srvName)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        m_srvName = srvName;
        return *this;
    }
    /**
     * @brief 累积功能函数 url（get_info 用）
     * @param url 完整功能函数 url（/服务名/函数名）
     * @return 自身链式引用
     * @warning 仅持有视图不拷贝, 调用方须保证 url 在同表达式终结前有效
     */
    SrvInfoBuilder& fn_url(std::string_view url)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        m_fnUrl = url;
        return *this;
    }
    /**
     * @brief 查询所有已注册服务名称（内省读终结）
     * @return Response 携带 StringArray; 未初始化返回 Invalid
     */
    yomk::payload::Response services()
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return yomk::server().url(yomk::url::srvServices).request();
    }
    /**
     * @brief 查询 srv_name() 指定服务的全部功能函数（内省读终结）
     * @return Response 携带 FuncInfoList 结构化清单（m_funcName/m_msgName/m_description）;
     *     未初始化返回 Invalid; 服务不存在时 m_msg 恒 "ok" 且清单为空
     */
    yomk::payload::Response functions()
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return yomk::server().url(yomk::url::srvFunctions).msg(yomk::msgs::String::shared(m_srvName)).request();
    }
    /**
     * @brief 查询 fn_url() 指定功能函数的元信息（内省读终结）
     * @return Response 携带 FuncInfo 包; 未初始化返回 Invalid
     */
    yomk::payload::Response get_info()
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return yomk::server().url(yomk::url::srvFunction).msg(yomk::msgs::String::shared(m_fnUrl)).request();
    }
    /**
     * @brief 查询框架全量服务及功能函数拓扑（内省读终结）
     * @return Response 携带 ServerServiceInfoList（层级为服务 → 函数元信息）; 未初始化返回 Invalid
     */
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
 * @brief ServerInfo 域入口：服务拓扑内省链式调用的起点
 * @return SrvInfoBuilder（按值, 仅用于单表达式链式调用）
 * @note 线程安全, 非阻塞
 */
inline detail::SrvInfoBuilder srv_info()
{
    return detail::SrvInfoBuilder{};
}
}  // namespace yomk
