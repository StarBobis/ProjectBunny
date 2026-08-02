#pragma once

#include <memory>

#include "Runtime/DX12RuntimeCore.hpp"

class DX12ModIaReplacement;

class DX12ActiveIaState
{
public:
	bool hasIndexBuffer = false;
	D3D12_INDEX_BUFFER_VIEW indexBuffer = {};
	bool indexBufferHashValid = false;
	uint32_t indexBufferHash = 0;
	UINT64 indexBufferHashContentGeneration = 0;
	UINT64 indexBufferHashResolveGeneration = 0;
	bool indexBufferResolved = false;
	DX12BufferResourceSummary indexBufferResource = {};
	bool hasVertexBuffer[32] = {};
	D3D12_VERTEX_BUFFER_VIEW vertexBuffers[32] = {};
	bool vertexBufferHashValid[32] = {};
	uint32_t vertexBufferHashes[32] = {};
	UINT64 vertexBufferHashContentGeneration[32] = {};
	UINT64 vertexBufferHashResolveGeneration[32] = {};
	bool vertexBufferResolved[32] = {};
	DX12BufferResourceSummary vertexBufferResources[32] = {};
	bool preSkinRedirectNegativeValid[32] = {};
	UINT64 preSkinRedirectNegativeGeneration[32] = {};
	D3D12_VERTEX_BUFFER_VIEW preSkinRedirectNegativeSource[32] = {};
	D3D12_PRIMITIVE_TOPOLOGY primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
};

class DX12DrawMatchUsage
{
public:
	bool useVertexCount = false;
	bool useIndexCount = false;
	bool useInstanceCount = false;
	bool useFirstVertex = false;
	bool useFirstIndex = false;
	bool useFirstInstance = false;

	void RequireAll()
	{
		useVertexCount = true;
		useIndexCount = true;
		useInstanceCount = true;
		useFirstVertex = true;
		useFirstIndex = true;
		useFirstInstance = true;
	}
};


enum class DX12CompiledDispatchDecisionKind : uint32_t
{
	NoComputeShader = 0,
	NoPreSkinCandidate = 1,
	NeedsPreSkinProbe = 2,
	NeedsShaderOverrideWork = 3
};

class DX12CompiledDispatchDecision
{
public:
	DX12CompiledDispatchDecisionKind kind = DX12CompiledDispatchDecisionKind::NoComputeShader;
	ID3D12PipelineState *pipelineState = nullptr;
	UINT64 reloadGeneration = 0;
	uint64_t computeShaderHash = 0;
	bool resolved = false;
	bool hasComputeShader = false;
	bool hasPreSkinCandidate = false;
	bool hasShaderOverrideSkip = false;
	bool hasActiveShaderOverrides = false;
	bool hasActivePreSkinProbe = false;

	void Reset()
	{
		kind = DX12CompiledDispatchDecisionKind::NoComputeShader;
		pipelineState = nullptr;
		reloadGeneration = 0;
		computeShaderHash = 0;
		resolved = false;
		hasComputeShader = false;
		hasPreSkinCandidate = false;
		hasShaderOverrideSkip = false;
		hasActiveShaderOverrides = false;
		hasActivePreSkinProbe = false;
	}

	void SetNoComputeShader(
		ID3D12PipelineState *state,
		UINT64 generation,
		bool activeShaderOverrides,
		bool activePreSkinProbe)
	{
		Reset();
		kind = DX12CompiledDispatchDecisionKind::NoComputeShader;
		pipelineState = state;
		reloadGeneration = generation;
		resolved = true;
		hasActiveShaderOverrides = activeShaderOverrides;
		hasActivePreSkinProbe = activePreSkinProbe;
	}

	void SetNoPreSkinCandidate(
		ID3D12PipelineState *state,
		UINT64 generation,
		uint64_t csHash,
		bool activeShaderOverrides,
		bool activePreSkinProbe)
	{
		Reset();
		kind = DX12CompiledDispatchDecisionKind::NoPreSkinCandidate;
		pipelineState = state;
		reloadGeneration = generation;
		computeShaderHash = csHash;
		resolved = true;
		hasComputeShader = csHash != 0;
		hasActiveShaderOverrides = activeShaderOverrides;
		hasActivePreSkinProbe = activePreSkinProbe;
	}

	void SetNeedsPreSkinProbe(
		ID3D12PipelineState *state,
		UINT64 generation,
		uint64_t csHash,
		bool shaderOverrideSkip,
		bool activeShaderOverrides,
		bool activePreSkinProbe)
	{
		Reset();
		kind = DX12CompiledDispatchDecisionKind::NeedsPreSkinProbe;
		pipelineState = state;
		reloadGeneration = generation;
		computeShaderHash = csHash;
		resolved = true;
		hasComputeShader = csHash != 0;
		hasPreSkinCandidate = true;
		hasShaderOverrideSkip = shaderOverrideSkip;
		hasActiveShaderOverrides = activeShaderOverrides;
		hasActivePreSkinProbe = activePreSkinProbe;
	}

	void SetNeedsShaderOverrideWork(
		ID3D12PipelineState *state,
		UINT64 generation,
		uint64_t csHash,
		bool shaderOverrideSkip,
		bool activeShaderOverrides,
		bool activePreSkinProbe)
	{
		Reset();
		kind = DX12CompiledDispatchDecisionKind::NeedsShaderOverrideWork;
		pipelineState = state;
		reloadGeneration = generation;
		computeShaderHash = csHash;
		resolved = true;
		hasComputeShader = csHash != 0;
		hasShaderOverrideSkip = shaderOverrideSkip;
		hasActiveShaderOverrides = activeShaderOverrides;
		hasActivePreSkinProbe = activePreSkinProbe;
	}

	bool NeedsPreSkinProbe() const
	{
		return kind == DX12CompiledDispatchDecisionKind::NeedsPreSkinProbe &&
			hasPreSkinCandidate;
	}

	bool NeedsShaderOverrideWork() const
	{
		return kind == DX12CompiledDispatchDecisionKind::NeedsShaderOverrideWork ||
			hasShaderOverrideSkip;
	}

	bool IsPreSkinRejected() const
	{
		return kind == DX12CompiledDispatchDecisionKind::NoComputeShader ||
			kind == DX12CompiledDispatchDecisionKind::NoPreSkinCandidate ||
			kind == DX12CompiledDispatchDecisionKind::NeedsShaderOverrideWork;
	}

	bool MatchesGlobals(bool activeShaderOverrides, bool activePreSkinProbe) const
	{
		return hasActiveShaderOverrides == activeShaderOverrides &&
			hasActivePreSkinProbe == activePreSkinProbe;
	}
};



class DX12IaHashTargetPlan
{
public:
	class DescriptorTarget
	{
	public:
		uint32_t kind = 0;
		uint32_t stage = 0;
		uint32_t slot = 0;
	};

	bool requireAllIaBuffers = true;
	bool requireIndexBuffer = false;
	bool requireIndexBufferView = false;
	UINT requireVertexBufferMask = 0;
	bool hasDescriptorTargets = false;
	bool hasTextureOverrideChecks = false;
	std::vector<DescriptorTarget> descriptorTargets;
	mutable UINT64 cachedFingerprint = 0;

	void ResetKnown()
	{
		requireAllIaBuffers = false;
		requireIndexBuffer = false;
		requireIndexBufferView = false;
		hasDescriptorTargets = false;
		hasTextureOverrideChecks = false;
		descriptorTargets.clear();
		requireVertexBufferMask = 0;
		cachedFingerprint = 0;
	}

	void RequireAll()
	{
		requireAllIaBuffers = true;
		requireIndexBuffer = true;
		requireIndexBufferView = true;
		descriptorTargets.clear();
		requireVertexBufferMask = UINT_MAX;
		cachedFingerprint = 0;
	}

	void RequireIndexBuffer()
	{
		requireIndexBuffer = true;
		requireIndexBufferView = true;
		hasTextureOverrideChecks = true;
		cachedFingerprint = 0;
	}

	void RequireIndexBufferView()
	{
		requireIndexBufferView = true;
		cachedFingerprint = 0;
	}

	void RequireVertexBuffer(UINT slot)
	{
		hasTextureOverrideChecks = true;
		if (slot >= 32) {
			RequireAll();
			return;
		}
		requireVertexBufferMask |= 1u << slot;
		cachedFingerprint = 0;
	}

	void RequireDescriptorTarget(
		uint32_t kind,
		uint32_t stage,
		uint32_t slot)
	{
		hasDescriptorTargets = true;
		hasTextureOverrideChecks = true;
		for (const DescriptorTarget &target : descriptorTargets) {
			if (target.kind == kind &&
			    target.stage == stage &&
			    target.slot == slot)
				return;
		}
		DescriptorTarget target;
		target.kind = kind;
		target.stage = stage;
		target.slot = slot;
		descriptorTargets.push_back(target);
		cachedFingerprint = 0;
	}

	bool RequiresIndexBufferHash(bool indexed) const
	{
		return indexed && (requireAllIaBuffers || requireIndexBuffer);
	}

	bool RequiresIndexBuffer(bool indexed) const
	{
		return requireIndexBufferView || RequiresIndexBufferHash(indexed);
	}

	bool RequiresVertexBuffer(UINT slot) const
	{
		return requireAllIaBuffers ||
			(slot < 32 && (requireVertexBufferMask & (1u << slot)) != 0);
	}

	bool HasIaTargets() const
	{
		if (requireAllIaBuffers || requireIndexBuffer || requireIndexBufferView)
			return true;
		return requireVertexBufferMask != 0;
	}

	bool HasSameIaHashTargets(const DX12IaHashTargetPlan &other) const
	{
		if (requireAllIaBuffers != other.requireAllIaBuffers ||
			requireIndexBuffer != other.requireIndexBuffer ||
			requireIndexBufferView != other.requireIndexBufferView)
			return false;
		return requireVertexBufferMask == other.requireVertexBufferMask;
	}

	UINT64 Fingerprint() const
	{
		if (cachedFingerprint)
			return cachedFingerprint;
		UINT64 key = requireAllIaBuffers ? 0xf17a11b7a11b7a11ull : 0x27a7b125f00d1234ull;
		key ^= requireIndexBuffer ? 0x9e3779b97f4a7c15ull : 0ull;
		key ^= requireIndexBufferView ? 0xb7e151628aed2a6bull : 0ull;
		key ^= hasDescriptorTargets ? 0x51ed1234cafe9876ull : 0ull;
		for (UINT slot = 0; slot < 32; ++slot) {
			if ((requireVertexBufferMask & (1u << slot)) == 0)
				continue;
			key ^= static_cast<UINT64>(slot + 1) * 0x100000001b3ull;
			key ^= key >> 33;
			key *= 0xff51afd7ed558ccdull;
		}
		cachedFingerprint = key ? key : 1;
		return cachedFingerprint;
	}
};



enum class DX12CompiledDrawDecisionKind : uint32_t
{
	NoopNeverMatches = 0,
	NeedsIaHash = 1,
	NeedsDescriptorMatch = 2,
	NeedsReplacement = 3,
	SkipOriginal = 4
};

