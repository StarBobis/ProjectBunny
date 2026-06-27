#pragma once

#include <Windows.h>
#include <d3d12.h>

#include "DX12BindingTracker.h"

void DX12FrameAnalysisManifestWriteCall(
	const char *functionName, UINT64 eventSerial, UINT64 drawId, UINT64 dispatchId,
	ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState,
	const DX12PsoShaderInfo &shaderInfo, D3D12_PRIMITIVE_TOPOLOGY topology,
	UINT vertexCountPerInstance, UINT indexCountPerInstance, UINT startVertexLocation,
	UINT startIndexLocation, INT baseVertexLocation, UINT instanceCount,
	UINT startInstanceLocation, UINT threadGroupCountX, UINT threadGroupCountY,
	UINT threadGroupCountZ, bool indexBufferValid, D3D12_GPU_VIRTUAL_ADDRESS indexBufferGpuVa,
	UINT indexBufferSize, DXGI_FORMAT indexBufferFormat);

void DX12FrameAnalysisManifestWriteFileDump(
	const wchar_t *filePath, bool isTexture, UINT64 bytes, const char *status,
	const char *note);

void DX12FrameAnalysisManifestWriteIaBinding(
	const DX12FrameIaBufferBinding &buffer, const D3D12_RESOURCE_DESC &desc,
	UINT64 sourceOffset, UINT64 copyBytes, D3D12_RESOURCE_STATES sourceState,
	bool hasCurrentState, const wchar_t *filePath);

void DX12FrameAnalysisManifestWriteResourceBinding(
	const DX12FrameResourceBinding &binding, const D3D12_RESOURCE_DESC &desc,
	UINT64 sourceOffset, UINT64 copyBytes, D3D12_RESOURCE_STATES sourceState,
	bool hasCurrentState, const wchar_t *filePath);
