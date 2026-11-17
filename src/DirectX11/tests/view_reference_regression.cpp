#include "slot_range_test_support.h"
#include "cache_policy_test_scope.h"
#include <D3Dcompiler.h>
using namespace dx11_slot_test;

namespace {
ComPtr<ID3D11Texture2D> array_texture(BatchDevice& device, DXGI_FORMAT format, UINT flags)
{
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = desc.Height = 8; desc.MipLevels = 2; desc.ArraySize = 2;
    desc.Format = format; desc.SampleDesc.Count = 1; desc.BindFlags = flags;
    ComPtr<ID3D11Texture2D> result;
    require_batch(SUCCEEDED(device.native->CreateTexture2D(&desc, nullptr, &result)), "ref: actual array/mip resource");
    return result;
}

ComPtr<ID3D11Resource> view_resource(ID3D11View* view)
{
    ComPtr<ID3D11Resource> result;
    if (view) view->GetResource(&result);
    return result;
}

D3D11_SHADER_RESOURCE_VIEW_DESC describe_roundtrip_srv(const ComPtr<ID3D11ShaderResourceView>& view)
{
    require_batch(view != nullptr, "ref: scalar roundtrip must bind a CS SRV before reading its description");
    D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
    view->GetDesc(&desc);
    return desc;
}

void view_kinds(BatchDevice& device, Registry& registry)
{
    auto native = array_texture(device, DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS);
    D3D11_SHADER_RESOURCE_VIEW_DESC desc = {};
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    desc.Texture2DArray.MostDetailedMip = 1; desc.Texture2DArray.MipLevels = 1;
    desc.Texture2DArray.FirstArraySlice = 1; desc.Texture2DArray.ArraySize = 1;
    ComPtr<ID3D11ShaderResourceView> original;
    require_batch(SUCCEEDED(device.native->CreateShaderResourceView(native.Get(), &desc, &original)), "ref: actual sliced/mipped SRV");
    registry.resource(L"resourceref_view_hold", device);
    set_slot(device, L'p', 6, original.Get());
    scalar(device, L"resourceref_view_hold", L"ref ps-t6");
    auto& hold = customResources.at(L"resourceref_view_hold");
    require_batch(hold.view == original.Get(), "ref: ordinary custom reference retains original exact SRV");
    device.context->ClearState();
    scalar(device, L"cs-t7", L"ref resourceref_view_hold");
    auto rebound = get_slot(device, L'c', 7);
    const auto actual = describe_roundtrip_srv(rebound);
    require_batch(rebound.Get() == original.Get() && actual.Texture2DArray.MostDetailedMip == 1 &&
        actual.Texture2DArray.FirstArraySlice == 1 && actual.Texture2DArray.ArraySize == 1,
        "ref: scalar roundtrip retains array/mip subview rather than widening to full resource");
    device.context->ClearState();
    scalar(device, L"cs-u2", L"ref resourceref_view_hold");
    ComPtr<ID3D11UnorderedAccessView> uav;
    device.context->CSGetUnorderedAccessViews(2, 1, &uav);
    require_batch(uav && view_resource(uav.Get()).Get() == native.Get() &&
        static_cast<ID3D11View*>(uav.Get()) != original.Get(), "ref: wrong-kind SRV source recreates a real UAV of the same resource");

    // One THIS operation sees different concrete kinds on successive calls.
    CommandList shared;
    append(shared, L"this", L"ref resourceref_view_hold");
    ResourceCopyTarget target;
    const std::wstring ns;
    for (const auto* text : {L"cs-t8", L"cs-u3", L"o1"}) {
        device.context->ClearState();
        require_batch(target.ParseTarget(text, false, &ns, nullptr), "ref: parse real concrete THIS destination");
        device.state.this_target = &target;
        execute(shared, device);
        if (target.type == ResourceCopyTargetType::SHADER_RESOURCE)
            require_batch(get_slot(device, L'c', 8).Get() == original.Get(), "ref: THIS SRV retains source kind");
        else if (target.type == ResourceCopyTargetType::UNORDERED_ACCESS_VIEW) {
            ComPtr<ID3D11UnorderedAccessView> actual_uav;
            device.context->CSGetUnorderedAccessViews(3, 1, &actual_uav);
            require_batch(actual_uav && view_resource(actual_uav.Get()).Get() == native.Get(), "ref: THIS cached view matches UAV kind");
        } else {
            ComPtr<ID3D11RenderTargetView> views[2];
            ID3D11RenderTargetView* raw[2] = {};
            device.context->OMGetRenderTargets(2, raw, nullptr);
            for (UINT i = 0; i < 2; ++i) views[i].Attach(raw[i]);
            require_batch(views[1] && view_resource(views[1].Get()).Get() == native.Get(), "ref: THIS rejects old UAV cached view and creates RTV");
        }
    }
    device.state.this_target = nullptr;
    device.context->ClearState();
    D3D11_RENDER_TARGET_VIEW_DESC rtv_desc = {};
    rtv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
    rtv_desc.Texture2DArray.MipSlice = 1; rtv_desc.Texture2DArray.FirstArraySlice = 1; rtv_desc.Texture2DArray.ArraySize = 1;
    ComPtr<ID3D11RenderTargetView> rtv;
    require_batch(SUCCEEDED(device.native->CreateRenderTargetView(native.Get(), &rtv_desc, &rtv)), "ref: actual sliced RTV");
    ID3D11RenderTargetView* raw_rtv = rtv.Get(); device.context->OMSetRenderTargets(1, &raw_rtv, nullptr);
    registry.resource(L"resourceref_rtv_hold", device);
    scalar(device, L"resourceref_rtv_hold", L"ref o0");
    device.context->ClearState(); scalar(device, L"o0", L"ref resourceref_rtv_hold");
    ComPtr<ID3D11RenderTargetView> rebound_rtv; device.context->OMGetRenderTargets(1, &rebound_rtv, nullptr);
    require_batch(rebound_rtv.Get() == rtv.Get(), "ref: RTV reference roundtrip retains exact subview");

    auto depth = array_texture(device, DXGI_FORMAT_R32_TYPELESS, D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE);
    D3D11_DEPTH_STENCIL_VIEW_DESC depth_desc = {};
    depth_desc.Format = DXGI_FORMAT_D32_FLOAT; depth_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
    depth_desc.Texture2DArray.MipSlice = 1; depth_desc.Texture2DArray.FirstArraySlice = 1; depth_desc.Texture2DArray.ArraySize = 1;
    ComPtr<ID3D11DepthStencilView> dsv;
    require_batch(SUCCEEDED(device.native->CreateDepthStencilView(depth.Get(), &depth_desc, &dsv)), "ref: actual sliced DSV");
    device.context->ClearState(); device.context->OMSetRenderTargets(0, nullptr, dsv.Get());
    registry.resource(L"resourceref_dsv_hold", device); scalar(device, L"resourceref_dsv_hold", L"ref od");
    device.context->ClearState(); scalar(device, L"od", L"ref resourceref_dsv_hold");
    ComPtr<ID3D11DepthStencilView> rebound_dsv; device.context->OMGetRenderTargets(0, nullptr, &rebound_dsv);
    require_batch(rebound_dsv.Get() == dsv.Get(), "ref: depth view roundtrip retains actual DSV kind/window");
}

void structured_to_cb_and_flags(BatchDevice& device, Registry& registry)
{
    auto structured = buffer(device, 64, D3D11_BIND_SHADER_RESOURCE, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 16);
    registry.resource(L"resourceref_structure", device, structured.Get(), DXGI_FORMAT_UNKNOWN, 16, 64);
    scalar(device, L"vs-cb0", L"copy resourceref_structure");
    UINT first = 0, count = 0;
    auto actual = get_cb(device, L'v', 0, first, count);
    D3D11_BUFFER_DESC desc = {}; require_batch(actual != nullptr, "ref: structured-to-CB copy creates actual CB");
    actual->GetDesc(&desc);
    require_batch(desc.StructureByteStride == 0 && !(desc.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED) &&
        desc.ByteWidth == 64 && first == 0, "ref: actual CB descriptor clears source structure representation and stride");
    desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = desc.MiscFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Buffer> staging;
    require_batch(SUCCEEDED(device.native->CreateBuffer(&desc, nullptr, &staging)), "ref: real copied-CB staging");
    device.context->CopyResource(staging.Get(), actual.Get());
    D3D11_MAPPED_SUBRESOURCE map = {};
    require_batch(SUCCEEDED(device.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map)), "ref: map actual structured-to-CB result");
    bool same = true;
    for (UINT i = 0; i < 64; ++i) same &= static_cast<const unsigned char*>(map.pData)[i] == static_cast<unsigned char>(i * 37 + 11);
    device.context->Unmap(staging.Get(), 0); require_batch(same, "ref: all structured source bytes copied to CB");

