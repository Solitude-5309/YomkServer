#include "ContextGroup.h"

#include <exception>
#include <iostream>

// 定点引入所需类型，避免在头文件中 using namespace
using yomk::ContextChecker;

ContextGroup::ContextGroup(std::string name)
    : m_name(std::move(name)), m_checkerEnabled(false), m_monitorEnabled(false),
      // 构造即建监控池：固定单线程保事件顺序；池 stop 后不可复用，服务删除后重新注册经新对象新构造自然重建
      m_monitorPool(std::make_unique<YomkSimpleThreadPool>(1))
{
}

ContextGroup::~ContextGroup()
{
    // 防御兜底：正常路径 deinit 已 stop，此处幂等重复调用无害
    stop();
}

void ContextGroup::stop()
{
    // 排空式停止（拒新 -> 排空 -> join）：返回前存量异步监控任务已全部执行；
    // stop 幂等，重复调用安全
    if (m_monitorPool)
    {
        m_monitorPool->stop();
    }
}

const std::string& ContextGroup::name() const
{
    return m_name;
}

int ContextGroup::create(const std::string& key, YomkPkgPtr value)
{
    if (key.empty())
    {
        YOMK_ERR_POS_LOG("key is empty, please check Context.m_key.");
        return 1;
    }
    if (value == nullptr)
    {
        YOMK_ERR_POS_LOG("value is empty, please check Context.m_value.");
        return 2;
    }

    {
        std::unique_lock<std::shared_mutex> lockContexts(m_contextsMutex);
        if (m_contexts.find(key) != m_contexts.end())
        {
            YOMK_ERR_POS_LOG("YomkContext key: " + key + " already exists, please check Context.m_key.");
            return 3;
        }

        Context ctx;
        ctx.key = key;
        ctx.value = value;
        ctx.checker = nullptr;
        ctx.monitors = {};

        m_contexts.emplace(key, ctx);
    }

    return 0;
}

int ContextGroup::destroy(const std::string& key)
{
    {
        std::unique_lock<std::shared_mutex> lockContexts(m_contextsMutex);
        auto itContext = m_contexts.find(key);
        if (itContext == m_contexts.end())
        {
            YOMK_ERR_POS_LOG("YomkContext key: " + key + " is not exist, please check key.");
            return 1;
        }
        m_contexts.erase(itContext);
    }

    return 0;
}

int ContextGroup::get(const std::string& key, YomkPkgPtr defaultValue, YomkPkgPtr& value)
{
    if (key.empty())
    {
        value = defaultValue;
        YOMK_ERR_POS_LOG("key is empty, please check Context.m_key.");
        return 1;
    }

    std::shared_lock<std::shared_mutex> lockContexts(m_contextsMutex);

    auto itContext = m_contexts.find(key);
    if (itContext == m_contexts.end())
    {
        value = defaultValue;
        YOMK_ERR_POS_LOG("YomkContext key: " + key + " is not exist, please check Context.m_key.");
        return 2;
    }

    value = itContext->second.value;
    return 0;
}

