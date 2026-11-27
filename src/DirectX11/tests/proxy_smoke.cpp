#include <Windows.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include "DX11ResourceIds.h"

namespace {
using Microsoft::WRL::ComPtr;
void check(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}

bool belongs_to_proxy(void* method, HMODULE proxy)
{
    HMODULE owner = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(method), &owner) && owner == proxy;
}

ULONG references(IUnknown* object)
{
    const ULONG count = object->AddRef();
    object->Release();
    return count;
}

void exercise_context1(ID3D11Device1* device, ID3D11DeviceContext1* context)
{
    UINT values[64];
    for (UINT index = 0; index < 64; ++index) values[index] = index * 7 + 13;
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = sizeof(values); desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    ComPtr<ID3D11Buffer> buffer, staging;
    check(SUCCEEDED(device->CreateBuffer(&desc,nullptr,&buffer)),"Context1 test buffer");
    ID3D11Buffer* bound = buffer.Get();
    const UINT first = 0, count = 16;
    decltype(&ID3D11DeviceContext1::VSSetConstantBuffers1) setters[] = {
        &ID3D11DeviceContext1::VSSetConstantBuffers1, &ID3D11DeviceContext1::HSSetConstantBuffers1,
        &ID3D11DeviceContext1::DSSetConstantBuffers1, &ID3D11DeviceContext1::GSSetConstantBuffers1,
        &ID3D11DeviceContext1::PSSetConstantBuffers1, &ID3D11DeviceContext1::CSSetConstantBuffers1};
    decltype(&ID3D11DeviceContext1::VSGetConstantBuffers1) getters[] = {
        &ID3D11DeviceContext1::VSGetConstantBuffers1, &ID3D11DeviceContext1::HSGetConstantBuffers1,
        &ID3D11DeviceContext1::DSGetConstantBuffers1, &ID3D11DeviceContext1::GSGetConstantBuffers1,
        &ID3D11DeviceContext1::PSGetConstantBuffers1, &ID3D11DeviceContext1::CSGetConstantBuffers1};
    for (unsigned stage = 0; stage < 6; ++stage) {
        (context->*setters[stage])(0,1,&bound,&first,&count);
        ComPtr<ID3D11Buffer> actual;
        UINT actual_first = 0, actual_count = 0;
        (context->*getters[stage])(0,1,&actual,&actual_first,&actual_count);
        check(actual.Get() == buffer.Get() && actual_first == first && actual_count == count,
            "Context1 constant-buffer windows round-trip on all six stages");
    }
    context->UpdateSubresource1(buffer.Get(),0,nullptr,values,0,0,0);
    desc.BindFlags = 0; desc.Usage = D3D11_USAGE_STAGING; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    check(SUCCEEDED(device->CreateBuffer(&desc,nullptr,&staging)),"Context1 readback allocation");
    context->CopySubresourceRegion1(staging.Get(),0,0,0,0,buffer.Get(),0,nullptr,0);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    check(SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)),"Context1 readback Map");
    const bool correct = std::memcmp(mapped.pData,values,sizeof(values)) == 0;
    context->Unmap(staging.Get(),0);
    check(correct,"Context1 update and copy preserve actual GPU bytes");
    D3D11_BLEND_DESC1 blend{};
    ComPtr<ID3D11BlendState1> blend_state;
    check(SUCCEEDED(device->CreateBlendState1(&blend,&blend_state)),"Device1 blend-state forwarding");
    D3D11_RASTERIZER_DESC1 raster{};
    raster.FillMode = D3D11_FILL_SOLID; raster.CullMode = D3D11_CULL_NONE; raster.DepthClipEnable = TRUE;
    ComPtr<ID3D11RasterizerState1> raster_state;
    check(SUCCEEDED(device->CreateRasterizerState1(&raster,&raster_state)),"Device1 rasterizer forwarding");
    const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    ComPtr<ID3DDeviceContextState> state, previous;
    check(SUCCEEDED(device->CreateDeviceContextState(0,&level,1,D3D11_SDK_VERSION,
        __uuidof(ID3D11Device),nullptr,&state)),"Device1 context-state creation");
    context->SwapDeviceContextState(state.Get(),&previous);
    context->SwapDeviceContextState(previous.Get(),nullptr);
    D3D11_TEXTURE2D_DESC texture_desc{};
    texture_desc.Width = texture_desc.Height = texture_desc.MipLevels = texture_desc.ArraySize = 1;
    texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; texture_desc.SampleDesc.Count = 1;
    texture_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11RenderTargetView> view;
    check(SUCCEEDED(device->CreateTexture2D(&texture_desc,nullptr,&texture)) &&
        SUCCEEDED(device->CreateRenderTargetView(texture.Get(),nullptr,&view)),"Context1 clear/discard fixture");
    const float color[] = {0,0.5f,1,1};
    context->ClearView(view.Get(),color,nullptr,0);
    context->DiscardView(view.Get());
    context->DiscardView1(view.Get(),nullptr,0);
    context->DiscardResource(texture.Get());
    void* unavailable = nullptr;
    check(FAILED(device->OpenSharedResource1(nullptr,__uuidof(ID3D11Texture2D),&unavailable)) &&
        unavailable == nullptr,"Device1 invalid shared-handle failure forwarding");
    check(FAILED(device->OpenSharedResourceByName(L"",GENERIC_READ,__uuidof(ID3D11Texture2D),&unavailable)) &&
        unavailable == nullptr,"Device1 invalid shared-name failure forwarding");
}

