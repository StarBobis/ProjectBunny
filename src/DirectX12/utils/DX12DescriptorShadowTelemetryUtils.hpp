#pragma once

#include <Windows.h>

#if defined(_DEBUG)

#include "DX12LogManager.hpp"
#include "DX12RuntimeState.hpp"

class DX12DescriptorShadowTelemetryUtils
{
public:
	struct WaitAccumulator
	{
		volatile LONG64 contentions = 0;
		volatile LONG64 waitTicks = 0;
		volatile LONG64 maxWaitTicks = 0;
	};

	inline static WaitAccumulator heapShared;
	inline static WaitAccumulator heapExclusive;
	inline static WaitAccumulator globalShared;
	inline static WaitAccumulator globalExclusive;
	inline static volatile LONG64 resolveMisses = 0;
	inline static volatile LONG64 resolveFailures = 0;
	inline static volatile LONG64 fallbackCalls = 0;
	inline static volatile LONG64 fallbackSlots = 0;

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
		if (!accumulator)
			return;
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
		if (!lock)
			return;
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
		if (!lock)
			return;
		if (TryAcquireSRWLockExclusive(lock))
			return;
		LARGE_INTEGER begin = {};
		LARGE_INTEGER end = {};
		QueryPerformanceCounter(&begin);
		AcquireSRWLockExclusive(lock);
		QueryPerformanceCounter(&end);
		RecordWait(accumulator, end.QuadPart - begin.QuadPart);
	}

	static void AcquireHeapShared(SRWLOCK *lock)
	{
		AcquireShared(lock, &heapShared);
	}

	static void AcquireHeapExclusive(SRWLOCK *lock)
	{
		AcquireExclusive(lock, &heapExclusive);
	}

	static void AcquireGlobalShared(SRWLOCK *lock)
	{
		AcquireShared(lock, &globalShared);
	}

	static void AcquireGlobalExclusive(SRWLOCK *lock)
	{
		AcquireExclusive(lock, &globalExclusive);
	}

	static void RecordResolveMiss()
	{
		InterlockedIncrement64(&resolveMisses);
	}

	static void RecordResolveFailure()
	{
		InterlockedIncrement64(&resolveFailures);
	}

	static void RecordFallback(UINT count)
	{
		InterlockedIncrement64(&fallbackCalls);
		InterlockedExchangeAdd64(&fallbackSlots, count);
	}

	static void LogAndReset()
	{
		const LONG64 frequency = Frequency();
		const LONG64 heapSharedContentions =
			InterlockedExchange64(&heapShared.contentions, 0);
		const LONG64 heapSharedTicks =
			InterlockedExchange64(&heapShared.waitTicks, 0);
		const LONG64 heapSharedMaxTicks =
			InterlockedExchange64(&heapShared.maxWaitTicks, 0);
		const LONG64 heapExclusiveContentions =
			InterlockedExchange64(&heapExclusive.contentions, 0);
		const LONG64 heapExclusiveTicks =
			InterlockedExchange64(&heapExclusive.waitTicks, 0);
		const LONG64 heapExclusiveMaxTicks =
			InterlockedExchange64(&heapExclusive.maxWaitTicks, 0);
		const LONG64 globalSharedContentions =
			InterlockedExchange64(&globalShared.contentions, 0);
		const LONG64 globalSharedTicks =
			InterlockedExchange64(&globalShared.waitTicks, 0);
		const LONG64 globalSharedMaxTicks =
			InterlockedExchange64(&globalShared.maxWaitTicks, 0);
		const LONG64 globalExclusiveContentions =
			InterlockedExchange64(&globalExclusive.contentions, 0);
		const LONG64 globalExclusiveTicks =
			InterlockedExchange64(&globalExclusive.waitTicks, 0);
		const LONG64 globalExclusiveMaxTicks =
			InterlockedExchange64(&globalExclusive.maxWaitTicks, 0);
		const LONG64 misses = InterlockedExchange64(&resolveMisses, 0);
		const LONG64 failures = InterlockedExchange64(&resolveFailures, 0);
		const LONG64 fallbacks = InterlockedExchange64(&fallbackCalls, 0);
		const LONG64 slots = InterlockedExchange64(&fallbackSlots, 0);
		if (heapSharedContentions == 0 && heapExclusiveContentions == 0 &&
			globalSharedContentions == 0 && globalExclusiveContentions == 0 &&
			misses == 0 && failures == 0 && fallbacks == 0)
			return;

		DX12LogManager::JsonFunc("DX12DescriptorShadowTiming",
			"\"present\":%ld,\"resolveMisses\":%lld,\"resolveFailures\":%lld,"
			"\"fallbackCalls\":%lld,\"fallbackSlots\":%lld,"
			"\"heapSharedContentions\":%lld,\"heapSharedWaitUs\":%lld,\"heapSharedMaxUs\":%lld,"
			"\"heapExclusiveContentions\":%lld,\"heapExclusiveWaitUs\":%lld,\"heapExclusiveMaxUs\":%lld,"
			"\"globalSharedContentions\":%lld,\"globalSharedWaitUs\":%lld,\"globalSharedMaxUs\":%lld,"
			"\"globalExclusiveContentions\":%lld,\"globalExclusiveWaitUs\":%lld,\"globalExclusiveMaxUs\":%lld",
			DX12RuntimeState::GetPresentCount(),
			static_cast<long long>(misses),
			static_cast<long long>(failures),
			static_cast<long long>(fallbacks),
			static_cast<long long>(slots),
			static_cast<long long>(heapSharedContentions),
			static_cast<long long>(heapSharedTicks * 1000000 / frequency),
			static_cast<long long>(heapSharedMaxTicks * 1000000 / frequency),
			static_cast<long long>(heapExclusiveContentions),
			static_cast<long long>(heapExclusiveTicks * 1000000 / frequency),
			static_cast<long long>(heapExclusiveMaxTicks * 1000000 / frequency),
			static_cast<long long>(globalSharedContentions),
			static_cast<long long>(globalSharedTicks * 1000000 / frequency),
			static_cast<long long>(globalSharedMaxTicks * 1000000 / frequency),
			static_cast<long long>(globalExclusiveContentions),
			static_cast<long long>(globalExclusiveTicks * 1000000 / frequency),
			static_cast<long long>(globalExclusiveMaxTicks * 1000000 / frequency));
	}
};

#else

class DX12DescriptorShadowTelemetryUtils
{
public:
	static void AcquireHeapShared(SRWLOCK *lock)
	{
		if (lock)
			AcquireSRWLockShared(lock);
	}
	static void AcquireHeapExclusive(SRWLOCK *lock)
	{
		if (lock)
			AcquireSRWLockExclusive(lock);
	}
	static void AcquireGlobalShared(SRWLOCK *lock)
	{
		if (lock)
			AcquireSRWLockShared(lock);
	}
	static void AcquireGlobalExclusive(SRWLOCK *lock)
	{
		if (lock)
			AcquireSRWLockExclusive(lock);
	}
	static void RecordResolveMiss() {}
	static void RecordResolveFailure() {}
	static void RecordFallback(UINT) {}
	static void LogAndReset() {}
};

#endif
