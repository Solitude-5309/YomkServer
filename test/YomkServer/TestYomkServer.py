"""
TestYomkServer.py —— YomkServer URL 解析路由与服务增删改查白盒测试（正式全面测试）

覆盖内容：
1. URL 解析全边界（经公开 request 路径）：空串/无 / 开头/无第二 /、多段路径语义、
   合法命中与 pkg 透传端到端、超长 URL；"srv is empty"与"function name is empty"
   两分支不可达（url[0]=='/' 且 posEnd>=1 保证 srvName 至少含 /、funcName 以 /
   开头必非空），书面豁免
2. async_request 解析边界：非法 url 回调永不触发；合法 url 回调到达；callback
   为空分支（请求仍被执行，fire-and-forget）
3. add_service 守卫与回滚：None、init 返回 -1、init 抛 MemoryError/RuntimeError
   （异常视同失败统一回滚，无半初始化残留、进程不终止）
4. 同名替换：旧服务先清理后新服务 init、表内唯一；替换 + 新服务 init 失败 ->
   旧不恢复、表为空（语义文档化）
5. del_service：不存在 -1、删除 0、重复删除 -1
6. 内省：service_names 随增删变化、service_func_infos 命中与元数据、不存在服务
   返回空表
7. new_service：有名注册可路由；无名以空名注册（现状文档化：URL 不可达，不修复
   仅记录）
8. start_service：空表、不支持名 continue、内置 creator 真实路径

剔除与差异说明（语言机制映射，非功能缺口）：
- main 第 4/5 节 deletedOnDeinit 断言剔除：deleted/markDeleted 弱绑定判活为语言
  机制剔除项（python 功能函数为直接引用，无弱绑定包装层）
- main 第 5 节删除即停端到端（全局弱绑定回调丢弃）剔除：python 无
  YomkBindWeakSelf 对应物
- main 第 8 节内置 creator 真实路径断言对齐（/YomkServerInfo）；python 内置服务映射
  表已随 Logger 模块轮补齐 5/5（/YomkContext、/YomkFunctionPool、/YomkEventLoop、
  /YomkLogger、/YomkServerInfo）
- 解包宏 -> 普通类属性访问（DemoPkg 载体）；promise/future -> Event + 容器

说明：全程不触碰 shutdown/YomkApi.init（服务器实例独立，每节 YomkServer.create(1)，
节首重置全局观测变量）；排空/幂等/拒新语义归后续 shutdown 序列测试

风格：纯 main() + 失败计数，返回非 0 表示存在失败用例（零第三方依赖）
"""

import sys
import threading
import time

from YomkServerPy import ResStatus, YomkResponse, YomkServer, YomkService

g_failed = 0


def check(cond, msg):
    global g_failed
    line = sys._getframe(1).f_lineno
    if cond:
        print(f"[ OK ] [line {line}] {msg}")
    else:
        print(f"[FAIL] [line {line}] {msg}")
        g_failed += 1


# ---- 文件级观测变量：init/deinit 次序与状态、注入开关（均为单线程节内使用） ----
g_init_count = 0    # init 调用次数
g_deinit_count = 0  # deinit 调用次数
g_ping_count = 0    # /ping 真实执行次数
g_init_ret = 0      # init 返回值注入：0 成功 / 非 0 失败
g_init_throw = 0    # init 异常注入：0 不抛 / 1 MemoryError / 2 RuntimeError


def reset_globals():
    global g_init_count, g_deinit_count, g_ping_count, g_init_ret, g_init_throw
    g_init_count = 0
    g_deinit_count = 0
    g_ping_count = 0
    g_init_ret = 0
    g_init_throw = 0


def name_in(names, srv_name):
    return srv_name in names


def name_count(names, srv_name):
    return names.count(srv_name)


class DemoPkg:
    """演示消息包：python 消息包为普通类（main 标准 String 包的对应载体）"""

    def __init__(self, d=""):
        self.d = d


