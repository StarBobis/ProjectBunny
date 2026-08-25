#pragma once

// DX12DdsTextureLoadUtils.h - part of DX12ModRuntimeInternal.h, included from it at the split position.
// Not meant to be included directly.


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

	// Upload channel for DDS textures. Mirrors DX11's immediate-context
	// upload semantics without the CPU stall: the copy and state transition
	// execute on a private queue, and the game's queue waits on the upload
	// fence at its next ExecuteCommandLists (GPU-side wait, no CPU block),
	// so the game's command list is never polluted with mod upload commands
	// (which previously added a large CopyTextureRegion + barrier into the
	// middle of the game's recording).
	// Created lazily on first upload and reused for the mod's lifetime.
	// A small allocator ring allows several uploads per frame to overlap:
	// slot i is only reused after its fence value completes.
	inline static constexpr UINT kUploadChannelSlots = 4;
	struct UploadChannelSlot
	{
		ID3D12CommandAllocator *allocator = nullptr;
		ID3D12GraphicsCommandList *list = nullptr;
		UINT64 busyUntilFence = 0; // 0 = never used / free
		bool everUsed = false;     // first use skips Reset (see below)
	};
	inline static SRWLOCK s_uploadLock = SRWLOCK_INIT;
	inline static ID3D12CommandQueue *s_uploadQueue = nullptr;
	inline static UploadChannelSlot s_uploadSlots[kUploadChannelSlots];
	inline static ID3D12Fence *s_uploadFence = nullptr;
	inline static UINT64 s_uploadFenceValue = 0;
	// Pending-upload publication for the game-queue wait (read by the
	// ExecuteCommandLists wrapper on the game thread under s_uploadLock).
	inline static UINT64 s_uploadPendingFenceValue = 0;
	inline static std::atomic<bool> s_uploadHasPending{false};
	// A queue only needs one GPU-side wait for a given upload fence value. Keep
	// the queues alive while the value is pending so a recycled COM address can
	// never make us incorrectly suppress a required wait.
	struct UploadQueueWait
	{
		ID3D12CommandQueue *queue = nullptr;
		UINT64 fenceValue = 0;
	};
	inline static std::vector<UploadQueueWait> s_uploadQueueWaits;

	static void ClearUploadQueueWaitsLocked()
	{
		for (UploadQueueWait &wait : s_uploadQueueWaits) {
			if (wait.queue)
				wait.queue->Release();
		}
		s_uploadQueueWaits.clear();
	}

	static bool EnsureUploadChannel(ID3D12Device *device)
	{
		if (s_uploadQueue && s_uploadFence &&
		    s_uploadSlots[0].allocator && s_uploadSlots[0].list)
			return true;
		if (!device)
			return false;
		D3D12_COMMAND_QUEUE_DESC queueDesc = {};
		queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		HRESULT hr = device->CreateCommandQueue(
			&queueDesc, IID_PPV_ARGS(&s_uploadQueue));
		if (FAILED(hr) || !s_uploadQueue) {
			DX12LogManager::Fields("DX12TextureUpload",
				"stage=ensure_channel_failed,step=create_queue,hr=0x%lx",
				hr);
			return false;
		}
		for (UINT i = 0; i < kUploadChannelSlots; ++i) {
			hr = device->CreateCommandAllocator(
				D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&s_uploadSlots[i].allocator));
			if (FAILED(hr) || !s_uploadSlots[i].allocator) {
				DX12LogManager::Fields("DX12TextureUpload",
					"stage=ensure_channel_failed,step=create_allocator,slot=%u,hr=0x%lx",
					i, hr);
				goto upload_channel_failed;
			}
			hr = device->CreateCommandList(
				0, D3D12_COMMAND_LIST_TYPE_DIRECT, s_uploadSlots[i].allocator,
				nullptr, IID_PPV_ARGS(&s_uploadSlots[i].list));
			if (FAILED(hr) || !s_uploadSlots[i].list) {
				DX12LogManager::Fields("DX12TextureUpload",
					"stage=ensure_channel_failed,step=create_list,slot=%u,hr=0x%lx",
					i, hr);
				goto upload_channel_failed;
			}
		}
		hr = device->CreateFence(
			0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&s_uploadFence));
		if (FAILED(hr) || !s_uploadFence) {
			DX12LogManager::Fields("DX12TextureUpload",
				"stage=ensure_channel_failed,step=create_fence,hr=0x%lx",
				hr);
			goto upload_channel_failed;
		}
		return true;

	upload_channel_failed:
		for (UINT i = 0; i < kUploadChannelSlots; ++i) {
			if (s_uploadSlots[i].list) {
				s_uploadSlots[i].list->Release();
				s_uploadSlots[i].list = nullptr;
			}
			if (s_uploadSlots[i].allocator) {
				s_uploadSlots[i].allocator->Release();
				s_uploadSlots[i].allocator = nullptr;
			}
		}
		if (s_uploadFence) {
			s_uploadFence->Release();
			s_uploadFence = nullptr;
		}
		if (s_uploadQueue) {
			s_uploadQueue->Release();
			s_uploadQueue = nullptr;
		}
		return false;
	}

	static bool WaitUploadFence(UINT64 fenceValue, DWORD timeoutMs)
	{
		if (!s_uploadFence)
			return false;
		if (s_uploadFence->GetCompletedValue() >= fenceValue)
			return true;
		HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		bool completed = false;
		if (event) {
			if (SUCCEEDED(s_uploadFence->SetEventOnCompletion(
					fenceValue, event))) {
				completed =
					WaitForSingleObject(event, timeoutMs) == WAIT_OBJECT_0;
			}
			CloseHandle(event);
		} else {
			completed = s_uploadFence->GetCompletedValue() >= fenceValue;
		}
		return completed;
	}

	static bool ExecuteTextureUpload(
		ID3D12Device *device,
		ID3D12Resource *texture,
		ID3D12Resource *upload,
		UINT subresourceCount,
		const std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> &layouts)
	{
		if (!device || !texture || !upload || subresourceCount == 0) {
			DX12LogManager::Fields("DX12TextureUpload",
				"stage=invalid_args,device=%d,texture=%d,upload=%d,subresources=%u",
				device != nullptr, texture != nullptr, upload != nullptr,
				subresourceCount);
			return false;
		}
		AcquireSRWLockExclusive(&s_uploadLock);
		if (!EnsureUploadChannel(device)) {
			ReleaseSRWLockExclusive(&s_uploadLock);
			return false;
		}

		// Select under the lock, but never wait for GPU progress while holding
		// it. The old DX11-style critical section made every concurrent upload
		// thread queue behind one fence wait. Release, wait, then reselect after
		// reacquiring so another producer can submit work in the meantime.
		UINT slotIndex = UINT_MAX;
		for (;;) {
			UINT oldestIndex = UINT_MAX;
			UINT64 oldestValue = UINT64_MAX;
			const UINT64 completedValue = s_uploadFence->GetCompletedValue();
			for (UINT i = 0; i < kUploadChannelSlots; ++i) {
				UploadChannelSlot &slot = s_uploadSlots[i];
				if (slot.busyUntilFence == 0 || completedValue >= slot.busyUntilFence) {
					slotIndex = i;
					break;
				}
				if (slot.busyUntilFence < oldestValue) {
					oldestValue = slot.busyUntilFence;
					oldestIndex = i;
				}
			}
			if (slotIndex != UINT_MAX)
				break;

			// The fence wait is deliberately outside s_uploadLock. No slot is
			// modified while unlocked, so reselecting after the wait is enough to
			// preserve allocator/list ownership correctness.
			ReleaseSRWLockExclusive(&s_uploadLock);
			const bool completed = WaitUploadFence(oldestValue, 5000);
			AcquireSRWLockExclusive(&s_uploadLock);
			if (!completed) {
				DX12LogManager::Fields("DX12TextureUpload",
					"stage=slot_wait_failed,fenceValue=%llu",
					static_cast<unsigned long long>(oldestValue));
				ReleaseSRWLockExclusive(&s_uploadLock);
				return false;
			}
			slotIndex = UINT_MAX;
		}

		UploadChannelSlot &slot = s_uploadSlots[slotIndex];
		if (slot.everUsed) {
			// Reuse: the previous list is closed and the fence check above
			// guarantees the GPU finished with it, so Reset is legal here.
			const HRESULT allocatorResetHr = slot.allocator->Reset();
			const HRESULT listResetHr = slot.list->Reset(slot.allocator, nullptr);
			if (FAILED(allocatorResetHr) || FAILED(listResetHr)) {
				DX12LogManager::Fields("DX12TextureUpload",
					"stage=reset_failed,slot=%u,allocatorHr=0x%lx,listHr=0x%lx",
					slotIndex, allocatorResetHr, listResetHr);
				ReleaseSRWLockExclusive(&s_uploadLock);
				return false;
			}
		} else {
			// First use: a freshly created command list is already in the
			// recording state and its allocator must NOT be reset while the
			// list is recording (Reset returns E_FAIL on strict drivers).
			// Record directly into the initial recording state.
			slot.everUsed = true;
		}
		for (UINT index = 0; index < subresourceCount; ++index) {
			D3D12_TEXTURE_COPY_LOCATION dst = {};
			dst.pResource = texture;
			dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			dst.SubresourceIndex = index;
			D3D12_TEXTURE_COPY_LOCATION src = {};
			src.pResource = upload;
			src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
			src.PlacedFootprint = layouts[index];
			slot.list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
		}
		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = texture;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		barrier.Transition.StateAfter =
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
		slot.list->ResourceBarrier(1, &barrier);
		const HRESULT closeHr = slot.list->Close();
		if (FAILED(closeHr)) {
			DX12LogManager::Fields("DX12TextureUpload",
				"stage=close_failed,slot=%u,hr=0x%lx",
				slotIndex, closeHr);
			ReleaseSRWLockExclusive(&s_uploadLock);
			return false;
		}

		// Async submit: no CPU wait. The game's queue waits on this fence
		// before its next ExecuteCommandLists, so the texture is in SRV
		// state before any game work that references it runs.
		ID3D12CommandList *lists[] = { slot.list };
		s_uploadQueue->ExecuteCommandLists(1, lists);
		const UINT64 fenceValue = ++s_uploadFenceValue;
		s_uploadQueue->Signal(s_uploadFence, fenceValue);
		slot.busyUntilFence = fenceValue;
		// A newer upload requires every game queue to establish a dependency on
		// the new fence value, even if it already waited for an older one.
		ClearUploadQueueWaitsLocked();
		s_uploadPendingFenceValue = fenceValue;
		s_uploadHasPending.store(true, std::memory_order_release);
		ReleaseSRWLockExclusive(&s_uploadLock);
		return true;
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
		// commandList is accepted for call-site symmetry but never used: the
		// upload is recorded and executed on the private upload channel, so a
		// null command list is fine (descriptor write-time patch path has no
		// command list in scope).
		if (!device || !resource)
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

		// Upload on the private channel (DX11 immediate-context equivalent)
		// instead of recording CopyTextureRegion + barrier into the game's
		// command list. The CPU waits for the upload to complete, so the
		// texture is in SRV state before the game ever submits.
		if (!ExecuteTextureUpload(
				device, texture, upload, subresourceCount, layouts)) {
			upload->Release();
			texture->Release();
			srvHeap->Release();
			DX12LogManager::Fields("DX12DrawSrvReplacement",
				"stage=texture_load_fail,reason=upload_failed,resource=%S",
				resource->name.c_str());
			return false;
		}

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

	// Device-only variant for the descriptor write-time patch path (plan step
	// 3.1), where no command list exists. EnsureTextureSrv uploads on the
	// private channel and only requires the device.
	static DX12LoadedResource *EnsureLoadedDrawTextureSrvDeviceOnly(
		ID3D12Device *device,
		const std::wstring &name)
	{
		if (!device || name.empty())
			return nullptr;
		DX12LoadedResource *resource =
			DX12DdsTextureLoadUtils::EnsureTextureResourceRecord(name);
		if (!resource)
			return nullptr;
		if (!DX12DdsTextureLoadUtils::EnsureTextureSrv(device, nullptr, resource))
			return nullptr;
		return resource;
	}
};
