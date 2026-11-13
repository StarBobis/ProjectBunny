#include "slot_range_test_support.h"
#include "cache_policy_test_scope.h"
#include <crc32c.h>

extern "C" const GUID ResourceReleaseTrackerGuid;

namespace {
struct WrappedCollisionHasher {
    size_t operator()(unsigned) const { return 63; }
};

void test_retired_key_erasure()
{
    using namespace dx11_slot_test;
    FlatHashMap<unsigned, unsigned, WrappedCollisionHasher> entries(64);
    for (unsigned key = 0; key < 25; ++key) entries.insert(key, key + 100);
    require_batch(entries.erase_if([](unsigned key, unsigned) { return key % 3 == 0; }) == 9,
        "round3 lifetime: erase matching keys across a wrapped collision cluster");
    for (unsigned key = 0; key < 25; ++key) {
        unsigned value = 0;
        require_batch(entries.find(key, value) == (key % 3 != 0) &&
            (key % 3 == 0 || value == key + 100),
            "round3 lifetime: retired keys absent and unrelated keys preserved");
    }
    require_batch(entries.size() == 16 &&
        entries.erase_if([](unsigned, unsigned) { return false; }) == 0 &&
        entries.erase_if([](unsigned, unsigned) { return true; }) == 16 && entries.size() == 0,
        "round3 lifetime: none/all predicate deletion retains correct population");
}
}

void test_dx11_round3_lifetime_regression()
{
    using namespace dx11_slot_test;
    test_retired_key_erasure();
    DataCacheTestScope policy;
    BatchDevice device;
    ClearRegionHashesGlobalCache();
    UINT values[16];
    for (UINT i = 0; i < 16; ++i) values[i] = i * 17 + 5;
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = sizeof(values);
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA initial{};
    initial.pSysMem = values;
    ComPtr<ID3D11Buffer> buffer;
    require_batch(SUCCEEDED(device.device->CreateBuffer(&desc, &initial, &buffer)),
        "round3 lifetime: create real tracked buffer");
    const UINT hash = GetRegionHash(device.wrapper.get(), buffer.Get(), 0, sizeof(values));
    require_batch(hash != 0, "round3 lifetime: populate actual L1/L2/L3 hash caches");
    require_batch(SUCCEEDED(buffer->SetPrivateDataInterface(ResourceReleaseTrackerGuid, nullptr)),
        "round3 lifetime: execute actual tracker destruction without allocator-dependent address reuse");
    {
        CriticalSectionGuard(&G->mResourcesLock);
        require_batch(G->mResources.count(buffer.Get()) == 0,
            "round3 lifetime: actual tracker destructor removed resource registration");
    }
    const UINT after = GetRegionHash(device.wrapper.get(), buffer.Get(), 0, sizeof(values));
    const auto retired_address = reinterpret_cast<uintptr_t>(buffer.Get());
    buffer.Reset();
    device.context->Flush();
    for (UINT i = 0; i < 16; ++i) values[i] = i * 23 + 101;
    bool reused = false;
    UINT replacement_hash = 0;
    for (UINT attempt = 0; attempt < 4096; ++attempt) {
        ComPtr<ID3D11Buffer> replacement;
        require_batch(SUCCEEDED(device.device->CreateBuffer(&desc, &initial, &replacement)),
            "round3 lifetime: create actual replacement buffer");
        if (reinterpret_cast<uintptr_t>(replacement.Get()) == retired_address) {
            reused = true;
            replacement_hash = GetRegionHash(device.wrapper.get(), replacement.Get(), 0, sizeof(values));
            const UINT expected = crc32c_hw(0, values, sizeof(values));
            std::fprintf(stderr, "round3 lifetime actual address reuse: previous=%08x replacement=%08x expected=%08x\n",
                hash, replacement_hash, expected);
            ClearRegionHashesGlobalCache();
            require_batch(replacement_hash == expected,
                "round3 lifetime: real replacement resource at recycled COM address must not reuse old hash");
            break;
        }
    }
    if (!reused) std::fputs("LIMITATION: WARP allocator did not reuse the retired address in4096 attempts; deterministic tracker destruction still tested\n", stderr);
    ClearRegionHashesGlobalCache();
    require_batch(after == 0,
        "round3 lifetime: tracker destruction must invalidate address-keyed L3 hash before identity can be reused");
}
