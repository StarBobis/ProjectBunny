#pragma once
#include <Windows.h>
#include <cstdio>
#include "log.h"

// Missing, empty or invalid levels use legacy calls/debug; disabled overrides them.
LogVerbosity ParseLogVerbosity(const wchar_t* level, bool calls, bool debug) noexcept;
// Read the source file directly before ParseIniFile replaces the old INI map.
void InitializeLogging(const wchar_t* ini_path, const wchar_t* log_path) noexcept;
