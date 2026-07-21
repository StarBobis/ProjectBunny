#pragma once

#include "Resources/DX12ResourceFormats.hpp"

class DX12TextureHashUtils
{
public:
	static bool IsBlockCompressedFormat(DXGI_FORMAT format)
	{
		switch (format) {
		case DXGI_FORMAT_BC1_TYPELESS:
		case DXGI_FORMAT_BC1_UNORM:
		case DXGI_FORMAT_BC1_UNORM_SRGB:
		case DXGI_FORMAT_BC2_TYPELESS:
		case DXGI_FORMAT_BC2_UNORM:
		case DXGI_FORMAT_BC2_UNORM_SRGB:
		case DXGI_FORMAT_BC3_TYPELESS:
		case DXGI_FORMAT_BC3_UNORM:
		case DXGI_FORMAT_BC3_UNORM_SRGB:
		case DXGI_FORMAT_BC4_TYPELESS:
		case DXGI_FORMAT_BC4_UNORM:
		case DXGI_FORMAT_BC4_SNORM:
		case DXGI_FORMAT_BC5_TYPELESS:
		case DXGI_FORMAT_BC5_UNORM:
		case DXGI_FORMAT_BC5_SNORM:
		case DXGI_FORMAT_BC6H_TYPELESS:
		case DXGI_FORMAT_BC6H_UF16:
		case DXGI_FORMAT_BC6H_SF16:
		case DXGI_FORMAT_BC7_TYPELESS:
		case DXGI_FORMAT_BC7_UNORM:
		case DXGI_FORMAT_BC7_UNORM_SRGB:
			return true;
		default:
			return false;
		}
	}

