#pragma once

// ============================================================================
// DX12ProfilingHud - the bridge between the profiler's aggregation pass and
// the on-screen text overlay, plus the mode/freeze/config state both sides
// read.
//
// The profiler records into per-thread bucket tables while the game runs and
// the writer thread drains, merges and *formats* them once per interval. None
// of that may happen on the render thread, so the finished report is published
// here as one immutable text block and the render thread only ever copies it
// out.
//
// Render-thread cost:
//   - report unchanged (the common case, ~59 frames in 60): one acquire load
//     and a compare. No lock, no allocation, no formatting.
//   - report changed: a shared SRW acquisition and one memcpy of at most
//     kTextBytes. The writer publishes at most once per interval, so the lock
//     is never contended and the worst case is bounded - which is why a plain
//     lock beats a seqlock here, whose retry loop would put an unbounded worst
//     case on the render thread.
//
// This header deliberately depends on nothing but Windows and <atomic> so the
// profiler header can include it without a cycle: the mode it stores *is* the
// sampling gate (see DX12DiagProf::Enabled), and the pump-thread start that a
// mode change implies is done by DX12ProfilingSetMode in Core/Diag/DX12Diag.h,
// which has both halves in scope.
// ============================================================================

#include <Windows.h>

#include <atomic>
#include <stddef.h>
#include <string.h>

class DX12ProfilingHud
{
public:
	enum class Mode : LONG
	{
		Off = 0,
		Compact,     // fps/frame/present/queue/HUD self-cost plus the top stages
		Summary,     // a fixed set of Present./Queue./Stage./Wait. fields
		Top,         // every bucket ranked by total time
		Sections,    // the same buckets grouped by domain prefix
		Counters,    // item-only buckets and the per-frame draw/dispatch counts

		ModeCount,
	};

	static constexpr size_t kTextBytes = 16 * 1024;
	static constexpr int kMaxTopRows = 160;
	static constexpr LONG kDefaultIntervalMs = 500;
	static constexpr LONG kMinIntervalMs = 100;
	static constexpr LONG kMaxIntervalMs = 10000;
	static constexpr int kDefaultTopRows = 24;

	// --- state ---------------------------------------------------------------
	static Mode GetMode()
	{
		return static_cast<Mode>(sMode.load(std::memory_order_relaxed));
	}

	static bool IsEnabled()
	{
		return sMode.load(std::memory_order_relaxed) !=
			static_cast<LONG>(Mode::Off);
	}

	static bool IsFrozen()
	{
		return sFrozen.load(std::memory_order_relaxed) != 0;
	}

	static LONG IntervalMs()
	{
		return sIntervalMs.load(std::memory_order_relaxed);
	}

	static int TopRows()
	{
		return sTopRows.load(std::memory_order_relaxed);
	}

	// Whether the bulk "name,calls,totalUs,avgUs,maxUs,items" table is still
	// written to the log. The on-screen HUD shows the same numbers, so once it
	// is on the table is redundant log bulk - but the per-domain troubleshooting
	// sections (frame timeline, api wall, hot path, create, cpu stage, lock,
	// counters) are unaffected and keep flowing.
	static bool LogBucketTable()
	{
		return sLogBucketTable.load(std::memory_order_relaxed) != 0;
	}

	static void SetMode(Mode mode)
	{
		const LONG raw = static_cast<LONG>(mode);
		sMode.store(raw >= 0 && raw < static_cast<LONG>(Mode::ModeCount) ? raw : 0,
			std::memory_order_relaxed);
	}

	static void CycleMode()
	{
		const LONG next = (sMode.load(std::memory_order_relaxed) + 1) %
			static_cast<LONG>(Mode::ModeCount);
		sMode.store(next, std::memory_order_relaxed);
	}

