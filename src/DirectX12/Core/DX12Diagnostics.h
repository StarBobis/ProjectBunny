#pragma once

#include "DX12CoreModels.h"

// Logging, runtime state, JSON diagnostics, and frame-flow telemetry.

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


enum class DX12RuntimeMode : LONG
{
	Mod = 0,
	Hunting = 1,
};

class DX12RuntimeState
{
public:
	inline static HINSTANCE module = nullptr;
	inline static std::atomic<LONG> presentCount{ 0 };
	inline static std::atomic<LONG> runtimeMode{ static_cast<LONG>(DX12RuntimeMode::Mod) };

	// Per-frame draw/dispatch counters feed diagnostics logs only. Sharded by
	// recording thread so the per-draw increment cannot ping-pong one cache
	// line across cores when the game records command lists from workers.
	struct alignas(64) FrameCounterShard
	{
		std::atomic<LONG> draws{ 0 };
		std::atomic<LONG> dispatches{ 0 };
	};
	static constexpr UINT FrameCounterShardCount = 64;
	inline static FrameCounterShard frameCounterShards[FrameCounterShardCount] = {};
	inline static std::atomic<UINT> frameCounterShardNext{ 0 };

	static void SetModule(HINSTANCE module)
	{
		DX12RuntimeState::module = module;
	}

	static HINSTANCE GetModule()
	{
		return module;
	}

	static LONG IncrementPresentCount()
	{
		return presentCount.fetch_add(1, std::memory_order_relaxed) + 1;
	}

	static LONG GetPresentCount()
	{
		return presentCount.load(std::memory_order_relaxed);
	}

	static DX12RuntimeMode GetRuntimeMode()
	{
		const LONG mode = runtimeMode.load(std::memory_order_relaxed);
		return mode == static_cast<LONG>(DX12RuntimeMode::Hunting) ?
			DX12RuntimeMode::Hunting : DX12RuntimeMode::Mod;
	}

	static void SetRuntimeMode(DX12RuntimeMode mode);

	static bool IsModMode()
	{
		return GetRuntimeMode() == DX12RuntimeMode::Mod;
	}

	static bool IsHuntingMode()
	{
		return GetRuntimeMode() == DX12RuntimeMode::Hunting;
	}

	static FrameCounterShard *FrameCounterShardForThread()
	{
		static thread_local FrameCounterShard *shard = nullptr;
		if (!shard) {
			const UINT index =
				frameCounterShardNext.fetch_add(1, std::memory_order_relaxed) %
				FrameCounterShardCount;
			shard = &frameCounterShards[index];
		}
		return shard;
	}

	static void IncrementFrameDraws()
	{
		FrameCounterShardForThread()->draws.fetch_add(1, std::memory_order_relaxed);
	}

	static void IncrementFrameDispatches()
	{
		FrameCounterShardForThread()->dispatches.fetch_add(1, std::memory_order_relaxed);
	}

	static LONG TakeFrameDraws()
	{
		LONG total = 0;
		for (UINT i = 0; i < FrameCounterShardCount; ++i)
			total += frameCounterShards[i].draws.exchange(0, std::memory_order_relaxed);
		return total;
	}

	static LONG TakeFrameDispatches()
	{
		LONG total = 0;
		for (UINT i = 0; i < FrameCounterShardCount; ++i)
			total += frameCounterShards[i].dispatches.exchange(0, std::memory_order_relaxed);
		return total;
	}
};

class DX12InternalReplayState
{
public:
	static UINT &Depth()
	{
		static thread_local UINT depth = 0;
		return depth;
	}

	static void Enter()
	{
		++Depth();
	}

	static void Leave()
	{
		UINT &depth = Depth();
		if (depth)
			--depth;
	}

	static bool IsActive()
	{
		return Depth() != 0;
	}
};

class DX12InternalReplayScope
{
public:
	DX12InternalReplayScope() { DX12InternalReplayState::Enter(); }
	~DX12InternalReplayScope() { DX12InternalReplayState::Leave(); }
	DX12InternalReplayScope(const DX12InternalReplayScope&) = delete;
	DX12InternalReplayScope &operator=(const DX12InternalReplayScope&) = delete;
};


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


