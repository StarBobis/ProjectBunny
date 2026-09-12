#pragma once

// Resident loaded-resource record and the map that owns it.
// Split out of ModRuntime/DX12ModRuntimeInternal.h, where it used to sit in the
// middle of a file that also defined everything before it. It lives under
// Core/ModState because it is plain shared data (D3D12 handles plus bookkeeping
// fields, no behaviour), and everything that needs it can now include just this
// header instead of the whole mod-runtime internal header.
//
// Include Core/DX12ModState.h (or its umbrella Core/DX12Core.h) from existing
// code.
#include "../DX12CommandFlow.h"

#include <map>
#include <string>
#include <unordered_map>

class DX12LoadedResource
{
public:
	ID3D12Resource *resource = nullptr;
	ID3D12Resource *uavResource = nullptr;
	ID3D12Resource *srvResource = nullptr;
	ID3D12Resource *drawSrvTextureResource = nullptr;
	ID3D12Resource *drawSrvUploadResource = nullptr;
	ID3D12DescriptorHeap *uavHeap = nullptr;
	ID3D12DescriptorHeap *srvHeap = nullptr;
	ID3D12DescriptorHeap *drawSrvHeap = nullptr;
	std::map<UINT64, ID3D12DescriptorHeap*> drawSrvCompatibleHeaps;
	std::map<UINT64, D3D12_CPU_DESCRIPTOR_HANDLE> drawSrvCompatibleCpus;
	bool failed = false;
	UINT64 byteWidth = 0;
	UINT stride = 0;
	DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
	D3D12_CPU_DESCRIPTOR_HANDLE uavCpu = {};
	D3D12_CPU_DESCRIPTOR_HANDLE srvCpu = {};
	D3D12_CPU_DESCRIPTOR_HANDLE drawSrvCpu = {};
	bool uavValid = false;
	// UAV validity is frame-local. Store its frame epoch instead of walking the
	// complete resident-resource map at every Present just to clear two booleans.
	LONG uavValidPresent = -1;
	bool uavBarrierPending = false;
	UINT64 uavByteWidth = 0;
	UINT uavStride = 0;
	UINT64 srvByteWidth = 0;
	UINT srvStride = 0;
	DXGI_FORMAT drawSrvFormat = DXGI_FORMAT_UNKNOWN;
	UINT drawSrvWidth = 0;
	UINT drawSrvHeight = 0;
	UINT drawSrvMipLevels = 0;
	D3D12_RESOURCE_STATES uavState = D3D12_RESOURCE_STATE_COPY_DEST;
	std::wstring name;
	std::wstring path;
};


inline std::unordered_map<std::wstring, DX12LoadedResource> gLoadedResources;


// Release every D3D12 object a record owns and reset it to a default record.
// Declared next to the type it operates on so any header that holds a
// DX12LoadedResource and needs to drop its contents can say so with an include,
// rather than depending on where it happens to be spliced into a larger header.
void ReleaseLoadedResourceObjects(DX12LoadedResource *resource);


// Get (or create) the resident record for a [ResourceX] section, loading it on
// first use. Defined in ModRuntime/DX12LoadedResources.cpp.
DX12LoadedResource *EnsureLoadedResourceForPreSkin(
	ID3D12Device *device, const std::wstring &name);
