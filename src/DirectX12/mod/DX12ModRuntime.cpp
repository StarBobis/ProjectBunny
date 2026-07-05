#include "DX12ModRuntime.hpp"

#include <Shlwapi.h>
#include <stdint.h>
#include <stdio.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cwctype>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "DX12LogManager.hpp"
#include "DX12OverlayStatusState.hpp"
#include "DX12RuntimeState.hpp"
#include "DX12CommandListRuntime.hpp"
#include "DX12DeviceHooks.hpp"
#include "DX12Profiling.hpp"
#include "DX12ShaderDump.hpp"
#include "util.h"
#include "IniDocument.hpp"
#include "MigotoCommandList.hpp"
#include "MigotoIniLoader.hpp"
#include "MigotoResource.hpp"
#include "MigotoShaderOverride.hpp"
#include "MigotoShaderRegex.hpp"
#include "MigotoTextureOverride.hpp"

static DXGI_FORMAT ParseDx12ResourceFormat(const std::wstring &format)
{
	std::wstring value = Bunny::IniDocumentText::ToLower(Bunny::IniDocumentText::Trim(format));
	if (value.empty())
		return DXGI_FORMAT_UNKNOWN;
	if (value.rfind(L"dxgi_format_", 0) == 0)
		value = value.substr(12);
	if (value == L"r16_uint")
		return DXGI_FORMAT_R16_UINT;
	if (value == L"r32_uint")
		return DXGI_FORMAT_R32_UINT;

	wchar_t *end = nullptr;
	unsigned long numeric = wcstoul(value.c_str(), &end, 10);
	if (end && *end == L'\0')
		return static_cast<DXGI_FORMAT>(numeric);
	return DXGI_FORMAT_UNKNOWN;
}

static SRWLOCK gModLock = SRWLOCK_INIT;
static bool gLoaded = false;
static std::wstring gConfigPath;
static std::wstring gBaseDir;
static std::wstring gShaderFixesDir;
static Bunny::ShaderOverrideMap gShaderOverrides;
static Bunny::ShaderRegexMap gShaderRegexes;
static Bunny::TextureOverrideMap gTextureOverrides;
static Bunny::ResourceMap gResources;
static Bunny::CommandListMap gCommandLists;
static std::vector<std::wstring> gVlrResourceCandidates;
static std::unordered_map<std::wstring, bool> gCommandListRuntimeEffect;
static std::unordered_map<std::wstring, bool> gTextureOverridePreRuntimeEffect;
static std::unordered_map<std::wstring, bool> gTextureOverridePostRuntimeEffect;
static std::unordered_map<std::wstring, uint32_t> gTextureOverrideSectionIds;
class DX12IaTextureOverrideCandidate
{
public:
	Bunny::TextureOverrideConfig config;
	UINT64 order = 0;
	uint32_t sectionId = 0;
	bool preRuntimeEffect = true;
	bool postRuntimeEffect = true;
};
static std::vector<DX12IaTextureOverrideCandidate> gIaTextureOverrides;
static std::unordered_map<uint32_t, std::vector<size_t>> gIaIndexTextureOverrideIndex;
static std::unordered_map<UINT64, std::vector<size_t>> gIaVertexTextureOverrideIndex;
static std::unordered_map<uint32_t, std::vector<size_t>> gIaAnyVertexTextureOverrideIndex;
static std::unordered_map<UINT64, std::vector<size_t>> gIaMergedVertexTextureOverrideIndex;

// Hash 级负缓存：记录 (hash, indexBuffer, vertexSlot) 在全局索引中无任何候选。
// draw-context 无关 → 命中率远高于 draw-params 依赖的全量缓存。
// key = bits[31:0]=hash | bit[32]=indexBuffer | bits[47:33]=vertexSlot
static std::unordered_set<UINT64> gIaTextureOverrideHashNoCandidates;
class DX12TriggeredTextureOverride
{
public:
	DX12TriggeredTextureOverride() = default;
	DX12TriggeredTextureOverride(const DX12TriggeredTextureOverride &other)
		: config(other.config),
		  sectionId(other.sectionId),
		  postRuntimeEffect(other.postRuntimeEffect),
		  indexBuffer(other.indexBuffer),
		  vertexSlot(other.vertexSlot),
		  executeCommands(other.executeCommands),
		  executeDrawActions(other.executeDrawActions)
	{
	}
	DX12TriggeredTextureOverride(DX12TriggeredTextureOverride &&other) noexcept
		: config(other.config),
		  sectionId(other.sectionId),
		  postRuntimeEffect(other.postRuntimeEffect),
		  indexBuffer(other.indexBuffer),
		  vertexSlot(other.vertexSlot),
		  executeCommands(other.executeCommands),
		  executeDrawActions(other.executeDrawActions)
	{
	}
	DX12TriggeredTextureOverride &operator=(const DX12TriggeredTextureOverride &other)
	{
		if (this == &other)
			return *this;
		config = other.config;
		sectionId = other.sectionId;
		postRuntimeEffect = other.postRuntimeEffect;
		indexBuffer = other.indexBuffer;
		vertexSlot = other.vertexSlot;
		executeCommands = other.executeCommands;
		executeDrawActions = other.executeDrawActions;
		return *this;
	}
	DX12TriggeredTextureOverride &operator=(DX12TriggeredTextureOverride &&other) noexcept
	{
		if (this == &other)
			return *this;
		config = other.config;
		sectionId = other.sectionId;
		postRuntimeEffect = other.postRuntimeEffect;
		indexBuffer = other.indexBuffer;
		vertexSlot = other.vertexSlot;
		executeCommands = other.executeCommands;
		executeDrawActions = other.executeDrawActions;
		return *this;
	}
	const Bunny::TextureOverrideConfig *config = nullptr;
	uint32_t sectionId = 0;
	bool postRuntimeEffect = true;
	bool indexBuffer = false;
	uint32_t vertexSlot = 0;
	bool executeCommands = false;
	bool executeDrawActions = true;
};

static uint64_t HashCombine64(uint64_t hash, uint64_t value)
{
	hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
	return hash;
}

class DX12CompiledVertexResourceBinding

{

public:
	uint32_t slot = 0;
	std::wstring resource;
};

class DX12PendingResourceViewRequest

{

public:
	std::wstring indexResource;
	std::vector<DX12CompiledVertexResourceBinding> vertexResources;
};

static bool AppendResourceViewsForCommandList(
	ID3D12Device *device, ID3D12GraphicsCommandList *commandList, const DX12IaHashState &iaState,
	const std::wstring &indexResource,
	const std::vector<DX12CompiledVertexResourceBinding> &vertexResources,
	DX12ModIaReplacement *replacement);

static bool ApplyPendingResourceViewsForCommandList(
	ID3D12Device *device, ID3D12GraphicsCommandList *commandList,
	const DX12IaHashState &iaState,
	const std::vector<DX12PendingResourceViewRequest> &requests,
	DX12ModIaReplacement *replacement);

class DX12PreSkinSrvReplacementBinding

{

public:
	uint32_t replaceSlot = 0;
	uint32_t shaderRegister = UINT_MAX;
	std::wstring resource;
};

