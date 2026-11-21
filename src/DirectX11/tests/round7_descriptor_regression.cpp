#include "slot_range_test_support.h"
#include "profiling.h"

namespace {
using namespace dx11_slot_test;
D3D11_TEXTURE2D_DESC texture_description(UINT width, UINT height, UINT mips, UINT slices, DXGI_FORMAT format)
{
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width; desc.Height = height; desc.MipLevels = mips; desc.ArraySize = slices;
    desc.Format = format; desc.SampleDesc.Count = 1; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    return desc;
}

void test_palette_buffer_copy(BatchDevice& device, Registry& registry)
{
    // The three float4 rows of each bone occupy 48 bytes in the INI, while
    // the actual SRV addresses 16-byte elements in a non-structured buffer.
    D3D11_BUFFER_DESC desc{2160, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0, 0};
    ComPtr<ID3D11Buffer> source;
    require_batch(SUCCEEDED(device.native->CreateBuffer(&desc, nullptr, &source)),
        "palette copy: create native buffer");
    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
    srv_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srv_desc.Buffer.FirstElement = 1;
    srv_desc.Buffer.NumElements = 134;
    ComPtr<ID3D11ShaderResourceView> source_view;
    require_batch(SUCCEEDED(device.native->CreateShaderResourceView(source.Get(), &srv_desc, &source_view)),
        "palette copy: initialize a typed view with a nonzero first element");
    set_slot(device, L'v', 0, source_view.Get());

    auto& target = registry.resource(L"resourcepalette_copy", device);
    target.override_stride = 48;
    target.override_array = 45;
    target.bind_flags = D3D11_BIND_SHADER_RESOURCE;
    CommandList copy;
    append(copy, target.name, L"copy vs-t0 unless_null");
    append(copy, L"cs-t0", L"ref " + target.name);
    const unsigned before = Profiling::resources_created;
    const unsigned copies = Profiling::resource_full_copies;
    execute(copy, device);
    ComPtr<ID3D11Resource> retained(target.resource);
    const auto first_view = get_slot(device, L'c', 0);
    execute(copy, device);
    require_batch(retained && target.resource == retained.Get() && Profiling::resources_created == before + 1,
        "palette copy: normalize native non-structured stride and reuse the allocation");
    require_batch(Profiling::resource_full_copies == copies + 2,
        "palette copy: reusing the destination allocation still copies each frame's data");
    const auto bound = get_slot(device, L'c', 0);
    require_batch(bound && bound.Get() == first_view.Get() && target.stride == 48,
        "palette copy: retain the typed SRV and the logical INI stride");
    D3D11_SHADER_RESOURCE_VIEW_DESC actual{};
    bound->GetDesc(&actual);
    require_batch(actual.Format == srv_desc.Format && actual.Buffer.FirstElement == srv_desc.Buffer.FirstElement &&
        actual.Buffer.NumElements == srv_desc.Buffer.NumElements,
        "palette copy: preserve the typed element window using the view stride");
    target.override_stride = 16;
    target.override_array = 135;
    execute(copy, device);
    require_batch(target.resource == retained.Get() && Profiling::resources_created == before + 1 && target.stride == 16,
        "palette copy: equivalent physical buffers reuse the allocation across logical stride changes");
    set_slot(device, L'v', 0, nullptr);
    set_slot(device, L'c', 0, nullptr);
}

void test_buffer_srv_validation(BatchDevice& device, Registry& registry)
{
    auto native = buffer(device, 48, D3D11_BIND_SHADER_RESOURCE);
    auto& source = registry.resource(L"resourceviewvalidation", device, native.Get());
    source.stride = 48;
    CommandList bind;
    append(bind, L"cs-t0", L"ref " + source.name);
    execute(bind, device);
    require_batch(!get_slot(device, L'c', 0), "buffer SRV: untyped ordinary buffer clears the slot");

    source.format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    source.offset = 48;
    execute(bind, device);
    require_batch(!get_slot(device, L'c', 0), "buffer SRV: out-of-range offset clears the slot");

    source.offset = 16;
    execute(bind, device);
    const auto view = get_slot(device, L'c', 0);
    require_batch(view != nullptr, "buffer SRV: valid metadata binds the same physical buffer");
    D3D11_SHADER_RESOURCE_VIEW_DESC actual{};
    view->GetDesc(&actual);
    require_batch(actual.Format == source.format && actual.Buffer.FirstElement == 1 && actual.Buffer.NumElements == 2,
        "buffer SRV: format width controls the element window");
    source.offset = 48;
    execute(bind, device);
    require_batch(!get_slot(device, L'c', 0), "buffer SRV: a prior valid view cannot bypass new invalid bounds");
    set_slot(device, L'c', 0, nullptr);
}

void test_invalid_creation_retry(BatchDevice& device, Registry& registry)
{
    auto native = texture(device, 0xff002244);
    auto& source = registry.resource(L"resourceretry_source", device, native.Get(), DXGI_FORMAT_R8G8B8A8_UNORM);
    auto& target = registry.resource(L"resourceretry_target", device);
    target.override_width = 1;
    target.override_height = 1;
    target.bind_flags = D3D11_BIND_SHADER_RESOURCE;
    CommandList copy;
    append(copy, target.name, L"copy_desc " + source.name);
    const unsigned before = Profiling::resources_created;
    for (const auto mips : {10, 11, 10, 11}) {
        target.override_mips = mips;
        execute(copy, device);
        require_batch(!target.resource, "allocation retry: the driver rejects invalid mip counts");
    }
    require_batch(Profiling::resources_created == before + 2,
        "allocation retry: an unchanged invalid request is temporarily suppressed, even when two descriptions alternate");
    target.override_mips = 1;
    execute(copy, device);
    require_batch(target.resource && Profiling::resources_created == before + 3,
        "allocation retry: a corrected description is allowed immediately");
}

void test_depth_snapshot_options(BatchDevice& device, Registry& registry)
{
    auto& source = registry.pool(L"pooldepthsnapshot_source", device, PoolIndexType::STATIC, 2);
    auto& destination = registry.pool(L"pooldepthsnapshot_destination", device, PoolIndexType::STATIC, 2);
    auto& snapshot = registry.resource(L"resourcesnapshot_options", device);
    CommandList rejected;
    require_batch(!parse(rejected, destination.name, L"ref " + source.name + L" reuse_unchanged"),
        "depth snapshot options: reject reuse_unchanged on a pool alias");
    require_batch(!parse(rejected, L"ps-t[20:21]", L"ref " + source.name + L"[0:1] reuse_unchanged"),
        "depth snapshot options: reject reuse_unchanged on a slot range");
    require_batch(!parse(rejected, snapshot.name, L"ref oD reuse_unchanged") &&
        !parse(rejected, snapshot.name, L"copy_desc oD reuse_unchanged") &&
        !parse(rejected, L"ps-t20", L"copy oD reuse_unchanged") &&
        !parse(rejected, destination.name + L"[0]", L"copy oD reuse_unchanged") && rejected.commands.empty(),
        "depth snapshot options: require an explicit full depth copy to an independent named resource");

    CommandList supported;
    append(supported, destination.name, L"ref " + source.name);
    append(supported, L"ps-t[20:21]", L"ref " + source.name + L"[0:1]");
    append(supported, snapshot.name, L"copy oD reuse_unchanged");
}

void test_depth_snapshot_reuse(BatchDevice& device, Registry& registry)
{
    auto desc = texture_description(4, 3, 1, 1, DXGI_FORMAT_R32_TYPELESS);
    desc.BindFlags |= D3D11_BIND_DEPTH_STENCIL;
    ComPtr<ID3D11Texture2D> depth;
    require_batch(SUCCEEDED(device.native->CreateTexture2D(&desc, nullptr, &depth)), "depth snapshot: create source");
    D3D11_DEPTH_STENCIL_VIEW_DESC view_desc{};
    view_desc.Format = DXGI_FORMAT_D32_FLOAT;
    view_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    ComPtr<ID3D11DepthStencilView> view;
    require_batch(SUCCEEDED(device.native->CreateDepthStencilView(depth.Get(), &view_desc, &view)), "depth snapshot: create DSV");
    device.context->OMSetRenderTargets(0, nullptr, view.Get());
    device.context->ClearDepthStencilView(view.Get(), D3D11_CLEAR_DEPTH, 0.25f, 0);

    auto& snapshot = registry.resource(L"resourcesnapshotdepth", device);
    snapshot.bind_flags = D3D11_BIND_SHADER_RESOURCE;
    auto& alias = registry.resource(L"resourcesnapshotdepthalias", device);
    CommandList copy, draw;
    append(copy, snapshot.name, L"copy oD unless_null reuse_unchanged");
    const std::wstring run = L"run = CommandListDepthSnapshot";
    LinkCommandLists(&draw, &copy, &run);
    append(draw, alias.name, L"ref " + snapshot.name);
    append(draw, L"ps-t20", L"null");
    LinkCommandLists(&draw, &copy, &run);

    DrawCallInfo draw_call(DrawCall::Draw, 3, 0, 0, 0, 0, 0, nullptr, 0);
    auto count_copies = [&] {
        unsigned before = Profiling::resource_full_copies;
        RunCommandList(device.device, device.wrapper.get(), &draw, &draw_call, false);
        return Profiling::resource_full_copies - before;
    };
    const unsigned initial_copies = count_copies();
    if (initial_copies != 1)
        std::fprintf(stderr, "depth snapshot first call count=%u destination=%p optimizations=%d frame_analysis=%d\n",
            initial_copies, static_cast<void*>(snapshot.resource), G->ModRuntimeOptimizationsEnabled(), (int)G->analyse_frame);
    require_batch(initial_copies == 1, "depth snapshot: repeat in the same draw list issues one actual copy");
    require_pixels(device, snapshot.resource, 0x3e800000); // 0.25f
    device.context->ClearDepthStencilView(view.Get(), D3D11_CLEAR_DEPTH, 0.5f, 0);
    require_batch(count_copies() == 1, "depth snapshot: new list does not reuse an earlier snapshot");
    require_pixels(device, snapshot.resource, 0x3f000000); // 0.5f

    CommandListScope scope;
    scope.emplace_front();
    draw.scope = &scope;
    std::wstring args = L"od depth 0.75";
    const std::wstring ini_namespace;
    require_batch(ParseCommandListGeneralCommands(L"CommandListDepthSnapshot", L"clear", &args,
        &draw, &draw, &draw, &ini_namespace), "depth snapshot: parse the intervening depth clear");
    draw.scope = nullptr;
    LinkCommandLists(&draw, &copy, &run);
    require_batch(count_copies() == 2, "depth snapshot: intervening clear requires a fresh copy");
    require_pixels(device, snapshot.resource, 0x3f400000); // 0.75f

    append(draw, snapshot.name, L"null");
    LinkCommandLists(&draw, &copy, &run);
    require_batch(count_copies() == 3, "depth snapshot: resetting the named resource requires a fresh copy");
    require_pixels(device, snapshot.resource, 0x3f400000);

    CommandList standard;
    append(standard, snapshot.name, L"copy oD unless_null");
    CommandList repeated;
    LinkCommandLists(&repeated, &standard, &run);
    LinkCommandLists(&repeated, &standard, &run);
    const unsigned before = Profiling::resource_full_copies;
    RunCommandList(device.device, device.wrapper.get(), &repeated, &draw_call, false);
    require_batch(Profiling::resource_full_copies - before == 2, "depth snapshot: ordinary copies preserve legacy behavior");
    require_pixels(device, snapshot.resource, 0x3f400000);
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
    test_palette_buffer_copy(device, registry);
    test_buffer_srv_validation(device, registry);
    test_invalid_creation_retry(device, registry);
    test_depth_snapshot_options(device, registry);
    test_depth_snapshot_reuse(device, registry);
}
