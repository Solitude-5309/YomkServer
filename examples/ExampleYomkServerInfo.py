#!/usr/bin/env python3
"""
ExampleYomkServerInfo —— YomkServerInfo 服务自省快速上手示例（面向初学者）

自省即"运行期问框架"：/YomkServerInfo 是随 YomkApi.init 自动启动的内置服务，
提供服务清单、功能函数清单、单函数元信息与全量拓扑四类查询——调试与运维时
用它确认"框架里现在有什么、每个功能函数期望什么消息类型"。

本示例按 7 个步骤演示全部用法。每一步先打印横幅说明
"接下来做什么、预期看到什么"，再调用门面 API，随后用返回值与自省结果
自证行为——只读运行输出即可明白每个 API 调用发生了什么。

步骤总览：
0. 框架就绪（5 个内置服务随 init 启动，/YomkServerInfo 即其中之一）
1. 注册演示服务（new_service 后清单多一项；request 证明注册即可调用）
2. 函数清单（server_info_functions：两参 vs 三/四参注册的元数据差异 / 空清单分支）
3. 单查元信息（server_info_function 命中契约：msg=期望消息类型名、四参函数说明挂 data 段；4 个 eNo 分支）
4. 全量拓扑（server_info_all：服务行 + 缩进函数行）
5. 运行期实时视图（del_service 后清单与拓扑立即变化）
6. 优雅关闭（shutdown 后自省返回 eInvalid，框架已关闭是普通返回值）

涉及的门面 API（YomkApi）：
  生命周期：init / new_service / request / del_service / shutdown
  自省：    server_info_services / server_info_functions /
            server_info_function / server_info_all
  服务侧：  install_func（三参声明期望消息类型、四参附函数说明，供自省识别）

注：日志/上下文/事件循环/函数池各内置模块另有模块级自省 API
（logger_info_* 等），见 ExampleYomkLogger.py 等模块示例。
"""

import YomkApi


# ---------------------------------------------------------------------------
# 自省素材：一个最小演示服务。两参注册的 /hi 无元数据，四参注册的 /sum
# 声明期望消息类型 SumReq 并附函数说明——差异将由步骤2/3 的自省结果直接呈现。
# ---------------------------------------------------------------------------

class SumReq:
    """加法请求：此处仅作 /sum 的期望消息类型元数据，供自省识别"""

    def __init__(self, a: int, b: int):
        self.a = a
        self.b = b


class InfoDemoService(YomkApi.YomkService):
    def __init__(self, server):
        super().__init__(server)
        # 服务名：URL 前缀，必须 / 开头且全局唯一
        self.set_name("/InfoDemoService")

    def init(self):
        # 两参注册：无元数据；四参注册：声明 /sum 期望 SumReq 并附函数说明
        self.install_func("/hi", self.hi)
        self.install_func("/sum", self.sum, "SumReq", "计算两整数之和")

    # 问好：收到字符串，回显进回包 msg
    def hi(self, pkg) -> YomkApi.YomkResponse:
        print("[InfoDemoService] hi called, got:", pkg)
        return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "hi " + str(pkg))

    # 加法：收 SumReq 消息包，把结果放进回包 msg
    def sum(self, pkg) -> YomkApi.YomkResponse:
        req: SumReq = pkg
        print(f"[InfoDemoService] sum called, a={req.a} b={req.b}")
        return YomkApi.YomkResponse(
            YomkApi.ResStatus.eOk, f"sum = {req.a + req.b}")


# ---------------------------------------------------------------------------
# 辅助输出：横幅与返回值打印。
# 横幅走 print 而非框架日志，保证叙事在任何日志开关状态下可见。
# ---------------------------------------------------------------------------

def print_step(n, title, explain):
    print(f"\n====== 步骤{n}：{title} ======")
    print(f">> {explain}")


def print_resp(prefix, resp):
    # YomkResponse 三态：eOk=0 成功；eNo=1 目标不存在或被拒绝；eInvalid=-1 参数无效或未初始化
    print(f'[{prefix}] status={resp.status.value}, msg="{resp.msg}"')
    # /function 命中时函数说明挂 data 段（字符串），非空即展示；
    # 列表形态 data 由 dump_lines 逐行打印，此处不重复
    if isinstance(resp.data, str):
        print(f'>> data="{resp.data}"')


def dump_lines(prefix, resp):
    # 自省四端点的返回形态：data 为字符串列表（services/functions/all）
    print_resp(prefix, resp)
    if resp.data is None:
        print(">> (no data)")
        return
    for line in resp.data:
        print(f">> | {line}")


