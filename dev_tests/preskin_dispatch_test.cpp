// Tests extracted production function bodies; D3D resources/descriptor lookup,
// instrumentation and the collector's cold tail are adapters, not a GPU test.
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
using UINT = unsigned int;
using UINT64 = uint64_t;
using LONG64 = int64_t;
constexpr UINT MaxRootParameters = 64;
constexpr UINT D3D12_SRV_DIMENSION_BUFFER = 1;
struct ID3D12GraphicsCommandList {};
struct ID3D12PipelineState {};
struct ID3D12RootSignature {};
struct ID3D12DescriptorHeap {};
struct Descriptor {
    std::string kind = "SRV";
    UINT viewDimension = D3D12_SRV_DIMENSION_BUFFER;
    UINT64 resourceOffset = 0, bufferViewOffset = 0, viewSize = 128, bufferViewBytes = 128;
    UINT structureByteStride = 4;
    uint32_t hash = 1;
};
struct DX12CurrentComputeUavBinding {
    bool hasDescriptor = true;
    UINT shaderRegister = 0;
    Descriptor descriptor;
};
struct DX12CurrentRootConstants { UINT values[64] = {}; };
struct DX12PreSkinDispatchOverride {};
struct DX12PreSkinSrvHashResult {
    uint32_t descriptorHash = 0;
    UINT64 sourceOffset = 0, viewBytes = 0;
    UINT stride = 0;
};
struct DX12PreSkinSrvReplacementBinding {
    UINT replaceSlot = 0, shaderRegister = 0;
    std::wstring resource;
};
namespace Bunny {
struct TextureOverrideConfig {
    uint64_t csHash = 7;
    bool eligible = true;
    std::map<UINT, uint32_t> preSkinMatchCsSrvHashes;
    std::map<UINT, std::wstring> preSkinCsSrvResources;
};
}
struct DX12PreSkinTextureOverrideCandidate {
    const Bunny::TextureOverrideConfig *config = nullptr;
    uint32_t hash = 0;
};
struct DX12UavHashTextureOverrideMatch {
    const Bunny::TextureOverrideConfig *config = nullptr;
    uint32_t hash = 0;
    std::vector<DX12PreSkinSrvReplacementBinding> srvReplacements;
};
static std::unordered_map<uint64_t, std::vector<size_t>> gPreSkinMatchCsTextureOverrideIndex;
static std::vector<DX12PreSkinTextureOverrideCandidate> gPreSkinTextureOverrides;
static unsigned hashCalls;
static uint32_t HashComputeBufferBinding(const DX12CurrentComputeUavBinding &b) { ++hashCalls; return b.descriptor.hash; }
static UINT64 DescriptorViewSize(const Descriptor &d) { return d.viewSize; }
static uint64_t HashCombine64(uint64_t a, uint64_t b) { return a ^ (b + 0x9e3779b97f4a7c15ull + (a << 6) + (a >> 2)); }
static bool IsComputePreSkinTextureOverride(const Bunny::TextureOverrideConfig &c) { return c.eligible; }
static uint64_t GetPreSkinCsHash(const Bunny::TextureOverrideConfig &c) { return c.csHash; }
#include "preskin_match_under_test.h"

