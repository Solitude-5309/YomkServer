"""
TestYomkContextCRUD.py —— YomkContext CRUD 与边界值白盒测试（Context 模块 MC1）

覆盖内容：
1. create：正常创建（str/int）、空 key、空 value（None）、重复 key
2. get：正常 get、不存在 key（返回默认 value）、空 key（返回默认 value）
3. set：正常 set、不存在 key、空 key、类型不匹配
4. keys/keyInfo/listAll：随增删变化、空 key、不存在 key、keyInfo 结构化包字段与清单行格式
5. destroy：正常销毁、重复销毁、不存在 key、空 key（等价不存在）、销毁后 keys 计数

语言机制说明（非功能缺口）：
- 门面 context_get(key, default) 不携带目标类型参数、eOk 返回存储值本身，
  调用方以 python 动态类型自然规避"类型不匹配"失败路径（A-2 语言机制剔除项）
- 内置服务注册校验以 server.service_names() 承载（A-4 口径）
- keyInfo 命中为结构化返回：msg 恒 "ok"，data 为 ContextKeyInfo 包（字段 type_name
  为 python 原生类型名 str/int/bool，value 为 None 时空串）；listAll 仍为清单行文本
  （类型名以 type().__name__ 呈现，C1 定稿口径）

风格：纯 main() + 失败计数，退出码非 0 表示存在失败用例（零第三方依赖）
全程使用门面单例；
不触发 checker/monitor，不触发 shutdown 排空（归 MC2/MC3）
"""

import sys

