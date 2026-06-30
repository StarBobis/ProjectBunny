#pragma once

#include <Windows.h>
#include <d3d12.h>

#include "DX12ResourceTrackerPrivate.hpp"

class DX12DescriptorCollectPipeline
{
public:
	static void RecordDescriptor(DescriptorRecord &&record, const char *source);
	static void RecordDescriptorCopyRange(
		ID3D12Device *device,
		D3D12_DESCRIPTOR_HEAP_TYPE type,
		D3D12_CPU_DESCRIPTOR_HANDLE destStart,
		D3D12_CPU_DESCRIPTOR_HANDLE srcStart,
		UINT count,
		const char *source);
	static void DrainForSnapshot(const char *reason);
	static void Stop();
};
