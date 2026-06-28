#include "DX12ResourceDumpPrepare.h"

#include "DX12ResourceDumpUtil.h"
#include "DX12ResourceTracker.h"
#include "DX12State.h"

D3D12_RESOURCE_STATES DX12DumpGuessDescriptorSourceState(const DX12FrameResourceBinding &binding)
{
	if (binding.descriptor.kind == DescriptorKind::CBV)
		return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
	if (binding.descriptor.kind == DescriptorKind::UAV)
		return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	if (binding.descriptor.kind == DescriptorKind::RTV)
		return D3D12_RESOURCE_STATE_RENDER_TARGET;
	if (binding.descriptor.kind == DescriptorKind::DSV)
		return D3D12_RESOURCE_STATE_DEPTH_WRITE;
	return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
}

D3D12_RESOURCE_STATES DX12DumpGuessIaBufferSourceState(const DX12FrameIaBufferBinding &buffer)
{
	if (buffer.role == IaBufferRole::IB)
		return D3D12_RESOURCE_STATE_INDEX_BUFFER;
	return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
}

bool DX12DumpGpuResourceCopyEnabled() { return true; }

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

static bool ResourceNeedsBarrier(const DX12DescriptorSummary &descriptor)
{
	if (descriptor.hasResourceHeapType &&
		(descriptor.resourceHeapType == D3D12_HEAP_TYPE_UPLOAD ||
			descriptor.resourceHeapType == D3D12_HEAP_TYPE_READBACK))
		return false;
	return true;
}

static bool ResourceNeedsBarrier(const DX12BufferResourceSummary &resource)
{
	if (resource.hasResourceHeapType &&
		(resource.resourceHeapType == D3D12_HEAP_TYPE_UPLOAD ||
			resource.resourceHeapType == D3D12_HEAP_TYPE_READBACK))
		return false;
	return true;
}

static bool StateTrackingAllowsCopy(const DX12DescriptorSummary &descriptor)
{
	if (descriptor.hasResourceHeapType &&
		(descriptor.resourceHeapType == D3D12_HEAP_TYPE_UPLOAD ||
			descriptor.resourceHeapType == D3D12_HEAP_TYPE_READBACK))
		return true;
	return descriptor.hasCurrentState;
}

static bool StateTrackingAllowsCopy(const DX12BufferResourceSummary &resource)
{
	if (resource.hasResourceHeapType &&
		(resource.resourceHeapType == D3D12_HEAP_TYPE_UPLOAD ||
			resource.resourceHeapType == D3D12_HEAP_TYPE_READBACK))
		return true;
	return resource.hasCurrentState;
}

static bool ResourceIsCpuVisible(const DX12DescriptorSummary &descriptor)
{
	return descriptor.hasResourceHeapType &&
		(descriptor.resourceHeapType == D3D12_HEAP_TYPE_UPLOAD ||
			descriptor.resourceHeapType == D3D12_HEAP_TYPE_READBACK);
}

static bool ResourceIsCpuVisible(const DX12BufferResourceSummary &resource)
{
	return resource.hasResourceHeapType &&
		(resource.resourceHeapType == D3D12_HEAP_TYPE_UPLOAD ||
			resource.resourceHeapType == D3D12_HEAP_TYPE_READBACK);
}

static bool CreateReadbackForTask(
	ID3D12Device *device, DumpTask *task,
	UINT64 *frameReadbackBytes, UINT64 frameReadbackBudgetBytes)
{
	if (!device || !task)
		return false;

	if (task->copyBytes == 0) {
		task->skipNote = "copy_size_zero";
		return false;
	}
	if (frameReadbackBytes && frameReadbackBudgetBytes > 0 &&
		*frameReadbackBytes <= frameReadbackBudgetBytes &&
		task->copyBytes > frameReadbackBudgetBytes - *frameReadbackBytes) {
		task->skipNote = "frame_readback_budget_exceeded";
		return false;
	}

	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = D3D12_HEAP_TYPE_READBACK;
	D3D12_RESOURCE_DESC readbackDesc = {};
	readbackDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	readbackDesc.Width = task->copyBytes;
	readbackDesc.Height = 1;
	readbackDesc.DepthOrArraySize = 1;
	readbackDesc.MipLevels = 1;
	readbackDesc.SampleDesc.Count = 1;
	readbackDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	HRESULT hr = device->CreateCommittedResource(
		&heapProps, D3D12_HEAP_FLAG_NONE, &readbackDesc,
		D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&task->readback));
	if (FAILED(hr) || !task->readback) {
		task->skipNote = "readback_create_failed";
		return false;
	}

	if (frameReadbackBytes)
		*frameReadbackBytes += task->copyBytes;
	task->ready = true;
	return true;
}

