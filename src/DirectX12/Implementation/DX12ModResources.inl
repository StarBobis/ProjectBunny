static std::wstring ResolveResourcePath(
	const Bunny::ResourceConfig &config, const std::wstring &baseDir,
	bool rootFallback)
{
	if (config.filename.empty())
		return L"";

	wchar_t path[MAX_PATH];
	wcsncpy_s(path, config.filename.c_str(), _TRUNCATE);
	if (PathIsRelativeW(path)) {
		wchar_t combined[MAX_PATH];
		const std::wstring &base = rootFallback || config.sourceDir.empty() ? baseDir : config.sourceDir;
		wcsncpy_s(combined, base.c_str(), _TRUNCATE);
		PathAppendW(combined, config.filename.c_str());
		return combined;
	}
	return path;
}

static std::wstring ResolveResourcePathLocked(
	const Bunny::ResourceConfig &config, bool rootFallback)
{
	return ResolveResourcePath(config, gBaseDir, rootFallback);
}

static bool ResourceConfigLooksLikeBuffer(const Bunny::ResourceConfig &config)
{
	std::wstring type = Bunny::IniDocumentText::ToLower(Bunny::IniDocumentText::Trim(config.type));
	if (type.empty())
		return true;
	return type == L"buffer" ||
		type == L"structuredbuffer" ||
		type == L"appendstructuredbuffer" ||
		type == L"consumestructuredbuffer" ||
		type == L"byteaddressbuffer" ||
		type == L"rwbuffer" ||
		type == L"rwstructuredbuffer" ||
		type == L"rwbyteaddressbuffer";
}

static bool LoadResourceBytes(
	const Bunny::ResourceConfig &config, const std::wstring &baseDir,
	std::vector<unsigned char> *bytes, std::wstring *path)
{
	if (!bytes)
		return false;
	bytes->clear();

	std::wstring resolvedPath = ResolveResourcePath(config, baseDir, false);
	if (!resolvedPath.empty()) {
		if (path)
			*path = resolvedPath;
		if (ReadFileBytes(resolvedPath.c_str(), bytes))
			return true;

		if (!config.sourceDir.empty()) {
			std::wstring fallbackPath = ResolveResourcePath(config, baseDir, true);
			if (fallbackPath != resolvedPath) {
				if (path)
					*path = fallbackPath;
				const bool fallbackOk = ReadFileBytes(fallbackPath.c_str(), bytes);
				DX12LogManager::Fields("DX12FallbackPath",
					"\"kind\":\"resource_path_root\",\"api\":\"DX12ModRuntime::LoadResourceBytes\","
					"\"section\":\"%S\",\"primary\":\"%S\",\"fallback\":\"%S\",\"success\":%s",
					config.section.c_str(), resolvedPath.c_str(), fallbackPath.c_str(),
					fallbackOk ? "true" : "false");
				return fallbackOk;
			}
		}
		return false;
	}

	if (config.data.empty())
		return false;

	std::wstring data = Bunny::IniDocumentText::Trim(config.data);
	if (data.rfind(L"0x", 0) == 0 || data.rfind(L"0X", 0) == 0)
		data = data.substr(2);
	data.erase(std::remove_if(data.begin(), data.end(), iswspace), data.end());
	if (data.empty() || (data.size() % 2) != 0)
		return false;

	for (size_t i = 0; i < data.size(); i += 2) {
		wchar_t text[3] = {data[i], data[i + 1], 0};
		wchar_t *end = nullptr;
		unsigned long parsed = wcstoul(text, &end, 16);
		if (!end || *end)
			return false;
		bytes->push_back(static_cast<unsigned char>(parsed));
	}
	return !bytes->empty();
}

static bool LoadResourceBytesLocked(
	const Bunny::ResourceConfig &config, std::vector<unsigned char> *bytes, std::wstring *path)
{
	return LoadResourceBytes(config, gBaseDir, bytes, path);
}

