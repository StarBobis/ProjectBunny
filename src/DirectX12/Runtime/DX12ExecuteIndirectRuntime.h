// Texture override matching and ExecuteIndirect runtime patching.
// Split from DX12Runtime.h; keep declarations in the layer that owns them.
#pragma once

#include "../Config/DX12Config.h"
#include "DX12ResourceTrackerHooks.h"

class DX12TextureOverrideMatch
{
public:
	const Bunny::TextureOverrideConfig *config = nullptr;
	bool preRuntimeEffect = true;
	bool postRuntimeEffect = true;
	bool hasThisSrvTarget = false;
	bool compute = false;
	DX12CurrentShaderResourceBinding thisSrvBinding;
	uint32_t matchedHash = 0;
	uint32_t resourceHash = 0;
	uint32_t descriptorHash = 0;
};


class DX12ExecuteIndirectRuntime
{
public:
	using Fn = DX12CommandListHookSignatures::ExecuteIndirect;

	class PreSkinProbe
	{
	public:
		std::vector<DX12CurrentComputeUavBinding> uavs;
		std::vector<DX12CurrentComputeUavBinding> srvs;
		std::vector<DX12CurrentComputeUavBinding> cbvs;
		std::vector<DX12CurrentRootConstants> rootConstants;
		bool bindingsFound = false;
		bool applied = false;
		uint64_t computeShaderHash = 0;
		UINT originalVertexCount = 0;
		UINT overrideVertexCount = 0;
		DX12PreSkinDispatchOverride dispatchOverride;
	};

	static void Execute(
		ID3D12GraphicsCommandList *commandList,
		ID3D12CommandSignature *commandSignature,
		UINT maxCommandCount,
		ID3D12Resource *argumentBuffer,
		UINT64 argumentBufferOffset,
		ID3D12Resource *countBuffer,
		UINT64 countBufferOffset,
		Fn originalExecuteIndirect)
	{
		// Indirect draws/dispatches read descriptor tables: flush any
		// deferred draw-SRV restore before either forward below.
		DX12ModFlushDrawSrvReplacement(commandList);
		if (!DX12ModNeedsPreSkinningUavProbe()) {
			if (originalExecuteIndirect) {
				originalExecuteIndirect(commandList, commandSignature, maxCommandCount,
					argumentBuffer, argumentBufferOffset, countBuffer, countBufferOffset);
			}
			return;
		}

		PreSkinProbe &probe = ScratchProbe();
		PreparePreSkinProbe(commandList, &probe);

		if (originalExecuteIndirect) {
			originalExecuteIndirect(commandList, commandSignature, maxCommandCount,
				argumentBuffer, argumentBufferOffset, countBuffer, countBufferOffset);
		}

		FinishPreSkinProbe(commandList, probe);
	}

	static PreSkinProbe &ScratchProbe()
	{
		static thread_local PreSkinProbe probe;
		probe.uavs.clear();
		probe.srvs.clear();
		probe.cbvs.clear();
		probe.rootConstants.clear();
		probe.bindingsFound = false;
		probe.applied = false;
		probe.computeShaderHash = 0;
		probe.originalVertexCount = 0;
		probe.overrideVertexCount = 0;
		probe.dispatchOverride = DX12PreSkinDispatchOverride();
		return probe;
	}

