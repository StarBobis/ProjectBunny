#pragma once

// Simulate-context draw/compute templates and resolved runtime state.
// Split out of Core/DX12CommandState.h; include that umbrella header from existing code.
#include "../DX12Diagnostics.h"

class DX12SimulateContextDrawTemplate
{
public:
	static constexpr size_t DirectIaCandidateSetCount = 512;
	static constexpr size_t DirectIaCandidateWayCount = 8;
	static constexpr size_t ResidentDrawNegativeEntryCount = 128;
	static_assert((DirectIaCandidateSetCount & (DirectIaCandidateSetCount - 1)) == 0,
		"Direct IA candidate set count must be a power of two");
	static_assert((ResidentDrawNegativeEntryCount & (ResidentDrawNegativeEntryCount - 1)) == 0,
		"Resident draw negative entry count must be a power of two");

	class ResolvedPlanState
	{
	public:
		bool dirty = true;
		UINT64 invalidationSerial = 1;
	};

	class CachedIaHashBuildEntry
	{
	public:
		bool valid = false;
		bool hasState = false;
		DX12IaHashStateCacheKey key;
		DX12IaHashState state;
	};

	class DirectIaCandidateEntry
	{
	public:
		bool valid = false;
		bool hasCandidate = false;
		UINT64 age = 0;
		size_t tag = 0;
		DX12IaHashStateCacheKey iaKey;
		UINT instanceCount = 0;
		UINT firstInstance = 0;
	};

	class DirectIaCandidateCache
	{
	public:
		using Entries = std::array<DirectIaCandidateEntry,
			DirectIaCandidateSetCount * DirectIaCandidateWayCount>;

		DirectIaCandidateCache() = default;

		DirectIaCandidateCache(const DirectIaCandidateCache &)
		{
		}

		DirectIaCandidateCache &operator=(const DirectIaCandidateCache &other)
		{
			if (this != &other) {
				entries.reset();
				clock = 0;
			}
			return *this;
		}

		DirectIaCandidateCache(DirectIaCandidateCache &&) noexcept = default;
		DirectIaCandidateCache &operator=(DirectIaCandidateCache &&) noexcept = default;

		Entries *EnsureEntries()
		{
			if (!entries)
				entries = std::make_unique<Entries>();
			return entries.get();
		}

		std::unique_ptr<Entries> entries;
		UINT64 clock = 0;
	};

	class IaTargetPresenceEntry
	{
	public:
		bool valid = false;
		bool hasTarget = false;
		UINT64 iaEpoch = 0;
		UINT64 targetPlanFingerprint = 0;
		bool indexed = false;
	};

	// IA-state-level negative cache: when the compiled prefilter tables are
	// draw-count independent (gIaPrefilterCountSensitive == 0), "this IA state
	// has no override candidate" is a property of the bound buffer views alone
	// (DX11: hash saved at bind time, map lookup miss ends the draw). Caching it
	// here lets every draw that shares the same IA bindings skip the whole
	// per-draw candidate chain.
	class IaStateNegativeCache
	{
	public:
		bool valid = false;
		bool noCandidate = false;
		UINT64 iaEpoch = 0;
		UINT64 iaContentEpoch = 0;
		// Global buffer-content generation: per-list iaContentEpoch misses
		// content written by other lists / Map, which would otherwise leave
		// a stale "no candidate" verdict across list Reset (missing mesh
		// replacement on shadow passes after scene transitions).
		UINT64 bufferContentGeneration = 0;
		UINT64 reloadGeneration = 0;
		UINT64 targetPlanFingerprint = 0;
		// Active pre-skin section generation. The verdict reads
		// GetActivePreSkinSectionsSnapshot() whenever a candidate's config is
		// gated on match_cs, and that generation is bumped independently of
		// iaEpoch / iaContentEpoch - without this term a verdict proven before a
		// pre-skin section became active keeps answering "no candidate" and the
		// override is skipped.
		UINT64 preSkinGeneration = 0;
		ID3D12PipelineState *pipelineState = nullptr;
	};