static DX12LoadedResource *EnsureLoadedResourceForPreSkin(
	ID3D12Device *device, const std::wstring &name);

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

	static D3D12_SRV_DIMENSION InferViewDimensionFromDescriptor(
		const DX12DescriptorSummary &descriptor)
	{
		if (descriptor.viewDimension != 0)
			return static_cast<D3D12_SRV_DIMENSION>(descriptor.viewDimension);
		if (descriptor.hasSrvDesc &&
		    descriptor.srvDesc.ViewDimension != D3D12_SRV_DIMENSION_UNKNOWN)
			return descriptor.srvDesc.ViewDimension;
		if (descriptor.hasResourceDesc) {
			switch (descriptor.resourceDesc.Dimension) {
			case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
				return D3D12_SRV_DIMENSION_TEXTURE2D;
			case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
				return D3D12_SRV_DIMENSION_TEXTURE1D;
			case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
				return D3D12_SRV_DIMENSION_TEXTURE3D;
			case D3D12_RESOURCE_DIMENSION_BUFFER:
				return D3D12_SRV_DIMENSION_BUFFER;
			default:
				break;
			}
		}
		return D3D12_SRV_DIMENSION_UNKNOWN;
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
		DX12LogManager::Fields("DX12DrawSrvReplacement",
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

class DX12DdsPixelFormat
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

class DX12DdsHeader
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
	DX12DdsPixelFormat ddspf;
	uint32_t caps = 0;
	uint32_t caps2 = 0;
	uint32_t caps3 = 0;
	uint32_t caps4 = 0;
	uint32_t reserved2 = 0;
};

class DX12DdsHeaderDxt10
{
public:
	uint32_t dxgiFormat = 0;
	uint32_t resourceDimension = 0;
	uint32_t miscFlag = 0;
	uint32_t arraySize = 0;
	uint32_t miscFlags2 = 0;
};

class DX12DdsSubresourceInfo
{
public:
	UINT width = 0;
	UINT height = 0;
	UINT64 rowBytes = 0;
	UINT64 numRows = 0;
	UINT64 dataOffset = 0;
	UINT64 dataBytes = 0;
};

class DX12DdsTextureInfo
{
public:
	UINT width = 0;
	UINT height = 0;
	UINT mipLevels = 0;
	DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
	std::vector<DX12DdsSubresourceInfo> subresources;
};

static_assert(sizeof(DX12DdsPixelFormat) == 32, "DDS pixel format layout mismatch");
static_assert(sizeof(DX12DdsHeader) == 124, "DDS header layout mismatch");
static_assert(sizeof(DX12DdsHeaderDxt10) == 20, "DDS DX10 header layout mismatch");

class DX12DdsTextureLoadUtils
{
public:
	static uint32_t MakeFourCc(char a, char b, char c, char d)
	{
		return static_cast<uint32_t>(static_cast<unsigned char>(a)) |
			(static_cast<uint32_t>(static_cast<unsigned char>(b)) << 8) |
			(static_cast<uint32_t>(static_cast<unsigned char>(c)) << 16) |
			(static_cast<uint32_t>(static_cast<unsigned char>(d)) << 24);
	}

	static bool PathLooksLikeDds(const std::wstring &path)
	{
		std::wstring lower = Bunny::IniDocumentText::ToLower(path);
		return lower.size() >= 4 && lower.compare(lower.size() - 4, 4, L".dds") == 0;
	}

	static bool ResourceConfigLooksLikeTexture(const Bunny::ResourceConfig &config)
	{
		std::wstring type = Bunny::IniDocumentText::ToLower(Bunny::IniDocumentText::Trim(config.type));
		if (type == L"texture" || type == L"texture2d")
			return true;
		return DX12DdsTextureLoadUtils::PathLooksLikeDds(config.filename);
	}

	static bool IsBlockCompressed(DXGI_FORMAT format)
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
		case DXGI_FORMAT_R32G32B32A32_FLOAT:
		case DXGI_FORMAT_R32G32B32A32_UINT:
		case DXGI_FORMAT_R32G32B32A32_SINT:
			return 128;
		case DXGI_FORMAT_R32G32B32_FLOAT:
		case DXGI_FORMAT_R32G32B32_UINT:
		case DXGI_FORMAT_R32G32B32_SINT:
			return 96;
		case DXGI_FORMAT_R16G16B16A16_FLOAT:
		case DXGI_FORMAT_R16G16B16A16_UNORM:
		case DXGI_FORMAT_R16G16B16A16_UINT:
		case DXGI_FORMAT_R16G16B16A16_SNORM:
		case DXGI_FORMAT_R16G16B16A16_SINT:
		case DXGI_FORMAT_R32G32_FLOAT:
		case DXGI_FORMAT_R32G32_UINT:
		case DXGI_FORMAT_R32G32_SINT:
			return 64;
		case DXGI_FORMAT_R8G8B8A8_UNORM:
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		case DXGI_FORMAT_R8G8B8A8_UINT:
		case DXGI_FORMAT_R8G8B8A8_SNORM:
		case DXGI_FORMAT_R8G8B8A8_SINT:
		case DXGI_FORMAT_B8G8R8A8_UNORM:
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		case DXGI_FORMAT_B8G8R8X8_UNORM:
		case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
		case DXGI_FORMAT_R32_FLOAT:
		case DXGI_FORMAT_R32_UINT:
		case DXGI_FORMAT_R32_SINT:
		case DXGI_FORMAT_R16G16_FLOAT:
		case DXGI_FORMAT_R16G16_UNORM:
		case DXGI_FORMAT_R16G16_UINT:
		case DXGI_FORMAT_R16G16_SNORM:
		case DXGI_FORMAT_R16G16_SINT:
			return 32;
		case DXGI_FORMAT_R8G8_UNORM:
		case DXGI_FORMAT_R8G8_UINT:
		case DXGI_FORMAT_R8G8_SNORM:
		case DXGI_FORMAT_R8G8_SINT:
		case DXGI_FORMAT_R16_FLOAT:
		case DXGI_FORMAT_R16_UNORM:
		case DXGI_FORMAT_R16_UINT:
		case DXGI_FORMAT_R16_SNORM:
		case DXGI_FORMAT_R16_SINT:
		case DXGI_FORMAT_B5G6R5_UNORM:
		case DXGI_FORMAT_B5G5R5A1_UNORM:
		case DXGI_FORMAT_B4G4R4A4_UNORM:
			return 16;
		case DXGI_FORMAT_R8_UNORM:
		case DXGI_FORMAT_R8_UINT:
		case DXGI_FORMAT_R8_SNORM:
		case DXGI_FORMAT_R8_SINT:
		case DXGI_FORMAT_A8_UNORM:
			return 8;
		default:
			return 0;
		}
	}

	static bool GetSurfaceInfo(
		UINT width, UINT height, DXGI_FORMAT format,
		UINT64 *rowBytes, UINT64 *numRows, UINT64 *numBytes)
	{
		if (!width || !height || !rowBytes || !numRows || !numBytes)
			return false;
		if (DX12DdsTextureLoadUtils::IsBlockCompressed(format)) {
			const UINT64 blocksWide = (std::max<UINT64>)(1, (static_cast<UINT64>(width) + 3) / 4);
			const UINT64 blocksHigh = (std::max<UINT64>)(1, (static_cast<UINT64>(height) + 3) / 4);
			*rowBytes = blocksWide * DX12DdsTextureLoadUtils::BlockBytes(format);
			*numRows = blocksHigh;
			*numBytes = (*rowBytes) * (*numRows);
			return true;
		}
		const UINT bitsPerPixel = DX12DdsTextureLoadUtils::BitsPerPixel(format);
		if (!bitsPerPixel)
			return false;
		*rowBytes = (static_cast<UINT64>(width) * bitsPerPixel + 7) / 8;
		*numRows = height;
		*numBytes = (*rowBytes) * (*numRows);
		return true;
	}

	static DXGI_FORMAT FormatFromLegacyPixelFormat(const DX12DdsPixelFormat &format)
	{
		const uint32_t fourCcFlag = 0x00000004;
		const uint32_t rgbFlag = 0x00000040;
		const uint32_t luminanceFlag = 0x00020000;
		const uint32_t alphaFlag = 0x00000002;
		if ((format.flags & fourCcFlag) != 0) {
			if (format.fourCc == DX12DdsTextureLoadUtils::MakeFourCc('D', 'X', 'T', '1'))
				return DXGI_FORMAT_BC1_UNORM;
			if (format.fourCc == DX12DdsTextureLoadUtils::MakeFourCc('D', 'X', 'T', '3'))
				return DXGI_FORMAT_BC2_UNORM;
			if (format.fourCc == DX12DdsTextureLoadUtils::MakeFourCc('D', 'X', 'T', '5'))
				return DXGI_FORMAT_BC3_UNORM;
			if (format.fourCc == DX12DdsTextureLoadUtils::MakeFourCc('A', 'T', 'I', '1') ||
			    format.fourCc == DX12DdsTextureLoadUtils::MakeFourCc('B', 'C', '4', 'U'))
				return DXGI_FORMAT_BC4_UNORM;
			if (format.fourCc == DX12DdsTextureLoadUtils::MakeFourCc('B', 'C', '4', 'S'))
				return DXGI_FORMAT_BC4_SNORM;
			if (format.fourCc == DX12DdsTextureLoadUtils::MakeFourCc('A', 'T', 'I', '2') ||
			    format.fourCc == DX12DdsTextureLoadUtils::MakeFourCc('B', 'C', '5', 'U'))
				return DXGI_FORMAT_BC5_UNORM;
			if (format.fourCc == DX12DdsTextureLoadUtils::MakeFourCc('B', 'C', '5', 'S'))
				return DXGI_FORMAT_BC5_SNORM;
			if (format.fourCc == 36)
				return DXGI_FORMAT_R16G16B16A16_UNORM;
			if (format.fourCc == 110)
				return DXGI_FORMAT_R16G16B16A16_SNORM;
			if (format.fourCc == 111)
				return DXGI_FORMAT_R16_FLOAT;
			if (format.fourCc == 112)
				return DXGI_FORMAT_R16G16_FLOAT;
			if (format.fourCc == 113)
				return DXGI_FORMAT_R16G16B16A16_FLOAT;
			if (format.fourCc == 114)
				return DXGI_FORMAT_R32_FLOAT;
			if (format.fourCc == 115)
				return DXGI_FORMAT_R32G32_FLOAT;
			if (format.fourCc == 116)
				return DXGI_FORMAT_R32G32B32A32_FLOAT;
		}
		if ((format.flags & rgbFlag) != 0) {
			if (format.rgbBitCount == 32) {
				if (format.rBitMask == 0x000000ff && format.gBitMask == 0x0000ff00 &&
				    format.bBitMask == 0x00ff0000 && format.aBitMask == 0xff000000)
					return DXGI_FORMAT_R8G8B8A8_UNORM;
				if (format.rBitMask == 0x00ff0000 && format.gBitMask == 0x0000ff00 &&
				    format.bBitMask == 0x000000ff && format.aBitMask == 0xff000000)
					return DXGI_FORMAT_B8G8R8A8_UNORM;
				if (format.rBitMask == 0x00ff0000 && format.gBitMask == 0x0000ff00 &&
				    format.bBitMask == 0x000000ff && format.aBitMask == 0x00000000)
					return DXGI_FORMAT_B8G8R8X8_UNORM;
			}
			if (format.rgbBitCount == 16) {
				if (format.rBitMask == 0x0000f800 && format.gBitMask == 0x000007e0 &&
				    format.bBitMask == 0x0000001f)
					return DXGI_FORMAT_B5G6R5_UNORM;
				if (format.rBitMask == 0x00007c00 && format.gBitMask == 0x000003e0 &&
				    format.bBitMask == 0x0000001f && format.aBitMask == 0x00008000)
					return DXGI_FORMAT_B5G5R5A1_UNORM;
				if (format.rBitMask == 0x00000f00 && format.gBitMask == 0x000000f0 &&
				    format.bBitMask == 0x0000000f && format.aBitMask == 0x0000f000)
					return DXGI_FORMAT_B4G4R4A4_UNORM;
			}
		}
		if ((format.flags & luminanceFlag) != 0 && format.rgbBitCount == 8)
			return DXGI_FORMAT_R8_UNORM;
		if ((format.flags & alphaFlag) != 0 && format.rgbBitCount == 8)
			return DXGI_FORMAT_A8_UNORM;
		return DXGI_FORMAT_UNKNOWN;
	}

	static bool ParseDds(
		const std::vector<unsigned char> &bytes,
		DX12DdsTextureInfo *info,
		std::string *reason)
	{
		if (info)
			*info = DX12DdsTextureInfo();
		if (reason)
			reason->clear();
		if (!info) {
			if (reason)
				*reason = "missing_info";
			return false;
		}
		if (bytes.size() < sizeof(uint32_t) + sizeof(DX12DdsHeader)) {
			if (reason)
				*reason = "too_small";
			return false;
		}
		uint32_t magic = 0;
		memcpy(&magic, bytes.data(), sizeof(magic));
		if (magic != DX12DdsTextureLoadUtils::MakeFourCc('D', 'D', 'S', ' ')) {
			if (reason)
				*reason = "bad_magic";
			return false;
		}
		DX12DdsHeader header;
		memcpy(&header, bytes.data() + sizeof(uint32_t), sizeof(header));
		if (header.size != 124 || header.ddspf.size != 32 || !header.width || !header.height) {
			if (reason)
				*reason = "bad_header";
			return false;
		}
		const uint32_t cubemapFlag = 0x00000200;
		if ((header.caps2 & cubemapFlag) != 0 || header.depth > 1) {
			if (reason)
				*reason = "unsupported_dimension";
			return false;
		}

		size_t dataOffset = sizeof(uint32_t) + sizeof(DX12DdsHeader);
		DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
		if ((header.ddspf.flags & 0x00000004) != 0 &&
		    header.ddspf.fourCc == DX12DdsTextureLoadUtils::MakeFourCc('D', 'X', '1', '0')) {
			if (bytes.size() < dataOffset + sizeof(DX12DdsHeaderDxt10)) {
				if (reason)
					*reason = "missing_dx10_header";
				return false;
			}
			DX12DdsHeaderDxt10 dx10;
			memcpy(&dx10, bytes.data() + dataOffset, sizeof(dx10));
			dataOffset += sizeof(dx10);
			if (dx10.resourceDimension != 3 || dx10.arraySize != 1) {
				if (reason)
					*reason = "unsupported_dx10_dimension";
				return false;
			}
			format = static_cast<DXGI_FORMAT>(dx10.dxgiFormat);
		} else {
			format = DX12DdsTextureLoadUtils::FormatFromLegacyPixelFormat(header.ddspf);
		}
		if (format == DXGI_FORMAT_UNKNOWN) {
			if (reason)
				*reason = "unsupported_format";
			return false;
		}
		const UINT mipLevels = header.mipMapCount ? header.mipMapCount : 1;
		UINT width = header.width;
		UINT height = header.height;
		UINT64 cursor = static_cast<UINT64>(dataOffset);
		for (UINT mip = 0; mip < mipLevels; ++mip) {
			UINT64 rowBytes = 0;
			UINT64 numRows = 0;
			UINT64 numBytes = 0;
			if (!DX12DdsTextureLoadUtils::GetSurfaceInfo(width, height, format, &rowBytes, &numRows, &numBytes)) {
				if (reason)
					*reason = "surface_info_failed";
				return false;
			}
			if (cursor + numBytes > bytes.size()) {
				if (reason)
					*reason = "truncated_data";
				return false;
			}
			DX12DdsSubresourceInfo subresource;
			subresource.width = width;
			subresource.height = height;
			subresource.rowBytes = rowBytes;
			subresource.numRows = numRows;
			subresource.dataOffset = cursor;
			subresource.dataBytes = numBytes;
			info->subresources.push_back(subresource);
			cursor += numBytes;
			width = width > 1 ? width >> 1 : 1;
			height = height > 1 ? height >> 1 : 1;
		}
		info->width = header.width;
		info->height = header.height;
		info->mipLevels = mipLevels;
		info->format = format;
		return true;
	}

	static bool LoadTextureBytesForResource(
		const std::wstring &name,
		DX12LoadedResource *resource,
		std::vector<unsigned char> *bytes,
		std::wstring *path)
	{
		if (!bytes || !path)
			return false;
		bytes->clear();
		path->clear();
		if (resource && !resource->path.empty()) {
			*path = resource->path;
			if (ReadFileBytes(resource->path.c_str(), bytes))
				return true;
		}

		Bunny::ResourceConfig config;
		std::wstring baseDir;
		AcquireSRWLockShared(&gModLock);
		Bunny::ResourceMap::iterator configIt = gModResources.find(name);
		if (configIt != gModResources.end()) {
			config = configIt->second;
			baseDir = gBaseDir;
		}
		ReleaseSRWLockShared(&gModLock);
		if (config.section.empty())
			return false;
		return LoadResourceBytes(config, baseDir, bytes, path);
	}

	static DX12LoadedResource *EnsureTextureResourceRecord(
		const std::wstring &name)
	{
		if (name.empty())
			return nullptr;
		AcquireSRWLockExclusive(&gModLock);
		std::unordered_map<std::wstring, DX12LoadedResource>::iterator loadedIt =
			gLoadedResources.find(name);
		if (loadedIt != gLoadedResources.end()) {
			DX12LoadedResource *resource = loadedIt->second.failed ? nullptr : &loadedIt->second;
			ReleaseSRWLockExclusive(&gModLock);
			return resource;
		}
		Bunny::ResourceMap::iterator configIt = gModResources.find(name);
		if (configIt == gModResources.end() ||
		    !DX12DdsTextureLoadUtils::ResourceConfigLooksLikeTexture(configIt->second)) {
			ReleaseSRWLockExclusive(&gModLock);
			return nullptr;
		}
		DX12LoadedResource placeholder;
		placeholder.name = name;
		placeholder.path = ResolveResourcePathLocked(configIt->second, false);
		std::pair<std::unordered_map<std::wstring, DX12LoadedResource>::iterator, bool> inserted =
			gLoadedResources.emplace(name, placeholder);
		DX12LoadedResource *resource = inserted.first->second.failed ? nullptr : &inserted.first->second;
		ReleaseSRWLockExclusive(&gModLock);
		return resource;
	}

	static bool EnsureTextureSrv(
		ID3D12Device *device,
		ID3D12GraphicsCommandList *commandList,
		DX12LoadedResource *resource)
	{
		if (!device || !commandList || !resource)
			return false;
		if (resource->drawSrvTextureResource && resource->drawSrvCpu.ptr)
			return true;

		std::vector<unsigned char> bytes;
		std::wstring path;
		if (!DX12DdsTextureLoadUtils::LoadTextureBytesForResource(resource->name, resource, &bytes, &path)) {
			DX12LogManager::Fields("DX12DrawSrvReplacement",
				"stage=texture_load_fail,reason=read_failed,resource=%S,path=%S",
				resource->name.c_str(), path.c_str());
			return false;
		}
		DX12DdsTextureInfo info;
		std::string parseReason;
		if (!DX12DdsTextureLoadUtils::ParseDds(bytes, &info, &parseReason)) {
			DX12LogManager::Fields("DX12DrawSrvReplacement",
				"stage=texture_load_fail,reason=%s,resource=%S,path=%S,bytes=%zu",
				parseReason.c_str(), resource->name.c_str(), path.c_str(), bytes.size());
			return false;
		}

		D3D12_RESOURCE_DESC textureDesc = {};
		textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		textureDesc.Alignment = 0;
		textureDesc.Width = info.width;
		textureDesc.Height = info.height;
		textureDesc.DepthOrArraySize = 1;
		textureDesc.MipLevels = static_cast<UINT16>(info.mipLevels);
		textureDesc.Format = info.format;
		textureDesc.SampleDesc.Count = 1;
		textureDesc.SampleDesc.Quality = 0;
		textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

		D3D12_HEAP_PROPERTIES defaultHeap = {};
		defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
		defaultHeap.CreationNodeMask = 1;
		defaultHeap.VisibleNodeMask = 1;

		ID3D12Resource *texture = nullptr;
		HRESULT hr = device->CreateCommittedResource(
			&defaultHeap, D3D12_HEAP_FLAG_NONE, &textureDesc,
			D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texture));
		if (FAILED(hr) || !texture) {
			DX12LogManager::Fields("DX12DrawSrvReplacement",
				"stage=texture_load_fail,reason=create_texture_failed,hr=0x%lx,resource=%S",
				hr, resource->name.c_str());
			return false;
		}

		const UINT subresourceCount = static_cast<UINT>(info.subresources.size());
		std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts(subresourceCount);
		std::vector<UINT> rowCounts(subresourceCount);
		std::vector<UINT64> rowSizes(subresourceCount);
		UINT64 uploadBytes = 0;
		device->GetCopyableFootprints(
			&textureDesc, 0, subresourceCount, 0,
			layouts.data(), rowCounts.data(), rowSizes.data(), &uploadBytes);
		if (!uploadBytes) {
			texture->Release();
			DX12LogManager::Fields("DX12DrawSrvReplacement",
				"stage=texture_load_fail,reason=empty_upload_footprint,resource=%S",
				resource->name.c_str());
			return false;
		}

		D3D12_RESOURCE_DESC uploadDesc = {};
		uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		uploadDesc.Width = uploadBytes;
		uploadDesc.Height = 1;
		uploadDesc.DepthOrArraySize = 1;
		uploadDesc.MipLevels = 1;
		uploadDesc.Format = DXGI_FORMAT_UNKNOWN;
		uploadDesc.SampleDesc.Count = 1;
		uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

		D3D12_HEAP_PROPERTIES uploadHeap = {};
		uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
		uploadHeap.CreationNodeMask = 1;
		uploadHeap.VisibleNodeMask = 1;

		ID3D12Resource *upload = nullptr;
		hr = device->CreateCommittedResource(
			&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload));
		if (FAILED(hr) || !upload) {
			texture->Release();
			DX12LogManager::Fields("DX12DrawSrvReplacement",
				"stage=texture_load_fail,reason=create_upload_failed,hr=0x%lx,resource=%S,bytes=%llu",
				hr, resource->name.c_str(), static_cast<unsigned long long>(uploadBytes));
			return false;
		}

		void *mapped = nullptr;
		D3D12_RANGE readRange = {};
		hr = upload->Map(0, &readRange, &mapped);
		if (FAILED(hr) || !mapped) {
			upload->Release();
			texture->Release();
			DX12LogManager::Fields("DX12DrawSrvReplacement",
				"stage=texture_load_fail,reason=map_upload_failed,hr=0x%lx,resource=%S",
				hr, resource->name.c_str());
			return false;
		}
		memset(mapped, 0, static_cast<size_t>(uploadBytes));
		unsigned char *uploadBase = static_cast<unsigned char*>(mapped);
		for (UINT index = 0; index < subresourceCount; ++index) {
			const DX12DdsSubresourceInfo &subresource = info.subresources[index];
			const unsigned char *src = bytes.data() + subresource.dataOffset;
			unsigned char *dst = uploadBase + layouts[index].Offset;
			const UINT64 dstRowPitch = layouts[index].Footprint.RowPitch;
			for (UINT64 row = 0; row < subresource.numRows; ++row) {
				memcpy(
					dst + row * dstRowPitch,
					src + row * subresource.rowBytes,
					static_cast<size_t>(subresource.rowBytes));
			}
		}
		upload->Unmap(0, nullptr);

		ID3D12DescriptorHeap *srvHeap = nullptr;
		if (!DX12DrawTextureSrvViewUtils::CreateSrvHeap(device, &srvHeap)) {
			upload->Release();
			texture->Release();
			DX12LogManager::Fields("DX12DrawSrvReplacement",
				"stage=texture_load_fail,reason=create_srv_heap_failed,resource=%S",
				resource->name.c_str());
			return false;
		}

		D3D12_CPU_DESCRIPTOR_HANDLE srvCpu = srvHeap->GetCPUDescriptorHandleForHeapStart();

		for (UINT index = 0; index < subresourceCount; ++index) {
			D3D12_TEXTURE_COPY_LOCATION dst = {};
			dst.pResource = texture;
			dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			dst.SubresourceIndex = index;
			D3D12_TEXTURE_COPY_LOCATION src = {};
			src.pResource = upload;
			src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
			src.PlacedFootprint = layouts[index];
			commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
		}
		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = texture;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		barrier.Transition.StateAfter =
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
		commandList->ResourceBarrier(1, &barrier);

		resource->drawSrvTextureResource = texture;
		resource->drawSrvUploadResource = upload;
		resource->drawSrvHeap = srvHeap;
		resource->drawSrvCpu = srvCpu;
		resource->drawSrvFormat = info.format;
		resource->drawSrvWidth = info.width;
		resource->drawSrvHeight = info.height;
		resource->drawSrvMipLevels = info.mipLevels;
		resource->path = path;
		DX12DrawTextureSrvViewUtils::CreateTexture2DSrv(
			device, resource, info.format, srvCpu);
		if (!info.subresources.empty()) {
			const DX12DdsSubresourceInfo &topMip = info.subresources.front();
			uint32_t textureHash = 0;
			if (DX12TextureHashUtils::Texture2DTopMipHashFromRows(
					textureDesc,
					bytes.data() + topMip.dataOffset,
					topMip.rowBytes,
					topMip.rowBytes,
					topMip.numRows,
					&textureHash) &&
			    textureHash) {
				DX12CacheTextureHash(
					texture,
					textureHash,
					&textureDesc,
					"mod_resource_load");
			}
		}
		DX12LogManager::Fields("DX12DrawSrvReplacement",
			"stage=dds_loaded,resource=%S,path=%S,width=%u,height=%u,mips=%u,format=%u,uploadBytes=%llu",
			resource->name.c_str(), path.c_str(), info.width, info.height,
			info.mipLevels, static_cast<UINT>(info.format),
			static_cast<unsigned long long>(uploadBytes));
		return true;
	}

	static DX12LoadedResource *EnsureLoadedDrawTextureSrv(
		ID3D12Device *device,
		ID3D12GraphicsCommandList *commandList,
		const std::wstring &name)
	{
		if (!device || !commandList || name.empty())
			return nullptr;
		DX12LoadedResource *resource = EnsureLoadedResourceForPreSkin(device, name);
		if (!resource)
			resource = DX12DdsTextureLoadUtils::EnsureTextureResourceRecord(name);
		if (!resource)
			return nullptr;
		if (!DX12DdsTextureLoadUtils::EnsureTextureSrv(device, commandList, resource))
			return nullptr;
		return resource;
	}
};

