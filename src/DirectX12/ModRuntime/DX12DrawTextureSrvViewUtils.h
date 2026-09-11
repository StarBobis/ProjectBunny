#pragma once

// DX12DrawTextureSrvViewUtils.h - part of DX12ModRuntimeInternal.h, included from it at the split position.
// Not meant to be included directly.


class DX12DrawTextureSrvViewUtils
{
public:
	static bool CreateSrvHeap(
		ID3D12Device *device,
		ID3D12DescriptorHeap **heap)
	{
		if (!device || !heap)
			return false;
		*heap = nullptr;
		D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
		srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		srvHeapDesc.NumDescriptors = 1;
		srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		HRESULT hr = device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(heap));
		return SUCCEEDED(hr) && *heap;
	}

	static UINT64 HashSrvDesc(const D3D12_SHADER_RESOURCE_VIEW_DESC &desc)
	{
		const unsigned char *bytes =
			reinterpret_cast<const unsigned char*>(&desc);
		UINT64 hash = 1469598103934665603ull;
		for (size_t index = 0; index < sizeof(desc); ++index) {
			hash ^= static_cast<UINT64>(bytes[index]);
			hash *= 1099511628211ull;
		}
		return hash ? hash : 1;
	}

	static UINT ClampMipLevels(UINT requested, UINT mostDetailedMip, UINT mipLevels)
	{
		if (!mipLevels || mostDetailedMip >= mipLevels)
			return 1;
		const UINT available = mipLevels - mostDetailedMip;
		if (!requested || requested == UINT_MAX)
			return available;
		return (std::min)(requested, available);
	}

	static D3D12_SHADER_RESOURCE_VIEW_DESC BuildFallbackTexture2DSrvDesc(
		DX12LoadedResource *resource,
		DXGI_FORMAT format)
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = format;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = resource ? resource->drawSrvMipLevels : 1;
		srvDesc.Texture2D.PlaneSlice = 0;
		srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
		return srvDesc;
	}

	static DXGI_FORMAT InferViewFormatFromDescriptor(
		const DX12DescriptorSummary &descriptor)
	{
		const DXGI_FORMAT viewFormat =
			static_cast<DXGI_FORMAT>(descriptor.viewFormat);
		if (viewFormat != DXGI_FORMAT_UNKNOWN)
			return viewFormat;
		if (descriptor.hasResourceDesc &&
		    descriptor.resourceDesc.Format != DXGI_FORMAT_UNKNOWN)
			return descriptor.resourceDesc.Format;
		return DXGI_FORMAT_UNKNOWN;
	}

	static D3D12_SHADER_RESOURCE_VIEW_DESC BuildSrvDescFromResourceDesc(
		DX12LoadedResource *resource,
		const DX12DescriptorSummary &originalDescriptor,
		DXGI_FORMAT srvFormat)
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = srvFormat;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		if (originalDescriptor.hasResourceDesc) {
			switch (originalDescriptor.resourceDesc.Dimension) {
			case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
				srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
				srvDesc.Texture2D.MostDetailedMip = 0;
				srvDesc.Texture2D.MipLevels = ClampMipLevels(
					originalDescriptor.resourceDesc.MipLevels,
					0,
					resource->drawSrvMipLevels);
				srvDesc.Texture2D.PlaneSlice = 0;
				srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
				break;
			case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
				srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
				srvDesc.Texture1D.MostDetailedMip = 0;
				srvDesc.Texture1D.MipLevels = ClampMipLevels(
					originalDescriptor.resourceDesc.MipLevels,
					0,
					resource->drawSrvMipLevels);
				srvDesc.Texture1D.ResourceMinLODClamp = 0.0f;
				break;
			case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
				srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
				srvDesc.Texture3D.MostDetailedMip = 0;
				srvDesc.Texture3D.MipLevels = ClampMipLevels(
					originalDescriptor.resourceDesc.MipLevels,
					0,
					resource->drawSrvMipLevels);
				break;
			default:
				return BuildFallbackTexture2DSrvDesc(resource, srvFormat);
			}
		} else {
			return BuildFallbackTexture2DSrvDesc(resource, srvFormat);
		}
		return srvDesc;
	}

	static bool BuildViewContractSrvDesc(
		DX12LoadedResource *resource,
		const DX12DescriptorSummary &originalDescriptor,
		D3D12_SHADER_RESOURCE_VIEW_DESC *outDesc,
		bool *fallback)
	{
		if (!resource || !outDesc)
			return false;
		if (fallback)
			*fallback = false;

		const DXGI_FORMAT originalViewFormat =
			InferViewFormatFromDescriptor(originalDescriptor);
		DXGI_FORMAT srvFormat =
			DX12TextureHashUtils::CompatibleReplacementSrvFormat(
				resource->drawSrvFormat, originalViewFormat);
		if (srvFormat == DXGI_FORMAT_UNKNOWN)
			srvFormat = resource->drawSrvFormat;

		const bool hasOriginalSrvDesc =
			originalDescriptor.hasSrvDesc &&
			originalDescriptor.srvDesc.ViewDimension != D3D12_SRV_DIMENSION_UNKNOWN;

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
		if (hasOriginalSrvDesc) {
			srvDesc = originalDescriptor.srvDesc;
			// Validate ViewDimension against the replacement resource type.
			// A BUFFER ViewDimension on a TEXTURE2D resource causes
			// CreateShaderResourceView to fail / hang the driver.
			const bool viewDimMatchesResource =
				(srvDesc.ViewDimension == D3D12_SRV_DIMENSION_TEXTURE2D) ||
				(srvDesc.ViewDimension == D3D12_SRV_DIMENSION_TEXTURE1D) ||
				(srvDesc.ViewDimension == D3D12_SRV_DIMENSION_TEXTURE3D) ||
				(srvDesc.ViewDimension == D3D12_SRV_DIMENSION_TEXTURE2DARRAY) ||
				(srvDesc.ViewDimension == D3D12_SRV_DIMENSION_TEXTURECUBE);
			if (!viewDimMatchesResource) {
				*outDesc = BuildSrvDescFromResourceDesc(
					resource, originalDescriptor, srvFormat);
				if (fallback)
					*fallback = true;
				return true;
			}
		} else {
			*outDesc = BuildSrvDescFromResourceDesc(
				resource, originalDescriptor, srvFormat);
			if (fallback)
				*fallback = true;
			return true;
		}

		srvDesc.Format = srvFormat;
		// Always use default component mapping for the replacement SRV.
		// This matches DX11 behavior: DirectX::CreateDDSTextureFromFileEx
		// creates SRVs with D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING.
		// Preserving the original's non-default mapping (e.g. R=0,G=G,B=0,A=A)
		// would make the replacement texture appear broken/unreadable.
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Texture2D.MostDetailedMip =
			(std::min)(srvDesc.Texture2D.MostDetailedMip,
				resource->drawSrvMipLevels ? resource->drawSrvMipLevels - 1 : 0);
		srvDesc.Texture2D.MipLevels = ClampMipLevels(
			srvDesc.Texture2D.MipLevels,
			srvDesc.Texture2D.MostDetailedMip,
			resource->drawSrvMipLevels);
		srvDesc.Texture2D.PlaneSlice = 0;
		*outDesc = srvDesc;
		return true;
	}

	static void CreateTexture2DSrv(
		ID3D12Device *device,
		DX12LoadedResource *resource,
		DXGI_FORMAT format,
		D3D12_CPU_DESCRIPTOR_HANDLE cpu)
	{
		if (!device || !resource || !resource->drawSrvTextureResource || !cpu.ptr)
			return;
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = format;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = resource->drawSrvMipLevels;
		srvDesc.Texture2D.PlaneSlice = 0;
		srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
		device->CreateShaderResourceView(
			resource->drawSrvTextureResource, &srvDesc, cpu);
	}

	static void CreateSrvFromDesc(
		ID3D12Device *device,
		DX12LoadedResource *resource,
		const D3D12_SHADER_RESOURCE_VIEW_DESC &srvDesc,
		D3D12_CPU_DESCRIPTOR_HANDLE cpu)
	{
		if (!device || !resource || !resource->drawSrvTextureResource || !cpu.ptr)
			return;
		device->CreateShaderResourceView(
			resource->drawSrvTextureResource, &srvDesc, cpu);
	}

	static D3D12_CPU_DESCRIPTOR_HANDLE EnsureCompatibleSrv(
		ID3D12Device *device,
		DX12LoadedResource *resource,
		const DX12DescriptorSummary &originalDescriptor,
		DXGI_FORMAT *createdFormat)
	{
		D3D12_CPU_DESCRIPTOR_HANDLE empty = {};
		if (!device || !resource || !resource->drawSrvTextureResource)
			return empty;
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		bool fallback = false;
		if (!BuildViewContractSrvDesc(resource, originalDescriptor, &srvDesc, &fallback))
			return resource->drawSrvCpu;
		if (createdFormat)
			*createdFormat = srvDesc.Format;
		if (!fallback &&
		    srvDesc.Format == resource->drawSrvFormat &&
		    srvDesc.ViewDimension == D3D12_SRV_DIMENSION_TEXTURE2D &&
		    srvDesc.Shader4ComponentMapping == D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING &&
		    srvDesc.Texture2D.MostDetailedMip == 0 &&
		    srvDesc.Texture2D.MipLevels == resource->drawSrvMipLevels &&
		    srvDesc.Texture2D.PlaneSlice == 0 &&
		    resource->drawSrvCpu.ptr)
			return resource->drawSrvCpu;
		const UINT64 key = HashSrvDesc(srvDesc);
		std::map<UINT64, D3D12_CPU_DESCRIPTOR_HANDLE>::iterator cpuIt =
			resource->drawSrvCompatibleCpus.find(key);
		if (cpuIt != resource->drawSrvCompatibleCpus.end())
			return cpuIt->second;
		ID3D12DescriptorHeap *heap = nullptr;
		if (!CreateSrvHeap(device, &heap))
			return resource->drawSrvCpu;
		D3D12_CPU_DESCRIPTOR_HANDLE cpu = heap->GetCPUDescriptorHandleForHeapStart();
		CreateSrvFromDesc(device, resource, srvDesc, cpu);
		resource->drawSrvCompatibleHeaps[key] = heap;
		resource->drawSrvCompatibleCpus[key] = cpu;
		DX12_LOG("DX12DrawSrvReplacement",
			"stage=view_contract_srv_created,resource=%S,resourceFormat=%u,originalViewFormat=%u,srvFormat=%u,viewDimension=%u,mipStart=%u,mips=%u,componentMapping=%u,fallback=%s,cpu=0x%zx",
			resource->name.c_str(),
			static_cast<UINT>(resource->drawSrvFormat),
			originalDescriptor.viewFormat,
			static_cast<UINT>(srvDesc.Format),
			static_cast<UINT>(srvDesc.ViewDimension),
			srvDesc.Texture2D.MostDetailedMip,
			srvDesc.Texture2D.MipLevels,
			srvDesc.Shader4ComponentMapping,
			fallback ? "true" : "false",
			cpu.ptr);
		return cpu;
	}
};