    auto& pool = registry.pool(L"poolref_flags", device, PoolIndexType::FIFO, 2);
    pool.allocate_slot_on_missing = true; pool.read_refreshes_expiration = true;
    pool.resource_template->bind_flags = D3D11_BIND_SHADER_RESOURCE;
    auto& index = registry.variable(L"$ref_flags_index", 77);
    ResourceCopyTarget target; const std::wstring ns, text = L"poolref_flags[$ref_flags_index]";
    CommandListScope scope;
    require_batch(target.ParseTarget(text.c_str(), false, &ns, &scope), "ref: parse dynamic pool destination flags");
    const auto before_resources = customResources.size();
    D3D11_RESOURCE_MISC_FLAG misc = static_cast<D3D11_RESOURCE_MISC_FLAG>(0);
    unsigned untouched = 1234;
    require_batch(target.BindFlags(&device.state, &misc) == D3D11_BIND_SHADER_RESOURCE && customResources.size() == before_resources &&
        !pool.GetLastUpdateFrame(77, false, &untouched) && untouched == 1234,
        "ref: querying dynamic destination flags inspects template without allocating a missing element");
    (void)index;
}
} // namespace

void test_dx11_view_reference_regression()
{
    BatchDevice device; Registry registry;
    view_kinds(device, registry);
    structured_to_cb_and_flags(device, registry);
}

