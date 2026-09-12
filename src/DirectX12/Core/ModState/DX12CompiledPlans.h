#pragma once

// Compiled command-list / texture-override / shader-override plans.
//
// These are the frozen form a config section takes after the ini is parsed:
// ModRuntime/DX12ModRuntimeLoad.cpp builds them once per reload, and the
// command-list executor and the draw-time replacement paths consume them.
//
// They used to be defined in ModRuntime/DX12ModRuntimeInternal.h - the faux
// translation unit that splices several code fragments together at fixed lines
// - which meant a header could only use them if it happened to be spliced in
// after their definition. They live here so every consumer can name what it
// needs with an include.
//
// Include Core/DX12ModState.h (or its umbrella Core/DX12Core.h).
#include "../DX12Core.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// FNV-style mixing used by the target-cache key hash below. Defined in
// ModRuntime/DX12ModRuntimeLoad.cpp.
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


// A batch of IA resource views the command-list executor queued for a later
// ApplyPendingResourceViewsForCommandList call on the same list.
class DX12PendingResourceViewRequest

{

public:
	std::wstring indexResource;
	std::vector<DX12CompiledVertexResourceBinding> vertexResources;
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


// Key for the per-command-list descriptor-target caches in
// DX12CommandListExecutorScratch: which target (kind/stage/slot) a cached
// binding set was resolved for.
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
