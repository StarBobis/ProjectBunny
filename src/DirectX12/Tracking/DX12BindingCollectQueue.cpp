// DX12BindingCollectQueue: deferred binding-collect task queue.
// Split from DX12BindingTracking.cpp; implementation unit for its module.
//
// Shadow command stream: the recorder thread appends compact delta entries
// per state mutation and the worker replays them in FIFO order into a
// per-command-list materialized state. Command-list submission is a queue
// marker (no game-thread waits), and the queue is bounded: overflow degrades
// to synchronous recording in the producer instead of growing without limit.
#include "DX12Core.h"
#include "DX12Hooks.h"
#include "DX12Tracking.h"
#include "DX12Runtime.h"
#include "DX12Resources.h"
#include "DX12Presentation.h"
#include <algorithm>
#include <chrono>
#include <iterator>
#include <memory>
#include <stdio.h>
#include <string.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>




static constexpr uint64_t kBindingQueueStatsInterval = 4096;
static constexpr int64_t kBindingQueueSlowTaskUs = 1000;
static constexpr int64_t kBindingQueueSlowDrainUs = 1000;

// ---------------------------------------------------------------------------
// Recorder-side delta stream state (producer side).
//
// A D3D12 command list is single-threaded while recording, so the per-CL
// recorder state is touched by exactly one thread at a time; creation and
// erasure take the shared lock and a generation counter guards the TLS cache
// (same pattern as the binding shards).
// ---------------------------------------------------------------------------

namespace {

struct BindingRecorderTlsCache
{
	ID3D12GraphicsCommandList *commandList = nullptr;
	DX12BindingRecorderState *state = nullptr;
	UINT64 generation = 0;
};

BindingRecorderTlsCache &RecorderTls()
{
	static thread_local BindingRecorderTlsCache tls;
	return tls;
}

// Bounded in-flight event marker for the producer side.
bool BindingRecordingActive()
{
	return DX12HotPathState::NeedsBindingEventRecording(
		DX12HotPathState::HookWorkFlags);
}

void ResolvePendingIa(DX12BindingRecorderState *rec)
{
	if (!rec)
		return;
	if (rec->vbDirtyMask != 0) {
		for (UINT slot = 0; slot < MaxVertexBufferSlots; ++slot) {
			if ((rec->vbDirtyMask & (1u << slot)) == 0)
				continue;
			if (!rec->vbValid[slot])
				continue;
			DX12BufferResourceSummary summary;
			bool resolved = false;
			DX12BindingTracker::ResolveAndRetainIaResource(
				rec->vbViews[slot].BufferLocation,
				rec->vbViews[slot].SizeInBytes,
				&summary,
				&resolved);
			if (!resolved)
				continue;
			DX12BindingDeltaEntry entry;
			entry.kind = DX12BindingDeltaKind::IaResolvedVertexBuffer;
			entry.index = slot;
			entry.iaSummary = summary;
			rec->log.push_back(entry);
		}
	}
	if (rec->ibDirty) {
		if (rec->ibValid) {
			DX12BufferResourceSummary summary;
			bool resolved = false;
			DX12BindingTracker::ResolveAndRetainIaResource(
				rec->ibView.BufferLocation,
				rec->ibView.SizeInBytes,
				&summary,
				&resolved);
			if (resolved) {
				DX12BindingDeltaEntry entry;
				entry.kind = DX12BindingDeltaKind::IaResolvedIndexBuffer;
				entry.iaSummary = summary;
				rec->log.push_back(entry);
			}
		}
	}
}

} // namespace

// ---------------------------------------------------------------------------
// Recorder state management
// ---------------------------------------------------------------------------

DX12BindingRecorderState *DX12BindingCollectQueue::GetRecorderState(
	ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return nullptr;

	BindingRecorderTlsCache &tls = RecorderTls();
	if (tls.commandList == commandList && tls.state) {
		const UINT64 generation =
			recorderGeneration.load(std::memory_order_acquire);
		if (tls.generation == generation)
			return tls.state;
	}

	DX12BindingRecorderState *state = nullptr;
	{
		std::lock_guard<std::mutex> lock(recorderMutex);
		auto it = recorderStates.find(commandList);
		if (it == recorderStates.end()) {
			it = recorderStates.emplace(
				commandList,
				std::make_unique<DX12BindingRecorderState>()).first;
		}
		state = it->second.get();
	}
	tls.commandList = commandList;
	tls.state = state;
	tls.generation = recorderGeneration.load(std::memory_order_acquire);
	return state;
}

