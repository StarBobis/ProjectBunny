#pragma once

#include "Resources/DX12ResourceServices.hpp"

#pragma pack(push, 1)
class DX12ResourceDumpDdsPixelFormat
{
public:
	uint32_t size = 0;
	uint32_t flags = 0;
	uint32_t fourCc = 0;
	uint32_t rgbBitCount = 0;
	uint32_t rBitMask = 0;
	uint32_t gBitMask = 0;
	uint32_t bBitMask = 0;
	uint32_t aBitMask = 0;
};

class DX12ResourceDumpDdsHeader
{
public:
	uint32_t size = 0;
	uint32_t flags = 0;
	uint32_t height = 0;
	uint32_t width = 0;
	uint32_t pitchOrLinearSize = 0;
	uint32_t depth = 0;
	uint32_t mipMapCount = 0;
	uint32_t reserved1[11] = {};
	DX12ResourceDumpDdsPixelFormat ddspf;
	uint32_t caps = 0;
	uint32_t caps2 = 0;
	uint32_t caps3 = 0;
	uint32_t caps4 = 0;
	uint32_t reserved2 = 0;
};

class DX12ResourceDumpDdsHeaderDxt10
{
public:
	uint32_t dxgiFormat = 0;
	uint32_t resourceDimension = 0;
	uint32_t miscFlag = 0;
	uint32_t arraySize = 0;
	uint32_t miscFlags2 = 0;
};
#pragma pack(pop)

static_assert(sizeof(DX12ResourceDumpDdsPixelFormat) == 32, "DDS pixel format layout mismatch");
static_assert(sizeof(DX12ResourceDumpDdsHeader) == 124, "DDS header layout mismatch");
static_assert(sizeof(DX12ResourceDumpDdsHeaderDxt10) == 20, "DDS DX10 header layout mismatch");

class DX12ResourceDumpDdsUtils
{
public:
	static constexpr uint32_t DDS_MAGIC = 0x20534444;
	static constexpr uint32_t DDS_FOURCC = 0x00000004;
	static constexpr uint32_t DDS_RGB = 0x00000040;
	static constexpr uint32_t DDS_RGBA = 0x00000041;
	static constexpr uint32_t DDS_LUMINANCE = 0x00020000;
	static constexpr uint32_t DDS_LUMINANCEA = 0x00020001;
	static constexpr uint32_t DDS_ALPHA = 0x00000002;
	static constexpr uint32_t DDS_BUMPDUDV = 0x00080000;
	static constexpr uint32_t DDS_HEADER_FLAGS_TEXTURE = 0x00001007;
	static constexpr uint32_t DDS_HEADER_FLAGS_MIPMAP = 0x00020000;
	static constexpr uint32_t DDS_HEADER_FLAGS_PITCH = 0x00000008;
	static constexpr uint32_t DDS_HEADER_FLAGS_LINEARSIZE = 0x00080000;
	static constexpr uint32_t DDS_SURFACE_FLAGS_TEXTURE = 0x00001000;

	static DX12ResourceDumpDdsPixelFormat PixelFormat(
		uint32_t flags, uint32_t fourCc, uint32_t rgbBitCount,
		uint32_t rBitMask, uint32_t gBitMask, uint32_t bBitMask, uint32_t aBitMask)
	{
		DX12ResourceDumpDdsPixelFormat format;
		format.size = sizeof(DX12ResourceDumpDdsPixelFormat);
		format.flags = flags;
		format.fourCc = fourCc;
		format.rgbBitCount = rgbBitCount;
		format.rBitMask = rBitMask;
		format.gBitMask = gBitMask;
		format.bBitMask = bBitMask;
		format.aBitMask = aBitMask;
		return format;
	}

	static DX12ResourceDumpDdsPixelFormat FourCcPixelFormat(char a, char b, char c, char d)
	{
		return PixelFormat(DDS_FOURCC, DX12ResourceDumpCore::MakeFourCC(a, b, c, d), 0, 0, 0, 0, 0);
	}

	static bool IsUnsupportedLegacyDdsFormat(DXGI_FORMAT format)
	{
		return format == DXGI_FORMAT_AI44 ||
			format == DXGI_FORMAT_IA44 ||
			format == DXGI_FORMAT_P8 ||
			format == DXGI_FORMAT_A8P8;
	}