class DX12CompiledTextureOverridePlan

{

public:
	bool hasActions = false;
	bool hasUnsupportedCommandActions = false;
	bool handlingSkip = false;
	std::wstring indexResource;
	std::vector<DX12CompiledVertexResourceBinding> vertexResources;
	std::vector<DX12ModIaReplacement::DrawCall> draws;
	std::vector<DX12ModIaReplacement::DispatchCall> dispatches;
};
static std::unordered_map<uint32_t, DX12CompiledTextureOverridePlan> gCompiledTextureOverridePlans;

class DX12CompiledCommandListPlan

{

public:
	bool unsupported = false;
	bool handlingSkip = false;
	std::wstring indexResource;
	std::vector<DX12CompiledVertexResourceBinding> vertexResources;
	std::vector<DX12ModIaReplacement::DrawCall> draws;
	std::vector<DX12ModIaReplacement::DispatchCall> dispatches;
};
static std::unordered_map<std::wstring, DX12CompiledCommandListPlan> gCompiledCommandListPlans;

class DX12VertexLimitRaiseConfig

{

public:
	std::wstring section;
	bool hasMatchCs = false;
	uint64_t matchCs = 0;
	uint32_t overrideByteStride = 0;
	uint32_t overrideVertexCount = 0;
	UINT64 overrideByteWidth = 0;
	uint32_t uavByteStride = 0;
	UINT64 overrideNumElements = 0;
};
static std::vector<DX12VertexLimitRaiseConfig> gVertexLimitRaiseConfigs;
static UINT64 gReloadGeneration = 1;
static volatile UINT64 gShaderOverrideNegativeCacheGeneration = 1;
static volatile LONG gHasVertexLimitRaiseConfigs = 0;
static volatile LONG gHasShaderOverrides = 0;
static volatile LONG gHasShaderRegexes = 0;
static volatile LONG gHasTextureOverrides = 0;
static volatile LONG gHasShaderDescriptorTextureOverrideTriggers = 0;
static volatile LONG gHasPreSkinTextureOverrideCandidates = 0;
static volatile LONG gHasShaderTriggeredTextureOverrides = 0;
static volatile LONG gHasPresentRuntimeEffect = 0;
static bool gHasPreSkinVlrWithoutMatchCs = false;
static std::unordered_set<uint64_t> gPreSkinMatchCsHashes;

// Adaptive idle counter: after N consecutive frames with zero active pre-skin
// overrides, binding tracking can be skipped to save ~1500+ lock acquisitions/frame.
static volatile LONG gPreSkinBindingIdleFrames = 0;
static constexpr LONG kPreSkinBindingIdleMax = 60;
class DX12PreSkinUavMatchCacheEntry
{
public:
	uint64_t computeShaderHash = 0;
	UINT64 computeBindingSerial = 0;
	bool valid = false;
	bool matched = false;
	DX12PreSkinDispatchOverride dispatchOverride;
};
// Stable key: reload generation + CS hash + effective binding fingerprint.
// Command list pointer is intentionally not part of the key because the cache
// stores match decisions, not reusable command-list commands.
static std::unordered_map<UINT64, DX12PreSkinUavMatchCacheEntry> gPreSkinUavMatchCache;
static constexpr size_t MaxPreSkinUavMatchCacheEntries = 4096;

static UINT64 MakePreSkinUavMatchCacheKey(
	ID3D12GraphicsCommandList *commandList, uint64_t csHash, UINT64 bindingHash)
{
	(void)commandList;
	UINT64 key = 0x4f1bbcdc9cb7a2c5ull;
	key = HashCombine64(key, gReloadGeneration);
	key = HashCombine64(key, csHash);
	key = HashCombine64(key, bindingHash);
	return key;
}
static volatile LONG gDx12SafeMode = 0;
static volatile LONG gPresentCommandListExecutedPresent = -1;
static volatile UINT64 gPreSkinActiveGeneration = 1;
static UINT64 gPreSkinSrvCacheGeneration = 1;
static volatile LONG gPreSkinDescriptorAbortCacheHitLogCount = 0;
static volatile LONG gPreSkinMatchCsProbeLogCount = 0;
static SRWLOCK gPreSkinMatchCsProbeLogLock = SRWLOCK_INIT;
static std::unordered_set<uint64_t> gPreSkinMatchCsProbeKeys;
static std::unordered_map<uint64_t, bool> gPreSkinMatchCsInputCache;
class DX12ShaderOverridePsoMatchCache
{
public:
	UINT64 generation = 0;
	std::vector<const Bunny::ShaderOverrideConfig*> drawConfigs;
	std::vector<const Bunny::ShaderOverrideConfig*> dispatchConfigs;
	std::vector<Bunny::ShaderOverrideConfig> regexDrawConfigs;
	std::vector<Bunny::ShaderOverrideConfig> regexDispatchConfigs;
	DX12IaHashTargetPlan drawTargetPlan;
};
static std::unordered_map<ID3D12PipelineState*, DX12ShaderOverridePsoMatchCache> gShaderOverridePsoMatchCache;
class DX12TextureOverrideLookupCacheKey
{
public:
	UINT64 reloadGeneration = 0;
	UINT64 preSkinGeneration = 0;
	uint32_t targetKind = 0;
	uint32_t shaderStage = 0;
	uint32_t slot = 0;
	uint32_t hash = 0;
	uint32_t vertexCount = 0;
	uint32_t indexCount = 0;
	uint32_t instanceCount = 0;
	uint32_t firstVertex = 0;
	uint32_t firstIndex = 0;
	uint32_t firstInstance = 0;

	bool operator==(const DX12TextureOverrideLookupCacheKey &other) const
	{
		return reloadGeneration == other.reloadGeneration &&
			preSkinGeneration == other.preSkinGeneration &&
			targetKind == other.targetKind &&
			shaderStage == other.shaderStage &&
			slot == other.slot &&
			hash == other.hash &&
			vertexCount == other.vertexCount &&
			indexCount == other.indexCount &&
			instanceCount == other.instanceCount &&
			firstVertex == other.firstVertex &&
			firstIndex == other.firstIndex &&
			firstInstance == other.firstInstance;
	}
};
class DX12TextureOverrideLookupCacheKeyHash
{
public:
	size_t operator()(const DX12TextureOverrideLookupCacheKey &key) const
	{
		UINT64 hash = 0x8a2f05c19f1d6b23ull;
		hash = HashCombine64(hash, key.reloadGeneration);
		hash = HashCombine64(hash, key.preSkinGeneration);
		hash = HashCombine64(hash, key.targetKind);
		hash = HashCombine64(hash, key.shaderStage);
		hash = HashCombine64(hash, key.slot);
		hash = HashCombine64(hash, key.hash);
		hash = HashCombine64(hash, key.vertexCount);
		hash = HashCombine64(hash, key.indexCount);
		hash = HashCombine64(hash, key.instanceCount);
		hash = HashCombine64(hash, key.firstVertex);
		hash = HashCombine64(hash, key.firstIndex);
		hash = HashCombine64(hash, key.firstInstance);
		return static_cast<size_t>(hash ^ (hash >> 32));
	}
};
class DX12TextureOverrideLookupCacheEntry
{
public:
	bool matched = false;
	std::vector<size_t> iaCandidateIndexes;
};
static std::unordered_map<
	DX12TextureOverrideLookupCacheKey,
	DX12TextureOverrideLookupCacheEntry,
	DX12TextureOverrideLookupCacheKeyHash> gTextureOverrideLookupCache;
