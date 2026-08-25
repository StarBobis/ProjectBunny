#pragma once

// Resident-maintenance telemetry counters and reporting.
// Split out of Core/DX12CommandFlow.h; include that umbrella header from existing code.
#include "../DX12HotPath.h"

class DX12ResidentMaintenanceTelemetryUtils
{
public:
	enum class Counter
	{
		PsoCalls,
		PsoChanged,
		PsoNoChange,
		RuntimePsoCalls,
		RuntimePsoChanged,
		RuntimePsoNoChange,
		RuntimeLookupTlsHits,
		RuntimeLookupDirectHits,
		RuntimeLookupShardHits,
		RuntimeLookupMisses,

		GraphicsRootSignatureCalls,
		GraphicsRootSignatureChanged,
		GraphicsRootSignatureNoChange,
		ComputeRootSignatureCalls,
		ComputeRootSignatureChanged,
		ComputeRootSignatureNoChange,

		DescriptorHeapResolveCalls,
		DescriptorHeapResolveSlots,
		DescriptorHeapCalls,
		DescriptorHeapChanged,
		DescriptorHeapNoChange,
		DescriptorHeapInvalidatedGraphicsTables,
		DescriptorHeapInvalidatedComputeTables,

		GraphicsRootTableCalls,
		GraphicsRootTableTlsHits,
		GraphicsRootTableChanged,
		GraphicsRootTableNoChange,
		ComputeRootTableCalls,
		ComputeRootTableTlsHits,
		ComputeRootTableChanged,
		ComputeRootTableNoChange,

		GraphicsRootDescriptorCalls,
		GraphicsRootDescriptorTlsHits,
		GraphicsRootDescriptorChanged,
		GraphicsRootDescriptorNoChange,
		ComputeRootDescriptorCalls,
		ComputeRootDescriptorTlsHits,
		ComputeRootDescriptorChanged,
		ComputeRootDescriptorNoChange,

		GraphicsRootConstantsCalls,
		GraphicsRootConstantsValues,
		ComputeRootConstantsCalls,
		ComputeRootConstantsValues,

		PrimitiveTopologyCalls,
		PrimitiveTopologyChanged,
		PrimitiveTopologyNoChange,
		RuntimePrimitiveTopologyCalls,
		RuntimePrimitiveTopologyChanged,
		RuntimePrimitiveTopologyNoChange,

		IndexBufferCalls,
		IndexBufferChanged,
		IndexBufferNoChange,
		IndexBufferResolvedReleased,
		RuntimeIndexBufferCalls,
		RuntimeIndexBufferChanged,
		RuntimeIndexBufferNoChange,

		VertexBufferCalls,
		VertexBufferSlots,
		VertexBufferChangedSlots,
		VertexBufferNoChangeSlots,
		VertexBufferResolvedReleased,
		RuntimeVertexBufferCalls,
		RuntimeVertexBufferSlots,
		RuntimeVertexBufferChangedSlots,
		RuntimeVertexBufferNoChangeSlots,

		BarrierCaptureCalls,
		BarrierCaptureItems,
		BarrierCaptureSkippedInternalReplay,
		BarrierCaptureSkippedHotPath,
		BarrierCaptureSkippedStatePolicy,
		BarrierRecordedCalls,
		BarrierRecordedItems,
		BarrierUnwrapCalls,
		BarrierUnwrapItems,

		PreSkinBindingCacheCalls,
		PreSkinBindingCacheHits,
		PreSkinBindingCacheMisses,
		PreSkinBindingCacheStores,
		PreSkinBindingRootTables,
		PreSkinBindingRanges,
		PreSkinBindingLightDescriptorLookups,
		PreSkinBindingFullDescriptorLookups,
		PreSkinBindingConstants,
		PreSkinBindingRootDescriptors,
		PreSkinBindingRuntimeResolved,
		PreSkinBindingRuntimeUnresolved,
		PreSkinBindingDescriptorHits,
		PreSkinBindingDescriptorMisses,
		PreSkinBindingDescriptorPending,
		PreSkinBindingWaitAvoided,
		RuntimeDescriptorWrites,
		RuntimeDescriptorCopyCalls,
		RuntimeDescriptorCopied,
		RuntimeDescriptorCopyMisses,

		DispatchDecisionCacheHits,
		DispatchDecisionCacheMisses,
		DispatchDecisionPsoCacheHits,
		DispatchDecisionPsoCacheMisses,
		DispatchDecisionBuilds,
		DispatchDecisionNoCompute,
		DispatchDecisionNoPreSkinCandidate,
		DispatchDecisionPreSkinCandidate,
		DispatchDecisionPreSkinCandidateDispatches,
		DispatchDecisionShaderOverrideWork,
		DispatchDecisionFastRejects,

		PreSkinPlanCacheLookups,
		PreSkinPlanCacheHits,
		PreSkinPlanCacheNegativeHits,
		PreSkinPlanCachePositiveHits,
		PreSkinPlanCacheMisses,
		PreSkinPlanCacheStores,
		PreSkinPlanFingerprintBuilds,

		DrawDecisionCacheHits,
		DrawDecisionCacheMisses,
		DrawDecisionPsoCacheHits,
		DrawDecisionPsoCacheMisses,
		DrawDecisionBuilds,
		DrawDecisionNoPso,
		DrawDecisionNoShaderOverride,
		DrawDecisionNoDrawOverride,
		DrawDecisionNeedsIaHash,
		DrawDecisionNeedsDescriptorMatch,
		DrawDecisionNeedsReplacement,
		DrawDecisionSkipOriginal,
		DrawDecisionIaHashDraws,
		DrawDecisionIaHashCacheLookups,
		DrawDecisionIaHashCacheHits,
		DrawDecisionIaHashCacheMisses,
		DrawDecisionIaHashCacheStores,
		DrawDecisionIaHashCacheEvictions,
		DrawDecisionIaHashCacheNegativeHits,
		DrawDecisionDescriptorDraws,
		DrawDecisionDirectProbeCacheLookups,
		DrawDecisionDirectProbeCacheHits,
		DrawDecisionDirectProbeCacheMisses,
		DrawDecisionDirectProbeCachePositiveHits,
		DrawDecisionDirectProbeCacheNegativeHits,
		DrawDecisionDirectProbeCacheStores,
		DrawDecisionDirectProbeCacheEvictions,

	// Draw-hook no-op forwarding diagnostics. These are per-frame counters in
	// timing builds so a rejected PSO-level fast path never requires per-draw
	// logging to diagnose.
		DrawNoopFastPathAttempts,
		DrawNoopFastPathForwarded,
		DrawNoopFastPathHuntBlocked,
		DrawNoopFastPathCaptureBlocked,
		DrawNoopFastPathShaderDumpBlocked,
		DrawNoopFastPathNoRuntime,
		DrawNoopFastPathNoDecision,
		DrawNoopFastPathNonNoopDecision,

	// Zero-work-path verification (docs/DX11_ARCHITECTURE_MIGRATION_TO_DX12.md
	// Stage A): binding events, descriptor epoch writes and Hunt visited-set
	// writes must stay 0 in plain Mod mode; descriptor record fast-skips should
	// cover nearly every binding call when no capture is active.
		BindingEventsRecorded,
		DescriptorEpochWrites,
		HuntVisitedWrites,
		DescriptorRecordFastSkips,
		Count
	};

