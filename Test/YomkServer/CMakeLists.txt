# Test/YomkServer：YomkServer 非 Modules 源文件（YomkServer/YomkService/YomkSimpleThreadPool/YomkAPI）
# 的正式全面覆盖测试；目录内扁平布局，每个被测主题一个独立可执行程序，目标按闭环增量追加
# 引用非导出内部头（如 YomkSimpleThreadPool.h）需要源码目录
include_directories(${YomkServer_DIR}/src)

# MC8：sanitizer 开关（off/asan/tsan），选项通过 PUBLIC 附加到 YomkServer 库，
# 依赖方（Examples、旧测试、新测试）自动继承，避免逐个目标设置
set(YOMK_TEST_SANITIZER off CACHE STRING "Sanitizer for tests: off/asan/tsan")
set_property(CACHE YOMK_TEST_SANITIZER PROPERTY STRINGS off asan tsan)

if(NOT YOMK_TEST_SANITIZER STREQUAL "off")
    if(YOMK_TEST_SANITIZER STREQUAL "asan")
        set(_YOMK_SAN_FLAGS -fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
    elseif(YOMK_TEST_SANITIZER STREQUAL "tsan")
        set(_YOMK_SAN_FLAGS -fsanitize=thread -fno-omit-frame-pointer -g -O1)
    else()
        message(FATAL_ERROR "Unknown YOMK_TEST_SANITIZER=${YOMK_TEST_SANITIZER}, expected off/asan/tsan")
    endif()

    target_compile_options(YomkServer PUBLIC ${_YOMK_SAN_FLAGS})
    target_link_options(YomkServer PUBLIC ${_YOMK_SAN_FLAGS})
    message(STATUS "YOMK_TEST_SANITIZER=${YOMK_TEST_SANITIZER}, flags=${_YOMK_SAN_FLAGS}")
endif()

# MC9：静态代码分析开关（OFF/ON），默认 OFF，不污染默认构建
set(YOMK_TEST_STATIC_ANALYZE OFF CACHE BOOL "Enable clang-tidy/cppcheck static analysis targets")
if(YOMK_TEST_STATIC_ANALYZE)
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/static-analysis)

    # 被测范围：非 Modules 的 YomkServer 源文件与公共头
    file(GLOB _YOMK_SA_SRCS
        ${YomkServer_DIR}/src/*.cpp
        ${YomkServer_DIR}/src/*.h
        ${YomkServer_DIR}/include/YomkServer/*.h
    )

    # clang-tidy 目标：依赖 compile_commands.json
    find_program(CLANG_TIDY_EXE NAMES clang-tidy)
    if(CLANG_TIDY_EXE)
        set(_YOMK_TIDY_CHECKS
            "-checks=bugprone-*,-bugprone-macro-parentheses,-bugprone-easily-swappable-parameters,cppcoreguidelines-*,-cppcoreguidelines-owning-memory,-cppcoreguidelines-macro-usage,-cppcoreguidelines-pro-bounds-array-to-pointer-decay,-cppcoreguidelines-special-member-functions,-cppcoreguidelines-explicit-virtual-functions,-cppcoreguidelines-non-private-member-variables-in-classes,-cppcoreguidelines-avoid-non-const-global-variables,clang-analyzer-*,performance-*,-performance-unnecessary-value-param,portability-*"
        )
        add_custom_target(YomkServer-clang-tidy
            COMMAND ${CLANG_TIDY_EXE}
                ${_YOMK_TIDY_CHECKS}
                -header-filter=${YomkServer_DIR}
                -p=${CMAKE_BINARY_DIR}
                ${_YOMK_SA_SRCS}
                > ${CMAKE_BINARY_DIR}/static-analysis/clang-tidy.log 2>&1
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            COMMENT "Running clang-tidy on YomkServer non-Modules sources"
        )
    else()
        message(STATUS "clang-tidy not found, YomkServer-clang-tidy target disabled")
    endif()

    # Cppcheck 目标
    find_program(CPPCHECK_EXE NAMES cppcheck)
    if(CPPCHECK_EXE)
        add_custom_target(YomkServer-cppcheck
            COMMAND ${CPPCHECK_EXE}
                --language=c++
                --std=c++17
                --enable=warning,style,performance,portability,information
                --suppress=missingInclude
                --suppress=noExplicitConstructor
                --suppress=useStlAlgorithm
                --inline-suppr
                --template=gcc
                --error-exitcode=0
                -I ${YomkServer_DIR}/include
                -I ${YomkServer_DIR}/src
                ${_YOMK_SA_SRCS}
                > ${CMAKE_BINARY_DIR}/static-analysis/cppcheck.log 2>&1
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            COMMENT "Running cppcheck on YomkServer non-Modules sources"
        )
    else()
        message(STATUS "cppcheck not found, YomkServer-cppcheck target disabled")
    endif()
endif()

# -------------sl TestYomkSimpleThreadPool ----------------
set(TestYomkSimpleThreadPool_SOURCES 
    ${TestYomkServer_DIR}/YomkServer/TestYomkSimpleThreadPool.cpp
)
add_executable(TestYomkSimpleThreadPool 
    ${TestYomkSimpleThreadPool_SOURCES}
)
target_link_libraries(TestYomkSimpleThreadPool PRIVATE
    ${YomkServerLib}
)
# -------------el TestYomkSimpleThreadPool ----------------

# -------------sl TestYomkService ----------------
set(TestYomkService_SOURCES 
    ${TestYomkServer_DIR}/YomkServer/TestYomkService.cpp
)
add_executable(TestYomkService 
    ${TestYomkService_SOURCES}
)
target_link_libraries(TestYomkService PRIVATE
    ${YomkServerLib}
)
# -------------el TestYomkService ----------------

# -------------sl TestYomkWeakFunc ----------------
set(TestYomkWeakFunc_SOURCES 
    ${TestYomkServer_DIR}/YomkServer/TestYomkWeakFunc.cpp
)
add_executable(TestYomkWeakFunc 
    ${TestYomkWeakFunc_SOURCES}
)
target_link_libraries(TestYomkWeakFunc PRIVATE
    ${YomkServerLib}
)
# -------------el TestYomkWeakFunc ----------------

# -------------sl TestYomkServer ----------------
set(TestYomkServer_SOURCES 
    ${TestYomkServer_DIR}/YomkServer/TestYomkServer.cpp
)
add_executable(TestYomkServer 
    ${TestYomkServer_SOURCES}
)
target_link_libraries(TestYomkServer PRIVATE
    ${YomkServerLib}
)
# -------------el TestYomkServer ----------------

# -------------sl TestYomkShutdownSeq ----------------
set(TestYomkShutdownSeq_SOURCES 
    ${TestYomkServer_DIR}/YomkServer/TestYomkShutdownSeq.cpp
)
add_executable(TestYomkShutdownSeq 
    ${TestYomkShutdownSeq_SOURCES}
)
target_link_libraries(TestYomkShutdownSeq PRIVATE
    ${YomkServerLib}
)
# -------------el TestYomkShutdownSeq ----------------

# -------------sl TestYomkAPINotInit ----------------
set(TestYomkAPINotInit_SOURCES 
    ${TestYomkServer_DIR}/YomkServer/TestYomkAPINotInit.cpp
)
add_executable(TestYomkAPINotInit 
    ${TestYomkAPINotInit_SOURCES}
)
target_link_libraries(TestYomkAPINotInit PRIVATE
    ${YomkServerLib}
)
# -------------el TestYomkAPINotInit ----------------

# -------------sl TestYomkAPILifecycle ----------------
set(TestYomkAPILifecycle_SOURCES 
    ${TestYomkServer_DIR}/YomkServer/TestYomkAPILifecycle.cpp
)
add_executable(TestYomkAPILifecycle 
    ${TestYomkAPILifecycle_SOURCES}
)
target_link_libraries(TestYomkAPILifecycle PRIVATE
    ${YomkServerLib}
)
# -------------el TestYomkAPILifecycle ----------------

# -------------sl TestYomkBoot ----------------
set(TestYomkBoot_SOURCES 
    ${TestYomkServer_DIR}/YomkServer/TestYomkBoot.cpp
)
add_executable(TestYomkBoot 
    ${TestYomkBoot_SOURCES}
)
target_link_libraries(TestYomkBoot PRIVATE
    ${YomkServerLib}
)
# -------------el TestYomkBoot ----------------

# -------------sl TestYomkConcurrency ----------------
set(TestYomkConcurrency_SOURCES 
    ${TestYomkServer_DIR}/YomkServer/TestYomkConcurrency.cpp
)
add_executable(TestYomkConcurrency 
    ${TestYomkConcurrency_SOURCES}
)
target_link_libraries(TestYomkConcurrency PRIVATE
    ${YomkServerLib}
)
# -------------el TestYomkConcurrency ----------------

# 测试程序仅供框架内部开发验证使用，不执行 install
