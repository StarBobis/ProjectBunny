// DX12LoadedResources.cpp: mod resource upload channel, GPU resource construction and static replacement resolve.
// Split from DX12ModRuntime.cpp; implementation unit for its module.
#include "DX12ModRuntimeInternal.h"


// Exported upload-channel state for the ExecuteCommandLists wrapper: the game
// queue waits on the upload fence (GPU-side) before its next submit instead
// of the upload caller blocking on WaitForSingleObject.
bool DX12UploadChannelHasPendingWork()
{
	return DX12DdsTextureLoadUtils::s_uploadHasPending.load(
		std::memory_order_acquire);
}


void DX12UploadChannelWaitOnQueue(ID3D12CommandQueue *queue)
{
	if (!queue || !DX12UploadChannelHasPendingWork())
		return;
	AcquireSRWLockExclusive(&DX12DdsTextureLoadUtils::s_uploadLock);
	const UINT64 pendingValue = DX12DdsTextureLoadUtils::s_uploadPendingFenceValue;
	const bool hasPending = DX12DdsTextureLoadUtils::s_uploadHasPending.load(
		std::memory_order_relaxed);
	if (hasPending && pendingValue &&
	    DX12DdsTextureLoadUtils::s_uploadFence) {
		if (DX12DdsTextureLoadUtils::s_uploadFence->GetCompletedValue() >=
		    pendingValue) {
			// Upload landed; stop paying the Wait on later submits.
			DX12DdsTextureLoadUtils::s_uploadHasPending.store(
				false, std::memory_order_release);
			DX12DdsTextureLoadUtils::ClearUploadQueueWaitsLocked();
		} else {
			bool alreadyWaited = false;
			for (const DX12DdsTextureLoadUtils::UploadQueueWait &wait :
			     DX12DdsTextureLoadUtils::s_uploadQueueWaits) {
				if (wait.queue == queue && wait.fenceValue >= pendingValue) {
					alreadyWaited = true;
					break;
				}
			}
			if (!alreadyWaited && SUCCEEDED(queue->Wait(
					DX12DdsTextureLoadUtils::s_uploadFence, pendingValue))) {
				queue->AddRef();
				DX12DdsTextureLoadUtils::s_uploadQueueWaits.push_back(
					{ queue, pendingValue });
			}
		}
	}
	ReleaseSRWLockExclusive(&DX12DdsTextureLoadUtils::s_uploadLock);
}


ID3D12Device *AcquireModDevice(ID3D12GraphicsCommandList *commandList)
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
ID3D12Device *AcquireModDeviceFast(ID3D12GraphicsCommandList *commandList, ID3D12Device *cachedDevice)
{
	if (cachedDevice)
		return cachedDevice;
	return AcquireModDevice(commandList);
}


