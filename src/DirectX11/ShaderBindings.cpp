#include "ShaderBindings.h"
#include "d3d11TokenizedProgramFormat.hpp"
#include <cstring>
#include <vector>
#include <algorithm>
#include <new>
#include <utility>

static D3D_SRV_DIMENSION dxbc_dimension_to_srv(D3D10_SB_RESOURCE_DIMENSION d)
{
	switch (d)
	{
	case D3D10_SB_RESOURCE_DIMENSION_BUFFER:            return D3D_SRV_DIMENSION_BUFFER;
	case D3D10_SB_RESOURCE_DIMENSION_TEXTURE1D:         return D3D_SRV_DIMENSION_TEXTURE1D;
	case D3D10_SB_RESOURCE_DIMENSION_TEXTURE1DARRAY:    return D3D_SRV_DIMENSION_TEXTURE1DARRAY;
	case D3D10_SB_RESOURCE_DIMENSION_TEXTURE2D:         return D3D_SRV_DIMENSION_TEXTURE2D;
	case D3D10_SB_RESOURCE_DIMENSION_TEXTURE2DARRAY:    return D3D_SRV_DIMENSION_TEXTURE2DARRAY;
	case D3D10_SB_RESOURCE_DIMENSION_TEXTURE2DMS:       return D3D_SRV_DIMENSION_TEXTURE2DMS;
	case D3D10_SB_RESOURCE_DIMENSION_TEXTURE2DMSARRAY:  return D3D_SRV_DIMENSION_TEXTURE2DMSARRAY;
	case D3D10_SB_RESOURCE_DIMENSION_TEXTURE3D:         return D3D_SRV_DIMENSION_TEXTURE3D;
	case D3D10_SB_RESOURCE_DIMENSION_TEXTURECUBE:       return D3D_SRV_DIMENSION_TEXTURECUBE;
	case D3D10_SB_RESOURCE_DIMENSION_TEXTURECUBEARRAY:  return D3D_SRV_DIMENSION_TEXTURECUBEARRAY;
	case D3D11_SB_RESOURCE_DIMENSION_RAW_BUFFER:        return D3D_SRV_DIMENSION_BUFFEREX;
	case D3D11_SB_RESOURCE_DIMENSION_STRUCTURED_BUFFER: return D3D_SRV_DIMENSION_BUFFER;
	default:                                            return D3D_SRV_DIMENSION_UNKNOWN;
	}
}


namespace {
constexpr uint32_t fourcc(char a, char b, char c, char d)
{
    return uint32_t(uint8_t(a)) | (uint32_t(uint8_t(b)) << 8) |
        (uint32_t(uint8_t(c)) << 16) | (uint32_t(uint8_t(d)) << 24);
}

uint32_t read_u32(const uint8_t* bytes)
{
    uint32_t value;
    std::memcpy(&value, bytes, sizeof(value));
    return value;
}

class DxbcWords {
    const uint8_t* bytes;
    size_t count;
public:
    DxbcWords(const uint8_t* data, size_t words) : bytes(data), count(words) {}
    bool Read(size_t index, uint32_t& value) const
    {
        if (index >= count) return false;
        value = read_u32(bytes + index * sizeof(uint32_t));
        return true;
    }
};

bool parse_program(const uint8_t* bytes, size_t size, ShaderBindings& bindings)
{
    if (size < 8 || size % 4) return false;
    const uint32_t version = read_u32(bytes);
    const unsigned major = DECODE_D3D10_SB_TOKENIZED_PROGRAM_MAJOR_VERSION(version);
    const unsigned minor = DECODE_D3D10_SB_TOKENIZED_PROGRAM_MINOR_VERSION(version);
    if (!((major == 4 && minor <= 1) || (major == 5 && minor == 0))) return false;
    const uint32_t word_count = read_u32(bytes + 4);
    if (word_count < 2 || word_count > size / 4) return false;
    size_t cursor = 2;
    DxbcWords program(bytes, word_count);
    while (cursor < word_count) {
        uint32_t token;
        if (!program.Read(cursor, token)) return false;
        const auto opcode = DECODE_D3D10_SB_OPCODE_TYPE(token);
        uint32_t length = DECODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(token);
        if (opcode == D3D10_SB_OPCODE_CUSTOMDATA) {
            if (!program.Read(cursor + 1, length) || length < 2 || length > word_count - cursor) return false;
            cursor += length;
            continue;
        }
        if (!length || length > word_count - cursor) return false;
        DxbcWords instruction(bytes + cursor * 4, length);
        const bool cb = opcode == D3D10_SB_OPCODE_DCL_CONSTANT_BUFFER;
        const bool typed = opcode == D3D10_SB_OPCODE_DCL_RESOURCE;
        const bool raw = opcode == D3D11_SB_OPCODE_DCL_RESOURCE_RAW;
        const bool structured = opcode == D3D11_SB_OPCODE_DCL_RESOURCE_STRUCTURED;
        if (cb || typed || raw || structured) {
            size_t operand_pos = 1;
            uint32_t extension = token;
            while (extension & 0x80000000u) {
                if (!instruction.Read(operand_pos++, extension)) return false;
            }
            uint32_t operand;
            if (!instruction.Read(operand_pos++, operand)) return false;
            extension = operand;
            while (extension & 0x80000000u) {
                if (!instruction.Read(operand_pos++, extension)) return false;
            }
            const auto operand_type = DECODE_D3D10_SB_OPERAND_TYPE(operand);
            const auto dimensions = DECODE_D3D10_SB_OPERAND_INDEX_DIMENSION(operand);
            if (operand_type != (cb ? D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER : D3D10_SB_OPERAND_TYPE_RESOURCE) ||
                dimensions != (cb ? D3D10_SB_OPERAND_INDEX_2D : D3D10_SB_OPERAND_INDEX_1D) ||
                DECODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(operand, 0) != D3D10_SB_OPERAND_INDEX_IMMEDIATE32 ||
                (cb && DECODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(operand, 1) != D3D10_SB_OPERAND_INDEX_IMMEDIATE32))
                return false; // SM5.1/ranged and relative declaration operands are outside this API.
            uint32_t slot, value = 0;
            if (!instruction.Read(operand_pos++, slot)) return false;
            if (cb || structured || typed) {
                if (!instruction.Read(operand_pos++, value)) return false;
            }
            if (cb) {
                if (slot >= bindings.constant_buffers.size()) return false;
                auto& entry = bindings.constant_buffers[slot];
                entry.size = value; // Number of 16-byte constants, as in DXBC.
                entry.type = DECODE_D3D10_SB_CONSTANT_BUFFER_ACCESS_PATTERN(token) == D3D10_SB_CONSTANT_BUFFER_DYNAMIC_INDEXED
                    ? ShaderConstantBufferType::DYNAMIC_INDEXED : ShaderConstantBufferType::IMMEDIATE_INDEXED;
            } else {
                if (slot >= bindings.resources.size()) return false;
                auto& entry = bindings.resources[slot];
                entry.type = typed ? ShaderResourceType::TYPED : raw ? ShaderResourceType::RAW : ShaderResourceType::STRUCTURED;
                entry.dimension = typed ? dxbc_dimension_to_srv(DECODE_D3D10_SB_RESOURCE_DIMENSION(token)) :
                    raw ? D3D_SRV_DIMENSION_BUFFEREX : D3D_SRV_DIMENSION_BUFFER;
                if (entry.dimension == D3D_SRV_DIMENSION_UNKNOWN) return false;
                entry.stride = structured ? value : 0;
            }
        }
        cursor += length;
    }
    return cursor == word_count;
}
} // namespace

