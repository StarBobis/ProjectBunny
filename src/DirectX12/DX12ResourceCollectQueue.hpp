#pragma once

#include <Windows.h>
#include <d3d12.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>

class DX12ResourceCollectTask
{
public:
	ID3D12Resource *resource = nullptr;
	D3D12_RESOURCE_DESC desc = {};
	bool hasDesc = false;
	D3D12_HEAP_PROPERTIES heapProperties = {};
	bool hasHeapProperties = false;
	D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON;
	uint64_t sequence = 0;
	DWORD enqueueThreadId = 0;
};

class DX12ResourceCollectQueue
{
public:
	std::mutex mutex;
	std::condition_variable wake;
	std::deque<DX12ResourceCollectTask> tasks;
	std::thread worker;
	std::atomic<bool> started{false};
	std::atomic<bool> stopRequested{false};
	std::atomic<uint64_t> nextSequence{1};
	std::atomic<uint64_t> enqueued{0};
	std::atomic<uint64_t> dequeued{0};
	std::atomic<uint64_t> completed{0};
	std::atomic<uint64_t> dropped{0};
	std::atomic<uint64_t> addRefFailed{0};
	std::atomic<uint64_t> maxDepth{0};

	static DX12ResourceCollectQueue &Get();
	static void Start();
	static void Stop();
	static bool Enqueue(
		ID3D12Resource *resource,
		const D3D12_RESOURCE_DESC *desc,
		const D3D12_HEAP_PROPERTIES *heapProperties,
		D3D12_RESOURCE_STATES initialState);
	static void DrainForSnapshot(const char *reason);
	static size_t PendingCount();

	void StartWorker();
	void StopWorker();
	bool EnqueueTask(
		ID3D12Resource *resource,
		const D3D12_RESOURCE_DESC *desc,
		const D3D12_HEAP_PROPERTIES *heapProperties,
		D3D12_RESOURCE_STATES initialState);
	void WorkerLoop();
	void DrainPendingOnCaller(const char *reason);
	void ProcessTask(DX12ResourceCollectTask *task, const char *source);
	void ReleaseTaskResource(DX12ResourceCollectTask *task);
	void UpdateMaxDepth(size_t depth);
	void LogStats(const char *event, uint64_t sequence, size_t depth);
};
