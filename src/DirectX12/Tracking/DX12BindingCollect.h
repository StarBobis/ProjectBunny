// Binding collect task queue for the deferred binding tracker.
// Split from DX12Tracking.h; keep declarations in the layer that owns them.
//
// Architecture: the queue consumes a "shadow command stream" that mirrors
// D3D12 recording. The recorder thread appends one compact delta entry per
// state mutation (SetPipelineState, SetGraphicsRootDescriptorTable, ...) and
// bundles the accumulated entries into a small packet at each draw/state
// event. The worker replays the packets in FIFO order into a per-command-list
// materialized state and builds BindingEvents from it. Command-list
// submission is itself a queue task, so the game thread never waits on the
// queue; the queue is bounded and overflows degrade to synchronous recording.
#pragma once

#include "../DX12Hooks.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

// Bounded queue depth for event tasks. Marker tasks (submit / unregister) are
// tiny and bypass the cap. With compact delta packets the worst-case footprint
// stays in the low megabytes; the old full-state tasks could reach gigabytes.
static constexpr size_t kMaxQueuedBindingTasks = 4096;

enum class DX12BindingCollectTaskKind
{
	StateEvent = 0,
	DrawEvent,
	DispatchEvent,
	OutputEvent,
	CommandListSubmit,      // worker moves the CL's recorded events into the ledger
	CommandListUnregister,  // worker drops its materialized state for the CL
};

// One recorded state mutation on a command list. Entries are appended by the
// CL's recorder thread (D3D12 command lists are single-threaded while
// recording, so the log is uncontended) and replayed in order by the worker.
// Payloads are plain data; entries that reference IA resources own one
// reference each (released via ReleaseDeltaIaRefs).
enum class DX12BindingDeltaKind : unsigned char
{
	GraphicsRootSignature = 0,
	ComputeRootSignature,
	DescriptorHeaps,
	PipelineState,
	PrimitiveTopology,
	GraphicsTable,
	ComputeTable,
	GraphicsRootDescriptor,
	ComputeRootDescriptor,
	GraphicsRootConstants,
	ComputeRootConstants,
	VertexBuffer,
	IndexBuffer,
	IaResolvedVertexBuffer,
	IaResolvedIndexBuffer,
	FullState,  // full snapshot, sent once per capture session / after reset
};

// Compact per-mutation record. The full-state snapshot is NOT part of this
// type (it would bloat every log entry to 60KB); FullState packets carry the
// snapshot on the task itself.
class DX12BindingDeltaEntry
{
public:
	// The payload union's variant members carry NSDMIs, which would make the
	// implicitly-declared default constructor deleted; initialize the first
	// variant explicitly so the entry stays trivially copyable for vector use.
	DX12BindingDeltaEntry()
		: kind(DX12BindingDeltaKind::PipelineState), index(0), count(0), offset(0), heaps{ nullptr, nullptr }
	{
	}

	DX12BindingDeltaKind kind = DX12BindingDeltaKind::PipelineState;
	UINT index = 0;   // root parameter index / vertex buffer slot
	UINT count = 0;   // constants: number of 32-bit values
	UINT offset = 0;  // constants: destination offset
	union
	{
		ID3D12RootSignature *rootSignature;
		ID3D12PipelineState *pipelineState;
		D3D12_PRIMITIVE_TOPOLOGY topology;
		struct
		{
			ID3D12DescriptorHeap *cbvSrvUavHeap;
			ID3D12DescriptorHeap *samplerHeap;
		} heaps;
		RootTableState table;
		RootDescriptorState descriptor;
		struct
		{
			UINT values[64];
		} constants;
		struct
		{
			D3D12_VERTEX_BUFFER_VIEW view;
			bool valid;
		} vertexBuffer;
		struct
		{
			D3D12_INDEX_BUFFER_VIEW view;
			bool valid;
		} indexBuffer;
		DX12BufferResourceSummary iaSummary;
	};
};

// Per-command-list recorder-side state. Owned by the CL's recording thread
// (the only writer), except creation/erasure which take the shared map lock.
// The log holds unsent delta entries; entries are drained into a packet at
// each event. The packet is only consumed on a successful enqueue: on
// overflow the log is restored so the next packet still carries the full
// delta (idempotent re-apply on the worker), and the event falls back to the
// synchronous path.
class DX12BindingRecorderState
{
public:
	UINT64 sessionId = 0;        // ledger session the log belongs to
	bool needsFullState = true;  // next event must send a full snapshot
	UINT32 vbDirtyMask = 0;      // VB slots changed since the last enqueue
	bool ibDirty = false;        // index buffer changed since the last enqueue
	D3D12_VERTEX_BUFFER_VIEW vbViews[MaxVertexBufferSlots] = {};
	bool vbValid[MaxVertexBufferSlots] = {};
	D3D12_INDEX_BUFFER_VIEW ibView = {};
	bool ibValid = false;
	std::vector<DX12BindingDeltaEntry> log;
};

