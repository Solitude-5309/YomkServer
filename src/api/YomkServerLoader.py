#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""YomkServerLoader.py —— 扩展本体装载 shim（构建/安装时更名为 YomkServer.py）

扩展本体文件名为 YomkServerPy.so（Windows 为 YomkServerPy.pyd），与 CMake 工程身份
YomkServerPy 一致，避免与 C++ 版产物混淆；而 PyInit_ 符号由合并源文件名
build/YomkServer.py 决定，即 PyInit_YomkServer。

本 shim 以模块名 YomkServer 显式装载扩展本体，使 `import YomkServer` /
`from YomkServer import ...` 与门面 `import YomkApi` 的既有契约保持不变。
"""

import importlib.machinery
import importlib.util
import os
import sys

_MODULE_NAME = "YomkServer"
_EXTENSION_NAME = "YomkServerPy"
_HERE = os.path.dirname(os.path.abspath(__file__))


def _find_extension():
    for suffix in importlib.machinery.EXTENSION_SUFFIXES:
        candidate = os.path.join(_HERE, _EXTENSION_NAME + suffix)
        if os.path.isfile(candidate):
            return candidate
    raise ImportError(
        "YomkServer extension not found: expected %s.{...} next to %s"
        % (_EXTENSION_NAME, os.path.abspath(__file__))
    )


def _load_extension():
    path = _find_extension()
    loader = importlib.machinery.ExtensionFileLoader(_MODULE_NAME, path)
    spec = importlib.util.spec_from_file_location(_MODULE_NAME, path, loader=loader)
    module = importlib.util.module_from_spec(spec)
    loader.exec_module(module)
    return module


# 以扩展模块替换 shim 自身，后续 `import YomkServer` 直接得到扩展模块
sys.modules[_MODULE_NAME] = _load_extension()