#pragma once

#include "DX12Foundation.hpp"

// Consolidated DirectX 12 interface layer.
// Sections retain their original file names in #line diagnostics.

// ---- DX12CreateTimingTelemetryUtils.hpp ----
#line 1 "DX12CreateTimingTelemetryUtils.hpp"
#include <Windows.h>

#if defined(_DEBUG)


class DX12CreateTimingTelemetryUtils
{
public:
	enum class Api : UINT
	{
		ResourceCommittedWrapper = 0,
		ResourcePlacedWrapper,
		ResourceReservedWrapper,
		ResourceCommittedHook,
		ResourcePlacedHook,
		ResourceReservedHook,
		DescriptorHeapWrapper,
		DescriptorHeapHook,
		GraphicsPsoWrapper,
		ComputePsoWrapper,
		PsoStreamWrapper,
		GraphicsPsoHook,
		ComputePsoHook,
		PsoStreamHook,
		RootSignatureWrapper,
		RootSignatureHook,
		DescriptorCbvWrapper,
		DescriptorCbvHook,
		DescriptorSrvWrapper,
		DescriptorSrvHook,
		DescriptorUavWrapper,
		DescriptorUavHook,
		DescriptorRtvWrapper,
		DescriptorRtvHook,
		DescriptorDsvWrapper,
		DescriptorDsvHook,
		SamplerWrapper,
		SamplerHook,
		CopyDescriptorsWrapper,
		CopyDescriptorsHook,
		CopyDescriptorsSimpleWrapper,
		CopyDescriptorsSimpleHook,
		CommandQueueWrapper,
		CommandQueueHook,
		CommandAllocatorWrapper,
		CommandAllocatorHook,
		CommandListWrapper,
		CommandListHook,
		FenceWrapper,
		FenceHook,
		HeapWrapper,
		QueryHeapWrapper,
		CommandSignatureWrapper,
		PipelineLibraryWrapper,
		DeviceMiscWrapper,
		DeviceResidencyWrapper,
		ResourceMapWrapper,
		ResourceMapHook,
		ResourceUnmapWrapper,
		ResourceUnmapHook,
		ResourceReadWriteWrapper,
		ResourceQueryWrapper,
		DeviceQueryWrapper,
		WrapperLifetime,
		RootSignatureSerializeProxy,
		ProxyDeviceCreate,
		ProxyMisc,
		ResourceInvalidate,
		DescriptorHeapInvalidate,
		Count
	};

	enum class Stage : UINT
	{
		Prepare = 0,
		Original,
		Record,
		Wrap,
		Count
	};

	static int WallBucket(Api api)
	{
		switch (api) {
		case Api::ResourceCommittedWrapper:
		case Api::ResourcePlacedWrapper:
		case Api::ResourceReservedWrapper:
		case Api::ResourceCommittedHook:
		case Api::ResourcePlacedHook:
		case Api::ResourceReservedHook:
			return DX12OuterTimingProbeUtils::ApiDeviceResourceCreate;
		case Api::DescriptorHeapWrapper:
		case Api::DescriptorHeapHook:
			return DX12OuterTimingProbeUtils::ApiDeviceDescriptorHeapCreate;
		case Api::GraphicsPsoWrapper:
		case Api::ComputePsoWrapper:
		case Api::PsoStreamWrapper:
		case Api::GraphicsPsoHook:
		case Api::ComputePsoHook:
		case Api::PsoStreamHook:
			return DX12OuterTimingProbeUtils::ApiDevicePsoCreate;
		case Api::RootSignatureWrapper:
		case Api::RootSignatureHook:
		case Api::RootSignatureSerializeProxy:
			return DX12OuterTimingProbeUtils::ApiDeviceRootSignatureCreate;
		case Api::DescriptorCbvWrapper:
		case Api::DescriptorCbvHook:
			return DX12OuterTimingProbeUtils::ApiDeviceCreateCbv;
		case Api::DescriptorSrvWrapper:
		case Api::DescriptorSrvHook:
			return DX12OuterTimingProbeUtils::ApiDeviceCreateSrv;
		case Api::DescriptorUavWrapper:
		case Api::DescriptorUavHook:
			return DX12OuterTimingProbeUtils::ApiDeviceCreateUav;
		case Api::DescriptorRtvWrapper:
		case Api::DescriptorRtvHook:
			return DX12OuterTimingProbeUtils::ApiDeviceCreateRtv;
		case Api::DescriptorDsvWrapper:
		case Api::DescriptorDsvHook:
			return DX12OuterTimingProbeUtils::ApiDeviceCreateDsv;
		case Api::SamplerWrapper:
		case Api::SamplerHook:
			return DX12OuterTimingProbeUtils::ApiDeviceCreateSampler;
		case Api::CopyDescriptorsWrapper:
		case Api::CopyDescriptorsHook:
			return DX12OuterTimingProbeUtils::ApiDeviceCopyDescriptors;
		case Api::CopyDescriptorsSimpleWrapper:
		case Api::CopyDescriptorsSimpleHook:
			return DX12OuterTimingProbeUtils::ApiDeviceCopyDescriptorsSimple;
		case Api::CommandQueueWrapper:
		case Api::CommandQueueHook:
		case Api::CommandAllocatorWrapper:
		case Api::CommandAllocatorHook:
		case Api::CommandListWrapper:
		case Api::CommandListHook:
			return DX12OuterTimingProbeUtils::ApiDeviceCommandCreate;
		case Api::FenceWrapper:
		case Api::FenceHook:
			return DX12OuterTimingProbeUtils::ApiDeviceFenceCreate;
		case Api::HeapWrapper:
			return DX12OuterTimingProbeUtils::ApiDeviceHeapCreate;
		case Api::QueryHeapWrapper:
			return DX12OuterTimingProbeUtils::ApiDeviceQueryHeapCreate;
		case Api::CommandSignatureWrapper:
			return DX12OuterTimingProbeUtils::ApiDeviceCommandSignatureCreate;
		case Api::PipelineLibraryWrapper:
			return DX12OuterTimingProbeUtils::ApiDevicePipelineLibraryCreate;
		case Api::DeviceResidencyWrapper:
			return DX12OuterTimingProbeUtils::ApiDeviceResidency;
		case Api::ResourceMapWrapper:
		case Api::ResourceMapHook:
			return DX12OuterTimingProbeUtils::ApiResourceMap;
		case Api::ResourceUnmapWrapper:
		case Api::ResourceUnmapHook:
			return DX12OuterTimingProbeUtils::ApiResourceUnmap;
		case Api::ResourceReadWriteWrapper:
			return DX12OuterTimingProbeUtils::ApiResourceReadWrite;
		case Api::ResourceQueryWrapper:
			return DX12OuterTimingProbeUtils::ApiResourceQuery;
		case Api::DeviceQueryWrapper:
			return DX12OuterTimingProbeUtils::ApiDeviceQuery;
		case Api::ProxyDeviceCreate:
		case Api::ProxyMisc:
			return DX12OuterTimingProbeUtils::ApiDeviceMisc;
		case Api::WrapperLifetime:
		case Api::ResourceInvalidate:
		case Api::DescriptorHeapInvalidate:
			return DX12OuterTimingProbeUtils::ApiObjectLifetime;
		default:
			return DX12OuterTimingProbeUtils::ApiDeviceMisc;
		}
	}

	class Accumulator
	{
	public:
		volatile LONG calls = 0;
		volatile LONG64 totalTicks = 0;
		volatile LONG64 maxTicks = 0;
	};

	class ScopedTimer
	{
	public:
		Api api;
		Stage stage;
		LARGE_INTEGER begin = {};
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope;

		ScopedTimer(Api apiValue, Stage stageValue)
			: api(apiValue), stage(stageValue), apiWallScope(WallBucket(apiValue))
		{
			QueryPerformanceCounter(&begin);
		}

		~ScopedTimer()
		{
			LARGE_INTEGER end = {};
			QueryPerformanceCounter(&end);
			Record(api, stage, end.QuadPart - begin.QuadPart);
		}
	};

	inline static Accumulator accumulators[
		static_cast<UINT>(Api::Count)][static_cast<UINT>(Stage::Count)] = {};

	static LONG64 Frequency()
	{
		static const LONG64 frequency = []() {
			LARGE_INTEGER value = {};
			QueryPerformanceFrequency(&value);
			return value.QuadPart > 0 ? value.QuadPart : 1;
		}();
		return frequency;
	}

	static void Record(Api api, Stage stage, LONG64 ticks)
	{
		if (ticks < 0)
			ticks = 0;
		Accumulator &accumulator =
			accumulators[static_cast<UINT>(api)][static_cast<UINT>(stage)];
		InterlockedIncrement(&accumulator.calls);
		InterlockedExchangeAdd64(&accumulator.totalTicks, ticks);
		LONG64 observed = InterlockedCompareExchange64(&accumulator.maxTicks, 0, 0);
		while (ticks > observed) {
			const LONG64 previous = InterlockedCompareExchange64(
				&accumulator.maxTicks, ticks, observed);
			if (previous == observed)
				break;
			observed = previous;
		}
	}

	static const char *ApiName(Api api)
	{
		static const char *names[] = {
			"ResourceCommittedWrapper", "ResourcePlacedWrapper",
			"ResourceReservedWrapper", "ResourceCommittedHook",
			"ResourcePlacedHook", "ResourceReservedHook",
			"DescriptorHeapWrapper", "DescriptorHeapHook",
			"GraphicsPsoWrapper", "ComputePsoWrapper", "PsoStreamWrapper",
			"GraphicsPsoHook", "ComputePsoHook", "PsoStreamHook",
			"RootSignatureWrapper", "RootSignatureHook",
			"DescriptorCbvWrapper", "DescriptorCbvHook",
			"DescriptorSrvWrapper", "DescriptorSrvHook",
			"DescriptorUavWrapper", "DescriptorUavHook",
			"DescriptorRtvWrapper", "DescriptorRtvHook",
			"DescriptorDsvWrapper", "DescriptorDsvHook",
			"SamplerWrapper", "SamplerHook",
			"CopyDescriptorsWrapper", "CopyDescriptorsHook",
			"CopyDescriptorsSimpleWrapper", "CopyDescriptorsSimpleHook",
			"CommandQueueWrapper", "CommandQueueHook",
			"CommandAllocatorWrapper", "CommandAllocatorHook",
			"CommandListWrapper", "CommandListHook",
			"FenceWrapper", "FenceHook", "HeapWrapper", "QueryHeapWrapper",
			"CommandSignatureWrapper", "PipelineLibraryWrapper", "DeviceMiscWrapper",
			"DeviceResidencyWrapper",
			"ResourceMapWrapper", "ResourceMapHook",
			"ResourceUnmapWrapper", "ResourceUnmapHook",
			"ResourceReadWriteWrapper", "ResourceQueryWrapper", "DeviceQueryWrapper",
			"WrapperLifetime", "RootSignatureSerializeProxy", "ProxyDeviceCreate",
			"ProxyMisc", "ResourceInvalidate",
			"DescriptorHeapInvalidate"
		};
		static_assert(ARRAYSIZE(names) == static_cast<UINT>(Api::Count),
			"DX12 detailed timing API name table is out of sync");
		return names[static_cast<UINT>(api)];
	}

	static const char *StageName(Stage stage)
	{
		static const char *names[] = { "prepare", "original", "record", "wrap" };
		return names[static_cast<UINT>(stage)];
	}

	static void LogAndReset()
	{
		const LONG64 frequency = Frequency();
		for (UINT apiIndex = 0; apiIndex < static_cast<UINT>(Api::Count); ++apiIndex) {
			for (UINT stageIndex = 0; stageIndex < static_cast<UINT>(Stage::Count); ++stageIndex) {
				Accumulator &accumulator = accumulators[apiIndex][stageIndex];
				const LONG calls = InterlockedExchange(&accumulator.calls, 0);
				const LONG64 totalTicks = InterlockedExchange64(&accumulator.totalTicks, 0);
				const LONG64 maxTicks = InterlockedExchange64(&accumulator.maxTicks, 0);
				if (calls <= 0)
					continue;
				const LONG64 totalUs = totalTicks * 1000000 / frequency;
				const LONG64 maxUs = maxTicks * 1000000 / frequency;
				DX12LogManager::JsonFunc("DX12CreateTiming",
					"\"present\":%ld,\"api\":\"%s\",\"stage\":\"%s\","
					"\"calls\":%ld,\"totalUs\":%lld,\"avgUs\":%lld,\"maxUs\":%lld",
					DX12RuntimeState::GetPresentCount(),
					ApiName(static_cast<Api>(apiIndex)),
					StageName(static_cast<Stage>(stageIndex)),
					calls,
					static_cast<long long>(totalUs),
					static_cast<long long>(totalUs / calls),
					static_cast<long long>(maxUs));
			}
		}
	}
};

#define DX12_CREATE_TIMING_JOIN_INNER(a, b) a##b
#define DX12_CREATE_TIMING_JOIN(a, b) DX12_CREATE_TIMING_JOIN_INNER(a, b)
#define DX12_CREATE_TIMING_SCOPE(api, stage) \
	DX12CreateTimingTelemetryUtils::ScopedTimer \
		DX12_CREATE_TIMING_JOIN(_dx12CreateTimer_, __LINE__)( \
			DX12CreateTimingTelemetryUtils::Api::api, \
			DX12CreateTimingTelemetryUtils::Stage::stage)

#else

#define DX12_CREATE_TIMING_SCOPE(api, stage) do {} while (0)

#endif

// ---- DX12DescriptorShadowTelemetryUtils.hpp ----
#line 1 "DX12DescriptorShadowTelemetryUtils.hpp"
#include <Windows.h>

#if defined(_DEBUG)


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

	static void RecordCopyRange(
		UINT count, bool resolved, bool sameHeap, bool overlaps)
	{
		InterlockedIncrement64(&copyRangeCalls);
		InterlockedExchangeAdd64(&copySlots, count);
		if (count == 1)
			InterlockedIncrement64(&copySingleRangeCalls);
		else
			InterlockedIncrement64(&copyMultiRangeCalls);
		if (resolved)
			InterlockedIncrement64(&copyResolvedRangeCalls);
		if (sameHeap)
			InterlockedIncrement64(&copySameHeapRangeCalls);
		if (overlaps)
			InterlockedIncrement64(&copyOverlapRangeCalls);
	}

	static void RecordCopyResults(
		UINT sourceFound, UINT sourceMissing,
		UINT changed, UINT unchanged,
		UINT erased, UINT alreadyEmpty)
	{
		if (sourceFound)
			InterlockedExchangeAdd64(&copySourceFoundSlots, sourceFound);
		if (sourceMissing)
			InterlockedExchangeAdd64(&copySourceMissingSlots, sourceMissing);
		if (changed)
			InterlockedExchangeAdd64(&copyChangedSlots, changed);
		if (unchanged)
			InterlockedExchangeAdd64(&copyUnchangedSlots, unchanged);
		if (erased)
			InterlockedExchangeAdd64(&copyErasedSlots, erased);
		if (alreadyEmpty)
			InterlockedExchangeAdd64(&copyAlreadyEmptySlots, alreadyEmpty);
	}

	static void RecordCopyMemo(bool hit)
	{
		InterlockedIncrement64(hit ? &copyMemoHits : &copyMemoMisses);
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
		LONG64 memoHits, LONG64 memoMisses)
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
		if (heapSharedContentions == 0 && heapExclusiveContentions == 0 &&
			globalSharedContentions == 0 && globalExclusiveContentions == 0 &&
			misses == 0 && failures == 0 && fallbacks == 0 && ranges == 0)
			return;

		DX12LogManager::JsonFunc("DX12DescriptorShadowTiming",
			"\"present\":%ld,\"resolveMisses\":%lld,\"resolveFailures\":%lld,"
			"\"fallbackCalls\":%lld,\"fallbackSlots\":%lld,"
			"\"copyRangeCalls\":%lld,\"copySlots\":%lld,"
			"\"copySingleRangeCalls\":%lld,\"copyMultiRangeCalls\":%lld,"
			"\"copyResolvedRangeCalls\":%lld,\"copySameHeapRangeCalls\":%lld,"
			"\"copyOverlapRangeCalls\":%lld,\"copySourceFoundSlots\":%lld,"
			"\"copySourceMissingSlots\":%lld,\"copyChangedSlots\":%lld,"
			"\"copyUnchangedSlots\":%lld,\"copyErasedSlots\":%lld,"
			"\"copyAlreadyEmptySlots\":%lld,"
			"\"copyMemoHits\":%lld,\"copyMemoMisses\":%lld,"
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
	static void RecordCopyResults(UINT, UINT, UINT, UINT, UINT, UINT) {}
	static void RecordCopyMemo(bool) {}
	static void RecordCopyBatch(
		LONG64, LONG64, LONG64, LONG64, LONG64, LONG64, LONG64,
		LONG64, LONG64, LONG64, LONG64, LONG64, LONG64, LONG64, LONG64) {}
	static void LogAndReset() {}
};

#endif

// ---- DX12HotPathTimingUtils.hpp ----
#line 1 "DX12HotPathTimingUtils.hpp"
#include <Windows.h>

#include <atomic>


// ── Hot-path stage timing ───────────────────────────────────────────
// Thread-local accumulators. QPC-based. Logged per-frame at Present.
// Detailed per-call QPC timing is opt-in, including in debug builds.
// Define DX12_ENABLE_DETAILED_TIMING when collecting timing diagnostics.
//
// Usage:
//   DX12_HOTPATH_TIME_BEGIN(DrawHook);
//   ... work ...
//   DX12_HOTPATH_TIME_END(DrawHook);

#if defined(_DEBUG) && defined(DX12_ENABLE_DETAILED_TIMING)

#define DX12_HOTPATH_TIME_ENABLED 1

#if !DX12_HOTPATH_TIME_ENABLED

// Empty inline stubs – compiler eliminates all calls
__forceinline void DX12HotPathTimeBegin(DX12HotPathStage::Id) {}
__forceinline void DX12HotPathTimeEnd(DX12HotPathStage::Id) {}
__forceinline void DX12HotPathTimeLogAndResetAll() {}
__forceinline void DX12HotPathTimeEnsureFreq() {}

#else

// ── Stage enum ─────────────────────────────────────────────────────
class DX12HotPathStage
{
public:
	enum Id : UINT
	{
		DrawHookTotal = 0,      // DX12DrawHookFlow::ExecuteHook end-to-end
		DrawOverrideTotal,      // HandleDrawOverrides total
		DrawPsoEarlyExit,       // PSO-level early exit check
		DrawBuildIaHash,        // BuildIaHashState (IB+VB hash resolution)
		DrawPsoIaNegCache,      // PSO+IA negative cache check
		DrawModPrepareOverride, // DX12ModPrepareShaderOverrideReplacement
		DrawApplyAndExecute,    // ApplyAndExecute replacement
		DispatchHookTotal,      // DX12DispatchHookFlow::ExecuteHook
		ResourceBarrierTotal,   // ResourceBarrier wrapper path
		BarrierUnwrap,          // ResourceBarrier unwrap phase
		BarrierCapture,         // ResourceBarrier state capture
		SetPipelineStateTotal,  // SetPipelineState wrapper path
		DescriptorHeapsTotal,   // SetDescriptorHeaps wrapper path
		RootDescTableTotal,     // SetGraphicsRootDescriptorTable + SetComputeRootDescriptorTable
		RootSignatureTotal,     // SetGraphicsRootSignature + SetComputeRootSignature
		IaSetTopologyTotal,     // IASetPrimitiveTopology wrapper path
		UnwrapObjTotal,         // UnwrapPipelineState / UnwrapResource (gIdentityMap lookup)
		StatePtrLookupTotal,    // GetStatePtrFast (gDirectStateMap lookup)
		PsoCacheLookupTotal,    // PSO replacement cache check (RuntimeState read)
		PreSkinBindQuery,        // GetCurrentComputeAll (BindingTracker query)
		PreSkinMatch,            // FindProducer matching (UAV hash lookup)
		PreSkinApply,            // ApplyPreSkinDescriptorTablePatchLocked
		ExecuteCLTotal,          // ExecuteCommandLists wrapper path
		PresentTotal,            // Present wrapper (excl. PresentWait)
		PreSkinLoadResource,     // EnsureLoadedResourceForPreSkin + EnsureResourceSrvLocked
		PreSkinRingAlloc,        // EnsurePreSkinDescriptorRingLocked (CreateDescriptorHeap)
		PreSkinCopyPatch,        // CopyDescriptorsSimple + SRV patch + CBV patch
		PreSkinBindHeaps,        // SetDescriptorHeaps + SetComputeRootDescriptorTable
		BindSnapshot,            // SnapshotBindingState (copy entire binding state)
		BindCollect,             // CollectComputeAll (iterate + collect descriptors)
		PresentModBegin,         // DX12ModBeginFrame
		PresentUpdate,           // DX12HotPathState::Update after Present
		ExecuteCLNotify,         // DX12ModNotifyCommandListsSubmitted
		WrapperMiscTotal,        // All unprobed wrapper methods (Copy*, Clear*, etc.)

			CloseCmdListTotal,
			ResetCmdListTotal,
			ClearStateTotal,
			CopyResourceOpTotal,
			RSSetViewportsTotal,
			RSSetScissorRectsTotal,
			OMSetBlendFactorTotal,
			OMSetStencilRefTotal,
			ExecuteBundleTotal,
			SetComputeCBVTotal,
			SetGraphicsCBVTotal,
			SetComputeSRVTotal,
			SetGraphicsSRVTotal,
			SetComputeUAVTotal,
			SetGraphicsUAVTotal,
			IASetIndexBufTotal,
			IASetVertexBufTotal,
			SOSetTargetsTotal,
			DiscardResourceTotal,
			BeginQueryTotal,
			EndQueryTotal,
			SetMarkerTotal,
			BeginEventTotal,
			EndEventTotal,
			OMSetDepthBoundsTotal,
			SetViewInstanceMaskTotal,
			SetProtectedSessionTotal,
			EndRenderPassTotal,
			SetPSO1Total,
			DispatchRaysTotal,
			RSSetShadingRateTotal,
			RSSetShadingRateImgTotal,
			DispatchMeshTotal,
			OMSetFrontBackStencilTotal,
			RSSetDepthBiasTotal,
			IASetIBStripCutTotal,
			SetProgramTotal,
			DispatchGraphTotal,
		STAGE_COUNT
	};

	static const char *Name(Id id)
	{
		static const char *names[] = {
			"DrawHook", "DrawOverride", "PsoEarlyExit", "BuildIaHash",
			"PsoIaNegCache", "ModPrepareOverride", "ApplyAndExecute",
			"DispatchHook", "ResourceBarrier", "BarrierUnwrap", "BarrierCapture",
			"SetPipelineState", "DescriptorHeaps",
			"RootDescTable", "RootSignature", "IaSetTopology",
			"UnwrapObj", "StatePtrLookup", "PsoCacheLookup",
			"PreSkinQuery", "PreSkinMatch", "PreSkinApply",
			"ExecuteCL", "Present",
			"PreSkinLoad", "PreSkinRing", "PreSkinCopy", "PreSkinBind",
			"BindSnapshot", "BindCollect",
			"PresentModBegin", "PresentUpdate", "ExecuteCLNotify",
			"WrapperMisc",
				"CloseCmdList",
				"ResetCmdList",
				"ClearState",
				"CopyResourceOp",
				"RSSetViewports",
				"RSSetScissorRects",
				"OMSetBlendFactor",
				"OMSetStencilRef",
				"ExecuteBundle",
				"SetComputeCBV",
				"SetGraphicsCBV",
				"SetComputeSRV",
				"SetGraphicsSRV",
				"SetComputeUAV",
				"SetGraphicsUAV",
				"IASetIndexBuf",
				"IASetVertexBuf",
				"SOSetTargets",
				"DiscardResource",
				"BeginQuery",
				"EndQuery",
				"SetMarker",
				"BeginEvent",
				"EndEvent",
				"OMSetDepthBounds",
				"SetViewInstanceMask",
				"SetProtectedSession",
				"EndRenderPass",
				"SetPSO1",
				"DispatchRays",
				"RSSetShadingRate",
				"RSSetShadingRateImg",
				"DispatchMesh",
				"OMSetFrontBackStencil",
				"RSSetDepthBias",
				"IASetIBStripCut",
				"SetProgram",
				"DispatchGraph",
		};
		return (id < STAGE_COUNT) ? names[id] : "?";
	}
};

// ── Per-stage thread-local accumulator ──────────────────────────────
class DX12HotPathTimingUtils
{
public:
	struct Accumulator
	{
		LARGE_INTEGER qpcSum;   // sum of (end - begin) in QPC ticks
		LONG callCount;
		LONG maxUs;             // max single invocation in microseconds
	};

	inline static thread_local Accumulator tlsAccum[DX12HotPathStage::STAGE_COUNT];
	inline static thread_local LARGE_INTEGER tlsBegin[DX12HotPathStage::STAGE_COUNT];
	inline static thread_local LARGE_INTEGER tlsFreq = {};
	inline static thread_local bool tlsFreqInit = false;

	static void EnsureFreq()
	{
		if (!tlsFreqInit) {
			QueryPerformanceFrequency(&tlsFreq);
			tlsFreqInit = true;
		}
	}

	static void Begin(DX12HotPathStage::Id stage)
	{
		EnsureFreq();
		QueryPerformanceCounter(&tlsBegin[stage]);
	}

	static void End(DX12HotPathStage::Id stage)
	{
		LARGE_INTEGER now;
		QueryPerformanceCounter(&now);
		LONG64 ticks = now.QuadPart - tlsBegin[stage].QuadPart;
		if (ticks < 0)
			ticks = 0;
		Accumulator &acc = tlsAccum[stage];
		acc.qpcSum.QuadPart += ticks;
		InterlockedIncrement(&acc.callCount);
		LONG us = static_cast<LONG>((ticks * 1000000) / tlsFreq.QuadPart);
		// non-atomic write: value used only for logging
		if (us > acc.maxUs)
			acc.maxUs = us;
	}

	// ── Snapshot + reset: called at frame end (Present time) ─────────
	static void LogAndResetAll()
	{
		DX12CreateTimingTelemetryUtils::LogAndReset();
		DX12DescriptorShadowTelemetryUtils::LogAndReset();
		DX12ResidentMaintenanceTelemetryUtils::LogAndReset();

		for (UINT i = 0; i < DX12HotPathStage::STAGE_COUNT; ++i) {
			LONG calls = tlsAccum[i].callCount;
			if (calls <= 0)
				continue;
			LONG64 totalUs = (tlsAccum[i].qpcSum.QuadPart * 1000000) / tlsFreq.QuadPart;
			LONG avgUs = static_cast<LONG>(totalUs / calls);
			DX12LogManager::JsonFunc("DX12HotPathTiming",
				"\"stage\":\"%s\",\"calls\":%ld,\"totalUs\":%lld,\"avgUs\":%ld,\"maxUs\":%ld",
				DX12HotPathStage::Name(static_cast<DX12HotPathStage::Id>(i)),
				calls,
				static_cast<long long>(totalUs),
				avgUs,
				tlsAccum[i].maxUs);
			tlsAccum[i] = {};
		}
	}

	// ── RAII scoped timer ───────────────────────────────────────────
	class ScopedTimer
	{
	public:
		DX12HotPathStage::Id stage;

		explicit ScopedTimer(DX12HotPathStage::Id s) : stage(s)
		{
			Begin(stage);
		}

		~ScopedTimer()
		{
			End(stage);
		}
	};
};

#define DX12_HOTPATH_TIME_BEGIN(stage) \
	DX12HotPathTimingUtils::Begin(DX12HotPathStage::stage)

#define DX12_HOTPATH_TIME_END(stage) \
	DX12HotPathTimingUtils::End(DX12HotPathStage::stage)

#define DX12_HOTPATH_TIME_SCOPE(stage) \
	DX12HotPathTimingUtils::ScopedTimer __hotpathTimer##__LINE__(DX12HotPathStage::stage)

#define DX12_HOTPATH_LOG_AND_RESET() \
	DX12HotPathTimingUtils::LogAndResetAll()

#endif // !DX12_HOTPATH_TIME_ENABLED

#else // !_DEBUG

#define DX12_HOTPATH_TIME_ENABLED 0
#define DX12_HOTPATH_TIME_BEGIN(stage) do {} while (0)
#define DX12_HOTPATH_TIME_END(stage) do {} while (0)
#define DX12_HOTPATH_TIME_SCOPE(stage) do {} while (0)
#define DX12_HOTPATH_LOG_AND_RESET() do {} while (0)

#endif

// ---- DX12DispatchHookFlow.hpp ----
#line 1 "DX12DispatchHookFlow.hpp"
#include <d3d12.h>

#include <cstring>
#include <unordered_map>
#include <vector>


#if defined(_DEBUG)
#define DX12_DISPATCH_DEBUG_VERBOSE_LOG(stmt) do { stmt; } while (0)
#else
#define DX12_DISPATCH_DEBUG_VERBOSE_LOG(stmt) do {} while (0)
#endif

class DX12DispatchHookFlowDispatch
{
public:
	using Fn = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, UINT, UINT);
};

class DX12DispatchPreSkinProbe
{
public:
	std::vector<DX12CurrentComputeUavBinding> uavs;
	std::vector<DX12CurrentComputeUavBinding> srvs;
	std::vector<DX12CurrentComputeUavBinding> cbvs;
	std::vector<DX12CurrentRootConstants> rootConstants;
	const std::vector<DX12CurrentComputeUavBinding> *cachedUavs = nullptr;
	const std::vector<DX12CurrentComputeUavBinding> *cachedSrvs = nullptr;
	const std::vector<DX12CurrentComputeUavBinding> *cachedCbvs = nullptr;
	const std::vector<DX12CurrentRootConstants> *cachedRootConstants = nullptr;
	bool uavsFound = false;
	bool srvsFound = false;
	bool cbvsFound = false;
	bool rootConstantsFound = false;
	bool applied = false;
	bool shouldLog = false;
	uint64_t computeShaderHash = 0;
	UINT64 computeBindingSerial = 0;
	UINT64 bindingDependencyFingerprint = 0;
	UINT originalVertexCount = 0;
	UINT overrideVertexCount = 0;
	DX12PreSkinDispatchOverride dispatchOverride;
};

class DX12DispatchHookFlowGuard
{
public:
	bool &active;

	explicit DX12DispatchHookFlowGuard(bool &value) : active(value) { active = true; }
	~DX12DispatchHookFlowGuard() { active = false; }
	DX12DispatchHookFlowGuard(const DX12DispatchHookFlowGuard&) = delete;
	DX12DispatchHookFlowGuard &operator=(const DX12DispatchHookFlowGuard&) = delete;
};

class DX12DispatchHookFlow
{
public:
	inline static thread_local bool InDispatchHookFlow = false;

