# TemplateExtension —— 第三方扩展示例模板（yomk 命名空间并入形态）

本模板演示第三方仓库如何为 YomkServerPy 提供扩展：**独立仓库只写
`yomk/msg/extension/`（消息类型）、`yomk/extension/`（运行时服务）与
`yomk/url/extension/`（URL 常量池）三个目录**，
安装到框架 site-packages 后按 PEP 420 命名空间与框架子包平级合并。

## 目录结构

```
template/extension/
├── src/yomk/               # 扩展源树（与框架同构：只写自己的叶子目录）
│   ├── msg/extension/      # 消息子包（并入框架 yomk.msg 命名空间段）
│   │   ├── __init__.py             #   导出 ExtensionRequest / ExtensionResponse
│   │   ├── ExtensionMsg.py         #   消息类型定义
│   │   └── py.typed
│   ├── extension/          # 运行时域子包（并入框架 yomk 命名空间段，与框架各域同构：builder + 类型桩）
│   │   ├── __init__.py             #   __version__ + 导出 extension/ExtensionBuilder/ExtensionService
│   │   ├── __init__.pyi            #   类型桩
│   │   ├── builder.py              #   extension 域 builder（版本查询终结 extension().version()）
│   │   ├── builder.pyi             #   类型桩
│   │   ├── ExtensionService.py     #   运行时服务（YomkService 子类，/echo 与 /version 端点）
│   │   └── py.typed
│   └── url/extension/      # URL 常量池子包（并入框架 yomk.url 常量池，与 yomk/url/ctx 同构）
│       ├── __init__.py             #   EXTENSION_ECHO / EXTENSION_VERSION 等端点常量
│       ├── __init__.pyi            #   类型桩
│       └── py.typed
├── test/test_extension.py
├── install.sh
└── README.md
```

## 命名空间纪律（必须遵守）

框架的 `yomk/`、`yomk/msg/`、`yomk/url/` 三层是 **PEP 420 命名空间包**（无 `__init__.py`，
`import yomk` 不加载任何子模块、`yomk.__file__ is None`）。第三方扩展依赖
该机制与框架合并，因此：

- 扩展仓库**不得创建** `yomk/__init__.py`、`yomk/msg/__init__.py` 或 `yomk/url/__init__.py`
  （install.sh 安装前会做守卫检查）；
- 只新增自己的叶子目录（本模板为 `yomk/msg/extension/`、`yomk/extension/` 与 `yomk/url/extension/`）；
- 扩展域的 `__init__.py` 内自带版本常量（框架各域由构建期注入
  `_version.py`，扩展可简化为普通常量）。

## 第三方运行时服务纪律

框架只拉起 5 个内置服务，**不做隐式发现**。扩展服务由使用方显式注册：

```python
from yomk.server import server
from yomk.extension import ExtensionService

server().async_thread(1).init()
server().srv(ExtensionService).create()   # 显式注册后 /ExtensionService/* 才可达
```

## 安装与验证

```bash
bash install.sh        # 交互输入框架安装路径（默认取 YOMK_PREFIX_PATH）
python3 test/test_extension.py
```

安装后可用：

```python
from yomk.msg.extension import ExtensionRequest, ExtensionResponse   # 扩展消息
from yomk.extension import ExtensionService, extension         # 扩展运行时 + extension 域 builder
from yomk.url.extension import EXTENSION_ECHO                  # 扩展 URL 常量（并入框架 yomk.url 常量池）

server().url(EXTENSION_ECHO).msg(ExtensionRequest("hi")).request()
extension().version()   # 扩展版本查询终结（对齐 C++ ExtensionApi.h extVersion()，返回 Response）
```

extension 域 builder 与框架各域同构，遵循三条 API 形态纪律：

1. **builder 是模块自己的 API**——只封装本模块自有接口（模板仅版本查询终结
   `extension().version()`），不重复框架通用请求链路；新增业务终结方法时在
   `yomk/extension/builder.py` 按同风格追加。
2. **builder 可省略**（可选件）：不提供时须在运行时域 `__init__` 或 URL 常量池
   注释中写明统一请求调用形态，并说明各 URL 常量的功能作用。
3. **推荐优先使用统一请求接口**（框架封装好的统一请求形态，屏蔽 URL 之下的路由细节）：

```python
server().url(EXTENSION_ECHO).msg(ExtensionRequest("hi")).request()
```
