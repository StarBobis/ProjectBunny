#include "DX12ResourceCollectQueue.hpp"

#include <chrono>

#include "DX12LogManager.hpp"
#include "DX12ResourceTrackerHookRuntime.hpp"
#include "DX12ResourceTrackerPrivate.hpp"

static constexpr uint64_t kResourceQueueStatsInterval = 4096;
static constexpr int64_t kResourceQueueSlowTaskUs = 2000;

void DX12ResourceCollectQueue::Start()
{
	bool expected = false;
	if (!started.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
		return;

	stopRequested.store(false, std::memory_order_release);
	worker = std::thread([]() { WorkerLoop(); });
	DX12LogManager::JsonFunc("DX12ResourceCollectQueue",
		"\"event\":\"Start\",\"thread\":\"%p\"",
		worker.native_handle());
}

void DX12ResourceCollectQueue::Stop()
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

bool DX12ResourceCollectQueue::Enqueue(
	ID3D12Resource *resource,
	const D3D12_RESOURCE_DESC *desc,
	const D3D12_HEAP_PROPERTIES *heapProperties,
	const PlacedResourceInfo *placedInfo,
	D3D12_RESOURCE_STATES initialState)
{
	if (!resource)
		return false;

	Start();

	DX12ResourceCollectTask task;
	task.kind = DX12ResourceCollectTaskKind::Resource;
	task.resource = resource;
	task.hasDesc = desc != nullptr;
	if (desc)
		task.desc = *desc;
	task.hasHeapProperties = heapProperties != nullptr;
	if (heapProperties)
		task.heapProperties = *heapProperties;
	task.hasPlacedInfo = placedInfo && placedInfo->hasPlacedHeap;
	if (placedInfo)
		task.placedInfo = *placedInfo;
	task.initialState = initialState;
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
	LogStats("EnqueueResource", task.sequence, depth);
	wake.notify_one();
	return true;
}

bool DX12ResourceCollectQueue::EnqueueResourceBarriers(
	UINT numBarriers,
	const D3D12_RESOURCE_BARRIER *barriers,
	const char *source)
{
	if (!barriers || numBarriers == 0)
		return false;

	Start();

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

bool DX12ResourceCollectQueue::EnqueueRootSignature(
	ID3D12RootSignature *rootSignature,
	UINT nodeMask,
	const void *blob,
	SIZE_T blobLength,
	const char *source)
{
	if (!rootSignature || !blob || blobLength == 0)
		return false;

	Start();

	DX12ResourceCollectTask task;
	task.kind = DX12ResourceCollectTaskKind::RootSignature;
	task.rootSignature = rootSignature;
	task.rootSignature->AddRef();
	task.rootSignatureNodeMask = nodeMask;
	task.rootSignatureBlob.assign(
		static_cast<const uint8_t*>(blob),
		static_cast<const uint8_t*>(blob) + blobLength);
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
	LogStats("EnqueueRootSignature", task.sequence, depth);
	DX12LogManager::JsonFunc("DX12ResourceCollectQueue",
		"\"event\":\"EnqueueRootSignatureDetail\",\"seq\":%llu,\"source\":\"%s\",\"rootSignature\":\"%p\",\"blobSize\":%zu,\"nodeMask\":%u,\"enqueueThreadId\":%lu",
		static_cast<unsigned long long>(task.sequence),
		source ? source : "",
		rootSignature,
		blobLength,
		nodeMask,
		task.enqueueThreadId);
	wake.notify_one();
	return true;
}

bool DX12ResourceCollectQueue::EnqueuePsoRootSignature(
	UINT64 psoIndex,
	const char *kind,
	ID3D12PipelineState *pipelineState,
	ID3D12RootSignature *rootSignature,
	const char *source)
{
	if (!psoIndex)
		return false;

	Start();

	DX12ResourceCollectTask task;
	task.kind = DX12ResourceCollectTaskKind::PsoRootSignature;
	task.psoIndex = psoIndex;
	task.psoKind = kind ? kind : "";
	task.pipelineState = pipelineState;
	if (task.pipelineState)
		task.pipelineState->AddRef();
	task.rootSignature = rootSignature;
	if (task.rootSignature)
		task.rootSignature->AddRef();
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
	LogStats("EnqueuePsoRootSignature", task.sequence, depth);
	DX12LogManager::JsonFunc("DX12ResourceCollectQueue",
		"\"event\":\"EnqueuePsoRootSignatureDetail\",\"seq\":%llu,\"source\":\"%s\",\"pso\":%llu,\"kind\":\"%s\",\"pipelineState\":\"%p\",\"rootSignature\":\"%p\",\"enqueueThreadId\":%lu",
		static_cast<unsigned long long>(task.sequence),
		source ? source : "",
		static_cast<unsigned long long>(psoIndex),
		kind ? kind : "",
		pipelineState,
		rootSignature,
		task.enqueueThreadId);
	wake.notify_one();
	return true;
}

void DX12ResourceCollectQueue::DrainForSnapshot(const char *reason)
{
	DrainPending(reason);
}

size_t DX12ResourceCollectQueue::PendingCount()
{
	std::lock_guard<std::mutex> lock(mutex);
	return tasks.size();
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
			wake.wait(lock, []() {
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
		if (depthAfterPop > 0)
			LogStats("DequeueNonEmpty", task.sequence, depthAfterPop);
		ProcessTask(&task, "Worker");
	}

	DrainPending("WorkerStop");
	DX12LogManager::JsonFunc("DX12ResourceCollectQueue",
		"\"event\":\"WorkerExit\",\"threadId\":%lu,\"pending\":%zu",
		GetCurrentThreadId(), PendingCount());
}

void DX12ResourceCollectQueue::DrainPending(const char *reason)
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
		if (depthAfterPop > 0)
			LogStats(reason ? reason : "Drain", task.sequence, depthAfterPop);
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
	case DX12ResourceCollectTaskKind::ResourceBarrier:
		ProcessResourceBarrierTask(task, source);
		return;
	case DX12ResourceCollectTaskKind::RootSignature:
		ProcessRootSignatureTask(task, source);
		return;
	case DX12ResourceCollectTaskKind::PsoRootSignature:
		ProcessPsoRootSignatureTask(task, source);
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
	RecordResource(
		task->resource,
		task->hasDesc ? &task->desc : nullptr,
		task->hasHeapProperties ? &task->heapProperties : nullptr,
		task->hasPlacedInfo ? &task->placedInfo : nullptr,
		task->initialState);

	const auto end = std::chrono::steady_clock::now();
	const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count();
	completed.fetch_add(1, std::memory_order_relaxed);
	RecordResourceTaskStats(task, source, static_cast<int64_t>(elapsedUs));
	task->resource = nullptr;
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

void DX12ResourceCollectQueue::ProcessRootSignatureTask(DX12ResourceCollectTask *task, const char *source)
{
	if (!task || !task->rootSignature || task->rootSignatureBlob.empty())
		return;

	const auto begin = std::chrono::steady_clock::now();
	DX12LogManager::JsonFunc("DX12ResourceCollectQueue",
		"\"event\":\"ProcessRootSignatureBegin\",\"source\":\"%s\",\"rootSource\":\"%s\",\"seq\":%llu,\"rootSignature\":\"%p\",\"blobSize\":%zu,\"nodeMask\":%u,\"enqueueThreadId\":%lu,\"workerThreadId\":%lu",
		source ? source : "",
		task->source ? task->source : "",
		static_cast<unsigned long long>(task->sequence),
		task->rootSignature,
		task->rootSignatureBlob.size(),
		task->rootSignatureNodeMask,
		task->enqueueThreadId,
		GetCurrentThreadId());

	RootSignatureRecord record;
	record.rootSignature = task->rootSignature;
	record.hash = Fnv1a64(task->rootSignatureBlob.data(), task->rootSignatureBlob.size());
	record.size = task->rootSignatureBlob.size();
	record.nodeMask = task->rootSignatureNodeMask;
	ParseRootSignatureBlob(&record, task->rootSignatureBlob.data(), task->rootSignatureBlob.size());
	const bool parsed = record.parsed;
	const size_t parameterCount = record.parameters.size();

	AcquireSRWLockExclusive(&gDescriptorLock);
	++gRootSignatureRecordsSeen;
	auto found = gRootSignatureByPtr.find(record.rootSignature);
	if (found != gRootSignatureByPtr.end()) {
		gRootSignatures[found->second] = std::move(record);
	} else {
		gRootSignatureByPtr[record.rootSignature] = gRootSignatures.size();
		gRootSignatures.push_back(std::move(record));
	}
	LogResourceTrackerStatsLocked();
	ReleaseSRWLockExclusive(&gDescriptorLock);

	const auto end = std::chrono::steady_clock::now();
	const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count();
	completed.fetch_add(1, std::memory_order_relaxed);
	DX12LogManager::JsonFunc("DX12ResourceCollectQueue",
		"\"event\":\"ProcessRootSignatureComplete\",\"source\":\"%s\",\"rootSource\":\"%s\",\"seq\":%llu,\"rootSignature\":\"%p\",\"parsed\":%u,\"parameterCount\":%zu,\"elapsedUs\":%lld,\"completed\":%llu",
		source ? source : "",
		task->source ? task->source : "",
		static_cast<unsigned long long>(task->sequence),
		task->rootSignature,
		parsed ? 1 : 0,
		parameterCount,
		static_cast<long long>(elapsedUs),
		static_cast<unsigned long long>(completed.load(std::memory_order_relaxed)));
	ReleaseTaskRootSignature(task);
}

void DX12ResourceCollectQueue::ProcessPsoRootSignatureTask(DX12ResourceCollectTask *task, const char *source)
{
	if (!task || !task->psoIndex)
		return;

	const auto begin = std::chrono::steady_clock::now();
	DX12LogManager::JsonFunc("DX12ResourceCollectQueue",
		"\"event\":\"ProcessPsoRootSignatureBegin\",\"source\":\"%s\",\"psoSource\":\"%s\",\"seq\":%llu,\"pso\":%llu,\"kind\":\"%s\",\"pipelineState\":\"%p\",\"rootSignature\":\"%p\",\"enqueueThreadId\":%lu,\"workerThreadId\":%lu",
		source ? source : "",
		task->source ? task->source : "",
		static_cast<unsigned long long>(task->sequence),
		static_cast<unsigned long long>(task->psoIndex),
		task->psoKind.c_str(),
		task->pipelineState,
		task->rootSignature,
		task->enqueueThreadId,
		GetCurrentThreadId());

	DX12RecordPsoRootSignatureImmediate(
		task->psoIndex,
		task->psoKind.c_str(),
		task->pipelineState,
		task->rootSignature);

	const auto end = std::chrono::steady_clock::now();
	const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count();
	completed.fetch_add(1, std::memory_order_relaxed);
	DX12LogManager::JsonFunc("DX12ResourceCollectQueue",
		"\"event\":\"ProcessPsoRootSignatureComplete\",\"source\":\"%s\",\"psoSource\":\"%s\",\"seq\":%llu,\"pso\":%llu,\"elapsedUs\":%lld,\"completed\":%llu",
		source ? source : "",
		task->source ? task->source : "",
		static_cast<unsigned long long>(task->sequence),
		static_cast<unsigned long long>(task->psoIndex),
		static_cast<long long>(elapsedUs),
		static_cast<unsigned long long>(completed.load(std::memory_order_relaxed)));
	ReleaseTaskPsoRootSignature(task);
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

void DX12ResourceCollectQueue::ReleaseTaskRootSignature(DX12ResourceCollectTask *task)
{
	if (!task)
		return;
	if (task->rootSignature) {
		task->rootSignature->Release();
		task->rootSignature = nullptr;
	}
	task->rootSignatureBlob.clear();
}

void DX12ResourceCollectQueue::ReleaseTaskPsoRootSignature(DX12ResourceCollectTask *task)
{
	if (!task)
		return;
	if (task->pipelineState) {
		task->pipelineState->Release();
		task->pipelineState = nullptr;
	}
	if (task->rootSignature) {
		task->rootSignature->Release();
		task->rootSignature = nullptr;
	}
	task->psoKind.clear();
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
	const uint64_t serial = resourceTaskStatsSerial.fetch_add(1, std::memory_order_relaxed) + 1;
	if (sequence != 0 && depth < 1024 && serial % kResourceQueueStatsInterval != 0)
		return;
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

void DX12ResourceCollectQueue::RecordResourceTaskStats(
	DX12ResourceCollectTask *task, const char *source, int64_t elapsedUs)
{
	const uint64_t count = resourceTasksCompleted.fetch_add(1, std::memory_order_relaxed) + 1;
	resourceTasksElapsedUs.fetch_add(elapsedUs > 0 ? static_cast<uint64_t>(elapsedUs) : 0,
		std::memory_order_relaxed);
	uint64_t currentMax = resourceTasksMaxElapsedUs.load(std::memory_order_relaxed);
	while (elapsedUs > 0 && static_cast<uint64_t>(elapsedUs) > currentMax &&
		!resourceTasksMaxElapsedUs.compare_exchange_weak(
			currentMax, static_cast<uint64_t>(elapsedUs), std::memory_order_relaxed)) {
	}

	if (elapsedUs >= kResourceQueueSlowTaskUs) {
		DX12LogManager::JsonFunc("DX12ResourceCollectQueueSlowResource",
			"\"source\":\"%s\",\"seq\":%llu,\"resource\":\"%p\",\"elapsedUs\":%lld,\"completed\":%llu,\"pending\":%zu",
			source ? source : "",
			static_cast<unsigned long long>(task ? task->sequence : 0),
			task ? task->resource : nullptr,
			static_cast<long long>(elapsedUs),
			static_cast<unsigned long long>(count),
			PendingCount());
		return;
	}

	if (count % kResourceQueueStatsInterval != 0)
		return;

	const uint64_t totalUs = resourceTasksElapsedUs.load(std::memory_order_relaxed);
	const uint64_t maxUs = resourceTasksMaxElapsedUs.load(std::memory_order_relaxed);
	DX12LogManager::JsonFunc("DX12ResourceCollectQueueStats",
		"\"resourceCompleted\":%llu,\"resourceElapsedUs\":%llu,\"resourceAvgUs\":%llu,\"resourceMaxUs\":%llu,\"pending\":%zu,\"enqueued\":%llu,\"dequeued\":%llu,\"completed\":%llu,\"maxDepth\":%llu",
		static_cast<unsigned long long>(count),
		static_cast<unsigned long long>(totalUs),
		static_cast<unsigned long long>(count ? totalUs / count : 0),
		static_cast<unsigned long long>(maxUs),
		PendingCount(),
		static_cast<unsigned long long>(enqueued.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(dequeued.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(completed.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(maxDepth.load(std::memory_order_relaxed)));
}
