#pragma once

#include "DX12FeatureInterfaces.hpp"

// Consolidated DirectX 12 interface layer.
// Sections retain their original file names in #line diagnostics.

// ---- DX12ResourceDumpPrepare.hpp ----
#line 1 "DX12ResourceDumpPrepare.hpp"
#include <d3d12.h>

#include <vector>


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

// ---- DX12ResourceDumpGpuCopy.hpp ----
#line 1 "DX12ResourceDumpGpuCopy.hpp"
#include <Windows.h>
#include <stdint.h>
#include <string.h>

#include <vector>


class DX12ResourceDumpGpuCopy
{
public:
static DWORD CopyFenceTimeoutMs()
{
	return 2000;
}

static const char *WaitResultName(DWORD waitResult)
{
	switch (waitResult) {
	case WAIT_OBJECT_0:
		return "signaled";
	case WAIT_TIMEOUT:
		return "timeout";
	case WAIT_FAILED:
		return "wait_failed";
	default:
		return "unexpected_wait_result";
	}
}

static bool DeviceRemoved(ID3D12Device *device, HRESULT *hrOut = nullptr)
{
	HRESULT hr = device ? device->GetDeviceRemovedReason() : E_POINTER;
	if (hrOut)
		*hrOut = hr;
	return FAILED(hr);
}

static void WriteResourceBarrier(
	ID3D12GraphicsCommandList *commandList, ID3D12Resource *resource,
	D3D12_RESOURCE_STATES beforeState, D3D12_RESOURCE_STATES afterState)
{
	if (!commandList || !resource || beforeState == afterState)
		return;

	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = resource;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = beforeState;
	barrier.Transition.StateAfter = afterState;
	commandList->ResourceBarrier(1, &barrier);
}

static bool WaitForFence(ID3D12CommandQueue *queue, ID3D12Device *device, DWORD *waitResultOut = nullptr)
{
	ID3D12Fence *fence = nullptr;
	HRESULT hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
	if (FAILED(hr) || !fence)
		return false;

	HANDLE eventHandle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	if (!eventHandle) {
		fence->Release();
		return false;
	}

	DWORD waitResult = WAIT_OBJECT_0;
	DWORD waitError = 0;
	UINT64 completedValue = 0;
	UINT64 completedBeforeSignal = DX12OuterTimingProbeUtils::FenceCompletedValue(fence);
	LARGE_INTEGER signalBegin = DX12OuterTimingProbeUtils::Now();
	hr = queue->Signal(fence, 1);
	UINT64 completedAfterSignal = DX12OuterTimingProbeUtils::FenceCompletedValue(fence);
	DX12OuterTimingProbeUtils::LogQueueFenceSlice(
		"Signal",
		"dump_copy_signal",
		queue,
		fence,
		1,
		completedBeforeSignal,
		completedAfterSignal,
		hr,
		DX12OuterTimingProbeUtils::ElapsedUs(signalBegin));
	if (SUCCEEDED(hr) && completedAfterSignal < 1) {
		LARGE_INTEGER setEventBegin = DX12OuterTimingProbeUtils::Now();
		hr = fence->SetEventOnCompletion(1, eventHandle);
		UINT64 completedAfterSetEvent = DX12OuterTimingProbeUtils::FenceCompletedValue(fence);
		DX12OuterTimingProbeUtils::LogFenceSlice(
			"SetEventOnCompletion",
			"dump_copy_set_event",
			fence,
			1,
			completedAfterSignal,
			completedAfterSetEvent,
			hr,
			DX12OuterTimingProbeUtils::ElapsedUs(setEventBegin));
		if (SUCCEEDED(hr)) {
			LARGE_INTEGER waitBegin = DX12OuterTimingProbeUtils::Now();
			waitResult = WaitForSingleObject(eventHandle, CopyFenceTimeoutMs());
			if (waitResult == WAIT_FAILED)
				waitError = GetLastError();
			DX12OuterTimingProbeUtils::LogEventWaitSlice(
				"WaitForSingleObject",
				"dump_copy_wait_event",
				eventHandle,
				waitResult,
				waitError,
				CopyFenceTimeoutMs(),
				DX12OuterTimingProbeUtils::ElapsedUs(waitBegin));
		}
	}
	LARGE_INTEGER completedBegin = DX12OuterTimingProbeUtils::Now();
	completedValue = DX12OuterTimingProbeUtils::FenceCompletedValue(fence);
	DX12OuterTimingProbeUtils::LogFenceSlice(
		"GetCompletedValue",
		"dump_copy_final_completed",
		fence,
		1,
		completedAfterSignal,
		completedValue,
		S_OK,
		DX12OuterTimingProbeUtils::ElapsedUs(completedBegin));

	CloseHandle(eventHandle);
	fence->Release();
	if (waitResultOut)
		*waitResultOut = waitResult;

	const bool completed = completedValue != UINT64_MAX && completedValue >= 1;
	const bool ok = SUCCEEDED(hr) && waitResult == WAIT_OBJECT_0 && completed;
	if (!ok) {
		HRESULT removedHr = S_OK;
		DeviceRemoved(device, &removedHr);
		DX12FrameAnalysis::LogJsonFunc("ResourceCopyFenceWaitFailed",
			"\"hr\":\"0x%lx\",\"deviceHr\":\"0x%lx\",\"wait\":\"%s\",\"waitResult\":%lu,\"waitError\":%lu,\"completed\":%llu,\"timeoutMs\":%lu",
			hr, removedHr, WaitResultName(waitResult), waitResult, waitError,
			static_cast<unsigned long long>(completedValue),
			CopyFenceTimeoutMs());
	}
	return ok;
}


static bool ExecuteCopyTask(
	ID3D12Device *device, ID3D12CommandQueue *queue, DumpTask *task)
{
	if (!device || !queue || !task || !task->ready || !task->readback)
		return false;
	task->copied = false;

	ID3D12CommandAllocator *allocator = nullptr;
	ID3D12GraphicsCommandList *commandList = nullptr;
	HRESULT hr = S_OK;
	if (DX12ResourceDumpGpuCopy::DeviceRemoved(device, &hr)) {
		task->skipNote = "device_removed";
		DX12FrameAnalysis::LogJsonFunc("ResourceCopySkipped",
			"\"reason\":\"device_removed\",\"hr\":\"0x%lx\"", hr);
		return false;
	}

	hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));
	if (SUCCEEDED(hr))
		hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, IID_PPV_ARGS(&commandList));
	if (FAILED(hr) || !allocator || !commandList) {
		DX12FrameAnalysis::LogJsonFunc("ResourceCopySetupFailed",
			"\"hr\":\"0x%lx\",\"source\":\"%p\",\"texture\":%u,\"offset\":%llu,\"bytes\":%llu",
			hr, task->source, task->isTexture ? 1 : 0,
			static_cast<unsigned long long>(task->sourceOffset),
			static_cast<unsigned long long>(task->copyBytes));
		if (commandList)
			commandList->Release();
		if (allocator)
			allocator->Release();
		return false;
	}

	__try {
		if (task->needsBarrier)
			DX12ResourceDumpGpuCopy::WriteResourceBarrier(commandList, task->source, task->sourceState, D3D12_RESOURCE_STATE_COPY_SOURCE);

		if (task->isTexture) {
			for (UINT subresource = 0; subresource < task->textureLayouts.size(); ++subresource) {
				D3D12_TEXTURE_COPY_LOCATION src = {};
				src.pResource = task->source;
				src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
				src.SubresourceIndex = subresource;
				D3D12_TEXTURE_COPY_LOCATION dst = {};
				dst.pResource = task->readback;
				dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
				dst.PlacedFootprint = task->textureLayouts[subresource];
				commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
			}
		} else {
			commandList->CopyBufferRegion(task->readback, 0, task->source,
				task->sourceOffset, task->copyBytes);
		}

		if (task->needsBarrier)
			DX12ResourceDumpGpuCopy::WriteResourceBarrier(commandList, task->source, D3D12_RESOURCE_STATE_COPY_SOURCE, task->sourceState);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		task->skipNote = "copy_record_exception";
		DX12FrameAnalysis::LogJsonFunc("ResourceCopyTaskFailed",
			"\"exception\":\"0x%lx\",\"source\":\"%p\",\"texture\":%u,\"offset\":%llu,\"bytes\":%llu,\"state\":\"0x%x\",\"note\":\"copy_record_exception\"",
			GetExceptionCode(), task->source, task->isTexture ? 1 : 0,
			static_cast<unsigned long long>(task->sourceOffset),
			static_cast<unsigned long long>(task->copyBytes),
			static_cast<UINT>(task->sourceState));
		commandList->Release();
		allocator->Release();
		return false;
	}

	hr = commandList->Close();
	if (FAILED(hr)) {
		task->skipNote = "copy_close_failed";
		DX12FrameAnalysis::LogJsonFunc("ResourceCopyTaskFailed",
			"\"hr\":\"0x%lx\",\"source\":\"%p\",\"texture\":%u,\"offset\":%llu,\"bytes\":%llu,\"state\":\"0x%x\",\"note\":\"copy_close_failed\"",
			hr, task->source, task->isTexture ? 1 : 0,
			static_cast<unsigned long long>(task->sourceOffset),
			static_cast<unsigned long long>(task->copyBytes),
			static_cast<UINT>(task->sourceState));
		commandList->Release();
		allocator->Release();
		return false;
	}
	ID3D12CommandList *lists[] = { commandList };
	bool ok = false;
	DWORD waitResult = WAIT_FAILED;
	__try {
		LARGE_INTEGER executeBegin = DX12OuterTimingProbeUtils::Now();
		queue->ExecuteCommandLists(1, lists);
		DX12OuterTimingProbeUtils::LogQueueExecuteSlice(
			"dump_copy_execute",
			queue,
			1,
			DX12OuterTimingProbeUtils::ElapsedUs(executeBegin));
		ok = DX12ResourceDumpGpuCopy::WaitForFence(queue, device, &waitResult);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		task->skipNote = "copy_execute_exception";
		DX12FrameAnalysis::LogJsonFunc("ResourceCopyTaskFailed",
			"\"exception\":\"0x%lx\",\"source\":\"%p\",\"texture\":%u,\"offset\":%llu,\"bytes\":%llu,\"state\":\"0x%x\",\"note\":\"copy_execute_exception\"",
			GetExceptionCode(), task->source, task->isTexture ? 1 : 0,
			static_cast<unsigned long long>(task->sourceOffset),
			static_cast<unsigned long long>(task->copyBytes),
			static_cast<UINT>(task->sourceState));
		ok = false;
	}

	if (!ok) {
		HRESULT removedHr = S_OK;
		const bool deviceRemoved = DX12ResourceDumpGpuCopy::DeviceRemoved(device, &removedHr);
		if (deviceRemoved)
			task->skipNote = "device_removed";
		else if (!task->skipNote && waitResult == WAIT_TIMEOUT)
			task->skipNote = "copy_wait_timeout";
		else if (!task->skipNote)
			task->skipNote = "copy_execute_failed";
		DX12FrameAnalysis::LogJsonFunc("ResourceCopyTaskFailed",
			"\"source\":\"%p\",\"taskSource\":\"%s\",\"bind\":\"%s\",\"texture\":%u,\"width\":%llu,\"height\":%u,\"depthOrArray\":%u,\"mips\":%u,\"format\":%u,\"offset\":%llu,\"bytes\":%llu,\"state\":\"0x%x\",\"stateKnown\":%u,\"stateTrackedByBarrier\":%u,\"note\":\"%s\",\"deviceHr\":\"0x%lx\",\"wait\":\"%s\"",
			task->source,
			DX12ResourceDumpCore::DumpTaskSourceName(task->sourceKind),
			task->binding.bindSpace.c_str(),
			task->isTexture ? 1 : 0,
			static_cast<unsigned long long>(task->desc.Width),
			static_cast<UINT>(task->desc.Height),
			static_cast<UINT>(task->desc.DepthOrArraySize),
			static_cast<UINT>(task->desc.MipLevels),
			static_cast<UINT>(task->desc.Format),
			static_cast<unsigned long long>(task->sourceOffset),
			static_cast<unsigned long long>(task->copyBytes),
			static_cast<UINT>(task->sourceState),
			task->stateKnown ? 1 : 0,
			task->binding.descriptor.stateTrackedByBarrier ? 1 : 0,
			task->skipNote ? task->skipNote : "copy_execute_failed",
			removedHr,
			WaitResultName(waitResult));
		if (deviceRemoved) {
			commandList->Release();
			allocator->Release();
		} else {
			task->keepReadbackAlive = true;
			DX12FrameAnalysis::LogJsonFunc("ResourceCopyObjectsRetained",
				"\"reason\":\"gpu_work_not_completed\",\"source\":\"%p\",\"bytes\":%llu",
				task->source, static_cast<unsigned long long>(task->copyBytes));
		}
		return false;
	}

	commandList->Release();
	allocator->Release();
	task->copied = true;
	task->skipNote = nullptr;
	return true;
}

static size_t ExecuteReadbackCopyTasks(
	ID3D12Device *device, ID3D12CommandQueue *queue, std::vector<DumpTask> *tasks,
	bool *readbackOkOut)
{
	if (readbackOkOut)
		*readbackOkOut = true;
	if (!tasks)
		return 0;

	size_t eligible = 0;
	size_t copied = 0;
	for (size_t i = 0; i < tasks->size(); ++i) {
		DumpTask &task = (*tasks)[i];
		if (task.directMap || !task.ready || !task.readback)
			continue;
		eligible++;
		if (DX12ResourceDumpGpuCopy::ExecuteCopyTask(device, queue, &task)) {
			copied++;
			continue;
		}

		HRESULT removedHr = S_OK;
		const bool deviceRemoved = DX12ResourceDumpGpuCopy::DeviceRemoved(device, &removedHr);
		const bool timedOut = task.skipNote && !strcmp(task.skipNote, "copy_wait_timeout");
		if (deviceRemoved || timedOut) {
			const char *abortNote = deviceRemoved ?
				"copy_aborted_device_removed" : "copy_aborted_wait_timeout";
			for (size_t j = i + 1; j < tasks->size(); ++j) {
				DumpTask &pending = (*tasks)[j];
				if (!pending.directMap && pending.ready && pending.readback)
					pending.skipNote = abortNote;
			}
			DX12FrameAnalysis::LogJsonFunc("ResourceCopyBatchAborted",
				"\"reason\":\"%s\",\"deviceHr\":\"0x%lx\",\"copied\":%zu,\"eligibleSeen\":%zu,\"tasks\":%zu",
				abortNote, removedHr, copied, eligible, tasks->size());
			break;
		}
	}

	if (eligible == 0)
		return DX12ResourceDumpPrepare::CountCopiedTasks(*tasks);

	if (readbackOkOut)
		*readbackOkOut = eligible == copied;
	DX12FrameAnalysis::LogJsonFunc("ResourceCopyResultDetail",
		"\"mode\":\"per_resource\",\"eligible\":%zu,\"copied\":%zu,\"failed\":%zu",
		eligible, copied, eligible >= copied ? eligible - copied : 0);
	return DX12ResourceDumpPrepare::CountCopiedTasks(*tasks);
}


};

// ---- DX12ResourceDumpIndexWriter.hpp ----
#line 1 "DX12ResourceDumpIndexWriter.hpp"
#include <Shlwapi.h>
#include <Windows.h>
#include <stdio.h>


class DX12ResourceDumpIndexWriter
{
public:
static void WriteIndexRow(
	FILE *index, const char *status, const DumpTask &task, const D3D12_RESOURCE_DESC &desc,
	UINT64 sourceOffset, UINT64 copyBytes, D3D12_RESOURCE_STATES sourceState,
	bool hasCurrentState, const wchar_t *semanticName, const wchar_t *filePath, const char *note)
{
	(void)index;
	(void)status;
	(void)semanticName;
	(void)note;
	if (task.sourceKind == DumpTaskSource::InputAssembler) {
		DX12FrameAnalysisManifest::WriteIaBinding(
			task.iaBuffer, desc, sourceOffset, copyBytes, sourceState,
			hasCurrentState, filePath);
		return;
	}

	DX12FrameAnalysisManifest::WriteResourceBinding(
		task.binding, desc, sourceOffset, copyBytes, sourceState,
		hasCurrentState, filePath, status, note);
}


};

// ---- DX12ResourceDump.hpp ----
#line 1 "DX12ResourceDump.hpp"
#include <Windows.h>

#include <string>
#include <string.h>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>


