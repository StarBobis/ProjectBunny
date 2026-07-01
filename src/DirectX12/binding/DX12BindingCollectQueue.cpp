#include "DX12BindingCollectQueue.hpp"

#include <chrono>

#include "DX12Profiling.hpp"
#include "DX12BindingTracker.hpp"
#include "DX12LogManager.hpp"

static constexpr uint64_t kBindingQueueStatsInterval = 4096;
static constexpr int64_t kBindingQueueSlowTaskUs = 1000;
static constexpr int64_t kBindingQueueSlowDrainUs = 1000;

void DX12BindingCollectQueue::Start()
{
	bool expected = false;
	if (!started.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
		return;

	stopRequested.store(false, std::memory_order_release);
	worker = std::thread([]() { WorkerLoop(); });
	DX12LogManager::JsonFunc("DX12BindingCollectQueue",
		"\"event\":\"Start\",\"thread\":\"%p\"",
		worker.native_handle());
}

void DX12BindingCollectQueue::Stop()
{
	if (!started.load(std::memory_order_acquire))
		return;

	WaitForIdle("StopBegin");
	stopRequested.store(true, std::memory_order_release);
	wake.notify_all();
	idle.notify_all();
	if (worker.joinable())
		worker.join();
	started.store(false, std::memory_order_release);

	DX12LogManager::JsonFunc("DX12BindingCollectQueue",
		"\"event\":\"Stopped\",\"pending\":%zu,\"enqueued\":%llu,\"dequeued\":%llu,\"completed\":%llu,\"maxDepth\":%llu,\"drainCalls\":%llu,\"drainWaitUs\":%llu,\"drainMaxUs\":%llu,\"taskElapsedUs\":%llu,\"taskMaxUs\":%llu",
		PendingCount(),
		static_cast<unsigned long long>(enqueued.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(dequeued.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(completed.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(maxDepth.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(drainCalls.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(drainWaitUs.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(drainMaxUs.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(taskElapsedUs.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(taskMaxUs.load(std::memory_order_relaxed)));
}

void DX12BindingCollectQueue::DrainForSnapshot(const char *reason)
{
	WaitForIdle(reason ? reason : "Snapshot");
}

void DX12BindingCollectQueue::BeginFrameDrain()
{
	WaitForIdle("BeginFrame");
}

bool DX12BindingCollectQueue::EnqueueStateEvent(
	ID3D12GraphicsCommandList *commandList,
	const CommandListBindingState &state,
	const char *kind,
	UINT64 descriptorEpoch)
{
	DX12BindingCollectTask task;
	task.kind = DX12BindingCollectTaskKind::StateEvent;
	task.commandList = commandList;
	task.state = state;
	task.descriptorEpoch = descriptorEpoch;
	task.eventKind = kind;
	return EnqueueTask(std::move(task));
}

bool DX12BindingCollectQueue::EnqueueDrawEvent(
	ID3D12GraphicsCommandList *commandList,
	const CommandListBindingState &state,
	const char *kind,
	UINT64 descriptorEpoch,
	UINT vertexCountPerInstance,
	UINT indexCountPerInstance,
	UINT instanceCount,
	UINT startVertexLocation,
	UINT startIndexLocation,
	INT baseVertexLocation,
	UINT startInstanceLocation)
{
	DX12BindingCollectTask task;
	task.kind = DX12BindingCollectTaskKind::DrawEvent;
	task.commandList = commandList;
	task.state = state;
	task.descriptorEpoch = descriptorEpoch;
	task.eventKind = kind;
	task.vertexCountPerInstance = vertexCountPerInstance;
	task.indexCountPerInstance = indexCountPerInstance;
	task.instanceCount = instanceCount;
	task.startVertexLocation = startVertexLocation;
	task.startIndexLocation = startIndexLocation;
	task.baseVertexLocation = baseVertexLocation;
	task.startInstanceLocation = startInstanceLocation;
	task.drawId = state.drawSerial;
	return EnqueueTask(std::move(task));
}

bool DX12BindingCollectQueue::EnqueueDispatchEvent(
	ID3D12GraphicsCommandList *commandList,
	const CommandListBindingState &state,
	UINT64 descriptorEpoch,
	UINT threadGroupCountX,
	UINT threadGroupCountY,
	UINT threadGroupCountZ)
{
	DX12BindingCollectTask task;
	task.kind = DX12BindingCollectTaskKind::DispatchEvent;
	task.commandList = commandList;
	task.state = state;
	task.descriptorEpoch = descriptorEpoch;
	task.eventKind = "dispatch";
	task.threadGroupCountX = threadGroupCountX;
	task.threadGroupCountY = threadGroupCountY;
	task.threadGroupCountZ = threadGroupCountZ;
	return EnqueueTask(std::move(task));
}

bool DX12BindingCollectQueue::EnqueueOutputEvent(
	ID3D12GraphicsCommandList *commandList,
	const CommandListBindingState &state,
	const char *kind,
	UINT slot,
	SIZE_T cpuHandle,
	UINT64 drawId)
{
	DX12BindingCollectTask task;
	task.kind = DX12BindingCollectTaskKind::OutputEvent;
	task.commandList = commandList;
	task.state = state;
	task.eventKind = kind;
	task.slot = slot;
	task.cpuHandle = cpuHandle;
	task.drawId = drawId;
	return EnqueueTask(std::move(task));
}

size_t DX12BindingCollectQueue::PendingCount()
{
	std::lock_guard<std::mutex> lock(mutex);
	return tasks.size() + static_cast<size_t>(activeTasks);
}

bool DX12BindingCollectQueue::EnqueueTask(DX12BindingCollectTask &&task)
{
	if (!task.commandList)
		return false;

	Start();
	task.sequence = nextSequence.fetch_add(1, std::memory_order_relaxed);
	task.enqueueThreadId = GetCurrentThreadId();
	if (task.kind != DX12BindingCollectTaskKind::OutputEvent)
		DX12BindingTracker::RetainStateIaResourcesForAsync(&task.state);
	const uint64_t sequence = task.sequence;

	size_t depth = 0;
	{
		std::lock_guard<std::mutex> lock(mutex);
		tasks.push_back(std::move(task));
		depth = tasks.size() + static_cast<size_t>(activeTasks);
	}
	enqueued.fetch_add(1, std::memory_order_relaxed);
	UpdateMaxDepth(depth);
	LogStats("Enqueue", sequence, depth);
	wake.notify_one();
	return true;
}

void DX12BindingCollectQueue::WorkerLoop()
{
	DX12LogManager::JsonFunc("DX12BindingCollectQueue",
		"\"event\":\"WorkerEnter\",\"threadId\":%lu",
		GetCurrentThreadId());

	for (;;) {
		DX12BindingCollectTask task;
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
			task = std::move(tasks.front());
			tasks.pop_front();
			activeTasks++;
			depthAfterPop = tasks.size() + static_cast<size_t>(activeTasks);
		}
		dequeued.fetch_add(1, std::memory_order_relaxed);
		if (depthAfterPop > 0)
			LogStats("Dequeue", task.sequence, depthAfterPop);
		ProcessTask(&task, "Worker");
		{
			std::lock_guard<std::mutex> lock(mutex);
			if (activeTasks > 0)
				activeTasks--;
		}
		completed.fetch_add(1, std::memory_order_relaxed);
		idle.notify_all();
	}

	idle.notify_all();
	DX12LogManager::JsonFunc("DX12BindingCollectQueue",
		"\"event\":\"WorkerExit\",\"threadId\":%lu,\"pending\":%zu",
		GetCurrentThreadId(), PendingCount());
}

void DX12BindingCollectQueue::WaitForIdle(const char *reason)
{
	const auto begin = std::chrono::steady_clock::now();
	size_t pendingSnapshot = 0;
	{
		std::unique_lock<std::mutex> lock(mutex);
		pendingSnapshot = tasks.size() + static_cast<size_t>(activeTasks);
		UpdateMaxPendingAtDrain(pendingSnapshot);
		if (pendingSnapshot != 0) {
			idle.wait(lock, []() {
				return tasks.empty() && activeTasks == 0;
			});
		}
	}
	const auto end = std::chrono::steady_clock::now();
	const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count();
	drainCalls.fetch_add(1, std::memory_order_relaxed);
	RecordDrainWaitStats(elapsedUs);
	DX12Profiling::RecordBindingQueueDrainUs(static_cast<LONG64>(elapsedUs), static_cast<LONG>(pendingSnapshot));
	if (elapsedUs >= kBindingQueueSlowDrainUs) {
		DX12LogManager::JsonFunc("DX12BindingCollectQueueDrain",
			"\"reason\":\"%s\",\"pending\":%zu,\"elapsedUs\":%lld",
			reason ? reason : "",
			pendingSnapshot,
			static_cast<long long>(elapsedUs));
	}
}

void DX12BindingCollectQueue::ProcessTask(DX12BindingCollectTask *task, const char *source)
{
	if (!task)
		return;

	const auto begin = std::chrono::steady_clock::now();
	switch (task->kind) {
	case DX12BindingCollectTaskKind::StateEvent:
		ProcessStateEventTask(task, source);
		break;
	case DX12BindingCollectTaskKind::DrawEvent:
		ProcessDrawEventTask(task, source);
		break;
	case DX12BindingCollectTaskKind::DispatchEvent:
		ProcessDispatchEventTask(task, source);
		break;
	case DX12BindingCollectTaskKind::OutputEvent:
		ProcessOutputEventTask(task, source);
		break;
	default:
		break;
	}
	const auto end = std::chrono::steady_clock::now();
	const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count();
	RecordTaskStats(elapsedUs);
	DX12Profiling::RecordBindingQueueTaskUs(static_cast<LONG64>(elapsedUs));
	if (task->kind != DX12BindingCollectTaskKind::OutputEvent)
		DX12BindingTracker::ReleaseRetainedIaResources(&task->state);
	if (elapsedUs >= kBindingQueueSlowTaskUs) {
		DX12LogManager::JsonFunc("DX12BindingCollectQueueSlowTask",
			"\"source\":\"%s\",\"seq\":%llu,\"kind\":%u,\"elapsedUs\":%lld,\"pending\":%zu",
			source ? source : "",
			static_cast<unsigned long long>(task->sequence),
			static_cast<unsigned>(task->kind),
			static_cast<long long>(elapsedUs),
			PendingCount());
	}
}

void DX12BindingCollectQueue::ProcessStateEventTask(DX12BindingCollectTask *task, const char *source)
{
	(void)source;
	if (!task)
		return;

	BindingEvent event;
	if (!DX12BindingTracker::BuildEvent(
			&event,
			task->eventKind ? task->eventKind : "state",
			task->commandList,
			task->state,
			task->descriptorEpoch))
		return;

	AcquireSRWLockExclusive(&DX12BindingTrackerState::EventsLock);
	DX12BindingTracker::StoreEventLocked(event);
	ReleaseSRWLockExclusive(&DX12BindingTrackerState::EventsLock);
}

void DX12BindingCollectQueue::ProcessDrawEventTask(DX12BindingCollectTask *task, const char *source)
{
	(void)source;
	if (!task)
		return;

	BindingEvent event;
	if (!DX12BindingTracker::BuildEvent(
			&event,
			task->eventKind ? task->eventKind : "draw",
			task->commandList,
			task->state,
			task->descriptorEpoch))
		return;
	event.vertexCountPerInstance = task->vertexCountPerInstance;
	event.indexCountPerInstance = task->indexCountPerInstance;
	event.instanceCount = task->instanceCount;
	event.startVertexLocation = task->startVertexLocation;
	event.startIndexLocation = task->startIndexLocation;
	event.baseVertexLocation = task->baseVertexLocation;
	event.startInstanceLocation = task->startInstanceLocation;

	AcquireSRWLockExclusive(&DX12BindingTrackerState::EventsLock);
	DX12BindingTracker::StoreEventLocked(event);
	DX12BindingTracker::StoreCurrentOutputTargetsLocked(task->commandList, task->state);
	ReleaseSRWLockExclusive(&DX12BindingTrackerState::EventsLock);
}

void DX12BindingCollectQueue::ProcessDispatchEventTask(DX12BindingCollectTask *task, const char *source)
{
	(void)source;
	if (!task)
		return;

	BindingEvent event;
	if (!DX12BindingTracker::BuildEvent(
			&event,
			task->eventKind ? task->eventKind : "dispatch",
			task->commandList,
			task->state,
			task->descriptorEpoch))
		return;
	event.threadGroupCountX = task->threadGroupCountX;
	event.threadGroupCountY = task->threadGroupCountY;
	event.threadGroupCountZ = task->threadGroupCountZ;

	AcquireSRWLockExclusive(&DX12BindingTrackerState::EventsLock);
	DX12BindingTracker::StoreEventLocked(event);
	ReleaseSRWLockExclusive(&DX12BindingTrackerState::EventsLock);
}

void DX12BindingCollectQueue::ProcessOutputEventTask(DX12BindingCollectTask *task, const char *source)
{
	(void)source;
	if (!task || !task->cpuHandle)
		return;

	AcquireSRWLockExclusive(&DX12BindingTrackerState::EventsLock);
	DX12BindingTracker::StoreOutputEventLocked(
		task->eventKind ? task->eventKind : "",
		task->commandList,
		task->state,
		task->slot,
		task->cpuHandle,
		task->drawId);
	ReleaseSRWLockExclusive(&DX12BindingTrackerState::EventsLock);
}

void DX12BindingCollectQueue::UpdateMaxDepth(size_t depth)
{
	uint64_t current = maxDepth.load(std::memory_order_relaxed);
	while (depth > current &&
		!maxDepth.compare_exchange_weak(current, depth, std::memory_order_relaxed)) {
	}
}

void DX12BindingCollectQueue::UpdateMaxPendingAtDrain(size_t depth)
{
	uint64_t current = maxPendingAtDrain.load(std::memory_order_relaxed);
	while (depth > current &&
		!maxPendingAtDrain.compare_exchange_weak(current, depth, std::memory_order_relaxed)) {
	}
}

void DX12BindingCollectQueue::RecordTaskStats(int64_t elapsedUs)
{
	if (elapsedUs <= 0)
		return;
	taskElapsedUs.fetch_add(static_cast<uint64_t>(elapsedUs), std::memory_order_relaxed);
	uint64_t currentMax = taskMaxUs.load(std::memory_order_relaxed);
	while (static_cast<uint64_t>(elapsedUs) > currentMax &&
		!taskMaxUs.compare_exchange_weak(currentMax, static_cast<uint64_t>(elapsedUs), std::memory_order_relaxed)) {
	}
}

void DX12BindingCollectQueue::RecordDrainWaitStats(int64_t elapsedUs)
{
	if (elapsedUs <= 0)
		return;
	drainWaitUs.fetch_add(static_cast<uint64_t>(elapsedUs), std::memory_order_relaxed);
	uint64_t currentMax = drainMaxUs.load(std::memory_order_relaxed);
	while (static_cast<uint64_t>(elapsedUs) > currentMax &&
		!drainMaxUs.compare_exchange_weak(currentMax, static_cast<uint64_t>(elapsedUs), std::memory_order_relaxed)) {
	}
}

void DX12BindingCollectQueue::LogStats(const char *event, uint64_t sequence, size_t depth)
{
	const uint64_t serial = dequeued.load(std::memory_order_relaxed) +
		enqueued.load(std::memory_order_relaxed);
	if (depth < 256 && sequence != 0 && (serial % kBindingQueueStatsInterval) != 0)
		return;

	DX12LogManager::JsonFunc("DX12BindingCollectQueueStats",
		"\"event\":\"%s\",\"seq\":%llu,\"pending\":%zu,\"enqueued\":%llu,\"dequeued\":%llu,\"completed\":%llu,\"maxDepth\":%llu,\"maxPendingAtDrain\":%llu,\"drainCalls\":%llu,\"drainWaitUs\":%llu,\"drainMaxUs\":%llu,\"taskElapsedUs\":%llu,\"taskMaxUs\":%llu",
		event ? event : "",
		static_cast<unsigned long long>(sequence),
		depth,
		static_cast<unsigned long long>(enqueued.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(dequeued.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(completed.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(maxDepth.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(maxPendingAtDrain.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(drainCalls.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(drainWaitUs.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(drainMaxUs.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(taskElapsedUs.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(taskMaxUs.load(std::memory_order_relaxed)));
}
