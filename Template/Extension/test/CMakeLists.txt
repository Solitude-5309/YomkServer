cmake_minimum_required(VERSION 3.14)
project(TestYomkExtension LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(YomkServer REQUIRED)
find_package(YomkExtension REQUIRED)

message(STATUS "YomkExtension version: ${YomkExtension_VERSION}")
message(STATUS "YomkExtension include dirs: ${YomkExtension_INCLUDE_DIRS}")
message(STATUS "YomkExtension lib dir: ${YomkExtension_LIB_DIR}")
message(STATUS "YomkExtension libraries: ${YomkExtension_LIBRARIES}")

add_executable(TestYomkExtension TestYomkExtension.cpp)
target_link_libraries(TestYomkExtension PRIVATE YomkExtension::YomkExtension YomkServer::YomkServer)