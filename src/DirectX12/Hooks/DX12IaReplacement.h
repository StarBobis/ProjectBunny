#pragma once

// IA replacement executors, buffer-view utils, redirect utils, PSO fast-reject.
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
		// A deferred draw-SRV restore may still have a patched mirror heap
		// bound on this list; the original draw must observe the game's own
		// bindings. No-op probe when nothing is pending (and after a
		// successful ApplyPending, which supersedes or borrows the entry).
		DX12ModFlushDrawSrvReplacement(commandList);

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
		// Same deferred-restore guard as ExecuteOriginalDraw: replacement
		// work recorded without a successful ApplyPending (e.g. a failed
		// apply with handling=skip) must not execute against the stale
		// patched mirror. Probe is a no-op after a successful apply.
		DX12ModFlushDrawSrvReplacement(commandList);

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
			resource.gpuVirtualAddress > sourceView.BufferLocation) {
			return false;
		}

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
						cached.StrideInBytes == sourceView.StrideInBytes) {
					continue;
				}
			}
			if (ExpandInPlaceVertexBufferView(sourceView, &redirectedViews[i])) {
				if (activeIa && slot < D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT)
					activeIa->preSkinRedirectNegativeValid[slot] = false;
				anyRedirected = true;
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
			// The VA may resolve into the pre-skin output range while this
			// slot is a DIFFERENT stream packed inside the same resource
			// (e.g. texcoords/normals beyond the producer UAV view). Only
			// redirect views whose stride matches the pre-skin output layout;
			// anything else must keep reading the original buffer.
			if (binding.stride && sourceView.StrideInBytes &&
			    binding.stride != sourceView.StrideInBytes) {
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
			if (!availableBytes) {
				continue;
			}
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
		if (liveIa.preSkinReplayApplied &&
		    liveIa.preSkinReplayGeneration == replayGeneration &&
		    liveIa.preSkinReplayIaFingerprint ==
		    	ComputeIaViewFingerprint(liveIa)) {
			return false;
		}
		const bool replayed = ReplayActiveVertexBuffers(
			commandList, callbacks, redirectedIa);
		// The replay wrote its per-slot "no redirect needed" verdicts into the
		// working copy, which ApplyAndExecute discards when it returns - so the
		// negative cache never survived past the first draw of the frame, and
		// every frame's first matching draw re-ran the full 32-slot lookup
		// chain (~0.4-0.8ms/frame, measured). Mirror the verdicts back onto
		// the live state; Reset preserves them (view-keyed, generation-guarded),
		// so the next frame's replay hits them.
		for (UINT slot = 0;
		     slot < ARRAYSIZE(liveIa.preSkinRedirectNegativeValid); ++slot) {
			liveIa.preSkinRedirectNegativeValid[slot] =
				redirectedIa->preSkinRedirectNegativeValid[slot];
			liveIa.preSkinRedirectNegativeGeneration[slot] =
				redirectedIa->preSkinRedirectNegativeGeneration[slot];
			liveIa.preSkinRedirectNegativeSource[slot] =
				redirectedIa->preSkinRedirectNegativeSource[slot];
		}
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
		const DX12IaReplacementExecutorCallbacks &callbacks,
		UINT64 flagsWord = DX12HotPathState::HookWorkFlags)
	{
		if (!commandList || !runtimeState ||
			!(flagsWord & DX12HotPathState::FLAG_PRESENT_REPLACEMENT) ||
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
#if defined(DX12_RELEASE_TIMING)
		// TIMING-build trace: dump the exact replacement state that is about
		// to be executed (skip flag, replacement IB view and every queued
		// draw), so "the ini appended the right branch but the picture did
		// not change" cases can be distinguished between "original draw was
		// not skipped", "replacement IB was not bound" and "GPU args were
		// wrong". Compiled out of regular Release builds.
		DX12LogManager::Fields("DX12IaReplayDiag",
			"event=execute_match skip=%d hasIb=%d ibSize=%u ibFmt=%d draws=%zu dispatches=%zu origIdx=%u origFirst=%u origBase=%d origInst=%u",
			replacement->skip ? 1 : 0,
			replacement->hasIndexBuffer ? 1 : 0,
			replacement->hasIndexBuffer ? replacement->indexBuffer.SizeInBytes : 0u,
			replacement->hasIndexBuffer ? static_cast<int>(replacement->indexBuffer.Format) : 0,
			replacement->draws.size(), replacement->dispatches.size(),
			draw.indexCount, draw.firstIndex, draw.baseVertex, draw.instanceCount);
		for (const DX12ModIaReplacement::DrawCall &queuedDraw : replacement->draws) {
			DX12LogManager::Fields("DX12IaReplayDiag",
				"event=execute_draw indexed=%d count=%u start=%u base=%d fromCaller=%d autoCount=%d",
				queuedDraw.indexed ? 1 : 0, queuedDraw.count, queuedDraw.start,
				queuedDraw.baseVertex, queuedDraw.fromCaller ? 1 : 0,
				queuedDraw.autoCount ? 1 : 0);
		}
		if (!replacement->skip)
			DX12LogManager::Fields("DX12IaReplayDiag", "event=execute_original_draw");
#endif

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
		// Diagnostic sub-probes: apply_replay_ia measures ~1ms per matching draw
		// while the replay body itself never runs; split the block to find the
		// real cost (flags vs IA copy vs gated call).
		const bool hasResizeBindings = DX12ModHasPreSkinOutputResizeBindings();
		const bool hasResizeCacheEntries =
			DX12ModHasPreSkinOutputResizeCacheEntries();
		const bool mayNeedExpansion = DX12ModMayNeedPreSkinIaViewExpansion(
			runtimeState.ia.vertexBuffers,
			runtimeState.ia.hasVertexBuffer,
			D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT);
		DX12OuterTimingProbeUtils::LogCpuStage(
			"IaReplacement", "replay_cond_flags", draw.indexed ? 1u : 0u, 1,
			DX12OuterTimingProbeUtils::ElapsedUs(applySegmentBegin));
		if (hasResizeBindings || hasResizeCacheEntries || mayNeedExpansion) {
			LARGE_INTEGER replayCopyBegin = DX12OuterTimingProbeUtils::Now();
			redirectedIa = runtimeState.ia;
			DX12OuterTimingProbeUtils::LogCpuStage(
				"IaReplacement", "replay_ia_copy", draw.indexed ? 1u : 0u, 1,
				DX12OuterTimingProbeUtils::ElapsedUs(replayCopyBegin));
			LARGE_INTEGER replayGatedBegin = DX12OuterTimingProbeUtils::Now();
			DX12PreSkinIaRedirectUtils::ReplayActiveVertexBuffersGated(
				commandList, callbacks, &redirectedIa, &runtimeState);
			DX12OuterTimingProbeUtils::LogCpuStage(
				"IaReplacement", "replay_gated_call", draw.indexed ? 1u : 0u, 1,
				DX12OuterTimingProbeUtils::ElapsedUs(replayGatedBegin));
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
		const DX12CompiledDrawDecision *precompiledDrawDecision = nullptr,
		UINT64 flagsWord = DX12HotPathState::HookWorkFlags)
	{
		DX12_HOTPATH_TIME_BEGIN(DrawOverrideTotal);

		// Draw consumes the compiled PSO decision instead of rediscovering
		// shader override metadata on every command.
		DX12_HOTPATH_TIME_BEGIN(DrawPsoEarlyExit);
		LARGE_INTEGER psoDecisionBegin = DX12OuterTimingProbeUtils::Now();
			// Gate bits come from the flags word the outer draw hook already
			// loaded: one snapshot per draw keeps every layer's gating
			// consistent even if an input-thread toggle lands mid-draw.
			const bool activeShaderOverrides =
				(flagsWord & DX12HotPathState::FLAG_SHADER_OVERRIDES) != 0;
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
			(flagsWord &
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