void DX12BindingCollectQueue::ResetRecorderState(
	ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return;
	// Command lists are reset every frame; only touch the recorder state when
	// recording is actually active so the hot path never allocates for it.
	if (!BindingRecordingActive())
		return;
	DX12BindingRecorderState *rec = GetRecorderState(commandList);
	if (!rec)
		return;
	ReleaseDeltaIaRefs(&rec->log, nullptr);
	rec->log.clear();
	rec->sessionId = 0;
	rec->needsFullState = true;
	rec->vbDirtyMask = 0;
	rec->ibDirty = false;
	memset(rec->vbViews, 0, sizeof(rec->vbViews));
	memset(rec->vbValid, 0, sizeof(rec->vbValid));
	rec->ibView = {};
	rec->ibValid = false;
}

void DX12BindingCollectQueue::EraseRecorderState(
	ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return;
	{
		std::lock_guard<std::mutex> lock(recorderMutex);
		auto it = recorderStates.find(commandList);
		if (it != recorderStates.end()) {
			ReleaseDeltaIaRefs(&it->second->log, nullptr);
			recorderStates.erase(it);
			recorderGeneration.fetch_add(1, std::memory_order_release);
		}
	}
	// Order the worker's materialized-state cleanup behind any in-flight
	// events for this CL.
	EnqueueUnregisterCommandList(commandList);
}

void DX12BindingCollectQueue::AppendDeltaEntry(
	ID3D12GraphicsCommandList *commandList,
	const DX12BindingDeltaEntry &entry)
{
	if (!commandList)
		return;
	if (!BindingRecordingActive())
		return;
	DX12BindingRecorderState *rec = GetRecorderState(commandList);
	if (!rec)
		return;
	rec->log.push_back(entry);
}

void DX12BindingCollectQueue::RecordVertexBufferChange(
	ID3D12GraphicsCommandList *commandList,
	UINT slot,
	const D3D12_VERTEX_BUFFER_VIEW &view,
	bool valid)
{
	if (!commandList || slot >= MaxVertexBufferSlots)
		return;
	if (!BindingRecordingActive())
		return;
	DX12BindingRecorderState *rec = GetRecorderState(commandList);
	if (!rec)
		return;
	rec->vbViews[slot] = view;
	rec->vbValid[slot] = valid;
	rec->vbDirtyMask |= (1u << slot);
	DX12BindingDeltaEntry entry;
	entry.kind = DX12BindingDeltaKind::VertexBuffer;
	entry.index = slot;
	entry.vertexBuffer.view = view;
	entry.vertexBuffer.valid = valid;
	rec->log.push_back(entry);
}

void DX12BindingCollectQueue::RecordIndexBufferChange(
	ID3D12GraphicsCommandList *commandList,
	const D3D12_INDEX_BUFFER_VIEW *view,
	bool valid)
{
	if (!commandList)
		return;
	if (!BindingRecordingActive())
		return;
	DX12BindingRecorderState *rec = GetRecorderState(commandList);
	if (!rec)
		return;
	rec->ibView = view ? *view : D3D12_INDEX_BUFFER_VIEW();
	rec->ibValid = valid;
	rec->ibDirty = true;
	DX12BindingDeltaEntry entry;
	entry.kind = DX12BindingDeltaKind::IndexBuffer;
	entry.indexBuffer.view = view ? *view : D3D12_INDEX_BUFFER_VIEW();
	entry.indexBuffer.valid = valid;
	rec->log.push_back(entry);
}

