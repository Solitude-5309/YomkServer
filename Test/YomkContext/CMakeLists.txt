include_directories(${YomkServer_INCLUDE})
include_directories(${TestYomkContext_DIR}/YomkContext)
# MC3：生命周期测试需构造/派生内部类 YomkContext，引用非导出内部头需要源码目录
include_directories(${YomkServer_DIR}/src)

set(YomkServerLib 
    YomkServer
)

# -------------sl TestYomkContextCRUD ----------------
set(TestYomkContextCRUD_SOURCES 
    ${TestYomkContext_DIR}/YomkContext/TestYomkContextCRUD.cpp
)
add_executable(TestYomkContextCRUD 
    ${TestYomkContextCRUD_SOURCES}
)
target_link_libraries(TestYomkContextCRUD PRIVATE
    ${YomkServerLib}
)
# -------------el TestYomkContextCRUD ----------------

# -------------sl TestYomkContextCheckerMonitor ----------------
set(TestYomkContextCheckerMonitor_SOURCES 
    ${TestYomkContext_DIR}/YomkContext/TestYomkContextCheckerMonitor.cpp
)
add_executable(TestYomkContextCheckerMonitor 
    ${TestYomkContextCheckerMonitor_SOURCES}
)
target_link_libraries(TestYomkContextCheckerMonitor PRIVATE
    ${YomkServerLib}
)
# -------------el TestYomkContextCheckerMonitor ----------------

# -------------sl TestYomkContextLifecycle ----------------
set(TestYomkContextLifecycle_SOURCES 
    ${TestYomkContext_DIR}/YomkContext/TestYomkContextLifecycle.cpp
)
add_executable(TestYomkContextLifecycle 
    ${TestYomkContextLifecycle_SOURCES}
)
target_link_libraries(TestYomkContextLifecycle PRIVATE
    ${YomkServerLib}
)
# -------------el TestYomkContextLifecycle ----------------

# -------------sl TestYomkContextConcurrency ----------------
set(TestYomkContextConcurrency_SOURCES 
    ${TestYomkContext_DIR}/YomkContext/TestYomkContextConcurrency.cpp
)
add_executable(TestYomkContextConcurrency 
    ${TestYomkContextConcurrency_SOURCES}
)
target_link_libraries(TestYomkContextConcurrency PRIVATE
    ${YomkServerLib}
)
# -------------el TestYomkContextConcurrency ----------------

# 测试程序仅供框架内部开发验证使用，不执行 install