class DX12ResourceDump
{
public:
static bool IsDiscoveryDuplicateTask(const DumpTask &task)
{
	if (task.sourceKind == DumpTaskSource::Resource)
		return task.binding.bindSpace == "hunting_resource";
	if (task.sourceKind != DumpTaskSource::Descriptor)
		return false;
	return task.binding.bindSpace == "hunting_metadata" ||
		task.binding.bindSpace == "hunting_resource";
}

static bool IsComputeCbvSrvUavBinding(const DX12FrameResourceBinding &binding)
{
	return binding.bindSpace == "compute_cbv_srv_uav";
}

static bool IsComputeCbvSrvUavBufferBinding(const DX12FrameResourceBinding &binding)
{
	if (!DX12ResourceDump::IsComputeCbvSrvUavBinding(binding))
		return false;
	return binding.hasDescriptor &&
		binding.descriptor.hasResourceDesc &&
		binding.descriptor.resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER;
}

static bool IsComputeCbvSrvUavBufferTask(const DumpTask &task)
{
	return task.sourceKind == DumpTaskSource::Descriptor &&
		DX12ResourceDump::IsComputeCbvSrvUavBufferBinding(task.binding);
}

static DX12FrameAnalysisDumpResult DumpCurrentFrameResources(const wchar_t *dir)
{
	DX12FrameAnalysisDumpResult result;
	if (!dir)
	{
		result.reason = "missing_directory";
		return result;
	}

	std::vector<DX12FrameResourceBinding> bindings;
	DX12BindingResourceExport::GetCurrentFrameResourceBindings(&bindings);
	std::vector<DX12FrameIaBufferBinding> iaBuffers;
	DX12BindingFrameExport::GetCurrentFrameIaBuffersWithResourceBindings(&iaBuffers, bindings);
	std::vector<DX12FrameOutputBinding> outputBindings;
	DX12BindingResourceExport::GetCurrentFrameOutputBindings(&outputBindings);

	wchar_t resourceDir[MAX_PATH];
	wcsncpy_s(resourceDir, dir, _TRUNCATE);
	if (!DX12ResourceDumpCore::EnsureDirectory(resourceDir)) {
		DX12FrameAnalysis::LogJsonFunc("ResourceDump",
			"\"status\":\"failed\",\"reason\":\"resource_dir_create_failed\",\"error\":%lu",
			GetLastError());
		result.failed = 1;
		result.reason = "resource_dir_create_failed";
		return result;
	}
	wchar_t dedupeDir[MAX_PATH];
	DX12ResourceDumpCore::GetDedupedDirectory(dir, dedupeDir, ARRAYSIZE(dedupeDir));

	const UINT64 frameReadbackBudget = DX12ResourceDumpCore::FrameReadbackBudgetBytes();
	UINT64 frameReadbackBytes = 0;
	const char *dumpMode = DX12RuntimeState::IsModMode() ? "Mod" : "Hunting";
	DX12FrameAnalysis::LogJsonFunc("ResourceDump",
		"\"mode\":\"%s\",\"bindings\":%zu,\"ia\":%zu,\"outputs\":%zu,\"frameReadbackBudget\":%llu,\"unsafeCopy\":%u,\"gpuCopy\":%u,\"stateTracking\":1",
		dumpMode,
		bindings.size(), iaBuffers.size(), outputBindings.size(),
		static_cast<unsigned long long>(frameReadbackBudget),
		1,
		1);
	DX12LogManager::JsonFunc("DX12ResourceDump",
		"\"bindings\":%zu,\"ia\":%zu,\"outputs\":%zu,\"frameReadbackBudget\":%llu",
		bindings.size(), iaBuffers.size(), outputBindings.size(),
		static_cast<unsigned long long>(frameReadbackBudget));

	ID3D12CommandQueue *queue = DX12CommandQueueState::Acquire();
	ID3D12Device *device = nullptr;
	if (!queue || FAILED(queue->GetDevice(IID_PPV_ARGS(&device))) || !device) {
		DX12FrameAnalysis::LogJsonFunc("ResourceDump",
			"\"status\":\"failed\",\"reason\":\"missing_command_queue\"");
		if (queue)
			queue->Release();
		result.failed = 1;
		result.reason = "missing_command_queue";
		return result;
	}

	std::vector<DumpTask> tasks;
	std::vector<DumpTask> duplicateTasks;
	std::vector<DumpTask> skippedTasks;
	std::unordered_map<std::string, size_t> canonicalTaskByKey;
	UINT duplicates = 0;
	UINT candidateCbv = 0;
	UINT candidateSrv = 0;
	UINT candidateUav = 0;
	UINT candidateTextures = 0;
	UINT candidateRtv = 0;
	UINT candidateDsv = 0;
	UINT metadataTextureCandidates = 0;
	UINT metadataBufferCandidates = 0;
	UINT metadataCbvBufferCandidates = 0;
	UINT metadataSrvBufferCandidates = 0;
	UINT metadataUavBufferCandidates = 0;
	UINT metadataMissingResource = 0;
	UINT metadataMissingResourceDesc = 0;
	UINT metadataUnsupportedKindOrDimension = 0;
	UINT metadataPrepareFailed = 0;
	UINT metadataBufferCopiesQueued = 0;
	UINT metadataBufferCopyBudgetSkipped = 0;
	UINT64 metadataBufferCopyBytesQueued = 0;
	UINT64 metadataTextureCopyBytesQueued = 0;
	UINT metadataCbvBufferCopiesQueued = 0;
	UINT64 metadataCbvBufferCopyBytesQueued = 0;
	UINT metadataSrvUavBufferCopiesQueued = 0;
	UINT64 metadataSrvUavBufferCopyBytesQueued = 0;
	UINT bindingTasksQueued = 0;
	UINT iaTasksQueued = 0;
	UINT outputTasksQueued = 0;
	UINT metadataTextureTasksQueued = 0;
	UINT metadataBufferTasksQueued = 0;
	UINT resourceTasksQueued = 0;
	UINT iaCandidates = 0;
	UINT iaResolvedCandidates = 0;
	UINT iaDuplicateAliases = 0;
	UINT iaKeyBuildFailures = 0;
	UINT untrackedDescriptorSlots = 0;
	UINT untrackedComputeUavSlots = 0;
	UINT computeBindingCandidates = 0;
	UINT computeBufferBindingCandidates = 0;
	UINT computeBindingResolved = 0;
	UINT computeCbvBufferCandidates = 0;
	UINT computeSrvBufferCandidates = 0;
	UINT computeUavBufferCandidates = 0;

	for (const DX12FrameResourceBinding &binding : bindings) {
		if (DX12ResourceDump::IsComputeCbvSrvUavBinding(binding)) {
			computeBindingCandidates++;
			if (binding.hasDescriptor)
				computeBindingResolved++;
			if (DX12ResourceDump::IsComputeCbvSrvUavBufferBinding(binding)) {
				computeBufferBindingCandidates++;
				if (binding.descriptor.kind == DescriptorKind::CBV)
					computeCbvBufferCandidates++;
				else if (binding.descriptor.kind == DescriptorKind::SRV)
					computeSrvBufferCandidates++;
				else if (binding.descriptor.kind == DescriptorKind::UAV)
					computeUavBufferCandidates++;
			}
		}
		if (!DX12ResourceDumpPolicy::ShouldDumpBinding(binding)) {
			if (!binding.hasDescriptor && !binding.rootDescriptor &&
					(binding.bindSpace == "compute_cbv_srv_uav" ||
					 binding.bindSpace == "graphics_cbv_srv_uav")) {
				DumpTask skipped;
				skipped.sourceKind = DumpTaskSource::Descriptor;
				skipped.binding = binding;
				skipped.skipNote = "untracked_descriptor";
				skippedTasks.push_back(std::move(skipped));
				untrackedDescriptorSlots++;
				if (binding.bindSpace == "compute_cbv_srv_uav" &&
						binding.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV)
					untrackedComputeUavSlots++;
			}
			continue;
		}
		const char *bindingKind = DX12ResourceDumpCore::BindingKindName(binding);
		if (!strcmp(bindingKind, DescriptorKind::CBV.data()))
			candidateCbv++;
		else if (!strcmp(bindingKind, DescriptorKind::SRV.data()))
			candidateSrv++;
		else if (!strcmp(bindingKind, DescriptorKind::UAV.data()))
			candidateUav++;
		if (binding.descriptor.hasResourceDesc &&
			DX12ResourceDumpPolicy::IsTextureResourceDimension(binding.descriptor.resourceDesc.Dimension))
			candidateTextures++;
		if (!binding.hasDescriptor && !binding.rootDescriptor) {
			DumpTask skipped;
			skipped.binding = binding;
			skipped.skipNote = binding.rootDescriptor ? "root_descriptor_summary_only" : "untracked_descriptor";
			skippedTasks.push_back(std::move(skipped));
			continue;
		}

		char key[128];
		if (binding.rootDescriptor) {
			sprintf_s(key, "root|0x%llx",
				static_cast<unsigned long long>(binding.gpuVirtualAddress));
		} else {
			DX12ResourceDumpNaming::BuildBindingDedupeKeyForCurrentMode(binding, key, ARRAYSIZE(key));
		}
		auto canonicalIt = canonicalTaskByKey.find(key);
		if (canonicalIt != canonicalTaskByKey.end()) {
			DumpTask duplicate;
			duplicate = tasks[canonicalIt->second];
			duplicate.readback = nullptr;
			duplicate.sourceRetained = false;
			duplicate.keepReadbackAlive = false;
			duplicate.binding = binding;
			if (binding.rootDescriptor) {
				duplicate.binding.hasDescriptor = true;
				duplicate.binding.descriptor = tasks[canonicalIt->second].binding.descriptor;
			}
			duplicate.sourceKind = DumpTaskSource::Descriptor;
			duplicate.dedupeKey = key;
			wchar_t fileName[MAX_PATH];
			DX12ResourceDumpNaming::BuildFileName(duplicate, fileName, ARRAYSIZE(fileName));
			duplicate.fileName = fileName;
			duplicate.skipNote = "already_dumped";
			duplicateTasks.push_back(std::move(duplicate));
			duplicates++;
			continue;
		}

		DumpTask task;
		if (DX12ResourceDumpPrepare::PrepareDescriptorTaskForCurrentMode(device, binding, &task, &frameReadbackBytes, frameReadbackBudget)) {
			wchar_t fileName[MAX_PATH];
			DX12ResourceDumpNaming::BuildFileName(task, fileName, ARRAYSIZE(fileName));
			task.fileName = fileName;
			task.dedupeKey = key;
			canonicalTaskByKey.emplace(task.dedupeKey, tasks.size());
			bindingTasksQueued++;
			tasks.push_back(std::move(task));
		} else {
			if (task.skipNote == nullptr)
				task.skipNote = "unsupported_or_no_resource_pointer";
			skippedTasks.push_back(std::move(task));
		}
	}
	DX12FrameAnalysis::LogJsonFunc("ResourceDumpComputeBindingsCollected",
		"\"bindings\":%u,\"resolved\":%u,\"buffer_bindings\":%u,\"cbv_buffers\":%u,\"srv_buffers\":%u,\"uav_buffers\":%u,\"queued_so_far\":%u,\"untracked_compute_uav\":%u",
		computeBindingCandidates,
		computeBindingResolved,
		computeBufferBindingCandidates,
		computeCbvBufferCandidates,
		computeSrvBufferCandidates,
		computeUavBufferCandidates,
		bindingTasksQueued,
		untrackedComputeUavSlots);

	for (const DX12FrameIaBufferBinding &buffer : iaBuffers) {
		iaCandidates++;
		DX12BufferResourceSummary iaResource = {};
		const bool iaResolved =
			DX12BindingFrameExport::ResolveIaBufferResourceSummary(buffer, &iaResource);
		if (iaResolved)
			iaResolvedCandidates++;
		char key[128];
		const bool keyBuilt = DX12ResourceDumpNaming::BuildIaBufferDedupeKey(
			buffer, &iaResource, iaResolved, key, ARRAYSIZE(key));
		if (!keyBuilt) {
			iaKeyBuildFailures++;
			DumpTask skipped;
			skipped.sourceKind = DumpTaskSource::InputAssembler;
			skipped.iaBuffer = buffer;
			skipped.skipNote = "ia_dedupe_key_build_failed";
			DX12FrameAnalysis::LogJsonFunc("IaBindingPrepareSkipped",
				"\"call_index\":%llu,\"role\":\"%s\",\"view_gpu\":\"0x%llx\",\"view_bytes\":%llu,\"draw_gpu\":\"0x%llx\",\"draw_bytes\":%llu,\"fmt\":%u,\"stride\":%u,\"resolved\":%u,\"note\":\"%s\"",
				static_cast<unsigned long long>(buffer.eventSerial),
				buffer.role.c_str(),
				static_cast<unsigned long long>(buffer.viewGpuVa),
				static_cast<unsigned long long>(buffer.viewSize),
				static_cast<unsigned long long>(buffer.drawGpuVa),
				static_cast<unsigned long long>(buffer.drawSize),
				buffer.format,
				buffer.stride,
				iaResolved ? 1 : 0,
				skipped.skipNote);
			skippedTasks.push_back(std::move(skipped));
			continue;
		}
		auto canonicalIt = canonicalTaskByKey.find(key);
		if (canonicalIt != canonicalTaskByKey.end()) {
			DumpTask duplicate;
			duplicate = tasks[canonicalIt->second];
			duplicate.readback = nullptr;
			duplicate.sourceRetained = false;
			duplicate.keepReadbackAlive = false;
			duplicate.sourceKind = DumpTaskSource::InputAssembler;
			duplicate.iaBuffer = buffer;
			duplicate.dedupeKey = key;
			wchar_t fileName[MAX_PATH];
			DX12ResourceDumpNaming::BuildFileName(duplicate, fileName, ARRAYSIZE(fileName));
			duplicate.fileName = fileName;
			duplicate.skipNote = "already_dumped";
			duplicateTasks.push_back(std::move(duplicate));
			duplicates++;
			iaDuplicateAliases++;
			continue;
		}

		DumpTask task;
		if (DX12ResourceDumpPrepare::PrepareIaBufferTaskForCurrentMode(device, buffer, &task, &frameReadbackBytes, frameReadbackBudget)) {
			wchar_t fileName[MAX_PATH];
			DX12BindingFrameExport::BuildIaBufferFileName(buffer, fileName, ARRAYSIZE(fileName));
			task.fileName = fileName;
			task.dedupeKey = key;
			canonicalTaskByKey.emplace(task.dedupeKey, tasks.size());
			iaTasksQueued++;
			tasks.push_back(std::move(task));
		} else {
			task.sourceKind = DumpTaskSource::InputAssembler;
			task.iaBuffer = buffer;
			if (task.skipNote == nullptr)
				task.skipNote = buffer.resolved ? "unsupported_or_no_resource_pointer" : "unresolved_gpu_va";
			if (task.skipNote && !strcmp(task.skipNote, "unresolved_gpu_va")) {
				UINT64 nearestBegin = 0;
				UINT64 nearestEnd = 0;
				UINT64 nearestResource = 0;
				const UINT64 diagnoseSize = buffer.viewSize ? buffer.viewSize :
					(buffer.drawSize ? buffer.drawSize : 1);
				const char *reason = DX12DiagnoseBufferResolveByGpuVa(
					buffer.viewGpuVa,
					diagnoseSize,
					&nearestBegin,
					&nearestEnd,
					&nearestResource);
				DX12FrameAnalysis::LogJsonFunc("IaBindingPrepareSkipped",
					"\"call_index\":%llu,\"role\":\"%s\",\"view_gpu\":\"0x%llx\",\"view_bytes\":%llu,\"draw_gpu\":\"0x%llx\",\"draw_bytes\":%llu,\"fmt\":%u,\"stride\":%u,\"resolved\":%u,\"reason\":\"%s\",\"nearest_begin\":\"0x%llx\",\"nearest_end\":\"0x%llx\",\"nearest_resource\":\"0x%llx\",\"note\":\"%s\"",
					static_cast<unsigned long long>(buffer.eventSerial),
					buffer.role.c_str(),
					static_cast<unsigned long long>(buffer.viewGpuVa),
					static_cast<unsigned long long>(buffer.viewSize),
					static_cast<unsigned long long>(buffer.drawGpuVa),
					static_cast<unsigned long long>(buffer.drawSize),
					buffer.format,
					buffer.stride,
					iaResolved ? 1 : 0,
					reason ? reason : "",
					static_cast<unsigned long long>(nearestBegin),
					static_cast<unsigned long long>(nearestEnd),
					static_cast<unsigned long long>(nearestResource),
					task.skipNote);
			} else {
				DX12FrameAnalysis::LogJsonFunc("IaBindingPrepareSkipped",
					"\"call_index\":%llu,\"role\":\"%s\",\"view_gpu\":\"0x%llx\",\"view_bytes\":%llu,\"draw_gpu\":\"0x%llx\",\"draw_bytes\":%llu,\"fmt\":%u,\"stride\":%u,\"resolved\":%u,\"note\":\"%s\"",
					static_cast<unsigned long long>(buffer.eventSerial),
					buffer.role.c_str(),
					static_cast<unsigned long long>(buffer.viewGpuVa),
					static_cast<unsigned long long>(buffer.viewSize),
					static_cast<unsigned long long>(buffer.drawGpuVa),
					static_cast<unsigned long long>(buffer.drawSize),
					buffer.format,
					buffer.stride,
					iaResolved ? 1 : 0,
					task.skipNote);
			}
			skippedTasks.push_back(std::move(task));
		}
	}
	DX12FrameAnalysis::LogJsonFunc("IaBindingsCollected",
		"\"candidates\":%u,\"resolved\":%u,\"queued\":%u,\"duplicate_aliases\":%u,\"key_failures\":%u,\"skipped_so_far\":%zu",
		iaCandidates,
		iaResolvedCandidates,
		iaTasksQueued,
		iaDuplicateAliases,
		iaKeyBuildFailures,
		skippedTasks.size());

	for (const DX12FrameOutputBinding &output : outputBindings) {
		DX12FrameResourceBinding binding = DX12ResourceDumpCore::ResourceBindingFromOutputBinding(output);
		if (!DX12ResourceDumpPolicy::ShouldDumpBinding(binding)) {
			DumpTask skipped;
			skipped.sourceKind = DumpTaskSource::Output;
			skipped.outputBinding = output;
			skipped.binding = binding;
			skipped.skipNote = output.hasDescriptor ?
				"unsupported_or_no_resource_pointer" : "untracked_output_descriptor";
			skippedTasks.push_back(std::move(skipped));
			continue;
		}
		if (binding.descriptor.kind == DescriptorKind::RTV)
			candidateRtv++;
		else if (binding.descriptor.kind == DescriptorKind::DSV)
			candidateDsv++;
		if (binding.descriptor.hasResourceDesc &&
			DX12ResourceDumpPolicy::IsTextureResourceDimension(binding.descriptor.resourceDesc.Dimension))
			candidateTextures++;

		char key[160];
		DX12ResourceDumpNaming::BuildOutputDedupeKeyForCurrentMode(output, binding, key, ARRAYSIZE(key));
		auto canonicalIt = canonicalTaskByKey.find(key);
		if (canonicalIt != canonicalTaskByKey.end()) {
			DumpTask duplicate;
			duplicate = tasks[canonicalIt->second];
			duplicate.readback = nullptr;
			duplicate.sourceRetained = false;
			duplicate.keepReadbackAlive = false;
			duplicate.sourceKind = DumpTaskSource::Output;
			duplicate.outputBinding = output;
			duplicate.binding = binding;
			duplicate.dedupeKey = key;
			wchar_t fileName[MAX_PATH];
			DX12ResourceDumpNaming::BuildFileName(duplicate, fileName, ARRAYSIZE(fileName));
			duplicate.fileName = fileName;
			duplicate.skipNote = "already_dumped";
			duplicateTasks.push_back(std::move(duplicate));
			duplicates++;
			continue;
		}

		DumpTask task;
		if (DX12ResourceDumpPrepare::PrepareDescriptorTaskForCurrentMode(device, binding, &task, &frameReadbackBytes, frameReadbackBudget)) {
			task.sourceKind = DumpTaskSource::Output;
			task.outputBinding = output;
			wchar_t fileName[MAX_PATH];
			DX12ResourceDumpNaming::BuildFileName(task, fileName, ARRAYSIZE(fileName));
			task.fileName = fileName;
			task.dedupeKey = key;
			canonicalTaskByKey.emplace(task.dedupeKey, tasks.size());
			outputTasksQueued++;
			tasks.push_back(std::move(task));
		} else {
			task.sourceKind = DumpTaskSource::Output;
			task.outputBinding = output;
			task.binding = binding;
			if (task.skipNote == nullptr)
				task.skipNote = "unsupported_or_no_resource_pointer";
			skippedTasks.push_back(std::move(task));
		}
	}

	DX12FrameAnalysis::LogJsonFunc("ResourceDumpDiscoverySkipped",
		"\"reason\":\"single_frame_event_scope\",\"metadataDescriptors\":0,\"trackedResources\":0");
	DX12FrameAnalysis::LogJsonFunc("ResourceDumpTasksQueued",
		"\"tasks\":%zu,\"binding\":%u,\"ia\":%u,\"output\":%u,\"metadataTexture\":%u,\"metadataBuffer\":%u,\"metadataTextureBytes\":%llu,\"metadataBufferBytes\":%llu,\"metadataCbvBuffer\":%u,\"metadataCbvBufferBytes\":%llu,\"metadataSrvUavBuffer\":%u,\"metadataSrvUavBufferBytes\":%llu,\"resource\":%u,\"duplicates\":%u,\"skippedSoFar\":%zu",
		tasks.size(), bindingTasksQueued, iaTasksQueued, outputTasksQueued,
		metadataTextureTasksQueued, metadataBufferTasksQueued,
		static_cast<unsigned long long>(metadataTextureCopyBytesQueued),
		static_cast<unsigned long long>(metadataBufferCopyBytesQueued),
		metadataCbvBufferCopiesQueued,
		static_cast<unsigned long long>(metadataCbvBufferCopyBytesQueued),
		metadataSrvUavBufferCopiesQueued,
		static_cast<unsigned long long>(metadataSrvUavBufferCopyBytesQueued),
		resourceTasksQueued, duplicates, skippedTasks.size());

	UINT dumpedTextures = 0;
	UINT dumpedBuffers = 0;
	UINT failed = 0;
	UINT linked = 0;
	UINT skipped = 0;
	UINT skippedDiscoveryDuplicates = 0;
	UINT skippedAbortedDuplicateAliases = 0;
	UINT copiedTasks = 0;
	UINT copyFailedTasks = 0;
	UINT directMapTasks = 0;
	UINT computeBindingFilesDumped = 0;
	UINT computeBindingFilesLinked = 0;
	UINT computeBindingFilesSkipped = 0;
	UINT computeBindingFilesFailed = 0;
	bool readbackCopyOk = true;
	bool copyBatchAborted = false;
	std::unordered_map<std::string, std::wstring> dedupePathByKey;
	std::unordered_map<std::string, const char*> dedupeFailureReasonByKey;
	std::unordered_set<std::wstring> loggedFileDumps;
	for (DumpTask &task : tasks) {
		bool ok = task.directMap || task.copied;
		if (task.directMap)
			directMapTasks++;
		if (!ok && task.ready && task.readback && !copyBatchAborted) {
			ok = DX12ResourceDumpGpuCopy::ExecuteCopyTask(device, queue, &task);
			if (ok)
				copiedTasks++;
			else {
				copyFailedTasks++;
				readbackCopyOk = false;
				HRESULT removedHr = S_OK;
				const bool deviceRemoved = DX12ResourceDumpGpuCopy::DeviceRemoved(device, &removedHr);
				const bool timedOut = task.skipNote && !strcmp(task.skipNote, "copy_wait_timeout");
				if (deviceRemoved || timedOut) {
					copyBatchAborted = true;
					const char *abortNote = deviceRemoved ?
						"copy_aborted_device_removed" : "copy_aborted_wait_timeout";
					DX12FrameAnalysis::LogJsonFunc("ResourceCopyBatchAborted",
						"\"reason\":\"%s\",\"deviceHr\":\"0x%lx\",\"copied\":%u,\"copyFailed\":%u,\"tasks\":%zu",
						abortNote, removedHr, copiedTasks, copyFailedTasks, tasks.size());
				}
			}
		} else if (!ok && copyBatchAborted && !task.skipNote) {
			task.skipNote = "copy_aborted";
		}
		std::wstring dedupePath;
		if (ok) {
			ok = DX12ResourceDumpFileWriter::MapAndWriteTask(dedupeDir, resourceDir, &task, &dedupePath);
			if (ok && !task.dedupeKey.empty())
				dedupePathByKey[task.dedupeKey] = dedupePath;
		}

		if (ok && task.isTexture)
			dumpedTextures++;
		else if (ok)
			dumpedBuffers++;
		else
			failed++;
		if (DX12ResourceDump::IsComputeCbvSrvUavBufferTask(task)) {
			if (ok)
				computeBindingFilesDumped++;
			else
				computeBindingFilesFailed++;
		}

		if (!ok) {
			if (!task.dedupeKey.empty())
				dedupeFailureReasonByKey[task.dedupeKey] =
					task.skipNote ? task.skipNote : "resource_dump_failed";
			continue;
		}

		if (loggedFileDumps.insert(dedupePath).second) {
			DX12FrameAnalysisManifest::WriteFileDump(
				dedupePath.c_str(), task.isTexture, task.copyBytes, "dumped", "");
		}
		DX12ResourceDumpIndexWriter::WriteIndexRow(nullptr, "dumped", task, task.desc,
			task.sourceOffset, task.copyBytes, task.sourceState, task.stateKnown,
			task.fileName.c_str(), dedupePath.c_str(), "");
	}

	DX12FrameAnalysis::LogJsonFunc("ResourceCopyResult",
		"\"gpuCopy\":%u,\"readbackOk\":%u,\"copied\":%u,\"copyFailed\":%u,\"directMap\":%u,\"tasks\":%zu,\"duplicates\":%u,\"streamedWrite\":1",
		1,
		readbackCopyOk ? 1 : 0, copiedTasks, copyFailedTasks, directMapTasks,
		tasks.size(), duplicates);

	for (DumpTask &task : duplicateTasks) {
		if (DX12ResourceDump::IsDiscoveryDuplicateTask(task)) {
			skipped++;
			skippedDiscoveryDuplicates++;
			continue;
		}

		auto dedupeIt = dedupePathByKey.find(task.dedupeKey);
		if (dedupeIt == dedupePathByKey.end()) {
			if (copyBatchAborted) {
				skipped++;
				skippedAbortedDuplicateAliases++;
				if (task.sourceKind == DumpTaskSource::InputAssembler) {
					DX12FrameAnalysis::LogJsonFunc("IaBindingAliasSkipped",
						"\"call_index\":%llu,\"role\":\"%s\",\"view_gpu\":\"0x%llx\",\"view_bytes\":%llu,\"draw_gpu\":\"0x%llx\",\"draw_bytes\":%llu,\"dedupe_key\":\"%s\",\"note\":\"copy_batch_aborted\"",
						static_cast<unsigned long long>(task.iaBuffer.eventSerial),
						task.iaBuffer.role.c_str(),
						static_cast<unsigned long long>(task.iaBuffer.viewGpuVa),
						static_cast<unsigned long long>(task.iaBuffer.viewSize),
						static_cast<unsigned long long>(task.iaBuffer.drawGpuVa),
						static_cast<unsigned long long>(task.iaBuffer.drawSize),
						task.dedupeKey.c_str());
				}
				continue;
			}
			if (task.sourceKind == DumpTaskSource::InputAssembler) {
				DX12FrameAnalysis::LogJsonFunc("IaBindingAliasMissingCanonicalDump",
					"\"call_index\":%llu,\"role\":\"%s\",\"view_gpu\":\"0x%llx\",\"view_bytes\":%llu,\"draw_gpu\":\"0x%llx\",\"draw_bytes\":%llu,\"dedupe_key\":\"%s\",\"note\":\"canonical_dedupe_path_missing\"",
					static_cast<unsigned long long>(task.iaBuffer.eventSerial),
					task.iaBuffer.role.c_str(),
					static_cast<unsigned long long>(task.iaBuffer.viewGpuVa),
					static_cast<unsigned long long>(task.iaBuffer.viewSize),
					static_cast<unsigned long long>(task.iaBuffer.drawGpuVa),
					static_cast<unsigned long long>(task.iaBuffer.drawSize),
					task.dedupeKey.c_str());
			}
			failed++;
			continue;
		}

		linked++;
		if (DX12ResourceDump::IsComputeCbvSrvUavBufferTask(task))
			computeBindingFilesLinked++;
		DX12ResourceDumpIndexWriter::WriteIndexRow(nullptr, "linked", task, task.desc,
			task.sourceOffset, task.copyBytes, task.sourceState, task.stateKnown,
			task.fileName.c_str(), dedupeIt->second.c_str(), "already_dumped");
	}

	for (const DumpTask &task : skippedTasks) {
		skipped++;
		if (DX12ResourceDump::IsComputeCbvSrvUavBufferTask(task))
			computeBindingFilesSkipped++;
		if (task.sourceKind == DumpTaskSource::Descriptor) {
			DX12ResourceDumpIndexWriter::WriteIndexRow(nullptr, "skipped", task, task.binding.descriptor.resourceDesc,
				task.binding.descriptor.resourceOffset, task.binding.descriptor.viewSize,
				task.binding.descriptor.hasCurrentState ?
					static_cast<D3D12_RESOURCE_STATES>(task.binding.descriptor.currentState) :
					DX12ResourceStateTracker::InferDescriptorSourceState(task.binding),
				task.binding.descriptor.hasCurrentState, L"", L"",
				task.skipNote ? task.skipNote : "skipped");
		} else if (task.sourceKind == DumpTaskSource::InputAssembler) {
			DX12FrameAnalysis::LogJsonFunc("IaBindingSkipped",
				"\"call_index\":%llu,\"role\":\"%s\",\"view_gpu\":\"0x%llx\",\"view_bytes\":%llu,\"draw_gpu\":\"0x%llx\",\"draw_bytes\":%llu,\"fmt\":%u,\"stride\":%u,\"note\":\"%s\"",
				static_cast<unsigned long long>(task.iaBuffer.eventSerial),
				task.iaBuffer.role.c_str(),
				static_cast<unsigned long long>(task.iaBuffer.viewGpuVa),
				static_cast<unsigned long long>(task.iaBuffer.viewSize),
				static_cast<unsigned long long>(task.iaBuffer.drawGpuVa),
				static_cast<unsigned long long>(task.iaBuffer.drawSize),
				task.iaBuffer.format,
				task.iaBuffer.stride,
				task.skipNote ? task.skipNote : "skipped");
			continue;
		} else if (task.sourceKind == DumpTaskSource::Output) {
			DX12ResourceDumpIndexWriter::WriteIndexRow(nullptr, "skipped", task, task.binding.descriptor.resourceDesc,
				task.binding.descriptor.resourceOffset, task.binding.descriptor.viewSize,
				task.binding.descriptor.hasCurrentState ?
					static_cast<D3D12_RESOURCE_STATES>(task.binding.descriptor.currentState) :
					DX12ResourceStateTracker::InferDescriptorSourceState(task.binding),
				task.binding.descriptor.hasCurrentState, L"", L"",
				task.skipNote ? task.skipNote : "skipped");
		} else if (task.sourceKind == DumpTaskSource::Resource) {
			const UINT64 skippedOffset = task.copyBytes ? task.sourceOffset : task.binding.descriptor.resourceOffset;
			const UINT64 skippedBytes = task.copyBytes ? task.copyBytes : task.binding.descriptor.viewSize;
			const D3D12_RESOURCE_STATES skippedState = task.stateKnown ?
				task.sourceState :
				(task.binding.descriptor.hasCurrentState ?
					static_cast<D3D12_RESOURCE_STATES>(task.binding.descriptor.currentState) :
					DX12ResourceStateTracker::InferDescriptorSourceState(task.binding));
			const bool skippedStateKnown = task.stateKnown || task.binding.descriptor.hasCurrentState;
			DX12ResourceDumpIndexWriter::WriteIndexRow(nullptr, "skipped", task, task.binding.descriptor.resourceDesc,
				skippedOffset, skippedBytes, skippedState, skippedStateKnown, L"", L"",
				task.skipNote ? task.skipNote : "skipped");
		}
	}

	DX12ResourceDumpPrepare::ReleaseTasks(&tasks);
	DX12ResourceDumpPrepare::ReleaseTasks(&skippedTasks);
	device->Release();
	queue->Release();

	DX12FrameAnalysis::LogJsonFunc("ResourceDumpComputeBindingFiles",
		"\"buffer_bindings\":%u,\"dumped\":%u,\"linked\":%u,\"skipped\":%u,\"failed\":%u,\"file_rows\":%u",
		computeBufferBindingCandidates,
		computeBindingFilesDumped,
		computeBindingFilesLinked,
		computeBindingFilesSkipped,
		computeBindingFilesFailed,
		computeBindingFilesDumped + computeBindingFilesLinked);
	DX12FrameAnalysis::LogJsonFunc("ResourceSummary",
		"\"textures\":%u,\"buffers\":%u,\"linked\":%u,\"skipped\":%u,\"duplicates\":%u,\"failed\":%u,\"readbackBytes\":%llu,\"bindings\":%zu,\"ia\":%zu,\"outputs\":%zu,\"candidate_cbv\":%u,\"candidate_srv\":%u,\"candidate_uav\":%u,\"candidate_rtv\":%u,\"candidate_dsv\":%u,\"candidate_textures\":%u,\"compute_bindings\":%u,\"compute_buffer_bindings\":%u,\"compute_cbv_buffer_bindings\":%u,\"compute_srv_buffer_bindings\":%u,\"compute_uav_buffer_bindings\":%u,\"compute_binding_file_rows\":%u,\"metadata_texture_candidates\":%u,\"metadata_buffer_candidates\":%u,\"metadata_cbv_buffer_candidates\":%u,\"metadata_srv_buffer_candidates\":%u,\"metadata_uav_buffer_candidates\":%u,\"metadata_prepare_failed\":%u,\"metadata_buffer_copies_queued\":%u,\"metadata_buffer_copy_bytes_queued\":%llu,\"metadata_buffer_copy_budget_skipped\":%u,\"metadata_missing_resource\":%u,\"metadata_missing_resource_desc\":%u,\"metadata_unsupported_kind_or_dimension\":%u,\"untracked_descriptor_slots\":%u,\"untracked_compute_uav_slots\":%u,\"skipped_discovery_duplicate_aliases\":%u,\"skipped_aborted_duplicate_aliases\":%u",
		dumpedTextures, dumpedBuffers, linked, skipped, duplicates, failed,
		static_cast<unsigned long long>(frameReadbackBytes),
		bindings.size(), iaBuffers.size(), outputBindings.size(),
		candidateCbv, candidateSrv, candidateUav, candidateRtv, candidateDsv,
		candidateTextures,
		computeBindingCandidates, computeBufferBindingCandidates,
		computeCbvBufferCandidates, computeSrvBufferCandidates,
		computeUavBufferCandidates, computeBindingFilesDumped + computeBindingFilesLinked,
		metadataTextureCandidates, metadataBufferCandidates,
		metadataCbvBufferCandidates, metadataSrvBufferCandidates,
		metadataUavBufferCandidates, metadataPrepareFailed,
		metadataBufferCopiesQueued,
		static_cast<unsigned long long>(metadataBufferCopyBytesQueued),
		metadataBufferCopyBudgetSkipped,
		metadataMissingResource, metadataMissingResourceDesc,
		metadataUnsupportedKindOrDimension,
		untrackedDescriptorSlots, untrackedComputeUavSlots,
		skippedDiscoveryDuplicates, skippedAbortedDuplicateAliases);
	result.ok = failed == 0;
	result.written = static_cast<size_t>(dumpedTextures) + dumpedBuffers + linked;
	result.skipped = skipped;
	result.failed = failed;
	result.reason = failed == 0 ? "" : "resource_failures";
	return result;
}





};

