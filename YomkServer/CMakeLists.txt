if(WIN32)
    set(CMAKE_INSTALL_PREFIX "$ENV{LOCALAPPDATA}/YomkServer" CACHE PATH "Installation path")
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
else()
    set(CMAKE_INSTALL_PREFIX "/usr/local" CACHE PATH "Installation path")
endif()

set(YomkServer_INCLUDE ${YomkServer_DIR}/include/YomkServer)
set(YomkServer_SRC ${YomkServer_DIR}/src)

set(INCLUDE_INSTALL_DIR include)
set(LIB_INSTALL_DIR lib)

include_directories(${YomkServer_INCLUDE})
include_directories(${nlohmann_json_DIR})

set(YomkServer_SOURCES 
    ${YomkServer_SRC}/YomkAPI.cpp
    ${YomkServer_SRC}/YomkServer.cpp
    ${YomkServer_SRC}/YomkServerPrivate.cpp
    ${YomkServer_SRC}/YomkService.cpp
    ${YomkServer_SRC}/YomkServicePrivate.cpp
    ${YomkServer_SRC}/Modules/Settings/YomkSettings.cpp
    ${YomkServer_SRC}/Modules/FunctionPool/YomkFunctionPool.cpp
    ${YomkServer_SRC}/Modules/Context/YomkContext.cpp
    ${YomkServer_SRC}/Modules/EventLoop/EventLoop.cpp
    ${YomkServer_SRC}/Modules/EventLoop/YomkEventLoop.cpp
    ${YomkServer_SRC}/Modules/Logger/YomkLogger.cpp
    ${YomkServer_SRC}/Modules/Logger/ConsoleLogger.cpp
    ${YomkServer_SRC}/Modules/Logger/FileLogger.cpp
    ${nlohmann_json_DIR}/json.hpp
)

add_library(YomkServer SHARED
    ${YomkServer_SOURCES}
)

target_compile_definitions(YomkServer PRIVATE YOMKSERVER_LIBRARY)

# 导出头文件
target_include_directories(YomkServer
    PUBLIC
        $<BUILD_INTERFACE:${YomkServer_INCLUDE}>  
        $<INSTALL_INTERFACE:include>
)

install(EXPORT YomkServerTargets
    FILE YomkServerTargets.cmake
    NAMESPACE YomkServer::  
    DESTINATION lib/cmake/YomkServer  
)

include(CMakePackageConfigHelpers)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/YomkServer/YomkServerConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/YomkServer/YomkServerConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/YomkServer/YomkServerConfig.cmake"
    INSTALL_DESTINATION lib/cmake/YomkServer
    PATH_VARS
        INCLUDE_INSTALL_DIR
        LIB_INSTALL_DIR
)

install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/YomkServer/YomkServerConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/YomkServer/YomkServerConfigVersion.cmake"
    DESTINATION lib/cmake/YomkServer
)

install(TARGETS YomkServer
    EXPORT YomkServerTargets         
    LIBRARY DESTINATION lib         
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include      
)

install(DIRECTORY ${YomkServer_INCLUDE}
    DESTINATION include              
    FILES_MATCHING PATTERN "*.h" 
)

if(UNIX AND NOT APPLE)
    install(CODE "
        # 处理 DESTDIR（打包场景）
        if (\"\$ENV{DESTDIR}\" STREQUAL \"\")
            set(_prefix \"${CMAKE_INSTALL_PREFIX}\")
        else()
            set(_prefix \"\$ENV{DESTDIR}/${CMAKE_INSTALL_PREFIX}\")
        endif()

        # 判断是否系统目录
        if (_prefix MATCHES \"^/(usr|usr/local|lib|lib64)$\"
            OR _prefix MATCHES \"^/(usr|usr/local)/lib(64)?$\")

            execute_process(
                COMMAND ldconfig
                RESULT_VARIABLE ldconfig_result
                ERROR_VARIABLE ldconfig_error
            )

            if (ldconfig_result EQUAL 0)
                message(STATUS \"ldconfig 执行成功，系统库缓存已更新\")
            else()
                message(WARNING \"ldconfig 执行失败（可能需要 root 权限）\")
                message(WARNING \"请手动执行：sudo ldconfig\")
            endif()
        else()
            message(STATUS \"非系统目录安装，跳过 ldconfig\")
            message(WARNING
                \"===========================================\\n\"
                \"请将以下命令加入 ~/.bashrc 或 ~/.zshrc：\\n\"
                \"export LD_LIBRARY_PATH=${CMAKE_INSTALL_PREFIX}/lib:\\\\\$LD_LIBRARY_PATH\\n\"
                \"===========================================\"
            )
        endif()
    ")
endif()