	static bool TryExecuteSkipAll(
		ID3D12GraphicsCommandList *commandList,
		UINT threadGroupCountX,
		UINT threadGroupCountY,
		UINT threadGroupCountZ,
		DX12DispatchHookFlowDispatch::Fn originalDispatch)
	{
		if (!DX12HotPathState::skipAll)
			return false;
		if (originalDispatch)
			originalDispatch(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
		return true;
	}

	static void ExecuteHook(
		ID3D12GraphicsCommandList *commandList,
		UINT threadGroupCountX,
		UINT threadGroupCountY,
		UINT threadGroupCountZ,
		DX12DispatchHookFlowDispatch::Fn originalDispatch)
	{
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12DispatchHookFlow::ExecuteHook(
			commandList,
			runtimeState,
			threadGroupCountX,
			threadGroupCountY,
			threadGroupCountZ,
			originalDispatch);
	}

	static void ExecuteHook(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeState *runtimeState,
		UINT threadGroupCountX,
		UINT threadGroupCountY,
		UINT threadGroupCountZ,
		DX12DispatchHookFlowDispatch::Fn originalDispatch)
	{
		if (!originalDispatch)
			return;

		DX12_HOTPATH_TIME_BEGIN(DispatchHookTotal);

		const UINT64 flagsWord = DX12HotPathState::HookWorkFlags;
		const bool hasShaderOverrides =
			(flagsWord & DX12HotPathState::FLAG_SHADER_OVERRIDES) != 0;
		const bool needsPreSkinProbe = (flagsWord & DX12HotPathState::FLAG_PRE_SKIN_PROBE) != 0;
		const bool huntEnabled = (flagsWord & DX12HotPathState::FLAG_HUNT_ENABLED) != 0;
		// No-candidate compute PSOs are resident state, just like DX11's current
		// shader. Bypass tracking-state lookup and all decision/cache machinery.
		if (runtimeState &&
			flagsWord == (DX12HotPathState::FLAG_NEEDS_MOD_WORK |
				DX12HotPathState::FLAG_SHADER_OVERRIDES) &&
			runtimeState->dispatchDecision.resolved &&
			runtimeState->dispatchDecision.pipelineState == runtimeState->pipelineState &&
			runtimeState->dispatchDecision.reloadGeneration == DX12ModGetReloadGenerationFast() &&
			runtimeState->dispatchDecision.MatchesGlobals(true, false) &&
			!runtimeState->dispatchDecision.NeedsShaderOverrideWork()) {
			DX12_PROFILE_FAST_FORWARD();
			originalDispatch(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
			DX12_HOTPATH_TIME_END(DispatchHookTotal);
			return;
		}
		const DX12CommandListTrackingState tracking =
			DX12CommandListStateCapture::GetTrackingState(commandList, runtimeState);
		if (DX12Profiling::ShouldCollectCounters() || hasShaderOverrides || needsPreSkinProbe)
			DX12RuntimeState::IncrementFrameDispatches();

		if (DX12InternalReplayState::IsActive()) {
			originalDispatch(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
			DX12_HOTPATH_TIME_END(DispatchHookTotal);
			return;
		}

		DX12CompiledDispatchDecision fallbackDecision;
		const DX12CompiledDispatchDecision *dispatchDecision =
			DX12CommandListRuntime::EnsureDispatchDecision(
				runtimeState, hasShaderOverrides, needsPreSkinProbe);
		if (!dispatchDecision) {
			ID3D12PipelineState *pipelineState =
				DX12CommandListRuntime::GetPipelineState(commandList);
			fallbackDecision = DX12CommandListRuntime::BuildDispatchDecision(
				pipelineState, hasShaderOverrides, needsPreSkinProbe);
			dispatchDecision = &fallbackDecision;
		}
		const bool decisionNeedsPreSkinProbe =
			needsPreSkinProbe && dispatchDecision && dispatchDecision->NeedsPreSkinProbe();
		const bool decisionNeedsShaderOverrideWork =
			hasShaderOverrides && dispatchDecision && dispatchDecision->NeedsShaderOverrideWork();
		const bool needsDispatchWork =
			decisionNeedsShaderOverrideWork || decisionNeedsPreSkinProbe;

		if (DX12HotPathState::skipBindings && huntEnabled && !needsDispatchWork) {
			DX12_PROFILE_FAST_FORWARD();
			DX12HuntRecordDispatch(commandList);
			if (DX12HuntShouldSkipDispatch(commandList)) {
				DX12_HOTPATH_TIME_END(DispatchHookTotal);
				return;
			}
			originalDispatch(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
			DX12_HOTPATH_TIME_END(DispatchHookTotal);
			return;
		}

		DX12CommandListFlowDiagnostics::LogDX12Call(
			"ID3D12GraphicsCommandList::Dispatch", commandList,
			" groups=%u,%u,%u", threadGroupCountX, threadGroupCountY, threadGroupCountZ);
		if (tracking.recordBindingEvents)
			DX12BindingTracker::RecordDispatch(
				commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
		if (huntEnabled) {
			DX12HuntRecordDispatch(commandList);
			if (DX12HuntShouldSkipDispatch(commandList)) {
				DX12_HOTPATH_TIME_END(DispatchHookTotal);
				return;
			}
		}
		if (!needsDispatchWork) {
			if (needsPreSkinProbe && dispatchDecision && dispatchDecision->IsPreSkinRejected())
				DX12ResidentMaintenanceTelemetryUtils::RecordDispatchDecisionFastReject();
			DX12_PROFILE_FAST_FORWARD();
			originalDispatch(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
			DX12_HOTPATH_TIME_END(DispatchHookTotal);
			return;
		}

		Execute(
			commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ,
			runtimeState, originalDispatch, dispatchDecision);
		DX12_HOTPATH_TIME_END(DispatchHookTotal);
	}

	static void Execute(
		ID3D12GraphicsCommandList *commandList,
		UINT threadGroupCountX,
		UINT threadGroupCountY,
		UINT threadGroupCountZ,
		DX12CommandListRuntimeState *runtimeState,
		DX12DispatchHookFlowDispatch::Fn originalDispatch,
		const DX12CompiledDispatchDecision *dispatchDecision = nullptr)
	{
		if (!originalDispatch)
			return;
		const UINT64 flagsWord = DX12HotPathState::HookWorkFlags;
		const bool hasShaderOverrides =
			(flagsWord & DX12HotPathState::FLAG_SHADER_OVERRIDES) != 0;
		const bool needsPreSkinProbe =
			(flagsWord & DX12HotPathState::FLAG_PRE_SKIN_PROBE) != 0;
		if (InDispatchHookFlow) {
			originalDispatch(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
			return;
		}
		bool shouldSkipDispatch = false;
		if (hasShaderOverrides) {
			if (dispatchDecision)
				shouldSkipDispatch = dispatchDecision->hasShaderOverrideSkip;
			else if (runtimeState)
				shouldSkipDispatch = DX12ModShouldSkipPipelineState(runtimeState->pipelineState, true);
		}
		if (shouldSkipDispatch)
			return;

		DX12DispatchHookFlowGuard guard(InDispatchHookFlow);

		// Design debt: this keeps old behavior for untracked command lists. The cleaner module seam is
		// to guarantee runtime state before entering dispatch flow, then remove this fallback entirely.
		DX12CommandListRuntimeState fallbackState = {};
		DX12CommandListRuntimeState *effectiveState = runtimeState;
		if (!effectiveState) {
#if defined(_DEBUG)
			DX12LogManager::Text("DX12DispatchHookFlowExecute: no runtimeState, using fallback\n");
#endif
			fallbackState.pipelineState = DX12CommandListRuntime::GetPipelineState(commandList);
			effectiveState = &fallbackState;
		}

		DX12CompiledDispatchDecision localDecision;
		const DX12CompiledDispatchDecision *effectiveDecision = dispatchDecision;
		if (!effectiveDecision && effectiveState) {
			localDecision = DX12CommandListRuntime::BuildDispatchDecision(
				effectiveState->pipelineState, hasShaderOverrides, needsPreSkinProbe);
			effectiveDecision = &localDecision;
		}
		const bool decisionNeedsPreSkinProbe =
			needsPreSkinProbe && effectiveDecision && effectiveDecision->NeedsPreSkinProbe();
		if (!needsPreSkinProbe || !effectiveState || !decisionNeedsPreSkinProbe) {
			if (needsPreSkinProbe && effectiveDecision && effectiveDecision->IsPreSkinRejected())
				DX12ResidentMaintenanceTelemetryUtils::RecordDispatchDecisionFastReject();
			originalDispatch(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
			return;
		}
		DX12ResidentMaintenanceTelemetryUtils::RecordDispatchDecisionPreSkinCandidateDispatch();

		DX12DispatchPreSkinProbe &probe = ScratchProbe();
		LARGE_INTEGER prepareProbeBegin = DX12OuterTimingProbeUtils::Now();
		PreparePreSkinProbe(commandList, effectiveState, &probe);
		const std::vector<DX12CurrentComputeUavBinding> &probeUavs =
			probe.cachedUavs ? *probe.cachedUavs : probe.uavs;
		const std::vector<DX12CurrentComputeUavBinding> &probeSrvs =
			probe.cachedSrvs ? *probe.cachedSrvs : probe.srvs;
		const std::vector<DX12CurrentComputeUavBinding> &probeCbvs =
			probe.cachedCbvs ? *probe.cachedCbvs : probe.cbvs;
		DX12OuterTimingProbeUtils::LogCpuStage(
			"PreSkin",
			"dispatch_prepare_probe",
			0,
			probeUavs.size() + probeSrvs.size() + probeCbvs.size(),
			DX12OuterTimingProbeUtils::ElapsedUs(prepareProbeBegin));
		UINT dispatchGroupX = threadGroupCountX;
		UINT dispatchGroupY = threadGroupCountY;
		UINT dispatchGroupZ = threadGroupCountZ;
		if (probe.applied && probe.dispatchOverride.handlingSkip) {
			if (probe.dispatchOverride.hasDispatch) {
				dispatchGroupX = probe.dispatchOverride.groupsX;
				dispatchGroupY = probe.dispatchOverride.groupsY;
				dispatchGroupZ = probe.dispatchOverride.groupsZ;
				LARGE_INTEGER dispatchBegin = DX12OuterTimingProbeUtils::Now();
				originalDispatch(commandList, dispatchGroupX, dispatchGroupY, dispatchGroupZ);
				DX12OuterTimingProbeUtils::LogCpuStage(
					"PreSkin",
					"dispatch_override_original",
					0,
					1,
					DX12OuterTimingProbeUtils::ElapsedUs(dispatchBegin));
			}
			LARGE_INTEGER finishBegin = DX12OuterTimingProbeUtils::Now();
			FinishPreSkinProbe(
				commandList,
				threadGroupCountX, threadGroupCountY, threadGroupCountZ,
				dispatchGroupX, dispatchGroupY, dispatchGroupZ, probe);
			DX12OuterTimingProbeUtils::LogCpuStage(
				"PreSkin",
				"dispatch_finish_probe",
				0,
				probeUavs.size(),
				DX12OuterTimingProbeUtils::ElapsedUs(finishBegin));
			return;
		}
		LARGE_INTEGER dispatchBegin = DX12OuterTimingProbeUtils::Now();
		originalDispatch(commandList, dispatchGroupX, dispatchGroupY, dispatchGroupZ);
		DX12OuterTimingProbeUtils::LogCpuStage(
			"PreSkin",
			"dispatch_original",
			0,
			1,
			DX12OuterTimingProbeUtils::ElapsedUs(dispatchBegin));
		LARGE_INTEGER finishBegin = DX12OuterTimingProbeUtils::Now();
		FinishPreSkinProbe(
			commandList,
			threadGroupCountX, threadGroupCountY, threadGroupCountZ,
			dispatchGroupX, dispatchGroupY, dispatchGroupZ, probe);
		DX12OuterTimingProbeUtils::LogCpuStage(
			"PreSkin",
			"dispatch_finish_probe",
			0,
			probeUavs.size(),
			DX12OuterTimingProbeUtils::ElapsedUs(finishBegin));
	}

	// Match the DX11 current-compute-shader model: the CS hash lives in the
	// runtime state and this fallback cache is only used outside wrapper mode.
	static uint64_t GetCachedCsHash(ID3D12PipelineState *pipelineState)
	{
		if (!pipelineState)
			return 0;
		// Fallback cache for command lists without an embedded runtime state.
		static std::unordered_map<ID3D12PipelineState*, uint64_t> cache;
		static UINT64 lastReloadGen = 0;
		UINT64 reloadGen = DX12ModGetReloadGeneration();
		if (reloadGen != lastReloadGen) {
			cache.clear();
			lastReloadGen = reloadGen;
		}
		std::unordered_map<ID3D12PipelineState*, uint64_t>::iterator it = cache.find(pipelineState);
		if (it != cache.end())
			return it->second;
		DX12PsoShaderInfo shaderInfo = {};
		if (!DX12GetPipelineStateShaderInfo(pipelineState, &shaderInfo) || !shaderInfo.hasCS)
			return 0;
		cache[pipelineState] = shaderInfo.cs;
		return shaderInfo.cs;
	}

	static bool HasConfiguredPreSkinCs(
		ID3D12GraphicsCommandList *commandList,
		const DX12CommandListRuntimeState *runtimeState)
	{
		ID3D12PipelineState *pipelineState = runtimeState ? runtimeState->pipelineState :
			DX12CommandListRuntime::GetPipelineState(commandList);
		if (!pipelineState)
			return false;
		uint64_t csHash = (runtimeState && runtimeState->cachedCsHashValid) ?
			runtimeState->cachedCsHash : GetCachedCsHash(pipelineState);
		return csHash != 0 && DX12ModShouldTrackPreSkinBindingsForCs(csHash);
	}

	static DX12DispatchPreSkinProbe &ScratchProbe()
	{
		static thread_local DX12DispatchPreSkinProbe probe;
		probe.uavs.clear();
		probe.srvs.clear();
		probe.cbvs.clear();
		probe.rootConstants.clear();
		probe.cachedUavs = nullptr;
		probe.cachedSrvs = nullptr;
		probe.cachedCbvs = nullptr;
		probe.cachedRootConstants = nullptr;
		probe.uavsFound = false;
		probe.srvsFound = false;
		probe.cbvsFound = false;
		probe.rootConstantsFound = false;
		probe.applied = false;
		probe.shouldLog = false;
		probe.computeShaderHash = 0;
		probe.computeBindingSerial = 0;
		probe.bindingDependencyFingerprint = 0;
		probe.originalVertexCount = 0;
		probe.overrideVertexCount = 0;
		probe.dispatchOverride = DX12PreSkinDispatchOverride();
		return probe;
	}

	static void LogPreSkinDispatchProbeReason(
		const char *reason,
		uint64_t computeShaderHash,
		UINT64 computeBindingSerial,
		size_t uavCount)
	{
#if defined(_DEBUG)
		if (reason && (!strcmp(reason, "uav_filter_miss") ||
		    !strcmp(reason, "cached_no_uav_match") ||
		    !strcmp(reason, "cs_not_configured"))) {
			return;
		}
		DX12LogManager::DebugJsonFunc("DX12PreSkinDispatchProbe",
			"\"reason\":\"%s\",\"cs\":\"%016llx\",\"bindingSerial\":%llu,\"uavs\":%zu",
			reason ? reason : "unknown",
			static_cast<unsigned long long>(computeShaderHash),
			static_cast<unsigned long long>(computeBindingSerial),
			uavCount);
#else
		(void)reason;
		(void)computeShaderHash;
		(void)computeBindingSerial;
		(void)uavCount;
#endif
	}

	static void PreparePreSkinProbe(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeState *runtimeState,
		DX12DispatchPreSkinProbe *probe)
	{
		if (!probe || !(DX12HotPathState::HookWorkFlags &
			DX12HotPathState::FLAG_PRE_SKIN_PROBE)) {
#if defined(_DEBUG)
			DX12LogManager::Text("PreparePreSkinProbe: exit needsProbe failed\n");
#endif
			return;
		}

		LARGE_INTEGER t0, t1, freq;
		QueryPerformanceCounter(&t0);

		// Match DX11 by reading the precomputed CS hash from RuntimeState.
		LARGE_INTEGER csHashBegin = DX12OuterTimingProbeUtils::Now();
		probe->computeShaderHash = (runtimeState && runtimeState->cachedCsHashValid) ?
			runtimeState->cachedCsHash :
			GetCachedCsHash(runtimeState ? runtimeState->pipelineState : nullptr);
		DX12OuterTimingProbeUtils::LogCpuStage(
			"PreSkin",
			"prepare_get_cs_hash",
			0,
			probe->computeShaderHash ? 1 : 0,
			DX12OuterTimingProbeUtils::ElapsedUs(csHashBegin));
		if (!probe->computeShaderHash) {
#if defined(_DEBUG)
			DX12LogManager::Text("PreparePreSkinProbe: exit no cs hash\n");
#endif
			LogPreSkinDispatchProbeReason("no_pipeline_cs_hash", 0, 0, 0);
			return;
		}
		probe->computeBindingSerial = runtimeState ? runtimeState->computeBindingSerial : 0;

		LARGE_INTEGER csConfigBegin = DX12OuterTimingProbeUtils::Now();
		if (!DX12ModShouldProbePreSkinningForCs(probe->computeShaderHash)) {
			DX12OuterTimingProbeUtils::LogCpuStage(
				"PreSkin",
				"prepare_cs_config_miss",
				0,
				1,
				DX12OuterTimingProbeUtils::ElapsedUs(csConfigBegin));
#if defined(_DEBUG)
			DX12LogManager::Text("PreparePreSkinProbe: exit cs not configured\n");
#endif
			LogPreSkinDispatchProbeReason(
				"cs_not_configured", probe->computeShaderHash, 0, 0);
			return;
		}
		DX12OuterTimingProbeUtils::LogCpuStage(
			"PreSkin",
			"prepare_cs_config_hit",
			0,
			1,
			DX12OuterTimingProbeUtils::ElapsedUs(csConfigBegin));

		probe->computeBindingSerial =
			DX12BindingTracker::GetComputeBindingSerial(commandList);
		if (runtimeState)
			runtimeState->computeBindingSerial = probe->computeBindingSerial;

		// Most candidate dispatches are repeated with no compute-binding or IA
		// mutation between them. A proven negative result is therefore reusable
		// before descriptor summaries are materialized. Positive results still go
		// through the exact-location path so replacement commands target the
		// currently bound descriptor table.
		const UINT64 earlyReloadGeneration = DX12ModGetReloadGenerationFast();
		if (runtimeState &&
			runtimeState->resolvedCompute.probeResultValid &&
			!runtimeState->resolvedCompute.probeApplied &&
			runtimeState->resolvedCompute.probeCsHash == probe->computeShaderHash &&
			runtimeState->resolvedCompute.probeReloadGeneration == earlyReloadGeneration &&
			runtimeState->resolvedCompute.probeBindingSerial == probe->computeBindingSerial &&
			runtimeState->resolvedCompute.probeIaContentEpoch == runtimeState->iaContentEpoch) {
			probe->applied = false;
			DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinPlanCacheLookup();
			DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinPlanCacheHit(false);
			DX12OuterTimingProbeUtils::LogCpuStage(
				"PreSkin", "probe_cache_early_negative_hit", 0, 1,
				DX12OuterTimingProbeUtils::ElapsedUs(t0));
			return;
		}

		if (!probe->uavsFound) {
			DX12_HOTPATH_TIME_BEGIN(PreSkinBindQuery);
			LARGE_INTEGER collectBegin = DX12OuterTimingProbeUtils::Now();

			// One dependency-aware cache owns PreSkin collection. It validates
			// exact descriptor slots and overlapping root-descriptor GPUVA ranges.
			probe->cachedUavs = nullptr;
			probe->cachedSrvs = nullptr;
			probe->cachedCbvs = nullptr;
			probe->cachedRootConstants = nullptr;
			probe->uavsFound = DX12BindingTracker::CollectPreSkinBindings(
				commandList, &probe->uavs, &probe->srvs, &probe->cbvs,
				&probe->rootConstants, &probe->bindingDependencyFingerprint);
			const std::vector<DX12CurrentComputeUavBinding> &uavs =
				probe->cachedUavs ? *probe->cachedUavs : probe->uavs;
			const std::vector<DX12CurrentComputeUavBinding> &srvs =
				probe->cachedSrvs ? *probe->cachedSrvs : probe->srvs;
			const std::vector<DX12CurrentComputeUavBinding> &cbvs =
				probe->cachedCbvs ? *probe->cachedCbvs : probe->cbvs;
			DX12OuterTimingProbeUtils::LogCpuStage(
				"PreSkin",
				"collect_bindings",
				0,
				uavs.size() + srvs.size() + cbvs.size(),
				DX12OuterTimingProbeUtils::ElapsedUs(collectBegin));
			DX12_HOTPATH_TIME_END(PreSkinBindQuery);
		}
		const std::vector<DX12CurrentComputeUavBinding> &uavs =
			probe->cachedUavs ? *probe->cachedUavs : probe->uavs;
		const std::vector<DX12CurrentComputeUavBinding> &srvs =
			probe->cachedSrvs ? *probe->cachedSrvs : probe->srvs;
		const std::vector<DX12CurrentComputeUavBinding> &cbvs =
			probe->cachedCbvs ? *probe->cachedCbvs : probe->cbvs;
		const std::vector<DX12CurrentRootConstants> &rootConstants =
			probe->cachedRootConstants ? *probe->cachedRootConstants : probe->rootConstants;
		probe->srvsFound = !srvs.empty();
		probe->cbvsFound = !cbvs.empty();
		probe->rootConstantsFound = !rootConstants.empty();

		LARGE_INTEGER fingerprintBegin = DX12OuterTimingProbeUtils::Now();
		UINT64 computeIaFingerprint = 0;
		if (runtimeState && probe->bindingDependencyFingerprint &&
				runtimeState->resolvedCompute.bindingFingerprintDependency ==
					probe->bindingDependencyFingerprint &&
				runtimeState->resolvedCompute.bindingFingerprint) {
			computeIaFingerprint = runtimeState->resolvedCompute.bindingFingerprint;
		} else {
			DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinPlanFingerprintBuild();
			computeIaFingerprint = DX12PreSkinPlanUtils::BuildStableBindingFingerprint(
				uavs, srvs, cbvs, rootConstants);
			if (computeIaFingerprint == 0)
				computeIaFingerprint = 1;
			if (runtimeState && probe->bindingDependencyFingerprint) {
				runtimeState->resolvedCompute.bindingFingerprintDependency =
					probe->bindingDependencyFingerprint;
				runtimeState->resolvedCompute.bindingFingerprint = computeIaFingerprint;
			}
		}
		computeIaFingerprint = DX12PreSkinCacheIdentityUtils::MakeProbeFingerprint(
			computeIaFingerprint,
			runtimeState ? runtimeState->iaContentEpoch : 0);
		DX12OuterTimingProbeUtils::LogCpuStage(
			"PreSkin",
			"build_binding_fingerprint",
			0,
			uavs.size() + srvs.size() + cbvs.size(),
			DX12OuterTimingProbeUtils::ElapsedUs(fingerprintBegin));

		// Negative hits can skip matching. Positive hits still reapply through
		// the existing path so required D3D12 commands are emitted.
		bool positiveCacheHit = false;
		DX12PreSkinDispatchOverride cachedDispatchOverride;
		DX12PreSkinProbeCacheEntry cacheEntry;
		DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinPlanCacheLookup();
		LARGE_INTEGER cacheLookupBegin = DX12OuterTimingProbeUtils::Now();
		const UINT64 reloadGeneration = DX12ModGetReloadGenerationFast();
		const UINT64 probeBindingSerial = probe->computeBindingSerial;
		const UINT64 probeIaContentEpoch = runtimeState ? runtimeState->iaContentEpoch : 0;
		const auto storeRuntimeProbeCache =
			[runtimeState, reloadGeneration, computeIaFingerprint,
			 probeBindingSerial, probeIaContentEpoch](
				uint64_t computeShaderHash,
				bool applied,
				const DX12PreSkinDispatchOverride &dispatchOverride) {
				if (!runtimeState)
					return;
				runtimeState->resolvedCompute.probeFingerprint =
					computeIaFingerprint;
				runtimeState->resolvedCompute.probeCsHash = computeShaderHash;
				runtimeState->resolvedCompute.probeReloadGeneration = reloadGeneration;
				runtimeState->resolvedCompute.probeBindingSerial = probeBindingSerial;
				runtimeState->resolvedCompute.probeIaContentEpoch = probeIaContentEpoch;
				runtimeState->resolvedCompute.probeResultValid = true;
				runtimeState->resolvedCompute.probeApplied = applied;
				runtimeState->resolvedCompute.probeDispatchHandlingSkip =
					dispatchOverride.handlingSkip;
				runtimeState->resolvedCompute.probeDispatchHasDispatch =
					dispatchOverride.hasDispatch;
				runtimeState->resolvedCompute.probeGroupsX = dispatchOverride.groupsX;
				runtimeState->resolvedCompute.probeGroupsY = dispatchOverride.groupsY;
				runtimeState->resolvedCompute.probeGroupsZ = dispatchOverride.groupsZ;
			};
		bool runtimeProbeCacheHit = false;
		if (runtimeState &&
			runtimeState->resolvedCompute.probeResultValid &&
			runtimeState->resolvedCompute.probeFingerprint ==
				computeIaFingerprint &&
			runtimeState->resolvedCompute.probeCsHash == probe->computeShaderHash &&
			runtimeState->resolvedCompute.probeReloadGeneration == reloadGeneration) {
			cacheEntry.applied = runtimeState->resolvedCompute.probeApplied;
			cacheEntry.dispatchOverride.handlingSkip =
				runtimeState->resolvedCompute.probeDispatchHandlingSkip;
			cacheEntry.dispatchOverride.hasDispatch =
				runtimeState->resolvedCompute.probeDispatchHasDispatch;
			cacheEntry.dispatchOverride.groupsX =
				runtimeState->resolvedCompute.probeGroupsX;
			cacheEntry.dispatchOverride.groupsY =
				runtimeState->resolvedCompute.probeGroupsY;
			cacheEntry.dispatchOverride.groupsZ =
				runtimeState->resolvedCompute.probeGroupsZ;
			runtimeProbeCacheHit = true;
		}
		if (runtimeProbeCacheHit || DX12ModGetCachedPreSkinProbeResult(
			    commandList, probe->computeShaderHash, computeIaFingerprint, &cacheEntry)) {
			DX12OuterTimingProbeUtils::LogCpuStage(
				"PreSkin",
				cacheEntry.applied ? "probe_cache_positive_hit" : "probe_cache_negative_hit",
				0,
				1,
				DX12OuterTimingProbeUtils::ElapsedUs(cacheLookupBegin));
			DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinPlanCacheHit(
				cacheEntry.applied);
#if defined(_DEBUG)
			DX12LogManager::Text("PreparePreSkinProbe: cache_hit applied=%d fingerprint=%llu\n",
				cacheEntry.applied ? 1 : 0,
				static_cast<unsigned long long>(computeIaFingerprint));
#endif
			if (!runtimeProbeCacheHit) {
				storeRuntimeProbeCache(
					probe->computeShaderHash,
					cacheEntry.applied,
					cacheEntry.dispatchOverride);
			}
			if (!cacheEntry.applied) {
				probe->applied = false;
				QueryPerformanceCounter(&t1);
				QueryPerformanceFrequency(&freq);
				DX12LogManager::JsonFunc("DX12Perf",
					"\"scope\":\"PreSkinProbe\",\"cs\":\"%016llx\",\"applied\":false,\"cache\":\"negative_hit\",\"us\":%lld",
					static_cast<unsigned long long>(probe->computeShaderHash),
					static_cast<long long>((t1.QuadPart - t0.QuadPart) * 1000000 / freq.QuadPart));
				return;
			}
			positiveCacheHit = true;
			cachedDispatchOverride = cacheEntry.dispatchOverride;
#if defined(_DEBUG)
			DX12LogManager::Text("PreparePreSkinProbe: positive cache requires command list reapply fingerprint=%llu\n",
				static_cast<unsigned long long>(computeIaFingerprint));
#endif
		} else {
			DX12OuterTimingProbeUtils::LogCpuStage(
				"PreSkin",
				"probe_cache_miss",
				0,
				1,
				DX12OuterTimingProbeUtils::ElapsedUs(cacheLookupBegin));
			DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinPlanCacheMiss();

			DX12OuterTimingProbeUtils::LogCpuStage(
				"PreSkin",
				"async_warmup_bypassed",
				0,
				1,
				0);
		}
#if defined(_DEBUG)
		probe->shouldLog = true;
#endif
#if defined(_DEBUG)
		DX12LogManager::Text("PreparePreSkinProbe: uavsFound=%d uavCount=%zu srvCount=%zu cbvCount=%zu\n",
			probe->uavsFound ? 1 : 0, uavs.size(), srvs.size(), cbvs.size());

		if (!probe->srvsFound) {
			DX12LogManager::Text("PreparePreSkinProbe: warning no_compute_srvs\n");
		}
#endif

		static const std::vector<DX12CurrentComputeUavBinding> kEmptyComputeBindings;
		static const std::vector<DX12CurrentRootConstants> kEmptyRootConstants;
#if defined(_DEBUG)
		DX12LogManager::Text("PreparePreSkinProbe: calling DX12ModApplyPreSkinningUavReplacement\n");
#endif
		DX12_HOTPATH_TIME_BEGIN(PreSkinMatch);
		DX12_HOTPATH_TIME_BEGIN(PreSkinApply);
		LARGE_INTEGER applyBegin = DX12OuterTimingProbeUtils::Now();
		probe->applied = DX12ModApplyPreSkinningUavReplacement(
			commandList, probe->computeShaderHash,
			probe->uavsFound ? uavs : kEmptyComputeBindings,
			srvs,
			probe->cbvsFound ? cbvs : kEmptyComputeBindings,
			probe->rootConstantsFound ? rootConstants : kEmptyRootConstants,
			&probe->originalVertexCount, &probe->overrideVertexCount,
			&probe->dispatchOverride);
		DX12OuterTimingProbeUtils::LogCpuStage(
			"PreSkin",
			probe->applied ? "apply_replacement_match" : "apply_replacement_miss",
			0,
			uavs.size() + srvs.size() + cbvs.size(),
			DX12OuterTimingProbeUtils::ElapsedUs(applyBegin));
		if (positiveCacheHit && probe->applied && !probe->dispatchOverride.hasDispatch &&
		    cachedDispatchOverride.hasDispatch) {
			probe->dispatchOverride = cachedDispatchOverride;
		}
		DX12_HOTPATH_TIME_END(PreSkinApply);
		DX12_HOTPATH_TIME_END(PreSkinMatch);
		if (!positiveCacheHit || probe->applied) {
			LARGE_INTEGER storeCacheBegin = DX12OuterTimingProbeUtils::Now();
			DX12ModStoreCachedPreSkinningUavMatch(
				commandList, probe->computeShaderHash, computeIaFingerprint,
				probe->applied, &probe->dispatchOverride);
			DX12OuterTimingProbeUtils::LogCpuStage(
				"PreSkin",
				"store_probe_cache",
				0,
				1,
				DX12OuterTimingProbeUtils::ElapsedUs(storeCacheBegin));
			DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinPlanCacheStore();
		}
		storeRuntimeProbeCache(
			probe->computeShaderHash,
			probe->applied,
			probe->dispatchOverride);

		QueryPerformanceCounter(&t1);
		QueryPerformanceFrequency(&freq);
		DX12LogManager::JsonFunc("DX12Perf",
			"\"scope\":\"PreSkinProbe\",\"cs\":\"%016llx\",\"applied\":%s,\"cache\":\"%s\",\"us\":%lld",
			static_cast<unsigned long long>(probe->computeShaderHash),
			probe->applied ? "true" : "false",
			positiveCacheHit ? "positive_reapply" : "miss",
			static_cast<long long>((t1.QuadPart - t0.QuadPart) * 1000000 / freq.QuadPart));
	}

	static void FinishPreSkinProbe(
		ID3D12GraphicsCommandList *commandList,
		UINT originalThreadGroupCountX,
		UINT originalThreadGroupCountY,
		UINT originalThreadGroupCountZ,
		UINT threadGroupCountX,
		UINT threadGroupCountY,
		UINT threadGroupCountZ,
		const DX12DispatchPreSkinProbe &probe)
	{
		const std::vector<DX12CurrentComputeUavBinding> &uavs =
			probe.cachedUavs ? *probe.cachedUavs : probe.uavs;
		const std::vector<DX12CurrentComputeUavBinding> &srvs =
			probe.cachedSrvs ? *probe.cachedSrvs : probe.srvs;
		const std::vector<DX12CurrentComputeUavBinding> &cbvs =
			probe.cachedCbvs ? *probe.cachedCbvs : probe.cbvs;
		const std::vector<DX12CurrentRootConstants> &rootConstants =
			probe.cachedRootConstants ? *probe.cachedRootConstants : probe.rootConstants;
		if (probe.applied) {
			LARGE_INTEGER restoreBegin = DX12OuterTimingProbeUtils::Now();
			DX12ModRestorePreSkinningUavReplacement(commandList);
			DX12OuterTimingProbeUtils::LogCpuStage(
				"PreSkin",
				"restore_replacement",
				0,
				1,
				DX12OuterTimingProbeUtils::ElapsedUs(restoreBegin));
		}
		if (probe.uavsFound) {
			LARGE_INTEGER recordUavBegin = DX12OuterTimingProbeUtils::Now();
			DX12ModRecordComputeUavs(commandList, uavs);
			DX12OuterTimingProbeUtils::LogCpuStage(
				"PreSkin",
				"record_compute_uavs",
				0,
				uavs.size(),
				DX12OuterTimingProbeUtils::ElapsedUs(recordUavBegin));
		}
		if (!probe.applied)
			return;

		DX12_DISPATCH_DEBUG_VERBOSE_LOG(
			DX12LogManager::DebugJsonFunc("DX12DispatchUavProbe",
				"\"found\":%s,\"uavs\":%zu,\"srvs\":%zu,\"cbvs\":%zu,\"rootConstants\":%zu,\"preSkinApplied\":%s,\"handlingSkip\":%s,\"explicitDispatch\":%s,\"cs\":\"%016llx\",\"originalGroups\":\"%u,%u,%u\",\"groups\":\"%u,%u,%u\"",
				probe.uavsFound ? "true" : "false", uavs.size(),
				probe.srvsFound ? srvs.size() : 0,
				probe.cbvsFound ? cbvs.size() : 0,
				probe.rootConstantsFound ? rootConstants.size() : 0,
				probe.applied ? "true" : "false",
				probe.dispatchOverride.handlingSkip ? "true" : "false",
				probe.dispatchOverride.hasDispatch ? "true" : "false",
				static_cast<unsigned long long>(probe.computeShaderHash),
				originalThreadGroupCountX, originalThreadGroupCountY, originalThreadGroupCountZ,
				threadGroupCountX, threadGroupCountY, threadGroupCountZ));
	}
};

// ---- DX12IaReplacementDrawScope.hpp ----
#line 1 "DX12IaReplacementDrawScope.hpp"
class DX12IaReplacementDrawScope
{
public:
	DX12IaReplacementDrawScope()
	{
		++Depth();
	}

	~DX12IaReplacementDrawScope()
	{
		--Depth();
	}

	static bool IsExecuting()
	{
		return Depth() > 0;
	}

	static int &Depth()
	{
		static thread_local int depth = 0;
		return depth;
	}
};

// ---- DX12IaCommandSignatures.hpp ----
#line 1 "DX12IaCommandSignatures.hpp"
#include <d3d12.h>

class DX12IaCommandSignatures
{
public:
	using DrawInstanced = void(STDMETHODCALLTYPE *)(
		ID3D12GraphicsCommandList*, UINT, UINT, UINT, UINT);
	using DrawIndexedInstanced = void(STDMETHODCALLTYPE *)(
		ID3D12GraphicsCommandList*, UINT, UINT, UINT, INT, UINT);
	using Dispatch = void(STDMETHODCALLTYPE *)(
		ID3D12GraphicsCommandList*, UINT, UINT, UINT);
	using SetIndexBuffer = void(STDMETHODCALLTYPE *)(
		ID3D12GraphicsCommandList*, const D3D12_INDEX_BUFFER_VIEW*);
	using SetVertexBuffers = void(STDMETHODCALLTYPE *)(
		ID3D12GraphicsCommandList*, UINT, UINT, const D3D12_VERTEX_BUFFER_VIEW*);
};

// ---- DX12IaReplacementExecutorCallbacks.hpp ----
#line 1 "DX12IaReplacementExecutorCallbacks.hpp"
class DX12IaReplacementExecutorCallbacks
{
public:
	DX12IaCommandSignatures::DrawInstanced drawInstanced = nullptr;
	DX12IaCommandSignatures::DrawIndexedInstanced drawIndexedInstanced = nullptr;
	DX12IaCommandSignatures::Dispatch dispatch = nullptr;
	DX12IaCommandSignatures::SetIndexBuffer setIndexBuffer = nullptr;
	DX12IaCommandSignatures::SetVertexBuffers setVertexBuffers = nullptr;
	void (*recordReplacementDraw)(bool indexed) = nullptr;
	void (*recordReplacementDispatch)() = nullptr;
};

// ---- DX12IaReplacementDrawExecutor.hpp ----
#line 1 "DX12IaReplacementDrawExecutor.hpp"
#include <Windows.h>
#include <d3d12.h>


class DX12IaReplacementDrawExecutor
{
public:
	ID3D12GraphicsCommandList *commandList = nullptr;
	const DX12IaReplacementExecutorCallbacks *callbacks = nullptr;

	DX12IaReplacementDrawExecutor() = default;

	DX12IaReplacementDrawExecutor(
		ID3D12GraphicsCommandList *commandList,
		const DX12IaReplacementExecutorCallbacks &callbacks)
		: commandList(commandList),
		  callbacks(&callbacks)
	{
	}

	void ExecuteOriginalDraw(const DX12IaDrawInvocation &draw) const
	{
		if (!commandList || !callbacks)
			return;

		if (draw.indexed) {
			if (callbacks->drawIndexedInstanced) {
				callbacks->drawIndexedInstanced(commandList, draw.indexCount, draw.instanceCount,
					draw.firstIndex, draw.baseVertex, draw.firstInstance);
			}
			return;
		}

		if (callbacks->drawInstanced) {
			callbacks->drawInstanced(commandList, draw.vertexCount, draw.instanceCount,
				draw.firstVertex, draw.firstInstance);
		}
	}

	void ExecuteReplacementDraws(
		const DX12ModIaReplacement &replacement,
		const DX12IaDrawInvocation &originalDraw) const
	{
		if (!commandList || !callbacks)
			return;

		for (const DX12ModIaReplacement::DispatchCall &dispatch : replacement.dispatches) {
			if (!dispatch.groupsX || !dispatch.groupsY || !dispatch.groupsZ || !callbacks->dispatch)
				continue;
			if (callbacks->recordReplacementDispatch)
				callbacks->recordReplacementDispatch();
			callbacks->dispatch(commandList, dispatch.groupsX, dispatch.groupsY, dispatch.groupsZ);
		}

		for (const DX12ModIaReplacement::DrawCall &draw : replacement.draws) {
			UINT count = draw.count;
			UINT start = draw.start;
			INT baseVertex = draw.baseVertex;

			if (draw.fromCaller) {
				if (draw.indexed) {
					count = originalDraw.indexCount;
					start = originalDraw.firstIndex;
					baseVertex = originalDraw.baseVertex;
				} else {
					count = originalDraw.vertexCount;
					start = originalDraw.firstVertex;
				}
				if (!count)
					continue;
			} else {
				if (!draw.count)
					continue;
			}

			DX12IaReplacementDrawScope replacementDrawScope;
			if (draw.indexed) {
				if (callbacks->drawIndexedInstanced) {
					if (callbacks->recordReplacementDraw)
						callbacks->recordReplacementDraw(true);
					callbacks->drawIndexedInstanced(
						commandList, count, originalDraw.instanceCount,
						start, baseVertex, originalDraw.firstInstance);
				}
			} else if (callbacks->drawInstanced) {
				if (callbacks->recordReplacementDraw)
					callbacks->recordReplacementDraw(false);
				callbacks->drawInstanced(
					commandList, count, originalDraw.instanceCount,
					start, originalDraw.firstInstance);
			}
		}
	}
};

// ---- DX12IaBufferViewUtils.hpp ----
#line 1 "DX12IaBufferViewUtils.hpp"
#include <d3d12.h>

class DX12IaBufferViewUtils
{
public:
	static bool IsVertexBufferBound(const D3D12_VERTEX_BUFFER_VIEW &view)
	{
		return view.BufferLocation != 0 && view.SizeInBytes != 0;
	}
};

// ---- DX12IaReplacementBindingExecutor.hpp ----
#line 1 "DX12IaReplacementBindingExecutor.hpp"
#include <Windows.h>
#include <d3d12.h>


class DX12IaReplacementBindingExecutor
{
public:
	ID3D12GraphicsCommandList *commandList = nullptr;
	const DX12IaReplacementExecutorCallbacks *callbacks = nullptr;

	DX12IaReplacementBindingExecutor() = default;

	DX12IaReplacementBindingExecutor(
		ID3D12GraphicsCommandList *commandList,
		const DX12IaReplacementExecutorCallbacks &callbacks)
		: commandList(commandList),
		  callbacks(&callbacks)
	{
	}

	void Apply(const DX12ModIaReplacement &replacement) const
	{
		if (!commandList || !callbacks)
			return;

		if (replacement.hasIndexBuffer && callbacks->setIndexBuffer) {
			DX12CommandListRuntime::RememberIndexBuffer(commandList, &replacement.indexBuffer);
			callbacks->setIndexBuffer(commandList, &replacement.indexBuffer);
		}

		if (!replacement.vertexBuffers.empty() && callbacks->setVertexBuffers) {
			const UINT count = static_cast<UINT>(replacement.vertexBuffers.size());
			UINT runStart = 0;
			while (runStart < count) {
				while (runStart < count &&
				       !DX12IaBufferViewUtils::IsVertexBufferBound(
					       replacement.vertexBuffers[runStart]))
					++runStart;
				if (runStart >= count)
					break;

				UINT runEnd = runStart + 1;
				while (runEnd < count &&
				       DX12IaBufferViewUtils::IsVertexBufferBound(
					       replacement.vertexBuffers[runEnd]))
					++runEnd;

				const UINT slot = replacement.vertexBufferStartSlot + runStart;
				const UINT runCount = runEnd - runStart;
				const D3D12_VERTEX_BUFFER_VIEW *views =
					replacement.vertexBuffers.data() + runStart;
				DX12CommandListRuntime::RememberVertexBuffers(commandList, slot, runCount, views);
				callbacks->setVertexBuffers(commandList, slot, runCount, views);
				runStart = runEnd;
			}
		}
	}

	void Restore(
		const DX12ModIaReplacement &replacement,
		const DX12ActiveIaState &originalState) const
	{
		if (!commandList || !callbacks)
			return;

		if (replacement.hasIndexBuffer && callbacks->setIndexBuffer) {
			DX12CommandListRuntime::RememberIndexBuffer(commandList,
				originalState.hasIndexBuffer ? &originalState.indexBuffer : nullptr);
			callbacks->setIndexBuffer(commandList,
				originalState.hasIndexBuffer ? &originalState.indexBuffer : nullptr);
		}

		if (!replacement.vertexBuffers.empty() && callbacks->setVertexBuffers) {
			D3D12_VERTEX_BUFFER_VIEW restore[32] = {};
			const UINT start = replacement.vertexBufferStartSlot;
			const UINT count = static_cast<UINT>(replacement.vertexBuffers.size());
			for (UINT i = 0; i < count && start + i < ARRAYSIZE(originalState.vertexBuffers); ++i)
				restore[i] = originalState.hasVertexBuffer[start + i] ?
					originalState.vertexBuffers[start + i] : D3D12_VERTEX_BUFFER_VIEW();
			DX12CommandListRuntime::RememberVertexBuffers(commandList, start, count, restore);
			callbacks->setVertexBuffers(commandList, start, count, restore);
		}
	}
};

// ---- DX12PreSkinIaRedirectUtils.hpp ----
#line 1 "DX12PreSkinIaRedirectUtils.hpp"
#include <Windows.h>
#include <d3d12.h>

#include <algorithm>


class DX12PreSkinIaRedirectUtils
{
public:
	static UINT ClampViewBytes(UINT64 byteWidth)
	{
		return byteWidth > static_cast<UINT64>(UINT_MAX)
			? UINT_MAX
			: static_cast<UINT>(byteWidth);
	}

	static void ApplyReadBarriers(
		ID3D12GraphicsCommandList *commandList,
		const DX12PreSkinOutputResizeBinding &binding)
	{
		if (!commandList || !binding.resizedResource)
			return;
		if (binding.uavBarrierPending) {
			D3D12_RESOURCE_BARRIER barrier = {};
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
			barrier.UAV.pResource = binding.resizedResource;
			commandList->ResourceBarrier(1, &barrier);
		}
		if (binding.transitionToVertex) {
			D3D12_RESOURCE_BARRIER barrier = {};
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Transition.pResource = binding.resizedResource;
			barrier.Transition.StateBefore = binding.stateBefore;
			barrier.Transition.StateAfter =
				D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
			barrier.Transition.Subresource =
				D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			commandList->ResourceBarrier(1, &barrier);
		}
	}

	static bool RedirectVertexBufferViews(
		ID3D12GraphicsCommandList *commandList,
		UINT startSlot,
		UINT count,
		const D3D12_VERTEX_BUFFER_VIEW *views,
		D3D12_VERTEX_BUFFER_VIEW *redirectedViews,
		UINT redirectedCapacity,
		DX12ActiveIaState *activeIa = nullptr)
	{
		if (!views || !redirectedViews || !count || count > redirectedCapacity)
			return false;
		bool anyRedirected = false;
		for (UINT i = 0; i < count; ++i)
			redirectedViews[i] = views[i];
		const UINT64 resizeGeneration = DX12ModGetPreSkinOutputResizeGeneration();
		for (UINT i = 0; i < count; ++i) {
			const D3D12_VERTEX_BUFFER_VIEW &sourceView = views[i];
			if (!sourceView.BufferLocation || !sourceView.SizeInBytes)
				continue;
			const UINT slot = startSlot + i;
			if (activeIa && slot < D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT &&
					activeIa->preSkinRedirectNegativeValid[slot] &&
					activeIa->preSkinRedirectNegativeGeneration[slot] ==
						resizeGeneration) {
				const D3D12_VERTEX_BUFFER_VIEW &cached =
					activeIa->preSkinRedirectNegativeSource[slot];
				if (cached.BufferLocation == sourceView.BufferLocation &&
						cached.SizeInBytes == sourceView.SizeInBytes &&
						cached.StrideInBytes == sourceView.StrideInBytes)
					continue;
			}
			DX12PreSkinOutputResizeBinding binding;
			if (!DX12ModResolvePreSkinOutputResizeByGpuVa(
				    sourceView.BufferLocation, &binding)) {
				if (activeIa && slot < D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT) {
					activeIa->preSkinRedirectNegativeValid[slot] = true;
					activeIa->preSkinRedirectNegativeGeneration[slot] =
						resizeGeneration;
					activeIa->preSkinRedirectNegativeSource[slot] = sourceView;
				}
				continue;
			}
			const UINT64 availableBytes =
				binding.resizedByteWidth > binding.sourceOffset
					? binding.resizedByteWidth - binding.sourceOffset
					: 0;
			if (!availableBytes)
				continue;
			if (activeIa && slot < D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT)
				activeIa->preSkinRedirectNegativeValid[slot] = false;
			const UINT64 redirectedGpuVa =
				binding.resizedGpuVa + binding.sourceOffset;
			ApplyReadBarriers(commandList, binding);
			redirectedViews[i].BufferLocation = redirectedGpuVa;
			redirectedViews[i].SizeInBytes = ClampViewBytes(availableBytes);
			if (!redirectedViews[i].StrideInBytes && binding.stride)
				redirectedViews[i].StrideInBytes = binding.stride;
			anyRedirected = true;
#if defined(_DEBUG)
			DX12LogManager::DebugJsonFunc("DX12PreSkinningApply",
				"\"status\":\"vb_redirected\","
				"\"slot\":%u,"
				"\"oldGpuVa\":\"0x%llx\","
				"\"newGpuVa\":\"0x%llx\","
				"\"oldBytes\":%u,"
				"\"newBytes\":%u,"
				"\"stride\":%u,"
				"\"offset\":%llu,"
				"\"uavBarrier\":%s,"
				"\"transition\":%s",
				startSlot + i,
				static_cast<unsigned long long>(sourceView.BufferLocation),
				static_cast<unsigned long long>(redirectedGpuVa),
				sourceView.SizeInBytes,
				redirectedViews[i].SizeInBytes,
				redirectedViews[i].StrideInBytes,
				static_cast<unsigned long long>(binding.sourceOffset),
				binding.uavBarrierPending ? "true" : "false",
				binding.transitionToVertex ? "true" : "false");
#endif
		}
		return anyRedirected;
	}

	static bool ReplayActiveVertexBuffers(
		ID3D12GraphicsCommandList *commandList,
		const DX12IaReplacementExecutorCallbacks &callbacks,
		DX12ActiveIaState *activeIa)
	{
		if (!commandList || !callbacks.setVertexBuffers || !activeIa)
			return false;
		bool replayedAny = false;
		UINT slot = 0;
		while (slot < D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT) {
			while (slot < D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT &&
			       !activeIa->hasVertexBuffer[slot])
				++slot;
			if (slot >= D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT)
				break;
			const UINT runStart = slot;
			while (slot < D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT &&
			       activeIa->hasVertexBuffer[slot])
				++slot;
			const UINT runCount = slot - runStart;
			D3D12_VERTEX_BUFFER_VIEW redirected[
				D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
			if (!RedirectVertexBufferViews(
				    commandList,
				    runStart,
				    runCount,
				    activeIa->vertexBuffers + runStart,
				    redirected,
				    D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT,
				    activeIa))
				continue;
			for (UINT i = 0; i < runCount; ++i)
				activeIa->vertexBuffers[runStart + i] = redirected[i];
			DX12CommandListRuntime::RememberVertexBuffers(
				commandList, runStart, runCount, redirected);
			callbacks.setVertexBuffers(
				commandList, runStart, runCount, redirected);
			replayedAny = true;
		}
		return replayedAny;
	}
};

// ---- DX12IaReplacementExecutor.hpp ----
#line 1 "DX12IaReplacementExecutor.hpp"
#include <d3d12.h>

#include <unordered_map>
#include <unordered_set>
#include <utility>


// Legacy stub - referenced by compiled code but definition was never provided.
inline bool TryQuickRejectPsoNoMatch(ID3D12PipelineState *)
{
	return false;
}

class DX12PsoDrawFastRejectKey
{
public:
	ID3D12PipelineState *pipelineState = nullptr;
	UINT64 reloadGeneration = 0;
	UINT64 targetPlanFingerprint = 0;
	UINT64 iaFingerprint = 0;
	uint32_t fingerprintKind = 0;
	bool indexed = false;
	uint32_t vertexCount = 0;
	uint32_t indexCount = 0;
	uint32_t instanceCount = 0;
	uint32_t firstVertex = 0;
	uint32_t firstIndex = 0;
	INT baseVertex = 0;
	uint32_t firstInstance = 0;

	bool operator==(const DX12PsoDrawFastRejectKey &other) const
	{
		return pipelineState == other.pipelineState &&
			reloadGeneration == other.reloadGeneration &&
			targetPlanFingerprint == other.targetPlanFingerprint &&
			iaFingerprint == other.iaFingerprint &&
			fingerprintKind == other.fingerprintKind &&
			indexed == other.indexed &&
			vertexCount == other.vertexCount &&
			indexCount == other.indexCount &&
			instanceCount == other.instanceCount &&
			firstVertex == other.firstVertex &&
			firstIndex == other.firstIndex &&
			baseVertex == other.baseVertex &&
			firstInstance == other.firstInstance;
	}
};

class DX12PsoDrawFastRejectKeyHash
{
public:
	static size_t Mix(size_t hash, UINT64 value)
	{
		hash ^= static_cast<size_t>(value) + 0x9e3779b97f4a7c15ull +
			(hash << 6) + (hash >> 2);
		return hash;
	}

	size_t operator()(const DX12PsoDrawFastRejectKey &key) const
	{
		size_t hash = reinterpret_cast<size_t>(key.pipelineState);
		hash = Mix(hash, key.reloadGeneration);
		hash = Mix(hash, key.targetPlanFingerprint);
		hash = Mix(hash, key.iaFingerprint);
		hash = Mix(hash, key.fingerprintKind);
		hash = Mix(hash, key.indexed ? 1 : 0);
		hash = Mix(hash, key.vertexCount);
		hash = Mix(hash, key.indexCount);
		hash = Mix(hash, key.instanceCount);
		hash = Mix(hash, key.firstVertex);
		hash = Mix(hash, key.firstIndex);
		hash = Mix(hash, static_cast<UINT64>(static_cast<INT64>(key.baseVertex)));
		hash = Mix(hash, key.firstInstance);
		return hash;
	}
};

class DX12IaReplacementExecutor
{
public:
	inline static std::unordered_set<
		DX12PsoDrawFastRejectKey,
		DX12PsoDrawFastRejectKeyHash> s_psoDrawFastRejectCache;
	inline static SRWLOCK s_psoDrawFastRejectCacheLock = SRWLOCK_INIT;
	static constexpr size_t MaxPsoDrawFastRejectEntries = 16384;

	static bool TryLoadResolvedIaHashBuild(
		DX12ResolvedDrawRuntimeState::CachedIaHashBuildEntry *entry,
		const DX12IaHashStateCacheKey &key,
		const DX12IaHashState **state,
		bool *hasIaHashState)
	{
		if (!entry || !entry->valid || !state || !hasIaHashState)
			return false;
		if (!(entry->key == key))
			return false;
		*state = &entry->state;
		*hasIaHashState = entry->hasState;
		return true;
	}

	static void StoreResolvedIaHashBuild(
		DX12ResolvedDrawRuntimeState::CachedIaHashBuildEntry *entry,
		const DX12IaHashStateCacheKey &key,
		bool hasIaHashState,
		const DX12IaHashState &state)
	{
		if (!entry)
			return;
		entry->valid = true;
		entry->hasState = hasIaHashState;
		entry->key = key;
		entry->state = state;
	}

	static size_t ResidentNegativeSlot(const DX12IaDrawInvocation &draw)
	{
		UINT64 key = draw.indexed ? 0x9e3779b97f4a7c15ull : 0xc4ceb9fe1a85ec53ull;
		key ^= static_cast<UINT64>(draw.firstIndex) << 32;
		key ^= static_cast<UINT64>(draw.indexCount) << 1;
		key ^= static_cast<UINT64>(draw.firstVertex) << 17;
		key ^= draw.instanceCount;
		return static_cast<size_t>((key ^ (key >> 32)) & 15);
	}

	static bool MatchesResidentNegative(
		const DX12ResidentDrawNegativeEntry &entry,
		const DX12CommandListRuntimeState &state,
		const DX12CompiledDrawDecision &decision,
		UINT64 contentGeneration,
		UINT64 resolveGeneration,
		const DX12IaDrawInvocation &draw)
	{
		return entry.valid && entry.pipelineState == state.pipelineState &&
			entry.reloadGeneration == decision.reloadGeneration &&
			entry.contentGeneration == contentGeneration &&
			entry.resolveGeneration == resolveGeneration &&
			entry.iaEpoch == state.iaEpoch &&
			entry.targetPlanFingerprint == decision.directTargetPlan.Fingerprint() &&
			entry.indexed == draw.indexed && entry.vertexCount == draw.vertexCount &&
			entry.indexCount == draw.indexCount && entry.instanceCount == draw.instanceCount &&
			entry.firstVertex == draw.firstVertex && entry.firstIndex == draw.firstIndex &&
			entry.baseVertex == draw.baseVertex && entry.firstInstance == draw.firstInstance;
	}

	static void StoreResidentNegative(
		DX12CommandListRuntimeState *state,
		const DX12CompiledDrawDecision &decision,
		UINT64 contentGeneration,
		UINT64 resolveGeneration,
		const DX12IaDrawInvocation &draw)
	{
		DX12ResidentDrawNegativeEntry &entry =
			state->resolvedDraw.residentDrawNegativeCache[ResidentNegativeSlot(draw)];
		entry.pipelineState = state->pipelineState;
		entry.reloadGeneration = decision.reloadGeneration;
		entry.contentGeneration = contentGeneration;
		entry.resolveGeneration = resolveGeneration;
		entry.iaEpoch = state->iaEpoch;
		entry.targetPlanFingerprint = decision.directTargetPlan.Fingerprint();
		entry.vertexCount = draw.vertexCount; entry.indexCount = draw.indexCount;
		entry.instanceCount = draw.instanceCount; entry.firstVertex = draw.firstVertex;
		entry.firstIndex = draw.firstIndex; entry.baseVertex = draw.baseVertex;
		entry.firstInstance = draw.firstInstance; entry.indexed = draw.indexed;
		entry.valid = true;
	}

	static UINT64 MixCheapValue(UINT64 key, UINT64 value)
	{
		key ^= value + 0x9e3779b97f4a7c15ull + (key << 6) + (key >> 2);
		key ^= key >> 33;
		key *= 0xff51afd7ed558ccdull;
		return key;
	}

	static DX12PsoDrawFastRejectKey MakeDrawFastRejectKey(
		ID3D12PipelineState *pipelineState,
		UINT64 reloadGeneration,
		const DX12IaHashTargetPlan &targetPlan,
		UINT64 iaFingerprint,
		uint32_t fingerprintKind,
		const DX12IaDrawInvocation &draw)
	{
		DX12PsoDrawFastRejectKey key;
		key.pipelineState = pipelineState;
		key.reloadGeneration = reloadGeneration;
		key.targetPlanFingerprint = targetPlan.Fingerprint();
		key.iaFingerprint = iaFingerprint;
		key.fingerprintKind = fingerprintKind;
		key.indexed = draw.indexed;
		key.vertexCount = draw.vertexCount;
		key.indexCount = draw.indexCount;
		key.instanceCount = draw.instanceCount;
		key.firstVertex = draw.firstVertex;
		key.firstIndex = draw.firstIndex;
		key.baseVertex = draw.baseVertex;
		key.firstInstance = draw.firstInstance;
		return key;
	}

	static DX12PsoDrawFastRejectKey MakePsoIaNegativeKey(
		ID3D12PipelineState *pipelineState,
		UINT64 reloadGeneration,
		const DX12IaHashTargetPlan &targetPlan,
		const DX12IaHashState &iaState,
		const DX12IaDrawInvocation &draw)
	{
		UINT64 fingerprint = 0x4a2d8b31c6e9075full;
		fingerprint = MixCheapValue(fingerprint, iaState.hasIndexBuffer ? 1ull : 0ull);
		if (iaState.hasIndexBuffer) {
			fingerprint = MixCheapValue(fingerprint, iaState.indexHash);
			fingerprint = MixCheapValue(fingerprint, iaState.indexView.BufferLocation);
			fingerprint = MixCheapValue(fingerprint, iaState.indexView.SizeInBytes);
			fingerprint = MixCheapValue(
				fingerprint, static_cast<UINT64>(iaState.indexView.Format));
		}
		for (const DX12IaBufferHash &buffer : iaState.vertexBuffers) {
			fingerprint = MixCheapValue(fingerprint, static_cast<UINT64>(buffer.slot));
			fingerprint = MixCheapValue(fingerprint, buffer.hash);
			fingerprint = MixCheapValue(fingerprint, buffer.vertexView.BufferLocation);
			fingerprint = MixCheapValue(fingerprint, buffer.vertexView.SizeInBytes);
			fingerprint = MixCheapValue(fingerprint, buffer.vertexView.StrideInBytes);
		}
		return MakeDrawFastRejectKey(
			pipelineState, reloadGeneration, targetPlan, fingerprint, 1, draw);
	}

	static bool TryFindPsoDrawFastReject(const DX12PsoDrawFastRejectKey &key)
	{
		AcquireSRWLockShared(&s_psoDrawFastRejectCacheLock);
		bool found = s_psoDrawFastRejectCache.find(key) != s_psoDrawFastRejectCache.end();
		ReleaseSRWLockShared(&s_psoDrawFastRejectCacheLock);
		return found;
	}

	static bool HasAnyDirectTextureOverrideCheckTarget(
		const DX12ActiveIaState &ia,
		const DX12IaHashTargetPlan &targetPlan,
		bool indexed)
	{
		if (!targetPlan.hasTextureOverrideChecks)
			return false;
		if (targetPlan.requireAllIaBuffers)
			return true;
		if (targetPlan.hasDescriptorTargets)
			return true;
		if (targetPlan.RequiresIndexBuffer(indexed) && ia.hasIndexBuffer)
			return true;
		for (UINT slot = 0; slot < ARRAYSIZE(ia.vertexBuffers); ++slot) {
			if (targetPlan.RequiresVertexBuffer(slot) && ia.hasVertexBuffer[slot])
				return true;
		}
		return false;
	}

	static void StorePsoDrawFastReject(const DX12PsoDrawFastRejectKey &key)
	{
		AcquireSRWLockExclusive(&s_psoDrawFastRejectCacheLock);
		if (s_psoDrawFastRejectCache.find(key) != s_psoDrawFastRejectCache.end()) {
			ReleaseSRWLockExclusive(&s_psoDrawFastRejectCacheLock);
			return;
		}
		if (s_psoDrawFastRejectCache.size() >= MaxPsoDrawFastRejectEntries)
			s_psoDrawFastRejectCache.erase(s_psoDrawFastRejectCache.begin());
		s_psoDrawFastRejectCache.insert(key);
		ReleaseSRWLockExclusive(&s_psoDrawFastRejectCacheLock);
	}



	static void ClearPsoDrawFastRejectCache()
	{
		AcquireSRWLockExclusive(&s_psoDrawFastRejectCacheLock);
		s_psoDrawFastRejectCache.clear();
		ReleaseSRWLockExclusive(&s_psoDrawFastRejectCacheLock);
	}

	static bool ExecutePresent(
		ID3D12GraphicsCommandList *commandList,
		const DX12CommandListRuntimeState &runtimeState,
		const DX12IaReplacementExecutorCallbacks &callbacks)
	{
		DX12ModIaReplacement replacement;
		if (!DX12ModPreparePresentReplacement(commandList, &replacement))
			return false;

		const DX12ActiveIaState originalIa = runtimeState.ia;
		DX12IaReplacementBindingExecutor bindingExecutor(commandList, callbacks);
		DX12IaReplacementDrawExecutor drawExecutor(commandList, callbacks);
		bindingExecutor.Apply(replacement);
		DX12ModApplyDrawSrvReplacement(commandList, &replacement, false);
		DX12IaDrawInvocation noDraw = {};
		drawExecutor.ExecuteReplacementDraws(replacement, noDraw);
		bindingExecutor.Restore(replacement, originalIa);
		DX12ModRestoreDrawSrvReplacement(commandList, &replacement);
		return true;
	}

	static bool ApplyAndExecute(
		ID3D12GraphicsCommandList *commandList,
		const DX12IaDrawInvocation &draw,
		const DX12IaHashState &iaState,
		DX12ModIaReplacement *replacement,
		const DX12CommandListRuntimeState &runtimeState,
		const DX12IaReplacementExecutorCallbacks &callbacks)
	{
		if (!commandList || !replacement)
			return false;
		if (replacement->skip && replacement->draws.empty() &&
		    replacement->dispatches.empty() && replacement->drawSrvPatches.empty()) {
			return true;
		}

		DX12ActiveIaState originalIa = runtimeState.ia;
		DX12PreSkinIaRedirectUtils::ReplayActiveVertexBuffers(
			commandList, callbacks, &originalIa);
		DX12IaReplacementBindingExecutor bindingExecutor(commandList, callbacks);
		DX12IaReplacementDrawExecutor drawExecutor(commandList, callbacks);
		bindingExecutor.Apply(*replacement);
		DX12ModApplyDrawSrvReplacement(
			commandList, replacement, !replacement->skip);

		if (!replacement->skip)
			drawExecutor.ExecuteOriginalDraw(draw);

		drawExecutor.ExecuteReplacementDraws(*replacement, draw);
		bindingExecutor.Restore(*replacement, originalIa);
		DX12ModRestoreDrawSrvReplacement(commandList, replacement);
		DX12Profiling::RecordIaApplied();
		DX12ModRunPostShaderOverrideReplacement(
			commandList, runtimeState.pipelineState, iaState,
			draw.vertexCount, draw.indexCount, draw.instanceCount,
			draw.firstVertex, draw.firstIndex, draw.firstInstance, replacement);
		return true;
	}

	static bool HandleDrawOverrides(
		ID3D12GraphicsCommandList *commandList,
		const DX12IaDrawInvocation &draw,
		const DX12CommandListRuntimeState &runtimeState,
		const DX12IaReplacementExecutorCallbacks &callbacks)
	{
		DX12_HOTPATH_TIME_BEGIN(DrawOverrideTotal);

		// Draw consumes the compiled PSO decision instead of rediscovering
		// shader override metadata on every command.
		DX12_HOTPATH_TIME_BEGIN(DrawPsoEarlyExit);
		LARGE_INTEGER psoDecisionBegin = DX12OuterTimingProbeUtils::Now();
		const bool activeShaderOverrides =
			(DX12HotPathState::HookWorkFlags &
			 DX12HotPathState::FLAG_SHADER_OVERRIDES) != 0;
		DX12CommandListRuntimeState *mutableRuntimeState =
			const_cast<DX12CommandListRuntimeState*>(&runtimeState);
		// The runtime deliberately preserves this cache across command-list Reset.
		// Using a second TLS cache here defeated that ownership model, mixed entries
		// from unrelated command lists, and forced every draw through a much larger
		// LRU. Keep hash state resident with the command list whose IA generations
		// form the key, matching DX11's context-local binding state.
		DX12IaHashStateCache *iaHashStateCache =
			&mutableRuntimeState->resolvedDraw.iaHashStateCache;
		const DX12CompiledDrawDecision *drawDecision =
			DX12CommandListRuntime::EnsureDrawDecision(
				mutableRuntimeState, activeShaderOverrides);
		DX12OuterTimingProbeUtils::LogCpuStage(
			"IaReplacement",
			"ensure_draw_decision",
			draw.indexed ? 1u : 0u,
			1,
			DX12OuterTimingProbeUtils::ElapsedUs(psoDecisionBegin));
		if (!drawDecision || drawDecision->CanFastReject()) {
			DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionFastReject();
			DX12_HOTPATH_TIME_END(DrawPsoEarlyExit);
			DX12_HOTPATH_TIME_END(DrawOverrideTotal);
			return false;
		}
		if (drawDecision->NeedsDescriptorMatch())
			DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionDescriptorDraw();
		DX12_HOTPATH_TIME_END(DrawPsoEarlyExit);

		DX12Profiling::RecordIaDrawOverrideCheck();
		DX12ModIaReplacement iaReplacement;
		DX12IaHashState iaState;
		const DX12IaHashState *activeIaState = &iaState;
		const uint32_t vertexCount = draw.indexed ? 0 : draw.vertexCount;
		const uint32_t indexCount = draw.indexed ? draw.indexCount : 0;
		const uint32_t firstVertex = draw.indexed ? 0 : draw.firstVertex;
		const uint32_t firstIndex = draw.indexed ? draw.firstIndex : 0;

		// Cheap negative cache lookup uses PSO, IA view identity, and draw args
		// before falling back to stable IA content hashing.
		DX12IaHashTargetPlan targetPlan = drawDecision->targetPlan;
		const DX12IaHashTargetPlan &directTargetPlan = drawDecision->directTargetPlan;
		const bool targetPlanHasIaTargets = drawDecision->targetPlanHasIaTargets;
		const bool psoIaNegativeCacheSafe = drawDecision->negativeCacheSafe;
		const bool shaderOverrideNegativeCacheAllowed =
			drawDecision->shaderOverrideNegativeCacheAllowed;
		bool hasIaHashState = false;
		DX12PsoDrawFastRejectKey psoPreFilterKey;
		DX12IaHashState directIaState;
		const DX12IaHashState *directIaStateView = &directIaState;
		bool reusedDirectIaState = false;
		DX12DirectIaFastRejectCacheKey directFastRejectKey;
		bool directFastRejectKeyValid = false;
		if (drawDecision->directTargetPlanResolved &&
		    directTargetPlan.hasTextureOverrideChecks &&
		    !DX12IaReplacementExecutor::HasAnyDirectTextureOverrideCheckTarget(
			    runtimeState.ia, directTargetPlan, draw.indexed)) {
			DX12OuterTimingProbeUtils::LogCpuStage(
				"IaReplacement",
				"skip_missing_direct_check_target",
				draw.indexed ? 1u : 0u,
				1,
				0);
			DX12Profiling::RecordIaHashStateResult(false);
			DX12_HOTPATH_TIME_END(DrawOverrideTotal);
			return false;
		}
		if (drawDecision->directTargetPlanResolved &&
		    directTargetPlan.hasTextureOverrideChecks &&
		    directTargetPlan.HasIaTargets() &&
		    !directTargetPlan.hasDescriptorTargets &&
		    !directTargetPlan.requireAllIaBuffers) {
			// Reject repeated negative draws from immutable command-list state before
			// resolving GPU virtual addresses. Global content and resource-lifetime
			// generations make the shortcut conservative across uploads and reuse.
				const UINT64 contentGeneration = DX12GetIaHashContentGeneration();
				const UINT64 resolveGeneration = DX12GetIaHashResolveGeneration();
				DX12ResidentDrawNegativeEntry &residentNegative =
					mutableRuntimeState->resolvedDraw.residentDrawNegativeCache[ResidentNegativeSlot(draw)];
				if (MatchesResidentNegative(residentNegative, *mutableRuntimeState,
						*drawDecision, contentGeneration, resolveGeneration, draw)) {
					DX12Profiling::RecordIaHashStateResult(false);
					DX12_HOTPATH_TIME_END(DrawOverrideTotal);
					return false;
				}
				directFastRejectKey = DX12DirectIaFastRejectCacheUtils::MakeKey(
					drawDecision->reloadGeneration,
					contentGeneration,
					resolveGeneration,
				runtimeState.ia,
				directTargetPlan,
				draw);
			directFastRejectKeyValid = true;
			if (DX12DirectIaFastRejectCacheUtils::Contains(
					&DX12DirectIaFastRejectCacheUtils::ThreadCache(),
					directFastRejectKey)) {
				DX12OuterTimingProbeUtils::LogCpuStage(
					"IaReplacement",
					"direct_ia_fast_reject_cache_hit",
					draw.indexed ? 1u : 0u,
					1,
					0);
				DX12Profiling::RecordIaHashStateResult(false);
				DX12_HOTPATH_TIME_END(DrawOverrideTotal);
				return false;
			}

				// The direct probe and the full override path consume the same stable IA
				// hash state. Cache it before building the probe key so a negative probe
				// hit does not require re-hashing the same draw region first.
				LARGE_INTEGER directProbeBuildBegin = DX12OuterTimingProbeUtils::Now();
				const DX12IaHashStateCacheKey directHashCacheKey =
					DX12CommandListRuntime::BuildIaHashStateCacheKeyForTargetsCached(
						mutableRuntimeState,
						0,
						directTargetPlan,
						draw.indexed,
						draw.vertexCount,
						draw.indexCount,
						draw.firstVertex,
						draw.firstIndex,
						draw.baseVertex);
				bool directHashFound = false;
				bool directHashCacheHit = TryLoadResolvedIaHashBuild(
					&mutableRuntimeState->resolvedDraw.lastDirectIaHashBuild,
					directHashCacheKey,
					&directIaStateView,
					&directHashFound);
				if (!directHashCacheHit) {
					directHashCacheHit = DX12IaHashStateCacheUtils::TryLoad(
						iaHashStateCache,
						directHashCacheKey,
						&directIaStateView,
						&directHashFound);
				}
				if (!directHashCacheHit) {
					directHashFound = DX12CommandListRuntime::BuildIaHashStateForTargets(
						runtimeState.ia,
						directTargetPlan,
						draw.indexed,
						draw.vertexCount,
						draw.indexCount,
						draw.firstVertex,
						draw.firstIndex,
						draw.baseVertex,
						&directIaState);
					bool directHashCacheEvicted = false;
					const DX12IaHashState *storedDirectState =
						DX12IaHashStateCacheUtils::Store(
							iaHashStateCache,
							directHashCacheKey,
							directHashFound,
							std::move(directIaState),
							&directHashCacheEvicted);
					if (storedDirectState)
						directIaStateView = storedDirectState;
					if (directIaStateView) {
						StoreResolvedIaHashBuild(
							&mutableRuntimeState->resolvedDraw.lastDirectIaHashBuild,
							directHashCacheKey,
							directHashFound,
							*directIaStateView);
					}
					if (directHashCacheEvicted)
						DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionIaHashCacheEviction();
				} else if (directIaStateView) {
					StoreResolvedIaHashBuild(
						&mutableRuntimeState->resolvedDraw.lastDirectIaHashBuild,
						directHashCacheKey,
						directHashFound,
						*directIaStateView);
				}
				if (directHashFound) {
					DX12OuterTimingProbeUtils::LogCpuStage(
						"IaReplacement",
						directHashCacheHit ?
							"direct_ia_probe_state_cache_hit" :
							"build_direct_ia_probe_state",
						draw.indexed ? 1u : 0u,
						1,
						DX12OuterTimingProbeUtils::ElapsedUs(directProbeBuildBegin));

					LARGE_INTEGER directProbeKeyBegin = DX12OuterTimingProbeUtils::Now();
					const DX12DirectIaProbeCacheKey directProbeCacheKey =
							DX12DirectIaProbeCacheUtils::MakeKey(
								drawDecision->reloadGeneration,
								directTargetPlan.Fingerprint(),
								*directIaStateView,
								draw);
					DX12OuterTimingProbeUtils::LogCpuStage(
						"IaReplacement",
						"build_direct_ia_probe_key",
						draw.indexed ? 1u : 0u,
						1,
						DX12OuterTimingProbeUtils::ElapsedUs(directProbeKeyBegin));

					DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionDirectProbeCacheLookup();
					LARGE_INTEGER directProbeCacheBegin = DX12OuterTimingProbeUtils::Now();
					bool hasDirectCandidate = false;
					if (DX12DirectIaProbeCacheUtils::TryLoad(
							&mutableRuntimeState->resolvedDraw.directIaProbeCache,
							directProbeCacheKey,
							&hasDirectCandidate)) {
						DX12OuterTimingProbeUtils::LogCpuStage(
							"IaReplacement",
							hasDirectCandidate ?
								"direct_ia_probe_cache_positive_hit" :
								"direct_ia_probe_cache_negative_hit",
							draw.indexed ? 1u : 0u,
							1,
							DX12OuterTimingProbeUtils::ElapsedUs(directProbeCacheBegin));
						DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionDirectProbeCacheHit(
							hasDirectCandidate);
					} else {
						DX12OuterTimingProbeUtils::LogCpuStage(
							"IaReplacement",
							"direct_ia_probe_cache_miss",
							draw.indexed ? 1u : 0u,
							1,
							DX12OuterTimingProbeUtils::ElapsedUs(directProbeCacheBegin));
						DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionDirectProbeCacheMiss();
						LARGE_INTEGER directProbeMatchBegin = DX12OuterTimingProbeUtils::Now();
							hasDirectCandidate = DX12ModHasAnyDirectIaTextureOverrideCandidate(
								*directIaStateView,
							directTargetPlan,
							draw.indexed,
							draw.vertexCount,
							draw.indexCount,
							draw.instanceCount,
							draw.firstVertex,
							draw.firstIndex,
							draw.firstInstance);
						DX12OuterTimingProbeUtils::LogCpuStage(
							"IaReplacement",
							hasDirectCandidate ?
								"direct_ia_probe_positive_result" :
								"direct_ia_probe_negative_result",
							draw.indexed ? 1u : 0u,
							1,
							DX12OuterTimingProbeUtils::ElapsedUs(directProbeMatchBegin));
						bool directProbeCacheEvicted = false;
						DX12DirectIaProbeCacheUtils::Store(
							&mutableRuntimeState->resolvedDraw.directIaProbeCache,
							directProbeCacheKey,
							hasDirectCandidate,
							&directProbeCacheEvicted);
						DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionDirectProbeCacheStore(
							directProbeCacheEvicted);
					}

					if (!hasDirectCandidate) {
						StoreResidentNegative(mutableRuntimeState, *drawDecision,
							contentGeneration, resolveGeneration, draw);
						if (directFastRejectKeyValid) {
							DX12DirectIaFastRejectCacheUtils::Store(
								&DX12DirectIaFastRejectCacheUtils::ThreadCache(),
								std::move(directFastRejectKey));
						}
						DX12OuterTimingProbeUtils::LogCpuStage(
							"IaReplacement",
							"skip_direct_ia_probe_miss",
							draw.indexed ? 1u : 0u,
							1,
							0);
						DX12Profiling::RecordIaHashStateResult(false);
						DX12_HOTPATH_TIME_END(DrawOverrideTotal);
						return false;
					}
					if (targetPlan.HasSameIaHashTargets(directTargetPlan)) {
						activeIaState = directIaStateView;
						hasIaHashState = true;
						reusedDirectIaState = true;
						DX12OuterTimingProbeUtils::LogCpuStage(
							"IaReplacement",
							"reuse_direct_ia_state",
							draw.indexed ? 1u : 0u,
							1,
							0);
					}
				} else {
					DX12OuterTimingProbeUtils::LogCpuStage(
						"IaReplacement",
						"build_direct_ia_probe_state_failed",
						draw.indexed ? 1u : 0u,
						1,
						DX12OuterTimingProbeUtils::ElapsedUs(directProbeBuildBegin));
				}
		}
			if (targetPlanHasIaTargets && !reusedDirectIaState) {
				LARGE_INTEGER iaKeyBegin = DX12OuterTimingProbeUtils::Now();
			DX12IaHashStateCacheKey iaCacheKey =
				DX12CommandListRuntime::BuildIaHashStateCacheKeyForTargetsCached(
					mutableRuntimeState,
					1,
					targetPlan,
					draw.indexed,
					draw.vertexCount,
					draw.indexCount,
					draw.firstVertex,
					draw.firstIndex,
					draw.baseVertex);
			DX12OuterTimingProbeUtils::LogCpuStage(
				"IaReplacement",
				"build_ia_cache_key",
				draw.indexed ? 1u : 0u,
				1,
				DX12OuterTimingProbeUtils::ElapsedUs(iaKeyBegin));
			psoPreFilterKey = DX12IaReplacementExecutor::MakeDrawFastRejectKey(
				runtimeState.pipelineState,
				drawDecision->reloadGeneration,
				targetPlan,
				iaCacheKey.iaViewFingerprint,
				0,
				draw);
			if (DX12IaReplacementExecutor::TryFindPsoDrawFastReject(psoPreFilterKey)
				&& !drawDecision->targetPlanHasDescriptorTargets) {
				DX12Profiling::RecordIaHashStateResult(false);
				DX12_HOTPATH_TIME_END(DrawOverrideTotal);
				return false;
			}
			DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionIaHashCacheLookup();
			LARGE_INTEGER iaCacheBegin = DX12OuterTimingProbeUtils::Now();
			bool iaHashCacheHit = TryLoadResolvedIaHashBuild(
				&mutableRuntimeState->resolvedDraw.lastFullIaHashBuild,
				iaCacheKey,
				&activeIaState,
				&hasIaHashState);
			if (!iaHashCacheHit) {
				iaHashCacheHit = DX12IaHashStateCacheUtils::TryLoad(
					iaHashStateCache,
					iaCacheKey,
					&activeIaState,
					&hasIaHashState);
			}
			if (iaHashCacheHit) {
				DX12OuterTimingProbeUtils::LogCpuStage(
					"IaReplacement",
					"ia_hash_cache_hit",
					draw.indexed ? 1u : 0u,
					hasIaHashState ? 1 : 0,
					DX12OuterTimingProbeUtils::ElapsedUs(iaCacheBegin));
				DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionIaHashCacheHit(
					hasIaHashState);
				if (activeIaState) {
					StoreResolvedIaHashBuild(
						&mutableRuntimeState->resolvedDraw.lastFullIaHashBuild,
						iaCacheKey,
						hasIaHashState,
						*activeIaState);
				}
			} else {
				DX12OuterTimingProbeUtils::LogCpuStage(
					"IaReplacement",
					"ia_hash_cache_miss",
					draw.indexed ? 1u : 0u,
					1,
					DX12OuterTimingProbeUtils::ElapsedUs(iaCacheBegin));
				DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionIaHashCacheMiss();
				DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionIaHashDraw();
				DX12_HOTPATH_TIME_BEGIN(DrawBuildIaHash);
				LARGE_INTEGER buildHashBegin = DX12OuterTimingProbeUtils::Now();
				hasIaHashState = DX12CommandListRuntime::BuildIaHashStateForTargets(
					runtimeState.ia, targetPlan, draw.indexed, draw.vertexCount, draw.indexCount,
					draw.firstVertex, draw.firstIndex, draw.baseVertex, &iaState);
				activeIaState = &iaState;
				DX12OuterTimingProbeUtils::LogCpuStage(
					"IaReplacement",
					"build_ia_hash_state",
					draw.indexed ? 1u : 0u,
					hasIaHashState ? 1 : 0,
					DX12OuterTimingProbeUtils::ElapsedUs(buildHashBegin));
				DX12_HOTPATH_TIME_END(DrawBuildIaHash);
				LARGE_INTEGER storeHashBegin = DX12OuterTimingProbeUtils::Now();
				bool cacheEvicted = false;
				const DX12IaHashState *storedIaState = DX12IaHashStateCacheUtils::Store(
					iaHashStateCache,
					iaCacheKey,
					hasIaHashState,
					std::move(iaState),
					&cacheEvicted);
				if (storedIaState)
					activeIaState = storedIaState;
				if (activeIaState) {
					StoreResolvedIaHashBuild(
						&mutableRuntimeState->resolvedDraw.lastFullIaHashBuild,
						iaCacheKey,
						hasIaHashState,
						*activeIaState);
				}
				if (cacheEvicted)
					DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionIaHashCacheEviction();
				DX12OuterTimingProbeUtils::LogCpuStage(
					"IaReplacement",
					"store_ia_hash_cache",
					draw.indexed ? 1u : 0u,
					1,
					DX12OuterTimingProbeUtils::ElapsedUs(storeHashBegin));
				DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionIaHashCacheStore();
			}
			DX12Profiling::RecordIaHashStateResult(hasIaHashState);
		} else {
			DX12Profiling::RecordIaHashStateResult(
				targetPlanHasIaTargets && reusedDirectIaState);
		}
		if (hasIaHashState || !targetPlanHasIaTargets ||
		    drawDecision->targetPlanHasDescriptorTargets) {
			const bool canUsePsoIaNegativeCache =
				psoIaNegativeCacheSafe && (!targetPlanHasIaTargets || hasIaHashState);
			DX12PsoDrawFastRejectKey psoIaNegKey;

			// PSO and IA negative cache.
			if (canUsePsoIaNegativeCache) {
				DX12_HOTPATH_TIME_BEGIN(DrawPsoIaNegCache);
				LARGE_INTEGER negKeyBegin = DX12OuterTimingProbeUtils::Now();
				psoIaNegKey = DX12IaReplacementExecutor::MakePsoIaNegativeKey(
					runtimeState.pipelineState,
					drawDecision->reloadGeneration,
					targetPlan,
					*activeIaState,
					draw);
				DX12OuterTimingProbeUtils::LogCpuStage(
					"IaReplacement",
					"build_pso_ia_negative_key",
					draw.indexed ? 1u : 0u,
					1,
					DX12OuterTimingProbeUtils::ElapsedUs(negKeyBegin));
				LARGE_INTEGER negLookupBegin = DX12OuterTimingProbeUtils::Now();
				if (DX12IaReplacementExecutor::TryFindPsoDrawFastReject(psoIaNegKey)) {
					DX12OuterTimingProbeUtils::LogCpuStage(
						"IaReplacement",
						"pso_ia_negative_hit",
						draw.indexed ? 1u : 0u,
						1,
						DX12OuterTimingProbeUtils::ElapsedUs(negLookupBegin));
					DX12_HOTPATH_TIME_END(DrawPsoIaNegCache);
					DX12_HOTPATH_TIME_END(DrawOverrideTotal);
					return false;
				}
				DX12OuterTimingProbeUtils::LogCpuStage(
					"IaReplacement",
					"pso_ia_negative_miss",
					draw.indexed ? 1u : 0u,
					1,
					DX12OuterTimingProbeUtils::ElapsedUs(negLookupBegin));
				DX12_HOTPATH_TIME_END(DrawPsoIaNegCache);
			}

			DX12_HOTPATH_TIME_BEGIN(DrawModPrepareOverride);
			LARGE_INTEGER prepareBegin = DX12OuterTimingProbeUtils::Now();
			if (DX12ModPrepareShaderOverrideReplacement(
				commandList, runtimeState.pipelineState, *activeIaState,
				vertexCount, indexCount, draw.instanceCount,
				firstVertex, firstIndex, draw.firstInstance, &iaReplacement,
				shaderOverrideNegativeCacheAllowed,
				runtimeState.device)) {
				DX12OuterTimingProbeUtils::LogCpuStage(
					"IaReplacement",
					"prepare_shader_override_match",
					draw.indexed ? 1u : 0u,
					1,
					DX12OuterTimingProbeUtils::ElapsedUs(prepareBegin));
				DX12_HOTPATH_TIME_END(DrawModPrepareOverride);
				DX12_HOTPATH_TIME_BEGIN(DrawApplyAndExecute);
				LARGE_INTEGER applyBegin = DX12OuterTimingProbeUtils::Now();
				if (DX12IaReplacementExecutor::ApplyAndExecute(
					commandList, draw, *activeIaState, &iaReplacement, runtimeState, callbacks)) {
					DX12OuterTimingProbeUtils::LogCpuStage(
						"IaReplacement",
						"apply_and_execute_match",
						draw.indexed ? 1u : 0u,
						1,
						DX12OuterTimingProbeUtils::ElapsedUs(applyBegin));
					DX12_HOTPATH_TIME_END(DrawApplyAndExecute);
					DX12_HOTPATH_TIME_END(DrawOverrideTotal);
					return true;
				}
				DX12OuterTimingProbeUtils::LogCpuStage(
					"IaReplacement",
					"apply_and_execute_failed",
					draw.indexed ? 1u : 0u,
					1,
					DX12OuterTimingProbeUtils::ElapsedUs(applyBegin));
				DX12_HOTPATH_TIME_END(DrawApplyAndExecute);
			} else {
				DX12OuterTimingProbeUtils::LogCpuStage(
					"IaReplacement",
					"prepare_shader_override_miss",
					draw.indexed ? 1u : 0u,
					1,
					DX12OuterTimingProbeUtils::ElapsedUs(prepareBegin));
				DX12_HOTPATH_TIME_END(DrawModPrepareOverride);
			}

			if (canUsePsoIaNegativeCache) {
				// No match: store both the full negative key and cheap-to-full map.
				LARGE_INTEGER negStoreBegin = DX12OuterTimingProbeUtils::Now();
				DX12IaReplacementExecutor::StorePsoDrawFastReject(psoIaNegKey);
				if (targetPlanHasIaTargets)
					DX12IaReplacementExecutor::StorePsoDrawFastReject(psoPreFilterKey);

				DX12OuterTimingProbeUtils::LogCpuStage(
					"IaReplacement",
					"store_pso_ia_negative",
					draw.indexed ? 1u : 0u,
					1,
					DX12OuterTimingProbeUtils::ElapsedUs(negStoreBegin));
			}
			DX12_HOTPATH_TIME_END(DrawOverrideTotal);
			return false;
		}
		if (drawDecision->hasShaderOverrideSkip) {
			DX12_HOTPATH_TIME_END(DrawOverrideTotal);
			return true;
		}
		DX12_HOTPATH_TIME_END(DrawOverrideTotal);
		return false;
	}
};

// ---- DX12DrawHookFlow.hpp ----
#line 1 "DX12DrawHookFlow.hpp"
#include <d3d12.h>


class DX12DrawHookFlow
{
public:
	static bool TryExecuteSkipAll(
		ID3D12GraphicsCommandList *commandList,
		const DX12IaDrawInvocation &draw,
		const DX12IaReplacementExecutorCallbacks &callbacks)
	{
		if (!DX12HotPathState::skipAll)
			return false;
		DX12IaReplacementDrawExecutor(commandList, callbacks).ExecuteOriginalDraw(draw);
		return true;
	}

	static void ExecuteHook(
		ID3D12GraphicsCommandList *commandList,
		const DX12IaDrawInvocation &draw,
		const DX12IaReplacementExecutorCallbacks &callbacks)
	{
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12DrawHookFlow::ExecuteHook(commandList, runtimeState, draw, callbacks);
	}

	static void ExecuteHook(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeState *runtimeState,
		const DX12IaDrawInvocation &draw,
		const DX12IaReplacementExecutorCallbacks &callbacks)
	{
		DX12_HOTPATH_TIME_BEGIN(DrawHookTotal);
		const UINT64 flagsWord = DX12HotPathState::HookWorkFlags;
		const bool needsModWork = (flagsWord & DX12HotPathState::FLAG_NEEDS_MOD_WORK) != 0;
		const bool huntEnabled = (flagsWord & DX12HotPathState::FLAG_HUNT_ENABLED) != 0;

		if (!(flagsWord & DX12HotPathState::MASK_DRAW_WORK)) {
			DX12_PROFILE_FAST_FORWARD();
			DX12IaReplacementDrawExecutor(commandList, callbacks).ExecuteOriginalDraw(draw);
			DX12_HOTPATH_TIME_END(DrawHookTotal);
			return;
		}

		// DX11-style resident PSO rejection: when shader overrides are the only
		// enabled draw feature, a PSO proven unable to match must not enter the
		// generic draw interpreter (counters, logging, IA keys, or cache layers).
		// Reload generation keeps the shortcut conservative across hot reloads.
		if (runtimeState &&
			flagsWord == (DX12HotPathState::FLAG_NEEDS_MOD_WORK |
				DX12HotPathState::FLAG_SHADER_OVERRIDES) &&
			runtimeState->drawDecision.resolved &&
			runtimeState->drawDecision.pipelineState == runtimeState->pipelineState &&
			runtimeState->drawDecision.reloadGeneration == DX12ModGetReloadGenerationFast() &&
			runtimeState->drawDecision.MatchesGlobals(true) &&
			runtimeState->drawDecision.CanFastReject()) {
			DX12_PROFILE_FAST_FORWARD();
			DX12IaReplacementDrawExecutor(commandList, callbacks).ExecuteOriginalDraw(draw);
			DX12_HOTPATH_TIME_END(DrawHookTotal);
			return;
		}

		if (DX12Profiling::ShouldCollectCounters() || needsModWork)
			DX12RuntimeState::IncrementFrameDraws();

		if (DX12IaReplacementDrawScope::IsExecuting()) {
			DX12_PROFILE_FAST_FORWARD();
			DX12IaReplacementDrawExecutor(commandList, callbacks).ExecuteOriginalDraw(draw);
			DX12_HOTPATH_TIME_END(DrawHookTotal);
			return;
		}

		if (DX12HotPathState::skipBindings && huntEnabled && !needsModWork) {
			DX12_PROFILE_FAST_FORWARD();
			if (ShouldSkipHuntDraw(commandList, draw)) {
				DX12_HOTPATH_TIME_END(DrawHookTotal);
				return;
			}
			RecordHuntDraw(commandList, draw);
			DX12IaReplacementDrawExecutor(commandList, callbacks).ExecuteOriginalDraw(draw);
			DX12_HOTPATH_TIME_END(DrawHookTotal);
			return;
		}

		LogDrawCall(commandList, draw);
		if (DX12CommandListStateCapture::ShouldRecordBindingEvents())
			RecordBindingDraw(commandList, draw);
		if (huntEnabled) {
			if (ShouldSkipHuntDraw(commandList, draw))
				return;
			RecordHuntDraw(commandList, draw);
		}
		if (!needsModWork) {
			DX12_PROFILE_FAST_FORWARD();
			DX12IaReplacementDrawExecutor(commandList, callbacks).ExecuteOriginalDraw(draw);
			DX12_HOTPATH_TIME_END(DrawHookTotal);
			return;
		}

		DX12DrawHookFlow::Execute(commandList, draw, runtimeState, callbacks);
		DX12_HOTPATH_TIME_END(DrawHookTotal);
	}

	static void Execute(
		ID3D12GraphicsCommandList *commandList,
		const DX12IaDrawInvocation &draw,
		const DX12CommandListRuntimeState *runtimeState,
		const DX12IaReplacementExecutorCallbacks &callbacks)
	{
		const UINT64 flagsWord = DX12HotPathState::HookWorkFlags;
		if (runtimeState &&
		    (flagsWord & DX12HotPathState::FLAG_PRESENT_REPLACEMENT))
			DX12IaReplacementExecutor::ExecutePresent(commandList, *runtimeState, callbacks);
		if (runtimeState &&
		    DX12IaReplacementExecutor::HandleDrawOverrides(commandList, draw, *runtimeState, callbacks))
			return;
		DX12IaReplacementDrawExecutor(commandList, callbacks).ExecuteOriginalDraw(draw);
	}

	static bool ShouldSkipHuntDraw(
		ID3D12GraphicsCommandList *commandList,
		const DX12IaDrawInvocation &draw)
	{
		return DX12HuntShouldSkipDraw(
			commandList, draw.indexed,
			draw.indexed ? 0 : draw.vertexCount,
			draw.indexed ? draw.indexCount : 0,
			draw.indexed ? 0 : draw.firstVertex,
			draw.indexed ? draw.firstIndex : 0,
			draw.indexed ? draw.baseVertex : 0);
	}

	static void RecordHuntDraw(
		ID3D12GraphicsCommandList *commandList,
		const DX12IaDrawInvocation &draw)
	{
		DX12HuntRecordDraw(
			commandList, draw.indexed,
			draw.indexed ? 0 : draw.vertexCount,
			draw.indexed ? draw.indexCount : 0,
			draw.indexed ? 0 : draw.firstVertex,
			draw.indexed ? draw.firstIndex : 0,
			draw.indexed ? draw.baseVertex : 0);
	}

	static void RecordBindingDraw(
		ID3D12GraphicsCommandList *commandList,
		const DX12IaDrawInvocation &draw)
	{
		if (draw.indexed) {
			DX12BindingTracker::RecordDrawIndexedInstanced(
				commandList, draw.indexCount, draw.instanceCount,
				draw.firstIndex, draw.baseVertex, draw.firstInstance);
			return;
		}
		DX12BindingTracker::RecordDrawInstanced(
			commandList, draw.vertexCount, draw.instanceCount,
			draw.firstVertex, draw.firstInstance);
	}

	static void LogDrawCall(
		ID3D12GraphicsCommandList *commandList,
		const DX12IaDrawInvocation &draw)
	{
		if (draw.indexed) {
			DX12CommandListFlowDiagnostics::LogDX12Call(
				"ID3D12GraphicsCommandList::DrawIndexedInstanced", commandList,
				" indices=%u instances=%u startIndex=%u baseVertex=%d startInstance=%u",
				draw.indexCount, draw.instanceCount, draw.firstIndex,
				draw.baseVertex, draw.firstInstance);
			return;
		}
		DX12CommandListFlowDiagnostics::LogDX12Call(
			"ID3D12GraphicsCommandList::DrawInstanced", commandList,
			" vertices=%u instances=%u startVertex=%u startInstance=%u",
			draw.vertexCount, draw.instanceCount, draw.firstVertex, draw.firstInstance);
	}
};

// ---- DX12HookRegistry.hpp ----
#line 1 "DX12HookRegistry.hpp"
#include <Windows.h>

#include <array>
#include <unordered_map>
#include <unordered_set>

#include "Nektra/NktHookLib.h"

class DX12HookRegistry
{
public:
	SRWLOCK lock;
	CNktHookLib hookManager;
	std::unordered_set<void*> hookedFunctions;
	std::unordered_map<void*, void*> originalFunctions;
	std::unordered_map<void*, std::array<void*, 256>> vtableOriginals;
	volatile LONG hooksFinalized = 0;

	DX12HookRegistry()
	{
		InitializeSRWLock(&lock);
	}

	static DX12HookRegistry &Get()
	{
		static DX12HookRegistry state;
		return state;
	}

	static bool RememberHook(void *target)
	{
		DX12HookRegistry &state = Get();
		AcquireSRWLockExclusive(&state.lock);
		bool inserted = state.hookedFunctions.insert(target).second;
		ReleaseSRWLockExclusive(&state.lock);
		return inserted;
	}

	static void RememberOriginal(void *target, void *original)
	{
		if (!target || !original)
			return;

		DX12HookRegistry &state = Get();
		AcquireSRWLockExclusive(&state.lock);
		state.originalFunctions[target] = original;
		ReleaseSRWLockExclusive(&state.lock);
	}

	static DWORD HookFunction(void **original, void *target, void *hook, const char *name)
	{
		if (!target || !hook)
			return ERROR_PROC_NOT_FOUND;
		if (!RememberHook(target))
			return ERROR_SUCCESS;

		SIZE_T hookId = 0;
		DWORD result = Get().hookManager.Hook(&hookId, original, target, hook);
		if (result == ERROR_SUCCESS && original)
			RememberOriginal(target, *original);
		DX12LogManager::JsonFunc(name ? name : "HookInstall",
			"\"event\":\"HookInstall\",\"status\":\"%s\",\"target\":\"%p\",\"original\":\"%p\",\"result\":\"0x%lx\"",
			result == ERROR_SUCCESS ? "installed" : "failed",
			target, original ? *original : nullptr, result);
		return result;
	}

	static void FinalizeHooks()
	{
		InterlockedExchange(&Get().hooksFinalized, 1);
	}

	static void *GetOriginalFunction(void *target)
	{
		if (!target)
			return nullptr;

		DX12HookRegistry &state = Get();
		if (state.hooksFinalized) {
			auto it = state.originalFunctions.find(target);
			return it != state.originalFunctions.end() ? it->second : nullptr;
		}

		AcquireSRWLockShared(&state.lock);
		auto it = state.originalFunctions.find(target);
		void *original = it != state.originalFunctions.end() ? it->second : nullptr;
		ReleaseSRWLockShared(&state.lock);
		return original;
	}

	static void UnhookAll()
	{
		Get().hookManager.UnhookAll();
	}

	static void CacheVtableOriginals(void *vtable, size_t maxSlot)
	{
		if (!vtable || maxSlot == 0 || maxSlot > 256)
			return;
		DX12HookRegistry &state = Get();
		void **slots = static_cast<void**>(vtable);
		std::array<void*, 256> originals = {};
		for (size_t i = 0; i < maxSlot; ++i) {
			void *target = slots[i];
			if (!target)
				continue;
			auto it = state.originalFunctions.find(target);
			if (it != state.originalFunctions.end())
				originals[i] = it->second;
		}
		state.vtableOriginals[vtable] = originals;
	}
};

// ---- DX12HookTypes.hpp ----
#line 1 "DX12HookTypes.hpp"
#include <Windows.h>

class DX12VTableHook
{
public:
	UINT slot;
	void **original;
	void *hook;
	const char *name;
};

// ---- DX12HookManager.hpp ----
#line 1 "DX12HookManager.hpp"
#include <Windows.h>
#include <stddef.h>


class DX12HookManager
{
public:
	static void **GetVTable(void *object)
	{
		if (!object)
			return nullptr;
		return *reinterpret_cast<void***>(object);
	}

	static DWORD InstallVTableHook(void *object, const DX12VTableHook &hook)
	{
		void **vtable = GetVTable(object);
		if (!vtable) {
			DX12LogManager::JsonFunc(hook.name ? hook.name : "HookInstall",
				"\"event\":\"HookInstall\",\"status\":\"failed\",\"reason\":\"missing_vtable\"");
			return ERROR_INVALID_FUNCTION;
		}

		return DX12HookRegistry::HookFunction(hook.original, vtable[hook.slot], hook.hook, hook.name);
	}

	static void InstallVTableHooks(void *object, const DX12VTableHook *hooks, size_t hookCount)
	{
		if (!object || !hooks)
			return;

		for (size_t i = 0; i < hookCount; ++i)
			InstallVTableHook(object, hooks[i]);
	}

	template <size_t N>
	static void InstallVTableHooks(void *object, const DX12VTableHook (&hooks)[N])
	{
		InstallVTableHooks(object, hooks, N);
	}

	static DWORD InstallExportHook(HMODULE module, const char *exportName, void **original, void *hook)
	{
		if (!module || !exportName)
			return ERROR_PROC_NOT_FOUND;

		return DX12HookRegistry::HookFunction(original, GetProcAddress(module, exportName), hook, exportName);
	}
};

// ---- DX12CommandListHookSlotTypes.hpp ----
#line 1 "DX12CommandListHookSlotTypes.hpp"
#include <Windows.h>

class DX12CommandListHookSlotTypes
{
public:
	class ID3D12GraphicsCommandList
	{
	public:
		static constexpr UINT Close = 9;
		static constexpr UINT Reset = 10;
		static constexpr UINT ClearState = 11;
		static constexpr UINT DrawInstanced = 12;
		static constexpr UINT DrawIndexedInstanced = 13;
		static constexpr UINT Dispatch = 14;
		static constexpr UINT IaSetPrimitiveTopology = 20;
		static constexpr UINT SetPipelineState = 25;
		static constexpr UINT ResourceBarrier = 26;
		static constexpr UINT ExecuteBundle = 27;
		static constexpr UINT SetDescriptorHeaps = 28;
		static constexpr UINT SetComputeRootSignature = 29;
		static constexpr UINT SetGraphicsRootSignature = 30;
		static constexpr UINT SetComputeRootDescriptorTable = 31;
		static constexpr UINT SetGraphicsRootDescriptorTable = 32;
		static constexpr UINT SetComputeRoot32BitConstant = 33;
		static constexpr UINT SetGraphicsRoot32BitConstant = 34;
		static constexpr UINT SetComputeRoot32BitConstants = 35;
		static constexpr UINT SetGraphicsRoot32BitConstants = 36;
		static constexpr UINT SetComputeRootConstantBufferView = 37;
		static constexpr UINT SetGraphicsRootConstantBufferView = 38;
		static constexpr UINT SetComputeRootShaderResourceView = 39;
		static constexpr UINT SetGraphicsRootShaderResourceView = 40;
		static constexpr UINT SetComputeRootUnorderedAccessView = 41;
		static constexpr UINT SetGraphicsRootUnorderedAccessView = 42;
		static constexpr UINT IaSetIndexBuffer = 43;
		static constexpr UINT IaSetVertexBuffers = 44;
		static constexpr UINT ExecuteIndirect = 59;
	};

	class ID3D12CommandQueue
	{
	public:
		static constexpr UINT UpdateTileMappings = 8;
		static constexpr UINT CopyTileMappings = 9;
		static constexpr UINT ExecuteCommandLists = 10;
		static constexpr UINT SetMarker = 11;
		static constexpr UINT BeginEvent = 12;
		static constexpr UINT EndEvent = 13;
		static constexpr UINT Signal = 14;
		static constexpr UINT Wait = 15;
		static constexpr UINT GetTimestampFrequency = 16;
		static constexpr UINT GetClockCalibration = 17;
	};

	class ID3D12Device
	{
	public:
		static constexpr UINT CreateCommandQueue = 8;
		static constexpr UINT CreateCommandAllocator = 9;
		static constexpr UINT CreateCommandList = 12;
	};

	class ID3D12Device4
	{
	public:
		static constexpr UINT CreateCommandList1 = 52;
	};
};

// ---- D3D12ProcessGateUtils.hpp ----
#line 1 "D3D12ProcessGateUtils.hpp"
#include <Windows.h>
#include <Shlwapi.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "util_min.h"

enum D3D12ProcessGateRole
{
	D3D12ProcessGateRoleReject = 0,
	D3D12ProcessGateRoleBootstrapOnly = 1,
	D3D12ProcessGateRoleTargetRuntime = 2
};

class D3D12ProcessGateDecision
{
public:
	bool allowed = false;
	D3D12ProcessGateRole role = D3D12ProcessGateRoleReject;
	wchar_t reason[256] = {};
	wchar_t modulePath[MAX_PATH] = {};
	wchar_t moduleDirectory[MAX_PATH] = {};
	wchar_t processPath[MAX_PATH] = {};
	wchar_t processDirectory[MAX_PATH] = {};
	wchar_t configPath[MAX_PATH] = {};
	wchar_t target[MAX_PATH] = {};
	wchar_t loader[MAX_PATH] = {};
};

class D3D12ProcessGateUtils
{
public:
	static bool VerifyIntendedTarget(HINSTANCE module)
	{
		D3D12ProcessGateDecision decision = Evaluate(module);
		DebugDecision(decision);
		return decision.role != D3D12ProcessGateRoleReject;
	}

	static D3D12ProcessGateDecision Evaluate(HINSTANCE module)
	{
		D3D12ProcessGateDecision decision;
		if (!ReadPaths(module, &decision)) {
			CopyReason(&decision, L"path discovery failed");
			return decision;
		}

		if (ReadLoaderConfig(&decision)) {
			if (MatchesConfiguredTarget(decision.processPath, decision.target)) {
				decision.allowed = true;
				decision.role = D3D12ProcessGateRoleTargetRuntime;
				CopyReason(&decision, L"matched configured loader target");
			} else if (MatchesConfiguredLoader(decision.processPath, decision.loader) ||
					(IsSameDirectory(decision.moduleDirectory, decision.processDirectory) &&
						IsBootstrapProcess(decision.processPath))) {
				decision.allowed = true;
				decision.role = D3D12ProcessGateRoleBootstrapOnly;
				CopyReason(&decision, L"matched configured loader bootstrap");
			} else {
				CopyReason(&decision, L"rejected by configured loader target");
			}
			return decision;
		}

		if (IsSameDirectory(decision.moduleDirectory, decision.processDirectory)) {
			decision.allowed = true;
			decision.role = D3D12ProcessGateRoleTargetRuntime;
			CopyReason(&decision, L"same directory proxy fallback");
		} else {
			CopyReason(&decision, L"no configured target and not same directory");
		}
		return decision;
	}

	static bool ReadPaths(HINSTANCE module, D3D12ProcessGateDecision *decision)
	{
		wchar_t *moduleBase = nullptr;
		wchar_t *processBase = nullptr;

		if (!decision)
			return false;
		if (!GetModuleFileNameW(module, decision->modulePath, MAX_PATH))
			return false;
		if (!GetModuleFileNameW(nullptr, decision->processPath, MAX_PATH))
			return false;

		wcsncpy_s(decision->moduleDirectory, MAX_PATH, decision->modulePath, _TRUNCATE);
		wcsncpy_s(decision->processDirectory, MAX_PATH, decision->processPath, _TRUNCATE);

		moduleBase = wcsrchr(decision->moduleDirectory, L'\\');
		processBase = wcsrchr(decision->processDirectory, L'\\');
		if (!moduleBase || !processBase)
			return false;

		*moduleBase = L'\0';
		*processBase = L'\0';

		wcsncpy_s(decision->configPath, MAX_PATH, decision->moduleDirectory, _TRUNCATE);
		PathAppendW(decision->configPath, L"d3dx.ini");
		return true;
	}

	static bool ReadLoaderConfig(D3D12ProcessGateDecision *decision)
	{
		HANDLE file = INVALID_HANDLE_VALUE;
		DWORD fileSize = 0;
		DWORD readSize = 0;
		char *buffer = nullptr;
		const char *section = nullptr;
		char targetUtf8[MAX_PATH] = {};
		char loaderUtf8[MAX_PATH] = {};
		bool foundTarget = false;
		bool foundLoader = false;

		if (!decision)
			return false;

		file = CreateFileW(decision->configPath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE)
			return false;

		fileSize = GetFileSize(file, nullptr);
		if (fileSize == INVALID_FILE_SIZE || fileSize == 0)
			goto out_close;

		buffer = new char[fileSize + 1];
		if (!buffer)
			goto out_close;

		if (!ReadFile(file, buffer, fileSize, &readSize, nullptr) || fileSize != readSize)
			goto out_free;
		buffer[fileSize] = '\0';

		section = find_ini_section_lite(buffer, "loader");
		if (!section)
			goto out_free;
		if (find_ini_setting_lite(section, "target", targetUtf8, MAX_PATH) &&
				MultiByteToWideChar(CP_UTF8, 0, targetUtf8, -1, decision->target, MAX_PATH)) {
			TrimQuotes(decision->target);
			foundTarget = decision->target[0] != L'\0';
		}

		if (find_ini_setting_lite(section, "loader", loaderUtf8, MAX_PATH) &&
				MultiByteToWideChar(CP_UTF8, 0, loaderUtf8, -1, decision->loader, MAX_PATH)) {
			TrimQuotes(decision->loader);
			foundLoader = decision->loader[0] != L'\0';
		}

	out_free:
		delete [] buffer;
	out_close:
		CloseHandle(file);
		return foundTarget || foundLoader;
	}

	static bool MatchesConfiguredTarget(const wchar_t *processPath, const wchar_t *target)
	{
		size_t processLength = 0;
		size_t targetLength = 0;

		if (!processPath || !target || target[0] == L'\0')
			return false;

		processLength = wcslen(processPath);
		targetLength = wcslen(target);
		if (processLength < targetLength)
			return false;

		if (target[0] != L'\\' && target[1] != L':' && processLength > targetLength) {
			if (processPath[processLength - targetLength - 1] != L'\\')
				return false;
		}

		return _wcsicmp(processPath + processLength - targetLength, target) == 0;
	}

	static bool IsSameDirectory(const wchar_t *left, const wchar_t *right)
	{
		if (!left || !right)
			return false;
		return _wcsicmp(left, right) == 0;
	}

	static bool MatchesConfiguredLoader(const wchar_t *processPath, const wchar_t *loader)
	{
		const wchar_t *baseName = nullptr;

		if (!processPath || !loader || loader[0] == L'\0')
			return false;

		baseName = wcsrchr(processPath, L'\\');
		if (baseName)
			baseName++;
		else
			baseName = processPath;

		return _wcsicmp(baseName, loader) == 0;
	}

	static bool IsBootstrapProcess(const wchar_t *processPath)
	{
		const wchar_t *baseName = nullptr;

		if (!processPath)
			return false;

		baseName = wcsrchr(processPath, L'\\');
		if (baseName)
			baseName++;
		else
			baseName = processPath;

		return _wcsicmp(baseName, L"Run.exe") == 0;
	}

	static const wchar_t *RoleName(D3D12ProcessGateRole role)
	{
		switch (role) {
		case D3D12ProcessGateRoleBootstrapOnly:
			return L"bootstrap";
		case D3D12ProcessGateRoleTargetRuntime:
			return L"target";
		default:
			return L"reject";
		}
	}

	static void TrimQuotes(wchar_t *text)
	{
		size_t length = 0;

		if (!text)
			return;

		length = wcslen(text);
		if (length >= 2 && text[0] == L'"' && text[length - 1] == L'"') {
			memmove(text, text + 1, (length - 2) * sizeof(wchar_t));
			text[length - 2] = L'\0';
		}
	}

	static void CopyReason(D3D12ProcessGateDecision *decision, const wchar_t *reason)
	{
		if (!decision || !reason)
			return;
		wcsncpy_s(decision->reason, 256, reason, _TRUNCATE);
	}

	static void DebugDecision(const D3D12ProcessGateDecision &decision)
	{
		wchar_t line[1024] = {};
		swprintf_s(line, 1024,
			L"[Bunny-D3D12Gate] %s role=%s: pid=%lu process=%s module=%s target=%s loader=%s config=%s reason=%s\n",
			decision.allowed ? L"allow" : L"deny",
			RoleName(decision.role),
			GetCurrentProcessId(),
			decision.processPath,
			decision.modulePath,
			decision.target,
			decision.loader,
			decision.configPath,
			decision.reason);
		OutputDebugStringW(line);
	}
};

// ---- D3D12ProxyRuntime.hpp ----
#line 1 "D3D12ProxyRuntime.hpp"
#include <Windows.h>

class D3D12ProxyRuntime
{
public:
	static HMODULE LoadRealD3D12();
	static void UnloadRealD3D12();
	static FARPROC GetRealProc(const char *name);
	static FARPROC GetRealProc(WORD ordinal);
	static UINT_PTR CallRealProc(
		const char *name,
		UINT_PTR a1 = 0, UINT_PTR a2 = 0, UINT_PTR a3 = 0, UINT_PTR a4 = 0,
		UINT_PTR a5 = 0, UINT_PTR a6 = 0, UINT_PTR a7 = 0, UINT_PTR a8 = 0);
	static UINT_PTR CallRealProc(
		WORD ordinal,
		UINT_PTR a1 = 0, UINT_PTR a2 = 0, UINT_PTR a3 = 0, UINT_PTR a4 = 0,
		UINT_PTR a5 = 0, UINT_PTR a6 = 0, UINT_PTR a7 = 0, UINT_PTR a8 = 0);
	static void Initialize(HINSTANCE module);
	static void Shutdown();
};

// ---- D3D12ProxySignatures.hpp ----
#line 1 "D3D12ProxySignatures.hpp"
#include <Windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>

class D3D12ProxySignatures
{
public:
	using CreateDevice = HRESULT(WINAPI *)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
	using GetDebugInterface = HRESULT(WINAPI *)(REFIID, void**);
	using SerializeRootSignature = HRESULT(WINAPI *)(
		const D3D12_ROOT_SIGNATURE_DESC*, D3D_ROOT_SIGNATURE_VERSION, ID3DBlob**, ID3DBlob**);
	using CreateRootSignatureDeserializer = HRESULT(WINAPI *)(LPCVOID, SIZE_T, REFIID, void**);
	using SerializeVersionedRootSignature = HRESULT(WINAPI *)(
		const D3D12_VERSIONED_ROOT_SIGNATURE_DESC*, ID3DBlob**, ID3DBlob**);
	using CreateVersionedRootSignatureDeserializer = HRESULT(WINAPI *)(LPCVOID, SIZE_T, REFIID, void**);
	using EnableExperimentalFeatures = HRESULT(WINAPI *)(UINT, const IID*, void*, UINT*);
	using GetInterface = HRESULT(WINAPI *)(REFCLSID, REFIID, void**);
	using Generic = UINT_PTR(WINAPI *)(
		UINT_PTR, UINT_PTR, UINT_PTR, UINT_PTR,
		UINT_PTR, UINT_PTR, UINT_PTR, UINT_PTR);
};

// ---- D3D12ProxyState.hpp ----
#line 1 "D3D12ProxyState.hpp"
#include <Windows.h>


class D3D12ProxyState
{
public:
	inline static HMODULE RealD3D12 = nullptr;
	inline static D3D12ProxySignatures::CreateDevice OrigCreateDevice = nullptr;
	inline static D3D12ProxySignatures::GetDebugInterface OrigGetDebugInterface = nullptr;
	inline static D3D12ProxySignatures::SerializeRootSignature OrigSerializeRootSignature = nullptr;
	inline static D3D12ProxySignatures::CreateRootSignatureDeserializer OrigCreateRootSignatureDeserializer = nullptr;
	inline static D3D12ProxySignatures::SerializeVersionedRootSignature OrigSerializeVersionedRootSignature = nullptr;
	inline static D3D12ProxySignatures::CreateVersionedRootSignatureDeserializer OrigCreateVersionedRootSignatureDeserializer = nullptr;
	inline static D3D12ProxySignatures::EnableExperimentalFeatures OrigEnableExperimentalFeatures = nullptr;
	inline static D3D12ProxySignatures::GetInterface OrigGetInterface = nullptr;
};

// ---- D3D12ProxyTargetHelper.hpp ----
#line 1 "D3D12ProxyTargetHelper.hpp"
#include <Windows.h>

class D3D12ProxyTargetHelper
{
public:
	static bool VerifyIntendedTarget(HINSTANCE module);
};

// ---- DX12BindingCollectQueue.hpp ----
#line 1 "DX12BindingCollectQueue.hpp"
#include <Windows.h>
#include <d3d12.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>


enum class DX12BindingCollectTaskKind
{
	StateEvent = 0,
	DrawEvent,
	DispatchEvent,
	OutputEvent,
};

class DX12BindingCollectTask
{
public:
	DX12BindingCollectTaskKind kind = DX12BindingCollectTaskKind::StateEvent;
	UINT64 sequence = 0;
	UINT64 captureSessionId = 0;
	DWORD enqueueThreadId = 0;
	ID3D12GraphicsCommandList *commandList = nullptr;
	CommandListBindingState state = {};
	UINT64 descriptorEpoch = 0;
	const char *eventKind = nullptr;
	UINT vertexCountPerInstance = 0;
	UINT indexCountPerInstance = 0;
	UINT instanceCount = 0;
	UINT startVertexLocation = 0;
	UINT startIndexLocation = 0;
	INT baseVertexLocation = 0;
	UINT startInstanceLocation = 0;
	UINT threadGroupCountX = 0;
	UINT threadGroupCountY = 0;
	UINT threadGroupCountZ = 0;
	UINT slot = 0;
	SIZE_T cpuHandle = 0;
	UINT64 drawId = 0;
};

class DX12BindingCollectQueue
{
public:
	inline static std::mutex mutex;
	inline static std::condition_variable wake;
	inline static std::condition_variable idle;
	inline static std::deque<DX12BindingCollectTask> tasks;
	inline static std::thread worker;
	inline static std::atomic<bool> started = false;
	inline static std::atomic<bool> stopRequested = false;
	inline static std::atomic<uint64_t> nextSequence = 1;
	inline static std::atomic<uint64_t> enqueued = 0;
	inline static std::atomic<uint64_t> dequeued = 0;
	inline static std::atomic<uint64_t> completed = 0;
	inline static std::atomic<uint64_t> completedSequence = 0;
	inline static std::atomic<uint64_t> maxDepth = 0;
	inline static std::atomic<uint64_t> maxPendingAtDrain = 0;
	inline static std::atomic<uint64_t> drainCalls = 0;
	inline static std::atomic<uint64_t> drainWaitUs = 0;
	inline static std::atomic<uint64_t> drainMaxUs = 0;
	inline static std::atomic<uint64_t> taskElapsedUs = 0;
	inline static std::atomic<uint64_t> taskMaxUs = 0;
	inline static uint64_t activeTasks = 0;

	static void Start();
	static void Stop();
	static void DrainForSnapshot(const char *reason);
	static void BeginFrameDrain();
	static bool EnqueueStateEvent(
		ID3D12GraphicsCommandList *commandList,
		const CommandListBindingState &state,
		const char *kind,
		UINT64 descriptorEpoch,
		UINT64 captureSessionId);
	static bool EnqueueDrawEvent(
		ID3D12GraphicsCommandList *commandList,
		const CommandListBindingState &state,
		const char *kind,
		UINT64 descriptorEpoch,
		UINT64 captureSessionId,
		UINT vertexCountPerInstance,
		UINT indexCountPerInstance,
		UINT instanceCount,
		UINT startVertexLocation,
		UINT startIndexLocation,
		INT baseVertexLocation,
		UINT startInstanceLocation);
	static bool EnqueueDispatchEvent(
		ID3D12GraphicsCommandList *commandList,
		const CommandListBindingState &state,
		UINT64 descriptorEpoch,
		UINT64 captureSessionId,
		UINT threadGroupCountX,
		UINT threadGroupCountY,
		UINT threadGroupCountZ);
	static bool EnqueueOutputEvent(
		ID3D12GraphicsCommandList *commandList,
		const CommandListBindingState &state,
		const char *kind,
		UINT slot,
		SIZE_T cpuHandle,
		UINT64 drawId,
		UINT64 captureSessionId);
	static size_t PendingCount();
	static UINT64 LastSubmittedSequence();

	static bool EnqueueTask(DX12BindingCollectTask &&task);
	static void WorkerLoop();
	static void WaitForIdle(const char *reason);
	static void WaitForSubmittedSequence(UINT64 targetSequence, const char *reason);
	static void ProcessTask(DX12BindingCollectTask *task, const char *source);
	static void ProcessStateEventTask(DX12BindingCollectTask *task, const char *source);
	static void ProcessDrawEventTask(DX12BindingCollectTask *task, const char *source);
	static void ProcessDispatchEventTask(DX12BindingCollectTask *task, const char *source);
	static void ProcessOutputEventTask(DX12BindingCollectTask *task, const char *source);
	static void UpdateMaxDepth(size_t depth);
	static void UpdateMaxPendingAtDrain(size_t depth);
	static void RecordTaskStats(int64_t elapsedUs);
	static void RecordDrainWaitStats(int64_t elapsedUs);
	static void LogStats(const char *event, uint64_t sequence, size_t depth);
};

// ---- DX12BindingTrackerQuery.hpp ----
#line 1 "DX12BindingTrackerQuery.hpp"
#include <Windows.h>
#include <d3d12.h>

#include <unordered_map>
#include <vector>


class DX12BindingTrackerQuery
{
public:
	static UINT DescriptorCountForRange(
		const DX12DescriptorHeapSummary &heap,
		UINT64 tableBaseIndex,
		const DX12RootDescriptorRangeSummary &range)
	{
		const UINT64 firstDescriptor = tableBaseIndex + range.effectiveOffset;
		if (firstDescriptor >= heap.numDescriptors)
			return 0;

		const UINT64 heapAvailable = heap.numDescriptors - firstDescriptor;
		const UINT64 declaredCount =
			range.numDescriptors == UINT_MAX ? heapAvailable : range.numDescriptors;
		const UINT64 boundedCount = min(declaredCount, heapAvailable);
		return boundedCount > UINT_MAX ? UINT_MAX : static_cast<UINT>(boundedCount);
	}
	static const char *HeapTypeName(UINT type)
	{
		switch (type)
		{
		case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV:
			return "CBV_SRV_UAV";
		case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:
			return "SAMPLER";
		case D3D12_DESCRIPTOR_HEAP_TYPE_RTV:
			return "RTV";
		case D3D12_DESCRIPTOR_HEAP_TYPE_DSV:
			return "DSV";
		default:
			return "UNKNOWN";
		}
	}

	static const char *ResourceDimensionName(D3D12_RESOURCE_DIMENSION dimension)
	{
		switch (dimension)
		{
		case D3D12_RESOURCE_DIMENSION_BUFFER:
			return "BUFFER";
		case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
			return "TEXTURE1D";
		case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
			return "TEXTURE2D";
		case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
			return "TEXTURE3D";
		default:
			return "UNKNOWN";
		}
	}

	static const DX12RootParameterSummary *FindRootParameter(
		const DX12RootSignatureSummary &rootSignature, UINT rootParameterIndex)
	{
		for (const DX12RootParameterSummary &parameter : rootSignature.parameters)
		{
			if (parameter.rootParameterIndex == rootParameterIndex)
				return &parameter;
		}
		return nullptr;
	}

	static bool GetRootSignatureForEvent(
		const BindingEvent &event, bool compute, DX12RootSignatureSummary *summary)
	{
		if (!summary)
			return false;
		ID3D12RootSignature *rootSignature = compute ? event.computeRootSignature : event.graphicsRootSignature;
		if (!rootSignature && event.shaderInfo.psoIndex)
			DX12GetPsoRootSignature(event.shaderInfo.psoIndex, &rootSignature);
		return rootSignature && DX12GetRootSignatureSummary(rootSignature, summary);
	}

	static UINT HeapTypeForDescriptorRange(UINT rangeType)
	{
		if (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER)
			return D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
		return D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	}

	static bool TableMatchesHeap(const RootTableState &table, const DX12DescriptorHeapSummary &heap)
	{
		if (!table.valid || heap.increment == 0 || heap.gpuStart == 0)
			return false;
		const UINT64 begin = heap.gpuStart;
		const UINT64 end = begin + static_cast<UINT64>(heap.numDescriptors) * heap.increment;
		return table.baseDescriptor.ptr >= begin && table.baseDescriptor.ptr < end;
	}

	static const DX12DescriptorHeapSummary *FindHeapForGpuHandle(
		const std::vector<DX12DescriptorHeapSummary> &heaps,
		const RootTableState &table,
		UINT requiredType)
	{
		for (const DX12DescriptorHeapSummary &heap : heaps)
		{
			if (heap.type != requiredType || heap.increment == 0 || heap.gpuStart == 0)
				continue;
			const UINT64 begin = heap.gpuStart;
			const UINT64 end = begin + static_cast<UINT64>(heap.numDescriptors) * heap.increment;
			if (table.baseDescriptor.ptr >= begin && table.baseDescriptor.ptr < end)
				return &heap;
		}
		return nullptr;
	}

	static const DX12DescriptorHeapSummary *FindHeapForTableRange(
		const std::vector<DX12DescriptorHeapSummary> &heaps,
		const RootTableState &table,
		const DX12RootDescriptorRangeSummary *range,
		UINT heapType)
	{
		const UINT requiredType = range ? HeapTypeForDescriptorRange(range->rangeType) : heapType;
		for (const DX12DescriptorHeapSummary &heap : heaps)
		{
			if (heap.type != requiredType || !TableMatchesHeap(table, heap))
				continue;
			return &heap;
		}
		return nullptr;
	}

	static const DX12DescriptorSummary *FindDescriptorByCpuHandle(
		const std::unordered_map<SIZE_T, const DX12DescriptorSummary *> &descriptorsByCpuHandle,
		SIZE_T cpuHandle)
	{
		auto it = descriptorsByCpuHandle.find(cpuHandle);
		return it != descriptorsByCpuHandle.end() ? it->second : nullptr;
	}

	static void BuildDescriptorLookup(
		const std::vector<DX12DescriptorSummary> &descriptors,
		std::unordered_map<SIZE_T, const DX12DescriptorSummary *> *descriptorsByCpuHandle)
	{
		if (!descriptorsByCpuHandle)
			return;

		descriptorsByCpuHandle->clear();
		descriptorsByCpuHandle->reserve(descriptors.size());
		for (const DX12DescriptorSummary &descriptor : descriptors)
			(*descriptorsByCpuHandle)[descriptor.cpuHandle] = &descriptor;
	}

	static bool IsDrawEvent(const BindingEvent &event)
	{
		return event.kind == "draw" || event.kind == "draw_indexed";
	}

	static bool IsDispatchEvent(const BindingEvent &event)
	{
		return event.kind == "dispatch";
	}
};

// ---- DX12CaptureLedger.hpp ----
#line 1 "DX12CaptureLedger.hpp"
#include <Windows.h>

#include <string>
#include <unordered_set>
#include <vector>


class DX12CaptureLedgerSnapshot
{
public:
	std::vector<BindingEvent> events;
	std::vector<OutputBindingEvent> outputEvents;
	UINT64 droppedEvents = 0;
	UINT64 droppedOutputEvents = 0;
	UINT64 beginPresent = 0;
	UINT64 ledgerEpoch = 0;
	UINT64 captureSessionId = 0;
};

enum class DX12CaptureLedgerRecordResult
{
	NotActive = 0,
	Stored = 1,
	Duplicate = 2,
	InactiveSession = 3,
};

class DX12CaptureOutputKey
{
public:
	ID3D12GraphicsCommandList *commandList = nullptr;
	SIZE_T cpuHandle = 0;
	std::string kind;
	UINT slot = 0;
	UINT64 drawId = 0;
	UINT64 dispatchId = 0;

	bool operator==(const DX12CaptureOutputKey &other) const
	{
		return commandList == other.commandList &&
			cpuHandle == other.cpuHandle &&
			kind == other.kind &&
			slot == other.slot &&
			drawId == other.drawId &&
			dispatchId == other.dispatchId;
	}
};

class DX12CaptureOutputKeyHash
{
public:
	size_t operator()(const DX12CaptureOutputKey &key) const
	{
		size_t hash = std::hash<void*>()(
			static_cast<void*>(key.commandList));
		hash = Mix(hash, key.cpuHandle);
		hash = Mix(hash, std::hash<std::string>()(key.kind));
		hash = Mix(hash, key.slot);
		hash = Mix(hash, key.drawId);
		return Mix(hash, key.dispatchId);
	}

private:
	static size_t Mix(size_t hash, UINT64 value)
	{
		return hash ^ (static_cast<size_t>(value) + 0x9e3779b97f4a7c15ull +
			(hash << 6) + (hash >> 2));
	}
};

class DX12CaptureLedger
{
public:
	static SRWLOCK Lock;
	static bool Active;
	static UINT64 BeginPresent;
	static UINT64 LedgerEpoch;
	static UINT64 CaptureSessionId;
	static UINT64 EventSerial;
	static UINT64 OutputEventSerial;
	static UINT64 DroppedEvents;
	static UINT64 DroppedOutputEvents;
	static std::vector<BindingEvent> Events;
	static std::vector<OutputBindingEvent> OutputEvents;
	static std::unordered_set<DX12CaptureOutputKey, DX12CaptureOutputKeyHash>
		OutputKeys;

	static void BeginCapture(UINT64 present);
	static void EndCapture();
	static void Clear();
	static bool IsActive();
	static UINT64 CurrentCaptureSessionId();
	static UINT64 LastCaptureSessionId();
	static bool IsSessionCurrent(UINT64 captureSessionId);
	static DX12CaptureLedgerRecordResult RecordEvent(BindingEvent *event);
	static DX12CaptureLedgerRecordResult TryStoreOutputEvent(OutputBindingEvent *event);
	static void GetSnapshot(DX12CaptureLedgerSnapshot *snapshot);
	static void ReleaseEventsLocked();
	static void LogSnapshotHealth(const char *stage, const DX12CaptureLedgerSnapshot &snapshot);
};

// ---- DX12CommandListHookCallbacks.hpp ----
#line 1 "DX12CommandListHookCallbacks.hpp"
#include <Windows.h>
#include <d3d12.h>


class DX12CommandListHookCallbacks
{
public:
	static DX12IaReplacementExecutorCallbacks MakeIaReplacementCallbacks(
		ID3D12GraphicsCommandList *commandList);
	static const DX12IaReplacementExecutorCallbacks &GetCachedIaReplacementCallbacks(
		ID3D12GraphicsCommandList *commandList);
	static void RegisterCommandListBase(
		ID3D12GraphicsCommandList *baseList, ID3D12PipelineState *initialState);
	static void RegisterCommandList(IUnknown *commandList, ID3D12PipelineState *initialState);
	static void RegisterSubmittedCommandList(IUnknown *commandList);
	static void RegisterCommandQueue(IUnknown *commandQueue);

	static HRESULT STDMETHODCALLTYPE HookedCreateCommandQueue(
		ID3D12Device *device, const D3D12_COMMAND_QUEUE_DESC *desc, REFIID riid, void **commandQueue);
	static HRESULT STDMETHODCALLTYPE HookedCreateCommandAllocator(
		ID3D12Device *device, D3D12_COMMAND_LIST_TYPE type, REFIID riid, void **allocator);
	static HRESULT STDMETHODCALLTYPE HookedCreateCommandList(
		ID3D12Device *device, UINT nodeMask, D3D12_COMMAND_LIST_TYPE type,
		ID3D12CommandAllocator *allocator, ID3D12PipelineState *initialState,
		REFIID riid, void **commandList);
	static HRESULT STDMETHODCALLTYPE HookedCreateCommandList1(
		ID3D12Device4 *device, UINT nodeMask, D3D12_COMMAND_LIST_TYPE type,
		D3D12_COMMAND_LIST_FLAGS flags, REFIID riid, void **commandList);
	static void STDMETHODCALLTYPE HookedQueueUpdateTileMappings(
		ID3D12CommandQueue *queue, ID3D12Resource *resource, UINT numResourceRegions,
		const D3D12_TILED_RESOURCE_COORDINATE *resourceRegionStartCoordinates,
		const D3D12_TILE_REGION_SIZE *resourceRegionSizes, ID3D12Heap *heap, UINT numRanges,
		const D3D12_TILE_RANGE_FLAGS *rangeFlags, const UINT *heapRangeStartOffsets,
		const UINT *rangeTileCounts, D3D12_TILE_MAPPING_FLAGS flags);
	static void STDMETHODCALLTYPE HookedQueueCopyTileMappings(
		ID3D12CommandQueue *queue, ID3D12Resource *dstResource,
		const D3D12_TILED_RESOURCE_COORDINATE *dstRegionStartCoordinate,
		ID3D12Resource *srcResource, const D3D12_TILED_RESOURCE_COORDINATE *srcRegionStartCoordinate,
		const D3D12_TILE_REGION_SIZE *regionSize, D3D12_TILE_MAPPING_FLAGS flags);
	static void STDMETHODCALLTYPE HookedQueueExecuteCommandLists(
		ID3D12CommandQueue *queue, UINT numCommandLists, ID3D12CommandList *const *commandLists);
	static void STDMETHODCALLTYPE HookedQueueSetMarker(
		ID3D12CommandQueue *queue, UINT metadata, const void *data, UINT size);
	static void STDMETHODCALLTYPE HookedQueueBeginEvent(
		ID3D12CommandQueue *queue, UINT metadata, const void *data, UINT size);
	static void STDMETHODCALLTYPE HookedQueueEndEvent(ID3D12CommandQueue *queue);
	static HRESULT STDMETHODCALLTYPE HookedQueueSignal(
		ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 value);
	static HRESULT STDMETHODCALLTYPE HookedQueueWait(
		ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 value);
	static HRESULT STDMETHODCALLTYPE HookedQueueGetTimestampFrequency(
		ID3D12CommandQueue *queue, UINT64 *frequency);
	static HRESULT STDMETHODCALLTYPE HookedQueueGetClockCalibration(
		ID3D12CommandQueue *queue, UINT64 *gpuTimestamp, UINT64 *cpuTimestamp);
	static HRESULT STDMETHODCALLTYPE HookedCloseCommandList(ID3D12GraphicsCommandList *commandList);
	static HRESULT STDMETHODCALLTYPE HookedResetCommandList(
		ID3D12GraphicsCommandList *commandList, ID3D12CommandAllocator *allocator,
		ID3D12PipelineState *initialState);
	static void STDMETHODCALLTYPE HookedClearState(
		ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState);
	static void STDMETHODCALLTYPE HookedDrawInstanced(
		ID3D12GraphicsCommandList *commandList, UINT vertexCountPerInstance, UINT instanceCount,
		UINT startVertexLocation, UINT startInstanceLocation);
	static void STDMETHODCALLTYPE HookedDrawIndexedInstanced(
		ID3D12GraphicsCommandList *commandList, UINT indexCountPerInstance, UINT instanceCount,
		UINT startIndexLocation, INT baseVertexLocation, UINT startInstanceLocation);
	static void STDMETHODCALLTYPE HookedDispatch(
		ID3D12GraphicsCommandList *commandList, UINT threadGroupCountX,
		UINT threadGroupCountY, UINT threadGroupCountZ);
	static void STDMETHODCALLTYPE HookedCopyBufferRegion(
		ID3D12GraphicsCommandList *commandList, ID3D12Resource *dstBuffer, UINT64 dstOffset,
		ID3D12Resource *srcBuffer, UINT64 srcOffset, UINT64 numBytes);
	static void STDMETHODCALLTYPE HookedCopyTextureRegion(
		ID3D12GraphicsCommandList *commandList, const D3D12_TEXTURE_COPY_LOCATION *dst,
		UINT dstX, UINT dstY, UINT dstZ, const D3D12_TEXTURE_COPY_LOCATION *src,
		const D3D12_BOX *srcBox);
	static void STDMETHODCALLTYPE HookedCopyResource(
		ID3D12GraphicsCommandList *commandList, ID3D12Resource *dstResource, ID3D12Resource *srcResource);
	static void STDMETHODCALLTYPE HookedCopyTiles(
		ID3D12GraphicsCommandList *commandList, ID3D12Resource *tiledResource,
		const D3D12_TILED_RESOURCE_COORDINATE *tileRegionStartCoordinate,
		const D3D12_TILE_REGION_SIZE *tileRegionSize, ID3D12Resource *buffer,
		UINT64 bufferStartOffsetInBytes, D3D12_TILE_COPY_FLAGS flags);
	static void STDMETHODCALLTYPE HookedResolveSubresource(
		ID3D12GraphicsCommandList *commandList, ID3D12Resource *dstResource, UINT dstSubresource,
		ID3D12Resource *srcResource, UINT srcSubresource, DXGI_FORMAT format);
	static void STDMETHODCALLTYPE HookedIASetPrimitiveTopology(
		ID3D12GraphicsCommandList *commandList, D3D12_PRIMITIVE_TOPOLOGY topology);
	static void STDMETHODCALLTYPE HookedRSSetViewports(
		ID3D12GraphicsCommandList *commandList, UINT count, const D3D12_VIEWPORT *viewports);
	static void STDMETHODCALLTYPE HookedRSSetScissorRects(
		ID3D12GraphicsCommandList *commandList, UINT count, const D3D12_RECT *rects);
	static void STDMETHODCALLTYPE HookedOMSetBlendFactor(
		ID3D12GraphicsCommandList *commandList, const FLOAT blendFactor[4]);
	static void STDMETHODCALLTYPE HookedOMSetStencilRef(
		ID3D12GraphicsCommandList *commandList, UINT stencilRef);
	static void STDMETHODCALLTYPE HookedSetPipelineState(
		ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState);
	static void STDMETHODCALLTYPE HookedResourceBarrier(
		ID3D12GraphicsCommandList *commandList, UINT numBarriers,
		const D3D12_RESOURCE_BARRIER *barriers);
	static void STDMETHODCALLTYPE HookedExecuteBundle(
		ID3D12GraphicsCommandList *commandList, ID3D12GraphicsCommandList *bundle);
	static void STDMETHODCALLTYPE HookedSetGraphicsRootUnorderedAccessView(
		ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
		D3D12_GPU_VIRTUAL_ADDRESS address);
	static void STDMETHODCALLTYPE HookedIASetIndexBuffer(
		ID3D12GraphicsCommandList *commandList, const D3D12_INDEX_BUFFER_VIEW *view);
	static void STDMETHODCALLTYPE HookedIASetVertexBuffers(
		ID3D12GraphicsCommandList *commandList, UINT startSlot, UINT count,
		const D3D12_VERTEX_BUFFER_VIEW *views);
	static void STDMETHODCALLTYPE HookedSOSetTargets(
		ID3D12GraphicsCommandList *commandList, UINT startSlot, UINT count,
		const D3D12_STREAM_OUTPUT_BUFFER_VIEW *views);
	static void STDMETHODCALLTYPE HookedOMSetRenderTargets(
		ID3D12GraphicsCommandList *commandList, UINT numRenderTargetDescriptors,
		const D3D12_CPU_DESCRIPTOR_HANDLE *renderTargetDescriptors,
		BOOL singleHandleToDescriptorRange, const D3D12_CPU_DESCRIPTOR_HANDLE *depthStencilDescriptor);
	static void STDMETHODCALLTYPE HookedClearDepthStencilView(
		ID3D12GraphicsCommandList *commandList, D3D12_CPU_DESCRIPTOR_HANDLE depthStencilView,
		D3D12_CLEAR_FLAGS clearFlags, FLOAT depth, UINT8 stencil, UINT numRects,
		const D3D12_RECT *rects);
	static void STDMETHODCALLTYPE HookedClearRenderTargetView(
		ID3D12GraphicsCommandList *commandList, D3D12_CPU_DESCRIPTOR_HANDLE renderTargetView,
		const FLOAT colorRGBA[4], UINT numRects, const D3D12_RECT *rects);
	static void STDMETHODCALLTYPE HookedClearUnorderedAccessViewUint(
		ID3D12GraphicsCommandList *commandList, D3D12_GPU_DESCRIPTOR_HANDLE viewGpuHandleInCurrentHeap,
		D3D12_CPU_DESCRIPTOR_HANDLE viewCpuHandle, ID3D12Resource *resource, const UINT values[4],
		UINT numRects, const D3D12_RECT *rects);
	static void STDMETHODCALLTYPE HookedClearUnorderedAccessViewFloat(
		ID3D12GraphicsCommandList *commandList, D3D12_GPU_DESCRIPTOR_HANDLE viewGpuHandleInCurrentHeap,
		D3D12_CPU_DESCRIPTOR_HANDLE viewCpuHandle, ID3D12Resource *resource, const FLOAT values[4],
		UINT numRects, const D3D12_RECT *rects);
	static void STDMETHODCALLTYPE HookedDiscardResource(
		ID3D12GraphicsCommandList *commandList, ID3D12Resource *resource,
		const D3D12_DISCARD_REGION *region);
	static void STDMETHODCALLTYPE HookedBeginQuery(
		ID3D12GraphicsCommandList *commandList, ID3D12QueryHeap *queryHeap,
		D3D12_QUERY_TYPE type, UINT index);
	static void STDMETHODCALLTYPE HookedEndQuery(
		ID3D12GraphicsCommandList *commandList, ID3D12QueryHeap *queryHeap,
		D3D12_QUERY_TYPE type, UINT index);
	static void STDMETHODCALLTYPE HookedResolveQueryData(
		ID3D12GraphicsCommandList *commandList, ID3D12QueryHeap *queryHeap, D3D12_QUERY_TYPE type,
		UINT startIndex, UINT numQueries, ID3D12Resource *destinationBuffer,
		UINT64 alignedDestinationBufferOffset);
	static void STDMETHODCALLTYPE HookedSetPredication(
		ID3D12GraphicsCommandList *commandList, ID3D12Resource *buffer,
		UINT64 alignedBufferOffset, D3D12_PREDICATION_OP operation);
	static void STDMETHODCALLTYPE HookedCommandListSetMarker(
		ID3D12GraphicsCommandList *commandList, UINT metadata, const void *data, UINT size);
	static void STDMETHODCALLTYPE HookedCommandListBeginEvent(
		ID3D12GraphicsCommandList *commandList, UINT metadata, const void *data, UINT size);
	static void STDMETHODCALLTYPE HookedCommandListEndEvent(ID3D12GraphicsCommandList *commandList);
	static void STDMETHODCALLTYPE HookedExecuteIndirect(
		ID3D12GraphicsCommandList *commandList, ID3D12CommandSignature *commandSignature,
		UINT maxCommandCount, ID3D12Resource *argumentBuffer, UINT64 argumentBufferOffset,
		ID3D12Resource *countBuffer, UINT64 countBufferOffset);
};

// ---- DX12CommandListHookSignatures.hpp ----
#line 1 "DX12CommandListHookSignatures.hpp"
#include <Windows.h>
#include <d3d12.h>

class DX12CommandListHookSignatures
{
public:
	using CreateCommandQueue = HRESULT(STDMETHODCALLTYPE *)(ID3D12Device*, const D3D12_COMMAND_QUEUE_DESC*, REFIID, void**);
	using CreateCommandAllocator = HRESULT(STDMETHODCALLTYPE *)(ID3D12Device*, D3D12_COMMAND_LIST_TYPE, REFIID, void**);
	using CreateCommandList = HRESULT(STDMETHODCALLTYPE *)(ID3D12Device*, UINT, D3D12_COMMAND_LIST_TYPE, ID3D12CommandAllocator*, ID3D12PipelineState*, REFIID, void**);
	using CreateCommandList1 = HRESULT(STDMETHODCALLTYPE *)(ID3D12Device4*, UINT, D3D12_COMMAND_LIST_TYPE, D3D12_COMMAND_LIST_FLAGS, REFIID, void**);

	using QueueUpdateTileMappings = void(STDMETHODCALLTYPE *)(ID3D12CommandQueue*, ID3D12Resource*, UINT, const D3D12_TILED_RESOURCE_COORDINATE*, const D3D12_TILE_REGION_SIZE*, ID3D12Heap*, UINT, const D3D12_TILE_RANGE_FLAGS*, const UINT*, const UINT*, D3D12_TILE_MAPPING_FLAGS);
	using QueueCopyTileMappings = void(STDMETHODCALLTYPE *)(ID3D12CommandQueue*, ID3D12Resource*, const D3D12_TILED_RESOURCE_COORDINATE*, ID3D12Resource*, const D3D12_TILED_RESOURCE_COORDINATE*, const D3D12_TILE_REGION_SIZE*, D3D12_TILE_MAPPING_FLAGS);
	using QueueExecuteCommandLists = void(STDMETHODCALLTYPE *)(ID3D12CommandQueue*, UINT, ID3D12CommandList *const*);
	using QueueSetMarker = void(STDMETHODCALLTYPE *)(ID3D12CommandQueue*, UINT, const void*, UINT);
	using QueueBeginEvent = void(STDMETHODCALLTYPE *)(ID3D12CommandQueue*, UINT, const void*, UINT);
	using QueueEndEvent = void(STDMETHODCALLTYPE *)(ID3D12CommandQueue*);
	using QueueSignal = HRESULT(STDMETHODCALLTYPE *)(ID3D12CommandQueue*, ID3D12Fence*, UINT64);
	using QueueWait = HRESULT(STDMETHODCALLTYPE *)(ID3D12CommandQueue*, ID3D12Fence*, UINT64);
	using QueueGetTimestampFrequency = HRESULT(STDMETHODCALLTYPE *)(ID3D12CommandQueue*, UINT64*);
	using QueueGetClockCalibration = HRESULT(STDMETHODCALLTYPE *)(ID3D12CommandQueue*, UINT64*, UINT64*);

	using CloseCommandList = HRESULT(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*);
	using ResetCommandList = HRESULT(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12CommandAllocator*, ID3D12PipelineState*);
	using ClearState = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12PipelineState*);
	using DrawInstanced = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, UINT, UINT, UINT);
	using DrawIndexedInstanced = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, UINT, UINT, INT, UINT);
	using Dispatch = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, UINT, UINT);
	using CopyBufferRegion = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12Resource*, UINT64, ID3D12Resource*, UINT64, UINT64);
	using CopyTextureRegion = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, const D3D12_TEXTURE_COPY_LOCATION*, UINT, UINT, UINT, const D3D12_TEXTURE_COPY_LOCATION*, const D3D12_BOX*);
	using CopyResource = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12Resource*, ID3D12Resource*);
	using CopyTiles = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12Resource*, const D3D12_TILED_RESOURCE_COORDINATE*, const D3D12_TILE_REGION_SIZE*, ID3D12Resource*, UINT64, D3D12_TILE_COPY_FLAGS);
	using ResolveSubresource = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12Resource*, UINT, ID3D12Resource*, UINT, DXGI_FORMAT);
	using IaSetPrimitiveTopology = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, D3D12_PRIMITIVE_TOPOLOGY);
	using RsSetViewports = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, const D3D12_VIEWPORT*);
	using RsSetScissorRects = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, const D3D12_RECT*);
	using OmSetBlendFactor = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, const FLOAT[4]);
	using OmSetStencilRef = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT);
	using SetPipelineState = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12PipelineState*);
	using ResourceBarrier = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, const D3D12_RESOURCE_BARRIER*);
	using ExecuteBundle = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12GraphicsCommandList*);
	using SetDescriptorHeaps = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, ID3D12DescriptorHeap *const*);
	using SetRootSignature = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12RootSignature*);
	using SetRootDescriptorTable = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_DESCRIPTOR_HANDLE);
	using SetRoot32BitConstant = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, UINT, UINT);
	using SetRoot32BitConstants = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, UINT, const void*, UINT);
	using SetRootGpuVa = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_VIRTUAL_ADDRESS);
	using IaSetIndexBuffer = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, const D3D12_INDEX_BUFFER_VIEW*);
	using IaSetVertexBuffers = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, UINT, const D3D12_VERTEX_BUFFER_VIEW*);
	using SoSetTargets = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, UINT, const D3D12_STREAM_OUTPUT_BUFFER_VIEW*);
	using OmSetRenderTargets = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, const D3D12_CPU_DESCRIPTOR_HANDLE*, BOOL, const D3D12_CPU_DESCRIPTOR_HANDLE*);
	using ClearDepthStencilView = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_CLEAR_FLAGS, FLOAT, UINT8, UINT, const D3D12_RECT*);
	using ClearRenderTargetView = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, D3D12_CPU_DESCRIPTOR_HANDLE, const FLOAT[4], UINT, const D3D12_RECT*);
	using ClearUavUint = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, D3D12_GPU_DESCRIPTOR_HANDLE, D3D12_CPU_DESCRIPTOR_HANDLE, ID3D12Resource*, const UINT[4], UINT, const D3D12_RECT*);
	using ClearUavFloat = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, D3D12_GPU_DESCRIPTOR_HANDLE, D3D12_CPU_DESCRIPTOR_HANDLE, ID3D12Resource*, const FLOAT[4], UINT, const D3D12_RECT*);
	using DiscardResource = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12Resource*, const D3D12_DISCARD_REGION*);
	using BeginQuery = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12QueryHeap*, D3D12_QUERY_TYPE, UINT);
	using EndQuery = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12QueryHeap*, D3D12_QUERY_TYPE, UINT);
	using ResolveQueryData = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12QueryHeap*, D3D12_QUERY_TYPE, UINT, UINT, ID3D12Resource*, UINT64);
	using SetPredication = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12Resource*, UINT64, D3D12_PREDICATION_OP);
	using CommandListSetMarker = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, const void*, UINT);
	using CommandListBeginEvent = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, const void*, UINT);
	using CommandListEndEvent = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*);
	using ExecuteIndirect = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12CommandSignature*, UINT, ID3D12Resource*, UINT64, ID3D12Resource*, UINT64);
};