// ---- DX12ResourceHookSlotTypes.hpp ----
#line 1 "DX12ResourceHookSlotTypes.hpp"
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

// ---- DX12ResourceTrackerHookCallbacks.hpp ----
#line 1 "DX12ResourceTrackerHookCallbacks.hpp"
#include <Windows.h>
#include <d3d12.h>
#include <dxgi.h>

class DX12ResourceTrackerHookCallbacks
{
public:
	static HRESULT STDMETHODCALLTYPE HookedCreateDescriptorHeap(
		ID3D12Device *device, const D3D12_DESCRIPTOR_HEAP_DESC *desc, REFIID riid, void **heap);
	static HRESULT STDMETHODCALLTYPE HookedCreateRootSignature(
		ID3D12Device *device, UINT nodeMask, const void *blob, SIZE_T blobLength,
		REFIID riid, void **rootSignature);
	static void STDMETHODCALLTYPE HookedCreateConstantBufferView(
		ID3D12Device *device, const D3D12_CONSTANT_BUFFER_VIEW_DESC *desc,
		D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor);
	static void STDMETHODCALLTYPE HookedCreateShaderResourceView(
		ID3D12Device *device, ID3D12Resource *resource,
		const D3D12_SHADER_RESOURCE_VIEW_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor);
	static void STDMETHODCALLTYPE HookedCreateUnorderedAccessView(
		ID3D12Device *device, ID3D12Resource *resource, ID3D12Resource *counterResource,
		const D3D12_UNORDERED_ACCESS_VIEW_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor);
	static void STDMETHODCALLTYPE HookedCreateRenderTargetView(
		ID3D12Device *device, ID3D12Resource *resource,
		const D3D12_RENDER_TARGET_VIEW_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor);
	static void STDMETHODCALLTYPE HookedCreateDepthStencilView(
		ID3D12Device *device, ID3D12Resource *resource,
		const D3D12_DEPTH_STENCIL_VIEW_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor);
	static void STDMETHODCALLTYPE HookedCreateSampler(
		ID3D12Device *device, const D3D12_SAMPLER_DESC *desc,
		D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor);
	static void STDMETHODCALLTYPE HookedCopyDescriptors(
		ID3D12Device *device, UINT numDestDescriptorRanges,
		const D3D12_CPU_DESCRIPTOR_HANDLE *destDescriptorRangeStarts,
		const UINT *destDescriptorRangeSizes, UINT numSrcDescriptorRanges,
		const D3D12_CPU_DESCRIPTOR_HANDLE *srcDescriptorRangeStarts,
		const UINT *srcDescriptorRangeSizes, D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapsType);
	static void STDMETHODCALLTYPE HookedCopyDescriptorsSimple(
		ID3D12Device *device, UINT numDescriptors, D3D12_CPU_DESCRIPTOR_HANDLE destDescriptorRangeStart,
		D3D12_CPU_DESCRIPTOR_HANDLE srcDescriptorRangeStart, D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapsType);
	static HRESULT STDMETHODCALLTYPE HookedCreateCommittedResource(
		ID3D12Device *device, const D3D12_HEAP_PROPERTIES *heapProperties,
		D3D12_HEAP_FLAGS heapFlags, const D3D12_RESOURCE_DESC *desc,
		D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE *optimizedClearValue,
		REFIID riid, void **resource);
	static HRESULT STDMETHODCALLTYPE HookedCreatePlacedResource(
		ID3D12Device *device, ID3D12Heap *heap, UINT64 heapOffset,
		const D3D12_RESOURCE_DESC *desc, D3D12_RESOURCE_STATES initialState,
		const D3D12_CLEAR_VALUE *optimizedClearValue, REFIID riid, void **resource);
	static HRESULT STDMETHODCALLTYPE HookedCreateReservedResource(
		ID3D12Device *device, const D3D12_RESOURCE_DESC *desc,
		D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE *optimizedClearValue,
		REFIID riid, void **resource);
	static HRESULT STDMETHODCALLTYPE HookedCreateFence(
		ID3D12Device *device, UINT64 initialValue, D3D12_FENCE_FLAGS flags,
		REFIID riid, void **fence);
	static HRESULT STDMETHODCALLTYPE HookedCreateCommittedResource1(
		ID3D12Device4 *device, const D3D12_HEAP_PROPERTIES *heapProperties,
		D3D12_HEAP_FLAGS heapFlags, const D3D12_RESOURCE_DESC *desc,
		D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE *optimizedClearValue,
		ID3D12ProtectedResourceSession *protectedSession, REFIID riid, void **resource);
	static HRESULT STDMETHODCALLTYPE HookedCreateReservedResource1(
		ID3D12Device4 *device, const D3D12_RESOURCE_DESC *desc,
		D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE *optimizedClearValue,
		ID3D12ProtectedResourceSession *protectedSession, REFIID riid, void **resource);
	static HRESULT STDMETHODCALLTYPE HookedCreateCommittedResource2(
		ID3D12Device8 *device, const D3D12_HEAP_PROPERTIES *heapProperties,
		D3D12_HEAP_FLAGS heapFlags, const D3D12_RESOURCE_DESC1 *desc,
		D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE *optimizedClearValue,
		ID3D12ProtectedResourceSession *protectedSession, REFIID riid, void **resource);
	static HRESULT STDMETHODCALLTYPE HookedCreatePlacedResource1(
		ID3D12Device8 *device, ID3D12Heap *heap, UINT64 heapOffset,
		const D3D12_RESOURCE_DESC1 *desc, D3D12_RESOURCE_STATES initialState,
		const D3D12_CLEAR_VALUE *optimizedClearValue, REFIID riid, void **resource);
	static HRESULT STDMETHODCALLTYPE HookedCreateCommittedResource3(
		ID3D12Device10 *device, const D3D12_HEAP_PROPERTIES *heapProperties,
		D3D12_HEAP_FLAGS heapFlags, const D3D12_RESOURCE_DESC1 *desc,
		D3D12_BARRIER_LAYOUT initialLayout, const D3D12_CLEAR_VALUE *optimizedClearValue,
		ID3D12ProtectedResourceSession *protectedSession, UINT32 numCastableFormats,
		const DXGI_FORMAT *castableFormats, REFIID riid, void **resource);
	static HRESULT STDMETHODCALLTYPE HookedCreatePlacedResource2(
		ID3D12Device10 *device, ID3D12Heap *heap, UINT64 heapOffset,
		const D3D12_RESOURCE_DESC1 *desc, D3D12_BARRIER_LAYOUT initialLayout,
		const D3D12_CLEAR_VALUE *optimizedClearValue, UINT32 numCastableFormats,
		const DXGI_FORMAT *castableFormats, REFIID riid, void **resource);
	static HRESULT STDMETHODCALLTYPE HookedCreateReservedResource2(
		ID3D12Device10 *device, const D3D12_RESOURCE_DESC *desc,
		D3D12_BARRIER_LAYOUT initialLayout, const D3D12_CLEAR_VALUE *optimizedClearValue,
		ID3D12ProtectedResourceSession *protectedSession, UINT32 numCastableFormats,
		const DXGI_FORMAT *castableFormats, REFIID riid, void **resource);
};

// ---- DX12ResourceTrackerHookDiagnostics.hpp ----
#line 1 "DX12ResourceTrackerHookDiagnostics.hpp"
#include <Windows.h>


class DX12ResourceTrackerHookDiagnostics
{
public:
	static void LogResourceHookCall(const char *api, const void *object)
	{
		if (!DX12LogManager::ShouldLogHookCall(api))
			return;
		DX12LogManager::DebugJsonFunc("DX12HookCall",
			"\"api\":\"%s\",\"present\":%ld,\"this\":\"%p\"",
			api ? api : "", DX12RuntimeState::GetPresentCount(), object);
	}

	static void LogResourceHookCallWhenTracking(const char *api, const void *object)
	{
		if (DX12HotPathState::skipAll &&
				!DX12ResourceTrackingPolicyUtils::NeedsFullResourceMetadata())
			return;
		LogResourceHookCall(api, object);
	}
};

// ---- DX12ResourceTrackerHookRegistry.hpp ----
#line 1 "DX12ResourceTrackerHookRegistry.hpp"
#include <Windows.h>


class DX12ResourceTrackerHookRegistry
{
public:
	template <typename T>
	static T GetDeviceOriginal(void *device, UINT slot, T fallback, const char *name)
	{
		return DX12OriginalFunctionRegistry::Resolve(device, slot, fallback, name);
	}
};

// ---- DX12ResourceTrackerHookRuntime.hpp ----
#line 1 "DX12ResourceTrackerHookRuntime.hpp"
#include <d3d12.h>


class DX12ResourceTrackerHookRuntime
{
public:
	static const D3D12_UNORDERED_ACCESS_VIEW_DESC *PrepareUavDesc(
		ID3D12Resource *resource,
		const D3D12_UNORDERED_ACCESS_VIEW_DESC *desc,
		D3D12_UNORDERED_ACCESS_VIEW_DESC *storage,
		const char *source);
	static const D3D12_RESOURCE_DESC *PrepareResourceDesc(
		const D3D12_RESOURCE_DESC *desc,
		D3D12_RESOURCE_DESC *storage,
		const char *source);
	static const D3D12_RESOURCE_DESC1 *PrepareResourceDesc1(
		const D3D12_RESOURCE_DESC1 *desc,
		D3D12_RESOURCE_DESC1 *storage,
		const char *source);
	static bool ShouldRecordCreatedResource(const D3D12_RESOURCE_DESC *desc);
	static void RecordCreatedDescriptorHeap(
		HRESULT hr, ID3D12Device *device, const D3D12_DESCRIPTOR_HEAP_DESC *desc, void **heap);
	static void RecordCreatedRootSignature(
		HRESULT hr, UINT nodeMask, const void *blob, SIZE_T blobLength, void **rootSignature);
	static void RecordCreatedConstantBufferView(
		const D3D12_CONSTANT_BUFFER_VIEW_DESC *desc,
		D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor);
	static void RecordCreatedShaderResourceView(
		ID3D12Resource *resource,
		const D3D12_SHADER_RESOURCE_VIEW_DESC *desc,
		D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor);
	static void RecordCreatedUnorderedAccessView(
		ID3D12Resource *resource,
		ID3D12Resource *counterResource,
		const D3D12_UNORDERED_ACCESS_VIEW_DESC *desc,
		D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor);
	static void RecordCreatedRenderTargetView(
		ID3D12Resource *resource,
		const D3D12_RENDER_TARGET_VIEW_DESC *desc,
		D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor);
	static void RecordCreatedDepthStencilView(
		ID3D12Resource *resource,
		const D3D12_DEPTH_STENCIL_VIEW_DESC *desc,
		D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor);
	static void RecordCreatedSampler(
		const D3D12_SAMPLER_DESC *desc,
		D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor);
	static void RecordCopiedDescriptors(
		ID3D12Device *device,
		UINT numDestDescriptorRanges,
		const D3D12_CPU_DESCRIPTOR_HANDLE *destDescriptorRangeStarts,
		const UINT *destDescriptorRangeSizes,
		UINT numSrcDescriptorRanges,
		const D3D12_CPU_DESCRIPTOR_HANDLE *srcDescriptorRangeStarts,
		const UINT *srcDescriptorRangeSizes,
		D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapsType);
	static void RecordCopiedDescriptorsSimple(
		ID3D12Device *device,
		UINT numDescriptors,
		D3D12_CPU_DESCRIPTOR_HANDLE destDescriptorRangeStart,
		D3D12_CPU_DESCRIPTOR_HANDLE srcDescriptorRangeStart,
		D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapsType);
	static void RecordCreatedResource(
		HRESULT hr,
		void **resource,
		const D3D12_RESOURCE_DESC *desc,
		const D3D12_HEAP_PROPERTIES *heapProperties,
		const PlacedResourceInfo *placedInfo,
		D3D12_RESOURCE_STATES initialState);
	static PlacedResourceInfo MakePlacedResourceInfo(ID3D12Heap *heap, UINT64 heapOffset);
	static void LogResourceCreateStage(
		const char *api,
		const char *stage,
		ID3D12Device *device,
		HRESULT hr,
		const D3D12_RESOURCE_DESC *desc,
		const D3D12_HEAP_PROPERTIES *heapProperties,
		D3D12_RESOURCE_STATES initialState,
		void **resource);
	static void LogResourceCreateStage1(
		const char *api,
		const char *stage,
		ID3D12Device *device,
		HRESULT hr,
		const D3D12_RESOURCE_DESC1 *desc,
		const D3D12_HEAP_PROPERTIES *heapProperties,
		D3D12_RESOURCE_STATES initialState,
		void **resource);
	static void RecordCreatedResource1(
		HRESULT hr,
		void **resource,
		const D3D12_RESOURCE_DESC1 *desc,
		const D3D12_HEAP_PROPERTIES *heapProperties,
		const PlacedResourceInfo *placedInfo,
		D3D12_RESOURCE_STATES initialState);
	static void RecordCreatedResourceWithLayout(
		HRESULT hr,
		void **resource,
		const D3D12_RESOURCE_DESC *desc,
		const D3D12_HEAP_PROPERTIES *heapProperties,
		const PlacedResourceInfo *placedInfo,
		D3D12_BARRIER_LAYOUT initialLayout);
	static void RecordCreatedResource1WithLayout(
		HRESULT hr,
		void **resource,
		const D3D12_RESOURCE_DESC1 *desc,
		const D3D12_HEAP_PROPERTIES *heapProperties,
		const PlacedResourceInfo *placedInfo,
		D3D12_BARRIER_LAYOUT initialLayout);

	static void InstallMapUnmapHooks(ID3D12Resource *resource);
};

// ---- DX12ResourceTrackerHookSignatures.hpp ----
#line 1 "DX12ResourceTrackerHookSignatures.hpp"
#include <Windows.h>
#include <d3d12.h>
#include <dxgi.h>

