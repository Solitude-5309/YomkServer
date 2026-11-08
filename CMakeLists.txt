cmake_minimum_required(VERSION 3.14)
# 版本号策略：YomkServerPy 使用独立号段，发布节奏自成一贯；
# 版本机制为编译期注入的单一来源链：
# project(VERSION) → 各域 set(YOMK_<域>_VERSION) → configure_file 生成
# yomk/<域>/_version.py 的 __version__ → yomk.<域>.__version__
# （server().version() 经 yomk.server.__version__ 暴露；构建期注入，无运行时读取），
# 另经 write_basic_package_version_file(SameMajorVersion) 供下游 find_package。
# 本行即版本唯一来源，发布时手动 bump。
project(YomkServerPy VERSION 2.2.14 LANGUAGES C)

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

# ---------- 3. 构建树产物目录 ----------
# 构建期即产出测试可用产物（构建期产物目录设计，测试无需 install 即可运行）：
# 新包纯 Python 面与各域 .so 直接落 build/prefix/<PYTHON_SITE_PACKAGES>；
# run_tests.sh 默认消费该目录——构建即可测，不依赖 install 步骤，
# 测试产物与安装产物同源同版本（install 仅负责部署到安装前缀）
set(BUILD_SITE_DIR "${CMAKE_BINARY_DIR}/prefix/${PYTHON_SITE_PACKAGES}")
file(MAKE_DIRECTORY "${BUILD_SITE_DIR}")

# ---------- 4. 安装规则 ----------
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

# ---------- 5. 示例不安装（Python 脚本源码即可运行，示例位于源码树 examples/，直接在源码树下运行） ----------

# ---------- 6. 新包全量 Cython + 每域版本 + staging + 安装（L13 起为唯一 Python 面） ----------
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

# INTERFACE 聚合 target：L13 起以工程名 YomkServerPy 接管旧本体目标名，
# 随 ${PROJECT_NAME}Targets 导出（YomkServerPy::YomkServerPy），下游
# find_package 消费者据此链接全部 yomk 域模块
add_library(${PROJECT_NAME} INTERFACE)
target_link_libraries(${PROJECT_NAME} INTERFACE ${YOMK_CYTHON_TARGETS})
install(TARGETS ${PROJECT_NAME} EXPORT ${PROJECT_NAME}Targets)

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
        EXPORT ${PROJECT_NAME}Targets
        LIBRARY DESTINATION "${PYTHON_SITE_PACKAGES}/yomk/${mod_dir}"
        ARCHIVE DESTINATION "${PYTHON_SITE_PACKAGES}/yomk/${mod_dir}"
        RUNTIME DESTINATION "${PYTHON_SITE_PACKAGES}/yomk/${mod_dir}"
    )
endforeach()