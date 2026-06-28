#include "DX12ResourceDumpUtil.h"

#include "DX12ResourceTracker.h"

bool DX12DumpIsTextureResourceDimension(D3D12_RESOURCE_DIMENSION dimension)
{
	return dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D ||
		dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
		dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D;
}

bool DX12DumpShouldDumpBinding(const DX12FrameResourceBinding &binding)
{
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
		binding.bindSpace == "compute_root";
	const bool isOutputSpace = binding.bindSpace == "output";
	if (isOutputSpace)
		return binding.hasDescriptor &&
			binding.descriptor.resource &&
			binding.descriptor.hasResourceDesc &&
			DX12DumpIsTextureResourceDimension(binding.descriptor.resourceDesc.Dimension) &&
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
		DX12DumpIsTextureResourceDimension(binding.descriptor.resourceDesc.Dimension);
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

const char *DX12DumpShortBindSpace(const std::string &bindSpace)
{
	if (bindSpace == "graphics_cbv_srv_uav")
		return "gfx";
	if (bindSpace == "graphics_sampler")
		return "gsp";
	if (bindSpace == "compute_cbv_srv_uav")
		return "cmp";
	if (bindSpace == "compute_sampler")
		return "csp";
	return bindSpace.empty() ? "unk" : bindSpace.c_str();
}

const char *DX12DumpShortDescriptorKind(const std::string &kind)
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
