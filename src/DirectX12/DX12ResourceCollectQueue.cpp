#include "DX12ResourceCollectQueue.hpp"

#include <chrono>

#include "DX12LogManager.hpp"
#include "DX12ResourceTrackerHookRuntime.hpp"
#include "DX12ResourceTrackerPrivate.hpp"

DX12ResourceCollectQueue &DX12ResourceCollectQueue::Get()
{
	static DX12ResourceCollectQueue queue;
	return queue;
}

void DX12ResourceCollectQueue::Start()
{
	Get().StartWorker();
}

void DX12ResourceCollectQueue::Stop()
{
	Get().StopWorker();
}

bool DX12ResourceCollectQueue::Enqueue(
	ID3D12Resource *resource,
	const D3D12_RESOURCE_DESC *desc,
	const D3D12_HEAP_PROPERTIES *heapProperties,
	D3D12_RESOURCE_STATES initialState)
{
	return Get().EnqueueTask(resource, desc, heapProperties, initialState);
}

bool DX12ResourceCollectQueue::EnqueueDescriptor(DescriptorRecord &&record, const char *source)
{
	return Get().EnqueueDescriptorTask(std::move(record), source);
}

bool DX12ResourceCollectQueue::EnqueueDescriptorCopyRange(
	ID3D12Device *device,
	D3D12_DESCRIPTOR_HEAP_TYPE type,
	D3D12_CPU_DESCRIPTOR_HANDLE destStart,
	D3D12_CPU_DESCRIPTOR_HANDLE srcStart,
	UINT count,
	const char *source)
{
	return Get().EnqueueDescriptorCopyRangeTask(device, type, destStart, srcStart, count, source);
}

bool DX12ResourceCollectQueue::EnqueueResourceBarriers(
	UINT numBarriers,
	const D3D12_RESOURCE_BARRIER *barriers,
	const char *source)
{
	return Get().EnqueueResourceBarrierTask(numBarriers, barriers, source);
}

void DX12ResourceCollectQueue::DrainForSnapshot(const char *reason)
{
	Get().DrainPendingOnCaller(reason);
}

size_t DX12ResourceCollectQueue::PendingCount()
{
	DX12ResourceCollectQueue &queue = Get();
	std::lock_guard<std::mutex> lock(queue.mutex);
	return queue.tasks.size();
}

