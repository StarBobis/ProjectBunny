#include "Logging.h"
#include <Windows.h>
#include <cwchar>
#include <share.h>

LogVerbosity ParseLogVerbosity(const wchar_t* level, bool calls, bool debug) noexcept
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

void InitializeLogging(const wchar_t* ini_path, const wchar_t* log_path) noexcept
{
    // Upstream treats both absent and explicitly empty levels as legacy mode.
    wchar_t level[256] = {};
    GetPrivateProfileStringW(L"Logging", L"log_level", L"", level, 256, ini_path);
    gLogVerbosity = ParseLogVerbosity(level,
        GetPrivateProfileIntW(L"Logging", L"calls", 0, ini_path) != 0,
        GetPrivateProfileIntW(L"Logging", L"debug", 0, ini_path) != 0);
    if (gLogVerbosity != LogVerbosity::DISABLED && !LogFile)
        LogFile = _wfsopen(log_path, L"w", _SH_DENYNO);
    if (!LogFile) gLogVerbosity = LogVerbosity::DISABLED;
    gLogDebug = gLogVerbosity == LogVerbosity::DEBUG;
}