static bool PrepareDirectMapTask(DumpTask *task)
{
	if (!task || !task->source)
		return false;
	if (task->desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER) {
		task->skipNote = "gpu_copy_disabled";
		return false;
	}
	if (task->copyBytes == 0) {
		task->skipNote = "copy_size_zero";
		return false;
	}

	task->directMap = true;
	task->ready = true;
	task->copied = true;
	task->readbackOffset = task->sourceOffset;
	return true;
}

static bool FillDescriptorDumpTask(
	ID3D12Device *device, const DX12FrameResourceBinding &binding, DumpTask *task)
{
	if (!device || !task || !DX12DumpShouldDumpBinding(binding))
		return false;

	task->sourceKind = DumpTaskSource::Descriptor;
	task->binding = binding;
	if (binding.rootDescriptor) {
		DX12BufferResourceSummary resource;
		if (!DX12ResolveBufferResourceByGpuVa(binding.gpuVirtualAddress, 1, &resource) ||
			!resource.resource || !resource.hasResourceDesc) {
			task->skipNote = "unresolved_root_cbv";
			return false;
		}
		task->source = resource.resource;
		task->desc = resource.resourceDesc;
		task->binding.hasDescriptor = true;
		task->binding.descriptor.kind = RootDescriptorKindName(binding.rangeType);
		task->binding.descriptor.resource = resource.resource;
		task->binding.descriptor.resourceDesc = resource.resourceDesc;
		task->binding.descriptor.hasResourceDesc = resource.hasResourceDesc;
		task->binding.descriptor.resourceHeapType = resource.resourceHeapType;
		task->binding.descriptor.hasResourceHeapType = resource.hasResourceHeapType;
		task->binding.descriptor.gpuVirtualAddress = resource.gpuVirtualAddress;
		task->binding.descriptor.resourceOffset = resource.resourceOffset;
		const UINT64 availableBytes = task->desc.Width > resource.resourceOffset ?
			task->desc.Width - resource.resourceOffset : 0;
		task->binding.descriptor.viewSize = availableBytes > 4096 ? 4096 : availableBytes;
		task->binding.descriptor.currentState = resource.currentState;
		task->binding.descriptor.hasCurrentState = resource.hasCurrentState;
	} else {
		task->source = binding.descriptor.resource;
		task->desc = binding.descriptor.resourceDesc;
	}

	task->sourceState = task->binding.descriptor.hasCurrentState ?
		static_cast<D3D12_RESOURCE_STATES>(task->binding.descriptor.currentState) :
		DX12DumpGuessDescriptorSourceState(task->binding);
	task->stateKnown = StateTrackingAllowsCopy(task->binding.descriptor);
	task->needsBarrier = ResourceNeedsBarrier(task->binding.descriptor);
	if (!task->stateKnown) {
		// State unknown: COMMON is the only universally valid before-state guess.
		task->sourceState = D3D12_RESOURCE_STATE_COMMON;
	}

	if (DX12DumpIsTextureResourceDimension(task->desc.Dimension)) {
		if (task->desc.SampleDesc.Count > 1) {
			task->skipNote = "multisampled_texture_not_supported";
			return false;
		}
		D3D12_RESOURCE_DESC copyDesc = task->desc;
		const UINT mipLevels = task->desc.MipLevels ? task->desc.MipLevels : 1;
		const UINT depthOrArray = task->desc.DepthOrArraySize ? task->desc.DepthOrArraySize : 1;
		UINT subresourceCount =
			task->desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ?
			mipLevels : mipLevels * depthOrArray;
		D3D12_FEATURE_DATA_FORMAT_INFO formatInfo = {};
		formatInfo.Format = task->desc.Format;
		if (SUCCEEDED(device->CheckFeatureSupport(
				D3D12_FEATURE_FORMAT_INFO, &formatInfo, sizeof(formatInfo))) &&
			formatInfo.PlaneCount > 1)
			subresourceCount *= formatInfo.PlaneCount;
		task->textureLayouts.resize(subresourceCount);
		task->textureRows.resize(subresourceCount);
		task->textureRowSizes.resize(subresourceCount);
		// D3D12 copies planar formats as extra subresources; RenderDoc does
		// the same PlaneCount expansion before GetCopyableFootprints. Skipping
		// invalid footprints avoids recording command lists that fail at Close.
		device->GetCopyableFootprints(&copyDesc, 0, subresourceCount, 0,
			task->textureLayouts.data(), task->textureRows.data(),
			task->textureRowSizes.data(), &task->copyBytes);
		if (task->copyBytes == UINT64_MAX) {
			task->skipNote = "copyable_footprints_failed";
			return false;
		}
		task->isTexture = true;
	} else if (task->desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
		task->sourceOffset = task->binding.descriptor.resourceOffset;
		task->copyBytes = task->binding.descriptor.viewSize ? task->binding.descriptor.viewSize : task->desc.Width;
		if (task->sourceOffset + task->copyBytes > task->desc.Width)
			task->copyBytes = task->desc.Width > task->sourceOffset ? task->desc.Width - task->sourceOffset : 0;
		task->isTexture = false;
	} else {
		task->skipNote = "unsupported_dimension";
		return false;
	}

	return true;
}

