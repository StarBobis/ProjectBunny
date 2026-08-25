#pragma once

// Hot-path stage timing accumulators and DX12_HOTPATH_TIME_* macros.
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

// Hot-path stage timing.
// Thread-local accumulators. QPC-based. Logged per-frame at Present.
// Detailed per-call QPC timing is opt-in, including in debug builds.
// Define DX12_ENABLE_DETAILED_TIMING when collecting timing diagnostics.
//
// Usage:
//   DX12_HOTPATH_TIME_BEGIN(DrawHook);
//   ... work ...
//   DX12_HOTPATH_TIME_END(DrawHook);

#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)

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

#else // !(DX12_ENABLE_DETAILED_TIMING && DX12_RELEASE_TIMING)

#define DX12_HOTPATH_TIME_ENABLED 0

#endif