	static bool TryGetLegacyPixelFormat(
		DXGI_FORMAT format, DX12ResourceDumpDdsPixelFormat *pixelFormat)
	{
		if (!pixelFormat)
			return false;

		switch (format) {
		case DXGI_FORMAT_R8G8B8A8_UNORM:
			*pixelFormat = PixelFormat(DDS_RGBA, 0, 32, 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000);
			return true;
		case DXGI_FORMAT_R16G16_UNORM:
			*pixelFormat = PixelFormat(DDS_RGB, 0, 32, 0x0000ffff, 0xffff0000, 0x00000000, 0x00000000);
			return true;
		case DXGI_FORMAT_R8G8_UNORM:
			*pixelFormat = PixelFormat(DDS_LUMINANCEA, 0, 16, 0x00ff, 0x0000, 0x0000, 0xff00);
			return true;
		case DXGI_FORMAT_R16_UNORM:
			*pixelFormat = PixelFormat(DDS_LUMINANCE, 0, 16, 0xffff, 0x0000, 0x0000, 0x0000);
			return true;
		case DXGI_FORMAT_R8_UNORM:
			*pixelFormat = PixelFormat(DDS_LUMINANCE, 0, 8, 0xff, 0x00, 0x00, 0x00);
			return true;
		case DXGI_FORMAT_A8_UNORM:
			*pixelFormat = PixelFormat(DDS_ALPHA, 0, 8, 0x00, 0x00, 0x00, 0xff);
			return true;
		case DXGI_FORMAT_R8G8_B8G8_UNORM:
			*pixelFormat = FourCcPixelFormat('R', 'G', 'B', 'G');
			return true;
		case DXGI_FORMAT_G8R8_G8B8_UNORM:
			*pixelFormat = FourCcPixelFormat('G', 'R', 'G', 'B');
			return true;
		case DXGI_FORMAT_BC1_UNORM:
			*pixelFormat = FourCcPixelFormat('D', 'X', 'T', '1');
			return true;
		case DXGI_FORMAT_BC2_UNORM:
			*pixelFormat = FourCcPixelFormat('D', 'X', 'T', '3');
			return true;
		case DXGI_FORMAT_BC3_UNORM:
			*pixelFormat = FourCcPixelFormat('D', 'X', 'T', '5');
			return true;
		case DXGI_FORMAT_BC4_UNORM:
			*pixelFormat = FourCcPixelFormat('B', 'C', '4', 'U');
			return true;
		case DXGI_FORMAT_BC4_SNORM:
			*pixelFormat = FourCcPixelFormat('B', 'C', '4', 'S');
			return true;
		case DXGI_FORMAT_BC5_UNORM:
			*pixelFormat = FourCcPixelFormat('B', 'C', '5', 'U');
			return true;
		case DXGI_FORMAT_BC5_SNORM:
			*pixelFormat = FourCcPixelFormat('B', 'C', '5', 'S');
			return true;
		case DXGI_FORMAT_B5G6R5_UNORM:
			*pixelFormat = PixelFormat(DDS_RGB, 0, 16, 0x0000f800, 0x000007e0, 0x0000001f, 0x00000000);
			return true;
		case DXGI_FORMAT_B5G5R5A1_UNORM:
			*pixelFormat = PixelFormat(DDS_RGBA, 0, 16, 0x00007c00, 0x000003e0, 0x0000001f, 0x00008000);
			return true;
		case DXGI_FORMAT_R8G8_SNORM:
			*pixelFormat = PixelFormat(DDS_BUMPDUDV, 0, 16, 0x00ff, 0xff00, 0x0000, 0x0000);
			return true;
		case DXGI_FORMAT_R8G8B8A8_SNORM:
			*pixelFormat = PixelFormat(DDS_BUMPDUDV, 0, 32, 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000);
			return true;
		case DXGI_FORMAT_R16G16_SNORM:
			*pixelFormat = PixelFormat(DDS_BUMPDUDV, 0, 32, 0x0000ffff, 0xffff0000, 0x00000000, 0x00000000);
			return true;
		case DXGI_FORMAT_B8G8R8A8_UNORM:
			*pixelFormat = PixelFormat(DDS_RGBA, 0, 32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000);
			return true;
		case DXGI_FORMAT_B8G8R8X8_UNORM:
			*pixelFormat = PixelFormat(DDS_RGB, 0, 32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0x00000000);
			return true;
		case DXGI_FORMAT_YUY2:
			*pixelFormat = FourCcPixelFormat('Y', 'U', 'Y', '2');
			return true;
		case DXGI_FORMAT_B4G4R4A4_UNORM:
			*pixelFormat = PixelFormat(DDS_RGBA, 0, 16, 0x00000f00, 0x000000f0, 0x0000000f, 0x0000f000);
			return true;
		case DXGI_FORMAT_R32G32B32A32_FLOAT:
			*pixelFormat = PixelFormat(DDS_FOURCC, 116, 0, 0, 0, 0, 0);
			return true;
		case DXGI_FORMAT_R16G16B16A16_FLOAT:
			*pixelFormat = PixelFormat(DDS_FOURCC, 113, 0, 0, 0, 0, 0);
			return true;
		case DXGI_FORMAT_R16G16B16A16_UNORM:
			*pixelFormat = PixelFormat(DDS_FOURCC, 36, 0, 0, 0, 0, 0);
			return true;
		case DXGI_FORMAT_R16G16B16A16_SNORM:
			*pixelFormat = PixelFormat(DDS_FOURCC, 110, 0, 0, 0, 0, 0);
			return true;
		case DXGI_FORMAT_R32G32_FLOAT:
			*pixelFormat = PixelFormat(DDS_FOURCC, 115, 0, 0, 0, 0, 0);
			return true;
		case DXGI_FORMAT_R16G16_FLOAT:
			*pixelFormat = PixelFormat(DDS_FOURCC, 112, 0, 0, 0, 0, 0);
			return true;
		case DXGI_FORMAT_R32_FLOAT:
			*pixelFormat = PixelFormat(DDS_FOURCC, 114, 0, 0, 0, 0, 0);
			return true;
		case DXGI_FORMAT_R16_FLOAT:
			*pixelFormat = PixelFormat(DDS_FOURCC, 111, 0, 0, 0, 0, 0);
			return true;
		default:
			return false;
		}
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

	static UINT BlockBytes(DXGI_FORMAT format)
	{
		switch (format) {
		case DXGI_FORMAT_BC1_TYPELESS:
		case DXGI_FORMAT_BC1_UNORM:
		case DXGI_FORMAT_BC1_UNORM_SRGB:
		case DXGI_FORMAT_BC4_TYPELESS:
		case DXGI_FORMAT_BC4_UNORM:
		case DXGI_FORMAT_BC4_SNORM:
			return 8;
		default:
			return 16;
		}
	}

	static UINT BitsPerPixel(DXGI_FORMAT format)
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
		case DXGI_FORMAT_A8P8:
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

	static bool GetSurfaceInfo(
		UINT width, UINT height, DXGI_FORMAT format,
		UINT64 *slicePitch, UINT64 *rowPitch, UINT64 *rowCount)
	{
		if (!width || !height || !slicePitch || !rowPitch || !rowCount)
			return false;
		if (IsBlockCompressedFormat(format)) {
			const UINT64 blocksWide = (std::max<UINT64>)(1, (static_cast<UINT64>(width) + 3) / 4);
			const UINT64 blocksHigh = (std::max<UINT64>)(1, (static_cast<UINT64>(height) + 3) / 4);
			*rowPitch = blocksWide * BlockBytes(format);
			*rowCount = blocksHigh;
			*slicePitch = (*rowPitch) * (*rowCount);
			return true;
		}
		const UINT bitsPerPixel = BitsPerPixel(format);
		if (!bitsPerPixel)
			return false;
		*rowPitch = (static_cast<UINT64>(width) * bitsPerPixel + 7) / 8;
		*rowCount = height;
		*slicePitch = (*rowPitch) * (*rowCount);
		return true;
	}

	static DXGI_FORMAT ResolveTextureFormat(const DumpTask &task)
	{
		UINT viewFormat = 0;
		if (task.sourceKind == DumpTaskSource::Output &&
				task.outputBinding.hasDescriptor &&
				task.outputBinding.descriptor.viewFormat)
			viewFormat = task.outputBinding.descriptor.viewFormat;
		else if (task.binding.hasDescriptor && task.binding.descriptor.viewFormat)
			viewFormat = task.binding.descriptor.viewFormat;
		return viewFormat ? static_cast<DXGI_FORMAT>(viewFormat) : task.desc.Format;
	}

	static bool BuildHeader(
		const DumpTask &task, DXGI_FORMAT format,
		DX12ResourceDumpDdsHeader *header,
		DX12ResourceDumpDdsHeaderDxt10 *dx10Header,
		bool *hasDx10Header,
		UINT64 *slicePitch,
		UINT64 *rowPitch,
		UINT64 *rowCount)
	{
		if (!header || !dx10Header || !hasDx10Header || !slicePitch || !rowPitch || !rowCount)
			return false;
		if (task.desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D)
			return false;
		if (task.desc.Width > UINT32_MAX || task.desc.Height > UINT32_MAX)
			return false;
		if (IsUnsupportedLegacyDdsFormat(format))
			return false;
		if (!GetSurfaceInfo(
				static_cast<UINT>(task.desc.Width), task.desc.Height, format,
				slicePitch, rowPitch, rowCount))
			return false;

		*header = DX12ResourceDumpDdsHeader();
		*dx10Header = DX12ResourceDumpDdsHeaderDxt10();
		*hasDx10Header = false;

		header->size = sizeof(DX12ResourceDumpDdsHeader);
		header->flags = DDS_HEADER_FLAGS_TEXTURE | DDS_HEADER_FLAGS_MIPMAP;
		header->height = task.desc.Height;
		header->width = static_cast<uint32_t>(task.desc.Width);
		header->mipMapCount = 1;
		header->caps = DDS_SURFACE_FLAGS_TEXTURE;

		if (!TryGetLegacyPixelFormat(format, &header->ddspf)) {
			header->ddspf = FourCcPixelFormat('D', 'X', '1', '0');
			*hasDx10Header = true;
			dx10Header->dxgiFormat = static_cast<uint32_t>(format);
			dx10Header->resourceDimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			dx10Header->arraySize = 1;
		}

		if (IsBlockCompressedFormat(format)) {
			header->flags |= DDS_HEADER_FLAGS_LINEARSIZE;
			header->pitchOrLinearSize = static_cast<uint32_t>(*slicePitch);
		} else {
			header->flags |= DDS_HEADER_FLAGS_PITCH;
			header->pitchOrLinearSize = static_cast<uint32_t>(*rowPitch);
		}
		return true;
	}

	static bool WriteZeroBytes(FILE *file, UINT64 byteCount)
	{
		if (!file)
			return false;
		static const unsigned char zeroes[256] = {};
		while (byteCount) {
			const size_t chunk = static_cast<size_t>((std::min<UINT64>)(byteCount, sizeof(zeroes)));
			if (fwrite(zeroes, 1, chunk, file) != chunk)
				return false;
			byteCount -= chunk;
		}
		return true;
	}

	static bool WriteTexture2DFile(FILE *file, const DumpTask &task, const void *mappedData)
	{
		if (!file || !mappedData || !task.isTexture)
			return false;
		if (task.textureLayouts.empty() || task.textureRows.empty() || task.textureRowSizes.empty())
			return false;

		const DXGI_FORMAT format = ResolveTextureFormat(task);
		DX12ResourceDumpDdsHeader header;
		DX12ResourceDumpDdsHeaderDxt10 dx10Header;
		bool hasDx10Header = false;
		UINT64 slicePitch = 0;
		UINT64 rowPitch = 0;
		UINT64 rowCount = 0;
		if (!BuildHeader(task, format, &header, &dx10Header, &hasDx10Header, &slicePitch, &rowPitch, &rowCount))
			return false;
		if (rowCount > task.textureRows[0])
			return false;

		const uint32_t magic = DDS_MAGIC;
		if (fwrite(&magic, sizeof(magic), 1, file) != 1)
			return false;
		if (fwrite(&header, sizeof(header), 1, file) != 1)
			return false;
		if (hasDx10Header && fwrite(&dx10Header, sizeof(dx10Header), 1, file) != 1)
			return false;

		const D3D12_PLACED_SUBRESOURCE_FOOTPRINT &layout = task.textureLayouts[0];
		const unsigned char *mapped = static_cast<const unsigned char*>(mappedData);
		const unsigned char *base = mapped + layout.Offset;
		const UINT64 sourceRowPitch = layout.Footprint.RowPitch;
		const UINT64 availableRowBytes = task.textureRowSizes[0];
		const UINT64 copyBytes = (std::min<UINT64>)(rowPitch, (std::min<UINT64>)(sourceRowPitch, availableRowBytes));
		for (UINT64 row = 0; row < rowCount; ++row) {
			const unsigned char *src = base + row * sourceRowPitch;
			if (copyBytes && fwrite(src, 1, static_cast<size_t>(copyBytes), file) != copyBytes)
				return false;
			if (copyBytes < rowPitch && !WriteZeroBytes(file, rowPitch - copyBytes))
				return false;
		}
		return true;
	}
};




class DX12ResourceDumpPolicy
{
public:
	static bool IsTextureResourceDimension(D3D12_RESOURCE_DIMENSION dimension)
	{
		return dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D ||
			dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
			dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D;
	}

