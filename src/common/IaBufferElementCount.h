#pragma once

#include <cstdint>
#include <dxgiformat.h>

namespace migoto {

constexpr std::uint32_t BufferElementCount(std::uint32_t bytes, std::uint32_t offset, std::uint32_t stride)
{
    return stride && offset <= bytes ? (bytes - offset) / stride : 0;
}

constexpr std::uint32_t IndexCount(std::uint32_t bytes, std::uint32_t offset, DXGI_FORMAT format)
{
    switch (format) {
    case DXGI_FORMAT_R16_UINT: return BufferElementCount(bytes, offset, 2);
    case DXGI_FORMAT_R32_UINT: return BufferElementCount(bytes, offset, 4);
    default: return 0;
    }
}

} // namespace migoto
