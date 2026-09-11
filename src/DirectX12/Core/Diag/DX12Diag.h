#pragma once

// ============================================================================
// Unified DX12 diagnostics: asynchronous logging + named-bucket profiling.
//
// This header replaces the former split implementations:
//   Core/DX12LogManager.h, Core/DX12LogUtils.h, Core/HotPath/DX12Profiling.h,
//   Core/CommandFlow/DX12ProfilingImpl.h, Hooks/DX12Profiler.h,
//   Hooks/DX12HotPathTiming.h, Hooks/DX12CreateTimingTelemetry.h,
//   Hooks/DX12DescriptorShadowTelemetry.h, ModRuntime/DX12ModLockTelemetry.h,
//   Core/CommandFlow/DX12ResidentMaintenanceTelemetry.h,
//   Core/ModState/DX12OuterTimingProbe.h
//
// Design rules:
//   1. TIMING builds only (DX12_RELEASE_TIMING). In every other build mode
//      the public macros at the bottom expand to nothing: no argument
//      evaluation, no object construction, no preparation work. Release
//      binaries carry zero diagnostic cost.
//   2. One profiler. Every scope/counter/slice records into the same
//      per-thread open-addressing bucket tables keyed by literal pointer.
//      Bucket names carry a domain prefix ("Api.", "Stage.", "Create.",
//      "Lock.", "Rm.", "Present.", "Queue.", ...) that the writer thread
//      maps back to the historical log line shapes.
//   3. Write points live off the render thread. The render thread records
//      counters (plain per-thread RMW) and pushes one small frame record per
//      present; the log writer thread drains the tables, merges, sorts,
//      formats and writes. Nothing is merged or formatted inside a frame.
// ============================================================================

#include "../DX12RuntimeState.h"

#if defined(DX12_RELEASE_TIMING)
#define DX12_TIMING_BUILD 1
#else
#define DX12_TIMING_BUILD 0
#endif

#define DX12_DIAG_JOIN_INNER(a, b) a##b
#define DX12_DIAG_JOIN(a, b) DX12_DIAG_JOIN_INNER(a, b)

#if DX12_TIMING_BUILD

// ----------------------------------------------------------------------------
// DX12DiagLog - queued async log writer.
//
// Each thread appends formatted lines into its own TLS buffer (no lock, no
// allocation on the hot path) and publishes the whole buffer under one SRW
// acquisition when it grows past kTlsFlushBytes or on an explicit Flush().
// The writer thread parks on an event with a 100ms timeout, writes queued
// lines, then pumps pending frame summaries from DX12DiagProf.
// ----------------------------------------------------------------------------
class DX12DiagLog
{
public:
	static bool Open();
	static void Close();
	// Publishes the calling thread's buffered lines and wakes the writer.
	static void Flush();

	static void Fields(const char *func, const char *fmt, ...);
	static void FieldsV(const char *func, const char *fmt, va_list args, bool flush);
	static void FieldsFlush(const char *func, const char *fmt, ...);
	static void Text(const char *fmt, ...);

	// Submission-gap probe for the ExecuteCommandLists hook: wall clock and
	// this thread's CPU time since the previous submit on this thread.
	static void LogExecuteGap()
	{
		const ULONGLONG wallNow = GetTickCount64();
		FILETIME createTime = {};
		FILETIME exitTime = {};
		FILETIME kernelTime = {};
		FILETIME userTime = {};
		GetThreadTimes(
			GetCurrentThread(), &createTime, &exitTime, &kernelTime, &userTime);
		const ULONGLONG cpuNow =
			((static_cast<ULONGLONG>(kernelTime.dwHighDateTime) << 32) |
				kernelTime.dwLowDateTime) / 10 +
			((static_cast<ULONGLONG>(userTime.dwHighDateTime) << 32) |
				userTime.dwLowDateTime) / 10;
		thread_local ULONGLONG lastWall = 0;
		thread_local ULONGLONG lastCpu = 0;
		thread_local LONG lastPresent = -1;
		const LONG present = DX12RuntimeState::GetPresentCount();
		if (lastWall != 0 && lastPresent >= 0) {
			const ULONGLONG wallDelta = wallNow - lastWall;
			const ULONGLONG cpuDelta = cpuNow - lastCpu;
			Fields("DX12ExecuteGapDiag",
				"\"present\":%ld,\"wallUs\":%llu,\"threadCpuUs\":%llu,"
				"\"framesSince\":%ld",
				present,
				wallDelta * 1000,
				cpuDelta,
				static_cast<LONG>(present - lastPresent));
		}
		lastWall = wallNow;
		lastCpu = cpuNow;
		lastPresent = present;
	}

	// Wakes the writer thread (latency hint only; it polls on a timeout).
	static void WakeWriter();

	// Handles owned by the writer itself (its 100ms poll event). Wait hooks
	// skip them so the hook never measures the wrapper's own idle sleep.
	static bool IsInternalEventHandle(HANDLE handle);

	// Direct writer-thread access for DX12DiagProf frame summaries.
	static FILE *LogFile() { return sLog; }
	static void WriteDirect(const char *text, size_t length);
	static void FlushDirect();

private:
	static DWORD WINAPI ThreadProc(void *);
	static void DrainQueueToFile();
	static void EnqueueLine(const char *line);
	static void PublishTlsBuffer();
	static std::string &TlsLineBuffer();
	static void NormalizeFields(const char *input, char *output, size_t outputSize);
	static void WriteFormatted(const char *func, const char *extra, bool flush);
	static void MakeTimePlain(char *buffer, size_t bufferSize);
	static void RegisterInternalEventHandle(HANDLE handle);

	static constexpr size_t kTlsFlushBytes = 8192;
	static constexpr size_t kMaxQueuedLines = 8192;

	inline static FILE *sLog = nullptr;
	inline static HANDLE sThread = nullptr;
	inline static HANDLE sEvent = nullptr;
	inline static SRWLOCK sQueueLock = SRWLOCK_INIT;
	inline static std::deque<std::string> sQueue;
	inline static volatile LONG sDroppedLines = 0;
	inline static volatile LONG sStopRequested = 0;
	inline static volatile LONG64 sInternalEventHandle = 0;
};

// One present's worth of frame-boundary context. Pushed by the render thread
// at frame leave; consumed by the writer thread. Plain data, fixed size.
struct DX12DiagFrameRecord
{
	const char *api = nullptr;
	void *swapChain = nullptr;
	LONG64 frameStartQpc = 0;
	LONG64 frameEndQpc = 0;
	LONG64 frameIntervalUs = 0;
	LONG64 outsideUs = 0;
	LONG64 presentWallUs = 0;
	LONG present = 0;
	LONG hr = 0;
};

// ----------------------------------------------------------------------------
// DX12DiagProf - unified named-bucket profiler.
//
// Recording primitives (hot path, any thread):
//   Scope / ScopePair  - RAII QPC scopes (Pause/Resume/Finish supported)
//   Begin / End        - positional TLS-stack pairs for awkward control flow
//   Count(name)        - +1 call
//   AddUs(name, us)    - +1 call, accumulate microseconds
//   AddItems(name, n)  - accumulate an item count (slots, bytes, lists)
//   AddCpuStage(...)   - (category, stage, kind) keyed bucket with items
//
// Frame boundary (render thread): FrameEnter/FrameLeave push a frame record
// and wake the writer. FlushFrameSummaries (writer thread) drains the ring,
// and every kSummaryIntervalFrames frames drains all thread tables and emits
// the historical timing lines straight to the log file.
// ----------------------------------------------------------------------------
class DX12DiagProf
{
public:
	struct Bucket
	{
		const char *name = nullptr;
		const char *sub = nullptr;
		LONG64 calls = 0;
		LONG64 totalUs = 0;
		LONG64 maxUs = 0;
		LONG64 items = 0;
		UINT kind = 0;
	};

	static constexpr UINT kThreadTableCount = 64;
	// Sized from the instrumented source, not guessed: this build registers 357
	// distinct bucket names (725 call sites). At 256 slots the per-thread tables
	// were over-subscribed, so FindBucket ran out of probe slots and dropped
	// counters silently - a newly added counter simply never appeared in the
	// log, and the surviving names paid long probe chains (which is how a
	// one-atomic-load stage got reported as ~2us/call). 1024 keeps the load
	// factor near 0.35, i.e. ~1 probe per lookup. TIMING builds only.
	static constexpr UINT kBucketsPerThread = 1024; // power of two
	// Merged table is a stack array (Bucket merged[kMergedBuckets]), so it stays
	// at 512: 357 names fit with room to spare and the stack cost stays small.
	static constexpr UINT kMergedBuckets = 512;    // power of two
	static constexpr UINT kScopeStackDepth = 32;
	static constexpr LONG kSummaryIntervalFrames = 60;

	struct alignas(64) ThreadTable
	{
		DWORD threadId = 0;
		Bucket buckets[kBucketsPerThread];
	};

	struct ScopeStackEntry
	{
		Bucket *bucket = nullptr;
		LONGLONG startQpc = 0;
	};

	class Scope
	{
	public:
		explicit Scope(const char *name)
			: mBucket(FindBucket(name, nullptr, 0))
		{
			if (!mBucket)
				return;
			++mBucket->calls;
			mStartQpc = QpcNow();
			mRunning = true;
		}

		~Scope() { Finish(); }

		void Pause()
		{
			if (!mRunning)
				return;
			AccumulateUs(mBucket, mStartQpc, QpcNow());
			mRunning = false;
		}

		void Resume()
		{
			if (mRunning || !mBucket)
				return;
			mStartQpc = QpcNow();
			mRunning = true;
		}

		void Finish()
		{
			if (!mRunning)
				return;
			AccumulateUs(mBucket, mStartQpc, QpcNow());
			mRunning = false;
		}

		Scope(const Scope &) = delete;
		Scope &operator=(const Scope &) = delete;

