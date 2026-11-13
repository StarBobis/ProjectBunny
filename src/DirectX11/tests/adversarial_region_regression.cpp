#include "slot_range_test_support.h"

namespace {
using namespace dx11_slot_test;

ComPtr<ID3D11Buffer> current_cb(BatchDevice& device)
{
    ComPtr<ID3D11Buffer> result;
    device.context->CSGetConstantBuffers(0, 1, &result);
    return result;
}
}

void test_dx11_adversarial_region_regression()
{
    using namespace dx11_slot_test;
    BatchDevice device;
    auto source = buffer(device, 131072, D3D11_BIND_VERTEX_BUFFER);
    auto sentinel = buffer(device, 256, D3D11_BIND_CONSTANT_BUFFER);
    ID3D11Buffer* source_ptr = source.Get();
    const UINT stride = 4, offset = 0;
    device.context->IASetVertexBuffers(0, 1, &source_ptr, &stride, &offset);
    ID3D11Buffer* sentinel_ptr = sentinel.Get();
    device.context->CSSetConstantBuffers(0, 1, &sentinel_ptr);
    scalar(device, L"cs-cb0", L"copy vb0->region(0,131072)");
    auto copied = current_cb(device);
    require_batch(copied && copied.Get() != sentinel.Get() && copied.Get() != source.Get(),
        "adversarial Region: upstream legal large Region copy replaces sentinel with independent clamped CB");
    D3D11_BUFFER_DESC desc{};
    copied->GetDesc(&desc);
    require_batch(desc.ByteWidth == 65536, "adversarial Region: CB copy clamps allocation to 64KiB");
    desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = desc.MiscFlags = desc.StructureByteStride = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Buffer> staging;
    require_batch(SUCCEEDED(device.native->CreateBuffer(&desc, nullptr, &staging)), "adversarial Region: real staging");
    device.context->CopyResource(staging.Get(), copied.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    require_batch(SUCCEEDED(device.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "adversarial Region: real readback");
    bool exact = true;
    for (UINT i = 0; i < desc.ByteWidth; ++i)
        exact &= static_cast<const unsigned char*>(mapped.pData)[i] == static_cast<unsigned char>(i * 37 + 11);
    device.context->Unmap(staging.Get(), 0);
    require_batch(exact, "adversarial Region: every selected clamped byte matches source");
    scalar(device, L"cs-cb0", L"ref vb0->region(0,131072)");
    require_batch(current_cb(device).Get() == copied.Get(), "adversarial Region: overlarge CB ref remains rejected");
    scalar(device, L"cs-cb0", L"copy vb0->region(131072,16)");
    require_batch(current_cb(device).Get() == copied.Get(), "adversarial Region: physically invalid copy remains rejected");
    device.context->ClearState();
}
