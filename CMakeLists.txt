cmake_minimum_required(VERSION 3.14)
# 版本号策略：YomkServerPy 使用独立号段，发布节奏自成一贯；
# 版本机制为编译期注入的单一来源链：
# project(VERSION) → configure_file 生成 YomkServerPyVersion.py 的 __version__
# → YomkApi 顶部 import → server().version()（构建期注入，无运行时读取），
# 另经 write_basic_package_version_file(SameMajorVersion) 供下游 find_package。
# 本行即版本唯一来源，发布时手动 bump。
project(YomkServerPy VERSION 2.2.10 LANGUAGES C)

# ---------- 各运行时域版本号 ----------
# 集中定义，当前全部取项目版本号；若某域需要独立版本，只改对应行。
# 第 11 节据此 configure_file 生成各域 _version.py。
set(YOMK_SERVER_VERSION "${PROJECT_VERSION}")
set(YOMK_CTX_VERSION "${PROJECT_VERSION}")
set(YOMK_EVENT_LOOP_VERSION "${PROJECT_VERSION}")
set(YOMK_FN_POOL_VERSION "${PROJECT_VERSION}")
set(YOMK_LOG_VERSION "${PROJECT_VERSION}")
set(YOMK_SRV_INFO_VERSION "${PROJECT_VERSION}")

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

# ---------- 3. 合并源片段并生成 C 文件 ----------
# Python 模块名（import 名 / PyInit 名 / .pyi 名）为 YomkServer；
# CMake 工程身份与扩展本体文件名均为 YomkServerPy，两者解耦：
# 本体 YomkServerPy.so 由装载 shim（api/YomkServerLoader.py → site-packages/YomkServer.py）
# 以模块名 YomkServer 装载，既避免与 C++ 版 YomkServer 产物同名，又不改变 import 契约
set(YOMK_PY_MODULE_NAME "YomkServer")
set(YOMK_PY_ARTIFACT_NAME "YomkServerPy")

# 片段清单：顺序即符号可见顺序（被继承的基类必须靠前）
set(YOMK_PY_FRAGMENTS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/YomkServer.py
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/YomkService.py
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/logger/YomkLog.py
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/logger/ConsoleLogger.py
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/logger/FileLogger.py
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/logger/YomkLogger.py
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/context/ContextGroup.py
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/context/YomkContext.py
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/event_loop/EventLoop.py
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/event_loop/YomkEventLoop.py
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/function_pool/YomkFunctionPool.py
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/server_info/YomkServerInfo.py
)

# 合并产物必须命名为 YomkServer.py：Cython 的模块名（PyInit_*）由源文件名决定
set(PY_SOURCE ${CMAKE_CURRENT_BINARY_DIR}/${YOMK_PY_MODULE_NAME}.py)
set(C_SOURCE ${CMAKE_CURRENT_BINARY_DIR}/${YOMK_PY_MODULE_NAME}.c)

add_custom_command(
    OUTPUT ${PY_SOURCE}
    COMMAND ${Python_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/assemble_py.py"
            --output "${PY_SOURCE}"
            --root "${CMAKE_CURRENT_SOURCE_DIR}"
            ${YOMK_PY_FRAGMENTS}
    DEPENDS ${YOMK_PY_FRAGMENTS}
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/assemble_py.py"
    COMMENT "Assembling Cython module from source fragments"
)

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
# 构建期即产出测试可用产物（构建期产物目录设计，测试无需 install 即可运行）：
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

# 纯 python 模块构建期同步拷入（YomkServerPyVersion.py 已由 configure_file 生成于构建树）；
# 装载 shim 以 YomkServer.py 名拷入，与安装布局保持同构
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/src/api/${YOMK_PY_MODULE_NAME}.pyi"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/api/YomkApi.py"
        "${CMAKE_BINARY_DIR}/${PROJECT_NAME}Version.py"
        "${BUILD_SITE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/src/api/YomkServerLoader.py"
        "${BUILD_SITE_DIR}/${YOMK_PY_MODULE_NAME}.py"
    COMMENT "Copying pure-python modules and loader shim into build site-packages"
)

# ---------- 5. 头文件和链接 ----------
target_include_directories(${PROJECT_NAME} PRIVATE
    ${Python_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME}
    ${Python_LIBRARIES}
)

# ---------- 6. 输出文件名（跨平台适配） ----------
# 本体文件名用 YomkServerPy（与 CMake 工程身份一致）；PyInit_ 符号仍由合并源
# build/YomkServer.py 决定（PyInit_YomkServer），由装载 shim 显式装载
if(WIN32)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        PREFIX ""
        OUTPUT_NAME "${YOMK_PY_ARTIFACT_NAME}"
        SUFFIX ".pyd"
    )