	private:
		Bucket *mBucket = nullptr;
		LONGLONG mStartQpc = 0;
		bool mRunning = false;
	};

	// One QPC pair feeding two buckets (the former ApiStage scope kind).
	class ScopePair
	{
	public:
		ScopePair(const char *nameA, const char *nameB)
			: mBucketA(FindBucket(nameA, nullptr, 0)),
			  mBucketB(FindBucket(nameB, nullptr, 0))
		{
			if (mBucketA)
				++mBucketA->calls;
			if (mBucketB)
				++mBucketB->calls;
			mStartQpc = QpcNow();
		}

		~ScopePair()
		{
			const LONGLONG endQpc = QpcNow();
			AccumulateUs(mBucketA, mStartQpc, endQpc);
			AccumulateUs(mBucketB, mStartQpc, endQpc);
		}

		ScopePair(const ScopePair &) = delete;
		ScopePair &operator=(const ScopePair &) = delete;

	private:
		Bucket *mBucketA = nullptr;
		Bucket *mBucketB = nullptr;
		LONGLONG mStartQpc = 0;
	};

	// --- time helpers ---------------------------------------------------
	static LONGLONG Frequency()
	{
		static const LONGLONG frequency = []() {
			LARGE_INTEGER value = {};
			QueryPerformanceFrequency(&value);
			return value.QuadPart > 0 ? value.QuadPart : 1;
		}();
		return frequency;
	}

	static LONGLONG QpcNow()
	{
		LARGE_INTEGER value = {};
		QueryPerformanceCounter(&value);
		return value.QuadPart;
	}

	static LARGE_INTEGER Now()
	{
		LARGE_INTEGER value = {};
		QueryPerformanceCounter(&value);
		return value;
	}

	static LONG64 DeltaUs(LONGLONG begin, LONGLONG end)
	{
		if (end <= begin)
			return 0;
		// Round to the nearest microsecond instead of truncating. Per-call work
		// on this hot path is routinely below the 0.1us QPC quantum, and a plain
		// integer division reported every one of those calls as exactly zero
		// while a 1.9us call reported 1us, so every sub-microsecond stage (per
		// draw, per IA bind) read as free and the remaining averages were
		// understated by up to a full microsecond per call. Rounding keeps the
		// same units and the same per-call upper bound while making the sum over
		// millions of calls an unbiased estimate of the real cost.
		const LONG64 frequency = Frequency();
		return ((end - begin) * 1000000 + frequency / 2) / frequency;
	}

	static LONG64 ElapsedUs(const LARGE_INTEGER &begin)
	{
		return DeltaUs(begin.QuadPart, QpcNow());
	}

	// --- recording primitives --------------------------------------------
	static void Count(const char *name)
	{
		Bucket *bucket = FindBucket(name, nullptr, 0);
		if (bucket)
			++bucket->calls;
	}

	static void AddUs(const char *name, LONG64 us)
	{
		if (us < 0)
			us = 0;
		Bucket *bucket = FindBucket(name, nullptr, 0);
		if (!bucket)
			return;
		++bucket->calls;
		bucket->totalUs += us;
		if (us > bucket->maxUs)
			bucket->maxUs = us;
	}

	static void AddUsItems(const char *name, LONG64 us, LONG64 items)
	{
		AddUs(name, us);
		Bucket *bucket = FindBucket(name, nullptr, 0);
		if (bucket)
			bucket->items += items;
	}

	static void AddItems(const char *name, LONG64 items)
	{
		if (!items)
			return;
		Bucket *bucket = FindBucket(name, nullptr, 0);
		if (bucket)
			bucket->items += items;
	}

	static void AddCpuStage(
		const char *category, const char *stage, UINT kind, LONG64 items, LONG64 us)
	{
		if (us < 0)
			us = 0;
		Bucket *bucket = FindBucket(category, stage, kind);
		if (!bucket)
			return;
		++bucket->calls;
		bucket->totalUs += us;
		if (us > bucket->maxUs)
			bucket->maxUs = us;
		bucket->items += items;
	}

	static void Begin(const char *name)
	{
		Bucket *bucket = FindBucket(name, nullptr, 0);
		if (!bucket)
			return;
		++bucket->calls;
		if (sScopeDepth < kScopeStackDepth) {
			ScopeStackEntry &entry = sScopeStack[sScopeDepth++];
			entry.bucket = bucket;
			entry.startQpc = QpcNow();
		}
	}

	static void End(const char *name)
	{
		(void)name; // pairing is positional; mismatched names only blur diagnostics
		if (sScopeDepth == 0)
			return;
		const ScopeStackEntry entry = sScopeStack[--sScopeDepth];
		AccumulateUs(entry.bucket, entry.startQpc, QpcNow());
	}

	// --- instrumented SRW locks -------------------------------------------
	// Uncontended acquire stays a single TryAcquire; only the contended slow
	// path is timed and charged to the named bucket (contentions = calls).
	static void AcquireLockShared(SRWLOCK *lock, const char *name)
	{
		if (TryAcquireSRWLockShared(lock))
			return;
		const LONGLONG begin = QpcNow();
		AcquireSRWLockShared(lock);
		AddUs(name, DeltaUs(begin, QpcNow()));
	}

	static void AcquireLockExclusive(SRWLOCK *lock, const char *name)
	{
		if (TryAcquireSRWLockExclusive(lock))
			return;
		const LONGLONG begin = QpcNow();
		AcquireSRWLockExclusive(lock);
		AddUs(name, DeltaUs(begin, QpcNow()));
	}

	// --- frame boundary (render thread) ------------------------------------
	static void FrameEnter(const char *api, void *swapChain)
	{
		(void)api;
		(void)swapChain;
		const LONGLONG now = QpcNow();
		const LONGLONG previousEnter =
			InterlockedExchange64(&sLastPresentEnterQpc, now);
		const LONGLONG previousLeave = ReadLong64(&sLastPresentLeaveQpc);
		sCurrentFrameIntervalUs = previousEnter > 0 ? DeltaUs(previousEnter, now) : 0;
		sCurrentOutsideUs = previousLeave > 0 ? DeltaUs(previousLeave, now) : 0;
		InterlockedExchange64(&sCurrentPresentEnterQpc, now);
		Count("Present.enter");
	}

	static void FrameLeave(const char *api, void *swapChain, HRESULT hr)
	{
		const LONGLONG now = QpcNow();
		const LONGLONG enter = InterlockedCompareExchange64(&sCurrentPresentEnterQpc, 0, 0);

		DX12DiagFrameRecord record;
		record.api = api;
		record.swapChain = swapChain;
		record.hr = static_cast<LONG>(hr);
		record.frameStartQpc = InterlockedCompareExchange64(&sLastPresentLeaveQpc, 0, 0);
		record.frameEndQpc = now;
		record.frameIntervalUs = sCurrentFrameIntervalUs;
		record.outsideUs = sCurrentOutsideUs;
		record.presentWallUs = enter > 0 ? DeltaUs(enter, now) : 0;
		record.present = DX12RuntimeState::GetPresentCount();

		InterlockedExchange64(&sLastPresentLeaveQpc, now);
		Count("Present.leave");
		PushFrameRecord(record);
		// Publish this thread's log lines before the writer drains the ring,
		// so the frame's own lines precede its summary block in the file.
		DX12DiagLog::Flush();
	}

	// --- writer-thread frame summaries --------------------------------------
	static void FlushFrameSummaries() { PumpFrameRing(false); }
	static void FlushFrameSummariesFinal() { PumpFrameRing(true); }

	// --- wait-probe shared state (DX12WaitHooks) ------------------------------
	static bool IsTrackedWaitHandle(HANDLE handle, bool *matchesFenceEvent, bool *matchesFrameLatency);
	static void SnapshotLastFenceEvent(ID3D12Fence **fence, UINT64 *value, HANDLE *eventHandle);
	static void RegisterFrameLatencyWaitableObject(IDXGISwapChain *swapChain, HANDLE handle);
	static void RegisterFenceEventOnCompletion(
		ID3D12Fence *fence, HANDLE eventHandle, UINT64 value,
		UINT64 completedBefore, UINT64 completedAfter, HRESULT hr);
	static void RecordSystemWait(
		const char *api, HANDLE primaryHandle, HANDLE secondaryHandle,
		DWORD waitMs, DWORD waitResult, DWORD lastError, LONG64 us,
		bool matchesFenceEvent, bool matchesFrameLatency);
	static void LogCurrentCallStack(
		const char *reason, const char *api, HANDLE handle,
		ID3D12Fence *fence, UINT64 fenceValue, LONG64 waitUs,
		DWORD waitResult, DWORD threadId, ULONG skipFrames = 0);
	static UINT64 FenceCompletedValue(ID3D12Fence *fence)
	{
		return fence ? fence->GetCompletedValue() : 0;
	}

private:
	// --- bucket table ------------------------------------------------------
	static UINT HashKey(const char *name, const char *sub, UINT kind)
	{
		UINT_PTR h = (reinterpret_cast<UINT_PTR>(name) >> 4) * 2654435761u;
		h ^= (reinterpret_cast<UINT_PTR>(sub) >> 4) * 40503u;
		h ^= kind * 0x9e3779b9u;
		return static_cast<UINT>(h);
	}

	static ThreadTable *AcquireThreadTable()
	{
		const LONG index = InterlockedIncrement(&sThreadTableCount) - 1;
		// Threads beyond the pool share the last table; only diagnostics blur.
		if (index < 0 || index >= static_cast<LONG>(kThreadTableCount))
			return &sThreadTables[kThreadTableCount - 1];
		sThreadTables[index].threadId = GetCurrentThreadId();
		return &sThreadTables[index];
	}

	static ThreadTable *GetThreadTable()
	{
		static thread_local ThreadTable *table = AcquireThreadTable();
		return table;
	}

