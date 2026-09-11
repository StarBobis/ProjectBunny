#pragma once

// Mod IA replacement interface and pre-skin producer summary.
// Split out of Core/DX12ModState.h; include that umbrella header from existing code.
#include "../DX12CommandFlow.h"

// Custom shader element spec model. Records carry a full copy so a reload
// that swaps the element snapshot can never invalidate an element while its
// replacement is still pending.
#include "../../ModRuntime/DX12CustomShaderElement.h"

class DX12ModPreSkinProducerSummary
{
public:
	UINT rootParameterIndex = 0;
	UINT rangeIndex = UINT_MAX;
	UINT shaderRegister = UINT_MAX;
	UINT registerSpace = 0;
	UINT descriptorOffset = 0;
	bool rootDescriptor = false;
	UINT64 gpuVirtualAddress = 0;
	DX12DescriptorSummary descriptor;
	bool hasDescriptor = false;
	UINT64 outputByteWidth = 0;
	UINT outputStride = 0;
};
bool DX12ModFindActivePreSkinProducerForOutput(
	ID3D12Resource *outputResource, UINT64 outputByteWidth, UINT outputStride,
	DX12ModPreSkinProducerSummary *summary);

class DX12ModIaReplacement
{
public:
	DX12ModIaReplacement() = default;
	DX12ModIaReplacement(const DX12ModIaReplacement &other)
		: skip(other.skip),
		  hasIndexBuffer(other.hasIndexBuffer),
		  indexBuffer(other.indexBuffer),
		  vertexBufferStartSlot(other.vertexBufferStartSlot),
		  vertexBuffers(other.vertexBuffers),
		  queuedPostTextureOverrides(other.queuedPostTextureOverrides),
		  postCommandLists(other.postCommandLists),
		  draws(other.draws),
		  dispatches(other.dispatches),
		  drawSrvPatches(other.drawSrvPatches),
		  drawSrvApplyState(other.drawSrvApplyState),
		  retainedDescriptorHeaps(other.retainedDescriptorHeaps),
		  customElementRecords(other.customElementRecords)
	{
		for (ID3D12Resource *resource : other.retainedResources)
			RetainResource(resource);
		for (ID3D12DescriptorHeap *heap : retainedDescriptorHeaps) {
			if (heap)
				heap->AddRef();
		}
	}
	DX12ModIaReplacement(DX12ModIaReplacement &&other) noexcept
		: skip(other.skip),
		  hasIndexBuffer(other.hasIndexBuffer),
		  indexBuffer(other.indexBuffer),
		  vertexBufferStartSlot(other.vertexBufferStartSlot),
		  vertexBuffers(std::move(other.vertexBuffers)),
		  queuedPostTextureOverrides(std::move(other.queuedPostTextureOverrides)),
		  postCommandLists(std::move(other.postCommandLists)),
		  draws(std::move(other.draws)),
		  dispatches(std::move(other.dispatches)),
		  drawSrvPatches(std::move(other.drawSrvPatches)),
		  drawSrvApplyState(std::move(other.drawSrvApplyState)),
		  retainedResources(std::move(other.retainedResources)),
		  retainedDescriptorHeaps(std::move(other.retainedDescriptorHeaps)),
		  customElementRecords(std::move(other.customElementRecords))
	{
		other.retainedResources.clear();
	}
	~DX12ModIaReplacement()
	{
		ClearRetainedResources();
	}
	DX12ModIaReplacement &operator=(const DX12ModIaReplacement &other)
	{
		if (this == &other)
			return *this;
		ClearRetainedResources();
		skip = other.skip;
		hasIndexBuffer = other.hasIndexBuffer;
		indexBuffer = other.indexBuffer;
		vertexBufferStartSlot = other.vertexBufferStartSlot;
		vertexBuffers = other.vertexBuffers;
		queuedPostTextureOverrides = other.queuedPostTextureOverrides;
		postCommandLists = other.postCommandLists;
		draws = other.draws;
		dispatches = other.dispatches;
		drawSrvPatches = other.drawSrvPatches;
		drawSrvApplyState = other.drawSrvApplyState;
		retainedDescriptorHeaps = other.retainedDescriptorHeaps;
		customElementRecords = other.customElementRecords;
		for (ID3D12Resource *resource : other.retainedResources)
			RetainResource(resource);
		for (ID3D12DescriptorHeap *heap : retainedDescriptorHeaps) {
			if (heap)
				heap->AddRef();
		}
		return *this;
	}
	DX12ModIaReplacement &operator=(DX12ModIaReplacement &&other) noexcept
	{
		if (this == &other)
			return *this;
		ClearRetainedResources();
		skip = other.skip;
		hasIndexBuffer = other.hasIndexBuffer;
		indexBuffer = other.indexBuffer;
		vertexBufferStartSlot = other.vertexBufferStartSlot;
		vertexBuffers = std::move(other.vertexBuffers);
		queuedPostTextureOverrides = std::move(other.queuedPostTextureOverrides);
		postCommandLists = std::move(other.postCommandLists);
		draws = std::move(other.draws);
		dispatches = std::move(other.dispatches);
		drawSrvPatches = std::move(other.drawSrvPatches);
		drawSrvApplyState = std::move(other.drawSrvApplyState);
		retainedResources = std::move(other.retainedResources);
		retainedDescriptorHeaps = std::move(other.retainedDescriptorHeaps);
		customElementRecords = std::move(other.customElementRecords);
		other.retainedResources.clear();
		return *this;
	}
	void RetainResource(ID3D12Resource *resource)
	{
		if (!resource)
			return;
		resource->AddRef();
		retainedResources.push_back(resource);
	}
	void AddPostCommandList(const std::wstring &list)
	{
		if (list.empty())
			return;
		if (std::find(postCommandLists.begin(), postCommandLists.end(), list) !=
		    postCommandLists.end())
			return;
		postCommandLists.push_back(list);
	}
	class QueuedTextureOverridePost
	{
	public:
		const Bunny::TextureOverrideConfig *config = nullptr;
		bool hasThisSrvTarget = false;
		bool compute = false;
		DX12CurrentShaderResourceBinding thisSrvBinding;
		uint32_t matchedHash = 0;
		uint32_t resourceHash = 0;
		uint32_t descriptorHash = 0;
	};
	void AddQueuedPostTextureOverride(const QueuedTextureOverridePost &queued)
	{
		if (!queued.config)
			return;
		for (const QueuedTextureOverridePost &existing : queuedPostTextureOverrides) {
			if (existing.config != queued.config ||
			    existing.hasThisSrvTarget != queued.hasThisSrvTarget ||
			    existing.compute != queued.compute ||
			    existing.matchedHash != queued.matchedHash ||
			    existing.resourceHash != queued.resourceHash ||
			    existing.descriptorHash != queued.descriptorHash) {
				continue;
			}
			if (!existing.hasThisSrvTarget)
				return;
			if (existing.thisSrvBinding.rootParameterIndex ==
			        queued.thisSrvBinding.rootParameterIndex &&
			    existing.thisSrvBinding.shaderRegister ==
			        queued.thisSrvBinding.shaderRegister &&
			    existing.thisSrvBinding.registerSpace ==
			        queued.thisSrvBinding.registerSpace &&
			    existing.thisSrvBinding.descriptorOffset ==
			        queued.thisSrvBinding.descriptorOffset &&
			    existing.thisSrvBinding.tableGpuStart.ptr ==
			        queued.thisSrvBinding.tableGpuStart.ptr) {
				return;
			}
		}
		queuedPostTextureOverrides.push_back(queued);
	}
	void ClearRetainedResources()
	{
		for (ID3D12Resource *resource : retainedResources) {
			if (resource)
				resource->Release();
		}
		retainedResources.clear();
		for (ID3D12DescriptorHeap *heap : retainedDescriptorHeaps) {
			if (heap)
				heap->Release();
		}
		retainedDescriptorHeaps.clear();
	}