struct RootTableState { bool valid = false; UINT64 address = 0; };
struct RootDescriptorState { bool valid = false; UINT64 address = 0; };
struct RootConstantsState { bool valid = false; UINT maxSet = 0; UINT values[64] = {}; bool valueValid[64] = {}; };
struct DX12DescriptorHeapSummary { UINT64 generation = 0, size = 0; };
#include "preskin_snapshot_type_under_test.h"
struct DX12SimulateContextComputeTemplate {
    bool probeResultValid = true, probeApplied = false;
    UINT64 probeCsHash = 7, probeReloadGeneration = 9, probeIaContentEpoch = 11;
    UINT64 probeBufferContentGeneration = 13, probeBindingDependencyFingerprint = 17;
    bool bindingsValid = true, bindingsUseRootValues = false;
    ID3D12DescriptorHeap *bindingsHeap = nullptr;
    ID3D12RootSignature *bindingsRootSignature = nullptr;
    UINT64 bindingsDependencyFingerprint = 17;
    std::vector<int> bindingsObservedSlots;
    std::vector<DX12CurrentComputeUavBinding> uavs, srvs, cbvs;
    std::vector<DX12CurrentRootConstants> rootConstants;
};
struct DX12SimulateContext {
    DX12SimulateContextComputeTemplate resolvedCompute;
    UINT64 iaContentEpoch = 11;
    bool computeRootUsedMaskValid = true;
    ID3D12PipelineState *pipelineState = nullptr;
    ID3D12RootSignature *computeRootSignature = nullptr;
    ID3D12DescriptorHeap *cbvSrvUavHeap = nullptr;
    bool runtimeCbvSrvUavHeapResolved = false;
    DX12DescriptorHeapSummary runtimeCbvSrvUavHeap;
    UINT64 computeBindingSerial = 1, bindingQuerySerial = 1;
    UINT computeRootTableValidCount = 0;
    RootTableState computeRootTables[MaxRootParameters];
    RootDescriptorState computeRootDescriptors[MaxRootParameters];
    RootConstantsState computeRootConstants[MaxRootParameters];
};
struct CommandListBindingState : DX12PreSkinComputeSnapshot {};
#include "preskin_snapshot_under_test.h"
struct DX12CommandListTrackingState { bool trackBindings = false; };
static DX12SimulateContext *resident;
struct DX12CommandListRuntime {
    static DX12SimulateContext *GetStatePtrFast(ID3D12GraphicsCommandList*) { return resident; }
    static DX12CommandListTrackingState GetTrackingState(DX12SimulateContext*) { return {}; }
};
static unsigned lookups, classifications, retries, freshCalls, generationReads;
static bool identityValid = true, rootsValid = true, retryResult = true;
static UINT64 fingerprint = 17, contentGeneration = 13;
struct DX12BindingTracker {
    static bool ValidateRuntimePreSkinBindingState(const DX12SimulateContext*) { ++classifications; return rootsValid; }
    static bool GetSimulateContextPreSkinBindings(ID3D12GraphicsCommandList*,
        const std::vector<DX12CurrentComputeUavBinding> **uavs,
        const std::vector<DX12CurrentComputeUavBinding> **srvs,
        const std::vector<DX12CurrentComputeUavBinding> **cbvs,
        const std::vector<DX12CurrentRootConstants> **constants,
        UINT64 *dependency, bool *found) {
        ++lookups;
        if (!identityValid) { *uavs = *srvs = *cbvs = nullptr; *constants = nullptr; *dependency = 0; *found = false; return false; }
        *uavs = &resident->resolvedCompute.uavs;
        *srvs = &resident->resolvedCompute.srvs;
        *cbvs = &resident->resolvedCompute.cbvs;
        *constants = &resident->resolvedCompute.rootConstants;
        *dependency = fingerprint; *found = true; return true;
    }
    static UINT64 GetComputeBindingSerial(ID3D12GraphicsCommandList*) { return 1; }
};
static bool TryLoadRuntimePreSkinBindings(DX12SimulateContext*, UINT64,
    std::vector<DX12CurrentComputeUavBinding> *uavs,
    std::vector<DX12CurrentComputeUavBinding>*, std::vector<DX12CurrentComputeUavBinding>*,
    std::vector<DX12CurrentRootConstants>*, UINT64 *dependency, bool *found) {
    ++retries;
    if (!retryResult) return false;
    if (uavs) uavs->push_back({});
    if (dependency) *dependency = 41;
    *found = true; return true;
}
static bool FreshCollect(std::vector<DX12CurrentComputeUavBinding> *uavs, UINT64 *dependency) {
    ++freshCalls;
    if (uavs) { assert(uavs->empty()); uavs->push_back({}); }
    if (dependency) *dependency = 43;
    return true;
}
struct DX12DiagProf { static bool Enabled() { return false; } };
#define DX12_PROF_COUNT(...) ((void)0)
#define DX12_PROF_CPU_STAGE(...) ((void)0)
#define DX12_DIAG_QPC(...) ((void)0)
#define DX12_DIAG_ELAPSED_US(...) 0
#define DX12_LOG(...) ((void)0)
static UINT64 DX12GetIaHashContentGeneration() { ++generationReads; return contentGeneration; }
#include "preskin_probe_type_under_test.h"
#include "preskin_verdict_enum_under_test.h"
#include "preskin_hook_under_test.h"
#include "preskin_collect_prefix_under_test.h"