	// Per-thread counter tables, claimed once per calling thread and drained
	// once per present. The previous process-wide interlocked counters
	// serialized every record call from every thread on shared cache lines.
	// Same pool idiom as DX12CreateTimingTelemetryUtils: plain per-thread
	// adds on the hot path, drain with InterlockedExchange64 at flush; a
	// racing increment can be attributed one frame late (diagnostics only).
	// Threads beyond the pool share the interlocked fallback counters. The
	// counters only run in an actual timing build (DX12CommandFlow.hpp
	// convention): defining DX12_ENABLE_DETAILED_TIMING alone must never
	// enable counting in a plain Release build.
	static constexpr UINT kThreadTableCount = 64;
	static constexpr UINT kCounterCount = static_cast<UINT>(Counter::Count);
	struct alignas(64) ThreadTable
	{
		DWORD threadId = 0;
		LONG64 values[kCounterCount] = {};
	};
	inline static ThreadTable ThreadTables[kThreadTableCount] = {};
	inline static volatile LONG ThreadTableCount = 0;
	inline static volatile LONG64 FallbackCounters[kCounterCount] = {};

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

	static void Inc(Counter counter)
	{
	#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
		const UINT index = static_cast<UINT>(counter);
		ThreadTable *table = GetThreadTable();
		if (table) {
			table->values[index] += 1;
			return;
		}
		// Overflow threads share the interlocked fallback counters.
		InterlockedExchangeAdd64(&FallbackCounters[index], 1);
	#else
		(void)counter;
	#endif
	}

	static void Add64(Counter counter, LONG64 amount)
	{
	#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
		if (!amount)
			return;
		const UINT index = static_cast<UINT>(counter);
		ThreadTable *table = GetThreadTable();
		if (table) {
			table->values[index] += amount;
			return;
		}
		InterlockedExchangeAdd64(&FallbackCounters[index], amount);
	#else
		(void)counter;
		(void)amount;
	#endif
	}

	static LONG64 Drain(Counter counter)
	{
		const UINT index = static_cast<UINT>(counter);
		LONG64 total = 0;
		const LONG tableCount = InterlockedCompareExchange(&ThreadTableCount, 0, 0);
		const LONG tablesToScan = tableCount < static_cast<LONG>(kThreadTableCount)
			? tableCount : static_cast<LONG>(kThreadTableCount);
		for (LONG t = 0; t < tablesToScan; ++t)
			total += InterlockedExchange64(&ThreadTables[t].values[index], 0);
		total += InterlockedExchange64(&FallbackCounters[index], 0);
		return total;
	}

	static LONG Take(Counter counter)
	{
		return static_cast<LONG>(Drain(counter));
	}

	static LONG64 Take64(Counter counter)
	{
		return Drain(counter);
	}

	static void RecordPsoCall()
	{
		Inc(Counter::PsoCalls);
	}

	static void RecordPsoChanged()
	{
		Inc(Counter::PsoChanged);
	}

	static void RecordPsoNoChange()
	{
		Inc(Counter::PsoNoChange);
	}

	static void RecordRuntimePso(bool changed)
	{
		Inc(Counter::RuntimePsoCalls);
		if (changed)
			Inc(Counter::RuntimePsoChanged);
		else
			Inc(Counter::RuntimePsoNoChange);
	}

	static void RecordRuntimeLookupTlsHit()
	{
		Inc(Counter::RuntimeLookupTlsHits);
	}

	static void RecordRuntimeLookupDirectHit()
	{
		Inc(Counter::RuntimeLookupDirectHits);
	}

	static void RecordRuntimeLookupShardHit()
	{
		Inc(Counter::RuntimeLookupShardHits);
	}

	static void RecordRuntimeLookupMiss()
	{
		Inc(Counter::RuntimeLookupMisses);
	}

	static void RecordGraphicsRootSignature(bool changed)
	{
		Inc(Counter::GraphicsRootSignatureCalls);
		if (changed)
			Inc(Counter::GraphicsRootSignatureChanged);
		else
			Inc(Counter::GraphicsRootSignatureNoChange);
	}

	static void RecordComputeRootSignature(bool changed)
	{
		Inc(Counter::ComputeRootSignatureCalls);
		if (changed)
			Inc(Counter::ComputeRootSignatureChanged);
		else
			Inc(Counter::ComputeRootSignatureNoChange);
	}

	static void RecordDescriptorHeapResolve(UINT count)
	{
		Inc(Counter::DescriptorHeapResolveCalls);
		Add64(Counter::DescriptorHeapResolveSlots, static_cast<LONG64>(count));
	}

	static void RecordDescriptorHeapsChanged(
		UINT graphicsInvalidated,
		UINT computeInvalidated)
	{
		Inc(Counter::DescriptorHeapCalls);
		Inc(Counter::DescriptorHeapChanged);
		Add64(Counter::DescriptorHeapInvalidatedGraphicsTables, static_cast<LONG64>(graphicsInvalidated));
		Add64(Counter::DescriptorHeapInvalidatedComputeTables, static_cast<LONG64>(computeInvalidated));
	}

	static void RecordDescriptorHeapsNoChange()
	{
		Inc(Counter::DescriptorHeapCalls);
		Inc(Counter::DescriptorHeapNoChange);
	}

	static void RecordGraphicsRootTableCall()
	{
		Inc(Counter::GraphicsRootTableCalls);
	}

	static void RecordGraphicsRootTableTlsHit()
	{
		Inc(Counter::GraphicsRootTableTlsHits);
	}

	static void RecordGraphicsRootTableChanged()
	{
		Inc(Counter::GraphicsRootTableChanged);
	}

	static void RecordGraphicsRootTableNoChange()
	{
		Inc(Counter::GraphicsRootTableNoChange);
	}

	static void RecordComputeRootTableCall()
	{
		Inc(Counter::ComputeRootTableCalls);
	}

	static void RecordComputeRootTableTlsHit()
	{
		Inc(Counter::ComputeRootTableTlsHits);
	}

	static void RecordComputeRootTableChanged()
	{
		Inc(Counter::ComputeRootTableChanged);
	}

	static void RecordComputeRootTableNoChange()
	{
		Inc(Counter::ComputeRootTableNoChange);
	}

	static void RecordGraphicsRootDescriptorCall()
	{
		Inc(Counter::GraphicsRootDescriptorCalls);
	}

	static void RecordGraphicsRootDescriptorTlsHit()
	{
		Inc(Counter::GraphicsRootDescriptorTlsHits);
	}

	static void RecordGraphicsRootDescriptorChanged()
	{
		Inc(Counter::GraphicsRootDescriptorChanged);
	}

	static void RecordGraphicsRootDescriptorNoChange()
	{
		Inc(Counter::GraphicsRootDescriptorNoChange);
	}

	static void RecordComputeRootDescriptorCall()
	{
		Inc(Counter::ComputeRootDescriptorCalls);
	}

	static void RecordComputeRootDescriptorTlsHit()
	{
		Inc(Counter::ComputeRootDescriptorTlsHits);
	}

	static void RecordComputeRootDescriptorChanged()
	{
		Inc(Counter::ComputeRootDescriptorChanged);
	}

	static void RecordComputeRootDescriptorNoChange()
	{
		Inc(Counter::ComputeRootDescriptorNoChange);
	}

	static void RecordGraphicsRootConstants(UINT count)
	{
		Inc(Counter::GraphicsRootConstantsCalls);
		Add64(Counter::GraphicsRootConstantsValues, static_cast<LONG64>(count));
	}

