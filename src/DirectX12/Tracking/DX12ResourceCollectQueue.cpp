// DX12ResourceCollectQueue: deferred resource-collect task queue.
// Split from DX12ResourceTracking.cpp; implementation unit for its module.
#include "DX12Core.h"
#include "DX12Hooks.h"
#include "DX12Tracking.h"
#include "DX12Runtime.h"
#include "DX12Resources.h"
#include "DX12Presentation.h"
#include <algorithm>
#include <chrono>
#include <unordered_set>
#include <intrin.h>


static constexpr uint64_t kResourceQueueStatsInterval = 4096;
// Depth that counts as a backed-up queue, and how much coarser the stats
// cadence gets while it stays that deep.
static constexpr size_t kResourceQueueDeepDepth = 1024;
static constexpr uint64_t kDeepQueueIntervalScale = 64;
static constexpr int64_t kResourceQueueSlowTaskUs = 2000;
static constexpr int64_t kResourceQueueSlowDrainUs = 1000;

void DX12ResourceCollectQueue::Start()
{
	bool expected = false;
	if (!started.compare_exchange_strong(
			expected,
			true,
			std::memory_order_acq_rel,
			std::memory_order_acquire)) {
		return;
	}

	stopRequested.store(false, std::memory_order_release);
	if (worker.joinable())
		worker.join();
	worker = std::thread(&DX12ResourceCollectQueue::WorkerLoop);
	DX12_LOG("DX12ResourceCollectQueue",
		"event=Start,mode=async");
}


void DX12ResourceCollectQueue::Stop()
{
	const bool wasStarted =
		started.exchange(false, std::memory_order_acq_rel);
	if (!wasStarted && !worker.joinable())
		return;

	stopRequested.store(true, std::memory_order_release);
	wake.notify_all();
	idle.notify_all();
	if (worker.joinable())
		worker.join();
	stopRequested.store(false, std::memory_order_release);
}

bool DX12ResourceCollectQueue::Enqueue(
	ID3D12Resource *resource,
	const D3D12_RESOURCE_DESC *desc,
	const D3D12_HEAP_PROPERTIES *heapProperties,
	const PlacedResourceInfo *placedInfo,
	D3D12_RESOURCE_STATES initialState)
{
	if (!resource || !started.load(std::memory_order_acquire))
		return false;

	DX12ResourceCollectTask task;
	task.kind = DX12ResourceCollectTaskKind::Resource;
	task.resource = resource;
	task.initialState = initialState;
	task.enqueueThreadId = GetCurrentThreadId();
	task.sequence = nextSequence.fetch_add(1, std::memory_order_relaxed);
	const UINT64 sequence = task.sequence;
	if (desc) {
		task.desc = *desc;
		task.hasDesc = true;
	}
	if (heapProperties) {
		task.heapProperties = *heapProperties;
		task.hasHeapProperties = true;
	}
	if (placedInfo) {
		task.placedInfo = *placedInfo;
		task.hasPlacedInfo = true;
	}
	if (!DX12ResourceSnapshotUtils::TryAddRef(resource)) {
		addRefFailed.fetch_add(1, std::memory_order_relaxed);
		return false;
	}
	task.retainedResource = true;

	size_t depth = 0;
	bool needWake = false;
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (!started.load(std::memory_order_acquire) ||
			stopRequested.load(std::memory_order_acquire)) {
			DX12ResourceSnapshotUtils::Release(task.resource);
			task.resource = nullptr;
			task.retainedResource = false;
			return false;
		}
		// Only the empty->non-empty transition needs a wake: the worker
		// re-checks the predicate under the same mutex, so pushes made while
		// it is draining are observed without a syscall.
		needWake = tasks.empty();
		tasks.push_back(std::move(task));
		depth = tasks.size() + static_cast<size_t>(activeTasks);
	}

	enqueued.fetch_add(1, std::memory_order_relaxed);
	UpdateMaxDepth(depth);
	LogStats("Enqueue", sequence, depth);
	if (needWake)
		wake.notify_one();
	return true;
}

bool DX12ResourceCollectQueue::EnqueueResourceBarriers(
	UINT numBarriers,
	const D3D12_RESOURCE_BARRIER *barriers,
	const char *source)
{
	if (!numBarriers || !barriers || !started.load(std::memory_order_acquire))
		return false;

	DX12ResourceCollectTask task;
	task.kind = DX12ResourceCollectTaskKind::ResourceBarrier;
	task.source = source;
	task.enqueueThreadId = GetCurrentThreadId();
	task.sequence = nextSequence.fetch_add(1, std::memory_order_relaxed);
	const UINT64 sequence = task.sequence;
	task.barriers.assign(barriers, barriers + numBarriers);
	bool retainedAll = true;
	for (D3D12_RESOURCE_BARRIER &barrier : task.barriers) {
		if (!TryAddRefBarrierResources(&barrier))
			retainedAll = false;
	}
	if (!retainedAll)
		addRefFailed.fetch_add(1, std::memory_order_relaxed);

	size_t depth = 0;
	bool needWake = false;
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (!started.load(std::memory_order_acquire) ||
			stopRequested.load(std::memory_order_acquire)) {
			for (D3D12_RESOURCE_BARRIER &barrier : task.barriers)
				ReleaseBarrierResources(&barrier);
			task.barriers.clear();
			return false;
		}
		// Only the empty->non-empty transition needs a wake (see Enqueue).
		needWake = tasks.empty();
		tasks.push_back(std::move(task));
		depth = tasks.size() + static_cast<size_t>(activeTasks);
	}

	enqueued.fetch_add(1, std::memory_order_relaxed);
	UpdateMaxDepth(depth);
	LogStats("Enqueue", sequence, depth);
	if (needWake)
		wake.notify_one();
	return true;
}


