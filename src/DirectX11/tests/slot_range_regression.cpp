#include "slot_range_test_support.h"
#include "cache_policy_test_scope.h"
#include "profiling.h"
using namespace dx11_slot_test;


extern uint32_t PackCellCoords(float, float, float, float);

namespace {
class RangeGlobals {
    DataCacheTestScope cache_policy;
    bool tracking = G->track_region_hashes;
    unsigned frame = G->frame_no;
    int dirty = G->user_config_dirty;
public:
    RangeGlobals() { G->track_region_hashes = true; }
    ~RangeGlobals() { G->track_region_hashes = tracking; G->frame_no = frame; G->user_config_dirty = dirty; ClearRegionHashesGlobalCache(); }
};

const GUID range_view_lifetime_guid = {0x2724bc2e, 0x4daa, 0x4a21, {0x9f,0x4c,0x5f,0x85,0xa7,0x16,0x1a,0x03}};
class RangeViewLifetime : public IUnknown {
    volatile LONG references = 1;
    int& destroyed;
public:
    explicit RangeViewLifetime(int& destroyed) : destroyed(destroyed) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override
    {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (id != __uuidof(IUnknown)) return E_NOINTERFACE;
        *out = this; AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&references); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const LONG count = InterlockedDecrement(&references);
        if (!count) { ++destroyed; delete this; }
        return count;
    }
};

void view_descriptor(BatchDevice& device, UINT slot, DXGI_FORMAT format, UINT first, UINT count, bool raw = false)
{
    auto view = get_slot(device, L'c', slot);
    if (!view) std::fprintf(stderr, "range missing buffer view: cs-t%u expected format=%u first=%u count=%u raw=%u\n",
        slot, static_cast<unsigned>(format), first, count, static_cast<unsigned>(raw));
    require_batch(view != nullptr, "range: actual first-created buffer view exists");
    D3D11_SHADER_RESOURCE_VIEW_DESC desc = {};
    view->GetDesc(&desc);
    require_batch(desc.Format == format, "range: actual view preserves current format metadata");
    require_batch((raw ? desc.BufferEx.FirstElement : desc.Buffer.FirstElement) == first &&
        (raw ? desc.BufferEx.NumElements : desc.Buffer.NumElements) == count, "range: actual view preserves current byte offset and full extent");
    if (raw) require_batch(desc.ViewDimension == D3D11_SRV_DIMENSION_BUFFEREX &&
        (desc.BufferEx.Flags & D3D11_BUFFEREX_SRV_FLAG_RAW), "range: raw view flags preserved");
}