// ---- DX12ThreadLocalCommandListOriginal.hpp ----
#line 1 "DX12ThreadLocalCommandListOriginal.hpp"
class DX12ThreadLocalCommandListOriginal
{
public:
	void *target = nullptr;
	void *original = nullptr;
	bool resolved = false;
};

// ---- DX12ThreadLocalIaCallbacksCache.hpp ----
#line 1 "DX12ThreadLocalIaCallbacksCache.hpp"
#include <d3d12.h>


class DX12ThreadLocalIaCallbacksCache
{
public:
	ID3D12GraphicsCommandList *commandList = nullptr;
	DX12IaReplacementExecutorCallbacks callbacks;
};

// ---- DX12VtableBaseCache.hpp ----
#line 1 "DX12VtableBaseCache.hpp"
#include <array>

class DX12VtableBaseCache
{
public:
	void *vtable = nullptr;
	std::array<void*, 256> originals{};
	std::array<bool, 256> slotResolved{};
};

// ---- DX12CommandListHookState.hpp ----
#line 1 "DX12CommandListHookState.hpp"
#include <Windows.h>
#include <d3d12.h>

#include <unordered_set>


class DX12CommandListHookState
{
public:
	static DX12CommandListHookSignatures::CreateCommandQueue OrigCreateCommandQueue;
	static DX12CommandListHookSignatures::CreateCommandAllocator OrigCreateCommandAllocator;
	static DX12CommandListHookSignatures::CreateCommandList OrigCreateCommandList;
	static DX12CommandListHookSignatures::CreateCommandList1 OrigCreateCommandList1;

