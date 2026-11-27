#pragma once

#include "DxgiFormatNames.h"
#include <iterator>
#include <optional>
#include <string>

namespace migoto::tests {

struct GraphicsExpressionCase {
	const wchar_t *text;
	float value;
};

inline constexpr GraphicsExpressionCase graphicsExpressions[] = {
	{L"D3D11_BIND_vertex_buffer", 1}, {L"d3d11_bind_INDEX_BUFFER", 2},
	{L"d3d11_bind_constant_buffer", 4}, {L"d3d11_bind_shader_resource", 8},
	{L"d3d11_bind_stream_output", 16}, {L"d3d11_bind_render_target", 32},
	{L"d3d11_bind_depth_stencil", 64}, {L"d3d11_bind_unordered_access", 128},
	{L"d3d11_bind_decoder", 512}, {L"d3d11_bind_video_encoder", 1024},
	{L"dxgi_format_unknown == 0", 1}, {L"DxGi_FoRmAt_R8G8B8A8_UNORM", 28},
	{L"DXGI_FORMAT_420_OPAQUE", 106}, {L"DXGI_FORMAT_B4G4R4A4_UNORM", 115},
	{L"d3d11_bind_index_buffer | d3d11_bind_shader_resource", 10},
	{L"DXGI_FORMAT_R32_UINT + DXGI_FORMAT_R16_UINT", 99},
};

// Exercise every legacy format name through the backend's expression evaluator.
template<class Evaluate, class Check>
void CheckGraphicsConstants(Evaluate evaluate, Check check)
{
	for (unsigned i = 0; i < std::size(DXGIFormats); ++i) {
		const char *name = DXGIFormats[i];
		std::wstring expression = L"DXGI_FORMAT_" + std::wstring(name, name + std::strlen(name));
		check(evaluate(expression) == static_cast<float>(i), "DXGI format expression has its original numeric value");
	}
	for (const auto &test : graphicsExpressions)
		check(evaluate(test.text) == test.value, "DX11 binding bit or combined graphics expression");
	for (const wchar_t *invalid : {L"DXGI_FORMAT_", L"DXGI_FORMAT_28", L"DXGI_FORMAT_R8_UNORMextra",
			L"DXGI_FORMAT_UNKNOWN1", L"D3D11_BIND_", L"D3D11_BIND_none", L"D3D11_BIND_decoderjunk",
			L"1 || d3d11_bind_misspelled"})
		check(!evaluate(invalid).has_value(), "reject malformed graphics constants, including a logically dead branch");
}

} // namespace migoto::tests
