#pragma once

#include "Commands/DX12CommandServices.hpp"

namespace Bunny {
class TextureOverrideConfig;
}

bool DX12ModRuntimeLoad(
	const wchar_t *configPath,
	std::wstring *statusMessage = nullptr,
	bool *hasWarnings = nullptr,
	bool *hasErrors = nullptr);
bool DX12ModRuntimeReload();
void DX12ModRefreshRuntimeStateAfterReload();
void DX12ModSetSafeMode(bool safeMode);
uint64_t DX12ModHashShaderBytecode(const void *data, size_t size);
bool DX12ModReplaceShaderBytecode(
	const char *stage, const D3D12_SHADER_BYTECODE &source,
	D3D12_SHADER_BYTECODE *replacement, std::vector<unsigned char> *storage);
bool DX12ModHasShaderOverride(uint64_t hash);
bool DX12ModHasActiveShaderOverrides();
bool DX12ModHasShaderOverridesForPso(ID3D12PipelineState *pipelineState);
bool DX12ModCollectShaderOverrideIaHashTargets(
	ID3D12PipelineState *pipelineState,
	DX12IaHashTargetPlan *targetPlan);
bool DX12ModCompileDrawDecision(
	ID3D12PipelineState *pipelineState,
	DX12CompiledDrawDecision *decision);
bool DX12ModNeedsShaderDescriptorTracking();
UINT64 DX12ModGetReloadGeneration();
UINT64 DX12ModGetReloadGenerationFast();
UINT64 DX12ModGetPreSkinActiveGenerationFast();
bool DX12ModHasActiveTextureOverrides();
bool DX12ModHasConfiguredTextureOverrides();
bool DX12ModHasAnyActiveOverrides();
bool DX12ModNeedsPresentReplacement();
bool DX12ModNeedsPreSkinningUavProbe();
bool DX12ModNeedsPreSkinningBindingTracking();
bool DX12ModNeedsCommandListSubmissionNotifications();
void DX12ModRunPresentMaintenance();
bool DX12ModHasActivePreSkinTextureOverrides();
bool DX12ModShouldProbePreSkinningForCs(uint64_t computeShaderHash);
bool DX12ModShouldTrackPreSkinBindingsForCs(uint64_t computeShaderHash);
bool DX12ModPreSkinningUavProducerMayMatch(
	uint64_t computeShaderHash,
	const std::vector<DX12CurrentComputeUavBinding> &uavs);
class DX12PreSkinDispatchOverride;
bool DX12ModHasCachedPreSkinningUavMatch(
	ID3D12GraphicsCommandList *commandList,
	uint64_t computeShaderHash,
	UINT64 computeBindingSerial,
	bool *matched);
void DX12ModStoreCachedPreSkinningUavMatch(
	ID3D12GraphicsCommandList *commandList,
	uint64_t computeShaderHash,
	UINT64 computeBindingSerial,
	bool matched,
	const DX12PreSkinDispatchOverride *dispatchOverride = nullptr);

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
		  retainedDescriptorHeaps(other.retainedDescriptorHeaps)
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
		  retainedDescriptorHeaps(std::move(other.retainedDescriptorHeaps))
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
	class DrawSrvApplyState
	{
	public:
		bool applied = false;
		ID3D12DescriptorHeap *patchHeap = nullptr;
		CommandListBindingState originalBindings;
	};
	DrawSrvApplyState drawSrvApplyState;
	std::vector<ID3D12Resource*> retainedResources;
	std::vector<ID3D12DescriptorHeap*> retainedDescriptorHeaps;
};
bool DX12ModPrepareShaderOverrideReplacement(
	ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState,
	bool hasActiveShaderOverrides,
	const DX12IaHashState &iaState,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance,
	const DX12DrawMatchUsage &drawMatchUsage,
	DX12ModIaReplacement *replacement,
	bool allowNegativeCache = true,
	ID3D12Device *cachedDevice = nullptr,
	bool skipDirectDescriptorTargets = false,
	std::shared_ptr<const DX12ModIaReplacement> *generatedStaticReplacement = nullptr);
bool DX12ModTryLoadStaticReplacementForPso(
	ID3D12PipelineState *pipelineState,
	UINT64 reloadGeneration,
	DX12ModIaReplacement *replacement);
bool DX12ModApplyDrawSrvReplacement(
	ID3D12GraphicsCommandList *commandList,
	DX12ModIaReplacement *replacement,
	bool executeOriginalDraw);
bool DX12ModHasAnyDirectIaTextureOverrideCandidate(
	const DX12IaHashState &iaState,
	const DX12IaHashTargetPlan &targetPlan,
	bool indexed,
	uint32_t vertexCount,
	uint32_t indexCount,
	uint32_t instanceCount,
	uint32_t firstVertex,
	uint32_t firstIndex,
	uint32_t firstInstance);
bool DX12ModHasAnyDirectDescriptorTextureOverrideCandidate(
	ID3D12GraphicsCommandList *commandList,
	const DX12IaHashTargetPlan &targetPlan,
	uint32_t vertexCount,
	uint32_t indexCount,
	uint32_t instanceCount,
	uint32_t firstVertex,
	uint32_t firstIndex,
	uint32_t firstInstance);
bool DX12ModHasAnyPotentialDirectIaTextureOverrideCandidate(
	const DX12ActiveIaState &ia,
	const DX12IaHashTargetPlan &targetPlan,
	bool indexed,
	uint32_t vertexCount,
	uint32_t indexCount,
	uint32_t instanceCount,
	uint32_t firstVertex,
	uint32_t firstIndex,
	uint32_t firstInstance);
void DX12ModRunPostShaderOverrideReplacement(
	ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState,
	const DX12IaHashState &iaState,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance,
	DX12ModIaReplacement *replacement);
void DX12ModRestoreDrawSrvReplacement(
	ID3D12GraphicsCommandList *commandList,
	DX12ModIaReplacement *replacement);
void DX12ModClearIaReplacementRuntimeCaches();
void DX12ModRecordGraphicsPipelineState(
	ID3D12Device *device, ID3D12PipelineState *pipelineState,
	const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc);
void DX12ModRecordComputePipelineState(
	ID3D12Device *device, ID3D12PipelineState *pipelineState,
	const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc);
ID3D12PipelineState *DX12ModGetReplacementPipelineState(ID3D12PipelineState *pipelineState);
bool DX12ModShouldSkipPipelineState(ID3D12PipelineState *pipelineState, bool dispatch);
void DX12ModRecordComputeUavs(
	ID3D12GraphicsCommandList *commandList, const std::vector<DX12CurrentComputeUavBinding> &uavs);
class DX12PreSkinDispatchOverride
{
public:
	bool handlingSkip = false;
	bool hasDispatch = false;
	UINT groupsX = 0;
	UINT groupsY = 0;
	UINT groupsZ = 0;
};
class DX12PreSkinProbeCacheEntry
{
public:
	bool applied = false;
	DX12PreSkinDispatchOverride dispatchOverride;
};
bool DX12ModGetCachedPreSkinProbeResult(
	ID3D12GraphicsCommandList *commandList,
	uint64_t computeShaderHash,
	UINT64 computeBindingSerial,
	DX12PreSkinProbeCacheEntry *result);
bool DX12ModApplyPreSkinningUavReplacement(
	ID3D12GraphicsCommandList *commandList,
	uint64_t computeShaderHash,
	const std::vector<DX12CurrentComputeUavBinding> &uavs,
	const std::vector<DX12CurrentComputeUavBinding> &srvs,
	const std::vector<DX12CurrentComputeUavBinding> &cbvs,
	const std::vector<DX12CurrentRootConstants> &rootConstants,
	UINT *originalVertexCount = nullptr,
	UINT *overrideVertexCount = nullptr,
	DX12PreSkinDispatchOverride *dispatchOverride = nullptr,
	ID3D12Device *cachedDevice = nullptr,
	UINT64 bindingFingerprint = 0);
bool DX12ModApplyKnownPreSkinningUavPatches(ID3D12GraphicsCommandList *commandList);
bool DX12ModCanDeferPreSkinningUavReplacement(
	ID3D12GraphicsCommandList *commandList);
bool DX12ModHasActivePreSkinningUavReplacement();
void DX12ModRestorePreSkinningUavReplacement(ID3D12GraphicsCommandList *commandList);
void DX12ModNotifyCommandListsSubmitted(
	ID3D12CommandQueue *queue, UINT numCommandLists, ID3D12CommandList *const *commandLists);
void DX12ModBeginFrame(IDXGISwapChain *swapChain);
bool DX12ModPresentReplacementPendingForCurrentPresent();
bool DX12ModPreparePresentReplacement(
	ID3D12GraphicsCommandList *commandList, DX12ModIaReplacement *replacement);
bool DX12ModAdjustBufferResourceDesc(D3D12_RESOURCE_DESC *desc, const char *source);
bool DX12ModAdjustBufferResourceDesc1(D3D12_RESOURCE_DESC1 *desc, const char *source);
bool DX12ModAdjustUavDesc(
	ID3D12Resource *resource, D3D12_UNORDERED_ACCESS_VIEW_DESC *desc, const char *source);
bool DX12ModResolveExpandedPreSkinBufferWidth(
	ID3D12Resource *resource, UINT vertexStride, UINT64 *outputByteWidth);

// PreSkin DynamicUAV lifetime contract:
// - the GPU-VA publish map is only a fast, frame-local producer handoff;
// - the resize cache is the durable, fence-tracked owner across Present/FG;
// - every returned binding retains its vertex resource until Clear().
// A consumer command list may be recorded after Present has cleared the publish
// map, so callers must preserve the cache fallback below.
class DX12PreSkinOutputResizeBinding
{
public:
	DX12PreSkinOutputResizeBinding() = default;
	DX12PreSkinOutputResizeBinding(const DX12PreSkinOutputResizeBinding &) = delete;
	DX12PreSkinOutputResizeBinding &operator=(const DX12PreSkinOutputResizeBinding &) = delete;
	~DX12PreSkinOutputResizeBinding()
	{
		Clear();
	}

	void Clear()
	{
		if (retainedResource && resizedResource)
			resizedResource->Release();
		resizedResource = nullptr;
		retainedResource = false;
		originalGpuVa = 0;
		originalByteWidth = 0;
		resizedGpuVa = 0;
		resizedByteWidth = 0;
		sourceOffset = 0;
		stride = 0;
		uavBarrierPending = false;
		transitionToVertex = false;
		stateBefore = D3D12_RESOURCE_STATE_COMMON;
	}

	ID3D12Resource *resizedResource = nullptr;
	bool retainedResource = false;
	UINT64 originalGpuVa = 0;
	UINT64 originalByteWidth = 0;
	UINT64 resizedGpuVa = 0;
	UINT64 resizedByteWidth = 0;
	UINT64 sourceOffset = 0;
	UINT stride = 0;
	bool uavBarrierPending = false;
	bool transitionToVertex = false;
	D3D12_RESOURCE_STATES stateBefore = D3D12_RESOURCE_STATE_COMMON;
};
bool DX12ModResolvePreSkinOutputResizeCacheByGpuVa(
	ID3D12GraphicsCommandList *commandList,
	UINT64 viewGpuVa, DX12PreSkinOutputResizeBinding *binding);
void DX12ModRegisterPreSkinOutputResize(
	ID3D12GraphicsCommandList *commandList,
	ID3D12Resource *originalResource, ID3D12Resource *resizedResource,
	UINT64 originalByteWidth, UINT64 resizedByteWidth, UINT stride);
bool DX12ModResolvePreSkinOutputResizeByGpuVa(
	ID3D12GraphicsCommandList *commandList,
	UINT64 viewGpuVa, DX12PreSkinOutputResizeBinding *binding);
bool DX12ModHasPreSkinOutputResizeBindings();
UINT64 DX12ModGetPreSkinOutputResizeGeneration();
void DX12ModForgetPreSkinOutputResizesForCommandList(
	ID3D12GraphicsCommandList *commandList);
void DX12ModAbandonPreSkinOutputResizeCacheUses(
	ID3D12GraphicsCommandList *commandList);
void DX12ModTrackPreSkinOutputResizeCacheUse(
	ID3D12GraphicsCommandList *commandList,
	ID3D12Resource *vertexResource);
void DX12ModClearPreSkinOutputResizeMap();




class DX12PreSkinPlanUtils
{
public:
	static uint64_t HashCombine(uint64_t hash, uint64_t value)
	{
		hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
		return hash;
	}

	static uint64_t HashString(uint64_t hash, const std::string &value)
	{
		for (size_t index = 0; index < value.size(); ++index) {
			hash = HashCombine(hash, static_cast<unsigned char>(value[index]));
		}
		return hash;
	}

	static uint64_t AppendDescriptor(uint64_t hash, const DX12DescriptorSummary &descriptor)
	{
		hash = HashString(hash, descriptor.kind);
		hash = HashCombine(hash, descriptor.hasDesc ? 1 : 0);
		hash = HashCombine(hash, reinterpret_cast<uint64_t>(descriptor.resource));
		hash = HashCombine(hash, reinterpret_cast<uint64_t>(descriptor.counterResource));
		hash = HashCombine(hash, descriptor.gpuVirtualAddress);
		hash = HashCombine(hash, descriptor.resourceOffset);
		hash = HashCombine(hash, descriptor.viewSize);
		hash = HashCombine(hash, descriptor.hasResourceHeapType ? 1 : 0);
		hash = HashCombine(hash, descriptor.hasResourceHeapType ?
			descriptor.resourceHeapType : 0);
		hash = HashCombine(hash, descriptor.viewFormat);
		hash = HashCombine(hash, descriptor.viewDimension);
		hash = HashCombine(hash, descriptor.cbvSize);
		hash = HashCombine(hash, descriptor.firstElement);
		hash = HashCombine(hash, descriptor.numElements);
		hash = HashCombine(hash, descriptor.structureByteStride);
		hash = HashCombine(hash, descriptor.bufferViewOffset);
		hash = HashCombine(hash, descriptor.bufferViewBytes);
		if (descriptor.hasResourceDesc) {
			hash = HashCombine(hash, descriptor.resourceDesc.Dimension);
			hash = HashCombine(hash, descriptor.resourceDesc.Alignment);
			hash = HashCombine(hash, descriptor.resourceDesc.Width);
			hash = HashCombine(hash, descriptor.resourceDesc.Height);
			hash = HashCombine(hash, descriptor.resourceDesc.DepthOrArraySize);
			hash = HashCombine(hash, descriptor.resourceDesc.MipLevels);
			hash = HashCombine(hash, descriptor.resourceDesc.Format);
			hash = HashCombine(hash, descriptor.resourceDesc.Layout);
			hash = HashCombine(hash, descriptor.resourceDesc.Flags);
		}
		return hash;
	}

