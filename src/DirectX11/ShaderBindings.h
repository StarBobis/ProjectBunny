#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <d3d11.h>

enum class ShaderConstantBufferType : uint8_t
{
	NONE              = 0,
	IMMEDIATE_INDEXED = 1,
	DYNAMIC_INDEXED   = 2
};

struct ShaderConstantBuffer
{
	ShaderConstantBufferType type = ShaderConstantBufferType::NONE;
	uint32_t size = 0;
};

enum class ShaderResourceType : uint8_t
{
	NONE       = 0,
	TYPED      = 1,
	STRUCTURED = 2,
	RAW        = 3
};

struct ShaderResource
{
	ShaderResourceType type = ShaderResourceType::NONE;

	// Always D3D_SRV_DIMENSION_BUFFEREX for ShaderResourceType::RAW.
	// Always D3D_SRV_DIMENSION_BUFFER for ShaderResourceType::STRUCTURED.
	D3D_SRV_DIMENSION dimension = D3D_SRV_DIMENSION_UNKNOWN;

	// Only meaningful for ShaderResourceType::STRUCTURED.
	uint32_t stride = 0;
};

struct ShaderSlotRange
{
    uint32_t first = 0;
    uint32_t last = 0;
    uint32_t count = 0;

    void include(uint32_t slot) noexcept
    {
        if (!count) first = last = slot;
        else {
            if (slot < first) first = slot;
            if (slot > last) last = slot;
        }
        ++count;
    }
};

struct ShaderBindings
{
	std::array<ShaderConstantBuffer, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT> constant_buffers{};
	std::array<ShaderResource, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> resources{};
    ShaderSlotRange constant_buffer_slots{};
    ShaderSlotRange resource_slots{};
};

bool get_shader_bindings_from_bytecode(const void* data, size_t size, ShaderBindings* out_bindings);