class RouteSrv(YomkService):
    """路由演示服务：/ping 固定响应、/echo 回显；init/deinit 记录观测变量"""

    def init(self):
        global g_init_count
        g_init_count += 1
        if g_init_throw == 1:
            raise MemoryError("init failed on purpose")
        if g_init_throw == 2:
            raise RuntimeError("init failed on purpose")
        if g_init_ret != 0:
            return g_init_ret  # 既有回滚路径：init 返回非 0
        self.install_func("/ping", self.ping, "String")
        self.install_func("/echo", self.echo, "String")
        return 0

    def deinit(self):
        global g_deinit_count
        g_deinit_count += 1

    def ping(self, pkg):
        global g_ping_count
        g_ping_count += 1
        return YomkResponse(ResStatus.eOk, "pong")

    def echo(self, pkg):
        # main 的 String 解包回显（解包宏剔除）：pkg 为空时函数内守卫返回 eNo
        if not pkg:
            return YomkResponse(ResStatus.eNo, "pkg is null")
        return YomkResponse(ResStatus.eOk, "echo:" + pkg.d)


# 1. URL 解析全边界与路由（经公开 request 路径）
def test_url_parse_boundaries():
    server = YomkServer.create(1)
    reset_globals()
    check(server.new_service(RouteSrv, "/RouteSrv") == 0, "注册路由演示服务成功")

    # 空串 / 无 / 开头：please start with / 契约
    resp = server.request("")
    check(resp.status == ResStatus.eNo and "please start with /" in resp.msg,
          "request 空 url 返回 eNo + please start with /")
    resp = server.request("no_slash/ping")
    check(resp.status == ResStatus.eNo and "please start with /" in resp.msg,
          "request 无 / 开头 url 返回 eNo + please start with /")

    # 无第二 /：not found service name 契约
    resp = server.request("/")
    check(resp.status == ResStatus.eNo and "not found service name" in resp.msg,
          "request \"/\" 返回 eNo + not found service name")
    resp = server.request("/Only")
    check(resp.status == ResStatus.eNo and "not found service name" in resp.msg,
          "request \"/Only\"（无第二 /）返回 eNo + not found service name")

    # 多段路径：srv_name="/RouteSrv"、func_name="/b/c" 未安装 -> not found（语义现状）
    resp = server.request("/RouteSrv/b/c")
    check(resp.status == ResStatus.eNo and "not found" in resp.msg,
          "request 多段路径 func_name=\"/b/c\" 未安装返回 eNo + not found")

    # 服务未注册：service not found 契约
    resp = server.request("/NoSuch/ping")
    check(resp.status == ResStatus.eNo and "service not found" in resp.msg,
          "request 未注册服务返回 eNo + service not found")

    # 合法命中 + pkg 透传端到端
    resp = server.request("/RouteSrv/ping")
    check(resp.status == ResStatus.eOk and resp.msg == "pong",
          "request 合法 url 命中返回 eOk + pong")
    resp = server.request("/RouteSrv/echo", DemoPkg("hi"))
    check(resp.status == ResStatus.eOk and resp.msg == "echo:hi",
          "request 携 pkg 路由透传，回显往返正确")

    # 超长 URL（4096 级服务名）：不崩溃，走 service not found
    resp = server.request("/" + "x" * 4096 + "/ping")
    check(resp.status == ResStatus.eNo and "service not found" in resp.msg,
          "request 超长 url 不崩溃，返回 eNo + service not found")


# 2. async_request 解析边界与到达（排空/关闭语义归 shutdown 序列测试）
def test_async_request_parse():
    server = YomkServer.create(1)
    reset_globals()
    check(server.new_service(RouteSrv, "/RouteSrv") == 0, "异步用例注册路由演示服务成功")

    # 非法 url：回调永不触发（解析失败直接丢弃）
    bad_cb_count = 0

    def on_bad(resp):
        nonlocal bad_cb_count
        bad_cb_count += 1

    server.async_request("bad_url", None, on_bad)
    time.sleep(0.2)
    check(bad_cb_count == 0, "async_request 非法 url：回调永不触发")

    # 合法 url：回调到达并收到 eOk（Event + 容器 + 超时保护）
    result_holder = {}
    arrived = threading.Event()

    def on_ok(resp):
        result_holder["resp"] = resp
        arrived.set()

    server.async_request("/RouteSrv/ping", None, on_ok)
    ready = arrived.wait(2.0)
    check(ready, "async_request 合法 url：回调在超时窗口内到达")
    if ready:
        resp = result_holder["resp"]
        check(resp.status == ResStatus.eOk and resp.msg == "pong",
              "async_request 回调收到 eOk + pong")

    # callback 为空分支：请求仍被执行（fire-and-forget）
    ping_before = g_ping_count
    server.async_request("/RouteSrv/ping")
    time.sleep(0.2)
    check(g_ping_count > ping_before, "async_request callback 为空：请求仍被执行")


