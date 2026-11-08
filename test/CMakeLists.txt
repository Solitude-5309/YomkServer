include_directories(${YOMK_SERVER_INCLUDE_DIR})
include_directories(${TestYomkServer_DIR})

set(YomkServerLib 
    YomkServer
)

# test/YomkContext：Context 模块的正式全面覆盖测试（顶层按被测模块分目录组织）
set(TestYomkContext_DIR ${TestYomkServer_DIR})
include(${TestYomkContext_DIR}/YomkContext/CMakeLists.txt)

# test/YomkEventLoop：EventLoop 模块的正式全面覆盖测试（顶层按被测模块分目录组织）
set(TestYomkEventLoop_DIR ${TestYomkServer_DIR})
include(${TestYomkEventLoop_DIR}/YomkEventLoop/CMakeLists.txt)

# test/YomkFunctionPool：FunctionPool 模块的正式全面覆盖测试（顶层按被测模块分目录组织）
set(TestYomkFunctionPool_DIR ${TestYomkServer_DIR})
include(${TestYomkFunctionPool_DIR}/YomkFunctionPool/CMakeLists.txt)

# test/YomkLogger：Logger 模块的正式全面覆盖测试（顶层按被测模块分目录组织）
set(TestYomkLogger_DIR ${TestYomkServer_DIR})
include(${TestYomkLogger_DIR}/YomkLogger/CMakeLists.txt)

# test/YomkServerInfo：ServerInfo 模块的正式全面覆盖测试（顶层按被测模块分目录组织）
set(TestYomkServerInfo_DIR ${TestYomkServer_DIR})
include(${TestYomkServerInfo_DIR}/YomkServerInfo/CMakeLists.txt)

# test/YomkServer：YomkServer 非 Modules 源文件的正式全面覆盖测试（顶层按被测模块分目录组织）
include(${TestYomkServer_DIR}/YomkServer/CMakeLists.txt)

# 测试程序仅供框架内部开发验证使用，不执行 install
