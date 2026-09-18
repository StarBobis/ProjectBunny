// Production bodies are extracted by ia_match_perf_test.ps1. D3D types are real;
// resource resolution is doubled; mod globals/build/publication and draw-template
// move types/statements are extracted from their actual production definitions.
// No game, GPU, full project build, or production source rewriting is involved.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <d3d12.h>
#include <array>
#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwctype>
#include <list>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

static void Check(bool ok, const char *what) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); std::exit(1); }
}
class DX12ActiveIaState {
public:
    bool hasIndexBuffer = false;
    D3D12_INDEX_BUFFER_VIEW indexBuffer{};
    bool indexBufferHashValid = false;
    uint32_t indexBufferHash = 0;
    UINT64 indexBufferHashResolveGeneration = 0;
    bool hasVertexBuffer[32]{};
    D3D12_VERTEX_BUFFER_VIEW vertexBuffers[32]{};
    bool vertexBufferHashValid[32]{};
    uint32_t vertexBufferHashes[32]{};
    UINT64 vertexBufferHashResolveGeneration[32]{};
};
#include "ia_plan_under_test.h"
#include "ia_cache_under_test.h"
#include "ia_draw_template_under_test.h"
#include "ia_reset_moves_under_test.h"

class DX12ResourcePublishedState {
public:
    UINT64 generation = 1;
    bool TryRead(UINT64 expected, void *, void *, void *) const { return expected == generation; }
};
class DX12BufferResourceSummary {
public:
    ID3D12Resource *resource = nullptr;
    bool hasResourceDesc = false;
    D3D12_RESOURCE_DESC resourceDesc{};
    UINT64 resourceOffset = 0;
    UINT resourceHeapType = 1;
    UINT64 resourceGeneration = 1;
    DX12ResourcePublishedState *publishedState = nullptr;
};
class ResourceFixture {
public:
    UINT64 va = 0;
    UINT64 width = 0;
    DX12ResourcePublishedState token;
};
static std::map<UINT64, ResourceFixture> resources;
static std::atomic<UINT64> gBufferResolveInvalidationEpochAtomic{1};
static std::atomic<UINT64> gBufferContentGeneration{1};
static SRWLOCK gResourceLock = SRWLOCK_INIT;
static size_t resolveCalls = 0, wrapperCalls = 0, wrapperStores = 0;
// Range history, modelled on the production one: an invalidation records the
// range that changed for that epoch, and a query asks whether anything recorded
// since then overlaps it. A bare epoch move (unrelated buffer churn) must NOT
// count as evidence that a specific range changed - that distinction is the
// whole point of the range-scoped revalidation the caches now use.
struct InvalidatedRange { UINT64 begin; UINT64 end; };
static std::map<UINT64, InvalidatedRange> invalidatedRanges;
static void InvalidateRange(UINT64 begin, UINT64 end)
{
    static std::atomic<UINT64> nextEpoch{1};
    const UINT64 epoch = ++nextEpoch;
    invalidatedRanges[epoch] = { begin, end };
    gBufferResolveInvalidationEpochAtomic.store(epoch);
}
class DX12BufferResolveCacheUtils {
public:
    static bool RangeInvalidatedSinceLocked(UINT64 begin, UINT64 end, UINT64 since) {
        const UINT64 current = gBufferResolveInvalidationEpochAtomic.load();
        if (since == current) return false;
        if (since > current) return true;
        for (UINT64 epoch = since + 1; epoch <= current; ++epoch) {
            auto found = invalidatedRanges.find(epoch);
            if (found == invalidatedRanges.end()) return true; // gap: be conservative
            if (begin < found->second.end && found->second.begin < end) return true;
        }
        return false;
    }
};
static bool DX12ResolveBufferResourceByGpuVa(UINT64 va, UINT64 size, DX12BufferResourceSummary *out) {
    ++resolveCalls;
    for (auto &[base, r] : resources) {
        if (va < base || va - base >= r.width || size > r.width - (va - base)) continue;
        out->resource = reinterpret_cast<ID3D12Resource*>(base);
        out->hasResourceDesc = true;
        out->resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        out->resourceDesc.Width = r.width;
        out->resourceDesc.Height = 1;
        out->resourceDesc.DepthOrArraySize = 1;
        out->resourceDesc.MipLevels = 1;
        out->resourceDesc.SampleDesc.Count = 1;
        out->resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        out->resourceOffset = va - base;
        out->resourceGeneration = r.token.generation;
        out->publishedState = &r.token;
        return true;
    }
    return false;
}
static UINT64 DX12GetBufferContentGenerationForResource(ID3D12Resource *) { return gBufferContentGeneration.load(); }
static uint32_t MakeTrackerFourCC(char a, char b, char c, char d) {
    return uint32_t(a) | (uint32_t(b) << 8) | (uint32_t(c) << 16) | (uint32_t(d) << 24);
}
// Substitute only the byte hash primitive, not its production inputs/cache paths.
static uint32_t HashBytes(uint32_t hash, const void *bytes, size_t size) {
    auto *p = static_cast<const uint8_t*>(bytes);
    for (size_t i = 0; i < size; ++i) hash = (hash ^ p[i]) * 16777619u;
    return hash;
}
static UINT64 DX12GetIaHashResolveGeneration() { return gBufferResolveInvalidationEpochAtomic.load(); }
#define DX12_PROF_COUNT(name) ((void)0)
#include "ia_hash_memo_under_test.h"
#include "ia_hash_under_test.h"
class DX12CommandListRuntime {
public:
    static uint32_t GetOrBuildCachedIndexViewHash(DX12ActiveIaState *);
    static uint32_t GetOrBuildCachedVertexViewHash(DX12ActiveIaState *, UINT);
};
// These would catch an accidental reintroduction of the wrapper slow path.
bool DX12TryGetWrapperIaHash(UINT64, UINT64, UINT, UINT, UINT64, uint32_t *) { ++wrapperCalls; return false; }
void DX12StoreWrapperIaHash(UINT64, UINT64, UINT, UINT, UINT64, uint32_t) { ++wrapperStores; }
#include "ia_slots_under_test.h"

