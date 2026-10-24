# ============================================================
# YomkServerPy 构建环境指纹模块
#
# 用途：帮助下游用户比对构建环境，排查 Python ABI 兼容问题
#   （Python C 扩展绑定构建时解释器主次版本，与常见"换 Python 版本
#     无法 import"问题直接相关）
#   yomk_env_report()：在 find_package(Python) 与 find_program(cython)
#   之后调用，采集/打印构建环境指纹，并生成 ${CMAKE_BINARY_DIR}/abi_report.txt
#   供随库安装、用户比对
# ============================================================

function(yomk_env_report)
    # 仅针对 Linux 场景；其他平台只做简短提示，不生成报告
    if(NOT UNIX OR NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
        message(STATUS "[YomkPy ABI] 非 Linux 环境，跳过环境指纹报告")
        return()
    endif()

    set(_L "")
    macro(_abi_line)
        # _abi_line("标签" "值") → 追加 "标签: 值" 到 _L 并 STATUS 打印
        list(APPEND _L "${ARGV0}: ${ARGV1}")
        message(STATUS "[YomkPy ABI] ${ARGV0}: ${ARGV1}")
    endmacro()

    # ---- 系统 / 发行版 ----
    set(_distro_desc "未知")
    set(_distro_detail "")
    if(EXISTS "/etc/os-release")
        file(STRINGS "/etc/os-release" _osr_lines)
        set(_os_id "")
        set(_os_ver "")
        foreach(_line IN LISTS _osr_lines)
            if(_line MATCHES "^PRETTY_NAME=")
                string(REGEX REPLACE "^PRETTY_NAME=\"?([^\"]*)\"?" "\\1" _distro_desc "${_line}")
            elseif(_line MATCHES "^ID=")
                string(REGEX REPLACE "^ID=\"?([^\"]*)\"?" "\\1" _os_id "${_line}")
            elseif(_line MATCHES "^VERSION_ID=")
                string(REGEX REPLACE "^VERSION_ID=\"?([^\"]*)\"?" "\\1" _os_ver "${_line}")
            endif()
        endforeach()
        if(_os_id)
            set(_distro_detail "(ID=${_os_id}")
            if(_os_ver)
                string(APPEND _distro_detail ", VERSION_ID=${_os_ver}")
            endif()
            string(APPEND _distro_detail ")")
        endif()
    elseif(EXISTS "/etc/redhat-release")
        file(READ "/etc/redhat-release" _distro_desc)
        string(STRIP "${_distro_desc}" _distro_desc)
    endif()
    _abi_line("系统" "${CMAKE_SYSTEM_NAME}")
    _abi_line("发行版" "${_distro_desc} ${_distro_detail}")
    _abi_line("内核" "${CMAKE_HOST_SYSTEM_VERSION}")
    _abi_line("架构" "${CMAKE_SYSTEM_PROCESSOR}")

    # ---- C 编译器 ----
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        set(_cc_desc "GCC (gcc)")
    elseif(CMAKE_C_COMPILER_ID MATCHES "Clang")
        set(_cc_desc "Clang (clang)")
    else()
        set(_cc_desc "${CMAKE_C_COMPILER_ID}")
    endif()
    _abi_line("编译器" "${_cc_desc} (${CMAKE_C_COMPILER})")
    _abi_line("编译器版本" "${CMAKE_C_COMPILER_VERSION}")

    # ---- Python ----
    # 注意：Python C 扩展绑定构建时解释器主次版本（X.Y），跨版本混用
    # 属 ABI 不兼容，请确保运行解释器与下方版本一致
    if(Python_EXECUTABLE)
        _abi_line("Python" "${Python_VERSION} (${Python_EXECUTABLE})")
    else()
        _abi_line("Python" "${Python_VERSION}")
    endif()
    _abi_line("Python 头文件" "${Python_INCLUDE_DIRS}")
    _abi_line("Python 库" "${Python_LIBRARIES}")

    # ---- Cython ----
    if(CYTHON_EXECUTABLE)
        execute_process(
            COMMAND ${CYTHON_EXECUTABLE} --version
            OUTPUT_VARIABLE _cython_out ERROR_VARIABLE _cython_out
            OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_STRIP_TRAILING_WHITESPACE)
        string(REGEX MATCH "[0-9]+\\.[0-9]+[^\r\n ]*" _cython_ver "${_cython_out}")
        # 回退形态为分号列表（<Python_EXECUTABLE>;-m;cython），展示时还原为空格分隔
        string(REPLACE ";" " " _cython_disp "${CYTHON_EXECUTABLE}")
        _abi_line("Cython" "${_cython_ver} (${_cython_disp})")
    else()
        _abi_line("Cython" "(未找到)")
    endif()

    # ---- 构建参数 ----
    if(CMAKE_BUILD_TYPE)
        _abi_line("构建类型" "${CMAKE_BUILD_TYPE}")
    else()
        _abi_line("构建类型" "(未指定，使用默认参数编译)")
    endif()
    set(_cfg_flags "${CMAKE_C_FLAGS}")
    if(CMAKE_BUILD_TYPE)
        string(TOUPPER "${CMAKE_BUILD_TYPE}" _cfg_upper)
        string(APPEND _cfg_flags " ${CMAKE_C_FLAGS_${_cfg_upper}}")
    endif()
    string(STRIP "${_cfg_flags}" _cfg_flags)
    if(_cfg_flags)
        _abi_line("C 标志" "${_cfg_flags}")
    else()
        _abi_line("C 标志" "(无)")
    endif()
    _abi_line("CMake 版本" "${CMAKE_VERSION}")
    _abi_line("生成器" "${CMAKE_GENERATOR}")

    # ---- 生成 abi_report.txt ----
    string(TIMESTAMP _ts UTC)
    set(_report "")
    string(APPEND _report
        "============================================================\n"
        " YomkServerPy 构建环境指纹 (ABI Report)\n"
        "============================================================\n"
        " 项目版本:        ${PROJECT_VERSION}\n"
        " 生成时间:        ${_ts}\n")
    foreach(_line IN LISTS _L)
        string(APPEND _report " ${_line}\n")
    endforeach()
    string(APPEND _report
        "============================================================\n"
        " 提示: Python C 扩展与构建时解释器主次版本绑定，运行解释器\n"
        "       版本须与上方 Python 版本一致，否则 import 会失败。\n"
        "============================================================\n")
    file(WRITE "${CMAKE_BINARY_DIR}/abi_report.txt" "${_report}")
    message(STATUS "[YomkPy ABI] 环境指纹报告已生成: ${CMAKE_BINARY_DIR}/abi_report.txt")
endfunction()
