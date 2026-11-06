#pragma once
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "YomkMsgs.h"
#include "YomkRequest.h"
#include "YomkUrl.h"

// ctx 域：上下文键值的链式访问面——key/group 累积定位目标，写路径终结经请求域 .status()
// 三态收敛，读路径终结经请求域 .request() 返回 Response；type<T>() 进入类型化读分面，
// get() 承载全表面唯一的类型化读逻辑（kName 比对 + cast + 默认值回退）。
// 无状态设计：全部经 YomkServerRef 快照取当前 server，未初始化处理内聚在守卫与请求域一层；
// 依赖单向（Request/Url/Msgs/Payload），不依赖 YomkApi.h 与其他域头，可独立包含。
// 约束：builder 持 key/group 视图，仅限单表达式内联使用，不得缓存复用（缓存将悬垂）
namespace yomk
{
// 类型化读分面：type<T>() 之后 withDefault 累积（可省略）→ get() 终结
template <typename T>
class ContextValue
{
    static_assert(std::is_base_of_v<YomkMsg, T>, "context type must derive YomkMsg");
    std::string_view m_key;
    std::string_view m_group;
    std::shared_ptr<T> m_default = nullptr;  // 省略 withDefault 时未命中回退空指针（超集语义）

public:
    ContextValue(std::string_view key, std::string_view group)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
        : m_key(key), m_group(group)
    {
    }
    // 累积默认值：get() 未命中时回退（原 getContext 必填参数放宽为可选）
    ContextValue& withDefault(std::shared_ptr<T> v)
    {
        m_default = std::move(v);
        return *this;
    }
    // 类型化读终结：kName 比对 + dynamic_pointer_cast + 默认值回退（原 getContext 逻辑逐字搬运）；
    // 返回只读快照，改动请构造新对象再 .set()，勿原地修改（否则与并发读竞争、可能崩溃）
    std::shared_ptr<T> get()
    {
        if (!detail::requireServer())
        {
            return m_default;
        }
        yomk::payload::Response response =
            yomk::server().url(yomk::url::ctxGet).request(yomk::msgs::Context::shared(yomk::payload::Context{std::string(m_key), m_default, std::string(m_group)}));
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
// 组级开关分面：checker()/monitor() 之后仅允许 enable()/disable()（组级操作，与键级
// 写操作 .checker(fn)/.monitor(fn) 分层；与日志域 console()/file() 分面 + 开关约定同构）
class GroupSwitch
{
    friend class ContextBuilder;
    std::string_view m_group;
    bool m_monitor;  // false = 校验门控，true = 变更监听器
    GroupSwitch(std::string_view group, bool monitor)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
        : m_group(group), m_monitor(monitor)
    {
    }

public:
    // 开启（组级；URL 与 String 载荷与旧 enableContextChecker/Monitor 一致）
    yomk::payload::ResStatus enable()
    {
        return yomk::server().url(m_monitor ? yomk::url::ctxTurnOnMonitor : yomk::url::ctxTurnOnChecker).status(yomk::msgs::String::shared(std::string(m_group)));
    }
    // 关闭（组级）
    yomk::payload::ResStatus disable()
    {
        return yomk::server().url(m_monitor ? yomk::url::ctxTurnOffMonitor : yomk::url::ctxTurnOffChecker).status(yomk::msgs::String::shared(std::string(m_group)));
    }
};
// ctx 域 builder：key/group 累积 + 写路径终结（三态）+ 读路径终结（Response）+ type<T>() 类型化分面
class ContextBuilder
{
    std::string_view m_key;
    std::string_view m_group = "main";

public:
    // 累积目标键名（ctxName，view 透传不拷贝）
    ContextBuilder& key(std::string_view name)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        m_key = name;
        return *this;
    }
    // 累积组名（原"组名约定永远是 API 最后一个参数"改为显式累积，缺省 main）
    ContextBuilder& group(std::string_view name = "main")  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        m_group = name;
        return *this;
    }
    // —— 写路径终结（三态 ResStatus；三态收敛与 diag 由请求域 .status() 一处承担）——
    // 创建上下文键值
    yomk::payload::ResStatus create(YomkMsgPtr ctx)
    {
        return yomk::server().url(yomk::url::ctxCreate).status(yomk::msgs::Context::shared(yomk::payload::Context{std::string(m_key), ctx, std::string(m_group)}));
    }
    // 设置上下文值（整体替换）
    yomk::payload::ResStatus set(YomkMsgPtr ctx)
    {
        return yomk::server().url(yomk::url::ctxSet).status(yomk::msgs::Context::shared(yomk::payload::Context{std::string(m_key), ctx, std::string(m_group)}));
    }
    // 销毁指定上下文键
    yomk::payload::ResStatus destroy()
    {
        return yomk::server().url(yomk::url::ctxDestroy).status(yomk::msgs::ContextGroupKey::shared(yomk::payload::ContextGroupKey{std::string(m_group), std::string(m_key)}));
    }
    // 设置上下文校验门控回调
    yomk::payload::ResStatus checker(YomkContextCheckerFunc checkFunc)
    {
        return yomk::server().url(yomk::url::ctxSetChecker).status(yomk::msgs::ContextChecker::shared(yomk::payload::ContextChecker{std::string(m_key), checkFunc, std::string(m_group)}));
    }
    // 设置上下文变更监听回调（async=true 为异步通知），仅通知发生了一次 set 事件，并回传该次 set
    // 的键值快照——不保证快照实时性；顺序上异步恒按 set 提交序送达、同步在并发 set 下不保证跨线程序
    yomk::payload::ResStatus monitor(YomkContextMonitorFunc monitorFunc, bool async = false)
    {
        return yomk::server().url(yomk::url::ctxSetMonitor).status(yomk::msgs::ContextMonitor::shared(yomk::payload::ContextMonitor{std::string(m_key), monitorFunc, async, std::string(m_group)}));
    }
    // 组级开关分面（checker()/monitor() 与键级回调终结 .checker(fn)/.monitor(fn) 按参数区分）
    GroupSwitch checker() const
    {
        return GroupSwitch(m_group, false);
    }
    GroupSwitch monitor() const
    {
        return GroupSwitch(m_group, true);
    }
    // —— 读路径终结（Response；未初始化守卫在此内聚，请求域不再重复记 diag）——
    // 获取全部上下文键名
    yomk::payload::Response keys()
    {
        if (!detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return yomk::server().url(yomk::url::ctxKeys).request(yomk::msgs::String::shared(std::string(m_group)));
    }
    // 查询指定上下文键状态
    yomk::payload::Response info()
    {
        if (!detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return yomk::server().url(yomk::url::ctxKey).request(yomk::msgs::ContextGroupKey::shared(yomk::payload::ContextGroupKey{std::string(m_group), std::string(m_key)}));
    }
    // 查询全部组名清单（返回 StringArray，对齐 listEventLoops 形态）
    yomk::payload::Response groups()
    {
        if (!detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return yomk::server().url(yomk::url::ctxGroups).request(nullptr);
    }
    // 查询全部组及其全部键的内省元信息（跨组聚合：返回 ContextGroupInfoList，层级为组 → 键元信息）
    yomk::payload::Response all()
    {
        if (!detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return yomk::server().url(yomk::url::ctxAll).request(nullptr);
    }
    // 类型化读分面：T 必须为 YomkMsg 派生（ContextValue 内 static_assert 把关）
    template <typename T>
    ContextValue<T> type() const
    {
        return ContextValue<T>(m_key, m_group);
    }
};
// ctx 域入口：上下文键值链式访问（读链即取值：key → type → withDefault → get）
inline ContextBuilder ctx()
{
    return ContextBuilder{};
}
}
