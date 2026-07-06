#pragma once

#include <Windows.h>
#include <d3d12.h>

#include <unordered_map>
#include <vector>

#include "DX12IaHashState.hpp"

class DX12IaHashStateCacheKey
{
public:
	ID3D12PipelineState *pipelineState = nullptr;
	UINT64 reloadGeneration = 0;
	UINT64 contentGeneration = 0;
	UINT64 resolveGeneration = 0;
	UINT64 targetPlanFingerprint = 0;
	UINT64 iaViewFingerprint = 0;
	bool indexed = false;
	UINT vertexCount = 0;
	UINT indexCount = 0;
	UINT firstVertex = 0;
	UINT firstIndex = 0;
	INT baseVertex = 0;

	bool operator==(const DX12IaHashStateCacheKey &other) const
	{
		return pipelineState == other.pipelineState &&
			reloadGeneration == other.reloadGeneration &&
			contentGeneration == other.contentGeneration &&
			resolveGeneration == other.resolveGeneration &&
			targetPlanFingerprint == other.targetPlanFingerprint &&
			iaViewFingerprint == other.iaViewFingerprint &&
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
	size_t operator()(const DX12IaHashStateCacheKey &k) const
	{
		size_t h = reinterpret_cast<size_t>(k.pipelineState);
		h ^= k.reloadGeneration + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
		h ^= k.contentGeneration + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
		h ^= k.resolveGeneration + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
		h ^= k.targetPlanFingerprint + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
		h ^= k.iaViewFingerprint + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
		h ^= static_cast<size_t>(k.indexed) * 0xff51afd7ed558ccdull;
		h ^= k.vertexCount * 0xc4ceb9fe1a85ec53ull;
		h ^= k.indexCount * 0x27d4eb2f165667c5ull;
		h ^= k.firstVertex * 0x94d049bb133111ebull;
		h ^= k.firstIndex * 0x9f5b4e1d4c3b2a19ull;
		h ^= static_cast<size_t>(k.baseVertex) * 0x3c6ef372fe94f82aull;
		return h;
	}
};

class DX12IaHashStateCacheEntry
{
public:
	UINT64 age = 0;
	DX12IaHashStateCacheKey key;
	bool hasIaHashState = false;
	DX12IaHashState state;
};

class DX12IaHashStateCacheUtils
{
public:
	static constexpr size_t CacheCapacity = 4096;

	static bool TryLoad(
		std::unordered_map<DX12IaHashStateCacheKey, DX12IaHashStateCacheEntry,
			DX12IaHashStateCacheKeyHash> *entries,
		UINT64 *clock,
		const DX12IaHashStateCacheKey &key,
		DX12IaHashState *state,
		bool *hasIaHashState)
	{
		if (!entries || !clock || !state || !hasIaHashState)
			return false;

		typename std::unordered_map<DX12IaHashStateCacheKey,
			DX12IaHashStateCacheEntry, DX12IaHashStateCacheKeyHash>::iterator it =
			entries->find(key);
		if (it == entries->end())
			return false;

		it->second.age = ++(*clock);
		*hasIaHashState = it->second.hasIaHashState;
		*state = it->second.state;
		return true;
	}

	static void Store(
		std::unordered_map<DX12IaHashStateCacheKey, DX12IaHashStateCacheEntry,
			DX12IaHashStateCacheKeyHash> *entries,
		UINT64 *clock,
		UINT *evictCursor,
		const DX12IaHashStateCacheKey &key,
		bool hasIaHashState,
		DX12IaHashState state)
	{
		if (!entries || !clock || !evictCursor)
			return;

		typename std::unordered_map<DX12IaHashStateCacheKey,
			DX12IaHashStateCacheEntry, DX12IaHashStateCacheKeyHash>::iterator it =
			entries->find(key);
		if (it != entries->end()) {
			it->second.age = ++(*clock);
			it->second.hasIaHashState = hasIaHashState;
			it->second.state = std::move(state);
			return;
		}

		// Round-robin clock eviction: O(1) amortized, no full-scan age comparison
		if (entries->size() >= CacheCapacity) {
			size_t pos = static_cast<size_t>(*evictCursor) % CacheCapacity;
			typename std::unordered_map<DX12IaHashStateCacheKey,
				DX12IaHashStateCacheEntry, DX12IaHashStateCacheKeyHash>::iterator victim =
				entries->begin();
			std::advance(victim, pos);
			entries->erase(victim);
			++(*evictCursor);
		}

		DX12IaHashStateCacheEntry entry;
		entry.age = ++(*clock);
		entry.key = key;
		entry.hasIaHashState = hasIaHashState;
		entry.state = std::move(state);
		(*entries)[key] = std::move(entry);
	}

	static void Clear(
		std::unordered_map<DX12IaHashStateCacheKey, DX12IaHashStateCacheEntry,
			DX12IaHashStateCacheKeyHash> *entries,
		UINT64 *clock,
		UINT *evictCursor)
	{
		if (entries)
			entries->clear();
		if (clock)
			*clock = 0;
		if (evictCursor)
			*evictCursor = 0;
	}
};
