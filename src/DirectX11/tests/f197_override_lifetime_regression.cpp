#include "slot_range_test_support.h"
#include "cache_policy_test_scope.h"
#include "lock.h"
#include "ResourceHash.h"

#include <crc32c.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

namespace {
using namespace dx11_slot_test;
using Microsoft::WRL::ComPtr;

class F197ConfigScope {
    UINT hunting = G->hunting;
    int track_updates = G->track_texture_updates;
    bool track_region = G->track_region_hashes;
    DataCacheBindFlags cache_policy = G->cache_resource_data.load();

public:
    F197ConfigScope()
    {
        G->hunting = 0;
        G->track_texture_updates = 0;
        G->track_region_hashes = false;
        G->cache_resource_data.store(DataCacheBindFlags::INVALID);
        clear();
    }

    ~F197ConfigScope()
    {
        clear();
        G->hunting = hunting;
        G->track_texture_updates = track_updates;
        G->track_region_hashes = track_region;
        G->cache_resource_data.store(cache_policy);
    }

    void clear()
    {
        CriticalSectionLockGuard lock(&G->mCriticalSection, __FUNCTION__, __LINE__);
        InvalidateTextureOverrideCandidates();
        G->mTextureOverrideMap.clear();
        G->mFuzzyTextureOverrides.clear();
    }

    void set(uint32_t hash, bool deny, bool expand)
    {
        CriticalSectionLockGuard lock(&G->mCriticalSection, __FUNCTION__, __LINE__);
        InvalidateTextureOverrideCandidates();
        auto& list = G->mTextureOverrideMap[hash];
        list.clear();
        list.resize(1);
        list.front().ini_section = L"F197";
        list.front().deny_cpu_read = deny;
        list.front().expand_region_copy = expand;
    }
};

ComPtr<ID3D11Texture2D> make_texture(BatchDevice& device, UINT colour, bool staging, UINT width = 8)
{
    const UINT height = 3;
    std::vector<UINT> pixels(static_cast<size_t>(width) * height, colour);

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.ArraySize = desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    D3D11_SUBRESOURCE_DATA initial = {};
    initial.pSysMem = pixels.data();
    initial.SysMemPitch = width * sizeof(UINT);

    ComPtr<ID3D11Texture2D> result;
    if (!staging) {
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        require_batch(SUCCEEDED(device.device->CreateTexture2D(&desc, &initial, &result)),
            "F197: create tracked default texture");
        return result;
    }

    // Staging textures cannot take initial data; create one, then copy a
    // populated native texture into it so the CPU-visible bytes are known.
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
    require_batch(SUCCEEDED(device.device->CreateTexture2D(&desc, nullptr, &result)),
        "F197: create tracked staging texture");
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;
    ComPtr<ID3D11Texture2D> source;
    require_batch(SUCCEEDED(device.native->CreateTexture2D(&desc, &initial, &source)),
        "F197: create native texture with known bytes");
    device.context->CopyResource(result.Get(), source.Get());
    return result;
}

std::vector<UINT> read_texture(BatchDevice& device, ID3D11Texture2D* texture)
{
    D3D11_TEXTURE2D_DESC desc = {};
    texture->GetDesc(&desc);
    std::vector<UINT> pixels(static_cast<size_t>(desc.Width) * desc.Height);

    if (desc.Usage == D3D11_USAGE_STAGING) {
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        require_batch(SUCCEEDED(device.context->Map(texture, 0, D3D11_MAP_READ, 0, &mapped)),
            "F197: map staging texture for readback");
        for (UINT y = 0; y < desc.Height; ++y)
            std::memcpy(&pixels[static_cast<size_t>(y) * desc.Width],
                static_cast<const unsigned char*>(mapped.pData) + mapped.RowPitch * y,
                desc.Width * sizeof(UINT));
        device.context->Unmap(texture, 0);
        return pixels;
    }

    D3D11_TEXTURE2D_DESC read_desc = desc;
    read_desc.Usage = D3D11_USAGE_STAGING;
    read_desc.BindFlags = 0;
    read_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    read_desc.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> readback;
    require_batch(SUCCEEDED(device.native->CreateTexture2D(&read_desc, nullptr, &readback)),
        "F197: create texture readback");
    device.context->CopyResource(readback.Get(), texture);

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    require_batch(SUCCEEDED(device.context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped)),
        "F197: map texture readback");
    for (UINT y = 0; y < desc.Height; ++y)
        std::memcpy(&pixels[static_cast<size_t>(y) * desc.Width],
            static_cast<const unsigned char*>(mapped.pData) + mapped.RowPitch * y,
            desc.Width * sizeof(UINT));
    device.context->Unmap(readback.Get(), 0);
    return pixels;
}

