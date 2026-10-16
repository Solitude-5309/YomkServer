#!/usr/bin/env python3
"""TestTemplateExtension —— 扩展功能测试程序（就地运行，不随扩展安装）"""

import sys

from TemplateExtension import ExtensionService
import YomkApi

g_pass = 0
g_fail = 0


def main():
    global g_pass, g_fail

    YomkApi.init()

    # 注册 ExtensionService（扩展服务需先注册才能使用）
    YomkApi.new_service(ExtensionService, "/ExtensionService")

    # 测试版本查询
    resp = YomkApi.request("/ExtensionService/version", None)
    if resp.status == YomkApi.ResStatus.eOk:
        print(f"[PASS] version: {resp.data}")
        g_pass += 1
    else:
        print(f"[FAIL] version request failed: {resp.msg}")
        g_fail += 1

    print("\n========== Test Summary ==========")
    print(f"PASS: {g_pass}")
    print(f"FAIL: {g_fail}")

    sys.exit(1 if g_fail > 0 else 0)


if __name__ == "__main__":
    main()
