#include <Windows.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>

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
constexpr ULONG_PTR kPoison = static_cast<ULONG_PTR>(0xCCCCCCCCCCCCCCCCull);
template<class T> T poison() { return reinterpret_cast<T>(kPoison); }
using AssembleFunction = HRESULT (WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, UINT, ID3DBlob**, ID3DBlob**);
const char* const kCompilerExports[] = {
    "D3DAssemble", "DebugSetMute", "D3DCompile2", "D3DCompile",
    "D3DCompileFromFile", "D3DCompressShaders", "D3DCreateBlob",
    "D3DDecompressShaders", "D3DDisassemble10Effect", "D3DDisassemble11Trace",
    "D3DDisassemble", "D3DDisassembleRegion", "D3DGetBlobPart",
    "D3DGetDebugInfo", "D3DGetInputAndOutputSignatureBlob",
    "D3DGetInputSignatureBlob", "D3DGetOutputSignatureBlob",
    "D3DGetTraceInstructionOffsets", "D3DPreprocess", "D3DReadFileToBlob",
    "D3DReflect", "D3DReturnFailure1", "D3DSetBlobPart", "D3DStripShader",
    "D3DWriteBlobToFile",
};
void compiler_exports(HMODULE module)
{
    for (int index = 0; index < static_cast<int>(sizeof(kCompilerExports) / sizeof(kCompilerExports[0])); ++index) {
        const FARPROC by_name = GetProcAddress(module, kCompilerExports[index]);
        check(by_name != nullptr, "compiler export is missing by name");
        const FARPROC by_ordinal = GetProcAddress(module, MAKEINTRESOURCEA(index + 1));
        check(by_ordinal != nullptr, "compiler export is missing by ordinal");
        check(by_name == by_ordinal, "compiler export name and ordinal resolve differently");
    }
    check(GetProcAddress(module, "DebugSetMute") == GetProcAddress(module, MAKEINTRESOURCEA(2)),
        "DebugSetMute must be export ordinal 2");
    std::puts("PASS: 25 compiler exports resolve by name and ordinal 1-25; DebugSetMute is ordinal 2");
}
void check_unavailable_exports(HMODULE module, HRESULT expected, bool compile_available)
{
    ID3DBlob* code = nullptr;
    ID3DBlob* errors = nullptr;
    if (!compile_available) {
        const auto compile = resolve<decltype(&D3DCompile)>(module, "D3DCompile");
        code = poison<ID3DBlob*>();
        errors = poison<ID3DBlob*>();
        const HRESULT result = compile("f107.hlsl", 9, "f107.hlsl", nullptr, nullptr, "main", "ps_5_0", 0, 0, &code, &errors);
        check(result == expected, "D3DCompile must return the unavailable HRESULT");
        check(code == nullptr && errors == nullptr, "D3DCompile outputs must be true nullptr");
    }
    const auto assemble = resolve<AssembleFunction>(module, "D3DAssemble");
    ID3DBlob* shader = poison<ID3DBlob*>();
    ID3DBlob* messages = poison<ID3DBlob*>();
    HRESULT result = assemble(nullptr, 0, nullptr, nullptr, nullptr, 0, &shader, &messages);
    check(result == expected, "D3DAssemble must return the unavailable HRESULT");
    check(shader == nullptr && messages == nullptr, "D3DAssemble outputs must be true nullptr");
    const auto disassemble = resolve<decltype(&D3DDisassemble)>(module, "D3DDisassemble");
    ID3DBlob* disassembly = poison<ID3DBlob*>();
    result = disassemble(nullptr, 0, 0, nullptr, &disassembly);
    check(result == expected && disassembly == nullptr, "D3DDisassemble must return unavailable and clear its output");
    const auto create_blob = resolve<decltype(&D3DCreateBlob)>(module, "D3DCreateBlob");
    ID3DBlob* blob = poison<ID3DBlob*>();
    result = create_blob(0, &blob);
    check(result == expected && blob == nullptr, "D3DCreateBlob must return unavailable and clear its output");
    const auto reflect = resolve<decltype(&D3DReflect)>(module, "D3DReflect");
    GUID interface_id{};
    void* reflector = poison<void*>();
    result = reflect(nullptr, 0, interface_id, &reflector);
    check(result == expected && reflector == nullptr, "D3DReflect must return unavailable and clear its output");
    const auto read_blob = resolve<decltype(&D3DReadFileToBlob)>(module, "D3DReadFileToBlob");
    ID3DBlob* contents = poison<ID3DBlob*>();
    result = read_blob(nullptr, &contents);
    check(result == expected && contents == nullptr, "D3DReadFileToBlob must return unavailable and clear its output");
    const auto compile2 = resolve<decltype(&D3DCompile2)>(module, "D3DCompile2");
    code = poison<ID3DBlob*>();
    errors = poison<ID3DBlob*>();
    result = compile2(nullptr, 0, nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, 0, nullptr, 0, &code, &errors);
    check(result == expected && code == nullptr && errors == nullptr, "D3DCompile2 must return unavailable and clear its outputs");
    const auto compile_file = resolve<decltype(&D3DCompileFromFile)>(module, "D3DCompileFromFile");
    code = poison<ID3DBlob*>();
    errors = poison<ID3DBlob*>();
    result = compile_file(nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, &code, &errors);
    check(result == expected && code == nullptr && errors == nullptr, "D3DCompileFromFile must return unavailable and clear its outputs");
    const auto preprocess = resolve<decltype(&D3DPreprocess)>(module, "D3DPreprocess");
    ID3DBlob* text = poison<ID3DBlob*>();
    errors = poison<ID3DBlob*>();
    result = preprocess(nullptr, 0, nullptr, nullptr, nullptr, &text, &errors);
    check(result == expected && text == nullptr && errors == nullptr, "D3DPreprocess must return unavailable and clear its outputs");
    const auto region = resolve<decltype(&D3DDisassembleRegion)>(module, "D3DDisassembleRegion");
    SIZE_T finish = kPoison;
    disassembly = poison<ID3DBlob*>();
    result = region(nullptr, 0, 0, nullptr, 0, 0, &finish, &disassembly);
    check(result == expected && disassembly == nullptr, "D3DDisassembleRegion must return unavailable and clear its output");
    check(finish == kPoison, "D3DDisassembleRegion must not touch pFinishByteOffset");

    const auto decompress = resolve<decltype(&D3DDecompressShaders)>(module, "D3DDecompressShaders");
    ID3DBlob* shaders[2];
    std::memset(shaders, 0xCC, sizeof(shaders));
    ID3DBlob* shaders_before[2];
    std::memcpy(shaders_before, shaders, sizeof(shaders));
    UINT total_shaders = 0xCCCCCCCCu;
    result = decompress(nullptr, 0, 2, 0, nullptr, 0, shaders, &total_shaders);
    check(result == expected, "D3DDecompressShaders must return the unavailable HRESULT");
    check(std::memcmp(shaders, shaders_before, sizeof(shaders)) == 0,
        "D3DDecompressShaders must leave the ppShaders array byte-identical");
    check(total_shaders == 0, "D3DDecompressShaders must write pTotalShaders=0");

    const auto trace = resolve<decltype(&D3DGetTraceInstructionOffsets)>(module, "D3DGetTraceInstructionOffsets");
    SIZE_T offsets[3];
    std::memset(offsets, 0xCC, sizeof(offsets));
    SIZE_T offsets_before[3];
    std::memcpy(offsets_before, offsets, sizeof(offsets));
    SIZE_T total_insts = kPoison;
    result = trace(nullptr, 0, 0, 0, 3, offsets, &total_insts);
    check(result == expected, "D3DGetTraceInstructionOffsets must return the unavailable HRESULT");
    check(std::memcmp(offsets, offsets_before, sizeof(offsets)) == 0,
        "D3DGetTraceInstructionOffsets must leave the pOffsets array byte-identical");
    check(total_insts == 0, "D3DGetTraceInstructionOffsets must write pTotalInsts=0");
}
void compiler_no_provider(HMODULE module)
{
    check(GetModuleHandleW(L"D3DCompiler_46_org.dll") == nullptr, "provider must be absent before the no-provider test");
    check_unavailable_exports(module, HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND), false);
    check(GetModuleHandleW(L"D3DCompiler_46_org.dll") == nullptr, "failed provider load must not leave a provider module");
    std::puts("PASS: no provider -> 0x8007007E; single outputs nullptr; arrays byte-identical");
}
void compiler_partial_provider(HMODULE module)
{
    const auto compile = resolve<decltype(&D3DCompile)>(module, "D3DCompile");
    ID3DBlob* code = poison<ID3DBlob*>();
    ID3DBlob* errors = poison<ID3DBlob*>();
    const HRESULT result = compile("f107.hlsl", 9, "f107.hlsl", nullptr, nullptr, "main", "ps_5_0", 0, 0, &code, &errors);
    check(result == E_FAIL, "D3DCompile must forward the partial provider's E_FAIL");
    check(code == nullptr && errors == nullptr, "partial provider must clear D3DCompile outputs");
    check(GetModuleHandleW(L"D3DCompiler_46_org.dll") != nullptr, "partial provider module must be loaded");
    check_unavailable_exports(module, HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND), true);
    std::puts("PASS: partial provider -> present export forwarded (E_FAIL); missing exports 0x8007007F; arrays byte-identical");
}
void compiler_late_provider(HMODULE module)
{
    check(GetModuleHandleW(L"D3DCompiler_46_org.dll") == nullptr, "late-provider test must start without a provider");
    const auto compile = resolve<decltype(&D3DCompile)>(module, "D3DCompile");
    ID3DBlob* code = poison<ID3DBlob*>();
    ID3DBlob* errors = poison<ID3DBlob*>();
    HRESULT result = compile("f107.hlsl", 9, "f107.hlsl", nullptr, nullptr, "main", "ps_5_0", 0, 0, &code, &errors);
    check(result == HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND) && code == nullptr && errors == nullptr,
        "first call must fail with ERROR_MOD_NOT_FOUND");
    wchar_t executable[MAX_PATH]{};
    check(GetModuleFileNameW(nullptr, executable, MAX_PATH) != 0, "locate host directory");
    const auto directory = std::filesystem::path(executable).parent_path();
    std::error_code copy_error;
    std::filesystem::copy_file(directory / L"late-provider.dll", directory / L"D3DCompiler_46_org.dll",
        std::filesystem::copy_options::overwrite_existing, copy_error);
    check(!copy_error, "place the late provider next to the host");
    code = poison<ID3DBlob*>();
    errors = poison<ID3DBlob*>();
    result = compile("f107.hlsl", 9, "f107.hlsl", nullptr, nullptr, "main", "ps_5_0", 0, 0, &code, &errors);
    check(result == HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND) && code == nullptr && errors == nullptr,
        "a provider that appears after the latch must not be used");
    check(GetModuleHandleW(L"D3DCompiler_46_org.dll") == nullptr, "late provider must not be loaded");
    std::puts("PASS: failed initialization is latched; a provider appearing later is not loaded");
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

