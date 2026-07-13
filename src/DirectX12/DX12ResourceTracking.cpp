// Consolidated implementation module: DX12ResourceTracking.cpp
// Original source names are retained in #line diagnostics.

// ---- DX12ResourceCollectQueue.cpp ----
#line 1 "DX12ResourceCollectQueue.cpp"
#include "DX12FeatureInterfaces.hpp"

#include <chrono>

#include "DX12Foundation.hpp"
#include "DX12Integration.hpp"
#include "DX12Infrastructure.hpp"

static constexpr uint64_t kResourceQueueStatsInterval = 4096;
static constexpr int64_t kResourceQueueSlowTaskUs = 2000;
static constexpr int64_t kResourceQueueSlowDrainUs = 1000;

void DX12ResourceCollectQueue::Start()
{
	started.store(false, std::memory_order_release);
	DX12LogManager::JsonFunc("DX12ResourceCollectQueue",
		"event=StartBypassed,mode=synchronous");
}


void DX12ResourceCollectQueue::Stop()
{
	started.store(false, std::memory_order_release);
}

bool DX12ResourceCollectQueue::Enqueue(
	ID3D12Resource *resource,
	const D3D12_RESOURCE_DESC *desc,
	const D3D12_HEAP_PROPERTIES *heapProperties,
	const PlacedResourceInfo *placedInfo,
	D3D12_RESOURCE_STATES initialState)
{
	(void)resource;
	(void)desc;
	(void)heapProperties;
	(void)placedInfo;
	(void)initialState;
	// Queue worker is bypassed (Start sets started=false). All enqueue
	// calls are synchronous fallbacks that only log and discard the data.
	// Skip the logging overhead entirely when no one is interested.
	return false;
}

bool DX12ResourceCollectQueue::EnqueueResourceBarriers(
	UINT numBarriers,
	const D3D12_RESOURCE_BARRIER *barriers,
	const char *source)
{
	(void)barriers;
	dropped.fetch_add(1, std::memory_order_relaxed);
	DX12LogManager::JsonFunc("DX12ResourceCollectQueue",
		"event=SyncFallback,kind=ResourceBarrier,count=%u,source=%s,reason=no_worker",
		numBarriers, source ? source : "");
	return false;
}

bool DX12ResourceCollectQueue::EnqueueRootSignature(
	ID3D12RootSignature *rootSignature,
	UINT nodeMask,
	const void *blob,
	SIZE_T blobLength,
	const char *source)
{
	(void)nodeMask;
	(void)blob;
	dropped.fetch_add(1, std::memory_order_relaxed);
	DX12LogManager::JsonFunc("DX12ResourceCollectQueue",
		"event=SyncFallback,kind=RootSignature,rootSignature=%p,blobSize=%zu,source=%s,reason=no_worker",
		rootSignature, blobLength, source ? source : "");
	return false;
}