static bool FillIaDumpTask(const DX12FrameIaBufferBinding &buffer, DumpTask *task)
{
	if (!task || !buffer.resolved || !buffer.resource.resource ||
		!buffer.resource.hasResourceDesc)
		return false;

	task->sourceKind = DumpTaskSource::InputAssembler;
	task->iaBuffer = buffer;
	task->source = buffer.resource.resource;
	task->desc = buffer.resource.resourceDesc;
	if (task->desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER) {
		task->skipNote = "unsupported_dimension";
		return false;
	}
	// IA VB files must represent the full buffer view, not the draw-trimmed
	// vertex byte count, or indexed mesh reconstruction can reference vertices
	// that were never dumped. IB keeps the draw slice by design.
	if (buffer.role == IaBufferRole::VB) {
		task->sourceOffset = buffer.viewOffset;
		task->copyBytes = buffer.viewSize;
	} else {
		task->sourceOffset = buffer.resource.resourceOffset;
		task->copyBytes = buffer.size;
	}
	if (task->sourceOffset + task->copyBytes > task->desc.Width)
		task->copyBytes = task->desc.Width > task->sourceOffset ? task->desc.Width - task->sourceOffset : 0;
	task->sourceState = buffer.resource.hasCurrentState ?
		static_cast<D3D12_RESOURCE_STATES>(buffer.resource.currentState) :
		DX12DumpGuessIaBufferSourceState(buffer);
	task->stateKnown = StateTrackingAllowsCopy(buffer.resource);
	task->needsBarrier = ResourceNeedsBarrier(buffer.resource);
	if (!task->stateKnown)
		task->sourceState = D3D12_RESOURCE_STATE_COMMON;
	task->isTexture = false;
	return true;
}

static bool PrepareHuntingDescriptorTask(
	ID3D12Device *device, const DX12FrameResourceBinding &binding, DumpTask *task,
	UINT64 *frameReadbackBytes, UINT64 frameReadbackBudgetBytes)
{
	if (!FillDescriptorDumpTask(device, binding, task))
		return false;
	return CreateReadbackForTask(device, task, frameReadbackBytes, frameReadbackBudgetBytes);
}

static bool PrepareHuntingIaBufferTask(
	ID3D12Device *device, const DX12FrameIaBufferBinding &buffer, DumpTask *task,
	UINT64 *frameReadbackBytes, UINT64 frameReadbackBudgetBytes)
{
	if (!device || !FillIaDumpTask(buffer, task))
		return false;
	return CreateReadbackForTask(device, task, frameReadbackBytes, frameReadbackBudgetBytes);
}

bool DX12DumpPrepareHuntingMetadataTextureTask(
	ID3D12Device *device, const DX12DescriptorSummary &descriptor, DumpTask *task,
	UINT64 *frameReadbackBytes, UINT64 frameReadbackBudgetBytes)
{
	if (!device || !task || !DX12IsHuntingMode())
		return false;
	if (!descriptor.resource || !descriptor.hasResourceDesc ||
		!DX12DumpIsTextureResourceDimension(descriptor.resourceDesc.Dimension))
		return false;

	DX12FrameResourceBinding binding;
	binding.bindSpace = "hunting_metadata";
	binding.rangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	binding.descriptor = descriptor;
	binding.hasDescriptor = true;
	if (binding.descriptor.kind.empty())
		binding.descriptor.kind = DescriptorKind::SRV;

	if (!FillDescriptorDumpTask(device, binding, task))
		return false;
	// Hunting is resource-discovery oriented: metadata texture descriptors must
	// not depend on a draw event using the descriptor table in the captured frame.
	return CreateReadbackForTask(device, task, frameReadbackBytes, frameReadbackBudgetBytes);
}

