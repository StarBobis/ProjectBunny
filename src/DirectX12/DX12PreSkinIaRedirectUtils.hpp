#pragma once

#include <Windows.h>
#include <d3d12.h>

#include <algorithm>

#include "DX12CommandListRuntime.hpp"
#include "DX12LogManager.hpp"
#include "DX12ModRuntime.hpp"
#include "DX12ActiveIaState.hpp"
#include "DX12IaReplacementExecutorCallbacks.hpp"

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
		const UINT64 resizeGeneration = DX12ModGetPreSkinOutputResizeGeneration();
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
						cached.StrideInBytes == sourceView.StrideInBytes)
					continue;
			}
			DX12PreSkinOutputResizeBinding binding;
			if (!DX12ModResolvePreSkinOutputResizeByGpuVa(
				    sourceView.BufferLocation, &binding)) {
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
			if (!availableBytes)
				continue;
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
#if defined(_DEBUG)
			DX12LogManager::DebugJsonFunc("DX12PreSkinningApply",
				"\"status\":\"vb_redirected\","
				"\"slot\":%u,"
				"\"oldGpuVa\":\"0x%llx\","
				"\"newGpuVa\":\"0x%llx\","
				"\"oldBytes\":%u,"
				"\"newBytes\":%u,"
				"\"stride\":%u,"
				"\"offset\":%llu,"
				"\"uavBarrier\":%s,"
				"\"transition\":%s",
				startSlot + i,
				static_cast<unsigned long long>(sourceView.BufferLocation),
				static_cast<unsigned long long>(redirectedGpuVa),
				sourceView.SizeInBytes,
				redirectedViews[i].SizeInBytes,
				redirectedViews[i].StrideInBytes,
				static_cast<unsigned long long>(binding.sourceOffset),
				binding.uavBarrierPending ? "true" : "false",
				binding.transitionToVertex ? "true" : "false");
#endif
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
};
