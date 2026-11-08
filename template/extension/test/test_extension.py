#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
test_extension.py —— 第三方扩展示例的运行级验证

流程：框架 init（内置 5 服务）→ 显式注册扩展运行时服务
（server().srv(ExtensionService).create()，无隐式发现）→ 裸请求 /ExtensionService/echo
（扩展消息 ExtensionRequest 作请求包，强类型 ExtensionResponse 承载回包）→ shutdown。
退出码 0 = 通过。
"""

import sys

from yomk.server import server
from yomk.msg.server import ResStatus
from yomk.msg.extension import ExtensionRequest, ExtensionResponse
from yomk.extension import ExtensionService


def main():
    server().async_thread(1).init()
    try:
        # 第三方运行时服务：显式注册（框架不做隐式发现）
        ret = server().srv(ExtensionService).create()
        assert ret == 0, f"srv(ExtensionService).create() == {ret}"

        # 扩展消息作请求包直发（裸 URL 请求）
        resp = server().url("/ExtensionService/echo").msg(ExtensionRequest("hello ext")).request()
        assert resp.status == ResStatus.eOk, (resp.status, resp.msg)
        data = resp.data
        assert isinstance(data, ExtensionResponse) and data.msg == "echo" \
            and data.data == "hello ext", data

        print("extension smoke OK:", data)
        return 0
    finally:
        server().shutdown()


if __name__ == "__main__":
    sys.exit(main())