static ID3D12Device *AcquireModDevice(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return nullptr;

	ID3D12Device *device = nullptr;
	if (SUCCEEDED(commandList->GetDevice(IID_PPV_ARGS(&device))) && device)
		return device;
	return nullptr;
}

// Fast path: use pre-cached device pointer to avoid per-call COM QI overhead.
// The caller must ensure cachedDevice remains valid (device outlives command list per DX12 spec).
static ID3D12Device *AcquireModDeviceFast(ID3D12GraphicsCommandList *commandList, ID3D12Device *cachedDevice)
{
	if (cachedDevice)
		return cachedDevice;
	return AcquireModDevice(commandList);
}

static void BuildLoadedBufferResource(
	ID3D12Device *device,
	const std::wstring &name,
	const Bunny::ResourceConfig &config,
	const std::wstring &baseDir,
	DX12ResidentResourceBuildResult *result)
{
	if (!result)
		return;
	result->name = name;
	result->resource.name = name;
	if (!device || name.empty()) {
		result->resource.failed = true;
		return;
	}

	std::vector<unsigned char> bytes;
	std::wstring path;
	if (!LoadResourceBytes(config, baseDir, &bytes, &path)) {
		result->resource.failed = true;
		result->resource.path = path;
		DX12LogManager::Fields("DX12ResourceLoad",
			"\"status\":\"failed\",\"section\":\"%S\",\"reason\":\"read_failed\",\"path\":\"%S\"",
			config.section.c_str(), path.c_str());
		wchar_t status[512];
		swprintf_s(status, L"DX12 mod warning: failed to read resource\n%ls\n%ls",
			config.section.c_str(), path.c_str());
		DX12OverlayStatusState::SetWarning(status);
		return;
	}

	UINT64 byteWidth = config.hasByteWidth ? config.byteWidth : bytes.size();
	if (byteWidth < bytes.size())
		byteWidth = bytes.size();
	if (byteWidth == 0) {
		result->resource.failed = true;
		result->resource.path = path;
		DX12LogManager::Fields("DX12ResourceLoad",
			"\"status\":\"failed\",\"section\":\"%S\",\"reason\":\"empty_resource\",\"path\":\"%S\"",
			config.section.c_str(), path.c_str());
		return;
	}

	D3D12_HEAP_PROPERTIES heap = {};
	heap.Type = D3D12_HEAP_TYPE_UPLOAD;
	heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	heap.CreationNodeMask = 1;
	heap.VisibleNodeMask = 1;

	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Width = byteWidth;
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_UNKNOWN;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	ID3D12Resource *resource = nullptr;
	HRESULT hr = device->CreateCommittedResource(
		&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr, IID_PPV_ARGS(&resource));
	if (FAILED(hr) || !resource) {
		result->resource.failed = true;
		result->resource.path = path;
		DX12LogManager::Fields("DX12ResourceLoad",
			"\"status\":\"failed\",\"section\":\"%S\",\"reason\":\"create_failed\",\"hr\":\"0x%lx\",\"bytes\":%llu",
			config.section.c_str(), hr, static_cast<unsigned long long>(byteWidth));
		wchar_t status[512];
		swprintf_s(status, L"DX12 mod error: failed to create resource\n%ls hr=0x%lx",
			config.section.c_str(), hr);
		DX12OverlayStatusState::SetError(status);
		return;
	}

	void *mapped = nullptr;
	D3D12_RANGE readRange = {};
	hr = resource->Map(0, &readRange, &mapped);
	if (SUCCEEDED(hr) && mapped) {
		memcpy(mapped, bytes.data(), bytes.size());
		if (byteWidth > bytes.size())
			memset(static_cast<unsigned char*>(mapped) + bytes.size(), 0, byteWidth - bytes.size());
		resource->Unmap(0, nullptr);
	} else {
		resource->Release();
		result->resource.failed = true;
		result->resource.path = path;
		DX12LogManager::Fields("DX12ResourceLoad",
			"\"status\":\"failed\",\"section\":\"%S\",\"reason\":\"map_failed\",\"hr\":\"0x%lx\"",
			config.section.c_str(), hr);
		wchar_t status[512];
		swprintf_s(status, L"DX12 mod error: failed to map resource\n%ls hr=0x%lx",
			config.section.c_str(), hr);
		DX12OverlayStatusState::SetError(status);
		return;
	}

	result->resource.resource = resource;
	result->resource.byteWidth = byteWidth;
	result->resource.stride = config.hasStride ? config.stride : 0;
	result->resource.format = ParseDx12ResourceFormat(config.format);
	result->resource.path = path;
	result->available = true;
	DX12LogManager::Fields("DX12ResourceLoad",
		"\"status\":\"loaded\",\"section\":\"%S\",\"name\":\"%S\",\"path\":\"%S\",\"bytes\":%llu,\"stride\":%u,\"format\":%u",
		config.section.c_str(), name.c_str(), path.c_str(),
		static_cast<unsigned long long>(byteWidth), result->resource.stride,
		static_cast<UINT>(result->resource.format));
}