class DX12ResourceTrackerHookSignatures
{
public:
	using CreateDescriptorHeap = HRESULT(STDMETHODCALLTYPE *)(
		ID3D12Device*, const D3D12_DESCRIPTOR_HEAP_DESC*, REFIID, void**);
	using CreateRootSignature = HRESULT(STDMETHODCALLTYPE *)(
		ID3D12Device*, UINT, const void*, SIZE_T, REFIID, void**);
	using CreateConstantBufferView = void(STDMETHODCALLTYPE *)(
		ID3D12Device*, const D3D12_CONSTANT_BUFFER_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
	using CreateShaderResourceView = void(STDMETHODCALLTYPE *)(
		ID3D12Device*, ID3D12Resource*, const D3D12_SHADER_RESOURCE_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
	using CreateUnorderedAccessView = void(STDMETHODCALLTYPE *)(
		ID3D12Device*, ID3D12Resource*, ID3D12Resource*, const D3D12_UNORDERED_ACCESS_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
	using CreateRenderTargetView = void(STDMETHODCALLTYPE *)(
		ID3D12Device*, ID3D12Resource*, const D3D12_RENDER_TARGET_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
	using CreateDepthStencilView = void(STDMETHODCALLTYPE *)(
		ID3D12Device*, ID3D12Resource*, const D3D12_DEPTH_STENCIL_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
	using CreateSampler = void(STDMETHODCALLTYPE *)(
		ID3D12Device*, const D3D12_SAMPLER_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
	using CopyDescriptors = void(STDMETHODCALLTYPE *)(
		ID3D12Device*, UINT, const D3D12_CPU_DESCRIPTOR_HANDLE*, const UINT*,
		UINT, const D3D12_CPU_DESCRIPTOR_HANDLE*, const UINT*, D3D12_DESCRIPTOR_HEAP_TYPE);
	using CopyDescriptorsSimple = void(STDMETHODCALLTYPE *)(
		ID3D12Device*, UINT, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_CPU_DESCRIPTOR_HANDLE,
		D3D12_DESCRIPTOR_HEAP_TYPE);
	using CreateCommittedResource = HRESULT(STDMETHODCALLTYPE *)(
		ID3D12Device*, const D3D12_HEAP_PROPERTIES*, D3D12_HEAP_FLAGS,
		const D3D12_RESOURCE_DESC*, D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE*,
		REFIID, void**);
	using CreatePlacedResource = HRESULT(STDMETHODCALLTYPE *)(
		ID3D12Device*, ID3D12Heap*, UINT64, const D3D12_RESOURCE_DESC*,
		D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE*, REFIID, void**);
	using CreateReservedResource = HRESULT(STDMETHODCALLTYPE *)(
		ID3D12Device*, const D3D12_RESOURCE_DESC*, D3D12_RESOURCE_STATES,
		const D3D12_CLEAR_VALUE*, REFIID, void**);
	using CreateFence = HRESULT(STDMETHODCALLTYPE *)(
		ID3D12Device*, UINT64, D3D12_FENCE_FLAGS, REFIID, void**);
	using CreateCommittedResource1 = HRESULT(STDMETHODCALLTYPE *)(
		ID3D12Device4*, const D3D12_HEAP_PROPERTIES*, D3D12_HEAP_FLAGS,
		const D3D12_RESOURCE_DESC*, D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE*,
		ID3D12ProtectedResourceSession*, REFIID, void**);
	using CreateReservedResource1 = HRESULT(STDMETHODCALLTYPE *)(
		ID3D12Device4*, const D3D12_RESOURCE_DESC*, D3D12_RESOURCE_STATES,
		const D3D12_CLEAR_VALUE*, ID3D12ProtectedResourceSession*, REFIID, void**);
	using CreateCommittedResource2 = HRESULT(STDMETHODCALLTYPE *)(
		ID3D12Device8*, const D3D12_HEAP_PROPERTIES*, D3D12_HEAP_FLAGS,
		const D3D12_RESOURCE_DESC1*, D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE*,
		ID3D12ProtectedResourceSession*, REFIID, void**);
	using CreatePlacedResource1 = HRESULT(STDMETHODCALLTYPE *)(
		ID3D12Device8*, ID3D12Heap*, UINT64, const D3D12_RESOURCE_DESC1*,
		D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE*, REFIID, void**);
	using CreateCommittedResource3 = HRESULT(STDMETHODCALLTYPE *)(
		ID3D12Device10*, const D3D12_HEAP_PROPERTIES*, D3D12_HEAP_FLAGS,
		const D3D12_RESOURCE_DESC1*, D3D12_BARRIER_LAYOUT, const D3D12_CLEAR_VALUE*,
		ID3D12ProtectedResourceSession*, UINT32, const DXGI_FORMAT*, REFIID, void**);
	using CreatePlacedResource2 = HRESULT(STDMETHODCALLTYPE *)(
		ID3D12Device10*, ID3D12Heap*, UINT64, const D3D12_RESOURCE_DESC1*,
		D3D12_BARRIER_LAYOUT, const D3D12_CLEAR_VALUE*, UINT32, const DXGI_FORMAT*,
		REFIID, void**);
	using CreateReservedResource2 = HRESULT(STDMETHODCALLTYPE *)(
		ID3D12Device10*, const D3D12_RESOURCE_DESC*, D3D12_BARRIER_LAYOUT,
		const D3D12_CLEAR_VALUE*, ID3D12ProtectedResourceSession*, UINT32,
		const DXGI_FORMAT*, REFIID, void**);
};

// ---- DX12ResourceTrackerHookSlotTypes.hpp ----
#line 1 "DX12ResourceTrackerHookSlotTypes.hpp"
#include <Windows.h>

class DX12ResourceTrackerHookSlotTypes
{
public:
	class ID3D12Device
	{
	public:
		static constexpr UINT CreateDescriptorHeap = 14;
		static constexpr UINT CreateRootSignature = 16;
		static constexpr UINT CreateConstantBufferView = 17;
		static constexpr UINT CreateShaderResourceView = 18;
		static constexpr UINT CreateUnorderedAccessView = 19;
		static constexpr UINT CreateRenderTargetView = 20;
		static constexpr UINT CreateDepthStencilView = 21;
		static constexpr UINT CreateSampler = 22;
		static constexpr UINT CopyDescriptors = 23;
		static constexpr UINT CopyDescriptorsSimple = 24;
		static constexpr UINT CreateCommittedResource = 27;
		static constexpr UINT CreatePlacedResource = 29;
		static constexpr UINT CreateReservedResource = 30;
		static constexpr UINT CreateFence = 36;
	};

	class ID3D12Device4
	{
	public:
		static constexpr UINT CreateCommittedResource1 = 53;
		static constexpr UINT CreateReservedResource1 = 55;
	};

	class ID3D12Device8
	{
	public:
		static constexpr UINT CreateCommittedResource2 = 69;
		static constexpr UINT CreatePlacedResource1 = 70;
	};

	class ID3D12Device10
	{
	public:
		static constexpr UINT CreateCommittedResource3 = 76;
		static constexpr UINT CreatePlacedResource2 = 77;
		static constexpr UINT CreateReservedResource2 = 78;
	};
};

// ---- DX12ResourceTrackerHookState.hpp ----
#line 1 "DX12ResourceTrackerHookState.hpp"
class DX12ResourceTrackerHookState
{
public:
	inline static DX12ResourceTrackerHookSignatures::CreateDescriptorHeap OrigCreateDescriptorHeap = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CreateRootSignature OrigCreateRootSignature = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CreateConstantBufferView OrigCreateConstantBufferView = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CreateShaderResourceView OrigCreateShaderResourceView = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CreateUnorderedAccessView OrigCreateUnorderedAccessView = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CreateRenderTargetView OrigCreateRenderTargetView = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CreateDepthStencilView OrigCreateDepthStencilView = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CreateSampler OrigCreateSampler = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CopyDescriptors OrigCopyDescriptors = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CopyDescriptorsSimple OrigCopyDescriptorsSimple = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CreateCommittedResource OrigCreateCommittedResource = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CreatePlacedResource OrigCreatePlacedResource = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CreateReservedResource OrigCreateReservedResource = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CreateFence OrigCreateFence = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CreateCommittedResource1 OrigCreateCommittedResource1 = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CreateReservedResource1 OrigCreateReservedResource1 = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CreateCommittedResource2 OrigCreateCommittedResource2 = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CreatePlacedResource1 OrigCreatePlacedResource1 = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CreateCommittedResource3 OrigCreateCommittedResource3 = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CreatePlacedResource2 OrigCreatePlacedResource2 = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CreateReservedResource2 OrigCreateReservedResource2 = nullptr;

	// Per-resource Map/Unmap originals for buffer content capture
	struct {
		void *Map = nullptr;
		void *Unmap = nullptr;
	} inline static ResourceOriginals;
};

// ---- DXGIHooks.hpp ----
#line 1 "DXGIHooks.hpp"
#include <Windows.h>
#include <dxgi1_4.h>

class DXGIHooks
{
public:
	static void Install();
	static void Shutdown();
};

// ---- MigotoConfig.hpp ----
#line 1 "MigotoConfig.hpp"
#include <windows.h>


namespace Bunny {

class LoaderConfig
{
public:
	std::wstring target;
	std::wstring module;
	std::wstring entryPoint = L"CBTProc";
	std::wstring launch;
	std::wstring workingDirectory;
	bool checkVersion = true;
	bool requireAdmin = false;
	bool waitForTarget = true;
	int hookProc = 5;
	int delay = 0;
};

class RuntimeConfig
{
public:
	bool dx12ProxyMode = true;
	bool dx12SafeMode = false;
	bool enableOverlay = false;
	std::wstring configPath;
};

class MigotoConfig {
public:
	bool Load(const wchar_t *path);

	const IniDocument &Ini() const { return mIni; }
	const LoaderConfig &Loader() const { return mLoader; }
	const RuntimeConfig &Runtime() const { return mRuntime; }

	const std::wstring &Path() const { return mPath; }
	const std::wstring &Error() const { return mError; }

public:
	void ReadLoaderConfig();
	void ReadRuntimeConfig();

	IniDocument mIni;
	LoaderConfig mLoader;
	RuntimeConfig mRuntime;
	std::wstring mPath;
	std::wstring mError;
};

class MigotoConfigLocator
{
public:
	static std::wstring FindDefaultConfigPath(HINSTANCE module);
};

}

#include <Shlwapi.h>

namespace Bunny {

class MigotoConfigReader
{
public:
static void ReadString(
	const IniDocument &ini, const wchar_t *section, const wchar_t *key, std::wstring *value)
{
	std::wstring text;
	if (ini.GetString(section, key, &text) && value)
		*value = text;
}

static void ReadBool(
	const IniDocument &ini, const wchar_t *section, const wchar_t *key, bool *value)
{
	bool parsed = false;
	if (ini.GetBool(section, key, &parsed) && value)
		*value = parsed;
}

static void ReadInt(
	const IniDocument &ini, const wchar_t *section, const wchar_t *key, int *value)
{
	int parsed = 0;
	if (ini.GetInt(section, key, &parsed) && value)
		*value = parsed;
}

};

inline bool MigotoConfig::Load(const wchar_t *path)
{
	mPath = path ? path : L"";
	mError.clear();
	mLoader = LoaderConfig();
	mRuntime = RuntimeConfig();
	mRuntime.configPath = mPath;

	if (!mIni.LoadFromFile(mPath.c_str())) {
		mError = mIni.Error();
		return false;
	}

	ReadLoaderConfig();
	ReadRuntimeConfig();
	return true;
}

inline void MigotoConfig::ReadLoaderConfig()
{
	MigotoConfigReader::ReadString(mIni, L"Loader", L"target", &mLoader.target);
	MigotoConfigReader::ReadString(mIni, L"Loader", L"module", &mLoader.module);
	MigotoConfigReader::ReadString(mIni, L"Loader", L"entry_point", &mLoader.entryPoint);
	MigotoConfigReader::ReadString(mIni, L"Loader", L"launch", &mLoader.launch);
	MigotoConfigReader::ReadString(mIni, L"Loader", L"working_dir", &mLoader.workingDirectory);
	MigotoConfigReader::ReadString(mIni, L"Loader", L"working_directory", &mLoader.workingDirectory);
	MigotoConfigReader::ReadBool(mIni, L"Loader", L"check_version", &mLoader.checkVersion);
	MigotoConfigReader::ReadBool(mIni, L"Loader", L"require_admin", &mLoader.requireAdmin);
	MigotoConfigReader::ReadBool(mIni, L"Loader", L"wait_for_target", &mLoader.waitForTarget);
	MigotoConfigReader::ReadInt(mIni, L"Loader", L"hook_proc", &mLoader.hookProc);
	MigotoConfigReader::ReadInt(mIni, L"Loader", L"delay", &mLoader.delay);
}

inline void MigotoConfig::ReadRuntimeConfig()
{
	MigotoConfigReader::ReadBool(mIni, L"DX12", L"proxy_mode", &mRuntime.dx12ProxyMode);
	MigotoConfigReader::ReadBool(mIni, L"DX12", L"safe_mode", &mRuntime.dx12SafeMode);
	MigotoConfigReader::ReadBool(mIni, L"DX12", L"overlay", &mRuntime.enableOverlay);
	MigotoConfigReader::ReadBool(mIni, L"System", L"dx12_proxy_mode", &mRuntime.dx12ProxyMode);
	MigotoConfigReader::ReadBool(mIni, L"System", L"dx12_safe_mode", &mRuntime.dx12SafeMode);
	MigotoConfigReader::ReadBool(mIni, L"System", L"dx12_overlay", &mRuntime.enableOverlay);
}

inline std::wstring MigotoConfigLocator::FindDefaultConfigPath(HINSTANCE module)
{
	wchar_t path[MAX_PATH] = {};
	wchar_t cwdPath[MAX_PATH] = {};

	if (GetCurrentDirectoryW(MAX_PATH, cwdPath)) {
		PathAppendW(cwdPath, L"d3dx.ini");
		if (PathFileExistsW(cwdPath))
			return cwdPath;
	}

	if (module && GetModuleFileNameW(module, path, MAX_PATH)) {
		PathRemoveFileSpecW(path);
		PathAppendW(path, L"d3dx.ini");
		if (PathFileExistsW(path))
			return path;
	}

	return L"d3dx.ini";
}

} // namespace Bunny

// ---- DX12Runtime.hpp ----
#line 1 "DX12Runtime.hpp"
#include <Windows.h>


class DX12Runtime
{
public:
	using LoadRealD3D12Fn = HMODULE (*)();

	inline static bool Initialized = false;
	inline static Bunny::MigotoConfig Config;
	inline static bool ConfigLoaded = false;
	inline static LoadRealD3D12Fn LoadRealD3D12 = nullptr;

	static DWORD WINAPI WorkerThread(void*)
	{
		DX12LogManager::Text("DX12 worker thread started\n");
		if (LoadRealD3D12)
			LoadRealD3D12();

		for (int i = 0; i < 200; ++i) {
			DXGIHooks::Install();
			Sleep(50);
		}

		return 0;
	}

	static void Initialize(HINSTANCE module, LoadRealD3D12Fn loadRealD3D12)
	{
		if (Initialized)
			return;
		Initialized = true;
		LoadRealD3D12 = loadRealD3D12;

		DX12RuntimeState::SetModule(module);
		DX12LogManager::Open();
		DX12Profiling::Init();
		DX12LogManager::Text(
			"\n*** 3DMigoto DX12 runtime initialized - build descriptor-churn+ia-reuse-v2 ***\n");

		std::wstring configPath = Bunny::MigotoConfigLocator::FindDefaultConfigPath(module);
		ConfigLoaded = Config.Load(configPath.c_str());
		if (ConfigLoaded) {
			DX12ModSetSafeMode(Config.Runtime().dx12SafeMode);
			DX12LogManager::JsonFunc("DX12Config",
				"\"path\":\"%S\",\"safeMode\":%s,\"overlay\":%s,\"overlayStarted\":true",
				Config.Path().c_str(),
				Config.Runtime().dx12SafeMode ? "true" : "false",
				Config.Runtime().enableOverlay ? "true" : "false");
			std::wstring modStatus;
			bool modWarnings = false;
			bool modErrors = false;
			if (!DX12ModRuntimeLoad(Config.Path().c_str(), &modStatus, &modWarnings, &modErrors))
				DX12OverlayStatusState::SetError(modStatus.c_str());
			else if (modErrors)
				DX12OverlayStatusState::SetError(modStatus.c_str());
			else if (modWarnings)
				DX12OverlayStatusState::SetWarning(modStatus.c_str());
		} else {
			DX12LogManager::JsonFunc("DX12Config",
				"\"path\":\"%S\",\"status\":\"missing_or_invalid\",\"error\":\"%S\"",
				configPath.c_str(), Config.Error().c_str());
		}
		DX12HotPathState::Update();

		// Start persistent resource/descriptor tracking queue (wrapper mode)
		DX12ResourceTrackerHooks::Install(nullptr);

		// CreateThread requires a DWORD WINAPI thread entry. Keep the entry
		// as a class method so runtime startup state stays in one module.
		HANDLE thread = CreateThread(nullptr, 0, WorkerThread, nullptr, 0, nullptr);
		if (thread)
			CloseHandle(thread);
		DX12HookRegistry::FinalizeHooks();

		DX12LogManager::EndReleaseStartupLogging();
	}

	static void Shutdown()
	{
		DX12LogManager::Text("*** 3DMigoto DX12 runtime shutting down ***\n");
		DX12NativeOverlayShutdown();
		DXGIHooks::Shutdown();
		DX12CommandListHookState::ReleaseKnownCommandQueue();
		DX12HookRegistry::UnhookAll();
		DX12LogManager::Close();
		DX12LogUtils::Close();
		LoadRealD3D12 = nullptr;
		Initialized = false;
	}
};

// ---- DX12RuntimeModeState.hpp ----
#line 1 "DX12RuntimeModeState.hpp"
inline void DX12RuntimeState::SetRuntimeMode(DX12RuntimeMode mode)
{
	const LONG value = mode == DX12RuntimeMode::Hunting ?
		static_cast<LONG>(DX12RuntimeMode::Hunting) :
		static_cast<LONG>(DX12RuntimeMode::Mod);
	const LONG old = InterlockedExchange(&runtimeMode, value);
	if (old != value) {
		DX12LogManager::JsonFunc("DX12RuntimeMode",
			"\"mode\":\"%s\"",
			mode == DX12RuntimeMode::Hunting ? "Hunting" : "Mod");
	}
}

// ---- DX12UiActions.hpp ----
#line 1 "DX12UiActions.hpp"
#include <Windows.h>


enum class DX12UiActionBits : LONG
{
	HuntToggle = 1 << 0,
	HuntPreviousVS = 1 << 1,
	HuntNextVS = 1 << 2,
	HuntPreviousPS = 1 << 3,
	HuntNextPS = 1 << 4,
	HuntPreviousCS = 1 << 5,
	HuntNextCS = 1 << 6,
	HuntPreviousIB = 1 << 7,
	HuntNextIB = 1 << 8,
	HuntPreviousVB = 1 << 9,
	HuntNextVB = 1 << 10,
	HuntCopy = 1 << 11,
	HuntReset = 1 << 12,
	FrameAnalysis = 1 << 13,
	ShaderDump = 1 << 14,
	ModReload = 1 << 15,
	Profiling = 1 << 16,
};

class DX12UiActions
{
public:
	inline static volatile LONG PendingActions = 0;

	static void Request(DX12UiActionBits action)
	{
		InterlockedOr(&PendingActions, static_cast<LONG>(action));
	}

	static void RequestHuntToggle() { Request(DX12UiActionBits::HuntToggle); }
	static void RequestHuntPreviousVS() { Request(DX12UiActionBits::HuntPreviousVS); }
	static void RequestHuntNextVS() { Request(DX12UiActionBits::HuntNextVS); }
	static void RequestHuntPreviousPS() { Request(DX12UiActionBits::HuntPreviousPS); }
	static void RequestHuntNextPS() { Request(DX12UiActionBits::HuntNextPS); }
	static void RequestHuntPreviousCS() { Request(DX12UiActionBits::HuntPreviousCS); }
	static void RequestHuntNextCS() { Request(DX12UiActionBits::HuntNextCS); }
	static void RequestHuntPreviousIB() { Request(DX12UiActionBits::HuntPreviousIB); }
	static void RequestHuntNextIB() { Request(DX12UiActionBits::HuntNextIB); }
	static void RequestHuntPreviousVB() { Request(DX12UiActionBits::HuntPreviousVB); }
	static void RequestHuntNextVB() { Request(DX12UiActionBits::HuntNextVB); }
	static void RequestHuntCopySelectedHash() { Request(DX12UiActionBits::HuntCopy); }
	static void RequestHuntResetSelection() { Request(DX12UiActionBits::HuntReset); }
	static void RequestFrameAnalysis() { Request(DX12UiActionBits::FrameAnalysis); }
	static void RequestShaderDump() { Request(DX12UiActionBits::ShaderDump); }
	static void RequestModReload() { Request(DX12UiActionBits::ModReload); }
	static void RequestProfilingToggle() { Request(DX12UiActionBits::Profiling); }

	static bool Consume()
	{
		const LONG actions = InterlockedExchange(&PendingActions, 0);
		if (!actions)
			return false;

		const bool huntToggleRequested = HasAction(actions, DX12UiActionBits::HuntToggle);

		if (huntToggleRequested)
			DX12HuntToggle();
		if (!huntToggleRequested && DX12HuntIsEnabled()) {
			if (HasAction(actions, DX12UiActionBits::HuntPreviousVS))
				DX12HuntPreviousVS();
			if (HasAction(actions, DX12UiActionBits::HuntNextVS))
				DX12HuntNextVS();
			if (HasAction(actions, DX12UiActionBits::HuntPreviousPS))
				DX12HuntPreviousPS();
			if (HasAction(actions, DX12UiActionBits::HuntNextPS))
				DX12HuntNextPS();
			if (HasAction(actions, DX12UiActionBits::HuntPreviousCS))
				DX12HuntPreviousCS();
			if (HasAction(actions, DX12UiActionBits::HuntNextCS))
				DX12HuntNextCS();
			if (HasAction(actions, DX12UiActionBits::HuntPreviousIB))
				DX12HuntPreviousIB();
			if (HasAction(actions, DX12UiActionBits::HuntNextIB))
				DX12HuntNextIB();
			if (HasAction(actions, DX12UiActionBits::HuntPreviousVB))
				DX12HuntPreviousVB();
			if (HasAction(actions, DX12UiActionBits::HuntNextVB))
				DX12HuntNextVB();
			if (HasAction(actions, DX12UiActionBits::HuntCopy))
				DX12HuntCopySelectedHash();
			if (HasAction(actions, DX12UiActionBits::HuntReset))
				DX12HuntResetSelection();
		}
		if (HasAction(actions, DX12UiActionBits::FrameAnalysis))
			RunFrameAnalysisRequest();
		if (HasAction(actions, DX12UiActionBits::ShaderDump))
			DX12RequestShaderDump();
		if (HasAction(actions, DX12UiActionBits::ModReload))
			DX12Input::RequestModReload();
		// Profiling 常驻开启，不再通过按键切换
		return true;
	}

	static bool HasAction(LONG actions, DX12UiActionBits action)
	{
		return (actions & static_cast<LONG>(action)) != 0;
	}

	static void RunFrameAnalysisRequest()
	{
		DX12LogManager::JsonFunc("DX12UiAction", "\"action\":\"FrameAnalysisRequest\"");
		if (!DX12RuntimeState::IsHuntingMode()) {
			DX12LogManager::JsonFunc("FrameAnalysisBeginFailed", "\"reason\":\"requires_hunting_mode\"");
			DX12OverlayStatusState::SetWarning(L"Enable Hunting before Frame Dump");
			return;
		}
		if (DX12FrameAnalysis::Begin()) {
			DX12FrameAnalysis::LogJsonFunc("FrameAnalysisArmed", nullptr);
			DX12FrameAnalysis::RequestCapture();
			DX12OverlayStatusState::SetStatus(L"Frame analysis armed");
		} else {
			DX12LogManager::JsonFunc("FrameAnalysisBeginFailed", "\"reason\":\"create_directory_failed\"");
			DX12OverlayStatusState::SetError(L"Frame analysis failed: cannot create directory");
		}
	}
};

// ---- DX12Wrapper.hpp ----
#line 1 "DX12Wrapper.hpp"
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>

namespace DX12Wrapper
{
	bool WrapD3D12DeviceResult(REFIID riid, void **device);
	bool WrapD3D12CommandAllocatorResult(REFIID riid, void **allocator);
	bool WrapD3D12FenceResult(REFIID riid, void **fence);
	bool WrapD3D12DescriptorHeapResult(REFIID riid, void **heap);
	bool WrapD3D12ResourceResult(REFIID riid, void **resource);
	bool WrapD3D12HeapResult(REFIID riid, void **heap);
	bool WrapD3D12PipelineStateResult(REFIID riid, void **pipelineState);
	bool WrapD3D12RootSignatureResult(REFIID riid, void **rootSignature);
	bool WrapD3D12QueryHeapResult(REFIID riid, void **queryHeap);
	bool WrapD3D12CommandSignatureResult(REFIID riid, void **commandSignature);
	bool WrapD3D12LifetimeTrackerResult(REFIID riid, void **tracker);
	bool WrapD3D12ProtectedResourceSessionResult(REFIID riid, void **session);
	bool WrapD3D12MetaCommandResult(REFIID riid, void **metaCommand);
	bool WrapD3D12StateObjectResult(REFIID riid, void **stateObject);
	bool WrapD3D12PipelineLibraryResult(REFIID riid, void **library);
	bool WrapD3D12ShaderCacheSessionResult(REFIID riid, void **session);
	bool WrapD3D12DebugInterfaceResult(REFIID riid, void **object);
	bool WrapD3D12AuxiliaryResult(REFIID riid, void **object);
	bool WrapD3D12DeviceFactoryResult(REFIID riid, void **object);
	bool WrapDXGIFactoryResult(REFIID riid, void **factory);
	bool WrapDXGISwapChainResult(REFIID riid, void **swapChain);
	IUnknown *Unwrap(IUnknown *object);
	void Shutdown();
}

// ---- DXGIHookDiagnostics.hpp ----
#line 1 "DXGIHookDiagnostics.hpp"
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>


class DXGIHookDiagnostics
{
public:
	static void LogHookCall(const char *api, const void *object)
	{
		DX12OriginalFunctionRegistry::LogHookCall(api, object);
	}

	static void LogPresentStage(
		const char *api, const char *stage, IDXGISwapChain *swapChain, HRESULT hr = S_OK)
	{
		if (!DX12LogManager::VerboseDiagnosticsEnabled())
			return;
		DX12LogManager::DebugJsonFuncFlush("DX12PresentStage",
			"\"api\":\"%s\",\"stage\":\"%s\",\"present\":%ld,\"swapchain\":\"%p\",\"hr\":\"0x%lx\"",
			api ? api : "", stage ? stage : "", DX12RuntimeState::GetPresentCount(), swapChain, hr);
	}

	static void LogPresentDeviceRemovedReason(const char *api, IDXGISwapChain *swapChain, HRESULT presentHr)
	{
		if (SUCCEEDED(presentHr) || !swapChain)
			return;

		ID3D12Device *device = nullptr;
		HRESULT queryHr = swapChain->GetDevice(IID_PPV_ARGS(&device));
		if (FAILED(queryHr) || !device) {
			DX12LogManager::DebugJsonFuncFlush("DX12DeviceRemoved",
				"\"api\":\"%s\",\"present\":%ld,\"swapchain\":\"%p\",\"presentHr\":\"0x%lx\",\"queryHr\":\"0x%lx\"",
				api ? api : "", DX12RuntimeState::GetPresentCount(), swapChain, presentHr, queryHr);
			return;
		}

		const HRESULT reason = device->GetDeviceRemovedReason();
		DX12LogManager::DebugJsonFuncFlush("DX12DeviceRemoved",
			"\"api\":\"%s\",\"present\":%ld,\"swapchain\":\"%p\",\"presentHr\":\"0x%lx\",\"reason\":\"0x%lx\"",
			api ? api : "", DX12RuntimeState::GetPresentCount(), swapChain, presentHr, reason);
		device->Release();
	}
};

// ---- DXGIHookRuntime.hpp ----
#line 1 "DXGIHookRuntime.hpp"
#include <Windows.h>
#include <dxgi1_4.h>


class DXGIHookRuntime
{
public:
	class DX12PresentSkipLogUtils
	{
	public:
		inline static volatile LONG64 SkipCalls = 0;
		inline static volatile LONG LastPresentLogged = -1;
		static constexpr LONG PresentInterval = 120;

		static void Record(const char *api, const char *stage, IDXGISwapChain *swapChain, HRESULT hr)
		{
			InterlockedIncrement64(&SkipCalls);
			const LONG present = DX12RuntimeState::GetPresentCount();
			const LONG last = InterlockedCompareExchange(&LastPresentLogged, 0, 0);
			const bool shouldLog = last < 0 || present >= last + PresentInterval;
			if (!shouldLog)
				return;
			if (InterlockedCompareExchange(&LastPresentLogged, present, last) != last)
				return;
			DX12LogManager::JsonFunc("DX12PresentHeartbeat",
				"\"api\":\"%s\",\"stage\":\"%s\",\"mode\":\"aggregate_skip\",\"present\":%ld,\"threadId\":%lu,\"swapchain\":\"%p\",\"hr\":\"0x%lx\",\"calls\":%lld",
				api ? api : "",
				stage ? stage : "",
				present,
				GetCurrentThreadId(),
				swapChain,
				hr,
				static_cast<long long>(InterlockedCompareExchange64(&SkipCalls, 0, 0)));
		}
	};

	static void RunPostPresentUi(IDXGISwapChain *swapChain)
	{
		DX12NativeOverlayRender(swapChain);
	}

	static void ConsumeUiActionsForFrame()
	{
		if (!DX12UiActions::Consume())
			return;
		DX12HotPathState::Update();
		DX12CommandListRuntime::BumpTrackingGeneration();
	}

	static void BeginFrameAnalysisCapture(LONG present)
	{
		DX12BindingTracker::BeginFrame();
		DX12FrameAnalysis::BeginCapture();
		DX12CaptureLedger::BeginCapture(static_cast<UINT64>(present));
		DX12CommandListRuntime::BumpTrackingGeneration();
		DX12FrameAnalysis::LogJsonFunc("FrameCaptureBegin", "\"present\":%ld", present);
	}

	static void BeginShaderDumpCapture()
	{
		DX12BindingTracker::BeginFrame();
		DX12CaptureLedger::BeginCapture(static_cast<UINT64>(DX12RuntimeState::GetPresentCount()));
		DX12ShaderDumpBeginCapture();
		DX12CommandListRuntime::BumpTrackingGeneration();
	}

	static void LogAndResetPerFrameCounters()
	{
		const LONG draws = DX12RuntimeState::TakeFrameDraws();
		const LONG dispatches = DX12RuntimeState::TakeFrameDispatches();
		if (DX12LogManager::VerboseDiagnosticsEnabled()) {
			DX12LogManager::JsonFunc("DX12PerFrameCounters",
				"\"draws\":%ld,\"dispatches\":%ld", draws, dispatches);
		}
	}

	static void CompleteCaptureOrBeginNext(const char *api, IDXGISwapChain *swapChain, HRESULT hr, bool dumpFrame, bool dumpShaders)
	{
		if (dumpFrame) {
			DX12FrameAnalysis::LogJsonFunc("FrameCaptureEnd",
				"\"present\":%ld", DX12RuntimeState::GetPresentCount());
			DX12BindingTracker::DrainPendingEvents("FrameCaptureEnd");
			DX12CaptureLedger::EndCapture();
			DX12DumpFrameAnalysis();
			DX12CaptureLedger::Clear();
			DX12FrameAnalysis::End();
		} else if (dumpShaders) {
			DX12BindingTracker::DrainPendingEvents("ShaderDumpEnd");
			DX12CaptureLedger::EndCapture();
			DX12DumpCapturedFrameShaders();
			DX12CaptureLedger::Clear();
		} else if (DX12FrameAnalysis::IsCaptureRequested()) {
			BeginFrameAnalysisCapture(DX12RuntimeState::GetPresentCount());
		} else if (DX12ShaderDumpIsCaptureRequested()) {
			BeginShaderDumpCapture();
		} else if (DX12ShaderDumpIsCapturingFrame() || DX12ModNeedsPreSkinningUavProbe()) {
			DX12BindingTracker::BeginFrame();
			DXGIHookDiagnostics::LogPresentStage(api, "afterBindingBeginFrame", swapChain, hr);
		}
	}

	static HRESULT Present(IDXGISwapChain *swapChain, UINT syncInterval, UINT flags)
	{
		if (!swapChain)
			return DXGI_ERROR_INVALID_CALL;
		if (DX12InternalReplayState::IsActive())
			return swapChain->Present(syncInterval, flags);

		DX12OuterTimingProbeUtils::LogPresentEnter("Present", swapChain, syncInterval, flags);
		LARGE_INTEGER sliceBegin = DX12OuterTimingProbeUtils::Now();
		ConsumeUiActionsForFrame();
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present", "consume_ui_actions", swapChain, S_OK,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));

		static volatile LONG firstPresentLog = 0;
		if (InterlockedCompareExchange(&firstPresentLog, 1, 0) == 0)
			DX12LogManager::JsonFuncFlush("DX12PresentFirst",
				"\"api\":\"Present\",\"stage\":\"enter\",\"swapchain\":\"%p\",\"sync\":%u,\"flags\":\"0x%x\",\"skipAll\":%ld",
				swapChain, syncInterval, flags, DX12HotPathState::skipAll);

		if (DX12HotPathState::skipAll) {
			DX12PresentSkipLogUtils::Record("Present", "BeforeOriginalSkip", swapChain, S_OK);
			sliceBegin = DX12OuterTimingProbeUtils::Now();
			HRESULT skipHr = swapChain->Present(syncInterval, flags);
			DX12OuterTimingProbeUtils::LogPresentSlice(
				"Present", "original_present_skip", swapChain, skipHr,
				DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
			DX12PresentSkipLogUtils::Record("Present", "AfterOriginalSkip", swapChain, skipHr);
			sliceBegin = DX12OuterTimingProbeUtils::Now();
			DX12RuntimeState::IncrementPresentCount();
			DX12HuntOnPresent();
			if (DX12RuntimeState::GetPresentCount() > 2)
				RunPostPresentUi(swapChain);
			DX12HotPathState::Update();
			DX12_HOTPATH_LOG_AND_RESET();
			DX12OuterTimingProbeUtils::LogPresentSlice(
				"Present", "post_original_skip", swapChain, skipHr,
				DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
			DX12OuterTimingProbeUtils::LogPresentLeave("Present", swapChain, skipHr);
			return skipHr;
		}

		DXGIHookDiagnostics::LogHookCall("IDXGISwapChain::Present", swapChain);
		DX12_PROFILE_SCOPE(Present);

		sliceBegin = DX12OuterTimingProbeUtils::Now();
		LogAndResetPerFrameCounters();
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present", "per_frame_counters", swapChain, S_OK,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));

		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DX12Profiling::BeginFrame();
		const bool dumpFrame = DX12FrameAnalysis::EndCapture();
		if (DX12FrameAnalysis::IsCapturing())
			DX12FrameAnalysis::LogJsonFunc("FrameCaptureContinue",
				"\"present\":%ld", DX12RuntimeState::GetPresentCount());
		const bool dumpShaders = DX12ShaderDumpEndCapture();
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present", "capture_bookkeeping", swapChain, S_OK,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
		DXGIHookDiagnostics::LogPresentStage("Present", "beforeOriginal", swapChain);
		LARGE_INTEGER tPresent0, tPresent1, freq;
		QueryPerformanceCounter(&tPresent0);
		HRESULT hr = swapChain->Present(syncInterval, flags);
		QueryPerformanceCounter(&tPresent1);
		QueryPerformanceFrequency(&freq);
		DX12LogManager::JsonFunc("DX12Perf",
			"\"scope\":\"PresentWait\",\"us\":%lld",
			static_cast<long long>((tPresent1.QuadPart - tPresent0.QuadPart) * 1000000 / freq.QuadPart));
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present", "original_present", swapChain, hr,
			static_cast<LONG64>((tPresent1.QuadPart - tPresent0.QuadPart) * 1000000 / freq.QuadPart));
		DXGIHookDiagnostics::LogPresentStage("Present", "afterOriginal", swapChain, hr);
		DXGIHookDiagnostics::LogPresentDeviceRemovedReason("Present", swapChain, hr);
		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DX12RuntimeState::IncrementPresentCount();
		DX12HuntOnPresent();
		RunPostPresentUi(swapChain);
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present", "post_original_runtime", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));

		DXGIHookDiagnostics::LogPresentStage("Present", "beforeModBeginFrame", swapChain, hr);
		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DX12ModBeginFrame(swapChain);
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present", "mod_begin_frame", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));

		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DX12HotPathState::Update();
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present", "hotpath_update", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
		DXGIHookDiagnostics::LogPresentStage("Present", "afterHotPathUpdate", swapChain, hr);

		sliceBegin = DX12OuterTimingProbeUtils::Now();
		CompleteCaptureOrBeginNext("Present", swapChain, hr, dumpFrame, dumpShaders);
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present", "capture_complete_or_begin", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DX12Profiling::EndFrame();
		DX12_HOTPATH_LOG_AND_RESET();
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present", "profiling_and_hotpath_flush", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
		DXGIHookDiagnostics::LogPresentStage("Present", "end", swapChain, hr);
		DX12OuterTimingProbeUtils::LogPresentLeave("Present", swapChain, hr);
		return hr;
	}

	static HRESULT Present1(
		IDXGISwapChain1 *swapChain, UINT syncInterval, UINT flags,
		const DXGI_PRESENT_PARAMETERS *presentParameters)
	{
		if (!swapChain)
			return DXGI_ERROR_INVALID_CALL;
		if (DX12InternalReplayState::IsActive())
			return swapChain->Present1(syncInterval, flags, presentParameters);

		DX12OuterTimingProbeUtils::LogPresentEnter("Present1", swapChain, syncInterval, flags);
		LARGE_INTEGER sliceBegin = DX12OuterTimingProbeUtils::Now();
		ConsumeUiActionsForFrame();
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present1", "consume_ui_actions", swapChain, S_OK,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));

		static volatile LONG firstPresent1Log = 0;
		if (InterlockedCompareExchange(&firstPresent1Log, 1, 0) == 0)
			DX12LogManager::JsonFuncFlush("DX12PresentFirst",
				"\"api\":\"Present1\",\"stage\":\"enter\",\"swapchain\":\"%p\",\"sync\":%u,\"flags\":\"0x%x\",\"skipAll\":%ld",
				swapChain, syncInterval, flags, DX12HotPathState::skipAll);

		if (DX12HotPathState::skipAll) {
			DX12PresentSkipLogUtils::Record("Present1", "BeforeOriginalSkip", swapChain, S_OK);
			sliceBegin = DX12OuterTimingProbeUtils::Now();
			HRESULT skipHr = swapChain->Present1(syncInterval, flags, presentParameters);
			DX12OuterTimingProbeUtils::LogPresentSlice(
				"Present1", "original_present_skip", swapChain, skipHr,
				DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
			DX12PresentSkipLogUtils::Record("Present1", "AfterOriginalSkip", swapChain, skipHr);
			sliceBegin = DX12OuterTimingProbeUtils::Now();
			DX12RuntimeState::IncrementPresentCount();
			DX12HuntOnPresent();
			if (DX12RuntimeState::GetPresentCount() > 2)
				RunPostPresentUi(swapChain);
			DX12HotPathState::Update();
			DX12_HOTPATH_LOG_AND_RESET();
			DX12OuterTimingProbeUtils::LogPresentSlice(
				"Present1", "post_original_skip", swapChain, skipHr,
				DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
			DX12OuterTimingProbeUtils::LogPresentLeave("Present1", swapChain, skipHr);
			return skipHr;
		}

		DXGIHookDiagnostics::LogHookCall("IDXGISwapChain1::Present1", swapChain);

		sliceBegin = DX12OuterTimingProbeUtils::Now();
		LogAndResetPerFrameCounters();
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present1", "per_frame_counters", swapChain, S_OK,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));

		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DX12Profiling::BeginFrame();
		const bool dumpFrame = DX12FrameAnalysis::EndCapture();
		if (DX12FrameAnalysis::IsCapturing())
			DX12FrameAnalysis::LogJsonFunc("FrameCaptureContinue",
				"\"present\":%ld", DX12RuntimeState::GetPresentCount());
		const bool dumpShaders = DX12ShaderDumpEndCapture();
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present1", "capture_bookkeeping", swapChain, S_OK,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
		DXGIHookDiagnostics::LogPresentStage("Present1", "beforeOriginal", swapChain);
		LARGE_INTEGER tPresent0, tPresent1, freq;
		QueryPerformanceCounter(&tPresent0);
		HRESULT hr = swapChain->Present1(syncInterval, flags, presentParameters);
		QueryPerformanceCounter(&tPresent1);
		QueryPerformanceFrequency(&freq);
		LONG64 presentUs = (tPresent1.QuadPart - tPresent0.QuadPart) * 1000000 / freq.QuadPart;
		DX12LogManager::JsonFunc("DX12Perf",
			"\"scope\":\"PresentWait\",\"us\":%lld", static_cast<long long>(presentUs));
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present1", "original_present", swapChain, hr, presentUs);
		DXGIHookDiagnostics::LogPresentStage("Present1", "afterOriginal", swapChain, hr);
		DXGIHookDiagnostics::LogPresentDeviceRemovedReason("Present1", swapChain, hr);
		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DX12RuntimeState::IncrementPresentCount();
		DX12HuntOnPresent();
		RunPostPresentUi(swapChain);
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present1", "post_original_runtime", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));

		DXGIHookDiagnostics::LogPresentStage("Present1", "beforeModBeginFrame", swapChain, hr);
		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DX12ModBeginFrame(swapChain);
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present1", "mod_begin_frame", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));

		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DX12HotPathState::Update();
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present1", "hotpath_update", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
		DXGIHookDiagnostics::LogPresentStage("Present1", "afterHotPathUpdate", swapChain, hr);

		sliceBegin = DX12OuterTimingProbeUtils::Now();
		CompleteCaptureOrBeginNext("Present1", swapChain, hr, dumpFrame, dumpShaders);
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present1", "capture_complete_or_begin", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DX12Profiling::EndFrame();
		DX12_HOTPATH_LOG_AND_RESET();
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present1", "profiling_and_hotpath_flush", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
		DXGIHookDiagnostics::LogPresentStage("Present1", "end", swapChain, hr);
		DX12OuterTimingProbeUtils::LogPresentLeave("Present1", swapChain, hr);
		return hr;
	}
};

// ---- DX12WrapperRuntimeUtils.hpp ----
#line 1 "DX12WrapperRuntimeUtils.hpp"
#include <Windows.h>
#include <d3d12.h>

#include <vector>


class DX12WrapperRuntimeUtils
{
public:
	static ID3D12CommandAllocator *UnwrapCommandAllocator(ID3D12CommandAllocator *allocator)
	{
		return reinterpret_cast<ID3D12CommandAllocator*>(DX12Wrapper::Unwrap(allocator));
	}

	static ID3D12CommandSignature *UnwrapCommandSignature(ID3D12CommandSignature *signature)
	{
		return reinterpret_cast<ID3D12CommandSignature*>(DX12Wrapper::Unwrap(signature));
	}

	static ID3D12CommandList *UnwrapCommandList(ID3D12CommandList *commandList)
	{
		return reinterpret_cast<ID3D12CommandList*>(DX12Wrapper::Unwrap(commandList));
	}

	static ID3D12DescriptorHeap *UnwrapDescriptorHeap(ID3D12DescriptorHeap *heap)
	{
		return reinterpret_cast<ID3D12DescriptorHeap*>(DX12Wrapper::Unwrap(heap));
	}

	static ID3D12Fence *UnwrapFence(ID3D12Fence *fence)
	{
		return reinterpret_cast<ID3D12Fence*>(DX12Wrapper::Unwrap(fence));
	}

	static ID3D12GraphicsCommandList *UnwrapGraphicsCommandList(ID3D12GraphicsCommandList *commandList)
	{
		return reinterpret_cast<ID3D12GraphicsCommandList*>(DX12Wrapper::Unwrap(commandList));
	}

	static ID3D12Heap *UnwrapHeap(ID3D12Heap *heap)
	{
		return reinterpret_cast<ID3D12Heap*>(DX12Wrapper::Unwrap(heap));
	}

	static ID3D12MetaCommand *UnwrapMetaCommand(ID3D12MetaCommand *metaCommand)
	{
		return reinterpret_cast<ID3D12MetaCommand*>(DX12Wrapper::Unwrap(metaCommand));
	}

	static ID3D12Pageable *UnwrapPageable(ID3D12Pageable *pageable)
	{
		return reinterpret_cast<ID3D12Pageable*>(DX12Wrapper::Unwrap(pageable));
	}

	static ID3D12PipelineState *UnwrapPipelineState(ID3D12PipelineState *pipelineState)
	{
		DX12_HOTPATH_TIME_BEGIN(UnwrapObjTotal);
		ID3D12PipelineState *result =
			reinterpret_cast<ID3D12PipelineState*>(DX12Wrapper::Unwrap(pipelineState));
		DX12_HOTPATH_TIME_END(UnwrapObjTotal);
		return result;
	}

	static ID3D12ProtectedResourceSession *UnwrapProtectedResourceSession(
		ID3D12ProtectedResourceSession *session)
	{
		return reinterpret_cast<ID3D12ProtectedResourceSession*>(DX12Wrapper::Unwrap(session));
	}

	static ID3D12QueryHeap *UnwrapQueryHeap(ID3D12QueryHeap *heap)
	{
		return reinterpret_cast<ID3D12QueryHeap*>(DX12Wrapper::Unwrap(heap));
	}

	static ID3D12Resource *UnwrapResource(ID3D12Resource *resource)
	{
		return reinterpret_cast<ID3D12Resource*>(DX12Wrapper::Unwrap(resource));
	}

	static ID3D12RootSignature *UnwrapRootSignature(ID3D12RootSignature *rootSignature)
	{
		return reinterpret_cast<ID3D12RootSignature*>(DX12Wrapper::Unwrap(rootSignature));
	}

	static ID3D12StateObject *UnwrapStateObject(ID3D12StateObject *stateObject)
	{
		return reinterpret_cast<ID3D12StateObject*>(DX12Wrapper::Unwrap(stateObject));
	}

	static const D3D12_GRAPHICS_PIPELINE_STATE_DESC *UnwrapGraphicsPipelineStateDesc(
		const D3D12_GRAPHICS_PIPELINE_STATE_DESC *source,
		D3D12_GRAPHICS_PIPELINE_STATE_DESC *storage)
	{
		if (!source || !storage)
			return source;
		*storage = *source;
		storage->pRootSignature = UnwrapRootSignature(source->pRootSignature);
		return storage;
	}

	static const D3D12_COMPUTE_PIPELINE_STATE_DESC *UnwrapComputePipelineStateDesc(
		const D3D12_COMPUTE_PIPELINE_STATE_DESC *source,
		D3D12_COMPUTE_PIPELINE_STATE_DESC *storage)
	{
		if (!source || !storage)
			return source;
		*storage = *source;
		storage->pRootSignature = UnwrapRootSignature(source->pRootSignature);
		return storage;
	}

	static void UnwrapStateObjectDesc(
		const D3D12_STATE_OBJECT_DESC *source,
		D3D12_STATE_OBJECT_DESC *descStorage,
		std::vector<D3D12_STATE_SUBOBJECT> *subobjectStorage,
		std::vector<D3D12_GLOBAL_ROOT_SIGNATURE> *globalRootSignatureStorage,
		std::vector<D3D12_LOCAL_ROOT_SIGNATURE> *localRootSignatureStorage,
		std::vector<D3D12_EXISTING_COLLECTION_DESC> *existingCollectionStorage,
		std::vector<D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION> *associationStorage,
		const D3D12_STATE_OBJECT_DESC **result)
	{
		if (!result)
			return;
		*result = source;
		if (!source || !descStorage || !subobjectStorage || !globalRootSignatureStorage ||
			!localRootSignatureStorage || !existingCollectionStorage || !associationStorage ||
			!source->pSubobjects || !source->NumSubobjects)
			return;

		*descStorage = *source;
		subobjectStorage->assign(source->pSubobjects, source->pSubobjects + source->NumSubobjects);
		globalRootSignatureStorage->clear();
		localRootSignatureStorage->clear();
		existingCollectionStorage->clear();
		associationStorage->clear();
		globalRootSignatureStorage->resize(source->NumSubobjects);
		localRootSignatureStorage->resize(source->NumSubobjects);
		existingCollectionStorage->resize(source->NumSubobjects);
		associationStorage->resize(source->NumSubobjects);

		for (UINT i = 0; i < source->NumSubobjects; ++i) {
			const D3D12_STATE_SUBOBJECT *sourceSubobject = &source->pSubobjects[i];
			D3D12_STATE_SUBOBJECT *activeSubobject = &(*subobjectStorage)[i];
			activeSubobject->pDesc = sourceSubobject->pDesc;

			if (!sourceSubobject->pDesc)
				continue;

			switch (sourceSubobject->Type) {
			case D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE:
				(*globalRootSignatureStorage)[i] =
					*reinterpret_cast<const D3D12_GLOBAL_ROOT_SIGNATURE*>(sourceSubobject->pDesc);
				(*globalRootSignatureStorage)[i].pGlobalRootSignature =
					UnwrapRootSignature((*globalRootSignatureStorage)[i].pGlobalRootSignature);
				activeSubobject->pDesc = &(*globalRootSignatureStorage)[i];
				break;
			case D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE:
				(*localRootSignatureStorage)[i] =
					*reinterpret_cast<const D3D12_LOCAL_ROOT_SIGNATURE*>(sourceSubobject->pDesc);
				(*localRootSignatureStorage)[i].pLocalRootSignature =
					UnwrapRootSignature((*localRootSignatureStorage)[i].pLocalRootSignature);
				activeSubobject->pDesc = &(*localRootSignatureStorage)[i];
				break;
			case D3D12_STATE_SUBOBJECT_TYPE_EXISTING_COLLECTION:
				(*existingCollectionStorage)[i] =
					*reinterpret_cast<const D3D12_EXISTING_COLLECTION_DESC*>(sourceSubobject->pDesc);
				(*existingCollectionStorage)[i].pExistingCollection =
					UnwrapStateObject((*existingCollectionStorage)[i].pExistingCollection);
				activeSubobject->pDesc = &(*existingCollectionStorage)[i];
				break;
			case D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION:
				(*associationStorage)[i] =
					*reinterpret_cast<const D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION*>(sourceSubobject->pDesc);
				for (UINT associatedIndex = 0; associatedIndex < source->NumSubobjects; ++associatedIndex) {
					if ((*associationStorage)[i].pSubobjectToAssociate == &source->pSubobjects[associatedIndex]) {
						(*associationStorage)[i].pSubobjectToAssociate = &(*subobjectStorage)[associatedIndex];
						break;
					}
				}
				activeSubobject->pDesc = &(*associationStorage)[i];
				break;
			default:
				break;
			}
		}

		descStorage->pSubobjects = subobjectStorage->data();
		*result = descStorage;
	}

	static void UnwrapResourceArray(
		ID3D12Resource *const *source,
		UINT count,
		std::vector<ID3D12Resource*> *storage,
		ID3D12Resource *const **result)
	{
		if (!result)
			return;
		*result = source;
		if (!source || !storage || !count)
			return;
		storage->resize(count);
		for (UINT i = 0; i < count; ++i)
			(*storage)[i] = UnwrapResource(source[i]);
		*result = storage->data();
	}

	static void UnwrapTextureCopyLocation(
		const D3D12_TEXTURE_COPY_LOCATION *source,
		D3D12_TEXTURE_COPY_LOCATION *storage,
		const D3D12_TEXTURE_COPY_LOCATION **result)
	{
		if (!result)
			return;
		*result = source;
		if (!source || !storage)
			return;
		*storage = *source;
		storage->pResource = UnwrapResource(source->pResource);
		*result = storage;
	}

	static void UnwrapResourceBarrier(
		const D3D12_RESOURCE_BARRIER *source,
		D3D12_RESOURCE_BARRIER *storage)
	{
		if (!source || !storage)
			return;
		*storage = *source;
		switch (storage->Type) {
		case D3D12_RESOURCE_BARRIER_TYPE_TRANSITION:
			storage->Transition.pResource = UnwrapResource(source->Transition.pResource);
			break;
		case D3D12_RESOURCE_BARRIER_TYPE_ALIASING:
			storage->Aliasing.pResourceBefore = UnwrapResource(source->Aliasing.pResourceBefore);
			storage->Aliasing.pResourceAfter = UnwrapResource(source->Aliasing.pResourceAfter);
			break;
		case D3D12_RESOURCE_BARRIER_TYPE_UAV:
			storage->UAV.pResource = UnwrapResource(source->UAV.pResource);
			break;
		default:
			break;
		}
	}

	static void UnwrapResourceBarrierArray(
		const D3D12_RESOURCE_BARRIER *source,
		UINT count,
		std::vector<D3D12_RESOURCE_BARRIER> *storage,
		const D3D12_RESOURCE_BARRIER **result)
	{
		if (!result)
			return;
		*result = source;
		if (!source || !storage || !count)
			return;
		DX12ResidentMaintenanceTelemetryUtils::RecordBarrierUnwrap(count);
		storage->resize(count);
		for (UINT i = 0; i < count; ++i)
			UnwrapResourceBarrier(&source[i], &(*storage)[i]);
		*result = storage->data();
	}

	static void UnwrapBarrierGroups(
		const D3D12_BARRIER_GROUP *source,
		UINT32 count,
		std::vector<D3D12_BARRIER_GROUP> *groupStorage,
		std::vector<std::vector<D3D12_TEXTURE_BARRIER>> *textureStorage,
		std::vector<std::vector<D3D12_BUFFER_BARRIER>> *bufferStorage,
		const D3D12_BARRIER_GROUP **result)
	{
		if (!result)
			return;
		*result = source;
		if (!source || !groupStorage || !textureStorage || !bufferStorage || !count)
			return;

		groupStorage->resize(count);
		textureStorage->clear();
		bufferStorage->clear();
		textureStorage->resize(count);
		bufferStorage->resize(count);

		for (UINT32 groupIndex = 0; groupIndex < count; ++groupIndex) {
			(*groupStorage)[groupIndex] = source[groupIndex];
			if (source[groupIndex].Type == D3D12_BARRIER_TYPE_TEXTURE &&
				source[groupIndex].pTextureBarriers &&
				source[groupIndex].NumBarriers) {
				std::vector<D3D12_TEXTURE_BARRIER> &textures = (*textureStorage)[groupIndex];
				textures.assign(
					source[groupIndex].pTextureBarriers,
					source[groupIndex].pTextureBarriers + source[groupIndex].NumBarriers);
				for (UINT32 barrierIndex = 0; barrierIndex < source[groupIndex].NumBarriers; ++barrierIndex)
					textures[barrierIndex].pResource =
						UnwrapResource(source[groupIndex].pTextureBarriers[barrierIndex].pResource);
				(*groupStorage)[groupIndex].pTextureBarriers = textures.data();
			} else if (source[groupIndex].Type == D3D12_BARRIER_TYPE_BUFFER &&
				source[groupIndex].pBufferBarriers &&
				source[groupIndex].NumBarriers) {
				std::vector<D3D12_BUFFER_BARRIER> &buffers = (*bufferStorage)[groupIndex];
				buffers.assign(
					source[groupIndex].pBufferBarriers,
					source[groupIndex].pBufferBarriers + source[groupIndex].NumBarriers);
				for (UINT32 barrierIndex = 0; barrierIndex < source[groupIndex].NumBarriers; ++barrierIndex)
					buffers[barrierIndex].pResource =
						UnwrapResource(source[groupIndex].pBufferBarriers[barrierIndex].pResource);
				(*groupStorage)[groupIndex].pBufferBarriers = buffers.data();
			}
		}
		*result = groupStorage->data();
	}

	static void LogCommandListStage(
		const char *method,
		const char *stage,
		ID3D12GraphicsCommandList *commandList)
	{
#if defined(_DEBUG)
		if (!DX12LogManager::ShouldLogHookCall(method))
			return;
		DX12LogManager::JsonFunc("DX12GraphicsCommandListWrapper",
			"\"method\":\"%s\",\"stage\":\"%s\",\"commandList\":\"%p\",\"threadId\":%lu",
			method ? method : "",
			stage ? stage : "",
			commandList,
			GetCurrentThreadId());
#else
		(void)method;
		(void)stage;
		(void)commandList;
#endif
	}

	static void STDMETHODCALLTYPE DirectQueueExecuteCommandLists(
		ID3D12CommandQueue *queue,
		UINT numCommandLists,
		ID3D12CommandList *const *commandLists)
	{
		if (queue)
			queue->ExecuteCommandLists(numCommandLists, commandLists);
	}

	static void ExecuteCommandListsDirectNative(
		ID3D12CommandQueue *queue,
		UINT numCommandLists,
		ID3D12CommandList *const *commandLists)
	{
		ID3D12CommandList *stackUnwrapped[8];
		std::vector<ID3D12CommandList*> heapUnwrapped;
		ID3D12CommandList *const *activeCommandLists = commandLists;
		if (commandLists && numCommandLists) {
			ID3D12CommandList **unwrapped = stackUnwrapped;
			if (numCommandLists > ARRAYSIZE(stackUnwrapped)) {
				heapUnwrapped.resize(numCommandLists);
				unwrapped = heapUnwrapped.data();
			}
			for (UINT i = 0; i < numCommandLists; ++i)
				unwrapped[i] = UnwrapCommandList(commandLists[i]);
			activeCommandLists = unwrapped;
		}
		DirectQueueExecuteCommandLists(queue, numCommandLists, activeCommandLists);
	}

	static void STDMETHODCALLTYPE DirectDrawInstanced(
		ID3D12GraphicsCommandList *commandList,
		UINT vertexCountPerInstance,
		UINT instanceCount,
		UINT startVertexLocation,
		UINT startInstanceLocation)
	{
		if (commandList)
			commandList->DrawInstanced(
				vertexCountPerInstance, instanceCount, startVertexLocation, startInstanceLocation);
	}

	static void STDMETHODCALLTYPE DirectDrawIndexedInstanced(
		ID3D12GraphicsCommandList *commandList,
		UINT indexCountPerInstance,
		UINT instanceCount,
		UINT startIndexLocation,
		INT baseVertexLocation,
		UINT startInstanceLocation)
	{
		if (commandList)
			commandList->DrawIndexedInstanced(
				indexCountPerInstance, instanceCount, startIndexLocation,
				baseVertexLocation, startInstanceLocation);
	}

	static void STDMETHODCALLTYPE DirectDispatch(
		ID3D12GraphicsCommandList *commandList,
		UINT threadGroupCountX,
		UINT threadGroupCountY,
		UINT threadGroupCountZ)
	{
		if (commandList)
			commandList->Dispatch(threadGroupCountX, threadGroupCountY, threadGroupCountZ);
	}

	static void STDMETHODCALLTYPE DirectExecuteIndirect(
		ID3D12GraphicsCommandList *commandList,
		ID3D12CommandSignature *commandSignature,
		UINT maxCommandCount,
		ID3D12Resource *argumentBuffer,
		UINT64 argumentBufferOffset,
		ID3D12Resource *countBuffer,
		UINT64 countBufferOffset)
	{
		if (commandList)
			commandList->ExecuteIndirect(
				commandSignature, maxCommandCount,
				argumentBuffer, argumentBufferOffset,
				countBuffer, countBufferOffset);
	}

	static void STDMETHODCALLTYPE DirectSetIndexBuffer(
		ID3D12GraphicsCommandList *commandList,
		const D3D12_INDEX_BUFFER_VIEW *view)
	{
		if (commandList)
			commandList->IASetIndexBuffer(view);
	}

	static void STDMETHODCALLTYPE DirectSetVertexBuffers(
		ID3D12GraphicsCommandList *commandList,
		UINT startSlot,
		UINT count,
		const D3D12_VERTEX_BUFFER_VIEW *views)
	{
		if (commandList)
			commandList->IASetVertexBuffers(startSlot, count, views);
	}

	static DX12IaReplacementExecutorCallbacks MakeDirectIaReplacementCallbacks()
	{
		DX12IaReplacementExecutorCallbacks callbacks;
		callbacks.drawInstanced = DirectDrawInstanced;
		callbacks.drawIndexedInstanced = DirectDrawIndexedInstanced;
		callbacks.dispatch = DirectDispatch;
		callbacks.setIndexBuffer = DirectSetIndexBuffer;
		callbacks.setVertexBuffers = DirectSetVertexBuffers;
		callbacks.recordReplacementDraw = DX12CommandListHookDiagnostics::RecordIaReplacementDrawExecute;
		callbacks.recordReplacementDispatch = DX12CommandListHookDiagnostics::RecordIaReplacementDispatchExecute;
		return callbacks;
	}

	static void RegisterSubmittedCommandListDirect(IUnknown *commandList)
	{
		if (!commandList)
			return;

		ID3D12GraphicsCommandList *baseList =
			reinterpret_cast<ID3D12GraphicsCommandList*>(commandList);
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetDirectStatePtr(baseList);
		if (!runtimeState)
			return;
		(void)runtimeState;

		if (DX12HuntIsEnabled())
			DX12HuntPublishCommandList(baseList);
		if (DX12CaptureLedger::CurrentCaptureSessionId() != 0)
			DX12BindingTracker::SubmitCommandList(baseList);
	}

	static HRESULT PresentDirect(
		IDXGISwapChain *swapChain,
		UINT syncInterval,
		UINT flags)
	{
		if (!swapChain)
			return DXGI_ERROR_INVALID_CALL;
		if (DX12InternalReplayState::IsActive())
			return swapChain->Present(syncInterval, flags);

		DX12OuterTimingProbeUtils::LogPresentEnter("PresentWrapper", swapChain, syncInterval, flags);
		LARGE_INTEGER sliceBegin = DX12OuterTimingProbeUtils::Now();
		DXGIHookRuntime::ConsumeUiActionsForFrame();
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"PresentWrapper", "consume_ui_actions", swapChain, S_OK,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));

		static volatile LONG firstPresentLog = 0;
		if (InterlockedCompareExchange(&firstPresentLog, 1, 0) == 0)
			DX12LogManager::JsonFuncFlush("DX12PresentFirst",
				"\"api\":\"Present\",\"stage\":\"wrapper_enter\",\"swapchain\":\"%p\",\"sync\":%u,\"flags\":\"0x%x\",\"skipAll\":%ld",
				swapChain, syncInterval, flags, DX12HotPathState::skipAll);

		if (DX12HotPathState::skipAll) {
			sliceBegin = DX12OuterTimingProbeUtils::Now();
			HRESULT skipHr = swapChain->Present(syncInterval, flags);
			DX12OuterTimingProbeUtils::LogPresentSlice(
				"PresentWrapper", "original_present_skip", swapChain, skipHr,
				DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
			sliceBegin = DX12OuterTimingProbeUtils::Now();
			DX12RuntimeState::IncrementPresentCount();
			DX12HuntOnPresent();
			if (DX12RuntimeState::GetPresentCount() > 2)
				DXGIHookRuntime::RunPostPresentUi(swapChain);
			DX12HotPathState::Update();
			DX12_HOTPATH_LOG_AND_RESET();
			DX12OuterTimingProbeUtils::LogPresentSlice(
				"PresentWrapper", "post_original_skip", swapChain, skipHr,
				DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
			DX12OuterTimingProbeUtils::LogPresentLeave("PresentWrapper", swapChain, skipHr);
			return skipHr;
		}

		DX12_HOTPATH_TIME_BEGIN(PresentTotal);
		DXGIHookDiagnostics::LogHookCall("IDXGISwapChain::Present", swapChain);
		DX12_PROFILE_SCOPE(Present);

		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DXGIHookRuntime::LogAndResetPerFrameCounters();
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"PresentWrapper", "per_frame_counters", swapChain, S_OK,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));

		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DX12Profiling::BeginFrame();
		const bool dumpFrame = DX12FrameAnalysis::EndCapture();
		if (DX12FrameAnalysis::IsCapturing())
			DX12FrameAnalysis::LogJsonFunc("FrameCaptureContinue",
				"\"present\":%ld", DX12RuntimeState::GetPresentCount());
		const bool dumpShaders = DX12ShaderDumpEndCapture();
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"PresentWrapper", "capture_bookkeeping", swapChain, S_OK,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
		DXGIHookDiagnostics::LogPresentStage("Present", "wrapperBeforeOriginal", swapChain);
		LARGE_INTEGER tPresent0;
		LARGE_INTEGER tPresent1;
		LARGE_INTEGER freq;
		QueryPerformanceCounter(&tPresent0);
		HRESULT hr = swapChain->Present(syncInterval, flags);
		QueryPerformanceCounter(&tPresent1);
		QueryPerformanceFrequency(&freq);
		DX12LogManager::JsonFunc("DX12Perf",
			"\"scope\":\"PresentWait\",\"us\":%lld",
			static_cast<long long>((tPresent1.QuadPart - tPresent0.QuadPart) * 1000000 / freq.QuadPart));
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"PresentWrapper", "original_present", swapChain, hr,
			static_cast<LONG64>((tPresent1.QuadPart - tPresent0.QuadPart) * 1000000 / freq.QuadPart));
		DXGIHookDiagnostics::LogPresentStage("Present", "wrapperAfterOriginal", swapChain, hr);
		DXGIHookDiagnostics::LogPresentDeviceRemovedReason("Present", swapChain, hr);
		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DX12RuntimeState::IncrementPresentCount();
		DX12HuntOnPresent();
		DXGIHookRuntime::RunPostPresentUi(swapChain);
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"PresentWrapper", "post_original_runtime", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));

		DXGIHookDiagnostics::LogPresentStage("Present", "wrapperBeforeModBeginFrame", swapChain, hr);
		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DX12ModBeginFrame(swapChain);
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"PresentWrapper", "mod_begin_frame", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));

		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DX12HotPathState::Update();
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"PresentWrapper", "hotpath_update", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DXGIHookRuntime::CompleteCaptureOrBeginNext("Present", swapChain, hr, dumpFrame, dumpShaders);
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"PresentWrapper", "capture_complete_or_begin", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DX12Profiling::EndFrame();
		DX12_HOTPATH_TIME_END(PresentTotal);
		DX12_HOTPATH_LOG_AND_RESET();
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"PresentWrapper", "profiling_and_hotpath_flush", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
		DXGIHookDiagnostics::LogPresentStage("Present", "wrapperEnd", swapChain, hr);
		DX12OuterTimingProbeUtils::LogPresentLeave("PresentWrapper", swapChain, hr);
		return hr;
	}

	static HRESULT Present1Direct(
		IDXGISwapChain1 *swapChain,
		UINT syncInterval,
		UINT flags,
		const DXGI_PRESENT_PARAMETERS *presentParameters)
	{
		if (!swapChain)
			return DXGI_ERROR_INVALID_CALL;
		if (DX12InternalReplayState::IsActive())
			return swapChain->Present1(syncInterval, flags, presentParameters);

		DX12OuterTimingProbeUtils::LogPresentEnter("Present1Wrapper", swapChain, syncInterval, flags);
		LARGE_INTEGER sliceBegin = DX12OuterTimingProbeUtils::Now();
		DXGIHookRuntime::ConsumeUiActionsForFrame();
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present1Wrapper", "consume_ui_actions", swapChain, S_OK,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));

		static volatile LONG firstPresent1Log = 0;
		if (InterlockedCompareExchange(&firstPresent1Log, 1, 0) == 0)
			DX12LogManager::JsonFuncFlush("DX12PresentFirst",
				"\"api\":\"Present1\",\"stage\":\"wrapper_enter\",\"swapchain\":\"%p\",\"sync\":%u,\"flags\":\"0x%x\",\"skipAll\":%ld",
				swapChain, syncInterval, flags, DX12HotPathState::skipAll);

		if (DX12HotPathState::skipAll) {
			sliceBegin = DX12OuterTimingProbeUtils::Now();
			HRESULT skipHr = swapChain->Present1(syncInterval, flags, presentParameters);
			DX12OuterTimingProbeUtils::LogPresentSlice(
				"Present1Wrapper", "original_present_skip", swapChain, skipHr,
				DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
			sliceBegin = DX12OuterTimingProbeUtils::Now();
			DX12RuntimeState::IncrementPresentCount();
			DX12HuntOnPresent();
			if (DX12RuntimeState::GetPresentCount() > 2)
				DXGIHookRuntime::RunPostPresentUi(swapChain);
			DX12HotPathState::Update();
			DX12_HOTPATH_LOG_AND_RESET();
			DX12OuterTimingProbeUtils::LogPresentSlice(
				"Present1Wrapper", "post_original_skip", swapChain, skipHr,
				DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
			DX12OuterTimingProbeUtils::LogPresentLeave("Present1Wrapper", swapChain, skipHr);
			return skipHr;
		}

		DXGIHookDiagnostics::LogHookCall("IDXGISwapChain1::Present1", swapChain);
		DX12_PROFILE_SCOPE(Present);

		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DXGIHookRuntime::LogAndResetPerFrameCounters();
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present1Wrapper", "per_frame_counters", swapChain, S_OK,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));

		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DX12Profiling::BeginFrame();
		const bool dumpFrame = DX12FrameAnalysis::EndCapture();
		if (DX12FrameAnalysis::IsCapturing())
			DX12FrameAnalysis::LogJsonFunc("FrameCaptureContinue",
				"\"present\":%ld", DX12RuntimeState::GetPresentCount());
		const bool dumpShaders = DX12ShaderDumpEndCapture();
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present1Wrapper", "capture_bookkeeping", swapChain, S_OK,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
		DXGIHookDiagnostics::LogPresentStage("Present1", "wrapperBeforeOriginal", swapChain);
		LARGE_INTEGER tPresent0;
		LARGE_INTEGER tPresent1;
		LARGE_INTEGER freq;
		QueryPerformanceCounter(&tPresent0);
		HRESULT hr = swapChain->Present1(syncInterval, flags, presentParameters);
		QueryPerformanceCounter(&tPresent1);
		QueryPerformanceFrequency(&freq);
		DX12LogManager::JsonFunc("DX12Perf",
			"\"scope\":\"PresentWait\",\"us\":%lld",
			static_cast<long long>((tPresent1.QuadPart - tPresent0.QuadPart) * 1000000 / freq.QuadPart));
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present1Wrapper", "original_present", swapChain, hr,
			static_cast<LONG64>((tPresent1.QuadPart - tPresent0.QuadPart) * 1000000 / freq.QuadPart));
		DXGIHookDiagnostics::LogPresentStage("Present1", "wrapperAfterOriginal", swapChain, hr);
		DXGIHookDiagnostics::LogPresentDeviceRemovedReason("Present1", swapChain, hr);
		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DX12RuntimeState::IncrementPresentCount();
		DX12HuntOnPresent();
		DXGIHookRuntime::RunPostPresentUi(swapChain);
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present1Wrapper", "post_original_runtime", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));

		DXGIHookDiagnostics::LogPresentStage("Present1", "wrapperBeforeModBeginFrame", swapChain, hr);
		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DX12ModBeginFrame(swapChain);
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present1Wrapper", "mod_begin_frame", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));

		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DX12HotPathState::Update();
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present1Wrapper", "hotpath_update", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DXGIHookRuntime::CompleteCaptureOrBeginNext("Present1", swapChain, hr, dumpFrame, dumpShaders);
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present1Wrapper", "capture_complete_or_begin", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DX12Profiling::EndFrame();
		DX12_HOTPATH_LOG_AND_RESET();
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present1Wrapper", "profiling_and_hotpath_flush", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
		DXGIHookDiagnostics::LogPresentStage("Present1", "wrapperEnd", swapChain, hr);
		DX12OuterTimingProbeUtils::LogPresentLeave("Present1Wrapper", swapChain, hr);
		return hr;
	}

	static HRESULT Present(
		IDXGISwapChain *swapChain,
		UINT syncInterval,
		UINT flags)
	{
		return PresentDirect(swapChain, syncInterval, flags);
	}

	static HRESULT Present1(
		IDXGISwapChain1 *swapChain,
		UINT syncInterval,
		UINT flags,
		const DXGI_PRESENT_PARAMETERS *presentParameters)
	{
		return Present1Direct(swapChain, syncInterval, flags, presentParameters);
	}

	static HRESULT ResizeBuffers(
		IDXGISwapChain *swapChain,
		UINT bufferCount,
		UINT width,
		UINT height,
		DXGI_FORMAT newFormat,
		UINT swapChainFlags)
	{
		if (!swapChain)
			return DXGI_ERROR_INVALID_CALL;
		if (!DX12InternalReplayState::IsActive())
			DX12NativeOverlayInvalidate(swapChain);
		HRESULT hr = swapChain->ResizeBuffers(bufferCount, width, height, newFormat, swapChainFlags);
		DX12LogManager::JsonFunc("IDXGISwapChain::ResizeBuffers",
			"\"swapchain\":\"%p\",\"buffers\":%u,\"width\":%u,\"height\":%u,\"format\":%u,\"flags\":\"0x%x\",\"hr\":\"0x%lx\"",
			swapChain, bufferCount, width, height, static_cast<UINT>(newFormat), swapChainFlags, hr);
		return hr;
	}

	static void ExecuteCommandLists(
		ID3D12CommandQueue *queue,
		UINT numCommandLists,
		ID3D12CommandList *const *commandLists,
		DX12CommandListHookSignatures::QueueExecuteCommandLists original)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiExecuteCommandLists);
		DX12_HOTPATH_TIME_BEGIN(ExecuteCLTotal);
		LARGE_INTEGER totalBegin = DX12OuterTimingProbeUtils::Now();
		LARGE_INTEGER unwrapBegin = DX12OuterTimingProbeUtils::Now();
		// Stack buffer for common case (≤8 command lists), fallback to heap.
		ID3D12CommandList *stackUnwrapped[8];
		ID3D12CommandList *const *activeCommandLists = commandLists;
		std::vector<ID3D12CommandList*> heapUnwrapped;
		if (commandLists && numCommandLists) {
			ID3D12CommandList **dest = stackUnwrapped;
			if (numCommandLists > 8) {
				heapUnwrapped.resize(numCommandLists);
				dest = heapUnwrapped.data();
			}
			for (UINT i = 0; i < numCommandLists; ++i)
				dest[i] = UnwrapCommandList(commandLists[i]);
			activeCommandLists = dest;
		}
		DX12OuterTimingProbeUtils::LogQueueExecuteSlice(
			"unwrap_command_lists",
			queue,
			numCommandLists,
			DX12OuterTimingProbeUtils::ElapsedUs(unwrapBegin));
		DX12CommandQueueExecutionFlow::Execute(
			queue,
			numCommandLists,
			activeCommandLists,
			original ? original : DirectQueueExecuteCommandLists,
			RegisterSubmittedCommandListDirect);
		DX12OuterTimingProbeUtils::LogQueueExecuteSlice(
			"wrapper_total",
			queue,
			numCommandLists,
			DX12OuterTimingProbeUtils::ElapsedUs(totalBegin));
		DX12_HOTPATH_TIME_END(ExecuteCLTotal);
	}

	static void DrawInstanced(
		ID3D12GraphicsCommandList *commandList,
		UINT vertexCountPerInstance,
		UINT instanceCount,
		UINT startVertexLocation,
		UINT startInstanceLocation,
		DX12CommandListHookSignatures::DrawInstanced original)
	{
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12WrapperRuntimeUtils::DrawInstanced(
			commandList,
			runtimeState,
			vertexCountPerInstance,
			instanceCount,
			startVertexLocation,
			startInstanceLocation,
			original);
	}

	static void DrawInstanced(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeState *runtimeState,
		UINT vertexCountPerInstance,
		UINT instanceCount,
		UINT startVertexLocation,
		UINT startInstanceLocation,
		DX12CommandListHookSignatures::DrawInstanced original)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiDrawInstanced);
		// Use pre-hook original as callback to bypass vtable hook.
		// Falls back to DirectDrawInstanced (hooked vtable) only if original is null.
		DX12IaReplacementExecutorCallbacks callbacks;
		callbacks.drawInstanced = original ? reinterpret_cast<DX12IaCommandSignatures::DrawInstanced>(original) : DirectDrawInstanced;
		callbacks.drawIndexedInstanced = DirectDrawIndexedInstanced;
		callbacks.dispatch = DirectDispatch;
		callbacks.setIndexBuffer = DirectSetIndexBuffer;
		callbacks.setVertexBuffers = DirectSetVertexBuffers;
		callbacks.recordReplacementDraw = DX12CommandListHookDiagnostics::RecordIaReplacementDrawExecute;
		callbacks.recordReplacementDispatch = DX12CommandListHookDiagnostics::RecordIaReplacementDispatchExecute;
		DX12IaDrawInvocation draw = DX12IaDrawInvocation::ForDrawInstanced(
			vertexCountPerInstance, instanceCount, startVertexLocation, startInstanceLocation);
		DX12DrawHookFlow::ExecuteHook(commandList, runtimeState, draw, callbacks);
	}

	static void DrawIndexedInstanced(
		ID3D12GraphicsCommandList *commandList,
		UINT indexCountPerInstance,
		UINT instanceCount,
		UINT startIndexLocation,
		INT baseVertexLocation,
		UINT startInstanceLocation,
		DX12CommandListHookSignatures::DrawIndexedInstanced original)
	{
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12WrapperRuntimeUtils::DrawIndexedInstanced(
			commandList,
			runtimeState,
			indexCountPerInstance,
			instanceCount,
			startIndexLocation,
			baseVertexLocation,
			startInstanceLocation,
			original);
	}

	static void DrawIndexedInstanced(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeState *runtimeState,
		UINT indexCountPerInstance,
		UINT instanceCount,
		UINT startIndexLocation,
		INT baseVertexLocation,
		UINT startInstanceLocation,
		DX12CommandListHookSignatures::DrawIndexedInstanced original)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiDrawIndexedInstanced);
		DX12IaReplacementExecutorCallbacks callbacks;
		callbacks.drawInstanced = DirectDrawInstanced;
		callbacks.drawIndexedInstanced = original ? reinterpret_cast<DX12IaCommandSignatures::DrawIndexedInstanced>(original) : DirectDrawIndexedInstanced;
		callbacks.dispatch = DirectDispatch;
		callbacks.setIndexBuffer = DirectSetIndexBuffer;
		callbacks.setVertexBuffers = DirectSetVertexBuffers;
		callbacks.recordReplacementDraw = DX12CommandListHookDiagnostics::RecordIaReplacementDrawExecute;
		callbacks.recordReplacementDispatch = DX12CommandListHookDiagnostics::RecordIaReplacementDispatchExecute;
		DX12IaDrawInvocation draw = DX12IaDrawInvocation::ForDrawIndexedInstanced(
			indexCountPerInstance, instanceCount, startIndexLocation, baseVertexLocation, startInstanceLocation);
		DX12DrawHookFlow::ExecuteHook(commandList, runtimeState, draw, callbacks);
	}

	static void Dispatch(
		ID3D12GraphicsCommandList *commandList,
		UINT threadGroupCountX,
		UINT threadGroupCountY,
		UINT threadGroupCountZ,
		DX12DispatchHookFlowDispatch::Fn original)
	{
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12WrapperRuntimeUtils::Dispatch(
			commandList,
			runtimeState,
			threadGroupCountX,
			threadGroupCountY,
			threadGroupCountZ,
			original);
	}

	static void Dispatch(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeState *runtimeState,
		UINT threadGroupCountX,
		UINT threadGroupCountY,
		UINT threadGroupCountZ,
		DX12DispatchHookFlowDispatch::Fn original)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiDispatch);
		DX12DispatchHookFlow::ExecuteHook(
			commandList,
			runtimeState,
			threadGroupCountX,
			threadGroupCountY,
			threadGroupCountZ,
			original ? original : DirectDispatch);
	}

	static void ExecuteIndirect(
		ID3D12GraphicsCommandList *commandList,
		ID3D12CommandSignature *commandSignature,
		UINT maxCommandCount,
		ID3D12Resource *argumentBuffer,
		UINT64 argumentBufferOffset,
		ID3D12Resource *countBuffer,
		UINT64 countBufferOffset,
		DX12CommandListHookSignatures::ExecuteIndirect original)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiExecuteIndirect);
		DX12CommandListRuntime::BumpIaContentEpoch(commandList);
		ID3D12Resource *unwrappedArgumentBuffer = UnwrapResource(argumentBuffer);
		ID3D12Resource *unwrappedCountBuffer = UnwrapResource(countBuffer);
		ID3D12CommandSignature *unwrappedCommandSignature = UnwrapCommandSignature(commandSignature);
		DX12ExecuteIndirectRuntime::Execute(
			commandList,
			unwrappedCommandSignature,
			maxCommandCount,
			unwrappedArgumentBuffer,
			argumentBufferOffset,
			unwrappedCountBuffer,
			countBufferOffset,
			original ? original : DirectExecuteIndirect);
	}

	static HRESULT QueueSignal(
		ID3D12CommandQueue *queue,
		ID3D12Fence *fence,
		UINT64 value,
		DX12CommandListHookSignatures::QueueSignal original)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiQueueSignal);
		ID3D12Fence *unwrappedFence = UnwrapFence(fence);
		const UINT64 completedBefore =
			DX12OuterTimingProbeUtils::FenceCompletedValue(unwrappedFence);
		LARGE_INTEGER begin = DX12OuterTimingProbeUtils::Now();
		HRESULT hr = original ? original(queue, unwrappedFence, value) :
			queue->Signal(unwrappedFence, value);
		const UINT64 completedAfter =
			DX12OuterTimingProbeUtils::FenceCompletedValue(unwrappedFence);
		DX12OuterTimingProbeUtils::LogQueueFenceSlice(
			"Signal",
			"original",
			queue,
			unwrappedFence,
			value,
			completedBefore,
			completedAfter,
			hr,
			DX12OuterTimingProbeUtils::ElapsedUs(begin));
		return hr;
	}

	static HRESULT QueueWait(
		ID3D12CommandQueue *queue,
		ID3D12Fence *fence,
		UINT64 value,
		DX12CommandListHookSignatures::QueueWait original)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiQueueWait);
		ID3D12Fence *unwrappedFence = UnwrapFence(fence);
		const UINT64 completedBefore =
			DX12OuterTimingProbeUtils::FenceCompletedValue(unwrappedFence);
		LARGE_INTEGER begin = DX12OuterTimingProbeUtils::Now();
		HRESULT hr = original ? original(queue, unwrappedFence, value) :
			queue->Wait(unwrappedFence, value);
		const UINT64 completedAfter =
			DX12OuterTimingProbeUtils::FenceCompletedValue(unwrappedFence);
		DX12OuterTimingProbeUtils::LogQueueFenceSlice(
			"Wait",
			"original",
			queue,
			unwrappedFence,
			value,
			completedBefore,
			completedAfter,
			hr,
			DX12OuterTimingProbeUtils::ElapsedUs(begin));
		return hr;
	}

	static HRESULT ResetCommandList(
		ID3D12GraphicsCommandList *commandList,
		ID3D12CommandAllocator *allocator,
		ID3D12PipelineState *initialState,
		DX12CommandListHookSignatures::ResetCommandList original)
	{
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		return DX12WrapperRuntimeUtils::ResetCommandList(
			commandList, runtimeState, allocator, initialState, original);
	}

	static HRESULT ResetCommandList(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeState *runtimeState,
		ID3D12CommandAllocator *allocator,
		ID3D12PipelineState *initialState,
		DX12CommandListHookSignatures::ResetCommandList original)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiResetCommandList);
		ID3D12CommandAllocator *unwrappedAllocator = UnwrapCommandAllocator(allocator);
		ID3D12PipelineState *unwrappedInitialState = UnwrapPipelineState(initialState);
		HRESULT hr = original ?
			original(commandList, unwrappedAllocator, unwrappedInitialState) :
			commandList->Reset(unwrappedAllocator, unwrappedInitialState);
		if (SUCCEEDED(hr)) {
			if (runtimeState)
				DX12CommandListRuntime::Reset(runtimeState, unwrappedInitialState);
			else
				DX12CommandListRuntime::Reset(commandList, unwrappedInitialState);
			DX12CommandListLifecycle::Reset(commandList, unwrappedInitialState);
		}
		return hr;
	}

	static HRESULT CloseCommandList(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListHookSignatures::CloseCommandList original)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiCloseCommandList);
		return original ? original(commandList) : commandList->Close();
	}

	static void SetPipelineState(
		ID3D12GraphicsCommandList *commandList,
		ID3D12PipelineState *pipelineState,
		DX12CommandListHookSignatures::SetPipelineState original)
	{
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12WrapperRuntimeUtils::SetPipelineState(
			commandList, runtimeState, pipelineState, original);
	}

	static void SetPipelineState(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeState *runtimeState,
		ID3D12PipelineState *pipelineState,
		DX12CommandListHookSignatures::SetPipelineState original)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetPipelineState);
		DX12_HOTPATH_TIME_BEGIN(SetPipelineStateTotal);
		ID3D12PipelineState *unwrappedPipelineState = nullptr;
		if (runtimeState && runtimeState->lastWrappedPipelineState == pipelineState) {
			unwrappedPipelineState = runtimeState->lastUnwrappedPipelineState;
		} else {
			unwrappedPipelineState = UnwrapPipelineState(pipelineState);
			if (runtimeState) {
				runtimeState->lastWrappedPipelineState = pipelineState;
				runtimeState->lastUnwrappedPipelineState = unwrappedPipelineState;
			}
		}
		ID3D12PipelineState *activePipelineState =
			DX12CommandListStateFlow::CapturePipelineState(
					commandList, runtimeState, unwrappedPipelineState);
		if (original)
			original(commandList, activePipelineState);
		else
			commandList->SetPipelineState(activePipelineState);
		DX12_HOTPATH_TIME_END(SetPipelineStateTotal);
	}

	static void ResourceBarrier(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeState *runtimeState,
		UINT numBarriers,
		const D3D12_RESOURCE_BARRIER *barriers,
		DX12CommandListHookSignatures::ResourceBarrier original)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiResourceBarrier);
		DX12_HOTPATH_TIME_BEGIN(ResourceBarrierTotal);
		std::vector<D3D12_RESOURCE_BARRIER> localBarriers;
		std::vector<D3D12_RESOURCE_BARRIER> &unwrappedBarriers =
			runtimeState ? runtimeState->barrierScratch : localBarriers;
		const D3D12_RESOURCE_BARRIER *activeBarriers = barriers;
		DX12_HOTPATH_TIME_BEGIN(BarrierUnwrap);
		UnwrapResourceBarrierArray(barriers, numBarriers, &unwrappedBarriers, &activeBarriers);
		DX12_HOTPATH_TIME_END(BarrierUnwrap);
		LogCommandListStage("ResourceBarrier", "before", commandList);
		DX12_HOTPATH_TIME_BEGIN(BarrierCapture);
		DX12CommandListStateFlow::CaptureResourceBarrier(commandList, numBarriers, activeBarriers);
		DX12_HOTPATH_TIME_END(BarrierCapture);
		if (original)
			original(commandList, numBarriers, activeBarriers);
		else
			commandList->ResourceBarrier(numBarriers, activeBarriers);
		LogCommandListStage("ResourceBarrier", "after", commandList);
		DX12_HOTPATH_TIME_END(ResourceBarrierTotal);
	}

	static void IASetIndexBuffer(
		ID3D12GraphicsCommandList *commandList,
		const D3D12_INDEX_BUFFER_VIEW *view,
		DX12CommandListHookSignatures::IaSetIndexBuffer original)
	{
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12WrapperRuntimeUtils::IASetIndexBuffer(commandList, runtimeState, view, original);
	}

	static void IASetIndexBuffer(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeState *runtimeState,
		const D3D12_INDEX_BUFFER_VIEW *view,
		DX12CommandListHookSignatures::IaSetIndexBuffer original)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiIASetIndexBuffer);
		DX12CommandListStateCapture::IndexBuffer(commandList, runtimeState, view);
		if (original)
			original(commandList, view);
		else
			commandList->IASetIndexBuffer(view);
	}

	static void IASetVertexBuffers(
		ID3D12GraphicsCommandList *commandList,
		UINT startSlot,
		UINT count,
		const D3D12_VERTEX_BUFFER_VIEW *views,
		DX12CommandListHookSignatures::IaSetVertexBuffers original)
	{
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12WrapperRuntimeUtils::IASetVertexBuffers(
			commandList, runtimeState, startSlot, count, views, original);
	}

	static void IASetVertexBuffers(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeState *runtimeState,
		UINT startSlot,
		UINT count,
		const D3D12_VERTEX_BUFFER_VIEW *views,
		DX12CommandListHookSignatures::IaSetVertexBuffers original)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiIASetVertexBuffers);
		D3D12_VERTEX_BUFFER_VIEW redirectedViews[
			D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
		const D3D12_VERTEX_BUFFER_VIEW *activeViews = views;
		if (DX12PreSkinIaRedirectUtils::RedirectVertexBufferViews(
			    commandList,
			    startSlot,
			    count,
			    views,
			    redirectedViews,
			    D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT,
			    runtimeState ? &runtimeState->ia : nullptr)) {
			activeViews = redirectedViews;
		}
		DX12CommandListStateCapture::VertexBuffers(
			commandList, runtimeState, startSlot, count, activeViews);
		if (original)
			original(commandList, startSlot, count, activeViews);
		else
			commandList->IASetVertexBuffers(startSlot, count, activeViews);
	}

	static bool ShouldSkipComputeBinding()
	{
		// Single bit test on HookWorkFlags — avoids function call
		// to DX12ModNeedsPreSkinningBindingTracking() per binding call.
		return DX12HotPathState::skipAll &&
			!(DX12HotPathState::HookWorkFlags & DX12HotPathState::FLAG_PRE_SKIN_BINDING_TRACK);
	}

	static void SetDescriptorHeaps(
		ID3D12GraphicsCommandList *commandList,
		UINT count,
		ID3D12DescriptorHeap *const *heaps)
	{
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12WrapperRuntimeUtils::SetDescriptorHeaps(commandList, runtimeState, count, heaps);
	}

	static void SetDescriptorHeaps(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeState *runtimeState,
		UINT count,
		ID3D12DescriptorHeap *const *heaps)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetDescriptorHeaps);
		DX12_HOTPATH_TIME_BEGIN(DescriptorHeapsTotal);
		ID3D12DescriptorHeap *cbvSrvUav = nullptr;
		ID3D12DescriptorHeap *sampler = nullptr;
		DX12CommandListComputeBindingFlow::ResolveDescriptorHeaps(
			count, heaps, &cbvSrvUav, &sampler);
		if (runtimeState)
			DX12CommandListRuntime::SetDescriptorHeaps(
				runtimeState, cbvSrvUav, sampler);
		else
			DX12CommandListRuntime::SetDescriptorHeaps(
				commandList, cbvSrvUav, sampler);
		if (!ShouldSkipComputeBinding()) {
			DX12_PROFILE_SCOPE(SetDescriptorHeaps);
			DX12CommandListComputeBindingFlow::CaptureDescriptorHeaps(
				commandList, runtimeState, count, heaps);
		}
		commandList->SetDescriptorHeaps(count, heaps);
		DX12_HOTPATH_TIME_END(DescriptorHeapsTotal);
	}

	static void SetComputeRootSignature(
		ID3D12GraphicsCommandList *commandList,
		ID3D12RootSignature *rootSignature)
	{
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12WrapperRuntimeUtils::SetComputeRootSignature(
			commandList, runtimeState, rootSignature, nullptr);
	}

	static void SetComputeRootSignature(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeState *runtimeState,
		ID3D12RootSignature *rootSignature,
		DX12CommandListHookSignatures::SetRootSignature original)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetComputeRootSignature);
		ID3D12RootSignature *unwrapped = nullptr;
		if (runtimeState && runtimeState->lastWrappedComputeRootSignature == rootSignature)
			unwrapped = runtimeState->lastUnwrappedComputeRootSignature;
		else {
			unwrapped = UnwrapRootSignature(rootSignature);
			if (runtimeState) {
				runtimeState->lastWrappedComputeRootSignature = rootSignature;
				runtimeState->lastUnwrappedComputeRootSignature = unwrapped;
			}
		}
		if (!ShouldSkipComputeBinding()) {
			DX12_PROFILE_SCOPE(SetComputeRootSignature);
			DX12CommandListComputeBindingFlow::CaptureRootSignature(
				commandList, runtimeState, unwrapped, false);
		}
		if (original) original(commandList, unwrapped);
		else commandList->SetComputeRootSignature(unwrapped);
	}

	static void SetGraphicsRootSignature(
		ID3D12GraphicsCommandList *commandList,
		ID3D12RootSignature *rootSignature)
	{
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12WrapperRuntimeUtils::SetGraphicsRootSignature(
			commandList, runtimeState, rootSignature, nullptr);
	}

	static void SetGraphicsRootSignature(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeState *runtimeState,
		ID3D12RootSignature *rootSignature,
		DX12CommandListHookSignatures::SetRootSignature original)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetGraphicsRootSignature);
		ID3D12RootSignature *unwrapped = nullptr;
		if (runtimeState && runtimeState->lastWrappedGraphicsRootSignature == rootSignature)
			unwrapped = runtimeState->lastUnwrappedGraphicsRootSignature;
		else {
			unwrapped = UnwrapRootSignature(rootSignature);
			if (runtimeState) {
				runtimeState->lastWrappedGraphicsRootSignature = rootSignature;
				runtimeState->lastUnwrappedGraphicsRootSignature = unwrapped;
			}
		}
		if (DX12HotPathState::skipAll) {
			if (original) original(commandList, unwrapped); else commandList->SetGraphicsRootSignature(unwrapped);
		}
		else {
			DX12_PROFILE_SCOPE(SetGraphicsRootSignature);
			DX12CommandListComputeBindingFlow::CaptureRootSignature(
				commandList, runtimeState, unwrapped, true);
			if (original) original(commandList, unwrapped); else commandList->SetGraphicsRootSignature(unwrapped);
		}
	}

	static void SetComputeRootDescriptorTable(
		ID3D12GraphicsCommandList *commandList,
		UINT rootParameterIndex,
		D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor)
	{
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12WrapperRuntimeUtils::SetComputeRootDescriptorTable(
			commandList, runtimeState, rootParameterIndex, baseDescriptor, nullptr);
	}

	static void SetComputeRootDescriptorTable(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeState *runtimeState,
		UINT rootParameterIndex,
		D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor,
		DX12CommandListHookSignatures::SetRootDescriptorTable original)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetComputeRootDescriptorTable);
		if (runtimeState) {
			DX12CommandListRuntime::RememberRootDescriptorTable(
				runtimeState, false, rootParameterIndex, baseDescriptor);
		} else {
			DX12CommandListRuntime::RememberRootDescriptorTable(
				commandList, false, rootParameterIndex, baseDescriptor);
		}
		if (!ShouldSkipComputeBinding()) {
			DX12_PROFILE_SCOPE(SetComputeRootDescriptorTable);
			DX12CommandListComputeBindingFlow::CaptureRootDescriptorTable(
				commandList, runtimeState, rootParameterIndex, baseDescriptor, false);
		}
		if (original) original(commandList, rootParameterIndex, baseDescriptor);
		else commandList->SetComputeRootDescriptorTable(rootParameterIndex, baseDescriptor);
	}

	static void SetGraphicsRootDescriptorTable(
		ID3D12GraphicsCommandList *commandList,
		UINT rootParameterIndex,
		D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor)
	{
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12WrapperRuntimeUtils::SetGraphicsRootDescriptorTable(
			commandList, runtimeState, rootParameterIndex, baseDescriptor, nullptr);
	}

	static void SetGraphicsRootDescriptorTable(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeState *runtimeState,
		UINT rootParameterIndex,
		D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor,
		DX12CommandListHookSignatures::SetRootDescriptorTable original)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetGraphicsRootDescriptorTable);
		if (runtimeState) {
			DX12CommandListRuntime::RememberRootDescriptorTable(
				runtimeState, true, rootParameterIndex, baseDescriptor);
		} else {
			DX12CommandListRuntime::RememberRootDescriptorTable(
				commandList, true, rootParameterIndex, baseDescriptor);
		}
		if (DX12HotPathState::skipAll)
			if (original) original(commandList, rootParameterIndex, baseDescriptor);
			else commandList->SetGraphicsRootDescriptorTable(rootParameterIndex, baseDescriptor);
		else {
			DX12_PROFILE_SCOPE(SetGraphicsRootDescriptorTable);
			DX12CommandListComputeBindingFlow::CaptureRootDescriptorTable(
				commandList, runtimeState, rootParameterIndex, baseDescriptor, true);
			if (original) original(commandList, rootParameterIndex, baseDescriptor);
			else commandList->SetGraphicsRootDescriptorTable(rootParameterIndex, baseDescriptor);
		}
	}

	static void SetComputeRootConstantBufferView(
		ID3D12GraphicsCommandList *commandList,
		UINT rootParameterIndex,
		D3D12_GPU_VIRTUAL_ADDRESS bufferLocation)
	{
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12WrapperRuntimeUtils::SetComputeRootConstantBufferView(
			commandList, runtimeState, rootParameterIndex, bufferLocation);
	}

	static void SetComputeRootConstantBufferView(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeState *runtimeState,
		UINT rootParameterIndex,
		D3D12_GPU_VIRTUAL_ADDRESS bufferLocation)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetComputeRootDescriptor);
		if (!ShouldSkipComputeBinding()) {
			DX12_PROFILE_SCOPE(SetComputeRootConstantBufferView);
			DX12CommandListStateCapture::ComputeRootDescriptor(
				commandList, runtimeState, rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE_CBV, bufferLocation);
		}
		commandList->SetComputeRootConstantBufferView(rootParameterIndex, bufferLocation);
	}

	static void SetGraphicsRootConstantBufferView(
		ID3D12GraphicsCommandList *commandList,
		UINT rootParameterIndex,
		D3D12_GPU_VIRTUAL_ADDRESS bufferLocation)
	{
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12WrapperRuntimeUtils::SetGraphicsRootConstantBufferView(
			commandList, runtimeState, rootParameterIndex, bufferLocation);
	}

	static void SetGraphicsRootConstantBufferView(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeState *runtimeState,
		UINT rootParameterIndex,
		D3D12_GPU_VIRTUAL_ADDRESS bufferLocation)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetGraphicsRootDescriptor);
		if (!DX12HotPathState::skipAll) {
			DX12_PROFILE_SCOPE(SetGraphicsRootConstantBufferView);
			DX12CommandListStateCapture::GraphicsRootDescriptor(
				commandList, runtimeState, rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE_CBV, bufferLocation);
		}
		commandList->SetGraphicsRootConstantBufferView(rootParameterIndex, bufferLocation);
	}

	static void SetComputeRootShaderResourceView(
		ID3D12GraphicsCommandList *commandList,
		UINT rootParameterIndex,
		D3D12_GPU_VIRTUAL_ADDRESS bufferLocation)
	{
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12WrapperRuntimeUtils::SetComputeRootShaderResourceView(
			commandList, runtimeState, rootParameterIndex, bufferLocation);
	}

	static void SetComputeRootShaderResourceView(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeState *runtimeState,
		UINT rootParameterIndex,
		D3D12_GPU_VIRTUAL_ADDRESS bufferLocation)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetComputeRootDescriptor);
		if (!ShouldSkipComputeBinding()) {
			DX12_PROFILE_SCOPE(SetComputeRootShaderResourceView);
			DX12CommandListStateCapture::ComputeRootDescriptor(
				commandList, runtimeState, rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE_SRV, bufferLocation);
		}
		commandList->SetComputeRootShaderResourceView(rootParameterIndex, bufferLocation);
	}

	static void SetGraphicsRootShaderResourceView(
		ID3D12GraphicsCommandList *commandList,
		UINT rootParameterIndex,
		D3D12_GPU_VIRTUAL_ADDRESS bufferLocation)
	{
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12WrapperRuntimeUtils::SetGraphicsRootShaderResourceView(
			commandList, runtimeState, rootParameterIndex, bufferLocation);
	}

	static void SetGraphicsRootShaderResourceView(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeState *runtimeState,
		UINT rootParameterIndex,
		D3D12_GPU_VIRTUAL_ADDRESS bufferLocation)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetGraphicsRootDescriptor);
		if (!DX12HotPathState::skipAll) {
			DX12_PROFILE_SCOPE(SetGraphicsRootShaderResourceView);
			DX12CommandListStateCapture::GraphicsRootDescriptor(
				commandList, runtimeState, rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE_SRV, bufferLocation);
		}
		commandList->SetGraphicsRootShaderResourceView(rootParameterIndex, bufferLocation);
	}

	static void SetComputeRootUnorderedAccessView(
		ID3D12GraphicsCommandList *commandList,
		UINT rootParameterIndex,
		D3D12_GPU_VIRTUAL_ADDRESS bufferLocation)
	{
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12WrapperRuntimeUtils::SetComputeRootUnorderedAccessView(
			commandList, runtimeState, rootParameterIndex, bufferLocation);
	}

	static void SetComputeRootUnorderedAccessView(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeState *runtimeState,
		UINT rootParameterIndex,
		D3D12_GPU_VIRTUAL_ADDRESS bufferLocation)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetComputeRootDescriptor);
		if (!ShouldSkipComputeBinding()) {
			DX12_PROFILE_SCOPE(SetComputeRootUnorderedAccessView);
			DX12CommandListStateCapture::ComputeRootDescriptor(
				commandList, runtimeState, rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE_UAV, bufferLocation);
		}
		commandList->SetComputeRootUnorderedAccessView(rootParameterIndex, bufferLocation);
	}

	static void SetGraphicsRootUnorderedAccessView(
		ID3D12GraphicsCommandList *commandList,
		UINT rootParameterIndex,
		D3D12_GPU_VIRTUAL_ADDRESS bufferLocation)
	{
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12WrapperRuntimeUtils::SetGraphicsRootUnorderedAccessView(
			commandList, runtimeState, rootParameterIndex, bufferLocation);
	}

	static void SetGraphicsRootUnorderedAccessView(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeState *runtimeState,
		UINT rootParameterIndex,
		D3D12_GPU_VIRTUAL_ADDRESS bufferLocation)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetGraphicsRootDescriptor);
		if (!DX12HotPathState::skipAll) {
			DX12_PROFILE_SCOPE(SetGraphicsRootUnorderedAccessView);
			DX12CommandListStateCapture::GraphicsRootDescriptor(
				commandList, runtimeState, rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE_UAV, bufferLocation);
		}
		commandList->SetGraphicsRootUnorderedAccessView(rootParameterIndex, bufferLocation);
	}

	static void IASetPrimitiveTopology(
		ID3D12GraphicsCommandList *commandList,
		D3D12_PRIMITIVE_TOPOLOGY topology)
	{
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12WrapperRuntimeUtils::IASetPrimitiveTopology(commandList, runtimeState, topology);
	}

	static void IASetPrimitiveTopology(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeState *runtimeState,
		D3D12_PRIMITIVE_TOPOLOGY topology)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiIASetPrimitiveTopology);
		if (DX12HotPathState::skipAll)
			commandList->IASetPrimitiveTopology(topology);
		else {
			DX12_PROFILE_SCOPE(IASetPrimitiveTopology);
			DX12CommandListStateFlow::CapturePrimitiveTopology(commandList, runtimeState, topology);
			commandList->IASetPrimitiveTopology(topology);
		}
	}
};

