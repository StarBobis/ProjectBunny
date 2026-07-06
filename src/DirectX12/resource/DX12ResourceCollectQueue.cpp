#include "DX12ResourceCollectQueue.hpp"

#include <chrono>

#include "DX12LogManager.hpp"
#include "DX12ResourceTrackerHookRuntime.hpp"
#include "DX12ResourceTrackerPrivate.hpp"
#include "utils/DX12OuterTimingProbeUtils.hpp"

static constexpr uint64_t kResourceQueueStatsInterval = 4096;
static constexpr int64_t kResourceQueueSlowTaskUs = 2000;
static constexpr int64_t kResourceQueueSlowDrainUs = 1000;

void DX12ResourceCollectQueue::Start()
{
	started.store(true, std::memory_order_release);
}


void DX12ResourceCollectQueue::Stop()
{
	started.store(false, std::memory_order_release);
}
// STOP_REPLACED
void DX12ResourceCollectQueue::DrainForSnapshot(const char *reason)
{
	WaitForSubmittedSequence(LastSubmittedSequence(), reason ? reason : "Snapshot");
}

size_t DX12ResourceCollectQueue::PendingCount()
{
	std::lock_guard<std::mutex> lock(mutex);
	return tasks.size() + static_cast<size_t>(activeTasks);
}

UINT64 DX12ResourceCollectQueue::LastSubmittedSequence()
{
	const uint64_t next = nextSequence.load(std::memory_order_acquire);
	if (next == 0)
		return 0;
	return next - 1;
}

void DX12ResourceCollectQueue::WorkerLoop()
{
	DX12LogManager::JsonFunc("DX12ResourceCollectQueue",
		"event=WorkerEnter,threadId=%lu",
		GetCurrentThreadId());

	static constexpr size_t kBatchSize = 64;
	for (;;) {
		DX12ResourceCollectTask batch[kBatchSize];
		size_t batchCount = 0;
		size_t depthAfterPop = 0;
		{
			std::unique_lock<std::mutex> lock(mutex);
			LARGE_INTEGER waitBegin = DX12OuterTimingProbeUtils::Now();
			wake.wait(lock, []() {
				return stopRequested.load(std::memory_order_acquire) || !tasks.empty();
			});
			DX12OuterTimingProbeUtils::LogWorkerWait(
				"ResourceCollectQueue",
				"WorkerWake",
				"worker_idle",
				0,
				completedSequence.load(std::memory_order_acquire),
				tasks.size() + static_cast<size_t>(activeTasks),
				DX12OuterTimingProbeUtils::ElapsedUs(waitBegin));
			if (tasks.empty()) {
				if (stopRequested.load(std::memory_order_acquire))
					break;
				continue;
			}
			// Batch dequeue: process up to kBatchSize tasks per lock acquisition.
			// Amortizes lock overhead ~64x and reduces cache invalidation storms.
			while (batchCount < kBatchSize && !tasks.empty()) {
				batch[batchCount] = std::move(tasks.front());
				tasks.pop_front();
				batchCount++;
			}
			activeTasks += batchCount;
			depthAfterPop = tasks.size() + static_cast<size_t>(activeTasks);
		}
		UINT64 lastSequence = 0;
		for (size_t i = 0; i < batchCount; ++i) {
			dequeued.fetch_add(1, std::memory_order_relaxed);
			if (depthAfterPop > 0)
				LogStats("DequeueNonEmpty", batch[i].sequence, depthAfterPop);
			ProcessTask(&batch[i], "Worker");
			lastSequence = batch[i].sequence;
		}
		{
			std::lock_guard<std::mutex> lock(mutex);
			if (activeTasks >= static_cast<int>(batchCount))
				activeTasks -= static_cast<int>(batchCount);
			else
				activeTasks = 0;
		}
		completed.fetch_add(batchCount, std::memory_order_relaxed);
		completedSequence.store(lastSequence, std::memory_order_release);
		idle.notify_all();
	}

	WaitForIdle("WorkerStopDrain");
	DrainPending("WorkerStop");
	idle.notify_all();
	DX12LogManager::JsonFunc("DX12ResourceCollectQueue",
		"event=WorkerExit,threadId=%lu,pending=%zu",
		GetCurrentThreadId(), PendingCount());

}