static bool StoreLoadedResourceResultLocked(
	const std::wstring &name, UINT64 generation, DX12LoadedResource *loadedResource,
	ID3D12Resource **discardResource)
{
	if (discardResource)
		*discardResource = nullptr;
	if (!loadedResource)
		return false;
	if (gReloadGeneration != generation) {
		if (discardResource)
			*discardResource = loadedResource->resource;
		return false;
	}
	auto inserted = gLoadedResources.emplace(name, *loadedResource);
	if (!inserted.second) {
		if (discardResource)
			*discardResource = loadedResource->resource;
	}
	return inserted.first->second.resource && !inserted.first->second.failed;
}

static bool EnsureLoadedResourceForCommandList(ID3D12Device *device, const std::wstring &name)
{
	if (!device || name.empty())
		return false;

	Bunny::ResourceConfig config;
	std::wstring baseDir;
	UINT64 generation = 0;
	AcquireSRWLockExclusive(&gModLock);
	auto loaded = gLoadedResources.find(name);
	if (loaded != gLoadedResources.end()) {
		const bool available = loaded->second.resource && !loaded->second.failed;
		ReleaseSRWLockExclusive(&gModLock);
		return available;
	}
	auto configIt = gModResources.find(name);
	if (configIt == gModResources.end() || !ResourceConfigLooksLikeBuffer(configIt->second)) {
		ReleaseSRWLockExclusive(&gModLock);
		return false;
	}
	config = configIt->second;
	baseDir = gBaseDir;
	generation = gReloadGeneration;
	ReleaseSRWLockExclusive(&gModLock);

	DX12ResidentResourceBuildResult result;
	result.generation = generation;
	BuildLoadedBufferResource(device, name, config, baseDir, &result);

	AcquireSRWLockExclusive(&gModLock);
	ID3D12Resource *discardResource = nullptr;
	const bool available = StoreLoadedResourceResultLocked(
		name, generation, &result.resource, &discardResource);
	ReleaseSRWLockExclusive(&gModLock);
	if (discardResource)
		discardResource->Release();
	return available;
}

