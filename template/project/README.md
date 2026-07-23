# ProjectName

基于 [YomkServerPy](https://github.com/Solitude-5309/YomkServerPy) 模块化框架的工程。  
YomkServerPy 是基于 Python 的模块化服务开发框架，核心设计理念：**「一切皆服务，一切皆请求」**。

## 前置条件

- Python >= 3.10
- YomkServerPy 已安装（`PYTHONPATH` 指向安装路径下的 `lib/pythonX.Y/site-packages`）

## 运行

```bash
export PYTHONPATH=~/YomkServer/install/lib/python3.10/site-packages:$PYTHONPATH
python main.py
```

## 工程结构

| 目录 | 职责 |
|------|------|
| `boot/` | 生命周期管理（before/start/after） |
| `config/` | 配置文件 |
| `msgs/` | 消息包定义 |
| `services/` | 服务实现 |
| `typedefine/` | 公共常量定义 |
| `test/` | 单元测试脚本 |
| `scripts/` | 项目辅助脚本 |