void buffer_views_and_lifetimes(BatchDevice& device, Registry& registry)
{
    device.context->ClearState();
    auto typed = buffer(device, 64, D3D11_BIND_SHADER_RESOURCE);
    auto structured = buffer(device, 64, D3D11_BIND_SHADER_RESOURCE, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 16);
    auto raw = buffer(device, 64, D3D11_BIND_SHADER_RESOURCE, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS);
    auto& alias = registry.resource(L"resourceslot_range_typed", device, typed.Get(), DXGI_FORMAT_R32_UINT, 4, 64);
    alias.offset = 16;
    registry.resource(L"resourceslot_range_structured", device, structured.Get(), DXGI_FORMAT_UNKNOWN, 16, 64);
    registry.resource(L"resourceslot_range_raw", device, raw.Get());
    CommandList typed_command;
    append(typed_command, L"cs-t[20:21]", L"ref resourceslot_range_typed");
    execute(typed_command, device);
    view_descriptor(device, 20, DXGI_FORMAT_R32_UINT, 4, 12);
    alias.offset = 32;
    execute(typed_command, device);
    view_descriptor(device, 20, DXGI_FORMAT_R32_UINT, 8, 8);
    alias.format = DXGI_FORMAT_R32_FLOAT;
    execute(typed_command, device);
    view_descriptor(device, 21, DXGI_FORMAT_R32_FLOAT, 8, 8);
    scalar(device, L"cs-t[22:23]", L"ref resourceslot_range_structured");
    view_descriptor(device, 22, DXGI_FORMAT_UNKNOWN, 0, 4);
    scalar(device, L"cs-t[24:25]", L"ref resourceslot_range_raw");
    view_descriptor(device, 24, DXGI_FORMAT_R32_TYPELESS, 0, 16, true);
    // More than fourteen slow/null elements exercise every formerly undersized
    // CB temporary array without relying on a source-code assertion.
    scalar(device, L"cs-t[32:63]", L"copy raw no_view_cache resourceslot_range_raw");
    view_descriptor(device, 63, DXGI_FORMAT_R32_TYPELESS, 0, 16, true);
    ComPtr<ID3D11Buffer> raw_copy;
    auto copied_raw_resource = bound_resource(device, L'c', 63);
    require_batch(copied_raw_resource && SUCCEEDED(copied_raw_resource.As(&raw_copy)) && raw_copy.Get() != raw.Get(),
        "range: raw slow copy has independent real buffer storage");
    D3D11_BUFFER_DESC copied_raw_desc = {}; raw_copy->GetDesc(&copied_raw_desc);
    require_batch((copied_raw_desc.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS) &&
        !(copied_raw_desc.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED),
        "range: raw copy never adds incompatible inferred structured misc flag");
    copied_raw_desc.Usage = D3D11_USAGE_STAGING; copied_raw_desc.BindFlags = copied_raw_desc.MiscFlags = 0;
    copied_raw_desc.StructureByteStride = 0; copied_raw_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Buffer> staging;
    require_batch(SUCCEEDED(device.native->CreateBuffer(&copied_raw_desc, nullptr, &staging)), "range: actual raw copy staging");
    device.context->CopyResource(staging.Get(), raw_copy.Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    require_batch(SUCCEEDED(device.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "range: map actual raw slow copy");
    bool exact = true;
    for (UINT i = 0; i < 64; ++i) exact &= static_cast<const unsigned char*>(mapped.pData)[i] == static_cast<unsigned char>(i * 37 + 11);
    device.context->Unmap(staging.Get(), 0);
    require_batch(exact, "range: every raw slow-copy byte matches original buffer");

    scalar(device, L"cs-t[0:127]", L"null");
    for (UINT slot = 0; slot < 128; ++slot) require_batch(!get_slot(device, L'c', slot), "range: all 128 null slots clear safely");

    typed_command.commands.clear();
    device.context->ClearState();
    int released = 0;
    CommandList uncached;
    append(uncached, L"cs-t[20:21]", L"ref no_view_cache resourceslot_range_typed");
    execute(uncached, device);
    auto observed = get_slot(device, L'c', 20);
    auto* lifetime = new RangeViewLifetime(released);
    require_batch(SUCCEEDED(observed->SetPrivateDataInterface(range_view_lifetime_guid, lifetime)), "range: attach real COM view lifetime observer");
    lifetime->Release();
    device.context->ClearState(); observed.Reset();
    // D3D11 defers destruction of device children referenced by queued work.
    // Drain the native context before attributing a live view to our cache.
    device.context->Flush();
    require_batch(released == 1, "range: no_view_cache releases actual view while command remains alive");
}

void physical_keys_and_bounds(BatchDevice& device, Registry& registry)
{
    auto a = texture(device, 0x12345678u), b = texture(device, 0x87654321u);
    for (auto type : {PoolIndexType::RING, PoolIndexType::STATIC, PoolIndexType::FIFO, PoolIndexType::SPATIAL}) {
        const auto name = L"poolslot_range_physical_" + std::to_wstring(static_cast<unsigned>(type));
        auto& pool = registry.pool(name, device, type, 4);
        const float key_a = type == PoolIndexType::SPATIAL ? BitCastToFloat(PackCellCoords(10, 0, 0, 1)) : 100.0f;
        const float key_b = type == PoolIndexType::SPATIAL ? BitCastToFloat(PackCellCoords(30, 0, 0, 1)) : 101.0f;
        auto* slot_a = pool.GetResource(key_a, false, false, true);
        auto* slot_b = pool.GetResource(key_b, false, false, true);
        require_batch(slot_a && slot_b && slot_a != slot_b, "range: distinct real keyed pool slots");
        assign(device, slot_a, a.Get()); assign(device, slot_b, b.Get());
        scalar(device, L"ps-t[0:3]", L"ref " + name + L"[0:3]");
        for (UINT i = 0; i < 4; ++i) {
            auto* physical = pool.GetResource(static_cast<float>(i), false, true, false);
            require_batch(bound_resource(device, L'p', i).Get() == (physical->is_null ? nullptr : physical->resource),
                "range: all index types read physical elements, not keys or missing templates");
        }
        // Explicit pool bounds wrap, unlike a bare slot side inheriting them.
        scalar(device, name + L"[3:6]", L"ref ps-t[0:3]");
        require_batch(pool.GetResource(key_a, false, false, false) == slot_a &&
            pool.GetResource(key_b, false, false, false) == slot_b, "range: physical writes retain original UID/spatial keys");
        scalar(device, L"ps-t[4:7]", L"ref " + name + L"[0:3]");
        for (UINT i = 0; i < 4; ++i) {
            auto* physical = pool.GetResource(static_cast<float>(i), false, true, false);
            require_batch(bound_resource(device, L'p', 4 + i).Get() == (physical->is_null ? nullptr : physical->resource),
                "range: explicit unequal slot and pool starts map physical indices");
        }
        CommandList legacy;
        require_batch(!parse(legacy, L"ps-t", L"ref " + name + L"[0:3]"), "range: bare slot side is rejected after explicit-bounds migration");
        require_batch(!parse(legacy, L"ps-t[0:3]", L"ref " + name), "range: bare pool side is rejected after explicit-bounds migration");
        CommandList invalid;
        append(invalid, L"ps-t[0:2]", L"ref " + name + L"[0:3]");
        auto before = get_slot(device, L'p', 0);
        const auto stats = execute(invalid, device);
        require_batch(stats.get_calls == 0 && stats.set_calls == 0 && get_slot(device, L'p', 0).Get() == before.Get(),
            "range: incompatible explicit lengths fail before actual native access");
        scalar(device, L"ps-t[0:3]", L"ref " + name + L"[3:6]");
        for (UINT i = 0; i < 4; ++i) {
            auto* physical = pool.GetResource(static_cast<float>((3 + i) % 4), false, true, false);
            require_batch(bound_resource(device, L'p', i).Get() == (physical->is_null ? nullptr : physical->resource),
                "range: explicit pool bounds retain legal ring wrapping");
        }
    }
}

void full_uav_ranges(BatchDevice& device, Registry& registry)
{
    require_batch(device.native->GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_1, "range: real full 64-UAV coverage requires available WARP 11.1");
    auto& pool = registry.pool(L"poolslot_range_uav", device, PoolIndexType::RING, 64);
    for (UINT i = 0; i < 64; ++i) {
        auto native = buffer(device, 64, D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE,
            D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS);
        D3D11_UNORDERED_ACCESS_VIEW_DESC desc = {};
        desc.Format = DXGI_FORMAT_R32_TYPELESS; desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        desc.Buffer.NumElements = 16; desc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
        ComPtr<ID3D11UnorderedAccessView> view;
        require_batch(SUCCEEDED(device.native->CreateUnorderedAccessView(native.Get(), &desc, &view)), "range: create distinct actual raw UAV");
        assign(device, pool.GetResource(static_cast<float>(i), false, true, true), native.Get(), view.Get(), 0, 64, DXGI_FORMAT_R32_TYPELESS);
    }
    for (wchar_t stage : std::wstring(L"cp")) {
        device.context->ClearState();
        CommandList bind;
        append(bind, std::wstring(1, stage) + L"s-u[0:63]", L"ref poolslot_range_uav[0:63]");
        const auto stats = execute(bind, device);
        require_batch(stats.uav_get_calls == 1 && stats.uav_set_calls == 1, "range: actual full UAV native calls are batched");
        ComPtr<ID3D11UnorderedAccessView> first, last;
        if (stage == L'c') {
            device.context->CSGetUnorderedAccessViews(0, 1, &first); device.context->CSGetUnorderedAccessViews(63, 1, &last);
        } else {
            device.context->OMGetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, 0, 1, &first);
            device.context->OMGetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, 63, 1, &last);
        }
        require_batch(first.Get() == pool.GetResource(0, false, true, false)->view &&
            last.Get() == pool.GetResource(63, false, true, false)->view, "range: native first/last UAV retain exact physical pool views");
        scalar(device, std::wstring(1, stage) + L"s-u[0:63]", L"null");
    }
}


