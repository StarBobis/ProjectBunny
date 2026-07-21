

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
	started.store(true, std::memory_order_release);
}


void DX12BindingCollectQueue::Stop()
{
	started.store(false, std::memory_order_release);
}
//STOP_R
void DX12BindingCollectQueue::DrainForSnapshot(const char *reason)
{
	DX12LogManager::Fields("DX12BindingCollectQueue",
		"event=DrainBypassed,reason=%s,mode=synchronous",
		reason ? reason : "Snapshot");
}

void DX12BindingCollectQueue::BeginFrameDrain()
{
	DX12LogManager::Fields("DX12BindingCollectQueue",
		"event=DrainBypassed,reason=BeginFrame,mode=synchronous");
}

bool DX12BindingCollectQueue::EnqueueStateEvent(
	ID3D12GraphicsCommandList *commandList,
	const CommandListBindingState &state,
	const char *kind,
	UINT64 descriptorEpoch,
	UINT64 captureSessionId)
{
	(void)commandList;
	(void)state;
	(void)kind;
	(void)descriptorEpoch;
	(void)captureSessionId;
	return false;
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
	(void)commandList;
	(void)state;
	(void)kind;
	(void)descriptorEpoch;
	(void)captureSessionId;
	(void)vertexCountPerInstance;
	(void)indexCountPerInstance;
	(void)instanceCount;
	(void)startVertexLocation;
	(void)startIndexLocation;
	(void)baseVertexLocation;
	(void)startInstanceLocation;
	return false;
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
	(void)commandList;
	(void)state;
	(void)descriptorEpoch;
	(void)captureSessionId;
	(void)threadGroupCountX;
	(void)threadGroupCountY;
	(void)threadGroupCountZ;
	return false;
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
	(void)commandList;
	(void)state;
	(void)kind;
	(void)slot;
	(void)cpuHandle;
	(void)drawId;
	(void)captureSessionId;
	return false;
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

	DX12LogManager::Fields("DX12BindingCollectQueue",
		"event=SyncFallback,kind=%u,commandList=%p,reason=no_worker",
		static_cast<UINT>(task.kind), task.commandList);
	return false;
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
