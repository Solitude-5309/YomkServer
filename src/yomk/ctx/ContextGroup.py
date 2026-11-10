from concurrent.futures import ThreadPoolExecutor

from readerwriterlock import rwlock

from yomk._diag import error as diag_error
from yomk._diag import warn as diag_warn
from yomk.msg.ctx import CheckStatus, Context, ContextKeyInfo


class Monitor:
    # 组内 monitor 存储条目：(回调, 通知形态布尔)，回调与形态正交（对齐
    # C++ ContextMonitor 单回调+标志形状）
    def __init__(self, monitor_func=None, async_monitor=False):
        self.monitor_func = monitor_func
        self.async_monitor = async_monitor


class ContextEntry:
    # Context 单条目内部结构（ContextGroup 持有），
    # 与消息包 Context 区分，仅服务内部使用
    def __init__(self, value):
        self.value = value
        self.checker = None    # check_func 或 None（可空）
        self.monitors = []     # [Monitor]


class ContextGroup:
    # Context 底层实例组：完整承载单组功能（键存储、checker/monitor 门控开关、
    # 自持单线程异步监控池），不继承 YomkService；YomkContext 按组名路由到
    # ContextGroup 实例（与 EventLoop 模块同款双层写法）。
    # 诊断契约（对齐 C++ 0032fc7）：用户误用自愈路径一律 WARN 携具体值
    # （空键/空值/已存在/不存在/类型错配/拒绝/pool 停止），同步 monitor 回调
    # 异常 ERROR，unpack 失败无 diag（服务层负责）
    def __init__(self, name):
        self.m_name = name
        self.m_checkerEnabled = False
        self.m_monitorEnabled = False
        self.m_contexts = {}     # {key: ContextEntry}（单条目结构）
        self.m_contextsMutex = rwlock.RWLockFair()
        # 构造即建监控池：池 shutdown 后不可复用，服务删除后重新注册经新对象新构造自然重建；
        # 固定单线程保事件顺序
        self.m_monitorPool = ThreadPoolExecutor(1)

    def name(self):
        return self.m_name

    def stop(self):
        # 排空式停止监控池（拒新 -> 排空 -> join）：返回前存量异步监控任务已全部执行；
        # shutdown 幂等，重复调用安全
        if self.m_monitorPool is not None:
            self.m_monitorPool.shutdown(wait=True)
            self.m_monitorPool = None

    # ---- 明参业务方法：返回码约定见各方法注释，响应构造在 YomkContext 层 ----

    # 返回：0 成功 / 1 key 为空 / 2 value 为空 / 3 key 已存在
    def create(self, key, value):
        if not key:
            diag_warn("key is empty, please check Context.m_key")
            return 1
        if value is None:
            diag_warn("context create key '" + key + "' failed: value is empty")
            return 2

        with self.m_contextsMutex.gen_wlock():
            if key in self.m_contexts:
                diag_warn("context key '" + key + "' already exists")
                return 3
            self.m_contexts[key] = ContextEntry(value)

        return 0

    # 返回：0 成功 / 1 key 不存在 / 2 key 为空
    def destroy(self, key):
        if not key:
            diag_warn("key is empty, please check key")
            return 2
        with self.m_contextsMutex.gen_wlock():
            if key not in self.m_contexts:
                diag_warn("YomkContext key: " + key
                          + " does not exist, please check key")
                return 1
            del self.m_contexts[key]

        return 0

    # 命中时 value 为现值，key 为空或未命中时为默认值；返回：0 命中 / 1 key 为空 / 2 key 不存在
    def get(self, key, default_value):
        if not key:
            diag_warn("key is empty, please check Context.m_key")
            return 1, default_value

        with self.m_contextsMutex.gen_rlock():
            if key not in self.m_contexts:
                diag_warn("YomkContext key: " + key
                          + " does not exist, please check Context.m_key")
                return 2, default_value
            return 0, self.m_contexts[key].value

    # 返回：(rc, detail)；0 成功 / 1 key 为空 / 2 key 不存在 / 3 类型不匹配 /
    # 4 checker 拒绝 / 5 value 为空；失败 detail 携带具体值与期望/实际类型名，
    # 经 YomkContext 透传进 Response.msg（对齐 C++ set(key, value, detail&)）
    def set(self, key, value):
        detail = "ok"
        if not key:
            diag_warn("key is empty, please check Context.m_key")
            detail = "set context key is empty"
            return 1, detail
        if value is None:
            diag_warn("set context key '" + key + "' failed: value is empty")
            detail = "set context key '" + key + "' failed: value is empty"
            return 5, detail

        # 本次提交的键值快照：checker 校验与 monitor 通知统一回传该临时对象
        data = Context(key, value)

        with self.m_contextsMutex.gen_wlock():
            entry = self.m_contexts.get(key)
            if entry is None:
                diag_warn("context key '" + key + "' does not exist")
                detail = "context key '" + key + "' does not exist"
                return 2, detail

            # 类型校验同名 key 的值类型名必须一致（新值为 None 时
            # NoneType 必不匹配，自然被拒）；具体值原则：期望/实际类型名
            # 一并输出，用户一眼定位类型错配
            expect = type(entry.value).__name__
            got = type(value).__name__
            if expect != got:
                diag_warn("context type not match: key '" + key
                          + "' expect '" + expect + "', got '" + got + "'")
                detail = ("context type not match: key '" + key
                          + "' expect '" + expect + "', got '" + got + "'")
                return 3, detail

            if self.m_checkerEnabled and entry.checker:
                if (entry.checker(data) == CheckStatus.eReject):
                    diag_warn("set context key '" + key
                              + "' rejected by checker")
                    detail = "set context key '" + key + "' rejected by checker"
                    return 4, detail

            entry.value = value
            monitors_snapshot = list(entry.monitors)

            # 异步入队在写锁内完成：入队序 = 提交序；配合单线程 FIFO 池 ⇒
            # 异步通知恒按 set 提交序送达，并发 set 亦然。
            # 锁序 m_contextsMutex -> 池内部锁单向安全：worker 锁外执行、stop 不持本锁调用
            monitor_enabled = self.m_monitorEnabled
            if monitor_enabled:
                for monitor in monitors_snapshot:
                    if (monitor.async_monitor):
                        self._post_monitor(monitor.monitor_func, data)

        if monitor_enabled:
            for monitor in monitors_snapshot:
                if not monitor.async_monitor:
                    # 同步 monitor 异常防护：用户回调异常记日志吞掉，不击穿 set 调用链，
                    # 单个回调异常不影响后续 monitor 执行
                    try:
                        monitor.monitor_func(data)
                    except Exception as e:
                        diag_error("sync monitor threw exception: " + str(e)
                                   + ", ignored")

        return 0, detail

    def _post_monitor(self, func, ctx):
        # 异步监控投递：池未创建或已停止时拒绝并记日志丢弃
        if self.m_monitorPool is None:
            diag_warn("context monitor pool is stopped, async monitor ignored")
            return
        try:
            self.m_monitorPool.submit(self._run_monitor_safe, func, ctx)
        except RuntimeError:
            # 池 shutdown 后 submit 拒新
            diag_warn("context monitor pool is stopped, async monitor ignored")

    @staticmethod
    def _run_monitor_safe(func, ctx):
        # 池 worker 兜底：异步监控回调异常记日志吞掉，不终止 worker
        try:
            func(ctx)
        except Exception as e:
            diag_error("async monitor threw exception: " + str(e) + ", ignored")

    def turn_on_checker(self):
        self.m_checkerEnabled = True

    def turn_off_checker(self):
        self.m_checkerEnabled = False

    def turn_on_monitor(self):
        self.m_monitorEnabled = True

    def turn_off_monitor(self):
        self.m_monitorEnabled = False

    # 返回：0 成功 / 1 key 为空 / 2 check_func 为空 / 3 key 不存在
    def set_checker(self, key, check_func):
        if not key:
            diag_warn("key is empty, please check ContextChecker.m_key")
            return 1
        if not check_func:
            diag_warn("set checker key '" + key
                      + "' failed: checkFunc is empty")
            return 2

        with self.m_contextsMutex.gen_wlock():
            entry = self.m_contexts.get(key)
            if entry is None:
                diag_warn("YomkContext key: " + key
                          + " does not exist, please check ContextChecker.m_key")
                return 3
            entry.checker = check_func

        return 0

    # 返回：0 成功 / 1 key 为空 / 2 monitor_func 为空 / 3 key 不存在
    def set_monitor(self, key, monitor_func, async_monitor):
        if not key:
            diag_warn("key is empty, please check ContextMonitor.m_key")
            return 1
        if not monitor_func:
            diag_warn("set monitor key '" + key
                      + "' failed: monitor function is empty")
            return 2

        with self.m_contextsMutex.gen_wlock():
            entry = self.m_contexts.get(key)
            if entry is None:
                diag_warn("YomkContext key: " + key
                          + " does not exist, please check ContextMonitor.m_key")
                return 3
            entry.monitors.append(Monitor(monitor_func, async_monitor))

        return 0

    def keys(self):
        with self.m_contextsMutex.gen_rlock():
            # 排序输出:map 迭代序（字典序）
            return sorted(self.m_contexts.keys())

    # 命中时返回 (0, ContextKeyInfo)；返回：0 命中 / 1 key 为空 / 2 key 不存在
    def key_info(self, key):
        if not key:
            diag_warn("key is empty, please check String.d")
            return 1, None

        # 命中：锁内构造结构化快照，明确字段承载全部返回数据（msg 恒 "ok"，数据走 data）
        with self.m_contextsMutex.gen_rlock():
            if key not in self.m_contexts:
                diag_warn("YomkContext key: " + key
                          + " does not exist, please check key")
                return 2, None
            entry = self.m_contexts[key]
            async_count = 0
            for monitor in entry.monitors:
                if monitor.async_monitor:
                    async_count += 1
            info = ContextKeyInfo(
                key,
                type(entry.value).__name__ if entry.value is not None else "",
                bool(entry.checker),
                len(entry.monitors),
                async_count,
                self.m_name)
            return 0, info

    def list_all(self):
        # 锁内构造结构化快照列表，明确字段承载全部返回数据（msg 恒 "ok"，数据走 data）；
        # 快照携带所属组名
        infos = []
        with self.m_contextsMutex.gen_rlock():
            # 排序遍历:map 迭代序（字典序）
            for key in sorted(self.m_contexts.keys()):
                entry = self.m_contexts[key]
                async_count = 0
                for monitor in entry.monitors:
                    if monitor.async_monitor:
                        async_count += 1
                infos.append(ContextKeyInfo(
                    key,
                    type(entry.value).__name__ if entry.value is not None else "",
                    bool(entry.checker),
                    len(entry.monitors),
                    async_count,
                    self.m_name))
        return infos
