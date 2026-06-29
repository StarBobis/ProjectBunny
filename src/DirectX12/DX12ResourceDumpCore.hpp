#pragma once

#include <Windows.h>
#include <stdint.h>
#include <stdio.h>

#include <string>

#include "crc32c.h"
#include "DX12BindingTracker.hpp"
#include "DX12FrameAnalysis.hpp"
#include "DX12ResourceDumpTypes.hpp"
#include "DX12ResourceTracker.hpp"

class DX12ResourceDumpCore
{
public:
static constexpr uint32_t MakeFourCC(char a, char b, char c, char d)
{
	return static_cast<uint32_t>(static_cast<uint8_t>(a)) |
		(static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8) |
		(static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16) |
		(static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
}

static bool EnsureDirectory(const wchar_t *path)
{
	return CreateDirectoryW(path, nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

static const char *DumpTaskSourceName(DumpTaskSource sourceKind)
{
	switch (sourceKind) {
	case DumpTaskSource::Descriptor:
		return "descriptor";
	case DumpTaskSource::InputAssembler:
		return "ia";
	case DumpTaskSource::Output:
		return "output";
	case DumpTaskSource::Resource:
		return "resource";
	default:
		return "unknown";
	}
}

static uint32_t HashBytes(uint32_t seed, const void *data, size_t size)
{
	if (!data || size == 0)
		return seed;
	__try {
		return crc32c_append(seed, static_cast<const uint8_t*>(data), size);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		DX12FrameAnalysis::LogJsonFunc("Crc32cFailed",
			"\"exception\":\"0x%lx\",\"size\":%zu", GetExceptionCode(), size);
		return 0;
	}
}

static void GetDedupedDirectory(const wchar_t *dir, wchar_t *path, size_t pathCount)
{
	swprintf_s(path, pathCount, L"%s\\deduped", dir);
	DX12ResourceDumpCore::EnsureDirectory(path);
}

static bool UnsafeResourceCopyEnabled() { return true; }
static UINT64 FrameReadbackBudgetBytes() { return 0; }

static UINT64 AlignUp(UINT64 value, UINT64 alignment)
{
	return (value + alignment - 1) & ~(alignment - 1);
}

static const char *RootDescriptorKindName(UINT rangeType)
{
	if (rangeType == D3D12_ROOT_PARAMETER_TYPE_CBV)
		return DescriptorKind::CBV.data();
	if (rangeType == D3D12_ROOT_PARAMETER_TYPE_SRV)
		return DescriptorKind::SRV.data();
	if (rangeType == D3D12_ROOT_PARAMETER_TYPE_UAV)
		return DescriptorKind::UAV.data();
	return "";
}

static const char *DescriptorRangeKindName(UINT rangeType)
{
	if (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_CBV)
		return DescriptorKind::CBV.data();
	if (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV)
		return DescriptorKind::SRV.data();
	if (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV)
		return DescriptorKind::UAV.data();
	if (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER)
		return DescriptorKind::Sampler.data();
	return "";
}

static const char *BindingKindName(const DX12FrameResourceBinding &binding)
{
	if (!binding.usageClass.empty())
		return binding.usageClass.c_str();
	if (!binding.descriptor.kind.empty())
		return binding.descriptor.kind.c_str();
	if (binding.rootDescriptor)
		return DX12ResourceDumpCore::RootDescriptorKindName(binding.rangeType);
	return DescriptorRangeKindName(binding.rangeType);
}

static DX12FrameResourceBinding ResourceBindingFromOutputBinding(
	const DX12FrameOutputBinding &output)
{
	DX12FrameResourceBinding binding;
	binding.eventSerial = output.eventSerial;
	binding.drawId = output.drawId ? output.drawId : output.eventSerial;
	binding.dispatchId = output.dispatchId;
	binding.psoIndex = output.psoIndex;
	binding.shaderInfo = output.shaderInfo;
	binding.bindSpace = "output";
	binding.rootParameterIndex = output.slot;
	binding.rangeIndex = 0;
	binding.rangeType = output.kind == "DSV" ?
		D3D12_DESCRIPTOR_HEAP_TYPE_DSV : D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	binding.shaderRegister = output.slot;
	binding.registerSpace = 0;
	binding.descriptorOffset = output.slot;
	binding.cpuHandle = output.cpuHandle;
	binding.descriptor = output.descriptor;
	if (binding.descriptor.kind.empty())
		binding.descriptor.kind = output.kind;
	binding.hasDescriptor = output.hasDescriptor;
	return binding;
}


static DX12FrameResourceBinding ResourceBindingFromTrackedResource(
	const DX12ResourceSummary &resource, UINT64 index)
{
	DX12FrameResourceBinding binding;
	binding.bindSpace = "hunting_resource";
	binding.rangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	binding.descriptorIndex = index;
	binding.descriptor.kind = DescriptorKind::SRV;
	binding.descriptor.resource = resource.resource;
	binding.descriptor.resourceDesc = resource.resourceDesc;
	binding.descriptor.hasResourceDesc = resource.hasResourceDesc;
	binding.descriptor.resourceHeapType = resource.resourceHeapType;
	binding.descriptor.hasResourceHeapType = resource.hasResourceHeapType;
	binding.descriptor.gpuVirtualAddress = resource.gpuVirtualAddress;
	binding.descriptor.viewSize = resource.size;
	binding.descriptor.currentState = resource.currentState;
	binding.descriptor.hasCurrentState = resource.hasCurrentState;
	binding.hasDescriptor = resource.resource != nullptr;
	return binding;
}


};

