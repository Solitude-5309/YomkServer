"""
TestYomkService.py —— YomkService 命名/函数注册/调用白盒单元测试（正式全面测试）

覆盖内容：
1. 命名：注册前设置/读取、空名拒绝
2. 注册锁定：add_service 成功后改名被拒（名字与注册键一致承诺）
3. add_service 两参形式：注册前改名生效
4. install_func：合法名、非法名（空串/无 / 开头）拒绝、空函数拒绝、
   三参元数据（msg_name）、两参覆盖清除残留元数据、三参覆盖恢复
5. func_infos 内省
6. invoke：命中（自身响应 + pkg 透传回显）、非法名/未安装返回 eNo 契约消息
7. 空服务器守卫：YomkService(None) 构造后不崩溃 + request/async_request 契约

语言机制说明（非功能缺口）：
- deleted/markDeleted 弱绑定判活不适用：python 功能函数为直接引用，无弱绑定包装层
- python 消息包为普通类，"pkg 为空解包失败返回 eNo" 的守卫
  由测试函数内显式判断承载（消息契约："pkg is null"）
- 空服务器守卫形态：python 无转发层，set_name/install_func/func_infos/invoke 直接作用于本地
  字段/表，仅 request/async_request 保留 server 判空契约。
  本文件第 7 节按 python 现状文档化断言
- invoke 的 pkg 参数为必填，测试显式传 None

风格：纯 main() + 失败计数，返回非 0 表示存在失败用例（零第三方依赖）
"""

import sys

from YomkServerPy import ResStatus, YomkResponse, YomkService
import YomkApi

g_failed = 0


def check(cond, msg):
    global g_failed
    line = sys._getframe(1).f_lineno
    if cond:
        print(f"[ OK ] [line {line}] {msg}")
    else:
        print(f"[FAIL] [line {line}] {msg}")
        g_failed += 1


def find_func_info(infos, func_name):
    """内省辅助：在 func_infos 中查找 func_name，返回 (found, msg_name)"""
    info = infos.get(func_name)
    if info is None:
        return False, ""
    return True, info.msg_name


class DemoPkg:
    """演示消息包：python 消息包为普通类"""

    def __init__(self, d=""):
        self.d = d


class DemoService(YomkService):
    """主演示服务：注册/调用/内省用例载体"""

    def __init__(self, server):
        super().__init__(server)
        self.set_name("/DemoService")

    def init(self):
        # 三参：内省元数据 String
        self.install_func("/echo", self.echo, "String")
        self.install_func("/plain", self.plain)                    # 两参：无元数据
        self.install_func("/reinstall", self.reinstall, "String")  # 先三参
        # 两参覆盖：清残留元数据
        self.install_func("/reinstall", self.reinstall)
        self.install_func("/restore", self.restore)                # 先两参
        self.install_func("/restore", self.restore, "String")      # 三参覆盖：恢复元数据
        return 0

    def echo(self, pkg):
        # String 解包回显：pkg 为空时函数内守卫返回 eNo
        if not pkg:
            return YomkResponse(ResStatus.eNo, "pkg is null")
        return YomkResponse(ResStatus.eOk, "echo:" + pkg.d)

    def plain(self, pkg):
        return YomkResponse(ResStatus.eOk, "plain")

    def reinstall(self, pkg):
        return YomkResponse(ResStatus.eOk, "reinstall")

    def restore(self, pkg):
        return YomkResponse(ResStatus.eOk, "restore")


class NullSrv(YomkService):
    """空服务器服务：以 None 构造，验证空守卫分支（不注册）"""

    def __init__(self):
        super().__init__(None)

    def init(self):
        return 0


# 1. 命名：注册前设置/读取一致、空名拒绝
def test_name_before_register(demo):
    check(demo.get_name() == "/DemoService", "注册前 get_name 读取与构造设置一致")
    demo.set_name("")  # 空名设置应被拒绝
    check(demo.get_name() == "/DemoService", "空名设置被拒，名字保持不变")


# 2. 注册与注册锁定：add_service 成功返回 0，注册后改名被拒
def test_register_and_lock_name(demo):
    check(YomkApi.add_service(demo) == 0, "add_service 注册成功返回 0")
    demo.set_name("/NewName")  # 注册后改名应被拒绝
    check(demo.get_name() == "/DemoService", "注册后改名被拒，名字与注册键一致（锁名承诺）")


# 3. add_service 两参形式：注册前改名生效
def test_add_service_with_name():
    renamed = DemoService(YomkApi.server_instance())
    check(YomkApi.add_service(renamed, "/RenamedDemo")
          == 0, "两参 add_service 注册成功返回 0")
    check(renamed.get_name() == "/RenamedDemo", "注册前经两参形式改名生效")


