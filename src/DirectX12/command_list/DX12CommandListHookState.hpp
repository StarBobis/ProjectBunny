#pragma once

#include <Windows.h>
#include <d3d12.h>

#include <array>
#include <unordered_set>

#include "DX12CommandListHookSignatures.hpp"
#include "DX12IaReplacementExecutor.hpp"

class DX12ThreadLocalCommandListOriginal
{
public:
	void *target = nullptr;
	void *original = nullptr;
};

class DX12VtableBaseCache
{
public:
	void *vtable = nullptr;
	std::array<void*, 256> originals{};
};

class DX12ThreadLocalIaCallbacksCache
{
public:
	ID3D12GraphicsCommandList *commandList = nullptr;
	DX12IaReplacementExecutorCallbacks callbacks;
};

class DX12CommandListHookState
{
public:
	static DX12CommandListHookSignatures::CreateCommandQueue OrigCreateCommandQueue;
	static DX12CommandListHookSignatures::CreateCommandAllocator OrigCreateCommandAllocator;
	static DX12CommandListHookSignatures::CreateCommandList OrigCreateCommandList;
	static DX12CommandListHookSignatures::CreateCommandList1 OrigCreateCommandList1;

	static DX12CommandListHookSignatures::QueueUpdateTileMappings OrigQueueUpdateTileMappings;
	static DX12CommandListHookSignatures::QueueCopyTileMappings OrigQueueCopyTileMappings;
	static DX12CommandListHookSignatures::QueueExecuteCommandLists OrigQueueExecuteCommandLists;
	static DX12CommandListHookSignatures::QueueSetMarker OrigQueueSetMarker;
	static DX12CommandListHookSignatures::QueueBeginEvent OrigQueueBeginEvent;
	static DX12CommandListHookSignatures::QueueEndEvent OrigQueueEndEvent;
	static DX12CommandListHookSignatures::QueueSignal OrigQueueSignal;
	static DX12CommandListHookSignatures::QueueWait OrigQueueWait;
	static DX12CommandListHookSignatures::QueueGetTimestampFrequency OrigQueueGetTimestampFrequency;
	static DX12CommandListHookSignatures::QueueGetClockCalibration OrigQueueGetClockCalibration;