void test_map_deny_snapshot(BatchDevice& device, F197ConfigScope& config)
{
    auto texture = make_texture(device, 0x11223344, true, 1);
    const uint32_t hash = GetOrigResourceHash(texture.Get());

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    const UINT sentinel = 0xaabbccdd;

    config.set(hash, true, false);
    require_batch(SUCCEEDED(device.wrapper->Map(texture.Get(), 0, D3D11_MAP_READ, 0, &mapped)),
        "F197: deny map succeeds");
    require_batch(mapped.pData != nullptr, "F197: deny map returns a diversion buffer");
    *static_cast<UINT*>(mapped.pData) = sentinel;
    device.wrapper->Unmap(texture.Get(), 0);
    require_batch(read_texture(device, texture.Get())[0] == 0x11223344,
        "F197: deny_cpu_read diversion does not write back to the resource");

    config.set(hash, false, false);
    mapped = {};
    require_batch(SUCCEEDED(device.wrapper->Map(texture.Get(), 0, D3D11_MAP_READ, 0, &mapped)),
        "F197: normal read map succeeds");
    *static_cast<UINT*>(mapped.pData) = sentinel;
    device.wrapper->Unmap(texture.Get(), 0);
    require_batch(read_texture(device, texture.Get())[0] == sentinel,
        "F197: deny_cpu_read=false keeps the native map writable");

    config.clear();
    mapped = {};
    require_batch(SUCCEEDED(device.wrapper->Map(texture.Get(), 0, D3D11_MAP_READ, 0, &mapped)),
        "F197: map after config clear succeeds");
    *static_cast<UINT*>(mapped.pData) = sentinel;
    device.wrapper->Unmap(texture.Get(), 0);
    require_batch(read_texture(device, texture.Get())[0] == sentinel,
        "F197: empty override map reads no stale deny flag");
}

