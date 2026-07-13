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
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12CommandListComputeBindingFlow::CaptureDescriptorHeaps(
			commandList, runtimeState, count, heaps);
	}

	static void CaptureDescriptorHeaps(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeState *runtimeState,
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
		if (runtimeState)
			DX12CommandListRuntime::SetDescriptorHeaps(runtimeState, cbvSrvUav, sampler);
		else
			DX12CommandListRuntime::SetDescriptorHeaps(commandList, cbvSrvUav, sampler);

		const DX12CommandListTrackingState tracking =
			DX12CommandListStateCapture::GetTrackingState(commandList, runtimeState);
		const bool skipTrackedCapture =
			DX12HotPathState::skipBindings && !tracking.trackBindings;
		if (skipTrackedCapture) {
			DX12_PROFILE_FAST_FORWARD();
			return;
		}

		DX12CommandListFlowDiagnostics::LogDX12Call(
			"ID3D12GraphicsCommandList::SetDescriptorHeaps", commandList,
			" count=%u", count);
		DX12CommandListStateCapture::DescriptorHeaps(
			commandList, runtimeState, cbvSrvUav, sampler);
	}

	static void UpdateComputeBindingHash(
		DX12CommandListRuntimeState *runtimeState,
		uint64_t hashComponent)
	{
		if (!runtimeState || !hashComponent)
			return;
		runtimeState->computeBindingHash ^= hashComponent;
		runtimeState->computeBindingHash ^= runtimeState->computeBindingHash >> 33;
		runtimeState->computeBindingHash *= 0xff51afd7ed558ccdull;
	}

	static void UpdateComputeBindingHash(
		ID3D12GraphicsCommandList *commandList,
		uint64_t hashComponent)
	{
		if (!hashComponent)
			return;
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12CommandListComputeBindingFlow::UpdateComputeBindingHash(runtimeState, hashComponent);
	}

	static void CaptureRootSignature(
		ID3D12GraphicsCommandList *commandList,
		ID3D12RootSignature *rootSignature,
		bool graphics)
	{
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12CommandListComputeBindingFlow::CaptureRootSignature(
			commandList, runtimeState, rootSignature, graphics);
	}

	static void CaptureRootSignature(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeState *runtimeState,
		ID3D12RootSignature *rootSignature,
		bool graphics)
	{
		const DX12CommandListTrackingState tracking =
			DX12CommandListStateCapture::GetTrackingState(commandList, runtimeState);
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
			DX12CommandListStateCapture::GraphicsRootSignature(
				commandList, runtimeState, rootSignature);
		} else {
			DX12CommandListStateCapture::ComputeRootSignature(
				commandList, runtimeState, rootSignature);
			DX12CommandListComputeBindingFlow::UpdateComputeBindingHash(
				runtimeState, reinterpret_cast<uint64_t>(rootSignature));
		}
	}

	static void CaptureRootDescriptorTable(
		ID3D12GraphicsCommandList *commandList,
		UINT rootParameterIndex,
		D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor,
		bool graphics)
	{
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12CommandListComputeBindingFlow::CaptureRootDescriptorTable(
			commandList, runtimeState, rootParameterIndex, baseDescriptor, graphics);
	}

	static void CaptureRootDescriptorTable(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeState *runtimeState,
		UINT rootParameterIndex,
		D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor,
		bool graphics)
	{
		if (DX12InternalReplayState::IsActive())
			return;
		if (runtimeState) {
			DX12CommandListRuntime::RememberRootDescriptorTable(
				runtimeState, graphics, rootParameterIndex, baseDescriptor);
		} else {
			DX12CommandListRuntime::RememberRootDescriptorTable(
				commandList, graphics, rootParameterIndex, baseDescriptor);
		}
		const DX12CommandListTrackingState tracking =
			DX12CommandListStateCapture::GetTrackingState(commandList, runtimeState);
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
				commandList, runtimeState, rootParameterIndex, baseDescriptor);
		} else {
			DX12CommandListStateCapture::ComputeRootDescriptorTable(
				commandList, runtimeState, rootParameterIndex, baseDescriptor);
			DX12CommandListComputeBindingFlow::UpdateComputeBindingHash(
				runtimeState,
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
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12CommandListComputeBindingFlow::CaptureRoot32BitConstant(
			commandList, runtimeState, rootParameterIndex, srcData, destOffset, graphics);
	}

	static void CaptureRoot32BitConstant(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeState *runtimeState,
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
		if (graphics)
			DX12CommandListStateCapture::GraphicsRoot32BitConstant(
				commandList, runtimeState, rootParameterIndex, srcData, destOffset);
		else
			DX12CommandListStateCapture::ComputeRoot32BitConstant(
				commandList, runtimeState, rootParameterIndex, srcData, destOffset);
	}

	static void CaptureRoot32BitConstants(
		ID3D12GraphicsCommandList *commandList,
		UINT rootParameterIndex,
		UINT num32BitValuesToSet,
		const void *srcData,
		UINT destOffset,
		bool graphics)
	{
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12CommandListComputeBindingFlow::CaptureRoot32BitConstants(
			commandList, runtimeState, rootParameterIndex,
			num32BitValuesToSet, srcData, destOffset, graphics);
	}

	static void CaptureRoot32BitConstants(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeState *runtimeState,
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
		if (graphics)
			DX12CommandListStateCapture::GraphicsRoot32BitConstants(
				commandList, runtimeState, rootParameterIndex, num32BitValuesToSet, srcData, destOffset);
		else
			DX12CommandListStateCapture::ComputeRoot32BitConstants(
				commandList, runtimeState, rootParameterIndex, num32BitValuesToSet, srcData, destOffset);
	}

	static void CaptureRootDescriptor(
		ID3D12GraphicsCommandList *commandList,
		UINT rootParameterIndex,
		D3D12_ROOT_PARAMETER_TYPE type,
		D3D12_GPU_VIRTUAL_ADDRESS address,
		bool graphics,
		const char *api)
	{
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12CommandListComputeBindingFlow::CaptureRootDescriptor(
			commandList, runtimeState, rootParameterIndex, type, address, graphics, api);
	}

	static void CaptureRootDescriptor(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeState *runtimeState,
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
		if (graphics)
			DX12CommandListStateCapture::GraphicsRootDescriptor(
				commandList, runtimeState, rootParameterIndex, type, address);
		else
			DX12CommandListStateCapture::ComputeRootDescriptor(
				commandList, runtimeState, rootParameterIndex, type, address);
	}
};
