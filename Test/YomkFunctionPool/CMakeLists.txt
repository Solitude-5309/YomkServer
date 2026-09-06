# Test/YomkFunctionPool：FunctionPool 模块的正式全面覆盖测试（顶层按被测模块分目录组织）

include_directories(${YomkServer_INCLUDE})

# -------------sl TestYomkFunctionPoolLifecycle ----------------
set(TestYomkFunctionPoolLifecycle_SOURCES 
    ${TestYomkFunctionPool_DIR}/YomkFunctionPool/TestYomkFunctionPoolLifecycle.cpp
)
add_executable(TestYomkFunctionPoolLifecycle 
    ${TestYomkFunctionPoolLifecycle_SOURCES}
)
target_link_libraries(TestYomkFunctionPoolLifecycle PRIVATE
    ${YomkServerLib}
)
# -------------el TestYomkFunctionPoolLifecycle ----------------

# -------------sl TestYomkFunctionPoolConcurrency ----------------
set(TestYomkFunctionPoolConcurrency_SOURCES 
    ${TestYomkFunctionPool_DIR}/YomkFunctionPool/TestYomkFunctionPoolConcurrency.cpp
)
add_executable(TestYomkFunctionPoolConcurrency 
    ${TestYomkFunctionPoolConcurrency_SOURCES}
)
target_link_libraries(TestYomkFunctionPoolConcurrency PRIVATE
    ${YomkServerLib}
)
# -------------el TestYomkFunctionPoolConcurrency ----------------

# -------------sl TestYomkFunctionPoolStress ----------------
set(TestYomkFunctionPoolStress_SOURCES 
    ${TestYomkFunctionPool_DIR}/YomkFunctionPool/TestYomkFunctionPoolStress.cpp
)
add_executable(TestYomkFunctionPoolStress 
    ${TestYomkFunctionPoolStress_SOURCES}
)
target_link_libraries(TestYomkFunctionPoolStress PRIVATE
    ${YomkServerLib}
)
# -------------el TestYomkFunctionPoolStress ----------------

# 测试程序仅供框架内部开发验证使用，不执行 install