class DX12JsonUtils
{
public:
	static void AppendText(char *dst, size_t dstSize, const char *text)
	{
		if (!dst || dstSize == 0 || !text)
			return;
		const size_t len = strlen(dst);
		if (len >= dstSize - 1)
			return;
		sprintf_s(dst + len, dstSize - len, "%s", text);
	}

	static void AppendStringField(char *dst, size_t dstSize, const char *name, const char *value)
	{
		char escaped[2048];
		EscapeString(escaped, sizeof(escaped), value ? value : "");
		AppendText(dst, dstSize, ",\"");
		AppendText(dst, dstSize, name);
		AppendText(dst, dstSize, "\":");
		AppendText(dst, dstSize, escaped);
	}

	static void AppendWStringField(
		char *dst, size_t dstSize, const char *name, const wchar_t *value)
	{
		char escaped[2048];
		EscapeWString(escaped, sizeof(escaped), value ? value : L"");
		AppendText(dst, dstSize, ",\"");
		AppendText(dst, dstSize, name);
		AppendText(dst, dstSize, "\":");
		AppendText(dst, dstSize, escaped);
	}

	static void AppendRawField(char *dst, size_t dstSize, const char *name, const char *value)
	{
		AppendText(dst, dstSize, ",\"");
		AppendText(dst, dstSize, name);
		AppendText(dst, dstSize, "\":");
		AppendText(dst, dstSize, value ? value : "0");
	}

	static bool IsIntegerText(const char *value)
	{
		if (!value || !*value)
			return false;
		if (*value == '-')
			value++;
		if (!*value)
			return false;
		for (; *value; ++value) {
			if (!isdigit(static_cast<unsigned char>(*value)))
				return false;
		}
		return true;
	}


	static bool IsIdentifierKeyChar(char c)
	{
		return isalnum(static_cast<unsigned char>(c)) || c == '_';
	}

	static bool IsSkippablePunctuation(char c)
	{
		return c == ':' || c == ',' || c == ';';
	}

	static void EscapeString(char *dst, size_t dstSize, const char *src)
	{
		if (!dst || dstSize == 0)
			return;

		size_t pos = 0;
		if (pos + 1 < dstSize)
			dst[pos++] = '"';
		if (src) {
			for (; *src && pos + 6 < dstSize; ++src) {
				switch (*src) {
				case '"': dst[pos++] = '\\'; dst[pos++] = '"'; break;
				case '\\': dst[pos++] = '\\'; dst[pos++] = '\\'; break;
				case '\b': dst[pos++] = '\\'; dst[pos++] = 'b'; break;
				case '\f': dst[pos++] = '\\'; dst[pos++] = 'f'; break;
				case '\n': dst[pos++] = '\\'; dst[pos++] = 'n'; break;
				case '\r': dst[pos++] = '\\'; dst[pos++] = 'r'; break;
				case '\t': dst[pos++] = '\\'; dst[pos++] = 't'; break;
				default:
					if (static_cast<unsigned char>(*src) < 0x20) {
						sprintf_s(dst + pos, dstSize - pos, "\\u%04x",
							static_cast<unsigned char>(*src));
						pos += 6;
					} else {
						dst[pos++] = *src;
					}
				}
			}
		}
		if (pos + 1 < dstSize)
			dst[pos++] = '"';
		if (pos < dstSize)
			dst[pos] = '\0';
	}

