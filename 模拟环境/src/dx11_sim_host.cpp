#include <Windows.h>
#include <d3d11_1.h>
#include <dxgi1_5.h>
#include <winternl.h>
#include <d3dkmthk.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
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
    void shared_primary_binding() const
    {
        const auto entry = reinterpret_cast<decltype(&D3DKMTGetSharedPrimaryHandle)>(
            GetProcAddress(module_, "D3DKMTGetSharedPrimaryHandle"));
        require(entry != nullptr, "F291: resolve actual shared-primary export");
        HMODULE entry_module = nullptr;
        require(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(entry), &entry_module) && entry_module == module_,
            "F291: resolved export belongs to the actual proxy module");
        const auto* base = reinterpret_cast<const unsigned char*>(module_);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        require(dos->e_magic == IMAGE_DOS_SIGNATURE, "F291: loaded proxy PE header");
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        require(nt->Signature == IMAGE_NT_SIGNATURE, "F291: loaded proxy NT header");
        const auto size = nt->OptionalHeader.SizeOfImage;
        const auto* code = reinterpret_cast<const unsigned char*>(entry);
        const auto offset = code - base;
        require(offset >= 0 && static_cast<size_t>(offset) + 6 <= size && code[0] == 0xff && code[1] == 0x25,
            "F291: current x64 export is an indirect import jump, not the C++ stub body");
        int32_t displacement = 0; std::memcpy(&displacement, code + 2, sizeof(displacement));
        const auto slot = static_cast<int64_t>(offset) + 6 + displacement;
        require(slot >= 0 && static_cast<uint64_t>(slot) + sizeof(FARPROC) <= size, "F291: import slot stays inside loaded image");
        FARPROC target = nullptr; std::memcpy(&target, base + slot, sizeof(target));
        HMODULE gdi32 = GetModuleHandleW(L"gdi32.dll");
        require(gdi32 != nullptr, "F291: system GDI32 is loaded");
        wchar_t native_path[MAX_PATH]{}, system_path[MAX_PATH]{};
        require(GetModuleFileNameW(gdi32, native_path, MAX_PATH) && GetSystemDirectoryW(system_path, MAX_PATH) &&
            std::filesystem::equivalent(native_path, std::filesystem::path(system_path) / L"gdi32.dll"),
            "F291: binding comparison uses the real system GDI32");
        const auto native = reinterpret_cast<decltype(entry)>(GetProcAddress(gdi32, "D3DKMTGetSharedPrimaryHandle"));
        require(native != nullptr && target == reinterpret_cast<FARPROC>(native), "F291: export forwards to the exact native GDI32 entry");
        D3DKMT_GETSHAREDPRIMARYHANDLE request{}, native_request{};
        const auto status = entry(&request), native_status = native(&native_request);
        require(status == native_status && request.hSharedPrimary == native_request.hSharedPrimary,
            "F291: invalid-window request preserves native status and output behavior");
        std::printf("F291 EXPORT_BINDING=System32/GDI32 PROXY_RVA=%llx NATIVE_STATUS=%08lx\n",
            static_cast<unsigned long long>(offset), static_cast<unsigned long>(native_status));
    }
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

void shader_export_failure(Device& gpu)
{
    HMODULE compiler = LoadLibraryExW(L"d3dcompiler_47.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    require(compiler != nullptr, "F003: load actual system shader compiler");
    const auto compile = reinterpret_cast<decltype(&D3DCompile)>(GetProcAddress(compiler, "D3DCompile"));
    require(compile != nullptr, "F003: resolve actual compiler entry");
    const char source[] = "float4 main():SV_Target{return float4(1,0,0,1);}";
    ComPtr<ID3DBlob> code, errors;
    succeeded(compile(source, sizeof(source)-1, nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, &code, &errors),
        "F003: compile real pixel shader before export failure");
    const std::string original(static_cast<const char*>(code->GetBufferPointer()), code->GetBufferSize());
    const auto read = [](const char* path) {
        std::ifstream input(path, std::ios::binary); require(input.good(), "F003: read actual fixture/log");
        return std::string(std::istreambuf_iterator<char>(input), {});
    };
    const auto before = read("d3d11_log.txt");
    ComPtr<ID3D11PixelShader> pixel;
    succeeded(gpu.device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &pixel),
        "F003: optional assembly export failure cannot override native shader creation success");
    require(pixel && !std::memcmp(original.data(), code->GetBufferPointer(), original.size()), "F003: retain shader and exact input bytecode");
    gpu.context->PSSetShader(pixel.Get(), nullptr, 0);
    ComPtr<ID3D11PixelShader> bound;
    gpu.context->PSGetShader(&bound, nullptr, nullptr);
    require(bound.Get() == pixel.Get(), "F003: successful shader remains usable in the real pipeline");
    const auto after = read("d3d11_log.txt");
    require(after.size() >= before.size() && after.compare(0, before.size(), before) == 0, "F003: diagnostic append preserves earlier log");
    const auto appended = after.substr(before.size());
    const auto contains = [&](const std::string& text) {
        if (appended.find(text) != std::string::npos) return true;
        std::string wide;
        for (const auto character : text) { wide.push_back(character); wide.push_back('\0'); }
        return appended.find(wide) != std::string::npos;
    };
    require(contains("error: ") && contains("storing disassembly"), "F003: actual failed text export is reported as a failed HRESULT");
    require(read("cache-blocker") == "F003 owned blocker", "F003: failed export cannot modify an existing non-directory");
}

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