	static Bucket *FindBucket(const char *name, const char *sub, UINT kind)
	{
		if (!name)
			return nullptr;
		ThreadTable *table = GetThreadTable();
		const UINT mask = kBucketsPerThread - 1;
		const UINT slot = HashKey(name, sub, kind) & mask;
		for (UINT probe = 0; probe < kBucketsPerThread; ++probe) {
			Bucket &bucket = table->buckets[(slot + probe) & mask];
			if (bucket.name == name && bucket.sub == sub && bucket.kind == kind)
				return &bucket;
			if (!bucket.name) {
				bucket.name = name;
				bucket.sub = sub;
				bucket.kind = kind;
				return &bucket;
			}
		}
		// Table full: the sample is dropped. Report it once so a silently
		// missing counter can never be mistaken for "the code never ran".
		if (InterlockedCompareExchange(&sTableFull, 1, 0) == 0)
			DX12DiagLog::FieldsFlush("DX12Profiling",
				"event=table_full threadId=%lu dropped=%s",
				GetCurrentThreadId(), name);
		return nullptr;
	}

	static void AccumulateUs(Bucket *bucket, LONGLONG startQpc, LONGLONG endQpc)
	{
		if (!bucket || endQpc <= startQpc)
			return;
		const LONG64 us = DeltaUs(startQpc, endQpc);
		bucket->totalUs += us;
		if (us > bucket->maxUs)
			bucket->maxUs = us;
	}

	// --- frame ring (SPSC: render thread produces, writer thread consumes) ---
	static void PushFrameRecord(const DX12DiagFrameRecord &record)
	{
		const LONG write = sFrameRingWrite;
		if (write - ReadLong(&sFrameRingRead) >= static_cast<LONG>(kFrameRingCapacity)) {
			InterlockedIncrement(&sFrameSummariesDropped);
			return;
		}
		sFrameRing[write % kFrameRingCapacity] = record;
		InterlockedExchange(&sFrameRingWrite, write + 1);
	}

	static void PumpFrameRing(bool force)
	{
		std::string pulses;
		for (;;) {
			const LONG read = sFrameRingRead;
			if (read == InterlockedCompareExchange(&sFrameRingWrite, 0, 0))
				break;
			AccumulateWindow(sFrameRing[read % kFrameRingCapacity]);
			// One lightweight per-frame pulse line, mirroring the legacy
			// per-frame DX12FrameTimeline cadence (sWindowLast is the record
			// just accumulated).
			EmitFramePulse(pulses);
			InterlockedExchange(&sFrameRingRead, read + 1);
		}
		if (!pulses.empty())
			DX12DiagLog::WriteDirect(pulses.data(), pulses.size());
		if (!force && sWindowFrames < kSummaryIntervalFrames)
			return;
		if (sWindowFrames <= 0)
			return;
		EmitSummary();
	}

	static void AccumulateWindow(const DX12DiagFrameRecord &record)
	{
		++sWindowFrames;
		sWindowFrameIntervalUs += record.frameIntervalUs;
		sWindowOutsideUs += record.outsideUs;
		sWindowPresentWallUs += record.presentWallUs;
		const LONG64 frameWallUs = record.outsideUs + record.presentWallUs;
		sWindowFrameWallUs += frameWallUs;
		if (frameWallUs > sWindowMaxFrameWallUs)
			sWindowMaxFrameWallUs = frameWallUs;
		if (!sWindowFirstFrameStartQpc)
			sWindowFirstFrameStartQpc = record.frameStartQpc;
		sWindowLastFrameEndQpc = record.frameEndQpc;
		sWindowLast = record;
	}

	// --- summary emission (writer thread only) --------------------------------
	static void EmitSummary();
	static void DrainThreadTables(Bucket *merged, UINT *mergedCount);
	static Bucket *FindMerged(
		Bucket *merged, UINT *mergedCount,
		const char *name, const char *sub, UINT kind);
	static const Bucket *LookupMerged(
		const Bucket *merged, UINT mergedCount, const char *name);
	static LONG64 MergedTotalUs(const Bucket *merged, UINT mergedCount, const char *name);
	static LONG64 MergedCalls(const Bucket *merged, UINT mergedCount, const char *name);
	static LONG64 MergedItems(const Bucket *merged, UINT mergedCount, const char *name);
	static bool NameHasPrefix(const char *name, const char *prefix);

	static void AppendFmt(std::string &out, const char *fmt, ...);
	static void EmitFramePulse(std::string &out);
	static void EmitFrameTimeline(std::string &out, const Bucket *merged, UINT mergedCount);
	static void EmitApiWallSection(std::string &out, const Bucket *merged, UINT mergedCount);
	static void EmitHotPathSection(std::string &out, const Bucket *merged, UINT mergedCount);
	static void EmitCreateSection(std::string &out, const Bucket *merged, UINT mergedCount);
	static void EmitCpuStageSection(std::string &out, const Bucket *merged, UINT mergedCount);
	static void EmitLockSection(std::string &out, const Bucket *merged, UINT mergedCount);
	static void EmitCounterSections(std::string &out, const Bucket *merged, UINT mergedCount);
	static void EmitScopeTable(std::string &out, const Bucket *merged, UINT mergedCount);
	static void ResetWindow();

	inline static ThreadTable sThreadTables[kThreadTableCount] = {};
	inline static volatile LONG sThreadTableCount = 0;
	// Set once when a thread table runs out of probe slots, so the drop is
	// visible in the log instead of looking like code that never ran.
	inline static volatile LONG sTableFull = 0;
	inline static thread_local ScopeStackEntry sScopeStack[kScopeStackDepth] = {};
	inline static thread_local UINT sScopeDepth = 0;

	static constexpr UINT kFrameRingCapacity = 16;
	inline static DX12DiagFrameRecord sFrameRing[kFrameRingCapacity] = {};
	inline static volatile LONG sFrameRingWrite = 0;
	inline static volatile LONG sFrameRingRead = 0;
	inline static volatile LONG sFrameSummariesDropped = 0;

	inline static volatile LONG64 sLastPresentEnterQpc = 0;
	inline static volatile LONG64 sLastPresentLeaveQpc = 0;
	inline static volatile LONG64 sCurrentPresentEnterQpc = 0;
	inline static LONG64 sCurrentFrameIntervalUs = 0;
	inline static LONG64 sCurrentOutsideUs = 0;

	inline static LONG sWindowFrames = 0;
	inline static LONG64 sWindowFrameIntervalUs = 0;
	inline static LONG64 sWindowOutsideUs = 0;
	inline static LONG64 sWindowPresentWallUs = 0;
	inline static LONG64 sWindowFrameWallUs = 0;
	inline static LONG64 sWindowMaxFrameWallUs = 0;
	inline static LONG64 sWindowFirstFrameStartQpc = 0;
	inline static LONG64 sWindowLastFrameEndQpc = 0;
	inline static DX12DiagFrameRecord sWindowLast;

	// Wait-probe registrations (written rarely, read on every hooked wait).
	inline static volatile LONG64 sLastFrameLatencyHandle = 0;
	inline static volatile LONG64 sLastFrameLatencySwapChain = 0;
	inline static volatile LONG64 sLastFenceEventHandle = 0;
	inline static volatile LONG64 sLastFenceEventFence = 0;
	inline static volatile LONG64 sLastFenceEventValue = 0;
	inline static volatile LONG64 sLastFenceEventCompletedBefore = 0;
	inline static volatile LONG64 sLastFenceEventCompletedAfter = 0;

	static LONG64 ReadLong64(volatile LONG64 *value) { return *value; }
	static LONG ReadLong(volatile LONG *value) { return *value; }
};

// ============================================================================
// DX12DiagLog implementation
// ============================================================================

inline bool DX12DiagLog::Open()
{
	wchar_t path[MAX_PATH];
	if (!GetModuleFileNameW(DX12RuntimeState::GetModule(), path, MAX_PATH))
		return false;

	PathRemoveFileSpecW(path);
	// Keep a timestamped copy of every run so normal vs broken sessions can
	// be compared: the log is overwritten on each launch otherwise.
	wchar_t logPath[MAX_PATH];
	wchar_t historyPath[MAX_PATH];
	wsprintfW(logPath, L"%s\\d3d12_log.txt", path);
	wsprintfW(historyPath, L"%s\\d3d12_log_%llu.txt", path,
		static_cast<unsigned long long>(GetTickCount64()));
	if (PathFileExistsW(logPath))
		CopyFileW(logPath, historyPath, FALSE);
	PathAppendW(path, L"d3d12_log.txt");
	sLog = _wfsopen(path, L"w", _SH_DENYNO);
	if (!sLog)
		return false;

	InterlockedExchange(&sStopRequested, 0);
	sEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	if (!sEvent) {
		fclose(sLog);
		sLog = nullptr;
		return false;
	}
	RegisterInternalEventHandle(sEvent);
	sThread = CreateThread(nullptr, 0, ThreadProc, nullptr, 0, nullptr);
	if (!sThread) {
		CloseHandle(sEvent);
		sEvent = nullptr;
		RegisterInternalEventHandle(nullptr);
		fclose(sLog);
		sLog = nullptr;
		return false;
	}
	return true;
}

inline void DX12DiagLog::Close()
{
	InterlockedExchange(&sStopRequested, 1);
	if (sEvent)
		SetEvent(sEvent);
	if (sThread) {
		WaitForSingleObject(sThread, 5000);
		CloseHandle(sThread);
		sThread = nullptr;
	}
	if (sEvent) {
		CloseHandle(sEvent);
		sEvent = nullptr;
		RegisterInternalEventHandle(nullptr);
	}
	if (sLog) {
		fclose(sLog);
		sLog = nullptr;
	}
	AcquireSRWLockExclusive(&sQueueLock);
	sQueue.clear();
	ReleaseSRWLockExclusive(&sQueueLock);
}