void all_copy_options(BatchDevice& device, Registry& registry)
{
    auto source = texture(device, 0x22446688u, 7);
    registry.resource(L"resourceslot_range_options", device, source.Get());
    CommandList copy;
    append(copy, L"ps-t[40:55]", L"copy resourceslot_range_options");
    auto stats = execute(copy, device);
    require_batch(stats.get_calls == 1 && stats.set_calls == 1, "range: 16 slow-copy slots publish one native span");
    auto allocation = bound_resource(device, L'p', 55);
    require_pixels(device, allocation.Get(), 0x22446688u);
    execute(copy, device);
    require_batch(bound_resource(device, L'p', 55).Get() == allocation.Get(), "range: repeat run retains per-slot actual copy allocations");
    scalar(device, L"ps-t[60:61]", L"copy_desc resourceslot_range_options");
    ComPtr<ID3D11Texture2D> description;
    auto description_resource = bound_resource(device, L'p', 61);
    require_batch(description_resource && SUCCEEDED(description_resource.As(&description)), "range: copy_desc produces actual texture");
    D3D11_TEXTURE2D_DESC actual = {}; description->GetDesc(&actual);
    require_batch(actual.Width == 7 && actual.Height == 3, "range: copy_desc preserves dimensions without claiming uninitialized data");
    scalar(device, L"ps-t[60:61]", L"ref mono set_viewport resourceslot_range_options");
    UINT viewport_count = 1; D3D11_VIEWPORT viewport = {};
    device.context->RSGetViewports(&viewport_count, &viewport);
    require_batch(viewport_count == 1 && viewport.Width == 7 && viewport.Height == 3 &&
        bound_resource(device, L'p', 61).Get() == source.Get(), "range: mono/ref/viewport options preserve real identity and ordered viewport effect");
    registry.resource(L"resourceslot_range_options_null", device);
    auto old = get_slot(device, L'p', 60);
    scalar(device, L"ps-t[60:61]", L"copy unless_null resourceslot_range_options_null");
    require_batch(get_slot(device, L'p', 60).Get() == old.Get(), "range: skipped slow-copy source retains exact original SRV");

    UINT quality = 0;
    require_batch(SUCCEEDED(device.native->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, 2, &quality)) && quality,
        "range: actual WARP supports 2x MSAA");
    D3D11_TEXTURE2D_DESC msaa = {}; msaa.Width = 4; msaa.Height = 3;
    msaa.MipLevels = msaa.ArraySize = 1; msaa.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    msaa.SampleDesc.Count = 2; msaa.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> multisample;
    ComPtr<ID3D11RenderTargetView> clear;
    require_batch(SUCCEEDED(device.native->CreateTexture2D(&msaa, nullptr, &multisample)) &&
        SUCCEEDED(device.native->CreateRenderTargetView(multisample.Get(), nullptr, &clear)), "range: actual MSAA resource and clear view");
    const float magenta[] = {1, 0, 1, 1}; device.context->ClearRenderTargetView(clear.Get(), magenta);
    registry.resource(L"resourceslot_range_msaa", device, multisample.Get());
    scalar(device, L"ps-t[62:63]", L"resolve_msaa resourceslot_range_msaa");
    require_pixels(device, bound_resource(device, L'p', 62).Get(), 0xffff00ffu);
    require_pixels(device, bound_resource(device, L'p', 63).Get(), 0xffff00ffu);
    CommandList rejected;
    require_batch(!parse(rejected, L"ps-t[0:1]", L"overwrite resourceslot_range_options"), "range: unimplemented overwrite option remains rejected");
}

