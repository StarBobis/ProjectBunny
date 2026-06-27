#pragma once

#include <d3d12.h>

#include "DX12CommandListRuntime.h"
#include "DX12ModRuntime.h"

typedef void(STDMETHODCALLTYPE *DX12IaExecDrawInstanced)(
	ID3D12GraphicsCommandList*, UINT, UINT, UINT, UINT);
typedef void(STDMETHODCALLTYPE *DX12IaExecDrawIndexedInstanced)(
	ID3D12GraphicsCommandList*, UINT, UINT, UINT, INT, UINT);
typedef void(STDMETHODCALLTYPE *DX12IaExecDispatch)(
	ID3D12GraphicsCommandList*, UINT, UINT, UINT);
typedef void(STDMETHODCALLTYPE *DX12IaExecSetIndexBuffer)(
	ID3D12GraphicsCommandList*, const D3D12_INDEX_BUFFER_VIEW*);
typedef void(STDMETHODCALLTYPE *DX12IaExecSetVertexBuffers)(
	ID3D12GraphicsCommandList*, UINT, UINT, const D3D12_VERTEX_BUFFER_VIEW*);

struct DX12IaDrawInvocation
{
	bool indexed = false;
	uint32_t vertexCount = 0;
	uint32_t indexCount = 0;
	uint32_t instanceCount = 0;
	uint32_t firstVertex = 0;
	uint32_t firstIndex = 0;
	INT baseVertex = 0;
	uint32_t firstInstance = 0;
};

struct DX12IaReplacementExecutorCallbacks
{
	DX12IaExecDrawInstanced drawInstanced = nullptr;
	DX12IaExecDrawIndexedInstanced drawIndexedInstanced = nullptr;
	DX12IaExecDispatch dispatch = nullptr;
	DX12IaExecSetIndexBuffer setIndexBuffer = nullptr;
	DX12IaExecSetVertexBuffers setVertexBuffers = nullptr;
	void (*recordReplacementDraw)(bool indexed) = nullptr;
	void (*recordReplacementDispatch)() = nullptr;
};

bool DX12IaReplacementIsExecutingInternalDraw();
bool DX12IaReplacementExecutePresent(
	ID3D12GraphicsCommandList *commandList,
	const DX12CommandListRuntimeState &runtimeState,
	const DX12IaReplacementExecutorCallbacks &callbacks);
bool DX12IaReplacementApplyAndExecute(
	ID3D12GraphicsCommandList *commandList,
	const DX12IaDrawInvocation &draw,
	const DX12IaHashState &iaState,
	DX12ModIaReplacement *replacement,
	const DX12CommandListRuntimeState &runtimeState,
	const DX12IaReplacementExecutorCallbacks &callbacks);
bool DX12IaReplacementHandleDrawOverrides(
	ID3D12GraphicsCommandList *commandList,
	const DX12IaDrawInvocation &draw,
	const DX12CommandListRuntimeState &runtimeState,
	const DX12IaReplacementExecutorCallbacks &callbacks);
