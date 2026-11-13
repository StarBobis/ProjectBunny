#include <Windows.h>
#include "version.h"
#include <cstring>
#include <cstdio>

int main()
{
    if (VERSION_MAJOR != 1 || VERSION_MINOR != 3 || VERSION_REVISION != 16 ||
        std::strcmp(VER_FILE_VERSION_STR, "1.3.16") || std::strcmp(VER_PRODUCTNAME_STR, "3Dmigoto")) {
        std::fputs("FAIL: DX12 shared version identity changed with DX11 version generation\n", stderr);
        return 1;
    }
    std::puts("PASS: non-DX11 shared version identity remains independent");
    return 0;
}
