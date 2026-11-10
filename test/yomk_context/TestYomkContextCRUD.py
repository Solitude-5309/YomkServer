"""
TestYomkContextCRUD.py —— YomkContext CRUD 与边界值白盒测试（Context 模块 MC1）

覆盖内容：
1. create：正常创建（str/int）、空 key、空 value（None）、重复 key
2. get：正常 get、不存在 key（返回默认 value）、空 key（返回默认 value）
3. set：正常 set、不存在 key、空 key、类型不匹配
4. keys/keyInfo/listAll：随增删变化、空 key、不存在 key、keyInfo 结构化包字段与 listAll 结构化列表
5. destroy：正常销毁、重复销毁、不存在 key、空 key（独立 Invalid 分支）、销毁后 keys 计数

语言机制说明（非功能缺口）：
- 门面 context_get(key, default) 不携带目标类型参数、eOk 返回存储值本身，
  调用方以 python 动态类型自然规避"类型不匹配"失败路径
- 内置服务注册校验以 server.service_names() 承载
- keyInfo 命中为结构化返回：msg 恒 "ok"，data 为 ContextKeyInfo 包（字段 type_name
  为 python 原生类型名 str/int/bool，value 为 None 时空串）；listAll 命中同为结构化
  返回：msg 恒 "ok"，data 为 ContextKeyInfo 列表（键序为字典序）

风格：纯 main() + 失败计数，退出码非 0 表示存在失败用例（零第三方依赖）
全程使用门面单例；
不触发 checker/monitor，不触发 shutdown 排空（归 MC2/MC3）
"""

import sys

from yomk.msg.server import ResStatus
from yomk.msg.ctx import Context, ContextGroupInfo, ContextGroupKey, ContextKeyInfo
from yomk.server import server
from yomk.ctx import ctx


def raw_ctx(endpoint, pkg):
    # 写路径链式终结收敛为 ResStatus（无 msg）；契约消息断言经 server 域
    # url/msg/request 裸链直取 Response（端点行为与链式终结一致）
    return server().url("/YomkContext/" + endpoint).msg(pkg).request()

g_failed = 0

def check(cond, msg):
    global g_failed
    line = sys._getframe(1).f_lineno
    if cond:
        print(f"[ OK ] [line {line}] {msg}")
    else:
        print(f"[FAIL] [line {line}] {msg}")
        g_failed += 1

