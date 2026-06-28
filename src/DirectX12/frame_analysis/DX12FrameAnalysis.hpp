#pragma once

#include <Windows.h>
#include <Shlwapi.h>
#include <share.h>
#include <stdarg.h>
#include <stdio.h>
#include <time.h>

#include "DX12Json.hpp"
#include "DX12FrameAnalysisTypes.hpp"
#include "DX12ResourceTracker.h"
#include "DX12State.h"

class DX12FrameAnalysisState
{
public:
	SRWLOCK lock;
	bool active = false;
	bool captureRequested = false;
	bool capturing = false;
	bool dumpPending = false;
	volatile LONG activeFast = 0;
	volatile LONG captureRequestedFast = 0;
	volatile LONG capturingFast = 0;
	wchar_t path[MAX_PATH] = {};
	FILE *log = nullptr;
	unsigned long long lineNo = 0;

	DX12FrameAnalysisState()
	{
		InitializeSRWLock(&lock);
	}

	~DX12FrameAnalysisState()
	{
		CloseLogLocked();
	}

	static DX12FrameAnalysisState &Get()
	{
		static DX12FrameAnalysisState state;
		return state;
	}

	bool Begin()
	{
		wchar_t modulePath[MAX_PATH];
		wchar_t subdir[MAX_PATH];
		__time64_t now;
		struct tm localTime;

		if (!GetModuleFileNameW(DX12GetModule(), modulePath, ARRAYSIZE(modulePath)))
			return false;
		PathRemoveFileSpecW(modulePath);

		_time64(&now);
		if (_localtime64_s(&localTime, &now) != 0)
			return false;
		wcsftime(subdir, ARRAYSIZE(subdir), L"FrameAnalysis-%Y-%m-%d-%H%M%S", &localTime);
		PathAppendW(modulePath, subdir);

		if (!CreateDirectoryW(modulePath, nullptr)) {
			DWORD error = GetLastError();
			DX12Log("Error creating frame analysis directory: %lu path=%S\n", error, modulePath);
			return false;
		}

		AcquireSRWLockExclusive(&lock);
		CloseLogLocked();
		wcsncpy_s(path, modulePath, _TRUNCATE);
		lineNo = 0;
		active = true;
		captureRequested = false;
		capturing = false;
		dumpPending = false;
		InterlockedExchange(&activeFast, 1);
		InterlockedExchange(&captureRequestedFast, 0);
		InterlockedExchange(&capturingFast, 0);
		ReleaseSRWLockExclusive(&lock);

		DX12Log("Frame analysis directory: %S\n", modulePath);
		return true;
	}

	void End()
	{
		AcquireSRWLockExclusive(&lock);
		active = false;
		captureRequested = false;
		capturing = false;
		dumpPending = false;
		InterlockedExchange(&activeFast, 0);
		InterlockedExchange(&captureRequestedFast, 0);
		InterlockedExchange(&capturingFast, 0);
		CloseLogLocked();
		ReleaseSRWLockExclusive(&lock);
	}

	void RequestCapture()
	{
		AcquireSRWLockExclusive(&lock);
		if (active && !capturing && !dumpPending) {
			captureRequested = true;
			InterlockedExchange(&captureRequestedFast, 1);
		}
		ReleaseSRWLockExclusive(&lock);
	}

	void BeginCapture()
	{
		AcquireSRWLockExclusive(&lock);
		if (active && captureRequested && !capturing && !dumpPending) {
			captureRequested = false;
			capturing = true;
			InterlockedExchange(&captureRequestedFast, 0);
			InterlockedExchange(&capturingFast, 1);
		}
		ReleaseSRWLockExclusive(&lock);
	}

	bool EndCapture()
	{
		AcquireSRWLockExclusive(&lock);
		if (active && capturing) {
			capturing = false;
			dumpPending = true;
			InterlockedExchange(&capturingFast, 0);
			ReleaseSRWLockExclusive(&lock);
			return true;
		}
		ReleaseSRWLockExclusive(&lock);
		return false;
	}

	bool IsActive()
	{
		return InterlockedCompareExchange(&activeFast, 0, 0) != 0;
	}

	bool IsCaptureRequested()
	{
		return InterlockedCompareExchange(&captureRequestedFast, 0, 0) != 0;
	}

	bool IsCapturing()
	{
		return InterlockedCompareExchange(&capturingFast, 0, 0) != 0;
	}