	static bool ShouldDumpBinding(const DX12FrameResourceBinding &binding)
	{
		if (!binding.usageClass.empty()) {
			if (!binding.hasDescriptor)
				return true;
			return binding.descriptor.resource &&
				binding.descriptor.hasResourceDesc &&
				binding.descriptor.resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER;
		}

		if (binding.bindSpace == "hunting_metadata" ||
			binding.bindSpace == "hunting_resource") {
			if (binding.bindSpace == "hunting_resource") {
				const bool isBufferResource =
					binding.hasDescriptor &&
					binding.descriptor.resource &&
					binding.descriptor.hasResourceDesc &&
					binding.descriptor.resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER;
				const bool isTextureResource =
					binding.hasDescriptor &&
					binding.descriptor.resource &&
					binding.descriptor.hasResourceDesc &&
					IsTextureResourceDimension(binding.descriptor.resourceDesc.Dimension);
				return binding.hasDescriptor &&
					binding.descriptor.resource &&
					binding.descriptor.hasResourceDesc &&
					(isBufferResource || isTextureResource);
			}
			const bool isBufferResource =
				binding.hasDescriptor &&
				binding.descriptor.resource &&
				binding.descriptor.hasResourceDesc &&
				binding.descriptor.resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER;
			const bool isTextureResource =
				binding.hasDescriptor &&
				binding.descriptor.resource &&
				binding.descriptor.hasResourceDesc &&
				IsTextureResourceDimension(binding.descriptor.resourceDesc.Dimension);
			return binding.hasDescriptor &&
				binding.descriptor.resource &&
				binding.descriptor.hasResourceDesc &&
				((isTextureResource && (
					binding.descriptor.kind == DescriptorKind::SRV ||
					binding.descriptor.kind == DescriptorKind::UAV ||
					binding.descriptor.kind == DescriptorKind::RTV ||
					binding.descriptor.kind == DescriptorKind::DSV)) ||
				 (isBufferResource && (
					binding.descriptor.kind == DescriptorKind::CBV ||
					binding.descriptor.kind == DescriptorKind::SRV ||
					binding.descriptor.kind == DescriptorKind::UAV)));
		}

		const bool isDispatch = binding.dispatchId != 0;
		const bool isDraw = binding.drawId != 0;
		if (!isDispatch && !isDraw)
			return false;

		const bool isGraphicsSpace =
			binding.bindSpace == "graphics_cbv_srv_uav" ||
			binding.bindSpace == "graphics_sampler" ||
			binding.bindSpace == "graphics_root";
		const bool isComputeSpace =
			binding.bindSpace == "compute_cbv_srv_uav" ||
			binding.bindSpace == "compute_sampler" ||
			binding.bindSpace == "compute_root" ||
			binding.bindSpace == "hunting_uav_state";
		const bool isOutputSpace = binding.bindSpace == "output";
		if (isOutputSpace)
			return binding.hasDescriptor &&
				binding.descriptor.resource &&
				binding.descriptor.hasResourceDesc &&
				IsTextureResourceDimension(binding.descriptor.resourceDesc.Dimension) &&
				(binding.descriptor.kind == DescriptorKind::RTV ||
				 binding.descriptor.kind == DescriptorKind::DSV);
		if ((isDispatch && !isComputeSpace) || (!isDispatch && !isGraphicsSpace))
			return false;
		if (binding.rootDescriptor)
			return binding.gpuVirtualAddress != 0 &&
				(binding.rangeType == D3D12_ROOT_PARAMETER_TYPE_CBV ||
				 binding.rangeType == D3D12_ROOT_PARAMETER_TYPE_SRV ||
				 binding.rangeType == D3D12_ROOT_PARAMETER_TYPE_UAV);
		if (!binding.hasDescriptor)
			return false;
		if (!binding.descriptor.resource || !binding.descriptor.hasResourceDesc)
			return false;
		const bool isBufferResource =
			binding.descriptor.resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER;
		const bool isTextureResource =
			IsTextureResourceDimension(binding.descriptor.resourceDesc.Dimension);
		if (!isBufferResource && !isTextureResource)
			return false;
		if (isDispatch)
			return binding.descriptor.kind == DescriptorKind::CBV ||
				binding.descriptor.kind == DescriptorKind::SRV ||
				binding.descriptor.kind == DescriptorKind::UAV;
		if (binding.descriptor.kind == DescriptorKind::CBV)
			return isBufferResource;
		return binding.descriptor.kind == DescriptorKind::SRV ||
			binding.descriptor.kind == DescriptorKind::UAV;
	}