def main():
    # 拉起内置服务 /YomkContext
    server().async_thread(1).init()
    srv = server().instance()
    check(srv is not None, "init 返回非空服务器")

    # 验证 /YomkContext 已注册
    check("/YomkContext" in srv.service_names(),
          "serviceNames 包含 /YomkContext")

    # create：正常创建 String 类型 value
    resp = raw_ctx("create", Context("str_key", "hello", "main"))
    check(resp.status == ResStatus.eOk, "create 正常 String value 返回 eOk")
    check(resp.msg == "create context success", "create 成功消息一致")

    # create：正常创建 Int32 类型 value
    resp = ctx().key("int_key").value(42).create()
    check(resp == ResStatus.eOk, "create 正常 Int32 value 返回 eOk")

    # create：空 key
    resp = raw_ctx("create", Context("", "value", "main"))
    check(resp.status == ResStatus.eInvalid, "create 空 key 返回 eInvalid")
    check(resp.msg == "context key is empty", "create 空 key 错误消息一致")

    # create：空 value（None）
    resp = raw_ctx("create", Context("null_value_key", None, "main"))
    check(resp.status == ResStatus.eInvalid, "create None value 返回 eInvalid")
    check(resp.msg == "create context key 'null_value_key' failed: value is empty",
          "create None value 错误消息一致")

    # create：重复 key
    resp = raw_ctx("create", Context("str_key", "dup", "main"))
    check(resp.status == ResStatus.eNo, "create 重复 key 返回 eNo")
    check(resp.msg == "context key 'str_key' already exists",
          "create 重复 key 错误消息一致")

    # get：正常 get
    check(ctx().key("str_key").msg().with_default("default").get() == "hello", "get 正常返回原始 value")

    # get：不存在 key，返回默认 value
    check(ctx().key("no_such_key").msg().with_default("default").get() == "default",
          "get 不存在 key 返回默认值")

    # get：空 key，返回默认 value
    check(ctx().key("").msg().with_default("default").get() == "default", "get 空 key 返回默认值")

    # set：正常 set 同类型 value
    resp = raw_ctx("set", Context("str_key", "world", "main"))
    check(resp.status == ResStatus.eOk, "set 正常同类型返回 eOk")
    check(resp.msg == "set context success", "set 成功消息一致")
    check(ctx().key("str_key").msg().with_default("default").get() == "world", "set 后 get 返回新 value")

    # set：不存在 key
    resp = raw_ctx("set", Context("no_such_key", "x", "main"))
    check(resp.status == ResStatus.eNo, "set 不存在 key 返回 eNo")
    check(resp.msg == "context key 'no_such_key' does not exist",
          "set 不存在 key 错误消息一致")

    # set：空 key
    resp = raw_ctx("set", Context("", "x", "main"))
    check(resp.status == ResStatus.eInvalid, "set 空 key 返回 eInvalid")
    check(resp.msg == "set context key is empty", "set 空 key 错误消息一致")

    # set：类型不匹配
    resp = raw_ctx("set", Context("str_key", 100, "main"))
    check(resp.status == ResStatus.eNo, "set 类型不匹配返回 eNo")
    check(resp.msg == "context type not match: key 'str_key' expect 'str', got 'int'",
          "set 类型不匹配错误消息一致")

    # keys：包含已创建 key
    resp = ctx().keys()
    check(resp.status == ResStatus.eOk, "keys 返回 eOk")
    arr = resp.data
    check(isinstance(arr, list) and len(arr) == 2, "keys 返回 2 个 key")
    check("str_key" in arr, "keys 包含 str_key")
    check("int_key" in arr, "keys 包含 int_key")

    # keyInfo：正常 key（结构化返回，msg 恒 "ok"，数据走 data）
    resp = ctx().key("str_key").info()
    check(resp.status == ResStatus.eOk, "keyInfo 正常 key 返回 eOk")
    check(resp.msg == "ok", "keyInfo 命中 msg 恒为 ok")
    check(isinstance(resp.data, ContextKeyInfo),
          "keyInfo 数据为 ContextKeyInfo 包")
    check(resp.data.key == "str_key", "keyInfo 字段 key 一致")
    check(resp.data.type_name == "str", "keyInfo 字段 type_name 一致")
    check(resp.data.checker_on is False, "keyInfo 字段 checker_on 为 False")
    check(resp.data.monitor_count == 0 and resp.data.async_monitor_count == 0,
          "keyInfo 字段 monitor 计数为 0")
    check(resp.data.group == "main", "keyInfo 字段 group 一致")

    # keyInfo：空 key
    resp = ctx().key("").info()
    check(resp.status == ResStatus.eInvalid, "keyInfo 空 key 返回 eInvalid")
    check(resp.msg == "context key is empty", "keyInfo 空 key 错误消息一致")

    # keyInfo：不存在 key
    resp = ctx().key("no_such_key").info()
    check(resp.status == ResStatus.eNo, "keyInfo 不存在 key 返回 eNo")
    check(resp.msg == "context key 'no_such_key' does not exist",
          "keyInfo 不存在 key 错误消息一致")

    # listAll：跨组聚合（组 → 键元信息两层结构化列表，msg 恒 "ok"）
    resp = ctx().all()
    check(resp.status == ResStatus.eOk, "listAll 返回 eOk")
    check(resp.msg == "ok", "listAll 成功消息恒为 ok")
    check(isinstance(resp.data, list) and len(resp.data) == 1,
          "listAll 返回 1 个组信息")
    check(isinstance(resp.data[0], ContextGroupInfo),
          "listAll 元素为 ContextGroupInfo 包")
    check(resp.data[0].group_name == "main", "listAll 组名为 main")
    # 组内 keys 为字典序快照：int_key < str_key；逐字段断言（均未挂 checker/monitor）
    check(len(resp.data[0].keys) == 2, "listAll 返回 2 个键信息")
    check(all(isinstance(i, ContextKeyInfo) for i in resp.data[0].keys),
          "listAll 元素为 ContextKeyInfo 包")
    check(resp.data[0].keys[0].key == "int_key" and resp.data[0].keys[1].key == "str_key",
          "listAll 键序为字典序（int_key, str_key）")
    check(resp.data[0].keys[0].type_name == "int" and resp.data[0].keys[1].type_name == "str",
          "listAll 类型名一致")
    check(resp.data[0].keys[0].group == "main" and resp.data[0].keys[1].group == "main",
          "listAll 键所属组一致")
    check(all(not i.checker_on and i.monitor_count == 0
              and i.async_monitor_count == 0 for i in resp.data[0].keys),
          "listAll checker/monitor 计数为 0")

    # destroy：正常销毁
    resp = raw_ctx("destroy", ContextGroupKey("main", "int_key"))
    check(resp.status == ResStatus.eOk, "destroy 正常销毁返回 eOk")
    check(resp.msg == "destroy context success", "destroy 成功消息一致")

    # destroy：重复销毁
    resp = raw_ctx("destroy", ContextGroupKey("main", "int_key"))
    check(resp.status == ResStatus.eNo, "destroy 重复销毁返回 eNo")
    check(resp.msg == "context key 'int_key' does not exist",
          "destroy 重复销毁错误消息一致")

    # destroy：不存在 key
    resp = raw_ctx("destroy", ContextGroupKey("main", "no_such_key"))
    check(resp.status == ResStatus.eNo, "destroy 不存在 key 返回 eNo")
    check(resp.msg == "context key 'no_such_key' does not exist",
          "destroy 不存在 key 错误消息一致")

    # destroy：空 key（独立空键分支：eInvalid，不等价于不存在）
    resp = raw_ctx("destroy", ContextGroupKey("main", ""))
    check(resp.status == ResStatus.eInvalid, "destroy 空 key 返回 eInvalid")
    check(resp.msg == "context key is empty", "destroy 空 key 错误消息一致")

    # keys：销毁后只剩一个 key
    resp = ctx().keys()
    check(resp.status == ResStatus.eOk, "keys 销毁后返回 eOk")
    arr = resp.data
    check(isinstance(arr, list) and len(arr) == 1, "keys 销毁后返回 1 个 key")
    check("str_key" in arr, "keys 销毁后仍包含 str_key")
    check("int_key" not in arr, "keys 销毁后不含 int_key")

    # 清理：销毁剩余 key
    resp = ctx().key("str_key").destroy()
    check(resp == ResStatus.eOk, "清理销毁 str_key 返回 eOk")

    # keys：全部销毁后为空
    resp = ctx().keys()
    check(resp.status == ResStatus.eOk, "keys 全部销毁后返回 eOk")
    check(resp.data == [], "keys 全部销毁后返回空数组")

    server().shutdown()

    print(("ALL PASSED" if g_failed == 0 else "SOME FAILED")
          + f" ({g_failed} failed)")
    return 0 if g_failed == 0 else 1

if __name__ == "__main__":
    sys.exit(main())
