#include "slot_range_test_support.h"

void test_dx11_round8_pool_regression()
{
    using namespace dx11_slot_test;
    BatchDevice device;
    Registry registry;
    D3D11_TEXTURE3D_DESC description{};
    description.Width = 8;
    description.Height = 4;
    description.Depth = 2;
    description.MipLevels = 1;
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture3D> texture;
    require_batch(SUCCEEDED(device.native->CreateTexture3D(&description, nullptr, &texture)),
        "round8 pool: native 3D source exists");
    auto& source = registry.resource(L"resourceround8volume", device, texture.Get(), description.Format);
    auto& target = registry.resource(L"resourceround8depth", device);
    target.bind_flags = D3D11_BIND_SHADER_RESOURCE;
    target.override_height = 4;
    target.override_depth = 3;
    scalar(device, target.name, L"copy_desc " + source.name);
    require_batch(target.resource != nullptr, "round8 pool: overridden 3D allocation exists");
    ComPtr<ID3D11Texture3D> result;
    require_batch(SUCCEEDED(target.resource->QueryInterface(IID_PPV_ARGS(&result))),
        "round8 pool: allocation remains a 3D texture");
    D3D11_TEXTURE3D_DESC actual{};
    result->GetDesc(&actual);
    require_batch(actual.Height == 4 && actual.Depth == 3,
        "round8 pool: depth override changes depth and preserves configured height");
}
