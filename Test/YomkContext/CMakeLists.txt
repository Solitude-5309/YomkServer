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

# 测试程序仅供框架内部开发验证使用，不执行 install