class DX12BindingCollectTask
{
public:
	DX12BindingCollectTask() = default;

	DX12BindingCollectTaskKind kind = DX12BindingCollectTaskKind::StateEvent;
	UINT64 sequence = 0;
	UINT64 captureSessionId = 0;
	DWORD enqueueThreadId = 0;
	ID3D12GraphicsCommandList *commandList = nullptr;
	// Delta packet describing the state as of this event. Move-only; owns one
	// IA reference per IaResolved entry until the worker applies it.
	std::vector<DX12BindingDeltaEntry> delta;
	// Full-state snapshot for the first event of a capture session (or after
	// a reset). Heap-allocated so the deque never carries 60KB inline.
	std::unique_ptr<CommandListBindingState> fullState;
	UINT64 descriptorEpoch = 0;
	const char *eventKind = nullptr;
	UINT vertexCountPerInstance = 0;
	UINT indexCountPerInstance = 0;
	UINT instanceCount = 0;
	UINT startVertexLocation = 0;
	UINT startIndexLocation = 0;
	INT baseVertexLocation = 0;
	UINT startInstanceLocation = 0;
	UINT threadGroupCountX = 0;
	UINT threadGroupCountY = 0;
	UINT threadGroupCountZ = 0;
	UINT slot = 0;
	SIZE_T cpuHandle = 0;
	UINT64 drawId = 0;       // event draw serial (state no longer carries it)
	UINT64 dispatchId = 0;   // event dispatch serial
};

class DX12BindingCollectQueue
{
public:
	inline static std::mutex mutex;
	inline static std::condition_variable wake;
	inline static std::condition_variable idle;
	inline static std::deque<DX12BindingCollectTask> tasks;
	inline static std::thread worker;
	inline static std::atomic<bool> started = false;
	inline static std::atomic<bool> stopRequested = false;
	inline static std::atomic<uint64_t> nextSequence = 1;
	inline static std::atomic<uint64_t> enqueued = 0;
	inline static std::atomic<uint64_t> dequeued = 0;
	inline static std::atomic<uint64_t> completed = 0;
	inline static std::atomic<uint64_t> completedSequence = 0;
	inline static std::atomic<uint64_t> maxDepth = 0;
	inline static std::atomic<uint64_t> maxPendingAtDrain = 0;
	inline static std::atomic<uint64_t> drainCalls = 0;
	inline static std::atomic<uint64_t> drainWaitUs = 0;
	inline static std::atomic<uint64_t> drainMaxUs = 0;
	inline static std::atomic<uint64_t> taskElapsedUs = 0;
	inline static std::atomic<uint64_t> taskMaxUs = 0;
	inline static uint64_t activeTasks = 0;

	// Recorder-side per-command-list delta streams. Entries are owned by the
	// CL's recording thread; the map itself is guarded for create/erase.
	inline static std::mutex recorderMutex;
	inline static std::unordered_map<
		ID3D12GraphicsCommandList*,
		std::unique_ptr<DX12BindingRecorderState>> recorderStates;
	inline static std::atomic<uint64_t> recorderGeneration{ 0 };

	static void Start();
	static void Stop();
	static void DrainForSnapshot(const char *reason);

	// Delta-stream recording (producer side, called on the CL's recorder thread).
	// AppendDeltaEntry / RecordVertexBufferChange / RecordIndexBufferChange are
	// no-ops when binding-event recording is inactive, so the SetX hot paths
	// pay nothing outside capture sessions.
	static DX12BindingRecorderState *GetRecorderState(ID3D12GraphicsCommandList *commandList);
	static void ResetRecorderState(ID3D12GraphicsCommandList *commandList);
	static void EraseRecorderState(ID3D12GraphicsCommandList *commandList);
	static void AppendDeltaEntry(ID3D12GraphicsCommandList *commandList, const DX12BindingDeltaEntry &entry);
	static void RecordVertexBufferChange(ID3D12GraphicsCommandList *commandList, UINT slot, const D3D12_VERTEX_BUFFER_VIEW &view, bool valid);
	static void RecordIndexBufferChange(ID3D12GraphicsCommandList *commandList, const D3D12_INDEX_BUFFER_VIEW *view, bool valid);
	// Builds the delta packet (drains the recorder log, resolves pending IA
	// views). A full-state snapshot for the first event of a session is
	// returned through *fullState. Returns false when the event must be
	// dropped (no active session). On a failed enqueue the caller must call
	// RestoreEventPacket before falling back to synchronous recording.
	static bool BuildEventPacket(
		ID3D12GraphicsCommandList *commandList,
		std::vector<DX12BindingDeltaEntry> *packet,
		std::unique_ptr<CommandListBindingState> *fullState);
	static void RestoreEventPacket(ID3D12GraphicsCommandList *commandList, std::vector<DX12BindingDeltaEntry> *packet);