static constexpr size_t MaxTextureOverrideLookupCacheEntries = 8192;

class DX12ShaderOverrideNegativeCacheKey

{

public:
	UINT64 reloadGeneration = 0;
	UINT64 preSkinGeneration = 0;
	ID3D12PipelineState *pipelineState = nullptr;
	uint32_t vertexCount = 0;
	uint32_t indexCount = 0;
	uint32_t instanceCount = 0;
	uint32_t firstVertex = 0;
	uint32_t firstIndex = 0;
	bool hasIndexBuffer = false;
	uint32_t indexHash = 0;
	UINT64 indexBufferLocation = 0;
	uint32_t indexBufferSize = 0;
	uint32_t indexBufferFormat = 0;
	std::array<uint32_t, 32> vertexHashes = {};
	std::array<uint8_t, 32> vertexPresent = {};
	std::array<UINT64, 32> vertexBufferLocations = {};
	std::array<uint32_t, 32> vertexBufferSizes = {};
	std::array<uint32_t, 32> vertexBufferStrides = {};

	bool operator==(const DX12ShaderOverrideNegativeCacheKey &other) const
	{
		return reloadGeneration == other.reloadGeneration &&
			preSkinGeneration == other.preSkinGeneration &&
			pipelineState == other.pipelineState &&
			vertexCount == other.vertexCount &&
			indexCount == other.indexCount &&
			instanceCount == other.instanceCount &&
			firstVertex == other.firstVertex &&
			firstIndex == other.firstIndex &&
			hasIndexBuffer == other.hasIndexBuffer &&
			indexHash == other.indexHash &&
			indexBufferLocation == other.indexBufferLocation &&
			indexBufferSize == other.indexBufferSize &&
			indexBufferFormat == other.indexBufferFormat &&
			vertexHashes == other.vertexHashes &&
			vertexPresent == other.vertexPresent &&
			vertexBufferLocations == other.vertexBufferLocations &&
			vertexBufferSizes == other.vertexBufferSizes &&
			vertexBufferStrides == other.vertexBufferStrides;
	}
};

class DX12ShaderOverrideNegativeCacheEntry

{

public:
	bool valid = false;
	UINT64 hash = 0;
	DX12ShaderOverrideNegativeCacheKey key;
};

static constexpr size_t ShaderOverrideNegativeCacheMaxEntries = 8192;
static std::unordered_map<UINT64, DX12ShaderOverrideNegativeCacheKey> gShaderOverrideNegativeCache;
static SRWLOCK gShaderOverrideNegativeCacheLock = SRWLOCK_INIT;

class DX12ComputeUavProducer

{

public:
	UINT64 serial = 0;
	UINT rootParameterIndex = 0;
	UINT rangeIndex = UINT_MAX;
	UINT shaderRegister = UINT_MAX;
	UINT registerSpace = 0;
	UINT descriptorOffset = 0;
	bool rootDescriptor = false;
	UINT64 gpuVirtualAddress = 0;
	DX12DescriptorSummary descriptor;
	bool hasDescriptor = false;
};

static SRWLOCK gPreSkinLock = SRWLOCK_INIT;
static UINT64 gComputeUavSerial = 0;
static std::vector<DX12ComputeUavProducer> gRecentComputeUavs;
static constexpr size_t MaxRecentComputeUavs = 512;
static std::set<SIZE_T> gPatchedPreSkinDescriptors;
static std::map<SIZE_T, std::wstring> gKnownPreSkinDescriptorPatches;
class DX12ActivePreSkinTextureOverride
{
public:
	Bunny::TextureOverrideConfig config;
	DX12ComputeUavProducer producer;
	ID3D12Resource *outputResource = nullptr;
	UINT64 outputByteWidth = 0;
	UINT outputStride = 0;
};

class DX12ActivePreSkinOutputState

{

public:
	D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	bool uavBarrierPending = false;
};

static std::map<std::wstring, DX12ActivePreSkinTextureOverride> gActivePreSkinTextureOverrides;
static std::unordered_map<ID3D12Resource*, DX12ActivePreSkinOutputState> gActivePreSkinOutputStates;
static UINT64 CurrentPreSkinActiveGeneration()
{
	return static_cast<UINT64>(InterlockedCompareExchange64(
		reinterpret_cast<volatile LONG64*>(&gPreSkinActiveGeneration), 0, 0));
}

static UINT64 SnapshotActivePreSkinSections(std::unordered_set<std::wstring> *sections)
{
	if (!sections)
		return CurrentPreSkinActiveGeneration();
	sections->clear();
	const UINT64 generation = CurrentPreSkinActiveGeneration();
	if (!DX12ModHasActivePreSkinTextureOverrides())
		return generation;
	AcquireSRWLockShared(&gPreSkinLock);
	sections->reserve(gActivePreSkinTextureOverrides.size());
	for (const auto &item : gActivePreSkinTextureOverrides)
		sections->insert(item.first);
	ReleaseSRWLockShared(&gPreSkinLock);
	return generation;
}

static void BumpPreSkinActiveGeneration()
{
	InterlockedIncrement64(reinterpret_cast<volatile LONG64*>(&gPreSkinActiveGeneration));
}

static bool ActivePreSkinResourceReferencedLocked(
	const std::wstring &skipSection, ID3D12Resource *resource)
{
	if (!resource)
		return false;
	for (const auto &item : gActivePreSkinTextureOverrides) {
		if (item.first == skipSection)
			continue;
		if (item.second.outputResource == resource)
			return true;
	}
	return false;
}

static void MoveActivePreSkinTextureOverrideResourceLocked(
	DX12ActivePreSkinTextureOverride *active, std::vector<ID3D12Resource*> *releaseResources)
{
	if (!active)
		return;
	if (active->outputResource) {
		if (releaseResources)
			releaseResources->push_back(active->outputResource);
		active->outputResource = nullptr;
	}
	active->outputByteWidth = 0;
	active->outputStride = 0;
}

static void ReleaseMovedPreSkinResources(std::vector<ID3D12Resource*> *resources)
{
	if (!resources)
		return;
	for (ID3D12Resource *resource : *resources) {
		if (resource)
			resource->Release();
	}
	resources->clear();
}

static void ClearActivePreSkinTextureOverridesLocked(
	std::vector<ID3D12Resource*> *releaseResources)
{
	const bool changed =
		!gActivePreSkinTextureOverrides.empty() || !gActivePreSkinOutputStates.empty();
	for (auto &item : gActivePreSkinTextureOverrides)
		MoveActivePreSkinTextureOverrideResourceLocked(&item.second, releaseResources);
	gActivePreSkinTextureOverrides.clear();
	gActivePreSkinOutputStates.clear();
	if (changed)
		BumpPreSkinActiveGeneration();
}

