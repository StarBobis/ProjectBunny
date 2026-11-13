#include "slot_range_test_support.h"
#include "cache_policy_test_scope.h"
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
    D3D11_SHADER_RESOURCE_VIEW_DESC actual = {}; rebound->GetDesc(&actual);
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