	static void EscapeWString(char *dst, size_t dstSize, const wchar_t *src)
	{
		if (!dst || dstSize == 0)
			return;

		size_t pos = 0;
		if (pos + 1 < dstSize)
			dst[pos++] = '"';
		if (src) {
			for (; *src && pos + 6 < dstSize; ++src) {
				wchar_t c = *src;
				if (c < 0x20) {
					sprintf_s(dst + pos, dstSize - pos, "\\u%04x",
						static_cast<unsigned int>(c));
					pos += 6;
				} else if (c == L'"') {
					dst[pos++] = '\\';
					dst[pos++] = '"';
				} else if (c == L'\\') {
					dst[pos++] = '\\';
					dst[pos++] = '\\';
				} else if (c < 0x80) {
					dst[pos++] = static_cast<char>(c);
				} else if (c < 0x800 && pos + 2 < dstSize) {
					dst[pos++] = static_cast<char>(0xC0 | (c >> 6));
					dst[pos++] = static_cast<char>(0x80 | (c & 0x3F));
				} else if (pos + 3 < dstSize) {
					dst[pos++] = static_cast<char>(0xE0 | (c >> 12));
					dst[pos++] = static_cast<char>(0x80 | ((c >> 6) & 0x3F));
					dst[pos++] = static_cast<char>(0x80 | (c & 0x3F));
				}
			}
		}
		if (pos + 1 < dstSize)
			dst[pos++] = '"';
		if (pos < dstSize)
			dst[pos] = '\0';
	}

	static void AppendLogFieldsFromText(char *dst, size_t dstSize, const char *text)
	{
		if (!dst || dstSize == 0)
			return;

		if (!text)
			text = "";

		char clean[2048];
		strncpy_s(clean, text, _TRUNCATE);
		size_t cleanLen = strlen(clean);
		while (cleanLen > 0 && (clean[cleanLen - 1] == '\n' || clean[cleanLen - 1] == '\r')) {
			clean[--cleanLen] = '\0';
		}

		const char *cursor = clean;
		const char *firstKey = nullptr;
		for (const char *scan = clean; *scan; ++scan) {
			if ((scan == clean || !IsIdentifierKeyChar(*(scan - 1))) && IsIdentifierKeyChar(*scan)) {
				const char *keyEnd = scan;
				while (IsIdentifierKeyChar(*keyEnd))
					keyEnd++;
				if (*keyEnd == '=') {
					firstKey = scan;
					break;
				}
				scan = keyEnd;
				if (!*scan)
					break;
			}
		}
		if (firstKey)
			cursor = firstKey;
		else
			cursor += strlen(cursor);

		char prefix[512] = "";
		if (cursor > clean) {
			size_t prefixLen = static_cast<size_t>(cursor - clean);
			if (prefixLen >= sizeof(prefix))
				prefixLen = sizeof(prefix) - 1;
			memcpy(prefix, clean, prefixLen);
			prefix[prefixLen] = '\0';
			while (prefixLen > 0 && isspace(static_cast<unsigned char>(prefix[prefixLen - 1])))
				prefix[--prefixLen] = '\0';
		}

		bool wroteKeyValue = false;
		bool wroteText = false;
		if (prefix[0]) {
			AppendStringField(dst, dstSize, "text", prefix);
			wroteText = true;
		}

		while (*cursor) {
			while (*cursor && (isspace(static_cast<unsigned char>(*cursor)) ||
				IsSkippablePunctuation(*cursor))) {
				cursor++;
			}
			if (!*cursor)
				break;

			const char *keyStart = cursor;
			while (IsIdentifierKeyChar(*cursor))
				cursor++;
			if (cursor == keyStart || *cursor != '=')
				break;

			char key[64];
			size_t keyLen = static_cast<size_t>(cursor - keyStart);
			if (keyLen >= sizeof(key))
				keyLen = sizeof(key) - 1;
			strncpy_s(key, keyLen + 1, keyStart, keyLen);
			cursor++;

			const char *valueStart = cursor;
			while (*cursor && !isspace(static_cast<unsigned char>(*cursor)))
				cursor++;

			char value[512];
			size_t valueLen = static_cast<size_t>(cursor - valueStart);
			if (valueLen >= sizeof(value))
				valueLen = sizeof(value) - 1;
			strncpy_s(value, valueLen + 1, valueStart, valueLen);

			while (valueLen > 0 && (value[valueLen - 1] == ',' || value[valueLen - 1] == ';')) {
				value[--valueLen] = '\0';
			}

			if (!strcmp(value, "true") || !strcmp(value, "false") || IsIntegerText(value))
				AppendRawField(dst, dstSize, key, value);
			else
				AppendStringField(dst, dstSize, key, value);
			wroteKeyValue = true;
		}

		if (!wroteKeyValue && !wroteText)
			AppendStringField(dst, dstSize, "text", clean);
	}
};
