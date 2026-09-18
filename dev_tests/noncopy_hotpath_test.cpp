// Production scratch pool and readback compaction bodies are extracted by the
// runner. Payload/COM types are doubles: these tests prove CPU ownership/order,
// not GPU synchronization or game-frame performance.
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
using UINT = unsigned int;
#define ARRAYSIZE(a) (sizeof(a) / sizeof((a)[0]))
namespace Bunny { using CommandListTarget = unsigned; }
using DX12TextureOverrideMatch = unsigned;
using DX12CurrentShaderResourceBinding = unsigned;
using DX12ResolvedGraphicsSrvBinding = unsigned;
using DX12TextureOverrideTargetCacheKey = unsigned;
using DX12TextureOverrideTargetCacheKeyHash = std::hash<unsigned>;
class DX12ReadbackEvent {
public:
    void *resource = nullptr;
    UINT id = 0;
};
#include "noncopy_under_test.h"
static void Check(bool value, const char *message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static void TestPool() {
    std::array<std::unique_ptr<DX12CommandListExecutorScratch>, 20> fallbacks;
    std::array<DX12CommandListExecutorScratch *, 20> slots{};
    std::array<bool, 20> pooled{};
    for (UINT i = 0; i < slots.size(); ++i) {
        slots[i] = DX12CommandListExecutor::AcquireScratch(&pooled[i], &fallbacks[i]);
        Check(pooled[i] == (i < 18), "depth selects pool or fallback");
        Check(static_cast<bool>(fallbacks[i]) == (i >= 18), "pooled path never constructs fallback containers");
        slots[i]->commandListStack.push_back(std::to_wstring(i));
        slots[i]->targetTextureOverrideCache[i] = {i};
        for (UINT previous = 0; previous < i; ++previous)
            Check(slots[previous] != slots[i], "nested executors never alias scratch");
    }
    for (UINT i = static_cast<UINT>(slots.size()); i-- > 0;) {
        Check(slots[i]->commandListStack.front() == std::to_wstring(i), "nested scratch retains its owner data");
        slots[i]->Reset();
        if (pooled[i]) --DX12CommandListExecutor::ScratchDepth;
    }
    Check(DX12CommandListExecutor::ScratchDepth == 0, "fallback release does not decrement pool depth");
    bool pooledAgain = false;
    std::unique_ptr<DX12CommandListExecutorScratch> unused;
    auto *reused = DX12CommandListExecutor::AcquireScratch(&pooledAgain, &unused);
    Check(reused == slots[0] && pooledAgain && !unused, "pool reused without fallback allocation");
    Check(reused->targetTextureOverrideCache.empty() && reused->commandListStack.empty(), "reuse cannot observe old verdicts");
    reused->Reset(); --DX12CommandListExecutor::ScratchDepth;
    std::puts("PASS production scratch: 18 pooled + 2 overflow, distinct ownership, clean reuse, lazy fallback");
}
static void TestCompaction() {
    for (UINT count : {0u, 1u, 64u, 4096u}) {
        for (UINT mode = 0; mode < 4; ++mode) {
            std::vector<DX12ReadbackEvent> events;
            std::vector<UINT> expected;
            for (UINT i = 0; i < count; ++i) {
                const bool keep = mode == 0 || (mode == 2 && i % 2) || (mode == 3 && i >= count / 2);
                events.push_back({keep ? reinterpret_cast<void *>(static_cast<size_t>(i) + 1) : nullptr, i});
                if (keep) expected.push_back(i);
            }
            auto removed = DX12ReadbackCompactRecordedEvents(events);
            Check(removed == count - expected.size() && events.size() == expected.size(), "exact skip accounting");
            for (size_t i = 0; i < expected.size(); ++i)
                Check(events[i].id == expected[i] && events[i].resource, "stable order and ownership of every survivor");
        }
    }
    std::puts("PASS production readback compaction: empty/all-valid/all-failed/mixed, stable order, up to 4096 events");
}
int main() {
    TestPool(); TestCompaction();
    std::puts("ALL PASS; production wiring/publication source invariants checked by runner");
}
