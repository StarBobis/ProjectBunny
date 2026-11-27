#pragma once

#include "EnumOptionPrefix.h"

enum class ParamOverrideType {
	INVALID,
	VALUE,
	INI_PARAM,
	VARIABLE,
	RT_WIDTH,
	RT_HEIGHT,
	RES_WIDTH,
	RES_HEIGHT,
	WINDOW_WIDTH,
	WINDOW_HEIGHT,
	TEXTURE,	// Needs shader type and slot number specified in
	SHADER,		// [ShaderOverride]. [TextureOverride] sections can
			// specify filter_index=N to define the value passed in
			// here. Special values for no [TextureOverride]
			// section = 0.0, or [TextureOverride] with no
			// filter_index = 1.0
	VERTEX_COUNT,
	INDEX_COUNT,
	INSTANCE_COUNT,
	FIRST_VERTEX,
	FIRST_INDEX,
	FIRST_INSTANCE,
	THREAD_GROUP_COUNT_X,
	THREAD_GROUP_COUNT_Y,
	THREAD_GROUP_COUNT_Z,
	INDIRECT_OFFSET,
	DRAW_TYPE,
	DRAW_HAS_OVERRIDE,
	CURSOR_VISIBLE,
	CURSOR_SCREEN_X, // Cursor in screen coordinates in pixels
	CURSOR_SCREEN_Y,
	CURSOR_WINDOW_X, // Cursor in window client area coordinates in pixels
	CURSOR_WINDOW_Y,
	CURSOR_X,        // Cursor position scaled so that client area is the range [0:1]
	CURSOR_Y,
	CURSOR_HOTSPOT_X,
	CURSOR_HOTSPOT_Y,
	TIME,
	SCISSOR_LEFT,   // May have an optional scissor rectangle index
	SCISSOR_TOP,    // specified, which is parsed in code, or not -
	SCISSOR_RIGHT,  // in which case it will match the keyword list
	SCISSOR_BOTTOM,
	HUNTING,
	FRAME_ANALYSIS,
	EFFECTIVE_DPI, // For calculating UI scaling factor on 4K+. Note not the same thing as raw DPI.
	SLI,
	STEREO_ACTIVE,
	STEREO_AVAILABLE,
	FRAME_NUMBER,
	DRAW_NUMBER,
	DISPATCH_NUMBER,
	FRAME_TIME,
	FPS,
    RESOURCE_FILTER,
};
inline EnumName_t<const wchar_t *, ParamOverrideType> ParamOverrideTypeNames[] = {
	{L"rt_width", ParamOverrideType::RT_WIDTH},
	{L"rt_height", ParamOverrideType::RT_HEIGHT},
	{L"res_width", ParamOverrideType::RES_WIDTH},
	{L"res_height", ParamOverrideType::RES_HEIGHT},
	{L"window_width", ParamOverrideType::WINDOW_WIDTH},
	{L"window_height", ParamOverrideType::WINDOW_HEIGHT},
	{L"vertex_count", ParamOverrideType::VERTEX_COUNT},
	{L"index_count", ParamOverrideType::INDEX_COUNT},
	{L"instance_count", ParamOverrideType::INSTANCE_COUNT},
	{L"first_vertex", ParamOverrideType::FIRST_VERTEX},
	{L"first_index", ParamOverrideType::FIRST_INDEX},
	{L"first_instance", ParamOverrideType::FIRST_INSTANCE},
	{L"thread_group_count_x", ParamOverrideType::THREAD_GROUP_COUNT_X},
	{L"thread_group_count_y", ParamOverrideType::THREAD_GROUP_COUNT_Y},
	{L"thread_group_count_z", ParamOverrideType::THREAD_GROUP_COUNT_Z},
	{L"indirect_offset", ParamOverrideType::INDIRECT_OFFSET},
	{L"draw_type", ParamOverrideType::DRAW_TYPE},
	{L"draw_has_override", ParamOverrideType::DRAW_HAS_OVERRIDE},
	{L"cursor_showing", ParamOverrideType::CURSOR_VISIBLE},
	{L"cursor_screen_x", ParamOverrideType::CURSOR_SCREEN_X},
	{L"cursor_screen_y", ParamOverrideType::CURSOR_SCREEN_Y},
	{L"cursor_window_x", ParamOverrideType::CURSOR_WINDOW_X},
	{L"cursor_window_y", ParamOverrideType::CURSOR_WINDOW_Y},
	{L"cursor_x", ParamOverrideType::CURSOR_X},
	{L"cursor_y", ParamOverrideType::CURSOR_Y},
	{L"cursor_hotspot_x", ParamOverrideType::CURSOR_HOTSPOT_X},
	{L"cursor_hotspot_y", ParamOverrideType::CURSOR_HOTSPOT_Y},
	{L"time", ParamOverrideType::TIME},
	{L"scissor_left", ParamOverrideType::SCISSOR_LEFT},
	{L"scissor_top", ParamOverrideType::SCISSOR_TOP},
	{L"scissor_right", ParamOverrideType::SCISSOR_RIGHT},
	{L"scissor_bottom", ParamOverrideType::SCISSOR_BOTTOM},
	{L"hunting", ParamOverrideType::HUNTING},
	{L"frame_analysis", ParamOverrideType::FRAME_ANALYSIS},
	{L"effective_dpi", ParamOverrideType::EFFECTIVE_DPI},
	{L"sli", ParamOverrideType::SLI},
	{L"stereo_active", ParamOverrideType::STEREO_ACTIVE},
	{L"stereo_available", ParamOverrideType::STEREO_AVAILABLE},
	{L"frame_number", ParamOverrideType::FRAME_NUMBER},
	{L"draw_number", ParamOverrideType::DRAW_NUMBER},
	{L"dispatch_number", ParamOverrideType::DISPATCH_NUMBER},
	{L"frame_time", ParamOverrideType::FRAME_TIME},
	{L"fps", ParamOverrideType::FPS},
	{NULL, ParamOverrideType::INVALID} // End of list marker
};

namespace migoto {

inline constexpr bool AllowedOutsideCommandList(ParamOverrideType type)
{
	// List of operand types allowed outside of a command list, e.g. in a
	// [Key] / [Preset] section
	switch (type) {
		case ParamOverrideType::VALUE:
		case ParamOverrideType::INI_PARAM:
		case ParamOverrideType::VARIABLE:
		case ParamOverrideType::RES_WIDTH:
		case ParamOverrideType::RES_HEIGHT:
		case ParamOverrideType::TIME:
		case ParamOverrideType::FRAME_NUMBER:
		case ParamOverrideType::HUNTING:
		case ParamOverrideType::EFFECTIVE_DPI:
		case ParamOverrideType::SLI:
		case ParamOverrideType::STEREO_ACTIVE:
		case ParamOverrideType::STEREO_AVAILABLE:
			return true;
	}
	return false;
}

} // namespace migoto
