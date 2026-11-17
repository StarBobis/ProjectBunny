#ifdef DX11_VERSION_GENERATED
#undef DX11_VERSION_GENERATED
#endif
// Exercise the production provider's archive fallback without colliding with
// the generated provider linked into the same regression executable.
#define MigotoFileVersionString MigotoFallbackFileVersionString
#include "../DX11Version.cpp"
#undef MigotoFileVersionString

#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>

void test_dx11_version_fallback_regression()
{
    if (std::strcmp(MigotoFallbackFileVersionString(), "1.3.16.0") ||
        VERSION_MAJOR != 1 || VERSION_MINOR != 3 || VERSION_REVISION != 16 || VERSION_BUILD != 0 ||
        DX11_VERSION_IS_RELEASE != 0 || VER_FILETYPE != VFT_DLL ||
        std::strcmp(VER_COMPANYNAME_STR, "Built by Unknown @ archive") || std::strcmp(VER_COMMENTS_STR, ""))
        throw std::runtime_error("non-generated DX11 provider and resource fallback changed");
    std::fputs("PASS: production DX11 version provider without generated metadata\n", stderr);
}
