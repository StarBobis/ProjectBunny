#pragma once

// DX12ModRuntimeInternal.h: shared state, types and internal declarations for the
// mod runtime implementation units under DirectX12/ModRuntime/.
// Split from DX12ModRuntime.cpp.

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
#include "DX12CustomShaderElement.h"
#include "DX12CustomShaderRegisters.h"
#include "DX12CustomShaderCompile.h"
#include "DX12CustomShaderPipeline.h"
#include "DX12CustomShaderDevice.h"
#include "DX12ResourceAliasStore.h"
#include <Windows.h>
#include <d3d12.h>


DXGI_FORMAT ParseDx12ResourceFormat(const std::wstring &format);


inline SRWLOCK gModLock = SRWLOCK_INIT;

inline bool gLoaded = false;

inline std::wstring gConfigPath;

inline std::wstring gPendingInitialConfigPath;

inline volatile LONG gDeferredInitialModLoadState = 0;

inline DWORD gDeferredInitialModLoadStartTick = 0;

inline DWORD gDeferredInitialModLoadDelayMs = 2000;

inline std::wstring gBaseDir;

inline std::wstring gShaderFixesDir;

inline Bunny::ShaderOverrideMap gShaderOverrides;

inline Bunny::ShaderRegexMap gShaderRegexes;

inline Bunny::TextureOverrideMap gTextureOverrides;

inline Bunny::ResourceMap gModResources;

inline Bunny::CommandListMap gCommandLists;

// Published by reload under gModLock: true when any [CommandList] exists.
// Frame-variable collection and key-binding VM execution are only useful
// when command lists exist, so hot-path code gates on this instead of
// touching gCommandLists itself.
inline std::atomic<bool> gHasCommandLists{ false };

inline std::vector<Bunny::MigotoKeyBindingUtils::Binding> gKeyBindings;

// Custom shader elements ([CustomShaderX] sections with stage files/render
// state) rebuilt on every load; consumed by the custom draw executor.
inline std::unordered_map<std::wstring, Bunny::DX12CustomShaderElementSpec>
	gCustomShaderElements;

using DX12CustomShaderElementMap =
	std::unordered_map<std::wstring, Bunny::DX12CustomShaderElementSpec>;

// Immutable per-generation snapshot published with the other runtime
// snapshots; recording threads read this instead of the mutable registry.
inline std::atomic<std::shared_ptr<const DX12CustomShaderElementMap>>
	gCustomShaderElementSnapshot;

// Runtime resource alias table for "ResourceX = ref ResourceY" directives
// (phase 3); cleared on reload with the other config globals.
inline Bunny::DX12ResourceAliasStore gResourceAliases;

inline std::vector<std::wstring> gVlrResourceCandidates;

inline std::unordered_map<std::wstring, bool> gCommandListRuntimeEffect;

inline std::unordered_map<std::wstring, bool> gCommandListPreRuntimeEffect;

inline std::unordered_map<std::wstring, bool> gCommandListPostRuntimeEffect;

inline std::unordered_map<std::wstring, bool> gTextureOverridePreRuntimeEffect;

inline std::unordered_map<std::wstring, bool> gTextureOverridePostRuntimeEffect;

inline std::unordered_map<std::wstring, uint32_t> gTextureOverrideSectionIds;

class DX12IaTextureOverrideCandidate
{
public:
	const Bunny::TextureOverrideConfig *config = nullptr;
	UINT64 order = 0;
	uint32_t sectionId = 0;
	bool preRuntimeEffect = true;
	bool postRuntimeEffect = true;
};

inline std::vector<DX12IaTextureOverrideCandidate> gIaTextureOverrides;

inline std::unordered_map<uint32_t, std::vector<size_t>> gIaIndexTextureOverrideIndex;

inline std::unordered_map<UINT64, std::vector<size_t>> gIaVertexTextureOverrideIndex;

inline std::unordered_map<uint32_t, std::vector<size_t>> gIaAnyVertexTextureOverrideIndex;

inline std::unordered_map<UINT64, std::vector<size_t>> gIaMergedVertexTextureOverrideIndex;

inline std::vector<size_t> gIaIndexTextureOverridePrefilterIndex;

inline std::unordered_map<uint32_t, std::vector<size_t>> gIaIndexTextureOverridePrefilterByIndexCount;

inline std::unordered_map<uint32_t, std::vector<size_t>> gIaVertexTextureOverridePrefilterIndex;

inline std::vector<size_t> gIaAnyVertexTextureOverridePrefilterIndex;

inline std::unordered_map<uint32_t, std::vector<size_t>> gIaAnyVertexTextureOverridePrefilterByVertexCount;

inline std::unordered_map<uint32_t, std::vector<size_t>> gIaMergedVertexTextureOverridePrefilterIndex;

inline std::unordered_map<UINT64, std::vector<size_t>> gIaVertexTextureOverridePrefilterByVertexCount;

inline std::unordered_map<UINT64, std::vector<size_t>> gIaMergedVertexTextureOverridePrefilterByVertexCount;


// True when any IA prefilter table is indexed by draw count. When false, the
// "does this IA state have any override candidate" answer is draw-arg
// independent, which lets the draw path cache it at IA-state granularity
// (DX11-style) instead of per draw.
inline volatile LONG gIaPrefilterCountSensitive = 0;


// The hash-level negative cache records tuples with no global candidates.
// It is draw-context independent and therefore has a higher hit rate.
// key = bits[31:0]=hash | bit[32]=indexBuffer | bits[47:33]=vertexSlot
inline std::unordered_set<UINT64> gIaTextureOverrideHashNoCandidates;

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


uint64_t HashCombine64(uint64_t hash, uint64_t value);


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


bool AppendResourceViewsForCommandList(
	ID3D12Device *device, ID3D12GraphicsCommandList *commandList, const DX12IaHashState &iaState,
	const std::wstring &indexResource,
	const std::vector<DX12CompiledVertexResourceBinding> &vertexResources,
	DX12ModIaReplacement *replacement);