bool DX12BindingCollectQueue::BuildEventPacket(
	ID3D12GraphicsCommandList *commandList,
	std::vector<DX12BindingDeltaEntry> *packet,
	std::unique_ptr<CommandListBindingState> *fullState)
{
	if (!commandList || !packet || !fullState)
		return false;
	DX12BindingRecorderState *rec = GetRecorderState(commandList);
	if (!rec)
		return false;

	const UINT64 sessionId = DX12BindingTracker::CurrentCaptureSessionId();
	if (sessionId == 0)
		return false;

	if (rec->sessionId != sessionId) {
		// Capture-session boundary: the worker's materialized state for this
		// CL is missing or describes an older session, so the first event of
		// the new session must re-establish it from a full snapshot.
		ReleaseDeltaIaRefs(&rec->log, nullptr);
		rec->log.clear();
		rec->sessionId = sessionId;
		rec->needsFullState = true;
	}

	if (rec->needsFullState) {
		ReleaseDeltaIaRefs(&rec->log, nullptr);
		rec->log.clear();
		packet->clear();
		fullState->reset(new CommandListBindingState());
		if (!DX12BindingTracker::SnapshotBindingState(
				commandList, fullState->get()))
			return false;
		// Resolve deferred IA views on the snapshot copy so the worker's
		// materialized state starts with retained resources, mirroring what
		// the synchronous draw path resolves under the shard lock.
		DX12BindingTracker::ResolveDeferredIaResourcesForSnapshot(fullState->get());
		return true;
	}

	// Steady state: resolve pending IA views into the log, then drain the log
	// into the packet. The dirty flags stay set until the packet is accepted
	// so a failed enqueue re-sends the resolution with the next packet
	// (idempotent re-apply on the worker).
	ResolvePendingIa(rec);
	packet->clear();
	packet->swap(rec->log);
	return true;
}

void DX12BindingCollectQueue::RestoreEventPacket(
	ID3D12GraphicsCommandList *commandList,
	std::vector<DX12BindingDeltaEntry> *packet)
{
	if (!commandList || !packet)
		return;
	DX12BindingRecorderState *rec = GetRecorderState(commandList);
	if (!rec)
		return;
	// Put the entries back so the next event still carries the full delta.
	// The packet buffer holds the log's old (empty) buffer; capacities are
	// preserved on both sides.
	rec->log.swap(*packet);
}

// ---------------------------------------------------------------------------
// Delta replay (worker side)
// ---------------------------------------------------------------------------

void DX12BindingCollectQueue::ResetGraphicsRootArguments(
	CommandListBindingState *state)
{
	if (!state)
		return;
	for (UINT root = 0; root < MaxRootParameters; ++root) {
		state->graphicsTables[root] = RootTableState();
		state->graphicsRootDescriptors[root] = RootDescriptorState();
		state->graphicsRootConstants[root] = RootConstantsState();
	}
	state->graphicsTableValidCount = 0;
}

void DX12BindingCollectQueue::ResetComputeRootArguments(
	CommandListBindingState *state)
{
	if (!state)
		return;
	for (UINT root = 0; root < MaxRootParameters; ++root) {
		state->computeTables[root] = RootTableState();
		state->computeRootDescriptors[root] = RootDescriptorState();
		state->computeRootConstants[root] = RootConstantsState();
	}
	state->computeTableValidCount = 0;
}

void DX12BindingCollectQueue::ApplyDescriptorHeapsChange(
	CommandListBindingState *state,
	ID3D12DescriptorHeap *cbvSrvUavHeap,
	ID3D12DescriptorHeap *samplerHeap)
{
	if (!state)
		return;
	// Mirrors DX12BindingTracker::SetDescriptorHeaps exactly (shared helper so
	// the live state and the replayed state can never diverge).
	if (state->graphicsTableValidCount || state->computeTableValidCount) {
		for (UINT root = 0; root < MaxRootParameters; ++root) {
			if (state->graphicsTables[root].valid)
				state->graphicsTables[root] = RootTableState();
			if (state->computeTables[root].valid)
				state->computeTables[root] = RootTableState();
		}
		state->graphicsTableValidCount = 0;
		state->computeTableValidCount = 0;
	}
	state->cbvSrvUavHeap = cbvSrvUavHeap;
	state->samplerHeap = samplerHeap;
	state->runtimeCbvSrvUavHeapResolved = false;
	state->runtimeCbvSrvUavHeap = {};
	state->runtimeSamplerHeapResolved = false;
	state->runtimeSamplerHeap = {};
}

