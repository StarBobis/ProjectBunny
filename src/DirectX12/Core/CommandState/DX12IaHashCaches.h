#pragma once

// Command-list tracking and IA hash/fast-reject/bound-draw caches.
// Split out of Core/DX12CommandState.h; include that umbrella header from existing code.
#include "../DX12Diagnostics.h"

class DX12CommandListTrackingState
{
public:
	bool trackBindings = false;
	bool trackIaBindings = false;
	bool trackPipelineStateBindings = false;
	bool recordBindingEvents = false;
	bool trackHuntIa = false;
	bool trackPsoState = false;
};


class DX12CommandListTrackingCache : public DX12CommandListTrackingState
{
public:
	UINT64 generation = 0;
};


class DX12PreSkinCacheIdentityUtils
{
public:
	static std::uint64_t MakeProbeFingerprint(
		std::uint64_t bindingFingerprint,
		std::uint64_t iaContentEpoch)
	{
		// PreSkin candidate selection depends on CS identity and the stable
		// Binding State fingerprint. Dispatch/Copy activity does not alter that
		// plan; positive hits still execute replacement commands every dispatch.
		(void)iaContentEpoch;
		return bindingFingerprint ? bindingFingerprint : 1;
	}
};


class DX12IaBufferHash
{
public:
	UINT slot = 0;
	uint32_t hash = 0;
	D3D12_VERTEX_BUFFER_VIEW vertexView = {};
};


class DX12IaHashState
{
public:
	bool hasIndexBuffer = false;
	uint32_t indexHash = 0;
	D3D12_INDEX_BUFFER_VIEW indexView = {};
	std::vector<DX12IaBufferHash> vertexBuffers;
};


class DX12IaDrawInvocation
{
public:
	bool indexed = false;
	uint32_t vertexCount = 0;
	uint32_t indexCount = 0;
	uint32_t instanceCount = 0;
	uint32_t firstVertex = 0;
	uint32_t firstIndex = 0;
	INT baseVertex = 0;
	uint32_t firstInstance = 0;

	static DX12IaDrawInvocation ForDrawInstanced(
		uint32_t vertexCount, uint32_t instanceCount,
		uint32_t firstVertex, uint32_t firstInstance)
	{
		DX12IaDrawInvocation draw;
		draw.indexed = false;
		draw.vertexCount = vertexCount;
		draw.instanceCount = instanceCount;
		draw.firstVertex = firstVertex;
		draw.firstInstance = firstInstance;
		return draw;
	}

	static DX12IaDrawInvocation ForDrawIndexedInstanced(
		uint32_t indexCount, uint32_t instanceCount,
		uint32_t firstIndex, INT baseVertex, uint32_t firstInstance)
	{
		DX12IaDrawInvocation draw;
		draw.indexed = true;
		draw.indexCount = indexCount;
		draw.instanceCount = instanceCount;
		draw.firstIndex = firstIndex;
		draw.baseVertex = baseVertex;
		draw.firstInstance = firstInstance;
		return draw;
	}
};


class DX12ResidentCacheGrowthUtils
{
public:
	static size_t NextBudget(size_t currentBudget, size_t minRequired)
	{
		size_t next = currentBudget ? currentBudget : 256;
		while (next < minRequired) {
			const size_t doubled = next * 2;
			if (doubled <= next) {
				next = minRequired;
				break;
			}
			next = doubled;
		}
		return next;
	}

	template <class AssocContainer>
	static void EnsureBudget(
		size_t *budget,
		AssocContainer *entries,
		size_t minRequired)
	{
		if (!budget || !entries || minRequired <= *budget)
			return;
		const size_t next = NextBudget(*budget, minRequired);
		entries->reserve(next);
		*budget = next;
	}
};

class DX12IaHashStateCacheKey
{
public:
	UINT64 targetPlanFingerprint = 0;
	UINT64 iaViewFingerprint = 0;
	UINT64 iaContentEpoch = 0;
	UINT64 bufferContentGeneration = 0;
	UINT64 bufferResolveGeneration = 0;
	bool usesGlobalFallback = false;
	bool indexed = false;
	UINT vertexCount = 0;
	UINT indexCount = 0;
	UINT firstVertex = 0;
	UINT firstIndex = 0;
	INT baseVertex = 0;

