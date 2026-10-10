# Test/YomkServerInfo：ServerInfo 模块的正式全面覆盖测试（顶层按被测模块分目录组织）

include_directories(${YomkServer_INCLUDE})
# S0 server destroyed 分支需白盒直连 YomkServerInfo 类（绕过路由触达 weakServer.lock 失败路径），
# 引用非导出内部头需要源码目录
include_directories(${YomkServer_DIR}/src)

set(YomkServerLib
    YomkServer
)

# -------------sl TestYomkServerInfoLifecycle ----------------
set(TestYomkServerInfoLifecycle_SOURCES
    ${TestYomkServerInfo_DIR}/YomkServerInfo/TestYomkServerInfoLifecycle.cpp
)
add_executable(TestYomkServerInfoLifecycle
    ${TestYomkServerInfoLifecycle_SOURCES}
)
target_link_libraries(TestYomkServerInfoLifecycle PRIVATE
    ${YomkServerLib}
)
# -------------el TestYomkServerInfoLifecycle ----------------

# 测试程序仅供框架内部开发验证使用，不执行 install