bool ApplyPendingResourceViewsForCommandList(
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

inline std::unordered_map<std::wstring, DX12CompiledCommandListPlan> gCompiledCommandListPlans;


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

inline std::unordered_map<uint32_t, DX12CompiledTextureOverridePlan> gCompiledTextureOverridePlans;


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

inline std::atomic<std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot>>
	gCommandExecutionRuntimeSnapshot;

// Publish generation for the snapshot above. Bumped (release) AFTER every
// store so readers can detect a reload with a single plain atomic UINT64
// load instead of touching the atomic<shared_ptr>.
inline std::atomic<UINT64> gCommandExecutionRuntimeSnapshotGeneration{ 0 };


// Thread-local snapshot cache. MSVC's std::atomic<std::shared_ptr> load is
// NOT lock-free: it goes through an internal spinlock pool, and every copy
// of the returned shared_ptr touches the shared control block
// (InterlockedIncrement/Decrement on one global cache line). IA texture
// override queries load this snapshot several times per draw per recording
// thread, which made both a global contention point - a DX11 single-thread
// assumption carried into the DX12 port. The snapshot only changes on mod
// reload, so keep one owning reference per thread and only pay the atomic
// shared_ptr load when the publish generation changes; steady-state reads
// are one acquire UINT64 load plus a raw pointer reuse, with no spinlock
// and no refcount traffic.
inline const std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> &
CommandExecutionRuntimeSnapshotTlsRef()
{
	thread_local UINT64 tlsGeneration = 0;
	thread_local std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot>
		tlsSnapshot;

	const UINT64 published = gCommandExecutionRuntimeSnapshotGeneration.load(
		std::memory_order_acquire);
	if (published == tlsGeneration)
		return tlsSnapshot;
	// Reload path: the generation may be bumped again while the shared_ptr
	// load is in flight, so re-read it afterwards and retry on change to
	// keep the (generation, snapshot) pair consistent.
	for (;;) {
		const UINT64 before = gCommandExecutionRuntimeSnapshotGeneration.load(
			std::memory_order_acquire);
		std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> snapshot =
			gCommandExecutionRuntimeSnapshot.load(std::memory_order_acquire);
		const UINT64 after = gCommandExecutionRuntimeSnapshotGeneration.load(
			std::memory_order_acquire);
		if (before == after) {
			tlsSnapshot = std::move(snapshot);
			tlsGeneration = after;
			return tlsSnapshot;
		}
	}
}


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

inline std::atomic<std::shared_ptr<const DX12StaticTextureReplacementTable>>
	gStaticTextureReplacementSnapshot;

// Low-cost gate mirroring the snapshot for hot paths that must not pay the
// shared_ptr atomic load when no static replacement exists at all.
inline volatile LONG gHasStaticTextureReplacements = 0;


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

inline std::atomic<std::shared_ptr<const DX12StaticIaReplacementTable>>
	gStaticIaReplacementSnapshot;

// Low-cost gate for the IASet bind hot path.
inline volatile LONG gHasStaticIaReplacements = 0;


inline std::shared_ptr<const DX12StaticIaReplacementTable>
LoadStaticIaReplacementSnapshot()
{
	return gStaticIaReplacementSnapshot.load(std::memory_order_acquire);
}


inline std::shared_ptr<const DX12StaticTextureReplacementTable>
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

inline std::vector<DX12VertexLimitRaiseConfig> gVertexLimitRaiseConfigs;

// Written under gModLock on reload; read on the hot path per Draw/Dispatch.
// volatile keeps every read a fresh load (aligned 64-bit loads are atomic on
// x64), so readers never need a LOCK CMPXCHG just to observe the value.
inline volatile UINT64 gReloadGeneration = 1;

inline volatile UINT64 gShaderOverrideNegativeCacheGeneration = 1;

inline volatile LONG gHasVertexLimitRaiseConfigs = 0;

inline volatile LONG gHasShaderOverrides = 0;

inline volatile LONG gHasShaderRegexes = 0;

inline volatile LONG gHasTextureOverrides = 0;

inline volatile LONG gHasShaderDescriptorTextureOverrideTriggers = 0;

inline volatile LONG gHasDrawSrvTextureReplacementOverrides = 0;

inline volatile LONG gHasIaResidentStateRequirements = 0;

inline volatile LONG gHasPreSkinTextureOverrideCandidates = 0;

inline volatile LONG gHasShaderTriggeredTextureOverrides = 0;

inline volatile LONG gHasPresentRuntimeEffect = 0;

inline bool gHasPreSkinVlrWithoutMatchCs = false;

inline std::unordered_set<uint64_t> gPreSkinMatchCsHashes;


// Adaptive idle counter: after N consecutive frames with zero active pre-skin
// overrides, binding tracking can be skipped to save ~1500+ lock acquisitions/frame.
inline volatile LONG gPreSkinBindingIdleFrames = 0;

inline volatile LONG gHasActivePreSkinTextureOverridesFast = 0;

inline volatile LONG gHasConfirmedPreSkinSectionsFast = 0;

inline constexpr LONG kPreSkinBindingIdleMax = 60;

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
inline std::unordered_map<UINT64, DX12PreSkinUavMatchCacheEntry> gPreSkinUavMatchCache;

inline constexpr size_t MaxPreSkinUavMatchCacheEntries = 4096;

inline size_t gPreSkinUavMatchCacheGrowthBudget = MaxPreSkinUavMatchCacheEntries;

inline SRWLOCK gPreSkinUavMatchCacheLock = SRWLOCK_INIT;



UINT64 MakePreSkinUavMatchCacheKey(
	ID3D12GraphicsCommandList *commandList, uint64_t csHash, UINT64 bindingHash,
	UINT64 bindingDependencyFingerprint);

inline volatile LONG gDx12SafeMode = 0;

inline volatile LONG gPresentCommandListExecutedPresent = -1;

inline volatile UINT64 gPreSkinActiveGeneration = 1;

inline std::atomic<UINT64> gPreSkinSrvCacheGeneration{1};

inline volatile LONG gPreSkinDescriptorAbortCacheHitLogCount = 0;

inline volatile LONG gPreSkinMatchCsProbeLogCount = 0;

inline SRWLOCK gPreSkinMatchCsProbeLogLock = SRWLOCK_INIT;

inline std::unordered_set<uint64_t> gPreSkinMatchCsProbeKeys;

void ClearPreSkinStickyMatchCache();

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

inline std::unordered_map<ID3D12PipelineState*, DX12ShaderOverridePsoMatchCache> gShaderOverridePsoMatchCache;

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

inline std::unordered_map<
	DX12TextureOverrideLookupCacheKey,
	DX12TextureOverrideLookupCacheEntry,
	DX12TextureOverrideLookupCacheKeyHash> gTextureOverrideLookupCache;

inline constexpr size_t MaxTextureOverrideLookupCacheEntries = 32768;


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


inline std::unordered_map<UINT64, DX12ShaderOverrideNegativeCacheKey> gShaderOverrideNegativeCache;

inline SRWLOCK gShaderOverrideNegativeCacheLock = SRWLOCK_INIT;

inline constexpr size_t MaxShaderOverrideNegativeCacheEntries = 16384;


class DX12ComputeUavProducer

{

public:
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


inline SRWLOCK gPreSkinLock = SRWLOCK_INIT;

inline std::set<SIZE_T> gPatchedPreSkinDescriptors;

inline std::map<SIZE_T, std::wstring> gKnownPreSkinDescriptorPatches;

class DX12ActivePreSkinTextureOverride
{
public:
	Bunny::TextureOverrideConfig config;
	DX12ComputeUavProducer producer;
	ID3D12Resource *outputResource = nullptr;
	UINT64 outputByteWidth = 0;
	UINT outputStride = 0;
	// Immutable output-resource facts cached when the resource identity is
	// stored. The IA view-expansion gate (DX12ModMayNeedPreSkinIaViewExpansion)
	// runs per matching draw on the render thread; without the cache it paid
	// a GetDesc + GetGPUVirtualAddress COM pair for every table entry on
	// every call.
	UINT64 outputGpuVirtualAddress = 0;
	UINT64 outputResourceWidth = 0;
	bool outputResourceFactsValid = false;
};


inline std::map<std::wstring, DX12ActivePreSkinTextureOverride> gActivePreSkinTextureOverrides;

// Plan fix C: reverse index from pre-skin output resource to the configured
// sections publishing it. DX12ModFindActivePreSkinProducerForOutput used to
// hold the lock and linearly scan the whole active table per call; the
// pre-skin IA replay path (ReplayActiveVertexBuffers) calls it once per VB
// slot per matching draw, so that scan dominated the frame cost. The index
// stores section names (not pointers) because the map entries are rewritten
// in place on every store and section names are the stable identity.
inline std::unordered_map<ID3D12Resource*, std::vector<std::wstring>> gPreSkinProducerSectionsByOutputResource;

// Snapshot of just the expanded pre-skin output VA ranges (published
// outputByteWidth exceeds the backing resource width), keyed by section.
// DX12ModMayNeedPreSkinIaViewExpansion runs per matching draw on the render
// thread; probing this handful of ranges under a shared read replaces the
// old locked full-table scan. Maintained under gPreSkinLock by the
// store/move/clear paths in DX12PreSkinRuntime.cpp; the fast flag is only a
// conservative hint (1 means "worth taking the lock and scanning").
class DX12PreSkinExpandedOutputRange
{
public:
	UINT64 gpuVirtualAddress = 0;
	UINT64 width = 0;
};
inline std::unordered_map<std::wstring, DX12PreSkinExpandedOutputRange> gPreSkinExpandedOutputRangesBySection;
inline LONG gHasPreSkinExpandedOutputRangesFast = 0;

// A successful producer match proves that this configured section participates
// in PreSkin for the current mod configuration. Keep that proof independently
// from the frame-local resource binding below: DX12 command lists may be recorded
// out of execution order, and Present/FG/capture can run before a consumer draw is
// recorded. Clearing this set at Present would make that draw fail match_cs and
// intermittently remove the entire replacement model.
inline std::unordered_set<std::wstring> gConfirmedPreSkinSections;

class DX12ActivePreSkinSectionSnapshotTlsCache
{
public:
	UINT64 generation = 0;
	std::unordered_set<std::wstring> sections;
};


UINT64 CurrentPreSkinActiveGeneration();


UINT64 DX12ModGetPreSkinActiveGenerationFast();


const std::unordered_set<std::wstring> &GetActivePreSkinSectionsSnapshot(
	UINT64 *generationOut = nullptr);



void BumpPreSkinActiveGeneration();


void MoveActivePreSkinTextureOverrideResourceLocked(
	const std::wstring &section, DX12ActivePreSkinTextureOverride *active,
	std::vector<ID3D12Resource*> *releaseResources);


void ReleaseMovedPreSkinResources(std::vector<ID3D12Resource*> *resources);


void ClearActivePreSkinTextureOverridesLocked(
	std::vector<ID3D12Resource*> *releaseResources);


void StoreActivePreSkinTextureOverrideLocked(
	const std::wstring &section, const DX12ActivePreSkinTextureOverride &active,
	std::vector<ID3D12Resource*> *releaseResources);


bool DX12ModMayNeedPreSkinIaViewExpansion(
	const D3D12_VERTEX_BUFFER_VIEW *views, const bool *hasViews, UINT count);


bool DX12ModFindActivePreSkinProducerForOutput(
	ID3D12Resource *outputResource, UINT64 outputByteWidth, UINT outputStride,
	DX12ModPreSkinProducerSummary *summary);



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


inline std::map<DX12PreSkinCbvPatchCacheKey, DX12PreSkinCbvPatchCacheEntry> gPreSkinCbvPatchCache;

inline UINT64 gPreSkinCbvPatchCacheGeneration = 0;


inline std::set<UINT64> gPreSkinSrvNegativeCache;

inline std::set<UINT64> gPreSkinSrvStableNegativeCache;

inline std::set<UINT64> gPreSkinDescriptorPatchAbortCache;

inline SRWLOCK gPreSkinDescriptorPatchAbortLock = SRWLOCK_INIT;

inline std::map<UINT64, DX12PreSkinSrvPositiveCacheValue> gPreSkinSrvPositiveCache;

inline std::map<UINT64, DX12PreSkinSrvPositiveCacheValue> gPreSkinSrvStablePositiveCache;

inline SRWLOCK gPreSkinSrvProbeCacheLock = SRWLOCK_INIT;


UINT64 DescriptorViewSize(const DX12DescriptorSummary &descriptor);

uint32_t HashComputeBufferBinding(const DX12CurrentComputeUavBinding &binding);

uint64_t HashComputeCbvListForProbe(
	const std::vector<DX12CurrentComputeUavBinding> &cbvs);


UINT64 HashWideString(UINT64 key, const std::wstring &text);


UINT64 HashComputeBindingLayout(
	UINT64 key, const DX12CurrentComputeUavBinding &binding, bool includeDescriptorHash);


uint64_t HashComputeCbvLayoutList(
	const std::vector<DX12CurrentComputeUavBinding> &cbvs);


UINT64 MakePreSkinDescriptorPatchAbortCacheKey(
	const Bunny::TextureOverrideConfig &config,
	const std::vector<DX12PreSkinSrvReplacementBinding> &srvReplacements,
	const std::vector<DX12CurrentComputeUavBinding> &srvs,
	const std::vector<DX12CurrentComputeUavBinding> &cbvs);


void StorePreSkinDescriptorPatchAbort(UINT64 key);


UINT64 MakePreSkinSrvStableProbeCacheKey(
	const DX12CurrentComputeUavBinding &srv,
	const Bunny::TextureOverrideConfig &config,
	UINT64 srvByteWidth);


UINT64 MakePreSkinSrvProbeCacheKey(
	const DX12CurrentComputeUavBinding &srv,
	const Bunny::TextureOverrideConfig &config,
	UINT64 srvByteWidth);


void StorePreSkinSrvProbeNegative(UINT64 negativeKey);

void StorePreSkinSrvStableProbeNegative(UINT64 stableNegativeKey, UINT64 negativeKey);

void StorePreSkinSrvProbePositive(
	UINT64 negativeKey,
	UINT64 stableNegativeKey,
	const DX12PreSkinSrvPositiveCacheValue &value);


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


inline std::vector<DX12PreSkinTextureOverrideCandidate> gPreSkinTextureOverrides;

inline std::unordered_map<uint32_t, std::vector<size_t>> gPreSkinUavHashTextureOverrideIndex;

inline std::unordered_map<uint64_t, std::vector<size_t>> gPreSkinMatchCsTextureOverrideIndex;

inline std::unordered_map<std::wstring, std::vector<std::wstring>> gExplicitMatchCsResourceSections;


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
	// Full desc of the captured texture (phase 3 "X = ref ps-tN"); zero for
	// file-loaded resources.
	D3D12_RESOURCE_DESC drawSrvResourceDesc = {};
	D3D12_RESOURCE_STATES uavState = D3D12_RESOURCE_STATE_COPY_DEST;
	std::wstring name;
	std::wstring path;
};


inline std::unordered_map<std::wstring, DX12LoadedResource> gLoadedResources;

class DX12RetiredLoadedResource

{

public:
	DX12LoadedResource resource;
	LONG retirePresent = 0;
};

inline std::vector<DX12RetiredLoadedResource> gRetiredLoadedResources;

inline constexpr LONG RetiredLoadedResourcePresentDelay = 8;

inline volatile LONG gActiveLoadedResourceSnapshots = 0;


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
	// Set when resource/outputVertexResource is a loaded mod resource whose
	// uavState bookkeeping must follow the transitions the restore records
	// (the VLR scratch output is both the dispatch target and the IA-read
	// vertex source, so no separate vertex twin exists for it).
	DX12LoadedResource *loadedReplacement = nullptr;
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


// Sharded active-replacement table (per command list). A single global map
// plus one global mutation generation meant every pre-skin apply on one
// command list invalidated the restore TLS negative cache of EVERY other
// list, forcing all recording threads back onto the global exclusive
// gPreSkinLock on their next draw/dispatch - a DX11 single-thread
// assumption carried into the port. Every access already targets exactly
// one command-list key, so the table is sharded by list pointer: each
// shard has its own SRWLOCK and mutation generation, and apply / restore /
// forget only touch the owning list's shard and bump only that shard's
// generation. Cross-list invalidation now only hits lists hashing to the
// same shard (1/64), and the contended lock is shard-local.
class DX12PreSkinReplacementShard
{
public:
	SRWLOCK lock = SRWLOCK_INIT;
	std::unordered_map<ID3D12GraphicsCommandList*, DX12PreSkinReplacementState> entries;

	// Monotonic mutation generation for this shard's map. Every insert and
	// erase bumps it, so a command-list-local TLS negative cache can prove
	// "this list has no pending restore right now" without taking the shard
	// lock on every draw while a replacement is pending anywhere (DX11 has
	// no global lock on the draw path). Recording is single-threaded per
	// command list, so a TLS cache keyed by the list pointer plus its own
	// shard's generation is exact; inserts and erases on OTHER shards no
	// longer invalidate it.
	std::atomic<UINT64> mutationGeneration{0};
};

inline DX12PreSkinReplacementShard gPreSkinReplacementShards[64];

inline DX12PreSkinReplacementShard &PreSkinReplacementShardFor(
	ID3D12GraphicsCommandList *commandList)
{
	constexpr size_t kShardCount =
		sizeof(gPreSkinReplacementShards) / sizeof(gPreSkinReplacementShards[0]);
	const UINT_PTR key = reinterpret_cast<UINT_PTR>(commandList);
	return gPreSkinReplacementShards[(key >> 4) % kShardCount];
}

// Approximate "any replacement pending anywhere" probe. Maintained as a
// count of entries across all shards (incremented on insert of a new key,
// decremented on erase); readers only test zero / non-zero, so the count
// keeps the exact old flag semantics while the table is sharded.
inline volatile LONG gHasActivePreSkinReplacement = 0;

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

inline std::deque<DX12RetiredPreSkinResource> gRetiredPreSkinResources;

// Present maintenance is called once per frame. Avoid taking the retirement
// lock when the GPU has nothing pending to reclaim.
inline volatile LONG gHasRetiredPreSkinObjectsFast = 0;

inline constexpr LONG PreSkinRetiredObjectPresentDelay = 120;


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
	// Readable extent of the original buffer as the game's consumer views see
	// it (the producer UAV view size). The fallback GPU-VA scan must only
	// redirect views inside this range: the same resource can pack other
	// streams (texcoords, normals) beyond the UAV view, and retargeting those
	// to the resized positions buffer corrupts them. Falls back to the
	// resource width (0) for preload-created entries that lack view info.
	UINT64 originalViewBytes = 0;
	UINT64 vertexGpuVa = 0;
	ID3D12DescriptorHeap *descriptorHeap = nullptr;
	D3D12_CPU_DESCRIPTOR_HANDLE descriptorCpu = {};
	std::vector<IUnknown*> pendingCommandLists;
	std::vector<CompletionPoint> completionPoints;
	UINT64 useSerial = 0;
};


inline SRWLOCK gPreSkinOutputResizeCacheLock = SRWLOCK_INIT;

inline std::map<DX12PreSkinOutputResizeCacheIdentity, DX12PreSkinOutputResizeCacheEntry>
	gPreSkinOutputResizeCache;

inline UINT64 gPreSkinOutputResizeCacheUseSerial = 0;

inline constexpr size_t PreSkinOutputResizeCacheCapacity = 32;

inline volatile LONG gHasPendingPreSkinOutputResizeSubmissionWorkFast = 0;

inline volatile LONG gHasPreSkinOutputResizeCacheEntriesFast = 0;


IUnknown *PreSkinCommandListIdentity(IUnknown *commandList);


bool IsPreSkinOutputResizeCacheEntryReusable(
	const DX12PreSkinOutputResizeCacheEntry &entry);


void TrackPreSkinOutputResizeCacheEntryUse(
	DX12PreSkinOutputResizeCacheEntry *entry,
	IUnknown *commandListIdentity);


void ReleasePreSkinOutputResizeCacheEntry(
	DX12PreSkinOutputResizeCacheEntry *entry);


void ClearPreSkinOutputResizeCache();


bool AcquirePreSkinOutputResizeCacheEntry(
	ID3D12Device *device, ID3D12GraphicsCommandList *commandList,
	ID3D12Resource *originalResource,
	UINT64 requiredByteWidth, UINT stride, UINT elementCount,
	UINT64 originalViewBytes,
	ID3D12Resource **resizedResourceOut,
	ID3D12Resource **vertexResourceOut,
	ID3D12DescriptorHeap **descriptorHeapOut,
	D3D12_CPU_DESCRIPTOR_HANDLE *descriptorCpuOut,
	bool *cacheHitOut, UINT *ringSlotOut);


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


inline SRWLOCK gPreSkinDescriptorRingLock = SRWLOCK_INIT;

inline DX12PreSkinDescriptorRing gPreSkinDescriptorRing;

inline std::deque<DX12RetiredPreSkinDescriptorHeap> gRetiredPreSkinDescriptorHeaps;