	static void RecordComputeRootConstants(UINT count)
	{
		Inc(Counter::ComputeRootConstantsCalls);
		Add64(Counter::ComputeRootConstantsValues, static_cast<LONG64>(count));
	}

	static void RecordPrimitiveTopology(bool changed)
	{
		Inc(Counter::PrimitiveTopologyCalls);
		if (changed)
			Inc(Counter::PrimitiveTopologyChanged);
		else
			Inc(Counter::PrimitiveTopologyNoChange);
	}

	static void RecordRuntimePrimitiveTopology(bool changed)
	{
		Inc(Counter::RuntimePrimitiveTopologyCalls);
		if (changed)
			Inc(Counter::RuntimePrimitiveTopologyChanged);
		else
			Inc(Counter::RuntimePrimitiveTopologyNoChange);
	}

	static void RecordIndexBuffer(bool changed)
	{
		Inc(Counter::IndexBufferCalls);
		if (changed)
			Inc(Counter::IndexBufferChanged);
		else
			Inc(Counter::IndexBufferNoChange);
	}

	static void RecordIndexBufferResolvedReleased()
	{
		Inc(Counter::IndexBufferResolvedReleased);
	}

	static void RecordRuntimeIndexBuffer(bool changed)
	{
		Inc(Counter::RuntimeIndexBufferCalls);
		if (changed)
			Inc(Counter::RuntimeIndexBufferChanged);
		else
			Inc(Counter::RuntimeIndexBufferNoChange);
	}

	static void RecordVertexBufferCall(UINT slots)
	{
		Inc(Counter::VertexBufferCalls);
		Add64(Counter::VertexBufferSlots, static_cast<LONG64>(slots));
	}

	static void RecordVertexBufferSlot(bool changed)
	{
		if (changed)
			Add64(Counter::VertexBufferChangedSlots, 1);
		else
			Add64(Counter::VertexBufferNoChangeSlots, 1);
	}

	static void RecordVertexBufferResolvedReleased()
	{
		Inc(Counter::VertexBufferResolvedReleased);
	}

	static void RecordRuntimeVertexBufferCall(UINT slots)
	{
		Inc(Counter::RuntimeVertexBufferCalls);
		Add64(Counter::RuntimeVertexBufferSlots, static_cast<LONG64>(slots));
	}

	static void RecordRuntimeVertexBufferSlot(bool changed)
	{
		if (changed)
			Add64(Counter::RuntimeVertexBufferChangedSlots, 1);
		else
			Add64(Counter::RuntimeVertexBufferNoChangeSlots, 1);
	}

	static void RecordBarrierCaptureCall(UINT count)
	{
		Inc(Counter::BarrierCaptureCalls);
		Add64(Counter::BarrierCaptureItems, static_cast<LONG64>(count));
	}

	static void RecordBarrierCaptureSkippedInternalReplay()
	{
		Inc(Counter::BarrierCaptureSkippedInternalReplay);
	}

	static void RecordBarrierCaptureSkippedHotPath()
	{
		Inc(Counter::BarrierCaptureSkippedHotPath);
	}

	static void RecordBarrierCaptureSkippedStatePolicy()
	{
		Inc(Counter::BarrierCaptureSkippedStatePolicy);
	}

	static void RecordBarrierRecorded(UINT count)
	{
		Inc(Counter::BarrierRecordedCalls);
		Add64(Counter::BarrierRecordedItems, static_cast<LONG64>(count));
	}

	static void RecordBarrierUnwrap(UINT count)
	{
		Inc(Counter::BarrierUnwrapCalls);
		Add64(Counter::BarrierUnwrapItems, static_cast<LONG64>(count));
	}

	static void RecordPreSkinBindingCacheCall()
	{
		Inc(Counter::PreSkinBindingCacheCalls);
	}

	static void RecordPreSkinBindingCacheHit()
	{
		Inc(Counter::PreSkinBindingCacheHits);
	}

	static void RecordPreSkinBindingCacheMiss()
	{
		Inc(Counter::PreSkinBindingCacheMisses);
	}

	static void RecordPreSkinBindingCacheStore()
	{
		Inc(Counter::PreSkinBindingCacheStores);
	}

	static void RecordPreSkinBindingRootTable()
	{
		Add64(Counter::PreSkinBindingRootTables, 1);
	}

	static void RecordPreSkinBindingRange()
	{
		Add64(Counter::PreSkinBindingRanges, 1);
	}


	static void RecordPreSkinBindingFullDescriptorLookup()
	{
		Add64(Counter::PreSkinBindingFullDescriptorLookups, 1);
	}

	static void RecordPreSkinBindingConstant()
	{
		Add64(Counter::PreSkinBindingConstants, 1);
	}

	static void RecordPreSkinBindingRootDescriptor()
	{
		Add64(Counter::PreSkinBindingRootDescriptors, 1);
	}

	static void RecordPreSkinBindingRuntimeTable(bool resolved)
	{
		if (resolved)
			Add64(Counter::PreSkinBindingRuntimeResolved, 1);
		else
			Add64(Counter::PreSkinBindingRuntimeUnresolved, 1);
	}

	static void RecordPreSkinBindingDescriptorLookupResult(
		LONG64 hitCount,
		LONG64 missCount,
		LONG64 pendingCount)
	{
		Add64(Counter::PreSkinBindingDescriptorHits, hitCount);
		Add64(Counter::PreSkinBindingDescriptorMisses, missCount);
		Add64(Counter::PreSkinBindingDescriptorPending, pendingCount);
		Add64(Counter::PreSkinBindingWaitAvoided, hitCount + missCount + pendingCount);
	}


	static void RecordDispatchDecisionCacheHit()
	{
		Inc(Counter::DispatchDecisionCacheHits);
	}

	static void RecordDispatchDecisionCacheMiss()
	{
		Inc(Counter::DispatchDecisionCacheMisses);
	}

	static void RecordDispatchDecisionPsoCacheHit()
	{
		Inc(Counter::DispatchDecisionPsoCacheHits);
	}

	static void RecordDispatchDecisionPsoCacheMiss()
	{
		Inc(Counter::DispatchDecisionPsoCacheMisses);
	}

	static void RecordDispatchDecisionBuild()
	{
		Inc(Counter::DispatchDecisionBuilds);
	}

	static void RecordDispatchDecisionNoCompute()
	{
		Inc(Counter::DispatchDecisionNoCompute);
	}

	static void RecordDispatchDecisionNoPreSkinCandidate()
	{
		Inc(Counter::DispatchDecisionNoPreSkinCandidate);
	}

	static void RecordDispatchDecisionPreSkinCandidate()
	{
		Inc(Counter::DispatchDecisionPreSkinCandidate);
	}

	static void RecordDispatchDecisionPreSkinCandidateDispatch()
	{
		Inc(Counter::DispatchDecisionPreSkinCandidateDispatches);
	}

	static void RecordDispatchDecisionShaderOverrideWork()
	{
		Inc(Counter::DispatchDecisionShaderOverrideWork);
	}

	static void RecordDispatchDecisionFastReject()
	{
		Inc(Counter::DispatchDecisionFastRejects);
	}

	static void RecordPreSkinPlanCacheLookup()
	{
		Inc(Counter::PreSkinPlanCacheLookups);
	}