namespace Bunny {
enum class NumericMatchOp { Equal, NotEqual };
class NumericMatch { public: bool enabled = false; NumericMatchOp op = NumericMatchOp::Equal; uint32_t value = 0; };
enum class CommandListActionKind { Draw, DrawIndexed };
class CommandListAction { public: CommandListActionKind kind = CommandListActionKind::Draw; };
class TextureOverrideConfig {
public:
    uint32_t hash = 0;
    bool hasMatchVertexCount = false, hasMatchIndexCount = false, hasMatchInstanceCount = false;
    bool hasMatchFirstVertex = false, hasMatchFirstIndex = false, hasMatchFirstInstance = false;
    bool handlingSkip = false, hasCsHash = false;
    NumericMatch matchVertexCount, matchIndexCount;
    std::wstring indexBufferResource, section;
    std::map<uint32_t, std::wstring> vertexBufferResources;
    std::vector<CommandListAction> actions;
    int matchPriority = 0;
};
using TextureOverrideMap = std::map<uint32_t, std::vector<TextureOverrideConfig>>;
class IniDocumentText { public: static std::wstring ToLower(std::wstring s) {
    for (auto &c : s) c = static_cast<wchar_t>(std::towlower(c)); return s;
} };
}
#include "ia_globals_under_test.h"
static bool LookupRuntimeEffect(const std::unordered_map<std::wstring, bool> &effects, const std::wstring &key) {
    auto it = effects.find(key); return it != effects.end() && it->second;
}
static std::unordered_set<std::wstring> activeSections;
static const std::unordered_set<std::wstring> &GetActivePreSkinSectionsSnapshot() { return activeSections; }
static bool DX12ModIaPrefilterCountSensitive() { return false; }
#include "ia_matching_under_test.h"