void test_dx11_view_reference_null_diagnostic_regression()
{
    BatchDevice device;
    device.context->ClearState();
    auto empty = get_slot(device, L'c', 7);
    require_batch(!empty, "F298: actual cleared CS slot returns a null SRV");
    std::fputs("F298: describe an actual empty slot through the ordinary regression consumer\n", stderr); std::fflush(stderr);
    bool diagnosed = false;
    try { describe_roundtrip_srv(empty); }
    catch (const std::runtime_error& error) {
        std::fprintf(stderr, "F298 caught diagnostic: %s\n", error.what());
        diagnosed = std::string(error.what()).find("scalar roundtrip must bind a CS SRV") != std::string::npos;
    }
    require_batch(diagnosed, "F298: missing SRV must produce the existing diagnostic exception before GetDesc");
    Registry registry;
    view_kinds(device, registry);
}

namespace {
// F-112 (A): AddRef() returns the new count; subtract the probe reference to
// report the object's actual reference count.
ULONG srv_refs(ID3D11ShaderResourceView *view)
{
    const ULONG probe = view->AddRef();
    view->Release();
    return probe - 1;
}

struct ZBufferHashScope
{
    uint32_t saved = G->ZBufferHashToInject;
    explicit ZBufferHashScope(uint32_t hash) { G->ZBufferHashToInject = hash; }
    ~ZBufferHashScope() { G->ZBufferHashToInject = saved; }
};

ComPtr<ID3D11Texture2D> z_depth_texture(BatchDevice &device)
{
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = desc.Height = 8;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R32_TYPELESS;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> texture;
    require_batch(SUCCEEDED(device.device->CreateTexture2D(&desc, nullptr, &texture)),
        "F112: wrapped depth texture creation");
    return texture;
}

ComPtr<ID3D11ShaderResourceView> z_srv(BatchDevice &device, ID3D11Texture2D *texture)
{
    D3D11_SHADER_RESOURCE_VIEW_DESC desc = {};
    desc.Format = DXGI_FORMAT_R32_FLOAT;
    desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    desc.Texture2D.MipLevels = 1;
    ComPtr<ID3D11ShaderResourceView> view;
    require_batch(SUCCEEDED(device.device->CreateShaderResourceView(texture, &desc, &view)),
        "F112: wrapped SRV creation");
    return view;
}

ComPtr<ID3D11PixelShader> z_test_pixel_shader(BatchDevice &device)
{
    const char hlsl[] = "float4 main():SV_Target{return float4(0.25,0.5,0.75,1);}";
    ComPtr<ID3DBlob> bytecode, errors;
    require_batch(SUCCEEDED(D3DCompile(hlsl, sizeof(hlsl) - 1, "ZBufferLease", nullptr, nullptr,
        "main", "ps_5_0", 0, 0, &bytecode, &errors)), "F112: compile pixel shader");
    ComPtr<ID3D11PixelShader> shader;
    require_batch(SUCCEEDED(device.device->CreatePixelShader(bytecode->GetBufferPointer(),
        bytecode->GetBufferSize(), nullptr, &shader)), "F112: wrapped pixel shader creation");
    return shader;
}

ComPtr<ID3D11ShaderResourceView> ps_slot_126(BatchDevice &device)
{
    ComPtr<ID3D11ShaderResourceView> slot;
    device.context->PSGetShaderResources(126, 1, &slot);
    return slot;
}
} // namespace

