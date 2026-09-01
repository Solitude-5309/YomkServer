#pragma once

#include <iostream>

#if defined(_WIN32) || defined(_WIN64)
#if defined(YOMKSERVER_LIBRARY)
#define YOMKSERVER_EXPORT __declspec(dllexport)
#else
#define YOMKSERVER_EXPORT __declspec(dllimport)
#endif
#else
#define YOMKSERVER_EXPORT __attribute__((visibility("default")))
#endif

#define YOMK_ERR_POS_LOG(msg)                                                                                    \
    do                                                                                                           \
    {                                                                                                            \
        std::cout << "[Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << msg << std::endl; \
    } while (0)
