# ============================================================
# YomkServer ABI 环境指纹模块
#
# 用途：防止下游用户因编译环境不一致产生 ABI 兼容问题
#   1. 自动检测 Linux 发行版（/etc/os-release）
#   2. 编译器预选：g++ 优先，回退 clang++（须在根 project() 之前 include 本文件）
#   3. yomk_abi_report()：project() 之后调用，采集/打印编译环境指纹，
#      并生成 ${CMAKE_BINARY_DIR}/abi_report.txt 供随库安装、用户比对
# ============================================================

# ------------- 编译器预选（project() 之前执行） -------------
# 仅当用户未显式指定编译器时介入：
#   - 命令行 -DCMAKE_CXX_COMPILER=xxx → 尊重用户，不干预
#   - 环境变量 $CXX                    → 尊重用户，不干预
if(UNIX AND NOT DEFINED CMAKE_CXX_COMPILER AND NOT DEFINED ENV{CXX})
    find_program(YOMK_ABI_GXX_PATH g++)
    find_program(YOMK_ABI_CLANGXX_PATH clang++)
    if(YOMK_ABI_GXX_PATH)
        # project() 会采用该普通变量的值完成编译器探测
        set(CMAKE_CXX_COMPILER "${YOMK_ABI_GXX_PATH}")
        message(STATUS "[Yomk ABI] 未显式指定编译器，自动选择 g++: ${YOMK_ABI_GXX_PATH}")
    elseif(YOMK_ABI_CLANGXX_PATH)
        set(CMAKE_CXX_COMPILER "${YOMK_ABI_CLANGXX_PATH}")
        message(STATUS "[Yomk ABI] 未找到 g++，回退使用 clang++: ${YOMK_ABI_CLANGXX_PATH}")
    else()
        message(FATAL_ERROR "[Yomk ABI] 未找到 g++ 或 clang++，请先安装: sudo apt-get install g++")
    endif()
    unset(YOMK_ABI_GXX_PATH CACHE)
    unset(YOMK_ABI_CLANGXX_PATH CACHE)
endif()

