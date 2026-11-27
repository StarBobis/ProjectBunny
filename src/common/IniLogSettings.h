#pragma once

#include <windows.h>
#include "IniLogVerbosity.h"

namespace migoto {

inline LogVerbosity ReadIniLogVerbosity(const wchar_t *ini_path) noexcept
{
    // Upstream treats both absent and explicitly empty levels as legacy mode.
    wchar_t level[256] = {};
    GetPrivateProfileStringW(L"Logging", L"log_level", L"", level, 256, ini_path);
    return ParseLogVerbosity(level,
        GetPrivateProfileIntW(L"Logging", L"calls", 0, ini_path) != 0,
        GetPrivateProfileIntW(L"Logging", L"debug", 0, ini_path) != 0);
}

} // namespace migoto
