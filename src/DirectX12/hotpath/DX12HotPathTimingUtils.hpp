#pragma once

#include <Windows.h>

#include <atomic>

#include "DX12LogManager.hpp"
#include "utils/DX12CreateTimingTelemetryUtils.hpp"
#include "utils/DX12DescriptorShadowTelemetryUtils.hpp"
#include "utils/DX12ResidentMaintenanceTelemetryUtils.hpp"

// ── Hot-path stage timing ───────────────────────────────────────────
// Thread-local accumulators. QPC-based. Logged per-frame at Present.
// Only active in _DEBUG. Zero overhead in release (empty inline stubs).
//
// Usage:
//   DX12_HOTPATH_TIME_BEGIN(DrawHook);
//   ... work ...
//   DX12_HOTPATH_TIME_END(DrawHook);

#if defined(_DEBUG)

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