int ContextGroup::set(const std::string& key, YomkPkgPtr value)
{
    if (key.empty())
    {
        YOMK_ERR_POS_LOG("key is empty, please check Context.m_key.");
        return 1;
    }

    // 本次提交的键值快照：checker 校验与 monitor 通知统一回传该临时对象（等价于原请求包的 context->d）
    yomk::Context data;
    data.m_key = key;
    data.m_value = value;

    std::unique_lock<std::shared_mutex> lockContexts(m_contextsMutex);
    auto itContext = m_contexts.find(key);
    if (itContext == m_contexts.end())
    {
        YOMK_ERR_POS_LOG("YomkContext key: " + key + " is not exist, please check Context.m_key.");
        return 2;
    }

    if (itContext->second.value->name() != value->name())
    {
        YOMK_ERR_POS_LOG("context: " + key + " type not match, please check Context.m_value.");
        return 3;
    }

    // checker 组开关开启且该 key 设置了 checker 才校验；未设 checker 视为放行（accept），
    // 避免对空 std::function 调用抛 std::bad_function_call 击穿 set 调用链
    if (m_checkerEnabled.load() && itContext->second.checker)
    {
        ContextChecker::ECheckStatus checkStatus = itContext->second.checker(data);
        if (checkStatus == ContextChecker::eReject)
        {
            return 4;
        }
    }

    itContext->second.value =
        value;  // 整体替换值对象（非原地改）：旧对象不动，先前 get/monitor 持有者即冻结为快照
    std::vector<ContextMonitor> monitors = itContext->second.monitors;

    // 异步入队在写锁内完成：入队序 = 提交序；配合单线程 FIFO 池 ⇒ 异步通知恒按 set 提交序送达（并发 set 亦然）。
    // 锁序 m_contextsMutex -> pool.m_mtx 单向安全：worker 弹出任务即释放池锁再锁外执行、deinit 不持本锁调 stop、stop 不持池锁 join，无反向同时持有。
    const bool monitorEnabled = m_monitorEnabled.load();
    if (monitorEnabled)
    {
        for (auto& monitor : monitors)
        {
            if (monitor.asyncMonitor)
            {
                // 值捕获回调与数据副本（data = 本次提交的临时快照），任务执行期自包含；池已停止时投递被拒绝并记日志丢弃
                auto monitorFunc = monitor.contextMonitorFunc;
                if (!(m_monitorPool && m_monitorPool->post([monitorFunc, data]() { monitorFunc(data); })))
                {
                    YOMK_ERR_POS_LOG("context monitor pool is stopped, async monitor ignored.");
                }
            }
        }
    }
    lockContexts.unlock();

    // 后端细节：monitors 已在锁内拷贝；同步回调锁外执行——回调收到本次 set 的键值快照（data），与 m_contexts 后续变更无关，
    // 故不保证实时性；锁外执行使回调内重入 get/keys 安全，但同步回调内重入 set 会递归（不收敛长链栈溢出），
    // 状态机/级联回写应改用异步 monitor（池线程迭代执行、平栈不溢出、按提交序保序）。
    if (monitorEnabled)
    {
        for (auto& monitor : monitors)
        {
            if (!monitor.asyncMonitor)
            {
                // 同步 monitor 异常防护：用户回调异常记日志吞掉，不击穿 set 调用链，
                // 与异步路径线程池兜底对齐；单个回调异常不影响后续 monitor 执行
                try
                {
                    monitor.contextMonitorFunc(data);
                }
                catch (const std::exception& e)
                {
                    YOMK_ERR_POS_LOG("sync monitor threw exception: " + std::string(e.what()) + ", ignored.");
                }
                catch (...)
                {
                    YOMK_ERR_POS_LOG("sync monitor threw unknown exception, ignored.");
                }
            }
        }
    }

    return 0;
}

void ContextGroup::turnOnChecker()
{
    m_checkerEnabled.store(true);
}

void ContextGroup::turnOffChecker()
{
    m_checkerEnabled.store(false);
}

void ContextGroup::turnOnMonitor()
{
    m_monitorEnabled.store(true);
}

void ContextGroup::turnOffMonitor()
{
    m_monitorEnabled.store(false);
}

int ContextGroup::setChecker(const std::string& key, YomkContextCheckFunc checkFunc)
{
    if (key.empty())
    {
        YOMK_ERR_POS_LOG("key is empty, please check ContextChecker.m_key.");
        return 1;
    }
    if (checkFunc == nullptr)
    {
        YOMK_ERR_POS_LOG("checkFunc is empty, please check ContextChecker.m_checkFunc.");
        return 2;
    }

    {
        // 写 checker 字段须持排他锁：与 keyInfo/listAll 的 shared_lock 读及并发 setChecker 互斥，避免数据竞争
        std::unique_lock<std::shared_mutex> lockContexts(m_contextsMutex);
        auto itContext = m_contexts.find(key);
        if (itContext == m_contexts.end())
        {
            YOMK_ERR_POS_LOG(
                "YomkContext key: " + key + " is not exist, please check ContextChecker.m_key.");
            return 3;
        }

        itContext->second.checker = checkFunc;
    }

    return 0;
}