void DX12ResourceCollectQueue::StartWorker()
{
	bool expected = false;
	if (!started.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
		return;

	stopRequested.store(false, std::memory_order_release);
	worker = std::thread([this]() { WorkerLoop(); });
	DX12LogManager::JsonFunc("DX12ResourceCollectQueue",
		"\"event\":\"Start\",\"thread\":\"%p\"",
		worker.native_handle());
}

void DX12ResourceCollectQueue::StopWorker()
{
	if (!started.load(std::memory_order_acquire))
		return;

	DX12LogManager::JsonFunc("DX12ResourceCollectQueue",
		"\"event\":\"StopRequested\",\"pending\":%zu,\"enqueued\":%llu,\"dequeued\":%llu,\"completed\":%llu,\"dropped\":%llu,\"addRefFailed\":%llu,\"maxDepth\":%llu",
		PendingCount(),
		static_cast<unsigned long long>(enqueued.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(dequeued.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(completed.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(dropped.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(addRefFailed.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(maxDepth.load(std::memory_order_relaxed)));

	stopRequested.store(true, std::memory_order_release);
	wake.notify_all();
	if (worker.joinable())
		worker.join();
	started.store(false, std::memory_order_release);

	DX12LogManager::JsonFunc("DX12ResourceCollectQueue",
		"\"event\":\"Stopped\",\"pending\":%zu,\"enqueued\":%llu,\"dequeued\":%llu,\"completed\":%llu,\"dropped\":%llu,\"addRefFailed\":%llu,\"maxDepth\":%llu",
		PendingCount(),
		static_cast<unsigned long long>(enqueued.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(dequeued.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(completed.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(dropped.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(addRefFailed.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(maxDepth.load(std::memory_order_relaxed)));
}

bool DX12ResourceCollectQueue::EnqueueTask(
	ID3D12Resource *resource,
	const D3D12_RESOURCE_DESC *desc,
	const D3D12_HEAP_PROPERTIES *heapProperties,
	D3D12_RESOURCE_STATES initialState)
{
	if (!resource)
		return false;

	StartWorker();

	DX12ResourceCollectTask task;
	task.kind = DX12ResourceCollectTaskKind::Resource;
	task.resource = resource;
	task.hasDesc = desc != nullptr;
	if (desc)
		task.desc = *desc;
	task.hasHeapProperties = heapProperties != nullptr;
	if (heapProperties)
		task.heapProperties = *heapProperties;
	task.initialState = initialState;
	task.sequence = nextSequence.fetch_add(1, std::memory_order_relaxed);
	task.enqueueThreadId = GetCurrentThreadId();

	if (!DX12ResourceSnapshotUtils::TryAddRef(resource)) {
		addRefFailed.fetch_add(1, std::memory_order_relaxed);
		DX12LogManager::JsonFunc("DX12ResourceCollectQueue",
			"\"event\":\"EnqueueAddRefFailed\",\"seq\":%llu,\"resource\":\"%p\"",
			static_cast<unsigned long long>(task.sequence), resource);
		return false;
	}

	size_t depth = 0;
	{
		std::lock_guard<std::mutex> lock(mutex);
		tasks.push_back(task);
		depth = tasks.size();
	}
	enqueued.fetch_add(1, std::memory_order_relaxed);
	UpdateMaxDepth(depth);
	LogStats("Enqueue", task.sequence, depth);
	wake.notify_one();
	return true;
}

bool DX12ResourceCollectQueue::EnqueueDescriptorTask(DescriptorRecord &&record, const char *source)
{
	StartWorker();

	DX12ResourceCollectTask task;
	task.kind = DX12ResourceCollectTaskKind::Descriptor;
	task.descriptor = std::move(record);
	task.source = source;
	task.sequence = nextSequence.fetch_add(1, std::memory_order_relaxed);
	task.enqueueThreadId = GetCurrentThreadId();

	size_t depth = 0;
	{
		std::lock_guard<std::mutex> lock(mutex);
		tasks.push_back(std::move(task));
		depth = tasks.size();
	}
	enqueued.fetch_add(1, std::memory_order_relaxed);
	UpdateMaxDepth(depth);
	LogStats("EnqueueDescriptor", task.sequence, depth);
	wake.notify_one();
	return true;
}

bool DX12ResourceCollectQueue::EnqueueDescriptorCopyRangeTask(
	ID3D12Device *device,
	D3D12_DESCRIPTOR_HEAP_TYPE type,
	D3D12_CPU_DESCRIPTOR_HANDLE destStart,
	D3D12_CPU_DESCRIPTOR_HANDLE srcStart,
	UINT count,
	const char *source)
{
	if (!device || count == 0)
		return false;

	StartWorker();

	DX12ResourceCollectTask task;
	task.kind = DX12ResourceCollectTaskKind::DescriptorCopyRange;
	task.device = device;
	task.device->AddRef();
	task.descriptorHeapType = type;
	task.descriptorDestStart = destStart;
	task.descriptorSrcStart = srcStart;
	task.descriptorCount = count;
	task.source = source;
	task.sequence = nextSequence.fetch_add(1, std::memory_order_relaxed);
	task.enqueueThreadId = GetCurrentThreadId();

	size_t depth = 0;
	{
		std::lock_guard<std::mutex> lock(mutex);
		tasks.push_back(task);
		depth = tasks.size();
	}
	enqueued.fetch_add(1, std::memory_order_relaxed);
	UpdateMaxDepth(depth);
	LogStats("EnqueueDescriptorCopyRange", task.sequence, depth);
	wake.notify_one();
	return true;
}

bool DX12ResourceCollectQueue::EnqueueResourceBarrierTask(
	UINT numBarriers,
	const D3D12_RESOURCE_BARRIER *barriers,
	const char *source)
{
	if (!barriers || numBarriers == 0)
		return false;

	StartWorker();

	DX12ResourceCollectTask task;
	task.kind = DX12ResourceCollectTaskKind::ResourceBarrier;
	task.source = source;
	task.sequence = nextSequence.fetch_add(1, std::memory_order_relaxed);
	task.enqueueThreadId = GetCurrentThreadId();
	task.barriers.assign(barriers, barriers + numBarriers);

	UINT addRefFailedCount = 0;
	for (D3D12_RESOURCE_BARRIER &barrier : task.barriers) {
		if (!TryAddRefBarrierResources(&barrier))
			++addRefFailedCount;
	}
	if (addRefFailedCount != 0)
		addRefFailed.fetch_add(addRefFailedCount, std::memory_order_relaxed);

	size_t depth = 0;
	{
		std::lock_guard<std::mutex> lock(mutex);
		tasks.push_back(std::move(task));
		depth = tasks.size();
	}
	enqueued.fetch_add(1, std::memory_order_relaxed);
	UpdateMaxDepth(depth);
	LogStats("EnqueueResourceBarrier", task.sequence, depth);
	DX12LogManager::JsonFunc("DX12ResourceCollectQueue",
		"\"event\":\"EnqueueResourceBarrierDetail\",\"seq\":%llu,\"source\":\"%s\",\"count\":%u,\"addRefFailed\":%u,\"enqueueThreadId\":%lu",
		static_cast<unsigned long long>(task.sequence),
		source ? source : "",
		numBarriers,
		addRefFailedCount,
		task.enqueueThreadId);
	wake.notify_one();
	return true;
}

void DX12ResourceCollectQueue::WorkerLoop()
{
	DX12LogManager::JsonFunc("DX12ResourceCollectQueue",
		"\"event\":\"WorkerEnter\",\"threadId\":%lu",
		GetCurrentThreadId());

	for (;;) {
		DX12ResourceCollectTask task;
		size_t depthAfterPop = 0;
		{
			std::unique_lock<std::mutex> lock(mutex);
			wake.wait(lock, [this]() {
				return stopRequested.load(std::memory_order_acquire) || !tasks.empty();
			});
			if (tasks.empty()) {
				if (stopRequested.load(std::memory_order_acquire))
					break;
				continue;
			}
			task = tasks.front();
			tasks.pop_front();
			depthAfterPop = tasks.size();
		}
		dequeued.fetch_add(1, std::memory_order_relaxed);
		LogStats("Dequeue", task.sequence, depthAfterPop);
		ProcessTask(&task, "Worker");
	}

	DrainPendingOnCaller("WorkerStop");
	DX12LogManager::JsonFunc("DX12ResourceCollectQueue",
		"\"event\":\"WorkerExit\",\"threadId\":%lu,\"pending\":%zu",
		GetCurrentThreadId(), PendingCount());
}

void DX12ResourceCollectQueue::DrainPendingOnCaller(const char *reason)
{
	for (;;) {
		DX12ResourceCollectTask task;
		size_t depthAfterPop = 0;
		{
			std::lock_guard<std::mutex> lock(mutex);
			if (tasks.empty())
				break;
			task = tasks.front();
			tasks.pop_front();
			depthAfterPop = tasks.size();
		}
		dequeued.fetch_add(1, std::memory_order_relaxed);
		DX12LogManager::JsonFunc("DX12ResourceCollectQueue",
			"\"event\":\"DrainPop\",\"reason\":\"%s\",\"seq\":%llu,\"pending\":%zu",
			reason ? reason : "",
			static_cast<unsigned long long>(task.sequence),
			depthAfterPop);
		ProcessTask(&task, reason ? reason : "Drain");
	}
}

void DX12ResourceCollectQueue::ProcessTask(DX12ResourceCollectTask *task, const char *source)
{
	if (!task)
		return;

	switch (task->kind) {
	case DX12ResourceCollectTaskKind::Resource:
		ProcessResourceTask(task, source);
		return;
	case DX12ResourceCollectTaskKind::Descriptor:
		ProcessDescriptorTask(task, source);
		return;
	case DX12ResourceCollectTaskKind::DescriptorCopyRange:
		ProcessDescriptorCopyRangeTask(task, source);
		return;
	case DX12ResourceCollectTaskKind::ResourceBarrier:
		ProcessResourceBarrierTask(task, source);
		return;
	default:
		return;
	}
}

void DX12ResourceCollectQueue::ProcessResourceTask(DX12ResourceCollectTask *task, const char *source)
{
	if (!task || !task->resource)
		return;

	const auto begin = std::chrono::steady_clock::now();
	DX12LogManager::JsonFunc("DX12ResourceCollectQueue",
		"\"event\":\"ProcessResourceBegin\",\"source\":\"%s\",\"seq\":%llu,\"resource\":\"%p\",\"hasDesc\":%u,\"hasHeap\":%u,\"initialState\":%u,\"enqueueThreadId\":%lu,\"workerThreadId\":%lu",
		source ? source : "",
		static_cast<unsigned long long>(task->sequence),
		task->resource,
		task->hasDesc ? 1 : 0,
		task->hasHeapProperties ? 1 : 0,
		static_cast<UINT>(task->initialState),
		task->enqueueThreadId,
		GetCurrentThreadId());

	RecordResource(
		task->resource,
		task->hasDesc ? &task->desc : nullptr,
		task->hasHeapProperties ? &task->heapProperties : nullptr,
		task->initialState);
	DX12ResourceTrackerHookRuntime::AttachResourceReleaseTracker(task->resource);

	const auto end = std::chrono::steady_clock::now();
	const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count();
	completed.fetch_add(1, std::memory_order_relaxed);
	DX12LogManager::JsonFunc("DX12ResourceCollectQueue",
		"\"event\":\"ProcessResourceComplete\",\"source\":\"%s\",\"seq\":%llu,\"resource\":\"%p\",\"elapsedUs\":%lld,\"completed\":%llu",
		source ? source : "",
		static_cast<unsigned long long>(task->sequence),
		task->resource,
		static_cast<long long>(elapsedUs),
		static_cast<unsigned long long>(completed.load(std::memory_order_relaxed)));
	ReleaseTaskResource(task);
}

void DX12ResourceCollectQueue::ProcessDescriptorTask(DX12ResourceCollectTask *task, const char *source)
{
	if (!task)
		return;

	const auto begin = std::chrono::steady_clock::now();
	DX12LogManager::JsonFunc("DX12ResourceCollectQueue",
		"\"event\":\"ProcessDescriptorBegin\",\"source\":\"%s\",\"descriptorSource\":\"%s\",\"seq\":%llu,\"kindId\":%u,\"cpuHandle\":\"0x%zx\",\"resource\":\"%p\",\"enqueueThreadId\":%lu,\"workerThreadId\":%lu",
		source ? source : "",
		task->source ? task->source : "",
		static_cast<unsigned long long>(task->sequence),
		task->descriptor.kindId,
		task->descriptor.cpuHandle,
		task->descriptor.resource,
		task->enqueueThreadId,
		GetCurrentThreadId());

	PrepareDescriptorValueId(&task->descriptor);
	if (!DescriptorValueUnchanged(task->descriptor.cpuHandle, task->descriptor.valueId)) {
		if (ShouldTrackResourceMetadata() &&
			task->descriptor.kindId == DescriptorKind::CBV_ID &&
			task->descriptor.gpuVirtualAddress &&
			!task->descriptor.resource) {
			ResolveBufferByGpuVa(
				task->descriptor.gpuVirtualAddress,
				task->descriptor.viewSize,
				&task->descriptor);
		}
		RecordDescriptor(std::move(task->descriptor));
		task->descriptor.resource = nullptr;
		task->descriptor.ownsResource = false;
		task->descriptor.counterResource = nullptr;
		task->descriptor.ownsCounterResource = false;
	}

	const auto end = std::chrono::steady_clock::now();
	const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count();
	completed.fetch_add(1, std::memory_order_relaxed);
	DX12LogManager::JsonFunc("DX12ResourceCollectQueue",
		"\"event\":\"ProcessDescriptorComplete\",\"source\":\"%s\",\"descriptorSource\":\"%s\",\"seq\":%llu,\"elapsedUs\":%lld,\"completed\":%llu",
		source ? source : "",
		task->source ? task->source : "",
		static_cast<unsigned long long>(task->sequence),
		static_cast<long long>(elapsedUs),
		static_cast<unsigned long long>(completed.load(std::memory_order_relaxed)));
	ReleaseTaskDescriptor(task);
}

void DX12ResourceCollectQueue::ProcessDescriptorCopyRangeTask(DX12ResourceCollectTask *task, const char *source)
{
	if (!task || !task->device || task->descriptorCount == 0)
		return;

	const auto begin = std::chrono::steady_clock::now();
	DX12LogManager::JsonFunc("DX12ResourceCollectQueue",
		"\"event\":\"ProcessDescriptorCopyRangeBegin\",\"source\":\"%s\",\"descriptorSource\":\"%s\",\"seq\":%llu,\"device\":\"%p\",\"type\":%u,\"dest\":\"0x%zx\",\"src\":\"0x%zx\",\"count\":%u,\"enqueueThreadId\":%lu,\"workerThreadId\":%lu",
		source ? source : "",
		task->source ? task->source : "",
		static_cast<unsigned long long>(task->sequence),
		task->device,
		static_cast<UINT>(task->descriptorHeapType),
		task->descriptorDestStart.ptr,
		task->descriptorSrcStart.ptr,
		task->descriptorCount,
		task->enqueueThreadId,
		GetCurrentThreadId());

	RecordDescriptorCopyRange(
		task->device,
		task->descriptorHeapType,
		task->descriptorDestStart,
		task->descriptorSrcStart,
		task->descriptorCount);

	const auto end = std::chrono::steady_clock::now();
	const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count();
	completed.fetch_add(1, std::memory_order_relaxed);
	DX12LogManager::JsonFunc("DX12ResourceCollectQueue",
		"\"event\":\"ProcessDescriptorCopyRangeComplete\",\"source\":\"%s\",\"descriptorSource\":\"%s\",\"seq\":%llu,\"count\":%u,\"elapsedUs\":%lld,\"completed\":%llu",
		source ? source : "",
		task->source ? task->source : "",
		static_cast<unsigned long long>(task->sequence),
		task->descriptorCount,
		static_cast<long long>(elapsedUs),
		static_cast<unsigned long long>(completed.load(std::memory_order_relaxed)));
	ReleaseTaskDevice(task);
}

void DX12ResourceCollectQueue::ProcessResourceBarrierTask(DX12ResourceCollectTask *task, const char *source)
{
	if (!task || task->barriers.empty())
		return;

	const auto begin = std::chrono::steady_clock::now();
	DX12LogManager::JsonFunc("DX12ResourceCollectQueue",
		"\"event\":\"ProcessResourceBarrierBegin\",\"source\":\"%s\",\"barrierSource\":\"%s\",\"seq\":%llu,\"count\":%zu,\"enqueueThreadId\":%lu,\"workerThreadId\":%lu",
		source ? source : "",
		task->source ? task->source : "",
		static_cast<unsigned long long>(task->sequence),
		task->barriers.size(),
		task->enqueueThreadId,
		GetCurrentThreadId());

	DX12RecordResourceBarrier(static_cast<UINT>(task->barriers.size()), task->barriers.data());

	const auto end = std::chrono::steady_clock::now();
	const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count();
	completed.fetch_add(1, std::memory_order_relaxed);
	DX12LogManager::JsonFunc("DX12ResourceCollectQueue",
		"\"event\":\"ProcessResourceBarrierComplete\",\"source\":\"%s\",\"barrierSource\":\"%s\",\"seq\":%llu,\"count\":%zu,\"elapsedUs\":%lld,\"completed\":%llu",
		source ? source : "",
		task->source ? task->source : "",
		static_cast<unsigned long long>(task->sequence),
		task->barriers.size(),
		static_cast<long long>(elapsedUs),
		static_cast<unsigned long long>(completed.load(std::memory_order_relaxed)));

	for (D3D12_RESOURCE_BARRIER &barrier : task->barriers) {
		ReleaseBarrierResources(&barrier);
	}
	task->barriers.clear();
}

bool DX12ResourceCollectQueue::TryAddRefBarrierResources(D3D12_RESOURCE_BARRIER *barrier)
{
	if (!barrier)
		return true;

	bool allSucceeded = true;
	switch (barrier->Type) {
	case D3D12_RESOURCE_BARRIER_TYPE_TRANSITION:
		if (barrier->Transition.pResource &&
			!DX12ResourceSnapshotUtils::TryAddRef(barrier->Transition.pResource)) {
			barrier->Transition.pResource = nullptr;
			allSucceeded = false;
		}
		break;
	case D3D12_RESOURCE_BARRIER_TYPE_ALIASING:
		if (barrier->Aliasing.pResourceBefore &&
			!DX12ResourceSnapshotUtils::TryAddRef(barrier->Aliasing.pResourceBefore)) {
			barrier->Aliasing.pResourceBefore = nullptr;
			allSucceeded = false;
		}
		if (barrier->Aliasing.pResourceAfter &&
			!DX12ResourceSnapshotUtils::TryAddRef(barrier->Aliasing.pResourceAfter)) {
			barrier->Aliasing.pResourceAfter = nullptr;
			allSucceeded = false;
		}
		break;
	case D3D12_RESOURCE_BARRIER_TYPE_UAV:
		if (barrier->UAV.pResource &&
			!DX12ResourceSnapshotUtils::TryAddRef(barrier->UAV.pResource)) {
			barrier->UAV.pResource = nullptr;
			allSucceeded = false;
		}
		break;
	default:
		break;
	}
	return allSucceeded;
}

void DX12ResourceCollectQueue::ReleaseBarrierResources(D3D12_RESOURCE_BARRIER *barrier)
{
	if (!barrier)
		return;

	switch (barrier->Type) {
	case D3D12_RESOURCE_BARRIER_TYPE_TRANSITION:
		DX12ResourceSnapshotUtils::Release(barrier->Transition.pResource);
		barrier->Transition.pResource = nullptr;
		break;
	case D3D12_RESOURCE_BARRIER_TYPE_ALIASING:
		DX12ResourceSnapshotUtils::Release(barrier->Aliasing.pResourceBefore);
		DX12ResourceSnapshotUtils::Release(barrier->Aliasing.pResourceAfter);
		barrier->Aliasing.pResourceBefore = nullptr;
		barrier->Aliasing.pResourceAfter = nullptr;
		break;
	case D3D12_RESOURCE_BARRIER_TYPE_UAV:
		DX12ResourceSnapshotUtils::Release(barrier->UAV.pResource);
		barrier->UAV.pResource = nullptr;
		break;
	default:
		break;
	}
}

void DX12ResourceCollectQueue::ReleaseTaskResource(DX12ResourceCollectTask *task)
{
	if (!task || !task->resource)
		return;
	DX12ResourceSnapshotUtils::Release(task->resource);
	task->resource = nullptr;
}

void DX12ResourceCollectQueue::ReleaseTaskDevice(DX12ResourceCollectTask *task)
{
	if (!task || !task->device)
		return;
	task->device->Release();
	task->device = nullptr;
}

void DX12ResourceCollectQueue::ReleaseTaskDescriptor(DX12ResourceCollectTask *task)
{
	if (!task)
		return;
	ReleaseDescriptorRecordResources(&task->descriptor);
}

void DX12ResourceCollectQueue::UpdateMaxDepth(size_t depth)
{
	uint64_t current = maxDepth.load(std::memory_order_relaxed);
	while (depth > current &&
		!maxDepth.compare_exchange_weak(current, depth, std::memory_order_relaxed)) {
	}
}

void DX12ResourceCollectQueue::LogStats(const char *event, uint64_t sequence, size_t depth)
{
	DX12LogManager::JsonFunc("DX12ResourceCollectQueue",
		"\"event\":\"%s\",\"seq\":%llu,\"pending\":%zu,\"enqueued\":%llu,\"dequeued\":%llu,\"completed\":%llu,\"maxDepth\":%llu",
		event ? event : "",
		static_cast<unsigned long long>(sequence),
		depth,
		static_cast<unsigned long long>(enqueued.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(dequeued.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(completed.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(maxDepth.load(std::memory_order_relaxed)));
}