else()
    set_target_properties(${PROJECT_NAME} PROPERTIES
        PREFIX ""
        OUTPUT_NAME "${YOMK_PY_ARTIFACT_NAME}"
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
    PATH_VARS PYTHON_SITE_PACKAGES
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
    DESTINATION lib/cmake/${PROJECT_NAME}
)

install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/api/${YOMK_PY_MODULE_NAME}.pyi"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/api/YomkApi.py"
    DESTINATION ${PYTHON_SITE_PACKAGES}
)

# 装载 shim：安装为 YomkServer.py，以模块名 YomkServer 装载本体 YomkServerPy.so
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/api/YomkServerLoader.py"
    DESTINATION ${PYTHON_SITE_PACKAGES}
    RENAME ${YOMK_PY_MODULE_NAME}.py
)

# 版本模块（源：project(VERSION)；YomkApi 顶部 import 暴露 __version__）
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/api/YomkServerPyVersion.py.in"
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

# ---------- 11. L11 新包全量 Cython + 每域版本 + staging + 安装 ----------
# 新包与旧合并目标并行存在（L11 不删除旧面，删除在 L13）：
# - 全量模块各自独立编译，输出 build/prefix/.../site-packages/yomk/<域>/；
# - 所有 __init__.py 与 builder.py 保持纯 Python，不列入编译清单；
# - 每域由 configure_file 生成 _version.py（各域版本号集中定义于 project() 之后）；
# - staging 把源码树与生成的 _version.py 合并到构建树，安装规则同步落盘。
set(YOMK_PY_PACKAGE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/yomk")
set(YOMK_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/yomk")
set(YOMK_VERSIONED_PACKAGES server ctx event_loop fn_pool log srv_info)

# 需 Cython 编译的模块（相对 src/yomk，不带 .py）
set(YOMK_CYTHON_MODULES
    msg/server/ServerMsg
    msg/ctx/ContextMsg
    msg/event_loop/EventLoopMsg
    msg/fn_pool/FnPoolMsg
    msg/log/LogMsg
    msg/srv_info/SrvInfoMsg
    server/YomkService
    server/YomkServer
    server/_diag
    server/_facade
    ctx/_diag
    ctx/ContextGroup
    ctx/YomkContext
    event_loop/_diag
    event_loop/EventLoop
    event_loop/YomkEventLoop
    fn_pool/_diag
    fn_pool/YomkFunctionPool
    log/ConsoleLogger
    log/FileLogger
    log/YomkLogger
    log/_diag
    srv_info/_diag
    srv_info/YomkServerInfo
)

set(YOMK_CYTHON_TARGETS "")
foreach(mod IN LISTS YOMK_CYTHON_MODULES)
    string(REPLACE "/" "." mod_dotted "${mod}")
    string(REPLACE "/" "_" target "yomk_${mod}")
    get_filename_component(mod_name "${mod}" NAME)
    get_filename_component(mod_dir "${mod}" DIRECTORY)

    set(py_src "${YOMK_PY_PACKAGE_DIR}/${mod}.py")
    set(c_src "${CMAKE_CURRENT_BINARY_DIR}/cython/yomk/${mod}.c")
    set(out_dir "${BUILD_SITE_DIR}/yomk/${mod_dir}")

    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/cython/yomk/${mod_dir}")
    file(MAKE_DIRECTORY "${out_dir}")

    add_custom_command(
        OUTPUT "${c_src}"
        COMMAND ${CYTHON_EXECUTABLE} -3
                --module-name "yomk.${mod_dotted}"
                -o "${c_src}" "${py_src}"
        DEPENDS "${py_src}"
        COMMENT "Cython yomk.${mod_dotted}"
    )

    add_library(${target} SHARED "${c_src}")
    set_target_properties(${target} PROPERTIES
        OUTPUT_NAME "${mod_name}"
        PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY "${out_dir}"
        ARCHIVE_OUTPUT_DIRECTORY "${out_dir}"
        RUNTIME_OUTPUT_DIRECTORY "${out_dir}"
    )
    target_include_directories(${target} PRIVATE ${Python_INCLUDE_DIRS})
    target_link_libraries(${target} ${Python_LIBRARIES})

    if(WIN32)
        set_target_properties(${target} PROPERTIES SUFFIX ".pyd")
        target_compile_definitions(${target} PRIVATE _hypot=hypot)
        target_compile_options(${target} PRIVATE /O2 /W3)
    else()
        set_target_properties(${target} PROPERTIES SUFFIX ".so")
        target_compile_options(${target} PRIVATE -O3 -Wall)
    endif()

    list(APPEND YOMK_CYTHON_TARGETS ${target})
endforeach()

# INTERFACE 聚合 target：L13 删除旧 YomkServerPy 本体后接管该名称；
# 当前为临时名且暂不进入安装导出集，避免改变现有 find_package 契约
add_library(YomkServerPyYomkModules INTERFACE)
target_link_libraries(YomkServerPyYomkModules INTERFACE ${YOMK_CYTHON_TARGETS})

foreach(pkg IN LISTS YOMK_VERSIONED_PACKAGES)
    string(TOUPPER "${pkg}" pkg_upper)
    set(YOMK_MODULE_VERSION "${YOMK_${pkg_upper}_VERSION}")
    file(MAKE_DIRECTORY "${YOMK_GEN_DIR}/${pkg}")
    configure_file(
        "${YOMK_PY_PACKAGE_DIR}/${pkg}/_version.py.in"
        "${YOMK_GEN_DIR}/${pkg}/_version.py"
        @ONLY
    )
endforeach()

# 纯 Python staging：源码树（.py/.pyi/py.typed）→ 构建树；
# 随后覆盖生成的 _version.py，并清理源码树带出的 .py.in 与 __pycache__
set(YOMK_STAGE_CLEANUP)
foreach(pkg IN LISTS YOMK_VERSIONED_PACKAGES)
    list(APPEND YOMK_STAGE_CLEANUP
        COMMAND ${CMAKE_COMMAND} -E remove_directory
            "${BUILD_SITE_DIR}/yomk/${pkg}/__pycache__"
        COMMAND ${CMAKE_COMMAND} -E remove_directory
            "${BUILD_SITE_DIR}/yomk/msg/${pkg}/__pycache__"
        COMMAND ${CMAKE_COMMAND} -E remove -f
            "${BUILD_SITE_DIR}/yomk/${pkg}/_version.py.in")
endforeach()

add_custom_target(yomk_stage_python ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${YOMK_PY_PACKAGE_DIR}"
            "${BUILD_SITE_DIR}/yomk"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${YOMK_GEN_DIR}"
            "${BUILD_SITE_DIR}/yomk"
    ${YOMK_STAGE_CLEANUP}
    COMMENT "Staging pure-python yomk package"
)

# 安装：源码树按扩展名过滤（不含 .py.in），生成版本单独安装，编译产物按域落盘。
# YOMK_INSTALL_PY_SOURCE=OFF 时只排除已编译模块的回退 .py；__init__.py、
# builder.py 与 _version.py 本就必须以纯 Python 形式发布，不能一并排除。
option(YOMK_INSTALL_PY_SOURCE "Install pure-python fallback sources alongside compiled modules" ON)
set(YOMK_PY_SOURCE_EXCLUDES)
if(NOT YOMK_INSTALL_PY_SOURCE)
    foreach(mod IN LISTS YOMK_CYTHON_MODULES)
        get_filename_component(mod_name "${mod}" NAME)
        get_filename_component(mod_dir "${mod}" DIRECTORY)
        list(APPEND YOMK_PY_SOURCE_EXCLUDES
            PATTERN "${mod_dir}/${mod_name}.py" EXCLUDE)
    endforeach()
endif()

install(DIRECTORY "${YOMK_PY_PACKAGE_DIR}/"
    DESTINATION "${PYTHON_SITE_PACKAGES}/yomk"
    FILES_MATCHING
        PATTERN "*.py"
        PATTERN "*.pyi"
        PATTERN "py.typed"
    PATTERN "__pycache__" EXCLUDE
    ${YOMK_PY_SOURCE_EXCLUDES}
)

install(DIRECTORY "${YOMK_GEN_DIR}/"
    DESTINATION "${PYTHON_SITE_PACKAGES}/yomk"
    FILES_MATCHING PATTERN "_version.py"
)

foreach(mod IN LISTS YOMK_CYTHON_MODULES)
    string(REPLACE "/" "_" target "yomk_${mod}")
    get_filename_component(mod_dir "${mod}" DIRECTORY)
    install(TARGETS ${target}
        LIBRARY DESTINATION "${PYTHON_SITE_PACKAGES}/yomk/${mod_dir}"
        ARCHIVE DESTINATION "${PYTHON_SITE_PACKAGES}/yomk/${mod_dir}"
        RUNTIME DESTINATION "${PYTHON_SITE_PACKAGES}/yomk/${mod_dir}"
    )
endforeach()