void compiler_replacement(HMODULE module)
{
    const auto compile = resolve<decltype(&D3DCompile)>(module, "D3DCompile");
    const char original[] = "float4 main():SV_Target{return float4(1,2,3,4);}";
    const char replacement[] = "float4 main():SV_Target{return float4(4,3,2,1);}";
    ComPtr<ID3DBlob> code, errors;
    std::fputs("F153 compiler: compile owned replacement source under ASan\n", stderr); std::fflush(stderr);
    check(SUCCEEDED(compile(original,sizeof(original)-1,"simulation.hlsl",nullptr,nullptr,"main","ps_5_0",0,0,&code,&errors)) && code,
        "F153: compiler proxy replacement succeeds");
    HMODULE provider=GetModuleHandleW(L"D3DCompiler_46_org.dll");
    check(provider != nullptr, "F153: real native provider was actually loaded");
    const auto native=resolve<decltype(&D3DCompile)>(provider,"D3DCompile");
    ComPtr<ID3DBlob> expected, native_errors;
    check(SUCCEEDED(native(replacement,sizeof(replacement)-1,"simulation.hlsl",nullptr,nullptr,"main","ps_5_0",0,0,&expected,&native_errors)) &&
        expected && expected->GetBufferSize()==code->GetBufferSize() &&
        !std::memcmp(expected->GetBufferPointer(),code->GetBufferPointer(),code->GetBufferSize()), "F153: actual replacement output equals native compilation");
    wchar_t executable[MAX_PATH]{};
    check(GetModuleFileNameW(nullptr,executable,MAX_PATH)!=0,"F153: locate owned replacement file");
    const auto directory=std::filesystem::path(executable).parent_path()/L"shaders";
    std::filesystem::path replace_file;
    for(const auto& file:std::filesystem::directory_iterator(directory)) {
        const auto name=file.path().filename().wstring();
        if(name.size()>=12 && name.compare(name.size()-12,12,L"_replace.txt")==0) {
            check(replace_file.empty(),"F153: replacement file is unique"); replace_file=file.path();
        }
    }
    check(!replace_file.empty(),"F153: replacement file exists");
    { std::ofstream output(replace_file,std::ios::binary|std::ios::trunc); output<<"this is not valid hlsl"; output.close(); check(output.good(),"F153: invalid-source fixture"); }
    code.Reset(); errors.Reset();
    check(FAILED(compile(original,sizeof(original)-1,"simulation.hlsl",nullptr,nullptr,"main","ps_5_0",0,0,&code,&errors)) && !code && errors,
        "F153: failed replacement compilation also retires its owned array");
    check(std::filesystem::remove(replace_file),"F153: remove only owned replacement");
    code.Reset(); errors.Reset(); expected.Reset(); native_errors.Reset();
    check(SUCCEEDED(compile(original,sizeof(original)-1,"simulation.hlsl",nullptr,nullptr,"main","ps_5_0",0,0,&code,&errors)) && code &&
        SUCCEEDED(native(original,sizeof(original)-1,"simulation.hlsl",nullptr,nullptr,"main","ps_5_0",0,0,&expected,&native_errors)) && expected &&
        expected->GetBufferSize()==code->GetBufferSize() && !std::memcmp(expected->GetBufferPointer(),code->GetBufferPointer(),code->GetBufferSize()),
        "F153: missing replacement preserves borrowed original source and native output");
    std::puts("PASS: compiler replacement success/failure/borrowed-input ownership");
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
        if (!std::wcscmp(argv[2], L"compiler") || !std::wcscmp(argv[2],L"compiler-export")) compiler(module);
        else if(!std::wcscmp(argv[2],L"compiler-replacement")) compiler_replacement(module);
        else if(!std::wcscmp(argv[2],L"compiler-exports")) compiler_exports(module);
        else if(!std::wcscmp(argv[2],L"compiler-no-provider")) compiler_no_provider(module);
        else if(!std::wcscmp(argv[2],L"compiler-partial-provider")) compiler_partial_provider(module);
        else if(!std::wcscmp(argv[2],L"compiler-late-provider")) compiler_late_provider(module);
        else if (!std::wcscmp(argv[2], L"dxgi")) dxgi(module);
        else throw std::runtime_error("unknown component scenario");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL component: %s (Win32=%lu)\n", error.what(), GetLastError());
        return 1;
    }
}
