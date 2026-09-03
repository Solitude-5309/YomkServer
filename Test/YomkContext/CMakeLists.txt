include_directories(${YomkServer_INCLUDE})
include_directories(${TestYomkContext_DIR}/YomkContext)

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

# 测试程序仅供框架内部开发验证使用，不执行 install
