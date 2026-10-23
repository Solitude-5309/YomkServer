"""
TestYomkServerInfoLifecycle.py —— ServerInfo 模块生命周期与四端点功能基线测试

（8 节，从零新建——模块此前零专属覆盖）：
S0. server destroyed 分支（白盒直连）：测试自持 YomkServerInfo 实例（不经 add_service），
    server 释放后 4 端点 invoke 逐一返回 eNo + "server has been destroyed"
    （m_weakServer 判活失败分支，invoke 为 YomkService public 方法可直连）
S1. /services：eOk + "ok" + 服务名列表含 4 内置服务；FmtSrv 注册后出现、删除后消失、重注册后出现
S2. /functions：行格式 "func [MsgType]" 与无类型 "func" 两种；空 srvName eInvalid；
    不存在服务 eOk 空表；内置 /YomkContext 与 /YomkServerInfo 自身行格式
S3. /function：命中时 msg 精确等于 msgName（特殊契约，非 "ok"）、无类型函数 msg 为空；
    四参函数说明挂入 data 段（msg 契约保持）、无说明函数 data 为空；
    URL 解析边界（空串/无 / 开头/无第二个 /）；服务不存在/函数不存在契约消息；null pkg
S4. /all：服务名行 + 两空格缩进功能行（字典序，FmtSrv 段相邻三行精确序）
S5. 轻量并发快照一致性：3 churn 线程 add/del × 3 内省线程轮询 4 端点（服务表读写竞争），
    恒 eOk、/services 恒含存活服务、churn 后无残留
S6. 内置服务删除/重注册自举契约（对齐 Context Lifecycle S4/S5 模式）：删除后 4 端点不可路由、
    服务表快照反映删除；new_service 无参重注册（构造函数固定名生效）后自举恢复
S7. 超长名内省边界（65536 字符服务名，对齐 FPC1-S8）：/services 行完整、/functions 行格式、
    /function 长名下 URL 二段解析、/all 缩进行完整无截断

语言机制说明：
- S1 断言 5 内置服务
  （/YomkFunctionPool /YomkContext /YomkEventLoop /YomkLogger /YomkServerInfo）
- server 判活用 weakref.ref：YomkServerInfo 构造时清除
  基类对 server 的强引用仅持弱引用；测试侧 del server 后补 gc.collect() 显式触发回收
  （CPython 引用计数遇环不归零）
- 服务表快照经 YomkApi.server().service_names()
- 内省数据解包后 YomkResponse.data 直接是 list[str]
- install_func 第三参承载函数消息类型元数据，第四参承载函数说明（三内置装配已补齐）
- 探针计数用 list.append（GIL 原子，T 轮既有模式）
- S0 类级直连（import YomkServerPy 白盒），放在 init 单例之前以独立实例隔离单例交互；
  S1-S7 经门面 API 走完整路由链

风格：纯 main() + 失败计数，返回非 0 表示存在失败用例（零第三方依赖）
"""

import gc
import sys
import threading

import YomkServerPy as ysp
from YomkApi import (
    YomkResponse,
    ResStatus,
    init,
    server,
    shutdown,
    add_service,
    del_service,
    new_service,
    request,
    server_info_services,
    server_info_functions,
    server_info_function,
    server_info_all,
)

PASS = 0
FAIL = 0


def check(cond, name):
    global PASS, FAIL
    if cond:
        PASS += 1
        print(f"[ OK ] {name}")
    else:
        FAIL += 1
        print(f"[FAIL] {name}")


def name_in(names, srv_name):
    return srv_name in names


def name_index(names, srv_name):
    if srv_name in names:
        return names.index(srv_name)
    return -1


def unpack_string_array(resp):
    # /services、/functions、/all 的 data 均为 list[str]
    if resp.status != ResStatus.eOk or resp.data is None:
        return []
    return list(resp.data)


# ---- 测试装置服务（FmtSrv/ChurnSrv）----