void DX12BindingCollectQueue::ApplyDeltaToState(
	CommandListBindingState *state,
	const std::vector<DX12BindingDeltaEntry> &delta,
	const CommandListBindingState *fullState)
{
	if (!state)
		return;
	if (fullState) {
		DX12BindingTracker::ReleaseRetainedIaResources(state);
		*state = *fullState;
		DX12BindingTracker::RetainStateIaResourcesForAsync(state);
	}
	for (const DX12BindingDeltaEntry &entry : delta) {
		switch (entry.kind) {
		case DX12BindingDeltaKind::GraphicsRootSignature:
			state->graphicsRootSignature = entry.rootSignature;
			ResetGraphicsRootArguments(state);
			break;
		case DX12BindingDeltaKind::ComputeRootSignature:
			state->computeRootSignature = entry.rootSignature;
			ResetComputeRootArguments(state);
			break;
		case DX12BindingDeltaKind::DescriptorHeaps:
			ApplyDescriptorHeapsChange(
				state, entry.heaps.cbvSrvUavHeap, entry.heaps.samplerHeap);
			break;
		case DX12BindingDeltaKind::PipelineState:
			state->pipelineState = entry.pipelineState;
			state->pipelineShaderInfo = {};
			state->pipelineShaderInfoValid = false;
			break;
		case DX12BindingDeltaKind::PrimitiveTopology:
			state->primitiveTopology = entry.topology;
			break;
		case DX12BindingDeltaKind::GraphicsTable:
			state->graphicsTables[entry.index] = entry.table;
			break;
		case DX12BindingDeltaKind::ComputeTable:
			state->computeTables[entry.index] = entry.table;
			break;
		case DX12BindingDeltaKind::GraphicsRootDescriptor:
			state->graphicsRootDescriptors[entry.index] = entry.descriptor;
			break;
		case DX12BindingDeltaKind::ComputeRootDescriptor:
			state->computeRootDescriptors[entry.index] = entry.descriptor;
			break;
		case DX12BindingDeltaKind::GraphicsRootConstants:
			if (entry.index < MaxRootParameters) {
				DX12BindingTracker::SetRootConstantsLocked(
					&state->graphicsRootConstants[entry.index],
					entry.index,
					entry.offset,
					entry.count,
					entry.constants.values);
			}
			break;
		case DX12BindingDeltaKind::ComputeRootConstants:
			if (entry.index < MaxRootParameters) {
				DX12BindingTracker::SetRootConstantsLocked(
					&state->computeRootConstants[entry.index],
					entry.index,
					entry.offset,
					entry.count,
					entry.constants.values);
			}
			break;
		case DX12BindingDeltaKind::VertexBuffer:
			if (entry.index < MaxVertexBufferSlots) {
				state->vertexBuffers[entry.index] = entry.vertexBuffer.view;
				state->vertexBufferValid[entry.index] = entry.vertexBuffer.valid;
				DX12BindingTracker::ReleaseResolvedIaResource(
					&state->vertexBufferResources[entry.index],
					&state->vertexBufferResolved[entry.index]);
			}
			break;
		case DX12BindingDeltaKind::IndexBuffer:
			state->indexBuffer = entry.indexBuffer.view;
			state->indexBufferValid = entry.indexBuffer.valid;
			DX12BindingTracker::ReleaseResolvedIaResource(
				&state->indexBufferResource,
				&state->indexBufferResolved);
			break;
		case DX12BindingDeltaKind::IaResolvedVertexBuffer:
			if (entry.index < MaxVertexBufferSlots &&
				entry.iaSummary.resource) {
				state->vertexBufferResources[entry.index] = entry.iaSummary;
				state->vertexBufferResolved[entry.index] = true;
				DX12RetainResource(entry.iaSummary.resource);
			}
			break;
		case DX12BindingDeltaKind::IaResolvedIndexBuffer:
			if (entry.iaSummary.resource) {
				state->indexBufferResource = entry.iaSummary;
				state->indexBufferResolved = true;
				DX12RetainResource(entry.iaSummary.resource);
			}
			break;
		default:
			break;
		}
	}
}

void DX12BindingCollectQueue::ReleaseDeltaIaRefs(
	std::vector<DX12BindingDeltaEntry> *delta,
	std::unique_ptr<CommandListBindingState> *fullState)
{
	if (delta) {
		for (DX12BindingDeltaEntry &entry : *delta) {
			switch (entry.kind) {
			case DX12BindingDeltaKind::IaResolvedVertexBuffer:
				if (entry.iaSummary.resource)
					DX12ReleaseRetainedResource(entry.iaSummary.resource);
				break;
			case DX12BindingDeltaKind::IaResolvedIndexBuffer:
				if (entry.iaSummary.resource)
					DX12ReleaseRetainedResource(entry.iaSummary.resource);
				break;
			default:
				break;
			}
		}
	}
	if (fullState && *fullState)
		DX12BindingTracker::ReleaseRetainedIaResources(fullState->get());
}

// ---------------------------------------------------------------------------
// Queue lifecycle
// ---------------------------------------------------------------------------

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
	DX12_LOG("DX12BindingCollectQueue",
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

	// Drop worker-owned materialized state; IA references are released here
	// instead of leaking at process exit.
	for (auto &entry : WorkerMaterialized())
		DX12BindingTracker::ReleaseRetainedIaResources(&entry.second);
	WorkerMaterialized().clear();
}

