# YomkMath 扩展

基于 [YomkServer](https://github.com/Solitude-5309/YomkServer) 框架的数学运算扩展服务，提供加减乘除四则运算功能。

## 功能

| URL | 功能 | 说明 |
|-----|------|------|
| `/MathService/add` | 加法 | 返回 a + b |
| `/MathService/sub` | 减法 | 返回 a - b |
| `/MathService/mul` | 乘法 | 返回 a * b |
| `/MathService/div` | 除法 | 返回 a / b，除数为零时返回错误 |

## 前置条件

- C++17 编译器
- CMake >= 3.14
- YomkServer 已安装

## 编译

```bash
source build_ubuntu.sh     # 交互式，前置路径与安装路径默认均取 $YOMK_PREFIX_PATH，可交互修改
```

> 扩展库需与 YomkServer 安装到一起：导出 target 不含 include 路径，头文件由 `YomkServer::YomkServer` 的 INTERFACE include 统一提供。
> 若未配置 `YOMK_PREFIX_PATH` 环境变量，脚本会提示警告，此时手动输入路径即可。

## 工程结构

```
YomkMath/
├── include/
│   └── MathService.h        # 服务头文件（消息包定义 + 类声明）
├── src/
│   └── MathService.cpp      # 服务实现
├── CMakeLists.txt            # CMake 构建配置
├── build_ubuntu.sh           # 一键编译脚本（交互式）
└── README.md
```

## 使用示例

```cpp
// 加法请求
YomkResponse resp = YOMK_REQUEST("/MathService/add", YomkMkPtr(YMathOp, MathOp{"add", 10.5, 3.2}));
if (resp.m_status == YomkResponse::eOk) {
    YomkUnPackPkg(resp.m_data, Float64, result);
    // result->d == 13.7
}
```
