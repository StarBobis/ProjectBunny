#pragma once

using namespace std;

#include <string>
#include <ctime>

// Wrappers to make logging cleaner.

extern FILE *LogFile;
extern bool gLogDebug;

// Note that for now I've left the definitions of LogFile and LogDebug as they
// were - either declared locally in a file, as an extern, or from another
// namespace altogether. At some point this needs to be cleaned up, but it's
// probably not worth doing so unless we were switching to use a central
// logging framework.

#if MIGOTO_DX == 11
#include "DirectX11/McpDiagnostics.h"
#include <cstdio>
#include <cwchar>

// DX11 levels are independent of the DX12 logging contract.
#include "IniLogVerbosity.h"
using migoto::LogVerbosity;
extern LogVerbosity gLogVerbosity;

inline bool Dx11LogEnabled(LogVerbosity level) noexcept
{
    return (LogFile && gLogVerbosity >= level) ||
        (Dx11Diagnostics::attached.load(std::memory_order_relaxed) &&
            Dx11Diagnostics::WantsLog(static_cast<unsigned>(level)));
}

inline void Dx11WriteLogV(LogVerbosity level, const char* format, va_list args)
{
    if (Dx11Diagnostics::attached.load(std::memory_order_relaxed))
        Dx11Diagnostics::RecordLog(static_cast<unsigned>(level), format, args);
    if (LogFile && gLogVerbosity >= level) vfprintf(LogFile, format, args);
}

inline void Dx11WriteLogV(LogVerbosity level, const wchar_t* format, va_list args)
{
    if (Dx11Diagnostics::attached.load(std::memory_order_relaxed))
        Dx11Diagnostics::RecordLog(static_cast<unsigned>(level), format, args);
    if (LogFile && gLogVerbosity >= level) vfwprintf(LogFile, format, args);
}

template<class Char>
inline void Dx11WriteLog(LogVerbosity level, const Char* format, ...)
{
    va_list args;
    va_start(args, format);
    Dx11WriteLogV(level, format, args);
    va_end(args);
}

#define DX11_VLOG(level, fmt, args) \
    do { if (Dx11LogEnabled(LogVerbosity::level)) Dx11WriteLogV(LogVerbosity::level, fmt, args); } while (0)

#define LogWarning(fmt, ...) \
    do { if (Dx11LogEnabled(LogVerbosity::WARNING)) Dx11WriteLog(LogVerbosity::WARNING, fmt, __VA_ARGS__); } while (0)
#define LogWarningW LogWarning
#define vLogWarning(fmt, args) DX11_VLOG(WARNING, fmt, args)
#define vLogWarningW vLogWarning
#define LogInfo(fmt, ...) \
    do { if (Dx11LogEnabled(LogVerbosity::INFO)) Dx11WriteLog(LogVerbosity::INFO, fmt, __VA_ARGS__); } while (0)
#define LogInfoW LogInfo
#define vLogInfo(fmt, args) DX11_VLOG(INFO, fmt, args)
#define vLogInfoW vLogInfo
#define LogDebug(fmt, ...) \
    do { if (Dx11LogEnabled(LogVerbosity::DEBUG)) Dx11WriteLog(LogVerbosity::DEBUG, fmt, __VA_ARGS__); } while (0)
#define LogDebugW LogDebug
#define vLogDebug(fmt, args) DX11_VLOG(DEBUG, fmt, args)
#define vLogDebugW vLogDebug
#else
#define LogInfo(fmt, ...) \
	do { if (LogFile) fprintf(LogFile, fmt, __VA_ARGS__); } while (0)
#define vLogInfo(fmt, va_args) \
	do { if (LogFile) vfprintf(LogFile, fmt, va_args); } while (0)
#define LogInfoW(fmt, ...) \
	do { if (LogFile) fwprintf(LogFile, fmt, __VA_ARGS__); } while (0)
#define vLogInfoW(fmt, va_args) \
	do { if (LogFile) vfwprintf(LogFile, fmt, va_args); } while (0)

#define LogDebug(fmt, ...) \
	do { if (gLogDebug) LogInfo(fmt, __VA_ARGS__); } while (0)
#define vLogDebug(fmt, va_args) \
	do { if (gLogDebug) vLogInfo(fmt, va_args); } while (0)
#define LogDebugW(fmt, ...) \
	do { if (gLogDebug) LogInfoW(fmt, __VA_ARGS__); } while (0)
#define vLogDebugW(fmt, va_args) \
	do { if (gLogDebug) vLogInfoW(fmt, va_args); } while (0)

#endif

// Aliases for the above functions that we use to denote that omitting the
// newline was done intentionally. For now this is just for our reference, but
// later we might actually make the default function insert a newline:
#define LogInfoNoNL LogInfo
#define LogInfoWNoNL LogInfoW
#define LogDebugNoNL LogDebug
#define LogDebugWNoNL LogDebugW

static string LogTime()
{
	string timeStr;
	char cTime[32];
	tm timestruct;

	time_t ltime = time(0);
	localtime_s(&timestruct, &ltime);
	asctime_s(cTime, sizeof(cTime), &timestruct);

	timeStr = cTime;
	return timeStr;
}

