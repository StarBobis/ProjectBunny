#pragma once

#include "DX12CommandListComputeBindingFlow.hpp"

class DX12CommandListComputeBindingHookCallbacks
{
public:
	// DX11 风格：skipAll=1 时所有 hook 零开销直通。
	// PreSkin 需要 compute 绑定跟踪 → skipAll 旁路。
	static bool ShouldSkipComputeBinding()
	{
		return DX12HotPathState::skipAll && !DX12ModNeedsPreSkinningBindingTracking();
	}

	static void STDMETHODCALLTYPE HookedSetDescriptorHeaps(
		ID3D12GraphicsCommandList *commandList, UINT count,
		ID3D12DescriptorHeap *const *heaps)
	{
		if (ShouldSkipComputeBinding()) {
			PFN_SET_DESCRIPTOR_HEAPS original =
				DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetDescriptorHeaps, PFN_SET_DESCRIPTOR_HEAPS, DX12CommandListHookState::OrigSetDescriptorHeaps, "SetDescriptorHeaps");
			if (original)
				original(commandList, count, heaps);
			return;
		}
		DX12_PROFILE_SCOPE(SetDescriptorHeaps);
		DX12CommandListComputeBindingFlow::CaptureDescriptorHeaps(commandList, count, heaps);
		PFN_SET_DESCRIPTOR_HEAPS original =
			DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetDescriptorHeaps, PFN_SET_DESCRIPTOR_HEAPS, DX12CommandListHookState::OrigSetDescriptorHeaps, "SetDescriptorHeaps");
		if (original)
			original(commandList, count, heaps);
	}

	static void STDMETHODCALLTYPE HookedSetComputeRootSignature(
		ID3D12GraphicsCommandList *commandList, ID3D12RootSignature *rootSignature)
	{
		if (ShouldSkipComputeBinding()) {
			PFN_SET_ROOT_SIGNATURE original =
				DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetComputeRootSignature, PFN_SET_ROOT_SIGNATURE, DX12CommandListHookState::OrigSetComputeRootSignature, "SetComputeRootSignature");
			if (original)
				original(commandList, rootSignature);
			return;
		}
		DX12_PROFILE_SCOPE(SetComputeRootSignature);
		DX12CommandListComputeBindingFlow::CaptureRootSignature(
			commandList, rootSignature, false);
		PFN_SET_ROOT_SIGNATURE original =
			DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetComputeRootSignature, PFN_SET_ROOT_SIGNATURE, DX12CommandListHookState::OrigSetComputeRootSignature, "SetComputeRootSignature");
		if (original)
			original(commandList, rootSignature);
	}

	static void STDMETHODCALLTYPE HookedSetGraphicsRootSignature(
		ID3D12GraphicsCommandList *commandList, ID3D12RootSignature *rootSignature)
	{
		if (DX12HotPathState::skipAll) {
			PFN_SET_ROOT_SIGNATURE original =
				DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetGraphicsRootSignature, PFN_SET_ROOT_SIGNATURE, DX12CommandListHookState::OrigSetGraphicsRootSignature, "SetGraphicsRootSignature");
			if (original)
				original(commandList, rootSignature);
			return;
		}
		DX12_PROFILE_SCOPE(SetGraphicsRootSignature);
		DX12CommandListComputeBindingFlow::CaptureRootSignature(
			commandList, rootSignature, true);
		PFN_SET_ROOT_SIGNATURE original =
			DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetGraphicsRootSignature, PFN_SET_ROOT_SIGNATURE, DX12CommandListHookState::OrigSetGraphicsRootSignature, "SetGraphicsRootSignature");
		if (original)
			original(commandList, rootSignature);
	}

	static void STDMETHODCALLTYPE HookedSetComputeRootDescriptorTable(
		ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
		D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor)
	{
		if (ShouldSkipComputeBinding()) {
			PFN_SET_ROOT_DESCRIPTOR_TABLE original =
				DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetComputeRootDescriptorTable, PFN_SET_ROOT_DESCRIPTOR_TABLE, DX12CommandListHookState::OrigSetComputeRootDescriptorTable, "SetComputeRootDescriptorTable");
			if (original)
				original(commandList, rootParameterIndex, baseDescriptor);
			return;
		}
		DX12_PROFILE_SCOPE(SetComputeRootDescriptorTable);
		DX12CommandListComputeBindingFlow::CaptureRootDescriptorTable(
			commandList, rootParameterIndex, baseDescriptor, false);
		PFN_SET_ROOT_DESCRIPTOR_TABLE original =
			DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetComputeRootDescriptorTable, PFN_SET_ROOT_DESCRIPTOR_TABLE, DX12CommandListHookState::OrigSetComputeRootDescriptorTable, "SetComputeRootDescriptorTable");
		if (original)
			original(commandList, rootParameterIndex, baseDescriptor);
	}

	static void STDMETHODCALLTYPE HookedSetGraphicsRootDescriptorTable(
		ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
		D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor)
	{
		if (DX12HotPathState::skipAll) {
			PFN_SET_ROOT_DESCRIPTOR_TABLE original =
				DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetGraphicsRootDescriptorTable, PFN_SET_ROOT_DESCRIPTOR_TABLE, DX12CommandListHookState::OrigSetGraphicsRootDescriptorTable, "SetGraphicsRootDescriptorTable");
			if (original)
				original(commandList, rootParameterIndex, baseDescriptor);
			return;
		}
		DX12_PROFILE_SCOPE(SetGraphicsRootDescriptorTable);
		DX12CommandListComputeBindingFlow::CaptureRootDescriptorTable(
			commandList, rootParameterIndex, baseDescriptor, true);
		PFN_SET_ROOT_DESCRIPTOR_TABLE original =
			DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetGraphicsRootDescriptorTable, PFN_SET_ROOT_DESCRIPTOR_TABLE, DX12CommandListHookState::OrigSetGraphicsRootDescriptorTable, "SetGraphicsRootDescriptorTable");
		if (original)
			original(commandList, rootParameterIndex, baseDescriptor);
	}

	static void STDMETHODCALLTYPE HookedSetComputeRoot32BitConstant(
		ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT srcData, UINT destOffset)
	{
		if (ShouldSkipComputeBinding()) {
			PFN_SET_ROOT_32BIT_CONSTANT original =
				DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetComputeRoot32BitConstant, PFN_SET_ROOT_32BIT_CONSTANT, DX12CommandListHookState::OrigSetComputeRoot32BitConstant, "SetComputeRoot32BitConstant");
			if (original)
				original(commandList, rootParameterIndex, srcData, destOffset);
			return;
		}
		DX12CommandListComputeBindingFlow::CaptureRoot32BitConstant(
			commandList, rootParameterIndex, srcData, destOffset, false);
		PFN_SET_ROOT_32BIT_CONSTANT original =
			DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetComputeRoot32BitConstant, PFN_SET_ROOT_32BIT_CONSTANT, DX12CommandListHookState::OrigSetComputeRoot32BitConstant, "SetComputeRoot32BitConstant");
		if (original)
			original(commandList, rootParameterIndex, srcData, destOffset);
	}

	static void STDMETHODCALLTYPE HookedSetGraphicsRoot32BitConstant(
		ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT srcData, UINT destOffset)
	{
		if (DX12HotPathState::skipAll) {
			PFN_SET_ROOT_32BIT_CONSTANT original =
				DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetGraphicsRoot32BitConstant, PFN_SET_ROOT_32BIT_CONSTANT, DX12CommandListHookState::OrigSetGraphicsRoot32BitConstant, "SetGraphicsRoot32BitConstant");
			if (original)
				original(commandList, rootParameterIndex, srcData, destOffset);
			return;
		}
		DX12CommandListComputeBindingFlow::CaptureRoot32BitConstant(
			commandList, rootParameterIndex, srcData, destOffset, true);
		PFN_SET_ROOT_32BIT_CONSTANT original =
			DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetGraphicsRoot32BitConstant, PFN_SET_ROOT_32BIT_CONSTANT, DX12CommandListHookState::OrigSetGraphicsRoot32BitConstant, "SetGraphicsRoot32BitConstant");
		if (original)
			original(commandList, rootParameterIndex, srcData, destOffset);
	}

	static void STDMETHODCALLTYPE HookedSetComputeRoot32BitConstants(
		ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT num32BitValuesToSet,
		const void *srcData, UINT destOffset)
	{
		if (ShouldSkipComputeBinding()) {
			PFN_SET_ROOT_32BIT_CONSTANTS original =
				DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetComputeRoot32BitConstants, PFN_SET_ROOT_32BIT_CONSTANTS, DX12CommandListHookState::OrigSetComputeRoot32BitConstants, "SetComputeRoot32BitConstants");
			if (original)
				original(commandList, rootParameterIndex, num32BitValuesToSet, srcData, destOffset);
			return;
		}
		DX12CommandListComputeBindingFlow::CaptureRoot32BitConstants(
			commandList, rootParameterIndex, num32BitValuesToSet, srcData, destOffset, false);
		PFN_SET_ROOT_32BIT_CONSTANTS original =
			DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetComputeRoot32BitConstants, PFN_SET_ROOT_32BIT_CONSTANTS, DX12CommandListHookState::OrigSetComputeRoot32BitConstants, "SetComputeRoot32BitConstants");
		if (original)
			original(commandList, rootParameterIndex, num32BitValuesToSet, srcData, destOffset);
	}

	static void STDMETHODCALLTYPE HookedSetGraphicsRoot32BitConstants(
		ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT num32BitValuesToSet,
		const void *srcData, UINT destOffset)
	{
		if (DX12HotPathState::skipAll) {
			PFN_SET_ROOT_32BIT_CONSTANTS original =
				DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetGraphicsRoot32BitConstants, PFN_SET_ROOT_32BIT_CONSTANTS, DX12CommandListHookState::OrigSetGraphicsRoot32BitConstants, "SetGraphicsRoot32BitConstants");
			if (original)
				original(commandList, rootParameterIndex, num32BitValuesToSet, srcData, destOffset);
			return;
		}
		DX12CommandListComputeBindingFlow::CaptureRoot32BitConstants(
			commandList, rootParameterIndex, num32BitValuesToSet, srcData, destOffset, true);
		PFN_SET_ROOT_32BIT_CONSTANTS original =
			DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetGraphicsRoot32BitConstants, PFN_SET_ROOT_32BIT_CONSTANTS, DX12CommandListHookState::OrigSetGraphicsRoot32BitConstants, "SetGraphicsRoot32BitConstants");
		if (original)
			original(commandList, rootParameterIndex, num32BitValuesToSet, srcData, destOffset);
	}

	static void STDMETHODCALLTYPE HookedSetComputeRootConstantBufferView(
		ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS address)
	{
		if (ShouldSkipComputeBinding()) {
			PFN_SET_ROOT_GPU_VA original =
				DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetComputeRootConstantBufferView, PFN_SET_ROOT_GPU_VA, DX12CommandListHookState::OrigSetComputeRootConstantBufferView, "SetComputeRootConstantBufferView");
			if (original)
				original(commandList, rootParameterIndex, address);
			return;
		}
		DX12_PROFILE_SCOPE(SetComputeRootConstantBufferView);
		DX12CommandListComputeBindingFlow::CaptureRootDescriptor(
			commandList, rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE_CBV, address, false,
			"ID3D12GraphicsCommandList::SetComputeRootConstantBufferView");
		PFN_SET_ROOT_GPU_VA original =
			DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetComputeRootConstantBufferView, PFN_SET_ROOT_GPU_VA, DX12CommandListHookState::OrigSetComputeRootConstantBufferView, "SetComputeRootConstantBufferView");
		if (original)
			original(commandList, rootParameterIndex, address);
	}

	static void STDMETHODCALLTYPE HookedSetGraphicsRootConstantBufferView(
		ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS address)
	{
		if (DX12HotPathState::skipAll) {
			PFN_SET_ROOT_GPU_VA original =
				DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetGraphicsRootConstantBufferView, PFN_SET_ROOT_GPU_VA, DX12CommandListHookState::OrigSetGraphicsRootConstantBufferView, "SetGraphicsRootConstantBufferView");
			if (original)
				original(commandList, rootParameterIndex, address);
			return;
		}
		DX12_PROFILE_SCOPE(SetGraphicsRootConstantBufferView);
		DX12CommandListComputeBindingFlow::CaptureRootDescriptor(
			commandList, rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE_CBV, address, true,
			"ID3D12GraphicsCommandList::SetGraphicsRootConstantBufferView");
		PFN_SET_ROOT_GPU_VA original =
			DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetGraphicsRootConstantBufferView, PFN_SET_ROOT_GPU_VA, DX12CommandListHookState::OrigSetGraphicsRootConstantBufferView, "SetGraphicsRootConstantBufferView");
		if (original)
			original(commandList, rootParameterIndex, address);
	}

	static void STDMETHODCALLTYPE HookedSetComputeRootShaderResourceView(
		ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS address)
	{
		if (ShouldSkipComputeBinding()) {
			PFN_SET_ROOT_GPU_VA original =
				DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetComputeRootShaderResourceView, PFN_SET_ROOT_GPU_VA, DX12CommandListHookState::OrigSetComputeRootShaderResourceView, "SetComputeRootShaderResourceView");
			if (original)
				original(commandList, rootParameterIndex, address);
			return;
		}
		DX12_PROFILE_SCOPE(SetComputeRootShaderResourceView);
		DX12CommandListComputeBindingFlow::CaptureRootDescriptor(
			commandList, rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE_SRV, address, false,
			"ID3D12GraphicsCommandList::SetComputeRootShaderResourceView");
		PFN_SET_ROOT_GPU_VA original =
			DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetComputeRootShaderResourceView, PFN_SET_ROOT_GPU_VA, DX12CommandListHookState::OrigSetComputeRootShaderResourceView, "SetComputeRootShaderResourceView");
		if (original)
			original(commandList, rootParameterIndex, address);
	}

	static void STDMETHODCALLTYPE HookedSetGraphicsRootShaderResourceView(
		ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS address)
	{
		if (DX12HotPathState::skipAll) {
			PFN_SET_ROOT_GPU_VA original =
				DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetGraphicsRootShaderResourceView, PFN_SET_ROOT_GPU_VA, DX12CommandListHookState::OrigSetGraphicsRootShaderResourceView, "SetGraphicsRootShaderResourceView");
			if (original)
				original(commandList, rootParameterIndex, address);
			return;
		}
		DX12_PROFILE_SCOPE(SetGraphicsRootShaderResourceView);
		DX12CommandListComputeBindingFlow::CaptureRootDescriptor(
			commandList, rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE_SRV, address, true,
			"ID3D12GraphicsCommandList::SetGraphicsRootShaderResourceView");
		PFN_SET_ROOT_GPU_VA original =
			DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetGraphicsRootShaderResourceView, PFN_SET_ROOT_GPU_VA, DX12CommandListHookState::OrigSetGraphicsRootShaderResourceView, "SetGraphicsRootShaderResourceView");
		if (original)
			original(commandList, rootParameterIndex, address);
	}

	static void STDMETHODCALLTYPE HookedSetComputeRootUnorderedAccessView(
		ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS address)
	{
		if (ShouldSkipComputeBinding()) {
			PFN_SET_ROOT_GPU_VA original =
				DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetComputeRootUnorderedAccessView, PFN_SET_ROOT_GPU_VA, DX12CommandListHookState::OrigSetComputeRootUnorderedAccessView, "SetComputeRootUnorderedAccessView");
			if (original)
				original(commandList, rootParameterIndex, address);
			return;
		}
		DX12_PROFILE_SCOPE(SetComputeRootUnorderedAccessView);
		DX12CommandListComputeBindingFlow::CaptureRootDescriptor(
			commandList, rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE_UAV, address, false,
			"ID3D12GraphicsCommandList::SetComputeRootUnorderedAccessView");
		PFN_SET_ROOT_GPU_VA original =
			DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetComputeRootUnorderedAccessView, PFN_SET_ROOT_GPU_VA, DX12CommandListHookState::OrigSetComputeRootUnorderedAccessView, "SetComputeRootUnorderedAccessView");
		if (original)
			original(commandList, rootParameterIndex, address);
	}
};