class DX12CompiledDrawDecision
{
public:
	DX12CompiledDrawDecisionKind kind = DX12CompiledDrawDecisionKind::NoopNeverMatches;
	ID3D12PipelineState *pipelineState = nullptr;
	UINT64 reloadGeneration = 0;
	bool resolved = false;
	bool hasActiveShaderOverrides = false;
	bool hasShaderOverrides = false;
	bool hasShaderOverrideSkip = false;
	bool targetPlanResolved = false;
	bool targetPlanHasIaTargets = false;
	bool targetPlanHasDescriptorTargets = false;
	bool directTargetPlanResolved = false;
	bool directTextureChecksOnly = false;
	bool negativeCacheSafe = false;
	bool shaderOverrideNegativeCacheAllowed = false;
	DX12IaHashTargetPlan targetPlan;
	DX12IaHashTargetPlan directTargetPlan;
	DX12DrawMatchUsage targetDrawMatchUsage;
	DX12DrawMatchUsage directTargetDrawMatchUsage;
	std::shared_ptr<const DX12ModIaReplacement> staticReplacementSnapshot;
	bool staticReplacementReady = false;

	void Reset()
	{
		kind = DX12CompiledDrawDecisionKind::NoopNeverMatches;
		pipelineState = nullptr;
		reloadGeneration = 0;
		resolved = false;
		hasActiveShaderOverrides = false;
		hasShaderOverrides = false;
		hasShaderOverrideSkip = false;
		targetPlanResolved = false;
		targetPlanHasIaTargets = false;
		targetPlanHasDescriptorTargets = false;
		directTargetPlanResolved = false;
		directTextureChecksOnly = false;
		negativeCacheSafe = false;
		shaderOverrideNegativeCacheAllowed = false;
		targetPlan = DX12IaHashTargetPlan();
		directTargetPlan = DX12IaHashTargetPlan();
		targetDrawMatchUsage = DX12DrawMatchUsage();
		directTargetDrawMatchUsage = DX12DrawMatchUsage();
		staticReplacementSnapshot.reset();
		staticReplacementReady = false;
	}

	void SetNoopNeverMatches(
		ID3D12PipelineState *state,
		UINT64 generation,
		bool activeShaderOverrides)
	{
		Reset();
		kind = DX12CompiledDrawDecisionKind::NoopNeverMatches;
		pipelineState = state;
		reloadGeneration = generation;
		resolved = true;
		hasActiveShaderOverrides = activeShaderOverrides;
	}

	void SetFromTargetPlan(
		ID3D12PipelineState *state,
		UINT64 generation,
		bool activeShaderOverrides,
		bool shaderOverrides,
		bool shaderOverrideSkip,
		const DX12IaHashTargetPlan &plan,
		const DX12IaHashTargetPlan &directPlan,
		const DX12DrawMatchUsage &drawMatchUsage,
		const DX12DrawMatchUsage &directDrawMatchUsage,
		bool directChecksOnly,
		bool descriptorTrackingNeeded)
	{
		Reset();
		pipelineState = state;
		reloadGeneration = generation;
		resolved = true;
		hasActiveShaderOverrides = activeShaderOverrides;
		hasShaderOverrides = shaderOverrides;
		hasShaderOverrideSkip = shaderOverrideSkip;
		targetPlan = plan;
		directTargetPlan = directPlan;
		targetDrawMatchUsage = drawMatchUsage;
		directTargetDrawMatchUsage = directDrawMatchUsage;
		targetPlanResolved = true;
		directTargetPlanResolved = true;
		targetPlanHasIaTargets = targetPlan.HasIaTargets();
		targetPlanHasDescriptorTargets = targetPlan.hasDescriptorTargets;
		directTextureChecksOnly = directChecksOnly;
		// Negative draw results are unsafe only when this PSO's own command
		// lists can observe descriptor contents. A global descriptor-tracking
		// need from another PSO should not disable IA-only draw miss caching.
		(void)descriptorTrackingNeeded;
		negativeCacheSafe = !targetPlanHasDescriptorTargets &&
			(targetPlan.hasTextureOverrideChecks || !targetPlanHasIaTargets);
		// The interpreter can cache only misses that prove they did not reach a
		// descriptor target. The outer PSO cache remains descriptor-conservative.
		shaderOverrideNegativeCacheAllowed =
			negativeCacheSafe || targetPlan.hasTextureOverrideChecks;

		if (!hasShaderOverrides && !hasShaderOverrideSkip) {
			kind = DX12CompiledDrawDecisionKind::NoopNeverMatches;
		} else if (hasShaderOverrideSkip) {
			kind = DX12CompiledDrawDecisionKind::SkipOriginal;
		} else if (targetPlanHasDescriptorTargets) {
			kind = DX12CompiledDrawDecisionKind::NeedsDescriptorMatch;
		} else if (targetPlanHasIaTargets) {
			kind = DX12CompiledDrawDecisionKind::NeedsIaHash;
		} else {
			kind = DX12CompiledDrawDecisionKind::NeedsReplacement;
		}
	}

	bool CanFastReject() const
	{
		return resolved &&
			kind == DX12CompiledDrawDecisionKind::NoopNeverMatches &&
			!hasShaderOverrideSkip;
	}

	bool NeedsIaHash() const
	{
		return targetPlanHasIaTargets;
	}

	bool NeedsDescriptorMatch() const
	{
		return targetPlanHasDescriptorTargets;
	}

	bool NeedsDrawOverrideWork() const
	{
		return !CanFastReject();
	}

	bool MatchesGlobals(bool activeShaderOverrides) const
	{
		return hasActiveShaderOverrides == activeShaderOverrides;
	}
};

using DX12SimulateContextDispatchExecutionTemplate =
	DX12CompiledDispatchDecision;
using DX12SimulateContextDrawExecutionTemplate =
	DX12CompiledDrawDecision;

class DX12CommandListTrackingState
{
public:
	bool trackBindings = false;
	bool trackIaBindings = false;
	bool trackPipelineStateBindings = false;
	bool recordBindingEvents = false;
	bool trackHuntIa = false;
	bool trackPsoState = false;
};



class DX12CommandListTrackingCache : public DX12CommandListTrackingState
{
public:
	UINT64 generation = 0;
};


class DX12PreSkinBindingCacheIdentity
{
public:
	std::uint64_t bindingSerial = 0;
	std::uint64_t descriptorHeapVersion = 0;
	std::uint64_t bufferResolveGeneration = 0;

	bool operator==(const DX12PreSkinBindingCacheIdentity &other) const
	{
		return bindingSerial == other.bindingSerial &&
			descriptorHeapVersion == other.descriptorHeapVersion &&
			bufferResolveGeneration == other.bufferResolveGeneration;
	}

	bool operator!=(const DX12PreSkinBindingCacheIdentity &other) const
	{
		return !(*this == other);
	}
};

class DX12PreSkinCacheIdentityUtils
{
public:
	static constexpr std::uint64_t NoDescriptorHeapVersion =
		~std::uint64_t{0};

	static bool CanReuseBindings(
		bool hasCachedBindings,
		const DX12PreSkinBindingCacheIdentity &cached,
		const DX12PreSkinBindingCacheIdentity &current)
	{
		// A bound heap with version 0 is unknown to the resource tracker. Reusing
		// its descriptor snapshot could hide a later descriptor update from Mod
		// matching, so keep the conservative rebuild behavior in that case.
		return hasCachedBindings && current.descriptorHeapVersion != 0 &&
			cached == current;
	}

	static std::uint64_t MakeProbeFingerprint(
		std::uint64_t bindingFingerprint,
		std::uint64_t iaContentEpoch)
	{
		// PreSkin candidate selection depends on CS identity and the stable
		// Binding State fingerprint. Dispatch/Copy activity does not alter that
		// plan; positive hits still execute replacement commands every dispatch.
		(void)iaContentEpoch;
		return bindingFingerprint ? bindingFingerprint : 1;
	}
};


class DX12IaBufferHash
{
public:
	UINT slot = 0;
	uint32_t hash = 0;
	D3D12_VERTEX_BUFFER_VIEW vertexView = {};
};




class DX12IaHashState
{
public:
	bool hasIndexBuffer = false;
	uint32_t indexHash = 0;
	D3D12_INDEX_BUFFER_VIEW indexView = {};
	std::vector<DX12IaBufferHash> vertexBuffers;
	// Compute dispatch bindings: UAV/SRV/CBV buffer hashes for PreSkin matching.
	std::vector<DX12IaBufferHash> computeBuffers;
};


class DX12IaDrawInvocation
{
public:
	bool indexed = false;
	uint32_t vertexCount = 0;
	uint32_t indexCount = 0;
	uint32_t instanceCount = 0;
	uint32_t firstVertex = 0;
	uint32_t firstIndex = 0;
	INT baseVertex = 0;
	uint32_t firstInstance = 0;

	static DX12IaDrawInvocation ForDrawInstanced(
		uint32_t vertexCount, uint32_t instanceCount,
		uint32_t firstVertex, uint32_t firstInstance)
	{
		DX12IaDrawInvocation draw;
		draw.indexed = false;
		draw.vertexCount = vertexCount;
		draw.instanceCount = instanceCount;
		draw.firstVertex = firstVertex;
		draw.firstInstance = firstInstance;
		return draw;
	}

	static DX12IaDrawInvocation ForDrawIndexedInstanced(
		uint32_t indexCount, uint32_t instanceCount,
		uint32_t firstIndex, INT baseVertex, uint32_t firstInstance)
	{
		DX12IaDrawInvocation draw;
		draw.indexed = true;
		draw.indexCount = indexCount;
		draw.instanceCount = instanceCount;
		draw.firstIndex = firstIndex;
		draw.baseVertex = baseVertex;
		draw.firstInstance = firstInstance;
		return draw;
	}
};



class DX12ResidentCacheGrowthUtils
{
public:
	static size_t NextBudget(size_t currentBudget, size_t minRequired)
	{
		size_t next = currentBudget ? currentBudget : 256;
		while (next < minRequired) {
			const size_t doubled = next * 2;
			if (doubled <= next) {
				next = minRequired;
				break;
			}
			next = doubled;
		}
		return next;
	}

	template <class AssocContainer>
	static void EnsureBudget(
		size_t *budget,
		AssocContainer *entries,
		size_t minRequired)
	{
		if (!budget || !entries || minRequired <= *budget)
			return;
		const size_t next = NextBudget(*budget, minRequired);
		entries->reserve(next);
		*budget = next;
	}
};

class DX12DirectIaFastRejectCacheKey
{
public:
	UINT64 reloadGeneration = 0;
	UINT64 preSkinGeneration = 0;
	UINT64 targetPlanFingerprint = 0;
	UINT64 iaViewFingerprint = 0;
	UINT64 contentGeneration = 0;
	UINT64 resolveGeneration = 0;
	bool requiresIndexBuffer = false;
	UINT32 requiredVertexMask = 0;
	bool hasIndexBuffer = false;
	UINT64 indexBufferLocation = 0;
	UINT indexBufferSize = 0;
	UINT indexBufferFormat = 0;
	UINT32 vertexBufferMask = 0;
	std::array<UINT64, 32> vertexBufferLocations = {};
	std::array<UINT, 32> vertexBufferSizes = {};
	std::array<UINT, 32> vertexBufferStrides = {};
	bool indexed = false;
	UINT vertexCount = 0;
	UINT indexCount = 0;
	UINT instanceCount = 0;
	UINT firstVertex = 0;
	UINT firstIndex = 0;
	INT baseVertex = 0;
	UINT firstInstance = 0;