class ProxyOverlayWindow {
public:
    HWND handle = nullptr;
    ProxyOverlayWindow()
    {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = DefWindowProcW; wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"BunnyProxyOverlayTest";
        check(RegisterClassW(&wc) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS, "proxy overlay window class");
        handle = CreateWindowW(wc.lpszClassName, L"Proxy overlay test", WS_OVERLAPPEDWINDOW,
            0, 0, 640, 360, nullptr, nullptr, wc.hInstance, nullptr);
        check(handle != nullptr, "proxy overlay hidden window");
    }
    ~ProxyOverlayWindow() { if (handle) DestroyWindow(handle); }
};
void exercise_overlay(HMODULE proxy)
{
    ProxyOverlayWindow window;
    auto create = reinterpret_cast<PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN>(GetProcAddress(proxy, "D3D11CreateDeviceAndSwapChain"));
    DXGI_SWAP_CHAIN_DESC desc = {};
    desc.BufferDesc.Width = 640; desc.BufferDesc.Height = 360;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
    desc.BufferCount = 1; desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = window.handle; desc.Windowed = TRUE;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain> chain;
    check(SUCCEEDED(create(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &desc, &chain, &device, nullptr, &context)), "proxy: actual wrapped swap chain and overlay construction");
    for (unsigned i = 0; i < 3; ++i) check(SUCCEEDED(chain->Present(0, 0)), "proxy: actual Present and hunting overlay");
    context->ClearState(); context->Flush();
    check(SUCCEEDED(chain->ResizeBuffers(1, 800, 450, DXGI_FORMAT_R8G8B8A8_UNORM, 0)), "proxy: actual resize releases backbuffer");
    check(SUCCEEDED(chain->Present(0, 0)), "proxy: actual Present after resize");
    context->ClearState(); context->Flush();
    check(!GetModuleHandleW(L"DWrite.dll") && !GetModuleHandleW(L"d2d1.dll"), "proxy: overlay rendering adds no DirectWrite/Direct2D modules");
}

