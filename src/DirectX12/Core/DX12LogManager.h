#pragma once

// Queued async log writer: log mode constants plus DX12LogManager.
// Split out of Core/DX12Diagnostics.h; include that umbrella header from existing code.
#include "DX12LogUtils.h"
#include "DX12RuntimeState.h"

// ---------------------------------------------------------------------------
// Log mode - fixed at compile time.
//
//   Debug         - full diagnostics: verbose hook/present logs + detailed
//                   timing tables.
//   Release       - startup and mod-load lifecycle logs only (allowlist in
//                   ReleaseShouldLogFunction); no per-frame diagnostics.
//   ReleaseTiming - detailed timing tables and diagnostics, but no verbose
//                   per-call/per-present spam (kVerboseEnabled is false).
//
// These constants let call sites gate with a single folded bool; Release and
// Timing builds compile the dead branches out entirely.
// ---------------------------------------------------------------------------
enum class DX12LogMode : unsigned char
{
	Debug = 0,
	Release,
	ReleaseTiming,
};

constexpr DX12LogMode gLogMode =
#if defined(DX12_RELEASE_TIMING)
	DX12LogMode::ReleaseTiming;
#else
	DX12LogMode::Release;
#endif

// Verbose per-call/per-present diagnostics: Debug only.
constexpr bool kVerboseEnabled = false;

// Detailed timing tables (ApiWall/HotPath/FrameTimeline): Debug + Timing.
constexpr bool kTimingEnabled =
#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
	true;
#else
	false;
#endif


class DX12LogManager
{
public:
	class DX12LogCostSnapshot
	{
	public:
		LONG64 fieldCalls = 0;
		LONG64 textCalls = 0;
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
	std::atomic<bool> releaseStartupLogging{ true };
	inline static volatile LONG64 LogCostFieldCalls = 0;
	inline static volatile LONG64 LogCostTextCalls = 0;
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
		return 8192;
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
		snapshot->fieldCalls = TakeLong64(&LogCostFieldCalls);
		snapshot->textCalls = TakeLong64(&LogCostTextCalls);
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
						"time=%s,func=DX12LogQueue,count=%ld\n", timePlain, dropped);
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
		state.releaseStartupLogging.store(true, std::memory_order_relaxed);
		wchar_t path[MAX_PATH];
		if (!GetModuleFileNameW(DX12RuntimeState::GetModule(), path, MAX_PATH))
			return false;

