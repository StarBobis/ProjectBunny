#pragma once

#include <Windows.h>

#include "DX12LogManager.hpp"

class DX12ResidentMaintenanceTelemetryUtils
{
public:
	inline static volatile LONG PsoCalls = 0;
	inline static volatile LONG PsoChanged = 0;
	inline static volatile LONG PsoNoChange = 0;
	inline static volatile LONG RuntimePsoCalls = 0;
	inline static volatile LONG RuntimePsoChanged = 0;
	inline static volatile LONG RuntimePsoNoChange = 0;

	inline static volatile LONG GraphicsRootSignatureCalls = 0;
	inline static volatile LONG GraphicsRootSignatureChanged = 0;
	inline static volatile LONG GraphicsRootSignatureNoChange = 0;
	inline static volatile LONG ComputeRootSignatureCalls = 0;
	inline static volatile LONG ComputeRootSignatureChanged = 0;
	inline static volatile LONG ComputeRootSignatureNoChange = 0;

	inline static volatile LONG DescriptorHeapResolveCalls = 0;
	inline static volatile LONG64 DescriptorHeapResolveSlots = 0;
	inline static volatile LONG DescriptorHeapCalls = 0;
	inline static volatile LONG DescriptorHeapChanged = 0;
	inline static volatile LONG DescriptorHeapNoChange = 0;
	inline static volatile LONG64 DescriptorHeapInvalidatedGraphicsTables = 0;
	inline static volatile LONG64 DescriptorHeapInvalidatedComputeTables = 0;

	inline static volatile LONG GraphicsRootTableCalls = 0;
	inline static volatile LONG GraphicsRootTableTlsHits = 0;
	inline static volatile LONG GraphicsRootTableChanged = 0;
	inline static volatile LONG GraphicsRootTableNoChange = 0;
	inline static volatile LONG ComputeRootTableCalls = 0;
	inline static volatile LONG ComputeRootTableTlsHits = 0;
	inline static volatile LONG ComputeRootTableChanged = 0;
	inline static volatile LONG ComputeRootTableNoChange = 0;

	inline static volatile LONG GraphicsRootDescriptorCalls = 0;
	inline static volatile LONG GraphicsRootDescriptorTlsHits = 0;
	inline static volatile LONG GraphicsRootDescriptorChanged = 0;
	inline static volatile LONG GraphicsRootDescriptorNoChange = 0;
	inline static volatile LONG ComputeRootDescriptorCalls = 0;
	inline static volatile LONG ComputeRootDescriptorTlsHits = 0;
	inline static volatile LONG ComputeRootDescriptorChanged = 0;
	inline static volatile LONG ComputeRootDescriptorNoChange = 0;

	inline static volatile LONG GraphicsRootConstantsCalls = 0;
	inline static volatile LONG64 GraphicsRootConstantsValues = 0;
	inline static volatile LONG ComputeRootConstantsCalls = 0;
	inline static volatile LONG64 ComputeRootConstantsValues = 0;

	inline static volatile LONG PrimitiveTopologyCalls = 0;
	inline static volatile LONG PrimitiveTopologyChanged = 0;
	inline static volatile LONG PrimitiveTopologyNoChange = 0;
	inline static volatile LONG RuntimePrimitiveTopologyCalls = 0;
	inline static volatile LONG RuntimePrimitiveTopologyChanged = 0;
	inline static volatile LONG RuntimePrimitiveTopologyNoChange = 0;

	inline static volatile LONG IndexBufferCalls = 0;
	inline static volatile LONG IndexBufferChanged = 0;
	inline static volatile LONG IndexBufferNoChange = 0;
	inline static volatile LONG IndexBufferResolvedReleased = 0;
	inline static volatile LONG RuntimeIndexBufferCalls = 0;
	inline static volatile LONG RuntimeIndexBufferChanged = 0;
	inline static volatile LONG RuntimeIndexBufferNoChange = 0;