class ProvenanceFixture {
    ComPtr<ID3D11Buffer> source, twin, palette, mapped, copied_map, output, vertex;
    ComPtr<ID3D11ShaderResourceView> inputs[3];
    ComPtr<ID3D11UnorderedAccessView> uav;
    ComPtr<ID3D11ComputeShader> cs;
    ComPtr<ID3D11DeviceContext1> immediate, writer_a, writer_b;

public:
    ProvenanceFixture(ID3D11Device* device, ID3D11DeviceContext* context)
    {
        ComPtr<ID3D11Device1> native;
        check(SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&native))) &&
            SUCCEEDED(context->QueryInterface(IID_PPV_ARGS(&immediate))) &&
            SUCCEEDED(native->CreateDeferredContext1(0, &writer_a)) &&
            SUCCEEDED(native->CreateDeferredContext1(0, &writer_b)), "create provenance fixture contexts");
        const UINT initial[] = {1, 2, 3, 4, 5, 6, 7, 8};
        const D3D11_SUBRESOURCE_DATA data{initial, 0, 0};
        const D3D11_BUFFER_DESC srv_desc{sizeof(initial), D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE,
            0, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0};
        check(SUCCEEDED(device->CreateBuffer(&srv_desc, &data, &source)) &&
            SUCCEEDED(device->CreateBuffer(&srv_desc, &data, &twin)) &&
            SUCCEEDED(device->CreateBuffer(&srv_desc, nullptr, &palette)) &&
            SUCCEEDED(device->CreateBuffer(&srv_desc, nullptr, &copied_map)), "create provenance fixture SRV buffers");
        D3D11_BUFFER_DESC staging{sizeof(initial), D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_WRITE, 0, 0};
        const D3D11_BUFFER_DESC output_desc{sizeof(initial), D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS,
            0, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0};
        const D3D11_BUFFER_DESC vb_desc{sizeof(initial), D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0};
        check(SUCCEEDED(device->CreateBuffer(&staging, nullptr, &mapped)) &&
            SUCCEEDED(device->CreateBuffer(&output_desc, nullptr, &output)) &&
            SUCCEEDED(device->CreateBuffer(&vb_desc, nullptr, &vertex)), "create provenance fixture map and output");

        D3D11_SHADER_RESOURCE_VIEW_DESC view{};
        view.Format = DXGI_FORMAT_R32_TYPELESS;
        view.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
        view.BufferEx = {0, 4, D3D11_BUFFEREX_SRV_FLAG_RAW};
        check(SUCCEEDED(device->CreateShaderResourceView(source.Get(), &view, &inputs[0])) &&
            SUCCEEDED(device->CreateShaderResourceView(copied_map.Get(), &view, &inputs[1])),
            "create provenance fixture CS t0 and t1");
        view.BufferEx = {4, 2, D3D11_BUFFEREX_SRV_FLAG_RAW};
        check(SUCCEEDED(device->CreateShaderResourceView(palette.Get(), &view, &inputs[2])),
            "create provenance fixture ranged CS t2");
        D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc{};
        uav_desc.Format = DXGI_FORMAT_R32_TYPELESS;
        uav_desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uav_desc.Buffer = {0, 8, D3D11_BUFFER_UAV_FLAG_RAW};
        check(SUCCEEDED(device->CreateUnorderedAccessView(output.Get(), &uav_desc, &uav)),
            "create provenance fixture CS u0");
        constexpr char shader[] =
            "ByteAddressBuffer x : register(t0), y : register(t1), z : register(t2);"
            "RWByteAddressBuffer result : register(u0);"
            "[numthreads(1,1,1)] void main() { result.Store(0, x.Load(0) + y.Load(0) + z.Load(0)); }";
        ComPtr<ID3DBlob> code;
        check(SUCCEEDED(D3DCompile(shader, sizeof(shader) - 1, nullptr, nullptr, nullptr, "main", "cs_5_0",
            0, 0, &code, nullptr)) && SUCCEEDED(device->CreateComputeShader(code->GetBufferPointer(),
            code->GetBufferSize(), nullptr, &cs)), "compile provenance fixture compute shader");
    }

    void Submit()
    {
        const UINT current[] = {21, 22, 23, 24, 25, 26, 27, 28};
        const UINT patch[] = {71, 72};
        const UINT older[] = {31, 32, 33, 34, 35, 36, 37, 38};
        const D3D11_BOX region{4, 0, 0, 12, 1, 1};
        D3D11_MAPPED_SUBRESOURCE map{};
        check(SUCCEEDED(immediate->Map(mapped.Get(), 0, D3D11_MAP_WRITE, 0, &map)), "map provenance fixture");
        memcpy(map.pData, current, sizeof(current));
        immediate->Unmap(mapped.Get(), 0);
        immediate->CopyResource(copied_map.Get(), mapped.Get());

        // Record A first and execute it twice after B. A must be the producer
        // for the copied subrange although B has the higher recording number.
        writer_a->UpdateSubresource1(twin.Get(), 0, &region, patch, 0, 0, 0);
        ComPtr<ID3D11CommandList> list_a, list_b;
        check(SUCCEEDED(writer_a->FinishCommandList(FALSE, &list_a)), "finish provenance command list A");
        writer_b->UpdateSubresource(twin.Get(), 0, nullptr, older, 0, 0);
        check(SUCCEEDED(writer_b->FinishCommandList(FALSE, &list_b)), "finish provenance command list B");
        immediate->ExecuteCommandList(list_b.Get(), TRUE);
        immediate->ExecuteCommandList(list_a.Get(), TRUE);
        immediate->ExecuteCommandList(list_a.Get(), TRUE);

        writer_a->CopySubresourceRegion1(palette.Get(), 0, 16, 0, 0, twin.Get(), 0, &region, 0);
        BindAndDispatch(writer_a.Get());
        ComPtr<ID3D11CommandList> dispatch;
        check(SUCCEEDED(writer_a->FinishCommandList(FALSE, &dispatch)), "finish provenance dispatch list");
        immediate->ExecuteCommandList(dispatch.Get(), TRUE);
        immediate->CopyResource(vertex.Get(), output.Get());
        const UINT stride = 4, offset = 0;
        immediate->IASetVertexBuffers(0, 1, vertex.GetAddressOf(), &stride, &offset);
    }

    void SubmitImmediate()
    {
        BindAndDispatch(immediate.Get());
        ID3D11UnorderedAccessView* empty = nullptr;
        immediate->CSSetUnorderedAccessViews(0, 1, &empty, nullptr);
        immediate->CopyResource(vertex.Get(), output.Get());
    }

private:
    void BindAndDispatch(ID3D11DeviceContext1* context)
    {
        ID3D11ShaderResourceView* views[] = {inputs[0].Get(), inputs[1].Get(), inputs[2].Get()};
        context->CSSetShaderResources(0, 3, views);
        context->CSSetUnorderedAccessViews(0, 1, uav.GetAddressOf(), nullptr);
        context->CSSetShader(cs.Get(), nullptr, 0);
        context->Dispatch(1, 1, 1);
    }
};