bool DX12ResourceCollectQueue::EnqueuePsoRootSignature(
	UINT64 psoIndex,
	const char *kind,
	ID3D12PipelineState *pipelineState,
	ID3D12RootSignature *rootSignature,
	const char *source)
{
	dropped.fetch_add(1, std::memory_order_relaxed);
	DX12LogManager::JsonFunc("DX12ResourceCollectQueue",
		"event=SyncFallback,kind=PsoRootSignature,pso=%llu,psoKind=%s,pipelineState=%p,rootSignature=%p,source=%s,reason=no_worker",
		static_cast<unsigned long long>(psoIndex),
		kind ? kind : "",
		pipelineState,
		rootSignature,
		source ? source : "");
	return false;
}
// STOP_REPLACED
void DX12ResourceCollectQueue::DrainForSnapshot(const char *reason)
{
	DX12LogManager::JsonFunc("DX12ResourceCollectQueue",
		"event=DrainBypassed,reason=%s,mode=synchronous",
		reason ? reason : "Snapshot");
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

// ---- DX12ResourceTracker.cpp ----
#line 1 "DX12ResourceTracker.cpp"
#include "DX12Infrastructure.hpp"

#include "DX12FeatureInterfaces.hpp"

#include "DX12Foundation.hpp"
#include "DX12RuntimeInterfaces.hpp"

uint32_t HashBytes(uint32_t seed, const void *data, size_t size)
{
	if (!data || size == 0)
		return seed;
	__try {
		return crc32c_append(seed, static_cast<const uint8_t*>(data), size);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return 0;
	}
}

SRWLOCK gResourceLock = SRWLOCK_INIT;
SRWLOCK gDescriptorLock = SRWLOCK_INIT;
std::vector<RootSignatureRecord> gRootSignatures;
std::deque<DescriptorHeapRecord> gDescriptorHeaps;
std::unordered_map<ID3D12RootSignature*, size_t> gRootSignatureByPtr;
std::unordered_map<ID3D12DescriptorHeap*, size_t> gDescriptorHeapByPtr;
std::map<SIZE_T, size_t> gDescriptorHeapByCpuStart;
std::unordered_map<SIZE_T, DescriptorRecord> gDescriptorByCpuHandle;
std::vector<PsoRootRecord> gPsoRoots;
std::unordered_map<UINT64, size_t> gPsoRootByIndex;
std::vector<ResourceRecord> gResources;
std::vector<size_t> gFreeResourceSlots;
size_t gLiveResourceCount = 0;
std::unordered_map<ID3D12Resource*, size_t> gResourceByPtr;
// Reverse ownership for gResourceByPtr. Resource invalidation used to scan
// every live pointer (roughly 20k in the target game) to find the one or two
// aliases for a released resource. Keep those aliases at their owning index
// so release is O(alias count), not O(all live resources).
static std::vector<std::vector<ID3D12Resource*>> gResourceAliasesByIndex;
std::map<UINT64, size_t> gBufferResourceByGpuVa;
std::map<UINT64, size_t> gBufferResourceByGpuVaComplete;
std::map<UINT64, BufferResolveCacheEntry> gBufferResolveCache;
std::atomic<UINT64> gBufferResolveGeneration{1};
std::atomic<UINT64> gResourceIdentityGeneration{1};
std::atomic<UINT64> gDescriptorQuerySerial{1};
static SRWLOCK gCpuMappableResourceLock = SRWLOCK_INIT;
static std::unordered_set<ID3D12Resource*> gCpuMappableResources;
static std::atomic<UINT64> gCpuMappableResourceGeneration{1};
bool gCleanupRegistered = false;
UINT64 gResourcesRecordedFromCreate = 0;
UINT64 gDescriptorRecordsSeen = 0;
UINT64 gDescriptorCopyRecordsSeen = 0;
UINT64 gDescriptorHeapRecordsSeen = 0;
UINT64 gRootSignatureRecordsSeen = 0;
UINT64 gPsoRootRecordsSeen = 0;
UINT64 gDescriptorEpoch = 0;
LONG gLastResourceStatsPresent = -1;
static UINT64 gDescriptorCopySpanFastPathCalls = 0;
static UINT64 gDescriptorCopySpanFastPathSlots = 0;
static UINT64 gDescriptorCopySpanFallbackCalls = 0;
static constexpr LONG kResourceStatsLogInterval = 300;
UINT64 gBufferResolveCacheInvalidations = 0;
UINT64 gBufferResolveCacheInvalidationEntries = 0;
UINT64 gBufferResolveTlsHits = 0;
UINT64 gBufferResolveGlobalHits = 0;
UINT64 gBufferResolveBackwardScanHits = 0;
UINT64 gBufferResolveMisses = 0;
UINT64 gBufferResolveFallbackScanHits = 0;

static void RegisterCpuMappableResource(ID3D12Resource *resource)
{
	if (!resource)
		return;
	AcquireSRWLockExclusive(&gCpuMappableResourceLock);
	if (gCpuMappableResources.insert(resource).second)
		gCpuMappableResourceGeneration.fetch_add(1, std::memory_order_release);
	ReleaseSRWLockExclusive(&gCpuMappableResourceLock);
}

static void UnregisterCpuMappableResource(ID3D12Resource *resource, ID3D12Resource *canonical)
{
	AcquireSRWLockExclusive(&gCpuMappableResourceLock);
	bool removed = resource && gCpuMappableResources.erase(resource) != 0;
	if (canonical && canonical != resource)
		removed = gCpuMappableResources.erase(canonical) != 0 || removed;
	if (removed)
		gCpuMappableResourceGeneration.fetch_add(1, std::memory_order_release);
	ReleaseSRWLockExclusive(&gCpuMappableResourceLock);
}
UINT64 gBufferResolveGenerationResets = 0;
UINT64 gBufferResolveRangeInvalidations = 0;
UINT64 gBufferResolveRangeInvalidationEntries = 0;
UINT64 gBufferResolveIndexNoopUpdates = 0;
UINT64 gBufferResolveStateOnlyUpdates = 0;
UINT64 gBufferResolveInvalidationEpoch = 0;
std::atomic<UINT64> gBufferResolveInvalidationEpochAtomic{0};
UINT64 gBufferResolveInvalidationHistoryLost = 0;
DX12BufferResolveInvalidatedRange gBufferResolveInvalidatedRanges[kDX12BufferResolveInvalidationHistoryCapacity] = {};
UINT64 gResourceExclusiveLockSlowCount = 0;
UINT64 gResourceExclusiveLockSlowMaxUs = 0;
UINT64 gDescriptorExclusiveLockSlowCount = 0;
UINT64 gDescriptorExclusiveLockSlowMaxUs = 0;

// Canonical identity cache: avoids per-call COM QueryInterface overhead.
static std::unordered_map<ID3D12Resource*, ID3D12Resource*> gCanonicalCache;
static SRWLOCK gCanonicalCacheLock = SRWLOCK_INIT;

class DX12BufferAddressRangeLogUtils
{
public:
	inline static volatile LONG64 AddedCalls = 0;
	inline static volatile LONG64 AddedBytes = 0;
	inline static volatile LONG LastPresentLogged = -1;
	static constexpr LONG PresentInterval = 300;

	static void Record(ID3D12Resource *resource, const ResourceRecord &record)
	{
		InterlockedIncrement64(&AddedCalls);
		InterlockedAdd64(&AddedBytes, static_cast<LONG64>(record.size));
		const LONG present = DX12RuntimeState::GetPresentCount();
		const LONG last = InterlockedCompareExchange(&LastPresentLogged, 0, 0);
		const bool shouldLog = last < 0 || present >= last + PresentInterval;
		if (!shouldLog)
			return;
		if (InterlockedCompareExchange(&LastPresentLogged, present, last) != last)
			return;
		DX12LogManager::JsonFunc("DX12BufferAddressRangeAdded",
			"mode=aggregate,present=%ld,threadId=%lu,resource=%p,gpuVa=0x%llX,size=%llu,realEnd=0x%llX,oobEnd=0x%llX,calls=%lld,bytes=%lld,hasPlacedHeap=%u",
			present,
			GetCurrentThreadId(),
			resource,
			static_cast<unsigned long long>(record.gpuVirtualAddress),
			static_cast<unsigned long long>(record.size),
			static_cast<unsigned long long>(record.gpuVirtualAddressRealEnd),
			static_cast<unsigned long long>(record.gpuVirtualAddressOobEnd),
			static_cast<long long>(InterlockedCompareExchange64(&AddedCalls, 0, 0)),
			static_cast<long long>(InterlockedCompareExchange64(&AddedBytes, 0, 0)),
			record.hasPlacedHeapRange ? 1 : 0);
	}
};

void LogResourceTrackerStatsLocked();

static bool DescriptorHeapSlotOccupied(const DescriptorHeapRecord &heap, UINT slot)
{
	return slot < heap.occupied.size() && heap.occupied[slot] != 0;
}

static bool DescriptorHeapIsShaderVisible(const DescriptorHeapRecord &heap)
{
	return (heap.desc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) != 0;
}

static bool ShouldStoreCreatedDescriptorInHeapSlot(const DescriptorHeapRecord &heap)
{
	return DX12ResourceTrackingPolicyUtils::NeedsDescriptorTracking() &&
		(DescriptorHeapIsShaderVisible(heap) ||
		 DX12ResourceTrackingPolicyUtils::NeedsFullDescriptorHistory());
}

static DescriptorRecord *DescriptorHeapSlot(DescriptorHeapRecord *heap, UINT slot)
{
	if (!heap || !DescriptorHeapSlotOccupied(*heap, slot) || slot >= heap->slots.size())
		return nullptr;
	return &heap->slots[slot];
}

static const DescriptorRecord *DescriptorHeapSlot(const DescriptorHeapRecord *heap, UINT slot)
{
	if (!heap || !DescriptorHeapSlotOccupied(*heap, slot) || slot >= heap->slots.size())
		return nullptr;
	return &heap->slots[slot];
}

static void ClearDescriptorHeapSlotLocked(DescriptorHeapRecord *heap, UINT slot)
{
	if (!heap || !DescriptorHeapSlotOccupied(*heap, slot) || slot >= heap->slots.size())
		return;
	DescriptorRecord &record = heap->slots[slot];
	ReleaseDescriptorRecordResources(&record);
	record = DescriptorRecord();
	heap->occupied[slot] = 0;
	DX12DescriptorSlotArrayUtils::DecrementLiveSlots(heap);
}

static void ClearDescriptorHeapLocked(DescriptorHeapRecord *heap)
{
	if (!heap)
		return;
	for (UINT slot = 0; slot < heap->occupied.size(); ++slot)
		ClearDescriptorHeapSlotLocked(heap, slot);
	for (std::vector<DescriptorRecord> &history : heap->slotHistory) {
		for (DescriptorRecord &record : history)
			ReleaseDescriptorRecordResources(&record);
	}
	heap->slots.clear();
	heap->occupied.clear();
	DX12DescriptorSlotArrayUtils::ClearSparseRecords(heap);
	heap->slotHistory.clear();
	heap->slotHistoryEpochs.clear();
	DX12DescriptorSlotArrayUtils::SetLiveSlots(heap, 0);
}

void InitializeDescriptorHeapRecord(DescriptorHeapRecord *record)
{
	if (!record || record->desc.NumDescriptors == 0)
		return;
	record->currentDescriptors.Initialize(record->desc.NumDescriptors);
	record->stateLocks = std::make_unique<DescriptorHeapRecord::StateLocks>(
		record->desc.NumDescriptors);
	DX12DescriptorSlotArrayUtils::SetLiveSlots(record, 0);
	DX12DescriptorHeapStateUtils::ResetVersion(record);
}

bool EnsureDescriptorHeapStorageLocked(DescriptorHeapRecord *record)
{
	if (!record || record->desc.NumDescriptors == 0)
		return false;
	DescriptorHeapRecord::StateLocks *stateLocks = record->stateLocks.get();
	if (stateLocks)
		stateLocks->LockAllExclusive();
	if (stateLocks && !stateLocks->IsActive()) {
		stateLocks->UnlockAllExclusive();
		return false;
	}
	if (!record->slots.empty()) {
		const bool ready = record->slots.size() == record->desc.NumDescriptors;
		if (stateLocks)
			stateLocks->UnlockAllExclusive();
		return ready;
	}
	// Allocate descriptor slots only after a real tracking need appears. Games
	// can create million-entry heaps during normal Mod mode; keeping only heap
	// boundaries until Mod/Dump needs the slots avoids turning capacity into CPU
	// work and memory pressure.
	record->slots.resize(record->desc.NumDescriptors);
	record->occupied.assign(record->desc.NumDescriptors, 0);
	const size_t sparseCount = record->currentDescriptors.Size();
	record->currentDescriptors.ForEach(
		[record](uint32_t slot, const DescriptorSlotBinding &binding) {
			if (slot >= record->slots.size() || !binding.value)
				return;
			DX12DescriptorSlotArrayUtils::CopyRecordValueForSlot(
				&record->slots[slot], binding.value->value,
				record->cpuStart + static_cast<SIZE_T>(slot) * record->increment);
			record->occupied[slot] = 1;
		});
	DX12DescriptorSlotArrayUtils::ClearSparseRecords(record);
	DX12DescriptorSlotArrayUtils::SetLiveSlots(
		record, static_cast<LONG64>(sparseCount));
	if (stateLocks)
		stateLocks->UnlockAllExclusive();
	return true;
}

static bool EnsureDescriptorHeapHistoryLocked(DescriptorHeapRecord *record)
{
	if (!record || record->desc.NumDescriptors == 0)
		return false;
	if (record->slotHistory.empty())
		record->slotHistory.resize(record->desc.NumDescriptors);
	if (record->slotHistoryEpochs.empty())
		record->slotHistoryEpochs.resize(record->desc.NumDescriptors);
	return record->slotHistory.size() == record->desc.NumDescriptors &&
		record->slotHistoryEpochs.size() == record->desc.NumDescriptors;
}

static void AppendDescriptorHistoryLocked(DescriptorHeapRecord *heap, UINT slot, const DescriptorRecord &record)
{
	if (!DX12ResourceTrackingPolicyUtils::NeedsFullDescriptorHistory())
		return;
	if (!heap || !EnsureDescriptorHeapHistoryLocked(heap) ||
			slot >= heap->slotHistory.size() || slot >= heap->slotHistoryEpochs.size())
		return;
	// Hunting needs RenderDoc-like event granularity: later descriptor heap
	// reuse must not rewrite what an earlier dispatch saw.
	++gDescriptorEpoch;
	heap->slotHistory[slot].push_back(CloneDescriptorRecord(record));
	heap->slotHistoryEpochs[slot].push_back(gDescriptorEpoch);
}

static void StoreDescriptorAtHeapSlotLocked(
	DescriptorHeapRecord *heap, UINT slot, DescriptorRecord &&record)
{
	if (!heap || !EnsureDescriptorHeapStorageLocked(heap) || slot >= heap->slots.size()) {
		ReleaseDescriptorRecordResources(&record);
		return;
	}
	const bool wasOccupied = DescriptorHeapSlotOccupied(*heap, slot);
	AppendDescriptorHistoryLocked(heap, slot, record);
	DescriptorRecord slotRecord;
	DX12DescriptorSlotArrayUtils::CopyRecordValueForSlot(
		&slotRecord, record, record.cpuHandle);
	heap->slots[slot] = std::move(slotRecord);
	ReleaseDescriptorRecordResources(&record);
	heap->occupied[slot] = 1;
	if (!wasOccupied)
		DX12DescriptorSlotArrayUtils::IncrementLiveSlots(heap);
	DX12DescriptorHeapStateUtils::BumpVersion(heap, slot);
}

static size_t LiveDescriptorHeapSlotsLocked()
{
	size_t live = 0;
	for (const DescriptorHeapRecord &heap : gDescriptorHeaps)
		live += static_cast<size_t>(
			DX12DescriptorSlotArrayUtils::ReadLiveSlots(&heap));
	return live;
}

static size_t LiveShaderVisibleDescriptorHeapSlotsLocked()
{
	size_t live = 0;
	for (const DescriptorHeapRecord &heap : gDescriptorHeaps) {
		if (DescriptorHeapIsShaderVisible(heap))
			live += static_cast<size_t>(
				DX12DescriptorSlotArrayUtils::ReadLiveSlots(&heap));
	}
	return live;
}

static size_t DescriptorHeapSlotCapacityLocked()
{
	size_t capacity = 0;
	for (const DescriptorHeapRecord &heap : gDescriptorHeaps)
		capacity += heap.slots.size();
	return capacity;
}

static size_t DescriptorHeapHistoryEntriesLocked()
{
	size_t entries = 0;
	for (const DescriptorHeapRecord &heap : gDescriptorHeaps) {
		for (const std::vector<DescriptorRecord> &history : heap.slotHistory)
			entries += history.size();
	}
	return entries;
}

static void ClearCpuDescriptorLedgerLocked()
{
	for (std::unordered_map<SIZE_T, DescriptorRecord>::value_type &entry : gDescriptorByCpuHandle)
		ReleaseDescriptorRecordResources(&entry.second);
	gDescriptorByCpuHandle.clear();
	// gRuntimeDescriptorByCpuHandle removed - heap slots are the single source of truth
}

static DescriptorRecord CloneRuntimeDescriptorRecord(const DescriptorRecord &record)
{
	DescriptorRecord clone = record;
	clone.ownsResource = false;
	clone.ownsCounterResource = false;
	if (clone.resource) {
		if (DX12ResourceSnapshotUtils::TryAddRef(clone.resource))
			clone.ownsResource = true;
		else
			clone.resource = nullptr;
	}
	if (clone.counterResource) {
		if (DX12ResourceSnapshotUtils::TryAddRef(clone.counterResource))
			clone.ownsCounterResource = true;
		else
			clone.counterResource = nullptr;
	}
	return clone;
}

static void StoreRuntimeDescriptorRecordLocked(const DescriptorRecord &record)
{
	// Forward to ShadowUtils for heap-slot storage
	DescriptorRecord temp = CloneDescriptorRecord(record);
	DX12DescriptorHeapShadowUtils::WriteDescriptorToSlot(record.cpuHandle, std::move(temp));
}

void StoreCpuDescriptorRecordLocked(DescriptorRecord &&record)
{
	if (!record.cpuHandle)
		return;
	std::unordered_map<SIZE_T, DescriptorRecord>::iterator found =
		gDescriptorByCpuHandle.find(record.cpuHandle);
	if (found != gDescriptorByCpuHandle.end())
		ReleaseDescriptorRecordResources(&found->second);
	gDescriptorByCpuHandle[record.cpuHandle] = std::move(record);
}

void InvalidateDescriptorHeapLocked(ID3D12DescriptorHeap *heap)
{
	if (!heap)
		return;
	auto found = gDescriptorHeapByPtr.find(heap);
	if (found == gDescriptorHeapByPtr.end() || found->second >= gDescriptorHeaps.size())
		return;
	DescriptorHeapRecord &record = gDescriptorHeaps[found->second];
	const SIZE_T cpuStart = record.cpuStart;
	gDescriptorHeapByCpuStart.erase(cpuStart);
	gDescriptorHeapByPtr.erase(found);
	DescriptorHeapRecord::StateLocks *stateLocks = record.stateLocks.get();
	if (stateLocks)
		stateLocks->LockAllExclusive();
	if (stateLocks)
		stateLocks->Deactivate();
	// gRuntimeDescriptorByCpuHandle removed - heap slots self-clean via ClearDescriptorHeapLocked
	ClearDescriptorHeapLocked(&record);
	record.heap = nullptr;
	record.desc = D3D12_DESCRIPTOR_HEAP_DESC();
	record.cpuStart = 0;
	record.gpuStart = 0;
	record.increment = 0;
	DX12DescriptorHeapStateUtils::ResetVersion(&record);
	if (stateLocks)
		stateLocks->UnlockAllExclusive();
	BumpDescriptorQuerySerial();
}

void InvalidateResourceLocked(ID3D12Resource *resource)
{
	if (!resource)
		return;
	ID3D12Resource *canonical = CanonicalResource(resource);
	UnregisterCpuMappableResource(resource, canonical);
	auto found = gResourceByPtr.find(canonical);
	if (found == gResourceByPtr.end())
		found = gResourceByPtr.find(resource);
	if (found == gResourceByPtr.end() || found->second >= gResources.size())
		return;

	const size_t index = found->second;
	ResourceRecord &record = gResources[index];
	if (!record.resource)
		return;
	if (record.resource != resource && record.resource != canonical) {
		DX12LogManager::DebugJsonFunc("DX12ResourceInvalidateSkipped",
			"reason=resource_mismatch,resource=%p,canonical=%p,recordResource=%p,index=%zu",
			resource,
			canonical,
			record.resource,
			index);
		return;
	}
	const UINT64 invalidatedGpuVa = record.gpuVirtualAddress;
	ResourceRecord oldRecord = record;
	if (record.gpuVirtualAddress) {
		gBufferResourceByGpuVa.erase(record.gpuVirtualAddress);
		gBufferResourceByGpuVaComplete.erase(record.gpuVirtualAddress);
	}
	if (index < gResourceAliasesByIndex.size()) {
		for (ID3D12Resource *alias : gResourceAliasesByIndex[index]) {
			auto aliasFound = gResourceByPtr.find(alias);
			if (aliasFound != gResourceByPtr.end() &&
				aliasFound->second == index)
				gResourceByPtr.erase(aliasFound);
		}
		gResourceAliasesByIndex[index].clear();
	} else {
		// Compatibility fallback for records created before the reverse index.
		gResourceByPtr.erase(resource);
		if (canonical != resource)
			gResourceByPtr.erase(canonical);
	}
	// Invalidate canonical cache entries for this resource
	AcquireSRWLockExclusive(&gCanonicalCacheLock);
	gCanonicalCache.erase(resource);
	if (canonical != resource)
		gCanonicalCache.erase(canonical);
	ReleaseSRWLockExclusive(&gCanonicalCacheLock);
	DX12InvalidateTextureHash(record.resource);
	DX12InvalidateBufferContent(record.resource);
	if (canonical != record.resource)
		DX12InvalidateTextureHash(canonical);
	if (canonical != record.resource)
		DX12InvalidateBufferContent(canonical);
	record = ResourceRecord();
	gFreeResourceSlots.push_back(index);
	if (gLiveResourceCount > 0)
		gLiveResourceCount--;
	DX12BufferResolveCacheUtils::InvalidateResourceRangeChangeLocked(
		&oldRecord, nullptr, "resource_invalidate", resource);
	DX12LogManager::DebugJsonFunc("DX12ResourceInvalidated",
		"resource=%p,canonical=%p,index=%zu,gpuVa=0x%llx,liveResources=%zu",
		resource,
		canonical,
		index,
		static_cast<unsigned long long>(invalidatedGpuVa),
		gLiveResourceCount);
}

typedef HRESULT(WINAPI *PFN_D3D12_CREATE_ROOT_SIGNATURE_DESERIALIZER_LOCAL)(
	LPCVOID, SIZE_T, REFIID, void**);
typedef HRESULT(WINAPI *PFN_D3D12_CREATE_VERSIONED_ROOT_SIGNATURE_DESERIALIZER_LOCAL)(
	LPCVOID, SIZE_T, REFIID, void**);

static HMODULE gD3D12ForDeserialization = nullptr;
static PFN_D3D12_CREATE_ROOT_SIGNATURE_DESERIALIZER_LOCAL gCreateRootSignatureDeserializer = nullptr;
static PFN_D3D12_CREATE_VERSIONED_ROOT_SIGNATURE_DESERIALIZER_LOCAL gCreateVersionedRootSignatureDeserializer = nullptr;
UINT64 Fnv1a64(const void *data, size_t size)
{
	const uint8_t *bytes = static_cast<const uint8_t*>(data);
	UINT64 hash = 14695981039346656037ull;
	for (size_t i = 0; i < size; ++i) {
		hash ^= bytes[i];
		hash *= 1099511628211ull;
	}
	return hash;
}

void ReleaseDescriptorRecordResources(DescriptorRecord *record)
{
	if (!record)
		return;
	if (record->ownsResource && record->resource)
		record->resource->Release();
	if (record->ownsCounterResource && record->counterResource)
		record->counterResource->Release();
	record->resource = nullptr;
	record->counterResource = nullptr;
	record->ownsResource = false;
	record->ownsCounterResource = false;
}

static void RetainDescriptorRecordResources(DescriptorRecord *record)
{
	if (!record)
		return;
	if (record->ownsResource && record->resource)
		record->resource->AddRef();
	if (record->ownsCounterResource && record->counterResource)
		record->counterResource->AddRef();
}

DescriptorRecord CloneDescriptorRecord(const DescriptorRecord &record)
{
	DescriptorRecord clone = record;
	RetainDescriptorRecordResources(&clone);
	return clone;
}

void PrepareDescriptorValueId(DescriptorRecord *record)
{
	if (!record)
		return;
	if (record->valueId == 0)
		record->valueId = DX12DescriptorValueHashUtils::HashRecord(*record);
}

D3D12_RESOURCE_DESC ResourceDescFromDesc1(const D3D12_RESOURCE_DESC1 *desc)
{
	D3D12_RESOURCE_DESC out = {};
	if (!desc)
		return out;
	out.Dimension = desc->Dimension;
	out.Alignment = desc->Alignment;
	out.Width = desc->Width;
	out.Height = desc->Height;
	out.DepthOrArraySize = desc->DepthOrArraySize;
	out.MipLevels = desc->MipLevels;
	out.Format = desc->Format;
	out.SampleDesc = desc->SampleDesc;
	out.Layout = desc->Layout;
	out.Flags = desc->Flags;
	return out;
}

void SetDescriptorKind(DescriptorRecord *record, UINT kindId)
{
	if (!record)
		return;
	record->kindId = kindId;
	switch (kindId) {
	case DescriptorKind::CBV_ID: record->kind = "CBV"; break;
	case DescriptorKind::SRV_ID: record->kind = "SRV"; break;
	case DescriptorKind::UAV_ID: record->kind = "UAV"; break;
	case DescriptorKind::RTV_ID: record->kind = "RTV"; break;
	case DescriptorKind::DSV_ID: record->kind = "DSV"; break;
	case DescriptorKind::Sampler_ID: record->kind = "Sampler"; break;
	default: record->kind.clear(); break;
	}
}

ID3D12Resource *CanonicalResource(ID3D12Resource *resource)
{
	if (!resource)
		return nullptr;

	// Fast path: cache lookup (shared lock)
	AcquireSRWLockShared(&gCanonicalCacheLock);
	std::unordered_map<ID3D12Resource*, ID3D12Resource*>::const_iterator it =
		gCanonicalCache.find(resource);
	if (it != gCanonicalCache.end()) {
		ID3D12Resource *canonical = it->second;
		ReleaseSRWLockShared(&gCanonicalCacheLock);
		return canonical;
	}
	ReleaseSRWLockShared(&gCanonicalCacheLock);

	// Slow path: QI + cache insert (exclusive lock)
	IUnknown *unknown = nullptr;
	if (SUCCEEDED(resource->QueryInterface(IID_PPV_ARGS(&unknown))) && unknown) {
		ID3D12Resource *canonical = reinterpret_cast<ID3D12Resource*>(unknown);
		unknown->Release();

		AcquireSRWLockExclusive(&gCanonicalCacheLock);
		gCanonicalCache[resource] = canonical;
		ReleaseSRWLockExclusive(&gCanonicalCacheLock);

		return canonical;
	}
	return resource;
}

static size_t *FindTrackedResourceIndexLocked(ID3D12Resource *resource)
{
	if (!resource)
		return nullptr;

	ID3D12Resource *canonical = CanonicalResource(resource);
	auto found = gResourceByPtr.find(canonical);
	if (found == gResourceByPtr.end())
		found = gResourceByPtr.find(resource);
	if (found == gResourceByPtr.end())
		return nullptr;
	return &found->second;
}

void RecordResource(
	ID3D12Resource *resource,
	const D3D12_RESOURCE_DESC *desc,
	const D3D12_HEAP_PROPERTIES *heapProperties,
	const PlacedResourceInfo *placedInfo,
	D3D12_RESOURCE_STATES initialState)
{
	if (!resource)
		return;

	ResourceRecord record;
	record.resource = resource;
	record.desc = desc ? *desc : resource->GetDesc();
	const bool bufferResource = record.desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER;
	const bool textureResource = IsTextureResourceDimension(record.desc.Dimension);
	if (!bufferResource && !textureResource)
		return;
	if (heapProperties) {
		record.heapType = heapProperties->Type;
		record.hasHeapType = true;
	}

	if (record.hasHeapType && record.heapType == D3D12_HEAP_TYPE_READBACK)
		return;

	DX12ObjectLifetimeTrackerUtils::InstallResourceReleaseTracker(resource);

	record.currentState = initialState;
	record.hasCurrentState = true;
	if (bufferResource) {
		record.gpuVirtualAddress = resource->GetGPUVirtualAddress();
		record.size = record.desc.Width;
		record.gpuVirtualAddressRealEnd = record.gpuVirtualAddress + record.size;
		record.gpuVirtualAddressOobEnd = record.gpuVirtualAddressRealEnd;
		if (record.gpuVirtualAddress == 0 && record.size > 0) {
			DX12FrameAnalysis::LogJsonFunc("TrackedBufferResourceZeroGpuVa",
				"\"resource\":\"%p\",\"size\":%llu,\"heapType\":%u,\"hasHeapType\":%u,\"dimension\":%u,\"format\":%u",
				resource,
				static_cast<unsigned long long>(record.size),
				record.hasHeapType ? static_cast<UINT>(record.heapType) : UINT_MAX,
				record.hasHeapType ? 1 : 0,
				static_cast<UINT>(record.desc.Dimension),
				static_cast<UINT>(record.desc.Format));
		}
		if (placedInfo && placedInfo->hasPlacedHeap) {
			record.hasPlacedHeapRange = true;
			record.placedHeapOffset = placedInfo->heapOffset;
			record.placedHeapSize = placedInfo->heapSize;
			if (placedInfo->heapSize > placedInfo->heapOffset)
				record.gpuVirtualAddressOobEnd =
					record.gpuVirtualAddress + (placedInfo->heapSize - placedInfo->heapOffset);
			if (record.gpuVirtualAddressOobEnd < record.gpuVirtualAddressRealEnd)
				record.gpuVirtualAddressOobEnd = record.gpuVirtualAddressRealEnd;
		}
		DX12BufferAddressRangeLogUtils::Record(resource, record);
		if (record.gpuVirtualAddress >= 0x10a4000000ull &&
			record.gpuVirtualAddress <= 0x10aaffffffull &&
			record.size >= 65536ull) {
			DX12FrameAnalysis::LogJsonFunc("TrackedBufferResourceRecorded",
				"\"resource\":\"%p\",\"gpuVa\":\"0x%llX\",\"size\":%llu,\"realEnd\":\"0x%llX\",\"oobEnd\":\"0x%llX\",\"heapType\":%u,\"hasPlacedHeap\":%u",
				resource,
				static_cast<unsigned long long>(record.gpuVirtualAddress),
				static_cast<unsigned long long>(record.size),
				static_cast<unsigned long long>(record.gpuVirtualAddressRealEnd),
				static_cast<unsigned long long>(record.gpuVirtualAddressOobEnd),
				record.hasHeapType ? static_cast<UINT>(record.heapType) : UINT_MAX,
				record.hasPlacedHeapRange ? 1 : 0);
		}
	}

	ID3D12Resource *canonical = CanonicalResource(resource);
	const auto lockBegin = std::chrono::steady_clock::now();
	AcquireSRWLockExclusive(&gResourceLock);
	auto found = gResourceByPtr.find(canonical);
	ResourceRecord oldRecord = {};
	bool hadOldRecord = false;
	if (found != gResourceByPtr.end()) {
		const size_t index = found->second;
		ResourceRecord &existing = gResources[found->second];
		oldRecord = existing;
		hadOldRecord = true;
		const bool sameIdentityRange =
			DX12BufferResolveCacheUtils::SameBufferRange(existing, record);
		record.identityGeneration = sameIdentityRange ?
			existing.identityGeneration :
			gResourceIdentityGeneration.fetch_add(
				1, std::memory_order_relaxed) + 1;
		if (existing.gpuVirtualAddress && existing.gpuVirtualAddress != record.gpuVirtualAddress) {
			gBufferResourceByGpuVa.erase(existing.gpuVirtualAddress);
			gBufferResourceByGpuVaComplete.erase(existing.gpuVirtualAddress);
		}
		gResources[found->second] = record;
		auto addAlias = [index](ID3D12Resource *alias) {
			if (!alias)
				return;
			gResourceByPtr[alias] = index;
			if (index >= gResourceAliasesByIndex.size())
				gResourceAliasesByIndex.resize(index + 1);
			auto &aliases = gResourceAliasesByIndex[index];
			if (std::find(aliases.begin(), aliases.end(), alias) == aliases.end())
				aliases.push_back(alias);
		};
		addAlias(resource);
		addAlias(canonical);
		if (record.gpuVirtualAddress && record.size) {
			gBufferResourceByGpuVa[record.gpuVirtualAddress] = found->second;
			gBufferResourceByGpuVaComplete[record.gpuVirtualAddress] = found->second;
		}
	} else {
		record.identityGeneration = gResourceIdentityGeneration.fetch_add(
			1, std::memory_order_relaxed) + 1;
		size_t index = 0;
		if (!gFreeResourceSlots.empty()) {
			index = gFreeResourceSlots.back();
			gFreeResourceSlots.pop_back();
			gResources[index] = record;
		} else {
			index = gResources.size();
			gResources.push_back(record);
		}
		gLiveResourceCount++;
		if (index >= gResourceAliasesByIndex.size())
			gResourceAliasesByIndex.resize(index + 1);
		auto addAlias = [index](ID3D12Resource *alias) {
			if (!alias)
				return;
			gResourceByPtr[alias] = index;
			auto &aliases = gResourceAliasesByIndex[index];
			if (std::find(aliases.begin(), aliases.end(), alias) == aliases.end())
				aliases.push_back(alias);
		};
		addAlias(resource);
		addAlias(canonical);
		if (record.gpuVirtualAddress && record.size) {
			gBufferResourceByGpuVa[record.gpuVirtualAddress] = index;
			gBufferResourceByGpuVaComplete[record.gpuVirtualAddress] = index;
		}
	}
	DX12BufferResolveCacheUtils::InvalidateResourceRangeChangeLocked(
		hadOldRecord ? &oldRecord : nullptr, &record, "resource_record_update", resource);
	gResourcesRecordedFromCreate++;
	LogResourceTrackerStatsLocked();
	ReleaseSRWLockExclusive(&gResourceLock);
	const auto lockEnd = std::chrono::steady_clock::now();
	const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(lockEnd - lockBegin).count();
	if (elapsedUs >= 1000) {
		gResourceExclusiveLockSlowCount++;
		if (static_cast<UINT64>(elapsedUs) > gResourceExclusiveLockSlowMaxUs)
			gResourceExclusiveLockSlowMaxUs = static_cast<UINT64>(elapsedUs);
		DX12LogManager::DebugJsonFunc("DX12ResourceLockSlow",
			"source=RecordResource,elapsedUs=%lld,resource=%p,present=%ld,threadId=%lu",
			static_cast<long long>(elapsedUs),
			resource,
			DX12RuntimeState::GetPresentCount(),
			GetCurrentThreadId());
	}
}

void CleanupTrackedResources()
{
	DX12BindingCollectQueue::Stop();
	DX12ResourceCollectQueue::Stop();
	// DX12DescriptorCollectPipeline::Stop() removed - pipeline was dead code
	AcquireSRWLockExclusive(&gResourceLock);
	AcquireSRWLockExclusive(&gDescriptorLock);
	for (DescriptorHeapRecord &heap : gDescriptorHeaps) {
		DescriptorHeapRecord::StateLocks *stateLocks = heap.stateLocks.get();
		if (stateLocks)
			stateLocks->LockAllExclusive();
		if (stateLocks)
			stateLocks->Deactivate();
		ClearDescriptorHeapLocked(&heap);
		if (stateLocks)
			stateLocks->UnlockAllExclusive();
	}
	ClearCpuDescriptorLedgerLocked();
	gRootSignatures.clear();
	gDescriptorHeaps.clear();
	gRootSignatureByPtr.clear();
	gDescriptorHeapByPtr.clear();
	gDescriptorHeapByCpuStart.clear();
	gPsoRoots.clear();
	gPsoRootByIndex.clear();
	gResources.clear();
	gFreeResourceSlots.clear();
	gLiveResourceCount = 0;
	gResourceByPtr.clear();
	gResourceAliasesByIndex.clear();
	gBufferResourceByGpuVa.clear();
	gBufferResourceByGpuVaComplete.clear();
	DX12BufferResolveCacheUtils::InvalidateAllLocked("cleanup");
	ReleaseSRWLockExclusive(&gDescriptorLock);
	ReleaseSRWLockExclusive(&gResourceLock);

	if (gD3D12ForDeserialization) {
		FreeLibrary(gD3D12ForDeserialization);
		gD3D12ForDeserialization = nullptr;
		gCreateRootSignatureDeserializer = nullptr;
		gCreateVersionedRootSignatureDeserializer = nullptr;
	}
}

static bool EnsureRootSignatureDeserializerFunctions()
{
	if (gCreateRootSignatureDeserializer || gCreateVersionedRootSignatureDeserializer)
		return true;

	if (!gD3D12ForDeserialization) {
		wchar_t path[MAX_PATH];
		if (GetSystemDirectoryW(path, MAX_PATH)) {
			PathAppendW(path, L"d3d12.dll");
			gD3D12ForDeserialization = LoadLibraryW(path);
		}
	}

	if (!gD3D12ForDeserialization)
		return false;

	gCreateRootSignatureDeserializer =
		reinterpret_cast<PFN_D3D12_CREATE_ROOT_SIGNATURE_DESERIALIZER_LOCAL>(
			GetProcAddress(gD3D12ForDeserialization, "D3D12CreateRootSignatureDeserializer"));
	gCreateVersionedRootSignatureDeserializer =
		reinterpret_cast<PFN_D3D12_CREATE_VERSIONED_ROOT_SIGNATURE_DESERIALIZER_LOCAL>(
			GetProcAddress(gD3D12ForDeserialization, "D3D12CreateVersionedRootSignatureDeserializer"));
	return gCreateRootSignatureDeserializer || gCreateVersionedRootSignatureDeserializer;
}

static UINT RootSignatureVersionNumber(D3D_ROOT_SIGNATURE_VERSION version)
{
	switch (version) {
	case D3D_ROOT_SIGNATURE_VERSION_1_0:
		return 0x10;
	case D3D_ROOT_SIGNATURE_VERSION_1_1:
		return 0x11;
	default:
		return static_cast<UINT>(version);
	}
}

static void RecordDescriptorRange(
	DX12RootParameterSummary *parameter, UINT rangeIndex,
	const D3D12_DESCRIPTOR_RANGE &range, UINT *appendOffset)
{
	if (!parameter || !appendOffset)
		return;

	DX12RootDescriptorRangeSummary summary;
	summary.rootParameterIndex = parameter->rootParameterIndex;
	summary.rangeIndex = rangeIndex;
	summary.rangeType = static_cast<UINT>(range.RangeType);
	summary.numDescriptors = range.NumDescriptors;
	summary.baseShaderRegister = range.BaseShaderRegister;
	summary.registerSpace = range.RegisterSpace;
	summary.offsetInDescriptorsFromTableStart = range.OffsetInDescriptorsFromTableStart;
	summary.shaderVisibility = parameter->shaderVisibility;
	if (range.OffsetInDescriptorsFromTableStart == D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND)
		summary.effectiveOffset = *appendOffset;
	else
		summary.effectiveOffset = range.OffsetInDescriptorsFromTableStart;

	if (range.NumDescriptors != UINT_MAX) {
		const UINT64 end = static_cast<UINT64>(summary.effectiveOffset) + range.NumDescriptors;
		*appendOffset = end > UINT_MAX ? UINT_MAX : static_cast<UINT>(end);
	}

	parameter->ranges.push_back(summary);
}

static void RecordDescriptorRange1(
	DX12RootParameterSummary *parameter, UINT rangeIndex,
	const D3D12_DESCRIPTOR_RANGE1 &range, UINT *appendOffset)
{
	if (!parameter || !appendOffset)
		return;

	DX12RootDescriptorRangeSummary summary;
	summary.rootParameterIndex = parameter->rootParameterIndex;
	summary.rangeIndex = rangeIndex;
	summary.rangeType = static_cast<UINT>(range.RangeType);
	summary.numDescriptors = range.NumDescriptors;
	summary.baseShaderRegister = range.BaseShaderRegister;
	summary.registerSpace = range.RegisterSpace;
	summary.offsetInDescriptorsFromTableStart = range.OffsetInDescriptorsFromTableStart;
	summary.flags = static_cast<UINT>(range.Flags);
	summary.shaderVisibility = parameter->shaderVisibility;
	if (range.OffsetInDescriptorsFromTableStart == D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND)
		summary.effectiveOffset = *appendOffset;
	else
		summary.effectiveOffset = range.OffsetInDescriptorsFromTableStart;

	if (range.NumDescriptors != UINT_MAX) {
		const UINT64 end = static_cast<UINT64>(summary.effectiveOffset) + range.NumDescriptors;
		*appendOffset = end > UINT_MAX ? UINT_MAX : static_cast<UINT>(end);
	}

	parameter->ranges.push_back(summary);
}

static void ParseRootParameter(
	RootSignatureRecord *record, UINT index, const D3D12_ROOT_PARAMETER &source)
{
	if (!record)
		return;

	DX12RootParameterSummary parameter;
	parameter.rootParameterIndex = index;
	parameter.parameterType = static_cast<UINT>(source.ParameterType);
	parameter.shaderVisibility = static_cast<UINT>(source.ShaderVisibility);
	switch (source.ParameterType) {
	case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE: {
		UINT appendOffset = 0;
		for (UINT i = 0; i < source.DescriptorTable.NumDescriptorRanges; ++i)
			RecordDescriptorRange(&parameter, i, source.DescriptorTable.pDescriptorRanges[i], &appendOffset);
		break;
	}
	case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
		parameter.shaderRegister = source.Constants.ShaderRegister;
		parameter.registerSpace = source.Constants.RegisterSpace;
		parameter.num32BitValues = source.Constants.Num32BitValues;
		break;
	case D3D12_ROOT_PARAMETER_TYPE_CBV:
	case D3D12_ROOT_PARAMETER_TYPE_SRV:
	case D3D12_ROOT_PARAMETER_TYPE_UAV:
		parameter.shaderRegister = source.Descriptor.ShaderRegister;
		parameter.registerSpace = source.Descriptor.RegisterSpace;
		break;
	default:
		break;
	}
	record->parameters.push_back(std::move(parameter));
}

static void ParseRootParameter1(
	RootSignatureRecord *record, UINT index, const D3D12_ROOT_PARAMETER1 &source)
{
	if (!record)
		return;

	DX12RootParameterSummary parameter;
	parameter.rootParameterIndex = index;
	parameter.parameterType = static_cast<UINT>(source.ParameterType);
	parameter.shaderVisibility = static_cast<UINT>(source.ShaderVisibility);
	switch (source.ParameterType) {
	case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE: {
		UINT appendOffset = 0;
		for (UINT i = 0; i < source.DescriptorTable.NumDescriptorRanges; ++i)
			RecordDescriptorRange1(&parameter, i, source.DescriptorTable.pDescriptorRanges[i], &appendOffset);
		break;
	}
	case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
		parameter.shaderRegister = source.Constants.ShaderRegister;
		parameter.registerSpace = source.Constants.RegisterSpace;
		parameter.num32BitValues = source.Constants.Num32BitValues;
		break;
	case D3D12_ROOT_PARAMETER_TYPE_CBV:
	case D3D12_ROOT_PARAMETER_TYPE_SRV:
	case D3D12_ROOT_PARAMETER_TYPE_UAV:
		parameter.shaderRegister = source.Descriptor.ShaderRegister;
		parameter.registerSpace = source.Descriptor.RegisterSpace;
		parameter.rootDescriptorFlags = static_cast<UINT>(source.Descriptor.Flags);
		break;
	default:
		break;
	}
	record->parameters.push_back(std::move(parameter));
}

void ParseRootSignatureBlob(RootSignatureRecord *record, const void *blob, SIZE_T blobLength)
{
	if (!record || !blob || blobLength == 0)
		return;
	if (!EnsureRootSignatureDeserializerFunctions())
		return;

	ID3D12VersionedRootSignatureDeserializer *versioned = nullptr;
	HRESULT hr = gCreateVersionedRootSignatureDeserializer ?
		gCreateVersionedRootSignatureDeserializer(blob, blobLength, IID_PPV_ARGS(&versioned)) :
		E_NOINTERFACE;
	if (SUCCEEDED(hr) && versioned) {
		const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *desc = nullptr;
		hr = versioned->GetRootSignatureDescAtVersion(D3D_ROOT_SIGNATURE_VERSION_1_1, &desc);
		if (FAILED(hr))
			hr = versioned->GetRootSignatureDescAtVersion(D3D_ROOT_SIGNATURE_VERSION_1_0, &desc);
		if (SUCCEEDED(hr) && desc) {
			record->version = RootSignatureVersionNumber(desc->Version);
			if (desc->Version == D3D_ROOT_SIGNATURE_VERSION_1_1) {
				record->flags = static_cast<UINT>(desc->Desc_1_1.Flags);
				record->staticSamplerCount = desc->Desc_1_1.NumStaticSamplers;
				record->parameters.reserve(desc->Desc_1_1.NumParameters);
				for (UINT i = 0; i < desc->Desc_1_1.NumParameters; ++i)
					ParseRootParameter1(record, i, desc->Desc_1_1.pParameters[i]);
			} else {
				record->flags = static_cast<UINT>(desc->Desc_1_0.Flags);
				record->staticSamplerCount = desc->Desc_1_0.NumStaticSamplers;
				record->parameters.reserve(desc->Desc_1_0.NumParameters);
				for (UINT i = 0; i < desc->Desc_1_0.NumParameters; ++i)
					ParseRootParameter(record, i, desc->Desc_1_0.pParameters[i]);
			}
			record->parsed = true;
		}
		versioned->Release();
		return;
	}

	ID3D12RootSignatureDeserializer *legacy = nullptr;
	hr = gCreateRootSignatureDeserializer ?
		gCreateRootSignatureDeserializer(blob, blobLength, IID_PPV_ARGS(&legacy)) :
		E_NOINTERFACE;
	if (FAILED(hr) || !legacy)
		return;

	const D3D12_ROOT_SIGNATURE_DESC *desc = legacy->GetRootSignatureDesc();
	if (desc) {
		record->version = RootSignatureVersionNumber(D3D_ROOT_SIGNATURE_VERSION_1_0);
		record->flags = static_cast<UINT>(desc->Flags);
		record->staticSamplerCount = desc->NumStaticSamplers;
		record->parameters.reserve(desc->NumParameters);
		for (UINT i = 0; i < desc->NumParameters; ++i)
			ParseRootParameter(record, i, desc->pParameters[i]);
		record->parsed = true;
	}
	legacy->Release();
}

void FillResourceInfo(DescriptorRecord *record, ID3D12Resource *resource)
{
	if (!record || !resource)
		return;

	record->resource = resource;
	AcquireSRWLockShared(&gResourceLock);
	size_t *index = FindTrackedResourceIndexLocked(resource);
	if (index && *index < gResources.size()) {
		const ResourceRecord &resourceRecord = gResources[*index];
		record->resourceDesc = resourceRecord.desc;
		record->hasResourceDesc = true;
		if (resourceRecord.desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
			record->gpuVirtualAddress = resourceRecord.gpuVirtualAddress;
		if (resourceRecord.hasHeapType) {
			record->resourceHeapType = static_cast<UINT>(resourceRecord.heapType);
			record->hasResourceHeapType = true;
		}
		record->currentState = resourceRecord.currentState;
		record->hasCurrentState = resourceRecord.hasCurrentState;
		record->stateTrackedByBarrier = resourceRecord.stateTrackedByBarrier;
	}
	ReleaseSRWLockShared(&gResourceLock);
}

void FillResourcePointerInfo(DescriptorRecord *record, ID3D12Resource *resource)
{
	if (!record || !resource)
		return;

	record->resource = resource;
	if (DX12ResourceTrackingPolicyUtils::NeedsFullResourceMetadata()) {
		FillResourceInfo(record, resource);
	} else {
		const D3D12_RESOURCE_DESC desc = resource->GetDesc();
		if (desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
			record->resourceDesc = desc;
			record->hasResourceDesc = true;
			record->gpuVirtualAddress = resource->GetGPUVirtualAddress();
		}
	}
}

void CaptureDescriptorResourcePointer(DescriptorRecord *record, ID3D12Resource *resource)
{
	if (!record || !resource)
		return;
	// Descriptor creation already captures the descriptor value itself. Keep
	// only the resource identity on the caller thread and defer resource-desc /
	// state completion to query or dump time.
	record->resource = resource;
}

bool ResolveBufferByGpuVa(UINT64 gpuVa, UINT64 size, DescriptorRecord *record)
{
	if (!record || gpuVa == 0)
		return false;

	bool resolved = false;
	const UINT64 requestedEnd = gpuVa + size;
	if (requestedEnd < gpuVa)
		return false;

	AcquireSRWLockShared(&gResourceLock);
	auto foundBuffer = gBufferResourceByGpuVa.upper_bound(gpuVa);
	if (foundBuffer != gBufferResourceByGpuVa.begin()) {
		--foundBuffer;
		if (foundBuffer->second < gResources.size()) {
			const ResourceRecord &resource = gResources[foundBuffer->second];
			const UINT64 begin = resource.gpuVirtualAddress;
			const UINT64 realEnd = resource.gpuVirtualAddressRealEnd ?
				resource.gpuVirtualAddressRealEnd : begin + resource.size;
			const UINT64 oobEnd = resource.gpuVirtualAddressOobEnd ?
				resource.gpuVirtualAddressOobEnd : realEnd;
			if (resource.desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER ||
				begin == 0 || resource.size == 0 ||
				gpuVa < begin || gpuVa >= realEnd) {
				ReleaseSRWLockShared(&gResourceLock);
				return false;
			}
			record->resource = resource.resource;
			record->resourceOffset = gpuVa - begin;
			const UINT64 available = realEnd - gpuVa;
			record->viewSize = (available && size > available) ? available : size;
			record->gpuVirtualAddress = gpuVa;
			if (DX12ResourceTrackingPolicyUtils::NeedsFullResourceMetadata()) {
				record->resourceDesc = resource.desc;
				record->hasResourceDesc = true;
			}
			if (resource.hasHeapType) {
				record->resourceHeapType = static_cast<UINT>(resource.heapType);
				record->hasResourceHeapType = true;
			}
			record->currentState = resource.currentState;
			record->hasCurrentState = resource.hasCurrentState;
			record->stateTrackedByBarrier = resource.stateTrackedByBarrier;
			resolved = true;
		}
	}
	ReleaseSRWLockShared(&gResourceLock);
	return resolved;
}

static UINT DxgiFormatBytesPerElement(DXGI_FORMAT format)
{
	switch (format) {
	case DXGI_FORMAT_R32G32B32A32_TYPELESS:
	case DXGI_FORMAT_R32G32B32A32_FLOAT:
	case DXGI_FORMAT_R32G32B32A32_UINT:
	case DXGI_FORMAT_R32G32B32A32_SINT:
		return 16;
	case DXGI_FORMAT_R32G32B32_TYPELESS:
	case DXGI_FORMAT_R32G32B32_FLOAT:
	case DXGI_FORMAT_R32G32B32_UINT:
	case DXGI_FORMAT_R32G32B32_SINT:
		return 12;
	case DXGI_FORMAT_R16G16B16A16_TYPELESS:
	case DXGI_FORMAT_R16G16B16A16_FLOAT:
	case DXGI_FORMAT_R16G16B16A16_UNORM:
	case DXGI_FORMAT_R16G16B16A16_UINT:
	case DXGI_FORMAT_R16G16B16A16_SNORM:
	case DXGI_FORMAT_R16G16B16A16_SINT:
	case DXGI_FORMAT_R32G32_TYPELESS:
	case DXGI_FORMAT_R32G32_FLOAT:
	case DXGI_FORMAT_R32G32_UINT:
	case DXGI_FORMAT_R32G32_SINT:
	case DXGI_FORMAT_R32G8X24_TYPELESS:
	case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
	case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
	case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
		return 8;
	case DXGI_FORMAT_R10G10B10A2_TYPELESS:
	case DXGI_FORMAT_R10G10B10A2_UNORM:
	case DXGI_FORMAT_R10G10B10A2_UINT:
	case DXGI_FORMAT_R11G11B10_FLOAT:
	case DXGI_FORMAT_R8G8B8A8_TYPELESS:
	case DXGI_FORMAT_R8G8B8A8_UNORM:
	case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
	case DXGI_FORMAT_R8G8B8A8_UINT:
	case DXGI_FORMAT_R8G8B8A8_SNORM:
	case DXGI_FORMAT_R8G8B8A8_SINT:
	case DXGI_FORMAT_R16G16_TYPELESS:
	case DXGI_FORMAT_R16G16_FLOAT:
	case DXGI_FORMAT_R16G16_UNORM:
	case DXGI_FORMAT_R16G16_UINT:
	case DXGI_FORMAT_R16G16_SNORM:
	case DXGI_FORMAT_R16G16_SINT:
	case DXGI_FORMAT_R32_TYPELESS:
	case DXGI_FORMAT_D32_FLOAT:
	case DXGI_FORMAT_R32_FLOAT:
	case DXGI_FORMAT_R32_UINT:
	case DXGI_FORMAT_R32_SINT:
	case DXGI_FORMAT_R24G8_TYPELESS:
	case DXGI_FORMAT_D24_UNORM_S8_UINT:
	case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
	case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
	case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
	case DXGI_FORMAT_R8G8_B8G8_UNORM:
	case DXGI_FORMAT_G8R8_G8B8_UNORM:
	case DXGI_FORMAT_B8G8R8A8_UNORM:
	case DXGI_FORMAT_B8G8R8X8_UNORM:
	case DXGI_FORMAT_B8G8R8A8_TYPELESS:
	case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
	case DXGI_FORMAT_B8G8R8X8_TYPELESS:
	case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
		return 4;
	case DXGI_FORMAT_R8G8_TYPELESS:
	case DXGI_FORMAT_R8G8_UNORM:
	case DXGI_FORMAT_R8G8_UINT:
	case DXGI_FORMAT_R8G8_SNORM:
	case DXGI_FORMAT_R8G8_SINT:
	case DXGI_FORMAT_R16_TYPELESS:
	case DXGI_FORMAT_R16_FLOAT:
	case DXGI_FORMAT_D16_UNORM:
	case DXGI_FORMAT_R16_UNORM:
	case DXGI_FORMAT_R16_UINT:
	case DXGI_FORMAT_R16_SNORM:
	case DXGI_FORMAT_R16_SINT:
	case DXGI_FORMAT_B5G6R5_UNORM:
	case DXGI_FORMAT_B5G5R5A1_UNORM:
	case DXGI_FORMAT_B4G4R4A4_UNORM:
		return 2;
	case DXGI_FORMAT_R8_TYPELESS:
	case DXGI_FORMAT_R8_UNORM:
	case DXGI_FORMAT_R8_UINT:
	case DXGI_FORMAT_R8_SNORM:
	case DXGI_FORMAT_R8_SINT:
	case DXGI_FORMAT_A8_UNORM:
		return 1;
	default:
		return 0;
	}
}

static UINT SrvBufferBytesPerElement(const D3D12_SHADER_RESOURCE_VIEW_DESC &desc)
{
	if (desc.Buffer.StructureByteStride)
		return desc.Buffer.StructureByteStride;
	if (desc.Buffer.Flags & D3D12_BUFFER_SRV_FLAG_RAW)
		return 4;
	return DxgiFormatBytesPerElement(desc.Format);
}

static UINT UavBufferBytesPerElement(const D3D12_UNORDERED_ACCESS_VIEW_DESC &desc)
{
	if (desc.Buffer.StructureByteStride)
		return desc.Buffer.StructureByteStride;
	if (desc.Buffer.Flags & D3D12_BUFFER_UAV_FLAG_RAW)
		return 4;
	return DxgiFormatBytesPerElement(desc.Format);
}

void FillSrvBufferView(DescriptorRecord *record)
{
	if (!record || !record->hasDesc ||
		record->srv.ViewDimension != D3D12_SRV_DIMENSION_BUFFER)
		return;

	const D3D12_BUFFER_SRV &buffer = record->srv.Buffer;
	const UINT bytesPerElement = SrvBufferBytesPerElement(record->srv);
	record->resourceOffset = buffer.FirstElement * bytesPerElement;
	record->viewSize = static_cast<UINT64>(buffer.NumElements) * bytesPerElement;
	if (record->viewSize == 0 && record->resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
		record->viewSize = record->resourceDesc.Width > record->resourceOffset ?
			record->resourceDesc.Width - record->resourceOffset : 0;
}

void FillUavBufferView(DescriptorRecord *record)
{
	if (!record || !record->hasDesc ||
		record->uav.ViewDimension != D3D12_UAV_DIMENSION_BUFFER)
		return;

	const D3D12_BUFFER_UAV &buffer = record->uav.Buffer;
	const UINT bytesPerElement = UavBufferBytesPerElement(record->uav);
	record->resourceOffset = buffer.FirstElement * bytesPerElement;
	record->viewSize = static_cast<UINT64>(buffer.NumElements) * bytesPerElement;
	if (record->viewSize == 0 && record->resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
		record->viewSize = record->resourceDesc.Width > record->resourceOffset ?
			record->resourceDesc.Width - record->resourceOffset : 0;
}

void LogResourceTrackerStatsLocked()
{
	const LONG present = DX12RuntimeState::GetPresentCount();
	const LONG last = InterlockedCompareExchange(&gLastResourceStatsPresent, 0, 0);
	if (present < last + kResourceStatsLogInterval)
		return;
	if (InterlockedCompareExchange(&gLastResourceStatsPresent, present, last) != last)
		return;
	DX12LogManager::DebugJsonFunc("DX12ResourceTrackerStats",
		"present=%ld,roots=%zu,rootRecordsSeen=%llu,"
		"heaps=%zu,heapRecordsSeen=%llu,"
		"descriptorSlots=%zu,shaderVisibleDescriptorSlots=%zu,descriptorSlotCapacity=%zu,descriptorHistoryEntries=%zu,"
		"descriptorHeapRanges=%zu,"
		"descriptorRecordsSeen=%llu,descriptorCopyRecordsSeen=%llu,"
		"descriptorCopySpanFastPathCalls=%llu,descriptorCopySpanFastPathSlots=%llu,descriptorCopySpanFallbackCalls=%llu,"
		"bufferResolveCacheEntries=%zu,bufferResolveGeneration=%llu,"
		"bufferResolveInvalidations=%llu,bufferResolveInvalidationEntries=%llu,"
		"bufferResolveRangeInvalidations=%llu,bufferResolveRangeInvalidationEntries=%llu,"
		"bufferResolveIndexNoopUpdates=%llu,bufferResolveStateOnlyUpdates=%llu,"
		"bufferResolveInvalidationEpoch=%llu,bufferResolveInvalidationHistoryLost=%llu,"
		"bufferResolveTlsHits=%llu,bufferResolveGlobalHits=%llu,bufferResolveBackwardScanHits=%llu,bufferResolveFallbackScanHits=%llu,bufferResolveMisses=%llu,bufferResolveGenerationResets=%llu,"
		"resourceExclusiveLockSlowCount=%llu,resourceExclusiveLockSlowMaxUs=%llu,descriptorExclusiveLockSlowCount=%llu,descriptorExclusiveLockSlowMaxUs=%llu,"
		"resources=%zu,resourceSlots=%zu,freeResourceSlots=%zu,"
		"resourcesFromCreate=%llu,cpuDescriptorLedger=%zu,"
		"psoRoots=%zu,psoRootRecordsSeen=%llu",
		present,
		gRootSignatures.size(),
		static_cast<unsigned long long>(gRootSignatureRecordsSeen),
		gDescriptorHeaps.size(),
		static_cast<unsigned long long>(gDescriptorHeapRecordsSeen),
		LiveDescriptorHeapSlotsLocked(),
		LiveShaderVisibleDescriptorHeapSlotsLocked(),
		DescriptorHeapSlotCapacityLocked(),
		DescriptorHeapHistoryEntriesLocked(),
		gDescriptorHeapByCpuStart.size(),
		static_cast<unsigned long long>(gDescriptorRecordsSeen),
		static_cast<unsigned long long>(gDescriptorCopyRecordsSeen),
		static_cast<unsigned long long>(gDescriptorCopySpanFastPathCalls),
		static_cast<unsigned long long>(gDescriptorCopySpanFastPathSlots),
		static_cast<unsigned long long>(gDescriptorCopySpanFallbackCalls),
		gBufferResolveCache.size(),
		static_cast<unsigned long long>(gBufferResolveGeneration.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(gBufferResolveCacheInvalidations),
		static_cast<unsigned long long>(gBufferResolveCacheInvalidationEntries),
		static_cast<unsigned long long>(gBufferResolveRangeInvalidations),
		static_cast<unsigned long long>(gBufferResolveRangeInvalidationEntries),
		static_cast<unsigned long long>(gBufferResolveIndexNoopUpdates),
		static_cast<unsigned long long>(gBufferResolveStateOnlyUpdates),
		static_cast<unsigned long long>(gBufferResolveInvalidationEpoch),
		static_cast<unsigned long long>(gBufferResolveInvalidationHistoryLost),
		static_cast<unsigned long long>(gBufferResolveTlsHits),
		static_cast<unsigned long long>(gBufferResolveGlobalHits),
		static_cast<unsigned long long>(gBufferResolveBackwardScanHits),
		static_cast<unsigned long long>(gBufferResolveFallbackScanHits),
		static_cast<unsigned long long>(gBufferResolveMisses),
		static_cast<unsigned long long>(gBufferResolveGenerationResets),
		static_cast<unsigned long long>(gResourceExclusiveLockSlowCount),
		static_cast<unsigned long long>(gResourceExclusiveLockSlowMaxUs),
		static_cast<unsigned long long>(gDescriptorExclusiveLockSlowCount),
		static_cast<unsigned long long>(gDescriptorExclusiveLockSlowMaxUs),
		gLiveResourceCount,
		gResources.size(),
		gFreeResourceSlots.size(),
		static_cast<unsigned long long>(gResourcesRecordedFromCreate),
		gDescriptorByCpuHandle.size(),
		gPsoRoots.size(),
		static_cast<unsigned long long>(gPsoRootRecordsSeen));
}

static bool DescriptorSlotFromHeapLocked(
	size_t heapIndex, SIZE_T cpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE *requiredType,
	DescriptorSlotLocation *location, UINT *slotsAvailable)
{
	if (heapIndex >= gDescriptorHeaps.size() || !cpuHandle || !location)
		return false;
	DescriptorHeapRecord &heap = gDescriptorHeaps[heapIndex];
	if ((requiredType && heap.desc.Type != *requiredType) ||
		heap.cpuStart == 0 || heap.increment == 0 || heap.desc.NumDescriptors == 0)
		return false;
	const SIZE_T begin = heap.cpuStart;
	const SIZE_T offset = cpuHandle >= begin ? cpuHandle - begin : 0;
	if (cpuHandle < begin || offset % heap.increment != 0)
		return false;
	const SIZE_T slot = offset / heap.increment;
	if (slot >= heap.desc.NumDescriptors || slot > UINT_MAX)
		return false;
	location->heapIndex = heapIndex;
	location->slot = static_cast<UINT>(slot);
	if (slotsAvailable)
		*slotsAvailable = heap.desc.NumDescriptors - location->slot;
	return true;
}

bool ResolveDescriptorSlotForCpuHandleLocked(
	SIZE_T cpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE *requiredType,
	DescriptorSlotLocation *location, UINT *slotsAvailable)
{
	if (!cpuHandle || !location)
		return false;
	struct DescriptorHeapLookupCache
	{
		size_t heapIndex = SIZE_MAX;
	};
	static thread_local DescriptorHeapLookupCache cache;
	if (DescriptorSlotFromHeapLocked(
			cache.heapIndex, cpuHandle, requiredType, location, slotsAvailable))
		return true;
	auto heapByStart = gDescriptorHeapByCpuStart.upper_bound(cpuHandle);
	if (heapByStart == gDescriptorHeapByCpuStart.begin())
		return false;
	--heapByStart;
	if (DescriptorSlotFromHeapLocked(
			heapByStart->second, cpuHandle, requiredType, location, slotsAvailable)) {
		cache.heapIndex = heapByStart->second;
		return true;
	}
	return false;
}

static bool DescriptorSlotForHandleLocked(SIZE_T cpuHandle, DescriptorSlotLocation *location)
{
	return ResolveDescriptorSlotForCpuHandleLocked(cpuHandle, nullptr, location, nullptr);
}

static bool DescriptorSlotForHandleLocked(
	SIZE_T cpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE type,
	DescriptorSlotLocation *location, UINT *slotsAvailable)
{
	return ResolveDescriptorSlotForCpuHandleLocked(cpuHandle, &type, location, slotsAvailable);
}

static bool FindLatestDescriptorAtSlotLocked(
	const DescriptorSlotLocation &location, DescriptorRecord *record)
{
	if (!record || location.heapIndex >= gDescriptorHeaps.size())
		return false;
	const DescriptorHeapRecord &heap = gDescriptorHeaps[location.heapIndex];
	const DescriptorRecord *slot = DescriptorHeapSlot(&heap, location.slot);
	if (!slot)
		return false;
	*record = CloneDescriptorRecord(*slot);
	return true;
}

static UINT64 DescriptorValueIdAtSlotLocked(const DescriptorSlotLocation &location)
{
	if (location.heapIndex >= gDescriptorHeaps.size())
		return 0;
	const DescriptorHeapRecord &heap = gDescriptorHeaps[location.heapIndex];
	const DescriptorRecord *slot = DescriptorHeapSlot(&heap, location.slot);
	return slot ? slot->valueId : 0;
}

bool DescriptorValueUnchanged(SIZE_T cpuHandle, UINT64 valueId)
{
	if (!cpuHandle || valueId == 0)
		return false;

	DescriptorRecord current;
	if (DX12DescriptorHeapShadowUtils::ReadDescriptorFromSlot(
			cpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES, &current))
		return current.valueId == valueId;

	AcquireSRWLockShared(&gDescriptorLock);
	const auto found = gDescriptorByCpuHandle.find(cpuHandle);
	const bool ledgerMatches = found != gDescriptorByCpuHandle.end() &&
		found->second.valueId == valueId;
	ReleaseSRWLockShared(&gDescriptorLock);
	return ledgerMatches;
}

static void RecordDescriptorAtSlotLocked(
	DescriptorRecord &&record, const DescriptorSlotLocation &location)
{
	if (record.valueId == 0)
		record.valueId = DX12DescriptorValueHashUtils::HashRecord(record);
	DescriptorHeapRecord &heap = gDescriptorHeaps[location.heapIndex];
	if (!EnsureDescriptorHeapStorageLocked(&heap)) {
		ReleaseDescriptorRecordResources(&record);
		return;
	}
	DescriptorRecord *existing = DescriptorHeapSlot(&heap, location.slot);
	if (existing) {
		if (existing->valueId == record.valueId &&
			existing->cpuHandle == record.cpuHandle) {
			ReleaseDescriptorRecordResources(&record);
			return;
		}
		ClearDescriptorHeapSlotLocked(&heap, location.slot);
	}
	if (location.slot >= heap.slots.size())
		return;
	++gDescriptorCopyRecordsSeen;
	StoreDescriptorAtHeapSlotLocked(&heap, location.slot, std::move(record));
}

static void RecordDescriptorInCacheLocked(DescriptorRecord &&record, bool copied)
{
	if (record.valueId == 0)
		record.valueId = DX12DescriptorValueHashUtils::HashRecord(record);

	DescriptorSlotLocation location = {};
	if (DescriptorSlotForHandleLocked(record.cpuHandle, &location)) {
		DescriptorHeapRecord &heap = gDescriptorHeaps[location.heapIndex];
		const bool storeInHeapSlot = copied || ShouldStoreCreatedDescriptorInHeapSlot(heap);
		if (!storeInHeapSlot) {
			// Non shader-visible/staging heap descriptors can become CopyDescriptors
			// sources later, so keep them in the CPU ledger as the fallback source.
			StoreCpuDescriptorRecordLocked(std::move(record));
			++gDescriptorRecordsSeen;
			return;
		}
		DescriptorRecord *existing = DescriptorHeapSlot(&heap, location.slot);
		if (existing) {
			if (existing->valueId == record.valueId &&
				existing->cpuHandle == record.cpuHandle) {
				ReleaseDescriptorRecordResources(&record);
				return;
			}
			ClearDescriptorHeapSlotLocked(&heap, location.slot);
		}
		if (copied)
			++gDescriptorCopyRecordsSeen;
		else
			++gDescriptorRecordsSeen;
		if (location.slot >= heap.slots.size())
			return;
		StoreDescriptorAtHeapSlotLocked(&heap, location.slot, std::move(record));
		return;
	}

	// Heap-external CPU descriptors are only discoverable through the ledger.
	StoreCpuDescriptorRecordLocked(std::move(record));
}

void RecordDescriptor(DescriptorRecord &&record)
{
	const auto lockBegin = std::chrono::steady_clock::now();
	AcquireSRWLockExclusive(&gDescriptorLock);
	RecordDescriptorInCacheLocked(std::move(record), false);
	BumpDescriptorQuerySerial();
	LogResourceTrackerStatsLocked();
	ReleaseSRWLockExclusive(&gDescriptorLock);
	const auto lockEnd = std::chrono::steady_clock::now();
	const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(lockEnd - lockBegin).count();
	if (elapsedUs >= 1000) {
		gDescriptorExclusiveLockSlowCount++;
		if (static_cast<UINT64>(elapsedUs) > gDescriptorExclusiveLockSlowMaxUs)
			gDescriptorExclusiveLockSlowMaxUs = static_cast<UINT64>(elapsedUs);
		DX12LogManager::DebugJsonFunc("DX12DescriptorLockSlow",
			"source=RecordDescriptor,elapsedUs=%lld,present=%ld,threadId=%lu",
			static_cast<long long>(elapsedUs),
			DX12RuntimeState::GetPresentCount(),
			GetCurrentThreadId());
	}
}

void RecordDescriptorBatch(std::vector<DescriptorRecord> *records)
{
	if (!records || records->empty())
		return;

	const size_t recordCount = records->size();
	const auto lockBegin = std::chrono::steady_clock::now();
	AcquireSRWLockExclusive(&gDescriptorLock);
	for (DescriptorRecord &queued : *records) {
		DescriptorRecord apply = queued;
		apply.ownsResource = false;
		apply.ownsCounterResource = false;
		RecordDescriptorInCacheLocked(std::move(apply), false);
		ReleaseDescriptorRecordResources(&queued);
	}
	BumpDescriptorQuerySerial();
	LogResourceTrackerStatsLocked();
	ReleaseSRWLockExclusive(&gDescriptorLock);
	records->clear();
	const auto lockEnd = std::chrono::steady_clock::now();
	const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(lockEnd - lockBegin).count();
	if (elapsedUs >= 1000) {
		gDescriptorExclusiveLockSlowCount++;
		if (static_cast<UINT64>(elapsedUs) > gDescriptorExclusiveLockSlowMaxUs)
			gDescriptorExclusiveLockSlowMaxUs = static_cast<UINT64>(elapsedUs);
		DX12LogManager::DebugJsonFunc("DX12DescriptorLockSlow",
			"source=RecordDescriptorBatch,elapsedUs=%lld,records=%zu,present=%ld,threadId=%lu",
			static_cast<long long>(elapsedUs),
			recordCount,
			DX12RuntimeState::GetPresentCount(),
			GetCurrentThreadId());
	}
}

void DX12RecordRootSignatureImmediate(
	ID3D12RootSignature *rootSignature,
	UINT nodeMask,
	const void *blob,
	SIZE_T blobLength)
{
	if (!rootSignature || !blob || blobLength == 0)
		return;

	RootSignatureRecord record;
	record.rootSignature = rootSignature;
	record.hash = Fnv1a64(blob, blobLength);
	record.size = blobLength;
	record.nodeMask = nodeMask;
	ParseRootSignatureBlob(&record, blob, blobLength);

	AcquireSRWLockExclusive(&gDescriptorLock);
	++gRootSignatureRecordsSeen;
	auto found = gRootSignatureByPtr.find(record.rootSignature);
	if (found != gRootSignatureByPtr.end()) {
		gRootSignatures[found->second] = std::move(record);
	}
	else {
		gRootSignatureByPtr[record.rootSignature] = gRootSignatures.size();
		gRootSignatures.push_back(std::move(record));
	}
	LogResourceTrackerStatsLocked();
	ReleaseSRWLockExclusive(&gDescriptorLock);
}

void DX12PrimeDescriptorHistoryForHunting()
{
	if (!DX12RuntimeState::IsHuntingMode())
		return;

	AcquireSRWLockExclusive(&gDescriptorLock);
	size_t occupied = 0;
	size_t primed = 0;
	for (DescriptorHeapRecord &heap : gDescriptorHeaps) {
		// Hunting history uses the dense compatibility adapter. Materialise the
		// current sparse state once when hunting is actually requested.
		if (!EnsureDescriptorHeapStorageLocked(&heap))
			continue;
		DescriptorHeapRecord::StateLocks *stateLocks = heap.stateLocks.get();
		if (stateLocks)
			stateLocks->LockAllExclusive();
		if (stateLocks && !stateLocks->IsActive()) {
			stateLocks->UnlockAllExclusive();
			continue;
		}
		const UINT slotCount = static_cast<UINT>(
			min(heap.slots.size(), heap.occupied.size()));
		for (UINT slot = 0; slot < slotCount; ++slot) {
			if (!DescriptorHeapSlotOccupied(heap, slot))
				continue;
			occupied++;
			if (slot < heap.slotHistory.size() && !heap.slotHistory[slot].empty())
				continue;
			const DescriptorRecord *record = DescriptorHeapSlot(&heap, slot);
			if (!record)
				continue;
			AppendDescriptorHistoryLocked(&heap, slot, *record);
			primed++;
		}
		if (stateLocks)
			stateLocks->UnlockAllExclusive();
	}
	DX12LogManager::JsonFunc("DX12DescriptorHistoryPrime",
		"heaps=%zu,occupied=%zu,primed=%zu,epoch=%llu,historyEntries=%zu",
		gDescriptorHeaps.size(),
		occupied,
		primed,
		static_cast<unsigned long long>(gDescriptorEpoch),
		DescriptorHeapHistoryEntriesLocked());
	LogResourceTrackerStatsLocked();
	ReleaseSRWLockExclusive(&gDescriptorLock);
}

UINT64 CurrentDescriptorEpoch()
{
	AcquireSRWLockShared(&gDescriptorLock);
	const UINT64 epoch = gDescriptorEpoch;
	ReleaseSRWLockShared(&gDescriptorLock);
	return epoch;
}

UINT64 CurrentDescriptorQuerySerial()
{
	return gDescriptorQuerySerial.load(std::memory_order_acquire);
}

UINT64 DX12GetDescriptorHeapVersion(ID3D12DescriptorHeap *heap)
{
	if (!heap)
		return 0;

	UINT64 version = 0;
	AcquireSRWLockShared(&gDescriptorLock);
	std::unordered_map<ID3D12DescriptorHeap*, size_t>::const_iterator found =
		gDescriptorHeapByPtr.find(heap);
	if (found != gDescriptorHeapByPtr.end() &&
		found->second < gDescriptorHeaps.size()) {
		const DescriptorHeapRecord &record = gDescriptorHeaps[found->second];
		if (record.heap == heap)
				version = DX12DescriptorHeapStateUtils::ReadVersion(record);
	}
	ReleaseSRWLockShared(&gDescriptorLock);
	return version;
}

bool DX12GetDescriptorSlotVersions(
	ID3D12DescriptorHeap *heap,
	const UINT *slots,
	size_t count,
	UINT64 *versions)
{
	if (!heap || (!slots && count) || (!versions && count))
		return false;
	for (size_t index = 0; index < count; ++index)
		versions[index] = 0;

	bool foundHeap = false;
	AcquireSRWLockShared(&gDescriptorLock);
	std::unordered_map<ID3D12DescriptorHeap*, size_t>::const_iterator found =
		gDescriptorHeapByPtr.find(heap);
	if (found != gDescriptorHeapByPtr.end() &&
			found->second < gDescriptorHeaps.size()) {
		const DescriptorHeapRecord &record = gDescriptorHeaps[found->second];
		if (record.heap == heap && record.stateLocks) {
			foundHeap = true;
			for (size_t index = 0; index < count; ++index) {
				if (slots[index] >= record.desc.NumDescriptors) {
					foundHeap = false;
					break;
				}
				versions[index] = record.stateLocks->ReadSlotVersion(slots[index]);
			}
		}
	}
	ReleaseSRWLockShared(&gDescriptorLock);
	return foundHeap;
}

bool DX12GetDescriptorSlotIdentities(
	ID3D12DescriptorHeap *heap,
	const UINT *slots,
	size_t count,
	UINT64 *versions,
	UINT64 *valueIds)
{
	if (!heap || (!slots && count) || (!versions && count) || (!valueIds && count))
		return false;
	for (size_t index = 0; index < count; ++index) {
		versions[index] = 0;
		valueIds[index] = 0;
	}

	bool foundHeap = false;
	AcquireSRWLockShared(&gDescriptorLock);
	std::unordered_map<ID3D12DescriptorHeap*, size_t>::const_iterator found =
		gDescriptorHeapByPtr.find(heap);
	if (found != gDescriptorHeapByPtr.end() &&
			found->second < gDescriptorHeaps.size()) {
		const DescriptorHeapRecord &record = gDescriptorHeaps[found->second];
		if (record.heap == heap && record.stateLocks) {
			foundHeap = true;
			for (size_t index = 0; index < count; ++index) {
				if (slots[index] >= record.desc.NumDescriptors) {
					foundHeap = false;
					break;
				}
				DescriptorHeapRecord::StateLocks::SlotIdentity identity;
				if (!record.stateLocks->ObserveSlotIdentity(slots[index], &identity)) {
					foundHeap = false;
					break;
				}
				versions[index] = identity.version;
				valueIds[index] = identity.valueId;
			}
		}
	}
	ReleaseSRWLockShared(&gDescriptorLock);
	return foundHeap;
}

void BumpDescriptorQuerySerial()
{
	UINT64 current = gDescriptorQuerySerial.load(std::memory_order_relaxed);
	for (;;) {
		UINT64 next = current == UINT64_MAX ? 1 : current + 1;
		if (gDescriptorQuerySerial.compare_exchange_weak(
				current,
				next,
				std::memory_order_release,
				std::memory_order_relaxed))
			return;
	}
}

static bool FindLatestDescriptorLocked(SIZE_T cpuHandle, DescriptorRecord *record)
{
	return DX12DescriptorHeapShadowUtils::FindLatestDescriptor(cpuHandle, record);
}

class DX12DescriptorCopyRangeUtils
{
public:
static bool RangesOverlap(
	const DescriptorSlotLocation &srcLocation,
	const DescriptorSlotLocation &destLocation,
	UINT count)
{
	if (srcLocation.heapIndex != destLocation.heapIndex || count == 0)
		return false;
	const UINT64 srcBegin = srcLocation.slot;
	const UINT64 srcEnd = srcBegin + count;
	const UINT64 destBegin = destLocation.slot;
	const UINT64 destEnd = destBegin + count;
	return srcBegin < destEnd && destBegin < srcEnd;
}

static bool GetSourceRecordLocked(
	const DescriptorHeapRecord &srcHeap,
	const DescriptorSlotLocation &srcLocation,
	SIZE_T srcCpuHandle,
	DescriptorRecord *storage,
	const DescriptorRecord **source)
{
	if (!storage || !source)
		return false;
	*source = DescriptorHeapSlot(&srcHeap, srcLocation.slot);
	if (*source)
		return true;
	if (!FindLatestDescriptorLocked(srcCpuHandle, storage))
		return false;
	*source = storage;
	return true;
}

static void StoreSlotValueLocked(
	DescriptorHeapRecord *destHeap,
	UINT destSlot,
	SIZE_T destCpuHandle,
	const DescriptorRecord &source,
	bool appendHistory)
{
	if (!destHeap || destSlot >= destHeap->slots.size())
		return;

	DescriptorRecord *existing = DescriptorHeapSlot(destHeap, destSlot);
	if (existing && DX12DescriptorSlotArrayUtils::SameRecordValueAtSlot(
			*existing, source, destCpuHandle)) {
		++gDescriptorCopyRecordsSeen;
		return;
	}

	if (existing)
		ClearDescriptorHeapSlotLocked(destHeap, destSlot);

	if (appendHistory) {
		DescriptorRecord historyRecord;
		DX12DescriptorSlotArrayUtils::CopyRecordValueForSlot(
			&historyRecord, source, destCpuHandle);
		AppendDescriptorHistoryLocked(destHeap, destSlot, historyRecord);
	}

	const bool wasOccupied = DescriptorHeapSlotOccupied(*destHeap, destSlot);
	DX12DescriptorSlotArrayUtils::CopyRecordValueForSlot(
		&destHeap->slots[destSlot], source, destCpuHandle);
	destHeap->occupied[destSlot] = 1;
	if (!wasOccupied)
		DX12DescriptorSlotArrayUtils::IncrementLiveSlots(destHeap);
	++gDescriptorCopyRecordsSeen;
	DX12DescriptorHeapStateUtils::BumpVersion(destHeap, destSlot);
}

static bool SourceHeapSpanReadyLocked(
	const DescriptorHeapRecord &srcHeap,
	UINT srcSlot,
	UINT count)
{
	if (count == 0)
		return false;
	if (srcSlot >= srcHeap.slots.size() || srcSlot >= srcHeap.occupied.size())
		return false;
	if (count > srcHeap.slots.size() - srcSlot ||
			count > srcHeap.occupied.size() - srcSlot)
		return false;
	for (UINT i = 0; i < count; ++i) {
		if (!DescriptorHeapSlotOccupied(srcHeap, srcSlot + i))
			return false;
	}
	return true;
}

static void CopySlotSpanValueLocked(
	DescriptorHeapRecord *destHeap,
	UINT destSlot,
	SIZE_T destCpuHandle,
	const DescriptorRecord &source)
{
	if (!destHeap || destSlot >= destHeap->slots.size() ||
			destSlot >= destHeap->occupied.size())
		return;

	const bool wasOccupied = DescriptorHeapSlotOccupied(*destHeap, destSlot);
	DX12DescriptorSlotArrayUtils::CopyRecordValueForSlot(
		&destHeap->slots[destSlot], source, destCpuHandle);
	destHeap->occupied[destSlot] = 1;
	if (!wasOccupied)
		DX12DescriptorSlotArrayUtils::IncrementLiveSlots(destHeap);
	DX12DescriptorHeapStateUtils::BumpVersion(destHeap, destSlot);
}

static bool TryCopyHeapSlotSpanLocked(
	const DescriptorHeapRecord &srcHeap,
	DescriptorHeapRecord *destHeap,
	UINT srcSlot,
	UINT destSlot,
	SIZE_T destHandle,
	UINT count,
	UINT increment)
{
	if (!destHeap || count == 0 || increment == 0)
		return false;
	if (DX12ResourceTrackingPolicyUtils::NeedsFullDescriptorHistory())
		return false;
	if (destSlot >= destHeap->slots.size() ||
			destSlot >= destHeap->occupied.size() ||
			count > destHeap->slots.size() - destSlot ||
			count > destHeap->occupied.size() - destSlot)
		return false;
	if (!SourceHeapSpanReadyLocked(srcHeap, srcSlot, count))
		return false;

	const bool sameHeap = &srcHeap == destHeap;
	const UINT64 srcBegin = srcSlot;
	const UINT64 srcEnd = srcBegin + count;
	const UINT64 destBegin = destSlot;
	const UINT64 destEnd = destBegin + count;
	const bool overlap = sameHeap && srcBegin < destEnd && destBegin < srcEnd;
	std::vector<DescriptorRecord> sourceSnapshot;
	if (overlap) {
		sourceSnapshot.resize(count);
		for (UINT i = 0; i < count; ++i) {
			DX12DescriptorSlotArrayUtils::CopyRecordValueForSlot(
				&sourceSnapshot[i], srcHeap.slots[srcSlot + i],
				srcHeap.slots[srcSlot + i].cpuHandle);
		}
	}

	for (UINT i = 0; i < count; ++i) {
		const DescriptorRecord &source = sourceSnapshot.empty() ?
			srcHeap.slots[srcSlot + i] : sourceSnapshot[i];
		const SIZE_T currentDestHandle =
			destHandle + static_cast<SIZE_T>(i) * increment;
		CopySlotSpanValueLocked(destHeap, destSlot + i, currentDestHandle, source);
	}

	gDescriptorCopyRecordsSeen += count;
	gDescriptorCopySpanFastPathCalls++;
	gDescriptorCopySpanFastPathSlots += count;
	return true;
}

static bool CopyHeapRangeLocked(
	const DescriptorSlotLocation &srcLocation,
	const DescriptorSlotLocation &destLocation,
	SIZE_T srcHandle,
	SIZE_T destHandle,
	UINT count,
	UINT increment)
{
	if (count == 0 || increment == 0 ||
			srcLocation.heapIndex >= gDescriptorHeaps.size() ||
			destLocation.heapIndex >= gDescriptorHeaps.size())
		return false;

	const DescriptorHeapRecord &srcHeap = gDescriptorHeaps[srcLocation.heapIndex];
	DescriptorHeapRecord &destHeap = gDescriptorHeaps[destLocation.heapIndex];
	if (!EnsureDescriptorHeapStorageLocked(&destHeap) ||
			destLocation.slot >= destHeap.slots.size())
		return false;

	const size_t destSlotsAvailable = destHeap.slots.size() - destLocation.slot;
	const UINT maxDestCount = destSlotsAvailable < count ?
		static_cast<UINT>(destSlotsAvailable) : count;
	const bool appendHistory = DX12ResourceTrackingPolicyUtils::NeedsFullDescriptorHistory();
	if (!appendHistory && TryCopyHeapSlotSpanLocked(
			srcHeap, &destHeap, srcLocation.slot, destLocation.slot,
			destHandle, maxDestCount, increment))
		return true;
	gDescriptorCopySpanFallbackCalls++;
	const bool overlap = RangesOverlap(srcLocation, destLocation, maxDestCount);
	std::vector<DescriptorRecord> sourceSnapshot;
	std::vector<uint8_t> sourceValid;
	if (overlap) {
		sourceSnapshot.resize(maxDestCount);
		sourceValid.assign(maxDestCount, 0);
		for (UINT i = 0; i < maxDestCount; ++i) {
			DescriptorSlotLocation currentSrc = srcLocation;
			currentSrc.slot += i;
			DescriptorRecord ledgerSource;
			const DescriptorRecord *source = nullptr;
			const SIZE_T currentSrcHandle = srcHandle + static_cast<SIZE_T>(i) * increment;
			if (!GetSourceRecordLocked(srcHeap, currentSrc, currentSrcHandle, &ledgerSource, &source)) {
				ReleaseDescriptorRecordResources(&ledgerSource);
				continue;
			}
			DX12DescriptorSlotArrayUtils::CopyRecordValueForSlot(
				&sourceSnapshot[i], *source, currentSrcHandle);
			sourceValid[i] = 1;
			ReleaseDescriptorRecordResources(&ledgerSource);
		}
	}

	for (UINT i = 0; i < maxDestCount; ++i) {
		const SIZE_T currentSrcHandle = srcHandle + static_cast<SIZE_T>(i) * increment;
		const SIZE_T currentDestHandle = destHandle + static_cast<SIZE_T>(i) * increment;
		const DescriptorRecord *source = nullptr;
		DescriptorRecord ledgerSource;
		if (overlap) {
			if (!sourceValid[i])
				continue;
			source = &sourceSnapshot[i];
		} else {
			DescriptorSlotLocation currentSrc = srcLocation;
			currentSrc.slot += i;
			if (!GetSourceRecordLocked(srcHeap, currentSrc, currentSrcHandle, &ledgerSource, &source)) {
				ReleaseDescriptorRecordResources(&ledgerSource);
				continue;
			}
		}
		StoreSlotValueLocked(&destHeap, destLocation.slot + i, currentDestHandle, *source, appendHistory);
		ReleaseDescriptorRecordResources(&ledgerSource);
	}
	return true;
}
};

static void RecordDescriptorLocked(DescriptorRecord &&record)
{
	RecordDescriptorInCacheLocked(std::move(record), true);
}

void RecordRuntimeDescriptor(DescriptorRecord &&record)
{
	if (!record.cpuHandle) {
		ReleaseDescriptorRecordResources(&record);
		return;
	}
	DX12DescriptorHeapShadowUtils::WriteDescriptorToSlot(record.cpuHandle, std::move(record));
}

void RecordRuntimeDescriptorCopyRange(
	ID3D12Device *device,
	D3D12_DESCRIPTOR_HEAP_TYPE type,
	D3D12_CPU_DESCRIPTOR_HANDLE destStart,
	D3D12_CPU_DESCRIPTOR_HANDLE srcStart,
	UINT count)
{
	(void)device; // No longer needed - ShadowUtils resolves handles via gDescriptorHeapByCpuStart
	DX12DescriptorHeapShadowUtils::CopyDescriptorRange(type, destStart, srcStart, count);
}

void RecordDescriptorCopyRange(
	ID3D12Device *device, D3D12_DESCRIPTOR_HEAP_TYPE type,
	D3D12_CPU_DESCRIPTOR_HANDLE destStart, D3D12_CPU_DESCRIPTOR_HANDLE srcStart,
	UINT count)
{
	(void)device;
	DX12DescriptorHeapShadowUtils::CopyDescriptorRange(type, destStart, srcStart, count);
}

void DX12RecordPsoRootSignature(
	UINT64 psoIndex, const char *kind, ID3D12PipelineState *pipelineState,
	ID3D12RootSignature *rootSignature)
{
	DX12LogManager::JsonFunc("DX12ResourceCollectQueue",
		"event=PsoRootSignatureImmediate,pso=%llu,kind=%s,pipelineState=%p,rootSignature=%p,threadId=%lu",
		static_cast<unsigned long long>(psoIndex),
		kind ? kind : "",
		pipelineState,
		rootSignature,
		GetCurrentThreadId());
	DX12RecordPsoRootSignatureImmediate(psoIndex, kind, pipelineState, rootSignature);
}

void DX12RecordPsoRootSignatureImmediate(
	UINT64 psoIndex, const char *kind, ID3D12PipelineState *pipelineState,
	ID3D12RootSignature *rootSignature)
{
	PsoRootRecord record;
	record.psoIndex = psoIndex;
	record.kind = kind ? kind : "";
	record.pipelineState = pipelineState;
	record.rootSignature = rootSignature;

	AcquireSRWLockExclusive(&gDescriptorLock);
	++gPsoRootRecordsSeen;
	auto found = gPsoRootByIndex.find(psoIndex);
	if (found != gPsoRootByIndex.end()) {
		gPsoRoots[found->second] = std::move(record);
	} else {
		gPsoRootByIndex[psoIndex] = gPsoRoots.size();
		gPsoRoots.push_back(std::move(record));
	}
	LogResourceTrackerStatsLocked();
	ReleaseSRWLockExclusive(&gDescriptorLock);
}
void DX12RecordResourceBarrier(UINT numBarriers, const D3D12_RESOURCE_BARRIER *barriers)
{
	if (!barriers || numBarriers == 0)
		return;

	AcquireSRWLockExclusive(&gResourceLock);
	bool resourceStateChanged = false;
	for (UINT i = 0; i < numBarriers; ++i) {
		const D3D12_RESOURCE_BARRIER &barrier = barriers[i];
		if (barrier.Type != D3D12_RESOURCE_BARRIER_TYPE_TRANSITION ||
			!barrier.Transition.pResource)
			continue;

		size_t *index = FindTrackedResourceIndexLocked(barrier.Transition.pResource);
		if (!index || *index >= gResources.size())
			continue;

		ResourceRecord &record = gResources[*index];
		if (barrier.Transition.Subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES ||
			record.desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
			record.currentState = barrier.Transition.StateAfter;
			record.hasCurrentState = true;
			record.stateTrackedByBarrier = true;
			resourceStateChanged = true;
		} else {
			// DX12 state is resource/subresource state, not descriptor state.
			// Keep the hot path O(barriers); dump/query code joins descriptors
			// with ResourceRecord when it needs a current state snapshot.
			record.hasCurrentState = false;
			record.stateTrackedByBarrier = true;
			resourceStateChanged = true;
		}
	}
	if (resourceStateChanged) {
		DX12BufferResolveCacheUtils::RecordStateOnlyUpdateLocked();
		DX12LogManager::DebugJsonFunc("BufferResolveCacheInvalidated",
			"reason=resource_barrier_state_update,mode=state_only,present=%ld,"
			"threadId=%lu,generation=%llu,gpuVaIndexSize=%zu,liveResources=%zu",
			DX12RuntimeState::GetPresentCount(),
			GetCurrentThreadId(),
			static_cast<unsigned long long>(gBufferResolveGeneration.load(std::memory_order_relaxed)),
			gBufferResourceByGpuVa.size(),
			gLiveResourceCount);
	}
	ReleaseSRWLockExclusive(&gResourceLock);
}



// RecordDescriptorMerged removed - dead code.  Hooks now call
// DX12DescriptorHeapShadowUtils::RecordDescriptor directly.

// ---- DX12ResourceTrackerHooks.cpp ----
#line 1 "DX12ResourceTrackerHooks.cpp"
#include "DX12Infrastructure.hpp"

#include "DX12RuntimeInterfaces.hpp"
#include "DX12FeatureInterfaces.hpp"
#include "DX12Integration.hpp"

// ── Buffer Content Capture via Map/Unmap hooks ───────────────────────
// Like DX11: capture UPLOAD buffer data at Unmap time so Tier 1
// (content CRC32) hashing works.  Maps are tracked per-thread.
struct DX12ResourceMapTracking
{
	ID3D12Resource *resource = nullptr;
	void *data = nullptr;
	UINT subresource = 0;
};

static thread_local DX12ResourceMapTracking gActiveMap;

struct DX12CpuMappableLookupCache
{
	ID3D12Resource *resource = nullptr;
	UINT64 generation = 0;
	bool eligible = false;
};

static thread_local DX12CpuMappableLookupCache gCpuMappableLookupCache;

static bool IsCpuMappableTrackedResource(ID3D12Resource *resource)
{
	const UINT64 generation =
		gCpuMappableResourceGeneration.load(std::memory_order_acquire);
	if (gCpuMappableLookupCache.resource == resource &&
		gCpuMappableLookupCache.generation == generation) {
		return gCpuMappableLookupCache.eligible;
	}

	AcquireSRWLockShared(&gCpuMappableResourceLock);
	const bool eligible = gCpuMappableResources.find(resource) != gCpuMappableResources.end();
	const UINT64 resolvedGeneration =
		gCpuMappableResourceGeneration.load(std::memory_order_relaxed);
	ReleaseSRWLockShared(&gCpuMappableResourceLock);
	gCpuMappableLookupCache.resource = resource;
	gCpuMappableLookupCache.generation = resolvedGeneration;
	gCpuMappableLookupCache.eligible = eligible;
	return eligible;
}

// Hooked Map ??records the mapped pointer for later content capture
static HRESULT STDMETHODCALLTYPE HookedResourceMap(
	ID3D12Resource *resource,
	UINT Subresource,
	const D3D12_RANGE *pReadRange,
	void **ppData)
{
	// Call original first to get the mapped pointer
	using MapFn = HRESULT(STDMETHODCALLTYPE *)(ID3D12Resource*, UINT, const D3D12_RANGE*, void**);
	MapFn original = reinterpret_cast<MapFn>(DX12ResourceTrackerHookState::ResourceOriginals.Map);
	if (!original)
		return E_FAIL;

	HRESULT hr = E_FAIL;
	{
		DX12_CREATE_TIMING_SCOPE(ResourceMapHook, Original);
		hr = original(resource, Subresource, pReadRange, ppData);
	}
	{
		DX12_CREATE_TIMING_SCOPE(ResourceMapHook, Record);
		if (SUCCEEDED(hr) && ppData && *ppData && IsCpuMappableTrackedResource(resource)) {
			gActiveMap.resource = resource;
			gActiveMap.data = *ppData;
			gActiveMap.subresource = Subresource;
		}
	}
	return hr;
}

// Hooked Unmap ??captures buffer content before unmapping
static void STDMETHODCALLTYPE HookedResourceUnmap(
	ID3D12Resource *resource,
	UINT Subresource,
	const D3D12_RANGE *pWrittenRange)
{
	{
		DX12_CREATE_TIMING_SCOPE(ResourceUnmapHook, Record);
		// Capture content if this resource was previously mapped AND tracked
		if (gActiveMap.resource == resource &&
		    gActiveMap.subresource == Subresource &&
		    gActiveMap.data) {
		// A null range means the whole subresource may have changed. An empty
		// range explicitly means no CPU writes and must not trigger a full copy.
		const bool hasWrites = !pWrittenRange || pWrittenRange->End > pWrittenRange->Begin;
		if (hasWrites) {
			size_t dataSize = 0;
			if (pWrittenRange) {
				dataSize = static_cast<size_t>(pWrittenRange->End - pWrittenRange->Begin);
			} else {
				D3D12_RESOURCE_DESC desc = resource->GetDesc();
				dataSize = static_cast<size_t>(desc.Width);
			}
			if (dataSize > 0) {
				const uint8_t *capturePtr =
					static_cast<const uint8_t*>(gActiveMap.data);
				if (pWrittenRange && pWrittenRange->Begin > 0)
					capturePtr += pWrittenRange->Begin;
				DX12CacheBufferContentRange(
					resource, pWrittenRange ? pWrittenRange->Begin : 0,
					capturePtr, dataSize);
			}
		}
			gActiveMap = DX12ResourceMapTracking();
		}
	}

	// Call original Unmap
	using UnmapFn = void(STDMETHODCALLTYPE *)(ID3D12Resource*, UINT, const D3D12_RANGE*);
	UnmapFn original = reinterpret_cast<UnmapFn>(DX12ResourceTrackerHookState::ResourceOriginals.Unmap);
	if (original) {
		DX12_CREATE_TIMING_SCOPE(ResourceUnmapHook, Original);
		original(resource, Subresource, pWrittenRange);
	}
}

// ── Resource VTable Hook Installation ─────────────────────────────────
void DX12ResourceTrackerHookRuntime::InstallMapUnmapHooks(ID3D12Resource *resource)
{
	if (!resource)
		return;

	// Map/Unmap hooks are only needed for buffer content capture (Dump feature).
	// Without an active Dump or Hunt there is nothing to capture, so skip the
	// GetHeapProperties driver call and VTable patching entirely.
	if (!DX12ResourceTrackingPolicyUtils::NeedsDumpOrHuntingTracking())
		return;

	D3D12_RESOURCE_DESC desc = resource->GetDesc();
	if (desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER)
		return;

	// Only hook UPLOAD/READBACK buffers (CPU-accessible)
	D3D12_HEAP_PROPERTIES heapProps = {};
	D3D12_HEAP_FLAGS heapFlags = {};
	HRESULT hr = resource->GetHeapProperties(&heapProps, &heapFlags);
	if (FAILED(hr))
		return;
	if (heapProps.Type != D3D12_HEAP_TYPE_UPLOAD &&
	    heapProps.Type != D3D12_HEAP_TYPE_READBACK)
		return;
	RegisterCpuMappableResource(resource);

	// Install Map/Unmap hooks on this resource (one-shot)
	DX12VTableHook hooks[] = {
		{DX12ResourceHookSlotTypes::ID3D12Resource::Map,
			reinterpret_cast<void**>(&DX12ResourceTrackerHookState::ResourceOriginals.Map),
			reinterpret_cast<void*>(HookedResourceMap), "ID3D12Resource::Map"},
		{DX12ResourceHookSlotTypes::ID3D12Resource::Unmap,
			reinterpret_cast<void**>(&DX12ResourceTrackerHookState::ResourceOriginals.Unmap),
			reinterpret_cast<void*>(HookedResourceUnmap), "ID3D12Resource::Unmap"},
	};
	DX12HookManager::InstallVTableHooks(resource, hooks, ARRAYSIZE(hooks));

	DX12LogManager::DebugJsonFunc("DX12BufferContentHook",
		"stage=installed,resource=0x%p,bytes=%llu,heapType=%u",
		resource, static_cast<unsigned long long>(desc.Width),
		static_cast<UINT>(heapProps.Type));
}

class DX12ResourceCreateLogUtils
{
public:
	inline static volatile LONG64 StageCalls = 0;
	inline static volatile LONG64 RecordCalls = 0;
	inline static volatile LONG64 RecordTracked = 0;
	inline static volatile LONG64 RecordSkipped = 0;
	static constexpr LONG64 StatsInterval = 4096;

	static bool ShouldLogSample(LONG64 value)
	{
		return value <= 8 || (value % StatsInterval) == 0;
	}

	static void LogStage(
		const char *api,
		const char *stage,
		ID3D12Device *device,
		HRESULT hr,
		const D3D12_RESOURCE_DESC *desc,
		const D3D12_HEAP_PROPERTIES *heapProperties,
		D3D12_RESOURCE_STATES initialState,
		void **resource)
	{
		const LONG64 calls = InterlockedIncrement64(&StageCalls);
		if (SUCCEEDED(hr) && !ShouldLogSample(calls))
			return;
		DX12LogManager::JsonFunc("DX12ResourceCreateStage",
			"api=%s,stage=%s,stageCalls=%lld,present=%ld,threadId=%lu,"
			"device=%p,hr=0x%lx,resourcePtr=%p,resource=%p,"
			"hasDesc=%u,dimension=%u,width=%llu,height=%u,format=%u,flags=0x%llx,"
			"hasHeap=%u,heapType=%u,initialState=%u,skipAll=%ld",
			api ? api : "",
			stage ? stage : "",
			static_cast<long long>(calls),
			DX12RuntimeState::GetPresentCount(),
			GetCurrentThreadId(),
			device,
			hr,
			resource,
			(resource && *resource) ? *resource : nullptr,
			desc ? 1 : 0,
			desc ? static_cast<UINT>(desc->Dimension) : 0,
			desc ? static_cast<unsigned long long>(desc->Width) : 0,
			desc ? desc->Height : 0,
			desc ? static_cast<UINT>(desc->Format) : 0,
			desc ? static_cast<unsigned long long>(desc->Flags) : 0,
			heapProperties ? 1 : 0,
			heapProperties ? static_cast<UINT>(heapProperties->Type) : 0,
			static_cast<UINT>(initialState),
			DX12HotPathState::skipAll);
	}

	static void LogRecordStats(const char *stage, HRESULT hr, void **resource, bool shouldTrack)
	{
		const LONG64 calls = InterlockedIncrement64(&RecordCalls);
		if (shouldTrack)
			InterlockedIncrement64(&RecordTracked);
		else
			InterlockedIncrement64(&RecordSkipped);
		if (SUCCEEDED(hr) && shouldTrack && !ShouldLogSample(calls))
			return;
		DX12LogManager::JsonFunc("DX12ResourceCreateRecord",
			"stage=%s,recordCalls=%lld,tracked=%lld,skipped=%lld,present=%ld,threadId=%lu,hr=0x%lx,resourcePtr=%p,resource=%p,shouldTrack=%u",
			stage ? stage : "",
			static_cast<long long>(calls),
			static_cast<long long>(InterlockedCompareExchange64(&RecordTracked, 0, 0)),
			static_cast<long long>(InterlockedCompareExchange64(&RecordSkipped, 0, 0)),
			DX12RuntimeState::GetPresentCount(),
			GetCurrentThreadId(),
			hr,
			resource,
			(resource && *resource) ? *resource : nullptr,
			shouldTrack ? 1 : 0);
	}

	static void LogFallback(ID3D12Resource *resource)
	{
		DX12LogManager::JsonFunc("DX12ResourceCollectQueue",
			"event=FallbackInlineRecord,resource=%p",
			resource);
	}
};

const D3D12_UNORDERED_ACCESS_VIEW_DESC *DX12ResourceTrackerHookRuntime::PrepareUavDesc(
	ID3D12Resource *resource,
	const D3D12_UNORDERED_ACCESS_VIEW_DESC *desc,
	D3D12_UNORDERED_ACCESS_VIEW_DESC *storage,
	const char *source)
{
	if (!desc || !storage)
		return desc;
	*storage = *desc;
	return DX12ModAdjustUavDesc(resource, storage, source) ? storage : desc;
}

const D3D12_RESOURCE_DESC *DX12ResourceTrackerHookRuntime::PrepareResourceDesc(
	const D3D12_RESOURCE_DESC *desc,
	D3D12_RESOURCE_DESC *storage,
	const char *source)
{
	if (!desc || !storage)
		return desc;
	*storage = *desc;
	return DX12ModAdjustBufferResourceDesc(storage, source) ? storage : desc;
}

const D3D12_RESOURCE_DESC1 *DX12ResourceTrackerHookRuntime::PrepareResourceDesc1(
	const D3D12_RESOURCE_DESC1 *desc,
	D3D12_RESOURCE_DESC1 *storage,
	const char *source)
{
	if (!desc || !storage)
		return desc;
	*storage = *desc;
	return DX12ModAdjustBufferResourceDesc1(storage, source) ? storage : desc;
}

bool DX12ResourceTrackerHookRuntime::ShouldRecordCreatedResource(const D3D12_RESOURCE_DESC *desc)
{
	// Resource creation is always a baseline fact. Only the heavier full-metadata
	// path is conditional; otherwise keep the always-needed dumpable resource set.
	if (DX12ResourceTrackingPolicyUtils::NeedsFullResourceMetadata())
		return true;
	if (!desc)
		return true;
	return desc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER ||
		IsTextureResourceDimension(desc->Dimension);
}

void DX12ResourceTrackerHookRuntime::LogResourceCreateStage(
	const char *api,
	const char *stage,
	ID3D12Device *device,
	HRESULT hr,
	const D3D12_RESOURCE_DESC *desc,
	const D3D12_HEAP_PROPERTIES *heapProperties,
	D3D12_RESOURCE_STATES initialState,
	void **resource)
{
	DX12ResourceCreateLogUtils::LogStage(
		api, stage, device, hr, desc, heapProperties, initialState, resource);
}

void DX12ResourceTrackerHookRuntime::LogResourceCreateStage1(
	const char *api,
	const char *stage,
	ID3D12Device *device,
	HRESULT hr,
	const D3D12_RESOURCE_DESC1 *desc,
	const D3D12_HEAP_PROPERTIES *heapProperties,
	D3D12_RESOURCE_STATES initialState,
	void **resource)
{
	D3D12_RESOURCE_DESC desc0 = ResourceDescFromDesc1(desc);
	LogResourceCreateStage(api, stage, device, hr, desc ? &desc0 : nullptr,
		heapProperties, initialState, resource);
}

void DX12ResourceTrackerHookRuntime::RecordCreatedDescriptorHeap(
	HRESULT hr, ID3D12Device *device, const D3D12_DESCRIPTOR_HEAP_DESC *desc, void **heap)
{
	if (FAILED(hr) || !device || !desc || !heap || !*heap)
		return;
	ID3D12DescriptorHeap *descriptorHeap = nullptr;
	if (FAILED(static_cast<IUnknown*>(*heap)->QueryInterface(IID_PPV_ARGS(&descriptorHeap))))
		return;

	DescriptorHeapRecord record;
	record.heap = descriptorHeap;
	record.desc = *desc;
	record.cpuStart = descriptorHeap->GetCPUDescriptorHandleForHeapStart().ptr;
	record.gpuStart = descriptorHeap->GetGPUDescriptorHandleForHeapStart().ptr;
	record.increment = device->GetDescriptorHandleIncrementSize(desc->Type);
	InitializeDescriptorHeapRecord(&record);
	DX12ObjectLifetimeTrackerUtils::InstallDescriptorHeapReleaseTracker(descriptorHeap);
	descriptorHeap->Release();

	AcquireSRWLockExclusive(&gDescriptorLock);
	++gDescriptorHeapRecordsSeen;
	auto found = gDescriptorHeapByPtr.find(record.heap);
	if (found != gDescriptorHeapByPtr.end()) {
		DescriptorHeapRecord &old = gDescriptorHeaps[found->second];
		InvalidateDescriptorHeapLocked(old.heap);
	}
	const size_t heapIndex = gDescriptorHeaps.size();
	gDescriptorHeapByPtr[record.heap] = heapIndex;
	if (record.cpuStart)
		gDescriptorHeapByCpuStart[record.cpuStart] = heapIndex;
	gDescriptorHeaps.push_back(std::move(record));
	BumpDescriptorQuerySerial();
	LogResourceTrackerStatsLocked();
	ReleaseSRWLockExclusive(&gDescriptorLock);
}

void DX12ResourceTrackerHookRuntime::RecordCreatedRootSignature(
	HRESULT hr, UINT nodeMask, const void *blob, SIZE_T blobLength, void **rootSignature)
{
	if (FAILED(hr) || !blob || !blobLength || !rootSignature || !*rootSignature)
		return;

	DX12RecordRootSignatureImmediate(
		static_cast<ID3D12RootSignature*>(*rootSignature),
		nodeMask,
		blob,
		blobLength);
}

void DX12ResourceTrackerHookRuntime::RecordCreatedConstantBufferView(
	const D3D12_CONSTANT_BUFFER_VIEW_DESC *desc,
	D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor)
{
	DX12DescriptorHeapShadowUtils::RecordCreatedConstantBufferViewIfChanged(
		desc, destDescriptor);
}

void DX12ResourceTrackerHookRuntime::RecordCreatedShaderResourceView(
	ID3D12Resource *resource,
	const D3D12_SHADER_RESOURCE_VIEW_DESC *desc,
	D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor)
{
	DescriptorRecord record;
	SetDescriptorKind(&record, DescriptorKind::SRV_ID);
	record.cpuHandle = destDescriptor.ptr;
	CaptureDescriptorResourcePointer(&record, resource);
	if (desc) {
		record.srv = *desc;
		record.hasDesc = true;
		FillSrvBufferView(&record);
	}
	PrepareDescriptorValueId(&record);
	DX12DescriptorHeapShadowUtils::RecordCreatedDescriptorIfChanged(std::move(record));
}

void DX12ResourceTrackerHookRuntime::RecordCreatedUnorderedAccessView(
	ID3D12Resource *resource,
	ID3D12Resource *counterResource,
	const D3D12_UNORDERED_ACCESS_VIEW_DESC *desc,
	D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor)
{
	DescriptorRecord record;
	SetDescriptorKind(&record, DescriptorKind::UAV_ID);
	record.cpuHandle = destDescriptor.ptr;
	record.counterResource = counterResource;
	CaptureDescriptorResourcePointer(&record, resource);
	if (desc) {
		record.uav = *desc;
		record.hasDesc = true;
		FillUavBufferView(&record);
	}
	PrepareDescriptorValueId(&record);
	DX12DescriptorHeapShadowUtils::RecordCreatedDescriptorIfChanged(std::move(record));
}

void DX12ResourceTrackerHookRuntime::RecordCreatedRenderTargetView(
	ID3D12Resource *resource,
	const D3D12_RENDER_TARGET_VIEW_DESC *desc,
	D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor)
{
	DescriptorRecord record;
	SetDescriptorKind(&record, DescriptorKind::RTV_ID);
	record.cpuHandle = destDescriptor.ptr;
	CaptureDescriptorResourcePointer(&record, resource);
	if (desc) {
		record.rtv = *desc;
		record.hasDesc = true;
	}
	PrepareDescriptorValueId(&record);
	DX12DescriptorHeapShadowUtils::RecordCreatedDescriptorIfChanged(std::move(record));
}

void DX12ResourceTrackerHookRuntime::RecordCreatedDepthStencilView(
	ID3D12Resource *resource,
	const D3D12_DEPTH_STENCIL_VIEW_DESC *desc,
	D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor)
{
	DescriptorRecord record;
	SetDescriptorKind(&record, DescriptorKind::DSV_ID);
	record.cpuHandle = destDescriptor.ptr;
	CaptureDescriptorResourcePointer(&record, resource);
	if (desc) {
		record.dsv = *desc;
		record.hasDesc = true;
	}
	PrepareDescriptorValueId(&record);
	DX12DescriptorHeapShadowUtils::RecordCreatedDescriptorIfChanged(std::move(record));
}

void DX12ResourceTrackerHookRuntime::RecordCreatedSampler(
	const D3D12_SAMPLER_DESC *desc,
	D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor)
{
	if (!desc)
		return;
	DescriptorRecord record;
	SetDescriptorKind(&record, DescriptorKind::Sampler_ID);
	record.cpuHandle = destDescriptor.ptr;
	record.sampler = *desc;
	record.hasDesc = true;
	PrepareDescriptorValueId(&record);
	DX12DescriptorHeapShadowUtils::RecordCreatedDescriptorIfChanged(std::move(record));
}

void DX12ResourceTrackerHookRuntime::RecordCopiedDescriptors(
	ID3D12Device *device,
	UINT numDestDescriptorRanges,
	const D3D12_CPU_DESCRIPTOR_HANDLE *destDescriptorRangeStarts,
	const UINT *destDescriptorRangeSizes,
	UINT numSrcDescriptorRanges,
	const D3D12_CPU_DESCRIPTOR_HANDLE *srcDescriptorRangeStarts,
	const UINT *srcDescriptorRangeSizes,
	D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapsType)
{
	if (!DX12ResourceTrackingPolicyUtils::NeedsDescriptorTracking() ||
			!destDescriptorRangeStarts || !srcDescriptorRangeStarts)
		return;

	UINT destRange = 0;
	UINT srcRange = 0;
	UINT destOffset = 0;
	UINT srcOffset = 0;
	DX12DescriptorHeapShadowUtils::CopyTelemetryBatch copyTelemetry;
	const UINT increment = device ?
		device->GetDescriptorHandleIncrementSize(descriptorHeapsType) : 0;
	if (!increment)
		return;
	D3D12_CPU_DESCRIPTOR_HANDLE pendingDest = {};
	D3D12_CPU_DESCRIPTOR_HANDLE pendingSrc = {};
	UINT pendingCount = 0;
	auto flushPendingRange = [&]() {
		if (!pendingCount)
			return;
		DX12DescriptorHeapShadowUtils::CopyDescriptorRange(
			descriptorHeapsType,
			pendingDest,
			pendingSrc,
			pendingCount,
			&copyTelemetry);
		pendingCount = 0;
	};
	while (destRange < numDestDescriptorRanges && srcRange < numSrcDescriptorRanges) {
		const UINT destRangeSize = destDescriptorRangeSizes ? destDescriptorRangeSizes[destRange] : 1;
		const UINT srcRangeSize = srcDescriptorRangeSizes ? srcDescriptorRangeSizes[srcRange] : 1;
		if (destOffset >= destRangeSize) {
			destRange++;
			destOffset = 0;
			continue;
		}
		if (srcOffset >= srcRangeSize) {
			srcRange++;
			srcOffset = 0;
			continue;
		}

		const UINT count = min(destRangeSize - destOffset, srcRangeSize - srcOffset);
		D3D12_CPU_DESCRIPTOR_HANDLE dest = destDescriptorRangeStarts[destRange];
		D3D12_CPU_DESCRIPTOR_HANDLE src = srcDescriptorRangeStarts[srcRange];
		dest.ptr += static_cast<SIZE_T>(destOffset) * increment;
		src.ptr += static_cast<SIZE_T>(srcOffset) * increment;
		const SIZE_T pendingBytes =
			static_cast<SIZE_T>(pendingCount) * increment;
		const bool contiguous = pendingCount != 0 &&
			dest.ptr == pendingDest.ptr + pendingBytes &&
			src.ptr == pendingSrc.ptr + pendingBytes &&
			count <= UINT_MAX - pendingCount;
		if (contiguous) {
			pendingCount += count;
		} else {
			flushPendingRange();
			pendingDest = dest;
			pendingSrc = src;
			pendingCount = count;
		}
		destOffset += count;
		srcOffset += count;
	}
	flushPendingRange();
	copyTelemetry.Flush();
}

void DX12ResourceTrackerHookRuntime::RecordCopiedDescriptorsSimple(
	ID3D12Device *device,
	UINT numDescriptors,
	D3D12_CPU_DESCRIPTOR_HANDLE destDescriptorRangeStart,
	D3D12_CPU_DESCRIPTOR_HANDLE srcDescriptorRangeStart,
	D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapsType)
{
	if (DX12ResourceTrackingPolicyUtils::NeedsDescriptorTracking())
		DX12DescriptorHeapShadowUtils::CopyDescriptorRange(
			descriptorHeapsType, destDescriptorRangeStart,
			srcDescriptorRangeStart, numDescriptors);
}

void DX12ResourceTrackerHookRuntime::RecordCreatedResource(
	HRESULT hr,
	void **resource,
	const D3D12_RESOURCE_DESC *desc,
	const D3D12_HEAP_PROPERTIES *heapProperties,
	const PlacedResourceInfo *placedInfo,
	D3D12_RESOURCE_STATES initialState)
{
	const bool shouldTrack = !FAILED(hr) && resource && *resource && ShouldRecordCreatedResource(desc);
	DX12ResourceCreateLogUtils::LogRecordStats("Record", hr, resource, shouldTrack);
	if (!shouldTrack)
	{
		return;
	}
	ID3D12Resource *d3dResource = static_cast<ID3D12Resource*>(*resource);
	RecordResource(d3dResource, desc, heapProperties, placedInfo, initialState);
	// ── Install Map/Unmap hooks for buffer content capture ──────────
	// Covers ALL creation paths (CreateCommittedResource, CreatePlacedResource,
	// CreateReservedResource, and all _1/_2/_3 variants).
	InstallMapUnmapHooks(d3dResource);
}

PlacedResourceInfo DX12ResourceTrackerHookRuntime::MakePlacedResourceInfo(
	ID3D12Heap *heap, UINT64 heapOffset)
{
	PlacedResourceInfo info;
	if (!heap)
		return info;
	info.hasPlacedHeap = true;
	info.heapOffset = heapOffset;
	info.heapSize = 0;
	return info;
}

void DX12ResourceTrackerHookRuntime::RecordCreatedResource1(
	HRESULT hr,
	void **resource,
	const D3D12_RESOURCE_DESC1 *desc,
	const D3D12_HEAP_PROPERTIES *heapProperties,
	const PlacedResourceInfo *placedInfo,
	D3D12_RESOURCE_STATES initialState)
{
	D3D12_RESOURCE_DESC desc0 = ResourceDescFromDesc1(desc);
	RecordCreatedResource(hr, resource, desc ? &desc0 : nullptr, heapProperties, placedInfo, initialState);
}

void DX12ResourceTrackerHookRuntime::RecordCreatedResourceWithLayout(
	HRESULT hr,
	void **resource,
	const D3D12_RESOURCE_DESC *desc,
	const D3D12_HEAP_PROPERTIES *heapProperties,
	const PlacedResourceInfo *placedInfo,
	D3D12_BARRIER_LAYOUT initialLayout)
{
	RecordCreatedResource(hr, resource, desc, heapProperties, placedInfo,
		DX12ResourceStateTracker::FromBarrierLayout(initialLayout));
}

void DX12ResourceTrackerHookRuntime::RecordCreatedResource1WithLayout(
	HRESULT hr,
	void **resource,
	const D3D12_RESOURCE_DESC1 *desc,
	const D3D12_HEAP_PROPERTIES *heapProperties,
	const PlacedResourceInfo *placedInfo,
	D3D12_BARRIER_LAYOUT initialLayout)
{
	D3D12_RESOURCE_DESC desc0 = ResourceDescFromDesc1(desc);
	RecordCreatedResourceWithLayout(hr, resource, desc ? &desc0 : nullptr, heapProperties, placedInfo, initialLayout);
}

HRESULT STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCreateDescriptorHeap(
	ID3D12Device *device, const D3D12_DESCRIPTOR_HEAP_DESC *desc, REFIID riid, void **heap)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device::CreateDescriptorHeap", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device::CreateDescriptorHeap, DX12ResourceTrackerHookState::OrigCreateDescriptorHeap, "ID3D12Device::CreateDescriptorHeap");
	if (DX12InternalReplayState::IsActive())
		return original ? original(device, desc, riid, heap) : E_FAIL;
	HRESULT hr = E_FAIL;
	{
		DX12_CREATE_TIMING_SCOPE(DescriptorHeapHook, Original);
		hr = original ? original(device, desc, riid, heap) : E_FAIL;
	}
	{
		DX12_CREATE_TIMING_SCOPE(DescriptorHeapHook, Record);
		DX12ResourceTrackerHookRuntime::RecordCreatedDescriptorHeap(hr, device, desc, heap);
	}
	{
		DX12_CREATE_TIMING_SCOPE(DescriptorHeapHook, Wrap);
		if (SUCCEEDED(hr) && heap && *heap)
			DX12Wrapper::WrapD3D12DescriptorHeapResult(riid, heap);
	}
	return hr;
}

HRESULT STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCreateRootSignature(
	ID3D12Device *device, UINT nodeMask, const void *blob, SIZE_T blobLength,
	REFIID riid, void **rootSignature)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device::CreateRootSignature", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device::CreateRootSignature, DX12ResourceTrackerHookState::OrigCreateRootSignature, "ID3D12Device::CreateRootSignature");
	if (DX12InternalReplayState::IsActive())
		return original ? original(device, nodeMask, blob, blobLength, riid, rootSignature) : E_FAIL;
	HRESULT hr = E_FAIL;
	{
		DX12_CREATE_TIMING_SCOPE(RootSignatureHook, Original);
		hr = original ? original(device, nodeMask, blob, blobLength, riid, rootSignature) : E_FAIL;
	}
	{
		DX12_CREATE_TIMING_SCOPE(RootSignatureHook, Record);
		DX12ResourceTrackerHookRuntime::RecordCreatedRootSignature(hr, nodeMask, blob, blobLength, rootSignature);
	}
	return hr;
}

void STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCreateConstantBufferView(
	ID3D12Device *device, const D3D12_CONSTANT_BUFFER_VIEW_DESC *desc,
	D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device::CreateConstantBufferView", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device::CreateConstantBufferView, DX12ResourceTrackerHookState::OrigCreateConstantBufferView, "ID3D12Device::CreateConstantBufferView");
	if (!original)
		return;
	if (DX12InternalReplayState::IsActive()) {
		original(device, desc, destDescriptor);
		return;
	}
	{
		DX12_CREATE_TIMING_SCOPE(DescriptorCbvHook, Original);
		original(device, desc, destDescriptor);
	}
	{
		DX12_CREATE_TIMING_SCOPE(DescriptorCbvHook, Record);
		DX12ResourceTrackerHookRuntime::RecordCreatedConstantBufferView(desc, destDescriptor);
	}
}

void STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCreateShaderResourceView(
	ID3D12Device *device, ID3D12Resource *resource,
	const D3D12_SHADER_RESOURCE_VIEW_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device::CreateShaderResourceView", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device::CreateShaderResourceView, DX12ResourceTrackerHookState::OrigCreateShaderResourceView, "ID3D12Device::CreateShaderResourceView");
	if (!original)
		return;
	if (DX12InternalReplayState::IsActive()) {
		original(device, resource, desc, destDescriptor);
		return;
	}
	{
		DX12_CREATE_TIMING_SCOPE(DescriptorSrvHook, Original);
		original(device, resource, desc, destDescriptor);
	}
	{
		DX12_CREATE_TIMING_SCOPE(DescriptorSrvHook, Record);
		DX12ResourceTrackerHookRuntime::RecordCreatedShaderResourceView(resource, desc, destDescriptor);
	}
}

void STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCreateUnorderedAccessView(
	ID3D12Device *device, ID3D12Resource *resource, ID3D12Resource *counterResource,
	const D3D12_UNORDERED_ACCESS_VIEW_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device::CreateUnorderedAccessView", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device::CreateUnorderedAccessView, DX12ResourceTrackerHookState::OrigCreateUnorderedAccessView, "ID3D12Device::CreateUnorderedAccessView");
	if (!original)
		return;
	if (DX12InternalReplayState::IsActive()) {
		original(device, resource, counterResource, desc, destDescriptor);
		return;
	}
	D3D12_UNORDERED_ACCESS_VIEW_DESC adjustedDesc = {};
	const D3D12_UNORDERED_ACCESS_VIEW_DESC *descToUse = desc;
	{
		DX12_CREATE_TIMING_SCOPE(DescriptorUavHook, Prepare);
		descToUse = DX12ResourceTrackerHookRuntime::PrepareUavDesc(
			resource, desc, &adjustedDesc, "CreateUnorderedAccessView");
	}
	{
		DX12_CREATE_TIMING_SCOPE(DescriptorUavHook, Original);
		original(device, resource, counterResource, descToUse, destDescriptor);
	}
	{
		DX12_CREATE_TIMING_SCOPE(DescriptorUavHook, Record);
		DX12ResourceTrackerHookRuntime::RecordCreatedUnorderedAccessView(
			resource, counterResource, descToUse, destDescriptor);
	}
}

void STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCreateRenderTargetView(
	ID3D12Device *device, ID3D12Resource *resource,
	const D3D12_RENDER_TARGET_VIEW_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device::CreateRenderTargetView", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device::CreateRenderTargetView, DX12ResourceTrackerHookState::OrigCreateRenderTargetView, "ID3D12Device::CreateRenderTargetView");
	if (!original)
		return;
	if (DX12InternalReplayState::IsActive()) {
		original(device, resource, desc, destDescriptor);
		return;
	}
	{
		DX12_CREATE_TIMING_SCOPE(DescriptorRtvHook, Original);
		original(device, resource, desc, destDescriptor);
	}
	{
		DX12_CREATE_TIMING_SCOPE(DescriptorRtvHook, Record);
		DX12ResourceTrackerHookRuntime::RecordCreatedRenderTargetView(resource, desc, destDescriptor);
	}
}

void STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCreateDepthStencilView(
	ID3D12Device *device, ID3D12Resource *resource,
	const D3D12_DEPTH_STENCIL_VIEW_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device::CreateDepthStencilView", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device::CreateDepthStencilView, DX12ResourceTrackerHookState::OrigCreateDepthStencilView, "ID3D12Device::CreateDepthStencilView");
	if (!original)
		return;
	if (DX12InternalReplayState::IsActive()) {
		original(device, resource, desc, destDescriptor);
		return;
	}
	{
		DX12_CREATE_TIMING_SCOPE(DescriptorDsvHook, Original);
		original(device, resource, desc, destDescriptor);
	}
	{
		DX12_CREATE_TIMING_SCOPE(DescriptorDsvHook, Record);
		DX12ResourceTrackerHookRuntime::RecordCreatedDepthStencilView(resource, desc, destDescriptor);
	}
}

void STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCreateSampler(
	ID3D12Device *device, const D3D12_SAMPLER_DESC *desc,
	D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device::CreateSampler", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device::CreateSampler, DX12ResourceTrackerHookState::OrigCreateSampler, "ID3D12Device::CreateSampler");
	if (!original)
		return;
	if (DX12InternalReplayState::IsActive()) {
		original(device, desc, destDescriptor);
		return;
	}
	{
		DX12_CREATE_TIMING_SCOPE(SamplerHook, Original);
		original(device, desc, destDescriptor);
	}
	{
		DX12_CREATE_TIMING_SCOPE(SamplerHook, Record);
		DX12ResourceTrackerHookRuntime::RecordCreatedSampler(desc, destDescriptor);
	}
}

void STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCopyDescriptors(
	ID3D12Device *device, UINT numDestDescriptorRanges,
	const D3D12_CPU_DESCRIPTOR_HANDLE *destDescriptorRangeStarts,
	const UINT *destDescriptorRangeSizes, UINT numSrcDescriptorRanges,
	const D3D12_CPU_DESCRIPTOR_HANDLE *srcDescriptorRangeStarts,
	const UINT *srcDescriptorRangeSizes, D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapsType)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device::CopyDescriptors", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device::CopyDescriptors, DX12ResourceTrackerHookState::OrigCopyDescriptors, "ID3D12Device::CopyDescriptors");
	if (!original)
		return;
	if (DX12InternalReplayState::IsActive()) {
		original(device, numDestDescriptorRanges, destDescriptorRangeStarts,
			destDescriptorRangeSizes, numSrcDescriptorRanges, srcDescriptorRangeStarts,
			srcDescriptorRangeSizes, descriptorHeapsType);
		return;
	}
	{
		DX12_CREATE_TIMING_SCOPE(CopyDescriptorsHook, Original);
		original(device, numDestDescriptorRanges, destDescriptorRangeStarts,
			destDescriptorRangeSizes, numSrcDescriptorRanges, srcDescriptorRangeStarts,
			srcDescriptorRangeSizes, descriptorHeapsType);
	}
	{
		DX12_CREATE_TIMING_SCOPE(CopyDescriptorsHook, Record);
		DX12ResourceTrackerHookRuntime::RecordCopiedDescriptors(
			device, numDestDescriptorRanges, destDescriptorRangeStarts, destDescriptorRangeSizes,
			numSrcDescriptorRanges, srcDescriptorRangeStarts, srcDescriptorRangeSizes, descriptorHeapsType);
	}
}

void STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCopyDescriptorsSimple(
	ID3D12Device *device, UINT numDescriptors, D3D12_CPU_DESCRIPTOR_HANDLE destDescriptorRangeStart,
	D3D12_CPU_DESCRIPTOR_HANDLE srcDescriptorRangeStart, D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapsType)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device::CopyDescriptorsSimple", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device::CopyDescriptorsSimple, DX12ResourceTrackerHookState::OrigCopyDescriptorsSimple, "ID3D12Device::CopyDescriptorsSimple");
	if (!original)
		return;
	if (DX12InternalReplayState::IsActive()) {
		original(device, numDescriptors, destDescriptorRangeStart,
			srcDescriptorRangeStart, descriptorHeapsType);
		return;
	}
	{
		DX12_CREATE_TIMING_SCOPE(CopyDescriptorsSimpleHook, Original);
		original(device, numDescriptors, destDescriptorRangeStart,
			srcDescriptorRangeStart, descriptorHeapsType);
	}
	{
		DX12_CREATE_TIMING_SCOPE(CopyDescriptorsSimpleHook, Record);
		DX12ResourceTrackerHookRuntime::RecordCopiedDescriptorsSimple(
			device, numDescriptors, destDescriptorRangeStart, srcDescriptorRangeStart, descriptorHeapsType);
	}
}

HRESULT STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCreateCommittedResource(
	ID3D12Device *device, const D3D12_HEAP_PROPERTIES *heapProperties,
	D3D12_HEAP_FLAGS heapFlags, const D3D12_RESOURCE_DESC *desc,
	D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE *optimizedClearValue,
	REFIID riid, void **resource)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device::CreateCommittedResource", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device::CreateCommittedResource, DX12ResourceTrackerHookState::OrigCreateCommittedResource, "ID3D12Device::CreateCommittedResource");
	if (!original)
		return E_FAIL;
	if (DX12InternalReplayState::IsActive())
		return original(device, heapProperties, heapFlags, desc,
			initialState, optimizedClearValue, riid, resource);
	D3D12_RESOURCE_DESC adjustedDesc = {};
	const D3D12_RESOURCE_DESC *descToUse = desc;
	{
		DX12_CREATE_TIMING_SCOPE(ResourceCommittedHook, Prepare);
		descToUse = DX12ResourceTrackerHookRuntime::PrepareResourceDesc(
			desc, &adjustedDesc, "CreateCommittedResource");
	}
	DX12ResourceTrackerHookRuntime::LogResourceCreateStage(
		"ID3D12Device::CreateCommittedResource", "OriginalBegin", device, S_OK,
		descToUse, heapProperties, initialState, resource);
	HRESULT hr = E_FAIL;
	{
		DX12_CREATE_TIMING_SCOPE(ResourceCommittedHook, Original);
		hr = original(device, heapProperties, heapFlags, descToUse,
			initialState, optimizedClearValue, riid, resource);
	}
	DX12ResourceTrackerHookRuntime::LogResourceCreateStage(
		"ID3D12Device::CreateCommittedResource", "OriginalEnd", device, hr,
		descToUse, heapProperties, initialState, resource);
	{
		DX12_CREATE_TIMING_SCOPE(ResourceCommittedHook, Record);
		DX12ResourceTrackerHookRuntime::RecordCreatedResource(
			hr, resource, descToUse, heapProperties, nullptr, initialState);
	}
	DX12ResourceTrackerHookRuntime::LogResourceCreateStage(
		"ID3D12Device::CreateCommittedResource", "HookReturn", device, hr,
		descToUse, heapProperties, initialState, resource);
	{
		DX12_CREATE_TIMING_SCOPE(ResourceCommittedHook, Wrap);
		if (SUCCEEDED(hr) && resource && *resource)
			DX12Wrapper::WrapD3D12ResourceResult(riid, resource);
	}
	return hr;
}