	static uint32_t BitsPerPixel(DXGI_FORMAT format)
	{
		switch (format) {
		case DXGI_FORMAT_R32G32B32A32_TYPELESS:
		case DXGI_FORMAT_R32G32B32A32_FLOAT:
		case DXGI_FORMAT_R32G32B32A32_UINT:
		case DXGI_FORMAT_R32G32B32A32_SINT:
			return 128;
		case DXGI_FORMAT_R32G32B32_TYPELESS:
		case DXGI_FORMAT_R32G32B32_FLOAT:
		case DXGI_FORMAT_R32G32B32_UINT:
		case DXGI_FORMAT_R32G32B32_SINT:
			return 96;
		case DXGI_FORMAT_R16G16B16A16_TYPELESS:
		case DXGI_FORMAT_R16G16B16A16_FLOAT:
		case DXGI_FORMAT_R16G16B16A16_UNORM:
		case DXGI_FORMAT_R16G16B16A16_UINT:
		case DXGI_FORMAT_R16G16B16A16_SNORM:
		case DXGI_FORMAT_R16G16B16A16_SINT:
		case DXGI_FORMAT_R32G32_TYPELESS:
		case DXGI_FORMAT_R32G32_FLOAT:
		case DXGI_FORMAT_R32G32_UINT:
		case DXGI_FORMAT_R32G32_SINT:
		case DXGI_FORMAT_R32G8X24_TYPELESS:
		case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
		case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
		case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
		case DXGI_FORMAT_Y416:
		case DXGI_FORMAT_Y210:
		case DXGI_FORMAT_Y216:
			return 64;
		case DXGI_FORMAT_R10G10B10A2_TYPELESS:
		case DXGI_FORMAT_R10G10B10A2_UNORM:
		case DXGI_FORMAT_R10G10B10A2_UINT:
		case DXGI_FORMAT_R11G11B10_FLOAT:
		case DXGI_FORMAT_R8G8B8A8_TYPELESS:
		case DXGI_FORMAT_R8G8B8A8_UNORM:
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		case DXGI_FORMAT_R8G8B8A8_UINT:
		case DXGI_FORMAT_R8G8B8A8_SNORM:
		case DXGI_FORMAT_R8G8B8A8_SINT:
		case DXGI_FORMAT_R16G16_TYPELESS:
		case DXGI_FORMAT_R16G16_FLOAT:
		case DXGI_FORMAT_R16G16_UNORM:
		case DXGI_FORMAT_R16G16_UINT:
		case DXGI_FORMAT_R16G16_SNORM:
		case DXGI_FORMAT_R16G16_SINT:
		case DXGI_FORMAT_R32_TYPELESS:
		case DXGI_FORMAT_D32_FLOAT:
		case DXGI_FORMAT_R32_FLOAT:
		case DXGI_FORMAT_R32_UINT:
		case DXGI_FORMAT_R32_SINT:
		case DXGI_FORMAT_R24G8_TYPELESS:
		case DXGI_FORMAT_D24_UNORM_S8_UINT:
		case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
		case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
		case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
		case DXGI_FORMAT_R8G8_B8G8_UNORM:
		case DXGI_FORMAT_G8R8_G8B8_UNORM:
		case DXGI_FORMAT_B8G8R8A8_UNORM:
		case DXGI_FORMAT_B8G8R8X8_UNORM:
		case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
		case DXGI_FORMAT_B8G8R8A8_TYPELESS:
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		case DXGI_FORMAT_B8G8R8X8_TYPELESS:
		case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
		case DXGI_FORMAT_AYUV:
		case DXGI_FORMAT_Y410:
		case DXGI_FORMAT_YUY2:
			return 32;
		case DXGI_FORMAT_P010:
		case DXGI_FORMAT_P016:
			return 24;
		case DXGI_FORMAT_R8G8_TYPELESS:
		case DXGI_FORMAT_R8G8_UNORM:
		case DXGI_FORMAT_R8G8_UINT:
		case DXGI_FORMAT_R8G8_SNORM:
		case DXGI_FORMAT_R8G8_SINT:
		case DXGI_FORMAT_R16_TYPELESS:
		case DXGI_FORMAT_R16_FLOAT:
		case DXGI_FORMAT_D16_UNORM:
		case DXGI_FORMAT_R16_UNORM:
		case DXGI_FORMAT_R16_UINT:
		case DXGI_FORMAT_R16_SNORM:
		case DXGI_FORMAT_R16_SINT:
		case DXGI_FORMAT_B5G6R5_UNORM:
		case DXGI_FORMAT_B5G5R5A1_UNORM:
		case DXGI_FORMAT_B4G4R4A4_UNORM:
			return 16;
		case DXGI_FORMAT_NV12:
		case DXGI_FORMAT_420_OPAQUE:
		case DXGI_FORMAT_NV11:
			return 12;
		case DXGI_FORMAT_R8_TYPELESS:
		case DXGI_FORMAT_R8_UNORM:
		case DXGI_FORMAT_R8_UINT:
		case DXGI_FORMAT_R8_SNORM:
		case DXGI_FORMAT_R8_SINT:
		case DXGI_FORMAT_A8_UNORM:
		case DXGI_FORMAT_AI44:
		case DXGI_FORMAT_IA44:
		case DXGI_FORMAT_P8:
			return 8;
		case DXGI_FORMAT_R1_UNORM:
			return 1;
		case DXGI_FORMAT_BC1_TYPELESS:
		case DXGI_FORMAT_BC1_UNORM:
		case DXGI_FORMAT_BC1_UNORM_SRGB:
		case DXGI_FORMAT_BC4_TYPELESS:
		case DXGI_FORMAT_BC4_UNORM:
		case DXGI_FORMAT_BC4_SNORM:
			return 4;
		case DXGI_FORMAT_BC2_TYPELESS:
		case DXGI_FORMAT_BC2_UNORM:
		case DXGI_FORMAT_BC2_UNORM_SRGB:
		case DXGI_FORMAT_BC3_TYPELESS:
		case DXGI_FORMAT_BC3_UNORM:
		case DXGI_FORMAT_BC3_UNORM_SRGB:
		case DXGI_FORMAT_BC5_TYPELESS:
		case DXGI_FORMAT_BC5_UNORM:
		case DXGI_FORMAT_BC5_SNORM:
		case DXGI_FORMAT_BC6H_TYPELESS:
		case DXGI_FORMAT_BC6H_UF16:
		case DXGI_FORMAT_BC6H_SF16:
		case DXGI_FORMAT_BC7_TYPELESS:
		case DXGI_FORMAT_BC7_UNORM:
		case DXGI_FORMAT_BC7_UNORM_SRGB:
			return 8;
		default:
			return 0;
		}
	}

	static UINT Texture2DTopMipRowCount(DXGI_FORMAT format, UINT height)
	{
		if (!height)
			return 0;
		if (IsBlockCompressedFormat(format))
			return (std::max<UINT>)(1, (height + 3) / 4);
		return height;
	}

	static UINT64 Texture2DTopMipRowBytes(DXGI_FORMAT format, UINT width)
	{
		if (!width)
			return 0;
		if (IsBlockCompressedFormat(format)) {
			const uint32_t bpp = BitsPerPixel(format);
			const UINT blockBytes = bpp == 4 ? 8 : 16;
			const UINT blocksWide = (std::max<UINT>)(1, (width + 3) / 4);
			return static_cast<UINT64>(blocksWide) * blockBytes;
		}
		const uint32_t bpp = BitsPerPixel(format);
		if (!bpp)
			return 0;
		return (static_cast<UINT64>(width) * bpp + 7) / 8;
	}

