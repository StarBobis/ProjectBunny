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
#include "DX12CrashReport.h"
#include "DX12ProfilingText.h"

#include <algorithm>
#include <atomic>
#include <cstring>

#if defined(DX12_RELEASE_TIMING)
#define DX12_TIMING_BUILD 1
#else
#define DX12_TIMING_BUILD 0
#endif

// The named-bucket profiler is compiled into every build, because the on-screen
// HUD is a Release feature too: the point of it is that a player can screenshot
// the numbers from the build they actually play.
//
// The two builds differ in *when* it samples, not in whether it exists:
//   - TIMING builds sample unconditionally. Their purpose is the diagnostic
//     log, so DX12DiagProf::Enabled() folds to a compile-time true and the
//     recording path compiles to exactly what it always did.
//   - Other builds sample only while the HUD is switched on, so a Release
//     binary that never opens the HUD pays one predicted-not-taken branch per
//     instrumentation site and nothing else.
//
// That gate is a runtime one on purpose. A preprocessor switch would mean the
// HUD-off path was never exercised in the build that ships it.
//
// Log *output* stays TIMING-only: the writer thread exists in both builds
// because it is also the profiler's aggregation pump, but only a TIMING build
// ever opens a log file.
#define DX12_DIAG_JOIN_INNER(a, b) a##b
#define DX12_DIAG_JOIN(a, b) DX12_DIAG_JOIN_INNER(a, b)

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
	// Creates the aggregation/writer thread on first use. A TIMING build gets
	// it from Open(); any other build only starts it when the profiler is
	// switched on, so a Release binary that never opens the HUD runs no extra
	// thread at all.
	static bool EnsurePumpThread();
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

	// Freeze watchdog thread-stack snapshots skip the writer: a suspended
	// writer cannot drain the queue the snapshot itself is logged into.
	static DWORD WriterThreadId()
	{
		return sThread ? GetThreadId(sThread) : 0;
	}

	// Crash path only (see DX12CrashReport.h). The writer owns the log's FILE*
	// together with its own file position, so a crash report appended through a
	// second handle is overwritten by the writer's next buffered flush - which
	// is exactly how the 2026-09-19 13:26 report lost its exception record,
	// registers and stack walk. Stops the writer, waits a bounded moment for it
	// to finish its current drain, and reports whether the file is now free.
	// A writer that is stuck is not waited for: no report is worse than a report
	// that shares a file with a writer nobody can stop.
	static bool QuiesceForCrash()
	{
		if (!sLog || !sThread)
			return true;
		InterlockedExchange(&sStopRequested, 1);
		if (sEvent)
			SetEvent(sEvent);
		if (WaitForSingleObject(sThread, kCrashQuiesceWaitMs) != WAIT_OBJECT_0)
			return false;
		FlushDirect();
		return true;
	}

