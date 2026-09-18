#pragma once

// Command-list tracking and IA hash/fast-reject/bound-draw caches.
// Split out of Core/DX12CommandState.h; include that umbrella header from existing code.
#include "../DX12Diagnostics.h"
#include "DX12IaDecisionState.h"
#include <bit>

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


// Signature of the IA-state-level "no override candidate" verdict
// (DX12ModIaStateDefinitelyHasNoCandidate). That predicate never looks at the
// raw IA mutation epoch: it reads the bound views of exactly the slots the plan
// requires, resolves their metadata-only view hashes, and probes the compiled
// prefilter tables. The signature below is that dependency set, so a verdict
// stays reusable while the bound views keep their values - including across the
// command-list Reset of a deterministic game that re-records the same bind
// sequence every frame (iaEpoch restarts at 1 there, which is why the
// single-entry epoch-keyed cache it replaces could only hit on immediate
// repeats: measured 15.7% on the profiled title).
//
// Per required slot there are exactly two "bound" inputs the verdict reads:
// ia.hasVertexBuffer[slot] (a slot that is not bound is skipped outright, and
// hasIndexBuffer likewise guards the index probe), and the metadata hash produced
// for that view. The raw view fields remain in the exact snapshot as a
// conservative guard, while the validated hash is what removes unrelated global
// resolve-epoch churn from the reusable verdict identity.
//
// The two accumulators are lookup accelerators, never proof of equality.
// Exact dependency snapshots below verify every signature hit. They are lazy,
// heap-owned and compacted to the required bound slots, avoiding a large inline
// 32-slot key array in DX12SimulateContext (also used on the dispatch stack).
class DX12IaStateNegativeSignature
{
public:
	UINT64 terms = 0;
	UINT64 slots = 0;

	__forceinline bool operator==(const DX12IaStateNegativeSignature &other) const
	{
		return terms == other.terms && slots == other.slots;
	}

	// Diffuse each field with splitmix64 and combine it with an ordered prefix.
	// Hash collisions only cause extra exact comparisons, never false hits.
	static __forceinline UINT64 Mix(UINT64 hash, UINT64 value)
	{
		UINT64 mixed = value + 0x9e3779b97f4a7c15ull;
		mixed = (mixed ^ (mixed >> 30)) * 0xbf58476d1ce4e5b9ull;
		mixed = (mixed ^ (mixed >> 27)) * 0x94d049bb133111ebull;
		// Rotate the accumulated prefix before folding the next field. Plain
		// XOR was commutative: swapping two slot views (or scalar fields)
		// produced exactly the same signature, not just a rare hash collision.
		return ((hash << 17) | (hash >> 47)) ^ (mixed ^ (mixed >> 31));
	}

	// The scalar dependency set and the per-slot views accumulate in separate
	// chains, so each keeps its own sensitivity and both stay order-sensitive:
	// a lookup that agrees on the scalars but not on the views still differs.
	// Holds the two accumulators rather than a finished signature: a nested class
	// cannot contain the still-incomplete enclosing type by value.
	class Builder
	{
	public:
		__forceinline void AddTerm(UINT64 value)
		{
			terms = Mix(terms, value);
		}

		__forceinline void AddSlot(UINT64 value)
		{
			slots = Mix(slots, value);
		}

		// A complete-class context, so the enclosing type can be built here.
		__forceinline DX12IaStateNegativeSignature Finish() const
		{
			DX12IaStateNegativeSignature signature;
			signature.terms = terms;
			signature.slots = slots;
			return signature;
		}

	private:
		UINT64 terms = 0;
		UINT64 slots = 0;
	};
};

// The IA negative verdict consumes hashes, not raw resource records. Keep the
// exact values that were refreshed by the authoritative hash path so the
// resident negative cache can survive unrelated resolve-epoch churn without
// trusting a recycled GPUVA. The valid masks mirror the prefilter gates: a
// value is present only when the verdict really probes that view.
class DX12IaStateNegativeHashSnapshot
{
public:
	bool indexHashValid = false;
	UINT32 indexHash = 0;
	UINT vertexHashMask = 0;
	UINT32 vertexHashes[D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};

	bool Matches(const DX12IaStateNegativeHashSnapshot &other) const
	{
		if (indexHashValid != other.indexHashValid ||
			(indexHashValid && indexHash != other.indexHash) ||
			vertexHashMask != other.vertexHashMask)
			return false;
		for (UINT remaining = vertexHashMask; remaining; remaining &= remaining - 1) {
			const UINT slot = std::countr_zero(remaining);
			if (vertexHashes[slot] != other.vertexHashes[slot])
				return false;
		}
		return true;
	}
};