HRESULT STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCreatePlacedResource(
	ID3D12Device *device, ID3D12Heap *heap, UINT64 heapOffset,
	const D3D12_RESOURCE_DESC *desc, D3D12_RESOURCE_STATES initialState,
	const D3D12_CLEAR_VALUE *optimizedClearValue, REFIID riid, void **resource)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device::CreatePlacedResource", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device::CreatePlacedResource, DX12ResourceTrackerHookState::OrigCreatePlacedResource, "ID3D12Device::CreatePlacedResource");
	if (!original)
		return E_FAIL;
	if (DX12InternalReplayState::IsActive())
		return original(device, heap, heapOffset, desc,
			initialState, optimizedClearValue, riid, resource);
	DX12ResourceTrackerHookRuntime::LogResourceCreateStage(
		"ID3D12Device::CreatePlacedResource", "OriginalBegin", device, S_OK,
		desc, nullptr, initialState, resource);
	HRESULT hr = E_FAIL;
	{
		DX12_CREATE_TIMING_SCOPE(ResourcePlacedHook, Original);
		hr = original(device, heap, heapOffset, desc,
			initialState, optimizedClearValue, riid, resource);
	}
	DX12ResourceTrackerHookRuntime::LogResourceCreateStage(
		"ID3D12Device::CreatePlacedResource", "OriginalEnd", device, hr,
		desc, nullptr, initialState, resource);
	{
		DX12_CREATE_TIMING_SCOPE(ResourcePlacedHook, Record);
		const PlacedResourceInfo placedInfo =
			DX12ResourceTrackerHookRuntime::MakePlacedResourceInfo(heap, heapOffset);
		DX12ResourceTrackerHookRuntime::RecordCreatedResource(
			hr, resource, desc, nullptr, &placedInfo, initialState);
	}
	DX12ResourceTrackerHookRuntime::LogResourceCreateStage(
		"ID3D12Device::CreatePlacedResource", "HookReturn", device, hr,
		desc, nullptr, initialState, resource);
	{
		DX12_CREATE_TIMING_SCOPE(ResourcePlacedHook, Wrap);
		if (SUCCEEDED(hr) && resource && *resource)
			DX12Wrapper::WrapD3D12ResourceResult(riid, resource);
	}
	return hr;
}

