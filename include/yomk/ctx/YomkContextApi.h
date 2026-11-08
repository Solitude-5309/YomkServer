#pragma once
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "yomk/ctx/YomkContextMsgs.h"
#include "yomk/ctx/YomkContextPayload.h"
#include "yomk/ctx/YomkContextUrl.h"
#include "yomk/YomkStdMsgs.h"
#include "yomk/server/YomkServerApi.h"

// ctx 域：上下文键值的链式访问面——key/group 累积定位目标，写路径终结经请求域 .status()
// 三态收敛，读路径终结经请求域 .request() 返回 Response；msg<T>() 进入类型化读分面，
// get() 承载全表面唯一的类型化读逻辑（kName 比对 + cast + 默认值回退）。
// 无状态设计：全部经 YomkServerRef 快照取当前 server，未初始化处理内聚在守卫与请求域一层；
// 依赖单向（Request/ContextUrl/Msgs/Payload），不依赖其他域头，可独立包含。
/**
 * @brief 上下文键值链式 Builder，读写指定组内的上下文
 *
 * @par 类型化读
 * yomk::ctx().key(n).group(g).msg<T>().with_default(d).get()
 * @li kName 比对 + dynamic_pointer_cast，返回 shared_ptr<T>
 * @li T 为 YOMK_DEFINE_MSG 消息类，static_assert 把关
 * @li .with_default(d) 可省略：未命中回退 nullptr
 *
 * @par 写路径
 * @li yomk::ctx().key(n).group(g).value(ctx).create()：创建上下文键值
 * @li yomk::ctx().key(n).group(g).value(ctx).set()：整体替换上下文值
 * @li yomk::ctx().key(n).group(g).destroy()：销毁指定上下文键
 * @li yomk::ctx().key(n).group(g).checker_fn(fn).apply()：设置键级校验门控回调
 * @li yomk::ctx().key(n).group(g).monitor_fn(fn).async(on).apply()：设置键级变更监听回调，
 *     on = true 异步通知；.async(on) 可省略，缺省 on = false（同步）
 * @li .value(ctx) 设置上下文值，YOMK_DEFINE_MSG 消息对象，create/set 共用
 * @li 以上均返回三态 ResStatus
 *
 * @par 组级开关（分面，均返回三态 ResStatus）
 * @li yomk::ctx().group(g).checker().enable() / .checker().disable()：校验门控开关
 * @li yomk::ctx().group(g).monitor().enable() / .monitor().disable()：变更监听开关
 *
 * @par 内省查询
 * @li yomk::ctx().group(g).keys()：组内全部上下文键名，返回 Response
 * @li yomk::ctx().key(n).group(g).info()：指定上下文键状态，返回 Response
 * @li yomk::ctx().groups()：全部组名清单，返回 Response（StringArray）
 * @li yomk::ctx().all()：全部组及其全部键的内省元信息，返回 Response（ContextGroupInfoList）
 *
 * @par 其他说明
 * @li key/group 为 view 累积，组名 g 省略即缺省 "main"
 * @li 读路径未初始化返回 Invalid 并记录诊断信息；写路径三态收敛由请求域 .status() 承担
 *
 * @warning Builder 持 key/group 视图，仅限单表达式内联使用，禁止缓存复用，否则悬垂引用！
 */
// 公开面仅暴露域入口, builder/分面类机制内敛, 调用方全程以 auto 接收、从不具名引用类型
namespace yomk::detail
{
class ContextBuilder;  // 前置声明，定义见下方机制段
}
namespace yomk
{
/**
 * @brief ctx 域入口：上下文键值链式访问的起点
 * @return ContextBuilder（按值, 仅用于单表达式链式调用）
 * @note 读链即取值: key → msg<T> → with_default → get
 */
inline detail::ContextBuilder ctx();  // 按值返回需完整类型，定义在机制段之后
}