	static void RecordPreSkinPlanCacheHit(bool positive)
	{
		Inc(Counter::PreSkinPlanCacheHits);
		if (positive)
			Inc(Counter::PreSkinPlanCachePositiveHits);
		else
			Inc(Counter::PreSkinPlanCacheNegativeHits);
	}

	static void RecordPreSkinPlanCacheMiss()
	{
		Inc(Counter::PreSkinPlanCacheMisses);
	}

	static void RecordPreSkinPlanCacheStore()
	{
		Inc(Counter::PreSkinPlanCacheStores);
	}

	static void RecordPreSkinPlanFingerprintBuild()
	{
		Inc(Counter::PreSkinPlanFingerprintBuilds);
	}

	static void RecordDrawDecisionCacheHit()
	{
		Inc(Counter::DrawDecisionCacheHits);
	}

	static void RecordDrawDecisionCacheMiss()
	{
		Inc(Counter::DrawDecisionCacheMisses);
	}

	static void RecordDrawDecisionPsoCacheHit()
	{
		Inc(Counter::DrawDecisionPsoCacheHits);
	}

	static void RecordDrawDecisionPsoCacheMiss()
	{
		Inc(Counter::DrawDecisionPsoCacheMisses);
	}

	static void RecordDrawDecisionBuild()
	{
		Inc(Counter::DrawDecisionBuilds);
	}

	static void RecordDrawDecisionNoPso()
	{
		Inc(Counter::DrawDecisionNoPso);
	}

	static void RecordDrawDecisionNoShaderOverride()
	{
		Inc(Counter::DrawDecisionNoShaderOverride);
	}

	static void RecordDrawDecisionNoDrawOverride()
	{
		Inc(Counter::DrawDecisionNoDrawOverride);
	}

	static void RecordDrawDecisionNeedsIaHash()
	{
		Inc(Counter::DrawDecisionNeedsIaHash);
	}

	static void RecordDrawDecisionNeedsDescriptorMatch()
	{
		Inc(Counter::DrawDecisionNeedsDescriptorMatch);
	}

	static void RecordDrawDecisionNeedsReplacement()
	{
		Inc(Counter::DrawDecisionNeedsReplacement);
	}

	static void RecordDrawDecisionSkipOriginal()
	{
		Inc(Counter::DrawDecisionSkipOriginal);
	}

	static void RecordDrawDecisionIaHashDraw()
	{
		Inc(Counter::DrawDecisionIaHashDraws);
	}

	static void RecordDrawDecisionIaHashCacheLookup()
	{
		Inc(Counter::DrawDecisionIaHashCacheLookups);
	}

	static void RecordDrawDecisionIaHashCacheHit(bool hasIaHashState)
	{
		Inc(Counter::DrawDecisionIaHashCacheHits);
		if (!hasIaHashState)
			Inc(Counter::DrawDecisionIaHashCacheNegativeHits);
	}

	static void RecordDrawDecisionIaHashCacheMiss()
	{
		Inc(Counter::DrawDecisionIaHashCacheMisses);
	}

	static void RecordDrawDecisionIaHashCacheStore()
	{
		Inc(Counter::DrawDecisionIaHashCacheStores);
	}

	static void RecordDrawDecisionIaHashCacheEviction()
	{
		Inc(Counter::DrawDecisionIaHashCacheEvictions);
	}

	static void RecordDrawDecisionDescriptorDraw()
	{
		Inc(Counter::DrawDecisionDescriptorDraws);
	}

	static void RecordDrawDecisionDirectProbeCacheLookup()
	{
		Inc(Counter::DrawDecisionDirectProbeCacheLookups);
	}

	static void RecordDrawDecisionDirectProbeCacheHit(bool hasDirectCandidate)
	{
		Inc(Counter::DrawDecisionDirectProbeCacheHits);
		if (hasDirectCandidate)
			Inc(Counter::DrawDecisionDirectProbeCachePositiveHits);
		else
			Inc(Counter::DrawDecisionDirectProbeCacheNegativeHits);
	}

	static void RecordDrawDecisionDirectProbeCacheMiss()
	{
		Inc(Counter::DrawDecisionDirectProbeCacheMisses);
	}

	static void RecordDrawDecisionDirectProbeCacheStore(bool evicted)
	{
		Inc(Counter::DrawDecisionDirectProbeCacheStores);
		if (evicted)
			Inc(Counter::DrawDecisionDirectProbeCacheEvictions);
	}

	static void RecordDrawNoopFastPathAttempt()
	{
		Inc(Counter::DrawNoopFastPathAttempts);
	}

	static void RecordDrawNoopFastPathForwarded()
	{
		Inc(Counter::DrawNoopFastPathForwarded);
	}

	static void RecordDrawNoopFastPathHuntBlocked()
	{
		Inc(Counter::DrawNoopFastPathHuntBlocked);
	}

	static void RecordDrawNoopFastPathCaptureBlocked()
	{
		Inc(Counter::DrawNoopFastPathCaptureBlocked);
	}

	static void RecordDrawNoopFastPathShaderDumpBlocked()
	{
		Inc(Counter::DrawNoopFastPathShaderDumpBlocked);
	}

	static void RecordDrawNoopFastPathNoRuntime()
	{
		Inc(Counter::DrawNoopFastPathNoRuntime);
	}

	static void RecordDrawNoopFastPathNoDecision()
	{
		Inc(Counter::DrawNoopFastPathNoDecision);
	}

	static void RecordDrawNoopFastPathNonNoopDecision()
	{
		Inc(Counter::DrawNoopFastPathNonNoopDecision);
	}

	static void LogPso()
	{
		LONG psoCalls = Take(Counter::PsoCalls);
		LONG psoChanged = Take(Counter::PsoChanged);
		LONG psoNoChange = Take(Counter::PsoNoChange);
		LONG runtimePsoCalls = Take(Counter::RuntimePsoCalls);
		LONG runtimePsoChanged = Take(Counter::RuntimePsoChanged);
		LONG runtimePsoNoChange = Take(Counter::RuntimePsoNoChange);
		if (!psoCalls && !psoChanged && !psoNoChange &&
			!runtimePsoCalls && !runtimePsoChanged && !runtimePsoNoChange)
			return;
		DX12LogManager::Fields("DX12ResidentMaintenance",
			"\"category\":\"pso\",\"trackerCalls\":%ld,\"trackerChanged\":%ld,\"trackerNoChange\":%ld,\"runtimeCalls\":%ld,\"runtimeChanged\":%ld,\"runtimeNoChange\":%ld",
			psoCalls, psoChanged, psoNoChange,
			runtimePsoCalls, runtimePsoChanged, runtimePsoNoChange);
	}

