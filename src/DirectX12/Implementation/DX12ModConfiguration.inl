class DX12StoredPso
{
public:
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
	UINT64 replacementNoopGeneration = 0;
	bool skipDraw = false;
	bool skipDispatch = false;
	UINT64 skipGeneration = 0;

	// PSO-Create-Time compiled Mod Plan: eliminates per-draw config rediscovery.
	// Computed when the PSO is first recorded (RecordGraphicsPipelineState).
	// After mod reload, generation mismatch triggers lazy rebuild on first access.
	DX12IaHashTargetPlan cachedDrawTargetPlan;
	UINT64 drawTargetPlanGeneration = 0;
	bool drawTargetPlanResolved = false;
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

class DX12PreSkinCandidateIdentityUtils
{
public:
	static UINT64 HashWideString(UINT64 hash, const std::wstring &value)
	{
		std::wstring normalized =
			Bunny::IniDocumentText::ToLower(Bunny::IniDocumentText::Trim(value));
		hash = HashCombine64(hash, normalized.size());
		for (wchar_t ch : normalized)
			hash = HashCombine64(hash, static_cast<UINT64>(ch));
		return hash;
	}

	static UINT64 Build(
		const Bunny::TextureOverrideConfig &config, uint32_t bucketHash,
		UINT64 order)
	{
		UINT64 identity = 0x84d56f03b18a6c25ull;
		identity = HashCombine64(identity, bucketHash);
		identity = HashCombine64(identity, config.hash);
		identity = HashCombine64(identity, GetPreSkinCsHash(config));
		identity = HashCombine64(identity, config.hasCsHash ? 1 : 0);
		identity = HashCombine64(identity, config.hasMatchCs ? 1 : 0);
		identity = HashCombine64(identity, config.hasMatchPriority ? 1 : 0);
		identity = HashCombine64(identity, static_cast<UINT64>(config.matchPriority));
		identity = HashWideString(identity, config.section);
		identity = HashWideString(identity, config.originalSection);
		identity = HashWideString(identity, config.sourcePath);
		identity = HashWideString(identity, config.sourceDir);
		identity = HashWideString(identity, config.iniNamespace);
		for (const auto &expected : config.preSkinMatchCsSrvHashes) {
			identity = HashCombine64(identity, expected.first);
			identity = HashCombine64(identity, expected.second);
		}
		for (const auto &replacement : config.preSkinCsSrvResources) {
			identity = HashCombine64(identity, replacement.first);
			identity = HashWideString(identity, replacement.second);
		}
		identity = HashCombine64(identity, order);
		return identity ? identity : 0x6c8e9cf570932bd1ull;
	}
};

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
			candidate.config = &config;
			candidate.hash = bucket.first;
			candidate.order = order++;
			candidate.identity = DX12PreSkinCandidateIdentityUtils::Build(
				config, bucket.first, candidate.order);
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
				(*resourceSections)[Bunny::IniDocumentText::ToLower(item.second)].push_back(config.section);
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
	return gExplicitMatchCsResourceSections.find(Bunny::IniDocumentText::ToLower(resourceName)) !=
		gExplicitMatchCsResourceSections.end();
}