	inline static volatile LONG VertexBufferCalls = 0;
	inline static volatile LONG64 VertexBufferSlots = 0;
	inline static volatile LONG64 VertexBufferChangedSlots = 0;
	inline static volatile LONG64 VertexBufferNoChangeSlots = 0;
	inline static volatile LONG VertexBufferResolvedReleased = 0;
	inline static volatile LONG RuntimeVertexBufferCalls = 0;
	inline static volatile LONG64 RuntimeVertexBufferSlots = 0;
	inline static volatile LONG64 RuntimeVertexBufferChangedSlots = 0;
	inline static volatile LONG64 RuntimeVertexBufferNoChangeSlots = 0;

	inline static volatile LONG BarrierCaptureCalls = 0;
	inline static volatile LONG64 BarrierCaptureItems = 0;
	inline static volatile LONG BarrierCaptureSkippedInternalReplay = 0;
	inline static volatile LONG BarrierCaptureSkippedHotPath = 0;
	inline static volatile LONG BarrierCaptureSkippedStatePolicy = 0;
	inline static volatile LONG BarrierRecordedCalls = 0;
	inline static volatile LONG64 BarrierRecordedItems = 0;
	inline static volatile LONG BarrierUnwrapCalls = 0;
	inline static volatile LONG64 BarrierUnwrapItems = 0;

	inline static volatile LONG PreSkinBindingCacheCalls = 0;
	inline static volatile LONG PreSkinBindingCacheHits = 0;
	inline static volatile LONG PreSkinBindingCacheMisses = 0;
	inline static volatile LONG PreSkinBindingCacheStores = 0;
	inline static volatile LONG64 PreSkinBindingRootTables = 0;
	inline static volatile LONG64 PreSkinBindingRanges = 0;
	inline static volatile LONG64 PreSkinBindingLightDescriptorLookups = 0;
	inline static volatile LONG64 PreSkinBindingFullDescriptorLookups = 0;
	inline static volatile LONG64 PreSkinBindingConstants = 0;
	inline static volatile LONG64 PreSkinBindingRootDescriptors = 0;
	inline static volatile LONG64 PreSkinBindingRuntimeResolved = 0;
	inline static volatile LONG64 PreSkinBindingRuntimeUnresolved = 0;
	inline static volatile LONG64 PreSkinBindingDescriptorHits = 0;
	inline static volatile LONG64 PreSkinBindingDescriptorMisses = 0;
	inline static volatile LONG64 PreSkinBindingDescriptorPending = 0;
	inline static volatile LONG64 PreSkinBindingWaitAvoided = 0;
	inline static volatile LONG64 RuntimeDescriptorWrites = 0;
	inline static volatile LONG64 RuntimeDescriptorCopyCalls = 0;
	inline static volatile LONG64 RuntimeDescriptorCopied = 0;
	inline static volatile LONG64 RuntimeDescriptorCopyMisses = 0;

	inline static volatile LONG DispatchDecisionCacheHits = 0;
	inline static volatile LONG DispatchDecisionCacheMisses = 0;
	inline static volatile LONG DispatchDecisionBuilds = 0;
	inline static volatile LONG DispatchDecisionNoCompute = 0;
	inline static volatile LONG DispatchDecisionNoPreSkinCandidate = 0;
	inline static volatile LONG DispatchDecisionPreSkinCandidate = 0;
	inline static volatile LONG DispatchDecisionPreSkinCandidateDispatches = 0;
	inline static volatile LONG DispatchDecisionShaderOverrideWork = 0;
	inline static volatile LONG DispatchDecisionFastRejects = 0;

	static void Inc(volatile LONG *value)
	{
		InterlockedIncrement(value);
	}

	static void Add64(volatile LONG64 *value, LONG64 amount)
	{
		if (amount)
			InterlockedExchangeAdd64(value, amount);
	}

	static LONG Take(volatile LONG *value)
	{
		return InterlockedExchange(value, 0);
	}

	static LONG64 Take64(volatile LONG64 *value)
	{
		return InterlockedExchange64(value, 0);
	}

	static void RecordPsoCall()
	{
		Inc(&PsoCalls);
	}

	static void RecordPsoChanged()
	{
		Inc(&PsoChanged);
	}

	static void RecordPsoNoChange()
	{
		Inc(&PsoNoChange);
	}

	static void RecordRuntimePso(bool changed)
	{
		Inc(&RuntimePsoCalls);
		if (changed)
			Inc(&RuntimePsoChanged);
		else
			Inc(&RuntimePsoNoChange);
	}

