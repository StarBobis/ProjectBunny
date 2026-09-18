#define NOMINMAX
#include <Windows.h>
#include <dxgiformat.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>
#include "MigotoCommandVmUtils.hpp"
#include "legacy_vm.h"

// COM, barriers and locks are deterministic adapters. All resource selection,
// view assembly, retention decisions and VM implementations below are production.
class ID3D12Resource {
public:
    int refs = 1;
    UINT64 address = 1234;
    void AddRef() { ++refs; }
    void Release() { --refs; }
    UINT64 GetGPUVirtualAddress() const { return address; }
};
class ID3D12DescriptorHeap {};
class ID3D12Device {};
class D3D12_CPU_DESCRIPTOR_HANDLE { public: SIZE_T ptr = 0; };
enum D3D12_RESOURCE_STATES { D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER };
constexpr UINT D3D12_RESOURCE_BARRIER_TYPE_UAV = 1, D3D12_RESOURCE_BARRIER_TYPE_TRANSITION = 2, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES = UINT_MAX;
class D3D12_RESOURCE_BARRIER {
public:
    UINT Type = 0;
    class Uav { public: ID3D12Resource* pResource = nullptr; } UAV;
    class TransitionData { public: ID3D12Resource* pResource = nullptr; D3D12_RESOURCE_STATES StateBefore = {}, StateAfter = {}; UINT Subresource = 0; } Transition;
};
class ID3D12GraphicsCommandList {
public:
    std::vector<D3D12_RESOURCE_BARRIER> barriers;
    void ResourceBarrier(UINT n, const D3D12_RESOURCE_BARRIER* p) { barriers.insert(barriers.end(), p, p + n); }
};
class D3D12_VERTEX_BUFFER_VIEW { public: UINT64 BufferLocation = 0; UINT SizeInBytes = 0, StrideInBytes = 0; };
class D3D12_INDEX_BUFFER_VIEW { public: UINT64 BufferLocation = 0; UINT SizeInBytes = 0; DXGI_FORMAT Format = DXGI_FORMAT_UNKNOWN; };
#include "resource_types.h"
class DX12RuntimeState { public: static LONG GetPresentCount() { return 51; } };
int gModLock = 0, sharedLocks = 0, exclusiveLocks = 0, ensures = 0;
#define DX12_LOCK_ACQUIRE_SHARED(lock, name) (++sharedLocks)
#define DX12_LOCK_ACQUIRE_EXCLUSIVE(lock, name) (++exclusiveLocks)
void ReleaseSRWLockShared(int*) {}
void ReleaseSRWLockExclusive(int*) {}
std::map<std::wstring, DX12LoadedResource> gLoadedResources;
class DX12LoadedResourceSnapshotReadScope {};
class DX12IaBufferHash { public: UINT slot = 0; D3D12_VERTEX_BUFFER_VIEW vertexView; };
class DX12IaHashState { public: bool hasIndexBuffer = false; D3D12_INDEX_BUFFER_VIEW indexView; std::vector<DX12IaBufferHash> vertexBuffers; };
class DX12CompiledVertexResourceBinding { public: UINT slot; std::wstring resource; };
class DX12ModIaReplacement {
public:
    bool hasIndexBuffer = false;
    D3D12_INDEX_BUFFER_VIEW indexBuffer;
    UINT vertexBufferStartSlot = 0;
    std::vector<D3D12_VERTEX_BUFFER_VIEW> vertexBuffers;
    std::vector<ID3D12Resource*> retained;
    void RetainResource(ID3D12Resource* p) { if (std::find(retained.begin(), retained.end(), p) == retained.end()) { p->AddRef(); retained.push_back(p); } }
    ~DX12ModIaReplacement() { for (auto* p : retained) p->Release(); }
};
void EnsureLoadedResourceForCommandList(ID3D12Device*, const std::wstring&) { ++ensures; }
const DX12IaBufferHash* FindIaVertexSlot(const DX12IaHashState& ia, UINT slot) { for (auto& item : ia.vertexBuffers) if (item.slot == slot) return &item; return nullptr; }
#include "resource_functions.h"
#include "legacy_resource_functions.h"