	bool operator==(const DX12DirectIaFastRejectCacheKey &other) const
	{
		if (reloadGeneration != other.reloadGeneration ||
			preSkinGeneration != other.preSkinGeneration ||
			targetPlanFingerprint != other.targetPlanFingerprint ||
			iaViewFingerprint != other.iaViewFingerprint ||
			contentGeneration != other.contentGeneration ||
			resolveGeneration != other.resolveGeneration ||
			requiresIndexBuffer != other.requiresIndexBuffer ||
			requiredVertexMask != other.requiredVertexMask ||
			hasIndexBuffer != other.hasIndexBuffer ||
			indexBufferLocation != other.indexBufferLocation ||
			indexBufferSize != other.indexBufferSize ||
			indexBufferFormat != other.indexBufferFormat ||
			vertexBufferMask != other.vertexBufferMask ||
			indexed != other.indexed ||
			vertexCount != other.vertexCount ||
			indexCount != other.indexCount ||
			instanceCount != other.instanceCount ||
			firstVertex != other.firstVertex ||
			firstIndex != other.firstIndex ||
			baseVertex != other.baseVertex ||
			firstInstance != other.firstInstance)
			return false;

		// MakeKey leaves non-required slots zeroed; only required slots belong to
		// the logical key, so avoid comparing three complete 32-element arrays.
		UINT32 slots = requiredVertexMask;
		while (slots) {
			unsigned long slot = 0;
			_BitScanForward(&slot, slots);
			if (vertexBufferLocations[slot] != other.vertexBufferLocations[slot] ||
					vertexBufferSizes[slot] != other.vertexBufferSizes[slot] ||
					vertexBufferStrides[slot] != other.vertexBufferStrides[slot])
				return false;
			slots &= slots - 1;
		}
		return true;
	}
};

class DX12DirectIaFastRejectCacheKeyHash
{
public:
	static size_t Mix(size_t hash, UINT64 value)
	{
		return hash ^ (static_cast<size_t>(value) + 0x9e3779b97f4a7c15ull +
			(hash << 6) + (hash >> 2));
	}

	size_t operator()(const DX12DirectIaFastRejectCacheKey &key) const
	{
		size_t hash = 0x84222325a9582d75ull;
		hash = Mix(hash, key.reloadGeneration);
		hash = Mix(hash, key.preSkinGeneration);
		hash = Mix(hash, key.targetPlanFingerprint);
		hash = Mix(hash, key.iaViewFingerprint);
		hash = Mix(hash, key.contentGeneration);
		hash = Mix(hash, key.resolveGeneration);
		hash = Mix(hash, key.indexed ? 1 : 0);
		hash = Mix(hash, key.vertexCount);
		hash = Mix(hash, key.indexCount);
		hash = Mix(hash, key.instanceCount);
		hash = Mix(hash, key.firstVertex);
		hash = Mix(hash, key.firstIndex);
		hash = Mix(hash, static_cast<UINT64>(static_cast<INT64>(key.baseVertex)));
		return Mix(hash, key.firstInstance);
	}
};

class DX12DirectIaFastRejectCache
{
public:
	static constexpr size_t MaxEntries = 16384;

	DX12DirectIaFastRejectCache() = default;

	DX12DirectIaFastRejectCache(const DX12DirectIaFastRejectCache &)
	{
	}

	DX12DirectIaFastRejectCache &operator=(const DX12DirectIaFastRejectCache &other)
	{
		if (this != &other) {
			entries.clear();
			hasScope = false;
			scopeReloadGeneration = 0;
		}
		return *this;
	}

	DX12DirectIaFastRejectCache(DX12DirectIaFastRejectCache &&) noexcept = default;
	DX12DirectIaFastRejectCache &operator=(DX12DirectIaFastRejectCache &&) noexcept = default;

	std::unordered_set<
		DX12DirectIaFastRejectCacheKey,
		DX12DirectIaFastRejectCacheKeyHash> entries;
	size_t growthBudget = 16384;
	bool hasScope = false;
	UINT64 scopeReloadGeneration = 0;
};

class DX12DirectIaFastRejectCacheUtils
{
public:
	static DX12DirectIaFastRejectCache &ThreadCache()
	{
		static thread_local DX12DirectIaFastRejectCache cache;
		return cache;
	}

	static UINT64 MixViewValue(UINT64 fingerprint, UINT64 value)
	{
		return fingerprint ^ (value + 0x9e3779b97f4a7c15ull +
			(fingerprint << 6) + (fingerprint >> 2));
	}

	static DX12DirectIaFastRejectCacheKey MakeKey(
		UINT64 reloadGeneration,
		UINT64 preSkinGeneration,
		UINT64 contentGeneration,
		UINT64 resolveGeneration,
		const DX12ActiveIaState &ia,
		const DX12IaHashTargetPlan &targetPlan,
		const DX12IaDrawInvocation &draw)
	{
		DX12DirectIaFastRejectCacheKey key;
		key.reloadGeneration = reloadGeneration;
		key.preSkinGeneration = preSkinGeneration;
		key.targetPlanFingerprint = targetPlan.Fingerprint();
		key.contentGeneration = contentGeneration;
		key.resolveGeneration = resolveGeneration;
		key.requiresIndexBuffer = targetPlan.RequiresIndexBuffer(draw.indexed);
		UINT64 viewFingerprint = 0x84222325a9582d75ull;
		if (key.requiresIndexBuffer) {
			viewFingerprint = MixViewValue(
				viewFingerprint, ia.hasIndexBuffer ? 1 : 0);
			if (ia.hasIndexBuffer) {
				viewFingerprint = MixViewValue(
					viewFingerprint, ia.indexBuffer.BufferLocation);
				viewFingerprint = MixViewValue(
					viewFingerprint, ia.indexBuffer.SizeInBytes);
				viewFingerprint = MixViewValue(
					viewFingerprint, ia.indexBuffer.Format);
			}
		}
		key.hasIndexBuffer = key.requiresIndexBuffer && ia.hasIndexBuffer;
		if (key.hasIndexBuffer) {
			key.indexBufferLocation = ia.indexBuffer.BufferLocation;
			key.indexBufferSize = ia.indexBuffer.SizeInBytes;
			key.indexBufferFormat = ia.indexBuffer.Format;
		}
		for (UINT slot = 0; slot < ARRAYSIZE(ia.vertexBuffers); ++slot) {
			if (!targetPlan.RequiresVertexBuffer(slot))
				continue;
			key.requiredVertexMask |= 1u << slot;
			viewFingerprint = MixViewValue(viewFingerprint, slot + 1);
			viewFingerprint = MixViewValue(
				viewFingerprint, ia.hasVertexBuffer[slot] ? 1 : 0);
			if (!ia.hasVertexBuffer[slot])
				continue;
			const D3D12_VERTEX_BUFFER_VIEW &view = ia.vertexBuffers[slot];
			key.vertexBufferMask |= 1u << slot;
			key.vertexBufferLocations[slot] = view.BufferLocation;
			key.vertexBufferSizes[slot] = view.SizeInBytes;
			key.vertexBufferStrides[slot] = view.StrideInBytes;
			viewFingerprint = MixViewValue(viewFingerprint, view.BufferLocation);
			viewFingerprint = MixViewValue(viewFingerprint, view.SizeInBytes);
			viewFingerprint = MixViewValue(viewFingerprint, view.StrideInBytes);
		}
		key.iaViewFingerprint = viewFingerprint;
		key.indexed = draw.indexed;
		key.vertexCount = draw.vertexCount;
		key.indexCount = draw.indexCount;
		key.instanceCount = draw.instanceCount;
		key.firstVertex = draw.firstVertex;
		key.firstIndex = draw.firstIndex;
		key.baseVertex = draw.baseVertex;
		key.firstInstance = draw.firstInstance;
		return key;
	}

	static bool Contains(
		DX12DirectIaFastRejectCache *cache,
		const DX12DirectIaFastRejectCacheKey &key)
	{
		if (!cache)
			return false;
		PrepareScope(cache, key.reloadGeneration);
		return cache->entries.find(key) != cache->entries.end();
	}

	static bool Store(
		DX12DirectIaFastRejectCache *cache,
		DX12DirectIaFastRejectCacheKey key)
	{
		if (!cache)
			return false;
		PrepareScope(cache, key.reloadGeneration);
		if (cache->entries.size() >= DX12DirectIaFastRejectCache::MaxEntries &&
		    !cache->entries.empty()) {
			cache->entries.erase(cache->entries.begin());
		}
		DX12ResidentCacheGrowthUtils::EnsureBudget(
			&cache->growthBudget, &cache->entries,
			(std::min)(cache->entries.size() + 1, DX12DirectIaFastRejectCache::MaxEntries));
		return cache->entries.insert(std::move(key)).second;
	}

	static void Clear(DX12DirectIaFastRejectCache *cache)
	{
		if (cache) {
			cache->entries.clear();
			cache->hasScope = false;
			cache->scopeReloadGeneration = 0;
		}
	}

private:
	static void PrepareScope(
		DX12DirectIaFastRejectCache *cache,
		UINT64 reloadGeneration)
	{
		if (cache->hasScope &&
				cache->scopeReloadGeneration == reloadGeneration)
			return;
		cache->entries.clear();
		cache->hasScope = true;
		cache->scopeReloadGeneration = reloadGeneration;
	}
};




class DX12IaHashStateCacheKey
{
public:
	UINT64 targetPlanFingerprint = 0;
	UINT64 iaViewFingerprint = 0;
	UINT64 iaContentEpoch = 0;
	UINT64 bufferContentGeneration = 0;
	UINT64 bufferResolveGeneration = 0;
	bool usesGlobalFallback = false;
	bool indexed = false;
	UINT vertexCount = 0;
	UINT indexCount = 0;
	UINT firstVertex = 0;
	UINT firstIndex = 0;
	INT baseVertex = 0;

	__forceinline bool operator==(const DX12IaHashStateCacheKey &other) const
	{
		return targetPlanFingerprint == other.targetPlanFingerprint &&
			iaViewFingerprint == other.iaViewFingerprint &&
			iaContentEpoch == other.iaContentEpoch &&
			bufferContentGeneration == other.bufferContentGeneration &&
			bufferResolveGeneration == other.bufferResolveGeneration &&
			usesGlobalFallback == other.usesGlobalFallback &&
			indexed == other.indexed &&
			vertexCount == other.vertexCount &&
			indexCount == other.indexCount &&
			firstVertex == other.firstVertex &&
			firstIndex == other.firstIndex &&
			baseVertex == other.baseVertex;
	}
};

class DX12IaHashStateCacheKeyHash
{
public:
	static __forceinline size_t Combine(size_t hash, UINT64 value)
	{
		hash ^= static_cast<size_t>(value) + 0x9e3779b97f4a7c15ull +
			(hash << 6) + (hash >> 2);
		return hash;
	}

