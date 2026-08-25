#pragma once

// In-process hang watchdog with self-minidump support.
// Split out of Core/DX12HotPath.h; include that umbrella header from existing code.
#include "../DX12CommandState.h"

#include <dbghelp.h>


// In-process hang watchdog with self-minidump support.
//
// The game is protected by anti-cheat, so external debuggers / Task Manager
// dumps cannot capture the frozen process. Writing the dump from inside the
// wrapper (MiniDumpWriteDump on self) needs no debug privilege and is the same
// mechanism in-process crash handlers use, so anti-cheat cannot block it.
//
// Triggers:
//  - No Present completion for more than ZZMIDX12_HANG_DUMP_MS (default 8000ms)
//    after the first present. This catches the startup/character-load hang
//    where the render thread stops presenting.
//  - No present at all for 30s after watchdog start (freeze before first frame).
//
// The dump is written next to d3d12.dll as hang_YYYYMMDD_HHMMSS.dmp with module
// data segments (covers the wrapper's SRW lock globals), thread info and stacks,
// so WinDbg can resolve which lock/wait the render thread is blocked on.
class DX12HangWatchdog
{
public:
	// Called right after the original Present/Present1 returns (both the
	// normal and the skipAll fast paths). Cheap: one GetTickCount64 + two
	// atomic stores.
	static void NotifyPresentCompleted()
	{
		const ULONGLONG now = GetTickCount64();
		InterlockedExchange64(
			&sLastPresentCompletionTick, static_cast<LONGLONG>(now));
		InterlockedExchange(&sHasSeenPresent, 1);
	}

	static void Start()
	{
		if (InterlockedCompareExchange(&sStarted, 1, 0) != 0)
			return;
		sInitTick = GetTickCount64();
		InterlockedExchange(&sStopRequested, 0);
		sThresholdMs = DefaultThresholdMs();
		sThread = CreateThread(nullptr, 0, ThreadProc, nullptr, 0, nullptr);
	}

	static void Stop()
	{
		InterlockedExchange(&sStopRequested, 1);
		if (sThread) {
			WaitForSingleObject(sThread, 5000);
			CloseHandle(sThread);
			sThread = nullptr;
		}
		InterlockedExchange(&sStarted, 0);
	}

private:
	static DWORD DefaultThresholdMs()
	{
		DWORD value = 8000;
		char env[32] = {};
		const DWORD len = GetEnvironmentVariableA(
			"ZZMIDX12_HANG_DUMP_MS", env, sizeof(env));
		if (len > 0 && len < sizeof(env)) {
			const int parsed = atoi(env);
			if (parsed > 0)
				value = static_cast<DWORD>(parsed);
		}
		return value;
	}

	static void WriteMinidump(const char *reason)
	{
		wchar_t directory[MAX_PATH] = {};
		const HMODULE module = DX12RuntimeState::GetModule();
		if (!module || !GetModuleFileNameW(module, directory, MAX_PATH))
			return;
		PathRemoveFileSpecW(directory);

		SYSTEMTIME st = {};
		GetLocalTime(&st);
		wchar_t path[MAX_PATH] = {};
		swprintf_s(path,
			L"%s\\hang_%04u%02u%02u_%02u%02u%02u.dmp",
			directory,
			st.wYear, st.wMonth, st.wDay,
			st.wHour, st.wMinute, st.wSecond);

		HANDLE file = CreateFileW(
			path, GENERIC_WRITE, FILE_SHARE_READ,
			nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE) {
			DX12LogManager::FieldsFlush("DX12HangWatchdog",
				"\"stage\":\"open_failed\",\"reason\":\"%s\",\"path\":\"%S\",\"error\":%lu",
				reason, path, GetLastError());
			return;
		}

		// DataSegs keeps every loaded module's data segment so the SRW lock
		// globals (gModLock/gResourceLock/...) are present in the dump;
		// thread stacks and thread info capture where each thread is blocked.
		const MINIDUMP_TYPE flags = static_cast<MINIDUMP_TYPE>(
			MiniDumpWithDataSegs |
			MiniDumpWithThreadInfo |
			MiniDumpWithProcessThreadData |
			MiniDumpWithHandleData |
			MiniDumpWithUnloadedModules);
		const BOOL ok = MiniDumpWriteDump(
			GetCurrentProcess(), GetCurrentProcessId(), file,
			flags, nullptr, nullptr, nullptr);
		const DWORD error = GetLastError();
		CloseHandle(file);

		if (ok) {
			DX12LogManager::FieldsFlush("DX12HangWatchdog",
				"\"stage\":\"written\",\"reason\":\"%s\",\"path\":\"%S\"",
				reason, path);
		} else {
			DX12LogManager::FieldsFlush("DX12HangWatchdog",
				"\"stage\":\"write_failed\",\"reason\":\"%s\",\"error\":%lu",
				reason, error);
		}
	}

	static void LogHangDetected(ULONGLONG elapsedMs)
	{
		DX12LogManager::FieldsFlush("DX12HangWatchdog",
			"\"stage\":\"detected\",\"elapsedMs\":%llu,\"presentSeen\":%ld",
			static_cast<unsigned long long>(elapsedMs), sHasSeenPresent);
	}

	static DWORD WINAPI ThreadProc(void *)
	{
		for (; InterlockedCompareExchange(&sStopRequested, 0, 0) == 0;) {
			Sleep(1000);
			const ULONGLONG now = GetTickCount64();
			if (!sHasSeenPresent) {
				// Freeze before the first present (early startup hang).
				if (now - sInitTick > 30000 &&
				    InterlockedCompareExchange(&sDumpAttempted, 1, 0) == 0) {
					LogHangDetected(now - sInitTick);
					WriteMinidump("no_present");
				}
				continue;
			}
			const LONGLONG last = InterlockedCompareExchange64(
				&sLastPresentCompletionTick, 0, 0);
			if (last == 0)
				continue;
			const ULONGLONG elapsed = now - static_cast<ULONGLONG>(last);
			if (elapsed > sThresholdMs &&
			    InterlockedCompareExchange(&sDumpAttempted, 1, 0) == 0) {
				LogHangDetected(elapsed);
				WriteMinidump("present_stall");
			}
		}
		return 0;
	}

	inline static volatile LONG sStarted = 0;
	inline static volatile LONG sStopRequested = 0;
	inline static volatile LONG sHasSeenPresent = 0;
	inline static volatile LONG sDumpAttempted = 0;
	inline static volatile LONGLONG sLastPresentCompletionTick = 0;
	inline static ULONGLONG sInitTick = 0;
	inline static DWORD sThresholdMs = 8000;
	inline static HANDLE sThread = nullptr;
};