static void TestMatching() {
    Bunny::TextureOverrideConfig config;
    config.preSkinMatchCsSrvHashes = {{0, 101}, {1, 102}, {2, 103}};
    config.preSkinCsSrvResources = {{0, L"position"}, {2, L"normal"}};
    std::vector<DX12CurrentComputeUavBinding> srvs(3), empty;
    std::vector<DX12CurrentRootConstants> constants;
    for (UINT i = 0; i < 3; ++i) { srvs[i].shaderRegister = i; srvs[i].descriptor.hash = 101 + i; }
    gPreSkinTextureOverrides = {{&config, 99}};
    gPreSkinMatchCsTextureOverrideIndex[7] = {1234, 0}; // Invalid candidate index still skipped.
    DX12UavHashTextureOverrideMatch match;
    hashCalls = 0;
    assert(FindMatchCsTextureOverrideLocked(7, empty, srvs, empty, constants, &match));
    assert(hashCalls == 3 && match.config == &config && match.hash == 99);
    assert(match.srvReplacements.size() == 2 && match.srvReplacements[1].resource == L"normal");
    // Old warm positive: every SRV was hashed for its key, then again for replacements.
    hashCalls = 0;
    (void)HashPreSkinMatchSrvKeysForProbe(srvs, config);
    assert(MatchComputeInputSrvsTextureOverrideLocked(srvs, config));
    assert(hashCalls == 6);
    for (unsigned mismatch = 0; mismatch != 3; ++mismatch) {
        srvs[mismatch].descriptor.hash = 0;
        hashCalls = 0;
        assert(!FindMatchCsTextureOverrideLocked(7, empty, srvs, empty, constants, &match));
        assert(hashCalls == mismatch + 1);
        srvs[mismatch].descriptor.hash = 101 + mismatch;
    }
    srvs[0].hasDescriptor = false;
    assert(!FindMatchCsTextureOverrideLocked(7, empty, srvs, empty, constants, &match));
    srvs[0].hasDescriptor = true;
    srvs[0].descriptor.kind = "UAV";
    assert(!FindMatchCsTextureOverrideLocked(7, empty, srvs, empty, constants, &match));
    srvs[0].descriptor.kind = "SRV";
    srvs[0].descriptor.viewDimension = 2;
    assert(!FindMatchCsTextureOverrideLocked(7, empty, srvs, empty, constants, &match));
    srvs[0].descriptor.viewDimension = D3D12_SRV_DIMENSION_BUFFER;
    assert(!FindMatchCsTextureOverrideLocked(0, empty, srvs, empty, constants, nullptr));
    assert(!FindMatchCsTextureOverrideLocked(8, empty, srvs, empty, constants, nullptr));
    assert(FindMatchCsTextureOverrideLocked(7, empty, srvs, empty, constants, nullptr));
    config.preSkinMatchCsSrvHashes.clear();
    assert(!FindMatchCsTextureOverrideLocked(7, empty, srvs, empty, constants, &match));
    // Configuration replacement/reload has no lingering bool-cache verdict.
    config.preSkinMatchCsSrvHashes = {{0, 900}};
    assert(!FindMatchCsTextureOverrideLocked(7, empty, srvs, empty, constants, &match));
    srvs[0].descriptor.hash = 900;
    config.preSkinCsSrvResources.clear();
    assert(FindMatchCsTextureOverrideLocked(7, empty, srvs, empty, constants, &match));
    assert(match.srvReplacements.empty());
    config.eligible = false;
    assert(!FindMatchCsTextureOverrideLocked(7, empty, srvs, empty, constants, &match));
    config.eligible = true; config.csHash = 9;
    assert(!FindMatchCsTextureOverrideLocked(7, empty, srvs, empty, constants, &match));
    std::cout << "matching: direct hash calls positive 3 vs previous warm 6; first mismatch 1 vs key 3\n";
}