	__forceinline size_t operator()(const DX12IaHashStateCacheKey &key) const
	{
		size_t hash = 0x84222325a9582d75ull;
		hash = Combine(hash, key.targetPlanFingerprint);
		hash = Combine(hash, key.iaViewFingerprint);
		hash = Combine(hash, key.iaContentEpoch);
		hash = Combine(hash, key.bufferContentGeneration);
		hash = Combine(hash, key.bufferResolveGeneration);
		hash = Combine(hash, key.usesGlobalFallback ? 1 : 0);
		hash = Combine(hash, key.indexed ? 1 : 0);
		hash = Combine(hash, key.vertexCount);
		hash = Combine(hash, key.indexCount);
		hash = Combine(hash, key.firstVertex);
		hash = Combine(hash, key.firstIndex);
		hash = Combine(hash, static_cast<UINT64>(static_cast<INT64>(key.baseVertex)));
		return hash;
	}
};

class DX12IaHashStateCacheEntry
{
public:
	bool hasIaHashState = false;
	DX12IaHashState state;
	std::list<DX12IaHashStateCacheKey>::iterator lruPosition;
};

class DX12IaHashKeyPrefixCacheEntry
{
public:
	bool valid = false;
	bool indexed = false;
	UINT64 iaEpoch = 0;
	UINT64 targetPlanFingerprint = 0;
	UINT64 globalContentGeneration = 0;
	UINT64 globalResolveGeneration = 0;
	bool usesGlobalFallback = false;
	DX12IaHashStateCacheKey prefix;
};

class DX12IaHashKeyPrefixCacheUtils
{
public:
	static __forceinline bool TryLoadStable(
		const DX12IaHashKeyPrefixCacheEntry &entry,
		UINT64 iaEpoch,
		UINT64 targetPlanFingerprint,
		bool indexed,
		DX12IaHashStateCacheKey *key)
	{
		if (!key || !entry.valid || entry.iaEpoch != iaEpoch ||
				entry.targetPlanFingerprint != targetPlanFingerprint ||
				entry.indexed != indexed || entry.usesGlobalFallback)
			return false;
		*key = entry.prefix;
		return true;
	}

	static __forceinline bool TryLoad(
		const DX12IaHashKeyPrefixCacheEntry &entry,
		UINT64 iaEpoch,
		UINT64 targetPlanFingerprint,
		bool indexed,
			UINT64 globalContentGeneration,
			UINT64 globalResolveGeneration,
			DX12IaHashStateCacheKey *key)
	{
		if (!key || !entry.valid || entry.iaEpoch != iaEpoch ||
				entry.targetPlanFingerprint != targetPlanFingerprint ||
				entry.indexed != indexed)
			return false;
		if (entry.usesGlobalFallback &&
				(entry.globalContentGeneration != globalContentGeneration ||
				 entry.globalResolveGeneration != globalResolveGeneration))
			return false;
		*key = entry.prefix;
		return true;
	}

	static __forceinline void Store(
		DX12IaHashKeyPrefixCacheEntry *entry,
		UINT64 iaEpoch,
		UINT64 targetPlanFingerprint,
		bool indexed,
		UINT64 globalContentGeneration,
		UINT64 globalResolveGeneration,
		const DX12IaHashStateCacheKey &prefix)
	{
		if (!entry)
			return;
		entry->iaEpoch = iaEpoch;
		entry->targetPlanFingerprint = targetPlanFingerprint;
		entry->indexed = indexed;
		entry->globalContentGeneration = globalContentGeneration;
		entry->globalResolveGeneration = globalResolveGeneration;
		entry->usesGlobalFallback = prefix.usesGlobalFallback;
		entry->prefix = prefix;
		entry->valid = true;
	}
};

class DX12IaHashStateCache
{
public:
	static constexpr size_t MaxEntries = 16384;

	DX12IaHashStateCache() = default;

	// Cache contents are derived from runtime state and do not belong in value snapshots.
	// Dropping them on copy also avoids copying list iterators into the source list.
	DX12IaHashStateCache(const DX12IaHashStateCache &)
	{
	}

	DX12IaHashStateCache &operator=(const DX12IaHashStateCache &other)
	{
		if (this != &other) {
			entries.clear();
			lruKeys.clear();
		}
		return *this;
	}

	DX12IaHashStateCache(DX12IaHashStateCache &&) noexcept = default;
	DX12IaHashStateCache &operator=(DX12IaHashStateCache &&) noexcept = default;

	std::list<DX12IaHashStateCacheKey> lruKeys;
	std::unordered_map<
		DX12IaHashStateCacheKey,
		DX12IaHashStateCacheEntry,
		DX12IaHashStateCacheKeyHash> entries;
	size_t growthBudget = 16384;
};

class DX12IaHashStateCacheUtils
{
public:
	// Command lists recorded on the same CPU thread commonly reference identical
	// immutable IA regions. The full key already carries view identity, resource
	// lifetime/content generations, target plan, and draw range, so cache ownership
	// does not need to be restricted to one command list.
	static DX12IaHashStateCache &ThreadCache()
	{
		static thread_local DX12IaHashStateCache cache;
		return cache;
	}

	static __forceinline bool TryLoad(
		DX12IaHashStateCache *cache,
		const DX12IaHashStateCacheKey &key,
		const DX12IaHashState **state,
		bool *hasIaHashState)
	{
		if (!cache || !state || !hasIaHashState)
			return false;

		auto found = cache->entries.find(key);
		if (found == cache->entries.end())
			return false;

		cache->lruKeys.splice(
			cache->lruKeys.begin(), cache->lruKeys, found->second.lruPosition);
		found->second.lruPosition = cache->lruKeys.begin();
		*hasIaHashState = found->second.hasIaHashState;
		*state = &found->second.state;
		return true;
	}

	static const DX12IaHashState *Store(
		DX12IaHashStateCache *cache,
		const DX12IaHashStateCacheKey &key,
		bool hasIaHashState,
		DX12IaHashState state,
		bool *evicted)
	{
		if (evicted)
			*evicted = false;
		if (!cache)
			return nullptr;

		auto found = cache->entries.find(key);
		if (found != cache->entries.end()) {
			cache->lruKeys.splice(
				cache->lruKeys.begin(), cache->lruKeys, found->second.lruPosition);
			found->second.lruPosition = cache->lruKeys.begin();
			found->second.hasIaHashState = hasIaHashState;
			found->second.state = std::move(state);
			return &found->second.state;
		}
		if (cache->entries.size() >= DX12IaHashStateCache::MaxEntries &&
		    !cache->lruKeys.empty()) {
			const DX12IaHashStateCacheKey &oldest = cache->lruKeys.back();
			cache->entries.erase(oldest);
			cache->lruKeys.pop_back();
			if (evicted)
				*evicted = true;
		}
		DX12ResidentCacheGrowthUtils::EnsureBudget(
			&cache->growthBudget, &cache->entries,
			(std::min)(cache->entries.size() + 1, DX12IaHashStateCache::MaxEntries));

		cache->lruKeys.push_front(key);
		DX12IaHashStateCacheEntry entry;
		entry.hasIaHashState = hasIaHashState;
		entry.state = std::move(state);
		entry.lruPosition = cache->lruKeys.begin();
		auto inserted = cache->entries.emplace(key, std::move(entry));
		return inserted.second ? &inserted.first->second.state : nullptr;
	}

	static void Clear(DX12IaHashStateCache *cache)
	{
		if (!cache)
			return;
		cache->entries.clear();
		cache->lruKeys.clear();
	}
};




// Exact, command-list-local negative result. Unlike the legacy unordered cache,
// this key uses the IA mutation epoch as the identity of all bound views. The
// global generations cover writes/resource reuse, so no content-hash result is
// reused after any dependency can have changed.
class DX12ResidentDrawNegativeEntry
{
public:
	ID3D12PipelineState *pipelineState = nullptr;
	UINT64 reloadGeneration = 0;
	UINT64 preSkinGeneration = 0;
	UINT64 contentGeneration = 0;
	UINT64 resolveGeneration = 0;
	UINT64 iaEpoch = 0;
	UINT64 graphicsDescriptorQuerySerial = 0;
	ID3D12DescriptorHeap *graphicsDescriptorHeap = nullptr;
	UINT64 graphicsDescriptorHeapVersion = 0;
	UINT64 targetPlanFingerprint = 0;
	UINT vertexCount = 0;
	UINT indexCount = 0;
	UINT instanceCount = 0;
	UINT firstVertex = 0;
	UINT firstIndex = 0;
	INT baseVertex = 0;
	UINT firstInstance = 0;
	bool indexed = false;
	bool valid = false;
};

// Top-level draw-result cache that captures the full bound-state identity.
// When nothing changes (PSO / IA / root descriptors / heap), the per-draw
// override decision must be identical. A hit here skips the entire multi-layer
// dynamic decision chain (IA hash, descriptor match, resource hash, etc.).
class DX12BoundDrawResultCache
{
public:
	bool valid = false;
	bool hasOverride = false;
	UINT64 iaEpoch = 0;
	UINT64 iaContentEpoch = 0;
	UINT64 graphicsDescriptorQuerySerial = 0;
	ID3D12PipelineState *pipelineState = nullptr;
	UINT64 reloadGeneration = 0;
	UINT64 preSkinGeneration = 0;
	ID3D12DescriptorHeap *cbvSrvUavHeap = nullptr;
	UINT64 cbvSrvUavHeapVersion = 0;
	ID3D12RootSignature *graphicsRootSignature = nullptr;
	bool indexed = false;
	UINT vertexCount = 0;
	UINT indexCount = 0;
	UINT instanceCount = 0;
	UINT firstVertex = 0;
	UINT firstIndex = 0;
	INT baseVertex = 0;
	UINT firstInstance = 0;

	void Reset()
	{
		valid = false;
		hasOverride = false;
	}

	bool Matches(
		UINT64 currentIaEpoch,
		UINT64 currentIaContentEpoch,
		UINT64 currentGraphicsDescriptorQuerySerial,
		ID3D12PipelineState *currentPso,
		UINT64 currentReloadGeneration,
		UINT64 currentPreSkinGeneration,
		ID3D12DescriptorHeap *currentHeap,
		UINT64 currentHeapVersion,
		ID3D12RootSignature *currentRootSig,
		const DX12IaDrawInvocation &draw,
		bool compareDescriptorState,
		bool compareDrawArgs) const
	{
		return valid &&
			iaEpoch == currentIaEpoch &&
			iaContentEpoch == currentIaContentEpoch &&
			pipelineState == currentPso &&
			reloadGeneration == currentReloadGeneration &&
			(!compareDescriptorState ||
				(graphicsDescriptorQuerySerial == currentGraphicsDescriptorQuerySerial &&
				 preSkinGeneration == currentPreSkinGeneration &&
				 cbvSrvUavHeap == currentHeap &&
				 cbvSrvUavHeapVersion == currentHeapVersion &&
				 graphicsRootSignature == currentRootSig)) &&
			(!compareDrawArgs ||
				(indexed == draw.indexed &&
				 vertexCount == draw.vertexCount &&
				 indexCount == draw.indexCount &&
				 instanceCount == draw.instanceCount &&
				 firstVertex == draw.firstVertex &&
				 firstIndex == draw.firstIndex &&
				 baseVertex == draw.baseVertex &&
				 firstInstance == draw.firstInstance));
	}

