struct DX12StoredPso
{
	DX12PsoKind kind = DX12PsoKind::Graphics;
	ID3D12Device *device = nullptr;
	ID3D12RootSignature *graphicsRootSignature = nullptr;
	ID3D12RootSignature *computeRootSignature = nullptr;
	D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsDesc = {};
	D3D12_COMPUTE_PIPELINE_STATE_DESC computeDesc = {};
	std::vector<unsigned char> vsBytecode;
	std::vector<unsigned char> psBytecode;
	std::vector<unsigned char> dsBytecode;
	std::vector<unsigned char> hsBytecode;
	std::vector<unsigned char> gsBytecode;
	std::vector<unsigned char> csBytecode;
	ID3D12PipelineState *replacement = nullptr;
	UINT64 replacementGeneration = 0;
	bool skipDraw = false;
	bool skipDispatch = false;
	UINT64 skipGeneration = 0;
};

static std::unordered_map<ID3D12PipelineState*, DX12StoredPso> gPsoRecords;

static void ReleaseLoadedResourcesLocked();
static bool ResourceConfigLooksLikeBuffer(const Bunny::ResourceConfig &config);

static std::vector<DX12VertexLimitRaiseConfig> CollectVertexLimitRaiseConfigs(
	const Bunny::TextureOverrideMap &textureOverrides)
{
	std::vector<DX12VertexLimitRaiseConfig> configs;
	for (const auto &bucket : textureOverrides) {
		for (const Bunny::TextureOverrideConfig &config : bucket.second) {
			if (!config.hasVertexLimitRaise || !config.overrideByteWidth)
				continue;
			DX12VertexLimitRaiseConfig out;
			out.section = config.originalSection.empty() ? config.section : config.originalSection;
			out.hasMatchCs = config.hasMatchCs || config.hasCsHash;
			out.matchCs = config.hasCsHash ? config.csHash : config.matchCs;
			out.overrideByteStride = config.overrideByteStride;
			out.overrideVertexCount = config.overrideVertexCount;
			out.overrideByteWidth = config.overrideByteWidth;
			out.uavByteStride = config.uavByteStride;
			out.overrideNumElements = config.overrideNumElements;
			configs.push_back(out);
		}
	}
	return configs;
}

static uint64_t GetPreSkinCsHash(const Bunny::TextureOverrideConfig &config)
{
	return config.hasCsHash ? config.csHash : config.matchCs;
}

static bool IsComputePreSkinTextureOverride(const Bunny::TextureOverrideConfig &config)
{
	const bool hasCsHash = config.hasMatchCs || config.hasCsHash;
	return hasCsHash && !config.preSkinMatchCsSrvHashes.empty() &&
		!config.preSkinCsSrvResources.empty();
}

static void BuildDx12PreSkinTextureOverrideIndex(
	const Bunny::TextureOverrideMap &textureOverrides,
	std::vector<DX12PreSkinTextureOverrideCandidate> *preSkinTextureOverrides,
	std::unordered_map<uint32_t, std::vector<size_t>> *uavHashTextureOverrideIndex,
	std::unordered_map<uint64_t, std::vector<size_t>> *matchCsTextureOverrideIndex)
{
	if (!preSkinTextureOverrides || !uavHashTextureOverrideIndex ||
	    !matchCsTextureOverrideIndex)
		return;

	preSkinTextureOverrides->clear();
	uavHashTextureOverrideIndex->clear();
	matchCsTextureOverrideIndex->clear();
	UINT64 order = 0;
	for (const auto &bucket : textureOverrides) {
		for (const Bunny::TextureOverrideConfig &config : bucket.second) {
			if (!IsComputePreSkinTextureOverride(config))
				continue;

			DX12PreSkinTextureOverrideCandidate candidate;
			candidate.config = config;
			candidate.hash = bucket.first;
			candidate.order = order++;
			const size_t index = preSkinTextureOverrides->size();
			preSkinTextureOverrides->push_back(std::move(candidate));
			(*matchCsTextureOverrideIndex)[GetPreSkinCsHash(config)].push_back(index);
		}
	}
}

static void BuildDx12ExplicitMatchCsResourceIndex(
	const Bunny::TextureOverrideMap &textureOverrides,
	std::unordered_map<std::wstring, std::vector<std::wstring>> *resourceSections)
{
	if (!resourceSections)
		return;

	resourceSections->clear();
	for (const auto &bucket : textureOverrides) {
		for (const Bunny::TextureOverrideConfig &config : bucket.second) {
			if (!config.hasMatchCs && !config.hasCsHash)
				continue;
			for (const auto &item : config.preSkinCsSrvResources) {
				if (item.second.empty())
					continue;
				(*resourceSections)[Bunny::ToLower(item.second)].push_back(config.section);
			}
		}
	}
}

static bool HasPreSkinTextureOverrideCandidates(
	const Bunny::TextureOverrideMap &textureOverrides,
	std::unordered_set<uint64_t> *matchCsHashes,
	bool *hasVlrWithoutMatchCs,
	bool *hasExplicitMatchCsUavBytes)
{
	if (matchCsHashes)
		matchCsHashes->clear();
	if (hasVlrWithoutMatchCs)
		*hasVlrWithoutMatchCs = false;
	if (hasExplicitMatchCsUavBytes)
		*hasExplicitMatchCsUavBytes = false;

	bool found = false;
	for (const auto &bucket : textureOverrides) {
		for (const Bunny::TextureOverrideConfig &config : bucket.second) {
			if (IsComputePreSkinTextureOverride(config)) {
				found = true;
				if (matchCsHashes)
					matchCsHashes->insert(GetPreSkinCsHash(config));
				continue;
			}
			if (config.hasVertexLimitRaise && config.overrideByteWidth) {
				found = true;
				if (hasVlrWithoutMatchCs)
					*hasVlrWithoutMatchCs = true;
			}
		}
	}
	return found;
}

static bool ResourceUsedByExplicitMatchCsLocked(const std::wstring &resourceName)
{
	if (resourceName.empty())
		return false;
	return gExplicitMatchCsResourceSections.find(Bunny::ToLower(resourceName)) !=
		gExplicitMatchCsResourceSections.end();
}

static bool ResourceBlockedByInactiveExplicitMatchCsLocked(
	const std::wstring &resourceName,
	const std::unordered_set<std::wstring> &activePreSkinSections)
{
	if (resourceName.empty())
		return false;

	auto indexed = gExplicitMatchCsResourceSections.find(Bunny::ToLower(resourceName));
	if (indexed == gExplicitMatchCsResourceSections.end())
		return false;

	for (const std::wstring &section : indexed->second) {
		if (activePreSkinSections.find(section) != activePreSkinSections.end())
			return false;
	}
	return true;
}

static void BuildDx12VlrResourceCandidates(
	const Bunny::ResourceMap &resources,
	const std::unordered_map<std::wstring, std::vector<std::wstring>> &explicitMatchCsResourceSections,
	std::vector<std::wstring> *resourceCandidates)
{
	if (!resourceCandidates)
		return;

	resourceCandidates->clear();
	std::unordered_set<std::wstring> seen;
	for (const auto &item : resources) {
		if (!ResourceConfigLooksLikeBuffer(item.second))
			continue;
		if (explicitMatchCsResourceSections.find(Bunny::ToLower(item.first)) !=
		    explicitMatchCsResourceSections.end())
			continue;
		if (!seen.insert(item.first).second)
			continue;
		resourceCandidates->push_back(item.first);
	}
}

