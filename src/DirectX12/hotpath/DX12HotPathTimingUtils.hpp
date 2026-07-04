#pragma once

#include <Windows.h>

#include <atomic>

#include "DX12LogManager.hpp"

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
		// volatile 读替代 CAS 读 — 此值仅用于日志统计，无需原子性
		LONG oldMax = acc.maxUs;
		while (us > oldMax)
			oldMax = InterlockedCompareExchange(&acc.maxUs, us, oldMax);
	}

	// ── Snapshot + reset: called at frame end (Present time) ─────────
	static void LogAndResetAll()
	{
		Accumulator snap[DX12HotPathStage::STAGE_COUNT];
		for (UINT i = 0; i < DX12HotPathStage::STAGE_COUNT; ++i) {
			snap[i] = tlsAccum[i];
			tlsAccum[i] = Accumulator{};
		}

		// Only log if anything was measured this frame
		bool anyActive = false;
		for (UINT i = 0; i < DX12HotPathStage::STAGE_COUNT; ++i) {
			if (snap[i].callCount > 0) {
				anyActive = true;
				break;
			}
		}
		if (!anyActive)
			return;

		for (UINT i = 0; i < DX12HotPathStage::STAGE_COUNT; ++i) {
			const Accumulator &acc = snap[i];
			if (acc.callCount <= 0)
				continue;
			LONG64 totalUs = (acc.qpcSum.QuadPart * 1000000) / tlsFreq.QuadPart;
			LONG avgUs = static_cast<LONG>(totalUs / acc.callCount);
			DX12LogManager::JsonFunc("DX12HotPathTiming",
				"\"stage\":\"%s\",\"calls\":%ld,\"totalUs\":%lld,\"avgUs\":%ld,\"maxUs\":%ld",
				DX12HotPathStage::Name(static_cast<DX12HotPathStage::Id>(i)),
				acc.callCount,
				static_cast<long long>(totalUs),
				avgUs,
				acc.maxUs);
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

#else // !_DEBUG

#define DX12_HOTPATH_TIME_ENABLED 0
#define DX12_HOTPATH_TIME_BEGIN(stage) do {} while (0)
#define DX12_HOTPATH_TIME_END(stage) do {} while (0)
#define DX12_HOTPATH_TIME_SCOPE(stage) do {} while (0)
#define DX12_HOTPATH_LOG_AND_RESET() do {} while (0)

#endif