static void TestVerdictAndIdentity() {
    ID3D12GraphicsCommandList list;
    ID3D12DescriptorHeap heap;
    ID3D12RootSignature sig;
    for (unsigned mask = 0; mask < 8192; ++mask) {
        DX12SimulateContext state;
        resident = &state;
        auto &c = state.resolvedCompute;
        UINT64 shader = (mask & 1) ? 0 : 7;
        c.probeResultValid = !(mask & 2);
        c.probeApplied = (mask & 4) != 0;
        c.probeCsHash = (mask & 8) ? 8 : 7;
        c.probeReloadGeneration = (mask & 16) ? 10 : 9;
        c.probeIaContentEpoch = (mask & 32) ? 12 : 11;
        c.probeBufferContentGeneration = (mask & 64) ? 14 : 13;
        c.bindingsValid = !(mask & 128);
        c.bindingsUseRootValues = (mask & 256) != 0;
        c.bindingsHeap = (mask & 512) ? &heap : nullptr;
        c.bindingsRootSignature = (mask & 1024) ? &sig : nullptr;
        identityValid = !(mask & 2048);
        fingerprint = (mask & 4096) ? 18 : 17;
        rootsValid = true;
        lookups = classifications = generationReads = 0;
        auto &probe = HookUnderTest::ScratchProbe();
        const auto result = HookUnderTest::EvaluatePreSkinExecVerdict(&list, &state, &probe, shader, 9);
        // Every original acceptance term remains necessary; no weaker key.
        assert((result == DX12ExecVerdictBlock::Count) == (mask == 0));
        assert(lookups <= 1 && generationReads <= 1);
        if (mask & 1) assert(result == DX12ExecVerdictBlock::NoShaderHash);
        else if (mask & 2) assert(result == DX12ExecVerdictBlock::ProbeNotValid);
        else if (mask & 4) assert(result == DX12ExecVerdictBlock::ProbeApplied);
        else if (mask & 120) assert(result == DX12ExecVerdictBlock::EpochMismatch);
        else if (mask & 1920) assert(result == DX12ExecVerdictBlock::BindingsInvalid);
        else if (mask & 2048) assert(result == DX12ExecVerdictBlock::IdentityProbe);
        else if (mask & 4096) assert(result == DX12ExecVerdictBlock::Fingerprint);
    }
    DX12SimulateContext state, other;
    resident = &state; fingerprint = 17; identityValid = true;
    lookups = classifications = 0;
    auto &probe = HookUnderTest::ScratchProbe();
    assert(HookUnderTest::ProbePreSkinBindingIdentity(&list, &state, &probe));
    assert(HookUnderTest::ProbePreSkinBindingIdentity(&list, &state, &probe));
    assert(lookups == 1 && classifications == 1); // Entire identity probe memoised.
    HookUnderTest::ScratchProbe();
    identityValid = false; rootsValid = false;
    assert(!HookUnderTest::ProbePreSkinBindingIdentity(&list, &state, &probe));
    assert(!HookUnderTest::ProbePreSkinBindingIdentity(&list, &state, &probe));
    assert(lookups == 1 && classifications == 2); // Root failure still exits before full lookup.
    HookUnderTest::ScratchProbe();
    assert(!HookUnderTest::ProbePreSkinBindingIdentity(&list, &other, &probe));
    assert(lookups == 1); // Never borrow a different state's snapshot.
    HookUnderTest::ScratchProbe();
    assert(!HookUnderTest::ProbePreSkinBindingIdentity(&list, nullptr, &probe));
    std::cout << "verdict: 8192 guard combinations and per-dispatch identity memo passed\n";
}

static void TestCollectorPrefix() {
    ID3D12GraphicsCommandList list;
    DX12SimulateContext state;
    resident = &state;
    std::vector<DX12CurrentComputeUavBinding> bindings(3);
    UINT64 dependency = 99;
    retries = freshCalls = 0; retryResult = true;
    assert(CollectPrefix(&list, &bindings, nullptr, nullptr, nullptr, &dependency, true));
    assert(retries == 1 && freshCalls == 0 && dependency == 41);
    // Already rejected: no second lookup, still enter fresh collection even if
    // a concurrent descriptor write would now let the resident cache pass.
    assert(CollectPrefix(&list, &bindings, nullptr, nullptr, nullptr, &dependency, false));
    assert(retries == 1 && freshCalls == 1 && dependency == 43);
    retryResult = false;
    assert(CollectPrefix(&list, &bindings, nullptr, nullptr, nullptr, &dependency, true));
    assert(retries == 2 && freshCalls == 2);
    resident = nullptr;
    assert(CollectPrefix(&list, &bindings, nullptr, nullptr, nullptr, &dependency, false));
    assert(freshCalls == 3);
    assert(!CollectPrefix(nullptr, &bindings, nullptr, nullptr, nullptr, &dependency, true));
    assert(dependency == 0 && retries == 2 && freshCalls == 3);
    std::cout << "collector prefix: default reuse/rejected retry/fresh fallback/null paths passed\n";
}