# 3. add_service 守卫与回滚（含 init 异常回滚验证）
def test_add_service_guard_and_rollback():
    global g_init_ret, g_init_throw
    server = YomkServer.create(1)
    reset_globals()

    check(server.add_service(None) == -1, "add_service(None) 返回 -1")

    # init 返回 -1：既有回滚路径
    g_init_ret = -1
    fail_ret_srv = RouteSrv(server)
    fail_ret_srv.set_name("/FailRet")
    check(server.add_service(fail_ret_srv)
          == -1, "init 返回 -1：add_service 返回 -1")
    check(not name_in(server.service_names(), "/FailRet"), "init 返回 -1：回滚后服务表无残留")
    check(g_deinit_count == 1, "init 返回 -1：回滚路径触发 deinit")

    # init 抛 MemoryError：异常视同失败，统一回滚
    g_init_ret = 0
    g_init_throw = 1
    mem_err_srv = RouteSrv(server)
    mem_err_srv.set_name("/ThrowMemErr")
    check(server.add_service(mem_err_srv) == -1,
          "init 抛 MemoryError：add_service 返回 -1（异常不穿透，进程不终止）")
    check(not name_in(server.service_names(), "/ThrowMemErr"),
          "init 抛 MemoryError：无半初始化残留")

    # init 抛 RuntimeError：同上
    g_init_throw = 2
    runtime_srv = RouteSrv(server)
    runtime_srv.set_name("/ThrowRuntime")
    check(server.add_service(runtime_srv) == -1,
          "init 抛 RuntimeError：add_service 返回 -1（异常不穿透）")
    check(not name_in(server.service_names(), "/ThrowRuntime"),
          "init 抛 RuntimeError：无半初始化残留")
    g_init_throw = 0

    check(g_deinit_count == 3, "三次失败注册均触发回滚 deinit（累计 3 次）")


# 4. 同名替换：先删后立承诺 + 替换失败语义文档化
def test_replace_same_name():
    global g_init_ret
    server = YomkServer.create(1)
    reset_globals()
    check(server.new_service(RouteSrv, "/Dup") == 0, "同名替换：首次注册成功")

    init_before = g_init_count
    check(server.new_service(RouteSrv, "/Dup") == 0, "同名第二实例注册（替换）返回 0")
    check(g_init_count == init_before + 1, "替换：新服务 init 在旧服务清理后被调用")
    check(g_deinit_count == 1, "替换：旧服务 deinit 被调用")
    # main 此处断言旧服务 deinit 时 deleted() 已为 true（先 markDeleted 承诺实证）——
    # python 无 deleted/markDeleted（语言机制剔除项），断言剔除

    resp = server.request("/Dup/ping")
    check(resp.status == ResStatus.eOk and resp.msg == "pong", "替换：新服务可路由")
    check(name_count(server.service_names(), "/Dup")
          == 1, "替换：service_names 中该名仅一条")

    # 替换 + 新服务 init 失败：旧已删、新回滚、表为空（语义文档化：不恢复旧服务）
    g_init_ret = -1
    bad_srv = RouteSrv(server)
    bad_srv.set_name("/Dup")
    check(server.add_service(bad_srv) == -1, "替换 + 新服务 init 失败：返回 -1")
    check(not name_in(server.service_names(), "/Dup"), "替换失败后该名为空（旧服务不恢复，语义文档化）")
    resp = server.request("/Dup/ping")
    check(resp.status == ResStatus.eNo and "service not found" in resp.msg,
          "替换失败后请求返回 eNo + service not found")
    g_init_ret = 0


