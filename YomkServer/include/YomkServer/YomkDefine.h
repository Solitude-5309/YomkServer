#pragma once

#include <iostream>

#if defined(_WIN32) || defined(_WIN64)
// Windows 平台（32/64位）
#if defined(YOMKSERVER_LIBRARY)
#define YOMKSERVER_EXPORT __declspec(dllexport)
#else
#define YOMKSERVER_EXPORT __declspec(dllimport)
#endif
#elif defined(__linux__)
#define YOMKSERVER_EXPORT __attribute__((visibility("default")))
#else
#define YOMKSERVER_EXPORT __attribute__((visibility("default")))
#endif

// do-while(0) 包裹保证语句宏在无大括号 if/else 中安全（防悬垂 else），尾分号由调用方提供
#define YOMK_ERR_POS_LOG(msg)                                                                                    \
    do                                                                                                           \
    {                                                                                                            \
        std::cout << "[Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << msg << std::endl; \
    } while (0)