inline void DX12DiagLog::Flush()
{
	PublishTlsBuffer();
	WakeWriter();
}

inline void DX12DiagLog::WakeWriter()
{
	if (sEvent)
		SetEvent(sEvent);
}

inline bool DX12DiagLog::IsInternalEventHandle(HANDLE handle)
{
	const ULONG_PTR internalHandle = static_cast<ULONG_PTR>(sInternalEventHandle);
	return internalHandle != 0 &&
		reinterpret_cast<ULONG_PTR>(handle) == internalHandle;
}

inline void DX12DiagLog::RegisterInternalEventHandle(HANDLE handle)
{
	InterlockedExchange64(&sInternalEventHandle,
		static_cast<LONG64>(reinterpret_cast<ULONG_PTR>(handle)));
}

inline void DX12DiagLog::WriteDirect(const char *text, size_t length)
{
	if (sLog && text && length)
		fwrite(text, 1, length, sLog);
}

inline void DX12DiagLog::FlushDirect()
{
	if (sLog)
		fflush(sLog);
}

inline DWORD WINAPI DX12DiagLog::ThreadProc(void *)
{
	for (;;) {
		if (sEvent)
			WaitForSingleObject(sEvent, 100);
		if (sEvent)
			ResetEvent(sEvent);

		DrainQueueToFile();
		// Frame summaries are drained, merged, formatted and written here, on
		// the writer thread - never inside the render thread's present.
		DX12DiagProf::FlushFrameSummaries();

		const bool stopping =
			InterlockedCompareExchange(&sStopRequested, 0, 0) != 0;
		if (stopping) {
			DrainQueueToFile();
			DX12DiagProf::FlushFrameSummariesFinal();
			if (sLog)
				fflush(sLog);
			break;
		}
	}
	return 0;
}

inline void DX12DiagLog::DrainQueueToFile()
{
	std::vector<std::string> pending;
	AcquireSRWLockExclusive(&sQueueLock);
	while (!sQueue.empty()) {
		pending.push_back(std::move(sQueue.front()));
		sQueue.pop_front();
	}
	ReleaseSRWLockExclusive(&sQueueLock);

	if (!sLog)
		return;
	const LONG dropped = InterlockedExchange(&sDroppedLines, 0);
	if (dropped > 0) {
		char droppedLine[256];
		char timePlain[32];
		MakeTimePlain(timePlain, sizeof(timePlain));
		snprintf(droppedLine, sizeof(droppedLine),
			"time=%s,func=DX12DiagLog,count=%ld\n", timePlain, dropped);
		fputs(droppedLine, sLog);
	}
	for (const std::string &line : pending)
		fputs(line.c_str(), sLog);
	if (!pending.empty())
		fflush(sLog);
}

inline std::string &DX12DiagLog::TlsLineBuffer()
{
	static thread_local std::string buffer;
	if (buffer.capacity() < kTlsFlushBytes)
		buffer.reserve(kTlsFlushBytes);
	return buffer;
}

inline void DX12DiagLog::PublishTlsBuffer()
{
	std::string &buffer = TlsLineBuffer();
	if (buffer.empty())
		return;
	AcquireSRWLockExclusive(&sQueueLock);
	const bool wasEmpty = sQueue.empty();
	if (sQueue.size() < kMaxQueuedLines) {
		sQueue.emplace_back();
		sQueue.back().swap(buffer);
	} else {
		InterlockedIncrement(&sDroppedLines);
		buffer.clear();
	}
	ReleaseSRWLockExclusive(&sQueueLock);
	// The writer polls on a timeout, so signaling is only a latency hint;
	// waking it once per publish keeps present-time log bursts cheap.
	if (wasEmpty)
		WakeWriter();
}

inline void DX12DiagLog::EnqueueLine(const char *line)
{
	if (!line || !line[0])
		return;
	std::string &buffer = TlsLineBuffer();
	buffer += line;
	if (buffer.size() >= kTlsFlushBytes)
		PublishTlsBuffer();
}

// Plain text fields: time=...,func=...,key=value,key2=value2.
// Existing JSON-like field fragments are normalized without allocation.
inline void DX12DiagLog::NormalizeFields(const char *input, char *output, size_t outputSize)
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

inline void DX12DiagLog::MakeTimePlain(char *buffer, size_t bufferSize)
{
	if (!buffer || !bufferSize)
		return;
	snprintf(buffer, bufferSize, "%llu",
		static_cast<unsigned long long>(GetTickCount64()));
}

inline void DX12DiagLog::WriteFormatted(const char *func, const char *extra, bool flush)
{
	if (!sLog)
		return;

	char timePlain[32] = {};
	MakeTimePlain(timePlain, sizeof(timePlain));
	char line[8192];
	char fields[6144];
	NormalizeFields(extra, fields, sizeof(fields));
	if (extra && extra[0]) {
		snprintf(line, sizeof(line), "time=%s,func=%s,%s\n", timePlain, func, fields);
	} else {
		snprintf(line, sizeof(line), "time=%s,func=%s\n", timePlain, func);
	}
	EnqueueLine(line);
	if (flush)
		Flush();
}

inline void DX12DiagLog::Text(const char *fmt, ...)
{
	if (!sLog)
		return;

	va_list args;
	char message[2048];
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);

	char fields[2304];
	snprintf(fields, sizeof(fields), "message=%s", message);
	WriteFormatted("Log", fields, false);
}

inline void DX12DiagLog::FieldsV(const char *func, const char *fmt, va_list args, bool flush)
{
	if (!sLog)
		return;

	const char *safeFunc = func ? func : "Unknown";
	char extra[4096];
	extra[0] = '\0';
	if (fmt && fmt[0])
		vsnprintf(extra, sizeof(extra), fmt, args);
	WriteFormatted(safeFunc, extra, flush);
}

inline void DX12DiagLog::Fields(const char *func, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	FieldsV(func, fmt, args, false);
	va_end(args);
}

inline void DX12DiagLog::FieldsFlush(const char *func, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	FieldsV(func, fmt, args, true);
	va_end(args);
}

// ============================================================================
// DX12DiagProf implementation (writer-thread half)
// ============================================================================

inline bool DX12DiagProf::IsTrackedWaitHandle(
	HANDLE handle, bool *matchesFenceEvent, bool *matchesFrameLatency)
{
	// Plain volatile reads: the handles are written once per registration
	// (rare) while every wait call in the process reads them. Aligned 64-bit
	// reads are atomic on x64 and a stale diagnostic value is fine.
	const ULONG_PTR rawHandle = reinterpret_cast<ULONG_PTR>(handle);
	const ULONG_PTR rawFenceHandle = static_cast<ULONG_PTR>(ReadLong64(&sLastFenceEventHandle));
	const ULONG_PTR rawLatencyHandle = static_cast<ULONG_PTR>(ReadLong64(&sLastFrameLatencyHandle));
	const bool fenceMatch = rawHandle != 0 && rawHandle == rawFenceHandle;
	const bool latencyMatch = rawHandle != 0 && rawHandle == rawLatencyHandle;
	if (matchesFenceEvent)
		*matchesFenceEvent = fenceMatch;
	if (matchesFrameLatency)
		*matchesFrameLatency = latencyMatch;
	return fenceMatch || latencyMatch;
}

inline void DX12DiagProf::SnapshotLastFenceEvent(
	ID3D12Fence **fence, UINT64 *value, HANDLE *eventHandle)
{
	if (fence) {
		*fence = reinterpret_cast<ID3D12Fence*>(static_cast<ULONG_PTR>(
			InterlockedCompareExchange64(&sLastFenceEventFence, 0, 0)));
	}
	if (value) {
		*value = static_cast<UINT64>(
			InterlockedCompareExchange64(&sLastFenceEventValue, 0, 0));
	}
	if (eventHandle) {
		*eventHandle = reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(
			InterlockedCompareExchange64(&sLastFenceEventHandle, 0, 0)));
	}
}

inline void DX12DiagProf::RegisterFrameLatencyWaitableObject(
	IDXGISwapChain *swapChain, HANDLE handle)
{
	InterlockedExchange64(&sLastFrameLatencyHandle,
		static_cast<LONG64>(reinterpret_cast<ULONG_PTR>(handle)));
	InterlockedExchange64(&sLastFrameLatencySwapChain,
		static_cast<LONG64>(reinterpret_cast<ULONG_PTR>(swapChain)));
	DX12DiagLog::Fields("DX12WaitableObject",
		"\"kind\":\"frame_latency\",\"swapchain\":\"%p\",\"handle\":\"%p\","
		"\"threadId\":%ld,\"present\":%ld",
		swapChain, handle,
		static_cast<LONG>(GetCurrentThreadId()),
		DX12RuntimeState::GetPresentCount());
}

inline void DX12DiagProf::RegisterFenceEventOnCompletion(
	ID3D12Fence *fence, HANDLE eventHandle, UINT64 value,
	UINT64 completedBefore, UINT64 completedAfter, HRESULT hr)
{
	InterlockedExchange64(&sLastFenceEventHandle,
		static_cast<LONG64>(reinterpret_cast<ULONG_PTR>(eventHandle)));
	InterlockedExchange64(&sLastFenceEventFence,
		static_cast<LONG64>(reinterpret_cast<ULONG_PTR>(fence)));
	InterlockedExchange64(&sLastFenceEventValue, static_cast<LONG64>(value));
	InterlockedExchange64(&sLastFenceEventCompletedBefore, static_cast<LONG64>(completedBefore));
	InterlockedExchange64(&sLastFenceEventCompletedAfter, static_cast<LONG64>(completedAfter));
	DX12DiagLog::Fields("DX12WaitableObject",
		"\"kind\":\"fence_event\",\"fence\":\"%p\",\"handle\":\"%p\","
		"\"value\":%llu,\"completedBefore\":%llu,\"completedAfter\":%llu,"
		"\"hr\":\"0x%lx\",\"threadId\":%ld,\"present\":%ld",
		fence, eventHandle,
		static_cast<unsigned long long>(value),
		static_cast<unsigned long long>(completedBefore),
		static_cast<unsigned long long>(completedAfter),
		static_cast<unsigned long>(hr),
		static_cast<LONG>(GetCurrentThreadId()),
		DX12RuntimeState::GetPresentCount());
}