static bool ResourceBlockedByInactiveExplicitMatchCsLocked(
	const std::wstring &resourceName,
	const std::unordered_set<std::wstring> &activePreSkinSections)
{
	if (resourceName.empty())
		return false;

	auto indexed = gExplicitMatchCsResourceSections.find(Bunny::IniDocumentText::ToLower(resourceName));
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
		if (explicitMatchCsResourceSections.find(Bunny::IniDocumentText::ToLower(item.first)) !=
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
	ClearPreSkinOutputResizeCache();
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
	DX12LogManager::DebugFields("DX12VertexLimitRaiseResize",
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
	DX12LogManager::DebugFields("DX12VertexLimitRaiseUav",
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
static void BuildDx12CompiledShaderOverridePlans(
	const Bunny::ShaderOverrideMap &shaderOverrides,
	const Bunny::ShaderRegexMap &shaderRegexes,
	const std::unordered_map<std::wstring, DX12CompiledCommandListPlan> &compiledCommandListPlans,
	std::unordered_map<std::wstring, DX12CompiledShaderOverridePlan> *compiledPlans);
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
static std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot>
BuildDx12CommandExecutionRuntimeSnapshot(
	const Bunny::CommandListMap &commandLists,
	const std::unordered_map<std::wstring, bool> &commandListRuntimeEffect,
	const std::unordered_map<std::wstring, uint32_t> &textureOverrideSectionIds,
	const std::unordered_map<std::wstring, DX12CompiledCommandListPlan> &compiledCommandListPlans,
	const std::unordered_map<uint32_t, DX12CompiledTextureOverridePlan> &compiledTextureOverridePlans);

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

static bool CommandListActionHasIaTextureOverrideTrigger(
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

static bool TargetNeedsIaBindings(const Bunny::CommandListTarget &target)
{
	return target.kind == Bunny::CommandListTargetKind::IndexBuffer ||
		target.kind == Bunny::CommandListTargetKind::VertexBuffer;
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

static bool CommandListHasIaTextureOverrideTrigger(
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
			if (CommandListActionHasIaTextureOverrideTrigger(
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

static bool CommandListActionHasIaTextureOverrideTrigger(
	const Bunny::CommandListAction &action,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *triggerCache,
	std::set<std::wstring> *visiting)
{
	switch (action.kind) {
	case Bunny::CommandListActionKind::Run:
		return CommandListHasIaTextureOverrideTrigger(
			action.commandList, commandLists, triggerCache, visiting);
	case Bunny::CommandListActionKind::CheckTextureOverride:
		return TargetNeedsIaBindings(action.target);
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

static bool CommandListLinksHaveIaTextureOverrideTrigger(
	const std::vector<std::wstring> &links,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *triggerCache)
{
	std::set<std::wstring> visiting;
	for (const std::wstring &link : links) {
		if (CommandListHasIaTextureOverrideTrigger(
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

static bool ShaderOverrideHasIaTextureOverrideTrigger(
	const Bunny::ShaderOverrideConfig &config,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *triggerCache)
{
	std::set<std::wstring> visiting;
	for (const Bunny::CommandListAction &action : config.actions) {
		if (CommandListActionHasIaTextureOverrideTrigger(
			    action, commandLists, triggerCache, &visiting))
			return true;
	}
	return CommandListLinksHaveIaTextureOverrideTrigger(
		config.commandLists.pre, commandLists, triggerCache) ||
		CommandListLinksHaveIaTextureOverrideTrigger(
			config.commandLists.main, commandLists, triggerCache) ||
		CommandListLinksHaveIaTextureOverrideTrigger(
			config.commandLists.post, commandLists, triggerCache);
}

static bool ShaderOverridesHaveIaTextureOverrideTrigger(
	const Bunny::ShaderOverrideMap &shaderOverrides,
	const Bunny::CommandListMap &commandLists)
{
	std::unordered_map<std::wstring, bool> triggerCache;
	for (const auto &item : shaderOverrides) {
		if (ShaderOverrideHasIaTextureOverrideTrigger(
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

static bool ShaderRegexHasIaTextureOverrideTrigger(
	const Bunny::ShaderRegexConfig &config,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *triggerCache)
{
	if (config.hasPattern)
		return false;
	std::set<std::wstring> visiting;
	for (const Bunny::CommandListAction &action : config.actions) {
		if (CommandListActionHasIaTextureOverrideTrigger(
			    action, commandLists, triggerCache, &visiting))
			return true;
	}
	return CommandListLinksHaveIaTextureOverrideTrigger(
		config.commandLists.pre, commandLists, triggerCache) ||
		CommandListLinksHaveIaTextureOverrideTrigger(
			config.commandLists.main, commandLists, triggerCache) ||
		CommandListLinksHaveIaTextureOverrideTrigger(
			config.commandLists.post, commandLists, triggerCache);
}

static bool ShaderRegexesHaveIaTextureOverrideTrigger(
	const Bunny::ShaderRegexMap &shaderRegexes,
	const Bunny::CommandListMap &commandLists)
{
	std::unordered_map<std::wstring, bool> triggerCache;
	for (const auto &item : shaderRegexes) {
		if (ShaderRegexHasIaTextureOverrideTrigger(
			    item.second, commandLists, &triggerCache))
			return true;
	}
	return false;
}

static bool TextureOverridesHaveDrawSrvReplacement(
	const Bunny::TextureOverrideMap &textureOverrides)
{
	for (Bunny::TextureOverrideMap::const_iterator bucket = textureOverrides.begin();
	     bucket != textureOverrides.end(); ++bucket) {
		const Bunny::TextureOverrideList &configs = bucket->second;
		for (size_t index = 0; index < configs.size(); ++index) {
			const Bunny::TextureOverrideConfig &config = configs[index];
			if (!config.drawPsSrvResources.empty() ||
			    !config.drawVsSrvResources.empty() ||
			    !config.drawReplaceThisResource.empty())
				return true;
		}
	}
	return false;
}

static bool CompiledTextureOverridePlansNeedResidentIaState(
	const std::unordered_map<uint32_t, DX12CompiledTextureOverridePlan> &plans)
{
	for (const auto &entry : plans) {
		if (entry.second.NeedsResidentIaState())
			return true;
	}
	return false;
}

static bool CompiledShaderOverridePlansNeedResidentIaState(
	const std::unordered_map<std::wstring, DX12CompiledShaderOverridePlan> &plans)
{
	for (const auto &entry : plans) {
		if (entry.second.NeedsResidentIaState())
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
				!config.drawPsSrvResources.empty() ||
				!config.drawVsSrvResources.empty() ||
				!config.drawReplaceThisResource.empty() ||
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

static std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot>
BuildDx12CommandExecutionRuntimeSnapshot(
	const Bunny::CommandListMap &commandLists,
	const std::unordered_map<std::wstring, bool> &commandListRuntimeEffect,
	const std::unordered_map<std::wstring, uint32_t> &textureOverrideSectionIds,
	const std::unordered_map<std::wstring, DX12CompiledCommandListPlan> &compiledCommandListPlans,
	const std::unordered_map<uint32_t, DX12CompiledTextureOverridePlan> &compiledTextureOverridePlans,
	const std::unordered_map<std::wstring, DX12CompiledShaderOverridePlan> &compiledShaderOverridePlans)
{
	std::shared_ptr<DX12CommandExecutionRuntimeSnapshot> snapshot =
		std::make_shared<DX12CommandExecutionRuntimeSnapshot>();
	snapshot->commandListRuntimeEffect = commandListRuntimeEffect;
	for (const auto &item : commandLists) {
		snapshot->commandLists.emplace(
			item.first,
			std::make_shared<const Bunny::CommandListConfig>(item.second));
	}
	for (const auto &item : compiledCommandListPlans) {
		if (item.second.unsupported)
			continue;
		snapshot->compiledCommandListPlans.emplace(
			item.first,
			std::make_shared<const DX12CompiledCommandListPlan>(item.second));
	}
	for (const auto &item : textureOverrideSectionIds) {
		if (!item.second)
			continue;
		auto planIt = compiledTextureOverridePlans.find(item.second);
		if (planIt == compiledTextureOverridePlans.end())
			continue;
		snapshot->compiledTextureOverridePlans.emplace(
			item.first,
			std::make_shared<const DX12CompiledTextureOverridePlan>(planIt->second));
	}
	for (const auto &item : compiledShaderOverridePlans) {
		snapshot->compiledShaderOverridePlans.emplace(
			item.first,
			std::make_shared<const DX12CompiledShaderOverridePlan>(item.second));
	}
	return snapshot;
}

class DX12ConstantsVmInitializerUtils
{
public:
	static void Initialize(const Bunny::CommandListMap &commandLists)
	{
		MigotoCommandVmUtils::ClearGlobals();
		const Bunny::CommandListConfig *constants = FindConstants(commandLists);
		if (!constants)
			return;

		MigotoCommandVmUtils::State state;
		state.PushLocalScope();
		for (const Bunny::CommandListAction &action : constants->actions)
			ApplyAction(&state, action);
		state.PopLocalScope();
	}

	static const Bunny::CommandListConfig *FindConstants(
		const Bunny::CommandListMap &commandLists)
	{
		for (const auto &item : commandLists) {
			if (Bunny::IniDocumentText::ToLower(item.first) == L"constants")
				return &item.second;
		}
		return nullptr;
	}

	static void ApplyAction(
		MigotoCommandVmUtils::State *state,
		const Bunny::CommandListAction &action)
	{
		if (!state)
			return;
		if (action.kind == Bunny::CommandListActionKind::ControlFlow) {
			state->ApplyControlFlow(action.rawLine.empty() ? action.key : action.rawLine);
			return;
		}
		if (!state->Active())
			return;

		switch (action.kind) {
		case Bunny::CommandListActionKind::VariableAssignment:
		case Bunny::CommandListActionKind::PostVariableAssignment:
			state->Assign(action.key, action.value);
			break;
		case Bunny::CommandListActionKind::LocalVariableAssignment:
			state->DeclareLocal(action.key, action.value);
			break;
		case Bunny::CommandListActionKind::SetShaderConstant:
			state->AssignSymbol(action.key, action.value);
			break;
		default:
			break;
		}
	}
};

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
	std::vector<Bunny::MigotoKeyBindingUtils::Binding> keyBindings;
	std::unordered_map<std::wstring, bool> commandListRuntimeEffect;
	std::unordered_map<std::wstring, bool> textureOverridePreRuntimeEffect;
	std::unordered_map<std::wstring, bool> textureOverridePostRuntimeEffect;
	std::unordered_map<std::wstring, uint32_t> textureOverrideSectionIds;
	std::unordered_map<uint32_t, DX12CompiledTextureOverridePlan> compiledTextureOverridePlans;
	std::unordered_map<std::wstring, DX12CompiledShaderOverridePlan> compiledShaderOverridePlans;
	std::unordered_map<std::wstring, DX12CompiledCommandListPlan> compiledCommandListPlans;
	std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> commandExecutionRuntimeSnapshot;
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
	if (!Bunny::MigotoIniLoader::LoadWithIncludes(configPath, &iniLoad)) {
		DX12LogManager::Fields("DX12ModRuntime",
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
	Bunny::MigotoShaderOverrideParser::ParseSections(iniLoad.document, &shaderOverrides);
	Bunny::MigotoShaderRegexParser::ParseSections(iniLoad.document, &shaderRegexes);
	Bunny::MigotoTextureOverrideParser::ParseSections(iniLoad.document, &textureOverrides);
	Bunny::MigotoResourceParser::ParseSections(iniLoad.document, &resources);
	Bunny::MigotoCommandListParser::ParseSections(iniLoad.document, &commandLists);
	Bunny::MigotoKeyBindingUtils::ParseSections(iniLoad.document, &keyBindings);
	DX12ConstantsVmInitializerUtils::Initialize(commandLists);

	DX12LogManager::DebugFields("DX12ModRuntimeLoadParse",
		"\"shaderOverrides\":%zu,\"shaderRegexes\":%zu,\"textureOverrides\":%zu,\"resources\":%zu,\"commandLists\":%zu,\"keyBindings\":%zu",
		shaderOverrides.size(), shaderRegexes.size(), textureOverrides.size(), resources.size(), commandLists.size(), keyBindings.size());

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
	bool hasDrawSrvTextureReplacementOverrides =
		TextureOverridesHaveDrawSrvReplacement(textureOverrides);
	bool hasExecutableShaderRegexes =
		ShaderRegexesHaveExecutableNoPatternGroup(shaderRegexes);
	bool hasShaderIaTextureOverrideTriggers =
		!textureOverrides.empty() &&
		(ShaderOverridesHaveIaTextureOverrideTrigger(shaderOverrides, commandLists) ||
		 ShaderRegexesHaveIaTextureOverrideTrigger(shaderRegexes, commandLists));

	DX12LogManager::DebugFields("DX12ModRuntimeLoadFlags",
		"\"hasShaderTriggeredTextureOverrides\":%s,\"hasExecutableShaderRegexes\":%s,\"hasPreSkinCandidates\":%s,\"hasDrawSrvTextureReplacementOverrides\":%s,\"hasShaderIaTextureOverrideTriggers\":%s",
		hasShaderTriggeredTextureOverrides ? "true" : "false",
		hasExecutableShaderRegexes ? "true" : "false",
		hasPreSkinTextureOverrideCandidates ? "true" : "false",
		hasDrawSrvTextureReplacementOverrides ? "true" : "false",
		hasShaderIaTextureOverrideTriggers ? "true" : "false");

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
	BuildDx12CompiledShaderOverridePlans(
		shaderOverrides, shaderRegexes, compiledCommandListPlans,
		&compiledShaderOverridePlans);
	bool hasIaResidentStateRequirements =
		hasShaderIaTextureOverrideTriggers ||
		CompiledTextureOverridePlansNeedResidentIaState(compiledTextureOverridePlans) ||
		CompiledShaderOverridePlansNeedResidentIaState(compiledShaderOverridePlans);
	commandExecutionRuntimeSnapshot = BuildDx12CommandExecutionRuntimeSnapshot(
		commandLists,
		commandListRuntimeEffect,
		textureOverrideSectionIds,
		compiledCommandListPlans,
		compiledTextureOverridePlans,
		compiledShaderOverridePlans);
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
	gModResources.swap(resources);
	gCommandLists.swap(commandLists);
	gKeyBindings.swap(keyBindings);
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
	gIaTextureOverrideHashNoCandidates.clear();
	DX12ModClearIaReplacementRuntimeCaches();
	gCompiledCommandListPlans.swap(compiledCommandListPlans);
	gCompiledTextureOverridePlans.swap(compiledTextureOverridePlans);
	std::atomic_store_explicit(
		&gCommandExecutionRuntimeSnapshot,
		commandExecutionRuntimeSnapshot,
		std::memory_order_release);
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
	gPreSkinUavMatchCacheLru.clear();
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
	InterlockedExchange(
		&gHasDrawSrvTextureReplacementOverrides,
		hasDrawSrvTextureReplacementOverrides ? 1 : 0);
	InterlockedExchange(
		&gHasIaResidentStateRequirements,
		hasIaResidentStateRequirements ? 1 : 0);
	InterlockedExchange(&gHasPresentRuntimeEffect, presentRuntimeEffect ? 1 : 0);
	gLoaded = true;
	++gReloadGeneration;
	InterlockedExchange64(
		reinterpret_cast<volatile LONG64*>(&gShaderOverrideNegativeCacheGeneration),
		static_cast<LONG64>(gReloadGeneration));
	loadedShaderOverrideCount = gShaderOverrides.size();
	loadedShaderRegexCount = gShaderRegexes.size();
	loadedTextureOverrideCount = gTextureOverrides.size();
	loadedResourceCount = gModResources.size();
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

	DX12LogManager::Fields("DX12ModRuntime",
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
		DX12LogManager::Fields("DX12ModRuntimeDisabled",
			"\"reason\":\"safe_mode\",\"shaderOverrides\":%zu,\"shaderRegexes\":%zu,\"textureOverrides\":%zu,\"preSkinCandidates\":%s,\"presentRuntimeEffect\":%s",
			loadedShaderOverrideCount, loadedShaderRegexCount, loadedTextureOverrideCount,
			loadedPreSkinTextureOverrideCandidates ? "true" : "false",
			loadedPresentRuntimeEffect ? "true" : "false");
	}
	for (const std::wstring &loadedFile : iniLoad.loadedFiles) {
		DX12LogManager::Fields("DX12ModRuntimeIni",
			"\"status\":\"loaded\",\"path\":\"%S\"", loadedFile.c_str());
	}
	for (const std::wstring &error : iniLoad.errors) {
		DX12LogManager::Fields("DX12ModRuntimeIni",
			"\"status\":\"error\",\"message\":\"%S\"", error.c_str());
	}
	for (const std::wstring &warning : iniLoad.warnings) {
		DX12LogManager::Fields("DX12ModRuntimeIni",
			"\"status\":\"warning\",\"message\":\"%S\"", warning.c_str());
	}
	for (const DX12VertexLimitRaiseConfig &vlr : loadedVertexLimitRaiseConfigs) {
		DX12LogManager::Fields("DX12VertexLimitRaiseConfig",
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
		DX12LogManager::Fields("DX12ModRuntimeReload", "\"status\":\"skipped\",\"reason\":\"empty_config_path\"");
		return false;
	}
	DX12LogManager::Fields("DX12ModRuntimeReload", "\"status\":\"begin\",\"path\":\"%S\"", configPath.c_str());
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