// Non-owning lookup inputs: no per-draw allocation or copy of the 32-slot IA
// state. The required mask + indexChecked are the exact effective target plan
// read by the verdict; descriptor targets are excluded by its caller.
class DX12IaStateNegativeInputs
{
public:
	const DX12ActiveIaState &ia;
	UINT requiredVertexMask;
	bool indexChecked;
	bool indexed;
	UINT64 reloadGeneration;
	// The verdict reads the active pre-skin SECTION SET (a membership test on
	// config.section inside TextureOverrideMatchCsSatisfied), not the pre-skin
	// runtime generation: which resource a section currently publishes is not an
	// input. Keying on the runtime generation made the cache unusable on a title
	// whose Present clears and whose producer refills the active map every frame
	// - one eviction of every stored verdict per frame for a set that never
	// changed. This is DX12ModGetPreSkinSectionSetGenerationFast().
	UINT64 preSkinSectionGeneration;
	DX12IaStateNegativeHashSnapshot hashes;

	DX12IaStateNegativeInputs(
		const DX12ActiveIaState &state, const DX12IaHashTargetPlan &plan,
		bool isIndexed, UINT64 reload, UINT64 preSkinSections,
		const DX12IaStateNegativeHashSnapshot *hashSnapshot = nullptr)
		: ia(state),
		  requiredVertexMask(plan.requireAllIaBuffers ? UINT_MAX : plan.requireVertexBufferMask),
		  indexChecked(plan.RequiresIndexBuffer(isIndexed)), indexed(isIndexed),
		  reloadGeneration(reload), preSkinSectionGeneration(preSkinSections)
	{
		if (hashSnapshot)
			hashes = *hashSnapshot;
	}

	UINT BoundVertexMask() const
	{
		// Derive from the authoritative bindings, not a second maintained mask:
		// replay, restore and ordinary IASet paths all remain covered.
		UINT bound = 0;
		for (UINT slot = 0; slot < D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT; ++slot)
			bound |= static_cast<UINT>(ia.hasVertexBuffer[slot]) << slot;
		return bound & requiredVertexMask;
	}
};

class DX12IaStateNegativeExactKey
{
public:
	UINT requiredVertexMask = 0;
	UINT boundVertexMask = 0;
	bool indexChecked = false;
	bool indexed = false;
	bool hasIndexBuffer = false;
	D3D12_INDEX_BUFFER_VIEW indexView = {};
	UINT64 reloadGeneration = 0;
	UINT64 preSkinSectionGeneration = 0;
	DX12IaStateNegativeHashSnapshot hashes;
	std::vector<D3D12_VERTEX_BUFFER_VIEW> vertexViews;

	bool Matches(const DX12IaStateNegativeInputs &inputs) const
	{
		if (requiredVertexMask != inputs.requiredVertexMask ||
			indexChecked != inputs.indexChecked || indexed != inputs.indexed ||
			reloadGeneration != inputs.reloadGeneration ||
			preSkinSectionGeneration != inputs.preSkinSectionGeneration ||
			!hashes.Matches(inputs.hashes))
			return false;
		const DX12ActiveIaState &ia = inputs.ia;
		if (indexChecked) {
			if (hasIndexBuffer != ia.hasIndexBuffer)
				return false;
			if (hasIndexBuffer && (indexView.BufferLocation != ia.indexBuffer.BufferLocation ||
				indexView.SizeInBytes != ia.indexBuffer.SizeInBytes ||
				indexView.Format != ia.indexBuffer.Format))
				return false;
		}
		if (boundVertexMask != inputs.BoundVertexMask())
			return false;
		size_t viewIndex = 0;
		for (UINT remaining = boundVertexMask; remaining; remaining &= remaining - 1) {
			const UINT slot = std::countr_zero(remaining);
			const D3D12_VERTEX_BUFFER_VIEW &view = vertexViews[viewIndex++];
			const D3D12_VERTEX_BUFFER_VIEW &current = ia.vertexBuffers[slot];
			if (view.BufferLocation != current.BufferLocation ||
				view.SizeInBytes != current.SizeInBytes ||
				view.StrideInBytes != current.StrideInBytes)
				return false;
		}
		return true;
	}