inline void DX12DiagProf::RecordSystemWait(
	const char *api, HANDLE primaryHandle, HANDLE secondaryHandle,
	DWORD waitMs, DWORD waitResult, DWORD lastError, LONG64 us,
	bool matchesFenceEvent, bool matchesFrameLatency)
{
	if (us < 0)
		us = 0;
	AddUs("Wait.system", us);

	const bool interesting = us >= 50000 || matchesFenceEvent || matchesFrameLatency;
	if (!interesting)
		return;

	DX12DiagLog::Fields("DX12WaitableObject",
		"\"kind\":\"system_wait\",\"api\":\"%s\",\"handle\":\"%p\",\"secondaryHandle\":\"%p\","
		"\"waitMs\":%lu,\"waitResult\":\"0x%lx\",\"lastError\":%lu,\"us\":%lld,"
		"\"matchesFenceEvent\":%s,\"matchesFrameLatency\":%s,\"threadId\":%lu,\"present\":%ld",
		api ? api : "",
		primaryHandle,
		secondaryHandle,
		static_cast<unsigned long>(waitMs),
		static_cast<unsigned long>(waitResult),
		static_cast<unsigned long>(lastError),
		static_cast<long long>(us),
		matchesFenceEvent ? "true" : "false",
		matchesFrameLatency ? "true" : "false",
		static_cast<unsigned long>(GetCurrentThreadId()),
		DX12RuntimeState::GetPresentCount());
}

inline void DX12DiagProf::LogCurrentCallStack(
	const char *reason, const char *api, HANDLE handle,
	ID3D12Fence *fence, UINT64 fenceValue, LONG64 waitUs,
	DWORD waitResult, DWORD threadId, ULONG skipFrames)
{
	void *frames[24] = {};
	const USHORT captured = CaptureStackBackTrace(
		static_cast<DWORD>(skipFrames + 1),
		ARRAYSIZE(frames), frames, nullptr);
	for (USHORT i = 0; i < captured; ++i) {
		void *address = frames[i];
		HMODULE module = nullptr;
		char modulePath[MAX_PATH] = {};
		const char *moduleText = "";
		ULONG_PTR moduleBase = 0;
		if (GetModuleHandleExA(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
					GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCSTR>(address), &module) && module) {
			moduleBase = reinterpret_cast<ULONG_PTR>(module);
			if (GetModuleFileNameA(module, modulePath, ARRAYSIZE(modulePath)))
				moduleText = modulePath;
		}

		DX12DiagLog::Fields("DX12WaitStack",
			"\"reason\":\"%s\",\"api\":\"%s\",\"frame\":%u,\"address\":\"%p\","
			"\"module\":\"%s\",\"moduleBase\":\"0x%llx\",\"moduleOffset\":\"0x%llx\","
			"\"handle\":\"%p\",\"fence\":\"%p\",\"fenceValue\":%llu,"
			"\"waitUs\":%lld,\"waitResult\":\"0x%lx\",\"threadId\":%lu,\"present\":%ld",
			reason ? reason : "",
			api ? api : "",
			static_cast<unsigned>(i),
			address,
			moduleText,
			static_cast<unsigned long long>(moduleBase),
			static_cast<unsigned long long>(
				moduleBase ? reinterpret_cast<ULONG_PTR>(address) - moduleBase : 0),
			handle,
			fence,
			static_cast<unsigned long long>(fenceValue),
			static_cast<long long>(waitUs),
			static_cast<unsigned long>(waitResult),
			static_cast<unsigned long>(threadId),
			DX12RuntimeState::GetPresentCount());
	}
}

// --- merged-table helpers (writer thread) ------------------------------------

inline DX12DiagProf::Bucket *DX12DiagProf::FindMerged(
	Bucket *merged, UINT *mergedCount,
	const char *name, const char *sub, UINT kind)
{
	const UINT mask = kMergedBuckets - 1;
	const UINT slot = HashKey(name, sub, kind) & mask;
	for (UINT probe = 0; probe < kMergedBuckets; ++probe) {
		Bucket &candidate = merged[(slot + probe) & mask];
		if (candidate.name == name && candidate.sub == sub && candidate.kind == kind)
			return &candidate;
		if (!candidate.name) {
			candidate.name = name;
			candidate.sub = sub;
			candidate.kind = kind;
			++(*mergedCount);
			return &candidate;
		}
	}
	return nullptr;
}

inline void DX12DiagProf::DrainThreadTables(Bucket *merged, UINT *mergedCount)
{
	*mergedCount = 0;
	const LONG tableCount = InterlockedCompareExchange(&sThreadTableCount, 0, 0);
	const LONG tablesToScan = tableCount < static_cast<LONG>(kThreadTableCount)
		? tableCount : static_cast<LONG>(kThreadTableCount);
	for (LONG t = 0; t < tablesToScan; ++t) {
		ThreadTable &table = sThreadTables[t];
		for (UINT i = 0; i < kBucketsPerThread; ++i) {
			Bucket &src = table.buckets[i];
			if (!src.name)
				continue;
			const LONG64 calls = InterlockedExchange64(&src.calls, 0);
			const LONG64 totalUs = InterlockedExchange64(&src.totalUs, 0);
			const LONG64 maxUs = InterlockedExchange64(&src.maxUs, 0);
			const LONG64 items = InterlockedExchange64(&src.items, 0);
			if (!calls && !totalUs && !items)
				continue;
			Bucket *dst = FindMerged(merged, mergedCount, src.name, src.sub, src.kind);
			if (!dst)
				continue;
			dst->calls += calls;
			dst->totalUs += totalUs;
			if (maxUs > dst->maxUs)
				dst->maxUs = maxUs;
			dst->items += items;
		}
	}
}

inline const DX12DiagProf::Bucket *DX12DiagProf::LookupMerged(
	const Bucket *merged, UINT mergedCount, const char *name)
{
	// Linear scan with string compare: writer-thread side, robust against
	// literal-pooling differences across translation units.
	for (UINT i = 0; i < kMergedBuckets && mergedCount; ++i) {
		const Bucket &bucket = merged[i];
		if (!bucket.name)
			continue;
		if (bucket.name == name || !strcmp(bucket.name, name))
			return &bucket;
	}
	return nullptr;
}

inline LONG64 DX12DiagProf::MergedTotalUs(
	const Bucket *merged, UINT mergedCount, const char *name)
{
	const Bucket *bucket = LookupMerged(merged, mergedCount, name);
	return bucket ? bucket->totalUs : 0;
}

inline LONG64 DX12DiagProf::MergedCalls(
	const Bucket *merged, UINT mergedCount, const char *name)
{
	const Bucket *bucket = LookupMerged(merged, mergedCount, name);
	return bucket ? bucket->calls : 0;
}

inline LONG64 DX12DiagProf::MergedItems(
	const Bucket *merged, UINT mergedCount, const char *name)
{
	const Bucket *bucket = LookupMerged(merged, mergedCount, name);
	return bucket ? bucket->items : 0;
}

inline bool DX12DiagProf::NameHasPrefix(const char *name, const char *prefix)
{
	if (!name || !prefix)
		return false;
	const size_t length = strlen(prefix);
	return !strncmp(name, prefix, length);
}

inline void DX12DiagProf::AppendFmt(std::string &out, const char *fmt, ...)
{
	char buffer[4096];
	va_list args;
	va_start(args, fmt);
	const int written = vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);
	if (written > 0)
		out.append(buffer, static_cast<size_t>(written) < sizeof(buffer)
			? static_cast<size_t>(written) : sizeof(buffer) - 1);
}

inline void DX12DiagProf::ResetWindow()
{
	sWindowFrames = 0;
	sWindowFrameIntervalUs = 0;
	sWindowOutsideUs = 0;
	sWindowPresentWallUs = 0;
	sWindowFrameWallUs = 0;
	sWindowMaxFrameWallUs = 0;
	sWindowFirstFrameStartQpc = 0;
	sWindowLastFrameEndQpc = 0;
	sWindowLast = DX12DiagFrameRecord();
}

// One lightweight line per frame: the per-frame wall/interval pulse that used
// to ride inside the per-frame DX12FrameTimeline line.
inline void DX12DiagProf::EmitFramePulse(std::string &out)
{
	AppendFmt(out,
		"func=DX12FramePulse,\"present\":%ld,\"api\":\"%s\",\"hr\":\"0x%lx\","
		"\"frameIntervalUs\":%lld,\"outsidePresentUs\":%lld,\"presentWallUs\":%lld\n",
		sWindowLast.present,
		sWindowLast.api ? sWindowLast.api : "",
		static_cast<unsigned long>(sWindowLast.hr),
		static_cast<long long>(sWindowLast.frameIntervalUs),
		static_cast<long long>(sWindowLast.outsideUs),
		static_cast<long long>(sWindowLast.presentWallUs));
}

