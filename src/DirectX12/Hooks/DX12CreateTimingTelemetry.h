#pragma once

// Create-API timing telemetry (timing builds) and DX12_CREATE_TIMING_SCOPE macro.
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

	// Per-thread accumulator tables, claimed once per calling thread and
	// drained once per present. The previous process-wide interlocked
	// accumulators serialized every scoped wrapper call from every thread on
	// shared cache lines (2-4 locked ops per scope). Same pool idiom as
	// DX12OuterTimingProbe's ApiWallThreadTable: plain per-thread adds on the
	// hot path, drain with InterlockedExchange64 at flush; a racing increment
	// can be attributed one frame late (diagnostics only). Threads beyond the
	// pool share the interlocked fallback accumulators above.
	static constexpr UINT kThreadTableCount = 64;
	struct alignas(64) ThreadTable
	{
		DWORD threadId = 0;
		LONG64 calls[static_cast<UINT>(Api::Count)][static_cast<UINT>(Stage::Count)] = {};
		LONG64 totalTicks[static_cast<UINT>(Api::Count)][static_cast<UINT>(Stage::Count)] = {};
		LONG64 maxTicks[static_cast<UINT>(Api::Count)][static_cast<UINT>(Stage::Count)] = {};
	};
	inline static ThreadTable ThreadTables[kThreadTableCount] = {};
	inline static volatile LONG ThreadTableCount = 0;

	static ThreadTable *AcquireThreadTable()
	{
		const LONG index = InterlockedIncrement(&ThreadTableCount) - 1;
		if (index < 0 || index >= static_cast<LONG>(kThreadTableCount))
			return nullptr;
		ThreadTables[index].threadId = GetCurrentThreadId();
		return &ThreadTables[index];
	}

	static ThreadTable *GetThreadTable()
	{
		static thread_local ThreadTable *table = AcquireThreadTable();
		return table;
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

	static void Record(Api api, Stage stage, LONG64 ticks)
	{
		if (ticks < 0)
			ticks = 0;
		const UINT apiIndex = static_cast<UINT>(api);
		const UINT stageIndex = static_cast<UINT>(stage);
		ThreadTable *table = GetThreadTable();
		if (table) {
			table->calls[apiIndex][stageIndex] += 1;
			table->totalTicks[apiIndex][stageIndex] += ticks;
			if (ticks > table->maxTicks[apiIndex][stageIndex])
				table->maxTicks[apiIndex][stageIndex] = ticks;
			return;
		}
		// Overflow threads share the interlocked fallback accumulators.
		Accumulator &accumulator = accumulators[apiIndex][stageIndex];
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
		const LONG tableCount = InterlockedCompareExchange(&ThreadTableCount, 0, 0);
		const LONG tablesToScan = tableCount < static_cast<LONG>(kThreadTableCount)
			? tableCount : static_cast<LONG>(kThreadTableCount);
		for (UINT apiIndex = 0; apiIndex < static_cast<UINT>(Api::Count); ++apiIndex) {
			for (UINT stageIndex = 0; stageIndex < static_cast<UINT>(Stage::Count); ++stageIndex) {
				LONG64 calls = 0;
				LONG64 totalTicks = 0;
				LONG64 maxTicks = 0;
				for (LONG t = 0; t < tablesToScan; ++t) {
					ThreadTable &table = ThreadTables[t];
					calls += InterlockedExchange64(&table.calls[apiIndex][stageIndex], 0);
					totalTicks += InterlockedExchange64(&table.totalTicks[apiIndex][stageIndex], 0);
					const LONG64 tableMaxTicks =
						InterlockedExchange64(&table.maxTicks[apiIndex][stageIndex], 0);
					if (tableMaxTicks > maxTicks)
						maxTicks = tableMaxTicks;
				}
				// Overflow threads share the interlocked fallback accumulators.
				Accumulator &accumulator = accumulators[apiIndex][stageIndex];
				calls += InterlockedExchange(&accumulator.calls, 0);
				totalTicks += InterlockedExchange64(&accumulator.totalTicks, 0);
				const LONG64 fallbackMaxTicks =
					InterlockedExchange64(&accumulator.maxTicks, 0);
				if (fallbackMaxTicks > maxTicks)
					maxTicks = fallbackMaxTicks;
				if (calls <= 0)
					continue;
				const LONG64 totalUs = totalTicks * 1000000 / frequency;
				const LONG64 maxUs = maxTicks * 1000000 / frequency;
				if (totalUs < 40 && maxUs < 10)
					continue;
				DX12LogManager::Fields("DX12CreateTiming",
					"\"present\":%ld,\"api\":\"%s\",\"stage\":\"%s\","
					"\"calls\":%lld,\"totalUs\":%lld,\"avgUs\":%lld,\"maxUs\":%lld",
					DX12RuntimeState::GetPresentCount(),
					ApiName(static_cast<Api>(apiIndex)),
					StageName(static_cast<Stage>(stageIndex)),
					static_cast<long long>(calls),
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