bool get_shader_bindings_from_bytecode(const void* data, size_t size, ShaderBindings* out)
{
    if (!out) return false;
    *out = {};
    if (!data || size < 32) return false;
    const auto* bytes = static_cast<const uint8_t*>(data);
    if (read_u32(bytes) != fourcc('D', 'X', 'B', 'C')) return false;
    const size_t container_size = read_u32(bytes + 24);
    if (container_size < 32 || container_size > size) return false;
    const size_t chunks = read_u32(bytes + 28);
    if (chunks > (container_size - 32) / 4) return false;
    const size_t table_end = 32 + chunks * 4;
    std::vector<std::pair<size_t, size_t>> spans;
    if (chunks > spans.max_size()) return false;
    try {
        spans.reserve(chunks);
        for (size_t i = 0; i < chunks; ++i) {
            const size_t offset = read_u32(bytes + 32 + i * 4);
            if (offset < table_end || offset > container_size || container_size - offset < 8) return false;
            const size_t payload = read_u32(bytes + offset + 4);
            if (payload > container_size - offset - 8) return false;
            spans.emplace_back(offset, offset + 8 + payload);
        }
    } catch (const std::bad_alloc&) { return false; }
    std::sort(spans.begin(), spans.end());
    for (size_t i = 1; i < spans.size(); ++i)
        if (spans[i].first < spans[i - 1].second) return false;
    ShaderBindings parsed;
    bool found_program = false;
    for (size_t i = 0; i < chunks; ++i) {
        const size_t offset = read_u32(bytes + 32 + i * 4);
        if (offset < table_end || offset > container_size || container_size - offset < 8) return false;
        const size_t chunk_size = read_u32(bytes + offset + 4);
        if (chunk_size > container_size - offset - 8) return false;
        const uint32_t tag = read_u32(bytes + offset);
        if (tag == fourcc('S', 'H', 'D', 'R') || tag == fourcc('S', 'H', 'E', 'X')) {
            if (found_program || !parse_program(bytes + offset + 8, chunk_size, parsed)) return false;
            found_program = true;
        }
    }
    if (!found_program) return false;
    // Summarize only fully validated slots. Repeated declaration tokens do
    // not inflate the number of distinct declared slots.
    for (uint32_t slot = 0; slot < parsed.constant_buffers.size(); ++slot)
        if (parsed.constant_buffers[slot].type != ShaderConstantBufferType::NONE)
            parsed.constant_buffer_slots.include(slot);
    for (uint32_t slot = 0; slot < parsed.resources.size(); ++slot)
        if (parsed.resources[slot].type != ShaderResourceType::NONE)
            parsed.resource_slots.include(slot);
    *out = parsed;
    return true;
}