	static void RecordGraphicsRootSignature(bool changed)
	{
		Inc(&GraphicsRootSignatureCalls);
		if (changed)
			Inc(&GraphicsRootSignatureChanged);
		else
			Inc(&GraphicsRootSignatureNoChange);
	}

	static void RecordComputeRootSignature(bool changed)
	{
		Inc(&ComputeRootSignatureCalls);
		if (changed)
			Inc(&ComputeRootSignatureChanged);
		else
			Inc(&ComputeRootSignatureNoChange);
	}

	static void RecordDescriptorHeapResolve(UINT count)
	{
		Inc(&DescriptorHeapResolveCalls);
		Add64(&DescriptorHeapResolveSlots, static_cast<LONG64>(count));
	}

	static void RecordDescriptorHeapsChanged(
		UINT graphicsInvalidated,
		UINT computeInvalidated)
	{
		Inc(&DescriptorHeapCalls);
		Inc(&DescriptorHeapChanged);
		Add64(&DescriptorHeapInvalidatedGraphicsTables, static_cast<LONG64>(graphicsInvalidated));
		Add64(&DescriptorHeapInvalidatedComputeTables, static_cast<LONG64>(computeInvalidated));
	}

	static void RecordDescriptorHeapsNoChange()
	{
		Inc(&DescriptorHeapCalls);
		Inc(&DescriptorHeapNoChange);
	}

	static void RecordGraphicsRootTableCall()
	{
		Inc(&GraphicsRootTableCalls);
	}

	static void RecordGraphicsRootTableTlsHit()
	{
		Inc(&GraphicsRootTableTlsHits);
	}

	static void RecordGraphicsRootTableChanged()
	{
		Inc(&GraphicsRootTableChanged);
	}

	static void RecordGraphicsRootTableNoChange()
	{
		Inc(&GraphicsRootTableNoChange);
	}

	static void RecordComputeRootTableCall()
	{
		Inc(&ComputeRootTableCalls);
	}

	static void RecordComputeRootTableTlsHit()
	{
		Inc(&ComputeRootTableTlsHits);
	}

	static void RecordComputeRootTableChanged()
	{
		Inc(&ComputeRootTableChanged);
	}

	static void RecordComputeRootTableNoChange()
	{
		Inc(&ComputeRootTableNoChange);
	}

	static void RecordGraphicsRootDescriptorCall()
	{
		Inc(&GraphicsRootDescriptorCalls);
	}

	static void RecordGraphicsRootDescriptorTlsHit()
	{
		Inc(&GraphicsRootDescriptorTlsHits);
	}

	static void RecordGraphicsRootDescriptorChanged()
	{
		Inc(&GraphicsRootDescriptorChanged);
	}

	static void RecordGraphicsRootDescriptorNoChange()
	{
		Inc(&GraphicsRootDescriptorNoChange);
	}

	static void RecordComputeRootDescriptorCall()
	{
		Inc(&ComputeRootDescriptorCalls);
	}

	static void RecordComputeRootDescriptorTlsHit()
	{
		Inc(&ComputeRootDescriptorTlsHits);
	}

	static void RecordComputeRootDescriptorChanged()
	{
		Inc(&ComputeRootDescriptorChanged);
	}

	static void RecordComputeRootDescriptorNoChange()
	{
		Inc(&ComputeRootDescriptorNoChange);
	}

	static void RecordGraphicsRootConstants(UINT count)
	{
		Inc(&GraphicsRootConstantsCalls);
		Add64(&GraphicsRootConstantsValues, static_cast<LONG64>(count));
	}

	static void RecordComputeRootConstants(UINT count)
	{
		Inc(&ComputeRootConstantsCalls);
		Add64(&ComputeRootConstantsValues, static_cast<LONG64>(count));
	}

	static void RecordPrimitiveTopology(bool changed)
	{
		Inc(&PrimitiveTopologyCalls);
		if (changed)
			Inc(&PrimitiveTopologyChanged);
		else
			Inc(&PrimitiveTopologyNoChange);
	}

	static void RecordRuntimePrimitiveTopology(bool changed)
	{
		Inc(&RuntimePrimitiveTopologyCalls);
		if (changed)
			Inc(&RuntimePrimitiveTopologyChanged);
		else
			Inc(&RuntimePrimitiveTopologyNoChange);
	}