// Reference is the previous generic prefilter + hash comparison, using the
// production prefilter/binding/match_cs functions and production-built indexes.
static bool ReferenceNoCandidate(bool ib, UINT slot, uint32_t hash) {
    std::vector<size_t> scratch;
    auto *indexes = FindIaTextureOverridePrefilterCandidatesLocked(ib, slot, 0, 0, &scratch);
    if (!indexes) return true;
    for (size_t i : *indexes) {
        if (i >= gIaTextureOverrides.size()) continue;
        auto *config = gIaTextureOverrides[i].config;
        if (config && config->hash == hash &&
            TextureOverrideMatchCsSatisfied(*config, activeSections) &&
            TextureOverrideMatchesIaBinding(*config, ib, slot)) return false;
    }
    return true;
}
#include "ia_publication_under_test.h"
static void TestPublication() {
    Bunny::TextureOverrideMap first, second, empty;
    Bunny::TextureOverrideConfig wildcard;
    wildcard.hash = 11; wildcard.handlingSkip = true; wildcard.section = L"wildcard";
    first[11].push_back(wildcard);
    Build(first);
    Check(!IaHashDefinitelyHasNoCandidate(true, 0, 11) &&
        !IaHashDefinitelyHasNoCandidate(false, 31, 11), "published IB/any-slot hash buckets are live");
    Check(gIaTextureOverrides[0].config == &gTextureOverrides.at(11)[0], "candidate pointers refer to published owner");
    Bunny::TextureOverrideConfig specific;
    specific.hash = 22; specific.indexBufferResource = L"ib";
    specific.vertexBufferResources[3] = L"vb"; specific.section = L"specific";
    second[22].push_back(specific);
    Build(second);
    Check(IaHashDefinitelyHasNoCandidate(true, 0, 11) &&
        IaHashDefinitelyHasNoCandidate(false, 31, 11), "reload removes old hash buckets");
    Check(!IaHashDefinitelyHasNoCandidate(true, 0, 22) &&
        !IaHashDefinitelyHasNoCandidate(false, 3, 22) &&
        IaHashDefinitelyHasNoCandidate(false, 31, 22), "reload publishes new IB/specific buckets");
    Check(gIaTextureOverrides[0].config == &gTextureOverrides.at(22)[0], "reload preserves new config ownership");
    Build(empty);
    Check(gIaTextureOverrides.empty() && gIaIndexTextureOverrideIndex.empty() &&
        gIaAnyVertexTextureOverrideIndex.empty() && gIaMergedVertexTextureOverrideIndex.empty(),
        "empty reload clears candidate vector and all hash indexes");
    std::printf("PASS real declarations/build/publication: owning swap, reload, empty cleanup, lock/generation guard\n");
}
static void TestNegativeHashSnapshot() {
    Bunny::TextureOverrideMap candidates;
    Bunny::TextureOverrideConfig candidate;
    candidate.hash = 33;
    candidate.indexBufferResource = L"ib";
    candidate.vertexBufferResources[3] = L"vb";
    candidates[33].push_back(candidate);
    Build(candidates);

    DX12ActiveIaState ia;
    ia.hasIndexBuffer = true;
    ia.indexBuffer = {0x400000, 4096, DXGI_FORMAT_R16_UINT};
    ia.hasVertexBuffer[3] = true;
    ia.vertexBuffers[3] = {0x500000, 8192, 32};
    DX12IaHashTargetPlan plan;
    plan.ResetKnown();
    plan.RequireIndexBuffer();
    plan.RequireVertexBuffer(3);

    DX12IaStateNegativeHashSnapshot first;
    DX12ModBuildIaStateNegativeHashSnapshot(ia, plan, true, &first);
    Check(first.indexHashValid && first.vertexHashMask == (1u << 3),
        "negative hash snapshot follows the prefilter target mask");

    InvalidateRange(0x7F000000, 0x7F001000);
    DX12IaStateNegativeHashSnapshot unrelatedChurn;
    DX12ModBuildIaStateNegativeHashSnapshot(ia, plan, true, &unrelatedChurn);
    Check(first.Matches(unrelatedChurn),
        "unrelated resolve churn preserves stable negative hash snapshot");

    InvalidateRange(0x400000, 0x401000);
    DX12IaStateNegativeHashSnapshot coveredRange;
    DX12ModBuildIaStateNegativeHashSnapshot(ia, plan, true, &coveredRange);
    Check(coveredRange.indexHashValid && coveredRange.vertexHashMask == (1u << 3),
        "covered range refreshes the snapshot without dropping required views");
    std::printf("PASS production negative hash snapshot: target gating, unrelated churn, covered-range refresh\n");
}
static void TestExactCache() {
    DX12ActiveIaState ia;
    DX12IaHashTargetPlan plan;
    plan.ResetKnown(); plan.RequireVertexBuffer(0); plan.RequireVertexBuffer(1); plan.RequireIndexBuffer();
    ia.hasVertexBuffer[0] = ia.hasVertexBuffer[1] = ia.hasIndexBuffer = true;
    ia.vertexBuffers[0] = {0x1000, 512, 16}; ia.vertexBuffers[1] = {0x2000, 256, 32};
    ia.indexBuffer = {0x3000, 128, DXGI_FORMAT_R16_UINT};
    DX12IaStateNegativeHashSnapshot hashes;
    hashes.indexHashValid = true;
    hashes.indexHash = 0x1111u;
    hashes.vertexHashMask = (1u << 0) | (1u << 1);
    hashes.vertexHashes[0] = 0x2222u;
    hashes.vertexHashes[1] = 0x3333u;
    DX12IaStateNegativeCache cache;
    auto inputs = DX12IaStateNegativeInputs(ia, plan, true, 10, 20, &hashes);
    auto signature = MakeIaStateNegativeSignature(inputs);
    DX12IaStateNegativeCache::Store(&cache, signature, inputs, true);
    Check(DX12IaStateNegativeCache::Find(cache, signature, inputs) != nullptr, "exact cache hit");
    auto *key = (*cache.entries)[DX12IaStateNegativeCache::SetFor(signature) * cache.kWayCount].exactKey.get();
    const auto *storage = key->vertexViews.data();
    std::swap(ia.vertexBuffers[0], ia.vertexBuffers[1]);
    Check(!(MakeIaStateNegativeSignature(inputs) == signature), "slot swaps change ordered signature");
    Check(!DX12IaStateNegativeCache::Find(cache, signature, inputs), "forced digest collision rejects swapped slots");
    std::swap(ia.vertexBuffers[0], ia.vertexBuffers[1]);
    std::swap(ia.vertexBuffers[0].SizeInBytes, ia.vertexBuffers[0].StrideInBytes);
    Check(!DX12IaStateNegativeCache::Find(cache, signature, inputs), "size/stride swap collision rejected");
    std::swap(ia.vertexBuffers[0].SizeInBytes, ia.vertexBuffers[0].StrideInBytes);
    for (int dependency = 0; dependency < 7; ++dependency) {
        auto changed = DX12IaStateNegativeInputs(ia, plan, true, 10, 20, &hashes);
        if (dependency == 0) ++changed.reloadGeneration;
        if (dependency == 1) ++changed.preSkinSectionGeneration;
        if (dependency == 2) ++changed.hashes.indexHash;
        if (dependency == 3) changed.requiredVertexMask ^= 4;
        if (dependency == 4) changed.indexChecked = false;
        if (dependency == 5) changed.indexed = false;
        if (dependency == 6) { changed.reloadGeneration = 20; changed.preSkinSectionGeneration = 10; }
        Check(!DX12IaStateNegativeCache::Find(cache, signature, changed), "each exact dependency invalidates forced collision");
    }
    ia.hasVertexBuffer[1] = false;
    Check(!DX12IaStateNegativeCache::Find(cache, signature, inputs), "unbinding invalidates");
    ia.hasVertexBuffer[1] = true;
    ia.indexBuffer.Format = DXGI_FORMAT_R32_UINT;
    Check(!DX12IaStateNegativeCache::Find(cache, signature, inputs), "index format invalidates");
    ia.indexBuffer.Format = DXGI_FORMAT_R16_UINT;
    ia.hasVertexBuffer[31] = true; ia.vertexBuffers[31] = {1234, 200, 64};
    Check(DX12IaStateNegativeCache::Find(cache, signature, inputs) != nullptr, "unrequired slots are irrelevant");
    for (int n = 0; n < 1000; ++n) DX12IaStateNegativeCache::Store(&cache, signature, inputs, true);
    Check(key->vertexViews.data() == storage, "steady-state capture reuses exact-key storage");
    DX12IaStateNegativeCache copy(cache);
    Check(!DX12IaStateNegativeCache::Find(copy, signature, inputs), "copy drops derived cache");
    cache = cache;
    Check(DX12IaStateNegativeCache::Find(cache, signature, inputs) != nullptr, "self assignment preserves cache");
    ResetTestState resetState;
    resetState.resolvedDraw.iaStateNegative = std::move(cache);
    const auto *table = resetState.resolvedDraw.iaStateNegative.entries.get();
    for (int frame = 0; frame < 1000; ++frame) {
        ResetDrawCacheOnly(&resetState);
        Check(resetState.resolvedDraw.iaStateNegative.entries.get() == table &&
            DX12IaStateNegativeCache::Find(resetState.resolvedDraw.iaStateNegative, signature, inputs),
            "real draw-template type and in-place invalidation preserve exact cache across frames");
    }
    std::printf("PASS real draw-template in-place invalidation: 1000 resets retain table and verdict\n");
    static_assert(sizeof(DX12IaStateNegativeCache) == sizeof(void *), "negative cache must stay off the hook stack");
    std::printf("PASS exact negative key: swaps, forced collisions, generations, plan, binding, copy, allocation reuse\n");
    std::printf("MEMORY cache object=%zu B, lazy table=%zu B, each exact key=%zu B + bound-view capacity\n",
        sizeof(cache), sizeof(DX12IaStateNegativeCache::Entries), sizeof(DX12IaStateNegativeExactKey));
}
static void TestSparseNegativeKey() {
    std::mt19937 random(0x9c31);
    for (UINT trial = 0; trial < 256; ++trial) {
        DX12ActiveIaState ia;
        DX12IaHashTargetPlan plan;
        plan.ResetKnown();
        plan.requireAllIaBuffers = (trial & 1) != 0;
        plan.requireVertexBufferMask = static_cast<UINT>(random());
        DX12IaStateNegativeHashSnapshot hashes;
        for (UINT slot = 0; slot < 32; ++slot) {
            ia.hasVertexBuffer[slot] = (random() & 3) == 0;
            ia.vertexBuffers[slot] = {0x1000ull * (slot + 1), 512u + slot, 16u + slot};
            if (ia.hasVertexBuffer[slot]) {
                hashes.vertexHashMask |= 1u << slot;
                hashes.vertexHashes[slot] = random();
            }
        }
        const auto inputs = DX12IaStateNegativeInputs(ia, plan, true, 1, 2, &hashes);
        const auto signature = MakeIaStateNegativeSignature(inputs);
        DX12IaStateNegativeExactKey key;
        key.Capture(inputs);
        Check(key.Matches(inputs), "sparse exact key roundtrip");
        for (UINT slot = 0; slot < 32; ++slot) {
            const bool required = (inputs.requiredVertexMask & (1u << slot)) != 0;
            ia.hasVertexBuffer[slot] = !ia.hasVertexBuffer[slot];
            Check(key.Matches(inputs) == !required, "all slots including bit31: bound mask exact validation");
            if (required)
                Check(!(MakeIaStateNegativeSignature(inputs) == signature), "binding mask changes signature");
            ia.hasVertexBuffer[slot] = !ia.hasVertexBuffer[slot];
            ++ia.vertexBuffers[slot].BufferLocation;
            Check(key.Matches(inputs) == !(required && ia.hasVertexBuffer[slot]),
                "sparse exact validation keeps bound view identity");
            --ia.vertexBuffers[slot].BufferLocation;
            auto changed = inputs;
            changed.hashes.vertexHashes[slot] ^= 1;
            Check(key.Matches(changed) == ((hashes.vertexHashMask & (1u << slot)) == 0),
                "hash snapshot compares every valid bit even under a forced digest collision");
        }
    }
    std::puts("PASS sparse negative key: 256 plans x 32 slots, binding/view/hash changes and bit31");
}
static void TestViewHashMemo() {
    DX12ResourcePublishedState token;
    token.generation = 7;
    uint32_t hash = 0;
    IaViewHashMemo::Publish(0x1000, 64, 16, 0, 1, 0xabcdu, 5, 7,
        reinterpret_cast<ID3D12Resource*>(0x1000), &token);
    Check(IaViewHashMemo::TryRead(0x1000, 64, 16, 0, 1, 5, &hash) && hash == 0xabcdu,
        "memo returns the published hash while its identity still validates");
    Check(!IaViewHashMemo::TryRead(0x1001, 64, 16, 0, 1, 5, &hash), "gpuVa separates memo keys");
    Check(!IaViewHashMemo::TryRead(0x1000, 65, 16, 0, 1, 5, &hash), "size separates memo keys");
    Check(!IaViewHashMemo::TryRead(0x1000, 64, 17, 0, 1, 5, &hash), "stride separates memo keys");
    Check(!IaViewHashMemo::TryRead(0x1000, 64, 16, 1, 1, 5, &hash), "format separates memo keys");
    Check(!IaViewHashMemo::TryRead(0x1000, 64, 16, 0, 2, 5, &hash), "slot separates memo keys");
    // The published record is immutable, so a moved identity generation is the
    // only way a resolved view's hash inputs can change - and it must miss even
    // though the key and the global resolve epoch are untouched. Without this,
    // a resource recreated on the same GPU virtual address with a different
    // descriptor would keep returning the hash of its predecessor.
    token.generation = 8;
    Check(!IaViewHashMemo::TryRead(0x1000, 64, 16, 0, 1, 5, &hash),
        "recreated resource on the same VA invalidates the memo");
    Check(!IaViewHashMemo::TryRead(0x1000, 64, 16, 0, 1, 6, &hash),
        "an invalid identity is not rescued by a moved epoch");
    // An unresolved view has no identity to compare, so only an unchanged epoch
    // may reuse it; the range-scoped revalidation stays in the L1 behind it.
    IaViewHashMemo::Publish(0x2000, 64, 16, 0, 2, 0x1234u, 9, 0, nullptr, nullptr);
    Check(IaViewHashMemo::TryRead(0x2000, 64, 16, 0, 2, 9, &hash) && hash == 0x1234u,
        "unresolved view is reusable while its epoch holds");
    Check(!IaViewHashMemo::TryRead(0x2000, 64, 16, 0, 2, 10, &hash),
        "moved epoch rejects an unresolved view instead of trusting it");
    // Republishing a key must replace its answer, never leave the older one.
    IaViewHashMemo::Publish(0x3000, 128, 32, 0, 3, 0x5555u, 9, 0, nullptr, nullptr);
    IaViewHashMemo::Publish(0x3000, 128, 32, 0, 3, 0x6666u, 9, 0, nullptr, nullptr);
    Check(IaViewHashMemo::TryRead(0x3000, 128, 32, 0, 3, 9, &hash) && hash == 0x6666u,
        "republishing a key replaces its answer");
    // Distinct keys must both stay readable: the way search only reuses a way
    // holding the same key or an empty one, so writing a second key may not
    // unanswer the first.
    IaViewHashMemo::Publish(0x4000, 64, 16, 0, 4, 0x7777u, 9, 0, nullptr, nullptr);
    IaViewHashMemo::Publish(0x4000, 64, 16, 0, 5, 0x8888u, 9, 0, nullptr, nullptr);
    Check(IaViewHashMemo::TryRead(0x4000, 64, 16, 0, 4, 9, &hash) && hash == 0x7777u,
        "publishing a second key leaves the first readable");
    Check(IaViewHashMemo::TryRead(0x4000, 64, 16, 0, 5, 9, &hash) && hash == 0x8888u,
        "the second key is readable too");
    std::printf("PASS flat view-hash memo: key fields, identity token, unresolved epoch, republish, ways\n");
    std::printf("MEMORY view hash memo: %zu sets x %zu ways, %zu B/entry, %zu KiB per resolving thread\n",
        IaViewHashMemo::kSetCount, IaViewHashMemo::kWayCount,
        sizeof(IaViewHashMemo::Entry),
        (IaViewHashMemo::kSetCount * IaViewHashMemo::kWayCount * sizeof(IaViewHashMemo::Entry)) / 1024);
}