	static void PreparePreSkinProbe(
		ID3D12GraphicsCommandList *commandList,
		PreSkinProbe *probe)
	{
		if (!probe || !DX12ModNeedsPreSkinningUavProbe())
			return;

		ID3D12PipelineState *pipelineState =
			DX12CommandListRuntime::GetPipelineState(commandList);
		if (!pipelineState) {
			DX12_LOG_TEXT("DX12ExecuteIndirectRuntime: no PSO from runtime state\n");
			return;
		}

		DX12PsoShaderInfo shaderInfo = {};
		if (!DX12GetPipelineStateShaderInfo(pipelineState, &shaderInfo)) {
			DX12_LOG_TEXT("DX12ExecuteIndirectRuntime: GetShaderInfo failed\n");
			return;
		}
		if (!shaderInfo.hasCS) {
			DX12_LOG_TEXT("DX12ExecuteIndirectRuntime: PSO is not compute\n");
			return;
		}

		probe->computeShaderHash = shaderInfo.cs;
		DX12_LOG_TEXT("DX12ExecuteIndirectRuntime: compute PSO cs=%016llx\n",
			static_cast<unsigned long long>(probe->computeShaderHash));
		if (!DX12ModShouldTrackPreSkinBindingsForCs(probe->computeShaderHash)) {
			DX12_LOG_TEXT("DX12ExecuteIndirectRuntime: CS not in pre-skin list\n");
			return;
		}

		DX12_LOG_TEXT("DX12ExecuteIndirectRuntime: pre-skin CS match\n");
		probe->bindingsFound = DX12BindingTracker::GetCurrentComputeAll(
			commandList, &probe->uavs, &probe->srvs, &probe->cbvs, &probe->rootConstants);
		if (!probe->bindingsFound) {
			DX12_LOG_TEXT("DX12ExecuteIndirectRuntime: no compute bindings found\n");
			return;
		}

		DX12_LOG_TEXT(
			"DX12ExecuteIndirectRuntime: bindings found uavs=%zu srvs=%zu cbvs=%zu\n",
			probe->uavs.size(), probe->srvs.size(), probe->cbvs.size());
		probe->applied = DX12ModApplyPreSkinningUavReplacement(
			commandList, probe->computeShaderHash,
			probe->uavs, probe->srvs, probe->cbvs, probe->rootConstants,
			&probe->originalVertexCount, &probe->overrideVertexCount,
			&probe->dispatchOverride);
		if (probe->applied) {
			DX12_LOG_TEXT("DX12ExecuteIndirectRuntime: pre-skin applied origVtx=%u newVtx=%u\n",
				probe->originalVertexCount, probe->overrideVertexCount);
			return;
		}
		DX12_LOG_TEXT("DX12ExecuteIndirectRuntime: pre-skin matching failed\n");
	}

	static void FinishPreSkinProbe(
		ID3D12GraphicsCommandList *commandList,
		const PreSkinProbe &probe)
	{
		(void)probe;
		DX12ModRestorePreSkinningUavReplacement(commandList);
	}
};




// Per-thread cache of the draw-path direct descriptor prefilter result.
// It caches values of DX12TextureOverrideMatch and is consumed by the
// command-list executor, so it lives in the Runtime layer beside the match
// type it holds by value. It used to be defined in
// ModRuntime/DX12ModRuntimeInternal.h, the faux translation unit that splices
// fragments together at fixed lines.
class DX12DirectDescriptorPrefilterCache
{
public:
	UINT64 reloadGeneration = 0;
	ID3D12GraphicsCommandList *commandList = nullptr;
	UINT64 descriptorQuerySerial = 0;
	bool usesGraphicsRootRevisions = false;
	std::vector<UINT> graphicsRootParameters;
	std::vector<UINT64> graphicsRootRevisions;
	ID3D12DescriptorHeap *descriptorHeap = nullptr;
	UINT64 descriptorHeapVersion = 0;
	UINT64 textureHashGeneration = 0;
	UINT64 targetPlanFingerprint = 0;
	uint32_t vertexCount = 0;
	uint32_t indexCount = 0;
	uint32_t instanceCount = 0;
	uint32_t firstVertex = 0;
	uint32_t firstIndex = 0;
	uint32_t firstInstance = 0;
	bool hasCandidate = false;
	std::unordered_map<
		DX12TextureOverrideTargetCacheKey,
		std::vector<DX12TextureOverrideMatch>,
		DX12TextureOverrideTargetCacheKeyHash> matchesByTarget;

	void Clear()
	{
		reloadGeneration = 0;
		commandList = nullptr;
		descriptorQuerySerial = 0;
		usesGraphicsRootRevisions = false;
		graphicsRootParameters.clear();
		graphicsRootRevisions.clear();
		descriptorHeap = nullptr;
		descriptorHeapVersion = 0;
		textureHashGeneration = 0;
		targetPlanFingerprint = 0;
		vertexCount = 0;
		indexCount = 0;
		instanceCount = 0;
		firstVertex = 0;
		firstIndex = 0;
		firstInstance = 0;
		hasCandidate = false;
		matchesByTarget.clear();
	}