class FmtSrv(ysp.YomkService):
    # 行格式演示服务：/ping 两参安装（无类型，行格式 "func"）+
    # /echo 四参安装（行格式 "func [String]"，附函数说明）
    def init(self):
        self.install_func("/ping", self.ping)
        self.install_func("/echo", self.echo, "String",
                          "echo demo description")
        return 0

    def ping(self, pkg):
        return YomkResponse(ResStatus.eOk, "pong")

    def echo(self, pkg):
        return YomkResponse(ResStatus.eOk, pkg)


class ChurnSrv(ysp.YomkService):
    # churn 压力服务：最小 /hit 函数，供 S5 并发增删使用
    def init(self):
        self.install_func("/hit", self.hit)
        return 0

    def hit(self, pkg):
        return YomkResponse(ResStatus.eOk, "hit")


# ---- S5 并发观测容器（list.append GIL 原子收集，节末单线程 check）----
g_add_fail = []     # churn add_service 失败数（应恒为 0）
g_del_fail = []     # churn del_service 失败数（应恒为 0）
g_bad_status = []   # 内省端点非法状态计数（应恒为 0）
g_miss_fmt = []     # /services 快照丢失存活服务计数（应恒为 0）
g_bad_funcs = []    # /functions("/FmtSrv") 快照非 2 行计数（应恒为 0）


