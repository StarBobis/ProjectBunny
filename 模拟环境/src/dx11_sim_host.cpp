#include <Windows.h>
#include <d3d11_1.h>
#include <wrl/client.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>

using Microsoft::WRL::ComPtr;

namespace {
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void succeeded(HRESULT result, const char* message)
{
    if (FAILED(result)) {
        std::fprintf(stderr, "HRESULT=%08lx: %s\n", static_cast<unsigned long>(result), message);
        throw std::runtime_error(message);
    }
}

std::string utf8(const std::wstring& text)
{
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
        nullptr, 0, nullptr, nullptr);
    require(length > 0, "convert module path to UTF-8");
    std::string result(length, '\0');
    require(WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
        result.data(), length, nullptr, nullptr) == length, "write UTF-8 module path");
    return result;
}

class Runtime {
public:
    explicit Runtime(const wchar_t* path)
    {
        module_ = LoadLibraryExW(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        require(module_ != nullptr, "load requested runtime");
        wchar_t loaded[32768]{};
        const DWORD length = GetModuleFileNameW(module_, loaded, static_cast<DWORD>(std::size(loaded)));
        require(length > 0 && length < std::size(loaded), "identify loaded runtime path");
        require(std::filesystem::equivalent(std::filesystem::path(path), std::filesystem::path(loaded)),
            "loaded runtime must be the requested DLL file");
        std::printf("LOADED_MODULE=%s\n", utf8(loaded).c_str());
        std::fflush(stdout);
        create = reinterpret_cast<PFN_D3D11_CREATE_DEVICE>(GetProcAddress(module_, "D3D11CreateDevice"));
        create_swapchain = reinterpret_cast<PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN>(
            GetProcAddress(module_, "D3D11CreateDeviceAndSwapChain"));
        require(create && create_swapchain, "resolve device and swapchain creation exports");
    }
    // Production proxies install process-wide hooks. The child process owns the
    // module until process exit; unloading is a separate, currently frozen test.
    PFN_D3D11_CREATE_DEVICE create = nullptr;
    PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN create_swapchain = nullptr;
private:
    HMODULE module_ = nullptr;
};

class Device {
public:
    explicit Device(const Runtime& runtime)
    {
        const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
        succeeded(runtime.create(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 1,
            D3D11_SDK_VERSION, &device, &level, &context), "create real WARP device");
        require(device && context, "creation returns both interfaces");
    }
    ~Device()
    {
        if (context) { context->ClearState(); context->Flush(); }
    }
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL level{};
};

class HiddenWindow {
public:
    HiddenWindow()
    {
        instance_ = GetModuleHandleW(nullptr);
        class_name_ = L"BunnyDX11Simulation_" + std::to_wstring(GetCurrentProcessId());
        WNDCLASSW cls{};
        cls.lpfnWndProc = DefWindowProcW;
        cls.hInstance = instance_;
        cls.lpszClassName = class_name_.c_str();
        require(RegisterClassW(&cls) != 0, "register hidden fixture window");
        handle = CreateWindowExW(0, class_name_.c_str(), L"DX11 test fixture", WS_OVERLAPPEDWINDOW,
            0, 0, 64, 64, nullptr, nullptr, instance_, nullptr);
        if (!handle) { UnregisterClassW(class_name_.c_str(), instance_); }
        require(handle != nullptr, "create hidden swapchain window");
    }
    ~HiddenWindow()
    {
        if (handle) DestroyWindow(handle);
        UnregisterClassW(class_name_.c_str(), instance_);
    }
    HWND handle = nullptr;
private:
    HINSTANCE instance_ = nullptr;
    std::wstring class_name_;
};

void read_buffer(Device& gpu, ID3D11Buffer* buffer, const void* expected, size_t bytes)
{
    D3D11_BUFFER_DESC desc{};
    buffer->GetDesc(&desc);
    require(bytes == desc.ByteWidth, "readback checks the complete buffer");
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = desc.MiscFlags = desc.StructureByteStride = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Buffer> staging;
    succeeded(gpu.device->CreateBuffer(&desc, nullptr, &staging), "create buffer readback");
    gpu.context->CopyResource(staging.Get(), buffer);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    succeeded(gpu.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Map buffer readback");
    const bool equal = std::memcmp(mapped.pData, expected, bytes) == 0;
    gpu.context->Unmap(staging.Get(), 0);
    require(equal, "buffer readback bytes equal deterministic fixture");
}

void capabilities(Device& gpu)
{
    ComPtr<ID3D11Device1> device1;
    ComPtr<ID3D11DeviceContext1> context1;
    const HRESULT device_hr = gpu.device.As(&device1), context_hr = gpu.context.As(&context1);
    D3D11_FEATURE_DATA_D3D11_OPTIONS options{};
    const HRESULT options_hr = gpu.device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS,
        &options, sizeof(options));
    std::printf("DRIVER=WARP FEATURE_LEVEL=%04x DEVICE1_HR=%08lx CONTEXT1_HR=%08lx OPTIONS_HR=%08lx\n",
        static_cast<unsigned>(gpu.level), static_cast<unsigned long>(device_hr),
        static_cast<unsigned long>(context_hr), static_cast<unsigned long>(options_hr));
    if (SUCCEEDED(options_hr)) {
        std::printf("CB_OFFSETTING=%d PARTIAL_CB_UPDATE=%d\n", options.ConstantBufferOffsetting,
            options.ConstantBufferPartialUpdate);
    }
    // This scenario records capabilities; it does not certify optional features.
}

void buffer_copy(Device& gpu)
{
    std::array<unsigned, 256> expected{};
    for (size_t i = 0; i < expected.size(); ++i) expected[i] = static_cast<unsigned>(i * 1664525u + 1013904223u);
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = static_cast<UINT>(sizeof(expected));
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA initial{};
    initial.pSysMem = expected.data();
    ComPtr<ID3D11Buffer> buffer;
    succeeded(gpu.device->CreateBuffer(&desc, &initial, &buffer), "create initialized buffer");
    read_buffer(gpu, buffer.Get(), expected.data(), sizeof(expected));
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ComPtr<ID3D11Buffer> dynamic;
    succeeded(gpu.device->CreateBuffer(&desc, nullptr, &dynamic), "create dynamic buffer");
    D3D11_MAPPED_SUBRESOURCE mapped{};
    succeeded(gpu.context->Map(dynamic.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped), "Map dynamic buffer");
    std::memcpy(mapped.pData, expected.data(), sizeof(expected));
    gpu.context->Unmap(dynamic.Get(), 0);
    read_buffer(gpu, dynamic.Get(), expected.data(), sizeof(expected));
    desc.ByteWidth = 0;
    ComPtr<ID3D11Buffer> invalid;
    const HRESULT result = gpu.device->CreateBuffer(&desc, nullptr, &invalid);
    require(FAILED(result) && !invalid, "invalid zero-size buffer fails without returning an object");
    // Invalid descriptors test validation, not allocation failure with valid input.
}

void read_texture(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* texture,
    const std::array<unsigned char, 4>& expected)
{
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    require(desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM && desc.SampleDesc.Count == 1,
        "readback fixture uses single-sample RGBA8");
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = desc.MiscFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    succeeded(device->CreateTexture2D(&desc, nullptr, &staging), "create texture readback");
    context->CopyResource(staging.Get(), texture);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    succeeded(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Map texture readback");
    bool equal = mapped.RowPitch >= desc.Width * 4;
    for (UINT y = 0; equal && y < desc.Height; ++y) {
        const auto* row = static_cast<const unsigned char*>(mapped.pData) + size_t(y) * mapped.RowPitch;
        for (UINT x = 0; equal && x < desc.Width; ++x) {
            for (unsigned c = 0; c < 4; ++c) {
                const int difference = int(row[x * 4 + c]) - int(expected[c]);
                if (difference < -1 || difference > 1) equal = false;
            }
        }
    }
    context->Unmap(staging.Get(), 0);
    require(equal, "all texture pixels match expected clear color, respecting RowPitch");
}

void texture_clear(Device& gpu)
{
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = 7; desc.Height = 5; desc.MipLevels = desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11RenderTargetView> view;
    succeeded(gpu.device->CreateTexture2D(&desc, nullptr, &texture), "create odd-size render texture");
    succeeded(gpu.device->CreateRenderTargetView(texture.Get(), nullptr, &view), "create render view");
    const float color[] = {0, 0.5f, 1, 1};
    gpu.context->ClearRenderTargetView(view.Get(), color);
    read_texture(gpu.device.Get(), gpu.context.Get(), texture.Get(), {0, 128, 255, 255});
}

void deferred_replay(Device& gpu)
{
    std::array<unsigned, 64> expected{};
    for (size_t i = 0; i < expected.size(); ++i) expected[i] = static_cast<unsigned>(i * 31 + 7);
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = static_cast<UINT>(sizeof(expected)); desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    ComPtr<ID3D11Buffer> buffer;
    succeeded(gpu.device->CreateBuffer(&desc, nullptr, &buffer), "create deferred replay buffer");
    ComPtr<ID3D11DeviceContext> deferred;
    succeeded(gpu.device->CreateDeferredContext(0, &deferred), "create deferred context");
    require(deferred->GetType() == D3D11_DEVICE_CONTEXT_DEFERRED, "returned context is deferred");
    deferred->UpdateSubresource(buffer.Get(), 0, nullptr, expected.data(), 0, 0);
    ComPtr<ID3D11CommandList> commands;
    succeeded(deferred->FinishCommandList(FALSE, &commands), "finish real deferred command list");
    gpu.context->ExecuteCommandList(commands.Get(), FALSE);
    read_buffer(gpu, buffer.Get(), expected.data(), sizeof(expected));
}

void swapchain(const Runtime& runtime)
{
    HiddenWindow window;
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferDesc.Width = desc.BufferDesc.Height = 16;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1; desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 1; desc.OutputWindow = window.handle; desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    ComPtr<IDXGISwapChain> chain;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
    succeeded(runtime.create_swapchain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 1,
        D3D11_SDK_VERSION, &desc, &chain, &device, nullptr, &context), "create hidden WARP swapchain");
    ComPtr<ID3D11Texture2D> buffer;
    succeeded(chain->GetBuffer(0, IID_PPV_ARGS(&buffer)), "get actual backbuffer");
    ComPtr<ID3D11RenderTargetView> view;
    succeeded(device->CreateRenderTargetView(buffer.Get(), nullptr, &view), "create backbuffer view");
    const float red[] = {1, 0, 0, 1};
    context->ClearRenderTargetView(view.Get(), red);
    read_texture(device.Get(), context.Get(), buffer.Get(), {255, 0, 0, 255});
    succeeded(chain->Present(0, 0), "Present hidden swapchain");
    context->ClearState(); context->Flush(); view.Reset(); buffer.Reset();
    succeeded(chain->ResizeBuffers(1, 32, 16, DXGI_FORMAT_R8G8B8A8_UNORM, 0), "resize unbound backbuffers");
    succeeded(chain->GetBuffer(0, IID_PPV_ARGS(&buffer)), "get resized backbuffer");
    D3D11_TEXTURE2D_DESC actual{};
    buffer->GetDesc(&actual);
    require(actual.Width == 32 && actual.Height == 16, "resized backbuffer dimensions are correct");
    context->ClearState(); context->Flush();
}

void injected_device_failure(const Runtime& runtime, const wchar_t* runtime_path)
{
    // Inputs are the same valid WARP creation descriptors used by the native
    // controls. The chained test DLL deliberately returns E_OUTOFMEMORY.
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    HRESULT result = runtime.create(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 1,
        D3D11_SDK_VERSION, &device, nullptr, &context);
    require(result == E_OUTOFMEMORY && !device && !context,
        "proxy propagates injected device failure without returned objects");
    HiddenWindow window;
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferDesc.Width = desc.BufferDesc.Height = 16;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1; desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 1; desc.OutputWindow = window.handle; desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    ComPtr<IDXGISwapChain> chain;
    result = runtime.create_swapchain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 1,
        D3D11_SDK_VERSION, &desc, &chain, &device, nullptr, &context);
    require(result == E_OUTOFMEMORY && !chain && !device && !context,
        "proxy propagates injected swapchain creation failure without returned objects");
    HMODULE provider = GetModuleHandleW(L"dx11_fault_provider.dll");
    require(provider != nullptr, "failure actually reached the chained test provider");
    wchar_t provider_path[32768]{};
    const DWORD length = GetModuleFileNameW(provider, provider_path, static_cast<DWORD>(std::size(provider_path)));
    require(length > 0 && length < std::size(provider_path), "identify fault-provider path");
    const auto expected_path = std::filesystem::path(runtime_path).parent_path() / L"dx11_fault_provider.dll";
    require(std::filesystem::equivalent(expected_path, std::filesystem::path(provider_path)),
        "fault provider belongs to this case directory");
    using GetCounts = void (WINAPI*)(UINT*, UINT*);
    const auto get_counts = reinterpret_cast<GetCounts>(GetProcAddress(provider, "BunnyFaultGetCounts"));
    require(get_counts != nullptr, "resolve test-only injection counter");
    UINT device_count = 0, chain_count = 0;
    get_counts(&device_count, &chain_count);
    require(device_count == 1 && chain_count == 1, "both creation exports reached the injected failure once");
    std::printf("SYNTHETIC_FAILURE=E_OUTOFMEMORY DEVICE_CALLS=%u SWAPCHAIN_CALLS=%u PROVIDER=%s\n",
        device_count, chain_count, utf8(provider_path).c_str());
}
}

int wmain(int argc, wchar_t** argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    try {
        require(argc == 3, "usage: dx11_sim_host.exe <absolute DLL path> <scenario>");
        require(std::filesystem::path(argv[1]).is_absolute(), "runtime path must be absolute");
        const std::wstring scenario(argv[2]);
        Runtime runtime(argv[1]);
        if (scenario == L"injected-device-failure") injected_device_failure(runtime, argv[1]);
        else if (scenario == L"swapchain") swapchain(runtime);
        else {
            Device gpu(runtime);
            if (scenario == L"capabilities") capabilities(gpu);
            else if (scenario == L"buffer") buffer_copy(gpu);
            else if (scenario == L"texture") texture_clear(gpu);
            else if (scenario == L"deferred") deferred_replay(gpu);
            else throw std::runtime_error("unknown scenario");
        }
        std::puts("PASS: actual runtime scenario");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s (Win32=%lu)\n", error.what(), GetLastError());
        return 1;
    }
}
