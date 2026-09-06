# Test/YomkLogger：Logger 模块的正式全面覆盖测试（顶层按被测模块分目录组织）

include_directories(${YomkServer_INCLUDE})
# ConsoleLogger/FileLogger 为库内部实现头（符号已从 so 导出），直连白盒测试需源码模块 include 路径
include_directories(${YomkServer_DIR}/src/Modules/Logger)

# -------------sl TestYomkLoggerLifecycle ----------------
set(TestYomkLoggerLifecycle_SOURCES 
    ${TestYomkLogger_DIR}/YomkLogger/TestYomkLoggerLifecycle.cpp
)
add_executable(TestYomkLoggerLifecycle 
    ${TestYomkLoggerLifecycle_SOURCES}
)
target_link_libraries(TestYomkLoggerLifecycle PRIVATE
    ${YomkServerLib}
)
# -------------el TestYomkLoggerLifecycle ----------------

# -------------sl TestYomkLoggerDirect ----------------
set(TestYomkLoggerDirect_SOURCES 
    ${TestYomkLogger_DIR}/YomkLogger/TestYomkLoggerDirect.cpp
)
add_executable(TestYomkLoggerDirect 
    ${TestYomkLoggerDirect_SOURCES}
)
target_link_libraries(TestYomkLoggerDirect PRIVATE
    ${YomkServerLib}
)
# -------------el TestYomkLoggerDirect ----------------

# -------------sl TestYomkLoggerConcurrency ----------------
set(TestYomkLoggerConcurrency_SOURCES 
    ${TestYomkLogger_DIR}/YomkLogger/TestYomkLoggerConcurrency.cpp
)
add_executable(TestYomkLoggerConcurrency 
    ${TestYomkLoggerConcurrency_SOURCES}
)
target_link_libraries(TestYomkLoggerConcurrency PRIVATE
    ${YomkServerLib}
)
# -------------el TestYomkLoggerConcurrency ----------------

# 测试程序仅供框架内部开发验证使用，不执行 install
