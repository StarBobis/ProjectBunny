#pragma once

#include "DxgiFormatNames.h"
#include "EnumOptionPrefix.h"
#include <string>

// The bind flags are usually set automatically, but there are cases where
// these can be used to influence driver heuristics (e.g. a buffer that
// includes a render target or UAV bind flag may be stereoised), so we allow
// them to be set manually as well. If specified these will *replace* the bind
// flags 3DMigoto sets automatically - if you use these, you presumably know
// what you are doing. This enumeration is essentially the same as
// D3D11_BIND_FLAG, but this allows us to use parse_enum_option_string.
enum class CustomResourceBindFlags {
	INVALID         = 0x00000000,
	VERTEX_BUFFER   = 0x00000001,
	INDEX_BUFFER    = 0x00000002,
	CONSTANT_BUFFER = 0x00000004,
	SHADER_RESOURCE = 0x00000008,
	STREAM_OUTPUT   = 0x00000010,
	RENDER_TARGET   = 0x00000020,
	DEPTH_STENCIL   = 0x00000040,
	UNORDERED_ACCESS= 0x00000080,
	DECODER         = 0x00000200,
	VIDEO_ENCODER   = 0x00000400,
};
SENSIBLE_ENUM(CustomResourceBindFlags);
inline EnumName_t<const wchar_t *, CustomResourceBindFlags> CustomResourceBindFlagNames[] = {
	{L"vertex_buffer", CustomResourceBindFlags::VERTEX_BUFFER},
	{L"index_buffer", CustomResourceBindFlags::INDEX_BUFFER},
	{L"constant_buffer", CustomResourceBindFlags::CONSTANT_BUFFER},
	{L"shader_resource", CustomResourceBindFlags::SHADER_RESOURCE},
	{L"stream_output", CustomResourceBindFlags::STREAM_OUTPUT},
	{L"render_target", CustomResourceBindFlags::RENDER_TARGET},
	{L"depth_stencil", CustomResourceBindFlags::DEPTH_STENCIL},
	{L"unordered_access", CustomResourceBindFlags::UNORDERED_ACCESS},
	{L"decoder", CustomResourceBindFlags::DECODER},
	{L"video_encoder", CustomResourceBindFlags::VIDEO_ENCODER},
	{NULL, CustomResourceBindFlags::INVALID} // End of list marker
};

namespace migoto {

enum class GraphicsConstant { Unmatched, Invalid, Value };

// Both INI dialects expose the DX11 binding bits, even on a DX12 device.
inline GraphicsConstant ReadGraphicsConstant(const std::wstring &name, float &value)
{
	if (name.size() < 11 || (name.front() != L'd' && name.front() != L'D'))
		return GraphicsConstant::Unmatched;
	if (name.size() >= 12 && !_wcsnicmp(name.c_str(), L"dxgi_format_", 12)) {
		const auto format = ParseFormatString(name.c_str(), false);
		if (format == static_cast<DXGI_FORMAT>(-1))
			return GraphicsConstant::Invalid;
		value = static_cast<float>(format);
		return GraphicsConstant::Value;
	}
	if (!_wcsnicmp(name.c_str(), L"d3d11_bind_", 11)) {
		const auto flag = lookup_enum_val(CustomResourceBindFlagNames, name.c_str() + 11, CustomResourceBindFlags::INVALID);
		if (flag == CustomResourceBindFlags::INVALID)
			return GraphicsConstant::Invalid;
		value = static_cast<float>(flag);
		return GraphicsConstant::Value;
	}
	return GraphicsConstant::Unmatched;
}

} // namespace migoto
