#include "slot_range_test_support.h"

namespace {
using namespace dx11_slot_test;

class CBRangeGlobals {
    unsigned saved_frame = G->frame_no;
public:
    ~CBRangeGlobals() { G->frame_no = saved_frame; }
};

void set_cb_window(BatchDevice& device, wchar_t stage, UINT slot, ID3D11Buffer* buffer, UINT first, UINT count)
{
    switch (stage) {
    case L'v': device.context->VSSetConstantBuffers1(slot, 1, &buffer, &first, &count); break;
    case L'h': device.context->HSSetConstantBuffers1(slot, 1, &buffer, &first, &count); break;
    case L'd': device.context->DSSetConstantBuffers1(slot, 1, &buffer, &first, &count); break;
    case L'g': device.context->GSSetConstantBuffers1(slot, 1, &buffer, &first, &count); break;
    case L'p': device.context->PSSetConstantBuffers1(slot, 1, &buffer, &first, &count); break;
    case L'c': device.context->CSSetConstantBuffers1(slot, 1, &buffer, &first, &count); break;
    }
}

void require_cb_bytes(BatchDevice& device, ID3D11Buffer* original, UINT expected_size)
{
    require_batch(original != nullptr, "CB range: real copied CB is bound");
    D3D11_BUFFER_DESC desc = {};
    original->GetDesc(&desc);
    require_batch(desc.ByteWidth == expected_size,
        "CB range: copy allocation stays inside physical source instead of hardware whole window");
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = desc.MiscFlags = desc.StructureByteStride = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Buffer> staging;
    require_batch(SUCCEEDED(device.native->CreateBuffer(&desc, nullptr, &staging)),
        "CB range: create actual staging buffer");
    device.context->CopyResource(staging.Get(), original);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    require_batch(SUCCEEDED(device.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)),
        "CB range: map actual copy bytes");
    bool correct = true;
    const auto* bytes = static_cast<const unsigned char*>(mapped.pData);
    for (UINT i = 0; i < expected_size; ++i)
        correct &= bytes[i] == static_cast<unsigned char>(i * 37 + 11);
    device.context->Unmap(staging.Get(), 0);
    require_batch(correct, "CB range: complete native copy matches every physical source byte");
}