	void Capture(
		UINT64 currentIaEpoch,
		UINT64 currentIaContentEpoch,
		UINT64 currentGraphicsDescriptorQuerySerial,
		ID3D12PipelineState *currentPso,
		UINT64 currentReloadGeneration,
		UINT64 currentPreSkinGeneration,
		ID3D12DescriptorHeap *currentHeap,
		UINT64 currentHeapVersion,
		ID3D12RootSignature *currentRootSig,
		const DX12IaDrawInvocation &draw,
		bool overrideResult)
	{
		valid = true;
		hasOverride = overrideResult;
		iaEpoch = currentIaEpoch;
		iaContentEpoch = currentIaContentEpoch;
		graphicsDescriptorQuerySerial = currentGraphicsDescriptorQuerySerial;
		pipelineState = currentPso;
		reloadGeneration = currentReloadGeneration;
		preSkinGeneration = currentPreSkinGeneration;
		cbvSrvUavHeap = currentHeap;
		cbvSrvUavHeapVersion = currentHeapVersion;
		graphicsRootSignature = currentRootSig;
		indexed = draw.indexed;
		vertexCount = draw.vertexCount;
		indexCount = draw.indexCount;
		instanceCount = draw.instanceCount;
		firstVertex = draw.firstVertex;
		firstIndex = draw.firstIndex;
		baseVertex = draw.baseVertex;
		firstInstance = draw.firstInstance;
	}
};

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
		UINT64 reloadGeneration = 0;
		UINT64 targetPlanFingerprint = 0;
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
	static void EnsureScene(DX12SimulateContextDrawTemplate *state);
	static void NoteLookup(
		DX12SimulateContextDrawTemplate *state,
		bool hit);
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
	std::vector<DX12CurrentComputeUavBinding> uavs;
	std::vector<DX12CurrentComputeUavBinding> srvs;
	std::vector<DX12CurrentComputeUavBinding> cbvs;
	std::vector<DX12CurrentRootConstants> rootConstants;

	UINT64 bindingFingerprintDependency = 0;
	UINT64 bindingFingerprint = 0;
	UINT64 probeBindingDependencyFingerprint = 0;
	UINT64 probeFingerprint = 0;
	uint64_t probeCsHash = 0;
	UINT64 probeReloadGeneration = 0;
	UINT64 probeBindingSerial = 0;
	UINT64 probeIaContentEpoch = 0;
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

	// Per-PSO override cache aligned with HotPathState generation.
	volatile LONG psoHasOverridesGeneration = -1;
	bool psoHasShaderOverrides = true;

	// Replacement PSO cache. First use may query global records; repeated use is direct.
	ID3D12PipelineState *cachedReplacementPsoKey = nullptr;
	ID3D12PipelineState *cachedReplacementPso = nullptr;

	// Compute shader hash cache, equivalent to DX11 current compute shader state.
	uint64_t cachedCsHash = 0;
	bool cachedCsHashValid = false;
	bool cachedCsHashResolved = false;

	// Stable compute binding fingerprint used as a dispatch probe cache key.
	uint64_t computeBindingHash = 0;

	// Compiled per-PSO dispatch policy. Dispatch consumes this instead of
	// rediscovering non-candidate PreSkin state on every command.
	DX12SimulateContextDispatchExecutionTemplate dispatchDecision;

	// Compiled per-PSO draw policy. Draw consumes this instead of rediscovering
	// PSO shader override metadata on every command.
	DX12SimulateContextDrawExecutionTemplate drawDecision;

	// Static replacement template cache, deliberately independent of
	// drawDecision: drawDecision.Reset() runs on every PSO transition, which
	// would discard an otherwise reusable replacement template (the game
	// changes PSO thousands of times per frame, so the template would be
	// regenerated on every encounter of the same PSO). Keyed by PSO pointer +
	// reload generation; a stale PSO pointer is only ever compared, never
	// dereferenced, matching the existing drawDecision.pipelineState pattern.
	ID3D12PipelineState *staticReplacementPso = nullptr;
	UINT64 staticReplacementReloadGeneration = 0;
	std::shared_ptr<const DX12ModIaReplacement> staticReplacementSnapshot;

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




class DX12CommandListRuntimeShard
{
public:
	SRWLOCK lock = SRWLOCK_INIT;
	std::unordered_map<ID3D12GraphicsCommandList*, DX12SimulateContext*> directStates;
	std::unordered_map<ID3D12GraphicsCommandList*, DX12SimulateContext> states;
};



class DX12CommandListRuntimeTlsCache
{
public:
	// Per-thread command-list state cache. Games commonly interleave 3+
	// command lists on one recording thread (scene / shadow / UI / compute);
	// a single "previous" slot forced a shard lock + unordered_map lookup on
	// every alternation past two lists. A small move-to-front history keeps
	// the common interleave sets on the lock-free path: the hot slot is
	// checked first (two loads), then the history slots are scanned linearly
	// (cheap compared to the locked fallback).
	static constexpr size_t kHistorySlotCount = 7;

	class Slot
	{
	public:
		ID3D12GraphicsCommandList *commandList = nullptr;
		DX12CommandListRuntimeShard *shard = nullptr;
		bool stateIsDirect = false;
		UINT64 trackingGeneration = 0;
		DX12CommandListTrackingCache tracking;
		DX12SimulateContext *statePtr = nullptr;
	};

	// Hot slot, kept as direct members so the common single-list fast path
	// stays a couple of loads and compares.
	ID3D12GraphicsCommandList *commandList = nullptr;
	DX12CommandListRuntimeShard *shard = nullptr;
	bool stateIsDirect = false;
	UINT64 trackingGeneration = 0;
	DX12CommandListTrackingCache tracking;
	DX12SimulateContext *statePtr = nullptr;

	// History slots for additional interleaved command lists. A hit swaps
	// the matching slot into the hot position (move-to-front); a demoted hot
	// entry overwrites slots[nextSlot] ring-style, preferring an existing
	// entry for the same list so each list appears at most once.
	Slot slots[kHistorySlotCount];
	UINT32 nextSlot = 0;

	void SwapHotWith(Slot &slot)
	{
		ID3D12GraphicsCommandList *tmpCommandList = commandList;
		commandList = slot.commandList;
		slot.commandList = tmpCommandList;

		DX12CommandListRuntimeShard *tmpShard = shard;
		shard = slot.shard;
		slot.shard = tmpShard;

		bool tmpStateIsDirect = stateIsDirect;
		stateIsDirect = slot.stateIsDirect;
		slot.stateIsDirect = tmpStateIsDirect;

		UINT64 tmpTrackingGeneration = trackingGeneration;
		trackingGeneration = slot.trackingGeneration;
		slot.trackingGeneration = tmpTrackingGeneration;

		DX12CommandListTrackingCache tmpTracking = tracking;
		tracking = slot.tracking;
		slot.tracking = tmpTracking;

		DX12SimulateContext *tmpStatePtr = statePtr;
		statePtr = slot.statePtr;
		slot.statePtr = tmpStatePtr;
	}

	void ClearHot()
	{
		commandList = nullptr;
		shard = nullptr;
		stateIsDirect = false;
		trackingGeneration = 0;
		statePtr = nullptr;
	}
};




class DX12CommandListRuntime
{
public:
	static constexpr size_t kRuntimeShardCount = 64;

	static DX12CommandListRuntimeShard Shards[kRuntimeShardCount];
	static volatile UINT64 TrackingGenerationValue;
	static thread_local DX12CommandListRuntimeTlsCache Tls;

	static void RegisterDirectState(ID3D12GraphicsCommandList *commandList, DX12SimulateContext *state);
	static void UnregisterDirectState(ID3D12GraphicsCommandList *commandList);
	static DX12SimulateContext *GetDirectStatePtr(ID3D12GraphicsCommandList *commandList);
	static IUnknown *GetCommandListIdentity(ID3D12GraphicsCommandList *commandList);

	static UINT64 TrackingGeneration();
	static void BumpTrackingGeneration();
	static DX12CommandListRuntimeShard &ShardFor(ID3D12GraphicsCommandList *commandList);
	static void RefreshTrackingCache(DX12CommandListTrackingCache &cache);
	static DX12CommandListTrackingState MakeTrackingState(const DX12CommandListTrackingCache &cache);
	static DX12CommandListTrackingState GetTrackingState(DX12SimulateContext *state);
	static void TlsRefreshFromState(
		DX12SimulateContext *state,
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeShard *shard,
		bool stateIsDirect);
	static DX12SimulateContext *TlsLookup(ID3D12GraphicsCommandList *commandList);
	static void TlsRefresh(DX12CommandListRuntimeShard &shard, ID3D12GraphicsCommandList *commandList);
	static void GetTrackingCacheFast(ID3D12GraphicsCommandList *commandList, DX12CommandListTrackingCache &out);
	static DX12CommandListTrackingState GetTrackingState(ID3D12GraphicsCommandList *commandList);
	static DX12SimulateContext *GetStatePtrFast(ID3D12GraphicsCommandList *commandList);
	static void TlsInvalidateIfMatch(ID3D12GraphicsCommandList *commandList);

	static void Register(ID3D12GraphicsCommandList *commandList);
	static bool Unregister(ID3D12GraphicsCommandList *commandList);
	static void Reset(DX12SimulateContext *state, ID3D12PipelineState *initialState);
	static void Reset(ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *initialState);
	static DX12CompiledDispatchDecision BuildDispatchDecision(
		ID3D12PipelineState *pipelineState,
		bool hasShaderOverrides,
		bool needsPreSkinProbe);
	static DX12CompiledDrawDecision BuildDrawDecision(
		ID3D12PipelineState *pipelineState,
		bool hasShaderOverrides);
	static const DX12CompiledDispatchDecision *EnsureDispatchDecision(
		DX12SimulateContext *state,
		bool hasShaderOverrides,
		bool needsPreSkinProbe);
	static const DX12CompiledDrawDecision *EnsureDrawDecision(
		DX12SimulateContext *state,
		bool hasShaderOverrides);
	static void CompileDispatchDecision(
		DX12SimulateContext *state,
		bool hasShaderOverrides,
		bool needsPreSkinProbe);
	static void CompileDrawDecision(
		DX12SimulateContext *state,
		bool hasShaderOverrides);
	static void InvalidateResolvedDrawPlan(DX12SimulateContext *state);
	static void RememberPipelineState(DX12SimulateContext *state, ID3D12PipelineState *pipelineState);
	static void RememberPipelineState(ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState);
	static void ResetIa(DX12SimulateContext *state);
	static void ResetIa(ID3D12GraphicsCommandList *commandList);
	static bool RememberIndexBuffer(DX12SimulateContext *state, const D3D12_INDEX_BUFFER_VIEW *view);
	static bool RememberIndexBuffer(ID3D12GraphicsCommandList *commandList, const D3D12_INDEX_BUFFER_VIEW *view);
	static bool RememberVertexBuffers(DX12SimulateContext *state, UINT startSlot, UINT count, const D3D12_VERTEX_BUFFER_VIEW *views);
	static bool RememberVertexBuffers(ID3D12GraphicsCommandList *commandList, UINT startSlot, UINT count, const D3D12_VERTEX_BUFFER_VIEW *views);
	static bool RememberPrimitiveTopology(DX12SimulateContext *state, D3D12_PRIMITIVE_TOPOLOGY topology);
	static bool RememberPrimitiveTopology(ID3D12GraphicsCommandList *commandList, D3D12_PRIMITIVE_TOPOLOGY topology);
	static void BumpIaEpoch(DX12SimulateContext *state);
	static void BumpIaContentEpoch(DX12SimulateContext *state);
	static void BumpIaContentEpoch(ID3D12GraphicsCommandList *commandList);
	static bool BuildIaHashState(const DX12ActiveIaState &ia, bool indexed, UINT vertexCount, UINT indexCount, UINT firstVertex, UINT firstIndex, INT baseVertex, DX12IaHashState *state);
	static bool BuildIaHashStateForTargets(const DX12ActiveIaState &ia, const DX12IaHashTargetPlan &targetPlan, bool indexed, UINT vertexCount, UINT indexCount, UINT firstVertex, UINT firstIndex, INT baseVertex, DX12IaHashState *state);
	static bool BuildIaHashStateForTargetsCached(DX12ActiveIaState *ia, const DX12IaHashTargetPlan &targetPlan, bool indexed, UINT vertexCount, UINT indexCount, UINT firstVertex, UINT firstIndex, INT baseVertex, DX12IaHashState *state);
	static DX12IaHashStateCacheKey BuildIaHashStateCacheKeyForTargets(const DX12ActiveIaState &ia, UINT64 iaContentEpoch, const DX12IaHashTargetPlan &targetPlan, bool indexed, UINT vertexCount, UINT indexCount, UINT firstVertex, UINT firstIndex, INT baseVertex);