static DX12LoadedResource *EnsureLoadedResourceLocked(
	ID3D12Device *device, const std::wstring &name)
{
	if (!device || name.empty())
		return nullptr;

	auto loaded = gLoadedResources.find(name);
	if (loaded != gLoadedResources.end())
		return loaded->second.resource && !loaded->second.failed ? &loaded->second : nullptr;

	auto configIt = gModResources.find(name);
	if (configIt == gModResources.end())
		return nullptr;

	const Bunny::ResourceConfig &config = configIt->second;
	if (!ResourceConfigLooksLikeBuffer(config))
		return nullptr;

	std::vector<unsigned char> bytes;
	std::wstring path;
	if (!LoadResourceBytesLocked(config, &bytes, &path)) {
		DX12LoadedResource failedResource;
		failedResource.failed = true;
		failedResource.name = name;
		failedResource.path = path;
		gLoadedResources[name] = failedResource;
		DX12LogManager::Fields("DX12ResourceLoad",
			"\"status\":\"failed\",\"section\":\"%S\",\"reason\":\"read_failed\",\"path\":\"%S\"",
			config.section.c_str(), path.c_str());
		wchar_t status[512];
		swprintf_s(status, L"DX12 mod warning: failed to read resource\n%ls\n%ls",
			config.section.c_str(), path.c_str());
		DX12OverlayStatusState::SetWarning(status);
		return nullptr;
	}

	UINT64 byteWidth = config.hasByteWidth ? config.byteWidth : bytes.size();
	if (byteWidth < bytes.size())
		byteWidth = bytes.size();
	if (byteWidth == 0)
		return nullptr;

	D3D12_HEAP_PROPERTIES heap = {};
	heap.Type = D3D12_HEAP_TYPE_UPLOAD;
	heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	heap.CreationNodeMask = 1;
	heap.VisibleNodeMask = 1;

	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Width = byteWidth;
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_UNKNOWN;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	ID3D12Resource *resource = nullptr;
	HRESULT hr = device->CreateCommittedResource(
		&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr, IID_PPV_ARGS(&resource));
	if (FAILED(hr) || !resource) {
		DX12LoadedResource failedResource;
		failedResource.failed = true;
		failedResource.name = name;
		failedResource.path = path;
		gLoadedResources[name] = failedResource;
		DX12LogManager::Fields("DX12ResourceLoad",
			"\"status\":\"failed\",\"section\":\"%S\",\"reason\":\"create_failed\",\"hr\":\"0x%lx\",\"bytes\":%llu",
			config.section.c_str(), hr, static_cast<unsigned long long>(byteWidth));
		wchar_t status[512];
		swprintf_s(status, L"DX12 mod error: failed to create resource\n%ls hr=0x%lx",
			config.section.c_str(), hr);
		DX12OverlayStatusState::SetError(status);
		return nullptr;
	}

	void *mapped = nullptr;
	D3D12_RANGE readRange = {};
	hr = resource->Map(0, &readRange, &mapped);
	if (SUCCEEDED(hr) && mapped) {
		memcpy(mapped, bytes.data(), bytes.size());
		if (byteWidth > bytes.size())
			memset(static_cast<unsigned char*>(mapped) + bytes.size(), 0, byteWidth - bytes.size());
		resource->Unmap(0, nullptr);
	} else {
		resource->Release();
		DX12LoadedResource failedResource;
		failedResource.failed = true;
		failedResource.name = name;
		failedResource.path = path;
		gLoadedResources[name] = failedResource;
		DX12LogManager::Fields("DX12ResourceLoad",
			"\"status\":\"failed\",\"section\":\"%S\",\"reason\":\"map_failed\",\"hr\":\"0x%lx\"",
			config.section.c_str(), hr);
		wchar_t status[512];
		swprintf_s(status, L"DX12 mod error: failed to map resource\n%ls hr=0x%lx",
			config.section.c_str(), hr);
		DX12OverlayStatusState::SetError(status);
		return nullptr;
	}

	DX12LoadedResource loadedResource;
	loadedResource.resource = resource;
	loadedResource.byteWidth = byteWidth;
	loadedResource.stride = config.hasStride ? config.stride : 0;
	loadedResource.format = ParseDx12ResourceFormat(config.format);
	loadedResource.name = name;
	loadedResource.path = path;
	auto inserted = gLoadedResources.emplace(name, loadedResource);
	DX12LogManager::Fields("DX12ResourceLoad",
		"\"status\":\"loaded\",\"section\":\"%S\",\"name\":\"%S\",\"path\":\"%S\",\"bytes\":%llu,\"stride\":%u,\"format\":%u",
		config.section.c_str(), name.c_str(), path.c_str(),
		static_cast<unsigned long long>(byteWidth), loadedResource.stride,
		static_cast<UINT>(loadedResource.format));
	return &inserted.first->second;
}

