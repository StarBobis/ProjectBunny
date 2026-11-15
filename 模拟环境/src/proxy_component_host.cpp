#include <Windows.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>

using Microsoft::WRL::ComPtr;
namespace {
void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
std::wstring system_dll(const wchar_t* name)
{
    wchar_t directory[MAX_PATH]{};
    check(GetSystemDirectoryW(directory, MAX_PATH) != 0, "locate real system runtime");
    return std::wstring(directory) + L"\\" + name;
}
HMODULE load_exact(const std::filesystem::path& path)
{
    HMODULE module = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    check(module != nullptr, "load requested component DLL");
    wchar_t actual[32768]{};
    check(GetModuleFileNameW(module, actual, static_cast<DWORD>(std::size(actual))) != 0 &&
        std::filesystem::equivalent(path, std::filesystem::path(actual)), "loaded component must be the requested file");
    const std::wstring wide(actual);
    const int size = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    std::string text(size, '\0');
    check(size > 0 && WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), text.data(), size, nullptr, nullptr) == size,
        "encode actual loaded path");
    std::printf("LOADED_MODULE=%s\n", text.c_str()); std::fflush(stdout);
    // Proxy unloading/loader-lock stress is a separate frozen task.
    return module;
}
template<class Function> Function resolve(HMODULE module, const char* name)
{
    const auto function = reinterpret_cast<Function>(GetProcAddress(module, name));
    check(function != nullptr, "resolve required public component export");
    return function;
}
void compiler(HMODULE module)
{
    const auto compile = resolve<decltype(&D3DCompile)>(module, "D3DCompile");
    const auto disassemble = resolve<decltype(&D3DDisassemble)>(module, "D3DDisassemble");
    const auto create_blob = resolve<decltype(&D3DCreateBlob)>(module, "D3DCreateBlob");
    const char source[] = "float4 main():SV_Target{return float4(1,2,3,4);}";
    ComPtr<ID3DBlob> code, errors, assembly, scratch;
    const HRESULT result = compile(source, sizeof(source)-1, "simulation.hlsl", nullptr, nullptr, "main", "ps_5_0", 0, 0, &code, &errors);
    check(SUCCEEDED(result) && code && code->GetBufferSize() > 32 && !std::memcmp(code->GetBufferPointer(), "DXBC", 4),
        "public compiler produces actual DXBC");
    check(SUCCEEDED(disassemble(code->GetBufferPointer(), code->GetBufferSize(), 0, nullptr, &assembly)) && assembly,
        "public disassembler consumes compiled bytecode");
    const std::string text(static_cast<const char*>(assembly->GetBufferPointer()), assembly->GetBufferSize());
    check(text.find("ps_5_0") != std::string::npos, "disassembly preserves expected shader model");
    check(SUCCEEDED(create_blob(64, &scratch)) && scratch && scratch->GetBufferSize() == 64,
        "public blob creation preserves requested size");
    std::memset(scratch->GetBufferPointer(), 0x5a, 64);
    if (HMODULE provider = GetModuleHandleW(L"D3DCompiler_46_org.dll")) {
        const auto native_compile = resolve<decltype(&D3DCompile)>(provider, "D3DCompile");
        ComPtr<ID3DBlob> expected, native_errors;
        check(SUCCEEDED(native_compile(source, sizeof(source)-1, "simulation.hlsl", nullptr, nullptr, "main", "ps_5_0", 0, 0,
            &expected, &native_errors)) && expected && expected->GetBufferSize() == code->GetBufferSize() &&
            !std::memcmp(expected->GetBufferPointer(), code->GetBufferPointer(), code->GetBufferSize()),
            "proxy compilation bytes equal the real compatible native provider");
    }
    ComPtr<ID3DBlob> failed, diagnostics;
    const char invalid[] = "this is not valid hlsl";
    check(FAILED(compile(invalid, sizeof(invalid)-1, "simulation.hlsl", nullptr, nullptr, "main", "ps_5_0", 0, 0,
        &failed, &diagnostics)) && !failed && diagnostics && diagnostics->GetBufferSize(),
        "invalid source preserves native failure and diagnostic blob");
    std::puts("PASS: compiler compile/disassemble/blob/failure and native-provider equivalence");
}
void dxgi(HMODULE module)
{
    using CreateFactory = HRESULT (WINAPI*)(REFIID, void**);
    const auto create = resolve<CreateFactory>(module, "CreateDXGIFactory1");
    ComPtr<IDXGIFactory4> factory;
    check(SUCCEEDED(create(IID_PPV_ARGS(&factory))) && factory, "actual DXGI factory creation succeeds");
    ComPtr<IDXGIAdapter1> adapter;
    check(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))) && adapter, "factory enumerates the actual WARP adapter");
    DXGI_ADAPTER_DESC1 desc{};
    check(SUCCEEDED(adapter->GetDesc1(&desc)) && (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE),
        "returned adapter is software rather than an assumed hardware substitute");
    std::puts("PASS: DXGI factory and real WARP adapter");
}
}
int wmain(int argc, wchar_t** argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    try {
        check(argc == 3 && std::filesystem::path(argv[1]).is_absolute(), "usage: component-host <absolute DLL> <compiler|dxgi>");
        // Resolve native dependencies before the DXGI proxy shadows its name.
        load_exact(system_dll(L"dxgi.dll"));
        load_exact(system_dll(L"d3d11.dll"));
        HMODULE module = load_exact(argv[1]);
        if (!std::wcscmp(argv[2], L"compiler")) compiler(module);
        else if (!std::wcscmp(argv[2], L"dxgi")) dxgi(module);
        else throw std::runtime_error("unknown component scenario");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL component: %s (Win32=%lu)\n", error.what(), GetLastError());
        return 1;
    }
}
