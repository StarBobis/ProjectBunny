#include "HackerContext.h"
#include "ResourceHash.h"

#include <wrl/client.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

using Microsoft::WRL::ComPtr;

static void require_readback(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL readback pool: %s\n", message);
        std::exit(EXIT_FAILURE);
    }
}

struct ReadbackCollisionHasher
{
    size_t operator()(unsigned) const { return 1; }
};

static void test_readback_map_iteration()
{
    FlatHashMap<unsigned, unsigned, ReadbackCollisionHasher> entries(0);
    require_readback(entries.capacity() >= 2, "minimum map capacity retained");
    unsigned visits = 0;
    entries.for_each([&](unsigned, unsigned) { ++visits; });
    require_readback(visits == 0, "empty map has no active entries");
    for (unsigned key = 0; key < 20; ++key)
        entries.insert(key, key * 2);
    entries.insert(3, 100);
    require_readback(entries.erase(0) && entries.erase(7) && !entries.erase(99), "colliding erase paths");
    unsigned observed[20] = {};
    entries.for_each([&](unsigned key, unsigned& value) {
        require_readback(key < 20 && key != 0 && key != 7, "only live keys visited");
        require_readback(++observed[key] == 1, "each active key visited exactly once");
        require_readback(value == (key == 3 ? 100 : key * 2), "updated values visited");
        ++value;
        ++visits;
    });
    require_readback(visits == 18, "all live entries visited after rehash and erase");
    const auto& immutable = entries;
    require_readback(immutable.find_ptr(3) && *immutable.find_ptr(3) == 101 &&
        immutable.find_ptr(7) == nullptr, "const find_ptr and callback value references retained");
    entries.clear();
    visits = 0;
    entries.for_each([&](unsigned, unsigned) { ++visits; });
    require_readback(visits == 0, "clear excludes previous generation entries");
    entries.insert(42, 99);
    entries.for_each([&](unsigned key, unsigned value) {
        require_readback(key == 42 && value == 99, "only new generation visited");
        ++visits;
    });
    require_readback(visits == 1, "map reusable after generation clear");
}

static void verify_readback_descriptor(ID3D11Buffer* buffer, UINT expected_size)
{
    require_readback(buffer != nullptr, "create readback bucket");
    D3D11_BUFFER_DESC desc = {};
    buffer->GetDesc(&desc);
    require_readback(desc.ByteWidth == expected_size && desc.Usage == D3D11_USAGE_STAGING &&
        desc.CPUAccessFlags == D3D11_CPU_ACCESS_READ && desc.BindFlags == 0 &&
        desc.MiscFlags == 0 && desc.StructureByteStride == 0, "staging descriptor is CPU-readable and unbound");
}