# 5. del_service：返回值契约
def test_del_service():
    server = YomkServer.create(1)
    reset_globals()

    check(server.del_service("/NotExist") == -1, "del_service 不存在服务返回 -1")
    check(server.new_service(RouteSrv, "/Del") == 0, "删除用例注册服务成功")
    # main 此处断言 init 时已存下全局弱绑定回调——python 无 YomkBindWeakSelf，剔除

    check(server.del_service("/Del") == 0, "del_service 已存在服务返回 0")
    check(g_deinit_count == 1, "del_service 触发 deinit（锁外清理）")
    # main 此处断言 markDeleted 先于 deinit——python 无 deleted/markDeleted，剔除

    resp = server.request("/Del/ping")
    check(resp.status == ResStatus.eNo and "service not found" in resp.msg,
          "删除后请求返回 eNo + service not found")
    check(not name_in(server.service_names(), "/Del"), "删除后 service_names 不含该名")
    check(server.del_service("/Del") == -1, "重复删除返回 -1")

    # main 删除即停端到端（弱绑定回调丢弃）——python 无弱绑定包装层，剔除


# 6. 内省接口：service_names / service_func_infos
def test_introspection():
    server = YomkServer.create(1)
    reset_globals()
    server.new_service(RouteSrv, "/RouteSrv")

    check(name_in(server.service_names(), "/RouteSrv"), "service_names 反映注册")

    infos = server.service_func_infos("/RouteSrv")
    check("/ping" in infos and "/echo" in infos,
          "service_func_infos 命中 /ping 与 /echo")
    check(infos["/ping"].func_name == "/ping" and infos["/ping"].msg_name == "String"
          and infos["/echo"].msg_name == "String",
          "service_func_infos 元数据正确（func_name/msg_name）")
    check(server.service_func_infos("/NoSuch")
          == {}, "service_func_infos 不存在服务返回空表")

    server.del_service("/RouteSrv")
    check(not name_in(server.service_names(), "/RouteSrv"), "service_names 反映删除")


# 7. new_service：有名注册 + 无名空名注册（现状文档化）
def test_new_service_template():
    server = YomkServer.create(1)
    reset_globals()

    check(server.new_service(RouteSrv, "/Tpl") == 0, "new_service 有名注册返回 0")
    check(server.request("/Tpl/ping").status ==
          ResStatus.eOk, "new_service 有名注册可路由")

    # 无名：以空名注册。URL 解析保证 srv_name 至少含 /，空名服务永不可达——现状文档化，不修复仅记录
    check(server.new_service(RouteSrv) == 0, "new_service 无名返回 0（以空名注册，现状文档化）")
    check(name_in(server.service_names(), ""),
          "service_names 含空串（空名服务 URL 不可达，现状文档化）")


# 8. start_service：空表 / 不支持名 continue / 内置 creator 真实路径
def test_start_service():
    server = YomkServer.create(1)
    reset_globals()

    check(server.start_service([]) == 0, "start_service 空表返回 0")
    check(server.start_service(["/NoSuchBuiltin"])
          == 0, "start_service 不支持名返回 0（continue 分支）")
    check(not name_in(server.service_names(),
          "/NoSuchBuiltin"), "start_service 不支持名不入表")
    check(server.start_service(["/YomkServerInfo"])
          == 0, "start_service 内置服务返回 0")
    check(name_in(server.service_names(), "/YomkServerInfo"),
          "内置 creator 真实路径：/YomkServerInfo 入表")


def main():
    test_url_parse_boundaries()
    test_async_request_parse()
    test_add_service_guard_and_rollback()
    test_replace_same_name()
    test_del_service()
    test_introspection()
    test_new_service_template()
    test_start_service()

    if g_failed > 0:
        print(f"TestYomkServer failed, count: {g_failed}")
        return 1
    print("TestYomkServer all check passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