	// Returns the object to its default-constructed state for reuse while
	// keeping the originalBindings payload untouched. That payload is
	// only ever read after a fresh assignment or move into it: the readers are
	// Restore's logical-sync and DeferPendingRestore
	// (DX12DrawSrvReplacementUtils.h), which both require
	// applied && patchHeap && !borrowedPendingMirror, and `applied` is only set
	// right after originalBindings is assigned. This is the same marker-only
	// invariant the resets in DX12CommandListRuntime/DX12BindingTracker use.
	// (The payload is a DX12DrawSrvBindingSnapshot, not a full
	// CommandListBindingState, so skipping it is now a small win rather than a
	// ~60KB one - the marker-only form is kept because it is still correct.)
	void ResetForDraw()
	{
		ClearRetainedResources();
		skip = false;
		hasIndexBuffer = false;
		indexBuffer = D3D12_INDEX_BUFFER_VIEW();
		vertexBufferStartSlot = 0;
		vertexBuffers.clear();
		queuedPostTextureOverrides.clear();
		postCommandLists.clear();
		draws.clear();
		dispatches.clear();
		drawSrvPatches.clear();
		drawSrvApplyState.ResetForDraw();
		customElementRecords.clear();
	}

	bool skip = false;
	bool hasIndexBuffer = false;
	D3D12_INDEX_BUFFER_VIEW indexBuffer = {};
	UINT vertexBufferStartSlot = 0;
	std::vector<D3D12_VERTEX_BUFFER_VIEW> vertexBuffers;
	std::vector<QueuedTextureOverridePost> queuedPostTextureOverrides;
	std::vector<std::wstring> postCommandLists;
	class DrawCall
	{
public:
		bool indexed = false;
		UINT count = 0;
		UINT start = 0;
		INT baseVertex = 0;
		bool fromCaller = false;
		// `drawindexed = auto`: count is recomputed from the replaced index
		// buffer view at execution time instead of using a literal value.
		bool autoCount = false;
	};
	class DispatchCall
	{
	public:
		UINT groupsX = 0;
		UINT groupsY = 0;
		UINT groupsZ = 0;
	};
	std::vector<DrawCall> draws;
	std::vector<DispatchCall> dispatches;
	class DrawSrvTablePatch
	{
	public:
		DX12CurrentShaderResourceBinding binding;
		D3D12_CPU_DESCRIPTOR_HANDLE replacementCpu = {};
		DXGI_FORMAT replacementViewFormat = DXGI_FORMAT_UNKNOWN;
		std::wstring resourceName;
		std::string source;
		bool compute = false;
		UINT rootParameterIndex = 0;
		D3D12_GPU_DESCRIPTOR_HANDLE originalTable = {};
		D3D12_CPU_DESCRIPTOR_HANDLE targetCpu = {};
	};
	std::vector<DrawSrvTablePatch> drawSrvPatches;
	class DrawSrvBundleRestore
	{
	public:
		UINT rootParameterIndex = 0;
		bool compute = false;
		// Physical handle emitted into the bundle to re-establish the pre-draw
		// table (the session mirror of the logical game table).
		D3D12_GPU_DESCRIPTOR_HANDLE emitTable = {};
		// Logical game table the shadow trackers must keep: bundle bindings
		// stay in game terms while the recorded stream uses mirror handles.
		D3D12_GPU_DESCRIPTOR_HANDLE trackTable = {};
	};
	class DrawSrvApplyState
	{
	public:
		bool applied = false;
		// This draw borrowed the still-bound mirror of the pending deferred
		// restore (see DX12DrawSrvReplacementUtils.h): ApplyPending emitted
		// nothing and Restore only releases the borrow.
		bool borrowedPendingMirror = false;
		// Applied inside a bundle (see DX12BundleSrvMirror.h): patch copies in
		// the bundle's mirror session are bound for the draw and Restore
		// rebinds the session mirrors immediately; no deferred restore exists
		// on bundle lists.
		bool bundleMode = false;
		ID3D12DescriptorHeap *patchHeap = nullptr;
		// Plan identity of the emitted mirror; a pending deferred restore
		// reuses the still-bound mirror when the next apply's fingerprint
		// and reload generation match.
		UINT64 planFingerprint = 0;
		UINT64 reloadGeneration = 0;
		// Slim view of the game's bindings: only what the logical/physical
		// restore halves read. See DX12DrawSrvBindingSnapshot.
		DX12DrawSrvBindingSnapshot originalBindings;
		std::vector<DrawSrvBundleRestore> bundleRestores;