void test_dx11_readback_pool_regression()
{
    std::fputs("Testing readback pool and FlatHashMap iteration\n", stderr);
    test_readback_map_iteration();
    HMODULE runtime = LoadLibraryExW(L"d3d11.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    require_readback(runtime != nullptr, "load actual system D3D11 runtime");
    const auto create_device = reinterpret_cast<decltype(&D3D11CreateDevice)>(
        GetProcAddress(runtime, "D3D11CreateDevice"));
    require_readback(create_device != nullptr, "resolve actual device creator");
    {
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> native;
        require_readback(SUCCEEDED(create_device(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
            nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &native)), "create actual WARP device");
        ComPtr<ID3D11Device1> device1;
        ComPtr<ID3D11DeviceContext1> native1;
        require_readback(SUCCEEDED(device.As(&device1)) && SUCCEEDED(native.As(&native1)), "query D3D11.1 interfaces");

        ID3D11Buffer* retained[4] = {};
        {
            HackerContext owner(device1.Get(), native1.Get());
            require_readback(owner.GetReadbackBuffer(0) == nullptr &&
                owner.GetReadbackBuffer((std::numeric_limits<UINT>::max)()) == nullptr &&
                owner.GetReadbackBuffer((std::numeric_limits<UINT>::max)()) == nullptr,
                "zero and overflowing buckets fail without caching invalid buffers");
            auto* one = owner.GetReadbackBuffer(1);
            auto* four = owner.GetReadbackBuffer(4);
            auto* eight = owner.GetReadbackBuffer(5);
            auto* sixteen = owner.GetReadbackBuffer(9);
            verify_readback_descriptor(one, 1);
            verify_readback_descriptor(four, 4);
            verify_readback_descriptor(eight, 8);
            verify_readback_descriptor(sixteen, 16);
            require_readback(one != four && four != eight && eight != sixteen &&
                owner.GetReadbackBuffer(7) == eight && owner.GetReadbackBuffer(8) == eight &&
                owner.GetReadbackBuffer(4) == four, "bucket rounding and reuse");

            ComPtr<ID3D11DeviceContext1> deferred;
            require_readback(SUCCEEDED(device1->CreateDeferredContext1(0, &deferred)), "create independent deferred context");
            HackerContext other_context(device1.Get(), deferred.Get());
            auto* other_bucket = other_context.GetReadbackBuffer(5);
            verify_readback_descriptor(other_bucket, 8);
            require_readback(other_bucket != eight, "readback buckets belong to individual contexts");

            ComPtr<ID3D11Device> second_device;
            ComPtr<ID3D11DeviceContext> second_native;
            require_readback(SUCCEEDED(create_device(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
                nullptr, 0, D3D11_SDK_VERSION, &second_device, nullptr, &second_native)), "create second actual WARP device");
            ComPtr<ID3D11Device1> second_device1;
            ComPtr<ID3D11DeviceContext1> second_native1;
            require_readback(SUCCEEDED(second_device.As(&second_device1)) &&
                SUCCEEDED(second_native.As(&second_native1)), "query second D3D11.1 interfaces");
            HackerContext other_device(second_device1.Get(), second_native1.Get());
            auto* second_bucket = other_device.GetReadbackBuffer(5);
            verify_readback_descriptor(second_bucket, 8);
            ComPtr<ID3D11Device> bucket_device;
            second_bucket->GetDevice(&bucket_device);
            require_readback(second_bucket != eight && bucket_device.Get() == second_device.Get(),
                "bucket allocation uses its own device");

            unsigned char bytes[5] = {1, 2, 3, 4, 5};
            D3D11_BUFFER_DESC desc = {};
            desc.ByteWidth = sizeof(bytes);
            desc.Usage = D3D11_USAGE_DEFAULT;
            D3D11_SUBRESOURCE_DATA initial = {};
            initial.pSysMem = bytes;
            ComPtr<ID3D11Buffer> source;
            require_readback(SUCCEEDED(device->CreateBuffer(&desc, &initial, &source)), "create non-power-of-two source");
            D3D11_BOX box = {0, 0, 0, sizeof(bytes), 1, 1};
            for (unsigned iteration = 0; iteration < 2; ++iteration) {
                if (iteration) {
                    std::memset(bytes, 0xA7, sizeof(bytes));
                    native->UpdateSubresource(source.Get(), 0, nullptr, bytes, 0, 0);
                }
                native->CopySubresourceRegion(eight, 0, 0, 0, 0, source.Get(), 0, &box);
                D3D11_MAPPED_SUBRESOURCE mapped = {};
                require_readback(SUCCEEDED(native->Map(eight, 0, D3D11_MAP_READ, 0, &mapped)), "map borrowed readback bucket");
                require_readback(std::memcmp(mapped.pData, bytes, sizeof(bytes)) == 0,
                    "copy only valid source range and refresh reused staging contents");
                native->Unmap(eight, 0);
            }
            native->Flush();
            retained[0] = one;
            retained[1] = four;
            retained[2] = eight;
            retained[3] = sixteen;
            for (auto* buffer : retained)
                buffer->AddRef();
        }
        // These references keep the resources alive while their pool owner is
        // destroyed. With its single reference released, ours are the last.
        for (auto* buffer : retained)
            require_readback(buffer->Release() == 0, "context destruction releases all pooled buffer references");
    }
    FreeLibrary(runtime);
}
