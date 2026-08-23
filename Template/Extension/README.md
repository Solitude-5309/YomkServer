# YomkExtension 扩展

基于 [YomkServer](https://github.com/Solitude-5309/YomkServer) 框架的扩展模板，提供版本查询接口，可作为新扩展的开发起点。

## 功能

| URL | 功能 | 说明 |
|-----|------|------|
| `/ExtensionService/version` | 版本查询 | 返回扩展版本信息 |

## 前置条件

- C++17 编译器
- CMake >= 3.14
- YomkServer 已安装

## 编译

```bash
source build_ubuntu.sh     # 交互式，前置路径与安装路径默认均取 $YOMK_PREFIX_PATH，可交互修改
```

> 扩展库需与 YomkServer 安装到一起：导出 target 不含 include 路径，头文件由 `YomkServer::YomkServer` 的 INTERFACE include 统一提供。
> 测试程序随扩展一并安装到 `<安装路径>/bin`，安装后可直接运行 `TestYomkExtension` 验证。
> 若未配置 `YOMK_PREFIX_PATH` 环境变量，脚本会提示警告，此时手动输入路径即可。

## 工程结构

```
YomkExtension/
├── include/
│   └── ExtensionService.h   # 服务头文件（类声明）
├── src/
│   └── ExtensionService.cpp # 服务实现
├── test/
│   ├── CMakeLists.txt       # 测试构建配置（测试程序随扩展安装）
│   └── TestYomkExtension.cpp
├── cmake/
│   └── ProjectConfig.cmake.in
├── CMakeLists.txt           # CMake 构建配置
├── build_ubuntu.sh          # 一键编译脚本（交互式）
└── README.md
```

## 使用示例

将以下完整程序拷贝为 main.cpp，安装扩展后可直接编译运行：

```cpp
#include <YomkServer/YomkAPI.h>
#include <YomkExtension/ExtensionService.h>
#include <iostream>

using namespace yomk;

int main(int argc, char *argv[])
{
    YOMK_INIT();

    // 注册 ExtensionService（扩展服务需先注册才能使用）
    YOMK_NEW_SERVICE(ExtensionService);

    // 版本查询请求
    YomkResponse resp = YOMK_REQUEST("/ExtensionService/version", nullptr);
    if (resp.m_status == YomkResponse::eOk)
    {
        YomkUnPackPkg(resp.m_data, String, version);
        std::cout << "version: " << version->d << std::endl; // 输出: YomkExtension v0.0.1
    }

    return 0;
}
```