// ---- DXGIHookCallbacks.hpp ----
#line 1 "DXGIHookCallbacks.hpp"
#include <Windows.h>
#include <dxgi1_4.h>

class DXGIHookCallbacks
{
public:
	static HRESULT WINAPI HookedCreateDXGIFactory(REFIID riid, void **factory);
	static HRESULT WINAPI HookedCreateDXGIFactory1(REFIID riid, void **factory);
	static HRESULT WINAPI HookedCreateDXGIFactory2(UINT flags, REFIID riid, void **factory);
};

// ---- DXGIHookSignatures.hpp ----
#line 1 "DXGIHookSignatures.hpp"
#include <Windows.h>
#include <dxgi1_4.h>

class DXGIHookSignatures
{
public:
	using CreateDXGIFactory = HRESULT(WINAPI *)(REFIID, void**);
	using CreateDXGIFactory2 = HRESULT(WINAPI *)(UINT, REFIID, void**);
};

// ---- DXGIHookSlotTypes.hpp ----
#line 1 "DXGIHookSlotTypes.hpp"
#include <Windows.h>

class DXGIHookSlotTypes
{
public:
	class IDXGISwapChain
	{
	public:
		static constexpr UINT Present = 8;
		static constexpr UINT ResizeBuffers = 13;
	};