def main():
    # ============ S0. server destroyed 分支（类级白盒，先于单例初始化） ============
    print("===== S0. server destroyed 分支（4 端点 weakServer 判活失败） =====")
    srv = ysp.YomkServer.create(1)
    # 测试自持实例（不经 add_service），构造时绑定 server 弱引用
    info = ysp.YomkServerInfo(srv)
    check(info.init() == 0, "YomkServerInfo 独立实例 init 返回 0（4 端点已装配）")

    del srv           # server 销毁，m_weakServer 失效；info 由测试持有仍存活
    gc.collect()      # CPython 引用计数遇环不归零，显式触发（docstring 差异记录）

    r = info.invoke("/services", None)
    check(r.status == ResStatus.eNo and r.msg == "server has been destroyed",
          "/services server destroyed: eNo + 契约消息: " + r.msg)

    # /functions 先做空名校验，须传非空 srvName 越过前置校验到达判活失败点
    r = info.invoke("/functions", "/Any")
    check(r.status == ResStatus.eNo and r.msg == "server has been destroyed",
          "/functions server destroyed: eNo + 契约消息")

    # /function 先做 URL 解析校验，须传合法 url 越过前置校验到达判活失败点
    r = info.invoke("/function", "/Any/f")
    check(r.status == ResStatus.eNo and r.msg == "server has been destroyed",
          "/function server destroyed: eNo + 契约消息")

    r = info.invoke("/all", None)
    check(r.status == ResStatus.eNo and r.msg == "server has been destroyed",
          "/all server destroyed: eNo + 契约消息")

    del info
    check(True, "destroyed 实例释放无崩溃（弱引用悬垂路径安全收尾）")

    # ============ 单例初始化（S1-S7 共用） ============
    check(init(1) is not None, "init 拉起单例与 5 个内置服务")

    srv_ref = server()

    # ============ S1. /services ============
    print("===== S1. /services 端点 =====")
    resp = server_info_services()
    check(resp.status == ResStatus.eOk and resp.msg ==
          "ok", "/services 返回 eOk + ok")

    names = unpack_string_array(resp)
    check(name_in(names, "/YomkFunctionPool"),
          "/services 含内置 /YomkFunctionPool")
    check(name_in(names, "/YomkContext"), "/services 含内置 /YomkContext")
    check(name_in(names, "/YomkEventLoop"), "/services 含内置 /YomkEventLoop")
    check(name_in(names, "/YomkLogger"), "/services 含内置 /YomkLogger")
    check(name_in(names, "/YomkServerInfo"), "/services 含内置 /YomkServerInfo")

    check(add_service(FmtSrv(srv_ref), "/FmtSrv") == 0, "注册 FmtSrv 返回 0")
    check(name_in(unpack_string_array(server_info_services()), "/FmtSrv"),
          "/services 实时反映新增服务")

    check(del_service("/FmtSrv") == 0, "删除 FmtSrv 返回 0")
    check(not name_in(unpack_string_array(server_info_services()), "/FmtSrv"),
          "/services 实时反映服务删除")

    check(add_service(FmtSrv(srv_ref), "/FmtSrv")
          == 0, "重注册 FmtSrv（供 S2-S5 使用）")
    check(name_in(unpack_string_array(server_info_services()), "/FmtSrv"),
          "/services 重注册后恢复")

    # ============ S2. /functions ============
    print("===== S2. /functions 端点 =====")
    resp = server_info_functions("/FmtSrv")
    check(resp.status == ResStatus.eOk, "/functions(FmtSrv) 返回 eOk")
    lines = unpack_string_array(resp)
    check(len(lines) == 2, f"FmtSrv 恰 2 个功能函数: {len(lines)}")
    check(name_in(lines, "/echo [String]"),
          "四参安装行格式 \"/echo [String]\"（/functions 行仅类型名）")
    check(name_in(lines, "/ping"), "两参安装行格式 \"/ping\"（无类型后缀）")

    empty = server_info_functions("")
    check(empty.status == ResStatus.eInvalid and empty.msg == "srvName is empty",
          "空 srvName 返回 eInvalid + 契约消息: " + empty.msg)

    no_srv = server_info_functions("/NoSuchSrv")
    check(no_srv.status == ResStatus.eOk and unpack_string_array(no_srv) == [],
          "不存在服务返回 eOk + 空列表（现状契约）")

    ctx = server_info_functions("/YomkContext")
    ctx_lines = unpack_string_array(ctx)
    check(ctx.status == ResStatus.eOk and name_in(ctx_lines, "/create [Context]"),
          "内置 /YomkContext 含 \"/create [Context]\"")
    check(name_in(ctx_lines, "/keys"), "内置 /YomkContext 无类型端点行 \"/keys\"")

    self_resp = server_info_functions("/YomkServerInfo")
    self_lines = unpack_string_array(self_resp)
    check(self_resp.status == ResStatus.eOk and len(self_lines) == 4,
          "/YomkServerInfo 自身恰 4 个端点")
    check(name_in(self_lines, "/function [String]") and name_in(self_lines, "/all"),
          "自身行格式含带类型与无类型两种")

    # ============ S3. /function ============
    print("===== S3. /function 端点 =====")
    hit_typed = server_info_function("/FmtSrv/echo")
    check(hit_typed.status == ResStatus.eOk and hit_typed.msg == "String",
          "命中四参函数：eOk 且 msg 精确等于 msgName \"String\"（特殊契约）")
    check(hit_typed.data == "echo demo description",
          "四参函数说明挂入 data 段（msg 契约保持）")

    hit_plain = server_info_function("/FmtSrv/ping")
    check(hit_plain.status == ResStatus.eOk and hit_plain.msg == "",
          "命中两参函数：eOk 且 msg 为空")
    check(hit_plain.data is None,
          "无说明函数 data 段为空（None）")

    empty_url = server_info_function("")
    check(empty_url.status == ResStatus.eNo and "please start with /" in empty_url.msg,
          "空 url 返回 eNo + please start with /: " + empty_url.msg)

    no_slash = server_info_function("abc")
    check(no_slash.status == ResStatus.eNo and "please start with /" in no_slash.msg,
          "无 / 开头返回 eNo + please start with /")

    no_func_part = server_info_function("/FmtSrv")
    check(no_func_part.status == ResStatus.eNo and "not found function name" in no_func_part.msg,
          "无第二个 / 返回 eNo + not found function name: " + no_func_part.msg)

    no_srv = server_info_function("/NoSuchSrv/f")
    check(no_srv.status == ResStatus.eNo and no_srv.msg == "service not found: /NoSuchSrv",
          "服务不存在契约消息精确匹配: " + no_srv.msg)

    no_func = server_info_function("/FmtSrv/nope")
    check(no_func.status == ResStatus.eNo and no_func.msg == "function not found: /FmtSrv/nope",
          "函数不存在契约消息精确匹配: " + no_func.msg)

    null_pkg = request("/YomkServerInfo/function", None)
    check(null_pkg.status == ResStatus.eNo and "pkg is null" in null_pkg.msg,
          "null pkg 返回 eNo + pkg is null（解包守卫）")

    # ============ S4. /all ============
    print("===== S4. /all 端点 =====")
    resp = server_info_all()
    check(resp.status == ResStatus.eOk and resp.msg == "ok", "/all 返回 eOk + ok")

    lines = unpack_string_array(resp)
    check(len(lines) > 0, "/all 输出非空")

    # FmtSrv 段：服务名行后紧跟两条两空格缩进功能行，段内按字典序 /echo 在 /ping 前
    fmt_idx = name_index(lines, "/FmtSrv")
    check(fmt_idx >= 0, "/all 含 FmtSrv 服务名行")
    if fmt_idx >= 0 and fmt_idx + 2 < len(lines):
        check(lines[fmt_idx + 1] == "  /FmtSrv/echo [String]",
              "缩进行精确格式 \"  /FmtSrv/echo [String]\"")
        check(lines[fmt_idx + 2] == "  /FmtSrv/ping",
              "缩进行精确格式 \"  /FmtSrv/ping\"")
    else:
        check(False, "FmtSrv 段缩进行缺失")

    # 内置 /YomkServerInfo 段：4 端点齐全（/services、/all 无类型，另两个带 [String]）
    self_idx = name_index(lines, "/YomkServerInfo")
    check(self_idx >= 0, "/all 含 /YomkServerInfo 服务名行")
    check(name_in(lines, "  /YomkServerInfo/services"), "内置段含 \"/services\" 缩进行")
    check(name_in(lines, "  /YomkServerInfo/functions [String]"),
          "内置段含 \"/functions [String]\" 缩进行")
    check(name_in(lines, "  /YomkServerInfo/all"), "内置段含 \"/all\" 缩进行")

    # ============ S5. 轻量并发快照一致性 ============
    print("===== S5. 并发 churn × 内省快照一致性 =====")

    def churn_fn(tid):
        for rnd in range(50):
            name = f"/Churn{tid}_{rnd}"
            if add_service(ChurnSrv(srv_ref), name) != 0:
                g_add_fail.append(1)
            if del_service(name) != 0:
                g_del_fail.append(1)

    def introspect_fn():
        for _ in range(200):
            services = server_info_services()
            if services.status != ResStatus.eOk:
                g_bad_status.append(1)
            elif not name_in(unpack_string_array(services), "/FmtSrv"):
                g_miss_fmt.append(1)   # FmtSrv 恒存活，快照不得丢失

            functions = server_info_functions("/FmtSrv")
            if functions.status != ResStatus.eOk or len(unpack_string_array(functions)) != 2:
                g_bad_funcs.append(1)

            if server_info_function("/FmtSrv/ping").status != ResStatus.eOk:
                g_bad_status.append(1)

            if server_info_all().status != ResStatus.eOk:
                g_bad_status.append(1)

    threads = [threading.Thread(target=churn_fn, args=(tid,))
               for tid in range(3)]
    threads += [threading.Thread(target=introspect_fn) for _ in range(3)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()

    check(len(g_add_fail) == 0 and len(g_del_fail)
          == 0, "churn 150 轮 add/del 全部成功")
    check(len(g_bad_status) == 0, "并发内省 4 端点状态全部合法")
    check(len(g_miss_fmt) == 0, "/services 快照恒含存活服务 /FmtSrv")
    check(len(g_bad_funcs) == 0, "/functions(\"/FmtSrv\") 快照恒为 2 行")

    # churn 收尾：无残留、存活服务仍可调用
    names = unpack_string_array(server_info_services())
    churn_left = [n for n in names if n.startswith("/Churn")]
    check(not churn_left, "churn 后服务表无 /Churn 残留")
    check(request("/FmtSrv/ping", None).status == ResStatus.eOk,
          "并发结束后 FmtSrv 仍可正常调用")

    # ============ S6. 内置服务删除/重注册自举契约 ============
    print("===== S6. 内置服务删除/重注册自举契约 =====")
    check(del_service("/YomkServerInfo") == 0, "删除内置 /YomkServerInfo 返回 0")

    deleted = server_info_services()
    check(deleted.status == ResStatus.eNo and deleted.msg == "service not found: /YomkServerInfo",
          "删除后内省端点不可路由: " + deleted.msg)

    check(not name_in(srv_ref.service_names(), "/YomkServerInfo"),
          "服务表快照不再含 /YomkServerInfo")

    # 无参重注册：new_service 不改名，构造函数固定名 "/YomkServerInfo" 生效
    check(new_service(ysp.YomkServerInfo) == 0,
          "new_service 无参重注册返回 0（构造函数固定名生效）")

    restored = server_info_services()
    check(restored.status == ResStatus.eOk and name_in(unpack_string_array(restored), "/YomkServerInfo"),
          "重注册后自举恢复：/services eOk 且含自身")
    check(name_in(unpack_string_array(server_info_all()), "/YomkServerInfo"),
          "重注册后 /all 含自身段（自举查询成功）")

    # ============ S7. 超长名内省边界（65536 字符服务名，对齐 FPC1-S8） ============
    print("===== S7. 超长名内省边界（65536 字符服务名） =====")
    long_name = "/Long" + "x" * 65531
    check(len(long_name) == 65536, "超长服务名长度 65536")

    check(add_service(FmtSrv(srv_ref), long_name) == 0, "超长名服务注册返回 0")
    check(name_in(unpack_string_array(server_info_services()), long_name),
          "/services 行完整包含超长服务名（无截断）")

    long_funcs = server_info_functions(long_name)
    long_func_lines = unpack_string_array(long_funcs)
    check(long_funcs.status == ResStatus.eOk and len(long_func_lines) == 2,
          "超长名服务 /functions 返回 2 行")
    check(name_in(long_func_lines, "/echo [String]") and name_in(long_func_lines, "/ping"),
          "超长名下函数行格式完整")

    long_function = server_info_function(long_name + "/ping")
    check(long_function.status == ResStatus.eOk and long_function.msg == "",
          "超长名下 /function URL 二段解析正确（msg 为空）")

    all_lines = unpack_string_array(server_info_all())
    long_idx = name_index(all_lines, long_name)
    check(long_idx >= 0, "/all 含超长服务名行")
    if long_idx >= 0 and long_idx + 2 < len(all_lines):
        check(all_lines[long_idx + 1] == "  " + long_name + "/echo [String]",
              "/all 超长缩进行完整（echo [String]）")
        check(all_lines[long_idx + 2] == "  " + long_name + "/ping",
              "/all 超长缩进行完整（ping）")
    else:
        check(False, "/all 超长段缩进行缺失")

    check(del_service(long_name) == 0, "超长名服务删除返回 0")
    check(not name_in(unpack_string_array(server_info_services()), long_name),
          "删除后 /services 不再含超长名")

    # ============ 单例收尾 ============
    shutdown()
    check(server() is None, "shutdown 后单例清空")

    print(("ALL PASSED" if FAIL == 0 else "SOME FAILED") + f" ({FAIL} failed)")
    sys.exit(0 if FAIL == 0 else 1)


if __name__ == "__main__":
    main()
