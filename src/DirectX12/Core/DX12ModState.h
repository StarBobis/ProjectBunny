#pragma once

#include "DX12CommandFlow.h"

// Mod replacement interfaces, pre-skin state, and resource-runtime APIs.
// Umbrella header: class definitions were split into per-responsibility
// headers under Core/ModState; free-function declarations stay here and the
// includes below sit exactly where the classes used to be declared.


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
bool DX12ModGetPipelineStateSkipFlags(
	ID3D12PipelineState *pipelineState,
	const DX12PsoShaderInfo *shaderInfo,
	bool *skipDraw,
	bool *skipDispatch);
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
bool DX12ModMayNeedPreSkinIaViewExpansion(
	const D3D12_VERTEX_BUFFER_VIEW *views, const bool *hasViews, UINT count);
bool DX12ModShouldProbePreSkinningForCs(uint64_t computeShaderHash);
bool DX12ModShouldTrackPreSkinBindingsForCs(uint64_t computeShaderHash);
bool DX12ModPreSkinningUavProducerMayMatch(
	uint64_t computeShaderHash,
	const std::vector<DX12CurrentComputeUavBinding> &uavs);
class DX12PreSkinDispatchOverride;
void DX12ModStoreCachedPreSkinningUavMatch(
	ID3D12GraphicsCommandList *commandList,
	uint64_t computeShaderHash,
	UINT64 computeBindingSerial,
	UINT64 bindingDependencyFingerprint,
	bool matched,
	const DX12PreSkinDispatchOverride *dispatchOverride = nullptr);

#include "ModState/DX12ModIaReplacement.h"
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
std::shared_ptr<const DX12ModIaReplacement> DX12ModFindStaticReplacementSnapshotForPso(
	ID3D12PipelineState *pipelineState,
	UINT64 reloadGeneration);
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
// DX11-style IA-state-level negative check (draw-arg independent; only valid
// when the compiled prefilter tables are not count-sensitive).
bool DX12ModIaStateDefinitelyHasNoCandidate(
	const DX12ActiveIaState &ia,
	const DX12IaHashTargetPlan &targetPlan,
	bool indexed);
bool DX12ModIaPrefilterCountSensitive();
void DX12ModRunPostShaderOverrideReplacement(
	ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState,
	const DX12IaHashState &iaState,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance,
	DX12ModIaReplacement *replacement);
void DX12ModRestoreDrawSrvReplacement(
	ID3D12GraphicsCommandList *commandList,
	DX12ModIaReplacement *replacement);
// Deferred draw-SRV restore (lazy heap switch): Restore performs only the
// logical shadow restore and leaves the patched mirror heap bound on the
// list. Flush replays the GPU-side restore before a command that observes
// descriptor-heap state; Drop discards it silently when the game rebinds
// descriptor heaps itself; Forget is the teardown half for
// Reset/Unregister/ClearState.
void DX12ModFlushDrawSrvReplacement(ID3D12GraphicsCommandList *commandList);
void DX12ModDropDrawSrvReplacementForHeapChange(ID3D12GraphicsCommandList *commandList);
void DX12ModForgetDrawSrvReplacementForCommandList(ID3D12GraphicsCommandList *commandList);
bool DX12ModHasPendingDrawSrvRestore();
// Bundle draw-SRV replacement (DX12BundleSrvMirror.h): record-time rewrite
// of descriptor-table binds / SetDescriptorHeaps inside bundles, and the
// ExecuteBundle parent replay that binds the bundle's mirror session heap.
void DX12ModRewriteBundleRootDescriptorTable(
	ID3D12GraphicsCommandList *commandList, bool graphics,
	UINT rootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE *baseDescriptor);
void DX12ModRewriteBundleDescriptorHeaps(
	ID3D12GraphicsCommandList *commandList, UINT numHeaps,
	ID3D12DescriptorHeap **heaps);
bool DX12ModApplyBundleMirrorForExecuteBundle(
	ID3D12GraphicsCommandList *parent, ID3D12GraphicsCommandList *bundle);