void DX12BindingCollectQueue::DrainForSnapshot(const char *reason)
{
	WaitForIdle(reason ? reason : "Snapshot");
}

// ---------------------------------------------------------------------------
// Enqueue side
// ---------------------------------------------------------------------------

bool DX12BindingCollectQueue::EnqueueStateEvent(
	ID3D12GraphicsCommandList *commandList,
	std::vector<DX12BindingDeltaEntry> *delta,
	std::unique_ptr<CommandListBindingState> *fullState,
	const char *kind,
	UINT64 descriptorEpoch,
	UINT64 captureSessionId)
{
	DX12BindingCollectTask task;
	task.kind = DX12BindingCollectTaskKind::StateEvent;
	task.commandList = commandList;
	task.eventKind = kind;
	task.descriptorEpoch = descriptorEpoch;
	task.captureSessionId = captureSessionId;
	DX12_PROF_COUNT("Rm.BindingEventsRecorded");
	DX12_PROF_COUNT("Rm.DescriptorEpochWrites");
	return EnqueueTask(std::move(task), delta, fullState);
}

bool DX12BindingCollectQueue::EnqueueDrawEvent(
	ID3D12GraphicsCommandList *commandList,
	std::vector<DX12BindingDeltaEntry> *delta,
	std::unique_ptr<CommandListBindingState> *fullState,
	const char *kind,
	UINT64 descriptorEpoch,
	UINT64 captureSessionId,
	UINT64 drawId,
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
	task.eventKind = kind;
	task.descriptorEpoch = descriptorEpoch;
	task.captureSessionId = captureSessionId;
	task.drawId = drawId;
	task.vertexCountPerInstance = vertexCountPerInstance;
	task.indexCountPerInstance = indexCountPerInstance;
	task.instanceCount = instanceCount;
	task.startVertexLocation = startVertexLocation;
	task.startIndexLocation = startIndexLocation;
	task.baseVertexLocation = baseVertexLocation;
	task.startInstanceLocation = startInstanceLocation;
	DX12_PROF_COUNT("Rm.BindingEventsRecorded");
	DX12_PROF_COUNT("Rm.DescriptorEpochWrites");
	return EnqueueTask(std::move(task), delta, fullState);
}

bool DX12BindingCollectQueue::EnqueueDispatchEvent(
	ID3D12GraphicsCommandList *commandList,
	std::vector<DX12BindingDeltaEntry> *delta,
	std::unique_ptr<CommandListBindingState> *fullState,
	UINT64 descriptorEpoch,
	UINT64 captureSessionId,
	UINT64 dispatchId,
	UINT threadGroupCountX,
	UINT threadGroupCountY,
	UINT threadGroupCountZ)
{
	DX12BindingCollectTask task;
	task.kind = DX12BindingCollectTaskKind::DispatchEvent;
	task.commandList = commandList;
	task.eventKind = "dispatch";
	task.descriptorEpoch = descriptorEpoch;
	task.captureSessionId = captureSessionId;
	task.dispatchId = dispatchId;
	task.threadGroupCountX = threadGroupCountX;
	task.threadGroupCountY = threadGroupCountY;
	task.threadGroupCountZ = threadGroupCountZ;
	DX12_PROF_COUNT("Rm.BindingEventsRecorded");
	DX12_PROF_COUNT("Rm.DescriptorEpochWrites");
	return EnqueueTask(std::move(task), delta, fullState);
}

void DX12BindingCollectQueue::EnqueueSubmitCommandList(
	ID3D12GraphicsCommandList *commandList,
	UINT64 captureSessionId)
{
	if (!commandList || captureSessionId == 0)
		return;
	DX12BindingCollectTask task;
	task.kind = DX12BindingCollectTaskKind::CommandListSubmit;
	task.commandList = commandList;
	task.captureSessionId = captureSessionId;
	EnqueueTask(std::move(task), nullptr, nullptr);
}