	// Cross-TU cached per-view IA hash builders (DX11-style: hash saved at bind
	// time, reused at draw time, invalidated by content generation).
	static uint32_t GetOrBuildCachedIndexViewHash(DX12ActiveIaState *ia);
	static uint32_t GetOrBuildCachedVertexViewHash(DX12ActiveIaState *ia, UINT slot);
	static DX12IaHashStateCacheKey BuildIaHashStateCacheKeyForTargetsCached(DX12SimulateContext *state, UINT cacheSlot, const DX12IaHashTargetPlan &targetPlan, bool indexed, UINT vertexCount, UINT indexCount, UINT firstVertex, UINT firstIndex, INT baseVertex);
	static ID3D12PipelineState *GetPipelineState(ID3D12GraphicsCommandList *commandList);
	static DX12SimulateContext GetState(ID3D12GraphicsCommandList *commandList);
	static const DX12SimulateContext *GetStatePtr(ID3D12GraphicsCommandList *commandList);
	static void SetDescriptorHeaps(DX12SimulateContext *state, ID3D12DescriptorHeap *cbvSrvUavHeap, ID3D12DescriptorHeap *samplerHeap);
	static void SetDescriptorHeaps(ID3D12GraphicsCommandList *commandList, ID3D12DescriptorHeap *cbvSrvUavHeap, ID3D12DescriptorHeap *samplerHeap);
	static bool RememberGraphicsRootSignature(DX12SimulateContext *state, ID3D12RootSignature *rootSignature);
	static bool RememberGraphicsRootSignature(ID3D12GraphicsCommandList *commandList, ID3D12RootSignature *rootSignature);
	static bool RememberComputeRootSignature(DX12SimulateContext *state, ID3D12RootSignature *rootSignature);
	static bool RememberComputeRootSignature(ID3D12GraphicsCommandList *commandList, ID3D12RootSignature *rootSignature);
	static bool RememberRootDescriptorTable(DX12SimulateContext *state, bool graphics, UINT rootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor);
	static bool RememberRootDescriptorTable(ID3D12GraphicsCommandList *commandList, bool graphics, UINT rootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor);
	static void RememberGraphicsRootDescriptor(DX12SimulateContext *state, UINT rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address);
	static void RememberGraphicsRootDescriptor(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address);
	static void RememberComputeRootDescriptor(DX12SimulateContext *state, UINT rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address);
	static void RememberComputeRootDescriptor(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address);
	static void RememberGraphicsRoot32BitConstants(DX12SimulateContext *state, UINT rootParameterIndex, UINT destOffset, UINT count, const void *values);
	static void RememberGraphicsRoot32BitConstants(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT destOffset, UINT count, const void *values);
	static void RememberComputeRoot32BitConstants(DX12SimulateContext *state, UINT rootParameterIndex, UINT destOffset, UINT count, const void *values);
	static void RememberComputeRoot32BitConstants(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT destOffset, UINT count, const void *values);
	static bool GetDescriptorHeaps(ID3D12GraphicsCommandList *commandList, ID3D12DescriptorHeap **cbvSrvUavHeap, ID3D12DescriptorHeap **samplerHeap);
};



class DX12CommandListStateCapture
{
public:
	static bool ShouldTrackBindings();
	static bool ShouldTrackIaBindings();
	static bool ShouldTrackPipelineStateBindings();
	static bool ShouldTrackResourceStates();
	static bool ShouldRecordBindingEvents();
	static bool ShouldTrackHuntIa();
	static bool ShouldTrackPsoState();
	static DX12CommandListTrackingState MakeTrackingState();
	static DX12CommandListTrackingState GetTrackingState(DX12SimulateContext *runtimeState);
	static DX12CommandListTrackingState GetTrackingState(ID3D12GraphicsCommandList *commandList);
	static DX12CommandListTrackingState GetTrackingState(ID3D12GraphicsCommandList *commandList, DX12SimulateContext *runtimeState);

	static void PrimitiveTopology(ID3D12GraphicsCommandList *commandList, D3D12_PRIMITIVE_TOPOLOGY topology);
	static void PrimitiveTopology(ID3D12GraphicsCommandList *commandList, DX12SimulateContext *runtimeState, D3D12_PRIMITIVE_TOPOLOGY topology);
	static ID3D12PipelineState *PipelineState(ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState);
	static ID3D12PipelineState *PipelineState(ID3D12GraphicsCommandList *commandList, DX12SimulateContext *runtimeState, ID3D12PipelineState *pipelineState);
	static void DescriptorHeaps(ID3D12GraphicsCommandList *commandList, UINT count, ID3D12DescriptorHeap *const *heaps);
	static void DescriptorHeaps(ID3D12GraphicsCommandList *commandList, DX12SimulateContext *runtimeState, UINT count, ID3D12DescriptorHeap *const *heaps);
	static void DescriptorHeaps(ID3D12GraphicsCommandList *commandList, ID3D12DescriptorHeap *cbvSrvUavHeap, ID3D12DescriptorHeap *samplerHeap);
	static void DescriptorHeaps(ID3D12GraphicsCommandList *commandList, DX12SimulateContext *runtimeState, ID3D12DescriptorHeap *cbvSrvUavHeap, ID3D12DescriptorHeap *samplerHeap);
	static void ComputeRootSignature(ID3D12GraphicsCommandList *commandList, ID3D12RootSignature *rootSignature);
	static void ComputeRootSignature(ID3D12GraphicsCommandList *commandList, DX12SimulateContext *runtimeState, ID3D12RootSignature *rootSignature);
	static void GraphicsRootSignature(ID3D12GraphicsCommandList *commandList, ID3D12RootSignature *rootSignature);
	static void GraphicsRootSignature(ID3D12GraphicsCommandList *commandList, DX12SimulateContext *runtimeState, ID3D12RootSignature *rootSignature);
	static void ComputeRootDescriptorTable(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor);
	static void ComputeRootDescriptorTable(ID3D12GraphicsCommandList *commandList, DX12SimulateContext *runtimeState, UINT rootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor);
	static void GraphicsRootDescriptorTable(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor);
	static void GraphicsRootDescriptorTable(ID3D12GraphicsCommandList *commandList, DX12SimulateContext *runtimeState, UINT rootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor);
	static void ComputeRoot32BitConstant(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT srcData, UINT destOffset);
	static void ComputeRoot32BitConstant(ID3D12GraphicsCommandList *commandList, DX12SimulateContext *runtimeState, UINT rootParameterIndex, UINT srcData, UINT destOffset);
	static void GraphicsRoot32BitConstant(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT srcData, UINT destOffset);
	static void GraphicsRoot32BitConstant(ID3D12GraphicsCommandList *commandList, DX12SimulateContext *runtimeState, UINT rootParameterIndex, UINT srcData, UINT destOffset);
	static void ComputeRoot32BitConstants(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT num32BitValuesToSet, const void *srcData, UINT destOffset);
	static void ComputeRoot32BitConstants(ID3D12GraphicsCommandList *commandList, DX12SimulateContext *runtimeState, UINT rootParameterIndex, UINT num32BitValuesToSet, const void *srcData, UINT destOffset);
	static void GraphicsRoot32BitConstants(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT num32BitValuesToSet, const void *srcData, UINT destOffset);
	static void GraphicsRoot32BitConstants(ID3D12GraphicsCommandList *commandList, DX12SimulateContext *runtimeState, UINT rootParameterIndex, UINT num32BitValuesToSet, const void *srcData, UINT destOffset);
	static void ComputeRootDescriptor(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address);
	static void ComputeRootDescriptor(ID3D12GraphicsCommandList *commandList, DX12SimulateContext *runtimeState, UINT rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address);
	static void GraphicsRootDescriptor(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address);
	static void GraphicsRootDescriptor(ID3D12GraphicsCommandList *commandList, DX12SimulateContext *runtimeState, UINT rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address);
	static void IndexBuffer(ID3D12GraphicsCommandList *commandList, const D3D12_INDEX_BUFFER_VIEW *view);
	static void IndexBuffer(ID3D12GraphicsCommandList *commandList, DX12SimulateContext *runtimeState, const D3D12_INDEX_BUFFER_VIEW *view);
	static void VertexBuffers(ID3D12GraphicsCommandList *commandList, UINT startSlot, UINT count, const D3D12_VERTEX_BUFFER_VIEW *views);
	static void VertexBuffers(ID3D12GraphicsCommandList *commandList, DX12SimulateContext *runtimeState, UINT startSlot, UINT count, const D3D12_VERTEX_BUFFER_VIEW *views);
};



class DX12FrameAnalysisState
{
public:
	inline static SRWLOCK lock = SRWLOCK_INIT;
	inline static bool active = false;
	inline static bool captureRequested = false;
	inline static bool capturing = false;
	inline static bool dumpPending = false;
	inline static UINT captureFramesRemaining = 0;
	inline static volatile LONG fastStateFlags = 0;
	inline static wchar_t path[MAX_PATH] = {};
	inline static FILE *log = nullptr;
	inline static unsigned long long lineNo = 0;
	inline static constexpr UINT CaptureFramesPerDump = 1;
	static constexpr LONG FLAG_ACTIVE = 1 << 0;
	static constexpr LONG FLAG_CAPTURE_REQUESTED = 1 << 1;
	static constexpr LONG FLAG_CAPTURING = 1 << 2;

	static void PublishFastStateLocked()
	{
		LONG flags = 0;
		if (active)
			flags |= FLAG_ACTIVE;
		if (captureRequested)
			flags |= FLAG_CAPTURE_REQUESTED;
		if (capturing)
			flags |= FLAG_CAPTURING;
		InterlockedExchange(&fastStateFlags, flags);
	}

