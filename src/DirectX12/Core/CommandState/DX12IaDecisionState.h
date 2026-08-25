#pragma once

// Active IA state and compiled draw/dispatch decision state.
// Split out of Core/DX12CommandState.h; include that umbrella header from existing code.
#include "../DX12Diagnostics.h"

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
	// Bind-time static IA replacement bookkeeping (plan step 3.2). When a
	// bound IB/VB content hash matched an unconditional static geometry
	// override at IASet time, the stored view above points at the mod buffer
	// and these fields remember the original binding plus the identity used
	// for staleness checks, so a rewritten or recreated game buffer can be
	// detected and re-bound with the original view.
	bool indexBufferStaticReplaced = false;
	D3D12_INDEX_BUFFER_VIEW indexBufferStaticOriginal = {};
	D3D12_INDEX_BUFFER_VIEW indexBufferStaticReplacedView = {};
	DX12BufferResourceSummary indexBufferStaticOriginalResource = {};
	bool indexBufferStaticOriginalResolved = false;
	UINT64 indexBufferStaticContentGeneration = 0;
	UINT64 indexBufferStaticResolveEpoch = 0;
	uint32_t indexBufferStaticMatchedHash = 0;
	bool vertexBufferStaticReplaced[32] = {};
	// Aggregate of vertexBufferStaticReplaced for a single-load draw-path gate.
	UINT32 vertexBufferStaticReplacedMask = 0;
	D3D12_VERTEX_BUFFER_VIEW vertexBufferStaticOriginal[32] = {};
	D3D12_VERTEX_BUFFER_VIEW vertexBufferStaticReplacedView[32] = {};
	DX12BufferResourceSummary vertexBufferStaticOriginalResources[32] = {};
	bool vertexBufferStaticOriginalResolved[32] = {};
	UINT64 vertexBufferStaticContentGeneration[32] = {};
	UINT64 vertexBufferStaticResolveEpoch[32] = {};
	uint32_t vertexBufferStaticMatchedHash[32] = {};
	// Plan fix B: lazily re-check static-replacement staleness only when the
	// global content/resolve generations moved since the last deep check on
	// this command list. Without this gate every draw pays a per-slot locked
	// map lookup (DX12GetBufferContentGenerationForResource) whenever a
	// bind-time static replacement is live.
	UINT64 staticReplacedLastCheckedContentGeneration = 0;
	UINT64 staticReplacedLastCheckedResolveEpoch = 0;
	// Plan fix F: pre-skin IA replay gate. ReplayActiveVertexBuffers (GPUVA
	// lookups + IASet) only needs to run once per output-resize/active
	// generation per IA state; further matching draws on the same command list
	// reuse the already-replayed views. The IA view fingerprint guards against
	// game rebinds between replays (rebinding identical views keeps the same
	// fingerprint, unlike iaEpoch which bumps on every rebind).
	UINT64 preSkinReplayGeneration = 0;
	UINT64 preSkinReplayIaFingerprint = 0;
	bool preSkinReplayApplied = false;
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
	// Every descriptor texture target in this decision's plans is an
	// unconditional static replacement handled at descriptor write time
	// (CreateShaderResourceView / CopyDescriptors), so the draw path never
	// needs to resolve SRV bindings for them.
	bool descriptorTargetsStatic = false;
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
		descriptorTargetsStatic = false;
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

		if (!hasShaderOverrideSkip &&
			!targetPlanHasIaTargets &&
			!targetPlanHasDescriptorTargets) {
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

	bool NeedsIaHash() const
	{
		return targetPlanHasIaTargets;
	}

	bool NeedsDescriptorMatch() const
	{
		return targetPlanHasDescriptorTargets;
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