	class IDXGISwapChain1
	{
	public:
		static constexpr UINT Present1 = 22;
	};

	class IDXGIFactory
	{
	public:
		static constexpr UINT CreateSwapChain = 10;
	};

	class IDXGIFactory2
	{
	public:
		static constexpr UINT CreateSwapChainForHwnd = 15;
		static constexpr UINT CreateSwapChainForCoreWindow = 16;
		static constexpr UINT CreateSwapChainForComposition = 24;
	};
};

// ---- DXGIHookState.hpp ----
#line 1 "DXGIHookState.hpp"
#include <Windows.h>
#include <dxgi1_4.h>


class DXGIHookState
{
public:
	inline static bool HookAttempted = false;
	inline static SRWLOCK HookLock = SRWLOCK_INIT;
	inline static DXGIHookSignatures::CreateDXGIFactory OrigCreateDXGIFactory = nullptr;
	inline static DXGIHookSignatures::CreateDXGIFactory OrigCreateDXGIFactory1 = nullptr;
	inline static DXGIHookSignatures::CreateDXGIFactory2 OrigCreateDXGIFactory2 = nullptr;
};

// ---- MigotoIniLoader.hpp ----
#line 1 "MigotoIniLoader.hpp"
#include <string>
#include <vector>


namespace Bunny {

class MigotoIniLoadResult
{
public:
	IniDocument document;
	std::vector<std::wstring> loadedFiles;
	std::vector<std::wstring> errors;
	std::vector<std::wstring> warnings;
};

class MigotoIniLoader
{
public:
	static bool LoadWithIncludes(const wchar_t *rootPath, MigotoIniLoadResult *result);
};

} // namespace Bunny