private:
	static DWORD WINAPI ThreadProc(void *);
	static void DrainQueueToFile();
	static void EnqueueLine(const char *line);
	static bool PublishTlsBuffer();
	static std::string &TlsLineBuffer();
	static void NormalizeFields(const char *input, char *output, size_t outputSize);
	static void WriteFormatted(const char *func, const char *extra, bool flush);
	static void MakeTimePlain(char *buffer, size_t bufferSize);
	static void RegisterInternalEventHandle(HANDLE handle);

	static constexpr size_t kTlsFlushBytes = 8192;
	static constexpr size_t kMaxQueuedLines = 8192;
	// Upper bound on how long a crash report waits for the writer. The writer
	// polls on a 100ms timeout, so a healthy one leaves well inside this.
	static constexpr DWORD kCrashQuiesceWaitMs = 500;

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
	// Sampling gate. A TIMING build samples unconditionally - its purpose is
	// the log - so this folds to a compile-time true and every guarded call
	// site compiles to exactly what it did before. Any other build samples
	// only while the on-screen HUD is switched on, so the gate *is* the HUD
	// mode: one L1-resident relaxed load, and the branch around the recording
	// work is predicted-not-taken in a HUD-less session.
	static bool Enabled()
	{
#if DX12_TIMING_BUILD
		return true;
#else
		return DX12ProfilingHud::IsEnabled();
#endif
	}

	struct Bucket
	{
		const char *name = nullptr;
		const char *sub = nullptr;
		LONG64 calls = 0;
		LONG64 totalUs = 0;
		LONG64 maxUs = 0;
		LONG64 items = 0;
		// The same samples, restricted to those taken while this bucket was the
		// outermost instrumented scope on its thread. totalUs counts a nested
		// scope once per enclosing scope as well (a draw is Api.X, Stage.DrawHook,
		// Stage.DrawOverride and Stage.DrawOvIaStateNegCache at the same time),
		// which is why summing totalUs over every bucket is an upper bound that
		// can exceed the frame. rootUs counts each sample exactly once, so the
		// HUD's OUR TOTAL is a sum over rootUs instead. Nothing about how a
		// sample is recorded changed: this is a second accumulator, and the
		// bucket grows from 56 to exactly 64 bytes because of the padding.
		LONG64 rootUs = 0;
		UINT kind = 0;
	};

	static constexpr UINT kThreadTableCount = 64;
	// Sized from the instrumented source, not guessed: this build registers
	// ~460 distinct bucket names (~835 call sites). At 256 slots the per-thread
	// tables were over-subscribed, so FindBucket ran out of probe slots and
	// dropped counters silently - a newly added counter simply never appeared
	// in the log, and the surviving names paid long probe chains (which is how
	// a one-atomic-load stage got reported as ~2us/call). 1024 keeps the load
	// factor near 0.45, i.e. 1-2 probes per lookup.
	static constexpr UINT kBucketsPerThread = 1024; // power of two
	// The merged table is a stack array, so its size trades stack for load
	// factor. It must stay comfortably above the live bucket count: at 512 the
	// ~460 registered names gave a 0.9 load factor, and FindMerged returns
	// nullptr once the table is full, which silently drops counters from the
	// summary (a missing row reads as "this code never ran"). 2048 restores
	// the ~0.22 the 512-slot table had when only ~110 names existed.
	static constexpr UINT kMergedBuckets = 2048;   // power of two
	static constexpr UINT kScopeStackDepth = 32;
	// Depth of the RAII-scope bracket counter (see EnterScope). Only RAII
	// scopes consume it - Begin/End pairs bracket through the positional stack
	// itself - so 32 is far above anything a call tree reaches. It is a clamp,
	// not an assertion: an unbalanced pair may only make the counter drift
	// towards zero, which over-counts the root total instead of silently
	// reporting that our code is free.
	static constexpr UINT kMaxNestDepth = 32;
	// Default summary interval. The number itself now lives next to the rest of
	// the window policy in DX12ProfilingHud::kWindowFrames: the window is
	// shared between the logger and the HUD - one window produces both sinks'
	// numbers - so it is defined where both can see it and there is no second
	// copy of it here.
	static constexpr LONG kSummaryIntervalFrames = DX12ProfilingHud::kWindowFrames;

	struct alignas(64) ThreadTable
	{
		DWORD threadId = 0;
		Bucket buckets[kBucketsPerThread];
	};

	struct ScopeStackEntry
	{
		Bucket *bucket = nullptr;
		LONGLONG startQpc = 0;
		// Whether this Begin opened at the top of the instrumented call tree.
		// Captured at Begin time because the positional stack cannot tell after
		// the fact, and End() has no name to consult.
		bool root = false;
	};

	// Where a scope instance sits in the instrumented call tree, taken once at
	// construction. It answers the one question the report needs - "is this
	// sample already inside another one?" - and doubles as the "was a bracket
	// taken" state the destructor needs to release it.
	enum class Bracket : unsigned char
	{
		None,   // not entered at all (profiling off, or the thread table is full)
		Root,   // opened at the top of the tree: this sample is nobody's child
		Nested, // opened inside another instrumented scope
	};

	class Scope
	{
	public:
		explicit Scope(const char *name)
			: mBucket(Enabled() ? FindBucket(name, nullptr, 0) : nullptr)
		{
			if (!mBucket)
				return;
			// Every instrumented wrapper entry is also the crash report's
			// "last thing our code was doing" breadcrumb. A TIMING build pays
			// one relaxed atomic; everywhere else this is an empty inline.
			DX12CrashBreadcrumb(name);
			++mBucket->calls;
			// The bracket is taken before the clock is read and it outlives
			// Pause/Resume/Finish: it describes the lexical region, not the
			// accumulated interval. A paused scope is still open, so work done
			// inside it while it is paused is still its child's work.
			mBracket = EnterScope();
			mStartQpc = QpcNow();
			mRunning = true;
		}

		// Destructor releases the bracket even if the clock was already
		// finished or paused, so the counter can only ever be balanced.
		~Scope()
		{
			Finish();
			LeaveScope(mBracket);
		}

		void Pause()
		{
			if (!mRunning)
				return;
			AccumulateUs(mBucket, mStartQpc, QpcNow(), mBracket);
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
			AccumulateUs(mBucket, mStartQpc, QpcNow(), mBracket);
			mRunning = false;
		}

		Scope(const Scope &) = delete;
		Scope &operator=(const Scope &) = delete;

	private:
		Bucket *mBucket = nullptr;
		LONGLONG mStartQpc = 0;
		Bracket mBracket = Bracket::None;
		bool mRunning = false;
	};

	// One QPC pair feeding two buckets (the former ApiStage scope kind).
	class ScopePair
	{
	public:
		ScopePair(const char *nameA, const char *nameB)
			: mBucketA(Enabled() ? FindBucket(nameA, nullptr, 0) : nullptr),
			  mBucketB(Enabled() ? FindBucket(nameB, nullptr, 0) : nullptr)
		{
			if (!mBucketA && !mBucketB)
				return;
			// The pair's first name is the API entry, which is the one the crash
			// report wants (the second is the stage it is attributed to).
			DX12CrashBreadcrumb(nameA);
			if (mBucketA)
				++mBucketA->calls;
			if (mBucketB)
				++mBucketB->calls;
			mBracket = EnterScope();
			mStartQpc = QpcNow();
		}

		~ScopePair()
		{
			LeaveScope(mBracket);
			if (!mBucketA && !mBucketB)
				return;
			const LONGLONG endQpc = QpcNow();
			// One interval under two names. Only the first one is the canonical
			// view of it, so only it contributes to the de-duplicated root
			// total; the second would otherwise add the same microseconds to
			// OUR TOTAL a second time.
			AccumulateUs(mBucketA, mStartQpc, endQpc, mBracket);
			AccumulateUs(mBucketB, mStartQpc, endQpc, Bracket::Nested);
		}

		ScopePair(const ScopePair &) = delete;
		ScopePair &operator=(const ScopePair &) = delete;

	private:
		Bucket *mBucketA = nullptr;
		Bucket *mBucketB = nullptr;
		LONGLONG mStartQpc = 0;
		Bracket mBracket = Bracket::None;
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
		// A zero start is the "clock was never read" marker DX12_DIAG_QPC
		// leaves behind when the profiler is off; reporting 0 keeps the
		// disabled path from inventing a bogus delta against the epoch.
		if (begin.QuadPart == 0)
			return 0;
		return DeltaUs(begin.QuadPart, QpcNow());
	}

	// --- recording primitives --------------------------------------------
	static void Count(const char *name)
	{
		Bucket *bucket = FindBucket(name, nullptr, 0);
		if (bucket)
			++bucket->calls;
	}

	// Point measurements (AddUs and friends) are bracketed too: they are timed
	// at whatever depth the caller happens to be at, so one taken inside a
	// scope is that scope's child and must not be added to OUR TOTAL on its
	// own. One taken with nothing open - Present's own bookkeeping, the
	// collector flush - is a root sample and counts.
	static void AddUs(const char *name, LONG64 us)
	{
		AddUsToBucket(FindBucket(name, nullptr, 0), us, RootScope());
	}

	static void AddUsItems(const char *name, LONG64 us, LONG64 items)
	{
		// One lookup for both counters. Resolving the bucket twice (the old
		// shape) re-ran the hash and the probe chain on a path whose callers
		// are per-submit hot sites.
		Bucket *bucket = FindBucket(name, nullptr, 0);
		AddUsToBucket(bucket, us, RootScope());
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

	// Adds to the call counter without one bucket lookup per event. Callers that
	// already accumulate over a batch publish the batch once; a per-event
	// counter on a path that runs once per copied descriptor made the counter
	// itself a measurable part of what it measures.
	static void AddCalls(const char *name, LONG64 calls)
	{
		if (!calls)
			return;
		Bucket *bucket = FindBucket(name, nullptr, 0);
		if (bucket)
			bucket->calls += calls;
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
		if (RootScope())
			bucket->rootUs += us;
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
			// "Root" is simply "nothing was open when this Begin ran": the
			// RAII counter for any Scope/ScopePair the caller is inside, and
			// this stack's own depth (still zero before the push below).
			const bool root = RootScope();
			ScopeStackEntry &entry = sScopeStack[sScopeDepth++];
			entry.bucket = bucket;
			entry.startQpc = QpcNow();
			entry.root = root;
		}
	}

	static void End(const char *name)
	{
		(void)name; // pairing is positional; mismatched names only blur diagnostics
		if (sScopeDepth == 0)
			return;
		const ScopeStackEntry entry = sScopeStack[--sScopeDepth];
		AccumulateUs(entry.bucket, entry.startQpc, QpcNow(),
			entry.root ? Bracket::Root : Bracket::Nested);
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
		if (!Enabled())
			return;
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
		if (!Enabled())
			return;
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
		// The per-frame diagnostic plumbing is measured so the timeline's
		// presentPerFrameCountersUs column reports something real: it used to
		// look up a bucket nothing ever recorded, so it was always zero.
		const LONGLONG plumbingStart = QpcNow();
		PushFrameRecord(record);
		// Publish this thread's log lines before the writer drains the ring,
		// so the frame's own lines precede its summary block in the file.
		// Non-TIMING builds never queue lines, and Flush() only wakes the
		// writer when something was actually published.
		DX12DiagLog::Flush();
		AddUs("Present.frame_counters", DeltaUs(plumbingStart, QpcNow()));
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

	static void AccumulateUs(
		Bucket *bucket, LONGLONG startQpc, LONGLONG endQpc, Bracket bracket)
	{
		if (!bucket || endQpc <= startQpc)
			return;
		const LONG64 us = DeltaUs(startQpc, endQpc);
		bucket->totalUs += us;
		if (bracket == Bracket::Root)
			bucket->rootUs += us;
		if (us > bucket->maxUs)
			bucket->maxUs = us;
	}

	// Shared tail of AddUs/AddUsItems so a caller that needs both counters
	// pays for one bucket lookup instead of two.
	static void AddUsToBucket(Bucket *bucket, LONG64 us, bool root)
	{
		if (!bucket)
			return;
		if (us < 0)
			us = 0;
		++bucket->calls;
		bucket->totalUs += us;
		if (root)
			bucket->rootUs += us;
		if (us > bucket->maxUs)
			bucket->maxUs = us;
	}

	// --- scope nesting -------------------------------------------------------
	// Every scope kind brackets its region with EnterScope/LeaveScope, so a
	// sample knows whether it was taken at the top of the instrumented tree or
	// inside somebody else's region. RAII scopes use the counter below;
	// Begin/End pairs bracket through the positional stack they already keep,
	// which is why they cannot leak it.
	static bool RootScope()
	{
		return sScopeDepth == 0 && sNestDepth == 0;
	}

	static Bracket EnterScope()
	{
		const Bracket bracket = RootScope() ? Bracket::Root : Bracket::Nested;
		if (sNestDepth < kMaxNestDepth)
			++sNestDepth;
		return bracket;
	}

	static void LeaveScope(Bracket bracket)
	{
		if (bracket == Bracket::None)
			return;
		if (sNestDepth > 0)
			--sNestDepth;
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
		if (!force && sWindowFrames > 0) {
			// The window is counted in frames, not milliseconds: only a fixed
			// frame count makes two reports comparable and keeps `max` (a
			// window maximum) stable. A wall-clock window held 15 frames at
			// 30fps and 72 at 144fps, so the same bucket reported a different
			// peak in every session and the HUD's whole spike column moved
			// with the frame rate instead of with the code.
			//
			// The time cap is a liveness floor only - a session too slow to
			// reach the target still refreshes - and it is floored by
			// kWindowMinFrames so it can never shrink the window back to the
			// 15-frame sample this replaced. Its summed present-to-present
			// interval is the window's own elapsed time.
			const bool framesReached =
				sWindowFrames >= DX12ProfilingHud::kWindowFrames;
			const bool fallbackReached =
				sWindowFrames >= DX12ProfilingHud::kWindowMinFrames &&
				sWindowFrameIntervalUs >= DX12ProfilingHud::WindowFallbackUs();
			if (!framesReached && !fallbackReached)
				return;
		}
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
		// The present-wall max is display-only (the HUD's frame block), which is
		// the same kind of window aggregate as the frame-wall max below.
		if (record.presentWallUs > sWindowMaxPresentWallUs)
			sWindowMaxPresentWallUs = record.presentWallUs;
		const LONG64 frameWallUs = record.outsideUs + record.presentWallUs;
		sWindowFrameWallUs += frameWallUs;
		if (frameWallUs > sWindowMaxFrameWallUs)
			sWindowMaxFrameWallUs = frameWallUs;
		sWindowLast = record;
	}

	// --- summary emission (writer thread only) --------------------------------
	static void EmitSummary();
	static void DrainThreadTables(Bucket *merged, UINT *mergedCount);
	static DX12ProfilingWindow BuildWindowReport(
		const Bucket *merged, UINT mergedCount);
	static UINT BuildRankedRows(
		DX12ProfilingRow *rows, UINT capacity,
		const Bucket *merged, UINT mergedCount);
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
	static void EmitFrameTimeline(std::string &out, const DX12ProfilingWindow &window);
	static void EmitApiWallSection(std::string &out, const Bucket *merged, UINT mergedCount);
	static void EmitHotPathSection(std::string &out, const Bucket *merged, UINT mergedCount);
	static void EmitCreateSection(std::string &out, const Bucket *merged, UINT mergedCount);
	static void EmitCpuStageSection(std::string &out, const Bucket *merged, UINT mergedCount);
	static void EmitLockSection(std::string &out, const Bucket *merged, UINT mergedCount);
	static void EmitCounterSections(std::string &out, const Bucket *merged, UINT mergedCount);
	static void ResetWindow();

	inline static ThreadTable sThreadTables[kThreadTableCount] = {};
	inline static volatile LONG sThreadTableCount = 0;
	// Set once when a thread table runs out of probe slots, so the drop is
	// visible in the log instead of looking like code that never ran.
	inline static volatile LONG sTableFull = 0;
	// Set once when the merged table runs out of probe slots. Same reasoning as
	// sTableFull, and the more dangerous of the two: a dropped merge makes a
	// whole row vanish from the summary rather than one sample from a row.
	inline static volatile LONG sMergedFull = 0;
	inline static thread_local ScopeStackEntry sScopeStack[kScopeStackDepth] = {};
	inline static thread_local UINT sScopeDepth = 0;
	// Open RAII scopes on this thread. Separate from sScopeDepth because the
	// two brackets nest through different mechanisms and both have to be zero
	// for a sample to count as a root sample.
	inline static thread_local UINT sNestDepth = 0;

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
	inline static LONG64 sWindowMaxPresentWallUs = 0;
	inline static LONG64 sWindowFrameWallUs = 0;
	inline static LONG64 sWindowMaxFrameWallUs = 0;
	inline static DX12DiagFrameRecord sWindowLast;

	// Wait-probe registrations (written rarely, read on every hooked wait).
	inline static volatile LONG64 sLastFrameLatencyHandle = 0;
	inline static volatile LONG64 sLastFenceEventHandle = 0;
	inline static volatile LONG64 sLastFenceEventFence = 0;
	inline static volatile LONG64 sLastFenceEventValue = 0;

	static LONG64 ReadLong64(volatile LONG64 *value) { return *value; }
	static LONG ReadLong(volatile LONG *value) { return *value; }
};