	static void RecordIndexBuffer(bool changed)
	{
		Inc(&IndexBufferCalls);
		if (changed)
			Inc(&IndexBufferChanged);
		else
			Inc(&IndexBufferNoChange);
	}

	static void RecordIndexBufferResolvedReleased()
	{
		Inc(&IndexBufferResolvedReleased);
	}

	static void RecordRuntimeIndexBuffer(bool changed)
	{
		Inc(&RuntimeIndexBufferCalls);
		if (changed)
			Inc(&RuntimeIndexBufferChanged);
		else
			Inc(&RuntimeIndexBufferNoChange);
	}

	static void RecordVertexBufferCall(UINT slots)
	{
		Inc(&VertexBufferCalls);
		Add64(&VertexBufferSlots, static_cast<LONG64>(slots));
	}

	static void RecordVertexBufferSlot(bool changed)
	{
		if (changed)
			Add64(&VertexBufferChangedSlots, 1);
		else
			Add64(&VertexBufferNoChangeSlots, 1);
	}

	static void RecordVertexBufferResolvedReleased()
	{
		Inc(&VertexBufferResolvedReleased);
	}

	static void RecordRuntimeVertexBufferCall(UINT slots)
	{
		Inc(&RuntimeVertexBufferCalls);
		Add64(&RuntimeVertexBufferSlots, static_cast<LONG64>(slots));
	}

	static void RecordRuntimeVertexBufferSlot(bool changed)
	{
		if (changed)
			Add64(&RuntimeVertexBufferChangedSlots, 1);
		else
			Add64(&RuntimeVertexBufferNoChangeSlots, 1);
	}

	static void RecordBarrierCaptureCall(UINT count)
	{
		Inc(&BarrierCaptureCalls);
		Add64(&BarrierCaptureItems, static_cast<LONG64>(count));
	}

	static void RecordBarrierCaptureSkippedInternalReplay()
	{
		Inc(&BarrierCaptureSkippedInternalReplay);
	}

	static void RecordBarrierCaptureSkippedHotPath()
	{
		Inc(&BarrierCaptureSkippedHotPath);
	}

	static void RecordBarrierCaptureSkippedStatePolicy()
	{
		Inc(&BarrierCaptureSkippedStatePolicy);
	}

	static void RecordBarrierRecorded(UINT count)
	{
		Inc(&BarrierRecordedCalls);
		Add64(&BarrierRecordedItems, static_cast<LONG64>(count));
	}

	static void RecordBarrierUnwrap(UINT count)
	{
		Inc(&BarrierUnwrapCalls);
		Add64(&BarrierUnwrapItems, static_cast<LONG64>(count));
	}

	static void RecordPreSkinBindingCacheCall()
	{
		Inc(&PreSkinBindingCacheCalls);
	}

	static void RecordPreSkinBindingCacheHit()
	{
		Inc(&PreSkinBindingCacheHits);
	}

	static void RecordPreSkinBindingCacheMiss()
	{
		Inc(&PreSkinBindingCacheMisses);
	}

	static void RecordPreSkinBindingCacheStore()
	{
		Inc(&PreSkinBindingCacheStores);
	}

	static void RecordPreSkinBindingRootTable()
	{
		Add64(&PreSkinBindingRootTables, 1);
	}

	static void RecordPreSkinBindingRange()
	{
		Add64(&PreSkinBindingRanges, 1);
	}

	static void RecordPreSkinBindingLightDescriptorLookup()
	{
		Add64(&PreSkinBindingLightDescriptorLookups, 1);
	}

	static void RecordPreSkinBindingFullDescriptorLookup()
	{
		Add64(&PreSkinBindingFullDescriptorLookups, 1);
	}

	static void RecordPreSkinBindingConstant()
	{
		Add64(&PreSkinBindingConstants, 1);
	}

	static void RecordPreSkinBindingRootDescriptor()
	{
		Add64(&PreSkinBindingRootDescriptors, 1);
	}

	static void RecordPreSkinBindingRuntimeTable(bool resolved)
	{
		if (resolved)
			Add64(&PreSkinBindingRuntimeResolved, 1);
		else
			Add64(&PreSkinBindingRuntimeUnresolved, 1);
	}

