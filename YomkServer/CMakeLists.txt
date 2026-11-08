# 默认安装前缀：仅当用户未通过 -DCMAKE_INSTALL_PREFIX 显式指定时生效。
# CMake 启动时已将该缓存预置为内置默认（Windows: c:/Program Files/<project>，Unix: /usr/local），
# 不带 FORCE 的 set(CACHE) 遇已有缓存值不会覆盖，故必须用 FORCE + INITIALIZED_TO_DEFAULT 守卫。
if(WIN32)
    if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
        set(CMAKE_INSTALL_PREFIX "$ENV{LOCALAPPDATA}/YomkServer" CACHE PATH "Installation path" FORCE)
    endif()
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
else()
    if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
        set(CMAKE_INSTALL_PREFIX "/usr/local" CACHE PATH "Installation path" FORCE)
    endif()
endif()

set(INCLUDE_INSTALL_DIR include)
set(LIB_INSTALL_DIR lib)

include_directories(${YOMK_SERVER_INCLUDE_DIR})

set(YomkServer_SOURCES
    ${YOMK_SERVER_SRC_DIR}/Core/YomkServer.cpp
    ${YOMK_SERVER_SRC_DIR}/Core/YomkServerPrivate.cpp
    ${YOMK_SERVER_SRC_DIR}/Core/YomkService.cpp
    ${YOMK_SERVER_SRC_DIR}/Core/YomkServicePrivate.cpp
    ${YOMK_SERVER_SRC_DIR}/Core/YomkSimpleThreadPool.cpp
    ${YOMK_SERVER_SRC_DIR}/Modules/FunctionPool/YomkFunctionPool.cpp
    ${YOMK_SERVER_SRC_DIR}/Modules/Context/ContextGroup.cpp
    ${YOMK_SERVER_SRC_DIR}/Modules/Context/YomkContext.cpp
    ${YOMK_SERVER_SRC_DIR}/Modules/EventLoop/EventLoop.cpp
    ${YOMK_SERVER_SRC_DIR}/Modules/EventLoop/YomkEventLoop.cpp
    ${YOMK_SERVER_SRC_DIR}/Modules/Logger/YomkLogger.cpp
    ${YOMK_SERVER_SRC_DIR}/Modules/Logger/ConsoleLogger.cpp
    ${YOMK_SERVER_SRC_DIR}/Modules/Logger/FileLogger.cpp
    ${YOMK_SERVER_SRC_DIR}/Modules/ServerInfo/YomkServerInfo.cpp
)

add_library(YomkServer SHARED
    ${YomkServer_SOURCES}
)

target_compile_definitions(YomkServer PRIVATE YOMKSERVER_LIBRARY)

# 编译期注入框架版本号（对应顶层 project(Yomk VERSION x.x.x) 定义的 VERSION），供 YOMK_VERSION 获取
target_compile_definitions(YomkServer PUBLIC YOMKSERVER_VERSION="${PROJECT_VERSION}")  # 闭环 6：version() 终结进头文件内联，宏须对消费方可见

# 导出头文件
target_include_directories(YomkServer
    PUBLIC
        $<BUILD_INTERFACE:${YOMK_SERVER_INCLUDE_DIR}>  
        $<INSTALL_INTERFACE:include>
)

install(EXPORT YomkServerTargets
    FILE YomkServerTargets.cmake
    NAMESPACE YomkServer::  
    DESTINATION lib/cmake/YomkServer  
)

include(CMakePackageConfigHelpers)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/YomkServer/YomkServerConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/YomkServer/YomkServerConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/YomkServer/YomkServerConfig.cmake"
    INSTALL_DESTINATION lib/cmake/YomkServer
    PATH_VARS
        INCLUDE_INSTALL_DIR
        LIB_INSTALL_DIR
)

install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/YomkServer/YomkServerConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/YomkServer/YomkServerConfigVersion.cmake"
    DESTINATION lib/cmake/YomkServer
)

install(TARGETS YomkServer
    EXPORT YomkServerTargets         
    LIBRARY DESTINATION lib         
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include      
)

install(DIRECTORY ${YOMK_SERVER_INCLUDE_DIR}
    DESTINATION include              
    FILES_MATCHING PATTERN "*.h" 
)