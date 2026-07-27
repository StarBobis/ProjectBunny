#pragma once

#include "Mods/DX12ModInterfaces.hpp"
#include "Nektra/NktHookLib.h"
#include "util_min.h"
#include "crc32c.h"

#include <d3dcompiler.h>

#include <climits>
#include <condition_variable>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))


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
		DeviceMiscPrivateDataWrapper,
		DeviceMiscSharedHandleWrapper,
		DeviceMiscPowerWrapper,
		DeviceMiscProtectedSessionWrapper,
		DeviceMiscLifetimeWrapper,
		DeviceMiscMetaCommandWrapper,
		DeviceMiscStateObjectWrapper,
		DeviceMiscBackgroundProcessingWrapper,
		DeviceMiscShaderCacheWrapper,
		DeviceResidencyWrapper,
		ResourceMapWrapper,
		ResourceMapHook,
		ResourceUnmapWrapper,
		ResourceUnmapHook,
		ResourceReadWriteWrapper,
		ResourceQueryWrapper,
		DeviceQueryWrapper,
		DeviceQueryPrivateDataWrapper,
		DeviceQueryCapabilityWrapper,
		DeviceQueryDescriptorWrapper,
		DeviceQueryAllocationWrapper,
		DeviceQueryLayoutWrapper,
		DeviceQueryTilingWrapper,
		DeviceQueryMetaCommandWrapper,
		DeviceQueryRaytracingWrapper,
		DeviceQueryMiscWrapper,
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
		case Api::DeviceMiscPrivateDataWrapper:
			return DX12OuterTimingProbeUtils::ApiDeviceMiscPrivateData;
		case Api::DeviceMiscSharedHandleWrapper:
			return DX12OuterTimingProbeUtils::ApiDeviceMiscSharedHandle;
		case Api::DeviceMiscPowerWrapper:
			return DX12OuterTimingProbeUtils::ApiDeviceMiscPower;
		case Api::DeviceMiscProtectedSessionWrapper:
			return DX12OuterTimingProbeUtils::ApiDeviceMiscProtectedSession;
		case Api::DeviceMiscLifetimeWrapper:
			return DX12OuterTimingProbeUtils::ApiDeviceMiscLifetime;
		case Api::DeviceMiscMetaCommandWrapper:
			return DX12OuterTimingProbeUtils::ApiDeviceMiscMetaCommand;
		case Api::DeviceMiscStateObjectWrapper:
			return DX12OuterTimingProbeUtils::ApiDeviceMiscStateObject;
		case Api::DeviceMiscBackgroundProcessingWrapper:
			return DX12OuterTimingProbeUtils::ApiDeviceMiscBackgroundProcessing;
		case Api::DeviceMiscShaderCacheWrapper:
			return DX12OuterTimingProbeUtils::ApiDeviceMiscShaderCache;
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
		case Api::DeviceQueryPrivateDataWrapper:
			return DX12OuterTimingProbeUtils::ApiDeviceQueryPrivateData;
		case Api::DeviceQueryCapabilityWrapper:
			return DX12OuterTimingProbeUtils::ApiDeviceQueryCapability;
		case Api::DeviceQueryDescriptorWrapper:
			return DX12OuterTimingProbeUtils::ApiDeviceQueryDescriptor;
		case Api::DeviceQueryAllocationWrapper:
			return DX12OuterTimingProbeUtils::ApiDeviceQueryAllocation;
		case Api::DeviceQueryLayoutWrapper:
			return DX12OuterTimingProbeUtils::ApiDeviceQueryLayout;
		case Api::DeviceQueryTilingWrapper:
			return DX12OuterTimingProbeUtils::ApiDeviceQueryTiling;
		case Api::DeviceQueryMetaCommandWrapper:
			return DX12OuterTimingProbeUtils::ApiDeviceQueryMetaCommand;
		case Api::DeviceQueryRaytracingWrapper:
			return DX12OuterTimingProbeUtils::ApiDeviceQueryRaytracing;
		case Api::DeviceQueryMiscWrapper:
			return DX12OuterTimingProbeUtils::ApiDeviceQueryMisc;
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
			"DeviceMiscPrivateDataWrapper", "DeviceMiscSharedHandleWrapper",
			"DeviceMiscPowerWrapper", "DeviceMiscProtectedSessionWrapper",
			"DeviceMiscLifetimeWrapper", "DeviceMiscMetaCommandWrapper",
			"DeviceMiscStateObjectWrapper", "DeviceMiscBackgroundProcessingWrapper",
			"DeviceMiscShaderCacheWrapper",
			"DeviceResidencyWrapper",
			"ResourceMapWrapper", "ResourceMapHook",
			"ResourceUnmapWrapper", "ResourceUnmapHook",
			"ResourceReadWriteWrapper", "ResourceQueryWrapper", "DeviceQueryWrapper",
			"DeviceQueryPrivateDataWrapper", "DeviceQueryCapabilityWrapper",
			"DeviceQueryDescriptorWrapper", "DeviceQueryAllocationWrapper",
			"DeviceQueryLayoutWrapper", "DeviceQueryTilingWrapper",
			"DeviceQueryMetaCommandWrapper", "DeviceQueryRaytracingWrapper",
			"DeviceQueryMiscWrapper",
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
				if (totalUs < 40 && maxUs < 10)
					continue;
				DX12LogManager::Fields("DX12CreateTiming",
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


#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))


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




// Hot-path stage timing.
// Thread-local accumulators. QPC-based. Logged per-frame at Present.
// Detailed per-call QPC timing is opt-in, including in debug builds.
// Define DX12_ENABLE_DETAILED_TIMING when collecting timing diagnostics.
//
// Usage:
//   DX12_HOTPATH_TIME_BEGIN(DrawHook);
//   ... work ...
//   DX12_HOTPATH_TIME_END(DrawHook);

#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))

#define DX12_HOTPATH_TIME_ENABLED 1

#if !DX12_HOTPATH_TIME_ENABLED

// Empty inline stubs; the compiler eliminates all calls.
__forceinline void DX12HotPathTimeBegin(DX12HotPathStage::Id) {}
__forceinline void DX12HotPathTimeEnd(DX12HotPathStage::Id) {}
__forceinline void DX12HotPathTimeLogAndResetAll() {}
__forceinline void DX12HotPathTimeEnsureFreq() {}

#else

// Timing stage identifiers.
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

// Per-stage thread-local accumulator.
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

	// Snapshot and reset at frame end.
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
			DX12LogManager::Fields("DX12HotPathTiming",
				"\"stage\":\"%s\",\"calls\":%ld,\"totalUs\":%lld,\"avgUs\":%ld,\"maxUs\":%ld",
				DX12HotPathStage::Name(static_cast<DX12HotPathStage::Id>(i)),
				calls,
				static_cast<long long>(totalUs),
				avgUs,
				tlsAccum[i].maxUs);
			tlsAccum[i] = {};
		}
	}

	// RAII scoped timer.
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




#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
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

class DX12PreSkinStableFingerprintCacheUtils
{
public:
	class Entry
	{
	public:
		UINT64 dependency = 0;
		UINT64 fingerprint = 0;
	};

	static constexpr size_t EntryCount = 1024;
	inline static thread_local std::array<Entry, EntryCount> Entries = {};

	static size_t Slot(UINT64 dependency)
	{
		dependency ^= dependency >> 33;
		dependency *= 0xff51afd7ed558ccdull;
		dependency ^= dependency >> 33;
		return static_cast<size_t>(dependency) & (EntryCount - 1);
	}

	static bool TryLoad(UINT64 dependency, UINT64 *fingerprint)
	{
		if (!dependency || !fingerprint)
			return false;
		const Entry &entry = Entries[Slot(dependency)];
		if (entry.dependency != dependency || !entry.fingerprint)
			return false;
		*fingerprint = entry.fingerprint;
		return true;
	}