inline void DX12DiagProf::EmitFrameTimeline(
	std::string &out, const Bucket *merged, UINT mergedCount)
{
	// Present slices. Known = every "Present.*" bucket except the three
	// post-original runtime details, mirroring the legacy classification.
	LONG64 presentKnownUs = 0;
	for (UINT i = 0; i < kMergedBuckets; ++i) {
		const Bucket &bucket = merged[i];
		if (!bucket.name || !NameHasPrefix(bucket.name, "Present."))
			continue;
		if (!strcmp(bucket.name, "Present.present_maintenance") ||
			!strcmp(bucket.name, "Present.hunt_on_present") ||
			!strcmp(bucket.name, "Present.post_present_ui"))
			continue;
		presentKnownUs += bucket.totalUs;
	}

	const LONG64 presentOriginalUs =
		MergedTotalUs(merged, mergedCount, "Present.original_present") +
		MergedTotalUs(merged, mergedCount, "Present.original_present_skip");
	const LONG64 presentPostOriginalRuntimeUs =
		MergedTotalUs(merged, mergedCount, "Present.post_original_runtime") +
		MergedTotalUs(merged, mergedCount, "Present.post_original_runtime_total") +
		MergedTotalUs(merged, mergedCount, "Present.post_original_skip");
	const LONG64 presentConsumeUiActionsUs =
		MergedTotalUs(merged, mergedCount, "Present.consume_ui_actions_poll") +
		MergedTotalUs(merged, mergedCount, "Present.consume_ui_actions_apply");
	const LONG64 presentOtherUs =
		MergedTotalUs(merged, mergedCount, "Present.mod_present_overlay") +
		MergedTotalUs(merged, mergedCount, "Present.readback_frame_flush");

	// Queue slices: wall stages own both the call count and the list count.
	const LONG64 queueFlowTotalUs =
		MergedTotalUs(merged, mergedCount, "Queue.total") +
		MergedTotalUs(merged, mergedCount, "Queue.total_skip") +
		MergedTotalUs(merged, mergedCount, "Queue.total_internal_replay") +
		MergedTotalUs(merged, mergedCount, "Queue.total_no_original");
	const LONG64 queueWrapperTotalUs =
		MergedTotalUs(merged, mergedCount, "Queue.wrapper_total");
	const LONG64 queueOriginalUs =
		MergedTotalUs(merged, mergedCount, "Queue.original") +
		MergedTotalUs(merged, mergedCount, "Queue.original_skip") +
		MergedTotalUs(merged, mergedCount, "Queue.original_internal_replay");
	const LONG64 queueRegisterUs =
		MergedTotalUs(merged, mergedCount, "Queue.register_submitted") +
		MergedTotalUs(merged, mergedCount, "Queue.register_submitted_skip");
	const LONG64 queueWallCalls =
		MergedCalls(merged, mergedCount, "Queue.wrapper_total") +
		MergedCalls(merged, mergedCount, "Queue.total") +
		MergedCalls(merged, mergedCount, "Queue.total_skip") +
		MergedCalls(merged, mergedCount, "Queue.total_internal_replay") +
		MergedCalls(merged, mergedCount, "Queue.total_no_original");
	const LONG64 queueWallCommandLists =
		MergedItems(merged, mergedCount, "Queue.wrapper_total") +
		MergedItems(merged, mergedCount, "Queue.total") +
		MergedItems(merged, mergedCount, "Queue.total_skip") +
		MergedItems(merged, mergedCount, "Queue.total_internal_replay") +
		MergedItems(merged, mergedCount, "Queue.total_no_original");
	LONG64 queueStageSamples = 0;
	for (UINT i = 0; i < kMergedBuckets; ++i) {
		const Bucket &bucket = merged[i];
		if (bucket.name && NameHasPrefix(bucket.name, "Queue."))
			queueStageSamples += bucket.calls;
	}
	const LONG64 queueEstimatedTotalUs =
		queueWrapperTotalUs > 0 ? queueWrapperTotalUs : queueFlowTotalUs;

	const LONG64 frameWallUs = sWindowFrameWallUs;
	const LONG64 presentWallUs = sWindowPresentWallUs;

	AppendFmt(out,
		"func=DX12FrameTimeline,\"present\":%ld,\"api\":\"%s\",\"swapchain\":\"%p\",\"hr\":\"0x%lx\","
		"\"frames\":%ld,"
		"\"frameIntervalUs\":%lld,\"frameWallUs\":%lld,\"outsidePresentUs\":%lld,"
		"\"presentWallUs\":%lld,\"maxFrameWallUs\":%lld,"
		"\"presentSliceKnownUs\":%lld,\"presentOriginalUs\":%lld,"
		"\"presentPostOriginalRuntimeUs\":%lld,"
		"\"presentModBeginFrameUs\":%lld,\"presentProfilingFlushUs\":%lld,"
		"\"presentConsumeUiActionsUs\":%lld,\"presentPerFrameCountersUs\":%lld,"
		"\"presentCaptureBookkeepingUs\":%lld,\"presentCaptureCompleteUs\":%lld,"
		"\"presentHotPathUpdateUs\":%lld,\"presentOtherUs\":%lld,"
		"\"presentEnterCalls\":%lld,\"presentLeaveCalls\":%lld,"
		"\"queueExecuteStageSamples\":%lld,\"queueExecuteWallCalls\":%lld,"
		"\"queueExecuteWallCommandLists\":%lld,\"queueEstimatedTotalUs\":%lld,"
		"\"queueWrapperTotalUs\":%lld,\"queueFlowTotalUs\":%lld,\"queueOriginalUs\":%lld,"
		"\"queueNotifyUs\":%lld,\"queueRegisterUs\":%lld,\"queueUnwrapUs\":%lld,"
		"\"queueRebuildWorkFlagsUs\":%lld,"
		"\"queueFenceCalls\":%lld,\"queueSignalUs\":%lld,\"queueWaitUs\":%lld,"
		"\"fenceCalls\":%lld,\"fenceTotalUs\":%lld,"
		"\"eventWaitCalls\":%lld,\"eventWaitTotalUs\":%lld,"
		"\"systemWaitCalls\":%lld,\"systemWaitTotalUs\":%lld,"
		"\"workerTaskCalls\":%lld,\"workerTaskUs\":%lld,"
		"\"workerWaitCalls\":%lld,\"workerWaitUs\":%lld,"
		"\"workerIdleWaitUs\":%lld,\"workerDrainWaitUs\":%lld,"
		"\"presentWallMinusKnownSlicesUs\":%lld,"
		"\"frameDraws\":%ld,\"frameDispatches\":%ld,"
		"\"droppedFrameSummaries\":%ld\n",
		sWindowLast.present,
		sWindowLast.api ? sWindowLast.api : "",
		sWindowLast.swapChain,
		static_cast<unsigned long>(sWindowLast.hr),
		static_cast<long>(sWindowFrames),
		static_cast<long long>(sWindowFrameIntervalUs),
		static_cast<long long>(frameWallUs),
		static_cast<long long>(sWindowOutsideUs),
		static_cast<long long>(presentWallUs),
		static_cast<long long>(sWindowMaxFrameWallUs),
		static_cast<long long>(presentKnownUs),
		static_cast<long long>(presentOriginalUs),
		static_cast<long long>(presentPostOriginalRuntimeUs),
		static_cast<long long>(MergedTotalUs(merged, mergedCount, "Present.mod_begin_frame")),
		static_cast<long long>(MergedTotalUs(merged, mergedCount, "Present.profiling_flush")),
		static_cast<long long>(presentConsumeUiActionsUs),
		static_cast<long long>(MergedTotalUs(merged, mergedCount, "Present.per_frame_counters")),
		static_cast<long long>(MergedTotalUs(merged, mergedCount, "Present.capture_bookkeeping")),
		static_cast<long long>(MergedTotalUs(merged, mergedCount, "Present.capture_complete_or_begin")),
		static_cast<long long>(MergedTotalUs(merged, mergedCount, "Present.hotpath_update")),
		static_cast<long long>(presentOtherUs),
		static_cast<long long>(MergedCalls(merged, mergedCount, "Present.enter")),
		static_cast<long long>(MergedCalls(merged, mergedCount, "Present.leave")),
		static_cast<long long>(queueStageSamples),
		static_cast<long long>(queueWallCalls),
		static_cast<long long>(queueWallCommandLists),
		static_cast<long long>(queueEstimatedTotalUs),
		static_cast<long long>(queueWrapperTotalUs),
		static_cast<long long>(queueFlowTotalUs),
		static_cast<long long>(queueOriginalUs),
		static_cast<long long>(MergedTotalUs(merged, mergedCount, "Queue.mod_notify_submitted")),
		static_cast<long long>(queueRegisterUs),
		static_cast<long long>(MergedTotalUs(merged, mergedCount, "Queue.unwrap_command_lists")),
		static_cast<long long>(MergedTotalUs(merged, mergedCount, "Queue.rebuild_work_flags")),
		static_cast<long long>(MergedCalls(merged, mergedCount, "Queue.Signal") +
			MergedCalls(merged, mergedCount, "Queue.Wait")),
		static_cast<long long>(MergedTotalUs(merged, mergedCount, "Queue.Signal")),
		static_cast<long long>(MergedTotalUs(merged, mergedCount, "Queue.Wait")),
		static_cast<long long>(MergedCalls(merged, mergedCount, "Fence.op")),
		static_cast<long long>(MergedTotalUs(merged, mergedCount, "Fence.op")),
		static_cast<long long>(MergedCalls(merged, mergedCount, "Wait.event")),
		static_cast<long long>(MergedTotalUs(merged, mergedCount, "Wait.event")),
		static_cast<long long>(MergedCalls(merged, mergedCount, "Wait.system")),
		static_cast<long long>(MergedTotalUs(merged, mergedCount, "Wait.system")),
		static_cast<long long>(MergedCalls(merged, mergedCount, "Worker.task")),
		static_cast<long long>(MergedTotalUs(merged, mergedCount, "Worker.task")),
		static_cast<long long>(MergedCalls(merged, mergedCount, "Worker.wait.idle") +
			MergedCalls(merged, mergedCount, "Worker.wait.drain")),
		static_cast<long long>(MergedTotalUs(merged, mergedCount, "Worker.wait.idle") +
			MergedTotalUs(merged, mergedCount, "Worker.wait.drain")),
		static_cast<long long>(MergedTotalUs(merged, mergedCount, "Worker.wait.idle")),
		static_cast<long long>(MergedTotalUs(merged, mergedCount, "Worker.wait.drain")),
		static_cast<long long>(presentWallUs > presentKnownUs
			? presentWallUs - presentKnownUs : 0),
		DX12RuntimeState::TakeFrameDraws(),
		DX12RuntimeState::TakeFrameDispatches(),
		static_cast<long>(InterlockedExchange(&sFrameSummariesDropped, 0)));
}