	IaStateNegativeCache iaStateNegative;

	DX12IaHashStateCache iaHashStateCache;
	// Direct-target and full-target key prefixes. Resolved IA resources carry
	// their own content/lifetime generations; global epochs are only consulted
	// for unresolved fallback keys.
	std::array<DX12IaHashKeyPrefixCacheEntry, 2> iaHashKeyPrefixes;
	CachedIaHashBuildEntry lastDirectIaHashBuild;
	CachedIaHashBuildEntry lastFullIaHashBuild;
	// Candidate matching is separate from IA-state materialization. Entries are
	// keyed by the complete resolved IA identity plus the draw inputs that do not
	// belong to the IA hash state itself.
	DirectIaCandidateCache directIaCandidateCache;
	// IA target-presence checks depend only on the resident IA bindings, the
	// compiled target plan, and indexed mode. Keep both semantics separately:
	// direct checks consider descriptor targets while full checks do not.
	IaTargetPresenceEntry directIaTargetPresence;
	IaTargetPresenceEntry fullIaTargetPresence;
	// Exact direct-mapped resident cache: the common repeated negative draw is a
	// handful of integer comparisons, with no 32-slot scan or unordered lookup.
	std::array<DX12ResidentDrawNegativeEntry,
		ResidentDrawNegativeEntryCount> residentDrawNegativeCache;
	// Unlike IA caches, a resolved plan describes the current command-list
	// bindings and must never survive Reset or a graphics-state transition.
	ResolvedPlanState plan;
	// Bound-state draw-result cache: captures the full PSO/IA/descriptor
	// identity so repeated draws with unchanged state skip the entire
	// dynamic decision chain (IA hash, descriptor match, resource hash).
	DX12BoundDrawResultCache boundDrawResult;
};

class DX12ResolvedDrawRuntimeStateUtils
{
public:
	static void Clear(DX12SimulateContextDrawTemplate *state);
	static void InvalidatePlan(DX12SimulateContextDrawTemplate *state);
};

class DX12SimulateContextComputeTemplate
{
public:
	bool bindingsValid = false;
	bool bindingsFoundAny = false;
	bool bindingsUseRootValues = false;
	UINT64 bindingsSerial = 0;
	UINT64 bindingsDescriptorVersion = 0;
	UINT64 bindingsDescriptorQuerySerial = 0;
	UINT64 bindingsDependencyFingerprint = 0;
	ID3D12DescriptorHeap *bindingsHeap = nullptr;
	ID3D12RootSignature *bindingsRootSignature = nullptr;
	// Exact root-binding identity snapshot for cache validation. The game
	// re-binds compute root tables on every dispatch (and bumps
	// computeBindingSerial / root revisions), which made the previous
	// serial/revision validation miss on every dispatch and forced a full
	// binding re-collect per dispatch. Same root signature + same root
	// table/descriptor identities + same observed slot versions prove the
	// collected bindings are identical, so the cache hits across re-binds.
	std::array<UINT64, MaxRootParameters> bindingsRootIdentities;
	std::array<bool, MaxRootParameters> bindingsRootValid;
	std::vector<UINT> bindingsRootParameters;
	std::vector<UINT64> bindingsRootRevisions;
	std::vector<UINT> observedSlots;
	std::vector<UINT64> observedSlotVersions;
	std::vector<UINT64> observedSlotValueIds;
	// Content-addressed per-slot snapshot for cache validation across
	// descriptor-ring rotation: (root, offset-from-table-base, version,
	// valueId). Validation recomputes the current absolute slot from the
	// current table base, so the cache can hit when the game re-binds the
	// same descriptor values at a different ring location. This is the DX12
	// model - value identity is authoritative and the GPU handle / ring slot
	// is transient - instead of DX11's binding-slot identity.
	struct RuntimeObservedSlot
	{
		UINT root = 0;
		UINT offset = 0;
		UINT64 version = 0;
		UINT64 valueId = 0;
	};
	std::vector<RuntimeObservedSlot> bindingsObservedSlots;
	// Table GPU base pointer captured at store time. Validation may skip the
	// slot identity read when the descriptor query serial is unchanged AND
	// every observed table still points at the same base; a moved table means
	// the ring rotated and the new slots must be re-validated even if no
	// descriptor write happened since the last check.
	std::array<UINT64, MaxRootParameters> bindingsRootTableBases = {};
	std::vector<DX12CurrentComputeUavBinding> uavs;
	std::vector<DX12CurrentComputeUavBinding> srvs;
	std::vector<DX12CurrentComputeUavBinding> cbvs;
	std::vector<DX12CurrentRootConstants> rootConstants;
	// Local lock-free probe verdict cache: (csHash, probeFingerprint,
	// reloadGeneration) -> applied + dispatch override. Recording is
	// single-threaded per command list and this template survives Reset, so
	// repeated dispatches across frames hit here instead of taking the
	// global mod lock + map lookup on every dispatch.
	struct LocalProbeCacheEntry
	{
		UINT64 reloadGeneration = 0;
		uint64_t csHash = 0;
		UINT64 fingerprint = 0;
		UINT64 bindingDependencyFingerprint = 0;
		UINT64 bufferContentGeneration = 0;
		UINT64 iaContentEpoch = 0;
		UINT64 bindingSerial = 0;
		bool applied = false;
		bool valid = false;
		bool handlingSkip = false;
		bool hasDispatch = false;
		UINT groupsX = 0;
		UINT groupsY = 0;
		UINT groupsZ = 0;
	};
	static constexpr size_t LocalProbeCacheCount = 32;
	std::array<LocalProbeCacheEntry, LocalProbeCacheCount> localProbeCache = {};

