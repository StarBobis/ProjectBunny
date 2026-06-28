#pragma once

#include <Windows.h>
#include <Shlwapi.h>

#include <atomic>
#include <deque>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "DX12Json.hpp"
#include "DX12RuntimeState.hpp"

class DX12LogManager
{
public:
	FILE *log = nullptr;
	HANDLE thread = nullptr;
	HANDLE event = nullptr;
	SRWLOCK queueLock;
	std::deque<std::string> queue;
	volatile LONG droppedLines = 0;
	volatile LONG stopRequested = 0;
#if !defined(_DEBUG)
	std::atomic<bool> releaseStartupLogging{ true };
#endif

	DX12LogManager()
	{
		InitializeSRWLock(&queueLock);
	}

	static DX12LogManager &Get()
	{
		static DX12LogManager state;
		return state;
	}

	static size_t MaxQueuedLogLines()
	{
#if defined(_DEBUG)
		return 262144;
#else
		return 8192;
#endif
	}

	static DWORD WINAPI ThreadProc(void*)
	{
		DX12LogManager &state = Get();
		for (;;) {
			if (state.event)
				WaitForSingleObject(state.event, 100);
			if (state.event)
				ResetEvent(state.event);

			std::vector<std::string> pending;
			AcquireSRWLockExclusive(&state.queueLock);
			while (!state.queue.empty()) {
				pending.push_back(std::move(state.queue.front()));
				state.queue.pop_front();
			}
			const bool stopping = InterlockedCompareExchange(&state.stopRequested, 0, 0) != 0;
			ReleaseSRWLockExclusive(&state.queueLock);

			if (state.log) {
				const LONG dropped = InterlockedExchange(&state.droppedLines, 0);
				if (dropped > 0) {
					char droppedLine[256];
					snprintf(droppedLine, sizeof(droppedLine),
						"{\"func\":\"DX12LogQueue\",\"event\":\"DroppedLines\",\"count\":%ld}\n",
						dropped);
					fputs(droppedLine, state.log);
				}
				for (const auto &line : pending)
					fputs(line.c_str(), state.log);
				if (!pending.empty() || stopping)
					fflush(state.log);
			}

			if (stopping && pending.empty())
				break;
		}
		return 0;
	}

	static bool Open()
	{
		DX12LogManager &state = Get();
#if !defined(_DEBUG)
		state.releaseStartupLogging.store(true, std::memory_order_relaxed);
#endif
		wchar_t path[MAX_PATH];
		if (!GetModuleFileNameW(DX12RuntimeState::GetModule(), path, MAX_PATH))
			return false;

		PathRemoveFileSpecW(path);
		PathAppendW(path, L"d3d12_log.jsonl");
		state.log = _wfsopen(path, L"w", _SH_DENYNO);
		if (!state.log)
			return false;

		InterlockedExchange(&state.stopRequested, 0);
		state.event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		if (!state.event) {
			fclose(state.log);
			state.log = nullptr;
			return false;
		}
		state.thread = CreateThread(nullptr, 0, ThreadProc, nullptr, 0, nullptr);
		if (!state.thread) {
			CloseHandle(state.event);
			state.event = nullptr;
			fclose(state.log);
			state.log = nullptr;
			return false;
		}
		return true;
	}

	static void Close()
	{
		DX12LogManager &state = Get();
		InterlockedExchange(&state.stopRequested, 1);
		if (state.event)
			SetEvent(state.event);
		if (state.thread) {
			WaitForSingleObject(state.thread, 5000);
			CloseHandle(state.thread);
			state.thread = nullptr;
		}
		if (state.event) {
			CloseHandle(state.event);
			state.event = nullptr;
		}
		if (state.log) {
			fclose(state.log);
			state.log = nullptr;
		}
		AcquireSRWLockExclusive(&state.queueLock);
		state.queue.clear();
		ReleaseSRWLockExclusive(&state.queueLock);
	}

