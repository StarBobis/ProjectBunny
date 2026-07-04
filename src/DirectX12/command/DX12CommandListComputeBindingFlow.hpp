#pragma once

#include <d3d12.h>

#include "DX12CommandListFlowDiagnostics.hpp"
#include "DX12CommandListRuntime.hpp"
#include "DX12CommandListStateCapture.hpp"
#include "DX12HotPathState.hpp"
#include "DX12Profiling.hpp"

class DX12CommandListComputeBindingFlow
{
public:
	static void ResolveDescriptorHeaps(
		UINT count,
		ID3D12DescriptorHeap *const *heaps,
		ID3D12DescriptorHeap **cbvSrvUav,
		ID3D12DescriptorHeap **sampler)
	{
		if (cbvSrvUav)
			*cbvSrvUav = nullptr;
		if (sampler)
			*sampler = nullptr;
		if (!heaps)
			return;

		for (UINT i = 0; i < count; ++i) {
			if (!heaps[i])
				continue;
			D3D12_DESCRIPTOR_HEAP_DESC desc = heaps[i]->GetDesc();
			if (desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) {
				if (cbvSrvUav)
					*cbvSrvUav = heaps[i];
			} else if (desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER) {
				if (sampler)
					*sampler = heaps[i];
			}
		}
	}

	static void CaptureDescriptorHeaps(
		ID3D12GraphicsCommandList *commandList,
		UINT count,
		ID3D12DescriptorHeap *const *heaps)
	{
		if (DX12InternalReplayState::IsActive())
			return;
		if (DX12HotPathState::skipAll) {
			DX12_PROFILE_FAST_FORWARD();
			return;
		}

		ID3D12DescriptorHeap *cbvSrvUav = nullptr;
		ID3D12DescriptorHeap *sampler = nullptr;
		ResolveDescriptorHeaps(count, heaps, &cbvSrvUav, &sampler);
		DX12CommandListRuntime::SetDescriptorHeaps(commandList, cbvSrvUav, sampler);

		const DX12CommandListTrackingState tracking =
			DX12CommandListStateCapture::GetTrackingState(commandList);
		const bool skipTrackedCapture =
			DX12HotPathState::skipBindings && !tracking.trackBindings;
		if (skipTrackedCapture) {
			DX12_PROFILE_FAST_FORWARD();
			return;
		}

		DX12CommandListFlowDiagnostics::LogDX12Call(
			"ID3D12GraphicsCommandList::SetDescriptorHeaps", commandList,
			" count=%u", count);
		DX12CommandListStateCapture::DescriptorHeaps(commandList, cbvSrvUav, sampler);
	}

	static void UpdateComputeBindingHash(
		ID3D12GraphicsCommandList *commandList, uint64_t hashComponent)
	{
		if (!hashComponent)
			return;
		DX12CommandListRuntimeState *state =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		if (!state)
			return;
		state->computeBindingHash ^= hashComponent;
		state->computeBindingHash ^= state->computeBindingHash >> 33;
		state->computeBindingHash *= 0xff51afd7ed558ccdull;
	}

	static void CaptureRootSignature(
		ID3D12GraphicsCommandList *commandList,
		ID3D12RootSignature *rootSignature,
		bool graphics)
	{
		const DX12CommandListTrackingState tracking =
			DX12CommandListStateCapture::GetTrackingState(commandList);
		// graphics 只看 skipGraphicsBindings（PreSkin 只需 compute bindings）
		const bool skipTrackedCapture = graphics ?
			DX12HotPathState::skipGraphicsBindings :
			(DX12HotPathState::skipBindings && !tracking.trackBindings);
		if (skipTrackedCapture) {
			DX12_PROFILE_FAST_FORWARD();
			return;
		}

		DX12CommandListFlowDiagnostics::LogDX12Call(
			graphics ? "ID3D12GraphicsCommandList::SetGraphicsRootSignature" :
				"ID3D12GraphicsCommandList::SetComputeRootSignature",
			commandList, " rootSignature=%p", rootSignature);
		if (graphics) {
			DX12CommandListStateCapture::GraphicsRootSignature(commandList, rootSignature);
		} else {
			DX12CommandListStateCapture::ComputeRootSignature(commandList, rootSignature);
			UpdateComputeBindingHash(commandList,
				reinterpret_cast<uint64_t>(rootSignature));
		}
	}

