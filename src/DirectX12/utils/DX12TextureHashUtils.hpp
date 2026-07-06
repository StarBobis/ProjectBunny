#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <d3d11.h>

#include <stdint.h>

#include "DX12ResourceTracker.hpp"
#include "crc32c.h"
#include "types/DX12ResourceTypes.hpp"

class DX12TextureHashUtils
{
public:
	static UINT64 TextureCopyableBytes(ID3D12Device *device, const D3D12_RESOURCE_DESC &desc)
	{
		if (!device)
			return 0;
		if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE1D &&
		    desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D &&
		    desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE3D)
			return 0;
		if (desc.SampleDesc.Count > 1)
			return 0;
		UINT subresources = static_cast<UINT>(desc.MipLevels ? desc.MipLevels : 1);
		if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE3D)
			subresources *= static_cast<UINT>(desc.DepthOrArraySize ? desc.DepthOrArraySize : 1);
		if (!subresources)
			return 0;
		D3D12_FEATURE_DATA_FORMAT_INFO formatInfo = {};
		formatInfo.Format = desc.Format;
		__try {
			if (SUCCEEDED(device->CheckFeatureSupport(
					D3D12_FEATURE_FORMAT_INFO, &formatInfo, sizeof(formatInfo))) &&
			    formatInfo.PlaneCount > 1) {
				const UINT64 planeSubresources =
					static_cast<UINT64>(subresources) * formatInfo.PlaneCount;
				if (planeSubresources == 0 || planeSubresources > static_cast<UINT64>(UINT_MAX))
					return 0;
				subresources = static_cast<UINT>(planeSubresources);
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return 0;
		}
		UINT64 copyBytes = 0;
		__try {
			device->GetCopyableFootprints(
				&desc, 0, subresources, 0, nullptr, nullptr, nullptr, &copyBytes);
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return 0;
		}
		return copyBytes == UINT64_MAX ? 0 : copyBytes;
	}

	static UINT64 DescriptorTextureHashFallbackSize(
		ID3D12Device *device,
		const DX12DescriptorSummary &descriptor)
	{
		if (!descriptor.hasResourceDesc)
			return descriptor.viewSize;
		const D3D12_RESOURCE_DESC &desc = descriptor.resourceDesc;
		if (desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D ||
		    desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
		    desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D) {
			const UINT64 copyBytes = TextureCopyableBytes(device, desc);
			if (copyBytes)
				return copyBytes;
		}
		return descriptor.viewSize;
	}

	static uint32_t DescriptorHuntHash(
		ID3D12Device *device,
		const DX12CurrentShaderResourceBinding &binding)
	{
		const DX12DescriptorSummary &descriptor = binding.descriptor;
		const UINT64 fallbackGpuVa = descriptor.gpuVirtualAddress ?
			descriptor.gpuVirtualAddress : binding.gpuVirtualAddress;
		const UINT64 fallbackSize =
			DescriptorTextureHashFallbackSize(device, descriptor);
		return DX12HashDescriptorBufferView(
			binding.hasDescriptor ? &descriptor : nullptr,
			fallbackGpuVa,
			fallbackSize);
	}

	static bool FillD3D11Texture2DDesc(
		const D3D12_RESOURCE_DESC &desc,
		D3D11_TEXTURE2D_DESC *outDesc)
	{
		if (!outDesc)
			return false;
		if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D)
			return false;
		D3D11_TEXTURE2D_DESC dx11Desc = {};
		dx11Desc.Width = static_cast<UINT>(desc.Width);
		dx11Desc.Height = desc.Height;
		dx11Desc.MipLevels = desc.MipLevels;
		dx11Desc.ArraySize = desc.DepthOrArraySize;
		dx11Desc.Format = desc.Format;
		dx11Desc.SampleDesc = desc.SampleDesc;
		dx11Desc.Usage = D3D11_USAGE_DEFAULT;
		dx11Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		if ((desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) != 0)
			dx11Desc.BindFlags |= D3D11_BIND_RENDER_TARGET;
		if ((desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) != 0)
			dx11Desc.BindFlags |= D3D11_BIND_DEPTH_STENCIL;
		if ((desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0)
			dx11Desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
		dx11Desc.CPUAccessFlags = 0;
		dx11Desc.MiscFlags = 0;
		*outDesc = dx11Desc;
		return true;
	}

	static uint32_t Dx11StyleTexture2DDescHash(
		const D3D12_RESOURCE_DESC &desc,
		uint32_t initialHash)
	{
		D3D11_TEXTURE2D_DESC dx11Desc = {};
		if (!FillD3D11Texture2DDesc(desc, &dx11Desc))
			return 0;
		return crc32c_append(
			initialHash,
			reinterpret_cast<const uint8_t*>(&dx11Desc),
			sizeof(dx11Desc));
	}

	static bool IsSrgbFormat(DXGI_FORMAT format)
	{
		switch (format) {
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
		case DXGI_FORMAT_BC1_UNORM_SRGB:
		case DXGI_FORMAT_BC2_UNORM_SRGB:
		case DXGI_FORMAT_BC3_UNORM_SRGB:
		case DXGI_FORMAT_BC7_UNORM_SRGB:
			return true;
		default:
			return false;
		}
	}

	static DXGI_FORMAT MakeSrgbFormat(DXGI_FORMAT format)
	{
		switch (format) {
		case DXGI_FORMAT_R8G8B8A8_UNORM:
		case DXGI_FORMAT_R8G8B8A8_TYPELESS:
			return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		case DXGI_FORMAT_B8G8R8A8_UNORM:
		case DXGI_FORMAT_B8G8R8A8_TYPELESS:
			return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
		case DXGI_FORMAT_B8G8R8X8_UNORM:
		case DXGI_FORMAT_B8G8R8X8_TYPELESS:
			return DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
		case DXGI_FORMAT_BC1_UNORM:
		case DXGI_FORMAT_BC1_TYPELESS:
			return DXGI_FORMAT_BC1_UNORM_SRGB;
		case DXGI_FORMAT_BC2_UNORM:
		case DXGI_FORMAT_BC2_TYPELESS:
			return DXGI_FORMAT_BC2_UNORM_SRGB;
		case DXGI_FORMAT_BC3_UNORM:
		case DXGI_FORMAT_BC3_TYPELESS:
			return DXGI_FORMAT_BC3_UNORM_SRGB;
		case DXGI_FORMAT_BC7_UNORM:
		case DXGI_FORMAT_BC7_TYPELESS:
			return DXGI_FORMAT_BC7_UNORM_SRGB;
		default:
			return format;
		}
	}

	static DXGI_FORMAT MakeUnormFormat(DXGI_FORMAT format)
	{
		switch (format) {
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		case DXGI_FORMAT_R8G8B8A8_TYPELESS:
			return DXGI_FORMAT_R8G8B8A8_UNORM;
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		case DXGI_FORMAT_B8G8R8A8_TYPELESS:
			return DXGI_FORMAT_B8G8R8A8_UNORM;
		case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
		case DXGI_FORMAT_B8G8R8X8_TYPELESS:
			return DXGI_FORMAT_B8G8R8X8_UNORM;
		case DXGI_FORMAT_BC1_UNORM_SRGB:
		case DXGI_FORMAT_BC1_TYPELESS:
			return DXGI_FORMAT_BC1_UNORM;
		case DXGI_FORMAT_BC2_UNORM_SRGB:
		case DXGI_FORMAT_BC2_TYPELESS:
			return DXGI_FORMAT_BC2_UNORM;
		case DXGI_FORMAT_BC3_UNORM_SRGB:
		case DXGI_FORMAT_BC3_TYPELESS:
			return DXGI_FORMAT_BC3_UNORM;
		case DXGI_FORMAT_BC7_UNORM_SRGB:
		case DXGI_FORMAT_BC7_TYPELESS:
			return DXGI_FORMAT_BC7_UNORM;
		default:
			return format;
		}
	}

	static bool FormatsDifferOnlyBySrgb(DXGI_FORMAT lhs, DXGI_FORMAT rhs)
	{
		return MakeUnormFormat(lhs) == MakeUnormFormat(rhs);
	}

	static DXGI_FORMAT CompatibleReplacementSrvFormat(
		DXGI_FORMAT replacementResourceFormat,
		DXGI_FORMAT originalViewFormat)
	{
		if (originalViewFormat == DXGI_FORMAT_UNKNOWN)
			return replacementResourceFormat;
		if (replacementResourceFormat == originalViewFormat)
			return replacementResourceFormat;
		if (FormatsDifferOnlyBySrgb(replacementResourceFormat, originalViewFormat)) {
			return IsSrgbFormat(originalViewFormat) ?
				MakeSrgbFormat(replacementResourceFormat) :
				MakeUnormFormat(replacementResourceFormat);
		}
		return replacementResourceFormat;
	}
};
