// Texture override matching and ExecuteIndirect runtime patching.
// Split from DX12Runtime.h; keep declarations in the layer that owns them.
#pragma once

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
	#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
			DX12LogManager::Text("DX12ExecuteIndirectRuntime: no PSO from runtime state\n");
#endif
			return;
		}

		DX12PsoShaderInfo shaderInfo = {};
		if (!DX12GetPipelineStateShaderInfo(pipelineState, &shaderInfo)) {
	#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
			DX12LogManager::Text("DX12ExecuteIndirectRuntime: GetShaderInfo failed\n");
#endif
			return;
		}
		if (!shaderInfo.hasCS) {
	#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
			DX12LogManager::Text("DX12ExecuteIndirectRuntime: PSO is not compute\n");
#endif
			return;
		}

		probe->computeShaderHash = shaderInfo.cs;
	#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
		DX12LogManager::Text("DX12ExecuteIndirectRuntime: compute PSO cs=%016llx\n",
			static_cast<unsigned long long>(probe->computeShaderHash));
#endif
		if (!DX12ModShouldTrackPreSkinBindingsForCs(probe->computeShaderHash)) {
	#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
			DX12LogManager::Text("DX12ExecuteIndirectRuntime: CS not in pre-skin list\n");
#endif
			return;
		}

	#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
		DX12LogManager::Text("DX12ExecuteIndirectRuntime: pre-skin CS match\n");
#endif
		probe->bindingsFound = DX12BindingTracker::GetCurrentComputeAll(
			commandList, &probe->uavs, &probe->srvs, &probe->cbvs, &probe->rootConstants);
		if (!probe->bindingsFound) {
	#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
			DX12LogManager::Text("DX12ExecuteIndirectRuntime: no compute bindings found\n");
#endif
			return;
		}

	#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
		DX12LogManager::Text(
			"DX12ExecuteIndirectRuntime: bindings found uavs=%zu srvs=%zu cbvs=%zu\n",
			probe->uavs.size(), probe->srvs.size(), probe->cbvs.size());
#endif
		probe->applied = DX12ModApplyPreSkinningUavReplacement(
			commandList, probe->computeShaderHash,
			probe->uavs, probe->srvs, probe->cbvs, probe->rootConstants,
			&probe->originalVertexCount, &probe->overrideVertexCount,
			&probe->dispatchOverride);
		if (probe->applied) {
	#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
			DX12LogManager::Text("DX12ExecuteIndirectRuntime: pre-skin applied origVtx=%u newVtx=%u\n",
				probe->originalVertexCount, probe->overrideVertexCount);
#endif
			return;
		}
	#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
		DX12LogManager::Text("DX12ExecuteIndirectRuntime: pre-skin matching failed\n");
#endif
	}

	static void FinishPreSkinProbe(
		ID3D12GraphicsCommandList *commandList,
		const PreSkinProbe &probe)
	{
		(void)probe;
		DX12ModRestorePreSkinningUavReplacement(commandList);
	}
};


