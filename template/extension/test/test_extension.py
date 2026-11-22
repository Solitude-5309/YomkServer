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
from yomk.msgs import String
from yomk.msgs.server import ResStatus
from yomk.msgs.extension import ExtensionRequest, ExtensionResponse
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

        # 类型契约：跨边界载荷只收 YomkMsg 实例，裸值在入口当场 TypeError
        try:
            server().url(EXTENSION_ECHO).msg("wrong type")
        except TypeError as e:
            assert str(e) == "msg() requires a YomkMsg instance or None, got 'str'", e
        else:
            raise AssertionError("msg() 传裸值应抛 TypeError")

        # 类型不符契约（闭环 3 起标准形态）：错包 → eNo + TYPE_NAME 双侧文案
        # （期望侧 ExtensionRequest.TYPE_NAME；got 侧为传入消息的 TYPE_NAME）
        bad = server().url(EXTENSION_ECHO).msg(String("wrong type")).request()
        assert bad.status == ResStatus.eNo, (bad.status, bad.msg)
        assert bad.msg == ("pkg is not yomk.msgs.extension.ExtensionRequest, "
                           "got 'yomk.msgs.String'"), bad.msg

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