	static bool BindingLess(
		const DX12CurrentComputeUavBinding &left,
		const DX12CurrentComputeUavBinding &right)
	{
		if (left.rootParameterIndex != right.rootParameterIndex)
			return left.rootParameterIndex < right.rootParameterIndex;
		if (left.registerSpace != right.registerSpace)
			return left.registerSpace < right.registerSpace;
		if (left.shaderRegister != right.shaderRegister)
			return left.shaderRegister < right.shaderRegister;
		if (left.rangeIndex != right.rangeIndex)
			return left.rangeIndex < right.rangeIndex;
		if (left.descriptorOffset != right.descriptorOffset)
			return left.descriptorOffset < right.descriptorOffset;
		if (left.rootDescriptor != right.rootDescriptor)
			return left.rootDescriptor < right.rootDescriptor;
		return left.gpuVirtualAddress < right.gpuVirtualAddress;
	}

	static uint64_t AppendBinding(
		uint64_t hash,
		const DX12CurrentComputeUavBinding &binding,
		uint64_t listSalt)
	{
		hash = HashCombine(hash, listSalt);
		hash = HashCombine(hash, binding.rootParameterIndex);
		hash = HashCombine(hash, binding.rangeIndex);
		hash = HashCombine(hash, binding.shaderVisibility);
		hash = HashCombine(hash, binding.shaderRegister);
		hash = HashCombine(hash, binding.registerSpace);
		hash = HashCombine(hash, binding.descriptorOffset);
		hash = HashCombine(hash, binding.rootDescriptor ? 1 : 0);
		hash = HashCombine(hash, binding.gpuVirtualAddress);
		hash = HashCombine(hash, binding.hasDescriptor ? 1 : 0);
		if (binding.hasDescriptor) {
			hash = AppendDescriptor(hash, binding.descriptor);
		} else {
			hash = HashCombine(hash, binding.rootDescriptor ? 0x726f6f7464657363ull : 0x756e7265736f6c76ull);
		}
		return hash;
	}

	static uint64_t AppendBindingList(
		uint64_t hash,
		const std::vector<DX12CurrentComputeUavBinding> &bindings,
		uint64_t listSalt)
	{
		std::vector<DX12CurrentComputeUavBinding> sortedBindings = bindings;
		std::sort(
			sortedBindings.begin(),
			sortedBindings.end(),
			DX12PreSkinPlanUtils::BindingLess);
		hash = HashCombine(hash, sortedBindings.size());
		for (size_t index = 0; index < sortedBindings.size(); ++index) {
			hash = AppendBinding(hash, sortedBindings[index], listSalt);
		}
		return hash;
	}

	static bool RootConstantsLess(
		const DX12CurrentRootConstants &left,
		const DX12CurrentRootConstants &right)
	{
		return left.rootParameterIndex < right.rootParameterIndex;
	}

	static uint64_t AppendRootConstants(
		uint64_t hash,
		const std::vector<DX12CurrentRootConstants> &rootConstants)
	{
		std::vector<DX12CurrentRootConstants> sortedConstants = rootConstants;
		std::sort(
			sortedConstants.begin(),
			sortedConstants.end(),
			DX12PreSkinPlanUtils::RootConstantsLess);
		hash = HashCombine(hash, sortedConstants.size());
		for (size_t rootIndex = 0; rootIndex < sortedConstants.size(); ++rootIndex) {
			const DX12CurrentRootConstants &root = sortedConstants[rootIndex];
			hash = HashCombine(hash, root.rootParameterIndex);
			hash = HashCombine(hash, root.maxSet);
			for (UINT valueIndex = 0; valueIndex < 64; ++valueIndex) {
				if (!root.valueValid[valueIndex])
					continue;
				hash = HashCombine(hash, valueIndex);
				hash = HashCombine(hash, root.values[valueIndex]);
			}
		}
		return hash;
	}

	static uint64_t BuildStableBindingFingerprint(
		const std::vector<DX12CurrentComputeUavBinding> &uavs,
		const std::vector<DX12CurrentComputeUavBinding> &srvs,
		const std::vector<DX12CurrentComputeUavBinding> &cbvs,
		const std::vector<DX12CurrentRootConstants> &rootConstants)
	{
		uint64_t hash = 0x7a4d17e6b90f8c35ull;
		hash = AppendBindingList(hash, uavs, 0x5541565full);
		hash = AppendBindingList(hash, srvs, 0x5352565full);
		hash = AppendBindingList(hash, cbvs, 0x4342565full);
		hash = AppendRootConstants(hash, rootConstants);
		if (hash == 0)
			hash = 1;
		return hash;
	}

	// Cross-frame stable fingerprint: uses only fields that survive across frames.
	// Strips GPU VA, resource pointer, resourceOffset, viewSize, and other per-
	// frame-volatile fields. The result is safe because different resources with
	// the same shader binding pattern produce identical PreSkin replacement results.
	static uint64_t AppendStableDescriptor(uint64_t hash, const DX12DescriptorSummary &descriptor)
	{
		hash = HashString(hash, descriptor.kind);
		hash = HashCombine(hash, descriptor.hasDesc ? 1 : 0);
		hash = HashCombine(hash, descriptor.viewFormat);
		hash = HashCombine(hash, descriptor.viewDimension);
		if (descriptor.hasResourceDesc) {
			hash = HashCombine(hash, descriptor.resourceDesc.Dimension);
			hash = HashCombine(hash, descriptor.resourceDesc.Width);
			hash = HashCombine(hash, descriptor.resourceDesc.Height);
			hash = HashCombine(hash, descriptor.resourceDesc.DepthOrArraySize);
			hash = HashCombine(hash, descriptor.resourceDesc.MipLevels);
			hash = HashCombine(hash, descriptor.resourceDesc.Format);
			hash = HashCombine(hash, descriptor.resourceDesc.Layout);
			hash = HashCombine(hash, descriptor.resourceDesc.Flags);
		}
		return hash;
	}

	static uint64_t AppendStableBinding(
		uint64_t hash,
		const DX12CurrentComputeUavBinding &binding,
		uint64_t listSalt)
	{
		hash = HashCombine(hash, listSalt);
		hash = HashCombine(hash, binding.rootParameterIndex);
		hash = HashCombine(hash, binding.rangeIndex);
		hash = HashCombine(hash, binding.shaderVisibility);
		hash = HashCombine(hash, binding.shaderRegister);
		hash = HashCombine(hash, binding.registerSpace);
		hash = HashCombine(hash, binding.rootDescriptor ? 1 : 0);
		if (binding.hasDescriptor) {
			hash = AppendStableDescriptor(hash, binding.descriptor);
		}
		return hash;
	}

	static uint64_t AppendStableBindingList(
		uint64_t hash,
		const std::vector<DX12CurrentComputeUavBinding> &bindings,
		uint64_t listSalt)
	{
		static thread_local std::vector<uint64_t> bindingHashes;
		bindingHashes.clear();
		if (bindingHashes.capacity() < bindings.size())
			bindingHashes.reserve(bindings.size());
		for (const DX12CurrentComputeUavBinding &binding : bindings) {
			bindingHashes.push_back(AppendStableBinding(
				0xc3a5c85c97cb3127ull,
				binding,
				listSalt));
		}
		std::sort(bindingHashes.begin(), bindingHashes.end());
		hash = HashCombine(hash, bindingHashes.size());
		for (uint64_t bindingHash : bindingHashes)
			hash = HashCombine(hash, bindingHash);
		return hash;
	}

	static uint64_t BuildCrossFrameStableBindingFingerprint(
		const std::vector<DX12CurrentComputeUavBinding> &uavs,
		const std::vector<DX12CurrentComputeUavBinding> &srvs,
		const std::vector<DX12CurrentComputeUavBinding> &cbvs,
		const std::vector<DX12CurrentRootConstants> &rootConstants)
	{
		uint64_t hash = 0xc5f3e8a17d24906bull;
		hash = AppendStableBindingList(hash, uavs, 0x5541565full);
		hash = AppendStableBindingList(hash, srvs, 0x5352565full);
		hash = AppendStableBindingList(hash, cbvs, 0x4342565full);
		hash = AppendRootConstants(hash, rootConstants);
		if (hash == 0)
			hash = 1;
		return hash;
	}
};




void DX12RecordPsoRootSignature(
	UINT64 psoIndex, const char *kind, ID3D12PipelineState *pipelineState,
	ID3D12RootSignature *rootSignature);
void DX12RecordResourceBarrier(UINT numBarriers, const D3D12_RESOURCE_BARRIER *barriers);
bool DX12ResolveBufferResourceByGpuVa(
	UINT64 gpuVirtualAddress, UINT64 size, DX12BufferResourceSummary *summary);
const char *DX12DiagnoseBufferResolveByGpuVa(
	UINT64 gpuVirtualAddress, UINT64 size,
	UINT64 *nearestBegin, UINT64 *nearestEnd, UINT64 *nearestResource);
uint32_t DX12HashBufferResourceView(
	const DX12BufferResourceSummary *summary, UINT64 fallbackGpuVirtualAddress,
	UINT64 fallbackSize);
uint32_t DX12HashBufferView(
	const DX12BufferResourceSummary *summary, UINT64 fallbackGpuVirtualAddress,
	UINT64 fallbackSize, UINT stride, UINT format, UINT slot);
uint32_t DX12HashIaBufferView(
	UINT64 gpuVirtualAddress, UINT64 size, UINT stride, UINT format, UINT slot);
uint32_t DX12HashIaBufferContentView(
	UINT64 gpuVirtualAddress, UINT64 size, UINT stride, UINT format, UINT slot);
bool DX12CacheBufferContent(ID3D12Resource *resource, const void *data, size_t dataSize);
bool DX12CacheBufferContentRange(
	ID3D12Resource *resource, UINT64 resourceOffset, const void *data, size_t dataSize);
bool DX12CopyBufferContentRange(
	ID3D12Resource *dstResource,
	UINT64 dstOffset,
	ID3D12Resource *srcResource,
	UINT64 srcOffset,
	UINT64 numBytes);
bool DX12CopyBufferContent(
	ID3D12Resource *dstResource,
	ID3D12Resource *srcResource);
bool DX12TryGetBufferContent(UINT64 gpuVirtualAddress, UINT64 size, const uint8_t **data, size_t *dataSize);
UINT64 DX12GetBufferContentGenerationForResource(ID3D12Resource *resource);
void DX12InvalidateBufferContentRange(
	ID3D12Resource *resource, UINT64 offset, UINT64 size);
void DX12InvalidateBufferContent(ID3D12Resource *resource);
bool DX12CacheTextureHash(
	ID3D12Resource *resource, uint32_t hash, const D3D12_RESOURCE_DESC *desc, const char *source);
bool DX12TryGetTextureHash(ID3D12Resource *resource, uint32_t *hash);
UINT64 DX12GetTextureHashGeneration();
void DX12InvalidateTextureHash(ID3D12Resource *resource);
bool DX12TrackTextureHashFromCopyTextureRegion(
	const D3D12_TEXTURE_COPY_LOCATION *dst, UINT dstX, UINT dstY, UINT dstZ,
	const D3D12_TEXTURE_COPY_LOCATION *src, const D3D12_BOX *srcBox);
UINT64 DX12GetIaHashContentGeneration();
UINT64 DX12GetIaHashResolveGeneration();
class DX12BufferResolveValidationUtils
{
public:
	static bool IsRangeStillValid(
		UINT64 observedEpoch, UINT64 gpuVirtualAddress, UINT64 size);
};
uint32_t DX12HashDescriptorBufferView(
	const DX12DescriptorSummary *descriptor, UINT64 fallbackGpuVirtualAddress,
	UINT64 fallbackSize);
bool DX12GetRootSignatureSummary(
	ID3D12RootSignature *rootSignature, DX12RootSignatureSummary *summary);
bool DX12GetPsoRootSignature(UINT64 psoIndex, ID3D12RootSignature **rootSignature);
void DX12GetResourceMetadataSnapshot(
	std::vector<DX12RootSignatureSummary> *rootSignatures,
	std::vector<DX12DescriptorSummary> *descriptors,
	std::vector<DX12PsoRootSummary> *psoRoots);
void DX12GetResourceMetadataSnapshot(
	std::vector<DX12RootSignatureSummary> *rootSignatures,
	std::vector<DX12DescriptorSummary> *descriptors,
	std::vector<DX12PsoRootSummary> *psoRoots,
	std::vector<DX12DescriptorHeapSummary> *descriptorHeaps);
void DX12GetTrackedResourceSnapshot(std::vector<DX12ResourceSummary> *resources);
void DX12GetRetainedTrackedResourceSnapshot(std::vector<DX12ResourceSummary> *resources);
void DX12ReleaseTrackedResourceSnapshot(std::vector<DX12ResourceSummary> *resources);
bool DX12RetainResource(ID3D12Resource *resource);
void DX12ReleaseRetainedResource(ID3D12Resource *resource);
bool DX12FindDescriptorSummaryByCpuHandle(
	SIZE_T cpuHandle, DX12DescriptorSummary *summary);
bool DX12FindDescriptorSummaryByCpuHandleLight(
	SIZE_T cpuHandle, DX12DescriptorSummary *summary);
void DX12FindDescriptorSummariesByCpuHandle(
	const SIZE_T *cpuHandles, size_t count,
	DX12DescriptorSummary *summaries, uint8_t *found,
	bool completeResourceInfo = true);
