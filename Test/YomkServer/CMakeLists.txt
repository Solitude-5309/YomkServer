include_directories(${YomkServer_INCLUDE})
include_directories(${TestYomkServer_DIR})

set(YomkServerLib 
    YomkServer
)

# -------------sl TestYomkService ----------------
set(TestYomkService_SOURCES 
    ${TestYomkServer_DIR}/TestYomkService.cpp
    ${TestYomkServer_DIR}/boot/MyBoot.cpp
    ${TestYomkServer_DIR}/services/YomkServiceA.cpp
    ${TestYomkServer_DIR}/services/YomkServiceB.cpp
)
add_executable(TestYomkService 
    ${TestYomkService_SOURCES}
)
target_link_libraries(TestYomkService PRIVATE
    ${YomkServerLib}
)
# -------------el TestYomkService ----------------

# -------------sl TestYomkFunctionPool ----------------
set(TestYomkFunctionPool_SOURCES 
    ${TestYomkServer_DIR}/TestYomkFunctionPool.cpp
)
add_executable(TestYomkFunctionPool 
    ${TestYomkFunctionPool_SOURCES}
)
target_link_libraries(TestYomkFunctionPool PRIVATE
    ${YomkServerLib}
)
# -------------el TestYomkFunctionPool ----------------

# -------------sl TestYomkContext ----------------
set(TestYomkContext_SOURCES 
    ${TestYomkServer_DIR}/TestYomkContext.cpp
)
add_executable(TestYomkContext 
    ${TestYomkContext_SOURCES}
)
target_link_libraries(TestYomkContext PRIVATE
    ${YomkServerLib}
)
# -------------el TestYomkContext ----------------

# -------------sl TestYomkEventLoop ----------------
set(TestYomkEventLoop_SOURCES 
    ${TestYomkServer_DIR}/TestYomkEventLoop.cpp
)
add_executable(TestYomkEventLoop 
    ${TestYomkEventLoop_SOURCES}
)
target_link_libraries(TestYomkEventLoop PRIVATE
    ${YomkServerLib}
)
# -------------el TestYomkEventLoop ----------------

# -------------sl TestYomkLogger ----------------
set(TestYomkLogger_SOURCES 
    ${TestYomkServer_DIR}/TestYomkLogger.cpp
)
add_executable(TestYomkLogger 
    ${TestYomkLogger_SOURCES}
)
target_link_libraries(TestYomkLogger PRIVATE
    ${YomkServerLib}
)
# -------------el TestYomkLogger ----------------

# -------------sl TestYomkServerInfo ----------------
set(TestYomkServerInfo_SOURCES 
    ${TestYomkServer_DIR}/TestYomkServerInfo.cpp
)
add_executable(TestYomkServerInfo 
    ${TestYomkServerInfo_SOURCES}
)
target_link_libraries(TestYomkServerInfo PRIVATE
    ${YomkServerLib}
)
# -------------el TestYomkServerInfo ----------------

# 安装测试可执行文件到 <prefix>/bin，供用户在任意终端运行验证安装
install(TARGETS
    TestYomkService
    TestYomkFunctionPool
    TestYomkContext
    TestYomkEventLoop
    TestYomkLogger
    TestYomkServerInfo
    RUNTIME DESTINATION bin
)