void extensionless_resource(Device& gpu)
{
    ComPtr<ID3D11ShaderResourceView> view;
    gpu.context->PSGetShaderResources(0,1,&view);
    require(view != nullptr, "F108: Constants binds the actual extensionless resource");
    ComPtr<ID3D11Resource> resource; view->GetResource(&resource);
    ComPtr<ID3D11Texture2D> texture, staging;
    succeeded(resource.As(&texture), "F108: bound resource is a real texture");
    D3D11_TEXTURE2D_DESC desc{}; texture->GetDesc(&desc);
    const bool rgba = desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM || desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    const bool bgra = desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM || desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    require(desc.Width==1 && desc.Height==1 && (rgba || bgra), "F108: expected fixture dimensions and native pixel layout");
    desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=desc.MiscFlags=0; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    succeeded(gpu.device->CreateTexture2D(&desc,nullptr,&staging), "F108: actual DLL resource readback");
    gpu.context->CopyResource(staging.Get(),texture.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    succeeded(gpu.context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped), "F108: Map extensionless texture pixels");
    std::array<unsigned char,4> actual{}; std::memcpy(actual.data(),mapped.pData,actual.size()); gpu.context->Unmap(staging.Get(),0);
    if(bgra) std::swap(actual[0],actual[2]);
    require(actual==std::array<unsigned char,4>{128,64,32,255}, "F108: exact original PNG pixels survive loading without an extension");
}