HRESULT STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCreateReservedResource(
	ID3D12Device *device, const D3D12_RESOURCE_DESC *desc,
	D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE *optimizedClearValue,
	REFIID riid, void **resource)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device::CreateReservedResource", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device::CreateReservedResource, DX12ResourceTrackerHookState::OrigCreateReservedResource, "ID3D12Device::CreateReservedResource");
	if (!original)
		return E_FAIL;
	if (DX12InternalReplayState::IsActive())
		return original(device, desc, initialState,
			optimizedClearValue, riid, resource);
	DX12ResourceTrackerHookRuntime::LogResourceCreateStage(
		"ID3D12Device::CreateReservedResource", "OriginalBegin", device, S_OK,
		desc, nullptr, initialState, resource);
	HRESULT hr = E_FAIL;
	{
		DX12_CREATE_TIMING_SCOPE(ResourceReservedHook, Original);
		hr = original(device, desc, initialState,
			optimizedClearValue, riid, resource);
	}
	DX12ResourceTrackerHookRuntime::LogResourceCreateStage(
		"ID3D12Device::CreateReservedResource", "OriginalEnd", device, hr,
		desc, nullptr, initialState, resource);
	{
		DX12_CREATE_TIMING_SCOPE(ResourceReservedHook, Record);
		DX12ResourceTrackerHookRuntime::RecordCreatedResource(
			hr, resource, desc, nullptr, nullptr, initialState);
	}
	DX12ResourceTrackerHookRuntime::LogResourceCreateStage(
		"ID3D12Device::CreateReservedResource", "HookReturn", device, hr,
		desc, nullptr, initialState, resource);
	{
		DX12_CREATE_TIMING_SCOPE(ResourceReservedHook, Wrap);
		if (SUCCEEDED(hr) && resource && *resource)
			DX12Wrapper::WrapD3D12ResourceResult(riid, resource);
	}
	return hr;
}

