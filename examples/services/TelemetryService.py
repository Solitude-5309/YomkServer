"""
留痕服务：被其他服务调用的最小服务样例（机械臂运行黑匣子）

定义一个服务只需三步：
1. 继承 YomkService，构造函数中 set_name("/TelemetryService") 起名
   （必须 / 开头、全局唯一，作为该服务所有功能函数的 URL 前缀）
2. 实现 init()，用 install_func 注册功能函数
3. 功能函数签名统一为 YomkResponse(pkg)

本服务额外演示"删除即停"安全：init 把成员函数注册到 FunctionPool
供跨服务按名直调，服务删除/替换时由 deinit 显式注销——python 无弱绑定
机制，显式注销承担同等失效语义，钩子不会悬垂。
"""

import YomkApi

from typing import Any


class TelemetryService(YomkApi.YomkService):
    instance_count = 0  # 实例计数：构造 +1、析构 -1

    def __init__(self, server):
        super().__init__(server)
        TelemetryService.instance_count += 1
        self.instance_id = TelemetryService.instance_count
        # 服务名：URL 前缀，全局唯一
        self.set_name("/TelemetryService")
        YomkApi.console_log_info_tag(
            "arm.telemetry", f"留痕服务实例 #{self.instance_id} 创建")

    def __del__(self):
        TelemetryService.instance_count -= 1
        try:
            # 框架已关闭时日志门面静默，此处直打会破坏"收班断电静默"叙事——仅框架存活时打
            if YomkApi.server_instance() is not None:
                YomkApi.console_log_info_tag(
                    "arm.telemetry", f"留痕服务实例 #{self.instance_id} 析构")
        except Exception:
            pass

    def init(self):
        # 注册功能函数：注册后即可通过 URL /TelemetryService/record 调用
        self.install_func("/record", self.record)

        # 注册到 FunctionPool 供按名直调：服务删除/替换时由 deinit 显式注销承担失效语义
        YomkApi.function_pool_register("telemetry_hook", self.record)

        YomkApi.console_log_info_tag(
            "arm.telemetry", "init: install /record, register telemetry_hook")
        return 0

    def deinit(self):
        # 显式注销函数池钩子：删除/替换后 telemetry_hook 立即失效，不悬垂
        YomkApi.function_pool_unregister("telemetry_hook")
        YomkApi.console_log_info_tag("arm.telemetry", "deinit: telemetry service offline")

    # 留痕功能函数：URL 为 /TelemetryService/record，记录一条运动留痕并回执
    def record(self, pkg: Any) -> YomkApi.YomkResponse:
        if not isinstance(pkg, str):
            return YomkApi.YomkResponse(
                YomkApi.ResStatus.eNo, " pkg is null or pkg is not String. ")
        YomkApi.console_log_info_tag(
            "arm.telemetry", f"留痕记录 [实例#{self.instance_id}]：{pkg}")
        return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "trace recorded")
