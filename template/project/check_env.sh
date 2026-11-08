#!/bin/bash
# 环境检查脚本
# 用法: bash check_env.sh
# 检查 YomkServerPy 运行环境（PYTHONPATH 由框架安装环节配置，本脚本只做检查）

echo "== YomkServerPy 环境检查 =="

if ! command -v python3 >/dev/null 2>&1; then
    echo "错误: 未找到 python3"
    exit 1
fi
echo "-- python3: $(python3 --version 2>&1)"

if python3 -c "import yomk.server" 2>/dev/null; then
    echo "-- yomk 命名空间锚点: yomk.__file__ = $(python3 -c 'import yomk; print(yomk.__file__)')（None 即 PEP 420 命名空间包正常形态）"
    echo "-- 框架版本: $(python3 -c 'import yomk.server; print(yomk.server.__version__)')"
    echo "环境检查通过"
else
    echo "错误: 无法导入 yomk.server（PYTHONPATH 未指向 YomkServerPy 安装路径下的 site-packages）"
    echo "提示: 确认框架已安装；若 YOMK_PREFIX_PATH 已配置，检查 PYTHONPATH 是否包含:"
    echo "  ${YOMK_PREFIX_PATH:-/opt/yomk}/lib/pythonX.Y/site-packages"
    exit 1
fi
