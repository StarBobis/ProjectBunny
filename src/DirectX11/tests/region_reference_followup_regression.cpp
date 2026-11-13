#include "slot_range_test_support.h"

namespace {
using namespace dx11_slot_test;

class RegionDrawScope {
    CommandListState& state;
    DrawCallInfo* previous;
    DrawCallInfo draw;
public:
    explicit RegionDrawScope(CommandListState& state) : state(state), previous(state.call_info)
    {
        draw.FirstVertex = 0;
        state.call_info = &draw;
    }
    ~RegionDrawScope() { state.call_info = previous; }
};

ResourceCopyTarget parsed_region_target(const wchar_t* name)
{
    ResourceCopyTarget target;
    CommandListScope scope;
    scope.emplace_front();
    const std::wstring ns;
    require_batch(target.ParseTarget(name, true, &ns, &scope), "Region reference: parse actual public target");
    return target;
}

void require_srv_window(BatchDevice& device, UINT slot, ID3D11Resource* original, UINT first, UINT count)
{
    auto view = get_slot(device, L'p', slot);
    require_batch(view != nullptr, "Region reference: actual SRV is bound");
    D3D11_SHADER_RESOURCE_VIEW_DESC desc = {};
    view->GetDesc(&desc);
    ComPtr<ID3D11Resource> resource;
    view->GetResource(&resource);
    require_batch(desc.ViewDimension == D3D11_SRV_DIMENSION_BUFFER,
        "Region reference: selected view has actual buffer SRV dimension");
    if (desc.Buffer.FirstElement != first || desc.Buffer.NumElements != count)
        std::fprintf(stderr, "Region reference SRV slot %u: actual FirstElement=%u NumElements=%u expected=%u/%u\n",
            slot, desc.Buffer.FirstElement, desc.Buffer.NumElements, first, count);
    require_batch(resource.Get() == original && desc.Format == DXGI_FORMAT_R32_UINT &&
        desc.ViewDimension == D3D11_SRV_DIMENSION_BUFFER && desc.Buffer.FirstElement == first && desc.Buffer.NumElements == count,
        "Region reference: actual typed SRV retains exact selected byte span");
}

void require_uav_window(BatchDevice& device, ID3D11Resource* original, UINT first, UINT count)
{
    ComPtr<ID3D11UnorderedAccessView> view;
    device.context->CSGetUnorderedAccessViews(0, 1, &view);
    require_batch(view != nullptr, "Region reference: actual UAV is bound");
    D3D11_UNORDERED_ACCESS_VIEW_DESC desc = {};
    view->GetDesc(&desc);
    ComPtr<ID3D11Resource> resource;
    view->GetResource(&resource);
    require_batch(resource.Get() == original && desc.Format == DXGI_FORMAT_R32_UINT &&
        desc.ViewDimension == D3D11_UAV_DIMENSION_BUFFER && desc.Buffer.FirstElement == first && desc.Buffer.NumElements == count,
        "Region reference: captured span is preserved when an ordinary ref creates a different view type");
}

ComPtr<ID3D11Buffer> require_copied_region(BatchDevice& device, CustomResource& destination, ID3D11Resource* original)
{
    ComPtr<ID3D11Buffer> result;
    require_batch(!destination.is_null && destination.resource && destination.resource != original &&
        SUCCEEDED(destination.resource->QueryInterface(IID_PPV_ARGS(&result))),
        "Region reference: captured span copies to independent actual buffer");
    D3D11_BUFFER_DESC desc = {};
    result->GetDesc(&desc);
    require_batch(desc.ByteWidth == 32 && destination.offset == 0,
        "Region reference: captured ref copies exactly 32 bytes to backing offset zero");
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = desc.MiscFlags = desc.StructureByteStride = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Buffer> staging;
    require_batch(SUCCEEDED(device.native->CreateBuffer(&desc, nullptr, &staging)), "Region reference: create actual staging buffer");
    device.context->CopyResource(staging.Get(), result.Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    require_batch(SUCCEEDED(device.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "Region reference: map actual copied span");
    bool exact = true;
    for (UINT i = 0; i < 32; ++i)
        exact &= static_cast<const unsigned char*>(mapped.pData)[i] == static_cast<unsigned char>((i + 16) * 37 + 11);
    device.context->Unmap(staging.Get(), 0);
    require_batch(exact, "Region reference: all selected bytes 16..47 copied in original order");
    return result;
}
} // namespace

void test_dx11_region_reference_followup_regression()
{
    BatchDevice device;
    Registry registry;
    RegionDrawScope draw(device.state);
    constexpr UINT flags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_VERTEX_BUFFER;
    auto original = buffer(device, 64, flags);
    auto& source = registry.resource(L"resourceregion_follow_source", device, original.Get(), DXGI_FORMAT_R32_UINT, 4, 64);
    auto& captured = registry.resource(L"resourceregion_follow_capture", device);
    auto& copied = registry.resource(L"resourceregion_follow_copy", device);
    copied.bind_flags = static_cast<D3D11_BIND_FLAG>(flags);

    scalar(device, L"ps-t0", L"ref resourceregion_follow_source->region(16,32)");
    require_srv_window(device, 0, original.Get(), 4, 8);
    device.context->ClearState();

    scalar(device, captured.name, L"ref resourceregion_follow_source->region(16,32)");
    auto capture_target = parsed_region_target(L"resourceregion_follow_capture");
    require_batch(capture_target.GetResourceSize(&device.state) == 32,
        "Region reference: captured explicit byte-count is exposed by actual custom Size getter");
    scalar(device, L"ps-t1", L"ref resourceregion_follow_capture");
    require_srv_window(device, 1, original.Get(), 4, 8);
    device.context->ClearState();
    scalar(device, L"cs-u0", L"ref resourceregion_follow_capture");
    require_uav_window(device, original.Get(), 4, 8);
    device.context->ClearState();
    scalar(device, L"vb1", L"ref resourceregion_follow_capture");
    auto vb = parsed_region_target(L"vb1");
    require_batch(vb.GetResourceOffset(&device.state) == 16,
        "Region reference: public VB Offset getter observes captured backing offset");
    device.context->ClearState();

    scalar(device, copied.name, L"copy resourceregion_follow_capture");
    auto copied_buffer = require_copied_region(device, copied, original.Get());
    auto copy_target = parsed_region_target(L"resourceregion_follow_copy");
    require_batch(copy_target.GetResourceSize(&device.state) == 32,
        "Region reference: copied custom Size getter reports the selected allocation");
    scalar(device, L"ps-t2", L"ref resourceregion_follow_copy");
    require_srv_window(device, 2, copied_buffer.Get(), 0, 8);
    device.context->ClearState();

    registry.pool(L"poolregion_follow_capture", device, PoolIndexType::RING, 1);
    scalar(device, L"poolregion_follow_capture[0]", L"ref resourceregion_follow_source->region(16,32)");
    CommandList broadcast;
    append(broadcast, L"ps-t[3:4]", L"ref poolregion_follow_capture[0]");
    execute(broadcast, device);
    require_srv_window(device, 3, original.Get(), 4, 8);
    require_srv_window(device, 4, original.Get(), 4, 8);
    device.context->ClearState();

    // Legacy buf_size is an end bound, not an explicit Region byte count.
    // A 64-byte backing buffer with offset 16 therefore exposes 12 floats.
    auto& legacy = registry.resource(L"resourceregion_follow_legacy", device);
    assign(device, &legacy, original.Get(), nullptr, 16, 64, DXGI_FORMAT_R32_UINT);
    legacy.stride = 4;
    scalar(device, captured.name, L"null");
    scalar(device, captured.name, L"ref resourceregion_follow_legacy");
    require_batch(capture_target.GetResourceSize(&device.state) == 64,
        "Region reference: ordinary legacy ref after clear restores legacy physical/end-bound metadata");
    scalar(device, L"ps-t1", L"ref resourceregion_follow_capture");
    require_srv_window(device, 1, original.Get(), 4, 12);
    device.context->ClearState();

    scalar(device, L"poolregion_follow_capture[*]", L"null");
    scalar(device, L"poolregion_follow_capture[0]", L"ref resourceregion_follow_legacy");
    execute(broadcast, device); // Same operation/cache after the pool reset.
    require_srv_window(device, 3, original.Get(), 4, 12);
    require_srv_window(device, 4, original.Get(), 4, 12);
    device.context->ClearState();
    (void)source;
}