static void StoreActivePreSkinTextureOverrideLocked(
	const std::wstring &section, const DX12ActivePreSkinTextureOverride &active,
	std::vector<ID3D12Resource*> *releaseResources)
{
	auto existing = gActivePreSkinTextureOverrides.find(section);
	ID3D12Resource *oldResource = existing != gActivePreSkinTextureOverrides.end() ?
		existing->second.outputResource : nullptr;
	if (existing != gActivePreSkinTextureOverrides.end())
		MoveActivePreSkinTextureOverrideResourceLocked(&existing->second, releaseResources);
	if (oldResource && oldResource != active.outputResource &&
	    !ActivePreSkinResourceReferencedLocked(section, oldResource))
		gActivePreSkinOutputStates.erase(oldResource);
	DX12ActivePreSkinTextureOverride stored = active;
	gActivePreSkinTextureOverrides[section] = stored;
	if (stored.outputResource) {
		DX12ActivePreSkinOutputState &state =
			gActivePreSkinOutputStates[stored.outputResource];
		state.state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		state.uavBarrierPending = true;
	}
	BumpPreSkinActiveGeneration();
}

bool DX12ModFindActivePreSkinProducerForOutput(
	ID3D12Resource *outputResource, UINT64 outputByteWidth, UINT outputStride,
	DX12ModPreSkinProducerSummary *summary)
{
	if (summary)
		*summary = DX12ModPreSkinProducerSummary();
	if (!outputResource || !summary)
		return false;

	bool found = false;
	AcquireSRWLockShared(&gPreSkinLock);
	for (const auto &item : gActivePreSkinTextureOverrides) {
		const DX12ActivePreSkinTextureOverride &active = item.second;
		if (active.outputResource != outputResource)
			continue;
		if (outputByteWidth && active.outputByteWidth &&
		    outputByteWidth != active.outputByteWidth)
			continue;
		if (outputStride && active.outputStride &&
		    outputStride != active.outputStride)
			continue;
		summary->rootParameterIndex = active.producer.rootParameterIndex;
		summary->rangeIndex = active.producer.rangeIndex;
		summary->shaderRegister = active.producer.shaderRegister;
		summary->registerSpace = active.producer.registerSpace;
		summary->descriptorOffset = active.producer.descriptorOffset;
		summary->rootDescriptor = active.producer.rootDescriptor;
		summary->gpuVirtualAddress = active.producer.gpuVirtualAddress;
		summary->descriptor = active.producer.descriptor;
		summary->hasDescriptor = active.producer.hasDescriptor;
		summary->outputByteWidth = active.outputByteWidth;
		summary->outputStride = active.outputStride;
		found = true;
		break;
	}
	ReleaseSRWLockShared(&gPreSkinLock);
	return found;
}

static std::map<std::wstring, LONG> gPreSkinSectionAppliedPresent;

class DX12PreSkinCbvReadCacheKey

{

public:
	ID3D12Resource *resource = nullptr;
	UINT64 offset = 0;
	UINT size = 0;

	bool operator<(const DX12PreSkinCbvReadCacheKey &rhs) const
	{
		if (resource != rhs.resource)
			return resource < rhs.resource;
		if (offset != rhs.offset)
			return offset < rhs.offset;
		return size < rhs.size;
	}
};

class DX12PreSkinCbvReadCacheValue

{

public:
	HRESULT hr = E_FAIL;
	std::vector<unsigned char> bytes;
};

class DX12PreSkinSrvHashResult

{

public:
	uint32_t descriptorHash = 0;
	UINT64 sourceOffset = 0;
	UINT64 viewBytes = 0;
	UINT stride = 0;
};

class DX12LoadedResource;

class DX12PreSkinSrvPositiveCacheValue

{

public:
	DX12LoadedResource *resource = nullptr;
	UINT elementStride = 0;
	UINT64 srvByteWidth = 0;
	uint32_t srvRegister = UINT_MAX;
};

static std::map<DX12PreSkinCbvReadCacheKey, DX12PreSkinCbvReadCacheValue> gPreSkinCbvReadCache;
static UINT64 gPreSkinCbvReadCacheReloadGen = 0;

// ── 跨帧 Patched CBV Buffer 缓存 ──────────────────────────────────
// 消除 PatchPreSkinVertexCountCbvLocked 中每帧首次的 CreateCommittedResource。
// 同一 (CBV resource, offset, size, overrideVertexCount) 复用 UPLOAD buffer 和 CBV descriptor。
class DX12PreSkinCbvPatchCacheKey
{
public:
	ID3D12Resource *resource = nullptr;
	UINT64 sourceOffset = 0;
	UINT viewBytes = 0;
	UINT overrideVertexCount = 0;

	bool operator<(const DX12PreSkinCbvPatchCacheKey &rhs) const
	{
		if (resource != rhs.resource) return resource < rhs.resource;
		if (sourceOffset != rhs.sourceOffset) return sourceOffset < rhs.sourceOffset;
		if (viewBytes != rhs.viewBytes) return viewBytes < rhs.viewBytes;
		return overrideVertexCount < rhs.overrideVertexCount;
	}
};

class DX12PreSkinCbvPatchCacheEntry
{
public:
	ID3D12Resource *patchedResource = nullptr;       // UPLOAD heap buffer, reused
	D3D12_CPU_DESCRIPTOR_HANDLE cbvCpu = {};         // CBV descriptor pointing to patchedResource
	UINT64 generation = 0;                            // mod reload generation when created
};

static std::map<DX12PreSkinCbvPatchCacheKey, DX12PreSkinCbvPatchCacheEntry> gPreSkinCbvPatchCache;
static UINT64 gPreSkinCbvPatchCacheGeneration = 0;

static std::set<UINT64> gPreSkinSrvNegativeCache;
static std::set<UINT64> gPreSkinSrvStableNegativeCache;
static std::set<UINT64> gPreSkinDescriptorPatchAbortCache;
static std::map<UINT64, DX12PreSkinSrvPositiveCacheValue> gPreSkinSrvPositiveCache;
static std::map<UINT64, DX12PreSkinSrvPositiveCacheValue> gPreSkinSrvStablePositiveCache;

static UINT64 DescriptorViewSize(const DX12DescriptorSummary &descriptor);
static uint32_t HashComputeBufferBinding(const DX12CurrentComputeUavBinding &binding);
static uint64_t HashComputeCbvListForProbe(
	const std::vector<DX12CurrentComputeUavBinding> &cbvs);

static UINT64 HashWideString(UINT64 key, const std::wstring &text)
{
	for (wchar_t ch : text) {
		key = HashCombine64(key, static_cast<UINT64>(ch & 0xffff));
	}
	return key;
}