	static DX12CommandListHookSignatures::QueueUpdateTileMappings OrigQueueUpdateTileMappings;
	static DX12CommandListHookSignatures::QueueCopyTileMappings OrigQueueCopyTileMappings;
	static DX12CommandListHookSignatures::QueueExecuteCommandLists OrigQueueExecuteCommandLists;
	static DX12CommandListHookSignatures::QueueSetMarker OrigQueueSetMarker;
	static DX12CommandListHookSignatures::QueueBeginEvent OrigQueueBeginEvent;
	static DX12CommandListHookSignatures::QueueEndEvent OrigQueueEndEvent;
	static DX12CommandListHookSignatures::QueueSignal OrigQueueSignal;
	static DX12CommandListHookSignatures::QueueWait OrigQueueWait;
	static DX12CommandListHookSignatures::QueueGetTimestampFrequency OrigQueueGetTimestampFrequency;
	static DX12CommandListHookSignatures::QueueGetClockCalibration OrigQueueGetClockCalibration;

	static DX12CommandListHookSignatures::CloseCommandList OrigCloseCommandList;
	static DX12CommandListHookSignatures::ResetCommandList OrigResetCommandList;
	static DX12CommandListHookSignatures::ClearState OrigClearState;
	static DX12CommandListHookSignatures::DrawInstanced OrigDrawInstanced;
	static DX12CommandListHookSignatures::DrawIndexedInstanced OrigDrawIndexedInstanced;
	static DX12CommandListHookSignatures::Dispatch OrigDispatch;
	static DX12CommandListHookSignatures::CopyBufferRegion OrigCopyBufferRegion;
	static DX12CommandListHookSignatures::CopyTextureRegion OrigCopyTextureRegion;
	static DX12CommandListHookSignatures::CopyResource OrigCopyResource;
	static DX12CommandListHookSignatures::CopyTiles OrigCopyTiles;
	static DX12CommandListHookSignatures::ResolveSubresource OrigResolveSubresource;
	static DX12CommandListHookSignatures::IaSetPrimitiveTopology OrigIASetPrimitiveTopology;
	static DX12CommandListHookSignatures::RsSetViewports OrigRSSetViewports;
	static DX12CommandListHookSignatures::RsSetScissorRects OrigRSSetScissorRects;
	static DX12CommandListHookSignatures::OmSetBlendFactor OrigOMSetBlendFactor;
	static DX12CommandListHookSignatures::OmSetStencilRef OrigOMSetStencilRef;
	static DX12CommandListHookSignatures::SetPipelineState OrigSetPipelineState;
	static DX12CommandListHookSignatures::ResourceBarrier OrigResourceBarrier;
	static DX12CommandListHookSignatures::ExecuteBundle OrigExecuteBundle;
	static DX12CommandListHookSignatures::SetDescriptorHeaps OrigSetDescriptorHeaps;
	static DX12CommandListHookSignatures::SetRootSignature OrigSetComputeRootSignature;
	static DX12CommandListHookSignatures::SetRootSignature OrigSetGraphicsRootSignature;
	static DX12CommandListHookSignatures::SetRootDescriptorTable OrigSetComputeRootDescriptorTable;
	static DX12CommandListHookSignatures::SetRootDescriptorTable OrigSetGraphicsRootDescriptorTable;
	static DX12CommandListHookSignatures::SetRoot32BitConstant OrigSetComputeRoot32BitConstant;
	static DX12CommandListHookSignatures::SetRoot32BitConstant OrigSetGraphicsRoot32BitConstant;
	static DX12CommandListHookSignatures::SetRoot32BitConstants OrigSetComputeRoot32BitConstants;
	static DX12CommandListHookSignatures::SetRoot32BitConstants OrigSetGraphicsRoot32BitConstants;
	static DX12CommandListHookSignatures::SetRootGpuVa OrigSetComputeRootConstantBufferView;
	static DX12CommandListHookSignatures::SetRootGpuVa OrigSetGraphicsRootConstantBufferView;
	static DX12CommandListHookSignatures::SetRootGpuVa OrigSetComputeRootShaderResourceView;
	static DX12CommandListHookSignatures::SetRootGpuVa OrigSetGraphicsRootShaderResourceView;
	static DX12CommandListHookSignatures::SetRootGpuVa OrigSetComputeRootUnorderedAccessView;
	static DX12CommandListHookSignatures::SetRootGpuVa OrigSetGraphicsRootUnorderedAccessView;
	static DX12CommandListHookSignatures::IaSetIndexBuffer OrigIASetIndexBuffer;
	static DX12CommandListHookSignatures::IaSetVertexBuffers OrigIASetVertexBuffers;
	static DX12CommandListHookSignatures::SoSetTargets OrigSOSetTargets;
	static DX12CommandListHookSignatures::OmSetRenderTargets OrigOMSetRenderTargets;
	static DX12CommandListHookSignatures::ClearDepthStencilView OrigClearDepthStencilView;
	static DX12CommandListHookSignatures::ClearRenderTargetView OrigClearRenderTargetView;
	static DX12CommandListHookSignatures::ClearUavUint OrigClearUnorderedAccessViewUint;
	static DX12CommandListHookSignatures::ClearUavFloat OrigClearUnorderedAccessViewFloat;
	static DX12CommandListHookSignatures::DiscardResource OrigDiscardResource;
	static DX12CommandListHookSignatures::BeginQuery OrigBeginQuery;
	static DX12CommandListHookSignatures::EndQuery OrigEndQuery;
	static DX12CommandListHookSignatures::ResolveQueryData OrigResolveQueryData;
	static DX12CommandListHookSignatures::SetPredication OrigSetPredication;
	static DX12CommandListHookSignatures::CommandListSetMarker OrigCommandListSetMarker;
	static DX12CommandListHookSignatures::CommandListBeginEvent OrigCommandListBeginEvent;
	static DX12CommandListHookSignatures::CommandListEndEvent OrigCommandListEndEvent;
	static DX12CommandListHookSignatures::ExecuteIndirect OrigExecuteIndirect;

