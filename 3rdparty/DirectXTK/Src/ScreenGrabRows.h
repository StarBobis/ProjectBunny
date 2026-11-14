#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace DirectX { namespace Internal {
// Caller validates pitches and destination capacity before mapping. Return the
// original pointer when no packing is needed; otherwise use the reusable buffer.
inline const uint8_t* PackDDSRows(const uint8_t* source, size_t sourceRowPitch,
    uint8_t* packed, size_t rowPitch, size_t rowCount)
{
    if (sourceRowPitch == rowPitch)
        return source;
    for (size_t y = 0; y < rowCount; ++y)
        std::memcpy(packed + y * rowPitch, source + y * sourceRowPitch, rowPitch);
    return packed;
}
} }