	static size_t LocalProbeCacheSlot(
		UINT64 fingerprint, uint64_t csHash)
	{
		return static_cast<size_t>(
			(fingerprint ^ (csHash * 0x9e3779b97f4a7c15ull)) &
			(LocalProbeCacheCount - 1));
	}

	UINT64 bindingFingerprintDependency = 0;
	UINT64 bindingFingerprint = 0;
	UINT64 probeBindingDependencyFingerprint = 0;
	UINT64 probeFingerprint = 0;
	uint64_t probeCsHash = 0;
	UINT64 probeReloadGeneration = 0;
	UINT64 probeBindingSerial = 0;
	UINT64 probeIaContentEpoch = 0;
	// Global buffer-content generation at probe time: per-list epochs miss
	// content rewritten in place via CopyDescriptors / Map by other lists,
	// which would otherwise let the negative probe cache hit with a stale
	// "no replacement" verdict and make PreSkin replacement flicker on/off.
	UINT64 probeBufferContentGeneration = 0;
	bool probeResultValid = false;
	bool probeApplied = false;
	bool probeDispatchHandlingSkip = false;
	bool probeDispatchHasDispatch = false;
	UINT probeGroupsX = 0;
	UINT probeGroupsY = 0;
	UINT probeGroupsZ = 0;
};

class DX12ResolvedDescriptorQueryRuntimeState
{
public:
	bool valid = false;
	bool foundAny = false;
	bool compute = false;
	bool usesRootRevisions = false;
	UINT rangeType = UINT_MAX;
	UINT64 bindingQuerySerial = 0;
	UINT64 descriptorQuerySerial = 0;
	ID3D12DescriptorHeap *bindingsHeap = nullptr;
	// Root-binding snapshot used for cache validation. Revisions/serial bump
	// on every re-bind even when the game binds the same descriptors again,
	// which forced a full re-collect on every draw in binding-churn games
	// (e.g. ZZZ rebinds per draw; heapVersion changes thousands of times per
	// frame). Instead validate the exact bound identity: the GPU handle of
	// each root table / root descriptor. Same identity + same observed slot
	// versions => the collected bindings are identical, so the cache can hit
	// across re-binds. Content changes of observed slots are caught by
	// observedSlotVersions below.
	ID3D12RootSignature *rootSignature = nullptr;
	std::array<UINT64, MaxRootParameters> rootBindingIdentities;
	std::array<bool, MaxRootParameters> rootBindingValid;
	std::vector<UINT> rootParameters;
	std::vector<UINT64> rootRevisions;
	std::vector<UINT> observedSlots;
	std::vector<UINT64> observedSlotVersions;
	std::vector<UINT64> observedSlotValueIds;
	std::vector<DX12CurrentComputeUavBinding> bindings;
};

