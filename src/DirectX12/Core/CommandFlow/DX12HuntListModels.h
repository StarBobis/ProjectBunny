#pragma once

// Hunt selection declarations. Split out of Core/DX12CommandFlow.h; include
// that umbrella header from existing code.
#include "../DX12HotPath.h"

// The hunt categories the keyboard matrix can navigate and copy hashes from.
enum class HuntCopyTarget
{
	None,
	VS,
	PS,
	CS,
	IB,
	VB
};

void DX12HuntResetCommandList(ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState);
void DX12HuntUnregisterCommandList(ID3D12GraphicsCommandList *commandList);
void DX12HuntObservePipelineState(ID3D12PipelineState *pipelineState);
void DX12HuntRecordDraw(
	ID3D12GraphicsCommandList *commandList, bool indexed,
	UINT vertexCount, UINT indexCount, UINT firstVertex, UINT firstIndex,
	INT baseVertex);
void DX12HuntPublishCommandList(ID3D12GraphicsCommandList *commandList);
void DX12HuntRecordDispatch(ID3D12GraphicsCommandList *commandList);
void DX12HuntRecordExecuteIndirect(ID3D12GraphicsCommandList *commandList);
bool DX12HuntShouldSkipDraw(
	ID3D12GraphicsCommandList *commandList, bool indexed,
	UINT vertexCount, UINT indexCount, UINT firstVertex, UINT firstIndex,
	INT baseVertex);
bool DX12HuntShouldSkipDispatch(ID3D12GraphicsCommandList *commandList);
bool DX12HuntIsEnabled();
bool DX12HuntNeedsHotPath();
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
void DX12HuntCopySelectedHash(HuntCopyTarget target);
void DX12HuntResetSelection();
// Composes the DX11-style green hunting text (counts, selected hashes,
// per-frame counters) drawn inside the game window while hunting is on.
void DX12HuntComposeOverlayText(wchar_t *buffer, size_t bufferCount);
