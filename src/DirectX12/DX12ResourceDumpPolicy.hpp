#pragma once

#include <d3d12.h>

#include <string>

#include "DX12BindingTracker.hpp"
#include "DX12ResourceTracker.hpp"

class DX12ResourceDumpPolicy
{
public:
	static bool IsTextureResourceDimension(D3D12_RESOURCE_DIMENSION dimension)
	{
		return dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D ||
			dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
			dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D;
	}

	static bool ShouldDumpBinding(const DX12FrameResourceBinding &binding)
	{
		if (!binding.usageClass.empty()) {
			if (!binding.hasDescriptor)
				return true;
			return binding.descriptor.resource &&
				binding.descriptor.hasResourceDesc &&
				binding.descriptor.resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER;
		}

		if (binding.bindSpace == "hunting_metadata" ||
			binding.bindSpace == "hunting_resource") {
			if (binding.bindSpace == "hunting_resource") {
				const bool isBufferResource =
					binding.hasDescriptor &&
					binding.descriptor.resource &&
					binding.descriptor.hasResourceDesc &&
					binding.descriptor.resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER;
				const bool isTextureResource =
					binding.hasDescriptor &&
					binding.descriptor.resource &&
					binding.descriptor.hasResourceDesc &&
					IsTextureResourceDimension(binding.descriptor.resourceDesc.Dimension);
				return binding.hasDescriptor &&
					binding.descriptor.resource &&
					binding.descriptor.hasResourceDesc &&
					(isBufferResource || isTextureResource);
			}
			const bool isBufferResource =
				binding.hasDescriptor &&
				binding.descriptor.resource &&
				binding.descriptor.hasResourceDesc &&
				binding.descriptor.resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER;
			const bool isTextureResource =
				binding.hasDescriptor &&
				binding.descriptor.resource &&
				binding.descriptor.hasResourceDesc &&
				IsTextureResourceDimension(binding.descriptor.resourceDesc.Dimension);
			return binding.hasDescriptor &&
				binding.descriptor.resource &&
				binding.descriptor.hasResourceDesc &&
				((isTextureResource && (
					binding.descriptor.kind == DescriptorKind::SRV ||
					binding.descriptor.kind == DescriptorKind::UAV ||
					binding.descriptor.kind == DescriptorKind::RTV ||
					binding.descriptor.kind == DescriptorKind::DSV)) ||
				 (isBufferResource && (
					binding.descriptor.kind == DescriptorKind::CBV ||
					binding.descriptor.kind == DescriptorKind::SRV ||
					binding.descriptor.kind == DescriptorKind::UAV)));
		}

		const bool isDispatch = binding.dispatchId != 0;
		const bool isDraw = binding.drawId != 0;
		if (!isDispatch && !isDraw)
			return false;

		const bool isGraphicsSpace =
			binding.bindSpace == "graphics_cbv_srv_uav" ||
			binding.bindSpace == "graphics_sampler" ||
			binding.bindSpace == "graphics_root";
		const bool isComputeSpace =
			binding.bindSpace == "compute_cbv_srv_uav" ||
			binding.bindSpace == "compute_sampler" ||
			binding.bindSpace == "compute_root" ||
			binding.bindSpace == "hunting_uav_state";
		const bool isOutputSpace = binding.bindSpace == "output";
		if (isOutputSpace)
			return binding.hasDescriptor &&
				binding.descriptor.resource &&
				binding.descriptor.hasResourceDesc &&
				IsTextureResourceDimension(binding.descriptor.resourceDesc.Dimension) &&
				(binding.descriptor.kind == DescriptorKind::RTV ||
				 binding.descriptor.kind == DescriptorKind::DSV);
		if ((isDispatch && !isComputeSpace) || (!isDispatch && !isGraphicsSpace))
			return false;
		if (binding.rootDescriptor)
			return binding.gpuVirtualAddress != 0 &&
				(binding.rangeType == D3D12_ROOT_PARAMETER_TYPE_CBV ||
				 binding.rangeType == D3D12_ROOT_PARAMETER_TYPE_SRV ||
				 binding.rangeType == D3D12_ROOT_PARAMETER_TYPE_UAV);
		if (!binding.hasDescriptor)
			return false;
		if (!binding.descriptor.resource || !binding.descriptor.hasResourceDesc)
			return false;
		const bool isBufferResource =
			binding.descriptor.resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER;
		const bool isTextureResource =
			IsTextureResourceDimension(binding.descriptor.resourceDesc.Dimension);
		if (!isBufferResource && !isTextureResource)
			return false;
		if (isDispatch)
			return binding.descriptor.kind == DescriptorKind::CBV ||
				binding.descriptor.kind == DescriptorKind::SRV ||
				binding.descriptor.kind == DescriptorKind::UAV;
		if (binding.descriptor.kind == DescriptorKind::CBV)
			return isBufferResource;
		return binding.descriptor.kind == DescriptorKind::SRV ||
			binding.descriptor.kind == DescriptorKind::UAV;
	}