# ------------- ABI 环境指纹采集/打印/生成（project() 之后调用） -------------
function(yomk_abi_report)
    # 仅针对 Linux 场景；其他平台只做简短提示，不生成报告
    if(NOT UNIX OR NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
        message(STATUS "[Yomk ABI] 非 Linux 环境，跳过 ABI 环境指纹报告")
        return()
    endif()

    set(_L "")
    macro(_abi_line)
        # _abi_line("标签" "值") → 追加 "标签: 值" 到 _L 并 STATUS 打印
        list(APPEND _L "${ARGV0}: ${ARGV1}")
        message(STATUS "[Yomk ABI] ${ARGV0}: ${ARGV1}")
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

    # ---- 编译器 ----
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set(_cxx_desc "GCC (g++)")
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        set(_cxx_desc "Clang (clang++)")
        message(STATUS "[Yomk ABI] *** 注意: 当前使用 clang++ 编译，与常见 g++ 产物混用存在 ABI 风险，请确保下游使用同一编译器 ***")
    else()
        set(_cxx_desc "${CMAKE_CXX_COMPILER_ID}")
    endif()
    _abi_line("编译器" "${_cxx_desc} (${CMAKE_CXX_COMPILER})")
    _abi_line("编译器版本" "${CMAKE_CXX_COMPILER_VERSION}")

    # ---- 标准库 / 双 ABI 宏（编译微型探针并运行，继承 CMAKE_CXX_FLAGS） ----
    set(_probe_dir "${CMAKE_BINARY_DIR}/abi_probe")
    file(WRITE "${_probe_dir}/probe.cpp" [=[
#include <cstdio>
#include <string>
int main() {
#if defined(_LIBCPP_VERSION)
    std::printf("stdlib=libc++\nlibcxx_version=%d\n", (int)_LIBCPP_VERSION);
#elif defined(__GLIBCXX__)
    std::printf("stdlib=libstdc++\nglibcxx_date=%ld\ncxx11abi=%d\n",
                (long)__GLIBCXX__,
#  if defined(_GLIBCXX_USE_CXX11_ABI)
                (int)_GLIBCXX_USE_CXX11_ABI
#  else
                0
#  endif
                );
#else
    std::printf("stdlib=unknown\n");
#endif
    return 0;
}
]=])
    set(_stdlib_desc "检测失败")
    set(_glibcxx_val "")
    set(_cxx11abi_val "")
    execute_process(
        COMMAND ${CMAKE_CXX_COMPILER} ${CMAKE_CXX_FLAGS} -fPIC
                "${_probe_dir}/probe.cpp" -o "${_probe_dir}/probe.bin"
        RESULT_VARIABLE _probe_build_rc
        OUTPUT_QUIET ERROR_QUIET)
    if(_probe_build_rc EQUAL 0)
        execute_process(
            COMMAND "${_probe_dir}/probe.bin"
            RESULT_VARIABLE _probe_run_rc
            OUTPUT_VARIABLE _probe_out ERROR_QUIET)
        if(_probe_run_rc EQUAL 0)
            string(REGEX MATCH "stdlib=[^\r\n]*" _m "${_probe_out}")
            string(REPLACE "stdlib=" "" _stdlib_name "${_m}")
            string(REGEX MATCH "glibcxx_date=[^\r\n]*" _m "${_probe_out}")
            string(REPLACE "glibcxx_date=" "" _glibcxx_val "${_m}")
            string(REGEX MATCH "cxx11abi=[^\r\n]*" _m "${_probe_out}")
            string(REPLACE "cxx11abi=" "" _cxx11abi_val "${_m}")
            string(REGEX MATCH "libcxx_version=[^\r\n]*" _m "${_probe_out}")
            string(REPLACE "libcxx_version=" "" _libcxx_ver "${_m}")
            set(_stdlib_desc "${_stdlib_name}")
            if(_glibcxx_val)
                set(_stdlib_desc "${_stdlib_desc} (__GLIBCXX__=${_glibcxx_val})")
            elseif(_libcxx_ver)
                set(_stdlib_desc "${_stdlib_desc} (_LIBCPP_VERSION=${_libcxx_ver})")
            endif()
        endif()
    endif()
    file(REMOVE_RECURSE "${_probe_dir}")
    _abi_line("标准库" "${_stdlib_desc}")
    if(_cxx11abi_val)
        _abi_line("C++11 双 ABI" "_GLIBCXX_USE_CXX11_ABI=${_cxx11abi_val}")
    endif()

    # ---- 编译参数 ----
    # CMAKE_CXX_EXTENSIONS 未定义时 CMake 默认启用扩展（-std=gnu++xx）
    if(NOT DEFINED CMAKE_CXX_EXTENSIONS OR CMAKE_CXX_EXTENSIONS)
        set(_std_flag "gnu++${CMAKE_CXX_STANDARD}")
    else()
        set(_std_flag "c++${CMAKE_CXX_STANDARD}")
    endif()
    _abi_line("C++ 标准" "${CMAKE_CXX_STANDARD} (${_std_flag})")
    if(CMAKE_BUILD_TYPE)
        _abi_line("构建类型" "${CMAKE_BUILD_TYPE}")
    else()
        _abi_line("构建类型" "(未指定，使用默认参数编译)")
    endif()
    set(_cfg_flags "${CMAKE_CXX_FLAGS}")
    if(CMAKE_BUILD_TYPE)
        string(TOUPPER "${CMAKE_BUILD_TYPE}" _cfg_upper)
        string(APPEND _cfg_flags " ${CMAKE_CXX_FLAGS_${_cfg_upper}}")
    endif()
    string(STRIP "${_cfg_flags}" _cfg_flags)
    if(_cfg_flags)
        _abi_line("CXX 标志" "${_cfg_flags}")
    else()
        _abi_line("CXX 标志" "(无)")
    endif()
    _abi_line("CMake 版本" "${CMAKE_VERSION}")
    _abi_line("生成器" "${CMAKE_GENERATOR}")

    # ---- 生成 abi_report.txt ----
    string(TIMESTAMP _ts UTC)
    set(_report "")
    string(APPEND _report
        "============================================================\n"
        " YomkServer 构建环境指纹 (ABI Report)\n"
        "============================================================\n"
        " 项目版本:        ${PROJECT_VERSION}\n"
        " 生成时间:        ${_ts}\n")
    foreach(_line IN LISTS _L)
        string(APPEND _report " ${_line}\n")
    endforeach()
    string(APPEND _report
        "============================================================\n"
        " 提示: 下游编译环境与上述不一致时可能出现 ABI 兼容问题，\n"
        "       建议使用相同发行版、编译器版本与编译参数。\n"
        "============================================================\n")
    file(WRITE "${CMAKE_BINARY_DIR}/abi_report.txt" "${_report}")
    message(STATUS "[Yomk ABI] 环境指纹报告已生成: ${CMAKE_BINARY_DIR}/abi_report.txt")
endfunction()