int serve_mcp_fixture(const wchar_t* library, bool provenance = false)
{
    HMODULE proxy = LoadLibraryW(library);
    check(proxy != nullptr, "load DX11 proxy for MCP");
    auto window = std::make_unique<ProxyOverlayWindow>();
    const auto create = reinterpret_cast<PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN>(
        GetProcAddress(proxy, "D3D11CreateDeviceAndSwapChain"));
    check(create != nullptr, "resolve DX11 swapchain entry point");
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferDesc.Width = 64;
    desc.BufferDesc.Height = 64;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferCount = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.Windowed = TRUE;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain> chain;
    D3D11_BUFFER_DESC index_desc{};
    index_desc.ByteWidth = 16;
    index_desc.Usage = D3D11_USAGE_DYNAMIC;
    index_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    index_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    const UINT initial[] = {100, 1, 2, 3};
    D3D11_SUBRESOURCE_DATA data{initial, 0, 0};
    ComPtr<ID3D11Buffer> index;
    ComPtr<ID3D11PixelShader> pixel_shader;
    ComPtr<ID3D11ShaderResourceView> red, blue;
    constexpr char pixel_source[] = "float4 main() : SV_Target { return float4(1, 1, 1, 1); }";
    ComPtr<ID3DBlob> pixel_code;
    check(SUCCEEDED(D3DCompile(pixel_source, sizeof(pixel_source) - 1, nullptr, nullptr, nullptr,
        "main", "ps_5_0", 0, 0, &pixel_code, nullptr)), "compile MCP fixture pixel shader");
    const auto fixture = std::filesystem::path(library).parent_path();
    const auto stop = fixture / L"stop";
    const auto make_device = [&] {
        desc.OutputWindow = window->handle;
        check(SUCCEEDED(create(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &desc, &chain, &device, nullptr, &context)), "create MCP WARP swapchain");
        check(SUCCEEDED(device->CreateBuffer(&index_desc, &data, &index)), "create MCP dynamic IB");
        context->IASetIndexBuffer(index.Get(), DXGI_FORMAT_R32_UINT, sizeof(UINT));
        check(SUCCEEDED(device->CreatePixelShader(pixel_code->GetBufferPointer(), pixel_code->GetBufferSize(),
            nullptr, pixel_shader.ReleaseAndGetAddressOf())), "create MCP fixture pixel shader");
        const auto color_view = [&](UINT color, ComPtr<ID3D11ShaderResourceView>& result) {
            std::array<UINT, 16> pixels;
            pixels.fill(color);
            D3D11_TEXTURE2D_DESC texture_desc{};
            texture_desc.Width = texture_desc.Height = 4;
            texture_desc.MipLevels = texture_desc.ArraySize = texture_desc.SampleDesc.Count = 1;
            texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            texture_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA image{pixels.data(), 4 * sizeof(UINT), 0};
            ComPtr<ID3D11Texture2D> texture;
            check(SUCCEEDED(device->CreateTexture2D(&texture_desc, &image, &texture)) &&
                SUCCEEDED(device->CreateShaderResourceView(texture.Get(), nullptr, result.ReleaseAndGetAddressOf())),
                "create tracked MCP fixture texture");
        };
        color_view(0xff0000ff, red);
        color_view(0xffff0000, blue);
    };
    make_device();
    std::unique_ptr<ProvenanceFixture> provenance_fixture;
    if (provenance) provenance_fixture = std::make_unique<ProvenanceFixture>(device.Get(), context.Get());
    const auto deadline = GetTickCount64() + 45000;
    bool holding_map = false, device_switched = false, paused = false;
    bool texture_updated = false;
    unsigned replacement_frames = 0;
    for (UINT counter = 0; GetTickCount64() < deadline && !std::filesystem::exists(stop); ++counter) {
        if (std::filesystem::exists(fixture / L"pause-present")) {
            if (!paused) std::ofstream(fixture / L"pause-observed").put('1');
            paused = true;
            Sleep(10);
            continue;
        }
        paused = false;
        if (!device_switched && std::filesystem::exists(fixture / L"switch-device")) {
            check(!holding_map, "switch fixture device only after Unmap");
            context->ClearState();
            context->Flush();
            index.Reset();
            pixel_shader.Reset();
            red.Reset();
            blue.Reset();
            chain.Reset();
            context.Reset();
            device.Reset();
            window = std::make_unique<ProxyOverlayWindow>();
            make_device();
            device_switched = true;
        }
        if (std::filesystem::exists(fixture / L"hold-map")) {
            if (!holding_map) {
                D3D11_MAPPED_SUBRESOURCE mapping{};
                check(SUCCEEDED(context->Map(index.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapping)), "hold MCP fixture mapping");
                memcpy(mapping.pData, initial, sizeof(initial));
                holding_map = true;
                std::ofstream(fixture / L"map-held").put('1');
            }
        } else {
            if (holding_map) {
                context->Unmap(index.Get(), 0);
                holding_map = false;
                std::ofstream(fixture / L"map-released").put('1');
            }
            if (!std::filesystem::exists(fixture / L"present-only")) {
                D3D11_MAPPED_SUBRESOURCE mapping{};
                check(SUCCEEDED(context->Map(index.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapping)), "update MCP dynamic IB");
                const UINT bytes[] = {counter, 1, 2, 3};
                memcpy(mapping.pData, bytes, sizeof(bytes));
                context->Unmap(index.Get(), 0);
                if (!texture_updated && std::filesystem::exists(fixture / L"update-red-texture")) {
                    ComPtr<ID3D11Resource> texture;
                    red->GetResource(&texture);
                    std::array<UINT, 16> pixels;
                    pixels.fill(0xffff0000);
                    context->UpdateSubresource(texture.Get(), 0, nullptr, pixels.data(), 4 * sizeof(UINT), 0);
                    texture_updated = true;
                    std::ofstream(fixture / L"texture-update-observed").put('1');
                }
                if (std::filesystem::exists(fixture / L"clear-ps-state"))
                    context->ClearState();
                else
                    context->PSSetShader(pixel_shader.Get(), nullptr, 0);
                if (provenance_fixture) provenance_fixture->Submit();
                context->IASetIndexBuffer(index.Get(), DXGI_FORMAT_R32_UINT, sizeof(UINT));
                ID3D11ShaderResourceView* views[] = {red.Get(), nullptr, blue.Get()};
                context->PSSetShaderResources(16, 3, views);
                context->Draw(3, 0);
                context->DrawIndexed(3, 0, 0);
                if (provenance_fixture) provenance_fixture->SubmitImmediate();
                context->PSSetShaderResources(16, 1, blue.GetAddressOf());
                context->DrawIndexedInstanced(2, 1, 1, 0, 0);
            }
        }
        const HRESULT present = chain->Present(0, 0);
        if (FAILED(present))
            std::fprintf(stderr, "MCP fixture Present failed: 0x%08lx, device reason 0x%08lx\n",
                static_cast<unsigned long>(present), static_cast<unsigned long>(device->GetDeviceRemovedReason()));
        check(SUCCEEDED(present), "pump DX11 MCP on Present");
        if (device_switched && ++replacement_frames == 20)
            std::ofstream(fixture / L"replacement-presenting").put('1');
        Sleep(10);
    }
    if (holding_map) context->Unmap(index.Get(), 0);
    return std::filesystem::exists(stop) ? 0 : 1;
}

