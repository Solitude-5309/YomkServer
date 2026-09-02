cmake_minimum_required(VERSION 3.14)

project(Yomk VERSION 2.4.4 LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

# generate to dir
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY $<1:${CMAKE_BINARY_DIR}/../bin>)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY $<1:${CMAKE_BINARY_DIR}/../bin>)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY $<1:${CMAKE_BINARY_DIR}/../bin>)

# CMake
include(CMake/Cpp.cmake)
message("-- env Cpp success.")

# YomkServer
set(YomkServer_DIR ${CMAKE_CURRENT_SOURCE_DIR}/YomkServer)
include(${YomkServer_DIR}/CMakeLists.txt)
message("-- YomkServer success.")

# ExamplesYomkServer（用户教程示例，默认编译并安装）
set(ExamplesYomkServer_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Examples)
include(${ExamplesYomkServer_DIR}/CMakeLists.txt)
message("-- ExamplesYomkServer success.")

# TestYomkServer（框架内部全面测试，可选编译，不安装）
option(YOMK_BUILD_TESTS "Build YomkServer internal tests" OFF)
if(YOMK_BUILD_TESTS)
    set(TestYomkServer_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Test)
    include(${TestYomkServer_DIR}/CMakeLists.txt)
    message("-- TestYomkServer success.")
endif()
