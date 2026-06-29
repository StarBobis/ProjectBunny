#pragma once

#include <d3d12.h>

class DX12IaBufferViewUtils
{
public:
	static bool IsVertexBufferBound(const D3D12_VERTEX_BUFFER_VIEW &view)
	{
		return view.BufferLocation != 0 && view.SizeInBytes != 0;
	}
};
