#include "slot_range_test_support.h"

namespace {
using namespace dx11_slot_test;

ComPtr<ID3D11Buffer> current_constant(BatchDevice& device)
{
    ComPtr<ID3D11Buffer> result;
    device.context->CSGetConstantBuffers(0, 1, &result);
    return result;
}

void require_copy_prefix(BatchDevice& device, ID3D11Buffer* resource,
    UINT allocation_size, UINT source_offset, UINT copied_bytes)
{
    require_batch(resource != nullptr, "round2 Region clamp: destination exists");
    D3D11_BUFFER_DESC desc{};
    resource->GetDesc(&desc);
    require_batch(desc.ByteWidth == allocation_size && copied_bytes <= allocation_size,
        "round2 Region clamp: destination allocation retains the requested bounded size");
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = desc.MiscFlags = desc.StructureByteStride = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Buffer> staging;
    require_batch(SUCCEEDED(device.native->CreateBuffer(&desc, nullptr, &staging)),
        "round2 Region clamp: create actual staging buffer");
    // Copy only defined bytes into staging; the uncopied destination tail is
    // unspecified, and this regression neither copies nor inspects that tail.
    D3D11_BOX box{};
    box.right = copied_bytes;
    box.bottom = box.back = 1;
    device.context->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, resource, 0, &box);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    require_batch(SUCCEEDED(device.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)),
        "round2 Region clamp: map actual copied prefix");
    bool equal = true;
    for (UINT i = 0; i < copied_bytes; ++i)
        equal &= static_cast<const unsigned char*>(mapped.pData)[i] ==
            static_cast<unsigned char>((source_offset + i) * 37 + 11);
    device.context->Unmap(staging.Get(), 0);
    require_batch(equal, "round2 Region clamp: every available source byte copied to destination zero");
}
} // namespace

void test_dx11_round2_region_clamp_regression()
{
    using namespace dx11_slot_test;
    BatchDevice device;
    auto source = buffer(device, 2048, D3D11_BIND_VERTEX_BUFFER);
    auto sentinel = buffer(device, 256, D3D11_BIND_CONSTANT_BUFFER);
    ID3D11Buffer* native_source = source.Get();
    const UINT stride = 4, offset = 0;
    device.context->IASetVertexBuffers(0, 1, &native_source, &stride, &offset);
    ID3D11Buffer* native_sentinel = sentinel.Get();
    device.context->CSSetConstantBuffers(0, 1, &native_sentinel);

    // This uses a VB with an explicit four-byte stride so upstream's generic
    // copy helper cannot round a short prefix down using a whole-CB stride.
    scalar(device, L"cs-cb0", L"copy vb0->Region(1792,512)");
    auto partial = current_constant(device);
    require_batch(partial && partial.Get() != sentinel.Get() && partial.Get() != source.Get(),
        "round2 Region clamp: crossing the physical end still publishes an independent destination");
    require_copy_prefix(device, partial.Get(), 512, 1792, 256);

    scalar(device, L"cs-cb0", L"copy vb0->Region(256,512)");
    auto inside = current_constant(device);
    require_batch(inside && inside.Get() != source.Get(),
        "round2 Region clamp: ordinary internal region remains an independent copy");
    require_copy_prefix(device, inside.Get(), 512, 256, 512);

    // RecreateCompatibleBuffer bounds an overlarge requested allocation by
    // the physical backing extent, separately from the source offset clamp.
    scalar(device, L"cs-cb0", L"copy vb0->Region(0,4096)");
    auto whole = current_constant(device);
    require_copy_prefix(device, whole.Get(), 2048, 0, 2048);

    for (const wchar_t* text : {L"copy vb0->Region(2048,16)", L"copy vb0->Region(2304,16)"}) {
        auto before = current_constant(device);
        scalar(device, L"cs-cb0", text);
        require_batch(current_constant(device).Get() == before.Get(),
            "round2 Region clamp: offset at or beyond physical end preserves the prior binding");
    }
    device.context->ClearState();
}
