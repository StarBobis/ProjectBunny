#include "CommandList.h"
#include "HackerContext.h"
#include "HackerDevice.h"
#include "ResourceHash.h"
#include "globals.h"
#include "cache_policy_test_scope.h"

#include <wrl/client.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

using Microsoft::WRL::ComPtr;

static void require_region(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL resource Region: %s\n", message);
        std::exit(EXIT_FAILURE);
    }
}

class RegionDevice
{
public:
    HMODULE runtime = nullptr;
    ComPtr<ID3D11Device1> native;
    ComPtr<ID3D11DeviceContext1> context;
    HackerDevice* device = nullptr;
    std::unique_ptr<HackerContext> wrapper;

    RegionDevice()
    {
        runtime = LoadLibraryExW(L"d3d11.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        require_region(runtime != nullptr, "load actual D3D11 runtime");
        auto create = reinterpret_cast<decltype(&D3D11CreateDevice)>(GetProcAddress(runtime, "D3D11CreateDevice"));
        require_region(create != nullptr, "resolve native device creator");
        ComPtr<ID3D11Device> base;
        ComPtr<ID3D11DeviceContext> immediate;
        require_region(SUCCEEDED(create(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &base, nullptr, &immediate)), "create real WARP device");
        require_region(SUCCEEDED(base.As(&native)) && SUCCEEDED(immediate.As(&context)), "query D3D11.1 interfaces");
        device = new HackerDevice(native.Get(), context.Get());
        native->AddRef();
        wrapper = std::make_unique<HackerContext>(native.Get(), context.Get());
        wrapper->SetHackerDevice(device);
        device->SetHackerContext(wrapper.get());
        device->SetHackerSwapChain(nullptr);
    }

    ~RegionDevice()
    {
        wrapper->ClearState();
        wrapper.reset();
        context.Reset();
        native.Reset();
        device->Release();
        FreeLibrary(runtime);
    }
};

class RegionGlobals
{
public:
    DataCacheTestScope cache_policy;
    const std::wstring alias_name = L"resourceregion_regression_alias";
    const bool saved_tracking = G->track_region_hashes;

    RegionGlobals()
    {
        require_region(customResources.count(alias_name) == 0, "region alias fixture has unique name");
        G->track_region_hashes = true;
        auto& alias = customResources[alias_name];
        alias.name = alias_name;
        alias.bind_flags = D3D11_BIND_CONSTANT_BUFFER;
    }

