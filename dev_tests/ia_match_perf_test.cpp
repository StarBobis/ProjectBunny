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
class DX12BufferResolveCacheUtils {
public:
    static bool RangeInvalidatedSinceLocked(UINT64, UINT64, UINT64 since) {
        return since != gBufferResolveInvalidationEpochAtomic.load();
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
static void TestExactCache() {
    DX12ActiveIaState ia;
    DX12IaHashTargetPlan plan;
    plan.ResetKnown(); plan.RequireVertexBuffer(0); plan.RequireVertexBuffer(1); plan.RequireIndexBuffer();
    ia.hasVertexBuffer[0] = ia.hasVertexBuffer[1] = ia.hasIndexBuffer = true;
    ia.vertexBuffers[0] = {0x1000, 512, 16}; ia.vertexBuffers[1] = {0x2000, 256, 32};
    ia.indexBuffer = {0x3000, 128, DXGI_FORMAT_R16_UINT};
    DX12IaStateNegativeCache cache;
    auto inputs = DX12IaStateNegativeInputs(ia, plan, true, 10, 20, 30);
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
        auto changed = DX12IaStateNegativeInputs(ia, plan, true, 10, 20, 30);
        if (dependency == 0) ++changed.reloadGeneration;
        if (dependency == 1) ++changed.preSkinGeneration;
        if (dependency == 2) ++changed.resolveGeneration;
        if (dependency == 3) changed.requiredVertexMask ^= 4;
        if (dependency == 4) changed.indexChecked = false;
        if (dependency == 5) changed.indexed = false;
        if (dependency == 6) { changed.reloadGeneration = 20; changed.preSkinGeneration = 10; }
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
            "real draw-template type and Reset moves preserve exact cache across frames");
    }
    std::printf("PASS real draw-template Reset moves: 1000 round trips retain table and verdict\n");
    static_assert(sizeof(DX12IaStateNegativeCache) == sizeof(void *), "negative cache must stay off the hook stack");
    std::printf("PASS exact negative key: swaps, forced collisions, generations, plan, binding, copy, allocation reuse\n");
    std::printf("MEMORY cache object=%zu B, lazy table=%zu B, each exact key=%zu B + bound-view capacity\n",
        sizeof(cache), sizeof(DX12IaStateNegativeCache::Entries), sizeof(DX12IaStateNegativeExactKey));
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
int main() {
    TestPublication(); TestExactCache(); TestHashCaches(); TestMatching();
    std::printf("ALL PASS; no project build or GPU performance claim\n");
}
