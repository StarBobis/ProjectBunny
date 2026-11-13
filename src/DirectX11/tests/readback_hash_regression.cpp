#include "CommandList.h"
#include "HackerContext.h"
#include "globals.h"
#include "cache_policy_test_scope.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static void require_readback_hash(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL readback hash: %s\n", message);
        std::exit(EXIT_FAILURE);
    }
}

void test_dx11_readback_hash_regression(CommandListState& state)
{
    DataCacheTestScope cache_policy;
    auto* context = state.mHackerContext;
    auto* native = state.mOrigContext1;
    auto* staging = context->GetReadbackBuffer(4100);
    require_readback_hash(staging != nullptr, "acquire shared staging bucket");
    D3D11_BUFFER_DESC bucket = {};
    staging->GetDesc(&bucket);
    require_readback_hash(bucket.ByteWidth == 8192, "non-power-of-two source uses larger bucket");

    std::vector<uint32_t> first(1025), second(1026);
    for (size_t i = 0; i < first.size(); ++i)
        first[i] = static_cast<uint32_t>(i * 31 + 17);
    for (size_t i = 0; i < second.size(); ++i)
        second[i] = static_cast<uint32_t>(i * 47 + 29);
    D3D11_BUFFER_DESC desc = {};
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA initial = {};
    initial.pSysMem = first.data();
    desc.ByteWidth = static_cast<UINT>(first.size() * sizeof(uint32_t));
    ID3D11Buffer* one = nullptr;
    require_readback_hash(SUCCEEDED(state.mOrigDevice1->CreateBuffer(&desc, &initial, &one)), "first source buffer");
    initial.pSysMem = second.data();
    desc.ByteWidth = static_cast<UINT>(second.size() * sizeof(uint32_t));
    ID3D11Buffer* two = nullptr;
    require_readback_hash(SUCCEEDED(state.mOrigDevice1->CreateBuffer(&desc, &initial, &two)), "second source buffer");
    G->mResources[one];
    G->mResources[two];
    ClearRegionHashesGlobalCache();
    const uint32_t first_hash = crc32c_hw(0, first.data(), first.size() * sizeof(uint32_t));
    require_readback_hash(GetRegionHash(context, one, 0, 4100) == first_hash,
        "copy only source bytes into larger staging bucket");
    require_readback_hash(GetRegionHash(context, two, 0, 4104) ==
        crc32c_hw(0, second.data(), second.size() * sizeof(uint32_t)), "same bucket reads distinct source contents");
    require_readback_hash(context->GetReadbackBuffer(4104) == staging, "hashing reuses context-owned staging");
    require_readback_hash(std::memcmp(G->mResources[one].GetCachedData(), first.data(), 4100) == 0,
        "CPU snapshot survives staging reuse by another resource");

    first.back() ^= 0xa5a55a5au;
    native->UpdateSubresource(one, 0, nullptr, first.data(), 0, 0);
    G->mResources[one].ClearDataCache();
    ClearRegionHashesGlobalCache();
    require_readback_hash(GetRegionHash(context, one, 0, 4100) ==
        crc32c_hw(0, first.data(), 4100), "invalidated snapshot refreshes final source bytes");

    HMODULE runtime = LoadLibraryExW(L"d3d11.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    require_readback_hash(runtime != nullptr, "load system runtime for foreign-device rejection");
    auto create = reinterpret_cast<decltype(&D3D11CreateDevice)>(GetProcAddress(runtime, "D3D11CreateDevice"));
    require_readback_hash(create != nullptr, "resolve foreign-device creation");
    ID3D11Device* foreign_device = nullptr;
    ID3D11DeviceContext* foreign_native = nullptr;
    require_readback_hash(SUCCEEDED(create(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &foreign_device, nullptr, &foreign_native)), "foreign WARP device");
    ID3D11Device1* foreign_device1 = nullptr;
    ID3D11DeviceContext1* foreign_native1 = nullptr;
    require_readback_hash(SUCCEEDED(foreign_device->QueryInterface(IID_PPV_ARGS(&foreign_device1))) &&
        SUCCEEDED(foreign_native->QueryInterface(IID_PPV_ARGS(&foreign_native1))), "foreign D3D11.1 interfaces");
    {
        HackerContext foreign(foreign_device1, foreign_native1);
        G->mResources[one].ClearDataCache();
        ClearRegionHashesGlobalCache();
        require_readback_hash(GetRegionHash(&foreign, one, 0, 4100) == 0 && !G->mResources[one].cached_data,
            "wrong-device context cannot publish stale staging contents");
    }
    foreign_native1->Release();
    foreign_device1->Release();
    foreign_native->Release();
    foreign_device->Release();
    FreeLibrary(runtime);

    // A deferred context cannot perform synchronous read maps. Failure must
    // leave the cache unpublished and allow a later immediate-context retry.
    ID3D11DeviceContext1* deferred_native = nullptr;
    require_readback_hash(SUCCEEDED(state.mOrigDevice1->CreateDeferredContext1(0, &deferred_native)), "deferred context");
    {
        HackerContext deferred(state.mOrigDevice1, deferred_native);
        G->mResources[one].ClearDataCache();
        ClearRegionHashesGlobalCache();
        require_readback_hash(GetRegionHash(&deferred, one, 0, 4100) == 0 &&
            !G->mResources[one].cached_data, "failed deferred readback does not publish a snapshot");
        uint32_t spatial = 0;
        require_readback_hash(!TryGetSpatialHash(&deferred, one, 0, 4, 8, 1, spatial),
            "spatial hash shares deferred readback failure behavior");
    }
    deferred_native->Release();
    require_readback_hash(GetRegionHash(context, one, 0, 4100) == crc32c_hw(0, first.data(), 4100),
        "immediate retry after failed readback succeeds");
    G->mResources.erase(one);
    G->mResources.erase(two);
    ClearRegionHashesGlobalCache();
    require_readback_hash(one->Release() == 0 && two->Release() == 0, "no source references retained by pooled readback");
}
