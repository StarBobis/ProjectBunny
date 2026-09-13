// Pre-skin output-resize identity and PSO decision caches.
// Split from DX12Resources.h; keep declarations in the layer that owns them.
#pragma once

#include "DX12PreSkinBindingCache.h"

class DX12PreSkinOutputResizeCacheIdentity
{
public:
	std::uintptr_t originalResource = 0;
	std::uint64_t requiredByteWidth = 0;
	std::uint32_t stride = 0;
	std::uint32_t ringSlot = 0;

	bool operator==(const DX12PreSkinOutputResizeCacheIdentity &rhs) const
	{
		return originalResource == rhs.originalResource &&
			requiredByteWidth == rhs.requiredByteWidth &&
			stride == rhs.stride &&
			ringSlot == rhs.ringSlot;
	}

	bool operator!=(const DX12PreSkinOutputResizeCacheIdentity &rhs) const
	{
		return !(*this == rhs);
	}

	bool operator<(const DX12PreSkinOutputResizeCacheIdentity &rhs) const
	{
		if (originalResource != rhs.originalResource)
			return originalResource < rhs.originalResource;
		if (requiredByteWidth != rhs.requiredByteWidth)
			return requiredByteWidth < rhs.requiredByteWidth;
		if (stride != rhs.stride)
			return stride < rhs.stride;
		return ringSlot < rhs.ringSlot;
	}
};


class DX12PsoDecisionCacheDispatchKey
{
public:
	ID3D12PipelineState *pipelineState = nullptr;
	UINT64 reloadGeneration = 0;
	bool activeShaderOverrides = false;
	bool activePreSkinProbe = false;

	bool operator==(const DX12PsoDecisionCacheDispatchKey &other) const
	{
		return pipelineState == other.pipelineState &&
			reloadGeneration == other.reloadGeneration &&
			activeShaderOverrides == other.activeShaderOverrides &&
			activePreSkinProbe == other.activePreSkinProbe;
	}
};

class DX12PsoDecisionCacheDispatchKeyHash
{
public:
	size_t operator()(const DX12PsoDecisionCacheDispatchKey &key) const
	{
		size_t hash = reinterpret_cast<size_t>(key.pipelineState);
		hash ^= key.reloadGeneration + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
		hash ^= static_cast<size_t>(key.activeShaderOverrides) * 0xff51afd7ed558ccdull;
		hash ^= static_cast<size_t>(key.activePreSkinProbe) * 0xc4ceb9fe1a85ec53ull;
		return hash;
	}
};

class DX12PsoDecisionCacheDrawKey
{
public:
	ID3D12PipelineState *pipelineState = nullptr;
	UINT64 reloadGeneration = 0;
	bool activeShaderOverrides = false;

	bool operator==(const DX12PsoDecisionCacheDrawKey &other) const
	{
		return pipelineState == other.pipelineState &&
			reloadGeneration == other.reloadGeneration &&
			activeShaderOverrides == other.activeShaderOverrides;
	}
};

class DX12PsoDecisionCacheDrawKeyHash
{
public:
	size_t operator()(const DX12PsoDecisionCacheDrawKey &key) const
	{
		size_t hash = reinterpret_cast<size_t>(key.pipelineState);
		hash ^= key.reloadGeneration + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
		hash ^= static_cast<size_t>(key.activeShaderOverrides) * 0xff51afd7ed558ccdull;
		return hash;
	}
};

class DX12PsoDecisionCacheDispatchEntry
{
public:
	DX12CompiledDispatchDecision decision;
};

class DX12PsoDecisionCacheDrawEntry
{
public:
	DX12CompiledDrawDecision decision;
};

class DX12PsoDecisionCacheUtils
{
public:
	// Decisions are immutable for a reload generation. Command-list recording is
	// already thread-affine, so a process-wide LRU only serialized otherwise
	// independent recording threads on every draw/dispatch cache hit. Keep the
	// resident PSO decisions in thread-local generation arenas instead: reads are
	// lock-free and a generation change discards the complete arena in O(1).
	inline static thread_local UINT64 ReloadGeneration = 0;
	static constexpr UINT64 CacheFormatVersionCurrent = 5;
	inline static thread_local UINT64 CacheFormatVersion = 0;
	static constexpr size_t InitialEntriesPerKind = 4096;
	inline static thread_local size_t DispatchGrowthBudget = InitialEntriesPerKind;
	inline static thread_local size_t DrawGrowthBudget = InitialEntriesPerKind;
	inline static thread_local std::unordered_map<
		DX12PsoDecisionCacheDispatchKey,
		DX12PsoDecisionCacheDispatchEntry,
		DX12PsoDecisionCacheDispatchKeyHash> DispatchEntries;
	inline static thread_local std::unordered_map<
		DX12PsoDecisionCacheDrawKey,
		DX12PsoDecisionCacheDrawEntry,
		DX12PsoDecisionCacheDrawKeyHash> DrawEntries;