inline constexpr UINT PreSkinDescriptorRingInitialCapacity = 4096;

inline constexpr UINT PreSkinDescriptorRingMaximumCapacity = 1000000;


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


inline SRWLOCK gPreSkinRetireLock = SRWLOCK_INIT;

inline std::unordered_map<IUnknown*, DX12PreSkinPendingSubmission> gPendingPreSkinSubmissions;

inline volatile LONG gHasPendingPreSkinSubmissionWorkFast = 0;

// Recycling bookkeeping for the pre-skin descriptor ring (guarded by
// gPreSkinRetireLock). The ring's transient mirrors only have to survive
// until every command list that recorded against them has been submitted
// AND the GPU has consumed that submission; once both hold, a heap's
// offsets can be rewound instead of growing the ring to its million-
// descriptor cap and rebuilding a shader-visible heap every few seconds.
//
// gPreSkinRetainedHeapListCounts: how many unsubmitted command-list buckets
// still reference each heap (incremented on retain, decremented when the
// bucket leaves the pending table at submission or list teardown).
// gPreSkinHeapCompletionPoints: queue-fence points of the submissions that
// drained those buckets; a point is complete when its fence value passed
// (present-count fallback when no fence could be signaled).
class DX12PreSkinHeapCompletionPoint
{
public:
	ID3D12Fence *fence = nullptr;
	UINT64 value = 0;
	LONG submittedPresent = 0;
};

inline std::unordered_map<ID3D12DescriptorHeap*, LONG> gPreSkinRetainedHeapListCounts;

inline std::unordered_map<ID3D12DescriptorHeap*, std::vector<DX12PreSkinHeapCompletionPoint>>
	gPreSkinHeapCompletionPoints;
inline SRWLOCK gPreSkinDescriptorRestoreHeapPoolLock = SRWLOCK_INIT;

inline std::vector<DX12PooledPreSkinDescriptorRestoreHeap> gPreSkinDescriptorRestoreHeapPool;

inline constexpr size_t PreSkinDescriptorRestoreHeapPoolCapacity = 64;


void PublishPendingPreSkinSubmissionWorkLocked();


bool AcquirePreSkinDescriptorRestoreHeap(
	ID3D12Device *device,
	ID3D12DescriptorHeap **heapOut,
	ID3D12Device **retainedDeviceOut);


void RecyclePreSkinDescriptorRestoreHeap(
	ID3D12Device *device,
	ID3D12DescriptorHeap *heap);


void DrainPreSkinDescriptorRestoreHeapPool(
	DX12PreSkinReleaseBatch *releaseBatch);


void ReleasePreSkinDescriptorRestores(
	std::vector<DX12PreSkinReplacementState::DescriptorRestore> *restores);


void ReleasePreSkinReleaseBatch(DX12PreSkinReleaseBatch *batch);


void MovePendingPreSkinSubmission(
	DX12PreSkinPendingSubmission *source,
	DX12PreSkinPendingSubmission *target);


void MovePendingPreSkinSubmissionForCommandListLocked(
	IUnknown *identity,
	DX12PreSkinPendingSubmission *target);


void MovePendingPreSkinSubmissionToReleaseBatch(
	DX12PreSkinPendingSubmission *source,
	DX12PreSkinReleaseBatch *batch);


void CleanupRetiredPreSkinObjectsLocked(
	DX12PreSkinReleaseBatch *releaseBatch);


IUnknown *PreSkinCommandListIdentity(IUnknown *commandList);


bool DX12ModResolvePreSkinOutputResizeCacheByGpuVa(
	ID3D12GraphicsCommandList *commandList,
	UINT64 viewGpuVa, DX12PreSkinOutputResizeBinding *binding);


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


inline SRWLOCK gPreSkinOutputResizeQueueFenceLock = SRWLOCK_INIT;

inline std::unordered_map<ID3D12CommandQueue*, DX12PreSkinOutputResizeQueueFence>
	gPreSkinOutputResizeQueueFences;


bool AcquirePreSkinOutputResizeSubmissionFence(
	ID3D12CommandQueue *queue, ID3D12Fence **fenceOut, UINT64 *valueOut);


// Returns one caller-owned completion fence when requested. Retirement can
// share this signal with the resize cache, keeping submission bookkeeping to
// one queue Signal per ExecuteCommandLists.
bool MarkPreSkinOutputResizeCacheUsesSubmitted(
	ID3D12CommandQueue *queue,
	UINT numCommandLists,
	ID3D12CommandList *const *commandLists,
	ID3D12Fence **completionFenceOut = nullptr,
	UINT64 *completionValueOut = nullptr);


void DX12ModAbandonPreSkinOutputResizeCacheUses(
	ID3D12GraphicsCommandList *commandList);


void DX12ModTrackPreSkinOutputResizeCacheUse(
	ID3D12GraphicsCommandList *commandList,
	ID3D12Resource *vertexResource);



void RetirePreSkinResource(ID3D12Resource *resource);


void RetirePreSkinResourceForCommandList(
	ID3D12GraphicsCommandList *commandList, ID3D12Resource *resource);



void RetirePreSkinDescriptorHeap(ID3D12DescriptorHeap *heap);

// Ring-heap recycling helpers (defined in DX12PreSkinRuntime.cpp); the
// bundle mirror sessions adopt retired heaps through the same drain checks.
bool IsPreSkinRingHeapReusableLocked(
	ID3D12DescriptorHeap *heap, UINT minCapacity);
void ClearPreSkinHeapCompletionPointsLocked(
	ID3D12DescriptorHeap *heap, std::vector<ID3D12Fence*> *fencesOut);


void RetainPreSkinDescriptorHeapForCommandList(
	ID3D12GraphicsCommandList *commandList, ID3D12DescriptorHeap *heap);


void DX12ModNotifyCommandListsSubmitted(
	ID3D12CommandQueue *queue, UINT numCommandLists, ID3D12CommandList *const *commandLists);


bool DX12ModNeedsCommandListSubmissionNotifications();


void DX12ModRunPresentMaintenance();


void ReleasePreSkinDescriptorRingLocked(DX12PreSkinReleaseBatch *releaseBatch);


bool EnsurePreSkinDescriptorRingLocked(
	ID3D12Device *device, ID3D12GraphicsCommandList *commandList, UINT requiredDescriptors,
	D3D12_CPU_DESCRIPTOR_HANDLE *cpuBase,
	D3D12_GPU_DESCRIPTOR_HANDLE *gpuBase,
	UINT *increment,
	ID3D12DescriptorHeap **heapOut);


enum class DX12PsoKind
{
	Graphics,
	Compute
};


void ProcessDx12KeyBindingsForFrame();


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


inline std::unordered_map<ID3D12PipelineState*, DX12StoredPso> gPsoRecords;


void ReleaseLoadedResourcesLocked();

bool ResourceConfigLooksLikeBuffer(const Bunny::ResourceConfig &config);


std::vector<DX12VertexLimitRaiseConfig> CollectVertexLimitRaiseConfigs(
	const Bunny::TextureOverrideMap &textureOverrides);


uint64_t GetPreSkinCsHash(const Bunny::TextureOverrideConfig &config);


bool IsComputePreSkinTextureOverride(const Bunny::TextureOverrideConfig &config);


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
bool TextureOverrideIsStaticReplacement(
	const Bunny::TextureOverrideConfig &config);


inline std::shared_ptr<DX12StaticTextureReplacementTable>
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
inline std::shared_ptr<DX12StaticIaReplacementTable>
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
			// vbN=/ib= produce a ResourceAssignment action alongside
			// vertexBufferResources/indexBufferResource; those ARE the
			// replacement and must not disqualify the section (DX11 applies
			// hash + vbN=/ib= at draw time). Any other action (handling=skip,
			// draw*, dispatch, run, checks, control flow...) makes the section
			// draw-time dependent and keeps it on the dynamic path.
			const bool hasNonGeometryAction = std::any_of(
				config.actions.begin(), config.actions.end(),
				[](const Bunny::CommandListAction &action) {
					return action.kind != Bunny::CommandListActionKind::ResourceAssignment &&
						action.kind != Bunny::CommandListActionKind::VariableAssignment &&
						action.kind != Bunny::CommandListActionKind::LocalVariableAssignment &&
						action.kind != Bunny::CommandListActionKind::PostVariableAssignment;
				});
			const bool clean =
				!config.handlingSkip &&
				!config.hasMatchVertexCount && !config.hasMatchIndexCount &&
				!config.hasMatchInstanceCount && !config.hasMatchFirstVertex &&
				!config.hasMatchFirstIndex && !config.hasMatchFirstInstance &&
				!config.hasMatchUavBytes &&
				!config.hasCsHash &&
				config.preSkinMatchCsSrvHashes.empty() &&
				config.preSkinCsSrvResources.empty() &&
				!config.hasVertexLimitRaise &&
				config.commandLists.pre.empty() &&
				config.commandLists.main.empty() &&
				config.commandLists.post.empty() &&
				!hasNonGeometryAction &&
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
	DX12_LOG("DX12StaticIaReplacement",
		"enabled=%d,indexEntries=%zu,vertexEntries=%zu,blockedByShaderIaTriggers=%d",
		table->enabled ? 1 : 0,
		table->indexByHash.size(),
		table->vertexByHashAndSlot.size(),
		hasShaderIaTextureOverrideTriggers ? 1 : 0);
	return table;
}


void BuildDx12PreSkinTextureOverrideIndex(
	const Bunny::TextureOverrideMap &textureOverrides,
	std::vector<DX12PreSkinTextureOverrideCandidate> *preSkinTextureOverrides,
	std::unordered_map<uint32_t, std::vector<size_t>> *uavHashTextureOverrideIndex,
	std::unordered_map<uint64_t, std::vector<size_t>> *matchCsTextureOverrideIndex);


void BuildDx12ExplicitMatchCsResourceIndex(
	const Bunny::TextureOverrideMap &textureOverrides,
	std::unordered_map<std::wstring, std::vector<std::wstring>> *resourceSections);


bool HasPreSkinTextureOverrideCandidates(
	const Bunny::TextureOverrideMap &textureOverrides,
	std::unordered_set<uint64_t> *matchCsHashes,
	bool *hasVlrWithoutMatchCs,
	bool *hasExplicitMatchCsUavBytes);



bool ResourceBlockedByInactiveExplicitMatchCsLocked(
	const std::wstring &resourceName,
	const std::unordered_set<std::wstring> &activePreSkinSections);


void BuildDx12VlrResourceCandidates(
	const Bunny::ResourceMap &resources,
	const std::unordered_map<std::wstring, std::vector<std::wstring>> &explicitMatchCsResourceSections,
	std::vector<std::wstring> *resourceCandidates);


void ClearPreSkinRuntimeState();



bool ProducerMatchesReplacementTarget(
	const DX12ComputeUavProducer &producer, const DX12VertexLimitRaiseConfig &vlr);


void SetBasePaths(const wchar_t *configPath);


void DX12ModScheduleInitialLoad(const wchar_t *configPath);


void DX12ModMaybeRunDeferredInitialLoad();


bool ReadFileBytes(const wchar_t *path, std::vector<unsigned char> *data);


void ReleaseStoredPso(DX12StoredPso *record);


void StoreShaderBytecode(
	const D3D12_SHADER_BYTECODE &source, std::vector<unsigned char> *storage,
	D3D12_SHADER_BYTECODE *target);


void DeepCopyGraphicsDesc(
	const D3D12_GRAPHICS_PIPELINE_STATE_DESC *source, DX12StoredPso *record);


void DeepCopyComputeDesc(
	const D3D12_COMPUTE_PIPELINE_STATE_DESC *source, DX12StoredPso *record);


uint64_t DX12ModHashShaderBytecode(const void *data, size_t size);


void DX12ModSetSafeMode(bool safeMode);


bool IsVlrBufferResizeCandidate(
	const D3D12_RESOURCE_DESC &desc, const DX12VertexLimitRaiseConfig &vlr);


bool AdjustBufferResourceDescCommon(D3D12_RESOURCE_DESC *desc, const char *source);


bool DX12ModAdjustBufferResourceDesc(D3D12_RESOURCE_DESC *desc, const char *source);


bool DX12ModAdjustBufferResourceDesc1(D3D12_RESOURCE_DESC1 *desc, const char *source);


class DX12ExpandedPreSkinBufferPrivateData
{
public:
	UINT version = 1;
	UINT vertexStride = 0;
	UINT uavStride = 0;
	UINT reserved = 0;
	UINT64 outputByteWidth = 0;
};


inline const GUID DX12ExpandedPreSkinBufferPrivateDataGuid =
	{0x2ce8d27c, 0xdd3f, 0x4d84, {0xa8, 0x9d, 0x7f, 0xb7, 0xf4, 0x85, 0xe0, 0x9f}};


bool DX12ModResolveExpandedPreSkinBufferWidth(
	ID3D12Resource *resource, UINT vertexStride, UINT64 *outputByteWidth);


bool DX12ModAdjustUavDesc(
	ID3D12Resource *resource, D3D12_UNORDERED_ACCESS_VIEW_DESC *desc, const char *source);


bool CommandListActionHasRuntimeEffect(
	const Bunny::CommandListAction &action,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *effectCache,
	std::set<std::wstring> *visiting);

bool TextureOverrideMatchesIaBinding(
	const Bunny::TextureOverrideConfig &config, bool indexBuffer, uint32_t vertexSlot);