	static void LogRootState()
	{
		LONG graphicsRootSignatureCalls = Take(Counter::GraphicsRootSignatureCalls);
		LONG graphicsRootSignatureChanged = Take(Counter::GraphicsRootSignatureChanged);
		LONG graphicsRootSignatureNoChange = Take(Counter::GraphicsRootSignatureNoChange);
		LONG computeRootSignatureCalls = Take(Counter::ComputeRootSignatureCalls);
		LONG computeRootSignatureChanged = Take(Counter::ComputeRootSignatureChanged);
		LONG computeRootSignatureNoChange = Take(Counter::ComputeRootSignatureNoChange);
		LONG graphicsRootTableCalls = Take(Counter::GraphicsRootTableCalls);
		LONG graphicsRootTableTlsHits = Take(Counter::GraphicsRootTableTlsHits);
		LONG graphicsRootTableChanged = Take(Counter::GraphicsRootTableChanged);
		LONG graphicsRootTableNoChange = Take(Counter::GraphicsRootTableNoChange);
		LONG computeRootTableCalls = Take(Counter::ComputeRootTableCalls);
		LONG computeRootTableTlsHits = Take(Counter::ComputeRootTableTlsHits);
		LONG computeRootTableChanged = Take(Counter::ComputeRootTableChanged);
		LONG computeRootTableNoChange = Take(Counter::ComputeRootTableNoChange);
		if (!graphicsRootSignatureCalls && !computeRootSignatureCalls &&
			!graphicsRootTableCalls && !computeRootTableCalls)
			return;
		DX12LogManager::Fields("DX12ResidentMaintenance",
			"\"category\":\"root\",\"gfxSigCalls\":%ld,\"gfxSigChanged\":%ld,\"gfxSigNoChange\":%ld,\"compSigCalls\":%ld,\"compSigChanged\":%ld,\"compSigNoChange\":%ld,\"gfxTableCalls\":%ld,\"gfxTableTlsHits\":%ld,\"gfxTableChanged\":%ld,\"gfxTableNoChange\":%ld,\"compTableCalls\":%ld,\"compTableTlsHits\":%ld,\"compTableChanged\":%ld,\"compTableNoChange\":%ld",
			graphicsRootSignatureCalls, graphicsRootSignatureChanged, graphicsRootSignatureNoChange,
			computeRootSignatureCalls, computeRootSignatureChanged, computeRootSignatureNoChange,
			graphicsRootTableCalls, graphicsRootTableTlsHits, graphicsRootTableChanged, graphicsRootTableNoChange,
			computeRootTableCalls, computeRootTableTlsHits, computeRootTableChanged, computeRootTableNoChange);
	}

	static void LogDescriptorState()
	{
		LONG descriptorHeapResolveCalls = Take(Counter::DescriptorHeapResolveCalls);
		LONG64 descriptorHeapResolveSlots = Take64(Counter::DescriptorHeapResolveSlots);
		LONG descriptorHeapCalls = Take(Counter::DescriptorHeapCalls);
		LONG descriptorHeapChanged = Take(Counter::DescriptorHeapChanged);
		LONG descriptorHeapNoChange = Take(Counter::DescriptorHeapNoChange);
		LONG64 descriptorHeapInvalidatedGraphicsTables = Take64(Counter::DescriptorHeapInvalidatedGraphicsTables);
		LONG64 descriptorHeapInvalidatedComputeTables = Take64(Counter::DescriptorHeapInvalidatedComputeTables);
		LONG graphicsRootDescriptorCalls = Take(Counter::GraphicsRootDescriptorCalls);
		LONG graphicsRootDescriptorTlsHits = Take(Counter::GraphicsRootDescriptorTlsHits);
		LONG graphicsRootDescriptorChanged = Take(Counter::GraphicsRootDescriptorChanged);
		LONG graphicsRootDescriptorNoChange = Take(Counter::GraphicsRootDescriptorNoChange);
		LONG computeRootDescriptorCalls = Take(Counter::ComputeRootDescriptorCalls);
		LONG computeRootDescriptorTlsHits = Take(Counter::ComputeRootDescriptorTlsHits);
		LONG computeRootDescriptorChanged = Take(Counter::ComputeRootDescriptorChanged);
		LONG computeRootDescriptorNoChange = Take(Counter::ComputeRootDescriptorNoChange);
		LONG graphicsRootConstantsCalls = Take(Counter::GraphicsRootConstantsCalls);
		LONG64 graphicsRootConstantsValues = Take64(Counter::GraphicsRootConstantsValues);
		LONG computeRootConstantsCalls = Take(Counter::ComputeRootConstantsCalls);
		LONG64 computeRootConstantsValues = Take64(Counter::ComputeRootConstantsValues);
		if (!descriptorHeapResolveCalls && !descriptorHeapCalls &&
			!graphicsRootDescriptorCalls && !computeRootDescriptorCalls &&
			!graphicsRootConstantsCalls && !computeRootConstantsCalls)
			return;
		DX12LogManager::Fields("DX12ResidentMaintenance",
			"\"category\":\"descriptor\",\"heapResolveCalls\":%ld,\"heapResolveSlots\":%lld,\"heapCalls\":%ld,\"heapChanged\":%ld,\"heapNoChange\":%ld,\"heapInvalidatedGfxTables\":%lld,\"heapInvalidatedCompTables\":%lld,\"gfxDescCalls\":%ld,\"gfxDescTlsHits\":%ld,\"gfxDescChanged\":%ld,\"gfxDescNoChange\":%ld,\"compDescCalls\":%ld,\"compDescTlsHits\":%ld,\"compDescChanged\":%ld,\"compDescNoChange\":%ld,\"gfxConstCalls\":%ld,\"gfxConstValues\":%lld,\"compConstCalls\":%ld,\"compConstValues\":%lld",
			descriptorHeapResolveCalls, static_cast<long long>(descriptorHeapResolveSlots),
			descriptorHeapCalls, descriptorHeapChanged, descriptorHeapNoChange,
			static_cast<long long>(descriptorHeapInvalidatedGraphicsTables),
			static_cast<long long>(descriptorHeapInvalidatedComputeTables),
			graphicsRootDescriptorCalls, graphicsRootDescriptorTlsHits,
			graphicsRootDescriptorChanged, graphicsRootDescriptorNoChange,
			computeRootDescriptorCalls, computeRootDescriptorTlsHits,
			computeRootDescriptorChanged, computeRootDescriptorNoChange,
			graphicsRootConstantsCalls, static_cast<long long>(graphicsRootConstantsValues),
			computeRootConstantsCalls, static_cast<long long>(computeRootConstantsValues));
	}

