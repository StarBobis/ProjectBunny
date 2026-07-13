#pragma once

#include "DX12FeatureInterfaces.hpp"

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




class DX12ResourceDumpGpuCopy
{
public:
class RetiredCopy
{
public:
	ID3D12Fence *fence = nullptr;
	ID3D12Device *device = nullptr;
	HANDLE eventHandle = nullptr;
	ID3D12GraphicsCommandList *commandList = nullptr;
	ID3D12CommandAllocator *allocator = nullptr;
	ID3D12Resource *source = nullptr;
	ID3D12Resource *readback = nullptr;
};

inline static SRWLOCK RetiredCopyLock = SRWLOCK_INIT;
inline static std::vector<RetiredCopy> RetiredCopies;
inline static std::atomic<size_t> RetiredCopyCount{0};

static void ReleaseRetiredCopy(RetiredCopy *copy)
{
	if (!copy)
		return;
	if (copy->eventHandle)
		CloseHandle(copy->eventHandle);
	if (copy->fence)
		copy->fence->Release();
	if (copy->device)
		copy->device->Release();
	if (copy->commandList)
		copy->commandList->Release();
	if (copy->allocator)
		copy->allocator->Release();
	if (copy->source)
		DX12ReleaseRetainedResource(copy->source);
	if (copy->readback)
		copy->readback->Release();
	*copy = RetiredCopy();
}

static void DrainRetiredCopies()
{
	if (RetiredCopyCount.load(std::memory_order_acquire) == 0)
		return;
	AcquireSRWLockExclusive(&RetiredCopyLock);
	for (size_t i = 0; i < RetiredCopies.size();) {
		const UINT64 completed = RetiredCopies[i].fence ?
			RetiredCopies[i].fence->GetCompletedValue() : 0;
		const bool deviceRemoved = RetiredCopies[i].device &&
			FAILED(RetiredCopies[i].device->GetDeviceRemovedReason());
		if (completed < 1 && completed != UINT64_MAX && !deviceRemoved) {
			++i;
			continue;
		}
		ReleaseRetiredCopy(&RetiredCopies[i]);
		RetiredCopies[i] = RetiredCopies.back();
		RetiredCopies.pop_back();
	}
	RetiredCopyCount.store(RetiredCopies.size(), std::memory_order_release);
	ReleaseSRWLockExclusive(&RetiredCopyLock);
}

static void RetireCopy(
	DumpTask *task,
	ID3D12GraphicsCommandList *commandList,
	ID3D12CommandAllocator *allocator,
	ID3D12Fence *fence,
	HANDLE eventHandle,
	ID3D12Device *device)
{
	RetiredCopy copy;
	copy.fence = fence;
	copy.device = device;
	if (copy.device)
		copy.device->AddRef();
	copy.eventHandle = eventHandle;
	copy.commandList = commandList;
	copy.allocator = allocator;
	if (task) {
		copy.source = task->sourceRetained ? task->source : nullptr;
		copy.readback = task->readback;
		task->source = nullptr;
		task->sourceRetained = false;
		task->readback = nullptr;
		task->keepReadbackAlive = false;
	}
	AcquireSRWLockExclusive(&RetiredCopyLock);
	RetiredCopies.push_back(copy);
	RetiredCopyCount.store(RetiredCopies.size(), std::memory_order_release);
	ReleaseSRWLockExclusive(&RetiredCopyLock);
}

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

static bool WaitForFence(
	ID3D12CommandQueue *queue,
	ID3D12Device *device,
	DWORD *waitResultOut = nullptr,
	ID3D12Fence **pendingFenceOut = nullptr,
	HANDLE *pendingEventOut = nullptr)
{
	if (pendingFenceOut)
		*pendingFenceOut = nullptr;
	if (pendingEventOut)
		*pendingEventOut = nullptr;
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
	if (ok || !pendingFenceOut || !pendingEventOut) {
		CloseHandle(eventHandle);
		fence->Release();
	} else {
		*pendingFenceOut = fence;
		*pendingEventOut = eventHandle;
	}
	return ok;
}


static bool ExecuteCopyTask(
	ID3D12Device *device, ID3D12CommandQueue *queue, DumpTask *task)
{
	if (!device || !queue || !task || !task->ready || !task->readback)
		return false;
	DrainRetiredCopies();
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
	ID3D12Fence *pendingFence = nullptr;
	HANDLE pendingEvent = nullptr;
	__try {
		LARGE_INTEGER executeBegin = DX12OuterTimingProbeUtils::Now();
		queue->ExecuteCommandLists(1, lists);
		DX12OuterTimingProbeUtils::LogQueueExecuteSlice(
			"dump_copy_execute",
			queue,
			1,
			DX12OuterTimingProbeUtils::ElapsedUs(executeBegin));
		ok = DX12ResourceDumpGpuCopy::WaitForFence(
			queue, device, &waitResult, &pendingFence, &pendingEvent);
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
			if (pendingEvent)
				CloseHandle(pendingEvent);
			if (pendingFence)
				pendingFence->Release();
			commandList->Release();
			allocator->Release();
		} else {
			ID3D12Resource *retiredSource = task->source;
			RetireCopy(
				task, commandList, allocator, pendingFence, pendingEvent, device);
			DX12FrameAnalysis::LogJsonFunc("ResourceCopyObjectsRetained",
				"\"reason\":\"gpu_work_not_completed\",\"source\":\"%p\",\"bytes\":%llu",
				retiredSource, static_cast<unsigned long long>(task->copyBytes));
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
	DX12LogManager::Fields("DX12ResourceDump",
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
