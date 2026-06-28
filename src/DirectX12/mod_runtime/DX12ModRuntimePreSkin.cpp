static UINT64 DescriptorViewSize(const DX12DescriptorSummary &descriptor)
{
	if (descriptor.viewSize)
		return descriptor.viewSize;
	if (descriptor.hasResourceDesc &&
	    descriptor.resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER &&
	    descriptor.resourceDesc.Width > descriptor.resourceOffset)
		return descriptor.resourceDesc.Width - descriptor.resourceOffset;
	return 0;
}

static void LogPreSkinSrvProbe(
	const char *status, const DX12CurrentComputeUavBinding *binding,
	const Bunny::TextureOverrideConfig *config, uint32_t srvRegister,
	UINT64 viewBytes, UINT elementStride)
{
#if defined(_DEBUG)
	if (!binding) {
		DX12LogManager::DebugJsonFunc("DX12PreSkinSrvProbe",
			"\"status\":\"%s\"", status ? status : "");
		return;
	}

	DX12LogManager::DebugJsonFunc("DX12PreSkinSrvProbe",
		"\"status\":\"%s\",\"section\":\"%S\",\"root\":%u,\"range\":%u,\"tRegister\":%u,\"space\":%u,\"offset\":%u,\"rootDescriptor\":%s,\"hasDescriptor\":%s,\"kind\":\"%s\",\"viewDimension\":%u,\"resource\":\"%p\",\"gpuVa\":\"0x%llx\",\"viewBytes\":%llu,\"stride\":%u,\"srvRegister\":%u,\"hasMappedResource\":%s",
		status ? status : "",
		config ? config->section.c_str() : L"",
		binding->rootParameterIndex, binding->rangeIndex,
		binding->shaderRegister, binding->registerSpace,
		binding->descriptorOffset,
		binding->rootDescriptor ? "true" : "false",
		binding->hasDescriptor ? "true" : "false",
		binding->descriptor.kind.c_str(),
		binding->descriptor.viewDimension,
		binding->descriptor.resource,
		static_cast<unsigned long long>(binding->descriptor.gpuVirtualAddress),
		static_cast<unsigned long long>(viewBytes),
		elementStride, srvRegister,
		(config && srvRegister != UINT_MAX &&
		 config->preSkinCsSrvResources.find(srvRegister) != config->preSkinCsSrvResources.end()) ? "true" : "false");
#else
	(void)status;
	(void)binding;
	(void)config;
	(void)srvRegister;
	(void)viewBytes;
	(void)elementStride;
#endif
}

static void LogPreSkinCbvProbe(
	ID3D12Device *device, ID3D12GraphicsCommandList *commandList,
	const DX12CurrentComputeUavBinding &cbv,
	const Bunny::TextureOverrideConfig &config)
{
#if defined(_DEBUG)
	(void)device;
	(void)commandList;

	UINT64 gpuVa = cbv.gpuVirtualAddress;
	UINT64 viewBytes = 0;
	ID3D12Resource *resource = nullptr;
	UINT64 resourceOffset = 0;
	UINT heapType = 0;
	bool hasHeapType = false;
	if (cbv.hasDescriptor && cbv.descriptor.kind == "CBV") {
		gpuVa = cbv.descriptor.gpuVirtualAddress;
		viewBytes = cbv.descriptor.viewSize;
		resource = cbv.descriptor.resource;
		resourceOffset = cbv.descriptor.resourceOffset;
		heapType = cbv.descriptor.resourceHeapType;
		hasHeapType = cbv.descriptor.hasResourceHeapType;
	} else if (gpuVa) {
		DX12BufferResourceSummary summary = {};
		if (DX12ResolveBufferResourceByGpuVa(gpuVa, 256, &summary)) {
			resource = summary.resource;
			resourceOffset = summary.resourceOffset;
			viewBytes = summary.viewSize;
			heapType = summary.resourceHeapType;
			hasHeapType = summary.hasResourceHeapType;
		}
	}

	UINT32 words[16] = {};
	bool mapped = false;
	HRESULT mapHr = E_FAIL;
	if (resource) {
		const UINT64 bytesToRead = (std::min<UINT64>)(sizeof(words),
			viewBytes ? viewBytes : sizeof(words));
		D3D12_RANGE readRange = {
			static_cast<SIZE_T>(resourceOffset),
			static_cast<SIZE_T>(resourceOffset + bytesToRead)
		};
		void *mappedPtr = nullptr;
		mapHr = resource->Map(0, &readRange, &mappedPtr);
		if (SUCCEEDED(mapHr) && mappedPtr) {
			memcpy(words, static_cast<const unsigned char*>(mappedPtr) + resourceOffset,
				static_cast<size_t>(bytesToRead));
			D3D12_RANGE writtenRange = {};
			resource->Unmap(0, &writtenRange);
			mapped = true;
		}
	}

	DX12LogManager::DebugJsonFunc("DX12PreSkinCbvProbe",
		"\"section\":\"%S\",\"root\":%u,\"range\":%u,\"bRegister\":%u,\"space\":%u,\"offset\":%u,\"rootDescriptor\":%s,\"hasDescriptor\":%s,\"resource\":\"%p\",\"gpuVa\":\"0x%llx\",\"resourceOffset\":%llu,\"viewBytes\":%llu,\"heapType\":%u,\"hasHeapType\":%s,\"mapped\":%s,\"mapHr\":\"0x%lx\",\"u32\":\"%08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x\",\"u64_0\":\"0x%016llx\",\"u64_1\":\"0x%016llx\",\"u64_2\":\"0x%016llx\",\"u64_3\":\"0x%016llx\"",
		config.section.c_str(),
		cbv.rootParameterIndex, cbv.rangeIndex,
		cbv.shaderRegister, cbv.registerSpace, cbv.descriptorOffset,
		cbv.rootDescriptor ? "true" : "false",
		cbv.hasDescriptor ? "true" : "false",
		resource,
		static_cast<unsigned long long>(gpuVa),
		static_cast<unsigned long long>(resourceOffset),
		static_cast<unsigned long long>(viewBytes),
		heapType,
		hasHeapType ? "true" : "false",
		mapped ? "true" : "false",
		mapHr,
		words[0], words[1], words[2], words[3],
		words[4], words[5], words[6], words[7],
		words[8], words[9], words[10], words[11],
		words[12], words[13], words[14], words[15],
		static_cast<unsigned long long>(
			(static_cast<UINT64>(words[1]) << 32) | words[0]),
		static_cast<unsigned long long>(
			(static_cast<UINT64>(words[3]) << 32) | words[2]),
		static_cast<unsigned long long>(
			(static_cast<UINT64>(words[5]) << 32) | words[4]),
		static_cast<unsigned long long>(
			(static_cast<UINT64>(words[7]) << 32) | words[6]));
#else
	(void)device;
	(void)commandList;
	(void)cbv;
	(void)config;
#endif
}

static UINT AlignCbvSize(UINT size)
{
	return (size + 255u) & ~255u;
}

static bool ReadPreSkinCbvBytesLocked(
	ID3D12Resource *resource, UINT64 sourceOffset, UINT viewBytes,
	std::vector<unsigned char> *bytes, HRESULT *hrOut)
{
	if (hrOut)
		*hrOut = E_FAIL;
	if (!resource || !viewBytes || !bytes)
		return false;

	const DX12PreSkinCbvReadCacheKey key = {
		resource, sourceOffset, viewBytes, DX12RuntimeState::GetPresentCount()
	};
	auto cached = gPreSkinCbvReadCache.find(key);
	if (cached != gPreSkinCbvReadCache.end()) {
		if (hrOut)
			*hrOut = cached->second.hr;
		if (FAILED(cached->second.hr))
			return false;
		*bytes = cached->second.bytes;
		return true;
	}

	DX12PreSkinCbvReadCacheValue value;
	value.bytes.assign(viewBytes, 0);
	D3D12_RANGE readRange = {
		static_cast<SIZE_T>(sourceOffset),
		static_cast<SIZE_T>(sourceOffset + viewBytes)
	};
	void *sourceMapped = nullptr;
	value.hr = resource->Map(0, &readRange, &sourceMapped);
	if (SUCCEEDED(value.hr) && sourceMapped) {
		memcpy(value.bytes.data(),
			static_cast<const unsigned char*>(sourceMapped) + sourceOffset,
			viewBytes);
		D3D12_RANGE writtenRange = {};
		resource->Unmap(0, &writtenRange);
	}

	if (gPreSkinCbvReadCache.size() > 128)
		gPreSkinCbvReadCache.clear();
	gPreSkinCbvReadCache[key] = value;
	if (hrOut)
		*hrOut = value.hr;
	if (FAILED(value.hr) || !sourceMapped)
		return false;
	*bytes = value.bytes;
	return true;
}