inline void DX12DiagProf::EmitApiWallSection(
	std::string &out, const Bucket *merged, UINT mergedCount)
{
	const LONG64 frameWallUs = sWindowFrameWallUs;
	for (UINT i = 0; i < kMergedBuckets; ++i) {
		const Bucket &bucket = merged[i];
		if (!bucket.name || !NameHasPrefix(bucket.name, "Api."))
			continue;
		if (!bucket.calls && !bucket.totalUs)
			continue;
		AppendFmt(out,
			"func=DX12ApiWallTiming,\"present\":%ld,\"bucket\":\"%s\",\"calls\":%lld,"
			"\"totalUs\":%lld,\"maxUs\":%lld,\"frameWallUs\":%lld,\"framePctX100\":%lld\n",
			sWindowLast.present,
			bucket.name + 4,
			static_cast<long long>(bucket.calls),
			static_cast<long long>(bucket.totalUs),
			static_cast<long long>(bucket.maxUs),
			static_cast<long long>(frameWallUs),
			static_cast<long long>(
				frameWallUs > 0 ? bucket.totalUs * 10000 / frameWallUs : 0));
	}
}

inline void DX12DiagProf::EmitHotPathSection(
	std::string &out, const Bucket *merged, UINT mergedCount)
{
	for (UINT i = 0; i < kMergedBuckets; ++i) {
		const Bucket &bucket = merged[i];
		if (!bucket.name || !NameHasPrefix(bucket.name, "Stage."))
			continue;
		if (!bucket.calls && !bucket.totalUs)
			continue;
		AppendFmt(out,
			"func=DX12HotPathTiming,\"present\":%ld,\"stage\":\"%s\",\"calls\":%lld,"
			"\"totalUs\":%lld,\"avgUs\":%lld,\"maxUs\":%lld\n",
			sWindowLast.present,
			bucket.name + 6,
			static_cast<long long>(bucket.calls),
			static_cast<long long>(bucket.totalUs),
			static_cast<long long>(bucket.calls ? bucket.totalUs / bucket.calls : 0),
			static_cast<long long>(bucket.maxUs));
	}
}

inline void DX12DiagProf::EmitCreateSection(
	std::string &out, const Bucket *merged, UINT mergedCount)
{
	for (UINT i = 0; i < kMergedBuckets; ++i) {
		const Bucket &bucket = merged[i];
		if (!bucket.name || !NameHasPrefix(bucket.name, "Create."))
			continue;
		if (!bucket.calls && !bucket.totalUs)
			continue;
		// "Create.<api>.<stage>": split at the last dot for the two fields.
		const char *body = bucket.name + 7;
		const char *lastDot = nullptr;
		for (const char *cursor = body; *cursor; ++cursor) {
			if (*cursor == '.')
				lastDot = cursor;
		}
		char apiName[128];
		if (lastDot) {
			const size_t apiLength = static_cast<size_t>(lastDot - body);
			const size_t clamped = apiLength < sizeof(apiName) - 1
				? apiLength : sizeof(apiName) - 1;
			memcpy(apiName, body, clamped);
			apiName[clamped] = '\0';
		} else {
			snprintf(apiName, sizeof(apiName), "%s", body);
		}
		const char *stageName = lastDot ? lastDot + 1 : "";
		if (bucket.totalUs < 40 && bucket.maxUs < 10)
			continue;
		AppendFmt(out,
			"func=DX12CreateTiming,\"present\":%ld,\"api\":\"%s\",\"stage\":\"%s\","
			"\"calls\":%lld,\"totalUs\":%lld,\"avgUs\":%lld,\"maxUs\":%lld\n",
			sWindowLast.present,
			apiName,
			stageName,
			static_cast<long long>(bucket.calls),
			static_cast<long long>(bucket.totalUs),
			static_cast<long long>(bucket.calls ? bucket.totalUs / bucket.calls : 0),
			static_cast<long long>(bucket.maxUs));
	}
}

inline void DX12DiagProf::EmitCpuStageSection(
	std::string &out, const Bucket *merged, UINT mergedCount)
{
	const LONG64 frameWallUs = sWindowFrameWallUs;
	for (UINT i = 0; i < kMergedBuckets; ++i) {
		const Bucket &bucket = merged[i];
		if (!bucket.name || !bucket.sub)
			continue;
		if (!bucket.calls && !bucket.totalUs)
			continue;
		if (bucket.totalUs < 40 && bucket.maxUs < 10)
			continue;
		AppendFmt(out,
			"func=DX12CpuStageTiming,\"present\":%ld,\"category\":\"%s\",\"stage\":\"%s\","
			"\"kind\":%u,\"calls\":%lld,\"items\":%lld,\"totalUs\":%lld,\"maxUs\":%lld,"
			"\"frameWallUs\":%lld,\"framePctX100\":%lld\n",
			sWindowLast.present,
			bucket.name,
			bucket.sub,
			bucket.kind,
			static_cast<long long>(bucket.calls),
			static_cast<long long>(bucket.items),
			static_cast<long long>(bucket.totalUs),
			static_cast<long long>(bucket.maxUs),
			static_cast<long long>(frameWallUs),
			static_cast<long long>(
				frameWallUs > 0 ? bucket.totalUs * 10000 / frameWallUs : 0));
	}
}

inline void DX12DiagProf::EmitLockSection(
	std::string &out, const Bucket *merged, UINT mergedCount)
{
	for (UINT i = 0; i < kMergedBuckets; ++i) {
		const Bucket &bucket = merged[i];
		if (!bucket.name || !NameHasPrefix(bucket.name, "Lock."))
			continue;
		if (!bucket.calls)
			continue;
		AppendFmt(out,
			"func=DX12ModLockTiming,\"lock\":\"%s\",\"contentions\":%lld,"
			"\"waitUs\":%lld,\"maxUs\":%lld\n",
			bucket.name + 5,
			static_cast<long long>(bucket.calls),
			static_cast<long long>(bucket.totalUs),
			static_cast<long long>(bucket.maxUs));
	}
}

// Prefixed pure counters, one line per non-zero counter:
//   "Rm."     -> DX12ResidentMaintenance
//   "Desc."   -> DX12DescriptorShadowTiming
//   "Worker." is consumed by the frame timeline and skipped here.
inline void DX12DiagProf::EmitCounterSections(
	std::string &out, const Bucket *merged, UINT mergedCount)
{
	for (UINT i = 0; i < kMergedBuckets; ++i) {
		const Bucket &bucket = merged[i];
		if (!bucket.name)
			continue;
		const char *counterName = nullptr;
		const char *func = nullptr;
		if (NameHasPrefix(bucket.name, "Rm.")) {
			func = "DX12ResidentMaintenance";
			counterName = bucket.name + 3;
		} else if (NameHasPrefix(bucket.name, "Desc.")) {
			func = "DX12DescriptorShadowTiming";
			counterName = bucket.name + 5;
		} else {
			continue;
		}
		if (!bucket.calls && !bucket.items)
			continue;
		AppendFmt(out,
			"func=%s,\"present\":%ld,\"counter\":\"%s\",\"calls\":%lld,"
			"\"items\":%lld,\"totalUs\":%lld,\"maxUs\":%lld\n",
			func,
			sWindowLast.present,
			counterName,
			static_cast<long long>(bucket.calls),
			static_cast<long long>(bucket.items),
			static_cast<long long>(bucket.totalUs),
			static_cast<long long>(bucket.maxUs));
	}
}

// Everything left over (plain scopes/counters) as the classic top-N table.
inline void DX12DiagProf::EmitScopeTable(
	std::string &out, const Bucket *merged, UINT mergedCount)
{
	const Bucket *sorted[kMergedBuckets] = {};
	UINT sortedCount = 0;
	for (UINT i = 0; i < kMergedBuckets; ++i) {
		const Bucket &bucket = merged[i];
		if (!bucket.name || bucket.sub)
			continue;
		if (NameHasPrefix(bucket.name, "Api.") ||
			NameHasPrefix(bucket.name, "Stage.") ||
			NameHasPrefix(bucket.name, "Create.") ||
			NameHasPrefix(bucket.name, "Lock.") ||
			NameHasPrefix(bucket.name, "Rm.") ||
			NameHasPrefix(bucket.name, "Desc.") ||
			NameHasPrefix(bucket.name, "Present.") ||
			NameHasPrefix(bucket.name, "Queue.") ||
			NameHasPrefix(bucket.name, "Fence.") ||
			NameHasPrefix(bucket.name, "Wait.") ||
			NameHasPrefix(bucket.name, "Worker."))
			continue;
		if (!bucket.calls && !bucket.totalUs && !bucket.items)
			continue;
		sorted[sortedCount++] = &bucket;
	}
	if (!sortedCount)
		return;
	for (UINT i = 1; i < sortedCount; ++i) {
		const Bucket *key = sorted[i];
		UINT j = i;
		while (j > 0 && sorted[j - 1]->totalUs < key->totalUs) {
			sorted[j] = sorted[j - 1];
			--j;
		}
		sorted[j] = key;
	}

	AppendFmt(out,
		"func=DX12Profiling,frames=%ld\nname,calls,totalUs,avgUs,maxUs,items\n",
		static_cast<long>(sWindowFrames));
	for (UINT i = 0; i < sortedCount; ++i) {
		const Bucket &bucket = *sorted[i];
		AppendFmt(out, "%s,%lld,%lld,%lld,%lld,%lld\n",
			bucket.name,
			static_cast<long long>(bucket.calls),
			static_cast<long long>(bucket.totalUs),
			static_cast<long long>(bucket.calls ? bucket.totalUs / bucket.calls : 0),
			static_cast<long long>(bucket.maxUs),
			static_cast<long long>(bucket.items));
	}
}