HRESULT STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCreateFence(
	ID3D12Device *device, UINT64 initialValue, D3D12_FENCE_FLAGS flags,
	REFIID riid, void **fence)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device::CreateFence", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device::CreateFence,
		DX12ResourceTrackerHookState::OrigCreateFence, "ID3D12Device::CreateFence");
	if (!original)
		return E_FAIL;
	HRESULT hr = E_FAIL;
	{
		DX12_CREATE_TIMING_SCOPE(FenceHook, Original);
		hr = original(device, initialValue, flags, riid, fence);
	}
	{
		DX12_CREATE_TIMING_SCOPE(FenceHook, Wrap);
		if (SUCCEEDED(hr) && fence && *fence)
			DX12Wrapper::WrapD3D12FenceResult(riid, fence);
	}
	return hr;
}

HRESULT STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCreateCommittedResource1(
	ID3D12Device4 *device, const D3D12_HEAP_PROPERTIES *heapProperties,
	D3D12_HEAP_FLAGS heapFlags, const D3D12_RESOURCE_DESC *desc,
	D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE *optimizedClearValue,
	ID3D12ProtectedResourceSession *protectedSession, REFIID riid, void **resource)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device4::CreateCommittedResource1", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device4::CreateCommittedResource1, DX12ResourceTrackerHookState::OrigCreateCommittedResource1, "ID3D12Device4::CreateCommittedResource1");
	if (!original)
		return E_FAIL;
	if (DX12InternalReplayState::IsActive())
		return original(device, heapProperties, heapFlags, desc,
			initialState, optimizedClearValue, protectedSession, riid, resource);
	D3D12_RESOURCE_DESC adjustedDesc = {};
	const D3D12_RESOURCE_DESC *descToUse = desc;
	{
		DX12_CREATE_TIMING_SCOPE(ResourceCommittedHook, Prepare);
		descToUse = DX12ResourceTrackerHookRuntime::PrepareResourceDesc(
			desc, &adjustedDesc, "CreateCommittedResource1");
	}
	DX12ResourceTrackerHookRuntime::LogResourceCreateStage(
		"ID3D12Device4::CreateCommittedResource1", "OriginalBegin", device, S_OK,
		descToUse, heapProperties, initialState, resource);
	HRESULT hr = E_FAIL;
	{
		DX12_CREATE_TIMING_SCOPE(ResourceCommittedHook, Original);
		hr = original(device, heapProperties, heapFlags, descToUse,
			initialState, optimizedClearValue, protectedSession, riid, resource);
	}
	DX12ResourceTrackerHookRuntime::LogResourceCreateStage(
		"ID3D12Device4::CreateCommittedResource1", "OriginalEnd", device, hr,
		descToUse, heapProperties, initialState, resource);
	{
		DX12_CREATE_TIMING_SCOPE(ResourceCommittedHook, Record);
		DX12ResourceTrackerHookRuntime::RecordCreatedResource(
			hr, resource, descToUse, heapProperties, nullptr, initialState);
	}
	DX12ResourceTrackerHookRuntime::LogResourceCreateStage(
		"ID3D12Device4::CreateCommittedResource1", "HookReturn", device, hr,
		descToUse, heapProperties, initialState, resource);
	{
		DX12_CREATE_TIMING_SCOPE(ResourceCommittedHook, Wrap);
		if (SUCCEEDED(hr) && resource && *resource)
			DX12Wrapper::WrapD3D12ResourceResult(riid, resource);
	}
	return hr;
}

HRESULT STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCreateReservedResource1(
	ID3D12Device4 *device, const D3D12_RESOURCE_DESC *desc,
	D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE *optimizedClearValue,
	ID3D12ProtectedResourceSession *protectedSession, REFIID riid, void **resource)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device4::CreateReservedResource1", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device4::CreateReservedResource1, DX12ResourceTrackerHookState::OrigCreateReservedResource1, "ID3D12Device4::CreateReservedResource1");
	if (!original)
		return E_FAIL;
	if (DX12InternalReplayState::IsActive())
		return original(device, desc, initialState,
			optimizedClearValue, protectedSession, riid, resource);
	DX12ResourceTrackerHookRuntime::LogResourceCreateStage(
		"ID3D12Device4::CreateReservedResource1", "OriginalBegin", device, S_OK,
		desc, nullptr, initialState, resource);
	HRESULT hr = E_FAIL;
	{
		DX12_CREATE_TIMING_SCOPE(ResourceReservedHook, Original);
		hr = original(device, desc, initialState,
			optimizedClearValue, protectedSession, riid, resource);
	}
	DX12ResourceTrackerHookRuntime::LogResourceCreateStage(
		"ID3D12Device4::CreateReservedResource1", "OriginalEnd", device, hr,
		desc, nullptr, initialState, resource);
	{
		DX12_CREATE_TIMING_SCOPE(ResourceReservedHook, Record);
		DX12ResourceTrackerHookRuntime::RecordCreatedResource(
			hr, resource, desc, nullptr, nullptr, initialState);
	}
	DX12ResourceTrackerHookRuntime::LogResourceCreateStage(
		"ID3D12Device4::CreateReservedResource1", "HookReturn", device, hr,
		desc, nullptr, initialState, resource);
	{
		DX12_CREATE_TIMING_SCOPE(ResourceReservedHook, Wrap);
		if (SUCCEEDED(hr) && resource && *resource)
			DX12Wrapper::WrapD3D12ResourceResult(riid, resource);
	}
	return hr;
}

HRESULT STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCreateCommittedResource2(
	ID3D12Device8 *device, const D3D12_HEAP_PROPERTIES *heapProperties,
	D3D12_HEAP_FLAGS heapFlags, const D3D12_RESOURCE_DESC1 *desc,
	D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE *optimizedClearValue,
	ID3D12ProtectedResourceSession *protectedSession, REFIID riid, void **resource)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device8::CreateCommittedResource2", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device8::CreateCommittedResource2, DX12ResourceTrackerHookState::OrigCreateCommittedResource2, "ID3D12Device8::CreateCommittedResource2");
	if (!original)
		return E_FAIL;
	if (DX12InternalReplayState::IsActive())
		return original(device, heapProperties, heapFlags, desc,
			initialState, optimizedClearValue, protectedSession, riid, resource);
	D3D12_RESOURCE_DESC1 adjustedDesc = {};
	const D3D12_RESOURCE_DESC1 *descToUse = desc;
	{
		DX12_CREATE_TIMING_SCOPE(ResourceCommittedHook, Prepare);
		descToUse = DX12ResourceTrackerHookRuntime::PrepareResourceDesc1(
			desc, &adjustedDesc, "CreateCommittedResource2");
	}
	DX12ResourceTrackerHookRuntime::LogResourceCreateStage1(
		"ID3D12Device8::CreateCommittedResource2", "OriginalBegin", device, S_OK,
		descToUse, heapProperties, initialState, resource);
	HRESULT hr = E_FAIL;
	{
		DX12_CREATE_TIMING_SCOPE(ResourceCommittedHook, Original);
		hr = original(device, heapProperties, heapFlags, descToUse,
			initialState, optimizedClearValue, protectedSession, riid, resource);
	}
	DX12ResourceTrackerHookRuntime::LogResourceCreateStage1(
		"ID3D12Device8::CreateCommittedResource2", "OriginalEnd", device, hr,
		descToUse, heapProperties, initialState, resource);
	{
		DX12_CREATE_TIMING_SCOPE(ResourceCommittedHook, Record);
		DX12ResourceTrackerHookRuntime::RecordCreatedResource1(
			hr, resource, descToUse, heapProperties, nullptr, initialState);
	}
	DX12ResourceTrackerHookRuntime::LogResourceCreateStage1(
		"ID3D12Device8::CreateCommittedResource2", "HookReturn", device, hr,
		descToUse, heapProperties, initialState, resource);
	{
		DX12_CREATE_TIMING_SCOPE(ResourceCommittedHook, Wrap);
		if (SUCCEEDED(hr) && resource && *resource)
			DX12Wrapper::WrapD3D12ResourceResult(riid, resource);
	}
	return hr;
}

HRESULT STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCreatePlacedResource1(
	ID3D12Device8 *device, ID3D12Heap *heap, UINT64 heapOffset,
	const D3D12_RESOURCE_DESC1 *desc, D3D12_RESOURCE_STATES initialState,
	const D3D12_CLEAR_VALUE *optimizedClearValue, REFIID riid, void **resource)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device8::CreatePlacedResource1", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device8::CreatePlacedResource1, DX12ResourceTrackerHookState::OrigCreatePlacedResource1, "ID3D12Device8::CreatePlacedResource1");
	if (!original)
		return E_FAIL;
	if (DX12InternalReplayState::IsActive())
		return original(device, heap, heapOffset, desc,
			initialState, optimizedClearValue, riid, resource);
	DX12ResourceTrackerHookRuntime::LogResourceCreateStage1(
		"ID3D12Device8::CreatePlacedResource1", "OriginalBegin", device, S_OK,
		desc, nullptr, initialState, resource);
	HRESULT hr = E_FAIL;
	{
		DX12_CREATE_TIMING_SCOPE(ResourcePlacedHook, Original);
		hr = original(device, heap, heapOffset, desc,
			initialState, optimizedClearValue, riid, resource);
	}
	DX12ResourceTrackerHookRuntime::LogResourceCreateStage1(
		"ID3D12Device8::CreatePlacedResource1", "OriginalEnd", device, hr,
		desc, nullptr, initialState, resource);
	{
		DX12_CREATE_TIMING_SCOPE(ResourcePlacedHook, Record);
		const PlacedResourceInfo placedInfo =
			DX12ResourceTrackerHookRuntime::MakePlacedResourceInfo(heap, heapOffset);
		DX12ResourceTrackerHookRuntime::RecordCreatedResource1(
			hr, resource, desc, nullptr, &placedInfo, initialState);
	}
	DX12ResourceTrackerHookRuntime::LogResourceCreateStage1(
		"ID3D12Device8::CreatePlacedResource1", "HookReturn", device, hr,
		desc, nullptr, initialState, resource);
	{
		DX12_CREATE_TIMING_SCOPE(ResourcePlacedHook, Wrap);
		if (SUCCEEDED(hr) && resource && *resource)
			DX12Wrapper::WrapD3D12ResourceResult(riid, resource);
	}
	return hr;
}

HRESULT STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCreateCommittedResource3(
	ID3D12Device10 *device, const D3D12_HEAP_PROPERTIES *heapProperties,
	D3D12_HEAP_FLAGS heapFlags, const D3D12_RESOURCE_DESC1 *desc,
	D3D12_BARRIER_LAYOUT initialLayout, const D3D12_CLEAR_VALUE *optimizedClearValue,
	ID3D12ProtectedResourceSession *protectedSession, UINT32 numCastableFormats,
	const DXGI_FORMAT *castableFormats, REFIID riid, void **resource)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device10::CreateCommittedResource3", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device10::CreateCommittedResource3, DX12ResourceTrackerHookState::OrigCreateCommittedResource3, "ID3D12Device10::CreateCommittedResource3");
	if (!original)
		return E_FAIL;
	if (DX12InternalReplayState::IsActive())
		return original(device, heapProperties, heapFlags, desc,
			initialLayout, optimizedClearValue, protectedSession, numCastableFormats,
			castableFormats, riid, resource);
	D3D12_RESOURCE_DESC1 adjustedDesc = {};
	const D3D12_RESOURCE_DESC1 *descToUse = desc;
	{
		DX12_CREATE_TIMING_SCOPE(ResourceCommittedHook, Prepare);
		descToUse = DX12ResourceTrackerHookRuntime::PrepareResourceDesc1(
			desc, &adjustedDesc, "CreateCommittedResource3");
	}
	DX12ResourceTrackerHookRuntime::LogResourceCreateStage1(
		"ID3D12Device10::CreateCommittedResource3", "OriginalBegin", device, S_OK,
		descToUse, heapProperties, DX12ResourceStateTracker::FromBarrierLayout(initialLayout), resource);
	HRESULT hr = E_FAIL;
	{
		DX12_CREATE_TIMING_SCOPE(ResourceCommittedHook, Original);
		hr = original(device, heapProperties, heapFlags, descToUse,
			initialLayout, optimizedClearValue, protectedSession, numCastableFormats,
			castableFormats, riid, resource);
	}
	DX12ResourceTrackerHookRuntime::LogResourceCreateStage1(
		"ID3D12Device10::CreateCommittedResource3", "OriginalEnd", device, hr,
		descToUse, heapProperties, DX12ResourceStateTracker::FromBarrierLayout(initialLayout), resource);
	{
		DX12_CREATE_TIMING_SCOPE(ResourceCommittedHook, Record);
		DX12ResourceTrackerHookRuntime::RecordCreatedResource1WithLayout(
			hr, resource, descToUse, heapProperties, nullptr, initialLayout);
	}
	DX12ResourceTrackerHookRuntime::LogResourceCreateStage1(
		"ID3D12Device10::CreateCommittedResource3", "HookReturn", device, hr,
		descToUse, heapProperties, DX12ResourceStateTracker::FromBarrierLayout(initialLayout), resource);
	{
		DX12_CREATE_TIMING_SCOPE(ResourceCommittedHook, Wrap);
		if (SUCCEEDED(hr) && resource && *resource)
			DX12Wrapper::WrapD3D12ResourceResult(riid, resource);
	}
	return hr;
}

HRESULT STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCreatePlacedResource2(
	ID3D12Device10 *device, ID3D12Heap *heap, UINT64 heapOffset,
	const D3D12_RESOURCE_DESC1 *desc, D3D12_BARRIER_LAYOUT initialLayout,
	const D3D12_CLEAR_VALUE *optimizedClearValue, UINT32 numCastableFormats,
	const DXGI_FORMAT *castableFormats, REFIID riid, void **resource)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device10::CreatePlacedResource2", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device10::CreatePlacedResource2, DX12ResourceTrackerHookState::OrigCreatePlacedResource2, "ID3D12Device10::CreatePlacedResource2");
	if (!original)
		return E_FAIL;
	if (DX12InternalReplayState::IsActive())
		return original(device, heap, heapOffset, desc,
			initialLayout, optimizedClearValue, numCastableFormats, castableFormats,
			riid, resource);
	DX12ResourceTrackerHookRuntime::LogResourceCreateStage1(
		"ID3D12Device10::CreatePlacedResource2", "OriginalBegin", device, S_OK,
		desc, nullptr, DX12ResourceStateTracker::FromBarrierLayout(initialLayout), resource);
	HRESULT hr = E_FAIL;
	{
		DX12_CREATE_TIMING_SCOPE(ResourcePlacedHook, Original);
		hr = original(device, heap, heapOffset, desc,
			initialLayout, optimizedClearValue, numCastableFormats, castableFormats,
			riid, resource);
	}
	DX12ResourceTrackerHookRuntime::LogResourceCreateStage1(
		"ID3D12Device10::CreatePlacedResource2", "OriginalEnd", device, hr,
		desc, nullptr, DX12ResourceStateTracker::FromBarrierLayout(initialLayout), resource);
	{
		DX12_CREATE_TIMING_SCOPE(ResourcePlacedHook, Record);
		const PlacedResourceInfo placedInfo =
			DX12ResourceTrackerHookRuntime::MakePlacedResourceInfo(heap, heapOffset);
		DX12ResourceTrackerHookRuntime::RecordCreatedResource1WithLayout(
			hr, resource, desc, nullptr, &placedInfo, initialLayout);
	}
	DX12ResourceTrackerHookRuntime::LogResourceCreateStage1(
		"ID3D12Device10::CreatePlacedResource2", "HookReturn", device, hr,
		desc, nullptr, DX12ResourceStateTracker::FromBarrierLayout(initialLayout), resource);
	{
		DX12_CREATE_TIMING_SCOPE(ResourcePlacedHook, Wrap);
		if (SUCCEEDED(hr) && resource && *resource)
			DX12Wrapper::WrapD3D12ResourceResult(riid, resource);
	}
	return hr;
}