	static const char *ShortBindSpace(const std::string &bindSpace)
	{
		if (bindSpace == "graphics_cbv_srv_uav")
			return "gfx";
		if (bindSpace == "graphics_sampler")
			return "gsp";
		if (bindSpace == "compute_cbv_srv_uav")
			return "cmp";
		if (bindSpace == "compute_sampler")
			return "csp";
		if (bindSpace == "hunting_metadata")
			return "meta";
		if (bindSpace == "hunting_resource")
			return "res";
		return bindSpace.empty() ? "unk" : bindSpace.c_str();
	}

	static const char *ShortDescriptorKind(const std::string &kind)
	{
		if (kind == DescriptorKind::CBV || kind == DescriptorKind::CONSTANT_BUFFER_VIEW)
			return "cbv";
		if (kind == DescriptorKind::SRV || kind == DescriptorKind::SHADER_RESOURCE_VIEW)
			return "srv";
		if (kind == DescriptorKind::UAV || kind == DescriptorKind::UNORDERED_ACCESS_VIEW)
			return "uav";
		if (kind == DescriptorKind::Sampler)
			return "smp";
		if (kind == DescriptorKind::RTV || kind == DescriptorKind::RENDER_TARGET_VIEW)
			return "rtv";
		if (kind == DescriptorKind::DSV || kind == DescriptorKind::DEPTH_STENCIL_VIEW)
			return "dsv";
		return kind.empty() ? "res" : kind.c_str();
	}