	static SRWLOCK IaReplacementSummaryLock;
	static LONG IaReplacementSummaryPresent;
	static volatile LONG IaReplacementSummaryDraws;
	static volatile LONG IaReplacementSummaryIndexedDraws;
	static volatile LONG IaReplacementSummaryDispatches;

	static SRWLOCK HookedVTableLock;
	static std::unordered_set<void*> HookedCommandListVTables;
	static SRWLOCK RegisteredListLock;
	static std::unordered_set<void*> RegisteredCommandLists;
	static SRWLOCK KnownCommandQueueLock;
	static IUnknown *KnownCommandQueue;
	static void ReleaseKnownCommandQueue();

	static thread_local DX12ThreadLocalCommandListOriginal CommandListOriginals[64];
	static thread_local DX12ThreadLocalCommandListOriginal CommandQueueOriginals[32];
	static thread_local DX12VtableBaseCache VtableBaseCache;
	static thread_local DX12ThreadLocalIaCallbacksCache IaCallbacksCache;
};

// ---- DX12CommandListHookDiagnostics.hpp ----
#line 1 "DX12CommandListHookDiagnostics.hpp"
#include <Windows.h>
#include <d3d12.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>


class DX12CommandListHookDiagnostics
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
		DX12LogManager::DebugJsonFunc("DX12HookCall",
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
		DX12LogManager::DebugJsonFunc("DX12QueueStage",
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

	static void LogCommandListStage(
		const char *api, const char *stage, ID3D12GraphicsCommandList *commandList,
		HRESULT hr = S_OK, ULONGLONG elapsedMs = 0)
	{
#if defined(_DEBUG)
		if (!DX12LogManager::VerboseDiagnosticsEnabled())
			return;
		if (SUCCEEDED(hr) && elapsedMs < 4)
			return;
		DX12LogManager::DebugJsonFunc("DX12CommandListStage",
			"\"api\":\"%s\",\"stage\":\"%s\",\"present\":%ld,\"commandList\":\"%p\","
			"\"hr\":\"0x%lx\",\"elapsedMs\":%llu",
			api ? api : "", stage ? stage : "", DX12RuntimeState::GetPresentCount(), commandList,
			hr, static_cast<unsigned long long>(elapsedMs));
#else
		(void)api;
		(void)stage;
		(void)commandList;
		(void)hr;
		(void)elapsedMs;
#endif
	}

	static void LogCommandListStageEdge(
		const char *api, const char *stage, ID3D12GraphicsCommandList *commandList)
	{
		if (!DX12LogManager::VerboseDiagnosticsEnabled())
			return;
		DX12LogManager::DebugJsonFunc("DX12CommandListStage",
			"\"api\":\"%s\",\"stage\":\"%s\",\"present\":%ld,\"commandList\":\"%p\"",
			api ? api : "", stage ? stage : "", DX12RuntimeState::GetPresentCount(), commandList);
	}

	static void FlushIaReplacementExecuteSummaryIfNeeded()
	{
#if defined(_DEBUG)
		constexpr bool kLogIaReplacementExecuteSummary = false;
		if (!kLogIaReplacementExecuteSummary)
			return;
		const LONG present = DX12RuntimeState::GetPresentCount();
		if (DX12CommandListHookState::IaReplacementSummaryPresent == present)
			return;

		AcquireSRWLockExclusive(&DX12CommandListHookState::IaReplacementSummaryLock);
		if (DX12CommandListHookState::IaReplacementSummaryPresent != present) {
			const LONG previousPresent = DX12CommandListHookState::IaReplacementSummaryPresent;
			const LONG draws = InterlockedExchange(&DX12CommandListHookState::IaReplacementSummaryDraws, 0);
			const LONG indexedDraws = InterlockedExchange(&DX12CommandListHookState::IaReplacementSummaryIndexedDraws, 0);
			const LONG dispatches = InterlockedExchange(&DX12CommandListHookState::IaReplacementSummaryDispatches, 0);
			if (previousPresent >= 0 && (draws || dispatches)) {
				DX12LogManager::DebugJsonFunc("DX12IaReplacementExecuteSummary",
					"\"present\":%ld,\"draws\":%ld,\"indexedDraws\":%ld,\"dispatches\":%ld",
					previousPresent, draws, indexedDraws, dispatches);
			}
			DX12CommandListHookState::IaReplacementSummaryPresent = present;
		}
		ReleaseSRWLockExclusive(&DX12CommandListHookState::IaReplacementSummaryLock);
#endif
	}

	static void RecordIaReplacementDrawExecute(bool indexed)
	{
		DX12Profiling::RecordIaReplacementDraw(indexed);
#if defined(_DEBUG)
		FlushIaReplacementExecuteSummaryIfNeeded();
		InterlockedIncrement(&DX12CommandListHookState::IaReplacementSummaryDraws);
		if (indexed)
			InterlockedIncrement(&DX12CommandListHookState::IaReplacementSummaryIndexedDraws);
#endif
	}

	static void RecordIaReplacementDispatchExecute()
	{
		DX12Profiling::RecordIaReplacementDispatch();
#if defined(_DEBUG)
		FlushIaReplacementExecuteSummaryIfNeeded();
		InterlockedIncrement(&DX12CommandListHookState::IaReplacementSummaryDispatches);
#endif
	}
};

// ---- DX12OriginalFunctionRegistry.hpp ----
#line 1 "DX12OriginalFunctionRegistry.hpp"
#include <Windows.h>


class DX12OriginalFunctionRegistry
{
public:
	static void LogHookCall(const char *api, const void *object)
	{
		if (!DX12LogManager::ShouldLogHookCall(api))
			return;
		DX12LogManager::DebugJsonFunc("DX12HookCall",
			"\"api\":\"%s\",\"present\":%ld,\"this\":\"%p\"",
			api ? api : "", DX12RuntimeState::GetPresentCount(), object);
	}

	static void LogOriginalFallback(const char *api, const void *object, UINT slot, const void *fallback)
	{
		if (!DX12LogManager::ShouldLogHookCall(api))
			return;
		DX12LogManager::DebugJsonFunc("DX12FallbackPath",
			"\"kind\":\"original_lookup\",\"api\":\"%s\",\"present\":%ld,\"this\":\"%p\","
			"\"slot\":%u,\"fallback\":\"%p\"",
			api ? api : "", DX12RuntimeState::GetPresentCount(), object, slot, fallback);
	}

	static void *GetVTableSlotTarget(void *object, UINT slot)
	{
		if (!object)
			return nullptr;
		void **vtable = *reinterpret_cast<void***>(object);
		return vtable ? vtable[slot] : nullptr;
	}

	// 批量预解析：一次性解析所有热点 vtable slot，填充二级 vtable 缓存
	// 后续所有 ResolveCached 调用直接命中 vtable 缓存，跳过全局 map 查找
	template <typename SlotCache, typename VtableCache>
	static void BulkResolveVtableSlots(
		void *object,
		const UINT *slots,
		size_t slotCount,
		SlotCache *slotCaches,
		size_t slotCacheCount,
		VtableCache *vtableCache)
	{
		if (!object || !slots || !slotCount || !vtableCache)
			return;

		void **vtable = *reinterpret_cast<void***>(object);
		if (!vtable)
			return;

		// 同一 vtable 已批量解析过，跳过
		if (vtableCache->vtable == vtable)
			return;

		// 清空并设置新的 vtable
		vtableCache->vtable = vtable;
		vtableCache->originals = {};
		vtableCache->slotResolved = {};

		for (size_t i = 0; i < slotCount; ++i) {
			UINT slot = slots[i];
			if (slot >= vtableCache->originals.size())
				continue;

			void *target = vtable[slot];
			if (!target) {
				vtableCache->slotResolved[slot] = true;
				continue;
			}

			void *original = DX12HookRegistry::GetOriginalFunction(target);
			vtableCache->originals[slot] = original;
			vtableCache->slotResolved[slot] = true;

			// 同步更新一级 slot 缓存
			if (slotCaches && slot < slotCacheCount) {
				slotCaches[slot].target = target;
				slotCaches[slot].original = original;
				slotCaches[slot].resolved = true;
			}
		}
	}

	template <typename T>
	static T Resolve(void *object, UINT slot, T fallback, const char *name)
	{
		void *target = GetVTableSlotTarget(object, slot);
		if (target) {
			void *original = DX12HookRegistry::GetOriginalFunction(target);
			if (original)
				return reinterpret_cast<T>(original);
		}

		if (fallback) {
			LogOriginalFallback(name, object, slot, reinterpret_cast<const void*>(fallback));
			return fallback;
		}

		DX12LogManager::JsonFunc(name ? name : "DX12::UnknownOriginal",
			"\"event\":\"MissingOriginal\",\"this\":\"%p\",\"slot\":%u",
			object, slot);
		return nullptr;
	}

	template <typename T, typename SlotCache, typename VtableCache>
	static T ResolveCached(
		void *object, UINT slot, T fallback, const char *name,
		SlotCache *slotCaches, size_t slotCacheCount, VtableCache *vtableCache)
	{
		if (object) {
			void **vtable = *reinterpret_cast<void***>(object);
			if (vtable) {
				void *target = vtable[slot];

				// 一级缓存：per-slot TLS 缓存（命中率最高）
				if (slotCaches && slot < slotCacheCount &&
					slotCaches[slot].resolved &&
					slotCaches[slot].target == target) {
					if (slotCaches[slot].original)
						return reinterpret_cast<T>(slotCaches[slot].original);
					// 负缓存命中：已解析过，无 hook，直接用 fallback
					if (fallback)
						return fallback;
					goto log_missing;
				}

				// 二级缓存：per-vtable 批量缓存
				if (vtableCache && vtableCache->vtable == vtable &&
					slot < vtableCache->slotResolved.size() &&
					vtableCache->slotResolved[slot]) {
					void *original = vtableCache->originals[slot];
					// 更新一级缓存
					if (slotCaches && slot < slotCacheCount) {
						slotCaches[slot].target = target;
						slotCaches[slot].original = original;
						slotCaches[slot].resolved = true;
					}
					if (original)
						return reinterpret_cast<T>(original);
					// 负缓存命中
					if (fallback)
						return fallback;
					goto log_missing;
				}

				// 三级：全局 hook registry 查找（最慢，仅首次访问）
				void *original = DX12HookRegistry::GetOriginalFunction(target);

				// 更新一级缓存（包括负结果）
				if (slotCaches && slot < slotCacheCount) {
					slotCaches[slot].target = target;
					slotCaches[slot].original = original;
					slotCaches[slot].resolved = true;
				}

				// 更新二级 vtable 缓存（包括负结果）
				if (vtableCache && slot < vtableCache->slotResolved.size()) {
					if (vtableCache->vtable != vtable) {
						vtableCache->vtable = vtable;
						vtableCache->originals = {};
						vtableCache->slotResolved = {};
					}
					vtableCache->originals[slot] = original;
					vtableCache->slotResolved[slot] = true;
				}

				if (original)
					return reinterpret_cast<T>(original);

				// 负结果：slot 未被 hook，走 fallback
				if (fallback)
					return fallback;
				goto log_missing;
			}
		}

		if (fallback) {
			LogOriginalFallback(name, object, slot, reinterpret_cast<const void*>(fallback));
			return fallback;
		}

	log_missing:
		DX12LogManager::JsonFunc(name ? name : "DX12::UnknownOriginal",
			"\"event\":\"MissingOriginal\",\"this\":\"%p\",\"slot\":%u",
			object, slot);
		return nullptr;
	}
};

// ---- DX12CommandListHookRegistry.hpp ----
#line 1 "DX12CommandListHookRegistry.hpp"
#include <Windows.h>
#include <d3d12.h>


class DX12CommandListHookRegistry
{
public:
	static bool ShouldHookCommandListVTable(void *vtable)
	{
		if (!vtable)
			return false;

		AcquireSRWLockShared(&DX12CommandListHookState::HookedVTableLock);
		const bool alreadyHooked =
			DX12CommandListHookState::HookedCommandListVTables.find(vtable) !=
			DX12CommandListHookState::HookedCommandListVTables.end();
		ReleaseSRWLockShared(&DX12CommandListHookState::HookedVTableLock);
		if (alreadyHooked)
			return false;

		bool shouldHook = false;
		AcquireSRWLockExclusive(&DX12CommandListHookState::HookedVTableLock);
		shouldHook = DX12CommandListHookState::HookedCommandListVTables.insert(vtable).second;
		ReleaseSRWLockExclusive(&DX12CommandListHookState::HookedVTableLock);
		return shouldHook;
	}

	static bool ShouldRegisterCommandListPointer(void *commandList)
	{
		if (!commandList)
			return false;

		AcquireSRWLockShared(&DX12CommandListHookState::RegisteredListLock);
		const bool alreadyRegistered =
			DX12CommandListHookState::RegisteredCommandLists.find(commandList) !=
			DX12CommandListHookState::RegisteredCommandLists.end();
		ReleaseSRWLockShared(&DX12CommandListHookState::RegisteredListLock);
		if (alreadyRegistered)
			return false;

		bool shouldRegister = false;
		AcquireSRWLockExclusive(&DX12CommandListHookState::RegisteredListLock);
		shouldRegister = DX12CommandListHookState::RegisteredCommandLists.insert(commandList).second;
		ReleaseSRWLockExclusive(&DX12CommandListHookState::RegisteredListLock);
		return shouldRegister;
	}

	static void ForgetRegisteredCommandListPointer(void *commandList)
	{
		if (!commandList)
			return;

		AcquireSRWLockExclusive(&DX12CommandListHookState::RegisteredListLock);
		DX12CommandListHookState::RegisteredCommandLists.erase(commandList);
		ReleaseSRWLockExclusive(&DX12CommandListHookState::RegisteredListLock);
	}

	template <typename T>
	static T GetCommandQueueOriginal(ID3D12CommandQueue *queue, UINT slot, T fallback, const char *name)
	{
		return DX12OriginalFunctionRegistry::ResolveCached(
			queue, slot, fallback, name,
			DX12CommandListHookState::CommandQueueOriginals,
			ARRAYSIZE(DX12CommandListHookState::CommandQueueOriginals),
			&DX12CommandListHookState::VtableBaseCache);
	}

	template <typename T>
	static T GetCommandListOriginal(
		ID3D12GraphicsCommandList *commandList, UINT slot, T fallback, const char *name)
	{
		return DX12OriginalFunctionRegistry::ResolveCached(
			commandList, slot, fallback, name,
			DX12CommandListHookState::CommandListOriginals,
			ARRAYSIZE(DX12CommandListHookState::CommandListOriginals),
			&DX12CommandListHookState::VtableBaseCache);
	}
};

// ---- DX12CommandListHooks.hpp ----
#line 1 "DX12CommandListHooks.hpp"
#include <Windows.h>

class DX12CommandListHooks
{
public:
	static void HookCommandListCreation(IUnknown *device);
	static void HookCommandList(IUnknown *commandList);
	static void HookCommandQueue(IUnknown *commandQueue);
	static void RememberCommandQueueForHotPath(IUnknown *commandQueue);
	static void ActivateCommandQueueHotPathHooks();
};

// ---- DX12CommandQueueExecutionState.hpp ----
#line 1 "DX12CommandQueueExecutionState.hpp"
#include <Windows.h>

class DX12CommandQueueExecutionState
{
public:
	inline static volatile LONG FirstExecuteLog = 0;
};

// ---- DX12CommandQueueExecutionFlow.hpp ----
#line 1 "DX12CommandQueueExecutionFlow.hpp"
#include <Windows.h>
#include <d3d12.h>


class DX12CommandQueueExecutionFlow
{
public:
	class DX12ExecuteSkipLogUtils
	{
	public:
		inline static volatile LONG64 SkipCalls = 0;
		inline static volatile LONG64 SkipCommandLists = 0;
		inline static volatile LONG LastPresentLogged = -1;
		static constexpr LONG PresentInterval = 120;

		static void Record(ID3D12CommandQueue *queue, UINT numCommandLists, const char *stage)
		{
			InterlockedIncrement64(&SkipCalls);
			InterlockedAdd64(&SkipCommandLists, static_cast<LONG64>(numCommandLists));
			const LONG present = DX12RuntimeState::GetPresentCount();
			const LONG last = InterlockedCompareExchange(&LastPresentLogged, 0, 0);
			const bool shouldLog = last < 0 || present >= last + PresentInterval;
			if (!shouldLog)
				return;
			if (InterlockedCompareExchange(&LastPresentLogged, present, last) != last)
				return;
			DX12LogManager::JsonFunc("DX12ExecuteCommandListsHeartbeat",
				"\"stage\":\"%s\",\"mode\":\"aggregate_skip\",\"present\":%ld,\"threadId\":%lu,\"queue\":\"%p\",\"calls\":%lld,\"commandLists\":%lld",
				stage ? stage : "",
				present,
				GetCurrentThreadId(),
				queue,
				static_cast<long long>(InterlockedCompareExchange64(&SkipCalls, 0, 0)),
				static_cast<long long>(InterlockedCompareExchange64(&SkipCommandLists, 0, 0)));
		}
	};

	using RegisterSubmittedCommandListFn = void (*)(IUnknown*);

	static void RegisterSubmittedCommandLists(
		UINT numCommandLists,
		ID3D12CommandList *const *commandLists,
		RegisterSubmittedCommandListFn registerSubmittedCommandList)
	{
		if (!registerSubmittedCommandList || !commandLists)
			return;
		for (UINT i = 0; i < numCommandLists; ++i)
			registerSubmittedCommandList(commandLists[i]);
	}

	static void Execute(
		ID3D12CommandQueue *queue,
		UINT numCommandLists,
		ID3D12CommandList *const *commandLists,
		DX12CommandListHookSignatures::QueueExecuteCommandLists original,
		RegisterSubmittedCommandListFn registerSubmittedCommandList)
	{
		LARGE_INTEGER totalBegin = DX12OuterTimingProbeUtils::Now();
		volatile LONG &firstExecuteLog = DX12CommandQueueExecutionState::FirstExecuteLog;
		if (InterlockedCompareExchange(&firstExecuteLog, 1, 0) == 0)
			DX12LogManager::JsonFuncFlush("DX12ExecuteCommandListsFirst",
				"\"stage\":\"enter\",\"queue\":\"%p\",\"count\":%u,\"skipAll\":%ld,\"internalReplay\":%s",
				queue, numCommandLists, DX12HotPathState::skipAll,
				DX12InternalReplayState::IsActive() ? "true" : "false");

		// 每帧刷新 flags word：确保 draw/dispatch 热路径使用最新状态
		LARGE_INTEGER sliceBegin = DX12OuterTimingProbeUtils::Now();
		DX12HotPathState::RebuildHookWorkFlags();
		DX12OuterTimingProbeUtils::LogQueueExecuteSlice(
			"rebuild_work_flags",
			queue,
			numCommandLists,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));

		if (DX12HotPathState::skipAll) {
			if (original) {
				DX12ExecuteSkipLogUtils::Record(
					queue, numCommandLists, "BeforeOriginalSkip");
				if (InterlockedCompareExchange(&firstExecuteLog, 2, 1) == 1)
					DX12LogManager::JsonFuncFlush("DX12ExecuteCommandListsFirst",
						"\"stage\":\"before_original_skip\",\"queue\":\"%p\",\"count\":%u",
						queue, numCommandLists);
				sliceBegin = DX12OuterTimingProbeUtils::Now();
				original(queue, numCommandLists, commandLists);
				DX12OuterTimingProbeUtils::LogQueueExecuteSlice(
					"original_skip",
					queue,
					numCommandLists,
					DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
				DX12ExecuteSkipLogUtils::Record(
					queue, numCommandLists, "AfterOriginalSkip");
				if (InterlockedCompareExchange(&firstExecuteLog, 3, 2) == 2)
					DX12LogManager::JsonFuncFlush("DX12ExecuteCommandListsFirst",
						"\"stage\":\"after_original_skip\",\"queue\":\"%p\",\"count\":%u",
						queue, numCommandLists);
			}
			const bool shouldRegisterSubmitted =
				DX12HuntIsEnabled() || DX12CaptureLedger::CurrentCaptureSessionId() != 0;
			if (shouldRegisterSubmitted) {
				sliceBegin = DX12OuterTimingProbeUtils::Now();
				RegisterSubmittedCommandLists(numCommandLists, commandLists, registerSubmittedCommandList);
				DX12OuterTimingProbeUtils::LogQueueExecuteSlice(
					"register_submitted_skip",
					queue,
					numCommandLists,
					DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
			}
			DX12ExecuteSkipLogUtils::Record(queue, numCommandLists, "ReturnSkip");
			DX12OuterTimingProbeUtils::LogQueueExecuteSlice(
				"total_skip",
				queue,
				numCommandLists,
				DX12OuterTimingProbeUtils::ElapsedUs(totalBegin));
			return;
		}

		if (DX12InternalReplayState::IsActive()) {
			if (original) {
				sliceBegin = DX12OuterTimingProbeUtils::Now();
				original(queue, numCommandLists, commandLists);
				DX12OuterTimingProbeUtils::LogQueueExecuteSlice(
					"original_internal_replay",
					queue,
					numCommandLists,
					DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
			}
			DX12OuterTimingProbeUtils::LogQueueExecuteSlice(
				"total_internal_replay",
				queue,
				numCommandLists,
				DX12OuterTimingProbeUtils::ElapsedUs(totalBegin));
			return;
		}

		DX12_PROFILE_SCOPE(ExecuteCommandLists);
		DX12CommandListFlowDiagnostics::LogDX12Call(
			"ID3D12CommandQueue::ExecuteCommandLists", queue, " count=%u", numCommandLists);
		if (!original) {
			DX12OuterTimingProbeUtils::LogQueueExecuteSlice(
				"total_no_original",
				queue,
				numCommandLists,
				DX12OuterTimingProbeUtils::ElapsedUs(totalBegin));
			return;
		}

		if (InterlockedCompareExchange(&firstExecuteLog, 4, 1) == 1)
			DX12LogManager::JsonFuncFlush("DX12ExecuteCommandListsFirst",
				"\"stage\":\"before_original_full\",\"queue\":\"%p\",\"count\":%u",
				queue, numCommandLists);
#if defined(_DEBUG)
		DX12CommandListFlowDiagnostics::LogQueueStage(
			"ExecuteCommandLists", "beforeOriginal", queue, numCommandLists);
		ULONGLONG startTick = GetTickCount64();
#endif
		sliceBegin = DX12OuterTimingProbeUtils::Now();
		original(queue, numCommandLists, commandLists);
		DX12OuterTimingProbeUtils::LogQueueExecuteSlice(
			"original",
			queue,
			numCommandLists,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
		if (InterlockedCompareExchange(&firstExecuteLog, 5, 4) == 4)
			DX12LogManager::JsonFuncFlush("DX12ExecuteCommandListsFirst",
				"\"stage\":\"after_original_full\",\"queue\":\"%p\",\"count\":%u",
				queue, numCommandLists);
#if defined(_DEBUG)
		ULONGLONG originalEndTick = GetTickCount64();
		DX12CommandListFlowDiagnostics::LogQueueStage(
			"ExecuteCommandLists", "afterOriginal", queue, numCommandLists,
			nullptr, 0, S_OK, originalEndTick - startTick);
#endif
		DX12_HOTPATH_TIME_BEGIN(ExecuteCLNotify);
		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DX12ModNotifyCommandListsSubmitted(queue, numCommandLists, commandLists);
		DX12OuterTimingProbeUtils::LogQueueExecuteSlice(
			"mod_notify_submitted",
			queue,
			numCommandLists,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
		DX12_HOTPATH_TIME_END(ExecuteCLNotify);
		sliceBegin = DX12OuterTimingProbeUtils::Now();
		RegisterSubmittedCommandLists(numCommandLists, commandLists, registerSubmittedCommandList);
		DX12OuterTimingProbeUtils::LogQueueExecuteSlice(
			"register_submitted",
			queue,
			numCommandLists,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
#if defined(_DEBUG)
		DX12CommandListFlowDiagnostics::LogQueueStage(
			"ExecuteCommandLists", "afterNotify", queue, numCommandLists,
			nullptr, 0, S_OK, GetTickCount64() - originalEndTick);
#endif
		DX12OuterTimingProbeUtils::LogQueueExecuteSlice(
			"total",
			queue,
			numCommandLists,
			DX12OuterTimingProbeUtils::ElapsedUs(totalBegin));
	}
};

// ---- DX12CommandQueueState.hpp ----
#line 1 "DX12CommandQueueState.hpp"
#include <Windows.h>
#include <d3d12.h>


class DX12CommandQueueState
{
public:
	SRWLOCK lock;
	ID3D12CommandQueue *queue = nullptr;
	volatile LONG nonDirectLogs = 0;

	DX12CommandQueueState()
	{
		InitializeSRWLock(&lock);
	}

	static DX12CommandQueueState &Get()
	{
		static DX12CommandQueueState state;
		return state;
	}

	static void Set(ID3D12CommandQueue *queue)
	{
		DX12CommandQueueState &state = Get();
		if (queue) {
			D3D12_COMMAND_QUEUE_DESC desc = queue->GetDesc();
			if (desc.Type != D3D12_COMMAND_LIST_TYPE_DIRECT) {
				if (InterlockedIncrement(&state.nonDirectLogs) <= 8)
					DX12LogManager::JsonFunc("DX12CommandQueueState",
						"\"event\":\"ignore_non_direct\",\"queue\":\"%p\",\"type\":%d",
						queue, static_cast<int>(desc.Type));
				return;
			}
			DX12LogManager::JsonFunc("DX12CommandQueueState",
				"\"event\":\"accept_direct\",\"queue\":\"%p\",\"type\":%d",
				queue, static_cast<int>(desc.Type));
		}

		if (queue)
			queue->AddRef();

		AcquireSRWLockExclusive(&state.lock);
		ID3D12CommandQueue *oldQueue = state.queue;
		state.queue = queue;
		ReleaseSRWLockExclusive(&state.lock);

		if (oldQueue)
			oldQueue->Release();
	}

	static ID3D12CommandQueue *Acquire()
	{
		DX12CommandQueueState &state = Get();
		AcquireSRWLockShared(&state.lock);
		ID3D12CommandQueue *queue = state.queue;
		if (queue)
			queue->AddRef();
		ReleaseSRWLockShared(&state.lock);
		return queue;
	}
};

// ---- DX12DescriptorArenaUtils.hpp ----
#line 1 "DX12DescriptorArenaUtils.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

class DX12DescriptorArenaUtils
{
public:
	template <class ComObject>
	static bool AppendUniqueOwned(
		std::vector<ComObject*> *objects,
		ComObject *object)
	{
		if (!objects || !object)
			return false;
		if (std::find(objects->begin(), objects->end(), object) != objects->end()) {
			object->Release();
			return false;
		}
		objects->push_back(object);
		return true;
	}

	template <class ComObject>
	static void MoveUniqueOwned(
		std::vector<ComObject*> *source,
		std::vector<ComObject*> *target)
	{
		if (!source || !target)
			return;
		for (ComObject *object : *source)
			AppendUniqueOwned(target, object);
		source->clear();
	}

	static uint32_t NextCapacity(
		uint32_t currentCapacity,
		uint32_t currentOffset,
		uint32_t requiredDescriptors,
		uint32_t initialCapacity,
		uint32_t maximumCapacity)
	{
		if (!requiredDescriptors || requiredDescriptors > maximumCapacity)
			return 0;
		const uint64_t highWater =
			static_cast<uint64_t>(currentOffset) + requiredDescriptors;
		const uint64_t doubled = currentCapacity ?
			static_cast<uint64_t>(currentCapacity) * 2 : initialCapacity;
		uint64_t capacity = (std::max)(
			static_cast<uint64_t>(initialCapacity),
			(std::max)(highWater, doubled));
		capacity = (std::max)(capacity,
			static_cast<uint64_t>(requiredDescriptors));
		capacity = (std::min)(capacity,
			static_cast<uint64_t>(maximumCapacity));
		return static_cast<uint32_t>(capacity);
	}
};

// ---- DX12DescriptorCopyMemoUtils.hpp ----
#line 1 "DX12DescriptorCopyMemoUtils.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

// Thread-local proof cache for the overwhelmingly common one-slot
// CopyDescriptors path. A hit means neither the source nor destination shard
// changed since a previous completed copy of the same slot pair. It therefore
// proves that repeating the shadow-state copy cannot change Descriptor State.
//
// The cache stores only stable heap-record identities and atomic version
// values. It never borrows a Descriptor Value or sparse-page pointer, so heap
// destruction and Descriptor Value pool reuse cannot create an ABA/UAF path.
class DX12DescriptorCopyMemo
{
public:
	enum class Result : uint8_t
	{
		SourceMissingDestinationEmpty,
		Unchanged
	};

	struct Key
	{
		const void *sourceHeap = nullptr;
		const void *destinationHeap = nullptr;
		uint32_t sourceSlot = 0;
		uint32_t destinationSlot = 0;

		bool operator==(const Key &other) const
		{
			return sourceHeap == other.sourceHeap &&
				destinationHeap == other.destinationHeap &&
				sourceSlot == other.sourceSlot &&
				destinationSlot == other.destinationSlot;
		}
	};

	__forceinline bool TryLoad(
		const Key &key,
		uint64_t sourceVersion,
		uint64_t destinationVersion,
		Result *result) const
	{
		if (!sets_ || !result)
			return false;
		const Set &set = sets_[SetIndex(key)];
		for (const Entry &entry : set.entries) {
			if (!entry.occupied || !(entry.key == key) ||
				entry.sourceVersion != sourceVersion ||
				entry.destinationVersion != destinationVersion)
				continue;
			*result = entry.result;
			return true;
		}
		return false;
	}

	__forceinline void Store(
		const Key &key,
		uint64_t sourceVersion,
		uint64_t destinationVersion,
		Result result)
	{
		EnsureStorage();
		Set &set = sets_[SetIndex(key)];
		Entry *target = nullptr;
		for (Entry &entry : set.entries) {
			if (entry.occupied && entry.key == key) {
				target = &entry;
				break;
			}
			if (!entry.occupied && !target)
				target = &entry;
		}
		if (!target) {
			target = &set.entries[set.nextVictim];
			set.nextVictim = static_cast<uint8_t>(
				(set.nextVictim + 1) % Ways);
		}
		target->key = key;
		target->sourceVersion = sourceVersion;
		target->destinationVersion = destinationVersion;
		target->result = result;
		target->occupied = true;
	}

	void Clear()
	{
		sets_.reset();
	}

private:
	// The captured game presents roughly 65k distinct single-slot copy pairs per
	// frame. Two ways still conflict-evicted about 10% of a frame-sized trace,
	// turning proven no-op copies back into locking/sparse-store traffic on the
	// next frame. Four ways retain the working set while keeping lookup bounded.
	static constexpr size_t SetCount = 65536;
	static constexpr size_t Ways = 4;
	static_assert((SetCount & (SetCount - 1)) == 0,
		"copy memo set count must be a power of two");

	struct Entry
	{
		Key key;
		uint64_t sourceVersion = 0;
		uint64_t destinationVersion = 0;
		Result result = Result::SourceMissingDestinationEmpty;
		bool occupied = false;
	};

	struct Set
	{
		std::array<Entry, Ways> entries;
		uint8_t nextVictim = 0;
	};

	static __forceinline uint64_t Mix(uint64_t value)
	{
		value ^= value >> 33;
		value *= 0xff51afd7ed558ccdull;
		value ^= value >> 33;
		value *= 0xc4ceb9fe1a85ec53ull;
		value ^= value >> 33;
		return value;
	}

	static __forceinline size_t SetIndex(const Key &key)
	{
		uint64_t hash = Mix(static_cast<uint64_t>(
			reinterpret_cast<uintptr_t>(key.sourceHeap)));
		hash ^= Mix(static_cast<uint64_t>(
			reinterpret_cast<uintptr_t>(key.destinationHeap)) +
			0x9e3779b97f4a7c15ull);
		hash ^= Mix((static_cast<uint64_t>(key.sourceSlot) << 32) |
			key.destinationSlot);
		return static_cast<size_t>(hash) & (SetCount - 1);
	}

	void EnsureStorage()
	{
		if (!sets_)
			sets_ = std::make_unique<Set[]>(SetCount);
	}

	std::unique_ptr<Set[]> sets_;
};

// ---- DX12FrameAnalysis.hpp ----
#line 1 "DX12FrameAnalysis.hpp"
#include <Windows.h>
#include <Shlwapi.h>
#include <share.h>
#include <stdarg.h>


class DX12FrameAnalysis
{
public:
	static bool Begin()
	{
		return DX12FrameAnalysisState::Begin();
	}

	static void End()
	{
		DX12FrameAnalysisState::End();
	}

	static void RequestCapture()
	{
		DX12FrameAnalysisState::RequestCapture();
		DX12HotPathState::Update();
	}

	static void BeginCapture()
	{
		DX12FrameAnalysisState::BeginCapture();
		DX12HotPathState::Update();
	}

	static bool EndCapture()
	{
		return DX12FrameAnalysisState::EndCapture();
	}

	static bool IsActive()
	{
		return DX12FrameAnalysisState::IsActive();
	}

	static bool IsCaptureRequested()
	{
		return DX12FrameAnalysisState::IsCaptureRequested();
	}

	static bool IsCapturing()
	{
		return DX12FrameAnalysisState::IsCapturing();
	}

	static bool GetPath(wchar_t *path, size_t pathCount)
	{
		return DX12FrameAnalysisState::GetPath(path, pathCount);
	}

	static void LogEvent(const char *fmt, ...)
	{
		va_list args;
		char buffer[1024];
		char fields[4096];

		if (!IsActive())
			return;

		va_start(args, fmt);
		vsnprintf(buffer, sizeof(buffer), fmt, args);
		va_end(args);

		strcpy_s(fields, sizeof(fields), "\"func\":\"Event\"");
		DX12JsonUtils::AppendLogFieldsFromText(fields, sizeof(fields), buffer);
		LogJsonFields(fields);
	}

	static void LogInfo(const char *fmt, ...)
	{
		va_list args;
		char buffer[1024];
		char fields[4096];

		if (!IsActive())
			return;

		va_start(args, fmt);
		vsnprintf(buffer, sizeof(buffer), fmt, args);
		va_end(args);

		strcpy_s(fields, sizeof(fields), "\"func\":\"LogInfo\"");
		DX12JsonUtils::AppendLogFieldsFromText(fields, sizeof(fields), buffer);
		LogJsonFields(fields);
	}

	static void LogJsonFunc(const char *func, const char *fmt, ...)
	{
		if (!IsActive())
			return;

		char funcJson[512];
		char extra[4096];
		char fields[4608];
		DX12JsonUtils::EscapeString(funcJson, sizeof(funcJson), func ? func : "Unknown");
		extra[0] = '\0';

		if (fmt && fmt[0]) {
			va_list args;
			va_start(args, fmt);
			vsnprintf(extra, sizeof(extra), fmt, args);
			va_end(args);
		}

		if (extra[0])
			sprintf_s(fields, sizeof(fields), "\"func\":%s,%s", funcJson, extra);
		else
			sprintf_s(fields, sizeof(fields), "\"func\":%s", funcJson);
		LogJsonFields(fields);
	}

	static void LogJsonFields(const char *fields)
	{
		DX12FrameAnalysisState::LogJsonFields(fields);
	}
};

// ---- DX12ResourceStateTracker.hpp ----
#line 1 "DX12ResourceStateTracker.hpp"
#include <d3d12.h>


class DX12ResourceStateTracker
{
public:
	// Microsoft documents ResourceBarrier as the explicit D3D12 transition
	// mechanism. These inference helpers are only used when no observed state
	// is available from creation or barrier tracking.
	static D3D12_RESOURCE_STATES FromBarrierLayout(D3D12_BARRIER_LAYOUT layout)
	{
		switch (layout) {
		case D3D12_BARRIER_LAYOUT_COMMON:
			return D3D12_RESOURCE_STATE_COMMON;
		case D3D12_BARRIER_LAYOUT_GENERIC_READ:
			return D3D12_RESOURCE_STATE_GENERIC_READ;
		case D3D12_BARRIER_LAYOUT_RENDER_TARGET:
			return D3D12_RESOURCE_STATE_RENDER_TARGET;
		case D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS:
			return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		case D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE:
			return D3D12_RESOURCE_STATE_DEPTH_WRITE;
		case D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_READ:
			return D3D12_RESOURCE_STATE_DEPTH_READ;
		case D3D12_BARRIER_LAYOUT_SHADER_RESOURCE:
			return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
		case D3D12_BARRIER_LAYOUT_COPY_SOURCE:
			return D3D12_RESOURCE_STATE_COPY_SOURCE;
		case D3D12_BARRIER_LAYOUT_COPY_DEST:
			return D3D12_RESOURCE_STATE_COPY_DEST;
		case D3D12_BARRIER_LAYOUT_RESOLVE_SOURCE:
			return D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
		case D3D12_BARRIER_LAYOUT_RESOLVE_DEST:
			return D3D12_RESOURCE_STATE_RESOLVE_DEST;
		default:
			return D3D12_RESOURCE_STATE_COMMON;
		}
	}

	static D3D12_RESOURCE_STATES InferDescriptorSourceState(const DX12FrameResourceBinding &binding)
	{
		if (binding.descriptor.kind == DescriptorKind::CBV)
			return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
		if (binding.descriptor.kind == DescriptorKind::UAV)
			return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		if (binding.descriptor.kind == DescriptorKind::RTV)
			return D3D12_RESOURCE_STATE_RENDER_TARGET;
		if (binding.descriptor.kind == DescriptorKind::DSV)
			return D3D12_RESOURCE_STATE_DEPTH_WRITE;
		return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	}

	static D3D12_RESOURCE_STATES InferIaBufferSourceState(const DX12FrameIaBufferBinding &buffer)
	{
		if (buffer.role == IaBufferRole::IB)
			return D3D12_RESOURCE_STATE_INDEX_BUFFER;
		return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
	}

	static bool HeapTypeAllowsDirectCopy(UINT heapType, bool hasHeapType)
	{
		return hasHeapType &&
			(heapType == D3D12_HEAP_TYPE_UPLOAD || heapType == D3D12_HEAP_TYPE_READBACK);
	}
};

// ---- DX12ResourceTrackerRuntimeTypes.hpp ----
#line 1 "DX12ResourceTrackerRuntimeTypes.hpp"
#include <Windows.h>

struct PlacedResourceInfo
{
	bool hasPlacedHeap = false;
	UINT64 heapOffset = 0;
	UINT64 heapSize = 0;
};

// ---- DX12ResourceTrackingPolicyUtils.hpp ----
#line 1 "DX12ResourceTrackingPolicyUtils.hpp"
bool DX12ModHasActiveShaderOverrides();
bool DX12ModNeedsPresentReplacement();
bool DX12ModNeedsPreSkinningUavProbe();
bool DX12ModNeedsShaderDescriptorTracking();
bool DX12ShaderDumpIsCaptureRequested();
bool DX12ShaderDumpIsCapturingFrame();
bool DX12ShaderDumpIsBusy();

class DX12ResourceTrackingPolicyUtils
{
public:
	static bool NeedsFrameDumpTracking()
	{
		return DX12FrameAnalysisState::IsActive() ||
			DX12FrameAnalysisState::IsCaptureRequested() ||
			DX12FrameAnalysisState::IsCapturing();
	}

	static bool NeedsShaderDumpTracking()
	{
		return DX12ShaderDumpIsCaptureRequested() ||
			DX12ShaderDumpIsCapturingFrame() ||
			DX12ShaderDumpIsBusy();
	}

	static bool NeedsDumpOrHuntingTracking()
	{
		return DX12RuntimeState::IsHuntingMode() ||
			NeedsFrameDumpTracking() ||
			NeedsShaderDumpTracking();
	}

	static bool NeedsDescriptorTracking()
	{
		return NeedsDumpOrHuntingTracking() ||
			DX12ModNeedsPreSkinningUavProbe() ||
			DX12ModNeedsShaderDescriptorTracking();
	}

	static bool NeedsFullDescriptorHistory()
	{
		return NeedsFrameDumpTracking();
	}

	static bool NeedsFullResourceMetadata()
	{
		return NeedsDumpOrHuntingTracking();
	}

	// Runtime recording (gRuntimeDescriptorByCpuHandle) is a fast cache for
	// draw-time descriptor queries.  It is only required when capture/dump/hunt
	// is actively inspecting every descriptor.  During normal Mod operation the
	// Worker pipeline + EnsureDescriptorHandlesVisible provides equivalent data
	// via gDescriptorByCpuHandle / gDescriptorHeaps.
	static bool NeedsRuntimeDescriptorRecording()
	{
		return NeedsFrameDumpTracking() ||
			NeedsShaderDumpTracking() ||
			DX12RuntimeState::IsHuntingMode() ||
			NeedsFullDescriptorHistory();
	}
};

// ---- DX12DescriptorHeapShardState.hpp ----
#line 1 "DX12DescriptorHeapShardState.hpp"
#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

// Lock and version storage for one descriptor heap. Each shard occupies its
// own cache line: packing SRWLOCKs or counters together would make unrelated
// descriptor slots contend through false sharing even though their logical
// locks are independent.
template <size_t ShardCountValue>
class DX12DescriptorHeapShardState
{
public:
	static constexpr size_t ShardCount = ShardCountValue;
	static constexpr size_t CacheLineBytes = 64;

	static_assert(ShardCount != 0 && (ShardCount & (ShardCount - 1)) == 0,
		"descriptor shard count must be a power of two");

	struct alignas(CacheLineBytes) LockCell
	{
		SRWLOCK value = SRWLOCK_INIT;
	};

	static_assert(sizeof(LockCell) % CacheLineBytes == 0,
		"descriptor locks must not share cache lines");

	explicit DX12DescriptorHeapShardState(uint32_t maxSlots = 0)
		: maxSlots_(maxSlots),
		  versionPageCount_(maxSlots == 0 ? 0 : static_cast<uint32_t>(
			(static_cast<uint64_t>(maxSlots) + VersionPageSize - 1) /
			VersionPageSize))
	{
		for (LockCell &cell : locks_)
			InitializeSRWLock(&cell.value);
		if (versionPageCount_ == 0)
			return;
		versionPages_ =
			std::make_unique<std::atomic<VersionPage*>[]>(versionPageCount_);
		for (uint32_t index = 0; index < versionPageCount_; ++index)
			versionPages_[index].store(nullptr, std::memory_order_relaxed);
	}

	~DX12DescriptorHeapShardState()
	{
		if (!versionPages_)
			return;
		for (uint32_t index = 0; index < versionPageCount_; ++index)
			delete versionPages_[index].load(std::memory_order_relaxed);
	}

	DX12DescriptorHeapShardState(const DX12DescriptorHeapShardState&) = delete;
	DX12DescriptorHeapShardState &operator=(
		const DX12DescriptorHeapShardState&) = delete;

	static constexpr size_t ShardForSlot(uint32_t slot)
	{
		return static_cast<size_t>(slot) & (ShardCount - 1);
	}

	SRWLOCK *LockForSlot(uint32_t slot)
	{
		return &locks_[ShardForSlot(slot)].value;
	}

	void LockAllExclusive()
	{
		for (LockCell &cell : locks_)
			AcquireSRWLockExclusive(&cell.value);
	}

	void UnlockAllExclusive()
	{
		for (size_t i = ShardCount; i > 0; --i)
			ReleaseSRWLockExclusive(&locks_[i - 1].value);
	}

	bool IsActive() const
	{
		return active_.load(std::memory_order_acquire);
	}

	void Deactivate()
	{
		active_.store(false, std::memory_order_release);
	}

	const SRWLOCK *LockForSlot(uint32_t slot) const
	{
		return &locks_[ShardForSlot(slot)].value;
	}

	void ResetVersions()
	{
		version_.store(1, std::memory_order_release);
		if (!versionPages_)
			return;
		for (uint32_t pageIndex = 0;
				pageIndex < versionPageCount_; ++pageIndex) {
			VersionPage *page = versionPages_[pageIndex].load(
				std::memory_order_acquire);
			if (!page)
				continue;
			for (std::atomic<uint64_t> &slotVersion : page->versions)
				slotVersion.store(2, std::memory_order_release);
			for (std::atomic<uint64_t> &valueId : page->valueIds)
				valueId.store(0, std::memory_order_release);
		}
	}

	void BumpVersion(uint32_t slot)
	{
		PublishSlotValue(slot, 0);
	}

	void PublishSlotValue(uint32_t slot, uint64_t valueId)
	{
		PublishSlotIdentity(slot, valueId);
		BumpHeapVersion();
	}

	// Range copies publish every slot identity, but consumers only require the
	// heap-wide invalidation token to change once for the completed API call.
	// Splitting these operations removes one contended atomic RMW per copied
	// descriptor without weakening per-slot identity validation.
	void PublishSlotIdentity(uint32_t slot, uint64_t valueId)
	{
		VersionPage *page = EnsureVersionPage(slot);
		if (page) {
			std::atomic<uint64_t> &slotVersion =
				page->versions[slot & (VersionPageSize - 1)];
			// Odd means a publication is in progress. Readers only accept the
			// value when the same even version brackets the Value ID load. Slot
			// writers are serialized by the owning shard lock, so the second
			// contended atomic RMW formerly used here bought no correctness. An
			// exchange starts publication and the release store completes it.
			const uint64_t previous = slotVersion.fetch_or(
				1ull, std::memory_order_acq_rel);
			page->valueIds[slot & (VersionPageSize - 1)].store(
				valueId, std::memory_order_relaxed);
			// Versions are initialized even and only one writer owns this slot.
			// Advancing from the previous even generation preserves cache-key
			// invalidation, including wraparound's even parity.
			slotVersion.store((previous + 2ull) & ~1ull,
				std::memory_order_release);
		}
	}

	void BumpHeapVersion()
	{
		version_.fetch_add(1, std::memory_order_release);
	}

	struct SlotIdentity
	{
		uint64_t version = 0;
		uint64_t valueId = 0;
	};

	bool ObserveSlotIdentity(uint32_t slot, SlotIdentity *identity) const
	{
		if (!identity || !versionPages_ || slot >= maxSlots_)
			return false;
		VersionPage *page = versionPages_[slot / VersionPageSize].load(
			std::memory_order_acquire);
		if (!page) {
			identity->version = 2;
			identity->valueId = 0;
			return true;
		}
		const uint32_t offset = slot & (VersionPageSize - 1);
		const uint64_t before = page->versions[offset].load(
			std::memory_order_acquire);
		if ((before & 1) != 0)
			return false;
		const uint64_t valueId = page->valueIds[offset].load(
			std::memory_order_acquire);
		const uint64_t after = page->versions[offset].load(
			std::memory_order_acquire);
		if (before != after || (after & 1) != 0)
			return false;
		identity->version = after;
		identity->valueId = valueId;
		return true;
	}

	// Creation hooks frequently publish the same compact descriptor value to the
	// same slot thousands of times per frame.  Reject those writes from the
	// seqlock publication itself before taking the destination shard exclusively
	// or touching the sparse descriptor store.
	bool PublishedSlotValueEquals(uint32_t slot, uint64_t valueId) const
	{
		if (valueId == 0)
			return false;
		SlotIdentity identity;
		return ObserveSlotIdentity(slot, &identity) &&
			identity.valueId == valueId;
	}

	uint64_t ReadSlotVersion(uint32_t slot) const
	{
		if (!versionPages_ || slot >= maxSlots_)
			return version_.load(std::memory_order_acquire);
		VersionPage *page = versionPages_[slot / VersionPageSize].load(
			std::memory_order_acquire);
		return page ?
			page->versions[slot & (VersionPageSize - 1)].load(
				std::memory_order_acquire) : 2;
	}

	uint64_t ReadVersion() const
	{
		return version_.load(std::memory_order_acquire);
	}

private:
	static constexpr uint32_t VersionPageSize = 256;
	static_assert((VersionPageSize & (VersionPageSize - 1)) == 0,
		"descriptor version page size must be a power of two");

	struct VersionPage
	{
		std::array<std::atomic<uint64_t>, VersionPageSize> versions;
		std::array<std::atomic<uint64_t>, VersionPageSize> valueIds;

		VersionPage()
		{
			for (std::atomic<uint64_t> &version : versions)
				version.store(2, std::memory_order_relaxed);
			for (std::atomic<uint64_t> &valueId : valueIds)
				valueId.store(0, std::memory_order_relaxed);
		}
	};

	VersionPage *EnsureVersionPage(uint32_t slot)
	{
		if (!versionPages_ || slot >= maxSlots_)
			return nullptr;
		std::atomic<VersionPage*> &cell =
			versionPages_[slot / VersionPageSize];
		VersionPage *page = cell.load(std::memory_order_acquire);
		if (page)
			return page;
		VersionPage *candidate = new VersionPage();
		VersionPage *expected = nullptr;
		if (cell.compare_exchange_strong(
				expected, candidate,
				std::memory_order_release,
				std::memory_order_acquire))
			return candidate;
		delete candidate;
		return expected;
	}

	std::atomic<bool> active_{true};
	std::array<LockCell, ShardCount> locks_;
	// Consumers only need a non-zero token that changes after any Descriptor
	// State write. Keeping 1024 per-shard counters made every cache validation
	// read 1024 cache lines; one heap-wide token makes that hot read O(1).
	alignas(CacheLineBytes) std::atomic<uint64_t> version_{1};
	uint32_t maxSlots_ = 0;
	uint32_t versionPageCount_ = 0;
	std::unique_ptr<std::atomic<VersionPage*>[]> versionPages_;
};

// ---- DX12SparseDescriptorStore.hpp ----
#line 1 "DX12SparseDescriptorStore.hpp"
#include <stdint.h>

#include <array>
#include <atomic>
#include <memory>
#include <utility>
#include <vector>

// Sparse current-value storage used by Descriptor State. The old layout kept
// 1024 unordered_maps per heap so independently locked slots could be updated
// concurrently. Real descriptor rings issue tens of thousands of single-slot
// copies per frame, making two hash-table probes per copy the dominant CPU
// cost. This layout retains sparse allocation, but resolves a slot through a
// pre-sized page-pointer table and one direct array index instead.
//
// The store deliberately contains no per-slot locking. The owning descriptor
// heap supplies that policy. Page publication is atomic because different slot
// locks can touch the same page for the first time concurrently.
template <typename Value>
class DX12SparseDescriptorStore
{
public:
	// Kept in sync with the owning heap's lock striping contract.
	// Descriptor writes are short and command recording normally has only a
	// handful of producer threads.  1024 cache-line-sized locks gave every heap
	// a 64 KiB lock working set; sequential descriptor rings consequently
	// touched a different cache line for virtually every CopyDescriptors slot.
	// 64 stripes still provide substantially more concurrency than the wrapper
	// can use while keeping the complete lock set in a small L1/L2 footprint.
	static constexpr uint32_t ShardCount = 64;
	static constexpr uint32_t PageSize = 256;

	static_assert((ShardCount & (ShardCount - 1)) == 0,
		"descriptor shard count must be a power of two");
	static_assert((PageSize & (PageSize - 1)) == 0,
		"descriptor page size must be a power of two");

	static constexpr uint32_t ShardForSlot(uint32_t slot)
	{
		return slot & (ShardCount - 1);
	}

	struct SnapshotEntry
	{
		bool occupied = false;
		Value value = {};
	};

	DX12SparseDescriptorStore() = default;

	DX12SparseDescriptorStore(const DX12SparseDescriptorStore&) = delete;
	DX12SparseDescriptorStore &operator=(
		const DX12SparseDescriptorStore&) = delete;

	DX12SparseDescriptorStore(DX12SparseDescriptorStore &&other) noexcept
	{
		MoveFrom(std::move(other));
	}

	DX12SparseDescriptorStore &operator=(
		DX12SparseDescriptorStore &&other) noexcept
	{
		if (this == &other)
			return *this;
		ResetStorage();
		MoveFrom(std::move(other));
		return *this;
	}

	~DX12SparseDescriptorStore()
	{
		ResetStorage();
	}

	// Called once when the descriptor heap boundary becomes known. Only the
	// small page-pointer index is allocated here; Value pages remain lazy.
	void Initialize(uint32_t maxSlots)
	{
		ResetStorage();
		maxSlots_ = maxSlots;
		pageCount_ = maxSlots == 0 ? 0 : static_cast<uint32_t>(
			(static_cast<uint64_t>(maxSlots) + PageSize - 1) / PageSize);
		if (pageCount_ == 0)
			return;
		pages_ = std::make_unique<std::atomic<Page*>[]>(pageCount_);
		for (uint32_t index = 0; index < pageCount_; ++index)
			pages_[index].store(nullptr, std::memory_order_relaxed);
	}

	const Value *Find(uint32_t slot) const
	{
		const Page *page = PageForSlot(slot);
		if (!page)
			return nullptr;
		const uint32_t offset = OffsetForSlot(slot);
		return page->occupied[offset] ? &page->values[offset] : nullptr;
	}

	Value *Find(uint32_t slot)
	{
		Page *page = PageForSlot(slot);
		if (!page)
			return nullptr;
		const uint32_t offset = OffsetForSlot(slot);
		return page->occupied[offset] ? &page->values[offset] : nullptr;
	}

	// Returns the existing value or inserts one default value. Callers already
	// serialize the owning slot, so the returned address remains stable.
	Value *FindOrInsertDefault(uint32_t slot, bool *inserted)
	{
		Page *page = EnsurePageForSlot(slot);
		if (!page) {
			if (inserted)
				*inserted = false;
			return nullptr;
		}
		const uint32_t offset = OffsetForSlot(slot);
		const bool created = page->occupied[offset] == 0;
		if (created) {
			page->values[offset] = Value();
			page->occupied[offset] = 1;
		}
		if (inserted)
			*inserted = created;
		return &page->values[offset];
	}

	bool Store(uint32_t slot, Value value)
	{
		bool inserted = false;
		Value *stored = FindOrInsertDefault(slot, &inserted);
		if (!stored)
			return false;
		*stored = std::move(value);
		return inserted;
	}

	bool Erase(uint32_t slot)
	{
		Page *page = PageForSlot(slot);
		if (!page)
			return false;
		const uint32_t offset = OffsetForSlot(slot);
		if (!page->occupied[offset])
			return false;
		page->values[offset] = Value();
		page->occupied[offset] = 0;
		return true;
	}

	void SnapshotRange(
		uint32_t firstSlot,
		uint32_t count,
		std::vector<SnapshotEntry> *snapshot) const
	{
		if (!snapshot)
			return;
		snapshot->clear();
		snapshot->resize(count);
		for (uint32_t i = 0; i < count; ++i) {
			const Value *value = Find(firstSlot + i);
			if (!value)
				continue;
			(*snapshot)[i].occupied = true;
			(*snapshot)[i].value = *value;
		}
	}

	template <typename Visitor>
	void ForEach(Visitor visitor) const
	{
		for (uint32_t pageIndex = 0; pageIndex < pageCount_; ++pageIndex) {
			const Page *page = pages_[pageIndex].load(std::memory_order_acquire);
			if (!page)
				continue;
			const uint32_t firstSlot = pageIndex * PageSize;
			const uint32_t slotsInPage =
				maxSlots_ - firstSlot < PageSize ? maxSlots_ - firstSlot : PageSize;
			for (uint32_t offset = 0; offset < slotsInPage; ++offset) {
				if (page->occupied[offset])
					visitor(firstSlot + offset, page->values[offset]);
			}
		}
	}

	size_t Size() const
	{
		size_t size = 0;
		ForEach([&size](uint32_t, const Value&) { ++size; });
		return size;
	}

	bool Empty() const
	{
		for (uint32_t pageIndex = 0; pageIndex < pageCount_; ++pageIndex) {
			const Page *page = pages_[pageIndex].load(std::memory_order_acquire);
			if (!page)
				continue;
			const uint32_t firstSlot = pageIndex * PageSize;
			const uint32_t slotsInPage =
				maxSlots_ - firstSlot < PageSize ? maxSlots_ - firstSlot : PageSize;
			for (uint32_t offset = 0; offset < slotsInPage; ++offset) {
				if (page->occupied[offset])
					return false;
			}
		}
		return true;
	}

	// Releases only allocated pages while retaining the small page index, so a
	// cleared active heap can be populated again without rebuilding capacity.
	void Clear()
	{
		if (!pages_)
			return;
		for (uint32_t index = 0; index < pageCount_; ++index) {
			Page *page = pages_[index].exchange(
				nullptr, std::memory_order_acq_rel);
			delete page;
		}
	}

	uint32_t AllocatedPageCount() const
	{
		uint32_t count = 0;
		for (uint32_t index = 0; index < pageCount_; ++index) {
			if (pages_[index].load(std::memory_order_acquire))
				++count;
		}
		return count;
	}

private:
	struct Page
	{
		std::array<Value, PageSize> values = {};
		std::array<uint8_t, PageSize> occupied = {};
	};

	static constexpr uint32_t OffsetForSlot(uint32_t slot)
	{
		return slot & (PageSize - 1);
	}

	Page *PageForSlot(uint32_t slot)
	{
		return const_cast<Page*>(
			static_cast<const DX12SparseDescriptorStore*>(this)->PageForSlot(slot));
	}

	const Page *PageForSlot(uint32_t slot) const
	{
		if (!pages_ || slot >= maxSlots_)
			return nullptr;
		return pages_[slot / PageSize].load(std::memory_order_acquire);
	}

	Page *EnsurePageForSlot(uint32_t slot)
	{
		if (!pages_ || slot >= maxSlots_)
			return nullptr;
		std::atomic<Page*> &pageCell = pages_[slot / PageSize];
		Page *page = pageCell.load(std::memory_order_acquire);
		if (page)
			return page;

		Page *candidate = new Page();
		Page *expected = nullptr;
		if (pageCell.compare_exchange_strong(
				expected, candidate,
				std::memory_order_release,
				std::memory_order_acquire))
			return candidate;
		delete candidate;
		return expected;
	}

	void ResetStorage()
	{
		Clear();
		pages_.reset();
		pageCount_ = 0;
		maxSlots_ = 0;
	}

	void MoveFrom(DX12SparseDescriptorStore &&other)
	{
		pages_ = std::move(other.pages_);
		pageCount_ = other.pageCount_;
		maxSlots_ = other.maxSlots_;
		other.pageCount_ = 0;
		other.maxSlots_ = 0;
	}

	std::unique_ptr<std::atomic<Page*>[]> pages_;
	uint32_t pageCount_ = 0;
	uint32_t maxSlots_ = 0;
};

// ---- DX12ResourceTrackerPrivate.hpp ----
#line 1 "DX12ResourceTrackerPrivate.hpp"
#include <Shlwapi.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <cstring>
#include <algorithm>
#include <atomic>
#include <climits>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "crc32c.h"

struct RootSignatureRecord
{
	ID3D12RootSignature *rootSignature = nullptr;
	UINT64 hash = 0;
	SIZE_T size = 0;
	UINT nodeMask = 0;
	UINT version = 0;
	UINT flags = 0;
	UINT staticSamplerCount = 0;
	bool parsed = false;
	std::vector<DX12RootParameterSummary> parameters;
};

class DX12DescriptorViewPayload
{
public:
	union
	{
		D3D12_CONSTANT_BUFFER_VIEW_DESC cbv;
		D3D12_SHADER_RESOURCE_VIEW_DESC srv;
		D3D12_UNORDERED_ACCESS_VIEW_DESC uav;
		D3D12_RENDER_TARGET_VIEW_DESC rtv;
		D3D12_DEPTH_STENCIL_VIEW_DESC dsv;
		D3D12_SAMPLER_DESC sampler;
	};

	DX12DescriptorViewPayload()
	{
		memset(this, 0, sizeof(*this));
	}

	DX12DescriptorViewPayload(const DX12DescriptorViewPayload &other)
	{
		memcpy(this, &other, sizeof(*this));
	}

	DX12DescriptorViewPayload &operator=(const DX12DescriptorViewPayload &other)
	{
		if (this != &other)
			memcpy(this, &other, sizeof(*this));
		return *this;
	}
};

struct DescriptorRecord : public DX12DescriptorViewPayload
{
	std::string kind;
	UINT kindId = 0;
	UINT64 valueId = 0;
	SIZE_T cpuHandle = 0;
	ID3D12Resource *resource = nullptr;
	bool ownsResource = false;
	ID3D12Resource *counterResource = nullptr;
	bool ownsCounterResource = false;
	D3D12_RESOURCE_DESC resourceDesc = {};
	bool hasResourceDesc = false;
	UINT resourceHeapType = 0;
	bool hasResourceHeapType = false;
	UINT64 gpuVirtualAddress = 0;
	UINT64 resourceOffset = 0;
	UINT64 viewSize = 0;
	D3D12_RESOURCE_STATES currentState = D3D12_RESOURCE_STATE_COMMON;
	bool hasCurrentState = false;
	bool stateTrackedByBarrier = false;
	bool hasDesc = false;
};

struct DX12ExtendedDescriptorValue : public DX12DescriptorViewPayload
{
	ID3D12Resource *resource = nullptr;
	ID3D12Resource *counterResource = nullptr;
	D3D12_RESOURCE_DESC resourceDesc = {};
	bool hasResourceDesc = false;
	UINT resourceHeapType = 0;
	bool hasResourceHeapType = false;
	UINT64 gpuVirtualAddress = 0;
	UINT64 resourceOffset = 0;
	UINT64 viewSize = 0;
	D3D12_RESOURCE_STATES currentState = D3D12_RESOURCE_STATE_COMMON;
	bool hasCurrentState = false;
	bool stateTrackedByBarrier = false;
	bool hasDesc = false;
};

// Immutable descriptor payload kept by normal Mod-mode Descriptor State.
// CBVs are overwhelmingly dominant and need only 40 bytes here. Rich metadata
// is allocated only for SRV/UAV/RTV/DSV/sampler values, while slot identity,
// presentation strings and snapshot ownership are materialized on query.
struct DX12DescriptorValue
{
	UINT kindId = 0;
	UINT64 valueId = 0;
	D3D12_CONSTANT_BUFFER_VIEW_DESC cbv = {};
	std::unique_ptr<DX12ExtendedDescriptorValue> extended;

	DX12DescriptorValue() = default;
	DX12DescriptorValue(DX12DescriptorValue &&) noexcept = default;
	DX12DescriptorValue &operator=(DX12DescriptorValue &&) noexcept = default;

	DX12DescriptorValue(const DX12DescriptorValue &other)
		: kindId(other.kindId), valueId(other.valueId), cbv(other.cbv)
	{
		if (other.extended)
			extended = std::make_unique<DX12ExtendedDescriptorValue>(
				*other.extended);
	}

	DX12DescriptorValue &operator=(const DX12DescriptorValue &other)
	{
		if (this == &other)
			return *this;
		kindId = other.kindId;
		valueId = other.valueId;
		cbv = other.cbv;
		if (other.extended)
			extended = std::make_unique<DX12ExtendedDescriptorValue>(
				*other.extended);
		else
			extended.reset();
		return *this;
	}
};

class DX12DescriptorValueHashUtils
{
private:
	static UINT64 Mix(UINT64 value)
	{
		value ^= value >> 30;
		value *= 0xbf58476d1ce4e5b9ull;
		value ^= value >> 27;
		value *= 0x94d049bb133111ebull;
		value ^= value >> 31;
		return value;
	}

	static void Append(UINT64 *hash, const void *data, size_t size)
	{
		const uint8_t *bytes = static_cast<const uint8_t*>(data);
		for (size_t i = 0; i < size; ++i) {
			*hash ^= bytes[i];
			*hash *= 1099511628211ull;
		}
	}

	static UINT64 Finish(UINT64 hash)
	{
		return hash ? hash : 1;
	}

public:
	static UINT64 HashRecord(const DescriptorRecord &record)
	{
		// CBVs contain only BufferLocation and SizeInBytes. They account for
		// tens of thousands of descriptor writes per frame, so avoid feeding
		// their duplicated fields through the Debug /Od byte-at-a-time path.
		if (record.kindId == DescriptorKind::CBV_ID && record.hasDesc)
			return HashConstantBufferView(record.cbv);
		UINT64 hash = 14695981039346656037ull;
		Append(&hash, &record.kindId, sizeof(record.kindId));
		Append(&hash, &record.resource, sizeof(record.resource));
		Append(&hash, &record.counterResource, sizeof(record.counterResource));
		Append(&hash, &record.gpuVirtualAddress, sizeof(record.gpuVirtualAddress));
		Append(&hash, &record.resourceOffset, sizeof(record.resourceOffset));
		Append(&hash, &record.viewSize, sizeof(record.viewSize));
		Append(&hash, &record.hasDesc, sizeof(record.hasDesc));
		if (record.kindId == DescriptorKind::CBV_ID)
			Append(&hash, &record.cbv, sizeof(record.cbv));
		else if (record.kindId == DescriptorKind::SRV_ID)
			Append(&hash, &record.srv, sizeof(record.srv));
		else if (record.kindId == DescriptorKind::UAV_ID)
			Append(&hash, &record.uav, sizeof(record.uav));
		else if (record.kindId == DescriptorKind::RTV_ID)
			Append(&hash, &record.rtv, sizeof(record.rtv));
		else if (record.kindId == DescriptorKind::DSV_ID)
			Append(&hash, &record.dsv, sizeof(record.dsv));
		else if (record.kindId == DescriptorKind::Sampler_ID)
			Append(&hash, &record.sampler, sizeof(record.sampler));
		return Finish(hash);
	}

	static UINT64 HashConstantBufferView(
		const D3D12_CONSTANT_BUFFER_VIEW_DESC &desc)
	{
		const UINT64 location = Mix(
			desc.BufferLocation ^ 0x4342565f49445f31ull);
		const UINT64 size = Mix(
			static_cast<UINT64>(desc.SizeInBytes) ^ 0x9e3779b97f4a7c15ull);
		return Finish(location ^ size);
	}
};

// Normal Mod mode stores one immutable Descriptor Value and lets every copied
// Descriptor State reference it. CopyDescriptors then moves a pointer-sized
// binding instead of duplicating the full DescriptorRecord payload.
struct alignas(MEMORY_ALLOCATION_ALIGNMENT) DX12DescriptorValueNode
{
	SLIST_ENTRY freeEntry = {};
	volatile LONG references = 0;
	DX12DescriptorValue value;
};

class DX12DescriptorValuePool
{
private:
	static BOOL CALLBACK InitializeFreeList(
		PINIT_ONCE, PVOID parameter, PVOID*)
	{
		InitializeSListHead(static_cast<PSLIST_HEADER>(parameter));
		return TRUE;
	}

	static PSLIST_HEADER FreeList()
	{
		alignas(MEMORY_ALLOCATION_ALIGNMENT) static SLIST_HEADER freeList;
		static INIT_ONCE once = INIT_ONCE_STATIC_INIT;
		InitOnceExecuteOnce(&once, InitializeFreeList, &freeList, nullptr);
		return &freeList;
	}

public:
	static DX12DescriptorValueNode *Acquire()
	{
		PSLIST_ENTRY entry = InterlockedPopEntrySList(FreeList());
		DX12DescriptorValueNode *node = entry ?
			CONTAINING_RECORD(entry, DX12DescriptorValueNode, freeEntry) :
			new DX12DescriptorValueNode();
		InterlockedExchange(&node->references, 1);
		return node;
	}

	static void Retain(DX12DescriptorValueNode *node)
	{
		if (node)
			InterlockedIncrement(&node->references);
	}

	static void Release(DX12DescriptorValueNode *node)
	{
		if (!node || InterlockedDecrement(&node->references) != 0)
			return;
		node->value = DX12DescriptorValue();
		InterlockedPushEntrySList(FreeList(), &node->freeEntry);
	}
};

struct DescriptorSlotBinding
{
	DX12DescriptorValueNode *value = nullptr;
};

struct DescriptorHeapRecord
{
	ID3D12DescriptorHeap *heap = nullptr;
	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	SIZE_T cpuStart = 0;
	UINT64 gpuStart = 0;
	UINT increment = 0;
	using StateLocks = DX12DescriptorHeapShardState<
		DX12SparseDescriptorStore<DescriptorSlotBinding>::ShardCount>;
	// Lock striping follows the heap record when gDescriptorHeaps grows or
	// reuses a slot. Sequential descriptor slots land in different shards.
	std::unique_ptr<StateLocks> stateLocks;
	std::vector<DescriptorRecord> slots;
	std::vector<uint8_t> occupied;
	DX12SparseDescriptorStore<DescriptorSlotBinding> currentDescriptors;
	std::vector<std::vector<DescriptorRecord>> slotHistory;
	std::vector<std::vector<UINT64>> slotHistoryEpochs;
	volatile LONG64 liveSlots = 0;
};

class DX12DescriptorHeapStateUtils
{
public:
static void ResetVersion(DescriptorHeapRecord *record)
{
	if (!record || !record->stateLocks)
		return;
	record->stateLocks->ResetVersions();
}

static void BumpVersion(DescriptorHeapRecord *record, UINT slot)
{
	if (!record || !record->stateLocks)
		return;
	UINT64 valueId = 0;
	if (!record->slots.empty() && slot < record->slots.size() &&
			slot < record->occupied.size() && record->occupied[slot]) {
		valueId = record->slots[slot].valueId;
	} else {
		const DescriptorSlotBinding *binding =
			record->currentDescriptors.Find(slot);
		if (binding && binding->value)
			valueId = binding->value->value.valueId;
	}
	record->stateLocks->PublishSlotValue(slot, valueId);
}

// Copy hot paths already have the authoritative source identity in hand.  Do
// not look the destination up again after every copy merely to publish the
// value that was just stored.
static void PublishValue(DescriptorHeapRecord *record, UINT slot, UINT64 valueId)
{
	if (record && record->stateLocks)
		record->stateLocks->PublishSlotValue(slot, valueId);
}

static void PublishRangeValue(
	DescriptorHeapRecord *record, UINT slot, UINT64 valueId)
{
	if (record && record->stateLocks)
		record->stateLocks->PublishSlotIdentity(slot, valueId);
}

static void CommitPublishedRange(DescriptorHeapRecord *record)
{
	if (record && record->stateLocks)
		record->stateLocks->BumpHeapVersion();
}

static UINT64 ReadVersion(const DescriptorHeapRecord &record)
{
	return record.stateLocks ? record.stateLocks->ReadVersion() : 0;
}

static void FillSummary(const DescriptorHeapRecord &record, DX12DescriptorHeapSummary *summary)
{
	if (!summary)
		return;
	summary->heap = record.heap;
	summary->type = static_cast<UINT>(record.desc.Type);
	summary->numDescriptors = record.desc.NumDescriptors;
	summary->flags = static_cast<UINT>(record.desc.Flags);
	summary->nodeMask = record.desc.NodeMask;
	summary->cpuStart = record.cpuStart;
	summary->gpuStart = record.gpuStart;
	summary->increment = record.increment;
	summary->version = ReadVersion(record);
}
};

struct DescriptorSlotLocation
{
	size_t heapIndex = 0;
	UINT slot = 0;
};

class DX12DescriptorSlotArrayUtils
{
public:
static LONG64 ReadLiveSlots(const DescriptorHeapRecord *heap)
{
	return heap ? InterlockedCompareExchange64(
		const_cast<volatile LONG64*>(&heap->liveSlots), 0, 0) : 0;
}

static void SetLiveSlots(DescriptorHeapRecord *heap, LONG64 value)
{
	if (heap)
		InterlockedExchange64(&heap->liveSlots, value);
}

static void IncrementLiveSlots(DescriptorHeapRecord *heap)
{
	if (heap)
		InterlockedIncrement64(&heap->liveSlots);
}

static void DecrementLiveSlots(DescriptorHeapRecord *heap)
{
	if (!heap)
		return;
	LONG64 current = ReadLiveSlots(heap);
	while (current > 0) {
		const LONG64 observed = InterlockedCompareExchange64(
			&heap->liveSlots, current - 1, current);
		if (observed == current)
			return;
		current = observed;
	}
}

static void CopyRecordValueForSlot(DescriptorRecord *dest, const DescriptorRecord &src, SIZE_T destCpuHandle)
{
	if (!dest)
		return;
	// Descriptor copies dominate some DX12 games. DescriptorRecord's generated
	// assignment unconditionally assigns std::string even though a slot almost
	// always keeps the same descriptor kind (especially CBV). Copy the POD-like
	// payload explicitly and only touch the string when its value really changes.
	static_cast<DX12DescriptorViewPayload &>(*dest) = src;
	if (dest->kind != src.kind)
		dest->kind = src.kind;
	dest->kindId = src.kindId;
	dest->valueId = src.valueId;
	dest->cpuHandle = destCpuHandle;
	dest->resource = src.resource;
	// Descriptor heap slots mirror descriptor values. Ownership stays with the
	// CPU descriptor ledger/resource tracker, so slot copies do not multiply
	// COM references.
	dest->ownsResource = false;
	dest->counterResource = src.counterResource;
	dest->ownsCounterResource = false;
	dest->resourceDesc = src.resourceDesc;
	dest->hasResourceDesc = src.hasResourceDesc;
	dest->resourceHeapType = src.resourceHeapType;
	dest->hasResourceHeapType = src.hasResourceHeapType;
	dest->gpuVirtualAddress = src.gpuVirtualAddress;
	dest->resourceOffset = src.resourceOffset;
	dest->viewSize = src.viewSize;
	dest->currentState = src.currentState;
	dest->hasCurrentState = src.hasCurrentState;
	dest->stateTrackedByBarrier = src.stateTrackedByBarrier;
	dest->hasDesc = src.hasDesc;
}

static void CopyDescriptorValue(
	DX12DescriptorValue *dest, const DescriptorRecord &src)
{
	if (!dest)
		return;
	dest->kindId = src.kindId;
	dest->valueId = src.valueId;
	if (src.kindId == DescriptorKind::CBV_ID) {
		dest->cbv = src.cbv;
		dest->extended.reset();
		return;
	}
	dest->cbv = {};
	dest->extended = std::make_unique<DX12ExtendedDescriptorValue>();
	DX12ExtendedDescriptorValue &extended = *dest->extended;
	static_cast<DX12DescriptorViewPayload &>(extended) = src;
	extended.resource = src.resource;
	extended.counterResource = src.counterResource;
	extended.resourceDesc = src.resourceDesc;
	extended.hasResourceDesc = src.hasResourceDesc;
	extended.resourceHeapType = src.resourceHeapType;
	extended.hasResourceHeapType = src.hasResourceHeapType;
	extended.gpuVirtualAddress = src.gpuVirtualAddress;
	extended.resourceOffset = src.resourceOffset;
	extended.viewSize = src.viewSize;
	extended.currentState = src.currentState;
	extended.hasCurrentState = src.hasCurrentState;
	extended.stateTrackedByBarrier = src.stateTrackedByBarrier;
	extended.hasDesc = src.hasDesc;
}

static void CopyRecordValueForSlot(
	DescriptorRecord *dest,
	const DX12DescriptorValue &src,
	SIZE_T destCpuHandle)
{
	if (!dest)
		return;
	const char *kind = "";
	switch (src.kindId) {
	case DescriptorKind::CBV_ID: kind = "CBV"; break;
	case DescriptorKind::SRV_ID: kind = "SRV"; break;
	case DescriptorKind::UAV_ID: kind = "UAV"; break;
	case DescriptorKind::RTV_ID: kind = "RTV"; break;
	case DescriptorKind::DSV_ID: kind = "DSV"; break;
	case DescriptorKind::Sampler_ID: kind = "Sampler"; break;
	}
	if (dest->kind != kind)
		dest->kind = kind;
	dest->kindId = src.kindId;
	dest->valueId = src.valueId;
	dest->cpuHandle = destCpuHandle;
	dest->resource = nullptr;
	dest->ownsResource = false;
	dest->counterResource = nullptr;
	dest->ownsCounterResource = false;
	dest->resourceDesc = {};
	dest->hasResourceDesc = false;
	dest->resourceHeapType = 0;
	dest->hasResourceHeapType = false;
	dest->gpuVirtualAddress = 0;
	dest->resourceOffset = 0;
	dest->viewSize = 0;
	dest->currentState = D3D12_RESOURCE_STATE_COMMON;
	dest->hasCurrentState = false;
	dest->stateTrackedByBarrier = false;
	dest->hasDesc = false;
	if (src.kindId == DescriptorKind::CBV_ID) {
		static_cast<DX12DescriptorViewPayload &>(*dest) =
			DX12DescriptorViewPayload();
		dest->cbv = src.cbv;
		dest->gpuVirtualAddress = src.cbv.BufferLocation;
		dest->viewSize = src.cbv.SizeInBytes;
		dest->hasDesc = true;
		return;
	}
	if (!src.extended) {
		static_cast<DX12DescriptorViewPayload &>(*dest) =
			DX12DescriptorViewPayload();
		return;
	}
	const DX12ExtendedDescriptorValue &extended = *src.extended;
	static_cast<DX12DescriptorViewPayload &>(*dest) = extended;
	dest->resource = extended.resource;
	dest->counterResource = extended.counterResource;
	dest->resourceDesc = extended.resourceDesc;
	dest->hasResourceDesc = extended.hasResourceDesc;
	dest->resourceHeapType = extended.resourceHeapType;
	dest->hasResourceHeapType = extended.hasResourceHeapType;
	dest->gpuVirtualAddress = extended.gpuVirtualAddress;
	dest->resourceOffset = extended.resourceOffset;
	dest->viewSize = extended.viewSize;
	dest->currentState = extended.currentState;
	dest->hasCurrentState = extended.hasCurrentState;
	dest->stateTrackedByBarrier = extended.stateTrackedByBarrier;
	dest->hasDesc = extended.hasDesc;
}

static DX12DescriptorValue MakeConstantBufferViewValue(
	const D3D12_CONSTANT_BUFFER_VIEW_DESC &desc,
	UINT64 valueId)
{
	DX12DescriptorValue value;
	value.kindId = DescriptorKind::CBV_ID;
	value.valueId = valueId;
	value.cbv = desc;
	return value;
}

static bool SameRecordValueAtSlot(const DescriptorRecord &dest, const DescriptorRecord &src, SIZE_T destCpuHandle)
{
	return src.valueId != 0 && dest.valueId == src.valueId &&
		dest.cpuHandle == destCpuHandle;
}

static const DescriptorSlotBinding *FindCurrentBinding(
	const DescriptorHeapRecord &heap, UINT slot)
{
	return heap.currentDescriptors.Find(slot);
}

static bool CopyCurrentRecord(
	const DescriptorHeapRecord &heap, UINT slot, DescriptorRecord *record)
{
	if (!record)
		return false;
	const DescriptorSlotBinding *binding = FindCurrentBinding(heap, slot);
	if (binding && binding->value) {
		CopyRecordValueForSlot(
			record, binding->value->value,
			heap.cpuStart + static_cast<SIZE_T>(slot) * heap.increment);
		return true;
	}
	if (slot < heap.occupied.size() && heap.occupied[slot] &&
			slot < heap.slots.size()) {
		CopyRecordValueForSlot(record, heap.slots[slot], heap.slots[slot].cpuHandle);
		return true;
	}
	return false;
}

static void StoreCurrentRecord(
	DescriptorHeapRecord *heap,
	UINT slot,
	const DescriptorRecord &source,
	SIZE_T destCpuHandle)
{
	if (!heap || slot >= heap->desc.NumDescriptors)
		return;
	if (!heap->slots.empty() && slot < heap->slots.size() &&
			slot < heap->occupied.size()) {
		DescriptorRecord stored;
		CopyRecordValueForSlot(&stored, source, destCpuHandle);
		const bool wasOccupied = heap->occupied[slot] != 0;
		heap->slots[slot] = std::move(stored);
		heap->occupied[slot] = 1;
		if (!wasOccupied)
			IncrementLiveSlots(heap);
		return;
	}
	StoreCurrentRecordIfChanged(heap, slot, source, destCpuHandle);
}

static bool StoreCurrentRecordIfChanged(
	DescriptorHeapRecord *heap,
	UINT slot,
	const DescriptorRecord &source,
	SIZE_T destCpuHandle)
{
	if (!heap || slot >= heap->desc.NumDescriptors)
		return false;
	if (!heap->slots.empty() && slot < heap->slots.size() &&
			slot < heap->occupied.size()) {
		const bool wasOccupied = heap->occupied[slot] != 0;
		if (wasOccupied && SameRecordValueAtSlot(
				heap->slots[slot], source, destCpuHandle))
			return false;
		CopyRecordValueForSlot(&heap->slots[slot], source, destCpuHandle);
		heap->occupied[slot] = 1;
		if (!wasOccupied)
			IncrementLiveSlots(heap);
		return true;
	}

	bool inserted = false;
	DescriptorSlotBinding *stored =
		heap->currentDescriptors.FindOrInsertDefault(slot, &inserted);
	if (!inserted && stored->value && source.valueId != 0 &&
			stored->value->value.valueId == source.valueId)
		return false;
	DX12DescriptorValueNode *newValue = DX12DescriptorValuePool::Acquire();
	CopyDescriptorValue(&newValue->value, source);
	DX12DescriptorValueNode *oldValue = stored->value;
	stored->value = newValue;
	DX12DescriptorValuePool::Release(oldValue);
	if (inserted)
		IncrementLiveSlots(heap);
	return true;
}

static bool StoreCurrentValueIfChanged(
	DescriptorHeapRecord *heap,
	UINT slot,
	const DX12DescriptorValue &source,
	SIZE_T destCpuHandle)
{
	if (!heap || slot >= heap->desc.NumDescriptors || source.valueId == 0)
		return false;
	if (!heap->slots.empty() && slot < heap->slots.size() &&
			slot < heap->occupied.size()) {
		const bool wasOccupied = heap->occupied[slot] != 0;
		if (wasOccupied && heap->slots[slot].valueId == source.valueId &&
				heap->slots[slot].cpuHandle == destCpuHandle)
			return false;
		CopyRecordValueForSlot(
			&heap->slots[slot], source, destCpuHandle);
		heap->occupied[slot] = 1;
		if (!wasOccupied)
			IncrementLiveSlots(heap);
		return true;
	}

	bool inserted = false;
	DescriptorSlotBinding *stored =
		heap->currentDescriptors.FindOrInsertDefault(slot, &inserted);
	if (!inserted && stored->value &&
			stored->value->value.valueId == source.valueId)
		return false;
	DX12DescriptorValueNode *newValue = DX12DescriptorValuePool::Acquire();
	newValue->value = source;
	DX12DescriptorValueNode *oldValue = stored->value;
	stored->value = newValue;
	DX12DescriptorValuePool::Release(oldValue);
	if (inserted)
		IncrementLiveSlots(heap);
	return true;
}

enum class CopyCurrentSlotResult
{
	SourceMissingDestinationEmpty,
	SourceMissingDestinationErased,
	Changed,
	Unchanged
};

static CopyCurrentSlotResult CopyCurrentSlotIfChanged(
	DescriptorHeapRecord *destHeap,
	UINT destSlot,
	const DescriptorHeapRecord &srcHeap,
	UINT srcSlot,
	SIZE_T destCpuHandle,
	UINT64 *publishedValueId = nullptr)
{
	if (publishedValueId)
		*publishedValueId = 0;
	if (!destHeap || destSlot >= destHeap->desc.NumDescriptors ||
			srcSlot >= srcHeap.desc.NumDescriptors) {
		return SourceMissingResult(destHeap, destSlot);
	}

	const DescriptorSlotBinding *sourceBinding = FindCurrentBinding(srcHeap, srcSlot);
	if (sourceBinding && sourceBinding->value && destHeap->slots.empty()) {
		if (publishedValueId)
			*publishedValueId = sourceBinding->value->value.valueId;
		bool inserted = false;
		DescriptorSlotBinding *destBinding =
			destHeap->currentDescriptors.FindOrInsertDefault(destSlot, &inserted);
		if (!inserted && destBinding->value &&
				(destBinding->value == sourceBinding->value ||
				 destBinding->value->value.valueId ==
					sourceBinding->value->value.valueId))
			return CopyCurrentSlotResult::Unchanged;
		DX12DescriptorValuePool::Retain(sourceBinding->value);
		DX12DescriptorValueNode *oldValue = destBinding->value;
		destBinding->value = sourceBinding->value;
		DX12DescriptorValuePool::Release(oldValue);
		if (inserted)
			IncrementLiveSlots(destHeap);
		return CopyCurrentSlotResult::Changed;
	}

	DescriptorRecord source;
	if (!CopyCurrentRecord(srcHeap, srcSlot, &source))
		return SourceMissingResult(destHeap, destSlot);
	if (publishedValueId)
		*publishedValueId = source.valueId;
	return StoreCurrentRecordIfChanged(
		destHeap, destSlot, source, destCpuHandle) ?
		CopyCurrentSlotResult::Changed : CopyCurrentSlotResult::Unchanged;
}

static bool EraseCurrentRecord(DescriptorHeapRecord *heap, UINT slot)
{
	if (!heap)
		return false;
	if (!heap->slots.empty() && slot < heap->slots.size() &&
			slot < heap->occupied.size()) {
		if (!heap->occupied[slot])
			return false;
		heap->slots[slot] = DescriptorRecord();
		heap->occupied[slot] = 0;
		DecrementLiveSlots(heap);
		return true;
	}
	DescriptorSlotBinding *binding = heap->currentDescriptors.Find(slot);
	DX12DescriptorValueNode *oldValue = binding ? binding->value : nullptr;
	const bool erased = heap->currentDescriptors.Erase(slot);
	if (erased)
		DX12DescriptorValuePool::Release(oldValue);
	if (erased)
		DecrementLiveSlots(heap);
	return erased;
}

static void ClearSparseRecords(DescriptorHeapRecord *heap)
{
	if (!heap)
		return;
	heap->currentDescriptors.ForEach(
		[](uint32_t, const DescriptorSlotBinding &binding) {
			DX12DescriptorValuePool::Release(binding.value);
		});
	heap->currentDescriptors.Clear();
}

private:
static CopyCurrentSlotResult SourceMissingResult(
	DescriptorHeapRecord *destHeap, UINT destSlot)
{
	return EraseCurrentRecord(destHeap, destSlot) ?
		CopyCurrentSlotResult::SourceMissingDestinationErased :
		CopyCurrentSlotResult::SourceMissingDestinationEmpty;
}
};

struct ResourceRecord
{
	ID3D12Resource *resource = nullptr;
	UINT64 identityGeneration = 0;
	D3D12_RESOURCE_DESC desc = {};
	D3D12_HEAP_TYPE heapType = D3D12_HEAP_TYPE_DEFAULT;
	bool hasHeapType = false;
	UINT64 gpuVirtualAddress = 0;
	UINT64 size = 0;
	UINT64 gpuVirtualAddressRealEnd = 0;
	UINT64 gpuVirtualAddressOobEnd = 0;
	UINT64 placedHeapOffset = 0;
	UINT64 placedHeapSize = 0;
	bool hasPlacedHeapRange = false;
	D3D12_RESOURCE_STATES currentState = D3D12_RESOURCE_STATE_COMMON;
	bool hasCurrentState = false;
	bool stateTrackedByBarrier = false;
};

class DX12ResourceSnapshotUtils
{
public:
static bool TryAddRef(ID3D12Resource *resource)
{
	if (!resource)
		return false;
	__try {
		resource->AddRef();
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

static void Release(ID3D12Resource *resource)
{
	if (!resource)
		return;
	__try {
		resource->Release();
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
	}
}
};

struct BufferResolveCacheEntry
{
	UINT64 begin = 0;
	UINT64 end = 0;
	DX12BufferResourceSummary summary = {};
	UINT64 rangeEpoch = 0;
};

struct DX12BufferResolveCacheInvalidationStats
{
	size_t removedEntries = 0;
	bool rangeChanged = false;
};

struct DX12BufferResolveInvalidatedRange
{
	UINT64 epoch = 0;
	UINT64 begin = 0;
	UINT64 end = 0;
};

constexpr size_t kDX12BufferResolveInvalidationHistoryCapacity = 4096;

struct PsoRootRecord
{
	UINT64 psoIndex = 0;
	std::string kind;
	ID3D12PipelineState *pipelineState = nullptr;
	ID3D12RootSignature *rootSignature = nullptr;
};

extern SRWLOCK gResourceLock;
extern SRWLOCK gDescriptorLock;
extern std::vector<RootSignatureRecord> gRootSignatures;
extern std::deque<DescriptorHeapRecord> gDescriptorHeaps;
extern std::unordered_map<ID3D12RootSignature*, size_t> gRootSignatureByPtr;
extern std::unordered_map<ID3D12DescriptorHeap*, size_t> gDescriptorHeapByPtr;
extern std::map<SIZE_T, size_t> gDescriptorHeapByCpuStart;
extern std::unordered_map<SIZE_T, DescriptorRecord> gDescriptorByCpuHandle;
// gRuntimeDescriptorByCpuHandle removed — heap slots are the single source of truth.
// Use DX12DescriptorHeapShadowUtils::FindLatestDescriptor instead.
extern std::vector<PsoRootRecord> gPsoRoots;
extern std::unordered_map<UINT64, size_t> gPsoRootByIndex;
extern std::vector<ResourceRecord> gResources;
extern std::vector<size_t> gFreeResourceSlots;
extern size_t gLiveResourceCount;
extern std::unordered_map<ID3D12Resource*, size_t> gResourceByPtr;
extern std::map<UINT64, size_t> gBufferResourceByGpuVa;
extern std::map<UINT64, size_t> gBufferResourceByGpuVaComplete;
extern std::map<UINT64, BufferResolveCacheEntry> gBufferResolveCache;
extern std::atomic<UINT64> gBufferResolveGeneration;
extern std::atomic<UINT64> gResourceIdentityGeneration;
extern std::atomic<UINT64> gDescriptorQuerySerial;
extern bool gCleanupRegistered;
extern UINT64 gResourcesRecordedFromCreate;
extern UINT64 gDescriptorRecordsSeen;
extern UINT64 gDescriptorCopyRecordsSeen;
extern UINT64 gDescriptorHeapRecordsSeen;
extern UINT64 gRootSignatureRecordsSeen;
extern UINT64 gPsoRootRecordsSeen;
extern UINT64 gDescriptorEpoch;
extern LONG gLastResourceStatsPresent;
extern UINT64 gBufferResolveCacheInvalidations;
extern UINT64 gBufferResolveCacheInvalidationEntries;
extern UINT64 gBufferResolveTlsHits;
extern UINT64 gBufferResolveGlobalHits;
extern UINT64 gBufferResolveBackwardScanHits;
extern UINT64 gBufferResolveFallbackScanHits;
extern UINT64 gBufferResolveMisses;
extern UINT64 gBufferResolveGenerationResets;
extern UINT64 gBufferResolveRangeInvalidations;
extern UINT64 gBufferResolveRangeInvalidationEntries;
extern UINT64 gBufferResolveIndexNoopUpdates;
extern UINT64 gBufferResolveStateOnlyUpdates;
extern UINT64 gBufferResolveInvalidationEpoch;
extern std::atomic<UINT64> gBufferResolveInvalidationEpochAtomic;
extern UINT64 gBufferResolveInvalidationHistoryLost;
extern DX12BufferResolveInvalidatedRange gBufferResolveInvalidatedRanges[kDX12BufferResolveInvalidationHistoryCapacity];
extern UINT64 gResourceExclusiveLockSlowCount;
extern UINT64 gResourceExclusiveLockSlowMaxUs;
extern UINT64 gDescriptorExclusiveLockSlowCount;
extern UINT64 gDescriptorExclusiveLockSlowMaxUs;

class DX12BufferResolveCacheUtils
{
public:
static bool RangesOverlap(UINT64 beginA, UINT64 endA, UINT64 beginB, UINT64 endB)
{
	return beginA < endB && beginB < endA;
}

static UINT64 CurrentInvalidationEpochLocked()
{
	return gBufferResolveInvalidationEpoch;
}

static UINT64 NormalizeRangeEnd(UINT64 begin, UINT64 end)
{
	if (end > begin)
		return end;
	return UINT64_MAX;
}

static bool SameBufferRange(const ResourceRecord &a, const ResourceRecord &b)
{
	return a.gpuVirtualAddress == b.gpuVirtualAddress &&
		a.size == b.size &&
		a.gpuVirtualAddressRealEnd == b.gpuVirtualAddressRealEnd &&
		a.gpuVirtualAddressOobEnd == b.gpuVirtualAddressOobEnd &&
		a.desc.Dimension == b.desc.Dimension;
}

static bool HasBufferRange(const ResourceRecord &record)
{
	return record.resource &&
		record.desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER &&
		record.gpuVirtualAddress != 0 &&
		record.size != 0;
}

static UINT64 RangeEnd(const ResourceRecord &record)
{
	if (record.gpuVirtualAddressRealEnd)
		return record.gpuVirtualAddressRealEnd;
	return record.gpuVirtualAddress + record.size;
}

static size_t PurgeRangeLocked(UINT64 begin, UINT64 end)
{
	if (!begin || end <= begin)
		return 0;

	size_t removed = 0;
	// gBufferResolveCache is keyed by entry begin address.
	// Entries overlapping [begin, end) must satisfy: key < end AND entry.end > begin.
	// Find the entry with the largest key that is <= begin, then iterate forward
	// until key >= end (entries starting at or past 'end' cannot overlap).
	std::map<UINT64, BufferResolveCacheEntry>::iterator it =
		gBufferResolveCache.lower_bound(begin);
	if (it != gBufferResolveCache.begin())
		--it;
	while (it != gBufferResolveCache.end() && it->first < end) {
		if (DX12BufferResolveCacheUtils::RangesOverlap(
				it->second.begin, it->second.end, begin, end)) {
			it = gBufferResolveCache.erase(it);
			removed++;
		} else {
			++it;
		}
	}
	return removed;
}

static void AppendInvalidatedRangeLocked(UINT64 epoch, UINT64 begin, UINT64 end)
{
	if (!epoch || !begin || end <= begin)
		return;
	const size_t slot = static_cast<size_t>(
		epoch % static_cast<UINT64>(kDX12BufferResolveInvalidationHistoryCapacity));
	gBufferResolveInvalidatedRanges[slot].epoch = epoch;
	gBufferResolveInvalidatedRanges[slot].begin = begin;
	gBufferResolveInvalidatedRanges[slot].end = end;
}

static bool RangeInvalidatedSinceLocked(UINT64 begin, UINT64 end, UINT64 sinceEpoch)
{
	if (!begin)
		return true;
	end = DX12BufferResolveCacheUtils::NormalizeRangeEnd(begin, end);
	const UINT64 currentEpoch = gBufferResolveInvalidationEpoch;
	if (sinceEpoch == currentEpoch)
		return false;
	if (sinceEpoch > currentEpoch)
		return true;
	if (currentEpoch - sinceEpoch >=
		static_cast<UINT64>(kDX12BufferResolveInvalidationHistoryCapacity)) {
		gBufferResolveInvalidationHistoryLost++;
		return true;
	}

	for (UINT64 epoch = sinceEpoch + 1; epoch <= currentEpoch; ++epoch) {
		const size_t slot = static_cast<size_t>(
			epoch % static_cast<UINT64>(kDX12BufferResolveInvalidationHistoryCapacity));
		const DX12BufferResolveInvalidatedRange &range = gBufferResolveInvalidatedRanges[slot];
		if (range.epoch != epoch) {
			gBufferResolveInvalidationHistoryLost++;
			return true;
		}
		if (DX12BufferResolveCacheUtils::RangesOverlap(begin, end, range.begin, range.end))
			return true;
	}
	return false;
}

static UINT64 BumpInvalidationEpochLocked()
{
	gBufferResolveInvalidationEpoch++;
	if (!gBufferResolveInvalidationEpoch)
		gBufferResolveInvalidationEpoch = 1;
	gBufferResolveInvalidationEpochAtomic.store(
		gBufferResolveInvalidationEpoch, std::memory_order_relaxed);
	gBufferResolveGeneration.fetch_add(1, std::memory_order_relaxed);
	return gBufferResolveInvalidationEpoch;
}

static DX12BufferResolveCacheInvalidationStats InvalidateResourceRangeChangeLocked(
	const ResourceRecord *oldRecord,
	const ResourceRecord *newRecord,
	const char *reason,
	ID3D12Resource *resource)
{
	DX12BufferResolveCacheInvalidationStats stats;
	const bool oldHasRange = oldRecord && DX12BufferResolveCacheUtils::HasBufferRange(*oldRecord);
	const bool newHasRange = newRecord && DX12BufferResolveCacheUtils::HasBufferRange(*newRecord);
	if (oldHasRange && newHasRange &&
		DX12BufferResolveCacheUtils::SameBufferRange(*oldRecord, *newRecord)) {
		gBufferResolveIndexNoopUpdates++;
		return stats;
	}

	stats.rangeChanged = oldHasRange || newHasRange;
	if (!stats.rangeChanged) {
		gBufferResolveIndexNoopUpdates++;
		return stats;
	}

	if (oldHasRange)
		stats.removedEntries += DX12BufferResolveCacheUtils::PurgeRangeLocked(
			oldRecord->gpuVirtualAddress,
			DX12BufferResolveCacheUtils::RangeEnd(*oldRecord));
	if (newHasRange)
		stats.removedEntries += DX12BufferResolveCacheUtils::PurgeRangeLocked(
			newRecord->gpuVirtualAddress,
			DX12BufferResolveCacheUtils::RangeEnd(*newRecord));
	if (!oldHasRange && stats.removedEntries == 0) {
		gBufferResolveIndexNoopUpdates++;
		DX12LogManager::DebugJsonFunc("BufferResolveCacheInvalidated",
			"\"reason\":\"%s\",\"mode\":\"range_noop_new\",\"present\":%ld,\"threadId\":%lu,"
			"\"resource\":\"%p\",\"newBegin\":\"0x%llx\",\"newEnd\":\"0x%llx\","
			"\"gpuVaIndexSize\":%zu,\"liveResources\":%zu",
			reason ? reason : "",
			DX12RuntimeState::GetPresentCount(),
			GetCurrentThreadId(),
			resource,
			newHasRange ? static_cast<unsigned long long>(newRecord->gpuVirtualAddress) : 0ull,
			newHasRange ? static_cast<unsigned long long>(DX12BufferResolveCacheUtils::RangeEnd(*newRecord)) : 0ull,
			gBufferResourceByGpuVa.size(),
			gLiveResourceCount);
		return stats;
	}

	const UINT64 invalidationEpoch =
		DX12BufferResolveCacheUtils::BumpInvalidationEpochLocked();
	if (oldHasRange)
		DX12BufferResolveCacheUtils::AppendInvalidatedRangeLocked(
			invalidationEpoch,
			oldRecord->gpuVirtualAddress,
			DX12BufferResolveCacheUtils::RangeEnd(*oldRecord));
	if (newHasRange)
		DX12BufferResolveCacheUtils::AppendInvalidatedRangeLocked(
			invalidationEpoch,
			newRecord->gpuVirtualAddress,
			DX12BufferResolveCacheUtils::RangeEnd(*newRecord));
	gBufferResolveCacheInvalidations++;
	gBufferResolveCacheInvalidationEntries += stats.removedEntries;
	gBufferResolveRangeInvalidations++;
	gBufferResolveRangeInvalidationEntries += stats.removedEntries;
	DX12LogManager::DebugJsonFunc("BufferResolveCacheInvalidated",
		"\"reason\":\"%s\",\"mode\":\"range\",\"present\":%ld,\"threadId\":%lu,"
		"\"entriesRemoved\":%zu,\"generation\":%llu,\"resource\":\"%p\","
		"\"oldBegin\":\"0x%llx\",\"oldEnd\":\"0x%llx\",\"newBegin\":\"0x%llx\",\"newEnd\":\"0x%llx\","
		"\"gpuVaIndexSize\":%zu,\"liveResources\":%zu",
		reason ? reason : "",
		DX12RuntimeState::GetPresentCount(),
		GetCurrentThreadId(),
		stats.removedEntries,
		static_cast<unsigned long long>(gBufferResolveGeneration.load(std::memory_order_relaxed)),
		resource,
		oldHasRange ? static_cast<unsigned long long>(oldRecord->gpuVirtualAddress) : 0ull,
		oldHasRange ? static_cast<unsigned long long>(DX12BufferResolveCacheUtils::RangeEnd(*oldRecord)) : 0ull,
		newHasRange ? static_cast<unsigned long long>(newRecord->gpuVirtualAddress) : 0ull,
		newHasRange ? static_cast<unsigned long long>(DX12BufferResolveCacheUtils::RangeEnd(*newRecord)) : 0ull,
		gBufferResourceByGpuVa.size(),
		gLiveResourceCount);
	return stats;
}

static void InvalidateAllLocked(const char *reason)
{
	const size_t cacheEntriesBeforeClear = gBufferResolveCache.size();
	gBufferResolveCache.clear();
	const UINT64 invalidationEpoch = DX12BufferResolveCacheUtils::BumpInvalidationEpochLocked();
	DX12BufferResolveCacheUtils::AppendInvalidatedRangeLocked(invalidationEpoch, 1, UINT64_MAX);
	gBufferResolveCacheInvalidations++;
	gBufferResolveCacheInvalidationEntries += cacheEntriesBeforeClear;
	gBufferResolveGenerationResets++;
	DX12LogManager::DebugJsonFunc("BufferResolveCacheInvalidated",
		"\"reason\":\"%s\",\"mode\":\"global\",\"present\":%ld,\"threadId\":%lu,"
		"\"cacheEntriesBeforeClear\":%zu,\"generation\":%llu,\"gpuVaIndexSize\":%zu,\"liveResources\":%zu",
		reason ? reason : "",
		DX12RuntimeState::GetPresentCount(),
		GetCurrentThreadId(),
		cacheEntriesBeforeClear,
		static_cast<unsigned long long>(gBufferResolveGeneration.load(std::memory_order_relaxed)),
		gBufferResourceByGpuVa.size(),
		gLiveResourceCount);
}

static void RecordStateOnlyUpdateLocked()
{
	gBufferResolveStateOnlyUpdates++;
}
};

uint32_t HashBytes(uint32_t seed, const void *data, size_t size);
UINT64 Fnv1a64(const void *data, size_t size);
constexpr uint32_t MakeTrackerFourCC(char a, char b, char c, char d)
{
	return static_cast<uint32_t>(static_cast<uint8_t>(a)) |
		(static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8) |
		(static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16) |
		(static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
}
constexpr bool IsTextureResourceDimension(D3D12_RESOURCE_DIMENSION dimension)
{
	return dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D ||
		dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
		dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D;
}
D3D12_RESOURCE_DESC ResourceDescFromDesc1(const D3D12_RESOURCE_DESC1 *desc);
ID3D12Resource *CanonicalResource(ID3D12Resource *resource);
void RecordResource(
	ID3D12Resource *resource,
	const D3D12_RESOURCE_DESC *desc,
	const D3D12_HEAP_PROPERTIES *heapProperties,
	const PlacedResourceInfo *placedInfo,
	D3D12_RESOURCE_STATES initialState);
void CleanupTrackedResources();
void ParseRootSignatureBlob(RootSignatureRecord *record, const void *blob, SIZE_T blobLength);
void FillResourceInfo(DescriptorRecord *record, ID3D12Resource *resource);
void FillResourcePointerInfo(DescriptorRecord *record, ID3D12Resource *resource);
void CaptureDescriptorResourcePointer(DescriptorRecord *record, ID3D12Resource *resource);
void ReleaseDescriptorRecordResources(DescriptorRecord *record);
DescriptorRecord CloneDescriptorRecord(const DescriptorRecord &record);
void StoreCpuDescriptorRecordLocked(DescriptorRecord &&record);
bool ResolveBufferByGpuVa(UINT64 gpuVa, UINT64 size, DescriptorRecord *record);
void SetDescriptorKind(DescriptorRecord *record, UINT kindId);
void FillSrvBufferView(DescriptorRecord *record);
void FillUavBufferView(DescriptorRecord *record);
void PrepareDescriptorValueId(DescriptorRecord *record);
bool DescriptorValueUnchanged(SIZE_T cpuHandle, UINT64 valueId);
void LogResourceTrackerStatsLocked();
void InvalidateDescriptorHeapLocked(ID3D12DescriptorHeap *heap);
void InvalidateResourceLocked(ID3D12Resource *resource);
void InitializeDescriptorHeapRecord(DescriptorHeapRecord *record);
bool EnsureDescriptorHeapStorageLocked(DescriptorHeapRecord *record);
bool ResolveDescriptorSlotForCpuHandleLocked(
	SIZE_T cpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE *requiredType,
	DescriptorSlotLocation *location, UINT *slotsAvailable);
void RecordDescriptor(DescriptorRecord &&record);
// RecordDescriptorMerged replaced by DX12DescriptorHeapShadowUtils::WriteDescriptorToSlot
// + WriteToCpuLedger (dual-path: heap-slot fast path or CPU ledger fallback)
void RecordDescriptorBatch(std::vector<DescriptorRecord> *records);
void RecordRuntimeDescriptor(DescriptorRecord &&record);
void DX12RecordRootSignatureImmediate(
	ID3D12RootSignature *rootSignature,
	UINT nodeMask,
	const void *blob,
	SIZE_T blobLength);
void DX12PrimeDescriptorHistoryForHunting();
void BumpDescriptorQuerySerial();
// RecordDescriptorCopyRange and RecordRuntimeDescriptorCopyRange replaced by
// DX12DescriptorHeapShadowUtils::CopyDescriptorRange
void DX12RecordPsoRootSignatureImmediate(
	UINT64 psoIndex, const char *kind, ID3D12PipelineState *pipelineState,
	ID3D12RootSignature *rootSignature);
