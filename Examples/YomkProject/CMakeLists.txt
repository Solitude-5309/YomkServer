cmake_minimum_required(VERSION 3.14)
project(YomkProject LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 固定安装到工程源码目录下的 install/
set(CMAKE_INSTALL_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/install" CACHE PATH "Install path" FORCE)

find_package(YomkServer REQUIRED)
find_package(nlohmann_json REQUIRED)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_executable(${PROJECT_NAME}
    main.cpp
    boot/MyBoot.cpp
    services/ConfigService.cpp
)
target_link_libraries(${PROJECT_NAME} PRIVATE
    YomkServer::YomkServer
    nlohmann_json::nlohmann_json
    $<$<AND:$<CXX_COMPILER_ID:GNU>,$<VERSION_LESS:$<CXX_COMPILER_VERSION>,9.0>>:stdc++fs>
)

# 安装可执行文件
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

# 安装配置文件
install(DIRECTORY config/
    DESTINATION config
)

# 生成 setup.bash
get_target_property(YomkServer_LIB_DIR YomkServer::YomkServer LOCATION)
get_filename_component(YomkServer_LIB_DIR ${YomkServer_LIB_DIR} DIRECTORY)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/setup.bash.in
    ${CMAKE_CURRENT_BINARY_DIR}/setup.bash
    @ONLY
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/setup.bash
    DESTINATION .
)