void DX12ResourceCollectQueue::DrainPending(const char *reason)
{
	static constexpr size_t kBatchSize = 64;
	for (;;) {
		DX12ResourceCollectTask batch[kBatchSize];
		size_t batchCount = 0;
		size_t depthAfterPop = 0;
		{
			std::lock_guard<std::mutex> lock(mutex);
			if (tasks.empty())
				break;
			while (batchCount < kBatchSize && !tasks.empty()) {
				batch[batchCount] = tasks.front();
				tasks.pop_front();
				batchCount++;
			}
			activeTasks += batchCount;
			depthAfterPop = tasks.size() + static_cast<size_t>(activeTasks);
		}
		UINT64 lastSequence = 0;
		for (size_t i = 0; i < batchCount; ++i) {
			dequeued.fetch_add(1, std::memory_order_relaxed);
			if (depthAfterPop > 0)
				LogStats(reason ? reason : "Drain", batch[i].sequence, depthAfterPop);
			ProcessTask(&batch[i], reason ? reason : "Drain");
			lastSequence = batch[i].sequence;
		}
		completed.fetch_add(batchCount, std::memory_order_relaxed);
		completedSequence.store(lastSequence, std::memory_order_release);
		idle.notify_all();
	}

}

void DX12ResourceCollectQueue::WaitForSubmittedSequence(UINT64 targetSequence, const char *reason)
{
	if (targetSequence == 0)
		return;

	const auto begin = std::chrono::steady_clock::now();
	size_t pendingSnapshot = 0;
	{
		std::unique_lock<std::mutex> lock(mutex);
		pendingSnapshot = tasks.size() + static_cast<size_t>(activeTasks);
		if (completedSequence.load(std::memory_order_acquire) < targetSequence) {
			idle.wait(lock, [targetSequence]() {
				return completedSequence.load(std::memory_order_acquire) >= targetSequence ||
					(tasks.empty() && activeTasks == 0);
			});
		}
	}
	const auto end = std::chrono::steady_clock::now();
	const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count();
	DX12OuterTimingProbeUtils::LogWorkerWait(
		"ResourceCollectQueue",
		reason ? reason : "",
		"sequence",
		targetSequence,
		completedSequence.load(std::memory_order_acquire),
		pendingSnapshot,
		static_cast<LONG64>(elapsedUs));
	if (elapsedUs >= kResourceQueueSlowDrainUs) {
		DX12LogManager::JsonFunc("DX12ResourceCollectQueueDrain",
			"reason=%s,mode=sequence,targetSequence=%llu,completedSequence=%llu,pending=%zu,elapsedUs=%lld",
			reason ? reason : "",
			static_cast<unsigned long long>(targetSequence),
			static_cast<unsigned long long>(completedSequence.load(std::memory_order_acquire)),
			pendingSnapshot,
			static_cast<long long>(elapsedUs));
	}
}

void DX12ResourceCollectQueue::WaitForIdle(const char *reason)
{
	const auto begin = std::chrono::steady_clock::now();
	size_t pendingSnapshot = 0;
	{
		std::unique_lock<std::mutex> lock(mutex);
		pendingSnapshot = tasks.size() + static_cast<size_t>(activeTasks);
		if (pendingSnapshot != 0) {
			idle.wait(lock, []() {
				return tasks.empty() && activeTasks == 0;
			});
		}
	}
	const auto end = std::chrono::steady_clock::now();
	const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count();
	DX12OuterTimingProbeUtils::LogWorkerWait(
		"ResourceCollectQueue",
		reason ? reason : "",
		"idle",
		0,
		completedSequence.load(std::memory_order_acquire),
		pendingSnapshot,
		static_cast<LONG64>(elapsedUs));
	if (elapsedUs >= kResourceQueueSlowDrainUs) {
		DX12LogManager::JsonFunc("DX12ResourceCollectQueueDrain",
			"reason=%s,mode=idle,pending=%zu,elapsedUs=%lld",
			reason ? reason : "",
			pendingSnapshot,
			static_cast<long long>(elapsedUs));
	}
}