	static void RecordPreSkinBindingDescriptorLookupResult(
		LONG64 hitCount,
		LONG64 missCount,
		LONG64 pendingCount)
	{
		Add64(&PreSkinBindingDescriptorHits, hitCount);
		Add64(&PreSkinBindingDescriptorMisses, missCount);
		Add64(&PreSkinBindingDescriptorPending, pendingCount);
		Add64(&PreSkinBindingWaitAvoided, hitCount + missCount + pendingCount);
	}

	static void RecordRuntimeDescriptorWrite()
	{
		Add64(&RuntimeDescriptorWrites, 1);
	}

	static void RecordRuntimeDescriptorCopy(UINT copied, UINT misses)
	{
		Add64(&RuntimeDescriptorCopyCalls, 1);
		Add64(&RuntimeDescriptorCopied, static_cast<LONG64>(copied));
		Add64(&RuntimeDescriptorCopyMisses, static_cast<LONG64>(misses));
	}

	static void RecordDispatchDecisionCacheHit()
	{
		Inc(&DispatchDecisionCacheHits);
	}

	static void RecordDispatchDecisionCacheMiss()
	{
		Inc(&DispatchDecisionCacheMisses);
	}

	static void RecordDispatchDecisionBuild()
	{
		Inc(&DispatchDecisionBuilds);
	}

	static void RecordDispatchDecisionNoCompute()
	{
		Inc(&DispatchDecisionNoCompute);
	}

	static void RecordDispatchDecisionNoPreSkinCandidate()
	{
		Inc(&DispatchDecisionNoPreSkinCandidate);
	}

	static void RecordDispatchDecisionPreSkinCandidate()
	{
		Inc(&DispatchDecisionPreSkinCandidate);
	}

	static void RecordDispatchDecisionPreSkinCandidateDispatch()
	{
		Inc(&DispatchDecisionPreSkinCandidateDispatches);
	}

	static void RecordDispatchDecisionShaderOverrideWork()
	{
		Inc(&DispatchDecisionShaderOverrideWork);
	}

	static void RecordDispatchDecisionFastReject()
	{
		Inc(&DispatchDecisionFastRejects);
	}

	static void LogPso()
	{
		LONG psoCalls = Take(&PsoCalls);
		LONG psoChanged = Take(&PsoChanged);
		LONG psoNoChange = Take(&PsoNoChange);
		LONG runtimePsoCalls = Take(&RuntimePsoCalls);
		LONG runtimePsoChanged = Take(&RuntimePsoChanged);
		LONG runtimePsoNoChange = Take(&RuntimePsoNoChange);
		if (!psoCalls && !psoChanged && !psoNoChange &&
			!runtimePsoCalls && !runtimePsoChanged && !runtimePsoNoChange)
			return;
		DX12LogManager::JsonFunc("DX12ResidentMaintenance",
			"\"category\":\"pso\",\"trackerCalls\":%ld,\"trackerChanged\":%ld,\"trackerNoChange\":%ld,\"runtimeCalls\":%ld,\"runtimeChanged\":%ld,\"runtimeNoChange\":%ld",
			psoCalls, psoChanged, psoNoChange,
			runtimePsoCalls, runtimePsoChanged, runtimePsoNoChange);
	}

