# Test/YomkServer：YomkServer 非 Modules 源文件（YomkServer/YomkService/YomkSimpleThreadPool/YomkAPI）
# 的正式全面覆盖测试；目录内扁平布局，每个被测主题一个独立可执行程序，目标按闭环增量追加
# 引用非导出内部头（如 YomkSimpleThreadPool.h）需要源码目录
include_directories(${YomkServer_DIR}/src)

# -------------sl TestYomkSimpleThreadPool ----------------
set(TestYomkSimpleThreadPool_SOURCES 
    ${TestYomkServer_DIR}/YomkServer/TestYomkSimpleThreadPool.cpp
)
add_executable(TestYomkSimpleThreadPool 
    ${TestYomkSimpleThreadPool_SOURCES}
)
target_link_libraries(TestYomkSimpleThreadPool PRIVATE
    ${YomkServerLib}
)
# -------------el TestYomkSimpleThreadPool ----------------

# -------------sl TestYomkService ----------------
set(TestYomkService_SOURCES 
    ${TestYomkServer_DIR}/YomkServer/TestYomkService.cpp
)
add_executable(TestYomkService 
    ${TestYomkService_SOURCES}
)
target_link_libraries(TestYomkService PRIVATE
    ${YomkServerLib}
)
# -------------el TestYomkService ----------------

# -------------sl TestYomkWeakFunc ----------------
set(TestYomkWeakFunc_SOURCES 
    ${TestYomkServer_DIR}/YomkServer/TestYomkWeakFunc.cpp
)
add_executable(TestYomkWeakFunc 
    ${TestYomkWeakFunc_SOURCES}
)
target_link_libraries(TestYomkWeakFunc PRIVATE
    ${YomkServerLib}
)
# -------------el TestYomkWeakFunc ----------------

# -------------sl TestYomkServer ----------------
set(TestYomkServer_SOURCES 
    ${TestYomkServer_DIR}/YomkServer/TestYomkServer.cpp
)
add_executable(TestYomkServer 
    ${TestYomkServer_SOURCES}
)
target_link_libraries(TestYomkServer PRIVATE
    ${YomkServerLib}
)
# -------------el TestYomkServer ----------------

# -------------sl TestYomkShutdownSeq ----------------
set(TestYomkShutdownSeq_SOURCES 
    ${TestYomkServer_DIR}/YomkServer/TestYomkShutdownSeq.cpp
)
add_executable(TestYomkShutdownSeq 
    ${TestYomkShutdownSeq_SOURCES}
)
target_link_libraries(TestYomkShutdownSeq PRIVATE
    ${YomkServerLib}
)
# -------------el TestYomkShutdownSeq ----------------

# 测试程序仅供框架内部开发验证使用，不执行 install