static UINT64 HashComputeBindingLayout(
	UINT64 key, const DX12CurrentComputeUavBinding &binding, bool includeDescriptorHash)
{
	key = HashCombine64(key, binding.rootParameterIndex);
	key = HashCombine64(key, binding.rangeIndex);
	key = HashCombine64(key, binding.shaderRegister);
	key = HashCombine64(key, binding.registerSpace);
	key = HashCombine64(key, binding.descriptorOffset);
	key = HashCombine64(key, binding.descriptor.viewDimension);
	key = HashCombine64(key, binding.descriptor.viewFormat);
	key = HashCombine64(key, binding.descriptor.firstElement);
	key = HashCombine64(key, binding.descriptor.numElements);
	key = HashCombine64(key, binding.descriptor.structureByteStride);
	key = HashCombine64(key, binding.descriptor.bufferViewOffset);
	key = HashCombine64(key, binding.descriptor.bufferViewBytes);
	key = HashCombine64(key, binding.descriptor.cbvSize);
	key = HashCombine64(key, DescriptorViewSize(binding.descriptor));
	if (includeDescriptorHash)
		key = HashCombine64(key, HashComputeBufferBinding(binding));
	return key;
}

static uint64_t HashComputeCbvLayoutList(
	const std::vector<DX12CurrentComputeUavBinding> &cbvs)
{
	uint64_t key = 0xb5316b20a34bf779ull;
	const size_t maxItems = 8;
	for (size_t i = 0; i < cbvs.size() && i < maxItems; ++i)
		key = HashComputeBindingLayout(key, cbvs[i], false);
	return key;
}

static UINT64 MakePreSkinDescriptorPatchAbortCacheKey(
	const Bunny::TextureOverrideConfig &config,
	const std::vector<DX12PreSkinSrvReplacementBinding> &srvReplacements,
	const std::vector<DX12CurrentComputeUavBinding> &srvs,
	const std::vector<DX12CurrentComputeUavBinding> &cbvs)
{
	UINT64 key = HashCombine64(gPreSkinSrvCacheGeneration, 0x7d9a4c6b2f1e8351ull);
	key = HashWideString(key, config.section);
	key = HashCombine64(key, srvReplacements.size());
	for (const DX12PreSkinSrvReplacementBinding &replacement : srvReplacements) {
		key = HashCombine64(key, replacement.replaceSlot);
		key = HashCombine64(key, replacement.shaderRegister);
		key = HashWideString(key, replacement.resource);
	}
	key = HashCombine64(key, srvs.size());
	for (const DX12CurrentComputeUavBinding &srv : srvs)
		key = HashComputeBindingLayout(key, srv, true);
	key = HashCombine64(key, HashComputeCbvLayoutList(cbvs));
	return key;
}

static void StorePreSkinDescriptorPatchAbort(UINT64 key)
{
	if (gPreSkinDescriptorPatchAbortCache.size() > 1024)
		gPreSkinDescriptorPatchAbortCache.clear();
	gPreSkinDescriptorPatchAbortCache.insert(key);
}

static UINT64 MakePreSkinSrvStableProbeCacheKey(
	const DX12CurrentComputeUavBinding &srv,
	const Bunny::TextureOverrideConfig &config,
	UINT64 srvByteWidth)
{
	UINT64 key = 14695981039346656037ull;
	auto append = [&key](UINT64 value) {
		for (size_t i = 0; i < sizeof(value); ++i) {
			key ^= static_cast<unsigned char>((value >> (i * 8)) & 0xff);
			key *= 1099511628211ull;
		}
	};
	append(static_cast<UINT64>(gPreSkinSrvCacheGeneration));
	append(static_cast<UINT64>(srv.rootParameterIndex));
	append(static_cast<UINT64>(srv.rangeIndex));
	append(static_cast<UINT64>(srv.shaderRegister));
	append(static_cast<UINT64>(srv.registerSpace));
	append(static_cast<UINT64>(srv.descriptorOffset));
	append(static_cast<UINT64>(srv.descriptor.viewDimension));
	append(static_cast<UINT64>(srv.descriptor.structureByteStride));
	append(static_cast<UINT64>(srv.descriptor.gpuVirtualAddress));
	append(srvByteWidth);
	append(reinterpret_cast<UINT64>(srv.descriptor.resource));
	for (wchar_t ch : config.section) {
		key ^= static_cast<unsigned char>(ch & 0xff);
		key *= 1099511628211ull;
		key ^= static_cast<unsigned char>((ch >> 8) & 0xff);
		key *= 1099511628211ull;
	}
	return key;
}

static UINT64 MakePreSkinSrvProbeCacheKey(
	const DX12CurrentComputeUavBinding &srv,
	const Bunny::TextureOverrideConfig &config,
	UINT64 srvByteWidth)
{
	UINT64 key = 14695981039346656037ull;
	auto append = [&key](UINT64 value) {
		for (size_t i = 0; i < sizeof(value); ++i) {
			key ^= static_cast<unsigned char>((value >> (i * 8)) & 0xff);
			key *= 1099511628211ull;
		}
	};
	append(static_cast<UINT64>(DX12RuntimeState::GetPresentCount()));
	append(static_cast<UINT64>(gPreSkinSrvCacheGeneration));
	append(static_cast<UINT64>(srv.rootParameterIndex));
	append(static_cast<UINT64>(srv.rangeIndex));
	append(static_cast<UINT64>(srv.shaderRegister));
	append(static_cast<UINT64>(srv.registerSpace));
	append(static_cast<UINT64>(srv.descriptorOffset));
	append(static_cast<UINT64>(srv.descriptor.viewDimension));
	append(static_cast<UINT64>(srv.descriptor.structureByteStride));
	append(static_cast<UINT64>(srv.descriptor.gpuVirtualAddress));
	append(srvByteWidth);
	append(reinterpret_cast<UINT64>(srv.descriptor.resource));
	for (wchar_t ch : config.section) {
		key ^= static_cast<unsigned char>(ch & 0xff);
		key *= 1099511628211ull;
		key ^= static_cast<unsigned char>((ch >> 8) & 0xff);
		key *= 1099511628211ull;
	}
	return key;
}

class DX12UavHashTextureOverrideMatch

{

public:
	Bunny::TextureOverrideConfig config;
	uint32_t hash = 0;
	std::wstring resourceName;
	std::vector<DX12PreSkinSrvReplacementBinding> srvReplacements;
};

class DX12PreSkinTextureOverrideCandidate

{

public:
	Bunny::TextureOverrideConfig config;
	uint32_t hash = 0;
	std::wstring resourceName;
	UINT64 order = 0;
};

static std::vector<DX12PreSkinTextureOverrideCandidate> gPreSkinTextureOverrides;
static std::unordered_map<uint32_t, std::vector<size_t>> gPreSkinUavHashTextureOverrideIndex;
static std::unordered_map<uint64_t, std::vector<size_t>> gPreSkinMatchCsTextureOverrideIndex;
static std::unordered_map<std::wstring, std::vector<std::wstring>> gExplicitMatchCsResourceSections;

class DX12LoadedResource

