#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
test_extension.py —— 第三方扩展示例的运行级验证

流程：框架 init（内置 5 服务）→ 显式注册扩展运行时服务
（server().srv(ExtensionService).create()，无隐式发现）→ 统一请求接口打 EXTENSION_ECHO
（扩展消息 ExtensionRequest 作请求包，强类型 ExtensionResponse 承载回包）→
extension 域 builder 版本查询终结（extension().version()）→ shutdown。
退出码 0 = 通过。
"""

import sys

from yomk.server import __version__, server
from yomk.msg.server import ResStatus
from yomk.msg.extension import ExtensionRequest, ExtensionResponse
from yomk.extension import ExtensionService, extension, __version__ as extension_version
from yomk.url.extension import EXTENSION_ECHO   # URL 常量池（并入框架 yomk.url 命名空间段）
from yomk.log import log


def main():
    server().async_thread(1).init()
    try:
        log().tag("test_extension").info(f"version: framework={__version__} extension={extension_version}")
        # 第三方运行时服务：显式注册（框架不做隐式发现）
        ret = server().srv(ExtensionService).create()
        assert ret == 0, f"srv(ExtensionService).create() == {ret}"

        # 统一请求接口（推荐优先）：扩展消息作请求包直发，端点经 yomk.url.extension 常量池
        resp = server().url(EXTENSION_ECHO).msg(ExtensionRequest("hello ext")).request()
        assert resp.status == ResStatus.eOk, (resp.status, resp.msg)
        data = resp.data
        assert isinstance(data, ExtensionResponse) and data.msg == "echo" \
            and data.data == "hello ext", data

        log().tag("test_extension").info("extension smoke OK:", data)

        # extension 域 builder 版本查询终结（对齐 C++ ExtensionApi.h extVersion()）
        ver = extension().version()
        assert ver.status == ResStatus.Succeeded, (ver.status, ver.msg)
        assert ver.data == f"extension v{extension_version}", ver
        log().tag("test_extension").info("extension version OK:", ver.data)
        return 0
    finally:
        server().shutdown()


if __name__ == "__main__":
    sys.exit(main())