	static void EndReleaseStartupLogging()
	{
#if !defined(_DEBUG)
		Get().releaseStartupLogging.store(false, std::memory_order_relaxed);
#endif
	}

	static bool DiagnosticsLoggingEnabled()
	{
#if defined(_DEBUG)
		return true;
#else
		return false;
#endif
	}

	static bool HookCallLoggingEnabled()
	{
#if defined(_DEBUG)
		static LONG sEnabled = -1;
		LONG enabled = InterlockedCompareExchange(&sEnabled, 0, -1);
		if (enabled == -1) {
			char value[16] = {};
			const DWORD len = GetEnvironmentVariableA(
				"ZZMIDX12_VERBOSE_DIAGNOSTICS", value, ARRAYSIZE(value));
			const bool requested = len > 0 &&
				(!_stricmp(value, "1") || !_stricmp(value, "true") || !_stricmp(value, "yes"));
			enabled = requested ? 1 : 0;
			InterlockedExchange(&sEnabled, enabled);
		}
		return enabled != 0;
#else
		return false;
#endif
	}

	static bool VerboseDiagnosticsEnabled()
	{
		return HookCallLoggingEnabled();
	}

	static bool ShouldLogHookCall(const char *api)
	{
#if defined(_DEBUG)
		if (!api || !api[0] || DX12InternalReplayState::IsActive() || !HookCallLoggingEnabled())
			return false;
		return true;
#else
		(void)api;
		return false;
#endif
	}

	static bool ReleaseShouldLogFunction(const char *func)
	{
#if !defined(_DEBUG)
		if (Get().releaseStartupLogging.load(std::memory_order_relaxed))
			return true;
		if (!func)
			return false;
		if (!strcmp(func, "Log"))
			return true;
		if (!strcmp(func, "HookInstall"))
			return true;
		if (!strcmp(func, "DeviceHooksReady"))
			return true;
		if (!strcmp(func, "D3D12CreateDevice"))
			return true;
		if (!strncmp(func, "CreateDXGIFactory", 17))
			return true;
		if (!strncmp(func, "IDXGIFactory", 12))
			return true;
		if (!strncmp(func, "IDXGISwapChain", 14))
			return true;
		if (!strncmp(func, "ID3D12CommandQueue", 18))
			return true;
		if (!strcmp(func, "DX12CommandQueueState"))
			return true;
		if (!strcmp(func, "DX12ExecuteCommandListsFirst"))
			return true;
		if (!strcmp(func, "DX12PresentFirst"))
			return true;
		if (!strcmp(func, "DX12HotPathHookGate"))
			return true;
		if (!strcmp(func, "DX12HotPathState"))
			return true;
		if (!strcmp(func, "InputHotkey"))
			return true;
		if (!strcmp(func, "DX12InputThread"))
			return true;
		if (!strcmp(func, "DX12HuntSelect"))
			return true;
		if (!strcmp(func, "DX12HuntIaStats"))
			return true;
		if (!strcmp(func, "DX12HuntToggle"))
			return true;
		if (!strncmp(func, "OverlayWindow", 13))
			return true;
		return false;
#else
		(void)func;
		return true;
#endif
	}

	static void MakeLogTime(char *buffer, size_t bufferSize)
	{
		if (!buffer || !bufferSize)
			return;
		SYSTEMTIME localTime;
		GetLocalTime(&localTime);
		snprintf(buffer, bufferSize,
			"\"%02u:%02u:%02u.%03u\"",
			localTime.wHour, localTime.wMinute, localTime.wSecond,
			localTime.wMilliseconds);
	}

	static void EnqueueJsonLogLine(const char *line)
	{
		if (!line || !line[0])
			return;

		DX12LogManager &state = Get();
		AcquireSRWLockExclusive(&state.queueLock);
		if (state.queue.size() < MaxQueuedLogLines()) {
			state.queue.emplace_back(line);
		} else {
			InterlockedIncrement(&state.droppedLines);
		}
		ReleaseSRWLockExclusive(&state.queueLock);
		if (state.event)
			SetEvent(state.event);
	}