{

public:
	ID3D12Resource *resource = nullptr;
	ID3D12Resource *uavResource = nullptr;
	ID3D12Resource *srvResource = nullptr;
	ID3D12DescriptorHeap *uavHeap = nullptr;
	ID3D12DescriptorHeap *srvHeap = nullptr;
	bool failed = false;
	UINT64 byteWidth = 0;
	UINT stride = 0;
	DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
	D3D12_CPU_DESCRIPTOR_HANDLE uavCpu = {};
	D3D12_GPU_DESCRIPTOR_HANDLE uavGpu = {};
	D3D12_CPU_DESCRIPTOR_HANDLE srvCpu = {};
	bool uavInitialized = false;
	bool uavWritten = false;
	bool uavValid = false;
	bool uavBarrierPending = false;
	UINT64 uavByteWidth = 0;
	UINT uavStride = 0;
	UINT64 srvByteWidth = 0;
	UINT srvStride = 0;
	D3D12_RESOURCE_STATES uavState = D3D12_RESOURCE_STATE_COPY_DEST;
	std::wstring name;
	std::wstring path;
};

static std::unordered_map<std::wstring, DX12LoadedResource> gLoadedResources;
class DX12RetiredLoadedResource

{

public:
	DX12LoadedResource resource;
	LONG retirePresent = 0;
};
static std::vector<DX12RetiredLoadedResource> gRetiredLoadedResources;
static constexpr LONG RetiredLoadedResourcePresentDelay = 8;
static volatile LONG gActiveLoadedResourceSnapshots = 0;

class DX12ResidentResourceBuildResult
{
public:
	std::wstring name;
	UINT64 generation = 0;
	DX12LoadedResource resource;
	bool available = false;
};

class DX12PreSkinReplacementState

{

public:
	bool active = false;
	UINT rootParameterIndex = 0;
	D3D12_GPU_DESCRIPTOR_HANDLE originalTable = {};
	D3D12_GPU_DESCRIPTOR_HANDLE replacementTable = {};
	ID3D12DescriptorHeap *descriptorHeap = nullptr;
	ID3D12DescriptorHeap *originalHeap = nullptr;
	ID3D12Resource *resource = nullptr;
	UINT64 byteWidth = 0;
	UINT stride = 0;
	UINT64 producerSerial = 0;
	bool hasActiveTextureOverride = false;
	Bunny::TextureOverrideConfig activeConfig;
	DX12ComputeUavProducer activeProducer;
	class DescriptorRestore
	{
	public:
		SIZE_T targetCpu = 0;
		ID3D12DescriptorHeap *heap = nullptr;
		D3D12_CPU_DESCRIPTOR_HANDLE cpu = {};
	};
	std::vector<DescriptorRestore> descriptorRestores;
	class TableRestore
	{
	public:
		UINT rootParameterIndex = 0;
		D3D12_GPU_DESCRIPTOR_HANDLE originalTable = {};
	};
	std::vector<TableRestore> tableRestores;
	ID3D12DescriptorHeap *patchHeap = nullptr;
	ID3D12DescriptorHeap *restoreCbvSrvUavHeap = nullptr;
	ID3D12DescriptorHeap *restoreSamplerHeap = nullptr;
	std::vector<ID3D12Resource*> temporaryResources;
};

static std::unordered_map<ID3D12GraphicsCommandList*, DX12PreSkinReplacementState> gActivePreSkinReplacements;
class DX12RetiredPreSkinResource
{
public:
	ID3D12Resource *resource = nullptr;
	LONG retirePresent = 0;
};
static std::vector<DX12RetiredPreSkinResource> gRetiredPreSkinResources;
static constexpr LONG PreSkinRetiredObjectPresentDelay = 120;

class DX12PreSkinDescriptorRing

{

public:
	ID3D12Device *device = nullptr;
	ID3D12DescriptorHeap *heap = nullptr;
	UINT capacity = 0;
	UINT offset = 0;
	UINT increment = 0;
	LONG present = -1;
};

class DX12RetiredPreSkinDescriptorHeap

{

public:
	ID3D12DescriptorHeap *heap = nullptr;
	LONG retirePresent = 0;
};

static SRWLOCK gPreSkinDescriptorRingLock = SRWLOCK_INIT;
static DX12PreSkinDescriptorRing gPreSkinDescriptorRing;
static std::vector<DX12RetiredPreSkinDescriptorHeap> gRetiredPreSkinDescriptorHeaps;
static constexpr UINT PreSkinDescriptorRingInitialCapacity = 4096;

class DX12PreSkinPendingSubmission

{

public:
	std::vector<ID3D12Resource*> resources;
	std::vector<ID3D12DescriptorHeap*> descriptorHeaps;
};

class DX12PreSkinReleaseBatch

{

public:
	std::vector<ID3D12Resource*> resources;
	std::vector<ID3D12DescriptorHeap*> descriptorHeaps;
	std::vector<ID3D12Device*> devices;
	std::vector<ID3D12CommandQueue*> queues;
};

static SRWLOCK gPreSkinRetireLock = SRWLOCK_INIT;
static std::unordered_map<IUnknown*, DX12PreSkinPendingSubmission> gPendingPreSkinSubmissions;

static void ReleasePreSkinDescriptorRestores(
	std::vector<DX12PreSkinReplacementState::DescriptorRestore> *restores)
{
	if (!restores)
		return;
	for (auto &restore : *restores) {
		if (restore.heap) {
			restore.heap->Release();
			restore.heap = nullptr;
		}
	}
	restores->clear();
}

static void ReleasePreSkinReleaseBatch(DX12PreSkinReleaseBatch *batch)
{
	if (!batch)
		return;
	for (ID3D12Resource *resource : batch->resources) {
		if (resource)
			resource->Release();
	}
	for (ID3D12DescriptorHeap *heap : batch->descriptorHeaps) {
		if (heap)
			heap->Release();
	}
	for (ID3D12CommandQueue *queue : batch->queues) {
		if (queue)
			queue->Release();
	}
	for (ID3D12Device *device : batch->devices) {
		if (device)
			device->Release();
	}
	batch->resources.clear();
	batch->descriptorHeaps.clear();
	batch->queues.clear();
	batch->devices.clear();
}

static void MovePendingPreSkinSubmission(
	DX12PreSkinPendingSubmission *source,
	DX12PreSkinPendingSubmission *target)
{
	if (!source || !target)
		return;
	target->resources.insert(target->resources.end(),
		source->resources.begin(), source->resources.end());
	target->descriptorHeaps.insert(target->descriptorHeaps.end(),
		source->descriptorHeaps.begin(), source->descriptorHeaps.end());
	source->resources.clear();
	source->descriptorHeaps.clear();
}

static void MovePendingPreSkinSubmissionToReleaseBatch(
	DX12PreSkinPendingSubmission *source,
	DX12PreSkinReleaseBatch *batch)
{
	if (!source || !batch)
		return;
	batch->resources.insert(batch->resources.end(),
		source->resources.begin(), source->resources.end());
	batch->descriptorHeaps.insert(batch->descriptorHeaps.end(),
		source->descriptorHeaps.begin(), source->descriptorHeaps.end());
	source->resources.clear();
	source->descriptorHeaps.clear();
}

