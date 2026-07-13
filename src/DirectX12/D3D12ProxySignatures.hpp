#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>

class D3D12ProxySignatures
{
public:
	using CreateDevice = HRESULT(WINAPI *)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
	using GetDebugInterface = HRESULT(WINAPI *)(REFIID, void**);
	using SerializeRootSignature = HRESULT(WINAPI *)(
		const D3D12_ROOT_SIGNATURE_DESC*, D3D_ROOT_SIGNATURE_VERSION, ID3DBlob**, ID3DBlob**);
	using CreateRootSignatureDeserializer = HRESULT(WINAPI *)(LPCVOID, SIZE_T, REFIID, void**);
	using SerializeVersionedRootSignature = HRESULT(WINAPI *)(
		const D3D12_VERSIONED_ROOT_SIGNATURE_DESC*, ID3DBlob**, ID3DBlob**);
	using CreateVersionedRootSignatureDeserializer = HRESULT(WINAPI *)(LPCVOID, SIZE_T, REFIID, void**);
	using EnableExperimentalFeatures = HRESULT(WINAPI *)(UINT, const IID*, void*, UINT*);
	using GetInterface = HRESULT(WINAPI *)(REFCLSID, REFIID, void**);
	using Generic = UINT_PTR(WINAPI *)(
		UINT_PTR, UINT_PTR, UINT_PTR, UINT_PTR,
		UINT_PTR, UINT_PTR, UINT_PTR, UINT_PTR);
};