bool DX12DumpPrepareHuntingResourceTextureTask(
	ID3D12Device *device, const DX12ResourceSummary &resource, DumpTask *task,
	UINT64 *frameReadbackBytes, UINT64 frameReadbackBudgetBytes)
{
	if (!device || !task || !DX12IsHuntingMode())
		return false;
	if (!resource.resource || !resource.hasResourceDesc ||
		!DX12DumpIsTextureResourceDimension(resource.resourceDesc.Dimension))
		return false;

	DX12DescriptorSummary descriptor;
	descriptor.kind = DescriptorKind::SRV;
	descriptor.resource = resource.resource;
	descriptor.resourceDesc = resource.resourceDesc;
	descriptor.hasResourceDesc = resource.hasResourceDesc;
	descriptor.resourceHeapType = resource.resourceHeapType;
	descriptor.hasResourceHeapType = resource.hasResourceHeapType;
	descriptor.gpuVirtualAddress = resource.gpuVirtualAddress;
	descriptor.viewSize = resource.size;
	descriptor.currentState = resource.currentState;
	descriptor.hasCurrentState = resource.hasCurrentState;

	// Created resources may never have had a tracked descriptor. Use the same
	// copy preparation path after projecting resource identity into a neutral
	// SRV summary, keeping dump failures isolated per texture.
	return DX12DumpPrepareHuntingMetadataTextureTask(
		device, descriptor, task, frameReadbackBytes, frameReadbackBudgetBytes);
}

static bool PrepareModDescriptorTask(
	ID3D12Device *device, const DX12FrameResourceBinding &binding, DumpTask *task,
	UINT64 *frameReadbackBytes, UINT64 frameReadbackBudgetBytes)
{
	if (!FillDescriptorDumpTask(device, binding, task))
		return false;
	if (DX12DumpGpuResourceCopyEnabled())
		return CreateReadbackForTask(device, task, frameReadbackBytes, frameReadbackBudgetBytes);
	if (ResourceIsCpuVisible(task->binding.descriptor))
		return PrepareDirectMapTask(task);
	task->skipNote = "gpu_copy_disabled";
	return false;
}

static bool PrepareModIaBufferTask(
	ID3D12Device *device, const DX12FrameIaBufferBinding &buffer, DumpTask *task,
	UINT64 *frameReadbackBytes, UINT64 frameReadbackBudgetBytes)
{
	if (!device || !FillIaDumpTask(buffer, task))
		return false;
	if (DX12DumpGpuResourceCopyEnabled())
		return CreateReadbackForTask(device, task, frameReadbackBytes, frameReadbackBudgetBytes);
	if (ResourceIsCpuVisible(task->iaBuffer.resource))
		return PrepareDirectMapTask(task);
	task->skipNote = "gpu_copy_disabled";
	return false;
}

bool DX12DumpPrepareDescriptorTaskForCurrentMode(
	ID3D12Device *device, const DX12FrameResourceBinding &binding, DumpTask *task,
	UINT64 *frameReadbackBytes, UINT64 frameReadbackBudgetBytes)
{
	if (DX12IsModMode())
		return PrepareModDescriptorTask(device, binding, task,
			frameReadbackBytes, frameReadbackBudgetBytes);
	if (DX12IsHuntingMode())
		return PrepareHuntingDescriptorTask(device, binding, task,
			frameReadbackBytes, frameReadbackBudgetBytes);
	return false;
}

bool DX12DumpPrepareIaBufferTaskForCurrentMode(
	ID3D12Device *device, const DX12FrameIaBufferBinding &buffer, DumpTask *task,
	UINT64 *frameReadbackBytes, UINT64 frameReadbackBudgetBytes)
{
	if (DX12IsModMode())
		return PrepareModIaBufferTask(device, buffer, task,
			frameReadbackBytes, frameReadbackBudgetBytes);
	if (DX12IsHuntingMode())
		return PrepareHuntingIaBufferTask(device, buffer, task,
			frameReadbackBytes, frameReadbackBudgetBytes);
	return false;
}

void DX12DumpReleaseTasks(std::vector<DumpTask> *tasks)
{
	if (!tasks)
		return;
	for (DumpTask &task : *tasks) {
		if (task.readback) {
			task.readback->Release();
			task.readback = nullptr;
		}
	}
}

size_t DX12DumpCountCopiedTasks(const std::vector<DumpTask> &tasks)
{
	size_t copied = 0;
	for (const DumpTask &task : tasks) {
		if (task.copied)
			copied++;
	}
	return copied;
}
