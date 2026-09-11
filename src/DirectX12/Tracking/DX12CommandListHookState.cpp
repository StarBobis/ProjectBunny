// DX12CommandListHookState: original-vtable storage and hook registration state.
// Split from DX12CommandLists.cpp; implementation unit for its module.
#include "DX12Core.h"
#include "DX12Hooks.h"
#include "DX12Tracking.h"
#include "DX12Runtime.h"
#include "DX12Resources.h"
#include "DX12Presentation.h"
#include <d3d12.h>
#include <unordered_set>


DX12CommandListHookSignatures::DrawInstanced DX12CommandListHookState::OrigDrawInstanced = nullptr;
DX12CommandListHookSignatures::DrawIndexedInstanced DX12CommandListHookState::OrigDrawIndexedInstanced = nullptr;
DX12CommandListHookSignatures::Dispatch DX12CommandListHookState::OrigDispatch = nullptr;
DX12CommandListHookSignatures::IaSetPrimitiveTopology DX12CommandListHookState::OrigIASetPrimitiveTopology = nullptr;
DX12CommandListHookSignatures::SetPipelineState DX12CommandListHookState::OrigSetPipelineState = nullptr;
DX12CommandListHookSignatures::ResourceBarrier DX12CommandListHookState::OrigResourceBarrier = nullptr;
DX12CommandListHookSignatures::SetDescriptorHeaps DX12CommandListHookState::OrigSetDescriptorHeaps = nullptr;
DX12CommandListHookSignatures::SetRootSignature DX12CommandListHookState::OrigSetComputeRootSignature = nullptr;
DX12CommandListHookSignatures::SetRootSignature DX12CommandListHookState::OrigSetGraphicsRootSignature = nullptr;
DX12CommandListHookSignatures::SetRootDescriptorTable DX12CommandListHookState::OrigSetComputeRootDescriptorTable = nullptr;
DX12CommandListHookSignatures::SetRootDescriptorTable DX12CommandListHookState::OrigSetGraphicsRootDescriptorTable = nullptr;
DX12CommandListHookSignatures::SetRoot32BitConstant DX12CommandListHookState::OrigSetComputeRoot32BitConstant = nullptr;
DX12CommandListHookSignatures::SetRoot32BitConstant DX12CommandListHookState::OrigSetGraphicsRoot32BitConstant = nullptr;
DX12CommandListHookSignatures::SetRoot32BitConstants DX12CommandListHookState::OrigSetComputeRoot32BitConstants = nullptr;
DX12CommandListHookSignatures::SetRoot32BitConstants DX12CommandListHookState::OrigSetGraphicsRoot32BitConstants = nullptr;
DX12CommandListHookSignatures::SetRootGpuVa DX12CommandListHookState::OrigSetComputeRootConstantBufferView = nullptr;
DX12CommandListHookSignatures::SetRootGpuVa DX12CommandListHookState::OrigSetGraphicsRootConstantBufferView = nullptr;
DX12CommandListHookSignatures::SetRootGpuVa DX12CommandListHookState::OrigSetComputeRootShaderResourceView = nullptr;
DX12CommandListHookSignatures::SetRootGpuVa DX12CommandListHookState::OrigSetGraphicsRootShaderResourceView = nullptr;
DX12CommandListHookSignatures::SetRootGpuVa DX12CommandListHookState::OrigSetComputeRootUnorderedAccessView = nullptr;
DX12CommandListHookSignatures::SetRootGpuVa DX12CommandListHookState::OrigSetGraphicsRootUnorderedAccessView = nullptr;
DX12CommandListHookSignatures::IaSetIndexBuffer DX12CommandListHookState::OrigIASetIndexBuffer = nullptr;
DX12CommandListHookSignatures::IaSetVertexBuffers DX12CommandListHookState::OrigIASetVertexBuffers = nullptr;
DX12CommandListHookSignatures::ExecuteIndirect DX12CommandListHookState::OrigExecuteIndirect = nullptr;


thread_local DX12ThreadLocalCommandListOriginal DX12CommandListHookState::CommandListOriginals[64];
thread_local DX12VtableBaseCache DX12CommandListHookState::VtableBaseCache;
