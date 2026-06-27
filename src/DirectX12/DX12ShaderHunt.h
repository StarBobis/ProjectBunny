#pragma once

#include <d3d12.h>
#include <dxgi.h>
#include <stdint.h>

#include "DX12CommandListRuntime.h"

void DX12HuntResetCommandList(ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState);
void DX12HuntSetPipelineState(ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState);
void DX12HuntSetIndexBuffer(
	ID3D12GraphicsCommandList *commandList, const D3D12_INDEX_BUFFER_VIEW *view);
void DX12HuntSetVertexBuffers(
	ID3D12GraphicsCommandList *commandList, UINT startSlot, UINT count,
	const D3D12_VERTEX_BUFFER_VIEW *views);
void DX12HuntRecordDraw(
	ID3D12GraphicsCommandList *commandList, bool indexed,
	UINT vertexCount, UINT indexCount, UINT firstVertex, UINT firstIndex);
void DX12HuntRecordDispatch(ID3D12GraphicsCommandList *commandList);
void DX12HuntRecordExecuteIndirect(ID3D12GraphicsCommandList *commandList);
bool DX12HuntShouldSkipDraw(
	ID3D12GraphicsCommandList *commandList, bool indexed,
	UINT vertexCount, UINT indexCount, UINT firstVertex, UINT firstIndex);
bool DX12HuntShouldSkipDispatch(ID3D12GraphicsCommandList *commandList);
bool DX12HuntIsEnabled();
bool DX12HuntShouldDrawOverlay();
bool DX12HuntGetIaHashes(
	ID3D12GraphicsCommandList *commandList, uint32_t *ibHash,
	uint32_t *vbHashes, size_t vbHashCount, size_t *vbHashWritten);
bool DX12HuntGetIaHashState(ID3D12GraphicsCommandList *commandList, DX12IaHashState *state);
bool DX12HuntHashIndexBufferView(const D3D12_INDEX_BUFFER_VIEW *view, uint32_t *hash);
bool DX12HuntHashVertexBufferView(UINT slot, const D3D12_VERTEX_BUFFER_VIEW *view, uint32_t *hash);

void DX12HuntToggle();
void DX12HuntOnPresent();
void DX12HuntPreviousVS();
void DX12HuntNextVS();
void DX12HuntPreviousPS();
void DX12HuntNextPS();
void DX12HuntPreviousCS();
void DX12HuntNextCS();
void DX12HuntPreviousIB();
void DX12HuntNextIB();
void DX12HuntPreviousVB();
void DX12HuntNextVB();
void DX12HuntCopySelectedHash();
void DX12HuntResetSelection();
void DX12HuntRefreshOverlay();