void DX12ResourceCollectQueue::ProcessTask(DX12ResourceCollectTask *task, const char *source)
{
	if (!task)
		return;

	LARGE_INTEGER begin = DX12OuterTimingProbeUtils::Now();
	const UINT taskKind = static_cast<UINT>(task->kind);
	const UINT64 sequence = task->sequence;
	const DWORD enqueueThreadId = task->enqueueThreadId;
	size_t itemCount = 1;
	if (task->kind == DX12ResourceCollectTaskKind::ResourceBarrier)
		itemCount = task->barriers.size();
	else if (task->kind == DX12ResourceCollectTaskKind::RootSignature)
		itemCount = task->rootSignatureBlob.size();

	switch (task->kind) {
	case DX12ResourceCollectTaskKind::Resource:
		ProcessResourceTask(task, source);
		break;
	case DX12ResourceCollectTaskKind::ResourceBarrier:
		ProcessResourceBarrierTask(task, source);
		break;
	case DX12ResourceCollectTaskKind::RootSignature:
		ProcessRootSignatureTask(task, source);
		break;
	case DX12ResourceCollectTaskKind::PsoRootSignature:
		ProcessPsoRootSignatureTask(task, source);
		break;
	default:
		break;
	}
	DX12OuterTimingProbeUtils::LogWorkerTask(
		"ResourceCollectQueue",
		"process_task",
		taskKind,
		sequence,
		itemCount,
		PendingCount(),
		enqueueThreadId,
		DX12OuterTimingProbeUtils::ElapsedUs(begin));
}

void DX12ResourceCollectQueue::ProcessResourceTask(DX12ResourceCollectTask *task, const char *source)
{
	if (!task || !task->resource)
		return;

	const auto begin = std::chrono::steady_clock::now();
	LARGE_INTEGER recordBegin = DX12OuterTimingProbeUtils::Now();
	RecordResource(
		task->resource,
		task->hasDesc ? &task->desc : nullptr,
		task->hasHeapProperties ? &task->heapProperties : nullptr,
		task->hasPlacedInfo ? &task->placedInfo : nullptr,
		task->initialState);
	DX12OuterTimingProbeUtils::LogCpuStage(
		"ResourceCollectQueue",
		"record_resource",
		static_cast<UINT>(task->kind),
		1,
		DX12OuterTimingProbeUtils::ElapsedUs(recordBegin));

	const auto end = std::chrono::steady_clock::now();
	const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count();
	RecordResourceTaskStats(task, source, static_cast<int64_t>(elapsedUs));
	if (task->retainedResource) {
		LARGE_INTEGER releaseBegin = DX12OuterTimingProbeUtils::Now();
		DX12ResourceSnapshotUtils::Release(task->resource);
		DX12OuterTimingProbeUtils::LogCpuStage(
			"ResourceCollectQueue",
			"release_resource_ref",
			static_cast<UINT>(task->kind),
			1,
			DX12OuterTimingProbeUtils::ElapsedUs(releaseBegin));
	}
	task->resource = nullptr;
	task->retainedResource = false;
}

