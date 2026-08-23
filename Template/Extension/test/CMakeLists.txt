cmake_minimum_required(VERSION 3.14)
project(TestYomkExtension LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(EXT_NAME YomkExtension)

find_package(YomkServer REQUIRED)
find_package(${EXT_NAME} REQUIRED)

message(STATUS "${EXT_NAME} version: ${${EXT_NAME}_VERSION}")
message(STATUS "${EXT_NAME} include dirs: ${${EXT_NAME}_INCLUDE_DIRS}")
message(STATUS "${EXT_NAME} lib dir: ${${EXT_NAME}_LIB_DIR}")
message(STATUS "${EXT_NAME} libraries: ${${EXT_NAME}_LIBRARIES}")

add_executable(TestYomkExtension TestYomkExtension.cpp)
target_link_libraries(TestYomkExtension PRIVATE ${EXT_NAME}::${EXT_NAME} YomkServer::YomkServer)

# 测试程序随扩展一并安装到 <安装路径>/bin，供用户在任意终端直接运行验证
# 测试程序可能有多个，此处显式列出全部测试目标名（不能用 ${PROJECT_NAME} 占位）
install(TARGETS
    TestYomkExtension
    RUNTIME DESTINATION bin
)