void test_dx11_zbuffer_view_ownership_regression()
{
    BatchDevice device;
    auto depth = z_depth_texture(device);
    const uint32_t hash = GetResourceHash(depth.Get());
    require_batch(hash != 0, "F112: wrapped depth texture is registered with a hash");
    ZBufferHashScope inject(hash);
    auto shader = z_test_pixel_shader(device);

    // 1) Capture: print the baseline reference count of a fresh SRV, then the
    // count of the captured SRV (baseline + 1 held by HackerDevice).
    {
        ZBufferHashScope control_hash(0);
        auto control = z_srv(device, depth.Get());
        require_batch(device.device->mZBufferResourceView == nullptr,
            "F112: SRV created without a hash match is not captured");
        std::fprintf(stderr, "F112: capture-before: fresh SRV refs = %lu\n",
            static_cast<unsigned long>(srv_refs(control.Get())));
    }
    auto srv = z_srv(device, depth.Get());
    require_batch(device.device->mZBufferResourceView == srv.Get(),
        "F112: wrapper publishes the captured z buffer SRV");
    std::fprintf(stderr, "F112: capture-after: captured SRV refs = %lu\n",
        static_cast<unsigned long>(srv_refs(srv.Get())));
    require_batch(srv_refs(srv.Get()) == 2, "F112: capture adds exactly one reference");
    device.wrapper->PSSetShader(shader.Get(), nullptr, 0);
    {
        auto slot = ps_slot_126(device);
        require_batch(slot.Get() == srv.Get(), "F112: captured SRV is bound to slot 126");
    }
    device.context->ClearState();
    device.context->Flush();

    // 2) Replacement: print the old count before replacement and both counts
    // after it; exactly one Release must have hit the old value (2 -> 1) and
    // exactly one AddRef the new one (1 -> 2).
    std::fprintf(stderr, "F112: replacement-before: old SRV refs = %lu\n",
        static_cast<unsigned long>(srv_refs(srv.Get())));
    auto replacement = z_srv(device, depth.Get());
    require_batch(device.device->mZBufferResourceView == replacement.Get(),
        "F112: replacement publishes the new SRV");
    std::fprintf(stderr, "F112: replacement-after: old SRV refs = %lu, new SRV refs = %lu\n",
        static_cast<unsigned long>(srv_refs(srv.Get())),
        static_cast<unsigned long>(srv_refs(replacement.Get())));
    require_batch(srv_refs(srv.Get()) == 1, "F112: replacement releases the old SRV exactly once");
    require_batch(srv_refs(replacement.Get()) == 2, "F112: replacement retains one reference on the new SRV");
    srv.Reset();
    require_batch(device.device->mZBufferResourceView == replacement.Get(),
        "F112: releasing the caller's old reference does not clear the replacement");

    // 3) HackerDevice::Release path: ReleaseZBufferResourceView() is the exact
    // step HackerDevice::Release() runs. Print the count before it, the value
    // returned by the view's Release (0 = last reference), then the pointer.
    device.context->ClearState();
    device.context->Flush();
    replacement.Reset();
    require_batch(device.device->mZBufferResourceView != nullptr,
        "F112: device still holds the last reference after the caller releases");
    std::fprintf(stderr, "F112: release-before: SRV refs = %lu\n",
        static_cast<unsigned long>(srv_refs(device.device->mZBufferResourceView)));
    const long released = device.device->ReleaseZBufferResourceView();
    std::fprintf(stderr, "F112: release-step: SRV Release() returned %ld (0 = refcount reached 0)\n", released);
    std::fprintf(stderr, "F112: release-step: mZBufferResourceView = %p (null after release)\n",
        static_cast<void*>(device.device->mZBufferResourceView));
    require_batch(released == 0, "F112: Release returns 0 when the retained reference is the last one");
    require_batch(device.device->mZBufferResourceView == nullptr,
        "F112: z buffer pointer is cleared after the release");
    device.wrapper->PSSetShader(shader.Get(), nullptr, 0);
    require_batch(!ps_slot_126(device), "F112: released z view is not bound after Release");
}
