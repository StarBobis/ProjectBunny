#pragma once

// F-200: fixed-buffer path formatting with one failure rule.
//
// Returns false when the complete formatted path (including the terminating
// NUL) does not fit. On that path the destination is not modified, so callers
// can reject the whole operation instead of handing a truncated name to a file
// API. A successful call is byte-for-byte identical to swprintf_s.
//
// The preflight pass (_vscwprintf) is what makes "never truncate" true: the
// destination is written only when the complete result is known to fit.

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cwchar>

#ifndef _WINDOWS_
#include <windows.h>
#endif
#include <shlwapi.h>

#ifdef _MSC_VER
// PathAppendBounded uses PathAppendW. Keep the header self-contained for
// targets (D3DCompiler_46) whose CMake link line does not list Shlwapi.
#pragma comment(lib, "shlwapi.lib")
#endif

inline bool FormatPathBounded(wchar_t *path, size_t pathCount, const wchar_t *format, ...)
{
	if (!path || pathCount == 0 || !format)
		return false;

	va_list args;
	va_start(args, format);
	const int needed = _vscwprintf(format, args);
	va_end(args);
	if (needed < 0 || static_cast<size_t>(needed) >= pathCount)
		return false;

	va_start(args, format);
	const int written = _vsnwprintf_s(path, pathCount, _TRUNCATE, format, args);
	va_end(args);
	return written == needed;
}

// Concatenation counterparts for the sites that build a path out of wcscat /
// wcscpy / PathAppendW instead of a format string. Same rule: the complete
// result (including the terminating NUL) fits, or false is returned and the
// destination is left untouched. No truncation is ever handed to a file API.

inline bool CopyPathBounded(wchar_t *path, size_t pathCount, const wchar_t *source)
{
	if (!path || pathCount == 0 || !source)
		return false;

	const size_t length = wcslen(source);
	if (length + 1 > pathCount)
		return false;
	memcpy(path, source, (length + 1) * sizeof(wchar_t));
	return true;
}

inline bool AppendPathBounded(wchar_t *path, size_t pathCount, const wchar_t *suffix)
{
	if (!path || pathCount == 0 || !suffix)
		return false;

	const size_t length = wcslen(path);
	const size_t suffixLength = wcslen(suffix);
	if (length + suffixLength + 1 > pathCount)
		return false;
	memcpy(path + length, suffix, (suffixLength + 1) * sizeof(wchar_t));
	return true;
}

// PathAppendW with the return value made contractual. PathAppendW may leave a
// partial result in the destination when it fails, so the append happens on a
// copy and the destination only changes when the complete path is known to fit.
inline bool PathAppendBounded(wchar_t *path, size_t pathCount, const wchar_t *more)
{
	if (!path || pathCount == 0 || !more)
		return false;

	const size_t length = wcslen(path);
	if (length + 1 > static_cast<size_t>(MAX_PATH) || length + 1 > pathCount)
		return false;

	wchar_t buffer[MAX_PATH];
	memcpy(buffer, path, (length + 1) * sizeof(wchar_t));
	if (!PathAppendW(buffer, more))
		return false;

	const size_t combinedLength = wcslen(buffer);
	if (combinedLength + 1 > pathCount)
		return false;
	memcpy(path, buffer, (combinedLength + 1) * sizeof(wchar_t));
	return true;
}
