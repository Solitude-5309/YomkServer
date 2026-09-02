include_directories(${YomkServer_INCLUDE})
include_directories(${TestYomkServer_DIR})

set(YomkServerLib 
    YomkServer
)

# Test/YomkServer：YomkServer 非 Modules 源文件的正式全面覆盖测试（顶层按被测模块分目录组织）
include(${TestYomkServer_DIR}/YomkServer/CMakeLists.txt)

# 测试程序仅供框架内部开发验证使用，不执行 install