	static LONG GetFastFlags()
	{
		return InterlockedCompareExchange(&fastStateFlags, 0, 0);
	}

	static bool Begin()
	{
		wchar_t modulePath[MAX_PATH];
		wchar_t subdir[MAX_PATH];
		__time64_t now;
		struct tm localTime;

		if (!GetModuleFileNameW(DX12RuntimeState::GetModule(), modulePath, ARRAYSIZE(modulePath)))
			return false;
		PathRemoveFileSpecW(modulePath);

		_time64(&now);
		if (_localtime64_s(&localTime, &now) != 0)
			return false;
		wcsftime(subdir, ARRAYSIZE(subdir), L"FrameAnalysis-%Y-%m-%d-%H%M%S", &localTime);
		PathAppendW(modulePath, subdir);

		if (!CreateDirectoryW(modulePath, nullptr)) {
			DWORD error = GetLastError();
			DX12LogManager::Text(
				"Error creating frame analysis directory: %lu path=%S\n", error, modulePath);
			return false;
		}

		AcquireSRWLockExclusive(&lock);
		CloseLogLocked();
		wcsncpy_s(path, modulePath, _TRUNCATE);
		lineNo = 0;
		active = true;
		captureRequested = false;
		capturing = false;
		dumpPending = false;
		captureFramesRemaining = 0;
		PublishFastStateLocked();
		ReleaseSRWLockExclusive(&lock);

		DX12LogManager::Text("Frame analysis directory: %S\n", modulePath);
		return true;
	}

	static void End()
	{
		AcquireSRWLockExclusive(&lock);
		active = false;
		captureRequested = false;
		capturing = false;
		dumpPending = false;
		captureFramesRemaining = 0;
		PublishFastStateLocked();
		CloseLogLocked();
		ReleaseSRWLockExclusive(&lock);
	}

	static void RequestCapture()
	{
		AcquireSRWLockExclusive(&lock);
		if (active && !capturing && !dumpPending) {
			captureRequested = true;
			PublishFastStateLocked();
		}
		ReleaseSRWLockExclusive(&lock);
	}

	static void BeginCapture()
	{
		AcquireSRWLockExclusive(&lock);
		if (active && captureRequested && !capturing && !dumpPending) {
			captureRequested = false;
			capturing = true;
			captureFramesRemaining = CaptureFramesPerDump;
			PublishFastStateLocked();
		}
		ReleaseSRWLockExclusive(&lock);
	}

	static bool EndCapture()
	{
		AcquireSRWLockExclusive(&lock);
		if (active && capturing) {
			if (captureFramesRemaining > 1) {
				captureFramesRemaining--;
				ReleaseSRWLockExclusive(&lock);
				return false;
			}
			capturing = false;
			dumpPending = true;
			captureFramesRemaining = 0;
			PublishFastStateLocked();
			ReleaseSRWLockExclusive(&lock);
			return true;
		}
		ReleaseSRWLockExclusive(&lock);
		return false;
	}

	static bool IsActive()
	{
		return (GetFastFlags() & FLAG_ACTIVE) != 0;
	}

	static bool IsCaptureRequested()
	{
		return (GetFastFlags() & FLAG_CAPTURE_REQUESTED) != 0;
	}

	static bool IsCapturing()
	{
		return (GetFastFlags() & FLAG_CAPTURING) != 0;
	}

	static bool GetPath(wchar_t *outPath, size_t pathCount)
	{
		if (!outPath || pathCount == 0)
			return false;

		AcquireSRWLockShared(&lock);
		bool ok = path[0] != 0;
		if (ok)
			wcsncpy_s(outPath, pathCount, path, _TRUNCATE);
		ReleaseSRWLockShared(&lock);
		return ok;
	}

	static bool OpenLogLocked()
	{
		wchar_t filename[MAX_PATH];

		if (log)
			return true;
		if (!active || !path[0])
			return false;

		swprintf_s(filename, L"%s\\log.jsonl", path);
		log = _wfsopen(filename, L"w", _SH_DENYNO);
		if (!log) {
			DX12LogManager::Text("Error opening frame analysis log: %S\n", filename);
			return false;
		}

		fprintf(log,
			"{\"index\":0,\"func\":\"Header\",\"schema\":\"dx12-fa-log/3\",\"present_count_at_start\":%ld}\n",
			DX12RuntimeState::GetPresentCount());
		return true;
	}

	static void CloseLogLocked()
	{
		if (log) {
			fclose(log);
			log = nullptr;
		}
	}

	static void LogJsonFields(const char *fields)
	{
		AcquireSRWLockExclusive(&lock);
		if (!active || !OpenLogLocked()) {
			ReleaseSRWLockExclusive(&lock);
			return;
		}

		fprintf(log, "{\"index\":%llu,%s}\n", ++lineNo,
			fields ? fields : "\"func\":\"Unknown\"");
		fflush(log);
		ReleaseSRWLockExclusive(&lock);
	}
};



bool DX12ShaderDumpIsCapturingFrame();
bool DX12ShaderDumpIsCaptureRequested();
bool DX12ShaderDumpIsBusy();
bool DX12HuntIsEnabled();
bool DX12HuntNeedsHotPath();
bool DX12ModHasActiveShaderOverrides();
bool DX12ModNeedsPresentReplacement();
bool DX12ModPresentReplacementPendingForCurrentPresent();
bool DX12ModNeedsPreSkinningUavProbe();
bool DX12ModNeedsPreSkinningBindingTracking();
bool DX12ModNeedsShaderDescriptorTracking();
UINT64 DX12ModGetReloadGenerationFast();
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
enum DX12ModHotPathFlagBits : UINT32
{
	DX12_MOD_HOT_SHADER_OVERRIDES = 1u << 0,
	DX12_MOD_HOT_PRESENT_REPLACEMENT = 1u << 1,
	DX12_MOD_HOT_PRE_SKIN_PROBE = 1u << 2,
	DX12_MOD_HOT_PRE_SKIN_BINDING_TRACK = 1u << 3,
	DX12_MOD_HOT_SHADER_DESCRIPTOR_TRACK = 1u << 4,
	DX12_MOD_HOT_IA_RESIDENT_STATE = 1u << 5,
	DX12_MOD_HOT_DRAW_SRV_SLOT_REPLACEMENT = 1u << 6
};
UINT32 DX12ModGetHotPathFlags();
void DX12ActivateCommandListHotPathHooks();

class DX12HotPathState
{
public:
	inline static volatile LONG skipAll = 1;
	inline static volatile LONG skipBindings = 1;
	inline static volatile LONG skipGraphicsBindings = 1;
	inline static volatile LONG trackResourceMetadata = 0;
	inline static volatile LONG stateMask = -1;

	// A single flags word replaces nine independent hot-path checks.
	// Update publishes it from Present-time frame actions and state transitions.
	inline static volatile UINT64 HookWorkFlags = 0;

	// Individual work bits.
	static constexpr UINT64 FLAG_NEEDS_MOD_WORK          = 1ull << 0;
	static constexpr UINT64 FLAG_HUNT_ENABLED            = 1ull << 1;
	static constexpr UINT64 FLAG_FRAME_ANALYSIS_ACTIVE   = 1ull << 2;
	static constexpr UINT64 FLAG_SHADER_DUMP_ACTIVE      = 1ull << 3;
	static constexpr UINT64 FLAG_PRE_SKIN_PROBE          = 1ull << 4;
	static constexpr UINT64 FLAG_PRE_SKIN_BINDING_TRACK  = 1ull << 5;
	static constexpr UINT64 FLAG_SHADER_OVERRIDES         = 1ull << 6;
	static constexpr UINT64 FLAG_PRESENT_REPLACEMENT      = 1ull << 7;
	static constexpr UINT64 FLAG_IA_RESIDENT_STATE        = 1ull << 8;
	static constexpr UINT64 FLAG_SHADER_DESCRIPTOR_TRACK  = 1ull << 9;
	static constexpr UINT64 FLAG_DRAW_SRV_SLOT_REPLACEMENT = 1ull << 10;

	// Combined masks allow one branch for draw or dispatch hook work.
	static constexpr UINT64 MASK_DRAW_WORK =
		FLAG_NEEDS_MOD_WORK | FLAG_HUNT_ENABLED |
		FLAG_FRAME_ANALYSIS_ACTIVE | FLAG_SHADER_DUMP_ACTIVE;
	static constexpr UINT64 MASK_DISPATCH_WORK =
		MASK_DRAW_WORK | FLAG_PRE_SKIN_PROBE;

	class SourceState
	{
	public:
		bool huntEnabled = false;
		bool frameAnalysisActive = false;
		bool shaderDumpActive = false;
		bool shaderOverrides = false;
		bool presentReplacement = false;
		bool preSkinProbe = false;
		bool preSkinBindingTrack = false;
		bool shaderDescriptorTrack = false;
		bool iaResidentState = false;
		bool drawSrvSlotReplacement = false;

		static SourceState Capture()
		{
			SourceState state;
			state.huntEnabled = DX12HuntNeedsHotPath();
			state.frameAnalysisActive = DX12FrameAnalysisState::IsActive();
			state.shaderDumpActive = DX12ShaderDumpIsBusy();
			const UINT32 modFlags = DX12ModGetHotPathFlags();
			state.shaderOverrides =
				(modFlags & DX12_MOD_HOT_SHADER_OVERRIDES) != 0;
			state.presentReplacement =
				(modFlags & DX12_MOD_HOT_PRESENT_REPLACEMENT) != 0;
			state.preSkinProbe =
				(modFlags & DX12_MOD_HOT_PRE_SKIN_PROBE) != 0;
			state.preSkinBindingTrack =
				(modFlags & DX12_MOD_HOT_PRE_SKIN_BINDING_TRACK) != 0;
			state.shaderDescriptorTrack =
				(modFlags & DX12_MOD_HOT_SHADER_DESCRIPTOR_TRACK) != 0;
			state.iaResidentState =
				(modFlags & DX12_MOD_HOT_IA_RESIDENT_STATE) != 0;
			state.drawSrvSlotReplacement =
				(modFlags & DX12_MOD_HOT_DRAW_SRV_SLOT_REPLACEMENT) != 0;
			return state;
		}

		UINT64 BuildHookWorkFlags() const
		{
			UINT64 flags = 0;
			if (NeedsModDrawWork())
				flags |= FLAG_NEEDS_MOD_WORK;
			if (shaderOverrides)
				flags |= FLAG_SHADER_OVERRIDES;
			if (presentReplacement)
				flags |= FLAG_PRESENT_REPLACEMENT;
			if (huntEnabled)
				flags |= FLAG_HUNT_ENABLED;
			if (frameAnalysisActive)
				flags |= FLAG_FRAME_ANALYSIS_ACTIVE;
			if (shaderDumpActive)
				flags |= FLAG_SHADER_DUMP_ACTIVE;
			if (preSkinProbe)
				flags |= FLAG_PRE_SKIN_PROBE;
			if (preSkinBindingTrack)
				flags |= FLAG_PRE_SKIN_BINDING_TRACK;
			if (iaResidentState)
				flags |= FLAG_IA_RESIDENT_STATE;
			if (shaderDescriptorTrack)
				flags |= FLAG_SHADER_DESCRIPTOR_TRACK;
			if (drawSrvSlotReplacement)
				flags |= FLAG_DRAW_SRV_SLOT_REPLACEMENT;
			return flags;
		}

