# TemplateExtension —— 第三方扩展示例模板（yomk 命名空间并入形态）

本模板演示第三方仓库如何为 YomkServerPy 提供扩展：**独立仓库只写
`yomk/msg/extension_service/`（消息类型）与 `yomk/extension_service/`（运行时服务）两个目录**，
安装到框架 site-packages 后按 PEP 420 命名空间与框架子包平级合并。

## 目录结构

```
template/extension/
├── src/yomk/               # 扩展源树（与框架同构：只写自己的叶子目录）
│   ├── msg/extension_service/      # 消息子包（并入框架 yomk.msg 命名空间段）
│   │   ├── __init__.py             #   导出 ExtensionRequest / ExtensionResponse
│   │   ├── ExtensionMsg.py         #   消息类型定义
│   │   └── py.typed
│   └── extension_service/          # 运行时域子包（并入框架 yomk 命名空间段）
│       ├── __init__.py             #   __version__ + 导出 ExtensionService
│       ├── ExtensionService.py     #   运行时服务（YomkService 子类）
│       └── py.typed
├── test/test_template_extension.py
├── install.sh
└── README.md
```

## 命名空间纪律（必须遵守）

框架的 `yomk/` 与 `yomk/msg/` 两层是 **PEP 420 命名空间包**（无 `__init__.py`，
`import yomk` 不加载任何子模块、`yomk.__file__ is None`）。第三方扩展依赖
该机制与框架合并，因此：

- 扩展仓库**不得创建** `yomk/__init__.py` 或 `yomk/msg/__init__.py`
  （install.sh 安装前会做守卫检查）；
- 只新增自己的叶子目录（本模板为 `yomk/msg/extension_service/` 与 `yomk/extension_service/`）；
- 扩展域的 `__init__.py` 内自带版本常量（框架各域由构建期注入
  `_version.py`，扩展可简化为普通常量）。

## 第三方运行时服务纪律

框架只拉起 5 个内置服务，**不做隐式发现**。扩展服务由使用方显式注册：

```python
from yomk.server import server
from yomk.extension_service import ExtensionService

server().async_thread(1).init()
server().srv(ExtensionService).create()   # 显式注册后 /ExtensionService/* 才可达
```

## 安装与验证

```bash
bash install.sh        # 交互输入框架安装路径（默认取 YOMK_PREFIX_PATH）
python3 test/test_template_extension.py
```

安装后可用：

```python
from yomk.msg.extension_service import ExtensionRequest, ExtensionResponse   # 扩展消息
from yomk.extension_service import ExtensionService                    # 扩展运行时
```