	static void LogRootState()
	{
		LONG graphicsRootSignatureCalls = Take(&GraphicsRootSignatureCalls);
		LONG graphicsRootSignatureChanged = Take(&GraphicsRootSignatureChanged);
		LONG graphicsRootSignatureNoChange = Take(&GraphicsRootSignatureNoChange);
		LONG computeRootSignatureCalls = Take(&ComputeRootSignatureCalls);
		LONG computeRootSignatureChanged = Take(&ComputeRootSignatureChanged);
		LONG computeRootSignatureNoChange = Take(&ComputeRootSignatureNoChange);
		LONG graphicsRootTableCalls = Take(&GraphicsRootTableCalls);
		LONG graphicsRootTableTlsHits = Take(&GraphicsRootTableTlsHits);
		LONG graphicsRootTableChanged = Take(&GraphicsRootTableChanged);
		LONG graphicsRootTableNoChange = Take(&GraphicsRootTableNoChange);
		LONG computeRootTableCalls = Take(&ComputeRootTableCalls);
		LONG computeRootTableTlsHits = Take(&ComputeRootTableTlsHits);
		LONG computeRootTableChanged = Take(&ComputeRootTableChanged);
		LONG computeRootTableNoChange = Take(&ComputeRootTableNoChange);
		if (!graphicsRootSignatureCalls && !computeRootSignatureCalls &&
			!graphicsRootTableCalls && !computeRootTableCalls)
			return;
		DX12LogManager::JsonFunc("DX12ResidentMaintenance",
			"\"category\":\"root\",\"gfxSigCalls\":%ld,\"gfxSigChanged\":%ld,\"gfxSigNoChange\":%ld,\"compSigCalls\":%ld,\"compSigChanged\":%ld,\"compSigNoChange\":%ld,\"gfxTableCalls\":%ld,\"gfxTableTlsHits\":%ld,\"gfxTableChanged\":%ld,\"gfxTableNoChange\":%ld,\"compTableCalls\":%ld,\"compTableTlsHits\":%ld,\"compTableChanged\":%ld,\"compTableNoChange\":%ld",
			graphicsRootSignatureCalls, graphicsRootSignatureChanged, graphicsRootSignatureNoChange,
			computeRootSignatureCalls, computeRootSignatureChanged, computeRootSignatureNoChange,
			graphicsRootTableCalls, graphicsRootTableTlsHits, graphicsRootTableChanged, graphicsRootTableNoChange,
			computeRootTableCalls, computeRootTableTlsHits, computeRootTableChanged, computeRootTableNoChange);
	}

