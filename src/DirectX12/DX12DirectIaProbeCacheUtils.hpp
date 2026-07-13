#pragma once

#include <array>
#include <list>
#include <unordered_map>

#include "DX12IaHashState.hpp"
#include "DX12IaDrawInvocation.hpp"

class DX12DirectIaProbeCacheKey
{
public:
	UINT64 reloadGeneration = 0;
	UINT64 targetPlanFingerprint = 0;
	bool indexed = false;
	bool hasIndexBuffer = false;
	uint32_t indexHash = 0;
	uint32_t vertexSlotMask = 0;
	std::array<uint32_t, 32> vertexHashes = {};
	uint32_t vertexCount = 0;
	uint32_t indexCount = 0;
	uint32_t instanceCount = 0;
	uint32_t firstVertex = 0;
	uint32_t firstIndex = 0;
	INT baseVertex = 0;
	uint32_t firstInstance = 0;

	bool operator==(const DX12DirectIaProbeCacheKey &other) const
	{
		return reloadGeneration == other.reloadGeneration &&
			targetPlanFingerprint == other.targetPlanFingerprint &&
			indexed == other.indexed &&
			hasIndexBuffer == other.hasIndexBuffer &&
			indexHash == other.indexHash &&
			vertexSlotMask == other.vertexSlotMask &&
			vertexHashes == other.vertexHashes &&
			vertexCount == other.vertexCount &&
			indexCount == other.indexCount &&
			instanceCount == other.instanceCount &&
			firstVertex == other.firstVertex &&
			firstIndex == other.firstIndex &&
			baseVertex == other.baseVertex &&
			firstInstance == other.firstInstance;
	}
};

class DX12DirectIaProbeCacheKeyHash
{
public:
	static size_t Combine(size_t hash, UINT64 value)
	{
		return hash ^ (static_cast<size_t>(value) + 0x9e3779b97f4a7c15ull +
			(hash << 6) + (hash >> 2));
	}

	size_t operator()(const DX12DirectIaProbeCacheKey &key) const
	{
		size_t hash = 0x84222325a9582d75ull;
		hash = Combine(hash, key.reloadGeneration);
		hash = Combine(hash, key.targetPlanFingerprint);
		hash = Combine(hash, key.indexed ? 1 : 0);
		hash = Combine(hash, key.hasIndexBuffer ? 1 : 0);
		hash = Combine(hash, key.indexHash);
		hash = Combine(hash, key.vertexSlotMask);
		uint32_t slots = key.vertexSlotMask;
		for (UINT slot = 0; slot < key.vertexHashes.size(); ++slot) {
			if (slots & (1u << slot))
				hash = Combine(hash, key.vertexHashes[slot]);
		}
		hash = Combine(hash, key.vertexCount);
		hash = Combine(hash, key.indexCount);
		hash = Combine(hash, key.instanceCount);
		hash = Combine(hash, key.firstVertex);
		hash = Combine(hash, key.firstIndex);
		hash = Combine(hash, static_cast<UINT64>(static_cast<INT64>(key.baseVertex)));
		hash = Combine(hash, key.firstInstance);
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
			hasScope = false;
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
	bool hasScope = false;
	UINT64 scopeReloadGeneration = 0;
};

class DX12DirectIaProbeCacheUtils
{
public:
	static constexpr size_t MaxEntries = 4096;

	static DX12DirectIaProbeCacheKey MakeKey(
		UINT64 reloadGeneration,
		UINT64 targetPlanFingerprint,
		const DX12IaHashState &state,
		const DX12IaDrawInvocation &draw)
	{
		DX12DirectIaProbeCacheKey key;
		key.reloadGeneration = reloadGeneration;
		key.targetPlanFingerprint = targetPlanFingerprint;
		key.indexed = draw.indexed;
		key.hasIndexBuffer = state.hasIndexBuffer;
		key.indexHash = state.indexHash;
		for (const DX12IaBufferHash &buffer : state.vertexBuffers) {
			if (buffer.slot >= key.vertexHashes.size())
				continue;
			key.vertexSlotMask |= 1u << buffer.slot;
			key.vertexHashes[buffer.slot] = buffer.hash;
		}
		key.vertexCount = draw.vertexCount;
		key.indexCount = draw.indexCount;
		key.instanceCount = draw.instanceCount;
		key.firstVertex = draw.firstVertex;
		key.firstIndex = draw.firstIndex;
		key.baseVertex = draw.baseVertex;
		key.firstInstance = draw.firstInstance;
		return key;
	}

	static bool TryLoad(
		DX12DirectIaProbeCache *cache,
		const DX12DirectIaProbeCacheKey &key,
		bool *hasDirectCandidate)
	{
		if (!cache || !hasDirectCandidate)
			return false;
		PrepareScope(cache, key);

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
		PrepareScope(cache, key);

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
		cache->hasScope = false;
	}

private:
	static void PrepareScope(
		DX12DirectIaProbeCache *cache,
		const DX12DirectIaProbeCacheKey &key)
	{
		if (!cache)
			return;
		const bool sameScope = cache->hasScope &&
			cache->scopeReloadGeneration == key.reloadGeneration;
		if (sameScope)
			return;
		cache->entries.clear();
		cache->lruKeys.clear();
		cache->hasScope = true;
		cache->scopeReloadGeneration = key.reloadGeneration;
	}
};
