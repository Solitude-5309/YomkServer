# YomkServerPy 简介

# YomkServerPy

## 设计哲学

1. 关注点分离：每个服务专注于单一职责
2. 约定优于配置：合理的默认值减少配置复杂度
3. 渐进式复杂度：从简单单体到复杂分布式系统的平滑演进
4. 开发者友好：直观的API设计和丰富的文档支持

## 模块化服务模型
两级模块化：  
1. YomkService：基础服务模块，封装独立业务域或技术组件
2. Function：服务模块内具体功能单元，通过唯一URL路径标识和访问

模块间通信：统一的 Request/Response 模型，支持同步与异步调用模式

## 技术特点

### 开发效率

1. 模块化设计：清晰的服务边界，支持并行开发
2. 快速集成：开箱即用的基础组件，减少重复工作
3. 敏捷迭代：动态函数池支持业务逻辑的快速变更和部署

### 可维护性

1. 标准接口：统一的通信模式降低集成复杂度
2. 状态可见：上下文管理提供清晰的系统状态视图
3. 配置驱动：集中化的配置管理简化部署和运维

### 可扩展性

1. 松耦合架构：服务间依赖最小化，支持独立扩展
2. 水平扩展：事件循环模型天然支持并发扩展
3. 插件化机制：可动态加载和卸载服务模块

## 基础服务组件

### YomkContext - 上下文服务

1. 统一管理框架运行时的全局状态
2. 提供状态安全检查机制：在状态变更前执行验证逻辑，防止非法状态迁移
3. 状态监控功能：实时监听状态变化，触发回调响应
4. 支持状态的创建、获取、更新、删除等全生命周期管理

### YomkEventLoop - 线程隔离的事件循环

1. 线程隔离的事件循环实例，每个循环运行在独立线程中
2. 同一事件循环内保证顺序执行，不同循环间并行处理
3. 支持非阻塞事件投递（立即返回）与阻塞投递（等待执行完成）
4. 适用于I/O密集型和高并发任务调度

### YomkFunctionPool - 动态函数池

1. 统一函数注册与调度中心，为每个函数分配唯一标识符
2. 支持运行时函数注册、更新和热替换
3. 面向过程开发范式，实现业务逻辑的快速迭代和动态调整
4. 提升系统应对需求变化的响应速度

## 编译与安装

### Linux

#### 方式 1：自动安装脚本（推荐）

```bash
./build_ubuntu.sh
```

脚本将自动检查依赖（cmake/gcc/python3/python3-dev/cython/readerwriterlock，缺失时可选自动安装）并交互式完成配置、编译、安装（默认安装路径 `/opt/yomk`），随后调用 `scripts/install_env.sh` 设置环境变量 `YOMK_PREFIX_PATH` 与 `PYTHONPATH`——新开任意终端即可直接 `import YomkServerPy`（无需手动 source 或 export）。

#### 方式 2：手动 CMake 构建

与方式 1 产生完全一致的安装结果（同一套 install 规则 + 同一份安装后配置脚本）：

```bash
# 依赖
sudo apt update && sudo apt install gcc python3-dev cmake
pip install cython readerwriterlock

# 编译安装
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/opt/yomk
cmake --build . -j
sudo cmake --install .
cd ..
./scripts/install_env.sh /opt/yomk   # 安装校验 + YOMK_PREFIX_PATH/PYTHONPATH 持久化（与方式 1 共用）
```

说明：

- `-DCMAKE_BUILD_TYPE=Release` 必须显式传入：Linux Makefiles 单配置生成器不识别 `--config Release`，缺失时将以无优化参数编译，产物与方式 1 不同
- Python C 扩展与构建时解释器主次版本绑定，请确保运行解释器版本一致；可比对安装产物 `share/YomkServerPy/abi_report.txt` 确认构建环境一致（记录 Python/Cython/gcc/发行版等指纹，随库安装）

## windows 编译安装

### 依赖
python3
pip install cython readerwriterlock

### 编译
mkdir build  
cd build  
cmake .. -DCMAKE_INSTALL_PREFIX="C:/Users/solit/Env/YomkServer/install"  
cmake --build . --target install --config Release  

### 测试
cd test  
$env:PYTHONPATH = "C:/Users/solit/Env/YomkServer/install/lib/python3.12/site-packages;" + $env:PYTHONPATH   
python TestYomkService.py  
python TestYomkContext.py  
python TestYomkFunctionPool.py  
python TestYomkEventLoop.py  

---

## 测试（Linux）

### 1. 一键运行全量测试

```bash
./test/run_tests.sh                      # 使用 $YOMK_PREFIX_PATH（缺省 /opt/yomk）
./test/run_tests.sh --prefix /opt/yomk   # 指定安装路径
./test/run_tests.sh --timeout 600        # 单测试超时秒数（默认 300）
```

运行器行为：

- **自动环境**：自动探测 `lib/pythonX.Y/site-packages` 并设置 PYTHONPATH，无需手动 export
- **失败即停**：任一测试退出码非 0（含超时被杀）立即终止，终端输出该测试 `[FAIL]` 摘要与最近日志
- **日志落盘**：`test/test_logs/<时间戳>/` 下每个测试一份日志 + `summary.log` 汇总
- **超时保护**：每个测试由 `timeout` 包裹，防卡死

### 2. 单独运行

```bash
export PYTHONPATH=$YOMK_PREFIX_PATH/lib/pythonX.Y/site-packages:$PYTHONPATH
cd test
python3 TestYomkService.py
python3 TestYomkContext.py
python3 TestYomkFunctionPool.py
python3 TestYomkEventLoop.py
```