	// Freezing keeps the report the screen is showing. The profiler keeps
	// sampling and the log keeps flowing; only the display stops changing, so
	// the numbers under the cursor do not move while they are being read.
	static void ToggleFreeze()
	{
		sFrozen.store(sFrozen.load(std::memory_order_relaxed) ? 0 : 1,
			std::memory_order_relaxed);
	}

	static void ApplyConfig(Mode mode, LONG intervalMs, int topRows, bool logBucketTable)
	{
		SetMode(mode);
		if (intervalMs < kMinIntervalMs)
			intervalMs = kMinIntervalMs;
		if (intervalMs > kMaxIntervalMs)
			intervalMs = kMaxIntervalMs;
		sIntervalMs.store(intervalMs, std::memory_order_relaxed);
		if (topRows < 1)
			topRows = 1;
		if (topRows > kMaxTopRows)
			topRows = kMaxTopRows;
		sTopRows.store(topRows, std::memory_order_relaxed);
		sLogBucketTable.store(logBucketTable ? 1 : 0, std::memory_order_relaxed);
	}

	static const char *ModeName(Mode mode)
	{
		switch (mode) {
		case Mode::Compact:  return "Compact";
		case Mode::Summary:  return "Summary";
		case Mode::Top:      return "Top";
		case Mode::Sections: return "Sections";
		case Mode::Counters: return "Counters";
		default:             return "Off";
		}
	}

	// --- report transport ----------------------------------------------------
	// Writer thread only. A frozen HUD keeps whatever the screen already shows.
	static void Publish(const char *text, size_t length)
	{
		if (!text || !length || IsFrozen())
			return;
		if (length >= kTextBytes)
			length = kTextBytes - 1;

		AcquireSRWLockExclusive(&sLock);
		memcpy(sText, text, length);
		sText[length] = '\0';
		sLength = length;
		// The version must move *after* the bytes, otherwise a consumer that
		// observes the new version could copy the previous report.
		InterlockedIncrement64(&sVersion);
		ReleaseSRWLockExclusive(&sLock);
	}

	// Render thread only. Returns false without touching the lock while the
	// published report is the one the caller already holds.
	static bool AcquireLatest(
		UINT64 *version, char *out, size_t outCapacity, size_t *outLength)
	{
		if (!version || !out || !outCapacity)
			return false;
		const LONG64 published = InterlockedCompareExchange64(&sVersion, 0, 0);
		if (published <= 0 || static_cast<UINT64>(published) == *version)
			return false;

		AcquireSRWLockShared(&sLock);
		// Holding the shared lock means the writer cannot be mid-copy, so the
		// length and the bytes are from the same report. The version stamped
		// on the caller is whatever the buffer currently holds, which is what
		// was just copied.
		size_t copy = sLength;
		if (copy >= outCapacity)
			copy = 0;
		if (copy) {
			memcpy(out, sText, copy);
			out[copy] = '\0';
			*outLength = copy;
			*version = static_cast<UINT64>(
				InterlockedCompareExchange64(&sVersion, 0, 0));
		}
		ReleaseSRWLockShared(&sLock);
		return copy != 0;
	}

	static UINT64 CurrentVersion()
	{
		const LONG64 value = InterlockedCompareExchange64(&sVersion, 0, 0);
		return value > 0 ? static_cast<UINT64>(value) : 0;
	}

private:
	inline static SRWLOCK sLock = SRWLOCK_INIT;
	inline static char sText[kTextBytes] = {};
	inline static size_t sLength = 0;
	inline static volatile LONG64 sVersion = 0;
	inline static std::atomic<LONG> sMode{ static_cast<LONG>(Mode::Off) };
	inline static std::atomic<LONG> sFrozen{ 0 };
	inline static std::atomic<LONG> sIntervalMs{ kDefaultIntervalMs };
	inline static std::atomic<LONG> sTopRows{ kDefaultTopRows };
	// Default on: the whole point of the change is that the bulk table moves to
	// the screen. Troubleshooting sections are not affected either way.
	inline static std::atomic<LONG> sLogBucketTable{ 0 };
};