	void Capture(const DX12IaStateNegativeInputs &inputs)
	{
		requiredVertexMask = inputs.requiredVertexMask;
		indexChecked = inputs.indexChecked;
		indexed = inputs.indexed;
		reloadGeneration = inputs.reloadGeneration;
		preSkinSectionGeneration = inputs.preSkinSectionGeneration;
		hashes = inputs.hashes;
		hasIndexBuffer = indexChecked && inputs.ia.hasIndexBuffer;
		if (hasIndexBuffer)
			indexView = inputs.ia.indexBuffer;
		boundVertexMask = inputs.BoundVertexMask();
		vertexViews.clear(); // retain capacity across replacement of this way
		for (UINT remaining = boundVertexMask; remaining; remaining &= remaining - 1) {
			const UINT slot = std::countr_zero(remaining);
			vertexViews.push_back(inputs.ia.vertexBuffers[slot]);
		}
	}
};

// Set-associative cache of the verdict above. The working set is "distinct IA
// states that reach the verdict", which on the profiled title is ~1100 per
// frame, so a single entry was never the right shape; this table keeps the
// repeated views of the frame-to-frame re-recorded draws resident without an
// LRU list or a per-hit clock update. The table and exact dependency snapshots
// are allocated lazily on the heap; no full key array lives on the hook stack.
//
// Sizing is empirical, not guessed. A deterministic game replays the same
// ~1100 signatures in the same order every frame, so the table behaves like a
// cyclic-replay cache: any set holding more states than ways keeps its first
// (kWayCount - 1) arrivals pinned and thrashes the rest through way 0, which
// caps the steady-state hit rate at (kWayCount - 1) / states-in-set for
// overfull sets. At 128x8 that predicted ~43% against the measured 44.8%
// (Rm.IaNegCacheHit vs Rm.IaNegVerdictRuns): nearly half of all draws paid the
// full verdict (~2.2us each, 1.26ms/frame) only because their entry had been
// evicted since the previous frame. A discrete replay simulation of the exact
// Store policy below (way 0 victim on a full set) gives, for a 1100-state
// working set: 128x8 ~41%, 256x8 ~77%, 512x8 ~98%, 256x16 ~99.8%, and 512x16
// ~100% - with 512x16 still holding ~99.9% at 2200 states and ~96% at 3300,
// i.e. a 3x larger scene than the profiled one. Round-robin victim selection
// scores 0% under cyclic replay and was rejected. 512x16 costs a 256KB lazy
// heap table per simulate context and the same one-set scan per lookup as any
// other shape; only the resident ways allocate exact keys.
class DX12IaStateNegativeCache
{
public:
	static constexpr size_t kSetCount = 512;
	static constexpr size_t kWayCount = 16;
	static constexpr size_t kEntryCount = kSetCount * kWayCount;

	static_assert((kSetCount & (kSetCount - 1)) == 0,
		"IA state negative cache set count must be a power of two");

	class Entry
	{
	public:
		DX12IaStateNegativeSignature signature;
		std::unique_ptr<DX12IaStateNegativeExactKey> exactKey;
		bool valid = false;
		bool noCandidate = false;
	};

	using Entries = std::array<Entry, kEntryCount>;
	std::unique_ptr<Entries> entries;

	DX12IaStateNegativeCache() = default;
	// Derived cache data is not part of a command-state value snapshot. Do not
	// duplicate heap-owned exact keys when simulate contexts are copied.
	DX12IaStateNegativeCache(const DX12IaStateNegativeCache &) {}
	DX12IaStateNegativeCache &operator=(const DX12IaStateNegativeCache &other)
	{
		if (this != &other && entries) {
			for (Entry &entry : *entries)
				entry.valid = false;
		}
		return *this;
	}
	DX12IaStateNegativeCache(DX12IaStateNegativeCache &&) noexcept = default;
	DX12IaStateNegativeCache &operator=(DX12IaStateNegativeCache &&) noexcept = default;

	// Both halves already end in a full avalanche, so their low bits carry every
	// folded term; folding them together keeps set selection independent of
	// which half a lookup happens to differ in.
	static __forceinline size_t SetFor(const DX12IaStateNegativeSignature &signature)
	{
		return static_cast<size_t>((signature.terms ^ signature.slots) & (kSetCount - 1));
	}

	static __forceinline bool Matches(
		const Entry &entry, const DX12IaStateNegativeSignature &signature)
	{
		return entry.valid && entry.signature == signature;
	}

	static const Entry *Find(
		const DX12IaStateNegativeCache &cache,
		const DX12IaStateNegativeSignature &signature,
		const DX12IaStateNegativeInputs &inputs)
	{
		if (!cache.entries)
			return nullptr;
		const Entry *set = cache.entries->data() + SetFor(signature) * kWayCount;
		for (size_t way = 0; way < kWayCount; ++way) {
			if (Matches(set[way], signature) && set[way].exactKey &&
				set[way].exactKey->Matches(inputs))
				return &set[way];
		}
		return nullptr;
	}

