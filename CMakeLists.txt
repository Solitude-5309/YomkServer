cmake_minimum_required(VERSION 3.14)
project(YomkServerPy VERSION 1.1.0 LANGUAGES C)

# ---------- 1. 查找 Python ----------
find_package(Python REQUIRED COMPONENTS Development)

string(REPLACE "." ";" PYTHON_VERSION_LIST ${Python_VERSION})
list(GET PYTHON_VERSION_LIST 0 PYTHON_MAJOR)
list(GET PYTHON_VERSION_LIST 1 PYTHON_MINOR)
set(PYTHON_SITE_PACKAGES "lib/python${PYTHON_MAJOR}.${PYTHON_MINOR}/site-packages")

# ---------- 2. 查找 Cython ----------
find_program(CYTHON_EXECUTABLE cython REQUIRED)

# ---------- 3. 生成 C 文件 ----------
set(PY_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/src/${PROJECT_NAME}.py)
set(C_SOURCE ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.c)

add_custom_command(
    OUTPUT ${C_SOURCE}
    COMMAND ${CYTHON_EXECUTABLE}
        --embed
        -3
        ${PY_SOURCE}
        -o ${C_SOURCE}
    DEPENDS ${PY_SOURCE}
    COMMENT "Running Cython to generate C source"
)

# ---------- 4. 编译为共享库 ----------
add_library(${PROJECT_NAME} SHARED ${C_SOURCE})

# ---------- 5. 头文件和链接 ----------
target_include_directories(${PROJECT_NAME} PRIVATE
    ${Python_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME}
    ${Python_LIBRARIES}
)

# ---------- 6. 输出文件名（跨平台适配） ----------
if(WIN32)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        PREFIX ""
        OUTPUT_NAME "${PROJECT_NAME}"
        SUFFIX ".pyd"
    )
else()
    set_target_properties(${PROJECT_NAME} PROPERTIES
        PREFIX ""
        OUTPUT_NAME "${PROJECT_NAME}"
        SUFFIX ".so"
    )
endif()

# ---------- 7. 编译选项（跨平台适配） ----------
if(WIN32)
    target_compile_options(${PROJECT_NAME} PRIVATE
        /O2
        /W3
    )
else()
    target_compile_options(${PROJECT_NAME} PRIVATE
        -O3
        -Wall
    )
endif()

# ---------- 8. Windows 特殊处理 ----------
if(WIN32)
    # Windows 需要定义 DLL 导出宏
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        _hypot=hypot
    )
endif()

# ---------- 9. 安装规则 ----------
# 导出 targets
install(EXPORT ${PROJECT_NAME}Targets
    FILE ${PROJECT_NAME}Targets.cmake
    NAMESPACE ${PROJECT_NAME}::
    DESTINATION lib/cmake/${PROJECT_NAME}
)

# Config & Version
include(CMakePackageConfigHelpers)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/${PROJECT_NAME}Config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
    INSTALL_DESTINATION lib/cmake/${PROJECT_NAME}
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
    DESTINATION lib/cmake/${PROJECT_NAME}
)

install(FILES 
    "${CMAKE_CURRENT_SOURCE_DIR}/src/${PROJECT_NAME}.pyi"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/YomkApi.py"
    DESTINATION ${PYTHON_SITE_PACKAGES}
)

install(TARGETS ${PROJECT_NAME}
    EXPORT ${PROJECT_NAME}Targets         
    LIBRARY DESTINATION ${PYTHON_SITE_PACKAGES}    
    ARCHIVE DESTINATION ${PYTHON_SITE_PACKAGES}
    RUNTIME DESTINATION ${PYTHON_SITE_PACKAGES}
)