ComPtr<ID3D11Buffer> tracked_buffer(BatchDevice& device, unsigned seed)
{
    UINT values[16];
    for (UINT i = 0; i < 16; ++i) values[i] = seed + i * 79;
    D3D11_BUFFER_DESC desc = {}; desc.ByteWidth = sizeof(values); desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA data = {}; data.pSysMem = values;
    ComPtr<ID3D11Buffer> result;
    require_batch(SUCCEEDED(device.device->CreateBuffer(&desc, &data, &result)), "range: actual wrapper registers source buffer");
    // Creation registers hashes, not a CPU snapshot. Warm the actual GPU
    // readback path; custom ref snapshots require existing captured source data.
    const auto hash = GetRegionHash(device.wrapper.get(), result.Get(), 0, sizeof(values));
    auto* info = GetResourceHandleInfo(result.Get());
    require_batch(hash && info && info->cached_data && info->cached_data_size == sizeof(values),
        "range: real region hash readback establishes source snapshot before reference fetch");
    return result;
}

void fetch_metadata(BatchDevice& device, Registry& registry)
{
    auto a = tracked_buffer(device, 111), b = tracked_buffer(device, 999);
    require_batch(GetRegionHash(device.wrapper.get(), a.Get(), 0, 64) != GetRegionHash(device.wrapper.get(), b.Get(), 0, 64),
        "range: actual warm source snapshots have distinct contents");
    D3D11_SHADER_RESOURCE_VIEW_DESC desc = {};
    desc.Format = DXGI_FORMAT_R32_UINT; desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    desc.Buffer.FirstElement = 4; desc.Buffer.NumElements = 3;
    ComPtr<ID3D11ShaderResourceView> view_a, view_b;
    require_batch(SUCCEEDED(device.native->CreateShaderResourceView(a.Get(), &desc, &view_a)) &&
        SUCCEEDED(device.native->CreateShaderResourceView(b.Get(), &desc, &view_b)), "range: real nonzero source buffer SRV windows");
    auto& pool = registry.pool(L"poolslot_range_metadata", device, PoolIndexType::RING, 1);
    CommandList fetch;
    append(fetch, L"poolslot_range_metadata[0:0]", L"ref ps-t[0:0]");
    set_slot(device, L'p', 0, view_a.Get()); execute(fetch, device);
    auto* element = pool.GetResource(0, false, true, false);
    if (element->resource != a.Get() || element->view != view_a.Get() || !element->GetHandleInfo())
        std::fprintf(stderr, "range fetch metadata: resource=%d source_view=%d snapshot=%d\n",
            element->resource == a.Get(), element->view == view_a.Get(), element->GetHandleInfo() != nullptr);
    require_batch(element->resource == a.Get() && element->view == view_a.Get() && element->GetHandleInfo(),
        "range: reference fetch preserves original SRV subview and captures current snapshot");
    const auto hash_a = GetRegionHash(device.wrapper.get(), a.Get(), 0, 12, element);
    element->source_stride = 999;
    set_slot(device, L'p', 0, view_b.Get()); execute(fetch, device);
    const auto hash_b = GetRegionHash(device.wrapper.get(), b.Get(), 0, 12, element);
    require_batch(element->resource == b.Get() && element->view == view_b.Get() && element->source_stride == 0 &&
        hash_a && hash_b && hash_a != hash_b && hash_b == GetRegionHash(device.wrapper.get(), b.Get(), 16, 12),
        "range: A-to-B fetch replaces snapshot/source_stride instead of reusing old buffer data");
    element->override_stride = 8; element->override_format = DXGI_FORMAT_R32_FLOAT;
    execute(fetch, device);
    require_batch(element->stride == 8 && element->format == DXGI_FORMAT_R32_FLOAT && element->source_stride == 8,
        "range: reference fetch honors destination out-of-band overrides");
    // Custom targets store resource metadata; they need not create a pipeline
    // view. Verify the real consumer builds its view from the overridden data.
    scalar(device, L"cs-t[26:27]", L"ref poolslot_range_metadata[0]");
    auto overridden = get_slot(device, L'c', 26);
    require_batch(overridden != nullptr, "range: actual consumer creates overridden SRV");
    D3D11_SHADER_RESOURCE_VIEW_DESC actual_override = {}; overridden->GetDesc(&actual_override);
    require_batch(actual_override.Format == DXGI_FORMAT_R32_FLOAT && overridden.Get() != view_b.Get(),
        "range: changed format override reaches actual consumer instead of retaining old UINT subview");
    element->override_stride = -1; element->override_format = static_cast<DXGI_FORMAT>(-1);
    element->max_copies_per_frame = 1; element->frame_no = G->frame_no; element->copies_this_frame = 1;
    set_slot(device, L'p', 0, view_a.Get()); execute(fetch, device);
    require_batch(element->resource == b.Get(), "range: reference fetch still enforces destination max_copies_per_frame");
    element->max_copies_per_frame = 0;
}