    ~RegionGlobals()
    {
        customResources.erase(alias_name);
        G->track_region_hashes = saved_tracking;
        ClearRegionHashesGlobalCache();
    }
};

static bool parse_region_command(CommandList& commands, const wchar_t* destination, const std::wstring& source)
{
    const std::wstring ini_namespace;
    CommandListScope scope;
    scope.emplace_front();
    commands.scope = &scope;
    std::wstring value = source;
    const bool parsed = ParseCommandListResourceCopyTargetDirective(L"CommandListRegionRegression", destination,
        &value, &commands, &ini_namespace);
    commands.scope = nullptr;
    return parsed;
}

static void run_region(RegionDevice& device, const wchar_t* destination, const std::wstring& source)
{
    CommandList commands;
    require_region(parse_region_command(commands, destination, source) && commands.commands.size() == 1,
        "parse actual Region resource-copy command");
    RunCommandList(device.device, device.wrapper.get(), &commands, nullptr, false);
}

static ComPtr<ID3D11Buffer> get_region_binding(RegionDevice& device, UINT slot, UINT& offset, UINT& count)
{
    ComPtr<ID3D11Buffer> buffer;
    device.context->CSGetConstantBuffers1(slot, 1, &buffer, &offset, &count);
    return buffer;
}

static void require_region_bytes(RegionDevice& device, ID3D11Buffer* buffer,
    const unsigned char* expected, UINT expected_size)
{
    require_region(buffer != nullptr, "copied destination is bound");
    D3D11_BUFFER_DESC desc = {};
    buffer->GetDesc(&desc);
    require_region(desc.ByteWidth == expected_size, "copy allocation matches selected region rather than underlying buffer");
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    desc.StructureByteStride = 0;
    ComPtr<ID3D11Buffer> staging;
    require_region(SUCCEEDED(device.native->CreateBuffer(&desc, nullptr, &staging)), "create full copied-buffer staging");
    device.context->CopyResource(staging.Get(), buffer);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    require_region(SUCCEEDED(device.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "map full copied-buffer contents");
    require_region(std::memcmp(mapped.pData, expected, expected_size) == 0, "every selected source byte copied exactly");
    device.context->Unmap(staging.Get(), 0);
}

void test_dx11_resource_region_regression()
{
    std::fputs("Testing absolute CB Region copies and reference windows\n", stderr);
    RegionDevice fixture;
    RegionGlobals globals;
    std::vector<unsigned char> bytes(2048);
    for (size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<unsigned char>((i * 37 + i / 256 * 19 + 11) & 255);
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = static_cast<UINT>(bytes.size());
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA initial = {};
    initial.pSysMem = bytes.data();
    ComPtr<ID3D11Buffer> source;
    // Use the real wrapper so its production resource metadata/release tracker
    // is populated, rather than manufacturing a cache entry in the test.
    require_region(SUCCEEDED(fixture.device->CreateBuffer(&desc, &initial, &source)), "create tracked real source CB");
    ID3D11Buffer* source_ptr = source.Get();
    UINT first = 32;
    UINT count = 16;
    fixture.context->VSSetConstantBuffers1(0, 1, &source_ptr, &first, &count);

    run_region(fixture, L"cs-cb0", L"copy vs-cb0");
    UINT actual_first = 0, actual_count = 0;
    auto copied = get_region_binding(fixture, 0, actual_first, actual_count);
    require_region(copied.Get() != source.Get(), "ordinary copy creates separate buffer");
    require_region_bytes(fixture, copied.Get(), bytes.data() + 512, 256);

    run_region(fixture, L"cs-cb1", L"copy vs-cb0->region(256,512)");
    copied = get_region_binding(fixture, 1, actual_first, actual_count);
    require_region_bytes(fixture, copied.Get(), bytes.data() + 256, 512);
    // Copy regions use byte offsets; only reference-binding windows require
    // 256-byte granularity. Both examples are valid non-window-aligned copies.
    run_region(fixture, L"cs-cb2", L"copy vs-cb0->region(20,32)");
    copied = get_region_binding(fixture, 2, actual_first, actual_count);
    require_region_bytes(fixture, copied.Get(), bytes.data() + 20, 32);
    run_region(fixture, L"cs-cb2", L"copy vs-cb0->region(3,16)");
    copied = get_region_binding(fixture, 2, actual_first, actual_count);
    require_region_bytes(fixture, copied.Get(), bytes.data() + 3, 16);

    run_region(fixture, L"cs-cb3", L"ref vs-cb0->region(256,512)");
    auto referenced = get_region_binding(fixture, 3, actual_first, actual_count);
    require_region(referenced.Get() == source.Get() && actual_first == 16 && actual_count == 32,
        "reference binds same physical buffer at absolute 256-byte start and 512-byte window");

    const uint32_t expected_hash = crc32c_hw(0, bytes.data() + 256, 512);
    require_region(GetRegionHash(fixture.wrapper.get(), source.Get(), 256, 512) == expected_hash,
        "source snapshot uses real readback and exact selected bytes");
    run_region(fixture, globals.alias_name.c_str(), L"ref vs-cb0->region(256,512)");
    auto& alias = customResources.at(globals.alias_name);
    require_region(alias.resource == source.Get() && alias.offset == 256 && alias.buf_size == 512,
        "custom reference alias preserves exact region metadata");
    require_region(alias.GetHandleInfo() != nullptr &&
        GetRegionHash(fixture.wrapper.get(), source.Get(), 0, 512, &alias) == expected_hash,
        "custom alias hash uses selected bytes without double-applying offset");
    run_region(fixture, L"cs-cb4", L"ref resourceregion_regression_alias->region(512,256)");
    referenced = get_region_binding(fixture, 4, actual_first, actual_count);
    require_region(referenced.Get() == source.Get() && actual_first == 32 && actual_count == 16,
        "Region on custom alias remains absolute to underlying buffer");
    run_region(fixture, globals.alias_name.c_str(), L"ref vs-cb0->region(512,256)");
    require_region(alias.offset == 512 && alias.buf_size == 256 &&
        GetRegionHash(fixture.wrapper.get(), source.Get(), 0, 256, &alias) ==
            crc32c_hw(0, bytes.data() + 512, 256), "rebinding alias updates metadata and invalidates its old hash window");

    run_region(fixture, L"cs-cb5", L"copy vs-cb0->region(1792,256)");
    copied = get_region_binding(fixture, 5, actual_first, actual_count);
    require_region_bytes(fixture, copied.Get(), bytes.data() + 1792, 256);
    run_region(fixture, L"cs-cb6", L"ref vs-cb0->region(1792,256)");
    referenced = get_region_binding(fixture, 6, actual_first, actual_count);
    require_region(referenced.Get() == source.Get() && actual_first == 112 && actual_count == 16,
        "last valid reference window ends at physical buffer boundary");

    // SetConstantBuffers1 permits windows extending past the allocation;
    // only the intersecting allocation is visible. Copy boxes cannot do this.
    run_region(fixture, L"cs-cb6", L"ref vs-cb0->region(1792,512)");
    referenced = get_region_binding(fixture, 6, actual_first, actual_count);
    require_region(referenced.Get() == source.Get() && actual_first == 112 && actual_count == 32,
        "legal reference window may extend past the physical buffer end");
    run_region(fixture, L"cs-cb6", L"ref vs-cb0->region(0,65536)");
    referenced = get_region_binding(fixture, 6, actual_first, actual_count);
    require_region(referenced.Get() == source.Get() && actual_first == 0 && actual_count == 4096,
        "maximum legal shader reference window is preserved");

    for (const wchar_t* region : {L"(0,0)", L"(-1,256)", L"(nan,256)", L"(inf,256)",
        L"(0,nan)", L"(0,inf)", L"(4294967296,256)", L"(0,65792)"}) {
        for (const wchar_t* option : {L"copy ", L"ref "}) {
            first = 0; count = 16;
            fixture.context->CSSetConstantBuffers1(7, 1, &source_ptr, &first, &count);
            CommandList commands;
            const std::wstring text = std::wstring(option) + L"vs-cb0->region" + region;
            if (parse_region_command(commands, L"cs-cb7", text))
                RunCommandList(fixture.device, fixture.wrapper.get(), &commands, nullptr, false);
            auto unchanged = get_region_binding(fixture, 7, actual_first, actual_count);
            require_region(unchanged.Get() == source.Get() && actual_first == 0 && actual_count == 16,
                "invalid region leaves existing destination resource and window unchanged");
        }
    }
    for (const wchar_t* region : {L"(2048,256)", L"(1792,512)", L"(0,4096)"}) {
        first = 0; count = 16;
        fixture.context->CSSetConstantBuffers1(7, 1, &source_ptr, &first, &count);
        CommandList commands;
        const std::wstring text = std::wstring(L"copy vs-cb0->region") + region;
        if (parse_region_command(commands, L"cs-cb7", text))
            RunCommandList(fixture.device, fixture.wrapper.get(), &commands, nullptr, false);
        auto unchanged = get_region_binding(fixture, 7, actual_first, actual_count);
        require_region(unchanged.Get() == source.Get() && actual_first == 0 && actual_count == 16,
            "copy region beyond allocation fails although corresponding reference window is legal");
    }
    for (const wchar_t* region : {L"(16,256)", L"(256,16)", L"(20,32)"}) {
        first = 0; count = 16;
        fixture.context->CSSetConstantBuffers1(7, 1, &source_ptr, &first, &count);
        CommandList commands;
        const std::wstring text = std::wstring(L"ref vs-cb0->region") + region;
        if (parse_region_command(commands, L"cs-cb7", text))
            RunCommandList(fixture.device, fixture.wrapper.get(), &commands, nullptr, false);
        auto unchanged = get_region_binding(fixture, 7, actual_first, actual_count);
        require_region(unchanged.Get() == source.Get() && actual_first == 0 && actual_count == 16,
            "misaligned CB reference window is rejected without dropping existing binding");
    }
    ComPtr<ID3D11Buffer> original_binding;
    fixture.context->VSGetConstantBuffers1(0, 1, &original_binding, &actual_first, &actual_count);
    require_region(original_binding.Get() == source.Get() && actual_first == 32 && actual_count == 16,
        "copy and reference overrides preserve the original source binding window");
    fixture.context->ClearState();
    fixture.context->Flush();
}