static DX12LoadedResource *EnsureLoadedResourceForPreSkin(
	ID3D12Device *device, const std::wstring &name)
{
	if (!device || name.empty())
		return nullptr;

	Bunny::ResourceConfig config;
	std::wstring baseDir;
	AcquireSRWLockExclusive(&gModLock);
	auto loaded = gLoadedResources.find(name);
	if (loaded != gLoadedResources.end()) {
		DX12LoadedResource *resource =
			loaded->second.resource && !loaded->second.failed ? &loaded->second : nullptr;
		ReleaseSRWLockExclusive(&gModLock);
		return resource;
	}
	auto configIt = gModResources.find(name);
	if (configIt == gModResources.end() || !ResourceConfigLooksLikeBuffer(configIt->second)) {
		ReleaseSRWLockExclusive(&gModLock);
		return nullptr;
	}
	config = configIt->second;
	baseDir = gBaseDir;
	ReleaseSRWLockExclusive(&gModLock);

	std::vector<unsigned char> bytes;
	std::wstring path;
	if (!LoadResourceBytes(config, baseDir, &bytes, &path)) {
		DX12LoadedResource failedResource;
		failedResource.failed = true;
		failedResource.name = name;
		failedResource.path = path;
		AcquireSRWLockExclusive(&gModLock);
		auto inserted = gLoadedResources.emplace(name, failedResource);
		DX12LoadedResource *result =
			inserted.first->second.resource && !inserted.first->second.failed ?
			&inserted.first->second : nullptr;
		ReleaseSRWLockExclusive(&gModLock);
		DX12LogManager::Fields("DX12ResourceLoad",
			"\"status\":\"failed\",\"section\":\"%S\",\"reason\":\"read_failed\",\"path\":\"%S\"",
			config.section.c_str(), path.c_str());
		wchar_t status[512];
		swprintf_s(status, L"DX12 mod warning: failed to read resource\n%ls\n%ls",
			config.section.c_str(), path.c_str());
		DX12OverlayStatusState::SetWarning(status);
		return result;
	}

	UINT64 byteWidth = config.hasByteWidth ? config.byteWidth : bytes.size();
	if (byteWidth < bytes.size())
		byteWidth = bytes.size();
	if (byteWidth == 0)
		return nullptr;

	D3D12_HEAP_PROPERTIES heap = {};
	heap.Type = D3D12_HEAP_TYPE_UPLOAD;
	heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	heap.CreationNodeMask = 1;
	heap.VisibleNodeMask = 1;

	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Width = byteWidth;
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_UNKNOWN;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	ID3D12Resource *resource = nullptr;
	HRESULT hr = device->CreateCommittedResource(
		&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr, IID_PPV_ARGS(&resource));
	if (FAILED(hr) || !resource) {
		DX12LoadedResource failedResource;
		failedResource.failed = true;
		failedResource.name = name;
		failedResource.path = path;
		AcquireSRWLockExclusive(&gModLock);
		gLoadedResources.emplace(name, failedResource);
		ReleaseSRWLockExclusive(&gModLock);
		DX12LogManager::Fields("DX12ResourceLoad",
			"\"status\":\"failed\",\"section\":\"%S\",\"reason\":\"create_failed\",\"hr\":\"0x%lx\",\"bytes\":%llu",
			config.section.c_str(), hr, static_cast<unsigned long long>(byteWidth));
		wchar_t status[512];
		swprintf_s(status, L"DX12 mod error: failed to create resource\n%ls hr=0x%lx",
			config.section.c_str(), hr);
		DX12OverlayStatusState::SetError(status);
		return nullptr;
	}

	void *mapped = nullptr;
	D3D12_RANGE readRange = {};
	hr = resource->Map(0, &readRange, &mapped);
	if (SUCCEEDED(hr) && mapped) {
		memcpy(mapped, bytes.data(), bytes.size());
		if (byteWidth > bytes.size())
			memset(static_cast<unsigned char*>(mapped) + bytes.size(), 0, byteWidth - bytes.size());
		resource->Unmap(0, nullptr);
	} else {
		resource->Release();
		DX12LoadedResource failedResource;
		failedResource.failed = true;
		failedResource.name = name;
		failedResource.path = path;
		AcquireSRWLockExclusive(&gModLock);
		gLoadedResources.emplace(name, failedResource);
		ReleaseSRWLockExclusive(&gModLock);
		DX12LogManager::Fields("DX12ResourceLoad",
			"\"status\":\"failed\",\"section\":\"%S\",\"reason\":\"map_failed\",\"hr\":\"0x%lx\"",
			config.section.c_str(), hr);
		wchar_t status[512];
		swprintf_s(status, L"DX12 mod error: failed to map resource\n%ls hr=0x%lx",
			config.section.c_str(), hr);
		DX12OverlayStatusState::SetError(status);
		return nullptr;
	}

	DX12LoadedResource loadedResource;
	loadedResource.resource = resource;
	loadedResource.byteWidth = byteWidth;
	loadedResource.stride = config.hasStride ? config.stride : 0;
	loadedResource.format = ParseDx12ResourceFormat(config.format);
	loadedResource.name = name;
	loadedResource.path = path;

	AcquireSRWLockExclusive(&gModLock);
	auto inserted = gLoadedResources.emplace(name, loadedResource);
	if (!inserted.second)
		resource->Release();
	DX12LoadedResource *result =
		inserted.first->second.resource && !inserted.first->second.failed ?
		&inserted.first->second : nullptr;
	ReleaseSRWLockExclusive(&gModLock);

	if (inserted.second) {
		DX12LogManager::Fields("DX12ResourceLoad",
			"\"status\":\"loaded\",\"section\":\"%S\",\"name\":\"%S\",\"path\":\"%S\",\"bytes\":%llu,\"stride\":%u,\"format\":%u",
			config.section.c_str(), name.c_str(), path.c_str(),
			static_cast<unsigned long long>(byteWidth), loadedResource.stride,
			static_cast<UINT>(loadedResource.format));
	}
	return result;
}

