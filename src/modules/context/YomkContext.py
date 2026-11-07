# 源片段：src/modules/context/YomkContext.py（构建期由 scripts/assemble_py.py 合并，请勿单独导入）



class YomkContext(YomkService):
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

    def find_group(self, group_name):
        # 组路由辅助：main 组成员直取；自定义组按名惰性自动创建（不支持销毁，只增不减）；
        # 返回 (group, fail)：fail 非 None 时调用方直接返回该错误响应
        if not group_name:
            log.error("group name is empty, please check Context.group.")
            return None, YomkResponse(ResStatus.eNo, "group name is empty")
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
        # 组名走 API 请求包（裸字符串）；历史调用传 None 时按缺省组 main 兜底
        group, fail = self.find_group(pkg if isinstance(pkg, str) else "main")
        if group is None:
            return fail
        group.turn_on_monitor()
        return YomkResponse(ResStatus.eOk, "turn on monitor success")

    def turn_off_monitor(self, pkg):
        group, fail = self.find_group(pkg if isinstance(pkg, str) else "main")
        if group is None:
            return fail
        group.turn_off_monitor()
        return YomkResponse(ResStatus.eOk, "turn off monitor success")

    def set_monitor(self, contextMonitor):
        if not isinstance(contextMonitor, ContextMonitor):
            raise TypeError("contextMonitor must be ContextMonitor")

        group, fail = self.find_group(contextMonitor.group)
        if group is None:
            return fail

        # 返回码映射（约定见 ContextGroup.set_monitor）：0 成功 / 1 key 为空 / 2 monitor_func 为空 / 3 key 不存在
        rc = group.set_monitor(
            contextMonitor.key,
            contextMonitor.monitor_func,
            contextMonitor.async_monitor_func)
        if rc == 1:
            return YomkResponse(ResStatus.eNo, "key is empty")
        if rc == 2:
            return YomkResponse(ResStatus.eNo, "context monitor function is empty")
        if rc == 3:
            return YomkResponse(ResStatus.eNo, "key is not exist")
        return YomkResponse(ResStatus.eOk, "set context monitor success")

    def set_checker(self, contextChecker):
        if not isinstance(contextChecker, ContextChecker):
            raise TypeError("contextChecker must be ContextChecker")

        group, fail = self.find_group(contextChecker.group)
        if group is None:
            return fail

        # 返回码映射（约定见 ContextGroup.set_checker）：0 成功 / 1 key 为空 / 2 check_func 为空 / 3 key 不存在
        rc = group.set_checker(
            contextChecker.key, contextChecker.check_func)
        if rc == 1:
            return YomkResponse(ResStatus.eNo, "key is empty")
        if rc == 2:
            return YomkResponse(ResStatus.eNo, "checkFunc is empty")
        if rc == 3:
            return YomkResponse(ResStatus.eNo, "key is not exist")
        return YomkResponse(ResStatus.eOk, "set checker success")

    def turn_on_checker(self, pkg):
        group, fail = self.find_group(pkg if isinstance(pkg, str) else "main")
        if group is None:
            return fail
        group.turn_on_checker()
        return YomkResponse(ResStatus.eOk, "turn on checker success")

    def turn_off_checker(self, pkg):
        group, fail = self.find_group(pkg if isinstance(pkg, str) else "main")
        if group is None:
            return fail
        group.turn_off_checker()
        return YomkResponse(ResStatus.eOk, "turn off checker success")

    def create(self, ctx):
        if not isinstance(ctx, Context):
            raise TypeError("ctx must be Context")

        group, fail = self.find_group(ctx.group)
        if group is None:
            return fail

        # 返回码映射（约定见 ContextGroup.create）：0 成功 / 1 key 为空 / 2 value 为空 / 3 key 已存在
        rc = group.create(ctx.key, ctx.value)
        if rc == 1:
            return YomkResponse(ResStatus.eNo, "key is empty")
        if rc == 2:
            return YomkResponse(ResStatus.eNo, "value is empty")
        if rc == 3:
            return YomkResponse(ResStatus.eNo, "key already exists")
        return YomkResponse(ResStatus.eOk, "create context success")

    def destroy(self, groupKey):
        if not isinstance(groupKey, ContextGroupKey):
            raise TypeError("groupKey must be ContextGroupKey")

        group, fail = self.find_group(groupKey.group)
        if group is None:
            return fail

        # 返回码映射（约定见 ContextGroup.destroy）：0 成功 / 1 key 不存在
        rc = group.destroy(groupKey.key)
        if rc != 0:
            return YomkResponse(ResStatus.eNo, "key is not exist")
        return YomkResponse(ResStatus.eOk, "destroy context success")

    def get(self, ctx):
        if not isinstance(ctx, Context):
            raise TypeError("ctx must be Context")

        group, fail = self.find_group(ctx.group)
        if group is None:
            return fail

        # 返回码映射（约定见 ContextGroup.get）：0 命中 / 1 key 为空 / 2 key 不存在；
        # 未命中 value 已为默认值，msg 注明返回默认值（提示 data 非存储值）
        rc, value = group.get(ctx.key, ctx.value)
        if rc == 1:
            return YomkResponse(ResStatus.eNo, "key is empty, return default value.", value)
        if rc == 2:
            return YomkResponse(ResStatus.eNo, "key is not exist, return default value.", value)
        return YomkResponse(ResStatus.eOk, "get context success", value)

    def set(self, ctx):
        if not isinstance(ctx, Context):
            raise TypeError("ctx must be Context")

        group, fail = self.find_group(ctx.group)
        if group is None:
            return fail

        # 返回码映射（约定见 ContextGroup.set）：0 成功 / 1 key 为空 / 2 key 不存在 / 3 类型不匹配 / 4 checker 拒绝
        rc = group.set(ctx.key, ctx.value)
        if rc == 1:
            return YomkResponse(ResStatus.eNo, "key is empty")
        if rc == 2:
            return YomkResponse(ResStatus.eNo, "key is not exist")
        if rc == 3:
            return YomkResponse(ResStatus.eNo, "context type not match")
        if rc == 4:
            return YomkResponse(ResStatus.eNo, "checker reject set context")
        return YomkResponse(ResStatus.eOk, "set context success")

    def groups(self, pkg):
        # 全部组名：main 恒在首位，自定义组按字典序追加（锁内拷出、锁外拼响应）
        groupNames = [self.m_main.name()]
        with self.m_customGroupMutex.gen_rlock():
            groupNames.extend(sorted(self.m_customGroup.keys()))
        return YomkResponse(ResStatus.eOk, "ok", groupNames)

    def keys(self, pkg):
        group, fail = self.find_group(pkg if isinstance(pkg, str) else "main")
        if group is None:
            return fail
        return YomkResponse(ResStatus.eOk, "ok", group.keys())

    def key_info(self, pkg):
        if not isinstance(pkg, ContextGroupKey):
            raise TypeError("pkg must be ContextGroupKey")

        group, fail = self.find_group(pkg.group)
        if group is None:
            return fail

        # 返回码映射（约定见 ContextGroup.key_info）：0 命中 / 1 key 为空 / 2 key 不存在
        rc, info = group.key_info(pkg.key)
        if rc == 1:
            return YomkResponse(ResStatus.eNo, "key is empty")
        if rc == 2:
            return YomkResponse(ResStatus.eNo, "key is not exist")
        return YomkResponse(ResStatus.eOk, "ok", info)

    def list_all(self, pkg):
        # 跨组聚合：组 → 键元信息两层结构（与服务全量内省同构的拓扑范式）；
        # main 组条目恒在首位，自定义组按字典序追加（锁内拷出、锁外取快照）
        groups = [ContextGroupInfo(self.m_main.name(), self.m_main.list_all())]
        with self.m_customGroupMutex.gen_rlock():
            customGroups = [self.m_customGroup[name]
                            for name in sorted(self.m_customGroup.keys())]
        for group in customGroups:
            groups.append(ContextGroupInfo(group.name(), group.list_all()))
        return YomkResponse(ResStatus.eOk, "ok", groups)
