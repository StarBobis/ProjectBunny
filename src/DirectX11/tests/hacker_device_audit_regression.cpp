// Substitute this TU only in the source regression executable, allowing tests
// to invoke the unchanged production shader preparation path directly.
#include "../HackerDevice.cpp"
#include "slot_range_test_support.h"
#include <filesystem>
#include <fstream>
#include <array>

extern "C" decltype(&D3DCompile) __imp_D3DCompile;
extern "C" decltype(&GetFileAttributesExW) __imp_GetFileAttributesExW;
void with_dx11_audit_shader_files(void (*scenario)(const std::filesystem::path&));
void with_dx11_audit_hunting_configuration(void (*scenario)());

namespace {
using namespace dx11_slot_test;
const auto native_compile_audit = __imp_D3DCompile;
unsigned diagnostic_compile_calls = 0;
bool diagnostic_compile_failure = false;
HRESULT WINAPI compile_with_audit_warning(LPCVOID source, SIZE_T size, LPCSTR filename, const D3D_SHADER_MACRO* defines,
    ID3DInclude* include, LPCSTR entry, LPCSTR target, UINT flags1, UINT flags2, ID3DBlob** output, ID3DBlob** errors)
{
    ++diagnostic_compile_calls;
    if (diagnostic_compile_failure) {
        const char broken[]="F111 intentionally invalid HLSL";
        return native_compile_audit(broken,sizeof(broken)-1,filename,defines,include,entry,target,flags1,flags2,output,errors);
    }
    const auto hr = native_compile_audit(source,size,filename,defines,include,entry,target,flags1,flags2,output,errors);
    if (SUCCEEDED(hr) && errors) {
        if (*errors) { (*errors)->Release(); *errors = nullptr; }
        const char warning[] = "F111 synthetic compilation warning";
        require_batch(SUCCEEDED(D3DCreateBlob(sizeof(warning),errors)), "F111: actual owned diagnostic blob");
        std::memcpy((*errors)->GetBufferPointer(),warning,sizeof(warning));
    }
    return hr;
}
class DiagnosticCompileScope {
    decltype(&D3DCompile) saved = __imp_D3DCompile;
    static void set(decltype(&D3DCompile) value)
    {
        DWORD protection = 0;
        require_batch(VirtualProtect(&__imp_D3DCompile,sizeof(__imp_D3DCompile),PAGE_READWRITE,&protection)!=FALSE,
            "F111: change only this executable's compiler import slot");
        __imp_D3DCompile=value;
        DWORD ignored=0;
        if (!VirtualProtect(&__imp_D3DCompile,sizeof(__imp_D3DCompile),protection,&ignored)) std::abort();
    }
public:
    explicit DiagnosticCompileScope(bool fail=false) { diagnostic_compile_calls=0; diagnostic_compile_failure=fail; set(compile_with_audit_warning); }
    ~DiagnosticCompileScope() { try { set(saved); } catch (...) { std::abort(); } }
};
unsigned timestamp_failures = 0;
BOOL WINAPI fail_export_attributes(LPCWSTR, GET_FILEEX_INFO_LEVELS, LPVOID)
{
    ++timestamp_failures; SetLastError(ERROR_ACCESS_DENIED); return FALSE;
}
class ExportTimestampFailureScope {
    decltype(&GetFileAttributesExW) saved=__imp_GetFileAttributesExW;
    static void set(decltype(&GetFileAttributesExW) value)
    {
        DWORD protection=0;
        require_batch(VirtualProtect(&__imp_GetFileAttributesExW,sizeof(__imp_GetFileAttributesExW),PAGE_READWRITE,&protection)!=FALSE,"F111: timestamp import scope");
        __imp_GetFileAttributesExW=value; DWORD ignored=0;
        if(!VirtualProtect(&__imp_GetFileAttributesExW,sizeof(__imp_GetFileAttributesExW),protection,&ignored)) std::abort();
    }
public:
    ExportTimestampFailureScope() { timestamp_failures=0; set(fail_export_attributes); }
    ~ExportTimestampFailureScope() { try { set(saved); } catch (...) { std::abort(); } }
};
class DecompileFileFlagsScope {
    FILE* saved_log=LogFile;
    LogVerbosity saved_level=gLogVerbosity;
    int old_hlsl=G->EXPORT_HLSL;
    bool old_fixed=G->EXPORT_FIXED;
    bool old_recompile=G->decompiler_settings.recompileVs, old_position=G->decompiler_settings.fixSvPosition;
public:
    FILE* capture=std::tmpfile();
    DecompileFileFlagsScope()
    {
        require_batch(capture!=nullptr,"F111: actual global log capture");
        LogFile=capture; gLogVerbosity=LogVerbosity::INFO;
        G->EXPORT_HLSL=0; G->EXPORT_FIXED=false; G->decompiler_settings.recompileVs=true; G->decompiler_settings.fixSvPosition=false;
    }
    ~DecompileFileFlagsScope()
    {
        LogFile=saved_log; gLogVerbosity=saved_level; G->EXPORT_HLSL=old_hlsl; G->EXPORT_FIXED=old_fixed;
        G->decompiler_settings.recompileVs=old_recompile; G->decompiler_settings.fixSvPosition=old_position;
        std::fclose(capture);
    }
};

void exercise_decompile_file_diagnostic(const std::filesystem::path& root)
{
    DecompileFileFlagsScope flags; BatchDevice device;
    const char source[]="float4 main(float4 p:POSITION):SV_Position{return p;}";
    Microsoft::WRL::ComPtr<ID3DBlob> original, errors;
    require_batch(SUCCEEDED(native_compile_audit(source,sizeof(source)-1,nullptr,nullptr,nullptr,"main","vs_5_0",0,0,&original,&errors)),
        "F111: real vertex shader input");
    char* replacement=nullptr; SIZE_T replacement_size=0; std::string model; FILETIME time{}; std::wstring header;
    bool result;
    std::fputs("F111: successful real recompilation with diagnostic blob and no requested file export\n",stderr); std::fflush(stderr);
    {
        DiagnosticCompileScope warning;
        result=DecompileAndPossiblyPatchShader(fnv_64_buf(original->GetBufferPointer(),original->GetBufferSize()),L"vs",
            original->GetBufferPointer(),original->GetBufferSize(),replacement,replacement_size,model,time,header,L"vs",model,time,nullptr);
    }
    std::unique_ptr<char[]> owned(replacement);
    if (diagnostic_compile_calls!=1 || !result || !replacement || !replacement_size) {
        std::fprintf(stderr,"F111 diagnostic calls=%u result=%d bytes=%zu\n",diagnostic_compile_calls,result,replacement_size);
        std::fflush(flags.capture); std::fseek(flags.capture,0,SEEK_SET);
        char block[1024]; size_t count;
        while((count=std::fread(block,1,sizeof(block),flags.capture))!=0) std::fwrite(block,1,count,stderr);
    }
    require_batch(diagnostic_compile_calls==1 && result && replacement && replacement_size,
        "F111: diagnostic file absence cannot skip successful patched output processing");
    Microsoft::WRL::ComPtr<ID3D11VertexShader> shader;
    require_batch(SUCCEEDED(device.native->CreateVertexShader(replacement,replacement_size,nullptr,&shader)) && shader,
        "F111: returned recompiled bytecode remains a valid native shader");
    require_batch(time.dwLowDateTime==0 && time.dwHighDateTime==0,"F111: no export does not publish a fictitious file timestamp");
    const auto hash=fnv_64_buf(original->GetBufferPointer(),original->GetBufferSize());
    const auto invoke=[&](bool fail) {
        replacement=nullptr; replacement_size=0; model.clear(); time={};
        DiagnosticCompileScope compiler(fail);
        const bool ok=DecompileAndPossiblyPatchShader(hash,L"vs",original->GetBufferPointer(),original->GetBufferSize(),
            replacement,replacement_size,model,time,header,L"vs",model,time,nullptr);
        owned.reset(replacement);
        require_batch(diagnostic_compile_calls==1,"F111: actual preparation reached the declared compile outcome once");
        if(fail) require_batch(!ok && !replacement && !replacement_size,"F111: failed compile diagnostics cannot publish replacement bytes");
        else {
            require_batch(ok && replacement && replacement_size,"F111: successful compile processing continues independently of diagnostics");
            Microsoft::WRL::ComPtr<ID3D11VertexShader> accepted;
            require_batch(SUCCEEDED(device.native->CreateVertexShader(replacement,replacement_size,nullptr,&accepted)),"F111: every successful result is native-valid");
        }
    };
    invoke(true);
    LogFile=nullptr; invoke(false); LogFile=flags.capture;

    wchar_t name[64]{}; swprintf_s(name,L"%016llx-vs_replace.txt",hash);
    const auto exported=root/L"cache"/name;
    G->EXPORT_HLSL=3;
    invoke(false);
    WIN32_FILE_ATTRIBUTE_DATA actual{};
    require_batch(GetFileAttributesExW(exported.c_str(),GetFileExInfoStandard,&actual) && CompareFileTime(&time,&actual.ftLastWriteTime)==0 &&
        (time.dwLowDateTime || time.dwHighDateTime),"F111: timestamp is the actual completed file's last-write time");
    {
        std::ifstream input(exported,std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(input)),{});
        require_batch(text.find("F111 synthetic compilation warning")!=std::string::npos && text.find("Recompiled ASM")!=std::string::npos,
            "F111: requested diagnostic and recompiled assembly comments remain in the export");
    }
    require_batch(std::filesystem::remove(exported),"F111: remove only this fixture's owned export before another case");
    {
        ExportTimestampFailureScope failure;
        invoke(false);
    }
    require_batch(timestamp_failures==1 && time.dwLowDateTime==0 && time.dwHighDateTime==0,
        "F111: failed timestamp query leaves deterministic zero metadata while retaining successful compiled bytes");
    require_batch(std::filesystem::remove(exported),"F111: remove owned timestamp-failure artifact");
    invoke(true);
    require_batch(GetFileAttributesExW(exported.c_str(),GetFileExInfoStandard,&actual) && CompareFileTime(&time,&actual.ftLastWriteTime)==0,
        "F111: a requested failed-compile diagnostic export also has real metadata");
}
}

static void initialize_decompile_file_fixture()
{
    with_dx11_audit_shader_files(exercise_decompile_file_diagnostic);
}

void test_dx11_decompile_file_diagnostic_regression()
{
    with_dx11_audit_hunting_configuration(initialize_decompile_file_fixture);
}