void pool_srv_writeback(BatchDevice& device, Registry& registry)
{
    device.context->ClearState();
    auto a = texture(device, 0x77442211u), b = texture(device, 0x99886633u);
    auto a_view = srv(device, a.Get()), b_view = srv(device, b.Get());
    registry.pool(L"poolslot_range_writeback", device, PoolIndexType::STATIC, 19);
    constexpr UINT null_slot = 4;
    ID3D11ShaderResourceView* original[19];
    for (UINT i = 0; i < 19; ++i) {
        original[i] = i == null_slot ? nullptr : i % 2 ? b_view.Get() : a_view.Get();
        set_slot(device, L'p', i, original[i]);
    }
    set_slot(device, L'p', 19, b_view.Get());

    CommandList save;
    append(save, L"poolslot_range_writeback[0:9]", L"ref ps-t[0:9]");
    append(save, L"poolslot_range_writeback[10:18]", L"ref ps-t[10:18]");
    const auto captured = execute(save, device);
    require_batch(captured.get_calls == 2 && captured.srv_slots_read == 19 && captured.set_calls == 0,
        "range writeback: capture both native SRV ranges into the pool");

    for (UINT i = 0; i < 19; ++i)
        set_slot(device, L'p', i, b_view.Get());
    CommandList restore;
    append(restore, L"ps-t[0:9]", L"ref poolslot_range_writeback[0:9]");
    append(restore, L"ps-t[10:18]", L"ref poolslot_range_writeback[10:18]");
    const auto restored = execute(restore, device);
    require_batch(restored.get_calls == 0 && restored.set_calls == 2,
        "range writeback: both complete ranges set once each without reading the old bindings");
    for (UINT i = 0; i < 19; ++i)
        require_batch(get_slot(device, L'p', i).Get() == original[i],
            "range writeback: preserve the exact saved view or explicitly clear a null slot");
    require_batch(get_slot(device, L'p', 19).Get() == b_view.Get(), "range writeback: slots outside the range stay bound");
    for (UINT i = 0; i < 19; ++i)
        set_slot(device, L'p', i, b_view.Get());
    G->mod_runtime_optimizations = false;
    const auto legacy = execute(restore, device);
    G->mod_runtime_optimizations = true;
    require_batch(legacy.get_calls == 2 && legacy.srv_slots_read == 19 && legacy.set_calls == 2,
        "runtime optimizations: legacy range path reads the complete original bindings");
    for (UINT i = 0; i < 19; ++i)
        require_batch(get_slot(device, L'p', i).Get() == original[i],
            "runtime optimizations: legacy and optimized range writeback bind the same views");

    for (UINT i = 0; i < 10; ++i)
        set_slot(device, L'p', i, b_view.Get());
    CommandList partial;
    append(partial, L"ps-t[0:9]", L"ref unless_null poolslot_range_writeback[0:9]");
    const auto preserved = execute(partial, device);
    require_batch(preserved.get_calls == 1 && preserved.srv_slots_read == 10 - null_slot && preserved.set_calls == 1,
        "range writeback: the first unless_null hole fetches only the remaining suffix");
    for (UINT i = 0; i < 10; ++i)
        require_batch(get_slot(device, L'p', i).Get() == (i == null_slot ? b_view.Get() : original[i]),
            "range writeback: preserve the hole and publish successful writes on both sides");
    device.context->ClearState();
}

