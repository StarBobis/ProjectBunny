#pragma once

#include "Core/DX12CoreState.hpp"

// DX11-aligned global log utilities
//
// DX12LogInfo and DX12LogDebug use direct fprintf without timestamps.
// Timestamped variants prefix the line with GetTickCount64.
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

// No-timestamp macros pure fprintf, DX11 speed
// Prefixed DX12 to avoid collision with DX12FrameAnalysis::LogInfo etc.

#if defined(_DEBUG)
#define DX12LogDebug(fmt, ...) \
	do { if (DX12LogUtils::LogFile) fprintf(DX12LogUtils::LogFile, fmt, __VA_ARGS__); } while (0)
#else
#define DX12LogDebug(fmt, ...) do {} while (0)
#endif

#define DX12LogInfo(fmt, ...) \
	do { if (DX12LogUtils::LogFile) fprintf(DX12LogUtils::LogFile, fmt, __VA_ARGS__); } while (0)

// Timestamped macros GetTickCount64() prefix + direct fprintf
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


enum class DX12RuntimeMode : LONG
{
	Mod = 0,
	Hunting = 1,
};

class DX12RuntimeState
{
public:
	inline static HINSTANCE module = nullptr;
	inline static volatile LONG presentCount = 0;
	inline static volatile LONG runtimeMode = static_cast<LONG>(DX12RuntimeMode::Mod);
	inline static volatile LONG perFrameTotalDraws = 0;
	inline static volatile LONG perFrameTotalDispatches = 0;

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
		return InterlockedIncrement(&presentCount);
	}

	static LONG GetPresentCount()
	{
		return InterlockedCompareExchange(&presentCount, 0, 0);
	}

	static DX12RuntimeMode GetRuntimeMode()
	{
		const LONG mode = InterlockedCompareExchange(&runtimeMode, 0, 0);
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

	static void IncrementFrameDraws()
	{
		InterlockedIncrement(&perFrameTotalDraws);
	}

	static void IncrementFrameDispatches()
	{
		InterlockedIncrement(&perFrameTotalDispatches);
	}

	static LONG TakeFrameDraws()
	{
		return InterlockedExchange(&perFrameTotalDraws, 0);
	}

	static LONG TakeFrameDispatches()
	{
		return InterlockedExchange(&perFrameTotalDispatches, 0);
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




class DX12LogManager
{
public:
	class DX12LogCostSnapshot
	{
	public:
		LONG64 fieldCalls = 0;
		LONG64 textCalls = 0;
		LONG64 debugFieldCalls = 0;
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
	inline static volatile LONG64 LogCostFieldCalls = 0;
	inline static volatile LONG64 LogCostTextCalls = 0;
	inline static volatile LONG64 LogCostDebugFieldCalls = 0;
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
		snapshot->fieldCalls = TakeLong64(&LogCostFieldCalls);
		snapshot->textCalls = TakeLong64(&LogCostTextCalls);
		snapshot->debugFieldCalls = TakeLong64(&LogCostDebugFieldCalls);
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
#if !defined(_DEBUG)
		state.releaseStartupLogging.store(true, std::memory_order_relaxed);
#endif
		wchar_t path[MAX_PATH];
		if (!GetModuleFileNameW(DX12RuntimeState::GetModule(), path, MAX_PATH))
			return false;

		PathRemoveFileSpecW(path);
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
#if defined(DX12_RELEASE_TIMING)
		// Timing builds exist to produce the full stage breakdown; the Release
		// allowlist would silently drop DX12HotPathTiming/ApiWall/queue slices.
		(void)func;
		return true;
#elif !defined(_DEBUG)
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

		char fields[2304];
		snprintf(fields, sizeof(fields), "message=%s", message);
		WriteLogLine("Log", fields, false);
	}

	static void FieldsV(const char *func, const char *fmt, va_list args, bool flush, bool debugCall)
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
			InterlockedIncrement64(&LogCostDebugFieldCalls);
		else
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
		FieldsV(func, fmt, args, false, false);
		va_end(args);
	}

	static void FieldsFlush(const char *func, const char *fmt, ...)
	{
		va_list args;
		va_start(args, fmt);
		FieldsV(func, fmt, args, true, false);
		va_end(args);
	}

#if defined(_DEBUG)
	static void DebugFields(const char *func, const char *fmt, ...)
	{
		if (!Get().log || !DiagnosticsLoggingEnabled())
			return;
		va_list args;
		va_start(args, fmt);
		FieldsV(func, fmt, args, false, true);
		va_end(args);
	}

	static void DebugFieldsFlush(const char *func, const char *fmt, ...)
	{
		if (!Get().log || !DiagnosticsLoggingEnabled())
			return;
		va_list args;
		va_start(args, fmt);
		FieldsV(func, fmt, args, true, true);
		va_end(args);
	}
#else
	static void DebugFields(const char *func, const char *fmt, ...)
	{
		(void)func;
		(void)fmt;
	}

	static void DebugFieldsFlush(const char *func, const char *fmt, ...)
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

	static bool IsHexText(const char *value)
	{
		if (!value || value[0] != '0' || (value[1] != 'x' && value[1] != 'X'))
			return false;
		value += 2;
		if (!*value)
			return false;
		for (; *value; ++value) {
			if (!isxdigit(static_cast<unsigned char>(*value)))
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



class DX12CommandListFlowDiagnostics
{
public:
#if defined(_DEBUG)
	static void LogDX12Call(const char *api, const void *object, const char *fmt = nullptr, ...)
	{
		if (!DX12LogManager::ShouldLogHookCall(api))
			return;

		char detail[1024];
		detail[0] = '\0';
		if (fmt && fmt[0]) {
			va_list args;
			va_start(args, fmt);
			vsnprintf(detail, sizeof(detail), fmt, args);
			va_end(args);
		}

		char apiJson[256];
		char detailJson[2048];
		DX12JsonUtils::EscapeString(apiJson, sizeof(apiJson), api ? api : "");
		DX12JsonUtils::EscapeString(detailJson, sizeof(detailJson), detail);
		DX12LogManager::DebugFields("DX12HookCall",
			"\"api\":%s,\"present\":%ld,\"this\":\"%p\",\"detail\":%s",
			apiJson, DX12RuntimeState::GetPresentCount(), object, detailJson);
	}
#else
	static __forceinline void LogDX12Call(const char *, const void *, const char * = nullptr, ...) {}
#endif

	static void LogQueueStage(
		const char *api, const char *stage, ID3D12CommandQueue *queue,
		UINT count = 0, ID3D12Fence *fence = nullptr, UINT64 value = 0, HRESULT hr = S_OK,
		ULONGLONG elapsedMs = 0)
	{
#if defined(_DEBUG)
		if (!DX12LogManager::VerboseDiagnosticsEnabled())
			return;
		if (SUCCEEDED(hr) && elapsedMs < 4 && stage && !strcmp(stage, "beforeOriginal"))
			return;
		if (SUCCEEDED(hr) && elapsedMs < 4 && api)
			return;
		DX12LogManager::DebugFields("DX12QueueStage",
			"\"api\":\"%s\",\"stage\":\"%s\",\"present\":%ld,\"queue\":\"%p\","
			"\"count\":%u,\"fence\":\"%p\",\"value\":%llu,\"hr\":\"0x%lx\",\"elapsedMs\":%llu",
			api ? api : "", stage ? stage : "", DX12RuntimeState::GetPresentCount(), queue,
			count, fence, static_cast<unsigned long long>(value), hr,
			static_cast<unsigned long long>(elapsedMs));
#else
		(void)api;
		(void)stage;
		(void)queue;
		(void)count;
		(void)fence;
		(void)value;
		(void)hr;
		(void)elapsedMs;
#endif
	}
};
