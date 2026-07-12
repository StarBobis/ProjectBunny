#pragma once

#include <list>
#include <unordered_map>

#include "DX12IaHashStateCacheUtils.hpp"

class DX12DirectIaProbeCacheKey
{
public:
	UINT64 reloadGeneration = 0;
	DX12IaHashStateCacheKey iaKey;

	bool operator==(const DX12DirectIaProbeCacheKey &other) const
	{
		return reloadGeneration == other.reloadGeneration &&
			iaKey == other.iaKey;
	}
};

class DX12DirectIaProbeCacheKeyHash
{
public:
	size_t operator()(const DX12DirectIaProbeCacheKey &key) const
	{
		size_t hash = DX12IaHashStateCacheKeyHash()(key.iaKey);
		hash ^= static_cast<size_t>(key.reloadGeneration) + 0x9e3779b97f4a7c15ull +
			(hash << 6) + (hash >> 2);
		return hash;
	}
};

class DX12DirectIaProbeCacheEntry
{
public:
	bool hasDirectCandidate = false;
	std::list<DX12DirectIaProbeCacheKey>::iterator lruPosition;
};

class DX12DirectIaProbeCache
{
public:
	DX12DirectIaProbeCache() = default;

	DX12DirectIaProbeCache(const DX12DirectIaProbeCache &)
	{
	}

	DX12DirectIaProbeCache &operator=(const DX12DirectIaProbeCache &other)
	{
		if (this != &other) {
			entries.clear();
			lruKeys.clear();
		}
		return *this;
	}

	DX12DirectIaProbeCache(DX12DirectIaProbeCache &&) noexcept = default;
	DX12DirectIaProbeCache &operator=(DX12DirectIaProbeCache &&) noexcept = default;

	std::list<DX12DirectIaProbeCacheKey> lruKeys;
	std::unordered_map<
		DX12DirectIaProbeCacheKey,
		DX12DirectIaProbeCacheEntry,
		DX12DirectIaProbeCacheKeyHash> entries;
};

class DX12DirectIaProbeCacheUtils
{
public:
	// A normal frame in the target title reaches roughly 1.3k distinct probes.
	// Keep the complete command-local working set so late draws can reuse early
	// negative results instead of evicting and rebuilding them in the same epoch.
	static constexpr size_t MaxEntries = 4096;

	static bool TryLoad(
		DX12DirectIaProbeCache *cache,
		const DX12DirectIaProbeCacheKey &key,
		bool *hasDirectCandidate)
	{
		if (!cache || !hasDirectCandidate)
			return false;

		auto found = cache->entries.find(key);
		if (found == cache->entries.end())
			return false;

		cache->lruKeys.splice(
			cache->lruKeys.begin(), cache->lruKeys, found->second.lruPosition);
		found->second.lruPosition = cache->lruKeys.begin();
		*hasDirectCandidate = found->second.hasDirectCandidate;
		return true;
	}

	static void Store(
		DX12DirectIaProbeCache *cache,
		const DX12DirectIaProbeCacheKey &key,
		bool hasDirectCandidate,
		bool *evicted)
	{
		if (evicted)
			*evicted = false;
		if (!cache)
			return;

		auto found = cache->entries.find(key);
		if (found != cache->entries.end()) {
			cache->lruKeys.splice(
				cache->lruKeys.begin(), cache->lruKeys, found->second.lruPosition);
			found->second.lruPosition = cache->lruKeys.begin();
			found->second.hasDirectCandidate = hasDirectCandidate;
			return;
		}

		if (cache->entries.size() >= MaxEntries) {
			cache->entries.erase(cache->lruKeys.back());
			cache->lruKeys.pop_back();
			if (evicted)
				*evicted = true;
		}

		cache->lruKeys.push_front(key);
		DX12DirectIaProbeCacheEntry entry;
		entry.hasDirectCandidate = hasDirectCandidate;
		entry.lruPosition = cache->lruKeys.begin();
		cache->entries.emplace(key, std::move(entry));
	}

	static void Clear(DX12DirectIaProbeCache *cache)
	{
		if (!cache)
			return;
		cache->entries.clear();
		cache->lruKeys.clear();
	}
};