	static void CaptureRootDescriptorTable(
		ID3D12GraphicsCommandList *commandList,
		UINT rootParameterIndex,
		D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor,
		bool graphics)
	{
		if (DX12InternalReplayState::IsActive())
			return;
		const DX12CommandListTrackingState tracking =
			DX12CommandListStateCapture::GetTrackingState(commandList);
		// graphics 只看 skipGraphicsBindings（PreSkin 只需 compute bindings）
		const bool skipTrackedCapture = graphics ?
			DX12HotPathState::skipGraphicsBindings :
			(DX12HotPathState::skipBindings && !tracking.trackBindings);
		if (skipTrackedCapture) {
			DX12_PROFILE_FAST_FORWARD();
			return;
		}

		DX12CommandListFlowDiagnostics::LogDX12Call(
			graphics ? "ID3D12GraphicsCommandList::SetGraphicsRootDescriptorTable" :
				"ID3D12GraphicsCommandList::SetComputeRootDescriptorTable",
			commandList, " root=%u gpu=0x%llx",
			rootParameterIndex, static_cast<unsigned long long>(baseDescriptor.ptr));
		if (graphics) {
			DX12CommandListStateCapture::GraphicsRootDescriptorTable(
				commandList, rootParameterIndex, baseDescriptor);
		} else {
			DX12CommandListStateCapture::ComputeRootDescriptorTable(
				commandList, rootParameterIndex, baseDescriptor);
			// 只用稳定的 root parameter index，不用 descriptor heap ptr（每帧变化）
			UpdateComputeBindingHash(commandList,
				static_cast<uint64_t>(rootParameterIndex) ^ 0x9e3779b97f4a7c15ull);
		}
	}

	static void CaptureRoot32BitConstant(
		ID3D12GraphicsCommandList *commandList,
		UINT rootParameterIndex,
		UINT srcData,
		UINT destOffset,
		bool graphics)
	{
		const bool skipBindingCapture = graphics ?
			DX12HotPathState::skipGraphicsBindings :
			DX12HotPathState::skipBindings;
		if (skipBindingCapture) {
			DX12_PROFILE_FAST_FORWARD();
			return;
		}

		DX12CommandListFlowDiagnostics::LogDX12Call(
			graphics ? "ID3D12GraphicsCommandList::SetGraphicsRoot32BitConstant" :
				"ID3D12GraphicsCommandList::SetComputeRoot32BitConstant",
			commandList, " root=%u destOffset=%u", rootParameterIndex, destOffset);
		if (graphics) {
			DX12CommandListStateCapture::GraphicsRoot32BitConstant(
				commandList, rootParameterIndex, srcData, destOffset);
		} else {
			DX12CommandListStateCapture::ComputeRoot32BitConstant(
				commandList, rootParameterIndex, srcData, destOffset);
		}
	}

	static void CaptureRoot32BitConstants(
		ID3D12GraphicsCommandList *commandList,
		UINT rootParameterIndex,
		UINT num32BitValuesToSet,
		const void *srcData,
		UINT destOffset,
		bool graphics)
	{
		const bool skipBindingCapture = graphics ?
			DX12HotPathState::skipGraphicsBindings :
			DX12HotPathState::skipBindings;
		if (skipBindingCapture) {
			DX12_PROFILE_FAST_FORWARD();
			return;
		}

		DX12CommandListFlowDiagnostics::LogDX12Call(
			graphics ? "ID3D12GraphicsCommandList::SetGraphicsRoot32BitConstants" :
				"ID3D12GraphicsCommandList::SetComputeRoot32BitConstants",
			commandList, " root=%u values=%u destOffset=%u",
			rootParameterIndex, num32BitValuesToSet, destOffset);
		if (graphics) {
			DX12CommandListStateCapture::GraphicsRoot32BitConstants(
				commandList, rootParameterIndex, num32BitValuesToSet, srcData, destOffset);
		} else {
			DX12CommandListStateCapture::ComputeRoot32BitConstants(
				commandList, rootParameterIndex, num32BitValuesToSet, srcData, destOffset);
		}
	}

	static void CaptureRootDescriptor(
		ID3D12GraphicsCommandList *commandList,
		UINT rootParameterIndex,
		D3D12_ROOT_PARAMETER_TYPE type,
		D3D12_GPU_VIRTUAL_ADDRESS address,
		bool graphics,
		const char *api)
	{
		const bool skipBindingCapture = graphics ?
			DX12HotPathState::skipGraphicsBindings :
			DX12HotPathState::skipBindings;
		if (skipBindingCapture) {
			DX12_PROFILE_FAST_FORWARD();
			return;
		}

		DX12CommandListFlowDiagnostics::LogDX12Call(
			api, commandList, " root=%u gpu=0x%llx",
			rootParameterIndex, static_cast<unsigned long long>(address));
		if (graphics) {
			DX12CommandListStateCapture::GraphicsRootDescriptor(
				commandList, rootParameterIndex, type, address);
		} else {
			DX12CommandListStateCapture::ComputeRootDescriptor(
				commandList, rootParameterIndex, type, address);
		}
	}
};