void DX12ModRestoreBundleMirrorAfterExecuteBundle(
	ID3D12GraphicsCommandList *parent, ID3D12GraphicsCommandList *bundle);
void DX12ModForgetBundleMirrorSession(ID3D12GraphicsCommandList *commandList);
void DX12ModClearIaReplacementRuntimeCaches();
void DX12ModRecordGraphicsPipelineState(
	ID3D12Device *device, ID3D12PipelineState *pipelineState,
	const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc);
void DX12ModRecordComputePipelineState(
	ID3D12Device *device, ID3D12PipelineState *pipelineState,
	const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc);
ID3D12PipelineState *DX12ModGetReplacementPipelineState(ID3D12PipelineState *pipelineState);
bool DX12ModShouldSkipPipelineState(ID3D12PipelineState *pipelineState, bool dispatch);
#include "ModState/DX12PreSkinDispatchOverride.h"
bool DX12ModGetCachedPreSkinProbeResult(
	ID3D12GraphicsCommandList *commandList,
	uint64_t computeShaderHash,
	UINT64 bindingHash,
	UINT64 bindingDependencyFingerprint,
	DX12PreSkinProbeCacheEntry *result);
uint64_t DX12ModMakePreSkinNegativeMatchKey(
	uint64_t computeShaderHash,
	const std::vector<DX12CurrentComputeUavBinding> &uavs,
	const std::vector<DX12CurrentComputeUavBinding> &srvs);
bool DX12ModTestPreSkinNegativeMatch(uint64_t key);
void DX12ModStorePreSkinNegativeMatch(uint64_t key);
bool MayMatchPreSkinStickyMatch(
	uint64_t csHash,
	const std::vector<DX12CurrentComputeUavBinding> &uavs);
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
	UINT64 bindingFingerprint = 0,
	UINT64 bindingDependencyFingerprint = 0,
	bool *matchPhaseMiss = nullptr);
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
// Parks the custom shader element invocations recorded while the [Present]
// command list ran (phase 2.4). The replay itself happens at Present time.
bool DX12ModQueuePresentCustomElements(
	ID3D12GraphicsCommandList *commandList, DX12ModIaReplacement *replacement);
// Replays the parked element records onto the back buffer via a mod-owned
// command list submitted on the game's direct queue right before the
// original Present call (the DX11 [Present] semantic: after the game's
// rendering, before present).
bool DX12ModSubmitPresentOverlay(IDXGISwapChain *swapChain);
// Tracks the game's direct command queue from the ExecuteCommandLists
// wrapper so the overlay submit lands on the same queue the game renders to.
void DX12ModTrackGameDirectQueue(ID3D12CommandQueue *queue);
// Borrows the tracked game direct queue with a reference held; caller
// releases. Used by the in-game overlay text submission.
ID3D12CommandQueue *DX12ModAcquireGameDirectQueue();
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
#include "ModState/DX12PreSkinOutputResizeBinding.h"
bool DX12ModResolvePreSkinOutputResizeCacheByGpuVa(
	ID3D12GraphicsCommandList *commandList,
	UINT64 viewGpuVa, DX12PreSkinOutputResizeBinding *binding);
void DX12ModRegisterPreSkinOutputResize(
	ID3D12GraphicsCommandList *commandList,
	ID3D12Resource *originalResource, ID3D12Resource *resizedResource,
	UINT64 originalByteWidth, UINT64 resizedByteWidth, UINT stride);
// Publish the resize mapping before the skinning dispatch executes instead of
// at restore time. Shadow/draw lists recorded concurrently (or before this
// dispatch on another list) resolve IA views during recording; if the mapping
// is only registered after the dispatch, those draws miss it and render the
// un-skinned original buffer - the shadow-mismatch flicker. Registering early
// is safe: the mapping is a VA alias (contents are written by the dispatch
// before any dependent draw on the same list), and re-registration with equal
// content is a no-op that does not bump the generation.
void DX12ModPublishPreSkinOutputResizeEarly(
	ID3D12GraphicsCommandList *commandList);
