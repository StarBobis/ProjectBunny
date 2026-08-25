#pragma once

// Low-level d3d12 log file open/close/flush helpers and DX12LogInfo macros.
// Split out of Core/DX12Diagnostics.h; include that umbrella header from existing code.
#include "DX12CoreModels.h"

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

// No-timestamp macros pure fprintf, DX11 speed
// Prefixed DX12 to avoid collision with DX12FrameAnalysis::LogInfo etc.

#define DX12LogInfo(fmt, ...) \
	do { if (DX12LogUtils::LogFile) fprintf(DX12LogUtils::LogFile, fmt, __VA_ARGS__); } while (0)

// Timestamped macros GetTickCount64() prefix + direct fprintf
// Format:  raw_tick\tmessage\n
// Uses string-literal concatenation: "%llu\t" fmt

#define DX12LogInfoTS(fmt, ...) \
	do { if (DX12LogUtils::LogFile) fprintf(DX12LogUtils::LogFile, "%llu\t" fmt, \
		static_cast<unsigned long long>(GetTickCount64()), __VA_ARGS__); } while (0)