void BuildDx12IaTextureOverrideIndex(
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

void BuildDx12CompiledTextureOverridePlans(
	const Bunny::TextureOverrideMap &textureOverrides,
	const std::unordered_map<std::wstring, uint32_t> &textureOverrideSectionIds,
	const std::unordered_map<std::wstring, DX12CompiledCommandListPlan> &compiledCommandListPlans,
	std::unordered_map<uint32_t, DX12CompiledTextureOverridePlan> *compiledPlans);

void BuildDx12CompiledShaderOverridePlans(
	const Bunny::ShaderOverrideMap &shaderOverrides,
	const Bunny::ShaderRegexMap &shaderRegexes,
	const std::unordered_map<std::wstring, DX12CompiledCommandListPlan> &compiledCommandListPlans,
	std::unordered_map<std::wstring, DX12CompiledShaderOverridePlan> *compiledPlans);

void BuildDx12CompiledCommandListPlans(
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, DX12CompiledCommandListPlan> *compiledPlans);

void BuildDx12PreSkinTextureOverrideIndex(
	const Bunny::TextureOverrideMap &textureOverrides,
	std::vector<DX12PreSkinTextureOverrideCandidate> *preSkinTextureOverrides,
	std::unordered_map<uint32_t, std::vector<size_t>> *uavHashTextureOverrideIndex,
	std::unordered_map<uint64_t, std::vector<size_t>> *matchCsTextureOverrideIndex);

void BuildDx12ExplicitMatchCsResourceIndex(
	const Bunny::TextureOverrideMap &textureOverrides,
	std::unordered_map<std::wstring, std::vector<std::wstring>> *resourceSections);

inline std::shared_ptr<DX12StaticTextureReplacementTable>
BuildDx12StaticTextureReplacementTable(
	const Bunny::TextureOverrideMap &textureOverrides,
	const std::unordered_map<std::wstring, uint32_t> &textureOverrideSectionIds);

// Plan step 3.2: write-time (bind-time) static IA replacement table.
inline std::shared_ptr<DX12StaticIaReplacementTable>
BuildDx12StaticIaReplacementTable(
	const Bunny::TextureOverrideMap &textureOverrides,
	bool hasShaderIaTextureOverrideTriggers);

void BuildDx12VlrResourceCandidates(
	const Bunny::ResourceMap &resources,
	const std::unordered_map<std::wstring, std::vector<std::wstring>> &explicitMatchCsResourceSections,
	std::vector<std::wstring> *resourceCandidates);

inline std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot>
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


bool CommandListHasRuntimeEffect(
	const std::wstring &name,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *effectCache,
	std::set<std::wstring> *visiting);


bool CommandListHasTextureOverrideTrigger(
	const std::wstring &name,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *triggerCache,
	std::set<std::wstring> *visiting);


bool CommandListActionHasTextureOverrideTrigger(
	const Bunny::CommandListAction &action,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *triggerCache,
	std::set<std::wstring> *visiting);


bool CommandListActionHasDescriptorTextureOverrideTrigger(
	const Bunny::CommandListAction &action,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *triggerCache,
	std::set<std::wstring> *visiting);


bool CommandListActionHasIaTextureOverrideTrigger(
	const Bunny::CommandListAction &action,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *triggerCache,
	std::set<std::wstring> *visiting);


bool CommandListHasTextureOverrideTrigger(
	const std::wstring &name,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *triggerCache,
	std::set<std::wstring> *visiting);


bool CommandListLinksHaveTextureOverrideTrigger(
	const std::vector<std::wstring> &links,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *triggerCache);


bool TargetNeedsDescriptorBindings(const Bunny::CommandListTarget &target);


bool TargetNeedsIaBindings(const Bunny::CommandListTarget &target);


bool CommandListHasDescriptorTextureOverrideTrigger(
	const std::wstring &name,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *triggerCache,
	std::set<std::wstring> *visiting);


bool CommandListHasIaTextureOverrideTrigger(
	const std::wstring &name,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *triggerCache,
	std::set<std::wstring> *visiting);


bool CommandListActionHasDescriptorTextureOverrideTrigger(
	const Bunny::CommandListAction &action,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *triggerCache,
	std::set<std::wstring> *visiting);


bool CommandListActionHasIaTextureOverrideTrigger(
	const Bunny::CommandListAction &action,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *triggerCache,
	std::set<std::wstring> *visiting);


bool CommandListLinksHaveDescriptorTextureOverrideTrigger(
	const std::vector<std::wstring> &links,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *triggerCache);


bool CommandListLinksHaveIaTextureOverrideTrigger(
	const std::vector<std::wstring> &links,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *triggerCache);


bool ShaderOverrideHasTextureOverrideTrigger(
	const Bunny::ShaderOverrideConfig &config,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *triggerCache);


bool ShaderOverridesHaveTextureOverrideTrigger(
	const Bunny::ShaderOverrideMap &shaderOverrides,
	const Bunny::CommandListMap &commandLists);


bool ShaderOverrideHasDescriptorTextureOverrideTrigger(
	const Bunny::ShaderOverrideConfig &config,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *triggerCache);


bool ShaderOverridesHaveDescriptorTextureOverrideTrigger(
	const Bunny::ShaderOverrideMap &shaderOverrides,
	const Bunny::CommandListMap &commandLists);


bool ShaderOverrideHasIaTextureOverrideTrigger(
	const Bunny::ShaderOverrideConfig &config,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *triggerCache);


bool ShaderOverridesHaveIaTextureOverrideTrigger(
	const Bunny::ShaderOverrideMap &shaderOverrides,
	const Bunny::CommandListMap &commandLists);


bool ShaderRegexHasTextureOverrideTrigger(
	const Bunny::ShaderRegexConfig &config,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *triggerCache);


bool ShaderRegexesHaveTextureOverrideTrigger(
	const Bunny::ShaderRegexMap &shaderRegexes,
	const Bunny::CommandListMap &commandLists);


bool ShaderRegexesHaveExecutableNoPatternGroup(
	const Bunny::ShaderRegexMap &shaderRegexes);


bool ShaderRegexHasDescriptorTextureOverrideTrigger(
	const Bunny::ShaderRegexConfig &config,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *triggerCache);


bool ShaderRegexesHaveDescriptorTextureOverrideTrigger(
	const Bunny::ShaderRegexMap &shaderRegexes,
	const Bunny::CommandListMap &commandLists);


bool ShaderRegexHasIaTextureOverrideTrigger(
	const Bunny::ShaderRegexConfig &config,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *triggerCache);


bool ShaderRegexesHaveIaTextureOverrideTrigger(
	const Bunny::ShaderRegexMap &shaderRegexes,
	const Bunny::CommandListMap &commandLists);


bool TextureOverridesHaveDrawSrvReplacement(
	const Bunny::TextureOverrideMap &textureOverrides);



bool CompiledShaderOverridePlansNeedResidentIaState(
	const std::unordered_map<std::wstring, DX12CompiledShaderOverridePlan> &plans);


bool CommandListActionHasRuntimeEffect(
	const Bunny::CommandListAction &action,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *effectCache,
	std::set<std::wstring> *visiting);


bool CommandListLinksHaveRuntimeEffect(
	const std::vector<std::wstring> &links,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *effectCache);


bool TextureOverrideActionsHaveRuntimeEffect(
	const Bunny::TextureOverrideConfig &config,
	bool executeDrawActions,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *effectCache);


void BuildDx12RuntimeEffectIndexes(
	const Bunny::TextureOverrideMap &textureOverrides,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *commandListEffect,
	std::unordered_map<std::wstring, bool> *textureOverridePreEffect,
	std::unordered_map<std::wstring, bool> *textureOverridePostEffect);


bool AnyRuntimeEffectInMap(
	const std::unordered_map<std::wstring, bool> &effects);


bool CommandListHasPhaseRuntimeEffect(
	const std::wstring &name,
	bool includePost,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *effectCache,
	std::set<std::wstring> *visiting,
	bool anyTextureOverridePreEffect,
	bool anyTextureOverridePostEffect);


bool CommandListActionHasPhaseRuntimeEffect(
	const Bunny::CommandListAction &action,
	bool includePost,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *effectCache,
	std::set<std::wstring> *visiting,
	bool anyTextureOverridePreEffect,
	bool anyTextureOverridePostEffect);


bool CommandListHasPhaseRuntimeEffect(
	const std::wstring &name,
	bool includePost,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *effectCache,
	std::set<std::wstring> *visiting,
	bool anyTextureOverridePreEffect,
	bool anyTextureOverridePostEffect);


void BuildDx12CommandListPhaseRuntimeEffectIndexes(
	const Bunny::CommandListMap &commandLists,
	const std::unordered_map<std::wstring, bool> &textureOverridePreEffect,
	const std::unordered_map<std::wstring, bool> &textureOverridePostEffect,
	std::unordered_map<std::wstring, bool> *commandListPreEffect,
	std::unordered_map<std::wstring, bool> *commandListPostEffect);


bool LookupRuntimeEffect(
	const std::unordered_map<std::wstring, bool> &effects,
	const std::wstring &section);


void BuildDx12TextureOverrideSectionIds(
	const Bunny::TextureOverrideMap &textureOverrides,
	std::unordered_map<std::wstring, uint32_t> *sectionIds);


inline std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot>
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
		// Persist semantics: variables declared with a "persist" prefix in
		// [Constants] keep their runtime-modified values across a reload.
		// Values are snapshotted from the live global table before
		// ClearGlobals() (the table still holds the previous generation's
		// runtime state at this point), then replayed after the [Constants]
		// defaults so the persisted value wins over the ini default - the
		// same outcome DX11 gets from its d3dx_user.ini persistence.
		std::vector<std::wstring> persistNames;
		const Bunny::CommandListConfig *constants = FindConstants(commandLists);
		if (constants)
			CollectTopLevelPersistNames(constants, &persistNames);
		MigotoCommandVmUtils::SavePersistSnapshot(persistNames);

		MigotoCommandVmUtils::ClearGlobals();
		if (!constants)
			return;

		MigotoCommandVmUtils::State state;
		state.PushLocalScope();
		for (const Bunny::CommandListAction &action : constants->actions)
			ApplyAction(&state, action);
		state.PopLocalScope();

		MigotoCommandVmUtils::RestorePersistSnapshot();
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

	// Collects the variable names of persist declarations that appear at the
	// top level of the [Constants] section (not inside if/else blocks: those
	// declarations may not be replayed on the next load, so persisting them
	// would resurrect values the config no longer assigns).
	static void CollectTopLevelPersistNames(
		const Bunny::CommandListConfig *constants, std::vector<std::wstring> *out)
	{
		if (!constants || !out)
			return;
		int branchDepth = 0;
		for (const Bunny::CommandListAction &action : constants->actions) {
			if (action.kind == Bunny::CommandListActionKind::ControlFlow) {
				const std::wstring line = Bunny::IniDocumentText::ToLower(
					Bunny::IniDocumentText::Trim(
						action.rawLine.empty() ? action.key : action.rawLine));
				if (line.rfind(L"if ", 0) == 0)
					++branchDepth;
				else if (line == L"endif" && branchDepth > 0)
					--branchDepth;
				continue;
			}
			if (branchDepth != 0 ||
			    action.kind != Bunny::CommandListActionKind::VariableAssignment)
				continue;
			const std::wstring key = Bunny::IniDocumentText::ToLower(
				Bunny::IniDocumentText::Trim(action.key));
			if (key.rfind(L"global persist $", 0) != 0 &&
			    key.rfind(L"persist $", 0) != 0)
				continue;
			const std::wstring name = MigotoCommandVmUtils::ExtractVariableName(key);
			if (!name.empty())
				out->push_back(name);
		}
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
	bool *hasErrors);


bool DX12ModRuntimeReload();



std::wstring ResolveResourcePath(
	const Bunny::ResourceConfig &config, const std::wstring &baseDir,
	bool rootFallback);


std::wstring ResolveResourcePathLocked(
	const Bunny::ResourceConfig &config, bool rootFallback);


bool ResourceConfigLooksLikeBuffer(const Bunny::ResourceConfig &config);


bool LoadResourceBytes(
	const Bunny::ResourceConfig &config, const std::wstring &baseDir,
	std::vector<unsigned char> *bytes, std::wstring *path);


DX12LoadedResource *EnsureLoadedResourceForPreSkin(
	ID3D12Device *device, const std::wstring &name);

#include "DX12DrawTextureSrvViewUtils.h"

#include "DX12WicTextureDecodeUtils.h"

// Frame-boundary breadcrumb (see DX12CommandListExecutor.cpp). A relaxed store
// of a static string literal: no allocation, no lock, no log traffic, so it can
// be dropped into any blockable path without perturbing what it measures. The
// last marker left behind by a frozen process names the blocking call.
// DX12MarkStage itself is declared by Core/Diag/DX12Diag.h (TIMING only).

// Prints the stage the previous frame boundary died in. Called at the top of
// DX12ModBeginFrame: a frozen process stops emitting, so the last line this
// prints names the frame that never finished.
#if DX12_TIMING_BUILD
void DX12LogFrameBoundaryStage();
#else
static inline void DX12LogFrameBoundaryStage() {}
#endif

#include "DX12DdsTextureLoadUtils.h"


// Exported upload-channel state for the ExecuteCommandLists wrapper: the game
// queue waits on the upload fence (GPU-side) before its next submit instead
// of the upload caller blocking on WaitForSingleObject.
bool DX12UploadChannelHasPendingWork();


void DX12UploadChannelWaitOnQueue(ID3D12CommandQueue *queue);


ID3D12Device *AcquireModDevice(ID3D12GraphicsCommandList *commandList);


// Fast path: use pre-cached device pointer to avoid per-call COM QI overhead.
// The caller must ensure cachedDevice remains valid (device outlives command list per DX12 spec).
ID3D12Device *AcquireModDeviceFast(ID3D12GraphicsCommandList *commandList, ID3D12Device *cachedDevice);


void BuildLoadedBufferResource(
	ID3D12Device *device,
	const std::wstring &name,
	const Bunny::ResourceConfig &config,
	const std::wstring &baseDir,
	DX12ResidentResourceBuildResult *result);


bool StoreLoadedResourceResultLocked(
	const std::wstring &name, UINT64 generation, DX12LoadedResource *loadedResource,
	ID3D12Resource **discardResource);


bool EnsureLoadedResourceForCommandList(ID3D12Device *device, const std::wstring &name);


DX12LoadedResource *EnsureLoadedResourceForPreSkin(
	ID3D12Device *device, const std::wstring &name);


bool EnsureResourceUavLocked(
	ID3D12Device *device, ID3D12GraphicsCommandList *commandList,
	DX12LoadedResource *resource, UINT elementStride, UINT64 minByteWidth = 0,
	bool allowInactiveExplicitMatchCs = false,
	const std::unordered_set<std::wstring> *activePreSkinSections = nullptr);


bool EnsureResourceSrvLocked(
	ID3D12Device *device, DX12LoadedResource *resource, UINT elementStride,
	UINT64 minByteWidth = 0);



bool FindReplacementResourceNameForVlrLocked(
	const DX12VertexLimitRaiseConfig &vlr, std::wstring *resourceName);


bool ProducerMatchesAnyVlr(
	const DX12ComputeUavProducer &producer, uint64_t computeShaderHash,
	DX12VertexLimitRaiseConfig *match);


bool ResourceMatchesVlrTarget(
	const DX12LoadedResource &resource, const DX12VertexLimitRaiseConfig &vlr);


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
inline std::unordered_map<uint32_t, DX12StaticTextureReplacementResolveCacheEntry>
	gStaticTextureReplacementResolveCache;

inline SRWLOCK gStaticTextureReplacementResolveLock = SRWLOCK_INIT;


void DX12NoteStaticTextureReplacementTablePublished();


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
	UINT64 *outTableGeneration);


