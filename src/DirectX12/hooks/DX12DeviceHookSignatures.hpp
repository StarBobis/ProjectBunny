#pragma once

#include <Windows.h>
#include <d3d12.h>

class DX12DeviceHookSignatures
{
public:
	using CreateGraphicsPipelineState = HRESULT(STDMETHODCALLTYPE *)(
		ID3D12Device*, const D3D12_GRAPHICS_PIPELINE_STATE_DESC*, REFIID, void**);
	using CreateComputePipelineState = HRESULT(STDMETHODCALLTYPE *)(
		ID3D12Device*, const D3D12_COMPUTE_PIPELINE_STATE_DESC*, REFIID, void**);
	using CreatePipelineState = HRESULT(STDMETHODCALLTYPE *)(
		ID3D12Device2*, const D3D12_PIPELINE_STATE_STREAM_DESC*, REFIID, void**);
	using DeviceFactoryCreateDevice = HRESULT(STDMETHODCALLTYPE *)(
		ID3D12DeviceFactory*, IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
};
