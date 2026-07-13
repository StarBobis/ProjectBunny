#pragma once

#include <d3d12.h>

#include <vector>

#include "DX12BindingTracker.hpp"
#include "DX12FrameAnalysis.hpp"
#include "DX12LogManager.hpp"
#include "DX12ResourceDumpCore.hpp"
#include "DX12ResourceDumpNaming.hpp"
#include "DX12ResourceDumpTypes.hpp"
#include "DX12ResourceDumpPolicy.hpp"
#include "DX12ResourceTracker.hpp"
#include "DX12ResourceStateTracker.hpp"
#include "DX12RuntimeState.hpp"
#include "DX12ResourceDumpSafetyUtils.hpp"

class DX12ResourceDumpPrepare
{
public:
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

static UINT DescriptorRangeTypeFromKind(const std::string &kind)
{
	if (kind == DescriptorKind::CBV || kind == DescriptorKind::CONSTANT_BUFFER_VIEW)
		return D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
	if (kind == DescriptorKind::UAV || kind == DescriptorKind::UNORDERED_ACCESS_VIEW)
		return D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
	return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
}

static bool ResourceNeedsBarrier(const DX12DescriptorSummary &descriptor)
{
	if (DX12ResourceStateTracker::HeapTypeAllowsDirectCopy(
			descriptor.resourceHeapType, descriptor.hasResourceHeapType))
		return false;
	return true;
}

static bool ResourceNeedsBarrier(const DX12BufferResourceSummary &resource)
{
	if (DX12ResourceStateTracker::HeapTypeAllowsDirectCopy(
			resource.resourceHeapType, resource.hasResourceHeapType))
		return false;
	return true;
}

static bool StateTrackingAllowsCopy(const DX12DescriptorSummary &descriptor)
{
	if (DX12ResourceStateTracker::HeapTypeAllowsDirectCopy(
			descriptor.resourceHeapType, descriptor.hasResourceHeapType))
		return true;
	return descriptor.hasCurrentState;
}

static bool StateTrackingAllowsCopy(const DX12BufferResourceSummary &resource)
{
	if (DX12ResourceStateTracker::HeapTypeAllowsDirectCopy(
			resource.resourceHeapType, resource.hasResourceHeapType))
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
	if (task->copyBytes > DX12ResourceDumpSafetyUtils::MaxSingleReadbackBytes()) {
		task->skipNote = "single_readback_too_large";
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

static bool RetainTaskSource(DumpTask *task)
{
	if (!task || !task->source)
		return false;
	if (task->sourceRetained)
		return true;
	if (!DX12RetainResource(task->source)) {
		task->skipNote = "source_retain_failed";
		return false;
	}
	task->sourceRetained = true;
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
	if (!device || !task || !DX12ResourceDumpPolicy::ShouldDumpBinding(binding))
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
		task->binding.descriptor.viewSize = availableBytes;
		task->binding.descriptor.currentState = resource.currentState;
		task->binding.descriptor.hasCurrentState = resource.hasCurrentState;
	} else {
		task->source = binding.descriptor.resource;
		task->desc = binding.descriptor.resourceDesc;
	}

	if (!task->source) {
		task->skipNote = "missing_resource_pointer";
		return false;
	}
	if (!DX12ResourceDumpSafetyUtils::ResourceDescLooksDumpable(task->desc)) {
		task->skipNote = "invalid_resource_desc";
		return false;
	}

	task->sourceState = task->binding.descriptor.hasCurrentState ?
		static_cast<D3D12_RESOURCE_STATES>(task->binding.descriptor.currentState) :
		DX12ResourceStateTracker::InferDescriptorSourceState(task->binding);
	task->stateKnown = StateTrackingAllowsCopy(task->binding.descriptor);
	task->needsBarrier = ResourceNeedsBarrier(task->binding.descriptor);
	if (!task->stateKnown) {
		// State unknown: COMMON is the only universally valid before-state guess.
		task->sourceState = D3D12_RESOURCE_STATE_COMMON;
	}

	if (DX12ResourceDumpPolicy::IsTextureResourceDimension(task->desc.Dimension)) {
		if (task->desc.SampleDesc.Count > 1) {
			task->skipNote = "multisampled_texture_not_supported";
			return false;
		}
		D3D12_RESOURCE_DESC copyDesc = task->desc;
		UINT subresourceCount = 0;
		if (!DX12ResourceDumpSafetyUtils::TextureSubresourceCountForDump(
				task->desc, &subresourceCount)) {
			task->skipNote = "invalid_texture_subresources";
			return false;
		}
		D3D12_FEATURE_DATA_FORMAT_INFO formatInfo = {};
		formatInfo.Format = task->desc.Format;
		__try {
			if (SUCCEEDED(device->CheckFeatureSupport(
					D3D12_FEATURE_FORMAT_INFO, &formatInfo, sizeof(formatInfo))) &&
				formatInfo.PlaneCount > 1) {
				const UINT64 planeSubresources =
					static_cast<UINT64>(subresourceCount) * formatInfo.PlaneCount;
				if (planeSubresources == 0 ||
						planeSubresources > DX12ResourceDumpSafetyUtils::MaxTextureSubresourcesForDump()) {
					task->skipNote = "too_many_texture_subresources";
					return false;
				}
				subresourceCount = static_cast<UINT>(planeSubresources);
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			task->skipNote = "format_info_exception";
			return false;
		}
		task->textureLayouts.resize(subresourceCount);
		task->textureRows.resize(subresourceCount);
		task->textureRowSizes.resize(subresourceCount);
		// D3D12 copies planar formats as extra subresources; RenderDoc does
		// the same PlaneCount expansion before GetCopyableFootprints. Skipping
		// invalid footprints avoids recording command lists that fail at Close.
		__try {
			device->GetCopyableFootprints(&copyDesc, 0, subresourceCount, 0,
				task->textureLayouts.data(), task->textureRows.data(),
				task->textureRowSizes.data(), &task->copyBytes);
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			task->skipNote = "copyable_footprints_exception";
			return false;
		}
		if (task->copyBytes == UINT64_MAX) {
			task->skipNote = "copyable_footprints_failed";
			return false;
		}
		if (task->copyBytes > DX12ResourceDumpSafetyUtils::MaxSingleReadbackBytes()) {
			task->skipNote = "single_readback_too_large";
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
	if (!task)
		return false;

	DX12BufferResourceSummary resource = buffer.resource;
	bool resolved = buffer.resolved;
	if (!resolved || !resource.resource || !resource.hasResourceDesc) {
		resolved = DX12BindingFrameExport::ResolveIaBufferResourceSummary(
			buffer, &resource);
	}
	if (!resolved || !resource.resource || !resource.hasResourceDesc) {
		task->skipNote = "unresolved_gpu_va";
		return false;
	}

	task->sourceKind = DumpTaskSource::InputAssembler;
	task->iaBuffer = buffer;
	task->iaBuffer.resource = resource;
	task->iaBuffer.resolved = true;
	task->source = resource.resource;
	task->desc = resource.resourceDesc;
	if (task->desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER) {
		task->skipNote = "unsupported_dimension";
		return false;
	}
	// IA dumps now use full view granularity for both VB and IB. Draw-specific
	// slicing stays in log.jsonl so the same .buf can serve multiple draws.
	task->sourceOffset = task->iaBuffer.resource.resourceOffset;
	task->copyBytes = buffer.viewSize;
	if (task->sourceOffset + task->copyBytes > task->desc.Width)
		task->copyBytes = task->desc.Width > task->sourceOffset ? task->desc.Width - task->sourceOffset : 0;
	task->sourceState = task->iaBuffer.resource.hasCurrentState ?
		static_cast<D3D12_RESOURCE_STATES>(task->iaBuffer.resource.currentState) :
		DX12ResourceStateTracker::InferIaBufferSourceState(task->iaBuffer);
	task->stateKnown = StateTrackingAllowsCopy(task->iaBuffer.resource);
	task->needsBarrier = ResourceNeedsBarrier(task->iaBuffer.resource);
	if (!task->stateKnown)
		task->sourceState = D3D12_RESOURCE_STATE_COMMON;
	if (task->copyBytes == 0) {
		task->skipNote = "copy_size_zero";
		return false;
	}
	task->isTexture = false;
	return true;
}

static bool PrepareHuntingDescriptorTask(
	ID3D12Device *device, const DX12FrameResourceBinding &binding, DumpTask *task,
	UINT64 *frameReadbackBytes, UINT64 frameReadbackBudgetBytes)
{
	if (!FillDescriptorDumpTask(device, binding, task))
		return false;
	if (!RetainTaskSource(task))
		return false;
	return CreateReadbackForTask(device, task, frameReadbackBytes, frameReadbackBudgetBytes);
}

static bool PrepareHuntingIaBufferTask(
	ID3D12Device *device, const DX12FrameIaBufferBinding &buffer, DumpTask *task,
	UINT64 *frameReadbackBytes, UINT64 frameReadbackBudgetBytes)
{
	if (!device || !FillIaDumpTask(buffer, task))
		return false;
	if (!RetainTaskSource(task))
		return false;
	return CreateReadbackForTask(device, task, frameReadbackBytes, frameReadbackBudgetBytes);
}

static bool PrepareHuntingMetadataTextureTask(
	ID3D12Device *device, const DX12DescriptorSummary &descriptor, DumpTask *task,
	UINT64 *frameReadbackBytes, UINT64 frameReadbackBudgetBytes)
{
	if (!device || !task)
		return false;
	if (!descriptor.resource || !descriptor.hasResourceDesc ||
		!DX12ResourceDumpPolicy::IsTextureResourceDimension(descriptor.resourceDesc.Dimension))
		return false;
	if (!DX12ResourceDumpSafetyUtils::DescriptorResourceDescLooksDumpable(descriptor)) {
		task->skipNote = "invalid_metadata_texture_desc";
		return false;
	}

	DX12FrameResourceBinding binding;
	binding.bindSpace = "hunting_metadata";
	binding.rangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	binding.descriptor = descriptor;
	binding.hasDescriptor = true;
	if (binding.descriptor.kind.empty())
		binding.descriptor.kind = DescriptorKind::SRV;

	if (!FillDescriptorDumpTask(device, binding, task))
		return false;
	if (!RetainTaskSource(task))
		return false;
	// Hunting is resource-discovery oriented: metadata texture descriptors must
	// not depend on a draw event using the descriptor table in the captured frame.
	return CreateReadbackForTask(device, task, frameReadbackBytes, frameReadbackBudgetBytes);
}

static bool PrepareHuntingMetadataBufferTask(
	ID3D12Device *device, const DX12DescriptorSummary &descriptor, DumpTask *task,
	UINT64 *frameReadbackBytes, UINT64 frameReadbackBudgetBytes)
{
	if (!device || !task)
		return false;
	if (!descriptor.resource || !descriptor.hasResourceDesc ||
		descriptor.resourceDesc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER)
		return false;
	if (!DX12ResourceDumpSafetyUtils::DescriptorResourceDescLooksDumpable(descriptor)) {
		task->skipNote = "invalid_metadata_buffer_desc";
		return false;
	}

	DX12FrameResourceBinding binding;
	binding.bindSpace = "hunting_metadata";
	binding.rangeType = DescriptorRangeTypeFromKind(descriptor.kind);
	binding.descriptor = descriptor;
	binding.hasDescriptor = true;
	if (binding.descriptor.kind.empty())
		binding.descriptor.kind = DescriptorKind::SRV;

	if (!FillDescriptorDumpTask(device, binding, task))
		return false;
	if (!RetainTaskSource(task))
		return false;
	return CreateReadbackForTask(device, task, frameReadbackBytes, frameReadbackBudgetBytes);
}

static bool PrepareHuntingResourceTask(
	ID3D12Device *device, const DX12ResourceSummary &resource, DumpTask *task,
	UINT64 *frameReadbackBytes, UINT64 frameReadbackBudgetBytes)
{
	if (!device || !task)
		return false;
	if (!resource.resource || !resource.hasResourceDesc ||
		(resource.resourceDesc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER &&
		 !DX12ResourceDumpPolicy::IsTextureResourceDimension(resource.resourceDesc.Dimension)))
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
	descriptor.stateTrackedByBarrier = resource.stateTrackedByBarrier;

	if (!DX12ResourceDumpSafetyUtils::ResourceStateAllowsLiveDiscoveryCopy(resource)) {
		task->sourceKind = DumpTaskSource::Resource;
		task->binding.bindSpace = "hunting_resource";
		task->binding.rangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		task->binding.descriptor = descriptor;
		task->binding.hasDescriptor = true;
		task->skipNote = DX12ResourceDumpSafetyUtils::LiveDiscoveryCopySkipReason(resource);
		return false;
	}

	// Created resources may never have had a tracked descriptor. Project them
	// into a neutral SRV summary so persistent textures and buffers are dumped
	// from the live resource inventory, independent of current-frame bindings.
	if (resource.resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
		return PrepareHuntingMetadataBufferTask(
			device, descriptor, task, frameReadbackBytes, frameReadbackBudgetBytes);
	return PrepareHuntingMetadataTextureTask(
		device, descriptor, task, frameReadbackBytes, frameReadbackBudgetBytes);
}

static bool PrepareModDescriptorTask(
	ID3D12Device *device, const DX12FrameResourceBinding &binding, DumpTask *task,
	UINT64 *frameReadbackBytes, UINT64 frameReadbackBudgetBytes)
{
	if (!FillDescriptorDumpTask(device, binding, task))
		return false;
	if (!RetainTaskSource(task))
		return false;
	return CreateReadbackForTask(device, task, frameReadbackBytes, frameReadbackBudgetBytes);
}

static bool PrepareModIaBufferTask(
	ID3D12Device *device, const DX12FrameIaBufferBinding &buffer, DumpTask *task,
	UINT64 *frameReadbackBytes, UINT64 frameReadbackBudgetBytes)
{
	if (!device || !FillIaDumpTask(buffer, task))
		return false;
	if (!RetainTaskSource(task))
		return false;
	return CreateReadbackForTask(device, task, frameReadbackBytes, frameReadbackBudgetBytes);
}

static bool PrepareDescriptorTaskForCurrentMode(
	ID3D12Device *device, const DX12FrameResourceBinding &binding, DumpTask *task,
	UINT64 *frameReadbackBytes, UINT64 frameReadbackBudgetBytes)
{
	if (DX12RuntimeState::IsModMode())
		return PrepareModDescriptorTask(device, binding, task,
			frameReadbackBytes, frameReadbackBudgetBytes);
	if (DX12RuntimeState::IsHuntingMode())
		return PrepareHuntingDescriptorTask(device, binding, task,
			frameReadbackBytes, frameReadbackBudgetBytes);
	return false;
}

static bool PrepareIaBufferTaskForCurrentMode(
	ID3D12Device *device, const DX12FrameIaBufferBinding &buffer, DumpTask *task,
	UINT64 *frameReadbackBytes, UINT64 frameReadbackBudgetBytes)
{
	if (DX12RuntimeState::IsModMode())
		return PrepareModIaBufferTask(device, buffer, task,
			frameReadbackBytes, frameReadbackBudgetBytes);
	if (DX12RuntimeState::IsHuntingMode())
		return PrepareHuntingIaBufferTask(device, buffer, task,
			frameReadbackBytes, frameReadbackBudgetBytes);
	return false;
}

static void ReleaseTask(DumpTask *task)
{
	if (!task)
		return;
	if (task->keepReadbackAlive)
		return;
	if (task->sourceRetained && task->source) {
		DX12ReleaseRetainedResource(task->source);
		task->sourceRetained = false;
	}
	if (task->readback) {
		task->readback->Release();
		task->readback = nullptr;
	}
}

static void ReleaseTasks(std::vector<DumpTask> *tasks)
{
	if (!tasks)
		return;
	for (DumpTask &task : *tasks)
		ReleaseTask(&task);
}

static size_t CountCopiedTasks(const std::vector<DumpTask> &tasks)
{
	size_t copied = 0;
	for (const DumpTask &task : tasks) {
		if (task.copied)
			copied++;
	}
	return copied;
}



};

class DX12ResourceDumpPrepareGuardUtils
{
public:
static bool TryPrepareHuntingMetadataTask(
	ID3D12Device *device, const DX12DescriptorSummary &descriptor, bool isTexture,
	DumpTask *task, UINT64 *frameReadbackBytes, UINT64 frameReadbackBudgetBytes,
	UINT64 descriptorIndex)
{
	if (!task)
		return false;
	bool prepared = false;
	__try {
		prepared = isTexture ?
			DX12ResourceDumpPrepare::PrepareHuntingMetadataTextureTask(
				device, descriptor, task, frameReadbackBytes, frameReadbackBudgetBytes) :
			DX12ResourceDumpPrepare::PrepareHuntingMetadataBufferTask(
				device, descriptor, task, frameReadbackBytes, frameReadbackBudgetBytes);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		DX12ResourceDumpPrepare::ReleaseTask(task);
		task->ready = false;
		task->copied = false;
		task->readback = nullptr;
		task->sourceRetained = false;
		task->skipNote = isTexture ?
			"metadata_texture_prepare_exception" :
			"metadata_buffer_prepare_exception";
		DX12FrameAnalysis::LogJsonFunc("ResourcePrepareTaskFailed",
			"\"exception\":\"0x%lx\",\"stage\":\"metadata_prepare\",\"descriptorIndex\":%llu,\"kind\":\"%s\",\"resource\":\"%p\",\"dimension\":%u,\"width\":%llu,\"height\":%u,\"format\":%u,\"note\":\"%s\"",
			GetExceptionCode(),
			static_cast<unsigned long long>(descriptorIndex),
			descriptor.kind.c_str(),
			descriptor.resource,
			descriptor.hasResourceDesc ? static_cast<UINT>(descriptor.resourceDesc.Dimension) : 0,
			descriptor.hasResourceDesc ? static_cast<unsigned long long>(descriptor.resourceDesc.Width) : 0,
			descriptor.hasResourceDesc ? static_cast<UINT>(descriptor.resourceDesc.Height) : 0,
			descriptor.hasResourceDesc ? static_cast<UINT>(descriptor.resourceDesc.Format) : 0,
			task->skipNote);
		prepared = false;
	}
	return prepared;
}

static bool TryBuildFileName(const DumpTask &task, wchar_t *fileName, size_t fileNameCount)
{
	if (!fileName || fileNameCount == 0)
		return false;
	fileName[0] = L'\0';
	bool ok = false;
	__try {
		DX12ResourceDumpNaming::BuildFileName(task, fileName, fileNameCount);
		ok = fileName[0] != L'\0';
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		DX12FrameAnalysis::LogJsonFunc("ResourceDumpFileNameFailed",
			"\"exception\":\"0x%lx\",\"taskSource\":\"%s\",\"source\":\"%p\",\"texture\":%u,\"bytes\":%llu",
			GetExceptionCode(),
			DX12ResourceDumpCore::DumpTaskSourceName(task.sourceKind),
			task.source,
			task.isTexture ? 1 : 0,
			static_cast<unsigned long long>(task.copyBytes));
		ok = false;
	}
	return ok;
}
};