void DX12ResourceCollectQueue::DrainForSnapshot(const char *reason)
{
	WaitForIdle(reason ? reason : "Snapshot");
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
	DX12_LOG("DX12ResourceCollectQueue",
		"event=WorkerEnter,threadId=%lu",
		GetCurrentThreadId());

	static constexpr size_t kBatchSize = 64;
	for (;;) {
		DX12ResourceCollectTask batch[kBatchSize];
		size_t batchCount = 0;
		size_t depthAfterPop = 0;
		{
			std::unique_lock<std::mutex> lock(mutex);
			DX12_DIAG_QPC(waitBegin);
			wake.wait(lock, []() {
				return stopRequested.load(std::memory_order_acquire) || !tasks.empty();
			});
			DX12_PROF_ADD_US_ITEMS(
				"Worker.wait.idle",
				DX12_DIAG_ELAPSED_US(waitBegin),
				static_cast<LONG64>(tasks.size() + static_cast<size_t>(activeTasks)));
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
	DX12_LOG("DX12ResourceCollectQueue",
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
	DX12_PROF_ADD_US_ITEMS(
		"Worker.wait.drain",
		static_cast<LONG64>(elapsedUs),
		static_cast<LONG64>(pendingSnapshot));
	if (elapsedUs >= kResourceQueueSlowDrainUs) {
		DX12_LOG("DX12ResourceCollectQueueDrain",
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
	DX12_PROF_ADD_US_ITEMS(
		"Worker.wait.drain",
		static_cast<LONG64>(elapsedUs),
		static_cast<LONG64>(pendingSnapshot));
	if (elapsedUs >= kResourceQueueSlowDrainUs) {
		DX12_LOG("DX12ResourceCollectQueueDrain",
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

	DX12_DIAG_QPC(taskBegin);
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
	DX12_PROF_ADD_US_ITEMS(
		"Worker.task",
		DX12_DIAG_ELAPSED_US(taskBegin),
		static_cast<LONG64>(itemCount));
}

void DX12ResourceCollectQueue::ProcessResourceTask(DX12ResourceCollectTask *task, const char *source)
{
	if (!task || !task->resource)
		return;

	const auto begin = std::chrono::steady_clock::now();
	DX12_DIAG_QPC(recordBegin);
	RecordResource(
		task->resource,
		task->hasDesc ? &task->desc : nullptr,
		task->hasHeapProperties ? &task->heapProperties : nullptr,
		task->hasPlacedInfo ? &task->placedInfo : nullptr,
		task->initialState);
	DX12_PROF_CPU_STAGE(
		"ResourceCollectQueue",
		"record_resource",
		static_cast<UINT>(task->kind),
		1,
		DX12_DIAG_ELAPSED_US(recordBegin));

	const auto end = std::chrono::steady_clock::now();
	const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count();
	RecordResourceTaskStats(task, source, static_cast<int64_t>(elapsedUs));
	if (task->retainedResource) {
		DX12_DIAG_QPC(releaseBegin);
		DX12ResourceSnapshotUtils::Release(task->resource);
		DX12_PROF_CPU_STAGE(
			"ResourceCollectQueue",
			"release_resource_ref",
			static_cast<UINT>(task->kind),
			1,
			DX12_DIAG_ELAPSED_US(releaseBegin));
	}
	task->resource = nullptr;
	task->retainedResource = false;
}

void DX12ResourceCollectQueue::ProcessResourceBarrierTask(DX12ResourceCollectTask *task, const char *source)
{
	if (!task || task->barriers.empty())
		return;

	DX12_DIAG_QPC(recordBegin);
	DX12RecordResourceBarrier(static_cast<UINT>(task->barriers.size()), task->barriers.data());
	DX12_PROF_CPU_STAGE(
		"ResourceCollectQueue",
		"record_resource_barriers",
		static_cast<UINT>(task->kind),
		static_cast<LONG64>(task->barriers.size()),
		DX12_DIAG_ELAPSED_US(recordBegin));

	DX12_DIAG_QPC(releaseBegin);
	for (D3D12_RESOURCE_BARRIER &barrier : task->barriers) {
		ReleaseBarrierResources(&barrier);
	}
	DX12_PROF_CPU_STAGE(
		"ResourceCollectQueue",
		"release_barrier_refs",
		static_cast<UINT>(task->kind),
		static_cast<LONG64>(task->barriers.size()),
		DX12_DIAG_ELAPSED_US(releaseBegin));
	task->barriers.clear();
}

void DX12ResourceCollectQueue::ProcessRootSignatureTask(DX12ResourceCollectTask *task, const char *source)
{
	if (!task || !task->rootSignature || task->rootSignatureBlob.empty())
		return;

	DX12_LOG("DX12ResourceCollectQueue",
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
	DX12_DIAG_QPC(parseBegin);
	record.hash = Fnv1a64(task->rootSignatureBlob.data(), task->rootSignatureBlob.size());
	record.size = task->rootSignatureBlob.size();
	record.nodeMask = task->rootSignatureNodeMask;
	ParseRootSignatureBlob(&record, task->rootSignatureBlob.data(), task->rootSignatureBlob.size());
	const bool parsed = record.parsed;
	const size_t parameterCount = record.parameters.size();
	DX12_PROF_CPU_STAGE(
		"ResourceCollectQueue",
		"parse_root_signature",
		static_cast<UINT>(task->kind),
		static_cast<LONG64>(task->rootSignatureBlob.size()),
		DX12_DIAG_ELAPSED_US(parseBegin));

	DX12_DIAG_QPC(storeBegin);
	AcquireSRWLockExclusive(&gDescriptorLock);
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
	DX12_PROF_CPU_STAGE(
		"ResourceCollectQueue",
		"store_root_signature",
		static_cast<UINT>(task->kind),
		static_cast<LONG64>(parameterCount),
		DX12_DIAG_ELAPSED_US(storeBegin));

	DX12_LOG("DX12ResourceCollectQueue",
		"event=ProcessRootSignatureComplete,source=%s,rootSource=%s,seq=%llu,rootSignature=%p,parsed=%u,parameterCount=%zu,completed=%llu",
		source ? source : "",
		task->source ? task->source : "",
		static_cast<unsigned long long>(task->sequence),
		task->rootSignature,
		parsed ? 1 : 0,
		parameterCount,
		static_cast<unsigned long long>(completed.load(std::memory_order_relaxed)));
	DX12_DIAG_QPC(releaseBegin);
	ReleaseTaskRootSignature(task);
	DX12_PROF_CPU_STAGE(
		"ResourceCollectQueue",
		"release_root_signature",
		static_cast<UINT>(task->kind),
		1,
		DX12_DIAG_ELAPSED_US(releaseBegin));
}

void DX12ResourceCollectQueue::ProcessPsoRootSignatureTask(DX12ResourceCollectTask *task, const char *source)
{
	if (!task || !task->psoIndex)
		return;

	DX12_LOG("DX12ResourceCollectQueue",
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

	DX12_DIAG_QPC(recordBegin);
	DX12RecordPsoRootSignatureImmediate(
		task->psoIndex,
		task->psoKind.c_str(),
		task->pipelineState,
		task->rootSignature);
	DX12_PROF_CPU_STAGE(
		"ResourceCollectQueue",
		"record_pso_root_signature",
		static_cast<UINT>(task->kind),
		1,
		DX12_DIAG_ELAPSED_US(recordBegin));

	DX12_LOG("DX12ResourceCollectQueue",
		"event=ProcessPsoRootSignatureComplete,source=%s,psoSource=%s,seq=%llu,pso=%llu,completed=%llu",
		source ? source : "",
		task->source ? task->source : "",
		static_cast<unsigned long long>(task->sequence),
		static_cast<unsigned long long>(task->psoIndex),
		static_cast<unsigned long long>(completed.load(std::memory_order_relaxed)));
	DX12_DIAG_QPC(releaseBegin);
	ReleaseTaskPsoRootSignature(task);
	DX12_PROF_CPU_STAGE(
		"ResourceCollectQueue",
		"release_pso_root_signature",
		static_cast<UINT>(task->kind),
		1,
		DX12_DIAG_ELAPSED_US(releaseBegin));
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
	// A persistent backlog must never disable the cadence. The deep-queue case
	// used to log every single task, so one queue that stayed backed up (a
	// hunting session drained ~18M tasks in minutes) produced gigabytes of log
	// per minute - and that log I/O itself stalled the game. Deep queues are
	// still reported, just at a coarser interval, and every line carries
	// pending/maxDepth so the backlog shape stays visible.
	const uint64_t interval = depth >= kResourceQueueDeepDepth ?
		kResourceQueueStatsInterval * kDeepQueueIntervalScale :
		kResourceQueueStatsInterval;
	if (sequence != 0 && serial % interval != 0)
		return;
	DX12_LOG("DX12ResourceCollectQueue",
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
		DX12_LOG("DX12ResourceCollectQueueSlowResource",
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
	DX12_LOG("DX12ResourceCollectQueueStats",
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