	__forceinline bool operator==(const DX12IaHashStateCacheKey &other) const
	{
		return targetPlanFingerprint == other.targetPlanFingerprint &&
			iaViewFingerprint == other.iaViewFingerprint &&
			iaContentEpoch == other.iaContentEpoch &&
			bufferContentGeneration == other.bufferContentGeneration &&
			bufferResolveGeneration == other.bufferResolveGeneration &&
			usesGlobalFallback == other.usesGlobalFallback &&
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
	static __forceinline size_t Combine(size_t hash, UINT64 value)
	{
		hash ^= static_cast<size_t>(value) + 0x9e3779b97f4a7c15ull +
			(hash << 6) + (hash >> 2);
		return hash;
	}

	__forceinline size_t operator()(const DX12IaHashStateCacheKey &key) const
	{
		size_t hash = 0x84222325a9582d75ull;
		hash = Combine(hash, key.targetPlanFingerprint);
		hash = Combine(hash, key.iaViewFingerprint);
		hash = Combine(hash, key.iaContentEpoch);
		hash = Combine(hash, key.bufferContentGeneration);
		hash = Combine(hash, key.bufferResolveGeneration);
		hash = Combine(hash, key.usesGlobalFallback ? 1 : 0);
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

class DX12IaHashKeyPrefixCacheEntry
{
public:
	bool valid = false;
	bool indexed = false;
	UINT64 iaEpoch = 0;
	UINT64 targetPlanFingerprint = 0;
	UINT64 globalContentGeneration = 0;
	UINT64 globalResolveGeneration = 0;
	bool usesGlobalFallback = false;
	DX12IaHashStateCacheKey prefix;
};

class DX12IaHashKeyPrefixCacheUtils
{
public:
	static __forceinline bool TryLoadStable(
		const DX12IaHashKeyPrefixCacheEntry &entry,
		UINT64 iaEpoch,
		UINT64 targetPlanFingerprint,
		bool indexed,
		DX12IaHashStateCacheKey *key)
	{
		if (!key || !entry.valid || entry.iaEpoch != iaEpoch ||
				entry.targetPlanFingerprint != targetPlanFingerprint ||
				entry.indexed != indexed || entry.usesGlobalFallback)
			return false;
		*key = entry.prefix;
		return true;
	}

	static __forceinline bool TryLoad(
		const DX12IaHashKeyPrefixCacheEntry &entry,
		UINT64 iaEpoch,
		UINT64 targetPlanFingerprint,
		bool indexed,
			UINT64 globalContentGeneration,
			UINT64 globalResolveGeneration,
			DX12IaHashStateCacheKey *key)
	{
		if (!key || !entry.valid || entry.iaEpoch != iaEpoch ||
				entry.targetPlanFingerprint != targetPlanFingerprint ||
				entry.indexed != indexed)
			return false;
		if (entry.usesGlobalFallback &&
				(entry.globalContentGeneration != globalContentGeneration ||
				 entry.globalResolveGeneration != globalResolveGeneration))
			return false;
		*key = entry.prefix;
		return true;
	}

	static __forceinline void Store(
		DX12IaHashKeyPrefixCacheEntry *entry,
		UINT64 iaEpoch,
		UINT64 targetPlanFingerprint,
		bool indexed,
		UINT64 globalContentGeneration,
		UINT64 globalResolveGeneration,
		const DX12IaHashStateCacheKey &prefix)
	{
		if (!entry)
			return;
		entry->iaEpoch = iaEpoch;
		entry->targetPlanFingerprint = targetPlanFingerprint;
		entry->indexed = indexed;
		entry->globalContentGeneration = globalContentGeneration;
		entry->globalResolveGeneration = globalResolveGeneration;
		entry->usesGlobalFallback = prefix.usesGlobalFallback;
		entry->prefix = prefix;
		entry->valid = true;
	}
};

class DX12IaHashStateCache
{
public:
	static constexpr size_t MaxEntries = 16384;

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
	size_t growthBudget = 16384;
};

class DX12IaHashStateCacheUtils
{
public:

	static __forceinline bool TryLoad(
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
		if (cache->entries.size() >= DX12IaHashStateCache::MaxEntries &&
		    !cache->lruKeys.empty()) {
			const DX12IaHashStateCacheKey &oldest = cache->lruKeys.back();
			cache->entries.erase(oldest);
			cache->lruKeys.pop_back();
			if (evicted)
				*evicted = true;
		}
		DX12ResidentCacheGrowthUtils::EnsureBudget(
			&cache->growthBudget, &cache->entries,
			(std::min)(cache->entries.size() + 1, DX12IaHashStateCache::MaxEntries));

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


// Exact, command-list-local negative result. Unlike the legacy unordered cache,
// this key uses the IA mutation epoch as the identity of all bound views. The
// global generations cover writes/resource reuse, so no content-hash result is
// reused after any dependency can have changed.
class DX12ResidentDrawNegativeEntry
{
public:
	ID3D12PipelineState *pipelineState = nullptr;
	UINT64 reloadGeneration = 0;
	UINT64 preSkinGeneration = 0;
	UINT64 contentGeneration = 0;
	UINT64 resolveGeneration = 0;
	UINT64 iaEpoch = 0;
	UINT64 graphicsDescriptorQuerySerial = 0;
	ID3D12DescriptorHeap *graphicsDescriptorHeap = nullptr;
	UINT64 graphicsDescriptorHeapVersion = 0;
	UINT64 targetPlanFingerprint = 0;
	UINT vertexCount = 0;
	UINT indexCount = 0;
	UINT instanceCount = 0;
	UINT firstVertex = 0;
	UINT firstIndex = 0;
	INT baseVertex = 0;
	UINT firstInstance = 0;
	bool indexed = false;
	bool valid = false;
};

// Top-level draw-result cache that captures the full bound-state identity.
// When nothing changes (PSO / IA / root descriptors / heap), the per-draw
// override decision must be identical. A hit here skips the entire multi-layer
// dynamic decision chain (IA hash, descriptor match, resource hash, etc.).
class DX12BoundDrawResultCache
{
public:
	bool valid = false;
	bool hasOverride = false;
	UINT64 iaEpoch = 0;
	UINT64 iaContentEpoch = 0;
	// Global buffer-content generation (gBufferContentGeneration): the
	// per-command-list iaContentEpoch only bumps on that list's own copy
	// calls, so content written by another list (upload list, Map/Unmap on
	// a shared buffer) would otherwise leave this cache hit with a stale
	// decision across command-list Reset / scene transitions - the root
	// cause of intermittent wrong IA replacement (shadow mismatch flicker).
	UINT64 bufferContentGeneration = 0;
	UINT64 graphicsDescriptorQuerySerial = 0;
	ID3D12PipelineState *pipelineState = nullptr;
	UINT64 reloadGeneration = 0;
	UINT64 preSkinGeneration = 0;
	ID3D12DescriptorHeap *cbvSrvUavHeap = nullptr;
	UINT64 cbvSrvUavHeapVersion = 0;
	ID3D12RootSignature *graphicsRootSignature = nullptr;
	bool indexed = false;
	UINT vertexCount = 0;
	UINT indexCount = 0;
	UINT instanceCount = 0;
	UINT firstVertex = 0;
	UINT firstIndex = 0;
	INT baseVertex = 0;
	UINT firstInstance = 0;

	void Reset()
	{
		valid = false;
		hasOverride = false;
	}

	// First comparison that rejected the cached result. Reported to the TIMING
	// counters because the per-draw miss rate is dominated by one or two terms
	// and guessing which one costs whole test rounds.
	enum class MissReason : UINT
	{
		None = 0,
		Invalid,
		IaEpoch,
		IaContentEpoch,
		BufferContentGeneration,
		PipelineState,
		ReloadGeneration,
		DescriptorQuerySerial,
		PreSkinGeneration,
		Heap,
		HeapVersion,
		RootSignature,
		DrawArgs,
		Count,
	};

	// Stable pointer per reason: FindBucket compares the counter name pointer,
	// so every sample must pass the very same literal.
	static const char *MissCounterName(MissReason reason)
	{
		static const char *const names[static_cast<UINT>(MissReason::Count)] = {
			"Rm.BoundCacheMissNone",
			"Rm.BoundCacheMissInvalid",
			"Rm.BoundCacheMissIaEpoch",
			"Rm.BoundCacheMissIaContentEpoch",
			"Rm.BoundCacheMissBufferContentGeneration",
			"Rm.BoundCacheMissPipelineState",
			"Rm.BoundCacheMissReloadGeneration",
			"Rm.BoundCacheMissDescriptorQuerySerial",
			"Rm.BoundCacheMissPreSkinGeneration",
			"Rm.BoundCacheMissHeap",
			"Rm.BoundCacheMissHeapVersion",
			"Rm.BoundCacheMissRootSignature",
			"Rm.BoundCacheMissDrawArgs",
		};
		const UINT index = static_cast<UINT>(reason);
		return index < static_cast<UINT>(MissReason::Count) ? names[index] : names[0];
	}

	bool Matches(
		UINT64 currentIaEpoch,
		UINT64 currentIaContentEpoch,
		UINT64 currentBufferContentGeneration,
		UINT64 currentGraphicsDescriptorQuerySerial,
		ID3D12PipelineState *currentPso,
		UINT64 currentReloadGeneration,
		UINT64 currentPreSkinGeneration,
		ID3D12DescriptorHeap *currentHeap,
		UINT64 currentHeapVersion,
		ID3D12RootSignature *currentRootSig,
		const DX12IaDrawInvocation &draw,
		bool compareDescriptorState,
		bool compareDrawArgs,
		MissReason *missReason = nullptr) const
	{
		auto reject = [missReason](MissReason reason) {
			if (missReason)
				*missReason = reason;
			return false;
		};
		if (!valid)
			return reject(MissReason::Invalid);
		if (iaEpoch != currentIaEpoch)
			return reject(MissReason::IaEpoch);
		if (iaContentEpoch != currentIaContentEpoch)
			return reject(MissReason::IaContentEpoch);
		if (bufferContentGeneration != currentBufferContentGeneration)
			return reject(MissReason::BufferContentGeneration);
		if (pipelineState != currentPso)
			return reject(MissReason::PipelineState);
		if (reloadGeneration != currentReloadGeneration)
			return reject(MissReason::ReloadGeneration);
		if (compareDescriptorState) {
			if (graphicsDescriptorQuerySerial != currentGraphicsDescriptorQuerySerial)
				return reject(MissReason::DescriptorQuerySerial);
			if (preSkinGeneration != currentPreSkinGeneration)
				return reject(MissReason::PreSkinGeneration);
			if (cbvSrvUavHeap != currentHeap)
				return reject(MissReason::Heap);
			if (cbvSrvUavHeapVersion != currentHeapVersion)
				return reject(MissReason::HeapVersion);
			if (graphicsRootSignature != currentRootSig)
				return reject(MissReason::RootSignature);
		}
		if (compareDrawArgs &&
			(indexed != draw.indexed ||
			 vertexCount != draw.vertexCount ||
			 indexCount != draw.indexCount ||
			 instanceCount != draw.instanceCount ||
			 firstVertex != draw.firstVertex ||
			 firstIndex != draw.firstIndex ||
			 baseVertex != draw.baseVertex ||
			 firstInstance != draw.firstInstance))
			return reject(MissReason::DrawArgs);
		return true;
	}

	void Capture(
		UINT64 currentIaEpoch,
		UINT64 currentIaContentEpoch,
		UINT64 currentBufferContentGeneration,
		UINT64 currentGraphicsDescriptorQuerySerial,
		ID3D12PipelineState *currentPso,
		UINT64 currentReloadGeneration,
		UINT64 currentPreSkinGeneration,
		ID3D12DescriptorHeap *currentHeap,
		UINT64 currentHeapVersion,
		ID3D12RootSignature *currentRootSig,
		const DX12IaDrawInvocation &draw,
		bool overrideResult)
	{
		valid = true;
		hasOverride = overrideResult;
		iaEpoch = currentIaEpoch;
		iaContentEpoch = currentIaContentEpoch;
		bufferContentGeneration = currentBufferContentGeneration;
		graphicsDescriptorQuerySerial = currentGraphicsDescriptorQuerySerial;
		pipelineState = currentPso;
		reloadGeneration = currentReloadGeneration;
		preSkinGeneration = currentPreSkinGeneration;
		cbvSrvUavHeap = currentHeap;
		cbvSrvUavHeapVersion = currentHeapVersion;
		graphicsRootSignature = currentRootSig;
		indexed = draw.indexed;
		vertexCount = draw.vertexCount;
		indexCount = draw.indexCount;
		instanceCount = draw.instanceCount;
		firstVertex = draw.firstVertex;
		firstIndex = draw.firstIndex;
		baseVertex = draw.baseVertex;
		firstInstance = draw.firstInstance;
	}
};