// The case the memo exists for on this title: an unresolved view whose hash is
// asked for again after the global resolve epoch moved for an unrelated reason.
// The range-scoped test must keep it reusable, and an invalidation that really
// covers it must still reject it.
static void TestViewHashMemoRangeRevalidation() {
    const UINT64 va = 0xA00000;
    const UINT64 epoch = gBufferResolveInvalidationEpochAtomic.load();
    IaViewHashMemo::Publish(va, 512, 32, 0, 1, 0xbeefu, epoch, 0, nullptr, nullptr);
    uint32_t hash = 0;
    Check(IaViewHashMemo::TryRead(va, 512, 32, 0, 1, epoch, &hash) && hash == 0xbeefu,
        "unresolved view is reusable while its own epoch holds");
    InvalidateRange(0xB00000, 0xB10000); // unrelated buffer churn moves the epoch
    Check(IaViewHashMemo::TryRead(va, 512, 32, 0, 1,
            gBufferResolveInvalidationEpochAtomic.load(), &hash) && hash == 0xbeefu,
        "unrelated invalidation must not send an unresolved view back to the container path");
    InvalidateRange(va + 256, va + 4096); // a range that overlaps this view's
    Check(!IaViewHashMemo::TryRead(va, 512, 32, 0, 1,
            gBufferResolveInvalidationEpochAtomic.load(), &hash),
        "an invalidation overlapping the view's range rejects it");
    // The same key, republished against the current epoch: an exact-range
    // invalidation afterwards must reject it just the same.
    IaViewHashMemo::Publish(va, 512, 32, 0, 1, 0xbeefu,
        gBufferResolveInvalidationEpochAtomic.load(), 0, nullptr, nullptr);
    InvalidateRange(va, va + 512);
    Check(!IaViewHashMemo::TryRead(va, 512, 32, 0, 1,
            gBufferResolveInvalidationEpochAtomic.load(), &hash),
        "an invalidation of exactly the view's range rejects it");
    std::printf("PASS unresolved view revalidation: unrelated churn reuses, covering invalidation rejects\n");
}
static void TestHashCaches() {
    ResourceFixture &r = resources[0x100000]; r.va = 0x100000; r.width = 4096;
    DX12ActiveIaState ia;
    ia.hasVertexBuffer[0] = true; ia.vertexBuffers[0] = {r.va, 512, 16};
    uint32_t h = DX12CommandListRuntime::GetOrBuildCachedVertexViewHash(&ia, 0);
    const size_t resolves = resolveCalls;
    for (int n = 0; n < 10000; ++n) {
        ia.vertexBufferHashValid[0] = false; // rotating binds rebuild slot, not TLS
        Check(DX12CommandListRuntime::GetOrBuildCachedVertexViewHash(&ia, 0) == h, "warm L1 hash");
    }
    Check(resolveCalls == resolves && wrapperCalls == 0 && wrapperStores == 0, "slot rebuilds avoid resource/wrapper lookups");
    ++gBufferContentGeneration;
    ia.vertexBufferHashValid[0] = false;
    Check(DX12CommandListRuntime::GetOrBuildCachedVertexViewHash(&ia, 0) == h, "content-only write preserves metadata-only hash");
    Check(resolveCalls == resolves, "unrelated content writes do not defeat L1");
    r.width = 8192; ++r.token.generation; ++gBufferResolveInvalidationEpochAtomic;
    uint32_t recreated = DX12CommandListRuntime::GetOrBuildCachedVertexViewHash(&ia, 0);
    Check(recreated != h, "same VA/resource pointer recreated with different descriptor invalidates");
    ia.vertexBuffers[0] = {0x900000, 256, 16}; ia.vertexBufferHashValid[0] = false;
    uint32_t fallback = DX12CommandListRuntime::GetOrBuildCachedVertexViewHash(&ia, 0);
    ResourceFixture &later = resources[0x900000]; later.va = 0x900000; later.width = 4096;
    ++gBufferResolveInvalidationEpochAtomic;
    Check(DX12CommandListRuntime::GetOrBuildCachedVertexViewHash(&ia, 0) != fallback, "hash miss then resolve publication invalidates fallback");
    std::printf("PASS production hash/slot bodies: TLS reuse, metadata content writes, VA reuse, miss->resolve\n");
}
static volatile size_t benchmarkSink = 0;
static void TestMatching() {
    Bunny::TextureOverrideMap configs;
    std::mt19937 rng(0x12345678);
    for (UINT i = 0; i < 2048; ++i) {
        Bunny::TextureOverrideConfig c;
        c.hash = rng() % 129; c.section = L"section" + std::to_wstring(i);
        c.handlingSkip = (rng() % 5 == 0); c.hasCsHash = (rng() % 3 == 0);
        c.hasMatchFirstIndex = (rng() % 4 == 0); // deliberately ignored by negative probe
        if (rng() % 3 == 0) c.indexBufferResource = L"ib";
        if (rng() % 3 == 0) c.vertexBufferResources[rng() % 32] = L"vb";
        if (rng() % 3 == 0) c.actions.push_back({Bunny::CommandListActionKind::DrawIndexed});
        if (rng() % 5 == 0) c.actions.push_back({Bunny::CommandListActionKind::Draw});
        configs[c.hash].push_back(c);
    }
    Build(configs);
    size_t comparisons = 0;
    for (int active = 0; active < 3; ++active) {
        activeSections.clear();
        if (active) for (const auto &[hash, bucket] : configs) for (const auto &c : bucket)
            if (active == 2 || rng() % 2) activeSections.insert(c.section);
        for (UINT slot = 0; slot < 32; ++slot) for (uint32_t hash = 0; hash < 140; ++hash) {
            Check(IaHashDefinitelyHasNoCandidate(false, slot, hash) == ReferenceNoCandidate(false, slot, hash), "VB hash-index equivalence");
            Check(IaHashDefinitelyHasNoCandidate(true, slot, hash) == ReferenceNoCandidate(true, slot, hash), "IB hash-index equivalence");
            comparisons += 2;
        }
    }
    gIaPrefilterCountSensitive = 1;
    Check(!IaHashDefinitelyHasNoCandidate(true, 0, 0xffffffff), "count-sensitive use fails open");
    gIaPrefilterCountSensitive = 0;
    constexpr int iterations = 200000;
    auto time = [&](bool reference) {
        const auto begin = std::chrono::steady_clock::now(); size_t result = 0;
        for (int i = 0; i < iterations; ++i) result += reference ?
            ReferenceNoCandidate(true, 0, 0xffffffff) : IaHashDefinitelyHasNoCandidate(true, 0, 0xffffffff);
        benchmarkSink = result;
        return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - begin).count();
    };
    const double oldUs = time(true), newUs = time(false);
    std::printf("PASS hash-index differential: %zu cases (wildcard/specific/skip/hash-zero/match_cs)\n", comparisons);
    std::printf("BENCH absent-hash probe %d runs: generic scan %.1f us, hash index %.1f us, %.1fx (synthetic CPU only)\n", iterations, oldUs, newUs, oldUs / newUs);
}
// Median-of-three timing of one lambda, in nanoseconds per call. The first
// round warms the caches; the median is reported so a single noisy round
// cannot be presented as the result.
template <typename Fn>
static double TimeNsPerCall(const char *label, int iterations, Fn &&body) {
    double samples[3] = {};
    for (int round = 0; round < 3; ++round) {
        const auto begin = std::chrono::steady_clock::now();
        for (int i = 0; i < iterations; ++i) body(i);
        const auto end = std::chrono::steady_clock::now();
        samples[round] = std::chrono::duration<double, std::nano>(end - begin).count() / iterations;
    }
    std::sort(samples, samples + 3);
    std::printf("BENCH %-46s %7.1f ns/call (3 rounds: %.1f/%.1f/%.1f)\n",
        label, samples[1], samples[0], samples[1], samples[2]);
    return samples[1];
}