	static const char *ShortBindSpace(const std::string &bindSpace)
	{
		if (bindSpace == "graphics_cbv_srv_uav")
			return "gfx";
		if (bindSpace == "graphics_sampler")
			return "gsp";
		if (bindSpace == "compute_cbv_srv_uav")
			return "cmp";
		if (bindSpace == "compute_sampler")
			return "csp";
		if (bindSpace == "hunting_metadata")
			return "meta";
		if (bindSpace == "hunting_resource")
			return "res";
		return bindSpace.empty() ? "unk" : bindSpace.c_str();
	}

	static const char *ShortDescriptorKind(const std::string &kind)
	{
		if (kind == DescriptorKind::CBV || kind == DescriptorKind::CONSTANT_BUFFER_VIEW)
			return "cbv";
		if (kind == DescriptorKind::SRV || kind == DescriptorKind::SHADER_RESOURCE_VIEW)
			return "srv";
		if (kind == DescriptorKind::UAV || kind == DescriptorKind::UNORDERED_ACCESS_VIEW)
			return "uav";
		if (kind == DescriptorKind::Sampler)
			return "smp";
		if (kind == DescriptorKind::RTV || kind == DescriptorKind::RENDER_TARGET_VIEW)
			return "rtv";
		if (kind == DescriptorKind::DSV || kind == DescriptorKind::DEPTH_STENCIL_VIEW)
			return "dsv";
		return kind.empty() ? "res" : kind.c_str();
	}

	static const char *ShaderVisibilityPrefix(UINT shaderVisibility, const std::string &bindSpace)
	{
		if (bindSpace.rfind("compute", 0) == 0)
			return "cs";
		switch (shaderVisibility) {
		case D3D12_SHADER_VISIBILITY_VERTEX:
			return "vs";
		case D3D12_SHADER_VISIBILITY_HULL:
			return "hs";
		case D3D12_SHADER_VISIBILITY_DOMAIN:
			return "ds";
		case D3D12_SHADER_VISIBILITY_GEOMETRY:
			return "gs";
		case D3D12_SHADER_VISIBILITY_PIXEL:
			return "ps";
		case D3D12_SHADER_VISIBILITY_ALL:
		default:
			return "all";
		}
	}

	static char RegisterPrefix(UINT rangeType, const std::string &descriptorKind)
	{
		if (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV ||
				rangeType == D3D12_ROOT_PARAMETER_TYPE_SRV)
			return 't';
		if (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV ||
				rangeType == D3D12_ROOT_PARAMETER_TYPE_UAV)
			return 'u';
		if (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_CBV ||
				rangeType == D3D12_ROOT_PARAMETER_TYPE_CBV)
			return 'b';
		if (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER)
			return 's';
		if (descriptorKind == DescriptorKind::SRV || descriptorKind == DescriptorKind::SHADER_RESOURCE_VIEW)
			return 't';
		if (descriptorKind == DescriptorKind::UAV || descriptorKind == DescriptorKind::UNORDERED_ACCESS_VIEW)
			return 'u';
		if (descriptorKind == DescriptorKind::CBV || descriptorKind == DescriptorKind::CONSTANT_BUFFER_VIEW)
			return 'b';
		if (descriptorKind == DescriptorKind::Sampler)
			return 's';
		return '\0';
	}

	static bool BuildSemanticSlot(
		const DX12FrameResourceBinding &binding, char *text, size_t textCount)
	{
		if (!text || textCount == 0)
			return false;
		text[0] = '\0';
		if (binding.bindSpace == "hunting_metadata") {
			sprintf_s(text, textCount, "meta-d%llu",
				static_cast<unsigned long long>(binding.descriptorIndex));
			return true;
		}
		if (binding.bindSpace == "hunting_resource") {
			sprintf_s(text, textCount, "res-d%llu",
				static_cast<unsigned long long>(binding.descriptorIndex));
			return true;
		}
		if (binding.shaderRegister == UINT_MAX)
			return false;
		const char registerPrefix = RegisterPrefix(binding.rangeType, binding.descriptor.kind);
		if (!registerPrefix)
			return false;
		const char *stage = ShaderVisibilityPrefix(binding.shaderVisibility, binding.bindSpace);
		sprintf_s(text, textCount, "%s-%c%u", stage, registerPrefix, binding.shaderRegister);
		return true;
	}
};