	static const char *ShaderVisibilityPrefix(UINT shaderVisibility, const std::string &bindSpace)
	{
		if (bindSpace.rfind("compute", 0) == 0)
			return "cs";
		switch (shaderVisibility) {
		case D3D12_SHADER_VISIBILITY_VERTEX:
			return "vs";
		case D3D12_SHADER_VISIBILITY_HULL:
			return "hs";
		case D3D12_SHADER_VISIBILITY_DOMAIN:
			return "ds";
		case D3D12_SHADER_VISIBILITY_GEOMETRY:
			return "gs";
		case D3D12_SHADER_VISIBILITY_PIXEL:
			return "ps";
		case D3D12_SHADER_VISIBILITY_ALL:
		default:
			return "all";
		}
	}

	static char RegisterPrefix(UINT rangeType, const std::string &descriptorKind)
	{
		if (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV ||
				rangeType == D3D12_ROOT_PARAMETER_TYPE_SRV)
			return 't';
		if (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV ||
				rangeType == D3D12_ROOT_PARAMETER_TYPE_UAV)
			return 'u';
		if (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_CBV ||
				rangeType == D3D12_ROOT_PARAMETER_TYPE_CBV)
			return 'b';
		if (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER)
			return 's';
		if (descriptorKind == DescriptorKind::SRV || descriptorKind == DescriptorKind::SHADER_RESOURCE_VIEW)
			return 't';
		if (descriptorKind == DescriptorKind::UAV || descriptorKind == DescriptorKind::UNORDERED_ACCESS_VIEW)
			return 'u';
		if (descriptorKind == DescriptorKind::CBV || descriptorKind == DescriptorKind::CONSTANT_BUFFER_VIEW)
			return 'b';
		if (descriptorKind == DescriptorKind::Sampler)
			return 's';
		return '\0';
	}

