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
	std::wstring type = Bunny::IniDocumentUtils::ToLower(Bunny::IniDocumentUtils::Trim(config.type));
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
				DX12LogManager::JsonFunc("DX12FallbackPath",
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

	std::wstring data = Bunny::IniDocumentUtils::Trim(config.data);
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

static ID3D12Device *AcquireModDevice(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return nullptr;

	ID3D12Device *device = nullptr;
	if (SUCCEEDED(commandList->GetDevice(IID_PPV_ARGS(&device))) && device)
		return device;
	return nullptr;
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
	auto configIt = gResources.find(name);
	if (configIt == gResources.end() || !ResourceConfigLooksLikeBuffer(configIt->second)) {
		ReleaseSRWLockExclusive(&gModLock);
		return false;
	}
	config = configIt->second;
	baseDir = gBaseDir;
	generation = gReloadGeneration;
	ReleaseSRWLockExclusive(&gModLock);

	std::vector<unsigned char> bytes;
	std::wstring path;
	if (!LoadResourceBytes(config, baseDir, &bytes, &path)) {
		DX12LoadedResource failedResource;
		failedResource.failed = true;
		failedResource.name = name;
		failedResource.path = path;
		AcquireSRWLockExclusive(&gModLock);
		ID3D12Resource *discardResource = nullptr;
		const bool result = StoreLoadedResourceResultLocked(
			name, generation, &failedResource, &discardResource);
		ReleaseSRWLockExclusive(&gModLock);
		DX12LogManager::JsonFunc("DX12ResourceLoad",
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
		return false;

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
		ID3D12Resource *discardResource = nullptr;
		StoreLoadedResourceResultLocked(name, generation, &failedResource, &discardResource);
		ReleaseSRWLockExclusive(&gModLock);
		DX12LogManager::JsonFunc("DX12ResourceLoad",
			"\"status\":\"failed\",\"section\":\"%S\",\"reason\":\"create_failed\",\"hr\":\"0x%lx\",\"bytes\":%llu",
			config.section.c_str(), hr, static_cast<unsigned long long>(byteWidth));
		wchar_t status[512];
		swprintf_s(status, L"DX12 mod error: failed to create resource\n%ls hr=0x%lx",
			config.section.c_str(), hr);
		DX12OverlayStatusState::SetError(status);
		return false;
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
		ID3D12Resource *discardResource = nullptr;
		StoreLoadedResourceResultLocked(name, generation, &failedResource, &discardResource);
		ReleaseSRWLockExclusive(&gModLock);
		DX12LogManager::JsonFunc("DX12ResourceLoad",
			"\"status\":\"failed\",\"section\":\"%S\",\"reason\":\"map_failed\",\"hr\":\"0x%lx\"",
			config.section.c_str(), hr);
		wchar_t status[512];
		swprintf_s(status, L"DX12 mod error: failed to map resource\n%ls hr=0x%lx",
			config.section.c_str(), hr);
		DX12OverlayStatusState::SetError(status);
		return false;
	}

	DX12LoadedResource loadedResource;
	loadedResource.resource = resource;
	loadedResource.byteWidth = byteWidth;
	loadedResource.stride = config.hasStride ? config.stride : 0;
	loadedResource.format = ParseDx12ResourceFormat(config.format);
	loadedResource.name = name;
	loadedResource.path = path;
	AcquireSRWLockExclusive(&gModLock);
	ID3D12Resource *discardResource = nullptr;
	const bool result = StoreLoadedResourceResultLocked(
		name, generation, &loadedResource, &discardResource);
	ReleaseSRWLockExclusive(&gModLock);
	if (discardResource)
		discardResource->Release();
	if (result) {
		DX12LogManager::JsonFunc("DX12ResourceLoad",
			"\"status\":\"loaded\",\"section\":\"%S\",\"name\":\"%S\",\"path\":\"%S\",\"bytes\":%llu,\"stride\":%u,\"format\":%u",
			config.section.c_str(), name.c_str(), path.c_str(),
			static_cast<unsigned long long>(byteWidth), loadedResource.stride,
			static_cast<UINT>(loadedResource.format));
	}
	return result;
}

static DX12LoadedResource *EnsureLoadedResourceLocked(
	ID3D12Device *device, const std::wstring &name)
{
	if (!device || name.empty())
		return nullptr;

	auto loaded = gLoadedResources.find(name);
	if (loaded != gLoadedResources.end())
		return loaded->second.resource && !loaded->second.failed ? &loaded->second : nullptr;

	auto configIt = gResources.find(name);
	if (configIt == gResources.end())
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
		DX12LogManager::JsonFunc("DX12ResourceLoad",
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
		DX12LogManager::JsonFunc("DX12ResourceLoad",
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
		DX12LogManager::JsonFunc("DX12ResourceLoad",
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
	DX12LogManager::JsonFunc("DX12ResourceLoad",
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
	auto configIt = gResources.find(name);
	if (configIt == gResources.end() || !ResourceConfigLooksLikeBuffer(configIt->second)) {
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
		DX12LogManager::JsonFunc("DX12ResourceLoad",
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
		DX12LogManager::JsonFunc("DX12ResourceLoad",
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
		DX12LogManager::JsonFunc("DX12ResourceLoad",
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
		DX12LogManager::JsonFunc("DX12ResourceLoad",
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
		DX12LogManager::DebugJsonFunc("DX12PreSkinningUav",
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
		DX12LogManager::DebugJsonFunc("DX12PreSkinningUav",
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
	DX12LogManager::DebugJsonFunc("DX12PreSkinningUav",
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
		DX12LogManager::DebugJsonFunc("DX12PreSkinningSrv",
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
			DX12LogManager::DebugJsonFunc("DX12PreSkinningSrv",
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
			DX12LogManager::DebugJsonFunc("DX12PreSkinningSrv",
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
	DX12LogManager::DebugJsonFunc("DX12PreSkinningSrv",
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
		auto configIt = gResources.find(name);
		if (configIt == gResources.end())
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
		auto configIt = gResources.find(name);
		if (configIt == gResources.end())
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