		// Clears every field except the originalBindings payload; see
		// DX12ModIaReplacement::ResetForDraw for why that payload may stay.
		void ResetForDraw()
		{
			applied = false;
			borrowedPendingMirror = false;
			bundleMode = false;
			patchHeap = nullptr;
			planFingerprint = 0;
			reloadGeneration = 0;
			bundleRestores.clear();
		}
	};
	DrawSrvApplyState drawSrvApplyState;
	std::vector<ID3D12Resource*> retainedResources;
	std::vector<ID3D12DescriptorHeap*> retainedDescriptorHeaps;

	// One deferred custom shader element invocation recorded by the mod
	// executor (phase 2.4): a full copy of the element spec, the ini params
	// float4 payload, the texture slots the body binds and the recorded draw
	// arguments. Parked by DX12ModQueuePresentCustomElements and replayed at
	// Present time by DX12ModSubmitPresentOverlay.
	class CustomElementRecord
	{
	public:
		Bunny::DX12CustomShaderElementSpec spec;
		std::vector<float> iniParams; // kIniParamsFloat4Count * 4 floats
		std::vector<std::pair<UINT, std::wstring>> textureSlots;
		UINT drawCount = 0;
		UINT drawStart = 0;
	};
	std::vector<CustomElementRecord> customElementRecords;

	void AddCustomElementRecord(const CustomElementRecord &record)
	{
		if (record.spec.section.empty())
			return;
		customElementRecords.push_back(record);
	}
};