from YomkApi import (
    ContextKeyInfo,
    context_create,
    context_destroy,
    context_get,
    context_info_all,
    context_info_key,
    context_info_keys,
    context_set,
    init,
    server,
    shutdown,
)
from YomkServerPy import ResStatus

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
    srv = init(1)
    check(srv is not None, "init 返回非空服务器")

    # 验证 /YomkContext 已注册
    check("/YomkContext" in srv.service_names(),
          "serviceNames 包含 /YomkContext")

    # create：正常创建 String 类型 value
    resp = context_create("str_key", "hello")
    check(resp.status == ResStatus.eOk, "create 正常 String value 返回 eOk")
    check(resp.msg == "create context success", "create 成功消息一致")

    # create：正常创建 Int32 类型 value
    resp = context_create("int_key", 42)
    check(resp.status == ResStatus.eOk, "create 正常 Int32 value 返回 eOk")

    # create：空 key
    resp = context_create("", "value")
    check(resp.status == ResStatus.eNo, "create 空 key 返回 eNo")
    check(resp.msg == "key is empty", "create 空 key 错误消息一致")

    # create：空 value（None）
    resp = context_create("null_value_key", None)
    check(resp.status == ResStatus.eNo, "create None value 返回 eNo")
    check(resp.msg == "value is empty", "create None value 错误消息一致")

    # create：重复 key
    resp = context_create("str_key", "dup")
    check(resp.status == ResStatus.eNo, "create 重复 key 返回 eNo")
    check(resp.msg == "key already exists", "create 重复 key 错误消息一致")

    # get：正常 get
    check(context_get("str_key", "default") == "hello", "get 正常返回原始 value")

    # get：不存在 key，返回默认 value
    check(context_get("no_such_key", "default") == "default",
          "get 不存在 key 返回默认值")

    # get：空 key，返回默认 value
    check(context_get("", "default") == "default", "get 空 key 返回默认值")

    # set：正常 set 同类型 value
    resp = context_set("str_key", "world")
    check(resp.status == ResStatus.eOk, "set 正常同类型返回 eOk")
    check(resp.msg == "set context success", "set 成功消息一致")
    check(context_get("str_key", "default") == "world", "set 后 get 返回新 value")

    # set：不存在 key
    resp = context_set("no_such_key", "x")
    check(resp.status == ResStatus.eNo, "set 不存在 key 返回 eNo")
    check(resp.msg == "key is not exist", "set 不存在 key 错误消息一致")

    # set：空 key
    resp = context_set("", "x")
    check(resp.status == ResStatus.eNo, "set 空 key 返回 eNo")
    check(resp.msg == "key is empty", "set 空 key 错误消息一致")

    # set：类型不匹配
    resp = context_set("str_key", 100)
    check(resp.status == ResStatus.eNo, "set 类型不匹配返回 eNo")
    check(resp.msg == "context type not match", "set 类型不匹配错误消息一致")

    # keys：包含已创建 key
    resp = context_info_keys()
    check(resp.status == ResStatus.eOk, "keys 返回 eOk")
    arr = resp.data
    check(isinstance(arr, list) and len(arr) == 2, "keys 返回 2 个 key")
    check("str_key" in arr, "keys 包含 str_key")
    check("int_key" in arr, "keys 包含 int_key")

    # keyInfo：正常 key（结构化返回，msg 恒 "ok"，数据走 data）
    resp = context_info_key("str_key")
    check(resp.status == ResStatus.eOk, "keyInfo 正常 key 返回 eOk")
    check(resp.msg == "ok", "keyInfo 命中 msg 恒为 ok")
    check(isinstance(resp.data, ContextKeyInfo),
          "keyInfo 数据为 ContextKeyInfo 包")
    check(resp.data.key == "str_key", "keyInfo 字段 key 一致")
    check(resp.data.type_name == "str", "keyInfo 字段 type_name 一致")
    check(resp.data.checker_on is False, "keyInfo 字段 checker_on 为 False")
    check(resp.data.monitor_count == 0 and resp.data.async_monitor_count == 0,
          "keyInfo 字段 monitor 计数为 0")

    # keyInfo：空 key
    resp = context_info_key("")
    check(resp.status == ResStatus.eNo, "keyInfo 空 key 返回 eNo")
    check(resp.msg == "key is empty", "keyInfo 空 key 错误消息一致")

    # keyInfo：不存在 key
    resp = context_info_key("no_such_key")
    check(resp.status == ResStatus.eNo, "keyInfo 不存在 key 返回 eNo")
    check(resp.msg == "key is not exist", "keyInfo 不存在 key 错误消息一致")

    # listAll：包含所有 key 信息
    resp = context_info_all()
    check(resp.status == ResStatus.eOk, "listAll 返回 eOk")
    check(isinstance(resp.data, list) and len(resp.data) == 2,
          "listAll 返回 2 行信息")

    # destroy：正常销毁
    resp = context_destroy("int_key")
    check(resp.status == ResStatus.eOk, "destroy 正常销毁返回 eOk")
    check(resp.msg == "destroy context success", "destroy 成功消息一致")

    # destroy：重复销毁
    resp = context_destroy("int_key")
    check(resp.status == ResStatus.eNo, "destroy 重复销毁返回 eNo")
    check(resp.msg == "key is not exist", "destroy 重复销毁错误消息一致")

    # destroy：不存在 key
    resp = context_destroy("no_such_key")
    check(resp.status == ResStatus.eNo, "destroy 不存在 key 返回 eNo")
    check(resp.msg == "key is not exist", "destroy 不存在 key 错误消息一致")

    # destroy：空 key（源码不单独判空，等价于不存在的 key）
    resp = context_destroy("")
    check(resp.status == ResStatus.eNo, "destroy 空 key 返回 eNo")
    check(resp.msg == "key is not exist", "destroy 空 key 错误消息一致")

    # keys：销毁后只剩一个 key
    resp = context_info_keys()
    check(resp.status == ResStatus.eOk, "keys 销毁后返回 eOk")
    arr = resp.data
    check(isinstance(arr, list) and len(arr) == 1, "keys 销毁后返回 1 个 key")
    check("str_key" in arr, "keys 销毁后仍包含 str_key")
    check("int_key" not in arr, "keys 销毁后不含 int_key")

    # 清理：销毁剩余 key
    resp = context_destroy("str_key")
    check(resp.status == ResStatus.eOk, "清理销毁 str_key 返回 eOk")

    # keys：全部销毁后为空
    resp = context_info_keys()
    check(resp.status == ResStatus.eOk, "keys 全部销毁后返回 eOk")
    check(resp.data == [], "keys 全部销毁后返回空数组")

    shutdown()

    print(("ALL PASSED" if g_failed == 0 else "SOME FAILED")
          + f" ({g_failed} failed)")
    return 0 if g_failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
