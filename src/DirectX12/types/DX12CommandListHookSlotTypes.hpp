#pragma once

#include <Windows.h>

class DX12CommandListHookSlotTypes
{
public:
	static constexpr UINT Reset = 10;
	static constexpr UINT DrawInstanced = 12;
	static constexpr UINT DrawIndexedInstanced = 13;
	static constexpr UINT Dispatch = 14;
	static constexpr UINT IaSetPrimitiveTopology = 20;
	static constexpr UINT SetPipelineState = 25;
	static constexpr UINT ResourceBarrier = 26;
	static constexpr UINT ExecuteBundle = 27;
	static constexpr UINT SetDescriptorHeaps = 28;
	static constexpr UINT SetComputeRootSignature = 29;
	static constexpr UINT SetGraphicsRootSignature = 30;
	static constexpr UINT SetComputeRootDescriptorTable = 31;
	static constexpr UINT SetGraphicsRootDescriptorTable = 32;
	static constexpr UINT SetComputeRootConstantBufferView = 37;
	static constexpr UINT SetGraphicsRootConstantBufferView = 38;
	static constexpr UINT SetComputeRootShaderResourceView = 39;
	static constexpr UINT SetGraphicsRootShaderResourceView = 40;
	static constexpr UINT SetComputeRootUnorderedAccessView = 41;
	static constexpr UINT SetGraphicsRootUnorderedAccessView = 42;
	static constexpr UINT IaSetIndexBuffer = 43;
	static constexpr UINT IaSetVertexBuffers = 44;
	static constexpr UINT ExecuteIndirect = 59;
};

class DX12CommandQueueHookSlotTypes
{
public:
	static constexpr UINT ExecuteCommandLists = 10;
};

class DX12DeviceCommandListHookSlotTypes
{
public:
	static constexpr UINT CreateCommandQueue = 8;
	static constexpr UINT CreateCommandAllocator = 9;
	static constexpr UINT CreateCommandList = 12;
	static constexpr UINT Device4CreateCommandList1 = 52;
};