void DX12BindingCollectQueue::EnqueueUnregisterCommandList(
	ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return;
	DX12BindingCollectTask task;
	task.kind = DX12BindingCollectTaskKind::CommandListUnregister;
	task.commandList = commandList;
	EnqueueTask(std::move(task), nullptr, nullptr);
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

bool DX12BindingCollectQueue::EnqueueTask(
	DX12BindingCollectTask &&task,
	std::vector<DX12BindingDeltaEntry> *packet,
	std::unique_ptr<CommandListBindingState> *fullState)
{
	if (!task.commandList)
		return false;
	if (!started.load(std::memory_order_acquire))
		return false;

	task.enqueueThreadId = GetCurrentThreadId();
	task.sequence = nextSequence.fetch_add(1, std::memory_order_relaxed);
	const UINT64 sequence = task.sequence;

	const bool isMarker =
		task.kind == DX12BindingCollectTaskKind::CommandListSubmit ||
		task.kind == DX12BindingCollectTaskKind::CommandListUnregister;
	size_t depth = 0;
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (!started.load(std::memory_order_acquire) ||
			stopRequested.load(std::memory_order_acquire)) {
			return false;
		}
		// Bounded queue: marker tasks are tiny and always accepted. Event
		// tasks overflow to synchronous recording in the caller, which is the
		// backpressure the producer applies when the worker cannot keep up:
		// one bounded unit of inline work instead of an unbounded wait.
		if (!isMarker &&
			tasks.size() + static_cast<size_t>(activeTasks) >=
				kMaxQueuedBindingTasks) {
			return false;
		}
		if (packet)
			task.delta = std::move(*packet);
		if (fullState)
			task.fullState = std::move(*fullState);
		tasks.push_back(std::move(task));
		depth = tasks.size() + static_cast<size_t>(activeTasks);
	}

	if (packet && !isMarker) {
		// The packet carrying the pending IA resolution reached the worker, so
		// the recorder's dirty flags are safe to clear; a full-state packet
		// also re-established the worker's materialized state. Same thread
		// that built the packet, so the TLS recorder cache is warm.
		DX12BindingRecorderState *rec = GetRecorderState(task.commandList);
		if (rec) {
			rec->vbDirtyMask = 0;
			rec->ibDirty = false;
			rec->needsFullState = false;
		}
	}

	enqueued.fetch_add(1, std::memory_order_relaxed);
	UpdateMaxDepth(depth);
	LogStats("Enqueue", sequence, depth);
	wake.notify_one();
	return true;
}

// ---------------------------------------------------------------------------
// Worker side
// ---------------------------------------------------------------------------

std::unordered_map<ID3D12GraphicsCommandList*, CommandListBindingState> &
DX12BindingCollectQueue::WorkerMaterialized()
{
	// Worker thread only; no synchronization required.
	static std::unordered_map<ID3D12GraphicsCommandList*, CommandListBindingState> state;
	return state;
}

CommandListBindingState &DX12BindingCollectQueue::MaterializedStateFor(
	ID3D12GraphicsCommandList *commandList)
{
	return WorkerMaterialized()[commandList];
}