// Costs of the three per-draw operations the profile attributes the draw hook
// to: the count-independent prefilter gate, the IA view-hash resolve on a view
// change (the case a game performs on most draws), and the IA-state negative
// cache lookup itself.
static void TestDrawPathBench() {
    Bunny::TextureOverrideMap configs;
    for (uint32_t hash : {0x7cf75550u, 0x47c09778u, 0x52ea588eu}) {
        Bunny::TextureOverrideConfig c;
        c.hash = hash; c.section = L"section" + std::to_wstring(hash);
        if (hash == 0x7cf75550u) c.indexBufferResource = L"ib";
        configs[hash].push_back(c);
    }
    Build(configs);

    constexpr int kGateIterations = 2000000;
    TimeNsPerCall("gate IaHasAnyPrefilterCandidate(ib)", kGateIterations,
        [](int i) { benchmarkSink += IaHasAnyPrefilterCandidate((i & 1) != 0, 0); });
    TimeNsPerCall("gate IaHasAnyPrefilterCandidate(vb1)", kGateIterations,
        [](int i) { benchmarkSink += IaHasAnyPrefilterCandidate(false, 1 + (i & 3)); });

    // One resolved buffer holding 512 distinct views, the shape a game presents
    // when it binds a different vertex buffer for most draws.
    ResourceFixture &r = resources[0x400000];
    r.va = 0x400000; r.width = 1 << 20;
    constexpr int kViews = 512;
    DX12ActiveIaState ia;
    ia.hasVertexBuffer[1] = true;
    constexpr int kHashIterations = 400000;
    TimeNsPerCall("view hash resolve, slot memo invalidated", kHashIterations,
        [&](int i) {
            ia.vertexBuffers[1] = {r.va + static_cast<UINT64>(i % kViews) * 512, 512, 32};
            ia.vertexBufferHashValid[1] = false; // the game rebound this slot
            benchmarkSink += DX12CommandListRuntime::GetOrBuildCachedVertexViewHash(&ia, 1);
        });
    TimeNsPerCall("view hash resolve, slot memo hit", kHashIterations,
        [&](int) {
            benchmarkSink += DX12CommandListRuntime::GetOrBuildCachedVertexViewHash(&ia, 1);
        });
    // Same call with a working set four times the memo's capacity, so part of
    // the rotation cannot be answered from it: this is the container path the
    // memo sits in front of, at a size the containers still hold comfortably.
    ResourceFixture &big = resources[0x8000000];
    big.va = 0x8000000; big.width = 1 << 24;
    constexpr int kBigViews = 8192;
    TimeNsPerCall("view hash resolve, 8K-view working set", kHashIterations,
        [&](int i) {
            ia.vertexBuffers[1] = {big.va + static_cast<UINT64>(i % kBigViews) * 512, 512, 32};
            ia.vertexBufferHashValid[1] = false;
            benchmarkSink += DX12CommandListRuntime::GetOrBuildCachedVertexViewHash(&ia, 1);
        });

    // The unresolved case with the global epoch moving underneath it, which is
    // what the profiled title does (5..17 unrelated buffer-range invalidations
    // per frame) and the shape that kept the container path on the draw hook.
    ia.vertexBuffers[1] = {0x600000, 512, 32};
    ia.vertexBufferHashValid[1] = false;
    benchmarkSink += DX12CommandListRuntime::GetOrBuildCachedVertexViewHash(&ia, 1);
    TimeNsPerCall("view hash resolve, unresolved view, epoch churn", kHashIterations,
        [&](int i) {
            if ((i % 64) == 0)
                InvalidateRange(0x7F000000, (0x7F000000 + 4096) + static_cast<UINT64>(i % 8) * 4096);
            ia.vertexBufferHashValid[1] = false;
            benchmarkSink += DX12CommandListRuntime::GetOrBuildCachedVertexViewHash(&ia, 1);
        });
    // Worst case for the revalidation: the epoch moves on every single request,
    // so each one pays the shared-lock range query. The title moves it 5..17
    // times per ~1665 requests, so this is a bound, not the expected cost.
    TimeNsPerCall("view hash resolve, epoch moves every request", kHashIterations,
        [&](int i) {
            InvalidateRange(0x7F000000, (0x7F000000 + 4096) + static_cast<UINT64>(i % 8) * 4096);
            ia.vertexBufferHashValid[1] = false;
            benchmarkSink += DX12CommandListRuntime::GetOrBuildCachedVertexViewHash(&ia, 1);
        });

    DX12IaHashTargetPlan plan;
    plan.ResetKnown();
    plan.RequireVertexBuffer(1);
    plan.RequireIndexBuffer();
    DX12ActiveIaState cacheIa;
    cacheIa.hasIndexBuffer = true;
    cacheIa.hasVertexBuffer[1] = true;
    cacheIa.indexBuffer = {0x500000, 4096, DXGI_FORMAT_R16_UINT};
    cacheIa.vertexBuffers[1] = {0x400000, 8192, 32};
    DX12IaStateNegativeCache cache;
    DX12IaStateNegativeHashSnapshot hashes;
    hashes.indexHashValid = true;
    hashes.indexHash = 0x1010u;
    hashes.vertexHashMask = 1u << 1;
    hashes.vertexHashes[1] = 0x2020u;
    auto inputs = DX12IaStateNegativeInputs(cacheIa, plan, true, 1, 1, &hashes);
    const auto signature = MakeIaStateNegativeSignature(inputs);
    DX12IaStateNegativeCache::Store(&cache, signature, inputs, true);
    constexpr int kCacheIterations = 400000;
    TimeNsPerCall("neg cache signature+Find (hit)", kCacheIterations,
        [&](int) {
            benchmarkSink += DX12IaStateNegativeCache::Find(
                cache, MakeIaStateNegativeSignature(inputs), inputs) != nullptr;
        });
    {
        // A miss must run the verdict and store; time the lookup half only, by
        // querying a signature the table never held.
        auto other = DX12IaStateNegativeInputs(cacheIa, plan, true, 2, 2, &hashes);
        TimeNsPerCall("neg cache signature+Find (miss)", kCacheIterations,
            [&](int) {
                benchmarkSink += DX12IaStateNegativeCache::Find(
                    cache, MakeIaStateNegativeSignature(other), other) != nullptr;
            });
    }
    plan.requireAllIaBuffers = true;
    auto allInputs = DX12IaStateNegativeInputs(cacheIa, plan, true, 1, 1, &hashes);
    DX12IaStateNegativeCache::Store(&cache, MakeIaStateNegativeSignature(allInputs), allInputs, true);
    TimeNsPerCall("neg cache all-IA sparse signature+Find", kCacheIterations,
        [&](int) {
            benchmarkSink += DX12IaStateNegativeCache::Find(
                cache, MakeIaStateNegativeSignature(allInputs), allInputs) != nullptr;
        });
}

int main() {
    TestPublication(); TestNegativeHashSnapshot(); TestExactCache(); TestViewHashMemo(); TestViewHashMemoRangeRevalidation();
    TestSparseNegativeKey(); TestHashCaches(); TestMatching();
    TestDrawPathBench();
    std::printf("ALL PASS; no project build or GPU performance claim\n");
}
