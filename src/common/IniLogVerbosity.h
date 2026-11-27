#pragma once

#include <cwchar>

namespace migoto {

enum class LogVerbosity : signed char {
    INVALID = -1, DISABLED = 0, WARNING = 1, INFO = 2, DEBUG = 3
};

inline LogVerbosity ParseLogVerbosity(const wchar_t* level, bool calls, bool debug) noexcept
{
    if (level && *level) {
        if (!_wcsicmp(level, L"disabled")) return LogVerbosity::DISABLED;
        if (!_wcsicmp(level, L"warning")) return LogVerbosity::WARNING;
        if (!_wcsicmp(level, L"info")) return LogVerbosity::INFO;
        if (!_wcsicmp(level, L"debug")) return LogVerbosity::DEBUG;
    }
    // Missing, empty and unrecognized levels use the legacy option pair.
    return calls ? (debug ? LogVerbosity::DEBUG : LogVerbosity::INFO) : LogVerbosity::DISABLED;
}

} // namespace migoto
