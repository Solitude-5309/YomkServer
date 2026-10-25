"""
运动服务：机械臂运动控制的业务服务样例

- /echo：通讯自检回环（2 参注册，无元数据）
- /move ：4 参注册声明 MoveCmd 元数据与函数说明；内部用成员 request()
          跨服务调用 /TelemetryService/record 留痕，演示调用链与失败传播

服务内部调用其他服务用成员函数 request("/服务名/函数名", pkg)，
与全局 YomkApi.request() 等价，但无需获取全局单例。
"""

import YomkApi

from msgs.ArmMsgs import MoveCmd, MoveResp


class MotionService(YomkApi.YomkService):
    def __init__(self, server):
        super().__init__(server)
        # 服务名：URL 前缀，全局唯一
        self.set_name("/MotionService")

    def init(self):
        # 2 参注册：无消息元数据；4 参注册：声明 /move 期望 MoveCmd 并附函数说明，供自省识别
        self.install_func("/echo", self.echo)
        self.install_func("/move", self.move, "MoveCmd", "运动控制：按指令运动到目标位姿并留痕")
        YomkApi.console_log_info_tag(
            "arm.motion", "init: install /echo (no meta), /move [MoveCmd] + description")
        return 0

    # 通讯自检：收到字符串原样回显
    def echo(self, pkg) -> YomkApi.YomkResponse:
        if not isinstance(pkg, str):
            return YomkApi.YomkResponse(
                YomkApi.ResStatus.eNo, " pkg is null or pkg is not String. ")
        YomkApi.console_log_info_tag("arm.motion", f'通讯自检回环：收到 "{pkg}"')
        return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "echo success", "echo: " + pkg)

    # 运动指令：先跨服务留痕，再返回 MoveResp 强类型到位回执
    def move(self, pkg) -> YomkApi.YomkResponse:
        if not isinstance(pkg, MoveCmd):
            return YomkApi.YomkResponse(
                YomkApi.ResStatus.eNo, " pkg is null or pkg is not MoveCmd. ")
        YomkApi.console_log_info_tag(
            "arm.motion",
            f"运动指令执行：目标 ({pkg.x}, {pkg.y}, {pkg.z})mm，倍率 {pkg.speed_ratio}")

        # 跨服务调用：成员 request() 与全局 YomkApi.request 等价，URL 直达留痕工位；
        # 留痕是运动的依赖前置，留痕缺失时失败沿调用链传播
        trace_resp = self.request(
            "/TelemetryService/record",
            f"运动留痕：move -> ({pkg.x},{pkg.y},{pkg.z})mm")
        if trace_resp.status != YomkApi.ResStatus.eOk:
            YomkApi.console_log_info_tag("arm.motion", "留痕失败：" + trace_resp.msg)
            return YomkApi.YomkResponse(YomkApi.ResStatus.eNo, "move failed: " + trace_resp.msg)
        YomkApi.console_log_info_tag("arm.motion", "留痕回执：" + trace_resp.msg)

        # 到位回执：data 携带强类型 MoveResp，不止能传 String
        return YomkApi.YomkResponse(
            YomkApi.ResStatus.eOk, "move success", MoveResp(pkg.x, pkg.y, pkg.z))
