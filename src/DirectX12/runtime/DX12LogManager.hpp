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

#include "utils/DX12LogUtils.hpp"
#include "DX12RuntimeState.hpp"

class DX12LogManager
{
public:
	class DX12LogCostSnapshot
	{
	public:
		LONG64 jsonCalls = 0;
		LONG64 textCalls = 0;
		LONG64 debugJsonCalls = 0;
		LONG64 enqueueCalls = 0;
		LONG64 formatUs = 0;
		LONG64 enqueueUs = 0;
		LONG64 droppedLines = 0;
	};

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
	inline static volatile LONG64 LogCostJsonCalls = 0;
	inline static volatile LONG64 LogCostTextCalls = 0;
	inline static volatile LONG64 LogCostDebugJsonCalls = 0;
	inline static volatile LONG64 LogCostEnqueueCalls = 0;
	inline static volatile LONG64 LogCostFormatUs = 0;
	inline static volatile LONG64 LogCostEnqueueUs = 0;
	inline static volatile LONG64 LogCostDroppedLines = 0;

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

	static LARGE_INTEGER LogFrequency()
	{
		static LARGE_INTEGER frequency = InitLogFrequency();
		return frequency;
	}

	static LARGE_INTEGER InitLogFrequency()
	{
		LARGE_INTEGER frequency = {};
		QueryPerformanceFrequency(&frequency);
		return frequency;
	}

	static LARGE_INTEGER LogNow()
	{
		LARGE_INTEGER value = {};
		QueryPerformanceCounter(&value);
		return value;
	}

	static LONG64 LogDeltaUs(const LARGE_INTEGER &begin, const LARGE_INTEGER &end)
	{
		const LARGE_INTEGER frequency = LogFrequency();
		if (frequency.QuadPart <= 0 || end.QuadPart <= begin.QuadPart)
			return 0;
		return static_cast<LONG64>(
			(end.QuadPart - begin.QuadPart) * 1000000 / frequency.QuadPart);
	}

	static void AddLogCost(volatile LONG64 *target, LONG64 us)
	{
		if (target && us > 0)
			InterlockedAdd64(target, us);
	}

	static LONG64 TakeLong64(volatile LONG64 *value)
	{
		return InterlockedExchange64(value, 0);
	}

	static void TakeLogCostSnapshot(DX12LogCostSnapshot *snapshot)
	{
		if (!snapshot)
			return;
		snapshot->jsonCalls = TakeLong64(&LogCostJsonCalls);
		snapshot->textCalls = TakeLong64(&LogCostTextCalls);
		snapshot->debugJsonCalls = TakeLong64(&LogCostDebugJsonCalls);
		snapshot->enqueueCalls = TakeLong64(&LogCostEnqueueCalls);
		snapshot->formatUs = TakeLong64(&LogCostFormatUs);
		snapshot->enqueueUs = TakeLong64(&LogCostEnqueueUs);
		snapshot->droppedLines = TakeLong64(&LogCostDroppedLines);
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
					char timePlain[32];
					DX12LogUtils::MakeLogTimePlain(timePlain, sizeof(timePlain));
					snprintf(droppedLine, sizeof(droppedLine),
						"%s\tDX12LogQueue\tcount=%ld\n", timePlain, dropped);
					fputs(droppedLine, state.log);
				}
				for (const std::string &line : pending)
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
		DX12LogUtils::Open(path); // DX11-style direct fprintf file
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
		if (!strcmp(func, "DX12FrameTimeline"))
			return true;
		if (!strcmp(func, "DX12ApiWallTiming"))
			return true;
		if (!strcmp(func, "DX12ResidentMaintenance"))
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

	static void EnqueueJsonLogLine(const char *line)
	{
		if (!line || !line[0])
			return;

		const LARGE_INTEGER begin = LogNow();
		DX12LogManager &state = Get();
		InterlockedIncrement64(&LogCostEnqueueCalls);
		AcquireSRWLockExclusive(&state.queueLock);
		if (state.queue.size() < MaxQueuedLogLines()) {
			state.queue.emplace_back(line);
		} else {
			InterlockedIncrement(&state.droppedLines);
			InterlockedIncrement64(&LogCostDroppedLines);
		}
		ReleaseSRWLockExclusive(&state.queueLock);
		if (state.event)
			SetEvent(state.event);
		AddLogCost(&LogCostEnqueueUs, LogDeltaUs(begin, LogNow()));
	}

	// ── TSV log format (DX11-aligned, no JSON overhead) ──────────────
	// Format: HH:MM:SS.mmm<TAB>FuncName<TAB>k1=v1<TAB>k2=v2<NEWLINE>
	// No JSON escaping — plain fprintf to async queue. Direct sprintf for
	// max speed, no intermediate field quoting or brace wrapping.

	static void WriteLogLine(
		const char *func, const char *extra, bool flush)
	{
		if (!Get().log)
			return;

		const LARGE_INTEGER formatBegin = LogNow();
		char timePlain[32] = {};
		DX12LogUtils::MakeLogTimePlain(timePlain, sizeof(timePlain));
		char line[8192];
		if (extra && extra[0]) {
			snprintf(line, sizeof(line), "%s\t%s\t%s\n", timePlain, func, extra);
		} else {
			snprintf(line, sizeof(line), "%s\t%s\n", timePlain, func);
		}
		AddLogCost(&LogCostFormatUs, LogDeltaUs(formatBegin, LogNow()));
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
		InterlockedIncrement64(&LogCostTextCalls);
		const LARGE_INTEGER formatBegin = LogNow();
		va_start(args, fmt);
		vsnprintf(message, sizeof(message), fmt, args);
		va_end(args);
		AddLogCost(&LogCostFormatUs, LogDeltaUs(formatBegin, LogNow()));

		WriteLogLine("Log", message, false);
	}

	static void JsonFuncV(const char *func, const char *fmt, va_list args, bool flush, bool debugCall)
	{
		if (!Get().log)
			return;
#if !defined(_DEBUG)
		if (!ReleaseShouldLogFunction(func))
			return;
#endif

		const char *safeFunc = func ? func : "Unknown";
		char extra[4096];
		extra[0] = '\0';
		if (debugCall)
			InterlockedIncrement64(&LogCostDebugJsonCalls);
		else
			InterlockedIncrement64(&LogCostJsonCalls);

		if (fmt && fmt[0]) {
			const LARGE_INTEGER formatBegin = LogNow();
			vsnprintf(extra, sizeof(extra), fmt, args);
			AddLogCost(&LogCostFormatUs, LogDeltaUs(formatBegin, LogNow()));
		}

		WriteLogLine(safeFunc, extra, flush);
	}

	static void JsonFunc(const char *func, const char *fmt, ...)
	{
		va_list args;
		va_start(args, fmt);
		JsonFuncV(func, fmt, args, false, false);
		va_end(args);
	}

	static void JsonFuncFlush(const char *func, const char *fmt, ...)
	{
		va_list args;
		va_start(args, fmt);
		JsonFuncV(func, fmt, args, true, false);
		va_end(args);
	}

#if defined(_DEBUG)
	static void DebugJsonFunc(const char *func, const char *fmt, ...)
	{
		if (!Get().log || !DiagnosticsLoggingEnabled())
			return;
		va_list args;
		va_start(args, fmt);
		JsonFuncV(func, fmt, args, false, true);
		va_end(args);
	}

	static void DebugJsonFuncFlush(const char *func, const char *fmt, ...)
	{
		if (!Get().log || !DiagnosticsLoggingEnabled())
			return;
		va_list args;
		va_start(args, fmt);
		JsonFuncV(func, fmt, args, true, true);
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
