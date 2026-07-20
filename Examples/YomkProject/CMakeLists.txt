cmake_minimum_required(VERSION 3.14)
project(YomkProject LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(YomkServer REQUIRED)

add_executable(${PROJECT_NAME}
    main.cpp
    boot/MyBoot.cpp
)
target_link_libraries(${PROJECT_NAME} PRIVATE YomkServer::YomkServer)