// Mode switching (F11) and the aggregation pump start it implies. These entry
// points live here because this is the only place that sees the HUD state and
// the writer thread together: the HUD header cannot include the profiler, and
// the profiler must not start a thread on its own.
//
// A non-TIMING build creates no writer thread at startup, so the first enable
// has to start one. Doing it here - rather than lazily on the render thread's
// next frame - keeps the "no extra thread unless profiling is wanted" property
// of a Release binary while making the switch take effect immediately.
inline void DX12ProfilingStartPumpIfEnabled()
{
#if !DX12_TIMING_BUILD
	if (DX12ProfilingHud::IsEnabled())
		DX12DiagLog::EnsurePumpThread();
#endif
}

inline void DX12ProfilingCycleMode()
{
	DX12ProfilingHud::CycleMode();
	DX12ProfilingStartPumpIfEnabled();
}

// ============================================================================
// DX12DiagLog implementation
// ============================================================================

inline bool DX12DiagLog::Open()
{
#if !DX12_TIMING_BUILD
	// Profiling without the log: the pump thread is started on demand by
	// DX12DiagProf::SetEnabled, and sLog stays null so every write path is a
	// no-op. A Release binary that never enables the HUD therefore opens no
	// file and creates no thread.
	return true;
#else
	wchar_t path[MAX_PATH];
	if (!GetModuleFileNameW(DX12RuntimeState::GetModule(), path, MAX_PATH))
		return false;

	PathRemoveFileSpecW(path);
	// Keep a timestamped copy of every run so normal vs broken sessions can
	// be compared: the log is overwritten on each launch otherwise.
	wchar_t logPath[MAX_PATH];
	wchar_t historyPath[MAX_PATH];
	wsprintfW(logPath, L"%s\\d3d12_log.txt", path);
	swprintf_s(historyPath, L"%s\\d3d12_log_%llu.txt", path,
		static_cast<unsigned long long>(GetTickCount64()));
	if (PathFileExistsW(logPath))
		CopyFileW(logPath, historyPath, FALSE);
	PathAppendW(path, L"d3d12_log.txt");
	sLog = _wfsopen(path, L"w", _SH_DENYNO);
	if (!sLog)
		return false;

	return EnsurePumpThread();
#endif
}

