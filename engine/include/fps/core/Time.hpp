#pragma once

#include <chrono>

namespace riftline::core {

using Clock = std::chrono::steady_clock;
using Seconds = std::chrono::duration<double>;

} // namespace riftline::core