void guarded_slotfix_cleanup(BatchDevice& device, Registry& registry)
{
    const wchar_t* name = L"commandlistslotfixtestcleanup";
    const std::wstring ns;
    require_batch(!explicitCommandListSections.count(name), "SlotFix guard: unique fixture");
    auto& cleanup = explicitCommandListSections[name];
    auto& pre = cleanup.command_list;
    auto& post = cleanup.post_command_list;
    CommandListScope scope;
    scope.emplace_front();
    pre.scope = post.scope = &scope;
    post.post = true;

    auto& saved_main = registry.variable(L"$slotfix_test_saved_main", 0).fval;
    auto& saved_glow = registry.variable(L"$slotfix_test_saved_glow", 0).fval;
    auto& post_marker = registry.variable(L"$slotfix_test_post", 0).fval;
    registry.pool(L"poolslotfix_test_cleanup", device, PoolIndexType::STATIC, 19);
    const auto flow = [&](std::wstring line) {
        require_batch(ParseCommandListFlowControl(name, &line, &pre, &post, &ns),
            "SlotFix guard: parse actual conditional");
    };
    const auto copy = [&](const wchar_t* destination, std::wstring source) {
        require_batch(ParseCommandListResourceCopyTargetDirective(name, destination, &source, &pre, &ns),
            "SlotFix guard: parse actual slot restore");
    };
    const auto set = [&](CommandList& list, const wchar_t* variable, std::wstring value) {
        require_batch(ParseCommandListVariableAssignment(name, variable, &value, nullptr, &list, &pre, &post, &ns),
            "SlotFix guard: parse actual assignment");
    };
    flow(L"if $slotfix_test_saved_main");
    copy(L"ps-t[0:9]", L"ref poolslotfix_test_cleanup[0:9]");
    set(pre, L"$slotfix_test_saved_main", L"0");
    flow(L"if $slotfix_test_saved_glow");
    copy(L"ps-t[10:18]", L"ref poolslotfix_test_cleanup[10:18]");
    set(pre, L"$slotfix_test_saved_glow", L"0");
    flow(L"endif");
    copy(L"poolslotfix_test_cleanup", L"null");
    flow(L"endif");
    set(post, L"$slotfix_test_post", L"$slotfix_test_post + 1");

    CommandList caller;
    std::wstring target(name);
    require_batch(ParseCommandListGeneralCommands(L"SlotFixGuardRegression", L"run", &target,
        &caller, &caller, &caller, &ns), "SlotFix guard: parse explicit post run");
    registered_command_lists.push_back(&pre);
    registered_command_lists.push_back(&post);
    optimise_command_lists(device.device);
    require_batch(pre.empty_if_false == &saved_main && !post.empty_if_false,
        "SlotFix guard: classify the pre phase independently of other post work");

    auto original = srv(device, texture(device, 0x33112244u).Get());
    auto replacement = srv(device, texture(device, 0x55667788u).Get());
    for (UINT slot = 0; slot <= 19; ++slot)
        set_slot(device, L'p', slot, slot == 4 ? nullptr : original.Get());

    const auto old_mode = Profiling::mode.exchange(Profiling::Mode::TOP_COMMAND_LISTS);
    const bool old_optimizations = G->mod_runtime_optimizations.exchange(true);
    const bool old_analysis = G->analyse_frame;
    const bool old_debug = gLogDebug;
    G->analyse_frame = gLogDebug = false;
    device.state.post = true;
    DrawCallInfo skipped;
    skipped.skip = true;
    device.state.call_info = &skipped;
    require_batch(execute(caller, device).set_calls == 0 && pre.executions == 0 && post_marker == 1,
        "SlotFix guard: unsaved cleanup skips the list and preserves other post commands");

    G->mod_runtime_optimizations = false;
    require_batch(execute(caller, device).set_calls == 0 && pre.executions == 1 && post_marker == 2,
        "SlotFix guard: compatibility switch retains the original empty cleanup dispatch");
    G->mod_runtime_optimizations = true;
    CommandList save;
    append(save, L"poolslotfix_test_cleanup[0:9]", L"ref ps-t[0:9]");
    append(save, L"poolslotfix_test_cleanup[10:18]", L"ref ps-t[10:18]");
    execute(save, device);
    saved_main = saved_glow = 1;
    for (UINT slot = 0; slot <= 19; ++slot)
        set_slot(device, L'p', slot, replacement.Get());
    execute(caller, device);
    for (UINT slot = 0; slot < 19; ++slot)
        require_batch(get_slot(device, L'p', slot).Get() == (slot == 4 ? nullptr : original.Get()),
            "SlotFix guard: a saved list restores the exact views even after a skipped draw");
    require_batch(!saved_main && !saved_glow && pre.executions == 2 && post_marker == 3 &&
        device.state.post && get_slot(device, L'p', 19).Get() == replacement.Get(),
        "SlotFix guard: reset save flags and leave adjacent slots and caller phase intact");
    require_batch(execute(caller, device).set_calls == 0 && pre.executions == 2 && post_marker == 4,
        "SlotFix guard: the next unsaved draw skips cleanup again");

    device.state.call_info = nullptr;
    device.state.post = false;
    device.context->ClearState();
    Profiling::clear();
    Profiling::mode = old_mode;
    G->mod_runtime_optimizations = old_optimizations;
    G->analyse_frame = old_analysis;
    gLogDebug = old_debug;
    explicitCommandListSections.erase(name);
}

