#pragma once

#include <Windows.h>
#include <d3d12.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>

#include "DX12BindingTrackerState.hpp"

enum class DX12BindingCollectTaskKind
{
	StateEvent = 0,
	DrawEvent,
	DispatchEvent,
	OutputEvent,
};

class DX12BindingCollectTask
{
public:
	DX12BindingCollectTaskKind kind = DX12BindingCollectTaskKind::StateEvent;
	UINT64 sequence = 0;
	UINT64 captureSessionId = 0;
	DWORD enqueueThreadId = 0;
	ID3D12GraphicsCommandList *commandList = nullptr;
	CommandListBindingState state = {};
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
	UINT64 drawId = 0;
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

	static void Start();
	static void Stop();
	static void DrainForSnapshot(const char *reason);
	static void BeginFrameDrain();
	static bool EnqueueStateEvent(
		ID3D12GraphicsCommandList *commandList,
		const CommandListBindingState &state,
		const char *kind,
		UINT64 descriptorEpoch,
		UINT64 captureSessionId);
	static bool EnqueueDrawEvent(
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
		UINT startInstanceLocation);
	static bool EnqueueDispatchEvent(
		ID3D12GraphicsCommandList *commandList,
		const CommandListBindingState &state,
		UINT64 descriptorEpoch,
		UINT64 captureSessionId,
		UINT threadGroupCountX,
		UINT threadGroupCountY,
		UINT threadGroupCountZ);
	static bool EnqueueOutputEvent(
		ID3D12GraphicsCommandList *commandList,
		const CommandListBindingState &state,
		const char *kind,
		UINT slot,
		SIZE_T cpuHandle,
		UINT64 drawId,
		UINT64 captureSessionId);
	static size_t PendingCount();
	static UINT64 LastSubmittedSequence();

	static bool EnqueueTask(DX12BindingCollectTask &&task);
	static void WorkerLoop();
	static void WaitForIdle(const char *reason);
	static void WaitForSubmittedSequence(UINT64 targetSequence, const char *reason);
	static void ProcessTask(DX12BindingCollectTask *task, const char *source);
	static void ProcessStateEventTask(DX12BindingCollectTask *task, const char *source);
	static void ProcessDrawEventTask(DX12BindingCollectTask *task, const char *source);
	static void ProcessDispatchEventTask(DX12BindingCollectTask *task, const char *source);
	static void ProcessOutputEventTask(DX12BindingCollectTask *task, const char *source);
	static void UpdateMaxDepth(size_t depth);
	static void UpdateMaxPendingAtDrain(size_t depth);
	static void RecordTaskStats(int64_t elapsedUs);
	static void RecordDrainWaitStats(int64_t elapsedUs);
	static void LogStats(const char *event, uint64_t sequence, size_t depth);
};
