#pragma once

#include <Windows.h>

class DX12ResourceHookSlotTypes
{
public:
	class ID3D12Resource
	{
	public:
		// IUnknown: slots 0-2
		// ID3D12Object: slots 3-6
		// ID3D12DeviceChild: slot 7 (GetDevice)
		// ID3D12Pageable: no methods
		// ID3D12Resource methods start at slot 8
		static constexpr UINT Map = 8;
		static constexpr UINT Unmap = 9;
	};
};
