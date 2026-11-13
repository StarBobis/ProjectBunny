#include "PngColorSpace.h"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <memory>

bool PngHasSrgbChunk(const std::wstring& filename) noexcept
{
    std::unique_ptr<FILE, decltype(&std::fclose)> file(_wfopen(filename.c_str(), L"rb"), &std::fclose);
    if (!file || _fseeki64(file.get(), 0, SEEK_END) != 0)
        return false;
    const auto end = _ftelli64(file.get());
    if (end < 8 || _fseeki64(file.get(), 0, SEEK_SET) != 0)
        return false;
    unsigned char signature[8] = {};
    if (std::fread(signature, 1, sizeof(signature), file.get()) != sizeof(signature) ||
        std::memcmp(signature, "\x89PNG\r\n\x1a\n", sizeof(signature)) != 0)
        return false;

    uint64_t position = sizeof(signature);
    const uint64_t file_size = static_cast<uint64_t>(end);
    while (file_size - position >= 12) {
        unsigned char header[8] = {};
        if (std::fread(header, 1, sizeof(header), file.get()) != sizeof(header))
            return false;
        const uint32_t length = (static_cast<uint32_t>(header[0]) << 24) |
            (static_cast<uint32_t>(header[1]) << 16) |
            (static_cast<uint32_t>(header[2]) << 8) | header[3];
        // PNG lengths are limited to 31 bits. Subtraction before addition also
        // rejects truncated data/CRC and avoids length+4 integer overflow.
        if (length > 0x7fffffffu || length > file_size - position - 12)
            return false;
        if (std::memcmp(header + 4, "sRGB", 4) == 0) {
            unsigned char intent = 0;
            return length == 1 && std::fread(&intent, 1, 1, file.get()) == 1 && intent <= 3;
        }
        if (std::memcmp(header + 4, "IDAT", 4) == 0 || std::memcmp(header + 4, "IEND", 4) == 0)
            return false;
        position += static_cast<uint64_t>(length) + 12;
        // position <= file_size <= signed 64-bit ftell result, so conversion
        // and the absolute seek cannot overflow even for a hostile header.
        if (_fseeki64(file.get(), static_cast<__int64>(position), SEEK_SET) != 0)
            return false;
    }
    return false;
}
