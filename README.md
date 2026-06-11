# YomkServerPy 简介

## 快速编译
chmod +x build.sh  
./build.sh

## build 交互
安装目录：~/YomkRpc2/install  
编译类型：release

## 运行test
1. export PYTHONPATH=$HOME/YomkServer/install/lib/python3.10/site-packages:$PYTHONPATH 【根据实际安装目录编写】
2. cd YomkServerPy/test
3. python3 TestYomkService.py
4. python3 TestYomkContext.py
5. python3 TestYomkFunctionPool.py

## 手动编译

### 依赖
sudo apt update && sudo apt install gcc python3-dev  
pip install cython readerwriterlock

### 编译YomkServerPy
3. cd YomkServerPy
3. mkdir build && cd build
4. cmake .. -DCMAKE_INSTALL_PREFIX=~/YomkServer/install
5. cmake --build . --target install --config Release


## TODO
1. 强类型优化
2. 日志优化

