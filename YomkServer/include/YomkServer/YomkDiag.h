#pragma once

#include <iostream>

#define YOMK_DIAG_ERR(msg)                                                                                    \
    do                                                                                                           \
    {                                                                                                            \
        std::cout << "[Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << msg << std::endl; \
    } while (0)
