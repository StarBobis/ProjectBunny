#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <stdint.h>

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