	void SetInvocation(
		UINT64 generation,
		ID3D12GraphicsCommandList *list,
		const DX12SimulateContext *runtimeState,
		UINT64 currentDescriptorQuerySerial,
		ID3D12DescriptorHeap *currentDescriptorHeap,
		UINT64 currentDescriptorHeapVersion,
		UINT64 currentTextureHashGeneration,
		UINT64 currentTargetPlanFingerprint,
		uint32_t drawVertexCount,
		uint32_t drawIndexCount,
		uint32_t drawInstanceCount,
		uint32_t drawFirstVertex,
		uint32_t drawFirstIndex,
		uint32_t drawFirstInstance)
	{
		if (MatchesInvocation(
				generation,
				list,
				runtimeState,
				currentDescriptorQuerySerial,
				currentDescriptorHeap,
				currentDescriptorHeapVersion,
				currentTextureHashGeneration,
				currentTargetPlanFingerprint,
				drawVertexCount,
				drawIndexCount,
				drawInstanceCount,
				drawFirstVertex,
				drawFirstIndex,
				drawFirstInstance)) {
			return;
		}
		reloadGeneration = generation;
		commandList = list;
		descriptorQuerySerial = currentDescriptorQuerySerial;
		usesGraphicsRootRevisions = false;
		graphicsRootParameters.clear();
		graphicsRootRevisions.clear();
		descriptorHeap = currentDescriptorHeap;
		descriptorHeapVersion = currentDescriptorHeapVersion;
		textureHashGeneration = currentTextureHashGeneration;
		targetPlanFingerprint = currentTargetPlanFingerprint;
		vertexCount = drawVertexCount;
		indexCount = drawIndexCount;
		instanceCount = drawInstanceCount;
		firstVertex = drawFirstVertex;
		firstIndex = drawFirstIndex;
		firstInstance = drawFirstInstance;
		hasCandidate = false;
		matchesByTarget.clear();
	}

	bool MatchesInvocation(
		UINT64 generation,
		ID3D12GraphicsCommandList *list,
		const DX12SimulateContext *runtimeState,
		UINT64 currentDescriptorQuerySerial,
		ID3D12DescriptorHeap *currentDescriptorHeap,
		UINT64 currentDescriptorHeapVersion,
		UINT64 currentTextureHashGeneration,
		UINT64 currentTargetPlanFingerprint,
		uint32_t drawVertexCount,
		uint32_t drawIndexCount,
		uint32_t drawInstanceCount,
		uint32_t drawFirstVertex,
		uint32_t drawFirstIndex,
		uint32_t drawFirstInstance) const
	{
		if (reloadGeneration != generation ||
			commandList != list ||
			descriptorHeap != currentDescriptorHeap ||
			descriptorHeapVersion != currentDescriptorHeapVersion ||
			textureHashGeneration != currentTextureHashGeneration ||
			targetPlanFingerprint != currentTargetPlanFingerprint ||
			vertexCount != drawVertexCount ||
			indexCount != drawIndexCount ||
			instanceCount != drawInstanceCount ||
			firstVertex != drawFirstVertex ||
			firstIndex != drawFirstIndex ||
			firstInstance != drawFirstInstance) {
			return false;
		}
		if (!usesGraphicsRootRevisions)
			return descriptorQuerySerial == currentDescriptorQuerySerial;
		if (!runtimeState ||
			graphicsRootParameters.size() != graphicsRootRevisions.size())
			return false;
		for (size_t index = 0; index < graphicsRootParameters.size(); ++index) {
			const UINT root = graphicsRootParameters[index];
			if (root >= runtimeState->graphicsRootBindingRevisions.size() ||
				runtimeState->graphicsRootBindingRevisions[root] !=
					graphicsRootRevisions[index])
				return false;
		}
		return true;
	}

	void StoreGraphicsRootDependencies(
		const DX12SimulateContext *runtimeState,
		bool dependenciesKnown,
		const std::array<bool, MaxRootParameters> &rootSeen)
	{
		usesGraphicsRootRevisions = dependenciesKnown && runtimeState;
		graphicsRootParameters.clear();
		graphicsRootRevisions.clear();
		if (!usesGraphicsRootRevisions)
			return;
		for (UINT root = 0; root < rootSeen.size(); ++root) {
			if (!rootSeen[root])
				continue;
			graphicsRootParameters.push_back(root);
			graphicsRootRevisions.push_back(
				runtimeState->graphicsRootBindingRevisions[root]);
		}
		if (graphicsRootParameters.empty())
			usesGraphicsRootRevisions = false;
	}

	bool MatchesInvocation(
		UINT64 generation,
		ID3D12GraphicsCommandList *list,
		uint32_t drawVertexCount,
		uint32_t drawIndexCount,
		uint32_t drawInstanceCount,
		uint32_t drawFirstVertex,
		uint32_t drawFirstIndex,
		uint32_t drawFirstInstance) const
	{
		return reloadGeneration == generation &&
			commandList == list &&
			vertexCount == drawVertexCount &&
			indexCount == drawIndexCount &&
			instanceCount == drawInstanceCount &&
			firstVertex == drawFirstVertex &&
			firstIndex == drawFirstIndex &&
			firstInstance == drawFirstInstance;
	}
};


inline thread_local DX12DirectDescriptorPrefilterCache tlsDirectDescriptorPrefilterCache;
