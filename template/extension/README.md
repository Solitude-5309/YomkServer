# TemplateExtension 扩展

基于 [YomkServerPy](https://github.com/Solitude-5309/YomkServerPy) 框架的扩展模板，提供版本查询接口，可作为新扩展的开发起点。

## 功能

| URL | 功能 | 说明 |
|-----|------|------|
| `/ExtensionService/version` | 版本查询 | 返回扩展版本信息 |

## 前置条件

- Python >= 3.10
- YomkServerPy 已安装到系统

## 安装

```bash
bash install.sh            # 交互式，前置路径默认取 $YOMK_PREFIX_PATH，可交互修改
```

> 扩展与 YomkServerPy 安装到一起：扩展包装入框架安装路径下的 site-packages（已在 PYTHONPATH 内），安装后任意终端可 `import TemplateExtension`。
> 测试程序不随扩展安装，就地在模板目录下 `test/` 运行验证。
> 若未配置 `YOMK_PREFIX_PATH` 环境变量，脚本会提示警告，此时手动输入 YomkServerPy 安装路径即可。

## 工程结构

```
TemplateExtension/
├── TemplateExtension/
│   ├── __init__.py          # 包入口（re-export 扩展版本号与服务类）
│   ├── version.py           # 扩展版本号 EXTENSION_VERSION（单一来源）
│   └── extension_service.py # 服务实现
├── test/
│   └── test_template_extension.py  # 测试程序（就地运行，不随扩展安装）
├── install.sh               # 扩展安装脚本（交互式）
└── README.md
```

## 使用示例

将以下完整程序拷贝为 main.py，安装扩展后可直接运行：

```python
import YomkApi
from TemplateExtension import ExtensionService

YomkApi.init()

# 注册 ExtensionService（扩展服务需先注册才能使用）
YomkApi.new_service(ExtensionService, "/ExtensionService")

# 版本查询请求
resp = YomkApi.request("/ExtensionService/version", None)
if resp.status == YomkApi.ResStatus.eOk:
    print(f"version: {resp.data}")  # 输出: TemplateExtension v0.0.1
```
