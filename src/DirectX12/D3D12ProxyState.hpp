#pragma once

#include <Windows.h>

#include "D3D12ProxySignatures.hpp"

class D3D12ProxyState
{
public:
	inline static HMODULE RealD3D12 = nullptr;
	inline static D3D12ProxySignatures::CreateDevice OrigCreateDevice = nullptr;
	inline static D3D12ProxySignatures::GetDebugInterface OrigGetDebugInterface = nullptr;
	inline static D3D12ProxySignatures::SerializeRootSignature OrigSerializeRootSignature = nullptr;
	inline static D3D12ProxySignatures::CreateRootSignatureDeserializer OrigCreateRootSignatureDeserializer = nullptr;
	inline static D3D12ProxySignatures::SerializeVersionedRootSignature OrigSerializeVersionedRootSignature = nullptr;
	inline static D3D12ProxySignatures::CreateVersionedRootSignatureDeserializer OrigCreateVersionedRootSignatureDeserializer = nullptr;
	inline static D3D12ProxySignatures::EnableExperimentalFeatures OrigEnableExperimentalFeatures = nullptr;
	inline static D3D12ProxySignatures::GetInterface OrigGetInterface = nullptr;
};
