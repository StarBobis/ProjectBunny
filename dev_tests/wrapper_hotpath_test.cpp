// Real wrapper/RememberPipelineState bodies, with isolated COM/redirect/compiler
// dependencies. This verifies CPU semantics, not driver behavior or frame time.
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>
#define DX12_PROF_COUNT(...) ((void)0)
#define DX12_PROF_ADD_ITEMS(...) ((void)0)
#define DX12_PROF_API_SCOPE(...) ((void)0)
#define DX12_PROF_STAGE_BEGIN(...) ((void)0)
#define DX12_PROF_STAGE_END(...) ((void)0)
#define DX12_PROF_STAGE_SCOPE(...) ((void)0)
static void Check(bool value, const char *message) { if (!value) { std::fprintf(stderr, "FAIL %s\n", message); std::exit(1); } }
static std::unordered_map<ID3D12Resource*, ID3D12Resource*> wrapped;
static size_t unwrapCalls;
static bool staticEnabled, preSkinEnabled, staticChanges, preSkinChanges;
static bool captureChanges = true;
static UINT restoreCalls;
struct Decision {
    int payload = 0;
    UINT64 computeShaderHash = 0;
    bool hasComputeShader = false;
    unsigned resets = 0;
    void Reset() { payload = 0; computeShaderHash = 0; hasComputeShader = false; ++resets; }
};
struct DX12SimulateContext {
    std::vector<D3D12_RESOURCE_BARRIER> barrierScratch;
    bool staticTemplateIaApplied = false;
    int ia = 0;
    ID3D12PipelineState *pipelineState = nullptr;
    ID3D12PipelineState *cachedReplacementPsoKey = nullptr;
    ID3D12PipelineState *cachedReplacementPso = nullptr;
    bool cachedCsHashValid = false;
    UINT64 cachedCsHash = 0;
    Decision dispatchDecision, drawDecision;
    unsigned invalidations = 0;
};
struct TestCommandList {
    std::vector<D3D12_RESOURCE_BARRIER> barriers;
    std::vector<D3D12_VERTEX_BUFFER_VIEW> views;
    UINT calls = 0, start = 0;
    void ResourceBarrier(UINT count, const D3D12_RESOURCE_BARRIER *data) {
        ++calls; barriers.clear(); if (data) barriers.assign(data, data + count);
    }
    void IASetVertexBuffers(UINT slot, UINT count, const D3D12_VERTEX_BUFFER_VIEW *data) {
        ++calls; start = slot; views.clear(); if (data) views.assign(data, data + count);
    }
};
namespace DX12CommandListHookSignatures {
using ResourceBarrier = void (*)(TestCommandList*, UINT, const D3D12_RESOURCE_BARRIER*);
using IaSetVertexBuffers = void (*)(TestCommandList*, UINT, UINT, const D3D12_VERTEX_BUFFER_VIEW*);
}
struct DX12IaReplacementExecutorCallbacks {
    void (*setIndexBuffer)() = nullptr;
    void (*setVertexBuffers)() = nullptr;
};
struct DX12IaReplacementExecutor {
    static void RestoreStaticTemplateIa(TestCommandList*, DX12SimulateContext *state, DX12IaReplacementExecutorCallbacks) {
        ++restoreCalls; state->staticTemplateIaApplied = false;
    }
};
static bool DX12HasStaticIaReplacements() { return staticEnabled; }
static bool DX12ModHasPreSkinOutputResizeBindings() { return preSkinEnabled; }
static std::vector<D3D12_RESOURCE_BARRIER> capturedBarriers;
static std::vector<D3D12_VERTEX_BUFFER_VIEW> capturedViews;
struct DX12CommandListStateFlow {
    static void CaptureResourceBarrier(TestCommandList*, UINT count, const D3D12_RESOURCE_BARRIER *data) {
        // Allocate before consuming: fallback storage must still be alive.
        std::vector<D3D12_RESOURCE_BARRIER> churn(count);
        capturedBarriers.clear(); if (data) capturedBarriers.assign(data, data + count);
    }
    static bool CaptureVertexBuffers(TestCommandList*, DX12SimulateContext*, UINT, UINT count, const D3D12_VERTEX_BUFFER_VIEW *data) {
        capturedViews.clear(); if (data) capturedViews.assign(data, data + count);
        return captureChanges;
    }
};
struct WrapperTestDependencies {
    static ID3D12Resource *UnwrapResource(ID3D12Resource *value) {
        ++unwrapCalls; auto it = wrapped.find(value); return it == wrapped.end() ? value : it->second;
    }
    static void DirectSetIndexBuffer() {}
    static void DirectSetVertexBuffers() {}
    static bool PreSkinRedirectPossible(const D3D12_VERTEX_BUFFER_VIEW*, UINT) { return preSkinEnabled; }
    static bool TryRedirectStaticIaVertexBuffers(TestCommandList*, DX12SimulateContext*, UINT start, UINT count,
        const D3D12_VERTEX_BUFFER_VIEW *input, D3D12_VERTEX_BUFFER_VIEW *output, UINT capacity) {
        if (!input || !count || start >= 32 || count > 32 - start || count > capacity) return false;
        for (UINT i = 0; i < count; ++i) { output[i] = input[i]; if (staticChanges) output[i].BufferLocation += 1000; }
        return staticChanges;
    }
};
struct DX12PreSkinIaRedirectUtils {
    static bool RedirectVertexBufferViews(TestCommandList*, UINT, UINT count, const D3D12_VERTEX_BUFFER_VIEW *input,
        D3D12_VERTEX_BUFFER_VIEW *output, UINT capacity, int*) {
        if (!input || !count || count > capacity) return false;
        for (UINT i = 0; i < count; ++i) { output[i] = input[i]; if (preSkinChanges) output[i].SizeInBytes += 500; }
        return preSkinChanges;
    }
};
#include "wrapper_under_test.h"
struct DX12HotPathState {
    static constexpr UINT64 FLAG_SHADER_OVERRIDES = 1, FLAG_PRE_SKIN_PROBE = 2;
    inline static UINT64 HookWorkFlags = 0;
};
struct DX12CommandListRuntime {
    static void RememberPipelineState(DX12SimulateContext*, ID3D12PipelineState*);
    static void InvalidateResolvedDrawPlan(DX12SimulateContext *s) { ++s->invalidations; }
    // Complete replacement models the production cache assignment / resetting
    // setters. An explicit Reset BEFORE this would be visible in resets.
    static void CompileDrawDecision(DX12SimulateContext *s, bool) { s->drawDecision.payload = 42; }
    static void CompileDispatchDecision(DX12SimulateContext *s, bool, bool) {
        s->dispatchDecision.payload = 77; s->dispatchDecision.computeShaderHash = 123; s->dispatchDecision.hasComputeShader = true;
    }
};
#include "pso_under_test.h"
static ID3D12Resource *Resource(size_t value) { return reinterpret_cast<ID3D12Resource*>(value * 256); }
static bool SameBarrier(const D3D12_RESOURCE_BARRIER &a, const D3D12_RESOURCE_BARRIER &b) { return std::memcmp(&a, &b, sizeof(a)) == 0; }
static void NativeBarrier(TestCommandList *c, UINT n, const D3D12_RESOURCE_BARRIER *b) { c->ResourceBarrier(n, b); }
static void NativeIa(TestCommandList *c, UINT s, UINT n, const D3D12_VERTEX_BUFFER_VIEW *v) { c->IASetVertexBuffers(s, n, v); }
static std::vector<D3D12_RESOURCE_BARRIER> MakeBarriers(UINT count, bool mixed) {
    std::vector<D3D12_RESOURCE_BARRIER> result(count);
    for (UINT i = 0; i < count; ++i) {
        auto &b = result[i]; b.Type = static_cast<D3D12_RESOURCE_BARRIER_TYPE>(i % 4);
        b.Flags = D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY;
        auto *p = Resource(mixed && i % 2 ? 1 : 3);
        switch (b.Type) {
        case D3D12_RESOURCE_BARRIER_TYPE_TRANSITION:
            b.Transition.pResource = p; b.Transition.Subresource = i; b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
            b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST; break;
        case D3D12_RESOURCE_BARRIER_TYPE_ALIASING:
            b.Aliasing.pResourceBefore = p; b.Aliasing.pResourceAfter = mixed ? Resource(2) : nullptr; break;
        case D3D12_RESOURCE_BARRIER_TYPE_UAV: b.UAV.pResource = i % 8 ? p : nullptr; break;
        default: break; // Unknown tags/payload are forwarded unchanged.
        }
    }
    return result;
}
static void TestBarriers() {
    wrapped = {{Resource(1), Resource(101)}, {Resource(2), Resource(102)}};
    for (UINT count : {0u, 1u, 8u, 64u, 65u, 1024u}) for (bool mixed : {false, true}) {
        auto source = MakeBarriers(count, mixed); auto immutable = source;
        std::vector<D3D12_RESOURCE_BARRIER> actual, expected;
        const D3D12_RESOURCE_BARRIER *a = nullptr, *b = nullptr;
        unwrapCalls = 0; BaselineWrapper::UnwrapResourceBarrierArray(source.data(), count, &expected, &b);
        size_t oldCalls = unwrapCalls;
        unwrapCalls = 0; DX12WrapperRuntimeUtils::UnwrapResourceBarrierArray(source.data(), count, &actual, &a);
        Check(unwrapCalls <= oldCalls, "no repeated unwrap probes");
        for (UINT i = 0; i < count; ++i) Check(SameBarrier(a[i], b[i]), "exact barrier flags/payload and both endpoints");
        Check(actual.empty() || a == actual.data(), "copied array owns result");
        if (!mixed) Check(a == source.data(), "native array remains zero-copy");
        Check(std::memcmp(source.data(), immutable.data(), count * sizeof(D3D12_RESOURCE_BARRIER)) == 0, "source remains unchanged");
        for (bool resident : {false, true}) for (bool callback : {false, true}) {
            DX12SimulateContext state; TestCommandList command;
            DX12WrapperRuntimeUtils::ResourceBarrier(&command, resident ? &state : nullptr, count, source.data(), callback ? NativeBarrier : nullptr);
            Check(command.calls == 1 && command.barriers.size() == count, "exact native barrier forwarding");
            for (UINT i = 0; i < count; ++i) {
                Check(SameBarrier(command.barriers[i], b[i]), "resident/fallback storage lifetime");
                Check(SameBarrier(capturedBarriers[i], b[i]), "capture sees same unwrapped barriers");
            }
        }
    }
    D3D12_RESOURCE_BARRIER alias{}; alias.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
    for (auto *before : std::array<ID3D12Resource*, 3>{nullptr, Resource(1), Resource(3)}) for (auto *after : std::array<ID3D12Resource*, 3>{nullptr, Resource(2), Resource(3)}) {
        alias.Aliasing = {before, after}; std::vector<D3D12_RESOURCE_BARRIER> storage; const D3D12_RESOURCE_BARRIER *out;
        DX12WrapperRuntimeUtils::UnwrapResourceBarrierArray(&alias, 1, &storage, &out);
        Check(out->Aliasing.pResourceBefore == WrapperTestDependencies::UnwrapResource(before), "alias before independent");
        Check(out->Aliasing.pResourceAfter == WrapperTestDependencies::UnwrapResource(after), "alias after independent");
    }
    std::vector<D3D12_RESOURCE_BARRIER> storage; const D3D12_RESOURCE_BARRIER *out = &alias;
    DX12WrapperRuntimeUtils::UnwrapResourceBarrierArray(nullptr, 99, &storage, &out); Check(out == nullptr, "null source");
    DX12WrapperRuntimeUtils::UnwrapResourceBarrierArray(&alias, 1, nullptr, &out); Check(out == &alias, "null storage");
    DX12WrapperRuntimeUtils::UnwrapResourceBarrierArray(&alias, 1, &storage, nullptr);
    // No extra pointer cache: remapping a live wrapper identity is seen immediately.
    wrapped[Resource(1)] = Resource(201); alias.Aliasing.pResourceBefore = Resource(1);
    DX12WrapperRuntimeUtils::UnwrapResourceBarrierArray(&alias, 1, &storage, &out);
    Check(out->Aliasing.pResourceBefore == Resource(201), "wrapper mapping change not cached across calls");
    std::puts("PASS barriers: native/wrapped/null, alias endpoints, unknown tags, 0..1024, resident/fallback, callback/direct");
}
static void TestIa() {
    for (UINT count : {0u, 1u, 32u, 33u}) for (bool stat : {false, true}) for (bool pre : {false, true})
    for (bool changes : {false, true}) for (bool resident : {false, true}) for (bool callback : {false, true}) {
        staticEnabled = stat; preSkinEnabled = pre; staticChanges = preSkinChanges = changes;
        std::vector<D3D12_VERTEX_BUFFER_VIEW> input(count);
        for (UINT i = 0; i < count; ++i) input[i] = {100 + i, 200 + i, 16};
        TestCommandList command; DX12SimulateContext state; state.staticTemplateIaApplied = true;
        DX12WrapperRuntimeUtils::IASetVertexBuffers(&command, resident ? &state : nullptr, 0, count, input.data(), callback ? NativeIa : nullptr);
        Check(command.calls == 1 && command.views.size() == count, "IA forwarding including oversized rejected redirects");
        for (UINT i = 0; i < count; ++i) {
            const auto &v = command.views[i]; const bool valid = count <= 32 && changes;
            Check(v.BufferLocation == input[i].BufferLocation + (valid && stat ? 1000 : 0), "static redirect precedes pre-skin");
            Check(v.SizeInBytes == input[i].SizeInBytes + (valid && pre ? 500 : 0), "pre-skin output survives capture and native call");
            Check(std::memcmp(&v, &capturedViews[i], sizeof(v)) == 0, "IA capture matches native");
        }
        if (resident) Check(!state.staticTemplateIaApplied, "deferred IA restore preserved");
    }
    TestCommandList command; captureChanges = false;
    DX12WrapperRuntimeUtils::IASetVertexBuffers(&command, nullptr, 31, 0, nullptr, NativeIa);
    Check(command.calls == 0, "unchanged capture forwarding policy preserved"); captureChanges = true;
    DX12WrapperRuntimeUtils::IASetVertexBuffers(&command, nullptr, 31, 1, nullptr, NativeIa);
    Check(command.calls == 1 && command.views.empty(), "null IA views forwarded");
    std::puts("PASS IA: static/pre-skin/both/miss, restore, partial/null/0/32/33, callback/direct");
}
static void TestPso() {
    for (UINT64 flags = 0; flags < 4; ++flags) {
        DX12SimulateContext state; state.drawDecision.payload = 999; state.dispatchDecision.payload = 999;
        state.cachedReplacementPsoKey = reinterpret_cast<ID3D12PipelineState*>(1);
        state.cachedCsHashValid = true; state.cachedCsHash = 888;
        DX12HotPathState::HookWorkFlags = flags;
        auto *pso = reinterpret_cast<ID3D12PipelineState*>(256);
        DX12CommandListRuntime::RememberPipelineState(&state, pso);
        Check(state.pipelineState == pso && state.cachedReplacementPsoKey == nullptr && state.invalidations == 1, "PSO transition invalidates old policy");
        Check(state.drawDecision.payload == (flags & 1 ? 42 : 0), "draw enabled compiled/disabled cleared");
        Check(state.drawDecision.resets == (flags & 1 ? 0u : 1u), "no redundant draw preclear");
        Check(state.dispatchDecision.payload == (flags ? 77 : 0), "dispatch enabled compiled/disabled cleared");
        Check(state.dispatchDecision.resets == (flags ? 0u : 1u), "no redundant dispatch preclear");
        Check(state.cachedCsHash == (flags ? 123u : 0u) && state.cachedCsHashValid == bool(flags), "CS cache follows compiled dispatch");
        DX12CommandListRuntime::RememberPipelineState(&state, pso);
        Check(state.invalidations == 1, "same PSO behavior unchanged");
        DX12HotPathState::HookWorkFlags = 0;
        DX12CommandListRuntime::RememberPipelineState(&state, nullptr);
        Check(state.drawDecision.payload == 0 && state.dispatchDecision.payload == 0 && !state.cachedCsHashValid, "null transition clears disabled decisions");
    }
    DX12CommandListRuntime::RememberPipelineState(nullptr, nullptr);
    std::puts("PASS PSO: all shader/probe masks, changed/repeated/null, disabled policy reset");
}
template<class Wrapper> static double Bench(const std::vector<D3D12_RESOURCE_BARRIER> &source, unsigned iterations, size_t &calls) {
    std::vector<D3D12_RESOURCE_BARRIER> storage; const D3D12_RESOURCE_BARRIER *result = nullptr;
    unwrapCalls = 0; auto begin = std::chrono::steady_clock::now();
    for (unsigned i = 0; i < iterations; ++i) {
        Wrapper::UnwrapResourceBarrierArray(source.data(), static_cast<UINT>(source.size()), &storage, &result);
        _ReadWriteBarrier();
    }
    auto end = std::chrono::steady_clock::now(); calls = unwrapCalls;
    Check(result != nullptr, "benchmark consumed result");
    return std::chrono::duration<double, std::micro>(end - begin).count();
}
int main() {
    TestBarriers(); TestIa(); TestPso();
    for (bool mixed : {false, true}) {
        auto source = MakeBarriers(64, mixed); size_t oldCalls = 0, newCalls = 0;
        for (unsigned round = 0; round < 3; ++round) {
            double oldUs = Bench<BaselineWrapper>(source, 200000, oldCalls);
            double newUs = Bench<DX12WrapperRuntimeUtils>(source, 200000, newCalls);
            std::printf("BENCH mixed=%d round=%u baseline_us=%.0f current_us=%.0f probes=%zu/%zu (stub registry, not frame timing)\n", mixed, round, oldUs, newUs, oldCalls, newCalls);
        }
    }
}
