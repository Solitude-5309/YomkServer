#pragma once
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "YomkPayload.h"

namespace yomk::payload
{

struct Context
{
    std::string m_key;
    YomkMsgPtr m_value;
    std::string m_group = "main";  // key 所属组名（缺省 main）
};

struct ContextChecker
{
    std::string m_key;
    std::function<CheckStatus(const yomk::payload::Context& ctx)> m_checkFunc;
    std::string m_group = "main";  // 目标 key 所属组名（缺省 main）
};

struct ContextMonitor
{
    std::string m_key;
    std::function<void(Context ctx)> m_contextMonitorFunc;
    bool m_asyncMonitor = false;
    std::string m_group = "main";  // 目标 key 所属组名（缺省 main）
};

struct ContextGroupKey
{
    std::string m_group = "main";  // 组名（缺省 main）
    std::string m_key;             // 键名
};

struct ContextKeyInfo
{
    std::string m_key;                    // 键名
    std::string m_typeName;               // 值消息包类型名（值为空时为空串）
    bool m_checkerOn = false;             // 门控是否开启
    std::size_t m_monitorCount = 0;       // monitor 总数
    std::size_t m_asyncMonitorCount = 0;  // 其中 async monitor 数
    std::string m_group = "main";         // key 所属组名
};

struct ContextGroupInfo
{
    std::string m_groupName;             // 组名
    std::vector<ContextKeyInfo> m_keys;  // 该组全部键的内省元信息（map 字典序）
};

}  // namespace yomk::payload

/**
 * @brief Context 变更监听回调：仅通知发生了 set 并回传该次快照
 * @param ctx 该次 set 的键值快照
 * @note 不保证快照实时性；异步形态按提交序保序、同步形态并发下不保证序。详见 ctx().monitor_fn(fn).apply()
 */
using YomkContextMonitorFunc = std::function<void(const yomk::payload::Context& ctx)>;
/**
 * @brief Context 校验门控回调：门控 set 是否放行
 * @param ctx 待写入的键值
 * @return Accept 放行 / Reject 拒绝
 * @note 详见 ctx().checker_fn(fn).apply()
 */
using YomkContextCheckerFunc = std::function<yomk::payload::CheckStatus(const yomk::payload::Context& ctx)>;