	static void WriteJsonLogLine(
		const char *func, const char *funcJson, const char *extra, bool flush)
	{
		if (!Get().log)
			return;

		char timeJson[64] = {};
		MakeLogTime(timeJson, sizeof(timeJson));
		const bool isHookCall = func && strcmp(func, "DX12HookCall") == 0;
		char line[8192];
		if (extra && extra[0]) {
			if (isHookCall) {
				snprintf(line, sizeof(line), "{%s,\"time\":%s}\n", extra, timeJson);
			} else {
				snprintf(line, sizeof(line), "{\"func\":%s,\"time\":%s,%s}\n",
					funcJson, timeJson, extra);
			}
		} else {
			if (isHookCall) {
				snprintf(line, sizeof(line), "{\"api\":\"\",\"time\":%s}\n", timeJson);
			} else {
				snprintf(line, sizeof(line), "{\"func\":%s,\"time\":%s}\n", funcJson, timeJson);
			}
		}
		EnqueueJsonLogLine(line);
		if (flush)
			Flush();
	}

	static void Text(const char *fmt, ...)
	{
		if (!Get().log)
			return;
#if !defined(_DEBUG)
		if (!ReleaseShouldLogFunction("Log"))
			return;
#endif

		va_list args;
		char message[2048];
		char fields[4096];

		va_start(args, fmt);
		vsnprintf(message, sizeof(message), fmt, args);
		va_end(args);

		fields[0] = '\0';
		DX12JsonUtils::AppendLogFieldsFromText(fields, sizeof(fields), message);
		char funcJson[16];
		DX12JsonUtils::EscapeString(funcJson, sizeof(funcJson), "Log");
		WriteJsonLogLine("Log", funcJson, fields[0] == ',' ? fields + 1 : fields, false);
	}

	static void JsonFuncV(const char *func, const char *fmt, va_list args, bool flush)
	{
		if (!Get().log)
			return;
#if !defined(_DEBUG)
		if (!ReleaseShouldLogFunction(func))
			return;
#endif

		char funcJson[512];
		char extra[4096];
		DX12JsonUtils::EscapeString(funcJson, sizeof(funcJson), func ? func : "Unknown");
		extra[0] = '\0';

		if (fmt && fmt[0])
			vsnprintf(extra, sizeof(extra), fmt, args);

		WriteJsonLogLine(func ? func : "Unknown", funcJson, extra, flush);
	}

	static void JsonFunc(const char *func, const char *fmt, ...)
	{
		va_list args;
		va_start(args, fmt);
		JsonFuncV(func, fmt, args, false);
		va_end(args);
	}

	static void JsonFuncFlush(const char *func, const char *fmt, ...)
	{
		va_list args;
		va_start(args, fmt);
		JsonFuncV(func, fmt, args, true);
		va_end(args);
	}

#if defined(_DEBUG)
	static void DebugJsonFunc(const char *func, const char *fmt, ...)
	{
		if (!Get().log || !DiagnosticsLoggingEnabled())
			return;
		va_list args;
		va_start(args, fmt);
		JsonFuncV(func, fmt, args, false);
		va_end(args);
	}

	static void DebugJsonFuncFlush(const char *func, const char *fmt, ...)
	{
		if (!Get().log || !DiagnosticsLoggingEnabled())
			return;
		va_list args;
		va_start(args, fmt);
		JsonFuncV(func, fmt, args, true);
		va_end(args);
	}
#else
	static void DebugJsonFunc(const char *func, const char *fmt, ...)
	{
		(void)func;
		(void)fmt;
	}

	static void DebugJsonFuncFlush(const char *func, const char *fmt, ...)
	{
		(void)func;
		(void)fmt;
	}
#endif

	static void Flush()
	{
		if (Get().event)
			SetEvent(Get().event);
	}
};