int ContextGroup::setMonitor(const std::string& key, YomkContextMonitorFunc monitorFunc, bool asyncMonitor)
{
    if (key.empty())
    {
        YOMK_ERR_POS_LOG("key is empty, please check ContextMonitor.m_key.");
        return 1;
    }
    if (monitorFunc == nullptr)
    {
        YOMK_ERR_POS_LOG("context monitor function is empty, please check ContextMonitor.m_contextMonitorFunc.");
        return 2;
    }

    {
        // push_back monitors 为写操作须持排他锁：并发 setMonitor 或读 monitors 否则数据竞争甚至 vector 扩容损坏
        std::unique_lock<std::shared_mutex> lockContexts(m_contextsMutex);
        auto itContext = m_contexts.find(key);
        if (itContext == m_contexts.end())
        {
            YOMK_ERR_POS_LOG(
                "YomkContext key: " + key + " is not exist, please check ContextMonitor.m_key.");
            return 3;
        }
        ContextMonitor contextMonitor;
        contextMonitor.contextMonitorFunc = monitorFunc;
        contextMonitor.asyncMonitor = asyncMonitor;
        itContext->second.monitors.push_back(contextMonitor);
    }
    return 0;
}

std::vector<std::string> ContextGroup::keys()
{
    std::vector<std::string> keyList;
    {
        std::shared_lock<std::shared_mutex> lockContexts(m_contextsMutex);
        for (auto& iter : m_contexts)
        {
            keyList.push_back(iter.first);
        }
    }
    return keyList;
}

int ContextGroup::keyInfo(const std::string& key, yomk::ContextKeyInfo& info)
{
    if (key.empty())
    {
        YOMK_ERR_POS_LOG("key is empty, please check String.d.");
        return 1;
    }

    std::shared_lock<std::shared_mutex> lockContexts(m_contextsMutex);
    auto itContext = m_contexts.find(key);
    if (itContext == m_contexts.end())
    {
        YOMK_ERR_POS_LOG("YomkContext key: " + key + " is not exist, please check key.");
        return 2;
    }
    // 命中：锁内填充结构化快照（info 重置后逐字段赋值，出参回传全部元数据）
    const auto& ctx = itContext->second;
    info = yomk::ContextKeyInfo();
    info.m_key = ctx.key;
    info.m_typeName = ctx.value ? ctx.value->name() : "";
    info.m_checkerOn = static_cast<bool>(ctx.checker);
    info.m_monitorCount = ctx.monitors.size();
    for (auto& monitor : ctx.monitors)
    {
        if (monitor.asyncMonitor)
        {
            ++info.m_asyncMonitorCount;
        }
    }
    info.m_group = m_name;
    return 0;
}

// 内省：全量键元信息快照，锁内构造、值返回（快照携带所属组名）
std::vector<yomk::ContextKeyInfo> ContextGroup::listAll()
{
    std::vector<yomk::ContextKeyInfo> infos;
    {
        std::shared_lock<std::shared_mutex> lockContexts(m_contextsMutex);
        infos.reserve(m_contexts.size());
        for (auto& iter : m_contexts)
        {
            const auto& ctx = iter.second;
            yomk::ContextKeyInfo info;
            info.m_key = ctx.key;
            info.m_typeName = ctx.value ? ctx.value->name() : "";
            info.m_checkerOn = static_cast<bool>(ctx.checker);
            info.m_monitorCount = ctx.monitors.size();
            for (auto& monitor : ctx.monitors)
            {
                if (monitor.asyncMonitor)
                {
                    ++info.m_asyncMonitorCount;
                }
            }
            info.m_group = m_name;
            infos.push_back(std::move(info));
        }
    }
    return infos;
}