namespace Bunny {
class TextureOverrideConfig { public: std::map<UINT, std::wstring> drawPsSrvResources, drawVsSrvResources; std::wstring drawReplaceThisResource; };
}
class DX12CompiledTextureOverridePlan { public: std::vector<UINT> drawSrvResources; std::wstring drawReplaceThisResource; };
class DX12TextureOverrideMatch {};
class DX12CurrentShaderResourceBinding { public: UINT slot = 0; std::array<UINT64, 24> descriptor = {}; };
class DX12DrawSrvReplacementUtils {
public:
    inline static int configCalls = 0, compiledCalls = 0;
    inline static bool changed = true;
    static bool ApplyConfig(ID3D12Device*, ID3D12GraphicsCommandList*, const Bunny::TextureOverrideConfig&, const DX12TextureOverrideMatch*, DX12ModIaReplacement*, const std::vector<DX12CurrentShaderResourceBinding>*) { ++configCalls; return changed; }
    static bool ApplyCompiledPlan(ID3D12Device*, ID3D12GraphicsCommandList*, const DX12CompiledTextureOverridePlan&, const DX12TextureOverrideMatch*, DX12ModIaReplacement*, const std::vector<DX12CurrentShaderResourceBinding>*) { ++compiledCalls; return changed; }
};
class ExecutorTail {
public:
    ID3D12Device* mDevice;
    ID3D12GraphicsCommandList* mCommandList;
    DX12ModIaReplacement* mReplacement;
    int queries = 0, changes = 0;
    std::vector<DX12CurrentShaderResourceBinding> source, result;
    const std::vector<DX12CurrentShaderResourceBinding>* GetGraphicsSrvBindings() { ++queries; result = source; return &result; }
    void MarkChanged() { ++changes; }
#include "executor_tail.h"
};
static void Check(bool ok, const char* message) { if (!ok) { std::fprintf(stderr, "FAIL %s\n", message); std::exit(1); } }
volatile UINT64 sink = 0;
template<class Fn> double Bench(Fn fn, int count) {
    const auto begin = std::chrono::steady_clock::now();
    for (int i = 0; i < count; ++i) fn(i);
    return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - begin).count() / count;
}
static void ResourceTests() {
    ID3D12Resource base, uav, srv;
    for (int flags = 0; flags < 512; ++flags) {
        DX12LoadedResource a;
        a.resource = flags & 1 ? &base : nullptr;
        a.uavResource = flags & 2 ? &uav : nullptr;
        a.srvResource = flags & 4 ? &srv : nullptr;
        if (flags & 256) a.srvResource = a.uavResource; // aliased pointer precedence
        a.uavValid = (flags & 8) != 0;
        a.uavValidPresent = flags & 16 ? 51 : 50;
        a.uavState = flags & 32 ? D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER : D3D12_RESOURCE_STATE_COPY_DEST;
        a.uavBarrierPending = (flags & 64) != 0;
        a.byteWidth = 100;
        a.uavByteWidth = flags & 128 ? 0 : UINT64(UINT_MAX) + 73;
        a.srvByteWidth = flags & 128 ? 80 : 200;
        a.stride = 24;
        a.format = DXGI_FORMAT_R16_UINT;
        a.name = L"Resource long name for metadata allocation";
        a.path = L"D:\\mod\\long\\resource\\path.bin";
        auto b = a;
        DX12IaResourceViewSnapshot now;
        LegacyIaResourceViewSnapshot old;
        SnapshotIaResourceViewLocked(&a, &now);
        LegacySnapshot(&b, &old);
        Check(now.resource == old.resource && now.uavBarrierPending == old.uavBarrierPending && now.transitionToVertex == old.transitionToVertex && now.stateBefore == old.stateBefore, "snapshot selection/barriers differential");
        Check(a.uavState == b.uavState && a.uavBarrierPending == b.uavBarrierPending, "resource mutation differential");
        if (now.resource) {
            Check(now.byteWidth == IaViewByteWidthForResource(old.metadata, old.resource) && now.stride == old.metadata.stride && now.format == old.metadata.format, "compact metadata differential");
            const int before = now.resource->refs;
            AddRefIaResourceViewSnapshot(&now); AddRefIaResourceViewSnapshot(&now);
            Check(now.resource->refs == before + 1, "one snapshot retain");
            ReleaseIaResourceViewSnapshot(&now);
            Check(old.resource->refs == before && !now.retained && !now.resource, "snapshot release");
        }
    }
    gLoadedResources.clear();
    auto& loaded = gLoadedResources[L"mesh"];
    loaded.resource = &base; loaded.byteWidth = 123; loaded.stride = 0;
    DX12IaHashState ia; ia.hasIndexBuffer = true; ia.indexView.Format = DXGI_FORMAT_R16_UINT;
    ia.vertexBuffers.push_back({2, {0, 0, 36}});
    ID3D12Device device; ID3D12GraphicsCommandList command;
    {
        DX12ModIaReplacement replacement;
        Check(AppendResourceViewsForCommandList(&device, &command, ia, L"mesh", {{2, L"mesh"}}, &replacement), "append resource production body");
        Check(replacement.indexBuffer.SizeInBytes == 123 && replacement.indexBuffer.Format == DXGI_FORMAT_R16_UINT && replacement.vertexBuffers[0].StrideInBytes == 36, "IA fallback format/stride");
        Check(base.refs == 2 && replacement.retained.size() == 1, "dedup retain adapter lifetime");
        loaded.uavResource = &uav; loaded.uavByteWidth = 456; loaded.uavValid = true; loaded.uavValidPresent = 51; loaded.uavBarrierPending = true;
        Check(AppendResourceViewsForCommandList(&device, &command, ia, L"mesh", {}, &replacement), "UAV append");
        Check(command.barriers.size() == 2 && replacement.indexBuffer.SizeInBytes == 456, "UAV transition preserved");
        loaded.uavValidPresent = 50; loaded.byteWidth = 789;
        AppendResourceViewsForCommandList(&device, &command, ia, L"mesh", {}, &replacement);
        Check(replacement.indexBuffer.SizeInBytes == 789, "fresh epoch and mutable metadata are not cached");
        gLoadedResources.clear();
        Check(!AppendResourceViewsForCommandList(&device, &command, ia, L"mesh", {}, &replacement), "missing resource after reload");
    }
    Check(base.refs == 1 && uav.refs == 1, "final resource lifetime");
    Check(sharedLocks > 0 && exclusiveLocks > 0, "shared and write snapshot branches");
    std::printf("PASS 512 production resource snapshot cases + append/lifetime/reload\n");
}
static void TailTests() {
    ID3D12Device device; ID3D12GraphicsCommandList command; DX12ModIaReplacement replacement;
    ExecutorTail tail{&device, &command, &replacement};
    Bunny::TextureOverrideConfig config;
    DX12CompiledTextureOverridePlan plan;
    DX12TextureOverrideMatch match;
    tail.ApplyTextureOverrideDrawSrvResources(config, nullptr, match);
    tail.ApplyTextureOverrideDrawSrvResources(config, &plan, match);
    Check(tail.queries == 0, "empty config and plan do not query bindings");
    config.drawPsSrvResources[3] = L"dynamic";
    tail.ApplyTextureOverrideDrawSrvResources(config, &plan, match);
    Check(tail.queries == 0, "compiled plan precedence over config");
    tail.ApplyTextureOverrideDrawSrvResources(config, nullptr, match);
    config.drawPsSrvResources.clear(); config.drawVsSrvResources[9] = L"vs";
    tail.ApplyTextureOverrideDrawSrvResources(config, nullptr, match);
    config.drawVsSrvResources.clear(); config.drawReplaceThisResource = L"this";
    tail.ApplyTextureOverrideDrawSrvResources(config, nullptr, match);
    plan.drawSrvResources.push_back(4);
    tail.ApplyTextureOverrideDrawSrvResources(config, &plan, match);
    plan.drawSrvResources.clear(); plan.drawReplaceThisResource = L"this";
    tail.ApplyTextureOverrideDrawSrvResources(config, &plan, match);
    Check(tail.queries == 5 && tail.changes == 5, "positive PS/VS/this and compiled paths still run");
    DX12DrawSrvReplacementUtils::changed = false;
    tail.ApplyTextureOverrideDrawSrvResources(config, &plan, match);
    Check(tail.queries == 6 && tail.changes == 5, "failure/no-change result preserved");
    tail.mDevice = nullptr;
    tail.ApplyTextureOverrideDrawSrvResources(config, &plan, match);
    Check(tail.queries == 6, "missing device gate");
    std::printf("PASS executor production tail gates (binding/apply adapters; no GPU claim)\n");
}
template<class VM> void VmStateTests() {
    typename VM::State state;
    VM::SetGlobal(L"$x", 1);
    state.PushLocalScope(); state.DeclareLocal(L"local $v", L"3");
    state.Assign(L"$x", L"$v + 2");
    state.QueuePostAssign(L"post $x", L"$x + $v");
    state.Assign(L"$v", L"4");
    state.FlushPostAssignments();
    Check(state.Evaluate(L"$x") == 9, "post evaluates late with current locals");
    state.ApplyControlFlow(L"if 0"); state.ApplyControlFlow(L"if 1"); Check(!state.Active(), "nested inactive branch");
    state.ApplyControlFlow(L"endif"); state.ApplyControlFlow(L"else if 1 || 0"); Check(state.Active(), "elif expression");
    state.ApplyControlFlow(L"else"); Check(!state.Active(), "taken branch not repeated");
    state.ApplyControlFlow(L"endif"); state.PopLocalScope();
    Check(state.Evaluate(L"$v") == 0, "local popped");
    VM::SetGlobal(L"$x", 27); Check(state.Evaluate(L"$\\ns\\x") == 27, "namespaced live globals");
}
static void VmTests() {
    VmStateTests<MigotoCommandVmUtils>(); VmStateTests<LegacyCommandVmUtils>();
    const std::vector<std::wstring> expressions = {L"", L"1", L" 1\t", L"1 || 0", L"0 && window_width", L"1 + 2 * 3", L"(1 + 2) * 3", L"-7 // 2", L"7 % 0", L"3 / 0.0000001", L"1==1.0000005", L"1!=1.000001", L"3>=4 || 4<=5", L"!(-2)", L"+2- -3", L"1e3/2", L".5", L"(1", L"1 +", L"1 2", L"$x + $\\ns\\x", L"missing", L"1 & 2", L"NaN", L"1 / 0", L"1 || time"};
    MigotoCommandVmUtils::ExpressionContext a;
    LegacyCommandVmUtils::ExpressionContext b;
    for (const auto& expression : expressions) {
        MigotoCommandVmUtils::ExpressionParser p(expression, a);
        LegacyCommandVmUtils::ExpressionParser q(expression, b);
        const double x = p.Parse(), y = q.Parse();
        Check((x == y || (std::isnan(x) && std::isnan(y))) && p.pos == q.pos && p.ok == q.ok, "VM grammar/value/position/error differential");
    }
    const std::array<const wchar_t*, 16> ops = {L"+", L"-", L"*", L"/", L"//", L"%", L"==", L"!=", L">=", L"<=", L">", L"<", L"||", L"&&", L"&", L""};
    for (int i = 0; i < 1024; ++i) {
        std::wstring expression = L"(" + std::to_wstring(i % 7 - 3) + ops[i % ops.size()] + std::to_wstring(i % 9) + L") " + ops[(i / 16) % ops.size()] + L" 2";
        MigotoCommandVmUtils::ExpressionParser p(expression, a); LegacyCommandVmUtils::ExpressionParser q(expression, b);
        Check(p.Parse() == q.Parse() && p.pos == q.pos && p.ok == q.ok, "generated valid/invalid VM differential");
    }
    Check(MigotoCommandVmUtils::VariableStore::frameVariableReferenced.load() == 1, "logical RHS still evaluated for frame variable side effect");
    std::printf("PASS VM real headers: 1050 expression differentials, post/local/branch/live global tests\n");
}
// The same noinline boundary prevents folding known metadata to a constant.
__declspec(noinline) static UINT64 SnapshotOld(const DX12LoadedResource& loaded) {
    LegacyIaResourceViewSnapshot s; LegacyReadOnly(loaded, &s);
    return IaViewByteWidthForResource(s.metadata, s.resource) + s.metadata.stride + UINT(s.metadata.format) + (s.resource != nullptr);
}
__declspec(noinline) static UINT64 SnapshotNew(const DX12LoadedResource& loaded) {
    DX12IaResourceViewSnapshot s; SnapshotIaResourceViewReadOnlyLocked(loaded, &s);
    return s.byteWidth + s.stride + UINT(s.format) + (s.resource != nullptr);
}
static void Benchmarks() {
    ID3D12Resource base;
    DX12LoadedResource loaded; loaded.resource = &base; loaded.byteWidth = 1024; loaded.stride = 24;
    loaded.name.assign(48, L'n'); loaded.path.assign(160, L'p');
    for (int mapSize : {0, 16}) {
        for (int i = 0; i < mapSize; ++i) { loaded.drawSrvCompatibleHeaps[i] = nullptr; loaded.drawSrvCompatibleCpus[i] = {}; }
        for (int run = 0; run < 3; ++run) {
            const auto old = Bench([&](int i) { loaded.byteWidth = UINT(i + 1); sink = SnapshotOld(loaded); }, 30000);
            const auto now = Bench([&](int i) { loaded.byteWidth = UINT(i + 1); sink = SnapshotNew(loaded); }, 30000);
            std::printf("metadata maps=%d run=%d baseline=%.1f ns compact=%.1f ns\n", mapSize, run, old, now);
        }
    }
    const std::array<std::wstring, 3> expressions = {L"1", L"$x == 27 && (4 * 5 + 2) >= 20", L"(1 + 2 * 3) > 0 || 0 && 4 <= 6"};
    for (size_t e = 0; e < expressions.size(); ++e) for (int run = 0; run < 3; ++run) {
        const auto old = Bench([&](int) { sink = UINT64(LegacyCommandVmUtils::EvaluateExpression({}, expressions[e])); }, 50000);
        const auto now = Bench([&](int) { sink = UINT64(MigotoCommandVmUtils::EvaluateExpression({}, expressions[e])); }, 50000);
        std::printf("VM expr=%zu run=%d baseline=%.1f ns literal-match=%.1f ns\n", e, run, old, now);
    }
    std::printf("snapshot sizeof baseline=%zu compact=%zu; metadata benchmark excludes locks/COM/GPU and is not frame timing\n", sizeof(LegacyIaResourceViewSnapshot), sizeof(DX12IaResourceViewSnapshot));
}
int main() { ResourceTests(); TailTests(); VmTests(); Benchmarks(); }
