cmake_minimum_required(VERSION 3.14)

project(Yomk VERSION 2.1.2 LANGUAGES CXX)

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

# TestYomkServer
set(TestYomkServer_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Test/YomkServer)
include(${TestYomkServer_DIR}/CMakeLists.txt)
message("-- TestYomkServer success.")