void constants_values(ID3D11Device* device, ID3D11DeviceContext* context)
{
    ComPtr<ID3D11ShaderResourceView> view;
    context->PSGetShaderResources(120, 1, &view);
    require(view != nullptr, "configuration: valid constants create the real IniParams view");
    ComPtr<ID3D11Resource> resource;
    view->GetResource(&resource);
    ComPtr<ID3D11Texture1D> source;
    succeeded(resource.As(&source), "configuration: obtain the real IniParams texture");
    D3D11_TEXTURE1D_DESC desc{};
    source->GetDesc(&desc);
    require(desc.Width >= 1 && desc.Format == DXGI_FORMAT_R32G32B32A32_FLOAT,
        "configuration: IniParams has the expected float4 layout");
    desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = desc.MiscFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture1D> staging;
    succeeded(device->CreateTexture1D(&desc, nullptr, &staging), "configuration: create constants readback");
    context->CopyResource(staging.Get(), source.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    succeeded(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "configuration: Map actual constants");
    float values[4]{};
    std::memcpy(values, mapped.pData, sizeof(values));
    context->Unmap(staging.Get(), 0);
    require(values[0] == 7 && values[1] == 9, "configuration: real DLL continues the Constants list and uploads exact values");
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

void swapchain(const Runtime& runtime, bool check_query4 = false, bool expect_proxy = false, bool check_constants = false)
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
    if (check_constants) constants_values(device.Get(), context.Get());
    if (check_query4) {
        auto query = [&]() {
            void* output = nullptr;
            const HRESULT result = chain->QueryInterface(__uuidof(IDXGISwapChain4), &output);
            if (expect_proxy) require(result == E_NOINTERFACE && !output, "F178: unsupported wrapper interface returns no object");
            else {
                require(SUCCEEDED(result) && output, "F178: native control actually supports SwapChain4");
                reinterpret_cast<IUnknown*>(output)->Release();
            }
        };
        query(); // Warm native lazy state before comparing relative counts.
        const ULONG before = chain->AddRef(); chain->Release();
        for (unsigned i = 0; i < 32; ++i) query();
        const ULONG after = chain->AddRef(); chain->Release();
        std::printf("F178 relative references before=%lu after=%lu proxy=%d\n", before, after, expect_proxy); std::fflush(stdout);
        require(before == after, "F178: rejecting/returning QI must not retain an extra native reference");
    }
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

enum class LoaderProbe { Oversized, ProfileAllow, ProfileDeny };

static void loader_file_probe(const wchar_t* path, LoaderProbe probe)
{
    const auto proxy = std::filesystem::canonical(path);
    wchar_t executable[MAX_PATH]{}, system[MAX_PATH]{};
    require(GetModuleFileNameW(nullptr, executable, MAX_PATH) && GetSystemDirectoryW(system, MAX_PATH),
        "F163: real loader-test paths");
    const auto executable_directory = std::filesystem::canonical(executable).parent_path().wstring();
    if (probe != LoaderProbe::Oversized)
        require(!_wcsicmp(std::filesystem::path(executable).filename().c_str(), L"rundll32.exe"),
            "F163: profile-helper fixture has the actual rundll32.exe basename");
    const auto proxy_directory = proxy.parent_path().wstring();
    require(_wcsicmp(proxy_directory.c_str(), executable_directory.c_str()) &&
        !wcsstr(proxy_directory.c_str(), executable_directory.c_str()), "F163: proxy reaches the outside-directory INI check");
    const auto ini = proxy.parent_path() / L"d3dx.ini";
    HANDLE input = CreateFileW(ini.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    require(input != INVALID_HANDLE_VALUE, "F163: actual outside-directory loader INI");
    LARGE_INTEGER size{}; const BOOL sized = GetFileSizeEx(input, &size); CloseHandle(input);
    require(sized && (probe == LoaderProbe::ProfileDeny ? size.QuadPart > 0 && size.QuadPart <= MAXDWORD : size.QuadPart > MAXDWORD),
        "F163: actual INI size distinguishes overflow rejection from the profile-helper early decision");
    // Keep the proxy's existing native hook dependencies available, as in the
    // normal component host; no missing provider is replaced by a test stub.
    const auto dxgi = std::filesystem::path(system) / L"dxgi.dll";
    require(LoadLibraryExW(dxgi.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32) != nullptr, "F163: real native DXGI provider");
    const auto d3d11 = std::filesystem::path(system) / L"d3d11.dll";
    Runtime native(d3d11.c_str());
    HMODULE loaded = LoadLibraryExW(proxy.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    const DWORD error = GetLastError();
    if (loaded) {
        wchar_t actual[MAX_PATH]{};
        require(GetModuleFileNameW(loaded, actual, MAX_PATH) && std::filesystem::equivalent(actual, proxy),
            "F163: accepted module is the exact requested production DLL file");
    }
    std::printf("F163 DLL_LOAD=%s error=%lu logical=%lld path=%s\n", loaded ? "accepted" : "rejected",
        error, size.QuadPart, utf8(proxy.c_str()).c_str());
    // A successfully loaded production proxy owns process-wide hooks until
    // this isolated child exits; unloading is outside this test's contract.
    require(probe == LoaderProbe::ProfileAllow ? loaded != nullptr : !loaded && error == ERROR_DLL_INIT_FAILED,
        "F163: real DllMain preserves the size/profile-helper allow and reject contracts");
}

int wmain(int argc, wchar_t** argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    try {
        require(argc == 3, "usage: dx11_sim_host.exe <absolute DLL path> <scenario>");
        require(std::filesystem::path(argv[1]).is_absolute(), "runtime path must be absolute");
        const std::wstring scenario(argv[2]);
        if (scenario == L"loader-size-rejection") { loader_file_probe(argv[1], LoaderProbe::Oversized); return 0; }
        if (scenario == L"loader-profile-allow,Install3DMigotoDriverProfile ") { loader_file_probe(argv[1], LoaderProbe::ProfileAllow); return 0; }
        if (scenario == L"loader-profile-deny") { loader_file_probe(argv[1], LoaderProbe::ProfileDeny); return 0; }
        Runtime runtime(argv[1]);
        if (scenario == L"injected-device-failure") injected_device_failure(runtime, argv[1]);
        else if (scenario == L"swapchain") swapchain(runtime);
        else if (scenario == L"constants-overlay-swapchain") swapchain(runtime, false, false, true);
        else if (scenario == L"swapchain-qi") {
            wchar_t system_directory[MAX_PATH]{};
            require(GetSystemDirectoryW(system_directory, MAX_PATH) != 0, "F178: locate native comparison path");
            const auto native = std::filesystem::path(system_directory) / L"d3d11.dll";
            swapchain(runtime, true, !std::filesystem::equivalent(native, std::filesystem::path(argv[1])));
        }
        else {
            Device gpu(runtime);
            if (scenario == L"capabilities") capabilities(gpu);
            else if (scenario == L"buffer") buffer_copy(gpu);
            else if (scenario == L"texture") texture_clear(gpu);
            else if (scenario == L"constants-missing-name" || scenario == L"constants-overlay-device") constants_values(gpu.device.Get(), gpu.context.Get());
            else if (scenario == L"deferred") deferred_replay(gpu);
            else if (scenario == L"kmt-binding") runtime.shared_primary_binding();
            else if (scenario == L"shader-export-failure") shader_export_failure(gpu);
            else if (scenario == L"resource-extensionless") extensionless_resource(gpu);
            else throw std::runtime_error("unknown scenario");
        }
        std::puts("PASS: actual runtime scenario");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s (Win32=%lu)\n", error.what(), GetLastError());
        return 1;
    }
}
