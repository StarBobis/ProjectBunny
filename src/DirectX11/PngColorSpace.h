#pragma once

#include <string>

// A bounded chunk-header scan, not a PNG decoder/CRC validator. Only a complete
// sRGB chunk before the first IDAT is a positive match; iCCP is not inferred.
bool PngHasSrgbChunk(const std::wstring& filename) noexcept;