#include <Windows.h>
#include <Shlwapi.h>

#include <algorithm>
#include <set>
#include <fstream>
#include <sstream>

namespace Bunny {

class MigotoIniLoaderPath
{
public:
static bool StartsWithI(const std::wstring &value, const wchar_t *prefix)
{
	std::wstring lower = IniDocumentText::ToLower(value);
	std::wstring lowerPrefix = IniDocumentText::ToLower(prefix);
	return lower.rfind(lowerPrefix, 0) == 0;
}

static bool EndsWithI(const std::wstring &value, const wchar_t *suffix)
{
	std::wstring lower = IniDocumentText::ToLower(value);
	std::wstring lowerSuffix = IniDocumentText::ToLower(suffix);
	if (lower.size() < lowerSuffix.size())
		return false;
	return lower.compare(lower.size() - lowerSuffix.size(), lowerSuffix.size(), lowerSuffix) == 0;
}

static bool IsAbsolutePath(const std::wstring &path)
{
	return path.size() >= 2 && (path[1] == L':' || path[0] == L'\\' || path[0] == L'/');
}

static std::wstring DirectoryOf(const std::wstring &path)
{
	wchar_t buffer[MAX_PATH];
	wcsncpy_s(buffer, path.c_str(), _TRUNCATE);
	PathRemoveFileSpecW(buffer);
	return buffer;
}

static std::wstring JoinPath(const std::wstring &base, const std::wstring &path)
{
	if (path.empty())
		return base;
	if (IsAbsolutePath(path))
		return path;

	wchar_t buffer[MAX_PATH];
	wcsncpy_s(buffer, base.c_str(), _TRUNCATE);
	PathAppendW(buffer, path.c_str());
	return buffer;
}

static std::wstring CanonicalPath(const std::wstring &path)
{
	wchar_t full[MAX_PATH];
	if (!GetFullPathNameW(path.c_str(), ARRAYSIZE(full), full, nullptr))
		return IniDocumentText::ToLower(path);
	return IniDocumentText::ToLower(full);
}

static bool IsDisabledName(const std::wstring &name)
{
	return StartsWithI(name, L"disabled") || StartsWithI(name, L"DISABLED");
}

static bool ShouldExcludeRecursive(const std::wstring &name, const std::vector<std::wstring> &patterns)
{
	if (IsDisabledName(name))
		return true;

	for (const std::wstring &pattern : patterns) {
		std::wstring lowerName = IniDocumentText::ToLower(name);
		std::wstring lowerPattern = IniDocumentText::ToLower(IniDocumentText::Trim(pattern));
		if (lowerPattern.empty())
			continue;
		if (lowerPattern == lowerName)
			return true;
		if (lowerPattern.back() == L'*') {
			lowerPattern.pop_back();
			if (lowerName.rfind(lowerPattern, 0) == 0)
				return true;
		}
	}
	return false;
}

static bool ReadFileText(const std::wstring &path, std::wstring *text)
{
	if (!text)
		return false;
	text->clear();

	FILE *file = _wfsopen(path.c_str(), L"rb", _SH_DENYNO);
	if (!file)
		return false;

	fseek(file, 0, SEEK_END);
	long size = ftell(file);
	fseek(file, 0, SEEK_SET);
	if (size <= 0) {
		fclose(file);
		return true;
	}

	std::string bytes;
	bytes.resize(static_cast<size_t>(size));
	size_t read = fread(&bytes[0], 1, bytes.size(), file);
	fclose(file);
	if (read != bytes.size())
		return false;

	if (bytes.size() >= 3 &&
	    static_cast<unsigned char>(bytes[0]) == 0xef &&
	    static_cast<unsigned char>(bytes[1]) == 0xbb &&
	    static_cast<unsigned char>(bytes[2]) == 0xbf) {
		bytes.erase(0, 3);
	}

	UINT codePage = CP_UTF8;
	int length = MultiByteToWideChar(
		codePage, MB_ERR_INVALID_CHARS, bytes.data(),
		static_cast<int>(bytes.size()), nullptr, 0);
	if (length <= 0) {
		codePage = CP_ACP;
		length = MultiByteToWideChar(
			codePage, 0, bytes.data(), static_cast<int>(bytes.size()),
			nullptr, 0);
	}
	if (length < 0)
		return false;

	text->assign(static_cast<size_t>(length), L'\0');
	if (length > 0) {
		const DWORD flags = codePage == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0;
		if (!MultiByteToWideChar(
			    codePage, flags, bytes.data(), static_cast<int>(bytes.size()),
			    &(*text)[0], length))
			return false;
	}
	return true;
}

static std::wstring MakeRelativeNamespace(
	const std::wstring &rootDir, const std::wstring &path)
{
	wchar_t relative[MAX_PATH];
	if (PathRelativePathToW(
		relative, rootDir.c_str(), FILE_ATTRIBUTE_DIRECTORY,
		path.c_str(), FILE_ATTRIBUTE_NORMAL)) {
		std::wstring value = relative;
		if (value.rfind(L".\\", 0) == 0 || value.rfind(L"./", 0) == 0)
			value = value.substr(2);
		return value;
	}
	return path;
}

class LoaderState
{
public:
	std::wstring rootDir;
	std::set<std::wstring> seenFiles;
	IniDocument document;
	std::vector<std::wstring> loadedFiles;
	std::vector<std::wstring> errors;
	std::vector<std::wstring> warnings;
};

static void CollectIncludeEntries(
	const IniDocument &ini, std::vector<std::wstring> *includes,
	std::vector<std::wstring> *recursiveIncludes,
	std::vector<std::wstring> *excludeRecursive)
{
	for (const IniSection &section : ini.Sections()) {
		if (!MigotoIniLoaderPath::StartsWithI(section.name, L"Include"))
			continue;
		for (const IniEntry &entry : section.entries) {
			if (!entry.hasEquals)
				continue;
			std::wstring key = IniDocumentText::ToLower(IniDocumentText::Trim(entry.key));
			if (key == L"include")
				includes->push_back(IniDocumentText::Trim(entry.value));
			else if (key == L"include_recursive")
				recursiveIncludes->push_back(IniDocumentText::Trim(entry.value));
			else if (key == L"exclude_recursive")
				excludeRecursive->push_back(IniDocumentText::Trim(entry.value));
		}
	}
}

static void LoadDirectoryRecursive(
	const std::wstring &dir, const std::vector<std::wstring> &excludeRecursive,
	LoaderState *state)
{
	if (!state)
		return;

	std::wstring search = MigotoIniLoaderPath::JoinPath(dir, L"*");
	WIN32_FIND_DATAW findData = {};
	HANDLE find = FindFirstFileW(search.c_str(), &findData);
	if (find == INVALID_HANDLE_VALUE) {
		state->warnings.push_back(L"include_recursive path not found: " + dir);
		return;
	}

	std::vector<std::wstring> iniFiles;
	std::vector<std::wstring> directories;
	do {
		std::wstring name = findData.cFileName;
		if (name == L"." || name == L"..")
			continue;
		if (MigotoIniLoaderPath::ShouldExcludeRecursive(name, excludeRecursive))
			continue;
		if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			directories.push_back(name);
		else if (MigotoIniLoaderPath::EndsWithI(name, L".ini"))
			iniFiles.push_back(name);
	} while (FindNextFileW(find, &findData));
	FindClose(find);

	std::sort(iniFiles.begin(), iniFiles.end(),
		[](const std::wstring &a, const std::wstring &b) { return IniDocumentText::ToLower(a) < IniDocumentText::ToLower(b); });
	std::sort(directories.begin(), directories.end(),
		[](const std::wstring &a, const std::wstring &b) { return IniDocumentText::ToLower(a) < IniDocumentText::ToLower(b); });

	for (const std::wstring &file : iniFiles)
	{
		std::wstring filePath = MigotoIniLoaderPath::JoinPath(dir, file);
		MigotoIniLoaderPath::LoadFileRecursive(filePath, MigotoIniLoaderPath::MakeRelativeNamespace(state->rootDir, filePath), state);
	}
	for (const std::wstring &child : directories)
		MigotoIniLoaderPath::LoadDirectoryRecursive(MigotoIniLoaderPath::JoinPath(dir, child), excludeRecursive, state);
}

static void LoadFileRecursive(
	const std::wstring &path, const std::wstring &iniNamespace, LoaderState *state)
{
	if (!state)
		return;

	std::wstring canonical = MigotoIniLoaderPath::CanonicalPath(path);
	if (!state->seenFiles.insert(canonical).second) {
		state->warnings.push_back(L"ini included multiple times: " + path);
		return;
	}

	std::wstring text;
	if (!MigotoIniLoaderPath::ReadFileText(path, &text)) {
		state->errors.push_back(L"unable to load ini: " + path);
		return;
	}

	state->loadedFiles.push_back(path);
	std::wstring sourceDir = MigotoIniLoaderPath::DirectoryOf(path);
	if (!state->document.AppendParse(text.c_str(), path, sourceDir, iniNamespace)) {
		state->errors.push_back(
			L"unable to parse ini: " + path + L" (" + state->document.Error() + L")");
		return;
	}

	std::vector<std::wstring> includes;
	std::vector<std::wstring> recursiveIncludes;
	std::vector<std::wstring> excludeRecursive;
	IniDocument localIni;
	localIni.Parse(text.c_str());
	MigotoIniLoaderPath::CollectIncludeEntries(localIni, &includes, &recursiveIncludes, &excludeRecursive);

	std::wstring includeBase = MigotoIniLoaderPath::DirectoryOf(path);
	for (const std::wstring &include : includes)
	{
		std::wstring includePath = MigotoIniLoaderPath::JoinPath(includeBase, include);
		MigotoIniLoaderPath::LoadFileRecursive(includePath, MigotoIniLoaderPath::MakeRelativeNamespace(state->rootDir, includePath), state);
	}
	for (const std::wstring &recursive : recursiveIncludes)
		MigotoIniLoaderPath::LoadDirectoryRecursive(MigotoIniLoaderPath::JoinPath(includeBase, recursive), excludeRecursive, state);
}

};

inline bool MigotoIniLoader::LoadWithIncludes(const wchar_t *rootPath, MigotoIniLoadResult *result)
{
	if (!rootPath || !result)
		return false;

	*result = MigotoIniLoadResult();
	MigotoIniLoaderPath::LoaderState state;
	state.rootDir = MigotoIniLoaderPath::DirectoryOf(rootPath);
	MigotoIniLoaderPath::LoadFileRecursive(rootPath, L"", &state);

	result->loadedFiles = std::move(state.loadedFiles);
	result->errors = std::move(state.errors);
	result->warnings = std::move(state.warnings);
	result->document = std::move(state.document);
	return true;
}

} // namespace Bunny

