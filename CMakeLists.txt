cmake_minimum_required(VERSION 3.14)
project(YomkServerPy VERSION 1.5.6 LANGUAGES C)

# ---------- 1. 查找 Python ----------
# Interpreter 组件提供 Python_EXECUTABLE：cython 仅 user-site 安装时（可执行入口
# 不在 PATH），Cython 转译回退为解释器模块调用（见下方 Cython 查找）
find_package(Python REQUIRED COMPONENTS Interpreter Development)

string(REPLACE "." ";" PYTHON_VERSION_LIST ${Python_VERSION})
list(GET PYTHON_VERSION_LIST 0 PYTHON_MAJOR)
list(GET PYTHON_VERSION_LIST 1 PYTHON_MINOR)
set(PYTHON_SITE_PACKAGES "lib/python${PYTHON_MAJOR}.${PYTHON_MINOR}/site-packages")

# ---------- 2. 查找 Cython ----------
# 优先 PATH 中的 cython 可执行文件；user-site 安装且 ~/.local/bin 不在 PATH 时
# 回退为解释器模块调用（COMMAND 处按分号展开为 <Python_EXECUTABLE> -m cython），
# 与 build_ubuntu.sh 依赖检测语义一致
find_program(CYTHON_EXECUTABLE cython)
if(NOT CYTHON_EXECUTABLE)
    set(CYTHON_EXECUTABLE "${Python_EXECUTABLE};-m;cython")
endif()

# ---------- 2.5 构建环境指纹（project() 之后调用，采集/打印环境信息并生成 abi_report.txt） ----------
include(cmake/EnvInfo.cmake)
yomk_env_report()

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
# 构建期即产出测试可用产物（对齐 main CMAKE_RUNTIME_OUTPUT_DIRECTORY=bin 的设计）：
# .so 直接输出到构建树内 build/prefix/<PYTHON_SITE_PACKAGES>，与 YomkApi.py/.pyi/
# 版本模块同目录；run_tests.sh 默认消费该目录——构建即可测，不依赖 install 步骤，
# 测试产物与安装产物同源同版本（install 仅负责部署到安装前缀）
set(BUILD_SITE_DIR "${CMAKE_BINARY_DIR}/prefix/${PYTHON_SITE_PACKAGES}")
file(MAKE_DIRECTORY "${BUILD_SITE_DIR}")

add_library(${PROJECT_NAME} SHARED ${C_SOURCE})
set_target_properties(${PROJECT_NAME} PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${BUILD_SITE_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY "${BUILD_SITE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${BUILD_SITE_DIR}"
)

# 纯 python 模块构建期同步拷入（YomkServerPyVersion.py 已由 configure_file 生成于构建树）
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/src/${PROJECT_NAME}.pyi"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/YomkApi.py"
        "${CMAKE_BINARY_DIR}/${PROJECT_NAME}Version.py"
        "${BUILD_SITE_DIR}"
    COMMENT "Copying pure-python modules into build site-packages"
)

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
# 环境指纹报告
install(FILES ${CMAKE_BINARY_DIR}/abi_report.txt
        DESTINATION share/YomkServerPy)

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

# 版本模块（源：project(VERSION)；YomkApi 顶部 import 暴露 __version__）
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/YomkServerPyVersion.py.in"
    "${CMAKE_CURRENT_BINARY_DIR}/YomkServerPyVersion.py"
    @ONLY
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/YomkServerPyVersion.py"
    DESTINATION ${PYTHON_SITE_PACKAGES}
)

install(TARGETS ${PROJECT_NAME}
    EXPORT ${PROJECT_NAME}Targets         
    LIBRARY DESTINATION ${PYTHON_SITE_PACKAGES}    
    ARCHIVE DESTINATION ${PYTHON_SITE_PACKAGES}
    RUNTIME DESTINATION ${PYTHON_SITE_PACKAGES}
)

# ---------- 10. 示例不安装（Python 脚本源码即可运行，示例位于源码树 examples/，直接在源码树下运行） ----------