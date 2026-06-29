#pragma once

#include <d3d12.h>

class DX12IaCommandSignatures
{
public:
	using DrawInstanced = void(STDMETHODCALLTYPE *)(
		ID3D12GraphicsCommandList*, UINT, UINT, UINT, UINT);
	using DrawIndexedInstanced = void(STDMETHODCALLTYPE *)(
		ID3D12GraphicsCommandList*, UINT, UINT, UINT, INT, UINT);
	using Dispatch = void(STDMETHODCALLTYPE *)(
		ID3D12GraphicsCommandList*, UINT, UINT, UINT);
	using SetIndexBuffer = void(STDMETHODCALLTYPE *)(
		ID3D12GraphicsCommandList*, const D3D12_INDEX_BUFFER_VIEW*);
	using SetVertexBuffers = void(STDMETHODCALLTYPE *)(
		ID3D12GraphicsCommandList*, UINT, UINT, const D3D12_VERTEX_BUFFER_VIEW*);
};