void DX12FindDescriptorSummariesByCpuHandleNoWait(
	const SIZE_T *cpuHandles, size_t count,
	DX12DescriptorSummary *summaries, uint8_t *found,
	uint8_t *pending,
	bool completeResourceInfo = true);
void DX12FindDescriptorSummariesByHeapSlot(
	ID3D12DescriptorHeap *heap,
	const UINT *slots,
	size_t count,
	DX12DescriptorSummary *summaries,
	uint8_t *found,
	bool completeResourceInfo = true);
UINT64 CurrentDescriptorEpoch();
UINT64 CurrentDescriptorQuerySerial();
UINT64 DX12GetDescriptorHeapVersion(ID3D12DescriptorHeap *heap);
bool DX12GetDescriptorSlotVersions(
	ID3D12DescriptorHeap *heap,
	const UINT *slots,
	size_t count,
	UINT64 *versions);
bool DX12GetDescriptorSlotIdentities(
	ID3D12DescriptorHeap *heap,
	const UINT *slots,
	size_t count,
	UINT64 *versions,
	UINT64 *valueIds);
bool FindDescriptorSummaryByCpuHandleAtEpoch(
	SIZE_T cpuHandle, UINT64 epoch, DX12DescriptorSummary *summary);
bool FindDescriptorSummaryByCpuHandleAtEpochAndKind(
	SIZE_T cpuHandle, UINT64 epoch, const char *kind, DX12DescriptorSummary *summary);
void DX12PrimeDescriptorHistoryForHunting();
bool DX12FindDescriptorHeapByPtr(
	ID3D12DescriptorHeap *heap, DX12DescriptorHeapSummary *summary);
bool DX12FindDescriptorHeapByGpuHandle(
	D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE type,
	DX12DescriptorHeapSummary *summary);
bool DX12FindDescriptorHeapByCpuHandle(
	D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE type,
	DX12DescriptorHeapSummary *summary);
void DX12BeginFrameAnalysisMetadataCache();
void DX12EndFrameAnalysisMetadataCache();



void DX12RecordGraphicsPipelineState(
	ID3D12PipelineState *pipelineState, const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc);
void DX12RecordComputePipelineState(
	ID3D12PipelineState *pipelineState, const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc);
void DX12RecordPipelineStateStream(
	ID3D12PipelineState *pipelineState, const D3D12_PIPELINE_STATE_STREAM_DESC *desc);
bool DX12GetPipelineStateShaderInfo(ID3D12PipelineState *pipelineState, DX12PsoShaderInfo *info);
bool DX12GetPsoShaderSummary(UINT64 psoIndex, DX12PsoShaderSummary *summary);
bool DX12GetPsoInputLayout(UINT64 psoIndex, DX12PsoInputLayoutInfo *layout);
void DX12DumpFrameAnalysis();
void DX12DumpCachedShaders();
void DX12RequestShaderDump();
bool DX12ShaderDumpIsCaptureRequested();
void DX12ShaderDumpBeginCapture();
bool DX12ShaderDumpEndCapture();
bool DX12ShaderDumpIsCapturingFrame();
bool DX12ShaderDumpIsBusy();
void DX12DumpCapturedFrameShaders();



class DX12OuterTimingProbeUtils
{
public:
	enum ApiWallBucket
	{
		ApiDrawInstanced = 0,
		ApiDrawIndexedInstanced,
		ApiDispatch,
		ApiExecuteIndirect,
		ApiExecuteCommandLists,
		ApiQueueSignal,
		ApiQueueWait,
		ApiResetCommandList,
		ApiCloseCommandList,
		ApiSetPipelineState,
		ApiResourceBarrier,
		ApiSetDescriptorHeaps,
		ApiSetComputeRootSignature,
		ApiSetGraphicsRootSignature,
		ApiSetComputeRootDescriptorTable,
		ApiSetGraphicsRootDescriptorTable,
		ApiSetComputeRootDescriptor,
		ApiSetGraphicsRootDescriptor,
		ApiSetComputeRootConstants,
		ApiSetGraphicsRootConstants,
		ApiIASetIndexBuffer,
		ApiIASetVertexBuffers,
		ApiIASetPrimitiveTopology,
		ApiCopyBufferRegion,
		ApiCopyTextureRegion,
		ApiCopyResource,
		ApiCopyTiles,
		ApiResolveSubresource,
		ApiClearUnorderedAccessView,
		ApiOtherCommandList,
		ApiDeviceCommandCreate,
		ApiDevicePsoCreate,
		ApiDeviceDescriptorHeapCreate,
		ApiDeviceRootSignatureCreate,
		ApiDeviceCreateCbv,
		ApiDeviceCreateSrv,
		ApiDeviceCreateUav,
		ApiDeviceCreateRtv,
		ApiDeviceCreateDsv,
		ApiDeviceCreateSampler,
		ApiDeviceCopyDescriptors,
		ApiDeviceCopyDescriptorsSimple,
		ApiDeviceResourceCreate,
		ApiDeviceHeapCreate,
		ApiDeviceResidency,
		ApiDeviceFenceCreate,
		ApiDeviceQueryHeapCreate,
		ApiDeviceCommandSignatureCreate,
		ApiDevicePipelineLibraryCreate,
		ApiDeviceMisc,
		ApiDeviceMiscPrivateData,
		ApiDeviceMiscSharedHandle,
		ApiDeviceMiscPower,
		ApiDeviceMiscProtectedSession,
		ApiDeviceMiscLifetime,
		ApiDeviceMiscMetaCommand,
		ApiDeviceMiscStateObject,
		ApiDeviceMiscBackgroundProcessing,
		ApiDeviceMiscShaderCache,
		ApiResourceMap,
		ApiResourceUnmap,
		ApiResourceReadWrite,
		ApiResourceQuery,
		ApiDeviceQuery,
		ApiDeviceQueryPrivateData,
		ApiDeviceQueryCapability,
		ApiDeviceQueryDescriptor,
		ApiDeviceQueryAllocation,
		ApiDeviceQueryLayout,
		ApiDeviceQueryTiling,
		ApiDeviceQueryMetaCommand,
		ApiDeviceQueryRaytracing,
		ApiDeviceQueryMisc,
		ApiObjectLifetime,
		ApiWallBucketCount
	};

#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
	class ApiWallScope
	{
	public:
		int bucket = ApiOtherCommandList;
		LARGE_INTEGER begin = {};

		ApiWallScope(int value)
		{
			bucket = value;
			begin = DX12OuterTimingProbeUtils::Now();
			DX12OuterTimingProbeUtils::LogApiScopeBegin(bucket, begin.QuadPart);
		}

		~ApiWallScope()
		{
			const LARGE_INTEGER end = DX12OuterTimingProbeUtils::Now();
			DX12OuterTimingProbeUtils::LogApiScopeEnd(
				bucket,
				begin.QuadPart,
				end.QuadPart);
		}
	};
#else
	class ApiWallScope
	{
	public:
		ApiWallScope(int) {}
	};
#endif

	class CpuStageBucket
	{
	public:
		char category[48] = {};
		char stage[64] = {};
		UINT kind = 0;
		LONG64 calls = 0;
		LONG64 totalUs = 0;
		LONG64 maxUs = 0;
		LONG64 items = 0;
	};

	class CpuStageLookupEntry
	{
	public:
		const char *category = nullptr;
		const char *stage = nullptr;
		UINT kind = 0;
		LONG index = -1;
	};

	class CpuStageLookupCache
	{
	public:
		CpuStageLookupEntry entries[160] = {};
		LONG count = 0;
	};

	inline static volatile LONG64 LastPresentEnterQpc = 0;
	inline static volatile LONG64 LastPresentLeaveQpc = 0;
	inline static volatile LONG64 CurrentPresentEnterQpc = 0;
	inline static volatile LONG64 CurrentFrameIntervalUs = 0;
	inline static volatile LONG64 CurrentOutsideBetweenPresentsUs = 0;

	inline static volatile LONG PresentEnterCalls = 0;
	inline static volatile LONG PresentLeaveCalls = 0;
	inline static volatile LONG64 PresentSliceKnownUs = 0;
	inline static volatile LONG64 PresentOriginalUs = 0;
	inline static volatile LONG64 PresentPostOriginalRuntimeUs = 0;
	inline static volatile LONG64 PresentModBeginFrameUs = 0;
	inline static volatile LONG64 PresentProfilingFlushUs = 0;
	inline static volatile LONG64 PresentConsumeUiActionsUs = 0;
	inline static volatile LONG64 PresentPerFrameCountersUs = 0;
	inline static volatile LONG64 PresentCaptureBookkeepingUs = 0;
	inline static volatile LONG64 PresentCaptureCompleteUs = 0;
	inline static volatile LONG64 PresentHotPathUpdateUs = 0;
	inline static volatile LONG64 PresentOtherUs = 0;

	inline static volatile LONG64 QueueExecuteStageSamples = 0;
	inline static volatile LONG64 QueueExecuteWallCalls = 0;
	inline static volatile LONG64 QueueExecuteWallCommandLists = 0;
	inline static volatile LONG64 QueueWrapperTotalUs = 0;
	inline static volatile LONG64 QueueFlowTotalUs = 0;
	inline static volatile LONG64 QueueOriginalUs = 0;
	inline static volatile LONG64 QueueNotifyUs = 0;
	inline static volatile LONG64 QueueRegisterUs = 0;
	inline static volatile LONG64 QueueUnwrapUs = 0;
	inline static volatile LONG64 QueueRebuildWorkFlagsUs = 0;
	inline static volatile LONG64 QueueOtherUs = 0;

	inline static volatile LONG64 QueueFenceCalls = 0;
	inline static volatile LONG64 QueueSignalUs = 0;
	inline static volatile LONG64 QueueWaitUs = 0;
	inline static volatile LONG64 FenceCalls = 0;
	inline static volatile LONG64 FenceTotalUs = 0;
	inline static volatile LONG64 EventWaitCalls = 0;
	inline static volatile LONG64 EventWaitTotalUs = 0;
	inline static volatile LONG64 SystemWaitCalls = 0;
	inline static volatile LONG64 SystemWaitTotalUs = 0;

	inline static volatile LONG64 WorkerTaskCalls = 0;
	inline static volatile LONG64 WorkerTaskUs = 0;
	inline static volatile LONG64 WorkerWaitCalls = 0;
	inline static volatile LONG64 WorkerWaitUs = 0;
	inline static volatile LONG64 WorkerIdleWaitUs = 0;
	inline static volatile LONG64 WorkerDrainWaitUs = 0;
	inline static volatile LONG64 ApiWallCalls[ApiWallBucketCount] = {};
	inline static volatile LONG64 ApiWallTotalUs[ApiWallBucketCount] = {};
	inline static volatile LONG64 ApiWallMaxUs[ApiWallBucketCount] = {};
	inline static volatile LONG64 ApiScopeActiveDepth = 0;
	inline static volatile LONG64 ApiScopeCalls = 0;
	inline static volatile LONG64 ApiScopeTotalUs = 0;
	inline static volatile LONG64 ApiScopeNestedCalls = 0;
	inline static volatile LONG64 ApiScopeFirstBeginQpc = 0;
	inline static volatile LONG64 ApiScopeLastEndQpc = 0;
	inline static volatile LONG64 ApiScopeMaxGapUs = 0;
	inline static volatile LONG64 ApiScopeMaxGapBeginQpc = 0;
	inline static volatile LONG64 ApiScopeMaxGapEndQpc = 0;
	inline static volatile LONG64 ApiScopeLastEndThreadKernelUs = 0;
	inline static volatile LONG64 ApiScopeLastEndThreadUserUs = 0;
	// Set by LogApiScopeBegin when a gap is worth recording; the matching End
	// then samples thread CPU times once. Keeps the GetThreadTimes syscall
	// (hundreds of ns each) off the per-API-call path.
	inline static volatile LONG ApiScopeEndCpuSamplePending = 0;
	inline static volatile LONG64 ApiScopeMaxGapBeforeThreadKernelUs = 0;
	inline static volatile LONG64 ApiScopeMaxGapBeforeThreadUserUs = 0;
	inline static volatile LONG64 ApiScopeMaxGapAfterThreadKernelUs = 0;
	inline static volatile LONG64 ApiScopeMaxGapAfterThreadUserUs = 0;
	inline static volatile LONG64 LastFrameLatencyHandle = 0;
	inline static volatile LONG64 LastFrameLatencySwapChain = 0;
	inline static volatile LONG LastFrameLatencyThreadId = 0;
	inline static volatile LONG LastFrameLatencyPresent = 0;
	inline static volatile LONG64 LastFrameLatencyQpc = 0;
	inline static volatile LONG64 LastFenceEventHandle = 0;
	inline static volatile LONG64 LastFenceEventFence = 0;
	inline static volatile LONG64 LastFenceEventValue = 0;
	inline static volatile LONG64 LastFenceEventCompletedBefore = 0;
	inline static volatile LONG64 LastFenceEventCompletedAfter = 0;
	inline static volatile LONG LastFenceEventThreadId = 0;
	inline static volatile LONG LastFenceEventPresent = 0;
	inline static volatile LONG64 LastFenceEventQpc = 0;
	inline static volatile LONG LastFenceEventHr = 0;
	inline static volatile LONG64 LastSystemWaitHandle = 0;
	inline static volatile LONG64 LastSystemWaitSecondaryHandle = 0;
	inline static volatile LONG64 LastSystemWaitQpc = 0;
	inline static volatile LONG64 LastSystemWaitUs = 0;
	inline static volatile LONG LastSystemWaitThreadId = 0;
	inline static volatile LONG LastSystemWaitPresent = 0;
	inline static volatile LONG LastSystemWaitApi = 0;
	inline static volatile LONG LastSystemWaitResult = 0;
	inline static volatile LONG LastSystemWaitError = 0;
	inline static volatile LONG LastSystemWaitMatchesFenceEvent = 0;
	inline static volatile LONG LastSystemWaitMatchesFrameLatency = 0;
	inline static volatile LONG ApiScopeFirstBucket = ApiOtherCommandList;
	inline static volatile LONG ApiScopeLastBucket = ApiOtherCommandList;
	inline static volatile LONG ApiScopeLastThreadId = 0;
	inline static volatile LONG ApiScopeMaxGapBeforeBucket = ApiOtherCommandList;
	inline static volatile LONG ApiScopeMaxGapAfterBucket = ApiOtherCommandList;
	inline static volatile LONG ApiScopeMaxGapBeforeThreadId = 0;
	inline static volatile LONG ApiScopeMaxGapAfterThreadId = 0;