static void CheckSnapshot(const DX12PreSkinComputeSnapshot &a, const DX12PreSkinComputeSnapshot &b) {
    assert(a.pipelineState == b.pipelineState && a.computeRootSignature == b.computeRootSignature);
    assert(a.cbvSrvUavHeap == b.cbvSrvUavHeap && a.runtimeCbvSrvUavHeapResolved == b.runtimeCbvSrvUavHeapResolved);
    assert(a.runtimeCbvSrvUavHeap.generation == b.runtimeCbvSrvUavHeap.generation && a.runtimeCbvSrvUavHeap.size == b.runtimeCbvSrvUavHeap.size);
    assert(a.computeBindingSerial == b.computeBindingSerial && a.bindingQuerySerial == b.bindingQuerySerial);
    assert(a.computeTableValidCount == b.computeTableValidCount);
    assert(!memcmp(a.computeTables, b.computeTables, sizeof(a.computeTables)));
    assert(!memcmp(a.computeRootDescriptors, b.computeRootDescriptors, sizeof(a.computeRootDescriptors)));
    assert(!memcmp(a.computeRootConstants, b.computeRootConstants, sizeof(a.computeRootConstants)));
}
static void TestSnapshotOverwrite() {
    DX12SimulateContext runtime;
    CommandListBindingState full;
    ID3D12PipelineState pso;
    ID3D12RootSignature sig;
    ID3D12DescriptorHeap heap;
    runtime.pipelineState = &pso; runtime.computeRootSignature = &sig; runtime.cbvSrvUavHeap = &heap;
    runtime.runtimeCbvSrvUavHeapResolved = true; runtime.runtimeCbvSrvUavHeap = {51, 52};
    runtime.computeBindingSerial = 53; runtime.bindingQuerySerial = 54; runtime.computeRootTableValidCount = 55;
    for (UINT i = 0; i < MaxRootParameters; ++i) {
        runtime.computeRootTables[i] = {true, i + 100ull};
        runtime.computeRootDescriptors[i] = {true, i + 200ull};
        runtime.computeRootConstants[i].valid = true;
        runtime.computeRootConstants[i].values[63] = i + 300;
        runtime.computeRootConstants[i].valueValid[63] = true;
    }
    DX12PreSkinComputeSnapshot reused, expected;
    assert(BuildRuntimeComputeBindingSnapshot(&runtime, &expected));
    assert(BuildRuntimeComputeBindingSnapshot(&runtime, &reused));
    CheckSnapshot(reused, expected);
    // Alternating sources and fully populated -> empty mimics reset/reload/reuse:
    // every scalar, array and invalid entry must be overwritten, not left stale.
    FillPreSkinComputeSnapshotFromFull(full, &reused);
    CheckSnapshot(reused, full);
    assert(BuildRuntimeComputeBindingSnapshot(&runtime, &reused));
    CheckSnapshot(reused, expected);
    assert(!BuildRuntimeComputeBindingSnapshot(nullptr, &reused));
    CheckSnapshot(reused, expected); // failure does not publish partial data
    assert(!BuildRuntimeComputeBindingSnapshot(&runtime, nullptr));
    FillPreSkinComputeSnapshotFromFull(full, nullptr);
    std::cout << "snapshot: both complete overwrite paths, empty reset and null failure passed\n";
}
int main() {
    TestMatching(); TestVerdictAndIdentity(); TestCollectorPrefix(); TestSnapshotOverwrite();
    std::cout << "PASS: standalone PreSkin production-body regression tests (not D3D integration)\n";
}