static bool PatchPreSkinVertexCountCbvLocked(
	ID3D12Device *device, const DX12CurrentComputeUavBinding &cbv,
	const Bunny::TextureOverrideConfig &config, UINT expectedOriginalVertexCount, UINT overrideVertexCount,
	D3D12_CPU_DESCRIPTOR_HANDLE dst,
	std::vector<ID3D12Resource*> *temporaryResources,
	UINT *originalVertexCountOut,
	UINT *overrideVertexCountOut)
{
	if (!device || !overrideVertexCount || !dst.ptr || !temporaryResources)
		return false;
	if (!cbv.hasDescriptor || cbv.rootDescriptor || cbv.descriptor.kind != "CBV" ||
	    !cbv.descriptor.resource)
		return false;

	const UINT64 sourceOffset = cbv.descriptor.resourceOffset;
	UINT viewBytes = cbv.descriptor.cbvSize;
	if (!viewBytes && cbv.descriptor.viewSize)
		viewBytes = static_cast<UINT>((std::min<UINT64>)(cbv.descriptor.viewSize, UINT_MAX));
	if (!viewBytes)
		viewBytes = 256;
	viewBytes = AlignCbvSize(viewBytes);

	std::vector<unsigned char> bytes;
	HRESULT hr = E_FAIL;
	if (!ReadPreSkinCbvBytesLocked(cbv.descriptor.resource, sourceOffset, viewBytes, &bytes, &hr)) {
#if defined(_DEBUG)
		DX12LogManager::DebugJsonFunc("DX12PreSkinCbvPatch",
			"\"status\":\"skip_map_failed\",\"section\":\"%S\",\"root\":%u,\"bRegister\":%u,\"hr\":\"0x%lx\"",
			config.section.c_str(), cbv.rootParameterIndex, cbv.shaderRegister, hr);
#endif
		return false;
	}

	std::vector<UINT> patchOffsets;
	UINT32 originalVertexCount = 0;
	if (expectedOriginalVertexCount) {
		for (UINT offset = 0; offset + sizeof(UINT32) <= bytes.size(); offset += sizeof(UINT32)) {
			UINT32 candidate = 0;
			memcpy(&candidate, bytes.data() + offset, sizeof(candidate));
			if (candidate == expectedOriginalVertexCount)
				patchOffsets.push_back(offset);
		}
		if (!patchOffsets.empty())
			originalVertexCount = expectedOriginalVertexCount;
	} else if (bytes.size() >= sizeof(UINT32)) {
		memcpy(&originalVertexCount, bytes.data(), sizeof(originalVertexCount));
		if (originalVertexCount && originalVertexCount != overrideVertexCount)
			patchOffsets.push_back(0);
	}
	if (patchOffsets.empty()) {
		UINT32 firstDword = 0;
		if (bytes.size() >= sizeof(firstDword))
			memcpy(&firstDword, bytes.data(), sizeof(firstDword));
#if defined(_DEBUG)
		DX12LogManager::DebugJsonFunc("DX12PreSkinCbvPatch",
			"\"status\":\"skip_count_mismatch\",\"section\":\"%S\",\"root\":%u,\"bRegister\":%u,\"old\":%u,\"expected\":%u,\"new\":%u,\"bytes\":%u",
			config.section.c_str(), cbv.rootParameterIndex, cbv.shaderRegister,
			firstDword, expectedOriginalVertexCount, overrideVertexCount, viewBytes);
#endif
		return false;
	}
	for (UINT offset : patchOffsets)
		memcpy(bytes.data() + offset, &overrideVertexCount, sizeof(overrideVertexCount));

	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
	heapProps.CreationNodeMask = 1;
	heapProps.VisibleNodeMask = 1;

	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Width = viewBytes;
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_UNKNOWN;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	ID3D12Resource *patchedResource = nullptr;
	hr = device->CreateCommittedResource(
		&heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr, IID_PPV_ARGS(&patchedResource));
	if (FAILED(hr) || !patchedResource)
		return false;

	void *patchedMapped = nullptr;
	D3D12_RANGE patchedReadRange = {};
	hr = patchedResource->Map(0, &patchedReadRange, &patchedMapped);
	if (FAILED(hr) || !patchedMapped) {
		patchedResource->Release();
		return false;
	}
	memcpy(patchedMapped, bytes.data(), viewBytes);
	patchedResource->Unmap(0, nullptr);

	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
	cbvDesc.BufferLocation = patchedResource->GetGPUVirtualAddress();
	cbvDesc.SizeInBytes = viewBytes;
	device->CreateConstantBufferView(&cbvDesc, dst);
	temporaryResources->push_back(patchedResource);
	if (originalVertexCountOut)
		*originalVertexCountOut = originalVertexCount;
	if (overrideVertexCountOut)
		*overrideVertexCountOut = overrideVertexCount;

#if defined(_DEBUG)
	char offsetText[256] = {};
	size_t offsetUsed = 0;
	for (size_t i = 0; i < patchOffsets.size(); ++i) {
		int written = sprintf_s(
			offsetText + offsetUsed, sizeof(offsetText) - offsetUsed,
			"%s%u", i ? "," : "", patchOffsets[i]);
		if (written <= 0)
			break;
		offsetUsed += static_cast<size_t>(written);
		if (offsetUsed >= sizeof(offsetText) - 1)
			break;
	}
	DX12LogManager::DebugJsonFunc("DX12PreSkinCbvPatch",
		"\"status\":\"patched\",\"section\":\"%S\",\"root\":%u,\"bRegister\":%u,\"old\":%u,\"new\":%u,\"bytes\":%u,\"offsets\":\"%s\",\"patches\":%zu",
		config.section.c_str(), cbv.rootParameterIndex, cbv.shaderRegister,
		originalVertexCount, overrideVertexCount, viewBytes, offsetText, patchOffsets.size());
#endif
	return true;
}

static uint32_t HashComputeBufferBinding(const DX12CurrentComputeUavBinding &binding)
{
	if (!binding.hasDescriptor || !binding.descriptor.resource)
		return 0;
	const DX12DescriptorSummary &descriptor = binding.descriptor;
	const bool bufferView =
		(descriptor.kind == "UAV" &&
		 descriptor.viewDimension == D3D12_UAV_DIMENSION_BUFFER) ||
		(descriptor.kind == "SRV" &&
		 descriptor.viewDimension == D3D12_SRV_DIMENSION_BUFFER);
	if (!bufferView)
		return 0;

	UINT64 fallbackGpuVa = descriptor.gpuVirtualAddress + descriptor.resourceOffset;
	if (!fallbackGpuVa)
		fallbackGpuVa = binding.gpuVirtualAddress;
	const UINT64 fallbackSize = DescriptorViewSize(descriptor);
	if (!fallbackGpuVa && !fallbackSize)
		return 0;

	return DX12HashDescriptorBufferView(&descriptor, fallbackGpuVa, fallbackSize);
}

static uint32_t HashComputeUavBinding(const DX12CurrentComputeUavBinding &uav)
{
	if (!uav.hasDescriptor || uav.descriptor.kind != "UAV" ||
	    uav.descriptor.viewDimension != D3D12_UAV_DIMENSION_BUFFER)
		return 0;
	return HashComputeBufferBinding(uav);
}

static bool FindUavHashTextureOverrideLocked(
	uint32_t hash, DX12UavHashTextureOverrideMatch *match)
{
	if (!hash)
		return false;
	auto indexed = gPreSkinUavHashTextureOverrideIndex.find(hash);
	if (indexed == gPreSkinUavHashTextureOverrideIndex.end())
		return false;

	for (size_t index : indexed->second) {
		if (index >= gPreSkinTextureOverrides.size())
			continue;
		const DX12PreSkinTextureOverrideCandidate &candidate =
			gPreSkinTextureOverrides[index];
		const Bunny::TextureOverrideConfig &config = candidate.config;
		if (config.hasMatchCs || config.vertexBufferResources.empty())
			continue;
		if (match) {
			match->config = config;
			match->hash = hash;
			match->resourceName = candidate.resourceName;
		}
		return true;
	}
	return false;
}

static bool FindPreSkinProducerByUavHashLocked(
	const std::vector<DX12CurrentComputeUavBinding> &uavs,
	DX12ComputeUavProducer *producer,
	DX12CurrentComputeUavBinding *binding,
	DX12UavHashTextureOverrideMatch *match)
{
	for (auto it = uavs.rbegin(); it != uavs.rend(); ++it) {
		if (!it->hasDescriptor || it->rootDescriptor || !it->descriptorIncrement ||
		    !it->tableCpuStart || !it->tableGpuStart.ptr)
			continue;
		if (!it->descriptor.resource || it->descriptor.viewSize == 0)
			continue;

		const uint32_t hash = HashComputeUavBinding(*it);
		DX12UavHashTextureOverrideMatch candidateMatch;
		const bool matched = FindUavHashTextureOverrideLocked(hash, &candidateMatch);
#if defined(_DEBUG)
		DX12LogManager::DebugJsonFunc("DX12PreSkinUavHashCandidate",
			"\"hash\":\"%08x\",\"matched\":%s,\"root\":%u,\"range\":%u,\"uRegister\":%u,\"space\":%u,\"viewBytes\":%llu,\"stride\":%u,\"resource\":\"%p\"",
			hash, matched ? "true" : "false",
			it->rootParameterIndex, it->rangeIndex, it->shaderRegister,
			it->registerSpace,
			static_cast<unsigned long long>(it->descriptor.viewSize),
			it->descriptor.structureByteStride,
			it->descriptor.resource);
#endif
		if (!matched)
			continue;

		DX12ComputeUavProducer candidate = {};
		candidate.rootParameterIndex = it->rootParameterIndex;
		candidate.rangeIndex = it->rangeIndex;
		candidate.shaderRegister = it->shaderRegister;
		candidate.registerSpace = it->registerSpace;
		candidate.descriptorOffset = it->descriptorOffset;
		candidate.rootDescriptor = it->rootDescriptor;
		candidate.gpuVirtualAddress = it->gpuVirtualAddress;
		candidate.hasDescriptor = it->hasDescriptor;
		candidate.descriptor = it->descriptor;
		if (producer)
			*producer = candidate;
		if (binding)
			*binding = *it;
		if (match)
			*match = candidateMatch;
		return true;
	}
	return false;
}

static void AppendComputeBindingHashList(
	const std::vector<DX12CurrentComputeUavBinding> &bindings,
	bool srvs, char *text, size_t textSize)
{
	if (!text || textSize == 0)
		return;
	text[0] = '\0';
	size_t used = 0;
	const size_t maxItems = srvs ? 8 : 6;
	for (size_t i = 0; i < bindings.size() && i < maxItems; ++i) {
		const DX12CurrentComputeUavBinding &binding = bindings[i];
		const uint32_t hash = srvs ? HashComputeBufferBinding(binding) : HashComputeUavBinding(binding);
		const UINT64 bytes = DescriptorViewSize(binding.descriptor);
		int written = sprintf_s(
			text + used, textSize - used,
			"%s%u/%u/%u/%u:%08x:%llu:%p",
			used ? ";" : "", binding.rootParameterIndex,
			binding.rangeIndex, binding.shaderRegister,
			binding.descriptorOffset, hash,
			static_cast<unsigned long long>(bytes),
			binding.descriptor.resource);
		if (written <= 0)
			break;
		used += static_cast<size_t>(written);
		if (used >= textSize - 1)
			break;
	}
}

static uint64_t HashComputeBindingListForProbe(
	const std::vector<DX12CurrentComputeUavBinding> &bindings, bool srvs)
{
	uint64_t key = srvs ? 0x91f5e20a7d1f0c3bull : 0x4db13be47c2d6a91ull;
	const size_t maxItems = srvs ? 8 : 6;
	for (size_t i = 0; i < bindings.size() && i < maxItems; ++i) {
		const DX12CurrentComputeUavBinding &binding = bindings[i];
		key = HashCombine64(key, binding.rootParameterIndex);
		key = HashCombine64(key, binding.rangeIndex);
		key = HashCombine64(key, binding.shaderRegister);
		key = HashCombine64(key, binding.registerSpace);
		key = HashCombine64(key, binding.descriptorOffset);
		key = HashCombine64(key, srvs ? HashComputeBufferBinding(binding) : HashComputeUavBinding(binding));
		key = HashCombine64(key, DescriptorViewSize(binding.descriptor));
	}
	return key;
}