// —— 机制段: builder 与分面 ——
namespace yomk::detail
{
/**
 * @brief 类型化读分面：msg<T>() 之后 with_default 累积（可省略）→ get() 终结
 * @tparam T YOMK_DEFINE_MSG 生成的消息类（kName 比对对象）
 */
template <typename T>
class ContextValue
{
    static_assert(std::is_base_of_v<YomkMsg, T>, "context type must derive YomkMsg");
    std::string_view m_key;
    std::string_view m_group;
    std::shared_ptr<T> m_default = nullptr;  // 省略 with_default 时未命中回退空指针（超集语义）

public:
    ContextValue(std::string_view key, std::string_view group)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
        : m_key(key), m_group(group)
    {
    }
    /**
     * @brief 累积默认值（get() 未命中时回退）
     * @param v 默认值指针, 可省略（回退 nullptr）
     * @return 自身链式引用
     */
    ContextValue& with_default(std::shared_ptr<T> v)
    {
        m_default = std::move(v);
        return *this;
    }
    /**
     * @brief 类型化读终结：kName 比对 + dynamic_pointer_cast + 默认值回退
     * @return shared_ptr<T> 值快照; 未命中/未初始化返回累积的默认值
     * @warning 返回只读快照, 改动请构造新对象再 .set(), 勿原地修改（否则与并发读竞争、可能崩溃）
     */
    std::shared_ptr<T> get()
    {
        if (!detail::requireServer())
        {
            return m_default;
        }
        yomk::payload::Response response =
            yomk::server().url(yomk::url::ctxGet).msg(yomk::msgs::Context::shared(yomk::payload::Context{std::string(m_key), m_default, std::string(m_group)})).request();
        if (response.m_status == yomk::payload::ResStatus::Succeeded)
        {
            std::shared_ptr<T> ctxData = nullptr;
            if (response.m_data && response.m_data->name() == T::kName)
            {
                ctxData = std::dynamic_pointer_cast<T>(response.m_data);
            }
            if (ctxData)
                return ctxData;
            else
                return m_default;
        }
        else
        {
            YOMK_DIAG_ERROR("get context failed");
            return m_default;
        }
    }
};

/**
 * @brief 组级开关分面：checker()/monitor() 之后仅允许 enable()/disable()
 * 组级操作, 键级回调累积命名为 checker_fn(fn)/monitor_fn(fn), 按名字分层
 */
class GroupSwitch
{
    friend class ContextBuilder;
    std::string_view m_group;
    bool m_monitor;  // false = 校验门控, true = 变更监听器
    GroupSwitch(std::string_view group, bool monitor)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
        : m_group(group), m_monitor(monitor)
    {
    }

public:
    /**
     * @brief 开启组级开关（校验门控或变更监听, 由分面决定）
     * @return 三态 ResStatus
     */
    yomk::payload::ResStatus enable()
    {
        return yomk::server().url(m_monitor ? yomk::url::ctxTurnOnMonitor : yomk::url::ctxTurnOnChecker).msg(yomk::msgs::String::shared(std::string(m_group))).status();
    }
    /**
     * @brief 关闭组级开关（校验门控或变更监听, 由分面决定）
     * @return 三态 ResStatus
     */
    yomk::payload::ResStatus disable()
    {
        return yomk::server().url(m_monitor ? yomk::url::ctxTurnOffMonitor : yomk::url::ctxTurnOffChecker).msg(yomk::msgs::String::shared(std::string(m_group))).status();
    }
};

/**
 * @brief ctx 域 builder：key/group 累积 + 写路径终结（三态）+ 读路径终结（Response）
 * + msg<T>() 类型化分面
 */
class ContextBuilder
{
    // 键级回调挂载点: checker/monitor 累积后经 apply() 终结派发（终结 op 为无参动作）
    enum class CallbackKind
    {
        None,
        Checker,
        Monitor
    };
    std::string_view m_key;
    std::string_view m_group = "main";
    YomkMsgPtr m_value;
    CallbackKind m_cbKind = CallbackKind::None;
    YomkContextCheckerFunc m_checker;
    YomkContextMonitorFunc m_monitor;
    bool m_asyncMonitor = false;  // monitor 异步通知开关, 缺省 false（同步）

public:
    /**
     * @brief 累积目标键名
     * @param name 键名（view 透传不拷贝）
     * @return 自身链式引用
     */
    ContextBuilder& key(std::string_view name)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        m_key = name;
        return *this;
    }
    /**
     * @brief 累积组名
     * @param name 组名, 省略 = 缺省 "main"
     * @return 自身链式引用
     */
    ContextBuilder& group(std::string_view name = "main")  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        m_group = name;
        return *this;
    }
    /**
     * @brief 累积上下文值（create/set 共用, 建议紧邻终结）
     * @param ctx YOMK_DEFINE_MSG 消息对象
     * @return 自身链式引用
     */
    ContextBuilder& value(YomkMsgPtr ctx)
    {
        m_value = std::move(ctx);
        return *this;
    }
    // —— 写路径终结（三态 ResStatus 无参动作; 三态收敛与 diag 由请求域 .status() 一处承担）——
    /**
     * @brief 创建上下文键值终结
     * @return 三态 ResStatus; key 空 / value 空 / key 已存在均失败
     */
    yomk::payload::ResStatus create()
    {
        return yomk::server().url(yomk::url::ctxCreate).msg(yomk::msgs::Context::shared(yomk::payload::Context{std::string(m_key), m_value, std::string(m_group)})).status();
    }
    /**
     * @brief 整体替换上下文值终结
     * @return 三态 ResStatus; key 不存在 / 类型不匹配 / checker 拒绝均失败
     */
    yomk::payload::ResStatus set()
    {
        return yomk::server().url(yomk::url::ctxSet).msg(yomk::msgs::Context::shared(yomk::payload::Context{std::string(m_key), m_value, std::string(m_group)})).status();
    }
    /**
     * @brief 销毁指定上下文键终结
     * @return 三态 ResStatus; key 不存在失败
     */
    yomk::payload::ResStatus destroy()
    {
        return yomk::server().url(yomk::url::ctxDestroy).msg(yomk::msgs::ContextGroupKey::shared(yomk::payload::ContextGroupKey{std::string(m_group), std::string(m_key)})).status();
    }
    // —— 键级回调: checker_fn/monitor_fn 累积回调, async 累积通知形态, apply() 无参终结派发 ——
    // 命名 _fn 后缀与组级开关分面 .checker()/.monitor() 按名字分层（不靠参数区分重载）
    /**
     * @brief 累积键级校验门控回调
     * @param checkFunc 校验回调, 拒绝则该键后续 set 失败
     * @return 自身链式引用
     */
    ContextBuilder& checker_fn(YomkContextCheckerFunc checkFunc)
    {
        m_checker = std::move(checkFunc);
        m_cbKind = CallbackKind::Checker;
        return *this;
    }
    /**
     * @brief 累积键级变更监听回调
     * @param monitorFunc 监听回调
     * @return 自身链式引用
     * @note 仅通知发生了一次 set 事件, 并回传该次 set 的键值快照——不保证快照实时性;
     *     异步形态恒按 set 提交序送达, 同步形态在并发 set 下不保证跨线程序
     */
    ContextBuilder& monitor_fn(YomkContextMonitorFunc monitorFunc)
    {
        m_monitor = std::move(monitorFunc);
        m_cbKind = CallbackKind::Monitor;
        return *this;
    }
    /**
     * @brief 累积监听通知形态（monitor_fn 之后使用）
     * @param on true = 异步（单线程池按 set 提交序保序）, 省略 = false 同步
     * @return 自身链式引用
     */
    ContextBuilder& async(bool on = true)
    {
        m_asyncMonitor = on;
        return *this;
    }
    /**
     * @brief 键级回调终结：按累积种类派发设置请求
     * @return 三态 ResStatus; 未累积任何回调返回 Invalid 并记诊断
     */
    yomk::payload::ResStatus apply()
    {
        switch (m_cbKind)
        {
        case CallbackKind::Checker:
            return yomk::server().url(yomk::url::ctxSetChecker).msg(yomk::msgs::ContextChecker::shared(yomk::payload::ContextChecker{std::string(m_key), m_checker, std::string(m_group)})).status();
        case CallbackKind::Monitor:
            return yomk::server().url(yomk::url::ctxSetMonitor).msg(yomk::msgs::ContextMonitor::shared(yomk::payload::ContextMonitor{std::string(m_key), m_monitor, m_asyncMonitor, std::string(m_group)})).status();
        case CallbackKind::None:
        default:
            YOMK_DIAG_ERROR("apply: no checker/monitor accumulated");
            return yomk::payload::ResStatus::Invalid;
        }
    }
    /**
     * @brief 切入组级校验门控开关分面
     * @return GroupSwitch（enable()/disable() 终结）
     */
    GroupSwitch checker() const
    {
        return GroupSwitch(m_group, false);
    }
    /**
     * @brief 切入组级变更监听开关分面
     * @return GroupSwitch（enable()/disable() 终结）
     */
    GroupSwitch monitor() const
    {
        return GroupSwitch(m_group, true);
    }
    // —— 读路径终结（Response; 未初始化守卫在此内聚, 请求域不再重复记 diag）——
    /**
     * @brief 查询组内全部上下文键名
     * @return Response; 未初始化返回 Invalid
     */
    yomk::payload::Response keys()
    {
        if (!detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return yomk::server().url(yomk::url::ctxKeys).msg(yomk::msgs::String::shared(std::string(m_group))).request();
    }
    /**
     * @brief 查询指定上下文键状态
     * @return Response; 未初始化返回 Invalid
     */
    yomk::payload::Response info()
    {
        if (!detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return yomk::server().url(yomk::url::ctxKey).msg(yomk::msgs::ContextGroupKey::shared(yomk::payload::ContextGroupKey{std::string(m_group), std::string(m_key)})).request();
    }
    /**
     * @brief 查询全部组名清单
     * @return Response 携带 StringArray; 未初始化返回 Invalid
     */
    yomk::payload::Response groups()
    {
        if (!detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return yomk::server().url(yomk::url::ctxGroups).request();
    }
    /**
     * @brief 查询全部组及其全部键的内省元信息
     * @return Response 携带 ContextGroupInfoList（层级为组 → 键元信息）; 未初始化返回 Invalid
     */
    yomk::payload::Response all()
    {
        if (!detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return yomk::server().url(yomk::url::ctxAll).request();
    }
    /**
     * @brief 切入类型化读分面
     * @tparam T 必须为 YomkMsg 派生（ContextValue 内 static_assert 把关）
     * @return ContextValue<T>（with_default 累积可省略, get() 终结）
     */
    template <typename T>
    ContextValue<T> msg() const
    {
        return ContextValue<T>(m_key, m_group);
    }
};
}  // namespace yomk::detail

namespace yomk
{
inline detail::ContextBuilder ctx()
{
    return detail::ContextBuilder{};
}
}  // namespace yomk
