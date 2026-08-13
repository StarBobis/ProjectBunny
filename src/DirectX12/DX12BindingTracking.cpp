#include "DX12Core.h"
#include "DX12Hooks.h"
#include "DX12Tracking.h"
#include "DX12Runtime.h"
#include "DX12Resources.h"
#include "DX12Presentation.h"
#include <chrono>
#include <stdio.h>
#include <string.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

SRWLOCK DX12CaptureLedger::Lock = SRWLOCK_INIT;
bool DX12CaptureLedger::Active = false;
UINT64 DX12CaptureLedger::BeginPresent = 0;
UINT64 DX12CaptureLedger::LedgerEpoch = 0;
UINT64 DX12CaptureLedger::CaptureSessionId = 0;
UINT64 DX12CaptureLedger::EventSerial = 0;
UINT64 DX12CaptureLedger::OutputEventSerial = 0;
UINT64 DX12CaptureLedger::DroppedEvents = 0;
UINT64 DX12CaptureLedger::DroppedOutputEvents = 0;
std::vector<BindingEvent> DX12CaptureLedger::Events;
std::vector<OutputBindingEvent> DX12CaptureLedger::OutputEvents;
std::unordered_set<DX12CaptureOutputKey, DX12CaptureOutputKeyHash>
	DX12CaptureLedger::OutputKeys;

void DX12CaptureLedger::BeginCapture(UINT64 present)
{
	AcquireSRWLockExclusive(&Lock);
	DX12CaptureLedger::ReleaseEventsLocked();
	Events.clear();
	OutputEvents.clear();
	OutputKeys.clear();
	DroppedEvents = 0;
	DroppedOutputEvents = 0;
	EventSerial = 0;
	OutputEventSerial = 0;
	BeginPresent = present;
	++LedgerEpoch;
	++CaptureSessionId;
	Active = true;
	const UINT64 epoch = LedgerEpoch;
	const UINT64 captureSessionId = CaptureSessionId;
	ReleaseSRWLockExclusive(&Lock);

	DX12FrameAnalysis::LogJsonFunc("CaptureLedgerBegin",
		"\"present\":%llu,\"epoch\":%llu,\"captureSessionId\":%llu",
		static_cast<unsigned long long>(present),
		static_cast<unsigned long long>(epoch),
		static_cast<unsigned long long>(captureSessionId));
}

void DX12CaptureLedger::EndCapture()
{
	AcquireSRWLockExclusive(&Lock);
	Active = false;
	const size_t events = Events.size();
	const size_t outputs = OutputEvents.size();
	const UINT64 droppedEvents = DroppedEvents;
	const UINT64 droppedOutputs = DroppedOutputEvents;
	const UINT64 epoch = LedgerEpoch;
	const UINT64 captureSessionId = CaptureSessionId;
	ReleaseSRWLockExclusive(&Lock);

	DX12FrameAnalysis::LogJsonFunc("CaptureLedgerEnd",
		"\"epoch\":%llu,\"captureSessionId\":%llu,\"events\":%zu,\"outputs\":%zu,\"dropped_events\":%llu,\"dropped_outputs\":%llu",
		static_cast<unsigned long long>(epoch), events, outputs,
		static_cast<unsigned long long>(captureSessionId),
		static_cast<unsigned long long>(droppedEvents),
		static_cast<unsigned long long>(droppedOutputs));
}

void DX12CaptureLedger::Clear()
{
	AcquireSRWLockExclusive(&Lock);
	DX12CaptureLedger::ReleaseEventsLocked();
	Events.clear();
	OutputEvents.clear();
	OutputKeys.clear();
	DroppedEvents = 0;
	DroppedOutputEvents = 0;
	EventSerial = 0;
	OutputEventSerial = 0;
	Active = false;
	ReleaseSRWLockExclusive(&Lock);
}

bool DX12CaptureLedger::IsActive()
{
	AcquireSRWLockShared(&Lock);
	const bool active = Active;
	ReleaseSRWLockShared(&Lock);
	return active;
}

UINT64 DX12CaptureLedger::CurrentCaptureSessionId()
{
	AcquireSRWLockShared(&Lock);
	const UINT64 captureSessionId = Active ? CaptureSessionId : 0;
	ReleaseSRWLockShared(&Lock);
	return captureSessionId;
}

UINT64 DX12CaptureLedger::LastCaptureSessionId()
{
	AcquireSRWLockShared(&Lock);
	const UINT64 captureSessionId = CaptureSessionId;
	ReleaseSRWLockShared(&Lock);
	return captureSessionId;
}

bool DX12CaptureLedger::IsSessionCurrent(UINT64 captureSessionId)
{
	if (captureSessionId == 0)
		return false;
	AcquireSRWLockShared(&Lock);
	const bool current = Active && CaptureSessionId == captureSessionId;
	ReleaseSRWLockShared(&Lock);
	return current;
}

DX12CaptureLedgerRecordResult DX12CaptureLedger::RecordEvent(BindingEvent *event)
{
	if (!event)
		return DX12CaptureLedgerRecordResult::NotActive;

	AcquireSRWLockExclusive(&Lock);
	if (!Active) {
		ReleaseSRWLockExclusive(&Lock);
		return DX12CaptureLedgerRecordResult::NotActive;
	}
	if (event->captureSessionId == 0 || event->captureSessionId != CaptureSessionId) {
		ReleaseSRWLockExclusive(&Lock);
		return DX12CaptureLedgerRecordResult::InactiveSession;
	}
	event->serial = ++EventSerial;
	Events.push_back(std::move(*event));
	ReleaseSRWLockExclusive(&Lock);
	return DX12CaptureLedgerRecordResult::Stored;
}

DX12CaptureLedgerRecordResult DX12CaptureLedger::TryStoreOutputEvent(OutputBindingEvent *event)
{
	if (!event)
		return DX12CaptureLedgerRecordResult::NotActive;

	AcquireSRWLockExclusive(&Lock);
	if (!Active) {
		ReleaseSRWLockExclusive(&Lock);
		return DX12CaptureLedgerRecordResult::NotActive;
	}
	if (event->captureSessionId == 0 || event->captureSessionId != CaptureSessionId) {
		ReleaseSRWLockExclusive(&Lock);
		return DX12CaptureLedgerRecordResult::InactiveSession;
	}
	event->serial = ++OutputEventSerial;
	DX12CaptureOutputKey key;
	if (event->drawId != 0) {
		key.commandList = event->commandList;
		key.cpuHandle = event->cpuHandle;
		key.kind = event->kind;
		key.slot = event->slot;
		key.drawId = event->drawId;
		key.dispatchId = event->dispatchId;
		if (OutputKeys.find(key) != OutputKeys.end()) {
			ReleaseSRWLockExclusive(&Lock);
			return DX12CaptureLedgerRecordResult::Duplicate;
		}
	}
	OutputEvents.push_back(std::move(*event));
	if (key.drawId != 0)
		OutputKeys.insert(std::move(key));
	ReleaseSRWLockExclusive(&Lock);
	return DX12CaptureLedgerRecordResult::Stored;
}

void DX12CaptureLedger::GetSnapshot(DX12CaptureLedgerSnapshot *snapshot)
{
	if (!snapshot)
		return;
	AcquireSRWLockShared(&Lock);
	snapshot->events = Events;
	snapshot->outputEvents = OutputEvents;
	snapshot->droppedEvents = DroppedEvents;
	snapshot->droppedOutputEvents = DroppedOutputEvents;
	snapshot->beginPresent = BeginPresent;
	snapshot->ledgerEpoch = LedgerEpoch;
	snapshot->captureSessionId = CaptureSessionId;
	ReleaseSRWLockShared(&Lock);
}

void DX12CaptureLedger::ReleaseEventsLocked()
{
	for (BindingEvent &event : Events)
		DX12BindingTracker::ReleaseEventIaResources(&event);
}

void DX12CaptureLedger::LogSnapshotHealth(const char *stage, const DX12CaptureLedgerSnapshot &snapshot)
{
	size_t draws = 0;
	size_t dispatches = 0;
	for (const BindingEvent &event : snapshot.events) {
		if (event.kind == "draw" || event.kind == "draw_indexed")
			draws++;
		else if (event.kind == "dispatch")
			dispatches++;
	}
	DX12FrameAnalysis::LogJsonFunc(stage ? stage : "CaptureLedgerSnapshot",
		"\"epoch\":%llu,\"captureSessionId\":%llu,\"begin_present\":%llu,\"events\":%zu,\"draws\":%zu,\"dispatches\":%zu,\"outputs\":%zu,\"dropped_events\":%llu,\"dropped_outputs\":%llu",
		static_cast<unsigned long long>(snapshot.ledgerEpoch),
		static_cast<unsigned long long>(snapshot.captureSessionId),
		static_cast<unsigned long long>(snapshot.beginPresent),
		snapshot.events.size(), draws, dispatches, snapshot.outputEvents.size(),
		static_cast<unsigned long long>(snapshot.droppedEvents),
		static_cast<unsigned long long>(snapshot.droppedOutputEvents));
}




static constexpr uint64_t kBindingQueueStatsInterval = 4096;
static constexpr int64_t kBindingQueueSlowTaskUs = 1000;
static constexpr int64_t kBindingQueueSlowDrainUs = 1000;

void DX12BindingCollectQueue::Start()
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
	worker = std::thread(&DX12BindingCollectQueue::WorkerLoop);
	DX12LogManager::Fields("DX12BindingCollectQueue",
		"event=Start,mode=async");
}


void DX12BindingCollectQueue::Stop()
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
	UINT64 descriptorEpoch,
	UINT64 captureSessionId)
{
	DX12BindingCollectTask task;
	task.kind = DX12BindingCollectTaskKind::StateEvent;
	task.commandList = commandList;
	task.state = state;
	task.eventKind = kind;
	task.descriptorEpoch = descriptorEpoch;
	task.captureSessionId = captureSessionId;
	DX12ResidentMaintenanceTelemetryUtils::RecordBindingEventRecorded();
	DX12ResidentMaintenanceTelemetryUtils::RecordDescriptorEpochWrite();
	return EnqueueTask(std::move(task));
}

bool DX12BindingCollectQueue::EnqueueDrawEvent(
	ID3D12GraphicsCommandList *commandList,
	const CommandListBindingState &state,
	const char *kind,
	UINT64 descriptorEpoch,
	UINT64 captureSessionId,
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
	task.eventKind = kind;
	task.descriptorEpoch = descriptorEpoch;
	task.captureSessionId = captureSessionId;
	DX12ResidentMaintenanceTelemetryUtils::RecordBindingEventRecorded();
	DX12ResidentMaintenanceTelemetryUtils::RecordDescriptorEpochWrite();
	task.vertexCountPerInstance = vertexCountPerInstance;
	task.indexCountPerInstance = indexCountPerInstance;
	task.instanceCount = instanceCount;
	task.startVertexLocation = startVertexLocation;
	task.startIndexLocation = startIndexLocation;
	task.baseVertexLocation = baseVertexLocation;
	task.startInstanceLocation = startInstanceLocation;
	return EnqueueTask(std::move(task));
}

bool DX12BindingCollectQueue::EnqueueDispatchEvent(
	ID3D12GraphicsCommandList *commandList,
	const CommandListBindingState &state,
	UINT64 descriptorEpoch,
	UINT64 captureSessionId,
	UINT threadGroupCountX,
	UINT threadGroupCountY,
	UINT threadGroupCountZ)
{
	DX12BindingCollectTask task;
	task.kind = DX12BindingCollectTaskKind::DispatchEvent;
	task.commandList = commandList;
	task.state = state;
	task.eventKind = "dispatch";
	task.descriptorEpoch = descriptorEpoch;
	task.captureSessionId = captureSessionId;
	DX12ResidentMaintenanceTelemetryUtils::RecordBindingEventRecorded();
	DX12ResidentMaintenanceTelemetryUtils::RecordDescriptorEpochWrite();
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
	UINT64 drawId,
	UINT64 captureSessionId)
{
	DX12BindingCollectTask task;
	task.kind = DX12BindingCollectTaskKind::OutputEvent;
	task.commandList = commandList;
	task.state = state;
	task.eventKind = kind;
	task.slot = slot;
	task.cpuHandle = cpuHandle;
	task.drawId = drawId;
	task.captureSessionId = captureSessionId;
	return EnqueueTask(std::move(task));
}

size_t DX12BindingCollectQueue::PendingCount()
{
	std::lock_guard<std::mutex> lock(mutex);
	return tasks.size() + static_cast<size_t>(activeTasks);
}

UINT64 DX12BindingCollectQueue::LastSubmittedSequence()
{
	const uint64_t next = nextSequence.load(std::memory_order_acquire);
	if (next == 0)
		return 0;
	return next - 1;
}

