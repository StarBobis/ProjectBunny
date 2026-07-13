#pragma once

#include <Windows.h>

class DX12DeviceHookSlotTypes
{
public:
	class ID3D12Device
	{
	public:
		static constexpr UINT CreateGraphicsPipelineState = 10;
		static constexpr UINT CreateComputePipelineState = 11;
	};

	class ID3D12Device2
	{
	public:
		static constexpr UINT CreatePipelineState = 47;
	};

	class ID3D12DeviceFactory
	{
	public:
		static constexpr UINT CreateDevice = 9;
	};
};