bool DX12StaticTextureReplacementAllDescriptorOverridesStatic();


bool DX12HasStaticTextureReplacements();


UINT64 DX12StaticTextureReplacementGeneration();


bool DX12HasStaticIaReplacements();


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
	D3D12_VERTEX_BUFFER_VIEW *outVertexView);


bool DX12StaticTextureReplacementContainsHash(uint32_t originalHash);

// Bundle draw-SRV replacement (DX12BundleSrvMirror.h, included below):
// ApplyPending/Restore branch to these on bundle lists.
inline bool DX12ModApplyDrawSrvReplacementBundle(
	ID3D12Device *device,
	ID3D12GraphicsCommandList *commandList,
	DX12ModIaReplacement *replacement,
	bool executeOriginalDraw);
inline void DX12ModRestoreDrawSrvReplacementBundle(
	ID3D12GraphicsCommandList *commandList,
	DX12ModIaReplacement *replacement);

#include "DX12DrawSrvReplacementUtils.h"

#include "DX12BundleSrvMirror.h"


UINT64 DescriptorViewSize(const DX12DescriptorSummary &descriptor);


void SyncPreSkinDescriptorHeaps(
	ID3D12GraphicsCommandList *commandList,
	DX12SimulateContext *runtimeState,
	ID3D12DescriptorHeap *cbvSrvUavHeap,
	ID3D12DescriptorHeap *samplerHeap);


void SyncPreSkinComputeRootDescriptorTable(
	ID3D12GraphicsCommandList *commandList,
	DX12SimulateContext *runtimeState,
	UINT rootParameterIndex,
	D3D12_GPU_DESCRIPTOR_HANDLE table);


UINT AlignCbvSize(UINT size);


bool ReadPreSkinCbvBytesLocked(
	ID3D12Resource *resource, UINT64 sourceOffset, UINT viewBytes,
	std::vector<unsigned char> *bytes, HRESULT *hrOut);


UINT64 HashPreSkinCbvBytes(const std::vector<unsigned char> &bytes);


void ReleasePreSkinCbvPatchCacheEntry(
	DX12PreSkinCbvPatchCacheEntry *entry);


void ClearPreSkinCbvPatchCache();


bool PatchPreSkinVertexCountCbvLocked(
	ID3D12Device *device, const DX12CurrentComputeUavBinding &cbv,
	UINT expectedOriginalVertexCount, UINT overrideVertexCount,
	D3D12_CPU_DESCRIPTOR_HANDLE dst,
	std::vector<ID3D12Resource *> *temporaryResources,
	UINT *originalVertexCountOut,
	UINT *overrideVertexCountOut);


uint32_t HashComputeBufferBinding(const DX12CurrentComputeUavBinding &binding);


uint32_t HashComputeUavBinding(const DX12CurrentComputeUavBinding &uav);



void AppendComputeBindingHashList(
	const std::vector<DX12CurrentComputeUavBinding> &bindings,
	bool srvs, char *text, size_t textSize);


uint64_t HashComputeBindingListForProbe(
	const std::vector<DX12CurrentComputeUavBinding> &bindings, bool srvs);


bool CanPatchPreSkinVertexCountCbvLocked(
	const DX12CurrentComputeUavBinding &cbv,
	UINT expectedOriginalVertexCount);

void AppendComputeCbvList(
	const std::vector<DX12CurrentComputeUavBinding> &cbvs, char *text, size_t textSize);

void AppendComputeRootConstantsList(
	const std::vector<DX12CurrentRootConstants> &rootConstants, char *text, size_t textSize);

uint64_t HashComputeCbvListForProbe(
	const std::vector<DX12CurrentComputeUavBinding> &cbvs);

uint64_t HashComputeRootConstantsForProbe(
	const std::vector<DX12CurrentRootConstants> &rootConstants);

uint64_t HashPreSkinMatchSrvKeysForProbe(
	const std::vector<DX12CurrentComputeUavBinding> &srvs,
	const Bunny::TextureOverrideConfig &config);

uint64_t MakePreSkinMatchCsInputCacheKey(
	uint64_t csHash, const DX12PreSkinTextureOverrideCandidate &candidate,
	UINT64 uavViewBytes,
	const std::vector<DX12CurrentComputeUavBinding> &uavs,
	const std::vector<DX12CurrentComputeUavBinding> &srvs,
	const std::vector<DX12CurrentComputeUavBinding> &cbvs,
	const std::vector<DX12CurrentRootConstants> &rootConstants);


void LogPreSkinMatchCsProbeLimited(
	uint64_t csHash, const DX12PreSkinTextureOverrideCandidate &candidate,
	UINT64 uavViewBytes, bool inputMatched,
	const std::vector<DX12CurrentComputeUavBinding> &uavs,
	const std::vector<DX12CurrentComputeUavBinding> &srvs,
	const std::vector<DX12CurrentComputeUavBinding> &cbvs,
	const std::vector<DX12CurrentRootConstants> &rootConstants);

void AppendComputeCbvList(
	const std::vector<DX12CurrentComputeUavBinding> &cbvs, char *text, size_t textSize);


void AppendComputeRootConstantsList(
	const std::vector<DX12CurrentRootConstants> &rootConstants, char *text, size_t textSize);


uint64_t HashComputeCbvListForProbe(
	const std::vector<DX12CurrentComputeUavBinding> &cbvs);


uint64_t HashComputeRootConstantsForProbe(
	const std::vector<DX12CurrentRootConstants> &rootConstants);


bool FindComputeSrvByRegister(
	const std::vector<DX12CurrentComputeUavBinding> &srvs,
	uint32_t shaderRegister,
	const DX12CurrentComputeUavBinding **bindingOut,
	DX12PreSkinSrvHashResult *hashOut);


uint64_t HashPreSkinMatchSrvKeysForProbe(
	const std::vector<DX12CurrentComputeUavBinding> &srvs,
	const Bunny::TextureOverrideConfig &config);


bool MatchComputeInputSrvsTextureOverrideLocked(
	const std::vector<DX12CurrentComputeUavBinding> &srvs,
	const Bunny::TextureOverrideConfig &config);


bool BuildPreSkinSrvReplacementBindingsTextureOverrideLocked(
	const std::vector<DX12CurrentComputeUavBinding> &srvs,
	const Bunny::TextureOverrideConfig &config,
	std::vector<DX12PreSkinSrvReplacementBinding> *replacements);


bool FindMatchCsTextureOverrideLocked(
	uint64_t csHash,
	const std::vector<DX12CurrentComputeUavBinding> &uavs,
	const std::vector<DX12CurrentComputeUavBinding> &srvs,
	const std::vector<DX12CurrentComputeUavBinding> &cbvs,
	const std::vector<DX12CurrentRootConstants> &rootConstants,
	DX12UavHashTextureOverrideMatch *match);


bool HasMatchCsTextureOverrideLocked(uint64_t csHash);


bool IsPreSkinMatchCsUavCandidate(const DX12CurrentComputeUavBinding &binding);


size_t FindPreSkinTextureOverrideCandidateIndex(
	const Bunny::TextureOverrideConfig *config);


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


inline constexpr LONG kPreSkinStickyMatchFrames = 8;

inline constexpr size_t kPreSkinStickyMatchSlots = 64;

inline DX12PreSkinStickyMatchEntry gPreSkinStickyMatch[kPreSkinStickyMatchSlots] = {};


void ClearPreSkinStickyMatchCache();


size_t PreSkinStickyMatchSlot(uint64_t csHash, UINT64 uavKey);


UINT64 MakePreSkinStickyMatchUavKey(
	const std::vector<DX12CurrentComputeUavBinding> &uavs);


void StorePreSkinStickyMatch(
	uint64_t csHash, UINT64 uavKey, size_t candidateIndex, uint32_t hash);


bool TryLoadPreSkinStickyMatch(
	uint64_t csHash, const std::vector<DX12CurrentComputeUavBinding> &uavs,
	DX12UavHashTextureOverrideMatch *match);


bool FindPreSkinProducerByMatchCsLocked(
	const std::vector<DX12CurrentComputeUavBinding> &uavs,
	const std::vector<DX12CurrentComputeUavBinding> &srvs,
	const std::vector<DX12CurrentComputeUavBinding> &cbvs,
	const std::vector<DX12CurrentRootConstants> &rootConstants, uint64_t csHash,
	DX12ComputeUavProducer *producer,
	DX12CurrentComputeUavBinding *binding,
	DX12UavHashTextureOverrideMatch *match);


UINT FindPreSkinUavElementStrideLocked(
	const DX12LoadedResource &resource,
	const std::unordered_set<std::wstring> &activePreSkinSections);


bool FindPreSkinProducerLocked(
	const std::vector<DX12CurrentComputeUavBinding> &uavs,
	uint64_t computeShaderHash,
	DX12ComputeUavProducer *producer,
	DX12VertexLimitRaiseConfig *vlr);


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


inline SRWLOCK gPreSkinPersistentDescriptorPatchLock = SRWLOCK_INIT;

inline std::unordered_map<
	DX12PreSkinPersistentDescriptorPatchKey,
	DX12PreSkinPersistentDescriptorPatchEntry,
	DX12PreSkinPersistentDescriptorPatchKeyHasher>
	gPreSkinPersistentDescriptorPatchCache;

inline UINT64 gPreSkinPersistentDescriptorPatchCacheGeneration = 0;

inline constexpr size_t MaxPreSkinPersistentDescriptorPatchEntries = 32;


void ClearPreSkinPersistentDescriptorPatchCacheLocked();


bool TryGetPersistentPreSkinDescriptorPatchLocked(
	UINT64 reloadGeneration,
	UINT64 bindingFingerprint,
	uint32_t triggerHash,
	size_t tableCount,
	ID3D12DescriptorHeap **heapOut,
	std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> *patchedTablesOut);


void StorePersistentPreSkinDescriptorPatchLocked(
	UINT64 reloadGeneration,
	UINT64 bindingFingerprint,
	uint32_t triggerHash,
	ID3D12DescriptorHeap *heap,
	const std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> &patchedTables);


bool TryReuseDeferredPreSkinDescriptorTable(
	ID3D12GraphicsCommandList *commandList,
	uint64_t computeShaderHash,
	UINT64 bindingFingerprint,
	uint32_t triggerHash,
	bool *hasPendingState);


bool DX12ModCanDeferPreSkinningUavReplacement(
	ID3D12GraphicsCommandList *commandList);


bool DX12ModHasActivePreSkinningUavReplacement();


bool ApplyPreSkinDescriptorTablePatchLocked(
	ID3D12Device *device, ID3D12GraphicsCommandList *commandList,
	const DX12CurrentComputeUavBinding &uavBinding, const DX12ComputeUavProducer &producer,
	const Bunny::TextureOverrideConfig &config,
	const std::vector<DX12PreSkinSrvReplacementBinding> &srvReplacements,
	const std::vector<DX12CurrentComputeUavBinding> &srvs,
	const std::vector<DX12CurrentComputeUavBinding> &cbvs,
	const std::vector<DX12CurrentRootConstants> &rootConstants,
	const wchar_t *section, uint32_t triggerHash,
	UINT *originalVertexCountOut, UINT *overrideVertexCountOut,
	UINT64 bindingFingerprint, uint64_t computeShaderHash);


bool HasExplicitPreSkinDispatchOverride(
	const Bunny::TextureOverrideConfig &config);

