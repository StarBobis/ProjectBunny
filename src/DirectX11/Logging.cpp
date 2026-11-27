#include "Logging.h"
#include "IniLogSettings.h"
#include "CrashDiagnostics.h"
#include <Windows.h>
#include <cwchar>
#include <share.h>

void InitializeLogging(const wchar_t* ini_path, const wchar_t* log_path) noexcept
{
    InitializeCrashDiagnostics(ini_path, log_path);
    gLogVerbosity = migoto::ReadIniLogVerbosity(ini_path);
    if (gLogVerbosity != LogVerbosity::DISABLED && !LogFile)
        LogFile = _wfsopen(log_path, L"w", _SH_DENYNO);
    if (!LogFile) gLogVerbosity = LogVerbosity::DISABLED;
    gLogDebug = gLogVerbosity == LogVerbosity::DEBUG;
}
