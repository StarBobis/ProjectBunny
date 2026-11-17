// DX12ShaderModelParse: pure DXBC/DXIL container helpers for the shader dump.
// No D3D12/DXGI types live here on purpose: the audit executable in
// src/DirectX12/tests includes this header and therefore exercises the exact
// production rules instead of a copy of them.
#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <string>

namespace dx12_shader_parse {

constexpr uint32_t MakeFourCC(char a, char b, char c, char d)
{
	return static_cast<uint32_t>(static_cast<uint8_t>(a)) |
		(static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8) |
		(static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16) |
		(static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
}

// `offset` is size_t: an offset computed as uint32_t can wrap before it is
// compared with the blob size.
inline bool ReadU32LE(const uint8_t *data, size_t size, size_t offset, uint32_t *value)
{
	if (!value || offset + sizeof(uint32_t) > size)
		return false;
	memcpy(value, data + offset, sizeof(uint32_t));
	return true;
}

inline const char *ShaderKindPrefix(uint32_t kind)
{
	switch (kind) {
	case 0:
		return "ps";
	case 1:
		return "vs";
	case 2:
		return "gs";
	case 3:
		return "hs";
	case 4:
		return "ds";
	case 5:
		return "cs";
	default:
		return nullptr;
	}
}

inline bool FormatShaderModel(uint32_t kind, uint32_t major, uint32_t minor, std::string *model)
{
	if (!model)
		return false;
	const char *prefix = ShaderKindPrefix(kind);
	if (!prefix)
		return false;
	char text[16] = {};
	sprintf_s(text, "%s_%u_%u", prefix, major, minor);
	*model = text;
	return true;
}

// Chunk offsets are read from the container as uint32_t.  The header-fit test
// must subtract (or widen) instead of comparing `chunkOffset + headerSize`:
// that addition wraps in 32 bits, so 0xffffffff + 12 compares as 11, passes a
// small blob, and the memcpy below then uses the full 0xffffffff offset.
// F-201b: see the audit item of the same name.
inline bool GetDxbcShaderModel(const void *data, size_t size, std::string *model)
{
	if (!data || size < 32 || !model)
		return false;

	const uint8_t *bytes = static_cast<const uint8_t*>(data);
	if (memcmp(bytes, "DXBC", 4))
		return false;

	uint32_t chunkCount = 0;
	if (!ReadU32LE(bytes, size, 28, &chunkCount))
		return false;

	for (uint32_t i = 0; i < chunkCount; ++i) {
		uint32_t chunkOffset = 0;
		if (!ReadU32LE(bytes, size, 32 + sizeof(uint32_t) * i, &chunkOffset))
			return false;
		if (chunkOffset > size || size - chunkOffset < 12)
			continue;
		uint32_t chunkFourCC = 0;
		memcpy(&chunkFourCC, bytes + chunkOffset, sizeof(chunkFourCC));
		if (chunkFourCC != MakeFourCC('S', 'H', 'D', 'R') &&
		    chunkFourCC != MakeFourCC('S', 'H', 'E', 'X'))
			continue;
		uint32_t versionToken = 0;
		if (!ReadU32LE(bytes, size, static_cast<size_t>(chunkOffset) + 8, &versionToken))
			continue;
		const uint32_t kind = (versionToken >> 16) & 0xffff;
		const uint32_t major = (versionToken >> 4) & 0xf;
		const uint32_t minor = versionToken & 0xf;
		return FormatShaderModel(kind, major, minor, model);
	}
	return false;
}

inline bool GetDxilShaderModel(const void *data, size_t size, std::string *model)
{
	if (!data || size < 32 || !model)
		return false;

	const uint8_t *bytes = static_cast<const uint8_t*>(data);
	if (memcmp(bytes, "DXBC", 4))
		return false;

	uint32_t chunkCount = 0;
	if (!ReadU32LE(bytes, size, 28, &chunkCount))
		return false;

	for (uint32_t i = 0; i < chunkCount; ++i) {
		uint32_t chunkOffset = 0;
		if (!ReadU32LE(bytes, size, 32 + sizeof(uint32_t) * i, &chunkOffset))
			return false;
		if (chunkOffset > size || size - chunkOffset < 16)
			continue;
		uint32_t chunkFourCC = 0;
		memcpy(&chunkFourCC, bytes + chunkOffset, sizeof(chunkFourCC));
		if (chunkFourCC != MakeFourCC('D', 'X', 'I', 'L'))
			continue;
		uint32_t programVersion = 0;
		if (!ReadU32LE(bytes, size, static_cast<size_t>(chunkOffset) + 8, &programVersion))
			continue;
		const uint32_t kind = (programVersion >> 16) & 0xffff;
		const uint32_t major = (programVersion >> 4) & 0xf;
		const uint32_t minor = programVersion & 0xf;
		return FormatShaderModel(kind, major, minor, model);
	}
	return false;
}

// Production order: a DXIL chunk wins over the legacy DXBC code chunk.
inline bool GetShaderModelFromBytes(const void *data, size_t size, std::string *model)
{
	return GetDxilShaderModel(data, size, model) ||
		GetDxbcShaderModel(data, size, model);
}

} // namespace dx12_shader_parse