void DX12ResourceCollectQueue::ProcessResourceBarrierTask(DX12ResourceCollectTask *task, const char *source)
{
	if (!task || task->barriers.empty())
		return;

	const auto begin = std::chrono::steady_clock::now();
	DX12LogManager::JsonFunc("DX12ResourceCollectQueue",
		"event=ProcessResourceBarrierBegin,source=%s,barrierSource=%s,seq=%llu,count=%zu,enqueueThreadId=%lu,workerThreadId=%lu",
		source ? source : "",
		task->source ? task->source : "",
		static_cast<unsigned long long>(task->sequence),
		task->barriers.size(),
		task->enqueueThreadId,
		GetCurrentThreadId());

	LARGE_INTEGER recordBegin = DX12OuterTimingProbeUtils::Now();
	DX12RecordResourceBarrier(static_cast<UINT>(task->barriers.size()), task->barriers.data());
	DX12OuterTimingProbeUtils::LogCpuStage(
		"ResourceCollectQueue",
		"record_resource_barriers",
		static_cast<UINT>(task->kind),
		task->barriers.size(),
		DX12OuterTimingProbeUtils::ElapsedUs(recordBegin));

	const auto end = std::chrono::steady_clock::now();
	const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count();
	DX12LogManager::JsonFunc("DX12ResourceCollectQueue",
		"event=ProcessResourceBarrierComplete,source=%s,barrierSource=%s,seq=%llu,count=%zu,elapsedUs=%lld,completed=%llu",
		source ? source : "",
		task->source ? task->source : "",
		static_cast<unsigned long long>(task->sequence),
		task->barriers.size(),
		static_cast<long long>(elapsedUs),
		static_cast<unsigned long long>(completed.load(std::memory_order_relaxed)));

	LARGE_INTEGER releaseBegin = DX12OuterTimingProbeUtils::Now();
	for (D3D12_RESOURCE_BARRIER &barrier : task->barriers) {
		ReleaseBarrierResources(&barrier);
	}
	DX12OuterTimingProbeUtils::LogCpuStage(
		"ResourceCollectQueue",
		"release_barrier_refs",
		static_cast<UINT>(task->kind),
		task->barriers.size(),
		DX12OuterTimingProbeUtils::ElapsedUs(releaseBegin));
	task->barriers.clear();
}

