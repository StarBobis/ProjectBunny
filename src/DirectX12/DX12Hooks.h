// ============================================================
// DX12Hooks.h - DX12 layered module header.
// Split from the merged DX12.h; keep declarations in the
// layer that owns them. Later layers include earlier ones.
// ============================================================
#pragma once

#include "DX12Core.h"

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

	static void BeginAt(DX12HotPathStage::Id stage, const LARGE_INTEGER &begin)
	{
		EnsureFreq();
		tlsBegin[stage] = begin;
	}

	static void EndAt(
		DX12HotPathStage::Id stage,
		LONGLONG beginQpc,
		LONGLONG endQpc)
	{
		LONG64 ticks = endQpc - beginQpc;
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

	static void Begin(DX12HotPathStage::Id stage)
	{
		LARGE_INTEGER now;
		QueryPerformanceCounter(&now);
		BeginAt(stage, now);
	}

	static void End(DX12HotPathStage::Id stage)
	{
		LARGE_INTEGER now;
		QueryPerformanceCounter(&now);
		EndAt(stage, tlsBegin[stage].QuadPart, now.QuadPart);
	}

	// Snapshot and reset at frame end.
	static void LogAndResetAll()
	{
		DX12CreateTimingTelemetryUtils::LogAndReset();
		DX12DescriptorShadowTelemetryUtils::LogAndReset();
		DX12ResidentMaintenanceTelemetryUtils::LogAndReset();
		LogFrameCpuDiagnostics();

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

	// Frame-level wall-clock vs render-thread CPU time. The hot-path stage
	// timers only account for instrumented wrapper work; when the frame wall
	// time is far larger than the thread CPU time, the thread is blocked
	// (fence wait, lock contention, game-side stall) and the missing time
	// must be hunted in the wait/lock path, not in the wrapper methods.
	static void LogFrameCpuDiagnostics()
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
		if (lastWall != 0) {
			const ULONGLONG wallDelta = wallNow - lastWall;
			const ULONGLONG cpuDelta = cpuNow - lastCpu;
			const UINT cpuPct = wallDelta
				? static_cast<UINT>((cpuDelta * 100) / (wallDelta * 1000))
				: 0;
			DX12LogManager::Fields("DX12FrameCpuDiag",
				"\"wallUs\":%llu,\"threadCpuUs\":%llu,\"threadCpuPct\":%u",
				wallDelta * 1000,
				cpuDelta,
				cpuPct);
		}
		lastWall = wallNow;
		lastCpu = cpuNow;
	}
};

#endif // DX12_HOTPATH_TIME_ENABLED (timing build)

#else // !(DX12_ENABLE_DETAILED_TIMING && (_DEBUG || DX12_RELEASE_TIMING))

#define DX12_HOTPATH_TIME_ENABLED 0

#endif


// ---------------------------------------------------------------------------
// DX12Profiler - single instrumentation engine.
//
// All three profiling styles (DX12Profiling counters, API wall buckets and
// hot-path stages) share one RAII scope and one per-frame flush. Existing
// macro families remain as thin aliases so call sites did not need to change;
// new code should use DX12_PROFILE_SCOPE / DX12_PROFILE_API / DX12_PROFILE_STAGE.
// ---------------------------------------------------------------------------
class DX12Profiler
{
public:
	enum Kind
	{
		Counter = 0,
		Api,
		Stage,
		ApiStage,   // one QPC pair feeding both the API bucket and a hot-path stage
	};

	class Scope
	{
	public:
		Kind kind;
		int apiBucket;
		int stage;
		DX12Profiling::Counter *counter;
		DX12Profiling::State state = {};
		LARGE_INTEGER begin = {};
		bool active;

		Scope(Kind kindValue, DX12Profiling::Counter *counterValue)
			: kind(kindValue), apiBucket(0), stage(0), counter(counterValue),
			  active(true)
		{
			DX12Profiling::Start(&state, counter);
		}

		Scope(Kind kindValue, int index)
			: kind(kindValue), apiBucket(index), stage(index), counter(nullptr),
			  active(true)
		{
			if (kind == Api) {
#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
				begin = DX12OuterTimingProbeUtils::Now();
				DX12OuterTimingProbeUtils::LogApiScopeBegin(apiBucket, begin.QuadPart);
#else
				active = false;
#endif
			} else if (kind == Stage) {
#if DX12_HOTPATH_TIME_ENABLED
				DX12HotPathTimingUtils::Begin(static_cast<DX12HotPathStage::Id>(stage));
#else
				active = false;
#endif
			} else {
				active = false;
			}
		}

		Scope(Kind kindValue, int apiValue, int stageValue)
			: kind(kindValue), apiBucket(apiValue), stage(stageValue),
			  counter(nullptr), active(true)
		{
#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
#if DX12_HOTPATH_TIME_ENABLED
			begin = DX12OuterTimingProbeUtils::Now();
			DX12OuterTimingProbeUtils::LogApiScopeBegin(apiBucket, begin.QuadPart);
			DX12HotPathTimingUtils::BeginAt(static_cast<DX12HotPathStage::Id>(stage), begin);
#else
			active = false;
#endif
#else
			active = false;
#endif
		}

		~Scope()
		{
			Finish();
		}

		void Pause()
		{
			if (!active || kind != Counter)
				return;
			DX12Profiling::End(&state);
			active = false;
		}

		void Resume()
		{
			if (active || kind != Counter)
				return;
			DX12Profiling::Start(&state, state.counter);
			active = true;
		}

		void Finish()
		{
			if (!active)
				return;
			switch (kind) {
			case Counter:
				DX12Profiling::End(&state);
				break;
			case Api:
#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
				{
					const LARGE_INTEGER end = DX12OuterTimingProbeUtils::Now();
					DX12OuterTimingProbeUtils::LogApiScopeEnd(
						apiBucket, begin.QuadPart, end.QuadPart);
				}
#endif
				break;
			case Stage:
#if DX12_HOTPATH_TIME_ENABLED
				DX12HotPathTimingUtils::End(static_cast<DX12HotPathStage::Id>(stage));
#endif
				break;
			case ApiStage:
#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
#if DX12_HOTPATH_TIME_ENABLED
				{
					const LARGE_INTEGER end = DX12OuterTimingProbeUtils::Now();
					DX12OuterTimingProbeUtils::LogApiScopeEnd(
						apiBucket, begin.QuadPart, end.QuadPart);
					DX12HotPathTimingUtils::EndAt(
						static_cast<DX12HotPathStage::Id>(stage),
						begin.QuadPart,
						end.QuadPart);
				}
#endif
#endif
				break;
			}
			active = false;
		}
	};

	static void BeginStage(int stage)
	{
#if DX12_HOTPATH_TIME_ENABLED
		DX12HotPathTimingUtils::Begin(static_cast<DX12HotPathStage::Id>(stage));
#else
		(void)stage;
#endif
	}

	static void EndStage(int stage)
	{
#if DX12_HOTPATH_TIME_ENABLED
		DX12HotPathTimingUtils::End(static_cast<DX12HotPathStage::Id>(stage));
#else
		(void)stage;
#endif
	}

	// Single per-frame flush: profiling counters, hot-path stages, create/
	// descriptor/resident telemetry and frame CPU diagnostics. API wall
	// buckets are flushed from LogPresentLeave via LogFrameTimeline.
	static void FlushFrame()
	{
		DX12Profiling::EndFrame();
#if DX12_HOTPATH_TIME_ENABLED
		DX12HotPathTimingUtils::LogAndResetAll();
#endif
	}
};

// Unified instrumentation macros for new code. Existing macro families
// (DX12_PROFILE_FUNCTION / DX12_HOTPATH_TIME_*) remain as compatibility
// aliases and all route through DX12Profiler above.
#define DX12_PROFILE_SCOPE(counter) \
	DX12Profiler::Scope _profile_scope( \
		DX12Profiler::Counter, &DX12Profiling::Get().counter)

#define DX12_PROFILE_API(api) \
	DX12Profiler::Scope _profile_api( \
		DX12Profiler::Api, static_cast<int>(api))

#define DX12_PROFILE_STAGE(stage) \
	DX12Profiler::Scope _profile_stage( \
		DX12Profiler::Stage, static_cast<int>(stage))

