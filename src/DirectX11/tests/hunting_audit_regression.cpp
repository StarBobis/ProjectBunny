// Compile the production implementation once, exposing its private reload entry
// to this regression host without exporting test hooks from the DLL.
#include "../Hunting.cpp"
#include <stdexcept>
#include <filesystem>
#include <fstream>
#include "slot_range_test_support.h"

void test_dx11_reload_shader_filename_regression()
{
    if (!G->mReloadedShaders.empty())
        throw std::runtime_error("F184: reload fixture must not replace live shader metadata");
    wchar_t path[] = L".";
    const wchar_t* valid[] = {L"0000000000000000-ps.txt", L"ffffffffffffffff-vs_replace.txt",
        L"aBcDeF0123456789-cs.txt"};
    for (const auto* name : valid) {
        std::wstring filename(name);
        if (!ReloadShader(path, &filename[0], nullptr, nullptr))
            throw std::runtime_error("F184: valid filenames retain the not-yet-loaded shader success path");
    }
    const wchar_t* invalid[] = {L"", L"1", L"gggggggggggggggg-ps.txt", L"123456789abcdefg-ps.txt",
        L"+23456789abcdef0-ps.txt", L" 23456789abcdef0-ps.txt", L"0x3456789abcdef0-ps.txt",
        L"-23456789abcdef0-ps.txt", L"123456789abcde\uff26f-ps.txt"};
    for (const auto* name : invalid) {
        std::fprintf(stderr, "F184: reject malformed reload filename without an exception\n"); std::fflush(stderr);
        std::wstring filename(name);
        std::string error;
        if (ReloadShader(path, &filename[0], nullptr, &error) || error.empty())
            throw std::runtime_error("F184: reject the complete invalid hash and report the reload error");
        if (!G->mReloadedShaders.empty())
            throw std::runtime_error("F184: invalid names cannot publish reload metadata");
    }
}

void test_dx11_reload_header_file_regression(const std::filesystem::path& directory,ID3DBlob* original)
{
    using namespace dx11_slot_test;
    BatchDevice device;
    const std::string source="float4 main():SV_Target{return float4(0.75,0.5,0.25,1);}";
    const UINT64 hash=fnv_64_buf(original->GetBufferPointer(),original->GetBufferSize());
    wchar_t filename[64]{}; swprintf_s(filename,L"%016llx-ps_replace.txt",hash);
    for(unsigned i=0;i<2;++i) {
        const auto text=i ? std::string("// ")+static_cast<char>(0xff)+"\n"+source : source;
        std::ofstream output(directory/filename,std::ios::binary); output.write(text.data(),text.size()); output.close();
        require_batch(output.good(),"F171: owned exact-size reload source");
        std::wstring folder=directory.wstring(),name=filename,header=L"stale"; FILETIME time{}; ComPtr<ID3DBlob> code; std::string error;
        require_batch(RegenerateShader(folder.data(),name.data(),"ps_5_0",hash,L"ps",original,&time,header,&code,&error) && code &&
            header==(i ? L"" : std::wstring(source.begin(),source.end())),"F171: real reload preserves successful code with bounded/invalid optional headers");
        ComPtr<ID3D11PixelShader> accepted;
        require_batch(SUCCEEDED(device.native->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&accepted)),
            "F171: regenerated shader remains native-valid");
        code.Reset(); const auto previous_header=header;
        require_batch(!RegenerateShader(folder.data(),name.data(),"ps_5_0",hash,L"ps",original,&time,header,&code,&error) && !code && header==previous_header,
            "F171: unchanged timestamp retains the existing no-recompile result and metadata");
    }
}
