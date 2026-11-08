cmake_minimum_required(VERSION 3.14)
project(TestExtension LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(YomkServer REQUIRED)
find_package(extension REQUIRED)

message(STATUS "extension version: ${extension_VERSION}")
message(STATUS "extension include dirs: ${extension_INCLUDE_DIRS}")
message(STATUS "extension lib dir: ${extension_LIB_DIR}")
message(STATUS "extension libraries: ${extension_LIBRARIES}")

add_executable(TestExtension TestExtension.cpp)
target_link_libraries(TestExtension PRIVATE extension::extension YomkServer::YomkServer)