	static void LogIaState()
	{
		LONG primitiveTopologyCalls = Take(Counter::PrimitiveTopologyCalls);
		LONG primitiveTopologyChanged = Take(Counter::PrimitiveTopologyChanged);
		LONG primitiveTopologyNoChange = Take(Counter::PrimitiveTopologyNoChange);
		LONG runtimePrimitiveTopologyCalls = Take(Counter::RuntimePrimitiveTopologyCalls);
		LONG runtimePrimitiveTopologyChanged = Take(Counter::RuntimePrimitiveTopologyChanged);
		LONG runtimePrimitiveTopologyNoChange = Take(Counter::RuntimePrimitiveTopologyNoChange);
		LONG indexBufferCalls = Take(Counter::IndexBufferCalls);
		LONG indexBufferChanged = Take(Counter::IndexBufferChanged);
		LONG indexBufferNoChange = Take(Counter::IndexBufferNoChange);
		LONG indexBufferResolvedReleased = Take(Counter::IndexBufferResolvedReleased);
		LONG runtimeIndexBufferCalls = Take(Counter::RuntimeIndexBufferCalls);
		LONG runtimeIndexBufferChanged = Take(Counter::RuntimeIndexBufferChanged);
		LONG runtimeIndexBufferNoChange = Take(Counter::RuntimeIndexBufferNoChange);
		LONG vertexBufferCalls = Take(Counter::VertexBufferCalls);
		LONG64 vertexBufferSlots = Take64(Counter::VertexBufferSlots);
		LONG64 vertexBufferChangedSlots = Take64(Counter::VertexBufferChangedSlots);
		LONG64 vertexBufferNoChangeSlots = Take64(Counter::VertexBufferNoChangeSlots);
		LONG vertexBufferResolvedReleased = Take(Counter::VertexBufferResolvedReleased);
		LONG runtimeVertexBufferCalls = Take(Counter::RuntimeVertexBufferCalls);
		LONG64 runtimeVertexBufferSlots = Take64(Counter::RuntimeVertexBufferSlots);
		LONG64 runtimeVertexBufferChangedSlots = Take64(Counter::RuntimeVertexBufferChangedSlots);
		LONG64 runtimeVertexBufferNoChangeSlots = Take64(Counter::RuntimeVertexBufferNoChangeSlots);
		if (!primitiveTopologyCalls && !runtimePrimitiveTopologyCalls &&
			!indexBufferCalls && !runtimeIndexBufferCalls &&
			!vertexBufferCalls && !runtimeVertexBufferCalls)
			return;
		DX12LogManager::Fields("DX12ResidentMaintenance",
			"\"category\":\"ia\",\"topologyCalls\":%ld,\"topologyChanged\":%ld,\"topologyNoChange\":%ld,\"runtimeTopologyCalls\":%ld,\"runtimeTopologyChanged\":%ld,\"runtimeTopologyNoChange\":%ld,\"ibCalls\":%ld,\"ibChanged\":%ld,\"ibNoChange\":%ld,\"ibResolvedReleased\":%ld,\"runtimeIbCalls\":%ld,\"runtimeIbChanged\":%ld,\"runtimeIbNoChange\":%ld,\"vbCalls\":%ld,\"vbSlots\":%lld,\"vbChangedSlots\":%lld,\"vbNoChangeSlots\":%lld,\"vbResolvedReleased\":%ld,\"runtimeVbCalls\":%ld,\"runtimeVbSlots\":%lld,\"runtimeVbChangedSlots\":%lld,\"runtimeVbNoChangeSlots\":%lld",
			primitiveTopologyCalls, primitiveTopologyChanged, primitiveTopologyNoChange,
			runtimePrimitiveTopologyCalls, runtimePrimitiveTopologyChanged, runtimePrimitiveTopologyNoChange,
			indexBufferCalls, indexBufferChanged, indexBufferNoChange, indexBufferResolvedReleased,
			runtimeIndexBufferCalls, runtimeIndexBufferChanged, runtimeIndexBufferNoChange,
			vertexBufferCalls, static_cast<long long>(vertexBufferSlots),
			static_cast<long long>(vertexBufferChangedSlots),
			static_cast<long long>(vertexBufferNoChangeSlots),
			vertexBufferResolvedReleased,
			runtimeVertexBufferCalls, static_cast<long long>(runtimeVertexBufferSlots),
			static_cast<long long>(runtimeVertexBufferChangedSlots),
			static_cast<long long>(runtimeVertexBufferNoChangeSlots));
	}

	static void LogBarrierState()
	{
		LONG barrierCaptureCalls = Take(Counter::BarrierCaptureCalls);
		LONG64 barrierCaptureItems = Take64(Counter::BarrierCaptureItems);
		LONG barrierCaptureSkippedInternalReplay = Take(Counter::BarrierCaptureSkippedInternalReplay);
		LONG barrierCaptureSkippedHotPath = Take(Counter::BarrierCaptureSkippedHotPath);
		LONG barrierCaptureSkippedStatePolicy = Take(Counter::BarrierCaptureSkippedStatePolicy);
		LONG barrierRecordedCalls = Take(Counter::BarrierRecordedCalls);
		LONG64 barrierRecordedItems = Take64(Counter::BarrierRecordedItems);
		LONG barrierUnwrapCalls = Take(Counter::BarrierUnwrapCalls);
		LONG64 barrierUnwrapItems = Take64(Counter::BarrierUnwrapItems);
		if (!barrierCaptureCalls && !barrierRecordedCalls && !barrierUnwrapCalls)
			return;
		DX12LogManager::Fields("DX12ResidentMaintenance",
			"\"category\":\"barrier\",\"captureCalls\":%ld,\"captureItems\":%lld,\"captureSkippedInternalReplay\":%ld,\"captureSkippedHotPath\":%ld,\"captureSkippedStatePolicy\":%ld,\"recordedCalls\":%ld,\"recordedItems\":%lld,\"unwrapCalls\":%ld,\"unwrapItems\":%lld",
			barrierCaptureCalls, static_cast<long long>(barrierCaptureItems),
			barrierCaptureSkippedInternalReplay, barrierCaptureSkippedHotPath,
			barrierCaptureSkippedStatePolicy,
			barrierRecordedCalls, static_cast<long long>(barrierRecordedItems),
			barrierUnwrapCalls, static_cast<long long>(barrierUnwrapItems));
	}

	static void LogPreSkinBindingState()
	{
		LONG preSkinBindingCacheCalls = Take(Counter::PreSkinBindingCacheCalls);
		LONG preSkinBindingCacheHits = Take(Counter::PreSkinBindingCacheHits);
		LONG preSkinBindingCacheMisses = Take(Counter::PreSkinBindingCacheMisses);
		LONG preSkinBindingCacheStores = Take(Counter::PreSkinBindingCacheStores);
		LONG64 preSkinBindingRootTables = Take64(Counter::PreSkinBindingRootTables);
		LONG64 preSkinBindingRanges = Take64(Counter::PreSkinBindingRanges);
		LONG64 preSkinBindingLightDescriptorLookups = Take64(Counter::PreSkinBindingLightDescriptorLookups);
		LONG64 preSkinBindingFullDescriptorLookups = Take64(Counter::PreSkinBindingFullDescriptorLookups);
		LONG64 preSkinBindingConstants = Take64(Counter::PreSkinBindingConstants);
		LONG64 preSkinBindingRootDescriptors = Take64(Counter::PreSkinBindingRootDescriptors);
		LONG64 preSkinBindingRuntimeResolved = Take64(Counter::PreSkinBindingRuntimeResolved);
		LONG64 preSkinBindingRuntimeUnresolved = Take64(Counter::PreSkinBindingRuntimeUnresolved);
		LONG64 preSkinBindingDescriptorHits = Take64(Counter::PreSkinBindingDescriptorHits);
		LONG64 preSkinBindingDescriptorMisses = Take64(Counter::PreSkinBindingDescriptorMisses);
		LONG64 preSkinBindingDescriptorPending = Take64(Counter::PreSkinBindingDescriptorPending);
		LONG64 preSkinBindingWaitAvoided = Take64(Counter::PreSkinBindingWaitAvoided);
		LONG64 runtimeDescriptorWrites = Take64(Counter::RuntimeDescriptorWrites);
		LONG64 runtimeDescriptorCopyCalls = Take64(Counter::RuntimeDescriptorCopyCalls);
		LONG64 runtimeDescriptorCopied = Take64(Counter::RuntimeDescriptorCopied);
		LONG64 runtimeDescriptorCopyMisses = Take64(Counter::RuntimeDescriptorCopyMisses);
		if (!preSkinBindingCacheCalls && !preSkinBindingCacheHits &&
			!preSkinBindingCacheMisses && !preSkinBindingCacheStores &&
			!preSkinBindingRuntimeResolved && !preSkinBindingRuntimeUnresolved &&
			!preSkinBindingWaitAvoided && !runtimeDescriptorWrites &&
			!runtimeDescriptorCopyCalls)
			return;
		DX12LogManager::Fields("DX12ResidentMaintenance",
			"\"category\":\"preskin_bind_cache\",\"calls\":%ld,\"hits\":%ld,\"misses\":%ld,\"stores\":%ld,\"rootTables\":%lld,\"ranges\":%lld,\"lightDescriptorLookups\":%lld,\"fullDescriptorLookups\":%lld,\"constants\":%lld,\"rootDescriptors\":%lld,\"runtimeResolved\":%lld,\"runtimeUnresolved\":%lld,\"descriptorHits\":%lld,\"descriptorMisses\":%lld,\"descriptorPending\":%lld,\"waitAvoided\":%lld,\"runtimeDescriptorWrites\":%lld,\"runtimeDescriptorCopyCalls\":%lld,\"runtimeDescriptorCopied\":%lld,\"runtimeDescriptorCopyMisses\":%lld",
			preSkinBindingCacheCalls, preSkinBindingCacheHits,
			preSkinBindingCacheMisses, preSkinBindingCacheStores,
			static_cast<long long>(preSkinBindingRootTables),
			static_cast<long long>(preSkinBindingRanges),
			static_cast<long long>(preSkinBindingLightDescriptorLookups),
			static_cast<long long>(preSkinBindingFullDescriptorLookups),
			static_cast<long long>(preSkinBindingConstants),
			static_cast<long long>(preSkinBindingRootDescriptors),
			static_cast<long long>(preSkinBindingRuntimeResolved),
			static_cast<long long>(preSkinBindingRuntimeUnresolved),
			static_cast<long long>(preSkinBindingDescriptorHits),
			static_cast<long long>(preSkinBindingDescriptorMisses),
			static_cast<long long>(preSkinBindingDescriptorPending),
			static_cast<long long>(preSkinBindingWaitAvoided),
			static_cast<long long>(runtimeDescriptorWrites),
			static_cast<long long>(runtimeDescriptorCopyCalls),
			static_cast<long long>(runtimeDescriptorCopied),
			static_cast<long long>(runtimeDescriptorCopyMisses));
	}