	inline static SRWLOCK CpuStageLock = SRWLOCK_INIT;
	inline static CpuStageBucket CpuStageBuckets[160] = {};
	inline static LONG CpuStageBucketCount = 0;
	inline static thread_local CpuStageLookupCache CpuStageCache = {};

	static LARGE_INTEGER Frequency()
	{
		static LARGE_INTEGER frequency = InitFrequency();
		return frequency;
	}

	static LARGE_INTEGER InitFrequency()
	{
		LARGE_INTEGER frequency = {};
		QueryPerformanceFrequency(&frequency);
		return frequency;
	}

	static LARGE_INTEGER Now()
	{
		LARGE_INTEGER value = {};
	#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
		QueryPerformanceCounter(&value);
	#endif
		return value;
	}

	static LONG64 DeltaUs(LONGLONG begin, LONGLONG end)
	{
		const LARGE_INTEGER frequency = Frequency();
		if (frequency.QuadPart <= 0 || end <= begin)
			return 0;
		return static_cast<LONG64>((end - begin) * 1000000 / frequency.QuadPart);
	}

	static LONG64 ElapsedUs(const LARGE_INTEGER &begin)
	{
	#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
		const LARGE_INTEGER end = Now();
		return DeltaUs(begin.QuadPart, end.QuadPart);
	#else
		(void)begin;
		return 0;
	#endif
	}

	static UINT64 FenceCompletedValue(ID3D12Fence *fence)
	{
		if (!fence)
			return 0;
		return fence->GetCompletedValue();
	}

	static UINT64 TimingFenceCompletedValue(ID3D12Fence *fence)
	{
	#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
		return FenceCompletedValue(fence);
	#else
		(void)fence;
		return 0;
	#endif
	}

	static LONG64 ClampUs(LONG64 us)
	{
		return us > 0 ? us : 0;
	}

	static UINT64 FileTimeToUs(const FILETIME &value)
	{
		ULARGE_INTEGER combined = {};
		combined.LowPart = value.dwLowDateTime;
		combined.HighPart = value.dwHighDateTime;
		return combined.QuadPart / 10;
	}

	static void GetCurrentThreadCpuUs(LONG64 *kernelUs, LONG64 *userUs)
	{
		if (kernelUs)
			*kernelUs = 0;
		if (userUs)
			*userUs = 0;
#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
		FILETIME createTime = {};
		FILETIME exitTime = {};
		FILETIME kernelTime = {};
		FILETIME userTime = {};
		if (!GetThreadTimes(GetCurrentThread(), &createTime, &exitTime, &kernelTime, &userTime))
			return;
		if (kernelUs)
			*kernelUs = static_cast<LONG64>(FileTimeToUs(kernelTime));
		if (userUs)
			*userUs = static_cast<LONG64>(FileTimeToUs(userTime));
#endif
	}

	static bool IsInterestingGap(LONG64 gapUs)
	{
		return gapUs >= 50000;
	}

	static const char *WaitApiName(LONG api)
	{
		switch (api) {
		case 1:
			return "WaitForSingleObject";
		case 2:
			return "WaitForMultipleObjects";
		case 3:
			return "SignalObjectAndWait";
		default:
			return "Unknown";
		}
	}

	static void LogCurrentCallStack(
		const char *reason,
		const char *api,
		HANDLE handle,
		ID3D12Fence *fence,
		UINT64 fenceValue,
		LONG64 waitUs,
		DWORD waitResult,
		DWORD threadId,
		ULONG skipFrames = 0)
	{
#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
		void *frames[24] = {};
		const USHORT captured = CaptureStackBackTrace(
			static_cast<DWORD>(skipFrames + 1),
			ARRAYSIZE(frames),
			frames,
			nullptr);
		for (USHORT i = 0; i < captured; ++i) {
			void *address = frames[i];
			HMODULE module = nullptr;
			char modulePath[MAX_PATH] = {};
			const char *moduleText = "";
			ULONG_PTR moduleBase = 0;
			if (GetModuleHandleExA(
					GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
						GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
					reinterpret_cast<LPCSTR>(address),
					&module) && module) {
				moduleBase = reinterpret_cast<ULONG_PTR>(module);
				if (GetModuleFileNameA(module, modulePath, ARRAYSIZE(modulePath)))
					moduleText = modulePath;
			}

			DX12LogManager::Fields("DX12WaitStack",
				"\"reason\":\"%s\",\"api\":\"%s\",\"frame\":%u,\"address\":\"%p\","
				"\"module\":\"%s\",\"moduleBase\":\"0x%llx\",\"moduleOffset\":\"0x%llx\","
				"\"handle\":\"%p\",\"fence\":\"%p\",\"fenceValue\":%llu,"
				"\"waitUs\":%lld,\"waitResult\":\"0x%lx\",\"threadId\":%lu,\"present\":%ld",
				reason ? reason : "",
				api ? api : "",
				static_cast<unsigned>(i),
				address,
				moduleText,
				static_cast<unsigned long long>(moduleBase),
				static_cast<unsigned long long>(
					moduleBase ? reinterpret_cast<ULONG_PTR>(address) - moduleBase : 0),
				handle,
				fence,
				static_cast<unsigned long long>(fenceValue),
				static_cast<long long>(waitUs),
				static_cast<unsigned long>(waitResult),
				static_cast<unsigned long>(threadId),
				DX12RuntimeState::GetPresentCount());
		}
#else
		(void)reason;
		(void)api;
		(void)handle;
		(void)fence;
		(void)fenceValue;
		(void)waitUs;
		(void)waitResult;
		(void)threadId;
		(void)skipFrames;
#endif
	}

	static bool IsTrackedWaitHandle(HANDLE handle, bool *matchesFenceEvent, bool *matchesFrameLatency)
	{
		const ULONG_PTR rawHandle = reinterpret_cast<ULONG_PTR>(handle);
		const ULONG_PTR rawFenceHandle = static_cast<ULONG_PTR>(
			InterlockedCompareExchange64(&LastFenceEventHandle, 0, 0));
		const ULONG_PTR rawFrameLatencyHandle = static_cast<ULONG_PTR>(
			InterlockedCompareExchange64(&LastFrameLatencyHandle, 0, 0));
		const bool fenceMatch = rawHandle != 0 && rawHandle == rawFenceHandle;
		const bool frameLatencyMatch = rawHandle != 0 && rawHandle == rawFrameLatencyHandle;
		if (matchesFenceEvent)
			*matchesFenceEvent = fenceMatch;
		if (matchesFrameLatency)
			*matchesFrameLatency = frameLatencyMatch;
		return fenceMatch || frameLatencyMatch;
	}

	static void SnapshotLastFenceEvent(
		ID3D12Fence **fence,
		UINT64 *value,
		HANDLE *eventHandle)
	{
		if (fence) {
			*fence = reinterpret_cast<ID3D12Fence*>(
				static_cast<ULONG_PTR>(
					InterlockedCompareExchange64(&LastFenceEventFence, 0, 0)));
		}
		if (value)
			*value = static_cast<UINT64>(
				InterlockedCompareExchange64(&LastFenceEventValue, 0, 0));
		if (eventHandle) {
			*eventHandle = reinterpret_cast<HANDLE>(
				static_cast<ULONG_PTR>(
					InterlockedCompareExchange64(&LastFenceEventHandle, 0, 0)));
		}
	}

	static void RegisterFrameLatencyWaitableObject(IDXGISwapChain *swapChain, HANDLE handle)
	{
#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
		InterlockedExchange64(&LastFrameLatencyHandle,
			static_cast<LONG64>(reinterpret_cast<ULONG_PTR>(handle)));
		InterlockedExchange64(&LastFrameLatencySwapChain,
			static_cast<LONG64>(reinterpret_cast<ULONG_PTR>(swapChain)));
		InterlockedExchange(&LastFrameLatencyThreadId, static_cast<LONG>(GetCurrentThreadId()));
		InterlockedExchange(&LastFrameLatencyPresent, DX12RuntimeState::GetPresentCount());
		InterlockedExchange64(&LastFrameLatencyQpc, Now().QuadPart);
		DX12LogManager::Fields("DX12WaitableObject",
			"\"kind\":\"frame_latency\",\"swapchain\":\"%p\",\"handle\":\"%p\","
			"\"threadId\":%ld,\"present\":%ld",
			swapChain,
			handle,
			static_cast<LONG>(GetCurrentThreadId()),
			DX12RuntimeState::GetPresentCount());
#else
		(void)swapChain;
		(void)handle;
#endif
	}

	static void RegisterFenceEventOnCompletion(
		ID3D12Fence *fence,
		HANDLE eventHandle,
		UINT64 value,
		UINT64 completedBefore,
		UINT64 completedAfter,
		HRESULT hr)
	{
#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
		InterlockedExchange64(&LastFenceEventHandle,
			static_cast<LONG64>(reinterpret_cast<ULONG_PTR>(eventHandle)));
		InterlockedExchange64(&LastFenceEventFence,
			static_cast<LONG64>(reinterpret_cast<ULONG_PTR>(fence)));
		InterlockedExchange64(&LastFenceEventValue, static_cast<LONG64>(value));
		InterlockedExchange64(&LastFenceEventCompletedBefore, static_cast<LONG64>(completedBefore));
		InterlockedExchange64(&LastFenceEventCompletedAfter, static_cast<LONG64>(completedAfter));
		InterlockedExchange(&LastFenceEventThreadId, static_cast<LONG>(GetCurrentThreadId()));
		InterlockedExchange(&LastFenceEventPresent, DX12RuntimeState::GetPresentCount());
		InterlockedExchange64(&LastFenceEventQpc, Now().QuadPart);
		InterlockedExchange(&LastFenceEventHr, static_cast<LONG>(hr));
		DX12LogManager::Fields("DX12WaitableObject",
			"\"kind\":\"fence_event\",\"fence\":\"%p\",\"handle\":\"%p\","
			"\"value\":%llu,\"completedBefore\":%llu,\"completedAfter\":%llu,"
			"\"hr\":\"0x%lx\",\"threadId\":%ld,\"present\":%ld",
			fence,
			eventHandle,
			static_cast<unsigned long long>(value),
			static_cast<unsigned long long>(completedBefore),
			static_cast<unsigned long long>(completedAfter),
			static_cast<unsigned long>(hr),
			static_cast<LONG>(GetCurrentThreadId()),
			DX12RuntimeState::GetPresentCount());
#else
		(void)fence;
		(void)eventHandle;
		(void)value;
		(void)completedBefore;
		(void)completedAfter;
		(void)hr;
#endif
	}

	static void RegisterSystemWait(
		LONG api,
		HANDLE primaryHandle,
		HANDLE secondaryHandle,
		DWORD waitResult,
		DWORD lastError,
		LONG64 us,
		bool matchesFenceEvent,
		bool matchesFrameLatency)
	{
#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
		InterlockedExchange64(&LastSystemWaitHandle,
			static_cast<LONG64>(reinterpret_cast<ULONG_PTR>(primaryHandle)));
		InterlockedExchange64(&LastSystemWaitSecondaryHandle,
			static_cast<LONG64>(reinterpret_cast<ULONG_PTR>(secondaryHandle)));
		InterlockedExchange64(&LastSystemWaitQpc, Now().QuadPart);
		InterlockedExchange64(&LastSystemWaitUs, us);
		InterlockedExchange(&LastSystemWaitThreadId, static_cast<LONG>(GetCurrentThreadId()));
		InterlockedExchange(&LastSystemWaitPresent, DX12RuntimeState::GetPresentCount());
		InterlockedExchange(&LastSystemWaitApi, api);
		InterlockedExchange(&LastSystemWaitResult, static_cast<LONG>(waitResult));
		InterlockedExchange(&LastSystemWaitError, static_cast<LONG>(lastError));
		InterlockedExchange(&LastSystemWaitMatchesFenceEvent, matchesFenceEvent ? 1 : 0);
		InterlockedExchange(&LastSystemWaitMatchesFrameLatency, matchesFrameLatency ? 1 : 0);
#else
		(void)api;
		(void)primaryHandle;
		(void)secondaryHandle;
		(void)waitResult;
		(void)lastError;
		(void)us;
		(void)matchesFenceEvent;
		(void)matchesFrameLatency;
#endif
	}

	static void LogSystemWaitSlice(
		const char *api,
		HANDLE primaryHandle,
		HANDLE secondaryHandle,
		DWORD waitMs,
		DWORD waitResult,
		DWORD lastError,
		LONG64 us,
		bool matchesFenceEvent,
		bool matchesFrameLatency)
	{
#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
		const LONG64 safeUs = ClampUs(us);
		InterlockedIncrement64(&SystemWaitCalls);
		AddUs(&SystemWaitTotalUs, safeUs);

		const bool interesting = safeUs >= 50000 || matchesFenceEvent || matchesFrameLatency;
		if (!interesting)
			return;

		DX12LogManager::Fields("DX12WaitableObject",
			"\"kind\":\"system_wait\",\"api\":\"%s\",\"handle\":\"%p\",\"secondaryHandle\":\"%p\","
			"\"waitMs\":%lu,\"waitResult\":\"0x%lx\",\"lastError\":%lu,\"us\":%lld,"
			"\"matchesFenceEvent\":%s,\"matchesFrameLatency\":%s,\"threadId\":%lu,\"present\":%ld",
			api ? api : "",
			primaryHandle,
			secondaryHandle,
			static_cast<unsigned long>(waitMs),
			static_cast<unsigned long>(waitResult),
			static_cast<unsigned long>(lastError),
			static_cast<long long>(safeUs),
			matchesFenceEvent ? "true" : "false",
			matchesFrameLatency ? "true" : "false",
			static_cast<unsigned long>(GetCurrentThreadId()),
			DX12RuntimeState::GetPresentCount());
#else
		(void)api;
		(void)primaryHandle;
		(void)secondaryHandle;
		(void)waitMs;
		(void)waitResult;
		(void)lastError;
		(void)us;
		(void)matchesFenceEvent;
		(void)matchesFrameLatency;
#endif
	}