static void CleanupRetiredPreSkinObjectsLocked(
	DX12PreSkinReleaseBatch *releaseBatch)
{
	if (!releaseBatch)
		return;
	const LONG present = DX12RuntimeState::GetPresentCount();
	auto resourceIt = gRetiredPreSkinResources.begin();
	while (resourceIt != gRetiredPreSkinResources.end()) {
		if (present - resourceIt->retirePresent < PreSkinRetiredObjectPresentDelay) {
			++resourceIt;
			continue;
		}
		if (resourceIt->resource)
			releaseBatch->resources.push_back(resourceIt->resource);
		resourceIt = gRetiredPreSkinResources.erase(resourceIt);
	}

	auto heapIt = gRetiredPreSkinDescriptorHeaps.begin();
	while (heapIt != gRetiredPreSkinDescriptorHeaps.end()) {
		if (present - heapIt->retirePresent < PreSkinRetiredObjectPresentDelay) {
			++heapIt;
			continue;
		}
		if (heapIt->heap)
			releaseBatch->descriptorHeaps.push_back(heapIt->heap);
		heapIt = gRetiredPreSkinDescriptorHeaps.erase(heapIt);
	}
}

static IUnknown *PreSkinCommandListIdentity(IUnknown *commandList)
{
	if (!commandList)
		return nullptr;
	IUnknown *identity = nullptr;
	if (FAILED(commandList->QueryInterface(IID_PPV_ARGS(&identity))) || !identity)
		return commandList;
	identity->Release();
	return identity;
}

static void RequeuePendingPreSkinSubmissionLocked(DX12PreSkinPendingSubmission *pending)
{
	if (!pending)
		return;
	DX12PreSkinPendingSubmission &globalPending = gPendingPreSkinSubmissions[nullptr];
	MovePendingPreSkinSubmission(pending, &globalPending);
}

static void RetirePreSkinResource(ID3D12Resource *resource)
{
	if (!resource)
		return;
	AcquireSRWLockExclusive(&gPreSkinRetireLock);
	gPendingPreSkinSubmissions[nullptr].resources.push_back(resource);
	ReleaseSRWLockExclusive(&gPreSkinRetireLock);
}

static void RetirePreSkinResourceForCommandList(
	ID3D12GraphicsCommandList *commandList, ID3D12Resource *resource)
{
	if (!resource)
		return;
	IUnknown *identity = PreSkinCommandListIdentity(commandList);
	AcquireSRWLockExclusive(&gPreSkinRetireLock);
	gPendingPreSkinSubmissions[identity].resources.push_back(resource);
	ReleaseSRWLockExclusive(&gPreSkinRetireLock);
}

static void RetainPreSkinResourceForCommandList(
	ID3D12GraphicsCommandList *commandList, ID3D12Resource *resource)
{
	if (!resource)
		return;
	resource->AddRef();
	RetirePreSkinResourceForCommandList(commandList, resource);
}

static void RetirePreSkinDescriptorHeap(ID3D12DescriptorHeap *heap)
{
	if (!heap)
		return;
	AcquireSRWLockExclusive(&gPreSkinRetireLock);
	gPendingPreSkinSubmissions[nullptr].descriptorHeaps.push_back(heap);
	ReleaseSRWLockExclusive(&gPreSkinRetireLock);
}

static void RetirePreSkinDescriptorHeapForCommandList(
	ID3D12GraphicsCommandList *commandList, ID3D12DescriptorHeap *heap)
{
	if (!heap)
		return;
	IUnknown *identity = PreSkinCommandListIdentity(commandList);
	AcquireSRWLockExclusive(&gPreSkinRetireLock);
	gPendingPreSkinSubmissions[identity].descriptorHeaps.push_back(heap);
	ReleaseSRWLockExclusive(&gPreSkinRetireLock);
}

void DX12ModNotifyCommandListsSubmitted(
	ID3D12CommandQueue *queue, UINT numCommandLists, ID3D12CommandList *const *commandLists)
{
	if (!queue || !commandLists || !numCommandLists)
		return;

	std::vector<IUnknown*> identities;
	identities.reserve(numCommandLists);
	for (UINT i = 0; i < numCommandLists; ++i)
		identities.push_back(PreSkinCommandListIdentity(commandLists[i]));

	DX12PreSkinReleaseBatch releaseBatch;
	DX12PreSkinPendingSubmission pending;
	AcquireSRWLockExclusive(&gPreSkinRetireLock);
	CleanupRetiredPreSkinObjectsLocked(&releaseBatch);

	auto globalIt = gPendingPreSkinSubmissions.find(nullptr);
	if (globalIt != gPendingPreSkinSubmissions.end()) {
		MovePendingPreSkinSubmission(&globalIt->second, &pending);
		gPendingPreSkinSubmissions.erase(globalIt);
	}

	for (IUnknown *identity : identities) {
		auto it = gPendingPreSkinSubmissions.find(identity);
		if (it == gPendingPreSkinSubmissions.end())
			continue;
		MovePendingPreSkinSubmission(&it->second, &pending);
		gPendingPreSkinSubmissions.erase(it);
	}

	if (pending.resources.empty() && pending.descriptorHeaps.empty()) {
		ReleaseSRWLockExclusive(&gPreSkinRetireLock);
		ReleasePreSkinReleaseBatch(&releaseBatch);
		return;
	}

	const LONG retirePresent = DX12RuntimeState::GetPresentCount();

	for (ID3D12Resource *resource : pending.resources) {
		if (!resource)
			continue;
		gRetiredPreSkinResources.push_back({ resource, retirePresent });
	}
	for (ID3D12DescriptorHeap *heap : pending.descriptorHeaps) {
		if (!heap)
			continue;
		gRetiredPreSkinDescriptorHeaps.push_back({ heap, retirePresent });
	}
	ReleaseSRWLockExclusive(&gPreSkinRetireLock);
	DX12LogManager::DebugJsonFunc("DX12PreSkinRetireSubmit",
		"\"present\":%ld,\"queue\":\"%p\",\"commandLists\":%u,\"resources\":%zu,\"descriptorHeaps\":%zu,\"delay\":%ld",
		retirePresent, queue, numCommandLists,
		pending.resources.size(), pending.descriptorHeaps.size(),
		PreSkinRetiredObjectPresentDelay);
	ReleasePreSkinReleaseBatch(&releaseBatch);
}
static void ReleasePreSkinDescriptorRingLocked(DX12PreSkinReleaseBatch *releaseBatch)
{
	if (!releaseBatch)
		return;
	if (gPreSkinDescriptorRing.heap) {
		releaseBatch->descriptorHeaps.push_back(gPreSkinDescriptorRing.heap);
		gPreSkinDescriptorRing.heap = nullptr;
	}
	if (gPreSkinDescriptorRing.device) {
		releaseBatch->devices.push_back(gPreSkinDescriptorRing.device);
		gPreSkinDescriptorRing.device = nullptr;
	}
	gPreSkinDescriptorRing.capacity = 0;
	gPreSkinDescriptorRing.offset = 0;
	gPreSkinDescriptorRing.increment = 0;
	gPreSkinDescriptorRing.present = -1;
	for (auto &retired : gRetiredPreSkinDescriptorHeaps) {
		if (retired.heap)
			releaseBatch->descriptorHeaps.push_back(retired.heap);
	}
	gRetiredPreSkinDescriptorHeaps.clear();
}