void test_map_deny_lock_scope(BatchDevice& device)
{
    auto texture = make_texture(device, 0x55667788, true, 1);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HRESULT result = E_FAIL;
    std::atomic<bool> gate{false};
    std::atomic<bool> started{false};
    std::atomic<bool> done{false};

    std::thread worker([&] {
        while (!gate.load())
            std::this_thread::yield();
        started.store(true);
        result = device.wrapper->Map(texture.Get(), 0, D3D11_MAP_READ, 0, &mapped);
        done.store(true);
    });

    bool completed_while_locked = false;
    {
        CriticalSectionGuard(&G->mCriticalSection);
        gate.store(true);
        while (!started.load())
            std::this_thread::yield();
        for (int i = 0; i < 100 && !done.load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        completed_while_locked = done.load();
    }
    worker.join();

    require_batch(SUCCEEDED(result), "F197: lock-scope map succeeds");
    require_batch(!completed_while_locked,
        "F197: MapDenyCPURead must take G before checking the empty override map");
    device.wrapper->Unmap(texture.Get(), 0);
}

void test_expand_region_copy_snapshot(BatchDevice& device, F197ConfigScope& config)
{
    auto source = make_texture(device, 0x12345678, false);
    const D3D11_BOX box = {0, 0, 0, 1, 1, 1};

    auto copy_and_read = [&](bool expand) {
        auto destination = make_texture(device, 0, false);
        const uint32_t hash = GetOrigResourceHash(destination.Get());
        config.set(hash, false, expand);
        device.wrapper->CopySubresourceRegion(destination.Get(), 0, 0, 0, 0,
            source.Get(), 0, &box);
        return read_texture(device, destination.Get());
    };

    auto expanded = copy_and_read(true);
    require_batch(expanded[0] == 0x12345678 && expanded[1] == 0x12345678 &&
        expanded[7] == 0x12345678,
        "F197: expand_region_copy=true copies the full destination width");

    auto unexpanded = copy_and_read(false);
    require_batch(unexpanded[0] == 0x12345678 && unexpanded[1] == 0 &&
        unexpanded[7] == 0,
        "F197: expand_region_copy=false preserves the original one-pixel box");

    config.clear();
    auto destination = make_texture(device, 0, false);
    device.wrapper->CopySubresourceRegion(destination.Get(), 0, 0, 0, 0,
        source.Get(), 0, &box);
    auto empty_map = read_texture(device, destination.Get());
    require_batch(empty_map[0] == 0x12345678 && empty_map[1] == 0,
        "F197: empty override map does not expand the region");
}

struct FakeBuffer;
struct FakeBufferVtbl {
    HRESULT(STDMETHODCALLTYPE* query_interface)(void*, REFIID, void**);
    ULONG(STDMETHODCALLTYPE* add_ref)(void*);
    ULONG(STDMETHODCALLTYPE* release)(void*);
};

struct FakeBuffer {
    FakeBufferVtbl* vtable;
    ULONG refs;
    unsigned adds;
    unsigned releases;
};

HRESULT STDMETHODCALLTYPE fake_query_interface(void*, REFIID, void**)
{
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE fake_add_ref(void* object)
{
    auto* buffer = static_cast<FakeBuffer*>(object);
    ++buffer->adds;
    return ++buffer->refs;
}

ULONG STDMETHODCALLTYPE fake_release(void* object)
{
    auto* buffer = static_cast<FakeBuffer*>(object);
    ++buffer->releases;
    return --buffer->refs;
}

FakeBufferVtbl fake_buffer_vtable = {
    &fake_query_interface,
    &fake_add_ref,
    &fake_release,
};

void test_com_ptr_keepalive(BatchDevice& device)
{
    FakeBuffer fake = {&fake_buffer_vtable, 1, 0, 0};
    auto* fake_buffer = reinterpret_cast<ID3D11Buffer*>(&fake);

    const float values[4] = {1.5f, 2.5f, 3.5f, 4.5f};
    auto* data = static_cast<unsigned char*>(malloc(sizeof(values)));
    require_batch(data != nullptr, "F197: allocate custom resource bytes");
    std::memcpy(data, values, sizeof(values));

    CustomResource resource;
    resource.InitializeHandleInfo(data, sizeof(values));

    // With a custom resource, GetRegionHash never dereferences `buffer`
    // except through the ComPtr lifetime guard. A three-slot IUnknown vtable
    // is therefore enough to observe the AddRef/Release deterministically.
    const uint32_t hash = GetRegionHash(device.wrapper.get(), fake_buffer, 0, 8, &resource);
    require_batch(hash == crc32c_hw(0, values, 8),
        "F197: custom-resource GetRegionHash output is unchanged");
    require_batch(fake.adds == 1 && fake.releases == 1 && fake.refs == 1,
        "F197: GetRegionHash holds exactly one ComPtr reference to the buffer");

    fake.adds = fake.releases = 0;
    uint32_t spatial_hash = 0;
    require_batch(TryGetSpatialHash(device.wrapper.get(), fake_buffer, 0, 4, 8, 1.0f,
        spatial_hash, &resource),
        "F197: custom-resource TryGetSpatialHash succeeds");
    require_batch(fake.adds == 1 && fake.releases == 1 && fake.refs == 1,
        "F197: TryGetSpatialHash holds exactly one ComPtr reference to the buffer");
}

void test_region_hash_legit_output(BatchDevice& device)
{
    DataCacheTestScope cache(DataCacheBindFlags::ALL_MASK);

    const float values[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = sizeof(values);
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA initial = {};
    initial.pSysMem = values;

    ComPtr<ID3D11Buffer> buffer;
    require_batch(SUCCEEDED(device.device->CreateBuffer(&desc, &initial, &buffer)),
        "F197: create tracked buffer for region hashes");

    const uint32_t expected = crc32c_hw(0, values, sizeof(values));
    ClearRegionHashesGlobalCache();
    require_batch(GetRegionHash(device.wrapper.get(), buffer.Get(), 0, sizeof(values)) == expected,
        "F197: GetRegionHash returns the real buffer bytes' CRC");
    require_batch(GetRegionHash(device.wrapper.get(), buffer.Get(), 0, sizeof(values)) == expected,
        "F197: repeated GetRegionHash remains stable");

    uint32_t spatial_hash = 0;
    require_batch(TryGetSpatialHash(device.wrapper.get(), buffer.Get(), 0, 4, 8, 1.0f,
        spatial_hash),
        "F197: TryGetSpatialHash reads finite float cells");
    require_batch(GetRegionHash(device.wrapper.get(), buffer.Get(), 0, sizeof(values)) == expected,
        "F197: spatial lookup does not change the region hash");
}

void run_f197_regression()
{
    BatchDevice device;
    F197ConfigScope config;
    test_map_deny_snapshot(device, config);
    test_map_deny_lock_scope(device);
    test_expand_region_copy_snapshot(device, config);
    test_com_ptr_keepalive(device);
    test_region_hash_legit_output(device);
}
} // namespace

void test_dx11_f197_override_lifetime_regression()
{
    try {
        run_f197_regression();
        std::fputs("PASS: F-197 override bool snapshots and region-hash buffer lifetime\n", stderr);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL F-197 override lifetime: %s\n", error.what());
        std::exit(EXIT_FAILURE);
    }
}