	static void LogRuntimeLookup()
	{
		LONG tlsHits = Take(Counter::RuntimeLookupTlsHits);
		LONG directHits = Take(Counter::RuntimeLookupDirectHits);
		LONG shardHits = Take(Counter::RuntimeLookupShardHits);
		LONG misses = Take(Counter::RuntimeLookupMisses);
		if (!tlsHits && !directHits && !shardHits && !misses)
			return;
		DX12LogManager::Fields("DX12ResidentMaintenance",
			"\"category\":\"runtime_lookup\",\"tlsHits\":%ld,\"directHits\":%ld,\"shardHits\":%ld,\"misses\":%ld",
			tlsHits, directHits, shardHits, misses);
	}

	static void LogDispatchDecisionState()
	{
		LONG cacheHits = Take(Counter::DispatchDecisionCacheHits);
		LONG cacheMisses = Take(Counter::DispatchDecisionCacheMisses);
		LONG psoCacheHits = Take(Counter::DispatchDecisionPsoCacheHits);
		LONG psoCacheMisses = Take(Counter::DispatchDecisionPsoCacheMisses);
		LONG builds = Take(Counter::DispatchDecisionBuilds);
		LONG noCompute = Take(Counter::DispatchDecisionNoCompute);
		LONG noPreSkinCandidate = Take(Counter::DispatchDecisionNoPreSkinCandidate);
		LONG preSkinCandidate = Take(Counter::DispatchDecisionPreSkinCandidate);
		LONG preSkinCandidateDispatches = Take(Counter::DispatchDecisionPreSkinCandidateDispatches);
		LONG shaderOverrideWork = Take(Counter::DispatchDecisionShaderOverrideWork);
		LONG fastRejects = Take(Counter::DispatchDecisionFastRejects);
		if (!cacheHits && !cacheMisses && !psoCacheHits && !psoCacheMisses &&
			!builds && !noCompute &&
			!noPreSkinCandidate && !preSkinCandidate && !preSkinCandidateDispatches &&
			!shaderOverrideWork && !fastRejects)
			return;
		DX12LogManager::Fields("DX12ResidentMaintenance",
			"\"category\":\"dispatch_decision\",\"cacheHits\":%ld,\"cacheMisses\":%ld,\"psoCacheHits\":%ld,\"psoCacheMisses\":%ld,\"builds\":%ld,\"noCompute\":%ld,\"noPreSkinCandidate\":%ld,\"preSkinCandidateBuilds\":%ld,\"preSkinCandidateDispatches\":%ld,\"shaderOverrideWork\":%ld,\"fastRejects\":%ld",
			cacheHits, cacheMisses, psoCacheHits, psoCacheMisses, builds, noCompute,
			noPreSkinCandidate, preSkinCandidate, preSkinCandidateDispatches,
			shaderOverrideWork, fastRejects);
	}

	static void LogPreSkinPlanState()
	{
		LONG lookups = Take(Counter::PreSkinPlanCacheLookups);
		LONG hits = Take(Counter::PreSkinPlanCacheHits);
		LONG negativeHits = Take(Counter::PreSkinPlanCacheNegativeHits);
		LONG positiveHits = Take(Counter::PreSkinPlanCachePositiveHits);
		LONG misses = Take(Counter::PreSkinPlanCacheMisses);
		LONG stores = Take(Counter::PreSkinPlanCacheStores);
		LONG fingerprints = Take(Counter::PreSkinPlanFingerprintBuilds);
		if (!lookups && !hits && !negativeHits && !positiveHits &&
			!misses && !stores && !fingerprints)
			return;
		DX12LogManager::Fields("DX12ResidentMaintenance",
			"\"category\":\"preskin_plan\",\"lookups\":%ld,\"hits\":%ld,\"negativeHits\":%ld,\"positiveHits\":%ld,\"misses\":%ld,\"stores\":%ld,\"fingerprintBuilds\":%ld",
			lookups, hits, negativeHits, positiveHits,
			misses, stores, fingerprints);
	}