void FillPreSkinDispatchOverrideFromConfig(
	const Bunny::TextureOverrideConfig &config,
	DX12PreSkinDispatchOverride *dispatchOverride);

void RunPreSkinMatchVmActions(
	const Bunny::TextureOverrideConfig &config);

bool ApplyPreSkinBindingPatchLocked(
	ID3D12Device *device, ID3D12GraphicsCommandList *commandList,
	const DX12CurrentComputeUavBinding &binding, const DX12ComputeUavProducer &producer,
	DX12LoadedResource *replacement, const DX12VertexLimitRaiseConfig &vlr,
	const std::vector<DX12CurrentComputeUavBinding> &srvs,
	const std::vector<DX12CurrentComputeUavBinding> &cbvs,
	const wchar_t *section, uint32_t triggerHash,
	UINT *originalVertexCountOut, UINT *overrideVertexCountOut);



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
	UINT64 bindingDependencyFingerprint,
	bool *matchPhaseMiss);



void DX12ModPublishPreSkinOutputResizeEarly(ID3D12GraphicsCommandList *commandList);


void DX12ModRestorePreSkinningUavReplacement(ID3D12GraphicsCommandList *commandList);


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
	DX12PreSkinDispatchOverride *outDispatchOverride);


bool DX12ModReplaceShaderBytecode(
	const char *stage, const D3D12_SHADER_BYTECODE &source,
	D3D12_SHADER_BYTECODE *replacement, std::vector<unsigned char> *storage);


bool DX12ModHasShaderOverride(uint64_t hash);


bool DX12ModHasActiveShaderOverrides();


bool DX12ModNeedsShaderDescriptorTracking();


bool DX12ModHasActiveTextureOverrides();


bool DX12ModHasConfiguredTextureOverrides();


bool DX12ModHasAnyActiveOverrides();


bool DX12ModNeedsPresentReplacement();


bool DX12ModNeedsPreSkinningUavProbe();


// Returns true when pre-skin binding tracking is actively needed.
// Automatically disables after kPreSkinBindingIdleMax consecutive frames
// with zero active overrides, saving ~1500+ lock acquisitions per frame.
// Note: idle counter is reset when a matching pre-skin CS dispatch is probed.
bool DX12ModNeedsPreSkinningBindingTracking();


bool DX12ModHasActivePreSkinTextureOverrides();


UINT32 DX12ModGetHotPathFlags();


bool DX12ModShouldProbePreSkinningForCs(uint64_t computeShaderHash);


bool DX12ModShouldTrackPreSkinBindingsForCs(uint64_t computeShaderHash);


bool DX12ModPreSkinningUavProducerMayMatch(
	uint64_t computeShaderHash,
	const std::vector<DX12CurrentComputeUavBinding> &uavs);


void UpdatePreSkinningUavMatchCacheLocked(
	ID3D12GraphicsCommandList *commandList,
	uint64_t computeShaderHash,
	UINT64 computeBindingSerial,
	UINT64 bindingDependencyFingerprint,
	bool matched,
	const DX12PreSkinDispatchOverride *dispatchOverride);


void DX12ModStoreCachedPreSkinningUavMatch(
	ID3D12GraphicsCommandList *commandList,
	uint64_t computeShaderHash,
	UINT64 computeBindingSerial,
	UINT64 bindingDependencyFingerprint,
	bool matched,
	const DX12PreSkinDispatchOverride *dispatchOverride);


bool DX12ModGetCachedPreSkinProbeResult(
	ID3D12GraphicsCommandList *commandList,
	uint64_t computeShaderHash,
	UINT64 bindingHash,
	UINT64 bindingDependencyFingerprint,
DX12PreSkinProbeCacheEntry *result);


bool NumericMatchSatisfied(const Bunny::NumericMatch &match, uint32_t actual);


bool TextureOverrideMatchesDrawContext(
	const Bunny::TextureOverrideConfig &config,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance);


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


DX12TextureOverrideDrawKeyUsage BuildTriggeredDrawKeyUsage(
	const std::vector<DX12TriggeredTextureOverride> *candidateTriggers);


void AppendDescriptorTriggeredPrefilterSectionIds(
	const std::vector<DX12TriggeredTextureOverride> &source,
	std::vector<uint32_t> *sectionIds);


bool HasPotentialDescriptorTriggeredPrefilterCandidates(
	const DX12CommandExecutionRuntimeSnapshot &snapshot,
	uint32_t vertexCount,
	uint32_t indexCount);


void CollectDescriptorTriggeredPrefilterSectionIds(
	const DX12CommandExecutionRuntimeSnapshot &snapshot,
	uint32_t vertexCount,
	uint32_t indexCount,
	std::vector<uint32_t> *sectionIds);


bool TextureOverrideHasDrawKind(
	const Bunny::TextureOverrideConfig &config, Bunny::CommandListActionKind kind);


bool TextureOverrideHasDrawContextMatch(const Bunny::TextureOverrideConfig &config);


void SetCompiledVertexResourceBinding(
	std::vector<DX12CompiledVertexResourceBinding> *bindings,
	uint32_t slot,
	const std::wstring &resource);


std::vector<DX12CompiledVertexResourceBinding> CompileVertexResourceBindings(
	const std::map<uint32_t, std::wstring> &vertexResources);


void AppendCompiledDrawSrvResourceBindings(
	std::vector<DX12CompiledDrawSrvResourceBinding> *bindings,
	const std::map<uint32_t, std::wstring> &resources,
	bool pixelStage);


bool TextureOverrideMatchesIaBinding(
	const Bunny::TextureOverrideConfig &config, bool indexBuffer, uint32_t vertexSlot);


UINT64 MakeIaVertexIndexKey(uint32_t hash, uint32_t vertexSlot);


bool NumericMatchIsExactValue(const Bunny::NumericMatch &match);


