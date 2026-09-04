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

# 测试程序仅供框架内部开发验证使用，不执行 install