	// The first signature-matching or empty way is used, otherwise way 0 is
	// replaced. A digest collision may replace a different exact key but cannot
	// return its verdict: Find verifies the inputs. Recency affects hit rate
	// only; a per-hit age update would add writes to the hot path.
	static void Store(
		DX12IaStateNegativeCache *cache,
		const DX12IaStateNegativeSignature &signature,
		const DX12IaStateNegativeInputs &inputs,
		bool noCandidate)
	{
		if (!cache)
			return;
		if (!cache->entries)
			cache->entries = std::make_unique<Entries>();
		Entry *set = cache->entries->data() + SetFor(signature) * kWayCount;
		Entry *target = &set[0];
		for (size_t way = 0; way < kWayCount; ++way) {
			if (Matches(set[way], signature) || !set[way].valid) {
				target = &set[way];
				break;
			}
		}
		target->valid = false;
		if (!target->exactKey)
			target->exactKey = std::make_unique<DX12IaStateNegativeExactKey>();
		target->exactKey->Capture(inputs);
		target->signature = signature;
		target->valid = true;
		target->noCandidate = noCandidate;
	}
};

// Folds the exact inputs the verdict reads into the signature above. Taking the
// IA state and the plan rather than the whole simulate context keeps this header
// free of a dependency on DX12SimulateContext.h and states the dependency set of
// the cached verdict in the signature.
//
// The hash snapshot replaces the old global resolve epoch. Each snapshot value
// has just been refreshed by DX12HashIaBufferContentView, whose published-state
// and range validation already handle VA reuse and unresolved range changes.
// Keeping the raw view fields in the exact key remains conservative: a digest
// collision can only lose a hit, never return another view's verdict.
inline DX12IaStateNegativeSignature MakeIaStateNegativeSignature(
	const DX12IaStateNegativeInputs &inputs)
{
	const DX12ActiveIaState &ia = inputs.ia;
	DX12IaStateNegativeSignature::Builder signature;
	signature.AddTerm(inputs.reloadGeneration);
	// Section-set membership, not the pre-skin runtime generation: see
	// DX12IaStateNegativeInputs.
	signature.AddTerm(inputs.preSkinSectionGeneration);
	// DX12HashIaBufferContentView produces metadata-only hashes (including
	// its unresolved fallback). CPU content writes cannot change this verdict;
	// keep the full content-dependent draw caches' invalidation separate.
	signature.AddTerm(inputs.requiredVertexMask);
	signature.AddTerm(inputs.indexed ? 1u : 0u);
	// Mirrors DX12ModIaStateDefinitelyHasNoCandidate's probeIndexBuffer.
	const bool indexChecked = inputs.indexChecked;
	signature.AddTerm(indexChecked ? 1u : 0u);
	if (indexChecked) {
		signature.AddTerm(ia.hasIndexBuffer ? 1u : 0u);
		signature.AddTerm(inputs.hashes.indexHashValid ? 1u : 0u);
		if (inputs.hashes.indexHashValid)
			signature.AddTerm(inputs.hashes.indexHash);
		if (ia.hasIndexBuffer) {
			signature.AddTerm(ia.indexBuffer.BufferLocation);
			signature.AddTerm(ia.indexBuffer.SizeInBytes);
			signature.AddTerm(static_cast<UINT>(ia.indexBuffer.Format));
		}
	}

	// Encode absence once as a mask, rather than serially mixing three words
	// for every empty slot of an all-IA plan on every cache hit. Exact-key
	// validation still checks every dependency, including forced collisions.
	const UINT boundMask = inputs.BoundVertexMask();
	const UINT hashMask = inputs.hashes.vertexHashMask & inputs.requiredVertexMask;
	signature.AddSlot(boundMask);
	signature.AddSlot(hashMask);
	for (UINT remaining = boundMask | hashMask; remaining; remaining &= remaining - 1) {
		const UINT slot = std::countr_zero(remaining);
		signature.AddSlot(slot);
		if (hashMask & (1u << slot))
			signature.AddSlot(inputs.hashes.vertexHashes[slot]);
		if (boundMask & (1u << slot)) {
			const D3D12_VERTEX_BUFFER_VIEW &view = ia.vertexBuffers[slot];
			signature.AddSlot(view.BufferLocation);
			signature.AddSlot(view.SizeInBytes);
			signature.AddSlot(view.StrideInBytes);
		}
	}
	return signature.Finish();
}


// Exact, command-list-local negative result. The raw view snapshot keeps the
// cache conservative for binding changes, while the validated per-view hashes
// cover resource identity and unresolved range changes. The old global resolve
// epoch is deliberately not part of this key: unrelated buffer churn must not
// discard a verdict whose hash inputs were revalidated by the authoritative path.
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
