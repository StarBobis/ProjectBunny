#include "slot_range_test_support.h"

namespace {
using namespace dx11_slot_test;
D3D11_TEXTURE2D_DESC texture_description(UINT width, UINT height, UINT mips, UINT slices, DXGI_FORMAT format)
{
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width; desc.Height = height; desc.MipLevels = mips; desc.ArraySize = slices;
    desc.Format = format; desc.SampleDesc.Count = 1; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    return desc;
}
}

void test_dx11_round7_descriptor_regression()
{
    using namespace dx11_slot_test;
    BatchDevice device;
    Registry registry;
    const auto first = texture_description(178,439,6,4,DXGI_FORMAT_R8G8_TYPELESS);
    const auto second = texture_description(915,667,2,16,DXGI_FORMAT_R16_TYPELESS);
    require_batch(crc32c_hw(0,&first,sizeof(first)) == crc32c_hw(0,&second,sizeof(second)),
        "round7 descriptors: two distinct descriptions share the cache lookup key");
    ComPtr<ID3D11Texture2D> a,b;
    require_batch(SUCCEEDED(device.native->CreateTexture2D(&first,nullptr,&a)) &&
        SUCCEEDED(device.native->CreateTexture2D(&second,nullptr,&b)),
        "round7 descriptors: both descriptions accepted by the actual native runtime");
    auto& source_a = registry.resource(L"resourceround7a",device,a.Get(),DXGI_FORMAT_R8G8_UNORM);
    auto& source_b = registry.resource(L"resourceround7b",device,b.Get(),DXGI_FORMAT_R16_FLOAT);
    auto& target = registry.resource(L"resourceround7target",device);
    target.bind_flags = D3D11_BIND_SHADER_RESOURCE;
    bool correct = true;
    for (unsigned step = 0; step < 3; ++step) {
        const auto& expected = step == 1 ? second : first;
        scalar(device,target.name,L"copy_desc " + (step == 1 ? source_b.name : source_a.name));
        require_batch(target.resource != nullptr,"round7 descriptors: destination allocation exists");
        ComPtr<ID3D11Texture2D> allocation;
        require_batch(SUCCEEDED(target.resource->QueryInterface(IID_PPV_ARGS(&allocation))),
            "round7 descriptors: actual allocation is a texture2D");
        D3D11_TEXTURE2D_DESC actual{};
        allocation->GetDesc(&actual);
        const bool same = std::memcmp(&actual,&expected,sizeof(actual)) == 0;
        std::fprintf(stderr,"round7 descriptor step%u: actual=%ux%u mips%u array%u format%u correct=%d\n",
            step,actual.Width,actual.Height,actual.MipLevels,actual.ArraySize,actual.Format,same);
        correct &= same;
    }
    require_batch(correct,"round7 descriptors: allocation identity includes the complete descriptor");
    target.override_mips = 0;
    scalar(device,target.name,L"copy_desc " + source_a.name);
    ComPtr<ID3D11Resource> automatic(target.resource);
    ComPtr<ID3D11Texture2D> automatic_texture;
    require_batch(automatic && SUCCEEDED(automatic.As(&automatic_texture)),
        "round7 descriptors: automatic mip allocation exists");
    D3D11_TEXTURE2D_DESC automatic_desc{};
    automatic_texture->GetDesc(&automatic_desc);
    require_batch(automatic_desc.MipLevels == 9,
        "round7 descriptors: native zero-mip request resolves to the full chain");
    scalar(device,target.name,L"copy_desc " + source_a.name);
    require_batch(target.resource == automatic.Get(),
        "round7 descriptors: normalized automatic mip description reuses the same allocation");
}
