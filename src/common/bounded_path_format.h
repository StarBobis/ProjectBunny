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
