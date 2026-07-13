#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <stdint.h>

#include "DX12IaHashTargetPlan.hpp"

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
	bool negativeCacheSafe = false;
	bool shaderOverrideNegativeCacheAllowed = false;
	DX12IaHashTargetPlan targetPlan;
	DX12IaHashTargetPlan directTargetPlan;

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
		negativeCacheSafe = false;
		shaderOverrideNegativeCacheAllowed = false;
		targetPlan = DX12IaHashTargetPlan();
		directTargetPlan = DX12IaHashTargetPlan();
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
		targetPlanResolved = true;
		directTargetPlanResolved = true;
		targetPlanHasIaTargets = targetPlan.HasIaTargets();
		targetPlanHasDescriptorTargets = targetPlan.hasDescriptorTargets;
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
