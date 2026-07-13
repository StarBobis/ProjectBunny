#pragma once

#include <Windows.h>

#include <array>
#include <unordered_set>

#include "DX12ActiveIaState.hpp"
#include "DX12IaHashTargetPlan.hpp"
#include "DX12IaDrawInvocation.hpp"

class DX12DirectIaFastRejectCacheKey
{
public:
	UINT64 reloadGeneration = 0;
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
		return reloadGeneration == other.reloadGeneration &&
			targetPlanFingerprint == other.targetPlanFingerprint &&
			iaViewFingerprint == other.iaViewFingerprint &&
			contentGeneration == other.contentGeneration &&
			resolveGeneration == other.resolveGeneration &&
			requiresIndexBuffer == other.requiresIndexBuffer &&
			requiredVertexMask == other.requiredVertexMask &&
			hasIndexBuffer == other.hasIndexBuffer &&
			indexBufferLocation == other.indexBufferLocation &&
			indexBufferSize == other.indexBufferSize &&
			indexBufferFormat == other.indexBufferFormat &&
			vertexBufferMask == other.vertexBufferMask &&
			vertexBufferLocations == other.vertexBufferLocations &&
			vertexBufferSizes == other.vertexBufferSizes &&
			vertexBufferStrides == other.vertexBufferStrides &&
			indexed == other.indexed &&
			vertexCount == other.vertexCount &&
			indexCount == other.indexCount &&
			instanceCount == other.instanceCount &&
			firstVertex == other.firstVertex &&
			firstIndex == other.firstIndex &&
			baseVertex == other.baseVertex &&
			firstInstance == other.firstInstance;
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
	DX12DirectIaFastRejectCache() = default;

	DX12DirectIaFastRejectCache(const DX12DirectIaFastRejectCache &)
	{
	}

	DX12DirectIaFastRejectCache &operator=(const DX12DirectIaFastRejectCache &other)
	{
		if (this != &other)
			entries.clear();
		return *this;
	}

	DX12DirectIaFastRejectCache(DX12DirectIaFastRejectCache &&) noexcept = default;
	DX12DirectIaFastRejectCache &operator=(DX12DirectIaFastRejectCache &&) noexcept = default;

	std::unordered_set<
		DX12DirectIaFastRejectCacheKey,
		DX12DirectIaFastRejectCacheKeyHash> entries;
};

class DX12DirectIaFastRejectCacheUtils
{
public:
	static constexpr size_t MaxEntries = 4096;

	static UINT64 MixViewValue(UINT64 fingerprint, UINT64 value)
	{
		return fingerprint ^ (value + 0x9e3779b97f4a7c15ull +
			(fingerprint << 6) + (fingerprint >> 2));
	}

	static UINT64 BuildViewFingerprint(
		const DX12ActiveIaState &ia,
		const DX12IaHashTargetPlan &targetPlan,
		bool indexed)
	{
		UINT64 fingerprint = 0x84222325a9582d75ull;
		if (targetPlan.RequiresIndexBuffer(indexed)) {
			fingerprint = MixViewValue(fingerprint, ia.hasIndexBuffer ? 1 : 0);
			if (ia.hasIndexBuffer) {
				fingerprint = MixViewValue(fingerprint, ia.indexBuffer.BufferLocation);
				fingerprint = MixViewValue(fingerprint, ia.indexBuffer.SizeInBytes);
				fingerprint = MixViewValue(fingerprint, ia.indexBuffer.Format);
			}
		}
		for (UINT slot = 0; slot < ARRAYSIZE(ia.vertexBuffers); ++slot) {
			if (!targetPlan.RequiresVertexBuffer(slot))
				continue;
			fingerprint = MixViewValue(fingerprint, slot + 1);
			fingerprint = MixViewValue(
				fingerprint, ia.hasVertexBuffer[slot] ? 1 : 0);
			if (!ia.hasVertexBuffer[slot])
				continue;
			const D3D12_VERTEX_BUFFER_VIEW &view = ia.vertexBuffers[slot];
			fingerprint = MixViewValue(fingerprint, view.BufferLocation);
			fingerprint = MixViewValue(fingerprint, view.SizeInBytes);
			fingerprint = MixViewValue(fingerprint, view.StrideInBytes);
		}
		return fingerprint;
	}

	static DX12DirectIaFastRejectCacheKey MakeKey(
		UINT64 reloadGeneration,
		UINT64 contentGeneration,
		UINT64 resolveGeneration,
		const DX12ActiveIaState &ia,
		const DX12IaHashTargetPlan &targetPlan,
		const DX12IaDrawInvocation &draw)
	{
		DX12DirectIaFastRejectCacheKey key;
		key.reloadGeneration = reloadGeneration;
		key.targetPlanFingerprint = targetPlan.Fingerprint();
		key.iaViewFingerprint = BuildViewFingerprint(ia, targetPlan, draw.indexed);
		key.contentGeneration = contentGeneration;
		key.resolveGeneration = resolveGeneration;
		key.requiresIndexBuffer = targetPlan.RequiresIndexBuffer(draw.indexed);
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
			if (!ia.hasVertexBuffer[slot])
				continue;
			key.vertexBufferMask |= 1u << slot;
			key.vertexBufferLocations[slot] = ia.vertexBuffers[slot].BufferLocation;
			key.vertexBufferSizes[slot] = ia.vertexBuffers[slot].SizeInBytes;
			key.vertexBufferStrides[slot] = ia.vertexBuffers[slot].StrideInBytes;
		}
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
		const DX12DirectIaFastRejectCache &cache,
		const DX12DirectIaFastRejectCacheKey &key)
	{
		return cache.entries.find(key) != cache.entries.end();
	}

	static bool Store(
		DX12DirectIaFastRejectCache *cache,
		const DX12DirectIaFastRejectCacheKey &key)
	{
		if (!cache)
			return false;
		if (cache->entries.size() >= MaxEntries)
			cache->entries.erase(cache->entries.begin());
		return cache->entries.insert(key).second;
	}

	static void Clear(DX12DirectIaFastRejectCache *cache)
	{
		if (cache)
			cache->entries.clear();
	}
};