	static bool BuildSemanticSlot(
		const DX12FrameResourceBinding &binding, char *text, size_t textCount)
	{
		if (!text || textCount == 0)
			return false;
		text[0] = '\0';
		if (binding.bindSpace == "hunting_metadata") {
			sprintf_s(text, textCount, "meta-d%llu",
				static_cast<unsigned long long>(binding.descriptorIndex));
			return true;
		}
		if (binding.bindSpace == "hunting_resource") {
			sprintf_s(text, textCount, "res-d%llu",
				static_cast<unsigned long long>(binding.descriptorIndex));
			return true;
		}
		if (binding.shaderRegister == UINT_MAX)
			return false;
		const char registerPrefix = RegisterPrefix(binding.rangeType, binding.descriptor.kind);
		if (!registerPrefix)
			return false;
		const char *stage = ShaderVisibilityPrefix(binding.shaderVisibility, binding.bindSpace);
		sprintf_s(text, textCount, "%s-%c%u", stage, registerPrefix, binding.shaderRegister);
		return true;
	}
};




class DX12ResourceDumpNaming
{
public:
static const wchar_t *FileNameFromPath(const std::wstring &path)
{
	const wchar_t *text = path.c_str();
	const wchar_t *slash = wcsrchr(text, L'\\');
	return slash ? slash + 1 : text;
}

static bool IsHashChar(wchar_t value)
{
	return (value >= L'0' && value <= L'9') ||
		(value >= L'a' && value <= L'f') ||
		(value >= L'A' && value <= L'F');
}

static bool IsHashText(const wchar_t *text)
{
	if (!text)
		return false;
	for (size_t i = 0; i < 8; ++i) {
		if (!IsHashChar(text[i]))
			return false;
	}
	return true;
}

static void CopyHashText(const wchar_t *source, char *hash, size_t hashCount)
{
	if (!hash || hashCount == 0)
		return;
	hash[0] = '\0';
	if (!source)
		return;
	size_t i = 0;
	for (; i < hashCount - 1 && i < 8 && source[i]; ++i)
		hash[i] = static_cast<char>(source[i]);
	hash[i] = '\0';
}

static void FileHashFromPath(const std::wstring &path, char *hash, size_t hashCount)
{
	if (!hash || hashCount == 0)
		return;
	hash[0] = '\0';
	const wchar_t *name = FileNameFromPath(path);
	if (IsHashText(name) && name[8] == L'_' &&
			IsHashText(name + 9) &&
			(name[17] == L'-' || name[17] == L'.' || name[17] == L'\0')) {
		CopyHashText(name + 9, hash, hashCount);
		return;
	}
	size_t i = 0;
	for (; i < hashCount - 1 && i < 8 && name[i] && name[i] != L'-'; ++i)
		hash[i] = static_cast<char>(name[i]);
	hash[i] = '\0';
}

static uint32_t DescriptorHuntHash(const DX12FrameResourceBinding &binding, UINT64 copyBytes)
{
	const DX12DescriptorSummary &descriptor = binding.descriptor;
	if (!binding.hasDescriptor) {
		uint32_t hash = DX12ResourceDumpCore::MakeFourCC('D', 'S', 'L', 'T');
		hash = DX12ResourceDumpCore::HashBytes(hash, &binding.psoIndex, sizeof(binding.psoIndex));
		hash = DX12ResourceDumpCore::HashBytes(hash, &binding.rootParameterIndex, sizeof(binding.rootParameterIndex));
		hash = DX12ResourceDumpCore::HashBytes(hash, &binding.rangeIndex, sizeof(binding.rangeIndex));
		hash = DX12ResourceDumpCore::HashBytes(hash, &binding.rangeType, sizeof(binding.rangeType));
		hash = DX12ResourceDumpCore::HashBytes(hash, &binding.shaderRegister, sizeof(binding.shaderRegister));
		hash = DX12ResourceDumpCore::HashBytes(hash, &binding.registerSpace, sizeof(binding.registerSpace));
		hash = DX12ResourceDumpCore::HashBytes(hash, &binding.descriptorIndex, sizeof(binding.descriptorIndex));
		return hash;
	}
	const UINT64 fallbackGpuVa = descriptor.gpuVirtualAddress ?
		descriptor.gpuVirtualAddress : binding.gpuVirtualAddress;
	const UINT64 fallbackSize = descriptor.viewSize ? descriptor.viewSize : copyBytes;
	return DX12HashDescriptorBufferView(
		binding.hasDescriptor ? &descriptor : nullptr, fallbackGpuVa, fallbackSize);
}

static void FormatEventPrefix(UINT64 drawId, UINT64 dispatchId, wchar_t *text, size_t textCount)
{
	if (!text || textCount == 0)
		return;
	if (dispatchId)
		swprintf_s(text, textCount, L"c%06llu",
			static_cast<unsigned long long>(dispatchId));
	else if (drawId)
		swprintf_s(text, textCount, L"d%06llu",
			static_cast<unsigned long long>(drawId));
	else
		swprintf_s(text, textCount, L"e%06llu",
			static_cast<unsigned long long>(0));
}

static void FormatMetadataPrefix(UINT64 descriptorIndex, wchar_t *text, size_t textCount)
{
	if (!text || textCount == 0)
		return;
	swprintf_s(text, textCount, L"m%06llu",
		static_cast<unsigned long long>(descriptorIndex));
}

static void FormatEventPrefixWithSerial(
	UINT64 drawId, UINT64 dispatchId, UINT64 eventSerial, wchar_t *text, size_t textCount)
{
	if (!text || textCount == 0)
		return;
	if (dispatchId)
		swprintf_s(text, textCount, L"c%06llu",
			static_cast<unsigned long long>(dispatchId));
	else if (drawId)
		swprintf_s(text, textCount, L"d%06llu",
			static_cast<unsigned long long>(drawId));
	else
		swprintf_s(text, textCount, L"e%06llu",
			static_cast<unsigned long long>(eventSerial));
}

static void AppendShaderHashNamePart(
	wchar_t *text, size_t textCount, const char *stage, bool hasHash, UINT64 hash)
{
	if (!text || textCount == 0 || !stage || !hasHash)
		return;
	const size_t used = wcslen(text);
	if (used >= textCount)
		return;
	swprintf_s(text + used, textCount - used, L"-%S%016llx",
		stage, static_cast<unsigned long long>(hash));
}

static bool HasAnyShaderHash(const DX12PsoShaderInfo &info)
{
	return info.hasVS || info.hasPS || info.hasCS;
}

static void BuildShaderHashNamePart(
	const DX12PsoShaderInfo &info, wchar_t *text, size_t textCount)
{
	if (!text || textCount == 0)
		return;
	text[0] = L'\0';
	AppendShaderHashNamePart(text, textCount, "vs", info.hasVS, info.vs);
	AppendShaderHashNamePart(text, textCount, "ps", info.hasPS, info.ps);
	AppendShaderHashNamePart(text, textCount, "cs", info.hasCS, info.cs);
}

static void BuildFileName(const DumpTask &task, wchar_t *fileName, size_t fileNameCount)
{
	if (task.sourceKind == DumpTaskSource::InputAssembler) {
		DX12BindingFrameExport::BuildIaBufferFileName(task.iaBuffer, fileName, fileNameCount);
		return;
	}

	const wchar_t *ext = task.isTexture ? L"dds" : L"buf";
	DX12FrameResourceBinding outputBinding;
	const DX12FrameResourceBinding *binding = &task.binding;
	wchar_t eventPrefix[32];
	if (task.sourceKind == DumpTaskSource::Resource) {
		FormatMetadataPrefix(task.binding.descriptorIndex,
			eventPrefix, ARRAYSIZE(eventPrefix));
	} else if (task.sourceKind == DumpTaskSource::Output) {
		outputBinding = DX12ResourceDumpCore::ResourceBindingFromOutputBinding(task.outputBinding);
		binding = &outputBinding;
		FormatEventPrefixWithSerial(
			task.outputBinding.drawId, task.outputBinding.dispatchId,
			task.outputBinding.eventSerial, eventPrefix, ARRAYSIZE(eventPrefix));
	} else {
		if (task.binding.bindSpace == "hunting_metadata")
			FormatMetadataPrefix(task.binding.descriptorIndex,
				eventPrefix, ARRAYSIZE(eventPrefix));
		else
			FormatEventPrefix(task.binding.drawId, task.binding.dispatchId,
				eventPrefix, ARRAYSIZE(eventPrefix));
	}
	wchar_t shaderPart[96];
	// Metadata texture discovery is not tied to a draw/dispatch PSO. Keeping
	// shader hashes out of those names avoids presenting missing context as a
	// bogus slot, which showed up as debug-fill cccccccc in hunting dumps.
	if (HasAnyShaderHash(binding->shaderInfo))
		BuildShaderHashNamePart(binding->shaderInfo, shaderPart, ARRAYSIZE(shaderPart));
	else
		shaderPart[0] = L'\0';
	char semanticSlot[32];
	if (DX12ResourceDumpPolicy::BuildSemanticSlot(*binding, semanticSlot, ARRAYSIZE(semanticSlot))) {
		swprintf_s(fileName, fileNameCount,
			L"%s-pso%llu%s-%S-%S-r%u-d%llu-%S_%p_o%llu_b%llu.%s",
			eventPrefix,
			static_cast<unsigned long long>(binding->psoIndex),
			shaderPart,
			DX12ResourceDumpPolicy::ShortBindSpace(binding->bindSpace),
			semanticSlot,
			binding->rootParameterIndex,
			static_cast<unsigned long long>(binding->descriptorIndex),
			DX12ResourceDumpPolicy::ShortDescriptorKind(binding->descriptor.kind),
			binding->descriptor.resource,
			static_cast<unsigned long long>(task.sourceOffset),
			static_cast<unsigned long long>(task.copyBytes),
			ext);
		return;
	}
	swprintf_s(fileName, fileNameCount,
		L"%s-pso%llu%s-%S-r%u-d%llu-%S_%p_o%llu_b%llu.%s",
		eventPrefix,
		static_cast<unsigned long long>(binding->psoIndex),
		shaderPart,
		DX12ResourceDumpPolicy::ShortBindSpace(binding->bindSpace),
		binding->rootParameterIndex,
		static_cast<unsigned long long>(binding->descriptorIndex),
		DX12ResourceDumpPolicy::ShortDescriptorKind(binding->descriptor.kind),
		binding->descriptor.resource,
		static_cast<unsigned long long>(task.sourceOffset),
		static_cast<unsigned long long>(task.copyBytes),
		ext);
}


static void BuildOutputDedupeKey(
	const DX12FrameOutputBinding &output, const DX12FrameResourceBinding &binding,
	char *key, size_t keyCount)
{
	if (!key || keyCount == 0)
		return;
	const char *kind = !binding.descriptor.kind.empty() ?
		binding.descriptor.kind.c_str() : output.kind.c_str();
	sprintf_s(key, keyCount, "output|%s|slot%u|%p|%llu|%llu|%u|%u",
		kind ? kind : "",
		output.slot,
		binding.descriptor.resource,
		static_cast<unsigned long long>(binding.descriptor.resourceOffset),
		static_cast<unsigned long long>(binding.descriptor.viewSize),
		binding.descriptor.viewFormat,
		binding.descriptor.viewDimension);
}

static void BuildDescriptorDedupeKey(
	const DX12DescriptorSummary &descriptor, char *key, size_t keyCount)
{
	if (!key || keyCount == 0)
		return;
	sprintf_s(key, keyCount, "%p|%llu|%llu|%u|%u",
		descriptor.resource,
		static_cast<unsigned long long>(descriptor.resourceOffset),
		static_cast<unsigned long long>(descriptor.viewSize),
		descriptor.viewFormat,
		descriptor.viewDimension);
}

static void BuildDescriptorResourceDedupeKey(
	const DX12DescriptorSummary &descriptor, char *key, size_t keyCount)
{
	if (!key || keyCount == 0)
		return;
	sprintf_s(key, keyCount, "%p|0|0|%u|%u",
		descriptor.resource,
		descriptor.hasResourceDesc ? static_cast<UINT>(descriptor.resourceDesc.Format) : 0,
		descriptor.hasResourceDesc ? static_cast<UINT>(descriptor.resourceDesc.Dimension) : 0);
}

static void BuildBindingDedupeKeyForCurrentMode(
	const DX12FrameResourceBinding &binding, char *key, size_t keyCount)
{
	if (!key || keyCount == 0)
		return;
	if (DX12RuntimeState::IsHuntingMode() &&
		binding.descriptor.hasResourceDesc &&
		DX12ResourceDumpPolicy::IsTextureResourceDimension(binding.descriptor.resourceDesc.Dimension)) {
		// Hunting is a resource discovery pass. The descriptor slot and view
		// still describe where the texture was seen, but one ID3D12Resource must
		// produce one readback, matching RenderDoc-style capture granularity.
		DX12ResourceDumpNaming::BuildDescriptorResourceDedupeKey(binding.descriptor, key, keyCount);
		return;
	}
	BuildDescriptorDedupeKey(binding.descriptor, key, keyCount);
}

static void BuildOutputDedupeKeyForCurrentMode(
	const DX12FrameOutputBinding &output, const DX12FrameResourceBinding &binding,
	char *key, size_t keyCount)
{
	if (!key || keyCount == 0)
		return;
	if (DX12RuntimeState::IsHuntingMode() &&
		binding.descriptor.hasResourceDesc &&
		DX12ResourceDumpPolicy::IsTextureResourceDimension(binding.descriptor.resourceDesc.Dimension)) {
		DX12ResourceDumpNaming::BuildDescriptorResourceDedupeKey(binding.descriptor, key, keyCount);
		return;
	}
	BuildOutputDedupeKey(output, binding, key, keyCount);
}

static void BuildResourceDedupeKey(
	const DX12ResourceSummary &resource, char *key, size_t keyCount)
{
	if (!key || keyCount == 0)
		return;
	sprintf_s(key, keyCount, "%p|0|0|%u|%u",
		resource.resource,
		resource.hasResourceDesc ? static_cast<UINT>(resource.resourceDesc.Format) : 0,
		resource.hasResourceDesc ? static_cast<UINT>(resource.resourceDesc.Dimension) : 0);
}

static const char *IaRoleName(const DX12FrameIaBufferBinding &buffer)
{
	if (!buffer.role.empty())
		return buffer.role.c_str();
	return "UNKNOWN";
}

static bool BuildIaBufferDedupeKey(
	const DX12FrameIaBufferBinding &buffer,
	const DX12BufferResourceSummary *resolvedResource,
	bool resolved,
	char *key, size_t keyCount)
{
	if (!key || keyCount == 0)
		return false;

	key[0] = '\0';
	const char *role = DX12ResourceDumpNaming::IaRoleName(buffer);
	int written = 0;
	if (resolved && resolvedResource && resolvedResource->resource) {
		written = sprintf_s(key, keyCount, "ia|%s|%p|%llu|%llu|%u|%u",
			role,
			resolvedResource->resource,
			static_cast<unsigned long long>(resolvedResource->resourceOffset),
			static_cast<unsigned long long>(buffer.viewSize),
			buffer.stride,
			buffer.format);
	} else {
		written = sprintf_s(key, keyCount, "ia|%s|0x%llx|%llu|%u|%u",
			role,
			static_cast<unsigned long long>(buffer.viewGpuVa),
			static_cast<unsigned long long>(buffer.viewSize),
			buffer.stride,
			buffer.format);
	}

	return written > 0 && key[0] != '\0';
}


};