static bool CanPatchPreSkinVertexCountCbvLocked(
	const DX12CurrentComputeUavBinding &cbv,
	UINT expectedOriginalVertexCount)
{
	if (!expectedOriginalVertexCount)
		return true;
	if (!cbv.hasDescriptor || cbv.rootDescriptor || cbv.descriptor.kind != "CBV" ||
	    !cbv.descriptor.resource)
		return false;

	const UINT64 sourceOffset = cbv.descriptor.resourceOffset;
	UINT viewBytes = cbv.descriptor.cbvSize;
	if (!viewBytes && cbv.descriptor.viewSize)
		viewBytes = static_cast<UINT>((std::min<UINT64>)(cbv.descriptor.viewSize, UINT_MAX));
	if (!viewBytes)
		viewBytes = 256;
	viewBytes = AlignCbvSize(viewBytes);

	std::vector<unsigned char> bytes;
	HRESULT hr = E_FAIL;
	if (!ReadPreSkinCbvBytesLocked(cbv.descriptor.resource, sourceOffset, viewBytes, &bytes, &hr))
		return false;

	for (UINT offset = 0; offset + sizeof(UINT32) <= bytes.size(); offset += sizeof(UINT32)) {
		UINT32 candidate = 0;
		memcpy(&candidate, bytes.data() + offset, sizeof(candidate));
		if (candidate == expectedOriginalVertexCount)
			return true;
	}
	return false;
}
static void AppendComputeCbvList(
	const std::vector<DX12CurrentComputeUavBinding> &cbvs, char *text, size_t textSize);
static void AppendComputeRootConstantsList(
	const std::vector<DX12CurrentRootConstants> &rootConstants, char *text, size_t textSize);
static uint64_t HashComputeCbvListForProbe(
	const std::vector<DX12CurrentComputeUavBinding> &cbvs);
static uint64_t HashComputeRootConstantsForProbe(
	const std::vector<DX12CurrentRootConstants> &rootConstants);
static uint64_t MakePreSkinMatchCsInputCacheKey(
	uint64_t csHash, const DX12PreSkinTextureOverrideCandidate &candidate,
	UINT64 uavViewBytes,
	const std::vector<DX12CurrentComputeUavBinding> &uavs,
	const std::vector<DX12CurrentComputeUavBinding> &srvs,
	const std::vector<DX12CurrentComputeUavBinding> &cbvs,
	const std::vector<DX12CurrentRootConstants> &rootConstants)
{
	uint64_t key = HashCombine64(gReloadGeneration, csHash);
	key = HashCombine64(key, candidate.hash);
	key = HashCombine64(key, uavViewBytes);
	key = HashCombine64(key, HashComputeBindingListForProbe(uavs, false));
	key = HashCombine64(key, HashComputeBindingListForProbe(srvs, true));
	key = HashCombine64(key, HashComputeCbvListForProbe(cbvs));
	key = HashCombine64(key, HashComputeRootConstantsForProbe(rootConstants));
	return key;
}

static void LogPreSkinMatchCsProbeLimited(
	uint64_t csHash, const DX12PreSkinTextureOverrideCandidate &candidate,
	UINT64 uavViewBytes, bool inputMatched,
	const std::vector<DX12CurrentComputeUavBinding> &uavs,
	const std::vector<DX12CurrentComputeUavBinding> &srvs,
	const std::vector<DX12CurrentComputeUavBinding> &cbvs,
	const std::vector<DX12CurrentRootConstants> &rootConstants)
{
	if (!inputMatched)
		return;
	const uint64_t probeKey = MakePreSkinMatchCsInputCacheKey(
		csHash, candidate, uavViewBytes, uavs, srvs, cbvs, rootConstants);
	bool emit = false;
	AcquireSRWLockExclusive(&gPreSkinMatchCsProbeLogLock);
	if (gPreSkinMatchCsProbeLogCount < 256 &&
	    gPreSkinMatchCsProbeKeys.insert(probeKey).second) {
		++gPreSkinMatchCsProbeLogCount;
		emit = true;
	}
	ReleaseSRWLockExclusive(&gPreSkinMatchCsProbeLogLock);
	if (!emit)
		return;
#if defined(_DEBUG)
	char uavText[512] = {};
	char srvText[768] = {};
	char cbvText[768] = {};
	char rootText[512] = {};
	AppendComputeBindingHashList(uavs, false, uavText, sizeof(uavText));
	AppendComputeBindingHashList(srvs, true, srvText, sizeof(srvText));
	AppendComputeCbvList(cbvs, cbvText, sizeof(cbvText));
	AppendComputeRootConstantsList(rootConstants, rootText, sizeof(rootText));
	DX12LogManager::JsonFunc("DX12PreSkinMatchCsProbe",
		"\"cs\":\"%016llx\",\"section\":\"%S\",\"targetHash\":\"%08x\",\"uavBytes\":%llu,\"matchUavBytes\":%llu,\"inputMatched\":%s,\"uavs\":\"%s\",\"srvs\":\"%s\",\"cbvs\":\"%s\",\"rootConstants\":\"%s\"",
		static_cast<unsigned long long>(csHash),
		candidate.config.originalSection.empty() ? candidate.config.section.c_str() : candidate.config.originalSection.c_str(),
		candidate.hash,
		static_cast<unsigned long long>(uavViewBytes),
		candidate.config.hasMatchUavBytes ? static_cast<unsigned long long>(candidate.config.matchUavBytes) : 0ull,
		inputMatched ? "true" : "false", uavText, srvText, cbvText, rootText);
#endif
}
static void AppendComputeCbvList(
	const std::vector<DX12CurrentComputeUavBinding> &cbvs, char *text, size_t textSize)
{
	if (!text || textSize == 0)
		return;
	text[0] = '\0';
	size_t used = 0;
	const size_t maxItems = 8;
	for (size_t i = 0; i < cbvs.size() && i < maxItems; ++i) {
		const DX12CurrentComputeUavBinding &cbv = cbvs[i];
		const UINT64 bytes = DescriptorViewSize(cbv.descriptor);
		int written = sprintf_s(
			text + used, textSize - used,
			"%s%u/%u/%u/%u:%llu:%p:0x%llx",
			used ? ";" : "", cbv.rootParameterIndex,
			cbv.rangeIndex, cbv.shaderRegister,
			cbv.descriptorOffset,
			static_cast<unsigned long long>(bytes),
			cbv.descriptor.resource,
			static_cast<unsigned long long>(cbv.descriptor.gpuVirtualAddress));
		if (written <= 0)
			break;
		used += static_cast<size_t>(written);
		if (used >= textSize - 1)
			break;
	}
}

static void AppendComputeRootConstantsList(
	const std::vector<DX12CurrentRootConstants> &rootConstants, char *text, size_t textSize)
{
	if (!text || textSize == 0)
		return;
	text[0] = '\0';
	size_t used = 0;
	const size_t maxRoots = 4;
	for (size_t i = 0; i < rootConstants.size() && i < maxRoots; ++i) {
		const DX12CurrentRootConstants &root = rootConstants[i];
		uint64_t valueHash = 14695981039346656037ull;
		UINT validCount = 0;
		for (UINT j = 0; j < 64; ++j) {
			if (!root.valueValid[j])
				continue;
			++validCount;
			valueHash = HashCombine64(valueHash, j);
			valueHash = HashCombine64(valueHash, root.values[j]);
		}
		int written = sprintf_s(
			text + used, textSize - used,
			"%s%u:%u:%016llx",
			used ? ";" : "", root.rootParameterIndex,
			validCount, static_cast<unsigned long long>(valueHash));
		if (written <= 0)
			break;
		used += static_cast<size_t>(written);
		if (used >= textSize - 1)
			break;
	}
}

static uint64_t HashComputeCbvListForProbe(
	const std::vector<DX12CurrentComputeUavBinding> &cbvs)
{
	uint64_t key = 0xa86f31b9dd3a7c55ull;
	const size_t maxItems = 8;
	for (size_t i = 0; i < cbvs.size() && i < maxItems; ++i) {
		const DX12CurrentComputeUavBinding &cbv = cbvs[i];
		key = HashCombine64(key, cbv.rootParameterIndex);
		key = HashCombine64(key, cbv.rangeIndex);
		key = HashCombine64(key, cbv.shaderRegister);
		key = HashCombine64(key, cbv.registerSpace);
		key = HashCombine64(key, cbv.descriptorOffset);
		key = HashCombine64(key, DescriptorViewSize(cbv.descriptor));
		key = HashCombine64(key, cbv.descriptor.resourceOffset);
		key = HashCombine64(key, cbv.descriptor.cbvSize);
	}
	return key;
}

static uint64_t HashComputeRootConstantsForProbe(
	const std::vector<DX12CurrentRootConstants> &rootConstants)
{
	uint64_t key = 0x53df92ecf49b31a7ull;
	const size_t maxRoots = 4;
	for (size_t i = 0; i < rootConstants.size() && i < maxRoots; ++i) {
		const DX12CurrentRootConstants &root = rootConstants[i];
		key = HashCombine64(key, root.rootParameterIndex);
		for (UINT j = 0; j < 64; ++j) {
			if (!root.valueValid[j])
				continue;
			key = HashCombine64(key, j);
			key = HashCombine64(key, root.values[j]);
		}
	}
	return key;
}

static bool FindComputeSrvByRegister(
	const std::vector<DX12CurrentComputeUavBinding> &srvs,
	uint32_t shaderRegister,
	const DX12CurrentComputeUavBinding **bindingOut,
	DX12PreSkinSrvHashResult *hashOut)
{
	for (const DX12CurrentComputeUavBinding &srv : srvs) {
		if (!srv.hasDescriptor || srv.descriptor.kind != "SRV" ||
		    srv.descriptor.viewDimension != D3D12_SRV_DIMENSION_BUFFER)
			continue;
		if (srv.shaderRegister != shaderRegister)
			continue;
		DX12PreSkinSrvHashResult hash = {};
		hash.descriptorHash = HashComputeBufferBinding(srv);
		hash.sourceOffset = srv.descriptor.resourceOffset ?
			srv.descriptor.resourceOffset : srv.descriptor.bufferViewOffset;
		hash.viewBytes = DescriptorViewSize(srv.descriptor);
		if (!hash.viewBytes && srv.descriptor.bufferViewBytes)
			hash.viewBytes = srv.descriptor.bufferViewBytes;
		hash.stride = srv.descriptor.structureByteStride;
		if (bindingOut)
			*bindingOut = &srv;
		if (hashOut)
			*hashOut = hash;
		return true;
	}
	return false;
}

