#pragma once

// Descriptor-shadow timing telemetry (timing builds + stub).
// Split out of DX12Hooks.h; include that umbrella header from existing code.

#include "../DX12Core.h"

#include "Nektra/NktHookLib.h"
#include "util_min.h"
#include "crc32c.h"

#include <d3dcompiler.h>

#include <climits>
#include <condition_variable>
#include <limits>
#include <map>
#include <mutex>
#include <thread>

#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)


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
	inline static volatile LONG64 copyRangeCalls = 0;
	inline static volatile LONG64 copySlots = 0;
	inline static volatile LONG64 copySingleRangeCalls = 0;
	inline static volatile LONG64 copyMultiRangeCalls = 0;
	inline static volatile LONG64 copyResolvedRangeCalls = 0;
	inline static volatile LONG64 copySameHeapRangeCalls = 0;
	inline static volatile LONG64 copyOverlapRangeCalls = 0;
	inline static volatile LONG64 copySourceFoundSlots = 0;
	inline static volatile LONG64 copySourceMissingSlots = 0;
	inline static volatile LONG64 copyChangedSlots = 0;
	inline static volatile LONG64 copyUnchangedSlots = 0;
	inline static volatile LONG64 copyErasedSlots = 0;
	inline static volatile LONG64 copyAlreadyEmptySlots = 0;
	inline static volatile LONG64 copyMemoHits = 0;
	inline static volatile LONG64 copyMemoMisses = 0;
	inline static volatile LONG64 copyRepeatHits = 0;
	inline static volatile LONG64 heapCacheHits = 0;
	inline static volatile LONG64 copyCallsCbvSrvUav = 0;
	inline static volatile LONG64 copyCallsSampler = 0;
	inline static volatile LONG64 copyCallsRtv = 0;
	inline static volatile LONG64 copyCallsDsv = 0;
	inline static volatile LONG64 slotAllocations = 0;
	inline static volatile LONG64 slotAllocationBytes = 0;

	// Per-frame copy telemetry is updated on the hottest path in the wrapper
	// (every descriptor range the game copies - tens of thousands per frame).
	// Routing those updates through per-thread accumulator blocks turns the
	// per-range cost into plain local increments and merges the blocks once
	// per frame at Present; the previous design paid up to 14 Interlocked
	// RMWs per range, which measurably inflated both the timing build's
	// numbers and its run time.
	//
	// The pool is a fixed array (never freed, no dangling registry entries on
	// thread exit); threads beyond the pool share the last block, which only
	// merges their counts. The present thread reads other threads' blocks
	// with aligned 64-bit loads, which are atomic on x64; a concurrent
	// increment can at worst be missed for one frame in the diagnostic.
	struct TlsCopyCounters
	{
		LONG64 resolveMisses = 0;
		LONG64 resolveFailures = 0;
		LONG64 fallbackCalls = 0;
		LONG64 fallbackSlots = 0;
		LONG64 rangeCalls = 0;
		LONG64 slots = 0;
		LONG64 singleRangeCalls = 0;
		LONG64 multiRangeCalls = 0;
		LONG64 resolvedRangeCalls = 0;
		LONG64 sameHeapRangeCalls = 0;
		LONG64 overlapRangeCalls = 0;
		LONG64 sourceFoundSlots = 0;
		LONG64 sourceMissingSlots = 0;
		LONG64 changedSlots = 0;
		LONG64 unchangedSlots = 0;
		LONG64 erasedSlots = 0;
		LONG64 alreadyEmptySlots = 0;
		LONG64 memoHits = 0;
		LONG64 memoMisses = 0;
		LONG64 heapCacheHits = 0; // global set-associative heap cache hits
		LONG64 copyCallsCbvSrvUav = 0;
		LONG64 copyCallsSampler = 0;
		LONG64 copyCallsRtv = 0;
		LONG64 copyCallsDsv = 0;
	};

	static constexpr UINT TlsBlockCount = 64;
	inline static TlsCopyCounters tlsBlocks[TlsBlockCount] = {};
	inline static volatile LONG nextTlsBlock = 0;
	inline static thread_local UINT tlsBlockIndex = UINT_MAX;

	static TlsCopyCounters &TlsCopyCountersRef()
	{
		if (tlsBlockIndex == UINT_MAX) {
			const LONG index = InterlockedIncrement(&nextTlsBlock) - 1;
			tlsBlockIndex = (index >= 0 &&
				index < static_cast<LONG>(TlsBlockCount))
				? static_cast<UINT>(index) : TlsBlockCount - 1;
		}
		return tlsBlocks[tlsBlockIndex];
	}

	static void RecordResolveMiss()
	{
		++TlsCopyCountersRef().resolveMisses;
	}

	static void RecordResolveFailure()
	{
		++TlsCopyCountersRef().resolveFailures;
	}

	static void RecordFallback(UINT count)
	{
		TlsCopyCounters &counters = TlsCopyCountersRef();
		++counters.fallbackCalls;
		counters.fallbackSlots += count;
	}

	static void RecordCopyRange(
		UINT count, bool resolved, bool sameHeap, bool overlaps)
	{
		TlsCopyCounters &counters = TlsCopyCountersRef();
		++counters.rangeCalls;
		counters.slots += count;
		if (count == 1)
			++counters.singleRangeCalls;
		else
			++counters.multiRangeCalls;
		if (resolved)
			++counters.resolvedRangeCalls;
		if (sameHeap)
			++counters.sameHeapRangeCalls;
		if (overlaps)
			++counters.overlapRangeCalls;
	}

	static void RecordCopyTypeCall(D3D12_DESCRIPTOR_HEAP_TYPE type)
	{
		TlsCopyCounters &counters = TlsCopyCountersRef();
		switch (type) {
		case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV:
			++counters.copyCallsCbvSrvUav;
			break;
		case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:
			++counters.copyCallsSampler;
			break;
		case D3D12_DESCRIPTOR_HEAP_TYPE_RTV:
			++counters.copyCallsRtv;
			break;
		case D3D12_DESCRIPTOR_HEAP_TYPE_DSV:
			++counters.copyCallsDsv;
			break;
		default:
			break;
		}
	}

	// Shadow slot array materialization is rare (once per heap) but can be
	// MBs-sized on the game thread; count it so the cost is visible.
	static void RecordSlotAllocation(UINT64 count, UINT64 bytes)
	{
		InterlockedExchangeAdd64(&slotAllocations, count);
		InterlockedExchangeAdd64(&slotAllocationBytes, bytes);
	}

	static void RecordCopyResults(
		UINT sourceFound, UINT sourceMissing,
		UINT changed, UINT unchanged,
		UINT erased, UINT alreadyEmpty)
	{
		TlsCopyCounters &counters = TlsCopyCountersRef();
		if (sourceFound)
			counters.sourceFoundSlots += sourceFound;
		if (sourceMissing)
			counters.sourceMissingSlots += sourceMissing;
		if (changed)
			counters.changedSlots += changed;
		if (unchanged)
			counters.unchangedSlots += unchanged;
		if (erased)
			counters.erasedSlots += erased;
		if (alreadyEmpty)
			counters.alreadyEmptySlots += alreadyEmpty;
	}

	static void RecordCopyMemo(bool hit)
	{
		TlsCopyCounters &counters = TlsCopyCountersRef();
		if (hit)
			++counters.memoHits;
		else
			++counters.memoMisses;
	}

	static void RecordHeapCacheHit()
	{
		++TlsCopyCountersRef().heapCacheHits;
	}

	static void MergeTlsCountersLocked()
	{
		for (UINT i = 0; i < TlsBlockCount; ++i) {
			TlsCopyCounters &block = tlsBlocks[i];
			if (!block.rangeCalls && !block.resolveMisses &&
				!block.fallbackCalls && !block.memoHits && !block.memoMisses &&
				!block.sourceFoundSlots && !block.sourceMissingSlots &&
				!block.changedSlots && !block.unchangedSlots &&
				!block.erasedSlots && !block.alreadyEmptySlots)
				continue;
			InterlockedExchangeAdd64(&resolveMisses, block.resolveMisses);
			InterlockedExchangeAdd64(&resolveFailures, block.resolveFailures);
			InterlockedExchangeAdd64(&fallbackCalls, block.fallbackCalls);
			InterlockedExchangeAdd64(&fallbackSlots, block.fallbackSlots);
			InterlockedExchangeAdd64(&copyRangeCalls, block.rangeCalls);
			InterlockedExchangeAdd64(&copySlots, block.slots);
			InterlockedExchangeAdd64(&copySingleRangeCalls, block.singleRangeCalls);
			InterlockedExchangeAdd64(&copyMultiRangeCalls, block.multiRangeCalls);
			InterlockedExchangeAdd64(&copyResolvedRangeCalls, block.resolvedRangeCalls);
			InterlockedExchangeAdd64(&copySameHeapRangeCalls, block.sameHeapRangeCalls);
			InterlockedExchangeAdd64(&copyOverlapRangeCalls, block.overlapRangeCalls);
			InterlockedExchangeAdd64(&copySourceFoundSlots, block.sourceFoundSlots);
			InterlockedExchangeAdd64(&copySourceMissingSlots, block.sourceMissingSlots);
			InterlockedExchangeAdd64(&copyChangedSlots, block.changedSlots);
			InterlockedExchangeAdd64(&copyUnchangedSlots, block.unchangedSlots);
			InterlockedExchangeAdd64(&copyErasedSlots, block.erasedSlots);
			InterlockedExchangeAdd64(&copyAlreadyEmptySlots, block.alreadyEmptySlots);
			InterlockedExchangeAdd64(&copyMemoHits, block.memoHits);
			InterlockedExchangeAdd64(&copyMemoMisses, block.memoMisses);
			InterlockedExchangeAdd64(&heapCacheHits, block.heapCacheHits);
			InterlockedExchangeAdd64(&copyCallsCbvSrvUav, block.copyCallsCbvSrvUav);
			InterlockedExchangeAdd64(&copyCallsSampler, block.copyCallsSampler);
			InterlockedExchangeAdd64(&copyCallsRtv, block.copyCallsRtv);
			InterlockedExchangeAdd64(&copyCallsDsv, block.copyCallsDsv);
			block = TlsCopyCounters();
		}
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

	// Commits one API call's locally accumulated descriptor-copy counters.
	// CopyDescriptors commonly exposes 8-10 single-slot ranges per call; doing
	// every counter update atomically per slot made the diagnostics themselves
	// part of the hot path while preserving no additional information.
	static void RecordCopyBatch(
		LONG64 rangeCalls, LONG64 slots,
		LONG64 singleRanges, LONG64 multiRanges,
		LONG64 resolvedRanges, LONG64 sameHeapRanges, LONG64 overlapRanges,
		LONG64 sourceFound, LONG64 sourceMissing,
		LONG64 changed, LONG64 unchanged,
		LONG64 erased, LONG64 alreadyEmpty,
		LONG64 memoHits, LONG64 memoMisses,
		LONG64 repeatHits)
	{
		if (rangeCalls) InterlockedExchangeAdd64(&copyRangeCalls, rangeCalls);
		if (slots) InterlockedExchangeAdd64(&copySlots, slots);
		if (singleRanges) InterlockedExchangeAdd64(&copySingleRangeCalls, singleRanges);
		if (multiRanges) InterlockedExchangeAdd64(&copyMultiRangeCalls, multiRanges);
		if (resolvedRanges) InterlockedExchangeAdd64(&copyResolvedRangeCalls, resolvedRanges);
		if (sameHeapRanges) InterlockedExchangeAdd64(&copySameHeapRangeCalls, sameHeapRanges);
		if (overlapRanges) InterlockedExchangeAdd64(&copyOverlapRangeCalls, overlapRanges);
		if (sourceFound) InterlockedExchangeAdd64(&copySourceFoundSlots, sourceFound);
		if (sourceMissing) InterlockedExchangeAdd64(&copySourceMissingSlots, sourceMissing);
		if (changed) InterlockedExchangeAdd64(&copyChangedSlots, changed);
		if (unchanged) InterlockedExchangeAdd64(&copyUnchangedSlots, unchanged);
		if (erased) InterlockedExchangeAdd64(&copyErasedSlots, erased);
		if (alreadyEmpty) InterlockedExchangeAdd64(&copyAlreadyEmptySlots, alreadyEmpty);
		if (memoHits) InterlockedExchangeAdd64(&copyMemoHits, memoHits);
		if (memoMisses) InterlockedExchangeAdd64(&copyMemoMisses, memoMisses);
		if (repeatHits) InterlockedExchangeAdd64(&copyRepeatHits, repeatHits);
	}

	static void LogAndReset()
	{
		const LONG64 frequency = Frequency();
		// Merge the per-thread copy counters into the frame globals once per
		// frame (the hot-path Record* methods only touch their own TLS block).
		MergeTlsCountersLocked();
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
		const LONG64 ranges = InterlockedExchange64(&copyRangeCalls, 0);
		const LONG64 copiedSlots = InterlockedExchange64(&copySlots, 0);
		const LONG64 singleRanges = InterlockedExchange64(&copySingleRangeCalls, 0);
		const LONG64 multiRanges = InterlockedExchange64(&copyMultiRangeCalls, 0);
		const LONG64 resolvedRanges = InterlockedExchange64(&copyResolvedRangeCalls, 0);
		const LONG64 sameHeapRanges = InterlockedExchange64(&copySameHeapRangeCalls, 0);
		const LONG64 overlapRanges = InterlockedExchange64(&copyOverlapRangeCalls, 0);
		const LONG64 sourceFound = InterlockedExchange64(&copySourceFoundSlots, 0);
		const LONG64 sourceMissing = InterlockedExchange64(&copySourceMissingSlots, 0);
		const LONG64 changed = InterlockedExchange64(&copyChangedSlots, 0);
		const LONG64 unchanged = InterlockedExchange64(&copyUnchangedSlots, 0);
		const LONG64 erased = InterlockedExchange64(&copyErasedSlots, 0);
		const LONG64 alreadyEmpty = InterlockedExchange64(&copyAlreadyEmptySlots, 0);
		const LONG64 memoHits = InterlockedExchange64(&copyMemoHits, 0);
		const LONG64 memoMisses = InterlockedExchange64(&copyMemoMisses, 0);
		const LONG64 repeatHits = InterlockedExchange64(&copyRepeatHits, 0);
		const LONG64 heapCacheHitCount = InterlockedExchange64(&heapCacheHits, 0);
		const LONG64 typeCbvSrvUav = InterlockedExchange64(&copyCallsCbvSrvUav, 0);
		const LONG64 typeSampler = InterlockedExchange64(&copyCallsSampler, 0);
		const LONG64 typeRtv = InterlockedExchange64(&copyCallsRtv, 0);
		const LONG64 typeDsv = InterlockedExchange64(&copyCallsDsv, 0);
		const LONG64 slotAllocationsCount = InterlockedExchange64(&slotAllocations, 0);
		const LONG64 slotAllocationBytesCount = InterlockedExchange64(&slotAllocationBytes, 0);
		if (heapSharedContentions == 0 && heapExclusiveContentions == 0 &&
			globalSharedContentions == 0 && globalExclusiveContentions == 0 &&
			misses == 0 && failures == 0 && fallbacks == 0 && ranges == 0)
			return;

		DX12LogManager::Fields("DX12DescriptorShadowTiming",
			"\"present\":%ld,\"resolveMisses\":%lld,\"resolveFailures\":%lld,"
			"\"fallbackCalls\":%lld,\"fallbackSlots\":%lld,"
			"\"copyRangeCalls\":%lld,\"copySlots\":%lld,"
			"\"copySingleRangeCalls\":%lld,\"copyMultiRangeCalls\":%lld,"
			"\"copyResolvedRangeCalls\":%lld,\"copySameHeapRangeCalls\":%lld,"
			"\"copyOverlapRangeCalls\":%lld,\"copySourceFoundSlots\":%lld,"
			"\"copySourceMissingSlots\":%lld,\"copyChangedSlots\":%lld,"
			"\"copyUnchangedSlots\":%lld,\"copyErasedSlots\":%lld,"
			"\"copyAlreadyEmptySlots\":%lld,"
			"\"copyMemoHits\":%lld,\"copyMemoMisses\":%lld,\"copyRepeatHits\":%lld,\"heapCacheHits\":%lld,"
			"\"copyTypeCbvSrvUav\":%lld,\"copyTypeSampler\":%lld,\"copyTypeRtv\":%lld,\"copyTypeDsv\":%lld,"
			"\"slotAllocations\":%lld,\"slotAllocationBytes\":%lld,"
			"\"heapSharedContentions\":%lld,\"heapSharedWaitUs\":%lld,\"heapSharedMaxUs\":%lld,"
			"\"heapExclusiveContentions\":%lld,\"heapExclusiveWaitUs\":%lld,\"heapExclusiveMaxUs\":%lld,"
			"\"globalSharedContentions\":%lld,\"globalSharedWaitUs\":%lld,\"globalSharedMaxUs\":%lld,"
			"\"globalExclusiveContentions\":%lld,\"globalExclusiveWaitUs\":%lld,\"globalExclusiveMaxUs\":%lld",
			DX12RuntimeState::GetPresentCount(),
			static_cast<long long>(misses),
			static_cast<long long>(failures),
			static_cast<long long>(fallbacks),
			static_cast<long long>(slots),
			static_cast<long long>(ranges),
			static_cast<long long>(copiedSlots),
			static_cast<long long>(singleRanges),
			static_cast<long long>(multiRanges),
			static_cast<long long>(resolvedRanges),
			static_cast<long long>(sameHeapRanges),
			static_cast<long long>(overlapRanges),
			static_cast<long long>(sourceFound),
			static_cast<long long>(sourceMissing),
			static_cast<long long>(changed),
			static_cast<long long>(unchanged),
			static_cast<long long>(erased),
			static_cast<long long>(alreadyEmpty),
			static_cast<long long>(memoHits),
			static_cast<long long>(memoMisses),
			static_cast<long long>(repeatHits),
			static_cast<long long>(heapCacheHitCount),
			static_cast<long long>(typeCbvSrvUav),
			static_cast<long long>(typeSampler),
			static_cast<long long>(typeRtv),
			static_cast<long long>(typeDsv),
			static_cast<long long>(slotAllocationsCount),
			static_cast<long long>(slotAllocationBytesCount),
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
	static void RecordCopyRange(UINT, bool, bool, bool) {}
	static void RecordCopyTypeCall(D3D12_DESCRIPTOR_HEAP_TYPE) {}
	static void RecordSlotAllocation(UINT64, UINT64) {}
	static void RecordCopyResults(UINT, UINT, UINT, UINT, UINT, UINT) {}
	static void RecordCopyMemo(bool) {}
	static void RecordHeapCacheHit() {}
	static void RecordCopyBatch(
		LONG64, LONG64, LONG64, LONG64, LONG64, LONG64, LONG64,
		LONG64, LONG64, LONG64, LONG64, LONG64, LONG64, LONG64, LONG64,
		LONG64) {}
	static void LogAndReset() {}
};

#endif
