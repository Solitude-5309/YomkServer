include_directories(${YomkServer_INCLUDE})
include_directories(${TestYomkServer_DIR}/src)

set(YomkServerLib 
    YomkServer
)

# -------------sl TestYomkSettings ----------------
set(TestYomkSettings_SOURCES 
    ${TestYomkServer_DIR}/TestYomkSettings.cpp
)
add_executable(TestYomkSettings 
    ${TestYomkSettings_SOURCES}
)
target_link_libraries(TestYomkSettings PRIVATE
    ${YomkServerLib}
)
# -------------el TestYomkSettings ----------------

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