static void LogPreSkinSrvHashProbeLimited(
	const Bunny::TextureOverrideConfig &config, uint32_t shaderRegister,
	uint32_t expectedHash, const DX12CurrentComputeUavBinding *binding,
	const DX12PreSkinSrvHashResult *hash, bool matched, const char *status)
{
#if defined(_DEBUG)
	if (!matched && status && (!strcmp(status, "descriptor_hash") || !strcmp(status, "hash_failed")))
		return;
	static SRWLOCK logLock = SRWLOCK_INIT;
	static std::unordered_set<uint64_t> logged;
	uint64_t key = 0x6e47f2cb0d45a31bull;
	key = HashCombine64(key, shaderRegister);
	key = HashCombine64(key, expectedHash);
	key = HashCombine64(key, hash ? hash->descriptorHash : 0);
	key = HashCombine64(key, binding ? reinterpret_cast<uint64_t>(binding->descriptor.resource) : 0);
	key = HashCombine64(key, binding ? binding->descriptor.resourceOffset : 0);
	key = HashCombine64(key, hash ? hash->viewBytes : 0);
	bool emit = false;
	AcquireSRWLockExclusive(&logLock);
	if (logged.size() > 512)
		logged.clear();
	emit = logged.insert(key).second;
	ReleaseSRWLockExclusive(&logLock);
	if (!emit)
		return;
	DX12LogManager::DebugJsonFunc("DX12PreSkinSrvHashProbe",
		"\"section\":\"%S\",\"status\":\"%s\",\"tRegister\":%u,\"expected\":\"%08x\",\"matched\":%s,\"descriptorHash\":\"%08x\",\"offset\":%llu,\"bytes\":%llu,\"stride\":%u,\"root\":%u,\"range\":%u,\"space\":%u,\"descriptorOffset\":%u,\"resource\":\"%p\"",
		config.originalSection.empty() ? config.section.c_str() : config.originalSection.c_str(),
		status ? status : "",
		shaderRegister, expectedHash, matched ? "true" : "false",
		hash ? hash->descriptorHash : 0,
		hash ? static_cast<unsigned long long>(hash->sourceOffset) : 0ull,
		hash ? static_cast<unsigned long long>(hash->viewBytes) : 0ull,
		hash ? hash->stride : 0,
		binding ? binding->rootParameterIndex : UINT_MAX,
		binding ? binding->rangeIndex : UINT_MAX,
		binding ? binding->registerSpace : UINT_MAX,
		binding ? binding->descriptorOffset : UINT_MAX,
		binding ? binding->descriptor.resource : nullptr);
#else
	(void)config;
	(void)shaderRegister;
	(void)expectedHash;
	(void)binding;
	(void)hash;
	(void)matched;
	(void)status;
#endif
}

static bool MatchComputeInputSrvsTextureOverrideLocked(
	const std::vector<DX12CurrentComputeUavBinding> &srvs,
	const Bunny::TextureOverrideConfig &config)
{
	if (config.preSkinMatchCsSrvHashes.empty())
		return false;
	for (const auto &expected : config.preSkinMatchCsSrvHashes) {
		const DX12CurrentComputeUavBinding *binding = nullptr;
		DX12PreSkinSrvHashResult actual = {};
		if (!FindComputeSrvByRegister(srvs, expected.first, &binding, &actual)) {
			LogPreSkinSrvHashProbeLimited(
				config, expected.first, expected.second, nullptr, nullptr,
				false, "missing_srv");
			return false;
		}
		const bool matched = actual.descriptorHash == expected.second;
		LogPreSkinSrvHashProbeLimited(
			config, expected.first, expected.second, binding, &actual,
			matched, actual.descriptorHash ? "descriptor_hash" : "hash_failed");
		if (!matched)
			return false;
	}
	return true;
}

static bool BuildPreSkinSrvReplacementBindingsTextureOverrideLocked(
	const std::vector<DX12CurrentComputeUavBinding> &srvs,
	const Bunny::TextureOverrideConfig &config,
	std::vector<DX12PreSkinSrvReplacementBinding> *replacements)
{
	if (!replacements)
		return false;
	replacements->clear();
	if (config.preSkinMatchCsSrvHashes.empty() ||
	    config.preSkinCsSrvResources.empty())
		return false;

	for (const auto &expected : config.preSkinMatchCsSrvHashes) {
		const DX12CurrentComputeUavBinding *binding = nullptr;
		DX12PreSkinSrvHashResult actual = {};
		if (!FindComputeSrvByRegister(srvs, expected.first, &binding, &actual))
			return false;
		const bool matched = actual.descriptorHash == expected.second;
		LogPreSkinSrvHashProbeLimited(
			config, expected.first, expected.second, binding, &actual,
			matched, actual.descriptorHash ? "descriptor_hash" : "hash_failed");
		if (!matched)
			return false;
	}

	for (const auto &replacement : config.preSkinCsSrvResources) {
		DX12PreSkinSrvReplacementBinding binding;
		binding.replaceSlot = replacement.first;
		binding.shaderRegister = replacement.first;
		binding.resource = replacement.second;
		replacements->push_back(std::move(binding));
	}
	return !replacements->empty();
}

static bool FindMatchCsTextureOverrideLocked(
	uint64_t csHash,
	const std::vector<DX12CurrentComputeUavBinding> &uavs,
	const std::vector<DX12CurrentComputeUavBinding> &srvs,
	const std::vector<DX12CurrentComputeUavBinding> &cbvs,
	const std::vector<DX12CurrentRootConstants> &rootConstants,
	DX12UavHashTextureOverrideMatch *match)
{
	if (!csHash)
		return false;
	auto indexed = gPreSkinMatchCsTextureOverrideIndex.find(csHash);
	if (indexed == gPreSkinMatchCsTextureOverrideIndex.end())
		return false;

	for (size_t index : indexed->second) {
		if (index >= gPreSkinTextureOverrides.size())
			continue;
		const DX12PreSkinTextureOverrideCandidate &candidate =
			gPreSkinTextureOverrides[index];
		const Bunny::TextureOverrideConfig &config = candidate.config;
		if (!IsComputePreSkinTextureOverride(config) || GetPreSkinCsHash(config) != csHash)
			continue;
		const uint64_t inputKey = MakePreSkinMatchCsInputCacheKey(
			csHash, candidate, 0, uavs, srvs, cbvs, rootConstants);
		bool inputMatched = false;
		auto cachedInput = gPreSkinMatchCsInputCache.find(inputKey);
		if (cachedInput != gPreSkinMatchCsInputCache.end()) {
			inputMatched = cachedInput->second;
		} else {
			inputMatched = MatchComputeInputSrvsTextureOverrideLocked(srvs, config);
			if (gPreSkinMatchCsInputCache.size() > 4096)
				gPreSkinMatchCsInputCache.clear();
			gPreSkinMatchCsInputCache[inputKey] = inputMatched;
		}
		LogPreSkinMatchCsProbeLimited(csHash, candidate, 0, inputMatched, uavs, srvs, cbvs, rootConstants);
		if (!inputMatched)
			continue;
		if (match) {
			match->config = config;
			match->hash = candidate.hash;
			BuildPreSkinSrvReplacementBindingsTextureOverrideLocked(
				srvs, config, &match->srvReplacements);
		}
		return true;
	}
	return false;
}

static bool HasMatchCsTextureOverrideLocked(uint64_t csHash)
{
	if (!csHash)
		return false;
	auto indexed = gPreSkinMatchCsTextureOverrideIndex.find(csHash);
	return indexed != gPreSkinMatchCsTextureOverrideIndex.end() &&
		!indexed->second.empty();
}

static void LogPreSkinMatchCsOutputProbeLimited(
	uint64_t csHash,
	const DX12UavHashTextureOverrideMatch &match,
	const std::vector<DX12CurrentComputeUavBinding> &uavs)
{
#if defined(_DEBUG)
	static SRWLOCK logLock = SRWLOCK_INIT;
	static std::unordered_set<uint64_t> logged;
	uint64_t key = HashCombine64(csHash, match.hash);
	key = HashCombine64(key, uavs.size());
	for (const DX12CurrentComputeUavBinding &uav : uavs) {
		key = HashCombine64(key, uav.rootParameterIndex);
		key = HashCombine64(key, uav.rangeIndex);
		key = HashCombine64(key, uav.shaderRegister);
		key = HashCombine64(key, uav.descriptorOffset);
		key = HashCombine64(key, reinterpret_cast<uint64_t>(uav.descriptor.resource));
		key = HashCombine64(key, uav.hasDescriptor ? 1 : 0);
		key = HashCombine64(key, uav.rootDescriptor ? 1 : 0);
		key = HashCombine64(key, uav.tableGpuStart.ptr);
	}
	bool emit = false;
	AcquireSRWLockExclusive(&logLock);
	if (logged.size() > 256)
		logged.clear();
	emit = logged.insert(key).second;
	ReleaseSRWLockExclusive(&logLock);
	if (!emit)
		return;
	char uavText[768] = {};
	AppendComputeBindingHashList(uavs, false, uavText, sizeof(uavText));
	DX12LogManager::DebugJsonFunc("DX12PreSkinMatchCsOutput",
		"\"status\":\"no_uav_output\",\"cs\":\"%016llx\",\"section\":\"%S\",\"targetHash\":\"%08x\",\"uavs\":%zu,\"uavList\":\"%s\"",
		static_cast<unsigned long long>(csHash),
		match.config.originalSection.empty() ? match.config.section.c_str() : match.config.originalSection.c_str(),
		match.hash, uavs.size(), uavText);
#else
	(void)csHash;
	(void)match;
	(void)uavs;
#endif
}

static bool IsPreSkinMatchCsUavCandidate(const DX12CurrentComputeUavBinding &binding)
{
	if (!binding.hasDescriptor || binding.rootDescriptor || !binding.descriptorIncrement ||
	    !binding.tableCpuStart || !binding.tableGpuStart.ptr)
		return false;
	if (!binding.descriptor.resource || binding.descriptor.viewSize == 0)
		return false;
	if (binding.descriptor.kind != "UAV" ||
	    binding.descriptor.viewDimension != D3D12_UAV_DIMENSION_BUFFER)
		return false;
	return true;
}

