include_directories(${YomkServer_INCLUDE})
include_directories(${ExamplesYomkServer_DIR})

set(YomkServerLib 
    YomkServer
)

# -------------sl ExampleYomkService ----------------
set(ExampleYomkService_SOURCES 
    ${ExamplesYomkServer_DIR}/ExampleYomkService.cpp
    ${ExamplesYomkServer_DIR}/boot/ArmBoot.cpp
    ${ExamplesYomkServer_DIR}/services/MotionService.cpp
    ${ExamplesYomkServer_DIR}/services/TelemetryService.cpp
)
add_executable(ExampleYomkService 
    ${ExampleYomkService_SOURCES}
)
target_link_libraries(ExampleYomkService PRIVATE
    ${YomkServerLib}
)
# -------------el ExampleYomkService ----------------

# -------------sl ExampleYomkFunctionPool ----------------
set(ExampleYomkFunctionPool_SOURCES 
    ${ExamplesYomkServer_DIR}/ExampleYomkFunctionPool.cpp
)
add_executable(ExampleYomkFunctionPool 
    ${ExampleYomkFunctionPool_SOURCES}
)
target_link_libraries(ExampleYomkFunctionPool PRIVATE
    ${YomkServerLib}
)
# -------------el ExampleYomkFunctionPool ----------------

# -------------sl ExampleYomkContext ----------------
set(ExampleYomkContext_SOURCES 
    ${ExamplesYomkServer_DIR}/ExampleYomkContext.cpp
)
add_executable(ExampleYomkContext 
    ${ExampleYomkContext_SOURCES}
)
target_link_libraries(ExampleYomkContext PRIVATE
    ${YomkServerLib}
)
# -------------el ExampleYomkContext ----------------

# -------------sl ExampleYomkEventLoop ----------------
set(ExampleYomkEventLoop_SOURCES 
    ${ExamplesYomkServer_DIR}/ExampleYomkEventLoop.cpp
)
add_executable(ExampleYomkEventLoop 
    ${ExampleYomkEventLoop_SOURCES}
)
target_link_libraries(ExampleYomkEventLoop PRIVATE
    ${YomkServerLib}
)
# -------------el ExampleYomkEventLoop ----------------

# -------------sl ExampleYomkLogger ----------------
set(ExampleYomkLogger_SOURCES 
    ${ExamplesYomkServer_DIR}/ExampleYomkLogger.cpp
)
add_executable(ExampleYomkLogger 
    ${ExampleYomkLogger_SOURCES}
)
target_link_libraries(ExampleYomkLogger PRIVATE
    ${YomkServerLib}
)
# -------------el ExampleYomkLogger ----------------

# -------------sl ExampleYomkServerInfo ----------------
set(ExampleYomkServerInfo_SOURCES 
    ${ExamplesYomkServer_DIR}/ExampleYomkServerInfo.cpp
)
add_executable(ExampleYomkServerInfo 
    ${ExampleYomkServerInfo_SOURCES}
)
target_link_libraries(ExampleYomkServerInfo PRIVATE
    ${YomkServerLib}
)
# -------------el ExampleYomkServerInfo ----------------

# 安装示例可执行文件到 <prefix>/bin，供用户在任意终端运行验证安装
install(TARGETS
    ExampleYomkService
    ExampleYomkFunctionPool
    ExampleYomkContext
    ExampleYomkEventLoop
    ExampleYomkLogger
    ExampleYomkServerInfo
    RUNTIME DESTINATION bin
)
