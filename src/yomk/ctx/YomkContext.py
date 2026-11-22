from readerwriterlock import rwlock

from yomk._diag import warn as diag_warn
from yomk.ctx.ContextGroup import ContextGroup
from yomk.msgs.ctx import (
    Context,
    ContextChecker,
    ContextGroupInfo,
    ContextGroupInfoList,
    ContextGroupKey,
    ContextMonitor,
)
from yomk.msgs.server import ResStatus, Response
from yomk.msgs.std_msg import String, StringArray
from yomk.msgs.YomkMsg import type_mismatch, unpack
from yomk.server import YomkService
from yomk.url.ctx import (
    CTX_ALL_DESC,
    CTX_CREATE_DESC,
    CTX_DESTROY_DESC,
    CTX_GET_DESC,
    CTX_GROUPS_DESC,
    CTX_KEY_DESC,
    CTX_KEYS_DESC,
    CTX_SET_CHECKER_DESC,
    CTX_SET_DESC,
    CTX_SET_MONITOR_DESC,
    CTX_TURN_OFF_CHECKER_DESC,
    CTX_TURN_OFF_MONITOR_DESC,
    CTX_TURN_ON_CHECKER_DESC,
    CTX_TURN_ON_MONITOR_DESC,
)


class YomkContext(YomkService):
    # 上下文内置服务：链式 builder 的全部终结都经 api_request 到达本域端点。
    # 诊断契约：空组名/空键/空值/不存在/类型错配/拒绝一律 WARN（由 ContextGroup
    # 发出）；unpack 失败不打诊断（Response.msg 已携具体类型名），成功路径无诊断。
    def __init__(self, server):
        super().__init__(server)
        self.set_name("/YomkContext")
        self.m_main = ContextGroup("main")
        # 自定义组表：按组名惰性自动创建、不支持销毁（只增不减）；
        # 组名唯一由 dict key 天然保证，同名寻址即复用同组
        self.m_customGroup = {}
        self.m_customGroupMutex = rwlock.RWLockFair()

    def init(self):
        self.install_func("/create", self.create, Context.TYPE_NAME, CTX_CREATE_DESC)
        self.install_func("/destroy", self.destroy, ContextGroupKey.TYPE_NAME,
                          CTX_DESTROY_DESC)
        self.install_func("/get", self.get, Context.TYPE_NAME, CTX_GET_DESC)
        self.install_func("/set", self.set, Context.TYPE_NAME, CTX_SET_DESC)
        self.install_func("/turn_on_checker", self.turn_on_checker, String.TYPE_NAME,
                          CTX_TURN_ON_CHECKER_DESC)
        self.install_func("/turn_off_checker", self.turn_off_checker, String.TYPE_NAME,
                          CTX_TURN_OFF_CHECKER_DESC)
        self.install_func("/set_checker", self.set_checker, ContextChecker.TYPE_NAME,
                          CTX_SET_CHECKER_DESC)
        self.install_func("/turn_on_monitor", self.turn_on_monitor, String.TYPE_NAME,
                          CTX_TURN_ON_MONITOR_DESC)
        self.install_func("/turn_off_monitor", self.turn_off_monitor, String.TYPE_NAME,
                          CTX_TURN_OFF_MONITOR_DESC)
        self.install_func("/set_monitor", self.set_monitor, ContextMonitor.TYPE_NAME,
                          CTX_SET_MONITOR_DESC)
        self.install_func("/groups", self.groups, "", CTX_GROUPS_DESC)
        self.install_func("/keys", self.keys, String.TYPE_NAME, CTX_KEYS_DESC)
        self.install_func("/key", self.key_info, ContextGroupKey.TYPE_NAME,
                          CTX_KEY_DESC)
        self.install_func("/all", self.list_all, "", CTX_ALL_DESC)

    def deinit(self):
        # 排空式停止监控池由组自持（拒新 -> 排空 -> join）：返回前存量异步监控任务已全部执行；
        # stop 幂等，重复 deinit 安全
        self.m_main.stop()
        # 自定义组：锁内拷出并清表、锁外逐组排空停止（保持"deinit 不持锁调 stop"）
        with self.m_customGroupMutex.gen_wlock():
            groups = list(self.m_customGroup.values())
            self.m_customGroup.clear()
        for group in groups:
            group.stop()

    def find_group(self, group_name):
        # 组路由辅助：main 组成员直取；自定义组按名惰性自动创建（不支持销毁，只增不减）；
        # 返回 (group, fail)：fail 非 None 时调用方直接返回该错误响应
        if not group_name:
            diag_warn("group name is empty, please check Context.m_group")
            return None, Response(ResStatus.eInvalid, "group name is empty")
        if group_name == "main":
            return self.m_main, None

        # 非 main：读锁快查自定义组表
        with self.m_customGroupMutex.gen_rlock():
            group = self.m_customGroup.get(group_name)
        if group is not None:
            return group, None
        # 未命中：写锁内 double-check 后惰性创建（构造 + 入表锁内原子完成，避免并发双建）；
        # 表持有强引用且只增不减，锁外使用长期安全
        with self.m_customGroupMutex.gen_wlock():
            group = self.m_customGroup.get(group_name)
            if group is None:
                group = ContextGroup(group_name)
                self.m_customGroup[group_name] = group
        return group, None

    def turn_on_monitor(self, msg):
        if unpack(msg, String) is None:
            return Response(ResStatus.eNo, type_mismatch(msg, String.TYPE_NAME))
        group, fail = self.find_group(msg.value)
        if group is None:
            return fail
        group.turn_on_monitor()
        return Response(ResStatus.eOk, "turn on monitor success")

    def turn_off_monitor(self, msg):
        if unpack(msg, String) is None:
            return Response(ResStatus.eNo, type_mismatch(msg, String.TYPE_NAME))
        group, fail = self.find_group(msg.value)
        if group is None:
            return fail
        group.turn_off_monitor()
        return Response(ResStatus.eOk, "turn off monitor success")

    def set_monitor(self, msg):
        if unpack(msg, ContextMonitor) is None:
            return Response(ResStatus.eNo,
                            type_mismatch(msg, ContextMonitor.TYPE_NAME))

        group, fail = self.find_group(msg.group)
        if group is None:
            return fail

        # 返回码映射（约定见 ContextGroup.set_monitor）：0 成功 / 1 key 为空 /
        # 2 monitor_func 为空 / 3 key 不存在；入参非法（键空/回调空）= Invalid
        rc = group.set_monitor(
            msg.key,
            msg.monitor_func,
            msg.async_monitor)
        if rc == 1:
            return Response(ResStatus.eInvalid, "set monitor key is empty")
        if rc == 2:
            return Response(
                ResStatus.eInvalid,
                "set monitor key '" + msg.key
                + "' failed: monitor function is empty")
        if rc == 3:
            return Response(ResStatus.eNo,
                            "context key '" + msg.key + "' does not exist")
        return Response(ResStatus.eOk, "set context monitor success")

    def set_checker(self, msg):
        if unpack(msg, ContextChecker) is None:
            return Response(ResStatus.eNo,
                            type_mismatch(msg, ContextChecker.TYPE_NAME))

        group, fail = self.find_group(msg.group)
        if group is None:
            return fail

        # 返回码映射（约定见 ContextGroup.set_checker）：0 成功 / 1 key 为空 /
        # 2 check_func 为空 / 3 key 不存在；入参非法（键空/回调空）= Invalid
        rc = group.set_checker(msg.key, msg.check_func)
        if rc == 1:
            return Response(ResStatus.eInvalid, "set checker key is empty")
        if rc == 2:
            return Response(
                ResStatus.eInvalid,
                "set checker key '" + msg.key
                + "' failed: checkFunc is empty")
        if rc == 3:
            return Response(ResStatus.eNo,
                            "context key '" + msg.key + "' does not exist")
        return Response(ResStatus.eOk, "set checker success")

    def turn_on_checker(self, msg):
        if unpack(msg, String) is None:
            return Response(ResStatus.eNo, type_mismatch(msg, String.TYPE_NAME))
        group, fail = self.find_group(msg.value)
        if group is None:
            return fail
        group.turn_on_checker()
        return Response(ResStatus.eOk, "turn on checker success")

    def turn_off_checker(self, msg):
        if unpack(msg, String) is None:
            return Response(ResStatus.eNo, type_mismatch(msg, String.TYPE_NAME))
        group, fail = self.find_group(msg.value)
        if group is None:
            return fail
        group.turn_off_checker()
        return Response(ResStatus.eOk, "turn off checker success")

    def create(self, msg):
        if unpack(msg, Context) is None:
            return Response(ResStatus.eNo, type_mismatch(msg, Context.TYPE_NAME))

        group, fail = self.find_group(msg.group)
        if group is None:
            return fail

        # 返回码映射（约定见 ContextGroup.create）：0 成功 / 1 key 为空 /
        # 2 value 为空 / 3 key 已存在；入参非法（键/值为空）= Invalid，
        # 业务冲突（已存在）= Failed
        rc = group.create(msg.key, msg.value)
        if rc == 1:
            return Response(ResStatus.eInvalid, "context key is empty")
        if rc == 2:
            return Response(
                ResStatus.eInvalid,
                "create context key '" + msg.key + "' failed: value is empty")
        if rc == 3:
            return Response(ResStatus.eNo,
                            "context key '" + msg.key + "' already exists")
        return Response(ResStatus.eOk, "create context success")

    def destroy(self, msg):
        if unpack(msg, ContextGroupKey) is None:
            return Response(ResStatus.eNo,
                            type_mismatch(msg, ContextGroupKey.TYPE_NAME))

        group, fail = self.find_group(msg.group)
        if group is None:
            return fail

        # 返回码映射（约定见 ContextGroup.destroy）：0 成功 / 1 key 不存在 /
        # 2 key 为空；入参非法（键空）= Invalid，业务未命中 = Failed
        rc = group.destroy(msg.key)
        if rc == 2:
            return Response(ResStatus.eInvalid, "context key is empty")
        if rc == 1:
            return Response(ResStatus.eNo,
                            "context key '" + msg.key + "' does not exist")
        return Response(ResStatus.eOk, "destroy context success")

    def get(self, msg):
        if unpack(msg, Context) is None:
            return Response(ResStatus.eNo, type_mismatch(msg, Context.TYPE_NAME))

        group, fail = self.find_group(msg.group)
        if group is None:
            return fail

        # 返回码映射（约定见 ContextGroup.get）：0 命中 / 1 key 为空 /
        # 2 key 不存在；未命中 value 已为默认值，msg 注明返回默认值
        # （提示 data 非存储值）
        rc, value = group.get(msg.key, msg.value)
        if rc == 1:
            return Response(ResStatus.eInvalid,
                            "context key is empty, return default value",
                            value)
        if rc == 2:
            return Response(
                ResStatus.eNo,
                "context key '" + msg.key
                + "' does not exist, return default value",
                value)
        return Response(ResStatus.eOk, "get context success", value)

    def set(self, msg):
        if unpack(msg, Context) is None:
            return Response(ResStatus.eNo, type_mismatch(msg, Context.TYPE_NAME))

        group, fail = self.find_group(msg.group)
        if group is None:
            return fail

        # 失败文案经 detail 透传（ContextGroup.set 失败时已携带 key 值与
        # 期望/实际类型名，此处无需再分支拼装）；入参非法（键空/value 空，
        # rc 1/5）= Invalid，业务未命中/冲突（2/3/4）= Failed
        rc, detail = group.set(msg.key, msg.value)
        if rc == 1 or rc == 5:
            return Response(ResStatus.eInvalid, detail)
        if rc != 0:
            return Response(ResStatus.eNo, detail)
        return Response(ResStatus.eOk, "set context success")

    def groups(self, msg):
        # 全部组名：main 恒在首位，自定义组按字典序追加（锁内拷出、锁外拼响应）
        groupNames = [self.m_main.name()]
        with self.m_customGroupMutex.gen_rlock():
            groupNames.extend(sorted(self.m_customGroup.keys()))
        return Response(ResStatus.eOk, "ok", StringArray(groupNames))

    def keys(self, msg):
        if unpack(msg, String) is None:
            return Response(ResStatus.eNo, type_mismatch(msg, String.TYPE_NAME))
        group, fail = self.find_group(msg.value)
        if group is None:
            return fail
        return Response(ResStatus.eOk, "ok", StringArray(group.keys()))

    def key_info(self, msg):
        if unpack(msg, ContextGroupKey) is None:
            return Response(ResStatus.eNo,
                            type_mismatch(msg, ContextGroupKey.TYPE_NAME))

        group, fail = self.find_group(msg.group)
        if group is None:
            return fail

        # 返回码映射（约定见 ContextGroup.key_info）：0 命中 / 1 key 为空 /
        # 2 key 不存在；入参非法（键空）= Invalid
        rc, info = group.key_info(msg.key)
        if rc == 1:
            return Response(ResStatus.eInvalid, "context key is empty")
        if rc == 2:
            return Response(ResStatus.eNo,
                            "context key '" + msg.key + "' does not exist")
        return Response(ResStatus.eOk, "ok", info)

    def list_all(self, msg):
        # 跨组聚合：组 → 键元信息两层结构（与服务全量内省同构的拓扑范式）；
        # main 组条目恒在首位，自定义组按字典序追加（锁内拷出、锁外取快照）
        groups = [ContextGroupInfo(self.m_main.name(), self.m_main.list_all())]
        with self.m_customGroupMutex.gen_rlock():
            customGroups = [self.m_customGroup[name]
                            for name in sorted(self.m_customGroup.keys())]
        for group in customGroups:
            groups.append(ContextGroupInfo(group.name(), group.list_all()))
        return Response(ResStatus.eOk, "ok", ContextGroupInfoList(groups))
