#pragma once

#include <Windows.h>
#include <d3d12.h>

#include <list>
#include <unordered_map>
#include <utility>

#include "DX12IaHashState.hpp"

class DX12IaHashStateCacheKey
{
public:
	UINT64 targetPlanFingerprint = 0;
	UINT64 iaViewFingerprint = 0;
	UINT64 iaContentEpoch = 0;
	UINT64 bufferContentGeneration = 0;
	UINT64 bufferResolveGeneration = 0;
	bool indexed = false;
	UINT vertexCount = 0;
	UINT indexCount = 0;
	UINT firstVertex = 0;
	UINT firstIndex = 0;
	INT baseVertex = 0;

	bool operator==(const DX12IaHashStateCacheKey &other) const
	{
		return targetPlanFingerprint == other.targetPlanFingerprint &&
			iaViewFingerprint == other.iaViewFingerprint &&
			iaContentEpoch == other.iaContentEpoch &&
			bufferContentGeneration == other.bufferContentGeneration &&
			bufferResolveGeneration == other.bufferResolveGeneration &&
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
	static size_t Combine(size_t hash, UINT64 value)
	{
		hash ^= static_cast<size_t>(value) + 0x9e3779b97f4a7c15ull +
			(hash << 6) + (hash >> 2);
		return hash;
	}

	size_t operator()(const DX12IaHashStateCacheKey &key) const
	{
		size_t hash = 0x84222325a9582d75ull;
		hash = Combine(hash, key.targetPlanFingerprint);
		hash = Combine(hash, key.iaViewFingerprint);
		hash = Combine(hash, key.iaContentEpoch);
		hash = Combine(hash, key.bufferContentGeneration);
		hash = Combine(hash, key.bufferResolveGeneration);
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

class DX12IaHashStateCache
{
public:
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
};

class DX12IaHashStateCacheUtils
{
public:
	static constexpr size_t MaxEntries = 1024;

	static bool TryLoad(
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

		if (cache->entries.size() >= MaxEntries) {
			cache->entries.erase(cache->lruKeys.back());
			cache->lruKeys.pop_back();
			if (evicted)
				*evicted = true;
		}

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
