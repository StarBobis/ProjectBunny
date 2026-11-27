#pragma once

#include <cstdint>

// These values can now be exposed through draw_type and their values should
// not be changed. Any additions should be added to the end of the list.
enum class DrawCall {
	Invalid                      = 0,
	Draw                         = 1,
	DrawIndexed                  = 2,
	DrawInstanced                = 3,
	DrawIndexedInstanced         = 4,
	DrawInstancedIndirect        = 5,
	DrawIndexedInstancedIndirect = 6,
	DrawAuto                     = 7,
	Dispatch                     = 8,
	DispatchIndirect             = 9,
};

struct DrawCallParameters
{
	DrawCall type = DrawCall::Invalid;
	std::uint32_t VertexCount = 0;
	std::uint32_t IndexCount = 0;
	std::uint32_t InstanceCount = 0;
	std::uint32_t FirstVertex = 0;
	std::uint32_t FirstIndex = 0;
	std::uint32_t FirstInstance = 0;
	std::uint32_t ThreadGroupCountX = 0;
	std::uint32_t ThreadGroupCountY = 0;
	std::uint32_t ThreadGroupCountZ = 0;
};
