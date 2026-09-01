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

	static void AcquireUavMatchCacheShared(SRWLOCK *lock)
	{
		AcquireShared(lock, &uavMatchCacheShared);
	}

	static void AcquireUavMatchCacheExclusive(SRWLOCK *lock)
	{
		AcquireExclusive(lock, &uavMatchCacheExclusive);
	}

	static void AcquireSrvProbeCacheShared(SRWLOCK *lock)
	{
		AcquireShared(lock, &srvProbeCacheShared);
	}

	static void AcquireSrvProbeCacheExclusive(SRWLOCK *lock)
	{
		AcquireExclusive(lock, &srvProbeCacheExclusive);
	}

	static void AcquireOutputResizeShared(SRWLOCK *lock)
	{
		AcquireShared(lock, &outputResizeShared);
	}

	static void AcquireOutputResizeExclusive(SRWLOCK *lock)
	{
		AcquireExclusive(lock, &outputResizeExclusive);
	}

	static void AcquireOutputResizeCacheShared(SRWLOCK *lock)
	{
		AcquireShared(lock, &outputResizeCacheShared);
	}

	static void AcquireOutputResizeCacheExclusive(SRWLOCK *lock)
	{
		AcquireExclusive(lock, &outputResizeCacheExclusive);
	}

	static void LogAndReset()
	{
		const LONG64 frequency = Frequency();
		LogLock(&modShared, "modShared", frequency);
		LogLock(&modExclusive, "modExclusive", frequency);
		LogLock(&preSkinShared, "preSkinShared", frequency);
		LogLock(&preSkinExclusive, "preSkinExclusive", frequency);
		LogLock(&uavMatchCacheShared, "uavMatchCacheShared", frequency);
		LogLock(&uavMatchCacheExclusive, "uavMatchCacheExclusive", frequency);
		LogLock(&srvProbeCacheShared, "srvProbeCacheShared", frequency);
		LogLock(&srvProbeCacheExclusive, "srvProbeCacheExclusive", frequency);
		LogLock(&outputResizeShared, "outputResizeShared", frequency);
		LogLock(&outputResizeExclusive, "outputResizeExclusive", frequency);
		LogLock(&outputResizeCacheShared, "outputResizeCacheShared", frequency);
		LogLock(&outputResizeCacheExclusive, "outputResizeCacheExclusive", frequency);
	}

private:
	inline static WaitAccumulator modShared;
	inline static WaitAccumulator modExclusive;
	inline static WaitAccumulator preSkinShared;
	inline static WaitAccumulator preSkinExclusive;
	inline static WaitAccumulator uavMatchCacheShared;
	inline static WaitAccumulator uavMatchCacheExclusive;
	inline static WaitAccumulator srvProbeCacheShared;
	inline static WaitAccumulator srvProbeCacheExclusive;
	inline static WaitAccumulator outputResizeShared;
	inline static WaitAccumulator outputResizeExclusive;
	inline static WaitAccumulator outputResizeCacheShared;
	inline static WaitAccumulator outputResizeCacheExclusive;

	static void LogLock(WaitAccumulator *accumulator, const char *name, LONG64 frequency)
	{
		const LONG64 contentions =
			InterlockedExchange64(&accumulator->contentions, 0);
		if (contentions == 0)
			return;
		const LONG64 waitTicks =
			InterlockedExchange64(&accumulator->waitTicks, 0);
		const LONG64 maxTicks =
			InterlockedExchange64(&accumulator->maxWaitTicks, 0);
		DX12LogManager::Fields("DX12ModLockTiming",
			"\"lock\":\"%s\",\"contentions\":%lld,\"waitUs\":%lld,\"maxUs\":%lld",
			name ? name : "",
			static_cast<long long>(contentions),
			static_cast<long long>(waitTicks * 1000000 / frequency),
			static_cast<long long>(maxTicks * 1000000 / frequency));
	}

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
	static void AcquireUavMatchCacheShared(SRWLOCK *lock)
	{
		AcquireSRWLockShared(lock);
	}
	static void AcquireUavMatchCacheExclusive(SRWLOCK *lock)
	{
		AcquireSRWLockExclusive(lock);
	}
	static void AcquireSrvProbeCacheShared(SRWLOCK *lock)
	{
		AcquireSRWLockShared(lock);
	}
	static void AcquireSrvProbeCacheExclusive(SRWLOCK *lock)
	{
		AcquireSRWLockExclusive(lock);
	}
	static void AcquireOutputResizeShared(SRWLOCK *lock)
	{
		AcquireSRWLockShared(lock);
	}
	static void AcquireOutputResizeExclusive(SRWLOCK *lock)
	{
		AcquireSRWLockExclusive(lock);
	}
	static void AcquireOutputResizeCacheShared(SRWLOCK *lock)
	{
		AcquireSRWLockShared(lock);
	}
	static void AcquireOutputResizeCacheExclusive(SRWLOCK *lock)
	{
		AcquireSRWLockExclusive(lock);
	}
	static void LogAndReset() {}
};

#endif