		PathRemoveFileSpecW(path);
		// Keep a timestamped copy of every run so normal vs broken sessions
		// can be compared: the log is overwritten on each launch otherwise.
		wchar_t logPath[MAX_PATH];
		wchar_t historyPath[MAX_PATH];
		wsprintfW(logPath, L"%s\\d3d12_log.txt", path);
		wsprintfW(historyPath, L"%s\\d3d12_log_%llu.txt", path,
			static_cast<unsigned long long>(GetTickCount64()));
		if (PathFileExistsW(logPath)) {
			CopyFileW(logPath, historyPath, FALSE);
		}
		PathAppendW(path, L"d3d12_log.txt");
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
		Get().releaseStartupLogging.store(false, std::memory_order_relaxed);
	}

	static bool ReleaseShouldLogFunction(const char *func)
	{
#if defined(DX12_RELEASE_TIMING)
		// Timing builds keep every Fields log (verbose per-call spam is gated
		// by kVerboseEnabled instead), so the timing tables are never dropped.
		(void)func;
		return true;
#else
		if (Get().releaseStartupLogging.load(std::memory_order_relaxed))
			return true;
		if (!func)
			return false;
		// Release keeps only startup and mod-load lifecycle logs.
		static const char *const kReleaseAllowlist[] = {
			"Log",                        // startup text messages
			"HookInstall",                // hook installation
			"DeviceHooksReady",           // device hooks finished
			"D3D12CreateDevice",          // device creation
			"CreateDXGIFactory",          // factory creation (1/2 included)
			"IDXGIFactory",               // factory/swapchain lifecycle
			"IDXGISwapChain",             // swapchain lifecycle
			"ID3D12CommandQueue",         // command queue lifecycle
			"DX12CommandQueueState",      // queue accept/ignore decisions
			"DX12ExecuteCommandListsFirst", // first command list submit
			"DX12PresentFirst",           // first present
			"DX12Config",                 // d3dx.ini configuration load
			"DX12ModRuntime",             // mod runtime load/reload/deferred
			"DX12ResourceLoad",           // mod resource loading
		};
		for (const char *name : kReleaseAllowlist) {
			if (!strncmp(func, name, strlen(name)))
				return true;
		}
		return false;
	#endif
	}

	static void EnqueueLogLine(const char *line)
	{
		if (!line || !line[0])
			return;

		const LARGE_INTEGER begin = LogNow();
		DX12LogManager &state = Get();
		InterlockedIncrement64(&LogCostEnqueueCalls);
		AcquireSRWLockExclusive(&state.queueLock);
		const bool wasEmpty = state.queue.empty();
		if (state.queue.size() < MaxQueuedLogLines()) {
			state.queue.emplace_back(line);
		} else {
			InterlockedIncrement(&state.droppedLines);
			InterlockedIncrement64(&LogCostDroppedLines);
		}
		ReleaseSRWLockExclusive(&state.queueLock);
		// The writer thread polls on a timeout, so signaling is only a
		// latency hint; waking it once per empty->non-empty transition
		// instead of once per line keeps present-time log bursts cheap.
		if (wasEmpty && state.event)
			SetEvent(state.event);
		AddLogCost(&LogCostEnqueueUs, LogDeltaUs(begin, LogNow()));
	}

	// Plain text fields: time=...,func=...,key=value,key2=value2.
	// Existing JSON-like field fragments are normalized without allocation.
	static void NormalizeFields(const char *input, char *output, size_t outputSize)
	{
		if (!output || outputSize == 0)
			return;
		output[0] = '\0';
		if (!input)
			return;

		bool quoted = false;
		bool escaped = false;
		size_t out = 0;
		for (const char *cursor = input; *cursor && out + 1 < outputSize; ++cursor) {
			char ch = *cursor;
			if (escaped) {
				output[out++] = ch;
				escaped = false;
				continue;
			}
			if (quoted && ch == '\\') {
				escaped = true;
				continue;
			}
			if (ch == '"') {
				quoted = !quoted;
				continue;
			}
			if (!quoted && (ch == '{' || ch == '}'))
				continue;
			if (!quoted && ch == ':')
				ch = '=';
			else if (quoted && ch == ',')
				ch = ';';
			else if (ch == '\r' || ch == '\n' || ch == '\t')
				ch = ' ';
			output[out++] = ch;
		}
		output[out] = '\0';
	}

	static void WriteLogLine(
		const char *func, const char *extra, bool flush)
	{
		if (!Get().log)
			return;

		const LARGE_INTEGER formatBegin = LogNow();
		char timePlain[32] = {};
		DX12LogUtils::MakeLogTimePlain(timePlain, sizeof(timePlain));
		char line[8192];
		char fields[6144];
		NormalizeFields(extra, fields, sizeof(fields));
		if (extra && extra[0]) {
			snprintf(line, sizeof(line), "time=%s,func=%s,%s\n", timePlain, func, fields);
		} else {
			snprintf(line, sizeof(line), "time=%s,func=%s\n", timePlain, func);
		}
		AddLogCost(&LogCostFormatUs, LogDeltaUs(formatBegin, LogNow()));
		EnqueueLogLine(line);
		if (flush)
			Flush();
	}

	// Batches formatted lines and enqueues them under one lock acquisition.
	// The per-present frame-timeline tables emit dozens of Fields lines back
	// to back on the render thread; routing them through a batch turns
	// dozens of exclusive-lock + SetEvent rounds into a single one while
	// keeping every emitted line byte-identical to WriteLogLine output.
	class LogBatch
	{
	public:
		~LogBatch() { Commit(); }

		void Append(const char *func, const char *extra)
		{
			const LARGE_INTEGER formatBegin = LogNow();
			char timePlain[32] = {};
			DX12LogUtils::MakeLogTimePlain(timePlain, sizeof(timePlain));
			char fields[6144];
			NormalizeFields(extra, fields, sizeof(fields));
			char line[8192];
			if (extra && extra[0]) {
				snprintf(line, sizeof(line), "time=%s,func=%s,%s\n", timePlain, func, fields);
			} else {
				snprintf(line, sizeof(line), "time=%s,func=%s\n", timePlain, func);
			}
			AddLogCost(&LogCostFormatUs, LogDeltaUs(formatBegin, LogNow()));
			mBuffer += line;
		}

		void Commit()
		{
			if (mBuffer.empty())
				return;
			EnqueueLogLine(mBuffer.c_str());
			mBuffer.clear();
		}

	private:
		std::string mBuffer;
	};

	static void Text(const char *fmt, ...)
	{
		if (!Get().log)
			return;
		if (!ReleaseShouldLogFunction("Log"))
			return;

		va_list args;
		char message[2048];
		InterlockedIncrement64(&LogCostTextCalls);
		const LARGE_INTEGER formatBegin = LogNow();
		va_start(args, fmt);
		vsnprintf(message, sizeof(message), fmt, args);
		va_end(args);
		AddLogCost(&LogCostFormatUs, LogDeltaUs(formatBegin, LogNow()));

		char fields[2304];
		snprintf(fields, sizeof(fields), "message=%s", message);
		WriteLogLine("Log", fields, false);
	}

	static void FieldsV(const char *func, const char *fmt, va_list args, bool flush)
	{
		if (!Get().log)
			return;
		if (!ReleaseShouldLogFunction(func))
			return;

		const char *safeFunc = func ? func : "Unknown";
		char extra[4096];
		extra[0] = '\0';
		InterlockedIncrement64(&LogCostFieldCalls);

		if (fmt && fmt[0]) {
			const LARGE_INTEGER formatBegin = LogNow();
			vsnprintf(extra, sizeof(extra), fmt, args);
			AddLogCost(&LogCostFormatUs, LogDeltaUs(formatBegin, LogNow()));
		}

		WriteLogLine(safeFunc, extra, flush);
	}

	static void Fields(const char *func, const char *fmt, ...)
	{
		va_list args;
		va_start(args, fmt);
		FieldsV(func, fmt, args, false);
		va_end(args);
	}

	static void FieldsFlush(const char *func, const char *fmt, ...)
	{
		va_list args;
		va_start(args, fmt);
		FieldsV(func, fmt, args, true);
		va_end(args);
	}

	static void Flush()
	{
		if (Get().event)
			SetEvent(Get().event);
	}
};
