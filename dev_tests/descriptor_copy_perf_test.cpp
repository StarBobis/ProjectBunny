// Exact production slot, cache and copy-hook bodies extracted by the .ps1.
// The hook's policy/handle-registry/ledger and profiling sinks are test adapters.
// No D3D device/game required. Results measure this CPU harness, not game FPS.
#define NOMINMAX
#include <Windows.h>
#include <d3d12.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>
using std::min;
#include "descriptor_types_under_test.h"
#include "descriptor_cache_under_test.h"
using Slots = DX12DescriptorSlotArrayUtils;
static void Check(bool value, const char *message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static std::unordered_map<std::string, LONG64> counters;
static uint64_t bucketLookups = 0;
static void Add(const char *name, LONG64 value) {
    if (!value) return;
    ++bucketLookups;
    counters[name] += value;
}
#define DX12_PROF_ADD_ITEMS(name, value) Add(name, value)
#define DX12_PROF_ADD_CALLS(name, value) Add(name, value)
static bool hunt = false;
class DX12ResourceTrackingPolicyUtils {
public: static bool NeedsDumpOrHuntingTracking() { return hunt; }
};
static std::vector<DescriptorHeapRecord*> heaps;
static std::unordered_map<SIZE_T, DescriptorRecord> ledger;
class DX12DescriptorHeapShadowUtils {
public:
    using ResolvedHeapSlot = CacheUnderTest::ResolvedHeapSlot;
#include "descriptor_commit_under_test.h"
    static UINT CachedIncrementForType(D3D12_DESCRIPTOR_HEAP_TYPE) { return 32; }
    static void CacheIncrementForType(D3D12_DESCRIPTOR_HEAP_TYPE, UINT) {}
    static bool ResolveHeapSlotFast(SIZE_T handle, D3D12_DESCRIPTOR_HEAP_TYPE type, ResolvedHeapSlot *out) {
        for (size_t i = 0; i < heaps.size(); ++i) {
            auto *h = heaps[i];
            if (h->stateLocks->IsActive() && handle >= h->cpuStart &&
                handle - h->cpuStart < SIZE_T(h->desc.NumDescriptors) * h->increment &&
                (type == D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES || h->desc.Type == type)) {
                *out = {h, i, UINT((handle - h->cpuStart) / h->increment)};
                return true;
            }
        }
        return false;
    }
    static bool FindLatestDescriptor(SIZE_T handle, DescriptorRecord *out) {
        ResolvedHeapSlot r;
        if (ResolveHeapSlotFast(handle, D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES, &r) &&
            Slots::CopyCurrentRecord(*r.heap, r.slot, out)) return true;
        auto i = ledger.find(handle);
        if (i == ledger.end()) return false;
        *out = i->second; return true;
    }
    static bool WriteDescriptorToSlot(SIZE_T handle, DescriptorRecord &&value) {
        ResolvedHeapSlot r;
        if (!ResolveHeapSlotFast(handle, D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES, &r)) return false;
        if (!r.heap->IsMaterialized()) r.heap->chunkedSlots.EnsureChunkForSlot(r.slot);
        if (Slots::StoreCurrentRecordIfChanged(r.heap, r.slot, value, handle) == Slots::StoreCurrentSlotResult::Stored)
            DX12DescriptorHeapStateUtils::PublishValue(r.heap, r.slot, value.valueId);
        return true;
    }
    static void WriteToCpuLedger(DescriptorRecord &&record) { ledger[record.cpuHandle] = std::move(record); }
    static void EraseDescriptor(SIZE_T handle) {
        ResolvedHeapSlot r;
        if (ResolveHeapSlotFast(handle, D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES, &r)) {
            if (Slots::EraseCurrentRecord(r.heap, r.slot)) DX12DescriptorHeapStateUtils::PublishValue(r.heap, r.slot, 0);
        } else ledger.erase(handle);
    }
};
class DX12ResourceTrackerHookRuntime {
public:
    static void RecordCopiedDescriptors(ID3D12Device*, UINT, const D3D12_CPU_DESCRIPTOR_HANDLE*, const UINT*, UINT, const D3D12_CPU_DESCRIPTOR_HANDLE*, const UINT*, D3D12_DESCRIPTOR_HEAP_TYPE);
    static void RecordCopiedDescriptorsSimple(UINT, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_DESCRIPTOR_HEAP_TYPE, UINT);
    static void RecordCopiedDescriptorsSimple(ID3D12Device*, UINT, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_DESCRIPTOR_HEAP_TYPE);
};
#include "descriptor_hooks_under_test.h"
static constexpr auto Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
static void Init(DescriptorHeapRecord &h, SIZE_T base, bool dense, UINT count = 8200) {
    h.cpuStart = base; h.increment = 32; h.desc.NumDescriptors = count; h.desc.Type = Type;
    h.stateLocks = std::make_unique<DescriptorHeapRecord::StateLocks>(count);
    h.chunkedSlots.Initialize(count);
    if (dense) { h.slots.resize(count); h.occupied.resize(count); h.stateLocks->MarkMaterialized(); }
}
static DescriptorRecord Value(UINT kind, UINT64 id) {
    DescriptorRecord r;
    static const char *names[] = {"", "CBV", "SRV", "UAV", "RTV", "DSV", "Sampler"};
    r.kind = names[kind]; r.kindId = kind; r.valueId = id; r.hasDesc = true;
    r.cbv = {id * 256, 256}; r.descriptorHuntHash = UINT(id * 13);
    r.staticReplacementGeneration = id * 7; r.hasResourceDesc = true;
    r.resourceDesc.Width = id * 1024; r.resourceHeapType = D3D12_HEAP_TYPE_UPLOAD;
    r.hasResourceHeapType = true; r.resourceOffset = 16; r.viewSize = 256;
    r.currentState = D3D12_RESOURCE_STATE_COPY_SOURCE; r.hasCurrentState = true;
    r.stateTrackedByBarrier = true;
    // Unresolved resource pointers are opaque descriptor metadata, not COM calls.
    r.resource = reinterpret_cast<ID3D12Resource*>(uintptr_t(0x12340000));
    r.counterResource = reinterpret_cast<ID3D12Resource*>(uintptr_t(0x56780000));
    r.ownsResource = r.ownsCounterResource = true;
    return r;
}
static void Store(DescriptorHeapRecord &h, UINT slot, const DescriptorRecord &r) {
    if (!h.IsMaterialized()) h.chunkedSlots.EnsureChunkForSlot(slot);
    Slots::StoreCurrentRecordIfChanged(&h, slot, r, h.cpuStart + SIZE_T(slot) * 32);
    DX12DescriptorHeapStateUtils::PublishValue(&h, slot, r.valueId);
}
static void CheckValue(const DescriptorRecord &actual, const DescriptorRecord &r, SIZE_T handle) {
    Check(actual.valueId == r.valueId && actual.cpuHandle == handle, "value and destination handle");
    Check(actual.kind == r.kind && actual.kindId == r.kindId, "descriptor kind");
    static constexpr size_t payloadSizes[] = {0, sizeof(r.cbv), sizeof(r.srv), sizeof(r.uav), sizeof(r.rtv), sizeof(r.dsv), sizeof(r.sampler)};
    Check(std::memcmp(static_cast<const DX12DescriptorViewPayload*>(&actual),
          static_cast<const DX12DescriptorViewPayload*>(&r), payloadSizes[r.kindId]) == 0, "active descriptor payload");
    Check(actual.resource == r.resource && actual.counterResource == r.counterResource &&
          !actual.ownsResource && !actual.ownsCounterResource, "borrowed resource metadata");
    Check(actual.descriptorHuntHash == r.descriptorHuntHash &&
          actual.staticReplacementGeneration == r.staticReplacementGeneration, "hunt/replacement metadata");
    Check(actual.resourceDesc.Width == r.resourceDesc.Width && actual.hasResourceDesc &&
          actual.resourceHeapType == r.resourceHeapType && actual.hasResourceHeapType &&
          actual.resourceOffset == r.resourceOffset && actual.viewSize == r.viewSize &&
          actual.currentState == r.currentState && actual.hasCurrentState && actual.stateTrackedByBarrier,
          "dump/resource metadata");
}
static void SlotCases(bool sourceDense, bool destDense) {
    DescriptorHeapRecord source, dest;
    Init(source, 0x100000, sourceDense); Init(dest, 0x200000, destDense);
    for (UINT kind = 1; kind <= 6; ++kind) {
        auto r = Value(kind, 100 + kind);
        Store(source, 4095 + kind, r);
        if (!destDense) dest.chunkedSlots.EnsureChunkForSlot(4095 + kind);
        UINT64 published = 0;
        const SIZE_T handle = dest.cpuStart + SIZE_T(4095 + kind) * 32;
        auto result = Slots::CopyCurrentSlotIfChanged(&dest, 4095 + kind, source, 4095 + kind, handle, &published);
        Check(result == Slots::CopyCurrentSlotResult::Changed && published == r.valueId, "changed slot");
        auto access = Slots::LoadCurrentSlotAccess(dest, 4095 + kind);
        CheckValue(*access.value, r, handle);
        DX12DescriptorHeapStateUtils::PublishValue(&dest, 4095 + kind, published);
        Check(Slots::CopyCurrentSlotIfChanged(&dest, 4095 + kind, source, 4095 + kind, handle) ==
            Slots::CopyCurrentSlotResult::Unchanged, "unchanged slot");
        Check(Slots::EraseCurrentRecord(&dest, 4095 + kind), "erase occupied");
        Check(!Slots::EraseCurrentRecord(&dest, 4095 + kind), "erase empty");
    }
    Check(Slots::ReadLiveSlots(&dest) == 0, "balanced live slots");
    auto unknown = Value(1, 0); Store(source, 0, unknown);
    if (!destDense) dest.chunkedSlots.EnsureChunkForSlot(0);
    Check(Slots::CopyCurrentSlotIfChanged(&dest, 0, source, 0, dest.cpuStart) == Slots::CopyCurrentSlotResult::Changed, "zero id never deduplicated");
    Check(Slots::CopyCurrentSlotIfChanged(&dest, 0, source, 1, dest.cpuStart) == Slots::CopyCurrentSlotResult::SourceMissingDestinationErased, "missing source erases destination");
    Check(Slots::CopyCurrentSlotIfChanged(&dest, 0, source, 1, dest.cpuStart) == Slots::CopyCurrentSlotResult::SourceMissingDestinationEmpty, "missing source and empty destination");
    DescriptorHeapRecord unallocated;
    Init(unallocated, 0x300000, false);
    Check(Slots::CopyCurrentSlotIfChanged(&unallocated, 0, source, 0, unallocated.cpuStart) == Slots::CopyCurrentSlotResult::LockRequired, "missing destination storage requires allocation");
    DescriptorRecord output;
    Check(Slots::CopyCurrentRecord(source, 0, &output), "copy current record");
    CheckValue(output, unknown, source.cpuStart);
}
static void HookCases(bool dense, bool hunting) {
    DescriptorHeapRecord source, dest;
    Init(source, 0x100000, dense); Init(dest, 0x200000, dense); heaps = {&source, &dest}; hunt = hunting;
    for (UINT i = 0; i < 16; ++i) { Store(source, i, Value(1, i + 1)); Store(dest, i, Value(1, 99)); }
    D3D12_CPU_DESCRIPTOR_HANDLE src[] = {{source.cpuStart}, {source.cpuStart + 6 * 32}, {source.cpuStart + 10 * 32}};
    D3D12_CPU_DESCRIPTOR_HANDLE dst[] = {{dest.cpuStart}, {dest.cpuStart + 4 * 32}, {dest.cpuStart + 8 * 32}};
    UINT sizes[] = {4, 0, 2};
    counters.clear(); bucketLookups = 0;
    DX12ResourceTrackerHookRuntime::RecordCopiedDescriptors(nullptr, 3, dst, sizes, 3, src, sizes, Type);
    Check(counters["Desc.CopySlots"] == 6 && counters["Desc.CopyBlocks"] == 2, "flattening and zero-size ranges");
    Check(Slots::CurrentSlotPtr(dest, 0)->valueId == 1 && Slots::CurrentSlotPtr(dest, 9)->valueId == 12, "flattened descriptor values");
    auto v = dest.stateLocks->ReadVersion();
    DX12ResourceTrackerHookRuntime::RecordCopiedDescriptors(nullptr, 3, dst, sizes, 3, src, sizes, Type);
    Check(dest.stateLocks->ReadVersion() == v, "unchanged copy preserves heap version");
    Store(source, 0, Value(1, 54321));
    DX12ResourceTrackerHookRuntime::RecordCopiedDescriptors(nullptr, 3, dst, sizes, 3, src, sizes, Type);
    Check(Slots::CurrentSlotPtr(dest, 0)->valueId == 54321 && dest.stateLocks->ReadVersion() != v, "changed source invalidates destination identity");
    Store(source, 0, Value(1, 1));
    // Known heap, missing chunk, ledger source, and unknown destination retain fallback behavior.
    auto r = Value(1, 900); r.cpuHandle = 0x900000; ledger[r.cpuHandle] = r;
    DX12ResourceTrackerHookRuntime::RecordCopiedDescriptorsSimple(1, {dest.cpuStart + 8192 * 32}, {r.cpuHandle}, Type, 32);
    Check(Slots::CurrentSlotPtr(dest, 8192)->valueId == 900, "ledger source to first-touch slot");
    DX12ResourceTrackerHookRuntime::RecordCopiedDescriptorsSimple(1, {0xa00000}, {source.cpuStart}, Type, 32);
    Check(ledger[0xa00000].valueId == 1, "unknown destination ledger");
    heaps.clear(); ledger.clear();
}
static void ResetCache() {
    // Only called before/after workers, while no cache operation is in flight.
    for (auto &set : CacheUnderTest::heapCache) {
        for (auto &entry : set.ways) entry = {};
        set.insertCounter = 0;
    }
}
static void CacheCases() {
    ResetCache();
    constexpr UINT N = 8;
    std::array<DescriptorHeapRecord, N> h;
    std::array<SIZE_T, N> handles{};
    // Distinct heaps collide in one set to exercise multi-writer replacement.
    UINT found = 0;
    for (SIZE_T p = 0x100000; found < N; p += 0x1000) {
        if (CacheUnderTest::HeapCacheSetIndex(p) != 0) continue;
        handles[found] = p; Init(h[found], p, false, 16); ++found;
    }
    std::atomic<bool> failed = false;
    std::vector<std::thread> workers;
    for (UINT t = 0; t < N; ++t) workers.emplace_back([&, t] {
        CacheUnderTest::ResolvedHeapSlot r{&h[t], t, 0};
        for (UINT i = 0; i < 40000; ++i) {
            CacheUnderTest::HeapCacheStore(handles[t], r);
            CacheUnderTest::ResolvedHeapSlot hit;
            if (CacheUnderTest::HeapCacheLookup(handles[t], Type, &hit) &&
                (hit.heap != &h[t] || hit.heapIndex != t || hit.slot != 0)) failed = true;
        }
    });
    for (auto &w : workers) w.join();
    Check(!failed, "concurrent cache snapshots never mix heap identities");
    CacheUnderTest::ResolvedHeapSlot r{&h[0], 0, 0}, result;
    CacheUnderTest::HeapCacheStore(handles[0], r);
    Check(CacheUnderTest::HeapCacheLookup(handles[0], Type, &result), "cache hit");
    Check(!CacheUnderTest::HeapCacheLookup(handles[0], D3D12_DESCRIPTOR_HEAP_TYPE_RTV, &result), "cache type validation");
    h[0].stateLocks->Deactivate();
    Check(!CacheUnderTest::HeapCacheLookup(handles[0], Type, &result), "retired heap rejected");
    ResetCache();
}
static void IndependentSlotWriters(bool dense) {
    DescriptorHeapRecord source, dest;
    Init(source, 0x100000, dense); Init(dest, 0x200000, dense);
    Store(source, 0, Value(1, 100));
    if (!dense) dest.chunkedSlots.EnsureChunkForSlot(0);
    std::vector<std::thread> workers;
    std::atomic<bool> failed = false;
    for (UINT t = 0; t < 8; ++t) workers.emplace_back([&, t] {
        // D3D12 permits concurrent independent slots, not competing writers
        // to the same descriptor. Shared source is immutable during these copies.
        for (UINT n = 0; n < 10000; ++n) {
            if (Slots::CopyCurrentSlotIfChanged(&dest, t, source, 0, dest.cpuStart + SIZE_T(t) * 32) != Slots::CopyCurrentSlotResult::Changed)
                failed = true;
            if (!Slots::EraseCurrentRecord(&dest, t)) failed = true;
        }
    });
    for (auto &w : workers) w.join();
    Check(!failed && Slots::ReadLiveSlots(&dest) == 0, "independent writers preserve occupancy and live count");
}
static void CacheBenchmark() {
    ResetCache();
    DescriptorHeapRecord heap;
    Init(heap, 0x100000, false);
    CacheUnderTest::ResolvedHeapSlot r{&heap, 42, 0};
    CacheUnderTest::HeapCacheStore(heap.cpuStart, r);
    constexpr UINT iterations = 1000000;
    auto start = std::chrono::steady_clock::now();
    UINT64 checksum = 0;
    for (UINT i = 0; i < iterations; ++i) {
        CacheUnderTest::ResolvedHeapSlot hit;
        Check(CacheUnderTest::HeapCacheLookup(heap.cpuStart, Type, &hit), "cache benchmark hit");
        checksum += hit.heapIndex;
    }
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
    Check(checksum == UINT64(iterations) * 42, "cache benchmark checksum");
    std::printf("global-cache-hit: %lld us / %u lookups (TLS intentionally bypassed)\n", us, iterations);
    ResetCache();
}
static void ChangedSlotBenchmark(bool dense) {
    DescriptorHeapRecord source, dest;
    Init(source, 0x100000, dense); Init(dest, 0x200000, dense);
    Store(source, 0, Value(1, 1)); Store(dest, 0, Value(1, 2));
    auto *src = Slots::CurrentSlotPtr(source, 0);
    constexpr UINT iterations = 1000000;
    auto start = std::chrono::steady_clock::now();
    for (UINT i = 0; i < iterations; ++i) {
        src->valueId = i + 3;
        UINT64 id = 0;
        Check(Slots::CopyCurrentSlotIfChanged(&dest, 0, source, 0, dest.cpuStart, &id) == Slots::CopyCurrentSlotResult::Changed, "changed benchmark result");
        DX12DescriptorHeapStateUtils::PublishRangeValue(&dest, 0, id);
    }
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
    Check(Slots::CurrentSlotPtr(dest, 0)->valueId == iterations + 2, "changed benchmark last value");
    std::printf("changed-slot-%s: %lld us / %u copies with identity publication\n", dense ? "dense" : "chunked", us, iterations);
}
static void Benchmark() {
    DescriptorHeapRecord source, dest;
    Init(source, 0x100000, false); Init(dest, 0x200000, false); heaps = {&source, &dest}; hunt = false;
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 8> src{}, dst{};
    for (UINT i = 0; i < 8; ++i) {
        Store(source, i * 2, Value(1, i + 1)); Store(dest, i, Value(1, i + 1));
        src[i].ptr = source.cpuStart + i * 2 * 32; dst[i].ptr = dest.cpuStart + i * 32;
    }
    constexpr UINT iterations = 100000;
    counters.clear(); bucketLookups = 0;
    auto start = std::chrono::steady_clock::now();
    for (UINT i = 0; i < iterations; ++i)
        DX12ResourceTrackerHookRuntime::RecordCopiedDescriptors(nullptr, 8, dst.data(), nullptr, 8, src.data(), nullptr, Type);
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
    Check(counters["Desc.CopyBlocks"] == iterations * 8 && counters["Rm.DescCopyUnchanged"] == iterations * 8, "benchmark counters");
    std::printf("unchanged-eight-blocks: %lld us, %llu profiling bucket lookups, %u slots\n", us, bucketLookups, iterations * 8);
    heaps.clear();
}
int main(int argc, char **argv) {
    for (bool src : {false, true}) for (bool dst : {false, true}) SlotCases(src, dst);
    for (bool dense : {false, true}) for (bool hunting : {false, true}) HookCases(dense, hunting);
    for (bool dense : {false, true}) IndependentSlotWriters(dense);
    const bool current = argc > 1 && std::strcmp(argv[1], "current") == 0;
    if (current) CacheCases();
    Benchmark();
    CacheBenchmark();
    for (bool dense : {false, true}) ChangedSlotBenchmark(dense);
    std::puts("PASS: slot metadata/zero-id/missing/erase, sparse+dense, range mapping, hunt gate, ledger fallback, invalidation, independent-slot writers");
    std::puts(current ? "PASS: concurrent cache snapshots and retirement" : "SKIP: baseline cache concurrent accesses have known undefined behavior");
}