	static bool StageEquals(const char *stage, const char *value)
	{
		return stage && value && strcmp(stage, value) == 0;
	}

	static bool StageStartsWith(const char *stage, const char *prefix)
	{
		if (!stage || !prefix)
			return false;
		const size_t prefixLength = strlen(prefix);
		return strncmp(stage, prefix, prefixLength) == 0;
	}

	static bool LabelEquals(const char *left, const char *right)
	{
		if (!left)
			left = "";
		if (!right)
			right = "";
		return strcmp(left, right) == 0;
	}

	static void CopyLabel(char *target, size_t targetSize, const char *source)
	{
		if (!target || targetSize == 0)
			return;
		target[0] = '\0';
		if (!source)
			source = "";
		strncpy_s(target, targetSize, source, _TRUNCATE);
	}

	static LONG64 TakeLong64(volatile LONG64 *value)
	{
		return InterlockedExchange64(value, 0);
	}

	static LONG TakeLong(volatile LONG *value)
	{
		return InterlockedExchange(value, 0);
	}

	static void AddUs(volatile LONG64 *target, LONG64 us)
	{
		const LONG64 safeUs = ClampUs(us);
		if (safeUs > 0)
			InterlockedAdd64(target, safeUs);
	}

	static const char *ApiWallBucketName(int bucket)
	{
		switch (bucket) {
		case ApiDrawInstanced: return "DrawInstanced";
		case ApiDrawIndexedInstanced: return "DrawIndexedInstanced";
		case ApiDispatch: return "Dispatch";
		case ApiExecuteIndirect: return "ExecuteIndirect";
		case ApiExecuteCommandLists: return "ExecuteCommandLists";
		case ApiQueueSignal: return "QueueSignal";
		case ApiQueueWait: return "QueueWait";
		case ApiResetCommandList: return "ResetCommandList";
		case ApiCloseCommandList: return "CloseCommandList";
		case ApiSetPipelineState: return "SetPipelineState";
		case ApiResourceBarrier: return "ResourceBarrier";
		case ApiSetDescriptorHeaps: return "SetDescriptorHeaps";
		case ApiSetComputeRootSignature: return "SetComputeRootSignature";
		case ApiSetGraphicsRootSignature: return "SetGraphicsRootSignature";
		case ApiSetComputeRootDescriptorTable: return "SetComputeRootDescriptorTable";
		case ApiSetGraphicsRootDescriptorTable: return "SetGraphicsRootDescriptorTable";
		case ApiSetComputeRootDescriptor: return "SetComputeRootDescriptor";
		case ApiSetGraphicsRootDescriptor: return "SetGraphicsRootDescriptor";
		case ApiSetComputeRootConstants: return "SetComputeRootConstants";
		case ApiSetGraphicsRootConstants: return "SetGraphicsRootConstants";
		case ApiIASetIndexBuffer: return "IASetIndexBuffer";
		case ApiIASetVertexBuffers: return "IASetVertexBuffers";
		case ApiIASetPrimitiveTopology: return "IASetPrimitiveTopology";
		case ApiCopyBufferRegion: return "CopyBufferRegion";
		case ApiCopyTextureRegion: return "CopyTextureRegion";
		case ApiCopyResource: return "CopyResource";
		case ApiCopyTiles: return "CopyTiles";
		case ApiResolveSubresource: return "ResolveSubresource";
		case ApiClearUnorderedAccessView: return "ClearUnorderedAccessView";
		case ApiDeviceCommandCreate: return "DeviceCommandCreate";
		case ApiDevicePsoCreate: return "DevicePsoCreate";
		case ApiDeviceDescriptorHeapCreate: return "DeviceDescriptorHeapCreate";
		case ApiDeviceRootSignatureCreate: return "DeviceRootSignatureCreate";
		case ApiDeviceCreateCbv: return "DeviceCreateCBV";
		case ApiDeviceCreateSrv: return "DeviceCreateSRV";
		case ApiDeviceCreateUav: return "DeviceCreateUAV";
		case ApiDeviceCreateRtv: return "DeviceCreateRTV";
		case ApiDeviceCreateDsv: return "DeviceCreateDSV";
		case ApiDeviceCreateSampler: return "DeviceCreateSampler";
		case ApiDeviceCopyDescriptors: return "DeviceCopyDescriptors";
		case ApiDeviceCopyDescriptorsSimple: return "DeviceCopyDescriptorsSimple";
		case ApiDeviceResourceCreate: return "DeviceResourceCreate";
		case ApiDeviceHeapCreate: return "DeviceHeapCreate";
		case ApiDeviceResidency: return "DeviceResidency";
		case ApiDeviceFenceCreate: return "DeviceFenceCreate";
		case ApiDeviceQueryHeapCreate: return "DeviceQueryHeapCreate";
		case ApiDeviceCommandSignatureCreate: return "DeviceCommandSignatureCreate";
		case ApiDevicePipelineLibraryCreate: return "DevicePipelineLibraryCreate";
		case ApiDeviceMisc: return "DeviceMisc";
		case ApiDeviceMiscPrivateData: return "DeviceMiscPrivateData";
		case ApiDeviceMiscSharedHandle: return "DeviceMiscSharedHandle";
		case ApiDeviceMiscPower: return "DeviceMiscPower";
		case ApiDeviceMiscProtectedSession: return "DeviceMiscProtectedSession";
		case ApiDeviceMiscLifetime: return "DeviceMiscLifetime";
		case ApiDeviceMiscMetaCommand: return "DeviceMiscMetaCommand";
		case ApiDeviceMiscStateObject: return "DeviceMiscStateObject";
		case ApiDeviceMiscBackgroundProcessing: return "DeviceMiscBackgroundProcessing";
		case ApiDeviceMiscShaderCache: return "DeviceMiscShaderCache";
		case ApiResourceMap: return "ResourceMap";
		case ApiResourceUnmap: return "ResourceUnmap";
		case ApiResourceReadWrite: return "ResourceReadWrite";
		case ApiResourceQuery: return "ResourceQuery";
		case ApiDeviceQuery: return "DeviceQuery";
		case ApiDeviceQueryPrivateData: return "DeviceQueryPrivateData";
		case ApiDeviceQueryCapability: return "DeviceQueryCapability";
		case ApiDeviceQueryDescriptor: return "DeviceQueryDescriptor";
		case ApiDeviceQueryAllocation: return "DeviceQueryAllocation";
		case ApiDeviceQueryLayout: return "DeviceQueryLayout";
		case ApiDeviceQueryTiling: return "DeviceQueryTiling";
		case ApiDeviceQueryMetaCommand: return "DeviceQueryMetaCommand";
		case ApiDeviceQueryRaytracing: return "DeviceQueryRaytracing";
		case ApiDeviceQueryMisc: return "DeviceQueryMisc";
		case ApiObjectLifetime: return "ObjectLifetime";
		default: return "OtherCommandList";
		}
	}

	static void MaxUs(volatile LONG64 *target, LONG64 value)
	{
		LONG64 oldValue = InterlockedCompareExchange64(target, 0, 0);
		while (value > oldValue) {
			const LONG64 previous =
				InterlockedCompareExchange64(target, value, oldValue);
			if (previous == oldValue)
				break;
			oldValue = previous;
		}
	}

	static void MaxQpc(volatile LONG64 *target, LONGLONG value)
	{
		LONG64 oldValue = InterlockedCompareExchange64(target, 0, 0);
		while (value > oldValue) {
			const LONG64 previous =
				InterlockedCompareExchange64(target, static_cast<LONG64>(value), oldValue);
			if (previous == oldValue)
				break;
			oldValue = previous;
		}
	}

	static void LogApiScopeBegin(int bucket, LONGLONG beginQpc)
	{
		const LONG64 activeDepth = InterlockedIncrement64(&ApiScopeActiveDepth);
		if (activeDepth > 1) {
			InterlockedIncrement64(&ApiScopeNestedCalls);
			return;
		}
		InterlockedIncrement64(&ApiScopeCalls);
		const LONG64 firstBegin = InterlockedCompareExchange64(
			&ApiScopeFirstBeginQpc,
			static_cast<LONG64>(beginQpc),
			0);
		if (firstBegin == 0)
			InterlockedExchange(&ApiScopeFirstBucket, bucket);
		const LONG64 previousEndQpc =
			InterlockedCompareExchange64(&ApiScopeLastEndQpc, 0, 0);
		if (previousEndQpc > 0 && beginQpc > previousEndQpc) {
			const LONG64 gapUs = DeltaUs(previousEndQpc, beginQpc);
			const LONG beforeBucket = InterlockedCompareExchange(&ApiScopeLastBucket, 0, 0);
			const LONG beforeThreadId = InterlockedCompareExchange(&ApiScopeLastThreadId, 0, 0);
			const LONG64 beforeKernelUs =
				InterlockedCompareExchange64(&ApiScopeLastEndThreadKernelUs, 0, 0);
			const LONG64 beforeUserUs =
				InterlockedCompareExchange64(&ApiScopeLastEndThreadUserUs, 0, 0);
			LONG64 afterKernelUs = 0;
			LONG64 afterUserUs = 0;
			// GetThreadTimes is a syscall (hundreds of ns each); sampling it on
			// every API call dominates the TIMING build cost while the 50ms
			// IsInterestingGap threshold almost never fires. Only sample when
			// this gap can actually be logged or beat the recorded maximum.
			const LONG64 observedMaxGap =
				InterlockedCompareExchange64(&ApiScopeMaxGapUs, 0, 0);
			if (IsInterestingGap(gapUs) || gapUs > observedMaxGap) {
				GetCurrentThreadCpuUs(&afterKernelUs, &afterUserUs);
				InterlockedExchange(&ApiScopeEndCpuSamplePending, 1);
			}
			const LONG64 threadKernelDeltaUs =
				static_cast<LONG>(GetCurrentThreadId()) == beforeThreadId ?
					SafeSubtract(afterKernelUs, beforeKernelUs) : 0;
			const LONG64 threadUserDeltaUs =
				static_cast<LONG>(GetCurrentThreadId()) == beforeThreadId ?
					SafeSubtract(afterUserUs, beforeUserUs) : 0;
			const LONG64 threadCpuDeltaUs = threadKernelDeltaUs + threadUserDeltaUs;
			LONG64 observed = InterlockedCompareExchange64(&ApiScopeMaxGapUs, 0, 0);
			while (gapUs > observed) {
				const LONG64 previous = InterlockedCompareExchange64(
					&ApiScopeMaxGapUs, gapUs, observed);
				if (previous == observed) {
					InterlockedExchange64(&ApiScopeMaxGapBeginQpc, previousEndQpc);
					InterlockedExchange64(&ApiScopeMaxGapEndQpc, beginQpc);
					InterlockedExchange64(&ApiScopeMaxGapBeforeThreadKernelUs, beforeKernelUs);
					InterlockedExchange64(&ApiScopeMaxGapBeforeThreadUserUs, beforeUserUs);
					InterlockedExchange64(&ApiScopeMaxGapAfterThreadKernelUs, afterKernelUs);
					InterlockedExchange64(&ApiScopeMaxGapAfterThreadUserUs, afterUserUs);
					InterlockedExchange(&ApiScopeMaxGapBeforeBucket, beforeBucket);
					InterlockedExchange(&ApiScopeMaxGapAfterBucket, bucket);
					InterlockedExchange(&ApiScopeMaxGapBeforeThreadId, beforeThreadId);
					InterlockedExchange(&ApiScopeMaxGapAfterThreadId,
						static_cast<LONG>(GetCurrentThreadId()));
					break;
				}
				observed = previous;
			}
			if (IsInterestingGap(gapUs)) {
				DX12LogManager::Fields("DX12CpuGapProbe",
					"\"present\":%ld,\"gapUs\":%lld,\"beforeBucket\":\"%s\","
					"\"afterBucket\":\"%s\",\"beforeThreadId\":%ld,\"afterThreadId\":%ld,"
					"\"sameThread\":%ld,\"threadKernelDeltaUs\":%lld,"
					"\"threadUserDeltaUs\":%lld,\"threadCpuDeltaUs\":%lld,"
					"\"beginQpc\":%lld,\"endQpc\":%lld",
					DX12RuntimeState::GetPresentCount(),
					static_cast<long long>(gapUs),
					ApiWallBucketName(beforeBucket),
					ApiWallBucketName(bucket),
					beforeThreadId,
					static_cast<LONG>(GetCurrentThreadId()),
					static_cast<LONG>(static_cast<LONG>(GetCurrentThreadId()) == beforeThreadId),
					static_cast<long long>(threadKernelDeltaUs),
					static_cast<long long>(threadUserDeltaUs),
					static_cast<long long>(threadCpuDeltaUs),
					static_cast<long long>(previousEndQpc),
					static_cast<long long>(beginQpc));
			}
		}
	}

	static void LogApiScopeEnd(int bucket, LONGLONG beginQpc, LONGLONG endQpc)
	{
		const LONG64 elapsedUs = DeltaUs(beginQpc, endQpc);
		const LONG64 activeDepth = InterlockedCompareExchange64(&ApiScopeActiveDepth, 0, 0);
		if (activeDepth <= 1) {
			AddUs(&ApiScopeTotalUs, elapsedUs);
			LONG64 observed = InterlockedCompareExchange64(&ApiScopeLastEndQpc, 0, 0);
			while (endQpc > observed) {
				const LONG64 previous = InterlockedCompareExchange64(
					&ApiScopeLastEndQpc, endQpc, observed);
				if (previous == observed) {
					InterlockedExchange(&ApiScopeLastBucket, bucket);
					InterlockedExchange(&ApiScopeLastThreadId,
						static_cast<LONG>(GetCurrentThreadId()));
					LONG64 kernelUs = 0;
					LONG64 userUs = 0;
					// Only sample when the matching Begin flagged a recordable
					// gap; see ApiScopeEndCpuSamplePending in LogApiScopeBegin.
					if (InterlockedExchange(&ApiScopeEndCpuSamplePending, 0)) {
						GetCurrentThreadCpuUs(&kernelUs, &userUs);
						InterlockedExchange64(&ApiScopeLastEndThreadKernelUs, kernelUs);
						InterlockedExchange64(&ApiScopeLastEndThreadUserUs, userUs);
					}
					break;
				}
				observed = previous;
			}
		}
		LogApiWallSlice(bucket, elapsedUs);
		InterlockedDecrement64(&ApiScopeActiveDepth);
	}