	static void Store(UINT64 dependency, UINT64 fingerprint)
	{
		if (!dependency || !fingerprint)
			return;
		Entry &entry = Entries[Slot(dependency)];
		entry.dependency = dependency;
		entry.fingerprint = fingerprint;
	}
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
		if (!originalDispatch)
			return;
		const UINT64 flagsWord = DX12HotPathState::HookWorkFlags;
		if (!(flagsWord & DX12HotPathState::MASK_DISPATCH_WORK) &&
			!DX12Profiling::ShouldCollectCounters()) {
			DX12_PROFILE_FAST_FORWARD();
			originalDispatch(
				commandList,
				threadGroupCountX,
				threadGroupCountY,
				threadGroupCountZ);
			return;
		}
		DX12SimulateContext *runtimeState =
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
		DX12SimulateContext *runtimeState,
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
		const bool shouldRecordBindingEvents =
			DX12HotPathState::NeedsBindingEventRecording(flagsWord);
		const bool needsDispatchDecision =
			hasShaderOverrides || needsPreSkinProbe;
		// No-candidate compute PSOs are resident state, just like DX11's current
		// shader. Bypass tracking-state lookup and all decision/cache machinery.
		if (DX12HotPathState::CanResidentDispatchFastReject(
				flagsWord, runtimeState)) {
			DX12_PROFILE_FAST_FORWARD();
			DX12ModRestorePreSkinningUavReplacement(commandList);
			originalDispatch(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
			DX12_HOTPATH_TIME_END(DispatchHookTotal);
			return;
		}
		if (DX12Profiling::ShouldCollectCounters() || needsDispatchDecision)
			DX12RuntimeState::IncrementFrameDispatches();

		if (DX12InternalReplayState::IsActive()) {
			originalDispatch(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
			DX12_HOTPATH_TIME_END(DispatchHookTotal);
			return;
		}

		// Match DX11's compute path: when dispatch only needs logging/hunting or
		// capture, stay on the straight-through path and skip PSO policy rebuilds.
		if (!needsDispatchDecision) {
			DX12ModRestorePreSkinningUavReplacement(commandList);
			if (DX12HotPathState::skipBindings && huntEnabled) {
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
			if (shouldRecordBindingEvents) {
				const DX12CommandListTrackingState tracking =
					DX12CommandListStateCapture::GetTrackingState(commandList, runtimeState);
				if (tracking.recordBindingEvents)
					DX12BindingTracker::RecordDispatch(
						commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
			}
			if (huntEnabled) {
				DX12HuntRecordDispatch(commandList);
				if (DX12HuntShouldSkipDispatch(commandList)) {
					DX12_HOTPATH_TIME_END(DispatchHookTotal);
					return;
				}
			}
			DX12_PROFILE_FAST_FORWARD();
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
			DX12ModRestorePreSkinningUavReplacement(commandList);
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
		if (shouldRecordBindingEvents) {
			const DX12CommandListTrackingState tracking =
				DX12CommandListStateCapture::GetTrackingState(commandList, runtimeState);
			if (tracking.recordBindingEvents)
				DX12BindingTracker::RecordDispatch(
					commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
		}
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
			DX12ModRestorePreSkinningUavReplacement(commandList);
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
		DX12SimulateContext *runtimeState,
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
		DX12SimulateContext fallbackState = {};
		DX12SimulateContext *effectiveState = runtimeState;
		if (!effectiveState) {
		#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
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
			DX12ModRestorePreSkinningUavReplacement(commandList);
			originalDispatch(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
			return;
		}
		DX12ResidentMaintenanceTelemetryUtils::RecordDispatchDecisionPreSkinCandidateDispatch();

		DX12DispatchPreSkinProbe &probe = ScratchProbe();
		const UINT64 outerReloadGeneration = DX12ModGetReloadGenerationFast();
		if (effectiveState && effectiveDecision &&
			effectiveState->resolvedCompute.probeResultValid &&
			!effectiveState->resolvedCompute.probeApplied &&
			effectiveDecision->computeShaderHash != 0 &&
			effectiveState->resolvedCompute.probeCsHash ==
				effectiveDecision->computeShaderHash &&
			effectiveState->resolvedCompute.probeReloadGeneration ==
				outerReloadGeneration &&
			effectiveState->resolvedCompute.probeIaContentEpoch ==
				effectiveState->iaContentEpoch &&
			effectiveState->resolvedCompute.bindingsValid &&
			!effectiveState->resolvedCompute.bindingsUseRootValues &&
			effectiveState->resolvedCompute.bindingsHeap ==
				effectiveState->cbvSrvUavHeap &&
			effectiveState->resolvedCompute.bindingsRootSignature ==
				effectiveState->computeRootSignature &&
			effectiveState->resolvedCompute.bindingsDescriptorVersion ==
				effectiveState->computeDescriptorBindingVersion) {
			probe.computeShaderHash = effectiveDecision->computeShaderHash;
			probe.computeBindingSerial = effectiveState->computeBindingSerial;
			probe.applied = false;
			DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinPlanCacheLookup();
			DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinPlanCacheHit(false);
			DX12OuterTimingProbeUtils::LogCpuStage(
				"PreSkin",
				"probe_cache_dispatch_descriptor_version_negative_hit",
				0,
				1,
				0);
			DX12ModRestorePreSkinningUavReplacement(commandList);
			originalDispatch(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
			return;
		}
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
		const DX12SimulateContext *runtimeState)
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
#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
		if (reason && (!strcmp(reason, "uav_filter_miss") ||
		    !strcmp(reason, "cached_no_uav_match") ||
		    !strcmp(reason, "cs_not_configured"))) {
			return;
		}
		DX12LogManager::DebugFields("DX12PreSkinDispatchProbe",
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
		DX12SimulateContext *runtimeState,
		DX12DispatchPreSkinProbe *probe)
	{
		if (!probe || !(DX12HotPathState::HookWorkFlags &
			DX12HotPathState::FLAG_PRE_SKIN_PROBE)) {
	#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
			DX12LogManager::Text("PreparePreSkinProbe: exit needsProbe failed\n");
	#endif
			return;
		}

		LARGE_INTEGER t0 = DX12OuterTimingProbeUtils::Now();

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
	#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
			DX12LogManager::Text("PreparePreSkinProbe: exit no cs hash\n");
#endif
			LogPreSkinDispatchProbeReason("no_pipeline_cs_hash", 0, 0, 0);
			return;
		}
		probe->computeBindingSerial = runtimeState ? runtimeState->computeBindingSerial : 0;

		// Execute only reaches this method after the reload-generation-aware
		// dispatch decision has already confirmed this CS as a PreSkin candidate.

		probe->computeBindingSerial = runtimeState ?
			runtimeState->computeBindingSerial :
			DX12BindingTracker::GetComputeBindingSerial(commandList);

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
		if (runtimeState &&
			runtimeState->resolvedCompute.probeResultValid &&
			!runtimeState->resolvedCompute.probeApplied &&
			runtimeState->resolvedCompute.probeCsHash == probe->computeShaderHash &&
			runtimeState->resolvedCompute.probeReloadGeneration == earlyReloadGeneration &&
			runtimeState->resolvedCompute.probeIaContentEpoch == runtimeState->iaContentEpoch &&
			runtimeState->resolvedCompute.bindingsValid &&
			!runtimeState->resolvedCompute.bindingsUseRootValues &&
			runtimeState->resolvedCompute.bindingsHeap == runtimeState->cbvSrvUavHeap &&
			runtimeState->resolvedCompute.bindingsRootSignature ==
				runtimeState->computeRootSignature &&
			runtimeState->resolvedCompute.bindingsDescriptorVersion ==
				runtimeState->computeDescriptorBindingVersion) {
			probe->applied = false;
			DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinPlanCacheLookup();
			DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinPlanCacheHit(false);
			DX12OuterTimingProbeUtils::LogCpuStage(
				"PreSkin",
				"probe_cache_descriptor_version_negative_hit",
				0,
				1,
				DX12OuterTimingProbeUtils::ElapsedUs(t0));
			return;
		}

		UINT64 templateStableBindingFingerprint = 0;
		if (!probe->uavsFound) {
			DX12_HOTPATH_TIME_BEGIN(PreSkinBindQuery);
			LARGE_INTEGER collectBegin = DX12OuterTimingProbeUtils::Now();

			// Consume the validated recording-context template directly. The
			// collector remains the cold path that materializes a missing template.
			probe->cachedUavs = nullptr;
			probe->cachedSrvs = nullptr;
			probe->cachedCbvs = nullptr;
			probe->cachedRootConstants = nullptr;
			bool computeTemplateLoaded =
				DX12BindingTracker::GetSimulateContextPreSkinBindings(
				commandList,
				&probe->cachedUavs,
				&probe->cachedSrvs,
				&probe->cachedCbvs,
				&probe->cachedRootConstants,
				&probe->bindingDependencyFingerprint,
				&probe->uavsFound);
			if (computeTemplateLoaded &&
				runtimeState &&
				runtimeState->resolvedCompute.probeResultValid &&
				!runtimeState->resolvedCompute.probeApplied &&
				runtimeState->resolvedCompute.probeCsHash == probe->computeShaderHash &&
				runtimeState->resolvedCompute.probeReloadGeneration == earlyReloadGeneration &&
				runtimeState->resolvedCompute.probeIaContentEpoch == runtimeState->iaContentEpoch &&
				runtimeState->resolvedCompute.probeBindingDependencyFingerprint ==
					probe->bindingDependencyFingerprint) {
				probe->applied = false;
				DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinPlanCacheLookup();
				DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinPlanCacheHit(false);
				DX12OuterTimingProbeUtils::LogCpuStage(
					"PreSkin",
					"probe_cache_template_negative_hit",
					0,
					1,
					DX12OuterTimingProbeUtils::ElapsedUs(t0));
				DX12_HOTPATH_TIME_END(PreSkinBindQuery);
				return;
			}
			if (computeTemplateLoaded &&
				runtimeState &&
				runtimeState->resolvedCompute.probeResultValid &&
				!runtimeState->resolvedCompute.probeApplied &&
				runtimeState->resolvedCompute.probeCsHash == probe->computeShaderHash &&
				runtimeState->resolvedCompute.probeReloadGeneration == earlyReloadGeneration &&
				runtimeState->resolvedCompute.probeIaContentEpoch == runtimeState->iaContentEpoch) {
				const std::vector<DX12CurrentComputeUavBinding> &templateUavs =
					probe->cachedUavs ? *probe->cachedUavs : probe->uavs;
				const std::vector<DX12CurrentComputeUavBinding> &templateSrvs =
					probe->cachedSrvs ? *probe->cachedSrvs : probe->srvs;
				const std::vector<DX12CurrentComputeUavBinding> &templateCbvs =
					probe->cachedCbvs ? *probe->cachedCbvs : probe->cbvs;
				const std::vector<DX12CurrentRootConstants> &templateRootConstants =
					probe->cachedRootConstants ? *probe->cachedRootConstants : probe->rootConstants;
				if (!DX12PreSkinStableFingerprintCacheUtils::TryLoad(
						probe->bindingDependencyFingerprint,
						&templateStableBindingFingerprint)) {
					DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinPlanFingerprintBuild();
					templateStableBindingFingerprint =
						DX12PreSkinPlanUtils::BuildCrossFrameStableBindingFingerprint(
							templateUavs,
							templateSrvs,
							templateCbvs,
							templateRootConstants);
					if (templateStableBindingFingerprint == 0)
						templateStableBindingFingerprint = 1;
					DX12PreSkinStableFingerprintCacheUtils::Store(
						probe->bindingDependencyFingerprint,
						templateStableBindingFingerprint);
				}
				const UINT64 templateProbeFingerprint =
					DX12PreSkinCacheIdentityUtils::MakeProbeFingerprint(
						templateStableBindingFingerprint,
						runtimeState->iaContentEpoch);
				if (runtimeState->resolvedCompute.probeFingerprint == templateProbeFingerprint) {
					probe->applied = false;
					DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinPlanCacheLookup();
					DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinPlanCacheHit(false);
					DX12OuterTimingProbeUtils::LogCpuStage(
						"PreSkin",
						"probe_cache_template_fingerprint_negative_hit",
						0,
						1,
						DX12OuterTimingProbeUtils::ElapsedUs(t0));
					DX12_HOTPATH_TIME_END(PreSkinBindQuery);
					return;
				}
			}
			if (!computeTemplateLoaded) {
				probe->uavsFound = DX12BindingTracker::CollectPreSkinBindings(
					commandList, &probe->uavs, &probe->srvs, &probe->cbvs,
					&probe->rootConstants, &probe->bindingDependencyFingerprint);
			}
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
		if (templateStableBindingFingerprint) {
			computeIaFingerprint = templateStableBindingFingerprint;
		} else if (runtimeState && probe->bindingDependencyFingerprint &&
				runtimeState->resolvedCompute.bindingFingerprintDependency ==
					probe->bindingDependencyFingerprint &&
				runtimeState->resolvedCompute.bindingFingerprint) {
			computeIaFingerprint = runtimeState->resolvedCompute.bindingFingerprint;
		} else if (DX12PreSkinStableFingerprintCacheUtils::TryLoad(
				probe->bindingDependencyFingerprint,
				&computeIaFingerprint)) {
		} else {
			DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinPlanFingerprintBuild();
			// Use the cross-frame stable fingerprint: it strips per-frame-volatile
			// fields (GPU VA, resource pointer, descriptor offset) that the game's
			// descriptor arena rewrites every frame, so the plan cache can finally
			// hit across frames. PreSkin replacement results depend on the binding
			// pattern (CS + slot layout + descriptor shape), not on the volatile
			// addresses.
			computeIaFingerprint = DX12PreSkinPlanUtils::BuildCrossFrameStableBindingFingerprint(
				uavs, srvs, cbvs, rootConstants);
			if (computeIaFingerprint == 0)
				computeIaFingerprint = 1;
			if (runtimeState && probe->bindingDependencyFingerprint) {
				runtimeState->resolvedCompute.bindingFingerprintDependency =
					probe->bindingDependencyFingerprint;
				runtimeState->resolvedCompute.bindingFingerprint = computeIaFingerprint;
			}
			DX12PreSkinStableFingerprintCacheUtils::Store(
				probe->bindingDependencyFingerprint,
				computeIaFingerprint);
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
			 probe = probe,
			 probeBindingSerial, probeIaContentEpoch](
				uint64_t computeShaderHash,
				bool applied,
				const DX12PreSkinDispatchOverride &dispatchOverride) {
				if (!runtimeState)
					return;
				runtimeState->resolvedCompute.probeBindingDependencyFingerprint =
					probe ? probe->bindingDependencyFingerprint : 0;
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
	#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
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
	#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
				DX12LogManager::Fields("DX12Perf",
					"\"scope\":\"PreSkinProbe\",\"cs\":\"%016llx\",\"applied\":false,\"cache\":\"negative_hit\",\"us\":%lld",
					static_cast<unsigned long long>(probe->computeShaderHash),
					static_cast<long long>(DX12OuterTimingProbeUtils::ElapsedUs(t0)));
	#endif
				return;
			}
			positiveCacheHit = true;
			cachedDispatchOverride = cacheEntry.dispatchOverride;
	#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
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
	#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
		probe->shouldLog = true;
	#endif
	#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
		DX12LogManager::Text("PreparePreSkinProbe: uavsFound=%d uavCount=%zu srvCount=%zu cbvCount=%zu\n",
			probe->uavsFound ? 1 : 0, uavs.size(), srvs.size(), cbvs.size());

		if (!probe->srvsFound) {
			DX12LogManager::Text("PreparePreSkinProbe: warning no_compute_srvs\n");
		}
#endif

		static const std::vector<DX12CurrentComputeUavBinding> kEmptyComputeBindings;
		static const std::vector<DX12CurrentRootConstants> kEmptyRootConstants;
	#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
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
			&probe->dispatchOverride,
			runtimeState ? runtimeState->device : nullptr,
			computeIaFingerprint);
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
		const bool updateSharedProbeCache =
			(!runtimeProbeCacheHit && !positiveCacheHit) || !probe->applied;
		if (updateSharedProbeCache) {
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

	#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
		DX12LogManager::Fields("DX12Perf",
			"\"scope\":\"PreSkinProbe\",\"cs\":\"%016llx\",\"applied\":%s,\"cache\":\"%s\",\"us\":%lld",
			static_cast<unsigned long long>(probe->computeShaderHash),
			probe->applied ? "true" : "false",
			positiveCacheHit ? "positive_reapply" : "miss",
			static_cast<long long>(DX12OuterTimingProbeUtils::ElapsedUs(t0)));
	#endif
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
		// Keep PreSkin descriptor/table mutations scoped to the current
		// dispatch. Deferred reuse is a CPU optimization, but if our tracked
		// command-list state drifts from the actual restored bindings it can
		// poison later matching and leave patched tables active for unrelated
		// Unity work.
		LARGE_INTEGER restoreBegin = DX12OuterTimingProbeUtils::Now();
		DX12ModRestorePreSkinningUavReplacement(commandList);
		DX12OuterTimingProbeUtils::LogCpuStage(
			"PreSkin",
			"restore_replacement",
			0,
			1,
			DX12OuterTimingProbeUtils::ElapsedUs(restoreBegin));
#if defined(_DEBUG)
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
	#endif
		if (!probe.applied)
			return;

		DX12_DISPATCH_DEBUG_VERBOSE_LOG(
			DX12LogManager::DebugFields("DX12DispatchUavProbe",
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


class DX12IaBufferViewUtils
{
public:
	static bool IsVertexBufferBound(const D3D12_VERTEX_BUFFER_VIEW &view)
	{
		return view.BufferLocation != 0 && view.SizeInBytes != 0;
	}
};



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

	static bool ExpandInPlaceVertexBufferView(
		const D3D12_VERTEX_BUFFER_VIEW &sourceView,
		D3D12_VERTEX_BUFFER_VIEW *expandedView)
	{
		// DX11 derives the readable VB range from the buffer object, while DX12
		// freezes it in D3D12_VERTEX_BUFFER_VIEW::SizeInBytes. Creation-time
		// resource expansion therefore also needs this command-list-local view
		// expansion. Match the exact active PreSkin output and keep BufferLocation
		// unchanged: its resource identity carries the game's barriers and queue
		// ordering. Do not replace it with a copied buffer or a global GPU-VA map;
		// independently recorded shadow passes can otherwise consume another frame.
		if (!expandedView || !sourceView.BufferLocation || !sourceView.SizeInBytes)
			return false;
		if (!DX12ModHasActivePreSkinTextureOverrides())
			return false;

		DX12BufferResourceSummary resource;
		if (!DX12ResolveBufferResourceByGpuVa(
				sourceView.BufferLocation, 1, &resource) ||
			!resource.hasResourceDesc ||
			resource.resourceDesc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER ||
			resource.gpuVirtualAddress > sourceView.BufferLocation)
			return false;

		const UINT64 offset = sourceView.BufferLocation - resource.gpuVirtualAddress;
		DX12ModPreSkinProducerSummary producer;
		if (!DX12ModFindActivePreSkinProducerForOutput(
				resource.resource, 0, sourceView.StrideInBytes, &producer) ||
			!producer.outputByteWidth ||
			producer.outputByteWidth > resource.resourceDesc.Width ||
			producer.outputByteWidth <= offset)
			return false;

		const UINT expandedBytes = ClampViewBytes(producer.outputByteWidth - offset);
		if (expandedBytes <= sourceView.SizeInBytes)
			return false;
		*expandedView = sourceView;
		expandedView->SizeInBytes = expandedBytes;
		return true;
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
		// A negative lookup is valid only while both the fallback map and the
		// active producer set are unchanged. A new dispatch can make an existing
		// same-VA view expandable without changing the fallback-map generation.
		const UINT64 resizeGeneration =
			DX12ModGetPreSkinOutputResizeGeneration() ^
			(DX12ModGetPreSkinActiveGenerationFast() * 0x9e3779b97f4a7c15ull);
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
			if (ExpandInPlaceVertexBufferView(sourceView, &redirectedViews[i])) {
				if (activeIa && slot < D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT)
					activeIa->preSkinRedirectNegativeValid[slot] = false;
				anyRedirected = true;
#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
				DX12LogManager::DebugFields("DX12PreSkinningApply",
					"\"status\":\"vb_view_expanded\","
					"\"slot\":%u,\"gpuVa\":\"0x%llx\","
					"\"oldBytes\":%u,\"newBytes\":%u,\"stride\":%u",
					slot,
					static_cast<unsigned long long>(sourceView.BufferLocation),
					sourceView.SizeInBytes,
					redirectedViews[i].SizeInBytes,
					redirectedViews[i].StrideInBytes);
#endif
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
#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
			DX12LogManager::DebugFields("DX12PreSkinningApply",
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
	UINT64 preSkinGeneration = 0;
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
			preSkinGeneration == other.preSkinGeneration &&
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
		hash = Mix(hash, key.preSkinGeneration);
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
	class PsoDrawFastRejectThreadCache
	{
	public:
		class Entry
		{
		public:
			bool valid = false;
			DX12PsoDrawFastRejectKey key;
		};

		static constexpr size_t SlotCount = 256;
		std::array<Entry, SlotCount> entries;

		static size_t Slot(const DX12PsoDrawFastRejectKey &key)
		{
			return DX12PsoDrawFastRejectKeyHash()(key) & (SlotCount - 1);
		}
	};

	inline static thread_local PsoDrawFastRejectThreadCache s_psoDrawFastRejectCache;

	static bool TryLoadResolvedIaHashBuild(
		DX12SimulateContextDrawTemplate::CachedIaHashBuildEntry *entry,
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
		DX12SimulateContextDrawTemplate::CachedIaHashBuildEntry *entry,
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

	static size_t DirectIaCandidateTag(
		const DX12IaHashStateCacheKey &iaKey,
		const DX12IaDrawInvocation &draw,
		const DX12DrawMatchUsage &usage)
	{
		size_t hash = DX12IaHashStateCacheKeyHash()(iaKey);
		hash = DX12IaHashStateCacheKeyHash::Combine(
			hash, CanonicalInstanceCount(draw, usage));
		hash = DX12IaHashStateCacheKeyHash::Combine(
			hash, CanonicalFirstInstance(draw, usage));
		return hash;
	}

	static bool TryLoadDirectIaCandidate(
		DX12SimulateContextDrawTemplate *templateState,
		const DX12IaHashStateCacheKey &iaKey,
		const DX12IaDrawInvocation &draw,
		const DX12DrawMatchUsage &usage,
		bool *hasCandidate)
	{
		if (!templateState || !hasCandidate)
			return false;
		DX12SimulateContextDrawTemplate::DirectIaCandidateCache::Entries *entries =
			templateState->directIaCandidateCache.entries.get();
		if (!entries)
			return false;
		const UINT instanceCount = CanonicalInstanceCount(draw, usage);
		const UINT firstInstance = CanonicalFirstInstance(draw, usage);
		const size_t tag = DirectIaCandidateTag(iaKey, draw, usage);
		const size_t setBase =
			(tag & (DX12SimulateContextDrawTemplate::DirectIaCandidateSetCount - 1)) *
			DX12SimulateContextDrawTemplate::DirectIaCandidateWayCount;
		for (size_t way = 0;
			 way < DX12SimulateContextDrawTemplate::DirectIaCandidateWayCount;
			 ++way) {
			DX12SimulateContextDrawTemplate::DirectIaCandidateEntry &entry =
				(*entries)[setBase + way];
			if (!entry.valid || entry.tag != tag || !(entry.iaKey == iaKey) ||
				entry.instanceCount != instanceCount ||
				entry.firstInstance != firstInstance)
				continue;
			entry.age = ++templateState->directIaCandidateCache.clock;
			*hasCandidate = entry.hasCandidate;
			return true;
		}
		return false;
	}

	static bool StoreDirectIaCandidate(
		DX12SimulateContextDrawTemplate *templateState,
		const DX12IaHashStateCacheKey &iaKey,
		const DX12IaDrawInvocation &draw,
		const DX12DrawMatchUsage &usage,
		bool hasCandidate)
	{
		if (!templateState)
			return false;
		DX12SimulateContextDrawTemplate::DirectIaCandidateCache::Entries *entries =
			templateState->directIaCandidateCache.EnsureEntries();
		if (!entries)
			return false;
		const UINT instanceCount = CanonicalInstanceCount(draw, usage);
		const UINT firstInstance = CanonicalFirstInstance(draw, usage);
		const size_t tag = DirectIaCandidateTag(iaKey, draw, usage);
		const size_t setBase =
			(tag & (DX12SimulateContextDrawTemplate::DirectIaCandidateSetCount - 1)) *
			DX12SimulateContextDrawTemplate::DirectIaCandidateWayCount;
		DX12SimulateContextDrawTemplate::DirectIaCandidateEntry *target = nullptr;
		for (size_t way = 0;
			 way < DX12SimulateContextDrawTemplate::DirectIaCandidateWayCount;
			 ++way) {
			DX12SimulateContextDrawTemplate::DirectIaCandidateEntry &entry =
				(*entries)[setBase + way];
			if (entry.valid && entry.tag == tag && entry.iaKey == iaKey &&
				entry.instanceCount == instanceCount &&
				entry.firstInstance == firstInstance) {
				target = &entry;
				break;
			}
			if (!target || !entry.valid || entry.age < target->age)
				target = &entry;
			if (!entry.valid)
				break;
		}
		if (!target)
			return false;
		DX12SimulateContextDrawTemplate::DirectIaCandidateEntry &entry = *target;
		const bool evicted = entry.valid &&
			(!(entry.iaKey == iaKey) ||
			 entry.instanceCount != instanceCount ||
			 entry.firstInstance != firstInstance);
		entry.valid = true;
		entry.hasCandidate = hasCandidate;
		entry.age = ++templateState->directIaCandidateCache.clock;
		entry.tag = tag;
		entry.iaKey = iaKey;
		entry.instanceCount = instanceCount;
		entry.firstInstance = firstInstance;
		return evicted;
	}

	static size_t ResidentNegativeSlot(const DX12IaDrawInvocation &draw)
	{
		UINT64 key = draw.indexed ? 0x9e3779b97f4a7c15ull : 0xc4ceb9fe1a85ec53ull;
		key = DX12IaHashStateCacheKeyHash::Combine(key, draw.vertexCount);
		key = DX12IaHashStateCacheKeyHash::Combine(key, draw.indexCount);
		key = DX12IaHashStateCacheKeyHash::Combine(key, draw.instanceCount);
		key = DX12IaHashStateCacheKeyHash::Combine(key, draw.firstVertex);
		key = DX12IaHashStateCacheKeyHash::Combine(key, draw.firstIndex);
		key = DX12IaHashStateCacheKeyHash::Combine(
			key, static_cast<UINT64>(static_cast<INT64>(draw.baseVertex)));
		key = DX12IaHashStateCacheKeyHash::Combine(key, draw.firstInstance);
		return static_cast<size_t>(key &
			(DX12SimulateContextDrawTemplate::ResidentDrawNegativeEntryCount - 1));
	}

	static UINT CanonicalVertexCount(
		const DX12IaDrawInvocation &draw,
		const DX12DrawMatchUsage &usage)
	{
		return usage.useVertexCount ? draw.vertexCount : 0;
	}

	static UINT CanonicalIndexCount(
		const DX12IaDrawInvocation &draw,
		const DX12DrawMatchUsage &usage)
	{
		return usage.useIndexCount ? draw.indexCount : 0;
	}

	static UINT CanonicalInstanceCount(
		const DX12IaDrawInvocation &draw,
		const DX12DrawMatchUsage &usage)
	{
		return usage.useInstanceCount ? draw.instanceCount : 0;
	}

	static UINT CanonicalFirstVertex(
		const DX12IaDrawInvocation &draw,
		const DX12DrawMatchUsage &usage)
	{
		return usage.useFirstVertex ? draw.firstVertex : 0;
	}

	static UINT CanonicalFirstIndex(
		const DX12IaDrawInvocation &draw,
		const DX12DrawMatchUsage &usage)
	{
		return usage.useFirstIndex ? draw.firstIndex : 0;
	}

	static UINT CanonicalFirstInstance(
		const DX12IaDrawInvocation &draw,
		const DX12DrawMatchUsage &usage)
	{
		return usage.useFirstInstance ? draw.firstInstance : 0;
	}

	static bool MatchesResidentNegative(
		const DX12ResidentDrawNegativeEntry &entry,
		const DX12SimulateContext &state,
		const DX12CompiledDrawDecision &decision,
		UINT64 preSkinGeneration,
		UINT64 contentGeneration,
		UINT64 resolveGeneration,
		UINT64 graphicsDescriptorQuerySerial,
		ID3D12DescriptorHeap *graphicsDescriptorHeap,
		UINT64 graphicsDescriptorHeapVersion,
		const DX12IaDrawInvocation &draw)
	{
		const DX12DrawMatchUsage &usage = decision.directTargetDrawMatchUsage;
		return entry.valid && entry.pipelineState == state.pipelineState &&
			entry.reloadGeneration == decision.reloadGeneration &&
			entry.preSkinGeneration == preSkinGeneration &&
			entry.contentGeneration == contentGeneration &&
			entry.resolveGeneration == resolveGeneration &&
			entry.iaEpoch == state.iaEpoch &&
			entry.graphicsDescriptorQuerySerial == graphicsDescriptorQuerySerial &&
			entry.graphicsDescriptorHeap == graphicsDescriptorHeap &&
			entry.graphicsDescriptorHeapVersion == graphicsDescriptorHeapVersion &&
			entry.targetPlanFingerprint == decision.directTargetPlan.Fingerprint() &&
			entry.indexed == draw.indexed &&
			entry.vertexCount == CanonicalVertexCount(draw, usage) &&
			entry.indexCount == CanonicalIndexCount(draw, usage) &&
			entry.instanceCount == CanonicalInstanceCount(draw, usage) &&
			entry.firstVertex == CanonicalFirstVertex(draw, usage) &&
			entry.firstIndex == CanonicalFirstIndex(draw, usage) &&
			entry.baseVertex == draw.baseVertex &&
			entry.firstInstance == CanonicalFirstInstance(draw, usage);
	}

	static bool TryFindResidentNegative(
		DX12SimulateContext *state,
		const DX12CompiledDrawDecision &decision,
		UINT64 preSkinGeneration,
		UINT64 contentGeneration,
		UINT64 resolveGeneration,
		UINT64 graphicsDescriptorQuerySerial,
		ID3D12DescriptorHeap *graphicsDescriptorHeap,
		UINT64 graphicsDescriptorHeapVersion,
		const DX12IaDrawInvocation &draw)
	{
		if (!state)
			return false;
		const DX12ResidentDrawNegativeEntry &entry =
			state->resolvedDraw.residentDrawNegativeCache[ResidentNegativeSlot(draw)];
		return MatchesResidentNegative(
			entry, *state, decision, preSkinGeneration,
			contentGeneration, resolveGeneration,
			graphicsDescriptorQuerySerial, graphicsDescriptorHeap,
			graphicsDescriptorHeapVersion, draw);
	}

	static void StoreResidentNegative(
		DX12SimulateContext *state,
		const DX12CompiledDrawDecision &decision,
		UINT64 preSkinGeneration,
		UINT64 contentGeneration,
		UINT64 resolveGeneration,
		UINT64 graphicsDescriptorQuerySerial,
		ID3D12DescriptorHeap *graphicsDescriptorHeap,
		UINT64 graphicsDescriptorHeapVersion,
		const DX12IaDrawInvocation &draw)
	{
		DX12ResidentDrawNegativeEntry &entry =
			state->resolvedDraw.residentDrawNegativeCache[ResidentNegativeSlot(draw)];
		const DX12DrawMatchUsage &usage = decision.directTargetDrawMatchUsage;
		entry.pipelineState = state->pipelineState;
		entry.reloadGeneration = decision.reloadGeneration;
		entry.preSkinGeneration = preSkinGeneration;
		entry.contentGeneration = contentGeneration;
		entry.resolveGeneration = resolveGeneration;
		entry.iaEpoch = state->iaEpoch;
		entry.graphicsDescriptorQuerySerial = graphicsDescriptorQuerySerial;
		entry.graphicsDescriptorHeap = graphicsDescriptorHeap;
		entry.graphicsDescriptorHeapVersion = graphicsDescriptorHeapVersion;
		entry.targetPlanFingerprint = decision.directTargetPlan.Fingerprint();
		entry.vertexCount = CanonicalVertexCount(draw, usage);
		entry.indexCount = CanonicalIndexCount(draw, usage);
		entry.instanceCount = CanonicalInstanceCount(draw, usage);
		entry.firstVertex = CanonicalFirstVertex(draw, usage);
		entry.firstIndex = CanonicalFirstIndex(draw, usage);
		entry.baseVertex = draw.baseVertex;
		entry.firstInstance = CanonicalFirstInstance(draw, usage);
		entry.indexed = draw.indexed;
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
		UINT64 preSkinGeneration,
		const DX12IaHashTargetPlan &targetPlan,
		const DX12DrawMatchUsage &usage,
		UINT64 iaFingerprint,
		uint32_t fingerprintKind,
		const DX12IaDrawInvocation &draw)
	{
		DX12PsoDrawFastRejectKey key;
		key.pipelineState = pipelineState;
		key.reloadGeneration = reloadGeneration;
		key.preSkinGeneration = preSkinGeneration;
		key.targetPlanFingerprint = targetPlan.Fingerprint();
		key.iaFingerprint = iaFingerprint;
		key.fingerprintKind = fingerprintKind;
		key.indexed = draw.indexed;
		key.vertexCount = CanonicalVertexCount(draw, usage);
		key.indexCount = CanonicalIndexCount(draw, usage);
		key.instanceCount = CanonicalInstanceCount(draw, usage);
		key.firstVertex = CanonicalFirstVertex(draw, usage);
		key.firstIndex = CanonicalFirstIndex(draw, usage);
		key.baseVertex = draw.baseVertex;
		key.firstInstance = CanonicalFirstInstance(draw, usage);
		return key;
	}

	static DX12PsoDrawFastRejectKey MakePsoIaNegativeKey(
		ID3D12PipelineState *pipelineState,
		UINT64 reloadGeneration,
		UINT64 preSkinGeneration,
		const DX12IaHashTargetPlan &targetPlan,
		const DX12DrawMatchUsage &usage,
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
			pipelineState, reloadGeneration, preSkinGeneration,
			targetPlan, usage, fingerprint, 1, draw);
	}

	static bool TryFindPsoDrawFastReject(const DX12PsoDrawFastRejectKey &key)
	{
		const PsoDrawFastRejectThreadCache::Entry &entry =
			s_psoDrawFastRejectCache.entries[
				PsoDrawFastRejectThreadCache::Slot(key)];
		return entry.valid && entry.key == key;
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

	static bool HasAnyIaTextureOverrideCheckTarget(
		const DX12ActiveIaState &ia,
		const DX12IaHashTargetPlan &targetPlan,
		bool indexed)
	{
		if (!targetPlan.hasTextureOverrideChecks ||
			!targetPlan.HasIaTargets()) {
			return false;
		}
		if (targetPlan.requireAllIaBuffers)
			return true;
		if (targetPlan.RequiresIndexBuffer(indexed) && ia.hasIndexBuffer)
			return true;
		for (UINT slot = 0; slot < ARRAYSIZE(ia.vertexBuffers); ++slot) {
			if (targetPlan.RequiresVertexBuffer(slot) && ia.hasVertexBuffer[slot])
				return true;
		}
		return false;
	}

	static bool HasCachedIaTextureOverrideCheckTarget(
		DX12SimulateContext *runtimeState,
		const DX12IaHashTargetPlan &targetPlan,
		bool indexed,
		bool directCheck)
	{
		if (!runtimeState)
			return false;
		DX12SimulateContextDrawTemplate::IaTargetPresenceEntry *entry =
			directCheck ?
				&runtimeState->resolvedDraw.directIaTargetPresence :
				&runtimeState->resolvedDraw.fullIaTargetPresence;
		const UINT64 fingerprint = targetPlan.Fingerprint();
		if (entry->valid &&
			entry->iaEpoch == runtimeState->iaEpoch &&
			entry->targetPlanFingerprint == fingerprint &&
			entry->indexed == indexed) {
			return entry->hasTarget;
		}

		const bool hasTarget = directCheck ?
			HasAnyDirectTextureOverrideCheckTarget(
				runtimeState->ia, targetPlan, indexed) :
			HasAnyIaTextureOverrideCheckTarget(
				runtimeState->ia, targetPlan, indexed);
		entry->valid = true;
		entry->hasTarget = hasTarget;
		entry->iaEpoch = runtimeState->iaEpoch;
		entry->targetPlanFingerprint = fingerprint;
		entry->indexed = indexed;
		return hasTarget;
	}

	static void StorePsoDrawFastReject(const DX12PsoDrawFastRejectKey &key)
	{
		PsoDrawFastRejectThreadCache::Entry &entry =
			s_psoDrawFastRejectCache.entries[
				PsoDrawFastRejectThreadCache::Slot(key)];
		entry.key = key;
		entry.valid = true;
	}



	static void ClearPsoDrawFastRejectCache()
	{
		s_psoDrawFastRejectCache.entries = {};
	}

	static bool ExecutePresent(
		ID3D12GraphicsCommandList *commandList,
		const DX12SimulateContext &runtimeState,
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

	static void ExecutePendingPresentBeforeFastReject(
		ID3D12GraphicsCommandList *commandList,
		const DX12SimulateContext *runtimeState,
		const DX12IaReplacementExecutorCallbacks &callbacks)
	{
		if (!commandList || !runtimeState ||
			!(DX12HotPathState::HookWorkFlags &
				DX12HotPathState::FLAG_PRESENT_REPLACEMENT) ||
			!DX12ModPresentReplacementPendingForCurrentPresent())
			return;

		// Present replacement is frame-scoped and must be recorded before a
		// resident draw can be fast-forwarded. Do only this small one-time
		// operation here; the draw itself can still use the resident fast path.
		ExecutePresent(commandList, *runtimeState, callbacks);
	}

	static bool ApplyAndExecute(
		ID3D12GraphicsCommandList *commandList,
		const DX12IaDrawInvocation &draw,
		const DX12IaHashState &iaState,
		DX12ModIaReplacement *replacement,
		const DX12SimulateContext &runtimeState,
		const DX12IaReplacementExecutorCallbacks &callbacks)
	{
		if (!commandList || !replacement)
			return false;
		if (replacement->skip && replacement->draws.empty() &&
		    replacement->dispatches.empty() && replacement->drawSrvPatches.empty()) {
			return true;
		}

		const bool modifiesIaBindings = replacement->hasIndexBuffer ||
			!replacement->vertexBuffers.empty();
		const bool hasDrawSrvPatches = !replacement->drawSrvPatches.empty();
		const DX12ActiveIaState *restoreIa = &runtimeState.ia;
		DX12ActiveIaState redirectedIa;
		if (DX12ModHasPreSkinOutputResizeBindings()) {
			redirectedIa = runtimeState.ia;
			DX12PreSkinIaRedirectUtils::ReplayActiveVertexBuffers(
				commandList, callbacks, &redirectedIa);
			restoreIa = &redirectedIa;
		}
		DX12IaReplacementDrawExecutor drawExecutor(commandList, callbacks);
		if (modifiesIaBindings) {
			DX12IaReplacementBindingExecutor bindingExecutor(commandList, callbacks);
			bindingExecutor.Apply(*replacement);
		}
		if (hasDrawSrvPatches)
			DX12ModApplyDrawSrvReplacement(commandList, replacement, !replacement->skip);

		if (!replacement->skip)
			drawExecutor.ExecuteOriginalDraw(draw);

		drawExecutor.ExecuteReplacementDraws(*replacement, draw);
		if (modifiesIaBindings) {
			DX12IaReplacementBindingExecutor bindingExecutor(commandList, callbacks);
			bindingExecutor.Restore(*replacement, *restoreIa);
		}
		if (hasDrawSrvPatches)
			DX12ModRestoreDrawSrvReplacement(commandList, replacement);
		DX12Profiling::RecordIaApplied();
		DX12ModRunPostShaderOverrideReplacement(
			commandList, runtimeState.pipelineState, iaState,
			draw.vertexCount, draw.indexCount, draw.instanceCount,
			draw.firstVertex, draw.firstIndex, draw.firstInstance, replacement);
		return true;
	}

	static bool ApplyStaticTemplateAndExecute(
		ID3D12GraphicsCommandList *commandList,
		const DX12IaDrawInvocation &draw,
		const DX12ModIaReplacement &replacement,
		const DX12SimulateContext &runtimeState,
		const DX12IaReplacementExecutorCallbacks &callbacks)
	{
		if (!commandList)
			return false;
		// Static templates are compiled only when they contain no descriptor
		// patches or post actions, so execution cannot mutate the snapshot.
		if (replacement.skip && replacement.draws.empty() &&
			replacement.dispatches.empty()) {
			return true;
		}

		const bool modifiesIaBindings = replacement.hasIndexBuffer ||
			!replacement.vertexBuffers.empty();
		const DX12ActiveIaState *restoreIa = &runtimeState.ia;
		DX12ActiveIaState redirectedIa;
		if (DX12ModHasPreSkinOutputResizeBindings()) {
			redirectedIa = runtimeState.ia;
			DX12PreSkinIaRedirectUtils::ReplayActiveVertexBuffers(
				commandList, callbacks, &redirectedIa);
			restoreIa = &redirectedIa;
		}
		DX12IaReplacementDrawExecutor drawExecutor(commandList, callbacks);
		if (modifiesIaBindings) {
			DX12IaReplacementBindingExecutor bindingExecutor(commandList, callbacks);
			bindingExecutor.Apply(replacement);
		}
		if (!replacement.skip)
			drawExecutor.ExecuteOriginalDraw(draw);
		drawExecutor.ExecuteReplacementDraws(replacement, draw);
		if (modifiesIaBindings) {
			DX12IaReplacementBindingExecutor bindingExecutor(commandList, callbacks);
			bindingExecutor.Restore(replacement, *restoreIa);
		}
		DX12Profiling::RecordIaApplied();
		return true;
	}

	static bool HandleDrawOverrides(
		ID3D12GraphicsCommandList *commandList,
		const DX12IaDrawInvocation &draw,
		const DX12SimulateContext &runtimeState,
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
			DX12SimulateContext *mutableRuntimeState =
				const_cast<DX12SimulateContext*>(&runtimeState);
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
			if (drawDecision->staticReplacementReady &&
			    drawDecision->staticReplacementSnapshot) {
				DX12OuterTimingProbeUtils::LogCpuStage(
					"IaReplacement",
					"static_pso_replacement_hit",
				draw.indexed ? 1u : 0u,
				1,
				DX12OuterTimingProbeUtils::ElapsedUs(psoDecisionBegin));
			DX12_HOTPATH_TIME_END(DrawPsoEarlyExit);
			DX12Profiling::RecordIaDrawOverrideCheck();
			DX12_HOTPATH_TIME_BEGIN(DrawApplyAndExecute);
			LARGE_INTEGER applyBegin = DX12OuterTimingProbeUtils::Now();
			if (DX12IaReplacementExecutor::ApplyStaticTemplateAndExecute(
					commandList,
					draw,
					*drawDecision->staticReplacementSnapshot,
					runtimeState,
					callbacks)) {
				DX12OuterTimingProbeUtils::LogCpuStage(
					"IaReplacement",
					"apply_static_pso_replacement",
					draw.indexed ? 1u : 0u,
					1,
					DX12OuterTimingProbeUtils::ElapsedUs(applyBegin));
				DX12_HOTPATH_TIME_END(DrawApplyAndExecute);
				DX12_HOTPATH_TIME_END(DrawOverrideTotal);
				return true;
			}
			DX12OuterTimingProbeUtils::LogCpuStage(
				"IaReplacement",
				"apply_static_pso_replacement_failed",
				draw.indexed ? 1u : 0u,
				1,
				DX12OuterTimingProbeUtils::ElapsedUs(applyBegin));
			DX12_HOTPATH_TIME_END(DrawApplyAndExecute);
		}
		if (drawDecision->NeedsDescriptorMatch())
			DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionDescriptorDraw();
		DX12_HOTPATH_TIME_END(DrawPsoEarlyExit);

		// Bound-state draw-result cache: when PSO, IA state, root descriptors,
		// and descriptor heap haven't changed since the last negative result on
		// this command list, the override decision must be identical. This skips
		// the entire multi-layer dynamic chain (IA hash, descriptor prefilter,
		// resource hash lookup, candidate matching). Only negative results are
		// cached in this phase; positive hits still execute the full path.
		const UINT64 preSkinGeneration = DX12ModGetPreSkinActiveGenerationFast();
		const UINT64 boundHeapVersion = runtimeState.cbvSrvUavHeap ?
			DX12GetDescriptorHeapVersion(runtimeState.cbvSrvUavHeap) : 0;
		DX12BoundDrawResultCache &boundDrawCache =
			mutableRuntimeState->resolvedDraw.boundDrawResult;
		if (boundDrawCache.Matches(
				runtimeState.iaEpoch,
				runtimeState.iaContentEpoch,
				runtimeState.graphicsDescriptorQuerySerial,
				runtimeState.pipelineState,
				drawDecision->reloadGeneration,
				preSkinGeneration,
				runtimeState.cbvSrvUavHeap,
				boundHeapVersion,
				runtimeState.graphicsRootSignature)) {
			if (!boundDrawCache.hasOverride) {
				DX12OuterTimingProbeUtils::LogCpuStage(
					"IaReplacement",
					"bound_draw_cache_hit_negative",
					draw.indexed ? 1u : 0u,
					1,
					0);
				DX12_HOTPATH_TIME_END(DrawOverrideTotal);
				return false;
			}
		}
		auto StoreBoundDrawNegative = [&]() {
			boundDrawCache.Capture(
				runtimeState.iaEpoch,
				runtimeState.iaContentEpoch,
				runtimeState.graphicsDescriptorQuerySerial,
				runtimeState.pipelineState,
				drawDecision->reloadGeneration,
				preSkinGeneration,
				runtimeState.cbvSrvUavHeap,
				boundHeapVersion,
				runtimeState.graphicsRootSignature,
				false);
		};

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
		const DX12IaHashTargetPlan &targetPlan = drawDecision->targetPlan;
		const DX12IaHashTargetPlan &directTargetPlan = drawDecision->directTargetPlan;
		const bool targetPlanHasIaTargets = drawDecision->targetPlanHasIaTargets;
		const bool psoIaNegativeCacheSafe = drawDecision->negativeCacheSafe;
		const bool shaderOverrideNegativeCacheAllowed =
			drawDecision->shaderOverrideNegativeCacheAllowed;
		// Match DX11's "current state changed, re-evaluate now" behavior:
		// match_cs / active pre-skin can flip without IA mutations, so draw-level
		// negative caches must incorporate that generation explicitly
		// (preSkinGeneration captured above for the bound-draw cache).
		bool hasIaHashState = false;
		DX12PsoDrawFastRejectKey psoPreFilterKey;
		DX12IaHashState directIaState;
		const DX12IaHashState *directIaStateView = &directIaState;
		bool reusedDirectIaState = false;
		DX12DirectIaFastRejectCacheKey directFastRejectKey;
		bool directFastRejectKeyValid = false;
		bool directDescriptorCandidateKnown = false;
		bool hasDirectDescriptorCandidate = false;
		UINT64 directGraphicsDescriptorQuerySerial = 0;
		ID3D12DescriptorHeap *directGraphicsDescriptorHeap = nullptr;
		UINT64 directGraphicsDescriptorHeapVersion = 0;
		UINT64 directDescriptorContentGeneration = 0;
		UINT64 directDescriptorResolveGeneration = 0;
		if (drawDecision->directTargetPlanResolved &&
		    directTargetPlan.hasTextureOverrideChecks &&
		    !DX12IaReplacementExecutor::HasCachedIaTextureOverrideCheckTarget(
			    mutableRuntimeState, directTargetPlan, draw.indexed, true)) {
			DX12OuterTimingProbeUtils::LogCpuStage(
				"IaReplacement",
				"skip_missing_direct_check_target",
				draw.indexed ? 1u : 0u,
				1,
				0);
			DX12Profiling::RecordIaHashStateResult(false);
			StoreBoundDrawNegative();
			DX12_HOTPATH_TIME_END(DrawOverrideTotal);
			return false;
		}
		if (drawDecision->directTextureChecksOnly &&
		    drawDecision->directTargetPlanResolved &&
		    directTargetPlan.hasDescriptorTargets &&
		    !directTargetPlan.descriptorTargets.empty() &&
		    !directTargetPlan.requireAllIaBuffers) {
			directGraphicsDescriptorQuerySerial =
				runtimeState.graphicsDescriptorQuerySerial ?
					runtimeState.graphicsDescriptorQuerySerial : 1;
			directGraphicsDescriptorHeap = runtimeState.cbvSrvUavHeap;
			directGraphicsDescriptorHeapVersion = DX12GetDescriptorHeapVersion(
				runtimeState.cbvSrvUavHeap);
			if (directTargetPlan.HasIaTargets()) {
				directDescriptorContentGeneration = DX12GetIaHashContentGeneration();
				directDescriptorResolveGeneration = DX12GetIaHashResolveGeneration();
				if (TryFindResidentNegative(
						mutableRuntimeState,
						*drawDecision,
						preSkinGeneration,
						directDescriptorContentGeneration,
						directDescriptorResolveGeneration,
						directGraphicsDescriptorQuerySerial,
						directGraphicsDescriptorHeap,
						directGraphicsDescriptorHeapVersion,
						draw)) {
					DX12Profiling::RecordIaHashStateResult(false);
					StoreBoundDrawNegative();
					DX12_HOTPATH_TIME_END(DrawOverrideTotal);
					return false;
				}
			}
			LARGE_INTEGER directDescriptorBegin =
				DX12OuterTimingProbeUtils::Now();
			hasDirectDescriptorCandidate =
				DX12ModHasAnyDirectDescriptorTextureOverrideCandidate(
					commandList,
					directTargetPlan,
					draw.vertexCount,
					draw.indexCount,
					draw.instanceCount,
					draw.firstVertex,
					draw.firstIndex,
					draw.firstInstance);
			directDescriptorCandidateKnown = true;
			DX12OuterTimingProbeUtils::LogCpuStage(
				"IaReplacement",
				hasDirectDescriptorCandidate ?
					"direct_descriptor_prefilter_positive" :
					"direct_descriptor_prefilter_negative",
				draw.indexed ? 1u : 0u,
				1,
				DX12OuterTimingProbeUtils::ElapsedUs(directDescriptorBegin));
			if (!directTargetPlan.HasIaTargets() &&
			    !hasDirectDescriptorCandidate) {
				StoreResidentNegative(
					mutableRuntimeState,
					*drawDecision,
					preSkinGeneration,
					0,
					0,
					directGraphicsDescriptorQuerySerial,
					directGraphicsDescriptorHeap,
					directGraphicsDescriptorHeapVersion,
					draw);
				DX12Profiling::RecordIaHashStateResult(false);
				StoreBoundDrawNegative();
				DX12_HOTPATH_TIME_END(DrawOverrideTotal);
				return false;
			}
		}
		if (drawDecision->directTextureChecksOnly &&
		    drawDecision->directTargetPlanResolved &&
		    directTargetPlan.hasTextureOverrideChecks &&
		    directTargetPlan.HasIaTargets() &&
		    !directTargetPlan.hasDescriptorTargets &&
		    !directTargetPlan.requireAllIaBuffers) {
				const UINT64 contentGeneration = DX12GetIaHashContentGeneration();
				const UINT64 resolveGeneration = DX12GetIaHashResolveGeneration();
				if (TryFindResidentNegative(mutableRuntimeState,
						*drawDecision, preSkinGeneration,
						contentGeneration, resolveGeneration,
						0, nullptr, 0, draw)) {
					DX12Profiling::RecordIaHashStateResult(false);
					StoreBoundDrawNegative();
					DX12_HOTPATH_TIME_END(DrawOverrideTotal);
					return false;
				}
				directFastRejectKey = DX12DirectIaFastRejectCacheUtils::MakeKey(
					drawDecision->reloadGeneration,
					preSkinGeneration,
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
				StoreBoundDrawNegative();
				DX12_HOTPATH_TIME_END(DrawOverrideTotal);
				return false;
			}

			LARGE_INTEGER directPrefilterBegin =
				DX12OuterTimingProbeUtils::Now();
			const bool hasPotentialDirectCandidate =
				DX12ModHasAnyPotentialDirectIaTextureOverrideCandidate(
					runtimeState.ia,
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
				hasPotentialDirectCandidate ?
					"direct_ia_prefilter_positive" :
					"direct_ia_prefilter_negative",
				draw.indexed ? 1u : 0u,
				1,
				DX12OuterTimingProbeUtils::ElapsedUs(directPrefilterBegin));
			if (!hasPotentialDirectCandidate) {
				StoreResidentNegative(
					mutableRuntimeState,
					*drawDecision,
					preSkinGeneration,
					contentGeneration,
					resolveGeneration,
					0,
					nullptr,
					0,
					draw);
				if (directFastRejectKeyValid) {
					DX12DirectIaFastRejectCacheUtils::Store(
						&DX12DirectIaFastRejectCacheUtils::ThreadCache(),
						directFastRejectKey);
				}
				DX12Profiling::RecordIaHashStateResult(false);
				StoreBoundDrawNegative();
				DX12_HOTPATH_TIME_END(DrawOverrideTotal);
				return false;
			}

				// The direct probe and the full override path consume the same stable IA
				// hash state. Cache it before building the probe key so a negative probe
				// hit does not require re-hashing the same draw region first.
				LARGE_INTEGER directProbeBuildBegin = DX12OuterTimingProbeUtils::Now();
				DX12IaHashStateCacheKey directHashCacheKey =
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
				const bool residentDirectHashCacheHit = TryLoadResolvedIaHashBuild(
					&mutableRuntimeState->resolvedDraw.lastDirectIaHashBuild,
					directHashCacheKey,
					&directIaStateView,
					&directHashFound);
				bool directHashCacheHit = residentDirectHashCacheHit;
				if (!directHashCacheHit) {
					directHashCacheHit = DX12IaHashStateCacheUtils::TryLoad(
						iaHashStateCache,
						directHashCacheKey,
						&directIaStateView,
						&directHashFound);
				}
				if (!directHashCacheHit) {
					directHashFound = DX12CommandListRuntime::BuildIaHashStateForTargetsCached(
						&mutableRuntimeState->ia,
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
				} else if (!residentDirectHashCacheHit && directIaStateView) {
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

					DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionDirectProbeCacheLookup();
					LARGE_INTEGER directProbeMatchBegin = DX12OuterTimingProbeUtils::Now();
					bool hasDirectCandidate = false;
					const bool directCandidateCacheHit =
						TryLoadDirectIaCandidate(
							&mutableRuntimeState->resolvedDraw,
							directHashCacheKey,
							draw,
							drawDecision->directTargetDrawMatchUsage,
							&hasDirectCandidate);
					if (!directCandidateCacheHit) {
						hasDirectCandidate =
							DX12ModHasAnyDirectIaTextureOverrideCandidate(
								*directIaStateView,
								directTargetPlan,
								draw.indexed,
								draw.vertexCount,
								draw.indexCount,
								draw.instanceCount,
								draw.firstVertex,
								draw.firstIndex,
								draw.firstInstance);
						const bool directCandidateCacheEvicted = StoreDirectIaCandidate(
							&mutableRuntimeState->resolvedDraw,
							directHashCacheKey,
							draw,
							drawDecision->directTargetDrawMatchUsage,
							hasDirectCandidate);
						DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionDirectProbeCacheMiss();
						DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionDirectProbeCacheStore(
							directCandidateCacheEvicted);
					} else {
						DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionDirectProbeCacheHit(
							hasDirectCandidate);
					}
					DX12OuterTimingProbeUtils::LogCpuStage(
						"IaReplacement",
						directCandidateCacheHit ?
							(hasDirectCandidate ?
								"direct_ia_candidate_positive_hit" :
								"direct_ia_candidate_negative_hit") :
							(hasDirectCandidate ?
								"direct_ia_probe_positive_result" :
								"direct_ia_probe_negative_result"),
						draw.indexed ? 1u : 0u,
						1,
						DX12OuterTimingProbeUtils::ElapsedUs(directProbeMatchBegin));
					if (!hasDirectCandidate) {
						StoreResidentNegative(mutableRuntimeState, *drawDecision,
							preSkinGeneration,
							contentGeneration, resolveGeneration,
							0, nullptr, 0, draw);
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
						StoreBoundDrawNegative();
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
		if (drawDecision->directTextureChecksOnly &&
		    drawDecision->directTargetPlanResolved &&
		    directTargetPlan.hasTextureOverrideChecks &&
		    directTargetPlan.hasDescriptorTargets &&
		    directTargetPlan.HasIaTargets() &&
		    !directTargetPlan.requireAllIaBuffers &&
		    directDescriptorCandidateKnown &&
		    !hasDirectDescriptorCandidate) {
			LARGE_INTEGER mixedPrefilterBegin =
				DX12OuterTimingProbeUtils::Now();
			const bool hasPotentialDirectIaCandidate =
				DX12ModHasAnyPotentialDirectIaTextureOverrideCandidate(
					runtimeState.ia,
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
				hasPotentialDirectIaCandidate ?
					"mixed_direct_prefilter_ia_positive" :
					"mixed_direct_prefilter_ia_negative",
				draw.indexed ? 1u : 0u,
				1,
				DX12OuterTimingProbeUtils::ElapsedUs(mixedPrefilterBegin));
			if (!hasPotentialDirectIaCandidate) {
				StoreResidentNegative(
					mutableRuntimeState,
					*drawDecision,
					preSkinGeneration,
					directDescriptorContentGeneration,
					directDescriptorResolveGeneration,
					directGraphicsDescriptorQuerySerial,
					directGraphicsDescriptorHeap,
					directGraphicsDescriptorHeapVersion,
					draw);
				DX12Profiling::RecordIaHashStateResult(false);
				StoreBoundDrawNegative();
				DX12_HOTPATH_TIME_END(DrawOverrideTotal);
				return false;
			}
		}
		const bool hasCurrentIaTarget =
			!targetPlanHasIaTargets ||
			DX12IaReplacementExecutor::HasCachedIaTextureOverrideCheckTarget(
				mutableRuntimeState,
				targetPlan,
				draw.indexed,
				false);
		if (targetPlanHasIaTargets && !reusedDirectIaState && hasCurrentIaTarget) {
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
				preSkinGeneration,
				targetPlan,
				drawDecision->targetDrawMatchUsage,
				iaCacheKey.iaViewFingerprint,
				0,
				draw);
			if (DX12IaReplacementExecutor::TryFindPsoDrawFastReject(psoPreFilterKey)
				&& !drawDecision->targetPlanHasDescriptorTargets) {
				DX12OuterTimingProbeUtils::LogCpuStage(
					"IaReplacement",
					"pso_view_negative_hit",
					draw.indexed ? 1u : 0u,
					1,
					0);
				DX12Profiling::RecordIaHashStateResult(false);
				StoreBoundDrawNegative();
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
				DX12ResolvedDrawRuntimeStateUtils::NoteLookup(
					&mutableRuntimeState->resolvedDraw, true);
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
				DX12ResolvedDrawRuntimeStateUtils::NoteLookup(
					&mutableRuntimeState->resolvedDraw, false);
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
				hasIaHashState = DX12CommandListRuntime::BuildIaHashStateForTargetsCached(
					&mutableRuntimeState->ia, targetPlan, draw.indexed, draw.vertexCount, draw.indexCount,
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
				targetPlanHasIaTargets && reusedDirectIaState && hasCurrentIaTarget);
		}
		if (hasIaHashState || !targetPlanHasIaTargets ||
		    drawDecision->targetPlanHasDescriptorTargets) {
			const bool canUsePsoIaNegativeCache =
				psoIaNegativeCacheSafe && (!targetPlanHasIaTargets || hasIaHashState);
			const bool skipDirectDescriptorTargets =
				drawDecision->directTextureChecksOnly &&
				directDescriptorCandidateKnown &&
				!hasDirectDescriptorCandidate;
			DX12PsoDrawFastRejectKey psoIaNegKey;

			// PSO and IA negative cache.
			if (canUsePsoIaNegativeCache) {
				DX12_HOTPATH_TIME_BEGIN(DrawPsoIaNegCache);
				LARGE_INTEGER negKeyBegin = DX12OuterTimingProbeUtils::Now();
				psoIaNegKey = DX12IaReplacementExecutor::MakePsoIaNegativeKey(
					runtimeState.pipelineState,
					drawDecision->reloadGeneration,
					preSkinGeneration,
					targetPlan,
					drawDecision->targetDrawMatchUsage,
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
					StoreBoundDrawNegative();
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
			std::shared_ptr<const DX12ModIaReplacement> generatedStaticReplacement;
			const bool preparedReplacement = DX12ModPrepareShaderOverrideReplacement(
				commandList, runtimeState.pipelineState, activeShaderOverrides,
				*activeIaState,
				vertexCount, indexCount, draw.instanceCount,
				firstVertex, firstIndex, draw.firstInstance,
				drawDecision->targetDrawMatchUsage,
				&iaReplacement,
				shaderOverrideNegativeCacheAllowed,
				runtimeState.device,
				skipDirectDescriptorTargets,
				&generatedStaticReplacement);
			if (generatedStaticReplacement) {
				mutableRuntimeState->drawDecision.staticReplacementSnapshot =
					std::move(generatedStaticReplacement);
				mutableRuntimeState->drawDecision.staticReplacementReady = true;
			}
			if (preparedReplacement) {
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
			if (skipDirectDescriptorTargets) {
				StoreResidentNegative(
					mutableRuntimeState,
					*drawDecision,
					preSkinGeneration,
					directDescriptorContentGeneration,
					directDescriptorResolveGeneration,
					directGraphicsDescriptorQuerySerial,
					directGraphicsDescriptorHeap,
					directGraphicsDescriptorHeapVersion,
					draw);
			}
			StoreBoundDrawNegative();
			DX12_HOTPATH_TIME_END(DrawOverrideTotal);
			return false;
		}
		if (drawDecision->hasShaderOverrideSkip) {
			DX12_HOTPATH_TIME_END(DrawOverrideTotal);
			return true;
		}
		StoreBoundDrawNegative();
		DX12_HOTPATH_TIME_END(DrawOverrideTotal);
		return false;
	}
};



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
		const UINT64 flagsWord = DX12HotPathState::HookWorkFlags;
		if (!(flagsWord & DX12HotPathState::MASK_DRAW_WORK) &&
			!DX12Profiling::ShouldCollectCounters()) {
			DX12_PROFILE_FAST_FORWARD();
			DX12IaReplacementDrawExecutor(commandList, callbacks).ExecuteOriginalDraw(draw);
			return;
		}
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12DrawHookFlow::ExecuteHook(commandList, runtimeState, draw, callbacks);
	}

	static void ExecuteHook(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
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
		// draw-driven feature for this PSO, a state proven unable to match must
		// not enter the generic draw interpreter (counters, logging, IA keys, or
		// cache layers). Reload generation keeps the shortcut conservative across
		// hot reloads while still allowing global resident descriptor/IA features
		// to coexist with the fast reject.
		DX12IaReplacementExecutor::ExecutePendingPresentBeforeFastReject(
			commandList, runtimeState, callbacks);
		if (DX12HotPathState::CanResidentDrawFastReject(
				flagsWord, runtimeState)) {
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
		const bool shouldRecordBindingEvents =
			DX12HotPathState::NeedsBindingEventRecording(flagsWord) &&
			(runtimeState ?
				DX12CommandListRuntime::GetTrackingState(runtimeState).recordBindingEvents :
				DX12CommandListStateCapture::ShouldRecordBindingEvents());
		if (shouldRecordBindingEvents)
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
		const DX12SimulateContext *runtimeState,
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