// ---- MigotoResource.hpp ----
#line 1 "MigotoResource.hpp"
#include <stdint.h>

#include <string>
#include <unordered_map>


namespace Bunny {

class ResourceConfig
{
public:
	std::wstring section;
	std::wstring name;
	std::wstring originalSection;
	std::wstring sourcePath;
	std::wstring type;
	std::wstring filename;
	std::wstring sourceDir;
	std::wstring iniNamespace;
	std::wstring format;
	std::wstring data;
	uint32_t stride = 0;
	uint32_t arraySize = 0;
	uint32_t byteWidth = 0;
	bool hasStride = false;
	bool hasArraySize = false;
	bool hasByteWidth = false;
};

using ResourceMap = std::unordered_map<std::wstring, ResourceConfig>;

class MigotoResourceParser
{
public:
	static void ParseSections(const IniDocument &ini, ResourceMap *resources);
};

} // namespace Bunny

namespace Bunny {

class MigotoResourceParserText
{
public:
static bool StartsWithI(const std::wstring &value, const wchar_t *prefix)
{
	std::wstring lower = IniDocumentText::ToLower(value);
	std::wstring lowerPrefix = IniDocumentText::ToLower(prefix);
	return lower.rfind(lowerPrefix, 0) == 0;
}

static bool ParseUInt(const std::wstring &text, uint32_t *value)
{
	if (!value)
		return false;

	std::wstring trimmed = IniDocumentText::Trim(text);
	if (trimmed.empty())
		return false;

	wchar_t *end = nullptr;
	unsigned long parsed = std::wcstoul(trimmed.c_str(), &end, 0);
	if (!end || *IniDocumentText::Trim(end).c_str())
		return false;

	*value = static_cast<uint32_t>(parsed);
	return true;
}

};

inline void MigotoResourceParser::ParseSections(const IniDocument &ini, ResourceMap *resources)
{
	if (!resources)
		return;

	resources->clear();
	for (const IniSection &section : ini.Sections()) {
		if (!MigotoResourceParserText::StartsWithI(section.name, L"Resource"))
			continue;

		ResourceConfig config;
		config.section = section.name;
		config.name = section.name;
		config.originalSection = section.originalName.empty() ? section.name : section.originalName;
		config.sourcePath = section.sourcePath;
		config.sourceDir = section.sourceDir;
		config.iniNamespace = section.iniNamespace;

		for (const IniEntry &entry : section.entries) {
			if (!entry.hasEquals)
				continue;

			std::wstring key = IniDocumentText::ToLower(entry.key);
			if (key == L"type") {
				config.type = IniDocumentText::Trim(entry.value);
			} else if (key == L"filename" || key == L"file") {
				config.filename = IniDocumentText::Trim(entry.value);
			} else if (key == L"format") {
				config.format = IniDocumentText::Trim(entry.value);
			} else if (key == L"data") {
				config.data = IniDocumentText::Trim(entry.value);
			} else if (key == L"stride") {
				uint32_t value = 0;
				if (MigotoResourceParserText::ParseUInt(entry.value, &value)) {
					config.stride = value;
					config.hasStride = true;
				}
			} else if (key == L"array" || key == L"array_size") {
				uint32_t value = 0;
				if (MigotoResourceParserText::ParseUInt(entry.value, &value)) {
					config.arraySize = value;
					config.hasArraySize = true;
				}
			} else if (key == L"byte_width" || key == L"width") {
				uint32_t value = 0;
				if (MigotoResourceParserText::ParseUInt(entry.value, &value)) {
					config.byteWidth = value;
					config.hasByteWidth = true;
				}
			}
		}

		(*resources)[config.name] = config;
		if (section.iniNamespace.empty())
			(*resources)[config.originalSection] = config;
	}
}

} // namespace Bunny

// ---- MigotoShaderRegex.hpp ----
#line 1 "MigotoShaderRegex.hpp"
#include <string>
#include <unordered_map>
#include <vector>



namespace Bunny {

class ShaderRegexConfig
{
public:
	std::wstring section;
	std::wstring originalSection;
	std::wstring sourcePath;
	std::wstring sourceDir;
	std::wstring iniNamespace;
	std::vector<std::string> shaderModels;
	CommandListLinks commandLists;
	std::vector<CommandListAction> actions;
	bool handlingSkip = false;
	bool hasPattern = false;
};

using ShaderRegexMap = std::unordered_map<std::wstring, ShaderRegexConfig>;

class MigotoShaderRegexParser
{
public:
	static void ParseSections(
		const IniDocument &ini, ShaderRegexMap *shaderRegexes);
};

}

namespace Bunny {

class MigotoShaderRegexParserText
{
public:
static bool StartsWithI(const std::wstring &value, const wchar_t *prefix)
{
	std::wstring lower = IniDocumentText::ToLower(value);
	std::wstring lowerPrefix = IniDocumentText::ToLower(prefix);
	return lower.rfind(lowerPrefix, 0) == 0;
}

static std::wstring ShaderRegexGroupName(const std::wstring &section)
{
	size_t namespaceEnd = 0;
	if (IniDocumentText::ToLower(section).rfind(L"shaderregex\\", 0) == 0) {
		size_t suffixStart = section.find_last_of(L'\\');
		if (suffixStart != std::wstring::npos)
			namespaceEnd = suffixStart + 1;
	}
	size_t dot = section.find(L'.', namespaceEnd);
	if (dot == std::wstring::npos)
		return section;
	return section.substr(0, dot);
}

static bool IsShaderRegexMainSection(const std::wstring &section)
{
	return ShaderRegexGroupName(section) == section;
}

static void AppendShaderModels(
	const std::wstring &value, std::vector<std::string> *shaderModels)
{
	if (!shaderModels)
		return;

	size_t start = 0;
	while (start <= value.size()) {
		size_t space = value.find_first_of(L" \t\r\n", start);
		std::wstring token = space == std::wstring::npos ?
			value.substr(start) : value.substr(start, space - start);
		token = IniDocumentText::ToLower(IniDocumentText::Trim(token));
		if (!token.empty()) {
			std::string model;
			model.reserve(token.size());
			bool ascii = true;
			for (wchar_t ch : token) {
				if (ch > 0x7f) {
					ascii = false;
					break;
				}
				model.push_back(static_cast<char>(ch));
			}
			if (ascii)
				shaderModels->push_back(model);
		}
		if (space == std::wstring::npos)
			break;
		start = space + 1;
	}
}

};

inline void MigotoShaderRegexParser::ParseSections(
	const IniDocument &ini, ShaderRegexMap *shaderRegexes)
{
	if (!shaderRegexes)
		return;

	shaderRegexes->clear();
	for (const IniSection &section : ini.Sections()) {
		if (!MigotoShaderRegexParserText::StartsWithI(section.name, L"ShaderRegex"))
			continue;

		const std::wstring groupName = MigotoShaderRegexParserText::ShaderRegexGroupName(section.name);
		ShaderRegexConfig &config = (*shaderRegexes)[groupName];
		if (config.section.empty()) {
			config.section = groupName;
			config.originalSection = section.originalName.empty() ? groupName : MigotoShaderRegexParserText::ShaderRegexGroupName(section.originalName);
			config.sourcePath = section.sourcePath;
			config.sourceDir = section.sourceDir;
			config.iniNamespace = section.iniNamespace;
		}

		const bool isMain = MigotoShaderRegexParserText::IsShaderRegexMainSection(section.name);
		if (!isMain) {
			config.hasPattern = true;
			DX12LogManager::DebugJsonFunc("MigotoShaderRegexParse",
				"\"stage\":\"sub_section\",\"section\":\"%S\",\"group\":\"%S\"",
				section.name.c_str(), groupName.c_str());
			continue;
		}

		config.originalSection = section.originalName.empty() ? section.name : section.originalName;
		config.sourcePath = section.sourcePath;
		config.sourceDir = section.sourceDir;
		config.iniNamespace = section.iniNamespace;

		DX12LogManager::DebugJsonFunc("MigotoShaderRegexParse",
			"\"stage\":\"main_section_found\",\"section\":\"%S\",\"group\":\"%S\",\"entries\":%zu",
			section.name.c_str(), groupName.c_str(), section.entries.size());

			for (const IniEntry &entry : section.entries) {
				CommandListAction rawAction;
				if (MigotoCommandListParser::ParseActionFromRawLine(entry.rawLine, &rawAction) &&
					(!entry.hasEquals || rawAction.kind == CommandListActionKind::ControlFlow)) {
					MigotoCommandListParser::ResolveActionReferences(&rawAction, entry.iniNamespace);
					config.actions.push_back(rawAction);
					continue;
				}
				if (!entry.hasEquals) {
					DX12LogManager::DebugJsonFunc("MigotoShaderRegexParse",
						"\"stage\":\"entry_no_equals\",\"section\":\"%S\",\"raw\":\"%S\"",
						section.name.c_str(), entry.rawLine.c_str());
					continue;
				}

			std::wstring key = IniDocumentText::ToLower(IniDocumentText::Trim(entry.key));
			std::wstring value = IniDocumentText::Trim(entry.value);
			if (key == L"shader_model") {
				size_t before = config.shaderModels.size();
				MigotoShaderRegexParserText::AppendShaderModels(value, &config.shaderModels);
				DX12LogManager::DebugJsonFunc("MigotoShaderRegexParse",
					"\"stage\":\"shader_model\",\"section\":\"%S\",\"value\":\"%S\",\"before\":%zu,\"after\":%zu",
					section.name.c_str(), value.c_str(), before, config.shaderModels.size());
				continue;
			}
			if (key == L"temps" || key == L"filter_index")
				continue;

			if (key == L"handling") {
				std::wstring handling = IniDocumentText::ToLower(IniDocumentText::Trim(value));
				if (handling == L"skip") {
					config.handlingSkip = true;
					CommandListAction action;
					if (MigotoCommandListParser::ParseActionFromEntry(key, handling, &action)) {
						MigotoCommandListParser::ResolveActionReferences(&action, entry.iniNamespace);
						config.actions.push_back(action);
					}
				}
				continue;
			}

			CommandListAction action;
			if (MigotoCommandListParser::ParseActionFromEntry(key, value, &action)) {
				MigotoCommandListParser::ResolveActionReferences(&action, entry.iniNamespace);
				config.actions.push_back(action);
				continue;
			}

			MigotoCommandListParser::ParseLinksFromEntry(key, entry.value, &config.commandLists);
		}

		MigotoCommandListParser::ResolveLinks(&config.commandLists, section.iniNamespace);
	}

	size_t erasedCount = 0;
	for (auto it = shaderRegexes->begin(); it != shaderRegexes->end();) {
		if (it->second.shaderModels.empty()) {
			DX12LogManager::DebugJsonFunc("MigotoShaderRegexParse",
				"\"stage\":\"erase_empty_models\",\"section\":\"%S\"",
				it->second.section.c_str());
			it = shaderRegexes->erase(it);
			++erasedCount;
		} else {
			++it;
		}
	}
	DX12LogManager::DebugJsonFunc("MigotoShaderRegexParse",
		"\"stage\":\"final\",\"count\":%zu,\"erased\":%zu",
		shaderRegexes->size(), erasedCount);
}

}