class AlternatingMcpDevice {
    ProxyOverlayWindow window;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain> chain;
    ComPtr<ID3D11Buffer> index;
public:
    AlternatingMcpDevice(PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN create, const UINT (&indices)[3])
    {
        DXGI_SWAP_CHAIN_DESC desc{};
        desc.BufferDesc.Width = 64;
        desc.BufferDesc.Height = 64;
        desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferCount = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.OutputWindow = window.handle;
        desc.Windowed = TRUE;
        check(SUCCEEDED(create(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &desc, &chain, &device, nullptr, &context)), "create alternating WARP swap chain");
        D3D11_BUFFER_DESC ib{};
        ib.ByteWidth = sizeof(indices);
        ib.Usage = D3D11_USAGE_IMMUTABLE;
        ib.BindFlags = D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA data{indices, 0, 0};
        check(SUCCEEDED(device->CreateBuffer(&ib, &data, &index)), "create tagged WARP index buffer");
    }

    void Draw()
    {
        context->IASetIndexBuffer(index.Get(), DXGI_FORMAT_R32_UINT, 0);
        context->DrawIndexed(3, 0, 0);
    }

    void Present()
    {
        check(SUCCEEDED(chain->Present(0, 0)), "present alternating WARP swap chain");
    }
};

int serve_mcp_alternating_fixture(const wchar_t* library)
{
    const auto proxy = LoadLibraryW(library);
    check(proxy != nullptr, "load proxy for alternating-device test");
    const auto create = reinterpret_cast<PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN>(
        GetProcAddress(proxy, "D3D11CreateDeviceAndSwapChain"));
    check(create != nullptr, "locate D3D11CreateDeviceAndSwapChain");
    AlternatingMcpDevice first(create, {11, 22, 33});
    AlternatingMcpDevice second(create, {44, 55, 66});
    const auto fixture = std::filesystem::path(library).parent_path();
    const auto deadline = GetTickCount64() + 20000;
    unsigned alternations = 0;
    unsigned second_only = 0;
    while (!std::filesystem::exists(fixture / L"stop") && GetTickCount64() < deadline) {
        const bool alternate = std::filesystem::exists(fixture / L"alternate");
        const bool pause_first = std::filesystem::exists(fixture / L"pause-first");
        if (alternate) {
            second.Draw();
            second.Present();
            if (pause_first && ++second_only == 70)
                std::ofstream(fixture / L"second-only").put('1');
        }
        if (!pause_first) {
            if (std::filesystem::exists(fixture / L"first-draw"))
                first.Draw();
            first.Present();
            if (alternate && ++alternations == 5)
                std::ofstream(fixture / L"alternated").put('1');
        }
        Sleep(10);
    }
    return std::filesystem::exists(fixture / L"stop") ? 0 : 1;
}