class DX12ResolvedGraphicsSrvBinding
{
public:
	DX12CurrentShaderResourceBinding binding;
	uint32_t resourceHash = 0;
	uint32_t descriptorHash = 0;
};

class DX12ResolvedGraphicsSrvRuntimeState
{
public:
	bool valid = false;
	bool usesRootRevisions = false;
	UINT64 descriptorQuerySerial = 0;
	std::vector<UINT> rootParameters;
	std::vector<UINT64> rootRevisions;
	std::vector<DX12ResolvedGraphicsSrvBinding> bindings;
};

class DX12SimulateContext
{
public:
	ID3D12PipelineState *pipelineState = nullptr;
	IUnknown *commandListIdentity = nullptr;
	// Wrapper-side object translation cache. These pointers are only compared;
	// ownership remains with the application/wrapper objects.
	ID3D12PipelineState *lastWrappedPipelineState = nullptr;
	ID3D12PipelineState *lastUnwrappedPipelineState = nullptr;
	ID3D12RootSignature *lastWrappedComputeRootSignature = nullptr;
	ID3D12RootSignature *lastUnwrappedComputeRootSignature = nullptr;
	ID3D12RootSignature *lastWrappedGraphicsRootSignature = nullptr;
	ID3D12RootSignature *lastUnwrappedGraphicsRootSignature = nullptr;
	// Reused barrier scratch storage avoids one allocation/free cycle per API call.
	std::vector<D3D12_RESOURCE_BARRIER> barrierScratch;
	volatile UINT64 computeBindingSerial = 0;
	DX12ActiveIaState ia;
	UINT64 iaEpoch = 1;
	UINT64 iaContentEpoch = 1;
	DX12SimulateContextDrawTemplate resolvedDraw;
	DX12CommandListTrackingCache trackingCache;

