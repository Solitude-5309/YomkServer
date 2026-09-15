cmake_minimum_required(VERSION 3.14)

# 编译器预选模块：g++ 优先，回退 clang++（必须在 project() 之前 include）
include(CMake/AbiInfo.cmake)

# 导出 compile_commands.json，供 IDE（clangd）/clang-tidy 按真实编译参数解析
option(YOMK_BUILD_TESTS "Build YomkServer internal tests" OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(Yomk VERSION 2.5.12 LANGUAGES CXX)

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
include(CMake/Cpp.cmake)
message("-- env Cpp success.")

# ABI 环境指纹：采集/打印编译环境信息并生成 abi_report.txt（project() 之后才可调用）
yomk_abi_report()
install(FILES ${CMAKE_BINARY_DIR}/abi_report.txt
        DESTINATION share/YomkServer)

# YomkServer
set(YomkServer_DIR ${CMAKE_CURRENT_SOURCE_DIR}/YomkServer)
include(${YomkServer_DIR}/CMakeLists.txt)
message("-- YomkServer success.")

# ExamplesYomkServer（用户教程示例，默认编译并安装）
set(ExamplesYomkServer_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Examples)
include(${ExamplesYomkServer_DIR}/CMakeLists.txt)
message("-- ExamplesYomkServer success.")

# TestYomkServer（框架内部全面测试，可选编译，不安装）
if(YOMK_BUILD_TESTS)
    set(TestYomkServer_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Test)
    include(${TestYomkServer_DIR}/CMakeLists.txt)
    message("-- TestYomkServer success.")
endif()
