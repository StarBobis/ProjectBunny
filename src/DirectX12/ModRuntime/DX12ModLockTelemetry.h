#pragma once

// Mod-runtime global lock wait telemetry (timing builds + stub).
// gModLock / gPreSkinLock waits park inside RtlWaitOnAddress, so they never
// reach the hooked system-wait probes and contention on these two globals is
// invisible in the existing timing logs. The helpers below keep the
// uncontended path free (a single TryAcquire) and charge only the contended
// slow path to per-lock accumulators, flushed once per frame from
// DX12Profiler::FlushFrame().

#include <Windows.h>

#include "Core/DX12LogManager.h"
#include "Core/DX12RuntimeState.h"

#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)

class DX12ModLockTelemetryUtils
{
public:
	struct WaitAccumulator
	{
		volatile LONG64 contentions = 0;
		volatile LONG64 waitTicks = 0;
		volatile LONG64 maxWaitTicks = 0;
	};

	static void AcquireModShared(SRWLOCK *lock)
	{
		AcquireShared(lock, &modShared);
	}

	static void AcquireModExclusive(SRWLOCK *lock)
	{
		AcquireExclusive(lock, &modExclusive);
	}

	static void AcquirePreSkinShared(SRWLOCK *lock)
	{
		AcquireShared(lock, &preSkinShared);
	}

	static void AcquirePreSkinExclusive(SRWLOCK *lock)
	{
		AcquireExclusive(lock, &preSkinExclusive);
	}

	static void LogAndReset()
	{
		const LONG64 frequency = Frequency();
		const LONG64 modSharedContentions =
			InterlockedExchange64(&modShared.contentions, 0);
		const LONG64 modSharedTicks =
			InterlockedExchange64(&modShared.waitTicks, 0);
		const LONG64 modSharedMaxTicks =
			InterlockedExchange64(&modShared.maxWaitTicks, 0);
		const LONG64 modExclusiveContentions =
			InterlockedExchange64(&modExclusive.contentions, 0);
		const LONG64 modExclusiveTicks =
			InterlockedExchange64(&modExclusive.waitTicks, 0);
		const LONG64 modExclusiveMaxTicks =
			InterlockedExchange64(&modExclusive.maxWaitTicks, 0);
		const LONG64 preSkinSharedContentions =
			InterlockedExchange64(&preSkinShared.contentions, 0);
		const LONG64 preSkinSharedTicks =
			InterlockedExchange64(&preSkinShared.waitTicks, 0);
		const LONG64 preSkinSharedMaxTicks =
			InterlockedExchange64(&preSkinShared.maxWaitTicks, 0);
		const LONG64 preSkinExclusiveContentions =
			InterlockedExchange64(&preSkinExclusive.contentions, 0);
		const LONG64 preSkinExclusiveTicks =
			InterlockedExchange64(&preSkinExclusive.waitTicks, 0);
		const LONG64 preSkinExclusiveMaxTicks =
			InterlockedExchange64(&preSkinExclusive.maxWaitTicks, 0);
		if (modSharedContentions == 0 && modExclusiveContentions == 0 &&
			preSkinSharedContentions == 0 && preSkinExclusiveContentions == 0)
			return;

		DX12LogManager::Fields("DX12ModLockTiming",
			"\"present\":%ld,"
			"\"modSharedContentions\":%lld,\"modSharedWaitUs\":%lld,\"modSharedMaxUs\":%lld,"
			"\"modExclusiveContentions\":%lld,\"modExclusiveWaitUs\":%lld,\"modExclusiveMaxUs\":%lld,"
			"\"preSkinSharedContentions\":%lld,\"preSkinSharedWaitUs\":%lld,\"preSkinSharedMaxUs\":%lld,"
			"\"preSkinExclusiveContentions\":%lld,\"preSkinExclusiveWaitUs\":%lld,\"preSkinExclusiveMaxUs\":%lld",
			DX12RuntimeState::GetPresentCount(),
			static_cast<long long>(modSharedContentions),
			static_cast<long long>(modSharedTicks * 1000000 / frequency),
			static_cast<long long>(modSharedMaxTicks * 1000000 / frequency),
			static_cast<long long>(modExclusiveContentions),
			static_cast<long long>(modExclusiveTicks * 1000000 / frequency),
			static_cast<long long>(modExclusiveMaxTicks * 1000000 / frequency),
			static_cast<long long>(preSkinSharedContentions),
			static_cast<long long>(preSkinSharedTicks * 1000000 / frequency),
			static_cast<long long>(preSkinSharedMaxTicks * 1000000 / frequency),
			static_cast<long long>(preSkinExclusiveContentions),
			static_cast<long long>(preSkinExclusiveTicks * 1000000 / frequency),
			static_cast<long long>(preSkinExclusiveMaxTicks * 1000000 / frequency));
	}

private:
	inline static WaitAccumulator modShared;
	inline static WaitAccumulator modExclusive;
	inline static WaitAccumulator preSkinShared;
	inline static WaitAccumulator preSkinExclusive;

	static LONG64 Frequency()
	{
		static const LONG64 frequency = []() {
			LARGE_INTEGER value = {};
			QueryPerformanceFrequency(&value);
			return value.QuadPart > 0 ? value.QuadPart : 1;
		}();
		return frequency;
	}

	static void RecordWait(WaitAccumulator *accumulator, LONG64 ticks)
	{
		if (ticks < 0)
			ticks = 0;
		InterlockedIncrement64(&accumulator->contentions);
		InterlockedExchangeAdd64(&accumulator->waitTicks, ticks);
		LONG64 observed = InterlockedCompareExchange64(
			&accumulator->maxWaitTicks, 0, 0);
		while (ticks > observed) {
			const LONG64 previous = InterlockedCompareExchange64(
				&accumulator->maxWaitTicks, ticks, observed);
			if (previous == observed)
				break;
			observed = previous;
		}
	}

	static void AcquireShared(SRWLOCK *lock, WaitAccumulator *accumulator)
	{
		if (TryAcquireSRWLockShared(lock))
			return;
		LARGE_INTEGER begin = {};
		LARGE_INTEGER end = {};
		QueryPerformanceCounter(&begin);
		AcquireSRWLockShared(lock);
		QueryPerformanceCounter(&end);
		RecordWait(accumulator, end.QuadPart - begin.QuadPart);
	}

	static void AcquireExclusive(SRWLOCK *lock, WaitAccumulator *accumulator)
	{
		if (TryAcquireSRWLockExclusive(lock))
			return;
		LARGE_INTEGER begin = {};
		LARGE_INTEGER end = {};
		QueryPerformanceCounter(&begin);
		AcquireSRWLockExclusive(lock);
		QueryPerformanceCounter(&end);
		RecordWait(accumulator, end.QuadPart - begin.QuadPart);
	}
};

#else

class DX12ModLockTelemetryUtils
{
public:
	static void AcquireModShared(SRWLOCK *lock)
	{
		AcquireSRWLockShared(lock);
	}
	static void AcquireModExclusive(SRWLOCK *lock)
	{
		AcquireSRWLockExclusive(lock);
	}
	static void AcquirePreSkinShared(SRWLOCK *lock)
	{
		AcquireSRWLockShared(lock);
	}
	static void AcquirePreSkinExclusive(SRWLOCK *lock)
	{
		AcquireSRWLockExclusive(lock);
	}
	static void LogAndReset() {}
};

#endif