static bool FindPreSkinProducerByMatchCsLocked(
	const std::vector<DX12CurrentComputeUavBinding> &uavs,
	const std::vector<DX12CurrentComputeUavBinding> &srvs,
	const std::vector<DX12CurrentComputeUavBinding> &cbvs,
	const std::vector<DX12CurrentRootConstants> &rootConstants, uint64_t csHash,
	DX12ComputeUavProducer *producer,
	DX12CurrentComputeUavBinding *binding,
	DX12UavHashTextureOverrideMatch *match)
{
	DX12UavHashTextureOverrideMatch candidateMatch = {};
	if (!FindMatchCsTextureOverrideLocked(
		    csHash, uavs, srvs, cbvs, rootConstants, &candidateMatch))
		return false;

	const DX12CurrentComputeUavBinding *chosenBinding = nullptr;
	for (auto it = uavs.rbegin(); it != uavs.rend(); ++it) {
		if (IsPreSkinMatchCsUavCandidate(*it)) {
			chosenBinding = &(*it);
			break;
		}
	}
	if (!chosenBinding) {
		LogPreSkinMatchCsOutputProbeLimited(csHash, candidateMatch, uavs);
		return false;
	}

	DX12ComputeUavProducer candidate = {};
	candidate.rootParameterIndex = chosenBinding->rootParameterIndex;
	candidate.rangeIndex = chosenBinding->rangeIndex;
	candidate.shaderRegister = chosenBinding->shaderRegister;
	candidate.registerSpace = chosenBinding->registerSpace;
	candidate.descriptorOffset = chosenBinding->descriptorOffset;
	candidate.rootDescriptor = chosenBinding->rootDescriptor;
	candidate.gpuVirtualAddress = chosenBinding->gpuVirtualAddress;
	candidate.hasDescriptor = chosenBinding->hasDescriptor;
	candidate.descriptor = chosenBinding->descriptor;
	if (producer)
		*producer = candidate;
	if (binding)
		*binding = *chosenBinding;
	if (match)
		*match = candidateMatch;
	return true;
}

static UINT FindPreSkinUavElementStrideLocked(
	const DX12LoadedResource &resource,
	const std::unordered_set<std::wstring> &activePreSkinSections)
{
	for (const DX12VertexLimitRaiseConfig &vlr : gVertexLimitRaiseConfigs) {
		if (vlr.hasMatchCs &&
		    ResourceBlockedByInactiveExplicitMatchCsLocked(resource.name, activePreSkinSections))
			continue;
		if (!ResourceMatchesVlrTarget(resource, vlr))
			continue;
		if (vlr.uavByteStride)
			return vlr.uavByteStride;
		if (vlr.overrideByteStride)
			return vlr.overrideByteStride;
	}
	return resource.stride ? resource.stride : 4;
}

static bool FindPreSkinProducerLocked(
	const std::vector<DX12CurrentComputeUavBinding> &uavs,
	uint64_t computeShaderHash,
	DX12ComputeUavProducer *producer,
	DX12VertexLimitRaiseConfig *vlr)
{
	for (auto it = uavs.rbegin(); it != uavs.rend(); ++it) {
		if (!it->hasDescriptor || it->rootDescriptor || !it->descriptorIncrement ||
		    !it->tableCpuStart || !it->tableGpuStart.ptr)
			continue;
		if (!it->descriptor.resource || it->descriptor.viewSize == 0)
			continue;
		DX12ComputeUavProducer candidate = {};
		candidate.rootParameterIndex = it->rootParameterIndex;
		candidate.rangeIndex = it->rangeIndex;
		candidate.shaderRegister = it->shaderRegister;
		candidate.registerSpace = it->registerSpace;
		candidate.descriptorOffset = it->descriptorOffset;
		candidate.rootDescriptor = it->rootDescriptor;
		candidate.gpuVirtualAddress = it->gpuVirtualAddress;
		candidate.hasDescriptor = it->hasDescriptor;
		candidate.descriptor = it->descriptor;
		DX12VertexLimitRaiseConfig candidateVlr;
		if (!ProducerMatchesAnyVlr(candidate, computeShaderHash, &candidateVlr))
			continue;
		if (producer)
			*producer = candidate;
		if (vlr)
			*vlr = candidateVlr;
		return true;
	}
	return false;
}