void BuildLoadedBufferResource(
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


bool StoreLoadedResourceResultLocked(
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


bool EnsureLoadedResourceForCommandList(ID3D12Device *device, const std::wstring &name)
{
	if (!device || name.empty())
		return false;

	Bunny::ResourceConfig config;
	std::wstring baseDir;
	UINT64 generation = 0;
	// Read-only fast path first: once loaded, this availability check runs
	// per resource per draw on every recording thread. A shared lock keeps
	// those threads off the global exclusive gModLock (DX11 checks loaded
	// resources without taking a global write lock).
	DX12ModLockTelemetryUtils::AcquireModShared(&gModLock);
	auto loaded = gLoadedResources.find(name);
	if (loaded != gLoadedResources.end()) {
		const bool available = loaded->second.resource && !loaded->second.failed;
		ReleaseSRWLockShared(&gModLock);
		return available;
	}
	ReleaseSRWLockShared(&gModLock);

	DX12ModLockTelemetryUtils::AcquireModExclusive(&gModLock);
	// Another thread may have loaded the resource between the two
	// acquisitions; re-check before doing the (expensive) load.
	loaded = gLoadedResources.find(name);
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

	DX12ModLockTelemetryUtils::AcquireModExclusive(&gModLock);
	ID3D12Resource *discardResource = nullptr;
	const bool available = StoreLoadedResourceResultLocked(
		name, generation, &result.resource, &discardResource);
	ReleaseSRWLockExclusive(&gModLock);
	if (discardResource)
		discardResource->Release();
	return available;
}


DX12LoadedResource *EnsureLoadedResourceForPreSkin(
	ID3D12Device *device, const std::wstring &name)
{
	if (!device || name.empty())
		return nullptr;

	Bunny::ResourceConfig config;
	std::wstring baseDir;
	// Read-only fast path first: gLoadedResources is queried on every pre-skin
	// apply, every frame. A shared lock lets concurrent recording threads and
	// the UI thread read it without serializing on the exclusive gModLock
	// (DX11 reads resource metadata without taking a global write lock).
	DX12ModLockTelemetryUtils::AcquireModShared(&gModLock);
	auto loaded = gLoadedResources.find(name);
	if (loaded != gLoadedResources.end()) {
		DX12LoadedResource *resource =
			loaded->second.resource && !loaded->second.failed ? &loaded->second : nullptr;
		ReleaseSRWLockShared(&gModLock);
		return resource;
	}
	ReleaseSRWLockShared(&gModLock);

	DX12ModLockTelemetryUtils::AcquireModExclusive(&gModLock);
	// Another thread may have loaded the resource while we held the shared
	// lock; re-check before doing the (expensive) load.
	loaded = gLoadedResources.find(name);
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
		DX12ModLockTelemetryUtils::AcquireModExclusive(&gModLock);
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
		DX12ModLockTelemetryUtils::AcquireModExclusive(&gModLock);
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
		DX12ModLockTelemetryUtils::AcquireModExclusive(&gModLock);
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

	DX12ModLockTelemetryUtils::AcquireModExclusive(&gModLock);
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


bool EnsureResourceUavLocked(
	ID3D12Device *device, ID3D12GraphicsCommandList *commandList,
	DX12LoadedResource *resource, UINT elementStride, UINT64 minByteWidth,
	bool allowInactiveExplicitMatchCs,
	const std::unordered_set<std::wstring> *activePreSkinSections)
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
	return true;
}


bool EnsureResourceSrvLocked(
	ID3D12Device *device, DX12LoadedResource *resource, UINT elementStride,
	UINT64 minByteWidth)
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
	return true;
}



bool FindReplacementResourceNameForVlrLocked(
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


bool ProducerMatchesAnyVlr(
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


bool ResourceMatchesVlrTarget(
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


void DX12NoteStaticTextureReplacementTablePublished()
{
	AcquireSRWLockExclusive(&gStaticTextureReplacementResolveLock);
	gStaticTextureReplacementResolveCache.clear();
	ReleaseSRWLockExclusive(&gStaticTextureReplacementResolveLock);
}


// Resolves the write-time replacement for an original texture hash. When
// sourceRecord is provided, the replacement SRV is made view-compatible with
// the descriptor the game originally wrote (mirrors the draw-time
// EnsureCompatibleSrv contract); otherwise the DDS default SRV is used.
// Returns true and fills outSrv on success; outResource receives the loaded
// mod resource when requested. This is deliberately non-static so the
// tracking TU (DX12ResourceTracking.cpp) can call it from the
// CreateShaderResourceView / CopyDescriptors hooks.
bool DX12ResolveStaticTextureReplacementSrv(
	ID3D12Device *device,
	uint32_t originalHash,
	const DescriptorRecord *sourceRecord,
	D3D12_CPU_DESCRIPTOR_HANDLE *outSrv,
	ID3D12Resource **outResource,
	UINT64 *outTableGeneration)
{
	if (outSrv)
		*outSrv = {};
	if (outResource)
		*outResource = nullptr;
	if (outTableGeneration)
		*outTableGeneration = 0;
	if (!device || !originalHash || !outSrv)
		return false;

	const std::shared_ptr<const DX12StaticTextureReplacementTable> table =
		LoadStaticTextureReplacementSnapshot();
	if (!table || !table->enabled || table->byHash.empty())
		return false;
	auto entryIt = table->byHash.find(originalHash);
	if (entryIt == table->byHash.end())
		return false;
	const DX12StaticTextureReplacementEntry &entry = entryIt->second;
	if (outTableGeneration)
		*outTableGeneration = table->reloadGeneration;
	if (entry.resourceName.empty())
		return false;

	// Device-only texture load: uploads execute on the private channel, so
	// this stays callable from descriptor-write hooks that have no command
	// list in scope.
	DX12LoadedResource *resource =
		DX12DdsTextureLoadUtils::EnsureLoadedDrawTextureSrvDeviceOnly(
			device, entry.resourceName);
	if (!resource)
		return false;

	if (sourceRecord) {
		DX12DescriptorSummary originalDescriptor = {};
		originalDescriptor.resource = sourceRecord->resource;
		originalDescriptor.resourceDesc = sourceRecord->resourceDesc;
		originalDescriptor.hasResourceDesc = sourceRecord->hasResourceDesc;
		originalDescriptor.hasDesc = sourceRecord->hasDesc;
		if (sourceRecord->hasDesc) {
			originalDescriptor.srvDesc = sourceRecord->srv;
			originalDescriptor.hasSrvDesc = true;
		}
		originalDescriptor.viewFormat =
			sourceRecord->hasDesc ?
				static_cast<UINT>(sourceRecord->srv.Format) :
				(sourceRecord->hasResourceDesc ?
					static_cast<UINT>(sourceRecord->resourceDesc.Format) : 0);
		originalDescriptor.viewDimension =
			sourceRecord->hasDesc ?
				static_cast<UINT>(sourceRecord->srv.ViewDimension) : 0;
		D3D12_CPU_DESCRIPTOR_HANDLE compatible =
			DX12DrawTextureSrvViewUtils::EnsureCompatibleSrv(
				device, resource, originalDescriptor, nullptr);
		if (!compatible.ptr)
			return false;
		*outSrv = compatible;
		if (outResource)
			*outResource = resource->drawSrvTextureResource;
		return true;
	}

	// Default-view path: cache the lookup per table generation.
	AcquireSRWLockShared(&gStaticTextureReplacementResolveLock);
	auto cached = gStaticTextureReplacementResolveCache.find(originalHash);
	if (cached != gStaticTextureReplacementResolveCache.end() &&
	    cached->second.tableGeneration == table->reloadGeneration &&
	    cached->second.hasDefaultSrv) {
		*outSrv = cached->second.defaultSrv;
		if (outResource)
			*outResource = resource->drawSrvTextureResource;
		ReleaseSRWLockShared(&gStaticTextureReplacementResolveLock);
		return true;
	}
	ReleaseSRWLockShared(&gStaticTextureReplacementResolveLock);

	if (!resource->drawSrvCpu.ptr)
		return false;
	AcquireSRWLockExclusive(&gStaticTextureReplacementResolveLock);
	DX12StaticTextureReplacementResolveCacheEntry &stored =
		gStaticTextureReplacementResolveCache[originalHash];
	stored.tableGeneration = table->reloadGeneration;
	stored.defaultSrv = resource->drawSrvCpu;
	stored.hasDefaultSrv = true;
	ReleaseSRWLockExclusive(&gStaticTextureReplacementResolveLock);

	*outSrv = resource->drawSrvCpu;
	if (outResource)
		*outResource = resource->drawSrvTextureResource;
	return true;
}


bool DX12StaticTextureReplacementAllDescriptorOverridesStatic()
{
	const std::shared_ptr<const DX12StaticTextureReplacementTable> table =
		LoadStaticTextureReplacementSnapshot();
	return table && table->enabled && table->allDescriptorOverridesStatic;
}


bool DX12HasStaticTextureReplacements()
{
	return InterlockedCompareExchange(
		&gHasStaticTextureReplacements, 0, 0) != 0;
}


UINT64 DX12StaticTextureReplacementGeneration()
{
	const std::shared_ptr<const DX12StaticTextureReplacementTable> table =
		LoadStaticTextureReplacementSnapshot();
	return table && table->enabled ? table->reloadGeneration : 0;
}


bool DX12HasStaticIaReplacements()
{
	// Plain volatile load: the flag is published with interlocked stores and
	// only ever read here, and this runs on every IA bind (the game binds
	// vertex/index buffers hundreds of times per frame), so the LOCK CMPXCHG
	// read-modify-write fence buys nothing.
	return gHasStaticIaReplacements != 0;
}


// Bind-time static IA replacement (plan step 3.2). Resolves the mod buffer
// for a bound IB/VB whose content hash matches an unconditional static
// geometry override, and builds the replacement view exactly the way the
// draw-time AppendResourceViewsForCommandList path does. The mod buffers
// are UPLOAD-heap resources in GENERIC_READ state (see
// EnsureLoadedResourceForPreSkin), so no barrier is required before binding.
// Returns false when the hash has no static replacement or the mod resource
// failed to load; the caller then binds the original view unchanged.
bool DX12ResolveStaticIaReplacement(
	ID3D12Device *device,
	bool indexBuffer,
	UINT vertexSlot,
	uint32_t contentHash,
	const D3D12_INDEX_BUFFER_VIEW *originalIndexView,
	D3D12_INDEX_BUFFER_VIEW *outIndexView,
	const D3D12_VERTEX_BUFFER_VIEW *originalVertexView,
	D3D12_VERTEX_BUFFER_VIEW *outVertexView)
{
	if (outIndexView)
		*outIndexView = {};
	if (outVertexView)
		*outVertexView = {};
	if (!device || !contentHash)
		return false;
	if (!indexBuffer && vertexSlot >= 32)
		return false;

	// TLS resolution cache. Within one reload generation the resolution of a
	// content hash to the mod buffer (GPU VA / byte width / stride / format)
	// is stable: the static IA table is published once per reload and the
	// loaded resources it points at are resident UPLOAD-heap buffers that are
	// only retired during the next reload (which also bumps
	// gReloadGeneration). Caching per thread keeps every IA bind off the
	// atomic<shared_ptr> snapshot load (MSVC spinlock + shared control-block
	// cache line) and off gModLock: one volatile generation read plus a map
	// lookup, instead of a snapshot load + hash find + shared lock per bind
	// per slot on every recording thread.
	struct DX12StaticIaResolvedView
	{
		bool resolved = false;
		D3D12_GPU_VIRTUAL_ADDRESS gpuVa = 0;
		UINT64 byteWidth = 0;
		UINT stride = 0;
		DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
	};
	static thread_local UINT64 tlsStaticIaGeneration = 0;
	static thread_local std::unordered_map<UINT64, DX12StaticIaResolvedView>
		tlsStaticIaCache;
	const UINT64 generation = DX12ModGetReloadGenerationFast();
	if (tlsStaticIaGeneration != generation) {
		tlsStaticIaCache.clear();
		tlsStaticIaGeneration = generation;
	}
	// IB entries share the key space with VB entries by using slot
	// 0xFFFFFFFF as the IB marker (real vertex slots are < 32).
	const UINT64 cacheKey = indexBuffer ?
		(0xFFFFFFFFull << 32) | contentHash :
		((static_cast<UINT64>(vertexSlot) << 32) | contentHash);
	auto applyCached = [&](const DX12StaticIaResolvedView &view) {
		if (indexBuffer) {
			if (!outIndexView)
				return false;
			outIndexView->BufferLocation = view.gpuVa;
			outIndexView->SizeInBytes = static_cast<UINT>(view.byteWidth);
			// Mirror the draw-time path: keep the mod resource's own R16/R32
			// format when declared, otherwise reuse the game's view format.
			outIndexView->Format =
				(view.format == DXGI_FORMAT_R16_UINT ||
				 view.format == DXGI_FORMAT_R32_UINT) ?
					view.format :
					(originalIndexView ? originalIndexView->Format :
						DXGI_FORMAT_R32_UINT);
			return true;
		}
		if (!outVertexView)
			return false;
		outVertexView->BufferLocation = view.gpuVa;
		outVertexView->SizeInBytes = static_cast<UINT>(view.byteWidth);
		outVertexView->StrideInBytes =
			view.stride ? view.stride :
				(originalVertexView ? originalVertexView->StrideInBytes : 0);
		return outVertexView->StrideInBytes != 0;
	};
	auto cached = tlsStaticIaCache.find(cacheKey);
	if (cached != tlsStaticIaCache.end()) {
		if (!cached->second.resolved)
			return false;
		return applyCached(cached->second);
	}

	DX12StaticIaResolvedView resolvedView;
	auto cacheAndReturn = [&](bool resolved) {
		// Bound the TLS map: a game can touch many distinct buffers within one
		// generation; clearing on overflow just re-resolves on next bind.
		constexpr size_t kMaxTlsStaticIaEntries = 1024;
		if (tlsStaticIaCache.size() >= kMaxTlsStaticIaEntries)
			tlsStaticIaCache.clear();
		tlsStaticIaCache.emplace(cacheKey, resolvedView);
		return resolved;
	};

	const std::shared_ptr<const DX12StaticIaReplacementTable> table =
		LoadStaticIaReplacementSnapshot();
	if (!table || !table->enabled)
		return false;

	const std::wstring *resourceName = nullptr;
	if (indexBuffer) {
		auto it = table->indexByHash.find(contentHash);
		if (it != table->indexByHash.end())
			resourceName = &it->second;
	} else {
		const uint64_t key =
			(static_cast<uint64_t>(vertexSlot) << 32) |
			static_cast<uint64_t>(contentHash);
		auto it = table->vertexByHashAndSlot.find(key);
		if (it != table->vertexByHashAndSlot.end())
			resourceName = &it->second;
	}
	if (!resourceName || resourceName->empty())
		return cacheAndReturn(false);

	DX12LoadedResource *resource =
		EnsureLoadedResourceForPreSkin(device, *resourceName);
	if (!resource || !resource->resource)
		return cacheAndReturn(false);

	const UINT64 byteWidth = resource->byteWidth;
	if (!byteWidth || byteWidth > UINT_MAX)
		return cacheAndReturn(false);

	resolvedView.resolved = true;
	resolvedView.gpuVa = resource->resource->GetGPUVirtualAddress();
	resolvedView.byteWidth = byteWidth;
	resolvedView.stride = resource->stride;
	resolvedView.format = resource->format;
	return cacheAndReturn(applyCached(resolvedView));
}


bool DX12StaticTextureReplacementContainsHash(uint32_t originalHash)
{
	if (!originalHash)
		return false;
	const std::shared_ptr<const DX12StaticTextureReplacementTable> table =
		LoadStaticTextureReplacementSnapshot();
	return table && table->enabled &&
		table->byHash.find(originalHash) != table->byHash.end();
}


void ReleaseLoadedResourcesLocked()
{
	for (auto &item : gLoadedResources) {
		DX12RetiredLoadedResource retired;
		retired.resource = item.second;
		retired.retirePresent = DX12RuntimeState::GetPresentCount();
		gRetiredLoadedResources.push_back(retired);
		item.second = DX12LoadedResource();
	}
	gLoadedResources.clear();
}


void MoveExpiredRetiredLoadedResourcesLocked(
	std::vector<DX12RetiredLoadedResource> *expired)
{
	if (!expired)
		return;
	expired->clear();
	if (gActiveLoadedResourceSnapshots != 0)
		return;
	const LONG present = DX12RuntimeState::GetPresentCount();
	auto it = gRetiredLoadedResources.begin();
	while (it != gRetiredLoadedResources.end()) {
		if (present - it->retirePresent < RetiredLoadedResourcePresentDelay) {
			++it;
			continue;
		}
		expired->push_back(*it);
		it = gRetiredLoadedResources.erase(it);
	}
}


void ReleaseLoadedResourceObjects(DX12LoadedResource *resource)
{
	if (!resource)
		return;
	if (resource->resource) {
		resource->resource->Release();
		resource->resource = nullptr;
	}
	if (resource->uavResource) {
		RetirePreSkinResource(resource->uavResource);
		resource->uavResource = nullptr;
	}
	if (resource->uavHeap) {
		RetirePreSkinDescriptorHeap(resource->uavHeap);
		resource->uavHeap = nullptr;
	}
	if (resource->srvResource) {
		resource->srvResource->Release();
		resource->srvResource = nullptr;
	}
	if (resource->srvHeap) {
		resource->srvHeap->Release();
		resource->srvHeap = nullptr;
	}
	if (resource->drawSrvTextureResource) {
		RetirePreSkinResource(resource->drawSrvTextureResource);
		resource->drawSrvTextureResource = nullptr;
	}
	if (resource->drawSrvUploadResource) {
		RetirePreSkinResource(resource->drawSrvUploadResource);
		resource->drawSrvUploadResource = nullptr;
	}
	if (resource->drawSrvHeap) {
		RetirePreSkinDescriptorHeap(resource->drawSrvHeap);
		resource->drawSrvHeap = nullptr;
	}
	for (std::map<UINT64, ID3D12DescriptorHeap*>::iterator heapIt =
		     resource->drawSrvCompatibleHeaps.begin();
	     heapIt != resource->drawSrvCompatibleHeaps.end(); ++heapIt) {
		if (heapIt->second)
			RetirePreSkinDescriptorHeap(heapIt->second);
	}
	resource->drawSrvCompatibleHeaps.clear();
	resource->drawSrvCompatibleCpus.clear();
	resource->drawSrvCpu = {};
}
