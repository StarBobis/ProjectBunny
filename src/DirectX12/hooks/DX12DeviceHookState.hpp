#pragma once

#include "hooks/DX12DeviceHookSignatures.hpp"

class DX12DeviceHookState
{
public:
	inline static DX12DeviceHookSignatures::CreateGraphicsPipelineState OrigCreateGraphicsPipelineState = nullptr;
	inline static DX12DeviceHookSignatures::CreateComputePipelineState OrigCreateComputePipelineState = nullptr;
	inline static DX12DeviceHookSignatures::CreatePipelineState OrigCreatePipelineState = nullptr;
	inline static DX12DeviceHookSignatures::DeviceFactoryCreateDevice OrigDeviceFactoryCreateDevice = nullptr;
};