int serve_mcp_loading_fixture(const wchar_t* library, bool instant_exit)
{
    const auto proxy = LoadLibraryW(library);
    check(proxy != nullptr, "load monitored DX11 proxy");
    if (!instant_exit) {
        const auto create = reinterpret_cast<PFN_D3D11_CREATE_DEVICE>(GetProcAddress(proxy, "D3D11CreateDevice"));
        check(create != nullptr, "locate monitored D3D11CreateDevice");
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        check(SUCCEEDED(create(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
            &device, nullptr, &context)), "initialize DX11 without creating a Present loop");
        const auto trigger = std::filesystem::path(library).parent_path() / L"crash";
        const auto deadline = GetTickCount64() + 15000;
        while (GetTickCount64() < deadline && !std::filesystem::exists(trigger)) Sleep(10);
        check(std::filesystem::exists(trigger), "receive simulated crash trigger");
    }
    // Force termination so the transport cannot rely on a DLL detach callback.
    TerminateProcess(GetCurrentProcess(), 0xc0000005u);
    return 1;
}

int run_child(const wchar_t* library, bool native_control, bool denied_interfaces, bool native_routing)
{
    if (native_control) {
        wchar_t system_dir[MAX_PATH]{};
        check(GetSystemDirectoryW(system_dir, MAX_PATH) != 0, "locate system runtime");
        const std::wstring native_path = std::wstring(system_dir) + L"\\d3d11.dll";
        HMODULE native = LoadLibraryW(native_path.c_str());
        const auto native_create = reinterpret_cast<PFN_D3D11_CREATE_DEVICE>(GetProcAddress(native, "D3D11CreateDevice"));
        check(native_create != nullptr, "resolve native creation control");
        ComPtr<ID3D11Device> native_both, native_only;
        ComPtr<ID3D11DeviceContext> native_context, native_late;
        check(SUCCEEDED(native_create(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &native_both, nullptr, &native_context)), "native both outputs");
        check(SUCCEEDED(native_create(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &native_only, nullptr, nullptr)), "native device only");
        native_only->GetImmediateContext(&native_late);
        const ULONG native_normal_refs = native_context->AddRef(); native_context->Release();
        const ULONG native_late_refs = native_late->AddRef(); native_late->Release();
        std::fprintf(stderr, "NATIVE refs normal=%lu late=%lu\n", native_normal_refs, native_late_refs);
        check(native_normal_refs == native_late_refs, "native late context control has equal ownership");
        ComPtr<ID3D11Device1> native1;
        check(SUCCEEDED(native_both.As(&native1)), "native extended device control");
        check(native_both->CreateDeferredContext(0,nullptr) == S_FALSE &&
            native1->CreateDeferredContext1(0,nullptr) == S_FALSE,
            "native deferred APIs accept validation-only outputs");
        ComPtr<ID3D11DeviceContext> deferred;
        ComPtr<ID3D11DeviceContext1> deferred1;
        check(SUCCEEDED(native_both->CreateDeferredContext(0,&deferred)) &&
            SUCCEEDED(native1->CreateDeferredContext1(0,&deferred1)), "native deferred creation control");
        check(references(deferred.Get()) == references(deferred1.Get()) &&
            deferred.Detach()->Release() == 0 && deferred1.Detach()->Release() == 0,
            "native deferred APIs transfer one balanced caller reference");
    }
    HMODULE proxy = LoadLibraryExW(library, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    check(proxy != nullptr, "load actual built proxy DLL and execute DllMain");
    for (const char* name : {"D3D11CreateDevice", "D3D11CreateDeviceAndSwapChain", "CBTProc"})
        check(GetProcAddress(proxy, name) != nullptr, "resolve actual proxy export");
    for (const WORD ordinal : {17, 18})
        check(GetProcAddress(proxy, MAKEINTRESOURCEA(ordinal)) != nullptr, "resolve creation export ordinal");
    check(!GetModuleHandleW(L"DWrite.dll") && !GetModuleHandleW(L"d2d1.dll"),
        "proxy loading does not initialize DirectWrite or Direct2D");
    const auto create = reinterpret_cast<PFN_D3D11_CREATE_DEVICE>(GetProcAddress(proxy, "D3D11CreateDevice"));
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
    const HRESULT created = create(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 1,
        D3D11_SDK_VERSION, &device, nullptr, &context);
    if (FAILED(created)) std::fprintf(stderr, "proxy CreateDevice HRESULT=%08lx\n", created);
    check(SUCCEEDED(created), "create WARP through loaded proxy export");
    check(device && context, "proxy returns actual device and immediate context");
    const auto beforeGetDevice = references(device.Get());
    {
        ComPtr<ID3D11Device> reported;
        context->GetDevice(&reported);
        check(reported.Get() == device.Get(), "public GetDevice preserves wrapper/native routing identity");
    }
    check(references(device.Get()) == beforeGetDevice, "public GetDevice transfers one balanced reference");

    auto** device_table = *reinterpret_cast<void***>(device.Get());
    auto** context_table = *reinterpret_cast<void***>(context.Get());
    check(belongs_to_proxy(device_table[3], proxy) != native_routing &&
        belongs_to_proxy(context_table[14], proxy) != native_routing,
        "creation uses the configured wrapper or native method-routing mode");
    if (denied_interfaces) {
        void* output = nullptr;
        const auto device_before = references(device.Get());
        const auto device_hr = device->QueryInterface(__uuidof(ID3D11Device1),&output);
        const bool device_null = output == nullptr;
        if (output) static_cast<IUnknown*>(output)->Release();
        const auto device_after = references(device.Get());
        output = nullptr;
        const auto context_before = references(context.Get());
        const auto context_hr = context->QueryInterface(__uuidof(ID3D11DeviceContext1),&output);
        const bool context_null = output == nullptr;
        if (output) static_cast<IUnknown*>(output)->Release();
        const auto context_after = references(context.Get());
        std::fprintf(stderr,"denied QI device refs%lu/%lu context refs%lu/%lu\n",
            device_before,device_after,context_before,context_after);
        check(device_hr == E_NOINTERFACE && context_hr == E_NOINTERFACE && device_null && context_null &&
            device_before == device_after && context_before == context_after,
            "configured QI rejection returns null without retaining the native interface");
        return 0;
    }
    const UINT values[] = {11, 22, 33, 44};
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = sizeof(values);
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA initial{};
    initial.pSysMem = values;
    ComPtr<ID3D11Buffer> source, staging;
    check(SUCCEEDED(device->CreateBuffer(&desc, &initial, &source)), "create buffer through loaded proxy");
    desc.BindFlags = 0;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    check(SUCCEEDED(device->CreateBuffer(&desc, nullptr, &staging)), "create proxy staging buffer");
    context->CopyResource(staging.Get(), source.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    check(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "read back through loaded proxy");
    const bool correct = std::memcmp(mapped.pData, values, sizeof(values)) == 0;
    context->Unmap(staging.Get(), 0);
    check(correct, "loaded proxy copy and Map preserve all source bytes");
    context->ClearState();
    ComPtr<ID3D11Device> device_only;
    check(SUCCEEDED(create(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &device_only, nullptr, nullptr)) && device_only,
        "proxy supports native default feature list and optional context output");
    ComPtr<ID3D11DeviceContext> late_context;
    device_only->GetImmediateContext(&late_context);
    const ULONG late_refs = late_context->AddRef(); late_context->Release();
    const ULONG normal_refs = context->AddRef(); context->Release();
    std::fprintf(stderr, "CREATION refs normal=%lu late=%lu\n", normal_refs, late_refs);
    ComPtr<ID3D11Device> device_one;
    check(SUCCEEDED(create(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &device_one, nullptr, nullptr)), "device for lazy context1");
    ComPtr<ID3D11Device1> device1;
    check(SUCCEEDED(device_one.As(&device1)), "device1 supported");
    ComPtr<ID3D11DeviceContext1> late1;
    device1->GetImmediateContext1(&late1);
    ComPtr<ID3D11ShaderResourceView> normal_ini, late_ini, late1_ini;
    context->PSGetShaderResources(120, 1, &normal_ini);
    late_context->PSGetShaderResources(120, 1, &late_ini);
    late1->PSGetShaderResources(120, 1, &late1_ini);
    std::fprintf(stderr, "INI bound normal=%d late=%d late1=%d\n", bool(normal_ini), bool(late_ini), bool(late1_ini));
    check(normal_ini && late_ini && late1_ini, "all creation routes bind the configured IniParams resource");
    check(late_refs == normal_refs, "late context creation preserves same COM ownership as ordinary creation");
    check(device->CreateDeferredContext(0,nullptr) == S_FALSE &&
        device1->CreateDeferredContext1(0,nullptr) == S_FALSE,
        "proxy deferred APIs preserve validation-only calls");
    ComPtr<ID3D11DeviceContext> deferred;
    ComPtr<ID3D11DeviceContext1> deferred1;
    check(SUCCEEDED(device->CreateDeferredContext(0,&deferred)) &&
        SUCCEEDED(device1->CreateDeferredContext1(0,&deferred1)), "proxy deferred context creation");
    const auto deferred_refs = references(deferred.Get()), deferred1_refs = references(deferred1.Get());
    const auto remaining = deferred.Detach()->Release(), remaining1 = deferred1.Detach()->Release();
    std::fprintf(stderr,"deferred refs%lu/%lu final%lu/%lu\n",deferred_refs,deferred1_refs,remaining,remaining1);
    check(deferred_refs == deferred1_refs && remaining == 0 && remaining1 == 0,
        "proxy deferred contexts have the same balanced lifetime as native control");
    ComPtr<ID3D11DeviceContext> context_only;
    check(SUCCEEDED(create(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, nullptr, nullptr, &context_only)) && context_only,
        "proxy supports optional device output");
    ComPtr<ID3D11Device> context_device;
    context_only->GetDevice(&context_device);
    check(context_device != nullptr, "context-only creation still returns its owning device");
    ComPtr<ID3D11Device1> full_device;
    ComPtr<ID3D11DeviceContext1> full_context;
    check(SUCCEEDED(device.As(&full_device)) && SUCCEEDED(context.As(&full_context)),
        "query extended interfaces for configured Context1 operations");
    exercise_context1(full_device.Get(),full_context.Get());
    check(SUCCEEDED(create(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, nullptr, nullptr, nullptr)),
        "proxy preserves native validation-only creation without outputs");
    exercise_overlay(proxy);
    // Keep the proxy loaded until process termination, as in its intended host.
    std::fputs("PASS: loaded proxy exports, overlay Present/resize, wrapped WARP device/context and buffer readback\n", stderr);
    return 0;
}

int run_parent(const wchar_t* library, const wchar_t* root)
{
    const auto base = std::filesystem::weakly_canonical(root);
    check(std::filesystem::is_directory(base), "proxy fixture parent already exists");
    bool in_build = false;
    for (const auto& part : base) in_build |= _wcsicmp(part.c_str(), L"build") == 0;
    check(in_build, "proxy fixture remains under build directory");
    const auto fixture = base / (L"proxy-smoke-" + std::to_wstring(GetCurrentProcessId()) +
        L"-" + std::to_wstring(GetTickCount64()));
    check(std::filesystem::create_directory(fixture), "create exclusive proxy fixture without overwriting files");
    const auto copied = fixture / L"d3d11.dll";
    std::filesystem::copy_file(library, copied);
    {
        std::ofstream ini(fixture / L"d3dx.ini", std::ios::binary);
        ini << "[Logging]\ncalls=0\ndebug=0\n[System]\nallow_platform_update=1\n[Hunting]\nhunting=1\n[Constants]\nx=1\n";
        check(ini.good(), "write owned isolated proxy configuration");
    }
    wchar_t executable[MAX_PATH]{};
    check(GetModuleFileNameW(nullptr, executable, MAX_PATH) != 0, "locate proxy smoke executable");
    const ULONGLONG deadline = GetTickCount64() + 20000;
    for (const auto* mode : {L"--child", L"--child-native-control", L"--child-hook-all", L"--child-denied-qi"}) {
        if (std::wstring(mode) == L"--child-hook-all") {
            std::ofstream ini(fixture / L"d3dx.ini",std::ios::binary | std::ios::trunc);
            ini << "[Logging]\ncalls=0\ndebug=0\n[System]\nallow_platform_update=1\nhook=all\n[Hunting]\nhunting=1\n[Constants]\nx=1\n";
            ini.close();
            check(!ini.fail(),"write owned native-routing configuration");
        }
        if (std::wstring(mode) == L"--child-denied-qi") {
            std::ofstream ini(fixture / L"d3dx.ini",std::ios::binary | std::ios::trunc);
            ini << "[Logging]\ncalls=0\ndebug=0\n[System]\nallow_platform_update=0\n[Hunting]\nhunting=1\n[Constants]\nx=1\n";
            ini.close();
            check(!ini.fail(),"write owned disabled-platform configuration");
        }
        check(GetTickCount64() < deadline,"proxy children remain within aggregate timeout");
        std::wstring command = L"\"" + std::wstring(executable) + L"\" " + mode + L" \"" + copied.wstring() + L"\"";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        check(CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
            nullptr, fixture.c_str(), &startup, &process) != FALSE, "launch isolated proxy child");
        CloseHandle(process.hThread);
        const ULONGLONG now = GetTickCount64();
        const ULONGLONG available = deadline > now ? deadline - now : 0;
        const DWORD wait = WaitForSingleObject(process.hProcess,
            static_cast<DWORD>(available < 10000 ? available : 10000));
        if (wait != WAIT_OBJECT_0) {
            TerminateProcess(process.hProcess, 124);
            WaitForSingleObject(process.hProcess, 5000);
        }
        DWORD code = 1;
        const BOOL obtained = GetExitCodeProcess(process.hProcess, &code);
        CloseHandle(process.hProcess);
        std::fprintf(stderr, "proxy child exit=%08lx wait=%08lx fixture=%ls\n", code, wait, fixture.c_str());
        check(wait == WAIT_OBJECT_0 && obtained && code == 0, "proxy child completes successfully within timeout");
    }
    // Only remove this process's exclusively created fixture after child exit.
    check(std::filesystem::weakly_canonical(fixture).parent_path() == base,
        "verify exact owned cleanup boundary");
    std::filesystem::remove_all(fixture);
    return 0;
}
}

int wmain(int argc, wchar_t** argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    try {
        check(argc == 3, "expected proxy DLL path and fixture parent, or --child and DLL path");
        const std::wstring mode(argv[1]);
        if (mode == L"--mcp-live") return serve_mcp_fixture(argv[2]);
        if (mode == L"--mcp-provenance") return serve_mcp_fixture(argv[2], true);
        if (mode == L"--mcp-alternating") return serve_mcp_alternating_fixture(argv[2]);
        if (mode == L"--mcp-pre-present-crash" || mode == L"--mcp-instant-crash")
            return serve_mcp_loading_fixture(argv[2], mode == L"--mcp-instant-crash");
        if (mode == L"--child" || mode == L"--child-native-control" || mode == L"--child-hook-all" || mode == L"--child-denied-qi")
            return run_child(argv[2], mode == L"--child-native-control", mode == L"--child-denied-qi", mode == L"--child-hook-all");
        return run_parent(argv[1], argv[2]);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL loaded proxy smoke: %s (Win32=%lu)\n", error.what(), GetLastError());
        return 1;
    }
}
