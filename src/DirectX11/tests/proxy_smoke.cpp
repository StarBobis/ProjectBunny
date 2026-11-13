#include <Windows.h>
#include <d3d11.h>
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

int run_child(const wchar_t* library)
{
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
    check(belongs_to_proxy(device_table[3], proxy) && belongs_to_proxy(context_table[14], proxy),
        "returned CreateBuffer and Map methods execute loaded proxy code");
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
    ComPtr<ID3D11DeviceContext> context_only;
    check(SUCCEEDED(create(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, nullptr, nullptr, &context_only)) && context_only,
        "proxy supports optional device output");
    ComPtr<ID3D11Device> context_device;
    context_only->GetDevice(&context_device);
    check(context_device != nullptr, "context-only creation still returns its owning device");
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
    const auto fixture = base / (L"proxy-smoke-" + std::to_wstring(GetCurrentProcessId()));
    check(std::filesystem::create_directory(fixture), "create exclusive proxy fixture without overwriting files");
    const auto copied = fixture / L"d3d11.dll";
    std::filesystem::copy_file(library, copied);
    {
        std::ofstream ini(fixture / L"d3dx.ini", std::ios::binary);
        ini << "[Logging]\ncalls=0\ndebug=0\n[Hunting]\nhunting=0\n";
        check(ini.good(), "write owned isolated proxy configuration");
    }
    wchar_t executable[MAX_PATH]{};
    check(GetModuleFileNameW(nullptr, executable, MAX_PATH) != 0, "locate proxy smoke executable");
    std::wstring command = L"\"" + std::wstring(executable) + L"\" --child \"" + copied.wstring() + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    check(CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
        nullptr, fixture.c_str(), &startup, &process) != FALSE, "launch isolated proxy child");
    CloseHandle(process.hThread);
    const DWORD wait = WaitForSingleObject(process.hProcess, 20000);
    if (wait != WAIT_OBJECT_0) {
        TerminateProcess(process.hProcess, 124);
        WaitForSingleObject(process.hProcess, 5000);
    }
    DWORD code = 1;
    const BOOL obtained = GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess);
    std::fprintf(stderr, "proxy child exit=%08lx wait=%08lx fixture=%ls\n", code, wait, fixture.c_str());
    check(wait == WAIT_OBJECT_0 && obtained && code == 0, "proxy child completes successfully within timeout");
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
        return std::wstring(argv[1]) == L"--child" ? run_child(argv[2]) : run_parent(argv[1], argv[2]);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL loaded proxy smoke: %s (Win32=%lu)\n", error.what(), GetLastError());
        return 1;
    }
}