HRESULT STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCreateReservedResource2(
	ID3D12Device10 *device, const D3D12_RESOURCE_DESC *desc,
	D3D12_BARRIER_LAYOUT initialLayout, const D3D12_CLEAR_VALUE *optimizedClearValue,
	ID3D12ProtectedResourceSession *protectedSession, UINT32 numCastableFormats,
	const DXGI_FORMAT *castableFormats, REFIID riid, void **resource)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device10::CreateReservedResource2", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device10::CreateReservedResource2, DX12ResourceTrackerHookState::OrigCreateReservedResource2, "ID3D12Device10::CreateReservedResource2");
	if (!original)
		return E_FAIL;
	if (DX12InternalReplayState::IsActive())
		return original(device, desc, initialLayout,
			optimizedClearValue, protectedSession, numCastableFormats, castableFormats,
			riid, resource);
	HRESULT hr = E_FAIL;
	{
		DX12_CREATE_TIMING_SCOPE(ResourceReservedHook, Original);
		hr = original(device, desc, initialLayout,
			optimizedClearValue, protectedSession, numCastableFormats, castableFormats,
			riid, resource);
	}
	{
		DX12_CREATE_TIMING_SCOPE(ResourceReservedHook, Record);
		DX12ResourceTrackerHookRuntime::RecordCreatedResourceWithLayout(
			hr, resource, desc, nullptr, nullptr, initialLayout);
	}
	{
		DX12_CREATE_TIMING_SCOPE(ResourceReservedHook, Wrap);
		if (SUCCEEDED(hr) && resource && *resource)
			DX12Wrapper::WrapD3D12ResourceResult(riid, resource);
	}
	return hr;
}

void DX12ResourceTrackerHooks::Install(ID3D12Device *device)
{
	(void)device;

	if (!gCleanupRegistered) {
		atexit(CleanupTrackedResources);
		gCleanupRegistered = true;
		DX12ResourceCollectQueue::Start();
		DX12LogManager::Text("DX12 persistent resource and descriptor tracking enabled (wrapper mode)\n");
	}
}

// ---- DX12ResourceTrackerQueries.cpp ----
#line 1 "DX12ResourceTrackerQueries.cpp"
#include "DX12Infrastructure.hpp"

#include "DX12RuntimeInterfaces.hpp"
#include "DX12FeatureInterfaces.hpp"

#include <unordered_set>

bool DX12GetRootSignatureSummary(
	ID3D12RootSignature *rootSignature, DX12RootSignatureSummary *summary)
{
	if (!rootSignature || !summary)
		return false;

	AcquireSRWLockShared(&gDescriptorLock);
	std::unordered_map<ID3D12RootSignature*, size_t>::iterator found =
		gRootSignatureByPtr.find(rootSignature);
	if (found == gRootSignatureByPtr.end() ||
		found->second >= gRootSignatures.size()) {
		ReleaseSRWLockShared(&gDescriptorLock);
		return false;
	}
	const RootSignatureRecord &record = gRootSignatures[found->second];
	summary->rootSignature = record.rootSignature;
	summary->hash = record.hash;
	summary->size = record.size;
	summary->nodeMask = record.nodeMask;
	summary->version = record.version;
	summary->flags = record.flags;
	summary->staticSamplerCount = record.staticSamplerCount;
	summary->parsed = record.parsed;
	summary->parameters = record.parameters;
	ReleaseSRWLockShared(&gDescriptorLock);
	return true;
}

bool DX12GetPsoRootSignature(UINT64 psoIndex, ID3D12RootSignature **rootSignature)
{
	if (!psoIndex || !rootSignature)
		return false;

	*rootSignature = nullptr;
	AcquireSRWLockShared(&gDescriptorLock);
	std::unordered_map<UINT64, size_t>::iterator found =
		gPsoRootByIndex.find(psoIndex);
	if (found == gPsoRootByIndex.end() ||
		found->second >= gPsoRoots.size() ||
		!gPsoRoots[found->second].rootSignature) {
		ReleaseSRWLockShared(&gDescriptorLock);
		return false;
	}
	*rootSignature = gPsoRoots[found->second].rootSignature;
	ReleaseSRWLockShared(&gDescriptorLock);
	return true;
}

class DX12BufferResolveRangeUtils
{
public:
// Cache entries retain both the canonical resource pointer and its monotonic
// identity generation.  That pair is sufficient to prove that a resolved
// buffer still names the same live D3D12 resource: RecordResource changes the
// generation whenever its GPU-VA range changes, while InvalidateResourceLocked
// removes every alias before its slot can be reused.  Do this O(1) validation
// instead of replaying the global range-invalidation journal for every cache
// hit.  The journal may contain thousands of unrelated transient resources in
// one frame, which turned a local IA cache hit into an O(history) operation.
static bool ApplyLatestResourceStateLocked(DX12BufferResourceSummary *summary)
{
	if (!summary || !summary->resource || !summary->resourceGeneration)
		return false;

	std::unordered_map<ID3D12Resource*, size_t>::iterator found = gResourceByPtr.find(summary->resource);
	if (found == gResourceByPtr.end() || found->second >= gResources.size())
		return false;

	const ResourceRecord &resource = gResources[found->second];
	if (!resource.resource ||
		resource.identityGeneration != summary->resourceGeneration)
		return false;

	summary->currentState = static_cast<UINT>(resource.currentState);
	summary->hasCurrentState = resource.hasCurrentState;
	summary->stateTrackedByBarrier = resource.stateTrackedByBarrier;
	return true;
}

static bool FillSummaryFromCacheEntry(
	UINT64 gpuVirtualAddress, UINT64 size,
	const BufferResolveCacheEntry &entry,
	DX12BufferResourceSummary *summary)
{
	if (!summary || !entry.begin || gpuVirtualAddress < entry.begin || gpuVirtualAddress >= entry.end)
		return false;
	*summary = entry.summary;
	summary->resourceOffset = gpuVirtualAddress - entry.begin;
	const UINT64 available = entry.end - gpuVirtualAddress;
	summary->viewSize = size < available ? size : available;
	return DX12BufferResolveRangeUtils::ApplyLatestResourceStateLocked(summary);
}

static bool TryResolveSummaryFromResourceRecord(
	UINT64 gpuVirtualAddress, UINT64 size,
	const ResourceRecord &resource,
	DX12BufferResourceSummary *summary,
	UINT64 *resolvedBegin,
	UINT64 *resolvedEnd)
{
	if (!summary)
		return false;
	if (!resource.resource ||
		resource.desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER ||
		resource.gpuVirtualAddress == 0 ||
		resource.size == 0)
		return false;

	const UINT64 begin = resource.gpuVirtualAddress;
	const UINT64 realEnd = resource.gpuVirtualAddressRealEnd ?
		resource.gpuVirtualAddressRealEnd : begin + resource.size;
	if (gpuVirtualAddress < begin || gpuVirtualAddress >= realEnd)
		return false;

	summary->resource = resource.resource;
	summary->resourceGeneration = resource.identityGeneration;
	summary->resourceDesc = resource.desc;
	summary->hasResourceDesc = true;
	summary->gpuVirtualAddress = resource.gpuVirtualAddress;
	summary->resourceOffset = gpuVirtualAddress - begin;
	const UINT64 available = realEnd - gpuVirtualAddress;
	summary->viewSize = size < available ? size : available;
	if (resource.hasHeapType) {
		summary->resourceHeapType = static_cast<UINT>(resource.heapType);
		summary->hasResourceHeapType = true;
	}
	summary->currentState = static_cast<UINT>(resource.currentState);
	summary->hasCurrentState = resource.hasCurrentState;
	summary->stateTrackedByBarrier = resource.stateTrackedByBarrier;
	if (resolvedBegin)
		*resolvedBegin = begin;
	if (resolvedEnd)
		*resolvedEnd = realEnd;
	return true;
}

template <typename IteratorT>
static bool SearchBackwardForCoveringBuffer(
	IteratorT current,
	IteratorT beginIt,
	UINT64 gpuVirtualAddress,
	UINT64 size,
	DX12BufferResourceSummary *summary,
	UINT64 *resolvedBegin,
	UINT64 *resolvedEnd,
	UINT *probes)
{
	if (!summary)
		return false;

	for (IteratorT it = current;;) {
		if (probes)
			(*probes)++;
		if (it->second < gResources.size()) {
			const ResourceRecord &resource = gResources[it->second];
			if (DX12BufferResolveRangeUtils::TryResolveSummaryFromResourceRecord(
					gpuVirtualAddress, size, resource, summary, resolvedBegin, resolvedEnd))
				return true;
		}

		if (it == beginIt)
			break;
		--it;
	}

	return false;
}
};

bool DX12ResolveBufferResourceByGpuVa(
	UINT64 gpuVirtualAddress, UINT64 size, DX12BufferResourceSummary *summary)
{
	if (!summary || gpuVirtualAddress == 0)
		return false;

	*summary = DX12BufferResourceSummary();
	const UINT64 requestedEnd = gpuVirtualAddress + size;
	if (requestedEnd < gpuVirtualAddress)
		return false;

	struct LocalBufferResolveCache
	{
		size_t next = 0;
		std::array<BufferResolveCacheEntry, 32> entries = {};
	};
	static thread_local LocalBufferResolveCache localCache;
	for (size_t cacheIndex = 0;
			cacheIndex < localCache.entries.size();
			++cacheIndex) {
		const BufferResolveCacheEntry &entry = localCache.entries[cacheIndex];
		if (entry.begin && gpuVirtualAddress >= entry.begin && gpuVirtualAddress < entry.end) {
			AcquireSRWLockShared(&gResourceLock);
			const bool cacheResolved =
				DX12BufferResolveRangeUtils::FillSummaryFromCacheEntry(
					gpuVirtualAddress, size, entry, summary);
			ReleaseSRWLockShared(&gResourceLock);
			if (cacheResolved) {
				if (cacheIndex != 0)
					std::swap(
						localCache.entries[0],
						localCache.entries[cacheIndex]);
				gBufferResolveTlsHits++;
				return true;
			}
		}
	}

	bool resolved = false;
	UINT64 resolvedBegin = 0;
	UINT64 resolvedEnd = 0;
	UINT backwardProbes = 0;
	AcquireSRWLockShared(&gResourceLock);
	std::map<UINT64, BufferResolveCacheEntry>::iterator cached =
		gBufferResolveCache.upper_bound(gpuVirtualAddress);
	if (cached != gBufferResolveCache.begin()) {
		--cached;
		if (gpuVirtualAddress >= cached->second.begin &&
			gpuVirtualAddress < cached->second.end) {
			const bool cacheResolved =
				DX12BufferResolveRangeUtils::FillSummaryFromCacheEntry(
					gpuVirtualAddress, size, cached->second, summary);
			if (cacheResolved) {
				gBufferResolveGlobalHits++;
				localCache.entries[
					localCache.next++ % localCache.entries.size()] =
					cached->second;
				ReleaseSRWLockShared(&gResourceLock);
				return true;
			}
		}
	}

	std::map<UINT64, size_t>::iterator foundBuffer =
		gBufferResourceByGpuVa.upper_bound(gpuVirtualAddress);
	if (foundBuffer != gBufferResourceByGpuVa.begin()) {
		--foundBuffer;
		resolved = DX12BufferResolveRangeUtils::SearchBackwardForCoveringBuffer(
			foundBuffer,
			gBufferResourceByGpuVa.begin(),
			gpuVirtualAddress,
			size,
			summary,
			&resolvedBegin,
			&resolvedEnd,
			&backwardProbes);
	}
	ReleaseSRWLockShared(&gResourceLock);

	if (!resolved) {
		// Fallback: first try gBufferResourceByGpuVaComplete 鈥?a secondary index
		// that mirrors all buffer resources with valid GPU VA that were missed by
		// the primary gBufferResourceByGpuVa index (e.g. reserved resources with
		// delayed VA assignment, or resources created before hook activation).
		// This replaces the O(n) linear scan with O(log n) map lookup.
		AcquireSRWLockShared(&gResourceLock);
		std::map<UINT64, size_t>::iterator completeIt =
			gBufferResourceByGpuVaComplete.upper_bound(gpuVirtualAddress);
		if (completeIt != gBufferResourceByGpuVaComplete.begin()) {
			--completeIt;
			const ResourceRecord &completeResource = gResources[completeIt->second];
			if (DX12BufferResolveRangeUtils::TryResolveSummaryFromResourceRecord(
					gpuVirtualAddress, size, completeResource, summary,
					&resolvedBegin, &resolvedEnd)) {
				resolved = true;
				gBufferResolveFallbackScanHits++;
			}
		}
		// Safety net: full linear scan for truly unindexed resources.
		if (!resolved) {
			for (size_t i = 0; i < gResources.size(); ++i) {
				const ResourceRecord &resource = gResources[i];
				if (!resource.resource)
					continue;
				if (resource.desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER)
					continue;
				if (resource.gpuVirtualAddress == 0 || resource.size == 0)
					continue;
				if (DX12BufferResolveRangeUtils::TryResolveSummaryFromResourceRecord(
						gpuVirtualAddress, size, resource, summary,
						&resolvedBegin, &resolvedEnd)) {
					resolved = true;
					gBufferResolveFallbackScanHits++;
					DX12FrameAnalysis::LogJsonFunc("BufferResolveFallbackScanHit",
							"\"gpuVa\":\"0x%llX\",\"size\":%llu,\"begin\":\"0x%llX\",\"end\":\"0x%llX\",\"resource\":\"%p\",\"resourceSize\":%llu,\"heapType\":%u,\"hasHeapType\":%u",
						static_cast<unsigned long long>(gpuVirtualAddress),
						static_cast<unsigned long long>(size),
						static_cast<unsigned long long>(resolvedBegin),
						static_cast<unsigned long long>(resolvedEnd),
						resource.resource,
						static_cast<unsigned long long>(resource.size),
						resource.hasHeapType ? static_cast<UINT>(resource.heapType) : UINT_MAX,
						resource.hasHeapType ? 1 : 0);
					break;
				}
			}
		}
		ReleaseSRWLockShared(&gResourceLock);
	}
	if (resolved) {
		gBufferResolveBackwardScanHits++;
		BufferResolveCacheEntry entry;
		entry.begin = resolvedBegin;
		entry.end = resolvedEnd;
		entry.summary = *summary;
		entry.summary.resourceOffset = 0;
		entry.summary.viewSize = entry.end - entry.begin;
		AcquireSRWLockExclusive(&gResourceLock);
		entry.rangeEpoch = DX12BufferResolveCacheUtils::CurrentInvalidationEpochLocked();
		gBufferResolveCache[entry.begin] = entry;
		ReleaseSRWLockExclusive(&gResourceLock);
		localCache.entries[
			localCache.next++ % localCache.entries.size()] = entry;
		if (backwardProbes > 1) {
			DX12FrameAnalysis::LogJsonFunc("BufferResolveBackwardScanHit",
				"\"gpuVa\":\"0x%llX\",\"size\":%llu,\"begin\":\"0x%llX\",\"end\":\"0x%llX\",\"probes\":%u,\"resource\":\"%p\"",
				static_cast<unsigned long long>(gpuVirtualAddress),
				static_cast<unsigned long long>(size),
				static_cast<unsigned long long>(resolvedBegin),
				static_cast<unsigned long long>(resolvedEnd),
				backwardProbes,
				summary->resource);
		}
	}
	else {
		gBufferResolveMisses++;
	}
	return resolved;
}

static bool FillBufferSummaryFromRecordLocked(
	UINT64 gpuVirtualAddress, UINT64 size, DX12BufferResourceSummary *summary)
{
	if (!summary || gpuVirtualAddress == 0 || size == 0)
		return false;

	*summary = DX12BufferResourceSummary();
	const UINT64 requestedEnd = gpuVirtualAddress + size;
	if (requestedEnd < gpuVirtualAddress)
		return false;

	std::map<UINT64, size_t>::iterator foundBuffer =
		gBufferResourceByGpuVa.upper_bound(gpuVirtualAddress);
	if (foundBuffer == gBufferResourceByGpuVa.begin())
		return false;
	--foundBuffer;
	return DX12BufferResolveRangeUtils::SearchBackwardForCoveringBuffer(
		foundBuffer,
		gBufferResourceByGpuVa.begin(),
		gpuVirtualAddress,
		size,
		summary,
		nullptr,
		nullptr,
		nullptr);
}

class DX12BufferResolveDiagnosticsUtils
{
public:
static const char *ReasonLocked(UINT64 gpuVirtualAddress, UINT64 size,
	UINT64 *nearestBegin, UINT64 *nearestEnd, UINT64 *nearestResource)
{
	if (nearestBegin)
		*nearestBegin = 0;
	if (nearestEnd)
		*nearestEnd = 0;
	if (nearestResource)
		*nearestResource = 0;
	if (gpuVirtualAddress == 0)
		return "zero_gpu_va";

	const UINT64 requestedEnd = gpuVirtualAddress + size;
	if (requestedEnd < gpuVirtualAddress)
		return "range_overflow";

	std::map<UINT64, size_t>::iterator foundBuffer =
		gBufferResourceByGpuVa.upper_bound(gpuVirtualAddress);
	if (foundBuffer == gBufferResourceByGpuVa.begin())
		return "no_lower_buffer";
	for (--foundBuffer;; --foundBuffer) {
		if (foundBuffer->second >= gResources.size()) {
			if (foundBuffer == gBufferResourceByGpuVa.begin())
				return "index_invalid";
			continue;
		}

		const ResourceRecord &resource = gResources[foundBuffer->second];
		const UINT64 begin = resource.gpuVirtualAddress;
		const UINT64 realEnd = resource.gpuVirtualAddressRealEnd ?
			resource.gpuVirtualAddressRealEnd : begin + resource.size;
		const UINT64 end = resource.gpuVirtualAddressOobEnd ?
			resource.gpuVirtualAddressOobEnd : realEnd;
		if (nearestBegin)
			*nearestBegin = begin;
		if (nearestEnd)
			*nearestEnd = end;
		if (nearestResource)
			*nearestResource = reinterpret_cast<UINT64>(resource.resource);
		if (!resource.resource) {
			if (foundBuffer == gBufferResourceByGpuVa.begin())
				return "resource_null";
			continue;
		}
		if (resource.desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER) {
			if (foundBuffer == gBufferResourceByGpuVa.begin())
				return "not_buffer";
			continue;
		}
		if (begin == 0 || resource.size == 0) {
			if (foundBuffer == gBufferResourceByGpuVa.begin())
				return "empty_range";
			continue;
		}
		if (gpuVirtualAddress < begin) {
			if (foundBuffer == gBufferResourceByGpuVa.begin())
				return "before_range";
			continue;
		}
		if (gpuVirtualAddress < realEnd) {
			if (requestedEnd > realEnd && gpuVirtualAddress < end)
				return "after_real_range_oob_candidate";
			return "unknown_covering_buffer_found";
		}
		if (requestedEnd > end) {
			if (foundBuffer == gBufferResourceByGpuVa.begin())
				return "after_range";
			continue;
		}
		if (foundBuffer == gBufferResourceByGpuVa.begin())
			return "unknown";
	}
	return "unknown";
}
};

const char *DX12DiagnoseBufferResolveByGpuVa(
	UINT64 gpuVirtualAddress, UINT64 size,
	UINT64 *nearestBegin, UINT64 *nearestEnd, UINT64 *nearestResource)
{
	AcquireSRWLockShared(&gResourceLock);
	const char *reason = DX12BufferResolveDiagnosticsUtils::ReasonLocked(
		gpuVirtualAddress, size, nearestBegin, nearestEnd, nearestResource);
	ReleaseSRWLockShared(&gResourceLock);
	return reason;
}

uint32_t DX12HashBufferResourceView(
	const DX12BufferResourceSummary *summary, UINT64 fallbackGpuVirtualAddress,
	UINT64 fallbackSize)
{
	uint32_t hash = 0;
	const uint32_t tag = MakeTrackerFourCC('D', 'X', 'B', 'V');
	hash = HashBytes(hash, &tag, sizeof(tag));

	if (summary && summary->hasResourceDesc) {
		D3D12_RESOURCE_DESC desc = summary->resourceDesc;
		desc.Alignment = 0;
		hash = HashBytes(hash, &desc, sizeof(desc));
		if (summary->hasResourceHeapType)
			hash = HashBytes(hash, &summary->resourceHeapType, sizeof(summary->resourceHeapType));
	} else {
		hash = HashBytes(hash, &fallbackSize, sizeof(fallbackSize));
	}

	if (!hash)
		hash = HashBytes(0, &fallbackGpuVirtualAddress, sizeof(fallbackGpuVirtualAddress));
	return hash;
}

uint32_t DX12HashBufferView(
	const DX12BufferResourceSummary *summary, UINT64 fallbackGpuVirtualAddress,
	UINT64 fallbackSize, UINT stride, UINT format, UINT slot)
{
	uint32_t hash = DX12HashBufferResourceView(summary, fallbackGpuVirtualAddress, fallbackSize);
	const uint32_t tag = MakeTrackerFourCC('D', 'X', 'I', 'A');
	hash = HashBytes(hash, &tag, sizeof(tag));
	hash = HashBytes(hash, &fallbackGpuVirtualAddress, sizeof(fallbackGpuVirtualAddress));
	hash = HashBytes(hash, &fallbackSize, sizeof(fallbackSize));
	hash = HashBytes(hash, &stride, sizeof(stride));
	hash = HashBytes(hash, &format, sizeof(format));
	hash = HashBytes(hash, &slot, sizeof(slot));
	return hash;
}

// 鈹€鈹€ Buffer Content Cache 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
// Like DX11: cache the actual buffer bytes so IA hashes are based on
// CONTENT (CRC32 of data), not GPU virtual address.
// Keyed by GPU VA base of the resource so sub-regions resolve correctly.

struct CachedBufferContent
{
	std::vector<uint8_t> data;
	UINT64 gpuVaBase = 0;
	UINT64 dataSize = 0;
	UINT64 generation = 1;
};

static std::unordered_map<ID3D12Resource*, CachedBufferContent> gBufferContentByResource;
static SRWLOCK gBufferContentLock = SRWLOCK_INIT;
static std::atomic<UINT64> gBufferContentGeneration{1};

bool DX12CacheBufferContent(ID3D12Resource *resource, const void *data, size_t dataSize)
{
	return DX12CacheBufferContentRange(resource, 0, data, dataSize);
}

bool DX12CacheBufferContentRange(
	ID3D12Resource *resource, UINT64 resourceOffset, const void *data, size_t dataSize)
{
	if (!resource || !data || !dataSize)
		return false;

	CachedBufferContent entry;
	entry.data.assign(static_cast<const uint8_t*>(data),
		static_cast<const uint8_t*>(data) + dataSize);
	entry.dataSize = dataSize;

	// Get the GPU VA base directly (available immediately after creation,
	// unlike GPU VA resolution which may not be ready if tracking is async).
	entry.gpuVaBase = resource->GetGPUVirtualAddress() + resourceOffset;
	entry.generation =
		gBufferContentGeneration.fetch_add(1, std::memory_order_relaxed) + 1;

	AcquireSRWLockExclusive(&gBufferContentLock);
	gBufferContentByResource[resource] = std::move(entry);
	ReleaseSRWLockExclusive(&gBufferContentLock);

#if defined(_DEBUG)
	DX12LogManager::DebugJsonFunc("DX12BufferContentCache",
		"stage=stored,resource=0x%p,offset=%llu,bytes=%zu,gpuVaBase=0x%llx",
		resource,
		static_cast<unsigned long long>(resourceOffset),
		dataSize,
		static_cast<unsigned long long>(entry.gpuVaBase));
#endif
	return true;
}

UINT64 DX12GetBufferContentGenerationForResource(ID3D12Resource *resource)
{
	if (!resource)
		return gBufferContentGeneration.load(std::memory_order_relaxed);
	AcquireSRWLockShared(&gBufferContentLock);
	auto found = gBufferContentByResource.find(resource);
	const UINT64 generation = found == gBufferContentByResource.end() ?
		1 : found->second.generation;
	ReleaseSRWLockShared(&gBufferContentLock);
	return generation;
}

void DX12InvalidateBufferContent(ID3D12Resource *resource)
{
	if (!resource)
		return;
	AcquireSRWLockExclusive(&gBufferContentLock);
	gBufferContentByResource.erase(resource);
	ReleaseSRWLockExclusive(&gBufferContentLock);
}
bool DX12TryGetBufferContent(UINT64 gpuVirtualAddress, UINT64 size,
	const uint8_t **outData, size_t *outDataSize)
{
	if (!outData || !outDataSize || !gpuVirtualAddress || !size)
		return false;
	*outData = nullptr;
	*outDataSize = 0;

	// Resolve GPU VA to find the owning resource
	DX12BufferResourceSummary summary = {};
	if (!DX12ResolveBufferResourceByGpuVa(gpuVirtualAddress, size, &summary))
		return false;
	if (!summary.resource)
		return false;

	AcquireSRWLockShared(&gBufferContentLock);
	std::unordered_map<ID3D12Resource*, CachedBufferContent>::iterator it =
		gBufferContentByResource.find(summary.resource);
	if (it == gBufferContentByResource.end()) {
		ReleaseSRWLockShared(&gBufferContentLock);
		return false;
	}

	const CachedBufferContent &cached = it->second;
	if (gpuVirtualAddress < cached.gpuVaBase) {
		ReleaseSRWLockShared(&gBufferContentLock);
		return false;
	}
	const UINT64 offset = gpuVirtualAddress - cached.gpuVaBase;
	if (offset + size > cached.dataSize) {
		ReleaseSRWLockShared(&gBufferContentLock);
		return false;
	}

	*outData = cached.data.data() + offset;
	*outDataSize = static_cast<size_t>(size);
	ReleaseSRWLockShared(&gBufferContentLock);
	return true;
}

struct CachedTextureHash
{
	uint32_t hash = 0;
	D3D12_RESOURCE_DESC desc = {};
	UINT64 generation = 0;
};

static std::unordered_map<ID3D12Resource*, CachedTextureHash> gTextureHashByResource;
static SRWLOCK gTextureHashLock = SRWLOCK_INIT;
static std::atomic<UINT64> gTextureHashGeneration{1};

bool DX12CacheTextureHash(
	ID3D12Resource *resource, uint32_t hash, const D3D12_RESOURCE_DESC *desc, const char *source)
{
	if (!resource || !hash)
		return false;

	CachedTextureHash entry;
	entry.hash = hash;
	if (desc)
		entry.desc = *desc;
	entry.generation = gTextureHashGeneration.fetch_add(1, std::memory_order_relaxed) + 1;

	AcquireSRWLockExclusive(&gTextureHashLock);
	gTextureHashByResource[resource] = entry;
	ReleaseSRWLockExclusive(&gTextureHashLock);

	DX12LogManager::DebugJsonFunc("DX12TextureHashCache",
		"\"stage\":\"stored\",\"source\":\"%s\",\"resource\":\"%p\",\"hash\":\"%08x\",\"generation\":%llu,\"width\":%llu,\"height\":%u,\"format\":%u",
		source ? source : "",
		resource,
		hash,
		static_cast<unsigned long long>(entry.generation),
		desc ? static_cast<unsigned long long>(desc->Width) : 0ull,
		desc ? desc->Height : 0u,
		desc ? static_cast<UINT>(desc->Format) : 0u);
	return true;
}

bool DX12TryGetTextureHash(ID3D12Resource *resource, uint32_t *hash)
{
	if (!hash)
		return false;
	*hash = 0;
	if (!resource)
		return false;

	AcquireSRWLockShared(&gTextureHashLock);
	std::unordered_map<ID3D12Resource*, CachedTextureHash>::iterator it =
		gTextureHashByResource.find(resource);
	if (it == gTextureHashByResource.end()) {
		ReleaseSRWLockShared(&gTextureHashLock);
		return false;
	}
	*hash = it->second.hash;
	ReleaseSRWLockShared(&gTextureHashLock);
	return *hash != 0;
}

void DX12InvalidateTextureHash(ID3D12Resource *resource)
{
	if (!resource)
		return;
	AcquireSRWLockExclusive(&gTextureHashLock);
	const size_t erased = gTextureHashByResource.erase(resource);
	ReleaseSRWLockExclusive(&gTextureHashLock);
	if (erased) {
		DX12LogManager::DebugJsonFunc("DX12TextureHashCache",
			"\"stage\":\"invalidated\",\"resource\":\"%p\",\"erased\":%zu",
			resource,
			erased);
	}
}

static bool DX12CopyRegionCoversTopMip(
	const D3D12_RESOURCE_DESC &desc,
	UINT dstX, UINT dstY, UINT dstZ,
	const D3D12_PLACED_SUBRESOURCE_FOOTPRINT &footprint,
	const D3D12_BOX *srcBox)
{
	if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D)
		return false;
	if (desc.Width > UINT_MAX || desc.Height == 0)
		return false;
	if (dstX != 0 || dstY != 0 || dstZ != 0)
		return false;
	if (footprint.Footprint.Width < desc.Width ||
		footprint.Footprint.Height < desc.Height)
		return false;
	if (!srcBox)
		return true;
	if (srcBox->left != 0 || srcBox->top != 0 || srcBox->front != 0)
		return false;
	if (srcBox->right < desc.Width || srcBox->bottom < desc.Height || srcBox->back < 1)
		return false;
	return true;
}

bool DX12TrackTextureHashFromCopyTextureRegion(
	const D3D12_TEXTURE_COPY_LOCATION *dst, UINT dstX, UINT dstY, UINT dstZ,
	const D3D12_TEXTURE_COPY_LOCATION *src, const D3D12_BOX *srcBox)
{
	if (!dst || !src || !dst->pResource || !src->pResource)
		return false;
	if (dst->Type != D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX ||
		dst->SubresourceIndex != 0 ||
		src->Type != D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT)
		return false;

	const D3D12_RESOURCE_DESC desc = dst->pResource->GetDesc();
	const D3D12_PLACED_SUBRESOURCE_FOOTPRINT &footprint = src->PlacedFootprint;
	if (!DX12CopyRegionCoversTopMip(desc, dstX, dstY, dstZ, footprint, srcBox))
		return false;

	const UINT rowCount = DX12TextureHashUtils::Texture2DTopMipRowCount(
		desc.Format, desc.Height);
	const UINT64 rowBytes = DX12TextureHashUtils::Texture2DTopMipRowBytes(
		desc.Format, static_cast<UINT>(desc.Width));
	if (!rowCount || !rowBytes || footprint.Footprint.RowPitch < rowBytes)
		return false;

	const UINT64 requiredBytes =
		static_cast<UINT64>(footprint.Footprint.RowPitch) * (rowCount - 1) + rowBytes;
	const UINT64 sourceGpuVa = src->pResource->GetGPUVirtualAddress() + footprint.Offset;
	const uint8_t *content = nullptr;
	size_t contentSize = 0;
	if (!DX12TryGetBufferContent(sourceGpuVa, requiredBytes, &content, &contentSize)) {
		DX12LogManager::DebugJsonFunc("DX12TextureHashCache",
			"\"stage\":\"copy_upload_miss\",\"dst\":\"%p\",\"src\":\"%p\",\"gpu\":\"0x%llx\",\"bytes\":%llu,\"width\":%llu,\"height\":%u,\"format\":%u",
			dst->pResource,
			src->pResource,
			static_cast<unsigned long long>(sourceGpuVa),
			static_cast<unsigned long long>(requiredBytes),
			static_cast<unsigned long long>(desc.Width),
			desc.Height,
			static_cast<UINT>(desc.Format));
		return false;
	}

	uint32_t hash = 0;
	if (!DX12TextureHashUtils::Texture2DTopMipHashFromRows(
			desc,
			content,
			footprint.Footprint.RowPitch,
			rowBytes,
			rowCount,
			&hash)) {
		return false;
	}
	return DX12CacheTextureHash(dst->pResource, hash, &desc, "copy_texture_region");
}
UINT64 DX12GetIaHashContentGeneration()
{
	return gBufferContentGeneration.load(std::memory_order_relaxed);
}