static bool EnsureResourceUavLocked(
	ID3D12Device *device, ID3D12GraphicsCommandList *commandList,
	DX12LoadedResource *resource, UINT elementStride, UINT64 minByteWidth = 0,
	bool allowInactiveExplicitMatchCs = false,
	const std::unordered_set<std::wstring> *activePreSkinSections = nullptr)
{
	if (!device || !commandList || !resource || !resource->resource)
		return false;
	if (!allowInactiveExplicitMatchCs &&
	    (!activePreSkinSections ||
	     ResourceBlockedByInactiveExplicitMatchCsLocked(resource->name, *activePreSkinSections)))
		return false;
	if (!elementStride)
		elementStride = resource->stride ? resource->stride : 4;
	if (!elementStride)
		return false;
	const UINT64 uavByteWidth = (std::max)(resource->byteWidth, minByteWidth);
	if (resource->uavHeap && resource->uavResource &&
	    resource->uavByteWidth >= uavByteWidth &&
	    resource->uavStride == elementStride)
		return true;

	if (resource->uavResource) {
		RetirePreSkinResourceForCommandList(commandList, resource->uavResource);
		resource->uavResource = nullptr;
	}
	if (resource->uavHeap) {
		RetirePreSkinDescriptorHeap(resource->uavHeap);
		resource->uavHeap = nullptr;
	}
	resource->uavCpu = {};
	resource->uavGpu = {};
	resource->uavInitialized = false;
	resource->uavWritten = false;
	resource->uavValid = false;
	resource->uavValidPresent = -1;
	resource->uavByteWidth = 0;
	resource->uavStride = 0;
	resource->uavState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.NumDescriptors = 1;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	heapDesc.NodeMask = 0;

	ID3D12DescriptorHeap *heap = nullptr;
	HRESULT hr = device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&heap));
	if (FAILED(hr) || !heap) {
		DX12LogManager::DebugFields("DX12PreSkinningUav",
			"\"status\":\"failed\",\"reason\":\"create_heap_failed\",\"hr\":\"0x%lx\",\"resource\":\"%S\"",
			hr, resource->name.c_str());
		return false;
	}

	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
	heapProps.CreationNodeMask = 1;
	heapProps.VisibleNodeMask = 1;

	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Width = uavByteWidth;
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_UNKNOWN;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	ID3D12Resource *uavResource = nullptr;
	hr = device->CreateCommittedResource(
		&heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		nullptr, IID_PPV_ARGS(&uavResource));
	if (FAILED(hr) || !uavResource) {
		DX12LogManager::DebugFields("DX12PreSkinningUav",
			"\"status\":\"failed\",\"reason\":\"create_resource_failed\",\"hr\":\"0x%lx\",\"resource\":\"%S\",\"bytes\":%llu",
			hr, resource->name.c_str(), static_cast<unsigned long long>(uavByteWidth));
		heap->Release();
		return false;
	}

	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	uavDesc.Format = DXGI_FORMAT_UNKNOWN;
	uavDesc.Buffer.FirstElement = 0;
	uavDesc.Buffer.StructureByteStride = elementStride;
	uavDesc.Buffer.NumElements = static_cast<UINT>(
		(std::min)(uavByteWidth / elementStride, static_cast<UINT64>(UINT_MAX)));
	uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

	resource->uavCpu = heap->GetCPUDescriptorHandleForHeapStart();
	resource->uavGpu = heap->GetGPUDescriptorHandleForHeapStart();
	device->CreateUnorderedAccessView(uavResource, nullptr, &uavDesc, resource->uavCpu);
	resource->uavHeap = heap;
	resource->uavResource = uavResource;
	resource->uavByteWidth = uavByteWidth;
	resource->uavStride = elementStride;
	resource->uavInitialized = true;
	resource->uavState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	DX12LogManager::DebugFields("DX12PreSkinningUav",
		"\"status\":\"created\",\"resource\":\"%S\",\"bytes\":%llu,\"uavBytes\":%llu,\"stride\":%u,\"elements\":%u",
		resource->name.c_str(),
		static_cast<unsigned long long>(resource->byteWidth),
		static_cast<unsigned long long>(uavByteWidth),
		elementStride, uavDesc.Buffer.NumElements);
	return true;
}

