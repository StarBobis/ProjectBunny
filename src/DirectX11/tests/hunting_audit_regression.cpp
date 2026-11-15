// Compile the production implementation once, exposing its private reload entry
// to this regression host without exporting test hooks from the DLL.
#include "../Hunting.cpp"
#include <stdexcept>

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
