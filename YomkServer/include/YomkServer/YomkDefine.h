#pragma once

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
