#pragma once

#include <Windows.h>
#include <Shlwapi.h>
#include <share.h>
#include <stdio.h>
#include <time.h>

#include "DX12FrameAnalysisTypes.hpp"
#include "DX12LogManager.hpp"
#include "DX12RuntimeState.hpp"

class DX12FrameAnalysisState
{
public:
	inline static SRWLOCK lock = SRWLOCK_INIT;
	inline static bool active = false;
	inline static bool captureRequested = false;
	inline static bool capturing = false;
	inline static bool dumpPending = false;
	inline static UINT captureFramesRemaining = 0;
	inline static volatile LONG activeFast = 0;
	inline static volatile LONG captureRequestedFast = 0;
	inline static volatile LONG capturingFast = 0;
	inline static wchar_t path[MAX_PATH] = {};
	inline static FILE *log = nullptr;
	inline static unsigned long long lineNo = 0;

	static bool Begin()
	{
		wchar_t modulePath[MAX_PATH];
		wchar_t subdir[MAX_PATH];
		__time64_t now;
		struct tm localTime;

		if (!GetModuleFileNameW(DX12RuntimeState::GetModule(), modulePath, ARRAYSIZE(modulePath)))
			return false;
		PathRemoveFileSpecW(modulePath);

		_time64(&now);
		if (_localtime64_s(&localTime, &now) != 0)
			return false;
		wcsftime(subdir, ARRAYSIZE(subdir), L"FrameAnalysis-%Y-%m-%d-%H%M%S", &localTime);
		PathAppendW(modulePath, subdir);

		if (!CreateDirectoryW(modulePath, nullptr)) {
			DWORD error = GetLastError();
			DX12LogManager::Text(
				"Error creating frame analysis directory: %lu path=%S\n", error, modulePath);
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
		captureFramesRemaining = 0;
		InterlockedExchange(&activeFast, 1);
		InterlockedExchange(&captureRequestedFast, 0);
		InterlockedExchange(&capturingFast, 0);
		ReleaseSRWLockExclusive(&lock);

		DX12LogManager::Text("Frame analysis directory: %S\n", modulePath);
		return true;
	}

	static void End()
	{
		AcquireSRWLockExclusive(&lock);
		active = false;
		captureRequested = false;
		capturing = false;
		dumpPending = false;
		captureFramesRemaining = 0;
		InterlockedExchange(&activeFast, 0);
		InterlockedExchange(&captureRequestedFast, 0);
		InterlockedExchange(&capturingFast, 0);
		CloseLogLocked();
		ReleaseSRWLockExclusive(&lock);
	}

	static void RequestCapture()
	{
		AcquireSRWLockExclusive(&lock);
		if (active && !capturing && !dumpPending) {
			captureRequested = true;
			InterlockedExchange(&captureRequestedFast, 1);
		}
		ReleaseSRWLockExclusive(&lock);
	}

	static void BeginCapture()
	{
		AcquireSRWLockExclusive(&lock);
		if (active && captureRequested && !capturing && !dumpPending) {
			captureRequested = false;
			capturing = true;
			InterlockedExchange(&captureRequestedFast, 0);
			InterlockedExchange(&capturingFast, 1);
			captureFramesRemaining = 3;
		}
		ReleaseSRWLockExclusive(&lock);
	}

	static bool EndCapture()
	{
		AcquireSRWLockExclusive(&lock);
		if (active && capturing) {
			if (captureFramesRemaining > 1) {
				captureFramesRemaining--;
				ReleaseSRWLockExclusive(&lock);
				return false;
			}
			capturing = false;
			dumpPending = true;
			captureFramesRemaining = 0;
			InterlockedExchange(&capturingFast, 0);
			ReleaseSRWLockExclusive(&lock);
			return true;
		}
		ReleaseSRWLockExclusive(&lock);
		return false;
	}

	static bool IsActive()
	{
		return InterlockedCompareExchange(&activeFast, 0, 0) != 0;
	}

	static bool IsCaptureRequested()
	{
		return InterlockedCompareExchange(&captureRequestedFast, 0, 0) != 0;
	}

	static bool IsCapturing()
	{
		return InterlockedCompareExchange(&capturingFast, 0, 0) != 0;
	}

	static bool GetPath(wchar_t *outPath, size_t pathCount)
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

	static bool OpenLogLocked()
	{
		wchar_t filename[MAX_PATH];

		if (log)
			return true;
		if (!active || !path[0])
			return false;

		swprintf_s(filename, L"%s\\log.jsonl", path);
		log = _wfsopen(filename, L"w", _SH_DENYNO);
		if (!log) {
			DX12LogManager::Text("Error opening frame analysis log: %S\n", filename);
			return false;
		}

		fprintf(log,
			"{\"index\":0,\"func\":\"Header\",\"schema\":\"dx12-fa-log/3\",\"present_count_at_start\":%ld}\n",
			DX12RuntimeState::GetPresentCount());
		return true;
	}

	static void CloseLogLocked()
	{
		if (log) {
			fclose(log);
			log = nullptr;
		}
	}

	static void LogJsonFields(const char *fields)
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
