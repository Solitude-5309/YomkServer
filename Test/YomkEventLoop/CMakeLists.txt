include_directories(${YomkServer_INCLUDE})
include_directories(${TestYomkEventLoop_DIR}/YomkEventLoop)
# 生命周期白盒测试需直连内部类 EventLoop，引用非导出内部头需要源码目录
include_directories(${YomkServer_DIR}/src)

set(YomkServerLib 
    YomkServer
)

# -------------sl TestYomkEventLoopLifecycle ----------------
set(TestYomkEventLoopLifecycle_SOURCES 
    ${TestYomkEventLoop_DIR}/YomkEventLoop/TestYomkEventLoopLifecycle.cpp
)
add_executable(TestYomkEventLoopLifecycle 
    ${TestYomkEventLoopLifecycle_SOURCES}
)
target_link_libraries(TestYomkEventLoopLifecycle PRIVATE
    ${YomkServerLib}
)
# -------------el TestYomkEventLoopLifecycle ----------------

# -------------sl TestYomkEventLoopConcurrency ----------------
set(TestYomkEventLoopConcurrency_SOURCES 
    ${TestYomkEventLoop_DIR}/YomkEventLoop/TestYomkEventLoopConcurrency.cpp
)
add_executable(TestYomkEventLoopConcurrency 
    ${TestYomkEventLoopConcurrency_SOURCES}
)
target_link_libraries(TestYomkEventLoopConcurrency PRIVATE
    ${YomkServerLib}
)
# -------------el TestYomkEventLoopConcurrency ----------------

# -------------sl TestYomkEventLoopStress ----------------
set(TestYomkEventLoopStress_SOURCES 
    ${TestYomkEventLoop_DIR}/YomkEventLoop/TestYomkEventLoopStress.cpp
)
add_executable(TestYomkEventLoopStress 
    ${TestYomkEventLoopStress_SOURCES}
)
target_link_libraries(TestYomkEventLoopStress PRIVATE
    ${YomkServerLib}
)
# -------------el TestYomkEventLoopStress ----------------

# 测试程序仅供框架内部开发验证使用，不执行 install