bool DX12ModResolvePreSkinOutputResizeByGpuVa(
	ID3D12GraphicsCommandList *commandList,
	UINT64 viewGpuVa, DX12PreSkinOutputResizeBinding *binding);
bool DX12ModHasPreSkinOutputResizeBindings();
// Cheap hint that the fence-owned resize cache holds at least one entry. The
// publish map above is frame-local (cleared by Present); the IA replay gate
// uses this to keep resolving through the cache for consumers recorded on the
// other side of that boundary.
bool DX12ModHasPreSkinOutputResizeCacheEntries();
UINT64 DX12ModGetPreSkinOutputResizeGeneration();
void DX12ModForgetPreSkinOutputResizesForCommandList(
	ID3D12GraphicsCommandList *commandList);
void DX12ModForgetPreSkinReplacementForCommandList(
	ID3D12GraphicsCommandList *commandList);
void DX12ModForgetPendingPreSkinSubmissionsForCommandList(
	ID3D12GraphicsCommandList *commandList);
void DX12ModAbandonPreSkinOutputResizeCacheUses(
	ID3D12GraphicsCommandList *commandList);
void DX12ModTrackPreSkinOutputResizeCacheUse(
	ID3D12GraphicsCommandList *commandList,
	ID3D12Resource *vertexResource);
void DX12ModClearPreSkinOutputResizeMap();


#include "ModState/DX12PreSkinPlanUtils.h"


void DX12RecordPsoRootSignature(
	UINT64 psoIndex, const char *kind, ID3D12PipelineState *pipelineState,
	ID3D12RootSignature *rootSignature);
void DX12RecordResourceBarrier(UINT numBarriers, const D3D12_RESOURCE_BARRIER *barriers);
bool DX12ResolveBufferResourceByGpuVa(
	UINT64 gpuVirtualAddress, UINT64 size, DX12BufferResourceSummary *summary);