static void ClearPreSkinRuntimeState()
{
	std::vector<ID3D12Resource*> releasePreSkinResources;
	AcquireSRWLockExclusive(&gPreSkinLock);
	gRecentComputeUavs.clear();
	gComputeUavSerial = 0;
	gPatchedPreSkinDescriptors.clear();
	gKnownPreSkinDescriptorPatches.clear();
	ClearActivePreSkinTextureOverridesLocked(&releasePreSkinResources);
	gPreSkinCbvReadCache.clear();
	gPreSkinSrvNegativeCache.clear();
	gPreSkinSrvStableNegativeCache.clear();
	gPreSkinDescriptorPatchAbortCache.clear();
	gPreSkinSrvPositiveCache.clear();
	gPreSkinSrvStablePositiveCache.clear();
	InterlockedExchange(&gPreSkinDescriptorAbortCacheHitLogCount, 0);
	++gPreSkinSrvCacheGeneration;
	ReleaseSRWLockExclusive(&gPreSkinLock);
	ReleaseMovedPreSkinResources(&releasePreSkinResources);

	DX12PreSkinReleaseBatch releaseBatch;
	AcquireSRWLockExclusive(&gPreSkinRetireLock);
	for (auto &pending : gPendingPreSkinSubmissions)
		MovePendingPreSkinSubmissionToReleaseBatch(&pending.second, &releaseBatch);
	gPendingPreSkinSubmissions.clear();
	for (auto &retired : gRetiredPreSkinResources) {
		if (retired.resource)
			releaseBatch.resources.push_back(retired.resource);
	}
	gRetiredPreSkinResources.clear();
	for (auto &retired : gRetiredPreSkinDescriptorHeaps) {
		if (retired.heap)
			releaseBatch.descriptorHeaps.push_back(retired.heap);
	}
	gRetiredPreSkinDescriptorHeaps.clear();
	ReleaseSRWLockExclusive(&gPreSkinRetireLock);

	AcquireSRWLockExclusive(&gPreSkinDescriptorRingLock);
	ReleasePreSkinDescriptorRingLocked(&releaseBatch);
	ReleaseSRWLockExclusive(&gPreSkinDescriptorRingLock);
	ReleasePreSkinReleaseBatch(&releaseBatch);
}

