#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <stdint.h>

#include <string>
#include <vector>

#include "DX12BindingTracker.hpp"

class DX12PreSkinPlanUtils
{
public:
	static uint64_t HashCombine(uint64_t hash, uint64_t value)
	{
		hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
		return hash;
	}

	static uint64_t HashString(uint64_t hash, const std::string &value)
	{
		for (size_t index = 0; index < value.size(); ++index) {
			hash = HashCombine(hash, static_cast<unsigned char>(value[index]));
		}
		return hash;
	}

	static uint64_t AppendDescriptor(uint64_t hash, const DX12DescriptorSummary &descriptor)
	{
		hash = HashString(hash, descriptor.kind);
		hash = HashCombine(hash, reinterpret_cast<uint64_t>(descriptor.resource));
		hash = HashCombine(hash, reinterpret_cast<uint64_t>(descriptor.counterResource));
		hash = HashCombine(hash, descriptor.gpuVirtualAddress);
		hash = HashCombine(hash, descriptor.resourceOffset);
		hash = HashCombine(hash, descriptor.viewSize);
		hash = HashCombine(hash, descriptor.viewFormat);
		hash = HashCombine(hash, descriptor.viewDimension);
		hash = HashCombine(hash, descriptor.cbvSize);
		hash = HashCombine(hash, descriptor.firstElement);
		hash = HashCombine(hash, descriptor.numElements);
		hash = HashCombine(hash, descriptor.structureByteStride);
		hash = HashCombine(hash, descriptor.bufferViewOffset);
		hash = HashCombine(hash, descriptor.bufferViewBytes);
		if (descriptor.hasResourceDesc) {
			hash = HashCombine(hash, descriptor.resourceDesc.Dimension);
			hash = HashCombine(hash, descriptor.resourceDesc.Alignment);
			hash = HashCombine(hash, descriptor.resourceDesc.Width);
			hash = HashCombine(hash, descriptor.resourceDesc.Height);
			hash = HashCombine(hash, descriptor.resourceDesc.DepthOrArraySize);
			hash = HashCombine(hash, descriptor.resourceDesc.MipLevels);
			hash = HashCombine(hash, descriptor.resourceDesc.Format);
			hash = HashCombine(hash, descriptor.resourceDesc.Layout);
			hash = HashCombine(hash, descriptor.resourceDesc.Flags);
		}
		return hash;
	}

	static uint64_t AppendBinding(
		uint64_t hash,
		const DX12CurrentComputeUavBinding &binding,
		uint64_t listSalt)
	{
		hash = HashCombine(hash, listSalt);
		hash = HashCombine(hash, binding.rootParameterIndex);
		hash = HashCombine(hash, binding.rangeIndex);
		hash = HashCombine(hash, binding.shaderVisibility);
		hash = HashCombine(hash, binding.shaderRegister);
		hash = HashCombine(hash, binding.registerSpace);
		hash = HashCombine(hash, binding.descriptorOffset);
		hash = HashCombine(hash, binding.rootDescriptor ? 1 : 0);
		hash = HashCombine(hash, binding.gpuVirtualAddress);
		hash = HashCombine(hash, binding.hasDescriptor ? 1 : 0);
		if (binding.hasDescriptor) {
			hash = AppendDescriptor(hash, binding.descriptor);
		} else {
			hash = HashCombine(hash, binding.descriptorIncrement);
			hash = HashCombine(hash, binding.tableCopyCount);
			hash = HashCombine(hash, static_cast<uint64_t>(binding.tableCpuStart));
			hash = HashCombine(hash, binding.tableGpuStart.ptr);
			hash = HashCombine(hash, reinterpret_cast<uint64_t>(binding.tableHeap));
		}
		return hash;
	}

	static uint64_t AppendBindingList(
		uint64_t hash,
		const std::vector<DX12CurrentComputeUavBinding> &bindings,
		uint64_t listSalt)
	{
		hash = HashCombine(hash, bindings.size());
		for (size_t index = 0; index < bindings.size(); ++index) {
			hash = AppendBinding(hash, bindings[index], listSalt);
		}
		return hash;
	}

	static uint64_t AppendRootConstants(
		uint64_t hash,
		const std::vector<DX12CurrentRootConstants> &rootConstants)
	{
		hash = HashCombine(hash, rootConstants.size());
		for (size_t rootIndex = 0; rootIndex < rootConstants.size(); ++rootIndex) {
			const DX12CurrentRootConstants &root = rootConstants[rootIndex];
			hash = HashCombine(hash, root.rootParameterIndex);
			hash = HashCombine(hash, root.maxSet);
			for (UINT valueIndex = 0; valueIndex < 64; ++valueIndex) {
				if (!root.valueValid[valueIndex])
					continue;
				hash = HashCombine(hash, valueIndex);
				hash = HashCombine(hash, root.values[valueIndex]);
			}
		}
		return hash;
	}

	static uint64_t BuildStableBindingFingerprint(
		const std::vector<DX12CurrentComputeUavBinding> &uavs,
		const std::vector<DX12CurrentComputeUavBinding> &srvs,
		const std::vector<DX12CurrentComputeUavBinding> &cbvs,
		const std::vector<DX12CurrentRootConstants> &rootConstants)
	{
		uint64_t hash = 0x7a4d17e6b90f8c35ull;
		hash = AppendBindingList(hash, uavs, 0x5541565full);
		hash = AppendBindingList(hash, srvs, 0x5352565full);
		hash = AppendBindingList(hash, cbvs, 0x4342565full);
		hash = AppendRootConstants(hash, rootConstants);
		if (hash == 0)
			hash = 1;
		return hash;
	}
};