inline bool DX12DiagLog::EnsurePumpThread()
{
	if (sThread)
		return true;

	InterlockedExchange(&sStopRequested, 0);
	if (!sEvent) {
		sEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		if (!sEvent) {
			if (sLog) {
				fclose(sLog);
				sLog = nullptr;
			}
			return false;
		}
		RegisterInternalEventHandle(sEvent);
	}
	sThread = CreateThread(nullptr, 0, ThreadProc, nullptr, 0, nullptr);
	if (!sThread) {
		CloseHandle(sEvent);
		sEvent = nullptr;
		RegisterInternalEventHandle(nullptr);
		if (sLog) {
			fclose(sLog);
			sLog = nullptr;
		}
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
	// Only wake the writer when there is something to write. FrameLeave calls
	// this on every present, and an unconditional SetEvent put a kernel
	// transition on the present path even in sessions that log nothing.
	if (PublishTlsBuffer())
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

inline bool DX12DiagLog::PublishTlsBuffer()
{
	std::string &buffer = TlsLineBuffer();
	if (buffer.empty())
		return false;
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
	return wasEmpty;
}

inline void DX12DiagLog::EnqueueLine(const char *line)
{
	if (!line || !line[0])
		return;
	std::string &buffer = TlsLineBuffer();
	buffer += line;
	// Before the first Present, a thread may emit only a few lines and then
	// fault or exit. The writer cannot drain another thread's TLS buffer.
	// Publish startup lines immediately; keep normal frame-time batching.
	if (buffer.size() >= kTlsFlushBytes || DX12RuntimeState::GetPresentCount() == 0)
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
		// Callers also log literal Windows paths, not just escaped JSON.
		// A backslash before an ordinary path character must survive.
		if (quoted && ch == '\\' && (cursor[1] == '\\' || cursor[1] == '"')) {
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
	// Full: every remaining sample is dropped from this window. Report it once
	// so a missing row can never be mistaken for "the code never ran".
	if (InterlockedCompareExchange(&sMergedFull, 1, 0) == 0)
		DX12DiagLog::FieldsFlush("DX12Profiling",
			"event=merged_table_full capacity=%u dropped=%s",
			kMergedBuckets, name);
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
			const LONG64 rootUs = InterlockedExchange64(&src.rootUs, 0);
			const LONG64 maxUs = InterlockedExchange64(&src.maxUs, 0);
			const LONG64 items = InterlockedExchange64(&src.items, 0);
			if (!calls && !totalUs && !items)
				continue;
			Bucket *dst = FindMerged(merged, mergedCount, src.name, src.sub, src.kind);
			if (!dst)
				continue;
			dst->calls += calls;
			dst->totalUs += totalUs;
			dst->rootUs += rootUs;
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
	sWindowMaxPresentWallUs = 0;
	sWindowFrameWallUs = 0;
	sWindowMaxFrameWallUs = 0;
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

// Every derived number for one aggregation window, computed once and shared by
// both sinks. This was the body of the timeline emitter: extracting it is what
// keeps the on-screen report and the log line from ever disagreeing, and it is
// also what makes the counters with reset side effects (the per-frame
// draw/dispatch shards and the dropped-summary counter) safe to read when only
// one of the two sinks is active - they are consumed exactly once, here.
inline DX12ProfilingWindow DX12DiagProf::BuildWindowReport(
	const Bucket *merged, UINT mergedCount)
{
	DX12ProfilingWindow w;
	w.api = sWindowLast.api;
	w.swapChain = sWindowLast.swapChain;
	w.hr = sWindowLast.hr;
	w.frames = sWindowFrames;
	w.present = sWindowLast.present;
	w.frameDraws = DX12RuntimeState::TakeFrameDraws();
	w.frameDispatches = DX12RuntimeState::TakeFrameDispatches();
	w.droppedFrameSummaries = InterlockedExchange(&sFrameSummariesDropped, 0);

	w.frameIntervalUs = sWindowFrameIntervalUs;
	w.frameWallUs = sWindowFrameWallUs;
	w.outsidePresentUs = sWindowOutsideUs;
	w.presentWallUs = sWindowPresentWallUs;
	w.maxFrameWallUs = sWindowMaxFrameWallUs;
	w.maxPresentWallUs = sWindowMaxPresentWallUs;
	// Which work the hooks were asked to do, straight from the published gate
	// word: the header line has to state it, because every number below it
	// means something different depending on it.
	w.hookWorkFlags = DX12ProfilingHud::HookWorkFlags();

	// Present slices. Known = every "Present.*" bucket except the three
	// post-original runtime details, mirroring the legacy classification.
	for (UINT i = 0; i < kMergedBuckets; ++i) {
		const Bucket &bucket = merged[i];
		if (!bucket.name || !NameHasPrefix(bucket.name, "Present."))
			continue;
		if (!strcmp(bucket.name, "Present.present_maintenance") ||
			!strcmp(bucket.name, "Present.hunt_on_present") ||
			!strcmp(bucket.name, "Present.post_present_ui"))
			continue;
		w.presentKnownUs += bucket.totalUs;
	}

	w.presentOriginalUs =
		MergedTotalUs(merged, mergedCount, "Present.original_present") +
		MergedTotalUs(merged, mergedCount, "Present.original_present_skip");
	w.presentPostOriginalRuntimeUs =
		MergedTotalUs(merged, mergedCount, "Present.post_original_runtime") +
		MergedTotalUs(merged, mergedCount, "Present.post_original_runtime_total") +
		MergedTotalUs(merged, mergedCount, "Present.post_original_skip");
	w.presentConsumeUiActionsUs =
		MergedTotalUs(merged, mergedCount, "Present.consume_ui_actions_poll") +
		MergedTotalUs(merged, mergedCount, "Present.consume_ui_actions_apply");
	w.presentOtherUs =
		MergedTotalUs(merged, mergedCount, "Present.mod_present_overlay") +
		MergedTotalUs(merged, mergedCount, "Present.readback_frame_flush");
	w.presentModBeginFrameUs =
		MergedTotalUs(merged, mergedCount, "Present.mod_begin_frame");
	w.presentProfilingFlushUs =
		MergedTotalUs(merged, mergedCount, "Present.profiling_and_hotpath_flush");
	w.presentFrameCountersUs =
		MergedTotalUs(merged, mergedCount, "Present.frame_counters");
	w.presentCaptureBookkeepingUs =
		MergedTotalUs(merged, mergedCount, "Present.capture_bookkeeping");
	w.presentCaptureCompleteUs =
		MergedTotalUs(merged, mergedCount, "Present.capture_complete_or_begin");
	w.presentHotPathUpdateUs =
		MergedTotalUs(merged, mergedCount, "Present.hotpath_update");
	// The overlay text record is timed on its own inside the merged compose, so
	// the HUD's cost stays auditable now that every overlay source shares one
	// command list. "overlay_text" and "hunt_overlay_text" are kept so a
	// window that straddles the rename reports something instead of zero.
	w.presentOverlayTextUs =
		MergedTotalUs(merged, mergedCount, "Present.overlay_text_record") +
		MergedTotalUs(merged, mergedCount, "Present.overlay_text") +
		MergedTotalUs(merged, mergedCount, "Present.hunt_overlay_text") +
		MergedTotalUs(merged, mergedCount, "Present.prof_hud_skip");
	w.presentEnterCalls = MergedCalls(merged, mergedCount, "Present.enter");
	w.presentLeaveCalls = MergedCalls(merged, mergedCount, "Present.leave");
	w.presentWallMinusKnownUs = w.presentWallUs > w.presentKnownUs
		? w.presentWallUs - w.presentKnownUs : 0;

	// Our two totals, summed here rather than in the formatter so both sinks
	// print the same numbers from the same pass. The row rules are the time
	// table's: no sub-keyed CPU-stage rows (they are a separate section, not
	// table rows), no zero-time rows, and the game's own Present/Wait excluded.
	// See DX12ProfilingWindow for what root and all mean.
	for (UINT i = 0; i < kMergedBuckets; ++i) {
		const Bucket &bucket = merged[i];
		if (!bucket.name || bucket.sub || bucket.totalUs <= 0)
			continue;
		if (DX12ProfilingClassify::ObservedOnly(bucket.name))
			continue;
		w.ourAllUs += bucket.totalUs;
		++w.ourAllRows;
		if (bucket.rootUs > 0 &&
			!DX12ProfilingClassify::WaitOnly(bucket.name)) {
			w.ourRootUs += bucket.rootUs;
			++w.ourRootRows;
		}
	}

	// Queue slices: wall stages own both the call count and the list count.
	w.queueFlowTotalUs =
		MergedTotalUs(merged, mergedCount, "Queue.total") +
		MergedTotalUs(merged, mergedCount, "Queue.total_skip") +
		MergedTotalUs(merged, mergedCount, "Queue.total_internal_replay") +
		MergedTotalUs(merged, mergedCount, "Queue.total_no_original");
	w.queueWrapperTotalUs =
		MergedTotalUs(merged, mergedCount, "Queue.wrapper_total");
	w.queueOriginalUs =
		MergedTotalUs(merged, mergedCount, "Queue.original") +
		MergedTotalUs(merged, mergedCount, "Queue.original_skip") +
		MergedTotalUs(merged, mergedCount, "Queue.original_internal_replay");
	w.queueRegisterUs =
		MergedTotalUs(merged, mergedCount, "Queue.register_submitted") +
		MergedTotalUs(merged, mergedCount, "Queue.register_submitted_skip");
	w.queueWallCalls =
		MergedCalls(merged, mergedCount, "Queue.wrapper_total") +
		MergedCalls(merged, mergedCount, "Queue.total") +
		MergedCalls(merged, mergedCount, "Queue.total_skip") +
		MergedCalls(merged, mergedCount, "Queue.total_internal_replay") +
		MergedCalls(merged, mergedCount, "Queue.total_no_original");
	w.queueWallCommandLists =
		MergedItems(merged, mergedCount, "Queue.wrapper_total") +
		MergedItems(merged, mergedCount, "Queue.total") +
		MergedItems(merged, mergedCount, "Queue.total_skip") +
		MergedItems(merged, mergedCount, "Queue.total_internal_replay") +
		MergedItems(merged, mergedCount, "Queue.total_no_original");
	for (UINT i = 0; i < kMergedBuckets; ++i) {
		const Bucket &bucket = merged[i];
		if (bucket.name && NameHasPrefix(bucket.name, "Queue."))
			w.queueExecuteStageSamples += bucket.calls;
	}
	w.queueEstimatedTotalUs =
		w.queueWrapperTotalUs > 0 ? w.queueWrapperTotalUs : w.queueFlowTotalUs;
	w.queueNotifyUs = MergedTotalUs(merged, mergedCount, "Queue.mod_notify_submitted");
	w.queueUnwrapUs = MergedTotalUs(merged, mergedCount, "Queue.unwrap_command_lists");
	w.queueRebuildWorkFlagsUs = MergedTotalUs(merged, mergedCount, "Queue.rebuild_work_flags");
	w.queueFenceCalls = MergedCalls(merged, mergedCount, "Queue.Signal") +
		MergedCalls(merged, mergedCount, "Queue.Wait");
	w.queueSignalUs = MergedTotalUs(merged, mergedCount, "Queue.Signal");
	w.queueWaitUs = MergedTotalUs(merged, mergedCount, "Queue.Wait");

	w.fenceCalls = MergedCalls(merged, mergedCount, "Fence.op");
	w.fenceTotalUs = MergedTotalUs(merged, mergedCount, "Fence.op");
	w.eventWaitCalls = MergedCalls(merged, mergedCount, "Wait.event");
	w.eventWaitTotalUs = MergedTotalUs(merged, mergedCount, "Wait.event");
	w.systemWaitCalls = MergedCalls(merged, mergedCount, "Wait.system");
	w.systemWaitTotalUs = MergedTotalUs(merged, mergedCount, "Wait.system");

	w.workerTaskCalls = MergedCalls(merged, mergedCount, "Worker.task");
	w.workerTaskUs = MergedTotalUs(merged, mergedCount, "Worker.task");
	w.workerWaitCalls =
		MergedCalls(merged, mergedCount, "Worker.wait.idle") +
		MergedCalls(merged, mergedCount, "Worker.wait.drain");
	w.workerWaitUs =
		MergedTotalUs(merged, mergedCount, "Worker.wait.idle") +
		MergedTotalUs(merged, mergedCount, "Worker.wait.drain");
	w.workerIdleWaitUs = MergedTotalUs(merged, mergedCount, "Worker.wait.idle");
	w.workerDrainWaitUs = MergedTotalUs(merged, mergedCount, "Worker.wait.drain");
	return w;
}

// The log's one-line frame health record. It now formats a report that was
// already computed, so both sinks see identical numbers.
inline void DX12DiagProf::EmitFrameTimeline(
	std::string &out, const DX12ProfilingWindow &w)
{
	AppendFmt(out,
		"func=DX12FrameTimeline,\"present\":%ld,\"api\":\"%s\",\"swapchain\":\"%p\",\"hr\":\"0x%lx\","
		"\"frames\":%ld,"
		"\"frameIntervalUs\":%lld,\"frameWallUs\":%lld,\"outsidePresentUs\":%lld,"
		"\"presentWallUs\":%lld,\"maxFrameWallUs\":%lld,"
		"\"ourRootUs\":%lld,\"ourAllUs\":%lld,"
		"\"ourRootRows\":%ld,\"ourAllRows\":%ld,"
		"\"windowTargetFrames\":%ld,"
		"\"presentSliceKnownUs\":%lld,\"presentOriginalUs\":%lld,"
		"\"presentPostOriginalRuntimeUs\":%lld,"
		"\"presentModBeginFrameUs\":%lld,\"presentProfilingFlushUs\":%lld,"
		"\"presentConsumeUiActionsUs\":%lld,\"presentPerFrameCountersUs\":%lld,"
		"\"presentCaptureBookkeepingUs\":%lld,\"presentCaptureCompleteUs\":%lld,"
		"\"presentHotPathUpdateUs\":%lld,\"presentOtherUs\":%lld,"
		"\"presentOverlayTextUs\":%lld,"
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
		w.present,
		w.api ? w.api : "",
		w.swapChain,
		static_cast<unsigned long>(w.hr),
		static_cast<long>(w.frames),
		static_cast<long long>(w.frameIntervalUs),
		static_cast<long long>(w.frameWallUs),
		static_cast<long long>(w.outsidePresentUs),
		static_cast<long long>(w.presentWallUs),
		static_cast<long long>(w.maxFrameWallUs),
		static_cast<long long>(w.ourRootUs),
		static_cast<long long>(w.ourAllUs),
		static_cast<long>(w.ourRootRows),
		static_cast<long>(w.ourAllRows),
		static_cast<long>(DX12ProfilingHud::kWindowFrames),
		static_cast<long long>(w.presentKnownUs),
		static_cast<long long>(w.presentOriginalUs),
		static_cast<long long>(w.presentPostOriginalRuntimeUs),
		static_cast<long long>(w.presentModBeginFrameUs),
		static_cast<long long>(w.presentProfilingFlushUs),
		static_cast<long long>(w.presentConsumeUiActionsUs),
		static_cast<long long>(w.presentFrameCountersUs),
		static_cast<long long>(w.presentCaptureBookkeepingUs),
		static_cast<long long>(w.presentCaptureCompleteUs),
		static_cast<long long>(w.presentHotPathUpdateUs),
		static_cast<long long>(w.presentOtherUs),
		static_cast<long long>(w.presentOverlayTextUs),
		static_cast<long long>(w.presentEnterCalls),
		static_cast<long long>(w.presentLeaveCalls),
		static_cast<long long>(w.queueExecuteStageSamples),
		static_cast<long long>(w.queueWallCalls),
		static_cast<long long>(w.queueWallCommandLists),
		static_cast<long long>(w.queueEstimatedTotalUs),
		static_cast<long long>(w.queueWrapperTotalUs),
		static_cast<long long>(w.queueFlowTotalUs),
		static_cast<long long>(w.queueOriginalUs),
		static_cast<long long>(w.queueNotifyUs),
		static_cast<long long>(w.queueRegisterUs),
		static_cast<long long>(w.queueUnwrapUs),
		static_cast<long long>(w.queueRebuildWorkFlagsUs),
		static_cast<long long>(w.queueFenceCalls),
		static_cast<long long>(w.queueSignalUs),
		static_cast<long long>(w.queueWaitUs),
		static_cast<long long>(w.fenceCalls),
		static_cast<long long>(w.fenceTotalUs),
		static_cast<long long>(w.eventWaitCalls),
		static_cast<long long>(w.eventWaitTotalUs),
		static_cast<long long>(w.systemWaitCalls),
		static_cast<long long>(w.systemWaitTotalUs),
		static_cast<long long>(w.workerTaskCalls),
		static_cast<long long>(w.workerTaskUs),
		static_cast<long long>(w.workerWaitCalls),
		static_cast<long long>(w.workerWaitUs),
		static_cast<long long>(w.workerIdleWaitUs),
		static_cast<long long>(w.workerDrainWaitUs),
		static_cast<long long>(w.presentWallMinusKnownUs),
		static_cast<long>(w.frameDraws),
		static_cast<long>(w.frameDispatches),
		static_cast<long>(w.droppedFrameSummaries));
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
inline void DX12DiagProf::EmitSummary()
{
	Bucket merged[kMergedBuckets] = {};
	UINT mergedCount = 0;
	DrainThreadTables(merged, &mergedCount);

	// Checked before the report is built: BuildWindowReport consumes counters
	// with reset side effects, so it must not run for an empty window.
	if (sWindowFrames <= 0)
		return;

	// One report, two sinks. Building it first is what lets both see the same
	// numbers and what keeps counters with reset side effects (draw/dispatch
	// shards, dropped summaries) from being consumed twice or not at all.
	const DX12ProfilingWindow window = BuildWindowReport(merged, mergedCount);

	// On-screen sink. The ranking is only needed by the HUD, so the log path
	// does not pay for it.
	if (DX12ProfilingHud::IsEnabled()) {
		DX12ProfilingRow ranked[kMergedBuckets];
		const UINT rankedCount = BuildRankedRows(
			ranked, kMergedBuckets, merged, mergedCount);
		char text[DX12ProfilingHud::kTextBytes];
		const size_t length = DX12ProfilingText::Format(
			text, sizeof(text), DX12ProfilingHud::GetMode(),
			window, ranked, rankedCount);
		if (length)
			DX12ProfilingHud::Publish(text, length);
	}

	// Log sink. TIMING builds only: the per-domain troubleshooting sections
	// are written unconditionally. The ranked per-bucket table the HUD shows
	// on its Buckets page used to be emitted here as well; it was dropped
	// together with the dx12_profiling_* config options.
#if DX12_TIMING_BUILD
	std::string out;
	out.reserve(65536);
	EmitFrameTimeline(out, window);
	EmitApiWallSection(out, merged, mergedCount);
	EmitHotPathSection(out, merged, mergedCount);
	EmitCreateSection(out, merged, mergedCount);
	EmitCpuStageSection(out, merged, mergedCount);
	EmitLockSection(out, merged, mergedCount);
	EmitCounterSections(out, merged, mergedCount);
	DX12DiagLog::WriteDirect(out.data(), out.size());
	DX12DiagLog::FlushDirect();
#endif

	ResetWindow();
}

// Flattens the merged table into the formatter's row type, dropping the
// sub-keyed entries (they belong to the per-domain sections) and ranking by
// total time. Writer thread only.
inline UINT DX12DiagProf::BuildRankedRows(
	DX12ProfilingRow *rows, UINT capacity,
	const Bucket *merged, UINT mergedCount)
{
	if (!rows || !capacity)
		return 0;
	UINT count = 0;
	for (UINT i = 0; i < kMergedBuckets && count < capacity; ++i) {
		const Bucket &bucket = merged[i];
		if (!bucket.name || bucket.sub)
			continue;
		if (!bucket.calls && !bucket.totalUs && !bucket.items)
			continue;
		DX12ProfilingRow &row = rows[count++];
		row.name = bucket.name;
		row.calls = bucket.calls;
		row.totalUs = bucket.totalUs;
		row.maxUs = bucket.maxUs;
		row.items = bucket.items;
		row.rootUs = bucket.rootUs;
	}
	std::sort(rows, rows + count,
		[](const DX12ProfilingRow &lhs, const DX12ProfilingRow &rhs) {
			return lhs.totalUs > rhs.totalUs;
		});
	return count;
}


// ============================================================================
// Public macro surface.
//
// Recording macros are present in every build. Each is guarded by
// DX12DiagProf::Enabled(), which is a compile-time `true` in a TIMING build -
// so there the branch folds away and the generated code is exactly what it
// always was - and the HUD mode elsewhere, so a Release binary with the HUD off
// pays one predicted-not-taken branch per site.
//
// Log macros stay TIMING-only: a non-TIMING build never formats a line and
// never opens a file.
// ============================================================================

#if DX12_TIMING_BUILD

#define DX12_LOG(func, ...)             DX12DiagLog::Fields(func, __VA_ARGS__)
#define DX12_LOG_FLUSH(func, ...)       DX12DiagLog::FieldsFlush(func, __VA_ARGS__)
#define DX12_LOG_TEXT(...)              DX12DiagLog::Text(__VA_ARGS__)
#define DX12_LOG_EXECUTE_GAP()          DX12DiagLog::LogExecuteGap()

#else

#define DX12_LOG(func, ...)             do {} while (0)
#define DX12_LOG_FLUSH(func, ...)       do {} while (0)
#define DX12_LOG_TEXT(...)              do {} while (0)
#define DX12_LOG_EXECUTE_GAP()          do {} while (0)

#endif // DX12_TIMING_BUILD

// RAII scopes self-gate in their constructor, so the declaration stays a
// declaration and the name is always in scope for a later Pause/Resume.
#define DX12_PROF_SCOPE(name) \
	DX12DiagProf::Scope DX12_DIAG_JOIN(_diagScope_, __LINE__)(name)
#define DX12_PROF_SCOPE_NAMED(var, name) \
	DX12DiagProf::Scope var(name)
#define DX12_PROF_FUNCTION(counter) \
	DX12DiagProf::Scope DX12_DIAG_JOIN(_diagScope_, __LINE__)(#counter)
#define DX12_PROF_PAUSE(var)            var.Pause()
#define DX12_PROF_RESUME(var)           var.Resume()
#define DX12_PROF_FINISH(var)           var.Finish()

#define DX12_PROF_BEGIN(name) \
	do { if (DX12DiagProf::Enabled()) DX12DiagProf::Begin(name); } while (0)
#define DX12_PROF_END(name) \
	do { if (DX12DiagProf::Enabled()) DX12DiagProf::End(name); } while (0)
#define DX12_PROF_COUNT(name) \
	do { if (DX12DiagProf::Enabled()) DX12DiagProf::Count(name); } while (0)
#define DX12_PROF_ADD_US(name, us) \
	do { if (DX12DiagProf::Enabled()) DX12DiagProf::AddUs(name, us); } while (0)
#define DX12_PROF_ADD_US_ITEMS(name, us, items) \
	do { if (DX12DiagProf::Enabled()) DX12DiagProf::AddUsItems(name, us, items); } while (0)
#define DX12_PROF_ADD_ITEMS(name, items) \
	do { if (DX12DiagProf::Enabled()) DX12DiagProf::AddItems(name, items); } while (0)
#define DX12_PROF_ADD_CALLS(name, calls) \
	do { if (DX12DiagProf::Enabled()) DX12DiagProf::AddCalls(name, calls); } while (0)
#define DX12_PROF_CPU_STAGE(category, stage, kind, items, us) \
	do { if (DX12DiagProf::Enabled()) \
		DX12DiagProf::AddCpuStage(category, stage, kind, items, us); } while (0)
#define DX12_PROF_FRAME_DRAW() \
	do { if (DX12DiagProf::Enabled()) DX12RuntimeState::IncrementFrameDraws(); } while (0)
#define DX12_PROF_FRAME_DISPATCH() \
	do { if (DX12DiagProf::Enabled()) DX12RuntimeState::IncrementFrameDispatches(); } while (0)

#define DX12_PROF_STAGE_SCOPE(stage) \
	DX12DiagProf::Scope DX12_DIAG_JOIN(_diagScope_, __LINE__)("Stage." #stage)
#define DX12_PROF_STAGE_BEGIN(stage) \
	do { if (DX12DiagProf::Enabled()) DX12DiagProf::Begin("Stage." #stage); } while (0)
#define DX12_PROF_STAGE_END(stage) \
	do { if (DX12DiagProf::Enabled()) DX12DiagProf::End("Stage." #stage); } while (0)
#define DX12_PROF_API_SCOPE(api) \
	DX12DiagProf::Scope DX12_DIAG_JOIN(_diagScope_, __LINE__)("Api." #api)
#define DX12_PROF_API_SCOPE_NAMED(var, api) \
	DX12DiagProf::Scope var("Api." #api)
#define DX12_PROF_API_STAGE_SCOPE_NAMED(var, api, stage) \
	DX12DiagProf::ScopePair var("Api." #api, "Stage." #stage)
#define DX12_PROF_CREATE_SCOPE(api, stage) \
	DX12DiagProf::Scope DX12_DIAG_JOIN(_diagScope_, __LINE__)( \
		"Create." #api "." #stage)

// Per-frame boundary: a call either way is irrelevant next to a Present, so
// the gate lives inside the functions rather than at the call site.
#define DX12_PROF_FRAME_ENTER(api, swapChain) \
	DX12DiagProf::FrameEnter(api, swapChain)
#define DX12_PROF_FRAME_LEAVE(api, swapChain, hr) \
	DX12DiagProf::FrameLeave(api, swapChain, hr)

// Declares the timestamp unconditionally (the name must stay in scope) but
// only reads the clock when the profiler is on; a zero QuadPart is the
// "never read" marker DX12DiagProf::ElapsedUs recognises.
#define DX12_DIAG_QPC(var) \
	LARGE_INTEGER var = {}; \
	if (DX12DiagProf::Enabled()) QueryPerformanceCounter(&var)
#define DX12_DIAG_QPC_RESET(var) \
	do { var.QuadPart = 0; \
		if (DX12DiagProf::Enabled()) QueryPerformanceCounter(&var); } while (0)
#define DX12_DIAG_ELAPSED_US(var)       DX12DiagProf::ElapsedUs(var)

// The lock must be taken regardless; only the contention measurement is
// conditional, so the disabled path is the plain SRW call. `name` is the
// complete bucket name - call sites already carry their domain prefix, and
// prepending one here used to turn "Lock.modShared" into "Lock.Lock.modShared".
#define DX12_LOCK_ACQUIRE_SHARED(lock, name) \
	do { if (DX12DiagProf::Enabled()) DX12DiagProf::AcquireLockShared(lock, name); \
		else AcquireSRWLockShared(lock); } while (0)
#define DX12_LOCK_ACQUIRE_EXCLUSIVE(lock, name) \
	do { if (DX12DiagProf::Enabled()) DX12DiagProf::AcquireLockExclusive(lock, name); \
		else AcquireSRWLockExclusive(lock); } while (0)

// Fence/wait probes. DX12_DIAG_FENCE_VALUE_VAR declares a sampled fence value
// that only other diag macros may reference (when the profiler is not built
// in it declares nothing, so no unused-variable warnings and zero work).
#define DX12_DIAG_FENCE_VALUE_VAR(var, fence) \
	const UINT64 var = DX12DiagProf::FenceCompletedValue(fence)
#define DX12_DIAG_REGISTER_FENCE_EVENT(fence, eventHandle, value, completedBefore, completedAfter, hr) \
	do { if (DX12DiagProf::Enabled()) DX12DiagProf::RegisterFenceEventOnCompletion( \
		fence, eventHandle, value, completedBefore, completedAfter, hr); } while (0)
#define DX12_DIAG_REGISTER_FRAME_LATENCY_WAITABLE(swapChain, handle) \
	do { if (DX12DiagProf::Enabled()) \
		DX12DiagProf::RegisterFrameLatencyWaitableObject(swapChain, handle); } while (0)


// Frame-boundary breadcrumb for the freeze watchdog. Kept outside the macro
// surface because it is a plain function used across many TUs. The watchdog
// itself is TIMING-only, so outside those builds this inlines to nothing.
#if DX12_TIMING_BUILD
void DX12MarkStage(const char *stage);
#else
inline void DX12MarkStage(const char *) {}
#endif