		bool HasAnyWork() const
		{
			return huntEnabled ||
				frameAnalysisActive ||
				shaderDumpActive ||
				shaderOverrides ||
				presentReplacement ||
				preSkinProbe ||
				preSkinBindingTrack ||
				shaderDescriptorTrack ||
				iaResidentState ||
				drawSrvSlotReplacement;
		}

		bool NeedsHeavyTracking() const
		{
			return huntEnabled || shaderDumpActive;
		}

		bool NeedsRecordWork() const
		{
			return huntEnabled || NeedsModDrawWork();
		}

		bool NeedsModDrawWork() const
		{
			return shaderOverrides ||
				shaderDescriptorTrack ||
				iaResidentState ||
				drawSrvSlotReplacement;
		}

		bool NeedsBindingState() const
		{
			// Pre-skin keeps its compute bindings resident in the lightweight
			// runtime snapshot and no longer needs the full tracked-binding path.
			return NeedsHeavyTracking();
		}

		bool NeedsGraphicsBindingState() const
		{
			return NeedsHeavyTracking() || shaderDescriptorTrack ||
				drawSrvSlotReplacement;
		}

		const char *ModeName() const
		{
			return huntEnabled ? "Hunting" : "Mod";
		}
	};

	static bool HasAnyFlag(UINT64 flags, UINT64 mask)
	{
		return (flags & mask) != 0;
	}

	static bool NeedsResidentIaState(UINT64 flags)
	{
		return HasAnyFlag(flags, FLAG_HUNT_ENABLED | FLAG_IA_RESIDENT_STATE);
	}

	static bool NeedsGraphicsResidentDescriptorState(UINT64 flags)
	{
		return HasAnyFlag(
			flags,
			FLAG_DRAW_SRV_SLOT_REPLACEMENT |
			FLAG_SHADER_DESCRIPTOR_TRACK |
			FLAG_FRAME_ANALYSIS_ACTIVE |
			FLAG_SHADER_DUMP_ACTIVE);
	}

	static bool NeedsGraphicsResidentRootDescriptorState(UINT64 flags)
	{
		// Descriptor-triggered Mod matching can still query graphics root
		// descriptors from the lightweight runtime snapshot, but draw-time SRV
		// table patching does not need them.
		return HasAnyFlag(
			flags,
			FLAG_SHADER_DESCRIPTOR_TRACK |
			FLAG_FRAME_ANALYSIS_ACTIVE |
			FLAG_SHADER_DUMP_ACTIVE);
	}

	static bool NeedsGraphicsResidentRootValueState(UINT64 flags)
	{
		// DX11-style Mod matching for graphics texture replacement only needs the
		// live descriptor heaps/tables/root signature. Outside capture/debug, the
		// only graphics root values that still matter are root descriptors for
		// descriptor-triggered Mod lookups; root constants remain capture-only.
		return HasAnyFlag(
			flags,
			FLAG_FRAME_ANALYSIS_ACTIVE |
			FLAG_SHADER_DUMP_ACTIVE);
	}

	static bool NeedsGraphicsTrackedBindingCapture(UINT64 flags)
	{
		return HasAnyFlag(
			flags,
			FLAG_FRAME_ANALYSIS_ACTIVE |
			FLAG_SHADER_DUMP_ACTIVE);
	}

	static bool NeedsComputeResidentDescriptorState(UINT64 flags)
	{
		return HasAnyFlag(
			flags,
			FLAG_PRE_SKIN_PROBE |
			FLAG_PRE_SKIN_BINDING_TRACK |
			FLAG_FRAME_ANALYSIS_ACTIVE |
			FLAG_SHADER_DUMP_ACTIVE);
	}

	static bool NeedsComputeResidentBindingState(UINT64 flags)
	{
		return HasAnyFlag(
			flags,
			FLAG_PRE_SKIN_PROBE |
			FLAG_PRE_SKIN_BINDING_TRACK |
			FLAG_FRAME_ANALYSIS_ACTIVE |
			FLAG_SHADER_DUMP_ACTIVE);
	}

	static bool NeedsAnyResidentDescriptorState(UINT64 flags)
	{
		return NeedsGraphicsResidentDescriptorState(flags) ||
			NeedsComputeResidentDescriptorState(flags);
	}

	static bool NeedsHuntOnlyPipelineObservation(UINT64 flags)
	{
		return HasAnyFlag(flags, FLAG_HUNT_ENABLED) &&
			!HasAnyFlag(flags, FLAG_SHADER_OVERRIDES);
	}

	static bool AllowsResidentDrawFastReject(UINT64 flags)
	{
		// Once the command-list-local draw decision proves a PSO can never
		// trigger draw-time work, any remaining global mod flags should not force
		// the call back through the generic interpreter unless diagnostics or
		// present-time replacement explicitly require it.
		return !HasAnyFlag(
			flags,
			FLAG_HUNT_ENABLED |
			FLAG_FRAME_ANALYSIS_ACTIVE |
			FLAG_SHADER_DUMP_ACTIVE);
	}

	static bool CanResidentDrawFastReject(
		UINT64 flags,
		const DX12SimulateContext *state)
	{
		return state &&
			AllowsResidentDrawFastReject(flags) &&
			state->drawDecision.resolved &&
			state->drawDecision.pipelineState == state->pipelineState &&
			state->drawDecision.reloadGeneration ==
				DX12ModGetReloadGenerationFast() &&
			state->drawDecision.MatchesGlobals(
				(flags & FLAG_SHADER_OVERRIDES) != 0) &&
			state->drawDecision.CanFastReject();
	}

	static bool AllowsResidentDispatchFastReject(UINT64 flags)
	{
		// Compute fast reject must stay disabled while pre-skin probing is live,
		// but graphics-only resident mod bits should not force every dispatch back
		// through the generic interpreter.
		return HasAnyFlag(flags, FLAG_SHADER_OVERRIDES) &&
			!HasAnyFlag(
				flags,
				FLAG_HUNT_ENABLED |
				FLAG_FRAME_ANALYSIS_ACTIVE |
				FLAG_SHADER_DUMP_ACTIVE |
				FLAG_PRE_SKIN_PROBE);
	}

	static bool CanResidentDispatchFastReject(
		UINT64 flags,
		const DX12SimulateContext *state)
	{
		return state &&
			AllowsResidentDispatchFastReject(flags) &&
			state->dispatchDecision.resolved &&
			state->dispatchDecision.pipelineState == state->pipelineState &&
			state->dispatchDecision.reloadGeneration ==
				DX12ModGetReloadGenerationFast() &&
			state->dispatchDecision.MatchesGlobals(
				(flags & FLAG_SHADER_OVERRIDES) != 0,
				(flags & FLAG_PRE_SKIN_PROBE) != 0) &&
			!state->dispatchDecision.NeedsShaderOverrideWork();
	}

	static bool NeedsPreSkinBindingTracking(UINT64 flags)
	{
		return HasAnyFlag(flags, FLAG_PRE_SKIN_BINDING_TRACK);
	}

	static bool NeedsPreSkinProbe(UINT64 flags)
	{
		return HasAnyFlag(flags, FLAG_PRE_SKIN_PROBE);
	}

	static bool NeedsIaBufferContentTracking(UINT64 flags)
	{
		return HasAnyFlag(flags, FLAG_IA_RESIDENT_STATE);
	}

	static bool NeedsTextureHashTracking(UINT64 flags)
	{
		// Only maintain the resource-content hash cache on copy paths when a
		// consumer can actually read it. DX11 gates the same work behind
		// track_texture_updates / hunting; running the full top-mip hash plus
		// a global lock insert on every streaming copy with no mods loaded is
		// pure overhead (wrapper-mode CopyTextureRegion was doing exactly that).
		return HasAnyFlag(
			flags,
			FLAG_SHADER_OVERRIDES |
			FLAG_IA_RESIDENT_STATE |
			FLAG_HUNT_ENABLED |
			FLAG_FRAME_ANALYSIS_ACTIVE |
			FLAG_SHADER_DUMP_ACTIVE |
			FLAG_PRE_SKIN_PROBE |
			FLAG_PRE_SKIN_BINDING_TRACK |
			FLAG_SHADER_DESCRIPTOR_TRACK |
			FLAG_DRAW_SRV_SLOT_REPLACEMENT);
	}

	static bool NeedsBindingEventRecording(UINT64 flags)
	{
		return HasAnyFlag(
			flags,
			FLAG_FRAME_ANALYSIS_ACTIVE | FLAG_SHADER_DUMP_ACTIVE);
	}

	static void Update()
	{
		const SourceState source = SourceState::Capture();
		if (skipAll && !source.HasAnyWork())
			return;

		// As in DX11, skipAll controls draw, state, and copy hooks.
		// PreSkin only needs dispatch hooks and compute binding tracking;
		// skipBindings and the dispatch-specific check handle that case.
		const LONG skipAll =
			(source.NeedsHeavyTracking() || source.NeedsRecordWork()) ? 0 : 1;
		const LONG skipBindings = source.NeedsBindingState() ? 0 : 1;
		const LONG skipGraphicsBindings =
			source.NeedsGraphicsBindingState() ? 0 : 1;
		const LONG trackResourceMetadata =
			source.NeedsHeavyTracking() ? 1 : 0;
		const UINT64 flags = source.BuildHookWorkFlags();

		InterlockedExchange(&DX12HotPathState::skipAll, skipAll);
		InterlockedExchange(&DX12HotPathState::skipBindings, skipBindings);
		InterlockedExchange(&DX12HotPathState::skipGraphicsBindings, skipGraphicsBindings);
		InterlockedExchange(&DX12HotPathState::trackResourceMetadata, trackResourceMetadata);
		InterlockedExchange64(
			reinterpret_cast<volatile LONG64*>(&HookWorkFlags),
			static_cast<LONG64>(flags));
		DX12ActivateCommandListHotPathHooks();

		const LONG mask =
			(skipAll ? 1 : 0) |
			(skipBindings ? 2 : 0) |
			(skipGraphicsBindings ? 4 : 0) |
			(trackResourceMetadata ? 8 : 0);
		if (InterlockedExchange(&stateMask, mask) != mask) {
			// Command lists cache binding-capture decisions. Mode or capture-state
			// changes must invalidate that cache, otherwise Hunting can keep using
			// a Mod-era "do not track bindings" decision and lose SRV texture tables.
			DX12CommandListRuntime::BumpTrackingGeneration();
			DX12LogManager::Fields("DX12HotPathState",
				"\"mode\":\"%s\",\"skipAll\":%ld,\"skipBindings\":%ld,\"skipGraphicsBindings\":%ld,\"trackResourceMetadata\":%ld",
				source.ModeName(),
				skipAll, skipBindings, skipGraphicsBindings, trackResourceMetadata);
		}
	}

	static bool HooksNeeded()
	{
		return InterlockedCompareExchange(&skipAll, 0, 0) == 0 ||
			HasAnyFlag(HookWorkFlags, FLAG_HUNT_ENABLED);
	}
};
