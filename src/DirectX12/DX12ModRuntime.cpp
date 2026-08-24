#include "DX12Core.h"
#include "DX12Hooks.h"
#include "DX12Tracking.h"
#include "DX12Runtime.h"
#include "DX12Resources.h"
#include "DX12Presentation.h"
#include <Shlwapi.h>
#include <stdint.h>
#include <stdio.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cwctype>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include "util.h"
#include "MigotoCommandVmUtils.hpp"
#include "MigotoKeyBindingUtils.hpp"
#include <Windows.h>
#include <d3d12.h>

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
static std::wstring gPendingInitialConfigPath;
static volatile LONG gDeferredInitialModLoadState = 0;
static DWORD gDeferredInitialModLoadStartTick = 0;
DWORD gDeferredInitialModLoadDelayMs = 2000;
static std::wstring gBaseDir;
static std::wstring gShaderFixesDir;
static Bunny::ShaderOverrideMap gShaderOverrides;
static Bunny::ShaderRegexMap gShaderRegexes;
static Bunny::TextureOverrideMap gTextureOverrides;
static Bunny::ResourceMap gModResources;
static Bunny::CommandListMap gCommandLists;
static std::vector<Bunny::MigotoKeyBindingUtils::Binding> gKeyBindings;
static std::vector<std::wstring> gVlrResourceCandidates;
static std::unordered_map<std::wstring, bool> gCommandListRuntimeEffect;
static std::unordered_map<std::wstring, bool> gCommandListPreRuntimeEffect;
static std::unordered_map<std::wstring, bool> gCommandListPostRuntimeEffect;
static std::unordered_map<std::wstring, bool> gTextureOverridePreRuntimeEffect;
static std::unordered_map<std::wstring, bool> gTextureOverridePostRuntimeEffect;
static std::unordered_map<std::wstring, uint32_t> gTextureOverrideSectionIds;
class DX12IaTextureOverrideCandidate
{
public:
	const Bunny::TextureOverrideConfig *config = nullptr;
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
static std::vector<size_t> gIaIndexTextureOverridePrefilterIndex;
static std::unordered_map<uint32_t, std::vector<size_t>> gIaIndexTextureOverridePrefilterByIndexCount;
static std::unordered_map<uint32_t, std::vector<size_t>> gIaVertexTextureOverridePrefilterIndex;
static std::vector<size_t> gIaAnyVertexTextureOverridePrefilterIndex;
static std::unordered_map<uint32_t, std::vector<size_t>> gIaAnyVertexTextureOverridePrefilterByVertexCount;
static std::unordered_map<uint32_t, std::vector<size_t>> gIaMergedVertexTextureOverridePrefilterIndex;
static std::unordered_map<UINT64, std::vector<size_t>> gIaVertexTextureOverridePrefilterByVertexCount;
static std::unordered_map<UINT64, std::vector<size_t>> gIaMergedVertexTextureOverridePrefilterByVertexCount;

// True when any IA prefilter table is indexed by draw count. When false, the
// "does this IA state have any override candidate" answer is draw-arg
// independent, which lets the draw path cache it at IA-state granularity
// (DX11-style) instead of per draw.
static volatile LONG gIaPrefilterCountSensitive = 0;

// The hash-level negative cache records tuples with no global candidates.
// It is draw-context independent and therefore has a higher hit rate.
// key = bits[31:0]=hash | bit[32]=indexBuffer | bits[47:33]=vertexSlot
static std::unordered_set<UINT64> gIaTextureOverrideHashNoCandidates;
class DX12TriggeredTextureOverride
{
public:
	DX12TriggeredTextureOverride() = default;
	DX12TriggeredTextureOverride(const DX12TriggeredTextureOverride &other)
		: config(other.config),
		  sectionId(other.sectionId),
		  preRuntimeEffect(other.preRuntimeEffect),
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
		  preRuntimeEffect(other.preRuntimeEffect),
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
		preRuntimeEffect = other.preRuntimeEffect;
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
		preRuntimeEffect = other.preRuntimeEffect;
		postRuntimeEffect = other.postRuntimeEffect;
		indexBuffer = other.indexBuffer;
		vertexSlot = other.vertexSlot;
		executeCommands = other.executeCommands;
		executeDrawActions = other.executeDrawActions;
		return *this;
	}
	const Bunny::TextureOverrideConfig *config = nullptr;
	uint32_t sectionId = 0;
	bool preRuntimeEffect = true;
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

class DX12CompiledDrawSrvResourceBinding

{

public:
	uint32_t slot = 0;
	std::wstring resource;
	bool pixelStage = true;
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

class DX12CompiledCommandListPlan

{

public:
	class Op
	{
	public:
		enum class Kind
		{
			HandlingSkip,
			SetIndexBuffer,
			SetVertexBuffer,
			SetConstantBuffer,
			SetShaderResource,
			SetUnorderedAccessView,
			Draw,
			Dispatch,
			CheckTextureOverride
		};

		Kind kind = Kind::HandlingSkip;
		Bunny::CommandListTarget target;
		std::wstring resource;
		DX12ModIaReplacement::DrawCall draw;
		DX12ModIaReplacement::DispatchCall dispatch;
	};

	bool unsupported = false;
	bool handlingSkip = false;
	bool hasDescriptorCheckTargets = false;
	std::wstring indexResource;
	std::vector<DX12CompiledVertexResourceBinding> vertexResources;
	std::vector<DX12ModIaReplacement::DrawCall> draws;
	std::vector<DX12ModIaReplacement::DispatchCall> dispatches;
	std::vector<Bunny::CommandListTarget> descriptorCheckTargets;
	std::vector<Op> ops;

	bool NeedsResidentIaState() const
	{
		return !indexResource.empty() || !vertexResources.empty();
	}
};
static std::unordered_map<std::wstring, DX12CompiledCommandListPlan> gCompiledCommandListPlans;

class DX12CompiledTextureOverridePlan

{

public:
	// Source texture hash of the owning TextureOverride section; used by the
	// write-time static replacement short-circuit to look the section up in
	// DX12StaticTextureReplacementTable without re-reading the config.
	uint32_t hash = 0;
	bool hasActions = false;
	bool hasUnsupportedCommandActions = false;
	bool hasPreCommandListPlan = false;
	bool hasPostCommandListPlan = false;
	bool hasMainActionPlan = false;
	bool handlingSkip = false;
	DX12CompiledCommandListPlan preCommandListPlan;
	DX12CompiledCommandListPlan mainActionPlan;
	DX12CompiledCommandListPlan postCommandListPlan;
	std::wstring indexResource;
	std::vector<DX12CompiledVertexResourceBinding> vertexResources;
	std::vector<DX12ModIaReplacement::DrawCall> draws;
	std::vector<DX12ModIaReplacement::DispatchCall> dispatches;
	std::vector<DX12CompiledDrawSrvResourceBinding> drawSrvResources;
	std::wstring drawReplaceThisResource;

	bool NeedsResidentIaState() const
	{
		return !indexResource.empty() ||
			!vertexResources.empty() ||
			(hasMainActionPlan && mainActionPlan.NeedsResidentIaState()) ||
			(hasPreCommandListPlan && preCommandListPlan.NeedsResidentIaState()) ||
			(hasPostCommandListPlan && postCommandListPlan.NeedsResidentIaState());
	}
};
static std::unordered_map<uint32_t, DX12CompiledTextureOverridePlan> gCompiledTextureOverridePlans;

class DX12CompiledShaderOverridePlan

{

public:
	bool unsupported = false;
	bool hasPostCommandListPlan = false;
	DX12CompiledCommandListPlan mainPlan;
	DX12CompiledCommandListPlan postCommandListPlan;
	std::vector<std::wstring> postCommandLists;

	bool NeedsResidentIaState() const
	{
		return mainPlan.NeedsResidentIaState() ||
			(hasPostCommandListPlan && postCommandListPlan.NeedsResidentIaState());
	}
};

class DX12CommandExecutionRuntimeSnapshot

{

public:
	bool anyTextureOverridePreRuntimeEffect = false;
	bool anyTextureOverridePostRuntimeEffect = false;
	std::unordered_map<std::wstring, bool> commandListRuntimeEffect;
	std::unordered_map<std::wstring, bool> commandListPreRuntimeEffect;
	std::unordered_map<std::wstring, bool> commandListPostRuntimeEffect;
	std::unordered_map<std::wstring, std::shared_ptr<const Bunny::CommandListConfig>>
		commandLists;
	std::unordered_map<uint32_t, std::shared_ptr<const Bunny::TextureOverrideConfig>>
		textureOverridesBySectionId;
	std::unordered_map<uint32_t, std::vector<DX12TriggeredTextureOverride>>
		textureOverridesByHash;
	std::vector<DX12TriggeredTextureOverride>
		textureOverridesWithoutDrawContext;
	std::vector<DX12TriggeredTextureOverride>
		textureOverridesGenericDrawContext;
	std::unordered_map<uint32_t, std::vector<DX12TriggeredTextureOverride>>
		textureOverridesByIndexCount;
	std::unordered_map<uint32_t, std::vector<DX12TriggeredTextureOverride>>
		textureOverridesByVertexCount;
	std::unordered_map<uint32_t, std::vector<DX12TriggeredTextureOverride>>
		iaIndexTriggeredTextureOverrides;
	std::unordered_map<UINT64, std::vector<DX12TriggeredTextureOverride>>
		iaMergedVertexTriggeredTextureOverrides;
	std::unordered_map<std::wstring, std::shared_ptr<const DX12CompiledCommandListPlan>>
		compiledCommandListPlans;
	std::unordered_map<std::wstring, std::shared_ptr<const DX12CompiledTextureOverridePlan>>
		compiledTextureOverridePlans;
	std::unordered_map<std::wstring, std::shared_ptr<const DX12CompiledShaderOverridePlan>>
		compiledShaderOverridePlans;
};
static std::atomic<std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot>>
	gCommandExecutionRuntimeSnapshot;

// Static texture replacement table (plan step 3.1): hash of the original
// texture -> the mod resource that unconditionally replaces it. Only
// TextureOverride sections with no draw-context filters, no match_cs / pre-skin
// dependency and no command lists / actions are eligible; those are patched at
// descriptor write time (CreateShaderResourceView / CopyDescriptors) so the
// draw path never has to resolve SRV bindings for them.
class DX12StaticTextureReplacementEntry
{
public:
	uint32_t hash = 0;
	std::wstring resourceName;
	uint32_t sectionId = 0;
	// Published generation this entry was resolved against. The replacement
	// descriptor handle is cached separately and must be re-resolved when the
	// reload generation changes.
	UINT64 reloadGeneration = 0;
};
class DX12StaticTextureReplacementTable
{
public:
	bool enabled = false;
	UINT64 reloadGeneration = 0;
	// Every descriptor-relevant hashed texture override is an unconditional
	// static replacement, and no shader override command list reads
	// checktextureoverride results of SRV targets. Only then may the draw
	// path skip descriptor candidate matching entirely.
	bool allDescriptorOverridesStatic = false;
	// hash -> entry. Built at ini load; published atomically so hot-path
	// readers (CreateShaderResourceView / CopyDescriptors hooks) never take
	// gModLock.
	std::unordered_map<uint32_t, DX12StaticTextureReplacementEntry> byHash;
};
static std::atomic<std::shared_ptr<const DX12StaticTextureReplacementTable>>
	gStaticTextureReplacementSnapshot;
// Low-cost gate mirroring the snapshot for hot paths that must not pay the
// shared_ptr atomic load when no static replacement exists at all.
static volatile LONG gHasStaticTextureReplacements = 0;

// Static IA replacement table (plan step 3.2): hash of the bound IB/VB
// content -> the mod buffer that unconditionally replaces the binding. Only
// TextureOverride sections that purely swap geometry (indexBufferResource /
// vertexBufferResources set, no skip, no draw-context filters, no match_cs /
// pre-skin dependency, no command lists / actions) are eligible, and the
// whole table is disabled when any shader override reads IA
// checktextureoverride results (those need the draw-time match on the
// original binding). Eligible bindings are redirected at IASet time so the
// draw path never re-evaluates them.
class DX12StaticIaReplacementTable
{
public:
	bool enabled = false;
	UINT64 reloadGeneration = 0;
	// IB content hash -> mod buffer resource name.
	std::unordered_map<uint32_t, std::wstring> indexByHash;
	// (vertexSlot << 32) | VB content hash -> mod buffer resource name.
	std::unordered_map<uint64_t, std::wstring> vertexByHashAndSlot;
};
static std::atomic<std::shared_ptr<const DX12StaticIaReplacementTable>>
	gStaticIaReplacementSnapshot;
// Low-cost gate for the IASet bind hot path.
static volatile LONG gHasStaticIaReplacements = 0;

static std::shared_ptr<const DX12StaticIaReplacementTable>
LoadStaticIaReplacementSnapshot()
{
	return gStaticIaReplacementSnapshot.load(std::memory_order_acquire);
}

static std::shared_ptr<const DX12StaticTextureReplacementTable>
LoadStaticTextureReplacementSnapshot()
{
	return gStaticTextureReplacementSnapshot.load(std::memory_order_acquire);
}

// Defined in DX12ModResources.inl (same TU); resets the resolved
// descriptor handle cache when a new table is published.
void DX12NoteStaticTextureReplacementTablePublished();

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
// Written under gModLock on reload; read on the hot path per Draw/Dispatch.
// volatile keeps every read a fresh load (aligned 64-bit loads are atomic on
// x64), so readers never need a LOCK CMPXCHG just to observe the value.
static volatile UINT64 gReloadGeneration = 1;
static volatile UINT64 gShaderOverrideNegativeCacheGeneration = 1;
static volatile LONG gHasVertexLimitRaiseConfigs = 0;
static volatile LONG gHasShaderOverrides = 0;
static volatile LONG gHasShaderRegexes = 0;
static volatile LONG gHasTextureOverrides = 0;
static volatile LONG gHasShaderDescriptorTextureOverrideTriggers = 0;
static volatile LONG gHasDrawSrvTextureReplacementOverrides = 0;
static volatile LONG gHasIaResidentStateRequirements = 0;
static volatile LONG gHasPreSkinTextureOverrideCandidates = 0;
static volatile LONG gHasShaderTriggeredTextureOverrides = 0;
static volatile LONG gHasPresentRuntimeEffect = 0;
static bool gHasPreSkinVlrWithoutMatchCs = false;
static std::unordered_set<uint64_t> gPreSkinMatchCsHashes;

// Adaptive idle counter: after N consecutive frames with zero active pre-skin
// overrides, binding tracking can be skipped to save ~1500+ lock acquisitions/frame.
static volatile LONG gPreSkinBindingIdleFrames = 0;
static volatile LONG gHasActivePreSkinTextureOverridesFast = 0;
static volatile LONG gHasConfirmedPreSkinSectionsFast = 0;
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
static std::deque<UINT64> gPreSkinUavMatchCacheLru;
static constexpr size_t MaxPreSkinUavMatchCacheEntries = 4096;
static size_t gPreSkinUavMatchCacheGrowthBudget = MaxPreSkinUavMatchCacheEntries;


static UINT64 MakePreSkinUavMatchCacheKey(
	ID3D12GraphicsCommandList *commandList, uint64_t csHash, UINT64 bindingHash,
	UINT64 bindingDependencyFingerprint)
{
	(void)commandList;
	UINT64 key = 0x4f1bbcdc9cb7a2c5ull;
	key = HashCombine64(key, gReloadGeneration);
	key = HashCombine64(key, csHash);
	key = HashCombine64(key, bindingHash);
	key = HashCombine64(key, bindingDependencyFingerprint);
	key = HashCombine64(key, DX12GetIaHashContentGeneration());
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
static void ClearPreSkinStickyMatchCache();
class DX12ShaderOverridePsoMatchCache
{
public:
	UINT64 generation = 0;
	std::vector<const Bunny::ShaderOverrideConfig*> drawConfigs;
	std::vector<const Bunny::ShaderOverrideConfig*> dispatchConfigs;
	std::vector<Bunny::ShaderOverrideConfig> regexDrawConfigs;
	std::vector<Bunny::ShaderOverrideConfig> regexDispatchConfigs;
	DX12IaHashTargetPlan drawTargetPlan;
	DX12IaHashTargetPlan drawDirectTargetPlan;
	DX12DrawMatchUsage drawMatchUsage;
	bool drawDirectTextureChecksOnly = false;
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
static constexpr size_t MaxTextureOverrideLookupCacheEntries = 32768;

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
	uint32_t firstInstance = 0;
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
			firstInstance == other.firstInstance &&
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

static std::unordered_map<UINT64, DX12ShaderOverrideNegativeCacheKey> gShaderOverrideNegativeCache;
static SRWLOCK gShaderOverrideNegativeCacheLock = SRWLOCK_INIT;
static constexpr size_t MaxShaderOverrideNegativeCacheEntries = 16384;

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
static std::deque<DX12ComputeUavProducer> gRecentComputeUavs;
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

static std::map<std::wstring, DX12ActivePreSkinTextureOverride> gActivePreSkinTextureOverrides;
// Plan fix C: reverse index from pre-skin output resource to the configured
// sections publishing it. DX12ModFindActivePreSkinProducerForOutput used to
// hold the lock and linearly scan the whole active table per call; the
// pre-skin IA replay path (ReplayActiveVertexBuffers) calls it once per VB
// slot per matching draw, so that scan dominated the frame cost. The index
// stores section names (not pointers) because the map entries are rewritten
// in place on every store and section names are the stable identity.
static std::unordered_map<ID3D12Resource*, std::vector<std::wstring>> gPreSkinProducerSectionsByOutputResource;
// A successful producer match proves that this configured section participates
// in PreSkin for the current mod configuration. Keep that proof independently
// from the frame-local resource binding below: DX12 command lists may be recorded
// out of execution order, and Present/FG/capture can run before a consumer draw is
// recorded. Clearing this set at Present would make that draw fail match_cs and
// intermittently remove the entire replacement model.
static std::unordered_set<std::wstring> gConfirmedPreSkinSections;
class DX12ActivePreSkinSectionSnapshotTlsCache
{
public:
	UINT64 generation = 0;
	std::unordered_set<std::wstring> sections;
};

static UINT64 CurrentPreSkinActiveGeneration()
{
	return static_cast<UINT64>(InterlockedCompareExchange64(
		reinterpret_cast<volatile LONG64*>(&gPreSkinActiveGeneration), 0, 0));
}

UINT64 DX12ModGetPreSkinActiveGenerationFast()
{
	return CurrentPreSkinActiveGeneration();
}

static const std::unordered_set<std::wstring> &GetActivePreSkinSectionsSnapshot(
	UINT64 *generationOut = nullptr)
{
	static thread_local DX12ActivePreSkinSectionSnapshotTlsCache cache;
	const UINT64 generation = CurrentPreSkinActiveGeneration();
	if (generationOut)
		*generationOut = generation;
	if (cache.generation == generation)
		return cache.sections;

	cache.generation = generation;
	cache.sections.clear();
	if (InterlockedCompareExchange(&gHasActivePreSkinTextureOverridesFast, 0, 0) == 0 &&
	    InterlockedCompareExchange(&gHasConfirmedPreSkinSectionsFast, 0, 0) == 0)
		return cache.sections;

	AcquireSRWLockShared(&gPreSkinLock);
	cache.sections.reserve(
		gActivePreSkinTextureOverrides.size() + gConfirmedPreSkinSections.size());
	for (const auto &item : gActivePreSkinTextureOverrides)
		cache.sections.insert(item.first);
	cache.sections.insert(
		gConfirmedPreSkinSections.begin(), gConfirmedPreSkinSections.end());
	ReleaseSRWLockShared(&gPreSkinLock);
	return cache.sections;
}


static void BumpPreSkinActiveGeneration()
{
	InterlockedIncrement64(reinterpret_cast<volatile LONG64*>(&gPreSkinActiveGeneration));
}

static void MoveActivePreSkinTextureOverrideResourceLocked(
	const std::wstring &section, DX12ActivePreSkinTextureOverride *active,
	std::vector<ID3D12Resource*> *releaseResources)
{
	if (!active)
		return;
	// Plan fix C: keep the output-resource reverse index in sync.
	if (active->outputResource) {
		auto indexIt = gPreSkinProducerSectionsByOutputResource.find(active->outputResource);
		if (indexIt != gPreSkinProducerSectionsByOutputResource.end()) {
			std::vector<std::wstring> &sections = indexIt->second;
			sections.erase(std::remove(sections.begin(), sections.end(), section), sections.end());
			if (sections.empty())
				gPreSkinProducerSectionsByOutputResource.erase(indexIt);
		}
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
		!gActivePreSkinTextureOverrides.empty();
	for (auto &item : gActivePreSkinTextureOverrides)
		MoveActivePreSkinTextureOverrideResourceLocked(item.first, &item.second, releaseResources);
	gActivePreSkinTextureOverrides.clear();
	gPreSkinProducerSectionsByOutputResource.clear();
	InterlockedExchange(&gHasActivePreSkinTextureOverridesFast, 0);
	// This clears only the transient producer publication. Never clear
	// gPreSkinOutputResizeCache here: Present calls this function every frame,
	// while DX12 may record the corresponding consumer draw after that Present.
	// DLSS FG and NVIDIA capture widen this window. The consumer must fall back
	// to the fence-owned cache in DX12ModResolvePreSkinOutputResizeCacheByGpuVa.
	DX12ModClearPreSkinOutputResizeMap();
	if (changed)
		BumpPreSkinActiveGeneration();
}

static void StoreActivePreSkinTextureOverrideLocked(
	const std::wstring &section, const DX12ActivePreSkinTextureOverride &active,
	std::vector<ID3D12Resource*> *releaseResources)
{
	// Do not use frame-local recording order as match_cs proof. Once the actual
	// PreSkin producer has matched, retain the section until configuration reload
	// so draws recorded across a Present or on another command list remain valid.
	if (gConfirmedPreSkinSections.insert(section).second)
		InterlockedExchange(&gHasConfirmedPreSkinSectionsFast, 1);
	auto existing = gActivePreSkinTextureOverrides.find(section);
	const DX12ActivePreSkinTextureOverride *existingPtr =
		existing != gActivePreSkinTextureOverrides.end() ? &existing->second : nullptr;
	// Plan fix G: the pre-skin dispatch publishes the same output buffer
	// identity every frame (the GPU rewrites its contents, but the active
	// entry - buffer, byte width, stride, producer slot layout - is stable).
	// Only bump the active generation when that identity actually changed;
	// bumping every frame invalidated the per-draw replay gate and the
	// per-slot pre-skin redirect negative caches, forcing the full GPUVA
	// lookup chain on every matching draw every frame.
	const bool sameContent = existingPtr &&
		existingPtr->outputResource == active.outputResource &&
		existingPtr->outputByteWidth == active.outputByteWidth &&
		existingPtr->outputStride == active.outputStride &&
		existingPtr->producer.rootParameterIndex == active.producer.rootParameterIndex &&
		existingPtr->producer.rangeIndex == active.producer.rangeIndex &&
		existingPtr->producer.shaderRegister == active.producer.shaderRegister &&
		existingPtr->producer.descriptorOffset == active.producer.descriptorOffset;
	if (sameContent) {
		// Keep the existing identity and generation, while releasing the old
		// reference after replacing the published metadata.
		ID3D12Resource *oldResource = existingPtr->outputResource;
		gActivePreSkinTextureOverrides[section] = active;
		if (releaseResources && oldResource)
			releaseResources->push_back(oldResource);
		return;
	}
	if (existing != gActivePreSkinTextureOverrides.end())
		MoveActivePreSkinTextureOverrideResourceLocked(section, &existing->second, releaseResources);
	InterlockedExchange(&gHasActivePreSkinTextureOverridesFast, 1);
	gActivePreSkinTextureOverrides[section] = active;
	// Plan fix C: publish the reverse index entry for the new output resource.
	if (active.outputResource) {
		gPreSkinProducerSectionsByOutputResource[active.outputResource].push_back(section);
	}
	BumpPreSkinActiveGeneration();
}

bool DX12ModMayNeedPreSkinIaViewExpansion(
	const D3D12_VERTEX_BUFFER_VIEW *views, const bool *hasViews, UINT count)
{
	if (!views || !hasViews || !count ||
		InterlockedCompareExchange(&gHasActivePreSkinTextureOverridesFast, 0, 0) == 0)
		return false;

	AcquireSRWLockShared(&gPreSkinLock);
	for (const auto &item : gActivePreSkinTextureOverrides) {
		const DX12ActivePreSkinTextureOverride &active = item.second;
		if (!active.outputResource || !active.outputByteWidth)
			continue;
		const D3D12_RESOURCE_DESC desc = active.outputResource->GetDesc();
		if (desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER ||
			active.outputByteWidth <= desc.Width)
			continue;
		const UINT64 outputGpuVa = active.outputResource->GetGPUVirtualAddress();
		if (!outputGpuVa)
			continue;
		for (UINT slot = 0; slot < count; ++slot) {
			if (!hasViews[slot] || views[slot].BufferLocation < outputGpuVa)
				continue;
			if (views[slot].BufferLocation - outputGpuVa < desc.Width) {
				ReleaseSRWLockShared(&gPreSkinLock);
				return true;
			}
		}
	}
	ReleaseSRWLockShared(&gPreSkinLock);
	return false;
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
	// Plan fix C: O(1) reverse-index lookup instead of a locked linear scan
	// of the whole active table (which the pre-skin IA replay path hit once
	// per VB slot per matching draw).
	auto indexIt = gPreSkinProducerSectionsByOutputResource.find(outputResource);
	if (indexIt != gPreSkinProducerSectionsByOutputResource.end()) {
		for (const std::wstring &section : indexIt->second) {
			auto item = gActivePreSkinTextureOverrides.find(section);
			if (item == gActivePreSkinTextureOverrides.end())
				continue;
			const DX12ActivePreSkinTextureOverride &active = item->second;
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
	}
	ReleaseSRWLockShared(&gPreSkinLock);
	return found;
}


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

// Cross-frame cache for patched CBV buffers.
// This avoids the first CreateCommittedResource call on every frame.
// Identical CBV inputs reuse the upload buffer and CBV descriptor.
class DX12PreSkinCbvPatchCacheKey
{
public:
	ID3D12Resource *resource = nullptr;
	UINT64 sourceOffset = 0;
	UINT viewBytes = 0;
	UINT overrideVertexCount = 0;
	UINT64 contentHash = 0;

	bool operator<(const DX12PreSkinCbvPatchCacheKey &rhs) const
	{
		if (resource != rhs.resource) return resource < rhs.resource;
		if (sourceOffset != rhs.sourceOffset) return sourceOffset < rhs.sourceOffset;
		if (viewBytes != rhs.viewBytes) return viewBytes < rhs.viewBytes;
		if (overrideVertexCount != rhs.overrideVertexCount)
			return overrideVertexCount < rhs.overrideVertexCount;
		return contentHash < rhs.contentHash;
	}
};

class DX12PreSkinCbvPatchCacheEntry
{
public:
	ID3D12Resource *patchedResource = nullptr;       // UPLOAD heap buffer, reused
	ID3D12DescriptorHeap *descriptorHeap = nullptr;  // owns cbvCpu
	D3D12_CPU_DESCRIPTOR_HANDLE cbvCpu = {};         // CBV descriptor pointing to patchedResource
	UINT64 generation = 0;                            // mod reload generation when created
	std::vector<unsigned char> patchedBytes;
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
	key = HashCombine64(key, cbvs.size());
	for (const DX12CurrentComputeUavBinding &cbv : cbvs)
		key = HashComputeBindingLayout(key, cbv, false);
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
	append(srvByteWidth);
	// Deliberately NOT part of the key: srv.descriptor.gpuVirtualAddress and
	// srv.descriptor.resource. The game rewrites its descriptor arena every
	// frame, so those fields change per frame even when the same logical slot
	// is bound; including them made this "stable" cache miss every frame and
	// forced EnsureLoadedResourceForPreSkin + EnsureResourceSrvLocked to run
	// on every apply. The probe outcome depends only on the slot layout and
	// view shape plus the mod config (the replacement SRV always comes from
	// the mod's own resource), never on which game resource happens to be
	// bound at the slot - the mod replaces the slot wholesale.
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
	const Bunny::TextureOverrideConfig *config = nullptr;
	uint32_t hash = 0;
	std::wstring resourceName;
	std::vector<DX12PreSkinSrvReplacementBinding> srvReplacements;
};

class DX12PreSkinTextureOverrideCandidate

{

public:
	const Bunny::TextureOverrideConfig *config = nullptr;
	uint32_t hash = 0;
	UINT64 identity = 0;
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
	ID3D12Resource *drawSrvTextureResource = nullptr;
	ID3D12Resource *drawSrvUploadResource = nullptr;
	ID3D12DescriptorHeap *uavHeap = nullptr;
	ID3D12DescriptorHeap *srvHeap = nullptr;
	ID3D12DescriptorHeap *drawSrvHeap = nullptr;
	std::map<UINT64, ID3D12DescriptorHeap*> drawSrvCompatibleHeaps;
	std::map<UINT64, D3D12_CPU_DESCRIPTOR_HANDLE> drawSrvCompatibleCpus;
	bool failed = false;
	UINT64 byteWidth = 0;
	UINT stride = 0;
	DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
	D3D12_CPU_DESCRIPTOR_HANDLE uavCpu = {};
	D3D12_GPU_DESCRIPTOR_HANDLE uavGpu = {};
	D3D12_CPU_DESCRIPTOR_HANDLE srvCpu = {};
	D3D12_CPU_DESCRIPTOR_HANDLE drawSrvCpu = {};
	bool uavInitialized = false;
	bool uavWritten = false;
	bool uavValid = false;
	// UAV validity is frame-local. Store its frame epoch instead of walking the
	// complete resident-resource map at every Present just to clear two booleans.
	LONG uavValidPresent = -1;
	bool uavBarrierPending = false;
	UINT64 uavByteWidth = 0;
	UINT uavStride = 0;
	UINT64 srvByteWidth = 0;
	UINT srvStride = 0;
	DXGI_FORMAT drawSrvFormat = DXGI_FORMAT_UNKNOWN;
	UINT drawSrvWidth = 0;
	UINT drawSrvHeight = 0;
	UINT drawSrvMipLevels = 0;
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
	ID3D12Resource *outputVertexResource = nullptr;
	ID3D12Resource *originalOutputResource = nullptr;
	UINT64 originalOutputByteWidth = 0;
	UINT outputResizeSlot = 0;
	UINT64 byteWidth = 0;
	UINT stride = 0;
	bool outputUavResized = false;
	UINT64 producerSerial = 0;
	uint64_t computeShaderHash = 0;
	UINT64 bindingFingerprint = 0;
	uint32_t triggerHash = 0;
	bool hasActiveTextureOverride = false;
	Bunny::TextureOverrideConfig activeConfig;
	DX12ComputeUavProducer activeProducer;
	class DescriptorRestore
	{
	public:
		SIZE_T targetCpu = 0;
		ID3D12Device *device = nullptr;
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
static volatile LONG gHasActivePreSkinReplacement = 0;
// Monotonic mutation generation for gActivePreSkinReplacements. Every insert
// and erase bumps it, so a command-list-local TLS negative cache can prove
// "this list has no pending restore right now" without taking gPreSkinLock on
// every draw while a replacement is pending anywhere (DX11 has no global lock
// on the draw path). Recording is single-threaded per command list, so a TLS
// cache keyed by the list pointer is exact; the generation covers inserts and
// erases made by other threads (cross-thread recording, list destruction).
static std::atomic<UINT64> gPreSkinReplacementMutationGeneration{0};
static void BumpPreSkinReplacementMutationGeneration()
{
	gPreSkinReplacementMutationGeneration.fetch_add(1, std::memory_order_relaxed);
}
class DX12RetiredPreSkinResource
{
public:
	ID3D12Resource *resource = nullptr;
	// GPU completion, rather than Present cadence, owns retired resources.
	// retirePresent remains only as a conservative fallback when signaling the
	// private queue fence is unavailable.
	ID3D12Fence *completionFence = nullptr;
	UINT64 completionValue = 0;
	LONG retirePresent = 0;
};
static std::deque<DX12RetiredPreSkinResource> gRetiredPreSkinResources;
// Present maintenance is called once per frame. Avoid taking the retirement
// lock when the GPU has nothing pending to reclaim.
static volatile LONG gHasRetiredPreSkinObjectsFast = 0;
static constexpr LONG PreSkinRetiredObjectPresentDelay = 120;

class DX12PreSkinOutputResizeCacheEntry
{
public:
	class CompletionPoint
	{
	public:
		ID3D12Fence *fence = nullptr;
		UINT64 value = 0;
	};

	ID3D12Resource *originalResource = nullptr;
	ID3D12Resource *resizedResource = nullptr;
	ID3D12Resource *vertexResource = nullptr;
	UINT64 originalGpuVa = 0;
	UINT64 originalByteWidth = 0;
	UINT64 vertexGpuVa = 0;
	ID3D12DescriptorHeap *descriptorHeap = nullptr;
	D3D12_CPU_DESCRIPTOR_HANDLE descriptorCpu = {};
	std::vector<IUnknown*> pendingCommandLists;
	std::vector<CompletionPoint> completionPoints;
	UINT64 useSerial = 0;
};

static SRWLOCK gPreSkinOutputResizeCacheLock = SRWLOCK_INIT;
static std::map<DX12PreSkinOutputResizeCacheIdentity, DX12PreSkinOutputResizeCacheEntry>
	gPreSkinOutputResizeCache;
static UINT64 gPreSkinOutputResizeCacheUseSerial = 0;
static constexpr size_t PreSkinOutputResizeCacheCapacity = 32;
static volatile LONG gHasPendingPreSkinOutputResizeSubmissionWorkFast = 0;
static volatile LONG gHasPreSkinOutputResizeCacheEntriesFast = 0;

static IUnknown *PreSkinCommandListIdentity(IUnknown *commandList);

static bool IsPreSkinOutputResizeCacheEntryReusable(
	const DX12PreSkinOutputResizeCacheEntry &entry)
{
	if (!entry.pendingCommandLists.empty())
		return false;
	for (const auto &completion : entry.completionPoints) {
		if (completion.fence && completion.value &&
		    completion.fence->GetCompletedValue() < completion.value)
			return false;
	}
	return true;
}

static void TrackPreSkinOutputResizeCacheEntryUse(
	DX12PreSkinOutputResizeCacheEntry *entry,
	IUnknown *commandListIdentity)
{
	if (!entry)
		return;
	if (commandListIdentity &&
	    std::find(entry->pendingCommandLists.begin(),
		entry->pendingCommandLists.end(), commandListIdentity) ==
		entry->pendingCommandLists.end()) {
		entry->pendingCommandLists.push_back(commandListIdentity);
		InterlockedExchange(
			&gHasPendingPreSkinOutputResizeSubmissionWorkFast, 1);
	}
}

static void ReleasePreSkinOutputResizeCacheEntry(
	DX12PreSkinOutputResizeCacheEntry *entry)
{
	if (!entry)
		return;
	if (entry->originalResource)
		entry->originalResource->Release();
	if (entry->resizedResource)
		entry->resizedResource->Release();
	if (entry->vertexResource)
		entry->vertexResource->Release();
	if (entry->descriptorHeap)
		entry->descriptorHeap->Release();
	for (auto &completion : entry->completionPoints) {
		if (completion.fence)
			completion.fence->Release();
	}
	*entry = DX12PreSkinOutputResizeCacheEntry();
}

static void ClearPreSkinOutputResizeCache()
{
	std::vector<DX12PreSkinOutputResizeCacheEntry> releaseEntries;
	AcquireSRWLockExclusive(&gPreSkinOutputResizeCacheLock);
	releaseEntries.reserve(gPreSkinOutputResizeCache.size());
	for (auto &item : gPreSkinOutputResizeCache)
		releaseEntries.push_back(item.second);
	gPreSkinOutputResizeCache.clear();
	gPreSkinOutputResizeCacheUseSerial = 0;
	InterlockedExchange(
		&gHasPendingPreSkinOutputResizeSubmissionWorkFast, 0);
	InterlockedExchange(&gHasPreSkinOutputResizeCacheEntriesFast, 0);
	ReleaseSRWLockExclusive(&gPreSkinOutputResizeCacheLock);
	for (DX12PreSkinOutputResizeCacheEntry &entry : releaseEntries)
		ReleasePreSkinOutputResizeCacheEntry(&entry);
}

static bool AcquirePreSkinOutputResizeCacheEntry(
	ID3D12Device *device, ID3D12GraphicsCommandList *commandList,
	ID3D12Resource *originalResource,
	UINT64 requiredByteWidth, UINT stride, UINT elementCount,
	ID3D12Resource **resizedResourceOut,
	ID3D12Resource **vertexResourceOut,
	ID3D12DescriptorHeap **descriptorHeapOut,
	D3D12_CPU_DESCRIPTOR_HANDLE *descriptorCpuOut,
	bool *cacheHitOut, UINT *ringSlotOut)
{
	if (resizedResourceOut)
		*resizedResourceOut = nullptr;
	if (vertexResourceOut)
		*vertexResourceOut = nullptr;
	if (descriptorHeapOut)
		*descriptorHeapOut = nullptr;
	if (descriptorCpuOut)
		*descriptorCpuOut = {};
	if (cacheHitOut)
		*cacheHitOut = false;
	if (ringSlotOut)
		*ringSlotOut = 0;
	if (!device || !originalResource || !requiredByteWidth || !stride || !elementCount ||
	    !resizedResourceOut || !vertexResourceOut || !descriptorHeapOut ||
	    !descriptorCpuOut)
		return false;
	const D3D12_RESOURCE_DESC originalDesc = originalResource->GetDesc();
	const UINT64 originalGpuVa = originalResource->GetGPUVirtualAddress();
	if (originalDesc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER ||
		!originalGpuVa || !originalDesc.Width)
		return false;

	DX12PreSkinOutputResizeCacheIdentity baseKey = {
		reinterpret_cast<std::uintptr_t>(originalResource), requiredByteWidth, stride, 0
	};
	DX12PreSkinOutputResizeCacheIdentity key = baseKey;
	IUnknown *commandListIdentity = PreSkinCommandListIdentity(commandList);
	auto retainEntry = [resizedResourceOut, vertexResourceOut, descriptorHeapOut,
		descriptorCpuOut](DX12PreSkinOutputResizeCacheEntry &entry) {
		if (!entry.resizedResource || !entry.vertexResource ||
		    !entry.descriptorHeap || !entry.descriptorCpu.ptr)
			return false;
		entry.resizedResource->AddRef();
		entry.vertexResource->AddRef();
		entry.descriptorHeap->AddRef();
		*resizedResourceOut = entry.resizedResource;
		*vertexResourceOut = entry.vertexResource;
		*descriptorHeapOut = entry.descriptorHeap;
		*descriptorCpuOut = entry.descriptorCpu;
		return true;
	};

	AcquireSRWLockExclusive(&gPreSkinOutputResizeCacheLock);
	auto cached = gPreSkinOutputResizeCache.find(baseKey);
	if (cached != gPreSkinOutputResizeCache.end()) {
		key = cached->first;
		cached->second.useSerial = ++gPreSkinOutputResizeCacheUseSerial;
		TrackPreSkinOutputResizeCacheEntryUse(
			&cached->second, commandListIdentity);
		const bool retained = retainEntry(cached->second);
		ReleaseSRWLockExclusive(&gPreSkinOutputResizeCacheLock);
		if (cacheHitOut)
			*cacheHitOut = retained;
		if (ringSlotOut)
			*ringSlotOut = key.ringSlot;
		return retained;
	}

	// Keep one replacement resource per original PreSkin output. Consumers bind
	// only the original GPU VA and often record on a different command list, so
	// they cannot identify a per-frame ring slot. A stable replacement inherits
	// the application's ordering for the original resource: producer copies and
	// consumer draws observe the same queue/fence dependency chain. Rotating the
	// replacement here lets a later recorded frame overwrite the global VA map
	// before an earlier frame records its draw, which appears as whole-model
	// flicker under FG/capture.
	key.ringSlot = 0;
	if (ringSlotOut)
		*ringSlotOut = key.ringSlot;
	ReleaseSRWLockExclusive(&gPreSkinOutputResizeCacheLock);

	D3D12_HEAP_PROPERTIES heapProperties = {};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
	heapProperties.CreationNodeMask = 1;
	heapProperties.VisibleNodeMask = 1;
	D3D12_RESOURCE_DESC resourceDesc = {};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resourceDesc.Width = requiredByteWidth;
	resourceDesc.Height = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	ID3D12Resource *createdResource = nullptr;
	HRESULT resourceHr = device->CreateCommittedResource(
		&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
		IID_PPV_ARGS(&createdResource));
	if (FAILED(resourceHr) || !createdResource)
		return false;
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	ID3D12Resource *createdVertexResource = nullptr;
	resourceHr = device->CreateCommittedResource(
		&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc,
		D3D12_RESOURCE_STATE_COMMON, nullptr,
		IID_PPV_ARGS(&createdVertexResource));
	if (FAILED(resourceHr) || !createdVertexResource) {
		createdResource->Release();
		return false;
	}

	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.NumDescriptors = 1;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ID3D12DescriptorHeap *createdHeap = nullptr;
	HRESULT heapHr = device->CreateDescriptorHeap(
		&heapDesc, IID_PPV_ARGS(&createdHeap));
	if (FAILED(heapHr) || !createdHeap) {
		createdResource->Release();
		createdVertexResource->Release();
		return false;
	}
	const D3D12_CPU_DESCRIPTOR_HANDLE createdCpu =
		createdHeap->GetCPUDescriptorHandleForHeapStart();
	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	uavDesc.Format = DXGI_FORMAT_UNKNOWN;
	uavDesc.Buffer.FirstElement = 0;
	uavDesc.Buffer.StructureByteStride = stride;
	uavDesc.Buffer.NumElements = elementCount;
	uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
	device->CreateUnorderedAccessView(
		createdResource, nullptr, &uavDesc, createdCpu);

	DX12PreSkinOutputResizeCacheEntry releaseEntry;
	AcquireSRWLockExclusive(&gPreSkinOutputResizeCacheLock);
	cached = gPreSkinOutputResizeCache.find(key);
	if (cached == gPreSkinOutputResizeCache.end()) {
		if (gPreSkinOutputResizeCache.size() >= PreSkinOutputResizeCacheCapacity) {
			auto oldest = gPreSkinOutputResizeCache.end();
			for (auto it = gPreSkinOutputResizeCache.begin();
			     it != gPreSkinOutputResizeCache.end(); ++it) {
				if (!IsPreSkinOutputResizeCacheEntryReusable(it->second))
					continue;
				if (oldest == gPreSkinOutputResizeCache.end() ||
				    it->second.useSerial < oldest->second.useSerial)
					oldest = it;
			}
			if (oldest != gPreSkinOutputResizeCache.end()) {
				releaseEntry = oldest->second;
				gPreSkinOutputResizeCache.erase(oldest);
			}
		}
		DX12PreSkinOutputResizeCacheEntry entry;
		originalResource->AddRef();
		entry.originalResource = originalResource;
		entry.resizedResource = createdResource;
		entry.vertexResource = createdVertexResource;
		entry.originalGpuVa = originalGpuVa;
		entry.originalByteWidth = originalDesc.Width;
		entry.vertexGpuVa = createdVertexResource->GetGPUVirtualAddress();
		entry.descriptorHeap = createdHeap;
		entry.descriptorCpu = createdCpu;
		if (commandListIdentity) {
			entry.pendingCommandLists.push_back(commandListIdentity);
			InterlockedExchange(
				&gHasPendingPreSkinOutputResizeSubmissionWorkFast, 1);
		}
		entry.useSerial = ++gPreSkinOutputResizeCacheUseSerial;
		cached = gPreSkinOutputResizeCache.emplace(key, entry).first;
		InterlockedExchange(&gHasPreSkinOutputResizeCacheEntriesFast, 1);
		createdResource = nullptr;
		createdVertexResource = nullptr;
		createdHeap = nullptr;
	} else {
		cached->second.useSerial = ++gPreSkinOutputResizeCacheUseSerial;
		TrackPreSkinOutputResizeCacheEntryUse(
			&cached->second, commandListIdentity);
	}
	const bool retained = retainEntry(cached->second);
	ReleaseSRWLockExclusive(&gPreSkinOutputResizeCacheLock);
	ReleasePreSkinOutputResizeCacheEntry(&releaseEntry);
	if (createdResource)
		createdResource->Release();
	if (createdVertexResource)
		createdVertexResource->Release();
	if (createdHeap)
		createdHeap->Release();
	return retained;
}

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
	ID3D12Fence *completionFence = nullptr;
	UINT64 completionValue = 0;
	LONG retirePresent = 0;
};

static SRWLOCK gPreSkinDescriptorRingLock = SRWLOCK_INIT;
static DX12PreSkinDescriptorRing gPreSkinDescriptorRing;
static std::deque<DX12RetiredPreSkinDescriptorHeap> gRetiredPreSkinDescriptorHeaps;
static constexpr UINT PreSkinDescriptorRingInitialCapacity = 4096;
static constexpr UINT PreSkinDescriptorRingMaximumCapacity = 1000000;

class DX12PreSkinPendingSubmission

{

public:
	std::vector<ID3D12Resource*> resources;
	std::vector<ID3D12DescriptorHeap*> descriptorHeaps;
};

class DX12PooledPreSkinDescriptorRestoreHeap
{
public:
	ID3D12Device *device = nullptr;
	ID3D12DescriptorHeap *heap = nullptr;
};

class DX12PreSkinReleaseBatch

{

public:
	std::vector<ID3D12Resource*> resources;
	std::vector<ID3D12DescriptorHeap*> descriptorHeaps;
	std::vector<ID3D12Fence*> fences;
	std::vector<ID3D12Device*> devices;
	std::vector<ID3D12CommandQueue*> queues;
};

static SRWLOCK gPreSkinRetireLock = SRWLOCK_INIT;
static std::unordered_map<IUnknown*, DX12PreSkinPendingSubmission> gPendingPreSkinSubmissions;
static volatile LONG gHasPendingPreSkinSubmissionWorkFast = 0;
static SRWLOCK gPreSkinDescriptorRestoreHeapPoolLock = SRWLOCK_INIT;
static std::vector<DX12PooledPreSkinDescriptorRestoreHeap> gPreSkinDescriptorRestoreHeapPool;
static constexpr size_t PreSkinDescriptorRestoreHeapPoolCapacity = 64;

static void PublishPendingPreSkinSubmissionWorkLocked()
{
	const bool hasWork =
		!gPendingPreSkinSubmissions.empty();
	InterlockedExchange(
		&gHasPendingPreSkinSubmissionWorkFast,
		hasWork ? 1 : 0);
}

static bool AcquirePreSkinDescriptorRestoreHeap(
	ID3D12Device *device,
	ID3D12DescriptorHeap **heapOut,
	ID3D12Device **retainedDeviceOut)
{
	if (!device || !heapOut || !retainedDeviceOut)
		return false;
	*heapOut = nullptr;
	*retainedDeviceOut = nullptr;

	AcquireSRWLockExclusive(&gPreSkinDescriptorRestoreHeapPoolLock);
	for (size_t index = gPreSkinDescriptorRestoreHeapPool.size(); index > 0; --index) {
		DX12PooledPreSkinDescriptorRestoreHeap &entry =
			gPreSkinDescriptorRestoreHeapPool[index - 1];
		if (entry.device != device || !entry.heap)
			continue;
		*heapOut = entry.heap;
		*retainedDeviceOut = entry.device;
		gPreSkinDescriptorRestoreHeapPool.erase(
			gPreSkinDescriptorRestoreHeapPool.begin() + static_cast<ptrdiff_t>(index - 1));
		ReleaseSRWLockExclusive(&gPreSkinDescriptorRestoreHeapPoolLock);
		return true;
	}
	ReleaseSRWLockExclusive(&gPreSkinDescriptorRestoreHeapPoolLock);

	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.NumDescriptors = 1;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	ID3D12DescriptorHeap *heap = nullptr;
	HRESULT hr = device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&heap));
	if (FAILED(hr) || !heap)
		return false;
	device->AddRef();
	*heapOut = heap;
	*retainedDeviceOut = device;
	return true;
}

static void RecyclePreSkinDescriptorRestoreHeap(
	ID3D12Device *device,
	ID3D12DescriptorHeap *heap)
{
	if (!device) {
		if (heap)
			heap->Release();
		return;
	}
	if (!heap) {
		device->Release();
		return;
	}

	bool pooled = false;
	AcquireSRWLockExclusive(&gPreSkinDescriptorRestoreHeapPoolLock);
	if (gPreSkinDescriptorRestoreHeapPool.size() <
			PreSkinDescriptorRestoreHeapPoolCapacity) {
		gPreSkinDescriptorRestoreHeapPool.push_back({ device, heap });
		pooled = true;
	}
	ReleaseSRWLockExclusive(&gPreSkinDescriptorRestoreHeapPoolLock);
	if (!pooled) {
		heap->Release();
		device->Release();
	}
}

static void DrainPreSkinDescriptorRestoreHeapPool(
	DX12PreSkinReleaseBatch *releaseBatch)
{
	if (!releaseBatch)
		return;
	AcquireSRWLockExclusive(&gPreSkinDescriptorRestoreHeapPoolLock);
	for (auto &entry : gPreSkinDescriptorRestoreHeapPool) {
		if (entry.heap)
			releaseBatch->descriptorHeaps.push_back(entry.heap);
		if (entry.device)
			releaseBatch->devices.push_back(entry.device);
	}
	gPreSkinDescriptorRestoreHeapPool.clear();
	ReleaseSRWLockExclusive(&gPreSkinDescriptorRestoreHeapPoolLock);
}

static void ReleasePreSkinDescriptorRestores(
	std::vector<DX12PreSkinReplacementState::DescriptorRestore> *restores)
{
	if (!restores)
		return;
	for (auto &restore : *restores) {
		RecyclePreSkinDescriptorRestoreHeap(restore.device, restore.heap);
		restore.device = nullptr;
		restore.heap = nullptr;
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
	for (ID3D12Fence *fence : batch->fences) {
		if (fence)
			fence->Release();
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
	batch->fences.clear();
	batch->queues.clear();
	batch->devices.clear();
}

static void MovePendingPreSkinSubmission(
	DX12PreSkinPendingSubmission *source,
	DX12PreSkinPendingSubmission *target)
{
	if (!source || !target)
		return;
	DX12DescriptorArenaUtils::MoveUniqueOwned(
		&source->resources, &target->resources);
	DX12DescriptorArenaUtils::MoveUniqueOwned(
		&source->descriptorHeaps, &target->descriptorHeaps);
}

static void MovePendingPreSkinSubmissionForCommandListLocked(
	IUnknown *identity,
	DX12PreSkinPendingSubmission *target)
{
	if (!identity || !target)
		return;
	auto it = gPendingPreSkinSubmissions.find(identity);
	if (it == gPendingPreSkinSubmissions.end())
		return;
	MovePendingPreSkinSubmission(&it->second, target);
	gPendingPreSkinSubmissions.erase(it);
}

static void MovePendingPreSkinSubmissionToReleaseBatch(
	DX12PreSkinPendingSubmission *source,
	DX12PreSkinReleaseBatch *batch)
{
	if (!source || !batch)
		return;
	DX12DescriptorArenaUtils::MoveUniqueOwned(
		&source->resources, &batch->resources);
	DX12DescriptorArenaUtils::MoveUniqueOwned(
		&source->descriptorHeaps, &batch->descriptorHeaps);
}

static void CleanupRetiredPreSkinObjectsLocked(
	DX12PreSkinReleaseBatch *releaseBatch)
{
	if (!releaseBatch)
		return;
	const LONG present = DX12RuntimeState::GetPresentCount();
	for (auto it = gRetiredPreSkinResources.begin();
		 it != gRetiredPreSkinResources.end();) {
		const bool fenceComplete = it->completionFence && it->completionValue &&
			it->completionFence->GetCompletedValue() >= it->completionValue;
		const bool fallbackExpired = !it->completionFence &&
			present - it->retirePresent >= PreSkinRetiredObjectPresentDelay;
		if (!fenceComplete && !fallbackExpired) {
			++it;
			continue;
		}
		if (it->resource)
			releaseBatch->resources.push_back(it->resource);
		if (it->completionFence)
			releaseBatch->fences.push_back(it->completionFence);
		it = gRetiredPreSkinResources.erase(it);
	}

	for (auto it = gRetiredPreSkinDescriptorHeaps.begin();
		 it != gRetiredPreSkinDescriptorHeaps.end();) {
		const bool fenceComplete = it->completionFence && it->completionValue &&
			it->completionFence->GetCompletedValue() >= it->completionValue;
		const bool fallbackExpired = !it->completionFence &&
			present - it->retirePresent >= PreSkinRetiredObjectPresentDelay;
		if (!fenceComplete && !fallbackExpired) {
			++it;
			continue;
		}
		if (it->heap)
			releaseBatch->descriptorHeaps.push_back(it->heap);
		if (it->completionFence)
			releaseBatch->fences.push_back(it->completionFence);
		it = gRetiredPreSkinDescriptorHeaps.erase(it);
	}
}

static IUnknown *PreSkinCommandListIdentity(IUnknown *commandList)
{
	if (!commandList)
		return nullptr;
	ID3D12GraphicsCommandList *baseList =
		reinterpret_cast<ID3D12GraphicsCommandList*>(commandList);
	IUnknown *identity = DX12CommandListRuntime::GetCommandListIdentity(baseList);
	return identity ? identity : commandList;
}

bool DX12ModResolvePreSkinOutputResizeCacheByGpuVa(
	ID3D12GraphicsCommandList *commandList,
	UINT64 viewGpuVa, DX12PreSkinOutputResizeBinding *binding)
{
	if (!viewGpuVa || !binding)
		return false;
	binding->Clear();

	// The publish map is intentionally frame-local, but the resize cache owns a
	// stable replacement for the original output resource until fence-safe
	// eviction or configuration reload. Resolve through that owner when Present,
	// FG, or capture falls between producer and consumer command-list recording.
	// This avoids both a frame-order dependency and a stale persistent GPU-VA map.
	IUnknown *commandListIdentity = PreSkinCommandListIdentity(commandList);
	AcquireSRWLockExclusive(&gPreSkinOutputResizeCacheLock);
	DX12PreSkinOutputResizeCacheEntry *bestEntry = nullptr;
	const DX12PreSkinOutputResizeCacheIdentity *bestKey = nullptr;
	UINT64 bestOffset = 0;
	// The same original allocation can have variants for different required
	// widths or strides. useSerial identifies the most recently observed producer
	// and keeps the fallback consistent with the publish map's last writer.
	for (auto &item : gPreSkinOutputResizeCache) {
		DX12PreSkinOutputResizeCacheEntry &candidate = item.second;
		if (!candidate.originalResource || !candidate.vertexResource ||
			!candidate.originalGpuVa || !candidate.originalByteWidth ||
			!candidate.vertexGpuVa)
			continue;
		if (viewGpuVa < candidate.originalGpuVa)
			continue;
		const UINT64 sourceOffset = viewGpuVa - candidate.originalGpuVa;
		if (sourceOffset >= candidate.originalByteWidth ||
		    sourceOffset >= item.first.requiredByteWidth)
			continue;
		if (!bestEntry || candidate.useSerial > bestEntry->useSerial) {
			bestEntry = &candidate;
			bestKey = &item.first;
			bestOffset = sourceOffset;
		}
	}

	if (!bestEntry || !bestKey) {
		ReleaseSRWLockExclusive(&gPreSkinOutputResizeCacheLock);
		return false;
	}

	// Bind vertexResource, not resizedResource. PreSkin writes the UAV resource,
	// then records the required barriers and a copy into this COMMON-state
	// consumer resource; IA must not read the UAV allocation directly.
	bestEntry->vertexResource->AddRef();
	binding->resizedResource = bestEntry->vertexResource;
	binding->retainedResource = true;
	binding->originalGpuVa = bestEntry->originalGpuVa;
	binding->originalByteWidth = bestEntry->originalByteWidth;
	binding->resizedGpuVa = bestEntry->vertexGpuVa;
	binding->resizedByteWidth = bestKey->requiredByteWidth;
	binding->sourceOffset = bestOffset;
	binding->stride = bestKey->stride;
	binding->uavBarrierPending = false;
	binding->transitionToVertex = false;
	binding->stateBefore = D3D12_RESOURCE_STATE_COMMON;
	bestEntry->useSerial = ++gPreSkinOutputResizeCacheUseSerial;
	TrackPreSkinOutputResizeCacheEntryUse(bestEntry, commandListIdentity);
	ReleaseSRWLockExclusive(&gPreSkinOutputResizeCacheLock);
	return true;
}

class DX12PreSkinOutputResizeQueueFence
{
public:
	ID3D12Fence *fence = nullptr;
	UINT64 nextValue = 0;
};

class DX12PreSkinOutputResizeSubmittedUse
{
public:
	DX12PreSkinOutputResizeCacheIdentity key;
};

static SRWLOCK gPreSkinOutputResizeQueueFenceLock = SRWLOCK_INIT;
static std::unordered_map<ID3D12CommandQueue*, DX12PreSkinOutputResizeQueueFence>
	gPreSkinOutputResizeQueueFences;

static bool AcquirePreSkinOutputResizeSubmissionFence(
	ID3D12CommandQueue *queue, ID3D12Fence **fenceOut, UINT64 *valueOut)
{
	if (!queue || !fenceOut || !valueOut)
		return false;
	*fenceOut = nullptr;
	*valueOut = 0;

	AcquireSRWLockExclusive(&gPreSkinOutputResizeQueueFenceLock);
	DX12PreSkinOutputResizeQueueFence &state =
		gPreSkinOutputResizeQueueFences[queue];
	if (!state.fence) {
		ID3D12Device *device = nullptr;
		if (FAILED(queue->GetDevice(IID_PPV_ARGS(&device))) || !device) {
			gPreSkinOutputResizeQueueFences.erase(queue);
			ReleaseSRWLockExclusive(&gPreSkinOutputResizeQueueFenceLock);
			return false;
		}
		const HRESULT hr = device->CreateFence(
			0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&state.fence));
		device->Release();
		if (FAILED(hr) || !state.fence) {
			gPreSkinOutputResizeQueueFences.erase(queue);
			ReleaseSRWLockExclusive(&gPreSkinOutputResizeQueueFenceLock);
			return false;
		}
	}
	if (state.nextValue == UINT64_MAX)
		state.nextValue = 0;
	*valueOut = ++state.nextValue;
	state.fence->AddRef();
	*fenceOut = state.fence;
	ReleaseSRWLockExclusive(&gPreSkinOutputResizeQueueFenceLock);
	return true;
}

// Returns one caller-owned completion fence when requested. Retirement can
// share this signal with the resize cache, keeping submission bookkeeping to
// one queue Signal per ExecuteCommandLists.
static bool MarkPreSkinOutputResizeCacheUsesSubmitted(
	ID3D12CommandQueue *queue,
	UINT numCommandLists,
	ID3D12CommandList *const *commandLists,
	ID3D12Fence **completionFenceOut = nullptr,
	UINT64 *completionValueOut = nullptr)
{
	if (completionFenceOut)
		*completionFenceOut = nullptr;
	if (completionValueOut)
		*completionValueOut = 0;
	if (!queue || !numCommandLists || !commandLists)
		return false;
	// Submission notifications are on the queue hot path.  Most submissions
	// have no resize-cache users; avoid allocating the identity/use vectors and
	// taking the cache lock in that overwhelmingly common case.  The flag is
	// published whenever a command list is associated with a cache entry and
	// cleared after the last pending use is consumed or abandoned.
	if (InterlockedCompareExchange(
		&gHasPendingPreSkinOutputResizeSubmissionWorkFast, 0, 0) == 0)
		return false;
	// ExecuteCommandLists batches are normally small.  Keep the common path
	// allocation-free while retaining support for arbitrarily large batches.
	static constexpr UINT kStackIdentityCount = 8;
	IUnknown *stackIdentities[kStackIdentityCount] = {};
	std::vector<IUnknown*> heapIdentities;
	IUnknown **identityStorage = stackIdentities;
	if (numCommandLists > kStackIdentityCount) {
		heapIdentities.resize(numCommandLists);
		identityStorage = heapIdentities.data();
	}
	for (UINT i = 0; i < numCommandLists; ++i) {
		IUnknown *identity = PreSkinCommandListIdentity(commandLists[i]);
		if (identity)
			identityStorage[i] = identity;
	}
	const UINT identityCount = [&]() {
		UINT count = 0;
		for (UINT i = 0; i < numCommandLists; ++i)
			if (identityStorage[i])
				++count;
		return count;
	}();
	if (identityCount == 0)
		return false;

	std::vector<DX12PreSkinOutputResizeSubmittedUse> submittedUses;
	AcquireSRWLockShared(&gPreSkinOutputResizeCacheLock);
	for (const auto &item : gPreSkinOutputResizeCache) {
		const DX12PreSkinOutputResizeCacheEntry &entry = item.second;
		bool hasSubmittedUse = false;
		for (IUnknown *pending : entry.pendingCommandLists) {
			if (std::find(identityStorage, identityStorage + numCommandLists,
					pending) != identityStorage + numCommandLists) {
				hasSubmittedUse = true;
				break;
			}
		}
		if (!hasSubmittedUse)
			continue;
		submittedUses.push_back({ item.first });
	}
	ReleaseSRWLockShared(&gPreSkinOutputResizeCacheLock);
	if (submittedUses.empty())
		return false;

	ID3D12Fence *fence = nullptr;
	UINT64 fenceValue = 0;
	if (!AcquirePreSkinOutputResizeSubmissionFence(
			queue, &fence, &fenceValue))
		return false;
	HRESULT signalHr = E_FAIL;
	{
		// The signal must follow the application's ExecuteCommandLists call but
		// bypass our normal queue instrumentation to avoid recursive bookkeeping.
		DX12InternalReplayScope internalReplay;
		signalHr = queue->Signal(fence, fenceValue);
	}
	if (FAILED(signalHr)) {
		fence->Release();
		return false;
	}

	AcquireSRWLockExclusive(&gPreSkinOutputResizeCacheLock);
	for (const DX12PreSkinOutputResizeSubmittedUse &submitted : submittedUses) {
		auto found = gPreSkinOutputResizeCache.find(submitted.key);
		if (found == gPreSkinOutputResizeCache.end())
			continue;
		DX12PreSkinOutputResizeCacheEntry &entry = found->second;
		const size_t oldPendingCount = entry.pendingCommandLists.size();
		entry.pendingCommandLists.erase(
			std::remove_if(
				entry.pendingCommandLists.begin(),
				entry.pendingCommandLists.end(),
				[identityStorage, numCommandLists](IUnknown *pending) {
					return std::find(
						identityStorage,
						identityStorage + numCommandLists,
						pending) != identityStorage + numCommandLists;
				}),
			entry.pendingCommandLists.end());
		if (entry.pendingCommandLists.size() == oldPendingCount)
			continue;
		auto existing = std::find_if(
			entry.completionPoints.begin(), entry.completionPoints.end(),
			[fence](const DX12PreSkinOutputResizeCacheEntry::CompletionPoint &point) {
				return point.fence == fence;
			});
		if (existing != entry.completionPoints.end()) {
			if (fenceValue > existing->value)
				existing->value = fenceValue;
		} else {
			fence->AddRef();
			entry.completionPoints.push_back({ fence, fenceValue });
		}
	}
	bool hasPendingUses = false;
	for (const auto &item : gPreSkinOutputResizeCache) {
		if (!item.second.pendingCommandLists.empty()) {
			hasPendingUses = true;
			break;
		}
	}
	InterlockedExchange(
		&gHasPendingPreSkinOutputResizeSubmissionWorkFast,
		hasPendingUses ? 1 : 0);
	ReleaseSRWLockExclusive(&gPreSkinOutputResizeCacheLock);
	if (completionFenceOut) {
		*completionFenceOut = fence;
		if (completionValueOut)
			*completionValueOut = fenceValue;
	} else {
		fence->Release();
	}
	return true;
}

void DX12ModAbandonPreSkinOutputResizeCacheUses(
	ID3D12GraphicsCommandList *commandList)
{
	IUnknown *identity = PreSkinCommandListIdentity(commandList);
	if (!identity)
		return;
	AcquireSRWLockExclusive(&gPreSkinOutputResizeCacheLock);
	for (auto &item : gPreSkinOutputResizeCache) {
		DX12PreSkinOutputResizeCacheEntry &entry = item.second;
		entry.pendingCommandLists.erase(
			std::remove(entry.pendingCommandLists.begin(),
				entry.pendingCommandLists.end(), identity),
			entry.pendingCommandLists.end());
	}
	bool hasPendingUses = false;
	for (const auto &item : gPreSkinOutputResizeCache) {
		if (!item.second.pendingCommandLists.empty()) {
			hasPendingUses = true;
			break;
		}
	}
	InterlockedExchange(
		&gHasPendingPreSkinOutputResizeSubmissionWorkFast,
		hasPendingUses ? 1 : 0);
	ReleaseSRWLockExclusive(&gPreSkinOutputResizeCacheLock);
}

void DX12ModTrackPreSkinOutputResizeCacheUse(
	ID3D12GraphicsCommandList *commandList,
	ID3D12Resource *vertexResource)
{
	IUnknown *identity = PreSkinCommandListIdentity(commandList);
	if (!identity || !vertexResource)
		return;
	AcquireSRWLockExclusive(&gPreSkinOutputResizeCacheLock);
	for (auto &item : gPreSkinOutputResizeCache) {
		DX12PreSkinOutputResizeCacheEntry &entry = item.second;
		if (entry.vertexResource != vertexResource)
			continue;
		if (std::find(entry.pendingCommandLists.begin(),
				entry.pendingCommandLists.end(), identity) ==
			entry.pendingCommandLists.end()) {
			entry.pendingCommandLists.push_back(identity);
			InterlockedExchange(
				&gHasPendingPreSkinOutputResizeSubmissionWorkFast, 1);
		}
		break;
	}
	ReleaseSRWLockExclusive(&gPreSkinOutputResizeCacheLock);
}


static void RetirePreSkinResource(ID3D12Resource *resource)
{
	if (!resource)
		return;
	AcquireSRWLockExclusive(&gPreSkinRetireLock);
	DX12DescriptorArenaUtils::AppendUniqueOwned(
		&gPendingPreSkinSubmissions[nullptr].resources, resource);
	PublishPendingPreSkinSubmissionWorkLocked();
	ReleaseSRWLockExclusive(&gPreSkinRetireLock);
}

static void RetirePreSkinResourceForCommandList(
	ID3D12GraphicsCommandList *commandList, ID3D12Resource *resource)
{
	if (!resource)
		return;
	IUnknown *identity = PreSkinCommandListIdentity(commandList);
	AcquireSRWLockExclusive(&gPreSkinRetireLock);
	DX12DescriptorArenaUtils::AppendUniqueOwned(
		&gPendingPreSkinSubmissions[identity].resources, resource);
	PublishPendingPreSkinSubmissionWorkLocked();
	ReleaseSRWLockExclusive(&gPreSkinRetireLock);
}


static void RetirePreSkinDescriptorHeap(ID3D12DescriptorHeap *heap)
{
	if (!heap)
		return;
	AcquireSRWLockExclusive(&gPreSkinRetireLock);
	DX12DescriptorArenaUtils::AppendUniqueOwned(
		&gPendingPreSkinSubmissions[nullptr].descriptorHeaps, heap);
	PublishPendingPreSkinSubmissionWorkLocked();
	ReleaseSRWLockExclusive(&gPreSkinRetireLock);
}

static void RetainPreSkinDescriptorHeapForCommandList(
	ID3D12GraphicsCommandList *commandList, ID3D12DescriptorHeap *heap)
{
	if (!heap)
		return;
	IUnknown *identity = PreSkinCommandListIdentity(commandList);
	AcquireSRWLockExclusive(&gPreSkinRetireLock);
	std::vector<ID3D12DescriptorHeap*> &heaps =
		gPendingPreSkinSubmissions[identity].descriptorHeaps;
	if (std::find(heaps.begin(), heaps.end(), heap) == heaps.end()) {
		heap->AddRef();
		heaps.push_back(heap);
	}
	PublishPendingPreSkinSubmissionWorkLocked();
	ReleaseSRWLockExclusive(&gPreSkinRetireLock);
}

void DX12ModNotifyCommandListsSubmitted(
	ID3D12CommandQueue *queue, UINT numCommandLists, ID3D12CommandList *const *commandLists)
{
	if (!queue || !commandLists || !numCommandLists)
		return;
	ID3D12Fence *fence = nullptr;
	UINT64 fenceValue = 0;
	MarkPreSkinOutputResizeCacheUsesSubmitted(
		queue, numCommandLists, commandLists, &fence, &fenceValue);

	DX12PreSkinReleaseBatch releaseBatch;
	DX12PreSkinPendingSubmission pending;
	AcquireSRWLockExclusive(&gPreSkinRetireLock);
	CleanupRetiredPreSkinObjectsLocked(&releaseBatch);

	auto globalIt = gPendingPreSkinSubmissions.find(nullptr);
	if (globalIt != gPendingPreSkinSubmissions.end()) {
		MovePendingPreSkinSubmission(&globalIt->second, &pending);
		gPendingPreSkinSubmissions.erase(globalIt);
	}

	if (!gPendingPreSkinSubmissions.empty()) {
		for (UINT i = 0; i < numCommandLists; ++i) {
			MovePendingPreSkinSubmissionForCommandListLocked(
				PreSkinCommandListIdentity(commandLists[i]),
				&pending);
		}
	}

	if (pending.resources.empty() && pending.descriptorHeaps.empty()) {
		PublishPendingPreSkinSubmissionWorkLocked();
		ReleaseSRWLockExclusive(&gPreSkinRetireLock);
		ReleasePreSkinReleaseBatch(&releaseBatch);
		return;
	}

	// The original ExecuteCommandLists has returned. Signal a queue-local fence
	// now, so every object retired by these command lists is released exactly
	// after the GPU has consumed this submission. Do not hold gPreSkinRetireLock
	// across Signal: drivers may synchronously enter queue instrumentation.
	ReleaseSRWLockExclusive(&gPreSkinRetireLock);
	HRESULT signalHr = E_FAIL;
	if (fence) {
		signalHr = S_OK;
	} else if (AcquirePreSkinOutputResizeSubmissionFence(queue, &fence, &fenceValue)) {
		DX12InternalReplayScope internalReplay;
		signalHr = queue->Signal(fence, fenceValue);
	}

	const LONG retirePresent = DX12RuntimeState::GetPresentCount();
	const bool fenceTracked = SUCCEEDED(signalHr) && fence && fenceValue;
	AcquireSRWLockExclusive(&gPreSkinRetireLock);
	for (ID3D12Resource *resource : pending.resources) {
		if (!resource)
			continue;
		if (fenceTracked)
			fence->AddRef();
		gRetiredPreSkinResources.push_back(
			{ resource, fenceTracked ? fence : nullptr,
				fenceTracked ? fenceValue : 0, retirePresent });
	}
	for (ID3D12DescriptorHeap *heap : pending.descriptorHeaps) {
		if (!heap)
			continue;
		if (fenceTracked)
			fence->AddRef();
		gRetiredPreSkinDescriptorHeaps.push_back(
			{ heap, fenceTracked ? fence : nullptr,
				fenceTracked ? fenceValue : 0, retirePresent });
	}
	if (!pending.resources.empty() || !pending.descriptorHeaps.empty())
		InterlockedExchange(&gHasRetiredPreSkinObjectsFast, 1);
	PublishPendingPreSkinSubmissionWorkLocked();
	ReleaseSRWLockExclusive(&gPreSkinRetireLock);
	if (fence)
		fence->Release();
	ReleasePreSkinReleaseBatch(&releaseBatch);
}

bool DX12ModNeedsCommandListSubmissionNotifications()
{
	return InterlockedCompareExchange(
		&gHasPendingPreSkinSubmissionWorkFast,
		0,
		0) != 0 ||
		InterlockedCompareExchange(
			&gHasPendingPreSkinOutputResizeSubmissionWorkFast,
			0,
			0) != 0;
}

void DX12ModRunPresentMaintenance()
{
	if (InterlockedCompareExchange(&gHasRetiredPreSkinObjectsFast, 0, 0) == 0)
		return;
	DX12PreSkinReleaseBatch releaseBatch;
	AcquireSRWLockExclusive(&gPreSkinRetireLock);
	CleanupRetiredPreSkinObjectsLocked(&releaseBatch);
	if (gRetiredPreSkinResources.empty() &&
		gRetiredPreSkinDescriptorHeaps.empty())
		InterlockedExchange(&gHasRetiredPreSkinObjectsFast, 0);
	ReleaseSRWLockExclusive(&gPreSkinRetireLock);
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
		if (retired.completionFence)
			releaseBatch->fences.push_back(retired.completionFence);
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
			*cpuBase = cpu;
			*gpuBase = gpu;
			*increment = gPreSkinDescriptorRing.increment;
			*heapOut = usedHeap;
			ReleaseSRWLockExclusive(&gPreSkinDescriptorRingLock);
			RetainPreSkinDescriptorHeapForCommandList(commandList, usedHeap);
			return true;
		}
		capacityToCreate = DX12DescriptorArenaUtils::NextCapacity(
			gPreSkinDescriptorRing.capacity,
			gPreSkinDescriptorRing.offset,
			requiredDescriptors,
			PreSkinDescriptorRingInitialCapacity,
			PreSkinDescriptorRingMaximumCapacity);
		ReleaseSRWLockExclusive(&gPreSkinDescriptorRingLock);
		if (!capacityToCreate)
			return false;

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
		RetainPreSkinDescriptorHeapForCommandList(commandList, usedHeap);
		return true;
	}
}

enum class DX12PsoKind
{
	Graphics,
	Compute
};

static void ProcessDx12KeyBindingsForFrame();

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

// Plan step 3.1: decide which TextureOverride sections are unconditional
// static texture replacements. A section qualifies when it only replaces the
// texture content (drawReplaceThisResource set), never observes the draw call
// (no draw-context match filters, no match_cs / pre-skin dependency), and runs
// no command lists or actions that could depend on the original texture.
static bool TextureOverrideIsStaticReplacement(
	const Bunny::TextureOverrideConfig &config)
{
	if (config.drawReplaceThisResource.empty())
		return false;
	if (config.hasMatchVertexCount || config.hasMatchIndexCount ||
	    config.hasMatchInstanceCount || config.hasMatchFirstVertex ||
	    config.hasMatchFirstIndex || config.hasMatchFirstInstance ||
	    config.hasMatchCs || config.hasMatchUavBytes || config.hasCsHash ||
	    !config.preSkinMatchCsSrvHashes.empty() ||
	    !config.preSkinCsSrvResources.empty())
		return false;
	if (config.hasVertexLimitRaise)
		return false;
	if (!config.commandLists.pre.empty() ||
	    !config.commandLists.main.empty() ||
	    !config.commandLists.post.empty() ||
	    !config.actions.empty())
		return false;
	// Sections that also replace IB/VB or draw extra calls are not pure texture
	// swaps; keep those on the draw-time path.
	if (!config.indexBufferResource.empty() ||
	    !config.vertexBufferResources.empty() ||
	    !config.drawPsSrvResources.empty() ||
	    !config.drawVsSrvResources.empty())
		return false;
	return true;
}

static std::shared_ptr<DX12StaticTextureReplacementTable>
BuildDx12StaticTextureReplacementTable(
	const Bunny::TextureOverrideMap &textureOverrides,
	const std::unordered_map<std::wstring, uint32_t> &textureOverrideSectionIds)
{
	std::shared_ptr<DX12StaticTextureReplacementTable> table =
		std::make_shared<DX12StaticTextureReplacementTable>();
	bool allStatic = true;
	for (const auto &bucket : textureOverrides) {
		for (const Bunny::TextureOverrideConfig &config : bucket.second) {
			// Descriptor consumers key on the section hash; only hashed
			// sections can ever match a descriptor slot.
			if (!config.hash)
				continue;
			if (TextureOverrideIsStaticReplacement(config)) {
				DX12StaticTextureReplacementEntry entry;
				entry.hash = config.hash;
				entry.resourceName = config.drawReplaceThisResource;
				auto sectionId =
					textureOverrideSectionIds.find(config.section);
				entry.sectionId =
					sectionId != textureOverrideSectionIds.end() ?
						sectionId->second : 0;
				table->byHash[config.hash] = std::move(entry);
				continue;
			}
			if (table->byHash.find(config.hash) != table->byHash.end()) {
				// A dynamic override exists for a hash we already classified as
				// static: the hash is ambiguous, fall back to draw-time matching.
				table->byHash.erase(config.hash);
			}
			allStatic = false;
		}
	}
	table->enabled = !table->byHash.empty();
	// The draw path may skip descriptor candidate matching only when every
	// descriptor-relevant hashed texture override is static. A shader
	// override that carries no descriptor targets never reads descriptors,
	// so it does not block this; one that does read descriptor hashes goes
	// through target plans that still carry hasDescriptorTargets, which the
	// draw short-circuit checks before consulting this flag.
	table->allDescriptorOverridesStatic = table->enabled && allStatic;
	return table;
}

// Plan step 3.2: decide which TextureOverride sections are unconditional
// static geometry (IB/VB) replacements. A section qualifies when it only
// swaps geometry (indexBufferResource / vertexBufferResources set), never
// observes the draw call, never skips the original draw, and runs no command
// lists or actions. A hash shared by any non-static override falls back to
// the draw-time path for every section using it. The whole table is
// disabled when any shader override reads IA checktextureoverride results:
// those control flows must keep matching against the original bindings.
static std::shared_ptr<DX12StaticIaReplacementTable>
BuildDx12StaticIaReplacementTable(
	const Bunny::TextureOverrideMap &textureOverrides,
	bool hasShaderIaTextureOverrideTriggers)
{
	std::shared_ptr<DX12StaticIaReplacementTable> table =
		std::make_shared<DX12StaticIaReplacementTable>();
	if (hasShaderIaTextureOverrideTriggers)
		return table;
	for (const auto &bucket : textureOverrides) {
		for (const Bunny::TextureOverrideConfig &config : bucket.second) {
			if (!config.hash)
				continue;
			const bool hasIaSwap =
				!config.indexBufferResource.empty() ||
				!config.vertexBufferResources.empty();
			const bool clean =
				!config.handlingSkip &&
				!config.hasMatchVertexCount && !config.hasMatchIndexCount &&
				!config.hasMatchInstanceCount && !config.hasMatchFirstVertex &&
				!config.hasMatchFirstIndex && !config.hasMatchFirstInstance &&
				!config.hasMatchCs && !config.hasMatchUavBytes &&
				!config.hasCsHash &&
				config.preSkinMatchCsSrvHashes.empty() &&
				config.preSkinCsSrvResources.empty() &&
				!config.hasVertexLimitRaise &&
				config.commandLists.pre.empty() &&
				config.commandLists.main.empty() &&
				config.commandLists.post.empty() &&
				config.actions.empty() &&
				config.drawPsSrvResources.empty() &&
				config.drawVsSrvResources.empty() &&
				config.drawReplaceThisResource.empty();
			if (!hasIaSwap || !clean) {
				// Any non-static override on this hash makes the hash ambiguous:
				// keep every section using it on the draw-time path.
				table->indexByHash.erase(config.hash);
				for (UINT slot = 0; slot < 32; ++slot) {
					const uint64_t key =
						(static_cast<uint64_t>(slot) << 32) |
						static_cast<uint64_t>(config.hash);
					table->vertexByHashAndSlot.erase(key);
				}
				continue;
			}
			if (!config.indexBufferResource.empty()) {
				auto inserted = table->indexByHash.emplace(
					config.hash, config.indexBufferResource);
				if (!inserted.second &&
				    inserted.first->second != config.indexBufferResource) {
					// Conflicting replacements for the same hash: ambiguous.
					table->indexByHash.erase(inserted.first);
				}
			}
			for (const auto &vb : config.vertexBufferResources) {
				if (vb.first >= 32)
					continue;
				const uint64_t key =
					(static_cast<uint64_t>(vb.first) << 32) |
					static_cast<uint64_t>(config.hash);
				auto inserted = table->vertexByHashAndSlot.emplace(key, vb.second);
				if (!inserted.second && inserted.first->second != vb.second)
					table->vertexByHashAndSlot.erase(inserted.first);
			}
		}
	}
	table->enabled =
		!table->indexByHash.empty() || !table->vertexByHashAndSlot.empty();
	return table;
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
	size_t candidateCount = 0;
	for (const auto &bucket : textureOverrides) {
		for (const Bunny::TextureOverrideConfig &config : bucket.second) {
			if (IsComputePreSkinTextureOverride(config))
				++candidateCount;
		}
	}
	preSkinTextureOverrides->reserve(candidateCount);
	matchCsTextureOverrideIndex->reserve(candidateCount);
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
	// Confirmed match_cs sections outlive Present boundaries because DX12 command
	// list recording is not frame ordered. A configuration reload is the point at
	// which that proof becomes stale and must be discarded.
	if (!gConfirmedPreSkinSections.empty()) {
		gConfirmedPreSkinSections.clear();
		InterlockedExchange(&gHasConfirmedPreSkinSectionsFast, 0);
		BumpPreSkinActiveGeneration();
	}
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
		if (retired.completionFence)
			releaseBatch.fences.push_back(retired.completionFence);
	}
	gRetiredPreSkinResources.clear();
	for (auto &retired : gRetiredPreSkinDescriptorHeaps) {
		if (retired.heap)
			releaseBatch.descriptorHeaps.push_back(retired.heap);
		if (retired.completionFence)
			releaseBatch.fences.push_back(retired.completionFence);
	}
	gRetiredPreSkinDescriptorHeaps.clear();
	InterlockedExchange(&gHasRetiredPreSkinObjectsFast, 0);
	InterlockedExchange(&gHasPendingPreSkinSubmissionWorkFast, 0);
	ReleaseSRWLockExclusive(&gPreSkinRetireLock);

	AcquireSRWLockExclusive(&gPreSkinDescriptorRingLock);
	ReleasePreSkinDescriptorRingLocked(&releaseBatch);
	ReleaseSRWLockExclusive(&gPreSkinDescriptorRingLock);
	DrainPreSkinDescriptorRestoreHeapPool(&releaseBatch);
	// Configuration reload is the ownership boundary for the durable resize
	// cache. Do not move this clear into DX12ModBeginFrame: consumer command lists
	// can legitimately be recorded after Present while referring to the previous
	// producer's original GPU VA.
	ClearPreSkinOutputResizeCache();
	ReleasePreSkinReleaseBatch(&releaseBatch);
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

void DX12ModScheduleInitialLoad(const wchar_t *configPath)
{
	AcquireSRWLockExclusive(&gModLock);
	SetBasePaths(configPath);
	gPendingInitialConfigPath = configPath ? configPath : L"";
	gDeferredInitialModLoadStartTick = GetTickCount();
	InterlockedExchange(&gDeferredInitialModLoadState,
		gPendingInitialConfigPath.empty() ? 0 : 1);
	ReleaseSRWLockExclusive(&gModLock);
}

void DX12ModMaybeRunDeferredInitialLoad()
{
	// Diagnostic probe: log the state machine inputs on every call so a
	// "mod never loads" report can be traced without guessing.
	{
		DWORD probeElapsed = 0;
		LONG probeState = InterlockedCompareExchange(&gDeferredInitialModLoadState, 0, 0);
		DX12LogManager::Fields("DX12ModDeferredProbe",
			"\"state\":%ld,\"present\":%ld,\"elapsedMs\":%lu,\"delayMs\":%lu",
			probeState,
			DX12RuntimeState::GetPresentCount(),
			probeElapsed = GetTickCount() - gDeferredInitialModLoadStartTick,
			gDeferredInitialModLoadDelayMs);
	}
	if (InterlockedCompareExchange(&gDeferredInitialModLoadState, 2, 1) != 1)
		return;

	std::wstring configPath;
	DWORD delayMs = 0;
	DWORD startTick = 0;
	AcquireSRWLockShared(&gModLock);
	configPath = gPendingInitialConfigPath;
	delayMs = gDeferredInitialModLoadDelayMs;
	startTick = gDeferredInitialModLoadStartTick;
	ReleaseSRWLockShared(&gModLock);

	if (configPath.empty()) {
		InterlockedExchange(&gDeferredInitialModLoadState, 0);
		return;
	}

	const LONG present = DX12RuntimeState::GetPresentCount();
	const DWORD elapsedMs = GetTickCount() - startTick;
	if (present <= 2 || elapsedMs < delayMs) {
		InterlockedExchange(&gDeferredInitialModLoadState, 1);
		return;
	}

	DX12LogManager::Fields("DX12ModRuntimeDeferredLoad",
		"\"status\":\"begin\",\"path\":\"%S\",\"present\":%ld,\"elapsedMs\":%lu,\"delayMs\":%lu",
		configPath.c_str(), present, elapsedMs, delayMs);

	DX12OverlayStatusState::SetStatus(L"DX12 startup reload running");
	const bool ok = DX12ModRuntimeReload();
	if (ok)
		DX12ModRefreshRuntimeStateAfterReload();

	AcquireSRWLockExclusive(&gModLock);
	if (ok)
		gPendingInitialConfigPath.clear();
	else
		gPendingInitialConfigPath = configPath;
	ReleaseSRWLockExclusive(&gModLock);

	DX12LogManager::Fields("DX12ModRuntimeDeferredLoad",
		"\"status\":\"done\",\"result\":\"%s\",\"present\":%ld,\"elapsedMs\":%lu",
		ok ? "ok" : "failed",
		DX12RuntimeState::GetPresentCount(),
		GetTickCount() - startTick);
	InterlockedExchange(&gDeferredInitialModLoadState, ok ? 0 : 1);
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

	desc->Width = match.overrideByteWidth;
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

class DX12ExpandedPreSkinBufferPrivateData
{
public:
	UINT version = 1;
	UINT vertexStride = 0;
	UINT uavStride = 0;
	UINT reserved = 0;
	UINT64 outputByteWidth = 0;
};

static const GUID DX12ExpandedPreSkinBufferPrivateDataGuid =
	{0x2ce8d27c, 0xdd3f, 0x4d84, {0xa8, 0x9d, 0x7f, 0xb7, 0xf4, 0x85, 0xe0, 0x9f}};

bool DX12ModResolveExpandedPreSkinBufferWidth(
	ID3D12Resource *resource, UINT vertexStride, UINT64 *outputByteWidth)
{
	if (outputByteWidth)
		*outputByteWidth = 0;
	if (!resource || !outputByteWidth)
		return false;

	DX12ExpandedPreSkinBufferPrivateData info;
	UINT infoSize = sizeof(info);
	if (FAILED(resource->GetPrivateData(
			DX12ExpandedPreSkinBufferPrivateDataGuid, &infoSize, &info)) ||
	    infoSize != sizeof(info) || info.version != 1 ||
	    !info.outputByteWidth ||
	    (vertexStride && info.vertexStride && vertexStride != info.vertexStride))
		return false;

	const D3D12_RESOURCE_DESC resourceDesc = resource->GetDesc();
	if (resourceDesc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER ||
	    info.outputByteWidth > resourceDesc.Width)
		return false;

	// Private data follows the resource through frame buffering, but the INI can
	// be reloaded while that resource remains alive. Require a current VLR with
	// the same shape so stale tags cannot reactivate a removed configuration.
	bool configured = false;
	AcquireSRWLockShared(&gModLock);
	for (const DX12VertexLimitRaiseConfig &vlr : gVertexLimitRaiseConfigs) {
		if (vlr.overrideByteWidth == info.outputByteWidth &&
		    (!info.vertexStride || vlr.overrideByteStride == info.vertexStride)) {
			configured = true;
			break;
		}
	}
	ReleaseSRWLockShared(&gModLock);
	if (!configured)
		return false;

	*outputByteWidth = info.outputByteWidth;
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

	// DX11 stores the VLR size on the resized buffer object. DX12 additionally
	// freezes the readable range in every VBV, so keep the matching VLR shape on
	// the resource itself. A section-global "latest producer" is insufficient:
	// frame generation and capture increase the number of in-flight frame
	// resources, and a later PreSkin dispatch can replace that entry before an
	// earlier command list records its IA binding.
	DX12ExpandedPreSkinBufferPrivateData info;
	info.vertexStride = match.overrideByteStride;
	info.uavStride = match.uavByteStride;
	info.outputByteWidth = match.overrideByteWidth;
	resource->SetPrivateData(
		DX12ExpandedPreSkinBufferPrivateDataGuid, sizeof(info), &info);

	desc->Buffer.NumElements = static_cast<UINT>(match.overrideNumElements);
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
	std::unordered_map<UINT64, std::vector<size_t>> *mergedVertexTextureOverrideIndex,
	std::vector<size_t> *indexTextureOverridePrefilterIndex,
	std::unordered_map<uint32_t, std::vector<size_t>> *indexTextureOverridePrefilterByIndexCount,
	std::unordered_map<uint32_t, std::vector<size_t>> *vertexTextureOverridePrefilterIndex,
	std::vector<size_t> *anyVertexTextureOverridePrefilterIndex,
	std::unordered_map<uint32_t, std::vector<size_t>> *anyVertexTextureOverridePrefilterByVertexCount,
	std::unordered_map<uint32_t, std::vector<size_t>> *mergedVertexTextureOverridePrefilterIndex,
	std::unordered_map<UINT64, std::vector<size_t>> *vertexTextureOverridePrefilterByVertexCount,
	std::unordered_map<UINT64, std::vector<size_t>> *mergedVertexTextureOverridePrefilterByVertexCount);
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
static std::shared_ptr<DX12StaticTextureReplacementTable>
BuildDx12StaticTextureReplacementTable(
	const Bunny::TextureOverrideMap &textureOverrides,
	const std::unordered_map<std::wstring, uint32_t> &textureOverrideSectionIds);
// Plan step 3.2: write-time (bind-time) static IA replacement table.
static std::shared_ptr<DX12StaticIaReplacementTable>
BuildDx12StaticIaReplacementTable(
	const Bunny::TextureOverrideMap &textureOverrides,
	bool hasShaderIaTextureOverrideTriggers);
static void BuildDx12VlrResourceCandidates(
	const Bunny::ResourceMap &resources,
	const std::unordered_map<std::wstring, std::vector<std::wstring>> &explicitMatchCsResourceSections,
	std::vector<std::wstring> *resourceCandidates);
static std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot>
BuildDx12CommandExecutionRuntimeSnapshot(
	const Bunny::CommandListMap &commandLists,
	const std::unordered_map<std::wstring, bool> &commandListRuntimeEffect,
	const std::unordered_map<std::wstring, bool> &commandListPreRuntimeEffect,
	const std::unordered_map<std::wstring, bool> &commandListPostRuntimeEffect,
	const Bunny::TextureOverrideMap &textureOverrides,
	const std::unordered_map<std::wstring, bool> &textureOverridePreRuntimeEffect,
	const std::unordered_map<std::wstring, bool> &textureOverridePostRuntimeEffect,
	const std::unordered_map<std::wstring, uint32_t> &textureOverrideSectionIds,
	const std::vector<DX12IaTextureOverrideCandidate> &iaTextureOverrides,
	const std::unordered_map<uint32_t, std::vector<size_t>> &iaIndexTextureOverrideIndex,
	const std::unordered_map<UINT64, std::vector<size_t>> &iaMergedVertexTextureOverrideIndex,
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
	case Bunny::CommandListActionKind::SetShaderResource:
	case Bunny::CommandListActionKind::SetConstantBuffer:
	case Bunny::CommandListActionKind::SetUnorderedAccessView:
	case Bunny::CommandListActionKind::Draw:
	case Bunny::CommandListActionKind::DrawIndexed:
	case Bunny::CommandListActionKind::DrawIndexedAuto:
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
		case Bunny::CommandListActionKind::SetShaderResource:
		case Bunny::CommandListActionKind::SetConstantBuffer:
		case Bunny::CommandListActionKind::SetUnorderedAccessView:
		case Bunny::CommandListActionKind::Dispatch:
			return true;
		case Bunny::CommandListActionKind::HandlingSkip:
		case Bunny::CommandListActionKind::Draw:
		case Bunny::CommandListActionKind::DrawIndexed:
		case Bunny::CommandListActionKind::DrawIndexedAuto:
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

static bool AnyRuntimeEffectInMap(
	const std::unordered_map<std::wstring, bool> &effects)
{
	for (const auto &item : effects) {
		if (item.second)
			return true;
	}
	return false;
}

static bool CommandListHasPhaseRuntimeEffect(
	const std::wstring &name,
	bool includePost,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *effectCache,
	std::set<std::wstring> *visiting,
	bool anyTextureOverridePreEffect,
	bool anyTextureOverridePostEffect);

static bool CommandListActionHasPhaseRuntimeEffect(
	const Bunny::CommandListAction &action,
	bool includePost,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *effectCache,
	std::set<std::wstring> *visiting,
	bool anyTextureOverridePreEffect,
	bool anyTextureOverridePostEffect)
{
	switch (action.kind) {
	case Bunny::CommandListActionKind::Run:
		return CommandListHasPhaseRuntimeEffect(
			action.commandList, includePost, commandLists, effectCache, visiting,
			anyTextureOverridePreEffect, anyTextureOverridePostEffect);
	case Bunny::CommandListActionKind::CheckTextureOverride:
		return includePost ? anyTextureOverridePostEffect :
			anyTextureOverridePreEffect;
	case Bunny::CommandListActionKind::HandlingSkip:
	case Bunny::CommandListActionKind::SetIndexBuffer:
	case Bunny::CommandListActionKind::SetVertexBuffer:
	case Bunny::CommandListActionKind::SetShaderResource:
	case Bunny::CommandListActionKind::SetConstantBuffer:
	case Bunny::CommandListActionKind::SetUnorderedAccessView:
	case Bunny::CommandListActionKind::Draw:
	case Bunny::CommandListActionKind::DrawIndexed:
	case Bunny::CommandListActionKind::DrawIndexedAuto:
	case Bunny::CommandListActionKind::DrawFromCaller:
	case Bunny::CommandListActionKind::DrawIndexedFromCaller:
	case Bunny::CommandListActionKind::Dispatch:
		return true;
	default:
		return false;
	}
}

static bool CommandListHasPhaseRuntimeEffect(
	const std::wstring &name,
	bool includePost,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *effectCache,
	std::set<std::wstring> *visiting,
	bool anyTextureOverridePreEffect,
	bool anyTextureOverridePostEffect)
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
			if (CommandListActionHasPhaseRuntimeEffect(
				    action, includePost, commandLists, effectCache, visiting,
				    anyTextureOverridePreEffect, anyTextureOverridePostEffect)) {
				effect = true;
				break;
			}
		}
	}
	visiting->erase(name);
	(*effectCache)[name] = effect;
	return effect;
}

static void BuildDx12CommandListPhaseRuntimeEffectIndexes(
	const Bunny::CommandListMap &commandLists,
	const std::unordered_map<std::wstring, bool> &textureOverridePreEffect,
	const std::unordered_map<std::wstring, bool> &textureOverridePostEffect,
	std::unordered_map<std::wstring, bool> *commandListPreEffect,
	std::unordered_map<std::wstring, bool> *commandListPostEffect)
{
	if (!commandListPreEffect || !commandListPostEffect)
		return;

	commandListPreEffect->clear();
	commandListPostEffect->clear();
	const bool anyTextureOverridePreRuntimeEffect =
		AnyRuntimeEffectInMap(textureOverridePreEffect);
	const bool anyTextureOverridePostRuntimeEffect =
		AnyRuntimeEffectInMap(textureOverridePostEffect);

	for (const auto &item : commandLists) {
		std::set<std::wstring> visiting;
		CommandListHasPhaseRuntimeEffect(
			item.first, false, commandLists, commandListPreEffect, &visiting,
			anyTextureOverridePreRuntimeEffect,
			anyTextureOverridePostRuntimeEffect);
	}
	for (const auto &item : commandLists) {
		std::set<std::wstring> visiting;
		CommandListHasPhaseRuntimeEffect(
			item.first, true, commandLists, commandListPostEffect, &visiting,
			anyTextureOverridePreRuntimeEffect,
			anyTextureOverridePostRuntimeEffect);
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
	const std::unordered_map<std::wstring, bool> &commandListPreRuntimeEffect,
	const std::unordered_map<std::wstring, bool> &commandListPostRuntimeEffect,
	const Bunny::TextureOverrideMap &textureOverrides,
	const std::unordered_map<std::wstring, bool> &textureOverridePreRuntimeEffect,
	const std::unordered_map<std::wstring, bool> &textureOverridePostRuntimeEffect,
	const std::unordered_map<std::wstring, uint32_t> &textureOverrideSectionIds,
	const std::vector<DX12IaTextureOverrideCandidate> &iaTextureOverrides,
	const std::unordered_map<uint32_t, std::vector<size_t>> &iaIndexTextureOverrideIndex,
	const std::unordered_map<UINT64, std::vector<size_t>> &iaMergedVertexTextureOverrideIndex,
	const std::unordered_map<std::wstring, DX12CompiledCommandListPlan> &compiledCommandListPlans,
	const std::unordered_map<uint32_t, DX12CompiledTextureOverridePlan> &compiledTextureOverridePlans,
	const std::unordered_map<std::wstring, DX12CompiledShaderOverridePlan> &compiledShaderOverridePlans)
{
	std::shared_ptr<DX12CommandExecutionRuntimeSnapshot> snapshot =
		std::make_shared<DX12CommandExecutionRuntimeSnapshot>();
	snapshot->anyTextureOverridePreRuntimeEffect =
		AnyRuntimeEffectInMap(textureOverridePreRuntimeEffect);
	snapshot->anyTextureOverridePostRuntimeEffect =
		AnyRuntimeEffectInMap(textureOverridePostRuntimeEffect);
	snapshot->commandListRuntimeEffect = commandListRuntimeEffect;
	snapshot->commandListPreRuntimeEffect = commandListPreRuntimeEffect;
	snapshot->commandListPostRuntimeEffect = commandListPostRuntimeEffect;
	for (const auto &item : commandLists) {
		snapshot->commandLists.emplace(
			item.first,
			std::make_shared<const Bunny::CommandListConfig>(item.second));
	}
	auto makeTriggered = [](
		const std::shared_ptr<const Bunny::TextureOverrideConfig> &config,
		uint32_t sectionId,
		bool preRuntimeEffect,
		bool postRuntimeEffect,
		bool indexBuffer,
		uint32_t vertexSlot) {
		DX12TriggeredTextureOverride triggered;
		triggered.config = config.get();
		triggered.sectionId = sectionId;
		triggered.preRuntimeEffect = preRuntimeEffect;
		triggered.postRuntimeEffect = postRuntimeEffect;
		triggered.indexBuffer = indexBuffer;
		triggered.vertexSlot = vertexSlot;
		triggered.executeCommands = true;
		triggered.executeDrawActions = true;
		return triggered;
	};
	auto hasDrawContext = [](const Bunny::TextureOverrideConfig &config) {
		return config.hasMatchVertexCount ||
			config.hasMatchIndexCount ||
			config.hasMatchInstanceCount ||
			config.hasMatchFirstVertex ||
			config.hasMatchFirstIndex ||
			config.hasMatchFirstInstance;
	};
	for (const auto &bucket : textureOverrides) {
		for (const Bunny::TextureOverrideConfig &config : bucket.second) {
			auto sectionId = textureOverrideSectionIds.find(config.section);
			if (sectionId == textureOverrideSectionIds.end() || !sectionId->second)
				continue;
			std::shared_ptr<const Bunny::TextureOverrideConfig> configSnapshot =
				std::make_shared<const Bunny::TextureOverrideConfig>(config);
			snapshot->textureOverridesBySectionId[sectionId->second] = configSnapshot;
			DX12TriggeredTextureOverride triggered = makeTriggered(
				configSnapshot,
				sectionId->second,
				LookupRuntimeEffect(textureOverridePreRuntimeEffect, config.section),
				LookupRuntimeEffect(textureOverridePostRuntimeEffect, config.section),
				false,
				0);
			snapshot->textureOverridesByHash[config.hash].push_back(triggered);

			const bool hasDrawContextMatch = hasDrawContext(config);
			bool indexedByExactCount = false;
			if (!hasDrawContextMatch) {
				snapshot->textureOverridesWithoutDrawContext.push_back(triggered);
			} else {
				if (config.hasMatchIndexCount &&
				    config.matchIndexCount.op == Bunny::NumericMatchOp::Equal) {
					snapshot->textureOverridesByIndexCount[config.matchIndexCount.value]
						.push_back(triggered);
					indexedByExactCount = true;
				}
				if (config.hasMatchVertexCount &&
				    config.matchVertexCount.op == Bunny::NumericMatchOp::Equal) {
					snapshot->textureOverridesByVertexCount[config.matchVertexCount.value]
						.push_back(triggered);
					indexedByExactCount = true;
				}
				if (!indexedByExactCount)
					snapshot->textureOverridesGenericDrawContext.push_back(triggered);
			}
		}
	}
	auto appendTriggeredByIndexes =
		[snapshot, &makeTriggered](
			const std::vector<DX12IaTextureOverrideCandidate> &candidates,
			const std::vector<size_t> &indexes,
			bool indexBuffer,
			uint32_t vertexSlot,
			std::vector<DX12TriggeredTextureOverride> *out) {
			if (!out)
				return;
			for (size_t index : indexes) {
				if (index >= candidates.size())
					continue;
				const DX12IaTextureOverrideCandidate &candidate = candidates[index];
				if (!candidate.sectionId)
					continue;
				auto config = snapshot->textureOverridesBySectionId.find(candidate.sectionId);
				if (config == snapshot->textureOverridesBySectionId.end())
					continue;
				out->push_back(makeTriggered(
					config->second,
					candidate.sectionId,
					candidate.preRuntimeEffect,
					candidate.postRuntimeEffect,
					indexBuffer,
					vertexSlot));
			}
		};
	for (const auto &item : iaIndexTextureOverrideIndex) {
		appendTriggeredByIndexes(
			iaTextureOverrides,
			item.second,
			true,
			0,
			&snapshot->iaIndexTriggeredTextureOverrides[item.first]);
	}
	for (const auto &item : iaMergedVertexTextureOverrideIndex) {
		appendTriggeredByIndexes(
			iaTextureOverrides,
			item.second,
			false,
			static_cast<uint32_t>(item.first & 0xffffffffull),
			&snapshot->iaMergedVertexTriggeredTextureOverrides[item.first]);
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
	std::unordered_map<std::wstring, bool> commandListPreRuntimeEffect;
	std::unordered_map<std::wstring, bool> commandListPostRuntimeEffect;
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
	std::vector<size_t> iaIndexTextureOverridePrefilterIndex;
	std::unordered_map<uint32_t, std::vector<size_t>> iaIndexTextureOverridePrefilterByIndexCount;
	std::unordered_map<uint32_t, std::vector<size_t>> iaVertexTextureOverridePrefilterIndex;
	std::vector<size_t> iaAnyVertexTextureOverridePrefilterIndex;
	std::unordered_map<uint32_t, std::vector<size_t>> iaMergedVertexTextureOverridePrefilterIndex;
	std::unordered_map<uint32_t, std::vector<size_t>> iaAnyVertexTextureOverridePrefilterByVertexCount;
	std::unordered_map<UINT64, std::vector<size_t>> iaVertexTextureOverridePrefilterByVertexCount;
	std::unordered_map<UINT64, std::vector<size_t>> iaMergedVertexTextureOverridePrefilterByVertexCount;
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


	bool hasShaderDescriptorTextureOverrideTriggers =
		hasShaderTriggeredTextureOverrides &&
		(ShaderOverridesHaveDescriptorTextureOverrideTrigger(shaderOverrides, commandLists) ||
		 ShaderRegexesHaveDescriptorTextureOverrideTrigger(shaderRegexes, commandLists));
	BuildDx12RuntimeEffectIndexes(
		textureOverrides, commandLists,
		&commandListRuntimeEffect,
		&textureOverridePreRuntimeEffect,
		&textureOverridePostRuntimeEffect);
	BuildDx12CommandListPhaseRuntimeEffectIndexes(
		commandLists,
		textureOverridePreRuntimeEffect,
		textureOverridePostRuntimeEffect,
		&commandListPreRuntimeEffect,
		&commandListPostRuntimeEffect);
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
		&iaMergedVertexTextureOverrideIndex,
		&iaIndexTextureOverridePrefilterIndex,
		&iaIndexTextureOverridePrefilterByIndexCount,
		&iaVertexTextureOverridePrefilterIndex,
		&iaAnyVertexTextureOverridePrefilterIndex,
		&iaAnyVertexTextureOverridePrefilterByVertexCount,
		&iaMergedVertexTextureOverridePrefilterIndex,
		&iaVertexTextureOverridePrefilterByVertexCount,
		&iaMergedVertexTextureOverridePrefilterByVertexCount);
	BuildDx12CompiledCommandListPlans(
		commandLists, &compiledCommandListPlans);
	BuildDx12CompiledTextureOverridePlans(
		textureOverrides, textureOverrideSectionIds, compiledCommandListPlans,
		&compiledTextureOverridePlans);
	BuildDx12CompiledShaderOverridePlans(
		shaderOverrides, shaderRegexes, compiledCommandListPlans,
		&compiledShaderOverridePlans);
	// Plan step 3.1: write-time static texture replacement table. Built from
	// the parsed configs before the swap-in below publishes them.
	std::shared_ptr<DX12StaticTextureReplacementTable> staticTextureReplacements =
		BuildDx12StaticTextureReplacementTable(
			textureOverrides, textureOverrideSectionIds);
	staticTextureReplacements->reloadGeneration = gReloadGeneration + 1;
	// Plan step 3.2: bind-time static IA replacement table. Disabled entirely
	// while any shader override reads IA checktextureoverride results.
	std::shared_ptr<DX12StaticIaReplacementTable> staticIaReplacements =
		BuildDx12StaticIaReplacementTable(
			textureOverrides, hasShaderIaTextureOverrideTriggers);
	staticIaReplacements->reloadGeneration = gReloadGeneration + 1;
	// Any draw-count-indexed prefilter table makes the "has candidate" answer
	// count-dependent; publish that before the tables are swapped in below so
	// the draw path can choose IA-state-level negative caching.
	const bool iaPrefilterCountSensitive =
		!iaIndexTextureOverridePrefilterByIndexCount.empty() ||
		!iaAnyVertexTextureOverridePrefilterByVertexCount.empty() ||
		!iaVertexTextureOverridePrefilterByVertexCount.empty() ||
		!iaMergedVertexTextureOverridePrefilterByVertexCount.empty();
	bool hasIaResidentStateRequirements =
		hasShaderIaTextureOverrideTriggers ||
		CompiledShaderOverridePlansNeedResidentIaState(compiledShaderOverridePlans);
	commandExecutionRuntimeSnapshot = BuildDx12CommandExecutionRuntimeSnapshot(
		commandLists,
		commandListRuntimeEffect,
		commandListPreRuntimeEffect,
		commandListPostRuntimeEffect,
		textureOverrides,
		textureOverridePreRuntimeEffect,
		textureOverridePostRuntimeEffect,
		textureOverrideSectionIds,
		iaTextureOverrides,
		iaIndexTextureOverrideIndex,
		iaMergedVertexTextureOverrideIndex,
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
	gCommandListPreRuntimeEffect.swap(commandListPreRuntimeEffect);
	gCommandListPostRuntimeEffect.swap(commandListPostRuntimeEffect);
	gTextureOverridePreRuntimeEffect.swap(textureOverridePreRuntimeEffect);
	gTextureOverridePostRuntimeEffect.swap(textureOverridePostRuntimeEffect);
	gTextureOverrideSectionIds.swap(textureOverrideSectionIds);
	gIaTextureOverrides.swap(iaTextureOverrides);
	gIaIndexTextureOverrideIndex.swap(iaIndexTextureOverrideIndex);
	gIaVertexTextureOverrideIndex.swap(iaVertexTextureOverrideIndex);
	gIaAnyVertexTextureOverrideIndex.swap(iaAnyVertexTextureOverrideIndex);
	gIaMergedVertexTextureOverrideIndex.swap(iaMergedVertexTextureOverrideIndex);
	gIaIndexTextureOverridePrefilterIndex.swap(iaIndexTextureOverridePrefilterIndex);
	gIaIndexTextureOverridePrefilterByIndexCount.swap(
		iaIndexTextureOverridePrefilterByIndexCount);
	gIaVertexTextureOverridePrefilterIndex.swap(iaVertexTextureOverridePrefilterIndex);
	gIaAnyVertexTextureOverridePrefilterIndex.swap(iaAnyVertexTextureOverridePrefilterIndex);
	gIaAnyVertexTextureOverridePrefilterByVertexCount.swap(
		iaAnyVertexTextureOverridePrefilterByVertexCount);
	gIaMergedVertexTextureOverridePrefilterIndex.swap(iaMergedVertexTextureOverridePrefilterIndex);
	gIaVertexTextureOverridePrefilterByVertexCount.swap(
		iaVertexTextureOverridePrefilterByVertexCount);
	gIaMergedVertexTextureOverridePrefilterByVertexCount.swap(
		iaMergedVertexTextureOverridePrefilterByVertexCount);
	gTextureOverrideLookupCache.clear();
	gIaTextureOverrideHashNoCandidates.clear();
	InterlockedExchange(&gIaPrefilterCountSensitive,
		iaPrefilterCountSensitive ? 1 : 0);
	DX12ModClearIaReplacementRuntimeCaches();
	gCompiledCommandListPlans.swap(compiledCommandListPlans);
	gCompiledTextureOverridePlans.swap(compiledTextureOverridePlans);
	gCommandExecutionRuntimeSnapshot.store(
		commandExecutionRuntimeSnapshot,
		std::memory_order_release);
	// Publish the write-time static texture replacement table (plan step 3.1)
	// and drop resolved descriptor handles cached against the old table.
	gStaticTextureReplacementSnapshot.store(
		std::shared_ptr<const DX12StaticTextureReplacementTable>(
			staticTextureReplacements),
		std::memory_order_release);
	InterlockedExchange(&gHasStaticTextureReplacements,
		staticTextureReplacements->enabled ? 1 : 0);
	DX12NoteStaticTextureReplacementTablePublished();
	gStaticIaReplacementSnapshot.store(
		std::shared_ptr<const DX12StaticIaReplacementTable>(
			staticIaReplacements),
		std::memory_order_release);
	InterlockedExchange(&gHasStaticIaReplacements,
		staticIaReplacements->enabled ? 1 : 0);
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
	ClearPreSkinStickyMatchCache();
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

static DX12LoadedResource *EnsureLoadedResourceForPreSkin(
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
	AcquireSRWLockShared(&gModLock);
	auto loaded = gLoadedResources.find(name);
	if (loaded != gLoadedResources.end()) {
		DX12LoadedResource *resource =
			loaded->second.resource && !loaded->second.failed ? &loaded->second : nullptr;
		ReleaseSRWLockShared(&gModLock);
		return resource;
	}
	ReleaseSRWLockShared(&gModLock);

	AcquireSRWLockExclusive(&gModLock);
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

// ---------------------------------------------------------------------------
// Write-time static texture replacement (plan step 3.1)
//
// DX12 gives every descriptor write to the application: CreateShaderResourceView
// hands us the resource pointer, CopyDescriptors hands us src and dst. When a
// TextureOverride is an unconditional static replacement (hash match only, no
// draw-context filters, no match_cs / pre-skin dependency, no command lists),
// the mod descriptor can be written at that point and the draw path never has
// to resolve SRV bindings for it. This block resolves original hash ->
// replacement SRV descriptor handle and is called from the tracking TU hooks
// through the non-static interface declared in Mods/DX12ModInterfaces.hpp.
// ---------------------------------------------------------------------------

class DX12StaticTextureReplacementResolveCacheEntry
{
public:
	UINT64 tableGeneration = 0;
	D3D12_CPU_DESCRIPTOR_HANDLE defaultSrv = {};
	bool hasDefaultSrv = false;
};

// Keyed by original texture hash. Only the lookup result (descriptor handle)
// is cached; the DX12LoadedResource owns the descriptor heap that backs it.
static std::unordered_map<uint32_t, DX12StaticTextureReplacementResolveCacheEntry>
	gStaticTextureReplacementResolveCache;
static SRWLOCK gStaticTextureReplacementResolveLock = SRWLOCK_INIT;

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
	return InterlockedCompareExchange(
		&gHasStaticIaReplacements, 0, 0) != 0;
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
		if (vertexSlot >= 32)
			return false;
		const uint64_t key =
			(static_cast<uint64_t>(vertexSlot) << 32) |
			static_cast<uint64_t>(contentHash);
		auto it = table->vertexByHashAndSlot.find(key);
		if (it != table->vertexByHashAndSlot.end())
			resourceName = &it->second;
	}
	if (!resourceName || resourceName->empty())
		return false;

	DX12LoadedResource *resource =
		EnsureLoadedResourceForPreSkin(device, *resourceName);
	if (!resource || !resource->resource)
		return false;

	const UINT64 byteWidth = resource->byteWidth;
	if (!byteWidth || byteWidth > UINT_MAX)
		return false;

	if (indexBuffer) {
		if (!outIndexView)
			return false;
		outIndexView->BufferLocation =
			resource->resource->GetGPUVirtualAddress();
		outIndexView->SizeInBytes = static_cast<UINT>(byteWidth);
		// Mirror the draw-time path: keep the mod resource's own R16/R32
		// format when declared, otherwise reuse the game's view format.
		outIndexView->Format =
			(resource->format == DXGI_FORMAT_R16_UINT ||
			 resource->format == DXGI_FORMAT_R32_UINT) ?
				resource->format :
				(originalIndexView ? originalIndexView->Format :
					DXGI_FORMAT_R32_UINT);
		return true;
	}

	if (!outVertexView)
		return false;
	outVertexView->BufferLocation =
		resource->resource->GetGPUVirtualAddress();
	outVertexView->SizeInBytes = static_cast<UINT>(byteWidth);
	outVertexView->StrideInBytes =
		resource->stride ? resource->stride :
			(originalVertexView ? originalVertexView->StrideInBytes : 0);
	return outVertexView->StrideInBytes != 0;
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

class DX12DrawSrvPatchRequest
{
public:
	DX12CurrentShaderResourceBinding binding;
	DX12LoadedResource *resource = nullptr;
	bool compute = false;
	const char *source = "";
	D3D12_CPU_DESCRIPTOR_HANDLE replacementCpu = {};
	DXGI_FORMAT replacementViewFormat = DXGI_FORMAT_UNKNOWN;
};

class DX12DrawSrvPatchTable
{
public:
	bool compute = false;
	bool sampler = false;
	UINT rootParameterIndex = 0;
	SIZE_T cpuStart = 0;
	D3D12_GPU_DESCRIPTOR_HANDLE gpuStart = {};
	ID3D12DescriptorHeap *heap = nullptr;
	UINT increment = 0;
	UINT count = 0;
	UINT tempOffset = 0;
};

class DX12DrawSrvRootPlanTemplate
{
public:
	bool valid = false;
	bool sampler = false;
	bool dynamicCount = false;
	UINT fixedCount = 0;
};

class DX12DrawSrvSignaturePlanTemplate
{
public:
	ID3D12RootSignature *rootSignature = nullptr;
	UINT64 rootSignatureHash = 0;
	DX12DrawSrvRootPlanTemplate roots[MaxRootParameters] = {};
};

class DX12DrawSrvReplacementUtils
{
public:
	static void SyncDescriptorHeaps(
		ID3D12GraphicsCommandList *commandList,
		ID3D12DescriptorHeap *cbvSrvUavHeap,
		ID3D12DescriptorHeap *samplerHeap)
	{
		if (!commandList)
			return;
		ID3D12DescriptorHeap *heaps[2] = {};
		UINT heapCount = 0;
		if (cbvSrvUavHeap)
			heaps[heapCount++] = cbvSrvUavHeap;
		if (samplerHeap)
			heaps[heapCount++] = samplerHeap;
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		if (runtimeState) {
			DX12CommandListRuntime::SetDescriptorHeaps(
				runtimeState, cbvSrvUavHeap, samplerHeap);
		} else {
			DX12CommandListRuntime::SetDescriptorHeaps(
				commandList, cbvSrvUavHeap, samplerHeap);
		}
		DX12BindingTracker::SetDescriptorHeaps(
			commandList, heapCount, heapCount ? heaps : nullptr);
	}

	static void SyncRootDescriptorTable(
		ID3D12GraphicsCommandList *commandList,
		bool compute,
		UINT rootParameterIndex,
		D3D12_GPU_DESCRIPTOR_HANDLE table)
	{
		if (!commandList)
			return;
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		if (runtimeState) {
			DX12CommandListRuntime::RememberRootDescriptorTable(
				runtimeState, !compute, rootParameterIndex, table);
		} else {
			DX12CommandListRuntime::RememberRootDescriptorTable(
				commandList, !compute, rootParameterIndex, table);
		}
		if (compute) {
			DX12BindingTracker::SetComputeRootDescriptorTable(
				commandList, rootParameterIndex, table);
		} else {
			DX12BindingTracker::SetGraphicsRootDescriptorTable(
				commandList, rootParameterIndex, table);
		}
	}

	static DX12DrawSrvSignaturePlanTemplate BuildSignaturePlanTemplate(
		const DX12RootSignatureSummary &rootSignature)
	{
		DX12DrawSrvSignaturePlanTemplate plan;
		plan.rootSignature = rootSignature.rootSignature;
		plan.rootSignatureHash = rootSignature.hash;
		for (const DX12RootParameterSummary &parameter :
				rootSignature.parameters) {
			if (parameter.rootParameterIndex >= MaxRootParameters)
				continue;
			DX12DrawSrvRootPlanTemplate &rootPlan =
				plan.roots[parameter.rootParameterIndex];
			if (parameter.parameterType !=
					D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE ||
					parameter.ranges.empty()) {
				rootPlan = DX12DrawSrvRootPlanTemplate();
				continue;
			}
			bool hasSampler = false;
			bool hasResource = false;
			bool dynamicCount = false;
			UINT64 fixedCount = 0;
			for (const DX12RootDescriptorRangeSummary &range :
					parameter.ranges) {
				if (range.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER)
					hasSampler = true;
				else
					hasResource = true;
				if (range.numDescriptors == UINT_MAX) {
					dynamicCount = true;
					continue;
				}
				const UINT64 count =
					static_cast<UINT64>(range.effectiveOffset) + range.numDescriptors;
				fixedCount = (std::max)(fixedCount, count);
			}
			if (hasSampler == hasResource)
				continue;
			rootPlan.valid = dynamicCount || fixedCount != 0;
			rootPlan.sampler = hasSampler;
			rootPlan.dynamicCount = dynamicCount;
			rootPlan.fixedCount = fixedCount > UINT_MAX ?
				UINT_MAX : static_cast<UINT>(fixedCount);
		}
		return plan;
	}

	static bool GetSignaturePlanTemplate(
		const DX12RootSignatureSummary &rootSignature,
		DX12DrawSrvSignaturePlanTemplate *plan)
	{
		if (!plan || !rootSignature.rootSignature)
			return false;

		static SRWLOCK s_planCacheLock = SRWLOCK_INIT;
		static std::unordered_map<ID3D12RootSignature*,
			DX12DrawSrvSignaturePlanTemplate> s_planCache;

		AcquireSRWLockShared(&s_planCacheLock);
		auto found = s_planCache.find(rootSignature.rootSignature);
		if (found != s_planCache.end() &&
		    found->second.rootSignatureHash == rootSignature.hash) {
			*plan = found->second;
			ReleaseSRWLockShared(&s_planCacheLock);
			return true;
		}
		ReleaseSRWLockShared(&s_planCacheLock);

		DX12DrawSrvSignaturePlanTemplate built =
			DX12DrawSrvReplacementUtils::BuildSignaturePlanTemplate(
				rootSignature);

		AcquireSRWLockExclusive(&s_planCacheLock);
		DX12DrawSrvSignaturePlanTemplate &stored =
			s_planCache[rootSignature.rootSignature];
		stored = built;
		*plan = stored;
		ReleaseSRWLockExclusive(&s_planCacheLock);
		return true;
	}

	static bool ShaderVisibilityMatchesPixel(UINT visibility, bool compute)
	{
		if (compute)
			return true;
		return visibility == D3D12_SHADER_VISIBILITY_ALL ||
			visibility == D3D12_SHADER_VISIBILITY_PIXEL;
	}

	static bool ShaderVisibilityMatchesVertex(UINT visibility, bool compute)
	{
		if (compute)
			return true;
		return visibility == D3D12_SHADER_VISIBILITY_ALL ||
			visibility == D3D12_SHADER_VISIBILITY_VERTEX;
	}

	static bool BindingCanBePatched(const DX12CurrentShaderResourceBinding &binding)
	{
		return !binding.rootDescriptor &&
			binding.tableCpuStart != 0 &&
			binding.tableGpuStart.ptr != 0 &&
			binding.descriptorIncrement != 0 &&
			binding.tableHeap != nullptr;
	}

	static bool CollectSrvBindingsFromRuntimeLayout(
		ID3D12GraphicsCommandList *commandList,
		bool compute,
		std::vector<DX12CurrentShaderResourceBinding> *bindings)
	{
		if (!bindings)
			return false;
		bindings->clear();
		if (!commandList)
			return false;

		// thread_local: SnapshotBindingState fully overwrites the object on
		// success, so zero-initializing the ~60KB NSDMI arrays on every call
		// is wasted work on this per-draw query path.
		static thread_local CommandListBindingState snapshot;
		if (!DX12BindingTracker::SnapshotBindingState(commandList, &snapshot))
			return false;

		ID3D12RootSignature *rootSignature =
			compute ? snapshot.computeRootSignature : snapshot.graphicsRootSignature;
		if (!rootSignature)
			return false;

		DX12RootSignatureSummary rootSignatureSummary;
		if (!DX12GetRootSignatureSummary(rootSignature, &rootSignatureSummary) ||
		    !rootSignatureSummary.parsed) {
			return false;
		}

		UINT estimatedBindingCount = 0;
		for (const DX12RootParameterSummary &parameter :
				rootSignatureSummary.parameters) {
			if (parameter.parameterType != D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE ||
			    parameter.rootParameterIndex >= MaxRootParameters) {
				continue;
			}
			for (const DX12RootDescriptorRangeSummary &range : parameter.ranges) {
				if (range.rangeType != D3D12_DESCRIPTOR_RANGE_TYPE_SRV)
					continue;
				if (range.numDescriptors == UINT_MAX)
					continue;
				estimatedBindingCount += range.numDescriptors;
			}
		}
		if (estimatedBindingCount)
			bindings->reserve(estimatedBindingCount);

		for (const DX12RootParameterSummary &parameter :
				rootSignatureSummary.parameters) {
			if (parameter.parameterType != D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE ||
			    parameter.rootParameterIndex >= MaxRootParameters) {
				continue;
			}

			const RootTableState &table =
				compute ?
					snapshot.computeTables[parameter.rootParameterIndex] :
					snapshot.graphicsTables[parameter.rootParameterIndex];
			if (!table.valid)
				continue;

			DX12DescriptorHeapSummary heap;
			UINT64 tableBaseIndex = 0;
			SIZE_T tableCpuStart = 0;
			if (!ResolvePatchTableLocation(
					snapshot,
					table,
					D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
					&heap,
					&tableBaseIndex,
					&tableCpuStart)) {
				continue;
			}

			const UINT tableCopyCount = DescriptorCountForTable(
				parameter,
				heap,
				tableBaseIndex,
				D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			for (const DX12RootDescriptorRangeSummary &range : parameter.ranges) {
				if (range.rangeType != D3D12_DESCRIPTOR_RANGE_TYPE_SRV)
					continue;
				const UINT64 first = tableBaseIndex + range.effectiveOffset;
				if (first >= heap.numDescriptors)
					continue;
				const UINT64 available = heap.numDescriptors - first;
				const UINT64 declared = range.numDescriptors == UINT_MAX ?
					available : range.numDescriptors;
				const UINT count = static_cast<UINT>((std::min)(declared, available));
				for (UINT offset = 0; offset < count; ++offset) {
					DX12CurrentShaderResourceBinding binding;
					binding.rootParameterIndex = parameter.rootParameterIndex;
					binding.rangeIndex = range.rangeIndex;
					binding.shaderVisibility = range.shaderVisibility;
					binding.shaderRegister = range.baseShaderRegister + offset;
					binding.registerSpace = range.registerSpace;
					binding.descriptorOffset = range.effectiveOffset + offset;
					binding.descriptorIncrement = heap.increment;
					binding.tableCopyCount = tableCopyCount;
					binding.tableCpuStart = tableCpuStart;
					binding.tableGpuStart = table.baseDescriptor;
					binding.tableHeap = heap.heap;
					bindings->push_back(std::move(binding));
				}
			}
		}

		return !bindings->empty();
	}

	static void CollectSrvBindings(
		ID3D12GraphicsCommandList *commandList,
		bool compute,
		std::vector<DX12CurrentShaderResourceBinding> *bindings)
	{
		if (!bindings)
			return;
		bindings->clear();
		if (!commandList)
			return;
		if (!DX12ModNeedsShaderDescriptorTracking() &&
		    DX12DrawSrvReplacementUtils::CollectSrvBindingsFromRuntimeLayout(
				commandList, compute, bindings)) {
			return;
		}
		DX12BindingTracker::GetCurrentShaderResourceBindings(
			commandList, compute, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, bindings);
	}

	static uint64_t BindingKey(const DX12CurrentShaderResourceBinding &binding, bool compute)
	{
		uint64_t key = compute ? 0x9ae16a3b2f90404full : 0xc3a5c85c97cb3127ull;
		key ^= static_cast<uint64_t>(binding.rootParameterIndex) + 0x9e3779b97f4a7c15ull + (key << 6) + (key >> 2);
		key ^= static_cast<uint64_t>(binding.descriptorOffset) + 0x9e3779b97f4a7c15ull + (key << 6) + (key >> 2);
		key ^= static_cast<uint64_t>(binding.tableGpuStart.ptr) + 0x9e3779b97f4a7c15ull + (key << 6) + (key >> 2);
		return key;
	}

	static void AddOrReplaceRequest(
		ID3D12Device *device,
		const DX12CurrentShaderResourceBinding &binding,
		DX12LoadedResource *resource,
		bool compute,
		const char *source,
		std::vector<DX12DrawSrvPatchRequest> *requests)
	{
		if (!requests || !resource || !resource->drawSrvCpu.ptr)
			return;
		if (!DX12DrawSrvReplacementUtils::BindingCanBePatched(binding))
			return;
		// Use the DDS default SRV directly. This matches DX11 behavior:
		// DirectX::CreateDDSTextureFromFileEx creates the replacement SRV
		// with DEFAULT component mapping, and PSSetShaderResources uses it
		// as-is without any "compatibility" conversion.
		D3D12_CPU_DESCRIPTOR_HANDLE replacementCpu = resource->drawSrvCpu;
		DXGI_FORMAT replacementViewFormat = resource->drawSrvFormat;
		const uint64_t key = DX12DrawSrvReplacementUtils::BindingKey(binding, compute);
		for (DX12DrawSrvPatchRequest &request : *requests) {
			if (DX12DrawSrvReplacementUtils::BindingKey(request.binding, request.compute) == key) {
				request.resource = resource;
				request.source = source ? source : "";
				request.replacementCpu = replacementCpu;
				request.replacementViewFormat = replacementViewFormat;
				return;
			}
		}
		DX12DrawSrvPatchRequest request;
		request.binding = binding;
		request.resource = resource;
		request.compute = compute;
		request.source = source ? source : "";
		request.replacementCpu = replacementCpu;
		request.replacementViewFormat = replacementViewFormat;
		requests->push_back(request);
	}

	static UINT AddSlotRequestsFromBindings(
		ID3D12Device *device,
		ID3D12GraphicsCommandList *commandList,
		const std::vector<DX12CurrentShaderResourceBinding> &bindings,
		bool compute,
		UINT slot,
		const std::wstring &resourceName,
		bool pixelStage,
		const char *source,
		std::vector<DX12DrawSrvPatchRequest> *requests)
	{
		if (!device || !commandList || !requests || resourceName.empty())
			return 0;
		UINT added = 0;
		DX12LoadedResource *resource = nullptr;
		for (const DX12CurrentShaderResourceBinding &binding : bindings) {
			if (binding.shaderRegister != slot || binding.registerSpace != 0)
				continue;
			const bool visibilityMatches = pixelStage ?
				DX12DrawSrvReplacementUtils::ShaderVisibilityMatchesPixel(binding.shaderVisibility, compute) :
				DX12DrawSrvReplacementUtils::ShaderVisibilityMatchesVertex(binding.shaderVisibility, compute);
			if (!visibilityMatches)
				continue;
			if (!resource)
				resource = DX12DdsTextureLoadUtils::EnsureLoadedDrawTextureSrv(
					device, commandList, resourceName);
			if (!resource)
				return added;
			DX12DrawSrvReplacementUtils::AddOrReplaceRequest(
				device, binding, resource, compute, source, requests);
			++added;
		}
		return added;
	}

	static UINT AddSlotRequestsByRegister(
		ID3D12Device *device,
		ID3D12GraphicsCommandList *commandList,
		bool compute,
		UINT slot,
		const std::wstring &resourceName,
		bool pixelStage,
		const char *source,
		std::vector<DX12DrawSrvPatchRequest> *requests,
		size_t *bindingCount = nullptr)
	{
		if (bindingCount)
			*bindingCount = 0;
		if (!device || !commandList || !requests || resourceName.empty())
			return 0;

		std::vector<DX12CurrentShaderResourceBinding> bindings;
		if (!DX12BindingTracker::GetCurrentShaderResourceBindingsByRegister(
				commandList,
				compute,
				D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
				slot,
				0,
				&bindings)) {
			return 0;
		}
		if (bindingCount)
			*bindingCount = bindings.size();
		return DX12DrawSrvReplacementUtils::AddSlotRequestsFromBindings(
			device, commandList, bindings, compute, slot, resourceName,
			pixelStage, source, requests);
	}

	static UINT AddThisRequestFromMatch(
		ID3D12Device *device,
		ID3D12GraphicsCommandList *commandList,
		const DX12TextureOverrideMatch *match,
		const std::wstring &resourceName,
		std::vector<DX12DrawSrvPatchRequest> *requests)
	{
		if (!device || !commandList || !requests || resourceName.empty())
			return 0;
		if (!match || !match->hasThisSrvTarget) {
			DX12LogManager::Fields("DX12DrawSrvReplacement",
				"stage=slot_patch_fail,source=this,reason=no_this_target,resource=%S",
				resourceName.c_str());
			return 0;
		}
		DX12LoadedResource *resource =
			DX12DdsTextureLoadUtils::EnsureLoadedDrawTextureSrv(
				device, commandList, resourceName);
		if (!resource)
			return 0;
		DX12DrawSrvReplacementUtils::AddOrReplaceRequest(
			device, match->thisSrvBinding, resource, match->compute, "this", requests);
		return 1;
	}

	static bool QueueRequests(
		const std::vector<DX12DrawSrvPatchRequest> &requests,
		DX12ModIaReplacement *replacement)
	{
		if (!replacement || requests.empty())
			return false;
		for (const DX12DrawSrvPatchRequest &request : requests) {
			if (!request.resource)
				continue;
			D3D12_CPU_DESCRIPTOR_HANDLE replacementCpu = request.replacementCpu.ptr ?
				request.replacementCpu : request.resource->drawSrvCpu;
			if (!replacementCpu.ptr)
				continue;
			const uint64_t key = BindingKey(request.binding, request.compute);
			DX12ModIaReplacement::DrawSrvTablePatch *queued = nullptr;
			for (DX12ModIaReplacement::DrawSrvTablePatch &existing :
					replacement->drawSrvPatches) {
				if (BindingKey(existing.binding, existing.compute) == key) {
					queued = &existing;
					break;
				}
			}
			if (!queued) {
				replacement->drawSrvPatches.push_back(
					DX12ModIaReplacement::DrawSrvTablePatch());
				queued = &replacement->drawSrvPatches.back();
			}
			queued->binding = request.binding;
			queued->replacementCpu = replacementCpu;
			queued->replacementViewFormat = request.replacementViewFormat;
			queued->resourceName = request.resource->name;
			queued->source = request.source ? request.source : "";
			queued->compute = request.compute;
			queued->rootParameterIndex = request.binding.rootParameterIndex;
			queued->originalTable = request.binding.tableGpuStart;
		}
		return !replacement->drawSrvPatches.empty();
	}

	static bool ResolveTableHeap(
		const RootTableState &table,
		D3D12_DESCRIPTOR_HEAP_TYPE type,
		DX12DescriptorHeapSummary *heap)
	{
		if (!heap || !table.valid)
			return false;
		*heap = DX12DescriptorHeapSummary();
		if (table.runtimeHeapResolved &&
				table.runtimeHeapType == static_cast<UINT>(type) &&
				table.runtimeHeapIncrement && table.runtimeHeapGpuStart) {
			heap->heap = table.runtimeHeap;
			heap->type = table.runtimeHeapType;
			heap->numDescriptors = table.runtimeHeapNumDescriptors;
			heap->cpuStart = table.runtimeHeapCpuStart;
			heap->gpuStart = table.runtimeHeapGpuStart;
			heap->increment = table.runtimeHeapIncrement;
			return true;
		}
		return DX12FindDescriptorHeapByGpuHandle(
			table.baseDescriptor, type, heap) && heap->increment != 0;
	}

	static UINT DescriptorCountForTable(
		const DX12RootParameterSummary &parameter,
		const DX12DescriptorHeapSummary &heap,
		UINT64 tableBaseIndex,
		D3D12_DESCRIPTOR_HEAP_TYPE type)
	{
		UINT64 required = 0;
		for (const DX12RootDescriptorRangeSummary &range : parameter.ranges) {
			const bool sampler =
				range.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
			if (sampler != (type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER))
				continue;
			const UINT64 first = tableBaseIndex + range.effectiveOffset;
			if (first >= heap.numDescriptors)
				continue;
			const UINT64 available = heap.numDescriptors - first;
			const UINT64 declared = range.numDescriptors == UINT_MAX ?
				available : range.numDescriptors;
			const UINT64 count = (std::min)(declared, available);
			required = (std::max)(required,
				static_cast<UINT64>(range.effectiveOffset) + count);
		}
		return required > UINT_MAX ? UINT_MAX : static_cast<UINT>(required);
	}

	static const DX12RootParameterSummary *FindRootParameterSummary(
		const DX12RootSignatureSummary &rootSignature,
		UINT rootParameterIndex)
	{
		for (const DX12RootParameterSummary &candidate :
				rootSignature.parameters) {
			if (candidate.rootParameterIndex == rootParameterIndex)
				return &candidate;
		}
		return nullptr;
	}

	static bool ResolvePatchTableLocation(
		const CommandListBindingState &snapshot,
		const RootTableState &state,
		D3D12_DESCRIPTOR_HEAP_TYPE type,
		DX12DescriptorHeapSummary *heap,
		UINT64 *tableBaseIndex,
		SIZE_T *tableCpuStart)
	{
		if (!heap || !tableBaseIndex || !tableCpuStart || !state.valid)
			return false;
		*heap = DX12DescriptorHeapSummary();
		*tableBaseIndex = 0;
		*tableCpuStart = 0;

		ID3D12DescriptorHeap *boundHeap =
			type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER ?
				snapshot.samplerHeap : snapshot.cbvSrvUavHeap;
		if (state.runtimeHeapResolved &&
		    state.runtimeHeapType == static_cast<UINT>(type) &&
		    state.runtimeHeapIncrement != 0 &&
		    state.runtimeHeap != nullptr &&
		    state.runtimeHeap == boundHeap &&
		    state.runtimeTableBaseIndex < state.runtimeHeapNumDescriptors) {
			heap->heap = state.runtimeHeap;
			heap->type = state.runtimeHeapType;
			heap->numDescriptors = state.runtimeHeapNumDescriptors;
			heap->cpuStart = state.runtimeHeapCpuStart;
			heap->gpuStart = state.runtimeHeapGpuStart;
			heap->increment = state.runtimeHeapIncrement;
			heap->version = state.runtimeHeapVersion;
			*tableBaseIndex = state.runtimeTableBaseIndex;
			*tableCpuStart = state.runtimeTableCpuStart;
			return true;
		}

		if (!ResolveTableHeap(state, type, heap) ||
		    !boundHeap ||
		    heap->heap != boundHeap ||
		    !heap->cpuStart ||
		    state.baseDescriptor.ptr < heap->gpuStart) {
			return false;
		}
		const UINT64 tableDelta = state.baseDescriptor.ptr - heap->gpuStart;
		if (tableDelta % heap->increment != 0)
			return false;
		*tableBaseIndex = tableDelta / heap->increment;
		if (*tableBaseIndex >= heap->numDescriptors)
			return false;
		*tableCpuStart =
			heap->cpuStart +
			static_cast<SIZE_T>(*tableBaseIndex) * heap->increment;
		return true;
	}

	static bool RequestMatchesSrvLayout(
		const DX12ModIaReplacement::DrawSrvTablePatch &request,
		const DX12RootSignatureSummary &rootSignature)
	{
		for (const DX12RootParameterSummary &parameter :
				rootSignature.parameters) {
			if (parameter.rootParameterIndex != request.binding.rootParameterIndex ||
					parameter.parameterType !=
						D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
				continue;
			for (const DX12RootDescriptorRangeSummary &range : parameter.ranges) {
				if (range.rangeType != D3D12_DESCRIPTOR_RANGE_TYPE_SRV ||
						request.binding.descriptorOffset < range.effectiveOffset)
					continue;
				const UINT64 relative = static_cast<UINT64>(
					request.binding.descriptorOffset - range.effectiveOffset);
				if (range.numDescriptors != UINT_MAX &&
						relative >= range.numDescriptors)
					continue;
				const UINT64 shaderRegister =
					static_cast<UINT64>(range.baseShaderRegister) + relative;
				if (shaderRegister > UINT_MAX ||
						static_cast<UINT>(shaderRegister) !=
							request.binding.shaderRegister ||
						range.registerSpace != request.binding.registerSpace ||
						(request.binding.rangeIndex != UINT_MAX &&
							range.rangeIndex != request.binding.rangeIndex))
					continue;
				return true;
			}
			return false;
		}
		return false;
	}

	static bool AppendPatchTables(
		const CommandListBindingState &snapshot,
		const DX12RootSignatureSummary &rootSignature,
		bool compute,
		std::vector<DX12DrawSrvPatchTable> *tables,
		UINT *totalDescriptors)
	{
		if (!tables || !totalDescriptors)
			return false;
		static constexpr UINT MaxMirroredDescriptors = 65536;
		DX12DrawSrvSignaturePlanTemplate cachedPlan;
		if (!DX12DrawSrvReplacementUtils::GetSignaturePlanTemplate(
				rootSignature, &cachedPlan))
			return false;
		for (UINT root = 0; root < MaxRootParameters; ++root) {
			const RootTableState &state = compute ?
				snapshot.computeTables[root] : snapshot.graphicsTables[root];
			if (!state.valid)
				continue;
			const DX12DrawSrvRootPlanTemplate &rootPlan =
				cachedPlan.roots[root];
			if (!rootPlan.valid) {
				return false;
			}
			const D3D12_DESCRIPTOR_HEAP_TYPE type = rootPlan.sampler ?
				D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER :
				D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			DX12DescriptorHeapSummary heap = {};
			UINT64 tableBaseIndex = 0;
			SIZE_T tableCpuStart = 0;
			if (!ResolvePatchTableLocation(
					snapshot, state, type,
					&heap, &tableBaseIndex, &tableCpuStart)) {
				return false;
			}
			UINT count = 0;
			if (rootPlan.dynamicCount) {
				const DX12RootParameterSummary *parameter =
					DX12DrawSrvReplacementUtils::FindRootParameterSummary(
						rootSignature, root);
				if (!parameter) {
					return false;
				}
				count = DescriptorCountForTable(
					*parameter, heap, tableBaseIndex, type);
			} else if (tableBaseIndex < heap.numDescriptors) {
				const UINT64 available = heap.numDescriptors - tableBaseIndex;
				const UINT64 clampedCount = (std::min)(
					static_cast<UINT64>(rootPlan.fixedCount),
					available);
				count = clampedCount > UINT_MAX ?
					UINT_MAX : static_cast<UINT>(clampedCount);
			}
			if (!count) {
				return false;
			}
			DX12DrawSrvPatchTable table;
			table.compute = compute;
			table.rootParameterIndex = root;
			table.sampler = rootPlan.sampler;
			table.cpuStart = tableCpuStart;
			table.gpuStart = state.baseDescriptor;
			table.heap = heap.heap;
			table.increment = heap.increment;
			table.count = count;
			if (!rootPlan.sampler) {
				if (*totalDescriptors > MaxMirroredDescriptors ||
						count > MaxMirroredDescriptors - *totalDescriptors) {
					return false;
				}
				table.tempOffset = *totalDescriptors;
				*totalDescriptors += count;
			}
			tables->push_back(table);
		}
		return true;
	}

	static bool BuildPatchTables(
		const CommandListBindingState &snapshot,
		const DX12RootSignatureSummary &rootSignature,
		std::vector<DX12DrawSrvPatchTable> *tables,
		UINT *totalDescriptors)
	{
		if (!tables || !totalDescriptors)
			return false;
		tables->clear();
		*totalDescriptors = 0;
		if (!AppendPatchTables(
				snapshot, rootSignature, false, tables, totalDescriptors))
			return false;
		return !tables->empty() && *totalDescriptors != 0;
	}

	// Descriptor contents are read by the GPU at execution time, not captured by
	// Draw*. Mirror every active graphics table into a command-list-lived heap,
	// patch that mirror, and temporarily bind it for the draw. This prevents the
	// game's descriptor ring from overwriting the replacement before submission.
	static bool ApplyPending(
		ID3D12Device *device,
		ID3D12GraphicsCommandList *commandList,
		DX12ModIaReplacement *replacement,
		bool executeOriginalDraw)
	{
		const LARGE_INTEGER applyBegin = DX12OuterTimingProbeUtils::Now();
		if (!device || !commandList || !replacement ||
				replacement->drawSrvPatches.empty())
			return false;
		if (replacement->drawSrvApplyState.applied) {
			DX12LogManager::Fields("DX12DrawSrvReplacement",
				"stage=table_patch_fail,reason=already_applied");
			return false;
		}
		if (commandList->GetType() == D3D12_COMMAND_LIST_TYPE_BUNDLE) {
			DX12LogManager::Fields("DX12DrawSrvReplacement",
				"stage=table_patch_fail,reason=bundle_requires_parent_replay");
			return false;
		}
		const LARGE_INTEGER planBegin = DX12OuterTimingProbeUtils::Now();
		// thread_local: see the note above for why this is safe and cheaper.
		static thread_local CommandListBindingState snapshot;
		if (!DX12BindingTracker::SnapshotBindingState(commandList, &snapshot)) {
			DX12LogManager::Fields("DX12DrawSrvReplacement",
				"stage=table_patch_fail,reason=binding_snapshot");
			return false;
		}
		bool needsGraphicsTables = executeOriginalDraw || !replacement->draws.empty();
		bool needsComputeTables = !replacement->dispatches.empty();
		for (const DX12ModIaReplacement::DrawSrvTablePatch &request :
				replacement->drawSrvPatches) {
			if (request.compute)
				needsComputeTables = true;
			else
				needsGraphicsTables = true;
		}
		std::vector<DX12DrawSrvPatchTable> tables;
		UINT totalDescriptors = 0;
		DX12RootSignatureSummary graphicsRootSignature;
		DX12RootSignatureSummary computeRootSignature;
		bool hasGraphicsRootSignature = false;
		bool hasComputeRootSignature = false;
		if (needsGraphicsTables && snapshot.graphicsTableValidCount) {
			if (!snapshot.graphicsRootSignature ||
					!DX12GetRootSignatureSummary(
						snapshot.graphicsRootSignature, &graphicsRootSignature) ||
					!AppendPatchTables(snapshot, graphicsRootSignature, false,
						&tables, &totalDescriptors)) {
				DX12LogManager::Fields("DX12DrawSrvReplacement",
					"stage=table_patch_fail,reason=graphics_root_signature");
				return false;
			}
			hasGraphicsRootSignature = true;
		}
		if (needsComputeTables && snapshot.computeTableValidCount) {
			if (!snapshot.computeRootSignature ||
					!DX12GetRootSignatureSummary(
						snapshot.computeRootSignature, &computeRootSignature) ||
					!AppendPatchTables(snapshot, computeRootSignature, true,
						&tables, &totalDescriptors)) {
				DX12LogManager::Fields("DX12DrawSrvReplacement",
					"stage=table_patch_fail,reason=compute_root_signature");
				return false;
			}
			hasComputeRootSignature = true;
		}
		if (tables.empty() || !totalDescriptors) {
			DX12LogManager::Fields("DX12DrawSrvReplacement",
				"stage=table_patch_fail,reason=table_plan");
			return false;
		}
		for (const DX12ModIaReplacement::DrawSrvTablePatch &request :
				replacement->drawSrvPatches) {
			const DX12RootSignatureSummary *requestRootSignature =
				request.compute ?
					(hasComputeRootSignature ? &computeRootSignature : nullptr) :
					(hasGraphicsRootSignature ? &graphicsRootSignature : nullptr);
			if (!requestRootSignature ||
					!RequestMatchesSrvLayout(request, *requestRootSignature)) {
				DX12LogManager::Fields("DX12DrawSrvReplacement",
					"stage=table_patch_fail,reason=request_layout,bindSpace=%s,"
					"root=%u,range=%u,slot=%u,space=%u,offset=%u",
					request.compute ? "compute" : "graphics",
					request.binding.rootParameterIndex,
					request.binding.rangeIndex,
					request.binding.shaderRegister,
					request.binding.registerSpace,
					request.binding.descriptorOffset);
				return false;
			}
			bool found = false;
			for (const DX12DrawSrvPatchTable &table : tables) {
				if (table.sampler ||
						table.compute != request.compute ||
						table.rootParameterIndex != request.binding.rootParameterIndex ||
						table.gpuStart.ptr != request.binding.tableGpuStart.ptr)
					continue;
				if (request.binding.descriptorOffset >= table.count)
					return false;
				found = true;
				break;
			}
			if (!found || !request.replacementCpu.ptr) {
				DX12LogManager::Fields("DX12DrawSrvReplacement",
					"stage=table_patch_fail,reason=request_table,root=%u,offset=%u",
					request.binding.rootParameterIndex,
					request.binding.descriptorOffset);
				return false;
			}
		}
		DX12OuterTimingProbeUtils::LogCpuStage(
			"DrawSrv",
			"build_mirror_plan",
			0,
			totalDescriptors,
			DX12OuterTimingProbeUtils::ElapsedUs(planBegin));

		D3D12_CPU_DESCRIPTOR_HANDLE tempCpuBase = {};
		D3D12_GPU_DESCRIPTOR_HANDLE tempGpuBase = {};
		UINT tempIncrement = 0;
		ID3D12DescriptorHeap *tempHeap = nullptr;
		const LARGE_INTEGER allocateBegin = DX12OuterTimingProbeUtils::Now();
		if (!EnsurePreSkinDescriptorRingLocked(
				device, commandList, totalDescriptors,
				&tempCpuBase, &tempGpuBase, &tempIncrement, &tempHeap)) {
			DX12LogManager::Fields("DX12DrawSrvReplacement",
				"stage=table_patch_fail,reason=descriptor_ring,count=%u",
				totalDescriptors);
			return false;
		}
		DX12OuterTimingProbeUtils::LogCpuStage(
			"DrawSrv",
			"allocate_mirror_range",
			0,
			totalDescriptors,
			DX12OuterTimingProbeUtils::ElapsedUs(allocateBegin));
		// Persist the only authoritative restore snapshot before issuing any
		// command-list state mutation. Restore never asks Binding State for a
		// second, potentially stale snapshot.
		replacement->drawSrvApplyState.originalBindings = snapshot;
		replacement->drawSrvApplyState.patchHeap = tempHeap;

		DX12InternalReplayScope internalReplay;
		const LARGE_INTEGER copyBegin = DX12OuterTimingProbeUtils::Now();
		// Batch every table-mirror copy into one CopyDescriptors call. The
		// destinations are disjoint slots in the freshly allocated temp ring
		// range and the sources live in other heaps, so merging the ranges is
		// identical to issuing one CopyDescriptorsSimple per table.
		static thread_local std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> tlsCopyDestStarts;
		static thread_local std::vector<UINT> tlsCopyDestSizes;
		static thread_local std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> tlsCopySrcStarts;
		static thread_local std::vector<UINT> tlsCopySrcSizes;
		std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> &copyDestStarts = tlsCopyDestStarts;
		std::vector<UINT> &copyDestSizes = tlsCopyDestSizes;
		std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> &copySrcStarts = tlsCopySrcStarts;
		std::vector<UINT> &copySrcSizes = tlsCopySrcSizes;
		copyDestStarts.clear();
		copyDestSizes.clear();
		copySrcStarts.clear();
		copySrcSizes.clear();
		for (const DX12DrawSrvPatchTable &table : tables) {
			if (table.sampler)
				continue;
			D3D12_CPU_DESCRIPTOR_HANDLE source = {};
			source.ptr = table.cpuStart;
			D3D12_CPU_DESCRIPTOR_HANDLE destination = tempCpuBase;
			destination.ptr += static_cast<SIZE_T>(table.tempOffset) * tempIncrement;
			copyDestStarts.push_back(destination);
			copyDestSizes.push_back(table.count);
			copySrcStarts.push_back(source);
			copySrcSizes.push_back(table.count);
		}
		if (!copyDestStarts.empty()) {
			device->CopyDescriptors(
				static_cast<UINT>(copyDestStarts.size()), copyDestStarts.data(),
				copyDestSizes.data(),
				static_cast<UINT>(copySrcStarts.size()), copySrcStarts.data(),
				copySrcSizes.data(),
				D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		}
		copyDestStarts.clear();
		copyDestSizes.clear();
		copySrcStarts.clear();
		copySrcSizes.clear();
		for (DX12ModIaReplacement::DrawSrvTablePatch &request :
				replacement->drawSrvPatches) {
			for (const DX12DrawSrvPatchTable &table : tables) {
				if (table.sampler ||
						table.compute != request.compute ||
						table.rootParameterIndex != request.binding.rootParameterIndex ||
						table.gpuStart.ptr != request.binding.tableGpuStart.ptr)
					continue;
				D3D12_CPU_DESCRIPTOR_HANDLE destination = tempCpuBase;
				destination.ptr += static_cast<SIZE_T>(
					table.tempOffset + request.binding.descriptorOffset) * tempIncrement;
				copyDestStarts.push_back(destination);
				copyDestSizes.push_back(1);
				copySrcStarts.push_back(request.replacementCpu);
				copySrcSizes.push_back(1);
				request.targetCpu = destination;
				break;
			}
		}
		// Flush after the mirror batch so patched slots still overwrite the
		// mirrored originals, exactly like the sequential calls did.
		if (!copyDestStarts.empty()) {
			device->CopyDescriptors(
				static_cast<UINT>(copyDestStarts.size()), copyDestStarts.data(),
				copyDestSizes.data(),
				static_cast<UINT>(copySrcStarts.size()), copySrcStarts.data(),
				copySrcSizes.data(),
				D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		}
		DX12OuterTimingProbeUtils::LogCpuStage(
			"DrawSrv",
			"copy_mirror_descriptors",
			0,
			totalDescriptors,
			DX12OuterTimingProbeUtils::ElapsedUs(copyBegin));
		replacement->drawSrvApplyState.applied = true;
		const LARGE_INTEGER bindBegin = DX12OuterTimingProbeUtils::Now();
		ID3D12DescriptorHeap *heaps[2] = { tempHeap, snapshot.samplerHeap };
		commandList->SetDescriptorHeaps(snapshot.samplerHeap ? 2 : 1, heaps);
		SyncDescriptorHeaps(commandList, tempHeap, snapshot.samplerHeap);
		for (const DX12DrawSrvPatchTable &table : tables) {
			D3D12_GPU_DESCRIPTOR_HANDLE tableGpu = table.gpuStart;
			if (!table.sampler) {
				tableGpu = tempGpuBase;
				tableGpu.ptr += static_cast<UINT64>(table.tempOffset) * tempIncrement;
			}
			if (table.compute)
				commandList->SetComputeRootDescriptorTable(
					table.rootParameterIndex, tableGpu);
			else
				commandList->SetGraphicsRootDescriptorTable(
					table.rootParameterIndex, tableGpu);
			SyncRootDescriptorTable(
				commandList, table.compute, table.rootParameterIndex, tableGpu);
		}
		DX12OuterTimingProbeUtils::LogCpuStage(
			"DrawSrv",
			"bind_mirror_tables",
			0,
			tables.size(),
			DX12OuterTimingProbeUtils::ElapsedUs(bindBegin));
		DX12OuterTimingProbeUtils::LogCpuStage(
			"DrawSrv",
			"apply_total",
			0,
			totalDescriptors,
			DX12OuterTimingProbeUtils::ElapsedUs(applyBegin));
		return true;
	}

	static bool ApplyConfig(
		ID3D12Device *device,
		ID3D12GraphicsCommandList *commandList,
		const Bunny::TextureOverrideConfig &config,
		const DX12TextureOverrideMatch *match,
		DX12ModIaReplacement *replacement,
		const std::vector<DX12CurrentShaderResourceBinding> *graphicsSrvs = nullptr)
	{
		if (!device || !commandList || !replacement)
			return false;
		// A bundle cannot switch to a private descriptor heap unless its parent
		// direct list binds that exact heap. Defer support to ExecuteBundle replay;
		// do not record uploads or an incompatible SetDescriptorHeaps in a bundle.
		if (commandList->GetType() == D3D12_COMMAND_LIST_TYPE_BUNDLE) {
			DX12LogManager::Fields("DX12DrawSrvReplacement",
				"stage=slot_patch_fail,reason=bundle_requires_parent_replay");
			return false;
		}
		if (config.drawPsSrvResources.empty() &&
		    config.drawVsSrvResources.empty() &&
		    config.drawReplaceThisResource.empty())
			return false;

		const std::vector<DX12CurrentShaderResourceBinding> *bindingsView =
			graphicsSrvs;

		std::vector<DX12DrawSrvPatchRequest> requests;
		for (std::map<uint32_t, std::wstring>::const_iterator it = config.drawPsSrvResources.begin();
		     it != config.drawPsSrvResources.end(); ++it) {
			size_t bindingCount = 0;
			UINT added = bindingsView ?
				DX12DrawSrvReplacementUtils::AddSlotRequestsFromBindings(
					device, commandList, *bindingsView, false, it->first, it->second,
					true, "ps-t", &requests) :
				DX12DrawSrvReplacementUtils::AddSlotRequestsByRegister(
					device, commandList, false, it->first, it->second,
					true, "ps-t", &requests, &bindingCount);
			if (!added) {
				DX12LogManager::Fields("DX12DrawSrvReplacement",
					"stage=slot_patch_fail,source=ps-t,reason=graphics_binding_missing,slot=%u,resource=%S,graphicsCount=%zu",
					it->first, it->second.c_str(),
					bindingsView ? bindingsView->size() : bindingCount);
			}
		}

		for (std::map<uint32_t, std::wstring>::const_iterator it = config.drawVsSrvResources.begin();
		     it != config.drawVsSrvResources.end(); ++it) {
			size_t bindingCount = 0;
			UINT added = bindingsView ?
				DX12DrawSrvReplacementUtils::AddSlotRequestsFromBindings(
					device, commandList, *bindingsView, false, it->first, it->second,
					false, "vs-t", &requests) :
				DX12DrawSrvReplacementUtils::AddSlotRequestsByRegister(
					device, commandList, false, it->first, it->second,
					false, "vs-t", &requests, &bindingCount);
			if (!added) {
				DX12LogManager::Fields("DX12DrawSrvReplacement",
					"stage=slot_patch_fail,source=vs-t,reason=graphics_binding_missing,slot=%u,resource=%S,graphicsCount=%zu",
					it->first, it->second.c_str(),
					bindingsView ? bindingsView->size() : bindingCount);
			}
		}

		if (!config.drawReplaceThisResource.empty()) {
			UINT added = DX12DrawSrvReplacementUtils::AddThisRequestFromMatch(
				device, commandList, match,
				config.drawReplaceThisResource, &requests);
			if (!added) {
				DX12LogManager::Fields("DX12DrawSrvReplacement",
					"stage=slot_patch_fail,source=this,reason=no_matched_target,hash=%08x,resource=%S,graphicsCount=%zu",
					static_cast<uint32_t>(config.hash),
					config.drawReplaceThisResource.c_str(),
					bindingsView ? bindingsView->size() : 0);
			}
		}

		return DX12DrawSrvReplacementUtils::QueueRequests(requests, replacement);
	}

	static bool ApplyCompiledPlan(
		ID3D12Device *device,
		ID3D12GraphicsCommandList *commandList,
		const DX12CompiledTextureOverridePlan &plan,
		const DX12TextureOverrideMatch *match,
		DX12ModIaReplacement *replacement,
		const std::vector<DX12CurrentShaderResourceBinding> *graphicsSrvs = nullptr)
	{
		if (!device || !commandList || !replacement)
			return false;
		if (plan.drawSrvResources.empty() &&
		    plan.drawReplaceThisResource.empty())
			return false;

		const std::vector<DX12CurrentShaderResourceBinding> *bindingsView =
			graphicsSrvs;

		std::vector<DX12DrawSrvPatchRequest> requests;
		for (const DX12CompiledDrawSrvResourceBinding &binding :
				plan.drawSrvResources) {
			const char *source = binding.pixelStage ? "ps-t" : "vs-t";
			size_t bindingCount = 0;
			UINT added = bindingsView ?
				DX12DrawSrvReplacementUtils::AddSlotRequestsFromBindings(
					device, commandList, *bindingsView, false,
					binding.slot, binding.resource,
					binding.pixelStage, source, &requests) :
				DX12DrawSrvReplacementUtils::AddSlotRequestsByRegister(
					device, commandList, false,
					binding.slot, binding.resource,
					binding.pixelStage, source, &requests, &bindingCount);
			if (!added) {
				DX12LogManager::Fields("DX12DrawSrvReplacement",
					"stage=slot_patch_fail,source=%s,reason=graphics_binding_missing,slot=%u,resource=%S,graphicsCount=%zu",
					source,
					binding.slot,
					binding.resource.c_str(),
					bindingsView ? bindingsView->size() : bindingCount);
			}
		}

		if (!plan.drawReplaceThisResource.empty()) {
			UINT added = DX12DrawSrvReplacementUtils::AddThisRequestFromMatch(
				device, commandList, match,
				plan.drawReplaceThisResource, &requests);
			if (!added) {
				DX12LogManager::Fields("DX12DrawSrvReplacement",
					"stage=slot_patch_fail,source=this,reason=no_matched_target,resource=%S,graphicsCount=%zu",
					plan.drawReplaceThisResource.c_str(),
					bindingsView ? bindingsView->size() : 0);
			}
		}

		return DX12DrawSrvReplacementUtils::QueueRequests(requests, replacement);
	}

	static void Restore(
		ID3D12GraphicsCommandList *commandList,
		DX12ModIaReplacement *replacement)
	{
		const LARGE_INTEGER restoreBegin = DX12OuterTimingProbeUtils::Now();
		if (!replacement)
			return;
		DX12ModIaReplacement::DrawSrvApplyState &state =
			replacement->drawSrvApplyState;
		if (!commandList || !state.applied || !state.patchHeap) {
			replacement->drawSrvPatches.clear();
			state = DX12ModIaReplacement::DrawSrvApplyState();
			return;
		}
		const CommandListBindingState &snapshot = state.originalBindings;
		DX12InternalReplayScope internalReplay;
		ID3D12DescriptorHeap *heaps[2] = {};
		UINT heapCount = 0;
		if (snapshot.cbvSrvUavHeap)
			heaps[heapCount++] = snapshot.cbvSrvUavHeap;
		if (snapshot.samplerHeap)
			heaps[heapCount++] = snapshot.samplerHeap;
		commandList->SetDescriptorHeaps(heapCount, heapCount ? heaps : nullptr);
		SyncDescriptorHeaps(
			commandList, snapshot.cbvSrvUavHeap, snapshot.samplerHeap);
		for (UINT root = 0; root < MaxRootParameters; ++root) {
			if (snapshot.graphicsTables[root].valid) {
				commandList->SetGraphicsRootDescriptorTable(
					root, snapshot.graphicsTables[root].baseDescriptor);
				SyncRootDescriptorTable(
					commandList, false, root,
					snapshot.graphicsTables[root].baseDescriptor);
			}
			if (snapshot.computeTables[root].valid) {
				commandList->SetComputeRootDescriptorTable(
					root, snapshot.computeTables[root].baseDescriptor);
				SyncRootDescriptorTable(
					commandList, true, root,
					snapshot.computeTables[root].baseDescriptor);
			}
		}
		DX12OuterTimingProbeUtils::LogCpuStage(
			"DrawSrv",
			"restore_original_tables",
			0,
			static_cast<size_t>(snapshot.graphicsTableValidCount) +
				snapshot.computeTableValidCount,
			DX12OuterTimingProbeUtils::ElapsedUs(restoreBegin));
		replacement->drawSrvPatches.clear();
		state = DX12ModIaReplacement::DrawSrvApplyState();
	}
};

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

static void SyncPreSkinDescriptorHeaps(
	ID3D12GraphicsCommandList *commandList,
	DX12SimulateContext *runtimeState,
	ID3D12DescriptorHeap *cbvSrvUavHeap,
	ID3D12DescriptorHeap *samplerHeap)
{
	if (runtimeState)
	{
		DX12CommandListRuntime::SetDescriptorHeaps(
			runtimeState, cbvSrvUavHeap, samplerHeap);
	}
	else
	{
		DX12CommandListRuntime::SetDescriptorHeaps(
			commandList, cbvSrvUavHeap, samplerHeap);
	}
	ID3D12DescriptorHeap *heaps[2] = {};
	UINT heapCount = 0;
	if (cbvSrvUavHeap)
		heaps[heapCount++] = cbvSrvUavHeap;
	if (samplerHeap)
		heaps[heapCount++] = samplerHeap;
	DX12BindingTracker::SetDescriptorHeaps(
		commandList, heapCount, heapCount ? heaps : nullptr);
}

static void SyncPreSkinComputeRootDescriptorTable(
	ID3D12GraphicsCommandList *commandList,
	DX12SimulateContext *runtimeState,
	UINT rootParameterIndex,
	D3D12_GPU_DESCRIPTOR_HANDLE table)
{
	if (runtimeState)
	{
		DX12CommandListRuntime::RememberRootDescriptorTable(
			runtimeState, false, rootParameterIndex, table);
	}
	else
	{
		DX12CommandListRuntime::RememberRootDescriptorTable(
			commandList, false, rootParameterIndex, table);
	}
	DX12BindingTracker::SetComputeRootDescriptorTable(
		commandList, rootParameterIndex, table);
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

	// Upload CBVs are commonly rewritten in place every frame. Resource identity
	// and GPU VA therefore do not identify their contents; caching this read made
	// later PreSkin dispatches consume the first frame's constants indefinitely.
	bytes->resize(viewBytes);
	D3D12_RANGE readRange = {
		static_cast<SIZE_T>(sourceOffset),
		static_cast<SIZE_T>(sourceOffset + viewBytes)};
	void *sourceMapped = nullptr;
	const HRESULT hr = resource->Map(0, &readRange, &sourceMapped);
	if (SUCCEEDED(hr) && sourceMapped)
	{
		memcpy(bytes->data(),
			   static_cast<const unsigned char *>(sourceMapped) + sourceOffset,
			   viewBytes);
		D3D12_RANGE writtenRange = {};
		resource->Unmap(0, &writtenRange);
	}
	if (hrOut)
		*hrOut = hr;
	if (FAILED(hr) || !sourceMapped)
		return false;
	return true;
}

static UINT64 HashPreSkinCbvBytes(const std::vector<unsigned char> &bytes)
{
	UINT64 hash = 1469598103934665603ull;
	for (unsigned char value : bytes)
	{
		hash ^= value;
		hash *= 1099511628211ull;
	}
	return hash ? hash : 1;
}

static void ReleasePreSkinCbvPatchCacheEntry(
	DX12PreSkinCbvPatchCacheEntry *entry)
{
	if (!entry)
		return;
	if (entry->patchedResource)
		entry->patchedResource->Release();
	if (entry->descriptorHeap)
		entry->descriptorHeap->Release();
	*entry = DX12PreSkinCbvPatchCacheEntry();
}

static void ClearPreSkinCbvPatchCache()
{
	for (auto &item : gPreSkinCbvPatchCache)
		ReleasePreSkinCbvPatchCacheEntry(&item.second);
	gPreSkinCbvPatchCache.clear();
}

static bool PatchPreSkinVertexCountCbvLocked(
	ID3D12Device *device, const DX12CurrentComputeUavBinding &cbv,
	const Bunny::TextureOverrideConfig &config, UINT expectedOriginalVertexCount, UINT overrideVertexCount,
	D3D12_CPU_DESCRIPTOR_HANDLE dst,
	std::vector<ID3D12Resource *> *temporaryResources,
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
	if (!ReadPreSkinCbvBytesLocked(cbv.descriptor.resource, sourceOffset, viewBytes, &bytes, &hr))
	{
		return false;
	}

	std::vector<UINT> patchOffsets;
	patchOffsets.reserve(bytes.size() / sizeof(UINT32));
	UINT32 originalVertexCount = 0;
	if (expectedOriginalVertexCount)
	{
		for (UINT offset = 0; offset + sizeof(UINT32) <= bytes.size(); offset += sizeof(UINT32))
		{
			UINT32 candidate = 0;
			memcpy(&candidate, bytes.data() + offset, sizeof(candidate));
			if (candidate == expectedOriginalVertexCount)
				patchOffsets.push_back(offset);
		}
		if (!patchOffsets.empty())
			originalVertexCount = expectedOriginalVertexCount;
	}
	else if (bytes.size() >= sizeof(UINT32))
	{
		memcpy(&originalVertexCount, bytes.data(), sizeof(originalVertexCount));
		if (originalVertexCount && originalVertexCount != overrideVertexCount)
			patchOffsets.push_back(0);
	}
	if (patchOffsets.empty())
	{
		UINT32 firstDword = 0;
		if (bytes.size() >= sizeof(firstDword))
			memcpy(&firstDword, bytes.data(), sizeof(firstDword));
		return false;
	}
	for (UINT offset : patchOffsets)
		memcpy(bytes.data() + offset, &overrideVertexCount, sizeof(overrideVertexCount));
	const UINT64 patchedContentHash = HashPreSkinCbvBytes(bytes);

	// Patched CBV Buffer
	// UPLOAD buffer + CBV descriptor mod
	// Map/Write/Unmap CPU-only CreateCommittedResource
	{
		DX12PreSkinCbvPatchCacheKey patchKey;
		patchKey.resource = cbv.descriptor.resource;
		patchKey.sourceOffset = sourceOffset;
		patchKey.viewBytes = viewBytes;
		patchKey.overrideVertexCount = overrideVertexCount;
		patchKey.contentHash = patchedContentHash;

		UINT64 reloadGen = DX12ModGetReloadGeneration();
		if (gPreSkinCbvPatchCacheGeneration != reloadGen)
		{
			ClearPreSkinCbvPatchCache();
			gPreSkinCbvPatchCacheGeneration = reloadGen;
		}

		auto cached = gPreSkinCbvPatchCache.find(patchKey);
		if (cached != gPreSkinCbvPatchCache.end() &&
			cached->second.patchedResource && cached->second.cbvCpu.ptr &&
			cached->second.generation == reloadGen &&
			cached->second.patchedBytes == bytes)
		{
			// D3D12 upload memory is read by the GPU at execution time. Rewriting a
			// cached CBV while an earlier command list is in flight lets that earlier
			// PreSkin dispatch observe a later frame's constants. FG and capture make
			// the race much easier to hit by increasing queue depth. The read cache
			// content hash and exact byte comparison guarantee this key uses the
			// same snapshot, so keep the allocation immutable and copy its descriptor.
			device->CopyDescriptorsSimple(1, dst, cached->second.cbvCpu,
										  D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			temporaryResources->push_back(cached->second.patchedResource);
			cached->second.patchedResource->AddRef();
			if (originalVertexCountOut)
				*originalVertexCountOut = originalVertexCount;
			if (overrideVertexCountOut)
				*overrideVertexCountOut = overrideVertexCount;
			return true;
		}
	}

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
	if (FAILED(hr) || !patchedMapped)
	{
		patchedResource->Release();
		return false;
	}
	memcpy(patchedMapped, bytes.data(), viewBytes);
	patchedResource->Unmap(0, nullptr);

	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
	cbvDesc.BufferLocation = patchedResource->GetGPUVirtualAddress();
	cbvDesc.SizeInBytes = viewBytes;
	device->CreateConstantBufferView(&cbvDesc, dst);

	{
		D3D12_DESCRIPTOR_HEAP_DESC patchHeapDesc = {};
		patchHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		patchHeapDesc.NumDescriptors = 1;
		patchHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		ID3D12DescriptorHeap *patchHeap = nullptr;
		HRESULT heapHr = device->CreateDescriptorHeap(&patchHeapDesc, IID_PPV_ARGS(&patchHeap));
		if (SUCCEEDED(heapHr) && patchHeap)
		{
			D3D12_CPU_DESCRIPTOR_HANDLE cachedCpu = patchHeap->GetCPUDescriptorHandleForHeapStart();
			device->CreateConstantBufferView(&cbvDesc, cachedCpu);
			DX12PreSkinCbvPatchCacheEntry entry;
			entry.patchedResource = patchedResource;
			entry.descriptorHeap = patchHeap;
			entry.cbvCpu = cachedCpu;
			entry.generation = DX12ModGetReloadGeneration();
			entry.patchedBytes = bytes;
			patchedResource->AddRef();

			DX12PreSkinCbvPatchCacheKey patchKey;
			patchKey.resource = cbv.descriptor.resource;
			patchKey.sourceOffset = sourceOffset;
			patchKey.viewBytes = viewBytes;
			patchKey.overrideVertexCount = overrideVertexCount;
			patchKey.contentHash = patchedContentHash;
			if (gPreSkinCbvPatchCache.size() > 64)
				ClearPreSkinCbvPatchCache();
			auto old = gPreSkinCbvPatchCache.find(patchKey);
			if (old != gPreSkinCbvPatchCache.end())
			{
				ReleasePreSkinCbvPatchCacheEntry(&old->second);
				gPreSkinCbvPatchCache.erase(old);
			}
			gPreSkinCbvPatchCache[patchKey] = entry;
		}
	}

	temporaryResources->push_back(patchedResource);
	if (originalVertexCountOut)
		*originalVertexCountOut = originalVertexCount;
	if (overrideVertexCountOut)
		*overrideVertexCountOut = overrideVertexCount;

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


static void AppendComputeBindingHashList(
	const std::vector<DX12CurrentComputeUavBinding> &bindings,
	bool srvs, char *text, size_t textSize)
{
	if (!text || textSize == 0)
		return;
	text[0] = '\0';
	size_t used = 0;
	const size_t maxItems = srvs ? 8 : 6;
	for (size_t i = 0; i < bindings.size() && i < maxItems; ++i)
	{
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
	for (size_t i = 0; i < bindings.size() && i < maxItems; ++i)
	{
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

	for (UINT offset = 0; offset + sizeof(UINT32) <= bytes.size(); offset += sizeof(UINT32))
	{
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
static uint64_t HashPreSkinMatchSrvKeysForProbe(
	const std::vector<DX12CurrentComputeUavBinding> &srvs,
	const Bunny::TextureOverrideConfig &config);
static uint64_t MakePreSkinMatchCsInputCacheKey(
	uint64_t csHash, const DX12PreSkinTextureOverrideCandidate &candidate,
	UINT64 uavViewBytes,
	const std::vector<DX12CurrentComputeUavBinding> &uavs,
	const std::vector<DX12CurrentComputeUavBinding> &srvs,
	const std::vector<DX12CurrentComputeUavBinding> &cbvs,
	const std::vector<DX12CurrentRootConstants> &rootConstants)
{
	uint64_t key = HashCombine64(gReloadGeneration, csHash);
	key = HashCombine64(key, candidate.identity);
	key = HashCombine64(key, uavViewBytes);
	// match_cs_t0_hash style input matching only observes the SRV bindings
	// for the configured registers. Hash just those instead of the full
	// binding set: games rewrite unrelated descriptor slots every frame, and
	// a full-binding key invalidates this cache even when the matching SRV
	// inputs are unchanged (observed 42/43 candidate dispatches re-probing
	// every frame). Fall back to the full binding hash when a candidate has
	// no SRV-hash inputs (the pre-skin path always has them, this is
	// defensive for the shared log-dedup call site).
	if (candidate.config && !candidate.config->preSkinMatchCsSrvHashes.empty())
		key = HashCombine64(key, HashPreSkinMatchSrvKeysForProbe(srvs, *candidate.config));
	else {
		key = HashCombine64(key, HashComputeBindingListForProbe(uavs, false));
		key = HashCombine64(key, HashComputeBindingListForProbe(srvs, true));
		key = HashCombine64(key, HashComputeCbvListForProbe(cbvs));
		key = HashCombine64(key, HashComputeRootConstantsForProbe(rootConstants));
	}
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
		gPreSkinMatchCsProbeKeys.insert(probeKey).second)
	{
		++gPreSkinMatchCsProbeLogCount;
		emit = true;
	}
	ReleaseSRWLockExclusive(&gPreSkinMatchCsProbeLogLock);
	if (!emit)
		return;
}
static void AppendComputeCbvList(
	const std::vector<DX12CurrentComputeUavBinding> &cbvs, char *text, size_t textSize)
{
	if (!text || textSize == 0)
		return;
	text[0] = '\0';
	size_t used = 0;
	const size_t maxItems = 8;
	for (size_t i = 0; i < cbvs.size() && i < maxItems; ++i)
	{
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
	for (size_t i = 0; i < rootConstants.size() && i < maxRoots; ++i)
	{
		const DX12CurrentRootConstants &root = rootConstants[i];
		uint64_t valueHash = 14695981039346656037ull;
		UINT validCount = 0;
		for (UINT j = 0; j < 64; ++j)
		{
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
	for (size_t i = 0; i < cbvs.size() && i < maxItems; ++i)
	{
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
	for (size_t i = 0; i < rootConstants.size() && i < maxRoots; ++i)
	{
		const DX12CurrentRootConstants &root = rootConstants[i];
		key = HashCombine64(key, root.rootParameterIndex);
		for (UINT j = 0; j < 64; ++j)
		{
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
	for (const DX12CurrentComputeUavBinding &srv : srvs)
	{
		if (!srv.hasDescriptor || srv.descriptor.kind != "SRV" ||
			srv.descriptor.viewDimension != D3D12_SRV_DIMENSION_BUFFER)
			continue;
		if (srv.shaderRegister != shaderRegister)
			continue;
		DX12PreSkinSrvHashResult hash = {};
		hash.descriptorHash = HashComputeBufferBinding(srv);
		hash.sourceOffset = srv.descriptor.resourceOffset ? srv.descriptor.resourceOffset : srv.descriptor.bufferViewOffset;
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

static uint64_t HashPreSkinMatchSrvKeysForProbe(
	const std::vector<DX12CurrentComputeUavBinding> &srvs,
	const Bunny::TextureOverrideConfig &config)
{
	// Key on the exact bindings MatchComputeInputSrvsTextureOverrideLocked
	// observes: the SRV at each configured register. A missing binding makes
	// the match fail, so that must invalidate the key too. sourceOffset /
	// viewBytes / stride are the same fields the hash probe reports; they are
	// subsumed by descriptorHash but kept as a cheap collision guard.
	uint64_t key = 0x5a3d9f21c7e4b608ull;
	for (const auto &expected : config.preSkinMatchCsSrvHashes)
	{
		key = HashCombine64(key, expected.first);
		key = HashCombine64(key, expected.second);
		const DX12CurrentComputeUavBinding *binding = nullptr;
		DX12PreSkinSrvHashResult actual = {};
		if (!FindComputeSrvByRegister(srvs, expected.first, &binding, &actual))
		{
			key = HashCombine64(key, 0xffffffffu);
			continue;
		}
		key = HashCombine64(key, actual.descriptorHash);
		key = HashCombine64(key, actual.sourceOffset);
		key = HashCombine64(key, actual.viewBytes);
		key = HashCombine64(key, actual.stride);
	}
	return key;
}

static bool MatchComputeInputSrvsTextureOverrideLocked(
	const std::vector<DX12CurrentComputeUavBinding> &srvs,
	const Bunny::TextureOverrideConfig &config)
{
	if (config.preSkinMatchCsSrvHashes.empty())
		return false;
	for (const auto &expected : config.preSkinMatchCsSrvHashes)
	{
		const DX12CurrentComputeUavBinding *binding = nullptr;
		DX12PreSkinSrvHashResult actual = {};
		if (!FindComputeSrvByRegister(srvs, expected.first, &binding, &actual))
		{
						return false;
		}
		const bool matched = actual.descriptorHash == expected.second;
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

	for (const auto &expected : config.preSkinMatchCsSrvHashes)
	{
		const DX12CurrentComputeUavBinding *binding = nullptr;
		DX12PreSkinSrvHashResult actual = {};
		if (!FindComputeSrvByRegister(srvs, expected.first, &binding, &actual))
			return false;
		const bool matched = actual.descriptorHash == expected.second;
				if (!matched)
			return false;
	}

	for (const auto &replacement : config.preSkinCsSrvResources)
	{
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
	static thread_local UINT64 tlsInputCacheReloadGeneration = 0;
	static thread_local std::unordered_map<uint64_t, bool> tlsInputCache;
	const UINT64 reloadGeneration = DX12ModGetReloadGenerationFast();
	if (tlsInputCacheReloadGeneration != reloadGeneration)
	{
		tlsInputCache.clear();
		tlsInputCacheReloadGeneration = reloadGeneration;
	}

	for (size_t index : indexed->second)
	{
		if (index >= gPreSkinTextureOverrides.size())
			continue;
		const DX12PreSkinTextureOverrideCandidate &candidate =
			gPreSkinTextureOverrides[index];
		const Bunny::TextureOverrideConfig *config = candidate.config;
		if (!config || !IsComputePreSkinTextureOverride(*config) ||
			GetPreSkinCsHash(*config) != csHash)
			continue;
		const uint64_t inputKey = MakePreSkinMatchCsInputCacheKey(
			csHash, candidate, 0, uavs, srvs, cbvs, rootConstants);
		bool inputMatched = false;
		auto cachedInput = tlsInputCache.find(inputKey);
		if (cachedInput != tlsInputCache.end())
		{
			inputMatched = cachedInput->second;
		}
		else
		{
			inputMatched = MatchComputeInputSrvsTextureOverrideLocked(srvs, *config);
			if (tlsInputCache.size() > 4096)
				tlsInputCache.clear();
			tlsInputCache[inputKey] = inputMatched;
		}
		LogPreSkinMatchCsProbeLimited(csHash, candidate, 0, inputMatched, uavs, srvs, cbvs, rootConstants);
		if (!inputMatched)
			continue;
		if (match)
		{
			match->config = config;
			match->hash = candidate.hash;
			BuildPreSkinSrvReplacementBindingsTextureOverrideLocked(
				srvs, *config, &match->srvReplacements);
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

static size_t FindPreSkinTextureOverrideCandidateIndex(
	const Bunny::TextureOverrideConfig *config)
{
	if (!config)
		return static_cast<size_t>(-1);
	for (size_t i = 0; i < gPreSkinTextureOverrides.size(); ++i)
	{
		if (gPreSkinTextureOverrides[i].config == config)
			return i;
	}
	return static_cast<size_t>(-1);
}

// Sticky-positive hysteresis for MatchCs pre-skin replacements.
//
// The game rewrites its descriptor tables every frame (observed ~73% slot
// churn), so the SRV view hash the probe matches against can flip between
// the original descriptor and an in-flight/alternate copy from frame to
// frame. Without hysteresis the replacement applies intermittently and the
// model visibly flickers between the replaced and original skinning. Once a
// dispatch has matched, remember its output UAV for a short window and keep
// treating it as a match even when the SRV hash probe fails. The key
// includes the output resource pointer + view geometry, so a different mesh
// that happens to share the CS hash is not affected.
struct DX12PreSkinStickyMatchEntry
{
	volatile UINT64 reloadGeneration = 0;
	volatile UINT64 uavKey = 0;
	volatile uint64_t csHash = 0;
	volatile size_t candidateIndex = static_cast<size_t>(-1);
	volatile uint32_t hash = 0;
	volatile LONG present = 0;
	volatile LONG occupied = 0;
};

static constexpr LONG kPreSkinStickyMatchFrames = 8;
static constexpr size_t kPreSkinStickyMatchSlots = 64;
static DX12PreSkinStickyMatchEntry gPreSkinStickyMatch[kPreSkinStickyMatchSlots] = {};

static void ClearPreSkinStickyMatchCache()
{
	for (size_t i = 0; i < kPreSkinStickyMatchSlots; ++i)
		InterlockedExchange(&gPreSkinStickyMatch[i].occupied, 0);
}

static size_t PreSkinStickyMatchSlot(uint64_t csHash, UINT64 uavKey)
{
	uint64_t hash = HashCombine64(csHash, uavKey);
	hash ^= hash >> 33;
	hash *= 0xff51afd7ed558ccdull;
	hash ^= hash >> 33;
	return static_cast<size_t>(hash) & (kPreSkinStickyMatchSlots - 1);
}

static UINT64 MakePreSkinStickyMatchUavKey(
	const std::vector<DX12CurrentComputeUavBinding> &uavs)
{
	for (auto it = uavs.rbegin(); it != uavs.rend(); ++it)
	{
		if (!IsPreSkinMatchCsUavCandidate(*it))
			continue;
		const DX12CurrentComputeUavBinding &uav = *it;
		UINT64 key = uav.rootParameterIndex;
		key = HashCombine64(key, uav.rangeIndex);
		key = HashCombine64(key, uav.shaderRegister);
		key = HashCombine64(key, uav.registerSpace);
		key = HashCombine64(key, uav.descriptorOffset);
		key = HashCombine64(key, reinterpret_cast<uintptr_t>(uav.descriptor.resource));
		key = HashCombine64(key, DescriptorViewSize(uav.descriptor));
		key = HashCombine64(key, uav.descriptor.structureByteStride);
		return key;
	}
	return 0;
}

static void StorePreSkinStickyMatch(
	uint64_t csHash, UINT64 uavKey, size_t candidateIndex, uint32_t hash)
{
	if (!csHash || !uavKey || candidateIndex >= gPreSkinTextureOverrides.size())
		return;
	DX12PreSkinStickyMatchEntry &entry =
		gPreSkinStickyMatch[PreSkinStickyMatchSlot(csHash, uavKey)];
	entry.reloadGeneration = DX12ModGetReloadGenerationFast();
	entry.uavKey = uavKey;
	entry.csHash = csHash;
	entry.candidateIndex = candidateIndex;
	entry.hash = hash;
	entry.present = DX12RuntimeState::GetPresentCount();
	InterlockedExchange(&entry.occupied, 1);
}

static bool TryLoadPreSkinStickyMatch(
	uint64_t csHash, const std::vector<DX12CurrentComputeUavBinding> &uavs,
	DX12UavHashTextureOverrideMatch *match)
{
	const UINT64 uavKey = MakePreSkinStickyMatchUavKey(uavs);
	if (!uavKey)
		return false;
	DX12PreSkinStickyMatchEntry &entry =
		gPreSkinStickyMatch[PreSkinStickyMatchSlot(csHash, uavKey)];
	if (InterlockedCompareExchange(&entry.occupied, 0, 0) == 0)
		return false;
	if (entry.reloadGeneration != DX12ModGetReloadGenerationFast() ||
		entry.csHash != csHash || entry.uavKey != uavKey)
		return false;
	if (DX12RuntimeState::GetPresentCount() - entry.present >
		kPreSkinStickyMatchFrames)
		return false;
	if (entry.candidateIndex >= gPreSkinTextureOverrides.size())
		return false;
	const DX12PreSkinTextureOverrideCandidate &candidate =
		gPreSkinTextureOverrides[entry.candidateIndex];
	const Bunny::TextureOverrideConfig *config = candidate.config;
	if (!config || !IsComputePreSkinTextureOverride(*config) ||
		GetPreSkinCsHash(*config) != csHash)
		return false;
	if (match)
	{
		match->config = config;
		match->hash = entry.hash;
		match->resourceName = candidate.resourceName;
	}
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
	const bool freshMatch = FindMatchCsTextureOverrideLocked(
			csHash, uavs, srvs, cbvs, rootConstants, &candidateMatch);
	if (!freshMatch && !TryLoadPreSkinStickyMatch(csHash, uavs, &candidateMatch))
		return false;

	const DX12CurrentComputeUavBinding *chosenBinding = nullptr;
	for (auto it = uavs.rbegin(); it != uavs.rend(); ++it)
	{
		if (IsPreSkinMatchCsUavCandidate(*it))
		{
			chosenBinding = &(*it);
			break;
		}
	}
	if (!chosenBinding)
	{
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
	// Remember the positive match for sticky hysteresis so the replacement
	// stays applied when the SRV hash probe flickers across frames.
	StorePreSkinStickyMatch(
		csHash,
		MakePreSkinStickyMatchUavKey(uavs),
		FindPreSkinTextureOverrideCandidateIndex(candidateMatch.config),
		candidateMatch.hash);
	return true;
}

static UINT FindPreSkinUavElementStrideLocked(
	const DX12LoadedResource &resource,
	const std::unordered_set<std::wstring> &activePreSkinSections)
{
	for (const DX12VertexLimitRaiseConfig &vlr : gVertexLimitRaiseConfigs)
	{
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
	for (auto it = uavs.rbegin(); it != uavs.rend(); ++it)
	{
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

class DX12PreSkinDescriptorTableCloneCache
{
public:
	ID3D12GraphicsCommandList *commandList = nullptr;
	UINT64 reloadGeneration = 0;
	UINT64 bindingFingerprint = 0;
	uint32_t triggerHash = 0;
	ID3D12DescriptorHeap *heap = nullptr;
	std::vector<DX12PreSkinReplacementState::TableRestore> tableRestores;
	std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> patchedTables;

	bool Matches(
		ID3D12GraphicsCommandList *candidateCommandList,
		UINT64 candidateReloadGeneration,
		UINT64 candidateBindingFingerprint,
		uint32_t candidateTriggerHash) const
	{
		return heap && commandList == candidateCommandList &&
			   reloadGeneration == candidateReloadGeneration &&
			   bindingFingerprint == candidateBindingFingerprint &&
			   triggerHash == candidateTriggerHash &&
			   tableRestores.size() == patchedTables.size();
	}

	void Clear()
	{
		if (heap)
			heap->Release();
		commandList = nullptr;
		reloadGeneration = 0;
		bindingFingerprint = 0;
		triggerHash = 0;
		heap = nullptr;
		tableRestores.clear();
		patchedTables.clear();
	}
};

class DX12PreSkinPersistentDescriptorPatchKey
{
public:
	UINT64 reloadGeneration = 0;
	UINT64 bindingFingerprint = 0;
	uint32_t triggerHash = 0;

	bool operator==(const DX12PreSkinPersistentDescriptorPatchKey &rhs) const
	{
		return reloadGeneration == rhs.reloadGeneration &&
			   bindingFingerprint == rhs.bindingFingerprint &&
			   triggerHash == rhs.triggerHash;
	}
};

class DX12PreSkinPersistentDescriptorPatchKeyHasher
{
public:
	size_t operator()(const DX12PreSkinPersistentDescriptorPatchKey &key) const
	{
		UINT64 hash = key.reloadGeneration;
		hash = HashCombine64(hash, key.bindingFingerprint);
		hash = HashCombine64(hash, key.triggerHash);
		return static_cast<size_t>(hash);
	}
};

class DX12PreSkinPersistentDescriptorPatchEntry
{
public:
	ID3D12DescriptorHeap *heap = nullptr;
	std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> patchedTables;

	void Clear()
	{
		if (heap)
			heap->Release();
		heap = nullptr;
		patchedTables.clear();
	}
};

static SRWLOCK gPreSkinPersistentDescriptorPatchLock = SRWLOCK_INIT;
static std::unordered_map<
	DX12PreSkinPersistentDescriptorPatchKey,
	DX12PreSkinPersistentDescriptorPatchEntry,
	DX12PreSkinPersistentDescriptorPatchKeyHasher>
	gPreSkinPersistentDescriptorPatchCache;
static UINT64 gPreSkinPersistentDescriptorPatchCacheGeneration = 0;
static constexpr size_t MaxPreSkinPersistentDescriptorPatchEntries = 32;

static void ClearPreSkinPersistentDescriptorPatchCacheLocked()
{
	for (auto &item : gPreSkinPersistentDescriptorPatchCache)
		item.second.Clear();
	gPreSkinPersistentDescriptorPatchCache.clear();
}

static bool TryGetPersistentPreSkinDescriptorPatchLocked(
	UINT64 reloadGeneration,
	UINT64 bindingFingerprint,
	uint32_t triggerHash,
	size_t tableCount,
	ID3D12DescriptorHeap **heapOut,
	std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> *patchedTablesOut)
{
	if (heapOut)
		*heapOut = nullptr;
	if (patchedTablesOut)
		patchedTablesOut->clear();
	if (!bindingFingerprint || !heapOut || !patchedTablesOut)
		return false;

	AcquireSRWLockShared(&gPreSkinPersistentDescriptorPatchLock);
	const bool generationMatches =
		gPreSkinPersistentDescriptorPatchCacheGeneration == reloadGeneration;
	bool found = false;
	if (generationMatches)
	{
		DX12PreSkinPersistentDescriptorPatchKey key;
		key.reloadGeneration = reloadGeneration;
		key.bindingFingerprint = bindingFingerprint;
		key.triggerHash = triggerHash;
		auto it = gPreSkinPersistentDescriptorPatchCache.find(key);
		if (it != gPreSkinPersistentDescriptorPatchCache.end() &&
			it->second.heap &&
			it->second.patchedTables.size() == tableCount)
		{
			it->second.heap->AddRef();
			*heapOut = it->second.heap;
			*patchedTablesOut = it->second.patchedTables;
			found = true;
		}
	}
	ReleaseSRWLockShared(&gPreSkinPersistentDescriptorPatchLock);
	if (found)
		return true;

	if (!generationMatches)
	{
		AcquireSRWLockExclusive(&gPreSkinPersistentDescriptorPatchLock);
		if (gPreSkinPersistentDescriptorPatchCacheGeneration != reloadGeneration)
		{
			ClearPreSkinPersistentDescriptorPatchCacheLocked();
			gPreSkinPersistentDescriptorPatchCacheGeneration = reloadGeneration;
		}
		ReleaseSRWLockExclusive(&gPreSkinPersistentDescriptorPatchLock);
	}
	return false;
}

static void StorePersistentPreSkinDescriptorPatchLocked(
	UINT64 reloadGeneration,
	UINT64 bindingFingerprint,
	uint32_t triggerHash,
	ID3D12DescriptorHeap *heap,
	const std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> &patchedTables)
{
	if (!heap || !bindingFingerprint || patchedTables.empty())
		return;

	AcquireSRWLockExclusive(&gPreSkinPersistentDescriptorPatchLock);
	if (gPreSkinPersistentDescriptorPatchCacheGeneration != reloadGeneration)
	{
		ClearPreSkinPersistentDescriptorPatchCacheLocked();
		gPreSkinPersistentDescriptorPatchCacheGeneration = reloadGeneration;
	}
	if (gPreSkinPersistentDescriptorPatchCache.size() >=
		MaxPreSkinPersistentDescriptorPatchEntries)
	{
		ClearPreSkinPersistentDescriptorPatchCacheLocked();
	}
	DX12PreSkinPersistentDescriptorPatchKey key;
	key.reloadGeneration = reloadGeneration;
	key.bindingFingerprint = bindingFingerprint;
	key.triggerHash = triggerHash;
	DX12PreSkinPersistentDescriptorPatchEntry &entry =
		gPreSkinPersistentDescriptorPatchCache[key];
	entry.Clear();
	heap->AddRef();
	entry.heap = heap;
	entry.patchedTables = patchedTables;
	ReleaseSRWLockExclusive(&gPreSkinPersistentDescriptorPatchLock);
}

static bool TryReuseDeferredPreSkinDescriptorTable(
	ID3D12GraphicsCommandList *commandList,
	uint64_t computeShaderHash,
	UINT64 bindingFingerprint,
	uint32_t triggerHash,
	bool *hasPendingState)
{
	if (hasPendingState)
		*hasPendingState = false;
	if (!commandList || !bindingFingerprint)
		return false;
	// Read-only probe: the flag is published with InterlockedExchange under
	// gPreSkinLock together with the map entry, and a same-thread reader always
	// observes its own writes in program order. A stale cross-thread zero only
	// skips a restore for a command list this thread does not own, which is
	// correct, so a plain volatile load (atomic on x86 for aligned 32-bit)
	// avoids a LOCK CMPXCHG on every hot-path call.
	if (gHasActivePreSkinReplacement == 0)
		return false;
	AcquireSRWLockShared(&gPreSkinLock);
	std::unordered_map<ID3D12GraphicsCommandList *, DX12PreSkinReplacementState>::const_iterator
		it = gActivePreSkinReplacements.find(commandList);
	if (it == gActivePreSkinReplacements.end())
	{
		ReleaseSRWLockShared(&gPreSkinLock);
		return false;
	}
	const DX12PreSkinReplacementState &state = it->second;
	const bool reusable = state.active && state.patchHeap &&
						  state.descriptorRestores.empty() && state.temporaryResources.empty() &&
						  state.computeShaderHash == computeShaderHash &&
						  state.bindingFingerprint == bindingFingerprint &&
						  state.triggerHash == triggerHash;
	if (hasPendingState)
		*hasPendingState = true;
	ReleaseSRWLockShared(&gPreSkinLock);
	return reusable;
}

bool DX12ModCanDeferPreSkinningUavReplacement(
	ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return false;
	// Plain volatile probe, see DX12ModIsPreSkinReplacementReusable for the
	// ordering argument.
	if (gHasActivePreSkinReplacement == 0)
		return false;
	AcquireSRWLockShared(&gPreSkinLock);
	std::unordered_map<ID3D12GraphicsCommandList *, DX12PreSkinReplacementState>::const_iterator
		it = gActivePreSkinReplacements.find(commandList);
	const bool canDefer = it != gActivePreSkinReplacements.end() &&
						  it->second.active && it->second.patchHeap &&
						  it->second.descriptorRestores.empty() &&
						  it->second.temporaryResources.empty() &&
						  it->second.bindingFingerprint != 0;
	ReleaseSRWLockShared(&gPreSkinLock);
	return canDefer;
}

bool DX12ModHasActivePreSkinningUavReplacement()
{
	// Plain volatile probe, see DX12ModIsPreSkinReplacementReusable for the
	// ordering argument. This is called on every graphics binding hook and
	// draw; a LOCK CMPXCHG here costs measurable cycles per call.
	return gHasActivePreSkinReplacement != 0;
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
	UINT *originalVertexCountOut, UINT *overrideVertexCountOut,
	UINT64 bindingFingerprint, uint64_t computeShaderHash)
{
	if (!device || !commandList || !uavBinding.hasDescriptor || !uavBinding.tableGpuStart.ptr)
		return false;
	DX12SimulateContext *runtimeState =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	auto logStage = [&config, section, triggerHash](const char *stage, UINT tables = 0, UINT descriptors = 0)
	{
		(void)stage;
		(void)tables;
		(void)descriptors;
	};
	logStage("enter");

	class TableCopy

	{

	public:
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
	auto addTable = [&tables](const DX12CurrentComputeUavBinding &binding)
	{
		if (!binding.hasDescriptor || binding.rootDescriptor || !binding.tableCpuStart ||
			!binding.tableGpuStart.ptr || !binding.descriptorIncrement || !binding.tableHeap)
			return;
		const UINT needed = (std::max)(binding.tableCopyCount ? binding.tableCopyCount : 1,
									   binding.descriptorOffset + 1);
		for (TableCopy &table : tables)
		{
			if (table.root == binding.rootParameterIndex &&
				table.cpuStart == binding.tableCpuStart &&
				table.gpuStart.ptr == binding.tableGpuStart.ptr)
			{
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
	if (tables.empty())
	{
		logStage("no_tables");
		return false;
	}

	if (srvs.empty())
	{
			}
	(void)rootConstants;

	const UINT64 abortCacheKey = MakePreSkinDescriptorPatchAbortCacheKey(
		config, srvReplacements, srvs, cbvs);
	if (gPreSkinDescriptorPatchAbortCache.find(abortCacheKey) !=
		gPreSkinDescriptorPatchAbortCache.end())
	{
		logStage("abort_cache_hit", 0, 0);
		return false;
	}

	UINT originalVertexCount = 0;
	UINT overrideVertexCount = 0;
	bool requiresCountPatch = false;
	bool canPatchCount = false;
	for (const DX12CurrentComputeUavBinding &srv : srvs)
	{
		const DX12PreSkinSrvReplacementBinding *replacementBinding = nullptr;
		for (const DX12PreSkinSrvReplacementBinding &binding : srvReplacements)
		{
			if (binding.shaderRegister == srv.shaderRegister)
			{
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
			srv.descriptor.structureByteStride ? srv.descriptor.structureByteStride : (resource ? resource->stride : 0);
		if (!resource || !elementStride)
			continue;

		const UINT srvOriginalVertexCount = static_cast<UINT>(
			(std::min)(srvByteWidth / elementStride, static_cast<UINT64>(UINT_MAX)));
		const UINT srvOverrideVertexCount = static_cast<UINT>(
			(std::min)(resource->byteWidth / elementStride, static_cast<UINT64>(UINT_MAX)));
		if (srvOriginalVertexCount && srvOverrideVertexCount)
		{
			if (!originalVertexCount || srvOriginalVertexCount < originalVertexCount)
				originalVertexCount = srvOriginalVertexCount;
			if (!overrideVertexCount || srvOverrideVertexCount < overrideVertexCount)
				overrideVertexCount = srvOverrideVertexCount;
		}
	}
	if (originalVertexCount && overrideVertexCount && originalVertexCount != overrideVertexCount)
	{
		requiresCountPatch = true;
		for (const DX12CurrentComputeUavBinding &cbv : cbvs)
		{
			if (CanPatchPreSkinVertexCountCbvLocked(cbv, originalVertexCount))
			{
				canPatchCount = true;
				break;
			}
		}
		if (!canPatchCount)
		{
			StorePreSkinDescriptorPatchAbort(abortCacheKey);
			return false;
		}
	}
	bool hasPendingState = false;
	if (!requiresCountPatch && TryReuseDeferredPreSkinDescriptorTable(
								   commandList, computeShaderHash, bindingFingerprint, triggerHash,
								   &hasPendingState))
	{
		if (originalVertexCountOut)
			*originalVertexCountOut = originalVertexCount;
		if (overrideVertexCountOut)
			*overrideVertexCountOut = overrideVertexCount;
		return true;
	}
	if (hasPendingState)
		DX12ModRestorePreSkinningUavReplacement(commandList);
	static thread_local DX12PreSkinDescriptorTableCloneCache tlsCloneCache;
	const UINT64 reloadGeneration = DX12ModGetReloadGenerationFast();
	{
		ID3D12DescriptorHeap *persistentHeap = nullptr;
		std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> persistentPatchedTables;
		if (!requiresCountPatch && bindingFingerprint &&
			TryGetPersistentPreSkinDescriptorPatchLocked(
				reloadGeneration,
				bindingFingerprint,
				triggerHash,
				tables.size(),
				&persistentHeap,
				&persistentPatchedTables))
		{
			ID3D12DescriptorHeap *restoreCbvSrvUavHeap = nullptr;
			ID3D12DescriptorHeap *restoreSamplerHeap = nullptr;
			DX12BindingTracker::GetCurrentDescriptorHeaps(
				commandList, &restoreCbvSrvUavHeap, &restoreSamplerHeap);
			if (restoreCbvSrvUavHeap != persistentHeap)
			{
				ID3D12DescriptorHeap *heaps[2] = {
					persistentHeap, restoreSamplerHeap};
				commandList->SetDescriptorHeaps(restoreSamplerHeap ? 2 : 1, heaps);
				SyncPreSkinDescriptorHeaps(
					commandList, runtimeState, persistentHeap, restoreSamplerHeap);
			}
			DX12PreSkinReplacementState state;
			state.active = true;
			state.computeShaderHash = computeShaderHash;
			state.bindingFingerprint = bindingFingerprint;
			state.triggerHash = triggerHash;
			state.rootParameterIndex = uavBinding.rootParameterIndex;
			state.originalTable = uavBinding.tableGpuStart;
			state.originalHeap = uavBinding.tableHeap;
			state.resource = uavBinding.descriptor.resource;
			state.byteWidth = DescriptorViewSize(uavBinding.descriptor);
			state.stride = uavBinding.descriptor.structureByteStride;
			state.hasActiveTextureOverride =
				state.resource && state.byteWidth && state.stride;
			if (state.hasActiveTextureOverride)
			{
				state.activeConfig = config;
				state.activeProducer = producer;
				state.resource->AddRef();
			}
			state.patchHeap = persistentHeap;
			state.restoreCbvSrvUavHeap = restoreCbvSrvUavHeap;
			state.restoreSamplerHeap = restoreSamplerHeap;
			for (size_t index = 0; index < tables.size() &&
								   index < persistentPatchedTables.size();
				 ++index)
			{
				commandList->SetComputeRootDescriptorTable(
					tables[index].root,
					persistentPatchedTables[index]);
				SyncPreSkinComputeRootDescriptorTable(
					commandList, runtimeState,
					tables[index].root,
					persistentPatchedTables[index]);
				DX12PreSkinReplacementState::TableRestore restore;
				restore.rootParameterIndex = tables[index].root;
				restore.originalTable = tables[index].gpuStart;
				state.tableRestores.push_back(restore);
			}
			RetainPreSkinDescriptorHeapForCommandList(commandList, persistentHeap);
			persistentHeap->Release();
			if (originalVertexCountOut)
				*originalVertexCountOut = originalVertexCount;
			if (overrideVertexCountOut)
				*overrideVertexCountOut = overrideVertexCount;
			AcquireSRWLockExclusive(&gPreSkinLock);
			gActivePreSkinReplacements[commandList] = std::move(state);
			InterlockedExchange(&gHasActivePreSkinReplacement, 1);
			ReleaseSRWLockExclusive(&gPreSkinLock);
			BumpPreSkinReplacementMutationGeneration();
			return true;
		}
	}
	if (!requiresCountPatch && bindingFingerprint && tlsCloneCache.Matches(commandList, reloadGeneration, bindingFingerprint, triggerHash))
	{
		ID3D12DescriptorHeap *restoreCbvSrvUavHeap = nullptr;
		ID3D12DescriptorHeap *restoreSamplerHeap = nullptr;
		DX12BindingTracker::GetCurrentDescriptorHeaps(
			commandList, &restoreCbvSrvUavHeap, &restoreSamplerHeap);
		if (restoreCbvSrvUavHeap != tlsCloneCache.heap)
		{
			ID3D12DescriptorHeap *heaps[2] = {
				tlsCloneCache.heap, restoreSamplerHeap};
			commandList->SetDescriptorHeaps(restoreSamplerHeap ? 2 : 1, heaps);
			SyncPreSkinDescriptorHeaps(
				commandList, runtimeState, tlsCloneCache.heap, restoreSamplerHeap);
		}
		DX12PreSkinReplacementState state;
		state.active = true;
		state.computeShaderHash = computeShaderHash;
		state.bindingFingerprint = bindingFingerprint;
		state.triggerHash = triggerHash;
		state.rootParameterIndex = uavBinding.rootParameterIndex;
		state.originalTable = uavBinding.tableGpuStart;
		state.originalHeap = uavBinding.tableHeap;
		state.resource = uavBinding.descriptor.resource;
		state.byteWidth = DescriptorViewSize(uavBinding.descriptor);
		state.stride = uavBinding.descriptor.structureByteStride;
		state.hasActiveTextureOverride =
			state.resource && state.byteWidth && state.stride;
		if (state.hasActiveTextureOverride)
		{
			state.activeConfig = config;
			state.activeProducer = producer;
			state.resource->AddRef();
		}
		state.patchHeap = tlsCloneCache.heap;
		state.restoreCbvSrvUavHeap = restoreCbvSrvUavHeap;
		state.restoreSamplerHeap = restoreSamplerHeap;
		state.tableRestores = tlsCloneCache.tableRestores;
		for (size_t index = 0; index < state.tableRestores.size(); ++index)
		{
			commandList->SetComputeRootDescriptorTable(
				state.tableRestores[index].rootParameterIndex,
				tlsCloneCache.patchedTables[index]);
			SyncPreSkinComputeRootDescriptorTable(
				commandList, runtimeState,
				state.tableRestores[index].rootParameterIndex,
				tlsCloneCache.patchedTables[index]);
		}
		RetainPreSkinDescriptorHeapForCommandList(
			commandList, tlsCloneCache.heap);
		if (originalVertexCountOut)
			*originalVertexCountOut = originalVertexCount;
		if (overrideVertexCountOut)
			*overrideVertexCountOut = overrideVertexCount;
		AcquireSRWLockExclusive(&gPreSkinLock);
		gActivePreSkinReplacements[commandList] = std::move(state);
		InterlockedExchange(&gHasActivePreSkinReplacement, 1);
		ReleaseSRWLockExclusive(&gPreSkinLock);
		BumpPreSkinReplacementMutationGeneration();
		return true;
	}

	UINT totalDescriptors = 0;
	for (TableCopy &table : tables)
	{
		table.tempOffset = totalDescriptors;
		totalDescriptors += table.count;
	}
	logStage("tables_built", static_cast<UINT>(tables.size()), totalDescriptors);
	if (!totalDescriptors)
	{
		logStage("no_descriptors", static_cast<UINT>(tables.size()), totalDescriptors);
		return false;
	}

	D3D12_CPU_DESCRIPTOR_HANDLE tempCpuBase = {};
	D3D12_GPU_DESCRIPTOR_HANDLE tempGpuBase = {};
	UINT tempIncrement = 0;
	ID3D12DescriptorHeap *tempHeap = nullptr;
	DX12_HOTPATH_TIME_BEGIN(PreSkinRingAlloc);
	if (!EnsurePreSkinDescriptorRingLocked(
			device, commandList, totalDescriptors, &tempCpuBase, &tempGpuBase, &tempIncrement,
			&tempHeap))
	{
		logStage("descriptor_ring_failed", static_cast<UINT>(tables.size()), totalDescriptors);
		return false;
	}
	logStage("ring_allocated", static_cast<UINT>(tables.size()), totalDescriptors);
	DX12_HOTPATH_TIME_END(PreSkinRingAlloc);
	// Batch all table copies into one CopyDescriptors call: destinations are
	// disjoint slots in the fresh ring range, sources live in other heaps.
	static thread_local std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> tlsTableCopyDestStarts;
	static thread_local std::vector<UINT> tlsTableCopyDestSizes;
	static thread_local std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> tlsTableCopySrcStarts;
	static thread_local std::vector<UINT> tlsTableCopySrcSizes;
	std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> &tableCopyDestStarts = tlsTableCopyDestStarts;
	std::vector<UINT> &tableCopyDestSizes = tlsTableCopyDestSizes;
	std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> &tableCopySrcStarts = tlsTableCopySrcStarts;
	std::vector<UINT> &tableCopySrcSizes = tlsTableCopySrcSizes;
	tableCopyDestStarts.clear();
	tableCopyDestSizes.clear();
	tableCopySrcStarts.clear();
	tableCopySrcSizes.clear();
	for (const TableCopy &table : tables)
	{
		D3D12_CPU_DESCRIPTOR_HANDLE src = {};
		src.ptr = table.cpuStart;
		D3D12_CPU_DESCRIPTOR_HANDLE dst = tempCpuBase;
		dst.ptr += static_cast<SIZE_T>(table.tempOffset) * tempIncrement;
		tableCopyDestStarts.push_back(dst);
		tableCopyDestSizes.push_back(table.count);
		tableCopySrcStarts.push_back(src);
		tableCopySrcSizes.push_back(table.count);
	}
	if (!tableCopyDestStarts.empty())
	{
		device->CopyDescriptors(
			static_cast<UINT>(tableCopyDestStarts.size()), tableCopyDestStarts.data(),
			tableCopyDestSizes.data(),
			static_cast<UINT>(tableCopySrcStarts.size()), tableCopySrcStarts.data(),
			tableCopySrcSizes.data(),
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}
	tableCopyDestStarts.clear();
	tableCopyDestSizes.clear();
	tableCopySrcStarts.clear();
	tableCopySrcSizes.clear();
	logStage("tables_copied", static_cast<UINT>(tables.size()), totalDescriptors);

	DX12_HOTPATH_TIME_BEGIN(PreSkinCopyPatch);
	class TableCopyLookupKeyUtils
	{
	public:
		static UINT64 Build(const DX12CurrentComputeUavBinding &binding)
		{
			UINT64 hash = binding.rootParameterIndex;
			hash = HashCombine64(hash, static_cast<UINT64>(binding.tableCpuStart));
			hash = HashCombine64(hash, binding.tableGpuStart.ptr);
			return hash;
		}

		static UINT64 Build(const TableCopy &table)
		{
			UINT64 hash = table.root;
			hash = HashCombine64(hash, static_cast<UINT64>(table.cpuStart));
			hash = HashCombine64(hash, table.gpuStart.ptr);
			return hash;
		}
	};
	static thread_local std::unordered_map<UINT64, size_t> tlsTableLookup;
	std::unordered_map<UINT64, size_t> &tableLookup = tlsTableLookup;
	tableLookup.clear();
	tableLookup.reserve(tables.size());
	for (size_t tableIndex = 0; tableIndex < tables.size(); ++tableIndex)
		tableLookup[TableCopyLookupKeyUtils::Build(tables[tableIndex])] = tableIndex;
	auto findTable = [&tables, &tableLookup](const DX12CurrentComputeUavBinding &binding) -> const TableCopy *
	{
		const auto found = tableLookup.find(TableCopyLookupKeyUtils::Build(binding));
		if (found == tableLookup.end() || found->second >= tables.size())
			return nullptr;
		return &tables[found->second];
	};
	static thread_local std::unordered_map<UINT, const DX12PreSkinSrvReplacementBinding *> tlsSrvReplacementLookup;
	std::unordered_map<UINT, const DX12PreSkinSrvReplacementBinding *> &srvReplacementLookup =
		tlsSrvReplacementLookup;
	srvReplacementLookup.clear();
	srvReplacementLookup.reserve(srvReplacements.size());
	for (const DX12PreSkinSrvReplacementBinding &binding : srvReplacements)
		srvReplacementLookup[binding.shaderRegister] = &binding;
	static thread_local std::vector<ID3D12Resource *> tlsTemporaryResources;
	std::vector<ID3D12Resource *> &temporaryResources = tlsTemporaryResources;
	temporaryResources.clear();
	bool patchedAnySrv = false;
	for (const DX12CurrentComputeUavBinding &srv : srvs)
	{
		const DX12PreSkinSrvReplacementBinding *replacementBinding = nullptr;
		DX12_HOTPATH_TIME_BEGIN(PreSkinLoadResource);
		const auto replacementIt = srvReplacementLookup.find(srv.shaderRegister);
		if (replacementIt != srvReplacementLookup.end())
			replacementBinding = replacementIt->second;
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
			srv.descriptor.viewDimension != D3D12_SRV_DIMENSION_BUFFER)
		{
			if (gPreSkinSrvStableNegativeCache.size() > 1024)
				gPreSkinSrvStableNegativeCache.clear();
			gPreSkinSrvStableNegativeCache.insert(stableNegativeKey);
			gPreSkinSrvNegativeCache.insert(negativeKey);
						continue;
		}
		const TableCopy *table = findTable(srv);
		if (!table)
		{
			gPreSkinSrvNegativeCache.insert(negativeKey);
						continue;
		}
		DX12LoadedResource *resource = nullptr;
		UINT elementStride = 0;
		auto stablePositiveIt = gPreSkinSrvStablePositiveCache.find(stableNegativeKey);
		if (stablePositiveIt != gPreSkinSrvStablePositiveCache.end())
		{
			resource = stablePositiveIt->second.resource;
			elementStride = stablePositiveIt->second.elementStride;
		}
		else
		{
			auto positiveIt = gPreSkinSrvPositiveCache.find(negativeKey);
			if (positiveIt != gPreSkinSrvPositiveCache.end())
			{
				resource = positiveIt->second.resource;
				elementStride = positiveIt->second.elementStride;
			}
			else
			{
				resource = EnsureLoadedResourceForPreSkin(device, replacementBinding->resource);
				elementStride =
					srv.descriptor.structureByteStride ? srv.descriptor.structureByteStride : (resource ? resource->stride : 0);
				if (!resource || !EnsureResourceSrvLocked(device, resource, elementStride, srvByteWidth) ||
					!resource->srvCpu.ptr)
				{
					gPreSkinSrvNegativeCache.insert(negativeKey);
										continue;
				}
				if (gPreSkinSrvPositiveCache.size() > 256)
					gPreSkinSrvPositiveCache.clear();
				if (gPreSkinSrvStablePositiveCache.size() > 512)
					gPreSkinSrvStablePositiveCache.clear();
				const DX12PreSkinSrvPositiveCacheValue cacheValue =
					{resource, elementStride, srvByteWidth, srv.shaderRegister};
				gPreSkinSrvPositiveCache[negativeKey] = cacheValue;
				gPreSkinSrvStablePositiveCache[stableNegativeKey] = cacheValue;
			}
		}
		if (!resource || !resource->srvCpu.ptr)
		{
			gPreSkinSrvNegativeCache.insert(negativeKey);
						continue;
		}
		if (!elementStride)
		{
			gPreSkinSrvNegativeCache.insert(negativeKey);
						continue;
		}
		D3D12_CPU_DESCRIPTOR_HANDLE dst = tempCpuBase;
		dst.ptr += static_cast<SIZE_T>(table->tempOffset + srv.descriptorOffset) * tempIncrement;
		tableCopyDestStarts.push_back(dst);
		tableCopyDestSizes.push_back(1);
		tableCopySrcStarts.push_back(resource->srvCpu);
		tableCopySrcSizes.push_back(1);
		patchedAnySrv = true;
	}

	// Flush the collected SRV patches as one batched copy before the CBV
	// count-patch phase; destination slots are distinct table offsets.
	if (!tableCopyDestStarts.empty())
	{
		device->CopyDescriptors(
			static_cast<UINT>(tableCopyDestStarts.size()), tableCopyDestStarts.data(),
			tableCopyDestSizes.data(),
			static_cast<UINT>(tableCopySrcStarts.size()), tableCopySrcStarts.data(),
			tableCopySrcSizes.data(),
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}

	if (!patchedAnySrv)
	{
		return false;
	}

	if (requiresCountPatch && canPatchCount)
	{
		bool patchedCountCbv = false;
		for (const DX12CurrentComputeUavBinding &cbv : cbvs)
		{
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
		if (!patchedCountCbv)
		{
			for (ID3D12Resource *resource : temporaryResources)
			{
				if (resource)
					resource->Release();
			}
			temporaryResources.clear();
			StorePreSkinDescriptorPatchAbort(abortCacheKey);
			return false;
		}
	}
	// Resize output UAV when mod vertex count exceeds original buffer capacity.
	// The compute shader writes a producer UAV, then the completed output is
	// copied to a separate IA-only buffer after dispatch.
	ID3D12Resource *resizedUavResource = nullptr;
	ID3D12Resource *resizedVertexResource = nullptr;
	ID3D12DescriptorHeap *resizedUavHeap = nullptr;
	D3D12_CPU_DESCRIPTOR_HANDLE resizedUavCpu = {};
	bool resizedUavCacheHit = false;
	UINT resizedOutputSlot = 0;
	bool outputUavResized = false;
	if (overrideVertexCount && originalVertexCount &&
		overrideVertexCount > originalVertexCount &&
		uavBinding.hasDescriptor && !uavBinding.rootDescriptor &&
		uavBinding.descriptor.kind == "UAV" &&
		uavBinding.descriptor.structureByteStride)
	{
		const UINT uavStride = uavBinding.descriptor.structureByteStride;
		const UINT64 requiredByteWidth =
			static_cast<UINT64>(uavStride) * overrideVertexCount;
		const UINT64 currentByteWidth = DescriptorViewSize(uavBinding.descriptor);
		if (requiredByteWidth > currentByteWidth)
		{
			const TableCopy *uavTable = findTable(uavBinding);
			if (uavTable && AcquirePreSkinOutputResizeCacheEntry(
								device, commandList, uavBinding.descriptor.resource,
								requiredByteWidth, uavStride, overrideVertexCount,
								&resizedUavResource, &resizedVertexResource,
								&resizedUavHeap, &resizedUavCpu, &resizedUavCacheHit,
								&resizedOutputSlot))
			{
				D3D12_CPU_DESCRIPTOR_HANDLE uavDst = tempCpuBase;
				uavDst.ptr += static_cast<SIZE_T>(
								  uavTable->tempOffset + uavBinding.descriptorOffset) *
							  tempIncrement;
				device->CopyDescriptorsSimple(
					1, uavDst, resizedUavCpu,
					D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				outputUavResized = true;
				// Acquired references follow this command list through the existing
				// delayed-retirement path. The cache owns separate base references.
				temporaryResources.push_back(resizedUavResource);
				temporaryResources.push_back(resizedVertexResource);
				if (resizedUavHeap)
				{
					resizedUavHeap->Release();
					resizedUavHeap = nullptr;
				}
			}
			if (!outputUavResized)
			{
				if (resizedUavResource)
					resizedUavResource->Release();
				if (resizedVertexResource)
					resizedVertexResource->Release();
				if (resizedUavHeap)
					resizedUavHeap->Release();
				resizedUavResource = nullptr;
				resizedVertexResource = nullptr;
				resizedUavHeap = nullptr;
				resizedUavCpu = {};
			}
		}
	}
	ID3D12DescriptorHeap *restoreCbvSrvUavHeap = nullptr;
	ID3D12DescriptorHeap *restoreSamplerHeap = nullptr;
	DX12BindingTracker::GetCurrentDescriptorHeaps(
		commandList, &restoreCbvSrvUavHeap, &restoreSamplerHeap);
	if (restoreCbvSrvUavHeap != tempHeap)
	{
		ID3D12DescriptorHeap *heaps[2] = {tempHeap, restoreSamplerHeap};
		logStage("before_set_heaps", static_cast<UINT>(tables.size()), totalDescriptors);
		commandList->SetDescriptorHeaps(restoreSamplerHeap ? 2 : 1, heaps);
		SyncPreSkinDescriptorHeaps(
			commandList, runtimeState, tempHeap, restoreSamplerHeap);
		logStage("after_set_heaps", static_cast<UINT>(tables.size()), totalDescriptors);
	}
	DX12PreSkinReplacementState state;
	state.active = true;
	state.computeShaderHash = computeShaderHash;
	state.bindingFingerprint = bindingFingerprint;
	state.triggerHash = triggerHash;
	DX12_HOTPATH_TIME_END(PreSkinCopyPatch);
	state.rootParameterIndex = uavBinding.rootParameterIndex;
	DX12_HOTPATH_TIME_BEGIN(PreSkinBindHeaps);
	state.originalTable = uavBinding.tableGpuStart;
	state.originalHeap = uavBinding.tableHeap;
	state.resource = outputUavResized ? resizedUavResource : uavBinding.descriptor.resource;
	state.outputVertexResource = outputUavResized ? resizedVertexResource : nullptr;
	state.originalOutputResource = outputUavResized ? uavBinding.descriptor.resource : nullptr;
	state.originalOutputByteWidth = outputUavResized ? DescriptorViewSize(uavBinding.descriptor) : 0;
	state.outputResizeSlot = outputUavResized ? resizedOutputSlot : 0;
	state.byteWidth = outputUavResized ? (static_cast<UINT64>(uavBinding.descriptor.structureByteStride) * overrideVertexCount) : DescriptorViewSize(uavBinding.descriptor);
	state.stride = uavBinding.descriptor.structureByteStride;
	state.outputUavResized = outputUavResized;
	state.hasActiveTextureOverride = state.resource && state.byteWidth && state.stride;
	if (state.hasActiveTextureOverride)
	{
		state.activeConfig = config;
		state.activeProducer = producer;
		if (state.resource)
			state.resource->AddRef();
	}
	state.patchHeap = tempHeap;
	state.restoreCbvSrvUavHeap = restoreCbvSrvUavHeap;
	state.restoreSamplerHeap = restoreSamplerHeap;
	state.temporaryResources = temporaryResources;
	for (const TableCopy &table : tables)
	{
		D3D12_GPU_DESCRIPTOR_HANDLE gpu = tempGpuBase;
		gpu.ptr += static_cast<UINT64>(table.tempOffset) * tempIncrement;
		logStage("before_set_root_table", static_cast<UINT>(tables.size()), totalDescriptors);
		commandList->SetComputeRootDescriptorTable(table.root, gpu);
		SyncPreSkinComputeRootDescriptorTable(
			commandList, runtimeState, table.root, gpu);
		logStage("after_set_root_table", static_cast<UINT>(tables.size()), totalDescriptors);
		DX12PreSkinReplacementState::TableRestore restore;
		restore.rootParameterIndex = table.root;
		restore.originalTable = table.gpuStart;
		state.tableRestores.push_back(restore);
	}
	if (!requiresCountPatch && !outputUavResized && bindingFingerprint)
	{
		tlsCloneCache.Clear();
		tempHeap->AddRef();
		tlsCloneCache.commandList = commandList;
		tlsCloneCache.reloadGeneration = reloadGeneration;
		tlsCloneCache.bindingFingerprint = bindingFingerprint;
		tlsCloneCache.triggerHash = triggerHash;
		tlsCloneCache.heap = tempHeap;
		tlsCloneCache.tableRestores = state.tableRestores;
		tlsCloneCache.patchedTables.reserve(tables.size());
		for (const TableCopy &table : tables)
		{
			D3D12_GPU_DESCRIPTOR_HANDLE gpu = tempGpuBase;
			gpu.ptr += static_cast<UINT64>(table.tempOffset) * tempIncrement;
			tlsCloneCache.patchedTables.push_back(gpu);
		}
		StorePersistentPreSkinDescriptorPatchLocked(
			reloadGeneration,
			bindingFingerprint,
			triggerHash,
			tempHeap,
			tlsCloneCache.patchedTables);
	}
	if (originalVertexCountOut)
		DX12_HOTPATH_TIME_END(PreSkinBindHeaps);
	*originalVertexCountOut = originalVertexCount;
	if (overrideVertexCountOut)
		*overrideVertexCountOut = overrideVertexCount;
	AcquireSRWLockExclusive(&gPreSkinLock);
	gActivePreSkinReplacements[commandList] = std::move(state);
	InterlockedExchange(&gHasActivePreSkinReplacement, 1);
	ReleaseSRWLockExclusive(&gPreSkinLock);
	BumpPreSkinReplacementMutationGeneration();

	return true;
}

static bool HasExplicitPreSkinDispatchOverride(
	const Bunny::TextureOverrideConfig &config)
{
	if (!config.handlingSkip)
		return false;
	for (const Bunny::CommandListAction &action : config.actions)
	{
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
	for (const Bunny::CommandListAction &action : config.actions)
	{
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
	if (replacement->uavState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
	{
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
													binding.descriptor.cpuHandle)
						  .second;
	if (gPatchedPreSkinDescriptors.size() > 4096)
		gPatchedPreSkinDescriptors.clear();
	gKnownPreSkinDescriptorPatches[binding.descriptor.cpuHandle] =
		replacement->name;
	ReleaseSRWLockExclusive(&gPreSkinLock);
	D3D12_CPU_DESCRIPTOR_HANDLE target = {};
	target.ptr = binding.descriptor.cpuHandle;
	if (descriptorRestores)
	{
		ID3D12DescriptorHeap *restoreHeap = nullptr;
		ID3D12Device *restoreDevice = nullptr;
		if (!AcquirePreSkinDescriptorRestoreHeap(
				device,
				&restoreHeap,
				&restoreDevice) ||
			!restoreHeap || !restoreDevice)
			return false;
		DX12PreSkinReplacementState::DescriptorRestore restore;
		restore.targetCpu = target.ptr;
		restore.device = restoreDevice;
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
	state.hasActiveTextureOverride =
		state.resource && state.byteWidth && state.stride &&
		producer.hasDescriptor && section && section[0];
	if (state.hasActiveTextureOverride)
	{
		state.activeConfig.section = section;
		state.activeProducer = producer;
		state.resource->AddRef();
	}
	if (descriptorRestores)
		state.descriptorRestores.swap(*descriptorRestores);
	AcquireSRWLockExclusive(&gPreSkinLock);
	gActivePreSkinReplacements[commandList] = std::move(state);
	InterlockedExchange(&gHasActivePreSkinReplacement, 1);
	ReleaseSRWLockExclusive(&gPreSkinLock);
	BumpPreSkinReplacementMutationGeneration();

	(void)alreadyPatched;
	return true;
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
	DX12PreSkinDispatchOverride *dispatchOverride,
	ID3D12Device *cachedDevice,
	UINT64 bindingFingerprint,
	UINT64 bindingDependencyFingerprint)
{
	if (originalVertexCount)
		*originalVertexCount = 0;
	if (overrideVertexCount)
		*overrideVertexCount = 0;
	if (dispatchOverride)
		*dispatchOverride = DX12PreSkinDispatchOverride();
	if (!commandList || !DX12ModHasActiveTextureOverrides())
		return false;

	LARGE_INTEGER acquireDeviceBegin = DX12OuterTimingProbeUtils::Now();
	ID3D12Device *device = AcquireModDeviceFast(commandList, cachedDevice);
	const bool ownsDevice = cachedDevice == nullptr;
	DX12OuterTimingProbeUtils::LogCpuStage(
		"PreSkinRuntime",
		"apply_acquire_device",
		0,
		1,
		DX12OuterTimingProbeUtils::ElapsedUs(acquireDeviceBegin));
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
	class PreSkinMatchPlanCache
	{
	public:
		ID3D12GraphicsCommandList *commandList = nullptr;
		UINT64 reloadGeneration = 0;
		UINT64 bindingFingerprint = 0;
		UINT64 bindingDependencyFingerprint = 0;
		UINT64 bufferContentGeneration = 0;
		uint64_t computeShaderHash = 0;
		DX12ComputeUavProducer producer;
		DX12VertexLimitRaiseConfig vlr;
		std::wstring vlrResourceName;
		DX12CurrentComputeUavBinding hashBinding;
		DX12UavHashTextureOverrideMatch hashMatch;
		bool valid = false;
		bool hashMatched = false;
		bool explicitMatchCs = false;
		bool hasExplicitDispatch = false;
		bool vlrMatched = false;
	};
	static thread_local PreSkinMatchPlanCache tlsMatchPlan;
	const UINT64 reloadGeneration = DX12ModGetReloadGenerationFast();
	const bool matchPlanCached = bindingFingerprint != 0 &&
								 tlsMatchPlan.valid &&
								 tlsMatchPlan.commandList == commandList &&
								 tlsMatchPlan.reloadGeneration == reloadGeneration &&
								 tlsMatchPlan.bindingFingerprint == bindingFingerprint &&
								 tlsMatchPlan.bindingDependencyFingerprint ==
									 bindingDependencyFingerprint &&
								 tlsMatchPlan.bufferContentGeneration ==
									 DX12GetIaHashContentGeneration() &&
								 tlsMatchPlan.computeShaderHash == computeShaderHash;

	LARGE_INTEGER matchBegin = DX12OuterTimingProbeUtils::Now();
	if (matchPlanCached)
	{
		producer = tlsMatchPlan.producer;
		vlr = tlsMatchPlan.vlr;
		vlrResourceName = tlsMatchPlan.vlrResourceName;
		hashBinding = tlsMatchPlan.hashBinding;
		hashMatch = tlsMatchPlan.hashMatch;
		hashMatched = tlsMatchPlan.hashMatched;
		explicitMatchCs = tlsMatchPlan.explicitMatchCs;
		hasExplicitDispatch = tlsMatchPlan.hasExplicitDispatch;
		vlrMatched = tlsMatchPlan.vlrMatched;
	}
	else
	{
		AcquireSRWLockShared(&gModLock);
		LARGE_INTEGER matchCsBegin = DX12OuterTimingProbeUtils::Now();
		explicitMatchCs = HasMatchCsTextureOverrideLocked(computeShaderHash);
		hashMatched = explicitMatchCs && FindPreSkinProducerByMatchCsLocked(
											 uavs, srvs, cbvs, rootConstants, computeShaderHash, &producer, &hashBinding, &hashMatch);
		DX12OuterTimingProbeUtils::LogCpuStage(
			"PreSkinRuntime",
			hashMatched ? "match_cs_hit" : "match_cs_miss",
			0,
			uavs.size() + srvs.size() + cbvs.size(),
			DX12OuterTimingProbeUtils::ElapsedUs(matchCsBegin));
		if (hashMatched && hashMatch.config)
			hasExplicitDispatch = HasExplicitPreSkinDispatchOverride(*hashMatch.config);
		if ((!hashMatched || !hasExplicitDispatch) && !uavs.empty())
		{
			LARGE_INTEGER vlrMatchBegin = DX12OuterTimingProbeUtils::Now();
			vlrMatched = FindPreSkinProducerLocked(uavs, computeShaderHash, &producer, &vlr) &&
						 FindReplacementResourceNameForVlrLocked(vlr, &vlrResourceName);
			DX12OuterTimingProbeUtils::LogCpuStage(
				"PreSkinRuntime",
				vlrMatched ? "vlr_match_hit" : "vlr_match_miss",
				0,
				uavs.size(),
				DX12OuterTimingProbeUtils::ElapsedUs(vlrMatchBegin));
		}
		ReleaseSRWLockShared(&gModLock);
		if (bindingFingerprint != 0)
		{
			tlsMatchPlan.commandList = commandList;
			tlsMatchPlan.reloadGeneration = reloadGeneration;
			tlsMatchPlan.bindingFingerprint = bindingFingerprint;
			tlsMatchPlan.bindingDependencyFingerprint =
				bindingDependencyFingerprint;
			tlsMatchPlan.bufferContentGeneration =
				DX12GetIaHashContentGeneration();
			tlsMatchPlan.computeShaderHash = computeShaderHash;
			tlsMatchPlan.producer = producer;
			tlsMatchPlan.vlr = vlr;
			tlsMatchPlan.vlrResourceName = vlrResourceName;
			tlsMatchPlan.hashBinding = hashBinding;
			tlsMatchPlan.hashMatch = hashMatch;
			tlsMatchPlan.valid = true;
			tlsMatchPlan.hashMatched = hashMatched;
			tlsMatchPlan.explicitMatchCs = explicitMatchCs;
			tlsMatchPlan.hasExplicitDispatch = hasExplicitDispatch;
			tlsMatchPlan.vlrMatched = vlrMatched;
		}
	}
	DX12OuterTimingProbeUtils::LogCpuStage(
		"PreSkinRuntime",
		"mod_lock_match_total",
		0,
		uavs.size() + srvs.size() + cbvs.size(),
		DX12OuterTimingProbeUtils::ElapsedUs(matchBegin));

	if (hashMatched)
	{
		if (!hasExplicitDispatch)
		{
		}
		else
		{
			if (!hashMatch.srvReplacements.empty())
			{
				LARGE_INTEGER patchBegin = DX12OuterTimingProbeUtils::Now();
				applied = ApplyPreSkinDescriptorTablePatchLocked(
					device, commandList, hashBinding, producer,
					*hashMatch.config, hashMatch.srvReplacements,
					srvs, cbvs, rootConstants,
					hashMatch.config->originalSection.empty() ? hashMatch.config->section.c_str() : hashMatch.config->originalSection.c_str(),
					hashMatch.hash, originalVertexCount, overrideVertexCount,
					bindingFingerprint, computeShaderHash);
				DX12OuterTimingProbeUtils::LogCpuStage(
					"PreSkinRuntime",
					applied ? "descriptor_table_patch_hit" : "descriptor_table_patch_miss",
					0,
					hashMatch.srvReplacements.size(),
					DX12OuterTimingProbeUtils::ElapsedUs(patchBegin));
			}
			if (applied)
			{
				LARGE_INTEGER dispatchOverrideBegin = DX12OuterTimingProbeUtils::Now();
				FillPreSkinDispatchOverrideFromConfig(*hashMatch.config, dispatchOverride);
				DX12OuterTimingProbeUtils::LogCpuStage(
					"PreSkinRuntime",
					"fill_dispatch_override",
					0,
					1,
					DX12OuterTimingProbeUtils::ElapsedUs(dispatchOverrideBegin));
				DX12Profiling::RecordPreSkinApplied();
			}
		}
	}
	if (!applied && vlrMatched)
	{
		LARGE_INTEGER activeSectionsBegin = DX12OuterTimingProbeUtils::Now();
		const std::unordered_set<std::wstring> &activePreSkinSections =
			GetActivePreSkinSectionsSnapshot();
		DX12OuterTimingProbeUtils::LogCpuStage(
			"PreSkinRuntime",
			"snapshot_active_sections",
			0,
			activePreSkinSections.size(),
			DX12OuterTimingProbeUtils::ElapsedUs(activeSectionsBegin));
		LARGE_INTEGER loadBegin = DX12OuterTimingProbeUtils::Now();
		replacement = EnsureLoadedResourceForPreSkin(device, vlrResourceName);
		DX12OuterTimingProbeUtils::LogCpuStage(
			"PreSkinRuntime",
			replacement ? "vlr_load_resource_hit" : "vlr_load_resource_miss",
			0,
			1,
			DX12OuterTimingProbeUtils::ElapsedUs(loadBegin));
		LARGE_INTEGER ensureUavBegin = DX12OuterTimingProbeUtils::Now();
		if (replacement && EnsureResourceUavLocked(
							   device, commandList, replacement,
							   vlr.uavByteStride ? vlr.uavByteStride : vlr.overrideByteStride,
							   0, true, &activePreSkinSections))
		{
			DX12OuterTimingProbeUtils::LogCpuStage(
				"PreSkinRuntime",
				"vlr_ensure_uav_hit",
				0,
				1,
				DX12OuterTimingProbeUtils::ElapsedUs(ensureUavBegin));
			LARGE_INTEGER findBindingBegin = DX12OuterTimingProbeUtils::Now();
			const DX12CurrentComputeUavBinding *binding = nullptr;
			for (const DX12CurrentComputeUavBinding &uav : uavs)
			{
				if (uav.rootParameterIndex == producer.rootParameterIndex &&
					uav.rangeIndex == producer.rangeIndex &&
					uav.descriptorOffset == producer.descriptorOffset &&
					uav.hasDescriptor && uav.tableGpuStart.ptr)
				{
					binding = &uav;
					break;
				}
			}
			DX12OuterTimingProbeUtils::LogCpuStage(
				"PreSkinRuntime",
				binding ? "vlr_find_binding_hit" : "vlr_find_binding_miss",
				0,
				uavs.size(),
				DX12OuterTimingProbeUtils::ElapsedUs(findBindingBegin));
			if (binding)
			{
				std::vector<DX12PreSkinReplacementState::DescriptorRestore> descriptorRestores;
				LARGE_INTEGER bindingPatchBegin = DX12OuterTimingProbeUtils::Now();
				applied = ApplyPreSkinBindingPatchLocked(
					device, commandList, *binding, producer, replacement,
					vlr.section.c_str(), 0, &descriptorRestores);
				DX12OuterTimingProbeUtils::LogCpuStage(
					"PreSkinRuntime",
					applied ? "vlr_binding_patch_hit" : "vlr_binding_patch_miss",
					0,
					1,
					DX12OuterTimingProbeUtils::ElapsedUs(bindingPatchBegin));
				if (applied)
					DX12Profiling::RecordPreSkinApplied();
				if (!applied)
					ReleasePreSkinDescriptorRestores(&descriptorRestores);
			}
		}
		else
		{
			DX12OuterTimingProbeUtils::LogCpuStage(
				"PreSkinRuntime",
				"vlr_ensure_uav_miss",
				0,
				1,
				DX12OuterTimingProbeUtils::ElapsedUs(ensureUavBegin));
		}
	}
	DX12Profiling::RecordPreSkinUavTest(applied);
	LARGE_INTEGER releaseDeviceBegin = DX12OuterTimingProbeUtils::Now();
	if (ownsDevice)
		device->Release();
	DX12OuterTimingProbeUtils::LogCpuStage(
		"PreSkinRuntime",
		"apply_release_device",
		0,
		1,
		DX12OuterTimingProbeUtils::ElapsedUs(releaseDeviceBegin));
	return applied;
}

bool DX12ModApplyKnownPreSkinningUavPatches(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList || !DX12ModHasActiveTextureOverrides())
		return false;

	std::vector<std::pair<SIZE_T, std::wstring>> patches;
	LARGE_INTEGER snapshotBegin = DX12OuterTimingProbeUtils::Now();
	AcquireSRWLockShared(&gPreSkinLock);
	patches.assign(gKnownPreSkinDescriptorPatches.begin(), gKnownPreSkinDescriptorPatches.end());
	ReleaseSRWLockShared(&gPreSkinLock);
	DX12OuterTimingProbeUtils::LogCpuStage(
		"PreSkinRuntime",
		"known_patch_snapshot",
		0,
		patches.size(),
		DX12OuterTimingProbeUtils::ElapsedUs(snapshotBegin));
	if (patches.empty())
		return false;

	LARGE_INTEGER acquireDeviceBegin = DX12OuterTimingProbeUtils::Now();
	ID3D12Device *device = AcquireModDevice(commandList);
	DX12OuterTimingProbeUtils::LogCpuStage(
		"PreSkinRuntime",
		"known_patch_acquire_device",
		0,
		1,
		DX12OuterTimingProbeUtils::ElapsedUs(acquireDeviceBegin));
	if (!device)
		return false;

	DX12InternalReplayScope internalReplay;
	bool applied = false;
	LARGE_INTEGER activeSectionsBegin = DX12OuterTimingProbeUtils::Now();
	const std::unordered_set<std::wstring> &activePreSkinSections =
		GetActivePreSkinSectionsSnapshot();
	DX12OuterTimingProbeUtils::LogCpuStage(
		"PreSkinRuntime",
		"known_patch_active_sections",
		0,
		activePreSkinSections.size(),
		DX12OuterTimingProbeUtils::ElapsedUs(activeSectionsBegin));
	for (const auto &patch : patches)
	{
		LARGE_INTEGER blockedBegin = DX12OuterTimingProbeUtils::Now();
		AcquireSRWLockExclusive(&gModLock);
		const bool blocked =
			ResourceBlockedByInactiveExplicitMatchCsLocked(patch.second, activePreSkinSections);
		ReleaseSRWLockExclusive(&gModLock);
		DX12OuterTimingProbeUtils::LogCpuStage(
			"PreSkinRuntime",
			blocked ? "known_patch_blocked" : "known_patch_allowed",
			0,
			1,
			DX12OuterTimingProbeUtils::ElapsedUs(blockedBegin));
		if (blocked)
			continue;
		LARGE_INTEGER loadBegin = DX12OuterTimingProbeUtils::Now();
		DX12LoadedResource *resource = EnsureLoadedResourceForPreSkin(device, patch.second);
		DX12OuterTimingProbeUtils::LogCpuStage(
			"PreSkinRuntime",
			resource ? "known_patch_load_resource_hit" : "known_patch_load_resource_miss",
			0,
			1,
			DX12OuterTimingProbeUtils::ElapsedUs(loadBegin));
		if (!resource)
			continue;
		LARGE_INTEGER strideBegin = DX12OuterTimingProbeUtils::Now();
		AcquireSRWLockExclusive(&gModLock);
		const UINT elementStride =
			FindPreSkinUavElementStrideLocked(*resource, activePreSkinSections);
		ReleaseSRWLockExclusive(&gModLock);
		DX12OuterTimingProbeUtils::LogCpuStage(
			"PreSkinRuntime",
			"known_patch_find_stride",
			0,
			1,
			DX12OuterTimingProbeUtils::ElapsedUs(strideBegin));
		LARGE_INTEGER ensureBegin = DX12OuterTimingProbeUtils::Now();
		if (!EnsureResourceUavLocked(
				device, commandList, resource, elementStride, 0, true,
				&activePreSkinSections))
		{
			DX12OuterTimingProbeUtils::LogCpuStage(
				"PreSkinRuntime",
				"known_patch_ensure_uav_miss",
				0,
				1,
				DX12OuterTimingProbeUtils::ElapsedUs(ensureBegin));
			continue;
		}
		DX12OuterTimingProbeUtils::LogCpuStage(
			"PreSkinRuntime",
			"known_patch_ensure_uav_hit",
			0,
			1,
			DX12OuterTimingProbeUtils::ElapsedUs(ensureBegin));
		if (!resource->uavResource || !resource->uavCpu.ptr)
			continue;
		if (resource->uavState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
		{
			LARGE_INTEGER barrierBegin = DX12OuterTimingProbeUtils::Now();
			D3D12_RESOURCE_BARRIER barrier = {};
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Transition.pResource = resource->uavResource;
			barrier.Transition.StateBefore = resource->uavState;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
			barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			commandList->ResourceBarrier(1, &barrier);
			resource->uavState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
			DX12OuterTimingProbeUtils::LogCpuStage(
				"PreSkinRuntime",
				"known_patch_resource_barrier",
				0,
				1,
				DX12OuterTimingProbeUtils::ElapsedUs(barrierBegin));
		}
		D3D12_CPU_DESCRIPTOR_HANDLE target = {};
		target.ptr = patch.first;
		LARGE_INTEGER copyBegin = DX12OuterTimingProbeUtils::Now();
		device->CopyDescriptorsSimple(
			1, target, resource->uavCpu,
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		DX12OuterTimingProbeUtils::LogCpuStage(
			"PreSkinRuntime",
			"known_patch_copy_descriptor",
			0,
			1,
			DX12OuterTimingProbeUtils::ElapsedUs(copyBegin));
		resource->uavWritten = true;
		resource->uavValid = true;
		resource->uavValidPresent = DX12RuntimeState::GetPresentCount();
		applied = true;
	}
	LARGE_INTEGER releaseDeviceBegin = DX12OuterTimingProbeUtils::Now();
	device->Release();
	DX12OuterTimingProbeUtils::LogCpuStage(
		"PreSkinRuntime",
		"known_patch_release_device",
		0,
		1,
		DX12OuterTimingProbeUtils::ElapsedUs(releaseDeviceBegin));
	return applied;
}

void DX12ModPublishPreSkinOutputResizeEarly(ID3D12GraphicsCommandList *commandList)
{
	// Publish the VA alias before the skinning dispatch runs. Draws recorded
	// on other lists (or earlier on this list) resolve IA views while
	// recording; registering only at restore time (after the dispatch) lets
	// those draws miss the mapping and render the un-skinned original
	// buffer - shadow/body mismatch and flicker. Re-registration from the
	// restore path with equal content is a no-op that does not bump the
	// generation (see DX12ModRegisterPreSkinOutputResize).
	if (!commandList || gHasActivePreSkinReplacement == 0)
		return;
	AcquireSRWLockShared(&gPreSkinLock);
	auto it = gActivePreSkinReplacements.find(commandList);
	if (it == gActivePreSkinReplacements.end())
	{
		ReleaseSRWLockShared(&gPreSkinLock);
		return;
	}
	const DX12PreSkinReplacementState &state = it->second;
	const bool canPublish = state.active && state.outputUavResized &&
							state.resource && state.outputVertexResource && state.byteWidth &&
							state.originalOutputResource && state.originalOutputByteWidth;
	ReleaseSRWLockShared(&gPreSkinLock);
	if (!canPublish)
		return;
	DX12ModRegisterPreSkinOutputResize(
		commandList,
		state.originalOutputResource,
		state.outputVertexResource,
		state.originalOutputByteWidth,
		state.byteWidth,
		state.stride);
}

void DX12ModRestorePreSkinningUavReplacement(ID3D12GraphicsCommandList *commandList)
{
	// Plain volatile probe, see DX12ModIsPreSkinReplacementReusable for the
	// ordering argument. The map lookup below is still lock-protected.
	if (!commandList || gHasActivePreSkinReplacement == 0)
		return;

	// Command-list-local negative cache: while no map mutation has happened,
	// "this list has no pending restore" cannot change. Recording is
	// single-threaded per command list, so a thread-local entry keyed by the
	// list pointer is exact; the mutation generation covers inserts/erases
	// made by other threads. This keeps the per-draw cost at two loads
	// instead of a global exclusive SRW lock + unordered_map lookup on every
	// draw/dispatch/binding mutation while any pre-skin replacement is
	// pending anywhere in the process.
	struct TlsNoPendingRestore
	{
		ID3D12GraphicsCommandList *commandList = nullptr;
		UINT64 generation = 0;
	};
	static thread_local TlsNoPendingRestore tlsNoPendingRestore;
	const UINT64 mutationGeneration =
		gPreSkinReplacementMutationGeneration.load(std::memory_order_acquire);
	if (tlsNoPendingRestore.commandList == commandList &&
		tlsNoPendingRestore.generation == mutationGeneration)
		return;

	DX12InternalReplayScope internalReplay;
	DX12PreSkinReplacementState state = {};
	bool found = false;
	bool erased = false;
	AcquireSRWLockExclusive(&gPreSkinLock);
	auto it = gActivePreSkinReplacements.find(commandList);
	if (it != gActivePreSkinReplacements.end())
	{
		state = std::move(it->second);
		gActivePreSkinReplacements.erase(it);
		found = state.active;
		erased = true;
	}
	if (gActivePreSkinReplacements.empty())
		InterlockedExchange(&gHasActivePreSkinReplacement, 0);
	ReleaseSRWLockExclusive(&gPreSkinLock);
	// Only bump the generation when the map actually changed. This lookup
	// runs on every draw/dispatch of every command list while any pre-skin
	// replacement is pending; an unconditional bump would invalidate the
	// TLS negative cache of every other list on each call, forcing them all
	// back onto the exclusive lock plus a map lookup for no reason (and it
	// would also make DX12ModIsPreSkinReplacementReusable observe a phantom
	// mutation). Same conditional-bump pattern as
	// DX12ModClearPreSkinOutputResizeMap.
	if (erased)
		BumpPreSkinReplacementMutationGeneration();

	// Re-read the generation after the mutation so the negative entry stays
	// valid for exactly the state we just observed.
	tlsNoPendingRestore.commandList = commandList;
	tlsNoPendingRestore.generation =
		gPreSkinReplacementMutationGeneration.load(std::memory_order_relaxed);

	if (!found)
		return;
	DX12SimulateContext *runtimeState =
		DX12CommandListRuntime::GetStatePtrFast(commandList);

	// Keep compute writes and IA reads on separate resources. The copy is ordered
	// after dispatch on this producer command list, while later draw lists only
	// ever see a COMMON buffer eligible for independent read promotion.
	if (state.outputUavResized && state.resource &&
		state.outputVertexResource && state.byteWidth)
	{
		// Restore the game invariant on the original output buffer too. The
		// dispatch wrote the resized UAV instead, so the original still holds
		// stale bytes; any consumer whose IA view misses the VA-alias redirect
		// (another command list, or recording after Present cleared the publish
		// map) then reads garbage vertices and explodes into full-screen
		// triangles. Copy the leading original-capacity span back so a missed
		// redirect degrades to un-skinned original vertices instead. The game
		// left this buffer in UNORDERED_ACCESS for the dispatch, and this
		// restore runs before its next command, so transitioning from and back
		// to UAV keeps the game's own barrier bookkeeping intact.
		const bool copyBackOriginal =
			state.originalOutputResource && state.originalOutputByteWidth != 0;
		D3D12_RESOURCE_BARRIER beforeCopy[3] = {};
		beforeCopy[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		beforeCopy[0].Transition.pResource = state.resource;
		beforeCopy[0].Transition.StateBefore =
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		beforeCopy[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
		beforeCopy[0].Transition.Subresource =
			D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		beforeCopy[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		beforeCopy[1].Transition.pResource = state.outputVertexResource;
		beforeCopy[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
		beforeCopy[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
		beforeCopy[1].Transition.Subresource =
			D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		UINT beforeCopyCount = 2;
		if (copyBackOriginal)
		{
			beforeCopy[2].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			beforeCopy[2].Transition.pResource = state.originalOutputResource;
			beforeCopy[2].Transition.StateBefore =
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
			beforeCopy[2].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
			beforeCopy[2].Transition.Subresource =
				D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			beforeCopyCount = 3;
		}
		commandList->ResourceBarrier(beforeCopyCount, beforeCopy);
		commandList->CopyBufferRegion(
			state.outputVertexResource, 0, state.resource, 0, state.byteWidth);
		if (copyBackOriginal)
			commandList->CopyBufferRegion(
				state.originalOutputResource, 0, state.resource, 0,
				(std::min)(state.originalOutputByteWidth, state.byteWidth));

		D3D12_RESOURCE_BARRIER afterCopy[3] = {};
		afterCopy[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		afterCopy[0].Transition.pResource = state.resource;
		afterCopy[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
		afterCopy[0].Transition.StateAfter =
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		afterCopy[0].Transition.Subresource =
			D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		afterCopy[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		afterCopy[1].Transition.pResource = state.outputVertexResource;
		afterCopy[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		afterCopy[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
		afterCopy[1].Transition.Subresource =
			D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		UINT afterCopyCount = 2;
		if (copyBackOriginal)
		{
			afterCopy[2].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			afterCopy[2].Transition.pResource = state.originalOutputResource;
			afterCopy[2].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
			afterCopy[2].Transition.StateAfter =
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
			afterCopy[2].Transition.Subresource =
				D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			afterCopyCount = 3;
		}
		commandList->ResourceBarrier(afterCopyCount, afterCopy);

		DX12ModRegisterPreSkinOutputResize(
			commandList,
			state.originalOutputResource,
			state.outputVertexResource,
			state.originalOutputByteWidth,
			state.byteWidth,
			state.stride);
	}

	if (state.patchHeap)
	{
		ID3D12DescriptorHeap *restoreCbvSrvUavHeap =
			state.restoreCbvSrvUavHeap ? state.restoreCbvSrvUavHeap : state.originalHeap;
		ID3D12DescriptorHeap *heaps[2] = {};
		UINT heapCount = 0;
		if (restoreCbvSrvUavHeap)
			heaps[heapCount++] = restoreCbvSrvUavHeap;
		if (state.restoreSamplerHeap)
			heaps[heapCount++] = state.restoreSamplerHeap;
		commandList->SetDescriptorHeaps(heapCount, heapCount ? heaps : nullptr);
		if (runtimeState)
		{
			DX12CommandListRuntime::SetDescriptorHeaps(
				runtimeState, restoreCbvSrvUavHeap, state.restoreSamplerHeap);
		}
		else
		{
			DX12CommandListRuntime::SetDescriptorHeaps(
				commandList, restoreCbvSrvUavHeap, state.restoreSamplerHeap);
		}
		DX12BindingTracker::SetDescriptorHeaps(
			commandList, heapCount, heapCount ? heaps : nullptr);
		for (const auto &restore : state.tableRestores)
		{
			commandList->SetComputeRootDescriptorTable(
				restore.rootParameterIndex, restore.originalTable);
			if (runtimeState)
			{
				DX12CommandListRuntime::RememberRootDescriptorTable(
					runtimeState, false, restore.rootParameterIndex,
					restore.originalTable);
			}
			else
			{
				DX12CommandListRuntime::RememberRootDescriptorTable(
					commandList, false, restore.rootParameterIndex,
					restore.originalTable);
			}
			DX12BindingTracker::SetComputeRootDescriptorTable(
				commandList, restore.rootParameterIndex, restore.originalTable);
		}
		state.patchHeap = nullptr;
	}
	for (ID3D12Resource *resource : state.temporaryResources)
	{
		if (resource)
			RetirePreSkinResourceForCommandList(commandList, resource);
	}
	state.temporaryResources.clear();
	ID3D12Device *device = AcquireModDevice(commandList);
	if (device)
	{
		for (const auto &restore : state.descriptorRestores)
		{
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

	if (state.hasActiveTextureOverride && state.resource)
	{
		std::vector<ID3D12Resource *> releasePreSkinResources;
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

}

// ---------------------------------------------------------------------------
// DX12ModComputePreSkinMatchOnly performs matching off the critical path.
// Runs only the data-structure lookup phase of PreSkinProbe without emitting
// any D3D12 commands.  Safe to call from the async worker thread.
// Populates gPreSkinUavMatchCache so that the next render-thread Dispatch
// hits the cache and applies synchronously (fast path).
// ---------------------------------------------------------------------------
bool DX12ModComputePreSkinMatchOnly(
	uint64_t computeShaderHash,
	const std::vector<DX12CurrentComputeUavBinding> &uavs,
	const std::vector<DX12CurrentComputeUavBinding> &srvs,
	const std::vector<DX12CurrentComputeUavBinding> &cbvs,
	const std::vector<DX12CurrentRootConstants> &rootConstants,
	bool *outMatched,
	DX12PreSkinDispatchOverride *outDispatchOverride)
{
	if (outMatched)
		*outMatched = false;
	if (outDispatchOverride)
		*outDispatchOverride = DX12PreSkinDispatchOverride();
	if (!DX12ModHasActiveTextureOverrides())
		return false;

	DX12ComputeUavProducer producer = {};
	DX12VertexLimitRaiseConfig vlr = {};
	std::wstring vlrResourceName;
	DX12CurrentComputeUavBinding hashBinding = {};
	DX12UavHashTextureOverrideMatch hashMatch = {};
	bool hashMatched = false;
	bool explicitMatchCs = false;
	bool hasExplicitDispatch = false;
	bool vlrMatched = false;
	const std::unordered_set<std::wstring> &activePreSkinSections =
		GetActivePreSkinSectionsSnapshot();

	AcquireSRWLockShared(&gModLock);
	explicitMatchCs = HasMatchCsTextureOverrideLocked(computeShaderHash);
	hashMatched = explicitMatchCs && FindPreSkinProducerByMatchCsLocked(
										 uavs, srvs, cbvs, rootConstants, computeShaderHash, &producer, &hashBinding, &hashMatch);
	if (hashMatched && hashMatch.config)
		hasExplicitDispatch = HasExplicitPreSkinDispatchOverride(*hashMatch.config);
	if ((!hashMatched || !hasExplicitDispatch) && !uavs.empty())
	{
		vlrMatched = FindPreSkinProducerLocked(uavs, computeShaderHash, &producer, &vlr) &&
					 FindReplacementResourceNameForVlrLocked(vlr, &vlrResourceName);
	}
	ReleaseSRWLockShared(&gModLock);

	bool matched = hashMatched || vlrMatched;
	if (outMatched)
		*outMatched = matched;
	if (matched && outDispatchOverride && hashMatched && hasExplicitDispatch)
	{
		// MatchCs path: populate dispatch override from the matched config.
		// VLR path sets matched=true but does not populate dispatchOverride;
		// the apply phase handles VLR via resource loading and UAV setup.
		FillPreSkinDispatchOverrideFromConfig(*hashMatch.config, outDispatchOverride);
	}
	return matched;
}

bool DX12ModReplaceShaderBytecode(
	const char *stage, const D3D12_SHADER_BYTECODE &source,
	D3D12_SHADER_BYTECODE *replacement, std::vector<unsigned char> *storage)
{
	if (!DX12ModHasActiveShaderOverrides())
		return false;
	if (!stage || !source.pShaderBytecode || source.BytecodeLength == 0 ||
		!replacement || !storage)
		return false;

	const uint64_t hash = DX12ModHashShaderBytecode(
		source.pShaderBytecode, source.BytecodeLength);

	std::wstring section;
	std::wstring shaderFixesDir;
	AcquireSRWLockShared(&gModLock);
	auto it = gShaderOverrides.find(hash);
	if (it == gShaderOverrides.end())
	{
		ReleaseSRWLockShared(&gModLock);
		return false;
	}
	section = it->second.section;
	shaderFixesDir = gShaderFixesDir;
	ReleaseSRWLockShared(&gModLock);

	wchar_t path[MAX_PATH];
	swprintf_s(path, L"%s\\%016llx-%S.bin",
			   shaderFixesDir.c_str(), static_cast<unsigned long long>(hash), stage);

	std::vector<unsigned char> bytes;
	if (!ReadFileBytes(path, &bytes))
	{
		return false;
	}

	storage->swap(bytes);
	replacement->pShaderBytecode = storage->data();
	replacement->BytecodeLength = storage->size();
	return true;
}

bool DX12ModHasShaderOverride(uint64_t hash)
{
	AcquireSRWLockShared(&gModLock);
	bool found = gShaderOverrides.find(hash) != gShaderOverrides.end();
	ReleaseSRWLockShared(&gModLock);
	return found;
}

bool DX12ModHasActiveShaderOverrides()
{
	return (DX12ModGetHotPathFlags() & DX12_MOD_HOT_SHADER_OVERRIDES) != 0;
}

bool DX12ModNeedsShaderDescriptorTracking()
{
	return (DX12ModGetHotPathFlags() &
			DX12_MOD_HOT_SHADER_DESCRIPTOR_TRACK) != 0;
}

bool DX12ModHasActiveTextureOverrides()
{
	return gHasTextureOverrides != 0 &&
		   gHasShaderTriggeredTextureOverrides != 0 &&
		   InterlockedCompareExchange(&gDx12SafeMode, 0, 0) == 0;
}

bool DX12ModHasConfiguredTextureOverrides()
{
	return gHasTextureOverrides != 0 &&
		   InterlockedCompareExchange(&gDx12SafeMode, 0, 0) == 0;
}

bool DX12ModHasAnyActiveOverrides()
{
	return InterlockedCompareExchange(&gDx12SafeMode, 0, 0) == 0 &&
		   (gHasShaderOverrides != 0 ||
			gHasShaderRegexes != 0 ||
			(gHasTextureOverrides != 0 && gHasShaderTriggeredTextureOverrides != 0));
}

bool DX12ModNeedsPresentReplacement()
{
	return (DX12ModGetHotPathFlags() &
			DX12_MOD_HOT_PRESENT_REPLACEMENT) != 0;
}

bool DX12ModNeedsPreSkinningUavProbe()
{
	return (DX12ModGetHotPathFlags() &
			DX12_MOD_HOT_PRE_SKIN_PROBE) != 0;
}

// Returns true when pre-skin binding tracking is actively needed.
// Automatically disables after kPreSkinBindingIdleMax consecutive frames
// with zero active overrides, saving ~1500+ lock acquisitions per frame.
// Note: idle counter is reset when a matching pre-skin CS dispatch is probed.
bool DX12ModNeedsPreSkinningBindingTracking()
{
	return (DX12ModGetHotPathFlags() &
			DX12_MOD_HOT_PRE_SKIN_BINDING_TRACK) != 0;
}

bool DX12ModHasActivePreSkinTextureOverrides()
{
	if (InterlockedCompareExchange(&gDx12SafeMode, 0, 0) != 0)
		return false;
	return InterlockedCompareExchange(&gHasActivePreSkinTextureOverridesFast, 0, 0) != 0;
}

UINT32 DX12ModGetHotPathFlags()
{
	if (InterlockedCompareExchange(&gDx12SafeMode, 0, 0) != 0)
		return 0;

	UINT32 flags = 0;
	const bool hasShaderOverrides =
		gHasShaderOverrides != 0 || gHasShaderRegexes != 0;
	const bool hasTextureOverrides =
		gHasTextureOverrides != 0 &&
		gHasShaderTriggeredTextureOverrides != 0;
	const bool hasDrawOverrideWork =
		hasShaderOverrides || hasTextureOverrides;

	if (hasShaderOverrides)
		flags |= DX12_MOD_HOT_SHADER_OVERRIDES;
	if (hasDrawOverrideWork && gHasIaResidentStateRequirements != 0)
		flags |= DX12_MOD_HOT_IA_RESIDENT_STATE;
	if (gHasPresentRuntimeEffect != 0)
		flags |= DX12_MOD_HOT_PRESENT_REPLACEMENT;
	if (hasTextureOverrides &&
		gHasPreSkinTextureOverrideCandidates != 0)
	{
		flags |= DX12_MOD_HOT_PRE_SKIN_PROBE;
		const bool hasActivePreSkinOverrides =
			InterlockedCompareExchange(&gHasActivePreSkinTextureOverridesFast, 0, 0) != 0;
		if (hasActivePreSkinOverrides ||
			InterlockedCompareExchange(&gPreSkinBindingIdleFrames, 0, 0) <
				kPreSkinBindingIdleMax)
		{
			flags |= DX12_MOD_HOT_PRE_SKIN_BINDING_TRACK;
		}
	}
	if (hasTextureOverrides &&
		gHasDrawSrvTextureReplacementOverrides != 0)
		flags |= DX12_MOD_HOT_DRAW_SRV_SLOT_REPLACEMENT;
	if (gHasShaderDescriptorTextureOverrideTriggers != 0)
	{
		flags |= DX12_MOD_HOT_SHADER_DESCRIPTOR_TRACK;
	}
	// Plan step 3.2: bind-time static IA replacements keep their staleness
	// bookkeeping on DX12ActiveIaState (checked lazily by
	// RestoreStaleStaticIaBindings once per content/resolve generation change).
	// IA resident state keeps the recorded views fresh. FLAG_SHADER_OVERRIDES
	// is deliberately NOT forced here: static IA replacement is unrelated to
	// shader overrides, and forcing the flag made every draw enter the full
	// HandleDrawOverrides chain (plan fix B).
	if (gHasStaticIaReplacements != 0)
		flags |= DX12_MOD_HOT_IA_RESIDENT_STATE;
	return flags;
}

bool DX12ModShouldProbePreSkinningForCs(uint64_t computeShaderHash)
{
	if (InterlockedCompareExchange(&gDx12SafeMode, 0, 0) != 0 ||
		gHasTextureOverrides == 0 ||
		gHasShaderTriggeredTextureOverrides == 0 ||
		gHasPreSkinTextureOverrideCandidates == 0)
		return false;

	// The configured CS hash set only changes on mod reload. Cache the
	// verdict per CS hash in TLS so the dispatch hot path does not take the
	// global mod lock on every dispatch (this predicate is consulted 2-3
	// times per dispatch and the game rotates through a small CS set).
	struct TlsProbeCsCacheEntry
	{
		uint64_t hash = 0;
		bool result = false;
		bool valid = false;
	};
	static thread_local std::array<TlsProbeCsCacheEntry, 32> tlsProbeCsCache;
	static thread_local UINT64 tlsProbeCsReloadGeneration = 0;
	const UINT64 reloadGeneration = DX12ModGetReloadGenerationFast();
	if (reloadGeneration != tlsProbeCsReloadGeneration)
	{
		tlsProbeCsCache = {};
		tlsProbeCsReloadGeneration = reloadGeneration;
	}
	const UINT slotMask =
		static_cast<UINT>(tlsProbeCsCache.size()) - 1;
	const UINT slot = static_cast<UINT>(computeShaderHash ^
										(computeShaderHash >> 32)) &
					  slotMask;
	TlsProbeCsCacheEntry &tlsEntry = tlsProbeCsCache[slot];
	if (tlsEntry.valid && tlsEntry.hash == computeShaderHash)
		return tlsEntry.result;

	bool shouldProbe = false;
	AcquireSRWLockShared(&gModLock);
	if (gHasPreSkinVlrWithoutMatchCs)
	{
		shouldProbe = true;
	}
	else if (computeShaderHash)
	{
		shouldProbe = gPreSkinMatchCsHashes.find(computeShaderHash) !=
					  gPreSkinMatchCsHashes.end();
	}
	ReleaseSRWLockShared(&gModLock);
	tlsEntry.valid = true;
	tlsEntry.hash = computeShaderHash;
	tlsEntry.result = shouldProbe;
	// Reset binding tracking idle counter: as long as a matching CS dispatch
	// runs, keep binding tracking active so the probe can access bindings.
	if (shouldProbe)
		InterlockedExchange(&gPreSkinBindingIdleFrames, 0);
	DX12Profiling::RecordPreSkinCsTest(shouldProbe);
	return shouldProbe;
}

bool DX12ModShouldTrackPreSkinBindingsForCs(uint64_t computeShaderHash)
{
	return DX12ModShouldProbePreSkinningForCs(computeShaderHash);
}

bool DX12ModPreSkinningUavProducerMayMatch(
	uint64_t computeShaderHash,
	const std::vector<DX12CurrentComputeUavBinding> &uavs)
{
	(void)uavs;
	return DX12ModShouldProbePreSkinningForCs(computeShaderHash);
}

bool DX12ModHasCachedPreSkinningUavMatch(
	ID3D12GraphicsCommandList *commandList,
	uint64_t computeShaderHash,
	UINT64 computeBindingSerial,
	bool *matched)
{
	if (!matched || !commandList || !computeShaderHash || computeBindingSerial == 0)
		return false;

	UINT64 cacheKey = MakePreSkinUavMatchCacheKey(
		commandList, computeShaderHash, computeBindingSerial, 0);
	AcquireSRWLockShared(&gModLock);
	auto cached = gPreSkinUavMatchCache.find(cacheKey);
	if (cached != gPreSkinUavMatchCache.end() &&
		cached->second.valid)
	{
		*matched = cached->second.matched;
		ReleaseSRWLockShared(&gModLock);
		return true;
	}
	ReleaseSRWLockShared(&gModLock);
	return false;
}

static void UpdatePreSkinningUavMatchCacheLocked(
	ID3D12GraphicsCommandList *commandList,
	uint64_t computeShaderHash,
	UINT64 computeBindingSerial,
	UINT64 bindingDependencyFingerprint,
	bool matched,
	const DX12PreSkinDispatchOverride *dispatchOverride)
{
	UINT64 cacheKey = MakePreSkinUavMatchCacheKey(
		commandList, computeShaderHash, computeBindingSerial,
		bindingDependencyFingerprint);
	DX12PreSkinUavMatchCacheEntry &entry = gPreSkinUavMatchCache[cacheKey];
	bool isNewEntry = !entry.valid;
	entry.computeShaderHash = computeShaderHash;
	entry.computeBindingSerial = computeBindingSerial;
	entry.valid = true;
	entry.matched = matched;
	if (dispatchOverride)
		entry.dispatchOverride = *dispatchOverride;
	if (isNewEntry && gPreSkinUavMatchCache.size() > gPreSkinUavMatchCacheGrowthBudget)
	{
		size_t nextBudget = gPreSkinUavMatchCacheGrowthBudget ? gPreSkinUavMatchCacheGrowthBudget : MaxPreSkinUavMatchCacheEntries;
		while (nextBudget < gPreSkinUavMatchCache.size())
		{
			const size_t doubled = nextBudget * 2;
			if (doubled <= nextBudget)
			{
				nextBudget = gPreSkinUavMatchCache.size();
				break;
			}
			nextBudget = doubled;
		}
		gPreSkinUavMatchCache.reserve(nextBudget);
		gPreSkinUavMatchCacheGrowthBudget = nextBudget;
	}
}

void DX12ModStoreCachedPreSkinningUavMatch(
	ID3D12GraphicsCommandList *commandList,
	uint64_t computeShaderHash,
	UINT64 computeBindingSerial,
	UINT64 bindingDependencyFingerprint,
	bool matched,
	const DX12PreSkinDispatchOverride *dispatchOverride)
{
	if (!commandList || !computeShaderHash || computeBindingSerial == 0)
		return;

	AcquireSRWLockExclusive(&gModLock);
	UpdatePreSkinningUavMatchCacheLocked(
		commandList, computeShaderHash, computeBindingSerial,
		bindingDependencyFingerprint, matched, dispatchOverride);
	ReleaseSRWLockExclusive(&gModLock);
}

bool DX12ModGetCachedPreSkinProbeResult(
	ID3D12GraphicsCommandList *commandList,
	uint64_t computeShaderHash,
	UINT64 bindingHash,
	UINT64 bindingDependencyFingerprint,
	DX12PreSkinProbeCacheEntry *result)
{
	if (!result || !commandList || !computeShaderHash || bindingHash == 0)
		return false;

	UINT64 cacheKey = MakePreSkinUavMatchCacheKey(
		commandList, computeShaderHash, bindingHash,
		bindingDependencyFingerprint);
	AcquireSRWLockShared(&gModLock);
	auto cached = gPreSkinUavMatchCache.find(cacheKey);
	if (cached != gPreSkinUavMatchCache.end() &&
		cached->second.valid)
	{
		result->applied = cached->second.matched;
		result->dispatchOverride = cached->second.dispatchOverride;
		ReleaseSRWLockShared(&gModLock);
		return true;
	}
	ReleaseSRWLockShared(&gModLock);
	return false;
}

void DX12ModRecordComputeUavs(
	ID3D12GraphicsCommandList *commandList, const std::vector<DX12CurrentComputeUavBinding> &uavs)
{
	if (!commandList || uavs.empty() || !DX12ModNeedsPreSkinningUavProbe())
		return;

	AcquireSRWLockExclusive(&gPreSkinLock);
	for (const DX12CurrentComputeUavBinding &uav : uavs)
	{
		DX12ComputeUavProducer producer;
		producer.serial = ++gComputeUavSerial;
		producer.rootParameterIndex = uav.rootParameterIndex;
		producer.rangeIndex = uav.rangeIndex;
		producer.shaderRegister = uav.shaderRegister;
		producer.registerSpace = uav.registerSpace;
		producer.descriptorOffset = uav.descriptorOffset;
		producer.rootDescriptor = uav.rootDescriptor;
		producer.gpuVirtualAddress = uav.gpuVirtualAddress;
		producer.hasDescriptor = uav.hasDescriptor;
		producer.descriptor = uav.descriptor;
		gRecentComputeUavs.push_back(std::move(producer));
	}
	while (gRecentComputeUavs.size() > MaxRecentComputeUavs)
	{
		gRecentComputeUavs.pop_front();
	}
	ReleaseSRWLockExclusive(&gPreSkinLock);
}

static bool NumericMatchSatisfied(const Bunny::NumericMatch &match, uint32_t actual)
{
	if (!match.enabled)
		return true;
	switch (match.op)
	{
	case Bunny::NumericMatchOp::Equal:
		return actual == match.value;
	case Bunny::NumericMatchOp::NotEqual:
		return actual != match.value;
	case Bunny::NumericMatchOp::Less:
		return actual < match.value;
	case Bunny::NumericMatchOp::LessEqual:
		return actual <= match.value;
	case Bunny::NumericMatchOp::Greater:
		return actual > match.value;
	case Bunny::NumericMatchOp::GreaterEqual:
		return actual >= match.value;
	default:
		return false;
	}
}

static bool TextureOverrideMatchesDrawContext(
	const Bunny::TextureOverrideConfig &config,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance)
{
	if (config.hasMatchVertexCount && !NumericMatchSatisfied(config.matchVertexCount, vertexCount))
		return false;
	if (config.hasMatchIndexCount && !NumericMatchSatisfied(config.matchIndexCount, indexCount))
		return false;
	if (config.hasMatchInstanceCount && !NumericMatchSatisfied(config.matchInstanceCount, instanceCount))
		return false;
	if (config.hasMatchFirstVertex && !NumericMatchSatisfied(config.matchFirstVertex, firstVertex))
		return false;
	if (config.hasMatchFirstIndex && !NumericMatchSatisfied(config.matchFirstIndex, firstIndex))
		return false;
	if (config.hasMatchFirstInstance && !NumericMatchSatisfied(config.matchFirstInstance, firstInstance))
		return false;
	return true;
}

class DX12TextureOverrideDrawKeyUsage
{
public:
	bool useVertexCount = false;
	bool useIndexCount = false;
	bool useInstanceCount = false;
	bool useFirstVertex = false;
	bool useFirstIndex = false;
	bool useFirstInstance = false;

	void MergeConfig(const Bunny::TextureOverrideConfig &config)
	{
		useVertexCount = useVertexCount || config.hasMatchVertexCount;
		useIndexCount = useIndexCount || config.hasMatchIndexCount;
		useInstanceCount = useInstanceCount || config.hasMatchInstanceCount;
		useFirstVertex = useFirstVertex || config.hasMatchFirstVertex;
		useFirstIndex = useFirstIndex || config.hasMatchFirstIndex;
		useFirstInstance = useFirstInstance || config.hasMatchFirstInstance;
	}
};

static DX12TextureOverrideDrawKeyUsage BuildTriggeredDrawKeyUsage(
	const std::vector<DX12TriggeredTextureOverride> *candidateTriggers)
{
	DX12TextureOverrideDrawKeyUsage usage;
	if (!candidateTriggers)
		return usage;
	for (const DX12TriggeredTextureOverride &triggered : *candidateTriggers) {
		if (!triggered.config)
			continue;
		usage.MergeConfig(*triggered.config);
	}
	return usage;
}

static void AppendDescriptorTriggeredPrefilterSectionIds(
	const std::vector<DX12TriggeredTextureOverride> &source,
	std::vector<uint32_t> *sectionIds)
{
	if (!sectionIds)
		return;
	for (const DX12TriggeredTextureOverride &triggered : source) {
		if (!triggered.config || !triggered.sectionId)
			continue;
		sectionIds->push_back(triggered.sectionId);
	}
}

static bool HasPotentialDescriptorTriggeredPrefilterCandidates(
	const DX12CommandExecutionRuntimeSnapshot &snapshot,
	uint32_t vertexCount,
	uint32_t indexCount)
{
	if (!snapshot.textureOverridesWithoutDrawContext.empty() ||
	    !snapshot.textureOverridesGenericDrawContext.empty())
		return true;
	if (indexCount) {
		auto indexed = snapshot.textureOverridesByIndexCount.find(indexCount);
		if (indexed != snapshot.textureOverridesByIndexCount.end() && !indexed->second.empty())
			return true;
	}
	if (vertexCount) {
		auto vertex = snapshot.textureOverridesByVertexCount.find(vertexCount);
		if (vertex != snapshot.textureOverridesByVertexCount.end() && !vertex->second.empty())
			return true;
	}
	return false;
}

static void CollectDescriptorTriggeredPrefilterSectionIds(
	const DX12CommandExecutionRuntimeSnapshot &snapshot,
	uint32_t vertexCount,
	uint32_t indexCount,
	std::vector<uint32_t> *sectionIds)
{
	if (!sectionIds)
		return;
	sectionIds->clear();
	AppendDescriptorTriggeredPrefilterSectionIds(
		snapshot.textureOverridesWithoutDrawContext,
		sectionIds);
	AppendDescriptorTriggeredPrefilterSectionIds(
		snapshot.textureOverridesGenericDrawContext,
		sectionIds);
	if (indexCount) {
		auto indexed = snapshot.textureOverridesByIndexCount.find(indexCount);
		if (indexed != snapshot.textureOverridesByIndexCount.end())
			AppendDescriptorTriggeredPrefilterSectionIds(indexed->second, sectionIds);
	}
	if (vertexCount) {
		auto vertex = snapshot.textureOverridesByVertexCount.find(vertexCount);
		if (vertex != snapshot.textureOverridesByVertexCount.end())
			AppendDescriptorTriggeredPrefilterSectionIds(vertex->second, sectionIds);
	}
	std::sort(sectionIds->begin(), sectionIds->end());
	sectionIds->erase(std::unique(sectionIds->begin(), sectionIds->end()), sectionIds->end());
}

static bool TextureOverrideHasDrawKind(
	const Bunny::TextureOverrideConfig &config, Bunny::CommandListActionKind kind)
{
	for (const Bunny::CommandListAction &action : config.actions)
	{
		if (action.kind == kind)
			return true;
	}
	return false;
}

static bool TextureOverrideHasDrawContextMatch(const Bunny::TextureOverrideConfig &config)
{
	return config.hasMatchVertexCount || config.hasMatchIndexCount ||
		   config.hasMatchInstanceCount || config.hasMatchFirstVertex ||
		   config.hasMatchFirstIndex || config.hasMatchFirstInstance;
}

static void SetCompiledVertexResourceBinding(
	std::vector<DX12CompiledVertexResourceBinding> *bindings,
	uint32_t slot,
	const std::wstring &resource)
{
	if (!bindings)
		return;
	for (DX12CompiledVertexResourceBinding &binding : *bindings)
	{
		if (binding.slot != slot)
			continue;
		binding.resource = resource;
		return;
	}
	bindings->push_back({slot, resource});
	std::sort(bindings->begin(), bindings->end(),
			  [](const DX12CompiledVertexResourceBinding &lhs,
				 const DX12CompiledVertexResourceBinding &rhs)
			  {
				  return lhs.slot < rhs.slot;
			  });
}

static std::vector<DX12CompiledVertexResourceBinding> CompileVertexResourceBindings(
	const std::map<uint32_t, std::wstring> &vertexResources)
{
	std::vector<DX12CompiledVertexResourceBinding> bindings;
	bindings.reserve(vertexResources.size());
	for (const auto &item : vertexResources)
		bindings.push_back({item.first, item.second});
	return bindings;
}

static void AppendCompiledDrawSrvResourceBindings(
	std::vector<DX12CompiledDrawSrvResourceBinding> *bindings,
	const std::map<uint32_t, std::wstring> &resources,
	bool pixelStage)
{
	if (!bindings)
		return;
	for (const auto &item : resources) {
		DX12CompiledDrawSrvResourceBinding binding;
		binding.slot = item.first;
		binding.resource = item.second;
		binding.pixelStage = pixelStage;
		bindings->push_back(std::move(binding));
	}
}

static bool TextureOverrideMatchesIaBinding(
	const Bunny::TextureOverrideConfig &config, bool indexBuffer, uint32_t vertexSlot)
{
	if (config.hash == 0 && TextureOverrideHasDrawContextMatch(config) &&
		config.indexBufferResource.empty() && config.vertexBufferResources.empty())
	{
		if (indexBuffer)
			return !TextureOverrideHasDrawKind(config, Bunny::CommandListActionKind::Draw);
		return TextureOverrideHasDrawKind(config, Bunny::CommandListActionKind::Draw);
	}

	if (indexBuffer)
	{
		if (config.indexBufferResource.empty() &&
			!TextureOverrideHasDrawKind(config, Bunny::CommandListActionKind::DrawIndexed))
			return false;
		return true;
	}

	if (config.vertexBufferResources.empty())
		return false;
	if (config.vertexBufferResources.find(vertexSlot) == config.vertexBufferResources.end())
		return false;
	return true;
}

static UINT64 MakeIaVertexIndexKey(uint32_t hash, uint32_t vertexSlot)
{
	return (static_cast<UINT64>(hash) << 32) | vertexSlot;
}

static bool NumericMatchIsExactValue(const Bunny::NumericMatch &match)
{
	return match.enabled && match.op == Bunny::NumericMatchOp::Equal;
}

static void BuildDx12IaTextureOverrideIndex(
	const Bunny::TextureOverrideMap &textureOverrides,
	const std::unordered_map<std::wstring, uint32_t> &textureOverrideSectionIds,
	const std::unordered_map<std::wstring, bool> &textureOverridePreEffect,
	const std::unordered_map<std::wstring, bool> &textureOverridePostEffect,
	std::vector<DX12IaTextureOverrideCandidate> *iaTextureOverrides,
	std::unordered_map<uint32_t, std::vector<size_t>> *indexTextureOverrideIndex,
	std::unordered_map<UINT64, std::vector<size_t>> *vertexTextureOverrideIndex,
	std::unordered_map<uint32_t, std::vector<size_t>> *anyVertexTextureOverrideIndex,
	std::unordered_map<UINT64, std::vector<size_t>> *mergedVertexTextureOverrideIndex,
	std::vector<size_t> *indexTextureOverridePrefilterIndex,
	std::unordered_map<uint32_t, std::vector<size_t>> *indexTextureOverridePrefilterByIndexCount,
	std::unordered_map<uint32_t, std::vector<size_t>> *vertexTextureOverridePrefilterIndex,
	std::vector<size_t> *anyVertexTextureOverridePrefilterIndex,
	std::unordered_map<uint32_t, std::vector<size_t>> *anyVertexTextureOverridePrefilterByVertexCount,
	std::unordered_map<uint32_t, std::vector<size_t>> *mergedVertexTextureOverridePrefilterIndex,
	std::unordered_map<UINT64, std::vector<size_t>> *vertexTextureOverridePrefilterByVertexCount,
	std::unordered_map<UINT64, std::vector<size_t>> *mergedVertexTextureOverridePrefilterByVertexCount)
{
	if (!iaTextureOverrides || !indexTextureOverrideIndex ||
		!vertexTextureOverrideIndex || !anyVertexTextureOverrideIndex ||
		!mergedVertexTextureOverrideIndex ||
		!indexTextureOverridePrefilterIndex ||
		!indexTextureOverridePrefilterByIndexCount ||
		!vertexTextureOverridePrefilterIndex ||
		!anyVertexTextureOverridePrefilterIndex ||
		!anyVertexTextureOverridePrefilterByVertexCount ||
		!mergedVertexTextureOverridePrefilterIndex ||
		!vertexTextureOverridePrefilterByVertexCount ||
		!mergedVertexTextureOverridePrefilterByVertexCount)
		return;

	iaTextureOverrides->clear();
	indexTextureOverrideIndex->clear();
	vertexTextureOverrideIndex->clear();
	anyVertexTextureOverrideIndex->clear();
	mergedVertexTextureOverrideIndex->clear();
	indexTextureOverridePrefilterIndex->clear();
	indexTextureOverridePrefilterByIndexCount->clear();
	vertexTextureOverridePrefilterIndex->clear();
	anyVertexTextureOverridePrefilterIndex->clear();
	anyVertexTextureOverridePrefilterByVertexCount->clear();
	mergedVertexTextureOverridePrefilterIndex->clear();
	vertexTextureOverridePrefilterByVertexCount->clear();
	mergedVertexTextureOverridePrefilterByVertexCount->clear();
	UINT64 order = 0;
	for (const auto &bucket : textureOverrides)
	{
		for (const Bunny::TextureOverrideConfig &config : bucket.second)
		{
			DX12IaTextureOverrideCandidate candidate;
			candidate.config = &config;
			candidate.order = order++;
			auto sectionId = textureOverrideSectionIds.find(config.section);
			candidate.sectionId =
				sectionId != textureOverrideSectionIds.end() ? sectionId->second : 0;
			candidate.preRuntimeEffect =
				LookupRuntimeEffect(textureOverridePreEffect, config.section);
			candidate.postRuntimeEffect =
				LookupRuntimeEffect(textureOverridePostEffect, config.section);
			const size_t index = iaTextureOverrides->size();
			iaTextureOverrides->push_back(std::move(candidate));

			if (TextureOverrideMatchesIaBinding(config, true, 0)) {
				(*indexTextureOverrideIndex)[config.hash].push_back(index);
				if (config.hasMatchIndexCount &&
				    NumericMatchIsExactValue(config.matchIndexCount)) {
					(*indexTextureOverridePrefilterByIndexCount)[
						config.matchIndexCount.value].push_back(index);
				} else {
					indexTextureOverridePrefilterIndex->push_back(index);
				}
			}

			bool indexedSpecificVertexSlot = false;
			for (const auto &slotResource : config.vertexBufferResources)
			{
				if (!TextureOverrideMatchesIaBinding(config, false, slotResource.first))
					continue;
				(*vertexTextureOverrideIndex)[MakeIaVertexIndexKey(config.hash, slotResource.first)].push_back(index);
				if (config.hasMatchVertexCount &&
				    NumericMatchIsExactValue(config.matchVertexCount)) {
					(*vertexTextureOverridePrefilterByVertexCount)[
						MakeIaVertexIndexKey(
							config.matchVertexCount.value,
							slotResource.first)].push_back(index);
				} else {
					(*vertexTextureOverridePrefilterIndex)[slotResource.first].push_back(index);
				}
				indexedSpecificVertexSlot = true;
			}
			if (!indexedSpecificVertexSlot &&
				TextureOverrideMatchesIaBinding(config, false, 0))
			{
				(*anyVertexTextureOverrideIndex)[config.hash].push_back(index);
				if (config.hasMatchVertexCount &&
				    NumericMatchIsExactValue(config.matchVertexCount)) {
					(*anyVertexTextureOverridePrefilterByVertexCount)[
						config.matchVertexCount.value].push_back(index);
				} else {
					anyVertexTextureOverridePrefilterIndex->push_back(index);
				}
			}
		}
	}

	auto sortCandidates = [iaTextureOverrides](std::vector<size_t> *indexes)
	{
		if (!indexes)
			return;
		std::sort(indexes->begin(), indexes->end(),
				  [iaTextureOverrides](size_t lhs, size_t rhs)
				  {
					  const DX12IaTextureOverrideCandidate &left = (*iaTextureOverrides)[lhs];
					  const DX12IaTextureOverrideCandidate &right = (*iaTextureOverrides)[rhs];
					  if (!left.config || !right.config)
						  return left.config != nullptr;
					  if (left.config->matchPriority != right.config->matchPriority)
						  return left.config->matchPriority > right.config->matchPriority;
					  const std::wstring leftSection = Bunny::IniDocumentText::ToLower(left.config->section);
					  const std::wstring rightSection = Bunny::IniDocumentText::ToLower(right.config->section);
					  if (leftSection != rightSection)
						  return leftSection < rightSection;
					  return left.order < right.order;
				  });
	};

	for (auto &bucket : *indexTextureOverrideIndex)
		sortCandidates(&bucket.second);
	for (auto &bucket : *vertexTextureOverrideIndex)
		sortCandidates(&bucket.second);
	for (auto &bucket : *anyVertexTextureOverrideIndex)
		sortCandidates(&bucket.second);
	sortCandidates(indexTextureOverridePrefilterIndex);
	for (auto &bucket : *indexTextureOverridePrefilterByIndexCount)
		sortCandidates(&bucket.second);
	for (auto &bucket : *vertexTextureOverridePrefilterIndex)
		sortCandidates(&bucket.second);
	sortCandidates(anyVertexTextureOverridePrefilterIndex);
	for (auto &bucket : *anyVertexTextureOverridePrefilterByVertexCount)
		sortCandidates(&bucket.second);
	for (auto &bucket : *vertexTextureOverridePrefilterByVertexCount)
		sortCandidates(&bucket.second);

	for (const auto &bucket : *vertexTextureOverrideIndex)
	{
		std::vector<size_t> merged = bucket.second;
		const uint32_t hash = static_cast<uint32_t>(bucket.first >> 32);
		auto anySlot = anyVertexTextureOverrideIndex->find(hash);
		if (anySlot != anyVertexTextureOverrideIndex->end())
			merged.insert(merged.end(), anySlot->second.begin(), anySlot->second.end());
		sortCandidates(&merged);
		(*mergedVertexTextureOverrideIndex)[bucket.first] = std::move(merged);
	}

	for (const auto &bucket : *vertexTextureOverridePrefilterIndex)
	{
		std::vector<size_t> merged = bucket.second;
		if (!anyVertexTextureOverridePrefilterIndex->empty()) {
			merged.insert(
				merged.end(),
				anyVertexTextureOverridePrefilterIndex->begin(),
				anyVertexTextureOverridePrefilterIndex->end());
		}
		sortCandidates(&merged);
		(*mergedVertexTextureOverridePrefilterIndex)[bucket.first] =
			std::move(merged);
	}
	for (const auto &bucket : *vertexTextureOverridePrefilterByVertexCount)
	{
		std::vector<size_t> merged = bucket.second;
		const uint32_t vertexCount = static_cast<uint32_t>(bucket.first >> 32);
		auto anySlot = anyVertexTextureOverridePrefilterByVertexCount->find(vertexCount);
		if (anySlot != anyVertexTextureOverridePrefilterByVertexCount->end()) {
			merged.insert(
				merged.end(),
				anySlot->second.begin(),
				anySlot->second.end());
		}
		sortCandidates(&merged);
		(*mergedVertexTextureOverridePrefilterByVertexCount)[bucket.first] =
			std::move(merged);
	}
}

static const std::vector<size_t> *FindIaTextureOverridePrefilterCandidatesLocked(
	bool indexBuffer, uint32_t vertexSlot, uint32_t vertexCount, uint32_t indexCount,
	std::vector<size_t> *scratch)
{
	if (scratch)
		scratch->clear();

	if (indexBuffer) {
		auto indexedByCount =
			gIaIndexTextureOverridePrefilterByIndexCount.find(indexCount);
		if (indexedByCount != gIaIndexTextureOverridePrefilterByIndexCount.end()) {
			// The exact-count bucket and the generic list (candidates without
			// an exact match_index_count) are mutually exclusive in the build,
			// but generic overrides match every draw count. Returning only the
			// exact-count bucket starves generic overrides whenever a draw
			// happens to use a configured exact count, which shows up as
			// intermittent missing matches (flicker). The buckets are disjoint
			// by construction, so no dedup is required.
			if (!scratch)
				return &indexedByCount->second;
			*scratch = indexedByCount->second;
			if (!gIaIndexTextureOverridePrefilterIndex.empty())
				scratch->insert(scratch->end(),
					gIaIndexTextureOverridePrefilterIndex.begin(),
					gIaIndexTextureOverridePrefilterIndex.end());
			return scratch;
		}
		return &gIaIndexTextureOverridePrefilterIndex;
	}

	auto mergedByCount = gIaMergedVertexTextureOverridePrefilterByVertexCount.find(
		MakeIaVertexIndexKey(vertexCount, vertexSlot));
	if (mergedByCount != gIaMergedVertexTextureOverridePrefilterByVertexCount.end()) {
		// Same rationale as the index side: also consider the generic merged
		// candidates for this slot, which match any vertex count.
		if (!scratch)
			return &mergedByCount->second;
		*scratch = mergedByCount->second;
		auto mergedGeneric =
			gIaMergedVertexTextureOverridePrefilterIndex.find(vertexSlot);
		if (mergedGeneric != gIaMergedVertexTextureOverridePrefilterIndex.end())
			scratch->insert(scratch->end(),
				mergedGeneric->second.begin(),
				mergedGeneric->second.end());
		return scratch;
	}

	auto anySlotByCount =
		gIaAnyVertexTextureOverridePrefilterByVertexCount.find(vertexCount);
	if (anySlotByCount != gIaAnyVertexTextureOverridePrefilterByVertexCount.end()) {
		// Merge in the generic any-slot candidates, which match any count.
		if (!scratch)
			return &anySlotByCount->second;
		*scratch = anySlotByCount->second;
		if (!gIaAnyVertexTextureOverridePrefilterIndex.empty())
			scratch->insert(scratch->end(),
				gIaAnyVertexTextureOverridePrefilterIndex.begin(),
				gIaAnyVertexTextureOverridePrefilterIndex.end());
		return scratch;
	}

	auto merged = gIaMergedVertexTextureOverridePrefilterIndex.find(vertexSlot);
	if (merged != gIaMergedVertexTextureOverridePrefilterIndex.end())
		return &merged->second;

	if (!gIaAnyVertexTextureOverridePrefilterIndex.empty())
		return &gIaAnyVertexTextureOverridePrefilterIndex;

	auto specific = gIaVertexTextureOverridePrefilterIndex.find(vertexSlot);
	return specific != gIaVertexTextureOverridePrefilterIndex.end() ?
		&specific->second : nullptr;
}

static bool AppendActionToCompiledPlan(
	const Bunny::CommandListAction &action,
	DX12CompiledCommandListPlan *plan)
{
	if (!plan)
		return false;

	auto targetNeedsDescriptorBindings = [](const Bunny::CommandListTarget &target) {
		return target.kind == Bunny::CommandListTargetKind::ConstantBuffer ||
			target.kind == Bunny::CommandListTargetKind::ShaderResource ||
			target.kind == Bunny::CommandListTargetKind::UnorderedAccessView;
	};
	auto appendUniqueDescriptorCheckTarget = [plan](const Bunny::CommandListTarget &target) {
		for (const Bunny::CommandListTarget &existing : plan->descriptorCheckTargets) {
			if (existing.kind == target.kind &&
				existing.stage == target.stage &&
				existing.slot == target.slot) {
				return;
			}
		}
		plan->descriptorCheckTargets.push_back(target);
		plan->hasDescriptorCheckTargets = true;
	};

	switch (action.kind)
	{
	case Bunny::CommandListActionKind::HandlingSkip:
		plan->handlingSkip = true;
		{
			DX12CompiledCommandListPlan::Op op;
			op.kind = DX12CompiledCommandListPlan::Op::Kind::HandlingSkip;
			plan->ops.push_back(std::move(op));
		}
		return true;
	case Bunny::CommandListActionKind::SetIndexBuffer:
		plan->indexResource = action.resource;
		{
			DX12CompiledCommandListPlan::Op op;
			op.kind = DX12CompiledCommandListPlan::Op::Kind::SetIndexBuffer;
			op.resource = action.resource;
			plan->ops.push_back(std::move(op));
		}
		return true;
	case Bunny::CommandListActionKind::SetVertexBuffer:
		SetCompiledVertexResourceBinding(
			&plan->vertexResources, action.target.slot, action.resource);
		{
			DX12CompiledCommandListPlan::Op op;
			op.kind = DX12CompiledCommandListPlan::Op::Kind::SetVertexBuffer;
			op.target = action.target;
			op.resource = action.resource;
			plan->ops.push_back(std::move(op));
		}
		return true;
	case Bunny::CommandListActionKind::SetShaderResource:
	{
		DX12CompiledCommandListPlan::Op op;
		op.kind = DX12CompiledCommandListPlan::Op::Kind::SetShaderResource;
		op.target = action.target;
		op.resource = action.resource;
		plan->ops.push_back(std::move(op));
		return true;
	}
	case Bunny::CommandListActionKind::SetConstantBuffer:
	{
		DX12CompiledCommandListPlan::Op op;
		op.kind = DX12CompiledCommandListPlan::Op::Kind::SetConstantBuffer;
		op.target = action.target;
		op.resource = action.resource;
		plan->ops.push_back(std::move(op));
		return true;
	}
	case Bunny::CommandListActionKind::SetUnorderedAccessView:
	{
		DX12CompiledCommandListPlan::Op op;
		op.kind = DX12CompiledCommandListPlan::Op::Kind::SetUnorderedAccessView;
		op.target = action.target;
		op.resource = action.resource;
		plan->ops.push_back(std::move(op));
		return true;
	}
	case Bunny::CommandListActionKind::Draw:
	case Bunny::CommandListActionKind::DrawIndexed:
	case Bunny::CommandListActionKind::DrawIndexedAuto:
	case Bunny::CommandListActionKind::DrawFromCaller:
	case Bunny::CommandListActionKind::DrawIndexedFromCaller:
	{
		DX12ModIaReplacement::DrawCall draw;
		draw.indexed = (action.kind == Bunny::CommandListActionKind::DrawIndexed ||
						action.kind == Bunny::CommandListActionKind::DrawIndexedAuto ||
						action.kind == Bunny::CommandListActionKind::DrawIndexedFromCaller);
		draw.fromCaller = (action.kind == Bunny::CommandListActionKind::DrawFromCaller ||
						   action.kind == Bunny::CommandListActionKind::DrawIndexedFromCaller);
		draw.autoCount = (action.kind == Bunny::CommandListActionKind::DrawIndexedAuto);
		if (!draw.fromCaller && !draw.autoCount)
		{
			draw.count = action.args[0];
			draw.start = action.args[1];
			draw.baseVertex = draw.indexed ? static_cast<INT>(action.args[2]) : 0;
		}
		plan->draws.push_back(draw);
		{
			DX12CompiledCommandListPlan::Op op;
			op.kind = DX12CompiledCommandListPlan::Op::Kind::Draw;
			op.draw = draw;
			plan->ops.push_back(std::move(op));
		}
		return true;
	}
	case Bunny::CommandListActionKind::Dispatch:
	{
		DX12ModIaReplacement::DispatchCall dispatch;
		dispatch.groupsX = action.args[0];
		dispatch.groupsY = action.args[1];
		dispatch.groupsZ = action.args[2];
		plan->dispatches.push_back(dispatch);
		{
			DX12CompiledCommandListPlan::Op op;
			op.kind = DX12CompiledCommandListPlan::Op::Kind::Dispatch;
			op.dispatch = dispatch;
			plan->ops.push_back(std::move(op));
		}
		return true;
	}
	case Bunny::CommandListActionKind::CheckTextureOverride:
	{
		DX12CompiledCommandListPlan::Op op;
		op.kind = DX12CompiledCommandListPlan::Op::Kind::CheckTextureOverride;
		op.target = action.target;
		plan->ops.push_back(std::move(op));
		if (targetNeedsDescriptorBindings(action.target))
			appendUniqueDescriptorCheckTarget(action.target);
		return true;
	}
	default:
		return false;
	}
}

static void AppendCompiledCommandListPlan(
	DX12CompiledCommandListPlan *dst, const DX12CompiledCommandListPlan &src)
{
	if (!dst)
		return;
	dst->handlingSkip = dst->handlingSkip || src.handlingSkip;
	dst->hasDescriptorCheckTargets =
		dst->hasDescriptorCheckTargets || src.hasDescriptorCheckTargets;
	if (!src.indexResource.empty())
		dst->indexResource = src.indexResource;
	for (const auto &item : src.vertexResources)
		SetCompiledVertexResourceBinding(&dst->vertexResources, item.slot, item.resource);
	for (const Bunny::CommandListTarget &target : src.descriptorCheckTargets) {
		bool exists = false;
		for (const Bunny::CommandListTarget &existing : dst->descriptorCheckTargets) {
			if (existing.kind == target.kind &&
				existing.stage == target.stage &&
				existing.slot == target.slot) {
				exists = true;
				break;
			}
		}
		if (!exists)
			dst->descriptorCheckTargets.push_back(target);
	}
	dst->draws.insert(dst->draws.end(), src.draws.begin(), src.draws.end());
	dst->dispatches.insert(dst->dispatches.end(), src.dispatches.begin(), src.dispatches.end());
	dst->ops.insert(dst->ops.end(), src.ops.begin(), src.ops.end());
}

static bool AppendCompiledCommandListLinksPlan(
	const std::vector<std::wstring> &links,
	const std::unordered_map<std::wstring, DX12CompiledCommandListPlan> &compiledCommandListPlans,
	DX12CompiledCommandListPlan *dst)
{
	if (!dst)
		return false;
	bool appended = false;
	for (const std::wstring &link : links) {
		auto commandPlan = compiledCommandListPlans.find(link);
		if (commandPlan == compiledCommandListPlans.end() ||
		    commandPlan->second.unsupported) {
			return false;
		}
		AppendCompiledCommandListPlan(dst, commandPlan->second);
		appended = true;
	}
	return appended;
}

static void AppendLegacyTextureOverrideStateToPlan(
	const Bunny::TextureOverrideConfig &config,
	DX12CompiledCommandListPlan *plan)
{
	if (!plan)
		return;

	if (config.handlingSkip) {
		DX12CompiledCommandListPlan::Op op;
		op.kind = DX12CompiledCommandListPlan::Op::Kind::HandlingSkip;
		plan->handlingSkip = true;
		plan->ops.push_back(std::move(op));
	}

	if (!config.indexBufferResource.empty()) {
		DX12CompiledCommandListPlan::Op op;
		op.kind = DX12CompiledCommandListPlan::Op::Kind::SetIndexBuffer;
		op.resource = config.indexBufferResource;
		plan->indexResource = config.indexBufferResource;
		plan->ops.push_back(std::move(op));
	}

	for (const auto &item : config.vertexBufferResources) {
		DX12CompiledCommandListPlan::Op op;
		op.kind = DX12CompiledCommandListPlan::Op::Kind::SetVertexBuffer;
		op.target.kind = Bunny::CommandListTargetKind::VertexBuffer;
		op.target.slot = item.first;
		op.resource = item.second;
		SetCompiledVertexResourceBinding(&plan->vertexResources, item.first, item.second);
		plan->ops.push_back(std::move(op));
	}
}


static void BuildCompiledShaderOverridePlanFromParts(
	bool handlingSkip,
	const Bunny::CommandListLinks &commandLists,
	const std::vector<Bunny::CommandListAction> &actions,
	const std::unordered_map<std::wstring, DX12CompiledCommandListPlan> &compiledCommandListPlans,
	DX12CompiledShaderOverridePlan *plan)
{
	if (!plan)
		return;
	*plan = DX12CompiledShaderOverridePlan();
	plan->mainPlan.handlingSkip = handlingSkip;
	if (!commandLists.pre.empty() &&
	    !AppendCompiledCommandListLinksPlan(
			commandLists.pre, compiledCommandListPlans, &plan->mainPlan)) {
		plan->unsupported = true;
		return;
	}
	if (!commandLists.main.empty() &&
	    !AppendCompiledCommandListLinksPlan(
			commandLists.main, compiledCommandListPlans, &plan->mainPlan)) {
		plan->unsupported = true;
		return;
	}
	for (const Bunny::CommandListAction &action : actions) {
		if (action.kind == Bunny::CommandListActionKind::Run) {
			auto commandPlan = compiledCommandListPlans.find(action.commandList);
			if (commandPlan == compiledCommandListPlans.end() ||
			    commandPlan->second.unsupported) {
				plan->unsupported = true;
				return;
			}
			AppendCompiledCommandListPlan(&plan->mainPlan, commandPlan->second);
			continue;
		}
		if (!AppendActionToCompiledPlan(action, &plan->mainPlan)) {
			plan->unsupported = true;
			return;
		}
	}
	if (!commandLists.post.empty()) {
		plan->hasPostCommandListPlan = true;
		plan->postCommandLists = commandLists.post;
		if (!AppendCompiledCommandListLinksPlan(
				commandLists.post, compiledCommandListPlans,
				&plan->postCommandListPlan)) {
			plan->unsupported = true;
			return;
		}
	}
}

static bool CompileCommandListPlanRecursive(
	const std::wstring &name,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, DX12CompiledCommandListPlan> *compiledPlans,
	std::unordered_set<std::wstring> *visiting,
	DX12CompiledCommandListPlan *outPlan)
{
	if (!compiledPlans || !visiting || !outPlan)
		return false;

	auto cached = compiledPlans->find(name);
	if (cached != compiledPlans->end())
	{
		if (cached->second.unsupported)
			return false;
		*outPlan = cached->second;
		return true;
	}
	if (!visiting->insert(name).second)
		return false;

	auto listIt = commandLists.find(name);
	if (listIt == commandLists.end())
	{
		visiting->erase(name);
		return false;
	}

	DX12CompiledCommandListPlan plan;
	for (const Bunny::CommandListAction &action : listIt->second.actions)
	{
		if (action.kind == Bunny::CommandListActionKind::Run)
		{
			DX12CompiledCommandListPlan childPlan;
			if (!CompileCommandListPlanRecursive(
					action.commandList, commandLists, compiledPlans, visiting, &childPlan))
			{
				plan.unsupported = true;
				break;
			}
			AppendCompiledCommandListPlan(&plan, childPlan);
			continue;
		}
		if (!AppendActionToCompiledPlan(action, &plan))
		{
			plan.unsupported = true;
			break;
		}
	}

	visiting->erase(name);
	(*compiledPlans)[name] = plan;
	if (plan.unsupported)
		return false;
	*outPlan = plan;
	return true;
}

static void BuildDx12CompiledCommandListPlans(
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, DX12CompiledCommandListPlan> *compiledPlans)
{
	if (!compiledPlans)
		return;

	compiledPlans->clear();
	for (const auto &item : commandLists)
	{
		std::unordered_set<std::wstring> visiting;
		DX12CompiledCommandListPlan plan;
		CompileCommandListPlanRecursive(
			item.first, commandLists, compiledPlans, &visiting, &plan);
	}
}

static void BuildDx12CompiledTextureOverridePlans(
	const Bunny::TextureOverrideMap &textureOverrides,
	const std::unordered_map<std::wstring, uint32_t> &textureOverrideSectionIds,
	const std::unordered_map<std::wstring, DX12CompiledCommandListPlan> &compiledCommandListPlans,
	std::unordered_map<uint32_t, DX12CompiledTextureOverridePlan> *compiledPlans)
{
	if (!compiledPlans)
		return;

	compiledPlans->clear();
	for (const auto &bucket : textureOverrides)
	{
		for (const Bunny::TextureOverrideConfig &config : bucket.second)
		{
			auto sectionIdIt = textureOverrideSectionIds.find(config.section);
			if (sectionIdIt == textureOverrideSectionIds.end() || !sectionIdIt->second)
				continue;

			DX12CompiledTextureOverridePlan plan;
			plan.hash = config.hash;
			plan.hasActions = !config.actions.empty();
			plan.drawSrvResources.reserve(
				config.drawPsSrvResources.size() +
				config.drawVsSrvResources.size());
			AppendCompiledDrawSrvResourceBindings(
				&plan.drawSrvResources, config.drawPsSrvResources, true);
			AppendCompiledDrawSrvResourceBindings(
				&plan.drawSrvResources, config.drawVsSrvResources, false);
			plan.drawReplaceThisResource = config.drawReplaceThisResource;

			if (!config.commandLists.pre.empty()) {
				plan.hasPreCommandListPlan = true;
				if (!AppendCompiledCommandListLinksPlan(
						config.commandLists.pre,
						compiledCommandListPlans,
						&plan.preCommandListPlan)) {
					plan.hasUnsupportedCommandActions = true;
				}
			}

			const bool hasMainRuntimeWork =
				!config.actions.empty() ||
				!config.commandLists.main.empty();
			if (!hasMainRuntimeWork) {
				plan.handlingSkip = config.handlingSkip;
				plan.indexResource = config.indexBufferResource;
				plan.vertexResources =
					CompileVertexResourceBindings(config.vertexBufferResources);
			} else {
				plan.hasMainActionPlan = true;
				if (config.actions.empty()) {
					AppendLegacyTextureOverrideStateToPlan(config, &plan.mainActionPlan);
				} else {
					for (const Bunny::CommandListAction &action : config.actions) {
						switch (action.kind) {
						case Bunny::CommandListActionKind::Run: {
							auto commandPlan = compiledCommandListPlans.find(action.commandList);
							if (commandPlan == compiledCommandListPlans.end() ||
								commandPlan->second.unsupported) {
								plan.hasUnsupportedCommandActions = true;
								break;
							}
							AppendCompiledCommandListPlan(
								&plan.mainActionPlan, commandPlan->second);
							break;
						}
						default:
							if (!AppendActionToCompiledPlan(action, &plan.mainActionPlan))
								plan.hasUnsupportedCommandActions = true;
							break;
						}

						if (plan.hasUnsupportedCommandActions)
							break;
					}
				}

				if (!plan.hasUnsupportedCommandActions &&
				    !config.commandLists.main.empty() &&
				    !AppendCompiledCommandListLinksPlan(
						config.commandLists.main,
						compiledCommandListPlans,
						&plan.mainActionPlan)) {
					plan.hasUnsupportedCommandActions = true;
				}

				if (!plan.hasUnsupportedCommandActions) {
					plan.handlingSkip = plan.mainActionPlan.handlingSkip;
					plan.indexResource = plan.mainActionPlan.indexResource;
					plan.vertexResources = plan.mainActionPlan.vertexResources;
					plan.draws = plan.mainActionPlan.draws;
					plan.dispatches = plan.mainActionPlan.dispatches;
				} else {
					plan.hasMainActionPlan = false;
				}
			}

			if (!plan.hasUnsupportedCommandActions &&
			    !config.commandLists.post.empty()) {
				plan.hasPostCommandListPlan = true;
				if (!AppendCompiledCommandListLinksPlan(
						config.commandLists.post,
						compiledCommandListPlans,
						&plan.postCommandListPlan)) {
					plan.hasUnsupportedCommandActions = true;
				}
			}

			(*compiledPlans)[sectionIdIt->second] = std::move(plan);
		}
	}
}

static void BuildDx12CompiledShaderOverridePlans(
	const Bunny::ShaderOverrideMap &shaderOverrides,
	const Bunny::ShaderRegexMap &shaderRegexes,
	const std::unordered_map<std::wstring, DX12CompiledCommandListPlan> &compiledCommandListPlans,
	std::unordered_map<std::wstring, DX12CompiledShaderOverridePlan> *compiledPlans)
{
	if (!compiledPlans)
		return;
	compiledPlans->clear();
	for (const auto &item : shaderOverrides) {
		DX12CompiledShaderOverridePlan plan;
		BuildCompiledShaderOverridePlanFromParts(
			item.second.handlingSkip,
			item.second.commandLists,
			item.second.actions,
			compiledCommandListPlans,
			&plan);
		(*compiledPlans)[item.second.section] = std::move(plan);
	}
	for (const auto &item : shaderRegexes) {
		DX12CompiledShaderOverridePlan plan;
		BuildCompiledShaderOverridePlanFromParts(
			item.second.handlingSkip,
			item.second.commandLists,
			item.second.actions,
			compiledCommandListPlans,
			&plan);
		(*compiledPlans)[item.second.section] = std::move(plan);
	}
}

static void ReleaseLoadedResourcesLocked()
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

static void MoveExpiredRetiredLoadedResourcesLocked(
	std::vector<DX12RetiredLoadedResource> *expired)
{
	if (!expired)
		return;
	expired->clear();
	if (InterlockedCompareExchange(&gActiveLoadedResourceSnapshots, 0, 0) != 0)
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

static void ReleaseLoadedResourceObjects(DX12LoadedResource *resource)
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

static DX12IaHashState EmptyIaHashState();
static bool ApplyPendingResourceViewsForCommandList(
	ID3D12Device *device, ID3D12GraphicsCommandList *commandList,
	const DX12IaHashState &iaState,
	const std::vector<DX12PendingResourceViewRequest> &requests,
	DX12ModIaReplacement *replacement);
static bool RunNamedCommandList(
	ID3D12Device *device,
	ID3D12GraphicsCommandList *commandList,
	const std::wstring &name,
	DX12ModIaReplacement *replacement,
	std::vector<DX12PendingResourceViewRequest> *pendingResourceViews);

static bool TextureOverrideMatchCsSatisfied(
	const Bunny::TextureOverrideConfig &config,
	const std::unordered_set<std::wstring> &activePreSkinSections)
{
	if (!config.hasMatchCs && !config.hasCsHash)
		return true;
	return activePreSkinSections.find(config.section) != activePreSkinSections.end();
}

static bool TextureOverrideDependsOnActivePreSkin(
	const Bunny::TextureOverrideConfig &config)
{
	return config.hasMatchCs || config.hasCsHash;
}

static const std::unordered_set<std::wstring> &EmptyActivePreSkinSections()
{
	static const std::unordered_set<std::wstring> empty;
	return empty;
}

static bool IaCandidateIndexesDependOnActivePreSkin(
	const std::vector<size_t> &candidateIndexes)
{
	for (size_t index : candidateIndexes) {
		if (index >= gIaTextureOverrides.size())
			continue;
		const DX12IaTextureOverrideCandidate &candidate = gIaTextureOverrides[index];
		if (candidate.config &&
		    TextureOverrideDependsOnActivePreSkin(*candidate.config))
			return true;
	}
	return false;
}

static bool IaTriggeredCandidatesDependOnActivePreSkin(
	const std::vector<DX12TriggeredTextureOverride> &candidateTriggers)
{
	for (const DX12TriggeredTextureOverride &triggered : candidateTriggers) {
		if (triggered.config &&
		    TextureOverrideDependsOnActivePreSkin(*triggered.config))
			return true;
	}
	return false;
}

static void PreloadPreSkinReplacementResources(ID3D12Device *device)
{
	if (!device)
		return;
	// Collect every buffer-like mod resource under the shared mod lock
	// (stable within a reload generation), then load each once outside the
	// lock. This covers preSkinCsSrvResources, vertexBufferResources, VLR
	// candidates and any other lazy EnsureLoadedResource* path, and moves the
	// first-match synchronous file I/O + resource creation off the
	// command-recording thread (multi-second freeze) to the first frame after
	// the mod is (re)loaded.
	std::vector<std::wstring> names;
	{
		AcquireSRWLockShared(&gModLock);
		names.reserve(gModResources.size());
		for (const auto &entry : gModResources)
		{
			if (ResourceConfigLooksLikeBuffer(entry.second))
				names.push_back(entry.first);
		}
		ReleaseSRWLockShared(&gModLock);
	}
	std::sort(names.begin(), names.end());
	names.erase(std::unique(names.begin(), names.end()), names.end());
	for (const std::wstring &name : names)
	{
		DX12LoadedResource *resource =
			EnsureLoadedResourceForPreSkin(device, name);
		if (resource && resource->resource && resource->stride &&
			resource->byteWidth)
			EnsureResourceSrvLocked(
				device, resource, resource->stride, resource->byteWidth);
	}
}

static bool PreloadPreSkinOutputResizeCache(ID3D12Device *device)
{
	if (!device || gHasPreSkinTextureOverrideCandidates == 0)
		return false;

	struct PreSkinOutputResizeCandidate
	{
		UINT64 byteWidth = 0;
		UINT stride = 0;
	};
	std::vector<PreSkinOutputResizeCandidate> candidates;
	{
		AcquireSRWLockShared(&gModLock);
		candidates.reserve(gPreSkinTextureOverrides.size() * 2);
		for (const DX12PreSkinTextureOverrideCandidate &overrideCandidate :
				gPreSkinTextureOverrides)
		{
			const Bunny::TextureOverrideConfig *config = overrideCandidate.config;
			if (!config || !IsComputePreSkinTextureOverride(*config))
				continue;
			for (const auto &entry : config->preSkinCsSrvResources)
			{
				auto loaded = gLoadedResources.find(entry.second);
				if (loaded == gLoadedResources.end() || loaded->second.failed ||
				    !loaded->second.resource || !loaded->second.byteWidth ||
				    !loaded->second.stride)
					continue;
				candidates.push_back({
					loaded->second.byteWidth,
					loaded->second.stride});
			}
		}
		ReleaseSRWLockShared(&gModLock);
	}
	if (candidates.empty())
		return false;

	std::sort(candidates.begin(), candidates.end(),
		[](const PreSkinOutputResizeCandidate &a,
		   const PreSkinOutputResizeCandidate &b) {
			if (a.byteWidth != b.byteWidth)
				return a.byteWidth < b.byteWidth;
			return a.stride < b.stride;
		});
	candidates.erase(
		std::unique(candidates.begin(), candidates.end(),
			[](const PreSkinOutputResizeCandidate &a,
			   const PreSkinOutputResizeCandidate &b) {
				return a.byteWidth == b.byteWidth && a.stride == b.stride;
			}),
		candidates.end());

	std::vector<DX12ResourceSummary> resources;
	DX12GetRetainedTrackedResourceSnapshot(&resources);
	bool madeProgress = false;
	for (const DX12ResourceSummary &resourceSummary : resources)
	{
		if (!resourceSummary.resource || !resourceSummary.hasResourceDesc ||
			resourceSummary.resourceDesc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER ||
			(resourceSummary.resourceDesc.Flags &
				D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) == 0)
			continue;

		for (const PreSkinOutputResizeCandidate &candidate : candidates)
		{
			if (!candidate.stride ||
				resourceSummary.resourceDesc.Width >= candidate.byteWidth ||
				resourceSummary.resourceDesc.Width + candidate.stride <
					candidate.byteWidth)
				continue;
			const UINT64 elementCount64 = candidate.byteWidth / candidate.stride;
			if (elementCount64 == 0 ||
				elementCount64 > static_cast<UINT64>(UINT_MAX))
				continue;

			ID3D12Resource *resizedUav = nullptr;
			ID3D12Resource *resizedVertex = nullptr;
			ID3D12DescriptorHeap *resizedHeap = nullptr;
			D3D12_CPU_DESCRIPTOR_HANDLE resizedCpu = {};
			bool cacheHit = false;
			UINT ringSlot = 0;
			if (!AcquirePreSkinOutputResizeCacheEntry(
					device, nullptr, resourceSummary.resource,
					candidate.byteWidth, candidate.stride,
					static_cast<UINT>(elementCount64),
					&resizedUav, &resizedVertex, &resizedHeap, &resizedCpu,
					&cacheHit, &ringSlot))
				continue;

			DX12ModRegisterPreSkinOutputResize(
				nullptr, resourceSummary.resource, resizedVertex,
				resourceSummary.resourceDesc.Width,
				candidate.byteWidth, candidate.stride);
			if (!cacheHit)
				madeProgress = true;
			if (resizedUav)
				resizedUav->Release();
			if (resizedVertex)
				resizedVertex->Release();
			if (resizedHeap)
				resizedHeap->Release();
			break;
		}
	}
	DX12ReleaseTrackedResourceSnapshot(&resources);
	return madeProgress;
}

void DX12ModBeginFrame(IDXGISwapChain *swapChain)
{
	DX12VmFrameVariablesUtils::UpdateForSwapChain(swapChain);
	ProcessDx12KeyBindingsForFrame();

	std::vector<DX12RetiredLoadedResource> expired;
	std::vector<ID3D12Resource*> releasePreSkinResources;
	AcquireSRWLockExclusive(&gPreSkinLock);
	// Track idle frames: if no active pre-skin overrides existed last frame, count idle.
	// If pre-skin was actively matching, reset idle counter immediately.
	if (gActivePreSkinTextureOverrides.empty()) {
		LONG idle = InterlockedIncrement(&gPreSkinBindingIdleFrames);
		if (idle > kPreSkinBindingIdleMax)
			InterlockedExchange(&gPreSkinBindingIdleFrames, kPreSkinBindingIdleMax);
	} else {
		InterlockedExchange(&gPreSkinBindingIdleFrames, 0);
	}
	ClearActivePreSkinTextureOverridesLocked(&releasePreSkinResources);
	ReleaseSRWLockExclusive(&gPreSkinLock);

	AcquireSRWLockExclusive(&gModLock);
	MoveExpiredRetiredLoadedResourcesLocked(&expired);
	ReleaseSRWLockExclusive(&gModLock);
	for (DX12RetiredLoadedResource &resource : expired)
		ReleaseLoadedResourceObjects(&resource.resource);
	ReleaseMovedPreSkinResources(&releasePreSkinResources);

	// Preload pre-skin replacement resources once per mod generation so the
	// first match does not pay synchronous file I/O + resource creation on
	// the command-recording thread.
	{
		static UINT64 sPreSkinResourcesPreloadedGeneration = 0;
		static UINT64 sOutputResizePreloadGeneration = 0;
		static LONG sOutputResizePreloadIdleFrames = 0;
		const UINT64 preloadGeneration = DX12ModGetReloadGenerationFast();
		if (gHasPreSkinTextureOverrideCandidates != 0 &&
			sPreSkinResourcesPreloadedGeneration != preloadGeneration)
		{
			ID3D12Device *preloadDevice = nullptr;
			if (swapChain &&
				SUCCEEDED(swapChain->GetDevice(IID_PPV_ARGS(&preloadDevice))) &&
				preloadDevice)
			{
				PreloadPreSkinReplacementResources(preloadDevice);
				preloadDevice->Release();
				sPreSkinResourcesPreloadedGeneration = preloadGeneration;
			}
		}
		if (gHasPreSkinTextureOverrideCandidates != 0)
		{
			if (sOutputResizePreloadGeneration != preloadGeneration)
			{
				sOutputResizePreloadGeneration = preloadGeneration;
				sOutputResizePreloadIdleFrames = 0;
			}
			if (sOutputResizePreloadIdleFrames < 30)
			{
				ID3D12Device *preloadDevice = nullptr;
				if (swapChain &&
					SUCCEEDED(swapChain->GetDevice(IID_PPV_ARGS(&preloadDevice))) &&
					preloadDevice)
				{
					const bool progress =
						PreloadPreSkinOutputResizeCache(preloadDevice);
					sOutputResizePreloadIdleFrames =
						progress ? 0 : sOutputResizePreloadIdleFrames + 1;
					preloadDevice->Release();
				}
			}
		}
	}

}

bool DX12ModPreparePresentReplacement(
	ID3D12GraphicsCommandList *commandList, DX12ModIaReplacement *replacement)
{
	if (!commandList || !replacement)
		return false;
	if (!DX12ModNeedsPresentReplacement())
		return false;

	const LONG present = DX12RuntimeState::GetPresentCount();
	for (;;) {
		const LONG observed = InterlockedCompareExchange(
			&gPresentCommandListExecutedPresent, 0, 0);
		if (observed == present)
			return false;
		if (InterlockedCompareExchange(
			    &gPresentCommandListExecutedPresent, present, observed) == observed)
			break;
	}

	ID3D12Device *device = AcquireModDevice(commandList);
	if (!device) {
		InterlockedCompareExchange(&gPresentCommandListExecutedPresent, present - 1, present);
		return false;
	}

	std::vector<DX12PendingResourceViewRequest> pendingResourceViews;
	bool changed = RunNamedCommandList(
		device, commandList, L"Present", replacement, &pendingResourceViews);
	DX12IaHashState emptyIa = EmptyIaHashState();
	changed |= ApplyPendingResourceViewsForCommandList(
		device, commandList, emptyIa, pendingResourceViews, replacement);
	device->Release();
	return changed;
}

bool DX12ModPresentReplacementPendingForCurrentPresent()
{
	if (!DX12ModNeedsPresentReplacement())
		return false;
	const LONG present = DX12RuntimeState::GetPresentCount();
	// Plain volatile probe: the value is only meaningful to the recording
	// thread and is published with interlocked ops, so a LOCK CMPXCHG here
	// (once per Draw while a Present mod is active) buys nothing.
	return gPresentCommandListExecutedPresent != present;
}

static DX12TextureOverrideLookupCacheKey MakeIaTextureOverrideLookupCacheKey(
	const Bunny::CommandListTarget &target, uint32_t hash,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance,
	UINT64 preSkinGeneration,
	const DX12TextureOverrideDrawKeyUsage &usage)
{
	DX12TextureOverrideLookupCacheKey key = {};
	key.reloadGeneration = gReloadGeneration;
	key.preSkinGeneration = preSkinGeneration;
	key.targetKind = static_cast<uint32_t>(target.kind);
	key.shaderStage = static_cast<uint32_t>(target.stage);
	key.slot = target.slot;
	key.hash = hash;
	key.vertexCount = usage.useVertexCount ? vertexCount : 0;
	key.indexCount = usage.useIndexCount ? indexCount : 0;
	key.instanceCount = usage.useInstanceCount ? instanceCount : 0;
	key.firstVertex = usage.useFirstVertex ? firstVertex : 0;
	key.firstIndex = usage.useFirstIndex ? firstIndex : 0;
	key.firstInstance = usage.useFirstInstance ? firstInstance : 0;
	return key;
}

static std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot>
LoadTextureOverrideRuntimeSnapshot()
{
	return gCommandExecutionRuntimeSnapshot.load(std::memory_order_acquire);
}

static void CollectIaTriggeredTextureOverrideCandidates(
	const DX12CommandExecutionRuntimeSnapshot &snapshot,
	uint32_t hash,
	bool indexBuffer,
	uint32_t vertexSlot,
	uint32_t vertexCount,
	uint32_t indexCount,
	std::vector<DX12TriggeredTextureOverride> *candidateTriggers)
{
	if (!candidateTriggers)
		return;
	candidateTriggers->clear();

	std::vector<size_t> prefilterScratch;
	const std::vector<size_t> *candidateIndexes =
		FindIaTextureOverridePrefilterCandidatesLocked(
			indexBuffer,
			vertexSlot,
			vertexCount,
			indexCount,
			&prefilterScratch);
	if (!candidateIndexes || candidateIndexes->empty())
		return;

	candidateTriggers->reserve(candidateIndexes->size());
	for (size_t index : *candidateIndexes) {
		if (index >= gIaTextureOverrides.size())
			continue;
		const DX12IaTextureOverrideCandidate &candidate = gIaTextureOverrides[index];
		const Bunny::TextureOverrideConfig *config = candidate.config;
		if (!config || config->hash != hash)
			continue;
		if (!TextureOverrideMatchesIaBinding(*config, indexBuffer, vertexSlot))
			continue;
		auto configIt = snapshot.textureOverridesBySectionId.find(candidate.sectionId);
		if (configIt == snapshot.textureOverridesBySectionId.end() || !configIt->second)
			continue;
		DX12TriggeredTextureOverride triggered;
		triggered.config = configIt->second.get();
		triggered.sectionId = candidate.sectionId;
		triggered.preRuntimeEffect = candidate.preRuntimeEffect;
		triggered.postRuntimeEffect = candidate.postRuntimeEffect;
		triggered.indexBuffer = indexBuffer;
		triggered.vertexSlot = vertexSlot;
		triggered.executeCommands = true;
		triggered.executeDrawActions = true;
		candidateTriggers->push_back(std::move(triggered));
	}
}


static bool FindIaTextureOverridesCached(
	const Bunny::CommandListTarget &target, uint32_t hash,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance,
	bool indexBuffer, uint32_t vertexSlot,
	std::vector<const Bunny::TextureOverrideConfig*> *configs)
{
	if (configs)
		configs->clear();

	std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> snapshot =
		LoadTextureOverrideRuntimeSnapshot();
	if (!snapshot)
		return false;

	static thread_local UINT64 tlsIaTriggeredCacheGeneration = 0;
	static thread_local std::unordered_map<
		DX12TextureOverrideLookupCacheKey,
		std::vector<DX12TriggeredTextureOverride>,
		DX12TextureOverrideLookupCacheKeyHash> tlsIaTriggeredCache;

	const UINT64 reloadGeneration = DX12ModGetReloadGenerationFast();
	if (tlsIaTriggeredCacheGeneration != reloadGeneration) {
		tlsIaTriggeredCache.clear();
		tlsIaTriggeredCacheGeneration = reloadGeneration;
	}

	std::vector<DX12TriggeredTextureOverride> collectedCandidateTriggers;
	CollectIaTriggeredTextureOverrideCandidates(
		*snapshot,
		hash,
		indexBuffer,
		vertexSlot,
		vertexCount,
		indexCount,
		&collectedCandidateTriggers);
	const std::vector<DX12TriggeredTextureOverride> *candidateTriggers =
		&collectedCandidateTriggers;
	UINT64 preSkinGeneration = 0;
	const std::unordered_set<std::wstring> &activePreSkinSections =
		(candidateTriggers &&
		 IaTriggeredCandidatesDependOnActivePreSkin(*candidateTriggers)) ?
			GetActivePreSkinSectionsSnapshot(&preSkinGeneration) :
			EmptyActivePreSkinSections();
	const DX12TextureOverrideDrawKeyUsage keyUsage =
		BuildTriggeredDrawKeyUsage(candidateTriggers);
	const DX12TextureOverrideLookupCacheKey key =
		MakeIaTextureOverrideLookupCacheKey(
			target, hash, vertexCount, indexCount, instanceCount,
			firstVertex, firstIndex, firstInstance, preSkinGeneration,
			keyUsage);

	auto cached = tlsIaTriggeredCache.find(key);
	if (cached != tlsIaTriggeredCache.end()) {
		if (configs) {
			configs->reserve(cached->second.size());
			for (const DX12TriggeredTextureOverride &triggered : cached->second) {
				if (triggered.config)
					configs->push_back(triggered.config);
			}
		}
		return !cached->second.empty();
	}

	if (!candidateTriggers || candidateTriggers->empty()) {
		tlsIaTriggeredCache.emplace(key, std::vector<DX12TriggeredTextureOverride>());
		return false;
	}

	std::vector<DX12TriggeredTextureOverride> matchedTriggers;
	matchedTriggers.reserve(candidateTriggers->size());
	for (const DX12TriggeredTextureOverride &triggered : *candidateTriggers) {
		const Bunny::TextureOverrideConfig *config = triggered.config;
		if (!config)
			continue;
		if (!TextureOverrideMatchCsSatisfied(*config, activePreSkinSections))
			continue;
		if (!TextureOverrideMatchesDrawContext(
			*config, vertexCount, indexCount, instanceCount,
			firstVertex, firstIndex, firstInstance))
			continue;
		matchedTriggers.push_back(triggered);
		if (configs)
			configs->push_back(config);
	}

	if (tlsIaTriggeredCache.size() >= MaxTextureOverrideLookupCacheEntries)
		tlsIaTriggeredCache.clear();
	tlsIaTriggeredCache.emplace(key, matchedTriggers);
	return !matchedTriggers.empty();
}

class DX12IaTargetTextureOverrideCacheKey

{

public:
	UINT64 reloadGeneration = 0;
	UINT64 preSkinGeneration = 0;
	uint32_t targetKind = 0;
	uint32_t slot = 0;
	uint32_t primaryHash = 0;
	UINT64 bufferLocation = 0;
	uint32_t bufferSize = 0;
	uint32_t viewParam = 0;
	uint32_t vertexCount = 0;
	uint32_t indexCount = 0;
	uint32_t instanceCount = 0;
	uint32_t firstVertex = 0;
	uint32_t firstIndex = 0;
	uint32_t firstInstance = 0;

	bool operator==(const DX12IaTargetTextureOverrideCacheKey &other) const
	{
		return reloadGeneration == other.reloadGeneration &&
			preSkinGeneration == other.preSkinGeneration &&
			targetKind == other.targetKind &&
			slot == other.slot &&
			primaryHash == other.primaryHash &&
			bufferLocation == other.bufferLocation &&
			bufferSize == other.bufferSize &&
			viewParam == other.viewParam &&
			vertexCount == other.vertexCount &&
			indexCount == other.indexCount &&
			instanceCount == other.instanceCount &&
			firstVertex == other.firstVertex &&
			firstIndex == other.firstIndex &&
			firstInstance == other.firstInstance;
	}
};

class DX12IaTargetTextureOverrideCacheKeyHash

{

public:
	size_t operator()(const DX12IaTargetTextureOverrideCacheKey &key) const
	{
		UINT64 hash = 0x9f4f2726179a98c3ull;
		hash = HashCombine64(hash, key.reloadGeneration);
		hash = HashCombine64(hash, key.preSkinGeneration);
		hash = HashCombine64(hash, key.targetKind);
		hash = HashCombine64(hash, key.slot);
		hash = HashCombine64(hash, key.primaryHash);
		hash = HashCombine64(hash, key.bufferLocation);
		hash = HashCombine64(hash, key.bufferSize);
		hash = HashCombine64(hash, key.viewParam);
		hash = HashCombine64(hash, key.vertexCount);
		hash = HashCombine64(hash, key.indexCount);
		hash = HashCombine64(hash, key.instanceCount);
		hash = HashCombine64(hash, key.firstVertex);
		hash = HashCombine64(hash, key.firstIndex);
		hash = HashCombine64(hash, key.firstInstance);
		return static_cast<size_t>(hash ^ (hash >> 32));
	}
};

static DX12IaTargetTextureOverrideCacheKey MakeIaTargetTextureOverrideCacheKey(
	const Bunny::CommandListTarget &target,
	uint32_t primaryHash,
	UINT64 bufferLocation,
	uint32_t bufferSize,
	uint32_t viewParam,
	uint32_t vertexCount,
	uint32_t indexCount,
	uint32_t instanceCount,
	uint32_t firstVertex,
	uint32_t firstIndex,
	uint32_t firstInstance)
{
	DX12IaTargetTextureOverrideCacheKey key = {};
	key.reloadGeneration = DX12ModGetReloadGenerationFast();
	key.preSkinGeneration = CurrentPreSkinActiveGeneration();
	key.targetKind = static_cast<uint32_t>(target.kind);
	key.slot = target.slot;
	key.primaryHash = primaryHash;
	key.bufferLocation = bufferLocation;
	key.bufferSize = bufferSize;
	key.viewParam = viewParam;
	key.vertexCount = vertexCount;
	key.indexCount = indexCount;
	key.instanceCount = instanceCount;
	key.firstVertex = firstVertex;
	key.firstIndex = firstIndex;
	key.firstInstance = firstInstance;
	return key;
}

static bool TryLoadIaTargetTextureOverrideCache(
	const DX12IaTargetTextureOverrideCacheKey &key,
	std::vector<DX12TextureOverrideMatch> *matches)
{
	static thread_local UINT64 tlsReloadGeneration = 0;
	static thread_local std::unordered_map<
		DX12IaTargetTextureOverrideCacheKey,
		std::vector<DX12TextureOverrideMatch>,
		DX12IaTargetTextureOverrideCacheKeyHash> tlsCache;

	if (tlsReloadGeneration != key.reloadGeneration) {
		tlsCache.clear();
		tlsReloadGeneration = key.reloadGeneration;
	}

	auto cached = tlsCache.find(key);
	if (cached == tlsCache.end())
		return false;
	if (matches)
		*matches = cached->second;
	return !cached->second.empty();
}

static void StoreIaTargetTextureOverrideCache(
	const DX12IaTargetTextureOverrideCacheKey &key,
	const std::vector<DX12TextureOverrideMatch> &matches)
{
	static thread_local UINT64 tlsReloadGeneration = 0;
	static thread_local std::unordered_map<
		DX12IaTargetTextureOverrideCacheKey,
		std::vector<DX12TextureOverrideMatch>,
		DX12IaTargetTextureOverrideCacheKeyHash> tlsCache;

	if (tlsReloadGeneration != key.reloadGeneration) {
		tlsCache.clear();
		tlsReloadGeneration = key.reloadGeneration;
	}

	if (tlsCache.size() >= MaxTextureOverrideLookupCacheEntries)
		tlsCache.clear();
	tlsCache[key] = matches;
}

static const DX12IaBufferHash *FindIaVertexSlot(const DX12IaHashState &iaState, uint32_t slot)
{
	for (const DX12IaBufferHash &buffer : iaState.vertexBuffers) {
		if (buffer.slot == slot)
			return &buffer;
	}
	return nullptr;
}

static bool TargetStageIsCompute(Bunny::CommandListShaderStage stage)
{
	return stage == Bunny::CommandListShaderStage::Compute;
}

static D3D12_DESCRIPTOR_RANGE_TYPE DescriptorRangeTypeForTarget(
	Bunny::CommandListTargetKind kind)
{
	switch (kind) {
	case Bunny::CommandListTargetKind::ConstantBuffer:
		return D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
	case Bunny::CommandListTargetKind::ShaderResource:
		return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	case Bunny::CommandListTargetKind::UnorderedAccessView:
		return D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
	default:
		return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	}
}

static bool DescriptorBindingMatchesTarget(
	const DX12CurrentShaderResourceBinding &binding,
	const Bunny::CommandListTarget &target)
{
	if (binding.shaderRegister != target.slot || binding.registerSpace != 0)
		return false;
	if (binding.shaderVisibility == D3D12_SHADER_VISIBILITY_ALL)
		return true;
	if (target.stage == Bunny::CommandListShaderStage::Vertex)
		return binding.shaderVisibility == D3D12_SHADER_VISIBILITY_VERTEX;
	if (target.stage == Bunny::CommandListShaderStage::Pixel)
		return binding.shaderVisibility == D3D12_SHADER_VISIBILITY_PIXEL;
	if (target.stage == Bunny::CommandListShaderStage::Compute)
		return true;
	return false;
}

static uint32_t HashDescriptorBinding(
	ID3D12GraphicsCommandList *commandList,
	ID3D12Device *cachedDevice,
	const DX12CurrentShaderResourceBinding &binding)
{
	if (binding.hasDescriptor) {
		// Content hash cached at descriptor write time (Create*View). The
		// fallback recompute costs device calls (CheckFeatureSupport /
		// GetCopyableFootprints) per binding per draw for texture views;
		// records without a cached hash (CBVs, untracked resources) still
		// take the fallback, so a missed cache is always safe.
		if (binding.descriptor.huntHash)
			return binding.descriptor.huntHash;
		ID3D12Device *device = AcquireModDeviceFast(commandList, cachedDevice);
		const bool deviceFromCache = (cachedDevice != nullptr);
		if (!device)
			return 0;
		const uint32_t hash =
			DX12TextureHashUtils::DescriptorHuntHash(device, binding);
		if (!deviceFromCache)
			device->Release();
		return hash;
	}
	if (binding.rootDescriptor && binding.gpuVirtualAddress) {
		DX12BufferResourceSummary summary = {};
		const UINT64 probeSize = 1;
		if (DX12ResolveBufferResourceByGpuVa(binding.gpuVirtualAddress, probeSize, &summary)) {
			const UINT64 size = summary.hasResourceDesc ?
				summary.resourceDesc.Width - summary.resourceOffset : summary.viewSize;
			return DX12HashBufferResourceView(
				&summary, binding.gpuVirtualAddress, size ? size : probeSize);
		}
		return DX12HashBufferResourceView(nullptr, binding.gpuVirtualAddress, probeSize);
	}
	return 0;
}

static uint32_t HashDescriptorBindingResource(const DX12CurrentShaderResourceBinding &binding)
{
	if (!binding.hasDescriptor || !binding.descriptor.hasResourceDesc)
		return 0;
	if (binding.descriptor.resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
		uint32_t hash = 0;
		if (DX12TryGetTextureHash(binding.descriptor.resource, &hash))
			return hash;
	}
	return 0;
}

class DX12DescriptorTextureOverrideCandidateCacheKey
{
public:
	UINT64 reloadGeneration = 0;
	const void *commandListIdentity = nullptr;
	uint32_t targetKind = 0;
	uint32_t shaderStage = 0;
	uint32_t slot = 0;
	uint32_t vertexCount = 0;
	uint32_t indexCount = 0;
	UINT64 descriptorQuerySerial = 0;
	ID3D12DescriptorHeap *descriptorHeap = nullptr;
	UINT64 descriptorHeapVersion = 0;
	UINT64 textureHashGeneration = 0;

	bool operator==(const DX12DescriptorTextureOverrideCandidateCacheKey &other) const
	{
		return reloadGeneration == other.reloadGeneration &&
			commandListIdentity == other.commandListIdentity &&
			targetKind == other.targetKind &&
			shaderStage == other.shaderStage &&
			slot == other.slot &&
			vertexCount == other.vertexCount &&
			indexCount == other.indexCount &&
			descriptorQuerySerial == other.descriptorQuerySerial &&
			descriptorHeap == other.descriptorHeap &&
			descriptorHeapVersion == other.descriptorHeapVersion &&
			textureHashGeneration == other.textureHashGeneration;
	}
};

class DX12DescriptorTextureOverrideCandidateCacheKeyHash
{
public:
	size_t operator()(const DX12DescriptorTextureOverrideCandidateCacheKey &key) const
	{
		UINT64 hash = 0x7fb5d329728ea185ull;
		hash = HashCombine64(hash, key.reloadGeneration);
		hash = HashCombine64(hash, reinterpret_cast<uintptr_t>(key.commandListIdentity));
		hash = HashCombine64(hash, key.targetKind);
		hash = HashCombine64(hash, key.shaderStage);
		hash = HashCombine64(hash, key.slot);
		hash = HashCombine64(hash, key.vertexCount);
		hash = HashCombine64(hash, key.indexCount);
		hash = HashCombine64(hash, key.descriptorQuerySerial);
		hash = HashCombine64(hash, reinterpret_cast<uintptr_t>(key.descriptorHeap));
		hash = HashCombine64(hash, key.descriptorHeapVersion);
		hash = HashCombine64(hash, key.textureHashGeneration);
		return static_cast<size_t>(hash ^ (hash >> 32));
	}
};

static bool BuildDescriptorTextureOverrideCandidateCacheKey(
	ID3D12GraphicsCommandList *commandList,
	DX12SimulateContext *runtimeState,
	const Bunny::CommandListTarget &target,
	bool compute,
	UINT64 reloadGeneration,
	uint32_t vertexCount,
	uint32_t indexCount,
	DX12DescriptorTextureOverrideCandidateCacheKey *key)
{
	if (!key)
		return false;
	*key = DX12DescriptorTextureOverrideCandidateCacheKey();
	key->reloadGeneration = reloadGeneration;
	key->commandListIdentity =
		runtimeState && runtimeState->commandListIdentity ?
			runtimeState->commandListIdentity :
			commandList;
	key->targetKind = static_cast<uint32_t>(target.kind);
	key->shaderStage = static_cast<uint32_t>(target.stage);
	key->slot = target.slot;
	key->vertexCount = vertexCount;
	key->indexCount = indexCount;
	if (!runtimeState)
		return false;
	key->descriptorQuerySerial = compute ?
		(runtimeState->computeDescriptorQuerySerial ?
			runtimeState->computeDescriptorQuerySerial : 1) :
		(runtimeState->graphicsDescriptorQuerySerial ?
			runtimeState->graphicsDescriptorQuerySerial : 1);
	key->descriptorHeap = runtimeState->cbvSrvUavHeap;
	key->descriptorHeapVersion =
		DX12GetDescriptorHeapVersion(runtimeState->cbvSrvUavHeap);
	key->textureHashGeneration = DX12GetTextureHashGeneration();
	return true;
}

static void AppendTextureOverrideCandidateMatchesByHash(
	const DX12CommandExecutionRuntimeSnapshot &snapshot,
	uint32_t hash,
	uint32_t resourceHash,
	uint32_t descriptorHash,
	const DX12CurrentShaderResourceBinding *binding,
	bool compute,
	const std::vector<uint32_t> *allowedSectionIds,
	std::vector<DX12TextureOverrideMatch> *matches)
{
	if (!hash || !matches)
		return;
	auto bucket = snapshot.textureOverridesByHash.find(hash);
	if (bucket == snapshot.textureOverridesByHash.end())
		return;

	for (const DX12TriggeredTextureOverride &triggered : bucket->second) {
		const Bunny::TextureOverrideConfig *config = triggered.config;
		if (!config)
			continue;
		if (allowedSectionIds && triggered.sectionId &&
		    !std::binary_search(
				allowedSectionIds->begin(),
				allowedSectionIds->end(),
				triggered.sectionId))
			continue;
		DX12TextureOverrideMatch match;
		match.config = config;
		match.preRuntimeEffect = triggered.preRuntimeEffect;
		match.postRuntimeEffect = triggered.postRuntimeEffect;
		match.matchedHash = hash;
		match.resourceHash = resourceHash;
		match.descriptorHash = descriptorHash;
		if (binding) {
			match.hasThisSrvTarget =
				binding->hasDescriptor &&
				binding->descriptor.kind == DescriptorKind::SRV &&
				!binding->rootDescriptor;
			match.compute = compute;
			match.thisSrvBinding = *binding;
		}
		matches->push_back(match);
	}
}

static bool FilterDescriptorTextureOverrideCandidates(
	const std::vector<DX12TextureOverrideMatch> &candidates,
	uint32_t vertexCount,
	uint32_t indexCount,
	uint32_t instanceCount,
	uint32_t firstVertex,
	uint32_t firstIndex,
	uint32_t firstInstance,
	std::vector<DX12TextureOverrideMatch> *matches)
{
	if (matches)
		matches->clear();
	if (candidates.empty())
		return false;

	const std::unordered_set<std::wstring> &activePreSkinSections =
		GetActivePreSkinSectionsSnapshot();
	bool found = false;
	for (const DX12TextureOverrideMatch &candidate : candidates) {
		const Bunny::TextureOverrideConfig *config = candidate.config;
		if (!config)
			continue;
		if (!TextureOverrideMatchCsSatisfied(*config, activePreSkinSections))
			continue;
		if (!TextureOverrideMatchesDrawContext(
				*config,
				vertexCount,
				indexCount,
				instanceCount,
				firstVertex,
				firstIndex,
				firstInstance))
			continue;
		if (matches)
			matches->push_back(candidate);
		found = true;
	}
	return found;
}


static bool HasPotentialIaTextureOverrideCandidateLocked(
	bool indexBuffer,
	uint32_t vertexSlot,
	uint32_t vertexCount,
	uint32_t indexCount,
	uint32_t instanceCount,
	uint32_t firstVertex,
	uint32_t firstIndex,
	uint32_t firstInstance,
	std::vector<size_t> *prefilterScratch = nullptr)
{
	std::vector<size_t> localScratch;
	const std::vector<size_t> *candidateIndexes =
		FindIaTextureOverridePrefilterCandidatesLocked(
			indexBuffer, vertexSlot, vertexCount, indexCount,
			prefilterScratch ? prefilterScratch : &localScratch);
	if (!candidateIndexes || candidateIndexes->empty())
		return false;

	const bool dependsOnActivePreSkin =
		IaCandidateIndexesDependOnActivePreSkin(*candidateIndexes);
	const std::unordered_set<std::wstring> &activePreSkinSections =
		dependsOnActivePreSkin ?
			GetActivePreSkinSectionsSnapshot() :
			EmptyActivePreSkinSections();

	for (size_t index : *candidateIndexes) {
		if (index >= gIaTextureOverrides.size())
			continue;
		const DX12IaTextureOverrideCandidate &candidate = gIaTextureOverrides[index];
		const Bunny::TextureOverrideConfig *config = candidate.config;
		if (!config)
			continue;
		if (!TextureOverrideMatchCsSatisfied(*config, activePreSkinSections))
			continue;
		if (!TextureOverrideMatchesIaBinding(*config, indexBuffer, vertexSlot))
			continue;
		if (!TextureOverrideMatchesDrawContext(
				*config,
				vertexCount,
				indexCount,
				instanceCount,
				firstVertex,
				firstIndex,
				firstInstance))
			continue;
		return true;
	}
	return false;
}

static bool HasPotentialHashedIaTextureOverrideCandidateLocked(
	bool indexBuffer,
	uint32_t vertexSlot,
	uint32_t vertexCount,
	uint32_t indexCount)
{
	std::vector<size_t> prefilterScratch;
	const std::vector<size_t> *candidateIndexes =
		FindIaTextureOverridePrefilterCandidatesLocked(
			indexBuffer,
			vertexSlot,
			vertexCount,
			indexCount,
			&prefilterScratch);
	if (!candidateIndexes || candidateIndexes->empty())
		return false;

	const bool dependsOnActivePreSkin =
		IaCandidateIndexesDependOnActivePreSkin(*candidateIndexes);
	const std::unordered_set<std::wstring> &activePreSkinSections =
		dependsOnActivePreSkin ?
			GetActivePreSkinSectionsSnapshot() :
			EmptyActivePreSkinSections();

	for (size_t index : *candidateIndexes) {
		if (index >= gIaTextureOverrides.size())
			continue;
		const DX12IaTextureOverrideCandidate &candidate = gIaTextureOverrides[index];
		const Bunny::TextureOverrideConfig *config = candidate.config;
		if (!config || !config->hash)
			continue;
		if (!TextureOverrideMatchCsSatisfied(*config, activePreSkinSections))
			continue;
		if (!TextureOverrideMatchesIaBinding(*config, indexBuffer, vertexSlot))
			continue;
		return true;
	}

	return false;
}

bool DX12ModHasPotentialIaTextureOverrideTargetByCount(
	bool indexBuffer,
	uint32_t vertexSlot,
	uint32_t vertexCount,
	uint32_t indexCount)
{
	std::vector<size_t> prefilterScratch;
	const std::vector<size_t> *candidateIndexes =
		FindIaTextureOverridePrefilterCandidatesLocked(
			indexBuffer,
			vertexSlot,
			vertexCount,
			indexCount,
			&prefilterScratch);
	if (!candidateIndexes || candidateIndexes->empty())
		return false;

	const bool dependsOnActivePreSkin =
		IaCandidateIndexesDependOnActivePreSkin(*candidateIndexes);
	const std::unordered_set<std::wstring> &activePreSkinSections =
		dependsOnActivePreSkin ?
			GetActivePreSkinSectionsSnapshot() :
			EmptyActivePreSkinSections();

	for (size_t index : *candidateIndexes) {
		if (index >= gIaTextureOverrides.size())
			continue;
		const DX12IaTextureOverrideCandidate &candidate = gIaTextureOverrides[index];
		const Bunny::TextureOverrideConfig *config = candidate.config;
		if (!config)
			continue;
		if (!TextureOverrideMatchCsSatisfied(*config, activePreSkinSections))
			continue;
		if (!TextureOverrideMatchesIaBinding(*config, indexBuffer, vertexSlot))
			continue;
		return true;
	}

	return false;
}

bool DX12ModHasPotentialHashedIaTextureOverrideTargetByCount(
	bool indexBuffer,
	uint32_t vertexSlot,
	uint32_t vertexCount,
	uint32_t indexCount)
{
	return HasPotentialHashedIaTextureOverrideCandidateLocked(
		indexBuffer,
		vertexSlot,
		vertexCount,
		indexCount);
}


static bool FindDescriptorTextureOverrides(
	ID3D12GraphicsCommandList *commandList,
	const Bunny::CommandListTarget &target,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance,
	std::vector<DX12TextureOverrideMatch> *matches,
	const std::vector<DX12CurrentShaderResourceBinding> *preResolvedBindings = nullptr)
{
	if (matches)
		matches->clear();
	if (!commandList)
		return false;

	DX12SimulateContext *runtimeState =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	ID3D12Device *cachedDevice = runtimeState ? runtimeState->device : nullptr;
	const bool compute = TargetStageIsCompute(target.stage);

	const UINT64 reloadGeneration = DX12ModGetReloadGenerationFast();
	static thread_local UINT64 tlsDescriptorCandidateCacheGeneration = 0;
	static thread_local std::unordered_map<
		DX12DescriptorTextureOverrideCandidateCacheKey,
		std::vector<DX12TextureOverrideMatch>,
		DX12DescriptorTextureOverrideCandidateCacheKeyHash> tlsDescriptorCandidateCache;
	if (tlsDescriptorCandidateCacheGeneration != reloadGeneration) {
		tlsDescriptorCandidateCache.clear();
		tlsDescriptorCandidateCacheGeneration = reloadGeneration;
	}

	DX12DescriptorTextureOverrideCandidateCacheKey cacheKey = {};
	const bool canUseSerialCache =
		BuildDescriptorTextureOverrideCandidateCacheKey(
			commandList,
			runtimeState,
			target,
			compute,
			reloadGeneration,
			vertexCount,
			indexCount,
			&cacheKey);
	if (canUseSerialCache) {
		auto cachedCandidates = tlsDescriptorCandidateCache.find(cacheKey);
		if (cachedCandidates != tlsDescriptorCandidateCache.end()) {
			return FilterDescriptorTextureOverrideCandidates(
				cachedCandidates->second,
				vertexCount,
				indexCount,
				instanceCount,
				firstVertex,
				firstIndex,
				firstInstance,
				matches);
		}
	}

	std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> snapshot =
		LoadTextureOverrideRuntimeSnapshot();
	if (!snapshot)
		return false;
	if (!HasPotentialDescriptorTriggeredPrefilterCandidates(
			*snapshot,
			vertexCount,
			indexCount)) {
		if (canUseSerialCache) {
			if (tlsDescriptorCandidateCache.size() >= MaxTextureOverrideLookupCacheEntries)
				tlsDescriptorCandidateCache.clear();
			tlsDescriptorCandidateCache.emplace(cacheKey, std::vector<DX12TextureOverrideMatch>());
		}
		return false;
	}
	static thread_local std::vector<uint32_t> tlsAllowedSectionIds;
	CollectDescriptorTriggeredPrefilterSectionIds(
		*snapshot,
		vertexCount,
		indexCount,
		&tlsAllowedSectionIds);
	if (tlsAllowedSectionIds.empty()) {
		if (canUseSerialCache) {
			if (tlsDescriptorCandidateCache.size() >= MaxTextureOverrideLookupCacheEntries)
				tlsDescriptorCandidateCache.clear();
			tlsDescriptorCandidateCache.emplace(cacheKey, std::vector<DX12TextureOverrideMatch>());
		}
		return false;
	}

	// DX11 consumes "current resources" at draw time rather than re-querying
	// per target. When the caller already has the current descriptor state for
	// this stage/range bucket, reuse it across all descriptor-trigger checks in
	// this draw.
	const bool canUsePreResolvedBindings = preResolvedBindings != nullptr;
	std::vector<DX12CurrentShaderResourceBinding> bindings;
	const std::vector<DX12CurrentShaderResourceBinding> *bindingsView =
		canUsePreResolvedBindings ? preResolvedBindings : &bindings;
	if (!canUsePreResolvedBindings) {
		if (!DX12BindingTracker::GetCurrentShaderResourceBindingsByRegister(
			    commandList,
			    compute,
			    DescriptorRangeTypeForTarget(target.kind),
			    target.slot,
			    0,
			    &bindings)) {
			if (canUseSerialCache) {
				if (tlsDescriptorCandidateCache.size() >= MaxTextureOverrideLookupCacheEntries)
					tlsDescriptorCandidateCache.clear();
				tlsDescriptorCandidateCache.emplace(cacheKey, std::vector<DX12TextureOverrideMatch>());
			}
			return false;
		}
	}
	if (!bindingsView || bindingsView->empty()) {
		if (canUseSerialCache) {
			if (tlsDescriptorCandidateCache.size() >= MaxTextureOverrideLookupCacheEntries)
				tlsDescriptorCandidateCache.clear();
			tlsDescriptorCandidateCache.emplace(cacheKey, std::vector<DX12TextureOverrideMatch>());
		}
		return false;
	}

	std::vector<DX12TextureOverrideMatch> candidates;
	std::vector<uint32_t> resourceHashes(bindingsView->size(), 0);
	for (size_t index = 0; index < bindingsView->size(); ++index) {
		const DX12CurrentShaderResourceBinding &binding = (*bindingsView)[index];
		if (!DescriptorBindingMatchesTarget(binding, target))
			continue;
		const uint32_t resourceHash =
			resourceHashes[index] ? resourceHashes[index] :
			(resourceHashes[index] = HashDescriptorBindingResource(binding));
		const uint32_t descriptorHash =
			HashDescriptorBinding(commandList, cachedDevice, binding);
		AppendTextureOverrideCandidateMatchesByHash(
			*snapshot,
			resourceHash,
			resourceHash,
			descriptorHash,
			&binding,
			compute,
			&tlsAllowedSectionIds,
			&candidates);
		if (descriptorHash && descriptorHash != resourceHash) {
			AppendTextureOverrideCandidateMatchesByHash(
				*snapshot,
				descriptorHash,
				resourceHash,
				descriptorHash,
				&binding,
				compute,
				&tlsAllowedSectionIds,
				&candidates);
		}
	}

	if (canUseSerialCache) {
		if (tlsDescriptorCandidateCache.size() >= MaxTextureOverrideLookupCacheEntries)
			tlsDescriptorCandidateCache.clear();
		tlsDescriptorCandidateCache.emplace(cacheKey, candidates);
	}

	return FilterDescriptorTextureOverrideCandidates(
		candidates,
		vertexCount,
		indexCount,
		instanceCount,
		firstVertex,
		firstIndex,
		firstInstance,
		matches);
}

class DX12IaResourceViewSnapshot

{

public:
	ID3D12Resource *resource = nullptr;
	DX12LoadedResource metadata;
	bool uavBarrierPending = false;
	bool transitionToVertex = false;
	D3D12_RESOURCE_STATES stateBefore = D3D12_RESOURCE_STATE_COMMON;
	bool retained = false;
};

static void ReleaseIaResourceViewSnapshot(DX12IaResourceViewSnapshot *snapshot)
{
	if (!snapshot || !snapshot->resource)
		return;
	if (snapshot->retained)
		snapshot->resource->Release();
	snapshot->resource = nullptr;
	snapshot->retained = false;
}

static void AddRefIaResourceViewSnapshot(DX12IaResourceViewSnapshot *snapshot)
{
	if (!snapshot || !snapshot->resource || snapshot->retained)
		return;
	snapshot->resource->AddRef();
	snapshot->retained = true;
}

class DX12LoadedResourceSnapshotReadScope

{

public:
	DX12LoadedResourceSnapshotReadScope()
	{
		InterlockedIncrement(&gActiveLoadedResourceSnapshots);
	}

	~DX12LoadedResourceSnapshotReadScope()
	{
		InterlockedDecrement(&gActiveLoadedResourceSnapshots);
	}
};

static void SnapshotIaResourceViewLocked(
	DX12LoadedResource *resource, DX12IaResourceViewSnapshot *snapshot)
{
	if (!snapshot)
		return;
	ReleaseIaResourceViewSnapshot(snapshot);
	if (!resource)
		return;
	if (resource->uavResource && resource->uavValid &&
		resource->uavValidPresent == DX12RuntimeState::GetPresentCount()) {
		snapshot->resource = resource->uavResource;
		snapshot->metadata = *resource;
		snapshot->uavBarrierPending = resource->uavBarrierPending;
		if (resource->uavState != D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER) {
			snapshot->transitionToVertex = true;
			snapshot->stateBefore = resource->uavState;
			resource->uavState = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
		}
		resource->uavBarrierPending = false;
		return;
	}
	if (resource->srvResource && resource->srvByteWidth > resource->byteWidth) {
		snapshot->resource = resource->srvResource;
		snapshot->metadata = *resource;
		return;
	}
	if (resource->resource) {
		snapshot->resource = resource->resource;
		snapshot->metadata = *resource;
	}
}

static UINT IaViewByteWidthForResource(const DX12LoadedResource &resource, ID3D12Resource *iaResource)
{
	UINT64 byteWidth = resource.byteWidth;
	if (resource.uavResource && iaResource == resource.uavResource && resource.uavByteWidth)
		byteWidth = resource.uavByteWidth;
	else if (resource.srvResource && iaResource == resource.srvResource && resource.srvByteWidth)
		byteWidth = resource.srvByteWidth;
	return static_cast<UINT>((std::min)(byteWidth, static_cast<UINT64>(UINT_MAX)));
}
static void ApplyIaResourceBarriers(
	ID3D12GraphicsCommandList *commandList, const DX12IaResourceViewSnapshot &snapshot)
{
	if (!commandList || !snapshot.resource)
		return;
	if (snapshot.uavBarrierPending) {
		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		barrier.UAV.pResource = snapshot.resource;
		commandList->ResourceBarrier(1, &barrier);
	}
	if (snapshot.transitionToVertex) {
		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = snapshot.resource;
		barrier.Transition.StateBefore = snapshot.stateBefore;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		commandList->ResourceBarrier(1, &barrier);
	}
}

static bool AppendResourceViewsForCommandList(
	ID3D12Device *device, ID3D12GraphicsCommandList *commandList, const DX12IaHashState &iaState,
	const std::wstring &indexResource,
	const std::vector<DX12CompiledVertexResourceBinding> &vertexResources,
	DX12ModIaReplacement *replacement)
{
	if (!device || !commandList || !replacement)
		return false;

	if (!indexResource.empty())
		EnsureLoadedResourceForCommandList(device, indexResource);
	for (const auto &item : vertexResources)
		EnsureLoadedResourceForCommandList(device, item.resource);

	bool changed = false;
	if (!indexResource.empty()) {
		DX12IaResourceViewSnapshot snapshot;
		{
			DX12LoadedResourceSnapshotReadScope scope;
			AcquireSRWLockExclusive(&gModLock);
			auto loaded = gLoadedResources.find(indexResource);
			if (loaded != gLoadedResources.end())
				SnapshotIaResourceViewLocked(&loaded->second, &snapshot);
			ReleaseSRWLockExclusive(&gModLock);
			AddRefIaResourceViewSnapshot(&snapshot);
		}
		if (snapshot.resource) {
			ApplyIaResourceBarriers(commandList, snapshot);
			replacement->indexBuffer.BufferLocation = snapshot.resource->GetGPUVirtualAddress();
			replacement->indexBuffer.SizeInBytes =
				IaViewByteWidthForResource(snapshot.metadata, snapshot.resource);
			replacement->indexBuffer.Format =
				snapshot.metadata.format == DXGI_FORMAT_R16_UINT ||
				snapshot.metadata.format == DXGI_FORMAT_R32_UINT ?
				snapshot.metadata.format :
				(iaState.hasIndexBuffer && iaState.indexView.Format != DXGI_FORMAT_UNKNOWN ?
					iaState.indexView.Format : DXGI_FORMAT_R32_UINT);
			replacement->RetainResource(snapshot.resource);
			replacement->hasIndexBuffer = true;
						changed = true;
		}
		ReleaseIaResourceViewSnapshot(&snapshot);
	}

	if (!vertexResources.empty()) {
		uint32_t minSlot = vertexResources.front().slot;
		uint32_t maxSlot = vertexResources.back().slot;
		if (maxSlot >= minSlot && maxSlot - minSlot < 32) {
			if (replacement->vertexBuffers.empty() ||
			    minSlot < replacement->vertexBufferStartSlot ||
			    maxSlot >= replacement->vertexBufferStartSlot + replacement->vertexBuffers.size()) {
				uint32_t oldStart = replacement->vertexBuffers.empty() ?
					minSlot : replacement->vertexBufferStartSlot;
				uint32_t oldEnd = replacement->vertexBuffers.empty() ?
					maxSlot : replacement->vertexBufferStartSlot +
						static_cast<uint32_t>(replacement->vertexBuffers.size()) - 1;
				uint32_t newStart = (std::min)(oldStart, minSlot);
				uint32_t newEnd = (std::max)(oldEnd, maxSlot);
				if (newEnd - newStart >= 32)
					return changed;

				std::vector<D3D12_VERTEX_BUFFER_VIEW> resized(newEnd - newStart + 1);
				for (size_t i = 0; i < replacement->vertexBuffers.size(); ++i) {
					uint32_t slot = replacement->vertexBufferStartSlot + static_cast<uint32_t>(i);
					resized[slot - newStart] = replacement->vertexBuffers[i];
				}
				replacement->vertexBufferStartSlot = newStart;
				replacement->vertexBuffers.swap(resized);
			}

			for (const auto &item : vertexResources) {
				DX12IaResourceViewSnapshot snapshot;
				{
					DX12LoadedResourceSnapshotReadScope scope;
					AcquireSRWLockExclusive(&gModLock);
					auto loaded = gLoadedResources.find(item.resource);
					if (loaded != gLoadedResources.end())
						SnapshotIaResourceViewLocked(&loaded->second, &snapshot);
					ReleaseSRWLockExclusive(&gModLock);
					AddRefIaResourceViewSnapshot(&snapshot);
				}
				if (!snapshot.resource) {
					ReleaseIaResourceViewSnapshot(&snapshot);
					continue;
				}
				ApplyIaResourceBarriers(commandList, snapshot);
				D3D12_VERTEX_BUFFER_VIEW &view =
					replacement->vertexBuffers[item.slot - replacement->vertexBufferStartSlot];
				view.BufferLocation = snapshot.resource->GetGPUVirtualAddress();
				view.SizeInBytes = IaViewByteWidthForResource(snapshot.metadata, snapshot.resource);
				const DX12IaBufferHash *sourceSlot = FindIaVertexSlot(iaState, item.slot);
				view.StrideInBytes = snapshot.metadata.stride ? snapshot.metadata.stride :
					(sourceSlot ? sourceSlot->vertexView.StrideInBytes : 0);
				replacement->RetainResource(snapshot.resource);
								changed = true;
				ReleaseIaResourceViewSnapshot(&snapshot);
			}
		}
	}
	return changed;
}

static bool ApplyPendingResourceViewsForCommandList(
	ID3D12Device *device, ID3D12GraphicsCommandList *commandList,
	const DX12IaHashState &iaState,
	const std::vector<DX12PendingResourceViewRequest> &requests,
	DX12ModIaReplacement *replacement)
{
	bool changed = false;
	for (const DX12PendingResourceViewRequest &request : requests) {
		changed |= AppendResourceViewsForCommandList(
			device, commandList, iaState,
			request.indexResource, request.vertexResources, replacement);
	}
	return changed;
}

static void AppendTextureOverrideMatches(
	const std::vector<const Bunny::TextureOverrideConfig*> &configs,
	uint32_t matchedHash,
	std::vector<DX12TextureOverrideMatch> *resolvedMatches,
	std::unordered_set<const Bunny::TextureOverrideConfig*> *seen)
{
	if (!resolvedMatches || !seen)
		return;
	for (const Bunny::TextureOverrideConfig *config : configs) {
		if (!config || !seen->insert(config).second)
			continue;
		DX12TextureOverrideMatch match;
		match.config = config;
		match.preRuntimeEffect =
			LookupRuntimeEffect(gTextureOverridePreRuntimeEffect, config->section);
		match.postRuntimeEffect =
			LookupRuntimeEffect(gTextureOverridePostRuntimeEffect, config->section);
		match.matchedHash = matchedHash;
		resolvedMatches->push_back(match);
	}
}

static void SortTextureOverrideMatches(
	std::vector<DX12TextureOverrideMatch> *matches)
{
	if (!matches)
		return;
	std::sort(matches->begin(), matches->end(),
		[](const DX12TextureOverrideMatch &lhs,
		   const DX12TextureOverrideMatch &rhs)
		{
			if (!lhs.config || !rhs.config)
				return lhs.config != nullptr;
			if (lhs.config->matchPriority != rhs.config->matchPriority)
				return lhs.config->matchPriority > rhs.config->matchPriority;
			const std::wstring leftSection =
				Bunny::IniDocumentText::ToLower(lhs.config->section);
			const std::wstring rightSection =
				Bunny::IniDocumentText::ToLower(rhs.config->section);
			if (leftSection != rightSection)
				return leftSection < rightSection;
			return lhs.matchedHash > rhs.matchedHash;
		});
}

static bool FindTargetTextureOverride(
	ID3D12GraphicsCommandList *commandList,
	const DX12IaHashState &iaState, const Bunny::CommandListTarget &target,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance,
	std::vector<DX12TextureOverrideMatch> *matches,
	const std::vector<DX12CurrentShaderResourceBinding> *preResolvedGraphicsSrvs = nullptr)
{
	if (matches)
		matches->clear();
	if (target.kind == Bunny::CommandListTargetKind::IndexBuffer) {
		if (!iaState.hasIndexBuffer) {
			return false;
		}
		const DX12IaTargetTextureOverrideCacheKey cacheKey =
			MakeIaTargetTextureOverrideCacheKey(
				target,
				iaState.indexHash,
				iaState.indexView.BufferLocation,
				iaState.indexView.SizeInBytes,
				static_cast<uint32_t>(iaState.indexView.Format),
				vertexCount,
				indexCount,
				instanceCount,
				firstVertex,
				firstIndex,
				firstInstance);
		if (TryLoadIaTargetTextureOverrideCache(cacheKey, matches))
			return true;

		std::vector<DX12TextureOverrideMatch> resolvedMatches;
		std::unordered_set<const Bunny::TextureOverrideConfig*> seen;
		std::vector<const Bunny::TextureOverrideConfig*> configs;
		bool found = FindIaTextureOverridesCached(
			target, 0,
			vertexCount, indexCount, instanceCount,
			firstVertex, firstIndex, firstInstance, true, 0, &configs);
		AppendTextureOverrideMatches(configs, 0, &resolvedMatches, &seen);

		configs.clear();
		uint32_t matchedHash = iaState.indexHash;
		bool hashedFound = iaState.indexHash && FindIaTextureOverridesCached(
			target, iaState.indexHash,
			vertexCount, indexCount, instanceCount,
			firstVertex, firstIndex, firstInstance, true, 0, &configs);
		if (!hashedFound && iaState.indexHash) {
			const DX12IaRegionDraw draw = DX12IaRegionDraw::FromDrawArguments(
				indexCount != 0,
				vertexCount,
				indexCount,
				firstVertex,
				firstIndex,
				0);
			const uint32_t regionHash =
				DX12IaRegion::HashIndexBufferView(iaState.indexView, draw);
			if (regionHash && regionHash != iaState.indexHash) {
				hashedFound = FindIaTextureOverridesCached(
					target, regionHash,
					vertexCount, indexCount, instanceCount,
					firstVertex, firstIndex, firstInstance, true, 0, &configs);
				if (hashedFound)
					matchedHash = regionHash;
			}
		}
		AppendTextureOverrideMatches(configs, matchedHash, &resolvedMatches, &seen);
		SortTextureOverrideMatches(&resolvedMatches);
		found = !resolvedMatches.empty();
		StoreIaTargetTextureOverrideCache(cacheKey, resolvedMatches);
		if (matches)
			*matches = resolvedMatches;
		return found;
	}

	if (target.kind == Bunny::CommandListTargetKind::VertexBuffer) {
		const DX12IaBufferHash *slot = FindIaVertexSlot(iaState, target.slot);
		if (!slot)
			return false;
		const DX12IaTargetTextureOverrideCacheKey cacheKey =
			MakeIaTargetTextureOverrideCacheKey(
				target,
				slot->hash,
				slot->vertexView.BufferLocation,
				slot->vertexView.SizeInBytes,
				slot->vertexView.StrideInBytes,
				vertexCount,
				indexCount,
				instanceCount,
				firstVertex,
				firstIndex,
				firstInstance);
		if (TryLoadIaTargetTextureOverrideCache(cacheKey, matches))
			return true;

		std::vector<DX12TextureOverrideMatch> resolvedMatches;
		std::unordered_set<const Bunny::TextureOverrideConfig*> seen;
		std::vector<const Bunny::TextureOverrideConfig*> configs;
		bool found = FindIaTextureOverridesCached(
			target, 0,
			vertexCount, indexCount, instanceCount,
			firstVertex, firstIndex, firstInstance, false, target.slot, &configs);
		AppendTextureOverrideMatches(configs, 0, &resolvedMatches, &seen);

		configs.clear();
		uint32_t matchedHash = slot->hash;
		bool hashedFound = slot->hash && FindIaTextureOverridesCached(
			target, slot->hash,
			vertexCount, indexCount, instanceCount,
			firstVertex, firstIndex, firstInstance, false, target.slot, &configs);
		if (!hashedFound && slot->hash) {
			const DX12IaRegionDraw draw = DX12IaRegionDraw::FromDrawArguments(
				indexCount != 0,
				vertexCount,
				indexCount,
				firstVertex,
				firstIndex,
				0);
			const uint32_t regionHash =
				DX12IaRegion::HashVertexBufferView(target.slot, slot->vertexView, draw);
			if (regionHash && regionHash != slot->hash) {
				hashedFound = FindIaTextureOverridesCached(
					target, regionHash,
					vertexCount, indexCount, instanceCount,
					firstVertex, firstIndex, firstInstance, false, target.slot, &configs);
				if (hashedFound)
					matchedHash = regionHash;
			}
		}
		AppendTextureOverrideMatches(configs, matchedHash, &resolvedMatches, &seen);
		SortTextureOverrideMatches(&resolvedMatches);
		found = !resolvedMatches.empty();
		StoreIaTargetTextureOverrideCache(cacheKey, resolvedMatches);
		if (matches)
			*matches = resolvedMatches;
		return found;
	}

	if (target.kind == Bunny::CommandListTargetKind::ConstantBuffer ||
	    target.kind == Bunny::CommandListTargetKind::ShaderResource ||
	    target.kind == Bunny::CommandListTargetKind::UnorderedAccessView) {
		return FindDescriptorTextureOverrides(
			commandList, target, vertexCount, indexCount, instanceCount,
			firstVertex, firstIndex, firstInstance, matches,
			preResolvedGraphicsSrvs);
	}

	return false;
}

bool DX12ModHasAnyPotentialDirectIaTextureOverrideCandidate(
	const DX12ActiveIaState &ia,
	const DX12IaHashTargetPlan &targetPlan,
	bool indexed,
	uint32_t vertexCount,
	uint32_t indexCount,
	uint32_t instanceCount,
	uint32_t firstVertex,
	uint32_t firstIndex,
	uint32_t firstInstance)
{
	if (!targetPlan.hasTextureOverrideChecks ||
	    targetPlan.hasDescriptorTargets ||
	    targetPlan.requireAllIaBuffers ||
	    !targetPlan.HasIaTargets()) {
		return true;
	}

	// Reuse one scratch vector across the index-buffer probe and all
	// vertex-buffer slot probes so the merged prefilter buckets do not
	// allocate a fresh buffer for every slot.
	std::vector<size_t> prefilterScratch;

	if (targetPlan.RequiresIndexBuffer(indexed) &&
	    ia.hasIndexBuffer &&
	    HasPotentialIaTextureOverrideCandidateLocked(
			true,
			0,
			vertexCount,
			indexCount,
			instanceCount,
			firstVertex,
			firstIndex,
			firstInstance,
			&prefilterScratch)) {
		return true;
	}

	for (UINT slot = 0; slot < ARRAYSIZE(ia.vertexBuffers); ++slot) {
		if (!targetPlan.RequiresVertexBuffer(slot) || !ia.hasVertexBuffer[slot])
			continue;
		if (HasPotentialIaTextureOverrideCandidateLocked(
				false,
				slot,
				vertexCount,
				indexCount,
				instanceCount,
				firstVertex,
				firstIndex,
				firstInstance,
				&prefilterScratch)) {
			return true;
		}
	}

	return false;
}

// Returns true when the prefilter tables prove no candidate can exist for this
// (ib/vb, slot, hash) tuple, ignoring draw-context filters (match_first_index
// etc.). The prefilter tables are queried with zero counts: callers must only
// use this when gIaPrefilterCountSensitive == 0 so the count-indexed tables are
// empty and the generic tables are authoritative. Candidates that depend on
// active pre-skin sections still resolve through the live snapshot, and any
// candidate that survives hash + match_cs + IA-binding checks counts as
// "possibly present" so the per-draw path stays authoritative.
static bool IaHashDefinitelyHasNoCandidate(
	bool indexBuffer,
	uint32_t vertexSlot,
	uint32_t hash)
{
	std::vector<size_t> prefilterScratch;
	const std::vector<size_t> *candidateIndexes =
		FindIaTextureOverridePrefilterCandidatesLocked(
			indexBuffer, vertexSlot, 0, 0, &prefilterScratch);
	if (!candidateIndexes || candidateIndexes->empty())
		return true;

	const bool dependsOnActivePreSkin =
		IaCandidateIndexesDependOnActivePreSkin(*candidateIndexes);
	const std::unordered_set<std::wstring> &activePreSkinSections =
		dependsOnActivePreSkin ?
			GetActivePreSkinSectionsSnapshot() :
			EmptyActivePreSkinSections();

	for (size_t index : *candidateIndexes) {
		if (index >= gIaTextureOverrides.size())
			continue;
		const DX12IaTextureOverrideCandidate &candidate =
			gIaTextureOverrides[index];
		const Bunny::TextureOverrideConfig *config = candidate.config;
		if (!config || config->hash != hash)
			continue;
		if (!TextureOverrideMatchCsSatisfied(*config, activePreSkinSections))
			continue;
		if (!TextureOverrideMatchesIaBinding(
				*config, indexBuffer, vertexSlot))
			continue;
		return false;
	}
	return true;
}

// DX11-style IA-state-level negative check: the per-view hashes are computed
// (or reused from the resident cache) and matched against the candidate tables
// without any draw-context filtering. Only call when gIaPrefilterCountSensitive
// is 0; the result is then independent of vertex/index counts and offsets, so
// the draw path can cache it across draws for the same IA bindings.
bool DX12ModIaStateDefinitelyHasNoCandidate(
	const DX12ActiveIaState &ia,
	const DX12IaHashTargetPlan &targetPlan,
	bool indexed)
{
	if (!targetPlan.hasTextureOverrideChecks ||
	    targetPlan.hasDescriptorTargets ||
	    !targetPlan.HasIaTargets()) {
		return false;
	}
	// requireAllIaBuffers is handled below: every bound buffer is probed
	// against the candidate table, so an all-IA plan still resolves to
	// "definitely no candidate" when none of the bound views match.

	if (targetPlan.RequiresIndexBuffer(indexed) && ia.hasIndexBuffer) {
		const uint32_t indexHash =
			DX12CommandListRuntime::GetOrBuildCachedIndexViewHash(
				const_cast<DX12ActiveIaState*>(&ia));
		if (!IaHashDefinitelyHasNoCandidate(true, 0, indexHash))
			return false;
	}

	for (UINT slot = 0; slot < ARRAYSIZE(ia.vertexBuffers); ++slot) {
		if (!targetPlan.RequiresVertexBuffer(slot) || !ia.hasVertexBuffer[slot])
			continue;
		const uint32_t vertexHash =
			DX12CommandListRuntime::GetOrBuildCachedVertexViewHash(
				const_cast<DX12ActiveIaState*>(&ia), slot);
		if (!IaHashDefinitelyHasNoCandidate(false, slot, vertexHash))
			return false;
	}

	return true;
}

bool DX12ModIaPrefilterCountSensitive()
{
	return gIaPrefilterCountSensitive != 0;
}

static std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot>
LoadCommandExecutionRuntimeSnapshot()
{
	return gCommandExecutionRuntimeSnapshot.load(std::memory_order_acquire);
}

static bool CommandListRuntimeEffectLocked(
	const std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> &snapshot,
	const std::wstring &name)
{
	if (!snapshot)
		return true;
	auto it = snapshot->commandListRuntimeEffect.find(name);
	return it == snapshot->commandListRuntimeEffect.end() || it->second;
}

static bool CommandListRuntimeEffectLocked(
	const std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> &snapshot,
	const std::wstring &name,
	bool includePost)
{
	if (!snapshot)
		return true;
	const auto &effects = includePost ?
		snapshot->commandListPostRuntimeEffect :
		snapshot->commandListPreRuntimeEffect;
	if (!effects.empty()) {
		auto it = effects.find(name);
		return it == effects.end() || it->second;
	}
	return CommandListRuntimeEffectLocked(snapshot, name);
}


static bool CommandListRuntimeEffect(
	const std::wstring &name, bool includePost)
{
	return CommandListRuntimeEffectLocked(
		LoadCommandExecutionRuntimeSnapshot(), name, includePost);
}


static std::shared_ptr<const Bunny::CommandListConfig>
SnapshotCommandListConfig(const std::wstring &name)
{
	std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> snapshot =
		LoadCommandExecutionRuntimeSnapshot();
	if (!snapshot)
		return nullptr;
	auto it = snapshot->commandLists.find(name);
	return it == snapshot->commandLists.end() ? nullptr : it->second;
}

static void AppendResolvedDescriptorPrefilterSectionIds(
	const std::vector<DX12TriggeredTextureOverride> &source,
	std::vector<uint32_t> *sectionIds)
{
	if (!sectionIds)
		return;
	for (const DX12TriggeredTextureOverride &triggered : source) {
		if (!triggered.config || !triggered.sectionId)
			continue;
		sectionIds->push_back(triggered.sectionId);
	}
}

static bool HasPotentialResolvedDescriptorPrefilterCandidates(
	const DX12CommandExecutionRuntimeSnapshot &snapshot,
	uint32_t vertexCount,
	uint32_t indexCount)
{
	if (!snapshot.textureOverridesWithoutDrawContext.empty() ||
	    !snapshot.textureOverridesGenericDrawContext.empty())
		return true;
	if (indexCount) {
		auto indexed = snapshot.textureOverridesByIndexCount.find(indexCount);
		if (indexed != snapshot.textureOverridesByIndexCount.end() && !indexed->second.empty())
			return true;
	}
	if (vertexCount) {
		auto vertex = snapshot.textureOverridesByVertexCount.find(vertexCount);
		if (vertex != snapshot.textureOverridesByVertexCount.end() && !vertex->second.empty())
			return true;
	}
	return false;
}

static void CollectResolvedDescriptorPrefilterSectionIds(
	const DX12CommandExecutionRuntimeSnapshot &snapshot,
	uint32_t vertexCount,
	uint32_t indexCount,
	std::vector<uint32_t> *sectionIds)
{
	if (!sectionIds)
		return;
	sectionIds->clear();
	AppendResolvedDescriptorPrefilterSectionIds(
		snapshot.textureOverridesWithoutDrawContext,
		sectionIds);
	AppendResolvedDescriptorPrefilterSectionIds(
		snapshot.textureOverridesGenericDrawContext,
		sectionIds);
	if (indexCount) {
		auto indexed = snapshot.textureOverridesByIndexCount.find(indexCount);
		if (indexed != snapshot.textureOverridesByIndexCount.end())
			AppendResolvedDescriptorPrefilterSectionIds(indexed->second, sectionIds);
	}
	if (vertexCount) {
		auto vertex = snapshot.textureOverridesByVertexCount.find(vertexCount);
		if (vertex != snapshot.textureOverridesByVertexCount.end())
			AppendResolvedDescriptorPrefilterSectionIds(vertex->second, sectionIds);
	}
	std::sort(sectionIds->begin(), sectionIds->end());
	sectionIds->erase(std::unique(sectionIds->begin(), sectionIds->end()), sectionIds->end());
}

static DX12IaHashState EmptyIaHashState()
{
	DX12IaHashState state = {};
	return state;
}

class DX12TextureOverrideTargetCacheKey
{
public:
	uint32_t kind = 0;
	uint32_t stage = 0;
	uint32_t slot = 0;

	bool operator==(const DX12TextureOverrideTargetCacheKey &other) const
	{
		return kind == other.kind &&
			stage == other.stage &&
			slot == other.slot;
	}
};

class DX12TextureOverrideTargetCacheKeyHash
{
public:
	size_t operator()(const DX12TextureOverrideTargetCacheKey &key) const
	{
		UINT64 hash = 0x4d8b2f6d9c3145a7ull;
		hash = HashCombine64(hash, key.kind);
		hash = HashCombine64(hash, key.stage);
		hash = HashCombine64(hash, key.slot);
		return static_cast<size_t>(hash ^ (hash >> 32));
	}
};

class DX12DirectDescriptorPrefilterCache
{
public:
	UINT64 reloadGeneration = 0;
	ID3D12GraphicsCommandList *commandList = nullptr;
	UINT64 descriptorQuerySerial = 0;
	bool usesGraphicsRootRevisions = false;
	std::vector<UINT> graphicsRootParameters;
	std::vector<UINT64> graphicsRootRevisions;
	ID3D12DescriptorHeap *descriptorHeap = nullptr;
	UINT64 descriptorHeapVersion = 0;
	UINT64 textureHashGeneration = 0;
	UINT64 targetPlanFingerprint = 0;
	uint32_t vertexCount = 0;
	uint32_t indexCount = 0;
	uint32_t instanceCount = 0;
	uint32_t firstVertex = 0;
	uint32_t firstIndex = 0;
	uint32_t firstInstance = 0;
	bool hasCandidate = false;
	std::unordered_map<
		DX12TextureOverrideTargetCacheKey,
		std::vector<DX12TextureOverrideMatch>,
		DX12TextureOverrideTargetCacheKeyHash> matchesByTarget;

	void Clear()
	{
		reloadGeneration = 0;
		commandList = nullptr;
		descriptorQuerySerial = 0;
		usesGraphicsRootRevisions = false;
		graphicsRootParameters.clear();
		graphicsRootRevisions.clear();
		descriptorHeap = nullptr;
		descriptorHeapVersion = 0;
		textureHashGeneration = 0;
		targetPlanFingerprint = 0;
		vertexCount = 0;
		indexCount = 0;
		instanceCount = 0;
		firstVertex = 0;
		firstIndex = 0;
		firstInstance = 0;
		hasCandidate = false;
		matchesByTarget.clear();
	}

	void SetInvocation(
		UINT64 generation,
		ID3D12GraphicsCommandList *list,
		const DX12SimulateContext *runtimeState,
		UINT64 currentDescriptorQuerySerial,
		ID3D12DescriptorHeap *currentDescriptorHeap,
		UINT64 currentDescriptorHeapVersion,
		UINT64 currentTextureHashGeneration,
		UINT64 currentTargetPlanFingerprint,
		uint32_t drawVertexCount,
		uint32_t drawIndexCount,
		uint32_t drawInstanceCount,
		uint32_t drawFirstVertex,
		uint32_t drawFirstIndex,
		uint32_t drawFirstInstance)
	{
		if (MatchesInvocation(
				generation,
				list,
				runtimeState,
				currentDescriptorQuerySerial,
				currentDescriptorHeap,
				currentDescriptorHeapVersion,
				currentTextureHashGeneration,
				currentTargetPlanFingerprint,
				drawVertexCount,
				drawIndexCount,
				drawInstanceCount,
				drawFirstVertex,
				drawFirstIndex,
				drawFirstInstance)) {
			return;
		}
		reloadGeneration = generation;
		commandList = list;
		descriptorQuerySerial = currentDescriptorQuerySerial;
		usesGraphicsRootRevisions = false;
		graphicsRootParameters.clear();
		graphicsRootRevisions.clear();
		descriptorHeap = currentDescriptorHeap;
		descriptorHeapVersion = currentDescriptorHeapVersion;
		textureHashGeneration = currentTextureHashGeneration;
		targetPlanFingerprint = currentTargetPlanFingerprint;
		vertexCount = drawVertexCount;
		indexCount = drawIndexCount;
		instanceCount = drawInstanceCount;
		firstVertex = drawFirstVertex;
		firstIndex = drawFirstIndex;
		firstInstance = drawFirstInstance;
		hasCandidate = false;
		matchesByTarget.clear();
	}

	bool MatchesInvocation(
		UINT64 generation,
		ID3D12GraphicsCommandList *list,
		const DX12SimulateContext *runtimeState,
		UINT64 currentDescriptorQuerySerial,
		ID3D12DescriptorHeap *currentDescriptorHeap,
		UINT64 currentDescriptorHeapVersion,
		UINT64 currentTextureHashGeneration,
		UINT64 currentTargetPlanFingerprint,
		uint32_t drawVertexCount,
		uint32_t drawIndexCount,
		uint32_t drawInstanceCount,
		uint32_t drawFirstVertex,
		uint32_t drawFirstIndex,
		uint32_t drawFirstInstance) const
	{
		if (reloadGeneration != generation ||
			commandList != list ||
			descriptorHeap != currentDescriptorHeap ||
			descriptorHeapVersion != currentDescriptorHeapVersion ||
			textureHashGeneration != currentTextureHashGeneration ||
			targetPlanFingerprint != currentTargetPlanFingerprint ||
			vertexCount != drawVertexCount ||
			indexCount != drawIndexCount ||
			instanceCount != drawInstanceCount ||
			firstVertex != drawFirstVertex ||
			firstIndex != drawFirstIndex ||
			firstInstance != drawFirstInstance) {
			return false;
		}
		if (!usesGraphicsRootRevisions)
			return descriptorQuerySerial == currentDescriptorQuerySerial;
		if (!runtimeState ||
			graphicsRootParameters.size() != graphicsRootRevisions.size())
			return false;
		for (size_t index = 0; index < graphicsRootParameters.size(); ++index) {
			const UINT root = graphicsRootParameters[index];
			if (root >= runtimeState->graphicsRootBindingRevisions.size() ||
				runtimeState->graphicsRootBindingRevisions[root] !=
					graphicsRootRevisions[index])
				return false;
		}
		return true;
	}

	void StoreGraphicsRootDependencies(
		const DX12SimulateContext *runtimeState,
		bool dependenciesKnown,
		const std::array<bool, MaxRootParameters> &rootSeen)
	{
		usesGraphicsRootRevisions = dependenciesKnown && runtimeState;
		graphicsRootParameters.clear();
		graphicsRootRevisions.clear();
		if (!usesGraphicsRootRevisions)
			return;
		for (UINT root = 0; root < rootSeen.size(); ++root) {
			if (!rootSeen[root])
				continue;
			graphicsRootParameters.push_back(root);
			graphicsRootRevisions.push_back(
				runtimeState->graphicsRootBindingRevisions[root]);
		}
		if (graphicsRootParameters.empty())
			usesGraphicsRootRevisions = false;
	}

	bool MatchesInvocation(
		UINT64 generation,
		ID3D12GraphicsCommandList *list,
		uint32_t drawVertexCount,
		uint32_t drawIndexCount,
		uint32_t drawInstanceCount,
		uint32_t drawFirstVertex,
		uint32_t drawFirstIndex,
		uint32_t drawFirstInstance) const
	{
		return reloadGeneration == generation &&
			commandList == list &&
			vertexCount == drawVertexCount &&
			indexCount == drawIndexCount &&
			instanceCount == drawInstanceCount &&
			firstVertex == drawFirstVertex &&
			firstIndex == drawFirstIndex &&
			firstInstance == drawFirstInstance;
	}
};

static thread_local DX12DirectDescriptorPrefilterCache tlsDirectDescriptorPrefilterCache;

// Draw-prefilter SRV resolution cache. Collecting and hashing the current
// graphics SRV bindings is the dominant per-draw cost of descriptor-target
// overrides. The collection result depends only on the bound root tables and
// the referenced descriptor contents - NOT on the PSO or draw arguments
// (the compiled target plan is applied afterwards, per draw). The cache is
// validated per draw against (a) the root signature (its register mapping
// defines the collection), (b) the resident root-table GPU handles, and
// (c) the observed descriptor slots' published identities. All three come
// from the wrapper's own tracking, so a hit is exactly as correct as a fresh
// collection; this mirrors the DX11 model of bind-time state instead of a
// per-draw rebuild.
struct PrefilterSrvResolutionCacheEntry
{
	bool valid = false;
	ID3D12GraphicsCommandList *commandList = nullptr;
	UINT64 reloadGeneration = 0;
	UINT64 textureHashGeneration = 0;
	ID3D12RootSignature *graphicsRootSignature = nullptr;
	UINT64 descriptorQuerySerial = 0;
	// stageIndex 0 = graphics (all SRV bindings), 1 = compute (unused here).
	std::vector<DX12CurrentShaderResourceBinding> bindings[2];
	std::vector<DX12ResolvedGraphicsSrvBinding> hashed[2];
	ID3D12DescriptorHeap *observedHeap = nullptr;
	std::vector<UINT> observedSlots;
	std::vector<UINT64> observedSlotVersions;
	std::vector<UINT64> observedSlotValueIds;
};
static thread_local PrefilterSrvResolutionCacheEntry tlsPrefilterSrvResolutionCache;

static bool PrefilterSrvCacheTryLoad(
	ID3D12GraphicsCommandList *commandList,
	DX12SimulateContext *runtimeState,
	UINT stageIndex,
	UINT64 reloadGeneration,
	std::vector<DX12ResolvedGraphicsSrvBinding> *outHashed)
{
	PrefilterSrvResolutionCacheEntry &cache = tlsPrefilterSrvResolutionCache;
	if (!cache.valid || !outHashed || !runtimeState ||
	    cache.commandList != commandList ||
	    cache.reloadGeneration != reloadGeneration ||
	    cache.textureHashGeneration != DX12GetTextureHashGeneration() ||
	    cache.graphicsRootSignature != runtimeState->graphicsRootSignature)
		return false;

	// Root tables must still hold the exact GPU handles observed at fill.
	for (UINT s = 0; s < 2; ++s) {
		for (const DX12CurrentShaderResourceBinding &binding : cache.bindings[s]) {
			if (binding.rootParameterIndex >= MaxRootParameters)
				return false;
			const RootTableState &table =
				runtimeState->graphicsRootTables[binding.rootParameterIndex];
			if (!table.valid ||
			    table.baseDescriptor.ptr != binding.tableGpuStart.ptr)
				return false;
		}
	}

	// Observed descriptor slots must still hold the same published content.
	// The query-serial gate skips the slot reads entirely when no descriptor
	// write happened since the fill (the common case between draws).
	if (!cache.observedSlots.empty()) {
		const UINT64 serialBefore = CurrentDescriptorQuerySerial();
		if (cache.descriptorQuerySerial != serialBefore) {
			if (!cache.observedHeap)
				return false;
			constexpr size_t StackIdentityCount = 64;
			std::array<UINT64, StackIdentityCount> stackVersions;
			std::array<UINT64, StackIdentityCount> stackValueIds;
			static thread_local std::vector<UINT64> tlsVersions;
			static thread_local std::vector<UINT64> tlsValueIds;
			UINT64 *versions = stackVersions.data();
			UINT64 *valueIds = stackValueIds.data();
			const size_t count = cache.observedSlots.size();
			if (count > StackIdentityCount) {
				tlsVersions.resize(count);
				tlsValueIds.resize(count);
				versions = tlsVersions.data();
				valueIds = tlsValueIds.data();
			}
			if (!DX12GetDescriptorSlotIdentities(
					cache.observedHeap,
					cache.observedSlots.data(),
					count,
					versions,
					valueIds))
				return false;
			for (size_t index = 0; index < count; ++index) {
				if (versions[index] != cache.observedSlotVersions[index] ||
				    valueIds[index] != cache.observedSlotValueIds[index])
					return false;
			}
			const UINT64 serialAfter = CurrentDescriptorQuerySerial();
			if (serialAfter != serialBefore)
				return false;
			cache.descriptorQuerySerial = serialAfter;
		}
	}

	*outHashed = cache.hashed[stageIndex];
	return true;
}

static void PrefilterSrvCacheStore(
	ID3D12GraphicsCommandList *commandList,
	DX12SimulateContext *runtimeState,
	UINT stageIndex,
	UINT64 reloadGeneration,
	const std::vector<DX12CurrentShaderResourceBinding> &bindings,
	const std::vector<DX12ResolvedGraphicsSrvBinding> &hashed)
{
	PrefilterSrvResolutionCacheEntry &cache = tlsPrefilterSrvResolutionCache;
	cache.valid = false;
	if (!runtimeState || bindings.empty() || hashed.empty())
		return;

	cache.bindings[stageIndex] = bindings;
	cache.hashed[stageIndex] = hashed;
	cache.commandList = commandList;
	cache.reloadGeneration = reloadGeneration;
	cache.textureHashGeneration = DX12GetTextureHashGeneration();
	cache.graphicsRootSignature = runtimeState->graphicsRootSignature;
	cache.descriptorQuerySerial = CurrentDescriptorQuerySerial();
	cache.observedHeap = nullptr;
	cache.observedSlots.clear();
	cache.observedSlotVersions.clear();
	cache.observedSlotValueIds.clear();

	// Cache only when every binding can be identity-validated: table-based
	// bindings with a resolvable CPU handle and a published slot identity.
	// Any binding that fails (inline root descriptor, unresolvable handle,
	// unpublished slot, multiple heaps) disables caching for the entry.
	ID3D12DescriptorHeap *observedHeap = nullptr;
	for (const DX12CurrentShaderResourceBinding &binding : bindings) {
		if (binding.rootDescriptor || binding.tableCpuStart == 0 ||
		    binding.descriptorIncrement == 0)
			return;
		const SIZE_T cpuHandle = binding.tableCpuStart +
			static_cast<SIZE_T>(binding.descriptorOffset) *
				binding.descriptorIncrement;
		DX12DescriptorHeapShadowUtils::ResolvedHeapSlot resolved;
		if (!DX12DescriptorHeapShadowUtils::ResolveHeapSlotFast(
				cpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES, &resolved) ||
		    !resolved.heap || !resolved.heap->stateLocks)
			return;
		DescriptorHeapRecord::StateLocks::SlotIdentity identity;
		if (!resolved.heap->stateLocks->ObserveSlotIdentity(
				resolved.slot, &identity) ||
		    identity.valueId == 0)
			return;
		if (!observedHeap) {
			observedHeap = resolved.heap->heap;
		} else if (observedHeap != resolved.heap->heap) {
			return;
		}
		cache.observedSlots.push_back(resolved.slot);
		cache.observedSlotVersions.push_back(identity.version);
		cache.observedSlotValueIds.push_back(identity.valueId);
	}
	if (!observedHeap || cache.observedSlots.empty())
		return;
	cache.observedHeap = observedHeap;
	cache.valid = true;
}

static void ResolveGraphicsSrvBindings(
	ID3D12GraphicsCommandList *commandList,
	ID3D12Device *cachedDevice,
	const std::vector<DX12CurrentShaderResourceBinding> &bindings,
	std::vector<DX12ResolvedGraphicsSrvBinding> *resolvedBindings)
{
	if (!resolvedBindings)
		return;
	resolvedBindings->clear();
	if (bindings.empty())
		return;

	resolvedBindings->reserve(bindings.size());
	for (const DX12CurrentShaderResourceBinding &binding : bindings) {
		DX12ResolvedGraphicsSrvBinding resolved;
		resolved.binding = binding;
		resolved.resourceHash = HashDescriptorBindingResource(binding);
		resolved.descriptorHash =
			HashDescriptorBinding(commandList, cachedDevice, binding);
		resolvedBindings->push_back(std::move(resolved));
	}
}

static bool TryLoadResolvedGraphicsSrvBindings(
	DX12SimulateContext *runtimeState,
	UINT64 descriptorQuerySerial,
	std::vector<DX12ResolvedGraphicsSrvBinding> *resolvedBindings)
{
	if (!runtimeState || !resolvedBindings)
		return false;
	if (!runtimeState->resolvedGraphicsSrvs.valid ||
	    (!runtimeState->resolvedGraphicsSrvs.usesRootRevisions &&
	     runtimeState->resolvedGraphicsSrvs.descriptorQuerySerial != descriptorQuerySerial)) {
		return false;
	}
	if (runtimeState->resolvedGraphicsSrvs.usesRootRevisions) {
		if (runtimeState->resolvedGraphicsSrvs.rootParameters.size() !=
			runtimeState->resolvedGraphicsSrvs.rootRevisions.size())
			return false;
		for (size_t index = 0;
			 index < runtimeState->resolvedGraphicsSrvs.rootParameters.size();
			 ++index) {
			const UINT root = runtimeState->resolvedGraphicsSrvs.rootParameters[index];
			if (root >= runtimeState->graphicsRootBindingRevisions.size() ||
				runtimeState->graphicsRootBindingRevisions[root] !=
					runtimeState->resolvedGraphicsSrvs.rootRevisions[index])
				return false;
		}
	}
	*resolvedBindings = runtimeState->resolvedGraphicsSrvs.bindings;
	return true;
}

static void StoreResolvedGraphicsSrvBindings(
	DX12SimulateContext *runtimeState,
	UINT64 descriptorQuerySerial,
	const std::vector<DX12ResolvedGraphicsSrvBinding> &resolvedBindings)
{
	if (!runtimeState)
		return;
	runtimeState->resolvedGraphicsSrvs.valid = true;
	runtimeState->resolvedGraphicsSrvs.descriptorQuerySerial = descriptorQuerySerial;
	runtimeState->resolvedGraphicsSrvs.rootParameters.clear();
	runtimeState->resolvedGraphicsSrvs.rootRevisions.clear();
	runtimeState->resolvedGraphicsSrvs.usesRootRevisions = !resolvedBindings.empty();
	if (runtimeState->resolvedGraphicsSrvs.usesRootRevisions) {
		std::array<bool, MaxRootParameters> rootSeen = {};
		for (const DX12ResolvedGraphicsSrvBinding &binding : resolvedBindings) {
			const UINT root = binding.binding.rootParameterIndex;
			if (root >= runtimeState->graphicsRootBindingRevisions.size()) {
				runtimeState->resolvedGraphicsSrvs.usesRootRevisions = false;
				runtimeState->resolvedGraphicsSrvs.rootParameters.clear();
				runtimeState->resolvedGraphicsSrvs.rootRevisions.clear();
				break;
			}
			if (rootSeen[root])
				continue;
			rootSeen[root] = true;
			runtimeState->resolvedGraphicsSrvs.rootParameters.push_back(root);
			runtimeState->resolvedGraphicsSrvs.rootRevisions.push_back(
				runtimeState->graphicsRootBindingRevisions[root]);
		}
	}
	runtimeState->resolvedGraphicsSrvs.bindings = resolvedBindings;
}

static void FillSyntheticTextureBinding(
	const Bunny::CommandListTarget &target,
	ID3D12Resource *resource,
	DXGI_FORMAT format,
	UINT width,
	UINT height,
	DX12CurrentShaderResourceBinding *binding)
{
	if (!binding)
		return;
	*binding = DX12CurrentShaderResourceBinding();
	binding->shaderRegister = target.slot;
	binding->registerSpace = 0;
	binding->shaderVisibility =
		target.stage == Bunny::CommandListShaderStage::Vertex ?
			D3D12_SHADER_VISIBILITY_VERTEX :
		target.stage == Bunny::CommandListShaderStage::Pixel ?
			D3D12_SHADER_VISIBILITY_PIXEL :
			D3D12_SHADER_VISIBILITY_ALL;
	if (!resource)
		return;
	binding->hasDescriptor = true;
	binding->rootDescriptor = false;
	binding->descriptor.kind = "SRV";
	binding->descriptor.resource = resource;
	binding->descriptor.hasResourceDesc = true;
	binding->descriptor.resourceDesc.Dimension =
		D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	binding->descriptor.resourceDesc.Width = width;
	binding->descriptor.resourceDesc.Height = height;
	binding->descriptor.resourceDesc.DepthOrArraySize = 1;
	binding->descriptor.resourceDesc.MipLevels = 1;
	binding->descriptor.resourceDesc.Format = format;
	binding->descriptor.viewFormat = static_cast<UINT>(format);
}


static bool FindDescriptorTextureOverridesFromResolvedBindings(
	const std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> &snapshot,
	const Bunny::CommandListTarget &target,
	const std::vector<DX12ResolvedGraphicsSrvBinding> &resolvedBindings,
	uint32_t vertexCount,
	uint32_t indexCount,
	uint32_t instanceCount,
	uint32_t firstVertex,
	uint32_t firstIndex,
	uint32_t firstInstance,
	std::vector<DX12TextureOverrideMatch> *matches)
{
	if (matches)
		matches->clear();
	if (!snapshot || resolvedBindings.empty())
		return false;

	const bool compute = target.stage == Bunny::CommandListShaderStage::Compute;
	if (!HasPotentialResolvedDescriptorPrefilterCandidates(
			*snapshot,
			vertexCount,
			indexCount))
		return false;

	static thread_local std::vector<uint32_t> tlsAllowedSectionIds;
	CollectResolvedDescriptorPrefilterSectionIds(
		*snapshot,
		vertexCount,
		indexCount,
		&tlsAllowedSectionIds);
	if (tlsAllowedSectionIds.empty())
		return false;

	std::vector<DX12TextureOverrideMatch> candidates;
	for (const DX12ResolvedGraphicsSrvBinding &resolved : resolvedBindings) {
		if (!DescriptorBindingMatchesTarget(resolved.binding, target))
			continue;
		if (resolved.resourceHash) {
			AppendTextureOverrideCandidateMatchesByHash(
				*snapshot,
				resolved.resourceHash,
				resolved.resourceHash,
				resolved.descriptorHash,
				&resolved.binding,
				compute,
				&tlsAllowedSectionIds,
				&candidates);
		}
		if (resolved.descriptorHash &&
		    resolved.descriptorHash != resolved.resourceHash) {
			AppendTextureOverrideCandidateMatchesByHash(
				*snapshot,
				resolved.descriptorHash,
				resolved.resourceHash,
				resolved.descriptorHash,
				&resolved.binding,
				compute,
				&tlsAllowedSectionIds,
				&candidates);
		}
	}

	return FilterDescriptorTextureOverrideCandidates(
		candidates,
		vertexCount,
		indexCount,
		instanceCount,
		firstVertex,
		firstIndex,
		firstInstance,
		matches);
}

static void AppendGraphicsSrvTextureOverrideCandidatesForTarget(
	const std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> &snapshot,
	const DX12ResolvedGraphicsSrvBinding &resolved,
	const DX12TextureOverrideTargetCacheKey &cacheKey,
	const std::vector<uint32_t> &allowedSectionIds,
	std::unordered_map<
		DX12TextureOverrideTargetCacheKey,
		std::vector<DX12TextureOverrideMatch>,
		DX12TextureOverrideTargetCacheKeyHash> *matchesByTarget)
{
	if (!snapshot || !matchesByTarget)
		return;
	std::vector<DX12TextureOverrideMatch> &candidates =
		(*matchesByTarget)[cacheKey];
	if (resolved.resourceHash) {
		AppendTextureOverrideCandidateMatchesByHash(
			*snapshot,
			resolved.resourceHash,
			resolved.resourceHash,
			resolved.descriptorHash,
			&resolved.binding,
			false,
			&allowedSectionIds,
			&candidates);
	}
	if (resolved.descriptorHash &&
		resolved.descriptorHash != resolved.resourceHash) {
		AppendTextureOverrideCandidateMatchesByHash(
			*snapshot,
			resolved.descriptorHash,
			resolved.resourceHash,
			resolved.descriptorHash,
			&resolved.binding,
			false,
			&allowedSectionIds,
			&candidates);
	}
}

static void AppendResolvedBindingsToDescriptorTextureOverrideCache(
	const std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> &snapshot,
	const std::vector<DX12ResolvedGraphicsSrvBinding> &resolvedBindings,
	Bunny::CommandListTargetKind kind,
	bool compute,
	const std::vector<uint32_t> &allowedSectionIds,
	std::unordered_map<
		DX12TextureOverrideTargetCacheKey,
		std::vector<DX12TextureOverrideMatch>,
		DX12TextureOverrideTargetCacheKeyHash> *matchesByTarget)
{
	if (!snapshot || !matchesByTarget)
		return;
	for (const DX12ResolvedGraphicsSrvBinding &resolved : resolvedBindings) {
		if (resolved.binding.registerSpace != 0)
			continue;
		if (compute) {
			AppendGraphicsSrvTextureOverrideCandidatesForTarget(
				snapshot,
				resolved,
				DX12TextureOverrideTargetCacheKey{
					static_cast<uint32_t>(kind),
					static_cast<uint32_t>(Bunny::CommandListShaderStage::Compute),
					resolved.binding.shaderRegister
				},
				allowedSectionIds,
				matchesByTarget);
			continue;
		}
		if (resolved.binding.shaderVisibility == D3D12_SHADER_VISIBILITY_VERTEX ||
			resolved.binding.shaderVisibility == D3D12_SHADER_VISIBILITY_ALL) {
			AppendGraphicsSrvTextureOverrideCandidatesForTarget(
				snapshot,
				resolved,
				DX12TextureOverrideTargetCacheKey{
					static_cast<uint32_t>(kind),
					static_cast<uint32_t>(Bunny::CommandListShaderStage::Vertex),
					resolved.binding.shaderRegister
				},
				allowedSectionIds,
				matchesByTarget);
		}
		if (resolved.binding.shaderVisibility == D3D12_SHADER_VISIBILITY_PIXEL ||
			resolved.binding.shaderVisibility == D3D12_SHADER_VISIBILITY_ALL) {
			AppendGraphicsSrvTextureOverrideCandidatesForTarget(
				snapshot,
				resolved,
				DX12TextureOverrideTargetCacheKey{
					static_cast<uint32_t>(kind),
					static_cast<uint32_t>(Bunny::CommandListShaderStage::Pixel),
					resolved.binding.shaderRegister
				},
				allowedSectionIds,
				matchesByTarget);
		}
	}
}


static void FillSyntheticBufferBinding(
	const Bunny::CommandListTarget &target,
	ID3D12Resource *resource,
	const std::string &kind,
	UINT64 viewBytes,
	UINT stride,
	DX12CurrentShaderResourceBinding *binding)
{
	if (!binding)
		return;
	*binding = DX12CurrentShaderResourceBinding();
	binding->shaderRegister = target.slot;
	binding->registerSpace = 0;
	binding->shaderVisibility =
		target.stage == Bunny::CommandListShaderStage::Vertex ?
			D3D12_SHADER_VISIBILITY_VERTEX :
		target.stage == Bunny::CommandListShaderStage::Pixel ?
			D3D12_SHADER_VISIBILITY_PIXEL :
			D3D12_SHADER_VISIBILITY_ALL;
	if (!resource)
		return;
	binding->hasDescriptor = true;
	binding->rootDescriptor = false;
	binding->descriptor.kind = kind;
	binding->descriptor.resource = resource;
	binding->descriptor.hasResourceDesc = true;
	binding->descriptor.resourceDesc.Dimension =
		D3D12_RESOURCE_DIMENSION_BUFFER;
	binding->descriptor.resourceDesc.Width = viewBytes;
	binding->descriptor.resourceDesc.Height = 1;
	binding->descriptor.resourceDesc.DepthOrArraySize = 1;
	binding->descriptor.resourceDesc.MipLevels = 1;
	binding->descriptor.resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
	binding->descriptor.resourceDesc.SampleDesc.Count = 1;
	binding->descriptor.resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	binding->descriptor.gpuVirtualAddress = resource->GetGPUVirtualAddress();
	binding->descriptor.viewSize = viewBytes;
	binding->descriptor.bufferViewOffset = 0;
	binding->descriptor.bufferViewBytes = viewBytes;
	binding->descriptor.structureByteStride = stride;
	if (target.kind == Bunny::CommandListTargetKind::ConstantBuffer) {
		binding->descriptor.viewDimension = 0;
		binding->descriptor.cbvSize = static_cast<UINT>(
			(std::min)(viewBytes, static_cast<UINT64>(UINT_MAX)));
	} else if (target.kind == Bunny::CommandListTargetKind::ShaderResource) {
		binding->descriptor.viewDimension = D3D12_SRV_DIMENSION_BUFFER;
		binding->descriptor.firstElement = 0;
		binding->descriptor.numElements = stride ?
			static_cast<UINT>((std::min)(
				viewBytes / stride,
				static_cast<UINT64>(UINT_MAX))) : 0;
	} else if (target.kind == Bunny::CommandListTargetKind::UnorderedAccessView) {
		binding->descriptor.viewDimension = D3D12_UAV_DIMENSION_BUFFER;
		binding->descriptor.firstElement = 0;
		binding->descriptor.numElements = stride ?
			static_cast<UINT>((std::min)(
				viewBytes / stride,
				static_cast<UINT64>(UINT_MAX))) : 0;
	}
}

static bool BuildResolvedDescriptorBindingsForResourceOverride(
	ID3D12Device *device,
	ID3D12GraphicsCommandList *commandList,
	const Bunny::CommandListTarget &target,
	const std::wstring &resourceName,
	const std::vector<DX12ResolvedGraphicsSrvBinding> *baseBindings,
	std::vector<DX12ResolvedGraphicsSrvBinding> *resolvedBindings)
{
	if (!resolvedBindings)
		return false;
	resolvedBindings->clear();
	if (!device || !commandList || resourceName.empty())
		return false;

	if (target.kind == Bunny::CommandListTargetKind::ShaderResource) {
		DX12LoadedResource *textureResource =
			DX12DdsTextureLoadUtils::EnsureLoadedDrawTextureSrv(
				device, commandList, resourceName);
		if (textureResource) {
			ID3D12Resource *texture = textureResource->drawSrvTextureResource ?
				textureResource->drawSrvTextureResource :
				textureResource->resource;
			uint32_t textureHash = 0;
			if (texture &&
			    DX12TryGetTextureHash(texture, &textureHash) &&
			    textureHash) {
				if (baseBindings && !baseBindings->empty()) {
					for (const DX12ResolvedGraphicsSrvBinding &base : *baseBindings) {
						DX12ResolvedGraphicsSrvBinding resolved = base;
						resolved.resourceHash = textureHash;
						resolved.descriptorHash = textureHash;
						FillSyntheticTextureBinding(
							target,
							texture,
							textureResource->drawSrvFormat,
							textureResource->drawSrvWidth,
							textureResource->drawSrvHeight,
							&resolved.binding);
						resolvedBindings->push_back(std::move(resolved));
					}
					return !resolvedBindings->empty();
				}

				DX12ResolvedGraphicsSrvBinding synthetic;
				FillSyntheticTextureBinding(
					target,
					texture,
					textureResource->drawSrvFormat,
					textureResource->drawSrvWidth,
					textureResource->drawSrvHeight,
					&synthetic.binding);
				synthetic.resourceHash = textureHash;
				synthetic.descriptorHash = textureHash;
				resolvedBindings->push_back(std::move(synthetic));
				return true;
			}
		}
	}

	DX12LoadedResource *resource = EnsureLoadedResourceForPreSkin(device, resourceName);
	if (!resource)
		return false;
	const UINT stride = resource->stride ? resource->stride : 4;
	ID3D12Resource *bindingResource = resource->resource;
	UINT64 viewBytes = resource->byteWidth;
	std::string kind(DescriptorKind::SRV);

	if (target.kind == Bunny::CommandListTargetKind::ShaderResource) {
		if (!EnsureResourceSrvLocked(device, resource, stride))
			return false;
		bindingResource = resource->srvResource ? resource->srvResource : resource->resource;
		viewBytes = resource->srvByteWidth ? resource->srvByteWidth : resource->byteWidth;
		kind = std::string(DescriptorKind::SRV);
	} else if (target.kind == Bunny::CommandListTargetKind::ConstantBuffer) {
		kind = std::string(DescriptorKind::CBV);
	} else if (target.kind == Bunny::CommandListTargetKind::UnorderedAccessView) {
		if (!EnsureResourceUavLocked(device, commandList, resource, stride, 0, true, nullptr))
			return false;
		bindingResource = resource->uavResource ? resource->uavResource : resource->resource;
		viewBytes = resource->uavByteWidth ? resource->uavByteWidth : resource->byteWidth;
		kind = std::string(DescriptorKind::UAV);
	} else {
		return false;
	}

	if (!bindingResource || !viewBytes)
		return false;

	DX12ResolvedGraphicsSrvBinding synthetic;
	FillSyntheticBufferBinding(
		target,
		bindingResource,
		kind,
		viewBytes,
		stride,
		&synthetic.binding);
	synthetic.resourceHash = HashDescriptorBindingResource(synthetic.binding);
	synthetic.descriptorHash =
		HashDescriptorBinding(commandList, device, synthetic.binding);
	resolvedBindings->push_back(std::move(synthetic));
	return true;
}

class DX12CommandListExecutorScratch
{
public:
	std::vector<std::wstring> commandListStack;
	std::unordered_set<std::wstring> commandListStackSet;
	std::vector<Bunny::CommandListTarget> descriptorTargetScratch;
	std::vector<DX12TextureOverrideMatch> targetMatchScratch;
	std::vector<DX12CurrentShaderResourceBinding> graphicsSrvBindings;
	std::vector<DX12ResolvedGraphicsSrvBinding> resolvedGraphicsSrvBindings;
	std::unordered_map<
		DX12TextureOverrideTargetCacheKey,
		std::vector<DX12ResolvedGraphicsSrvBinding>,
		DX12TextureOverrideTargetCacheKeyHash> graphicsSrvBindingsByTarget;
	std::unordered_map<
		DX12TextureOverrideTargetCacheKey,
		std::vector<DX12TextureOverrideMatch>,
		DX12TextureOverrideTargetCacheKeyHash> graphicsSrvTextureOverrideCache;
	std::unordered_map<
		DX12TextureOverrideTargetCacheKey,
		std::vector<DX12ResolvedGraphicsSrvBinding>,
		DX12TextureOverrideTargetCacheKeyHash> shaderResourceOverrideBindingsByTarget;
	std::unordered_map<
		DX12TextureOverrideTargetCacheKey,
		std::vector<DX12TextureOverrideMatch>,
		DX12TextureOverrideTargetCacheKeyHash> targetTextureOverrideCache;

	void Reset()
	{
		commandListStack.clear();
		commandListStackSet.clear();
		descriptorTargetScratch.clear();
		targetMatchScratch.clear();
		graphicsSrvBindings.clear();
		resolvedGraphicsSrvBindings.clear();
		graphicsSrvBindingsByTarget.clear();
		graphicsSrvTextureOverrideCache.clear();
		shaderResourceOverrideBindingsByTarget.clear();
		targetTextureOverrideCache.clear();
	}
};

class DX12CommandListExecutor
{
public:
	inline static thread_local DX12CommandListExecutorScratch ScratchPool[18] = {};
	inline static thread_local UINT ScratchDepth = 0;

	static DX12CommandListExecutorScratch *AcquireScratch(
		bool *pooled,
		DX12CommandListExecutorScratch *fallback)
	{
		if (ScratchDepth < ARRAYSIZE(ScratchPool)) {
			DX12CommandListExecutorScratch *scratch = &ScratchPool[ScratchDepth++];
			scratch->Reset();
			*pooled = true;
			return scratch;
		}
		fallback->Reset();
		*pooled = false;
		return fallback;
	}

	class CommandVmRunScope
	{
	public:
		MigotoCommandVmUtils::State *state = nullptr;
		size_t branchDepth = 0;
		bool flushPost = false;

		CommandVmRunScope(MigotoCommandVmUtils::State *vmState, bool flushPostAssignments)
			: state(vmState),
			  flushPost(flushPostAssignments)
		{
			if (!state)
				return;
			branchDepth = state->branches.size();
			state->PushLocalScope();
		}

		~CommandVmRunScope()
		{
			if (!state)
				return;
			if (state->branches.size() > branchDepth)
				state->branches.resize(branchDepth);
			if (flushPost)
				state->FlushPostAssignments();
			state->PopLocalScope();
		}
	};

	class BoolScope
	{
	public:
		BoolScope(bool *flag, bool value)
			: target(flag),
			  saved(flag ? *flag : false)
		{
			if (target)
				*target = value;
		}

		~BoolScope()
		{
			if (target)
				*target = saved;
		}

	private:
		bool *target = nullptr;
		bool saved = false;
	};

	DX12CommandListExecutor(
		ID3D12Device *device,
		ID3D12GraphicsCommandList *commandList,
		const DX12IaHashState &iaState,
		uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
		uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance,
		DX12ModIaReplacement *replacement,
		std::vector<DX12PendingResourceViewRequest> *pendingResourceViews = nullptr)
		: mDevice(device),
		  mCommandList(commandList),
		  mIaState(iaState),
		  mVertexCount(vertexCount),
		  mIndexCount(indexCount),
		  mInstanceCount(instanceCount),
		  mFirstVertex(firstVertex),
		  mFirstIndex(firstIndex),
		  mFirstInstance(firstInstance),
		  mReplacement(replacement),
		  mPendingResourceViews(pendingResourceViews),
		  mScratch(AcquireScratch(&mScratchPooled, &mFallbackScratch)),
		  mCommandListStack(mScratch->commandListStack),
		  mCommandListStackSet(mScratch->commandListStackSet),
		  mDescriptorTargetScratch(mScratch->descriptorTargetScratch),
		  mTargetMatchScratch(mScratch->targetMatchScratch),
		  mGraphicsSrvBindings(mScratch->graphicsSrvBindings),
		  mResolvedGraphicsSrvBindings(mScratch->resolvedGraphicsSrvBindings),
		  mGraphicsSrvBindingsByTarget(mScratch->graphicsSrvBindingsByTarget),
		  mGraphicsSrvTextureOverrideCache(mScratch->graphicsSrvTextureOverrideCache),
		  mShaderResourceOverrideBindingsByTarget(mScratch->shaderResourceOverrideBindingsByTarget),
		  mTargetTextureOverrideCache(mScratch->targetTextureOverrideCache)
	{
		mCurrentIaState = iaState;
		mCommandListStack.reserve(8);
		mCommandListStackSet.reserve(8);
		mDescriptorTargetScratch.reserve(8);
		mTargetMatchScratch.reserve(8);
		mGraphicsSrvBindings.reserve(32);
		mResolvedGraphicsSrvBindings.reserve(32);
		mGraphicsSrvBindingsByTarget.reserve(16);
		mGraphicsSrvTextureOverrideCache.reserve(16);
		mShaderResourceOverrideBindingsByTarget.reserve(16);
		mTargetTextureOverrideCache.reserve(16);
	}

	~DX12CommandListExecutor()
	{
		mScratch->Reset();
		if (mScratchPooled)
			--ScratchDepth;
	}

	void SetExecutionMode(bool executeCommands, bool executeDrawActions)
	{
		mExecuteCommands = executeCommands;
		mExecuteDrawActions = executeDrawActions;
	}

	void SetSkipDirectDescriptorTargets(bool skip)
	{
		mSkipDirectDescriptorTargets = skip;
	}

	void AdoptDirectDescriptorPrefilterCache()
	{
		const UINT64 reloadGeneration = DX12ModGetReloadGenerationFast();
		if (!tlsDirectDescriptorPrefilterCache.MatchesInvocation(
				reloadGeneration,
				mCommandList,
				mVertexCount,
				mIndexCount,
				mInstanceCount,
				mFirstVertex,
				mFirstIndex,
				mFirstInstance)) {
			return;
		}
		for (const auto &entry : tlsDirectDescriptorPrefilterCache.matchesByTarget)
			mGraphicsSrvTextureOverrideCache[entry.first] = entry.second;
	}

	bool observedDescriptorTarget = false;
	bool observedDynamicCommandState = false;

	bool Changed() const
	{
		return mChanged;
	}

	void ClearChanged()
	{
		mChanged = false;
	}

	void MarkExternalChange()
	{
		MarkChanged();
	}

	void RunTextureOverride(
		const Bunny::TextureOverrideConfig &config, bool includePost)
	{
		DX12TextureOverrideMatch match;
		match.config = &config;
		RunTextureOverride(match, 0, includePost);
	}

	void RunCommandListLinks(
		const Bunny::CommandListLinks &links, bool includePost)
	{
		RunCommandListLinks(links, 0, includePost, nullptr);
	}

	void RunShaderOverride(const Bunny::ShaderOverrideConfig &config)
	{
		if (!mReplacement)
			return;
		BoolScope directDescriptorSkipScope(
			&mSkipDirectDescriptorTargetsActive,
			mSkipDirectDescriptorTargets);
		for (const std::wstring &list : config.commandLists.post)
			mReplacement->AddPostCommandList(list);
		const std::shared_ptr<const DX12CompiledShaderOverridePlan> compiledPlan =
			GetCompiledShaderOverridePlan(config.section);
		if (compiledPlan && !compiledPlan->unsupported) {
			RunCompiledShaderOverridePlan(*compiledPlan);
			return;
		}
		if (config.handlingSkip)
			SetSkip();
		RunCommandListLinks(config.commandLists, 0, false, nullptr);
		RunActions(config.actions, 0, false, nullptr);
	}

	void QueuePostTextureOverride(
		const DX12TextureOverrideMatch &match)
	{
		if (!mReplacement || !match.config || !match.postRuntimeEffect)
			return;
		DX12ModIaReplacement::QueuedTextureOverridePost queued;
		queued.config = match.config;
		queued.hasThisSrvTarget = match.hasThisSrvTarget;
		queued.compute = match.compute;
		queued.thisSrvBinding = match.thisSrvBinding;
		queued.matchedHash = match.matchedHash;
		queued.resourceHash = match.resourceHash;
		queued.descriptorHash = match.descriptorHash;
		mReplacement->AddQueuedPostTextureOverride(queued);
	}

	void RunQueuedPostTextureOverride(
		const DX12ModIaReplacement::QueuedTextureOverridePost &queued)
	{
		if (!queued.config)
			return;

		const std::shared_ptr<const DX12CompiledTextureOverridePlan> compiledPlan =
			GetCompiledTextureOverridePlan(*queued.config);
		const DX12CompiledTextureOverridePlan *compiledPlanView =
			compiledPlan.get();
		const bool compiledTextureOverride =
			compiledPlanView && !compiledPlanView->hasUnsupportedCommandActions;
		if (compiledTextureOverride) {
			if (compiledPlanView->hasPostCommandListPlan)
				ExecuteCompiledCommandListPlan(
					compiledPlanView->postCommandListPlan, 0, true);
			return;
		}

		if (queued.config->commandLists.post.empty())
			return;

		DX12TextureOverrideMatch match;
		match.config = queued.config;
		match.preRuntimeEffect = true;
		match.postRuntimeEffect = true;
		match.hasThisSrvTarget = queued.hasThisSrvTarget;
		match.compute = queued.compute;
		match.thisSrvBinding = queued.thisSrvBinding;
		match.matchedHash = queued.matchedHash;
		match.resourceHash = queued.resourceHash;
		match.descriptorHash = queued.descriptorHash;
		Bunny::CommandListLinks postLinks;
		postLinks.main = queued.config->commandLists.post;
		RunCommandListLinks(postLinks, 0, true, &match);
	}

	void RunPostShaderOverrideLists(
		const Bunny::ShaderOverrideConfig &config)
	{
		const std::shared_ptr<const DX12CompiledShaderOverridePlan> compiledPlan =
			GetCompiledShaderOverridePlan(config.section);
		if (compiledPlan && !compiledPlan->unsupported) {
			RunCompiledShaderOverridePostPlan(*compiledPlan);
			return;
		}
		Bunny::CommandListLinks postLinks;
		postLinks.main = config.commandLists.post;
		RunCommandListLinks(postLinks, 0, true, nullptr);
	}

public:
	void MarkChanged()
	{
		mChanged = true;
	}

	void SetSkip()
	{
		if (mReplacement)
			mReplacement->skip = true;
	}

	void AppendDraw(const Bunny::CommandListAction &action)
	{
		if (!mReplacement)
			return;
		DX12ModIaReplacement::DrawCall draw;
		draw.indexed =
			action.kind == Bunny::CommandListActionKind::DrawIndexed ||
			action.kind == Bunny::CommandListActionKind::DrawIndexedAuto ||
			action.kind == Bunny::CommandListActionKind::DrawIndexedFromCaller;
		draw.fromCaller =
			action.kind == Bunny::CommandListActionKind::DrawFromCaller ||
			action.kind == Bunny::CommandListActionKind::DrawIndexedFromCaller;
		draw.autoCount = action.kind == Bunny::CommandListActionKind::DrawIndexedAuto;
		if (!draw.fromCaller && !draw.autoCount) {
			draw.count = action.args[0];
			draw.start = action.args[1];
			draw.baseVertex = draw.indexed ? static_cast<INT>(action.args[2]) : 0;
		}
		mReplacement->draws.push_back(draw);
		MarkChanged();
	}

	void AppendDispatch(const Bunny::CommandListAction &action)
	{
		if (!mReplacement)
			return;
		DX12ModIaReplacement::DispatchCall dispatch;
		dispatch.groupsX = action.args[0];
		dispatch.groupsY = action.args[1];
		dispatch.groupsZ = action.args[2];
		mReplacement->dispatches.push_back(dispatch);
		MarkChanged();
	}

	void AppendResourceViews(
		const std::wstring &indexResource,
		const std::vector<DX12CompiledVertexResourceBinding> &vertexResources)
	{
		if (!indexResource.empty() || !vertexResources.empty())
			InvalidateTargetCaches();
		ApplyIaStateResourceOverrides(indexResource, vertexResources);
		if (mPendingResourceViews) {
			DX12PendingResourceViewRequest request;
			request.indexResource = indexResource;
			request.vertexResources = vertexResources;
			mPendingResourceViews->push_back(std::move(request));
			return;
		}
		mChanged |= AppendResourceViewsForCommandList(
			mDevice, mCommandList, mCurrentIaState, indexResource, vertexResources, mReplacement);
	}

	void ApplyIaStateResourceOverrides(
		const std::wstring &indexResource,
		const std::vector<DX12CompiledVertexResourceBinding> &vertexResources)
	{
		if (!mDevice)
			return;
		if (!indexResource.empty()) {
			DX12LoadedResource *resource =
				EnsureLoadedResourceForPreSkin(mDevice, indexResource);
			if (resource && resource->resource) {
				const DXGI_FORMAT indexFormat =
					resource->format == DXGI_FORMAT_R16_UINT ||
					resource->format == DXGI_FORMAT_R32_UINT ?
						resource->format :
						(mCurrentIaState.hasIndexBuffer &&
						 mCurrentIaState.indexView.Format != DXGI_FORMAT_UNKNOWN ?
							mCurrentIaState.indexView.Format :
							DXGI_FORMAT_R32_UINT);
				mCurrentIaState.hasIndexBuffer = true;
				mCurrentIaState.indexView.BufferLocation =
					resource->resource->GetGPUVirtualAddress();
				mCurrentIaState.indexView.SizeInBytes = static_cast<UINT>(
					(std::min)(resource->byteWidth, static_cast<UINT64>(UINT_MAX)));
				mCurrentIaState.indexView.Format = indexFormat;
				mCurrentIaState.indexHash = DX12HashIaBufferView(
					mCurrentIaState.indexView.BufferLocation,
					mCurrentIaState.indexView.SizeInBytes,
					0,
					static_cast<UINT>(indexFormat),
					0);
			}
		}

		for (const DX12CompiledVertexResourceBinding &item : vertexResources) {
			DX12LoadedResource *resource =
				EnsureLoadedResourceForPreSkin(mDevice, item.resource);
			if (!resource || !resource->resource || item.slot >= 32)
				continue;
			const UINT stride =
				resource->stride ? resource->stride :
				FindIaVertexSlot(mCurrentIaState, item.slot) ?
					FindIaVertexSlot(mCurrentIaState, item.slot)->vertexView.StrideInBytes :
					0;
			DX12IaBufferHash *existing = nullptr;
			for (DX12IaBufferHash &buffer : mCurrentIaState.vertexBuffers) {
				if (buffer.slot == item.slot) {
					existing = &buffer;
					break;
				}
			}
			DX12IaBufferHash created = {};
			if (!existing) {
				created.slot = item.slot;
				mCurrentIaState.vertexBuffers.push_back(created);
				existing = &mCurrentIaState.vertexBuffers.back();
			}
			existing->slot = item.slot;
			existing->vertexView.BufferLocation =
				resource->resource->GetGPUVirtualAddress();
			existing->vertexView.SizeInBytes = static_cast<UINT>(
				(std::min)(resource->byteWidth, static_cast<UINT64>(UINT_MAX)));
			existing->vertexView.StrideInBytes = stride;
			existing->hash = DX12HashIaBufferView(
				existing->vertexView.BufferLocation,
				existing->vertexView.SizeInBytes,
				existing->vertexView.StrideInBytes,
				0,
				item.slot);
		}
	}

	bool ApplySetDescriptorResourceAction(
		const Bunny::CommandListTarget &target,
		const std::wstring &resourceName)
	{
		if ((target.kind != Bunny::CommandListTargetKind::ShaderResource &&
		     target.kind != Bunny::CommandListTargetKind::ConstantBuffer &&
		     target.kind != Bunny::CommandListTargetKind::UnorderedAccessView) ||
		    resourceName.empty()) {
			return false;
		}
		const DX12TextureOverrideTargetCacheKey cacheKey = {
			static_cast<uint32_t>(target.kind),
			static_cast<uint32_t>(target.stage),
			target.slot
		};
		const std::vector<DX12ResolvedGraphicsSrvBinding> *baseBindings =
			target.stage == Bunny::CommandListShaderStage::Compute ?
				nullptr :
				GetResolvedGraphicsSrvBindingsForTarget(target);
		std::vector<DX12ResolvedGraphicsSrvBinding> resolvedBindings;
		if (!BuildResolvedDescriptorBindingsForResourceOverride(
				mDevice,
				mCommandList,
				target,
				resourceName,
				baseBindings,
				&resolvedBindings)) {
			return false;
		}
		mShaderResourceOverrideBindingsByTarget[cacheKey] =
			std::move(resolvedBindings);
		InvalidateGraphicsSrvBindingCaches();
		InvalidateTargetCaches();
		return true;
	}

	bool FindTargetTextureOverride(
		const Bunny::CommandListTarget &target,
		std::vector<DX12TextureOverrideMatch> *matches)
	{
		const DX12TextureOverrideTargetCacheKey cacheKey = {
			static_cast<uint32_t>(target.kind),
			static_cast<uint32_t>(target.stage),
			target.slot
		};
		auto cached = mTargetTextureOverrideCache.find(cacheKey);
		if (cached != mTargetTextureOverrideCache.end()) {
			if (matches)
				*matches = cached->second;
			return !cached->second.empty();
		}

		const bool descriptorTarget =
			target.kind == Bunny::CommandListTargetKind::ConstantBuffer ||
			target.kind == Bunny::CommandListTargetKind::ShaderResource ||
			target.kind == Bunny::CommandListTargetKind::UnorderedAccessView;
		if (descriptorTarget) {
			auto overridden = mShaderResourceOverrideBindingsByTarget.find(cacheKey);
			auto descriptorCached = mGraphicsSrvTextureOverrideCache.find(cacheKey);
			if (descriptorCached != mGraphicsSrvTextureOverrideCache.end()) {
				mTargetTextureOverrideCache.emplace(cacheKey, descriptorCached->second);
				if (matches)
					*matches = descriptorCached->second;
				return !descriptorCached->second.empty();
			}
			if (!mGraphicsSrvTextureOverrideCacheResolved)
				BuildDescriptorTextureOverrideCache();
			descriptorCached = mGraphicsSrvTextureOverrideCache.find(cacheKey);
			if (descriptorCached != mGraphicsSrvTextureOverrideCache.end()) {
				mTargetTextureOverrideCache.emplace(cacheKey, descriptorCached->second);
				if (matches)
					*matches = descriptorCached->second;
				return !descriptorCached->second.empty();
			}
			if (overridden != mShaderResourceOverrideBindingsByTarget.end()) {
				mTargetTextureOverrideCache.emplace(
					cacheKey,
					std::vector<DX12TextureOverrideMatch>());
				return false;
			}
			if (mSkipDirectDescriptorTargetsActive)
				return false;
			observedDescriptorTarget = true;
			mTargetTextureOverrideCache.emplace(
				cacheKey,
				std::vector<DX12TextureOverrideMatch>());
			return false;
		}
		const bool found = ::FindTargetTextureOverride(
			mCommandList, mCurrentIaState, target, mVertexCount, mIndexCount, mInstanceCount,
			mFirstVertex, mFirstIndex, mFirstInstance, matches,
			nullptr);
		mTargetTextureOverrideCache.emplace(
			cacheKey,
			matches ? *matches : std::vector<DX12TextureOverrideMatch>());
		return found;
	}

	void RunTextureOverride(
		const DX12TextureOverrideMatch &match, int depth, bool includePost)
	{
		const Bunny::TextureOverrideConfig *config = match.config;
		if (!mReplacement || depth > 16 || !config)
			return;
		BoolScope directDescriptorSkipScope(
			&mSkipDirectDescriptorTargetsActive,
			false);

		const std::shared_ptr<const DX12CompiledTextureOverridePlan> compiledPlan =
			GetCompiledTextureOverridePlan(*config);
		const DX12CompiledTextureOverridePlan *compiledPlanView =
			compiledPlan.get();
		const bool compiledTextureOverride =
			compiledPlanView && !compiledPlanView->hasUnsupportedCommandActions;
		if (includePost) {
			if (!match.postRuntimeEffect)
				return;
			if (compiledTextureOverride) {
				if (compiledPlanView->hasPostCommandListPlan) {
					ExecuteCompiledCommandListPlan(
						compiledPlanView->postCommandListPlan, depth + 1, true);
				}
			} else {
				for (const std::wstring &list : config->commandLists.post) {
					Bunny::CommandListLinks links;
					links.main.push_back(list);
					RunCommandListLinks(links, depth + 1, true, &match);
				}
			}
			return;
		}

		QueuePostTextureOverride(match);
		if (!match.preRuntimeEffect)
			return;

		if (compiledTextureOverride) {
			if (compiledPlanView->hasPreCommandListPlan)
				ExecuteCompiledCommandListPlan(
					compiledPlanView->preCommandListPlan, depth + 1, false);
			RunCompiledTextureOverridePlan(*compiledPlanView, depth, includePost);
		} else {
			if (mExecuteCommands) {
				for (const std::wstring &list : config->commandLists.pre) {
					Bunny::CommandListLinks links;
					links.main.push_back(list);
					RunCommandListLinks(links, depth + 1, false, &match);
				}
			}
			RunTextureOverrideActions(*config, depth, includePost, &match);
			if (mExecuteCommands) {
				for (const std::wstring &list : config->commandLists.main) {
					Bunny::CommandListLinks links;
					links.main.push_back(list);
					RunCommandListLinks(links, depth + 1, false, &match);
				}

			}
		}

		const std::vector<DX12CurrentShaderResourceBinding> *graphicsSrvBindings =
			GetGraphicsSrvBindings();
		const bool drawSrvChanged = compiledPlanView ?
			DX12DrawSrvReplacementUtils::ApplyCompiledPlan(
				mDevice, mCommandList, *compiledPlanView, &match, mReplacement,
				graphicsSrvBindings) :
			DX12DrawSrvReplacementUtils::ApplyConfig(
				mDevice, mCommandList, *config, &match, mReplacement,
				graphicsSrvBindings);
		if (drawSrvChanged)
			MarkChanged();
	}

	bool RunCompiledTextureOverridePlan(
		const DX12CompiledTextureOverridePlan &plan, int depth, bool includePost)
	{
		if (plan.hasMainActionPlan) {
			ExecuteCompiledCommandListPlan(plan.mainActionPlan, depth, includePost);
			return true;
		}
		AppendResourceViews(plan.indexResource, plan.vertexResources);
		const bool executeActions = mExecuteCommands || mExecuteDrawActions;
		if (executeActions && plan.handlingSkip)
			SetSkip();
		if (executeActions) {
			for (const DX12ModIaReplacement::DrawCall &draw : plan.draws) {
				mReplacement->draws.push_back(draw);
				MarkChanged();
			}
			for (const DX12ModIaReplacement::DispatchCall &dispatch : plan.dispatches) {
				mReplacement->dispatches.push_back(dispatch);
				MarkChanged();
			}
		}
		return true;
	}

	void RunCompiledShaderOverridePlan(const DX12CompiledShaderOverridePlan &plan)
	{
		if (plan.unsupported)
			return;
		BoolScope directDescriptorSkipScope(
			&mSkipDirectDescriptorTargetsActive,
			mSkipDirectDescriptorTargets);
		if (mReplacement) {
			for (const std::wstring &list : plan.postCommandLists)
				mReplacement->AddPostCommandList(list);
		}
		ExecuteCompiledCommandListPlan(plan.mainPlan, 0, false);
	}

	void CollectDescriptorCheckTargetsFromCompiledPlans(
		const std::vector<DX12CompiledShaderOverridePlan> &plans)
	{
		mDescriptorTargetScratch.clear();
		for (const DX12CompiledShaderOverridePlan &plan : plans) {
			if (!plan.mainPlan.hasDescriptorCheckTargets)
				continue;
			for (const Bunny::CommandListTarget &target :
				 plan.mainPlan.descriptorCheckTargets) {
				bool exists = false;
				for (const Bunny::CommandListTarget &existing :
					 mDescriptorTargetScratch) {
					if (existing.kind == target.kind &&
						existing.stage == target.stage &&
						existing.slot == target.slot) {
						exists = true;
						break;
					}
				}
				if (!exists)
					mDescriptorTargetScratch.push_back(target);
			}
		}
	}

	void RunCompiledShaderOverridePostPlan(const DX12CompiledShaderOverridePlan &plan)
	{
		if (plan.unsupported || !plan.hasPostCommandListPlan)
			return;
		ExecuteCompiledCommandListPlan(plan.postCommandListPlan, 0, true);
	}

	void BuildDescriptorTextureOverrideCacheForTargets(
		const std::vector<Bunny::CommandListTarget> *targets)
	{
		mGraphicsSrvTextureOverrideCacheResolved = true;
		mDescriptorTextureOverrideCachePrebuilt = true;
		mGraphicsSrvTextureOverrideCache.clear();
		if (!mCommandList)
			return;

		const std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> &snapshot =
			GetTextureOverrideRuntimeSnapshot();
		if (!snapshot)
			return;
		if (!HasPotentialResolvedDescriptorPrefilterCandidates(
				*snapshot,
				mVertexCount,
				mIndexCount))
			return;

		static thread_local std::vector<uint32_t> tlsAllowedSectionIds;
		CollectResolvedDescriptorPrefilterSectionIds(
			*snapshot,
			mVertexCount,
			mIndexCount,
			&tlsAllowedSectionIds);
		if (tlsAllowedSectionIds.empty())
			return;

		auto appendTarget = [this, &snapshot](const Bunny::CommandListTarget &target) {
			const DX12TextureOverrideTargetCacheKey cacheKey = {
				static_cast<uint32_t>(target.kind),
				static_cast<uint32_t>(target.stage),
				target.slot
			};
			auto overridden = mShaderResourceOverrideBindingsByTarget.find(cacheKey);
			const std::vector<DX12ResolvedGraphicsSrvBinding> *resolvedBindingsView = nullptr;
			std::vector<DX12ResolvedGraphicsSrvBinding> resolvedBindings;
			if (overridden != mShaderResourceOverrideBindingsByTarget.end()) {
				resolvedBindingsView = &overridden->second;
			} else if (target.kind == Bunny::CommandListTargetKind::ShaderResource &&
				(target.stage == Bunny::CommandListShaderStage::Vertex ||
				 target.stage == Bunny::CommandListShaderStage::Pixel)) {
				resolvedBindingsView = GetResolvedGraphicsSrvBindingsForTarget(target);
			} else {
				std::vector<DX12CurrentShaderResourceBinding> bindings;
				if (!DX12BindingTracker::GetCurrentShaderResourceBindingsByRegister(
						mCommandList,
						target.stage == Bunny::CommandListShaderStage::Compute,
						DescriptorRangeTypeForTarget(target.kind),
						target.slot,
						0,
						&bindings) || bindings.empty()) {
					return;
				}
				ResolveGraphicsSrvBindings(
					mCommandList,
					mDevice,
					bindings,
					&resolvedBindings);
				resolvedBindingsView = &resolvedBindings;
			}
			if (!resolvedBindingsView || resolvedBindingsView->empty())
				return;

			std::vector<DX12TextureOverrideMatch> resolvedMatches;
			FindDescriptorTextureOverridesFromResolvedBindings(
				snapshot,
				target,
				*resolvedBindingsView,
				mVertexCount,
				mIndexCount,
				mInstanceCount,
				mFirstVertex,
				mFirstIndex,
				mFirstInstance,
				&resolvedMatches);
			if (!resolvedMatches.empty())
				mGraphicsSrvTextureOverrideCache[cacheKey] = std::move(resolvedMatches);
		};

		if (targets) {
			if (targets->empty())
				return;
			for (const Bunny::CommandListTarget &target : *targets)
				appendTarget(target);
			return;
		}

		BuildDescriptorTextureOverrideCache();
	}

	void PrimeDescriptorTextureOverrideCacheForTargets(
		const std::vector<Bunny::CommandListTarget> *targets)
	{
		if (mDescriptorTextureOverrideCachePrebuilt)
			return;
		BuildDescriptorTextureOverrideCacheForTargets(targets);
	}

	void CollectDescriptorCheckTargetsFromActions(
		const std::vector<Bunny::CommandListAction> &actions)
	{
		mDescriptorTargetScratch.clear();
		for (const Bunny::CommandListAction &action : actions) {
			if (action.kind != Bunny::CommandListActionKind::CheckTextureOverride)
				continue;
			if (action.target.kind != Bunny::CommandListTargetKind::ConstantBuffer &&
				action.target.kind != Bunny::CommandListTargetKind::ShaderResource &&
				action.target.kind != Bunny::CommandListTargetKind::UnorderedAccessView)
				continue;
			bool exists = false;
			for (const Bunny::CommandListTarget &existing : mDescriptorTargetScratch) {
				if (existing.kind == action.target.kind &&
					existing.stage == action.target.stage &&
					existing.slot == action.target.slot) {
					exists = true;
					break;
				}
			}
			if (!exists)
				mDescriptorTargetScratch.push_back(action.target);
		}
	}

	void RunCheckTextureOverrideTarget(
		const Bunny::CommandListTarget &target,
		int depth,
		bool includePost)
	{
		if (!HasTextureOverrideRuntimeEffect(includePost))
			return;
		mTargetMatchScratch.clear();
		if (!FindTargetTextureOverride(target, &mTargetMatchScratch))
			return;
		for (const DX12TextureOverrideMatch &match : mTargetMatchScratch)
			RunTextureOverride(match, depth + 1, includePost);
	}

	bool RunCompiledCommandListPlan(
		const std::wstring &name, int depth, bool includePost)
	{
		if (!mReplacement)
			return false;
		if (!HasCommandListRuntimeEffect(name, includePost))
			return false;
		if (!EnterCommandList(name))
			return true;
		const std::shared_ptr<const DX12CompiledCommandListPlan> plan =
			GetCompiledCommandListPlan(name);
		if (!plan) {
			LeaveCommandList();
			return false;
		}
		ExecuteCompiledCommandListPlan(*plan, depth, includePost);
		LeaveCommandList();
		return true;
	}

	void ExecuteCompiledCommandListPlan(
		const DX12CompiledCommandListPlan &plan, int depth, bool includePost)
	{
		const bool executeActions = mReplacement && (mExecuteCommands || mExecuteDrawActions);
		if (plan.hasDescriptorCheckTargets && !plan.descriptorCheckTargets.empty())
			PrimeDescriptorTextureOverrideCacheForTargets(&plan.descriptorCheckTargets);
		if (!plan.ops.empty()) {
			for (const DX12CompiledCommandListPlan::Op &op : plan.ops) {
				switch (op.kind) {
				case DX12CompiledCommandListPlan::Op::Kind::HandlingSkip:
					if (executeActions)
						SetSkip();
					break;
				case DX12CompiledCommandListPlan::Op::Kind::SetIndexBuffer:
					AppendResourceViews(
						op.resource,
						std::vector<DX12CompiledVertexResourceBinding>());
					break;
				case DX12CompiledCommandListPlan::Op::Kind::SetVertexBuffer:
					AppendResourceViews(
						L"",
						std::vector<DX12CompiledVertexResourceBinding>{
							{ op.target.slot, op.resource }});
					break;
				case DX12CompiledCommandListPlan::Op::Kind::SetShaderResource:
					ApplySetDescriptorResourceAction(op.target, op.resource);
					break;
				case DX12CompiledCommandListPlan::Op::Kind::SetConstantBuffer:
					ApplySetDescriptorResourceAction(op.target, op.resource);
					break;
				case DX12CompiledCommandListPlan::Op::Kind::SetUnorderedAccessView:
					ApplySetDescriptorResourceAction(op.target, op.resource);
					break;
				case DX12CompiledCommandListPlan::Op::Kind::Draw:
					if (executeActions) {
						mReplacement->draws.push_back(op.draw);
						MarkChanged();
					}
					break;
				case DX12CompiledCommandListPlan::Op::Kind::Dispatch:
					if (executeActions) {
						mReplacement->dispatches.push_back(op.dispatch);
						MarkChanged();
					}
					break;
				case DX12CompiledCommandListPlan::Op::Kind::CheckTextureOverride:
					if (!mExecuteCommands)
						break;
					RunCheckTextureOverrideTarget(op.target, depth, includePost);
					break;
				}
			}
			return;
		}
		AppendResourceViews(plan.indexResource, plan.vertexResources);
		if (plan.handlingSkip)
			SetSkip();
		for (const DX12ModIaReplacement::DrawCall &draw : plan.draws) {
			mReplacement->draws.push_back(draw);
			MarkChanged();
		}
		for (const DX12ModIaReplacement::DispatchCall &dispatch : plan.dispatches) {
			mReplacement->dispatches.push_back(dispatch);
			MarkChanged();
		}
	}

	const std::vector<DX12CurrentShaderResourceBinding> *GetGraphicsSrvBindings()
	{
		if (!mCommandList)
			return nullptr;
		if (!mGraphicsSrvBindingsResolved) {
			mGraphicsSrvBindingsResolved = true;
			// DX11 draw-time matching consumes the current bound resources, not a
			// stripped patch layout. TextureOverride matching and draw SRV patching
			// therefore share the same fully resolved current SRV snapshot.
			DX12BindingTracker::GetCurrentShaderResourceBindings(
				mCommandList,
				false,
				D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
				&mGraphicsSrvBindings);
			for (const auto &overrideEntry : mShaderResourceOverrideBindingsByTarget) {
				const DX12TextureOverrideTargetCacheKey &key = overrideEntry.first;
				if (key.kind != static_cast<uint32_t>(Bunny::CommandListTargetKind::ShaderResource) ||
				    key.stage == static_cast<uint32_t>(Bunny::CommandListShaderStage::Compute)) {
					continue;
				}
				Bunny::CommandListTarget target;
				target.kind = Bunny::CommandListTargetKind::ShaderResource;
				target.stage = static_cast<Bunny::CommandListShaderStage>(key.stage);
				target.slot = key.slot;
				mGraphicsSrvBindings.erase(
					std::remove_if(
						mGraphicsSrvBindings.begin(),
						mGraphicsSrvBindings.end(),
						[&target](const DX12CurrentShaderResourceBinding &binding) {
							return DescriptorBindingMatchesTarget(binding, target);
						}),
					mGraphicsSrvBindings.end());
				for (const DX12ResolvedGraphicsSrvBinding &resolved : overrideEntry.second)
					mGraphicsSrvBindings.push_back(resolved.binding);
			}
		}
		return &mGraphicsSrvBindings;
	}

	const std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> &
	GetTextureOverrideRuntimeSnapshot()
	{
		if (!mTextureOverrideRuntimeSnapshotResolved) {
			mTextureOverrideRuntimeSnapshotResolved = true;
			mTextureOverrideRuntimeSnapshot = LoadTextureOverrideRuntimeSnapshot();
		}
		return mTextureOverrideRuntimeSnapshot;
	}

	std::shared_ptr<const DX12CompiledTextureOverridePlan>
	GetCompiledTextureOverridePlan(const Bunny::TextureOverrideConfig &config)
	{
		const std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> &snapshot =
			GetTextureOverrideRuntimeSnapshot();
		if (!snapshot)
			return nullptr;
		auto found = snapshot->compiledTextureOverridePlans.find(config.section);
		return found == snapshot->compiledTextureOverridePlans.end() ?
			nullptr : found->second;
	}

	std::shared_ptr<const DX12CompiledShaderOverridePlan>
	GetCompiledShaderOverridePlan(const std::wstring &section)
	{
		const std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> &snapshot =
			GetTextureOverrideRuntimeSnapshot();
		if (!snapshot)
			return nullptr;
		auto found = snapshot->compiledShaderOverridePlans.find(section);
		return found == snapshot->compiledShaderOverridePlans.end() ?
			nullptr : found->second;
	}

	std::shared_ptr<const DX12CompiledCommandListPlan>
	GetCompiledCommandListPlan(const std::wstring &name)
	{
		const std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> &snapshot =
			GetTextureOverrideRuntimeSnapshot();
		if (!snapshot)
			return nullptr;
		auto found = snapshot->compiledCommandListPlans.find(name);
		return found == snapshot->compiledCommandListPlans.end() ?
			nullptr : found->second;
	}

	bool HasTextureOverrideRuntimeEffect(bool includePost)
	{
		const std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> &snapshot =
			GetTextureOverrideRuntimeSnapshot();
		return !snapshot || (includePost ?
			snapshot->anyTextureOverridePostRuntimeEffect :
			snapshot->anyTextureOverridePreRuntimeEffect);
	}

	bool HasCommandListRuntimeEffect(const std::wstring &name, bool includePost)
	{
		const std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> &snapshot =
			GetTextureOverrideRuntimeSnapshot();
		return !snapshot || CommandListRuntimeEffectLocked(snapshot, name, includePost);
	}

	const std::vector<DX12ResolvedGraphicsSrvBinding> *GetResolvedGraphicsSrvBindings()
	{
		if (!mResolvedGraphicsSrvBindingsResolved) {
			mResolvedGraphicsSrvBindingsResolved = true;
			DX12SimulateContext *runtimeState =
				mCommandList ?
					DX12CommandListRuntime::GetStatePtrFast(mCommandList) :
					nullptr;
			const UINT64 descriptorQuerySerial =
				(runtimeState && runtimeState->graphicsDescriptorQuerySerial) ?
					runtimeState->graphicsDescriptorQuerySerial : 1;
			if (!TryLoadResolvedGraphicsSrvBindings(
					runtimeState,
					descriptorQuerySerial,
					&mResolvedGraphicsSrvBindings)) {
				const std::vector<DX12CurrentShaderResourceBinding> *bindings =
					GetGraphicsSrvBindings();
				if (bindings) {
					ResolveGraphicsSrvBindings(
						mCommandList,
						mDevice,
						*bindings,
						&mResolvedGraphicsSrvBindings);
					StoreResolvedGraphicsSrvBindings(
						runtimeState,
						descriptorQuerySerial,
						mResolvedGraphicsSrvBindings);
				}
			}
		}
		return &mResolvedGraphicsSrvBindings;
	}

	const std::vector<DX12ResolvedGraphicsSrvBinding> *
	GetResolvedGraphicsSrvBindingsForTarget(const Bunny::CommandListTarget &target)
	{
		if (target.kind != Bunny::CommandListTargetKind::ShaderResource ||
		    (target.stage != Bunny::CommandListShaderStage::Vertex &&
		     target.stage != Bunny::CommandListShaderStage::Pixel)) {
			return nullptr;
		}
		if (!mGraphicsSrvBindingsByTargetResolved) {
			mGraphicsSrvBindingsByTargetResolved = true;
			const std::vector<DX12ResolvedGraphicsSrvBinding> *resolvedBindings =
				GetResolvedGraphicsSrvBindings();
			if (resolvedBindings) {
				for (const DX12ResolvedGraphicsSrvBinding &resolved :
				     *resolvedBindings) {
					if (resolved.binding.registerSpace != 0)
						continue;
					if (resolved.binding.shaderVisibility ==
					    D3D12_SHADER_VISIBILITY_VERTEX ||
					    resolved.binding.shaderVisibility ==
					        D3D12_SHADER_VISIBILITY_ALL) {
						mGraphicsSrvBindingsByTarget[DX12TextureOverrideTargetCacheKey{
							static_cast<uint32_t>(
								Bunny::CommandListTargetKind::ShaderResource),
							static_cast<uint32_t>(
								Bunny::CommandListShaderStage::Vertex),
							resolved.binding.shaderRegister
						}].push_back(resolved);
					}
					if (resolved.binding.shaderVisibility ==
					    D3D12_SHADER_VISIBILITY_PIXEL ||
					    resolved.binding.shaderVisibility ==
					        D3D12_SHADER_VISIBILITY_ALL) {
						mGraphicsSrvBindingsByTarget[DX12TextureOverrideTargetCacheKey{
							static_cast<uint32_t>(
								Bunny::CommandListTargetKind::ShaderResource),
							static_cast<uint32_t>(
								Bunny::CommandListShaderStage::Pixel),
							resolved.binding.shaderRegister
						}].push_back(resolved);
					}
				}
			}
		}
		const DX12TextureOverrideTargetCacheKey cacheKey = {
			static_cast<uint32_t>(target.kind),
			static_cast<uint32_t>(target.stage),
			target.slot
		};
		auto found = mGraphicsSrvBindingsByTarget.find(cacheKey);
		return found == mGraphicsSrvBindingsByTarget.end() ?
			nullptr : &found->second;
	}

	void BuildDescriptorTextureOverrideCache()
	{
		mGraphicsSrvTextureOverrideCacheResolved = true;
		mGraphicsSrvTextureOverrideCache.clear();
		if (!mCommandList)
			return;

		const std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> &snapshot =
			GetTextureOverrideRuntimeSnapshot();
		if (!snapshot)
			return;
		if (!HasPotentialResolvedDescriptorPrefilterCandidates(
				*snapshot,
				mVertexCount,
				mIndexCount)) {
			return;
		}

		static thread_local std::vector<uint32_t> tlsAllowedSectionIds;
		CollectResolvedDescriptorPrefilterSectionIds(
			*snapshot,
			mVertexCount,
			mIndexCount,
			&tlsAllowedSectionIds);
		if (tlsAllowedSectionIds.empty())
			return;

		auto appendCurrentBindings = [this, &snapshot](
			D3D12_DESCRIPTOR_RANGE_TYPE rangeType,
			Bunny::CommandListTargetKind kind,
			bool compute) {
			if (kind == Bunny::CommandListTargetKind::ShaderResource && !compute) {
				const std::vector<DX12ResolvedGraphicsSrvBinding> *resolvedBindings =
					GetResolvedGraphicsSrvBindings();
				if (resolvedBindings && !resolvedBindings->empty()) {
					AppendResolvedBindingsToDescriptorTextureOverrideCache(
						snapshot,
						*resolvedBindings,
						kind,
						false,
						tlsAllowedSectionIds,
						&mGraphicsSrvTextureOverrideCache);
				}
				return;
			}

			std::vector<DX12CurrentShaderResourceBinding> bindings;
			if (!DX12BindingTracker::GetCurrentShaderResourceBindings(
					mCommandList,
					compute,
					rangeType,
					&bindings) || bindings.empty()) {
				return;
			}

			std::vector<DX12ResolvedGraphicsSrvBinding> resolvedBindings;
			ResolveGraphicsSrvBindings(
				mCommandList,
				mDevice,
				bindings,
				&resolvedBindings);
			if (resolvedBindings.empty())
				return;
			AppendResolvedBindingsToDescriptorTextureOverrideCache(
				snapshot,
				resolvedBindings,
				kind,
				compute,
				tlsAllowedSectionIds,
				&mGraphicsSrvTextureOverrideCache);
		};

		appendCurrentBindings(
			D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
			Bunny::CommandListTargetKind::ShaderResource,
			false);
		appendCurrentBindings(
			D3D12_DESCRIPTOR_RANGE_TYPE_CBV,
			Bunny::CommandListTargetKind::ConstantBuffer,
			false);
		appendCurrentBindings(
			D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
			Bunny::CommandListTargetKind::UnorderedAccessView,
			false);
		appendCurrentBindings(
			D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
			Bunny::CommandListTargetKind::ShaderResource,
			true);
		appendCurrentBindings(
			D3D12_DESCRIPTOR_RANGE_TYPE_CBV,
			Bunny::CommandListTargetKind::ConstantBuffer,
			true);
		appendCurrentBindings(
			D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
			Bunny::CommandListTargetKind::UnorderedAccessView,
			true);

		for (const auto &overrideEntry : mShaderResourceOverrideBindingsByTarget) {
			Bunny::CommandListTarget target;
			target.kind = static_cast<Bunny::CommandListTargetKind>(overrideEntry.first.kind);
			target.stage = static_cast<Bunny::CommandListShaderStage>(overrideEntry.first.stage);
			target.slot = overrideEntry.first.slot;
			std::vector<DX12TextureOverrideMatch> resolvedMatches;
			if (!overrideEntry.second.empty()) {
				FindDescriptorTextureOverridesFromResolvedBindings(
					snapshot,
					target,
					overrideEntry.second,
					mVertexCount,
					mIndexCount,
					mInstanceCount,
					mFirstVertex,
					mFirstIndex,
					mFirstInstance,
					&resolvedMatches);
			}
			if (resolvedMatches.empty()) {
				mGraphicsSrvTextureOverrideCache.erase(overrideEntry.first);
				continue;
			}
			mGraphicsSrvTextureOverrideCache[overrideEntry.first] =
				std::move(resolvedMatches);
		}
	}

	bool FindGraphicsSrvTextureOverrides(
		const Bunny::CommandListTarget &target,
		std::vector<DX12TextureOverrideMatch> *matches)
	{
		if (matches)
			matches->clear();
		const DX12TextureOverrideTargetCacheKey cacheKey = {
			static_cast<uint32_t>(target.kind),
			static_cast<uint32_t>(target.stage),
			target.slot
		};
		if (!mGraphicsSrvTextureOverrideCacheResolved) {
			BuildDescriptorTextureOverrideCache();
		}
		auto cached = mGraphicsSrvTextureOverrideCache.find(cacheKey);
		if (cached != mGraphicsSrvTextureOverrideCache.end()) {
			if (matches)
				*matches = cached->second;
			return !cached->second.empty();
		}
		return false;
	}

	void InvalidateTargetCaches()
	{
		mDescriptorTextureOverrideCachePrebuilt = false;
		mGraphicsSrvTextureOverrideCacheResolved = false;
		mGraphicsSrvTextureOverrideCache.clear();
		mTargetTextureOverrideCache.clear();
	}

	void InvalidateGraphicsSrvBindingCaches()
	{
		mGraphicsSrvBindingsResolved = false;
		mResolvedGraphicsSrvBindingsResolved = false;
		mGraphicsSrvBindingsByTargetResolved = false;
		mGraphicsSrvBindings.clear();
		mResolvedGraphicsSrvBindings.clear();
		mGraphicsSrvBindingsByTarget.clear();
	}

	void RunTextureOverrideActions(
		const Bunny::TextureOverrideConfig &config, int depth, bool includePost,
		const DX12TextureOverrideMatch *thisMatch)
	{
		RunActions(config.actions, depth, includePost, thisMatch);
	}

	void RunCommandList(
		const Bunny::CommandListConfig &commandList, int depth, bool includePost,
		const DX12TextureOverrideMatch *thisMatch)
	{
		if (depth > 16)
			return;
		if (RunCompiledCommandListPlan(commandList.section, depth, includePost))
			return;
		if (!EnterCommandList(commandList.section))
			return;
		CollectDescriptorCheckTargetsFromActions(commandList.actions);
		if (!mDescriptorTargetScratch.empty())
			PrimeDescriptorTextureOverrideCacheForTargets(&mDescriptorTargetScratch);
		CommandVmRunScope vmScope(&mVm, includePost);
		for (const Bunny::CommandListAction &action : commandList.actions) {
			if (ApplyVmAction(action, includePost))
				continue;
			if (!mVm.Active())
				continue;
			switch (action.kind) {
			case Bunny::CommandListActionKind::Run: {
				const std::shared_ptr<const Bunny::CommandListConfig> config =
					SnapshotCommandListConfig(action.commandList);
				if (config)
					RunCommandList(*config, depth + 1, includePost, thisMatch);
				break;
			}
			case Bunny::CommandListActionKind::CheckTextureOverride: {
				if (!mReplacement)
					break;
				RunCheckTextureOverrideTarget(action.target, depth, includePost);
				break;
			}
			case Bunny::CommandListActionKind::HandlingSkip:
				if (mReplacement)
					SetSkip();
				break;
			case Bunny::CommandListActionKind::SetIndexBuffer:
				if (mReplacement)
					AppendResourceViews(action.resource, std::vector<DX12CompiledVertexResourceBinding>());
				break;
			case Bunny::CommandListActionKind::SetVertexBuffer:
				if (mReplacement)
					AppendResourceViews(
						L"", std::vector<DX12CompiledVertexResourceBinding>{
							{ action.target.slot, action.resource }});
				break;
			case Bunny::CommandListActionKind::SetShaderResource:
				ApplySetDescriptorResourceAction(action.target, action.resource);
				break;
			case Bunny::CommandListActionKind::SetConstantBuffer:
				ApplySetDescriptorResourceAction(action.target, action.resource);
				break;
			case Bunny::CommandListActionKind::SetUnorderedAccessView:
				ApplySetDescriptorResourceAction(action.target, action.resource);
				break;
			case Bunny::CommandListActionKind::Draw:
			case Bunny::CommandListActionKind::DrawIndexed:
			case Bunny::CommandListActionKind::DrawIndexedAuto:
			case Bunny::CommandListActionKind::DrawFromCaller:
			case Bunny::CommandListActionKind::DrawIndexedFromCaller:
				if (mReplacement)
					AppendDraw(action);
				break;
			case Bunny::CommandListActionKind::Dispatch:
				if (mReplacement)
					AppendDispatch(action);
				break;
			case Bunny::CommandListActionKind::ControlFlow:
			case Bunny::CommandListActionKind::VariableAssignment:
			case Bunny::CommandListActionKind::PostVariableAssignment:
			case Bunny::CommandListActionKind::LocalVariableAssignment:
			case Bunny::CommandListActionKind::ResourceAssignment:
			case Bunny::CommandListActionKind::SetShader:
			case Bunny::CommandListActionKind::SetShaderConstant:
			case Bunny::CommandListActionKind::SetRenderState:
				break;
			}
		}
		LeaveCommandList();
	}

	void RunActions(
		const std::vector<Bunny::CommandListAction> &actions, int depth, bool includePost,
		const DX12TextureOverrideMatch *thisMatch)
	{
		if (depth > 16)
			return;

		const bool executeActions = mReplacement && (mExecuteCommands || mExecuteDrawActions);
		CollectDescriptorCheckTargetsFromActions(actions);
		if (!mDescriptorTargetScratch.empty())
			PrimeDescriptorTextureOverrideCacheForTargets(&mDescriptorTargetScratch);
		CommandVmRunScope vmScope(&mVm, includePost);
		for (const Bunny::CommandListAction &action : actions) {
			if (ApplyVmAction(action, includePost))
				continue;
			if (!mVm.Active())
				continue;
			switch (action.kind) {
			case Bunny::CommandListActionKind::Run: {
				if (!mExecuteCommands)
					break;
				const std::shared_ptr<const Bunny::CommandListConfig> config =
					SnapshotCommandListConfig(action.commandList);
				if (config)
					RunCommandList(*config, depth + 1, includePost, thisMatch);
				break;
			}
			case Bunny::CommandListActionKind::CheckTextureOverride: {
				if (!mReplacement || !mExecuteCommands)
					break;
				RunCheckTextureOverrideTarget(action.target, depth, includePost);
				break;
			}
			case Bunny::CommandListActionKind::HandlingSkip:
				if (executeActions)
					SetSkip();
				break;
			case Bunny::CommandListActionKind::SetIndexBuffer:
				if (mReplacement)
					AppendResourceViews(action.resource, std::vector<DX12CompiledVertexResourceBinding>());
				break;
			case Bunny::CommandListActionKind::SetVertexBuffer:
				if (mReplacement)
					AppendResourceViews(
						L"", std::vector<DX12CompiledVertexResourceBinding>{
							{ action.target.slot, action.resource }});
				break;
			case Bunny::CommandListActionKind::SetShaderResource:
				ApplySetDescriptorResourceAction(action.target, action.resource);
				break;
			case Bunny::CommandListActionKind::SetConstantBuffer:
				ApplySetDescriptorResourceAction(action.target, action.resource);
				break;
			case Bunny::CommandListActionKind::SetUnorderedAccessView:
				ApplySetDescriptorResourceAction(action.target, action.resource);
				break;
			case Bunny::CommandListActionKind::Draw:
			case Bunny::CommandListActionKind::DrawIndexed:
			case Bunny::CommandListActionKind::DrawIndexedAuto:
			case Bunny::CommandListActionKind::DrawFromCaller:
			case Bunny::CommandListActionKind::DrawIndexedFromCaller:
				if (executeActions)
					AppendDraw(action);
				break;
			case Bunny::CommandListActionKind::Dispatch:
				if (executeActions)
					AppendDispatch(action);
				break;
			case Bunny::CommandListActionKind::ControlFlow:
			case Bunny::CommandListActionKind::VariableAssignment:
			case Bunny::CommandListActionKind::PostVariableAssignment:
			case Bunny::CommandListActionKind::LocalVariableAssignment:
			case Bunny::CommandListActionKind::ResourceAssignment:
			case Bunny::CommandListActionKind::SetShader:
			case Bunny::CommandListActionKind::SetShaderConstant:
			case Bunny::CommandListActionKind::SetRenderState:
				break;
			}
		}
	}

	bool ApplyVmAction(const Bunny::CommandListAction &action, bool includePost)
	{
		switch (action.kind) {
		case Bunny::CommandListActionKind::ControlFlow:
			observedDynamicCommandState = true;
			mVm.ApplyControlFlow(action.rawLine.empty() ? action.key : action.rawLine);
			return true;
		case Bunny::CommandListActionKind::VariableAssignment:
			observedDynamicCommandState = true;
			if (mVm.Active())
				mVm.Assign(action.key, action.value);
			return true;
		case Bunny::CommandListActionKind::PostVariableAssignment:
			observedDynamicCommandState = true;
			if (includePost && mVm.Active())
				mVm.QueuePostAssign(action.key, action.value);
			return true;
		case Bunny::CommandListActionKind::LocalVariableAssignment:
			observedDynamicCommandState = true;
			if (mVm.Active())
				mVm.DeclareLocal(action.key, action.value);
			return true;
		case Bunny::CommandListActionKind::SetShaderConstant:
			observedDynamicCommandState = true;
			if (mVm.Active())
				mVm.AssignSymbol(action.key, action.value);
			return true;
		default:
			return false;
		}
	}

	void RunCommandListLinks(
		const Bunny::CommandListLinks &links, int depth, bool includePost,
		const DX12TextureOverrideMatch *thisMatch)
	{
		if (depth > 16)
			return;

		for (const std::wstring &list : links.pre) {
			if (!HasCommandListRuntimeEffect(list, false))
				continue;
			if (RunCompiledCommandListPlan(list, depth + 1, false))
				continue;
			const std::shared_ptr<const Bunny::CommandListConfig> config =
				SnapshotCommandListConfig(list);
			if (config)
				RunCommandList(*config, depth + 1, false, thisMatch);
		}
		for (const std::wstring &list : links.main) {
			if (!HasCommandListRuntimeEffect(list, includePost))
				continue;
			if (RunCompiledCommandListPlan(list, depth + 1, includePost))
				continue;
			const std::shared_ptr<const Bunny::CommandListConfig> config =
				SnapshotCommandListConfig(list);
			if (config)
				RunCommandList(*config, depth + 1, includePost, thisMatch);
		}
		if (includePost) {
			for (const std::wstring &list : links.post) {
				if (!HasCommandListRuntimeEffect(list, true))
					continue;
				if (RunCompiledCommandListPlan(list, depth + 1, true))
					continue;
				const std::shared_ptr<const Bunny::CommandListConfig> config =
					SnapshotCommandListConfig(list);
				if (config)
					RunCommandList(*config, depth + 1, true, thisMatch);
			}
		}
	}

	bool EnterCommandList(const std::wstring &name)
	{
		if (name.empty())
			return true;
		std::pair<std::unordered_set<std::wstring>::iterator, bool> inserted =
			mCommandListStackSet.insert(name);
		if (inserted.second) {
			mCommandListStack.push_back(name);
			return true;
		}
		return false;
	}

	void LeaveCommandList()
	{
		if (!mCommandListStack.empty()) {
			mCommandListStackSet.erase(mCommandListStack.back());
			mCommandListStack.pop_back();
		}
	}

	ID3D12Device *mDevice = nullptr;
	ID3D12GraphicsCommandList *mCommandList = nullptr;
	const DX12IaHashState &mIaState;
	DX12IaHashState mCurrentIaState;
	uint32_t mVertexCount = 0;
	uint32_t mIndexCount = 0;
	uint32_t mInstanceCount = 0;
	uint32_t mFirstVertex = 0;
	uint32_t mFirstIndex = 0;
	uint32_t mFirstInstance = 0;
	DX12ModIaReplacement *mReplacement = nullptr;
	std::vector<DX12PendingResourceViewRequest> *mPendingResourceViews = nullptr;
	bool mChanged = false;
	bool mExecuteCommands = true;
	bool mExecuteDrawActions = true;
	bool mSkipDirectDescriptorTargets = false;
	bool mSkipDirectDescriptorTargetsActive = false;
	bool mDescriptorTextureOverrideCachePrebuilt = false;
	bool mGraphicsSrvBindingsResolved = false;
	bool mResolvedGraphicsSrvBindingsResolved = false;
	bool mGraphicsSrvBindingsByTargetResolved = false;
	bool mGraphicsSrvTextureOverrideCacheResolved = false;
	bool mTextureOverrideRuntimeSnapshotResolved = false;
	DX12CommandListExecutorScratch mFallbackScratch;
	bool mScratchPooled = false;
	DX12CommandListExecutorScratch *mScratch = nullptr;
	std::vector<std::wstring> &mCommandListStack;
	std::unordered_set<std::wstring> &mCommandListStackSet;
	std::vector<Bunny::CommandListTarget> &mDescriptorTargetScratch;
	std::vector<DX12TextureOverrideMatch> &mTargetMatchScratch;
	std::vector<DX12CurrentShaderResourceBinding> &mGraphicsSrvBindings;
	std::vector<DX12ResolvedGraphicsSrvBinding> &mResolvedGraphicsSrvBindings;
	std::unordered_map<
		DX12TextureOverrideTargetCacheKey,
		std::vector<DX12ResolvedGraphicsSrvBinding>,
		DX12TextureOverrideTargetCacheKeyHash> &mGraphicsSrvBindingsByTarget;
	std::unordered_map<
		DX12TextureOverrideTargetCacheKey,
		std::vector<DX12TextureOverrideMatch>,
		DX12TextureOverrideTargetCacheKeyHash> &mGraphicsSrvTextureOverrideCache;
	std::unordered_map<
		DX12TextureOverrideTargetCacheKey,
		std::vector<DX12ResolvedGraphicsSrvBinding>,
		DX12TextureOverrideTargetCacheKeyHash> &mShaderResourceOverrideBindingsByTarget;
	std::unordered_map<
		DX12TextureOverrideTargetCacheKey,
		std::vector<DX12TextureOverrideMatch>,
		DX12TextureOverrideTargetCacheKeyHash> &mTargetTextureOverrideCache;
	std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot>
		mTextureOverrideRuntimeSnapshot;
	MigotoCommandVmUtils::State mVm;
};

static bool RunNamedCommandList(
	ID3D12Device *device,
	ID3D12GraphicsCommandList *commandList,
	const std::wstring &name,
	DX12ModIaReplacement *replacement,
	std::vector<DX12PendingResourceViewRequest> *pendingResourceViews)
{
	if (!device || !commandList || !replacement)
		return false;
	if (!CommandListRuntimeEffect(name, true))
		return false;

	DX12IaHashState emptyIa = EmptyIaHashState();
	DX12CommandListExecutor executor(
		device, commandList, emptyIa, 0, 0, 0, 0, 0, 0,
		replacement, pendingResourceViews);
	executor.RunCommandListLinks(Bunny::CommandListLinks{ {}, { name }, {} }, true);
	return executor.Changed();
}

static bool RunNamedCommandListVmOnly(const std::wstring &name, bool includePost)
{
	if (name.empty())
		return false;
	if (!CommandListRuntimeEffect(name, includePost))
		return false;

	DX12IaHashState emptyIa = EmptyIaHashState();
	DX12CommandListExecutor executor(
		nullptr, nullptr, emptyIa, 0, 0, 0, 0, 0, 0, nullptr, nullptr);
	executor.RunCommandListLinks(Bunny::CommandListLinks{ {}, { name }, {} }, includePost);
	return true;
}

static void ProcessDx12KeyBindingsForFrame()
{
	std::vector<Bunny::MigotoKeyBindingUtils::RuntimeAction> actions;
	AcquireSRWLockExclusive(&gModLock);
	if (gLoaded)
		Bunny::MigotoKeyBindingUtils::ProcessBindings(&gKeyBindings, &actions);
	ReleaseSRWLockExclusive(&gModLock);


	for (const Bunny::MigotoKeyBindingUtils::RuntimeAction &action : actions)
		RunNamedCommandListVmOnly(action.commandList, action.includePost);
}

static void AppendShaderOverrideForHashLocked(
	uint64_t hash, std::vector<const Bunny::ShaderOverrideConfig*> *configs,
	std::unordered_set<std::wstring> *seen)
{
	if (!hash || !configs || !seen)
		return;
	auto it = gShaderOverrides.find(hash);
	if (it == gShaderOverrides.end())
		return;
	if (!seen->insert(it->second.section).second)
		return;
	configs->push_back(&it->second);
}

static bool ShaderModelMatchesRegex(
	const Bunny::ShaderRegexConfig &regex, const std::string &shaderModel)
{
	if (regex.hasPattern || shaderModel.empty())
		return false;
	for (const std::string &model : regex.shaderModels) {
		if (model == shaderModel)
			return true;
	}
	return false;
}

static constexpr LONG ShaderOverrideHotPathLogLimit = 512;
static volatile LONG gShaderOverridePsoCacheHitLogCount = 0;
static volatile LONG gShaderOverrideNegativeCacheHitLogCount = 0;
static volatile LONG gShaderOverrideNegativeCacheStoreLogCount = 0;
static volatile LONG gShaderOverrideNegativeCacheDescriptorBlockedLogCount = 0;
static volatile LONG gShaderOverrideNegativeCacheDynamicBlockedLogCount = 0;

static bool AllowShaderOverrideHotPathLog(volatile LONG *counter)
{
	(void)counter;
	return false;
}

static Bunny::ShaderOverrideConfig MakeShaderOverrideFromRegex(
	const Bunny::ShaderRegexConfig &regex, uint64_t hash)
{
	Bunny::ShaderOverrideConfig config;
	config.section = regex.section;
	config.originalSection = regex.originalSection;
	config.sourcePath = regex.sourcePath;
	config.sourceDir = regex.sourceDir;
	config.iniNamespace = regex.iniNamespace;
	config.hash = hash;
	config.handlingSkip = regex.handlingSkip;
	config.commandLists = regex.commandLists;
	config.actions = regex.actions;
	return config;
}

static void AppendShaderRegexForShaderLocked(
	uint64_t hash,
	const std::string &shaderModel,
	std::vector<Bunny::ShaderOverrideConfig> *regexConfigs,
	std::unordered_set<std::wstring> *seen)
{
	if (!hash || shaderModel.empty() || !regexConfigs || !seen)
		return;

	for (const auto &item : gShaderRegexes) {
		const Bunny::ShaderRegexConfig &regex = item.second;
		if (!ShaderModelMatchesRegex(regex, shaderModel))
			continue;
		if (!seen->insert(regex.section).second)
			continue;
		regexConfigs->push_back(MakeShaderOverrideFromRegex(regex, hash));
	}
}

static void BuildShaderOverrideConfigPointers(
	DX12ShaderOverridePsoMatchCache *cache,
	bool dispatch,
	std::vector<const Bunny::ShaderOverrideConfig*> *configs)
{
	if (!cache || !configs)
		return;
	configs->clear();
	const std::vector<const Bunny::ShaderOverrideConfig*> &direct =
		dispatch ? cache->dispatchConfigs : cache->drawConfigs;
	configs->insert(configs->end(), direct.begin(), direct.end());
	std::vector<Bunny::ShaderOverrideConfig> &regex =
		dispatch ? cache->regexDispatchConfigs : cache->regexDrawConfigs;
	for (Bunny::ShaderOverrideConfig &config : regex)
		configs->push_back(&config);
}

static void CopyShaderOverrideConfigs(
	const std::vector<const Bunny::ShaderOverrideConfig*> &configs,
	std::vector<Bunny::ShaderOverrideConfig> *snapshot)
{
	if (!snapshot)
		return;
	snapshot->clear();
	snapshot->reserve(configs.size());
	for (const Bunny::ShaderOverrideConfig *config : configs) {
		if (config)
			snapshot->push_back(*config);
	}
}

static void MergeTextureOverrideDrawMatchUsage(
	const Bunny::TextureOverrideConfig &config,
	DX12DrawMatchUsage *usage)
{
	if (!usage)
		return;
	usage->useVertexCount = usage->useVertexCount || config.hasMatchVertexCount;
	usage->useIndexCount = usage->useIndexCount || config.hasMatchIndexCount;
	usage->useInstanceCount = usage->useInstanceCount || config.hasMatchInstanceCount;
	usage->useFirstVertex = usage->useFirstVertex || config.hasMatchFirstVertex;
	usage->useFirstIndex = usage->useFirstIndex || config.hasMatchFirstIndex;
	usage->useFirstInstance = usage->useFirstInstance || config.hasMatchFirstInstance;
}

static void AppendUniqueDrawMatchUsageTargetLocked(
	const Bunny::CommandListTarget &target,
	std::vector<Bunny::CommandListTarget> *targets)
{
	if (!targets)
		return;
	for (const Bunny::CommandListTarget &existing : *targets) {
		if (existing.kind == target.kind &&
			existing.stage == target.stage &&
			existing.slot == target.slot) {
			return;
		}
	}
	targets->push_back(target);
}

static bool DrawMatchUsageCommandListRuntimeEffectLocked(const std::wstring &name)
{
	std::unordered_map<std::wstring, bool>::const_iterator found =
		gCommandListPreRuntimeEffect.find(name);
	return found == gCommandListPreRuntimeEffect.end() || found->second;
}

static void CollectCheckTextureOverrideTargetsFromActionsLocked(
	const std::vector<Bunny::CommandListAction> &actions,
	UINT depth,
	std::unordered_set<std::wstring> *commandListStack,
	std::vector<Bunny::CommandListTarget> *targets)
{
	if (depth > 16)
		return;
	for (const Bunny::CommandListAction &action : actions) {
		switch (action.kind) {
		case Bunny::CommandListActionKind::CheckTextureOverride:
			AppendUniqueDrawMatchUsageTargetLocked(action.target, targets);
			break;
		case Bunny::CommandListActionKind::Run: {
			if (action.commandList.empty() ||
				!DrawMatchUsageCommandListRuntimeEffectLocked(action.commandList)) {
				break;
			}
			if (commandListStack &&
				commandListStack->find(action.commandList) !=
					commandListStack->end()) {
				break;
			}
			if (commandListStack)
				commandListStack->insert(action.commandList);
			Bunny::CommandListMap::const_iterator found =
				gCommandLists.find(action.commandList);
			if (found != gCommandLists.end()) {
				CollectCheckTextureOverrideTargetsFromActionsLocked(
					found->second.actions,
					depth + 1,
					commandListStack,
					targets);
			}
			if (commandListStack)
				commandListStack->erase(action.commandList);
			break;
		}
		default:
			break;
		}
	}
}

static void CollectCheckTextureOverrideTargetsFromShaderOverrideLocked(
	const Bunny::ShaderOverrideConfig &config,
	std::vector<Bunny::CommandListTarget> *targets)
{
	std::unordered_set<std::wstring> commandListStack;
	CollectCheckTextureOverrideTargetsFromActionsLocked(
		config.actions,
		0,
		&commandListStack,
		targets);
	for (const std::wstring &name : config.commandLists.pre) {
		if (!name.empty()) {
			commandListStack.insert(name);
			Bunny::CommandListMap::const_iterator found = gCommandLists.find(name);
			if (DrawMatchUsageCommandListRuntimeEffectLocked(name) &&
				found != gCommandLists.end()) {
				CollectCheckTextureOverrideTargetsFromActionsLocked(
					found->second.actions,
					1,
					&commandListStack,
					targets);
			}
			commandListStack.erase(name);
		}
	}
	for (const std::wstring &name : config.commandLists.main) {
		if (!name.empty()) {
			commandListStack.insert(name);
			Bunny::CommandListMap::const_iterator found = gCommandLists.find(name);
			if (DrawMatchUsageCommandListRuntimeEffectLocked(name) &&
				found != gCommandLists.end()) {
				CollectCheckTextureOverrideTargetsFromActionsLocked(
					found->second.actions,
					1,
					&commandListStack,
					targets);
			}
			commandListStack.erase(name);
		}
	}
}


static void MergeDescriptorTextureOverrideDrawMatchUsageLocked(
	DX12DrawMatchUsage *usage)
{
	if (!usage)
		return;
	std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> snapshot =
		LoadCommandExecutionRuntimeSnapshot();
	if (snapshot) {
		std::unordered_set<const Bunny::TextureOverrideConfig*> seenConfigs;
		std::unordered_map<uint32_t, std::vector<DX12TriggeredTextureOverride>>::const_iterator bucket =
			snapshot->textureOverridesByHash.begin();
		for (; bucket != snapshot->textureOverridesByHash.end(); ++bucket) {
			if (!bucket->first)
				continue;
			const std::vector<DX12TriggeredTextureOverride> &triggers = bucket->second;
			for (const DX12TriggeredTextureOverride &triggered : triggers) {
				if (!triggered.config || !seenConfigs.insert(triggered.config).second)
					continue;
				MergeTextureOverrideDrawMatchUsage(*triggered.config, usage);
			}
		}
		return;
	}
	Bunny::TextureOverrideMap::const_iterator bucket = gTextureOverrides.begin();
	for (; bucket != gTextureOverrides.end(); ++bucket) {
		if (!bucket->first)
			continue;
		const std::vector<Bunny::TextureOverrideConfig> &configs = bucket->second;
		for (const Bunny::TextureOverrideConfig &config : configs)
			MergeTextureOverrideDrawMatchUsage(config, usage);
	}
}

static void MergeIaTextureOverrideDrawMatchUsageLocked(
	bool indexBuffer,
	uint32_t vertexSlot,
	DX12DrawMatchUsage *usage)
{
	if (!usage)
		return;
	for (size_t index = 0; index < gIaTextureOverrides.size(); ++index) {
		const Bunny::TextureOverrideConfig *config = gIaTextureOverrides[index].config;
		if (!config)
			continue;
		if (!TextureOverrideMatchesIaBinding(*config, indexBuffer, vertexSlot))
			continue;
		MergeTextureOverrideDrawMatchUsage(*config, usage);
	}
}

static DX12DrawMatchUsage BuildTextureOverrideDrawMatchUsageLocked(
	const std::vector<Bunny::CommandListTarget> &targets)
{
	DX12DrawMatchUsage usage;
	for (const Bunny::CommandListTarget &target : targets) {
		switch (target.kind) {
		case Bunny::CommandListTargetKind::IndexBuffer:
			MergeIaTextureOverrideDrawMatchUsageLocked(true, 0, &usage);
			break;
		case Bunny::CommandListTargetKind::VertexBuffer:
			MergeIaTextureOverrideDrawMatchUsageLocked(false, target.slot, &usage);
			break;
		case Bunny::CommandListTargetKind::ConstantBuffer:
		case Bunny::CommandListTargetKind::ShaderResource:
		case Bunny::CommandListTargetKind::UnorderedAccessView:
			MergeDescriptorTextureOverrideDrawMatchUsageLocked(&usage);
			break;
		default:
			break;
		}
	}
	return usage;
}

static DX12DrawMatchUsage BuildDrawMatchUsageLocked(
	const std::vector<const Bunny::ShaderOverrideConfig*> &configs)
{
	std::vector<Bunny::CommandListTarget> targets;
	for (const Bunny::ShaderOverrideConfig *config : configs) {
		if (config)
			CollectCheckTextureOverrideTargetsFromShaderOverrideLocked(*config, &targets);
	}
	return BuildTextureOverrideDrawMatchUsageLocked(targets);
}

static DX12DrawMatchUsage BuildDrawMatchUsageLocked(
	const std::vector<Bunny::ShaderOverrideConfig> &configs)
{
	std::vector<Bunny::CommandListTarget> targets;
	for (const Bunny::ShaderOverrideConfig &config : configs) {
		CollectCheckTextureOverrideTargetsFromShaderOverrideLocked(config, &targets);
	}
	return BuildTextureOverrideDrawMatchUsageLocked(targets);
}

class DX12ShaderOverrideConfigView
{
public:
	void SetOwnedSnapshot(
		const std::shared_ptr<const std::vector<Bunny::ShaderOverrideConfig>> &snapshot)
	{
		mOwnedSnapshot = snapshot;
		mFallbackSnapshot.clear();
	}

	std::vector<Bunny::ShaderOverrideConfig> *MutableFallbackSnapshot()
	{
		mOwnedSnapshot.reset();
		return &mFallbackSnapshot;
	}

	const std::vector<Bunny::ShaderOverrideConfig> &Get() const
	{
		return mOwnedSnapshot ? *mOwnedSnapshot : mFallbackSnapshot;
	}

private:
	std::shared_ptr<const std::vector<Bunny::ShaderOverrideConfig>> mOwnedSnapshot;
	std::vector<Bunny::ShaderOverrideConfig> mFallbackSnapshot;
};

// PSO-Create-Time pre-compiled override plan: snapshots all shader override configs
// for a PSO so draw-time discovery and per-draw deep copy are eliminated.
// Populated in RecordGraphicsPipelineState; consumed in PrepareShaderOverrideReplacement.
class DX12CompiledPsoOverridePlan
{
public:
	bool valid = false;
	bool alwaysMiss = false;
	UINT64 generation = 0;
	bool drawDecisionMetadataValid = false;
	std::shared_ptr<const std::vector<Bunny::ShaderOverrideConfig>> configSnapshot;
	std::shared_ptr<const std::vector<DX12CompiledShaderOverridePlan>> compiledPlanSnapshot;
	std::shared_ptr<const DX12ModIaReplacement> staticReplacementSnapshot;
	DX12IaHashTargetPlan drawTargetPlan;
	DX12IaHashTargetPlan drawDirectTargetPlan;
	DX12DrawMatchUsage drawMatchUsage;
	DX12DrawMatchUsage drawDirectDrawMatchUsage;
	bool drawDirectTextureChecksOnly = false;
	bool staticReplacementReady = false;
};

static std::unordered_map<ID3D12PipelineState*, DX12CompiledPsoOverridePlan> gPsoOverridePlans;
static SRWLOCK gPsoOverridePlansLock = SRWLOCK_INIT;
static UINT64 gPsoOverridePlansFormatVersion = 0;

static void ClearPsoOverridesIfFormatChanged()
{
	if (gPsoOverridePlansFormatVersion !=
		DX12PsoDecisionCacheUtils::CacheFormatVersionCurrent) {
		AcquireSRWLockExclusive(&gPsoOverridePlansLock);
		gPsoOverridePlans.clear();
		gPsoOverridePlansFormatVersion =
			DX12PsoDecisionCacheUtils::CacheFormatVersionCurrent;
		ReleaseSRWLockExclusive(&gPsoOverridePlansLock);
	}
}

class DX12CompiledPsoPlanTlsCache
{
public:
	ID3D12PipelineState *pipelineState = nullptr;
	UINT64 reloadGeneration = 0;
	bool valid = false;
	bool alwaysMiss = false;
	std::shared_ptr<const std::vector<Bunny::ShaderOverrideConfig>> configSnapshot;
	std::shared_ptr<const std::vector<DX12CompiledShaderOverridePlan>> compiledPlanSnapshot;

	~DX12CompiledPsoPlanTlsCache()
	{
		if (pipelineState)
			pipelineState->Release();
	}

	void Store(
		ID3D12PipelineState *newPipelineState,
		UINT64 newReloadGeneration,
		bool newAlwaysMiss,
		const std::shared_ptr<const std::vector<Bunny::ShaderOverrideConfig>> &newConfigSnapshot,
		const std::shared_ptr<const std::vector<DX12CompiledShaderOverridePlan>> &newCompiledPlanSnapshot)
	{
		if (pipelineState != newPipelineState) {
			if (pipelineState)
				pipelineState->Release();
			pipelineState = newPipelineState;
			if (pipelineState)
				pipelineState->AddRef();
		}
		reloadGeneration = newReloadGeneration;
		valid = true;
		alwaysMiss = newAlwaysMiss;
		configSnapshot = newConfigSnapshot;
		compiledPlanSnapshot = newCompiledPlanSnapshot;
	}
};

static thread_local DX12CompiledPsoPlanTlsCache gCompiledPsoPlanTlsCache;

static bool TryLoadCompiledPsoPlan(
	ID3D12PipelineState *pipelineState,
	UINT64 reloadGeneration,
	std::shared_ptr<const std::vector<Bunny::ShaderOverrideConfig>> *configSnapshot,
	std::shared_ptr<const std::vector<DX12CompiledShaderOverridePlan>> *compiledPlanSnapshot,
	bool *alwaysMiss)
{
	if (!configSnapshot || !compiledPlanSnapshot || !alwaysMiss)
		return false;
	*alwaysMiss = false;
	configSnapshot->reset();
	compiledPlanSnapshot->reset();
	if (!pipelineState)
		return false;
	if (gCompiledPsoPlanTlsCache.valid &&
		gCompiledPsoPlanTlsCache.pipelineState == pipelineState &&
		gCompiledPsoPlanTlsCache.reloadGeneration == reloadGeneration) {
		*alwaysMiss = gCompiledPsoPlanTlsCache.alwaysMiss;
		if (!*alwaysMiss) {
			*configSnapshot = gCompiledPsoPlanTlsCache.configSnapshot;
			*compiledPlanSnapshot = gCompiledPsoPlanTlsCache.compiledPlanSnapshot;
		}
		return true;
	}

	ClearPsoOverridesIfFormatChanged();
	AcquireSRWLockShared(&gPsoOverridePlansLock);
	auto it = gPsoOverridePlans.find(pipelineState);
	if (it == gPsoOverridePlans.end() || it->second.generation != reloadGeneration) {
		ReleaseSRWLockShared(&gPsoOverridePlansLock);
		return false;
	}
	const bool cachedAlwaysMiss = it->second.alwaysMiss;
	const std::shared_ptr<const std::vector<Bunny::ShaderOverrideConfig>> cachedConfigSnapshot =
		it->second.configSnapshot;
	const std::shared_ptr<const std::vector<DX12CompiledShaderOverridePlan>> cachedCompiledPlanSnapshot =
		it->second.compiledPlanSnapshot;
	ReleaseSRWLockShared(&gPsoOverridePlansLock);
	gCompiledPsoPlanTlsCache.Store(
		pipelineState,
		reloadGeneration,
		cachedAlwaysMiss,
		cachedConfigSnapshot,
		cachedCompiledPlanSnapshot);
	*alwaysMiss = cachedAlwaysMiss;
	if (!*alwaysMiss) {
		*configSnapshot = cachedConfigSnapshot;
		*compiledPlanSnapshot = cachedCompiledPlanSnapshot;
	}
	return true;
}

static bool TryLoadCompiledPsoDrawDecisionMetadata(
	ID3D12PipelineState *pipelineState,
	UINT64 reloadGeneration,
	DX12IaHashTargetPlan *drawTargetPlan,
	DX12IaHashTargetPlan *drawDirectTargetPlan,
	DX12DrawMatchUsage *drawMatchUsage,
	DX12DrawMatchUsage *drawDirectDrawMatchUsage,
	bool *drawDirectTextureChecksOnly)
{
	if (!pipelineState ||
		!drawTargetPlan ||
		!drawDirectTargetPlan ||
		!drawMatchUsage ||
		!drawDirectDrawMatchUsage ||
		!drawDirectTextureChecksOnly) {
		return false;
	}

	AcquireSRWLockShared(&gPsoOverridePlansLock);
	auto it = gPsoOverridePlans.find(pipelineState);
	if (it == gPsoOverridePlans.end() ||
		it->second.generation != reloadGeneration ||
		!it->second.drawDecisionMetadataValid) {
		ReleaseSRWLockShared(&gPsoOverridePlansLock);
		return false;
	}
	*drawTargetPlan = it->second.drawTargetPlan;
	*drawDirectTargetPlan = it->second.drawDirectTargetPlan;
	*drawMatchUsage = it->second.drawMatchUsage;
	*drawDirectDrawMatchUsage = it->second.drawDirectDrawMatchUsage;
	*drawDirectTextureChecksOnly = it->second.drawDirectTextureChecksOnly;
	ReleaseSRWLockShared(&gPsoOverridePlansLock);
	return true;
}

bool DX12ModTryLoadStaticReplacementForPso(
	ID3D12PipelineState *pipelineState,
	UINT64 reloadGeneration,
	DX12ModIaReplacement *replacement)
{
	if (!replacement || !pipelineState)
		return false;
	AcquireSRWLockShared(&gPsoOverridePlansLock);
	auto it = gPsoOverridePlans.find(pipelineState);
	if (it == gPsoOverridePlans.end() ||
	    it->second.generation != reloadGeneration ||
	    !it->second.staticReplacementReady ||
	    !it->second.staticReplacementSnapshot) {
		ReleaseSRWLockShared(&gPsoOverridePlansLock);
		return false;
	}
	*replacement = *it->second.staticReplacementSnapshot;
	ReleaseSRWLockShared(&gPsoOverridePlansLock);
	return true;
}

std::shared_ptr<const DX12ModIaReplacement>
DX12ModFindStaticReplacementSnapshotForPso(
	ID3D12PipelineState *pipelineState,
	UINT64 reloadGeneration)
{
	if (!pipelineState)
		return nullptr;
	AcquireSRWLockShared(&gPsoOverridePlansLock);
	auto it = gPsoOverridePlans.find(pipelineState);
	if (it == gPsoOverridePlans.end() ||
	    it->second.generation != reloadGeneration ||
	    !it->second.staticReplacementReady ||
	    !it->second.staticReplacementSnapshot) {
		ReleaseSRWLockShared(&gPsoOverridePlansLock);
		return nullptr;
	}
	std::shared_ptr<const DX12ModIaReplacement> snapshot =
		it->second.staticReplacementSnapshot;
	ReleaseSRWLockShared(&gPsoOverridePlansLock);
	return snapshot;
}

static std::shared_ptr<const std::vector<DX12CompiledShaderOverridePlan>>
BuildCompiledShaderOverridePlanSnapshot(
	const std::vector<Bunny::ShaderOverrideConfig> &configSnapshot)
{
	if (configSnapshot.empty())
		return nullptr;
	std::shared_ptr<std::vector<DX12CompiledShaderOverridePlan>> compiledPlans =
		std::make_shared<std::vector<DX12CompiledShaderOverridePlan>>();
	compiledPlans->reserve(configSnapshot.size());
	for (const Bunny::ShaderOverrideConfig &config : configSnapshot) {
		DX12CompiledShaderOverridePlan plan;
		BuildCompiledShaderOverridePlanFromParts(
			config.handlingSkip,
			config.commandLists,
			config.actions,
			gCompiledCommandListPlans,
			&plan);
		if (plan.unsupported)
			return nullptr;
		compiledPlans->push_back(std::move(plan));
	}
	return compiledPlans;
}

static std::shared_ptr<const DX12ModIaReplacement>
BuildStaticReplacementSnapshotFromCompiledPlans(
	const std::vector<DX12CompiledShaderOverridePlan> &plans);

static void StoreCompiledPsoPlan(
	ID3D12PipelineState *pipelineState,
	UINT64 reloadGeneration,
	const std::vector<Bunny::ShaderOverrideConfig> &configSnapshot,
	const DX12IaHashTargetPlan *drawTargetPlan = nullptr,
	const DX12IaHashTargetPlan *drawDirectTargetPlan = nullptr,
	const DX12DrawMatchUsage *drawMatchUsage = nullptr,
	const DX12DrawMatchUsage *drawDirectDrawMatchUsage = nullptr,
	const bool *drawDirectTextureChecksOnly = nullptr)
{
	// Never store empty plans: PSOs created before mod loads would cache "alwaysMiss"
	// and never recover when the mod later loads matching configs.
	if (!pipelineState || configSnapshot.empty())
		return;

	AcquireSRWLockExclusive(&gPsoOverridePlansLock);
	DX12CompiledPsoOverridePlan plan;
	plan.valid = true;
	plan.alwaysMiss = false;
	plan.generation = reloadGeneration;
	plan.configSnapshot = std::make_shared<const std::vector<Bunny::ShaderOverrideConfig>>(configSnapshot);
	if (drawTargetPlan && drawDirectTargetPlan && drawMatchUsage &&
		drawDirectDrawMatchUsage && drawDirectTextureChecksOnly) {
		plan.drawTargetPlan = *drawTargetPlan;
		plan.drawDirectTargetPlan = *drawDirectTargetPlan;
		plan.drawMatchUsage = *drawMatchUsage;
		plan.drawDirectDrawMatchUsage = *drawDirectDrawMatchUsage;
		plan.drawDirectTextureChecksOnly = *drawDirectTextureChecksOnly;
		plan.drawDecisionMetadataValid = true;
	}
	plan.compiledPlanSnapshot =
		BuildCompiledShaderOverridePlanSnapshot(configSnapshot);
	if (plan.compiledPlanSnapshot && !plan.compiledPlanSnapshot->empty()) {
		plan.staticReplacementSnapshot =
			BuildStaticReplacementSnapshotFromCompiledPlans(
				*plan.compiledPlanSnapshot);
		plan.staticReplacementReady = plan.staticReplacementSnapshot != nullptr;
	}
	gPsoOverridePlans[pipelineState] = plan;
	ReleaseSRWLockExclusive(&gPsoOverridePlansLock);
}

static void StoreStaticReplacementForPso(
	ID3D12PipelineState *pipelineState,
	UINT64 reloadGeneration,
	const DX12ModIaReplacement &replacement)
{
	if (!pipelineState)
		return;
	AcquireSRWLockExclusive(&gPsoOverridePlansLock);
	auto it = gPsoOverridePlans.find(pipelineState);
	if (it != gPsoOverridePlans.end() &&
	    it->second.generation == reloadGeneration) {
		it->second.staticReplacementSnapshot =
			std::make_shared<const DX12ModIaReplacement>(replacement);
		it->second.staticReplacementReady = true;
	}
	ReleaseSRWLockExclusive(&gPsoOverridePlansLock);
}

static void ClearCompiledPsoPlans()
{
	AcquireSRWLockExclusive(&gPsoOverridePlansLock);
	gPsoOverridePlans.clear();
	ReleaseSRWLockExclusive(&gPsoOverridePlansLock);
}

static bool ReplacementHasAnyEffect(const DX12ModIaReplacement &replacement)
{
	return replacement.skip ||
		!replacement.draws.empty() ||
		!replacement.dispatches.empty() ||
		!replacement.drawSrvPatches.empty() ||
		!replacement.postCommandLists.empty() ||
		!replacement.queuedPostTextureOverrides.empty();
}

static bool ReplacementIsStaticTemplateCandidate(
	const DX12CommandListExecutor &executor,
	const std::vector<DX12PendingResourceViewRequest> &pendingResourceViews,
	const DX12ModIaReplacement &replacement)
{
	if (executor.observedDescriptorTarget ||
	    executor.observedDynamicCommandState ||
	    !pendingResourceViews.empty() ||
	    !replacement.drawSrvPatches.empty() ||
	    !replacement.postCommandLists.empty() ||
	    !replacement.queuedPostTextureOverrides.empty()) {
		return false;
	}
	for (const DX12ModIaReplacement::DrawCall &draw : replacement.draws) {
		if (draw.fromCaller)
			return false;
	}
	return true;
}

static bool CompiledCommandListPlanIsStaticTemplateCandidate(
	const DX12CompiledCommandListPlan &plan)
{
	if (plan.unsupported || plan.NeedsResidentIaState())
		return false;
	if (!plan.ops.empty()) {
		for (const DX12CompiledCommandListPlan::Op &op : plan.ops) {
			switch (op.kind) {
			case DX12CompiledCommandListPlan::Op::Kind::HandlingSkip:
			case DX12CompiledCommandListPlan::Op::Kind::Draw:
			case DX12CompiledCommandListPlan::Op::Kind::Dispatch:
				if (op.kind == DX12CompiledCommandListPlan::Op::Kind::Draw &&
				    op.draw.fromCaller) {
					return false;
				}
				break;
			default:
				return false;
			}
		}
	}
	for (const DX12ModIaReplacement::DrawCall &draw : plan.draws) {
		if (draw.fromCaller)
			return false;
	}
	return true;
}

static bool CompiledShaderOverridePlanIsStaticTemplateCandidate(
	const DX12CompiledShaderOverridePlan &plan)
{
	if (plan.unsupported)
		return false;
	if (plan.hasPostCommandListPlan || !plan.postCommandLists.empty())
		return false;
	return CompiledCommandListPlanIsStaticTemplateCandidate(plan.mainPlan);
}

static std::shared_ptr<const DX12ModIaReplacement>
BuildStaticReplacementSnapshotFromCompiledPlans(
	const std::vector<DX12CompiledShaderOverridePlan> &plans)
{
	for (const DX12CompiledShaderOverridePlan &plan : plans) {
		if (!CompiledShaderOverridePlanIsStaticTemplateCandidate(plan))
			return nullptr;
	}

	DX12ModIaReplacement replacement;
	DX12IaHashState emptyIa = EmptyIaHashState();
	DX12CommandListExecutor executor(
		nullptr, nullptr, emptyIa, 0, 0, 0, 0, 0, 0, &replacement, nullptr);
	executor.SetExecutionMode(true, true);
	for (const DX12CompiledShaderOverridePlan &plan : plans)
		executor.RunCompiledShaderOverridePlan(plan);
	if (!ReplacementHasAnyEffect(replacement))
		return nullptr;
	return std::make_shared<const DX12ModIaReplacement>(replacement);
}

class DX12ShaderOverrideIaTargetPlanUtils
{
public:
	static bool CommandListRuntimeEffectLocked(const std::wstring &name)
	{
		std::unordered_map<std::wstring, bool>::const_iterator found =
			gCommandListPreRuntimeEffect.find(name);
		return found == gCommandListPreRuntimeEffect.end() || found->second;
	}

	static void RequireTargetLocked(
		const Bunny::CommandListTarget &target,
		DX12IaHashTargetPlan *targetPlan)
	{
		if (!targetPlan)
			return;

		switch (target.kind) {
		case Bunny::CommandListTargetKind::IndexBuffer:
			targetPlan->RequireIndexBuffer();
			break;
		case Bunny::CommandListTargetKind::VertexBuffer:
			targetPlan->RequireVertexBuffer(target.slot);
			break;
		case Bunny::CommandListTargetKind::ConstantBuffer:
		case Bunny::CommandListTargetKind::ShaderResource:
		case Bunny::CommandListTargetKind::UnorderedAccessView:
			targetPlan->RequireDescriptorTarget(
				static_cast<uint32_t>(target.kind),
				static_cast<uint32_t>(target.stage),
				target.slot);
			break;
		default:
			targetPlan->RequireAll();
			break;
		}
	}

	static void CollectCommandListLocked(
		const std::wstring &name,
		UINT depth,
		std::unordered_set<std::wstring> *commandListStack,
		DX12IaHashTargetPlan *targetPlan,
		bool *textureOverrideTargetsCollected,
		bool collectTextureOverrideTargets,
		bool *hasDirectNonTextureEffect);

	static void CollectActionsLocked(
		const std::vector<Bunny::CommandListAction> &actions,
		UINT depth,
		std::unordered_set<std::wstring> *commandListStack,
		DX12IaHashTargetPlan *targetPlan,
		bool *textureOverrideTargetsCollected,
		bool collectTextureOverrideTargets,
		bool *hasDirectNonTextureEffect)
	{
		if (!targetPlan || depth > 16)
			return;

		for (const Bunny::CommandListAction &action : actions) {
			switch (action.kind) {
			case Bunny::CommandListActionKind::Run:
				CollectCommandListLocked(
					action.commandList, depth + 1, commandListStack,
					targetPlan, textureOverrideTargetsCollected,
					collectTextureOverrideTargets,
					hasDirectNonTextureEffect);
				break;
			case Bunny::CommandListActionKind::CheckTextureOverride:
				RequireTargetLocked(action.target, targetPlan);
				if (collectTextureOverrideTargets &&
				    textureOverrideTargetsCollected &&
				    !*textureOverrideTargetsCollected) {
					*textureOverrideTargetsCollected = true;
					CollectAllTextureOverrideTargetsLocked(
						commandListStack, targetPlan,
						textureOverrideTargetsCollected);
				}
				break;
			default:
				if (hasDirectNonTextureEffect)
					*hasDirectNonTextureEffect = true;
				break;
			}
		}
	}

	static void CollectCommandListLinksLocked(
		const Bunny::CommandListLinks &links,
		bool includePost,
		UINT depth,
		std::unordered_set<std::wstring> *commandListStack,
		DX12IaHashTargetPlan *targetPlan,
		bool *textureOverrideTargetsCollected,
		bool collectTextureOverrideTargets,
		bool *hasDirectNonTextureEffect)
	{
		if (!targetPlan || depth > 16)
			return;

		for (const std::wstring &name : links.pre) {
			CollectCommandListLocked(
				name, depth + 1, commandListStack, targetPlan,
				textureOverrideTargetsCollected, collectTextureOverrideTargets,
				hasDirectNonTextureEffect);
		}
		for (const std::wstring &name : links.main) {
			CollectCommandListLocked(
				name, depth + 1, commandListStack, targetPlan,
				textureOverrideTargetsCollected, collectTextureOverrideTargets,
				hasDirectNonTextureEffect);
		}
		if (!includePost)
			return;
		for (const std::wstring &name : links.post) {
			CollectCommandListLocked(
				name, depth + 1, commandListStack, targetPlan,
				textureOverrideTargetsCollected, collectTextureOverrideTargets,
				hasDirectNonTextureEffect);
		}
	}

	static void CollectShaderOverrideConfigLocked(
		const Bunny::ShaderOverrideConfig &config,
		std::unordered_set<std::wstring> *commandListStack,
		DX12IaHashTargetPlan *targetPlan,
		bool *textureOverrideTargetsCollected,
		bool collectTextureOverrideTargets,
		bool includePostLinks,
		bool *hasDirectNonTextureEffect)
	{
		CollectCommandListLinksLocked(
			config.commandLists, includePostLinks, 0, commandListStack, targetPlan,
			textureOverrideTargetsCollected, collectTextureOverrideTargets,
			hasDirectNonTextureEffect);
		CollectActionsLocked(
			config.actions, 0, commandListStack, targetPlan,
			textureOverrideTargetsCollected, collectTextureOverrideTargets,
			hasDirectNonTextureEffect);
	}

	static void CollectAllTextureOverrideTargetsLocked(
		std::unordered_set<std::wstring> *commandListStack,
		DX12IaHashTargetPlan *targetPlan,
		bool *textureOverrideTargetsCollected)
	{
		if (!targetPlan)
			return;
		if (textureOverrideTargetsCollected)
			*textureOverrideTargetsCollected = true;

		Bunny::TextureOverrideMap::const_iterator bucket = gTextureOverrides.begin();
		for (; bucket != gTextureOverrides.end(); ++bucket) {
			const Bunny::TextureOverrideList &configs = bucket->second;
			for (size_t index = 0; index < configs.size(); ++index) {
				const Bunny::TextureOverrideConfig &config = configs[index];
				CollectCommandListLinksLocked(
					config.commandLists, true, 0, commandListStack,
					targetPlan, textureOverrideTargetsCollected, false, nullptr);
				CollectActionsLocked(
					config.actions, 0, commandListStack, targetPlan,
					textureOverrideTargetsCollected, false, nullptr);
			}
		}
	}

	static DX12IaHashTargetPlan BuildDrawPlanLocked(
		const DX12ShaderOverridePsoMatchCache &cache,
		bool collectTextureOverrideTargets,
		bool *hasDirectNonTextureEffect = nullptr)
	{
		DX12IaHashTargetPlan targetPlan;
		targetPlan.ResetKnown();
		bool textureOverrideTargetsCollected = false;
		std::unordered_set<std::wstring> commandListStack;

		for (const Bunny::ShaderOverrideConfig *config : cache.drawConfigs) {
			if (!config)
				continue;
			CollectShaderOverrideConfigLocked(
				*config, &commandListStack, &targetPlan,
				collectTextureOverrideTargets ? &textureOverrideTargetsCollected : nullptr,
				collectTextureOverrideTargets,
				false,
				hasDirectNonTextureEffect);
		}
		for (const Bunny::ShaderOverrideConfig &config : cache.regexDrawConfigs) {
			CollectShaderOverrideConfigLocked(
				config, &commandListStack, &targetPlan,
				collectTextureOverrideTargets ? &textureOverrideTargetsCollected : nullptr,
				collectTextureOverrideTargets,
				false,
				hasDirectNonTextureEffect);
		}
		return targetPlan;
	}

	static DX12IaHashTargetPlan BuildDrawPlanLocked(
		const std::vector<Bunny::ShaderOverrideConfig> &configs,
		bool collectTextureOverrideTargets,
		bool *hasDirectNonTextureEffect = nullptr)
	{
		DX12IaHashTargetPlan targetPlan;
		targetPlan.ResetKnown();
		bool textureOverrideTargetsCollected = false;
		std::unordered_set<std::wstring> commandListStack;

		for (const Bunny::ShaderOverrideConfig &config : configs) {
			CollectShaderOverrideConfigLocked(
				config, &commandListStack, &targetPlan,
				collectTextureOverrideTargets ? &textureOverrideTargetsCollected : nullptr,
				collectTextureOverrideTargets,
				false,
				hasDirectNonTextureEffect);
		}
		return targetPlan;
	}
};

inline void DX12ShaderOverrideIaTargetPlanUtils::CollectCommandListLocked(
	const std::wstring &name,
	UINT depth,
	std::unordered_set<std::wstring> *commandListStack,
	DX12IaHashTargetPlan *targetPlan,
	bool *textureOverrideTargetsCollected,
	bool collectTextureOverrideTargets,
	bool *hasDirectNonTextureEffect)
{
	if (!targetPlan || name.empty() || depth > 16)
		return;
	if (!CommandListRuntimeEffectLocked(name))
		return;
	if (commandListStack) {
		if (commandListStack->find(name) != commandListStack->end())
			return;
		commandListStack->insert(name);
	}

	Bunny::CommandListMap::const_iterator found = gCommandLists.find(name);
	if (found != gCommandLists.end()) {
		CollectActionsLocked(
			found->second.actions, depth + 1, commandListStack, targetPlan,
			textureOverrideTargetsCollected, collectTextureOverrideTargets,
			hasDirectNonTextureEffect);
	}

	if (commandListStack)
		commandListStack->erase(name);
}

static bool ShaderOverrideNegativeCacheEnabled(
	bool allowDescriptorIndependentNegativeCache)
{
	return allowDescriptorIndependentNegativeCache &&
		DX12ModHasActiveShaderOverrides();
}

static DX12ShaderOverrideNegativeCacheKey MakeShaderOverrideNegativeCacheKey(
	ID3D12PipelineState *pipelineState,
	const DX12IaHashState &iaState,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance,
	const DX12DrawMatchUsage &usage)
{
	DX12ShaderOverrideNegativeCacheKey key = {};
	key.reloadGeneration = static_cast<UINT64>(InterlockedCompareExchange64(
		reinterpret_cast<volatile LONG64*>(&gShaderOverrideNegativeCacheGeneration), 0, 0));
	key.preSkinGeneration = CurrentPreSkinActiveGeneration();
	key.pipelineState = pipelineState;
	key.vertexCount = usage.useVertexCount ? vertexCount : 0;
	key.indexCount = usage.useIndexCount ? indexCount : 0;
	key.instanceCount = usage.useInstanceCount ? instanceCount : 0;
	key.firstVertex = usage.useFirstVertex ? firstVertex : 0;
	key.firstIndex = usage.useFirstIndex ? firstIndex : 0;
	key.firstInstance = usage.useFirstInstance ? firstInstance : 0;
	key.hasIndexBuffer = iaState.hasIndexBuffer;
	key.indexHash = iaState.hasIndexBuffer ? iaState.indexHash : 0;
	if (iaState.hasIndexBuffer) {
		key.indexBufferSize = iaState.indexView.SizeInBytes;
		key.indexBufferFormat = static_cast<uint32_t>(iaState.indexView.Format);
	}
	for (const DX12IaBufferHash &buffer : iaState.vertexBuffers) {
		if (buffer.slot < key.vertexHashes.size()) {
			key.vertexHashes[buffer.slot] = buffer.hash;
			key.vertexPresent[buffer.slot] = 1;
			key.vertexBufferSizes[buffer.slot] = buffer.vertexView.SizeInBytes;
			key.vertexBufferStrides[buffer.slot] = buffer.vertexView.StrideInBytes;
		}
	}
	return key;
}

static UINT64 HashShaderOverrideNegativeCacheKey(
	const DX12ShaderOverrideNegativeCacheKey &key)
{
	UINT64 hash = 0x28cb7a8d31e7b4c5ull;
	hash = HashCombine64(hash, key.reloadGeneration);
	hash = HashCombine64(hash, key.preSkinGeneration);
	hash = HashCombine64(hash, reinterpret_cast<uintptr_t>(key.pipelineState));
	hash = HashCombine64(hash, key.vertexCount);
	hash = HashCombine64(hash, key.indexCount);
	hash = HashCombine64(hash, key.instanceCount);
	hash = HashCombine64(hash, key.firstVertex);
	hash = HashCombine64(hash, key.firstIndex);
	hash = HashCombine64(hash, key.firstInstance);
	hash = HashCombine64(hash, key.hasIndexBuffer ? 1 : 0);
	hash = HashCombine64(hash, key.indexHash);
	hash = HashCombine64(hash, key.indexBufferSize);
	hash = HashCombine64(hash, key.indexBufferFormat);
	for (size_t index = 0; index < key.vertexHashes.size(); ++index) {
		hash = HashCombine64(hash, key.vertexHashes[index]);
		hash = HashCombine64(hash, key.vertexPresent[index]);
		hash = HashCombine64(hash, key.vertexBufferSizes[index]);
		hash = HashCombine64(hash, key.vertexBufferStrides[index]);
	}
	return hash ? hash : 1;
}

static bool TryFindShaderOverrideNegativeCache(
	const DX12ShaderOverrideNegativeCacheKey &key)
{
	static thread_local UINT64 tlsGeneration = 0;
	static thread_local std::unordered_map<
		UINT64,
		DX12ShaderOverrideNegativeCacheKey> tlsCache;
	if (tlsGeneration != key.reloadGeneration) {
		tlsCache.clear();
		tlsGeneration = key.reloadGeneration;
	}
	const UINT64 hash = HashShaderOverrideNegativeCacheKey(key);
	auto it = tlsCache.find(hash);
	return it != tlsCache.end() && it->second == key;
}

static void StoreShaderOverrideNegativeCache(
	const DX12ShaderOverrideNegativeCacheKey &key)
{
	static thread_local UINT64 tlsGeneration = 0;
	static thread_local std::unordered_map<
		UINT64,
		DX12ShaderOverrideNegativeCacheKey> tlsCache;
	if (tlsGeneration != key.reloadGeneration) {
		tlsCache.clear();
		tlsGeneration = key.reloadGeneration;
	}
	const UINT64 hash = HashShaderOverrideNegativeCacheKey(key);
	auto existing = tlsCache.find(hash);
	if (existing == tlsCache.end() &&
	    tlsCache.size() >= MaxShaderOverrideNegativeCacheEntries) {
		tlsCache.clear();
	}
	tlsCache[hash] = key;
}

static void ClearShaderOverrideNegativeCache()
{
	InterlockedIncrement64(
		reinterpret_cast<volatile LONG64*>(&gShaderOverrideNegativeCacheGeneration));
}

static bool TryFindCachedShaderOverridesForPsoLocked(
	ID3D12PipelineState *pipelineState, bool dispatch,
	std::vector<const Bunny::ShaderOverrideConfig*> *configs)
{
	if (!configs)
		return true;
	configs->clear();
	if (!pipelineState)
		return true;

	auto cached = gShaderOverridePsoMatchCache.find(pipelineState);
	if (cached != gShaderOverridePsoMatchCache.end() &&
	    cached->second.generation == gReloadGeneration) {
		BuildShaderOverrideConfigPointers(&cached->second, dispatch, configs);
		return true;
	}
	return false;
}

// Public draw-path PSO plan lookup used before IA hash work.
// This avoids building IA hashes for PSOs with no matching shader overrides.
static bool TryFindCachedShaderOverrideIaTargetPlanForPsoLocked(
	ID3D12PipelineState *pipelineState,
	DX12IaHashTargetPlan *targetPlan)
{
	if (!targetPlan || !pipelineState)
		return false;
	std::unordered_map<ID3D12PipelineState*, DX12ShaderOverridePsoMatchCache>::iterator cached =
		gShaderOverridePsoMatchCache.find(pipelineState);
	if (cached == gShaderOverridePsoMatchCache.end() ||
	    cached->second.generation != gReloadGeneration)
		return false;
	*targetPlan = cached->second.drawTargetPlan;
	return true;
}

static bool TryFindCachedShaderOverrideDirectIaTargetPlanForPsoLocked(
	ID3D12PipelineState *pipelineState,
	DX12IaHashTargetPlan *targetPlan)
{
	if (!targetPlan || !pipelineState)
		return false;
	std::unordered_map<ID3D12PipelineState*, DX12ShaderOverridePsoMatchCache>::iterator cached =
		gShaderOverridePsoMatchCache.find(pipelineState);
	if (cached == gShaderOverridePsoMatchCache.end() ||
	    cached->second.generation != gReloadGeneration)
		return false;
	*targetPlan = cached->second.drawDirectTargetPlan;
	return true;
}

static bool TryFindCachedShaderOverrideDirectTextureChecksOnlyForPsoLocked(
	ID3D12PipelineState *pipelineState,
	bool *directTextureChecksOnly)
{
	if (!directTextureChecksOnly || !pipelineState)
		return false;
	std::unordered_map<ID3D12PipelineState*, DX12ShaderOverridePsoMatchCache>::iterator cached =
		gShaderOverridePsoMatchCache.find(pipelineState);
	if (cached == gShaderOverridePsoMatchCache.end() ||
	    cached->second.generation != gReloadGeneration)
		return false;
	*directTextureChecksOnly = cached->second.drawDirectTextureChecksOnly;
	return true;
}

static bool TryFindCachedShaderOverrideDrawMatchUsageForPsoLocked(
	ID3D12PipelineState *pipelineState,
	DX12DrawMatchUsage *drawMatchUsage)
{
	if (!drawMatchUsage || !pipelineState)
		return false;
	std::unordered_map<ID3D12PipelineState*, DX12ShaderOverridePsoMatchCache>::iterator cached =
		gShaderOverridePsoMatchCache.find(pipelineState);
	if (cached == gShaderOverridePsoMatchCache.end() ||
		cached->second.generation != gReloadGeneration)
		return false;
	*drawMatchUsage = cached->second.drawMatchUsage;
	return true;
}

bool DX12ModHasShaderOverridesForPso(ID3D12PipelineState *pipelineState)
{
	if (!pipelineState)
		return false;
	std::vector<const Bunny::ShaderOverrideConfig*> configs;
	bool cached = false;
	AcquireSRWLockShared(&gModLock);
	cached = TryFindCachedShaderOverridesForPsoLocked(pipelineState, false, &configs);
	ReleaseSRWLockShared(&gModLock);
	return !cached || !configs.empty();
}

static void FindShaderOverridesForPsoLocked(
	ID3D12PipelineState *pipelineState, bool dispatch,
	std::vector<const Bunny::ShaderOverrideConfig*> *configs)
{
	if (TryFindCachedShaderOverridesForPsoLocked(pipelineState, dispatch, configs))
		return;

	DX12PsoShaderInfo info = {};
	if (!DX12GetPipelineStateShaderInfo(pipelineState, &info))
		return;

	std::unordered_set<std::wstring> seen;
	std::unordered_set<std::wstring> regexSeen;
	DX12ShaderOverridePsoMatchCache matchCache;
	matchCache.generation = gReloadGeneration;
	if (dispatch) {
		if (info.hasCS) {
			AppendShaderOverrideForHashLocked(info.cs, &matchCache.dispatchConfigs, &seen);
			AppendShaderRegexForShaderLocked(
				info.cs, info.csModel,
				&matchCache.regexDispatchConfigs, &regexSeen);
		}
		DX12ShaderOverridePsoMatchCache &stored =
			gShaderOverridePsoMatchCache[pipelineState];
		stored = std::move(matchCache);
		BuildShaderOverrideConfigPointers(&stored, dispatch, configs);
		return;
	}

	if (info.hasVS) {
		AppendShaderOverrideForHashLocked(info.vs, &matchCache.drawConfigs, &seen);
		AppendShaderRegexForShaderLocked(
			info.vs, info.vsModel,
			&matchCache.regexDrawConfigs, &regexSeen);
	}
	if (info.hasPS) {
		AppendShaderOverrideForHashLocked(info.ps, &matchCache.drawConfigs, &seen);
		AppendShaderRegexForShaderLocked(
			info.ps, info.psModel,
			&matchCache.regexDrawConfigs, &regexSeen);
	}
	bool drawDirectHasNonTextureEffect = false;
	matchCache.drawDirectTargetPlan =
		DX12ShaderOverrideIaTargetPlanUtils::BuildDrawPlanLocked(
			matchCache,
			false,
			&drawDirectHasNonTextureEffect);
	std::vector<const Bunny::ShaderOverrideConfig*> drawUsageConfigs =
		matchCache.drawConfigs;
	for (Bunny::ShaderOverrideConfig &config : matchCache.regexDrawConfigs)
		drawUsageConfigs.push_back(&config);
	matchCache.drawMatchUsage = BuildDrawMatchUsageLocked(drawUsageConfigs);
	matchCache.drawDirectTextureChecksOnly = !drawDirectHasNonTextureEffect;
	// Keep the draw-decision miss path focused on the first-step CTO targets.
	// Nested TextureOverride CTOs are now executed by the compiled command-list op
	// stream on actual matches, so globally unioning every reachable TO target
	// here only inflates descriptor/IA requirements for negative draws.
	matchCache.drawTargetPlan = matchCache.drawDirectTargetPlan;
	gShaderOverridePsoMatchCache[pipelineState] = std::move(matchCache);
	BuildShaderOverrideConfigPointers(
		&gShaderOverridePsoMatchCache[pipelineState], dispatch, configs);
}

bool DX12ModCollectShaderOverrideIaHashTargets(
	ID3D12PipelineState *pipelineState,
	DX12IaHashTargetPlan *targetPlan)
{
	if (!targetPlan)
		return false;
	targetPlan->RequireAll();
	if (!pipelineState || !DX12ModHasActiveShaderOverrides())
		return false;

	AcquireSRWLockShared(&gModLock);
	const bool cachedPlan =
		TryFindCachedShaderOverrideIaTargetPlanForPsoLocked(
			pipelineState, targetPlan);
	ReleaseSRWLockShared(&gModLock);
	if (cachedPlan)
		return true;

	std::vector<const Bunny::ShaderOverrideConfig*> configs;
	AcquireSRWLockExclusive(&gModLock);
	FindShaderOverridesForPsoLocked(pipelineState, false, &configs);
	const bool builtPlan =
		TryFindCachedShaderOverrideIaTargetPlanForPsoLocked(
			pipelineState, targetPlan);
	ReleaseSRWLockExclusive(&gModLock);
	return builtPlan;
}

bool DX12ModCompileDrawDecision(
	ID3D12PipelineState *pipelineState,
	DX12CompiledDrawDecision *decision)
{
	if (!decision)
		return false;

	const bool activeShaderOverrides = DX12ModHasActiveShaderOverrides();
	const UINT64 reloadGeneration = DX12ModGetReloadGeneration();
	if (!pipelineState || !activeShaderOverrides) {
		decision->SetNoopNeverMatches(
			pipelineState, reloadGeneration, activeShaderOverrides);
		return true;
	}

	std::vector<const Bunny::ShaderOverrideConfig*> configs;
	DX12IaHashTargetPlan targetPlan;
	DX12IaHashTargetPlan directTargetPlan;
	targetPlan.RequireAll();
	directTargetPlan.RequireAll();
	bool cached = false;
	bool compiledMetadataHit = false;
	bool targetPlanResolved = false;
	bool directTargetPlanResolved = false;
	bool directTextureChecksOnly = false;
	DX12DrawMatchUsage drawMatchUsage;
	DX12DrawMatchUsage directDrawMatchUsage;

	ClearPsoOverridesIfFormatChanged();
	compiledMetadataHit = TryLoadCompiledPsoDrawDecisionMetadata(
		pipelineState,
		reloadGeneration,
		&targetPlan,
		&directTargetPlan,
		&drawMatchUsage,
		&directDrawMatchUsage,
		&directTextureChecksOnly);
	if (compiledMetadataHit) {
		targetPlanResolved = true;
		directTargetPlanResolved = true;
		cached = true;
	}

	if (!compiledMetadataHit) {
		AcquireSRWLockShared(&gModLock);
		cached = TryFindCachedShaderOverridesForPsoLocked(
			pipelineState, false, &configs);
		if (cached) {
			targetPlanResolved =
				TryFindCachedShaderOverrideIaTargetPlanForPsoLocked(
					pipelineState, &targetPlan);
			directTargetPlanResolved =
				TryFindCachedShaderOverrideDirectIaTargetPlanForPsoLocked(
					pipelineState, &directTargetPlan);
			TryFindCachedShaderOverrideDirectTextureChecksOnlyForPsoLocked(
				pipelineState, &directTextureChecksOnly);
			TryFindCachedShaderOverrideDrawMatchUsageForPsoLocked(
				pipelineState, &drawMatchUsage);
		}
		ReleaseSRWLockShared(&gModLock);
	}

	if (!cached) {
		AcquireSRWLockExclusive(&gModLock);
		FindShaderOverridesForPsoLocked(pipelineState, false, &configs);
		cached = TryFindCachedShaderOverridesForPsoLocked(
			pipelineState, false, &configs);
		if (cached) {
			targetPlanResolved =
				TryFindCachedShaderOverrideIaTargetPlanForPsoLocked(
					pipelineState, &targetPlan);
			directTargetPlanResolved =
				TryFindCachedShaderOverrideDirectIaTargetPlanForPsoLocked(
					pipelineState, &directTargetPlan);
			TryFindCachedShaderOverrideDirectTextureChecksOnlyForPsoLocked(
				pipelineState, &directTextureChecksOnly);
			TryFindCachedShaderOverrideDrawMatchUsageForPsoLocked(
				pipelineState, &drawMatchUsage);
		}
		ReleaseSRWLockExclusive(&gModLock);
	}

	const bool shaderOverrides = compiledMetadataHit ? true : !configs.empty();
	if (!compiledMetadataHit) {
		if (!cached)
			drawMatchUsage = BuildDrawMatchUsageLocked(configs);
		directDrawMatchUsage = drawMatchUsage;
	}
	if (!targetPlanResolved) {
		// Only PSOs that actually matched override configs need the conservative
		// RequireAll plan.  Unmatched PSOs get an empty plan so the decision can
		// become NoopNeverMatches and fast-reject every draw on this PSO.
		if (shaderOverrides)
			targetPlan.RequireAll();
		else
			targetPlan.ResetKnown();
	}
	if (!directTargetPlanResolved) {
		if (shaderOverrides)
			directTargetPlan.RequireAll();
		else
			directTargetPlan.ResetKnown();
	}

	bool skipDraw = false;
	bool skipDispatch = false;
	DX12ModGetPipelineStateSkipFlags(
		pipelineState, nullptr, &skipDraw, &skipDispatch);
	const bool shaderOverrideSkip = skipDraw;
	const bool descriptorTrackingNeeded =
		DX12ModNeedsShaderDescriptorTracking();
	decision->SetFromTargetPlan(
		pipelineState, reloadGeneration, activeShaderOverrides,
		shaderOverrides, shaderOverrideSkip, targetPlan, directTargetPlan,
		drawMatchUsage, directDrawMatchUsage,
		directTextureChecksOnly,
		descriptorTrackingNeeded);
	// Plan step 3.1: when every descriptor-relevant hashed texture override is
	// an unconditional static replacement handled at descriptor write time,
	// this decision's descriptor targets are already satisfied by the patched
	// descriptors, so the draw path can skip SRV binding resolution entirely.
	decision->descriptorTargetsStatic =
		DX12StaticTextureReplacementAllDescriptorOverridesStatic();
	return true;
}

bool DX12ModHasAnyDirectIaTextureOverrideCandidate(
	const DX12IaHashState &iaState,
	const DX12IaHashTargetPlan &targetPlan,
	bool indexed,
	uint32_t vertexCount,
	uint32_t indexCount,
	uint32_t instanceCount,
	uint32_t firstVertex,
	uint32_t firstIndex,
	uint32_t firstInstance)
{
	if (!targetPlan.hasTextureOverrideChecks ||
	    targetPlan.hasDescriptorTargets ||
	    targetPlan.requireAllIaBuffers ||
	    !targetPlan.HasIaTargets()) {
		return true;
	}

	Bunny::CommandListTarget target;
	target.stage = Bunny::CommandListShaderStage::Unknown;
	if (targetPlan.RequiresIndexBuffer(indexed) &&
	    iaState.hasIndexBuffer) {
		target.kind = Bunny::CommandListTargetKind::IndexBuffer;
		target.slot = 0;
		// Query the real hash first: hash-triggered configs (the common case)
		// either match here or miss both times, so ordering the wildcard
		// (hash==0) probe after the real one saves one full lookup+snapshot
		// cycle per draw for every config that does match. OR semantics make
		// the order irrelevant for the result.
		if (FindIaTextureOverridesCached(
			    target,
			    iaState.indexHash,
			    vertexCount,
			    indexCount,
			    instanceCount,
			    firstVertex,
				    firstIndex,
				    firstInstance,
				    true,
				    0,
				    nullptr) ||
			FindIaTextureOverridesCached(
			    target,
			    0,
			    vertexCount,
			    indexCount,
			    instanceCount,
			    firstVertex,
				firstIndex,
				firstInstance,
				true,
				0,
				nullptr)) {
				return true;
			}
		}

	for (const DX12IaBufferHash &buffer : iaState.vertexBuffers) {
		if (!targetPlan.RequiresVertexBuffer(buffer.slot))
			continue;

		target.kind = Bunny::CommandListTargetKind::VertexBuffer;
		target.slot = buffer.slot;
		if (FindIaTextureOverridesCached(
			    target,
			    buffer.hash,
			    vertexCount,
			    indexCount,
			    instanceCount,
			    firstVertex,
				firstIndex,
				firstInstance,
				false,
				buffer.slot,
				nullptr) ||
			FindIaTextureOverridesCached(
			    target,
			    0,
			    vertexCount,
			    indexCount,
			    instanceCount,
			    firstVertex,
				    firstIndex,
				    firstInstance,
				    false,
				    buffer.slot,
				    nullptr)) {
				return true;
			}
	}
	return false;
}

bool DX12ModHasAnyDirectDescriptorTextureOverrideCandidate(
	ID3D12GraphicsCommandList *commandList,
	const DX12IaHashTargetPlan &targetPlan,
	uint32_t vertexCount,
	uint32_t indexCount,
	uint32_t instanceCount,
	uint32_t firstVertex,
	uint32_t firstIndex,
	uint32_t firstInstance)
{
	if (!commandList ||
	    !targetPlan.hasTextureOverrideChecks ||
	    !targetPlan.hasDescriptorTargets ||
	    targetPlan.descriptorTargets.empty()) {
		tlsDirectDescriptorPrefilterCache.Clear();
		return false;
	}

	const UINT64 reloadGeneration = DX12ModGetReloadGenerationFast();
	DX12SimulateContext *runtimeState =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	const UINT64 descriptorQuerySerial =
		runtimeState && runtimeState->graphicsDescriptorQuerySerial ?
			runtimeState->graphicsDescriptorQuerySerial : 1;
	ID3D12DescriptorHeap *descriptorHeap =
		runtimeState ? runtimeState->cbvSrvUavHeap : nullptr;
	const UINT64 descriptorHeapVersion =
		DX12GetDescriptorHeapVersion(descriptorHeap);
	const UINT64 textureHashGeneration = DX12GetTextureHashGeneration();
	const UINT64 targetPlanFingerprint = targetPlan.Fingerprint();
	tlsDirectDescriptorPrefilterCache.SetInvocation(
		reloadGeneration,
		commandList,
		runtimeState,
		descriptorQuerySerial,
		descriptorHeap,
		descriptorHeapVersion,
		textureHashGeneration,
		targetPlanFingerprint,
		vertexCount,
		indexCount,
		instanceCount,
		firstVertex,
		firstIndex,
		firstInstance);
	if (!tlsDirectDescriptorPrefilterCache.matchesByTarget.empty() ||
		!tlsDirectDescriptorPrefilterCache.hasCandidate) {
		const bool cacheMatches = tlsDirectDescriptorPrefilterCache.MatchesInvocation(
			reloadGeneration,
			commandList,
			runtimeState,
			descriptorQuerySerial,
			descriptorHeap,
			descriptorHeapVersion,
			textureHashGeneration,
			targetPlanFingerprint,
			vertexCount,
			indexCount,
			instanceCount,
			firstVertex,
			firstIndex,
			firstInstance);
		if (cacheMatches &&
			(!tlsDirectDescriptorPrefilterCache.matchesByTarget.empty() ||
			 tlsDirectDescriptorPrefilterCache.hasCandidate)) {
			return tlsDirectDescriptorPrefilterCache.hasCandidate;
		}
	}

	const std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> snapshot =
		LoadTextureOverrideRuntimeSnapshot();
	if (!snapshot ||
		!HasPotentialResolvedDescriptorPrefilterCandidates(
			*snapshot,
			vertexCount,
			indexCount)) {
		return false;
	}

	static thread_local std::vector<uint32_t> tlsAllowedSectionIds;
	CollectResolvedDescriptorPrefilterSectionIds(
		*snapshot,
		vertexCount,
		indexCount,
		&tlsAllowedSectionIds);
	if (tlsAllowedSectionIds.empty())
		return false;

	ID3D12Device *cachedDevice = runtimeState ? runtimeState->device : nullptr;
	std::unordered_map<
		DX12TextureOverrideTargetCacheKey,
		std::vector<DX12TextureOverrideMatch>,
		DX12TextureOverrideTargetCacheKeyHash> candidateMatchesByTarget;
	std::vector<DX12CurrentShaderResourceBinding> resolvedBindings[2][3];
	std::vector<DX12ResolvedGraphicsSrvBinding> hashedBindings[2][3];
	bool resolvedBindingsLoaded[2][3] = {};
	bool resolvedBindingsFound[2][3] = {};
	bool graphicsRootDependenciesKnown = runtimeState != nullptr;
	std::array<bool, MaxRootParameters> graphicsRootSeen = {};
	std::vector<DX12TextureOverrideMatch> matches;
	bool hasCandidate = false;
	for (const DX12IaHashTargetPlan::DescriptorTarget &descriptorTarget :
	     targetPlan.descriptorTargets) {
		Bunny::CommandListTarget target;
		target.kind = static_cast<Bunny::CommandListTargetKind>(
			descriptorTarget.kind);
		target.stage = static_cast<Bunny::CommandListShaderStage>(
			descriptorTarget.stage);
		target.slot = descriptorTarget.slot;
		const bool compute = TargetStageIsCompute(target.stage);
		const D3D12_DESCRIPTOR_RANGE_TYPE rangeType =
			DescriptorRangeTypeForTarget(target.kind);
		int rangeIndex = -1;
		switch (rangeType) {
		case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
			rangeIndex = 0;
			break;
		case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
			rangeIndex = 1;
			break;
		case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
			rangeIndex = 2;
			break;
		default:
			continue;
		}
		const int stageIndex = compute ? 1 : 0;
		if (!resolvedBindingsLoaded[stageIndex][rangeIndex]) {
			resolvedBindingsLoaded[stageIndex][rangeIndex] = true;
			// SRV-range graphics targets: reuse the cached resolution when the
			// root tables and observed descriptor slots are unchanged. The
			// cached result is PSO/draw-argument independent - the compiled
			// target plan is applied below, per draw.
			const bool srvGraphicsCacheable =
				!compute && rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
			if (srvGraphicsCacheable &&
			    PrefilterSrvCacheTryLoad(
				    commandList,
				    runtimeState,
				    stageIndex,
				    reloadGeneration,
				    &hashedBindings[stageIndex][rangeIndex])) {
				resolvedBindingsFound[stageIndex][rangeIndex] =
					!hashedBindings[stageIndex][rangeIndex].empty();
				if (resolvedBindingsFound[stageIndex][rangeIndex]) {
					for (const DX12ResolvedGraphicsSrvBinding &resolved :
					     hashedBindings[stageIndex][rangeIndex]) {
						if (resolved.binding.rootParameterIndex >=
						    graphicsRootSeen.size()) {
							graphicsRootDependenciesKnown = false;
							break;
						}
						graphicsRootSeen[
							resolved.binding.rootParameterIndex] = true;
					}
					AppendResolvedBindingsToDescriptorTextureOverrideCache(
						snapshot,
						hashedBindings[stageIndex][rangeIndex],
						target.kind,
						compute,
						tlsAllowedSectionIds,
						&candidateMatchesByTarget);
				} else {
					graphicsRootDependenciesKnown = false;
				}
			} else {
				resolvedBindingsFound[stageIndex][rangeIndex] =
					DX12BindingTracker::GetCurrentShaderResourceBindings(
						commandList,
						compute,
						rangeType,
						&resolvedBindings[stageIndex][rangeIndex]) &&
					!resolvedBindings[stageIndex][rangeIndex].empty();
				if (resolvedBindingsFound[stageIndex][rangeIndex]) {
					if (compute) {
						graphicsRootDependenciesKnown = false;
					} else {
						for (const DX12CurrentShaderResourceBinding &binding :
						     resolvedBindings[stageIndex][rangeIndex]) {
							if (binding.rootParameterIndex >= graphicsRootSeen.size()) {
								graphicsRootDependenciesKnown = false;
								break;
							}
							graphicsRootSeen[binding.rootParameterIndex] = true;
						}
					}
					ResolveGraphicsSrvBindings(
						commandList,
						cachedDevice,
						resolvedBindings[stageIndex][rangeIndex],
						&hashedBindings[stageIndex][rangeIndex]);
					if (srvGraphicsCacheable) {
						PrefilterSrvCacheStore(
							commandList,
							runtimeState,
							stageIndex,
							reloadGeneration,
							resolvedBindings[stageIndex][rangeIndex],
							hashedBindings[stageIndex][rangeIndex]);
					}
					AppendResolvedBindingsToDescriptorTextureOverrideCache(
						snapshot,
						hashedBindings[stageIndex][rangeIndex],
						target.kind,
						compute,
						tlsAllowedSectionIds,
						&candidateMatchesByTarget);
				} else {
					graphicsRootDependenciesKnown = false;
				}
			}
		}
		if (!resolvedBindingsFound[stageIndex][rangeIndex])
			continue;
		const DX12TextureOverrideTargetCacheKey cacheKey = {
			static_cast<uint32_t>(target.kind),
			static_cast<uint32_t>(target.stage),
			target.slot
		};
		std::unordered_map<
			DX12TextureOverrideTargetCacheKey,
			std::vector<DX12TextureOverrideMatch>,
			DX12TextureOverrideTargetCacheKeyHash>::iterator matchIt =
			candidateMatchesByTarget.find(cacheKey);
		if (matchIt == candidateMatchesByTarget.end()) {
			tlsDirectDescriptorPrefilterCache.matchesByTarget.emplace(
				cacheKey,
				std::vector<DX12TextureOverrideMatch>());
			continue;
		}
		if (FilterDescriptorTextureOverrideCandidates(
				matchIt->second,
				vertexCount,
				indexCount,
				instanceCount,
				firstVertex,
				firstIndex,
				firstInstance,
				&matches)) {
			tlsDirectDescriptorPrefilterCache.matchesByTarget[cacheKey] = matches;
				tlsDirectDescriptorPrefilterCache.hasCandidate = true;
				hasCandidate = true;
				break;
		}
		tlsDirectDescriptorPrefilterCache.matchesByTarget[cacheKey] = matches;
	}

	tlsDirectDescriptorPrefilterCache.StoreGraphicsRootDependencies(
		runtimeState,
		graphicsRootDependenciesKnown,
		graphicsRootSeen);
	return hasCandidate;
}

static void BackfillCompiledPsoPlanFromFallback(
	ID3D12PipelineState *pipelineState,
	UINT64 reloadGeneration,
	const std::vector<Bunny::ShaderOverrideConfig> &configSnapshot)
{
	if (!pipelineState || configSnapshot.empty() ||
		DX12ModGetReloadGenerationFast() != reloadGeneration) {
		return;
	}

	AcquireSRWLockShared(&gModLock);
	if (DX12ModGetReloadGenerationFast() == reloadGeneration) {
		bool drawDirectHasNonTextureEffect = false;
		DX12IaHashTargetPlan drawDirectTargetPlan =
			DX12ShaderOverrideIaTargetPlanUtils::BuildDrawPlanLocked(
				configSnapshot,
				false,
				&drawDirectHasNonTextureEffect);
		DX12IaHashTargetPlan drawTargetPlan = drawDirectTargetPlan;
		DX12DrawMatchUsage drawMatchUsage =
			BuildDrawMatchUsageLocked(configSnapshot);
		DX12DrawMatchUsage directDrawMatchUsage = drawMatchUsage;
		const bool drawDirectTextureChecksOnly =
			!drawDirectHasNonTextureEffect;
		StoreCompiledPsoPlan(
			pipelineState,
			reloadGeneration,
			configSnapshot,
			&drawTargetPlan,
			&drawDirectTargetPlan,
			&drawMatchUsage,
			&directDrawMatchUsage,
			&drawDirectTextureChecksOnly);
	}
	ReleaseSRWLockShared(&gModLock);
}

bool DX12ModPrepareShaderOverrideReplacement(
	ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState,
	bool hasActiveShaderOverrides,
	const DX12IaHashState &iaState,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance,
	const DX12DrawMatchUsage &drawMatchUsage,
	DX12ModIaReplacement *replacement,
	bool allowDescriptorIndependentNegativeCache,
	ID3D12Device *cachedDevice,
	bool skipDirectDescriptorTargets,
	std::shared_ptr<const DX12ModIaReplacement> *generatedStaticReplacement)
{
	if (!replacement)
		return false;
	if (generatedStaticReplacement)
		generatedStaticReplacement->reset();
	if (!commandList || !pipelineState || !hasActiveShaderOverrides)
		return false;

	const UINT64 reloadGeneration = DX12ModGetReloadGenerationFast();

	const bool negativeCacheEnabled = ShaderOverrideNegativeCacheEnabled(
		allowDescriptorIndependentNegativeCache);
	DX12ShaderOverrideNegativeCacheKey negativeCacheKey = {};
	if (negativeCacheEnabled) {
		negativeCacheKey = MakeShaderOverrideNegativeCacheKey(
			pipelineState, iaState,
			vertexCount, indexCount, instanceCount,
			firstVertex, firstIndex, firstInstance,
			drawMatchUsage);
		if (TryFindShaderOverrideNegativeCache(negativeCacheKey)) {
			return false;
		}
	}

	// Try the precompiled PSO plan first (zero-lock, zero-discovery).
	// Eliminates per-draw lock+lookup+copy for PSOs recorded after mod loading.
	DX12ShaderOverrideConfigView configView;
	std::shared_ptr<const std::vector<Bunny::ShaderOverrideConfig>> compiledSnapshot;
	std::shared_ptr<const std::vector<DX12CompiledShaderOverridePlan>> compiledPlanSnapshot;
	bool alwaysMiss = false;
	bool compiledPlanHit = TryLoadCompiledPsoPlan(
		pipelineState, reloadGeneration,
		&compiledSnapshot, &compiledPlanSnapshot, &alwaysMiss);
	if (compiledPlanHit && alwaysMiss)
		return false;
	if (compiledPlanHit) {
		configView.SetOwnedSnapshot(compiledSnapshot);
		if (configView.Get().empty())
			compiledPlanHit = false;
	}
	bool cached = compiledPlanHit;

	const bool fallbackPlanResolved = !compiledPlanHit;
	if (!compiledPlanHit) {
		// Fallback: legacy PSO config cache lookup (first draw after mod reload).
		std::vector<const Bunny::ShaderOverrideConfig*> configs;
		AcquireSRWLockShared(&gModLock);
		cached = TryFindCachedShaderOverridesForPsoLocked(pipelineState, false, &configs);
		if (cached)
			CopyShaderOverrideConfigs(configs, configView.MutableFallbackSnapshot());
		ReleaseSRWLockShared(&gModLock);

		if (cached && configView.Get().empty())
			return false;

		if (!cached) {
			AcquireSRWLockExclusive(&gModLock);
			FindShaderOverridesForPsoLocked(pipelineState, false, &configs);
			CopyShaderOverrideConfigs(configs, configView.MutableFallbackSnapshot());
			ReleaseSRWLockExclusive(&gModLock);
		}
		if (configView.Get().empty())
			return false;
	}
	const std::vector<Bunny::ShaderOverrideConfig> &configSnapshot = configView.Get();
	if (fallbackPlanResolved) {
		BackfillCompiledPsoPlanFromFallback(
			pipelineState,
			reloadGeneration,
			configSnapshot);
	}

	ID3D12Device *device = AcquireModDeviceFast(commandList, cachedDevice);
	const bool deviceFromCache = (cachedDevice != nullptr);
	if (!device)
		return false;
	std::vector<DX12PendingResourceViewRequest> pendingResourceViews;
	DX12CommandListExecutor executor(
		device, commandList, iaState,
		vertexCount, indexCount, instanceCount,
		firstVertex, firstIndex, firstInstance,
		replacement, &pendingResourceViews);
	executor.SetExecutionMode(true, true);
	executor.SetSkipDirectDescriptorTargets(skipDirectDescriptorTargets);
	executor.AdoptDirectDescriptorPrefilterCache();
	if (compiledPlanSnapshot && !compiledPlanSnapshot->empty()) {
		executor.CollectDescriptorCheckTargetsFromCompiledPlans(
			*compiledPlanSnapshot);
		if (!executor.mDescriptorTargetScratch.empty()) {
			executor.PrimeDescriptorTextureOverrideCacheForTargets(
				&executor.mDescriptorTargetScratch);
		}
		for (const DX12CompiledShaderOverridePlan &plan : *compiledPlanSnapshot)
			executor.RunCompiledShaderOverridePlan(plan);
	} else {
		for (const Bunny::ShaderOverrideConfig &config : configSnapshot)
			executor.RunShaderOverride(config);
	}
	const bool resourcesChanged = ApplyPendingResourceViewsForCommandList(
		device, commandList, iaState, pendingResourceViews, replacement);
	if (resourcesChanged)
		executor.MarkExternalChange();
		const bool changed = executor.Changed() || ReplacementHasAnyEffect(*replacement);
	if (!deviceFromCache)
		device->Release();

	if (ReplacementIsStaticTemplateCandidate(
			executor,
			pendingResourceViews,
			*replacement)) {
		StoreStaticReplacementForPso(
			pipelineState,
			reloadGeneration,
			*replacement);
		if (generatedStaticReplacement) {
			*generatedStaticReplacement =
				std::make_shared<const DX12ModIaReplacement>(*replacement);
		}
	}

	const bool negativeCacheStoreSafe = negativeCacheEnabled && !changed &&
		!skipDirectDescriptorTargets &&
		pendingResourceViews.empty() && !executor.observedDescriptorTarget &&
		!executor.observedDynamicCommandState;
	if (negativeCacheStoreSafe) {
		StoreShaderOverrideNegativeCache(negativeCacheKey);
	} else if (negativeCacheEnabled && !changed && executor.observedDescriptorTarget) {
	} else if (negativeCacheEnabled && !changed && executor.observedDynamicCommandState) {
	}

	return changed;
}

void DX12ModRunPostShaderOverrideReplacement(
	ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState,
	const DX12IaHashState &iaState,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance,
	DX12ModIaReplacement *replacement)
{
	if (!replacement || !commandList || !pipelineState || !DX12ModHasActiveShaderOverrides())
		return;
	if (replacement->postCommandLists.empty() &&
	    replacement->queuedPostTextureOverrides.empty())
		return;

	ID3D12Device *device = AcquireModDevice(commandList);
	if (!device)
		return;

	std::vector<DX12PendingResourceViewRequest> pendingResourceViews;
	DX12CommandListExecutor executor(
		device, commandList, iaState,
		vertexCount, indexCount, instanceCount,
		firstVertex, firstIndex, firstInstance,
		replacement, &pendingResourceViews);
	executor.SetExecutionMode(true, true);
	for (const std::wstring &list : replacement->postCommandLists)
		executor.RunCommandListLinks(Bunny::CommandListLinks{ {}, { list }, {} }, true);
	for (const DX12ModIaReplacement::QueuedTextureOverridePost &queued :
	     replacement->queuedPostTextureOverrides)
		executor.RunQueuedPostTextureOverride(queued);
	const bool resourcesChanged = ApplyPendingResourceViewsForCommandList(
		device, commandList, iaState, pendingResourceViews, replacement);
	if (resourcesChanged)
		executor.MarkExternalChange();
	device->Release();
}

static bool ShaderOverrideHasSkipLocked(uint64_t hash)
{
	auto it = gShaderOverrides.find(hash);
	return it != gShaderOverrides.end() && it->second.handlingSkip;
}

static bool ShaderBytecodeHasSkipLocked(const D3D12_SHADER_BYTECODE &bytecode)
{
	if (!bytecode.pShaderBytecode || !bytecode.BytecodeLength)
		return false;
	return ShaderOverrideHasSkipLocked(
		DX12ModHashShaderBytecode(bytecode.pShaderBytecode, bytecode.BytecodeLength));
}


static void UpdateStoredPsoSkipLocked(DX12StoredPso *record)
{
	if (!record || record->skipGeneration == gReloadGeneration)
		return;

	record->skipDraw = false;
	record->skipDispatch = false;
	if (record->kind == DX12PsoKind::Compute) {
		record->skipDispatch = ShaderBytecodeHasSkipLocked(record->computeDesc.CS);
	} else {
		record->skipDraw =
			ShaderBytecodeHasSkipLocked(record->graphicsDesc.VS) ||
			ShaderBytecodeHasSkipLocked(record->graphicsDesc.PS);
	}
	record->skipGeneration = gReloadGeneration;
}

bool DX12ModGetPipelineStateSkipFlags(
	ID3D12PipelineState *pipelineState,
	const DX12PsoShaderInfo *shaderInfo,
	bool *skipDraw,
	bool *skipDispatch)
{
	if (skipDraw)
		*skipDraw = false;
	if (skipDispatch)
		*skipDispatch = false;
	if (!pipelineState || !DX12ModHasActiveShaderOverrides())
		return false;

	AcquireSRWLockShared(&gModLock);
	auto record = gPsoRecords.find(pipelineState);
	if (record != gPsoRecords.end()) {
		if (record->second.skipGeneration == gReloadGeneration) {
			if (skipDraw)
				*skipDraw = record->second.skipDraw;
			if (skipDispatch)
				*skipDispatch = record->second.skipDispatch;
			ReleaseSRWLockShared(&gModLock);
			return true;
		}
	}
	ReleaseSRWLockShared(&gModLock);

	AcquireSRWLockExclusive(&gModLock);
	record = gPsoRecords.find(pipelineState);
	if (record != gPsoRecords.end()) {
		UpdateStoredPsoSkipLocked(&record->second);
		if (skipDraw)
			*skipDraw = record->second.skipDraw;
		if (skipDispatch)
			*skipDispatch = record->second.skipDispatch;
		ReleaseSRWLockExclusive(&gModLock);
		return true;
	}
	ReleaseSRWLockExclusive(&gModLock);

	DX12PsoShaderInfo queriedInfo = {};
	if (!shaderInfo) {
		if (!DX12GetPipelineStateShaderInfo(pipelineState, &queriedInfo))
			return false;
		shaderInfo = &queriedInfo;
	}

	AcquireSRWLockShared(&gModLock);
	if (skipDispatch)
		*skipDispatch = shaderInfo->hasCS && ShaderOverrideHasSkipLocked(shaderInfo->cs);
	if (skipDraw)
		*skipDraw = (shaderInfo->hasVS && ShaderOverrideHasSkipLocked(shaderInfo->vs)) ||
			(shaderInfo->hasPS && ShaderOverrideHasSkipLocked(shaderInfo->ps));
	ReleaseSRWLockShared(&gModLock);
	return true;
}

bool DX12ModShouldSkipPipelineState(ID3D12PipelineState *pipelineState, bool dispatch)
{
	bool skipDraw = false;
	bool skipDispatch = false;
	if (!DX12ModGetPipelineStateSkipFlags(
			pipelineState, nullptr, &skipDraw, &skipDispatch))
		return false;
	return dispatch ? skipDispatch : skipDraw;
}

UINT64 DX12ModGetReloadGeneration()
{
	AcquireSRWLockShared(&gModLock);
	UINT64 generation = gReloadGeneration;
	ReleaseSRWLockShared(&gModLock);
	return generation;
}

UINT64 DX12ModGetReloadGenerationFast()
{
	// gReloadGeneration is volatile; an aligned 64-bit load is atomic on x64.
	// This runs on every Draw/Dispatch resident fast-reject, so avoid the
	// LOCK CMPXCHG that a read-modify-write helper would cost.
	return gReloadGeneration;
}

void DX12ModRecordGraphicsPipelineState(
	ID3D12Device *device, ID3D12PipelineState *pipelineState,
	const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc)
{
	if (!device || !pipelineState || !desc)
		return;

	DX12StoredPso record;
	record.kind = DX12PsoKind::Graphics;
	record.device = device;
	record.device->AddRef();
	DeepCopyGraphicsDesc(desc, &record);

	AcquireSRWLockExclusive(&gModLock);
	auto existing = gPsoRecords.find(pipelineState);
	if (existing != gPsoRecords.end())
		ReleaseStoredPso(&existing->second);

	// Eagerly compile PSO configuration and store the precompiled plan.
	// Eliminates per-draw lock+lookup+copy for subsequent draws.
	if (DX12ModHasActiveShaderOverrides()) {
		std::vector<const Bunny::ShaderOverrideConfig*> configs;
		FindShaderOverridesForPsoLocked(pipelineState, false, &configs);
		std::vector<Bunny::ShaderOverrideConfig> snapshot;
		CopyShaderOverrideConfigs(configs, &snapshot);
		bool drawDirectHasNonTextureEffect = false;
		DX12IaHashTargetPlan drawDirectTargetPlan =
			DX12ShaderOverrideIaTargetPlanUtils::BuildDrawPlanLocked(
				snapshot,
				false,
				&drawDirectHasNonTextureEffect);
		DX12IaHashTargetPlan drawTargetPlan = drawDirectTargetPlan;
		DX12DrawMatchUsage drawMatchUsage = BuildDrawMatchUsageLocked(snapshot);
		DX12DrawMatchUsage directDrawMatchUsage = drawMatchUsage;
		const bool drawDirectTextureChecksOnly = !drawDirectHasNonTextureEffect;
		StoreCompiledPsoPlan(
			pipelineState,
			gReloadGeneration,
			snapshot,
			&drawTargetPlan,
			&drawDirectTargetPlan,
			&drawMatchUsage,
			&directDrawMatchUsage,
			&drawDirectTextureChecksOnly);
	}

	gPsoRecords[pipelineState] = record;
	ReleaseSRWLockExclusive(&gModLock);
}

void DX12ModRecordComputePipelineState(
	ID3D12Device *device, ID3D12PipelineState *pipelineState,
	const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc)
{
	if (!device || !pipelineState || !desc)
		return;

	DX12StoredPso record;
	record.kind = DX12PsoKind::Compute;
	record.device = device;
	record.device->AddRef();
	DeepCopyComputeDesc(desc, &record);

	AcquireSRWLockExclusive(&gModLock);
	auto existing = gPsoRecords.find(pipelineState);
	if (existing != gPsoRecords.end())
		ReleaseStoredPso(&existing->second);

	// Eagerly compile configuration for compute pipelines as well.
	if (DX12ModHasActiveShaderOverrides()) {
		std::vector<const Bunny::ShaderOverrideConfig*> configs;
		FindShaderOverridesForPsoLocked(pipelineState, true, &configs);
		std::vector<Bunny::ShaderOverrideConfig> snapshot;
		CopyShaderOverrideConfigs(configs, &snapshot);
		StoreCompiledPsoPlan(
			pipelineState,
			gReloadGeneration,
			snapshot,
			nullptr,
			nullptr,
			nullptr,
			nullptr,
			nullptr);
	}

	gPsoRecords[pipelineState] = record;
	ReleaseSRWLockExclusive(&gModLock);
}

static bool HasShaderOverrideLocked(uint64_t hash)
{
	return gShaderOverrides.find(hash) != gShaderOverrides.end();
}

static bool GraphicsPsoNeedsReplacementLocked(const D3D12_GRAPHICS_PIPELINE_STATE_DESC &desc)
{
	if (desc.VS.pShaderBytecode && desc.VS.BytecodeLength &&
	    HasShaderOverrideLocked(DX12ModHashShaderBytecode(desc.VS.pShaderBytecode, desc.VS.BytecodeLength)))
		return true;
	if (desc.PS.pShaderBytecode && desc.PS.BytecodeLength &&
	    HasShaderOverrideLocked(DX12ModHashShaderBytecode(desc.PS.pShaderBytecode, desc.PS.BytecodeLength)))
		return true;
	return false;
}

static bool ComputePsoNeedsReplacementLocked(const D3D12_COMPUTE_PIPELINE_STATE_DESC &desc)
{
	return desc.CS.pShaderBytecode && desc.CS.BytecodeLength &&
		HasShaderOverrideLocked(DX12ModHashShaderBytecode(desc.CS.pShaderBytecode, desc.CS.BytecodeLength));
}

static ID3D12PipelineState *CreateGraphicsReplacement(DX12StoredPso *record)
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = record->graphicsDesc;
	D3D12_SHADER_BYTECODE vs = {};
	D3D12_SHADER_BYTECODE ps = {};
	std::vector<unsigned char> vsBytes;
	std::vector<unsigned char> psBytes;
	bool changed = false;

	if (DX12ModReplaceShaderBytecode("vs", desc.VS, &vs, &vsBytes)) {
		desc.VS = vs;
		changed = true;
	}
	if (DX12ModReplaceShaderBytecode("ps", desc.PS, &ps, &psBytes)) {
		desc.PS = ps;
		changed = true;
	}
	if (!changed)
		return nullptr;

	ID3D12PipelineState *replacement = nullptr;
	HRESULT hr = DX12DeviceHooks::CreateGraphicsPipelineStateOriginal(
		record->device, &desc, IID_PPV_ARGS(&replacement));
	return SUCCEEDED(hr) ? replacement : nullptr;
}

static ID3D12PipelineState *CreateComputeReplacement(DX12StoredPso *record)
{
	D3D12_COMPUTE_PIPELINE_STATE_DESC desc = record->computeDesc;
	D3D12_SHADER_BYTECODE cs = {};
	std::vector<unsigned char> csBytes;
	if (!DX12ModReplaceShaderBytecode("cs", desc.CS, &cs, &csBytes))
		return nullptr;
	desc.CS = cs;

	ID3D12PipelineState *replacement = nullptr;
	HRESULT hr = DX12DeviceHooks::CreateComputePipelineStateOriginal(
		record->device, &desc, IID_PPV_ARGS(&replacement));
	return SUCCEEDED(hr) ? replacement : nullptr;
}

ID3D12PipelineState *DX12ModGetReplacementPipelineState(ID3D12PipelineState *pipelineState)
{
	if (!pipelineState || !DX12ModHasActiveShaderOverrides())
		return nullptr;

	DX12StoredPso createRecord;
	bool shouldCreate = false;
	UINT64 generation = 0;

	AcquireSRWLockShared(&gModLock);
	std::unordered_map<ID3D12PipelineState*, DX12StoredPso>::iterator cached =
		gPsoRecords.find(pipelineState);
	if (cached == gPsoRecords.end()) {
		ReleaseSRWLockShared(&gModLock);
		return nullptr;
	}
	generation = gReloadGeneration;
	if (cached->second.replacement && cached->second.replacementGeneration == generation) {
		ID3D12PipelineState *replacement = cached->second.replacement;
		ReleaseSRWLockShared(&gModLock);
		return replacement;
	}
	if (cached->second.replacementNoopGeneration == generation) {
		ReleaseSRWLockShared(&gModLock);
		return nullptr;
	}
	ReleaseSRWLockShared(&gModLock);

	AcquireSRWLockExclusive(&gModLock);
	std::unordered_map<ID3D12PipelineState*, DX12StoredPso>::iterator it =
		gPsoRecords.find(pipelineState);
	if (it == gPsoRecords.end()) {
		ReleaseSRWLockExclusive(&gModLock);
		return nullptr;
	}

	DX12StoredPso &record = it->second;
	generation = gReloadGeneration;
	if (record.replacement && record.replacementGeneration == generation) {
		ID3D12PipelineState *replacement = record.replacement;
		ReleaseSRWLockExclusive(&gModLock);
		return replacement;
	}
	if (record.replacementNoopGeneration == generation) {
		ReleaseSRWLockExclusive(&gModLock);
		return nullptr;
	}

	if (record.replacement) {
		record.replacement->Release();
		record.replacement = nullptr;
		record.replacementGeneration = 0;
	}

	bool needsReplacement = record.kind == DX12PsoKind::Graphics ?
		GraphicsPsoNeedsReplacementLocked(record.graphicsDesc) :
		ComputePsoNeedsReplacementLocked(record.computeDesc);
	if (!needsReplacement) {
		record.replacementNoopGeneration = generation;
		ReleaseSRWLockExclusive(&gModLock);
		return nullptr;
	}
	record.replacementNoopGeneration = 0;

	createRecord.kind = record.kind;
	createRecord.device = record.device;
	createRecord.graphicsRootSignature = record.graphicsRootSignature;
	createRecord.computeRootSignature = record.computeRootSignature;
	createRecord.graphicsDesc = record.graphicsDesc;
	createRecord.computeDesc = record.computeDesc;
	createRecord.vsBytecode = record.vsBytecode;
	createRecord.psBytecode = record.psBytecode;
	createRecord.dsBytecode = record.dsBytecode;
	createRecord.hsBytecode = record.hsBytecode;
	createRecord.gsBytecode = record.gsBytecode;
	createRecord.csBytecode = record.csBytecode;
	if (!createRecord.vsBytecode.empty())
		createRecord.graphicsDesc.VS.pShaderBytecode = createRecord.vsBytecode.data();
	if (!createRecord.psBytecode.empty())
		createRecord.graphicsDesc.PS.pShaderBytecode = createRecord.psBytecode.data();
	if (!createRecord.dsBytecode.empty())
		createRecord.graphicsDesc.DS.pShaderBytecode = createRecord.dsBytecode.data();
	if (!createRecord.hsBytecode.empty())
		createRecord.graphicsDesc.HS.pShaderBytecode = createRecord.hsBytecode.data();
	if (!createRecord.gsBytecode.empty())
		createRecord.graphicsDesc.GS.pShaderBytecode = createRecord.gsBytecode.data();
	if (!createRecord.csBytecode.empty())
		createRecord.computeDesc.CS.pShaderBytecode = createRecord.csBytecode.data();
	if (createRecord.device)
		createRecord.device->AddRef();
	if (createRecord.graphicsRootSignature)
		createRecord.graphicsRootSignature->AddRef();
	if (createRecord.computeRootSignature)
		createRecord.computeRootSignature->AddRef();
	shouldCreate = true;
	ReleaseSRWLockExclusive(&gModLock);

	if (!shouldCreate)
		return nullptr;

	ID3D12PipelineState *newReplacement = createRecord.kind == DX12PsoKind::Graphics ?
		CreateGraphicsReplacement(&createRecord) :
		CreateComputeReplacement(&createRecord);
	if (createRecord.device)
		createRecord.device->Release();
	if (createRecord.graphicsRootSignature)
		createRecord.graphicsRootSignature->Release();
	if (createRecord.computeRootSignature)
		createRecord.computeRootSignature->Release();
	if (!newReplacement)
		return nullptr;

	AcquireSRWLockExclusive(&gModLock);
	it = gPsoRecords.find(pipelineState);
	if (it == gPsoRecords.end() || gReloadGeneration != generation) {
		ReleaseSRWLockExclusive(&gModLock);
		newReplacement->Release();
		return nullptr;
	}
	if (it->second.replacement)
		it->second.replacement->Release();
	it->second.replacement = newReplacement;
	it->second.replacementGeneration = generation;
	ReleaseSRWLockExclusive(&gModLock);
	return newReplacement;
}


bool DX12ModApplyDrawSrvReplacement(
	ID3D12GraphicsCommandList *commandList,
	DX12ModIaReplacement *replacement,
	bool executeOriginalDraw)
{
	if (!commandList || !replacement || replacement->drawSrvPatches.empty())
		return false;
	ID3D12Device *device = AcquireModDevice(commandList);
	if (!device)
		return false;
	const bool applied = DX12DrawSrvReplacementUtils::ApplyPending(
		device, commandList, replacement, executeOriginalDraw);
	device->Release();
	return applied;
}


void DX12ModRestoreDrawSrvReplacement(
	ID3D12GraphicsCommandList *commandList,
	DX12ModIaReplacement *replacement)
{
	DX12DrawSrvReplacementUtils::Restore(commandList, replacement);
}

void DX12ModClearIaReplacementRuntimeCaches()
{
	DX12IaReplacementExecutor::ClearPsoDrawFastRejectCache();
	ClearShaderOverrideNegativeCache();
	ClearCompiledPsoPlans();
}
// PreSkin DynamicUAV output resize map.
// Maps original game-created output UAV resources to resized buffers
// when the mod vertex count exceeds the original capacity.
// The IASetVertexBuffers hook reads this map to redirect vb0 reads.

class DX12PreSkinOutputResizeEntry
{
public:
	ID3D12Resource *resizedResource = nullptr;
	UINT64 originalGpuVa = 0;
	UINT64 originalByteWidth = 0;
	UINT64 resizedByteWidth = 0;
	UINT stride = 0;
	UINT64 resizedGpuVa = 0;
};

static std::unordered_map<UINT64, DX12PreSkinOutputResizeEntry>
	gPreSkinOutputResizeMap;
using DX12PreSkinOutputResizeEntryMap =
	std::unordered_map<UINT64, DX12PreSkinOutputResizeEntry>;
static std::unordered_map<IUnknown*, DX12PreSkinOutputResizeEntryMap>
	gPreSkinOutputResizeCommandListMaps;
static SRWLOCK gPreSkinOutputResizeLock = SRWLOCK_INIT;
static std::atomic<bool> gHasPreSkinOutputResizeBindings{false};
static std::atomic<UINT64> gPreSkinOutputResizeGeneration{1};

static void BumpPreSkinOutputResizeGeneration()
{
	UINT64 current = gPreSkinOutputResizeGeneration.load(std::memory_order_relaxed);
	for (;;) {
		UINT64 next = current == UINT64_MAX ? 1 : current + 1;
		if (gPreSkinOutputResizeGeneration.compare_exchange_weak(
				current,
				next,
				std::memory_order_release,
				std::memory_order_relaxed))
			return;
	}
}

static IUnknown *PreSkinOutputResizeCommandListIdentity(
	ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return nullptr;
	IUnknown *identity = DX12CommandListRuntime::GetCommandListIdentity(commandList);
	return identity ? identity : commandList;
}

static ID3D12Resource *StorePreSkinOutputResizeEntryLocked(
	DX12PreSkinOutputResizeEntryMap *entries,
	const DX12PreSkinOutputResizeEntry &entry)
{
	if (!entries || !entry.originalGpuVa || !entry.resizedResource)
		return nullptr;
	ID3D12Resource *oldResource = nullptr;
	auto oldIt = entries->find(entry.originalGpuVa);
	if (oldIt != entries->end())
		oldResource = oldIt->second.resizedResource;
	entry.resizedResource->AddRef();
	(*entries)[entry.originalGpuVa] = entry;
	return oldResource;
}

void DX12ModRegisterPreSkinOutputResize(
	ID3D12GraphicsCommandList *commandList,
	ID3D12Resource *originalResource, ID3D12Resource *resizedResource,
	UINT64 originalByteWidth, UINT64 resizedByteWidth, UINT stride)
{
	if (!originalResource || !resizedResource)
		return;
	const UINT64 originalGpuVa = originalResource->GetGPUVirtualAddress();
	const UINT64 resizedGpuVa = resizedResource->GetGPUVirtualAddress();
	if (!originalGpuVa || !resizedGpuVa)
		return;
	if (!originalByteWidth) {
		const D3D12_RESOURCE_DESC desc = originalResource->GetDesc();
		if (desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
			originalByteWidth = desc.Width;
	}
	if (!originalByteWidth || !resizedByteWidth)
		return;
	DX12PreSkinOutputResizeEntry entry;
	entry.resizedResource = resizedResource;
	entry.originalGpuVa = originalGpuVa;
	entry.originalByteWidth = originalByteWidth;
	entry.resizedByteWidth = resizedByteWidth;
	entry.stride = stride;
	entry.resizedGpuVa = resizedGpuVa;

	// The fallback map is safe only because the resize cache keeps one stable
	// replacement for each original output resource. Producer-local entries make
	// lookup precise when dispatch and draw share a command list; consumers on a
	// different list resolve the same stable resource through the global entry.
	// Do not publish a rotating per-frame allocation here: IA bindings contain
	// only the original GPU VA and cannot identify which ring slot produced them.
	ID3D12Resource *oldGlobalResource = nullptr;
	ID3D12Resource *oldCommandListResource = nullptr;
	IUnknown *commandListIdentity =
		PreSkinOutputResizeCommandListIdentity(commandList);
	AcquireSRWLockExclusive(&gPreSkinOutputResizeLock);
	// Bump the generation only when a published mapping actually changes.
	// The resize maps are rebuilt every frame (Present clears them, each
	// matching dispatch re-registers the same stable replacement); bumping on
	// every store made RedirectVertexBufferViews' negative cache miss on
	// every matching draw and redo the full GPU-VA lookup + barriers three
	// times a frame (~2ms). A content-equal re-registration keeps the cached
	// negative valid: the redirection outcome depends only on
	// resizedGpuVa/width/stride, so identical values yield identical results.
	// Both the per-command-list and global maps are checked because the
	// resolver consults local first, then global.
	bool resizeMappingChanged = true;
	{
		auto globalIt = gPreSkinOutputResizeMap.find(entry.originalGpuVa);
		if (globalIt != gPreSkinOutputResizeMap.end()) {
			const DX12PreSkinOutputResizeEntry &old = globalIt->second;
			resizeMappingChanged =
				old.resizedGpuVa != entry.resizedGpuVa ||
				old.originalByteWidth != entry.originalByteWidth ||
				old.resizedByteWidth != entry.resizedByteWidth ||
				old.stride != entry.stride;
			if (!resizeMappingChanged && commandListIdentity) {
				auto localIt =
					gPreSkinOutputResizeCommandListMaps.find(commandListIdentity);
				if (localIt != gPreSkinOutputResizeCommandListMaps.end()) {
					auto localEntryIt = localIt->second.find(entry.originalGpuVa);
					if (localEntryIt != localIt->second.end()) {
						const DX12PreSkinOutputResizeEntry &oldLocal =
							localEntryIt->second;
						resizeMappingChanged =
							oldLocal.resizedGpuVa != entry.resizedGpuVa ||
							oldLocal.originalByteWidth != entry.originalByteWidth ||
							oldLocal.resizedByteWidth != entry.resizedByteWidth ||
							oldLocal.stride != entry.stride;
					}
				}
			}
		}
	}
	oldGlobalResource =
		StorePreSkinOutputResizeEntryLocked(&gPreSkinOutputResizeMap, entry);
	if (commandListIdentity) {
		oldCommandListResource = StorePreSkinOutputResizeEntryLocked(
			&gPreSkinOutputResizeCommandListMaps[commandListIdentity], entry);
	}
	gHasPreSkinOutputResizeBindings.store(true, std::memory_order_release);
	if (resizeMappingChanged)
		BumpPreSkinOutputResizeGeneration();
	ReleaseSRWLockExclusive(&gPreSkinOutputResizeLock);
	if (oldGlobalResource)
		oldGlobalResource->Release();
	if (oldCommandListResource)
		oldCommandListResource->Release();
}

static DX12PreSkinOutputResizeEntry *FindPreSkinOutputResizeEntryLocked(
	DX12PreSkinOutputResizeEntryMap *entries,
	UINT64 viewGpuVa,
	UINT64 *sourceOffset)
{
	if (sourceOffset)
		*sourceOffset = 0;
	if (!entries)
		return nullptr;
	auto exactIt = entries->find(viewGpuVa);
	if (exactIt != entries->end())
		return &exactIt->second;
	for (auto &item : *entries) {
		DX12PreSkinOutputResizeEntry &candidate = item.second;
		if (!candidate.originalGpuVa || !candidate.originalByteWidth ||
		    viewGpuVa < candidate.originalGpuVa)
			continue;
		const UINT64 candidateOffset = viewGpuVa - candidate.originalGpuVa;
		if (candidateOffset >= candidate.originalByteWidth)
			continue;
		if (sourceOffset)
			*sourceOffset = candidateOffset;
		return &candidate;
	}
	return nullptr;
}

bool DX12ModResolvePreSkinOutputResizeByGpuVa(
	ID3D12GraphicsCommandList *commandList,
	UINT64 viewGpuVa, DX12PreSkinOutputResizeBinding *binding)
{
	if (!viewGpuVa || !binding)
		return false;
	binding->Clear();
	IUnknown *commandListIdentity =
		PreSkinOutputResizeCommandListIdentity(commandList);
	AcquireSRWLockShared(&gPreSkinOutputResizeLock);
	DX12PreSkinOutputResizeEntry *entry = nullptr;
	UINT64 sourceOffset = 0;
	if (commandListIdentity) {
		auto local = gPreSkinOutputResizeCommandListMaps.find(commandListIdentity);
		if (local != gPreSkinOutputResizeCommandListMaps.end()) {
			entry = FindPreSkinOutputResizeEntryLocked(
				&local->second, viewGpuVa, &sourceOffset);
		}
	}
	if (!entry)
		entry = FindPreSkinOutputResizeEntryLocked(
			&gPreSkinOutputResizeMap, viewGpuVa, &sourceOffset);
	if (!entry || !entry->resizedResource || sourceOffset >= entry->resizedByteWidth) {
		ReleaseSRWLockShared(&gPreSkinOutputResizeLock);
		// A miss is expected when Present/FG occurs between producer and consumer
		// recording. Do not turn this into an immediate failure or republish a raw
		// GPU VA globally; resolve through the cache that owns both COM lifetime and
		// queue-fence completion for the stable replacement resource.
		return DX12ModResolvePreSkinOutputResizeCacheByGpuVa(
			commandList, viewGpuVa, binding);
	}
	entry->resizedResource->AddRef();
	binding->resizedResource = entry->resizedResource;
	binding->retainedResource = true;
	binding->originalGpuVa = entry->originalGpuVa;
	binding->originalByteWidth = entry->originalByteWidth;
	binding->resizedGpuVa = entry->resizedGpuVa;
	binding->resizedByteWidth = entry->resizedByteWidth;
	binding->sourceOffset = sourceOffset;
	binding->stride = entry->stride;
	binding->uavBarrierPending = false;
	binding->transitionToVertex = false;
	binding->stateBefore = D3D12_RESOURCE_STATE_COMMON;
	ReleaseSRWLockShared(&gPreSkinOutputResizeLock);
	DX12ModTrackPreSkinOutputResizeCacheUse(
		commandList, binding->resizedResource);
	return true;
}

UINT64 DX12ModGetPreSkinOutputResizeGeneration()
{
	return gPreSkinOutputResizeGeneration.load(std::memory_order_acquire);
}

bool DX12ModHasPreSkinOutputResizeBindings()
{
	return gHasPreSkinOutputResizeBindings.load(std::memory_order_acquire);
}

// Plain volatile probe: the flag only ever goes 0->1 on the first cache entry
// after a clear and is reset under the cache lock, so a stale 1 is
// conservative (the IA replay gate does a lookup and finds nothing), never a
// missed redirect. Present clears the frame-local publish map, so this
// fence-owned-cache hint is what keeps the gate trying when producer and
// consumer record on opposite sides of that boundary.
bool DX12ModHasPreSkinOutputResizeCacheEntries()
{
	return InterlockedCompareExchange(
		&gHasPreSkinOutputResizeCacheEntriesFast, 0, 0) != 0;
}

void DX12ModForgetPreSkinOutputResizesForCommandList(
	ID3D12GraphicsCommandList *commandList)
{
	DX12ModAbandonPreSkinOutputResizeCacheUses(commandList);
	IUnknown *identity = PreSkinOutputResizeCommandListIdentity(commandList);
	if (!identity)
		return;
	std::vector<ID3D12Resource*> releaseResources;
	AcquireSRWLockExclusive(&gPreSkinOutputResizeLock);
	auto found = gPreSkinOutputResizeCommandListMaps.find(identity);
	if (found != gPreSkinOutputResizeCommandListMaps.end()) {
		releaseResources.reserve(found->second.size());
		for (auto &item : found->second) {
			if (item.second.resizedResource)
				releaseResources.push_back(item.second.resizedResource);
		}
		gPreSkinOutputResizeCommandListMaps.erase(found);
		BumpPreSkinOutputResizeGeneration();
	}
	ReleaseSRWLockExclusive(&gPreSkinOutputResizeLock);
	for (ID3D12Resource *resource : releaseResources)
		resource->Release();
}

void DX12ModClearPreSkinOutputResizeMap()
{
	std::vector<ID3D12Resource*> releaseResources;
	AcquireSRWLockExclusive(&gPreSkinOutputResizeLock);
	// Plan fix G: only bump the resize generation when entries were actually
	// cleared. Present calls this every frame; unconditionally bumping made
	// the per-draw pre-skin replay gate and slot negative caches (keyed on
	// the resize generation) miss every frame even when no resize happened,
	// forcing the full GPUVA lookup chain on every matching draw.
	const bool hadContent =
		!gPreSkinOutputResizeMap.empty() || !gPreSkinOutputResizeCommandListMaps.empty();
	for (auto &item : gPreSkinOutputResizeMap) {
		if (item.second.resizedResource)
			releaseResources.push_back(item.second.resizedResource);
		item.second.resizedResource = nullptr;
	}
	for (auto &commandList : gPreSkinOutputResizeCommandListMaps) {
		for (auto &item : commandList.second) {
			if (item.second.resizedResource)
				releaseResources.push_back(item.second.resizedResource);
			item.second.resizedResource = nullptr;
		}
	}
	gPreSkinOutputResizeMap.clear();
	gPreSkinOutputResizeCommandListMaps.clear();
	gHasPreSkinOutputResizeBindings.store(false, std::memory_order_release);
	if (hadContent)
		BumpPreSkinOutputResizeGeneration();
	ReleaseSRWLockExclusive(&gPreSkinOutputResizeLock);
	for (ID3D12Resource *resource : releaseResources)
		resource->Release();
}