	static bool Texture2DTopMipHashFromRows(
		const D3D12_RESOURCE_DESC &desc,
		const uint8_t *base,
		UINT64 sourceRowPitch,
		UINT64 sourceRowBytes,
		UINT sourceRowCount,
		uint32_t *hashOut)
	{
		if (!hashOut)
			return false;
		*hashOut = 0;
		if (!base || desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D)
			return false;
		if (desc.Width > UINT_MAX)
			return false;
		const UINT width = static_cast<UINT>(desc.Width);
		const UINT rowCount = Texture2DTopMipRowCount(desc.Format, desc.Height);
		const UINT64 rowBytes = Texture2DTopMipRowBytes(desc.Format, width);
		if (!rowCount || !rowBytes)
			return false;
		if (sourceRowCount < rowCount || sourceRowBytes < rowBytes || sourceRowPitch < rowBytes)
			return false;

		uint32_t hash = 0;
		for (UINT row = 0; row < rowCount; ++row) {
			const uint8_t *src = base + static_cast<size_t>(row) * sourceRowPitch;
			hash = crc32c_append(hash, src, static_cast<size_t>(rowBytes));
		}
		hash = Dx11StyleTexture2DDescHash(desc, hash);
		*hashOut = hash;
		return true;
	}

	static bool Texture2DTopMipHashFromPlacedFootprint(
		const D3D12_RESOURCE_DESC &desc,
		const D3D12_PLACED_SUBRESOURCE_FOOTPRINT &layout,
		UINT rowCount,
		UINT64 rowBytes,
		const void *mappedData,
		uint32_t *hashOut)
	{
		if (!mappedData)
			return false;
		const uint8_t *base =
			static_cast<const uint8_t*>(mappedData) + layout.Offset;
		return Texture2DTopMipHashFromRows(
			desc, base, layout.Footprint.RowPitch, rowBytes, rowCount, hashOut);
	}

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




class DX12ResourceDumpFileWriter
{
public:
static void WriteU32(FILE *file, uint32_t value)
{
	fwrite(&value, sizeof(value), 1, file);
}

static bool IsBlockCompressedFormat(DXGI_FORMAT format)
{
	switch (format) {
	case DXGI_FORMAT_BC1_TYPELESS:
	case DXGI_FORMAT_BC1_UNORM:
	case DXGI_FORMAT_BC1_UNORM_SRGB:
	case DXGI_FORMAT_BC2_TYPELESS:
	case DXGI_FORMAT_BC2_UNORM:
	case DXGI_FORMAT_BC2_UNORM_SRGB:
	case DXGI_FORMAT_BC3_TYPELESS:
	case DXGI_FORMAT_BC3_UNORM:
	case DXGI_FORMAT_BC3_UNORM_SRGB:
	case DXGI_FORMAT_BC4_TYPELESS:
	case DXGI_FORMAT_BC4_UNORM:
	case DXGI_FORMAT_BC4_SNORM:
	case DXGI_FORMAT_BC5_TYPELESS:
	case DXGI_FORMAT_BC5_UNORM:
	case DXGI_FORMAT_BC5_SNORM:
	case DXGI_FORMAT_BC6H_TYPELESS:
	case DXGI_FORMAT_BC6H_UF16:
	case DXGI_FORMAT_BC6H_SF16:
	case DXGI_FORMAT_BC7_TYPELESS:
	case DXGI_FORMAT_BC7_UNORM:
	case DXGI_FORMAT_BC7_UNORM_SRGB:
		return true;
	default:
		return false;
	}
}

static uint32_t BitsPerPixel(DXGI_FORMAT format)
{
	switch (format) {
	case DXGI_FORMAT_R32G32B32A32_TYPELESS:
	case DXGI_FORMAT_R32G32B32A32_FLOAT:
	case DXGI_FORMAT_R32G32B32A32_UINT:
	case DXGI_FORMAT_R32G32B32A32_SINT:
		return 128;
	case DXGI_FORMAT_R32G32B32_TYPELESS:
	case DXGI_FORMAT_R32G32B32_FLOAT:
	case DXGI_FORMAT_R32G32B32_UINT:
	case DXGI_FORMAT_R32G32B32_SINT:
		return 96;
	case DXGI_FORMAT_R16G16B16A16_TYPELESS:
	case DXGI_FORMAT_R16G16B16A16_FLOAT:
	case DXGI_FORMAT_R16G16B16A16_UNORM:
	case DXGI_FORMAT_R16G16B16A16_UINT:
	case DXGI_FORMAT_R16G16B16A16_SNORM:
	case DXGI_FORMAT_R16G16B16A16_SINT:
	case DXGI_FORMAT_R32G32_TYPELESS:
	case DXGI_FORMAT_R32G32_FLOAT:
	case DXGI_FORMAT_R32G32_UINT:
	case DXGI_FORMAT_R32G32_SINT:
	case DXGI_FORMAT_R32G8X24_TYPELESS:
	case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
	case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
	case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
	case DXGI_FORMAT_Y416:
	case DXGI_FORMAT_Y210:
	case DXGI_FORMAT_Y216:
		return 64;
	case DXGI_FORMAT_R10G10B10A2_TYPELESS:
	case DXGI_FORMAT_R10G10B10A2_UNORM:
	case DXGI_FORMAT_R10G10B10A2_UINT:
	case DXGI_FORMAT_R11G11B10_FLOAT:
	case DXGI_FORMAT_R8G8B8A8_TYPELESS:
	case DXGI_FORMAT_R8G8B8A8_UNORM:
	case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
	case DXGI_FORMAT_R8G8B8A8_UINT:
	case DXGI_FORMAT_R8G8B8A8_SNORM:
	case DXGI_FORMAT_R8G8B8A8_SINT:
	case DXGI_FORMAT_R16G16_TYPELESS:
	case DXGI_FORMAT_R16G16_FLOAT:
	case DXGI_FORMAT_R16G16_UNORM:
	case DXGI_FORMAT_R16G16_UINT:
	case DXGI_FORMAT_R16G16_SNORM:
	case DXGI_FORMAT_R16G16_SINT:
	case DXGI_FORMAT_R32_TYPELESS:
	case DXGI_FORMAT_D32_FLOAT:
	case DXGI_FORMAT_R32_FLOAT:
	case DXGI_FORMAT_R32_UINT:
	case DXGI_FORMAT_R32_SINT:
	case DXGI_FORMAT_R24G8_TYPELESS:
	case DXGI_FORMAT_D24_UNORM_S8_UINT:
	case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
	case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
	case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
	case DXGI_FORMAT_R8G8_B8G8_UNORM:
	case DXGI_FORMAT_G8R8_G8B8_UNORM:
	case DXGI_FORMAT_B8G8R8A8_UNORM:
	case DXGI_FORMAT_B8G8R8X8_UNORM:
	case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
	case DXGI_FORMAT_B8G8R8A8_TYPELESS:
	case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
	case DXGI_FORMAT_B8G8R8X8_TYPELESS:
	case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
	case DXGI_FORMAT_AYUV:
	case DXGI_FORMAT_Y410:
	case DXGI_FORMAT_YUY2:
		return 32;
	case DXGI_FORMAT_P010:
	case DXGI_FORMAT_P016:
		return 24;
	case DXGI_FORMAT_R8G8_TYPELESS:
	case DXGI_FORMAT_R8G8_UNORM:
	case DXGI_FORMAT_R8G8_UINT:
	case DXGI_FORMAT_R8G8_SNORM:
	case DXGI_FORMAT_R8G8_SINT:
	case DXGI_FORMAT_R16_TYPELESS:
	case DXGI_FORMAT_R16_FLOAT:
	case DXGI_FORMAT_D16_UNORM:
	case DXGI_FORMAT_R16_UNORM:
	case DXGI_FORMAT_R16_UINT:
	case DXGI_FORMAT_R16_SNORM:
	case DXGI_FORMAT_R16_SINT:
	case DXGI_FORMAT_B5G6R5_UNORM:
	case DXGI_FORMAT_B5G5R5A1_UNORM:
	case DXGI_FORMAT_B4G4R4A4_UNORM:
		return 16;
	case DXGI_FORMAT_NV12:
	case DXGI_FORMAT_420_OPAQUE:
	case DXGI_FORMAT_NV11:
		return 12;
	case DXGI_FORMAT_R8_TYPELESS:
	case DXGI_FORMAT_R8_UNORM:
	case DXGI_FORMAT_R8_UINT:
	case DXGI_FORMAT_R8_SNORM:
	case DXGI_FORMAT_R8_SINT:
	case DXGI_FORMAT_A8_UNORM:
	case DXGI_FORMAT_AI44:
	case DXGI_FORMAT_IA44:
	case DXGI_FORMAT_P8:
		return 8;
	case DXGI_FORMAT_R1_UNORM:
		return 1;
	case DXGI_FORMAT_BC1_TYPELESS:
	case DXGI_FORMAT_BC1_UNORM:
	case DXGI_FORMAT_BC1_UNORM_SRGB:
	case DXGI_FORMAT_BC4_TYPELESS:
	case DXGI_FORMAT_BC4_UNORM:
	case DXGI_FORMAT_BC4_SNORM:
		return 4;
	case DXGI_FORMAT_BC2_TYPELESS:
	case DXGI_FORMAT_BC2_UNORM:
	case DXGI_FORMAT_BC2_UNORM_SRGB:
	case DXGI_FORMAT_BC3_TYPELESS:
	case DXGI_FORMAT_BC3_UNORM:
	case DXGI_FORMAT_BC3_UNORM_SRGB:
	case DXGI_FORMAT_BC5_TYPELESS:
	case DXGI_FORMAT_BC5_UNORM:
	case DXGI_FORMAT_BC5_SNORM:
	case DXGI_FORMAT_BC6H_TYPELESS:
	case DXGI_FORMAT_BC6H_UF16:
	case DXGI_FORMAT_BC6H_SF16:
	case DXGI_FORMAT_BC7_TYPELESS:
	case DXGI_FORMAT_BC7_UNORM:
	case DXGI_FORMAT_BC7_UNORM_SRGB:
		return 8;
	default:
		return 0;
	}
}

static UINT MipSize(UINT64 value, UINT mip)
{
	UINT64 size = value >> mip;
	return static_cast<UINT>(size ? size : 1);
}

static UINT64 TextureMipBytes(const D3D12_RESOURCE_DESC &desc, UINT mip)
{
	const UINT width = MipSize(desc.Width, mip);
	const UINT height = MipSize(desc.Height, mip);
	if (IsBlockCompressedFormat(desc.Format)) {
		const UINT blockBytes = BitsPerPixel(desc.Format) == 4 ? 8 : 16;
		const UINT blocksWide = std::max<UINT>(1, (width + 3) / 4);
		const UINT blocksHigh = std::max<UINT>(1, (height + 3) / 4);
		return static_cast<UINT64>(blocksWide) * blocksHigh * blockBytes;
	}
	const uint32_t bpp = BitsPerPixel(desc.Format);
	if (!bpp)
		return 0;
	return (static_cast<UINT64>(width) * height * bpp + 7) / 8;
}

static void WriteDDSHeader(FILE *file, const D3D12_RESOURCE_DESC &desc)
{
	WriteU32(file, DX12ResourceDumpCore::MakeFourCC('D', 'D', 'S', ' '));
	WriteU32(file, 124);
	uint32_t flags = 0x00001007;
	if (desc.MipLevels > 1)
		flags |= 0x00020000;
	if (IsBlockCompressedFormat(desc.Format))
		flags |= 0x00080000;
	else
		flags |= 0x00000008;
	WriteU32(file, flags);
	WriteU32(file, desc.Height);
	WriteU32(file, static_cast<uint32_t>(desc.Width));
	WriteU32(file, static_cast<uint32_t>(TextureMipBytes(desc, 0)));
	WriteU32(file, 0);
	WriteU32(file, desc.MipLevels ? desc.MipLevels : 1);
	for (int i = 0; i < 11; ++i)
		WriteU32(file, 0);

	WriteU32(file, 32);
	WriteU32(file, 0x00000004);
	WriteU32(file, DX12ResourceDumpCore::MakeFourCC('D', 'X', '1', '0'));
	WriteU32(file, 0);
	WriteU32(file, 0);
	WriteU32(file, 0);
	WriteU32(file, 0);
	WriteU32(file, 0);

	uint32_t caps = 0x00001000;
	if (desc.MipLevels > 1)
		caps |= 0x00400008;
	WriteU32(file, caps);
	WriteU32(file, 0);
	WriteU32(file, 0);
	WriteU32(file, 0);
	WriteU32(file, 0);

	WriteU32(file, static_cast<uint32_t>(desc.Format));
	WriteU32(file, D3D12_RESOURCE_DIMENSION_TEXTURE2D);
	WriteU32(file, 0);
	WriteU32(file, desc.DepthOrArraySize ? desc.DepthOrArraySize : 1);
	WriteU32(file, 0);
}

static bool WriteTextureDDSFile(FILE *file, const DumpTask &task, const void *mappedData)
{
	return DX12ResourceDumpDdsUtils::WriteTexture2DFile(file, task, mappedData);
}

static bool WriteBufferFileData(FILE *file, const DumpTask &task, const void *mappedData)
{
	if (!file || !mappedData)
		return false;
	const uint8_t *src = static_cast<const uint8_t*>(mappedData) + task.readbackOffset;
	fwrite(src, 1, static_cast<size_t>(task.copyBytes), file);
	return true;
}

static void BuildTextFileName(const std::wstring &fileName, wchar_t *textName, size_t textNameCount)
{
	if (!textName || textNameCount == 0)
		return;
	textName[0] = L'\0';
	if (fileName.empty())
		return;
	const wchar_t *dot = wcsrchr(fileName.c_str(), L'.');
	if (dot) {
		const size_t prefixLength = static_cast<size_t>(dot - fileName.c_str());
		wcsncpy_s(textName, textNameCount, fileName.c_str(), prefixLength);
	} else {
		wcsncpy_s(textName, textNameCount, fileName.c_str(), _TRUNCATE);
	}
	wcscat_s(textName, textNameCount, L".txt");
}

static bool WriteGenericBufferTextFile(FILE *file, const DumpTask &task, const uint8_t *data)
{
	if (!file || !data)
		return false;

	fprintf(file, "bytes: %llu\n", static_cast<unsigned long long>(task.copyBytes));
	fprintf(file, "offset: %llu\n", static_cast<unsigned long long>(task.sourceOffset));
	const UINT stride = task.sourceKind == DumpTaskSource::InputAssembler ?
		task.iaBuffer.stride : task.binding.descriptor.structureByteStride;
	if (stride)
		fprintf(file, "stride: %u\n", stride);

	const UINT64 floatCount = task.copyBytes / sizeof(float);
	const float *floats = reinterpret_cast<const float*>(data);
	for (UINT64 i = 0; i < floatCount; ++i)
		fprintf(file, "buf[%llu]: %.9g\n", static_cast<unsigned long long>(i), floats[i]);
	return true;
}

static bool WriteIndexBufferTextFile(FILE *file, const DumpTask &task, const uint8_t *data)
{
	if (!file || !data)
		return false;

	const DX12FrameIaBufferBinding &buffer = task.iaBuffer;
	fprintf(file, "byte offset: %llu\n", static_cast<unsigned long long>(task.sourceOffset));
	fprintf(file, "index count: %llu\n", static_cast<unsigned long long>(
		buffer.format == DXGI_FORMAT_R16_UINT ? task.copyBytes / 2 : task.copyBytes / 4));
	fprintf(file, "format: %s\n", buffer.format == DXGI_FORMAT_R16_UINT ?
		"DXGI_FORMAT_R16_UINT" : buffer.format == DXGI_FORMAT_R32_UINT ?
		"DXGI_FORMAT_R32_UINT" : "UNKNOWN");

	if (buffer.format == DXGI_FORMAT_R16_UINT) {
		const uint16_t *indices = reinterpret_cast<const uint16_t*>(data);
		const UINT64 count = task.copyBytes / 2;
		for (UINT64 i = 0; i < count; ++i) {
			if (i % 3 == 0)
				fprintf(file, "\n");
			else
				fprintf(file, " ");
			fprintf(file, "%u", indices[i]);
		}
		fprintf(file, "\n");
		return true;
	}
	if (buffer.format == DXGI_FORMAT_R32_UINT) {
		const uint32_t *indices = reinterpret_cast<const uint32_t*>(data);
		const UINT64 count = task.copyBytes / 4;
		for (UINT64 i = 0; i < count; ++i) {
			if (i % 3 == 0)
				fprintf(file, "\n");
			else
				fprintf(file, " ");
			fprintf(file, "%u", indices[i]);
		}
		fprintf(file, "\n");
		return true;
	}
	return WriteGenericBufferTextFile(file, task, data);
}

static bool WriteVertexBufferTextFile(FILE *file, const DumpTask &task, const uint8_t *data)
{
	if (!file || !data)
		return false;

	const DX12FrameIaBufferBinding &buffer = task.iaBuffer;
	fprintf(file, "byte offset: %llu\n", static_cast<unsigned long long>(task.sourceOffset));
	fprintf(file, "slot: %u\n", buffer.slot);
	fprintf(file, "stride: %u\n", buffer.stride);

	const UINT stride = buffer.stride ? buffer.stride : 16;
	const UINT64 vertexCount = stride ? task.copyBytes / stride : 0;
	fprintf(file, "vertex count: %llu\n", static_cast<unsigned long long>(vertexCount));
	for (UINT64 v = 0; v < vertexCount; ++v) {
		const uint8_t *vertex = data + v * stride;
		fprintf(file, "vertex[%llu]", static_cast<unsigned long long>(v));
		const UINT floatCount = stride / sizeof(float);
		const float *floats = reinterpret_cast<const float*>(vertex);
		for (UINT c = 0; c < floatCount; ++c)
			fprintf(file, " f%u=%.9g", c, floats[c]);
		fprintf(file, "\n");
	}
	return true;
}

static bool WriteBufferTextFile(const wchar_t *path, const DumpTask &task, const void *mappedData)
{
	if (!path || !mappedData || task.isTexture)
		return false;

	FILE *file = _wfsopen(path, L"w", _SH_DENYNO);
	if (!file)
		return false;

	const bool ok = DX12ResourceDumpBufferTextUtils::WriteBufferTextFile(file, task, mappedData);

	fclose(file);
	return ok;
}

static uint32_t HashDumpTaskData(const DumpTask &task, const void *mappedData)
{
	uint32_t hash = 0;
	hash = DX12ResourceDumpCore::HashBytes(hash, &task.desc, sizeof(task.desc));
	hash = DX12ResourceDumpCore::HashBytes(hash, &task.sourceOffset, sizeof(task.sourceOffset));
	hash = DX12ResourceDumpCore::HashBytes(hash, &task.copyBytes, sizeof(task.copyBytes));
	hash = DX12ResourceDumpCore::HashBytes(hash, &task.isTexture, sizeof(task.isTexture));

	if (task.isTexture) {
		const uint8_t *mapped = static_cast<const uint8_t*>(mappedData);
		for (size_t subresource = 0; subresource < task.textureLayouts.size(); ++subresource) {
			const D3D12_PLACED_SUBRESOURCE_FOOTPRINT &layout = task.textureLayouts[subresource];
			hash = DX12ResourceDumpCore::HashBytes(hash, &layout.Footprint.RowPitch, sizeof(layout.Footprint.RowPitch));
			hash = DX12ResourceDumpCore::HashBytes(hash, &task.textureRows[subresource], sizeof(task.textureRows[subresource]));
			hash = DX12ResourceDumpCore::HashBytes(hash, &task.textureRowSizes[subresource], sizeof(task.textureRowSizes[subresource]));
			const uint8_t *base = mapped + layout.Offset;
			for (UINT row = 0; row < task.textureRows[subresource]; ++row) {
				const uint8_t *src = base + static_cast<size_t>(row) * layout.Footprint.RowPitch;
				hash = DX12ResourceDumpCore::HashBytes(hash, src, static_cast<size_t>(task.textureRowSizes[subresource]));
			}
		}
		return hash;
	}

	const uint8_t *src = static_cast<const uint8_t*>(mappedData) + task.readbackOffset;
	return DX12ResourceDumpCore::HashBytes(hash, src, static_cast<size_t>(task.copyBytes));
}

static bool UpdateTextureContentHashFromMappedData(DumpTask *task, const void *mappedData)
{
	if (!task || !mappedData || !task->isTexture)
		return false;
	if (task->hasContentHash && task->contentHash)
		return true;

	uint32_t hash = 0;
	const bool hasTopMipLayout =
		task->desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D &&
		!task->textureLayouts.empty() &&
		!task->textureRows.empty() &&
		!task->textureRowSizes.empty();
	if (hasTopMipLayout) {
		DX12TextureHashUtils::Texture2DTopMipHashFromPlacedFootprint(
			task->desc,
			task->textureLayouts[0],
			task->textureRows[0],
			task->textureRowSizes[0],
			mappedData,
			&hash);
	}
	if (!hash && DX12TryGetTextureHash(task->source, &hash)) {
		task->contentHash = hash;
		task->hasContentHash = true;
		return true;
	}
	if (!hash) {
		DX12LogManager::DebugFields("DX12TextureHashCache",
			"\"stage\":\"dump_hash_miss\",\"source\":\"%p\",\"dimension\":%u,\"format\":%u,\"width\":%llu,\"height\":%u,\"layouts\":%zu",
			task->source,
			static_cast<UINT>(task->desc.Dimension),
			static_cast<UINT>(task->desc.Format),
			static_cast<unsigned long long>(task->desc.Width),
			task->desc.Height,
			task->textureLayouts.size());
		return false;
	}

	task->contentHash = hash;
	task->hasContentHash = true;
	DX12CacheTextureHash(task->source, hash, &task->desc, "dump_readback");
	return true;
}

static void BuildDedupedRelativePath(const wchar_t *dedupeFileName, wchar_t *path, size_t pathCount)
{
	if (!path || pathCount == 0)
		return;
	path[0] = L'\0';
	if (!dedupeFileName || !dedupeFileName[0])
		return;
	swprintf_s(path, pathCount, L"deduped\\%s", dedupeFileName);
}

static uint32_t HuntHashForTask(const DumpTask &task)
{
	if (!task.isTexture)
		return 0;
	if (task.hasContentHash && task.contentHash)
		return task.contentHash;
	uint32_t cachedHash = 0;
	if (DX12TryGetTextureHash(task.source, &cachedHash))
		return cachedHash;
	if (task.sourceKind == DumpTaskSource::Output) {
		DX12FrameResourceBinding outputBinding =
			DX12ResourceDumpCore::ResourceBindingFromOutputBinding(task.outputBinding);
		return DX12ResourceDumpNaming::DescriptorHuntHash(outputBinding, task.copyBytes);
	}
	if (task.sourceKind == DumpTaskSource::InputAssembler)
		return 0;
	return DX12ResourceDumpNaming::DescriptorHuntHash(task.binding, task.copyBytes);
}

static bool WriteDedupedResourceFile(
	const wchar_t *dedupeDir, const wchar_t *resourceDir,
	DumpTask *task, const void *mappedData,
	std::wstring *dedupePathOut = nullptr)
{
	if (!dedupeDir || !resourceDir || !task || !mappedData)
		return false;

	UpdateTextureContentHashFromMappedData(task, mappedData);
	const uint32_t fileHash = HashDumpTaskData(*task, mappedData);
	const uint32_t huntHash = HuntHashForTask(*task);
	wchar_t dedupeFileName[MAX_PATH];
	DX12ResourceDumpDedupedNamingUtils::BuildDedupedFileName(
		*task, huntHash, fileHash, dedupeFileName, ARRAYSIZE(dedupeFileName));
	wchar_t dedupePath[MAX_PATH];
	swprintf_s(dedupePath, L"%s\\%s", dedupeDir, dedupeFileName);
	if (dedupePathOut) {
		wchar_t relativePath[MAX_PATH];
		BuildDedupedRelativePath(dedupeFileName, relativePath, ARRAYSIZE(relativePath));
		*dedupePathOut = relativePath;
	}

	if (GetFileAttributesW(dedupePath) == INVALID_FILE_ATTRIBUTES) {
		FILE *file = _wfsopen(dedupePath, L"wb", _SH_DENYNO);
		if (!file)
			return false;
		bool ok = task->isTexture ? WriteTextureDDSFile(file, *task, mappedData) :
			WriteBufferFileData(file, *task, mappedData);
		fclose(file);
		if (!ok) {
			DeleteFileW(dedupePath);
			return false;
		}
	}
	if (!task->isTexture) {
		wchar_t dedupeTextPath[MAX_PATH];
		BuildTextFileName(dedupePath, dedupeTextPath, ARRAYSIZE(dedupeTextPath));
		if (GetFileAttributesW(dedupeTextPath) == INVALID_FILE_ATTRIBUTES)
			WriteBufferTextFile(dedupeTextPath, *task, mappedData);
	}
	return true;
}

static bool MapAndWriteTask(
	const wchar_t *dedupeDir, const wchar_t *resourceDir, DumpTask *task,
	std::wstring *dedupePath)
{
	if (!dedupeDir || !resourceDir || !task)
		return false;

	bool ok = false;
	__try {
		void *mapped = nullptr;
		D3D12_RANGE range = { static_cast<SIZE_T>(task->readbackOffset),
			static_cast<SIZE_T>(task->readbackOffset + task->copyBytes) };
		ID3D12Resource *mapResource = task->directMap ? task->source : task->readback;
		if (!mapResource) {
			task->skipNote = task->directMap ? "missing_direct_resource" : "missing_readback";
			DX12FrameAnalysis::LogJsonFunc("ResourceMapWriteSkipped",
				"\"reason\":\"%s\",\"sourceKind\":\"%s\",\"source\":\"%p\",\"readback\":\"%p\",\"direct\":%u,\"offset\":%llu,\"bytes\":%llu",
				task->skipNote,
				DX12ResourceDumpCore::DumpTaskSourceName(task->sourceKind),
				task->source, task->readback, task->directMap ? 1 : 0,
				static_cast<unsigned long long>(task->readbackOffset),
				static_cast<unsigned long long>(task->copyBytes));
			return false;
		}
	const HRESULT hr = mapResource->Map(0, &range, &mapped);
	if (SUCCEEDED(hr) && mapped) {
		ok = WriteDedupedResourceFile(dedupeDir, resourceDir, task, mapped, dedupePath);
		D3D12_RANGE emptyRange = { 0, 0 };
			mapResource->Unmap(0, &emptyRange);
		} else {
			task->skipNote = "map_failed";
			DX12FrameAnalysis::LogJsonFunc("ResourceMapWriteSkipped",
				"\"reason\":\"map_failed\",\"hr\":\"0x%lx\",\"sourceKind\":\"%s\",\"source\":\"%p\",\"readback\":\"%p\",\"direct\":%u,\"offset\":%llu,\"bytes\":%llu",
				hr,
				DX12ResourceDumpCore::DumpTaskSourceName(task->sourceKind),
				task->source, task->readback, task->directMap ? 1 : 0,
				static_cast<unsigned long long>(task->readbackOffset),
				static_cast<unsigned long long>(task->copyBytes));
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		task->skipNote = "map_or_write_exception";
		DX12FrameAnalysis::LogJsonFunc("ResourceMapWriteSkipped",
			"\"exception\":\"0x%lx\",\"source\":\"%p\",\"readback\":\"%p\",\"direct\":%u,\"offset\":%llu,\"bytes\":%llu",
			GetExceptionCode(), task->source, task->readback, task->directMap ? 1 : 0,
			static_cast<unsigned long long>(task->readbackOffset),
			static_cast<unsigned long long>(task->copyBytes));
		ok = false;
	}
	return ok;
}


};
