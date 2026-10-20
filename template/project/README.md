# TemplateProject

基于 [YomkServerPy](https://github.com/Solitude-5309/YomkServerPy) 模块化框架的空白工程模板。

YomkServerPy 是基于 Python 的模块化服务开发框架，核心设计理念：**「一切皆服务，一切皆请求」**。

## 前置条件

- Python >= 3.10
- YomkServerPy 已安装到系统

### 安装 YomkServerPy

```bash
# 在 YomkServerPy 源码目录下，一键编译安装（默认安装到 /opt/yomk，可交互自定义路径）
./build_ubuntu.sh
```

安装后会自动配置环境变量 `YOMK_PREFIX_PATH` 指向安装路径，并将 `PYTHONPATH` 指向安装路径下的 site-packages（任意终端可 `import YomkApi`）。

## 工程结构

```
TemplateProject/
├── main.py                 # 程序入口
├── boot/
│   └── MyBoot.py           # 生命周期管理
├── config/
│   └── config.json         # 配置文件
├── msgs/
│   └── YomkMsgs.py         # 消息包定义
├── services/               # 服务实现（按业务分子目录）
│   └── ConfigService.py
├── typedefine/
│   └── TypeDefine.py       # 公共常量定义（含工程版本号 APP_VERSION）
├── test/                   # 单元测试文件
├── scripts/                # 项目辅助脚本
├── check_env.sh            # 环境检查脚本
└── README.md
```

| 目录 | 职责 |
|------|------|
| `boot/` | 程序生命周期管理（before/start/after），路径推导、Context 创建、按需启动服务 |
| `config/` | 配置文件目录 |
| `msgs/` | 所有服务间通信的消息包定义集中管理 |
| `services/` | 所有服务实现，按业务类别分子目录存放 |
| `typedefine/` | 常量定义、宏定义、类型别名等公共定义 |

## Linux 运行

```bash
bash check_env.sh          # 检查 YomkServerPy 运行环境（PYTHONPATH 由框架安装环节配置）
python main.py
```

`check_env.sh` 检查 python3 可用性并验证 `import YomkApi`，输出框架版本；失败时提示 PYTHONPATH 应指向的 site-packages 路径。

## 生命周期说明

`MyBoot` 管理程序三阶段生命周期：

| 阶段 | 方法 | 用途 |
|------|------|------|
| 启动前 | `before()` | 创建 Context、EventLoop、注册 FunctionPool、设置日志代理等 |
| 启动中 | `start()` | 注册并启动服务 |
| 启动后 | `after()` | 调用服务接口做初始化、自启动任务等 |

## 核心组件速查

| 组件 | 用途 | YomkApi 接口 |
|------|------|--------------|
| YomkContext | 全局 K-V 状态管理 | `context_create` / `context_get` / `context_set` / `context_destroy` |
| YomkEventLoop | 线程隔离事件循环 | `event_loop_start` / `event_loop_post` / `event_loop_post_wait` |
| YomkFunctionPool | 动态公共函数池 | `function_pool_register` / `function_pool_call` |
| YomkLogger | 多级别日志系统 | `console_log_info` / `console_log_error` / `file_log_create` |