// Fast path: try GPU VA → ResourceWrapper lookup (sorted vector) before
// falling back to the full gBufferResourceByGpuVa std::map search.
bool DX12TryResolveFromGpuVaWrapper(
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

// ---------------------------------------------------------------------------
// Async content capture (copy-queue readback pipeline).
//
// DX11 kept CPU-side buffer content by memcpy'ing at Map/Unmap time on the
// recording thread; the DX12 port mirrored that with synchronous cache writes
// and global-epoch bumps on every Unmap/Copy call. This pipeline replaces the
// synchronous writes: recording threads only enqueue a tiny capture event
// (no memcpy, no cache lock, no epoch bump). A dedicated copy queue reads the
// buffer/texture content back into a readback ring after the game's submit,
// and a worker thread publishes the bytes into the existing content caches
// (DX12CacheBufferContentRange / DX12CacheTextureHash) when the fence lands.
// Draw-time consumers therefore see content one frame late, which is exact
// for static resources and self-corrects for streaming ones.
// Implemented in the tracking TU (DX12ResourceTracking.cpp).
bool DX12AsyncReadbackQueueBufferCapture(
	ID3D12Resource *resource, UINT64 offset, UINT64 size);
bool DX12AsyncReadbackQueueTextureCapture(
	ID3D12Resource *texture, const D3D12_RESOURCE_DESC *desc);
// Cheap volatile gate: true when at least one capture event is pending a
// submit. Called on the game's ExecuteCommandLists hot path.
bool DX12AsyncReadbackHasPendingWork();
// Submit all pending capture events on the copy queue, ordered after the
// given (game) queue via Signal/Wait. Called after the original
// ExecuteCommandLists call; pure queue operations on the mod-owned channel.
void DX12AsyncReadbackSubmitPending(ID3D12CommandQueue *gameQueue);
// Once-per-frame flush of the gated pending captures, called from the
// Present path. Collapses the per-submit fixed cost (queue Signal/Wait,
// command list recording, event creation) to a single payment per frame.
void DX12AsyncReadbackFlushFrameEnd();
void DX12AsyncReadbackShutdown();

// ---------------------------------------------------------------------------
// Async upload channel (ring of allocators, no per-upload CPU wait).
// The game's queue waits on the upload fence at its next ExecuteCommandLists
// instead of the caller blocking on WaitForSingleObject per upload.
bool DX12UploadChannelHasPendingWork();
void DX12UploadChannelWaitOnQueue(ID3D12CommandQueue *queue);

// Resource state query for the readback pipeline: returns the tracked current
// state of a resource (from creation initialState and recorded transitions).
// knownOut=false when no state is tracked; barrierTrackedOut=true when the
// state was observed from an explicit ResourceBarrier transition.
bool DX12GetResourceCurrentState(
	ID3D12Resource *resource,
	D3D12_RESOURCE_STATES *stateOut,
	bool *knownOut,
	bool *barrierTrackedOut);

// Write-time static texture replacement (plan step 3.1). Implemented in the
// mod TU (ModRuntime/DX12LoadedResources.cpp) and consumed by the tracking TU descriptor
// hooks; this TU (resource queries) provides the texture hash cache the
// resolution is keyed on.
struct DescriptorRecord;
// Resolves the write-time replacement SRV descriptor for an original texture
// hash. With sourceRecord the SRV is made view-compatible with the
// descriptor the game originally wrote; otherwise the DDS default SRV is
// used. Returns false when no unconditional static replacement exists.
bool DX12ResolveStaticTextureReplacementSrv(
	ID3D12Device *device,
	uint32_t originalHash,
	const DescriptorRecord *sourceRecord,
	D3D12_CPU_DESCRIPTOR_HANDLE *outSrv,
	ID3D12Resource **outResource,
	UINT64 *outTableGeneration = nullptr);
// True when every descriptor-relevant hashed texture override is an
// unconditional static replacement; gates the draw-path descriptor skip.
bool DX12StaticTextureReplacementAllDescriptorOverridesStatic();
// True when this original texture hash has a static write-time replacement.
bool DX12StaticTextureReplacementContainsHash(uint32_t originalHash);
// Cheap volatile gate so descriptor-copy hot paths can skip all patch work
// when no static replacement table is active.
bool DX12HasStaticTextureReplacements();
UINT64 DX12StaticTextureReplacementGeneration();
// Bind-time static IA replacement (plan step 3.2). Same gating idea for the
// IASet bind hot path.
bool DX12HasStaticIaReplacements();
// Resolves the static geometry replacement for a bound IB/VB content hash.
// Fills the replacement view the same way the draw-time path does; returns
// false when there is no static replacement so the caller binds the
// original view unchanged.
bool DX12ResolveStaticIaReplacement(
	ID3D12Device *device,
	bool indexBuffer,
	UINT vertexSlot,
	uint32_t contentHash,
	const D3D12_INDEX_BUFFER_VIEW *originalIndexView,
	D3D12_INDEX_BUFFER_VIEW *outIndexView,
	const D3D12_VERTEX_BUFFER_VIEW *originalVertexView,
	D3D12_VERTEX_BUFFER_VIEW *outVertexView);
bool DX12TrackTextureHashFromCopyTextureRegion(
	const D3D12_TEXTURE_COPY_LOCATION *dst, UINT dstX, UINT dstY, UINT dstZ,
	const D3D12_TEXTURE_COPY_LOCATION *src, const D3D12_BOX *srcBox);
UINT64 DX12GetIaHashContentGeneration();
UINT64 DX12GetIaHashResolveGeneration();
#include "ModState/DX12BufferResolveValidation.h"
uint32_t DX12HashDescriptorBufferView(
	const DX12DescriptorSummary *descriptor, UINT64 fallbackGpuVirtualAddress,
	UINT64 fallbackSize);
uint32_t DX12ComputeDescriptorHuntHash(
	ID3D12Device *device, const DescriptorRecord &record);
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

// The outer timing probe was unified into Core/Diag/DX12Diag.h.