static bool DescriptorOverlapsView(
	const DX12DescriptorSummary &descriptor, UINT64 gpuVa, UINT64 size)
{
	if (!descriptor.resource || gpuVa == 0 || size == 0)
		return false;

	UINT64 descriptorBegin = descriptor.gpuVirtualAddress + descriptor.resourceOffset;
	UINT64 descriptorSize = descriptor.viewSize;
	if (!descriptorSize && descriptor.hasResourceDesc &&
		descriptor.resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
		descriptorSize = descriptor.resourceDesc.Width;
	if (!descriptorSize)
		return false;

	const UINT64 descriptorEnd = descriptorBegin + descriptorSize;
	const UINT64 viewEnd = gpuVa + size;
	return gpuVa < descriptorEnd && descriptorBegin < viewEnd;
}

static bool ProducerMatchesIaBuffer(
	const DX12ComputeUavProducer &producer, const DX12IaBufferHash &buffer)
{
	if (!producer.hasDescriptor)
		return false;
	const D3D12_VERTEX_BUFFER_VIEW &view = buffer.vertexView;
	return DescriptorOverlapsView(producer.descriptor, view.BufferLocation, view.SizeInBytes);
}

static bool ProducerMatchesReplacementTarget(
	const DX12ComputeUavProducer &producer, const DX12VertexLimitRaiseConfig &vlr)
{
	if (!producer.hasDescriptor)
		return false;
	const DX12DescriptorSummary &descriptor = producer.descriptor;
	if (descriptor.kind != "UAV" || descriptor.viewDimension != D3D12_UAV_DIMENSION_BUFFER)
		return false;
	if (vlr.uavByteStride && descriptor.structureByteStride &&
		descriptor.structureByteStride != vlr.uavByteStride)
		return false;
	if (vlr.overrideByteWidth && descriptor.viewSize &&
		descriptor.viewSize + static_cast<UINT64>(vlr.overrideByteStride) * 4096 < vlr.overrideByteWidth)
		return false;
	return true;
}

static void SetBasePaths(const wchar_t *configPath)
{
	gConfigPath = configPath ? configPath : L"";
	gBaseDir = gConfigPath;
	if (!gBaseDir.empty()) {
		wchar_t path[MAX_PATH];
		wcsncpy_s(path, gBaseDir.c_str(), _TRUNCATE);
		PathRemoveFileSpecW(path);
		gBaseDir = path;
	}
	if (gBaseDir.empty())
		gBaseDir = L".";

	wchar_t shaderFixes[MAX_PATH];
	wcsncpy_s(shaderFixes, gBaseDir.c_str(), _TRUNCATE);
	PathAppendW(shaderFixes, L"ShaderFixes");
	gShaderFixesDir = shaderFixes;
}

static bool ReadFileBytes(const wchar_t *path, std::vector<unsigned char> *data)
{
	if (!path || !data)
		return false;

	FILE *file = _wfsopen(path, L"rb", _SH_DENYNO);
	if (!file)
		return false;

	if (fseek(file, 0, SEEK_END) != 0) {
		fclose(file);
		return false;
	}
	long size = ftell(file);
	if (size <= 0) {
		fclose(file);
		return false;
	}
	if (fseek(file, 0, SEEK_SET) != 0) {
		fclose(file);
		return false;
	}

	data->resize(static_cast<size_t>(size));
	size_t read = fread(data->data(), 1, data->size(), file);
	fclose(file);
	if (read != data->size()) {
		data->clear();
		return false;
	}
	return true;
}

static void ReleaseStoredPso(DX12StoredPso *record)
{
	if (!record)
		return;
	if (record->replacement) {
		record->replacement->Release();
		record->replacement = nullptr;
	}
	if (record->device) {
		record->device->Release();
		record->device = nullptr;
	}
	if (record->graphicsRootSignature) {
		record->graphicsRootSignature->Release();
		record->graphicsRootSignature = nullptr;
	}
	if (record->computeRootSignature) {
		record->computeRootSignature->Release();
		record->computeRootSignature = nullptr;
	}
}

static void StoreShaderBytecode(
	const D3D12_SHADER_BYTECODE &source, std::vector<unsigned char> *storage,
	D3D12_SHADER_BYTECODE *target)
{
	if (!storage || !target)
		return;

	*target = {};
	storage->clear();
	if (!source.pShaderBytecode || source.BytecodeLength == 0)
		return;

	storage->resize(source.BytecodeLength);
	memcpy(storage->data(), source.pShaderBytecode, source.BytecodeLength);
	target->pShaderBytecode = storage->data();
	target->BytecodeLength = storage->size();
}

static void DeepCopyGraphicsDesc(
	const D3D12_GRAPHICS_PIPELINE_STATE_DESC *source, DX12StoredPso *record)
{
	record->graphicsDesc = *source;
	record->graphicsRootSignature = source->pRootSignature;
	if (record->graphicsRootSignature)
		record->graphicsRootSignature->AddRef();
	StoreShaderBytecode(source->VS, &record->vsBytecode, &record->graphicsDesc.VS);
	StoreShaderBytecode(source->PS, &record->psBytecode, &record->graphicsDesc.PS);
	StoreShaderBytecode(source->DS, &record->dsBytecode, &record->graphicsDesc.DS);
	StoreShaderBytecode(source->HS, &record->hsBytecode, &record->graphicsDesc.HS);
	StoreShaderBytecode(source->GS, &record->gsBytecode, &record->graphicsDesc.GS);
}

static void DeepCopyComputeDesc(
	const D3D12_COMPUTE_PIPELINE_STATE_DESC *source, DX12StoredPso *record)
{
	record->computeDesc = *source;
	record->computeRootSignature = source->pRootSignature;
	if (record->computeRootSignature)
		record->computeRootSignature->AddRef();
	StoreShaderBytecode(source->CS, &record->csBytecode, &record->computeDesc.CS);
}

uint64_t DX12ModHashShaderBytecode(const void *data, size_t size)
{
	const unsigned char *bytes = static_cast<const unsigned char*>(data);
	uint64_t hash = 14695981039346656037ull;
	for (size_t i = 0; i < size; ++i) {
		hash ^= bytes[i];
		hash *= 1099511628211ull;
	}
	return hash;
}

void DX12ModSetSafeMode(bool safeMode)
{
	InterlockedExchange(&gDx12SafeMode, safeMode ? 1 : 0);
}

static bool IsVlrBufferResizeCandidate(
	const D3D12_RESOURCE_DESC &desc, const DX12VertexLimitRaiseConfig &vlr)
{
	if (desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER || desc.Width == 0)
		return false;
	if ((desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) == 0)
		return false;
	if (desc.Width >= vlr.overrideByteWidth)
		return false;

	const UINT64 tolerance = std::max<UINT64>(
		static_cast<UINT64>(vlr.overrideByteStride) * 4096,
		vlr.overrideByteWidth / 4);
	if (desc.Width + tolerance < vlr.overrideByteWidth)
		return false;

	return true;
}

static bool AdjustBufferResourceDescCommon(D3D12_RESOURCE_DESC *desc, const char *source)
{
	if (!desc)
		return false;
	if (gHasVertexLimitRaiseConfigs == 0)
		return false;

	DX12VertexLimitRaiseConfig match;
	bool found = false;
	if (!TryAcquireSRWLockShared(&gModLock))
		return false;
	for (const DX12VertexLimitRaiseConfig &vlr : gVertexLimitRaiseConfigs) {
		if (!IsVlrBufferResizeCandidate(*desc, vlr))
			continue;
		if (!found || vlr.overrideByteWidth > match.overrideByteWidth) {
			match = vlr;
			found = true;
		}
	}
	ReleaseSRWLockShared(&gModLock);

	if (!found)
		return false;

#if defined(_DEBUG)
	const UINT64 oldWidth = desc->Width;
#endif
	desc->Width = match.overrideByteWidth;
#if defined(_DEBUG)
	DX12LogManager::DebugJsonFunc("DX12VertexLimitRaiseResize",
		"\"source\":\"%s\",\"section\":\"%S\",\"oldWidth\":%llu,\"newWidth\":%llu,\"overrideByteStride\":%u,\"overrideVertexCount\":%u,\"flags\":%u",
		source ? source : "", match.section.c_str(),
		static_cast<unsigned long long>(oldWidth),
		static_cast<unsigned long long>(desc->Width),
		match.overrideByteStride, match.overrideVertexCount,
		static_cast<unsigned>(desc->Flags));
#endif
	return true;
}

bool DX12ModAdjustBufferResourceDesc(D3D12_RESOURCE_DESC *desc, const char *source)
{
	return AdjustBufferResourceDescCommon(desc, source);
}

bool DX12ModAdjustBufferResourceDesc1(D3D12_RESOURCE_DESC1 *desc, const char *source)
{
	if (!desc)
		return false;
	D3D12_RESOURCE_DESC desc0 = {};
	desc0.Dimension = desc->Dimension;
	desc0.Alignment = desc->Alignment;
	desc0.Width = desc->Width;
	desc0.Height = desc->Height;
	desc0.DepthOrArraySize = desc->DepthOrArraySize;
	desc0.MipLevels = desc->MipLevels;
	desc0.Format = desc->Format;
	desc0.SampleDesc = desc->SampleDesc;
	desc0.Layout = desc->Layout;
	desc0.Flags = desc->Flags;
	if (!AdjustBufferResourceDescCommon(&desc0, source))
		return false;
	desc->Width = desc0.Width;
	return true;
}

bool DX12ModAdjustUavDesc(
	ID3D12Resource *resource, D3D12_UNORDERED_ACCESS_VIEW_DESC *desc, const char *source)
{
	if (!resource || !desc || desc->ViewDimension != D3D12_UAV_DIMENSION_BUFFER)
		return false;
	if (gHasVertexLimitRaiseConfigs == 0)
		return false;

	UINT bytesPerElement = desc->Buffer.StructureByteStride;
	if (!bytesPerElement && (desc->Buffer.Flags & D3D12_BUFFER_UAV_FLAG_RAW))
		bytesPerElement = 4;
	if (!bytesPerElement)
		return false;

	D3D12_RESOURCE_DESC resourceDesc = resource->GetDesc();
	if (resourceDesc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER)
		return false;

	DX12VertexLimitRaiseConfig match;
	bool found = false;
	if (!TryAcquireSRWLockShared(&gModLock))
		return false;
	for (const DX12VertexLimitRaiseConfig &vlr : gVertexLimitRaiseConfigs) {
		if (!vlr.overrideNumElements || !vlr.uavByteStride)
			continue;
		if (bytesPerElement != vlr.uavByteStride)
			continue;
		if (desc->Buffer.NumElements >= vlr.overrideNumElements)
			continue;
		const UINT64 requiredBytes =
			(desc->Buffer.FirstElement + vlr.overrideNumElements) * bytesPerElement;
		if (resourceDesc.Width < requiredBytes)
			continue;
		if (!found || vlr.overrideNumElements > match.overrideNumElements) {
			match = vlr;
			found = true;
		}
	}
	ReleaseSRWLockShared(&gModLock);

	if (!found)
		return false;

#if defined(_DEBUG)
	const UINT oldNumElements = desc->Buffer.NumElements;
#endif
	desc->Buffer.NumElements = static_cast<UINT>(match.overrideNumElements);
#if defined(_DEBUG)
	DX12LogManager::DebugJsonFunc("DX12VertexLimitRaiseUav",
		"\"source\":\"%s\",\"section\":\"%S\",\"oldNumElements\":%u,\"newNumElements\":%u,\"bytesPerElement\":%u,\"resourceWidth\":%llu",
		source ? source : "", match.section.c_str(), oldNumElements,
		desc->Buffer.NumElements, bytesPerElement,
		static_cast<unsigned long long>(resourceDesc.Width));
#endif
	return true;
}

static bool CommandListActionHasRuntimeEffect(
	const Bunny::CommandListAction &action,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *effectCache,
	std::set<std::wstring> *visiting);
static bool TextureOverrideMatchesIaBinding(
	const Bunny::TextureOverrideConfig &config, bool indexBuffer, uint32_t vertexSlot);
static void BuildDx12IaTextureOverrideIndex(
	const Bunny::TextureOverrideMap &textureOverrides,
	const std::unordered_map<std::wstring, uint32_t> &textureOverrideSectionIds,
	const std::unordered_map<std::wstring, bool> &textureOverridePreEffect,
	const std::unordered_map<std::wstring, bool> &textureOverridePostEffect,
	std::vector<DX12IaTextureOverrideCandidate> *iaTextureOverrides,
	std::unordered_map<uint32_t, std::vector<size_t>> *indexTextureOverrideIndex,
	std::unordered_map<UINT64, std::vector<size_t>> *vertexTextureOverrideIndex,
	std::unordered_map<uint32_t, std::vector<size_t>> *anyVertexTextureOverrideIndex,
	std::unordered_map<UINT64, std::vector<size_t>> *mergedVertexTextureOverrideIndex);
static void BuildDx12CompiledTextureOverridePlans(
	const Bunny::TextureOverrideMap &textureOverrides,
	const std::unordered_map<std::wstring, uint32_t> &textureOverrideSectionIds,
	const std::unordered_map<std::wstring, DX12CompiledCommandListPlan> &compiledCommandListPlans,
	std::unordered_map<uint32_t, DX12CompiledTextureOverridePlan> *compiledPlans);
static void BuildDx12CompiledCommandListPlans(
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, DX12CompiledCommandListPlan> *compiledPlans);
static void BuildDx12PreSkinTextureOverrideIndex(
	const Bunny::TextureOverrideMap &textureOverrides,
	std::vector<DX12PreSkinTextureOverrideCandidate> *preSkinTextureOverrides,
	std::unordered_map<uint32_t, std::vector<size_t>> *uavHashTextureOverrideIndex,
	std::unordered_map<uint64_t, std::vector<size_t>> *matchCsTextureOverrideIndex);
static void BuildDx12ExplicitMatchCsResourceIndex(
	const Bunny::TextureOverrideMap &textureOverrides,
	std::unordered_map<std::wstring, std::vector<std::wstring>> *resourceSections);
static void BuildDx12VlrResourceCandidates(
	const Bunny::ResourceMap &resources,
	const std::unordered_map<std::wstring, std::vector<std::wstring>> &explicitMatchCsResourceSections,
	std::vector<std::wstring> *resourceCandidates);

static bool CommandListHasRuntimeEffect(
	const std::wstring &name,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *effectCache,
	std::set<std::wstring> *visiting)
{
	if (!effectCache || !visiting)
		return true;

	auto cached = effectCache->find(name);
	if (cached != effectCache->end())
		return cached->second;
	if (!visiting->insert(name).second)
		return true;

	bool effect = false;
	auto it = commandLists.find(name);
	if (it != commandLists.end()) {
		for (const Bunny::CommandListAction &action : it->second.actions) {
			if (CommandListActionHasRuntimeEffect(
				    action, commandLists, effectCache, visiting)) {
				effect = true;
				break;
			}
		}
	}
	visiting->erase(name);
	(*effectCache)[name] = effect;
	return effect;
}

static bool CommandListHasTextureOverrideTrigger(
	const std::wstring &name,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *triggerCache,
	std::set<std::wstring> *visiting);

static bool CommandListActionHasTextureOverrideTrigger(
	const Bunny::CommandListAction &action,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *triggerCache,
	std::set<std::wstring> *visiting)
{
	switch (action.kind) {
	case Bunny::CommandListActionKind::Run:
		return CommandListHasTextureOverrideTrigger(
			action.commandList, commandLists, triggerCache, visiting);
	case Bunny::CommandListActionKind::CheckTextureOverride:
		return true;
	default:
		return false;
	}
}

static bool CommandListActionHasDescriptorTextureOverrideTrigger(
	const Bunny::CommandListAction &action,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *triggerCache,
	std::set<std::wstring> *visiting);

static bool CommandListHasTextureOverrideTrigger(
	const std::wstring &name,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *triggerCache,
	std::set<std::wstring> *visiting)
{
	if (!triggerCache || !visiting)
		return false;

	auto cached = triggerCache->find(name);
	if (cached != triggerCache->end())
		return cached->second;
	if (!visiting->insert(name).second)
		return false;

	bool triggered = false;
	auto it = commandLists.find(name);
	if (it != commandLists.end()) {
		for (const Bunny::CommandListAction &action : it->second.actions) {
			if (CommandListActionHasTextureOverrideTrigger(
				    action, commandLists, triggerCache, visiting)) {
				triggered = true;
				break;
			}
		}
	}
	visiting->erase(name);
	(*triggerCache)[name] = triggered;
	return triggered;
}

static bool CommandListLinksHaveTextureOverrideTrigger(
	const std::vector<std::wstring> &links,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *triggerCache)
{
	std::set<std::wstring> visiting;
	for (const std::wstring &link : links) {
		if (CommandListHasTextureOverrideTrigger(
			    link, commandLists, triggerCache, &visiting))
			return true;
	}
	return false;
}

static bool TargetNeedsDescriptorBindings(const Bunny::CommandListTarget &target)
{
	return target.kind == Bunny::CommandListTargetKind::ConstantBuffer ||
		target.kind == Bunny::CommandListTargetKind::ShaderResource ||
		target.kind == Bunny::CommandListTargetKind::UnorderedAccessView;
}

static bool CommandListHasDescriptorTextureOverrideTrigger(
	const std::wstring &name,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *triggerCache,
	std::set<std::wstring> *visiting)
{
	if (!triggerCache || !visiting)
		return false;

	auto cached = triggerCache->find(name);
	if (cached != triggerCache->end())
		return cached->second;
	if (!visiting->insert(name).second)
		return false;

	bool triggered = false;
	auto it = commandLists.find(name);
	if (it != commandLists.end()) {
		for (const Bunny::CommandListAction &action : it->second.actions) {
			if (CommandListActionHasDescriptorTextureOverrideTrigger(
				    action, commandLists, triggerCache, visiting)) {
				triggered = true;
				break;
			}
		}
	}
	visiting->erase(name);
	(*triggerCache)[name] = triggered;
	return triggered;
}

static bool CommandListActionHasDescriptorTextureOverrideTrigger(
	const Bunny::CommandListAction &action,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *triggerCache,
	std::set<std::wstring> *visiting)
{
	switch (action.kind) {
	case Bunny::CommandListActionKind::Run:
		return CommandListHasDescriptorTextureOverrideTrigger(
			action.commandList, commandLists, triggerCache, visiting);
	case Bunny::CommandListActionKind::CheckTextureOverride:
		return TargetNeedsDescriptorBindings(action.target);
	default:
		return false;
	}
}

static bool CommandListLinksHaveDescriptorTextureOverrideTrigger(
	const std::vector<std::wstring> &links,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *triggerCache)
{
	std::set<std::wstring> visiting;
	for (const std::wstring &link : links) {
		if (CommandListHasDescriptorTextureOverrideTrigger(
			    link, commandLists, triggerCache, &visiting))
			return true;
	}
	return false;
}

static bool ShaderOverrideHasTextureOverrideTrigger(
	const Bunny::ShaderOverrideConfig &config,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *triggerCache)
{
	std::set<std::wstring> visiting;
	for (const Bunny::CommandListAction &action : config.actions) {
		if (CommandListActionHasTextureOverrideTrigger(
			    action, commandLists, triggerCache, &visiting))
			return true;
	}
	return CommandListLinksHaveTextureOverrideTrigger(
		config.commandLists.pre, commandLists, triggerCache) ||
		CommandListLinksHaveTextureOverrideTrigger(
			config.commandLists.main, commandLists, triggerCache) ||
		CommandListLinksHaveTextureOverrideTrigger(
			config.commandLists.post, commandLists, triggerCache);
}

static bool ShaderOverridesHaveTextureOverrideTrigger(
	const Bunny::ShaderOverrideMap &shaderOverrides,
	const Bunny::CommandListMap &commandLists)
{
	std::unordered_map<std::wstring, bool> triggerCache;
	for (const auto &item : shaderOverrides) {
		if (ShaderOverrideHasTextureOverrideTrigger(
			    item.second, commandLists, &triggerCache))
			return true;
	}
	return false;
}

static bool ShaderOverrideHasDescriptorTextureOverrideTrigger(
	const Bunny::ShaderOverrideConfig &config,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *triggerCache)
{
	std::set<std::wstring> visiting;
	for (const Bunny::CommandListAction &action : config.actions) {
		if (CommandListActionHasDescriptorTextureOverrideTrigger(
			    action, commandLists, triggerCache, &visiting))
			return true;
	}
	return CommandListLinksHaveDescriptorTextureOverrideTrigger(
		config.commandLists.pre, commandLists, triggerCache) ||
		CommandListLinksHaveDescriptorTextureOverrideTrigger(
			config.commandLists.main, commandLists, triggerCache) ||
		CommandListLinksHaveDescriptorTextureOverrideTrigger(
			config.commandLists.post, commandLists, triggerCache);
}

static bool ShaderOverridesHaveDescriptorTextureOverrideTrigger(
	const Bunny::ShaderOverrideMap &shaderOverrides,
	const Bunny::CommandListMap &commandLists)
{
	std::unordered_map<std::wstring, bool> triggerCache;
	for (const auto &item : shaderOverrides) {
		if (ShaderOverrideHasDescriptorTextureOverrideTrigger(
			    item.second, commandLists, &triggerCache))
			return true;
	}
	return false;
}

static bool ShaderRegexHasTextureOverrideTrigger(
	const Bunny::ShaderRegexConfig &config,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *triggerCache)
{
	if (config.hasPattern)
		return false;
	std::set<std::wstring> visiting;
	for (const Bunny::CommandListAction &action : config.actions) {
		if (CommandListActionHasTextureOverrideTrigger(
			    action, commandLists, triggerCache, &visiting))
			return true;
	}
	return CommandListLinksHaveTextureOverrideTrigger(
		config.commandLists.pre, commandLists, triggerCache) ||
		CommandListLinksHaveTextureOverrideTrigger(
			config.commandLists.main, commandLists, triggerCache) ||
		CommandListLinksHaveTextureOverrideTrigger(
			config.commandLists.post, commandLists, triggerCache);
}

static bool ShaderRegexesHaveTextureOverrideTrigger(
	const Bunny::ShaderRegexMap &shaderRegexes,
	const Bunny::CommandListMap &commandLists)
{
	std::unordered_map<std::wstring, bool> triggerCache;
	for (const auto &item : shaderRegexes) {
		if (ShaderRegexHasTextureOverrideTrigger(
			    item.second, commandLists, &triggerCache))
			return true;
	}
	return false;
}

static bool ShaderRegexesHaveExecutableNoPatternGroup(
	const Bunny::ShaderRegexMap &shaderRegexes)
{
	for (const auto &item : shaderRegexes) {
		if (!item.second.hasPattern)
			return true;
	}
	return false;
}

static bool ShaderRegexHasDescriptorTextureOverrideTrigger(
	const Bunny::ShaderRegexConfig &config,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *triggerCache)
{
	if (config.hasPattern)
		return false;
	std::set<std::wstring> visiting;
	for (const Bunny::CommandListAction &action : config.actions) {
		if (CommandListActionHasDescriptorTextureOverrideTrigger(
			    action, commandLists, triggerCache, &visiting))
			return true;
	}
	return CommandListLinksHaveDescriptorTextureOverrideTrigger(
		config.commandLists.pre, commandLists, triggerCache) ||
		CommandListLinksHaveDescriptorTextureOverrideTrigger(
			config.commandLists.main, commandLists, triggerCache) ||
		CommandListLinksHaveDescriptorTextureOverrideTrigger(
			config.commandLists.post, commandLists, triggerCache);
}

static bool ShaderRegexesHaveDescriptorTextureOverrideTrigger(
	const Bunny::ShaderRegexMap &shaderRegexes,
	const Bunny::CommandListMap &commandLists)
{
	std::unordered_map<std::wstring, bool> triggerCache;
	for (const auto &item : shaderRegexes) {
		if (ShaderRegexHasDescriptorTextureOverrideTrigger(
			    item.second, commandLists, &triggerCache))
			return true;
	}
	return false;
}

static bool CommandListActionHasRuntimeEffect(
	const Bunny::CommandListAction &action,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *effectCache,
	std::set<std::wstring> *visiting)
{
	switch (action.kind) {
	case Bunny::CommandListActionKind::Run:
		return CommandListHasRuntimeEffect(
			action.commandList, commandLists, effectCache, visiting);
	case Bunny::CommandListActionKind::CheckTextureOverride:
	case Bunny::CommandListActionKind::HandlingSkip:
	case Bunny::CommandListActionKind::SetIndexBuffer:
	case Bunny::CommandListActionKind::SetVertexBuffer:
	case Bunny::CommandListActionKind::Draw:
	case Bunny::CommandListActionKind::DrawIndexed:
	case Bunny::CommandListActionKind::DrawFromCaller:
	case Bunny::CommandListActionKind::DrawIndexedFromCaller:
	case Bunny::CommandListActionKind::Dispatch:
		return true;
	}
	return true;
}

static bool CommandListLinksHaveRuntimeEffect(
	const std::vector<std::wstring> &links,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *effectCache)
{
	std::set<std::wstring> visiting;
	for (const std::wstring &link : links) {
		if (CommandListHasRuntimeEffect(link, commandLists, effectCache, &visiting))
			return true;
	}
	return false;
}

static bool TextureOverrideActionsHaveRuntimeEffect(
	const Bunny::TextureOverrideConfig &config,
	bool executeDrawActions,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *effectCache)
{
	if (config.actions.empty())
		return !config.indexBufferResource.empty() ||
			!config.vertexBufferResources.empty() ||
			(executeDrawActions && config.handlingSkip);

	std::set<std::wstring> visiting;
	for (const Bunny::CommandListAction &action : config.actions) {
		switch (action.kind) {
		case Bunny::CommandListActionKind::Run:
			if (CommandListHasRuntimeEffect(
				    action.commandList, commandLists, effectCache, &visiting))
				return true;
			break;
		case Bunny::CommandListActionKind::CheckTextureOverride:
		case Bunny::CommandListActionKind::SetIndexBuffer:
		case Bunny::CommandListActionKind::SetVertexBuffer:
		case Bunny::CommandListActionKind::Dispatch:
			return true;
		case Bunny::CommandListActionKind::HandlingSkip:
		case Bunny::CommandListActionKind::Draw:
		case Bunny::CommandListActionKind::DrawIndexed:
		case Bunny::CommandListActionKind::DrawFromCaller:
		case Bunny::CommandListActionKind::DrawIndexedFromCaller:
			if (executeDrawActions)
				return true;
			break;
		}
	}
	return false;
}

static void BuildDx12RuntimeEffectIndexes(
	const Bunny::TextureOverrideMap &textureOverrides,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *commandListEffect,
	std::unordered_map<std::wstring, bool> *textureOverridePreEffect,
	std::unordered_map<std::wstring, bool> *textureOverridePostEffect)
{
	if (!commandListEffect || !textureOverridePreEffect || !textureOverridePostEffect)
		return;

	commandListEffect->clear();
	textureOverridePreEffect->clear();
	textureOverridePostEffect->clear();

	for (const auto &item : commandLists) {
		std::set<std::wstring> visiting;
		CommandListHasRuntimeEffect(item.first, commandLists, commandListEffect, &visiting);
	}

	for (const auto &bucket : textureOverrides) {
		for (const Bunny::TextureOverrideConfig &config : bucket.second) {
			const bool preEffect =
				CommandListLinksHaveRuntimeEffect(
					config.commandLists.pre, commandLists, commandListEffect) ||
				CommandListLinksHaveRuntimeEffect(
					config.commandLists.main, commandLists, commandListEffect) ||
				TextureOverrideActionsHaveRuntimeEffect(
					config, true, commandLists, commandListEffect);
			const bool postEffect =
				CommandListLinksHaveRuntimeEffect(
					config.commandLists.post, commandLists, commandListEffect);
			(*textureOverridePreEffect)[config.section] = preEffect;
			(*textureOverridePostEffect)[config.section] = postEffect;
		}
	}
}

static bool LookupRuntimeEffect(
	const std::unordered_map<std::wstring, bool> &effects,
	const std::wstring &section)
{
	auto effect = effects.find(section);
	return effect == effects.end() || effect->second;
}

static void BuildDx12TextureOverrideSectionIds(
	const Bunny::TextureOverrideMap &textureOverrides,
	std::unordered_map<std::wstring, uint32_t> *sectionIds)
{
	if (!sectionIds)
		return;

	sectionIds->clear();
	uint32_t nextId = 1;
	for (const auto &bucket : textureOverrides) {
		for (const Bunny::TextureOverrideConfig &config : bucket.second) {
			if (sectionIds->find(config.section) != sectionIds->end())
				continue;
			(*sectionIds)[config.section] = nextId++;
		}
	}
}

bool DX12ModRuntimeLoad(
	const wchar_t *configPath,
	std::wstring *statusMessage,
	bool *hasWarnings,
	bool *hasErrors)
{
	Bunny::MigotoIniLoadResult iniLoad;
	Bunny::ShaderOverrideMap shaderOverrides;
	Bunny::ShaderRegexMap shaderRegexes;
	Bunny::TextureOverrideMap textureOverrides;
	Bunny::ResourceMap resources;
	Bunny::CommandListMap commandLists;
	std::unordered_map<std::wstring, bool> commandListRuntimeEffect;
	std::unordered_map<std::wstring, bool> textureOverridePreRuntimeEffect;
	std::unordered_map<std::wstring, bool> textureOverridePostRuntimeEffect;
	std::unordered_map<std::wstring, uint32_t> textureOverrideSectionIds;
	std::unordered_map<uint32_t, DX12CompiledTextureOverridePlan> compiledTextureOverridePlans;
	std::unordered_map<std::wstring, DX12CompiledCommandListPlan> compiledCommandListPlans;
	bool presentRuntimeEffect = false;
	std::vector<DX12IaTextureOverrideCandidate> iaTextureOverrides;
	std::unordered_map<uint32_t, std::vector<size_t>> iaIndexTextureOverrideIndex;
	std::unordered_map<UINT64, std::vector<size_t>> iaVertexTextureOverrideIndex;
	std::unordered_map<uint32_t, std::vector<size_t>> iaAnyVertexTextureOverrideIndex;
	std::unordered_map<UINT64, std::vector<size_t>> iaMergedVertexTextureOverrideIndex;
	std::vector<DX12PreSkinTextureOverrideCandidate> preSkinTextureOverrides;
	std::unordered_map<uint32_t, std::vector<size_t>> preSkinUavHashTextureOverrideIndex;
	std::unordered_map<uint64_t, std::vector<size_t>> preSkinMatchCsTextureOverrideIndex;
	std::unordered_map<std::wstring, std::vector<std::wstring>> explicitMatchCsResourceSections;
	std::vector<std::wstring> vlrResourceCandidates;
	std::unordered_set<uint64_t> preSkinMatchCsHashes;
	bool hasPreSkinVlrWithoutMatchCs = false;
	size_t loadedShaderOverrideCount = 0;
	size_t loadedShaderRegexCount = 0;
	size_t loadedTextureOverrideCount = 0;
	size_t loadedResourceCount = 0;
	size_t loadedCommandListCount = 0;
	bool safeModeActive = false;
	bool activeShaderOverrides = false;
	bool activeTextureOverrides = false;
	bool preSkinProbeEnabled = false;
	bool loadedPreSkinTextureOverrideCandidates = false;
	bool loadedShaderTriggeredTextureOverrides = false;
	bool loadedPresentRuntimeEffect = false;
	std::wstring loadedShaderFixesDir;
	std::vector<DX12VertexLimitRaiseConfig> loadedVertexLimitRaiseConfigs;
	UINT64 loadedGeneration = 0;
	
	SetBasePaths(configPath);
	if (!Bunny::LoadMigotoIniWithIncludes(configPath, &iniLoad)) {
		DX12LogManager::JsonFunc("DX12ModRuntime",
			"\"status\":\"config_load_failed\",\"path\":\"%S\",\"error\":\"%S\"",
			configPath ? configPath : L"", iniLoad.document.Error().c_str());
		if (statusMessage)
			*statusMessage = L"DX12 mod config load failed: " + iniLoad.document.Error();
		if (hasWarnings)
			*hasWarnings = true;
		if (hasErrors)
			*hasErrors = true;
		return false;
	}

	const bool hasIniErrors = !iniLoad.errors.empty();
	Bunny::ParseShaderOverrideSections(iniLoad.document, &shaderOverrides);
	Bunny::ParseShaderRegexSections(iniLoad.document, &shaderRegexes);
	Bunny::ParseTextureOverrideSections(iniLoad.document, &textureOverrides);
	Bunny::ParseResourceSections(iniLoad.document, &resources);
	Bunny::ParseCommandListSections(iniLoad.document, &commandLists);
	std::vector<DX12VertexLimitRaiseConfig> vertexLimitRaiseConfigs =
		CollectVertexLimitRaiseConfigs(textureOverrides);
	bool hasPreSkinTextureOverrideCandidates =
		HasPreSkinTextureOverrideCandidates(
			textureOverrides, &preSkinMatchCsHashes,
			&hasPreSkinVlrWithoutMatchCs,
			nullptr);
	bool hasShaderTriggeredTextureOverrides =
		!textureOverrides.empty() &&
		(ShaderOverridesHaveTextureOverrideTrigger(shaderOverrides, commandLists) ||
		 ShaderRegexesHaveTextureOverrideTrigger(shaderRegexes, commandLists));
	bool hasExecutableShaderRegexes =
		ShaderRegexesHaveExecutableNoPatternGroup(shaderRegexes);
	bool hasShaderDescriptorTextureOverrideTriggers =
		hasShaderTriggeredTextureOverrides &&
		(ShaderOverridesHaveDescriptorTextureOverrideTrigger(shaderOverrides, commandLists) ||
		 ShaderRegexesHaveDescriptorTextureOverrideTrigger(shaderRegexes, commandLists));
	BuildDx12RuntimeEffectIndexes(
		textureOverrides, commandLists,
		&commandListRuntimeEffect,
		&textureOverridePreRuntimeEffect,
		&textureOverridePostRuntimeEffect);
	{
		std::set<std::wstring> visiting;
		presentRuntimeEffect =
			CommandListHasRuntimeEffect(
				L"Present", commandLists, &commandListRuntimeEffect, &visiting);
	}
	BuildDx12TextureOverrideSectionIds(
		textureOverrides, &textureOverrideSectionIds);
	BuildDx12IaTextureOverrideIndex(
		textureOverrides,
		textureOverrideSectionIds,
		textureOverridePreRuntimeEffect,
		textureOverridePostRuntimeEffect,
		&iaTextureOverrides,
		&iaIndexTextureOverrideIndex,
		&iaVertexTextureOverrideIndex,
		&iaAnyVertexTextureOverrideIndex,
		&iaMergedVertexTextureOverrideIndex);
	BuildDx12CompiledCommandListPlans(
		commandLists, &compiledCommandListPlans);
	BuildDx12CompiledTextureOverridePlans(
		textureOverrides, textureOverrideSectionIds, compiledCommandListPlans,
		&compiledTextureOverridePlans);
	BuildDx12PreSkinTextureOverrideIndex(
		textureOverrides,
		&preSkinTextureOverrides,
		&preSkinUavHashTextureOverrideIndex,
		&preSkinMatchCsTextureOverrideIndex);
	BuildDx12ExplicitMatchCsResourceIndex(
		textureOverrides, &explicitMatchCsResourceSections);
	BuildDx12VlrResourceCandidates(
		resources, explicitMatchCsResourceSections, &vlrResourceCandidates);

	AcquireSRWLockExclusive(&gModLock);
	gShaderOverrides.swap(shaderOverrides);
	gShaderRegexes.swap(shaderRegexes);
	gTextureOverrides.swap(textureOverrides);
	gResources.swap(resources);
	gCommandLists.swap(commandLists);
	gVlrResourceCandidates.swap(vlrResourceCandidates);
	gCommandListRuntimeEffect.swap(commandListRuntimeEffect);
	gTextureOverridePreRuntimeEffect.swap(textureOverridePreRuntimeEffect);
	gTextureOverridePostRuntimeEffect.swap(textureOverridePostRuntimeEffect);
	gTextureOverrideSectionIds.swap(textureOverrideSectionIds);
	gIaTextureOverrides.swap(iaTextureOverrides);
	gIaIndexTextureOverrideIndex.swap(iaIndexTextureOverrideIndex);
	gIaVertexTextureOverrideIndex.swap(iaVertexTextureOverrideIndex);
	gIaAnyVertexTextureOverrideIndex.swap(iaAnyVertexTextureOverrideIndex);
	gIaMergedVertexTextureOverrideIndex.swap(iaMergedVertexTextureOverrideIndex);
	gTextureOverrideLookupCache.clear();
	gCompiledCommandListPlans.swap(compiledCommandListPlans);
	gCompiledTextureOverridePlans.swap(compiledTextureOverridePlans);
	gPreSkinTextureOverrides.swap(preSkinTextureOverrides);
	gPreSkinUavHashTextureOverrideIndex.swap(preSkinUavHashTextureOverrideIndex);
	gPreSkinMatchCsTextureOverrideIndex.swap(preSkinMatchCsTextureOverrideIndex);
	gExplicitMatchCsResourceSections.swap(explicitMatchCsResourceSections);
	gVertexLimitRaiseConfigs.swap(vertexLimitRaiseConfigs);
	InterlockedExchange(
		&gHasVertexLimitRaiseConfigs,
		gVertexLimitRaiseConfigs.empty() ? 0 : 1);
	gPreSkinMatchCsHashes.swap(preSkinMatchCsHashes);
	gHasPreSkinVlrWithoutMatchCs = hasPreSkinVlrWithoutMatchCs;
	ReleaseLoadedResourcesLocked();
	gShaderOverridePsoMatchCache.clear();
	gPresentCommandListExecutedPresent = -1;
	gPreSkinSectionAppliedPresent.clear();
	gPreSkinCbvReadCache.clear();
	gPreSkinSrvNegativeCache.clear();
	gPreSkinSrvStableNegativeCache.clear();
	gPreSkinDescriptorPatchAbortCache.clear();
	gPreSkinSrvPositiveCache.clear();
	gPreSkinSrvStablePositiveCache.clear();
	InterlockedExchange(&gPreSkinDescriptorAbortCacheHitLogCount, 0);
	gPreSkinUavMatchCache.clear();
	gPreSkinMatchCsInputCache.clear();
	gPreSkinMatchCsProbeKeys.clear();
	InterlockedExchange(&gPreSkinMatchCsProbeLogCount, 0);
	BumpPreSkinActiveGeneration();
	++gPreSkinSrvCacheGeneration;
	InterlockedExchange(&gHasShaderOverrides, gShaderOverrides.empty() ? 0 : 1);
	InterlockedExchange(&gHasShaderRegexes, hasExecutableShaderRegexes ? 1 : 0);
	InterlockedExchange(&gHasTextureOverrides, gTextureOverrides.empty() ? 0 : 1);
	InterlockedExchange(
		&gHasPreSkinTextureOverrideCandidates,
		hasPreSkinTextureOverrideCandidates ? 1 : 0);
	InterlockedExchange(
		&gHasShaderTriggeredTextureOverrides,
		hasShaderTriggeredTextureOverrides ? 1 : 0);
	InterlockedExchange(
		&gHasShaderDescriptorTextureOverrideTriggers,
		hasShaderDescriptorTextureOverrideTriggers ? 1 : 0);
	InterlockedExchange(&gHasPresentRuntimeEffect, presentRuntimeEffect ? 1 : 0);
	gLoaded = true;
	++gReloadGeneration;
	InterlockedExchange64(
		reinterpret_cast<volatile LONG64*>(&gShaderOverrideNegativeCacheGeneration),
		static_cast<LONG64>(gReloadGeneration));
	loadedShaderOverrideCount = gShaderOverrides.size();
	loadedShaderRegexCount = gShaderRegexes.size();
	loadedTextureOverrideCount = gTextureOverrides.size();
	loadedResourceCount = gResources.size();
	loadedCommandListCount = gCommandLists.size();
	loadedPreSkinTextureOverrideCandidates = hasPreSkinTextureOverrideCandidates;
	loadedShaderTriggeredTextureOverrides = hasShaderTriggeredTextureOverrides;
	loadedPresentRuntimeEffect = presentRuntimeEffect;
	safeModeActive = InterlockedCompareExchange(&gDx12SafeMode, 0, 0) != 0;
	activeShaderOverrides = !safeModeActive &&
		(!gShaderOverrides.empty() || hasExecutableShaderRegexes);
	activeTextureOverrides =
		!safeModeActive && !gTextureOverrides.empty() &&
		hasShaderTriggeredTextureOverrides;
	preSkinProbeEnabled =
		!safeModeActive && hasShaderTriggeredTextureOverrides &&
		hasPreSkinTextureOverrideCandidates;
	loadedShaderFixesDir = gShaderFixesDir;
	loadedVertexLimitRaiseConfigs = gVertexLimitRaiseConfigs;
	loadedGeneration = gReloadGeneration;
	for (auto &item : gPsoRecords) {
		if (item.second.replacement) {
			item.second.replacement->Release();
			item.second.replacement = nullptr;
		}
		item.second.replacementGeneration = 0;
		item.second.skipGeneration = 0;
	}
	ReleaseSRWLockExclusive(&gModLock);
	ClearPreSkinRuntimeState();

	DX12LogManager::JsonFunc("DX12ModRuntime",
		"\"status\":\"loaded\",\"path\":\"%S\",\"iniFiles\":%zu,\"errors\":%zu,\"warnings\":%zu,\"shaderOverrides\":%zu,\"shaderRegexes\":%zu,\"textureOverrides\":%zu,\"resources\":%zu,\"commandLists\":%zu,\"safeMode\":%s,\"activeShaderOverrides\":%s,\"activeTextureOverrides\":%s,\"shaderTriggeredTextureOverrides\":%s,\"descriptorTextureOverrideTriggers\":%s,\"preSkinCandidates\":%s,\"preSkinProbeEnabled\":%s,\"presentRuntimeEffect\":%s,\"shaderFixes\":\"%S\",\"generation\":%llu",
		configPath ? configPath : L"", iniLoad.loadedFiles.size(), iniLoad.errors.size(), iniLoad.warnings.size(),
		loadedShaderOverrideCount, loadedShaderRegexCount, loadedTextureOverrideCount, loadedResourceCount, loadedCommandListCount,
		safeModeActive ? "true" : "false",
		activeShaderOverrides ? "true" : "false",
		activeTextureOverrides ? "true" : "false",
		loadedShaderTriggeredTextureOverrides ? "true" : "false",
		hasShaderDescriptorTextureOverrideTriggers ? "true" : "false",
		loadedPreSkinTextureOverrideCandidates ? "true" : "false",
		preSkinProbeEnabled ? "true" : "false",
		loadedPresentRuntimeEffect ? "true" : "false",
		loadedShaderFixesDir.c_str(),
		static_cast<unsigned long long>(loadedGeneration));
	if (safeModeActive &&
	    (loadedShaderOverrideCount || loadedShaderRegexCount ||
	     loadedTextureOverrideCount || loadedPresentRuntimeEffect)) {
		DX12LogManager::JsonFunc("DX12ModRuntimeDisabled",
			"\"reason\":\"safe_mode\",\"shaderOverrides\":%zu,\"shaderRegexes\":%zu,\"textureOverrides\":%zu,\"preSkinCandidates\":%s,\"presentRuntimeEffect\":%s",
			loadedShaderOverrideCount, loadedShaderRegexCount, loadedTextureOverrideCount,
			loadedPreSkinTextureOverrideCandidates ? "true" : "false",
			loadedPresentRuntimeEffect ? "true" : "false");
	}
	for (const std::wstring &loadedFile : iniLoad.loadedFiles) {
		DX12LogManager::JsonFunc("DX12ModRuntimeIni",
			"\"status\":\"loaded\",\"path\":\"%S\"", loadedFile.c_str());
	}
	for (const std::wstring &error : iniLoad.errors) {
		DX12LogManager::JsonFunc("DX12ModRuntimeIni",
			"\"status\":\"error\",\"message\":\"%S\"", error.c_str());
	}
	for (const std::wstring &warning : iniLoad.warnings) {
		DX12LogManager::JsonFunc("DX12ModRuntimeIni",
			"\"status\":\"warning\",\"message\":\"%S\"", warning.c_str());
	}
	for (const DX12VertexLimitRaiseConfig &vlr : loadedVertexLimitRaiseConfigs) {
		DX12LogManager::JsonFunc("DX12VertexLimitRaiseConfig",
			"\"section\":\"%S\",\"overrideByteStride\":%u,\"overrideVertexCount\":%u,\"overrideByteWidth\":%llu,\"uavByteStride\":%u,\"overrideNumElements\":%llu",
			vlr.section.c_str(), vlr.overrideByteStride, vlr.overrideVertexCount,
			static_cast<unsigned long long>(vlr.overrideByteWidth),
			vlr.uavByteStride, static_cast<unsigned long long>(vlr.overrideNumElements));
	}
	if (statusMessage) {
		wchar_t text[256];
		if (hasIniErrors) {
			swprintf_s(text, L"F10 DX12 mod config reloaded with %zu error(s)",
				iniLoad.errors.size());
		} else if (iniLoad.warnings.empty()) {
			swprintf_s(text, L"F10 DX12 mod config reloaded: %zu ini, %zu TextureOverride, %zu Resource",
				iniLoad.loadedFiles.size(), loadedTextureOverrideCount, loadedResourceCount);
		} else {
			swprintf_s(text, L"F10 DX12 mod config reloaded with %zu warning(s)",
				iniLoad.warnings.size());
		}
		*statusMessage = text;
	}
	if (hasWarnings)
		*hasWarnings = hasIniErrors || !iniLoad.warnings.empty();
	if (hasErrors)
		*hasErrors = hasIniErrors;
	return true;
}

bool DX12ModRuntimeReload()
{
	std::wstring configPath;
	AcquireSRWLockShared(&gModLock);
	configPath = gConfigPath;
	ReleaseSRWLockShared(&gModLock);
	if (configPath.empty()) {
		DX12LogManager::JsonFunc("DX12ModRuntimeReload", "\"status\":\"skipped\",\"reason\":\"empty_config_path\"");
		return false;
	}
	DX12LogManager::JsonFunc("DX12ModRuntimeReload", "\"status\":\"begin\",\"path\":\"%S\"", configPath.c_str());
	std::wstring status;
	bool hasWarnings = false;
	bool hasErrors = false;
	if (!DX12ModRuntimeLoad(configPath.c_str(), &status, &hasWarnings, &hasErrors)) {
		DX12OverlayStatusState::SetError(status.c_str());
		return false;
	}
	if (hasErrors)
		DX12OverlayStatusState::SetError(status.c_str());
	else if (hasWarnings)
		DX12OverlayStatusState::SetWarning(status.c_str());
	else
		DX12OverlayStatusState::SetStatus(status.c_str());
	return true;
}

