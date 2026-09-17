// Exact production slot, cache and copy-hook bodies extracted by the .ps1.
// The hook's policy/ledger/fallback sinks remain test adapters. Real-resolver
// cases use production global/TLS/sequential/map resolution and real SRW locks.
// No D3D device/game required. Results measure this CPU harness, not game FPS.
// Serial retirement/materialization tests do NOT prove legal concurrent heap
// destruction, metadata cleanup, COM lifetime, or hunt-mode transitions.
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
#include <deque>
#include <map>
#include <memory>
#include <optional>
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
static bool collectCounters = true;
static uint64_t fullResolveCalls = 0;
static bool useRealResolver = false;
static DescriptorHeapRecord *diagnosticDestination = nullptr;
static UINT64 destinationL2Hits = 0;
static void Add(const char *name, LONG64 value) {
    if (!value || !collectCounters) return;
    ++bucketLookups;
    counters[name] += value;
}
#define DX12_PROF_ADD_ITEMS(name, value) Add(name, value)
#define DX12_PROF_ADD_CALLS(name, value) Add(name, value)
#define DX12_PROF_COUNT(name) Add(name, 1)
#define DX12_LOCK_ACQUIRE_SHARED(lock, name) AcquireSRWLockShared(lock)
#include "descriptor_resolver_under_test.h"
using RealResolver = ResolverUnderTest::DX12DescriptorHeapShadowUtils;
static bool hunt = false;
class DX12ResourceTrackingPolicyUtils {
public: static bool NeedsDumpOrHuntingTracking() { return hunt; }
};
static std::vector<DescriptorHeapRecord*> heaps;
static std::unordered_map<SIZE_T, DescriptorRecord> ledger;
class DX12DescriptorHeapShadowUtils {
public:
    using ResolvedHeapSlot = RealResolver::ResolvedHeapSlot;
    using HeapResolveCacheEntry = RealResolver::HeapResolveCacheEntry;
#include "descriptor_commit_under_test.h"
    static UINT CachedIncrementForType(D3D12_DESCRIPTOR_HEAP_TYPE) { return 32; }
    static void CacheIncrementForType(D3D12_DESCRIPTOR_HEAP_TYPE, UINT) {}
    static bool ResolveHeapSlotFast(SIZE_T handle, D3D12_DESCRIPTOR_HEAP_TYPE type, ResolvedHeapSlot *out) {
        if (collectCounters) ++fullResolveCalls; // Diagnostics only; no timed-loop counter writes.
        if (useRealResolver) {
            if (collectCounters && diagnosticDestination) {
                const auto before = counters["Lock.Desc.HeapCacheHit"];
                const bool found = RealResolver::ResolveHeapSlotFast(handle, type, out);
                if (found && out->heap == diagnosticDestination)
                    destinationL2Hits += counters["Lock.Desc.HeapCacheHit"] - before;
                return found;
            }
            return RealResolver::ResolveHeapSlotFast(handle, type, out);
        }
        if (!handle || !out) return false;
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
    r.cbv.BufferLocation = id * 256; r.cbv.SizeInBytes = 256;
    r.descriptorHuntHash = UINT(id * 13);
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
    if (r.kindId == 1) {
        // CBV has tail padding; equality must never compare indeterminate bytes.
        Check(actual.cbv.BufferLocation == r.cbv.BufferLocation && actual.cbv.SizeInBytes == r.cbv.SizeInBytes, "CBV payload fields");
    } else {
        Check(std::memcmp(static_cast<const DX12DescriptorViewPayload*>(&actual),
              static_cast<const DX12DescriptorViewPayload*>(&r), payloadSizes[r.kindId]) == 0, "active descriptor payload");
    }
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
    std::printf("vector-mock-only unchanged-eight-blocks (not resolver evidence): %lld us, %llu profiling bucket lookups, %u slots\n", us, bucketLookups, iterations * 8);
    heaps.clear();
}
// Real registry records stay alive until process exit, exactly as required by
// the production TLS/global caches. No cache reset or retired pointer reuse.
static DescriptorHeapRecord &RegisterHeap(bool dense = false, UINT count = 8200, SIZE_T base = 0) {
    using namespace ResolverUnderTest;
    AcquireSRWLockExclusive(&gDescriptorLock);
    const size_t index = gDescriptorHeaps.size();
    gDescriptorHeaps.emplace_back();
    auto &h = gDescriptorHeaps.back();
    Init(h, base ? base : SIZE_T(0x10000000) + index * 0x100000, dense, count);
    gDescriptorHeapByCpuStart[h.cpuStart] = index;
    ReleaseSRWLockExclusive(&gDescriptorLock);
    return h;
}
static SIZE_T Handle(const DescriptorHeapRecord &h, UINT slot) { return h.cpuStart + SIZE_T(slot) * h.increment; }
static void CopyEight(DescriptorHeapRecord &s, DescriptorHeapRecord &d) {
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 8> src{}, dst{};
    for (UINT i = 0; i < 8; ++i) { src[i].ptr = Handle(s, i * 2); dst[i].ptr = Handle(d, i); }
    DX12ResourceTrackerHookRuntime::RecordCopiedDescriptors(nullptr, 8, dst.data(), nullptr, 8, src.data(), nullptr, Type);
}
static void RealApiCases(bool dense) {
    useRealResolver = true; hunt = false;
    auto &s = RegisterHeap(dense); auto &d = RegisterHeap(dense);
    for (UINT i = 0; i < 8; ++i) { Store(s, i * 2, Value(1, 100 + i)); Store(d, i, Value(1, 999)); }
    fullResolveCalls = 0; counters.clear();
    const auto before = d.stateLocks->ReadVersion();
    CopyEight(s, d);
    Check(fullResolveCalls == (HAS_DESCRIPTOR_COPY_HEAP_MEMO ? 9 : 16), "eight real blocks resolve exactly 9/destination-memo or 16/baseline");
    Check(counters["Desc.CopyBlocks"] == 8, "eight non-coalescible blocks");
    Check(d.stateLocks->ReadVersion() == before + 1, "one heap commit for eight changed blocks");
    for (UINT i = 0; i < 8; ++i) {
        CheckValue(*Slots::CurrentSlotPtr(d, i), Value(1, 100 + i), Handle(d, i));
        Check(d.stateLocks->PublishedSlotValueEquals(i, 100 + i), "each copied slot identity published");
    }
    fullResolveCalls = 0;
    CopyEight(s, d);
    Check(fullResolveCalls == (HAS_DESCRIPTOR_COPY_HEAP_MEMO ? 9 : 16), "next API starts with empty call memos");
    Check(d.stateLocks->ReadVersion() == before + 1, "unchanged API does not commit");
    fullResolveCalls = 0;
    DX12ResourceTrackerHookRuntime::RecordCopiedDescriptorsSimple(1, {Handle(d, 0)}, {Handle(s, 0)}, Type, 32);
    Check(fullResolveCalls == 2, "Simple uses two original resolutions");
    D3D12_CPU_DESCRIPTOR_HANDLE singleSrc{s.cpuStart}, singleDst{d.cpuStart};
    fullResolveCalls = 0; counters.clear();
    DX12ResourceTrackerHookRuntime::RecordCopiedDescriptors(nullptr, 1, &singleDst, nullptr, 1, &singleSrc, nullptr, Type);
    Check(fullResolveCalls == 2 && counters["Desc.CopyBlocks"] == 1, "multi API single range uses original two resolves");
    Check(d.stateLocks->ReadVersion() == before + 1, "single-range unchanged API does not commit");
    // One block spans a real chunk boundary, another revisits a different chunk.
    for (UINT i = 4094; i < 4099; ++i) { Store(s, i, Value(1, i)); Store(d, i, Value(1, 999)); }
    D3D12_CPU_DESCRIPTOR_HANDLE src[] = {{Handle(s, 4094)}, {Handle(s, 0)}};
    D3D12_CPU_DESCRIPTOR_HANDLE dst[] = {{Handle(d, 4094)}, {Handle(d, 20)}};
    UINT sizes[] = {5, 1};
    DX12ResourceTrackerHookRuntime::RecordCopiedDescriptors(nullptr, 2, dst, sizes, 2, src, sizes, Type);
    for (UINT i = 4094; i < 4099; ++i)
        CheckValue(*Slots::CurrentSlotPtr(d, i), Value(1, i), Handle(d, i));
    Check(d.stateLocks->PublishedSlotValueEquals(20, 100), "cross-chunk next block identity");
    useRealResolver = false;
}
static void RealMappingCases() {
    useRealResolver = true; hunt = false;
    std::array<DescriptorHeapRecord*, 8> sources{};
    auto &a = RegisterHeap(false, 32); auto &b = RegisterHeap(false, 32);
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 8> src{}, dst{};
    const UINT slots[] = {21, 3, 17, 9, 0, 31, 4, 1};
    for (UINT i = 0; i < 8; ++i) {
        sources[i] = &RegisterHeap(false, 32);
        Store(*sources[i], slots[i], Value(1, 300 + i));
        src[i].ptr = Handle(*sources[i], slots[i]);
        auto &d = i < 4 ? a : b;
        Store(d, i, Value(1, 999)); dst[i].ptr = Handle(d, i);
    }
    const auto av = a.stateLocks->ReadVersion(), bv = b.stateLocks->ReadVersion();
    fullResolveCalls = 0;
    DX12ResourceTrackerHookRuntime::RecordCopiedDescriptors(nullptr, 8, dst.data(), nullptr, 8, src.data(), nullptr, Type);
    Check(fullResolveCalls == (HAS_DESCRIPTOR_COPY_HEAP_MEMO ? 10 : 16), "eight changing sources, two destination heap runs");
    Check(a.stateLocks->ReadVersion() == av + 1 && b.stateLocks->ReadVersion() == bv + 1, "commit exactly once per switched destination heap");
    for (UINT i = 0; i < 8; ++i) {
        const auto &d = i < 4 ? a : b;
        CheckValue(*Slots::CurrentSlotPtr(d, i), Value(1, 300 + i), dst[i].ptr);
        Check(d.stateLocks->PublishedSlotValueEquals(i, 300 + i), "heap-switch destination slot identities");
    }
    // Revisit the same source handle and destination slot after a different
    // block; this is legal serial shadow bookkeeping, not overlapping D3D copy.
    D3D12_CPU_DESCRIPTOR_HANDLE repeatSrc[] = {src[0], src[1], src[0]};
    D3D12_CPU_DESCRIPTOR_HANDLE repeatDst[] = {{Handle(a, 15)}, {Handle(a, 14)}, {Handle(a, 13)}};
    DX12ResourceTrackerHookRuntime::RecordCopiedDescriptors(nullptr, 3, repeatDst, nullptr, 3, repeatSrc, nullptr, Type);
    Check(Slots::CurrentSlotPtr(a, 15)->valueId == 300 && Slots::CurrentSlotPtr(a, 14)->valueId == 301 && Slots::CurrentSlotPtr(a, 13)->valueId == 300, "API repeated source and descending destination handles");
    useRealResolver = false;
}
#if HAS_DESCRIPTOR_COPY_HEAP_MEMO
static DescriptorCopyCallScope NewScope(DescriptorCopyHeapMemo &memo, bool hunting = false) {
    DescriptorCopyCallScope scope;
    scope.heapType = Type; scope.increment = 32; scope.dumpOrHunt = hunting; scope.destinationMemo = &memo;
    return scope;
}
static void MemoBoundaryCases() {
    useRealResolver = true;
    auto &a = RegisterHeap(false, 32); auto &b = RegisterHeap(false, 32);
    DescriptorCopyHeapMemo memo;
    RealResolver::ResolvedHeapSlot r;
    fullResolveCalls = 0;
    const UINT order[] = {17, 3, 31, 0, 3, 29, 28, 27, 27, 1};
    for (UINT slot : order) {
        Check(memo.Resolve(Handle(a, slot), Type, &r) && r.heap == &a && r.slot == slot, "random/reverse/repeated slots recomputed");
    }
    Check(fullResolveCalls == 1, "random handles reuse only heap boundary");
    Check(memo.Resolve(Handle(a, 31) + 31, Type, &r) && r.slot == 31, "last range byte preserves original integer-slot semantics");
    Check(!memo.Resolve(Handle(a, 32), Type, &r) && !r.heap && !memo.entry.heap, "exclusive range tail clears memo/output");
    Check(!memo.Resolve(a.cpuStart - 1, Type, &r), "below range fails");
    Check(!memo.Resolve(0, Type, &r), "zero handle fails");
    Check(memo.Resolve(a.cpuStart, Type, &r), "failed then resolved");
    Check(!memo.Resolve(a.cpuStart, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, &r), "wrong required type rejected");
    Check(memo.Resolve(a.cpuStart, D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES, &r), "wildcard accepts CBV heap");
    Check(memo.Resolve(b.cpuStart, Type, &r) && r.heap == &b, "heap switch refreshes memo");
    Check(memo.Resolve(a.cpuStart, Type, &r) && r.heap == &a, "heap switch back refreshes memo");
    Check(!memo.Resolve(a.cpuStart, Type, nullptr), "null output rejected");
    auto &typed = RegisterHeap(false, 32); typed.desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    Check(memo.Resolve(typed.cpuStart, D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES, &r) && r.heap == &typed, "wildcard switches to RTV heap");
    Check(!memo.Resolve(typed.cpuStart, Type, &r), "wildcard fill does not bypass later type validation");
    // Serial retirement only: caller has stopped using descriptors. Metadata is
    // cleared like production retirement, stable record and stateLocks survive.
    Check(memo.Resolve(a.cpuStart, Type, &r), "prime old heap before retirement");
    const auto oldBase = a.cpuStart;
    AcquireSRWLockExclusive(&ResolverUnderTest::gDescriptorLock);
    a.stateLocks->Deactivate(); ResolverUnderTest::gDescriptorHeapByCpuStart.erase(oldBase);
    a.cpuStart = 0; a.increment = 0; a.desc = {};
    ReleaseSRWLockExclusive(&ResolverUnderTest::gDescriptorLock);
    Check(!memo.Resolve(oldBase, Type, &r), "serial retired range fails");
    auto &replacement = RegisterHeap(false, 32, oldBase);
    Check(memo.Resolve(oldBase, Type, &r) && r.heap == &replacement, "same CPU address resolves new stable record");
    // Repeat without the intervening failed resolve, exercising active rejection
    // on a still-populated call memo, sequential predictor, TLS and global cache.
    replacement.stateLocks->Deactivate(); replacement.cpuStart = 0; replacement.increment = 0; replacement.desc = {};
    auto &third = RegisterHeap(false, 32, oldBase);
    Check(memo.Resolve(oldBase, Type, &r) && r.heap == &third, "populated memo rejects retired identity on address reuse");
    useRealResolver = false;
}
static void MaterializeSerially(DescriptorHeapRecord &h) {
    // Deterministic test adapter, not the production materialization routine.
    // Gather chunk records before publishing nonempty vectors (slot reads prefer
    // vectors immediately). Do not model unsupported concurrent cleanup.
    std::vector<DescriptorRecord> dense(h.desc.NumDescriptors);
    std::vector<uint8_t> occupied(h.desc.NumDescriptors);
    for (UINT i = 0; i < h.desc.NumDescriptors; ++i) {
        if (Slots::CopyCurrentRecord(h, i, &dense[i])) occupied[i] = 1;
    }
    h.slots = std::move(dense); h.occupied = std::move(occupied);
    h.stateLocks->MarkMaterialized();
}
static void MemoBlockCases() {
    useRealResolver = true; hunt = false;
    auto &s = RegisterHeap(false, 64); auto &d = RegisterHeap(false, 64);
    Store(s, 0, Value(1, 11)); Store(d, 0, Value(1, 99));
    DescriptorCopyHeapMemo memos;
    auto scope = NewScope(memos);
    const auto before = d.stateLocks->ReadVersion();
    RecordCopiedDescriptorBlock(scope, 1, {d.cpuStart}, {s.cpuStart});
    Check(d.stateLocks->ReadVersion() == before && d.stateLocks->PublishedSlotValueEquals(0, 11), "identity immediate, heap commit deferred");
    auto changed = Value(1, 12); changed.staticReplacementGeneration = 98765;
    Store(s, 0, changed);
    RecordCopiedDescriptorBlock(scope, 1, {d.cpuStart}, {s.cpuStart});
    CheckValue(*Slots::CurrentSlotPtr(d, 0), changed, d.cpuStart);
    Check(d.stateLocks->PublishedSlotValueEquals(0, 12), "same-call mutation not content-cached");
    // A deterministic transition between blocks, not a simulated legal race.
    MaterializeSerially(s); MaterializeSerially(d);
    Store(s, 0, Value(1, 13));
    RecordCopiedDescriptorBlock(scope, 1, {d.cpuStart}, {s.cpuStart});
    Check(d.slots[0].valueId == 13, "cached heap rechecks materialization each block");
    FinishCopiedDescriptorCall(scope);
    Check(d.stateLocks->ReadVersion() == before + 1, "same-call writes commit destination once");
    auto &s2 = RegisterHeap(false, 64); auto &d2 = RegisterHeap(false, 64);
    Store(s2, 0, Value(1, 14));
    DescriptorCopyHeapMemo fallbackMemos;
    auto fallback = NewScope(fallbackMemos, true); hunt = true;
    RecordCopiedDescriptorBlock(fallback, 1, {d2.cpuStart}, {s2.cpuStart});
    Check(fallback.lockedPathCalls == 1 && Slots::CurrentSlotPtr(d2, 0)->valueId == 14, "hunt + missing chunk stays on fallback adapter");
    Store(s2, 0, Value(1, 15));
    RecordCopiedDescriptorBlock(fallback, 1, {d2.cpuStart}, {s2.cpuStart});
    Check(fallback.lockedPathCalls == 2 && d2.stateLocks->PublishedSlotValueEquals(0, 15), "memo does not cache hunt/content verdict");
    FinishCopiedDescriptorCall(fallback);
    hunt = false; useRealResolver = false;
}
#endif
static void RealResolverBenchmark(UINT sourceCount, bool simple, bool mixed, bool distinctSources = false, bool singleRange = false) {
    // A fresh worker gives every workload fresh function-local TLS caches;
    // stable registry records remain alive for the global cache afterwards.
    std::thread worker([=] {
        useRealResolver = true; hunt = false;
        // Identical virtual handles across revisions, independent of how many
        // memo-only correctness cases registered heaps earlier in this process.
        const SIZE_T base = SIZE_T(0x1000000000ull) + SIZE_T(sourceCount) * 0x100000000ull +
            SIZE_T(simple) * 0x10000000 + SIZE_T(mixed) * 0x20000000 + SIZE_T(distinctSources) * 0x40000000 +
            SIZE_T(singleRange) * 0x80000000;
        std::vector<DescriptorHeapRecord*> sources;
        for (UINT n = 0; n < sourceCount; ++n) {
            auto &s = RegisterHeap(false, 32, base + SIZE_T(n) * 0x100000); sources.push_back(&s);
            for (UINT i = 0; i < 8; ++i) Store(s, i * 2, Value(1, i + 1));
        }
        auto &d = RegisterHeap(false, 32, base + SIZE_T(sourceCount) * 0x100000);
        for (UINT i = 0; i < 8; ++i) Store(d, i, Value(1, i + 1));
        const UINT blocks = simple || singleRange ? 1 : 8;
        auto sourceFor = [&](UINT n, UINT block) -> DescriptorHeapRecord & {
            return *sources[(n * (distinctSources ? blocks : 1) + (distinctSources ? block : 0)) % sourceCount];
        };
        auto copy = [&](UINT n) {
            auto &s = sourceFor(n, 0);
            if (simple) DX12ResourceTrackerHookRuntime::RecordCopiedDescriptorsSimple(1, {d.cpuStart}, {s.cpuStart}, Type, 32);
            else if (singleRange) {
                D3D12_CPU_DESCRIPTOR_HANDLE src{s.cpuStart}, dst{d.cpuStart};
                DX12ResourceTrackerHookRuntime::RecordCopiedDescriptors(nullptr, 1, &dst, nullptr, 1, &src, nullptr, Type);
            } else if (!distinctSources) CopyEight(s, d);
            else {
                std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 8> src{}, dst{};
                for (UINT i = 0; i < 8; ++i) {
                    src[i].ptr = Handle(sourceFor(n, i), i * 2); dst[i].ptr = Handle(d, i);
                }
                DX12ResourceTrackerHookRuntime::RecordCopiedDescriptors(nullptr, 8, dst.data(), nullptr, 8, src.data(), nullptr, Type);
            }
        };
        auto prepareSource = [&](UINT n) {
            if (!mixed) return;
            // Exactly 20% changed, including source-heap switches. Setup is
            // timed identically in both revisions and explicitly labeled.
            for (UINT i = 0; i < blocks; ++i) {
                const UINT64 old = Slots::CurrentSlotPtr(d, i)->valueId;
                Store(sourceFor(n, i), i * 2, Value(1, old + ((n * blocks + i) % 5 == 0 ? 100 : 0)));
            }
        };
        for (UINT i = 0; i < sourceCount * 8; ++i) copy(i);
        // Diagnostic pass uses original production profiling sites. This observes
        // L2 hits and map misses without instrumenting individual TLS probes.
        counters.clear(); fullResolveCalls = 0; destinationL2Hits = 0; diagnosticDestination = &d;
        constexpr UINT diagnosticCalls = 4000;
        for (UINT i = 0; i < diagnosticCalls; ++i) { prepareSource(i); copy(i); }
        Check(counters["Rm.DescCopyUnchanged"] == LONG64(diagnosticCalls) * blocks * (mixed ? 4 : 5) / 5, "real diagnostic unchanged ratio exactly 80 or 100 percent");
        Check(counters["Rm.DescCopyChanged"] == (mixed ? LONG64(diagnosticCalls) * blocks / 5 : 0), "real diagnostic changed ratio exactly 20 or 0 percent");
        const UINT resolvesPerCall = simple || singleRange ? 2 : HAS_DESCRIPTOR_COPY_HEAP_MEMO ? 9 : 16;
        Check(fullResolveCalls == UINT64(diagnosticCalls) * resolvesPerCall, "real benchmark resolver count");
        const auto l2 = counters["Lock.Desc.HeapCacheHit"], misses = counters["Desc.ResolveMiss"];
        const auto destL2 = destinationL2Hits;
        diagnosticDestination = nullptr;
        constexpr UINT iterations = 100000;
        // Profile hash buckets AND test-only resolver counters are disabled
        // identically for both revisions during timing. The mock forwarding
        // branches remain, so timings are CPU harness evidence, not game FPS.
        collectCounters = false;
        auto start = std::chrono::steady_clock::now();
        for (UINT n = 0; n < iterations; ++n) { prepareSource(n); copy(n); }
        const auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
        collectCounters = true;
        Check(fullResolveCalls == UINT64(diagnosticCalls) * resolvesPerCall, "timed loop performed no resolver-counter writes");
        for (UINT i = 0; i < blocks; ++i)
            Check(d.stateLocks->PublishedSlotValueEquals(i, Slots::CurrentSlotPtr(d, i)->valueId), "real benchmark final identity");
        std::printf("real-resolver API %s %u-source-heaps %s %s: %lld us / %u calls; diagnostic full=%llu L2=%lld dest-L2=%llu map=%lld\n",
            simple ? "Simple1-block" : singleRange ? "Multi1-range1-block" : "8-block", sourceCount, distinctSources ? "distinct-per-block" : "same-per-call",
            mixed ? "80%-unchanged-mixed(includes-source-setup)" : "warm-unchanged", us, iterations,
            UINT64(diagnosticCalls) * resolvesPerCall, l2, destL2, misses);
        useRealResolver = false;
    });
    worker.join();
}
int main() {
    for (bool src : {false, true}) for (bool dst : {false, true}) SlotCases(src, dst);
    for (bool dense : {false, true}) for (bool hunting : {false, true}) HookCases(dense, hunting);
    for (bool dense : {false, true}) IndependentSlotWriters(dense);
    if (HAS_THREAD_SAFE_HEAP_CACHE) CacheCases();
    for (bool dense : {false, true}) RealApiCases(dense);
    RealMappingCases();
#if HAS_DESCRIPTOR_COPY_HEAP_MEMO
    MemoBoundaryCases(); MemoBlockCases();
    std::puts("PASS: call memo boundary/type/retirement/refresh, current-handle slot, mutation/materialization/hunt");
#else
    std::puts("SKIP: call memo-specific tests (source revision has no memo); baseline API/slot/resolver tests remain enabled");
#endif
    for (UINT sources : {1u, 40u}) for (bool mixed : {false, true}) {
        RealResolverBenchmark(sources, false, mixed);
        RealResolverBenchmark(sources, true, mixed);
        RealResolverBenchmark(sources, false, mixed, false, true);
    }
    for (UINT sources : {8u, 40u}) for (bool mixed : {false, true})
        RealResolverBenchmark(sources, false, mixed, true);
    Benchmark();
    CacheBenchmark();
    for (bool dense : {false, true}) ChangedSlotBenchmark(dense);
    std::puts("PASS: slot metadata/zero-id/missing/erase, sparse+dense, range mapping, hunt gate, ledger fallback, invalidation, independent-slot writers");
    std::puts(HAS_THREAD_SAFE_HEAP_CACHE ? "PASS: synchronized cache concurrent snapshots and retirement (source detected)" : "SKIP: this source snapshot lacks synchronized cache snapshots");
}
