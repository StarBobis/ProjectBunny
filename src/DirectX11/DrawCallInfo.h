#pragma once

#include <d3d11_1.h>

#include "../common/DrawCallParameters.h"

enum class DrawOverrideCandidate : unsigned char { Unqueried, Miss, Possible };

struct DrawCallInfo : DrawCallParameters
{
	ID3D11Buffer **indirect_buffer = nullptr;
	UINT args_offset = 0;

	bool skip = false, hunting_skip = false;
	DrawOverrideCandidate override_candidate = DrawOverrideCandidate::Unqueried;

	DrawCallInfo() = default;

	DrawCallInfo(DrawCall type,
			UINT VertexCount, UINT IndexCount, UINT InstanceCount,
			UINT FirstVertex, UINT FirstIndex, UINT FirstInstance,
			ID3D11Buffer **indirect_buffer, UINT args_offset,
			UINT ThreadGroupCountX = 0, UINT ThreadGroupCountY = 0, UINT ThreadGroupCountZ = 0) :
		DrawCallParameters{type, VertexCount, IndexCount, InstanceCount, FirstVertex, FirstIndex,
			FirstInstance, ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ},
		indirect_buffer(indirect_buffer),
		args_offset(args_offset)
	{}
};