	// Event enqueue. *delta (and *fullState, when a full-state snapshot was
	// built) is moved into the task on success and left intact on failure
	// (the caller then processes the event synchronously).
	static bool EnqueueStateEvent(
		ID3D12GraphicsCommandList *commandList,
		std::vector<DX12BindingDeltaEntry> *delta,
		std::unique_ptr<CommandListBindingState> *fullState,
		const char *kind,
		UINT64 descriptorEpoch,
		UINT64 captureSessionId);
	static bool EnqueueDrawEvent(
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
		UINT startInstanceLocation);
	static bool EnqueueDispatchEvent(
		ID3D12GraphicsCommandList *commandList,
		std::vector<DX12BindingDeltaEntry> *delta,
		std::unique_ptr<CommandListBindingState> *fullState,
		UINT64 descriptorEpoch,
		UINT64 captureSessionId,
		UINT64 dispatchId,
		UINT threadGroupCountX,
		UINT threadGroupCountY,
		UINT threadGroupCountZ);
	// Marker tasks: never rejected, never wait.
	static void EnqueueSubmitCommandList(ID3D12GraphicsCommandList *commandList, UINT64 captureSessionId);
	static void EnqueueUnregisterCommandList(ID3D12GraphicsCommandList *commandList);

	static size_t PendingCount();
	static UINT64 LastSubmittedSequence();

	static bool EnqueueTask(
		DX12BindingCollectTask &&task,
		std::vector<DX12BindingDeltaEntry> *packet,
		std::unique_ptr<CommandListBindingState> *fullState);
	static void WorkerLoop();
	static void WaitForIdle(const char *reason);
	static void WaitForSubmittedSequence(UINT64 targetSequence, const char *reason);
	static void ProcessTask(DX12BindingCollectTask *task, const char *source);
	static void ProcessDeltaEventTask(DX12BindingCollectTask *task, const char *source);
	static void ProcessSubmitTask(DX12BindingCollectTask *task, const char *source);
	static void ProcessUnregisterTask(DX12BindingCollectTask *task, const char *source);
	static void ProcessStateEventTask(DX12BindingCollectTask *task, const char *source);
	static void ProcessDrawEventTask(DX12BindingCollectTask *task, const char *source);
	static void ProcessDispatchEventTask(DX12BindingCollectTask *task, const char *source);
	static void ProcessOutputEventTask(DX12BindingCollectTask *task, const char *source);
	static void UpdateMaxDepth(size_t depth);
	static void UpdateMaxPendingAtDrain(size_t depth);
	static void RecordTaskStats(int64_t elapsedUs);
	static void RecordDrainWaitStats(int64_t elapsedUs);
	static void LogStats(const char *event, uint64_t sequence, size_t depth);

	// Delta replay (worker side) and helpers.
	static std::unordered_map<ID3D12GraphicsCommandList*, CommandListBindingState> &WorkerMaterialized();
	static void ApplyDeltaToState(
		CommandListBindingState *state,
		const std::vector<DX12BindingDeltaEntry> &delta,
		const CommandListBindingState *fullState);
	static void ReleaseDeltaIaRefs(
		std::vector<DX12BindingDeltaEntry> *delta,
		std::unique_ptr<CommandListBindingState> *fullState);
	static void ResetGraphicsRootArguments(CommandListBindingState *state);
	static void ResetComputeRootArguments(CommandListBindingState *state);
	static void ApplyDescriptorHeapsChange(CommandListBindingState *state, ID3D12DescriptorHeap *cbvSrvUavHeap, ID3D12DescriptorHeap *samplerHeap);
	static CommandListBindingState &MaterializedStateFor(ID3D12GraphicsCommandList *commandList);
};