	bool GetPath(wchar_t *outPath, size_t pathCount)
	{
		if (!outPath || pathCount == 0)
			return false;

		AcquireSRWLockShared(&lock);
		bool ok = path[0] != 0;
		if (ok)
			wcsncpy_s(outPath, pathCount, path, _TRUNCATE);
		ReleaseSRWLockShared(&lock);
		return ok;
	}

	bool OpenLogLocked()
	{
		wchar_t filename[MAX_PATH];

		if (log)
			return true;
		if (!active || !path[0])
			return false;

		swprintf_s(filename, L"%s\\log.jsonl", path);
		log = _wfsopen(filename, L"w", _SH_DENYNO);
		if (!log) {
			DX12Log("Error opening frame analysis log: %S\n", filename);
			return false;
		}

		fprintf(log,
			"{\"index\":0,\"func\":\"Header\",\"schema\":\"dx12-fa-log/3\",\"present_count_at_start\":%ld}\n",
			DX12GetPresentCount());
		return true;
	}

	void CloseLogLocked()
	{
		if (log) {
			fclose(log);
			log = nullptr;
		}
	}

	void LogJsonFields(const char *fields)
	{
		AcquireSRWLockExclusive(&lock);
		if (!active || !OpenLogLocked()) {
			ReleaseSRWLockExclusive(&lock);
			return;
		}

		fprintf(log, "{\"index\":%llu,%s}\n", ++lineNo,
			fields ? fields : "\"func\":\"Unknown\"");
		fflush(log);
		ReleaseSRWLockExclusive(&lock);
	}
};

class DX12FrameAnalysis
{
public:
	static bool Begin()
	{
		return DX12FrameAnalysisState::Get().Begin();
	}

	static void End()
	{
		DX12FrameAnalysisState::Get().End();
	}

	static void RequestCapture()
	{
		DX12FrameAnalysisState::Get().RequestCapture();
		DX12HotPathUpdate();
		DX12PrimeDescriptorHistoryForHunting();
	}

	static void BeginCapture()
	{
		DX12FrameAnalysisState::Get().BeginCapture();
		DX12HotPathUpdate();
		DX12PrimeDescriptorHistoryForHunting();
	}

	static bool EndCapture()
	{
		return DX12FrameAnalysisState::Get().EndCapture();
	}

	static bool IsActive()
	{
		return DX12FrameAnalysisState::Get().IsActive();
	}

	static bool IsCaptureRequested()
	{
		return DX12FrameAnalysisState::Get().IsCaptureRequested();
	}

	static bool IsCapturing()
	{
		return DX12FrameAnalysisState::Get().IsCapturing();
	}

	static bool GetPath(wchar_t *path, size_t pathCount)
	{
		return DX12FrameAnalysisState::Get().GetPath(path, pathCount);
	}

	static void LogEvent(const char *fmt, ...)
	{
		va_list args;
		char buffer[1024];
		char fields[4096];

		if (!IsActive())
			return;

		va_start(args, fmt);
		vsnprintf(buffer, sizeof(buffer), fmt, args);
		va_end(args);

		strcpy_s(fields, sizeof(fields), "\"func\":\"Event\"");
		DX12JsonUtils::AppendLogFieldsFromText(fields, sizeof(fields), buffer);
		LogJsonFields(fields);
	}

	static void LogInfo(const char *fmt, ...)
	{
		va_list args;
		char buffer[1024];
		char fields[4096];

		if (!IsActive())
			return;

		va_start(args, fmt);
		vsnprintf(buffer, sizeof(buffer), fmt, args);
		va_end(args);

		strcpy_s(fields, sizeof(fields), "\"func\":\"LogInfo\"");
		DX12JsonUtils::AppendLogFieldsFromText(fields, sizeof(fields), buffer);
		LogJsonFields(fields);
	}

	static void LogJsonFunc(const char *func, const char *fmt, ...)
	{
		if (!IsActive())
			return;

		char funcJson[512];
		char extra[4096];
		char fields[4608];
		DX12JsonUtils::EscapeString(funcJson, sizeof(funcJson), func ? func : "Unknown");
		extra[0] = '\0';

		if (fmt && fmt[0]) {
			va_list args;
			va_start(args, fmt);
			vsnprintf(extra, sizeof(extra), fmt, args);
			va_end(args);
		}

		if (extra[0])
			sprintf_s(fields, sizeof(fields), "\"func\":%s,%s", funcJson, extra);
		else
			sprintf_s(fields, sizeof(fields), "\"func\":%s", funcJson);
		LogJsonFields(fields);
	}

	static void LogJsonFields(const char *fields)
	{
		DX12FrameAnalysisState::Get().LogJsonFields(fields);
	}
};

