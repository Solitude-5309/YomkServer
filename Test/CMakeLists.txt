include_directories(${YomkServer_INCLUDE})
include_directories(${TestYomkServer_DIR})

set(YomkServerLib 
    YomkServer
)

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

# -------------sl TestYomkContextInfo ----------------
set(TestYomkContextInfo_SOURCES 
    ${TestYomkServer_DIR}/TestYomkContextInfo.cpp
)
add_executable(TestYomkContextInfo 
    ${TestYomkContextInfo_SOURCES}
)
target_link_libraries(TestYomkContextInfo PRIVATE
    ${YomkServerLib}
)
# -------------el TestYomkContextInfo ----------------

# -------------sl TestYomkEventLoopInfo ----------------
set(TestYomkEventLoopInfo_SOURCES 
    ${TestYomkServer_DIR}/TestYomkEventLoopInfo.cpp
)
add_executable(TestYomkEventLoopInfo 
    ${TestYomkEventLoopInfo_SOURCES}
)
target_link_libraries(TestYomkEventLoopInfo PRIVATE
    ${YomkServerLib}
)
# -------------el TestYomkEventLoopInfo ----------------

# -------------sl TestYomkFunctionPoolInfo ----------------
set(TestYomkFunctionPoolInfo_SOURCES 
    ${TestYomkServer_DIR}/TestYomkFunctionPoolInfo.cpp
)
add_executable(TestYomkFunctionPoolInfo 
    ${TestYomkFunctionPoolInfo_SOURCES}
)
target_link_libraries(TestYomkFunctionPoolInfo PRIVATE
    ${YomkServerLib}
)
# -------------el TestYomkFunctionPoolInfo ----------------

# -------------sl TestYomkLoggerInfo ----------------
set(TestYomkLoggerInfo_SOURCES 
    ${TestYomkServer_DIR}/TestYomkLoggerInfo.cpp
)
add_executable(TestYomkLoggerInfo 
    ${TestYomkLoggerInfo_SOURCES}
)
target_link_libraries(TestYomkLoggerInfo PRIVATE
    ${YomkServerLib}
)
# -------------el TestYomkLoggerInfo ----------------

# -------------sl TestYomkShutdown ----------------
set(TestYomkShutdown_SOURCES 
    ${TestYomkServer_DIR}/TestYomkShutdown.cpp
)
add_executable(TestYomkShutdown 
    ${TestYomkShutdown_SOURCES}
)
target_link_libraries(TestYomkShutdown PRIVATE
    ${YomkServerLib}
)
# -------------el TestYomkShutdown ----------------

# 测试程序仅供框架内部开发验证使用，不执行 install
