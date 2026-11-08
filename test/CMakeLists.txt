include_directories(${YOMK_SERVER_INCLUDE_DIR})
include_directories(${TestYomkServer_DIR})

set(YomkServerLib 
    YomkServer
)

# test/yomk_context：Context 模块的正式全面覆盖测试（顶层按被测模块分目录组织）
set(TestYomkContext_DIR ${TestYomkServer_DIR})
include(${TestYomkContext_DIR}/yomk_context/CMakeLists.txt)

# test/yomk_event_loop：EventLoop 模块的正式全面覆盖测试（顶层按被测模块分目录组织）
set(TestYomkEventLoop_DIR ${TestYomkServer_DIR})
include(${TestYomkEventLoop_DIR}/yomk_event_loop/CMakeLists.txt)

# test/yomk_function_pool：FunctionPool 模块的正式全面覆盖测试（顶层按被测模块分目录组织）
set(TestYomkFunctionPool_DIR ${TestYomkServer_DIR})
include(${TestYomkFunctionPool_DIR}/yomk_function_pool/CMakeLists.txt)

# test/yomk_logger：Logger 模块的正式全面覆盖测试（顶层按被测模块分目录组织）
set(TestYomkLogger_DIR ${TestYomkServer_DIR})
include(${TestYomkLogger_DIR}/yomk_logger/CMakeLists.txt)

# test/yomk_server_info：ServerInfo 模块的正式全面覆盖测试（顶层按被测模块分目录组织）
set(TestYomkServerInfo_DIR ${TestYomkServer_DIR})
include(${TestYomkServerInfo_DIR}/yomk_server_info/CMakeLists.txt)

# test/yomk_server：YomkServer 非 Modules 源文件的正式全面覆盖测试（顶层按被测模块分目录组织）
include(${TestYomkServer_DIR}/yomk_server/CMakeLists.txt)

# 测试程序仅供框架内部开发验证使用，不执行 install
