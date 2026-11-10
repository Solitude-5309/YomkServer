from readerwriterlock import rwlock

from yomk._diag import warn as diag_warn
from yomk.ctx.ContextGroup import ContextGroup
from yomk.msg.ctx import (
    Context,
    ContextChecker,
    ContextGroupInfo,
    ContextGroupKey,
    ContextMonitor,
)
from yomk.msg.server import ResStatus, Response
from yomk.server import YomkService


class YomkContext(YomkService):
    # 上下文内置服务：链式 builder 全部终结经 api_request 到达本域端点。
    # 诊断契约（对齐 C++ 0032fc7）：空组名/空键/空值/不存在/类型错配/拒绝
    # 一律 WARN（ContextGroup 层发出），unpack 失败无 diag（Response.msg 已携
    # 具体类型名），成功路径无 diag
    def __init__(self, server):
        super().__init__(server)
        self.set_name("/YomkContext")
        self.m_main = ContextGroup("main")
        # 自定义组表：按组名惰性自动创建、不支持销毁（只增不减）；
        # 组名唯一由 dict key 天然保证，同名寻址即复用同组
        self.m_customGroup = {}
        self.m_customGroupMutex = rwlock.RWLockFair()

    def init(self):
        self.install_func("/create", self.create, "Context")
        self.install_func("/destroy", self.destroy, "ContextGroupKey")
        self.install_func("/get", self.get, "Context")
        self.install_func("/set", self.set, "Context")
        self.install_func("/turn_on_checker", self.turn_on_checker)
        self.install_func("/turn_off_checker", self.turn_off_checker)
        self.install_func("/set_checker", self.set_checker, "ContextChecker")
        self.install_func("/turn_on_monitor", self.turn_on_monitor)
        self.install_func("/turn_off_monitor", self.turn_off_monitor)
        self.install_func("/set_monitor", self.set_monitor, "ContextMonitor")
        self.install_func("/groups", self.groups)
        self.install_func("/keys", self.keys)
        self.install_func("/key", self.key_info, "ContextGroupKey")
        self.install_func("/all", self.list_all)

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

    @staticmethod
    def _unpack(expected, pkg):
        # 裸链误用载荷：eNo + "pkg is not <X>, got '<type>'"（对齐 C++
        # unpack 失败契约，typeOf 语言映射 type(pkg).__name__；String 为
        # std 报文，Python 载体是内建 str；Response.msg 已携带同信息，
        # 不打诊断日志）
        if isinstance(pkg, expected):
            return None
        name = "String" if expected is str else expected.__name__
        return Response(ResStatus.eNo,
                        "pkg is not " + name
                        + ", got '" + type(pkg).__name__ + "'")

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

    def turn_on_monitor(self, pkg):
        bad = self._unpack(str, pkg)
        if bad:
            return bad
        group, fail = self.find_group(pkg)
        if group is None:
            return fail
        group.turn_on_monitor()
        return Response(ResStatus.eOk, "turn on monitor success")

    def turn_off_monitor(self, pkg):
        bad = self._unpack(str, pkg)
        if bad:
            return bad
        group, fail = self.find_group(pkg)
        if group is None:
            return fail
        group.turn_off_monitor()
        return Response(ResStatus.eOk, "turn off monitor success")

    def set_monitor(self, pkg):
        bad = self._unpack(ContextMonitor, pkg)
        if bad:
            return bad

        group, fail = self.find_group(pkg.group)
        if group is None:
            return fail

        # 返回码映射（约定见 ContextGroup.set_monitor）：0 成功 / 1 key 为空 /
        # 2 monitor_func 为空 / 3 key 不存在；入参非法（键空/回调空）= Invalid
        rc = group.set_monitor(
            pkg.key,
            pkg.monitor_func,
            pkg.async_monitor)
        if rc == 1:
            return Response(ResStatus.eInvalid, "set monitor key is empty")
        if rc == 2:
            return Response(
                ResStatus.eInvalid,
                "set monitor key '" + pkg.key
                + "' failed: monitor function is empty")
        if rc == 3:
            return Response(ResStatus.eNo,
                            "context key '" + pkg.key + "' does not exist")
        return Response(ResStatus.eOk, "set context monitor success")

    def set_checker(self, pkg):
        bad = self._unpack(ContextChecker, pkg)
        if bad:
            return bad

        group, fail = self.find_group(pkg.group)
        if group is None:
            return fail

        # 返回码映射（约定见 ContextGroup.set_checker）：0 成功 / 1 key 为空 /
        # 2 check_func 为空 / 3 key 不存在；入参非法（键空/回调空）= Invalid
        rc = group.set_checker(pkg.key, pkg.check_func)
        if rc == 1:
            return Response(ResStatus.eInvalid, "set checker key is empty")
        if rc == 2:
            return Response(
                ResStatus.eInvalid,
                "set checker key '" + pkg.key
                + "' failed: checkFunc is empty")
        if rc == 3:
            return Response(ResStatus.eNo,
                            "context key '" + pkg.key + "' does not exist")
        return Response(ResStatus.eOk, "set checker success")

    def turn_on_checker(self, pkg):
        bad = self._unpack(str, pkg)
        if bad:
            return bad
        group, fail = self.find_group(pkg)
        if group is None:
            return fail
        group.turn_on_checker()
        return Response(ResStatus.eOk, "turn on checker success")

    def turn_off_checker(self, pkg):
        bad = self._unpack(str, pkg)
        if bad:
            return bad
        group, fail = self.find_group(pkg)
        if group is None:
            return fail
        group.turn_off_checker()
        return Response(ResStatus.eOk, "turn off checker success")

    def create(self, pkg):
        bad = self._unpack(Context, pkg)
        if bad:
            return bad

        group, fail = self.find_group(pkg.group)
        if group is None:
            return fail

        # 返回码映射（约定见 ContextGroup.create）：0 成功 / 1 key 为空 /
        # 2 value 为空 / 3 key 已存在；入参非法（键/值为空）= Invalid，
        # 业务冲突（已存在）= Failed
        rc = group.create(pkg.key, pkg.value)
        if rc == 1:
            return Response(ResStatus.eInvalid, "context key is empty")
        if rc == 2:
            return Response(
                ResStatus.eInvalid,
                "create context key '" + pkg.key + "' failed: value is empty")
        if rc == 3:
            return Response(ResStatus.eNo,
                            "context key '" + pkg.key + "' already exists")
        return Response(ResStatus.eOk, "create context success")

    def destroy(self, pkg):
        bad = self._unpack(ContextGroupKey, pkg)
        if bad:
            return bad

        group, fail = self.find_group(pkg.group)
        if group is None:
            return fail

        # 返回码映射（约定见 ContextGroup.destroy）：0 成功 / 1 key 不存在 /
        # 2 key 为空；入参非法（键空）= Invalid，业务未命中 = Failed
        rc = group.destroy(pkg.key)
        if rc == 2:
            return Response(ResStatus.eInvalid, "context key is empty")
        if rc == 1:
            return Response(ResStatus.eNo,
                            "context key '" + pkg.key + "' does not exist")
        return Response(ResStatus.eOk, "destroy context success")

    def get(self, pkg):
        bad = self._unpack(Context, pkg)
        if bad:
            return bad

        group, fail = self.find_group(pkg.group)
        if group is None:
            return fail

        # 返回码映射（约定见 ContextGroup.get）：0 命中 / 1 key 为空 /
        # 2 key 不存在；未命中 value 已为默认值，msg 注明返回默认值
        # （提示 data 非存储值）
        rc, value = group.get(pkg.key, pkg.value)
        if rc == 1:
            return Response(ResStatus.eInvalid,
                            "context key is empty, return default value",
                            value)
        if rc == 2:
            return Response(
                ResStatus.eNo,
                "context key '" + pkg.key
                + "' does not exist, return default value",
                value)
        return Response(ResStatus.eOk, "get context success", value)

    def set(self, pkg):
        bad = self._unpack(Context, pkg)
        if bad:
            return bad

        group, fail = self.find_group(pkg.group)
        if group is None:
            return fail

        # 失败文案经 detail 透传（ContextGroup.set 失败时已携带 key 值与
        # 期望/实际类型名，此处无需再分支拼装）；入参非法（键空/value 空，
        # rc 1/5）= Invalid，业务未命中/冲突（2/3/4）= Failed
        rc, detail = group.set(pkg.key, pkg.value)
        if rc == 1 or rc == 5:
            return Response(ResStatus.eInvalid, detail)
        if rc != 0:
            return Response(ResStatus.eNo, detail)
        return Response(ResStatus.eOk, "set context success")

    def groups(self, pkg):
        # 全部组名：main 恒在首位，自定义组按字典序追加（锁内拷出、锁外拼响应）
        groupNames = [self.m_main.name()]
        with self.m_customGroupMutex.gen_rlock():
            groupNames.extend(sorted(self.m_customGroup.keys()))
        return Response(ResStatus.eOk, "ok", groupNames)

    def keys(self, pkg):
        bad = self._unpack(str, pkg)
        if bad:
            return bad
        group, fail = self.find_group(pkg)
        if group is None:
            return fail
        return Response(ResStatus.eOk, "ok", group.keys())

    def key_info(self, pkg):
        bad = self._unpack(ContextGroupKey, pkg)
        if bad:
            return bad

        group, fail = self.find_group(pkg.group)
        if group is None:
            return fail

        # 返回码映射（约定见 ContextGroup.key_info）：0 命中 / 1 key 为空 /
        # 2 key 不存在；入参非法（键空）= Invalid
        rc, info = group.key_info(pkg.key)
        if rc == 1:
            return Response(ResStatus.eInvalid, "context key is empty")
        if rc == 2:
            return Response(ResStatus.eNo,
                            "context key '" + pkg.key + "' does not exist")
        return Response(ResStatus.eOk, "ok", info)

    def list_all(self, pkg):
        # 跨组聚合：组 → 键元信息两层结构（与服务全量内省同构的拓扑范式）；
        # main 组条目恒在首位，自定义组按字典序追加（锁内拷出、锁外取快照）
        groups = [ContextGroupInfo(self.m_main.name(), self.m_main.list_all())]
        with self.m_customGroupMutex.gen_rlock():
            customGroups = [self.m_customGroup[name]
                            for name in sorted(self.m_customGroup.keys())]
        for group in customGroups:
            groups.append(ContextGroupInfo(group.name(), group.list_all()))
        return Response(ResStatus.eOk, "ok", groups)