	static void LogApiWallSlice(int bucket, LONG64 us)
	{
		if (bucket < 0 || bucket >= ApiWallBucketCount)
			bucket = ApiOtherCommandList;
		const LONG64 safeUs = ClampUs(us);
		if (safeUs <= 0)
			return;
		InterlockedIncrement64(&ApiWallCalls[bucket]);
		InterlockedAdd64(&ApiWallTotalUs[bucket], safeUs);
		MaxUs(&ApiWallMaxUs[bucket], safeUs);
	}

	static void LogApiWallTimingState(LONG present, LONG64 frameWallUs)
	{
		for (int bucket = 0; bucket < ApiWallBucketCount; ++bucket) {
			const LONG64 calls = TakeLong64(&ApiWallCalls[bucket]);
			const LONG64 totalUs = TakeLong64(&ApiWallTotalUs[bucket]);
			const LONG64 maxUs = TakeLong64(&ApiWallMaxUs[bucket]);
			if (!calls && !totalUs && !maxUs)
				continue;
			DX12LogManager::Fields("DX12ApiWallTiming",
				"\"present\":%ld,\"bucket\":\"%s\",\"calls\":%lld,\"totalUs\":%lld,\"maxUs\":%lld,\"frameWallUs\":%lld,\"framePctX100\":%lld",
				present,
				ApiWallBucketName(bucket),
				static_cast<long long>(calls),
				static_cast<long long>(totalUs),
				static_cast<long long>(maxUs),
				static_cast<long long>(frameWallUs),
				static_cast<long long>(
					frameWallUs > 0 ? (totalUs * 10000 / frameWallUs) : 0));
		}
	}

	static void LogCpuTimelineCoverageState(
		LONG present,
		LONGLONG frameStartQpc,
		LONGLONG frameEndQpc,
		LONG64 frameWallUs,
		LONG64 presentWallUs)
	{
		const LONG64 calls = TakeLong64(&ApiScopeCalls);
		const LONG64 totalUs = TakeLong64(&ApiScopeTotalUs);
		const LONG64 nestedCalls = TakeLong64(&ApiScopeNestedCalls);
		const LONG64 firstBeginQpc = TakeLong64(&ApiScopeFirstBeginQpc);
		const LONG64 lastEndQpc = TakeLong64(&ApiScopeLastEndQpc);
		const LONG64 maxGapUs = TakeLong64(&ApiScopeMaxGapUs);
		const LONG64 maxGapBeginQpc = TakeLong64(&ApiScopeMaxGapBeginQpc);
		const LONG64 maxGapEndQpc = TakeLong64(&ApiScopeMaxGapEndQpc);
		const LONG64 maxGapBeforeThreadKernelUs =
			TakeLong64(&ApiScopeMaxGapBeforeThreadKernelUs);
		const LONG64 maxGapBeforeThreadUserUs =
			TakeLong64(&ApiScopeMaxGapBeforeThreadUserUs);
		const LONG64 maxGapAfterThreadKernelUs =
			TakeLong64(&ApiScopeMaxGapAfterThreadKernelUs);
		const LONG64 maxGapAfterThreadUserUs =
			TakeLong64(&ApiScopeMaxGapAfterThreadUserUs);
		const LONG firstBucket = InterlockedExchange(&ApiScopeFirstBucket, ApiOtherCommandList);
		const LONG lastBucket = InterlockedExchange(&ApiScopeLastBucket, ApiOtherCommandList);
		const LONG maxGapBeforeBucket = InterlockedExchange(
			&ApiScopeMaxGapBeforeBucket, ApiOtherCommandList);
		const LONG maxGapAfterBucket = InterlockedExchange(
			&ApiScopeMaxGapAfterBucket, ApiOtherCommandList);
		const LONG maxGapBeforeThreadId = TakeLong(&ApiScopeMaxGapBeforeThreadId);
		const LONG maxGapAfterThreadId = TakeLong(&ApiScopeMaxGapAfterThreadId);
		TakeLong(&ApiScopeLastThreadId);
		TakeLong64(&ApiScopeLastEndThreadKernelUs);
		TakeLong64(&ApiScopeLastEndThreadUserUs);
		const LONG64 maxGapThreadKernelDeltaUs =
			maxGapBeforeThreadId == maxGapAfterThreadId ?
				SafeSubtract(maxGapAfterThreadKernelUs, maxGapBeforeThreadKernelUs) : 0;
		const LONG64 maxGapThreadUserDeltaUs =
			maxGapBeforeThreadId == maxGapAfterThreadId ?
				SafeSubtract(maxGapAfterThreadUserUs, maxGapBeforeThreadUserUs) : 0;
		const LONG64 maxGapThreadCpuDeltaUs =
			maxGapThreadKernelDeltaUs + maxGapThreadUserDeltaUs;
		const LONG64 lastFrameLatencyHandle = TakeLong64(&LastFrameLatencyHandle);
		const LONG64 lastFrameLatencySwapChain = TakeLong64(&LastFrameLatencySwapChain);
		const LONG lastFrameLatencyThreadId = TakeLong(&LastFrameLatencyThreadId);
		const LONG lastFrameLatencyPresent = TakeLong(&LastFrameLatencyPresent);
		const LONG64 lastFrameLatencyQpc = TakeLong64(&LastFrameLatencyQpc);
		const LONG64 lastFenceEventHandle = TakeLong64(&LastFenceEventHandle);
		const LONG64 lastFenceEventFence = TakeLong64(&LastFenceEventFence);
		const LONG64 lastFenceEventValue = TakeLong64(&LastFenceEventValue);
		const LONG64 lastFenceEventCompletedBefore = TakeLong64(&LastFenceEventCompletedBefore);
		const LONG64 lastFenceEventCompletedAfter = TakeLong64(&LastFenceEventCompletedAfter);
		const LONG lastFenceEventThreadId = TakeLong(&LastFenceEventThreadId);
		const LONG lastFenceEventPresent = TakeLong(&LastFenceEventPresent);
		const LONG64 lastFenceEventQpc = TakeLong64(&LastFenceEventQpc);
		const LONG lastFenceEventHr = TakeLong(&LastFenceEventHr);
		const LONG64 lastSystemWaitHandle = TakeLong64(&LastSystemWaitHandle);
		const LONG64 lastSystemWaitSecondaryHandle = TakeLong64(&LastSystemWaitSecondaryHandle);
		const LONG64 lastSystemWaitQpc = TakeLong64(&LastSystemWaitQpc);
		const LONG64 lastSystemWaitUs = TakeLong64(&LastSystemWaitUs);
		const LONG lastSystemWaitThreadId = TakeLong(&LastSystemWaitThreadId);
		const LONG lastSystemWaitPresent = TakeLong(&LastSystemWaitPresent);
		const LONG lastSystemWaitApi = TakeLong(&LastSystemWaitApi);
		const LONG lastSystemWaitResult = TakeLong(&LastSystemWaitResult);
		const LONG lastSystemWaitError = TakeLong(&LastSystemWaitError);
		const LONG lastSystemWaitMatchesFenceEvent =
			TakeLong(&LastSystemWaitMatchesFenceEvent);
		const LONG lastSystemWaitMatchesFrameLatency =
			TakeLong(&LastSystemWaitMatchesFrameLatency);

		const LONG64 apiSpanUs =
			(firstBeginQpc > 0 && lastEndQpc > firstBeginQpc) ?
				DeltaUs(firstBeginQpc, lastEndQpc) : 0;
		const LONG64 beforeFirstApiUs =
			(frameStartQpc > 0 && firstBeginQpc > frameStartQpc) ?
				DeltaUs(frameStartQpc, firstBeginQpc) : 0;
		const LONG64 afterLastApiUs =
			(lastEndQpc > 0 && frameEndQpc > lastEndQpc) ?
				DeltaUs(lastEndQpc, frameEndQpc) : 0;
		const LONG64 firstBeginToPresentUs =
			(firstBeginQpc > 0 && frameEndQpc > firstBeginQpc) ?
				DeltaUs(firstBeginQpc, frameEndQpc) : 0;
		const LONG64 lastEndToPresentUs =
			(lastEndQpc > 0 && frameEndQpc > lastEndQpc) ?
				DeltaUs(lastEndQpc, frameEndQpc) : 0;

		DX12LogManager::Fields("DX12CpuTimelineCoverage",
			"\"present\":%ld,\"apiScopeCalls\":%lld,\"apiScopeNestedCalls\":%lld,"
			"\"apiScopeTotalUs\":%lld,\"apiScopeSpanUs\":%lld,\"apiScopeMaxGapUs\":%lld,"
			"\"apiScopeFirstBucket\":\"%s\",\"apiScopeLastBucket\":\"%s\","
			"\"apiScopeMaxGapBeforeBucket\":\"%s\",\"apiScopeMaxGapAfterBucket\":\"%s\","
			"\"apiScopeMaxGapBeforeThreadId\":%ld,\"apiScopeMaxGapAfterThreadId\":%ld,"
			"\"apiScopeMaxGapSameThread\":%ld,\"apiScopeMaxGapThreadKernelDeltaUs\":%lld,"
			"\"apiScopeMaxGapThreadUserDeltaUs\":%lld,\"apiScopeMaxGapThreadCpuDeltaUs\":%lld,"
			"\"apiScopeMaxGapBeginQpc\":%lld,\"apiScopeMaxGapEndQpc\":%lld,"
			"\"lastFrameLatencyHandle\":\"0x%llx\",\"lastFrameLatencySwapChain\":\"0x%llx\","
			"\"lastFrameLatencyThreadId\":%ld,\"lastFrameLatencyPresent\":%ld,"
			"\"lastFrameLatencyQpc\":%lld,"
			"\"lastFenceEventHandle\":\"0x%llx\",\"lastFenceEventFence\":\"0x%llx\","
			"\"lastFenceEventValue\":%lld,\"lastFenceEventCompletedBefore\":%lld,"
			"\"lastFenceEventCompletedAfter\":%lld,\"lastFenceEventThreadId\":%ld,"
			"\"lastFenceEventPresent\":%ld,\"lastFenceEventQpc\":%lld,\"lastFenceEventHr\":\"0x%lx\","
			"\"lastSystemWaitApi\":\"%s\",\"lastSystemWaitHandle\":\"0x%llx\","
			"\"lastSystemWaitSecondaryHandle\":\"0x%llx\",\"lastSystemWaitUs\":%lld,"
			"\"lastSystemWaitResult\":\"0x%lx\",\"lastSystemWaitError\":%ld,"
			"\"lastSystemWaitThreadId\":%ld,\"lastSystemWaitPresent\":%ld,"
			"\"lastSystemWaitQpc\":%lld,\"lastSystemWaitMatchesFenceEvent\":%ld,"
			"\"lastSystemWaitMatchesFrameLatency\":%ld,"
			"\"apiBeforeFirstScopeUs\":%lld,\"apiAfterLastScopeUs\":%lld,"
			"\"apiFirstBeginToPresentUs\":%lld,\"apiLastEndToPresentUs\":%lld,"
			"\"frameWallUs\":%lld,\"presentWallUs\":%lld,\"frameMinusApiTotalUs\":%lld,"
			"\"frameMinusApiSpanUs\":%lld,\"apiTotalPctX100\":%lld,\"apiSpanPctX100\":%lld",
			present,
			static_cast<long long>(calls),
			static_cast<long long>(nestedCalls),
			static_cast<long long>(totalUs),
			static_cast<long long>(apiSpanUs),
			static_cast<long long>(maxGapUs),
			ApiWallBucketName(firstBucket),
			ApiWallBucketName(lastBucket),
			ApiWallBucketName(maxGapBeforeBucket),
			ApiWallBucketName(maxGapAfterBucket),
			maxGapBeforeThreadId,
			maxGapAfterThreadId,
			static_cast<LONG>(maxGapBeforeThreadId == maxGapAfterThreadId),
			static_cast<long long>(maxGapThreadKernelDeltaUs),
			static_cast<long long>(maxGapThreadUserDeltaUs),
			static_cast<long long>(maxGapThreadCpuDeltaUs),
			static_cast<long long>(maxGapBeginQpc),
			static_cast<long long>(maxGapEndQpc),
			static_cast<unsigned long long>(lastFrameLatencyHandle),
			static_cast<unsigned long long>(lastFrameLatencySwapChain),
			lastFrameLatencyThreadId,
			lastFrameLatencyPresent,
			static_cast<long long>(lastFrameLatencyQpc),
			static_cast<unsigned long long>(lastFenceEventHandle),
			static_cast<unsigned long long>(lastFenceEventFence),
			static_cast<long long>(lastFenceEventValue),
			static_cast<long long>(lastFenceEventCompletedBefore),
			static_cast<long long>(lastFenceEventCompletedAfter),
			lastFenceEventThreadId,
			lastFenceEventPresent,
			static_cast<long long>(lastFenceEventQpc),
			static_cast<unsigned long>(lastFenceEventHr),
			WaitApiName(lastSystemWaitApi),
			static_cast<unsigned long long>(lastSystemWaitHandle),
			static_cast<unsigned long long>(lastSystemWaitSecondaryHandle),
			static_cast<long long>(lastSystemWaitUs),
			static_cast<unsigned long>(lastSystemWaitResult),
			lastSystemWaitError,
			lastSystemWaitThreadId,
			lastSystemWaitPresent,
			static_cast<long long>(lastSystemWaitQpc),
			lastSystemWaitMatchesFenceEvent,
			lastSystemWaitMatchesFrameLatency,
			static_cast<long long>(beforeFirstApiUs),
			static_cast<long long>(afterLastApiUs),
			static_cast<long long>(firstBeginToPresentUs),
			static_cast<long long>(lastEndToPresentUs),
			static_cast<long long>(frameWallUs),
			static_cast<long long>(presentWallUs),
			static_cast<long long>(SafeSubtract(frameWallUs, totalUs)),
			static_cast<long long>(SafeSubtract(frameWallUs, apiSpanUs)),
			static_cast<long long>(frameWallUs > 0 ? (totalUs * 10000 / frameWallUs) : 0),
			static_cast<long long>(frameWallUs > 0 ? (apiSpanUs * 10000 / frameWallUs) : 0));
	}

