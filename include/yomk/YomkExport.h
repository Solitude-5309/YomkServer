#pragma once

#if defined(_WIN32) || defined(_WIN64)
#if defined(YOMK_LIBRARY)
#define YOMK_EXPORT __declspec(dllexport)
#else
#define YOMK_EXPORT __declspec(dllimport)
#endif
#else
#define YOMK_EXPORT __attribute__((visibility("default")))
#endif