	ID3D12DescriptorHeap *cbvSrvUavHeap = nullptr;
	ID3D12DescriptorHeap *samplerHeap = nullptr;
	// Shadow of the game's render-target state, recorded by the wrapper's
	// OMSetRenderTargets override (RTV handles are expanded when the game binds
	// a contiguous range). The custom shader element replay rebinds the back
	// buffer + full viewport mid-list and restores the game state from these
	// fields afterwards; without them the replay would leak its OM state into
	// every subsequent game draw on the same list.
	UINT omRenderTargetCount = 0;
	D3D12_CPU_DESCRIPTOR_HANDLE omRenderTargets[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
	bool omHasDepthStencil = false;
	D3D12_CPU_DESCRIPTOR_HANDLE omDepthStencil = {};
	// Cached CBV/SRV/UAV heap content version.  Re-read on every SetDescriptorHeaps
	// instead of on every draw: DX12GetDescriptorHeapVersion takes the global
	// descriptor lock plus a hash-map lookup, and draws outnumber heap changes by
	// ~250x (988 vs 4 per frame in the target game).
	UINT64 cbvSrvUavHeapVersion = 0;
	// Last raw SetDescriptorHeaps input. Lets CaptureDescriptorHeaps skip
	// redundant GetDesc resolution when the game re-binds the identical heap
	// set (the resident heap state already reflects it). Only recorded after
	// the resident state was actually updated, and cleared with the rest of
	// the context on command-list Reset.
	UINT lastSetHeapsCount = 0xFFFFFFFF;
	ID3D12DescriptorHeap *lastSetHeapsInput[2] = {};
	bool runtimeCbvSrvUavHeapResolved = false;
	DX12DescriptorHeapSummary runtimeCbvSrvUavHeap = {};
	bool runtimeSamplerHeapResolved = false;
	DX12DescriptorHeapSummary runtimeSamplerHeap = {};
	UINT64 bindingQuerySerial = 1;
	UINT64 graphicsDescriptorQuerySerial = 1;
	UINT64 computeDescriptorQuerySerial = 1;
	UINT64 computeDescriptorBindingVersion = 1;
	// DXVK-style per-root-parameter dirty tracking for compute bindings.
	// RememberComputeRoot*() sets the bit for the changed root parameter
	// instead of bumping one global serial; probe/collect paths clear the
	// mask after they consume it. This keeps "binding churn on unrelated root
	// parameters" from invalidating the whole compute binding cache, exactly
	// like DXVK's bindingsDirty bitmask vs bindingsUsed intersection.
	UINT64 computeRootDirtyMask = 0;
	// Root parameters this compute root signature actually exposes to the
	// shader (from the parsed root signature summary). Only dirty bits that
	// intersect this mask invalidate the binding cache.
	UINT64 computeRootUsedMask = 0;
	bool computeRootUsedMaskValid = false;
	std::array<UINT64, MaxRootParameters> graphicsRootBindingRevisions = {};
	std::array<UINT64, MaxRootParameters> computeRootBindingRevisions = {};
	UINT graphicsRootTableValidCount = 0;
	UINT computeRootTableValidCount = 0;
	ID3D12RootSignature *graphicsRootSignature = nullptr;
	ID3D12RootSignature *computeRootSignature = nullptr;
	RootTableState graphicsRootTables[MaxRootParameters] = {};
	RootTableState computeRootTables[MaxRootParameters] = {};
	RootDescriptorState graphicsRootDescriptors[MaxRootParameters] = {};
	RootDescriptorState computeRootDescriptors[MaxRootParameters] = {};
	RootConstantsState graphicsRootConstants[MaxRootParameters] = {};
	RootConstantsState computeRootConstants[MaxRootParameters] = {};

	// Replacement PSO cache. First use may query global records; repeated use is direct.
	ID3D12PipelineState *cachedReplacementPsoKey = nullptr;
	ID3D12PipelineState *cachedReplacementPso = nullptr;

	// Compute shader hash cache, equivalent to DX11 current compute shader state.
	uint64_t cachedCsHash = 0;
	bool cachedCsHashValid = false;

	// Compiled per-PSO dispatch policy. Dispatch consumes this instead of
	// rediscovering non-candidate PreSkin state on every command.
	DX12SimulateContextDispatchExecutionTemplate dispatchDecision;

	// Compiled per-PSO draw policy. Draw consumes this instead of rediscovering
	// PSO shader override metadata on every command.
	DX12SimulateContextDrawExecutionTemplate drawDecision;

	// Plan fix A: lazy static-template IA application. The template's IA
	// replacement is applied once when the template first becomes active on
	// this command list and kept live until the PSO changes or the game
	// explicitly rebinds IA, instead of applying + restoring on every draw
	// (which also bumped iaEpoch every draw and defeated every IA-keyed
	// negative cache). staticTemplateSavedIa snapshots the IA state right
	// before the apply so the restore can put back the exact original views.
	std::shared_ptr<const DX12ModIaReplacement> staticTemplateReplacement;
	bool staticTemplateIaApplied = false;
	DX12ActiveIaState staticTemplateSavedIa;

	// Cached device pointer to eliminate per-draw AcquireModDevice QI overhead.
	// Set during Reset from the command list; device must outlive the command list
	// per DX12 spec (device -> command list is a parent-child relationship).
	ID3D12Device *device = nullptr;

	DX12SimulateContextComputeTemplate resolvedCompute;
	// Resident descriptor-query results, keyed by descriptor-only serials so
	// unrelated root-constant churn does not invalidate SRV slot lookups.
	std::array<DX12ResolvedDescriptorQueryRuntimeState, 6> resolvedDescriptorQueries;
	DX12ResolvedGraphicsSrvRuntimeState resolvedGraphicsSrvs;
};