inline void DX12DiagProf::EmitSummary()
{
	Bucket merged[kMergedBuckets] = {};
	UINT mergedCount = 0;
	DrainThreadTables(merged, &mergedCount);

	std::string out;
	out.reserve(65536);
	EmitFrameTimeline(out, merged, mergedCount);
	EmitApiWallSection(out, merged, mergedCount);
	EmitHotPathSection(out, merged, mergedCount);
	EmitCreateSection(out, merged, mergedCount);
	EmitCpuStageSection(out, merged, mergedCount);
	EmitLockSection(out, merged, mergedCount);
	EmitCounterSections(out, merged, mergedCount);
	EmitScopeTable(out, merged, mergedCount);

	DX12DiagLog::WriteDirect(out.data(), out.size());
	DX12DiagLog::FlushDirect();
	ResetWindow();
}

#endif // DX12_TIMING_BUILD

// ============================================================================
// Public macro surface.
//
// TIMING build: route into DX12DiagLog / DX12DiagProf.
// Any other build: every macro expands to nothing. Arguments are never
// evaluated, no objects are constructed, no preparation runs - the release
// binary is bit-for-bit free of diagnostic work.
// ============================================================================

#if DX12_TIMING_BUILD

#define DX12_LOG(func, ...)             DX12DiagLog::Fields(func, __VA_ARGS__)
#define DX12_LOG_FLUSH(func, ...)       DX12DiagLog::FieldsFlush(func, __VA_ARGS__)
#define DX12_LOG_TEXT(...)              DX12DiagLog::Text(__VA_ARGS__)
#define DX12_LOG_EXECUTE_GAP()          DX12DiagLog::LogExecuteGap()

#define DX12_PROF_SCOPE(name) \
	DX12DiagProf::Scope DX12_DIAG_JOIN(_diagScope_, __LINE__)(name)
#define DX12_PROF_SCOPE_NAMED(var, name) \
	DX12DiagProf::Scope var(name)
#define DX12_PROF_FUNCTION(counter) \
	DX12DiagProf::Scope DX12_DIAG_JOIN(_diagScope_, __LINE__)(#counter)
#define DX12_PROF_PAUSE(var)            var.Pause()
#define DX12_PROF_RESUME(var)           var.Resume()
#define DX12_PROF_FINISH(var)           var.Finish()
#define DX12_PROF_BEGIN(name)           DX12DiagProf::Begin(name)
#define DX12_PROF_END(name)             DX12DiagProf::End(name)
#define DX12_PROF_COUNT(name)           DX12DiagProf::Count(name)
#define DX12_PROF_ADD_US(name, us)      DX12DiagProf::AddUs(name, us)
#define DX12_PROF_ADD_US_ITEMS(name, us, items) \
	DX12DiagProf::AddUsItems(name, us, items)
#define DX12_PROF_ADD_ITEMS(name, items) DX12DiagProf::AddItems(name, items)
#define DX12_PROF_CPU_STAGE(category, stage, kind, items, us) \
	DX12DiagProf::AddCpuStage(category, stage, kind, items, us)
#define DX12_PROF_FRAME_DRAW()          DX12RuntimeState::IncrementFrameDraws()
#define DX12_PROF_FRAME_DISPATCH()      DX12RuntimeState::IncrementFrameDispatches()

#define DX12_PROF_STAGE_SCOPE(stage) \
	DX12DiagProf::Scope DX12_DIAG_JOIN(_diagScope_, __LINE__)("Stage." #stage)
#define DX12_PROF_STAGE_BEGIN(stage)  DX12DiagProf::Begin("Stage." #stage)
#define DX12_PROF_STAGE_END(stage)    DX12DiagProf::End("Stage." #stage)
#define DX12_PROF_API_SCOPE(api) \
	DX12DiagProf::Scope DX12_DIAG_JOIN(_diagScope_, __LINE__)("Api." #api)
#define DX12_PROF_API_SCOPE_NAMED(var, api) \
	DX12DiagProf::Scope var("Api." #api)
#define DX12_PROF_API_STAGE_SCOPE_NAMED(var, api, stage) \
	DX12DiagProf::ScopePair var("Api." #api, "Stage." #stage)
#define DX12_PROF_CREATE_SCOPE(api, stage) \
	DX12DiagProf::Scope DX12_DIAG_JOIN(_diagScope_, __LINE__)( \
		"Create." #api "." #stage)

#define DX12_PROF_FRAME_ENTER(api, swapChain) \
	DX12DiagProf::FrameEnter(api, swapChain)
#define DX12_PROF_FRAME_LEAVE(api, swapChain, hr) \
	DX12DiagProf::FrameLeave(api, swapChain, hr)

#define DX12_DIAG_QPC(var)              LARGE_INTEGER var = DX12DiagProf::Now()
#define DX12_DIAG_QPC_RESET(var)        var = DX12DiagProf::Now()
#define DX12_DIAG_ELAPSED_US(var)       DX12DiagProf::ElapsedUs(var)

#define DX12_LOCK_ACQUIRE_SHARED(lock, name) \
	DX12DiagProf::AcquireLockShared(lock, "Lock." name)
#define DX12_LOCK_ACQUIRE_EXCLUSIVE(lock, name) \
	DX12DiagProf::AcquireLockExclusive(lock, "Lock." name)

// Fence/wait probes. DX12_DIAG_FENCE_VALUE_VAR declares a sampled fence value
// that only other diag macros may reference (release declares nothing, so no
// unused-variable warnings and zero work).
#define DX12_DIAG_FENCE_VALUE_VAR(var, fence) \
	const UINT64 var = DX12DiagProf::FenceCompletedValue(fence)
#define DX12_DIAG_REGISTER_FENCE_EVENT(fence, eventHandle, value, completedBefore, completedAfter, hr) \
	DX12DiagProf::RegisterFenceEventOnCompletion( \
		fence, eventHandle, value, completedBefore, completedAfter, hr)
#define DX12_DIAG_REGISTER_FRAME_LATENCY_WAITABLE(swapChain, handle) \
	DX12DiagProf::RegisterFrameLatencyWaitableObject(swapChain, handle)

#else

#define DX12_LOG(func, ...)             do {} while (0)
#define DX12_LOG_FLUSH(func, ...)       do {} while (0)
#define DX12_LOG_TEXT(...)              do {} while (0)
#define DX12_LOG_EXECUTE_GAP()          do {} while (0)

#define DX12_PROF_SCOPE(name)           do {} while (0)
#define DX12_PROF_SCOPE_NAMED(var, name) do {} while (0)
#define DX12_PROF_FUNCTION(counter)     do {} while (0)
#define DX12_PROF_PAUSE(var)            do {} while (0)
#define DX12_PROF_RESUME(var)           do {} while (0)
#define DX12_PROF_FINISH(var)           do {} while (0)
#define DX12_PROF_BEGIN(name)           do {} while (0)
#define DX12_PROF_END(name)             do {} while (0)
#define DX12_PROF_COUNT(name)           do {} while (0)
#define DX12_PROF_ADD_US(name, us)      do {} while (0)
#define DX12_PROF_ADD_US_ITEMS(name, us, items) do {} while (0)
#define DX12_PROF_ADD_ITEMS(name, items) do {} while (0)
#define DX12_PROF_CPU_STAGE(category, stage, kind, items, us) do {} while (0)
#define DX12_PROF_FRAME_DRAW()          do {} while (0)
#define DX12_PROF_FRAME_DISPATCH()      do {} while (0)

#define DX12_PROF_STAGE_SCOPE(stage)    do {} while (0)
#define DX12_PROF_STAGE_BEGIN(stage)    do {} while (0)
#define DX12_PROF_STAGE_END(stage)      do {} while (0)
#define DX12_PROF_API_SCOPE(api)        do {} while (0)
#define DX12_PROF_API_SCOPE_NAMED(var, api) do {} while (0)
#define DX12_PROF_API_STAGE_SCOPE_NAMED(var, api, stage) do {} while (0)
#define DX12_PROF_CREATE_SCOPE(api, stage) do {} while (0)

#define DX12_PROF_FRAME_ENTER(api, swapChain) do {} while (0)
#define DX12_PROF_FRAME_LEAVE(api, swapChain, hr) do {} while (0)

#define DX12_DIAG_QPC(var)              do {} while (0)
#define DX12_DIAG_QPC_RESET(var)        do {} while (0)
#define DX12_DIAG_ELAPSED_US(var)       0

#define DX12_LOCK_ACQUIRE_SHARED(lock, name) AcquireSRWLockShared(lock)
#define DX12_LOCK_ACQUIRE_EXCLUSIVE(lock, name) AcquireSRWLockExclusive(lock)

#define DX12_DIAG_FENCE_VALUE_VAR(var, fence) do {} while (0)
#define DX12_DIAG_REGISTER_FENCE_EVENT(fence, eventHandle, value, completedBefore, completedAfter, hr) \
	do {} while (0)
#define DX12_DIAG_REGISTER_FRAME_LATENCY_WAITABLE(swapChain, handle) do {} while (0)

#endif // DX12_TIMING_BUILD

// Frame-boundary breadcrumb for the freeze watchdog. Kept outside the macro
// surface because it is a plain function used across many TUs; in non-TIMING
// builds it inlines to nothing (the watchdog itself is TIMING-only).
#if DX12_TIMING_BUILD
void DX12MarkStage(const char *stage);
#else
inline void DX12MarkStage(const char *) {}
#endif