	static void LogDescriptorState()
	{
		LONG descriptorHeapResolveCalls = Take(&DescriptorHeapResolveCalls);
		LONG64 descriptorHeapResolveSlots = Take64(&DescriptorHeapResolveSlots);
		LONG descriptorHeapCalls = Take(&DescriptorHeapCalls);
		LONG descriptorHeapChanged = Take(&DescriptorHeapChanged);
		LONG descriptorHeapNoChange = Take(&DescriptorHeapNoChange);
		LONG64 descriptorHeapInvalidatedGraphicsTables = Take64(&DescriptorHeapInvalidatedGraphicsTables);
		LONG64 descriptorHeapInvalidatedComputeTables = Take64(&DescriptorHeapInvalidatedComputeTables);
		LONG graphicsRootDescriptorCalls = Take(&GraphicsRootDescriptorCalls);
		LONG graphicsRootDescriptorTlsHits = Take(&GraphicsRootDescriptorTlsHits);
		LONG graphicsRootDescriptorChanged = Take(&GraphicsRootDescriptorChanged);
		LONG graphicsRootDescriptorNoChange = Take(&GraphicsRootDescriptorNoChange);
		LONG computeRootDescriptorCalls = Take(&ComputeRootDescriptorCalls);
		LONG computeRootDescriptorTlsHits = Take(&ComputeRootDescriptorTlsHits);
		LONG computeRootDescriptorChanged = Take(&ComputeRootDescriptorChanged);
		LONG computeRootDescriptorNoChange = Take(&ComputeRootDescriptorNoChange);
		LONG graphicsRootConstantsCalls = Take(&GraphicsRootConstantsCalls);
		LONG64 graphicsRootConstantsValues = Take64(&GraphicsRootConstantsValues);
		LONG computeRootConstantsCalls = Take(&ComputeRootConstantsCalls);
		LONG64 computeRootConstantsValues = Take64(&ComputeRootConstantsValues);
		if (!descriptorHeapResolveCalls && !descriptorHeapCalls &&
			!graphicsRootDescriptorCalls && !computeRootDescriptorCalls &&
			!graphicsRootConstantsCalls && !computeRootConstantsCalls)
			return;
		DX12LogManager::JsonFunc("DX12ResidentMaintenance",
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
		LONG primitiveTopologyCalls = Take(&PrimitiveTopologyCalls);
		LONG primitiveTopologyChanged = Take(&PrimitiveTopologyChanged);
		LONG primitiveTopologyNoChange = Take(&PrimitiveTopologyNoChange);
		LONG runtimePrimitiveTopologyCalls = Take(&RuntimePrimitiveTopologyCalls);
		LONG runtimePrimitiveTopologyChanged = Take(&RuntimePrimitiveTopologyChanged);
		LONG runtimePrimitiveTopologyNoChange = Take(&RuntimePrimitiveTopologyNoChange);
		LONG indexBufferCalls = Take(&IndexBufferCalls);
		LONG indexBufferChanged = Take(&IndexBufferChanged);
		LONG indexBufferNoChange = Take(&IndexBufferNoChange);
		LONG indexBufferResolvedReleased = Take(&IndexBufferResolvedReleased);
		LONG runtimeIndexBufferCalls = Take(&RuntimeIndexBufferCalls);
		LONG runtimeIndexBufferChanged = Take(&RuntimeIndexBufferChanged);
		LONG runtimeIndexBufferNoChange = Take(&RuntimeIndexBufferNoChange);
		LONG vertexBufferCalls = Take(&VertexBufferCalls);
		LONG64 vertexBufferSlots = Take64(&VertexBufferSlots);
		LONG64 vertexBufferChangedSlots = Take64(&VertexBufferChangedSlots);
		LONG64 vertexBufferNoChangeSlots = Take64(&VertexBufferNoChangeSlots);
		LONG vertexBufferResolvedReleased = Take(&VertexBufferResolvedReleased);
		LONG runtimeVertexBufferCalls = Take(&RuntimeVertexBufferCalls);
		LONG64 runtimeVertexBufferSlots = Take64(&RuntimeVertexBufferSlots);
		LONG64 runtimeVertexBufferChangedSlots = Take64(&RuntimeVertexBufferChangedSlots);
		LONG64 runtimeVertexBufferNoChangeSlots = Take64(&RuntimeVertexBufferNoChangeSlots);
		if (!primitiveTopologyCalls && !runtimePrimitiveTopologyCalls &&
			!indexBufferCalls && !runtimeIndexBufferCalls &&
			!vertexBufferCalls && !runtimeVertexBufferCalls)
			return;
		DX12LogManager::JsonFunc("DX12ResidentMaintenance",
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
		LONG barrierCaptureCalls = Take(&BarrierCaptureCalls);
		LONG64 barrierCaptureItems = Take64(&BarrierCaptureItems);
		LONG barrierCaptureSkippedInternalReplay = Take(&BarrierCaptureSkippedInternalReplay);
		LONG barrierCaptureSkippedHotPath = Take(&BarrierCaptureSkippedHotPath);
		LONG barrierCaptureSkippedStatePolicy = Take(&BarrierCaptureSkippedStatePolicy);
		LONG barrierRecordedCalls = Take(&BarrierRecordedCalls);
		LONG64 barrierRecordedItems = Take64(&BarrierRecordedItems);
		LONG barrierUnwrapCalls = Take(&BarrierUnwrapCalls);
		LONG64 barrierUnwrapItems = Take64(&BarrierUnwrapItems);
		if (!barrierCaptureCalls && !barrierRecordedCalls && !barrierUnwrapCalls)
			return;
		DX12LogManager::JsonFunc("DX12ResidentMaintenance",
			"\"category\":\"barrier\",\"captureCalls\":%ld,\"captureItems\":%lld,\"captureSkippedInternalReplay\":%ld,\"captureSkippedHotPath\":%ld,\"captureSkippedStatePolicy\":%ld,\"recordedCalls\":%ld,\"recordedItems\":%lld,\"unwrapCalls\":%ld,\"unwrapItems\":%lld",
			barrierCaptureCalls, static_cast<long long>(barrierCaptureItems),
			barrierCaptureSkippedInternalReplay, barrierCaptureSkippedHotPath,
			barrierCaptureSkippedStatePolicy,
			barrierRecordedCalls, static_cast<long long>(barrierRecordedItems),
			barrierUnwrapCalls, static_cast<long long>(barrierUnwrapItems));
	}

	static void LogPreSkinBindingState()
	{
		LONG preSkinBindingCacheCalls = Take(&PreSkinBindingCacheCalls);
		LONG preSkinBindingCacheHits = Take(&PreSkinBindingCacheHits);
		LONG preSkinBindingCacheMisses = Take(&PreSkinBindingCacheMisses);
		LONG preSkinBindingCacheStores = Take(&PreSkinBindingCacheStores);
		LONG64 preSkinBindingRootTables = Take64(&PreSkinBindingRootTables);
		LONG64 preSkinBindingRanges = Take64(&PreSkinBindingRanges);
		LONG64 preSkinBindingLightDescriptorLookups = Take64(&PreSkinBindingLightDescriptorLookups);
		LONG64 preSkinBindingFullDescriptorLookups = Take64(&PreSkinBindingFullDescriptorLookups);
		LONG64 preSkinBindingConstants = Take64(&PreSkinBindingConstants);
		LONG64 preSkinBindingRootDescriptors = Take64(&PreSkinBindingRootDescriptors);
		LONG64 preSkinBindingRuntimeResolved = Take64(&PreSkinBindingRuntimeResolved);
		LONG64 preSkinBindingRuntimeUnresolved = Take64(&PreSkinBindingRuntimeUnresolved);
		LONG64 preSkinBindingDescriptorHits = Take64(&PreSkinBindingDescriptorHits);
		LONG64 preSkinBindingDescriptorMisses = Take64(&PreSkinBindingDescriptorMisses);
		LONG64 preSkinBindingDescriptorPending = Take64(&PreSkinBindingDescriptorPending);
		LONG64 preSkinBindingWaitAvoided = Take64(&PreSkinBindingWaitAvoided);
		LONG64 runtimeDescriptorWrites = Take64(&RuntimeDescriptorWrites);
		LONG64 runtimeDescriptorCopyCalls = Take64(&RuntimeDescriptorCopyCalls);
		LONG64 runtimeDescriptorCopied = Take64(&RuntimeDescriptorCopied);
		LONG64 runtimeDescriptorCopyMisses = Take64(&RuntimeDescriptorCopyMisses);
		if (!preSkinBindingCacheCalls && !preSkinBindingCacheHits &&
			!preSkinBindingCacheMisses && !preSkinBindingCacheStores &&
			!preSkinBindingRuntimeResolved && !preSkinBindingRuntimeUnresolved &&
			!preSkinBindingWaitAvoided && !runtimeDescriptorWrites &&
			!runtimeDescriptorCopyCalls)
			return;
		DX12LogManager::JsonFunc("DX12ResidentMaintenance",
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

	static void LogDispatchDecisionState()
	{
		LONG cacheHits = Take(&DispatchDecisionCacheHits);
		LONG cacheMisses = Take(&DispatchDecisionCacheMisses);
		LONG builds = Take(&DispatchDecisionBuilds);
		LONG noCompute = Take(&DispatchDecisionNoCompute);
		LONG noPreSkinCandidate = Take(&DispatchDecisionNoPreSkinCandidate);
		LONG preSkinCandidate = Take(&DispatchDecisionPreSkinCandidate);
		LONG preSkinCandidateDispatches = Take(&DispatchDecisionPreSkinCandidateDispatches);
		LONG shaderOverrideWork = Take(&DispatchDecisionShaderOverrideWork);
		LONG fastRejects = Take(&DispatchDecisionFastRejects);
		if (!cacheHits && !cacheMisses && !builds && !noCompute &&
			!noPreSkinCandidate && !preSkinCandidate && !preSkinCandidateDispatches &&
			!shaderOverrideWork && !fastRejects)
			return;
		DX12LogManager::JsonFunc("DX12ResidentMaintenance",
			"\"category\":\"dispatch_decision\",\"cacheHits\":%ld,\"cacheMisses\":%ld,\"builds\":%ld,\"noCompute\":%ld,\"noPreSkinCandidate\":%ld,\"preSkinCandidateBuilds\":%ld,\"preSkinCandidateDispatches\":%ld,\"shaderOverrideWork\":%ld,\"fastRejects\":%ld",
			cacheHits, cacheMisses, builds, noCompute,
			noPreSkinCandidate, preSkinCandidate, preSkinCandidateDispatches,
			shaderOverrideWork, fastRejects);
	}

	static void LogAndReset()
	{
		LogPso();
		LogRootState();
		LogDescriptorState();
		LogIaState();
		LogBarrierState();
		LogPreSkinBindingState();
		LogDispatchDecisionState();
	}
};
