#pragma once

#include <chrono>
#include <cstdint>

namespace migoto {

// Monotonic microseconds, shared by frame timing and expression seeding.
inline std::uint64_t GetSystemTicks() noexcept
{
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

} // namespace migoto
