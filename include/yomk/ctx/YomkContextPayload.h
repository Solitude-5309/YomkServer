#pragma once
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "yomk/server/YomkServerPayload.h"

namespace yomk::payload
{

/**
 * @brief Context checker gating verdict: the checker callback return value, decides whether set passes
 * @note The checker callback bound via weakFunc falls back to Accept (pass by default) after the service is deregistered
 */
// @zh: Context 校验门控结论：checker 回调返回值，决定 set 是否放行
// @zh: note weakFunc 绑定的 checker 回调在服务注销后兜底返回 Accept（默认放行）
enum class CheckStatus
{
    Accept,
    Reject
};

/** @brief Context key-value write payload (shared by create/set) */
// @zh: 上下文键值写载荷（create/set 共用）
struct Context
{
    std::string m_key;
    YomkMsgPtr m_value;
    std::string m_group = "main";  // key 所属组名(缺省 main)
};

/** @brief Key-level checker registration payload */
// @zh: 键级 checker 注册载荷
struct ContextChecker
{
    std::string m_key;
    std::function<CheckStatus(const yomk::payload::Context& ctx)> m_checkFunc;
    std::string m_group = "main";  // 目标 key 所属组名(缺省 main)
};

/** @brief Key-level monitor registration payload */
// @zh: 键级 monitor 注册载荷
struct ContextMonitor
{
    std::string m_key;
    std::function<void(Context ctx)> m_contextMonitorFunc;
    bool m_asyncMonitor = false;
    std::string m_group = "main";  // 目标 key 所属组名(缺省 main)
};

/** @brief In-group key locator payload (group + key) */
// @zh: 组内键定位载荷（group + key）
struct ContextGroupKey
{
    std::string m_group = "main";  // 组名(缺省 main)
    std::string m_key;             // 键名
};

/** @brief Single-key introspection snapshot */
// @zh: 单键内省快照
struct ContextKeyInfo
{
    std::string m_key;                    // 键名
    std::string m_typeName;               // 值消息包类型名(值为空时为空串)
    bool m_checkerOn = false;             // 门控是否开启
    std::size_t m_monitorCount = 0;       // monitor 总数
    std::size_t m_asyncMonitorCount = 0;  // 其中 async monitor 数
    std::string m_group = "main";         // key 所属组名
};

/** @brief Single-group introspection snapshot (group name + metadata of all keys) */
// @zh: 单组内省快照（组名 + 全键元信息）
struct ContextGroupInfo
{
    std::string m_groupName;             // 组名
    std::vector<ContextKeyInfo> m_keys;  // 该组全部键的内省元信息(map 字典序)
};

}  // namespace yomk::payload

/**
 * @brief Context change monitor callback: only notifies that a set occurred and passes back that snapshot
 * @param ctx The key-value snapshot of that set
 * @note Snapshot timeliness is not guaranteed; the asynchronous form preserves submission order, the synchronous form does not guarantee ordering under concurrency. See ctx().monitor_fn(fn).apply() for details
 */
// @zh: Context 变更监听回调：仅通知发生了 set 并回传该次快照
// @zh: ctx 该次 set 的键值快照
// @zh: note 不保证快照实时性；异步形态按提交序保序，同步形态并发下不保证序。详见 ctx().monitor_fn(fn).apply()
using YomkContextMonitorFunc = std::function<void(const yomk::payload::Context& ctx)>;
/**
 * @brief Context checker gating callback: gates whether set passes
 * @param ctx The key-value to be written
 * @return Accept to pass / Reject to deny
 * @note See ctx().checker_fn(fn).apply() for details
 */
// @zh: Context 校验门控回调：门控 set 是否放行
// @zh: ctx 待写入的键值
// @zh: return Accept 放行 / Reject 拒绝
// @zh: note 详见 ctx().checker_fn(fn).apply()
using YomkContextCheckerFunc = std::function<yomk::payload::CheckStatus(const yomk::payload::Context& ctx)>;