void DX12ResourceCollectQueue::ProcessRootSignatureTask(DX12ResourceCollectTask *task, const char *source)
{
	if (!task || !task->rootSignature || task->rootSignatureBlob.empty())
		return;

	const auto begin = std::chrono::steady_clock::now();
	DX12LogManager::JsonFunc("DX12ResourceCollectQueue",
		"event=ProcessRootSignatureBegin,source=%s,rootSource=%s,seq=%llu,rootSignature=%p,blobSize=%zu,nodeMask=%u,enqueueThreadId=%lu,workerThreadId=%lu",
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
	LARGE_INTEGER parseBegin = DX12OuterTimingProbeUtils::Now();
	record.hash = Fnv1a64(task->rootSignatureBlob.data(), task->rootSignatureBlob.size());
	record.size = task->rootSignatureBlob.size();
	record.nodeMask = task->rootSignatureNodeMask;
	ParseRootSignatureBlob(&record, task->rootSignatureBlob.data(), task->rootSignatureBlob.size());
	const bool parsed = record.parsed;
	const size_t parameterCount = record.parameters.size();
	DX12OuterTimingProbeUtils::LogCpuStage(
		"ResourceCollectQueue",
		"parse_root_signature",
		static_cast<UINT>(task->kind),
		task->rootSignatureBlob.size(),
		DX12OuterTimingProbeUtils::ElapsedUs(parseBegin));

	LARGE_INTEGER storeBegin = DX12OuterTimingProbeUtils::Now();
	AcquireSRWLockExclusive(&gDescriptorLock);
	++gRootSignatureRecordsSeen;
	std::unordered_map<ID3D12RootSignature*, size_t>::iterator found =
		gRootSignatureByPtr.find(record.rootSignature);
	if (found != gRootSignatureByPtr.end()) {
		gRootSignatures[found->second] = std::move(record);
	} else {
		gRootSignatureByPtr[record.rootSignature] = gRootSignatures.size();
		gRootSignatures.push_back(std::move(record));
	}
	LogResourceTrackerStatsLocked();
	ReleaseSRWLockExclusive(&gDescriptorLock);
	DX12OuterTimingProbeUtils::LogCpuStage(
		"ResourceCollectQueue",
		"store_root_signature",
		static_cast<UINT>(task->kind),
		parameterCount,
		DX12OuterTimingProbeUtils::ElapsedUs(storeBegin));

	const auto end = std::chrono::steady_clock::now();
	const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count();
	DX12LogManager::JsonFunc("DX12ResourceCollectQueue",
		"event=ProcessRootSignatureComplete,source=%s,rootSource=%s,seq=%llu,rootSignature=%p,parsed=%u,parameterCount=%zu,elapsedUs=%lld,completed=%llu",
		source ? source : "",
		task->source ? task->source : "",
		static_cast<unsigned long long>(task->sequence),
		task->rootSignature,
		parsed ? 1 : 0,
		parameterCount,
		static_cast<long long>(elapsedUs),
		static_cast<unsigned long long>(completed.load(std::memory_order_relaxed)));
	LARGE_INTEGER releaseBegin = DX12OuterTimingProbeUtils::Now();
	ReleaseTaskRootSignature(task);
	DX12OuterTimingProbeUtils::LogCpuStage(
		"ResourceCollectQueue",
		"release_root_signature",
		static_cast<UINT>(task->kind),
		1,
		DX12OuterTimingProbeUtils::ElapsedUs(releaseBegin));
}

void DX12ResourceCollectQueue::ProcessPsoRootSignatureTask(DX12ResourceCollectTask *task, const char *source)
{
	if (!task || !task->psoIndex)
		return;

	const auto begin = std::chrono::steady_clock::now();
	DX12LogManager::JsonFunc("DX12ResourceCollectQueue",
		"event=ProcessPsoRootSignatureBegin,source=%s,psoSource=%s,seq=%llu,pso=%llu,kind=%s,pipelineState=%p,rootSignature=%p,enqueueThreadId=%lu,workerThreadId=%lu",
		source ? source : "",
		task->source ? task->source : "",
		static_cast<unsigned long long>(task->sequence),
		static_cast<unsigned long long>(task->psoIndex),
		task->psoKind.c_str(),
		task->pipelineState,
		task->rootSignature,
		task->enqueueThreadId,
		GetCurrentThreadId());

	LARGE_INTEGER recordBegin = DX12OuterTimingProbeUtils::Now();
	DX12RecordPsoRootSignatureImmediate(
		task->psoIndex,
		task->psoKind.c_str(),
		task->pipelineState,
		task->rootSignature);
	DX12OuterTimingProbeUtils::LogCpuStage(
		"ResourceCollectQueue",
		"record_pso_root_signature",
		static_cast<UINT>(task->kind),
		1,
		DX12OuterTimingProbeUtils::ElapsedUs(recordBegin));

	const auto end = std::chrono::steady_clock::now();
	const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count();
	DX12LogManager::JsonFunc("DX12ResourceCollectQueue",
		"event=ProcessPsoRootSignatureComplete,source=%s,psoSource=%s,seq=%llu,pso=%llu,elapsedUs=%lld,completed=%llu",
		source ? source : "",
		task->source ? task->source : "",
		static_cast<unsigned long long>(task->sequence),
		static_cast<unsigned long long>(task->psoIndex),
		static_cast<long long>(elapsedUs),
		static_cast<unsigned long long>(completed.load(std::memory_order_relaxed)));
	LARGE_INTEGER releaseBegin = DX12OuterTimingProbeUtils::Now();
	ReleaseTaskPsoRootSignature(task);
	DX12OuterTimingProbeUtils::LogCpuStage(
		"ResourceCollectQueue",
		"release_pso_root_signature",
		static_cast<UINT>(task->kind),
		1,
		DX12OuterTimingProbeUtils::ElapsedUs(releaseBegin));
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
		"event=%s,seq=%llu,pending=%zu,enqueued=%llu,dequeued=%llu,completed=%llu,maxDepth=%llu",
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
			"source=%s,seq=%llu,resource=%p,elapsedUs=%lld,completed=%llu,pending=%zu",
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
		"resourceCompleted=%llu,resourceElapsedUs=%llu,resourceAvgUs=%llu,resourceMaxUs=%llu,pending=%zu,enqueued=%llu,dequeued=%llu,completed=%llu,maxDepth=%llu",
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