	static void ClearIfReloadChanged(UINT64 reloadGeneration)
	{
		if (DX12PsoDecisionCacheUtils::ReloadGeneration != reloadGeneration ||
			DX12PsoDecisionCacheUtils::CacheFormatVersion !=
				CacheFormatVersionCurrent) {
			DX12PsoDecisionCacheUtils::ReloadGeneration = reloadGeneration;
			DX12PsoDecisionCacheUtils::CacheFormatVersion =
				CacheFormatVersionCurrent;
			DX12PsoDecisionCacheUtils::DispatchEntries.clear();
			DX12PsoDecisionCacheUtils::DrawEntries.clear();
		}
	}

	static bool TryLoadDispatch(
		ID3D12PipelineState *pipelineState,
		UINT64 reloadGeneration,
		bool activeShaderOverrides,
		bool activePreSkinProbe,
		DX12CompiledDispatchDecision *decision)
	{
		if (!decision)
			return false;

		DX12PsoDecisionCacheDispatchKey key;
		key.pipelineState = pipelineState;
		key.reloadGeneration = reloadGeneration;
		key.activeShaderOverrides = activeShaderOverrides;
		key.activePreSkinProbe = activePreSkinProbe;

		std::unordered_map<
			DX12PsoDecisionCacheDispatchKey,
			DX12PsoDecisionCacheDispatchEntry,
			DX12PsoDecisionCacheDispatchKeyHash>::iterator it =
			DX12PsoDecisionCacheUtils::DispatchEntries.find(key);
		if (it != DX12PsoDecisionCacheUtils::DispatchEntries.end()) {
			*decision = it->second.decision;
			return true;
		}
		return false;
	}

	static void StoreDispatch(
		ID3D12PipelineState *pipelineState,
		UINT64 reloadGeneration,
		bool activeShaderOverrides,
		bool activePreSkinProbe,
		const DX12CompiledDispatchDecision &decision)
	{
		DX12PsoDecisionCacheDispatchKey key;
		key.pipelineState = pipelineState;
		key.reloadGeneration = reloadGeneration;
		key.activeShaderOverrides = activeShaderOverrides;
		key.activePreSkinProbe = activePreSkinProbe;

		if (DX12PsoDecisionCacheUtils::DispatchEntries.find(key) ==
				DX12PsoDecisionCacheUtils::DispatchEntries.end()) {
			DX12FeatureCacheGrowthUtils::EnsureBudget(
				&DX12PsoDecisionCacheUtils::DispatchGrowthBudget,
				&DX12PsoDecisionCacheUtils::DispatchEntries,
				DX12PsoDecisionCacheUtils::DispatchEntries.size() + 1);
		}
		DX12PsoDecisionCacheDispatchEntry &entry =
			DX12PsoDecisionCacheUtils::DispatchEntries[key];
		entry.decision = decision;
	}

	static bool TryLoadDraw(
		ID3D12PipelineState *pipelineState,
		UINT64 reloadGeneration,
		bool activeShaderOverrides,
		DX12CompiledDrawDecision *decision)
	{
		if (!decision)
			return false;

		DX12PsoDecisionCacheDrawKey key;
		key.pipelineState = pipelineState;
		key.reloadGeneration = reloadGeneration;
		key.activeShaderOverrides = activeShaderOverrides;

		std::unordered_map<
			DX12PsoDecisionCacheDrawKey,
			DX12PsoDecisionCacheDrawEntry,
			DX12PsoDecisionCacheDrawKeyHash>::iterator it =
			DX12PsoDecisionCacheUtils::DrawEntries.find(key);
		if (it != DX12PsoDecisionCacheUtils::DrawEntries.end()) {
			*decision = it->second.decision;
			return true;
		}
		return false;
	}

	static void StoreDraw(
		ID3D12PipelineState *pipelineState,
		UINT64 reloadGeneration,
		bool activeShaderOverrides,
		const DX12CompiledDrawDecision &decision)
	{
		DX12PsoDecisionCacheDrawKey key;
		key.pipelineState = pipelineState;
		key.reloadGeneration = reloadGeneration;
		key.activeShaderOverrides = activeShaderOverrides;

		if (DX12PsoDecisionCacheUtils::DrawEntries.find(key) ==
				DX12PsoDecisionCacheUtils::DrawEntries.end()) {
			DX12FeatureCacheGrowthUtils::EnsureBudget(
				&DX12PsoDecisionCacheUtils::DrawGrowthBudget,
				&DX12PsoDecisionCacheUtils::DrawEntries,
				DX12PsoDecisionCacheUtils::DrawEntries.size() + 1);
		}
		DX12PsoDecisionCacheDrawEntry &entry =
			DX12PsoDecisionCacheUtils::DrawEntries[key];
		entry.decision = decision;
	}
};


// Deferred initial mod load (startup hang workaround). Declared here so every
// TU that compiles DX12ResourceRuntime.hpp sees them regardless of include
// order; the definitions live in ModRuntime/DX12ModRuntimeLoad.cpp.
extern void DX12ModScheduleInitialLoad(const wchar_t *configPath);
extern void DX12ModMaybeRunDeferredInitialLoad();