static bool EnsureResourceSrvLocked(
	ID3D12Device *device, DX12LoadedResource *resource, UINT elementStride,
	UINT64 minByteWidth = 0)
{
	if (!device || !resource || !resource->resource)
		return false;
	if (!elementStride)
		elementStride = resource->stride ? resource->stride : 4;
	if (!elementStride)
		return false;
	const UINT64 srvByteWidth = (std::max)(resource->byteWidth, minByteWidth);
	if (resource->srvHeap && resource->srvCpu.ptr &&
	    resource->srvStride == elementStride && resource->srvByteWidth >= srvByteWidth)
		return true;

	if (resource->srvHeap) {
		resource->srvHeap->Release();
		resource->srvHeap = nullptr;
	}
	if (resource->srvResource) {
		resource->srvResource->Release();
		resource->srvResource = nullptr;
	}
	resource->srvCpu = {};
	resource->srvStride = 0;
	resource->srvByteWidth = 0;

	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.NumDescriptors = 1;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	heapDesc.NodeMask = 0;

	ID3D12DescriptorHeap *heap = nullptr;
	HRESULT hr = device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&heap));
	if (FAILED(hr) || !heap) {
		DX12LogManager::DebugFields("DX12PreSkinningSrv",
			"\"status\":\"failed\",\"reason\":\"create_heap_failed\",\"hr\":\"0x%lx\",\"resource\":\"%S\"",
			hr, resource->name.c_str());
		return false;
	}

	ID3D12Resource *srvBacking = resource->resource;
	if (srvByteWidth > resource->byteWidth) {
		D3D12_HEAP_PROPERTIES heapProps = {};
		heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
		heapProps.CreationNodeMask = 1;
		heapProps.VisibleNodeMask = 1;

		D3D12_RESOURCE_DESC desc = {};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		desc.Width = srvByteWidth;
		desc.Height = 1;
		desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.Format = DXGI_FORMAT_UNKNOWN;
		desc.SampleDesc.Count = 1;
		desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

		hr = device->CreateCommittedResource(
			&heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr, IID_PPV_ARGS(&srvBacking));
		if (FAILED(hr) || !srvBacking) {
			heap->Release();
			DX12LogManager::DebugFields("DX12PreSkinningSrv",
				"\"status\":\"failed\",\"reason\":\"create_padded_resource_failed\",\"hr\":\"0x%lx\",\"resource\":\"%S\",\"bytes\":%llu",
				hr, resource->name.c_str(), static_cast<unsigned long long>(srvByteWidth));
			return false;
		}

		void *dst = nullptr;
		D3D12_RANGE dstReadRange = {};
		hr = srvBacking->Map(0, &dstReadRange, &dst);
		if (FAILED(hr) || !dst) {
			srvBacking->Release();
			heap->Release();
			return false;
		}
		memset(dst, 0, static_cast<size_t>(srvByteWidth));
		void *src = nullptr;
		D3D12_RANGE srcReadRange = { 0, static_cast<SIZE_T>(resource->byteWidth) };
		hr = resource->resource->Map(0, &srcReadRange, &src);
		if (SUCCEEDED(hr) && src) {
			memcpy(dst, src, static_cast<size_t>(resource->byteWidth));
			D3D12_RANGE srcWrittenRange = {};
			resource->resource->Unmap(0, &srcWrittenRange);
		}
		srvBacking->Unmap(0, nullptr);
		if (FAILED(hr) || !src) {
			srvBacking->Release();
			heap->Release();
			DX12LogManager::DebugFields("DX12PreSkinningSrv",
				"\"status\":\"failed\",\"reason\":\"copy_padded_resource_failed\",\"hr\":\"0x%lx\",\"resource\":\"%S\"",
				hr, resource->name.c_str());
			return false;
		}
		resource->srvResource = srvBacking;
	}

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Buffer.FirstElement = 0;
	srvDesc.Buffer.NumElements = static_cast<UINT>(
		(std::min)(resource->byteWidth / elementStride, static_cast<UINT64>(UINT_MAX)));
	srvDesc.Buffer.StructureByteStride = elementStride;
	srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

	resource->srvCpu = heap->GetCPUDescriptorHandleForHeapStart();
	device->CreateShaderResourceView(srvBacking, &srvDesc, resource->srvCpu);
	resource->srvHeap = heap;
	resource->srvStride = elementStride;
	resource->srvByteWidth = srvByteWidth;
	DX12LogManager::DebugFields("DX12PreSkinningSrv",
		"\"status\":\"created\",\"resource\":\"%S\",\"bytes\":%llu,\"srvBytes\":%llu,\"stride\":%u,\"elements\":%u",
		resource->name.c_str(),
		static_cast<unsigned long long>(resource->byteWidth),
		static_cast<unsigned long long>(srvByteWidth),
		elementStride, srvDesc.Buffer.NumElements);
	return true;
}

static DX12LoadedResource *FindReplacementResourceForVlrLocked(
	ID3D12Device *device, const DX12VertexLimitRaiseConfig &vlr)
{
	DX12LoadedResource *best = nullptr;
	for (const std::wstring &name : gVlrResourceCandidates) {
		auto configIt = gModResources.find(name);
		if (configIt == gModResources.end())
			continue;
		const Bunny::ResourceConfig &config = configIt->second;
		if (config.hasStride && vlr.overrideByteStride &&
		    config.stride != vlr.overrideByteStride)
			continue;
		DX12LoadedResource *resource = EnsureLoadedResourceLocked(device, name);
		if (!resource || !resource->resource)
			continue;
		if (resource->byteWidth < vlr.overrideByteWidth)
			continue;
		if (!best || resource->byteWidth < best->byteWidth)
			best = resource;
	}
	return best;
}

static bool FindReplacementResourceNameForVlrLocked(
	const DX12VertexLimitRaiseConfig &vlr, std::wstring *resourceName)
{
	if (resourceName)
		resourceName->clear();
	for (const std::wstring &name : gVlrResourceCandidates) {
		auto configIt = gModResources.find(name);
		if (configIt == gModResources.end())
			continue;
		const Bunny::ResourceConfig &config = configIt->second;
		if (config.hasStride && vlr.overrideByteStride &&
		    config.stride != vlr.overrideByteStride)
			continue;
		if (resourceName)
			*resourceName = name;
		return true;
	}
	return false;
}

static bool ProducerMatchesAnyVlr(
	const DX12ComputeUavProducer &producer, uint64_t computeShaderHash,
	DX12VertexLimitRaiseConfig *match)
{
	for (const DX12VertexLimitRaiseConfig &vlr : gVertexLimitRaiseConfigs) {
		if (vlr.hasMatchCs && vlr.matchCs != computeShaderHash)
			continue;
		// Also skip if config doesn't match but vlr was derived from hash
		if (!vlr.hasMatchCs)
			continue;
		if (!ProducerMatchesReplacementTarget(producer, vlr))
			continue;
		if (match)
			*match = vlr;
		return true;
	}
	return false;
}

static bool ResourceMatchesVlrTarget(
	const DX12LoadedResource &resource, const DX12VertexLimitRaiseConfig &vlr)
{
	if (!vlr.overrideByteWidth)
		return false;
	if (resource.byteWidth < vlr.overrideByteWidth)
		return false;
	if (vlr.overrideByteStride && resource.stride &&
	    resource.stride != vlr.overrideByteStride)
		return false;
	return true;
}
