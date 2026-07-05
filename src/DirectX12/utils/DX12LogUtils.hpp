#pragma once

#include <Windows.h>

#include <stdarg.h>
#include <stdio.h>

// ── DX11-aligned global log utilities ──────────────────────────────
//
//   DX12LogInfo  / DX12LogDebug   — direct fprintf, zero overhead, no timestamp
//   DX12LogInfoTS / DX12LogDebugTS — direct fprintf + GetTickCount64() prefix
//
// Format (TS variants):  raw_tick<TAB>message<NEWLINE>
// Format (no-TS variants): message as-is, caller controls newline
//
// All variants: no queue, no thread, no lock. Just like DX11 LogInfo.

class DX12LogUtils
{
public:
	static FILE *LogFile;

	static bool Open(const wchar_t *path)
	{
		LogFile = _wfsopen(path, L"w", _SH_DENYNO);
		return LogFile != nullptr;
	}

	static void Close()
	{
		if (LogFile) {
			fclose(LogFile);
			LogFile = nullptr;
		}
	}

	static void Flush()
	{
		if (LogFile)
			fflush(LogFile);
	}

	static void MakeLogTimePlain(char *buffer, size_t bufferSize)
	{
		if (!buffer || !bufferSize)
			return;
		snprintf(buffer, bufferSize, "%llu",
			static_cast<unsigned long long>(GetTickCount64()));
	}
};

// ── No-timestamp macros — pure fprintf, DX11 speed ──────────────────
// Prefixed DX12 to avoid collision with DX12FrameAnalysis::LogInfo etc.

#if defined(_DEBUG)
#define DX12LogDebug(fmt, ...) \
	do { if (DX12LogUtils::LogFile) fprintf(DX12LogUtils::LogFile, fmt, __VA_ARGS__); } while (0)
#else
#define DX12LogDebug(fmt, ...) do {} while (0)
#endif

#define DX12LogInfo(fmt, ...) \
	do { if (DX12LogUtils::LogFile) fprintf(DX12LogUtils::LogFile, fmt, __VA_ARGS__); } while (0)

// ── Timestamped macros — GetTickCount64() prefix + direct fprintf ────
// Format:  raw_tick\tmessage\n
// Uses string-literal concatenation: "%llu\t" fmt

#if defined(_DEBUG)
#define DX12LogDebugTS(fmt, ...) \
	do { if (DX12LogUtils::LogFile) fprintf(DX12LogUtils::LogFile, "%llu\t" fmt, \
		static_cast<unsigned long long>(GetTickCount64()), __VA_ARGS__); } while (0)
#else
#define DX12LogDebugTS(fmt, ...) do {} while (0)
#endif

#define DX12LogInfoTS(fmt, ...) \
	do { if (DX12LogUtils::LogFile) fprintf(DX12LogUtils::LogFile, "%llu\t" fmt, \
		static_cast<unsigned long long>(GetTickCount64()), __VA_ARGS__); } while (0)