UINT64 DX12GetIaHashResolveGeneration()
{
	return gBufferResolveInvalidationEpochAtomic.load(std::memory_order_relaxed);
}

bool DX12BufferResolveValidationUtils::IsRangeStillValid(
	UINT64 observedEpoch, UINT64 gpuVirtualAddress, UINT64 size)
{
	if (!gpuVirtualAddress || !size)
		return false;
	const UINT64 currentEpoch = DX12GetIaHashResolveGeneration();
	if (currentEpoch == observedEpoch)
		return true;
	const UINT64 available = UINT64_MAX - gpuVirtualAddress;
	const UINT64 rangeSize = (std::min)(size, available);
	if (!rangeSize)
		return false;
	AcquireSRWLockShared(&gResourceLock);
	const bool invalidated = DX12BufferResolveCacheUtils::RangeInvalidatedSinceLocked(
		gpuVirtualAddress,
		gpuVirtualAddress + rangeSize,
		observedEpoch);
	ReleaseSRWLockShared(&gResourceLock);
	return !invalidated;
}

uint32_t DX12HashIaBufferView(
	UINT64 gpuVirtualAddress, UINT64 size, UINT stride, UINT format, UINT slot)
{
	if (!gpuVirtualAddress || !size)
		return 0;

	// 鈹€鈹€ DX11 瀵归綈: 鍙岀骇 TLS 缂撳瓨 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
	// L1: gpuVa-based key 鈥?蹇€熸崟鎹夋椂闂村眬閮ㄦ€э紙鍚屼竴 gpuVa 閲嶅鍑虹幇锛?
	// L2: resource-based key 鈥?绋冲畾鎹曟崏 buffer 绾у鐢紙鍚屼竴 buffer 涓嶅悓 offset锛?
	//     瀵归綈 DX11 鐨?(ID3D11Buffer*, offset, size) 妯″紡銆?
	// Resource-generation invalidation:
	// - GPUVA index/resource lifetime changes bump gBufferResolveGeneration.
	// - CPU-upload content changes bump gBufferContentGeneration.
	// This keeps IA region hashes reusable across frames without keeping stale
	// content-derived hashes after upload buffers are rewritten.
	struct IaHashCacheValue {
		uint32_t hash = 0;
		UINT64 resolveGeneration = 0;
		UINT64 contentGeneration = 0;
		UINT64 observedGlobalContentGeneration = 0;
		UINT64 gpuVa = 0;
		UINT64 size = 0;
		ID3D12Resource *resource = nullptr;
	};

	// -- L1 key (gpuVa-based) --
	struct IaHashKeyL1 {
		UINT64 gpuVa; UINT64 sz; UINT str; UINT fmt; UINT sl;
		bool operator==(const IaHashKeyL1 &o) const {
			return gpuVa == o.gpuVa && sz == o.sz && str == o.str && fmt == o.fmt && sl == o.sl;
		}
	};
	struct IaHashKeyL1Hasher {
		size_t operator()(const IaHashKeyL1 &k) const {
			return static_cast<size_t>(k.gpuVa ^ (k.gpuVa >> 32) ^ k.sz ^ (k.sz >> 32) ^
				(static_cast<UINT64>(k.str) << 20) ^ (static_cast<UINT64>(k.fmt) << 28) ^
				(static_cast<UINT64>(k.sl) << 36));
		}
	};

	// -- L2 key (resource-based, aligns with DX11) --
	struct IaHashKeyL2 {
		void *resource; UINT64 offset; UINT64 viewSz; UINT str; UINT fmt; UINT sl;
		bool operator==(const IaHashKeyL2 &o) const {
			return resource == o.resource && offset == o.offset && viewSz == o.viewSz &&
				str == o.str && fmt == o.fmt && sl == o.sl;
		}
	};
	struct IaHashKeyL2Hasher {
		size_t operator()(const IaHashKeyL2 &k) const {
			UINT64 h = reinterpret_cast<UINT64>(k.resource);
			return static_cast<size_t>(h ^ (h >> 32) ^ k.offset ^ (k.offset >> 32) ^
				k.viewSz ^ (k.viewSz >> 32) ^
				(static_cast<UINT64>(k.str) << 20) ^ (static_cast<UINT64>(k.fmt) << 28) ^
				(static_cast<UINT64>(k.sl) << 36));
		}
	};

	static thread_local std::unordered_map<IaHashKeyL1, IaHashCacheValue, IaHashKeyL1Hasher> s_cacheL1;
	static thread_local std::unordered_map<IaHashKeyL2, IaHashCacheValue, IaHashKeyL2Hasher> s_cacheL2;
	const UINT64 resolveGeneration = gBufferResolveInvalidationEpochAtomic.load(std::memory_order_relaxed);
	const UINT64 globalContentGeneration =
		gBufferContentGeneration.load(std::memory_order_relaxed);

	// -- L1 lookup (gpuVa-based, fastest) --
	IaHashKeyL1 keyL1 = { gpuVirtualAddress, size, stride, format, slot };
	std::unordered_map<IaHashKeyL1, IaHashCacheValue, IaHashKeyL1Hasher>::iterator itL1 = s_cacheL1.find(keyL1);
	if (itL1 != s_cacheL1.end()) {
		IaHashCacheValue &cacheValue = itL1->second;
		bool invalidated = true;
		const UINT64 currentContentGeneration =
			cacheValue.observedGlobalContentGeneration == globalContentGeneration ?
				cacheValue.contentGeneration :
				(cacheValue.resource ?
					DX12GetBufferContentGenerationForResource(cacheValue.resource) :
					globalContentGeneration);
		if (cacheValue.contentGeneration == currentContentGeneration) {
			cacheValue.observedGlobalContentGeneration = globalContentGeneration;
			if (cacheValue.resolveGeneration == resolveGeneration) {
				invalidated = false;
			} else {
				AcquireSRWLockShared(&gResourceLock);
				invalidated = DX12BufferResolveCacheUtils::RangeInvalidatedSinceLocked(
					cacheValue.gpuVa, cacheValue.gpuVa + cacheValue.size, cacheValue.resolveGeneration);
				ReleaseSRWLockShared(&gResourceLock);
				if (!invalidated)
					cacheValue.resolveGeneration = resolveGeneration;
			}
		}
		if (!invalidated)
			return cacheValue.hash;
		s_cacheL1.erase(itL1);
	}

	// -- L2 lookup (resource-based, stable across draws) --
	// 瀵归綈 DX11: 鐢?(resource, offset, viewSize) 鏇夸唬 gpuVa
	DX12BufferResourceSummary summary = {};
	const bool resolved = DX12ResolveBufferResourceByGpuVa(gpuVirtualAddress, size, &summary);
	if (resolved && summary.resource) {
		IaHashKeyL2 keyL2 = { summary.resource,
			summary.resourceOffset, summary.viewSize,
			stride, format, slot };
		std::unordered_map<IaHashKeyL2, IaHashCacheValue, IaHashKeyL2Hasher>::iterator itL2 = s_cacheL2.find(keyL2);
		if (itL2 != s_cacheL2.end()) {
			// L2 鍛戒腑 鈫?鎻愬崌鍒?L1锛堜笅娆＄浉鍚?gpuVa 鐩存帴鍛戒腑锛?
			IaHashCacheValue &cacheValue = itL2->second;
			bool invalidated = true;
			const UINT64 currentContentGeneration =
				cacheValue.observedGlobalContentGeneration == globalContentGeneration ?
					cacheValue.contentGeneration :
					(cacheValue.resource ?
						DX12GetBufferContentGenerationForResource(cacheValue.resource) :
						globalContentGeneration);
			if (cacheValue.contentGeneration == currentContentGeneration) {
				cacheValue.observedGlobalContentGeneration = globalContentGeneration;
				if (cacheValue.resolveGeneration == resolveGeneration) {
					invalidated = false;
				} else {
					AcquireSRWLockShared(&gResourceLock);
					invalidated = DX12BufferResolveCacheUtils::RangeInvalidatedSinceLocked(
						cacheValue.gpuVa, cacheValue.gpuVa + cacheValue.size, cacheValue.resolveGeneration);
					ReleaseSRWLockShared(&gResourceLock);
					if (!invalidated)
						cacheValue.resolveGeneration = resolveGeneration;
				}
			}
			if (!invalidated) {
				s_cacheL1[keyL1] = cacheValue;
				return cacheValue.hash;
			}
			s_cacheL2.erase(itL2);
		}
	}

	// -- 涓ょ骇缂撳瓨閮?miss 鈫?璁＄畻 hash --
	uint32_t hash = 0;
	const UINT64 contentGeneration = resolved && summary.resource ?
		DX12GetBufferContentGenerationForResource(summary.resource) :
		globalContentGeneration;

	// 鈹€鈹€ Tier 1: Content-based CRC32 (matches DX11 behavior) 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
	const uint8_t *contentData = nullptr;
	size_t contentSize = 0;
	if (DX12TryGetBufferContent(gpuVirtualAddress, size, &contentData, &contentSize))
	{
		const uint32_t tag = MakeTrackerFourCC('D', 'X', 'I', 'C');
		hash = HashBytes(hash, &tag, sizeof(tag));
		hash = crc32c_append(hash, contentData, contentSize);
		hash = HashBytes(hash, &stride, sizeof(stride));
		hash = HashBytes(hash, &format, sizeof(format));
		hash = HashBytes(hash, &slot, sizeof(slot));
		IaHashCacheValue contentCacheValue;
		contentCacheValue.hash = hash;
		contentCacheValue.resolveGeneration = resolveGeneration;
		contentCacheValue.contentGeneration = contentGeneration;
		contentCacheValue.observedGlobalContentGeneration = globalContentGeneration;
		contentCacheValue.gpuVa = gpuVirtualAddress;
		contentCacheValue.size = size;
		contentCacheValue.resource = resolved ? summary.resource : nullptr;
		s_cacheL1[keyL1] = contentCacheValue;
		if (resolved && summary.resource) {
			IaHashKeyL2 keyL2 = { summary.resource,
				summary.resourceOffset, summary.viewSize,
				stride, format, slot };
			s_cacheL2[keyL2] = contentCacheValue;
		}
		if (s_cacheL1.size() > 8192 || s_cacheL2.size() > 8192) {
			s_cacheL1.clear();
			s_cacheL2.clear();
		}
#if defined(_DEBUG)
		DX12LogManager::DebugJsonFunc("DX12IaHashResolve",
			"stage=content_crc32,gpuVa=0x%llx,size=%llu,"
			"stride=%u,format=%u,slot=%u,hash=%08x",
			static_cast<unsigned long long>(gpuVirtualAddress),
			static_cast<unsigned long long>(size),
			stride, format, slot, hash);
#endif
		return hash;
	}

	// 鈹€鈹€ Tier 2/3: Metadata-based hash 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
	const uint32_t tag = MakeTrackerFourCC('D', 'X', 'I', 'A');
	hash = HashBytes(hash, &tag, sizeof(tag));

	if (resolved && summary.hasResourceDesc) {
		const D3D12_RESOURCE_DESC &desc = summary.resourceDesc;
		hash = HashBytes(hash, &desc.Dimension, sizeof(desc.Dimension));
		hash = HashBytes(hash, &desc.Width, sizeof(desc.Width));
		hash = HashBytes(hash, &desc.Height, sizeof(desc.Height));
		hash = HashBytes(hash, &desc.DepthOrArraySize, sizeof(desc.DepthOrArraySize));
		hash = HashBytes(hash, &desc.MipLevels, sizeof(desc.MipLevels));
		hash = HashBytes(hash, &desc.Format, sizeof(desc.Format));
		hash = HashBytes(hash, &desc.SampleDesc.Count, sizeof(desc.SampleDesc.Count));
		hash = HashBytes(hash, &desc.SampleDesc.Quality, sizeof(desc.SampleDesc.Quality));
		hash = HashBytes(hash, &desc.Layout, sizeof(desc.Layout));
		hash = HashBytes(hash, &desc.Flags, sizeof(desc.Flags));
		hash = HashBytes(hash, &summary.resourceHeapType, sizeof(summary.resourceHeapType));
		hash = HashBytes(hash, &summary.resourceOffset, sizeof(summary.resourceOffset));
		hash = HashBytes(hash, &summary.viewSize, sizeof(summary.viewSize));
#if defined(_DEBUG)
		DX12LogManager::DebugJsonFunc("DX12IaHashResolve",
			"stage=resolved,gpuVa=0x%llx,size=%llu,"
			"offset=%llu,viewSize=%llu,stride=%u,format=%u,"
			"slot=%u,hash=%08x",
			static_cast<unsigned long long>(gpuVirtualAddress),
			static_cast<unsigned long long>(size),
			static_cast<unsigned long long>(summary.resourceOffset),
			static_cast<unsigned long long>(summary.viewSize),
			stride, format, slot, hash);
#endif
	} else {
		hash = HashBytes(hash, &size, sizeof(size));
#if defined(_DEBUG)
		DX12LogManager::DebugJsonFunc("DX12IaHashResolve",
			"stage=fallback_no_gpuva,gpuVa=0x%llx,"
			"size=%llu,stride=%u,format=%u,slot=%u,"
			"hash=%08x",
			static_cast<unsigned long long>(gpuVirtualAddress),
			static_cast<unsigned long long>(size),
			stride, format, slot, hash);
#endif
	}

	hash = HashBytes(hash, &stride, sizeof(stride));
	hash = HashBytes(hash, &format, sizeof(format));
	hash = HashBytes(hash, &slot, sizeof(slot));

	// 鍐欏叆鍙岀骇缂撳瓨
	IaHashCacheValue cacheValue;
	cacheValue.hash = hash;
	cacheValue.resolveGeneration = resolveGeneration;
	cacheValue.contentGeneration = contentGeneration;
	cacheValue.observedGlobalContentGeneration = globalContentGeneration;
	cacheValue.gpuVa = gpuVirtualAddress;
	cacheValue.size = size;
	cacheValue.resource = resolved ? summary.resource : nullptr;
	s_cacheL1[keyL1] = cacheValue;
	if (resolved && summary.resource) {
		IaHashKeyL2 keyL2 = { summary.resource,
			summary.resourceOffset, summary.viewSize,
			stride, format, slot };
		s_cacheL2[keyL2] = cacheValue;
	}
	if (s_cacheL1.size() > 8192 || s_cacheL2.size() > 8192) {
		s_cacheL1.clear();
		s_cacheL2.clear();
	}
	return hash;
}

uint32_t DX12HashDescriptorBufferView(
	const DX12DescriptorSummary *descriptor, UINT64 fallbackGpuVirtualAddress,
	UINT64 fallbackSize)
{
	(void)fallbackGpuVirtualAddress;
	uint32_t hash = 0;
	const uint32_t tag = MakeTrackerFourCC('D', 'X', 'D', 'V');
	hash = HashBytes(hash, &tag, sizeof(tag));
	if (descriptor) {
		hash = HashBytes(hash, descriptor->kind.data(), descriptor->kind.size());
		hash = HashBytes(hash, &descriptor->viewFormat, sizeof(descriptor->viewFormat));
		hash = HashBytes(hash, &descriptor->viewDimension, sizeof(descriptor->viewDimension));
		hash = HashBytes(hash, &descriptor->firstElement, sizeof(descriptor->firstElement));
		hash = HashBytes(hash, &descriptor->numElements, sizeof(descriptor->numElements));
		hash = HashBytes(hash, &descriptor->structureByteStride, sizeof(descriptor->structureByteStride));
		hash = HashBytes(hash, &descriptor->bufferViewOffset, sizeof(descriptor->bufferViewOffset));
		hash = HashBytes(hash, &descriptor->bufferViewBytes, sizeof(descriptor->bufferViewBytes));
		const UINT64 stableViewSize = descriptor->viewSize ?
			descriptor->viewSize : fallbackSize;
		hash = HashBytes(hash, &stableViewSize, sizeof(stableViewSize));
	} else {
		hash = HashBytes(hash, &fallbackSize, sizeof(fallbackSize));
	}
	return hash;
}

static void CompleteDescriptorSummaryResourceInfo(
	DX12DescriptorSummary *summary, bool resourceLockHeld)
{
	if (!summary || !summary->resource)
		return;

	if (!resourceLockHeld)
		AcquireSRWLockShared(&gResourceLock);
	std::unordered_map<ID3D12Resource*, size_t>::iterator found =
		gResourceByPtr.find(summary->resource);
	if (found == gResourceByPtr.end() || found->second >= gResources.size()) {
		if (!resourceLockHeld)
			ReleaseSRWLockShared(&gResourceLock);
		return;
	}
	const ResourceRecord &resource = gResources[found->second];
	if (!summary->hasResourceDesc) {
		summary->resourceDesc = resource.desc;
		summary->hasResourceDesc = true;
		if (summary->resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER &&
			summary->gpuVirtualAddress == 0)
			summary->gpuVirtualAddress = resource.gpuVirtualAddress;
	}
	if (!summary->hasResourceHeapType && resource.hasHeapType) {
		summary->resourceHeapType = static_cast<UINT>(resource.heapType);
		summary->hasResourceHeapType = true;
	}
	if (!summary->hasCurrentState) {
		summary->currentState = static_cast<UINT>(resource.currentState);
		summary->hasCurrentState = resource.hasCurrentState;
		summary->stateTrackedByBarrier = resource.stateTrackedByBarrier;
	} else if (!summary->stateTrackedByBarrier && resource.stateTrackedByBarrier) {
		summary->currentState = static_cast<UINT>(resource.currentState);
		summary->hasCurrentState = resource.hasCurrentState;
		summary->stateTrackedByBarrier = true;
	}
	if (!resourceLockHeld)
		ReleaseSRWLockShared(&gResourceLock);
}

static bool CompleteDescriptorSummaryBufferGpuVaInfo(
	DX12DescriptorSummary *summary, UINT64 gpuVirtualAddress, UINT64 size,
	bool resourceLockHeld)
{
	if (!summary || summary->resource || gpuVirtualAddress == 0 || size == 0)
		return false;

	DX12BufferResourceSummary resource;
	const bool resolved = resourceLockHeld ?
		FillBufferSummaryFromRecordLocked(gpuVirtualAddress, size, &resource) :
		DX12ResolveBufferResourceByGpuVa(gpuVirtualAddress, size, &resource);
	if (!resolved || !resource.resource || !resource.hasResourceDesc) {
		// CBV sizes are often aligned or stale relative to the currently tracked
		// resource extent. Resolve by address first, then clamp the view to the
		// known buffer range so hunting metadata can still dump the bytes that
		// actually exist.
		const bool addressResolved = resourceLockHeld ?
			FillBufferSummaryFromRecordLocked(gpuVirtualAddress, 1, &resource) :
			DX12ResolveBufferResourceByGpuVa(gpuVirtualAddress, 1, &resource);
		if (!addressResolved || !resource.resource || !resource.hasResourceDesc)
			return false;
		if (resource.resourceDesc.Width > resource.resourceOffset) {
			const UINT64 available = resource.resourceDesc.Width - resource.resourceOffset;
			resource.viewSize = size < available ? size : available;
		}
	}

	summary->resource = resource.resource;
	summary->resourceDesc = resource.resourceDesc;
	summary->hasResourceDesc = resource.hasResourceDesc;
	summary->resourceHeapType = resource.resourceHeapType;
	summary->hasResourceHeapType = resource.hasResourceHeapType;
	summary->gpuVirtualAddress = gpuVirtualAddress;
	summary->resourceOffset = resource.resourceOffset;
	summary->viewSize = resource.viewSize ? resource.viewSize : size;
	summary->currentState = resource.currentState;
	summary->hasCurrentState = resource.hasCurrentState;
	summary->stateTrackedByBarrier = resource.stateTrackedByBarrier;
	return true;
}

static void FillDescriptorSummary(
	DX12DescriptorSummary *summary, const DescriptorRecord &record,
	bool completeResourceInfo = true, bool resourceLockHeld = false,
	bool completeBufferGpuVaInfo = true)
{
	if (!summary)
		return;

	summary->kind = record.kind;
	summary->cpuHandle = record.cpuHandle;
	summary->resource = record.resource;
	summary->counterResource = record.counterResource;
	summary->resourceDesc = record.resourceDesc;
	summary->hasResourceDesc = record.hasResourceDesc;
	summary->resourceHeapType = record.resourceHeapType;
	summary->hasResourceHeapType = record.hasResourceHeapType;
	summary->gpuVirtualAddress = record.gpuVirtualAddress;
	summary->resourceOffset = record.resourceOffset;
	summary->viewSize = record.viewSize;
	summary->currentState = static_cast<UINT>(record.currentState);
	summary->hasCurrentState = record.hasCurrentState;
	summary->stateTrackedByBarrier = record.stateTrackedByBarrier;
	summary->hasDesc = record.hasDesc;
	if (completeResourceInfo)
		CompleteDescriptorSummaryResourceInfo(summary, resourceLockHeld);

	if (record.kindId == DescriptorKind::CBV_ID) {
		summary->cbvSize = record.cbv.SizeInBytes;
		summary->bufferViewOffset = summary->resourceOffset;
		summary->bufferViewBytes = summary->viewSize ? summary->viewSize : record.cbv.SizeInBytes;
		const UINT64 cbvGpuVa = record.cbv.BufferLocation ?
			record.cbv.BufferLocation : record.gpuVirtualAddress;
		const UINT64 cbvBytes = record.cbv.SizeInBytes ?
			record.cbv.SizeInBytes : record.viewSize;
		if (completeBufferGpuVaInfo && !summary->resource && cbvGpuVa && cbvBytes) {
			CompleteDescriptorSummaryBufferGpuVaInfo(
				summary, cbvGpuVa, cbvBytes, resourceLockHeld);
			summary->bufferViewOffset = summary->resourceOffset;
			summary->bufferViewBytes = summary->viewSize;
		}
	} else if (record.kindId == DescriptorKind::SRV_ID && record.hasDesc) {
		summary->srvDesc = record.srv;
		summary->hasSrvDesc = true;
		summary->viewFormat = static_cast<UINT>(record.srv.Format);
		summary->viewDimension = static_cast<UINT>(record.srv.ViewDimension);
		if (record.srv.ViewDimension == D3D12_SRV_DIMENSION_BUFFER) {
			summary->firstElement = record.srv.Buffer.FirstElement;
			summary->numElements = record.srv.Buffer.NumElements;
			summary->structureByteStride = record.srv.Buffer.StructureByteStride;
			summary->bufferViewOffset = record.resourceOffset;
			summary->bufferViewBytes = record.viewSize;
			if (summary->bufferViewBytes == 0 &&
					summary->hasResourceDesc &&
					summary->resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER &&
					summary->resourceDesc.Width > summary->bufferViewOffset) {
				summary->bufferViewBytes =
					summary->resourceDesc.Width - summary->bufferViewOffset;
				if (summary->viewSize == 0)
					summary->viewSize = summary->bufferViewBytes;
			}
		}
	} else if (record.kindId == DescriptorKind::UAV_ID && record.hasDesc) {
		summary->viewFormat = static_cast<UINT>(record.uav.Format);
		summary->viewDimension = static_cast<UINT>(record.uav.ViewDimension);
		if (record.uav.ViewDimension == D3D12_UAV_DIMENSION_BUFFER) {
			summary->firstElement = record.uav.Buffer.FirstElement;
			summary->numElements = record.uav.Buffer.NumElements;
			summary->structureByteStride = record.uav.Buffer.StructureByteStride;
			summary->bufferViewOffset = record.resourceOffset;
			summary->bufferViewBytes = record.viewSize;
			if (summary->bufferViewBytes == 0 &&
					summary->hasResourceDesc &&
					summary->resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER &&
					summary->resourceDesc.Width > summary->bufferViewOffset) {
				summary->bufferViewBytes =
					summary->resourceDesc.Width - summary->bufferViewOffset;
				if (summary->viewSize == 0)
					summary->viewSize = summary->bufferViewBytes;
			}
		}
	} else if (record.kindId == DescriptorKind::RTV_ID && record.hasDesc) {
		summary->viewFormat = static_cast<UINT>(record.rtv.Format);
		summary->viewDimension = static_cast<UINT>(record.rtv.ViewDimension);
	} else if (record.kindId == DescriptorKind::DSV_ID && record.hasDesc) {
		summary->viewFormat = static_cast<UINT>(record.dsv.Format);
		summary->viewDimension = static_cast<UINT>(record.dsv.ViewDimension);
	}
}

struct MetadataSnapshotCache
{
	bool enabled = false;
	bool populated = false;
	bool building = false;
	std::vector<DX12RootSignatureSummary> rootSignatures;
	std::vector<DX12DescriptorSummary> descriptors;
	std::vector<DX12PsoRootSummary> psoRoots;
	std::vector<DX12DescriptorHeapSummary> descriptorHeaps;
};

static thread_local MetadataSnapshotCache tMetadataSnapshotCache;

static void CopyCachedMetadataSnapshot(
	const MetadataSnapshotCache &cache,
	std::vector<DX12RootSignatureSummary> *rootSignatures,
	std::vector<DX12DescriptorSummary> *descriptors,
	std::vector<DX12PsoRootSummary> *psoRoots,
	std::vector<DX12DescriptorHeapSummary> *descriptorHeaps)
{
	if (rootSignatures)
		*rootSignatures = cache.rootSignatures;
	if (descriptors)
		*descriptors = cache.descriptors;
	if (psoRoots)
		*psoRoots = cache.psoRoots;
	if (descriptorHeaps)
		*descriptorHeaps = cache.descriptorHeaps;
}

static void BuildResourceMetadataSnapshot(
	std::vector<DX12RootSignatureSummary> *rootSignatures,
	std::vector<DX12DescriptorSummary> *descriptors,
	std::vector<DX12PsoRootSummary> *psoRoots,
	std::vector<DX12DescriptorHeapSummary> *descriptorHeaps);

void DX12BeginFrameAnalysisMetadataCache()
{
	tMetadataSnapshotCache = MetadataSnapshotCache();
	tMetadataSnapshotCache.enabled = true;
}

void DX12EndFrameAnalysisMetadataCache()
{
	tMetadataSnapshotCache = MetadataSnapshotCache();
}

void DX12GetResourceMetadataSnapshot(
	std::vector<DX12RootSignatureSummary> *rootSignatures,
	std::vector<DX12DescriptorSummary> *descriptors,
	std::vector<DX12PsoRootSummary> *psoRoots)
{
	DX12GetResourceMetadataSnapshot(rootSignatures, descriptors, psoRoots, nullptr);
}

void DX12GetResourceMetadataSnapshot(
	std::vector<DX12RootSignatureSummary> *rootSignatures,
	std::vector<DX12DescriptorSummary> *descriptors,
	std::vector<DX12PsoRootSummary> *psoRoots,
	std::vector<DX12DescriptorHeapSummary> *descriptorHeaps)
{
	// DX12DescriptorCollectPipeline::DrainForSnapshot removed 鈥?was dead code
	MetadataSnapshotCache &cache = tMetadataSnapshotCache;
	if (cache.enabled && !cache.building) {
		if (!cache.populated) {
			cache.building = true;
			// FrameAnalysis has several exporters that all need the same
			// descriptor inventory. Build it once per dump thread; per-export
			// million-slot rescans were the dominant F8 stall.
			BuildResourceMetadataSnapshot(
				&cache.rootSignatures, &cache.descriptors,
				&cache.psoRoots, &cache.descriptorHeaps);
			cache.building = false;
			cache.populated = true;
		}
		CopyCachedMetadataSnapshot(
			cache, rootSignatures, descriptors, psoRoots, descriptorHeaps);
		DX12FrameAnalysis::LogJsonFunc("MetadataSnapshotCacheHit",
			"\"roots\":%zu,\"descriptors\":%zu,\"psoRoots\":%zu,\"heaps\":%zu,\"wantRoots\":%u,\"wantDescriptors\":%u,\"wantPsoRoots\":%u,\"wantHeaps\":%u",
			cache.rootSignatures.size(), cache.descriptors.size(),
			cache.psoRoots.size(), cache.descriptorHeaps.size(),
			rootSignatures ? 1 : 0,
			descriptors ? 1 : 0,
			psoRoots ? 1 : 0,
			descriptorHeaps ? 1 : 0);
		return;
	}

	BuildResourceMetadataSnapshot(rootSignatures, descriptors, psoRoots, descriptorHeaps);
}