	static DX12CommandListHookSignatures::CloseCommandList OrigCloseCommandList;
	static DX12CommandListHookSignatures::ResetCommandList OrigResetCommandList;
	static DX12CommandListHookSignatures::ClearState OrigClearState;
	static DX12CommandListHookSignatures::DrawInstanced OrigDrawInstanced;
	static DX12CommandListHookSignatures::DrawIndexedInstanced OrigDrawIndexedInstanced;
	static DX12CommandListHookSignatures::Dispatch OrigDispatch;
	static DX12CommandListHookSignatures::CopyBufferRegion OrigCopyBufferRegion;
	static DX12CommandListHookSignatures::CopyTextureRegion OrigCopyTextureRegion;
	static DX12CommandListHookSignatures::CopyResource OrigCopyResource;
	static DX12CommandListHookSignatures::CopyTiles OrigCopyTiles;
	static DX12CommandListHookSignatures::ResolveSubresource OrigResolveSubresource;
	static DX12CommandListHookSignatures::IaSetPrimitiveTopology OrigIASetPrimitiveTopology;
	static DX12CommandListHookSignatures::RsSetViewports OrigRSSetViewports;
	static DX12CommandListHookSignatures::RsSetScissorRects OrigRSSetScissorRects;
	static DX12CommandListHookSignatures::OmSetBlendFactor OrigOMSetBlendFactor;
	static DX12CommandListHookSignatures::OmSetStencilRef OrigOMSetStencilRef;
	static DX12CommandListHookSignatures::SetPipelineState OrigSetPipelineState;
	static DX12CommandListHookSignatures::ResourceBarrier OrigResourceBarrier;
	static DX12CommandListHookSignatures::ExecuteBundle OrigExecuteBundle;
	static DX12CommandListHookSignatures::SetDescriptorHeaps OrigSetDescriptorHeaps;
	static DX12CommandListHookSignatures::SetRootSignature OrigSetComputeRootSignature;
	static DX12CommandListHookSignatures::SetRootSignature OrigSetGraphicsRootSignature;
	static DX12CommandListHookSignatures::SetRootDescriptorTable OrigSetComputeRootDescriptorTable;
	static DX12CommandListHookSignatures::SetRootDescriptorTable OrigSetGraphicsRootDescriptorTable;
	static DX12CommandListHookSignatures::SetRoot32BitConstant OrigSetComputeRoot32BitConstant;
	static DX12CommandListHookSignatures::SetRoot32BitConstant OrigSetGraphicsRoot32BitConstant;
	static DX12CommandListHookSignatures::SetRoot32BitConstants OrigSetComputeRoot32BitConstants;
	static DX12CommandListHookSignatures::SetRoot32BitConstants OrigSetGraphicsRoot32BitConstants;
	static DX12CommandListHookSignatures::SetRootGpuVa OrigSetComputeRootConstantBufferView;
	static DX12CommandListHookSignatures::SetRootGpuVa OrigSetGraphicsRootConstantBufferView;
	static DX12CommandListHookSignatures::SetRootGpuVa OrigSetComputeRootShaderResourceView;
	static DX12CommandListHookSignatures::SetRootGpuVa OrigSetGraphicsRootShaderResourceView;
	static DX12CommandListHookSignatures::SetRootGpuVa OrigSetComputeRootUnorderedAccessView;
	static DX12CommandListHookSignatures::SetRootGpuVa OrigSetGraphicsRootUnorderedAccessView;
	static DX12CommandListHookSignatures::IaSetIndexBuffer OrigIASetIndexBuffer;
	static DX12CommandListHookSignatures::IaSetVertexBuffers OrigIASetVertexBuffers;
	static DX12CommandListHookSignatures::SoSetTargets OrigSOSetTargets;
	static DX12CommandListHookSignatures::OmSetRenderTargets OrigOMSetRenderTargets;
	static DX12CommandListHookSignatures::ClearDepthStencilView OrigClearDepthStencilView;
	static DX12CommandListHookSignatures::ClearRenderTargetView OrigClearRenderTargetView;
	static DX12CommandListHookSignatures::ClearUavUint OrigClearUnorderedAccessViewUint;
	static DX12CommandListHookSignatures::ClearUavFloat OrigClearUnorderedAccessViewFloat;
	static DX12CommandListHookSignatures::DiscardResource OrigDiscardResource;
	static DX12CommandListHookSignatures::BeginQuery OrigBeginQuery;
	static DX12CommandListHookSignatures::EndQuery OrigEndQuery;
	static DX12CommandListHookSignatures::ResolveQueryData OrigResolveQueryData;
	static DX12CommandListHookSignatures::SetPredication OrigSetPredication;
	static DX12CommandListHookSignatures::CommandListSetMarker OrigCommandListSetMarker;
	static DX12CommandListHookSignatures::CommandListBeginEvent OrigCommandListBeginEvent;
	static DX12CommandListHookSignatures::CommandListEndEvent OrigCommandListEndEvent;
	static DX12CommandListHookSignatures::ExecuteIndirect OrigExecuteIndirect;

	static SRWLOCK IaReplacementSummaryLock;
	static LONG IaReplacementSummaryPresent;
	static volatile LONG IaReplacementSummaryDraws;
	static volatile LONG IaReplacementSummaryIndexedDraws;
	static volatile LONG IaReplacementSummaryDispatches;

	static SRWLOCK HookedVTableLock;
	static std::unordered_set<void*> HookedCommandListVTables;
	static SRWLOCK RegisteredListLock;
	static std::unordered_set<void*> RegisteredCommandLists;
	static SRWLOCK KnownCommandQueueLock;
	static IUnknown *KnownCommandQueue;

	static thread_local DX12ThreadLocalCommandListOriginal CommandListOriginals[64];
	static thread_local DX12ThreadLocalCommandListOriginal CommandQueueOriginals[32];
	static thread_local DX12VtableBaseCache VtableBaseCache;
	static thread_local DX12ThreadLocalIaCallbacksCache IaCallbacksCache;
};
