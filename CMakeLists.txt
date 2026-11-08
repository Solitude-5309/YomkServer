cmake_minimum_required(VERSION 3.14)

# 编译器预选模块：g++ 优先，回退 clang++（必须在 project() 之前 include）
include(cmake/AbiInfo.cmake)

# 导出 compile_commands.json，供 IDE（clangd）/clang-tidy 按真实编译参数解析
option(YOMK_BUILD_TESTS "Build YomkServer internal tests" OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(Yomk VERSION 3.6.0 LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
# compile_commands.json 导出到源码根目录，供 clangd 使用：
# 构建目录可任意（仓库外亦可），避免用户因 build 目录位置不同而 IDE 解析失败
add_custom_target(export_compile_commands ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_BINARY_DIR}/compile_commands.json
        ${CMAKE_SOURCE_DIR}/compile_commands.json
)

# generate to dir（允许外部通过 -DCMAKE_*_OUTPUT_DIRECTORY 覆盖，以支持 sanitizer 独立构建目录）
if(NOT CMAKE_ARCHIVE_OUTPUT_DIRECTORY)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY $<1:${CMAKE_BINARY_DIR}/../bin>)
endif()
if(NOT CMAKE_LIBRARY_OUTPUT_DIRECTORY)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY $<1:${CMAKE_BINARY_DIR}/../bin>)
endif()
if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY $<1:${CMAKE_BINARY_DIR}/../bin>)
endif()

# CMake
include(cmake/Cpp.cmake)
message("-- env Cpp success.")

# ABI 环境指纹：采集/打印编译环境信息并生成 abi_report.txt（project() 之后才可调用）
yomk_abi_report()
install(FILES ${CMAKE_BINARY_DIR}/abi_report.txt
        DESTINATION share/YomkServer)

# YomkServer
# 构建期内部路径变量：与 find_package 写入缓存的包定位变量解耦，统一唯一事实来源。
set(YOMK_SERVER_SOURCE_ROOT  "${CMAKE_CURRENT_SOURCE_DIR}")
set(YOMK_SERVER_INCLUDE_ROOT "${YOMK_SERVER_SOURCE_ROOT}/include")
set(YOMK_SERVER_INCLUDE_DIR  "${YOMK_SERVER_INCLUDE_ROOT}")  # 头包含基目录（#include "yomk/..." 相对此解析）
set(YOMK_SERVER_SRC_DIR      "${YOMK_SERVER_SOURCE_ROOT}/src")
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
    ${YOMK_SERVER_SRC_DIR}/server/YomkServer.cpp
    ${YOMK_SERVER_SRC_DIR}/server/YomkServerPrivate.cpp
    ${YOMK_SERVER_SRC_DIR}/server/YomkService.cpp
    ${YOMK_SERVER_SRC_DIR}/server/YomkServicePrivate.cpp
    ${YOMK_SERVER_SRC_DIR}/server/YomkSimpleThreadPool.cpp
    ${YOMK_SERVER_SRC_DIR}/fn_pool/YomkFunctionPool.cpp
    ${YOMK_SERVER_SRC_DIR}/ctx/ContextGroup.cpp
    ${YOMK_SERVER_SRC_DIR}/ctx/YomkContext.cpp
    ${YOMK_SERVER_SRC_DIR}/event_loop/EventLoop.cpp
    ${YOMK_SERVER_SRC_DIR}/event_loop/YomkEventLoop.cpp
    ${YOMK_SERVER_SRC_DIR}/log/YomkLogger.cpp
    ${YOMK_SERVER_SRC_DIR}/log/ConsoleLogger.cpp
    ${YOMK_SERVER_SRC_DIR}/log/FileLogger.cpp
    ${YOMK_SERVER_SRC_DIR}/srv_info/YomkServerInfo.cpp
)

add_library(YomkServer SHARED
    ${YomkServer_SOURCES}
)

target_compile_definitions(YomkServer PRIVATE YOMK_LIBRARY)

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
    "${CMAKE_CURRENT_BINARY_DIR}/yomk_server/YomkServerConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/YomkServerConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/yomk_server/YomkServerConfig.cmake"
    INSTALL_DESTINATION lib/cmake/YomkServer
    PATH_VARS
        INCLUDE_INSTALL_DIR
        LIB_INSTALL_DIR
)

install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/yomk_server/YomkServerConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/yomk_server/YomkServerConfigVersion.cmake"
    DESTINATION lib/cmake/YomkServer
)

install(TARGETS YomkServer
    EXPORT YomkServerTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)

install(DIRECTORY ${YOMK_SERVER_INCLUDE_DIR}/yomk
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)
message("-- YomkServer success.")

# ExamplesYomkServer（用户教程示例，默认编译并安装）
set(ExamplesYomkServer_DIR ${CMAKE_CURRENT_SOURCE_DIR}/examples)
include(${ExamplesYomkServer_DIR}/CMakeLists.txt)
message("-- ExamplesYomkServer success.")

# TestYomkServer（框架内部全面测试，可选编译，不安装）
if(YOMK_BUILD_TESTS)
    set(TestYomkServer_DIR ${CMAKE_CURRENT_SOURCE_DIR}/test)
    include(${TestYomkServer_DIR}/CMakeLists.txt)
    message("-- TestYomkServer success.")
endif()