	static LONG FindCpuStageBucketLocked(const char *category, const char *stage, UINT kind)
	{
		for (LONG index = 0; index < CpuStageBucketCount; ++index) {
			if (CpuStageBuckets[index].kind == kind &&
					LabelEquals(CpuStageBuckets[index].category, category) &&
					LabelEquals(CpuStageBuckets[index].stage, stage)) {
				return index;
			}
		}
		if (CpuStageBucketCount >= static_cast<LONG>(sizeof(CpuStageBuckets) / sizeof(CpuStageBuckets[0])))
			return -1;
		const LONG newIndex = CpuStageBucketCount;
		++CpuStageBucketCount;
		CopyLabel(CpuStageBuckets[newIndex].category, sizeof(CpuStageBuckets[newIndex].category), category);
		CopyLabel(CpuStageBuckets[newIndex].stage, sizeof(CpuStageBuckets[newIndex].stage), stage);
		CpuStageBuckets[newIndex].kind = kind;
		return newIndex;
	}

	static LONG FindCpuStageBucketCached(const char *category, const char *stage, UINT kind)
	{
		for (LONG index = 0; index < CpuStageCache.count; ++index) {
			const CpuStageLookupEntry &entry = CpuStageCache.entries[index];
			if (entry.category == category && entry.stage == stage && entry.kind == kind)
				return entry.index;
		}

		AcquireSRWLockExclusive(&CpuStageLock);
		const LONG bucketIndex = FindCpuStageBucketLocked(category, stage, kind);
		ReleaseSRWLockExclusive(&CpuStageLock);
		if (bucketIndex >= 0 && CpuStageCache.count < ARRAYSIZE(CpuStageCache.entries)) {
			CpuStageLookupEntry &entry = CpuStageCache.entries[CpuStageCache.count++];
			entry.category = category;
			entry.stage = stage;
			entry.kind = kind;
			entry.index = bucketIndex;
		}
		return bucketIndex;
	}

	static void UpdateCpuStageMax(LONG64 *maximum, LONG64 value)
	{
		LONG64 observed = InterlockedCompareExchange64(maximum, 0, 0);
		while (value > observed) {
			const LONG64 previous = InterlockedCompareExchange64(maximum, value, observed);
			if (previous == observed)
				break;
			observed = previous;
		}
	}

	static void LogCpuStage(
		const char *category,
		const char *stage,
		UINT kind,
		size_t itemCount,
		LONG64 us)
	{
#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
		const LONG64 safeUs = ClampUs(us);
		const LONG index = FindCpuStageBucketCached(category, stage, kind);
		if (index >= 0) {
			CpuStageBucket &bucket = CpuStageBuckets[index];
			InterlockedIncrement64(&bucket.calls);
			InterlockedExchangeAdd64(&bucket.totalUs, safeUs);
			UpdateCpuStageMax(&bucket.maxUs, safeUs);
			InterlockedExchangeAdd64(&bucket.items, static_cast<LONG64>(itemCount));
		}
#else
		(void)category;
		(void)stage;
		(void)kind;
		(void)itemCount;
		(void)us;
#endif
	}

	static void LogCpuStageTimingState(LONG present, LONG64 frameWallUs)
	{
		CpuStageBucket snapshot[160] = {};
		LONG snapshotCount = 0;
		AcquireSRWLockExclusive(&CpuStageLock);
		snapshotCount = CpuStageBucketCount;
		for (LONG index = 0; index < snapshotCount; ++index) {
			CopyLabel(snapshot[index].category, sizeof(snapshot[index].category), CpuStageBuckets[index].category);
			CopyLabel(snapshot[index].stage, sizeof(snapshot[index].stage), CpuStageBuckets[index].stage);
			snapshot[index].kind = CpuStageBuckets[index].kind;
			snapshot[index].calls = InterlockedExchange64(&CpuStageBuckets[index].calls, 0);
			snapshot[index].totalUs = InterlockedExchange64(&CpuStageBuckets[index].totalUs, 0);
			snapshot[index].maxUs = InterlockedExchange64(&CpuStageBuckets[index].maxUs, 0);
			snapshot[index].items = InterlockedExchange64(&CpuStageBuckets[index].items, 0);
		}
		ReleaseSRWLockExclusive(&CpuStageLock);

		for (LONG index = 0; index < snapshotCount; ++index) {
			if (!snapshot[index].calls && !snapshot[index].totalUs)
				continue;
			if (snapshot[index].totalUs < 40 && snapshot[index].maxUs < 10)
				continue;
			DX12LogManager::Fields("DX12CpuStageTiming",
				"\"present\":%ld,\"category\":\"%s\",\"stage\":\"%s\",\"kind\":%u,"
				"\"calls\":%lld,\"items\":%lld,\"totalUs\":%lld,\"maxUs\":%lld,"
				"\"frameWallUs\":%lld,\"framePctX100\":%lld",
				present,
				snapshot[index].category,
				snapshot[index].stage,
				snapshot[index].kind,
				static_cast<long long>(snapshot[index].calls),
				static_cast<long long>(snapshot[index].items),
				static_cast<long long>(snapshot[index].totalUs),
				static_cast<long long>(snapshot[index].maxUs),
				static_cast<long long>(frameWallUs),
				static_cast<long long>(
					frameWallUs > 0 ? (snapshot[index].totalUs * 10000 / frameWallUs) : 0));
		}
	}

	static void LogLogCostState(LONG present, LONG64 frameWallUs)
	{
		DX12LogManager::DX12LogCostSnapshot snapshot = {};
		DX12LogManager::TakeLogCostSnapshot(&snapshot);
		if (!snapshot.fieldCalls && !snapshot.textCalls && !snapshot.debugFieldCalls &&
				!snapshot.enqueueCalls && !snapshot.formatUs && !snapshot.enqueueUs &&
				!snapshot.droppedLines) {
			return;
		}
		const LONG64 totalUs = snapshot.formatUs + snapshot.enqueueUs;
		if (totalUs < 40 && !snapshot.droppedLines)
			return;
		DX12LogManager::Fields("DX12LogCost",
			"\"present\":%ld,\"fieldCalls\":%lld,\"textCalls\":%lld,"
			"\"debugFieldCalls\":%lld,\"enqueueCalls\":%lld,\"formatUs\":%lld,"
			"\"enqueueUs\":%lld,\"totalUs\":%lld,\"droppedLines\":%lld,"
			"\"frameWallUs\":%lld,\"framePctX100\":%lld",
			present,
			static_cast<long long>(snapshot.fieldCalls),
			static_cast<long long>(snapshot.textCalls),
			static_cast<long long>(snapshot.debugFieldCalls),
			static_cast<long long>(snapshot.enqueueCalls),
			static_cast<long long>(snapshot.formatUs),
			static_cast<long long>(snapshot.enqueueUs),
			static_cast<long long>(totalUs),
			static_cast<long long>(snapshot.droppedLines),
			static_cast<long long>(frameWallUs),
			static_cast<long long>(frameWallUs > 0 ? (totalUs * 10000 / frameWallUs) : 0));
	}

	static void LogPresentEnter(const char *api, IDXGISwapChain *swapChain, UINT syncInterval, UINT flags)
	{
	#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
		(void)api;
		(void)swapChain;
		(void)syncInterval;
		(void)flags;

		const LARGE_INTEGER now = Now();
		const LONGLONG previousEnter = InterlockedExchange64(&LastPresentEnterQpc, now.QuadPart);
		const LONGLONG previousLeave = InterlockedCompareExchange64(&LastPresentLeaveQpc, 0, 0);
		LONG64 frameIntervalUs = 0;
		LONG64 outsideUs = 0;
		if (previousEnter > 0)
			frameIntervalUs = DeltaUs(previousEnter, now.QuadPart);
		if (previousLeave > 0)
			outsideUs = DeltaUs(previousLeave, now.QuadPart);
		InterlockedExchange64(&CurrentFrameIntervalUs, frameIntervalUs);
		InterlockedExchange64(&CurrentOutsideBetweenPresentsUs, outsideUs);
		InterlockedExchange64(&CurrentPresentEnterQpc, now.QuadPart);
		InterlockedIncrement(&PresentEnterCalls);
	#else
		(void)api;
		(void)swapChain;
		(void)syncInterval;
		(void)flags;
	#endif
	}

	static void LogPresentLeave(const char *api, IDXGISwapChain *swapChain, HRESULT hr)
	{
	#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
		const LARGE_INTEGER now = Now();
		const LONGLONG enter = InterlockedCompareExchange64(&CurrentPresentEnterQpc, 0, 0);
		const LONGLONG previousLeave = InterlockedCompareExchange64(&LastPresentLeaveQpc, 0, 0);
		const LONG64 presentWallUs = enter > 0 ? DeltaUs(enter, now.QuadPart) : 0;
		const LONG64 frameIntervalUs = InterlockedCompareExchange64(&CurrentFrameIntervalUs, 0, 0);
		const LONG64 outsideUs = InterlockedCompareExchange64(&CurrentOutsideBetweenPresentsUs, 0, 0);
		const LONG64 frameWallUs = outsideUs + presentWallUs;

		InterlockedIncrement(&PresentLeaveCalls);
		LogFrameTimeline(
			api,
			swapChain,
			hr,
			frameIntervalUs,
			outsideUs,
			presentWallUs,
			frameWallUs,
			previousLeave,
			now.QuadPart);
		InterlockedExchange64(&LastPresentLeaveQpc, now.QuadPart);
	#else
		(void)api;
		(void)swapChain;
		(void)hr;
	#endif
	}

	static void LogPresentSlice(const char *api, const char *stage, IDXGISwapChain *swapChain, HRESULT hr, LONG64 us)
	{
	#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
		(void)api;
		(void)swapChain;
		(void)hr;

		const LONG64 safeUs = ClampUs(us);
		const bool isPostOriginalRuntimeDetail =
			StageEquals(stage, "present_maintenance") ||
			StageEquals(stage, "hunt_on_present") ||
			StageEquals(stage, "post_present_ui");
		if (!isPostOriginalRuntimeDetail)
			AddUs(&PresentSliceKnownUs, safeUs);
		if (StageEquals(stage, "original_present") || StageEquals(stage, "original_present_skip")) {
			AddUs(&PresentOriginalUs, safeUs);
		} else if (StageEquals(stage, "post_original_runtime") ||
			StageEquals(stage, "post_original_runtime_total") ||
			StageEquals(stage, "post_original_skip")) {
			AddUs(&PresentPostOriginalRuntimeUs, safeUs);
		} else if (StageEquals(stage, "mod_begin_frame")) {
			AddUs(&PresentModBeginFrameUs, safeUs);
		} else if (StageEquals(stage, "profiling_and_hotpath_flush")) {
			AddUs(&PresentProfilingFlushUs, safeUs);
		} else if (StageEquals(stage, "consume_ui_actions") ||
			StageEquals(stage, "consume_ui_actions_poll") ||
			StageEquals(stage, "consume_ui_actions_apply")) {
			AddUs(&PresentConsumeUiActionsUs, safeUs);
		} else if (StageEquals(stage, "per_frame_counters")) {
			AddUs(&PresentPerFrameCountersUs, safeUs);
		} else if (StageEquals(stage, "capture_bookkeeping")) {
			AddUs(&PresentCaptureBookkeepingUs, safeUs);
		} else if (StageEquals(stage, "capture_complete_or_begin")) {
			AddUs(&PresentCaptureCompleteUs, safeUs);
		} else if (StageEquals(stage, "hotpath_update")) {
			AddUs(&PresentHotPathUpdateUs, safeUs);
		} else {
			AddUs(&PresentOtherUs, safeUs);
		}
	#else
		(void)api;
		(void)stage;
		(void)swapChain;
		(void)hr;
		(void)us;
	#endif
	}

	static void LogQueueExecuteSlice(const char *stage, ID3D12CommandQueue *queue, UINT numCommandLists, LONG64 us)
	{
	#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
		(void)queue;

		const LONG64 safeUs = ClampUs(us);
		InterlockedIncrement64(&QueueExecuteStageSamples);
		if (IsQueueWallStage(stage)) {
			InterlockedIncrement64(&QueueExecuteWallCalls);
			InterlockedAdd64(&QueueExecuteWallCommandLists, static_cast<LONG64>(numCommandLists));
		}

		if (StageEquals(stage, "wrapper_total")) {
			AddUs(&QueueWrapperTotalUs, safeUs);
		} else if (StageEquals(stage, "total") ||
			StageEquals(stage, "total_skip") ||
			StageEquals(stage, "total_internal_replay") ||
			StageEquals(stage, "total_no_original")) {
			AddUs(&QueueFlowTotalUs, safeUs);
		} else if (StageEquals(stage, "original") ||
			StageEquals(stage, "original_skip") ||
			StageEquals(stage, "original_internal_replay")) {
			AddUs(&QueueOriginalUs, safeUs);
		} else if (StageEquals(stage, "mod_notify_submitted")) {
			AddUs(&QueueNotifyUs, safeUs);
		} else if (StageEquals(stage, "register_submitted") ||
			StageEquals(stage, "register_submitted_skip")) {
			AddUs(&QueueRegisterUs, safeUs);
		} else if (StageEquals(stage, "unwrap_command_lists")) {
			AddUs(&QueueUnwrapUs, safeUs);
		} else if (StageEquals(stage, "rebuild_work_flags")) {
			AddUs(&QueueRebuildWorkFlagsUs, safeUs);
		} else {
			AddUs(&QueueOtherUs, safeUs);
		}
#else
		(void)stage;
		(void)queue;
		(void)numCommandLists;
		(void)us;
#endif
	}