static bool ApplyPreSkinDescriptorTablePatchLocked(
	ID3D12Device *device, ID3D12GraphicsCommandList *commandList,
	const DX12CurrentComputeUavBinding &uavBinding, const DX12ComputeUavProducer &producer,
	const Bunny::TextureOverrideConfig &config,
	const std::vector<DX12PreSkinSrvReplacementBinding> &srvReplacements,
	const std::vector<DX12CurrentComputeUavBinding> &srvs,
	const std::vector<DX12CurrentComputeUavBinding> &cbvs,
	const std::vector<DX12CurrentRootConstants> &rootConstants,
	const wchar_t *section, uint32_t triggerHash,
	UINT *originalVertexCountOut, UINT *overrideVertexCountOut)
{
	if (!device || !commandList || !uavBinding.hasDescriptor || !uavBinding.tableGpuStart.ptr)
		return false;
	auto logStage = [&config, section, triggerHash](const char *stage, UINT tables = 0, UINT descriptors = 0) {
#if defined(_DEBUG)
		static SRWLOCK logLock = SRWLOCK_INIT;
		static std::unordered_set<uint64_t> logged;
		uint64_t key = HashCombine64(triggerHash, reinterpret_cast<uint64_t>(stage));
		key = HashCombine64(key, tables);
		key = HashCombine64(key, descriptors);
		bool emit = false;
		AcquireSRWLockExclusive(&logLock);
		if (logged.size() > 512)
			logged.clear();
		emit = logged.insert(key).second;
		ReleaseSRWLockExclusive(&logLock);
		if (!emit)
			return;
		DX12LogManager::DebugJsonFunc("DX12PreSkinDescriptorPatch",
			"\"stage\":\"%s\",\"section\":\"%S\",\"triggerHash\":\"%08x\",\"tables\":%u,\"descriptors\":%u",
			stage ? stage : "", section ? section :
			(config.originalSection.empty() ? config.section.c_str() : config.originalSection.c_str()),
			triggerHash, tables, descriptors);
#else
		(void)stage;
		(void)tables;
		(void)descriptors;
#endif
	};
	logStage("enter");


	struct TableCopy
	{
		UINT root = 0;
		SIZE_T cpuStart = 0;
		D3D12_GPU_DESCRIPTOR_HANDLE gpuStart = {};
		ID3D12DescriptorHeap *heap = nullptr;
		UINT increment = 0;
		UINT count = 0;
		UINT tempOffset = 0;
	};
	static thread_local std::vector<TableCopy> tlsTables;
	std::vector<TableCopy> &tables = tlsTables;
	tables.clear();
	auto addTable = [&tables](const DX12CurrentComputeUavBinding &binding) {
		if (!binding.hasDescriptor || binding.rootDescriptor || !binding.tableCpuStart ||
		    !binding.tableGpuStart.ptr || !binding.descriptorIncrement || !binding.tableHeap)
			return;
		const UINT needed = (std::max)(
			binding.tableCopyCount ? binding.tableCopyCount : 1,
			binding.descriptorOffset + 1);
		for (TableCopy &table : tables) {
			if (table.root == binding.rootParameterIndex &&
			    table.cpuStart == binding.tableCpuStart &&
			    table.gpuStart.ptr == binding.tableGpuStart.ptr) {
				table.count = (std::max)(table.count, needed);
				return;
			}
		}
		TableCopy table;
		table.root = binding.rootParameterIndex;
		table.cpuStart = binding.tableCpuStart;
		table.gpuStart = binding.tableGpuStart;
		table.heap = binding.tableHeap;
		table.increment = binding.descriptorIncrement;
		table.count = needed;
		tables.push_back(table);
	};

	for (const DX12CurrentComputeUavBinding &cbv : cbvs)
		addTable(cbv);
	for (const DX12CurrentComputeUavBinding &srv : srvs)
		addTable(srv);
	addTable(uavBinding);
	if (tables.empty()) {
		logStage("no_tables");
		return false;
	}

	if (srvs.empty()) {
		LogPreSkinSrvProbe("no_srvs", nullptr, &config, UINT_MAX, 0, 0);
	}
	(void)rootConstants;

	const UINT64 abortCacheKey = MakePreSkinDescriptorPatchAbortCacheKey(
		config, srvReplacements, srvs, cbvs);
	if (gPreSkinDescriptorPatchAbortCache.find(abortCacheKey) !=
	    gPreSkinDescriptorPatchAbortCache.end()) {
		logStage("abort_cache_hit", 0, 0);
#if defined(_DEBUG)
		const LONG hitLogCount = InterlockedIncrement(&gPreSkinDescriptorAbortCacheHitLogCount);
		if (hitLogCount <= 32) {
			DX12LogManager::DebugJsonFunc("DX12PreSkinningApply",
				"\"status\":\"abort_cache_hit\",\"section\":\"%S\",\"triggerHash\":\"%08x\",\"tables\":%u,\"descriptors\":%u,\"cbvs\":%zu",
				section ? section : L"", triggerHash,
				0u, 0u, cbvs.size());
		}
#endif
		return false;
	}

	UINT originalVertexCount = 0;
	UINT overrideVertexCount = 0;
	bool requiresCountPatch = false;
	bool canPatchCount = false;
	for (const DX12CurrentComputeUavBinding &srv : srvs) {
		const DX12PreSkinSrvReplacementBinding *replacementBinding = nullptr;
		for (const DX12PreSkinSrvReplacementBinding &binding : srvReplacements) {
			if (binding.shaderRegister == srv.shaderRegister) {
				replacementBinding = &binding;
				break;
			}
		}
		if (!replacementBinding || !srv.hasDescriptor || srv.descriptor.kind != "SRV" ||
		    srv.descriptor.viewDimension != D3D12_SRV_DIMENSION_BUFFER)
			continue;

		const UINT64 srvByteWidth = DescriptorViewSize(srv.descriptor);
		DX12LoadedResource *resource = EnsureLoadedResourceForPreSkin(device, replacementBinding->resource);
		const UINT elementStride =
			srv.descriptor.structureByteStride ? srv.descriptor.structureByteStride :
			(resource ? resource->stride : 0);
		if (!resource || !elementStride)
			continue;

		const UINT srvOriginalVertexCount = static_cast<UINT>(
			(std::min)(srvByteWidth / elementStride, static_cast<UINT64>(UINT_MAX)));
		const UINT srvOverrideVertexCount = static_cast<UINT>(
			(std::min)(resource->byteWidth / elementStride, static_cast<UINT64>(UINT_MAX)));
		if (srvOriginalVertexCount && srvOverrideVertexCount) {
			if (!originalVertexCount || srvOriginalVertexCount < originalVertexCount)
				originalVertexCount = srvOriginalVertexCount;
			if (!overrideVertexCount || srvOverrideVertexCount < overrideVertexCount)
				overrideVertexCount = srvOverrideVertexCount;
		}
	}
	if (originalVertexCount && overrideVertexCount && originalVertexCount != overrideVertexCount) {
		requiresCountPatch = true;
		for (const DX12CurrentComputeUavBinding &cbv : cbvs) {
			if (CanPatchPreSkinVertexCountCbvLocked(cbv, originalVertexCount)) {
				canPatchCount = true;
				break;
			}
		}
		if (!canPatchCount) {
			StorePreSkinDescriptorPatchAbort(abortCacheKey);
#if defined(_DEBUG)
			DX12LogManager::DebugJsonFunc("DX12PreSkinningApply",
				"\"status\":\"resized_count_unpatchable\",\"section\":\"%S\",\"triggerHash\":\"%08x\",\"old\":%u,\"new\":%u,\"cbvs\":%zu",
				section ? section : L"", triggerHash,
				originalVertexCount, overrideVertexCount, cbvs.size());
#endif
			return false;
		}
	}

	UINT totalDescriptors = 0;
	for (TableCopy &table : tables) {
		table.tempOffset = totalDescriptors;
		totalDescriptors += table.count;
	}
	logStage("tables_built", static_cast<UINT>(tables.size()), totalDescriptors);
	if (!totalDescriptors) {
		logStage("no_descriptors", static_cast<UINT>(tables.size()), totalDescriptors);
		return false;
	}

	D3D12_CPU_DESCRIPTOR_HANDLE tempCpuBase = {};
	D3D12_GPU_DESCRIPTOR_HANDLE tempGpuBase = {};
	UINT tempIncrement = 0;
	ID3D12DescriptorHeap *tempHeap = nullptr;
	if (!EnsurePreSkinDescriptorRingLocked(
		    device, commandList, totalDescriptors, &tempCpuBase, &tempGpuBase, &tempIncrement,
		    &tempHeap)) {
		logStage("descriptor_ring_failed", static_cast<UINT>(tables.size()), totalDescriptors);
		return false;
	}
	logStage("ring_allocated", static_cast<UINT>(tables.size()), totalDescriptors);
	for (const TableCopy &table : tables) {
		D3D12_CPU_DESCRIPTOR_HANDLE src = {};
		src.ptr = table.cpuStart;
		D3D12_CPU_DESCRIPTOR_HANDLE dst = tempCpuBase;
		dst.ptr += static_cast<SIZE_T>(table.tempOffset) * tempIncrement;
		device->CopyDescriptorsSimple(
			table.count, dst, src,
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}
	logStage("tables_copied", static_cast<UINT>(tables.size()), totalDescriptors);

	auto findTable = [&tables](const DX12CurrentComputeUavBinding &binding) -> const TableCopy* {
		for (const TableCopy &table : tables) {
			if (table.root == binding.rootParameterIndex &&
			    table.cpuStart == binding.tableCpuStart &&
			    table.gpuStart.ptr == binding.tableGpuStart.ptr)
				return &table;
		}
		return nullptr;
	};
	static thread_local std::vector<ID3D12Resource*> tlsTemporaryResources;
	std::vector<ID3D12Resource*> &temporaryResources = tlsTemporaryResources;
	temporaryResources.clear();
	bool patchedAnySrv = false;
	for (const DX12CurrentComputeUavBinding &srv : srvs) {
		const DX12PreSkinSrvReplacementBinding *replacementBinding = nullptr;
		for (const DX12PreSkinSrvReplacementBinding &binding : srvReplacements) {
			if (binding.shaderRegister == srv.shaderRegister) {
				replacementBinding = &binding;
				break;
			}
		}
		if (!replacementBinding)
			continue;

		const UINT64 srvByteWidth = DescriptorViewSize(srv.descriptor);
		const UINT64 negativeKey = MakePreSkinSrvProbeCacheKey(srv, config, srvByteWidth);
		const UINT64 stableNegativeKey =
			MakePreSkinSrvStableProbeCacheKey(srv, config, srvByteWidth);
		if (gPreSkinSrvStableNegativeCache.find(stableNegativeKey) !=
		    gPreSkinSrvStableNegativeCache.end())
			continue;
		if (gPreSkinSrvNegativeCache.find(negativeKey) != gPreSkinSrvNegativeCache.end())
			continue;
		if (!srv.hasDescriptor || srv.descriptor.kind != "SRV" ||
		    srv.descriptor.viewDimension != D3D12_SRV_DIMENSION_BUFFER) {
			if (gPreSkinSrvStableNegativeCache.size() > 1024)
				gPreSkinSrvStableNegativeCache.clear();
			gPreSkinSrvStableNegativeCache.insert(stableNegativeKey);
			gPreSkinSrvNegativeCache.insert(negativeKey);
			LogPreSkinSrvProbe("skip_not_buffer_srv", &srv, &config, srv.shaderRegister, srvByteWidth, 0);
			continue;
		}
		const TableCopy *table = findTable(srv);
		if (!table) {
			gPreSkinSrvNegativeCache.insert(negativeKey);
			LogPreSkinSrvProbe("skip_no_table", &srv, &config, srv.shaderRegister, srvByteWidth, 0);
			continue;
		}
		DX12LoadedResource *resource = nullptr;
		UINT elementStride = 0;
		auto stablePositiveIt = gPreSkinSrvStablePositiveCache.find(stableNegativeKey);
		if (stablePositiveIt != gPreSkinSrvStablePositiveCache.end()) {
			resource = stablePositiveIt->second.resource;
			elementStride = stablePositiveIt->second.elementStride;
		} else {
			auto positiveIt = gPreSkinSrvPositiveCache.find(negativeKey);
			if (positiveIt != gPreSkinSrvPositiveCache.end()) {
				resource = positiveIt->second.resource;
				elementStride = positiveIt->second.elementStride;
			} else {
				resource = EnsureLoadedResourceForPreSkin(device, replacementBinding->resource);
				elementStride =
					srv.descriptor.structureByteStride ? srv.descriptor.structureByteStride :
					(resource ? resource->stride : 0);
				if (!resource || !EnsureResourceSrvLocked(device, resource, elementStride, srvByteWidth) ||
				    !resource->srvCpu.ptr) {
					gPreSkinSrvNegativeCache.insert(negativeKey);
					LogPreSkinSrvProbe("skip_srv_create_failed", &srv, &config, srv.shaderRegister, srvByteWidth, elementStride);
					continue;
				}
				if (gPreSkinSrvPositiveCache.size() > 256)
					gPreSkinSrvPositiveCache.clear();
				if (gPreSkinSrvStablePositiveCache.size() > 512)
					gPreSkinSrvStablePositiveCache.clear();
				const DX12PreSkinSrvPositiveCacheValue cacheValue =
					{ resource, elementStride, srvByteWidth, srv.shaderRegister };
				gPreSkinSrvPositiveCache[negativeKey] = cacheValue;
				gPreSkinSrvStablePositiveCache[stableNegativeKey] = cacheValue;
			}
		}
		if (!resource || !resource->srvCpu.ptr) {
			gPreSkinSrvNegativeCache.insert(negativeKey);
			LogPreSkinSrvProbe("skip_srv_create_failed", &srv, &config, srv.shaderRegister, srvByteWidth, elementStride);
			continue;
		}
		if (!elementStride) {
			gPreSkinSrvNegativeCache.insert(negativeKey);
			LogPreSkinSrvProbe("skip_invalid_stride", &srv, &config, srv.shaderRegister, srvByteWidth, elementStride);
			continue;
		}
		D3D12_CPU_DESCRIPTOR_HANDLE dst = tempCpuBase;
		dst.ptr += static_cast<SIZE_T>(table->tempOffset + srv.descriptorOffset) * tempIncrement;
		device->CopyDescriptorsSimple(
			1, dst, resource->srvCpu,
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		patchedAnySrv = true;
	}

	if (!patchedAnySrv) {
#if defined(_DEBUG)
		DX12LogManager::DebugJsonFunc("DX12PreSkinningApply",
			"\"status\":\"skip_no_srv_patched\",\"section\":\"%S\",\"triggerHash\":\"%08x\"",
			section ? section : L"", triggerHash);
#endif
		return false;
	}

	if (requiresCountPatch && canPatchCount) {
		bool patchedCountCbv = false;
		for (const DX12CurrentComputeUavBinding &cbv : cbvs) {
			if (!cbv.hasDescriptor || cbv.rootDescriptor || cbv.descriptor.kind != "CBV")
				continue;
			const TableCopy *table = findTable(cbv);
			if (!table)
				continue;
			D3D12_CPU_DESCRIPTOR_HANDLE dst = tempCpuBase;
			dst.ptr += static_cast<SIZE_T>(table->tempOffset + cbv.descriptorOffset) * tempIncrement;
			if (PatchPreSkinVertexCountCbvLocked(
				device, cbv, config, originalVertexCount, overrideVertexCount, dst,
				&temporaryResources, originalVertexCountOut, overrideVertexCountOut))
				patchedCountCbv = true;
		}
		if (!patchedCountCbv) {
			for (ID3D12Resource *resource : temporaryResources) {
				if (resource)
					resource->Release();
			}
			temporaryResources.clear();
			StorePreSkinDescriptorPatchAbort(abortCacheKey);
#if defined(_DEBUG)
			DX12LogManager::DebugJsonFunc("DX12PreSkinningApply",
				"\"status\":\"resized_count_unpatched\",\"section\":\"%S\",\"triggerHash\":\"%08x\",\"old\":%u,\"new\":%u,\"cbvs\":%zu",
				section ? section : L"", triggerHash,
				originalVertexCount, overrideVertexCount, cbvs.size());
#endif
			return false;
		}
	}
	ID3D12DescriptorHeap *restoreCbvSrvUavHeap = nullptr;
	ID3D12DescriptorHeap *restoreSamplerHeap = nullptr;
	DX12BindingTracker::GetCurrentDescriptorHeaps(
		commandList, &restoreCbvSrvUavHeap, &restoreSamplerHeap);
	if (restoreCbvSrvUavHeap != tempHeap) {
		ID3D12DescriptorHeap *heaps[2] = { tempHeap, restoreSamplerHeap };
		logStage("before_set_heaps", static_cast<UINT>(tables.size()), totalDescriptors);
		commandList->SetDescriptorHeaps(restoreSamplerHeap ? 2 : 1, heaps);
		logStage("after_set_heaps", static_cast<UINT>(tables.size()), totalDescriptors);
	}
	DX12PreSkinReplacementState state;
	state.active = true;
	state.rootParameterIndex = uavBinding.rootParameterIndex;
	state.originalTable = uavBinding.tableGpuStart;
	state.originalHeap = uavBinding.tableHeap;
	state.resource = uavBinding.descriptor.resource;
	state.byteWidth = DescriptorViewSize(uavBinding.descriptor);
	state.stride = uavBinding.descriptor.structureByteStride;
	state.hasActiveTextureOverride = state.resource && state.byteWidth && state.stride;
	if (state.hasActiveTextureOverride) {
		state.activeConfig = config;
		state.activeProducer = producer;
		if (state.resource)
			state.resource->AddRef();
	}
	state.patchHeap = tempHeap;
	state.restoreCbvSrvUavHeap = restoreCbvSrvUavHeap;
	state.restoreSamplerHeap = restoreSamplerHeap;
	state.temporaryResources = temporaryResources;
	for (const TableCopy &table : tables) {
		D3D12_GPU_DESCRIPTOR_HANDLE gpu = tempGpuBase;
		gpu.ptr += static_cast<UINT64>(table.tempOffset) * tempIncrement;
		logStage("before_set_root_table", static_cast<UINT>(tables.size()), totalDescriptors);
		commandList->SetComputeRootDescriptorTable(table.root, gpu);
		logStage("after_set_root_table", static_cast<UINT>(tables.size()), totalDescriptors);
		DX12PreSkinReplacementState::TableRestore restore;
		restore.rootParameterIndex = table.root;
		restore.originalTable = table.gpuStart;
		state.tableRestores.push_back(restore);
	}
	if (originalVertexCountOut)
		*originalVertexCountOut = originalVertexCount;
	if (overrideVertexCountOut)
		*overrideVertexCountOut = overrideVertexCount;
	AcquireSRWLockExclusive(&gPreSkinLock);
	gActivePreSkinReplacements[commandList] = state;
	ReleaseSRWLockExclusive(&gPreSkinLock);

#if defined(_DEBUG)
	DX12LogManager::DebugJsonFunc("DX12PreSkinningApply",
		"\"status\":\"srv_table_patched\",\"section\":\"%S\",\"triggerHash\":\"%08x\",\"tables\":%zu,\"descriptors\":%u",
		section ? section : L"", triggerHash, tables.size(), totalDescriptors);
#endif
	return true;
}

static bool HasExplicitPreSkinDispatchOverride(
	const Bunny::TextureOverrideConfig &config)
{
	if (!config.handlingSkip)
		return false;
	for (const Bunny::CommandListAction &action : config.actions) {
		if (action.kind == Bunny::CommandListActionKind::Dispatch &&
		    action.argCount >= 3)
			return true;
	}
	return false;
}
static void FillPreSkinDispatchOverrideFromConfig(
	const Bunny::TextureOverrideConfig &config,
	DX12PreSkinDispatchOverride *dispatchOverride)
{
	if (!dispatchOverride)
		return;
	dispatchOverride->handlingSkip = config.handlingSkip;
	dispatchOverride->hasDispatch = false;
	dispatchOverride->groupsX = 0;
	dispatchOverride->groupsY = 0;
	dispatchOverride->groupsZ = 0;
	for (const Bunny::CommandListAction &action : config.actions) {
		if (action.kind != Bunny::CommandListActionKind::Dispatch ||
		    action.argCount < 3)
			continue;
		dispatchOverride->hasDispatch = true;
		dispatchOverride->groupsX = action.args[0];
		dispatchOverride->groupsY = action.args[1];
		dispatchOverride->groupsZ = action.args[2];
	}
}
static bool ApplyPreSkinBindingPatchLocked(
	ID3D12Device *device, ID3D12GraphicsCommandList *commandList,
	const DX12CurrentComputeUavBinding &binding, const DX12ComputeUavProducer &producer,
	DX12LoadedResource *replacement, const wchar_t *section, uint32_t triggerHash,
	std::vector<DX12PreSkinReplacementState::DescriptorRestore> *descriptorRestores)
{
	if (!device || !commandList || !replacement || !replacement->uavResource ||
	    !replacement->uavCpu.ptr || !binding.hasDescriptor || !binding.tableGpuStart.ptr)
		return false;
	if (replacement->uavState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = replacement->uavResource;
		barrier.Transition.StateBefore = replacement->uavState;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		commandList->ResourceBarrier(1, &barrier);
		replacement->uavState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	}
	replacement->uavWritten = false;
	replacement->uavValid = false;

	bool alreadyPatched = false;
	AcquireSRWLockExclusive(&gPreSkinLock);
	alreadyPatched = !gPatchedPreSkinDescriptors.insert(
		binding.descriptor.cpuHandle).second;
	if (gPatchedPreSkinDescriptors.size() > 4096)
		gPatchedPreSkinDescriptors.clear();
	gKnownPreSkinDescriptorPatches[binding.descriptor.cpuHandle] =
		replacement->name;
	ReleaseSRWLockExclusive(&gPreSkinLock);
	D3D12_CPU_DESCRIPTOR_HANDLE target = {};
	target.ptr = binding.descriptor.cpuHandle;
	if (descriptorRestores) {
		D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
		heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		heapDesc.NumDescriptors = 1;
		heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		ID3D12DescriptorHeap *restoreHeap = nullptr;
		HRESULT hr = device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&restoreHeap));
		if (FAILED(hr) || !restoreHeap)
			return false;
		DX12PreSkinReplacementState::DescriptorRestore restore;
		restore.targetCpu = target.ptr;
		restore.heap = restoreHeap;
		restore.cpu = restoreHeap->GetCPUDescriptorHandleForHeapStart();
		device->CopyDescriptorsSimple(
			1, restore.cpu, target,
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		descriptorRestores->push_back(restore);
	}
	device->CopyDescriptorsSimple(
		1, target, replacement->uavCpu,
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	DX12PreSkinReplacementState state;
	state.active = true;
	state.rootParameterIndex = binding.rootParameterIndex;
	state.originalTable = binding.tableGpuStart;
	state.replacementTable = replacement->uavGpu;
	state.descriptorHeap = replacement->uavHeap;
	state.originalHeap = binding.tableHeap;
	state.resource = replacement->uavResource;
	state.byteWidth = replacement->byteWidth;
	state.stride = replacement->stride;
	state.producerSerial = gComputeUavSerial + 1;
	if (descriptorRestores)
		state.descriptorRestores.swap(*descriptorRestores);
	AcquireSRWLockExclusive(&gPreSkinLock);
	gActivePreSkinReplacements[commandList] = state;
	ReleaseSRWLockExclusive(&gPreSkinLock);

#if defined(_DEBUG)
	if (!alreadyPatched) {
		DX12LogManager::DebugJsonFunc("DX12PreSkinningApply",
			"\"status\":\"patched\",\"section\":\"%S\",\"resource\":\"%S\",\"triggerHash\":\"%08x\",\"root\":%u,\"range\":%u,\"uRegister\":%u,\"space\":%u,\"targetCpu\":\"0x%zx\",\"bytes\":%llu,\"stride\":%u,\"producerResource\":\"%p\",\"producerBytes\":%llu,\"producerStride\":%u",
			section ? section : L"", replacement->name.c_str(), triggerHash,
			binding.rootParameterIndex, binding.rangeIndex,
			binding.shaderRegister, binding.registerSpace,
			binding.descriptor.cpuHandle,
			static_cast<unsigned long long>(replacement->byteWidth),
			replacement->stride,
			producer.descriptor.resource,
			static_cast<unsigned long long>(producer.descriptor.viewSize),
			producer.descriptor.structureByteStride);
	}
#else
	(void)alreadyPatched;
#endif
	return true;
}

static bool ApplyPreSkinSrvInputPatchesLocked(
	ID3D12Device *device, const std::vector<DX12CurrentComputeUavBinding> &srvs,
	const Bunny::TextureOverrideConfig &config,
	const std::vector<DX12PreSkinSrvReplacementBinding> &srvReplacements,
	std::vector<DX12PreSkinReplacementState::DescriptorRestore> *descriptorRestores)
{
	if (!device || srvs.empty())
		return false;

	bool changed = false;
	for (const DX12CurrentComputeUavBinding &srv : srvs) {
		if (!srv.hasDescriptor || !srv.descriptor.cpuHandle ||
		    srv.descriptor.kind != "SRV" ||
		    srv.descriptor.viewDimension != D3D12_SRV_DIMENSION_BUFFER)
			continue;

		const DX12PreSkinSrvReplacementBinding *replacementBinding = nullptr;
		for (const DX12PreSkinSrvReplacementBinding &binding : srvReplacements) {
			if (binding.shaderRegister == srv.shaderRegister) {
				replacementBinding = &binding;
				break;
			}
		}
		if (!replacementBinding)
			continue;

		DX12LoadedResource *resource = EnsureLoadedResourceForPreSkin(device, replacementBinding->resource);
		const UINT elementStride =
			srv.descriptor.structureByteStride ? srv.descriptor.structureByteStride :
			(resource ? resource->stride : 0);
		const UINT64 srvByteWidth = DescriptorViewSize(srv.descriptor);
		if (!resource || !EnsureResourceSrvLocked(device, resource, elementStride, srvByteWidth) ||
		    !resource->srvCpu.ptr)
			continue;

		D3D12_CPU_DESCRIPTOR_HANDLE target = {};
		target.ptr = srv.descriptor.cpuHandle;
		if (descriptorRestores) {
			D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
			heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			heapDesc.NumDescriptors = 1;
			heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
			ID3D12DescriptorHeap *restoreHeap = nullptr;
			HRESULT hr = device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&restoreHeap));
			if (FAILED(hr) || !restoreHeap)
				continue;
			DX12PreSkinReplacementState::DescriptorRestore restore;
			restore.targetCpu = target.ptr;
			restore.heap = restoreHeap;
			restore.cpu = restoreHeap->GetCPUDescriptorHandleForHeapStart();
			device->CopyDescriptorsSimple(
				1, restore.cpu, target,
				D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			descriptorRestores->push_back(restore);
		}
		device->CopyDescriptorsSimple(
			1, target, resource->srvCpu,
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		changed = true;

#if defined(_DEBUG)
		DX12LogManager::DebugJsonFunc("DX12PreSkinningSrvPatch",
			"\"status\":\"patched\",\"resource\":\"%S\",\"csTRegister\":%u,\"tRegister\":%u,\"targetCpu\":\"0x%zx\",\"bytes\":%llu,\"srvBytes\":%llu,\"stride\":%u",
			resource->name.c_str(), replacementBinding->replaceSlot, srv.shaderRegister,
			srv.descriptor.cpuHandle,
			static_cast<unsigned long long>(resource->byteWidth),
			static_cast<unsigned long long>(resource->srvByteWidth),
			elementStride);
#endif
	}
	return changed;
}

bool DX12ModApplyPreSkinningUavReplacement(
	ID3D12GraphicsCommandList *commandList,
	uint64_t computeShaderHash,
	const std::vector<DX12CurrentComputeUavBinding> &uavs,
	const std::vector<DX12CurrentComputeUavBinding> &srvs,
	const std::vector<DX12CurrentComputeUavBinding> &cbvs,
	const std::vector<DX12CurrentRootConstants> &rootConstants,
	UINT *originalVertexCount,
	UINT *overrideVertexCount,
	DX12PreSkinDispatchOverride *dispatchOverride)
{
	if (originalVertexCount)
		*originalVertexCount = 0;
	if (overrideVertexCount)
		*overrideVertexCount = 0;
	if (dispatchOverride)
		*dispatchOverride = DX12PreSkinDispatchOverride();
	if (!commandList || !DX12ModHasActiveTextureOverrides())
		return false;

	ID3D12Device *device = AcquireModDevice(commandList);
	if (!device)
		return false;

	DX12InternalReplayScope internalReplay;
	bool applied = false;
	DX12ComputeUavProducer producer = {};
	DX12VertexLimitRaiseConfig vlr = {};
	std::wstring vlrResourceName;
	DX12LoadedResource *replacement = nullptr;
	DX12CurrentComputeUavBinding hashBinding = {};
	DX12UavHashTextureOverrideMatch hashMatch = {};
	bool hashMatched = false;
	bool explicitMatchCs = false;
	bool hasExplicitDispatch = false;
	bool vlrMatched = false;
	std::unordered_set<std::wstring> activePreSkinSections;
	SnapshotActivePreSkinSections(&activePreSkinSections);

	AcquireSRWLockExclusive(&gModLock);
	explicitMatchCs = HasMatchCsTextureOverrideLocked(computeShaderHash);
	hashMatched = explicitMatchCs && FindPreSkinProducerByMatchCsLocked(
		uavs, srvs, cbvs, rootConstants, computeShaderHash, &producer, &hashBinding, &hashMatch);
	if (hashMatched)
		hasExplicitDispatch = HasExplicitPreSkinDispatchOverride(hashMatch.config);
	if ((!hashMatched || !hasExplicitDispatch) && !uavs.empty()) {
		vlrMatched = FindPreSkinProducerLocked(uavs, computeShaderHash, &producer, &vlr) &&
			FindReplacementResourceNameForVlrLocked(vlr, &vlrResourceName);
	}
	ReleaseSRWLockExclusive(&gModLock);

	if (hashMatched) {
		if (!hasExplicitDispatch) {
#if defined(_DEBUG)
			DX12LogManager::DebugJsonFunc("DX12PreSkinningApply",
				"\"status\":\"skip_missing_explicit_dispatch\",\"section\":\"%S\",\"handlingSkip\":%s",
				hashMatch.config.section.c_str(),
				hashMatch.config.handlingSkip ? "true" : "false");
#endif
		} else {
			if (!hashMatch.srvReplacements.empty()) {
				applied = ApplyPreSkinDescriptorTablePatchLocked(
					device, commandList, hashBinding, producer,
					hashMatch.config, hashMatch.srvReplacements,
					srvs, cbvs, rootConstants,
					hashMatch.config.originalSection.empty() ?
						hashMatch.config.section.c_str() :
						hashMatch.config.originalSection.c_str(),
					hashMatch.hash, originalVertexCount, overrideVertexCount);
			}
			if (applied) {
				FillPreSkinDispatchOverrideFromConfig(hashMatch.config, dispatchOverride);
				DX12Profiling::RecordPreSkinApplied();
			}
		}
	}
	if (!applied && vlrMatched) {
		replacement = EnsureLoadedResourceForPreSkin(device, vlrResourceName);
		if (replacement && EnsureResourceUavLocked(
			device, commandList, replacement,
			vlr.uavByteStride ? vlr.uavByteStride : vlr.overrideByteStride,
			0, true, &activePreSkinSections)) {
			const DX12CurrentComputeUavBinding *binding = nullptr;
			for (const DX12CurrentComputeUavBinding &uav : uavs) {
				if (uav.rootParameterIndex == producer.rootParameterIndex &&
				    uav.rangeIndex == producer.rangeIndex &&
				    uav.descriptorOffset == producer.descriptorOffset &&
				    uav.hasDescriptor && uav.tableGpuStart.ptr) {
					binding = &uav;
					break;
				}
			}
			if (binding) {
				std::vector<DX12PreSkinReplacementState::DescriptorRestore> descriptorRestores;
				applied = ApplyPreSkinBindingPatchLocked(
					device, commandList, *binding, producer, replacement,
					vlr.section.c_str(), 0, &descriptorRestores);
				if (applied)
					DX12Profiling::RecordPreSkinApplied();
				if (!applied)
					ReleasePreSkinDescriptorRestores(&descriptorRestores);
			}
		}
	}
	DX12Profiling::RecordPreSkinUavTest(applied);
	device->Release();
	return applied;
}

bool DX12ModApplyKnownPreSkinningUavPatches(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList || !DX12ModHasActiveTextureOverrides())
		return false;

	std::vector<std::pair<SIZE_T, std::wstring>> patches;
	AcquireSRWLockShared(&gPreSkinLock);
	patches.assign(gKnownPreSkinDescriptorPatches.begin(), gKnownPreSkinDescriptorPatches.end());
	ReleaseSRWLockShared(&gPreSkinLock);
	if (patches.empty())
		return false;

	ID3D12Device *device = AcquireModDevice(commandList);
	if (!device)
		return false;

	DX12InternalReplayScope internalReplay;
	bool applied = false;
	std::unordered_set<std::wstring> activePreSkinSections;
	SnapshotActivePreSkinSections(&activePreSkinSections);
	for (const auto &patch : patches) {
		AcquireSRWLockExclusive(&gModLock);
		const bool blocked =
			ResourceBlockedByInactiveExplicitMatchCsLocked(patch.second, activePreSkinSections);
		ReleaseSRWLockExclusive(&gModLock);
		if (blocked)
			continue;
		DX12LoadedResource *resource = EnsureLoadedResourceForPreSkin(device, patch.second);
		if (!resource)
			continue;
		AcquireSRWLockExclusive(&gModLock);
		const UINT elementStride =
			FindPreSkinUavElementStrideLocked(*resource, activePreSkinSections);
		ReleaseSRWLockExclusive(&gModLock);
		if (!EnsureResourceUavLocked(
			device, commandList, resource, elementStride, 0, true,
			&activePreSkinSections))
			continue;
		if (!resource->uavResource || !resource->uavCpu.ptr)
			continue;
		if (resource->uavState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
			D3D12_RESOURCE_BARRIER barrier = {};
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Transition.pResource = resource->uavResource;
			barrier.Transition.StateBefore = resource->uavState;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
			barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			commandList->ResourceBarrier(1, &barrier);
			resource->uavState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		}
		D3D12_CPU_DESCRIPTOR_HANDLE target = {};
		target.ptr = patch.first;
		device->CopyDescriptorsSimple(
			1, target, resource->uavCpu,
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		resource->uavWritten = true;
		resource->uavValid = true;
		applied = true;
	}
	device->Release();
	return applied;
}

void DX12ModRestorePreSkinningUavReplacement(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return;

	DX12InternalReplayScope internalReplay;
	DX12PreSkinReplacementState state = {};
	bool found = false;
	AcquireSRWLockExclusive(&gPreSkinLock);
	auto it = gActivePreSkinReplacements.find(commandList);
	if (it != gActivePreSkinReplacements.end()) {
		state = it->second;
		gActivePreSkinReplacements.erase(it);
		found = state.active;
	}
	ReleaseSRWLockExclusive(&gPreSkinLock);

	if (!found)
		return;

#if defined(_DEBUG)
	const size_t restoredDescriptorCount = state.descriptorRestores.size();
	const size_t restoredTableCount = state.tableRestores.size();
#endif
	if (state.patchHeap) {
		ID3D12DescriptorHeap *restoreCbvSrvUavHeap =
			state.restoreCbvSrvUavHeap ? state.restoreCbvSrvUavHeap : state.originalHeap;
		ID3D12DescriptorHeap *heaps[2] = {};
		UINT heapCount = 0;
		if (restoreCbvSrvUavHeap)
			heaps[heapCount++] = restoreCbvSrvUavHeap;
		if (state.restoreSamplerHeap)
			heaps[heapCount++] = state.restoreSamplerHeap;
		commandList->SetDescriptorHeaps(heapCount, heapCount ? heaps : nullptr);
		DX12CommandListRuntimeSetDescriptorHeaps(
			commandList, restoreCbvSrvUavHeap, state.restoreSamplerHeap);
		DX12BindingTracker::SetDescriptorHeaps(commandList, heapCount, heapCount ? heaps : nullptr);
		for (const auto &restore : state.tableRestores) {
			commandList->SetComputeRootDescriptorTable(
				restore.rootParameterIndex, restore.originalTable);
			DX12BindingTracker::SetComputeRootDescriptorTable(
				commandList, restore.rootParameterIndex, restore.originalTable);
		}
		state.patchHeap = nullptr;
	}
	for (ID3D12Resource *resource : state.temporaryResources) {
		if (resource)
			RetirePreSkinResourceForCommandList(commandList, resource);
	}
	state.temporaryResources.clear();
	ID3D12Device *device = AcquireModDevice(commandList);
	if (device) {
		for (const auto &restore : state.descriptorRestores) {
			if (!restore.heap || !restore.cpu.ptr || !restore.targetCpu)
				continue;
			D3D12_CPU_DESCRIPTOR_HANDLE target = {};
			target.ptr = restore.targetCpu;
			device->CopyDescriptorsSimple(
				1, target, restore.cpu,
				D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		}
		device->Release();
	}
	ReleasePreSkinDescriptorRestores(&state.descriptorRestores);

	if (state.hasActiveTextureOverride && state.resource) {
		std::vector<ID3D12Resource*> releasePreSkinResources;
		DX12ActivePreSkinTextureOverride active = {};
		active.config = state.activeConfig;
		active.producer = state.activeProducer;
		active.outputResource = state.resource;
		active.outputByteWidth = state.byteWidth;
		active.outputStride = state.stride;
		AcquireSRWLockExclusive(&gPreSkinLock);
		StoreActivePreSkinTextureOverrideLocked(
			active.config.section, active, &releasePreSkinResources);
		ReleaseSRWLockExclusive(&gPreSkinLock);
		state.resource = nullptr;
		ReleaseMovedPreSkinResources(&releasePreSkinResources);
	}

#if defined(_DEBUG)
	DX12LogManager::DebugJsonFunc("DX12PreSkinningApply",
		"\"status\":\"post_dispatch\",\"section\":\"%S\",\"present\":%ld,\"root\":%u,\"bytes\":%llu,\"stride\":%u,\"restoredDescriptors\":%zu,\"restoredTables\":%zu",
		state.activeConfig.section.c_str(),
		DX12RuntimeState::GetPresentCount(),
		state.rootParameterIndex,
		static_cast<unsigned long long>(state.byteWidth), state.stride,
		restoredDescriptorCount, restoredTableCount);
#endif
}