# 4. install_func：正常/非法名/空函数 + 三参元数据 + 覆盖安装元数据清除/恢复
def test_install_func(demo):
    # 正常路径：运行时追加安装合法名
    demo.install_func(
        "/manual", lambda pkg: YomkResponse(ResStatus.eOk, "manual"))

    # 非法名：空串与无 / 开头均被拒
    demo.install_func("", lambda pkg: YomkResponse(ResStatus.eOk, ""))
    demo.install_func("no_slash", lambda pkg: YomkResponse(ResStatus.eOk, ""))

    # 空函数对象：安装被拒
    demo.install_func("/null_func", None)

    infos = demo.func_infos()

    found, _ = find_func_info(infos, "/manual")
    check(found, "合法 / 开头名安装成功，func_infos 命中")
    found, _ = find_func_info(infos, "")
    check(not found, "空串函数名安装被拒，func_infos 不含")
    found, _ = find_func_info(infos, "no_slash")
    check(not found, "无 / 开头函数名安装被拒，func_infos 不含")
    found, _ = find_func_info(infos, "/null_func")
    check(not found, "空函数对象安装被拒，func_infos 不含")

    # 三参元数据与覆盖安装
    found, msg_name = find_func_info(infos, "/echo")
    check(found and msg_name == "String", "三参安装声明元数据 msg_name == String")
    found, msg_name = find_func_info(infos, "/plain")
    check(found and msg_name == "", "两参安装无元数据")
    found, msg_name = find_func_info(infos, "/reinstall")
    check(found and msg_name == "", "两参覆盖安装清除残留元数据")
    found, msg_name = find_func_info(infos, "/restore")
    check(found and msg_name == "String", "三参覆盖安装恢复元数据")


# 5. invoke：命中（含 pkg 透传）与异常路径契约
def test_invoke(demo):
    # 命中：返回函数自身响应
    resp = demo.invoke("/plain", None)
    check(resp.status == ResStatus.eOk and resp.msg == "plain",
          "invoke 命中，返回函数自身响应")

    # 命中：pkg 透传与回显（数据往返）
    resp = demo.invoke("/echo", DemoPkg("hi"))
    check(resp.status == ResStatus.eOk and resp.msg == "echo:hi",
          "invoke 命中，pkg 透传回显正确")

    # 命中：pkg 为空，函数内解包守卫返回 eNo
    resp = demo.invoke("/echo", None)
    check(resp.status == ResStatus.eNo and "pkg is null" in resp.msg,
          "invoke 命中但 pkg 为空，函数解包守卫返回 eNo")

    # 异常路径：空名/无 / 开头返回 eNo + parse error 契约消息
    resp = demo.invoke("", None)
    check(resp.status == ResStatus.eNo and "parse error" in resp.msg,
          "invoke 空函数名返回 eNo 且 msg 含 parse error")
    resp = demo.invoke("no_slash", None)
    check(resp.status == ResStatus.eNo and "parse error" in resp.msg,
          "invoke 无 / 开头函数名返回 eNo 且 msg 含 parse error")

    # 异常路径：未安装函数返回 eNo + not found 契约消息
    resp = demo.invoke("/not_exist", None)
    check(resp.status == ResStatus.eNo and "not found" in resp.msg,
          "invoke 未安装函数返回 eNo 且 msg 含 not found")

    # 空函数被拒后按未安装处理
    resp = demo.invoke("/null_func", None)
    check(resp.status == ResStatus.eNo and "not found" in resp.msg,
          "invoke 被拒安装的空函数按未安装返回 eNo")


# 6. 空服务器守卫（python 现状文档化，见文件头差异说明）
def test_null_server_guards():
    srv = NullSrv()
    srv.set_name("x")                                    # 不应崩溃（本地字段直接生效）
    srv.install_func("/x", lambda pkg: YomkResponse())   # 不应崩溃（本地表直接生效）
    ret = srv.async_request("/SomeService/func")         # 不应崩溃（静默）

    check(srv.get_name() == "x",
          "空 server 下 set_name 本地字段直接生效（现状文档化）")
    found, _ = find_func_info(srv.func_infos(), "/x")
    check(found,
          "空 server 下 install_func 本地表直接生效（现状文档化）")
    check(ret is None, "空 server 下 async_request 静默返回 None")

    resp = srv.request("/SomeService/func")
    check(resp.status == ResStatus.eNo and resp.msg == "service is null",
          "空 server 下 request 返回 eNo + service is null")

    resp = srv.invoke("/x", None)
    check(resp.status == ResStatus.eInvalid,
          "空 server 下 invoke 执行本地函数返回其响应（现状文档化）")


def main():
    YomkApi.init()

    demo = DemoService(YomkApi.server_instance())
    test_name_before_register(demo)
    test_register_and_lock_name(demo)
    test_add_service_with_name()
    test_install_func(demo)
    test_invoke(demo)
    test_null_server_guards()

    if g_failed > 0:
        print(f"TestYomkService failed, count: {g_failed}")
        return 1
    print("TestYomkService all check passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
