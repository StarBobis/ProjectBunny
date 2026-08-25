#pragma once

// GPU readback/copy machinery for resource dumps.
// Split out of Resources/DX12ResourceDump.h; include that umbrella header from existing code.
#include "../DX12Runtime.h"
#include <d3d11.h>

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
	std::vector<ID3D12Resource*> sources;
	std::vector<ID3D12Resource*> readbacks;
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
	for (ID3D12Resource *source : copy->sources) {
		if (source)
			DX12ReleaseRetainedResource(source);
	}
	for (ID3D12Resource *readback : copy->readbacks) {
		if (readback)
			readback->Release();
	}
	copy->sources.clear();
	copy->readbacks.clear();
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
		copy.sources.push_back(task->sourceRetained ? task->source : nullptr);
		copy.readbacks.push_back(task->readback);
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

static void RetireCopyBatch(
	const std::vector<DumpTask*> &tasks,
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
	copy.sources.reserve(tasks.size());
	copy.readbacks.reserve(tasks.size());
	for (DumpTask *task : tasks) {
		if (!task)
			continue;
		copy.sources.push_back(task->sourceRetained ? task->source : nullptr);
		copy.readbacks.push_back(task->readback);
		task->source = nullptr;
		task->sourceRetained = false;
		task->readback = nullptr;
		task->keepReadbackAlive = false;
	}
	AcquireSRWLockExclusive(&RetiredCopyLock);
	RetiredCopies.push_back(std::move(copy));
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

static bool TryExecuteCopyBatch(
	ID3D12Device *device,
	ID3D12CommandQueue *queue,
	const std::vector<DumpTask*> &eligibleTasks,
	bool *readbackOkOut,
	bool *batchAbortedOut,
	size_t *copiedOut)
{
	if (readbackOkOut)
		*readbackOkOut = true;
	if (batchAbortedOut)
		*batchAbortedOut = false;
	if (copiedOut)
		*copiedOut = 0;
	if (!device || !queue || eligibleTasks.size() <= 1)
		return false;

	DrainRetiredCopies();

	ID3D12CommandAllocator *allocator = nullptr;
	ID3D12GraphicsCommandList *commandList = nullptr;
	HRESULT hr = S_OK;
	if (!DX12ResourceDumpGpuCopy::DeviceRemoved(device, &hr)) {
		hr = device->CreateCommandAllocator(
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			IID_PPV_ARGS(&allocator));
		if (SUCCEEDED(hr)) {
			hr = device->CreateCommandList(
				0,
				D3D12_COMMAND_LIST_TYPE_DIRECT,
				allocator,
				nullptr,
				IID_PPV_ARGS(&commandList));
		}
	}

	if (FAILED(hr) || !allocator || !commandList) {
		if (commandList)
			commandList->Release();
		if (allocator)
			allocator->Release();
		return false;
	}

	bool recorded = true;
	for (DumpTask *task : eligibleTasks) {
		if (!task || !task->source || !task->readback) {
			recorded = false;
			break;
		}
		__try {
			if (task->needsBarrier) {
				DX12ResourceDumpGpuCopy::WriteResourceBarrier(
					commandList,
					task->source,
					task->sourceState,
					D3D12_RESOURCE_STATE_COPY_SOURCE);
			}

			if (task->isTexture) {
				for (UINT subresource = 0;
					 subresource < task->textureLayouts.size();
					 ++subresource) {
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
				commandList->CopyBufferRegion(
					task->readback,
					0,
					task->source,
					task->sourceOffset,
					task->copyBytes);
			}

			if (task->needsBarrier) {
				DX12ResourceDumpGpuCopy::WriteResourceBarrier(
					commandList,
					task->source,
					D3D12_RESOURCE_STATE_COPY_SOURCE,
					task->sourceState);
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			recorded = false;
		}
		if (!recorded)
			break;
	}

	if (!recorded) {
		commandList->Release();
		allocator->Release();
		return false;
	}

	hr = commandList->Close();
	if (FAILED(hr)) {
		commandList->Release();
		allocator->Release();
		return false;
	}

	ID3D12CommandList *lists[] = { commandList };
	DWORD waitResult = WAIT_FAILED;
	ID3D12Fence *pendingFence = nullptr;
	HANDLE pendingEvent = nullptr;
	bool executed = false;
	__try {
		LARGE_INTEGER executeBegin = DX12OuterTimingProbeUtils::Now();
		queue->ExecuteCommandLists(1, lists);
		DX12OuterTimingProbeUtils::LogQueueExecuteSlice(
			"dump_copy_execute_batch",
			queue,
			1,
			DX12OuterTimingProbeUtils::ElapsedUs(executeBegin));
		executed = DX12ResourceDumpGpuCopy::WaitForFence(
			queue,
			device,
			&waitResult,
			&pendingFence,
			&pendingEvent);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		executed = false;
	}

	if (executed) {
		commandList->Release();
		allocator->Release();
		for (DumpTask *task : eligibleTasks) {
			task->copied = true;
			task->skipNote = nullptr;
		}
		if (copiedOut)
			*copiedOut = eligibleTasks.size();
		DX12FrameAnalysis::LogJsonFunc(
			"ResourceCopyResultDetail",
			"\"mode\":\"batched\",\"eligible\":%zu,\"copied\":%zu,\"failed\":0",
			eligibleTasks.size(),
			eligibleTasks.size());
		return true;
	}

	HRESULT removedHr = S_OK;
	const bool deviceRemoved =
		DX12ResourceDumpGpuCopy::DeviceRemoved(device, &removedHr);
	const bool timedOut = waitResult == WAIT_TIMEOUT;
	const char *failureNote = deviceRemoved ?
		"copy_aborted_device_removed" :
		(timedOut ? "copy_aborted_wait_timeout" : "copy_execute_failed");
	for (DumpTask *task : eligibleTasks) {
		if (!task->skipNote)
			task->skipNote = failureNote;
	}
	if (readbackOkOut)
		*readbackOkOut = false;
	if (batchAbortedOut)
		*batchAbortedOut = deviceRemoved || timedOut;

	if (deviceRemoved) {
		if (pendingEvent)
			CloseHandle(pendingEvent);
		if (pendingFence)
			pendingFence->Release();
		commandList->Release();
		allocator->Release();
	} else {
		RetireCopyBatch(
			eligibleTasks,
			commandList,
			allocator,
			pendingFence,
			pendingEvent,
			device);
		DX12FrameAnalysis::LogJsonFunc(
			"ResourceCopyObjectsRetained",
			"\"reason\":\"gpu_work_not_completed\",\"tasks\":%zu,\"mode\":\"batched\"",
			eligibleTasks.size());
	}

	DX12FrameAnalysis::LogJsonFunc(
		"ResourceCopyResultDetail",
		"\"mode\":\"batched\",\"eligible\":%zu,\"copied\":0,\"failed\":%zu",
		eligibleTasks.size(),
		eligibleTasks.size());
	return true;
}

static size_t ExecuteReadbackCopyTasks(
	ID3D12Device *device, ID3D12CommandQueue *queue, std::vector<DumpTask> *tasks,
	bool *readbackOkOut, bool *batchAbortedOut)
{
	if (readbackOkOut)
		*readbackOkOut = true;
	if (batchAbortedOut)
		*batchAbortedOut = false;
	if (!tasks)
		return 0;

	std::vector<DumpTask*> eligibleTasks;
	eligibleTasks.reserve(tasks->size());
	for (DumpTask &task : *tasks) {
		if (!task.directMap && task.ready && task.readback) {
			task.copied = false;
			eligibleTasks.push_back(&task);
		}
	}
	if (eligibleTasks.empty())
		return DX12ResourceDumpPrepare::CountCopiedTasks(*tasks);

	size_t batchCopied = 0;
	if (TryExecuteCopyBatch(
			device,
			queue,
			eligibleTasks,
			readbackOkOut,
			batchAbortedOut,
			&batchCopied)) {
		return batchCopied;
	}

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
			if (batchAbortedOut)
				*batchAbortedOut = true;
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