void DX12BindingCollectQueue::WorkerLoop()
{
	DX12_LOG("DX12BindingCollectQueue",
		"event=WorkerEnter,threadId=%lu",
		GetCurrentThreadId());

	for (;;) {
		DX12BindingCollectTask task;
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
	DX12_LOG("DX12BindingCollectQueue",
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
	DX12_PROF_ADD_US_ITEMS(
		"Worker.wait.drain",
		static_cast<LONG64>(elapsedUs),
		static_cast<LONG64>(pendingSnapshot));
	drainCalls.fetch_add(1, std::memory_order_relaxed);
	RecordDrainWaitStats(elapsedUs);
	DX12_PROF_ADD_US("BindingQueueDrain", static_cast<LONG64>(elapsedUs));
	if (elapsedUs >= kBindingQueueSlowDrainUs) {
		DX12_LOG("DX12BindingCollectQueueDrain",
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
	DX12_PROF_ADD_US_ITEMS(
		"Worker.wait.drain",
		static_cast<LONG64>(elapsedUs),
		static_cast<LONG64>(pendingSnapshot));
	drainCalls.fetch_add(1, std::memory_order_relaxed);
	RecordDrainWaitStats(elapsedUs);
	DX12_PROF_ADD_US("BindingQueueDrain", static_cast<LONG64>(elapsedUs));
	if (elapsedUs >= kBindingQueueSlowDrainUs) {
		DX12_LOG("DX12BindingCollectQueueDrain",
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

	const auto begin = std::chrono::steady_clock::now();
	switch (task->kind) {
	case DX12BindingCollectTaskKind::StateEvent:
	case DX12BindingCollectTaskKind::DrawEvent:
	case DX12BindingCollectTaskKind::DispatchEvent:
		ProcessDeltaEventTask(task, source);
		break;
	case DX12BindingCollectTaskKind::OutputEvent:
		ProcessOutputEventTask(task, source);
		break;
	case DX12BindingCollectTaskKind::CommandListSubmit:
		ProcessSubmitTask(task, source);
		break;
	case DX12BindingCollectTaskKind::CommandListUnregister:
		ProcessUnregisterTask(task, source);
		break;
	default:
		break;
	}
	// The delta packet owns one IA reference per IaResolved entry and the
	// full-state snapshot owns its IA references until applied; the
	// materialized state takes its own references during replay, so the
	// task's copies are released here.
	ReleaseDeltaIaRefs(&task->delta, &task->fullState);
	const auto end = std::chrono::steady_clock::now();
	const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count();
	DX12_PROF_ADD_US_ITEMS(
		"Worker.task",
		static_cast<LONG64>(elapsedUs),
		1);
	RecordTaskStats(elapsedUs);
	DX12_PROF_ADD_US("BindingQueueTask", static_cast<LONG64>(elapsedUs));
	if (elapsedUs >= kBindingQueueSlowTaskUs) {
		DX12_LOG("DX12BindingCollectQueueSlowTask",
			"source=%s,seq=%llu,captureSessionId=%llu,kind=%u,elapsedUs=%lld,pending=%zu",
			source ? source : "",
			static_cast<unsigned long long>(task->sequence),
			static_cast<unsigned long long>(task->captureSessionId),
			static_cast<unsigned>(task->kind),
			static_cast<long long>(elapsedUs),
			PendingCount());
	}
}

void DX12BindingCollectQueue::ProcessDeltaEventTask(
	DX12BindingCollectTask *task,
	const char *source)
{
	(void)source;
	if (!task || !task->commandList)
		return;

	CommandListBindingState &state = MaterializedStateFor(task->commandList);
	ApplyDeltaToState(&state, task->delta, task->fullState.get());
	// The materialized state's shader info is resolved lazily here instead of
	// on the recording thread (the old enqueue-side on-demand resolve).
	if (state.pipelineState && !state.pipelineShaderInfoValid)
		DX12BindingTracker::ResolvePipelineShaderInfoOnDemand(&state);

	BindingEvent event;
	DX12_DIAG_QPC(buildBegin);
	if (!DX12BindingTracker::BuildEvent(
			&event,
			task->eventKind ? task->eventKind : "state",
			task->commandList,
			state,
			task->descriptorEpoch,
			task->captureSessionId))
		return;
	DX12_PROF_CPU_STAGE(
		"BindingCollectQueue",
		"build_delta_event",
		static_cast<UINT>(task->kind),
		1,
		DX12_DIAG_ELAPSED_US(buildBegin));

	// The materialized state carries no per-draw serials; the task does.
	event.drawId = task->drawId;
	event.dispatchId = task->dispatchId;
	if (task->kind == DX12BindingCollectTaskKind::DrawEvent) {
		event.vertexCountPerInstance = task->vertexCountPerInstance;
		event.indexCountPerInstance = task->indexCountPerInstance;
		event.instanceCount = task->instanceCount;
		event.startVertexLocation = task->startVertexLocation;
		event.startIndexLocation = task->startIndexLocation;
		event.baseVertexLocation = task->baseVertexLocation;
		event.startInstanceLocation = task->startInstanceLocation;
	} else if (task->kind == DX12BindingCollectTaskKind::DispatchEvent) {
		event.threadGroupCountX = task->threadGroupCountX;
		event.threadGroupCountY = task->threadGroupCountY;
		event.threadGroupCountZ = task->threadGroupCountZ;
	}

	DX12_DIAG_QPC(storeBegin);
	DX12BindingTracker::StoreEventLocked(event);
	DX12_PROF_CPU_STAGE(
		"BindingCollectQueue",
		"store_delta_event",
		static_cast<UINT>(task->kind),
		1,
		DX12_DIAG_ELAPSED_US(storeBegin));

	if (task->kind == DX12BindingCollectTaskKind::DrawEvent) {
		DX12_DIAG_QPC(outputBegin);
		DX12BindingTracker::StoreCurrentOutputTargetsLocked(
			task->commandList,
			state,
			task->captureSessionId,
			task->drawId);
		DX12_PROF_CPU_STAGE(
			"BindingCollectQueue",
			"store_draw_outputs",
			static_cast<UINT>(task->kind),
			1,
			DX12_DIAG_ELAPSED_US(outputBegin));
	}
}

void DX12BindingCollectQueue::ProcessSubmitTask(
	DX12BindingCollectTask *task,
	const char *source)
{
	(void)source;
	if (!task || !task->commandList)
		return;
	if (task->captureSessionId == 0)
		return;

	std::vector<CommandListRecordedEvent> events;
	{
		BindingShard &shard = DX12BindingTrackerState::ShardFor(task->commandList);
		AcquireSRWLockExclusive(&shard.lock);
		auto it = shard.recordedEvents.find(task->commandList);
		if (it != shard.recordedEvents.end()) {
			std::vector<CommandListRecordedEvent> &recorded = it->second;
			// Move only the events recorded in this task's session: a lagged
			// submit must never pull a newer session's events into an older
			// ledger, and a session that already ended leaves the events for
			// the ledger's own session check to reject.
			auto split = std::stable_partition(
				recorded.begin(),
				recorded.end(),
				[task](const CommandListRecordedEvent &event) {
					const UINT64 eventSession =
						event.kind == CommandListRecordedEventKind::Binding ?
							event.binding.captureSessionId :
							event.output.captureSessionId;
					return eventSession != task->captureSessionId;
				});
			events.assign(
				std::make_move_iterator(split),
				std::make_move_iterator(recorded.end()));
			recorded.erase(split, recorded.end());
		}
		ReleaseSRWLockExclusive(&shard.lock);
	}

	for (const CommandListRecordedEvent &event : events)
		DX12BindingTracker::SubmitRecordedEvent(event, task->captureSessionId);
	for (CommandListRecordedEvent &event : events)
		DX12BindingTracker::ReleaseRecordedEvent(&event);

	DX12FrameAnalysis::LogJsonFunc("BindingSubmitCommandList",
		"\"commandList\":\"%p\",\"captureSessionId\":%llu,\"templates\":%zu",
		task->commandList,
		static_cast<unsigned long long>(task->captureSessionId),
		events.size());
}

void DX12BindingCollectQueue::ProcessUnregisterTask(
	DX12BindingCollectTask *task,
	const char *source)
{
	(void)source;
	if (!task || !task->commandList)
		return;
	std::unordered_map<ID3D12GraphicsCommandList*, CommandListBindingState> &materialized =
		WorkerMaterialized();
	auto it = materialized.find(task->commandList);
	if (it != materialized.end()) {
		DX12BindingTracker::ReleaseRetainedIaResources(&it->second);
		materialized.erase(it);
	}
}

void DX12BindingCollectQueue::ProcessStateEventTask(DX12BindingCollectTask *task, const char *source)
{
	// Folded into ProcessDeltaEventTask; kept as a forwarding stub so the
	// header stays a stable call surface.
	ProcessDeltaEventTask(task, source);
}

void DX12BindingCollectQueue::ProcessDrawEventTask(DX12BindingCollectTask *task, const char *source)
{
	// Folded into ProcessDeltaEventTask; kept as a forwarding stub so the
	// header stays a stable call surface.
	ProcessDeltaEventTask(task, source);
}

void DX12BindingCollectQueue::ProcessDispatchEventTask(DX12BindingCollectTask *task, const char *source)
{
	// Folded into ProcessDeltaEventTask; kept as a forwarding stub so the
	// header stays a stable call surface.
	ProcessDeltaEventTask(task, source);
}

void DX12BindingCollectQueue::ProcessOutputEventTask(DX12BindingCollectTask *task, const char *source)
{
	(void)source;
	if (!task || !task->cpuHandle)
		return;

	// No current enqueuer produces OutputEvent tasks; materialize from the
	// worker's per-CL state to keep the path consistent if one is added.
	CommandListBindingState &state = MaterializedStateFor(task->commandList);
	DX12_DIAG_QPC(storeBegin);
	DX12BindingTracker::StoreOutputEventLocked(
		task->eventKind ? task->eventKind : "",
		task->commandList,
		state,
		task->slot,
		task->cpuHandle,
		task->drawId,
		task->captureSessionId);
	DX12_PROF_CPU_STAGE(
		"BindingCollectQueue",
		"store_output_event",
		static_cast<UINT>(task->kind),
		1,
		DX12_DIAG_ELAPSED_US(storeBegin));
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

	DX12_LOG("DX12BindingCollectQueueStats",
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