void BuildDx12IaTextureOverrideIndex(
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


const std::vector<size_t> *FindIaTextureOverridePrefilterCandidatesLocked(
	bool indexBuffer, uint32_t vertexSlot, uint32_t vertexCount, uint32_t indexCount,
	std::vector<size_t> *scratch);


bool AppendActionToCompiledPlan(
	const Bunny::CommandListAction &action,
	DX12CompiledCommandListPlan *plan);


void AppendCompiledCommandListPlan(
	DX12CompiledCommandListPlan *dst, const DX12CompiledCommandListPlan &src);


bool AppendCompiledCommandListLinksPlan(
	const std::vector<std::wstring> &links,
	const std::unordered_map<std::wstring, DX12CompiledCommandListPlan> &compiledCommandListPlans,
	DX12CompiledCommandListPlan *dst);


void AppendLegacyTextureOverrideStateToPlan(
	const Bunny::TextureOverrideConfig &config,
	DX12CompiledCommandListPlan *plan);



void BuildCompiledShaderOverridePlanFromParts(
	bool handlingSkip,
	const Bunny::CommandListLinks &commandLists,
	const std::vector<Bunny::CommandListAction> &actions,
	const std::unordered_map<std::wstring, DX12CompiledCommandListPlan> &compiledCommandListPlans,
	DX12CompiledShaderOverridePlan *plan);


bool CompileCommandListPlanRecursive(
	const std::wstring &name,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, DX12CompiledCommandListPlan> *compiledPlans,
	std::unordered_set<std::wstring> *visiting,
	DX12CompiledCommandListPlan *outPlan);


void BuildDx12CompiledCommandListPlans(
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, DX12CompiledCommandListPlan> *compiledPlans);


void BuildDx12CompiledTextureOverridePlans(
	const Bunny::TextureOverrideMap &textureOverrides,
	const std::unordered_map<std::wstring, uint32_t> &textureOverrideSectionIds,
	const std::unordered_map<std::wstring, DX12CompiledCommandListPlan> &compiledCommandListPlans,
	std::unordered_map<uint32_t, DX12CompiledTextureOverridePlan> *compiledPlans);


void BuildDx12CompiledShaderOverridePlans(
	const Bunny::ShaderOverrideMap &shaderOverrides,
	const Bunny::ShaderRegexMap &shaderRegexes,
	const std::unordered_map<std::wstring, DX12CompiledCommandListPlan> &compiledCommandListPlans,
	std::unordered_map<std::wstring, DX12CompiledShaderOverridePlan> *compiledPlans);


void ReleaseLoadedResourcesLocked();


void MoveExpiredRetiredLoadedResourcesLocked(
	std::vector<DX12RetiredLoadedResource> *expired);


void ReleaseLoadedResourceObjects(DX12LoadedResource *resource);


DX12IaHashState EmptyIaHashState();

bool ApplyPendingResourceViewsForCommandList(
	ID3D12Device *device, ID3D12GraphicsCommandList *commandList,
	const DX12IaHashState &iaState,
	const std::vector<DX12PendingResourceViewRequest> &requests,
	DX12ModIaReplacement *replacement);

bool RunNamedCommandList(
	ID3D12Device *device,
	ID3D12GraphicsCommandList *commandList,
	const std::wstring &name,
	DX12ModIaReplacement *replacement,
	std::vector<DX12PendingResourceViewRequest> *pendingResourceViews);


bool TextureOverrideMatchCsSatisfied(
	const Bunny::TextureOverrideConfig &config,
	const std::unordered_set<std::wstring> &activePreSkinSections);


bool TextureOverrideDependsOnActivePreSkin(
	const Bunny::TextureOverrideConfig &config);


const std::unordered_set<std::wstring> &EmptyActivePreSkinSections();


bool IaCandidateIndexesDependOnActivePreSkin(
	const std::vector<size_t> &candidateIndexes);


bool IaTriggeredCandidatesDependOnActivePreSkin(
	const std::vector<DX12TriggeredTextureOverride> &candidateTriggers);


bool PreloadPreSkinReplacementResources(ID3D12Device *device);


bool PreloadPreSkinOutputResizeCache(ID3D12Device *device);


void DX12ModBeginFrame(IDXGISwapChain *swapChain);


bool DX12ModPreparePresentReplacement(
	ID3D12GraphicsCommandList *commandList, DX12ModIaReplacement *replacement);


bool DX12ModPresentReplacementPendingForCurrentPresent();


DX12TextureOverrideLookupCacheKey MakeIaTextureOverrideLookupCacheKey(
	const Bunny::CommandListTarget &target, uint32_t hash,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance,
	UINT64 preSkinGeneration,
	const DX12TextureOverrideDrawKeyUsage &usage);


inline const DX12CommandExecutionRuntimeSnapshot *
LoadTextureOverrideRuntimeSnapshot()
{
	// TLS-cached: a plain generation check on the hot path, the atomic
	// shared_ptr load only happens once per mod reload per thread. The
	// returned pointer stays valid until this thread's next snapshot query
	// (the TLS reference keeps the object alive).
	return CommandExecutionRuntimeSnapshotTlsRef().get();
}


void CollectIaTriggeredTextureOverrideCandidates(
	const DX12CommandExecutionRuntimeSnapshot &snapshot,
	uint32_t hash,
	bool indexBuffer,
	uint32_t vertexSlot,
	uint32_t vertexCount,
	uint32_t indexCount,
	std::vector<DX12TriggeredTextureOverride> *candidateTriggers);



bool FindIaTextureOverridesCached(
	const Bunny::CommandListTarget &target, uint32_t hash,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance,
	bool indexBuffer, uint32_t vertexSlot,
	std::vector<const Bunny::TextureOverrideConfig*> *configs);


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


DX12IaTargetTextureOverrideCacheKey MakeIaTargetTextureOverrideCacheKey(
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
	uint32_t firstInstance);


bool TryLoadIaTargetTextureOverrideCache(
	const DX12IaTargetTextureOverrideCacheKey &key,
	std::vector<DX12TextureOverrideMatch> *matches);


void StoreIaTargetTextureOverrideCache(
	const DX12IaTargetTextureOverrideCacheKey &key,
	const std::vector<DX12TextureOverrideMatch> &matches);


const DX12IaBufferHash *FindIaVertexSlot(const DX12IaHashState &iaState, uint32_t slot);


bool TargetStageIsCompute(Bunny::CommandListShaderStage stage);


D3D12_DESCRIPTOR_RANGE_TYPE DescriptorRangeTypeForTarget(
	Bunny::CommandListTargetKind kind);


bool DescriptorBindingMatchesTarget(
	const DX12CurrentShaderResourceBinding &binding,
	const Bunny::CommandListTarget &target);


uint32_t HashDescriptorBinding(
	ID3D12GraphicsCommandList *commandList,
	ID3D12Device *cachedDevice,
	const DX12CurrentShaderResourceBinding &binding);


uint32_t HashDescriptorBindingResource(const DX12CurrentShaderResourceBinding &binding);


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


bool BuildDescriptorTextureOverrideCandidateCacheKey(
	ID3D12GraphicsCommandList *commandList,
	DX12SimulateContext *runtimeState,
	const Bunny::CommandListTarget &target,
	bool compute,
	UINT64 reloadGeneration,
	uint32_t vertexCount,
	uint32_t indexCount,
	DX12DescriptorTextureOverrideCandidateCacheKey *key);


void AppendTextureOverrideCandidateMatchesByHash(
	const DX12CommandExecutionRuntimeSnapshot &snapshot,
	uint32_t hash,
	uint32_t resourceHash,
	uint32_t descriptorHash,
	const DX12CurrentShaderResourceBinding *binding,
	bool compute,
	const std::vector<uint32_t> *allowedSectionIds,
	std::vector<DX12TextureOverrideMatch> *matches);


bool FilterDescriptorTextureOverrideCandidates(
	const std::vector<DX12TextureOverrideMatch> &candidates,
	uint32_t vertexCount,
	uint32_t indexCount,
	uint32_t instanceCount,
	uint32_t firstVertex,
	uint32_t firstIndex,
	uint32_t firstInstance,
	std::vector<DX12TextureOverrideMatch> *matches);



bool HasPotentialIaTextureOverrideCandidateLocked(
	bool indexBuffer,
	uint32_t vertexSlot,
	uint32_t vertexCount,
	uint32_t indexCount,
	uint32_t instanceCount,
	uint32_t firstVertex,
	uint32_t firstIndex,
	uint32_t firstInstance,
	std::vector<size_t> *prefilterScratch = nullptr);


bool HasPotentialHashedIaTextureOverrideCandidateLocked(
	bool indexBuffer,
	uint32_t vertexSlot,
	uint32_t vertexCount,
	uint32_t indexCount);


bool DX12ModHasPotentialIaTextureOverrideTargetByCount(
	bool indexBuffer,
	uint32_t vertexSlot,
	uint32_t vertexCount,
	uint32_t indexCount);


bool DX12ModHasPotentialHashedIaTextureOverrideTargetByCount(
	bool indexBuffer,
	uint32_t vertexSlot,
	uint32_t vertexCount,
	uint32_t indexCount);



bool FindDescriptorTextureOverrides(
	ID3D12GraphicsCommandList *commandList,
	const Bunny::CommandListTarget &target,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance,
	std::vector<DX12TextureOverrideMatch> *matches,
	const std::vector<DX12CurrentShaderResourceBinding> *preResolvedBindings = nullptr);


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


void ReleaseIaResourceViewSnapshot(DX12IaResourceViewSnapshot *snapshot);


void AddRefIaResourceViewSnapshot(DX12IaResourceViewSnapshot *snapshot);


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


void SnapshotIaResourceViewLocked(
	DX12LoadedResource *resource, DX12IaResourceViewSnapshot *snapshot);


void SnapshotIaResourceViewForCommandList(
	const std::wstring &name, DX12IaResourceViewSnapshot *snapshot);


UINT IaViewByteWidthForResource(const DX12LoadedResource &resource, ID3D12Resource *iaResource);

void ApplyIaResourceBarriers(
	ID3D12GraphicsCommandList *commandList, const DX12IaResourceViewSnapshot &snapshot);


bool AppendResourceViewsForCommandList(
	ID3D12Device *device, ID3D12GraphicsCommandList *commandList, const DX12IaHashState &iaState,
	const std::wstring &indexResource,
	const std::vector<DX12CompiledVertexResourceBinding> &vertexResources,
	DX12ModIaReplacement *replacement);


bool ApplyPendingResourceViewsForCommandList(
	ID3D12Device *device, ID3D12GraphicsCommandList *commandList,
	const DX12IaHashState &iaState,
	const std::vector<DX12PendingResourceViewRequest> &requests,
	DX12ModIaReplacement *replacement);


void AppendTextureOverrideMatches(
	const std::vector<const Bunny::TextureOverrideConfig*> &configs,
	uint32_t matchedHash,
	std::vector<DX12TextureOverrideMatch> *resolvedMatches,
	std::unordered_set<const Bunny::TextureOverrideConfig*> *seen);


void SortTextureOverrideMatches(
	std::vector<DX12TextureOverrideMatch> *matches);


bool FindTargetTextureOverride(
	ID3D12GraphicsCommandList *commandList,
	const DX12IaHashState &iaState, const Bunny::CommandListTarget &target,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance,
	std::vector<DX12TextureOverrideMatch> *matches,
	const std::vector<DX12CurrentShaderResourceBinding> *preResolvedGraphicsSrvs = nullptr);


bool DX12ModHasAnyPotentialDirectIaTextureOverrideCandidate(
	const DX12ActiveIaState &ia,
	const DX12IaHashTargetPlan &targetPlan,
	bool indexed,
	uint32_t vertexCount,
	uint32_t indexCount,
	uint32_t instanceCount,
	uint32_t firstVertex,
	uint32_t firstIndex,
	uint32_t firstInstance);


// Returns true when the prefilter tables prove no candidate can exist for this
// (ib/vb, slot, hash) tuple, ignoring draw-context filters (match_first_index
// etc.). The prefilter tables are queried with zero counts: callers must only
// use this when gIaPrefilterCountSensitive == 0 so the count-indexed tables are
// empty and the generic tables are authoritative. Candidates that depend on
// active pre-skin sections still resolve through the live snapshot, and any
// candidate that survives hash + match_cs + IA-binding checks counts as
// "possibly present" so the per-draw path stays authoritative.
bool IaHashDefinitelyHasNoCandidate(
	bool indexBuffer,
	uint32_t vertexSlot,
	uint32_t hash);


// DX11-style IA-state-level negative check: the per-view hashes are computed
// (or reused from the resident cache) and matched against the candidate tables
// without any draw-context filtering. Only call when gIaPrefilterCountSensitive
// is 0; the result is then independent of vertex/index counts and offsets, so
// the draw path can cache it across draws for the same IA bindings.
bool DX12ModIaStateDefinitelyHasNoCandidate(
	const DX12ActiveIaState &ia,
	const DX12IaHashTargetPlan &targetPlan,
	bool indexed);


bool DX12ModIaPrefilterCountSensitive();


inline const DX12CommandExecutionRuntimeSnapshot *
LoadCommandExecutionRuntimeSnapshot()
{
	// See LoadTextureOverrideRuntimeSnapshot: TLS-cached, generation-checked.
	return CommandExecutionRuntimeSnapshotTlsRef().get();
}


// Ownership-taking variant for callers that must hold the snapshot beyond
// the current query (e.g. executor member caches). One refcount copy from
// the TLS cache; NOT for per-draw hot paths.
inline std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot>
SnapshotCommandExecutionRuntimeShared()
{
	return CommandExecutionRuntimeSnapshotTlsRef();
}


bool CommandListRuntimeEffectLocked(
	const DX12CommandExecutionRuntimeSnapshot *snapshot,
	const std::wstring &name);


bool CommandListRuntimeEffectLocked(
	const DX12CommandExecutionRuntimeSnapshot *snapshot,
	const std::wstring &name,
	bool includePost);



bool CommandListRuntimeEffect(
	const std::wstring &name, bool includePost);



inline std::shared_ptr<const Bunny::CommandListConfig>
SnapshotCommandListConfig(const std::wstring &name)
{
	const DX12CommandExecutionRuntimeSnapshot *snapshot =
		LoadCommandExecutionRuntimeSnapshot();
	if (!snapshot)
		return nullptr;
	auto it = snapshot->commandLists.find(name);
	return it == snapshot->commandLists.end() ? nullptr : it->second;
}


void AppendResolvedDescriptorPrefilterSectionIds(
	const std::vector<DX12TriggeredTextureOverride> &source,
	std::vector<uint32_t> *sectionIds);


bool HasPotentialResolvedDescriptorPrefilterCandidates(
	const DX12CommandExecutionRuntimeSnapshot &snapshot,
	uint32_t vertexCount,
	uint32_t indexCount);


void CollectResolvedDescriptorPrefilterSectionIds(
	const DX12CommandExecutionRuntimeSnapshot &snapshot,
	uint32_t vertexCount,
	uint32_t indexCount,
	std::vector<uint32_t> *sectionIds);


DX12IaHashState EmptyIaHashState();


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


inline thread_local DX12DirectDescriptorPrefilterCache tlsDirectDescriptorPrefilterCache;


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

inline thread_local PrefilterSrvResolutionCacheEntry tlsPrefilterSrvResolutionCache;


bool PrefilterSrvCacheTryLoad(
	ID3D12GraphicsCommandList *commandList,
	DX12SimulateContext *runtimeState,
	UINT stageIndex,
	UINT64 reloadGeneration,
	std::vector<DX12ResolvedGraphicsSrvBinding> *outHashed);


void PrefilterSrvCacheStore(
	ID3D12GraphicsCommandList *commandList,
	DX12SimulateContext *runtimeState,
	UINT stageIndex,
	UINT64 reloadGeneration,
	const std::vector<DX12CurrentShaderResourceBinding> &bindings,
	const std::vector<DX12ResolvedGraphicsSrvBinding> &hashed);


void ResolveGraphicsSrvBindings(
	ID3D12GraphicsCommandList *commandList,
	ID3D12Device *cachedDevice,
	const std::vector<DX12CurrentShaderResourceBinding> &bindings,
	std::vector<DX12ResolvedGraphicsSrvBinding> *resolvedBindings);


bool TryLoadResolvedGraphicsSrvBindings(
	DX12SimulateContext *runtimeState,
	UINT64 descriptorQuerySerial,
	std::vector<DX12ResolvedGraphicsSrvBinding> *resolvedBindings);


void StoreResolvedGraphicsSrvBindings(
	DX12SimulateContext *runtimeState,
	UINT64 descriptorQuerySerial,
	const std::vector<DX12ResolvedGraphicsSrvBinding> &resolvedBindings);


void FillSyntheticTextureBinding(
	const Bunny::CommandListTarget &target,
	ID3D12Resource *resource,
	DXGI_FORMAT format,
	UINT width,
	UINT height,
	DX12CurrentShaderResourceBinding *binding);



bool FindDescriptorTextureOverridesFromResolvedBindings(
	const DX12CommandExecutionRuntimeSnapshot *snapshot,
	const Bunny::CommandListTarget &target,
	const std::vector<DX12ResolvedGraphicsSrvBinding> &resolvedBindings,
	uint32_t vertexCount,
	uint32_t indexCount,
	uint32_t instanceCount,
	uint32_t firstVertex,
	uint32_t firstIndex,
	uint32_t firstInstance,
	std::vector<DX12TextureOverrideMatch> *matches);


void AppendGraphicsSrvTextureOverrideCandidatesForTarget(
	const DX12CommandExecutionRuntimeSnapshot *snapshot,
	const DX12ResolvedGraphicsSrvBinding &resolved,
	const DX12TextureOverrideTargetCacheKey &cacheKey,
	const std::vector<uint32_t> &allowedSectionIds,
	std::unordered_map<
		DX12TextureOverrideTargetCacheKey,
		std::vector<DX12TextureOverrideMatch>,
		DX12TextureOverrideTargetCacheKeyHash> *matchesByTarget);


void AppendResolvedBindingsToDescriptorTextureOverrideCache(
	const DX12CommandExecutionRuntimeSnapshot *snapshot,
	const std::vector<DX12ResolvedGraphicsSrvBinding> &resolvedBindings,
	Bunny::CommandListTargetKind kind,
	bool compute,
	const std::vector<uint32_t> &allowedSectionIds,
	std::unordered_map<
		DX12TextureOverrideTargetCacheKey,
		std::vector<DX12TextureOverrideMatch>,
		DX12TextureOverrideTargetCacheKeyHash> *matchesByTarget);



void FillSyntheticBufferBinding(
	const Bunny::CommandListTarget &target,
	ID3D12Resource *resource,
	const std::string &kind,
	UINT64 viewBytes,
	UINT stride,
	DX12CurrentShaderResourceBinding *binding);


bool BuildResolvedDescriptorBindingsForResourceOverride(
	ID3D12Device *device,
	ID3D12GraphicsCommandList *commandList,
	const Bunny::CommandListTarget &target,
	const std::wstring &resourceName,
	const std::vector<DX12ResolvedGraphicsSrvBinding> *baseBindings,
	std::vector<DX12ResolvedGraphicsSrvBinding> *resolvedBindings);

// Capture mode needs ReleaseLoadedResourceObjects (declared above with the
// loaded-resource API) plus the SRV view utilities, so it sits just before
// the executor that calls it.
#include "DX12DrawTextureCaptureUtils.h"

#include "DX12CommandListExecutor.h"


bool RunNamedCommandList(
	ID3D12Device *device,
	ID3D12GraphicsCommandList *commandList,
	const std::wstring &name,
	DX12ModIaReplacement *replacement,
	std::vector<DX12PendingResourceViewRequest> *pendingResourceViews);


bool RunNamedCommandListVmOnly(const std::wstring &name, bool includePost);


void ProcessDx12KeyBindingsForFrame();


void AppendShaderOverrideForHashLocked(
	uint64_t hash, std::vector<const Bunny::ShaderOverrideConfig*> *configs,
	std::unordered_set<std::wstring> *seen);


bool ShaderModelMatchesRegex(
	const Bunny::ShaderRegexConfig &regex, const std::string &shaderModel);


inline constexpr LONG ShaderOverrideHotPathLogLimit = 512;

inline volatile LONG gShaderOverridePsoCacheHitLogCount = 0;

inline volatile LONG gShaderOverrideNegativeCacheHitLogCount = 0;

inline volatile LONG gShaderOverrideNegativeCacheStoreLogCount = 0;

inline volatile LONG gShaderOverrideNegativeCacheDescriptorBlockedLogCount = 0;

inline volatile LONG gShaderOverrideNegativeCacheDynamicBlockedLogCount = 0;


bool AllowShaderOverrideHotPathLog(volatile LONG *counter);


Bunny::ShaderOverrideConfig MakeShaderOverrideFromRegex(
	const Bunny::ShaderRegexConfig &regex, uint64_t hash);


void AppendShaderRegexForShaderLocked(
	uint64_t hash,
	const std::string &shaderModel,
	std::vector<Bunny::ShaderOverrideConfig> *regexConfigs,
	std::unordered_set<std::wstring> *seen);


void BuildShaderOverrideConfigPointers(
	DX12ShaderOverridePsoMatchCache *cache,
	bool dispatch,
	std::vector<const Bunny::ShaderOverrideConfig*> *configs);


void CopyShaderOverrideConfigs(
	const std::vector<const Bunny::ShaderOverrideConfig*> &configs,
	std::vector<Bunny::ShaderOverrideConfig> *snapshot);


void MergeTextureOverrideDrawMatchUsage(
	const Bunny::TextureOverrideConfig &config,
	DX12DrawMatchUsage *usage);


void AppendUniqueDrawMatchUsageTargetLocked(
	const Bunny::CommandListTarget &target,
	std::vector<Bunny::CommandListTarget> *targets);


bool DrawMatchUsageCommandListRuntimeEffectLocked(const std::wstring &name);


void CollectCheckTextureOverrideTargetsFromActionsLocked(
	const std::vector<Bunny::CommandListAction> &actions,
	UINT depth,
	std::unordered_set<std::wstring> *commandListStack,
	std::vector<Bunny::CommandListTarget> *targets);


void CollectCheckTextureOverrideTargetsFromShaderOverrideLocked(
	const Bunny::ShaderOverrideConfig &config,
	std::vector<Bunny::CommandListTarget> *targets);



void MergeDescriptorTextureOverrideDrawMatchUsageLocked(
	DX12DrawMatchUsage *usage);


void MergeIaTextureOverrideDrawMatchUsageLocked(
	bool indexBuffer,
	uint32_t vertexSlot,
	DX12DrawMatchUsage *usage);


DX12DrawMatchUsage BuildTextureOverrideDrawMatchUsageLocked(
	const std::vector<Bunny::CommandListTarget> &targets);


DX12DrawMatchUsage BuildDrawMatchUsageLocked(
	const std::vector<const Bunny::ShaderOverrideConfig*> &configs);


DX12DrawMatchUsage BuildDrawMatchUsageLocked(
	const std::vector<Bunny::ShaderOverrideConfig> &configs);


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


inline std::unordered_map<ID3D12PipelineState*, DX12CompiledPsoOverridePlan> gPsoOverridePlans;

inline SRWLOCK gPsoOverridePlansLock = SRWLOCK_INIT;

inline UINT64 gPsoOverridePlansFormatVersion = 0;


void ClearPsoOverridesIfFormatChanged();


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


inline thread_local DX12CompiledPsoPlanTlsCache gCompiledPsoPlanTlsCache;


bool TryLoadCompiledPsoPlan(
	ID3D12PipelineState *pipelineState,
	UINT64 reloadGeneration,
	std::shared_ptr<const std::vector<Bunny::ShaderOverrideConfig>> *configSnapshot,
	std::shared_ptr<const std::vector<DX12CompiledShaderOverridePlan>> *compiledPlanSnapshot,
	bool *alwaysMiss);


bool TryLoadCompiledPsoDrawDecisionMetadata(
	ID3D12PipelineState *pipelineState,
	UINT64 reloadGeneration,
	DX12IaHashTargetPlan *drawTargetPlan,
	DX12IaHashTargetPlan *drawDirectTargetPlan,
	DX12DrawMatchUsage *drawMatchUsage,
	DX12DrawMatchUsage *drawDirectDrawMatchUsage,
	bool *drawDirectTextureChecksOnly);


bool DX12ModTryLoadStaticReplacementForPso(
	ID3D12PipelineState *pipelineState,
	UINT64 reloadGeneration,
	DX12ModIaReplacement *replacement);


std::shared_ptr<const DX12ModIaReplacement> DX12ModFindStaticReplacementSnapshotForPso(
	ID3D12PipelineState *pipelineState,
	UINT64 reloadGeneration);


inline std::shared_ptr<const std::vector<DX12CompiledShaderOverridePlan>>
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


inline std::shared_ptr<const DX12ModIaReplacement>
BuildStaticReplacementSnapshotFromCompiledPlans(
	const std::vector<DX12CompiledShaderOverridePlan> &plans);


void StoreCompiledPsoPlan(
	ID3D12PipelineState *pipelineState,
	UINT64 reloadGeneration,
	const std::vector<Bunny::ShaderOverrideConfig> &configSnapshot,
	const DX12IaHashTargetPlan *drawTargetPlan = nullptr,
	const DX12IaHashTargetPlan *drawDirectTargetPlan = nullptr,
	const DX12DrawMatchUsage *drawMatchUsage = nullptr,
	const DX12DrawMatchUsage *drawDirectDrawMatchUsage = nullptr,
	const bool *drawDirectTextureChecksOnly = nullptr);


void StoreStaticReplacementForPso(
	ID3D12PipelineState *pipelineState,
	UINT64 reloadGeneration,
	const DX12ModIaReplacement &replacement);


void ClearCompiledPsoPlans();


bool ReplacementHasAnyEffect(const DX12ModIaReplacement &replacement);


bool ReplacementIsStaticTemplateCandidate(
	const DX12CommandListExecutor &executor,
	const std::vector<DX12PendingResourceViewRequest> &pendingResourceViews,
	const DX12ModIaReplacement &replacement);


bool CompiledCommandListPlanIsStaticTemplateCandidate(
	const DX12CompiledCommandListPlan &plan);


bool CompiledShaderOverridePlanIsStaticTemplateCandidate(
	const DX12CompiledShaderOverridePlan &plan);


inline std::shared_ptr<const DX12ModIaReplacement>
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

#include "DX12ShaderOverrideIaTargetPlanUtils.h"


bool ShaderOverrideNegativeCacheEnabled(
	bool allowDescriptorIndependentNegativeCache);


DX12ShaderOverrideNegativeCacheKey MakeShaderOverrideNegativeCacheKey(
	ID3D12PipelineState *pipelineState,
	const DX12IaHashState &iaState,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance,
	const DX12DrawMatchUsage &usage);


UINT64 HashShaderOverrideNegativeCacheKey(
	const DX12ShaderOverrideNegativeCacheKey &key);


bool TryFindShaderOverrideNegativeCache(
	const DX12ShaderOverrideNegativeCacheKey &key);


void StoreShaderOverrideNegativeCache(
	const DX12ShaderOverrideNegativeCacheKey &key);


void ClearShaderOverrideNegativeCache();


bool TryFindCachedShaderOverridesForPsoLocked(
	ID3D12PipelineState *pipelineState, bool dispatch,
	std::vector<const Bunny::ShaderOverrideConfig*> *configs);


// Public draw-path PSO plan lookup used before IA hash work.
// This avoids building IA hashes for PSOs with no matching shader overrides.
bool TryFindCachedShaderOverrideIaTargetPlanForPsoLocked(
	ID3D12PipelineState *pipelineState,
	DX12IaHashTargetPlan *targetPlan);


bool TryFindCachedShaderOverrideDirectIaTargetPlanForPsoLocked(
	ID3D12PipelineState *pipelineState,
	DX12IaHashTargetPlan *targetPlan);


bool TryFindCachedShaderOverrideDirectTextureChecksOnlyForPsoLocked(
	ID3D12PipelineState *pipelineState,
	bool *directTextureChecksOnly);


bool TryFindCachedShaderOverrideDrawMatchUsageForPsoLocked(
	ID3D12PipelineState *pipelineState,
	DX12DrawMatchUsage *drawMatchUsage);


bool DX12ModHasShaderOverridesForPso(ID3D12PipelineState *pipelineState);


void FindShaderOverridesForPsoLocked(
	ID3D12PipelineState *pipelineState, bool dispatch,
	std::vector<const Bunny::ShaderOverrideConfig*> *configs);


bool DX12ModCollectShaderOverrideIaHashTargets(
	ID3D12PipelineState *pipelineState,
	DX12IaHashTargetPlan *targetPlan);


bool DX12ModCompileDrawDecision(
	ID3D12PipelineState *pipelineState,
	DX12CompiledDrawDecision *decision);


bool DX12ModHasAnyDirectIaTextureOverrideCandidate(
	const DX12IaHashState &iaState,
	const DX12IaHashTargetPlan &targetPlan,
	bool indexed,
	uint32_t vertexCount,
	uint32_t indexCount,
	uint32_t instanceCount,
	uint32_t firstVertex,
	uint32_t firstIndex,
	uint32_t firstInstance);


bool DX12ModHasAnyDirectDescriptorTextureOverrideCandidate(
	ID3D12GraphicsCommandList *commandList,
	const DX12IaHashTargetPlan &targetPlan,
	uint32_t vertexCount,
	uint32_t indexCount,
	uint32_t instanceCount,
	uint32_t firstVertex,
	uint32_t firstIndex,
	uint32_t firstInstance);


void BackfillCompiledPsoPlanFromFallback(
	ID3D12PipelineState *pipelineState,
	UINT64 reloadGeneration,
	const std::vector<Bunny::ShaderOverrideConfig> &configSnapshot);


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
	std::shared_ptr<const DX12ModIaReplacement> *generatedStaticReplacement);