void material_alias_metadata(BatchDevice& device, Registry& registry)
{
    auto ordinary = texture(device, 0x31527394u);
    auto& source = registry.resource(L"resourcealias_source", device, ordinary.Get());
    auto& diffuse = registry.resource(L"resourcealias_diffuse", device);
    auto& fx = registry.resource(L"resourcealias_fx", device);
    CommandList first, second;
    append(first, L"resourcealias_diffuse", L"ref resourcealias_source");
    append(second, L"resourcealias_fx", L"ref resourcealias_source");
    require_batch(execute(first, device).alias_metadata_hits == 0 && diffuse.resource == ordinary.Get() &&
        diffuse.format == DXGI_FORMAT_R8G8B8A8_UNORM && !diffuse.view,
        "material alias: cold metadata resolves the native texture format without an explicit view");
    source.match_dhash = 0x123456789abcdef0ull;
    source.match_dhash_set = true;
    require_batch(execute(second, device).alias_metadata_hits == 1 && fx.resource == ordinary.Get() &&
        fx.format == diffuse.format && fx.match_dhash == source.match_dhash && fx.match_dhash_set,
        "material alias: another alias reuses descriptors and receives the live match metadata");

    D3D11_TEXTURE2D_DESC desc{};
    ordinary->GetDesc(&desc);
    desc.MipLevels = 2;
    ComPtr<ID3D11Texture2D> mipped;
    require_batch(SUCCEEDED(device.native->CreateTexture2D(&desc, nullptr, &mipped)),
        "material alias: create native texture with two mips");
    auto whole_view = srv(device, mipped.Get());
    D3D11_SHADER_RESOURCE_VIEW_DESC mip_desc{};
    mip_desc.Format = desc.Format;
    mip_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    mip_desc.Texture2D.MipLevels = mip_desc.Texture2D.MostDetailedMip = 1;
    ComPtr<ID3D11ShaderResourceView> second_mip;
    require_batch(SUCCEEDED(device.native->CreateShaderResourceView(mipped.Get(), &mip_desc, &second_mip)),
        "material alias: create a distinct mip view");
    assign(device, &source, mipped.Get(), whole_view.Get());
    require_batch(execute(first, device).alias_metadata_hits == 0 && diffuse.view == whole_view.Get(),
        "material alias: source resource change invalidates the metadata");
    require_batch(execute(second, device).alias_metadata_hits == 1 && fx.view == whole_view.Get(),
        "material alias: validated source view is reused across aliases");
    assign(device, &source, mipped.Get(), second_mip.Get());
    require_batch(execute(first, device).alias_metadata_hits == 0 && diffuse.view == second_mip.Get(),
        "material alias: changing the view on the same resource preserves the new mip");
    assign(device, &source, nullptr);
    require_batch(execute(first, device).alias_metadata_hits == 0 && diffuse.is_null,
        "material alias: null source still clears the alias's visible value");

    auto buffer = tracked_buffer(device, 392);
    assign(device, &source, buffer.Get(), nullptr, 16, 64, DXGI_FORMAT_R32_UINT);
    execute(first, device);
    const auto prior_hash = GetRegionHash(device.wrapper.get(), buffer.Get(), 0, 16, &diffuse);
    require_batch(prior_hash != 0, "material alias: the tracked buffer alias captures a real snapshot");

    UINT changed[16];
    std::fill_n(changed, 16, 777u);
    device.wrapper->UpdateSubresource(buffer.Get(), 0, nullptr, changed, 0, 0);
    const auto new_hash = GetRegionHash(device.wrapper.get(), buffer.Get(), 16, 16);
    require_batch(new_hash && new_hash != prior_hash && execute(first, device).alias_metadata_hits == 1 &&
        GetRegionHash(device.wrapper.get(), buffer.Get(), 0, 16, &diffuse) == new_hash,
        "material alias: a descriptor hit still refreshes the tracked buffer snapshot after a GPU write");
    G->mod_runtime_optimizations = false;
    const auto legacy = execute(first, device);
    G->mod_runtime_optimizations = true;
    require_batch(legacy.alias_metadata_hits == 0 && diffuse.resource == buffer.Get() &&
        GetRegionHash(device.wrapper.get(), buffer.Get(), 0, 16, &diffuse) == new_hash,
        "runtime optimizations: legacy alias resolution preserves the current regional hash");

    source.offset = 32;
    fx.override_format = DXGI_FORMAT_R32_FLOAT;
    fx.override_stride = 8;
    require_batch(execute(second, device).alias_metadata_hits == 0 && fx.offset == 32 &&
        fx.format == DXGI_FORMAT_R32_FLOAT && fx.stride == 8,
        "material alias: offset and destination overrides take precedence over the previous metadata");
    fx.max_copies_per_frame = 1;
    fx.frame_no = G->frame_no;
    fx.copies_this_frame = 1;
    source.offset = 0;
    require_batch(execute(second, device).alias_metadata_hits == 0 && fx.offset == 32,
        "material alias: quota still rejects the assignment before metadata resolution");
}
} // namespace

