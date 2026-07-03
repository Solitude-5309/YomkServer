# YomkServerPy 简介

## linux

### 快速编译
chmod +x build.sh  
./build.sh

### build 交互
安装目录：~/YomkServer/install  
编译类型：release

### 运行test
export PYTHONPATH=$HOME/YomkServer/install/lib/python3.10/site-packages:$PYTHONPATH 【根据实际安装目录编写】  
cd YomkServerPy/test  
python3 TestYomkService.py  
python3 TestYomkContext.py  
python3 TestYomkFunctionPool.py  
python3 TestYomkEventLoop.py  

### 手动编译

#### 依赖
sudo apt update && sudo apt install gcc python3-dev  
pip install cython readerwriterlock

#### 编译
cd YomkServerPy  
mkdir build && cd build  
cmake .. -DCMAKE_INSTALL_PREFIX=~/YomkServer/install  
cmake --build . --target install --config Release  

## windows

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
python3 TestYomkContext.py  
python3 TestYomkFunctionPool.py  
python3 TestYomkEventLoop.py  

