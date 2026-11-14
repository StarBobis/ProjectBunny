#include <Windows.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include "DX11Version.h"

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
    for (const WORD font : {IDR_COURIERBOLD, IDR_COURIERSMALL, IDR_ARIAL})
        check(FindResourceW(proxy, MAKEINTRESOURCEW(font), MAKEINTRESOURCEW(SPRITEFONT)) != nullptr,
            "actual built DLL contains required overlay font resource");
    const auto create = reinterpret_cast<PFN_D3D11_CREATE_DEVICE>(GetProcAddress(proxy, "D3D11CreateDevice"));
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
    const HRESULT created = create(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 1,
        D3D11_SDK_VERSION, &device, nullptr, &context);
    if (FAILED(created)) std::fprintf(stderr, "proxy CreateDevice HRESULT=%08lx\n", created);
    check(SUCCEEDED(created), "create WARP through loaded proxy export");
    check(device && context, "proxy returns actual device and immediate context");
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
    // Keep the proxy loaded until process termination, as in its intended host.
    std::fputs("PASS: loaded proxy exports, resources, wrapped WARP device/context and buffer readback\n", stderr);
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
        ini << "[Logging]\ncalls=0\ndebug=0\n[System]\nallow_platform_update=1\n[Hunting]\nhunting=0\n[Constants]\nx=1\n";
        check(ini.good(), "write owned isolated proxy configuration");
    }
    wchar_t executable[MAX_PATH]{};
    check(GetModuleFileNameW(nullptr, executable, MAX_PATH) != 0, "locate proxy smoke executable");
    const ULONGLONG deadline = GetTickCount64() + 20000;
    for (const auto* mode : {L"--child", L"--child-native-control", L"--child-hook-all", L"--child-denied-qi"}) {
        if (std::wstring(mode) == L"--child-hook-all") {
            std::ofstream ini(fixture / L"d3dx.ini",std::ios::binary | std::ios::trunc);
            ini << "[Logging]\ncalls=0\ndebug=0\n[System]\nallow_platform_update=1\nhook=all\n[Hunting]\nhunting=0\n[Constants]\nx=1\n";
            ini.close();
            check(!ini.fail(),"write owned native-routing configuration");
        }
        if (std::wstring(mode) == L"--child-denied-qi") {
            std::ofstream ini(fixture / L"d3dx.ini",std::ios::binary | std::ios::trunc);
            ini << "[Logging]\ncalls=0\ndebug=0\n[System]\nallow_platform_update=0\n[Hunting]\nhunting=0\n[Constants]\nx=1\n";
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
        if (mode == L"--child" || mode == L"--child-native-control" || mode == L"--child-hook-all" || mode == L"--child-denied-qi")
            return run_child(argv[2], mode == L"--child-native-control", mode == L"--child-denied-qi", mode == L"--child-hook-all");
        return run_parent(argv[1], argv[2]);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL loaded proxy smoke: %s (Win32=%lu)\n", error.what(), GetLastError());
        return 1;
    }
}