static bool EnsurePreSkinDescriptorRingLocked(
	ID3D12Device *device, ID3D12GraphicsCommandList *commandList, UINT requiredDescriptors,
	D3D12_CPU_DESCRIPTOR_HANDLE *cpuBase,
	D3D12_GPU_DESCRIPTOR_HANDLE *gpuBase,
	UINT *increment,
	ID3D12DescriptorHeap **heapOut)
{
	if (!device || !requiredDescriptors || !cpuBase || !gpuBase || !increment || !heapOut)
		return false;
	*heapOut = nullptr;

	for (;;) {
		ID3D12DescriptorHeap *oldHeap = nullptr;
		ID3D12Device *oldDevice = nullptr;
		ID3D12DescriptorHeap *usedHeap = nullptr;
		UINT capacityToCreate = 0;
		AcquireSRWLockExclusive(&gPreSkinDescriptorRingLock);
		const bool needsNewHeap =
			gPreSkinDescriptorRing.device != device ||
			!gPreSkinDescriptorRing.heap ||
			requiredDescriptors > gPreSkinDescriptorRing.capacity ||
			gPreSkinDescriptorRing.offset + requiredDescriptors > gPreSkinDescriptorRing.capacity;
		if (!needsNewHeap) {
			D3D12_CPU_DESCRIPTOR_HANDLE cpu =
				gPreSkinDescriptorRing.heap->GetCPUDescriptorHandleForHeapStart();
			D3D12_GPU_DESCRIPTOR_HANDLE gpu =
				gPreSkinDescriptorRing.heap->GetGPUDescriptorHandleForHeapStart();
			cpu.ptr += static_cast<SIZE_T>(gPreSkinDescriptorRing.offset) *
				gPreSkinDescriptorRing.increment;
			gpu.ptr += static_cast<UINT64>(gPreSkinDescriptorRing.offset) *
				gPreSkinDescriptorRing.increment;
			gPreSkinDescriptorRing.offset += requiredDescriptors;
			usedHeap = gPreSkinDescriptorRing.heap;
			if (usedHeap)
				usedHeap->AddRef();
			*cpuBase = cpu;
			*gpuBase = gpu;
			*increment = gPreSkinDescriptorRing.increment;
			*heapOut = usedHeap;
			ReleaseSRWLockExclusive(&gPreSkinDescriptorRingLock);
			if (usedHeap)
				RetirePreSkinDescriptorHeapForCommandList(commandList, usedHeap);
#if defined(_DEBUG)
			DX12LogManager::DebugJsonFunc("DX12PreSkinDescriptorRing",
				"\"status\":\"reuse\",\"required\":%u,\"capacity\":%u,\"offset\":%u,\"present\":%ld",
				requiredDescriptors,
				gPreSkinDescriptorRing.capacity,
				gPreSkinDescriptorRing.offset,
				DX12RuntimeState::GetPresentCount());
#endif
			return true;
		}
		capacityToCreate = (std::max)(
			PreSkinDescriptorRingInitialCapacity, requiredDescriptors);
		ReleaseSRWLockExclusive(&gPreSkinDescriptorRingLock);

		D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
		heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		heapDesc.NumDescriptors = capacityToCreate;
		heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		ID3D12DescriptorHeap *newHeap = nullptr;
		const auto createBegin = std::chrono::steady_clock::now();
		HRESULT hr = device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&newHeap));
		const auto createEnd = std::chrono::steady_clock::now();
		const auto createElapsedUs =
			std::chrono::duration_cast<std::chrono::microseconds>(createEnd - createBegin).count();
		if (FAILED(hr) || !newHeap)
			return false;

		AcquireSRWLockExclusive(&gPreSkinDescriptorRingLock);
		const bool stillNeedsNewHeap =
			gPreSkinDescriptorRing.device != device ||
			!gPreSkinDescriptorRing.heap ||
			requiredDescriptors > gPreSkinDescriptorRing.capacity ||
			gPreSkinDescriptorRing.offset + requiredDescriptors > gPreSkinDescriptorRing.capacity;
		if (!stillNeedsNewHeap) {
			ReleaseSRWLockExclusive(&gPreSkinDescriptorRingLock);
#if defined(_DEBUG)
			DX12LogManager::DebugJsonFunc("DX12PreSkinDescriptorRing",
				"\"status\":\"create_raced\",\"required\":%u,\"capacity\":%u,\"elapsedUs\":%lld,\"present\":%ld",
				requiredDescriptors,
				capacityToCreate,
				static_cast<long long>(createElapsedUs),
				DX12RuntimeState::GetPresentCount());
#endif
			newHeap->Release();
			continue;
		}
		oldHeap = gPreSkinDescriptorRing.heap;
		oldDevice = gPreSkinDescriptorRing.device != device ? gPreSkinDescriptorRing.device : nullptr;
		if (oldDevice)
			gPreSkinDescriptorRing.device = nullptr;
		device->AddRef();
		gPreSkinDescriptorRing.device = device;
		gPreSkinDescriptorRing.heap = newHeap;
		usedHeap = newHeap;
		usedHeap->AddRef();
		gPreSkinDescriptorRing.capacity = capacityToCreate;
		gPreSkinDescriptorRing.offset = requiredDescriptors;
		gPreSkinDescriptorRing.increment =
			device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		gPreSkinDescriptorRing.present = DX12RuntimeState::GetPresentCount();
		*cpuBase = usedHeap->GetCPUDescriptorHandleForHeapStart();
		*gpuBase = usedHeap->GetGPUDescriptorHandleForHeapStart();
		*increment = gPreSkinDescriptorRing.increment;
		*heapOut = usedHeap;
		ReleaseSRWLockExclusive(&gPreSkinDescriptorRingLock);

		if (oldHeap)
			RetirePreSkinDescriptorHeap(oldHeap);
		if (oldDevice)
			oldDevice->Release();
		if (usedHeap)
			RetirePreSkinDescriptorHeapForCommandList(commandList, usedHeap);
#if defined(_DEBUG)
		DX12LogManager::DebugJsonFunc("DX12PreSkinDescriptorRing",
			"\"status\":\"created\",\"capacity\":%u,\"required\":%u,\"elapsedUs\":%lld,\"present\":%ld",
			capacityToCreate, requiredDescriptors,
			static_cast<long long>(createElapsedUs),
			DX12RuntimeState::GetPresentCount());
#endif
		return true;
	}
}

enum class DX12PsoKind
{
	Graphics,
	Compute
};


#include "DX12IaReplacementExecutor.hpp"
#include "DX12ModRuntimeLoader.hpp"
#include "DX12ModResourceRuntime.hpp"
#include "hotpath/DX12HotPathTimingUtils.hpp"
#include "DX12PreSkinRuntime.hpp"
#include "DX12ModRuntimeState.hpp"
#include "DX12IaOverrideMatcher.hpp"
#include "DX12IaResourceRuntime.hpp"
#include "DX12CommandListRuntimeExecutor.hpp"
#include "DX12ShaderOverrideRuntime.hpp"



