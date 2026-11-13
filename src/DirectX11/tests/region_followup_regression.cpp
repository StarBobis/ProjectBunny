#include "slot_range_test_support.h"

namespace {
using namespace dx11_slot_test;

std::vector<unsigned char> region_bytes(BatchDevice& device, ID3D11Buffer* resource)
{
    D3D11_BUFFER_DESC desc = {};
    resource->GetDesc(&desc);
    std::vector<unsigned char> bytes(desc.ByteWidth);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = desc.MiscFlags = desc.StructureByteStride = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Buffer> staging;
    require_batch(SUCCEEDED(device.native->CreateBuffer(&desc, nullptr, &staging)), "Region followup: staging allocation");
    device.context->CopyResource(staging.Get(), resource);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    require_batch(SUCCEEDED(device.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "Region followup: native Map");
    std::memcpy(bytes.data(), mapped.pData, bytes.size());
    device.context->Unmap(staging.Get(), 0);
    return bytes;
}

ComPtr<ID3D11Buffer> copied_buffer(CustomResource& resource, UINT expected_size)
{
    ComPtr<ID3D11Buffer> result;
    require_batch(resource.resource && SUCCEEDED(resource.resource->QueryInterface(IID_PPV_ARGS(&result))), "Region followup: destination is actual buffer");
    D3D11_BUFFER_DESC desc = {};
    result->GetDesc(&desc);
    require_batch(desc.ByteWidth == expected_size && resource.offset == 0,
        "Region followup: new allocation has selected length and zero destination offset");
    return result;
}

void require_region_bytes(BatchDevice& device, ID3D11Buffer* resource, UINT first, UINT count)
{
    auto bytes = region_bytes(device, resource);
    require_batch(bytes.size() == count, "Region followup: readback covers exact selected allocation");
    for (UINT i = 0; i < count; ++i)
        require_batch(bytes[i] == static_cast<unsigned char>((first + i) * 37 + 11),
            "Region followup: actual selected bytes are copied without stride rounding");
}
} // namespace

void test_dx11_region_followup_regression()
{
    BatchDevice device;
    Registry registry;
    auto typed = buffer(device, 64, D3D11_BIND_SHADER_RESOURCE);
    auto& source = registry.resource(L"resourceregion_bytes_source", device, typed.Get(), DXGI_FORMAT_R32_UINT, 4, 64);
    auto& copy = registry.resource(L"resourceregion_bytes_copy", device);
    copy.bind_flags = D3D11_BIND_SHADER_RESOURCE;
    scalar(device, copy.name, L"copy resourceregion_bytes_source->region(16,32)");
    auto allocation = copied_buffer(copy, 32);
    require_region_bytes(device, allocation.Get(), 16, 32);
    scalar(device, L"ps-t0", L"ref resourceregion_bytes_copy");
    auto view = get_slot(device, L'p', 0);
    D3D11_SHADER_RESOURCE_VIEW_DESC view_desc = {};
    require_batch(view != nullptr, "Region followup: copied typed allocation has SRV");
    view->GetDesc(&view_desc);
    require_batch(view_desc.Buffer.FirstElement == 0 && view_desc.Buffer.NumElements == 8,
        "Region followup: copied allocation view never uses source backing extent");
    device.context->ClearState();

    auto structured = buffer(device, 64, D3D11_BIND_SHADER_RESOURCE, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 16);
    registry.resource(L"resourceregion_struct_source", device, structured.Get(), DXGI_FORMAT_UNKNOWN, 16, 64);
    auto& structure_copy = registry.resource(L"resourceregion_struct_copy", device);
    structure_copy.bind_flags = D3D11_BIND_SHADER_RESOURCE;
    scalar(device, structure_copy.name, L"copy resourceregion_struct_source->region(16,32)");
    auto structured_allocation = copied_buffer(structure_copy, 32);
    D3D11_BUFFER_DESC structured_desc = {};
    structured_allocation->GetDesc(&structured_desc);
    require_batch((structured_desc.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED) && structured_desc.StructureByteStride == 16,
        "Region followup: legal structured copy retains actual structure layout");
    require_region_bytes(device, structured_allocation.Get(), 16, 32);
    ID3D11Resource* previous = structure_copy.resource;
    scalar(device, structure_copy.name, L"copy resourceregion_struct_source->region(4,16)");
    require_batch(structure_copy.resource == previous && structure_copy.offset == 0,
        "Region followup: misaligned explicit structured span is rejected before replacing custom resource");
    require_region_bytes(device, structured_allocation.Get(), 16, 32);

    // COPY_DESC creates only the allocation/view. Its uninitialized contents are deliberately not read.
    scalar(device, L"ps-t1", L"copy_desc resourceregion_bytes_source->region(16,32)");
    auto descriptor_view = get_slot(device, L'p', 1);
    require_batch(descriptor_view != nullptr, "Region followup: descriptor-only selected view exists");
    descriptor_view->GetDesc(&view_desc);
    ComPtr<ID3D11Resource> descriptor_resource;
    descriptor_view->GetResource(&descriptor_resource);
    ComPtr<ID3D11Buffer> descriptor_buffer;
    require_batch(SUCCEEDED(descriptor_resource.As(&descriptor_buffer)), "Region followup: descriptor-only native buffer");
    D3D11_BUFFER_DESC descriptor = {};
    descriptor_buffer->GetDesc(&descriptor);
    require_batch(descriptor.ByteWidth == 32 && view_desc.Buffer.FirstElement == 0 && view_desc.Buffer.NumElements == 8,
        "Region followup: descriptor-only allocation and view use local zero-based selected extent");

    auto bound = bound_resource(device, L'p', 1);
    const wchar_t* invalid[] = {L"copy resourceregion_bytes_source->region(4294967295,16)",
        L"copy resourceregion_bytes_source->region(64,1)", L"copy resourceregion_bytes_source->region(16,4294967295)"};
    for (const auto* expression : invalid) {
        scalar(device, L"ps-t1", expression);
        require_batch(bound_resource(device, L'p', 1).Get() == bound.Get(),
            "Region followup: overflowing/out-of-physical explicit request retains previous native binding");
        ID3D11Resource* old_custom = copy.resource;
        scalar(device, copy.name, expression);
        require_batch(copy.resource == old_custom && copy.offset == 0,
            "Region followup: invalid explicit request is rejected before custom recreation");
    }
    device.context->ClearState();

    // Both a captured-count custom target and a direct Region with an already
    // existing full UAV must clear only the selected 32 bytes.
    for (unsigned direct = 0; direct < 2; ++direct) {
        auto writable = buffer(device, 64, D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE);
        const std::wstring name = direct ? L"resourceregion_clear_direct" : L"resourceregion_clear_source";
        auto& writable_source = registry.resource(name, device, writable.Get(), DXGI_FORMAT_R32_UINT, 4, 64);
        D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
        uav_desc.Format = DXGI_FORMAT_R32_UINT;
        uav_desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uav_desc.Buffer.NumElements = 16;
        ComPtr<ID3D11UnorderedAccessView> full_view;
        require_batch(SUCCEEDED(device.native->CreateUnorderedAccessView(writable.Get(), &uav_desc, &full_view)),
            "Region followup: actual existing full UAV");
        writable_source.view = full_view.Get(); writable_source.view->AddRef();
        std::wstring clear_target = name + L"->region(16,32)";
        if (!direct) {
            auto& captured = registry.resource(L"resourceregion_clear_capture", device);
            scalar(device, captured.name, L"ref " + clear_target);
            clear_target = captured.name;
        }
        CommandList clear;
        CommandListScope scope; scope.emplace_front(); clear.scope = &scope;
        const std::wstring ns;
        std::wstring args = clear_target + L" 0x0";
        require_batch(ParseCommandListGeneralCommands(L"CommandListRegionClear", L"clear", &args,
            &clear, &clear, &clear, &ns), "Region followup: parse actual clear command");
        clear.scope = nullptr;
        execute(clear, device);
        auto bytes = region_bytes(device, writable.Get());
        for (UINT i = 0; i < bytes.size(); ++i)
            require_batch(bytes[i] == (i >= 16 && i < 48 ? 0 : static_cast<unsigned char>(i * 37 + 11)),
                "Region followup: actual clear respects selected span and preserves prefix/suffix");
    }
    device.context->ClearState();
    std::fprintf(stderr, "[PASS] Region native copy/descriptor/physical bounds followup\n");
}
