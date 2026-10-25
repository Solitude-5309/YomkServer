"""
机械臂控制示例消息包定义

服务间传递的数据结构在此集中定义：普通类即可作为框架内传输的消息包
（python 版对象直传，无需注册宏；类型识别名经 type(value).__name__ 供自省识别）。
"""


class MoveCmd:
    # 运动指令：末端目标位姿与速度倍率
    def __init__(self, x=0.0, y=0.0, z=0.0, speed_ratio=0.0):
        self.x = x                      # 目标 X（mm）
        self.y = y                      # 目标 Y（mm）
        self.z = z                      # 目标 Z（mm）
        self.speed_ratio = speed_ratio  # 速度倍率 0.0~1.0


class MoveResp:
    # 运动回执：到位反馈（演示回包 data 可传自定义强类型，不局限于 String）
    def __init__(self, reach_x=0.0, reach_y=0.0, reach_z=0.0):
        self.reach_x = reach_x  # 到位 X（mm）
        self.reach_y = reach_y  # 到位 Y（mm）
        self.reach_z = reach_z  # 到位 Z（mm）
