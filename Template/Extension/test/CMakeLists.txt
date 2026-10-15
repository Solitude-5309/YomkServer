cmake_minimum_required(VERSION 3.14)
project(TestTemplateExtension LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(YomkServer REQUIRED)
find_package(TemplateExtension REQUIRED)

message(STATUS "TemplateExtension version: ${TemplateExtension_VERSION}")
message(STATUS "TemplateExtension include dirs: ${TemplateExtension_INCLUDE_DIRS}")
message(STATUS "TemplateExtension lib dir: ${TemplateExtension_LIB_DIR}")
message(STATUS "TemplateExtension libraries: ${TemplateExtension_LIBRARIES}")

add_executable(TestTemplateExtension TestTemplateExtension.cpp)
target_link_libraries(TestTemplateExtension PRIVATE TemplateExtension::TemplateExtension YomkServer::YomkServer)