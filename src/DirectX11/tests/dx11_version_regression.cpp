#include <Windows.h>
#include <winver.h>
#include "DX11Version.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void check(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}
std::wstring wide(const char* text)
{
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, nullptr, 0);
    check(size > 0, "decode generated UTF8 version string");
    std::wstring result(size, L'\0');
    check(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, result.data(), size) == size,
        "convert complete generated version string");
    result.pop_back();
    return result;
}
void run_version_test()
{
    wchar_t executable[MAX_PATH] = {};
    check(GetModuleFileNameW(nullptr, executable, MAX_PATH) != 0, "locate built DX11 version fixture");
    const auto library = std::filesystem::path(executable).parent_path() / L"d3d11.dll";
    DWORD ignored = 0;
    const DWORD bytes = GetFileVersionInfoSizeW(library.c_str(), &ignored);
    check(bytes != 0, "read actual built DLL version resource size");
    std::vector<unsigned char> info(bytes);
    check(GetFileVersionInfoW(library.c_str(), 0, bytes, info.data()) != FALSE, "load actual built DLL version resource");
    void* raw = nullptr;
    UINT size = 0;
    check(VerQueryValueW(info.data(), L"\\", &raw, &size) && size >= sizeof(VS_FIXEDFILEINFO),
        "query actual fixed version structure");
    const auto* fixed = static_cast<const VS_FIXEDFILEINFO*>(raw);
    check(HIWORD(fixed->dwFileVersionMS) == VERSION_MAJOR && LOWORD(fixed->dwFileVersionMS) == VERSION_MINOR &&
        HIWORD(fixed->dwFileVersionLS) == VERSION_REVISION && LOWORD(fixed->dwFileVersionLS) == VERSION_BUILD &&
        fixed->dwProductVersionMS == fixed->dwFileVersionMS && fixed->dwProductVersionLS == fixed->dwFileVersionLS,
        "DLL fixed file/product versions match all four generated components");
    check(fixed->dwFileType == VFT_DLL &&
        bool(fixed->dwFileFlags & VS_FF_PRIVATEBUILD) == !bool(DX11_VERSION_IS_RELEASE),
        "DLL type and private/release identity match generated provenance");
    for (const auto& expected : std::vector<std::pair<std::wstring, std::wstring>>{
        {L"FileVersion", wide(VER_FILE_VERSION_STR)},
        {L"ProductVersion", wide(VER_PRODUCT_VERSION_STR)},
        {L"ProductName", wide(VER_PRODUCTNAME_STR)},
        {L"CompanyName", wide(VER_COMPANYNAME_STR)},
        {L"Comments", wide(VER_COMMENTS_STR)},
        {L"FileDescription", L"ProjectBunny DX11 (XXMI/3Dmigoto) - d3d11.dll"}}) {
        const std::wstring query = L"\\StringFileInfo\\040904b0\\" + expected.first;
        check(VerQueryValueW(info.data(), query.c_str(), &raw, &size) && size > 0 &&
            std::wstring(static_cast<const wchar_t*>(raw)) == expected.second,
            "actual DLL metadata string matches generated header and product identity");
    }
#if !DX11_VERSION_IS_RELEASE
    check(VerQueryValueW(info.data(), L"\\StringFileInfo\\040904b0\\PrivateBuild", &raw, &size) &&
        std::wstring(static_cast<const wchar_t*>(raw)) == wide(VER_COMPANYNAME_STR),
        "private build flag has corresponding private provenance string");
#endif
}
}
void test_dx11_version_regression()
{
    try {
        run_version_test();
        std::fputs("PASS: actual built DX11 DLL four-part version and resource provenance\n", stderr);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL DX11 DLL version: %s\n", error.what());
        std::exit(EXIT_FAILURE);
    }
}