	static void LogDrawDecisionState()
	{
		LONG cacheHits = Take(Counter::DrawDecisionCacheHits);
		LONG cacheMisses = Take(Counter::DrawDecisionCacheMisses);
		LONG psoCacheHits = Take(Counter::DrawDecisionPsoCacheHits);
		LONG psoCacheMisses = Take(Counter::DrawDecisionPsoCacheMisses);
		LONG builds = Take(Counter::DrawDecisionBuilds);
		LONG noPso = Take(Counter::DrawDecisionNoPso);
		LONG noShaderOverride = Take(Counter::DrawDecisionNoShaderOverride);
		LONG noDrawOverride = Take(Counter::DrawDecisionNoDrawOverride);
		LONG needsIaHash = Take(Counter::DrawDecisionNeedsIaHash);
		LONG needsDescriptorMatch = Take(Counter::DrawDecisionNeedsDescriptorMatch);
		LONG needsReplacement = Take(Counter::DrawDecisionNeedsReplacement);
		LONG skipOriginal = Take(Counter::DrawDecisionSkipOriginal);
		LONG iaHashDraws = Take(Counter::DrawDecisionIaHashDraws);
		LONG iaHashCacheLookups = Take(Counter::DrawDecisionIaHashCacheLookups);
		LONG iaHashCacheHits = Take(Counter::DrawDecisionIaHashCacheHits);
		LONG iaHashCacheMisses = Take(Counter::DrawDecisionIaHashCacheMisses);
		LONG iaHashCacheStores = Take(Counter::DrawDecisionIaHashCacheStores);
		LONG iaHashCacheEvictions = Take(Counter::DrawDecisionIaHashCacheEvictions);
		LONG iaHashCacheNegativeHits = Take(Counter::DrawDecisionIaHashCacheNegativeHits);
		LONG descriptorDraws = Take(Counter::DrawDecisionDescriptorDraws);
		LONG directProbeCacheLookups = Take(Counter::DrawDecisionDirectProbeCacheLookups);
		LONG directProbeCacheHits = Take(Counter::DrawDecisionDirectProbeCacheHits);
		LONG directProbeCacheMisses = Take(Counter::DrawDecisionDirectProbeCacheMisses);
		LONG directProbeCachePositiveHits = Take(Counter::DrawDecisionDirectProbeCachePositiveHits);
		LONG directProbeCacheNegativeHits = Take(Counter::DrawDecisionDirectProbeCacheNegativeHits);
		LONG directProbeCacheStores = Take(Counter::DrawDecisionDirectProbeCacheStores);
		LONG directProbeCacheEvictions = Take(Counter::DrawDecisionDirectProbeCacheEvictions);
		if (!cacheHits && !cacheMisses && !psoCacheHits && !psoCacheMisses &&
			!builds && !noPso &&
			!noShaderOverride && !noDrawOverride && !needsIaHash &&
			!needsDescriptorMatch && !needsReplacement && !skipOriginal &&
			!iaHashDraws && !iaHashCacheLookups &&
			!iaHashCacheHits && !iaHashCacheMisses && !iaHashCacheStores &&
			!iaHashCacheEvictions &&
			!iaHashCacheNegativeHits && !descriptorDraws &&
			!directProbeCacheLookups && !directProbeCacheHits &&
			!directProbeCacheMisses && !directProbeCachePositiveHits &&
			!directProbeCacheNegativeHits && !directProbeCacheStores &&
			!directProbeCacheEvictions)
			return;
		DX12LogManager::Fields("DX12ResidentMaintenance",
			"\"category\":\"draw_decision\",\"cacheHits\":%ld,\"cacheMisses\":%ld,\"psoCacheHits\":%ld,\"psoCacheMisses\":%ld,\"builds\":%ld,\"noPso\":%ld,\"noShaderOverride\":%ld,\"noDrawOverride\":%ld,\"needsIaHash\":%ld,\"needsDescriptorMatch\":%ld,\"needsReplacement\":%ld,\"skipOriginal\":%ld,\"iaHashDraws\":%ld,\"iaHashCacheLookups\":%ld,\"iaHashCacheHits\":%ld,\"iaHashCacheMisses\":%ld,\"iaHashCacheStores\":%ld,\"iaHashCacheEvictions\":%ld,\"iaHashCacheNegativeHits\":%ld,\"descriptorDraws\":%ld,\"directProbeCacheLookups\":%ld,\"directProbeCacheHits\":%ld,\"directProbeCacheMisses\":%ld,\"directProbeCachePositiveHits\":%ld,\"directProbeCacheNegativeHits\":%ld,\"directProbeCacheStores\":%ld,\"directProbeCacheEvictions\":%ld",
			cacheHits, cacheMisses, psoCacheHits, psoCacheMisses, builds, noPso,
			noShaderOverride, noDrawOverride, needsIaHash,
			needsDescriptorMatch, needsReplacement, skipOriginal,
			iaHashDraws, iaHashCacheLookups,
			iaHashCacheHits, iaHashCacheMisses, iaHashCacheStores, iaHashCacheEvictions,
			iaHashCacheNegativeHits, descriptorDraws,
			directProbeCacheLookups, directProbeCacheHits, directProbeCacheMisses,
			directProbeCachePositiveHits, directProbeCacheNegativeHits,
			directProbeCacheStores, directProbeCacheEvictions);
	}

	static void LogDrawNoopFastPathState()
	{
		LONG attempts = Take(Counter::DrawNoopFastPathAttempts);
		LONG forwarded = Take(Counter::DrawNoopFastPathForwarded);
		LONG huntBlocked = Take(Counter::DrawNoopFastPathHuntBlocked);
		LONG captureBlocked = Take(Counter::DrawNoopFastPathCaptureBlocked);
		LONG shaderDumpBlocked = Take(Counter::DrawNoopFastPathShaderDumpBlocked);
		LONG noRuntime = Take(Counter::DrawNoopFastPathNoRuntime);
		LONG noDecision = Take(Counter::DrawNoopFastPathNoDecision);
		LONG nonNoopDecision = Take(Counter::DrawNoopFastPathNonNoopDecision);
		if (!attempts && !forwarded && !huntBlocked && !captureBlocked &&
			!shaderDumpBlocked && !noRuntime && !noDecision && !nonNoopDecision)
			return;
		DX12LogManager::Fields("DX12ResidentMaintenance",
			"\"category\":\"draw_noop_fast_path\",\"attempts\":%ld,\"forwarded\":%ld,\"huntBlocked\":%ld,\"captureBlocked\":%ld,\"shaderDumpBlocked\":%ld,\"noRuntime\":%ld,\"noDecision\":%ld,\"nonNoopDecision\":%ld",
			attempts, forwarded, huntBlocked, captureBlocked, shaderDumpBlocked,
			noRuntime, noDecision, nonNoopDecision);
	}

	static void RecordBindingEventRecorded()
	{
		Inc(Counter::BindingEventsRecorded);
	}

	static void RecordDescriptorEpochWrite()
	{
		Inc(Counter::DescriptorEpochWrites);
	}

	static void RecordHuntVisitedWrite()
	{
		Inc(Counter::HuntVisitedWrites);
	}

	static void RecordDescriptorRecordFastSkip()
	{
		Inc(Counter::DescriptorRecordFastSkips);
	}

	static void LogZeroWorkState()
	{
		LONG bindingEvents = Take(Counter::BindingEventsRecorded);
		LONG epochWrites = Take(Counter::DescriptorEpochWrites);
		LONG visitedWrites = Take(Counter::HuntVisitedWrites);
		LONG descriptorFastSkips = Take(Counter::DescriptorRecordFastSkips);
		if (!bindingEvents && !epochWrites && !visitedWrites && !descriptorFastSkips)
			return;
		DX12LogManager::Fields("DX12ResidentMaintenance",
			"\"category\":\"zero_work\",\"bindingEventsRecorded\":%ld,\"descriptorEpochWrites\":%ld,\"huntVisitedWrites\":%ld,\"descriptorRecordFastSkips\":%ld",
			bindingEvents, epochWrites, visitedWrites, descriptorFastSkips);
	}

	static void LogAndReset()
	{
		LogRuntimeLookup();
		LogPso();
		LogRootState();
		LogDescriptorState();
		LogIaState();
		LogBarrierState();
		LogPreSkinBindingState();
		LogDispatchDecisionState();
		LogPreSkinPlanState();
		LogDrawDecisionState();
		LogDrawNoopFastPathState();
		LogZeroWorkState();
	}
};

inline void DX12RecordDescriptorRecordFastSkip()
{
	DX12ResidentMaintenanceTelemetryUtils::RecordDescriptorRecordFastSkip();
}
