cmake_minimum_required(VERSION 3.14)
project(TestYomkMath LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(YOMK_MATH_NAME YomkMath)

find_package(YomkServer REQUIRED)
find_package(${YOMK_MATH_NAME} REQUIRED)

message(STATUS "${YOMK_MATH_NAME} version: ${${YOMK_MATH_NAME}_VERSION}")
message(STATUS "${YOMK_MATH_NAME} include dirs: ${${YOMK_MATH_NAME}_INCLUDE_DIRS}")
message(STATUS "${YOMK_MATH_NAME} lib dir: ${${YOMK_MATH_NAME}_LIB_DIR}")
message(STATUS "${YOMK_MATH_NAME} libraries: ${${YOMK_MATH_NAME}_LIBRARIES}")

add_executable(${PROJECT_NAME} TestYomkMath.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE ${YOMK_MATH_NAME}::${YOMK_MATH_NAME} YomkServer::YomkServer)
