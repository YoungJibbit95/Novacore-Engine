#pragma once

#include <cstdlib>
#include <iostream>

#define RIFTLINE_ASSERT(condition, message)                                      \
    do {                                                                         \
        if (!(condition)) {                                                       \
            std::cerr << "[assert] " << (message) << " (" << __FILE__ << ":"    \
                      << __LINE__ << ")" << std::endl;                          \
            std::abort();                                                        \
        }                                                                        \
    } while (false)