void exercise_cb_stage(BatchDevice& device, Registry& registry, wchar_t stage)
{
    const auto suffix = std::wstring(1, stage);
    const auto prefix = suffix + L"s-cb";
    device.context->ClearState();
    auto current = buffer(device, 1024, D3D11_BIND_CONSTANT_BUFFER);
    auto previous = buffer(device, 1024, D3D11_BIND_CONSTANT_BUFFER);
    auto& source = registry.pool(L"poolcb_range_sources_" + suffix, device, PoolIndexType::RING, 2);
    // Stored source metadata can describe a region. Plain CB range ref must
    // nevertheless select the whole backing buffer, as scalar plain ref does.
    assign(device, source.GetResource(0, false, true, true), current.Get(), nullptr, 256, 256);
    assign(device, source.GetResource(1, false, true, true), nullptr);

    CommandList mixed;
    append(mixed, prefix + L"[0:1]", L"ref unless_null poolcb_range_sources_" + suffix + L"[0:1]");
    // An ordinary in-bounds window, and a hardware-legal window whose end
    // exceeds the 1 KiB physical backing allocation, must both survive skips.
    for (UINT first : {16u, 48u}) {
        device.context->ClearState();
        set_cb_window(device, stage, 1, previous.Get(), first, 32);
        UINT before_offset = 0, before_count = 0;
        auto before = get_cb(device, stage, 1, before_offset, before_count);
        require_batch(before.Get() == previous.Get() && before_offset == first && before_count == 32,
            "CB range: native setup retains legal CB1 window before assignment");
        const auto stats = execute(mixed, device);
        require_batch(stats.cb_get_calls == 1 && stats.cb1_set_calls == 1 && stats.cb_set_calls == 0,
            "CB range: mixed whole/window publication uses one actual CB1 helper call");
        UINT offset = 0, count = 0;
        auto newly_bound = get_cb(device, stage, 0, offset, count);
        require_batch(newly_bound.Get() == current.Get() && offset == 0 && count == 4096,
            "CB range: new plain reference is whole-buffer 0/4096 in mixed CB1 group");
        auto kept = get_cb(device, stage, 1, offset, count);
        require_batch(kept.Get() == previous.Get() && offset == before_offset && count == before_count,
            "CB range: unless_null preserves exact old window including out-of-physical end");
    }

    // Full source metadata may be the hardware 64 KiB whole window. Copying
    // still reads only the physical buffer and binds copied storage whole.
    assign(device, source.GetResource(0, false, true, true), current.Get(), nullptr, 0, 65536);
    assign(device, source.GetResource(1, false, true, true), current.Get(), nullptr, 0, 65536);
    CommandList copy;
    append(copy, prefix + L"[0:1]", L"copy poolcb_range_sources_" + suffix + L"[0:1]");
    execute(copy, device);
    for (UINT slot = 0; slot < 2; ++slot) {
        UINT offset = 0, count = 0;
        auto copied = get_cb(device, stage, slot, offset, count);
        require_batch(copied.Get() != current.Get() && offset == 0 && count == 4096,
            "CB range: explicit copy creates independent whole-bound storage");
        require_cb_bytes(device, copied.Get(), 1024);
    }

    auto& captured = registry.pool(L"poolcb_range_captured_" + suffix, device, PoolIndexType::RING, 2);
    CommandList fetch;
    append(fetch, L"poolcb_range_captured_" + suffix + L"[0:1]", L"copy " + prefix + L"[0:1]");
    execute(fetch, device);
    for (UINT slot = 0; slot < 2; ++slot) {
        auto* element = captured.GetResource(static_cast<float>(slot), false, true, false);
        require_batch(element && !element->is_null && element->resource != current.Get(),
            "CB range: fetch copy publishes actual pool element resource");
        ComPtr<ID3D11Buffer> copied;
        require_batch(SUCCEEDED(element->resource->QueryInterface(IID_PPV_ARGS(&copied))),
            "CB range: captured resource is a real buffer");
        require_cb_bytes(device, copied.Get(), 1024);
    }

    // Null-source skips must occur before even resolving assignment targets.
    auto& skipped = registry.pool(L"poolcb_range_skipped_" + suffix, device, PoolIndexType::FIFO, 2);
    skipped.element_type_switch_reset = true;
    skipped.read_refreshes_expiration = false;
    G->frame_no = 71;
    auto* old_variable = skipped.GetVariable(0, false, true, true);
    require_batch(old_variable != nullptr, "CB range: initialize actual variable slot");
    old_variable->fval = 42;
    unsigned timestamp = 0;
    require_batch(skipped.GetLastUpdateFrame(0, true, &timestamp) && timestamp == 71,
        "CB range: record actual pre-skip update timestamp");
    unsigned unused = 0;
    require_batch(!skipped.GetLastUpdateFrame(1, true, &unused), "CB range: second slot starts unwritten");
    const size_t resources_before = customResources.size();
    const size_t variables_before = command_list_globals.size();
    CommandList skip_fetch;
    append(skip_fetch, L"poolcb_range_skipped_" + suffix + L"[0:1]", L"copy unless_null " + prefix + L"[0:1]");
    device.context->ClearState();
    G->frame_no = 93;
    execute(skip_fetch, device);
    require_batch(customResources.size() == resources_before && command_list_globals.size() == variables_before,
        "CB range: skipped fetch creates no lazy resource or variable registrations");
    require_batch(old_variable->fval == 42 && skipped.GetLastUpdateFrame(0, true, &timestamp) && timestamp == 71,
        "CB range: skipped fetch preserves variable type/value and update timestamp");
    require_batch(!skipped.GetLastUpdateFrame(1, true, &unused),
        "CB range: skipped empty target gains no fabricated update record");
    device.context->ClearState();
}
} // namespace

void test_dx11_slot_range_cb_regression()
{
    CBRangeGlobals restore;
    BatchDevice device;
    Registry registry;
    for (wchar_t stage : std::wstring(L"vhdgpc"))
        exercise_cb_stage(device, registry, stage);
}
