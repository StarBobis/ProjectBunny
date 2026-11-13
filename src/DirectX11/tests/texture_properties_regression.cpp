#include "CommandList.h"
#include "HackerContext.h"
#include "HackerDevice.h"
#include "globals.h"

#include <wrl/client.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <vector>

extern bool ParseFloatValue(const wchar_t*, const wchar_t*, const std::wstring&, float&, bool, const std::wstring*);

namespace {
using Microsoft::WRL::ComPtr;

void check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

class PropertiesDevice {
public:
    HMODULE runtime = nullptr;
    ComPtr<ID3D11Device1> native;
    ComPtr<ID3D11DeviceContext1> context;
    HackerDevice* device = nullptr;
    std::unique_ptr<HackerContext> wrapper;
    CommandListState state;
    std::vector<std::wstring> resources;

    PropertiesDevice()
    {
        wchar_t system[MAX_PATH] = {};
        check(GetSystemDirectoryW(system, MAX_PATH) > 0, "locate texture property WARP runtime");
        runtime = LoadLibraryW((std::filesystem::path(system) / L"d3d11.dll").c_str());
        check(runtime != nullptr, "load texture property WARP runtime");
        auto create = reinterpret_cast<decltype(&D3D11CreateDevice)>(GetProcAddress(runtime, "D3D11CreateDevice"));
        check(create != nullptr, "resolve texture property device creation");
        ComPtr<ID3D11Device> base;
        ComPtr<ID3D11DeviceContext> immediate;
        check(SUCCEEDED(create(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
            base.GetAddressOf(), nullptr, immediate.GetAddressOf())) && SUCCEEDED(base.As(&native)) &&
            SUCCEEDED(immediate.As(&context)), "create texture property real DX11 device/context");
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

    ~PropertiesDevice()
    {
        wrapper->ClearState();
        for (const auto& name : resources)
            customResources.erase(name);
        wrapper.reset();
        context.Reset();
        native.Reset();
        device->Release();
        FreeLibrary(runtime);
    }

    CustomResource& add(const wchar_t* id)
    {
        const std::wstring name = id;
        check(customResources.count(name) == 0, "texture property resource fixture must be unique");
        resources.push_back(name);
        auto& resource = customResources[name];
        resource.name = name;
        return resource;
    }

    void seed(const wchar_t* name, ID3D11Resource* resource, ID3D11View* view = nullptr,
        UINT stride = 0, UINT offset = 0, DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN, UINT size = 0)
    {
        add(name);
        ResourceCopyTarget target;
        const std::wstring ini_namespace;
        check(target.ParseTarget(name, false, &ini_namespace, nullptr), "parse actual custom target for seeding");
        target.SetResource(&state, resource, view, stride, offset, format, size);
    }
};

float evaluate(PropertiesDevice& device, const std::wstring& text)
{
    const std::wstring ini_namespace;
    CommandListScope scope;
    CommandListExpression expression;
    if (!expression.parse(&text, &ini_namespace, &scope)) {
        std::fwprintf(stderr, L"Texture property expression rejected: %ls\n", text.c_str());
        throw std::runtime_error("parse real texture property expression");
    }
    return expression.evaluate(&device.state);
}

void reference(PropertiesDevice& device, const wchar_t* destination, const wchar_t* source,
    const wchar_t* operation = L"ref")
{
    const std::wstring ini_namespace;
    CommandListScope scope;
    CommandList commands;
    commands.scope = &scope;
    std::wstring value = std::wstring(operation) + L" " + source;
    check(ParseCommandListResourceCopyTargetDirective(L"CommandListTexturePropertiesRegression", destination,
        &value, &commands, &ini_namespace), "parse actual reference copy with view metadata");
    RunCommandList(device.device, device.wrapper.get(), &commands, nullptr, false);
}

ULONG references(IUnknown* object)
{
    object->AddRef();
    return object->Release();
}

void expect(PropertiesDevice& device, const wchar_t* target, float width, float height, DXGI_FORMAT format)
{
    const std::wstring name = target;
    check(evaluate(device, name + L"->width") == width && evaluate(device, name + L"->height") == height &&
        evaluate(device, name + L"->format") == static_cast<float>(format),
        "resource getters return whole native extent and effective view format");
}

void test_color_views(PropertiesDevice& device)
{
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = 32; desc.Height = 16; desc.MipLevels = 4; desc.ArraySize = 3;
    desc.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS;
    ComPtr<ID3D11Texture2D> texture;
    check(SUCCEEDED(device.native->CreateTexture2D(&desc, nullptr, texture.GetAddressOf())), "create real typeless array/mip texture");
    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
    srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    srv_desc.Texture2DArray.MostDetailedMip = 2; srv_desc.Texture2DArray.MipLevels = 1;
    srv_desc.Texture2DArray.FirstArraySlice = 1; srv_desc.Texture2DArray.ArraySize = 1;
    ComPtr<ID3D11ShaderResourceView> srv;
    check(SUCCEEDED(device.native->CreateShaderResourceView(texture.Get(), &srv_desc, srv.GetAddressOf())), "create mip-two typed array SRV");
    ID3D11ShaderResourceView* source = srv.Get();
    device.context->VSSetShaderResources(0, 1, &source);
    expect(device, L"vs-t0", 32, 16, DXGI_FORMAT_R8G8B8A8_UNORM);
    check(evaluate(device, L"vs-t0->format == dxgi_format_r8g8b8a8_unorm") == 1,
        "format getter compares against actual DXGI enum literal");
    device.add(L"resourceproperties_srvref");
    reference(device, L"resourceproperties_srvref", L"vs-t0");
    check(customResources.at(L"resourceproperties_srvref").offset == 0,
        "texture SRV MostDetailedMip is never interpreted as buffer FirstElement");
    expect(device, L"resourceproperties_srvref", 32, 16, DXGI_FORMAT_R8G8B8A8_UNORM);

    D3D11_RENDER_TARGET_VIEW_DESC rtv_desc = {};
    rtv_desc.Format = DXGI_FORMAT_R8G8B8A8_UINT;
    rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
    rtv_desc.Texture2DArray.MipSlice = 1; rtv_desc.Texture2DArray.FirstArraySlice = 1; rtv_desc.Texture2DArray.ArraySize = 1;
    ComPtr<ID3D11RenderTargetView> rtv;
    check(SUCCEEDED(device.native->CreateRenderTargetView(texture.Get(), &rtv_desc, rtv.GetAddressOf())), "create differently typed array RTV");
    ID3D11RenderTargetView* render = rtv.Get();
    device.context->OMSetRenderTargets(1, &render, nullptr);
    expect(device, L"o0", 32, 16, DXGI_FORMAT_R8G8B8A8_UINT);
    device.add(L"resourceproperties_rtvref");
    reference(device, L"resourceproperties_rtvref", L"o0");
    check(customResources.at(L"resourceproperties_rtvref").offset == 0,
        "texture RTV MipSlice is never read through buffer view union");

    D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
    uav_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
    uav_desc.Texture2DArray.MipSlice = 1; uav_desc.Texture2DArray.FirstArraySlice = 2; uav_desc.Texture2DArray.ArraySize = 1;
    ComPtr<ID3D11UnorderedAccessView> uav;
    check(SUCCEEDED(device.native->CreateUnorderedAccessView(texture.Get(), &uav_desc, uav.GetAddressOf())), "create mip-one typed array UAV");
    ID3D11UnorderedAccessView* unordered = uav.Get();
    device.context->CSSetUnorderedAccessViews(0, 1, &unordered, nullptr);
    expect(device, L"cs-u0", 32, 16, DXGI_FORMAT_R8G8B8A8_UNORM);
    device.add(L"resourceproperties_uavref");
    reference(device, L"resourceproperties_uavref", L"cs-u0");
    check(customResources.at(L"resourceproperties_uavref").offset == 0,
        "texture UAV MipSlice is never interpreted as buffer offset");

    device.seed(L"resourceproperties_native", texture.Get());
    expect(device, L"resourceproperties_native", 32, 16, DXGI_FORMAT_R8G8B8A8_TYPELESS);
    device.seed(L"resourceproperties_uncachedview", texture.Get(), srv.Get());
    expect(device, L"resourceproperties_uncachedview", 32, 16, DXGI_FORMAT_R8G8B8A8_UNORM);
    auto& custom = customResources.at(L"resourceproperties_uncachedview");
    custom.format = DXGI_FORMAT_R8G8B8A8_UINT;
    check(evaluate(device, L"resourceproperties_uncachedview->format") == DXGI_FORMAT_R8G8B8A8_UINT,
        "custom cached format takes precedence over typed view format");
    custom.override_format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    custom.override_width = 41; custom.override_height = 23;
    expect(device, L"resourceproperties_uncachedview", 41, 23, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
    custom.override_width = custom.override_height = -1;
    custom.override_format = static_cast<DXGI_FORMAT>(-1);
    custom.format = DXGI_FORMAT_UNKNOWN;
    const ULONG texture_refs = references(texture.Get());
    const ULONG srv_refs = references(srv.Get());
    const ULONG rtv_refs = references(rtv.Get());
    const ULONG uav_refs = references(uav.Get());
    for (int i = 0; i < 16; ++i) {
        expect(device, L"vs-t0", 32, 16, DXGI_FORMAT_R8G8B8A8_UNORM);
        expect(device, L"o0", 32, 16, DXGI_FORMAT_R8G8B8A8_UINT);
        expect(device, L"cs-u0", 32, 16, DXGI_FORMAT_R8G8B8A8_UNORM);
        expect(device, L"resourceproperties_uncachedview", 32, 16, DXGI_FORMAT_R8G8B8A8_UNORM);
    }
    check(references(texture.Get()) == texture_refs && references(srv.Get()) == srv_refs &&
        references(rtv.Get()) == rtv_refs && references(uav.Get()) == uav_refs,
        "repeated SRV/RTV/UAV/custom queries release resource, view and QI references");
    device.context->ClearState();
}

void test_other_dimensions(PropertiesDevice& device)
{
    D3D11_TEXTURE1D_DESC one_desc = {};
    one_desc.Width = 37; one_desc.MipLevels = 3; one_desc.ArraySize = 4;
    one_desc.Format = DXGI_FORMAT_R32_FLOAT; one_desc.Usage = D3D11_USAGE_DEFAULT;
    one_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture1D> one;
    check(SUCCEEDED(device.native->CreateTexture1D(&one_desc, nullptr, one.GetAddressOf())), "create actual 1D array texture");
    D3D11_SHADER_RESOURCE_VIEW_DESC one_view = {};
    one_view.Format = one_desc.Format; one_view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE1DARRAY;
    one_view.Texture1DArray.MostDetailedMip = 1; one_view.Texture1DArray.MipLevels = 1;
    one_view.Texture1DArray.FirstArraySlice = 2; one_view.Texture1DArray.ArraySize = 1;
    ComPtr<ID3D11ShaderResourceView> one_srv;
    check(SUCCEEDED(device.native->CreateShaderResourceView(one.Get(), &one_view, one_srv.GetAddressOf())), "create 1D mip array SRV");
    ID3D11ShaderResourceView* bound = one_srv.Get();
    device.context->VSSetShaderResources(1, 1, &bound);
    expect(device, L"vs-t1", 37, 1, DXGI_FORMAT_R32_FLOAT);

    D3D11_TEXTURE3D_DESC three_desc = {};
    three_desc.Width = 19; three_desc.Height = 11; three_desc.Depth = 7; three_desc.MipLevels = 2;
    three_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; three_desc.Usage = D3D11_USAGE_DEFAULT;
    three_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    ComPtr<ID3D11Texture3D> three;
    check(SUCCEEDED(device.native->CreateTexture3D(&three_desc, nullptr, three.GetAddressOf())), "create actual 3D mip texture");
    D3D11_SHADER_RESOURCE_VIEW_DESC three_view = {};
    three_view.Format = three_desc.Format; three_view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
    three_view.Texture3D.MostDetailedMip = 1; three_view.Texture3D.MipLevels = 1;
    ComPtr<ID3D11ShaderResourceView> three_srv;
    check(SUCCEEDED(device.native->CreateShaderResourceView(three.Get(), &three_view, three_srv.GetAddressOf())), "create 3D mip-one SRV");
    bound = three_srv.Get();
    device.context->VSSetShaderResources(2, 1, &bound);
    expect(device, L"vs-t2", 19, 11, DXGI_FORMAT_R8G8B8A8_UNORM);
    D3D11_UNORDERED_ACCESS_VIEW_DESC three_uav_desc = {};
    three_uav_desc.Format = three_desc.Format; three_uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D;
    three_uav_desc.Texture3D.MipSlice = 1; three_uav_desc.Texture3D.FirstWSlice = 1; three_uav_desc.Texture3D.WSize = 2;
    ComPtr<ID3D11UnorderedAccessView> three_uav;
    check(SUCCEEDED(device.native->CreateUnorderedAccessView(three.Get(), &three_uav_desc, three_uav.GetAddressOf())), "create partial 3D UAV");
    ID3D11UnorderedAccessView* unordered = three_uav.Get();
    // Unbind its overlapping mip SRV before using the same mip as output.
    bound = nullptr; device.context->VSSetShaderResources(2, 1, &bound);
    device.context->CSSetUnorderedAccessViews(1, 1, &unordered, nullptr);
    expect(device, L"cs-u1", 19, 11, DXGI_FORMAT_R8G8B8A8_UNORM);

    D3D11_TEXTURE2D_DESC depth_desc = {};
    depth_desc.Width = 64; depth_desc.Height = 32; depth_desc.ArraySize = 2; depth_desc.MipLevels = 3;
    depth_desc.Format = DXGI_FORMAT_R32_TYPELESS; depth_desc.SampleDesc.Count = 1;
    depth_desc.Usage = D3D11_USAGE_DEFAULT; depth_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> depth;
    check(SUCCEEDED(device.native->CreateTexture2D(&depth_desc, nullptr, depth.GetAddressOf())), "create typeless depth array texture");
    D3D11_DEPTH_STENCIL_VIEW_DESC depth_view = {};
    depth_view.Format = DXGI_FORMAT_D32_FLOAT; depth_view.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
    depth_view.Texture2DArray.MipSlice = 1; depth_view.Texture2DArray.FirstArraySlice = 1; depth_view.Texture2DArray.ArraySize = 1;
    ComPtr<ID3D11DepthStencilView> dsv;
    check(SUCCEEDED(device.native->CreateDepthStencilView(depth.Get(), &depth_view, dsv.GetAddressOf())), "create typed mip-one array DSV");
    device.context->OMSetRenderTargets(0, nullptr, dsv.Get());
    expect(device, L"od", 64, 32, DXGI_FORMAT_D32_FLOAT);
    device.add(L"resourceproperties_dsvref");
    reference(device, L"resourceproperties_dsvref", L"od");
    check(customResources.at(L"resourceproperties_dsvref").offset == 0,
        "DSV array/mip metadata does not become a fictitious buffer offset");
    const ULONG depth_refs = references(depth.Get());
    const ULONG dsv_refs = references(dsv.Get());
    for (int i = 0; i < 16; ++i)
        expect(device, L"od", 64, 32, DXGI_FORMAT_D32_FLOAT);
    check(references(depth.Get()) == depth_refs && references(dsv.Get()) == dsv_refs,
        "DSV property queries release actual view/resource references");
    device.context->ClearState();
}

void test_buffers_and_windows(PropertiesDevice& device)
{
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = 128; desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_INDEX_BUFFER;
    std::vector<unsigned char> bytes(desc.ByteWidth);
    for (UINT i = 0; i < desc.ByteWidth; ++i)
        bytes[i] = static_cast<unsigned char>(i);
    D3D11_SUBRESOURCE_DATA initial = {};
    initial.pSysMem = bytes.data();
    ComPtr<ID3D11Buffer> buffer;
    check(SUCCEEDED(device.native->CreateBuffer(&desc, &initial, buffer.GetAddressOf())), "create typed-buffer property source");
    D3D11_SHADER_RESOURCE_VIEW_DESC view_desc = {};
    view_desc.Format = DXGI_FORMAT_R32_FLOAT; view_desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    view_desc.Buffer.FirstElement = 4; view_desc.Buffer.NumElements = 8;
    ComPtr<ID3D11ShaderResourceView> view;
    check(SUCCEEDED(device.native->CreateShaderResourceView(buffer.Get(), &view_desc, view.GetAddressOf())), "create real buffer range SRV");
    ID3D11ShaderResourceView* bound = view.Get();
    device.context->VSSetShaderResources(0, 1, &bound);
    check(evaluate(device, L"vs-t0->format") == DXGI_FORMAT_R32_FLOAT &&
        evaluate(device, L"vs-t0->width") == -4 && evaluate(device, L"vs-t0->height") == -4,
        "typed buffers expose view format but distinguish non-texture extents");
    device.add(L"resourceproperties_bufferref");
    reference(device, L"resourceproperties_bufferref", L"vs-t0");
    check(customResources.at(L"resourceproperties_bufferref").offset == 16 &&
        customResources.at(L"resourceproperties_bufferref").stride == 4,
        "buffer view FirstElement conversion preserves actual byte offset and typed element size");
    device.context->IASetIndexBuffer(buffer.Get(), DXGI_FORMAT_R16_UINT, 0);
    check(evaluate(device, L"ib->format") == DXGI_FORMAT_R16_UINT && evaluate(device, L"ib->stride") == 2,
        "minimal output refactor retains required native index-buffer format for stride");

    device.seed(L"resourceproperties_explicit", buffer.Get(), view.Get(), 8, 12, DXGI_FORMAT_R16_UINT, UINT32_MAX);
    device.add(L"resourceproperties_explicitref");
    reference(device, L"resourceproperties_explicitref", L"resourceproperties_explicit");
    const auto& explicit_ref = customResources.at(L"resourceproperties_explicitref");
    // Ordinary references publish an unspecified length; their size getter
    // derives the physical allocation instead of publishing the source hint.
    check(explicit_ref.stride == 8 && explicit_ref.offset == 12 && explicit_ref.buf_size == 0 &&
        evaluate(device, L"resourceproperties_explicitref->size") == 128 &&
        explicit_ref.format == DXGI_FORMAT_R16_UINT,
        "explicit metadata wins and an ordinary reference retains physical-size fallback");

    // A real copy consumes FillInMissingInfo's bounded size, unlike a ref's
    // deliberately unspecified output length. An oversized source hint must
    // not turn this suffix into an out-of-bounds copy box. The existing
    // region-copy contract copies complete eight-byte source elements.
    reference(device, L"cs-cb2", L"resourceproperties_explicit", L"copy");
    ComPtr<ID3D11Buffer> suffix;
    device.context->CSGetConstantBuffers(2, 1, suffix.GetAddressOf());
    check(suffix != nullptr, "oversized custom metadata still produces a real copied buffer");
    D3D11_BUFFER_DESC staging_desc = {};
    suffix->GetDesc(&staging_desc);
    check(staging_desc.ByteWidth == 128, "copy allocation remains bounded by physical source size");
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.BindFlags = 0;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    staging_desc.MiscFlags = 0;
    ComPtr<ID3D11Buffer> staging;
    check(SUCCEEDED(device.native->CreateBuffer(&staging_desc, nullptr, staging.GetAddressOf())), "create suffix readback buffer");
    device.context->CopyResource(staging.Get(), suffix.Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    check(SUCCEEDED(device.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "map bounded suffix copy");
    constexpr size_t complete_suffix_bytes = (128 - 12) / 8 * 8;
    const bool copied = std::memcmp(mapped.pData, bytes.data() + 12, complete_suffix_bytes) == 0;
    device.context->Unmap(staging.Get(), 0);
    check(copied, "all complete source elements within the physical suffix copy despite oversized metadata");

    D3D11_BUFFER_DESC cb_desc = {};
    cb_desc.ByteWidth = 2048; cb_desc.Usage = D3D11_USAGE_DEFAULT; cb_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    ComPtr<ID3D11Buffer> cb;
    check(SUCCEEDED(device.native->CreateBuffer(&cb_desc, nullptr, cb.GetAddressOf())), "create bound-window constant buffer");
    ID3D11Buffer* bound_cb = cb.Get();
    const UINT first = 16, count = 16;
    device.context->VSSetConstantBuffers1(0, 1, &bound_cb, &first, &count);
    device.add(L"resourceproperties_cbref");
    reference(device, L"resourceproperties_cbref", L"vs-cb0");
    reference(device, L"ps-cb1", L"resourceproperties_cbref->region(256,256)");
    ComPtr<ID3D11Buffer> actual;
    UINT actual_first = 0, actual_count = 0;
    device.context->PSGetConstantBuffers1(1, 1, actual.GetAddressOf(), &actual_first, &actual_count);
    check(actual.Get() == cb.Get() && actual_first == first && actual_count == count,
        "FillInMissingInfo refactor preserves real CB FirstConstant/NumConstants window");
    device.context->ClearState();
}

void test_missing_and_literals(PropertiesDevice& device)
{
    device.add(L"resourceproperties_missing");
    for (const wchar_t* name : {L"vs-t7", L"resourceproperties_missing"}) {
        const std::wstring target = name;
        check(evaluate(device, target + L"->format") == -2 && evaluate(device, target + L"->width") == -2 &&
            evaluate(device, target + L"->height") == -2, "missing backing resources consistently return RESOURCE_NOT_FOUND");
    }
    auto& metadata = device.add(L"resourceproperties_metadata");
    metadata.override_format = DXGI_FORMAT_R32_FLOAT; metadata.override_width = 0; metadata.override_height = 7;
    expect(device, L"resourceproperties_metadata", 0, 7, DXGI_FORMAT_R32_FLOAT);
    const std::wstring ini_namespace;
    for (const wchar_t* literal : {L"dxgi_format_unknown", L"dxgi_format_r32_float", L"dxgi_format_r8g8b8a8_unorm"}) {
        const std::wstring text = literal;
        CommandListExpression expression;
        float value = -1;
        check(expression.parse(&text, &ini_namespace, nullptr) && expression.static_evaluate(&value) &&
            value == static_cast<float>(ParseFormatString(literal, false)), "DXGI format literals support real static expression evaluation");
    }
    float configured = -1;
    check(ParseFloatValue(L"Constants", L"texture_properties_regression", L"DXGI_FORMAT_R8G8B8A8_UNORM", configured,
        false, &ini_namespace) && configured == DXGI_FORMAT_R8G8B8A8_UNORM, "INI case normalization accepts uppercase DXGI enum spelling");
    for (const wchar_t* literal : {L"dxgi_format_", L"dxgi_format", L"dxgi_format_42", L"dxgi_format_unknownjunk",
        L"dxgifactory_format_r32_float", L"dxgi_format_r32_float_bad"}) {
        const std::wstring text = literal;
        CommandListExpression rejected;
        check(!rejected.parse(&text, &ini_namespace, nullptr), "format enum parser requires exact prefix and complete valid spelling");
    }
}

void run_texture_properties_regression()
{
    PropertiesDevice device;
    test_color_views(device);
    test_other_dimensions(device);
    test_buffers_and_windows(device);
    test_missing_and_literals(device);
}
} // namespace

void test_dx11_texture_properties_regression()
{
    try {
        run_texture_properties_regression();
        std::fputs("PASS: real texture formats/extents, view metadata, buffer bounds and CB windows\n", stderr);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: texture properties regression: %s\n", error.what());
        std::exit(EXIT_FAILURE);
    }
}
