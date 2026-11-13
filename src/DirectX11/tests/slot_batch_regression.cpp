#include "CommandList.h"
#include "HackerContext.h"
#include "HackerDevice.h"
#include "globals.h"

#include <wrl/client.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
using Microsoft::WRL::ComPtr;

void require_batch(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

class BatchDevice {
public:
    HMODULE runtime = nullptr;
    ComPtr<ID3D11Device1> native;
    ComPtr<ID3D11DeviceContext1> context;
    HackerDevice* device = nullptr;
    std::unique_ptr<HackerContext> wrapper;
    CommandListState state;

    BatchDevice()
    {
        runtime = LoadLibraryExW(L"d3d11.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        require_batch(runtime != nullptr, "batch: load actual D3D11 runtime");
        auto create = reinterpret_cast<decltype(&D3D11CreateDevice)>(GetProcAddress(runtime, "D3D11CreateDevice"));
        require_batch(create != nullptr, "batch: resolve actual WARP device creator");
        ComPtr<ID3D11Device> base;
        ComPtr<ID3D11DeviceContext> immediate;
        require_batch(SUCCEEDED(create(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &base, nullptr, &immediate)), "batch: create actual WARP device");
        require_batch(SUCCEEDED(base.As(&native)) && SUCCEEDED(immediate.As(&context)), "batch: query D3D11.1");
        device = new HackerDevice(native.Get(), context.Get());
        native->AddRef();
        wrapper = std::make_unique<HackerContext>(native.Get(), context.Get());
        wrapper->SetHackerDevice(device);
        device->SetHackerContext(wrapper.get());
        device->SetHackerSwapChain(nullptr);
        state.mHackerDevice = device;
        state.mHackerContext = wrapper.get();
        state.mOrigDevice1 = native.Get();
        state.mOrigContext1 = context.Get();
    }

    ~BatchDevice()
    {
        wrapper->ClearState();
        wrapper.reset();
        context.Reset();
        native.Reset();
        device->Release();
        FreeLibrary(runtime);
    }
};

class BatchAliases {
public:
    std::vector<std::wstring> names;
    ~BatchAliases()
    {
        customResourcePools.erase(L"poolslotbatch_dynamic");
        for (const auto& name : names) customResources.erase(name);
    }

    CustomResource& add(const wchar_t* name, BatchDevice& device, ID3D11Resource* resource = nullptr)
    {
        require_batch(customResources.count(name) == 0, "batch: fixture resource name is unique");
        names.emplace_back(name);
        auto& alias = customResources[name];
        alias.name = name;
        alias.resource = resource;
        if (resource) resource->AddRef();
        alias.device = device.native.Get();
        alias.is_null = resource == nullptr;
        alias.substantiated = true;
        alias.bind_flags = D3D11_BIND_SHADER_RESOURCE;
        return alias;
    }
};

ComPtr<ID3D11Texture2D> texture(BatchDevice& device, UINT colour, UINT width = 4)
{
    std::vector<UINT> pixels(width * 3, colour);
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = 3;
    desc.ArraySize = desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial = {};
    initial.pSysMem = pixels.data();
    initial.SysMemPitch = width * sizeof(UINT);
    ComPtr<ID3D11Texture2D> result;
    require_batch(SUCCEEDED(device.native->CreateTexture2D(&desc, &initial, &result)), "batch: create actual populated texture");
    return result;
}

ComPtr<ID3D11ShaderResourceView> srv(BatchDevice& device, ID3D11Resource* resource)
{
    ComPtr<ID3D11ShaderResourceView> result;
    require_batch(SUCCEEDED(device.native->CreateShaderResourceView(resource, nullptr, &result)), "batch: create actual SRV");
    return result;
}

void set_slot(BatchDevice& device, wchar_t stage, UINT slot, ID3D11ShaderResourceView* view)
{
    switch (stage) {
    case L'v': device.context->VSSetShaderResources(slot, 1, &view); break;
    case L'h': device.context->HSSetShaderResources(slot, 1, &view); break;
    case L'd': device.context->DSSetShaderResources(slot, 1, &view); break;
    case L'g': device.context->GSSetShaderResources(slot, 1, &view); break;
    case L'p': device.context->PSSetShaderResources(slot, 1, &view); break;
    case L'c': device.context->CSSetShaderResources(slot, 1, &view); break;
    }
}

ComPtr<ID3D11ShaderResourceView> get_slot(BatchDevice& device, wchar_t stage, UINT slot)
{
    ComPtr<ID3D11ShaderResourceView> result;
    switch (stage) {
    case L'v': device.context->VSGetShaderResources(slot, 1, &result); break;
    case L'h': device.context->HSGetShaderResources(slot, 1, &result); break;
    case L'd': device.context->DSGetShaderResources(slot, 1, &result); break;
    case L'g': device.context->GSGetShaderResources(slot, 1, &result); break;
    case L'p': device.context->PSGetShaderResources(slot, 1, &result); break;
    case L'c': device.context->CSGetShaderResources(slot, 1, &result); break;
    }
    return result;
}

ComPtr<ID3D11Resource> bound_resource(BatchDevice& device, wchar_t stage, UINT slot)
{
    auto view = get_slot(device, stage, slot);
    ComPtr<ID3D11Resource> resource;
    if (view) view->GetResource(&resource);
    return resource;
}

bool parse(CommandList& list, std::wstring destination, std::wstring source)
{
    std::transform(destination.begin(), destination.end(), destination.begin(), ::towlower);
    std::transform(source.begin(), source.end(), source.begin(), ::towlower);
    const std::wstring ini_namespace;
    CommandListScope scope;
    scope.emplace_front();
    list.scope = &scope;
    const bool result = ParseCommandListResourceCopyTargetDirective(L"CommandListSlotBatchRegression",
        destination.c_str(), &source, &list, &ini_namespace);
    list.scope = nullptr;
    return result;
}

void append(CommandList& list, const std::wstring& destination, const std::wstring& source)
{
    require_batch(parse(list, destination, source), "batch: parse actual resource-copy directive");
}

ShaderResourceBatchStats execute(CommandList& list, BatchDevice& device)
{
    // These counters measure native calls issued by the batch helpers only.
    // They intentionally exclude test observations and standalone barriers.
    ShaderResourceBatchStats stats;
    device.state.slot_batch_stats = &stats;
    try {
        for (const auto& command : list.commands) command->run(&device.state);
    } catch (...) {
        device.state.slot_batch_stats = nullptr;
        throw;
    }
    device.state.slot_batch_stats = nullptr;
    return stats;
}

void require_pixels(BatchDevice& device, ID3D11Resource* resource, UINT colour)
{
    ComPtr<ID3D11Texture2D> original;
    require_batch(resource && SUCCEEDED(resource->QueryInterface(IID_PPV_ARGS(&original))), "batch: copied texture exists");
    D3D11_TEXTURE2D_DESC desc = {};
    original->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = desc.MiscFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    require_batch(SUCCEEDED(device.native->CreateTexture2D(&desc, nullptr, &staging)), "batch: create real staging readback");
    device.context->CopyResource(staging.Get(), resource);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    require_batch(SUCCEEDED(device.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "batch: map actual copy");
    bool equal = true;
    for (UINT y = 0; y < desc.Height; ++y) {
        const auto* row = reinterpret_cast<const UINT*>(static_cast<const unsigned char*>(mapped.pData) + mapped.RowPitch * y);
        for (UINT x = 0; x < desc.Width; ++x) equal &= row[x] == colour;
    }
    device.context->Unmap(staging.Get(), 0);
    require_batch(equal, "batch: all texture rows retain actual copied bytes");
}

void stages_and_order(BatchDevice& device, ID3D11Resource* a, ID3D11Resource* b, ID3D11ShaderResourceView* old)
{
    for (wchar_t stage : std::wstring(L"vhdgpc")) {
        device.context->ClearState();
        for (UINT slot : {0u, 1u, 2u, 3u, 4u, 5u, 127u}) set_slot(device, stage, slot, old);
        CommandList bind;
        const auto target = std::wstring(1, stage) + L"s-t";
        append(bind, target + L"127", L"ref resourceslotbatch_a");
        append(bind, target + L"0", L"ref resourceslotbatch_b");
        append(bind, target + L"127", L"ref resourceslotbatch_b");
        append(bind, target + L"2", L"unless_null ref resourceslotbatch_null");
        append(bind, target + L"3", L"null");
        append(bind, target + L"4", L"ref no_view_cache resourceslotbatch_a");
        append(bind, target + L"5", L"ref resourceslotbatch_null");
        merge_shader_resource_batches(&bind);
        require_batch(bind.commands.size() == 1 && dynamic_cast<ShaderResourceBindBatch*>(bind.commands.front().get()),
            "batch: unsorted/duplicate/gapped bind run forms one native span");
        auto calls = execute(bind, device);
        require_batch(calls.get_calls == 1 && calls.set_calls == 1, "batch: one actual native Get/Set for seven bind operations");
        require_batch(bound_resource(device, stage, 0).Get() == b && bound_resource(device, stage, 127).Get() == b,
            "batch: slot 0 and 127 bind; duplicate last writer wins");
        require_batch(get_slot(device, stage, 1).Get() == old && get_slot(device, stage, 2).Get() == old,
            "batch: gaps and unless_null preserve exact original SRVs");
        require_batch(!get_slot(device, stage, 3) && !get_slot(device, stage, 5) && bound_resource(device, stage, 4).Get() == a,
            "batch: explicit/missing null and no_view_cache keep original publication semantics");

        // Source slots descend then return to an earlier slot. Sorting by source
        // would change the final custom destination from A into B.
        set_slot(device, stage, 0, old);
        auto view_b = srv(device, b);
        set_slot(device, stage, 127, view_b.Get());
        CommandList fetch;
        append(fetch, L"resourceslotbatch_result", L"ref " + target + L"127");
        append(fetch, L"resourceslotbatch_result", L"ref " + target + L"0");
        append(fetch, L"resourceslotbatch_result", L"ref " + target + L"127");
        append(fetch, L"resourceslotbatch_result", L"ref " + target + L"0");
        merge_shader_resource_batches(&fetch);
        require_batch(fetch.commands.size() == 1 && dynamic_cast<ShaderResourceFetchBatch*>(fetch.commands.front().get()),
            "batch: inverse/gapped fetch remains one ordered batch");
        calls = execute(fetch, device);
        require_batch(calls.get_calls == 1 && calls.set_calls == 0, "batch: fetch performs one actual native Get and no native Set");
        require_batch(customResources.at(L"resourceslotbatch_result").resource == a,
            "batch: original INI fetch order determines final aliased destination");
    }
}

void lazy_seed_cases(BatchDevice& device, ID3D11Resource* a,
    ID3D11Resource* b, ID3D11ShaderResourceView* old)
{
    device.context->ClearState();
    set_slot(device, L'p', 0, old);
    set_slot(device, L'p', 1, old);
    CommandList contiguous;
    append(contiguous, L"ps-t0", L"ref resourceslotbatch_a");
    append(contiguous, L"ps-t1", L"ref resourceslotbatch_b");
    merge_shader_resource_batches(&contiguous);
    auto calls = execute(contiguous, device);
    require_batch(calls.get_calls == 0 && calls.set_calls == 1 &&
        bound_resource(device, L'p', 0).Get() == a && bound_resource(device, L'p', 1).Get() == b,
        "batch: two successful consecutive binds issue zero helper Gets and one native helper Set");

    set_slot(device, L'p', 0, old);
    set_slot(device, L'p', 1, old);
    CommandList skipped;
    append(skipped, L"ps-t0", L"unless_null ref resourceslotbatch_null");
    append(skipped, L"ps-t1", L"unless_null ref resourceslotbatch_null");
    merge_shader_resource_batches(&skipped);
    calls = execute(skipped, device);
    require_batch(calls.get_calls == 0 && calls.set_calls == 0 &&
        get_slot(device, L'p', 0).Get() == old && get_slot(device, L'p', 1).Get() == old,
        "batch: entirely skipped unless_null performs no helper API calls and preserves exact old views");

    CommandList gap;
    append(gap, L"ps-t0", L"ref resourceslotbatch_b");
    append(gap, L"ps-t2", L"ref resourceslotbatch_a");
    merge_shader_resource_batches(&gap);
    calls = execute(gap, device);
    require_batch(calls.get_calls == 1 && calls.set_calls == 1 && get_slot(device, L'p', 1).Get() == old &&
        bound_resource(device, L'p', 0).Get() == b && bound_resource(device, L'p', 2).Get() == a,
        "batch: a true span gap lazily fetches original bindings once and keeps exact gap identity");

    CommandList duplicate;
    append(duplicate, L"ps-t0", L"ref resourceslotbatch_b");
    append(duplicate, L"ps-t0", L"unless_null ref resourceslotbatch_null");
    merge_shader_resource_batches(&duplicate);
    calls = execute(duplicate, device);
    require_batch(calls.get_calls == 0 && calls.set_calls == 1 && bound_resource(device, L'p', 0).Get() == b,
        "batch: a skipped duplicate keeps the successful new binding rather than restoring the original");

    // Directly exercise the sink's non-publishing failure path with a real
    // Region command on a texture. Region validation rejects non-buffers.
    // The merge pass normally leaves Region operations as barriers; this
    // narrow run-method test does not claim that it batches such commands.
    set_slot(device, L'p', 1, old);
    CommandList failed;
    append(failed, L"ps-t0", L"ref resourceslotbatch_b");
    append(failed, L"ps-t1", L"ref resourceslotbatch_a->region(0,16)");
    auto failure_batch = std::make_shared<ShaderResourceBindBatch>();
    failure_batch->shader_type = L'p';
    failure_batch->first_slot = 0;
    failure_batch->count = 2;
    for (const auto& command : failed.commands) {
        auto operation = std::dynamic_pointer_cast<ResourceCopyOperation>(command);
        require_batch(operation != nullptr, "batch: retain real production resource-copy failure command");
        failure_batch->operations.push_back(std::move(operation));
    }
    failed.commands.assign(1, failure_batch);
    calls = execute(failed, device);
    require_batch(calls.get_calls == 1 && calls.set_calls == 1 && get_slot(device, L'p', 1).Get() == old &&
        bound_resource(device, L'p', 0).Get() == b,
        "batch: a real non-publishing validation failure lazily restores its exact old view");
}

void options_and_barriers(BatchDevice& device, ID3D11Resource* a, ID3D11ShaderResourceView* old)
{
    device.context->ClearState();
    CommandList copy;
    append(copy, L"ps-t0", L"copy resourceslotbatch_a");
    append(copy, L"ps-t1", L"ref mono resourceslotbatch_a");
    append(copy, L"ps-t2", L"copy_desc resourceslotbatch_a");
    merge_shader_resource_batches(&copy);
    auto calls = execute(copy, device);
    require_batch(calls.get_calls == 0 && calls.set_calls == 1, "batch: successful contiguous copy/ref/copy_desc need no original binding fetch");
    auto first = bound_resource(device, L'p', 0);
    require_batch(first && first.Get() != a && bound_resource(device, L'p', 1).Get() == a,
        "batch: copy has independent allocation while ref preserves identity");
    require_pixels(device, first.Get(), 0x77442211u);
    ComPtr<ID3D11Texture2D> description;
    auto desc_resource = bound_resource(device, L'p', 2);
    require_batch(desc_resource && SUCCEEDED(desc_resource.As(&description)), "batch: copy_desc creates actual texture");
    D3D11_TEXTURE2D_DESC desc = {};
    description->GetDesc(&desc);
    require_batch(desc.Width == 4 && desc.Height == 3, "batch: copy_desc retains actual dimensions");
    execute(copy, device);
    require_batch(bound_resource(device, L'p', 0).Get() == first.Get(), "batch: repeated execution retains production copy cache");

    CommandList barrier;
    append(barrier, L"ps-t0", L"ref resourceslotbatch_a");
    append(barrier, L"ps-t1", L"ref resourceslotbatch_a");
    append(barrier, L"ps-t2", L"ref set_viewport resourceslotbatch_b");
    append(barrier, L"ps-t3", L"ref resourceslotbatch_a");
    append(barrier, L"ps-t4", L"ref resourceslotbatch_a");
    merge_shader_resource_batches(&barrier);
    require_batch(barrier.commands.size() == 3, "batch: viewport command stays in its original execution boundary");
    calls = execute(barrier, device);
    require_batch(calls.get_calls == 0 && calls.set_calls == 2, "batch: successful safe runs on both sides of viewport need only two batch Sets");
    D3D11_VIEWPORT viewport = {};
    UINT count = 1;
    device.context->RSGetViewports(&count, &viewport);
    require_batch(count == 1 && viewport.Width == 7 && viewport.Height == 3, "batch: original viewport side effect survives");

    CommandList failure;
    auto& invalid = customResources.at(L"resourceslotbatch_result");
    const auto saved_format = invalid.override_format;
    invalid.override_format = static_cast<DXGI_FORMAT>(0x7fffffff);
    append(failure, L"resourceslotbatch_result", L"copy ps-t0");
    append(failure, L"resourceslotbatch_result", L"copy ps-t1");
    merge_shader_resource_batches(&failure);
    execute(failure, device); // Failed resource/view creation must release prefetched refs safely.
    invalid.override_format = saved_format;

    CommandList unsupported;
    require_batch(!parse(unsupported, L"ps-t0", L"overwrite resourceslotbatch_a"),
        "batch: unsupported overwrite stays rejected rather than invented");

    // Two independent devices execute the SAME operations. Null operations have
    // no mutable copy cache, so this specifically exercises per-invocation sinks.
    BatchDevice second;
    auto second_texture = texture(second, 0x12345678);
    auto second_view = srv(second, second_texture.Get());
    set_slot(device, L'p', 0, old);
    set_slot(second, L'p', 0, second_view.Get());
    set_slot(second, L'p', 1, second_view.Get());
    CommandList clear;
    append(clear, L"ps-t0", L"null");
    append(clear, L"ps-t2", L"null");
    merge_shader_resource_batches(&clear);
    execute(clear, device);
    require_batch(get_slot(second, L'p', 0).Get() == second_view.Get(), "batch: first invocation does not publish into other context");
    execute(clear, second);
    require_batch(!get_slot(second, L'p', 0) && !get_slot(second, L'p', 2) &&
        get_slot(second, L'p', 1).Get() == second_view.Get(), "batch: shared operations use second context and retain its gap");
}

void raw_and_resolve(BatchDevice& device, BatchAliases& aliases)
{
    const UINT words[] = {0x01020304, 0xfedcba98, 0x12345678, 0xabcdef01};
    D3D11_BUFFER_DESC buffer_desc = {};
    buffer_desc.ByteWidth = sizeof(words);
    buffer_desc.Usage = D3D11_USAGE_DEFAULT;
    buffer_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    buffer_desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
    D3D11_SUBRESOURCE_DATA initial = {};
    initial.pSysMem = words;
    ComPtr<ID3D11Buffer> buffer;
    require_batch(SUCCEEDED(device.native->CreateBuffer(&buffer_desc, &initial, &buffer)), "batch: create real raw buffer");
    auto& raw = aliases.add(L"resourceslotbatch_raw", device, buffer.Get());
    raw.misc_flags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
    CommandList raw_commands;
    append(raw_commands, L"cs-t10", L"ref raw no_view_cache resourceslotbatch_raw");
    append(raw_commands, L"cs-t11", L"ref raw resourceslotbatch_raw");
    merge_shader_resource_batches(&raw_commands);
    auto calls = execute(raw_commands, device);
    auto view = get_slot(device, L'c', 10);
    require_batch(calls.get_calls == 0 && calls.set_calls == 1 && view,
        "batch: raw and no_view_cache options retain actual merged publication");
    D3D11_SHADER_RESOURCE_VIEW_DESC raw_desc = {};
    view->GetDesc(&raw_desc);
    require_batch(raw_desc.ViewDimension == D3D11_SRV_DIMENSION_BUFFEREX &&
        raw_desc.Format == DXGI_FORMAT_R32_TYPELESS && raw_desc.BufferEx.NumElements == 4 &&
        (raw_desc.BufferEx.Flags & D3D11_BUFFEREX_SRV_FLAG_RAW), "batch: real SRV retains raw format/extent/flag");
    require_batch(bound_resource(device, L'c', 10).Get() == buffer.Get(), "batch: raw reference retains physical buffer");

    UINT quality = 0;
    require_batch(SUCCEEDED(device.native->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, 2, &quality)) && quality,
        "batch: actual WARP supports required 2x MSAA resolve test");
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = 4;
    desc.Height = 3;
    desc.MipLevels = desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 2;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> multisample;
    require_batch(SUCCEEDED(device.native->CreateTexture2D(&desc, nullptr, &multisample)), "batch: create actual MSAA source");
    ComPtr<ID3D11RenderTargetView> target;
    require_batch(SUCCEEDED(device.native->CreateRenderTargetView(multisample.Get(), nullptr, &target)), "batch: create MSAA clear target");
    const float colour[] = {1, 0, 1, 1};
    device.context->ClearRenderTargetView(target.Get(), colour);
    aliases.add(L"resourceslotbatch_msaa", device, multisample.Get());
    CommandList resolve;
    append(resolve, L"ps-t10", L"resolve_msaa resourceslotbatch_msaa");
    append(resolve, L"ps-t11", L"resolve_msaa resourceslotbatch_msaa");
    merge_shader_resource_batches(&resolve);
    calls = execute(resolve, device);
    require_batch(calls.get_calls == 0 && calls.set_calls == 1, "batch: successful contiguous resolve_msaa needs only the final Set");
    require_pixels(device, bound_resource(device, L'p', 10).Get(), 0xffff00ffu);
    require_pixels(device, bound_resource(device, L'p', 11).Get(), 0xffff00ffu);
}

void dynamic_pool_barrier(BatchDevice& device, BatchAliases& aliases, ID3D11Resource* a)
{
    const std::wstring pool_name = L"poolslotbatch_dynamic";
    require_batch(!customResourcePools.count(pool_name), "batch: pool fixture name is unique");
    auto& pool = customResourcePools[pool_name];
    pool.name = pool_name;
    auto& resource = aliases.add(L"resourceslotbatch_template", device, a);
    resource.pool = &pool;
    resource.pool_index = -1;
    pool.resource_template = &resource;
    pool.variable_template = std::make_unique<CommandListVariable>(L"slotbatch_template", 0, VariableFlags::NONE);
    pool.index_type = PoolIndexType::FIFO;
    pool.Initialize(2);
    CommandList dynamic;
    append(dynamic, L"ps-t0", L"ref resourceslotbatch_a");
    append(dynamic, L"ps-t1", L"ref resourceslotbatch_a");
    // This index observes the actual pipeline state written by the previous run.
    append(dynamic, L"ps-t2", L"ref poolslotbatch_dynamic[ps-t0->width]");
    append(dynamic, L"ps-t3", L"ref resourceslotbatch_a");
    append(dynamic, L"ps-t4", L"ref resourceslotbatch_a");
    merge_shader_resource_batches(&dynamic);
    require_batch(dynamic.commands.size() == 3 && dynamic_cast<ResourceCopyOperation*>(dynamic.commands[1].get()),
        "batch: dynamic pipeline-dependent pool source is a real execution barrier");
    auto calls = execute(dynamic, device);
    require_batch(calls.get_calls == 0 && calls.set_calls == 2 && bound_resource(device, L'p', 2).Get() == a,
        "batch: dynamic pool read observes flushed slots and returns template without losing safe batching");

    CommandList constants;
    append(constants, L"ps-t5", L"ref poolslotbatch_dynamic[1+1]");
    append(constants, L"ps-t6", L"ref poolslotbatch_dynamic[2]");
    merge_shader_resource_batches(&constants);
    require_batch(constants.commands.size() == 1 && dynamic_cast<ShaderResourceBindBatch*>(constants.commands[0].get()),
        "batch: constant-index pool reads retain real batching");
    calls = execute(constants, device);
    require_batch(calls.get_calls == 0 && calls.set_calls == 1 && bound_resource(device, L'p', 6).Get() == a,
        "batch: static-index pool template binds correctly");
}
} // namespace

void test_dx11_slot_batch_regression()
{
    std::fputs("Testing real six-stage SRV slot batching, ordering and barriers\n", stderr);
    BatchDevice device;
    BatchAliases aliases;
    auto a = texture(device, 0x77442211u);
    auto b = texture(device, 0x99886633u, 7);
    auto old = srv(device, a.Get());
    aliases.add(L"resourceslotbatch_a", device, a.Get());
    aliases.add(L"resourceslotbatch_b", device, b.Get());
    aliases.add(L"resourceslotbatch_null", device);
    aliases.add(L"resourceslotbatch_result", device);
    stages_and_order(device, a.Get(), b.Get(), old.Get());
    lazy_seed_cases(device, a.Get(), b.Get(), old.Get());
    options_and_barriers(device, a.Get(), old.Get());
    raw_and_resolve(device, aliases);
    dynamic_pool_barrier(device, aliases, a.Get());
    std::fputs("PASS: real six-stage SRV batching, ordered side effects and lazy API publication\n", stderr);
}