bool DX12BindingCollectQueue::EnqueueTask(DX12BindingCollectTask &&task)
{
	if (!task.commandList)
		return false;
	if (!started.load(std::memory_order_acquire))
		return false;

	task.enqueueThreadId = GetCurrentThreadId();
	task.sequence = nextSequence.fetch_add(1, std::memory_order_relaxed);
	const UINT64 sequence = task.sequence;
	DX12BindingTracker::ResolvePipelineShaderInfoOnDemand(&task.state);
	DX12BindingTracker::RetainStateIaResourcesForAsync(&task.state);

	size_t depth = 0;
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (!started.load(std::memory_order_acquire) ||
			stopRequested.load(std::memory_order_acquire)) {
			DX12BindingTracker::ReleaseRetainedIaResources(&task.state);
			return false;
		}
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
	DX12LogManager::Fields("DX12BindingCollectQueue",
		"event=WorkerEnter,threadId=%lu",
		GetCurrentThreadId());

	for (;;) {
		DX12BindingCollectTask task;
		size_t depthAfterPop = 0;
		{
			std::unique_lock<std::mutex> lock(mutex);
			LARGE_INTEGER waitBegin = DX12OuterTimingProbeUtils::Now();
			wake.wait(lock, []() {
				return stopRequested.load(std::memory_order_acquire) || !tasks.empty();
			});
			DX12OuterTimingProbeUtils::LogWorkerWait(
				"BindingCollectQueue",
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
		completedSequence.store(task.sequence, std::memory_order_release);
		idle.notify_all();
	}

	idle.notify_all();
	DX12LogManager::Fields("DX12BindingCollectQueue",
		"event=WorkerExit,threadId=%lu,pending=%zu",
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
	DX12OuterTimingProbeUtils::LogWorkerWait(
		"BindingCollectQueue",
		reason ? reason : "",
		"idle",
		0,
		completedSequence.load(std::memory_order_acquire),
		pendingSnapshot,
		static_cast<LONG64>(elapsedUs));
	drainCalls.fetch_add(1, std::memory_order_relaxed);
	RecordDrainWaitStats(elapsedUs);
	DX12Profiling::RecordBindingQueueDrainUs(static_cast<LONG64>(elapsedUs), static_cast<LONG>(pendingSnapshot));
	if (elapsedUs >= kBindingQueueSlowDrainUs) {
		DX12LogManager::Fields("DX12BindingCollectQueueDrain",
			"reason=%s,pending=%zu,elapsedUs=%lld",
			reason ? reason : "",
			pendingSnapshot,
			static_cast<long long>(elapsedUs));
	}
}

void DX12BindingCollectQueue::WaitForSubmittedSequence(UINT64 targetSequence, const char *reason)
{
	if (targetSequence == 0)
		return;

	const auto begin = std::chrono::steady_clock::now();
	size_t pendingSnapshot = 0;
	{
		std::unique_lock<std::mutex> lock(mutex);
		pendingSnapshot = tasks.size() + static_cast<size_t>(activeTasks);
		UpdateMaxPendingAtDrain(pendingSnapshot);
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
		"BindingCollectQueue",
		reason ? reason : "",
		"sequence",
		targetSequence,
		completedSequence.load(std::memory_order_acquire),
		pendingSnapshot,
		static_cast<LONG64>(elapsedUs));
	drainCalls.fetch_add(1, std::memory_order_relaxed);
	RecordDrainWaitStats(elapsedUs);
	DX12Profiling::RecordBindingQueueDrainUs(static_cast<LONG64>(elapsedUs), static_cast<LONG>(pendingSnapshot));
	if (elapsedUs >= kBindingQueueSlowDrainUs) {
		DX12LogManager::Fields("DX12BindingCollectQueueDrain",
			"reason=%s,mode=sequence,targetSequence=%llu,completedSequence=%llu,pending=%zu,elapsedUs=%lld",
			reason ? reason : "",
			static_cast<unsigned long long>(targetSequence),
			static_cast<unsigned long long>(completedSequence.load(std::memory_order_acquire)),
			pendingSnapshot,
			static_cast<long long>(elapsedUs));
	}
}

void DX12BindingCollectQueue::ProcessTask(DX12BindingCollectTask *task, const char *source)
{
	if (!task)
		return;

	const UINT taskKind = static_cast<UINT>(task->kind);
	const UINT64 sequence = task->sequence;
	const DWORD enqueueThreadId = task->enqueueThreadId;
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
	DX12OuterTimingProbeUtils::LogWorkerTask(
		"BindingCollectQueue",
		"process_task",
		taskKind,
		sequence,
		1,
		PendingCount(),
		enqueueThreadId,
		static_cast<LONG64>(elapsedUs));
	RecordTaskStats(elapsedUs);
	DX12Profiling::RecordBindingQueueTaskUs(static_cast<LONG64>(elapsedUs));
	if (task->kind != DX12BindingCollectTaskKind::OutputEvent)
		DX12BindingTracker::ReleaseRetainedIaResources(&task->state);
	if (elapsedUs >= kBindingQueueSlowTaskUs) {
		DX12LogManager::Fields("DX12BindingCollectQueueSlowTask",
			"source=%s,seq=%llu,captureSessionId=%llu,kind=%u,elapsedUs=%lld,pending=%zu",
			source ? source : "",
			static_cast<unsigned long long>(task->sequence),
			static_cast<unsigned long long>(task->captureSessionId),
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
	LARGE_INTEGER buildBegin = DX12OuterTimingProbeUtils::Now();
	if (!DX12BindingTracker::BuildEvent(
			&event,
			task->eventKind ? task->eventKind : "state",
			task->commandList,
			task->state,
			task->descriptorEpoch,
			task->captureSessionId))
		return;
	DX12OuterTimingProbeUtils::LogCpuStage(
		"BindingCollectQueue",
		"build_state_event",
		static_cast<UINT>(task->kind),
		1,
		DX12OuterTimingProbeUtils::ElapsedUs(buildBegin));

	LARGE_INTEGER storeBegin = DX12OuterTimingProbeUtils::Now();
	DX12BindingTracker::StoreEventLocked(event);
	DX12OuterTimingProbeUtils::LogCpuStage(
		"BindingCollectQueue",
		"store_state_event",
		static_cast<UINT>(task->kind),
		1,
		DX12OuterTimingProbeUtils::ElapsedUs(storeBegin));
}

void DX12BindingCollectQueue::ProcessDrawEventTask(DX12BindingCollectTask *task, const char *source)
{
	(void)source;
	if (!task)
		return;

	BindingEvent event;
	LARGE_INTEGER buildBegin = DX12OuterTimingProbeUtils::Now();
	if (!DX12BindingTracker::BuildEvent(
			&event,
			task->eventKind ? task->eventKind : "draw",
			task->commandList,
			task->state,
			task->descriptorEpoch,
			task->captureSessionId))
		return;
	DX12OuterTimingProbeUtils::LogCpuStage(
		"BindingCollectQueue",
		"build_draw_event",
		static_cast<UINT>(task->kind),
		1,
		DX12OuterTimingProbeUtils::ElapsedUs(buildBegin));
	event.vertexCountPerInstance = task->vertexCountPerInstance;
	event.indexCountPerInstance = task->indexCountPerInstance;
	event.instanceCount = task->instanceCount;
	event.startVertexLocation = task->startVertexLocation;
	event.startIndexLocation = task->startIndexLocation;
	event.baseVertexLocation = task->baseVertexLocation;
	event.startInstanceLocation = task->startInstanceLocation;

	LARGE_INTEGER storeBegin = DX12OuterTimingProbeUtils::Now();
	DX12BindingTracker::StoreEventLocked(event);
	DX12OuterTimingProbeUtils::LogCpuStage(
		"BindingCollectQueue",
		"store_draw_event",
		static_cast<UINT>(task->kind),
		1,
		DX12OuterTimingProbeUtils::ElapsedUs(storeBegin));
	LARGE_INTEGER outputBegin = DX12OuterTimingProbeUtils::Now();
	DX12BindingTracker::StoreCurrentOutputTargetsLocked(task->commandList, task->state);
	DX12OuterTimingProbeUtils::LogCpuStage(
		"BindingCollectQueue",
		"store_draw_outputs",
		static_cast<UINT>(task->kind),
		1,
		DX12OuterTimingProbeUtils::ElapsedUs(outputBegin));
}

void DX12BindingCollectQueue::ProcessDispatchEventTask(DX12BindingCollectTask *task, const char *source)
{
	(void)source;
	if (!task)
		return;

	BindingEvent event;
	LARGE_INTEGER buildBegin = DX12OuterTimingProbeUtils::Now();
	if (!DX12BindingTracker::BuildEvent(
			&event,
			task->eventKind ? task->eventKind : "dispatch",
			task->commandList,
			task->state,
			task->descriptorEpoch,
			task->captureSessionId))
		return;
	DX12OuterTimingProbeUtils::LogCpuStage(
		"BindingCollectQueue",
		"build_dispatch_event",
		static_cast<UINT>(task->kind),
		1,
		DX12OuterTimingProbeUtils::ElapsedUs(buildBegin));
	event.threadGroupCountX = task->threadGroupCountX;
	event.threadGroupCountY = task->threadGroupCountY;
	event.threadGroupCountZ = task->threadGroupCountZ;

	LARGE_INTEGER storeBegin = DX12OuterTimingProbeUtils::Now();
	DX12BindingTracker::StoreEventLocked(event);
	DX12OuterTimingProbeUtils::LogCpuStage(
		"BindingCollectQueue",
		"store_dispatch_event",
		static_cast<UINT>(task->kind),
		1,
		DX12OuterTimingProbeUtils::ElapsedUs(storeBegin));
}

void DX12BindingCollectQueue::ProcessOutputEventTask(DX12BindingCollectTask *task, const char *source)
{
	(void)source;
	if (!task || !task->cpuHandle)
		return;

	LARGE_INTEGER storeBegin = DX12OuterTimingProbeUtils::Now();
	DX12BindingTracker::StoreOutputEventLocked(
		task->eventKind ? task->eventKind : "",
		task->commandList,
		task->state,
		task->slot,
		task->cpuHandle,
		task->drawId,
		task->captureSessionId);
	DX12OuterTimingProbeUtils::LogCpuStage(
		"BindingCollectQueue",
		"store_output_event",
		static_cast<UINT>(task->kind),
		1,
		DX12OuterTimingProbeUtils::ElapsedUs(storeBegin));
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

	DX12LogManager::Fields("DX12BindingCollectQueueStats",
		"event=%s,seq=%llu,pending=%zu,enqueued=%llu,dequeued=%llu,completed=%llu,completedSequence=%llu,maxDepth=%llu,maxPendingAtDrain=%llu,drainCalls=%llu,drainWaitUs=%llu,drainMaxUs=%llu,taskElapsedUs=%llu,taskMaxUs=%llu",
		event ? event : "",
		static_cast<unsigned long long>(sequence),
		depth,
		static_cast<unsigned long long>(enqueued.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(dequeued.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(completed.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(completedSequence.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(maxDepth.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(maxPendingAtDrain.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(drainCalls.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(drainWaitUs.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(drainMaxUs.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(taskElapsedUs.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(taskMaxUs.load(std::memory_order_relaxed)));
}




FILE *DX12LogUtils::LogFile = nullptr;

BindingShard DX12BindingTrackerState::Shards[kBindingShardCount];
volatile LONG64 DX12BindingTrackerState::GlobalDrawSerial = 0;
volatile LONG64 DX12BindingTrackerState::GlobalDispatchSerial = 0;

static thread_local BindingTlsTableCache tlsGraphicsTableCache;
static thread_local BindingTlsTableCache tlsComputeTableCache;
static thread_local BindingTlsDescriptorCache tlsGraphicsDescCache;
static thread_local BindingTlsDescriptorCache tlsComputeDescCache;
static thread_local BindingTlsDescriptorQueryCache
	tlsDescriptorQueryCaches[BindingTlsDescriptorQueryCache::CacheSlotCount];
static thread_local BindingTlsComputeAllQueryCache tlsComputeAllQueryCache;
static thread_local BindingTlsStateCache tlsStateCache;

// Resolve CommandListBindingState pointer via thread-local cache.
// Eliminates ShardFor() hash + HashMap::operator[] on repeated calls
// for the same command list. Generation counter guards against pointer
// invalidation due to unordered_map rehash on other threads.

static CommandListBindingState* GetOrResolveBindingState(
	ID3D12GraphicsCommandList *commandList)
{
	// Fast path: TLS cache hit with matching shard generation
	if (tlsStateCache.commandList == commandList && tlsStateCache.state) {
		BindingShard &cachedShard = DX12BindingTrackerState::Shards[tlsStateCache.shardIndex];
		if (tlsStateCache.shardGeneration == cachedShard.generation) {
			return tlsStateCache.state;
		}
	}

	// Slow path: compute shard hash, lock, hashmap lookup
	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	CommandListBindingState &state = shard.states[commandList];
	tlsStateCache.commandList = commandList;
	tlsStateCache.state = &state;
	tlsStateCache.shardGeneration = shard.generation;
	tlsStateCache.shardIndex = static_cast<size_t>(&shard - DX12BindingTrackerState::Shards);
	ReleaseSRWLockExclusive(&shard.lock);
	return &state;
}

static UINT64 MixPreSkinDependencyFingerprint(UINT64 fingerprint, UINT64 value)
{
	return fingerprint ^ (value + 0x9e3779b97f4a7c15ull +
		(fingerprint << 6) + (fingerprint >> 2));
}

static UINT64 BuildRuntimePreSkinDependencyFingerprint(
	UINT64 computeBindingSerial,
	ID3D12DescriptorHeap *heap,
	const UINT64 *slotVersions,
	const UINT64 *slotValueIds,
	size_t slotCount)
{
	UINT64 fingerprint = 14695981039346656037ull;
	fingerprint = MixPreSkinDependencyFingerprint(fingerprint, computeBindingSerial);
	fingerprint = MixPreSkinDependencyFingerprint(
		fingerprint,
		static_cast<UINT64>(reinterpret_cast<uintptr_t>(heap)));
	for (size_t index = 0; index < slotCount; ++index) {
		fingerprint = MixPreSkinDependencyFingerprint(fingerprint, slotVersions[index]);
		fingerprint = MixPreSkinDependencyFingerprint(fingerprint, slotValueIds[index]);
	}
	return fingerprint ? fingerprint : 1;
}

static bool RuntimeSlotIdentityMatches(
	UINT64 currentVersion,
	UINT64 currentValueId,
	UINT64 cachedVersion,
	UINT64 cachedValueId)
{
	// A stable descriptor Value ID survives repeated copies of the same
	// value, while the mutation version churns whenever the game rewrites
	// its descriptor arena (e.g. ZZZ re-copies ring descriptors every
	// frame). When the cache captured a non-zero value identity, the
	// semantic content is proven identical by the value id alone, so the
	// ring-slot version is allowed to rotate. Fall back to comparing both
	// version and value id when the cached slot has no authoritative value.
	if (cachedValueId != 0)
		return currentValueId == cachedValueId;
	return currentVersion == cachedVersion &&
		currentValueId == cachedValueId;
}

static bool RuntimeResolveHeapRecord(
	const RootTableState &table,
	UINT slot,
	DescriptorHeapRecord::StateLocks **stateLocks)
{
	if (!stateLocks ||
			!table.runtimeHeap || !table.runtimeHeapCpuStart ||
			table.runtimeHeapIncrement == 0 ||
			slot >= table.runtimeHeapNumDescriptors)
		return false;
	const SIZE_T cpuHandle = table.runtimeHeapCpuStart +
		static_cast<SIZE_T>(slot) * table.runtimeHeapIncrement;
	DX12DescriptorHeapShadowUtils::ResolvedHeapSlot resolved = {};
	if (!DX12DescriptorHeapShadowUtils::HeapCacheLookup(
			cpuHandle,
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
			&resolved) ||
			resolved.slot != slot ||
			!resolved.heap || !resolved.heap->stateLocks)
		return false;
	*stateLocks = resolved.heap->stateLocks.get();
	return true;
}

static bool RuntimeReadSlotIdentitiesFast(
	const DX12DescriptorHeapSummary &heapSummary,
	const UINT *slots,
	size_t count,
	UINT64 *versions,
	UINT64 *valueIds)
{
	if (!heapSummary.heap || !heapSummary.cpuStart ||
			heapSummary.increment == 0 || count == 0 ||
			!slots || !versions || !valueIds)
		return false;
	// Resolve the heap record once through the lock-free CPU-handle cache,
	// then read every slot directly through the per-slot seqlock. No global
	// descriptor lock is taken on this path.
	const SIZE_T firstCpu = heapSummary.cpuStart +
		static_cast<SIZE_T>(slots[0]) * heapSummary.increment;
	DX12DescriptorHeapShadowUtils::ResolvedHeapSlot resolved = {};
	if (!DX12DescriptorHeapShadowUtils::HeapCacheLookup(
			firstCpu,
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
			&resolved) ||
			!resolved.heap || !resolved.heap->stateLocks)
		return false;
	for (size_t index = 0; index < count; ++index) {
		const UINT slot = slots[index];
		if (slot >= heapSummary.numDescriptors)
			return false;
		DescriptorHeapRecord::StateLocks::SlotIdentity identity = {};
		if (!resolved.heap->stateLocks->ObserveSlotIdentity(
				slot, &identity))
			return false;
		versions[index] = identity.version;
		valueIds[index] = identity.valueId;
	}
	return true;
}

static bool RuntimeDescriptorRootsMatch(
	const DX12SimulateContext *runtimeState)
{
	if (!runtimeState)
		return false;
	const DX12SimulateContextComputeTemplate &cache =
		runtimeState->resolvedCompute;
	const bool checkAll = !runtimeState->computeRootUsedMaskValid;
	for (UINT root = 0; root < MaxRootParameters; ++root) {
		if (!checkAll &&
				!(runtimeState->computeRootUsedMask & (1ull << root)))
			continue;
		const RootDescriptorState &descriptor =
			runtimeState->computeRootDescriptors[root];
		const bool valid = descriptor.valid;
		if (valid != cache.bindingsRootValid[root])
			return false;
		if (valid && descriptor.address != cache.bindingsRootIdentities[root])
			return false;
	}
	return true;
}

static bool RuntimeConstantsMatch(
	const DX12SimulateContext *runtimeState)
{
	if (!runtimeState)
		return false;
	const DX12SimulateContextComputeTemplate &cache =
		runtimeState->resolvedCompute;
	const bool checkAll = !runtimeState->computeRootUsedMaskValid;
	// Every cached constant must still match the live root constant state.
	for (const DX12CurrentRootConstants &cached : cache.rootConstants) {
		const UINT root = cached.rootParameterIndex;
		if (root >= MaxRootParameters)
			return false;
		const RootConstantsState &current =
			runtimeState->computeRootConstants[root];
		if (!current.valid ||
				current.maxSet != cached.maxSet ||
				memcmp(current.values, cached.values,
					sizeof(current.values)) != 0 ||
				memcmp(current.valueValid, cached.valueValid,
					sizeof(current.valueValid)) != 0)
			return false;
	}
	// A used root that gained constants absent from the cache invalidates it.
	for (UINT root = 0; root < MaxRootParameters; ++root) {
		if (!checkAll &&
				!(runtimeState->computeRootUsedMask & (1ull << root)))
			continue;
		if (!runtimeState->computeRootConstants[root].valid)
			continue;
		bool found = false;
		for (const DX12CurrentRootConstants &cached : cache.rootConstants) {
			if (cached.rootParameterIndex == root) {
				found = true;
				break;
			}
		}
		if (!found)
			return false;
	}
	return true;
}

static void RebaseRuntimePreSkinBindings(
	DX12SimulateContext *runtimeState)
{
	if (!runtimeState)
		return;
	DX12SimulateContextComputeTemplate &cache =
		runtimeState->resolvedCompute;
	const auto rebase = [runtimeState](
		std::vector<DX12CurrentComputeUavBinding> &bindings) {
		for (DX12CurrentComputeUavBinding &binding : bindings) {
			if (!binding.hasDescriptor || binding.rootDescriptor ||
					binding.rootParameterIndex >= MaxRootParameters)
				continue;
			const RootTableState &table =
				runtimeState->computeRootTables[binding.rootParameterIndex];
			if (!table.valid || !table.runtimeHeapResolved ||
					table.runtimeHeapIncrement == 0)
				continue;
			binding.tableHeap = table.runtimeHeap;
			binding.tableCpuStart = table.runtimeTableCpuStart;
			binding.tableGpuStart.ptr = table.runtimeTableGpuStart;
			binding.descriptorIncrement = table.runtimeHeapIncrement;
			binding.descriptor.cpuHandle = table.runtimeTableCpuStart +
				static_cast<SIZE_T>(binding.descriptorOffset) *
					table.runtimeHeapIncrement;
		}
	};
	rebase(cache.uavs);
	rebase(cache.srvs);
	rebase(cache.cbvs);
}

static bool TryGetRuntimePreSkinBindings(
	DX12SimulateContext *runtimeState,
	UINT64 computeBindingSerial,
	const std::vector<DX12CurrentComputeUavBinding> **uavs,
	const std::vector<DX12CurrentComputeUavBinding> **srvs,
	const std::vector<DX12CurrentComputeUavBinding> **cbvs,
	const std::vector<DX12CurrentRootConstants> **constants,
	UINT64 *dependencyFingerprint,
	bool *foundAny)
{
	(void)computeBindingSerial;
	if (!runtimeState || !runtimeState->resolvedCompute.bindingsValid ||
			runtimeState->resolvedCompute.bindingsHeap != runtimeState->cbvSrvUavHeap ||
			runtimeState->resolvedCompute.bindingsRootSignature !=
				runtimeState->computeRootSignature)
		return false;

	// DX12-style semantic validation: root descriptors must match exactly
	// (a bare GPU VA has no value identity), root constants must match by
	// content, and descriptor-table content is proven by per-slot value
	// identities read at the CURRENT ring locations. The previous dirty-mask
	// / binding-serial checks invalidated on every re-bind even when the game
	// bound identical descriptors at a rotated ring position, forcing a full
	// re-collect per dispatch (DX11-style deferred state inspection).
	if (!RuntimeDescriptorRootsMatch(runtimeState) ||
			!RuntimeConstantsMatch(runtimeState))
		return false;

	DX12SimulateContextComputeTemplate &cache =
		runtimeState->resolvedCompute;
	const size_t observedCount = cache.bindingsObservedSlots.size();
	if (observedCount != cache.observedSlots.size() ||
			observedCount != cache.observedSlotVersions.size() ||
			observedCount != cache.observedSlotValueIds.size())
		return false;

	if (observedCount) {
		if (!cache.bindingsHeap)
			return false;
		// A rotated table invalidates the "no descriptor writes since last
		// check" shortcut: the new slots were never validated, so their
		// content must be read even when the global serial is unchanged.
		bool tablesUnchanged = true;
		for (size_t index = 0; index < observedCount; ++index) {
			const UINT root = cache.bindingsObservedSlots[index].root;
			if (root >= MaxRootParameters)
				return false;
			const RootTableState &table = runtimeState->computeRootTables[root];
			const UINT64 cachedBase = cache.bindingsRootTableBases[root];
			const UINT64 currentBase = table.valid ?
				table.baseDescriptor.ptr : 0;
			if (currentBase != cachedBase) {
				tablesUnchanged = false;
				break;
			}
		}
		const UINT64 descriptorQuerySerialBefore =
			CurrentDescriptorQuerySerial();
		if (cache.bindingsDescriptorQuerySerial !=
				descriptorQuerySerialBefore ||
				!tablesUnchanged) {
			std::array<DescriptorHeapRecord::StateLocks*,
				MaxRootParameters> stateLocksByRoot = {};
			for (size_t index = 0; index < observedCount; ++index) {
				const DX12SimulateContextComputeTemplate::RuntimeObservedSlot
					&observed = cache.bindingsObservedSlots[index];
				if (observed.root >= MaxRootParameters)
					return false;
				const RootTableState &table =
					runtimeState->computeRootTables[observed.root];
				if (!table.valid || !table.runtimeHeapResolved ||
						table.runtimeHeapIncrement == 0)
					return false;
				const UINT64 currentSlot64 =
					table.runtimeTableBaseIndex + observed.offset;
				if (currentSlot64 >= table.runtimeHeapNumDescriptors)
					return false;
				const UINT currentSlot = static_cast<UINT>(currentSlot64);
				DescriptorHeapRecord::StateLocks *stateLocks =
					stateLocksByRoot[observed.root];
				if (!stateLocks &&
						!RuntimeResolveHeapRecord(
							table, currentSlot, &stateLocks))
					return false;
				stateLocksByRoot[observed.root] = stateLocks;
				UINT64 currentVersion = 0;
				UINT64 currentValueId = 0;
				DescriptorHeapRecord::StateLocks::SlotIdentity identity = {};
				if (!stateLocks->ObserveSlotIdentity(
						currentSlot, &identity))
					return false;
				currentVersion = identity.version;
				currentValueId = identity.valueId;
				if (!RuntimeSlotIdentityMatches(
						currentVersion,
						currentValueId,
						observed.version,
						observed.valueId)) {
					return false;
				}
			}
			const UINT64 descriptorQuerySerialAfter =
				CurrentDescriptorQuerySerial();
			if (descriptorQuerySerialAfter != descriptorQuerySerialBefore)
				return false;
			cache.bindingsDescriptorQuerySerial =
				descriptorQuerySerialAfter;
			for (size_t index = 0; index < observedCount; ++index) {
				const UINT root = cache.bindingsObservedSlots[index].root;
				cache.bindingsRootTableBases[root] =
					runtimeState->computeRootTables[root].baseDescriptor.ptr;
			}
			}
	}

	// The ring may have rotated since the collection. The cached bindings are
	// semantically identical (value IDs matched), so rebase their CPU/GPU
	// table locations to the current tables before handing them out.
	RebaseRuntimePreSkinBindings(runtimeState);

	if (uavs)
		*uavs = &cache.uavs;
	if (srvs)
		*srvs = &cache.srvs;
	if (cbvs)
		*cbvs = &cache.cbvs;
	if (constants)
		*constants = &cache.rootConstants;
	if (dependencyFingerprint)
		*dependencyFingerprint = cache.bindingsDependencyFingerprint;
	if (foundAny)
		*foundAny = cache.bindingsFoundAny;
	return true;
}

static bool TryLoadRuntimePreSkinBindings(
	DX12SimulateContext *runtimeState,
	UINT64 computeBindingSerial,
	std::vector<DX12CurrentComputeUavBinding> *uavs,
	std::vector<DX12CurrentComputeUavBinding> *srvs,
	std::vector<DX12CurrentComputeUavBinding> *cbvs,
	std::vector<DX12CurrentRootConstants> *constants,
	UINT64 *dependencyFingerprint,
	bool *foundAny)
{
	const std::vector<DX12CurrentComputeUavBinding> *cachedUavs = nullptr;
	const std::vector<DX12CurrentComputeUavBinding> *cachedSrvs = nullptr;
	const std::vector<DX12CurrentComputeUavBinding> *cachedCbvs = nullptr;
	const std::vector<DX12CurrentRootConstants> *cachedConstants = nullptr;
	if (!TryGetRuntimePreSkinBindings(
			runtimeState,
			computeBindingSerial,
			&cachedUavs,
			&cachedSrvs,
			&cachedCbvs,
			&cachedConstants,
			dependencyFingerprint,
			foundAny))
		return false;
	if (uavs && cachedUavs)
		*uavs = *cachedUavs;
	if (srvs && cachedSrvs)
		*srvs = *cachedSrvs;
	if (cbvs && cachedCbvs)
		*cbvs = *cachedCbvs;
	if (constants && cachedConstants)
		*constants = *cachedConstants;
	return true;
}

static void StoreRuntimePreSkinBindings(
	DX12SimulateContext *runtimeState,
	UINT64 computeBindingSerial,
	UINT64 descriptorQuerySerial,
	UINT64 dependencyFingerprint,
	bool usesRootValues,
	bool foundAny,
	const std::vector<UINT> &observedRootParameters,
	const std::vector<UINT> &observedSlots,
	const std::vector<UINT64> &observedSlotVersions,
	const std::vector<UINT64> &observedSlotValueIds,
	const std::vector<DX12SimulateContextComputeTemplate::RuntimeObservedSlot>
		&observedSlotsRelative,
	const std::vector<DX12CurrentComputeUavBinding> &uavs,
	const std::vector<DX12CurrentComputeUavBinding> &srvs,
	const std::vector<DX12CurrentComputeUavBinding> &cbvs,
	const std::vector<DX12CurrentRootConstants> &constants)
{
	if (!runtimeState)
		return;
	runtimeState->resolvedCompute.bindingsValid = true;
	runtimeState->resolvedCompute.bindingsFoundAny = foundAny;
	runtimeState->resolvedCompute.bindingsUseRootValues = usesRootValues;
	runtimeState->resolvedCompute.bindingsSerial = computeBindingSerial;
	runtimeState->resolvedCompute.bindingsDescriptorVersion =
		runtimeState->computeDescriptorBindingVersion;
	runtimeState->resolvedCompute.bindingsDescriptorQuerySerial =
		descriptorQuerySerial;
	runtimeState->resolvedCompute.bindingsDependencyFingerprint =
		dependencyFingerprint;
	runtimeState->resolvedCompute.bindingsHeap = runtimeState->cbvSrvUavHeap;
	runtimeState->resolvedCompute.bindingsRootSignature =
		runtimeState->computeRootSignature;
	// Snapshot the exact descriptor-root identity (bare GPU VA roots have no
	// content-addressed value identity, so they must match exactly on hit).
	for (UINT root = 0; root < MaxRootParameters; ++root) {
		const RootDescriptorState &descriptor =
			runtimeState->computeRootDescriptors[root];
		runtimeState->resolvedCompute.bindingsRootValid[root] =
			descriptor.valid;
		runtimeState->resolvedCompute.bindingsRootIdentities[root] =
			descriptor.valid ? descriptor.address : 0;
		runtimeState->resolvedCompute.bindingsRootTableBases[root] =
			runtimeState->computeRootTables[root].valid ?
				runtimeState->computeRootTables[root].baseDescriptor.ptr : 0;
	}
	runtimeState->resolvedCompute.bindingsRootParameters =
		observedRootParameters;
	runtimeState->resolvedCompute.bindingsRootRevisions.resize(
		observedRootParameters.size());
	for (size_t index = 0; index < observedRootParameters.size(); ++index) {
		const UINT root = observedRootParameters[index];
		runtimeState->resolvedCompute.bindingsRootRevisions[index] =
			root < runtimeState->computeRootBindingRevisions.size() ?
				runtimeState->computeRootBindingRevisions[root] : 0;
	}
	runtimeState->resolvedCompute.observedSlots = observedSlots;
	runtimeState->resolvedCompute.observedSlotVersions = observedSlotVersions;
	runtimeState->resolvedCompute.observedSlotValueIds = observedSlotValueIds;
	if (observedSlotsRelative.size() == observedSlots.size()) {
		runtimeState->resolvedCompute.bindingsObservedSlots =
			observedSlotsRelative;
	} else {
		runtimeState->resolvedCompute.bindingsObservedSlots.clear();
	}
	runtimeState->resolvedCompute.uavs = uavs;
	runtimeState->resolvedCompute.srvs = srvs;
	runtimeState->resolvedCompute.cbvs = cbvs;
	runtimeState->resolvedCompute.rootConstants = constants;

	// DXVK-style ResetDirtyTracking: the cache now reflects every binding
	// this root signature uses, so those dirty bits are consumed. Bits for
	// root parameters outside the used mask (churn on tables the shader does
	// not reference) are left set; they never invalidate the cache and any
	// future collection pass clears them naturally. When no parsed root
	// signature is available the mask is not authoritative, so leave it
	// untouched (the serial fallback governs).
	if (runtimeState->computeRootUsedMaskValid) {
		runtimeState->computeRootDirtyMask &=
			~runtimeState->computeRootUsedMask;
	}
}

bool DX12BindingTracker::GetSimulateContextPreSkinBindings(
	ID3D12GraphicsCommandList *commandList,
	const std::vector<DX12CurrentComputeUavBinding> **uavs,
	const std::vector<DX12CurrentComputeUavBinding> **srvs,
	const std::vector<DX12CurrentComputeUavBinding> **cbvs,
	const std::vector<DX12CurrentRootConstants> **constants,
	UINT64 *dependencyFingerprint,
	bool *foundAny)
{
	if (uavs)
		*uavs = nullptr;
	if (srvs)
		*srvs = nullptr;
	if (cbvs)
		*cbvs = nullptr;
	if (constants)
		*constants = nullptr;
	if (dependencyFingerprint)
		*dependencyFingerprint = 0;
	if (foundAny)
		*foundAny = false;
	if (!commandList)
		return false;

	DX12SimulateContext *context =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	if (!context || !context->computeBindingSerial)
		return false;

	bool cachedFoundAny = false;
	const bool loaded = TryGetRuntimePreSkinBindings(
		context,
		context->computeBindingSerial,
		uavs,
		srvs,
		cbvs,
		constants,
		dependencyFingerprint,
		&cachedFoundAny);
	if (foundAny)
		*foundAny = cachedFoundAny;
	return loaded;
}

static bool TryLoadRuntimeDescriptorBindings(
	DX12SimulateContext *runtimeState,
	bool compute,
	D3D12_DESCRIPTOR_RANGE_TYPE rangeType,
	UINT64 bindingQuerySerial,
	std::vector<DX12CurrentComputeUavBinding> *bindings,
	bool *foundAny)
{
	if (!runtimeState || !bindings)
		return false;
	(void)bindingQuerySerial;

	const UINT cacheSlot =
		BindingTlsDescriptorQueryCache::CacheSlotIndex(compute, rangeType);
	if (cacheSlot == UINT_MAX ||
			cacheSlot >= runtimeState->resolvedDescriptorQueries.size())
		return false;

	DX12ResolvedDescriptorQueryRuntimeState &cache =
		runtimeState->resolvedDescriptorQueries[cacheSlot];
	if (!cache.valid ||
			cache.compute != compute ||
			cache.rangeType != static_cast<UINT>(rangeType) ||
			cache.bindingsHeap != runtimeState->cbvSrvUavHeap)
		return false;
	// Validate the exact root-binding identity instead of revisions/serial:
	// games like ZZZ re-bind root tables on every draw even when binding the
	// same descriptors, so revision/serial-based validation misses on every
	// draw and forces a full re-collect (snapshot copy + descriptor resolve +
	// locks) per draw. Same root signature + same root table/descriptor
	// identity + same observed slot versions proves the collected bindings
	// are identical.
	const ID3D12RootSignature *currentRootSignature = compute ?
		runtimeState->computeRootSignature :
		runtimeState->graphicsRootSignature;
	if (cache.rootSignature != currentRootSignature)
		return false;
	const RootTableState *currentTables = compute ?
		runtimeState->computeRootTables :
		runtimeState->graphicsRootTables;
	const RootDescriptorState *currentRootDescriptors = compute ?
		runtimeState->computeRootDescriptors :
		runtimeState->graphicsRootDescriptors;
	for (UINT root = 0; root < MaxRootParameters; ++root) {
		const bool currentValid =
			currentTables[root].valid ||
			currentRootDescriptors[root].valid;
		if (currentValid != cache.rootBindingValid[root])
			return false;
		if (!currentValid)
			continue;
		const UINT64 currentIdentity = currentTables[root].valid ?
			currentTables[root].baseDescriptor.ptr :
			currentRootDescriptors[root].address;
		if (currentIdentity != cache.rootBindingIdentities[root])
			return false;
	}

	const size_t observedCount = cache.observedSlots.size();
	if (observedCount != cache.observedSlotVersions.size() ||
			observedCount != cache.observedSlotValueIds.size())
		return false;

	if (observedCount) {
		if (!cache.bindingsHeap)
			return false;
		const UINT64 descriptorQuerySerialBefore =
			CurrentDescriptorQuerySerial();
		if (cache.descriptorQuerySerial != descriptorQuerySerialBefore) {
			constexpr size_t StackIdentityCount = 64;
			std::array<UINT64, StackIdentityCount> stackSlotVersions;
			std::array<UINT64, StackIdentityCount> stackSlotValueIds;
			std::vector<UINT64> heapSlotVersions;
			std::vector<UINT64> heapSlotValueIds;
			UINT64 *currentSlotVersions = stackSlotVersions.data();
			UINT64 *currentSlotValueIds = stackSlotValueIds.data();
			if (observedCount > StackIdentityCount) {
				heapSlotVersions.resize(observedCount);
				heapSlotValueIds.resize(observedCount);
				currentSlotVersions = heapSlotVersions.data();
				currentSlotValueIds = heapSlotValueIds.data();
			}
			if (!DX12GetDescriptorSlotIdentities(
					cache.bindingsHeap,
					cache.observedSlots.data(),
					observedCount,
					currentSlotVersions,
					currentSlotValueIds))
				return false;
			for (size_t index = 0; index < observedCount; ++index) {
				if (!RuntimeSlotIdentityMatches(
						currentSlotVersions[index],
						currentSlotValueIds[index],
						cache.observedSlotVersions[index],
						cache.observedSlotValueIds[index])) {
					return false;
				}
			}
			const UINT64 descriptorQuerySerialAfter =
				CurrentDescriptorQuerySerial();
			if (descriptorQuerySerialAfter != descriptorQuerySerialBefore)
				return false;
			cache.descriptorQuerySerial = descriptorQuerySerialAfter;
		}
	}

	*bindings = cache.bindings;
	if (foundAny)
		*foundAny = cache.foundAny;
	return true;
}

static void StoreRuntimeDescriptorBindings(
	DX12SimulateContext *runtimeState,
	bool compute,
	D3D12_DESCRIPTOR_RANGE_TYPE rangeType,
	UINT64 bindingQuerySerial,
	bool foundAny,
	const std::vector<UINT> &observedSlots,
	const std::vector<UINT64> &observedSlotVersions,
	const std::vector<UINT64> &observedSlotValueIds,
	const std::vector<DX12CurrentComputeUavBinding> &bindings)
{
	if (!runtimeState)
		return;

	const UINT cacheSlot =
		BindingTlsDescriptorQueryCache::CacheSlotIndex(compute, rangeType);
	if (cacheSlot == UINT_MAX ||
			cacheSlot >= runtimeState->resolvedDescriptorQueries.size())
		return;

	DX12ResolvedDescriptorQueryRuntimeState &cache =
		runtimeState->resolvedDescriptorQueries[cacheSlot];
	cache.valid = true;
	cache.foundAny = foundAny;
	cache.compute = compute;
	cache.rangeType = static_cast<UINT>(rangeType);
	cache.bindingQuerySerial = bindingQuerySerial;
	cache.descriptorQuerySerial = CurrentDescriptorQuerySerial();
	cache.bindingsHeap = runtimeState->cbvSrvUavHeap;
	// Snapshot the exact root-binding identity for validation (see
	// TryLoadRuntimeDescriptorBindings): revisions/serial bump on every
	// re-bind, which defeats the cache in binding-churn games.
	cache.rootSignature = compute ?
		runtimeState->computeRootSignature :
		runtimeState->graphicsRootSignature;
	{
		const RootTableState *tables = compute ?
			runtimeState->computeRootTables :
			runtimeState->graphicsRootTables;
		const RootDescriptorState *rootDescriptors = compute ?
			runtimeState->computeRootDescriptors :
			runtimeState->graphicsRootDescriptors;
		for (UINT root = 0; root < MaxRootParameters; ++root) {
			const bool tableValid = tables[root].valid;
			const bool descriptorValid = rootDescriptors[root].valid;
			cache.rootBindingValid[root] = tableValid || descriptorValid;
			cache.rootBindingIdentities[root] = tableValid ?
				tables[root].baseDescriptor.ptr :
				(descriptorValid ? rootDescriptors[root].address : 0);
		}
	}
	cache.rootParameters.clear();
	cache.rootRevisions.clear();
	cache.usesRootRevisions = !bindings.empty();
	if (cache.usesRootRevisions) {
		const std::array<UINT64, MaxRootParameters> &revisions = compute ?
			runtimeState->computeRootBindingRevisions :
			runtimeState->graphicsRootBindingRevisions;
		std::array<bool, MaxRootParameters> rootSeen = {};
		for (const DX12CurrentComputeUavBinding &binding : bindings) {
			if (binding.rootParameterIndex >= revisions.size()) {
				cache.usesRootRevisions = false;
				cache.rootParameters.clear();
				cache.rootRevisions.clear();
				break;
			}
			if (rootSeen[binding.rootParameterIndex])
				continue;
			rootSeen[binding.rootParameterIndex] = true;
			cache.rootParameters.push_back(binding.rootParameterIndex);
			cache.rootRevisions.push_back(
				revisions[binding.rootParameterIndex]);
		}
	}
	cache.observedSlots = observedSlots;
	cache.observedSlotVersions = observedSlotVersions;
	cache.observedSlotValueIds = observedSlotValueIds;
	cache.bindings = bindings;
}

static UINT64 GetRuntimeDescriptorQuerySerial(
	const DX12SimulateContext *runtimeState,
	bool compute)
{
	if (!runtimeState)
		return 0;
	const UINT64 serial = compute ?
		runtimeState->computeDescriptorQuerySerial :
		runtimeState->graphicsDescriptorQuerySerial;
	return serial ? serial : 1;
}

static bool BuildRuntimeBindingSnapshot(
	const DX12SimulateContext *runtimeState,
	CommandListBindingState *snapshot)
{
	if (!runtimeState || !snapshot)
		return false;

	*snapshot = CommandListBindingState();
	snapshot->pipelineState = runtimeState->pipelineState;
	snapshot->graphicsRootSignature = runtimeState->graphicsRootSignature;
	snapshot->computeRootSignature = runtimeState->computeRootSignature;
	snapshot->cbvSrvUavHeap = runtimeState->cbvSrvUavHeap;
	snapshot->samplerHeap = runtimeState->samplerHeap;
	snapshot->runtimeCbvSrvUavHeapResolved =
		runtimeState->runtimeCbvSrvUavHeapResolved;
	snapshot->runtimeCbvSrvUavHeap = runtimeState->runtimeCbvSrvUavHeap;
	snapshot->runtimeSamplerHeapResolved =
		runtimeState->runtimeSamplerHeapResolved;
	snapshot->runtimeSamplerHeap = runtimeState->runtimeSamplerHeap;
	snapshot->computeBindingSerial = runtimeState->computeBindingSerial;
	snapshot->bindingQuerySerial = runtimeState->bindingQuerySerial;
	snapshot->graphicsTableValidCount = runtimeState->graphicsRootTableValidCount;
	snapshot->computeTableValidCount = runtimeState->computeRootTableValidCount;
	memcpy(snapshot->graphicsTables,
		runtimeState->graphicsRootTables,
		sizeof(snapshot->graphicsTables));
	memcpy(snapshot->computeTables,
		runtimeState->computeRootTables,
		sizeof(snapshot->computeTables));
	memcpy(snapshot->graphicsRootDescriptors,
		runtimeState->graphicsRootDescriptors,
		sizeof(snapshot->graphicsRootDescriptors));
	memcpy(snapshot->computeRootDescriptors,
		runtimeState->computeRootDescriptors,
		sizeof(snapshot->computeRootDescriptors));
	memcpy(snapshot->graphicsRootConstants,
		runtimeState->graphicsRootConstants,
		sizeof(snapshot->graphicsRootConstants));
	memcpy(snapshot->computeRootConstants,
		runtimeState->computeRootConstants,
		sizeof(snapshot->computeRootConstants));
	snapshot->primitiveTopology = runtimeState->ia.primitiveTopology;
	if (runtimeState->ia.hasIndexBuffer) {
		snapshot->indexBuffer = runtimeState->ia.indexBuffer;
		snapshot->indexBufferValid = true;
		snapshot->indexBufferResource = runtimeState->ia.indexBufferResource;
		snapshot->indexBufferResolved = runtimeState->ia.indexBufferResolved;
	}
	for (UINT slot = 0; slot < ARRAYSIZE(runtimeState->ia.vertexBuffers); ++slot) {
		if (!runtimeState->ia.hasVertexBuffer[slot])
			continue;
		snapshot->vertexBuffers[slot] = runtimeState->ia.vertexBuffers[slot];
		snapshot->vertexBufferValid[slot] = true;
		snapshot->vertexBufferResources[slot] =
			runtimeState->ia.vertexBufferResources[slot];
		snapshot->vertexBufferResolved[slot] =
			runtimeState->ia.vertexBufferResolved[slot];
	}
	return true;
}

// Compute-only snapshot consumed by the PreSkin collector. Keeping this a
// dedicated type makes the contract explicit: this path only observes the
// compute root signature / tables / descriptors / constants and the bound
// CBV/SRV/UAV heap. The full CommandListBindingState (graphics tables, IA
// views, render targets, ...) is intentionally never copied here.
struct DX12PreSkinComputeSnapshot
{
	ID3D12PipelineState *pipelineState = nullptr;
	ID3D12RootSignature *computeRootSignature = nullptr;
	ID3D12DescriptorHeap *cbvSrvUavHeap = nullptr;
	bool runtimeCbvSrvUavHeapResolved = false;
	DX12DescriptorHeapSummary runtimeCbvSrvUavHeap = {};
	UINT64 computeBindingSerial = 0;
	UINT64 bindingQuerySerial = 1;
	UINT computeTableValidCount = 0;
	RootTableState computeTables[MaxRootParameters] = {};
	RootDescriptorState computeRootDescriptors[MaxRootParameters] = {};
	RootConstantsState computeRootConstants[MaxRootParameters] = {};
};

static bool BuildRuntimeComputeBindingSnapshot(
	const DX12SimulateContext *runtimeState,
	DX12PreSkinComputeSnapshot *snapshot)
{
	if (!runtimeState || !snapshot)
		return false;
	snapshot->pipelineState = runtimeState->pipelineState;
	snapshot->computeRootSignature = runtimeState->computeRootSignature;
	snapshot->cbvSrvUavHeap = runtimeState->cbvSrvUavHeap;
	snapshot->runtimeCbvSrvUavHeapResolved =
		runtimeState->runtimeCbvSrvUavHeapResolved;
	snapshot->runtimeCbvSrvUavHeap = runtimeState->runtimeCbvSrvUavHeap;
	snapshot->computeBindingSerial = runtimeState->computeBindingSerial;
	snapshot->bindingQuerySerial = runtimeState->bindingQuerySerial;
	snapshot->computeTableValidCount = runtimeState->computeRootTableValidCount;
	memcpy(snapshot->computeTables,
		runtimeState->computeRootTables,
		sizeof(snapshot->computeTables));
	memcpy(snapshot->computeRootDescriptors,
		runtimeState->computeRootDescriptors,
		sizeof(snapshot->computeRootDescriptors));
	memcpy(snapshot->computeRootConstants,
		runtimeState->computeRootConstants,
		sizeof(snapshot->computeRootConstants));
	return true;
}

static void FillPreSkinComputeSnapshotFromFull(
	const CommandListBindingState &src,
	DX12PreSkinComputeSnapshot *dst)
{
	if (!dst)
		return;
	dst->pipelineState = src.pipelineState;
	dst->computeRootSignature = src.computeRootSignature;
	dst->cbvSrvUavHeap = src.cbvSrvUavHeap;
	dst->runtimeCbvSrvUavHeapResolved = src.runtimeCbvSrvUavHeapResolved;
	dst->runtimeCbvSrvUavHeap = src.runtimeCbvSrvUavHeap;
	dst->computeBindingSerial = src.computeBindingSerial;
	dst->bindingQuerySerial = src.bindingQuerySerial;
	dst->computeTableValidCount = src.computeTableValidCount;
	memcpy(dst->computeTables,
		src.computeTables,
		sizeof(dst->computeTables));
	memcpy(dst->computeRootDescriptors,
		src.computeRootDescriptors,
		sizeof(dst->computeRootDescriptors));
	memcpy(dst->computeRootConstants,
		src.computeRootConstants,
		sizeof(dst->computeRootConstants));
}

class DX12BindingTrackerQueryCacheUtils
{
public:
	static bool ReadDescriptorSlotIdentities(
		ID3D12DescriptorHeap *heap,
		const UINT *slots,
		size_t count,
		UINT64 *versions,
		UINT64 *valueIds,
		const DX12DescriptorHeapSummary *summary = nullptr)
	{
		if (summary && RuntimeReadSlotIdentitiesFast(
				*summary, slots, count, versions, valueIds))
			return true;
		return DX12GetDescriptorSlotIdentities(
			heap, slots, count, versions, valueIds);
	}

	static void FillRuntimeTableFromHeapSummary(
		RootTableState *table,
		const DX12DescriptorHeapSummary &heap)
	{
		if (!table || !table->valid ||
				heap.increment == 0 || heap.gpuStart == 0 ||
				table->baseDescriptor.ptr < heap.gpuStart)
			return;

		const UINT64 tableBaseIndex =
			(table->baseDescriptor.ptr - heap.gpuStart) / heap.increment;
		if (tableBaseIndex >= heap.numDescriptors)
			return;

		table->runtimeHeapResolved = true;
		table->runtimeHeap = heap.heap;
		table->runtimeHeapType = heap.type;
		table->runtimeHeapCpuStart = heap.cpuStart;
		table->runtimeHeapGpuStart = heap.gpuStart;
		table->runtimeHeapIncrement = heap.increment;
		table->runtimeHeapNumDescriptors = heap.numDescriptors;
		table->runtimeHeapVersion = heap.version;
		table->runtimeTableBaseIndex = tableBaseIndex;
		table->runtimeTableCpuStart =
			heap.cpuStart + static_cast<SIZE_T>(tableBaseIndex) * heap.increment;
		table->runtimeTableGpuStart =
			heap.gpuStart + tableBaseIndex * heap.increment;
	}

	static void InvalidateRuntimeTable(
		RootTableState *table)
	{
		if (!table)
			return;

		table->runtimeHeapResolved = false;
		table->runtimeHeap = nullptr;
		table->runtimeHeapType = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		table->runtimeHeapCpuStart = 0;
		table->runtimeHeapGpuStart = 0;
		table->runtimeHeapIncrement = 0;
		table->runtimeHeapNumDescriptors = 0;
		table->runtimeHeapVersion = 0;
		table->runtimeTableBaseIndex = 0;
		table->runtimeTableCpuStart = 0;
		table->runtimeTableGpuStart = 0;
	}

	template <class Snapshot>
	static void RefreshRuntimeTable(
		const Snapshot &snapshot,
		RootTableState *table)
	{
		if (!table || !table->valid)
			return;

		DX12BindingTrackerQueryCacheUtils::InvalidateRuntimeTable(table);

		DX12DescriptorHeapSummary heap = {};
		if (!DX12BindingTrackerQueryCacheUtils::ResolveTableHeap(
				snapshot, *table, &heap))
			return;
		DX12BindingTrackerQueryCacheUtils::FillRuntimeTableFromHeapSummary(
			table, heap);
	}

	static bool MakeHeapSummaryFromRuntimeTable(
		const RootTableState &table,
		DX12DescriptorHeapSummary *heap)
	{
		if (!heap || !table.valid || !table.runtimeHeapResolved ||
				table.runtimeHeapIncrement == 0 ||
				table.runtimeTableBaseIndex >= table.runtimeHeapNumDescriptors)
			return false;

		heap->heap = table.runtimeHeap;
		heap->type = table.runtimeHeapType;
		heap->numDescriptors = table.runtimeHeapNumDescriptors;
		heap->cpuStart = table.runtimeHeapCpuStart;
		heap->gpuStart = table.runtimeHeapGpuStart;
		heap->increment = table.runtimeHeapIncrement;
		heap->version = table.runtimeHeapVersion;
		return true;
	}

	template <class Snapshot>
	static bool TryGetBoundCbvSrvUavHeap(
		const Snapshot &snapshot,
		DX12DescriptorHeapSummary *heap)
	{
		if (!heap || !snapshot.cbvSrvUavHeap)
			return false;
		if (snapshot.runtimeCbvSrvUavHeapResolved &&
				snapshot.runtimeCbvSrvUavHeap.heap == snapshot.cbvSrvUavHeap &&
				snapshot.runtimeCbvSrvUavHeap.type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV &&
				snapshot.runtimeCbvSrvUavHeap.increment != 0) {
			*heap = snapshot.runtimeCbvSrvUavHeap;
			return true;
		}
		if (!DX12FindDescriptorHeapByPtr(snapshot.cbvSrvUavHeap, heap))
			return false;
		return heap->type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV &&
			heap->increment != 0;
	}

	static void StoreRuntimeHeapSummaries(
		CommandListBindingState *state,
		const DX12DescriptorHeapSummary *cbvSrvUavHeap,
		bool cbvSrvUavResolved,
		const DX12DescriptorHeapSummary *samplerHeap,
		bool samplerResolved)
	{
		if (!state)
			return;
		state->runtimeCbvSrvUavHeapResolved = cbvSrvUavResolved;
		state->runtimeCbvSrvUavHeap =
			(cbvSrvUavResolved && cbvSrvUavHeap) ?
			*cbvSrvUavHeap : DX12DescriptorHeapSummary();
		state->runtimeSamplerHeapResolved = samplerResolved;
		state->runtimeSamplerHeap =
			(samplerResolved && samplerHeap) ?
			*samplerHeap : DX12DescriptorHeapSummary();
	}

	static void RefreshAllRuntimeTables(CommandListBindingState *state)
	{
		if (!state)
			return;
		for (UINT root = 0; root < MaxRootParameters; ++root) {
			if (state->graphicsTables[root].valid)
				DX12BindingTrackerQueryCacheUtils::RefreshRuntimeTable(
					*state, &state->graphicsTables[root]);
			if (state->computeTables[root].valid)
				DX12BindingTrackerQueryCacheUtils::RefreshRuntimeTable(
					*state, &state->computeTables[root]);
		}
	}

	template <class Snapshot>
	static bool ResolveTableHeap(
		const Snapshot &snapshot,
		const RootTableState &table,
		DX12DescriptorHeapSummary *heap)
	{
		if (!heap || !table.valid)
			return false;
		if (DX12BindingTrackerQueryCacheUtils::MakeHeapSummaryFromRuntimeTable(
				table, heap))
			return true;
		DX12DescriptorHeapSummary boundHeap = {};
		if (DX12BindingTrackerQueryCacheUtils::TryGetBoundCbvSrvUavHeap(
				snapshot, &boundHeap) &&
			DX12BindingTrackerQuery::TableMatchesHeap(table, boundHeap)) {
			*heap = boundHeap;
			return true;
		}
		return DX12FindDescriptorHeapByGpuHandle(
			table.baseDescriptor, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, heap) &&
			heap->increment != 0;
	}

	template <class Snapshot>
	static bool ResolveRuntimeTableHeap(
		const Snapshot &snapshot,
		const RootTableState &table,
		DX12DescriptorHeapSummary *heap)
	{
		if (DX12BindingTrackerQueryCacheUtils::MakeHeapSummaryFromRuntimeTable(
				table, heap))
			return true;
		return DX12BindingTrackerQueryCacheUtils::ResolveTableHeap(
			snapshot, table, heap);
	}

	static bool EnsureQueryRootSignature(
		const CommandListBindingState &snapshot,
		bool compute,
		BindingEvent *event)
	{
		if (!event)
			return false;

		ID3D12RootSignature *rootSignature = compute ?
			event->computeRootSignature : event->graphicsRootSignature;
		if (rootSignature)
			return true;
		if (!snapshot.pipelineState)
			return false;

		if (event->shaderInfo.psoIndex == 0)
			DX12GetPipelineStateShaderInfo(snapshot.pipelineState, &event->shaderInfo);
		if (event->shaderInfo.psoIndex == 0)
			return false;

		ID3D12RootSignature *psoRootSignature = nullptr;
		if (!DX12GetPsoRootSignature(event->shaderInfo.psoIndex, &psoRootSignature) ||
			!psoRootSignature)
			return false;

		if (compute)
			event->computeRootSignature = psoRootSignature;
		else
			event->graphicsRootSignature = psoRootSignature;
		return true;
	}

	static void BuildEventForQuery(
		const CommandListBindingState &snapshot,
		bool compute,
		BindingEvent *event)
	{
		if (!event)
			return;
		*event = BindingEvent();
		event->pipelineState = snapshot.pipelineState;
		event->graphicsRootSignature = snapshot.graphicsRootSignature;
		event->computeRootSignature = snapshot.computeRootSignature;
		event->shaderInfo = snapshot.pipelineShaderInfo;
		DX12BindingTrackerQueryCacheUtils::EnsureQueryRootSignature(
			snapshot, compute, event);
	}

	static bool CollectDescriptorBindings(
		const CommandListBindingState &snapshot,
		bool compute,
		D3D12_DESCRIPTOR_RANGE_TYPE rangeType,
		const char *descriptorKind,
		D3D12_ROOT_PARAMETER_TYPE rootDescriptorType,
		std::vector<DX12CurrentComputeUavBinding> *outBindings,
		ID3D12DescriptorHeap **observedSlotHeap = nullptr,
		std::vector<UINT> *observedSlots = nullptr)
	{
		if (!outBindings)
			return false;

		outBindings->clear();
		if (observedSlotHeap)
			*observedSlotHeap = nullptr;
		if (observedSlots)
			observedSlots->clear();
		BindingEvent event;
		BuildEventForQuery(snapshot, compute, &event);
		DX12RootSignatureSummary rootSignature;
		const bool hasRootSignature =
			DX12BindingTrackerQuery::GetRootSignatureForEvent(
				event, compute, &rootSignature);
		const bool completeDescriptorResourceInfo =
			rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_CBV ||
			rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		for (UINT root = 0; root < MaxRootParameters; ++root) {
			const RootTableState &table = compute ?
				snapshot.computeTables[root] : snapshot.graphicsTables[root];
			if (table.valid) {
				const DX12RootParameterSummary *parameter =
					hasRootSignature ? DX12BindingTrackerQuery::FindRootParameter(rootSignature, root) : nullptr;
						if (parameter && parameter->parameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
					for (const DX12RootDescriptorRangeSummary &range : parameter->ranges) {
						if (range.rangeType != rangeType)
							continue;
						DX12DescriptorHeapSummary heap = {};
						if (!DX12BindingTrackerQueryCacheUtils::ResolveTableHeap(
								snapshot, table, &heap))
							continue;
						const UINT64 tableBaseIndex =
							(table.baseDescriptor.ptr - heap.gpuStart) / heap.increment;
						const UINT count = DX12BindingTrackerQuery::DescriptorCountForRange(
							heap, tableBaseIndex, range);
						std::vector<SIZE_T> cpuHandles;
						std::vector<DX12DescriptorSummary> descriptors;
						std::vector<uint8_t> found;
						if (observedSlotHeap && heap.heap && !*observedSlotHeap)
							*observedSlotHeap = heap.heap;
						cpuHandles.reserve(count);
						for (UINT offset = 0; offset < count; ++offset) {
							const UINT64 descriptorIndex =
								tableBaseIndex + range.effectiveOffset + offset;
							if (descriptorIndex >= heap.numDescriptors)
								break;
							if (observedSlots)
								observedSlots->push_back(static_cast<UINT>(descriptorIndex));
							const SIZE_T cpuHandle =
								heap.cpuStart + static_cast<SIZE_T>(descriptorIndex) * heap.increment;
							cpuHandles.push_back(cpuHandle);
						}
						descriptors.resize(cpuHandles.size());
						found.resize(cpuHandles.size());
						DX12FindDescriptorSummariesByCpuHandle(
							cpuHandles.data(), cpuHandles.size(),
							descriptors.data(), found.data(),
							completeDescriptorResourceInfo);
						for (UINT offset = 0; offset < cpuHandles.size(); ++offset) {
							if (!found[offset] || descriptors[offset].kind != descriptorKind)
								continue;
							DX12CurrentComputeUavBinding binding;
							binding.rootParameterIndex = root;
							binding.rangeIndex = range.rangeIndex;
							binding.shaderVisibility = range.shaderVisibility;
							binding.shaderRegister = range.baseShaderRegister + offset;
							binding.registerSpace = range.registerSpace;
							binding.descriptorOffset = range.effectiveOffset + offset;
							binding.descriptorIncrement = heap.increment;
							binding.tableCopyCount = count;
							binding.tableCpuStart =
								heap.cpuStart + static_cast<SIZE_T>(tableBaseIndex) * heap.increment;
							binding.tableGpuStart.ptr =
								heap.gpuStart + tableBaseIndex * heap.increment;
							binding.tableHeap = heap.heap;
							binding.descriptor = descriptors[offset];
							binding.hasDescriptor = true;
							outBindings->push_back(std::move(binding));
						}
					}
				} else {
					DX12DescriptorHeapSummary heap = {};
					if (DX12BindingTrackerQueryCacheUtils::ResolveTableHeap(
							snapshot, table, &heap)) {
						const UINT64 descriptorIndex =
							(table.baseDescriptor.ptr - heap.gpuStart) / heap.increment;
						if (observedSlotHeap && heap.heap && !*observedSlotHeap)
							*observedSlotHeap = heap.heap;
						if (observedSlots && descriptorIndex < heap.numDescriptors)
							observedSlots->push_back(static_cast<UINT>(descriptorIndex));
						const SIZE_T cpuHandle =
							heap.cpuStart + static_cast<SIZE_T>(descriptorIndex) * heap.increment;
						DX12DescriptorSummary descriptor = {};
						const bool foundDescriptor = completeDescriptorResourceInfo ?
							DX12FindDescriptorSummaryByCpuHandle(cpuHandle, &descriptor) :
							DX12FindDescriptorSummaryByCpuHandleLight(cpuHandle, &descriptor);
						if (foundDescriptor && descriptor.kind == descriptorKind) {
							DX12CurrentComputeUavBinding binding;
							binding.rootParameterIndex = root;
							binding.shaderVisibility = parameter ?
								parameter->shaderVisibility : D3D12_SHADER_VISIBILITY_ALL;
							binding.descriptorIncrement = heap.increment;
							binding.tableCopyCount = 1;
							binding.tableCpuStart = cpuHandle;
							binding.tableGpuStart = table.baseDescriptor;
							binding.tableHeap = heap.heap;
							binding.descriptor = descriptor;
							binding.hasDescriptor = true;
							outBindings->push_back(std::move(binding));
						}
					}
				}
			}

			const RootDescriptorState &rootDescriptor = compute ?
				snapshot.computeRootDescriptors[root] : snapshot.graphicsRootDescriptors[root];
			if (rootDescriptor.valid && rootDescriptor.type == rootDescriptorType) {
				DX12CurrentComputeUavBinding binding;
				binding.rootParameterIndex = root;
				if (hasRootSignature) {
					const DX12RootParameterSummary *parameter =
						DX12BindingTrackerQuery::FindRootParameter(rootSignature, root);
					if (parameter) {
						binding.shaderVisibility = parameter->shaderVisibility;
						binding.shaderRegister = parameter->shaderRegister;
						binding.registerSpace = parameter->registerSpace;
					}
				}
				binding.rootDescriptor = true;
				binding.gpuVirtualAddress = rootDescriptor.address;
				outBindings->push_back(std::move(binding));
			}
		}

		return !outBindings->empty();
	}

	static bool CollectDescriptorBindingsByRegister(
		const CommandListBindingState &snapshot,
		bool compute,
		D3D12_DESCRIPTOR_RANGE_TYPE rangeType,
		UINT shaderRegister,
		UINT registerSpace,
		const char *descriptorKind,
		D3D12_ROOT_PARAMETER_TYPE rootDescriptorType,
		std::vector<DX12CurrentComputeUavBinding> *outBindings)
	{
		if (!outBindings)
			return false;

		outBindings->clear();
		BindingEvent event;
		BuildEventForQuery(snapshot, compute, &event);
		DX12RootSignatureSummary rootSignature;
		const bool hasRootSignature =
			DX12BindingTrackerQuery::GetRootSignatureForEvent(
				event, compute, &rootSignature);
		if (!hasRootSignature) {
			std::vector<DX12CurrentComputeUavBinding> allBindings;
			if (!CollectDescriptorBindings(
					snapshot, compute, rangeType, descriptorKind,
					rootDescriptorType, &allBindings))
				return false;
			for (const DX12CurrentComputeUavBinding &binding : allBindings) {
				if (binding.shaderRegister != shaderRegister ||
				    binding.registerSpace != registerSpace)
					continue;
				outBindings->push_back(binding);
			}
			return !outBindings->empty();
		}

		const bool completeDescriptorResourceInfo =
			rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_CBV ||
			rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		for (UINT root = 0; root < MaxRootParameters; ++root) {
			const RootTableState &table = compute ?
				snapshot.computeTables[root] : snapshot.graphicsTables[root];
			const DX12RootParameterSummary *parameter =
				DX12BindingTrackerQuery::FindRootParameter(rootSignature, root);
			if (table.valid && parameter &&
			    parameter->parameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
				for (const DX12RootDescriptorRangeSummary &range : parameter->ranges) {
					if (range.rangeType != rangeType ||
					    range.registerSpace != registerSpace ||
					    shaderRegister < range.baseShaderRegister)
						continue;
					DX12DescriptorHeapSummary heap = {};
					if (!DX12BindingTrackerQueryCacheUtils::ResolveTableHeap(
							snapshot, table, &heap))
						continue;
					const UINT64 tableBaseIndex =
						(table.baseDescriptor.ptr - heap.gpuStart) / heap.increment;
					const UINT count = DX12BindingTrackerQuery::DescriptorCountForRange(
						heap, tableBaseIndex, range);
					const UINT offset = shaderRegister - range.baseShaderRegister;
					if (offset >= count)
						continue;
					const UINT64 descriptorIndex =
						tableBaseIndex + range.effectiveOffset + offset;
					if (descriptorIndex >= heap.numDescriptors)
						continue;
					const SIZE_T cpuHandle =
						heap.cpuStart + static_cast<SIZE_T>(descriptorIndex) * heap.increment;
					DX12DescriptorSummary descriptor = {};
					const bool foundDescriptor = completeDescriptorResourceInfo ?
						DX12FindDescriptorSummaryByCpuHandle(cpuHandle, &descriptor) :
						DX12FindDescriptorSummaryByCpuHandleLight(cpuHandle, &descriptor);
					if (!foundDescriptor || descriptor.kind != descriptorKind)
						continue;
					DX12CurrentComputeUavBinding binding;
					binding.rootParameterIndex = root;
					binding.rangeIndex = range.rangeIndex;
					binding.shaderVisibility = range.shaderVisibility;
					binding.shaderRegister = shaderRegister;
					binding.registerSpace = registerSpace;
					binding.descriptorOffset = range.effectiveOffset + offset;
					binding.descriptorIncrement = heap.increment;
					binding.tableCopyCount = count;
					binding.tableCpuStart =
						heap.cpuStart + static_cast<SIZE_T>(tableBaseIndex) * heap.increment;
					binding.tableGpuStart.ptr =
						heap.gpuStart + tableBaseIndex * heap.increment;
					binding.tableHeap = heap.heap;
					binding.descriptor = descriptor;
					binding.hasDescriptor = true;
					outBindings->push_back(std::move(binding));
				}
			}

			const RootDescriptorState &rootDescriptor = compute ?
				snapshot.computeRootDescriptors[root] : snapshot.graphicsRootDescriptors[root];
			if (!rootDescriptor.valid || rootDescriptor.type != rootDescriptorType || !parameter)
				continue;
			if (parameter->shaderRegister != shaderRegister ||
			    parameter->registerSpace != registerSpace)
				continue;
			DX12CurrentComputeUavBinding binding;
			binding.rootParameterIndex = root;
			binding.shaderVisibility = parameter->shaderVisibility;
			binding.shaderRegister = parameter->shaderRegister;
			binding.registerSpace = parameter->registerSpace;
			binding.rootDescriptor = true;
			binding.gpuVirtualAddress = rootDescriptor.address;
			outBindings->push_back(std::move(binding));
		}

		return !outBindings->empty();
	}

	static bool CollectComputeAll(
		const CommandListBindingState &snapshot,
		std::vector<DX12CurrentComputeUavBinding> *uavs,
		std::vector<DX12CurrentComputeUavBinding> *srvs,
		std::vector<DX12CurrentComputeUavBinding> *cbvs,
		std::vector<DX12CurrentRootConstants> *constants)
	{
		if (uavs) uavs->clear();
		if (srvs) srvs->clear();
		if (cbvs) cbvs->clear();
		if (constants) constants->clear();

		BindingEvent event;
		BuildEventForQuery(snapshot, true, &event);
		DX12RootSignatureSummary rootSignature;
		const bool hasRootSignature =
			DX12BindingTrackerQuery::GetRootSignatureForEvent(event, true, &rootSignature);

		// Root-signature-driven iteration: use declared parameter count as upper bound.
		const UINT maxRoot = hasRootSignature
			? static_cast<UINT>(rootSignature.parameters.size())
			: MaxRootParameters;

		static thread_local std::vector<SIZE_T> tlsCpuHandles;
		static thread_local std::vector<DX12DescriptorSummary> tlsDescriptors;
		static thread_local std::vector<uint8_t> tlsFound;

		for (UINT root = 0; root < maxRoot; ++root) {
			const RootTableState &table = snapshot.computeTables[root];
			if (table.valid) {
				const DX12RootParameterSummary *parameter =
					hasRootSignature ? DX12BindingTrackerQuery::FindRootParameter(rootSignature, root) : nullptr;
				if (parameter && parameter->parameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
					DX12DescriptorHeapSummary heap = {};
					if (!DX12BindingTrackerQueryCacheUtils::ResolveTableHeap(
							snapshot, table, &heap))
						continue;
					const UINT64 tableBase = (table.baseDescriptor.ptr - heap.gpuStart) / heap.increment;
					for (const DX12RootDescriptorRangeSummary &range : parameter->ranges) {
						std::vector<DX12CurrentComputeUavBinding> *out = nullptr;
						const char *kind = nullptr;
						bool completeDescriptorResourceInfo = false;
						if (range.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV && uavs) { out = uavs; kind = "UAV"; }
						else if (range.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV && srvs) {
							out = srvs;
							kind = "SRV";
							completeDescriptorResourceInfo = true;
						}
						else if (range.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_CBV && cbvs) {
							out = cbvs;
							kind = "CBV";
							completeDescriptorResourceInfo = true;
						}
						else { continue; }

						const UINT count = DX12BindingTrackerQuery::DescriptorCountForRange(heap, tableBase, range);
						tlsCpuHandles.clear();
						tlsCpuHandles.reserve(count);
						for (UINT offset = 0; offset < count; ++offset) {
							const UINT64 di = tableBase + range.effectiveOffset + offset;
							if (di >= heap.numDescriptors) break;
							const SIZE_T cpuHandle = heap.cpuStart + static_cast<SIZE_T>(di) * heap.increment;
							tlsCpuHandles.push_back(cpuHandle);
						}
						tlsDescriptors.resize(tlsCpuHandles.size());
						tlsFound.resize(tlsCpuHandles.size());
						DX12FindDescriptorSummariesByCpuHandle(
							tlsCpuHandles.data(), tlsCpuHandles.size(),
							tlsDescriptors.data(), tlsFound.data(),
							completeDescriptorResourceInfo);
						for (UINT offset = 0; offset < tlsCpuHandles.size(); ++offset) {
							if (!tlsFound[offset] || tlsDescriptors[offset].kind != kind)
								continue;
							DX12CurrentComputeUavBinding binding;
							binding.rootParameterIndex = root;
							binding.rangeIndex = range.rangeIndex;
							binding.shaderVisibility = range.shaderVisibility;
							binding.shaderRegister = range.baseShaderRegister + offset;
							binding.registerSpace = range.registerSpace;
							binding.descriptorOffset = range.effectiveOffset + offset;
							binding.descriptorIncrement = heap.increment;
							binding.tableCopyCount = count;
							binding.tableCpuStart = heap.cpuStart + static_cast<SIZE_T>(tableBase) * heap.increment;
							binding.tableGpuStart.ptr = heap.gpuStart + tableBase * heap.increment;
							binding.tableHeap = heap.heap;
							binding.descriptor = tlsDescriptors[offset];
							binding.hasDescriptor = true;
							out->push_back(std::move(binding));
						}
					}
				} else {
					DX12DescriptorHeapSummary heap = {};
					if (!DX12BindingTrackerQueryCacheUtils::ResolveTableHeap(
							snapshot, table, &heap))
						continue;
					const UINT64 di = (table.baseDescriptor.ptr - heap.gpuStart) / heap.increment;
					const SIZE_T cpuHandle = heap.cpuStart + static_cast<SIZE_T>(di) * heap.increment;
					DX12DescriptorSummary desc = {};
					if (!DX12FindDescriptorSummaryByCpuHandleLight(cpuHandle, &desc))
						continue;
					std::vector<DX12CurrentComputeUavBinding> *out = nullptr;
					bool completeDescriptorResourceInfo = false;
					if (desc.kind == DescriptorKind::UAV && uavs) out = uavs;
					else if (desc.kind == DescriptorKind::SRV && srvs) {
						if (!DX12FindDescriptorSummaryByCpuHandle(cpuHandle, &desc))
							continue;
						out = srvs;
						completeDescriptorResourceInfo = true;
					}
					else if (desc.kind == DescriptorKind::CBV && cbvs) {
						if (!DX12FindDescriptorSummaryByCpuHandle(cpuHandle, &desc))
							continue;
						out = cbvs;
						completeDescriptorResourceInfo = true;
					}
					if (!out) continue;
					(void)completeDescriptorResourceInfo;
					DX12CurrentComputeUavBinding binding;
					binding.rootParameterIndex = root;
					binding.shaderVisibility = parameter ? parameter->shaderVisibility : D3D12_SHADER_VISIBILITY_ALL;
					binding.descriptorIncrement = heap.increment;
					binding.tableCopyCount = 1;
					binding.tableCpuStart = cpuHandle;
					binding.tableGpuStart = table.baseDescriptor;
					binding.tableHeap = heap.heap;
					binding.descriptor = desc;
					binding.hasDescriptor = true;
					out->push_back(std::move(binding));
				}
			}

			const RootDescriptorState &rd = snapshot.computeRootDescriptors[root];
			if (rd.valid) {
				std::vector<DX12CurrentComputeUavBinding> *out = nullptr;
				if (rd.type == D3D12_ROOT_PARAMETER_TYPE_UAV && uavs) out = uavs;
				else if (rd.type == D3D12_ROOT_PARAMETER_TYPE_SRV && srvs) out = srvs;
				else if (rd.type == D3D12_ROOT_PARAMETER_TYPE_CBV && cbvs) out = cbvs;
				if (out) {
					DX12CurrentComputeUavBinding binding;
					binding.rootParameterIndex = root;
					if (hasRootSignature) {
						const DX12RootParameterSummary *p = DX12BindingTrackerQuery::FindRootParameter(rootSignature, root);
						if (p) {
							binding.shaderVisibility = p->shaderVisibility;
							binding.shaderRegister = p->shaderRegister;
							binding.registerSpace = p->registerSpace;
						}
					}
					binding.rootDescriptor = true;
					binding.gpuVirtualAddress = rd.address;
					out->push_back(std::move(binding));
				}
			}

			if (constants) {
				const RootConstantsState &rc = snapshot.computeRootConstants[root];
				if (rc.valid) {
					DX12CurrentRootConstants current;
					current.rootParameterIndex = root;
					current.maxSet = rc.maxSet;
					memcpy(current.values, rc.values, sizeof(current.values));
					memcpy(current.valueValid, rc.valueValid, sizeof(current.valueValid));
					constants->push_back(current);
				}
			}
		}

		return (uavs && !uavs->empty()) || (srvs && !srvs->empty()) ||
			(cbvs && !cbvs->empty()) || (constants && !constants->empty());
	}
};

// ============================================================================
// CollectPreSkinBindings - lightweight replacement for GetCurrentComputeAll
// in the PreSkin hot path. Uses a cached root signature summary (parsed once
// per ID3D12RootSignature*) to iterate descriptor table ranges without
// re-parsing the blob on every call. Scans multiple descriptors per table
// (not just the first) to correctly capture UAVs at non-zero offsets.
//
// Cost: O(tables + descriptors_in_relevant_ranges) vs old O(all_descriptors).
// Typically ~50x faster (~0.5ms vs ~25ms) while producing identical results.
// ============================================================================
bool DX12BindingTracker::CollectPreSkinBindings(
	ID3D12GraphicsCommandList *commandList,
	std::vector<DX12CurrentComputeUavBinding> *uavs,
	std::vector<DX12CurrentComputeUavBinding> *srvs,
	std::vector<DX12CurrentComputeUavBinding> *cbvs,
	std::vector<DX12CurrentRootConstants> *constants,
	UINT64 *dependencyFingerprint)
{
	if (dependencyFingerprint)
		*dependencyFingerprint = 0;
	if (!commandList)
		return false;

	if (uavs) uavs->clear();
	if (srvs) srvs->clear();
	if (cbvs) cbvs->clear();
	if (constants) constants->clear();

	DX12SimulateContext *runtimeState =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	const DX12CommandListTrackingState tracking =
		runtimeState ?
		DX12CommandListRuntime::GetTrackingState(runtimeState) :
		DX12CommandListTrackingState();
	const bool preferRuntimeSnapshot = runtimeState && !tracking.trackBindings;
	const UINT64 computeBindingSerial =
		runtimeState ?
		runtimeState->computeBindingSerial :
		DX12BindingTracker::GetComputeBindingSerial(commandList);
	bool cachedFoundAny = false;
	DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinBindingCacheCall();
	if (runtimeState && computeBindingSerial &&
			TryLoadRuntimePreSkinBindings(
				runtimeState,
				computeBindingSerial,
				uavs,
				srvs,
				cbvs,
				constants,
				dependencyFingerprint,
				&cachedFoundAny)) {
		DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinBindingCacheHit();
		return cachedFoundAny;
	}

	DX12PreSkinComputeSnapshot snapshot;
	DX12_HOTPATH_TIME_BEGIN(BindSnapshot);
	bool snapshotReady = false;
	if (preferRuntimeSnapshot) {
		snapshotReady = BuildRuntimeComputeBindingSnapshot(
			runtimeState, &snapshot);
	} else {
		// Non-runtime tracked lists snapshot the full state, then only the
		// compute fields this collector observes are carried over.
		CommandListBindingState fullSnapshot;
		if (DX12BindingTracker::SnapshotBindingState(
				commandList, &fullSnapshot)) {
			FillPreSkinComputeSnapshotFromFull(fullSnapshot, &snapshot);
			snapshotReady = true;
		}
	}
	if (!snapshotReady) {
		DX12_HOTPATH_TIME_END(BindSnapshot);
		return false;
	}
	DX12_HOTPATH_TIME_END(BindSnapshot);

	// Cache root signature summary per ID3D12RootSignature* pointer.
	// Parse the blob once, reuse across all dispatches with the same signature.
	// Thread-local: the summary only changes on a root-signature/PSO transition,
	// so per-thread caching removes the global SRW lock + shared unordered_map
	// lookup from the per-dispatch PreSkin hot path. Reloads invalidate via the
	// reload generation, matching the surrounding resident-state pattern.
	static thread_local std::unordered_map<ID3D12RootSignature*, DX12RootSignatureSummary> s_rootSigCache;
	static thread_local UINT64 s_rootSigCacheReloadGen = 0;
	UINT64 reloadGen = DX12ModGetReloadGeneration();
	if (s_rootSigCacheReloadGen != reloadGen) {
		s_rootSigCache.clear();
		s_rootSigCacheReloadGen = reloadGen;
	}

	ID3D12RootSignature *computeRootSig = snapshot.computeRootSignature;
	if (!computeRootSig && snapshot.pipelineState) {
		DX12PsoShaderInfo shaderInfo = {};
		ID3D12RootSignature *psoRootSignature = nullptr;
		if (DX12GetPipelineStateShaderInfo(snapshot.pipelineState, &shaderInfo) &&
			shaderInfo.psoIndex &&
			DX12GetPsoRootSignature(shaderInfo.psoIndex, &psoRootSignature) &&
			psoRootSignature) {
			computeRootSig = psoRootSignature;
		}
	}
	DX12RootSignatureSummary rootSignatureSummary;
	const DX12RootSignatureSummary *cachedSummary = nullptr;
	if (computeRootSig) {
		std::unordered_map<ID3D12RootSignature*, DX12RootSignatureSummary>::const_iterator sigIt =
			s_rootSigCache.find(computeRootSig);
		if (sigIt != s_rootSigCache.end()) {
			rootSignatureSummary = sigIt->second;
			cachedSummary = &rootSignatureSummary;
		} else {
			DX12RootSignatureSummary parsed;
			if (DX12GetRootSignatureSummary(computeRootSig, &parsed)) {
				s_rootSigCache[computeRootSig] = parsed;
				rootSignatureSummary = parsed;
				cachedSummary = &rootSignatureSummary;
			}
		}
	}
	const bool hasParsedRootSignatureSummary =
		cachedSummary && cachedSummary->parsed;
	// The runtime state caches the heap content version at SetDescriptorHeaps
	// time, so the runtime path does not need the global descriptor lock to
	// re-read it here.
	const UINT64 descriptorHeapVersionBefore =
		preferRuntimeSnapshot ?
			runtimeState->cbvSrvUavHeapVersion :
			DX12GetDescriptorHeapVersion(snapshot.cbvSrvUavHeap);
	const UINT64 bufferResolveEpoch =
		gBufferResolveGeneration.load(std::memory_order_relaxed);
	DX12PreSkinBindingCacheKey bindingCacheKey;
	DX12PreSkinBindingCacheUtils::BuildKey<DX12BindingTrackerQueryCacheUtils>(
		snapshot,
		descriptorHeapVersionBefore,
		bufferResolveEpoch,
		hasParsedRootSignatureSummary ? cachedSummary : nullptr,
		&bindingCacheKey);
	static thread_local UINT64 tlsPreSkinBindingCacheReloadGeneration = 0;
	static thread_local DX12PreSkinBindingTlsCache tlsPreSkinBindingCache;
	if (tlsPreSkinBindingCacheReloadGeneration != reloadGen) {
		tlsPreSkinBindingCache = DX12PreSkinBindingTlsCache();
		tlsPreSkinBindingCacheReloadGeneration = reloadGen;
	}
	bool contentCachedFoundAny = false;
	if (DX12PreSkinBindingCacheUtils::TryLoad(
			&tlsPreSkinBindingCache,
			bindingCacheKey,
			uavs,
			srvs,
			cbvs,
			constants,
			&contentCachedFoundAny)) {
		if (dependencyFingerprint)
			*dependencyFingerprint = bindingCacheKey.fingerprint;
		DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinBindingCacheHit();
		return contentCachedFoundAny;
	}
	DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinBindingCacheMiss();

	bool allRuntimeDescriptorsStable = true;
	bool canStoreRuntimeBindings = runtimeState && computeBindingSerial != 0;
	ID3D12DescriptorHeap *observedSlotHeap = nullptr;
	bool usesRootValues = false;

	// Thread-local scratch buffers grow to the actual descriptor workload and
	// retain capacity across calls. Correctness is bounded by D3D12 metadata,
	// not by a local fixed-size array.
	static thread_local std::vector<DX12CurrentComputeUavBinding> tlsLocalUavs;
	static thread_local std::vector<DX12CurrentComputeUavBinding> tlsLocalSrvs;
	static thread_local std::vector<DX12CurrentComputeUavBinding> tlsLocalCbvs;
	static thread_local std::vector<DX12CurrentRootConstants> tlsLocalConstants;
	static thread_local std::vector<UINT> tlsObservedRootParameters;
	static thread_local std::vector<UINT> tlsObservedSlots;
	static thread_local std::vector<UINT> tlsObservedRoots;
	static thread_local std::vector<UINT> tlsObservedOffsets;
	static thread_local std::vector<UINT64> tlsObservedSlotVersions;
	static thread_local std::vector<UINT64> tlsObservedSlotValueIds;
	static thread_local std::vector<
		DX12SimulateContextComputeTemplate::RuntimeObservedSlot>
		tlsObservedSlotsRelative;
	static thread_local std::vector<SIZE_T> tlsCpuHandles;
	static thread_local std::vector<DX12DescriptorSummary> tlsDescriptors;
	static thread_local std::vector<uint8_t> tlsFound;
	static thread_local std::vector<uint8_t> tlsPending;
	static thread_local std::vector<UINT> tlsDescriptorOffsets;
	static thread_local std::vector<UINT64> tlsDescriptorIndices;
	static thread_local std::vector<UINT> tlsDescriptorSlots;

	std::vector<DX12CurrentComputeUavBinding> &localUavs = tlsLocalUavs;
	std::vector<DX12CurrentComputeUavBinding> &localSrvs = tlsLocalSrvs;
	std::vector<DX12CurrentComputeUavBinding> &localCbvs = tlsLocalCbvs;
	std::vector<DX12CurrentRootConstants> &localConstants = tlsLocalConstants;
	std::vector<UINT> &observedRootParameters = tlsObservedRootParameters;
	std::vector<UINT> &observedSlots = tlsObservedSlots;
	std::vector<UINT> &observedRoots = tlsObservedRoots;
	std::vector<UINT> &observedOffsets = tlsObservedOffsets;
	std::vector<UINT64> &observedSlotVersions = tlsObservedSlotVersions;
	std::vector<UINT64> &observedSlotValueIds = tlsObservedSlotValueIds;
	std::vector<DX12SimulateContextComputeTemplate::RuntimeObservedSlot>
		&observedSlotsRelative = tlsObservedSlotsRelative;
	localUavs.clear();
	localSrvs.clear();
	localCbvs.clear();
	localConstants.clear();
	observedRootParameters.clear();
	observedSlots.clear();
	observedRoots.clear();
	observedOffsets.clear();
	observedSlotVersions.clear();
	observedSlotValueIds.clear();
	observedSlotsRelative.clear();

	DX12_HOTPATH_TIME_BEGIN(BindCollect);

	// Root-signature-driven iteration: use declared parameter count as upper bound.
	// If metadata is temporarily unavailable, keep the cheap root descriptor /
	// constant paths alive, but never turn that miss into an O(heap_size) scan.
	const UINT maxRoot = hasParsedRootSignatureSummary
		? static_cast<UINT>(cachedSummary->parameters.size())
		: MaxRootParameters;
	for (UINT root = 0; root < maxRoot; ++root) {
		bool affectsPreSkinBindings = !hasParsedRootSignatureSummary;
		if (hasParsedRootSignatureSummary) {
			const DX12RootParameterSummary *parameter =
				DX12BindingTrackerQuery::FindRootParameter(*cachedSummary, root);
			if (parameter) {
				affectsPreSkinBindings =
					parameter->parameterType !=
						D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
				for (const DX12RootDescriptorRangeSummary &range : parameter->ranges) {
					if (range.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV ||
							range.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV ||
							range.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_CBV) {
						affectsPreSkinBindings = true;
						break;
					}
				}
			}
		}
		if (affectsPreSkinBindings)
			observedRootParameters.push_back(root);
	}

	// Collect root constants directly from snapshot
	for (UINT root = 0; root < maxRoot; ++root) {
		const RootConstantsState &rc = snapshot.computeRootConstants[root];
		if (!rc.valid)
			continue;
		DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinBindingConstant();
		DX12CurrentRootConstants current;
		current.rootParameterIndex = root;
		current.maxSet = rc.maxSet;
		memcpy(current.values, rc.values, sizeof(current.values));
		memcpy(current.valueValid, rc.valueValid, sizeof(current.valueValid));
		localConstants.push_back(current);
		usesRootValues = true;
	}

	// Collect descriptor table bindings.
	// With root signature: iterate exactly the declared ranges (correct offset/count).
	// P5: Bulk descriptor lookup: collect ALL CPU handles from ALL ranges first,
	// then issue a SINGLE DX12FindDescriptorSummariesByCpuHandleNoWait call,
	// then distribute results back to per-range output vectors.
	// Without root signature: scan to the descriptor heap boundary, classify by kind.

	// Per-descriptor metadata for bulk lookup distribution.
	// Slot i tracks: output vector, kind string, root param index, range metadata, heap data.
	static thread_local std::vector<std::vector<DX12CurrentComputeUavBinding>*> tlsOutVec;
	static thread_local std::vector<const char*> tlsKind;
	static thread_local std::vector<UINT> tlsRootIndex;
	static thread_local std::vector<UINT> tlsRangeIndex;
	static thread_local std::vector<UINT> tlsRangeShaderVisibility;
	static thread_local std::vector<UINT> tlsRangeBaseShaderRegister;
	static thread_local std::vector<UINT> tlsRangeRegisterSpace;
	static thread_local std::vector<UINT> tlsRangeEffectiveOffset;
	static thread_local std::vector<UINT> tlsCount;
	static thread_local std::vector<UINT64> tlsTableBaseIndex;
	static thread_local std::vector<SIZE_T> tlsTableCpuStart;
	static thread_local std::vector<UINT64> tlsTableGpuStart;
	static thread_local std::vector<UINT> tlsTableIncrement;
	static thread_local std::vector<ID3D12DescriptorHeap*> tlsTableHeap;

	tlsCpuHandles.clear();
	tlsDescriptorOffsets.clear();
	tlsDescriptorIndices.clear();
	tlsDescriptorSlots.clear();
	tlsOutVec.clear();
	tlsKind.clear();
	tlsRootIndex.clear();
	tlsRangeIndex.clear();
	tlsRangeShaderVisibility.clear();
	tlsRangeBaseShaderRegister.clear();
	tlsRangeRegisterSpace.clear();
	tlsRangeEffectiveOffset.clear();
	tlsCount.clear();
	tlsTableBaseIndex.clear();
	tlsTableCpuStart.clear();
	tlsTableGpuStart.clear();
	tlsTableIncrement.clear();
	tlsTableHeap.clear();

	// Phase 1: Collect all CPU handles from root-signature ranges.
	size_t totalHandleCount = 0;
	ID3D12DescriptorHeap *slotLookupHeap = nullptr;
	bool canUseSlotLookup = true;
	for (UINT root = 0; root < maxRoot; ++root) {
		const RootTableState &table = snapshot.computeTables[root];
		if (!table.valid)
			continue;
		DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinBindingRootTable();

		DX12DescriptorHeapSummary heap = {};
		if (!DX12BindingTrackerQueryCacheUtils::ResolveRuntimeTableHeap(snapshot, table, &heap)) {
			DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinBindingRuntimeTable(false);
			allRuntimeDescriptorsStable = false;
			continue;
		}
		DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinBindingRuntimeTable(
			table.runtimeHeapResolved);

		const UINT64 tableBaseIndex = (table.baseDescriptor.ptr - heap.gpuStart) / heap.increment;
		if (tableBaseIndex >= heap.numDescriptors)
			continue;

		const DX12RootParameterSummary *parameter =
			hasParsedRootSignatureSummary ?
				DX12BindingTrackerQuery::FindRootParameter(*cachedSummary, root) :
				nullptr;

		if (!parameter || parameter->parameterType != D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
			// Without parsed root-signature metadata, do not speculate about the
			// descriptor table extent. DX11-style fast rejection is preferable to
			// scanning an arbitrary portion of the heap on the dispatch hot path.
			continue;
		}

		// Precompute heap data shared by all ranges in this table.
		const SIZE_T tableCpuStart = heap.cpuStart + static_cast<SIZE_T>(tableBaseIndex) * heap.increment;
		const UINT64 tableGpuStart = heap.gpuStart + tableBaseIndex * heap.increment;

		for (const DX12RootDescriptorRangeSummary &range : parameter->ranges) {
			DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinBindingRange();

			std::vector<DX12CurrentComputeUavBinding> *out = nullptr;
			const char *kind = nullptr;
			if (range.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV) {
				out = &localUavs; kind = "UAV";
			} else if (range.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV) {
				out = &localSrvs; kind = "SRV";
			} else if (range.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_CBV) {
				out = &localCbvs; kind = "CBV";
			} else {
				continue;
			}

			const UINT count = DX12BindingTrackerQuery::DescriptorCountForRange(heap, tableBaseIndex, range);
			for (UINT offset = 0; offset < count; ++offset) {
				const UINT64 di = tableBaseIndex + range.effectiveOffset + offset;
				if (di >= heap.numDescriptors)
					break;
				const SIZE_T cpuHandle = heap.cpuStart + static_cast<SIZE_T>(di) * heap.increment;
				DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinBindingFullDescriptorLookup();
				if (!heap.heap || di > static_cast<UINT64>(UINT_MAX)) {
					canUseSlotLookup = false;
				} else if (!slotLookupHeap) {
					slotLookupHeap = heap.heap;
				} else if (slotLookupHeap != heap.heap) {
					canUseSlotLookup = false;
				}
				if (canStoreRuntimeBindings) {
					if (!heap.heap || di > static_cast<UINT64>(UINT_MAX)) {
						canStoreRuntimeBindings = false;
					} else if (!observedSlotHeap) {
						observedSlotHeap = heap.heap;
					} else if (observedSlotHeap != heap.heap) {
						canStoreRuntimeBindings = false;
					}
					if (canStoreRuntimeBindings)
						observedSlots.push_back(static_cast<UINT>(di));
					if (canStoreRuntimeBindings) {
						observedRoots.push_back(root);
						observedOffsets.push_back(static_cast<UINT>(
							di - tableBaseIndex));
					}
				}

				tlsCpuHandles.push_back(cpuHandle);
				tlsDescriptorSlots.push_back(static_cast<UINT>(di));
				tlsDescriptorOffsets.push_back(offset);
				tlsOutVec.push_back(out);
				tlsKind.push_back(kind);
				tlsRootIndex.push_back(root);
				tlsRangeIndex.push_back(range.rangeIndex);
				tlsRangeShaderVisibility.push_back(range.shaderVisibility);
				tlsRangeBaseShaderRegister.push_back(range.baseShaderRegister);
				tlsRangeRegisterSpace.push_back(range.registerSpace);
				tlsRangeEffectiveOffset.push_back(range.effectiveOffset);
				tlsCount.push_back(count);
				tlsTableBaseIndex.push_back(tableBaseIndex);
				tlsTableCpuStart.push_back(tableCpuStart);
				tlsTableGpuStart.push_back(tableGpuStart);
				tlsTableIncrement.push_back(heap.increment);
				tlsTableHeap.push_back(heap.heap);
				totalHandleCount++;
			}
		}
	}

	// Phase 2: Single bulk descriptor lookup for all root-signature ranges.
	if (totalHandleCount > 0) {
		tlsDescriptors.resize(totalHandleCount);
		tlsFound.resize(totalHandleCount);
		tlsPending.resize(totalHandleCount);
		if (canUseSlotLookup && slotLookupHeap) {
			memset(tlsPending.data(), 0, totalHandleCount * sizeof(uint8_t));
			DX12FindDescriptorSummariesByHeapSlot(
				slotLookupHeap,
				tlsDescriptorSlots.data(),
				totalHandleCount,
				tlsDescriptors.data(),
				tlsFound.data(),
				true); // needFullResource=true is the safe superset for UAV+SRV+CBV.
		} else {
			DX12FindDescriptorSummariesByCpuHandleNoWait(
				tlsCpuHandles.data(),
				totalHandleCount,
				tlsDescriptors.data(),
				tlsFound.data(),
				tlsPending.data(),
				true); // needFullResource=true is the safe superset for UAV+SRV+CBV.
		}

		LONG64 descriptorHits = 0;
		LONG64 descriptorMisses = 0;
		LONG64 descriptorPending = 0;
		for (size_t i = 0; i < totalHandleCount; ++i) {
			if (tlsPending[i]) {
				descriptorPending++;
				allRuntimeDescriptorsStable = false;
			} else if (tlsFound[i]) {
				descriptorHits++;
			} else {
				descriptorMisses++;
				allRuntimeDescriptorsStable = false;
			}
		}
		DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinBindingDescriptorLookupResult(
			descriptorHits, descriptorMisses, descriptorPending);

		// Phase 3: Distribute results to per-range output vectors.
		for (size_t i = 0; i < totalHandleCount; ++i) {
			if (!tlsFound[i] || tlsDescriptors[i].kind != tlsKind[i])
				continue;

			std::vector<DX12CurrentComputeUavBinding> *out = tlsOutVec[i];

			DX12CurrentComputeUavBinding binding;
			binding.rootParameterIndex = tlsRootIndex[i];
			binding.rangeIndex = tlsRangeIndex[i];
			binding.shaderVisibility = tlsRangeShaderVisibility[i];
			binding.shaderRegister =
				tlsRangeBaseShaderRegister[i] + tlsDescriptorOffsets[i];
			binding.registerSpace = tlsRangeRegisterSpace[i];
			binding.descriptorOffset =
				tlsRangeEffectiveOffset[i] + tlsDescriptorOffsets[i];
			binding.descriptorIncrement = tlsTableIncrement[i];
			binding.tableCopyCount = tlsCount[i];
			binding.tableCpuStart = tlsTableCpuStart[i];
			binding.tableGpuStart.ptr = tlsTableGpuStart[i];
			binding.tableHeap = tlsTableHeap[i];
			binding.descriptor = tlsDescriptors[i];
			binding.hasDescriptor = true;
			out->push_back(std::move(binding));
		}
	}

	// Collect root descriptors (bare GPU VA, no heap needed)
	for (UINT root = 0; root < maxRoot; ++root) {
		const RootDescriptorState &rd = snapshot.computeRootDescriptors[root];
		if (!rd.valid)
			continue;
		DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinBindingRootDescriptor();

		const DX12RootParameterSummary *parameter =
			hasParsedRootSignatureSummary ?
				DX12BindingTrackerQuery::FindRootParameter(*cachedSummary, root) :
				nullptr;

		DX12CurrentComputeUavBinding binding;
		binding.rootParameterIndex = root;
		binding.rootDescriptor = true;
		binding.gpuVirtualAddress = rd.address;
		if (parameter) {
			binding.shaderVisibility = parameter->shaderVisibility;
			binding.shaderRegister = parameter->shaderRegister;
			binding.registerSpace = parameter->registerSpace;
		}
		usesRootValues = true;

		if (rd.type == D3D12_ROOT_PARAMETER_TYPE_UAV)
			localUavs.push_back(std::move(binding));
		else if (rd.type == D3D12_ROOT_PARAMETER_TYPE_SRV)
			localSrvs.push_back(std::move(binding));
		else if (rd.type == D3D12_ROOT_PARAMETER_TYPE_CBV)
			localCbvs.push_back(std::move(binding));
	}

	DX12_HOTPATH_TIME_END(BindCollect);

	const bool foundAny = !localUavs.empty() || !localSrvs.empty() ||
		!localCbvs.empty() || !localConstants.empty();
	const UINT64 descriptorHeapVersionAfter =
		preferRuntimeSnapshot ?
			runtimeState->cbvSrvUavHeapVersion :
			DX12GetDescriptorHeapVersion(snapshot.cbvSrvUavHeap);
	const UINT64 descriptorQuerySerialAfter = CurrentDescriptorQuerySerial();
	// The global descriptor-query serial bumps on any descriptor write anywhere
	// in the process, including slots this dispatch never observes. Requiring it
	// to stay constant across the collect pass made the cache un-storeable in
	// descriptor-churn games (observed 0% hit rate): the serial changed mid-collect
	// even when every observed slot was stable. The load path already re-reads each
	// observed slot's version/value-id to prove correctness, so keep only the
	// heap-structure stability check here.
	const bool descriptorStateStable =
		descriptorHeapVersionAfter == descriptorHeapVersionBefore;
	if (allRuntimeDescriptorsStable && descriptorStateStable) {
		DX12PreSkinBindingCacheUtils::Store(
			&tlsPreSkinBindingCache,
			bindingCacheKey,
			localUavs,
			localSrvs,
			localCbvs,
			localConstants,
			foundAny);
	}
	if (canStoreRuntimeBindings && allRuntimeDescriptorsStable && descriptorStateStable) {
		observedSlotVersions.resize(observedSlots.size());
		observedSlotValueIds.resize(observedSlots.size());
		bool observedSlotsValid = true;
		if (!observedSlots.empty()) {
			if (observedSlotHeap &&
					runtimeState->runtimeCbvSrvUavHeapResolved &&
					runtimeState->runtimeCbvSrvUavHeap.heap ==
						observedSlotHeap) {
				observedSlotsValid = RuntimeReadSlotIdentitiesFast(
					runtimeState->runtimeCbvSrvUavHeap,
					observedSlots.data(),
					observedSlots.size(),
					observedSlotVersions.data(),
					observedSlotValueIds.data());
			} else {
				observedSlotsValid =
					observedSlotHeap &&
					DX12GetDescriptorSlotIdentities(
						observedSlotHeap,
						observedSlots.data(),
						observedSlots.size(),
						observedSlotVersions.data(),
						observedSlotValueIds.data());
			}
		}
		if (observedSlotsValid) {
			const UINT64 runtimeDependencyFingerprint = bindingCacheKey.fingerprint;
			observedSlotsRelative.resize(observedSlots.size());
			for (size_t slotIndex = 0;
					slotIndex < observedSlots.size(); ++slotIndex) {
				DX12SimulateContextComputeTemplate::RuntimeObservedSlot &slot =
					observedSlotsRelative[slotIndex];
				slot.root = observedRoots[slotIndex];
				slot.offset = observedOffsets[slotIndex];
				slot.version = observedSlotVersions[slotIndex];
				slot.valueId = observedSlotValueIds[slotIndex];
			}
			StoreRuntimePreSkinBindings(
				runtimeState,
				computeBindingSerial,
				descriptorQuerySerialAfter,
				runtimeDependencyFingerprint,
				usesRootValues,
				foundAny,
				observedRootParameters,
				observedSlots,
				observedSlotVersions,
				observedSlotValueIds,
				observedSlotsRelative,
				localUavs,
				localSrvs,
				localCbvs,
				localConstants);
			if (dependencyFingerprint)
				*dependencyFingerprint = runtimeDependencyFingerprint;
			DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinBindingCacheStore();
		}
	} else if (!allRuntimeDescriptorsStable || !descriptorStateStable) {
		DX12LogManager::DebugFields("DX12PreSkinRuntimeDescriptorSnapshot",
			"event=skip_cache_unstable,commandList=%p,uavs=%zu,srvs=%zu,cbvs=%zu",
			commandList,
			localUavs.size(),
			localSrvs.size(),
			localCbvs.size());
	}

	if (uavs) *uavs = std::move(localUavs);
	if (srvs) *srvs = std::move(localSrvs);
	if (cbvs) *cbvs = std::move(localCbvs);
	if (constants) *constants = std::move(localConstants);

	return foundAny;
}

void DX12BindingTracker::DrainPendingEvents(const char *reason)
{
	DX12BindingCollectQueue::DrainForSnapshot(reason ? reason : "BindingTracker");
}

UINT64 DX12BindingTracker::CurrentCaptureSessionId()
{
	return DX12CaptureLedger::CurrentCaptureSessionId();
}

UINT64 DX12BindingTracker::CaptureFenceSequence()
{
	return DX12BindingCollectQueue::LastSubmittedSequence();
}

bool DX12BindingTracker::PopulateActiveEventSource(
	DX12FrameEventSource *source,
	const char *reason)
{
	if (!source)
		return false;

	const UINT64 captureSessionId = DX12BindingTracker::CurrentCaptureSessionId();
	const UINT64 effectiveCaptureSessionId = captureSessionId != 0 ?
		captureSessionId :
		DX12CaptureLedger::LastCaptureSessionId();
	if (effectiveCaptureSessionId == 0) {
		*source = DX12FrameEventSource();
		return false;
	}
	return DX12BindingTracker::PopulateCaptureEventSource(
		source,
		effectiveCaptureSessionId,
		reason ? reason : "PopulateActiveEventSource");
}

bool DX12BindingTracker::PopulateCaptureEventSource(
	DX12FrameEventSource *source,
	UINT64 captureSessionId,
	const char *reason)
{
	if (!source)
		return false;
	if (captureSessionId == 0) {
		*source = DX12FrameEventSource();
		return false;
	}

	const UINT64 targetSequence = DX12BindingTracker::CaptureFenceSequence();
	DX12BindingCollectQueue::WaitForSubmittedSequence(
		targetSequence,
		reason ? reason : "PopulateCaptureEventSource");

	*source = DX12FrameEventSource();

	DX12CaptureLedgerSnapshot ledgerSnapshot;
	DX12CaptureLedger::GetSnapshot(&ledgerSnapshot);
	if (ledgerSnapshot.captureSessionId != captureSessionId) {
		DX12FrameAnalysis::LogJsonFunc("BindingCaptureSessionMismatch",
			"\"requested_capture_session_id\":%llu,\"snapshot_capture_session_id\":%llu,\"target_sequence\":%llu",
			static_cast<unsigned long long>(captureSessionId),
			static_cast<unsigned long long>(ledgerSnapshot.captureSessionId),
			static_cast<unsigned long long>(targetSequence));
		return false;
	}

	source->captureSessionId = ledgerSnapshot.captureSessionId;
	source->ownedEvents = std::move(ledgerSnapshot.events);
	source->ownedOutputEvents = std::move(ledgerSnapshot.outputEvents);
	source->events = &source->ownedEvents;
	source->outputEvents = &source->ownedOutputEvents;
	source->droppedEvents = ledgerSnapshot.droppedEvents;
	source->droppedOutputEvents = ledgerSnapshot.droppedOutputEvents;

	DX12CaptureLedger::LogSnapshotHealth(
		reason ? reason : "PopulateCaptureEventSource",
		ledgerSnapshot);
	return !source->ownedEvents.empty() || !source->ownedOutputEvents.empty() ||
		source->droppedEvents != 0 || source->droppedOutputEvents != 0;
}

void DX12BindingTracker::ReleaseRetainedIaResources(CommandListBindingState *state)
{
	if (!state)
		return;
	for (UINT slot = 0; slot < MaxVertexBufferSlots; ++slot) {
		if (state->vertexBufferResolved[slot] && state->vertexBufferResources[slot].resource) {
			DX12ReleaseRetainedResource(state->vertexBufferResources[slot].resource);
			state->vertexBufferResources[slot] = {};
			state->vertexBufferResolved[slot] = false;
		}
	}
	if (state->indexBufferResolved && state->indexBufferResource.resource) {
		DX12ReleaseRetainedResource(state->indexBufferResource.resource);
		state->indexBufferResource = {};
		state->indexBufferResolved = false;
	}
}

void DX12BindingTracker::RetainEventIaResources(BindingEvent *event)
{
	if (!event)
		return;
	for (UINT slot = 0; slot < MaxVertexBufferSlots; ++slot) {
		if (!event->vertexBufferResolved[slot] || !event->vertexBufferResources[slot].resource)
			continue;
		if (!DX12RetainResource(event->vertexBufferResources[slot].resource)) {
			event->vertexBufferResources[slot] = {};
			event->vertexBufferResolved[slot] = false;
		}
	}
	if (event->indexBufferResolved && event->indexBufferResource.resource &&
		!DX12RetainResource(event->indexBufferResource.resource)) {
		event->indexBufferResource = {};
		event->indexBufferResolved = false;
	}
}

void DX12BindingTracker::ReleaseEventIaResources(BindingEvent *event)
{
	if (!event)
		return;
	for (UINT slot = 0; slot < MaxVertexBufferSlots; ++slot) {
		if (event->vertexBufferResolved[slot] && event->vertexBufferResources[slot].resource)
			DX12ReleaseRetainedResource(event->vertexBufferResources[slot].resource);
		event->vertexBufferResources[slot] = {};
		event->vertexBufferResolved[slot] = false;
	}
	if (event->indexBufferResolved && event->indexBufferResource.resource)
		DX12ReleaseRetainedResource(event->indexBufferResource.resource);
	event->indexBufferResource = {};
	event->indexBufferResolved = false;
}

void DX12BindingTracker::RetainStateIaResourcesForAsync(CommandListBindingState *state)
{
	if (!state)
		return;
	for (UINT slot = 0; slot < MaxVertexBufferSlots; ++slot) {
		if (!state->vertexBufferResolved[slot] || !state->vertexBufferResources[slot].resource)
			continue;
		if (!DX12RetainResource(state->vertexBufferResources[slot].resource)) {
			state->vertexBufferResources[slot] = {};
			state->vertexBufferResolved[slot] = false;
		}
	}
	if (state->indexBufferResolved && state->indexBufferResource.resource &&
		!DX12RetainResource(state->indexBufferResource.resource)) {
		state->indexBufferResource = {};
		state->indexBufferResolved = false;
	}
}

void DX12BindingTracker::RefreshPipelineShaderInfo(CommandListBindingState *state)
{
	if (!state)
		return;

	state->pipelineShaderInfo = {};
	state->pipelineShaderInfoValid = false;
	if (!state->pipelineState)
		return;

	if (DX12GetPipelineStateShaderInfo(state->pipelineState, &state->pipelineShaderInfo))
		state->pipelineShaderInfoValid = true;
}

void DX12BindingTracker::InvalidatePipelineShaderInfo(CommandListBindingState *state)
{
	if (!state)
		return;
	state->pipelineShaderInfo = {};
	state->pipelineShaderInfoValid = false;
}

void DX12BindingTracker::BumpBindingQuerySerial(CommandListBindingState *state)
{
	if (!state)
		return;
	if (state->bindingQuerySerial == UINT64_MAX)
		state->bindingQuerySerial = 1;
	else
		++state->bindingQuerySerial;
}

bool DX12BindingTracker::ResolvePipelineShaderInfoOnDemand(CommandListBindingState *state)
{
	if (!state || !state->pipelineState)
		return false;
	if (state->pipelineShaderInfoValid)
		return true;
	DX12BindingTracker::RefreshPipelineShaderInfo(state);
	return state->pipelineShaderInfoValid;
}

bool DX12BindingTracker::SnapshotBindingState(
	ID3D12GraphicsCommandList *commandList,
	CommandListBindingState *snapshot,
	UINT64 *bindingQuerySerial)
{
	if (!commandList || !snapshot)
		return false;

	const DX12SimulateContext *runtimeState =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	const DX12CommandListTrackingState tracking =
		runtimeState ?
			DX12CommandListRuntime::GetTrackingState(
				const_cast<DX12SimulateContext*>(runtimeState)) :
			DX12CommandListTrackingState();
	if (runtimeState && !tracking.trackBindings) {
		if (!BuildRuntimeBindingSnapshot(runtimeState, snapshot))
			return false;
		if (bindingQuerySerial)
			*bindingQuerySerial =
				snapshot->bindingQuerySerial ? snapshot->bindingQuerySerial : 1;
		return true;
	}

	bool found = false;
	UINT64 serial = 0;
	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockShared(&shard.lock);
	auto it = shard.states.find(commandList);
	if (it != shard.states.end()) {
		*snapshot = it->second;
		serial = it->second.bindingQuerySerial;
		found = true;
	}
	ReleaseSRWLockShared(&shard.lock);
	if (runtimeState) {
		CommandListBindingState runtimeSnapshot;
		if (BuildRuntimeBindingSnapshot(runtimeState, &runtimeSnapshot)) {
			if (!found) {
				*snapshot = runtimeSnapshot;
				serial = runtimeSnapshot.bindingQuerySerial;
				found = true;
			} else {
				snapshot->graphicsRootSignature =
					runtimeSnapshot.graphicsRootSignature;
				snapshot->computeRootSignature =
					runtimeSnapshot.computeRootSignature;
				snapshot->cbvSrvUavHeap = runtimeSnapshot.cbvSrvUavHeap;
				snapshot->samplerHeap = runtimeSnapshot.samplerHeap;
				snapshot->runtimeCbvSrvUavHeapResolved =
					runtimeSnapshot.runtimeCbvSrvUavHeapResolved;
				snapshot->runtimeCbvSrvUavHeap =
					runtimeSnapshot.runtimeCbvSrvUavHeap;
				snapshot->runtimeSamplerHeapResolved =
					runtimeSnapshot.runtimeSamplerHeapResolved;
				snapshot->runtimeSamplerHeap =
					runtimeSnapshot.runtimeSamplerHeap;
				snapshot->graphicsTableValidCount =
					runtimeSnapshot.graphicsTableValidCount;
				snapshot->computeTableValidCount =
					runtimeSnapshot.computeTableValidCount;
				memcpy(
					snapshot->graphicsTables,
					runtimeSnapshot.graphicsTables,
					sizeof(snapshot->graphicsTables));
				memcpy(
					snapshot->computeTables,
					runtimeSnapshot.computeTables,
					sizeof(snapshot->computeTables));
				memcpy(
					snapshot->graphicsRootDescriptors,
					runtimeSnapshot.graphicsRootDescriptors,
					sizeof(snapshot->graphicsRootDescriptors));
				memcpy(
					snapshot->computeRootDescriptors,
					runtimeSnapshot.computeRootDescriptors,
					sizeof(snapshot->computeRootDescriptors));
				memcpy(
					snapshot->graphicsRootConstants,
					runtimeSnapshot.graphicsRootConstants,
					sizeof(snapshot->graphicsRootConstants));
				memcpy(
					snapshot->computeRootConstants,
					runtimeSnapshot.computeRootConstants,
					sizeof(snapshot->computeRootConstants));
				serial ^= runtimeSnapshot.bindingQuerySerial;
			}
		}
	}
	if (bindingQuerySerial)
		*bindingQuerySerial = serial ? serial : 1;
	return found;
}

void DX12BindingTracker::ReleaseResolvedIaResource(
	DX12BufferResourceSummary *summary, bool *resolved)
{
	if (!summary || !resolved)
		return;
	if (*resolved && summary->resource)
		DX12ReleaseRetainedResource(summary->resource);
	*summary = {};
	*resolved = false;
}

// Resolve all deferred (unresolved) IA resources in state at draw-snapshot time.
// This is the counterpart to the "defer resource resolution" optimization in
// SetVertexBuffers / SetIndexBuffer. Called inside the shard lock during
// RecordDrawInstanced / RecordDrawIndexedInstanced just before state snapshot.
static void ResolveDeferredIaResources(CommandListBindingState *state)
{
	if (!state)
		return;
	for (UINT slot = 0; slot < MaxVertexBufferSlots; ++slot) {
		if (state->vertexBufferValid[slot] && !state->vertexBufferResolved[slot]) {
			DX12BindingTracker::ResolveAndRetainIaResource(
				state->vertexBuffers[slot].BufferLocation,
				state->vertexBuffers[slot].SizeInBytes,
				&state->vertexBufferResources[slot],
				&state->vertexBufferResolved[slot]);
		}
	}
	if (state->indexBufferValid && !state->indexBufferResolved) {
		DX12BindingTracker::ResolveAndRetainIaResource(
			state->indexBuffer.BufferLocation,
			state->indexBuffer.SizeInBytes,
			&state->indexBufferResource,
			&state->indexBufferResolved);
	}
}

void DX12BindingTracker::ResolveAndRetainIaResource(
	UINT64 gpuVirtualAddress, UINT64 size,
	DX12BufferResourceSummary *summary, bool *resolved)
{
	if (!summary || !resolved) {
		return;
	}

	*summary = {};
	*resolved = false;
	if (!gpuVirtualAddress || !size)
		return;
	if (!DX12ResolveBufferResourceByGpuVa(gpuVirtualAddress, size, summary))
		return;
	if (summary->resource && !DX12RetainResource(summary->resource)) {
		*summary = {};
		return;
	}
	*resolved = true;
}

bool DX12BindingTracker::BuildEvent(
	BindingEvent *event,
	const char *kind,
	ID3D12GraphicsCommandList *commandList,
	const CommandListBindingState &state,
	UINT64 descriptorEpoch,
	UINT64 captureSessionId)
{
	if (!event)
		return false;
	*event = BindingEvent();
	event->kind = kind ? kind : "";
	event->captureSessionId = captureSessionId;
	const bool isDrawKind = kind && (!strcmp(kind, "draw") || !strcmp(kind, "draw_indexed"));
	const bool isDispatchKind = kind && !strcmp(kind, "dispatch");
	event->drawId = isDrawKind ? state.drawSerial : 0;
	event->dispatchId = isDispatchKind ? state.dispatchSerial : 0;
	event->descriptorEpoch = descriptorEpoch;
	DX12ResidentMaintenanceTelemetryUtils::RecordBindingEventRecorded();
	DX12ResidentMaintenanceTelemetryUtils::RecordDescriptorEpochWrite();
	event->commandList = commandList;
	event->pipelineState = state.pipelineState;
	event->graphicsRootSignature = state.graphicsRootSignature;
	event->computeRootSignature = state.computeRootSignature;
	event->cbvSrvUavHeap = state.cbvSrvUavHeap;
	event->samplerHeap = state.samplerHeap;
	if (isDrawKind) {
		// Draw events only need graphics state
		memcpy(event->graphicsTables, state.graphicsTables, sizeof(event->graphicsTables));
		memcpy(event->graphicsRootDescriptors, state.graphicsRootDescriptors, sizeof(event->graphicsRootDescriptors));
		memcpy(event->graphicsRootConstants, state.graphicsRootConstants, sizeof(event->graphicsRootConstants));
		memcpy(event->vertexBuffers, state.vertexBuffers, sizeof(event->vertexBuffers));
		memcpy(event->vertexBufferValid, state.vertexBufferValid, sizeof(event->vertexBufferValid));
		memcpy(event->vertexBufferResources, state.vertexBufferResources, sizeof(event->vertexBufferResources));
		memcpy(event->vertexBufferResolved, state.vertexBufferResolved, sizeof(event->vertexBufferResolved));
	} else if (isDispatchKind) {
		// Dispatch events only need compute state
		memcpy(event->computeTables, state.computeTables, sizeof(event->computeTables));
		memcpy(event->computeRootDescriptors, state.computeRootDescriptors, sizeof(event->computeRootDescriptors));
		memcpy(event->computeRootConstants, state.computeRootConstants, sizeof(event->computeRootConstants));
	} else {
		// Other event kinds: copy everything
		memcpy(event->graphicsTables, state.graphicsTables, sizeof(event->graphicsTables));
		memcpy(event->computeTables, state.computeTables, sizeof(event->computeTables));
		memcpy(event->graphicsRootDescriptors, state.graphicsRootDescriptors, sizeof(event->graphicsRootDescriptors));
		memcpy(event->computeRootDescriptors, state.computeRootDescriptors, sizeof(event->computeRootDescriptors));
		memcpy(event->graphicsRootConstants, state.graphicsRootConstants, sizeof(event->graphicsRootConstants));
		memcpy(event->computeRootConstants, state.computeRootConstants, sizeof(event->computeRootConstants));
		memcpy(event->vertexBuffers, state.vertexBuffers, sizeof(event->vertexBuffers));
		memcpy(event->vertexBufferValid, state.vertexBufferValid, sizeof(event->vertexBufferValid));
		memcpy(event->vertexBufferResources, state.vertexBufferResources, sizeof(event->vertexBufferResources));
		memcpy(event->vertexBufferResolved, state.vertexBufferResolved, sizeof(event->vertexBufferResolved));
	}
	event->indexBuffer = state.indexBuffer;
	event->indexBufferValid = state.indexBufferValid;
	event->indexBufferResource = state.indexBufferResource;
	event->indexBufferResolved = state.indexBufferResolved;
	event->primitiveTopology = state.primitiveTopology;
	if (state.pipelineState) {
		if (state.pipelineShaderInfoValid) {
			event->shaderInfo = state.pipelineShaderInfo;
		} else {
			DX12GetPipelineStateShaderInfo(state.pipelineState, &event->shaderInfo);
		}
		ID3D12RootSignature *psoRootSignature = nullptr;
		if (DX12GetPsoRootSignature(event->shaderInfo.psoIndex, &psoRootSignature)) {
			if (!event->graphicsRootSignature)
				event->graphicsRootSignature = psoRootSignature;
			if (!event->computeRootSignature)
				event->computeRootSignature = psoRootSignature;
		}
	}
	return true;
}

bool DX12BindingTracker::BuildEvent(
	BindingEvent *event, const char *kind, ID3D12GraphicsCommandList *commandList,
	const CommandListBindingState &state)
{
	return DX12BindingTracker::BuildEvent(
		event,
		kind,
		commandList,
		state,
		CurrentDescriptorEpoch(),
		DX12BindingTracker::CurrentCaptureSessionId());
}

void DX12BindingTracker::StoreEventLocked(const BindingEvent &event)
{
	DX12BindingTracker::RecordTemplateEvent(event.commandList, event);
}

void DX12BindingTracker::StoreEventLocked(
	const char *kind, ID3D12GraphicsCommandList *commandList,
	const CommandListBindingState &state)
{
	BindingEvent event;
	if (!DX12BindingTracker::BuildEvent(&event, kind, commandList, state))
		return;
	DX12BindingTracker::StoreEventLocked(event);
}

void DX12BindingTracker::StoreOutputEventLocked(
	const char *kind, ID3D12GraphicsCommandList *commandList,
	const CommandListBindingState &state, UINT slot, SIZE_T cpuHandle, UINT64 drawId, UINT64 captureSessionId)
{
	if (!cpuHandle)
		return;
	const UINT64 dispatchId = 0;
	OutputBindingEvent event;
	event.serial = 0;
	event.captureSessionId = captureSessionId;
	event.kind = kind ? kind : "";
	event.commandList = commandList;
	event.pipelineState = state.pipelineState;
	event.drawId = drawId;
	event.dispatchId = dispatchId;
	event.slot = slot;
	event.cpuHandle = cpuHandle;
	if (state.pipelineState) {
		if (state.pipelineShaderInfoValid)
			event.shaderInfo = state.pipelineShaderInfo;
		else
			DX12GetPipelineStateShaderInfo(state.pipelineState, &event.shaderInfo);
	}
	DX12BindingTracker::RecordTemplateOutputEvent(commandList, event);
}

void DX12BindingTracker::StoreCurrentOutputTargetsLocked(
	ID3D12GraphicsCommandList *commandList, const CommandListBindingState &state, UINT64 captureSessionId)
{
	if (captureSessionId == 0)
		captureSessionId = DX12BindingTracker::CurrentCaptureSessionId();
	for (UINT i = 0; i < MaxRenderTargetSlots; ++i) {
		if (state.renderTargetValid[i])
			DX12BindingTracker::StoreOutputEventLocked("RTV", commandList, state, i,
				state.renderTargets[i].ptr, state.drawSerial, captureSessionId);
	}
	if (state.depthStencilValid)
		DX12BindingTracker::StoreOutputEventLocked("DSV", commandList, state, 0,
			state.depthStencil.ptr, state.drawSerial, captureSessionId);
}

void DX12BindingTracker::ReleaseRecordedEvent(CommandListRecordedEvent *event)
{
	if (!event)
		return;
	if (event->kind == CommandListRecordedEventKind::Binding)
		DX12BindingTracker::ReleaseEventIaResources(&event->binding);
}

void DX12BindingTracker::ReleaseRecordedEventsLocked(
	BindingShard *shard, ID3D12GraphicsCommandList *commandList)
{
	if (!shard || !commandList)
		return;
	auto it = shard->recordedEvents.find(commandList);
	if (it == shard->recordedEvents.end())
		return;
	for (CommandListRecordedEvent &event : it->second)
		DX12BindingTracker::ReleaseRecordedEvent(&event);
	shard->recordedEvents.erase(it);
}

void DX12BindingTracker::RecordTemplateEvent(
	ID3D12GraphicsCommandList *commandList, const BindingEvent &event)
{
	if (!commandList)
		return;
	CommandListRecordedEvent recorded;
	recorded.kind = CommandListRecordedEventKind::Binding;
	recorded.binding = event;
	recorded.binding.commandList = commandList;
	recorded.binding.captureSessionId = 0;
	recorded.binding.serial = 0;
	DX12BindingTracker::RetainEventIaResources(&recorded.binding);
	const std::string kind = recorded.binding.kind;

	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	std::vector<CommandListRecordedEvent> &events = shard.recordedEvents[commandList];
	events.push_back(std::move(recorded));
	DX12ResidentMaintenanceTelemetryUtils::RecordBindingEventRecorded();
	const size_t templateCount = events.size();
	ReleaseSRWLockExclusive(&shard.lock);

	DX12FrameAnalysis::LogJsonFunc("BindingRecordTemplateEvent",
		"\"commandList\":\"%p\",\"kind\":\"%s\",\"templates\":%zu",
		commandList, kind.c_str(), templateCount);
}

void DX12BindingTracker::RecordTemplateOutputEvent(
	ID3D12GraphicsCommandList *commandList, const OutputBindingEvent &event)
{
	if (!commandList)
		return;
	CommandListRecordedEvent recorded;
	recorded.kind = CommandListRecordedEventKind::Output;
	recorded.output = event;
	recorded.output.commandList = commandList;
	recorded.output.captureSessionId = 0;
	recorded.output.serial = 0;
	const std::string kind = recorded.output.kind;

	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	std::vector<CommandListRecordedEvent> &events = shard.recordedEvents[commandList];
	events.push_back(std::move(recorded));
	DX12ResidentMaintenanceTelemetryUtils::RecordBindingEventRecorded();
	const size_t templateCount = events.size();
	ReleaseSRWLockExclusive(&shard.lock);

	DX12FrameAnalysis::LogJsonFunc("BindingRecordTemplateOutputEvent",
		"\"commandList\":\"%p\",\"kind\":\"%s\",\"templates\":%zu",
		commandList, kind.c_str(), templateCount);
}

void DX12BindingTracker::StoreEventInCaptureLedger(const BindingEvent &event)
{
	BindingEvent stored = event;
	DX12BindingTracker::RetainEventIaResources(&stored);
	stored.serial = 0;
	const DX12CaptureLedgerRecordResult ledgerResult =
		DX12CaptureLedger::RecordEvent(&stored);
	if (ledgerResult == DX12CaptureLedgerRecordResult::Stored)
		return;
	DX12BindingTracker::ReleaseEventIaResources(&stored);
	DX12FrameAnalysis::LogJsonFunc("BindingLedgerEventSkipped",
		"\"commandList\":\"%p\",\"kind\":\"%s\",\"captureSessionId\":%llu,\"result\":%u",
		stored.commandList,
		stored.kind.c_str(),
		static_cast<unsigned long long>(stored.captureSessionId),
		static_cast<UINT>(ledgerResult));
}

void DX12BindingTracker::StoreOutputEventInCaptureLedger(const OutputBindingEvent &event)
{
	OutputBindingEvent stored = event;
	stored.serial = 0;
	const DX12CaptureLedgerRecordResult ledgerResult =
		DX12CaptureLedger::TryStoreOutputEvent(&stored);
	if (ledgerResult == DX12CaptureLedgerRecordResult::Stored ||
		ledgerResult == DX12CaptureLedgerRecordResult::Duplicate)
		return;
	DX12FrameAnalysis::LogJsonFunc("BindingLedgerOutputSkipped",
		"\"commandList\":\"%p\",\"kind\":\"%s\",\"captureSessionId\":%llu,\"result\":%u",
		stored.commandList,
		stored.kind.c_str(),
		static_cast<unsigned long long>(stored.captureSessionId),
		static_cast<UINT>(ledgerResult));
}

void DX12BindingTracker::SubmitRecordedEvent(
	const CommandListRecordedEvent &recorded, UINT64 captureSessionId)
{
	if (captureSessionId == 0)
		return;
	if (recorded.kind == CommandListRecordedEventKind::Binding) {
		BindingEvent event = recorded.binding;
		event.captureSessionId = captureSessionId;
		event.serial = 0;
		DX12BindingTracker::StoreEventInCaptureLedger(event);
		return;
	}
	OutputBindingEvent event = recorded.output;
	event.captureSessionId = captureSessionId;
	event.serial = 0;
	DX12BindingTracker::StoreOutputEventInCaptureLedger(event);
}

void DX12BindingTracker::SubmitCommandList(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return;
	const UINT64 captureSessionId = DX12BindingTracker::CurrentCaptureSessionId();
	if (captureSessionId == 0)
		return;
	const UINT64 targetSequence = DX12BindingCollectQueue::LastSubmittedSequence();
	if (targetSequence != 0) {
		DX12BindingCollectQueue::WaitForSubmittedSequence(
			targetSequence,
			"SubmitCommandList");
	}

	std::vector<CommandListRecordedEvent> events;
	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockShared(&shard.lock);
	auto it = shard.recordedEvents.find(commandList);
	if (it != shard.recordedEvents.end()) {
		events = it->second;
		for (CommandListRecordedEvent &event : events) {
			if (event.kind == CommandListRecordedEventKind::Binding)
				DX12BindingTracker::RetainEventIaResources(&event.binding);
		}
	}
	ReleaseSRWLockShared(&shard.lock);

	for (const CommandListRecordedEvent &event : events)
		DX12BindingTracker::SubmitRecordedEvent(event, captureSessionId);
	for (CommandListRecordedEvent &event : events)
		DX12BindingTracker::ReleaseRecordedEvent(&event);

	DX12FrameAnalysis::LogJsonFunc("BindingSubmitCommandList",
		"\"commandList\":\"%p\",\"captureSessionId\":%llu,\"templates\":%zu",
		commandList,
		static_cast<unsigned long long>(captureSessionId),
		events.size());
}

bool DX12BindingTracker::RegisterCommandList(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return false;

	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	const bool inserted = shard.states.try_emplace(commandList).second;
	if (inserted) {
		// Bump generation to invalidate any TLS-cached state pointers
		// that may be invalidated by unordered_map rehash.
		shard.generation++;
	}
	ReleaseSRWLockExclusive(&shard.lock);
	return inserted;
}

bool DX12BindingTracker::UnregisterCommandList(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return false;

	bool removedState = false;
	bool removedEvents = false;
	size_t recordedEventCount = 0;
	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	auto eventIt = shard.recordedEvents.find(commandList);
	if (eventIt != shard.recordedEvents.end()) {
		recordedEventCount = eventIt->second.size();
		removedEvents = true;
	}
	DX12BindingTracker::ReleaseRecordedEventsLocked(&shard, commandList);

	auto stateIt = shard.states.find(commandList);
	if (stateIt != shard.states.end()) {
		DX12BindingTracker::ReleaseRetainedIaResources(&stateIt->second);
		shard.states.erase(stateIt);
		removedState = true;
	}
	if (removedState || removedEvents)
		shard.generation++;
	ReleaseSRWLockExclusive(&shard.lock);

	if (tlsGraphicsTableCache.cl == commandList)
		tlsGraphicsTableCache.cl = nullptr;
	if (tlsComputeTableCache.cl == commandList)
		tlsComputeTableCache.cl = nullptr;
	if (tlsGraphicsDescCache.cl == commandList)
		tlsGraphicsDescCache.cl = nullptr;
	if (tlsComputeDescCache.cl == commandList)
		tlsComputeDescCache.cl = nullptr;
	for (BindingTlsDescriptorQueryCache &queryCache : tlsDescriptorQueryCaches) {
		if (queryCache.commandList == commandList)
			queryCache.commandList = nullptr;
	}
	if (tlsComputeAllQueryCache.commandList == commandList)
		tlsComputeAllQueryCache.commandList = nullptr;
	if (tlsStateCache.commandList == commandList) {
		tlsStateCache.commandList = nullptr;
		tlsStateCache.state = nullptr;
	}

	if (removedState || removedEvents) {
		DX12LogManager::Fields("DX12BindingTracker",
			"stage=unregister_command_list,commandList=%p,state=%u,recordedEvents=%zu",
			commandList,
			removedState ? 1u : 0u,
			recordedEventCount);
	}
	return removedState || removedEvents;
}

void DX12BindingTracker::ResetCommandList(
	ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *initialState)
{
	if (!commandList)
		return;

	const DX12CommandListTrackingState tracking =
		DX12CommandListStateCapture::GetTrackingState(commandList);
	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	CommandListBindingState &state = shard.states[commandList];
	DX12BindingTracker::ReleaseRecordedEventsLocked(&shard, commandList);
	DX12BindingTracker::ReleaseRetainedIaResources(&state);
	state = CommandListBindingState();
	state.pipelineState = initialState;
	if (tracking.recordBindingEvents)
		DX12BindingTracker::RefreshPipelineShaderInfo(&state);
	ReleaseSRWLockExclusive(&shard.lock);

	tlsGraphicsTableCache.cl = nullptr;
	tlsComputeTableCache.cl = nullptr;
	tlsGraphicsDescCache.cl = nullptr;
	tlsComputeDescCache.cl = nullptr;
	for (BindingTlsDescriptorQueryCache &queryCache : tlsDescriptorQueryCaches)
		queryCache.commandList = nullptr;
	tlsComputeAllQueryCache.commandList = nullptr;
	tlsStateCache.commandList = nullptr;
	tlsStateCache.state = nullptr;
}

bool DX12BindingTracker::SetPipelineState(
	ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState)
{
	if (!commandList)
		return false;

	DX12ResidentMaintenanceTelemetryUtils::RecordPsoCall();
	// Lock-free fast path: the TLS-cached binding state already carries this
	// PSO. The shard lock + map re-validation below would otherwise run on
	// every redundant SetPipelineState (games set the same PSO thousands of
	// times per frame); the TLS cache is invalidated on shard generation
	// change, so a warm match is exact for this thread and command list.
	if (tlsStateCache.commandList == commandList && tlsStateCache.state) {
		BindingShard &cachedShard = DX12BindingTrackerState::Shards[tlsStateCache.shardIndex];
		if (tlsStateCache.shardGeneration == cachedShard.generation &&
			tlsStateCache.state->pipelineState == pipelineState) {
			DX12ResidentMaintenanceTelemetryUtils::RecordPsoNoChange();
			return false;
		}
	}
	CommandListBindingState *statePtr = GetOrResolveBindingState(commandList);
	BindingShard &shard = DX12BindingTrackerState::Shards[tlsStateCache.shardIndex];
	AcquireSRWLockExclusive(&shard.lock);
	if (tlsStateCache.shardGeneration != shard.generation) {
		statePtr = &shard.states[commandList];
		tlsStateCache.state = statePtr;
		tlsStateCache.shardGeneration = shard.generation;
	}
	CommandListBindingState &state = *statePtr;
	if (state.pipelineState == pipelineState) {
		DX12ResidentMaintenanceTelemetryUtils::RecordPsoNoChange();
		ReleaseSRWLockExclusive(&shard.lock);
		return false;
	}
	state.pipelineState = pipelineState;
	DX12BindingTracker::InvalidatePipelineShaderInfo(&state);
	DX12BindingTracker::BumpBindingQuerySerial(&state);
	DX12ResidentMaintenanceTelemetryUtils::RecordPsoChanged();
	ReleaseSRWLockExclusive(&shard.lock);
	return true;
}

bool DX12BindingTracker::SetGraphicsRootSignature(
	ID3D12GraphicsCommandList *commandList, ID3D12RootSignature *rootSignature)
{
	if (!commandList)
		return false;

	// Lock-free fast path, same TLS validation as GetOrResolveBindingState.
	// SetGraphicsRootSignature is called once per SetGraphicsRootSignature
	// API call; titles that re-set the same signature before every draw were
	// paying a full shard exclusive lock + map lookup per call even though the
	// state never changed.
	if (tlsStateCache.commandList == commandList && tlsStateCache.state) {
		BindingShard &cachedShard = DX12BindingTrackerState::Shards[tlsStateCache.shardIndex];
		if (tlsStateCache.shardGeneration == cachedShard.generation &&
			tlsStateCache.state->graphicsRootSignature == rootSignature) {
			DX12ResidentMaintenanceTelemetryUtils::RecordGraphicsRootSignature(false);
			return false;
		}
	}
	CommandListBindingState *statePtr = GetOrResolveBindingState(commandList);
	BindingShard &shard = DX12BindingTrackerState::Shards[tlsStateCache.shardIndex];
	AcquireSRWLockExclusive(&shard.lock);
	if (tlsStateCache.shardGeneration != shard.generation) {
		statePtr = &shard.states[commandList];
		tlsStateCache.state = statePtr;
		tlsStateCache.shardGeneration = shard.generation;
	}
	CommandListBindingState &state = *statePtr;
	if (state.graphicsRootSignature == rootSignature) {
		DX12ResidentMaintenanceTelemetryUtils::RecordGraphicsRootSignature(false);
		ReleaseSRWLockExclusive(&shard.lock);
		return false;
	}
	state.graphicsRootSignature = rootSignature;
	// D3D12 root arguments become stale when the root signature changes. Keep
	// Binding State subject to the same rule so a table from the previous root
	// signature can never be matched or replayed against the new layout.
	for (UINT root = 0; root < MaxRootParameters; ++root) {
		state.graphicsTables[root] = RootTableState();
		state.graphicsRootDescriptors[root] = RootDescriptorState();
		state.graphicsRootConstants[root] = RootConstantsState();
	}
	state.graphicsTableValidCount = 0;
	DX12BindingTracker::BumpBindingQuerySerial(&state);
	DX12ResidentMaintenanceTelemetryUtils::RecordGraphicsRootSignature(true);
	ReleaseSRWLockExclusive(&shard.lock);
	tlsGraphicsTableCache.cl = nullptr;
	tlsGraphicsDescCache.cl = nullptr;
	return true;
}

bool DX12BindingTracker::SetComputeRootSignature(
	ID3D12GraphicsCommandList *commandList, ID3D12RootSignature *rootSignature)
{
	if (!commandList)
		return false;

	// Lock-free fast path, mirrors SetGraphicsRootSignature.
	if (tlsStateCache.commandList == commandList && tlsStateCache.state) {
		BindingShard &cachedShard = DX12BindingTrackerState::Shards[tlsStateCache.shardIndex];
		if (tlsStateCache.shardGeneration == cachedShard.generation &&
			tlsStateCache.state->computeRootSignature == rootSignature) {
			DX12ResidentMaintenanceTelemetryUtils::RecordComputeRootSignature(false);
			return false;
		}
	}
	CommandListBindingState *statePtr = GetOrResolveBindingState(commandList);
	BindingShard &shard = DX12BindingTrackerState::Shards[tlsStateCache.shardIndex];
	AcquireSRWLockExclusive(&shard.lock);
	if (tlsStateCache.shardGeneration != shard.generation) {
		statePtr = &shard.states[commandList];
		tlsStateCache.state = statePtr;
		tlsStateCache.shardGeneration = shard.generation;
	}
	CommandListBindingState &state = *statePtr;
	if (state.computeRootSignature == rootSignature) {
		DX12ResidentMaintenanceTelemetryUtils::RecordComputeRootSignature(false);
		ReleaseSRWLockExclusive(&shard.lock);
		return false;
	}
	state.computeRootSignature = rootSignature;
	for (UINT root = 0; root < MaxRootParameters; ++root) {
		state.computeTables[root] = RootTableState();
		state.computeRootDescriptors[root] = RootDescriptorState();
		state.computeRootConstants[root] = RootConstantsState();
	}
	state.computeTableValidCount = 0;
	++state.computeBindingSerial;
	DX12BindingTracker::BumpBindingQuerySerial(&state);
	DX12ResidentMaintenanceTelemetryUtils::RecordComputeRootSignature(true);
	ReleaseSRWLockExclusive(&shard.lock);
	tlsComputeTableCache.cl = nullptr;
	tlsComputeDescCache.cl = nullptr;
	return true;
}

void DX12BindingTracker::RecordStateEvent(ID3D12GraphicsCommandList *commandList, const char *kind)
{
	if (!commandList)
		return;

	CommandListBindingState snapshot;
	if (!DX12BindingTracker::SnapshotBindingState(commandList, &snapshot))
		return;
	const UINT64 descriptorEpoch = CurrentDescriptorEpoch();
	const UINT64 captureSessionId = DX12BindingTracker::CurrentCaptureSessionId();
	if (DX12BindingCollectQueue::EnqueueStateEvent(
			commandList,
			snapshot,
			kind ? kind : "state",
			descriptorEpoch,
			captureSessionId))
		return;

	BindingEvent event;
	if (DX12BindingTracker::BuildEvent(
			&event,
			kind ? kind : "state",
			commandList,
			snapshot,
			descriptorEpoch,
			captureSessionId))
		DX12BindingTracker::StoreEventLocked(event);
}

bool DX12BindingTracker::SetDescriptorHeaps(
	ID3D12GraphicsCommandList *commandList, UINT count,
	ID3D12DescriptorHeap *const *heaps)
{
	if (!commandList)
		return false;

	DX12ResidentMaintenanceTelemetryUtils::RecordDescriptorHeapResolve(count);
	ID3D12DescriptorHeap *cbvSrvUavHeap = nullptr;
	ID3D12DescriptorHeap *samplerHeap = nullptr;
	for (UINT i = 0; i < count && heaps; ++i) {
		ID3D12DescriptorHeap *heap = heaps[i];
		if (!heap)
			continue;
		D3D12_DESCRIPTOR_HEAP_DESC desc = heap->GetDesc();
		if (desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
			cbvSrvUavHeap = heap;
		else if (desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
			samplerHeap = heap;
	}

	return DX12BindingTracker::SetDescriptorHeaps(commandList, cbvSrvUavHeap, samplerHeap);
}

bool DX12BindingTracker::SetDescriptorHeaps(
	ID3D12GraphicsCommandList *commandList,
	ID3D12DescriptorHeap *cbvSrvUavHeap,
	ID3D12DescriptorHeap *samplerHeap)
{
	if (!commandList)
		return false;

	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	CommandListBindingState &state = shard.states[commandList];
	if (state.cbvSrvUavHeap == cbvSrvUavHeap &&
	    state.samplerHeap == samplerHeap) {
		DX12ResidentMaintenanceTelemetryUtils::RecordDescriptorHeapsNoChange();
		ReleaseSRWLockExclusive(&shard.lock);
		return false;
	}
	UINT invalidatedGraphicsTables = state.graphicsTableValidCount;
	UINT invalidatedComputeTables = state.computeTableValidCount;
	if (state.graphicsTableValidCount || state.computeTableValidCount) {
		for (UINT root = 0; root < MaxRootParameters; ++root) {
			if (state.graphicsTables[root].valid)
				state.graphicsTables[root] = RootTableState();
			if (state.computeTables[root].valid)
				state.computeTables[root] = RootTableState();
		}
		state.graphicsTableValidCount = 0;
		state.computeTableValidCount = 0;
	}
	state.cbvSrvUavHeap = cbvSrvUavHeap;
	state.samplerHeap = samplerHeap;
	// Keep the hot path to pointer swaps only. Heap summaries are resolved
	// lazily by the query path when a capture or replacement actually needs to
	// inspect descriptor contents.
	DX12BindingTrackerQueryCacheUtils::StoreRuntimeHeapSummaries(
		&state,
		nullptr,
		false,
		nullptr,
		false);
	++state.computeBindingSerial;
	DX12BindingTracker::BumpBindingQuerySerial(&state);
	ReleaseSRWLockExclusive(&shard.lock);
	tlsGraphicsTableCache.cl = nullptr;
	tlsComputeTableCache.cl = nullptr;
	DX12ResidentMaintenanceTelemetryUtils::RecordDescriptorHeapsChanged(
		invalidatedGraphicsTables, invalidatedComputeTables);
	DX12LogManager::DebugFields("DX12BindingDescriptorHeapsChanged",
		"commandList=%p,cbvSrvUavHeap=%p,samplerHeap=%p,invalidatedGraphicsTables=%u,invalidatedComputeTables=%u",
		commandList,
		cbvSrvUavHeap,
		samplerHeap,
		invalidatedGraphicsTables,
		invalidatedComputeTables);
	return true;
}

bool DX12BindingTracker::SetGraphicsRootDescriptorTable(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor)
{
	if (!commandList || rootParameterIndex >= MaxRootParameters)
		return false;
	DX12ResidentMaintenanceTelemetryUtils::RecordGraphicsRootTableCall();
	if (tlsGraphicsTableCache.cl == commandList &&
		tlsGraphicsTableCache.valid[rootParameterIndex] &&
		tlsGraphicsTableCache.gpuHandle[rootParameterIndex] == baseDescriptor.ptr)
	{
		DX12ResidentMaintenanceTelemetryUtils::RecordGraphicsRootTableTlsHit();
		return false;
	}

	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	CommandListBindingState &state = shard.states[commandList];
	RootTableState &stateTable = state.graphicsTables[rootParameterIndex];
	if (stateTable.valid && stateTable.baseDescriptor.ptr == baseDescriptor.ptr) {
		ReleaseSRWLockExclusive(&shard.lock);
		if (tlsGraphicsTableCache.cl != commandList) {
			memset(tlsGraphicsTableCache.valid, 0, sizeof(tlsGraphicsTableCache.valid));
			memset(tlsGraphicsTableCache.gpuHandle, 0, sizeof(tlsGraphicsTableCache.gpuHandle));
			tlsGraphicsTableCache.cl = commandList;
		}
		tlsGraphicsTableCache.valid[rootParameterIndex] = true;
		tlsGraphicsTableCache.gpuHandle[rootParameterIndex] = baseDescriptor.ptr;
		DX12ResidentMaintenanceTelemetryUtils::RecordGraphicsRootTableNoChange();
		return false;
	}
	if (!stateTable.valid)
		++state.graphicsTableValidCount;
	stateTable = RootTableState();
	stateTable.valid = true;
	stateTable.rootParameterIndex = rootParameterIndex;
	stateTable.baseDescriptor = baseDescriptor;
	DX12BindingTracker::BumpBindingQuerySerial(&state);
	ReleaseSRWLockExclusive(&shard.lock);
	if (tlsGraphicsTableCache.cl != commandList) {
		memset(tlsGraphicsTableCache.valid, 0, sizeof(tlsGraphicsTableCache.valid));
		memset(tlsGraphicsTableCache.gpuHandle, 0, sizeof(tlsGraphicsTableCache.gpuHandle));
		tlsGraphicsTableCache.cl = commandList;
	}
	tlsGraphicsTableCache.valid[rootParameterIndex] = true;
	tlsGraphicsTableCache.gpuHandle[rootParameterIndex] = baseDescriptor.ptr;
	DX12ResidentMaintenanceTelemetryUtils::RecordGraphicsRootTableChanged();
	return true;
}

bool DX12BindingTracker::SetComputeRootDescriptorTable(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor)
{
	if (!commandList || rootParameterIndex >= MaxRootParameters)
		return false;
	DX12ResidentMaintenanceTelemetryUtils::RecordComputeRootTableCall();
	if (tlsComputeTableCache.cl == commandList &&
		tlsComputeTableCache.valid[rootParameterIndex] &&
		tlsComputeTableCache.gpuHandle[rootParameterIndex] == baseDescriptor.ptr)
	{
		DX12ResidentMaintenanceTelemetryUtils::RecordComputeRootTableTlsHit();
		return false;
	}

	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	CommandListBindingState &state = shard.states[commandList];
	RootTableState &table = state.computeTables[rootParameterIndex];
	if (table.valid && table.baseDescriptor.ptr == baseDescriptor.ptr) {
		ReleaseSRWLockExclusive(&shard.lock);
		if (tlsComputeTableCache.cl != commandList) {
			memset(tlsComputeTableCache.valid, 0, sizeof(tlsComputeTableCache.valid));
			memset(tlsComputeTableCache.gpuHandle, 0, sizeof(tlsComputeTableCache.gpuHandle));
			tlsComputeTableCache.cl = commandList;
		}
		tlsComputeTableCache.valid[rootParameterIndex] = true;
		tlsComputeTableCache.gpuHandle[rootParameterIndex] = baseDescriptor.ptr;
		DX12ResidentMaintenanceTelemetryUtils::RecordComputeRootTableNoChange();
		return false;
	}
	if (!table.valid)
		++state.computeTableValidCount;
	table = RootTableState();
	table.valid = true;
	table.rootParameterIndex = rootParameterIndex;
	table.baseDescriptor = baseDescriptor;
	++state.computeBindingSerial;
	DX12BindingTracker::BumpBindingQuerySerial(&state);
	ReleaseSRWLockExclusive(&shard.lock);
	if (tlsComputeTableCache.cl != commandList) {
		memset(tlsComputeTableCache.valid, 0, sizeof(tlsComputeTableCache.valid));
		memset(tlsComputeTableCache.gpuHandle, 0, sizeof(tlsComputeTableCache.gpuHandle));
		tlsComputeTableCache.cl = commandList;
	}
	tlsComputeTableCache.valid[rootParameterIndex] = true;
	tlsComputeTableCache.gpuHandle[rootParameterIndex] = baseDescriptor.ptr;
	DX12ResidentMaintenanceTelemetryUtils::RecordComputeRootTableChanged();
	return true;
}

void DX12BindingTracker::SetPrimitiveTopology(
	ID3D12GraphicsCommandList *commandList, D3D12_PRIMITIVE_TOPOLOGY topology)
{
	if (!commandList)
		return;

	CommandListBindingState *statePtr = GetOrResolveBindingState(commandList);
	BindingShard &shard = DX12BindingTrackerState::Shards[tlsStateCache.shardIndex];
	AcquireSRWLockExclusive(&shard.lock);
	if (tlsStateCache.shardGeneration != shard.generation) {
		statePtr = &shard.states[commandList];
		tlsStateCache.state = statePtr;
		tlsStateCache.shardGeneration = shard.generation;
	}
	CommandListBindingState &state = *statePtr;
	if (state.primitiveTopology == topology) {
		DX12ResidentMaintenanceTelemetryUtils::RecordPrimitiveTopology(false);
		ReleaseSRWLockExclusive(&shard.lock);
		return;
	}
	state.primitiveTopology = topology;
	DX12BindingTracker::BumpBindingQuerySerial(&state);
	DX12ResidentMaintenanceTelemetryUtils::RecordPrimitiveTopology(true);
	ReleaseSRWLockExclusive(&shard.lock);
}

void DX12BindingTracker::SetIndexBuffer(
	ID3D12GraphicsCommandList *commandList, const D3D12_INDEX_BUFFER_VIEW *view)
{
	if (!commandList)
		return;

	// Use TLS-cached state pointer. Resource resolution deferred to draw time.
	CommandListBindingState *statePtr = GetOrResolveBindingState(commandList);
	BindingShard &shard = DX12BindingTrackerState::Shards[tlsStateCache.shardIndex];
	AcquireSRWLockExclusive(&shard.lock);
	if (tlsStateCache.shardGeneration != shard.generation) {
		statePtr = &shard.states[commandList];
		tlsStateCache.state = statePtr;
		tlsStateCache.shardGeneration = shard.generation;
	}
	CommandListBindingState &state = *statePtr;

	const bool newValid = view != nullptr;
	const bool same =
		state.indexBufferValid == newValid &&
		(!newValid ||
			(state.indexBuffer.BufferLocation == view->BufferLocation &&
				state.indexBuffer.SizeInBytes == view->SizeInBytes &&
				state.indexBuffer.Format == view->Format));
	if (same) {
		DX12ResidentMaintenanceTelemetryUtils::RecordIndexBuffer(false);
		ReleaseSRWLockExclusive(&shard.lock);
		return;
	}

	const bool hadResolved = state.indexBufferResolved;
	DX12BindingTracker::ReleaseResolvedIaResource(
		&state.indexBufferResource, &state.indexBufferResolved);
	if (hadResolved)
		DX12ResidentMaintenanceTelemetryUtils::RecordIndexBufferResolvedReleased();
	if (view) {
		state.indexBuffer = *view;
		state.indexBufferValid = true;
	} else {
		state.indexBuffer = {};
		state.indexBufferValid = false;
	}
	// Resource resolution DEFERRED to draw time; see SetVertexBuffers comment.
	DX12BindingTracker::BumpBindingQuerySerial(&state);
	DX12ResidentMaintenanceTelemetryUtils::RecordIndexBuffer(true);
	ReleaseSRWLockExclusive(&shard.lock);
}

void DX12BindingTracker::SetVertexBuffers(
	ID3D12GraphicsCommandList *commandList, UINT startSlot, UINT count,
	const D3D12_VERTEX_BUFFER_VIEW *views)
{
	if (!commandList || startSlot >= MaxVertexBufferSlots)
		return;

	const UINT trackedCount = min(count, MaxVertexBufferSlots - startSlot);
	DX12ResidentMaintenanceTelemetryUtils::RecordVertexBufferCall(trackedCount);

	// Use TLS-cached state pointer to avoid ShardFor() hash + HashMap lookup.
	// Still acquire lock for mutation safety (negligible cost when uncontended,
	// ~20ns vs ~11us of the old eager resource resolution per slot).
	CommandListBindingState *statePtr = GetOrResolveBindingState(commandList);
	BindingShard &shard = DX12BindingTrackerState::Shards[tlsStateCache.shardIndex];
	AcquireSRWLockExclusive(&shard.lock);
	// Re-validate after lock: unordered_map may have rehashed while we were
	// waiting on the lock (extremely rare, but generation guard handles it).
	if (tlsStateCache.shardGeneration != shard.generation) {
		statePtr = &shard.states[commandList];
		tlsStateCache.state = statePtr;
		tlsStateCache.shardGeneration = shard.generation;
	}
	CommandListBindingState &state = *statePtr;

	bool anyChanged = false;
	for (UINT i = 0; i < trackedCount; ++i) {
		const UINT slot = startSlot + i;
		D3D12_VERTEX_BUFFER_VIEW newView = {};
		bool newValid = false;
		if (views) {
			newView = views[i];
			newValid = newView.BufferLocation != 0 && newView.SizeInBytes != 0;
		}
		const bool same =
			state.vertexBufferValid[slot] == newValid &&
			state.vertexBuffers[slot].BufferLocation == newView.BufferLocation &&
			state.vertexBuffers[slot].SizeInBytes == newView.SizeInBytes &&
			state.vertexBuffers[slot].StrideInBytes == newView.StrideInBytes;
		if (same) {
			DX12ResidentMaintenanceTelemetryUtils::RecordVertexBufferSlot(false);
			continue;
		}

		const bool hadResolved = state.vertexBufferResolved[slot];
		DX12BindingTracker::ReleaseResolvedIaResource(
			&state.vertexBufferResources[slot],
			&state.vertexBufferResolved[slot]);
		if (hadResolved)
			DX12ResidentMaintenanceTelemetryUtils::RecordVertexBufferResolvedReleased();
		state.vertexBuffers[slot] = newView;
		state.vertexBufferValid[slot] = newValid;
		DX12ResidentMaintenanceTelemetryUtils::RecordVertexBufferSlot(true);
		anyChanged = true;
		// Resource resolution DEFERRED to draw time (RecordDrawInstanced /
		// RecordDrawIndexedInstanced). This is a ~90% cost reduction per call:
		//   Old: ReleaseResolvedIaResource + ResolveAndRetainIaResource
		//        -> DX12ReleaseRetainedResource + DX12ResolveBufferResourceByGpuVa + DX12RetainResource
		//   New: trivial struct copy (D3D12_VERTEX_BUFFER_VIEW = 3x UINT64)
	}
	if (anyChanged)
		DX12BindingTracker::BumpBindingQuerySerial(&state);
	ReleaseSRWLockExclusive(&shard.lock);
}

void DX12BindingTracker::RecordOutputTargets(
	ID3D12GraphicsCommandList *commandList, UINT numRenderTargetDescriptors,
	const D3D12_CPU_DESCRIPTOR_HANDLE *renderTargetDescriptors,
	BOOL singleHandleToDescriptorRange, const D3D12_CPU_DESCRIPTOR_HANDLE *depthStencilDescriptor)
{
	if (!commandList)
		return;

	CommandListBindingState snapshot;
	bool found = false;
	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockShared(&shard.lock);
	auto it = shard.states.find(commandList);
	if (it != shard.states.end()) {
		snapshot = it->second;
		found = true;
	}
	ReleaseSRWLockShared(&shard.lock);
	if (!found)
		snapshot = CommandListBindingState();
	const UINT64 captureSessionId = DX12BindingTracker::CurrentCaptureSessionId();
	for (UINT i = 0; i < numRenderTargetDescriptors && renderTargetDescriptors; ++i) {
		D3D12_CPU_DESCRIPTOR_HANDLE handle = renderTargetDescriptors[i];
		if (singleHandleToDescriptorRange && i > 0) {
			DX12DescriptorHeapSummary heap = {};
			if (!DX12FindDescriptorHeapByCpuHandle(
					renderTargetDescriptors[0], D3D12_DESCRIPTOR_HEAP_TYPE_RTV, &heap) ||
				!heap.increment)
				continue;
			handle.ptr = renderTargetDescriptors[0].ptr +
				static_cast<SIZE_T>(i) * heap.increment;
		}
		if (DX12BindingCollectQueue::EnqueueOutputEvent(
				commandList,
				snapshot,
				"RTV",
				i,
				handle.ptr,
				0,
				captureSessionId))
			continue;
		DX12BindingTracker::StoreOutputEventLocked(
			"RTV",
			commandList,
			snapshot,
			i,
			handle.ptr,
			0,
			captureSessionId);
	}
	if (depthStencilDescriptor &&
		!DX12BindingCollectQueue::EnqueueOutputEvent(commandList, snapshot, "DSV", 0,
			depthStencilDescriptor->ptr, 0, captureSessionId)) {
		DX12BindingTracker::StoreOutputEventLocked("DSV", commandList, snapshot, 0,
			depthStencilDescriptor->ptr, 0, captureSessionId);
	}

	AcquireSRWLockExclusive(&shard.lock);
	CommandListBindingState &state = shard.states[commandList];
	for (UINT i = 0; i < MaxRenderTargetSlots; ++i) {
		state.renderTargets[i] = {};
		state.renderTargetValid[i] = false;
	}
	for (UINT i = 0; i < numRenderTargetDescriptors && i < MaxRenderTargetSlots; ++i) {
		D3D12_CPU_DESCRIPTOR_HANDLE handle = {};
		if (renderTargetDescriptors) {
			handle = renderTargetDescriptors[i];
			if (singleHandleToDescriptorRange && i > 0) {
				DX12DescriptorHeapSummary heap = {};
				if (DX12FindDescriptorHeapByCpuHandle(
						renderTargetDescriptors[0], D3D12_DESCRIPTOR_HEAP_TYPE_RTV, &heap) &&
					heap.increment) {
					handle.ptr = renderTargetDescriptors[0].ptr +
						static_cast<SIZE_T>(i) * heap.increment;
				}
			}
		}
		state.renderTargets[i] = handle;
		state.renderTargetValid[i] = handle.ptr != 0;
	}
	if (depthStencilDescriptor) {
		state.depthStencil = *depthStencilDescriptor;
		state.depthStencilValid = depthStencilDescriptor->ptr != 0;
	} else {
		state.depthStencil = {};
		state.depthStencilValid = false;
	}
	ReleaseSRWLockExclusive(&shard.lock);
}

void DX12BindingTracker::RecordRenderTargetClear(
	ID3D12GraphicsCommandList *commandList, D3D12_CPU_DESCRIPTOR_HANDLE renderTargetView)
{
	if (!commandList || !renderTargetView.ptr)
		return;

	CommandListBindingState snapshot;
	bool found = false;
	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockShared(&shard.lock);
	auto it = shard.states.find(commandList);
	if (it != shard.states.end()) {
		snapshot = it->second;
		found = true;
	}
	ReleaseSRWLockShared(&shard.lock);
	if (!found)
		return;
	const UINT64 captureSessionId = DX12BindingTracker::CurrentCaptureSessionId();
	if (DX12BindingCollectQueue::EnqueueOutputEvent(
			commandList,
			snapshot,
			"RTV",
			0,
			renderTargetView.ptr,
			0,
			captureSessionId))
		return;

	DX12BindingTracker::StoreOutputEventLocked(
		"RTV",
		commandList,
		snapshot,
		0,
		renderTargetView.ptr,
		0,
		captureSessionId);
}

void DX12BindingTracker::RecordDepthStencilClear(
	ID3D12GraphicsCommandList *commandList, D3D12_CPU_DESCRIPTOR_HANDLE depthStencilView)
{
	if (!commandList || !depthStencilView.ptr)
		return;

	CommandListBindingState snapshot;
	bool found = false;
	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockShared(&shard.lock);
	auto it = shard.states.find(commandList);
	if (it != shard.states.end()) {
		snapshot = it->second;
		found = true;
	}
	ReleaseSRWLockShared(&shard.lock);
	if (!found)
		return;
	const UINT64 captureSessionId = DX12BindingTracker::CurrentCaptureSessionId();
	if (DX12BindingCollectQueue::EnqueueOutputEvent(
			commandList,
			snapshot,
			"DSV",
			0,
			depthStencilView.ptr,
			0,
			captureSessionId))
		return;

	DX12BindingTracker::StoreOutputEventLocked(
		"DSV",
		commandList,
		snapshot,
		0,
		depthStencilView.ptr,
		0,
		captureSessionId);
}

bool DX12BindingTracker::GetCurrentIaState(
	ID3D12GraphicsCommandList *commandList, DX12CurrentIaState *state)
{
	if (!commandList || !state)
		return false;

	*state = DX12CurrentIaState();
	CommandListBindingState snapshot;
	if (!DX12BindingTracker::SnapshotBindingState(commandList, &snapshot))
		return false;

	for (UINT slot = 0; slot < MaxVertexBufferSlots; ++slot) {
		if (!snapshot.vertexBufferValid[slot])
			continue;
		const D3D12_VERTEX_BUFFER_VIEW &view = snapshot.vertexBuffers[slot];
		DX12CurrentIaBuffer buffer;
		buffer.role = "VB";
		buffer.gpuVa = view.BufferLocation;
		buffer.size = view.SizeInBytes;
		buffer.stride = view.StrideInBytes;
		buffer.slot = slot;
		buffer.resolved = snapshot.vertexBufferResolved[slot];
		buffer.resource = snapshot.vertexBufferResources[slot];
		if (!buffer.resolved)
			buffer.resolved = DX12ResolveBufferResourceByGpuVa(
				buffer.gpuVa, buffer.size, &buffer.resource);
		state->vertexBuffers.push_back(buffer);
	}

	if (snapshot.indexBufferValid && snapshot.indexBuffer.BufferLocation &&
		snapshot.indexBuffer.SizeInBytes) {
		state->hasIndexBuffer = true;
		state->indexBuffer.role = "IB";
		state->indexBuffer.gpuVa = snapshot.indexBuffer.BufferLocation;
		state->indexBuffer.size = snapshot.indexBuffer.SizeInBytes;
		state->indexBuffer.format = static_cast<UINT>(snapshot.indexBuffer.Format);
		state->indexBuffer.resolved = snapshot.indexBufferResolved;
		state->indexBuffer.resource = snapshot.indexBufferResource;
		if (!state->indexBuffer.resolved)
			state->indexBuffer.resolved = DX12ResolveBufferResourceByGpuVa(
				state->indexBuffer.gpuVa, state->indexBuffer.size, &state->indexBuffer.resource);
	}

	return true;
}


bool DX12BindingTracker::GetCurrentDescriptors(
	ID3D12GraphicsCommandList *commandList,
	bool compute,
	D3D12_DESCRIPTOR_RANGE_TYPE rangeType,
	const char *descriptorKind,
	D3D12_ROOT_PARAMETER_TYPE rootDescriptorType,
	std::vector<DX12CurrentComputeUavBinding> *uavs)
{
	if (!commandList || !uavs)
		return false;

	uavs->clear();
	DX12SimulateContext *runtimeState =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	const DX12CommandListTrackingState tracking =
		runtimeState ?
			DX12CommandListRuntime::GetTrackingState(runtimeState) :
			DX12CommandListTrackingState();
	const bool preferRuntimeSnapshot = runtimeState && !tracking.trackBindings;
	if (preferRuntimeSnapshot) {
		const UINT64 runtimeBindingQuerySerial =
			GetRuntimeDescriptorQuerySerial(runtimeState, compute);
		bool cachedFoundAny = false;
		if (TryLoadRuntimeDescriptorBindings(
				runtimeState,
				compute,
				rangeType,
				runtimeBindingQuerySerial,
				uavs,
				&cachedFoundAny)) {
			return cachedFoundAny;
		}
	}

	CommandListBindingState snapshot;
	UINT64 bindingQuerySerial = 0;
	const bool snapshotReady = preferRuntimeSnapshot ?
		BuildRuntimeBindingSnapshot(runtimeState, &snapshot) :
		DX12BindingTracker::SnapshotBindingState(
			commandList, &snapshot, &bindingQuerySerial);
	if (!snapshotReady)
		return false;
	if (preferRuntimeSnapshot)
		bindingQuerySerial = GetRuntimeDescriptorQuerySerial(runtimeState, compute);
	const ID3D12DescriptorHeap *cbvSrvUavHeap = snapshot.cbvSrvUavHeap;
	const UINT64 cbvSrvUavHeapVersion = DX12GetDescriptorHeapVersion(
		snapshot.cbvSrvUavHeap);
	const UINT cacheSlot = BindingTlsDescriptorQueryCache::CacheSlotIndex(
		compute, rangeType);
	if (cacheSlot == UINT_MAX)
		return false;
	BindingTlsDescriptorQueryCache &queryCache = tlsDescriptorQueryCaches[cacheSlot];
	if (queryCache.commandList == commandList &&
		queryCache.compute == compute &&
		queryCache.rangeType == static_cast<UINT>(rangeType) &&
		queryCache.bindingQuerySerial == bindingQuerySerial &&
		queryCache.cbvSrvUavHeap == cbvSrvUavHeap &&
		queryCache.cbvSrvUavHeapVersion == cbvSrvUavHeapVersion) {
		*uavs = queryCache.bindings;
		const UINT64 heapVersionAfterHit = DX12GetDescriptorHeapVersion(
			snapshot.cbvSrvUavHeap);
		if (heapVersionAfterHit != cbvSrvUavHeapVersion) {
			uavs->clear();
		} else {
	#if defined(_DEBUG)
			DX12LogManager::DebugFields("DX12DescriptorQueryCache",
			"\"stage\":\"hit\",\"slot\":%u,\"compute\":%s,\"rangeType\":%u,\"serial\":%llu,\"heapVersion\":%llu,\"bindings\":%zu",
			cacheSlot, compute ? "true" : "false", static_cast<UINT>(rangeType),
			static_cast<unsigned long long>(bindingQuerySerial),
			static_cast<unsigned long long>(cbvSrvUavHeapVersion), uavs->size());
	#endif
			return !uavs->empty();
		}
	}

	ID3D12DescriptorHeap *observedSlotHeap = nullptr;
	std::vector<UINT> observedSlots;
	const bool foundAny = DX12BindingTrackerQueryCacheUtils::CollectDescriptorBindings(
		snapshot,
		compute,
		rangeType,
		descriptorKind,
		rootDescriptorType,
		uavs,
		&observedSlotHeap,
		&observedSlots);
	const UINT64 cbvSrvUavHeapVersionAfter = DX12GetDescriptorHeapVersion(
		snapshot.cbvSrvUavHeap);
	if (cbvSrvUavHeapVersionAfter == cbvSrvUavHeapVersion) {
		queryCache.commandList = commandList;
		queryCache.compute = compute;
		queryCache.rangeType = static_cast<UINT>(rangeType);
		queryCache.bindingQuerySerial = bindingQuerySerial;
		queryCache.cbvSrvUavHeap = snapshot.cbvSrvUavHeap;
		queryCache.cbvSrvUavHeapVersion = cbvSrvUavHeapVersionAfter;
		queryCache.bindings = *uavs;
	} else {
		queryCache.commandList = nullptr;
	}
	if (preferRuntimeSnapshot &&
			runtimeState &&
			bindingQuerySerial != 0 &&
			cbvSrvUavHeapVersionAfter == cbvSrvUavHeapVersion) {
		std::vector<UINT64> observedSlotVersions(observedSlots.size());
		std::vector<UINT64> observedSlotValueIds(observedSlots.size());
		bool observedSlotsValid = true;
		if (!observedSlots.empty()) {
			observedSlotsValid =
				observedSlotHeap &&
				DX12GetDescriptorSlotIdentities(
					observedSlotHeap,
					observedSlots.data(),
					observedSlots.size(),
					observedSlotVersions.data(),
					observedSlotValueIds.data());
		}
		if (observedSlotsValid) {
			StoreRuntimeDescriptorBindings(
				runtimeState,
				compute,
				rangeType,
				bindingQuerySerial,
				foundAny,
				observedSlots,
				observedSlotVersions,
				observedSlotValueIds,
				*uavs);
		}
	}
	#if defined(_DEBUG)
	DX12LogManager::DebugFields("DX12DescriptorQueryCache",
		"\"stage\":\"miss\",\"slot\":%u,\"compute\":%s,\"rangeType\":%u,\"serial\":%llu,\"heapVersion\":%llu,\"bindings\":%zu",
		cacheSlot, compute ? "true" : "false", static_cast<UINT>(rangeType),
		static_cast<unsigned long long>(bindingQuerySerial),
		static_cast<unsigned long long>(cbvSrvUavHeapVersionAfter), uavs->size());
	#endif
	return foundAny;
}

bool DX12BindingTracker::GetCurrentComputeUavs(
	ID3D12GraphicsCommandList *commandList,
	std::vector<DX12CurrentComputeUavBinding> *uavs)
{
	return DX12BindingTracker::GetCurrentDescriptors(
		commandList, true, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, "UAV",
		D3D12_ROOT_PARAMETER_TYPE_UAV, uavs);
}

bool DX12BindingTracker::GetCurrentComputeSrvs(
	ID3D12GraphicsCommandList *commandList,
	std::vector<DX12CurrentComputeUavBinding> *srvs)
{
	return DX12BindingTracker::GetCurrentDescriptors(
		commandList, true, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, "SRV",
		D3D12_ROOT_PARAMETER_TYPE_SRV, srvs);
}

bool DX12BindingTracker::GetCurrentComputeCbvs(
	ID3D12GraphicsCommandList *commandList,
	std::vector<DX12CurrentComputeUavBinding> *cbvs)
{
	return DX12BindingTracker::GetCurrentDescriptors(
		commandList, true, D3D12_DESCRIPTOR_RANGE_TYPE_CBV, "CBV",
		D3D12_ROOT_PARAMETER_TYPE_CBV, cbvs);
}

bool DX12BindingTracker::GetCurrentComputeAll(
	ID3D12GraphicsCommandList *commandList,
	std::vector<DX12CurrentComputeUavBinding> *uavs,
	std::vector<DX12CurrentComputeUavBinding> *srvs,
	std::vector<DX12CurrentComputeUavBinding> *cbvs,
	std::vector<DX12CurrentRootConstants> *constants)
{
	if (!commandList)
		return false;
	if (uavs) uavs->clear();
	if (srvs) srvs->clear();
	if (cbvs) cbvs->clear();
	if (constants) constants->clear();

	CommandListBindingState snapshot;
	UINT64 bindingQuerySerial = 0;
	DX12_HOTPATH_TIME_BEGIN(BindSnapshot);
	if (!DX12BindingTracker::SnapshotBindingState(commandList, &snapshot, &bindingQuerySerial)) {
		DX12_HOTPATH_TIME_END(BindSnapshot);
		return false;
	}
	DX12_HOTPATH_TIME_END(BindSnapshot);
	const ID3D12DescriptorHeap *cbvSrvUavHeap = snapshot.cbvSrvUavHeap;
	const UINT64 cbvSrvUavHeapVersion = DX12GetDescriptorHeapVersion(
		snapshot.cbvSrvUavHeap);
	// Use computeTables content hash as the cache key: unchanged bindings keep the hash stable across dispatches.
	// Heap pointers can change on SetDescriptorHeaps, so they are not suitable as the key.
	UINT64 tableHash = 0;
	for (UINT r = 0; r < MaxRootParameters; ++r) {
		const RootTableState &t = snapshot.computeTables[r];
		if (!t.valid) continue;
		tableHash ^= static_cast<UINT64>(r);
		tableHash ^= t.baseDescriptor.ptr;
		tableHash ^= tableHash >> 33;
		tableHash *= 0xff51afd7ed558ccdull;
	}
	if (tlsComputeAllQueryCache.commandList == commandList &&
		tlsComputeAllQueryCache.computeTableHash == tableHash &&
		tlsComputeAllQueryCache.bindingQuerySerial == bindingQuerySerial &&
		tlsComputeAllQueryCache.cbvSrvUavHeap == cbvSrvUavHeap &&
		tlsComputeAllQueryCache.cbvSrvUavHeapVersion == cbvSrvUavHeapVersion) {
		if (uavs) *uavs = tlsComputeAllQueryCache.uavs;
		if (srvs) *srvs = tlsComputeAllQueryCache.srvs;
		if (cbvs) *cbvs = tlsComputeAllQueryCache.cbvs;
		if (constants) *constants = tlsComputeAllQueryCache.constants;
		if (DX12GetDescriptorHeapVersion(snapshot.cbvSrvUavHeap) ==
				cbvSrvUavHeapVersion) {
			return (uavs && !uavs->empty()) || (srvs && !srvs->empty()) ||
				(cbvs && !cbvs->empty()) || (constants && !constants->empty());
		}
		if (uavs) uavs->clear();
		if (srvs) srvs->clear();
		if (cbvs) cbvs->clear();
		if (constants) constants->clear();
	}

	std::vector<DX12CurrentComputeUavBinding> cachedUavs;
	std::vector<DX12CurrentComputeUavBinding> cachedSrvs;
	std::vector<DX12CurrentComputeUavBinding> cachedCbvs;
	std::vector<DX12CurrentRootConstants> cachedConstants;
	DX12_HOTPATH_TIME_BEGIN(BindCollect);
	const bool foundAny = DX12BindingTrackerQueryCacheUtils::CollectComputeAll(
		snapshot, &cachedUavs, &cachedSrvs, &cachedCbvs, &cachedConstants);
	DX12_HOTPATH_TIME_END(BindCollect);
	const UINT64 cbvSrvUavHeapVersionAfter = DX12GetDescriptorHeapVersion(
		snapshot.cbvSrvUavHeap);
	if (cbvSrvUavHeapVersionAfter == cbvSrvUavHeapVersion) {
		tlsComputeAllQueryCache.commandList = commandList;
		tlsComputeAllQueryCache.computeTableHash = tableHash;
		tlsComputeAllQueryCache.bindingQuerySerial = bindingQuerySerial;
		tlsComputeAllQueryCache.cbvSrvUavHeap = snapshot.cbvSrvUavHeap;
		tlsComputeAllQueryCache.cbvSrvUavHeapVersion = cbvSrvUavHeapVersionAfter;
		tlsComputeAllQueryCache.uavs = cachedUavs;
		tlsComputeAllQueryCache.srvs = cachedSrvs;
		tlsComputeAllQueryCache.cbvs = cachedCbvs;
		tlsComputeAllQueryCache.constants = cachedConstants;
	} else {
		tlsComputeAllQueryCache.commandList = nullptr;
	}
	if (uavs) *uavs = cachedUavs;
	if (srvs) *srvs = cachedSrvs;
	if (cbvs) *cbvs = cachedCbvs;
	if (constants) *constants = cachedConstants;
	return foundAny;
}

bool DX12BindingTracker::GetCurrentShaderResourceBindings(
	ID3D12GraphicsCommandList *commandList,
	bool compute,
	D3D12_DESCRIPTOR_RANGE_TYPE rangeType,
	std::vector<DX12CurrentShaderResourceBinding> *bindings)
{
	const char *descriptorKind = nullptr;
	D3D12_ROOT_PARAMETER_TYPE rootDescriptorType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	switch (rangeType) {
	case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
		descriptorKind = "CBV";
		rootDescriptorType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		break;
	case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
		descriptorKind = "SRV";
		rootDescriptorType = D3D12_ROOT_PARAMETER_TYPE_SRV;
		break;
	case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
		descriptorKind = "UAV";
		rootDescriptorType = D3D12_ROOT_PARAMETER_TYPE_UAV;
		break;
	default:
		if (bindings)
			bindings->clear();
		return false;
	}

	return DX12BindingTracker::GetCurrentDescriptors(
		commandList, compute, rangeType, descriptorKind,
		rootDescriptorType, bindings);
}

bool DX12BindingTracker::GetCurrentShaderResourceBindingsByRegister(
	ID3D12GraphicsCommandList *commandList,
	bool compute,
	D3D12_DESCRIPTOR_RANGE_TYPE rangeType,
	UINT shaderRegister,
	UINT registerSpace,
	std::vector<DX12CurrentShaderResourceBinding> *bindings)
{
	if (!bindings)
		return false;
	bindings->clear();

	const char *descriptorKind = nullptr;
	D3D12_ROOT_PARAMETER_TYPE rootDescriptorType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	switch (rangeType) {
	case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
		descriptorKind = "CBV";
		rootDescriptorType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		break;
	case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
		descriptorKind = "SRV";
		rootDescriptorType = D3D12_ROOT_PARAMETER_TYPE_SRV;
		break;
	case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
		descriptorKind = "UAV";
		rootDescriptorType = D3D12_ROOT_PARAMETER_TYPE_UAV;
		break;
	default:
		return false;
	}

	DX12SimulateContext *runtimeState =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	const DX12CommandListTrackingState tracking =
		runtimeState ?
			DX12CommandListRuntime::GetTrackingState(runtimeState) :
			DX12CommandListTrackingState();
	if (runtimeState && !tracking.trackBindings) {
		CommandListBindingState snapshot;
		if (!BuildRuntimeBindingSnapshot(runtimeState, &snapshot))
			return false;
		return DX12BindingTrackerQueryCacheUtils::CollectDescriptorBindingsByRegister(
			snapshot,
			compute,
			rangeType,
			shaderRegister,
			registerSpace,
			descriptorKind,
			rootDescriptorType,
			bindings);
	}

	CommandListBindingState snapshot;
	if (!DX12BindingTracker::SnapshotBindingState(commandList, &snapshot, nullptr))
		return false;

	return DX12BindingTrackerQueryCacheUtils::CollectDescriptorBindingsByRegister(
		snapshot,
		compute,
		rangeType,
		shaderRegister,
		registerSpace,
		descriptorKind,
		rootDescriptorType,
		bindings);
}

bool DX12BindingTracker::GetCurrentDescriptorHeaps(
	ID3D12GraphicsCommandList *commandList,
	ID3D12DescriptorHeap **cbvSrvUavHeap,
	ID3D12DescriptorHeap **samplerHeap)
{
	return DX12CommandListRuntime::GetDescriptorHeaps(
		commandList, cbvSrvUavHeap, samplerHeap);
}

void DX12BindingTracker::RecordDrawInstanced(
	ID3D12GraphicsCommandList *commandList, UINT vertexCountPerInstance,
	UINT instanceCount, UINT startVertexLocation, UINT startInstanceLocation)
{
	if (!commandList)
		return;

	CommandListBindingState snapshot;
	const UINT64 drawSerial = static_cast<UINT64>(InterlockedIncrement64(&DX12BindingTrackerState::GlobalDrawSerial));
	CommandListBindingState *statePtr = GetOrResolveBindingState(commandList);
	BindingShard &shard = DX12BindingTrackerState::Shards[tlsStateCache.shardIndex];
	AcquireSRWLockExclusive(&shard.lock);
	if (tlsStateCache.shardGeneration != shard.generation) {
		statePtr = &shard.states[commandList];
		tlsStateCache.state = statePtr;
		tlsStateCache.shardGeneration = shard.generation;
	}
	CommandListBindingState &state = *statePtr;
	state.drawSerial = drawSerial;
	// Resolve deferred VB/IB resources before snapshot (they were NOT resolved
	// eagerly in SetVertexBuffers/SetIndexBuffer; deferred to here for ~90%
	// per-call cost reduction).
	ResolveDeferredIaResources(&state);
	snapshot = state;
	ReleaseSRWLockExclusive(&shard.lock);
	const UINT64 descriptorEpoch = CurrentDescriptorEpoch();
	const UINT64 captureSessionId = DX12BindingTracker::CurrentCaptureSessionId();
	if (DX12BindingCollectQueue::EnqueueDrawEvent(
			commandList,
			snapshot,
			"draw",
			descriptorEpoch,
			captureSessionId,
			vertexCountPerInstance,
			0,
			instanceCount,
			startVertexLocation,
			0,
			0,
			startInstanceLocation))
		return;

	BindingEvent event;
	if (!DX12BindingTracker::BuildEvent(
			&event,
			"draw",
			commandList,
			snapshot,
			descriptorEpoch,
			captureSessionId)) {
		return;
	}
	event.vertexCountPerInstance = vertexCountPerInstance;
	event.instanceCount = instanceCount;
	event.startVertexLocation = startVertexLocation;
	event.startInstanceLocation = startInstanceLocation;
	DX12BindingTracker::StoreEventLocked(event);
	DX12BindingTracker::StoreCurrentOutputTargetsLocked(commandList, snapshot, captureSessionId);
}

void DX12BindingTracker::RecordDrawIndexedInstanced(
	ID3D12GraphicsCommandList *commandList, UINT indexCountPerInstance,
	UINT instanceCount, UINT startIndexLocation, INT baseVertexLocation,
	UINT startInstanceLocation)
{
	if (!commandList)
		return;

	CommandListBindingState snapshot;
	const UINT64 drawSerial = static_cast<UINT64>(InterlockedIncrement64(&DX12BindingTrackerState::GlobalDrawSerial));
	CommandListBindingState *statePtr = GetOrResolveBindingState(commandList);
	BindingShard &shard = DX12BindingTrackerState::Shards[tlsStateCache.shardIndex];
	AcquireSRWLockExclusive(&shard.lock);
	if (tlsStateCache.shardGeneration != shard.generation) {
		statePtr = &shard.states[commandList];
		tlsStateCache.state = statePtr;
		tlsStateCache.shardGeneration = shard.generation;
	}
	CommandListBindingState &state = *statePtr;
	state.drawSerial = drawSerial;
	// Resolve deferred VB/IB resources before snapshot; see RecordDrawInstanced.
	ResolveDeferredIaResources(&state);
	snapshot = state;
	ReleaseSRWLockExclusive(&shard.lock);
	const UINT64 descriptorEpoch = CurrentDescriptorEpoch();
	const UINT64 captureSessionId = DX12BindingTracker::CurrentCaptureSessionId();
	if (DX12BindingCollectQueue::EnqueueDrawEvent(
			commandList,
			snapshot,
			"draw_indexed",
			descriptorEpoch,
			captureSessionId,
			0,
			indexCountPerInstance,
			instanceCount,
			0,
			startIndexLocation,
			baseVertexLocation,
			startInstanceLocation))
		return;

	BindingEvent event;
	if (!DX12BindingTracker::BuildEvent(
			&event,
			"draw_indexed",
			commandList,
			snapshot,
			descriptorEpoch,
			captureSessionId)) {
		return;
	}
	event.indexCountPerInstance = indexCountPerInstance;
	event.instanceCount = instanceCount;
	event.startIndexLocation = startIndexLocation;
	event.baseVertexLocation = baseVertexLocation;
	event.startInstanceLocation = startInstanceLocation;
	DX12BindingTracker::StoreEventLocked(event);
	DX12BindingTracker::StoreCurrentOutputTargetsLocked(commandList, snapshot, captureSessionId);
}

void DX12BindingTracker::RecordDispatch(
	ID3D12GraphicsCommandList *commandList, UINT threadGroupCountX,
	UINT threadGroupCountY, UINT threadGroupCountZ)
{
	if (!commandList)
		return;

	CommandListBindingState snapshot;
	const UINT64 dispatchSerial =
		static_cast<UINT64>(InterlockedIncrement64(&DX12BindingTrackerState::GlobalDispatchSerial));
	CommandListBindingState *statePtr = GetOrResolveBindingState(commandList);
	BindingShard &shard = DX12BindingTrackerState::Shards[tlsStateCache.shardIndex];
	AcquireSRWLockExclusive(&shard.lock);
	if (tlsStateCache.shardGeneration != shard.generation) {
		statePtr = &shard.states[commandList];
		tlsStateCache.state = statePtr;
		tlsStateCache.shardGeneration = shard.generation;
	}
	CommandListBindingState &state = *statePtr;
	state.dispatchSerial = dispatchSerial;
	snapshot = state;
	ReleaseSRWLockExclusive(&shard.lock);
	const UINT64 descriptorEpoch = CurrentDescriptorEpoch();
	const UINT64 captureSessionId = DX12BindingTracker::CurrentCaptureSessionId();
	if (DX12BindingCollectQueue::EnqueueDispatchEvent(
			commandList,
			snapshot,
			descriptorEpoch,
			captureSessionId,
			threadGroupCountX,
			threadGroupCountY,
			threadGroupCountZ))
		return;

	BindingEvent event;
	if (!DX12BindingTracker::BuildEvent(
			&event,
			"dispatch",
			commandList,
			snapshot,
			descriptorEpoch,
			captureSessionId)) {
		return;
	}
	event.threadGroupCountX = threadGroupCountX;
	event.threadGroupCountY = threadGroupCountY;
	event.threadGroupCountZ = threadGroupCountZ;
	DX12BindingTracker::StoreEventLocked(event);
}

void DX12BindingTracker::BeginFrame()
{
	InterlockedExchange64(&DX12BindingTrackerState::GlobalDrawSerial, 0);
	InterlockedExchange64(&DX12BindingTrackerState::GlobalDispatchSerial, 0);
}

void DX12BindingTracker::SetGraphicsRootDescriptor(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address)
{
	if (!commandList || rootParameterIndex >= MaxRootParameters)
		return;
	DX12ResidentMaintenanceTelemetryUtils::RecordGraphicsRootDescriptorCall();
	if (tlsGraphicsDescCache.cl == commandList &&
		tlsGraphicsDescCache.valid[rootParameterIndex] &&
		tlsGraphicsDescCache.address[rootParameterIndex] == address &&
		tlsGraphicsDescCache.type[rootParameterIndex] == type)
	{
		DX12ResidentMaintenanceTelemetryUtils::RecordGraphicsRootDescriptorTlsHit();
		return;
	}

	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	CommandListBindingState &state = shard.states[commandList];
	RootDescriptorState &descriptor = state.graphicsRootDescriptors[rootParameterIndex];
	const bool newValid = address != 0;
	if (descriptor.valid == newValid &&
		descriptor.rootParameterIndex == rootParameterIndex &&
		descriptor.type == type &&
		descriptor.address == address) {
		ReleaseSRWLockExclusive(&shard.lock);
		if (tlsGraphicsDescCache.cl != commandList) {
			memset(tlsGraphicsDescCache.valid, 0, sizeof(tlsGraphicsDescCache.valid));
			memset(tlsGraphicsDescCache.address, 0, sizeof(tlsGraphicsDescCache.address));
			memset(tlsGraphicsDescCache.type, 0, sizeof(tlsGraphicsDescCache.type));
			tlsGraphicsDescCache.cl = commandList;
		}
		tlsGraphicsDescCache.valid[rootParameterIndex] = true;
		tlsGraphicsDescCache.address[rootParameterIndex] = address;
		tlsGraphicsDescCache.type[rootParameterIndex] = type;
		DX12ResidentMaintenanceTelemetryUtils::RecordGraphicsRootDescriptorNoChange();
		return;
	}
	descriptor.valid = newValid;
	descriptor.rootParameterIndex = rootParameterIndex;
	descriptor.type = type;
	descriptor.address = address;
	DX12BindingTracker::BumpBindingQuerySerial(&state);
	ReleaseSRWLockExclusive(&shard.lock);
	if (tlsGraphicsDescCache.cl != commandList) {
		memset(tlsGraphicsDescCache.valid, 0, sizeof(tlsGraphicsDescCache.valid));
		memset(tlsGraphicsDescCache.address, 0, sizeof(tlsGraphicsDescCache.address));
		memset(tlsGraphicsDescCache.type, 0, sizeof(tlsGraphicsDescCache.type));
		tlsGraphicsDescCache.cl = commandList;
	}
	tlsGraphicsDescCache.valid[rootParameterIndex] = true;
	tlsGraphicsDescCache.address[rootParameterIndex] = address;
	tlsGraphicsDescCache.type[rootParameterIndex] = type;
	DX12ResidentMaintenanceTelemetryUtils::RecordGraphicsRootDescriptorChanged();
}

void DX12BindingTracker::SetComputeRootDescriptor(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address)
{
	if (!commandList || rootParameterIndex >= MaxRootParameters)
		return;
	DX12ResidentMaintenanceTelemetryUtils::RecordComputeRootDescriptorCall();
	if (tlsComputeDescCache.cl == commandList &&
		tlsComputeDescCache.valid[rootParameterIndex] &&
		tlsComputeDescCache.address[rootParameterIndex] == address &&
		tlsComputeDescCache.type[rootParameterIndex] == type)
	{
		DX12ResidentMaintenanceTelemetryUtils::RecordComputeRootDescriptorTlsHit();
		return;
	}

	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	CommandListBindingState &state = shard.states[commandList];
	RootDescriptorState &descriptor = state.computeRootDescriptors[rootParameterIndex];
	const bool newValid = address != 0;
	if (descriptor.valid == newValid &&
		descriptor.rootParameterIndex == rootParameterIndex &&
		descriptor.type == type &&
		descriptor.address == address) {
		ReleaseSRWLockExclusive(&shard.lock);
		if (tlsComputeDescCache.cl != commandList) {
			memset(tlsComputeDescCache.valid, 0, sizeof(tlsComputeDescCache.valid));
			memset(tlsComputeDescCache.address, 0, sizeof(tlsComputeDescCache.address));
			memset(tlsComputeDescCache.type, 0, sizeof(tlsComputeDescCache.type));
			tlsComputeDescCache.cl = commandList;
		}
		tlsComputeDescCache.valid[rootParameterIndex] = true;
		tlsComputeDescCache.address[rootParameterIndex] = address;
		tlsComputeDescCache.type[rootParameterIndex] = type;
		DX12ResidentMaintenanceTelemetryUtils::RecordComputeRootDescriptorNoChange();
		return;
	}
	descriptor.valid = newValid;
	descriptor.rootParameterIndex = rootParameterIndex;
	descriptor.type = type;
	descriptor.address = address;
	++state.computeBindingSerial;
	DX12BindingTracker::BumpBindingQuerySerial(&state);
	ReleaseSRWLockExclusive(&shard.lock);
	if (tlsComputeDescCache.cl != commandList) {
		memset(tlsComputeDescCache.valid, 0, sizeof(tlsComputeDescCache.valid));
		memset(tlsComputeDescCache.address, 0, sizeof(tlsComputeDescCache.address));
		memset(tlsComputeDescCache.type, 0, sizeof(tlsComputeDescCache.type));
		tlsComputeDescCache.cl = commandList;
	}
	tlsComputeDescCache.valid[rootParameterIndex] = true;
	tlsComputeDescCache.address[rootParameterIndex] = address;
	tlsComputeDescCache.type[rootParameterIndex] = type;
	DX12ResidentMaintenanceTelemetryUtils::RecordComputeRootDescriptorChanged();
}

bool DX12BindingTracker::SetRootConstantsLocked(
	RootConstantsState *constants, UINT rootParameterIndex,
	UINT destOffset, UINT count, const void *values)
{
	if (!constants || !values || destOffset >= 64 || count == 0)
		return false;
	const UINT *src = static_cast<const UINT*>(values);
	const UINT copyCount = min(count, 64 - destOffset);
	bool changed = !constants->valid ||
		constants->rootParameterIndex != rootParameterIndex;
	constants->valid = true;
	constants->rootParameterIndex = rootParameterIndex;
	for (UINT i = 0; i < copyCount; ++i) {
		const UINT slot = destOffset + i;
		changed = changed || !constants->valueValid[slot] ||
			constants->values[slot] != src[i];
		constants->values[slot] = src[i];
		constants->valueValid[slot] = true;
		if (slot + 1 > constants->maxSet)
			constants->maxSet = slot + 1;
	}
	return changed;
}

void DX12BindingTracker::SetGraphicsRoot32BitConstant(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	UINT destOffset, UINT value)
{
	DX12BindingTracker::SetGraphicsRoot32BitConstants(
		commandList, rootParameterIndex, destOffset, 1, &value);
}

void DX12BindingTracker::SetComputeRoot32BitConstant(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	UINT destOffset, UINT value)
{
	DX12BindingTracker::SetComputeRoot32BitConstants(
		commandList, rootParameterIndex, destOffset, 1, &value);
}

void DX12BindingTracker::SetGraphicsRoot32BitConstants(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	UINT destOffset, UINT count, const void *values)
{
	if (!commandList || rootParameterIndex >= MaxRootParameters)
		return;

	DX12ResidentMaintenanceTelemetryUtils::RecordGraphicsRootConstants(count);
	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	CommandListBindingState &state = shard.states[commandList];
	RootConstantsState &constants = state.graphicsRootConstants[rootParameterIndex];
	if (DX12BindingTracker::SetRootConstantsLocked(
			&constants, rootParameterIndex, destOffset, count, values))
		DX12BindingTracker::BumpBindingQuerySerial(&state);
	ReleaseSRWLockExclusive(&shard.lock);
}

void DX12BindingTracker::SetComputeRoot32BitConstants(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	UINT destOffset, UINT count, const void *values)
{
	if (!commandList || rootParameterIndex >= MaxRootParameters)
		return;

	DX12ResidentMaintenanceTelemetryUtils::RecordComputeRootConstants(count);
	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	CommandListBindingState &state = shard.states[commandList];
	RootConstantsState &constants = state.computeRootConstants[rootParameterIndex];
	if (DX12BindingTracker::SetRootConstantsLocked(
			&constants, rootParameterIndex, destOffset, count, values)) {
		++state.computeBindingSerial;
		DX12BindingTracker::BumpBindingQuerySerial(&state);
	}
	ReleaseSRWLockExclusive(&shard.lock);
}

bool DX12BindingTracker::GetCurrentComputeRootConstants(
	ID3D12GraphicsCommandList *commandList,
	std::vector<DX12CurrentRootConstants> *constants)
{
	if (!commandList || !constants)
		return false;

	constants->clear();
	CommandListBindingState snapshot;
	bool found = false;
	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockShared(&shard.lock);
	auto it = shard.states.find(commandList);
	if (it != shard.states.end()) {
		snapshot = it->second;
		found = true;
	}
	ReleaseSRWLockShared(&shard.lock);
	if (!found)
		return false;

	for (UINT root = 0; root < MaxRootParameters; ++root) {
		const RootConstantsState &source = snapshot.computeRootConstants[root];
		if (!source.valid)
			continue;
		DX12CurrentRootConstants current;
		current.rootParameterIndex = root;
		current.maxSet = source.maxSet;
		memcpy(current.values, source.values, sizeof(current.values));
		memcpy(current.valueValid, source.valueValid, sizeof(current.valueValid));
		constants->push_back(current);
	}
	return !constants->empty();
}

UINT64 DX12BindingTracker::GetComputeBindingSerial(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return 0;

	UINT64 serial = 0;
	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockShared(&shard.lock);
	auto it = shard.states.find(commandList);
	if (it != shard.states.end())
		serial = it->second.computeBindingSerial;
	ReleaseSRWLockShared(&shard.lock);
	return serial;
}

UINT64 DX12BindingTracker::GetBindingQuerySerial(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return 0;

	UINT64 serial = 0;
	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockShared(&shard.lock);
	auto it = shard.states.find(commandList);
	if (it != shard.states.end())
		serial = it->second.bindingQuerySerial;
	ReleaseSRWLockShared(&shard.lock);
	return serial;
}

static bool PopulatePreferredFrameEventSource(DX12FrameEventSource *source, const char *reason)
{
	if (!source)
		return false;

	if (DX12BindingTracker::PopulateActiveEventSource(
			source,
			reason ? reason : "PopulatePreferredFrameEventSource"))
		return true;

	*source = DX12FrameEventSource();
	return false;
}


void DX12BindingFrameExport::GetCurrentFrameShaderInfos(std::vector<DX12PsoShaderInfo> *shaderInfos)
{
	if (!shaderInfos)
		return;

	shaderInfos->clear();
	DX12FrameEventSource source;
	PopulatePreferredFrameEventSource(&source, "GetCurrentFrameShaderInfos");
	const std::vector<BindingEvent> &sourceEvents = source.GetEvents();
	std::unordered_set<UINT64> seen;
	for (const BindingEvent &event : sourceEvents) {
		if (!event.shaderInfo.psoIndex)
			continue;
		if (!seen.insert(event.shaderInfo.psoIndex).second)
			continue;
		shaderInfos->push_back(event.shaderInfo);
	}
}

void DX12BindingFrameExport::WriteShaderInfo(FILE *file, const DX12PsoShaderInfo &info)
{
	if (!file)
		return;

	fprintf(file, "%llu,",
		static_cast<unsigned long long>(info.psoIndex));
	if (info.hasVS)
		fprintf(file, "%016llx", static_cast<unsigned long long>(info.vs));
	fprintf(file, ",");
	if (info.hasPS)
		fprintf(file, "%016llx", static_cast<unsigned long long>(info.ps));
	fprintf(file, ",");
	if (info.hasCS)
		fprintf(file, "%016llx", static_cast<unsigned long long>(info.cs));
}

void DX12BindingFrameExport::FormatOptionalHashText(bool hasHash, UINT64 hash, char *text, size_t textCount)
{
	if (!text || textCount == 0)
		return;
	if (hasHash)
		sprintf_s(text, textCount, "%016llx", static_cast<unsigned long long>(hash));
	else
		sprintf_s(text, textCount, "-");
}

void DX12BindingFrameExport::AppendShaderHashNamePart(
	wchar_t *text, size_t textCount, const char *stage, bool hasHash, UINT64 hash)
{
	if (!text || textCount == 0 || !stage || !hasHash)
		return;
	const size_t used = wcslen(text);
	if (used >= textCount)
		return;
	swprintf_s(text + used, textCount - used, L"-%S%016llx",
		stage, static_cast<unsigned long long>(hash));
}

void DX12BindingFrameExport::BuildShaderHashNamePart(
	const DX12PsoShaderInfo &info, wchar_t *text, size_t textCount)
{
	if (!text || textCount == 0)
		return;
	text[0] = L'\0';
	DX12BindingFrameExport::AppendShaderHashNamePart(text, textCount, "vs", info.hasVS, info.vs);
	DX12BindingFrameExport::AppendShaderHashNamePart(text, textCount, "ps", info.hasPS, info.ps);
	DX12BindingFrameExport::AppendShaderHashNamePart(text, textCount, "cs", info.hasCS, info.cs);
}

void DX12BindingFrameExport::WriteRootTables(FILE *file, const RootTableState *tables)
{
	bool first = true;
	for (UINT i = 0; i < MaxRootParameters; ++i) {
		if (!tables[i].valid)
			continue;
		if (!first)
			fprintf(file, ";");
		fprintf(file, "%u=0x%llx",
			tables[i].rootParameterIndex,
			static_cast<unsigned long long>(tables[i].baseDescriptor.ptr));
		first = false;
	}
}

void DX12BindingFrameExport::WriteVertexBufferSlots(FILE *file, const BindingEvent &event)
{
	bool first = true;
	for (UINT slot = 0; slot < MaxVertexBufferSlots; ++slot) {
		if (!event.vertexBufferValid[slot])
			continue;
		const D3D12_VERTEX_BUFFER_VIEW &view = event.vertexBuffers[slot];
		if (!first)
			fprintf(file, ";");
		fprintf(file, "%u:0x%llx:%u:%u",
			slot,
			static_cast<unsigned long long>(view.BufferLocation),
			view.SizeInBytes,
			view.StrideInBytes);
		first = false;
	}
}

const char *DX12BindingFrameExport::TopologyName(D3D12_PRIMITIVE_TOPOLOGY topology)
{
	switch (topology) {
	case D3D_PRIMITIVE_TOPOLOGY_POINTLIST:
		return "POINTLIST";
	case D3D_PRIMITIVE_TOPOLOGY_LINELIST:
		return "LINELIST";
	case D3D_PRIMITIVE_TOPOLOGY_LINESTRIP:
		return "LINESTRIP";
	case D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST:
		return "TRIANGLELIST";
	case D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:
		return "TRIANGLESTRIP";
	default:
		return "UNKNOWN";
	}
}

std::string DX12BindingFrameExport::NextBufferId(const char *prefix, UINT *nextId)
{
	char id[32];
	const UINT value = nextId ? ++(*nextId) : 0;
	sprintf_s(id, "%s_%u", prefix ? prefix : "buf", value);
	return id;
}

DX12IaRegionDraw DX12BindingFrameExport::IaRegionDrawFromEvent(const BindingEvent &event)
{
	return DX12IaRegionDraw::FromDrawArguments(
		event.kind == "draw_indexed",
		event.vertexCountPerInstance,
		event.indexCountPerInstance,
		event.startVertexLocation,
		event.startIndexLocation,
		event.baseVertexLocation);
}

bool DX12BindingFrameExport::IaRegionForEvent(
	const BindingEvent &event, const char *role, UINT slot, DX12IaRegion *region)
{
	if (!region || !DX12BindingTrackerQuery::IsDrawEvent(event))
		return false;
	*region = DX12IaRegion();
	DX12IaRegionDraw draw = DX12BindingFrameExport::IaRegionDrawFromEvent(event);
	if (role && !strcmp(role, "IB")) {
		if (!draw.indexed || !event.indexBufferValid)
			return false;
		return region->SetFromIndexBufferView(event.indexBuffer, draw);
	}
	if (slot >= MaxVertexBufferSlots || !event.vertexBufferValid[slot])
		return false;
	return region->SetFromVertexBufferView(slot, event.vertexBuffers[slot], draw);
}

uint32_t DX12BindingFrameExport::IaRegionHashForEvent(const BindingEvent &event, const char *role, UINT slot)
{
	DX12IaRegionDraw draw = DX12BindingFrameExport::IaRegionDrawFromEvent(event);
	if (role && !strcmp(role, "IB"))
		return draw.indexed && event.indexBufferValid ?
			DX12IaRegion::HashIndexBufferView(event.indexBuffer, draw) : 0;
	if (slot >= MaxVertexBufferSlots || !event.vertexBufferValid[slot])
		return 0;
	return DX12IaRegion::HashVertexBufferView(slot, event.vertexBuffers[slot], draw);
}

bool DX12BindingFrameExport::ProjectResolvedIaResourceToView(
	UINT64 viewGpuVa, UINT64 viewSize, UINT64 drawGpuVa, UINT64 drawSize,
	const DX12BufferResourceSummary &resolvedDrawResource,
	DX12BufferResourceSummary *viewResource)
{
	if (!viewResource || !resolvedDrawResource.resource ||
		!resolvedDrawResource.hasResourceDesc || !viewGpuVa || !viewSize)
		return false;
	if (resolvedDrawResource.gpuVirtualAddress == 0)
		return false;
	if (viewGpuVa < resolvedDrawResource.gpuVirtualAddress)
		return false;

	*viewResource = resolvedDrawResource;
	const UINT64 viewOffset = viewGpuVa - resolvedDrawResource.gpuVirtualAddress;
	if (viewOffset >= resolvedDrawResource.resourceDesc.Width)
		return false;

	UINT64 availableBytes = resolvedDrawResource.resourceDesc.Width - viewOffset;
	UINT64 projectedViewBytes = viewSize < availableBytes ? viewSize : availableBytes;
	if (!projectedViewBytes)
		return false;

	viewResource->resourceOffset = viewOffset;
	viewResource->viewSize = projectedViewBytes;
	if (drawGpuVa && drawSize &&
		drawGpuVa >= viewGpuVa &&
		drawGpuVa - viewGpuVa <= projectedViewBytes) {
		const UINT64 drawOffsetWithinView = drawGpuVa - viewGpuVa;
		const UINT64 drawAvailableBytes = projectedViewBytes - drawOffsetWithinView;
		if (drawAvailableBytes < drawSize) {
			viewResource->viewSize = drawOffsetWithinView + drawAvailableBytes;
		}
	}
	return true;
}

bool DX12BindingFrameExport::ResolveIaBufferResourceSummary(
	UINT64 viewGpuVa, UINT64 viewSize, UINT64 drawGpuVa, UINT64 drawSize,
	const DX12BufferResourceSummary &existingResource, bool existingResolved,
	DX12BufferResourceSummary *resource)
{
	if (!resource || !viewGpuVa || !viewSize)
		return false;

	*resource = DX12BufferResourceSummary();
	if (existingResolved &&
		DX12BindingFrameExport::ProjectResolvedIaResourceToView(
			viewGpuVa, viewSize, drawGpuVa, drawSize, existingResource, resource))
		return true;

	if (DX12ResolveBufferResourceByGpuVa(viewGpuVa, viewSize, resource))
		return true;

	if (drawGpuVa && drawSize) {
		DX12BufferResourceSummary drawResource = {};
		if (DX12ResolveBufferResourceByGpuVa(drawGpuVa, drawSize, &drawResource) &&
			DX12BindingFrameExport::ProjectResolvedIaResourceToView(
				viewGpuVa, viewSize, drawGpuVa, drawSize, drawResource, resource)) {
#if defined(_DEBUG)
			DX12FrameAnalysis::LogJsonFunc("IaBufferResolveFallback",
				"\"mode\":\"draw_range\",\"view_gpu\":\"0x%llx\",\"view_bytes\":%llu,\"draw_gpu\":\"0x%llx\",\"draw_bytes\":%llu,\"resource\":\"%p\"",
				static_cast<unsigned long long>(viewGpuVa),
				static_cast<unsigned long long>(viewSize),
				static_cast<unsigned long long>(drawGpuVa),
				static_cast<unsigned long long>(drawSize),
				resource->resource);
#endif
			return true;
		}
	}

	if (DX12BindingFrameExport::ResolveIaBufferResourceSummaryFromCombinedRange(
			viewGpuVa,
			viewSize,
			drawGpuVa,
			drawSize,
			resource)) {
#if defined(_DEBUG)
		DX12FrameAnalysis::LogJsonFunc("IaBufferResolveFallback",
			"\"mode\":\"combined_range\",\"view_gpu\":\"0x%llx\",\"view_bytes\":%llu,\"draw_gpu\":\"0x%llx\",\"draw_bytes\":%llu,\"resource\":\"%p\"",
			static_cast<unsigned long long>(viewGpuVa),
			static_cast<unsigned long long>(viewSize),
			static_cast<unsigned long long>(drawGpuVa),
			static_cast<unsigned long long>(drawSize),
			resource->resource);
#endif
		return true;
	}

	return false;
}

bool DX12BindingFrameExport::ResolveIaBufferResourceSummaryFromCombinedRange(
	UINT64 viewGpuVa,
	UINT64 viewSize,
	UINT64 drawGpuVa,
	UINT64 drawSize,
	DX12BufferResourceSummary *resource)
{
	if (!resource || !viewGpuVa || !viewSize)
		return false;

	UINT64 combinedBegin = viewGpuVa;
	UINT64 combinedEnd = viewGpuVa + viewSize;
	if (combinedEnd < combinedBegin)
		return false;

	if (drawGpuVa && drawSize) {
		const UINT64 drawEnd = drawGpuVa + drawSize;
		if (drawEnd >= drawGpuVa) {
			if (drawGpuVa < combinedBegin)
				combinedBegin = drawGpuVa;
			if (drawEnd > combinedEnd)
				combinedEnd = drawEnd;
		}
	}

	if (combinedEnd <= combinedBegin)
		return false;

	DX12BufferResourceSummary combinedResource = {};
	if (!DX12ResolveBufferResourceByGpuVa(
			combinedBegin,
			combinedEnd - combinedBegin,
			&combinedResource))
		return false;

	return DX12BindingFrameExport::ProjectResolvedIaResourceToView(
		viewGpuVa,
		viewSize,
		drawGpuVa,
		drawSize,
		combinedResource,
		resource);
}

bool DX12BindingFrameExport::ResolveIaBufferResourceSummary(
	const DX12FrameIaBufferBinding &buffer, DX12BufferResourceSummary *resource)
{
	return DX12BindingFrameExport::ResolveIaBufferResourceSummary(
		buffer.viewGpuVa,
		buffer.viewSize,
		buffer.drawGpuVa ? buffer.drawGpuVa : buffer.gpuVa,
		buffer.drawSize ? buffer.drawSize : buffer.size,
		buffer.resource,
		buffer.resolved,
		resource);
}

void DX12BindingFrameExport::AddFrameIaBufferRow(
	std::vector<FlatBufferRow> *rows, UINT *nextVbId, UINT *nextIbId,
	const BindingEvent &event, const char *role, UINT slot)
{
	if (!rows)
		return;

	DX12IaRegion region = {};
	if (!DX12BindingFrameExport::IaRegionForEvent(event, role, slot, &region))
		return;
	const uint32_t huntHash = DX12BindingFrameExport::IaRegionHashForEvent(event, role, slot);
	if (!huntHash)
		return;
	const bool ib = role && !strcmp(role, "IB");
	const D3D12_VERTEX_BUFFER_VIEW &vb = event.vertexBuffers[slot];
	const UINT64 viewGpuVa = ib ? event.indexBuffer.BufferLocation : vb.BufferLocation;
	const UINT64 viewSize = ib ? event.indexBuffer.SizeInBytes : vb.SizeInBytes;
	const UINT64 drawGpuVa = ib ? region.gpuVa : viewGpuVa;
	const UINT64 drawSize = ib ? region.size : viewSize;
	FlatBufferRow row;
	row.id = DX12BindingFrameExport::NextBufferId(role && !strcmp(role, "IB") ? "ib" : "vb",
		role && !strcmp(role, "IB") ? nextIbId : nextVbId);
	row.role = role ? role : "";
	row.eventSerial = event.serial;
	row.drawId = event.drawId;
	row.dispatchId = event.dispatchId;
	row.psoIndex = event.shaderInfo.psoIndex;
	row.shaderInfo = event.shaderInfo;
	row.gpuVa = viewGpuVa;
	row.size = viewSize;
	row.viewGpuVa = viewGpuVa;
	row.viewSize = viewSize;
	row.drawGpuVa = drawGpuVa;
	row.drawSize = drawSize;
	row.drawOffset = region.viewOffset;
	row.stride = ib ? 0 : vb.StrideInBytes;
	row.slot = slot;
	row.format = ib ? static_cast<UINT>(event.indexBuffer.Format) : 0;
	if (ib) {
		row.drawStartIndex = event.startIndexLocation;
		row.drawIndexCount = event.indexCountPerInstance;
	} else {
		row.drawStartIndex = vb.StrideInBytes ? static_cast<UINT>(region.viewOffset / vb.StrideInBytes) : 0;
		row.drawIndexCount = vb.StrideInBytes ? static_cast<UINT>(region.size / vb.StrideInBytes) : 0;
	}
	row.drawBaseVertex = event.baseVertexLocation;
	row.drawStartInstance = event.startInstanceLocation;
	row.drawInstanceCount = event.instanceCount;
	row.topology = event.primitiveTopology;
	if (!ib)
		DX12GetPsoInputLayout(row.psoIndex, &row.inputLayout);
	row.resource = ib ? event.indexBufferResource : event.vertexBufferResources[slot];
	row.resolved = DX12BindingFrameExport::ResolveIaBufferResourceSummary(
		viewGpuVa,
		viewSize,
		drawGpuVa,
		drawSize,
		row.resource,
		ib ? event.indexBufferResolved : event.vertexBufferResolved[slot],
		&row.resource);
	row.viewOffset = row.resolved ? row.resource.resourceOffset : 0;
	row.huntHash = huntHash;
	rows->push_back(row);
}

void DX12BindingFrameExport::CollectFrameIaBuffersForEvents(
	const std::vector<BindingEvent> &events, std::vector<FlatBufferRow> *buffers)
{
	if (!buffers)
		return;

	buffers->clear();
	UINT nextVbId = 0;
	UINT nextIbId = 0;
	for (const BindingEvent &event : events) {
		if (!DX12BindingTrackerQuery::IsDrawEvent(event))
			continue;
		for (UINT slot = 0; slot < MaxVertexBufferSlots; ++slot) {
			if (!event.vertexBufferValid[slot])
				continue;
			DX12BindingFrameExport::AddFrameIaBufferRow(buffers, &nextVbId, &nextIbId, event, "VB", slot);
		}
		if (event.indexBufferValid) {
			DX12BindingFrameExport::AddFrameIaBufferRow(buffers, &nextVbId, &nextIbId, event, "IB", 0);
		}
	}
}

bool DX12BindingFrameExport::RangesOverlap(UINT64 aBegin, UINT64 aSize, UINT64 bBegin, UINT64 bSize)
{
	if (aSize == 0 || bSize == 0)
		return false;
	const UINT64 aEnd = aBegin + aSize;
	const UINT64 bEnd = bBegin + bSize;
	if (aEnd < aBegin || bEnd < bBegin)
		return false;
	return aBegin < bEnd && bBegin < aEnd;
}

bool DX12BindingFrameExport::BindingIsUavBufferProducer(const DX12FrameResourceBinding &binding)
{
	if (!binding.usageClass.empty())
		return binding.usageClass == "UAV" &&
			binding.hasDescriptor &&
			binding.descriptor.resource &&
			binding.descriptor.hasResourceDesc &&
			binding.descriptor.resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER;
	if (binding.rootDescriptor)
		return binding.rangeType == D3D12_ROOT_PARAMETER_TYPE_UAV &&
			binding.gpuVirtualAddress != 0;
	if (!binding.hasDescriptor || binding.descriptor.kind != "UAV")
		return false;
	if (!binding.descriptor.resource)
		return false;
	if (!binding.descriptor.hasResourceDesc ||
		binding.descriptor.resourceDesc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER)
		return false;
	return true;
}

BufferProducer DX12BindingFrameExport::ProducerFromBinding(const DX12FrameResourceBinding &binding)
{
	BufferProducer producer;
	producer.eventSerial = binding.eventSerial;
	producer.drawId = binding.drawId;
	producer.dispatchId = binding.dispatchId;
	producer.psoIndex = binding.psoIndex;
	producer.shaderInfo = binding.shaderInfo;
	producer.bindSpace = binding.bindSpace;
	producer.rootParameterIndex = binding.rootParameterIndex;
	producer.shaderRegister = binding.shaderRegister;
	if (!binding.usageClass.empty()) {
		producer.resource = binding.descriptor.resource;
		producer.begin = binding.usageBegin ? binding.usageBegin :
			binding.descriptor.gpuVirtualAddress + binding.descriptor.resourceOffset;
		const UINT64 bytes = binding.usageBytes ? binding.usageBytes :
			binding.descriptor.viewSize ? binding.descriptor.viewSize :
			binding.descriptor.resourceDesc.Width;
		producer.end = producer.begin + bytes;
		if (producer.end < producer.begin)
			producer.end = producer.begin;
		return producer;
	}
	if (binding.rootDescriptor) {
		DX12BufferResourceSummary resource;
		if (DX12ResolveBufferResourceByGpuVa(binding.gpuVirtualAddress, 1, &resource)) {
			producer.resource = resource.resource;
			producer.begin = binding.gpuVirtualAddress;
			const UINT64 viewBytes = resource.viewSize ? resource.viewSize :
				resource.hasResourceDesc ? resource.resourceDesc.Width : 1;
			producer.end = producer.begin + viewBytes;
			if (producer.end < producer.begin)
				producer.end = producer.begin;
		}
		return producer;
	}
	producer.resource = binding.descriptor.resource;
	producer.begin = binding.descriptor.gpuVirtualAddress + binding.descriptor.resourceOffset;
	const UINT64 viewBytes = binding.descriptor.viewSize ?
		binding.descriptor.viewSize : binding.descriptor.resourceDesc.Width;
	producer.end = producer.begin + viewBytes;
	if (producer.end < producer.begin)
		producer.end = producer.begin;
	return producer;
}

bool DX12BindingFrameExport::IsHuntingUavStateProducer(const BufferProducer &producer)
{
	return producer.bindSpace == "hunting_uav_state";
}

bool DX12BindingFrameExport::ProducerMatchesBuffer(
	const FlatBufferRow &row, const BufferProducer &producer)
{
	if (row.role != "VB" || !row.resolved || !row.resource.resource)
		return false;
	if (producer.resource != row.resource.resource)
		return false;
	return DX12BindingFrameExport::RangesOverlap(
		row.viewGpuVa, row.viewSize, producer.begin, producer.end - producer.begin);
}

bool DX12BindingFrameExport::ProducerIsPreferredForIaBuffer(
	const BufferProducer &candidate, const BufferProducer *best)
{
	if (!best)
		return true;

	const bool candidateFallback = DX12BindingFrameExport::IsHuntingUavStateProducer(candidate);
	const bool bestFallback = DX12BindingFrameExport::IsHuntingUavStateProducer(*best);
	if (candidateFallback != bestFallback)
		return !candidateFallback;

	if (candidate.eventSerial != best->eventSerial)
		return candidate.eventSerial > best->eventSerial;

	const bool candidateCompute = candidate.bindSpace == "compute_cbv_srv_uav";
	const bool bestCompute = best->bindSpace == "compute_cbv_srv_uav";
	if (candidateCompute != bestCompute)
		return candidateCompute;

	if (candidate.shaderRegister != UINT_MAX && best->shaderRegister == UINT_MAX)
		return true;
	if (candidate.shaderRegister == UINT_MAX && best->shaderRegister != UINT_MAX)
		return false;

	return false;
}

const BufferProducer *DX12BindingFrameExport::FindLatestProducerForBuffer(
	const FlatBufferRow &row, const std::vector<BufferProducer> &producers)
{
	const BufferProducer *best = nullptr;
	for (const BufferProducer &producer : producers) {
		if (producer.eventSerial >= row.eventSerial)
			continue;
		if (DX12BindingFrameExport::IsHuntingUavStateProducer(producer))
			continue;
		if (!DX12BindingFrameExport::ProducerMatchesBuffer(row, producer))
			continue;
		if (DX12BindingFrameExport::ProducerIsPreferredForIaBuffer(producer, best))
			best = &producer;
	}
	return best;
}

const BufferProducer *DX12BindingFrameExport::FindLatestHuntingUavStateProducerForBuffer(
	const FlatBufferRow &row, const std::vector<BufferProducer> &producers)
{
	const BufferProducer *best = nullptr;
	for (const BufferProducer &producer : producers) {
		if (producer.eventSerial >= row.eventSerial)
			continue;
		if (!DX12BindingFrameExport::IsHuntingUavStateProducer(producer))
			continue;
		if (!DX12BindingFrameExport::ProducerMatchesBuffer(row, producer))
			continue;
		if (DX12BindingFrameExport::ProducerIsPreferredForIaBuffer(producer, best))
			best = &producer;
	}
	return best;
}

void DX12BindingFrameExport::ClassifyIaBufferSkinning(
	std::vector<FlatBufferRow> *buffers,
	const std::vector<DX12FrameResourceBinding> &resourceBindings)
{
	if (!buffers)
		return;

	std::vector<BufferProducer> producers;
	size_t computeProducerCount = 0;
	size_t huntingProducerCount = 0;
	size_t otherProducerCount = 0;
	for (const DX12FrameResourceBinding &binding : resourceBindings) {
		if (!DX12BindingFrameExport::BindingIsUavBufferProducer(binding))
			continue;
		BufferProducer producer = DX12BindingFrameExport::ProducerFromBinding(binding);
		if (producer.begin == producer.end)
			continue;
		if (DX12BindingFrameExport::IsHuntingUavStateProducer(producer))
			huntingProducerCount++;
		else if (producer.bindSpace == "compute_cbv_srv_uav")
			computeProducerCount++;
		else
			otherProducerCount++;
		producers.push_back(std::move(producer));
	}

	size_t vbRows = 0;
	size_t ibRows = 0;
	size_t gpuPreskinRows = 0;
	size_t cpuPreskinRows = 0;
	size_t unknownRows = 0;
	size_t notApplicableRows = 0;
	size_t realProducerRows = 0;
	size_t huntingFallbackRows = 0;
	size_t huntingFallbackAvailableButIgnored = 0;
	for (FlatBufferRow &row : *buffers) {
		if (row.role != "VB") {
			row.skinningClass = "not_applicable";
			notApplicableRows++;
			ibRows++;
			continue;
		}
		vbRows++;

		const BufferProducer *producer = DX12BindingFrameExport::FindLatestProducerForBuffer(row, producers);
		const BufferProducer *huntingProducer =
			DX12BindingFrameExport::FindLatestHuntingUavStateProducerForBuffer(row, producers);
		if (producer && huntingProducer)
			huntingFallbackAvailableButIgnored++;
		if (!producer)
			producer = huntingProducer;
		if (producer) {
			row.skinningClass = "gpu_preskinning";
			row.producerEventSerial = producer->eventSerial;
			row.producerDrawId = producer->drawId;
			row.producerDispatchId = producer->dispatchId;
			row.producerPsoIndex = producer->psoIndex;
			row.producerShaderInfo = producer->shaderInfo;
			row.producerBindSpace = producer->bindSpace;
			row.producerRootParameterIndex = producer->rootParameterIndex;
			row.producerShaderRegister = producer->shaderRegister;
			gpuPreskinRows++;
			if (DX12BindingFrameExport::IsHuntingUavStateProducer(*producer))
				huntingFallbackRows++;
			else
				realProducerRows++;
		} else if (row.resolved) {
			row.skinningClass = "cpu_preskinning";
			cpuPreskinRows++;
		} else {
			row.skinningClass = "unknown";
			unknownRows++;
		}
	}

	DX12FrameAnalysis::LogJsonFunc("IaBufferSkinningClassification",
		"\"buffers\":%zu,\"vb\":%zu,\"ib\":%zu,\"producers\":%zu,\"compute_producers\":%zu,\"hunting_uav_state_producers\":%zu,\"other_producers\":%zu,\"gpu_preskinning\":%zu,\"gpu_preskinning_real_producer\":%zu,\"gpu_preskinning_hunting_fallback\":%zu,\"hunting_fallback_available_but_ignored\":%zu,\"cpu_preskinning\":%zu,\"unknown\":%zu,\"not_applicable\":%zu",
		buffers->size(),
		vbRows,
		ibRows,
		producers.size(),
		computeProducerCount,
		huntingProducerCount,
		otherProducerCount,
		gpuPreskinRows,
		realProducerRows,
		huntingFallbackRows,
		huntingFallbackAvailableButIgnored,
		cpuPreskinRows,
		unknownRows,
		notApplicableRows);
}

DX12FrameIaBufferBinding DX12BindingFrameExport::FrameIaBufferFromFlatRow(const FlatBufferRow &row)
{
	DX12FrameIaBufferBinding buffer;
	buffer.bufferId = row.id;
	buffer.role = row.role;
	buffer.skinningClass = row.skinningClass;
	buffer.eventSerial = row.eventSerial;
	buffer.drawId = row.drawId;
	buffer.dispatchId = row.dispatchId;
	buffer.psoIndex = row.psoIndex;
	buffer.shaderInfo = row.shaderInfo;
	buffer.producerEventSerial = row.producerEventSerial;
	buffer.producerDrawId = row.producerDrawId;
	buffer.producerDispatchId = row.producerDispatchId;
	buffer.producerPsoIndex = row.producerPsoIndex;
	buffer.producerShaderInfo = row.producerShaderInfo;
	buffer.producerBindSpace = row.producerBindSpace;
	buffer.producerRootParameterIndex = row.producerRootParameterIndex;
	buffer.producerShaderRegister = row.producerShaderRegister;
	buffer.gpuVa = row.gpuVa;
	buffer.size = row.size;
	buffer.viewGpuVa = row.viewGpuVa;
	buffer.viewSize = row.viewSize;
	buffer.viewOffset = row.viewOffset;
	buffer.drawGpuVa = row.drawGpuVa;
	buffer.drawSize = row.drawSize;
	buffer.drawOffset = row.drawOffset;
	buffer.stride = row.stride;
	buffer.slot = row.slot;
	buffer.format = row.format;
	buffer.drawStartIndex = row.drawStartIndex;
	buffer.drawIndexCount = row.drawIndexCount;
	buffer.drawBaseVertex = row.drawBaseVertex;
	buffer.drawStartInstance = row.drawStartInstance;
	buffer.drawInstanceCount = row.drawInstanceCount;
	buffer.topology = row.topology;
	buffer.inputLayout = row.inputLayout;
	buffer.huntHash = row.huntHash;
	buffer.resolved = row.resolved;
	buffer.resource = row.resource;
	return buffer;
}

void DX12BindingFrameExport::BuildIaBufferFileName(
	const DX12FrameIaBufferBinding &buffer, wchar_t *fileName, size_t fileNameCount)
{
	if (!fileName || fileNameCount == 0)
		return;

	fileName[0] = L'\0';
	const char *role = buffer.role.empty() ? "BUF" : buffer.role.c_str();
	const wchar_t eventKind = buffer.dispatchId ? L'c' : L'd';
	const UINT64 eventId = buffer.dispatchId ? buffer.dispatchId : buffer.drawId;
	wchar_t shaderPart[96];
	DX12BindingFrameExport::BuildShaderHashNamePart(buffer.shaderInfo, shaderPart, ARRAYSIZE(shaderPart));
	swprintf_s(fileName, fileNameCount,
		L"%c%06llu-pso%llu%s-ia_%S_slot%u_hash%08x_va%016llx_off%llu_size%llu_stride%u_fmt%u.buf",
		eventKind,
		static_cast<unsigned long long>(eventId),
		static_cast<unsigned long long>(buffer.psoIndex),
		shaderPart,
		role,
		buffer.slot,
		buffer.huntHash,
		static_cast<unsigned long long>(buffer.gpuVa),
		static_cast<unsigned long long>(buffer.viewOffset),
		static_cast<unsigned long long>(
			buffer.role == IaBufferRole::VB ? buffer.viewSize : buffer.size),
		buffer.stride,
		buffer.format);
}

void DX12BindingFrameExport::GetCurrentFrameIaBuffers(std::vector<DX12FrameIaBufferBinding> *buffers)
{
	std::vector<DX12FrameResourceBinding> resourceBindings;
	DX12BindingResourceExport::GetCurrentFrameResourceBindings(&resourceBindings);
	DX12BindingFrameExport::GetCurrentFrameIaBuffersWithResourceBindings(buffers, resourceBindings);
}

void DX12BindingFrameExport::GetCurrentFrameIaBuffersWithResourceBindings(
	std::vector<DX12FrameIaBufferBinding> *buffers,
	const std::vector<DX12FrameResourceBinding> &resourceBindings)
{
	if (!buffers)
		return;

	DX12FrameEventSource source;
	PopulatePreferredFrameEventSource(&source, "GetCurrentFrameIaBuffersWithResourceBindings");

	std::vector<FlatBufferRow> rows;
	DX12BindingFrameExport::CollectFrameIaBuffersForEvents(source.GetEvents(), &rows);
	DX12BindingFrameExport::ClassifyIaBufferSkinning(&rows, resourceBindings);

	buffers->clear();
	buffers->reserve(rows.size());
	for (const FlatBufferRow &row : rows)
		buffers->push_back(DX12BindingFrameExport::FrameIaBufferFromFlatRow(row));

	DX12FrameAnalysis::LogJsonFunc("IaBufferStage",
		"\"events\":%zu,\"buffers\":%zu,\"resource_bindings\":%zu,\"reused_resource_bindings\":1",
		source.GetEvents().size(), buffers->size(), resourceBindings.size());
}

DX12FrameAnalysisDumpResult DX12BindingFrameExport::DumpBindingTrace(const wchar_t *dir)
{
	DX12FrameAnalysisDumpResult result;
	if (!dir)
	{
		result.reason = "missing_directory";
		return result;
	}

	DX12FrameEventSource source;
	PopulatePreferredFrameEventSource(&source, "DumpBindingTrace");
	const std::vector<BindingEvent> &events = source.GetEvents();
	const UINT64 droppedEvents = source.droppedEvents;

	DX12FrameAnalysis::LogJsonFunc("BindingTraceBegin",
		"\"events\":%zu,\"capture_session_id\":%llu,\"dropped\":%llu,\"maxEvents\":0",
		events.size(),
		static_cast<unsigned long long>(source.captureSessionId),
		static_cast<unsigned long long>(droppedEvents));
	for (const BindingEvent &event : events) {
		if (DX12BindingTrackerQuery::IsDrawEvent(event) || DX12BindingTrackerQuery::IsDispatchEvent(event)) {
			DX12FrameAnalysisManifest::WriteCall(
				event.kind.c_str(),
				event.serial,
				event.drawId,
				event.dispatchId,
				event.commandList,
				event.pipelineState,
				event.shaderInfo,
				event.primitiveTopology,
				event.vertexCountPerInstance,
				event.indexCountPerInstance,
				event.startVertexLocation,
				event.startIndexLocation,
				event.baseVertexLocation,
				event.instanceCount,
				event.startInstanceLocation,
				event.threadGroupCountX,
				event.threadGroupCountY,
				event.threadGroupCountZ,
				event.indexBufferValid,
				event.indexBuffer.BufferLocation,
				event.indexBuffer.SizeInBytes,
				event.indexBuffer.Format);
		}

	}

	DX12FrameAnalysis::LogJsonFunc("BindingTraceEnd",
		"\"events\":%zu,\"dropped\":%llu",
		events.size(), static_cast<unsigned long long>(droppedEvents));
	result.ok = true;
	result.written = events.size();
	result.skipped = static_cast<size_t>(droppedEvents);
	return result;
}





static bool PopulatePreferredResourceEventSource(DX12FrameEventSource *source, const char *reason)
{
	if (!source)
		return false;

	if (DX12BindingTracker::PopulateActiveEventSource(
			source,
			reason ? reason : "PopulatePreferredResourceEventSource"))
		return true;

	*source = DX12FrameEventSource();
	return false;
}

bool DX12BindingResourceExport::InsertBindingSeen(
	std::unordered_set<std::string> *seen, const BindingEvent &event,
	const char *bindSpace, UINT rootParameterIndex, UINT rangeIndex, UINT rangeType,
	ID3D12DescriptorHeap *heap, UINT64 descriptorIndex, bool rootDescriptor)
{
	if (!seen)
		return false;

	char key[320];
	sprintf_s(key, "%llu|%llu|%llu|%llu|%s|%u|%u|%u|%p|%llu|%u",
		static_cast<unsigned long long>(event.serial),
		static_cast<unsigned long long>(event.drawId),
		static_cast<unsigned long long>(event.dispatchId),
		static_cast<unsigned long long>(event.shaderInfo.psoIndex),
		bindSpace ? bindSpace : "",
		rootParameterIndex,
		rangeIndex,
		rangeType,
		heap,
		static_cast<unsigned long long>(descriptorIndex),
		rootDescriptor ? 1 : 0);
	return seen->insert(key).second;
}

bool DX12BindingResourceExport::DescriptorMatchesRangeType(
	const DX12DescriptorSummary *descriptor, UINT rangeType)
{
	if (!descriptor)
		return true;
	if (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_CBV)
		return descriptor->kind == "CBV";
	if (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV)
		return descriptor->kind == "SRV";
	if (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV)
		return descriptor->kind == "UAV";
	if (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER)
		return descriptor->kind == "Sampler";
	return true;
}

const DX12DescriptorSummary *DX12BindingResourceExport::DescriptorForExpectedRange(
	const DX12DescriptorSummary *descriptor, UINT rangeType)
{
	return DX12BindingResourceExport::DescriptorMatchesRangeType(descriptor, rangeType) ? descriptor : nullptr;
}

const char *DX12BindingResourceExport::DescriptorKindForRangeType(UINT rangeType)
{
	if (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_CBV)
		return "CBV";
	if (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV)
		return "SRV";
	if (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV)
		return "UAV";
	if (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER)
		return "Sampler";
	return "";
}

const char *DX12BindingResourceExport::ExpectedBindingKind(const DX12FrameResourceBinding &binding)
{
	if (!binding.usageClass.empty())
		return binding.usageClass.c_str();
	if (binding.rootDescriptor) {
		if (binding.rangeType == D3D12_ROOT_PARAMETER_TYPE_CBV)
			return "CBV";
		if (binding.rangeType == D3D12_ROOT_PARAMETER_TYPE_SRV)
			return "SRV";
		if (binding.rangeType == D3D12_ROOT_PARAMETER_TYPE_UAV)
			return "UAV";
		return "";
	}
	if (binding.hasDescriptor)
		return binding.descriptor.kind.c_str();
	if (binding.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_CBV)
		return "CBV";
	if (binding.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV)
		return "SRV";
	if (binding.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV)
		return "UAV";
	if (binding.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER)
		return "Sampler";
	return "";
}

bool DX12BindingResourceExport::IsBufferResourceSummary(const DX12ResourceSummary &resource)
{
	return resource.resource &&
		resource.hasResourceDesc &&
		resource.resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER &&
		resource.size != 0;
}

void DX12BindingResourceExport::CollectHuntingUavStateResourceUsages(
	std::vector<DX12FrameResourceBinding> *bindings,
	const std::vector<BindingEvent> &events)
{
	if (!bindings || !DX12RuntimeState::IsHuntingMode())
		return;

	std::vector<DX12ResourceSummary> resources;
	DX12GetTrackedResourceSnapshot(&resources);
	if (resources.empty())
		return;

	std::vector<const BindingEvent*> dispatches;
	dispatches.reserve(events.size());
	for (const BindingEvent &event : events) {
		if (DX12BindingTrackerQuery::IsDispatchEvent(event))
			dispatches.push_back(&event);
	}
	if (dispatches.empty())
		return;

	std::unordered_set<std::string> seen;
	size_t candidates = 0;
	size_t emitted = 0;
	for (const DX12ResourceSummary &resource : resources) {
		if (!DX12BindingResourceExport::IsBufferResourceSummary(resource) ||
				!resource.hasCurrentState ||
				!(resource.currentState & D3D12_RESOURCE_STATE_UNORDERED_ACCESS))
			continue;
		candidates++;

		const BindingEvent *producerEvent = dispatches.back();
		char key[96];
		sprintf_s(key, "%p|%llu|%llu",
			resource.resource,
			static_cast<unsigned long long>(resource.gpuVirtualAddress),
			static_cast<unsigned long long>(resource.size));
		if (!seen.insert(key).second)
			continue;

		DX12FrameResourceBinding binding;
		binding.eventSerial = producerEvent->serial;
		binding.dispatchId = producerEvent->dispatchId;
		binding.psoIndex = producerEvent->shaderInfo.psoIndex;
		binding.shaderInfo = producerEvent->shaderInfo;
		binding.bindSpace = "hunting_uav_state";
		binding.rangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
		binding.shaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		binding.shaderRegister = UINT_MAX;
		binding.usageClass = "UAV";
		binding.usageBegin = resource.gpuVirtualAddress;
		binding.usageBytes = resource.size;
		binding.gpuVirtualAddress = resource.gpuVirtualAddress;
		binding.descriptor.kind = DescriptorKind::UAV;
		binding.descriptor.resource = resource.resource;
		binding.descriptor.resourceDesc = resource.resourceDesc;
		binding.descriptor.hasResourceDesc = true;
		binding.descriptor.resourceHeapType = resource.resourceHeapType;
		binding.descriptor.hasResourceHeapType = resource.hasResourceHeapType;
		binding.descriptor.gpuVirtualAddress = resource.gpuVirtualAddress;
		binding.descriptor.resourceOffset = 0;
		binding.descriptor.viewSize = resource.size;
		binding.descriptor.currentState = resource.currentState;
		binding.descriptor.hasCurrentState = resource.hasCurrentState;
		binding.hasDescriptor = true;
		bindings->push_back(std::move(binding));
		emitted++;
	}

	DX12FrameAnalysis::LogJsonFunc("HuntingUavStateResourceUsages",
		"resources=%zu,dispatches=%zu,candidates=%zu,emitted=%zu",
		resources.size(), dispatches.size(), candidates, emitted);
}

void DX12BindingResourceExport::CollectActivePreSkinProducerBindings(
	std::vector<DX12FrameResourceBinding> *bindings)
{
	if (!bindings)
		return;

	size_t candidates = 0;
	size_t emitted = 0;
	std::unordered_set<std::string> seen;
	const size_t initialCount = bindings->size();
	for (size_t i = 0; i < initialCount; ++i) {
		const DX12FrameResourceBinding &source = (*bindings)[i];
		if (source.bindSpace != "hunting_uav_state" ||
		    source.descriptor.kind != DescriptorKind::UAV ||
		    !source.hasDescriptor ||
		    !source.descriptor.resource ||
		    !source.descriptor.hasResourceDesc ||
		    source.descriptor.resourceDesc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER)
			continue;
		candidates++;

		DX12ModPreSkinProducerSummary producer = {};
		if (!DX12ModFindActivePreSkinProducerForOutput(
				source.descriptor.resource, source.descriptor.viewSize, 0, &producer))
			continue;
		if (!producer.hasDescriptor ||
		    producer.descriptor.kind != DescriptorKind::UAV ||
		    producer.shaderRegister == UINT_MAX)
			continue;

		char key[128];
		sprintf_s(key, "%llu|%p|%u|%u|%u|%u",
			static_cast<unsigned long long>(source.eventSerial),
			source.descriptor.resource,
			producer.rootParameterIndex,
			producer.rangeIndex,
			producer.shaderRegister,
			producer.registerSpace);
		if (!seen.insert(key).second)
			continue;

		DX12FrameResourceBinding binding = source;
		binding.bindSpace = "compute_cbv_srv_uav";
		binding.rootParameterIndex = producer.rootParameterIndex;
		binding.rangeIndex = producer.rangeIndex;
		binding.rangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
		binding.shaderRegister = producer.shaderRegister;
		binding.registerSpace = producer.registerSpace;
		binding.descriptorOffset = producer.descriptorOffset;
		binding.rootDescriptor = producer.rootDescriptor;
		binding.gpuVirtualAddress = producer.gpuVirtualAddress;
		binding.usageClass.clear();
		binding.usageBegin = 0;
		binding.usageBytes = 0;
		binding.descriptor.structureByteStride =
			producer.outputStride ? producer.outputStride :
			(source.descriptor.structureByteStride ?
			 source.descriptor.structureByteStride :
			 producer.descriptor.structureByteStride);
		if (!binding.descriptor.structureByteStride && producer.descriptor.numElements)
			binding.descriptor.structureByteStride =
				static_cast<UINT>(source.descriptor.viewSize / producer.descriptor.numElements);
		binding.descriptor.numElements =
			binding.descriptor.structureByteStride ?
			static_cast<UINT>(source.descriptor.viewSize / binding.descriptor.structureByteStride) :
			source.descriptor.numElements;
		binding.descriptor.bufferViewOffset = 0;
		binding.descriptor.bufferViewBytes = source.descriptor.viewSize;
		binding.descriptor.viewDimension = D3D12_UAV_DIMENSION_BUFFER;
		binding.descriptor.viewSize = source.descriptor.viewSize;
		binding.descriptor.resourceOffset = source.descriptor.resourceOffset;
		binding.descriptor.gpuVirtualAddress = source.descriptor.gpuVirtualAddress;
		binding.hasDescriptor = true;
		bindings->push_back(std::move(binding));
		emitted++;
	}

	DX12FrameAnalysis::LogJsonFunc("ActivePreSkinProducerBindings",
		"candidates=%zu,emitted=%zu",
		candidates, emitted);
}

bool DX12BindingResourceExport::DescriptorBelongsToHeap(
	const DX12DescriptorSummary &descriptor, const DX12DescriptorHeapSummary &heap,
	UINT64 *descriptorIndex)
{
	if (!descriptor.cpuHandle || !heap.cpuStart || !heap.increment || !descriptorIndex)
		return false;
	if (descriptor.cpuHandle < heap.cpuStart)
		return false;
	const SIZE_T offset = descriptor.cpuHandle - heap.cpuStart;
	if (offset % heap.increment != 0)
		return false;
	const UINT64 index = offset / heap.increment;
	if (index >= heap.numDescriptors)
		return false;
	*descriptorIndex = index;
	return true;
}

void DX12BindingResourceExport::BuildDescriptorLookup(
	const std::vector<DX12DescriptorSummary> &descriptors,
	const std::vector<DX12DescriptorHeapSummary> &heaps,
	DescriptorLookup *lookup)
{
	if (!lookup)
		return;
	lookup->byCpuHandle.clear();
	lookup->byCpuHandle.reserve(descriptors.size());
	lookup->bySlot.clear();
	lookup->bySlot.reserve(descriptors.size());

	for (const DX12DescriptorSummary &descriptor : descriptors) {
		lookup->byCpuHandle[descriptor.cpuHandle] = &descriptor;
		for (const DX12DescriptorHeapSummary &heap : heaps) {
			UINT64 descriptorIndex = 0;
			if (!DX12BindingResourceExport::DescriptorBelongsToHeap(descriptor, heap, &descriptorIndex))
				continue;
			DescriptorSlotKey key;
			key.heap = heap.heap;
			key.descriptorIndex = descriptorIndex;
			lookup->bySlot[key] = &descriptor;
			break;
		}
	}
}

const DX12DescriptorSummary *DX12BindingResourceExport::FindDescriptorForSlot(
	const DescriptorLookup &lookup, const DX12DescriptorHeapSummary *heap,
	UINT64 descriptorIndex, SIZE_T cpuHandle)
{
	auto byCpu = lookup.byCpuHandle.find(cpuHandle);
	if (byCpu != lookup.byCpuHandle.end())
		return byCpu->second;
	if (!heap)
		return nullptr;
	DescriptorSlotKey key;
	key.heap = heap->heap;
	key.descriptorIndex = descriptorIndex;
	auto bySlot = lookup.bySlot.find(key);
	return bySlot != lookup.bySlot.end() ? bySlot->second : nullptr;
}

const DX12DescriptorSummary *DX12BindingResourceExport::FindDescriptorForEventSlot(
	DX12DescriptorSummary *historyStorage,
	const BindingEvent &event,
	const DescriptorLookup &lookup, const DX12DescriptorHeapSummary *heap,
	UINT64 descriptorIndex, SIZE_T cpuHandle, UINT rangeType,
	size_t *historyLookups, size_t *historyHits)
{
	if (historyStorage && event.descriptorEpoch) {
		if (historyLookups)
			(*historyLookups)++;
		const char *kind = DX12BindingResourceExport::DescriptorKindForRangeType(rangeType);
		if (FindDescriptorSummaryByCpuHandleAtEpochAndKind(
				cpuHandle, event.descriptorEpoch, kind, historyStorage) ||
				FindDescriptorSummaryByCpuHandleAtEpoch(
					cpuHandle, event.descriptorEpoch, historyStorage)) {
			if (historyHits)
				(*historyHits)++;
			return historyStorage;
		}
	}
	return DX12BindingResourceExport::FindDescriptorForSlot(lookup, heap, descriptorIndex, cpuHandle);
}

UINT DX12BindingResourceExport::UnknownTableRangeType(UINT heapType)
{
	return heapType == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER ?
		D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER : UINT_MAX;
}

bool DX12BindingResourceExport::MakeTableBinding(
	DX12FrameResourceBinding *binding, const BindingEvent &event, const char *bindSpace,
	const RootTableState &table, UINT rangeIndex, UINT rangeType,
	UINT shaderVisibility, UINT shaderRegister, UINT registerSpace, UINT descriptorOffset,
	const DX12DescriptorHeapSummary *heap, UINT64 descriptorIndex,
	UINT64 gpuHandle, SIZE_T cpuHandle, const DX12DescriptorSummary *descriptor)
{
	if (!binding || !heap)
		return false;

	binding->eventSerial = event.serial;
	binding->drawId = event.drawId;
	binding->dispatchId = event.dispatchId;
	binding->psoIndex = event.shaderInfo.psoIndex;
	binding->shaderInfo = event.shaderInfo;
	binding->bindSpace = bindSpace ? bindSpace : "";
	binding->rootParameterIndex = table.rootParameterIndex;
	binding->rangeIndex = rangeIndex;
	binding->rangeType = rangeType;
	binding->shaderVisibility = shaderVisibility;
	binding->shaderRegister = shaderRegister;
	binding->registerSpace = registerSpace;
	binding->descriptorOffset = descriptorOffset;
	binding->heap = heap->heap;
	binding->heapType = heap->type;
	binding->descriptorIndex = descriptorIndex;
	binding->gpuHandle = gpuHandle;
	binding->cpuHandle = cpuHandle;
	binding->heapGpuStart = heap->gpuStart;
	if (descriptor) {
		binding->descriptor = *descriptor;
		binding->hasDescriptor = true;
	}
	return true;
}

bool DX12BindingResourceExport::CollectDescriptorTableEntry(
	std::vector<DX12FrameResourceBinding> *bindings,
	const BindingEvent &event, const char *bindSpace,
	const RootTableState &table, UINT rangeIndex, UINT rangeType,
	UINT shaderVisibility, UINT shaderRegister, UINT registerSpace, UINT descriptorOffset,
	const DX12DescriptorHeapSummary *heap, UINT64 descriptorIndex,
	UINT64 gpuHandle, SIZE_T cpuHandle,
	const DX12DescriptorSummary *descriptor,
	std::unordered_set<std::string> *seen, size_t *untrackedSkipped)
{
	if (!bindings || !heap)
		return false;
	const DX12DescriptorSummary *expectedDescriptor =
		DX12BindingResourceExport::DescriptorForExpectedRange(descriptor, rangeType);
	if (!DX12BindingResourceExport::InsertBindingSeen(seen, event, bindSpace, table.rootParameterIndex,
			rangeIndex, rangeType, heap->heap, descriptorIndex, false))
		return false;
	if (!expectedDescriptor && untrackedSkipped)
		(*untrackedSkipped)++;

	DX12FrameResourceBinding binding;
	if (!DX12BindingResourceExport::MakeTableBinding(&binding, event, bindSpace, table, rangeIndex, rangeType,
			shaderVisibility, shaderRegister, registerSpace, descriptorOffset,
			heap, descriptorIndex, gpuHandle, cpuHandle, expectedDescriptor))
		return false;
	bindings->push_back(std::move(binding));
	return true;
}

bool DX12BindingResourceExport::VisitTableStartOnly(
	const RootTableState &table, UINT heapType,
	const std::vector<DX12DescriptorHeapSummary> &heaps,
	UINT64 *descriptorIndex, UINT64 *gpuHandle, SIZE_T *cpuHandle,
	const DX12DescriptorHeapSummary **heap)
{
	if (!descriptorIndex || !gpuHandle || !cpuHandle || !heap)
		return false;
	*heap = DX12BindingTrackerQuery::FindHeapForGpuHandle(heaps, table, heapType);
	if (!*heap || (*heap)->increment == 0)
		return false;
	*descriptorIndex = (table.baseDescriptor.ptr - (*heap)->gpuStart) / (*heap)->increment;
	if (*descriptorIndex >= (*heap)->numDescriptors)
		return false;
	*gpuHandle = (*heap)->gpuStart + *descriptorIndex * (*heap)->increment;
	*cpuHandle = (*heap)->cpuStart + static_cast<SIZE_T>(*descriptorIndex) * (*heap)->increment;
	return true;
}

bool DX12BindingResourceExport::IsCompatibleRangeForSpace(const DX12RootDescriptorRangeSummary &range, const char *bindSpace)
{
	if (!bindSpace)
		return true;
	const bool samplerSpace = strstr(bindSpace, "sampler") != nullptr;
	return samplerSpace == (range.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER);
}

bool DX12BindingResourceExport::IsTextureResourceDimension(D3D12_RESOURCE_DIMENSION dimension)
{
	return dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D ||
		dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
		dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D;
}


void DX12BindingResourceExport::CollectFrameResourceBinding(
	std::vector<DX12FrameResourceBinding> *bindings,
	const BindingEvent &event, const char *bindSpace,
	const RootTableState &table, UINT heapType,
	const std::vector<DX12DescriptorHeapSummary> &heaps,
	const DescriptorLookup &descriptors,
	std::unordered_set<std::string> *seen, size_t *untrackedSkipped,
	size_t *historyLookups, size_t *historyHits)
{
	if (!bindings || !table.valid || !seen)
		return;

	DX12RootSignatureSummary rootSignature;
	const bool compute = bindSpace && !strncmp(bindSpace, "compute", 7);
	const DX12RootParameterSummary *parameter = nullptr;
	if (DX12BindingTrackerQuery::GetRootSignatureForEvent(event, compute, &rootSignature))
		parameter = DX12BindingTrackerQuery::FindRootParameter(rootSignature, table.rootParameterIndex);

	bool collectedRange = false;
	if (parameter && parameter->parameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
		for (const DX12RootDescriptorRangeSummary &range : parameter->ranges) {
			if (!DX12BindingResourceExport::IsCompatibleRangeForSpace(range, bindSpace))
				continue;
			const DX12DescriptorHeapSummary *heap = DX12BindingTrackerQuery::FindHeapForTableRange(heaps, table, &range, heapType);
			if (!heap)
				continue;
			const UINT64 tableBaseIndex = (table.baseDescriptor.ptr - heap->gpuStart) / heap->increment;
			const UINT count = DX12BindingTrackerQuery::DescriptorCountForRange(*heap, tableBaseIndex, range);
			for (UINT offset = 0; offset < count; ++offset) {
				const UINT64 descriptorIndex = tableBaseIndex + range.effectiveOffset + offset;
				if (descriptorIndex >= heap->numDescriptors)
					break;
				const UINT64 gpuHandle = heap->gpuStart + descriptorIndex * heap->increment;
				const SIZE_T cpuHandle = heap->cpuStart + static_cast<SIZE_T>(descriptorIndex) * heap->increment;
				DX12DescriptorSummary historyStorage;
				const DX12DescriptorSummary *descriptor = DX12BindingResourceExport::FindDescriptorForEventSlot(
					&historyStorage, event, descriptors, heap, descriptorIndex, cpuHandle,
					range.rangeType, historyLookups, historyHits);
				if (DX12BindingResourceExport::CollectDescriptorTableEntry(bindings, event, bindSpace, table,
						range.rangeIndex, range.rangeType, range.shaderVisibility,
						range.baseShaderRegister + offset, range.registerSpace,
						range.effectiveOffset + offset,
						heap, descriptorIndex, gpuHandle, cpuHandle, descriptor,
						seen, untrackedSkipped))
					collectedRange = true;
			}
		}
	}

	if (collectedRange)
		return;

	UINT64 descriptorIndex = 0;
	UINT64 gpuHandle = 0;
	SIZE_T cpuHandle = 0;
	const DX12DescriptorHeapSummary *heap = nullptr;
	if (DX12BindingResourceExport::VisitTableStartOnly(table, heapType, heaps, &descriptorIndex,
			&gpuHandle, &cpuHandle, &heap)) {
		DX12DescriptorSummary historyStorage;
		const DX12DescriptorSummary *descriptor = DX12BindingResourceExport::FindDescriptorForEventSlot(
			&historyStorage, event, descriptors, heap, descriptorIndex, cpuHandle,
			DX12BindingResourceExport::UnknownTableRangeType(heapType), historyLookups, historyHits);
		// Same design as file export: keep the table base binding when root metadata is absent,
		// but do not fabricate descriptor ranges that D3D12 did not expose.
		DX12BindingResourceExport::CollectDescriptorTableEntry(bindings, event, bindSpace, table, UINT_MAX,
			DX12BindingResourceExport::UnknownTableRangeType(heapType), D3D12_SHADER_VISIBILITY_ALL, UINT_MAX, 0, 0,
			heap, descriptorIndex, gpuHandle, cpuHandle, descriptor, seen, untrackedSkipped);
	}
}

void DX12BindingResourceExport::CollectFrameRootDescriptorBinding(
	std::vector<DX12FrameResourceBinding> *bindings,
	const BindingEvent &event, const char *bindSpace,
	const RootDescriptorState &rootDescriptor,
	std::unordered_set<std::string> *seen)
{
	if (!bindings || !rootDescriptor.valid || !seen)
		return;
	if (!DX12BindingResourceExport::InsertBindingSeen(seen, event, bindSpace, rootDescriptor.rootParameterIndex,
			UINT_MAX, static_cast<UINT>(rootDescriptor.type),
			nullptr, rootDescriptor.address, true))
		return;

	DX12RootSignatureSummary rootSignature;
	const bool compute = bindSpace && !strncmp(bindSpace, "compute", 7);
	const DX12RootParameterSummary *parameter = nullptr;
	if (DX12BindingTrackerQuery::GetRootSignatureForEvent(event, compute, &rootSignature))
		parameter = DX12BindingTrackerQuery::FindRootParameter(rootSignature, rootDescriptor.rootParameterIndex);

	DX12FrameResourceBinding binding;
	binding.eventSerial = event.serial;
	binding.drawId = event.drawId;
	binding.dispatchId = event.dispatchId;
	binding.psoIndex = event.shaderInfo.psoIndex;
	binding.shaderInfo = event.shaderInfo;
	binding.bindSpace = bindSpace ? bindSpace : "";
	binding.rootParameterIndex = rootDescriptor.rootParameterIndex;
	binding.rangeIndex = UINT_MAX;
	binding.rangeType = static_cast<UINT>(rootDescriptor.type);
	binding.shaderVisibility = parameter ? parameter->shaderVisibility : D3D12_SHADER_VISIBILITY_ALL;
	binding.shaderRegister = parameter ? parameter->shaderRegister : UINT_MAX;
	binding.registerSpace = parameter ? parameter->registerSpace : 0;
	binding.rootDescriptor = true;
	binding.gpuVirtualAddress = rootDescriptor.address;
	DX12BufferResourceSummary resource;
	if (DX12ResolveBufferResourceByGpuVa(rootDescriptor.address, 1, &resource) &&
			resource.resource && resource.hasResourceDesc) {
		// Root descriptors carry only a GPU VA in D3D12, so project the resolved
		// buffer into descriptor form here. This keeps compute UAV producers on
		// the same event-resource path as descriptor tables, matching RenderDoc's
		// resource-usage granularity without scanning resources in Mod mode.
		binding.descriptor.kind = DX12BindingResourceExport::ExpectedBindingKind(binding);
		binding.descriptor.resource = resource.resource;
		binding.descriptor.resourceDesc = resource.resourceDesc;
		binding.descriptor.hasResourceDesc = true;
		binding.descriptor.resourceHeapType = resource.resourceHeapType;
		binding.descriptor.hasResourceHeapType = resource.hasResourceHeapType;
		binding.descriptor.gpuVirtualAddress = resource.gpuVirtualAddress;
		binding.descriptor.resourceOffset = resource.resourceOffset;
		const UINT64 availableBytes = resource.resourceDesc.Width > resource.resourceOffset ?
			resource.resourceDesc.Width - resource.resourceOffset : 0;
		binding.descriptor.viewSize = availableBytes;
		binding.descriptor.currentState = resource.currentState;
		binding.descriptor.hasCurrentState = resource.hasCurrentState;
		binding.hasDescriptor = true;
	}
	bindings->push_back(std::move(binding));
}

void DX12BindingResourceExport::CountEventTables(
	const BindingEvent &event,
	const std::vector<DX12DescriptorHeapSummary> &heaps,
	size_t *graphicsTables, size_t *computeTables,
	size_t *graphicsTablesWithRoots, size_t *computeTablesWithRoots,
	size_t *rootRangeTables, size_t *tableStartOnly,
	size_t *unboundedRanges, size_t *unboundedDescriptors,
	size_t *graphicsTablesWithoutRoots, size_t *computeTablesWithoutRoots,
	size_t *descriptorTablesWithoutRanges)
{
	if (!graphicsTables || !computeTables || !graphicsTablesWithRoots ||
			!computeTablesWithRoots || !rootRangeTables || !tableStartOnly ||
			!unboundedRanges || !unboundedDescriptors ||
			!graphicsTablesWithoutRoots || !computeTablesWithoutRoots ||
			!descriptorTablesWithoutRanges)
		return;

	DX12RootSignatureSummary graphicsRoot;
	DX12RootSignatureSummary computeRoot;
	const bool hasGraphicsRoot = DX12BindingTrackerQuery::GetRootSignatureForEvent(event, false, &graphicsRoot);
	const bool hasComputeRoot = DX12BindingTrackerQuery::GetRootSignatureForEvent(event, true, &computeRoot);
	for (UINT i = 0; i < MaxRootParameters; ++i) {
		if (event.graphicsTables[i].valid) {
			(*graphicsTables)++;
			if (hasGraphicsRoot) {
				(*graphicsTablesWithRoots)++;
				const DX12RootParameterSummary *parameter =
					DX12BindingTrackerQuery::FindRootParameter(graphicsRoot, i);
				if (parameter && parameter->parameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
					(*rootRangeTables)++;
					const DX12DescriptorHeapSummary *heap =
						DX12BindingTrackerQuery::FindHeapForGpuHandle(heaps, event.graphicsTables[i],
							D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
					if (heap && heap->increment) {
						const UINT64 tableBaseIndex =
							(event.graphicsTables[i].baseDescriptor.ptr - heap->gpuStart) / heap->increment;
						for (const DX12RootDescriptorRangeSummary &range : parameter->ranges) {
							if (range.numDescriptors != UINT_MAX)
								continue;
							(*unboundedRanges)++;
							(*unboundedDescriptors) +=
								DX12BindingTrackerQuery::DescriptorCountForRange(*heap, tableBaseIndex, range);
						}
					}
				} else {
					(*tableStartOnly)++;
					(*descriptorTablesWithoutRanges)++;
				}
			} else {
				(*tableStartOnly)++;
				(*graphicsTablesWithoutRoots)++;
			}
		}
		if (event.computeTables[i].valid) {
			(*computeTables)++;
			if (hasComputeRoot) {
				(*computeTablesWithRoots)++;
				const DX12RootParameterSummary *parameter =
					DX12BindingTrackerQuery::FindRootParameter(computeRoot, i);
				if (parameter && parameter->parameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
					(*rootRangeTables)++;
					const DX12DescriptorHeapSummary *heap =
						DX12BindingTrackerQuery::FindHeapForGpuHandle(heaps, event.computeTables[i],
							D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
					if (heap && heap->increment) {
						const UINT64 tableBaseIndex =
							(event.computeTables[i].baseDescriptor.ptr - heap->gpuStart) / heap->increment;
						for (const DX12RootDescriptorRangeSummary &range : parameter->ranges) {
							if (range.numDescriptors != UINT_MAX)
								continue;
							(*unboundedRanges)++;
							(*unboundedDescriptors) +=
								DX12BindingTrackerQuery::DescriptorCountForRange(*heap, tableBaseIndex, range);
						}
					}
				} else {
					(*tableStartOnly)++;
					(*descriptorTablesWithoutRanges)++;
				}
			} else {
				(*tableStartOnly)++;
				(*computeTablesWithoutRoots)++;
			}
		}
	}
}

void DX12BindingResourceExport::CollectEventResourceBindings(
	std::vector<DX12FrameResourceBinding> *bindings,
	const BindingEvent &event,
	const std::vector<DX12DescriptorHeapSummary> &heaps,
	const DescriptorLookup &descriptorLookup,
	std::unordered_set<std::string> *seen,
	size_t *untrackedSkipped,
	size_t *historyLookups,
	 size_t *historyHits)
{
	if (!bindings || !seen)
		return;
	for (UINT i = 0; i < MaxRootParameters; ++i) {
		DX12BindingResourceExport::CollectFrameResourceBinding(bindings, event, "graphics_cbv_srv_uav",
			event.graphicsTables[i], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
			heaps, descriptorLookup, seen, untrackedSkipped, historyLookups, historyHits);
		DX12BindingResourceExport::CollectFrameResourceBinding(bindings, event, "graphics_sampler",
			event.graphicsTables[i], D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
			heaps, descriptorLookup, seen, untrackedSkipped, historyLookups, historyHits);
		DX12BindingResourceExport::CollectFrameResourceBinding(bindings, event, "compute_cbv_srv_uav",
			event.computeTables[i], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
			heaps, descriptorLookup, seen, untrackedSkipped, historyLookups, historyHits);
		DX12BindingResourceExport::CollectFrameResourceBinding(bindings, event, "compute_sampler",
			event.computeTables[i], D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
			heaps, descriptorLookup, seen, untrackedSkipped, historyLookups, historyHits);
		DX12BindingResourceExport::CollectFrameRootDescriptorBinding(bindings, event, "graphics_root",
			event.graphicsRootDescriptors[i], seen);
		DX12BindingResourceExport::CollectFrameRootDescriptorBinding(bindings, event, "compute_root",
			event.computeRootDescriptors[i], seen);
	}
}

void DX12BindingResourceExport::GetCurrentFrameResourceBindings(std::vector<DX12FrameResourceBinding> *bindings)
{
	if (!bindings)
		return;

	DX12FrameEventSource source;
	PopulatePreferredResourceEventSource(&source, "GetCurrentFrameResourceBindings");
	const std::vector<BindingEvent> &events = source.GetEvents();
	DX12FrameAnalysis::LogJsonFunc("BindingResourcesSnapshot",
		"events=%zu,capture_session_id=%llu",
		events.size(),
		static_cast<unsigned long long>(source.captureSessionId));

	std::vector<DX12DescriptorSummary> descriptors;
	std::vector<DX12DescriptorHeapSummary> heaps;
	DX12GetResourceMetadataSnapshot(nullptr, &descriptors, nullptr, &heaps);
	DX12FrameAnalysis::LogJsonFunc("BindingResourcesMetadataReady",
		"descriptors=%zu,heaps=%zu",
		descriptors.size(), heaps.size());
	DescriptorLookup descriptorLookup;
	DX12BindingResourceExport::BuildDescriptorLookup(descriptors, heaps, &descriptorLookup);
	DX12FrameAnalysis::LogJsonFunc("BindingResourcesLookupReady",
		"cpu_entries=%zu,slot_entries=%zu",
		descriptorLookup.byCpuHandle.size(), descriptorLookup.bySlot.size());

	bindings->clear();
	std::unordered_set<std::string> seen;
	size_t drawEvents = 0;
	size_t dispatchEvents = 0;
	size_t graphicsTables = 0;
	size_t computeTables = 0;
	size_t graphicsTablesWithRoots = 0;
	size_t computeTablesWithRoots = 0;
	size_t rootRangeTables = 0;
	size_t tableStartOnly = 0;
	size_t unboundedRanges = 0;
	size_t unboundedDescriptors = 0;
	size_t graphicsTablesWithoutRoots = 0;
	size_t computeTablesWithoutRoots = 0;
	size_t descriptorTablesWithoutRanges = 0;
	size_t samplerBindings = 0;
	size_t cbvBindings = 0;
	size_t srvBindings = 0;
	size_t uavBindings = 0;
	size_t untrackedBindings = 0;
	size_t untrackedCbvBindings = 0;
	size_t untrackedSrvBindings = 0;
	size_t untrackedUavBindings = 0;
	size_t untrackedSkipped = 0;
	size_t textureBindings = 0;
	size_t rebuiltEventBindings = 0;
	size_t rebuiltResolvedBindings = 0;
	size_t descriptorHistoryLookups = 0;
	size_t descriptorHistoryHits = 0;
	size_t zeroDescriptorEpochEvents = 0;
	for (const BindingEvent &event : events) {
		if (!DX12BindingTrackerQuery::IsDrawEvent(event) && !DX12BindingTrackerQuery::IsDispatchEvent(event))
			continue;
		if (event.descriptorEpoch == 0)
			zeroDescriptorEpochEvents++;
		if (DX12BindingTrackerQuery::IsDrawEvent(event))
			drawEvents++;
		if (DX12BindingTrackerQuery::IsDispatchEvent(event))
			dispatchEvents++;
		DX12BindingResourceExport::CountEventTables(event, heaps, &graphicsTables, &computeTables,
			&graphicsTablesWithRoots, &computeTablesWithRoots,
			&rootRangeTables, &tableStartOnly, &unboundedRanges,
			&unboundedDescriptors, &graphicsTablesWithoutRoots,
			&computeTablesWithoutRoots, &descriptorTablesWithoutRanges);
		const size_t before = bindings->size();
		// Rebuild from the final descriptor metadata snapshot. Event recording only
		// stores root table/root descriptor state; resolving resources here matches
		// RenderDoc's event-granular model and avoids hot-path descriptor scans.
		DX12BindingResourceExport::CollectEventResourceBindings(
			bindings, event, heaps, descriptorLookup, &seen, &untrackedSkipped,
			&descriptorHistoryLookups, &descriptorHistoryHits);
		rebuiltEventBindings += bindings->size() - before;
		for (size_t i = before; i < bindings->size(); ++i) {
			if ((*bindings)[i].hasDescriptor)
				rebuiltResolvedBindings++;
		}
	}
	DX12FrameAnalysis::LogJsonFunc("BindingResourcesDiscoveryFallbackSkipped",
		"\"reason\":\"single_frame_event_scope\",\"hunting_uav_state\":0,\"active_pre_skin\":0");
	for (const DX12FrameResourceBinding &binding : *bindings) {
		const char *expectedKind = DX12BindingResourceExport::ExpectedBindingKind(binding);
		if (!binding.hasDescriptor) {
			untrackedBindings++;
			if (!strcmp(expectedKind, "CBV"))
				untrackedCbvBindings++;
			else if (!strcmp(expectedKind, "SRV"))
				untrackedSrvBindings++;
			else if (!strcmp(expectedKind, "UAV"))
				untrackedUavBindings++;
			continue;
		}
		if (!strcmp(expectedKind, "CBV"))
			cbvBindings++;
		else if (!strcmp(expectedKind, "SRV"))
			srvBindings++;
		else if (!strcmp(expectedKind, "UAV"))
			uavBindings++;
		else if (!strcmp(expectedKind, "Sampler"))
			samplerBindings++;
		if (binding.descriptor.hasResourceDesc &&
			DX12BindingResourceExport::IsTextureResourceDimension(binding.descriptor.resourceDesc.Dimension))
			textureBindings++;
	}
	DX12FrameAnalysis::LogJsonFunc("BindingResourcesCollected",
		"bindings=%zu,unique=%zu,rebuilt_bindings=%zu,rebuilt_resolved=%zu,descriptor_history_lookups=%zu,descriptor_history_hits=%zu,zero_descriptor_epoch_events=%zu,draw_events=%zu,dispatch_events=%zu,graphics_tables=%zu,compute_tables=%zu,graphics_tables_with_roots=%zu,compute_tables_with_roots=%zu,root_range_tables=%zu,table_start_only=%zu,graphics_tables_without_roots=%zu,compute_tables_without_roots=%zu,descriptor_tables_without_ranges=%zu,unbounded_ranges=%zu,unbounded_descriptors=%zu,cbv=%zu,srv=%zu,uav=%zu,sampler=%zu,texture=%zu,untracked=%zu,untracked_cbv=%zu,untracked_srv=%zu,untracked_uav=%zu,untracked_skipped=%zu",
		bindings->size(), seen.size(), rebuiltEventBindings, rebuiltResolvedBindings,
		descriptorHistoryLookups, descriptorHistoryHits, zeroDescriptorEpochEvents,
		drawEvents, dispatchEvents,
		graphicsTables, computeTables,
		graphicsTablesWithRoots, computeTablesWithRoots,
		rootRangeTables, tableStartOnly,
		graphicsTablesWithoutRoots, computeTablesWithoutRoots,
		descriptorTablesWithoutRanges,
		unboundedRanges, unboundedDescriptors,
		cbvBindings, srvBindings, uavBindings, samplerBindings,
		textureBindings, untrackedBindings,
		untrackedCbvBindings, untrackedSrvBindings, untrackedUavBindings,
		untrackedSkipped);
	const char *historyStatus = "ok";
	if (zeroDescriptorEpochEvents)
		historyStatus = "zero_epoch";
	else if (descriptorHistoryLookups && !descriptorHistoryHits)
		historyStatus = "miss";
	else if (untrackedUavBindings && !descriptorHistoryLookups)
		historyStatus = "no_history_lookup";
	else if (untrackedUavBindings)
		historyStatus = "untracked_uav";
	DX12FrameAnalysis::LogJsonFunc("BindingResourcesHistoryHealth",
		"status=%s,events=%zu,zero_descriptor_epoch_events=%zu,descriptor_history_lookups=%zu,descriptor_history_hits=%zu,untracked_uav=%zu",
		historyStatus,
		drawEvents + dispatchEvents,
		zeroDescriptorEpochEvents,
		descriptorHistoryLookups,
		descriptorHistoryHits,
		untrackedUavBindings);
}

void DX12BindingResourceExport::GetCurrentFrameOutputBindings(std::vector<DX12FrameOutputBinding> *bindings)
{
	if (!bindings)
		return;

	DX12FrameEventSource source;
	PopulatePreferredResourceEventSource(&source, "GetCurrentFrameOutputBindings");
	const std::vector<OutputBindingEvent> &events = source.GetOutputEvents();
	const UINT64 dropped = source.droppedOutputEvents;
	DX12FrameAnalysis::LogJsonFunc("OutputResourcesLedgerSnapshot",
		"events=%zu,capture_session_id=%llu,dropped=%llu",
		events.size(),
		static_cast<unsigned long long>(source.captureSessionId),
		static_cast<unsigned long long>(dropped));

	std::unordered_set<std::string> seen;
	bindings->clear();
	bindings->reserve(events.size());
	size_t rtv = 0;
	size_t dsv = 0;
	size_t untracked = 0;
	std::vector<OutputBindingEvent> uniqueEvents;
	std::vector<SIZE_T> cpuHandles;
	uniqueEvents.reserve(events.size());
	cpuHandles.reserve(events.size());
	for (const OutputBindingEvent &event : events) {
		if (!event.cpuHandle)
			continue;
		char key[160];
		sprintf_s(key, "%s|%u|%zx|%llu|%llu|%llu",
			event.kind.c_str(),
			event.slot,
			event.cpuHandle,
			static_cast<unsigned long long>(event.drawId),
			static_cast<unsigned long long>(event.dispatchId),
			static_cast<unsigned long long>(
				(event.drawId || event.dispatchId) ? 0 : event.serial));
		if (!seen.insert(key).second)
			continue;
		uniqueEvents.push_back(event);
		cpuHandles.push_back(event.cpuHandle);
	}

	std::vector<DX12DescriptorSummary> descriptorSummaries(uniqueEvents.size());
	std::vector<uint8_t> descriptorFound(uniqueEvents.size());
	DX12FindDescriptorSummariesByCpuHandle(
		cpuHandles.data(), cpuHandles.size(),
		descriptorSummaries.data(), descriptorFound.data(), true);

	for (size_t i = 0; i < uniqueEvents.size(); ++i) {
		const OutputBindingEvent &event = uniqueEvents[i];
		DX12FrameOutputBinding binding;
		binding.kind = event.kind;
		binding.eventSerial = event.serial;
		binding.drawId = event.drawId;
		binding.dispatchId = event.dispatchId;
		binding.psoIndex = event.shaderInfo.psoIndex;
		binding.shaderInfo = event.shaderInfo;
		binding.slot = event.slot;
		binding.cpuHandle = event.cpuHandle;
		binding.hasDescriptor = descriptorFound[i] != 0;
		if (binding.hasDescriptor)
			binding.descriptor = descriptorSummaries[i];
		if (binding.kind == "RTV")
			rtv++;
		else if (binding.kind == "DSV")
			dsv++;
		if (!binding.hasDescriptor)
			untracked++;
		bindings->push_back(std::move(binding));
	}

	DX12FrameAnalysis::LogJsonFunc("OutputResourcesCollected",
		"bindings=%zu,rtv=%zu,dsv=%zu,untracked=%zu,dropped=%llu",
		bindings->size(), rtv, dsv, untracked,
		static_cast<unsigned long long>(dropped));
	DX12LogManager::Fields("DX12OutputResourcesCollected",
		"bindings=%zu,rtv=%zu,dsv=%zu,untracked=%zu,dropped=%llu",
		bindings->size(), rtv, dsv, untracked,
		static_cast<unsigned long long>(dropped));
}

