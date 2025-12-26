
set(YomkServer_INCLUDE ${YomkServer_DIR}/include)
set(YomkServer_SRC ${YomkServer_DIR}/src)

include_directories(${YomkServer_INCLUDE})
include_directories(${nlohmann_json_DIR})

set(YomkServer_SOURCES 
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