cmake_minimum_required(VERSION 3.14)

project(Yomk LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

# Qt Version
set(Build_Qt_Version 6)

# generate to dir
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY $<1:${CMAKE_BINARY_DIR}/../bin>)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY $<1:${CMAKE_BINARY_DIR}/../bin>)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY $<1:${CMAKE_BINARY_DIR}/../bin>)

# CMake
include(CMake/Cpp.cmake)
message("-- env Cpp success.")

# nlohmann_json
set(nlohmann_json_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/nlohmann_json)

# YomkServer
set(YomkServer_DIR ${CMAKE_CURRENT_SOURCE_DIR}/YomkServer)
include(${YomkServer_DIR}/CMakeLists.txt)
message("-- YomkServer success.")

# TestYomkServer
set(TestYomkServer_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Test/YomkServer)
include(${TestYomkServer_DIR}/CMakeLists.txt)
message("-- TestYomkServer success.")