void DX12ModRunPostShaderOverrideReplacement(
	ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState,
	const DX12IaHashState &iaState,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance,
	DX12ModIaReplacement *replacement);


bool ShaderOverrideHasSkipLocked(uint64_t hash);


bool ShaderBytecodeHasSkipLocked(const D3D12_SHADER_BYTECODE &bytecode);



void UpdateStoredPsoSkipLocked(DX12StoredPso *record);


bool DX12ModGetPipelineStateSkipFlags(
	ID3D12PipelineState *pipelineState,
	const DX12PsoShaderInfo *shaderInfo,
	bool *skipDraw,
	bool *skipDispatch);


bool DX12ModShouldSkipPipelineState(ID3D12PipelineState *pipelineState, bool dispatch);


UINT64 DX12ModGetReloadGeneration();


UINT64 DX12ModGetReloadGenerationFast();


void DX12ModRecordGraphicsPipelineState(
	ID3D12Device *device, ID3D12PipelineState *pipelineState,
	const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc);


void DX12ModRecordComputePipelineState(
	ID3D12Device *device, ID3D12PipelineState *pipelineState,
	const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc);


bool HasShaderOverrideLocked(uint64_t hash);


bool GraphicsPsoNeedsReplacementLocked(const D3D12_GRAPHICS_PIPELINE_STATE_DESC &desc);


bool ComputePsoNeedsReplacementLocked(const D3D12_COMPUTE_PIPELINE_STATE_DESC &desc);


ID3D12PipelineState *CreateGraphicsReplacement(DX12StoredPso *record);


ID3D12PipelineState *CreateComputeReplacement(DX12StoredPso *record);


ID3D12PipelineState *DX12ModGetReplacementPipelineState(ID3D12PipelineState *pipelineState);



bool DX12ModApplyDrawSrvReplacement(
	ID3D12GraphicsCommandList *commandList,
	DX12ModIaReplacement *replacement,
	bool executeOriginalDraw);



void DX12ModRestoreDrawSrvReplacement(
	ID3D12GraphicsCommandList *commandList,
	DX12ModIaReplacement *replacement);


void DX12ModClearIaReplacementRuntimeCaches();

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


inline std::unordered_map<UINT64, DX12PreSkinOutputResizeEntry>
	gPreSkinOutputResizeMap;

using DX12PreSkinOutputResizeEntryMap =
	std::unordered_map<UINT64, DX12PreSkinOutputResizeEntry>;

inline std::unordered_map<IUnknown*, DX12PreSkinOutputResizeEntryMap>
	gPreSkinOutputResizeCommandListMaps;

inline SRWLOCK gPreSkinOutputResizeLock = SRWLOCK_INIT;

inline std::atomic<bool> gHasPreSkinOutputResizeBindings{false};

inline std::atomic<UINT64> gPreSkinOutputResizeGeneration{1};


void BumpPreSkinOutputResizeGeneration();


IUnknown *PreSkinOutputResizeCommandListIdentity(
	ID3D12GraphicsCommandList *commandList);


ID3D12Resource *StorePreSkinOutputResizeEntryLocked(
	DX12PreSkinOutputResizeEntryMap *entries,
	const DX12PreSkinOutputResizeEntry &entry);


void DX12ModRegisterPreSkinOutputResize(
	ID3D12GraphicsCommandList *commandList,
	ID3D12Resource *originalResource, ID3D12Resource *resizedResource,
	UINT64 originalByteWidth, UINT64 resizedByteWidth, UINT stride);


DX12PreSkinOutputResizeEntry *FindPreSkinOutputResizeEntryLocked(
	DX12PreSkinOutputResizeEntryMap *entries,
	UINT64 viewGpuVa,
	UINT64 *sourceOffset);


bool DX12ModResolvePreSkinOutputResizeByGpuVa(
	ID3D12GraphicsCommandList *commandList,
	UINT64 viewGpuVa, DX12PreSkinOutputResizeBinding *binding);


UINT64 DX12ModGetPreSkinOutputResizeGeneration();


bool DX12ModHasPreSkinOutputResizeBindings();


// Plain volatile probe: the flag only ever goes 0->1 on the first cache entry
// after a clear and is reset under the cache lock, so a stale 1 is
// conservative (the IA replay gate does a lookup and finds nothing), never a
// missed redirect. Present clears the frame-local publish map, so this
// fence-owned-cache hint is what keeps the gate trying when producer and
// consumer record on opposite sides of that boundary.
bool DX12ModHasPreSkinOutputResizeCacheEntries();


void DX12ModForgetPreSkinOutputResizesForCommandList(
	ID3D12GraphicsCommandList *commandList);


void DX12ModClearPreSkinOutputResizeMap();