	static bool IsQueueWallStage(const char *stage)
	{
		return StageEquals(stage, "wrapper_total") ||
			StageEquals(stage, "total") ||
			StageEquals(stage, "total_skip") ||
			StageEquals(stage, "total_internal_replay") ||
			StageEquals(stage, "total_no_original");
	}

	static void LogQueueFenceSlice(
		const char *api,
		const char *stage,
		ID3D12CommandQueue *queue,
		ID3D12Fence *fence,
		UINT64 value,
		UINT64 completedBefore,
		UINT64 completedAfter,
		HRESULT hr,
		LONG64 us)
	{
	#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
		(void)stage;
		(void)queue;
		(void)fence;
		(void)value;
		(void)completedBefore;
		(void)completedAfter;
		(void)hr;

		const LONG64 safeUs = ClampUs(us);
		InterlockedIncrement64(&QueueFenceCalls);
		if (StageEquals(api, "Signal")) {
			AddUs(&QueueSignalUs, safeUs);
		} else if (StageEquals(api, "Wait")) {
			AddUs(&QueueWaitUs, safeUs);
		}
	#else
		(void)api;
		(void)stage;
		(void)queue;
		(void)fence;
		(void)value;
		(void)completedBefore;
		(void)completedAfter;
		(void)hr;
		(void)us;
	#endif
	}

	static void LogFenceSlice(
		const char *api,
		const char *stage,
		ID3D12Fence *fence,
		UINT64 value,
		UINT64 completedBefore,
		UINT64 completedAfter,
		HRESULT hr,
		LONG64 us)
	{
	#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
		(void)api;
		(void)stage;
		(void)fence;
		(void)value;
		(void)completedBefore;
		(void)completedAfter;
		(void)hr;

		InterlockedIncrement64(&FenceCalls);
		AddUs(&FenceTotalUs, us);
	#else
		(void)api;
		(void)stage;
		(void)fence;
		(void)value;
		(void)completedBefore;
		(void)completedAfter;
		(void)hr;
		(void)us;
	#endif
	}

	static void LogEventWaitSlice(const char *api, const char *stage, HANDLE eventHandle, DWORD waitMs, DWORD waitResult, DWORD lastError, LONG64 us)
	{
	#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
		(void)api;
		(void)stage;
		(void)eventHandle;
		(void)waitMs;
		(void)waitResult;
		(void)lastError;

		InterlockedIncrement64(&EventWaitCalls);
		AddUs(&EventWaitTotalUs, us);
	#else
		(void)api;
		(void)stage;
		(void)eventHandle;
		(void)waitMs;
		(void)waitResult;
		(void)lastError;
		(void)us;
	#endif
	}

	static void LogWorkerTask(const char *queueName, const char *stage, UINT taskKind, UINT64 sequence, size_t batchSize, size_t pending, DWORD enqueueThreadId, LONG64 us)
	{
	#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
		(void)taskKind;
		(void)sequence;
		(void)batchSize;
		(void)pending;
		(void)enqueueThreadId;

		InterlockedIncrement64(&WorkerTaskCalls);
		AddUs(&WorkerTaskUs, us);
		LogCpuStage(queueName, stage, taskKind, batchSize, us);
	#else
		(void)queueName;
		(void)stage;
		(void)taskKind;
		(void)sequence;
		(void)batchSize;
		(void)pending;
		(void)enqueueThreadId;
		(void)us;
	#endif
	}

	static void LogWorkerWait(const char *queueName, const char *reason, const char *mode, UINT64 targetSequence, UINT64 completedSequence, size_t pending, LONG64 us)
	{
	#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
		(void)queueName;
		(void)reason;
		(void)targetSequence;
		(void)completedSequence;
		(void)pending;

		const LONG64 safeUs = ClampUs(us);
		InterlockedIncrement64(&WorkerWaitCalls);
		AddUs(&WorkerWaitUs, safeUs);
		if (StageEquals(mode, "worker_idle")) {
			AddUs(&WorkerIdleWaitUs, safeUs);
		} else {
			AddUs(&WorkerDrainWaitUs, safeUs);
		}
	#else
		(void)queueName;
		(void)reason;
		(void)mode;
		(void)targetSequence;
		(void)completedSequence;
		(void)pending;
		(void)us;
	#endif
	}

	static LONG64 EstimatedQueueTotalUs(LONG64 queueWrapperTotalUs, LONG64 queueFlowTotalUs)
	{
		return queueWrapperTotalUs > 0 ? queueWrapperTotalUs : queueFlowTotalUs;
	}

	static LONG64 SafeSubtract(LONG64 left, LONG64 right)
	{
		if (left <= right)
			return 0;
		return left - right;
	}

	static void LogFrameTimeline(
		const char *api,
		IDXGISwapChain *swapChain,
		HRESULT hr,
		LONG64 frameIntervalUs,
		LONG64 outsideUs,
		LONG64 presentWallUs,
		LONG64 frameWallUs,
		LONGLONG frameStartQpc,
		LONGLONG frameEndQpc)
	{
		const LONG present = DX12RuntimeState::GetPresentCount();
		const LONG presentEnterCalls = TakeLong(&PresentEnterCalls);
		const LONG presentLeaveCalls = TakeLong(&PresentLeaveCalls);
		const LONG64 presentSliceKnownUs = TakeLong64(&PresentSliceKnownUs);
		const LONG64 presentOriginalUs = TakeLong64(&PresentOriginalUs);
		const LONG64 presentPostOriginalRuntimeUs = TakeLong64(&PresentPostOriginalRuntimeUs);
		const LONG64 presentModBeginFrameUs = TakeLong64(&PresentModBeginFrameUs);
		const LONG64 presentProfilingFlushUs = TakeLong64(&PresentProfilingFlushUs);
		const LONG64 presentConsumeUiActionsUs = TakeLong64(&PresentConsumeUiActionsUs);
		const LONG64 presentPerFrameCountersUs = TakeLong64(&PresentPerFrameCountersUs);
		const LONG64 presentCaptureBookkeepingUs = TakeLong64(&PresentCaptureBookkeepingUs);
		const LONG64 presentCaptureCompleteUs = TakeLong64(&PresentCaptureCompleteUs);
		const LONG64 presentHotPathUpdateUs = TakeLong64(&PresentHotPathUpdateUs);
		const LONG64 presentOtherUs = TakeLong64(&PresentOtherUs);

		const LONG64 queueExecuteStageSamples = TakeLong64(&QueueExecuteStageSamples);
		const LONG64 queueExecuteWallCalls = TakeLong64(&QueueExecuteWallCalls);
		const LONG64 queueExecuteWallCommandLists = TakeLong64(&QueueExecuteWallCommandLists);
		const LONG64 queueWrapperTotalUs = TakeLong64(&QueueWrapperTotalUs);
		const LONG64 queueFlowTotalUs = TakeLong64(&QueueFlowTotalUs);
		const LONG64 queueOriginalUs = TakeLong64(&QueueOriginalUs);
		const LONG64 queueNotifyUs = TakeLong64(&QueueNotifyUs);
		const LONG64 queueRegisterUs = TakeLong64(&QueueRegisterUs);
		const LONG64 queueUnwrapUs = TakeLong64(&QueueUnwrapUs);
		const LONG64 queueRebuildWorkFlagsUs = TakeLong64(&QueueRebuildWorkFlagsUs);
		const LONG64 queueOtherUs = TakeLong64(&QueueOtherUs);
		const LONG64 queueEstimatedTotalUs = EstimatedQueueTotalUs(queueWrapperTotalUs, queueFlowTotalUs);

		const LONG64 queueFenceCalls = TakeLong64(&QueueFenceCalls);
		const LONG64 queueSignalUs = TakeLong64(&QueueSignalUs);
		const LONG64 queueWaitUs = TakeLong64(&QueueWaitUs);
		const LONG64 fenceCalls = TakeLong64(&FenceCalls);
		const LONG64 fenceTotalUs = TakeLong64(&FenceTotalUs);
		const LONG64 eventWaitCalls = TakeLong64(&EventWaitCalls);
		const LONG64 eventWaitTotalUs = TakeLong64(&EventWaitTotalUs);
		const LONG64 systemWaitCalls = TakeLong64(&SystemWaitCalls);
		const LONG64 systemWaitTotalUs = TakeLong64(&SystemWaitTotalUs);

		const LONG64 workerTaskCalls = TakeLong64(&WorkerTaskCalls);
		const LONG64 workerTaskUs = TakeLong64(&WorkerTaskUs);
		const LONG64 workerWaitCalls = TakeLong64(&WorkerWaitCalls);
		const LONG64 workerWaitUs = TakeLong64(&WorkerWaitUs);
		const LONG64 workerIdleWaitUs = TakeLong64(&WorkerIdleWaitUs);
		const LONG64 workerDrainWaitUs = TakeLong64(&WorkerDrainWaitUs);

		LogCpuTimelineCoverageState(
			present,
			frameStartQpc,
			frameEndQpc,
			frameWallUs,
			presentWallUs);
		LogApiWallTimingState(present, frameWallUs);
		LogCpuStageTimingState(present, frameWallUs);

		DX12LogManager::Fields("DX12FrameTimeline",
			"\"present\":%ld,\"api\":\"%s\",\"swapchain\":\"%p\",\"hr\":\"0x%lx\","
			"\"frameIntervalUs\":%lld,\"frameWallUs\":%lld,"
			"\"outsidePresentUs\":%lld,"
			"\"presentWallUs\":%lld,\"presentSliceKnownUs\":%lld,\"presentOriginalUs\":%lld,"
			"\"presentPostOriginalRuntimeUs\":%lld,\"presentModBeginFrameUs\":%lld,"
			"\"presentProfilingFlushUs\":%lld,\"presentConsumeUiActionsUs\":%lld,"
			"\"presentPerFrameCountersUs\":%lld,\"presentCaptureBookkeepingUs\":%lld,"
			"\"presentCaptureCompleteUs\":%lld,\"presentHotPathUpdateUs\":%lld,"
			"\"presentOtherUs\":%lld,\"presentEnterCalls\":%ld,\"presentLeaveCalls\":%ld,"
			"\"queueExecuteStageSamples\":%lld,\"queueExecuteWallCalls\":%lld,"
			"\"queueExecuteWallCommandLists\":%lld,\"queueEstimatedTotalUs\":%lld,"
			"\"queueWrapperTotalUs\":%lld,\"queueFlowTotalUs\":%lld,\"queueOriginalUs\":%lld,"
			"\"queueNotifyUs\":%lld,\"queueRegisterUs\":%lld,\"queueUnwrapUs\":%lld,"
			"\"queueRebuildWorkFlagsUs\":%lld,\"queueOtherUs\":%lld,"
			"\"queueFenceCalls\":%lld,\"queueSignalUs\":%lld,\"queueWaitUs\":%lld,"
			"\"fenceCalls\":%lld,\"fenceTotalUs\":%lld,\"eventWaitCalls\":%lld,"
			"\"eventWaitTotalUs\":%lld,\"systemWaitCalls\":%lld,\"systemWaitTotalUs\":%lld,"
			"\"workerTaskCalls\":%lld,\"workerTaskUs\":%lld,"
			"\"workerWaitCalls\":%lld,\"workerWaitUs\":%lld,\"workerIdleWaitUs\":%lld,"
			"\"workerDrainWaitUs\":%lld,"
			"\"presentWallMinusKnownSlicesUs\":%lld",
			present,
			api ? api : "",
			swapChain,
			static_cast<long>(hr),
			static_cast<long long>(frameIntervalUs),
			static_cast<long long>(frameWallUs),
			static_cast<long long>(outsideUs),
			static_cast<long long>(presentWallUs),
			static_cast<long long>(presentSliceKnownUs),
			static_cast<long long>(presentOriginalUs),
			static_cast<long long>(presentPostOriginalRuntimeUs),
			static_cast<long long>(presentModBeginFrameUs),
			static_cast<long long>(presentProfilingFlushUs),
			static_cast<long long>(presentConsumeUiActionsUs),
			static_cast<long long>(presentPerFrameCountersUs),
			static_cast<long long>(presentCaptureBookkeepingUs),
			static_cast<long long>(presentCaptureCompleteUs),
			static_cast<long long>(presentHotPathUpdateUs),
			static_cast<long long>(presentOtherUs),
			presentEnterCalls,
			presentLeaveCalls,
			static_cast<long long>(queueExecuteStageSamples),
			static_cast<long long>(queueExecuteWallCalls),
			static_cast<long long>(queueExecuteWallCommandLists),
			static_cast<long long>(queueEstimatedTotalUs),
			static_cast<long long>(queueWrapperTotalUs),
			static_cast<long long>(queueFlowTotalUs),
			static_cast<long long>(queueOriginalUs),
			static_cast<long long>(queueNotifyUs),
			static_cast<long long>(queueRegisterUs),
			static_cast<long long>(queueUnwrapUs),
			static_cast<long long>(queueRebuildWorkFlagsUs),
			static_cast<long long>(queueOtherUs),
			static_cast<long long>(queueFenceCalls),
			static_cast<long long>(queueSignalUs),
			static_cast<long long>(queueWaitUs),
			static_cast<long long>(fenceCalls),
			static_cast<long long>(fenceTotalUs),
			static_cast<long long>(eventWaitCalls),
			static_cast<long long>(eventWaitTotalUs),
			static_cast<long long>(systemWaitCalls),
			static_cast<long long>(systemWaitTotalUs),
			static_cast<long long>(workerTaskCalls),
			static_cast<long long>(workerTaskUs),
			static_cast<long long>(workerWaitCalls),
			static_cast<long long>(workerWaitUs),
			static_cast<long long>(workerIdleWaitUs),
			static_cast<long long>(workerDrainWaitUs),
			static_cast<long long>(SafeSubtract(presentWallUs, presentSliceKnownUs)));
		LogLogCostState(present, frameWallUs);
	}
};