void test_dx11_slot_range_regression()
{
    BatchDevice device;
    RangeGlobals globals;
    Registry registry;
    auto a = texture(device, 0x77442211u), b = texture(device, 0x99886633u);
    registry.resource(L"resourceslot_range_a", device, a.Get());
    auto& pool = registry.pool(L"poolslot_range_full", device, PoolIndexType::FIFO, 128);
    for (UINT i = 0; i < 128; ++i) assign(device, pool.GetResource(static_cast<float>(i), false, true, true), i % 2 ? b.Get() : a.Get());
    for (wchar_t stage : std::wstring(L"vhdgpc")) {
        const auto prefix = std::wstring(1, stage) + L"s-t";
        CommandList bind;
        append(bind, prefix + L"[0:127]", L"ref poolslot_range_full[0:127]");
        auto stats = execute(bind, device);
        require_batch(stats.get_calls == 0 && stats.set_calls == 1, "range: full 128-SRV bind has one real Set and no Get");
        for (UINT i = 0; i < 128; ++i)
            require_batch(bound_resource(device, stage, i).Get() == (i % 2 ? b.Get() : a.Get()), "range: every real SRV slot retains physical FIFO element identity");
        CommandList fetch;
        append(fetch, L"poolslot_range_full[0:127]", L"ref " + prefix + L"[0:127]");
        stats = execute(fetch, device);
        require_batch(stats.get_calls == 1 && stats.set_calls == 0, "range: full fetch has one real native Get");
    }
    CommandList copy;
    append(copy, L"ps-t[0:1]", L"copy resourceslot_range_a");
    execute(copy, device);
    require_pixels(device, bound_resource(device, L'p', 0).Get(), 0x77442211u);
    require_pixels(device, bound_resource(device, L'p', 1).Get(), 0x77442211u);
    require_batch(bound_resource(device, L'p', 0).Get() != a.Get(), "range: copy uses independent actual storage");
    CommandList rejected;
    require_batch(!parse(rejected, L"poolslot_range_full[0:1]", L"ref resourceslot_range_a"), "range: pool range requires slot range opposite");
    require_batch(!parse(rejected, L"ps-t", L"ref poolslot_range_full"), "range: bare sides require explicit bounds");
    buffer_views_and_lifetimes(device, registry);
    physical_keys_and_bounds(device, registry);
    full_uav_ranges(device, registry);
    all_copy_options(device, registry);
    fetch_metadata(device, registry);
    pool_srv_writeback(device, registry);
    guarded_slotfix_cleanup(device, registry);
    material_alias_metadata(device, registry);

}