#if DX12_HOTPATH_TIME_ENABLED

#define DX12_HOTPATH_TIME_BEGIN(stage) \
	DX12Profiler::BeginStage(static_cast<int>(DX12HotPathStage::stage))

#define DX12_HOTPATH_TIME_END(stage) \
	DX12Profiler::EndStage(static_cast<int>(DX12HotPathStage::stage))

#define DX12_HOTPATH_TIME_SCOPE(stage) \
	DX12Profiler::Scope __hotpathTimer##__LINE__( \
		DX12Profiler::Stage, static_cast<int>(DX12HotPathStage::stage))

#define DX12_HOTPATH_LOG_AND_RESET() \
	DX12HotPathTimingUtils::LogAndResetAll()

#else

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
		// ExecuteHook resolves a decision before entering this function.  Keep the
		// hot path on the command-list-local immutable decision instead of falling
		// back to DX12ModShouldSkipPipelineState(), which takes gModLock again.
		if (hasShaderOverrides && dispatchDecision)
			shouldSkipDispatch = dispatchDecision->hasShaderOverrideSkip;
		if (shouldSkipDispatch)
			return;

		DX12DispatchHookFlowGuard guard(InDispatchHookFlow);

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
			effectiveState->resolvedCompute.probeBufferContentGeneration ==
				DX12GetIaHashContentGeneration() &&
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
		DX12LogManager::Fields("DX12DispatchExecDetail",
			"applied=%d,skip=%d,hasD=%d,gx=%u,gy=%u,gz=%u,origX=%u,origY=%u,origZ=%u,cs=%llx",
			probe.applied ? 1 : 0,
			probe.dispatchOverride.handlingSkip ? 1 : 0,
			probe.dispatchOverride.hasDispatch ? 1 : 0,
			probe.dispatchOverride.groupsX,
			probe.dispatchOverride.groupsY,
			probe.dispatchOverride.groupsZ,
			threadGroupCountX, threadGroupCountY, threadGroupCountZ,
			static_cast<unsigned long long>(probe.computeShaderHash));
		if (probe.applied && probe.dispatchOverride.handlingSkip) {
			if (probe.dispatchOverride.hasDispatch) {
				dispatchGroupX = probe.dispatchOverride.groupsX;
				dispatchGroupY = probe.dispatchOverride.groupsY;
				dispatchGroupZ = probe.dispatchOverride.groupsZ;
				// Publish the resize alias before the dispatch so draws
				// recorded on other lists still resolve the resized output.
				DX12ModPublishPreSkinOutputResizeEarly(commandList);
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
		if (probe.applied)
			DX12ModPublishPreSkinOutputResizeEarly(commandList);
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
			runtimeState->resolvedCompute.probeIaContentEpoch == runtimeState->iaContentEpoch &&
			runtimeState->resolvedCompute.probeBufferContentGeneration ==
				DX12GetIaHashContentGeneration()) {
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
			runtimeState->resolvedCompute.probeBufferContentGeneration ==
				DX12GetIaHashContentGeneration() &&
			runtimeState->resolvedCompute.bindingsValid &&
			!runtimeState->resolvedCompute.bindingsUseRootValues &&
				runtimeState->resolvedCompute.bindingsHeap == runtimeState->cbvSrvUavHeap &&
				runtimeState->resolvedCompute.bindingsRootSignature ==
					runtimeState->computeRootSignature &&
				runtimeState->resolvedCompute.bindingsDescriptorVersion ==
					runtimeState->computeDescriptorBindingVersion &&
				DX12BindingTracker::ValidateRuntimePreSkinBindingState(runtimeState)) {
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
			// DX12 command lists keep resident binding state. When the descriptor
			// version, heap/root signature, and every previously observed table
			// base are unchanged, the cached snapshot is already authoritative.
			// This avoids re-entering the generic DX11-style binding collector on
			// every Dispatch while preserving invalidation on descriptor writes or
			// descriptor-ring rotation.
			bool residentSnapshotLoaded = false;
			if (runtimeState &&
				runtimeState->resolvedCompute.bindingsValid &&
				!runtimeState->resolvedCompute.bindingsUseRootValues &&
				runtimeState->resolvedCompute.bindingsHeap ==
					runtimeState->cbvSrvUavHeap &&
				runtimeState->resolvedCompute.bindingsRootSignature ==
					runtimeState->computeRootSignature &&
				runtimeState->resolvedCompute.bindingsDescriptorVersion ==
					runtimeState->computeDescriptorBindingVersion) {
				residentSnapshotLoaded = true;
				for (const DX12SimulateContextComputeTemplate::RuntimeObservedSlot &observed :
					runtimeState->resolvedCompute.bindingsObservedSlots) {
					if (observed.root >= MaxRootParameters) {
						residentSnapshotLoaded = false;
						break;
					}
					const RootTableState &table =
						runtimeState->computeRootTables[observed.root];
					if (!table.valid ||
						table.baseDescriptor.ptr !=
							runtimeState->resolvedCompute.bindingsRootTableBases[observed.root]) {
						residentSnapshotLoaded = false;
						break;
					}
				}
			}
			if (residentSnapshotLoaded) {
				DX12SimulateContextComputeTemplate &cached =
					runtimeState->resolvedCompute;
				probe->cachedUavs = &cached.uavs;
				probe->cachedSrvs = &cached.srvs;
				probe->cachedCbvs = &cached.cbvs;
				probe->cachedRootConstants = &cached.rootConstants;
				probe->bindingDependencyFingerprint =
					cached.bindingsDependencyFingerprint;
				probe->uavsFound = cached.bindingsFoundAny;
				DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinPlanCacheLookup();
				DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinPlanCacheHit(false);
				DX12OuterTimingProbeUtils::LogCpuStage(
					"PreSkin", "resident_binding_snapshot_hit", 0,
					cached.uavs.size() + cached.srvs.size() + cached.cbvs.size(),
					DX12OuterTimingProbeUtils::ElapsedUs(collectBegin));
			}

			// Consume the validated recording-context template directly. The
			// collector remains the cold path that materializes a missing template.
			bool computeTemplateLoaded = residentSnapshotLoaded;
			if (!computeTemplateLoaded) {
				probe->cachedUavs = nullptr;
				probe->cachedSrvs = nullptr;
				probe->cachedCbvs = nullptr;
				probe->cachedRootConstants = nullptr;
				computeTemplateLoaded =
					DX12BindingTracker::GetSimulateContextPreSkinBindings(
						commandList,
						&probe->cachedUavs,
						&probe->cachedSrvs,
						&probe->cachedCbvs,
						&probe->cachedRootConstants,
						&probe->bindingDependencyFingerprint,
						&probe->uavsFound);
			}
			if (computeTemplateLoaded &&
				runtimeState &&
				runtimeState->resolvedCompute.probeResultValid &&
				!runtimeState->resolvedCompute.probeApplied &&
				runtimeState->resolvedCompute.probeCsHash == probe->computeShaderHash &&
				runtimeState->resolvedCompute.probeReloadGeneration == earlyReloadGeneration &&
				runtimeState->resolvedCompute.probeIaContentEpoch == runtimeState->iaContentEpoch &&
				runtimeState->resolvedCompute.probeBufferContentGeneration ==
					DX12GetIaHashContentGeneration() &&
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
				runtimeState->resolvedCompute.probeIaContentEpoch == runtimeState->iaContentEpoch &&
				runtimeState->resolvedCompute.probeBufferContentGeneration ==
					DX12GetIaHashContentGeneration()) {
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
				runtimeState->resolvedCompute.probeBufferContentGeneration =
					DX12GetIaHashContentGeneration();
				runtimeState->resolvedCompute.probeResultValid = true;
				runtimeState->resolvedCompute.probeApplied = applied;
				runtimeState->resolvedCompute.probeDispatchHandlingSkip =
					dispatchOverride.handlingSkip;
				runtimeState->resolvedCompute.probeDispatchHasDispatch =
					dispatchOverride.hasDispatch;
				runtimeState->resolvedCompute.probeGroupsX = dispatchOverride.groupsX;
				runtimeState->resolvedCompute.probeGroupsY = dispatchOverride.groupsY;
				runtimeState->resolvedCompute.probeGroupsZ = dispatchOverride.groupsZ;
				DX12SimulateContextComputeTemplate::LocalProbeCacheEntry
					&localEntry =
					runtimeState->resolvedCompute.localProbeCache[
						DX12SimulateContextComputeTemplate::LocalProbeCacheSlot(
							computeIaFingerprint, computeShaderHash)];
				localEntry.valid = true;
				localEntry.reloadGeneration = reloadGeneration;
				localEntry.csHash = computeShaderHash;
				localEntry.fingerprint = computeIaFingerprint;
				localEntry.bindingDependencyFingerprint =
					probe ? probe->bindingDependencyFingerprint : 0;
				localEntry.bufferContentGeneration =
					DX12GetIaHashContentGeneration();
				localEntry.iaContentEpoch = probeIaContentEpoch;
				localEntry.bindingSerial = probeBindingSerial;
				localEntry.applied = applied;
				localEntry.handlingSkip = dispatchOverride.handlingSkip;
				localEntry.hasDispatch = dispatchOverride.hasDispatch;
				localEntry.groupsX = dispatchOverride.groupsX;
				localEntry.groupsY = dispatchOverride.groupsY;
				localEntry.groupsZ = dispatchOverride.groupsZ;
			};
		bool runtimeProbeCacheHit = false;
		if (runtimeState &&
			runtimeState->resolvedCompute.probeResultValid &&
			runtimeState->resolvedCompute.probeFingerprint ==
				computeIaFingerprint &&
			runtimeState->resolvedCompute.probeCsHash == probe->computeShaderHash &&
			runtimeState->resolvedCompute.probeReloadGeneration == reloadGeneration &&
			runtimeState->resolvedCompute.probeBindingDependencyFingerprint ==
				(probe ? probe->bindingDependencyFingerprint : 0) &&
			runtimeState->resolvedCompute.probeBufferContentGeneration ==
				DX12GetIaHashContentGeneration() &&
			runtimeState->resolvedCompute.probeIaContentEpoch == probeIaContentEpoch &&
			runtimeState->resolvedCompute.probeBindingSerial == probeBindingSerial) {
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
		// Local lock-free multi-entry verdict cache: same semantic key as the
		// shared cache (command list + CS hash + stable probe fingerprint +
		// reload generation) but stored in the command-list runtime state that
		// survives Reset, so repeated dispatches across frames skip the global
		// mod lock + map lookup entirely.
		bool localProbeCacheHit = false;
		if (runtimeState) {
			DX12SimulateContextComputeTemplate::LocalProbeCacheEntry &localEntry =
				runtimeState->resolvedCompute.localProbeCache[
					DX12SimulateContextComputeTemplate::LocalProbeCacheSlot(
						computeIaFingerprint, probe->computeShaderHash)];
			if (localEntry.valid &&
			    localEntry.reloadGeneration == reloadGeneration &&
			    localEntry.csHash == probe->computeShaderHash &&
			    localEntry.fingerprint == computeIaFingerprint &&
			    localEntry.bindingDependencyFingerprint ==
				    (probe ? probe->bindingDependencyFingerprint : 0) &&
			    localEntry.bufferContentGeneration ==
				    DX12GetIaHashContentGeneration() &&
			    localEntry.iaContentEpoch == probeIaContentEpoch &&
			    localEntry.bindingSerial == probeBindingSerial) {
				cacheEntry.applied = localEntry.applied;
				cacheEntry.dispatchOverride.handlingSkip =
					localEntry.handlingSkip;
				cacheEntry.dispatchOverride.hasDispatch =
					localEntry.hasDispatch;
				cacheEntry.dispatchOverride.groupsX = localEntry.groupsX;
				cacheEntry.dispatchOverride.groupsY = localEntry.groupsY;
				cacheEntry.dispatchOverride.groupsZ = localEntry.groupsZ;
				localProbeCacheHit = true;
			}
		}
		if (runtimeProbeCacheHit || localProbeCacheHit ||
		    DX12ModGetCachedPreSkinProbeResult(
			    commandList, probe->computeShaderHash, computeIaFingerprint,
			    probe ? probe->bindingDependencyFingerprint : 0, &cacheEntry)) {
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
			computeIaFingerprint,
			probe ? probe->bindingDependencyFingerprint : 0);
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
				probe ? probe->bindingDependencyFingerprint : 0,
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
			} else if (draw.autoCount) {
				// `drawindexed = auto`: recompute the index count from the
				// replaced index buffer view, mirroring DX11's
				// get_index_count_from_current_ib (ByteWidth - offset) / 2|4.
				if (!draw.indexed || !replacement.hasIndexBuffer) {
					continue;
				}
				const UINT elementSize =
					(replacement.indexBuffer.Format == DXGI_FORMAT_R16_UINT) ? 2u : 4u;
				count = replacement.indexBuffer.SizeInBytes / elementSize;
				start = 0;
				baseVertex = 0;
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
		// Resource-private expansion metadata is the durable proof here. The
		// current active-producer table is frame-local and may already have been
		// cleared by Present while this consumer list is still being recorded.
		if (!DX12ModNeedsPreSkinningUavProbe())
			return false;

		DX12BufferResourceSummary resource;
		if (!DX12ResolveBufferResourceByGpuVa(
				sourceView.BufferLocation, 1, &resource) ||
			!resource.hasResourceDesc ||
			resource.resourceDesc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER ||
			resource.gpuVirtualAddress > sourceView.BufferLocation)
			return false;

		const UINT64 offset = sourceView.BufferLocation - resource.gpuVirtualAddress;
		UINT64 outputByteWidth = 0;
		DX12ModPreSkinProducerSummary producer;
		if (DX12ModFindActivePreSkinProducerForOutput(
				resource.resource, 0, sourceView.StrideInBytes, &producer)) {
			outputByteWidth = producer.outputByteWidth;
		}
		// The active-producer table is only a fast path. It is keyed by Mod
		// section, so a later frame can replace its output resource while an
		// earlier frame's command list is still being recorded. DLSS Frame
		// Generation and NVIDIA capture make that normal by increasing the number
		// of in-flight resources. Recover the VLR size from resource-local metadata
		// written when its UAV was expanded, matching DX11's buffer-local lifetime.
		if (!outputByteWidth) {
			DX12ModResolveExpandedPreSkinBufferWidth(
				resource.resource, sourceView.StrideInBytes, &outputByteWidth);
		}
		if (!outputByteWidth ||
			outputByteWidth > resource.resourceDesc.Width ||
			outputByteWidth <= offset)
			return false;

		const UINT expandedBytes = ClampViewBytes(outputByteWidth - offset);
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
				    commandList, sourceView.BufferLocation, &binding)) {
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
		LARGE_INTEGER replaySegmentBegin = DX12OuterTimingProbeUtils::Now();
		LONG64 replayLookupUs = 0;
		LONG64 replaySetUs = 0;
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
			replaySegmentBegin = DX12OuterTimingProbeUtils::Now();
			if (!RedirectVertexBufferViews(
				    commandList,
				    runStart,
				    runCount,
				    activeIa->vertexBuffers + runStart,
				    redirected,
				    D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT,
				    activeIa))
				continue;
			replayLookupUs += DX12OuterTimingProbeUtils::ElapsedUs(replaySegmentBegin);
			replaySegmentBegin = DX12OuterTimingProbeUtils::Now();
			for (UINT i = 0; i < runCount; ++i)
				activeIa->vertexBuffers[runStart + i] = redirected[i];
			DX12CommandListRuntime::RememberVertexBuffers(
				commandList, runStart, runCount, redirected);
			callbacks.setVertexBuffers(
				commandList, runStart, runCount, redirected);
			replaySetUs += DX12OuterTimingProbeUtils::ElapsedUs(replaySegmentBegin);
			replayedAny = true;
		}
		DX12OuterTimingProbeUtils::LogCpuStage(
			"IaReplacement", "apply_replay_lookup", 1, 1, replayLookupUs);
		DX12OuterTimingProbeUtils::LogCpuStage(
			"IaReplacement", "apply_replay_setvb", 1, 1, replaySetUs);
		return replayedAny;
	}

	// Plan fix F: pre-skin IA replay gate. ReplayActiveVertexBuffers (GPUVA
	// lookups + IASet) only needs to run once per output-resize/active
	// generation per command list; further matching draws reuse the already
	// replayed views. The IA view fingerprint guards against game rebinds
	// between replays (rebinding identical views keeps the same fingerprint,
	// while iaEpoch bumped on every rebind and defeated the gate). The gate
	// state lives on the real runtimeState->ia (not the per-call redirectedIa
	// copy the replay itself works on).
	static UINT64 ComputeIaViewFingerprint(const DX12ActiveIaState &ia)
	{
		UINT64 fp = 0x84222325a9582d75ull;
		auto mix = [&fp](UINT64 value) {
			fp ^= value + 0x9e3779b97f4a7c15ull + (fp << 6) + (fp >> 2);
		};
		mix(ia.hasIndexBuffer ? 0x7f4a7c15ull : 0x51afd7edull);
		if (ia.hasIndexBuffer) {
			mix(ia.indexBuffer.BufferLocation);
			mix(ia.indexBuffer.SizeInBytes);
			mix(ia.indexBuffer.Format);
		}
		for (UINT slot = 0; slot < ARRAYSIZE(ia.vertexBuffers); ++slot) {
			if (!ia.hasVertexBuffer[slot])
				continue;
			mix(static_cast<UINT64>(slot + 1));
			mix(ia.vertexBuffers[slot].BufferLocation);
			mix(ia.vertexBuffers[slot].SizeInBytes);
			mix(ia.vertexBuffers[slot].StrideInBytes);
		}
		return fp ? fp : 1;
	}

	static bool ReplayActiveVertexBuffersGated(
		ID3D12GraphicsCommandList *commandList,
		const DX12IaReplacementExecutorCallbacks &callbacks,
		DX12ActiveIaState *redirectedIa,
		const DX12SimulateContext *runtimeState)
	{
		if (!runtimeState)
			return false;
		DX12SimulateContext *mutableState =
			const_cast<DX12SimulateContext*>(runtimeState);
		const UINT64 replayGeneration = DX12ModGetPreSkinOutputResizeGeneration() ^
			(DX12ModGetPreSkinActiveGenerationFast() * 0x9e3779b97f4a7c15ull);
		DX12ActiveIaState &liveIa = mutableState->ia;
		const UINT64 iaFingerprint = ComputeIaViewFingerprint(liveIa);
		if (liveIa.preSkinReplayApplied &&
		    liveIa.preSkinReplayGeneration == replayGeneration &&
		    liveIa.preSkinReplayIaFingerprint == iaFingerprint)
			return false;
		const bool replayed = ReplayActiveVertexBuffers(
			commandList, callbacks, redirectedIa);
		liveIa.preSkinReplayApplied = true;
		liveIa.preSkinReplayGeneration = replayGeneration;
		// Replay updates liveIa to the redirected views, so the stored
		// fingerprint must reflect the post-replay state or the gate never
		// hits on the next matching draw.
		liveIa.preSkinReplayIaFingerprint =
			ComputeIaViewFingerprint(liveIa);
		return replayed;
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
		// Present clears the frame-local resize publish map, so its fast flag
		// cannot decide whether an already-bound IA view needs recovery. Retry for
		// configured PreSkin draws; durable resource metadata and the fence-owned
		// resize cache make the lookup cheap and independent of recording order.
		LARGE_INTEGER applySegmentBegin = DX12OuterTimingProbeUtils::Now();
		if (DX12ModHasPreSkinOutputResizeBindings() ||
			DX12ModHasPreSkinOutputResizeCacheEntries() ||
			DX12ModMayNeedPreSkinIaViewExpansion(
				runtimeState.ia.vertexBuffers,
				runtimeState.ia.hasVertexBuffer,
				D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT)) {
			redirectedIa = runtimeState.ia;
			DX12PreSkinIaRedirectUtils::ReplayActiveVertexBuffersGated(
				commandList, callbacks, &redirectedIa, &runtimeState);
			restoreIa = &redirectedIa;
		}
		DX12OuterTimingProbeUtils::LogCpuStage(
			"IaReplacement", "apply_replay_ia", draw.indexed ? 1u : 0u, 1,
			DX12OuterTimingProbeUtils::ElapsedUs(applySegmentBegin));
		DX12IaReplacementDrawExecutor drawExecutor(commandList, callbacks);
		applySegmentBegin = DX12OuterTimingProbeUtils::Now();
		if (modifiesIaBindings) {
			DX12IaReplacementBindingExecutor bindingExecutor(commandList, callbacks);
			bindingExecutor.Apply(*replacement);
		}
		DX12OuterTimingProbeUtils::LogCpuStage(
			"IaReplacement", "apply_ia_bindings", draw.indexed ? 1u : 0u, 1,
			DX12OuterTimingProbeUtils::ElapsedUs(applySegmentBegin));
		applySegmentBegin = DX12OuterTimingProbeUtils::Now();
		if (hasDrawSrvPatches)
			DX12ModApplyDrawSrvReplacement(commandList, replacement, !replacement->skip);
		DX12OuterTimingProbeUtils::LogCpuStage(
			"IaReplacement", "apply_draw_srv", draw.indexed ? 1u : 0u, 1,
			DX12OuterTimingProbeUtils::ElapsedUs(applySegmentBegin));

		applySegmentBegin = DX12OuterTimingProbeUtils::Now();
		if (!replacement->skip)
			drawExecutor.ExecuteOriginalDraw(draw);

		drawExecutor.ExecuteReplacementDraws(*replacement, draw);
		DX12OuterTimingProbeUtils::LogCpuStage(
			"IaReplacement", "apply_execute_draws", draw.indexed ? 1u : 0u, 1,
			DX12OuterTimingProbeUtils::ElapsedUs(applySegmentBegin));
		applySegmentBegin = DX12OuterTimingProbeUtils::Now();
		if (modifiesIaBindings) {
			DX12IaReplacementBindingExecutor bindingExecutor(commandList, callbacks);
			bindingExecutor.Restore(*replacement, *restoreIa);
		}
		if (hasDrawSrvPatches)
			DX12ModRestoreDrawSrvReplacement(commandList, replacement);
		DX12OuterTimingProbeUtils::LogCpuStage(
			"IaReplacement", "apply_restore", draw.indexed ? 1u : 0u, 1,
			DX12OuterTimingProbeUtils::ElapsedUs(applySegmentBegin));
		DX12Profiling::RecordIaApplied();
		applySegmentBegin = DX12OuterTimingProbeUtils::Now();
		DX12ModRunPostShaderOverrideReplacement(
			commandList, runtimeState.pipelineState, iaState,
			draw.vertexCount, draw.indexCount, draw.instanceCount,
			draw.firstVertex, draw.firstIndex, draw.firstInstance, replacement);
		DX12OuterTimingProbeUtils::LogCpuStage(
			"IaReplacement", "apply_post_override", draw.indexed ? 1u : 0u, 1,
			DX12OuterTimingProbeUtils::ElapsedUs(applySegmentBegin));
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
		// Keep this gate aligned with the dynamic Execute path above. The resize
		// publication flag is frame-local and is deliberately false after Present;
		// configured PreSkin work must still replay IA so metadata/cache fallback
		// can recover a consumer recorded on the other side of that boundary.
		if (DX12ModHasPreSkinOutputResizeBindings() ||
			DX12ModHasPreSkinOutputResizeCacheEntries() ||
			DX12ModMayNeedPreSkinIaViewExpansion(
				runtimeState.ia.vertexBuffers,
				runtimeState.ia.hasVertexBuffer,
				D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT)) {
			redirectedIa = runtimeState.ia;
			DX12PreSkinIaRedirectUtils::ReplayActiveVertexBuffersGated(
				commandList, callbacks, &redirectedIa, &runtimeState);
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

	// Plan fix A: apply a static template's IA replacement once and keep it
	// live until the PSO changes or the game explicitly rebinds IA, instead of
	// applying + restoring on every draw. The saved pre-apply IA state is
	// snapshotted here so RestoreStaticTemplateIa can put back the exact
	// original views. Skip / replacement-draw execution still happens on every
	// draw (draw arguments are per-draw), but the binding churn - and the
	// iaEpoch bumps that defeated every IA-keyed negative cache - is gone.
	static bool ApplyStaticTemplateLazy(
		ID3D12GraphicsCommandList *commandList,
		const DX12IaDrawInvocation &draw,
		const std::shared_ptr<const DX12ModIaReplacement> &replacementSnapshot,
		DX12SimulateContext *runtimeState,
		const DX12IaReplacementExecutorCallbacks &callbacks)
	{
		if (!commandList || !runtimeState || !replacementSnapshot)
			return false;
		const DX12ModIaReplacement &replacement = *replacementSnapshot;
		if (replacement.skip && replacement.draws.empty() &&
			replacement.dispatches.empty()) {
			return true;
		}

		const bool modifiesIaBindings = replacement.hasIndexBuffer ||
			!replacement.vertexBuffers.empty();
		if (modifiesIaBindings && !runtimeState->staticTemplateIaApplied) {
			runtimeState->staticTemplateSavedIa = runtimeState->ia;
			// Share ownership with the caller's snapshot: the drawDecision
			// resets on every PSO transition and would otherwise destroy the
			// replacement while the deferred restore still references it.
			runtimeState->staticTemplateReplacement = replacementSnapshot;
			DX12IaReplacementBindingExecutor bindingExecutor(commandList, callbacks);
			bindingExecutor.Apply(replacement);
			runtimeState->staticTemplateIaApplied = true;
		}

		DX12IaReplacementDrawExecutor drawExecutor(commandList, callbacks);
		if (!replacement.skip)
			drawExecutor.ExecuteOriginalDraw(draw);
		drawExecutor.ExecuteReplacementDraws(replacement, draw);
		DX12Profiling::RecordIaApplied();
		return true;
	}

	// Plan fix A: deferred restore half of the lazy static template. Puts the
	// snapshotted original views back on the command list (shadow state and
	// driver) and clears the live-template marker. Called from the draw path
	// when the decision no longer carries the active template, and from the
	// IASet* wrappers when the game rebinds IA over a live replacement.
	static void RestoreStaticTemplateIa(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		const DX12IaReplacementExecutorCallbacks &callbacks)
	{
		if (!commandList || !runtimeState || !runtimeState->staticTemplateIaApplied)
			return;
		const DX12ModIaReplacement &replacement =
			*runtimeState->staticTemplateReplacement;
		const DX12ActiveIaState &savedIa = runtimeState->staticTemplateSavedIa;

		if (replacement.hasIndexBuffer && callbacks.setIndexBuffer) {
			const D3D12_INDEX_BUFFER_VIEW *originalView =
				savedIa.hasIndexBuffer ? &savedIa.indexBuffer : nullptr;
			DX12CommandListRuntime::RememberIndexBuffer(commandList, originalView);
			callbacks.setIndexBuffer(commandList, originalView);
		}
		if (!replacement.vertexBuffers.empty() && callbacks.setVertexBuffers) {
			D3D12_VERTEX_BUFFER_VIEW restore[32] = {};
			const UINT start = replacement.vertexBufferStartSlot;
			const UINT count = static_cast<UINT>(replacement.vertexBuffers.size());
			for (UINT i = 0; i < count && start + i < ARRAYSIZE(savedIa.vertexBuffers); ++i)
				restore[i] = savedIa.hasVertexBuffer[start + i] ?
					savedIa.vertexBuffers[start + i] : D3D12_VERTEX_BUFFER_VIEW();
			DX12CommandListRuntime::RememberVertexBuffers(commandList, start, count, restore);
			callbacks.setVertexBuffers(commandList, start, count, restore);
		}
		runtimeState->staticTemplateIaApplied = false;
		runtimeState->staticTemplateReplacement.reset();
		runtimeState->staticTemplateSavedIa = DX12ActiveIaState();
	}

	// Bind-time static IA replacement (plan step 3.2): staleness watchdog.
	// A bind redirected to a mod buffer must fall back to the original game
	// binding when (a) the replacement table was reloaded away, (b) the game
	// rebound a different view in the meantime (bookkeeping no longer matches
	// the live view), (c) the backing resource was released/recreated
	// (resolve epoch bump), or (d) the buffer content was rewritten
	// (per-resource content generation bump). The check costs two scalar
	// loads per draw when nothing is replaced.
	static void RestoreStaleStaticIaBindings(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		const DX12IaReplacementExecutorCallbacks &callbacks)
	{
		if (!commandList || !runtimeState)
			return;
		DX12ActiveIaState &ia = runtimeState->ia;
		if (!ia.indexBufferStaticReplaced && !ia.vertexBufferStaticReplacedMask)
			return;
		// Plan fix B: the per-slot deep check below reads the per-resource
		// content generation under a lock for every redirected slot. Buffer
		// content changes are what can make a redirect stale, and every such
		// change bumps the global content generation, so a fast global
		// comparison gates the deep check: it only runs on the first draw
		// after any buffer content change / resolve epoch bump on this command
		// list instead of on every draw.
		const UINT64 globalContentGeneration = DX12GetIaHashContentGeneration();
		const UINT64 resolveEpoch = DX12GetIaHashResolveGeneration();
		if (ia.staticReplacedLastCheckedContentGeneration == globalContentGeneration &&
		    ia.staticReplacedLastCheckedResolveEpoch == resolveEpoch)
			return;
		ia.staticReplacedLastCheckedContentGeneration = globalContentGeneration;
		ia.staticReplacedLastCheckedResolveEpoch = resolveEpoch;
		// Evaluated only when this command list actually carries a redirected
		// binding, so the volatile gate read stays off the common draw path.
		const bool tableEnabled = DX12HasStaticIaReplacements();
		bool restoredAny = false;

		if (ia.indexBufferStaticReplaced) {
			bool stale = !tableEnabled;
			bool bookkeepingMatchesLiveView =
				ia.hasIndexBuffer &&
				ia.indexBuffer.BufferLocation ==
					ia.indexBufferStaticReplacedView.BufferLocation &&
				ia.indexBuffer.SizeInBytes ==
					ia.indexBufferStaticReplacedView.SizeInBytes &&
				ia.indexBuffer.Format ==
					ia.indexBufferStaticReplacedView.Format;
			if (!bookkeepingMatchesLiveView) {
				// The game rebound something else after our redirect (or the
				// binding was restored/cleared by another path). Nothing to
				// restore; just drop the stale bookkeeping.
				ia.indexBufferStaticReplaced = false;
			} else {
				const bool indexIdentityAvailable =
					ia.indexBufferStaticOriginalResolved &&
					ia.indexBufferStaticOriginalResource.resource &&
					ia.indexBufferStaticOriginalResource.resourceGeneration != 0;
				if (!stale && indexIdentityAvailable) {
					DX12BufferResourceSummary currentOriginal = {};
					if (!DX12ResolveBufferResourceByGpuVa(
							ia.indexBufferStaticOriginal.BufferLocation,
							ia.indexBufferStaticOriginal.SizeInBytes,
							&currentOriginal) ||
					    currentOriginal.resource !=
						    ia.indexBufferStaticOriginalResource.resource ||
					    currentOriginal.resourceGeneration !=
						    ia.indexBufferStaticOriginalResource.resourceGeneration) {
						stale = true;
					}
				} else if (!stale &&
				    ia.indexBufferStaticResolveEpoch != resolveEpoch) {
					stale = true;
				}
				if (!stale && ia.indexBufferStaticOriginalResolved &&
				    ia.indexBufferStaticOriginalResource.resource) {
					const UINT64 currentContentGeneration =
						DX12GetBufferContentGenerationForResource(
							ia.indexBufferStaticOriginalResource.resource);
					if (currentContentGeneration !=
					    ia.indexBufferStaticContentGeneration)
						stale = true;
				}
				if (stale) {
					if (callbacks.setIndexBuffer) {
						callbacks.setIndexBuffer(
							commandList, &ia.indexBufferStaticOriginal);
					}
					ia.indexBuffer = ia.indexBufferStaticOriginal;
					ia.hasIndexBuffer = true;
					ia.indexBufferHashValid = false;
					ia.indexBufferResolved = false;
					ia.indexBufferResource = DX12BufferResourceSummary();
					ia.indexBufferStaticReplaced = false;
					restoredAny = true;
				}
			}
		}

		UINT32 pendingMask = ia.vertexBufferStaticReplacedMask;
		while (pendingMask) {
			DWORD lowestBit = 0;
			_BitScanForward(&lowestBit, pendingMask);
			const UINT slot = static_cast<UINT>(lowestBit);
			pendingMask &= pendingMask - 1;
			if (!ia.vertexBufferStaticReplaced[slot])
				continue;
			bool stale = !tableEnabled;
			const bool bookkeepingMatchesLiveView =
				ia.hasVertexBuffer[slot] &&
				ia.vertexBuffers[slot].BufferLocation ==
					ia.vertexBufferStaticReplacedView[slot].BufferLocation &&
				ia.vertexBuffers[slot].SizeInBytes ==
					ia.vertexBufferStaticReplacedView[slot].SizeInBytes &&
				ia.vertexBuffers[slot].StrideInBytes ==
					ia.vertexBufferStaticReplacedView[slot].StrideInBytes;
			if (!bookkeepingMatchesLiveView) {
				ia.vertexBufferStaticReplaced[slot] = false;
				ia.vertexBufferStaticReplacedMask &= ~(1u << slot);
				continue;
			}
			const bool vertexIdentityAvailable =
				ia.vertexBufferStaticOriginalResolved[slot] &&
				ia.vertexBufferStaticOriginalResources[slot].resource &&
				ia.vertexBufferStaticOriginalResources[slot].resourceGeneration != 0;
			if (!stale && vertexIdentityAvailable) {
				DX12BufferResourceSummary currentOriginal = {};
				if (!DX12ResolveBufferResourceByGpuVa(
						ia.vertexBufferStaticOriginal[slot].BufferLocation,
						ia.vertexBufferStaticOriginal[slot].SizeInBytes,
						&currentOriginal) ||
				    currentOriginal.resource !=
					    ia.vertexBufferStaticOriginalResources[slot].resource ||
				    currentOriginal.resourceGeneration !=
					    ia.vertexBufferStaticOriginalResources[slot].resourceGeneration) {
					stale = true;
				}
			} else if (!stale &&
			    ia.vertexBufferStaticResolveEpoch[slot] != resolveEpoch) {
				stale = true;
			}
			if (!stale && ia.vertexBufferStaticOriginalResolved[slot] &&
			    ia.vertexBufferStaticOriginalResources[slot].resource) {
				const UINT64 currentContentGeneration =
					DX12GetBufferContentGenerationForResource(
						ia.vertexBufferStaticOriginalResources[slot].resource);
				if (currentContentGeneration !=
				    ia.vertexBufferStaticContentGeneration[slot])
					stale = true;
			}
			if (!stale)
				continue;
			if (callbacks.setVertexBuffers) {
				callbacks.setVertexBuffers(
					commandList, slot, 1,
					&ia.vertexBufferStaticOriginal[slot]);
			}
			ia.vertexBuffers[slot] = ia.vertexBufferStaticOriginal[slot];
			ia.hasVertexBuffer[slot] = true;
			ia.vertexBufferHashValid[slot] = false;
			ia.vertexBufferResolved[slot] = false;
			ia.vertexBufferResources[slot] = DX12BufferResourceSummary();
			ia.vertexBufferStaticReplaced[slot] = false;
			ia.vertexBufferStaticReplacedMask &= ~(1u << slot);
			restoredAny = true;
		}

		if (restoredAny)
			DX12CommandListRuntime::BumpIaEpoch(runtimeState);
	}

	static bool HandleDrawOverrides(
		ID3D12GraphicsCommandList *commandList,
		const DX12IaDrawInvocation &draw,
		const DX12SimulateContext &runtimeState,
		const DX12IaReplacementExecutorCallbacks &callbacks,
		const DX12CompiledDrawDecision *precompiledDrawDecision = nullptr)
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
		// Bind-time static IA replacement (plan step 3.2): restore any stale
		// redirected bindings before the decision/cache layer reads IA state.
		RestoreStaleStaticIaBindings(
			commandList, mutableRuntimeState, callbacks);
		// SetPipelineState compiles the immutable PSO policy.  The outer draw
		// hook may already have consumed that resident decision to reject a
		// Noop PSO; carry it into the interpreter instead of resolving the same
		// policy a second time for this Draw.
		const bool precompiledDecisionStillCurrent =
			precompiledDrawDecision && precompiledDrawDecision->resolved &&
			precompiledDrawDecision->pipelineState == runtimeState.pipelineState &&
			precompiledDrawDecision->reloadGeneration ==
				DX12ModGetReloadGenerationFast() &&
			precompiledDrawDecision->MatchesGlobals(activeShaderOverrides);
		const DX12CompiledDrawDecision *drawDecision =
			precompiledDecisionStillCurrent ? precompiledDrawDecision :
			DX12CommandListRuntime::EnsureDrawDecision(
				mutableRuntimeState, activeShaderOverrides);
		DX12OuterTimingProbeUtils::LogCpuStage(
			"IaReplacement",
			"ensure_draw_decision",
			draw.indexed ? 1u : 0u,
			1,
			DX12OuterTimingProbeUtils::ElapsedUs(psoDecisionBegin));
		if (!drawDecision) {
			DX12_HOTPATH_TIME_END(DrawPsoEarlyExit);
				DX12_HOTPATH_TIME_END(DrawOverrideTotal);
				return false;
			}
			// Plan fix A: a live static-template IA replacement must be
			// restored before any draw whose decision does not carry the same
			// template (PSO switched away, or the template was regenerated by
			// a config reload). This is the deferred half of the old per-draw
			// apply+restore; it now runs once per PSO transition instead of on
			// every draw.
			if (mutableRuntimeState->staticTemplateIaApplied &&
			    (!drawDecision->staticReplacementReady ||
			     drawDecision->staticReplacementSnapshot.get() !=
				     mutableRuntimeState->staticTemplateReplacement.get())) {
				DX12IaReplacementExecutor::RestoreStaticTemplateIa(
					commandList, mutableRuntimeState, callbacks);
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
			bool applied = false;
			if (DX12ModNeedsPreSkinningUavProbe()) {
				// Pre-skin keeps per-draw apply+restore semantics: its VB
				// content is GPU-rewritten every frame, so a retained
				// replacement would go stale within the same frame.
				applied = DX12IaReplacementExecutor::ApplyStaticTemplateAndExecute(
						commandList,
						draw,
						*drawDecision->staticReplacementSnapshot,
						runtimeState,
						callbacks);
			} else {
				// Plan fix A: apply the template IA once and keep it applied;
				// the restore is deferred to PSO change / game rebind.
				applied = DX12IaReplacementExecutor::ApplyStaticTemplateLazy(
						commandList,
						draw,
						drawDecision->staticReplacementSnapshot,
						mutableRuntimeState,
						callbacks);
			}
			if (applied) {
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
		if (drawDecision->kind == DX12CompiledDrawDecisionKind::NoopNeverMatches) {
			// Plan step 3.3: the decision was compiled with no IA targets, no
			// descriptor targets and no skip, so no draw on this PSO can ever
			// match. Return before building any negative-cache key (bound-draw,
			// IA-state or fast-reject) or touching the pre-skin generation.
			DX12_HOTPATH_TIME_END(DrawPsoEarlyExit);
			DX12_HOTPATH_TIME_END(DrawOverrideTotal);
			return false;
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
		const UINT64 boundHeapVersion = runtimeState.cbvSrvUavHeapVersion;
		// A pure IA-content decision (IB/VB hash targets, no descriptor
		// targets, no draw-SRV patch, no pre-skin, no capture/hunt) does not
		// depend on the graphics descriptor query serial, heap identity,
		// heap version or root signature. Those change every frame in most
		// games, so comparing them here would force the bound-draw cache to
		// miss on every draw even though the IA state is unchanged - the
		// exact cost DX11 avoids by keying its region-hash caches on the
		// buffer, not the descriptor pipeline. Only compare descriptor state
		// when the compiled decision can actually observe it.
		const bool boundCacheCompareDescriptorState =
			drawDecision->targetPlanHasDescriptorTargets ||
			drawDecision->directTargetPlan.hasDescriptorTargets ||
			(DX12HotPathState::HookWorkFlags &
				(DX12HotPathState::FLAG_DRAW_SRV_SLOT_REPLACEMENT |
				 DX12HotPathState::FLAG_PRE_SKIN_PROBE |
				 DX12HotPathState::FLAG_PRE_SKIN_BINDING_TRACK |
				 DX12HotPathState::FLAG_HUNT_ENABLED |
				 DX12HotPathState::FLAG_FRAME_ANALYSIS_ACTIVE |
				 DX12HotPathState::FLAG_SHADER_DUMP_ACTIVE)) != 0;
		// Draw-argument comparison is only required when the negative result
		// can depend on draw context: count-sensitive prefilter tables, plans
		// with descriptor targets (their section filtering is count-based),
		// or shader overrides with draw-arg filters. For pure-IA plans
		// without count tables, "no override candidate for this bound state"
		// is a property of the bound views alone (DX11: hash saved at bind
		// time), so the negative cache can hit across different draw args.
		const bool boundCacheCompareDrawArgs =
			DX12ModIaPrefilterCountSensitive() ||
			drawDecision->targetPlanHasDescriptorTargets ||
			drawDecision->directTargetPlan.hasDescriptorTargets ||
			!drawDecision->shaderOverrideNegativeCacheAllowed;
		DX12BoundDrawResultCache &boundDrawCache =
			mutableRuntimeState->resolvedDraw.boundDrawResult;
		LARGE_INTEGER boundCacheBegin = DX12OuterTimingProbeUtils::Now();
		const bool boundCacheHit = boundDrawCache.Matches(
				runtimeState.iaEpoch,
				runtimeState.iaContentEpoch,
				DX12GetIaHashContentGeneration(),
				runtimeState.graphicsDescriptorQuerySerial,
				runtimeState.pipelineState,
				drawDecision->reloadGeneration,
				preSkinGeneration,
				runtimeState.cbvSrvUavHeap,
				boundHeapVersion,
				runtimeState.graphicsRootSignature,
				draw,
				boundCacheCompareDescriptorState,
				boundCacheCompareDrawArgs);
		DX12OuterTimingProbeUtils::LogCpuStage(
			"IaReplacement",
			boundCacheHit ? "bound_cache_compare_hit" : "bound_cache_compare_miss",
			draw.indexed ? 1u : 0u,
			1,
			DX12OuterTimingProbeUtils::ElapsedUs(boundCacheBegin));
		if (boundCacheHit) {
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
			LARGE_INTEGER storeBegin = DX12OuterTimingProbeUtils::Now();
			boundDrawCache.Capture(
				runtimeState.iaEpoch,
				runtimeState.iaContentEpoch,
				DX12GetIaHashContentGeneration(),
				runtimeState.graphicsDescriptorQuerySerial,
				runtimeState.pipelineState,
				drawDecision->reloadGeneration,
				preSkinGeneration,
				runtimeState.cbvSrvUavHeap,
				boundHeapVersion,
				runtimeState.graphicsRootSignature,
				draw,
				false);
			DX12OuterTimingProbeUtils::LogCpuStage(
				"IaReplacement",
				"store_bound_negative",
				draw.indexed ? 1u : 0u,
				1,
				DX12OuterTimingProbeUtils::ElapsedUs(storeBegin));
		};

		DX12Profiling::RecordIaDrawOverrideCheck();
		// DX12ModIaReplacement embeds CommandListBindingState (~14KB of
		// zero-initialized root/descriptor arrays), so it is deliberately
		// NOT constructed here: 99%+ of draws exit through the negative
		// caches below and never need it. It is declared right before the
		// prepare/apply block that actually uses it.
		DX12IaHashState iaState;
		const DX12IaHashState *activeIaState = &iaState;
		const uint32_t vertexCount = draw.indexed ? 0 : draw.vertexCount;
		const uint32_t indexCount = draw.indexed ? draw.indexCount : 0;
		// DX11 reports FirstVertex = BaseVertexLocation for indexed draws
		// (HackerContext::DrawContext), so match_first_vertex applies to the
		// base vertex there. Mirror that semantic instead of pinning it to 0.
		const uint32_t firstVertex = draw.indexed ?
			static_cast<uint32_t>(draw.baseVertex) : draw.firstVertex;
		const uint32_t firstIndex = draw.indexed ? draw.firstIndex : 0;

		// Cheap negative cache lookup uses PSO, IA view identity, and draw args
		// before falling back to stable IA content hashing.
		const DX12IaHashTargetPlan &targetPlan = drawDecision->targetPlan;
		const DX12IaHashTargetPlan &directTargetPlan = drawDecision->directTargetPlan;
		// IA-state-level negative cache: with count-independent prefilter
		// tables, "this IA state has no override candidate" is a property of
		// the bound buffer views alone (DX11: hash saved at bind time, map
		// lookup miss ends the draw). Check it before any per-draw key
		// construction; only draw-context filters (match_first_index etc.)
		// are draw-arg dependent, and those only matter once a candidate
		// exists, which is exactly the case this cache does not short-circuit.
		if (drawDecision->directTextureChecksOnly &&
		    drawDecision->directTargetPlanResolved &&
		    directTargetPlan.hasTextureOverrideChecks &&
		    directTargetPlan.HasIaTargets() &&
		    !directTargetPlan.hasDescriptorTargets &&
		    !DX12ModIaPrefilterCountSensitive()) {
			DX12SimulateContextDrawTemplate::IaStateNegativeCache &iaNeg =
				mutableRuntimeState->resolvedDraw.iaStateNegative;
			const UINT64 iaPlanFingerprint = directTargetPlan.Fingerprint();
			if (iaNeg.valid &&
			    iaNeg.iaEpoch == runtimeState.iaEpoch &&
			    iaNeg.iaContentEpoch == runtimeState.iaContentEpoch &&
			    iaNeg.bufferContentGeneration == DX12GetIaHashContentGeneration() &&
			    iaNeg.reloadGeneration == drawDecision->reloadGeneration &&
			    iaNeg.targetPlanFingerprint == iaPlanFingerprint &&
			    iaNeg.pipelineState == runtimeState.pipelineState) {
				if (iaNeg.noCandidate) {
					StoreBoundDrawNegative();
					DX12_HOTPATH_TIME_END(DrawOverrideTotal);
					return false;
				}
			} else {
				const bool noCandidate =
					DX12ModIaStateDefinitelyHasNoCandidate(
						runtimeState.ia, directTargetPlan, draw.indexed);
				iaNeg.valid = true;
				iaNeg.noCandidate = noCandidate;
				iaNeg.iaEpoch = runtimeState.iaEpoch;
				iaNeg.iaContentEpoch = runtimeState.iaContentEpoch;
				iaNeg.bufferContentGeneration = DX12GetIaHashContentGeneration();
				iaNeg.reloadGeneration = drawDecision->reloadGeneration;
				iaNeg.targetPlanFingerprint = iaPlanFingerprint;
				iaNeg.pipelineState = runtimeState.pipelineState;
				if (noCandidate) {
					StoreBoundDrawNegative();
					DX12_HOTPATH_TIME_END(DrawOverrideTotal);
					return false;
				}
			}
		}
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
			directGraphicsDescriptorHeapVersion =
				runtimeState.cbvSrvUavHeapVersion;
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
			if (drawDecision->descriptorTargetsStatic) {
				// Plan step 3.1: every descriptor texture target is an
				// unconditional static replacement already applied at
				// descriptor write time. Skip the draw-time SRV binding
				// resolution and hash candidate prefilter entirely.
				hasDirectDescriptorCandidate = true;
				directDescriptorCandidateKnown = true;
				DX12OuterTimingProbeUtils::LogCpuStage(
					"IaReplacement",
					"direct_descriptor_prefilter_static_skip",
					draw.indexed ? 1u : 0u,
					1,
					DX12OuterTimingProbeUtils::ElapsedUs(directDescriptorBegin));
			} else {
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
			}
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
		    !directTargetPlan.hasDescriptorTargets) {
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
			// Constructed only on the (rare) path that reaches shader
			// override evaluation: the ~14KB embedded binding state would
			// otherwise be zero-initialized on every draw.
			DX12ModIaReplacement iaReplacement;
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
		const bool frameCapturePendingOrActive =
			DX12FrameAnalysisState::IsCaptureRequested() ||
			DX12FrameAnalysisState::IsCapturing();
		const DX12CompiledDrawDecision *residentDrawDecision = nullptr;

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
		if (DX12Profiling::ShouldCollectCounters() || needsModWork)
			DX12RuntimeState::IncrementFrameDraws();

		if (DX12IaReplacementDrawScope::IsExecuting()) {
			DX12_PROFILE_FAST_FORWARD();
			DX12IaReplacementDrawExecutor(commandList, callbacks).ExecuteOriginalDraw(draw);
			DX12_HOTPATH_TIME_END(DrawHookTotal);
			return;
		}

		// A compiled NoopNeverMatches decision is a PSO-level proof that no
		// draw on this PSO can reach an override.  Keep that decision at the
		// SetPipelineState/draw boundary instead of paying the DX11-style
		// generic draw interpreter for every draw in the main scene.  Preserve
		// the two static-IA restore checks from HandleDrawOverrides before
		// forwarding: they are normally no-ops, but a PSO switch can require one
		// restore before this non-matching draw is recorded. Hunt/capture paths
		// retain their per-draw bookkeeping below.
		if (needsModWork) {
			DX12ResidentMaintenanceTelemetryUtils::RecordDrawNoopFastPathAttempt();
			if (huntEnabled) {
				DX12ResidentMaintenanceTelemetryUtils::RecordDrawNoopFastPathHuntBlocked();
			} else if (frameCapturePendingOrActive) {
				DX12ResidentMaintenanceTelemetryUtils::RecordDrawNoopFastPathCaptureBlocked();
			} else if (flagsWord & DX12HotPathState::FLAG_SHADER_DUMP_ACTIVE) {
				DX12ResidentMaintenanceTelemetryUtils::RecordDrawNoopFastPathShaderDumpBlocked();
			} else if (!runtimeState) {
				DX12ResidentMaintenanceTelemetryUtils::RecordDrawNoopFastPathNoRuntime();
			} else {
				const bool activeShaderOverrides =
					(flagsWord & DX12HotPathState::FLAG_SHADER_OVERRIDES) != 0;
				DX12IaReplacementExecutor::RestoreStaleStaticIaBindings(
					commandList, runtimeState, callbacks);
				const DX12CompiledDrawDecision *decision =
					DX12CommandListRuntime::EnsureDrawDecision(
						runtimeState, activeShaderOverrides);
				residentDrawDecision = decision;
				if (!decision) {
					DX12ResidentMaintenanceTelemetryUtils::RecordDrawNoopFastPathNoDecision();
				} else if (decision->kind == DX12CompiledDrawDecisionKind::NoopNeverMatches) {
					if (runtimeState->staticTemplateIaApplied) {
						DX12IaReplacementExecutor::RestoreStaticTemplateIa(
							commandList, runtimeState, callbacks);
					}
					DX12ResidentMaintenanceTelemetryUtils::RecordDrawNoopFastPathForwarded();
					DX12_PROFILE_FAST_FORWARD();
					DX12IaReplacementDrawExecutor(commandList, callbacks).ExecuteOriginalDraw(draw);
					DX12_HOTPATH_TIME_END(DrawHookTotal);
					return;
				} else {
					DX12ResidentMaintenanceTelemetryUtils::RecordDrawNoopFastPathNonNoopDecision();
				}
			}
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

		DX12DrawHookFlow::Execute(
			commandList, draw, runtimeState, callbacks, residentDrawDecision);
		DX12_HOTPATH_TIME_END(DrawHookTotal);
	}

	static void Execute(
		ID3D12GraphicsCommandList *commandList,
		const DX12IaDrawInvocation &draw,
		const DX12SimulateContext *runtimeState,
		const DX12IaReplacementExecutorCallbacks &callbacks,
		const DX12CompiledDrawDecision *residentDrawDecision = nullptr)
	{
		const UINT64 flagsWord = DX12HotPathState::HookWorkFlags;
		if (runtimeState &&
		    (flagsWord & DX12HotPathState::FLAG_PRESENT_REPLACEMENT))
			DX12IaReplacementExecutor::ExecutePresent(commandList, *runtimeState, callbacks);
		if (runtimeState &&
		    DX12IaReplacementExecutor::HandleDrawOverrides(
				commandList, draw, *runtimeState, callbacks, residentDrawDecision))
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


class DX12HookRegistry
{
public:
	SRWLOCK lock;
	CNktHookLib hookManager;
	std::unordered_set<void*> hookedFunctions;
	std::unordered_map<void*, void*> originalFunctions;
	volatile LONG hooksFinalized = 0;
	// Vtable method hooks are never installed in wrapper-only mode.  Keeping
	// the counter lets original-resolution cache raw vtable entries as positive
	// results (zero means no method code in d3d12.dll/dxgi.dll was patched).
	volatile LONG vtableHooksInstalled = 0;

	DX12HookRegistry()
	{
		InitializeSRWLock(&lock);
		// Install patches with all other threads suspended. Nektra's default
		// inline hook rewrites the target's entry bytes non-atomically; if the
		// game executes the function during the rewrite, the torn call can
		// corrupt a wait/sync/init sequence and leave the game in a broken
		// state (observed as permanent 100ms timeout polling, GPU pegged at
		// 99%, flicker) - randomly, depending on whether a thread happened to
		// be inside the patched function at install time. Suspending threads
		// during installation removes that race for every export hook.
		hookManager.SetSuspendThreadsWhileHooking(TRUE);
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
		DX12LogManager::Fields(name ? name : "HookInstall",
			"\"event\":\"HookInstall\",\"status\":\"%s\",\"target\":\"%p\",\"original\":\"%p\",\"result\":\"0x%lx\"",
			result == ERROR_SUCCESS ? "installed" : "failed",
			target, original ? *original : nullptr, result);
		return result;
	}

	static void FinalizeHooks()
	{
		InterlockedExchange(&Get().hooksFinalized, 1);
	}

	static bool AnyVtableHooksInstalled()
	{
		return InterlockedCompareExchange(&Get().vtableHooksInstalled, 0, 0) != 0;
	}

	static void *GetOriginalFunction(void *target)
	{
		if (!target)
			return nullptr;

		// Wrapper mode never patches vtables: an unhooked vtable entry IS
		// the original implementation, and the registry only ever contains
		// entries for actually patched targets. Skipping the lookup removes
		// a shared SRW lock acquire plus an unordered_map find from every
		// hot hook call (CopyDescriptors / Create*View run thousands of
		// times per frame).
		if (!AnyVtableHooksInstalled())
			return nullptr;

		DX12HookRegistry &state = Get();
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

};


class DX12HookManager
{
public:
	static DWORD InstallExportHook(HMODULE module, const char *exportName, void **original, void *hook)
	{
		if (!module || !exportName)
			return ERROR_PROC_NOT_FOUND;

		return DX12HookRegistry::HookFunction(original, GetProcAddress(module, exportName), hook, exportName);
	}
};


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
		static constexpr UINT CopyBufferRegion = 15;
		static constexpr UINT CopyTextureRegion = 16;
		static constexpr UINT CopyResource = 17;
		static constexpr UINT CopyTiles = 18;
		static constexpr UINT ResolveSubresource = 19;
		static constexpr UINT IaSetPrimitiveTopology = 20;
		static constexpr UINT RsSetViewports = 21;
		static constexpr UINT RsSetScissorRects = 22;
		static constexpr UINT OmSetBlendFactor = 23;
		static constexpr UINT OmSetStencilRef = 24;
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
		static constexpr UINT SoSetTargets = 45;
		static constexpr UINT OmSetRenderTargets = 46;
		static constexpr UINT ClearDepthStencilView = 47;
		static constexpr UINT ClearRenderTargetView = 48;
		static constexpr UINT ClearUnorderedAccessViewUint = 49;
		static constexpr UINT ClearUnorderedAccessViewFloat = 50;
		static constexpr UINT DiscardResource = 51;
		static constexpr UINT BeginQuery = 52;
		static constexpr UINT EndQuery = 53;
		static constexpr UINT ResolveQueryData = 54;
		static constexpr UINT SetPredication = 55;
		static constexpr UINT SetMarker = 56;
		static constexpr UINT BeginEvent = 57;
		static constexpr UINT EndEvent = 58;
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


class D3D12ProxyTargetHelper
{
public:
	static bool VerifyIntendedTarget(HINSTANCE module);
};


