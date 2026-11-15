// Substitute this TU only in the source regression executable, allowing tests
// to invoke the unchanged production shader preparation path directly.
#include "../HackerDevice.cpp"
#include "slot_range_test_support.h"
#include <filesystem>
#include <fstream>
#include <array>
#include <limits>

extern "C" decltype(&D3DCompile) __imp_D3DCompile;
extern "C" decltype(&GetFileAttributesExW) __imp_GetFileAttributesExW;
extern "C" decltype(&ReadFile) __imp_ReadFile;
extern "C" decltype(&GetFileTime) __imp_GetFileTime;
void with_dx11_audit_shader_files(void (*scenario)(const std::filesystem::path&));
void with_dx11_audit_hunting_configuration(void (*scenario)());
void test_dx11_reload_header_file_regression(const std::filesystem::path&,ID3DBlob*);

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

namespace {
enum class HeaderInputFault {None,ReadFailure,ShortRead,TimestampFailure};
HeaderInputFault header_input_fault=HeaderInputFault::None;
unsigned header_read_calls=0,header_time_calls=0;
const auto native_header_read=__imp_ReadFile;
const auto native_header_time=__imp_GetFileTime;
BOOL WINAPI audit_header_read(HANDLE file,LPVOID data,DWORD size,LPDWORD read,LPOVERLAPPED overlapped)
{
    ++header_read_calls;
    if(header_input_fault==HeaderInputFault::ReadFailure) {*read=0; SetLastError(ERROR_READ_FAULT); return FALSE;}
    if(header_input_fault==HeaderInputFault::ShortRead && size) --size;
    return native_header_read(file,data,size,read,overlapped);
}
BOOL WINAPI audit_header_time(HANDLE file,LPFILETIME created,LPFILETIME access,LPFILETIME written)
{
    ++header_time_calls;
    if(header_input_fault==HeaderInputFault::TimestampFailure) {SetLastError(ERROR_ACCESS_DENIED); return FALSE;}
    return native_header_time(file,created,access,written);
}
template<class Function> class HeaderInputImportScope {
    Function* slot; Function saved;
    void set(Function value)
    {
        DWORD previous=0;
        require_batch(VirtualProtect(slot,sizeof(*slot),PAGE_READWRITE,&previous)!=FALSE,"F171: this executable's existing Win32 file import");
        *slot=value; DWORD ignored=0;
        if(!VirtualProtect(slot,sizeof(*slot),previous,&ignored)) std::abort();
    }
public:
    HeaderInputImportScope(Function& target,Function value):slot(&target),saved(target) {set(value);}
    ~HeaderInputImportScope() {try {set(saved);} catch(...) {std::abort();}}
};
class HeaderInputFailureScope {
    HeaderInputFault saved=header_input_fault;
    HeaderInputImportScope<decltype(&ReadFile)> read{__imp_ReadFile,audit_header_read};
    HeaderInputImportScope<decltype(&GetFileTime)> time{__imp_GetFileTime,audit_header_time};
public:
    explicit HeaderInputFailureScope(HeaderInputFault fault) {header_input_fault=fault; header_read_calls=header_time_calls=0;}
    ~HeaderInputFailureScope() {header_input_fault=saved;}
};

class GuardedShaderHeaderSpan {
    void* allocation=nullptr;
public:
    const char* data=nullptr;
    explicit GuardedShaderHeaderSpan(const std::string& text)
    {
        SYSTEM_INFO info{}; GetSystemInfo(&info);
        require_batch(text.size()<=info.dwPageSize,"F171: guarded header fits the input page");
        allocation=VirtualAlloc(nullptr,2*info.dwPageSize,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
        require_batch(allocation!=nullptr,"F171: allocate guarded source span");
        auto* end=static_cast<char*>(allocation)+info.dwPageSize; DWORD previous=0;
        if(!VirtualProtect(end,info.dwPageSize,PAGE_NOACCESS,&previous)) {
            VirtualFree(allocation,0,MEM_RELEASE); allocation=nullptr; throw std::runtime_error("F171: inaccessible end-of-span page");
        }
        data=end-text.size();
        if(!text.empty()) std::memcpy(const_cast<char*>(data),text.data(),text.size());
    }
    ~GuardedShaderHeaderSpan() {if(allocation) VirtualFree(allocation,0,MEM_RELEASE);}
};

void exercise_shader_header_files(const std::filesystem::path& root)
{
    using Microsoft::WRL::ComPtr;
    const std::pair<std::string,std::wstring> headers[] = {{"",L""},{"one line",L"one line"},{"first\nsecond",L"first"},
        {"\nnext",L""},{"first\r\nnext",L"first\r"},{"\xe4\xb8\xad\xe6\x96\x87",L"\u4e2d\u6587"},
        {"\xf0\x9f\x98\x80",L"\U0001f600"},{"\xef\xbb\xbfheader",L"\ufeffheader"},
        {std::string(1,static_cast<char>(0xff)),L""},{"prefix\xe4\xb8",L""},{"\xc0\xaf",L""},
        {std::string("good\n")+static_cast<char>(0xff),L"good"}};
    for(const auto& entry:headers) {
        GuardedShaderHeaderSpan span(entry.first);
        require_batch(ReadShaderHeaderLine(span.data,entry.first.size())==entry.second,"F171: exact guard-page boundary and complete UTF-8 header decoding");
    }
    require_batch(ReadShaderHeaderLine(nullptr,0).empty(),"F171: null empty spans require no read");
    BatchDevice device;
    const char original_source[]="float4 main():SV_Target{return float4(1,0,0,1);}";
    const std::string replacement="float4 main():SV_Target{return float4(0.25,0.5,0.75,1);}";
    ComPtr<ID3DBlob> original,errors;
    require_batch(SUCCEEDED(native_compile_audit(original_source,sizeof(original_source)-1,nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&original,&errors)),
        "F171: actual original shader for replacement header paths");
    const auto hash=fnv_64_buf(original->GetBufferPointer(),original->GetBufferSize());
    wchar_t filename[64]{}; swprintf_s(filename,L"%016llx-ps_replace.txt",hash);
    const auto path=root/L"fixes"/filename;
    const auto write=[&](const std::filesystem::path& target,const std::string& text) {
        std::ofstream output(target,std::ios::binary|std::ios::trunc);
        output.write(text.data(),text.size()); output.close();
        require_batch(output.good(),"F171: complete bounded source file fixture");
    };
    const auto invoke_hlsl=[&](const std::string& source,const std::wstring& expected_header,bool success) {
        write(path,source);
        char* result=nullptr; SIZE_T size=0; std::string model; FILETIME time{}; std::wstring header=L"stale";
        const bool loaded=ReplaceHLSLShader(hash,L"ps",original->GetBufferPointer(),original->GetBufferSize(),nullptr,result,size,model,time,header);
        std::unique_ptr<char[]> owned(result);
        require_batch(loaded==success && header==expected_header,"F171: HLSL header respects actual source bounds and encoding failure policy");
        if(success) {
            ComPtr<ID3D11PixelShader> accepted;
            require_batch(result && size && SUCCEEDED(device.native->CreatePixelShader(result,size,nullptr,&accepted)),
                "F171: metadata handling preserves successful replacement shader bytes");
        } else require_batch(!result,"F171: invalid HLSL still follows the original compile failure path");
    };
    std::fputs("F171: valid one-line HLSL has neither newline nor an allocated NUL sentinel\n",stderr); std::fflush(stderr);
    invoke_hlsl(replacement,std::wstring(replacement.begin(),replacement.end()),true);
    invoke_hlsl("// \xe4\xb8\xad\xe6\x96\x87\r\n"+replacement,L"// \u4e2d\u6587\r",true);
    invoke_hlsl(std::string("// ")+static_cast<char>(0xff)+"\n"+replacement,L"",true);
    invoke_hlsl("",L"",false);
    write(path,replacement);
    for(const auto fault:{HeaderInputFault::ReadFailure,HeaderInputFault::ShortRead,HeaderInputFault::TimestampFailure}) {
        char* result=nullptr; SIZE_T size=0; std::string model; FILETIME time{}; std::wstring header;
        bool loaded=false;
        {
            HeaderInputFailureScope input(fault); DiagnosticCompileScope compiler;
            loaded=ReplaceHLSLShader(hash,L"ps",original->GetBufferPointer(),original->GetBufferSize(),nullptr,result,size,model,time,header);
            require_batch(header_read_calls==1,"F171: fault injection reached the actual Win32 read call");
            if(fault!=HeaderInputFault::TimestampFailure)
                require_batch(!loaded && !result && diagnostic_compile_calls==0,"F171: failed or partial source is rejected before header decoding and compilation");
            else require_batch(loaded && result && header_time_calls==1 && diagnostic_compile_calls==1 &&
                !time.dwLowDateTime && !time.dwHighDateTime && header==std::wstring(replacement.begin(),replacement.end()),
                "F171: unavailable timestamp remains zero and preserves successfully read/compiled source");
        }
        std::unique_ptr<char[]> owned_result(result);
    }
    require_batch(std::filesystem::remove(path),"F171: retire only this fixture's HLSL input before the ASM path");
    ComPtr<ID3DBlob> assembly;
    require_batch(SUCCEEDED(D3DDisassemble(original->GetBufferPointer(),original->GetBufferSize(),0,nullptr,&assembly)),"F171: actual native assembly fixture");
    std::string text(static_cast<const char*>(assembly->GetBufferPointer()));
    text=std::string("// ")+static_cast<char>(0xff)+"\n"+text;
    wchar_t asm_name[64]{}; swprintf_s(asm_name,L"%016llx-ps.txt",hash);
    write(root/L"fixes"/asm_name,text);
    char* assembled=nullptr; SIZE_T assembled_size=0; std::string model; FILETIME time{}; std::wstring header=L"stale";
    const bool loaded=ReplaceASMShader(hash,L"ps",original->GetBufferPointer(),original->GetBufferSize(),assembled,assembled_size,model,time,header);
    std::unique_ptr<char[]> owned(assembled); ComPtr<ID3D11PixelShader> accepted;
    require_batch(loaded && header.empty() && assembled && SUCCEEDED(device.native->CreatePixelShader(assembled,assembled_size,nullptr,&accepted)),
        "F171: an invalid optional ASM comment header cannot discard a successful assembly");
    for(const auto fault:{HeaderInputFault::ReadFailure,HeaderInputFault::ShortRead,HeaderInputFault::TimestampFailure}) {
        char* result=nullptr; SIZE_T size=0; std::string result_model; FILETIME stamp{}; std::wstring result_header;
        bool succeeded=false;
        {
            HeaderInputFailureScope input(fault);
            succeeded=ReplaceASMShader(hash,L"ps",original->GetBufferPointer(),original->GetBufferSize(),result,size,result_model,stamp,result_header);
            require_batch(header_read_calls==1,"F171: actual ASM source read is inside the fault boundary");
            if(fault!=HeaderInputFault::TimestampFailure) require_batch(!succeeded && !result,"F171: incomplete ASM input is rejected before parsing");
            else require_batch(succeeded && result && header_time_calls==1 && !stamp.dwLowDateTime && !stamp.dwHighDateTime,
                "F171: successful ASM survives optional timestamp failure");
        }
        std::unique_ptr<char[]> owned_result(result);
    }
    require_batch(std::filesystem::remove(root/L"fixes"/asm_name),"F171: retire owned ASM input before exercising HLSL reload");
    test_dx11_reload_header_file_regression(root/L"fixes",original.Get());
}
void initialize_shader_header_files() {with_dx11_audit_shader_files(exercise_shader_header_files);}
}

void test_dx11_shader_header_file_regression()
{
    with_dx11_audit_hunting_configuration(initialize_shader_header_files);
}

namespace {
class ResourceOverrideAuditScope {
    decltype(G->mTextureOverrideMap) overrides=std::move(G->mTextureOverrideMap);
    decltype(G->mFuzzyTextureOverrides) fuzzy=std::move(G->mFuzzyTextureOverrides);
    int hunting=G->hunting;
public:
    ResourceOverrideAuditScope() {InvalidateTextureOverrideCandidates(); G->hunting=0;}
    ~ResourceOverrideAuditScope()
    {
        InvalidateTextureOverrideCandidates(); G->mTextureOverrideMap.clear(); G->mFuzzyTextureOverrides.clear();
        G->mTextureOverrideMap=std::move(overrides); G->mFuzzyTextureOverrides=std::move(fuzzy); G->hunting=hunting;
    }
    void clear() {InvalidateTextureOverrideCandidates(); G->mTextureOverrideMap.clear();}
};
class ResourceOverrideLog {
    FILE* previous=LogFile;
    LogVerbosity level=gLogVerbosity;
    bool debug=gLogDebug;
    FILE* output=std::tmpfile();
public:
    explicit ResourceOverrideLog(LogVerbosity requested=LogVerbosity::INFO)
    {
        require_batch(output!=nullptr,"F175: actual diagnostic capture");
        LogFile=output; gLogVerbosity=requested; gLogDebug=false;
    }
    ~ResourceOverrideLog() {LogFile=previous; gLogVerbosity=level; gLogDebug=debug; std::fclose(output);}
    std::string text()
    {
        require_batch(!std::fflush(output) && !std::fseek(output,0,SEEK_SET),"F175: rewind runtime diagnostics");
        std::string result; char data[1024]; size_t count;
        while((count=std::fread(data,1,sizeof(data),output))!=0) result.append(data,count);
        return result;
    }
};
bool resource_override_log_contains(const std::string& bytes,const std::string& token)
{
    if(bytes.find(token)!=std::string::npos) return true;
    std::string utf16;
    for(const char character:token) {utf16.push_back(character); utf16.push_back('\0');}
    return bytes.find(utf16)!=std::string::npos;
}
}

void test_dx11_resource_override_validation_regression()
{
    ResourceOverrideAuditScope configuration; BatchDevice device;
    D3D11_TEXTURE2D_DESC original{};
    original.Width=4; original.Height=3; original.ArraySize=original.MipLevels=1;
    original.Format=DXGI_FORMAT_R8G8B8A8_UNORM; original.SampleDesc.Count=1; original.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    const uint32_t hash=CalcTexture2DDescHash(0,&original);
    auto set_override=[&]() -> TextureOverride& {
        configuration.clear(); auto& entries=G->mTextureOverrideMap[hash]; entries.resize(1);
        entries.front().ini_section=L"TextureOverrideAuditF175"; return entries.front();
    };
    for(const float invalid:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-1.0f,1073741824.0f,(std::numeric_limits<float>::max)()}) {
        auto& entry=set_override(); entry.width_multiply=invalid;
        ResourceOverrideLog diagnostic; ComPtr<ID3D11Texture2D> result;
        const auto hr=device.device->CreateTexture2D(&original,nullptr,&result);
        const auto text=diagnostic.text();
        std::fputs("F175: invalid floating-point dimensions must fail with section/field diagnostics\n",stderr); std::fflush(stderr);
        if(hr!=E_INVALIDARG || result || !resource_override_log_contains(text,"Invalid resource override") || !resource_override_log_contains(text,"width_multiply"))
            std::fprintf(stderr,"F175 observed HRESULT=%08lx, result=%p, diagnostics=[%s]\n",static_cast<unsigned long>(hr),result.Get(),text.c_str());
        require_batch(hr==E_INVALIDARG && !result && resource_override_log_contains(text,"Invalid resource override") &&
            resource_override_log_contains(text,"TextureOverrideAuditF175") && resource_override_log_contains(text,"width_multiply"),
            "F175: reject an unsafe float-to-UINT conversion explicitly, without restoring the original descriptor");
    }
    for(const unsigned field:{0u,1u,2u}) {
        auto& entry=set_override();
        if(field==0) entry.width=-2; else if(field==1) entry.height=-2; else entry.format=-2;
        ResourceOverrideLog diagnostic; ComPtr<ID3D11Texture2D> result;
        require_batch(device.device->CreateTexture2D(&original,nullptr,&result)==E_INVALIDARG && !result &&
            resource_override_log_contains(diagnostic.text(),"Invalid resource override"),"F175: negative explicit values do not wrap into unsigned descriptors");
    }
    {
        auto& entry=set_override(); entry.width_multiply=-1;
        ResourceOverrideLog diagnostic(LogVerbosity::WARNING); ComPtr<ID3D11Texture2D> result;
        require_batch(device.device->CreateTexture2D(&original,nullptr,&result)==E_INVALIDARG && !result &&
            resource_override_log_contains(diagnostic.text(),"width_multiply"),"F175: numeric failures remain visible in warning-only logging");
    }
    for(const bool bad_format:{false,true}) {
        auto& entry=set_override();
        D3D11_TEXTURE2D_DESC invalid=original;
        if(bad_format) {entry.format=999; invalid.Format=static_cast<DXGI_FORMAT>(999);}
        else {entry.width_multiply=0; invalid.Width=0;}
        ComPtr<ID3D11Texture2D> native,result; ResourceOverrideLog diagnostic;
        const auto native_hr=device.native->CreateTexture2D(&invalid,nullptr,&native);
        const auto hr=device.device->CreateTexture2D(&original,nullptr,&result);
        const auto text=diagnostic.text();
        require_batch(FAILED(native_hr) && hr==native_hr && !result && text.find("Resource creation failed with matching TextureOverride")!=std::string::npos,
            "F175: native descriptor rejection is preserved and reported at INFO level, never retried with the original");
    }
    {
        auto& entry=set_override(); entry.width_multiply=1.5f; entry.height_multiply=2.5f;
        ComPtr<ID3D11Texture2D> result;
        require_batch(SUCCEEDED(device.device->CreateTexture2D(&original,nullptr,&result)),"F175: valid dimension multipliers still create resources");
        D3D11_TEXTURE2D_DESC actual{}; result->GetDesc(&actual);
        require_batch(actual.Width==6 && actual.Height==7 && original.Width==4 && original.Height==3,
            "F175: preserve valid truncation and never mutate the caller's descriptor");
    }
    {
        configuration.clear(); auto& entries=G->mTextureOverrideMap[hash]; entries.resize(2);
        entries[0].ini_section=L"TextureOverrideAuditF175Low"; entries[0].width=0; entries[0].iterations={0,1};
        entries[1].ini_section=L"TextureOverrideAuditF175High"; entries[1].width=8; entries[1].iterations={0,1};
        ComPtr<ID3D11Texture2D> result;
        require_batch(SUCCEEDED(device.device->CreateTexture2D(&original,nullptr,&result)),"F175: a later absolute width preserves normal priority repair of a defined zero value");
        D3D11_TEXTURE2D_DESC actual{}; result->GetDesc(&actual);
        require_batch(actual.Width==8 && entries[0].iterations[0]==1 && entries[1].iterations[0]==1,
            "F175: normal override order and per-entry iteration consumption remain unchanged");
    }
    {
        D3D11_TEXTURE2D_DESC desc=original; desc.Width=16777219;
        TextureOverride entry; entry.width_multiply=1.25f;
        override_resource_desc_common_2d_3d(&desc,&entry);
        require_batch(desc.Width==20971524,"F175: preserve the existing float product rounding before unsigned truncation");
        desc=original; entry.width_multiply=1073741760.0f;
        require_batch(override_resource_desc_common_2d_3d(&desc,&entry) && desc.Width==4294967040u,
            "F175: the last representable float below 2^32 remains a defined conversion");
    }
    {
        configuration.clear(); auto& entries=G->mTextureOverrideMap[hash]; entries.resize(2);
        entries[0].ini_section=L"TextureOverrideAuditF175BadLow"; entries[0].width_multiply=std::numeric_limits<float>::quiet_NaN(); entries[0].iterations={0,1};
        entries[1].ini_section=L"TextureOverrideAuditF175GoodHigh"; entries[1].width=8; entries[1].iterations={0,1};
        ComPtr<ID3D11Texture2D> result; ResourceOverrideLog diagnostic;
        require_batch(device.device->CreateTexture2D(&original,nullptr,&result)==E_INVALIDARG && !result &&
            entries[0].iterations[0]==1 && entries[1].iterations[0]==1,"F175: unsafe numeric input fails without skipping later iteration bookkeeping");
        require_batch(SUCCEEDED(device.device->CreateTexture2D(&original,nullptr,&result)),"F175: an inactive invalid iteration cannot poison later creations");
        D3D11_TEXTURE2D_DESC actual{}; result->GetDesc(&actual);
        require_batch(actual.Width==original.Width && entries[0].iterations[0]==2 && entries[1].iterations[0]==2,
            "F175: subsequent iterations retain the original resource description");
    }
    D3D11_TEXTURE3D_DESC volume{}; volume.Width=volume.Height=volume.Depth=4; volume.MipLevels=1;
    volume.Format=original.Format; volume.BindFlags=original.BindFlags;
    configuration.clear(); auto& entries=G->mTextureOverrideMap[CalcTexture3DDescHash(0,&volume)]; entries.resize(1);
    entries[0].ini_section=L"TextureOverrideAuditF175Volume"; entries[0].height_multiply=-1;
    ComPtr<ID3D11Texture3D> rejected;
    {ResourceOverrideLog diagnostic;
        require_batch(device.device->CreateTexture3D(&volume,nullptr,&rejected)==E_INVALIDARG && !rejected &&
            resource_override_log_contains(diagnostic.text(),"height_multiply"),"F175: 3D uses the same safe conversion failure boundary");}
    configuration.clear();
    require_batch(SUCCEEDED(device.device->CreateTexture3D(&volume,nullptr,&rejected)),"F175: removing the bad override recovers normal creation");
}
#include "texture_initial_debug_audit.inl"