static void BuildResourceMetadataSnapshot(
	std::vector<DX12RootSignatureSummary> *rootSignatures,
	std::vector<DX12DescriptorSummary> *descriptors,
	std::vector<DX12PsoRootSummary> *psoRoots,
	std::vector<DX12DescriptorHeapSummary> *descriptorHeaps)
{
	AcquireSRWLockShared(&gResourceLock);
	// Snapshot pointers must remain stable while sparse heap maps can rehash.
	// This path is frame-analysis only, so take a coherent exclusive snapshot
	// instead of adding synchronization cost to normal Mod descriptor writes.
	AcquireSRWLockExclusive(&gDescriptorLock);
	std::vector<const DescriptorRecord*> liveDescriptors;
	std::vector<DescriptorRecord> heapDescriptors;
	std::unordered_set<SIZE_T> heapDescriptorHandles;
	size_t liveDescriptorCount = 0;
	for (const DescriptorHeapRecord &heap : gDescriptorHeaps) {
		liveDescriptorCount += static_cast<size_t>(
			DX12DescriptorSlotArrayUtils::ReadLiveSlots(&heap));
	}
	liveDescriptors.reserve(liveDescriptorCount + gDescriptorByCpuHandle.size());
	heapDescriptors.reserve(liveDescriptorCount);
	heapDescriptorHandles.reserve(liveDescriptorCount);
	for (DescriptorHeapRecord &heap : gDescriptorHeaps) {
		DescriptorHeapRecord::StateLocks *stateLocks = heap.stateLocks.get();
		if (stateLocks)
			stateLocks->LockAllExclusive();
		if (stateLocks && !stateLocks->IsActive()) {
			stateLocks->UnlockAllExclusive();
			continue;
		}
		heap.currentDescriptors.ForEach(
			[&heapDescriptors, &heapDescriptorHandles, &heap](
				uint32_t slot, const DescriptorSlotBinding &binding) {
				if (!binding.value)
					return;
				const SIZE_T cpuHandle = heap.cpuStart +
					static_cast<SIZE_T>(slot) * heap.increment;
				heapDescriptors.push_back(DescriptorRecord());
				DX12DescriptorSlotArrayUtils::CopyRecordValueForSlot(
					&heapDescriptors.back(), binding.value->value,
					cpuHandle);
				heapDescriptorHandles.insert(cpuHandle);
			});
		for (UINT slot = 0; slot < heap.occupied.size() && slot < heap.slots.size(); ++slot) {
			if (!heap.occupied[slot])
				continue;
			heapDescriptors.push_back(DescriptorRecord());
			DX12DescriptorSlotArrayUtils::CopyRecordValueForSlot(
				&heapDescriptors.back(), heap.slots[slot],
				heap.slots[slot].cpuHandle);
			heapDescriptorHandles.insert(heap.slots[slot].cpuHandle);
		}
		if (stateLocks)
			stateLocks->UnlockAllExclusive();
	}
	for (const DescriptorRecord &record : heapDescriptors)
		liveDescriptors.push_back(&record);
	for (const std::pair<const SIZE_T, DescriptorRecord> &entry : gDescriptorByCpuHandle) {
		if (heapDescriptorHandles.find(entry.first) != heapDescriptorHandles.end())
			continue;
		liveDescriptors.push_back(&entry.second);
	}

	size_t descriptorStateKnown = 0;
	size_t cbvDescriptors = 0;
	size_t srvDescriptors = 0;
	size_t uavDescriptors = 0;
	size_t rtvDescriptors = 0;
	size_t dsvDescriptors = 0;
	size_t textureDescriptors = 0;
	for (const DescriptorRecord *recordPtr : liveDescriptors) {
		const DescriptorRecord &record = *recordPtr;
		bool hasCurrentState = record.hasCurrentState;
		if (!hasCurrentState && record.resource) {
			std::unordered_map<ID3D12Resource*, size_t>::iterator foundResource =
				gResourceByPtr.find(record.resource);
			if (foundResource != gResourceByPtr.end() &&
				foundResource->second < gResources.size()) {
				hasCurrentState = gResources[foundResource->second].hasCurrentState;
			}
		}
		if (hasCurrentState)
			descriptorStateKnown++;
		D3D12_RESOURCE_DESC resourceDesc = record.resourceDesc;
		bool hasResourceDesc = record.hasResourceDesc;
		if (!hasResourceDesc && record.resource) {
			std::unordered_map<ID3D12Resource*, size_t>::iterator foundResource =
				gResourceByPtr.find(record.resource);
			if (foundResource != gResourceByPtr.end() &&
				foundResource->second < gResources.size()) {
				resourceDesc = gResources[foundResource->second].desc;
				hasResourceDesc = true;
			}
		}
		if (record.kindId == DescriptorKind::CBV_ID)
			cbvDescriptors++;
		else if (record.kindId == DescriptorKind::SRV_ID)
			srvDescriptors++;
		else if (record.kindId == DescriptorKind::UAV_ID)
			uavDescriptors++;
		else if (record.kindId == DescriptorKind::RTV_ID)
			rtvDescriptors++;
		else if (record.kindId == DescriptorKind::DSV_ID)
			dsvDescriptors++;
		if (hasResourceDesc &&
			resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D)
			textureDescriptors++;
	}
	DX12FrameAnalysis::LogJsonFunc("MetadataSnapshotBegin",
		"\"roots\":%zu,\"descriptors\":%zu,\"descriptorStates\":%zu,"
		"\"resources\":%zu,\"resourceSlots\":%zu,\"resourcesFromCreate\":%llu,"
		"\"psoRoots\":%zu,\"heaps\":%zu,\"cbv\":%zu,\"srv\":%zu,\"uav\":%zu,\"rtv\":%zu,\"dsv\":%zu,\"texture\":%zu,\"wantRoots\":%u,\"wantDescriptors\":%u,\"wantPsoRoots\":%u,\"wantHeaps\":%u",
		gRootSignatures.size(), liveDescriptors.size(), descriptorStateKnown,
		gLiveResourceCount, gResources.size(),
		static_cast<unsigned long long>(gResourcesRecordedFromCreate),
		gPsoRoots.size(), gDescriptorHeaps.size(),
		cbvDescriptors, srvDescriptors, uavDescriptors, rtvDescriptors, dsvDescriptors,
		textureDescriptors,
		rootSignatures ? 1 : 0,
		descriptors ? 1 : 0,
		psoRoots ? 1 : 0,
		descriptorHeaps ? 1 : 0);
	DX12LogManager::JsonFunc("DX12MetadataSnapshot",
		"descriptors=%zu,cbv=%zu,srv=%zu,uav=%zu,rtv=%zu,dsv=%zu,texture=%zu,resources=%zu,resourceSlots=%zu,heaps=%zu",
		liveDescriptors.size(), cbvDescriptors, srvDescriptors, uavDescriptors,
		rtvDescriptors, dsvDescriptors, textureDescriptors,
		gLiveResourceCount, gResources.size(), gDescriptorHeaps.size());

	if (rootSignatures) {
		rootSignatures->clear();
		rootSignatures->reserve(gRootSignatures.size());
		for (const RootSignatureRecord &record : gRootSignatures) {
			DX12RootSignatureSummary summary;
			summary.rootSignature = record.rootSignature;
			summary.hash = record.hash;
			summary.size = record.size;
			summary.nodeMask = record.nodeMask;
			summary.version = record.version;
			summary.flags = record.flags;
			summary.staticSamplerCount = record.staticSamplerCount;
			summary.parsed = record.parsed;
			summary.parameters = record.parameters;
			rootSignatures->push_back(summary);
		}
		DX12FrameAnalysis::LogJsonFunc("MetadataSnapshotCopied",
			"\"kind\":\"roots\",\"count\":%zu", rootSignatures->size());
	}

	if (descriptors) {
		descriptors->clear();
		descriptors->reserve(liveDescriptors.size());
		size_t copiedBufferDescriptors = 0;
		size_t copiedCbvBufferDescriptors = 0;
		size_t copiedSrvBufferDescriptors = 0;
		size_t copiedUavBufferDescriptors = 0;
		size_t copiedCbvUnresolvedGpuVaDescriptors = 0;
		size_t copiedCbvMissingGpuVaDescriptors = 0;
		size_t copiedCbvMissingSizeDescriptors = 0;
		size_t cbvResolvedByAddressOnly = 0;
		size_t cbvAfterRangeMisses = 0;
		size_t cbvNoLowerBufferMisses = 0;
		size_t cbvOtherResolveMisses = 0;
		size_t cbvResolveSamples = 0;
		for (const DescriptorRecord *recordPtr : liveDescriptors) {
			DX12DescriptorSummary summary;
			FillDescriptorSummary(&summary, *recordPtr, true, true);
			if (summary.hasResourceDesc &&
				summary.resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
				copiedBufferDescriptors++;
				if (summary.kind == DescriptorKind::CBV) {
					copiedCbvBufferDescriptors++;
					if (recordPtr->cbv.SizeInBytes && summary.viewSize &&
						summary.viewSize < recordPtr->cbv.SizeInBytes)
						cbvResolvedByAddressOnly++;
				}
				else if (summary.kind == DescriptorKind::SRV)
					copiedSrvBufferDescriptors++;
				else if (summary.kind == DescriptorKind::UAV)
					copiedUavBufferDescriptors++;
			} else if (summary.kind == DescriptorKind::CBV &&
					summary.gpuVirtualAddress && !summary.resource) {
				copiedCbvUnresolvedGpuVaDescriptors++;
				UINT64 nearestBegin = 0;
				UINT64 nearestEnd = 0;
				UINT64 nearestResource = 0;
				const UINT64 cbvSize = summary.cbvSize ? summary.cbvSize : summary.viewSize;
				const char *reason = DX12BufferResolveDiagnosticsUtils::ReasonLocked(
					summary.gpuVirtualAddress, cbvSize ? cbvSize : 1,
					&nearestBegin, &nearestEnd, &nearestResource);
				if (!strcmp(reason, "after_range"))
					cbvAfterRangeMisses++;
				else if (!strcmp(reason, "no_lower_buffer"))
					cbvNoLowerBufferMisses++;
				else
					cbvOtherResolveMisses++;
				if (cbvResolveSamples < 16) {
					DX12FrameAnalysis::LogJsonFunc("CBVResolveMissSample",
						"\"gpuVa\":\"0x%llX\",\"size\":%llu,\"reason\":\"%s\",\"nearestBegin\":\"0x%llX\",\"nearestEnd\":\"0x%llX\",\"nearestResource\":\"0x%llX\",\"bufferIndexSize\":%zu,\"resourceSlots\":%zu",
						static_cast<unsigned long long>(summary.gpuVirtualAddress),
						static_cast<unsigned long long>(cbvSize),
						reason,
						static_cast<unsigned long long>(nearestBegin),
						static_cast<unsigned long long>(nearestEnd),
						static_cast<unsigned long long>(nearestResource),
						gBufferResourceByGpuVa.size(), gResources.size());
					cbvResolveSamples++;
				}
			} else if (summary.kind == DescriptorKind::CBV && !summary.gpuVirtualAddress) {
				copiedCbvMissingGpuVaDescriptors++;
			} else if (summary.kind == DescriptorKind::CBV && !summary.viewSize && !summary.cbvSize) {
				copiedCbvMissingSizeDescriptors++;
			}
			descriptors->push_back(summary);
		}
		DX12FrameAnalysis::LogJsonFunc("MetadataSnapshotCopied",
			"\"kind\":\"descriptors\",\"count\":%zu", descriptors->size());
		DX12FrameAnalysis::LogJsonFunc("MetadataSnapshotDescriptorResources",
			"\"descriptors\":%zu,\"buffer\":%zu,\"cbvBuffer\":%zu,\"srvBuffer\":%zu,\"uavBuffer\":%zu,\"cbvUnresolvedGpuVa\":%zu,\"cbvMissingGpuVa\":%zu,\"cbvMissingSize\":%zu,\"cbvResolvedByAddressOnly\":%zu,\"cbvAfterRangeMisses\":%zu,\"cbvNoLowerBufferMisses\":%zu,\"cbvOtherResolveMisses\":%zu,\"bufferIndexSize\":%zu",
			descriptors->size(), copiedBufferDescriptors,
			copiedCbvBufferDescriptors, copiedSrvBufferDescriptors,
			copiedUavBufferDescriptors, copiedCbvUnresolvedGpuVaDescriptors,
			copiedCbvMissingGpuVaDescriptors, copiedCbvMissingSizeDescriptors,
			cbvResolvedByAddressOnly, cbvAfterRangeMisses, cbvNoLowerBufferMisses,
			cbvOtherResolveMisses, gBufferResourceByGpuVa.size());
	}

	if (psoRoots) {
		psoRoots->clear();
		psoRoots->reserve(gPsoRoots.size());
		for (const PsoRootRecord &record : gPsoRoots) {
			DX12PsoRootSummary summary;
			summary.psoIndex = record.psoIndex;
			summary.kind = record.kind;
			summary.pipelineState = record.pipelineState;
			summary.rootSignature = record.rootSignature;
			psoRoots->push_back(summary);
		}
		DX12FrameAnalysis::LogJsonFunc("MetadataSnapshotCopied",
			"\"kind\":\"psoRoots\",\"count\":%zu", psoRoots->size());
	}

	if (descriptorHeaps) {
		descriptorHeaps->clear();
		descriptorHeaps->reserve(gDescriptorHeaps.size());
		for (const DescriptorHeapRecord &record : gDescriptorHeaps) {
			DX12DescriptorHeapSummary summary;
			summary.heap = record.heap;
			summary.type = static_cast<UINT>(record.desc.Type);
			summary.numDescriptors = record.desc.NumDescriptors;
			summary.flags = static_cast<UINT>(record.desc.Flags);
			summary.nodeMask = record.desc.NodeMask;
			summary.cpuStart = record.cpuStart;
			summary.gpuStart = record.gpuStart;
			summary.increment = record.increment;
			descriptorHeaps->push_back(summary);
		}
		DX12FrameAnalysis::LogJsonFunc("MetadataSnapshotCopied",
			"\"kind\":\"heaps\",\"count\":%zu", descriptorHeaps->size());
	}

	ReleaseSRWLockExclusive(&gDescriptorLock);
	ReleaseSRWLockShared(&gResourceLock);
	DX12FrameAnalysis::LogJsonFunc("MetadataSnapshotComplete", nullptr);
}

bool DX12FindDescriptorSummaryByCpuHandle(
	SIZE_T cpuHandle, DX12DescriptorSummary *summary)
{
	if (!DX12FindDescriptorSummaryByCpuHandleLight(cpuHandle, summary))
		return false;
	CompleteDescriptorSummaryResourceInfo(summary, false);
	if (summary->kind == DescriptorKind::CBV && !summary->resource) {
		const UINT64 cbvGpuVa = summary->gpuVirtualAddress;
		const UINT64 cbvBytes = summary->cbvSize ? summary->cbvSize : summary->viewSize;
		if (cbvGpuVa && cbvBytes) {
			CompleteDescriptorSummaryBufferGpuVaInfo(summary, cbvGpuVa, cbvBytes, false);
			summary->bufferViewOffset = summary->resourceOffset;
			summary->bufferViewBytes = summary->viewSize;
		}
	}
	return true;
}

bool DX12FindDescriptorSummaryByCpuHandleLight(
	SIZE_T cpuHandle, DX12DescriptorSummary *summary)
{
	if (!cpuHandle || !summary)
		return false;

	DX12DescriptorHeapShadowUtils::EnsureVisible(
		&cpuHandle,
		1);
	// Try heap slot via ShadowUtils first (per-heap lock, no global contention)
	DescriptorRecord shadowRecord;
	if (DX12DescriptorHeapShadowUtils::ReadDescriptorFromSlot(cpuHandle,
			D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES, &shadowRecord)) {
		FillDescriptorSummary(summary, shadowRecord, false, false, false);
		return true;
	}
	AcquireSRWLockShared(&gDescriptorLock);
	DescriptorSlotLocation location = {};
	if (ResolveDescriptorSlotForCpuHandleLocked(cpuHandle, nullptr, &location, nullptr)) {
		if (location.heapIndex < gDescriptorHeaps.size()) {
			const DescriptorHeapRecord &heap = gDescriptorHeaps[location.heapIndex];
			if (location.slot < heap.occupied.size() &&
					location.slot < heap.slots.size() &&
					heap.occupied[location.slot]) {
				FillDescriptorSummary(summary, heap.slots[location.slot], false, false, false);
				ReleaseSRWLockShared(&gDescriptorLock);
				return true;
			}
		}
	}

	std::unordered_map<SIZE_T, DescriptorRecord>::iterator found =
		gDescriptorByCpuHandle.find(cpuHandle);
	if (found != gDescriptorByCpuHandle.end()) {
		FillDescriptorSummary(summary, found->second, false, false, false);
		ReleaseSRWLockShared(&gDescriptorLock);
		return true;
	}

	ReleaseSRWLockShared(&gDescriptorLock);
	return false;
}

class DX12DescriptorQueryBatchUtils
{
public:
static bool FillLightSummaryLocked(SIZE_T cpuHandle, DX12DescriptorSummary *summary)
{
	if (!cpuHandle || !summary)
		return false;

	DescriptorRecord shadowRecord;
	if (DX12DescriptorHeapShadowUtils::ReadDescriptorFromSlotLocked(
			cpuHandle,
			D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES,
			&shadowRecord)) {
		FillDescriptorSummary(summary, shadowRecord, false, false, false);
		return true;
	}

	DescriptorSlotLocation location = {};
	if (ResolveDescriptorSlotForCpuHandleLocked(cpuHandle, nullptr, &location, nullptr)) {
		if (location.heapIndex < gDescriptorHeaps.size()) {
			const DescriptorHeapRecord &heap = gDescriptorHeaps[location.heapIndex];
			if (location.slot < heap.occupied.size() &&
					location.slot < heap.slots.size() &&
					heap.occupied[location.slot]) {
				FillDescriptorSummary(summary, heap.slots[location.slot], false, false, false);
				return true;
			}
		}
	}

	std::unordered_map<SIZE_T, DescriptorRecord>::iterator found =
		gDescriptorByCpuHandle.find(cpuHandle);
	if (found != gDescriptorByCpuHandle.end()) {
		FillDescriptorSummary(summary, found->second, false, false, false);
		return true;
	}

	return false;
}

static void CompleteResourceInfoForFoundSummaries(
	DX12DescriptorSummary *summaries, const uint8_t *found, size_t count)
{
	if (!summaries || !found || count == 0)
		return;

	AcquireSRWLockShared(&gResourceLock);
	for (size_t i = 0; i < count; ++i) {
		if (!found[i])
			continue;
		CompleteDescriptorSummaryResourceInfo(&summaries[i], true);
		const bool isCbv = summaries[i].kind == DescriptorKind::CBV;
		const UINT64 cbvGpuVa = summaries[i].gpuVirtualAddress;
		const UINT64 cbvBytes = summaries[i].cbvSize ? summaries[i].cbvSize : summaries[i].viewSize;
		if (isCbv && !summaries[i].resource && cbvGpuVa && cbvBytes) {
			CompleteDescriptorSummaryBufferGpuVaInfo(&summaries[i], cbvGpuVa, cbvBytes, true);
			summaries[i].bufferViewOffset = summaries[i].resourceOffset;
			summaries[i].bufferViewBytes = summaries[i].viewSize;
		}
	}
	ReleaseSRWLockShared(&gResourceLock);
}
};

class DX12DescriptorHeapQueryUtils
{
public:
static bool TryFillByPtrLocked(
	ID3D12DescriptorHeap *heap,
	DX12DescriptorHeapSummary *summary)
{
	if (!heap || !summary)
		return false;
	std::unordered_map<ID3D12DescriptorHeap*, size_t>::const_iterator found =
		gDescriptorHeapByPtr.find(heap);
	if (found == gDescriptorHeapByPtr.end() ||
		found->second >= gDescriptorHeaps.size()) {
		return false;
	}
	const DescriptorHeapRecord &record = gDescriptorHeaps[found->second];
	if (record.heap != heap)
		return false;
	DX12DescriptorHeapStateUtils::FillSummary(record, summary);
	return true;
}

static bool TryFillByGpuHandleLocked(
	D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle,
	D3D12_DESCRIPTOR_HEAP_TYPE type,
	DX12DescriptorHeapSummary *summary)
{
	if (!gpuHandle.ptr || !summary)
		return false;
	for (const DescriptorHeapRecord &record : gDescriptorHeaps) {
		if (record.desc.Type != type || record.gpuStart == 0 || record.increment == 0)
			continue;
		const UINT64 begin = record.gpuStart;
		const UINT64 end = begin +
			static_cast<UINT64>(record.desc.NumDescriptors) * record.increment;
		if (gpuHandle.ptr < begin || gpuHandle.ptr >= end)
			continue;
		DX12DescriptorHeapStateUtils::FillSummary(record, summary);
		return true;
	}
	return false;
}

static bool TryFillByCpuHandleLocked(
	D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
	D3D12_DESCRIPTOR_HEAP_TYPE type,
	DX12DescriptorHeapSummary *summary)
{
	if (!cpuHandle.ptr || !summary)
		return false;
	for (const DescriptorHeapRecord &record : gDescriptorHeaps) {
		if (record.desc.Type != type || record.cpuStart == 0 || record.increment == 0)
			continue;
		const SIZE_T begin = record.cpuStart;
		const SIZE_T end = begin +
			static_cast<SIZE_T>(record.desc.NumDescriptors) * record.increment;
		if (cpuHandle.ptr < begin || cpuHandle.ptr >= end)
			continue;
		DX12DescriptorHeapStateUtils::FillSummary(record, summary);
		return true;
	}
	return false;
}
};

void DX12FindDescriptorSummariesByCpuHandle(
	const SIZE_T *cpuHandles, size_t count,
	DX12DescriptorSummary *summaries, uint8_t *found,
	bool completeResourceInfo)
{
	if (!cpuHandles || !summaries || !found || count == 0)
		return;

	memset(found, 0, count * sizeof(uint8_t));
	DX12DescriptorHeapShadowUtils::EnsureVisible(
		cpuHandles,
		count);
	AcquireSRWLockShared(&gDescriptorLock);
	for (size_t i = 0; i < count; ++i) {
		summaries[i] = DX12DescriptorSummary();
		if (DX12DescriptorQueryBatchUtils::FillLightSummaryLocked(cpuHandles[i], &summaries[i]))
			found[i] = 1;
	}
	ReleaseSRWLockShared(&gDescriptorLock);

	if (completeResourceInfo)
		DX12DescriptorQueryBatchUtils::CompleteResourceInfoForFoundSummaries(
			summaries, found, count);
}

void DX12FindDescriptorSummariesByCpuHandleNoWait(
	const SIZE_T *cpuHandles, size_t count,
	DX12DescriptorSummary *summaries, uint8_t *found,
	uint8_t *pending,
	bool completeResourceInfo)
{
	if (!cpuHandles || !summaries || !found || count == 0)
		return;

	memset(found, 0, count * sizeof(uint8_t));
	if (pending)
		memset(pending, 0, count * sizeof(uint8_t));

	AcquireSRWLockShared(&gDescriptorLock);
	for (size_t i = 0; i < count; ++i) {
		summaries[i] = DX12DescriptorSummary();
		if (DX12DescriptorQueryBatchUtils::FillLightSummaryLocked(cpuHandles[i], &summaries[i])) {
			found[i] = 1;
		}
	}
	ReleaseSRWLockShared(&gDescriptorLock);

	if (completeResourceInfo)
		DX12DescriptorQueryBatchUtils::CompleteResourceInfoForFoundSummaries(
			summaries, found, count);
}

void DX12FindDescriptorSummariesByHeapSlot(
	ID3D12DescriptorHeap *heap,
	const UINT *slots,
	size_t count,
	DX12DescriptorSummary *summaries,
	uint8_t *found,
	bool completeResourceInfo)
{
	if (!heap || !slots || !summaries || !found || count == 0)
		return;

	memset(found, 0, count * sizeof(uint8_t));
	AcquireSRWLockShared(&gDescriptorLock);
	std::unordered_map<ID3D12DescriptorHeap*, size_t>::const_iterator heapIt =
		gDescriptorHeapByPtr.find(heap);
	if (heapIt != gDescriptorHeapByPtr.end() &&
			heapIt->second < gDescriptorHeaps.size()) {
		const size_t heapIndex = heapIt->second;
		DescriptorHeapRecord &record = gDescriptorHeaps[heapIndex];
		if (record.heap == heap) {
			for (size_t i = 0; i < count; ++i) {
				summaries[i] = DX12DescriptorSummary();
				const UINT slot = slots[i];
				if (slot >= record.desc.NumDescriptors)
					continue;
				SRWLOCK *slotLock =
					DX12DescriptorHeapShadowUtils::HeapLock(heapIndex, slot);
				if (slotLock)
					DX12DescriptorShadowTelemetryUtils::AcquireHeapShared(slotLock);
				DescriptorRecord recordCopy;
				const bool copied =
					DX12DescriptorSlotArrayUtils::CopyCurrentRecord(
						record, slot, &recordCopy);
				if (slotLock)
					ReleaseSRWLockShared(slotLock);
				if (!copied)
					continue;
				FillDescriptorSummary(
					&summaries[i], recordCopy, false, false, false);
				found[i] = 1;
			}
		}
	}
	ReleaseSRWLockShared(&gDescriptorLock);

	if (completeResourceInfo)
		DX12DescriptorQueryBatchUtils::CompleteResourceInfoForFoundSummaries(
			summaries, found, count);
}

bool FindDescriptorSummaryByCpuHandleAtEpoch(
	SIZE_T cpuHandle, UINT64 epoch, DX12DescriptorSummary *summary)
{
	if (!cpuHandle || !epoch || !summary)
		return false;

	DX12DescriptorHeapShadowUtils::EnsureVisible(
		&cpuHandle,
		1);
	AcquireSRWLockShared(&gDescriptorLock);
	DescriptorSlotLocation location = {};
	if (!ResolveDescriptorSlotForCpuHandleLocked(cpuHandle, nullptr, &location, nullptr) ||
			location.heapIndex >= gDescriptorHeaps.size()) {
		ReleaseSRWLockShared(&gDescriptorLock);
		return false;
	}
	const DescriptorHeapRecord &heap = gDescriptorHeaps[location.heapIndex];
	if (location.slot >= heap.slotHistory.size() ||
			location.slot >= heap.slotHistoryEpochs.size()) {
		ReleaseSRWLockShared(&gDescriptorLock);
		return false;
	}
	const std::vector<DescriptorRecord> &history = heap.slotHistory[location.slot];
	const std::vector<UINT64> &epochs = heap.slotHistoryEpochs[location.slot];
	if (history.empty() || epochs.empty()) {
		ReleaseSRWLockShared(&gDescriptorLock);
		return false;
	}
	size_t best = SIZE_MAX;
	size_t lo = 0;
	size_t hi = epochs.size();
	while (lo < hi) {
		const size_t mid = lo + (hi - lo) / 2;
		if (epochs[mid] <= epoch) {
			best = mid;
			lo = mid + 1;
		} else {
			hi = mid;
		}
	}
	if (best == SIZE_MAX || best >= history.size()) {
		ReleaseSRWLockShared(&gDescriptorLock);
		return false;
	}
	FillDescriptorSummary(summary, history[best], false);
	ReleaseSRWLockShared(&gDescriptorLock);
	CompleteDescriptorSummaryResourceInfo(summary, false);
	return true;
}

bool FindDescriptorSummaryByCpuHandleAtEpochAndKind(
	SIZE_T cpuHandle, UINT64 epoch, const char *kind, DX12DescriptorSummary *summary)
{
	if (!cpuHandle || !epoch || !kind || !kind[0] || !summary)
		return false;

	DX12DescriptorHeapShadowUtils::EnsureVisible(
		&cpuHandle,
		1);
	AcquireSRWLockShared(&gDescriptorLock);
	DescriptorSlotLocation location = {};
	if (!ResolveDescriptorSlotForCpuHandleLocked(cpuHandle, nullptr, &location, nullptr) ||
			location.heapIndex >= gDescriptorHeaps.size()) {
		ReleaseSRWLockShared(&gDescriptorLock);
		return false;
	}
	const DescriptorHeapRecord &heap = gDescriptorHeaps[location.heapIndex];
	if (location.slot >= heap.slotHistory.size() ||
			location.slot >= heap.slotHistoryEpochs.size()) {
		ReleaseSRWLockShared(&gDescriptorLock);
		return false;
	}
	const std::vector<DescriptorRecord> &history = heap.slotHistory[location.slot];
	const std::vector<UINT64> &epochs = heap.slotHistoryEpochs[location.slot];
	if (history.empty() || epochs.empty()) {
		ReleaseSRWLockShared(&gDescriptorLock);
		return false;
	}

	size_t best = SIZE_MAX;
	size_t lo = 0;
	size_t hi = epochs.size();
	while (lo < hi) {
		const size_t mid = lo + (hi - lo) / 2;
		if (epochs[mid] <= epoch) {
			best = mid;
			lo = mid + 1;
		} else {
			hi = mid;
		}
	}
	if (best == SIZE_MAX || best >= history.size()) {
		ReleaseSRWLockShared(&gDescriptorLock);
		return false;
	}

	for (size_t i = best + 1; i > 0; --i) {
		const DescriptorRecord &record = history[i - 1];
		if (record.kind != kind)
			continue;
		// Root-signature ranges declare the descriptor type used by the shader.
		// Walk the slot history to the latest matching type instead of treating
		// a later different-kind write as loss of the SRV/UAV binding evidence.
		FillDescriptorSummary(summary, record, false);
		ReleaseSRWLockShared(&gDescriptorLock);
		CompleteDescriptorSummaryResourceInfo(summary, false);
		return true;
	}

	ReleaseSRWLockShared(&gDescriptorLock);
	return false;
}

bool DX12FindDescriptorHeapByGpuHandle(
	D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE type,
	DX12DescriptorHeapSummary *summary)
{
	if (!gpuHandle.ptr || !summary)
		return false;

	AcquireSRWLockShared(&gDescriptorLock);
	const bool found = DX12DescriptorHeapQueryUtils::TryFillByGpuHandleLocked(
		gpuHandle, type, summary);
	ReleaseSRWLockShared(&gDescriptorLock);
	return found;
}

bool DX12FindDescriptorHeapByPtr(
	ID3D12DescriptorHeap *heap, DX12DescriptorHeapSummary *summary)
{
	if (!heap || !summary)
		return false;

	AcquireSRWLockShared(&gDescriptorLock);
	const bool found = DX12DescriptorHeapQueryUtils::TryFillByPtrLocked(
		heap, summary);
	ReleaseSRWLockShared(&gDescriptorLock);
	return found;
}

bool DX12FindDescriptorHeapByCpuHandle(
	D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE type,
	DX12DescriptorHeapSummary *summary)
{
	if (!cpuHandle.ptr || !summary)
		return false;

	AcquireSRWLockShared(&gDescriptorLock);
	const bool found = DX12DescriptorHeapQueryUtils::TryFillByCpuHandleLocked(
		cpuHandle, type, summary);
	ReleaseSRWLockShared(&gDescriptorLock);
	return found;
}

void DX12GetTrackedResourceSnapshot(std::vector<DX12ResourceSummary> *resources)
{
	if (!resources)
		return;

	resources->clear();
	AcquireSRWLockShared(&gResourceLock);
	resources->reserve(gLiveResourceCount);
	for (const ResourceRecord &record : gResources) {
		if (!record.resource)
			continue;
		DX12ResourceSummary summary;
		summary.resource = record.resource;
		summary.resourceDesc = record.desc;
		summary.hasResourceDesc = true;
		if (record.hasHeapType) {
			summary.resourceHeapType = static_cast<UINT>(record.heapType);
			summary.hasResourceHeapType = true;
		}
		summary.gpuVirtualAddress = record.gpuVirtualAddress;
		summary.size = record.size;
		summary.currentState = static_cast<UINT>(record.currentState);
		summary.hasCurrentState = record.hasCurrentState;
		summary.stateTrackedByBarrier = record.stateTrackedByBarrier;
		resources->push_back(summary);
	}
	ReleaseSRWLockShared(&gResourceLock);
}

void DX12GetRetainedTrackedResourceSnapshot(std::vector<DX12ResourceSummary> *resources)
{
	if (!resources)
		return;

	resources->clear();
	AcquireSRWLockShared(&gResourceLock);
	resources->reserve(gLiveResourceCount);
	for (const ResourceRecord &record : gResources) {
		if (!record.resource)
			continue;
		if (!DX12ResourceSnapshotUtils::TryAddRef(record.resource))
			continue;
		DX12ResourceSummary summary;
		summary.resource = record.resource;
		summary.resourceDesc = record.desc;
		summary.hasResourceDesc = true;
		if (record.hasHeapType) {
			summary.resourceHeapType = static_cast<UINT>(record.heapType);
			summary.hasResourceHeapType = true;
		}
		summary.gpuVirtualAddress = record.gpuVirtualAddress;
		summary.size = record.size;
		summary.currentState = static_cast<UINT>(record.currentState);
		summary.hasCurrentState = record.hasCurrentState;
		summary.stateTrackedByBarrier = record.stateTrackedByBarrier;
		summary.retained = true;
		resources->push_back(summary);
	}
	ReleaseSRWLockShared(&gResourceLock);
}

void DX12ReleaseTrackedResourceSnapshot(std::vector<DX12ResourceSummary> *resources)
{
	if (!resources)
		return;
	for (DX12ResourceSummary &summary : *resources) {
		if (summary.retained && summary.resource)
			DX12ResourceSnapshotUtils::Release(summary.resource);
		summary.resource = nullptr;
		summary.retained = false;
	}
	resources->clear();
}

bool DX12RetainResource(ID3D12Resource *resource)
{
	return DX12ResourceSnapshotUtils::TryAddRef(resource);
}

void DX12ReleaseRetainedResource(ID3D12Resource *resource)
{
	DX12ResourceSnapshotUtils::Release(resource);
}
