#include "slot_range_test_support.h"
#include "cache_policy_test_scope.h"

namespace {
using namespace dx11_slot_test;

ComPtr<ID3D11Buffer> tracked_vertex_buffer(BatchDevice& device, UINT seed)
{
    UINT values[16];
    for (UINT i = 0; i < 16; ++i) values[i] = seed + i;
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = sizeof(values);
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA initial{};
    initial.pSysMem = values;
    ComPtr<ID3D11Buffer> buffer;
    require_batch(SUCCEEDED(device.device->CreateBuffer(&desc, &initial, &buffer)),
        "round2 snapshot: create real tracked vertex buffer");
    return buffer;
}

void capture(BatchDevice& device, const wchar_t* name)
{
    CommandList commands;
    append(commands, name, L"ref vb0");
    execute(commands, device);
}

float property(BatchDevice& device, const wchar_t* text, bool stride)
{
    const std::wstring ns;
    ResourceCopyTarget target;
    require_batch(target.ParseTarget(text, true, &ns, nullptr),
        "round2 snapshot: parse real custom resource property");
    return stride ? target.GetResourceStride(&device.state) : target.GetResourceRegionHash(&device.state);
}
}

void test_dx11_round2_snapshot_stride_regression()
{
    using namespace dx11_slot_test;
    DataCacheTestScope policy;
    BatchDevice device;
    Registry registry;
    auto source = tracked_vertex_buffer(device, 10);
    auto patch = tracked_vertex_buffer(device, 90);
    const uint32_t before = GetRegionHash(device.wrapper.get(), source.Get(), 0, 16);
    require_batch(before != 0 && GetRegionHash(device.wrapper.get(), patch.Get(), 0, 16) != 0,
        "round2 snapshot: prepare CPU caches through actual production readback");
    auto& saved = registry.resource(L"resourceround2_snapshot_saved", device);
    auto& cold = registry.resource(L"resourceround2_snapshot_cold", device);
    ID3D11Buffer* bound = source.Get();
    const UINT offset = 0;
    for (const UINT stride : {4u, 8u}) {
        device.context->IASetVertexBuffers(0, 1, &bound, &stride, &offset);
        capture(device, L"resourceround2_snapshot_saved");
        require_batch(saved.override_stride == -1 && saved.override_format == static_cast<DXGI_FORMAT>(-1),
            "round2 stride: exercise custom fallback without declaration overrides");
        require_batch(property(device, L"resourceround2_snapshot_saved->stride", true) == stride,
            "round2 stride: captured VB reports actual 4/8-byte stride, intentionally fixing upstream fallthrough");
    }
    capture(device, L"resourceround2_snapshot_cold");
    require_batch(saved.resource == source.Get() && cold.resource == source.Get() &&
        saved.GetHandleInfo() && cold.GetHandleInfo(),
        "round2 snapshot: real ref captures share the native buffer and own CPU cache views");
    const float old_hash = property(device, L"resourceround2_snapshot_saved->hashregion(0,16)", false);
    require_batch(old_hash == EncodeFloat30(HashUnsigned32(before)),
        "round2 snapshot: warm captured view hash from original bytes");

    D3D11_BOX box{};
    box.left = 0;
    box.right = sizeof(UINT);
    box.bottom = box.back = 1;
    device.wrapper->CopySubresourceRegion(source.Get(), 0, sizeof(UINT), 0, 0, patch.Get(), 0, &box);
    const uint32_t after = GetRegionHash(device.wrapper.get(), source.Get(), 0, 16);
    require_batch(after != 0 && after != before,
        "round2 snapshot: tracked partial copy updates source hash and invalidates its old layers");
    require_batch(property(device, L"resourceround2_snapshot_saved->hashregion(0,16)", false) == old_hash &&
        property(device, L"resourceround2_snapshot_cold->hashregion(0,16)", false) == old_hash,
        "round2 snapshot: warmed and first-use captured CPU views both preserve capture-time bytes");
    UINT current = 0, captured = 0;
    require_batch(TryReadResourceDataCache(source.Get(), sizeof(UINT), &current, sizeof(current)),
        "round2 snapshot: read current source CPU cache");
    std::memcpy(&captured, cold.GetHandleInfo()->GetCachedData() + sizeof(UINT), sizeof(captured));
    require_batch(current == 90 && captured == 11,
        "round2 snapshot: source contains partial write while captured view keeps old bytes");
    D3D11_BUFFER_DESC readback_desc{};
    source->GetDesc(&readback_desc);
    readback_desc.Usage = D3D11_USAGE_STAGING;
    readback_desc.BindFlags = 0;
    readback_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Buffer> readback;
    require_batch(SUCCEEDED(device.native->CreateBuffer(&readback_desc, nullptr, &readback)),
        "round2 snapshot: create independent native readback");
    device.context->CopyResource(readback.Get(), source.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    require_batch(SUCCEEDED(device.context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped)),
        "round2 snapshot: map actual partially updated GPU buffer");
    UINT gpu_value = 0;
    std::memcpy(&gpu_value, static_cast<const uint8_t*>(mapped.pData) + sizeof(UINT), sizeof(gpu_value));
    device.context->Unmap(readback.Get(), 0);
    require_batch(gpu_value == current,
        "round2 snapshot: current source cache agrees with actual GPU bytes");
    std::fputs("PASS: intentional custom-stride and captured CPU-snapshot behavior through production WARP paths\n", stderr);
}