def main():
    # 初始化框架（自动启动 5 个内置服务，自省服务 /YomkServerInfo 即其中之一）
    YomkApi.init()

    #
    # 步骤0：框架就绪
    #
    # init 自动启动 5 个内置服务，自省服务 /YomkServerInfo 开箱即用，
    # 无需注册。server_info_services 查到的就是这张运行期服务表。
    #
    print_step(0, "框架就绪",
               "YomkApi.init 启动框架与 5 个内置服务；/YomkServerInfo 即其中之一，自省开箱即用。")
    dump_lines("server_info_services", YomkApi.server_info_services())
    print(">> 清单即内置服务：自省查询的就是框架运行期服务表")

    #
    # 步骤1：注册演示服务
    #
    # new_service 一步注册；注册后清单立即多一项，
    # request /hi 与 /sum 证明注册的服务真的可用。
    #
    print_step(1, "注册演示服务",
               "new_service(InfoDemoService) 后清单多一项；request /hi 与 /sum 证明注册即可调用。")
    new_ret = YomkApi.new_service(InfoDemoService)
    print(f"[new_service(InfoDemoService)] ret={new_ret}")
    dump_lines("server_info_services", YomkApi.server_info_services())
    print_resp("request(hi)", YomkApi.request("/InfoDemoService/hi", "yomk"))
    print_resp("request(sum)", YomkApi.request(
        "/InfoDemoService/sum", SumReq(3, 4)))

    #
    # 步骤2：函数清单
    #
    # server_info_functions 列出指定服务的全部功能函数：四参注册的 /sum 带
    # [SumReq] 元数据，两参注册的 /hi 没有；查不存在的服务返回空清单。
    #
    print_step(2, "函数清单",
               "server_info_functions 看函数清单：/sum [SumReq] 带注册元数据，/hi 无；查不存在的服务返回空清单。")
    dump_lines("server_info_functions(/InfoDemoService)",
               YomkApi.server_info_functions("/InfoDemoService"))
    print(">> [SumReq] 即期望消息类型元数据，由 install_func 第三/四参声明")
    dump_lines("server_info_functions(/GhostService)",
               YomkApi.server_info_functions("/GhostService"))
    print(">> 服务不存在 → 返回空清单（eOk），查询失败不崩溃")

    #
    # 步骤3：单查元信息
    #
    # server_info_function 按 URL 单查：命中时 msg 精确返回期望消息类型名
    # （无元数据为空串），四参函数说明挂 data 段；4 种坏 URL 均返回 eNo，
    # 是普通返回值不崩溃。
    #
    print_step(3, "单查元信息",
               "server_info_function 命中时 msg=期望消息类型名（/sum 为 SumReq，/hi 为空串），函数说明挂 data 段；4 种坏 URL 均为 eNo=1。")
    print_resp("server_info_function(/sum)",
               YomkApi.server_info_function("/InfoDemoService/sum"))
    print_resp("server_info_function(/hi)",
               YomkApi.server_info_function("/InfoDemoService/hi"))
    print(">> 命中契约：msg 即该函数期望的消息类型名，空串表示未声明；函数说明经 data 段透出")
    print_resp("server_info_function(url不以/开头)",
               YomkApi.server_info_function("InfoDemoService/sum"))
    print_resp("server_info_function(缺函数名)",
               YomkApi.server_info_function("/InfoDemoService"))
    print_resp("server_info_function(服务不存在)",
               YomkApi.server_info_function("/GhostService/hi"))
    print_resp("server_info_function(函数不存在)",
               YomkApi.server_info_function("/InfoDemoService/ghost"))
    print(">> URL 解析失败 / 目标不存在均为 eNo=1，调用失败是普通返回值")

    #
    # 步骤4：全量拓扑
    #
    # server_info_all 一次拉取全部服务及其功能函数：服务名一行、函数行缩进两格。
    #
    print_step(4, "全量拓扑",
               "server_info_all 输出全部服务及各自的功能函数：服务名一行，函数行缩进两格。")
    dump_lines("server_info_all", YomkApi.server_info_all())
    print(">> 6 个服务 = 5 个内置 + 1 个演示；排障时一眼看清调用拓扑")

    #
    # 步骤5：运行期实时视图
    #
    # 自省读的是运行期服务表：del_service 后清单与拓扑立即少一项，
    # 无需重启或刷新——注册/删除即时反映到下一次查询。
    #
    print_step(5, "运行期实时视图",
               "del_service 删除演示服务后，清单与拓扑立即少一项——自省是实时视图。")
    del_ret = YomkApi.del_service("/InfoDemoService")
    print(f"[del_service(/InfoDemoService)] ret={del_ret}")
    dump_lines("server_info_services", YomkApi.server_info_services())
    dump_lines("server_info_all(终态)", YomkApi.server_info_all())
    print(">> 删除即从拓扑消失，剩下的 5 项即内置服务")

    #
    # 步骤6：优雅关闭
    #
    # shutdown 后框架已关闭，任何自省调用返回 eInvalid=-1（普通返回值）。
    #
    print_step(6, "优雅关闭",
               "shutdown 后自省返回 eInvalid=-1：框架已关闭是普通返回值，不崩溃。")
    YomkApi.shutdown()
    print(">> YomkApi.shutdown() 完成：排空在途请求并释放全部服务（幂等）")
    print_resp("server_info_services(关闭后)", YomkApi.server_info_services())

    print("\n====== 示例结束：按回车退出 ======")
    input()


if __name__ == "__main__":
    main()
