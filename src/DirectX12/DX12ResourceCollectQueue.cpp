#include "DX12ResourceCollectQueue.hpp"

#include <chrono>

#include "DX12LogManager.hpp"
#include "DX12ResourceTrackerHookRuntime.hpp"
#include "DX12ResourceTrackerPrivate.hpp"

DX12ResourceCollectQueue &DX12ResourceCollectQueue::Get()
{
	static DX12ResourceCollectQueue queue;
	return queue;
}

void DX12ResourceCollectQueue::Start()
{
	Get().StartWorker();
}

void DX12ResourceCollectQueue::Stop()
{
	Get().StopWorker();
}

bool DX12ResourceCollectQueue::Enqueue(
	ID3D12Resource *resource,
	const D3D12_RESOURCE_DESC *desc,
	const D3D12_HEAP_PROPERTIES *heapProperties,
	D3D12_RESOURCE_STATES initialState)
{
	return Get().EnqueueTask(resource, desc, heapProperties, initialState);
}

void DX12ResourceCollectQueue::DrainForSnapshot(const char *reason)
{
	Get().DrainPendingOnCaller(reason);
}

size_t DX12ResourceCollectQueue::PendingCount()
{
	DX12ResourceCollectQueue &queue = Get();
	std::lock_guard<std::mutex> lock(queue.mutex);
	return queue.tasks.size();
}

void DX12ResourceCollectQueue::StartWorker()
{
	bool expected = false;
	if (!started.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
		return;

	stopRequested.store(false, std::memory_order_release);
	worker = std::thread([this]() { WorkerLoop(); });
	DX12LogManager::JsonFunc("DX12ResourceCollectQueue",
		"\"event\":\"Start\",\"thread\":\"%p\"",
		worker.native_handle());
}

void DX12ResourceCollectQueue::StopWorker()
{
	if (!started.load(std::memory_order_acquire))
		return;

	DX12LogManager::JsonFunc("DX12ResourceCollectQueue",
		"\"event\":\"StopRequested\",\"pending\":%zu,\"enqueued\":%llu,\"dequeued\":%llu,\"completed\":%llu,\"dropped\":%llu,\"addRefFailed\":%llu,\"maxDepth\":%llu",
		PendingCount(),
		static_cast<unsigned long long>(enqueued.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(dequeued.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(completed.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(dropped.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(addRefFailed.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(maxDepth.load(std::memory_order_relaxed)));

	stopRequested.store(true, std::memory_order_release);
	wake.notify_all();
	if (worker.joinable())
		worker.join();
	started.store(false, std::memory_order_release);

	DX12LogManager::JsonFunc("DX12ResourceCollectQueue",
		"\"event\":\"Stopped\",\"pending\":%zu,\"enqueued\":%llu,\"dequeued\":%llu,\"completed\":%llu,\"dropped\":%llu,\"addRefFailed\":%llu,\"maxDepth\":%llu",
		PendingCount(),
		static_cast<unsigned long long>(enqueued.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(dequeued.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(completed.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(dropped.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(addRefFailed.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(maxDepth.load(std::memory_order_relaxed)));
}

bool DX12ResourceCollectQueue::EnqueueTask(
	ID3D12Resource *resource,
	const D3D12_RESOURCE_DESC *desc,
	const D3D12_HEAP_PROPERTIES *heapProperties,
	D3D12_RESOURCE_STATES initialState)
{
	if (!resource)
		return false;

	StartWorker();

	DX12ResourceCollectTask task;
	task.resource = resource;
	task.hasDesc = desc != nullptr;
	if (desc)
		task.desc = *desc;
	task.hasHeapProperties = heapProperties != nullptr;
	if (heapProperties)
		task.heapProperties = *heapProperties;
	task.initialState = initialState;
	task.sequence = nextSequence.fetch_add(1, std::memory_order_relaxed);
	task.enqueueThreadId = GetCurrentThreadId();

	if (!DX12ResourceSnapshotUtils::TryAddRef(resource)) {
		addRefFailed.fetch_add(1, std::memory_order_relaxed);
		DX12LogManager::JsonFunc("DX12ResourceCollectQueue",
			"\"event\":\"EnqueueAddRefFailed\",\"seq\":%llu,\"resource\":\"%p\"",
			static_cast<unsigned long long>(task.sequence), resource);
		return false;
	}

	size_t depth = 0;
	{
		std::lock_guard<std::mutex> lock(mutex);
		tasks.push_back(task);
		depth = tasks.size();
	}
	enqueued.fetch_add(1, std::memory_order_relaxed);
	UpdateMaxDepth(depth);
	LogStats("Enqueue", task.sequence, depth);
	wake.notify_one();
	return true;
}

void DX12ResourceCollectQueue::WorkerLoop()
{
	DX12LogManager::JsonFunc("DX12ResourceCollectQueue",
		"\"event\":\"WorkerEnter\",\"threadId\":%lu",
		GetCurrentThreadId());

	for (;;) {
		DX12ResourceCollectTask task;
		size_t depthAfterPop = 0;
		{
			std::unique_lock<std::mutex> lock(mutex);
			wake.wait(lock, [this]() {
				return stopRequested.load(std::memory_order_acquire) || !tasks.empty();
			});
			if (tasks.empty()) {
				if (stopRequested.load(std::memory_order_acquire))
					break;
				continue;
			}
			task = tasks.front();
			tasks.pop_front();
			depthAfterPop = tasks.size();
		}
		dequeued.fetch_add(1, std::memory_order_relaxed);
		LogStats("Dequeue", task.sequence, depthAfterPop);
		ProcessTask(&task, "Worker");
	}

	DrainPendingOnCaller("WorkerStop");
	DX12LogManager::JsonFunc("DX12ResourceCollectQueue",
		"\"event\":\"WorkerExit\",\"threadId\":%lu,\"pending\":%zu",
		GetCurrentThreadId(), PendingCount());
}

void DX12ResourceCollectQueue::DrainPendingOnCaller(const char *reason)
{
	for (;;) {
		DX12ResourceCollectTask task;
		size_t depthAfterPop = 0;
		{
			std::lock_guard<std::mutex> lock(mutex);
			if (tasks.empty())
				break;
			task = tasks.front();
			tasks.pop_front();
			depthAfterPop = tasks.size();
		}
		dequeued.fetch_add(1, std::memory_order_relaxed);
		DX12LogManager::JsonFunc("DX12ResourceCollectQueue",
			"\"event\":\"DrainPop\",\"reason\":\"%s\",\"seq\":%llu,\"pending\":%zu",
			reason ? reason : "",
			static_cast<unsigned long long>(task.sequence),
			depthAfterPop);
		ProcessTask(&task, reason ? reason : "Drain");
	}
}

void DX12ResourceCollectQueue::ProcessTask(DX12ResourceCollectTask *task, const char *source)
{
	if (!task || !task->resource)
		return;

	const auto begin = std::chrono::steady_clock::now();
	DX12LogManager::JsonFunc("DX12ResourceCollectQueue",
		"\"event\":\"ProcessBegin\",\"source\":\"%s\",\"seq\":%llu,\"resource\":\"%p\",\"hasDesc\":%u,\"hasHeap\":%u,\"initialState\":%u,\"enqueueThreadId\":%lu,\"workerThreadId\":%lu",
		source ? source : "",
		static_cast<unsigned long long>(task->sequence),
		task->resource,
		task->hasDesc ? 1 : 0,
		task->hasHeapProperties ? 1 : 0,
		static_cast<UINT>(task->initialState),
		task->enqueueThreadId,
		GetCurrentThreadId());

	RecordResource(
		task->resource,
		task->hasDesc ? &task->desc : nullptr,
		task->hasHeapProperties ? &task->heapProperties : nullptr,
		task->initialState);
	DX12ResourceTrackerHookRuntime::AttachResourceReleaseTracker(task->resource);

	const auto end = std::chrono::steady_clock::now();
	const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count();
	completed.fetch_add(1, std::memory_order_relaxed);
	DX12LogManager::JsonFunc("DX12ResourceCollectQueue",
		"\"event\":\"ProcessComplete\",\"source\":\"%s\",\"seq\":%llu,\"resource\":\"%p\",\"elapsedUs\":%lld,\"completed\":%llu",
		source ? source : "",
		static_cast<unsigned long long>(task->sequence),
		task->resource,
		static_cast<long long>(elapsedUs),
		static_cast<unsigned long long>(completed.load(std::memory_order_relaxed)));
	ReleaseTaskResource(task);
}

void DX12ResourceCollectQueue::ReleaseTaskResource(DX12ResourceCollectTask *task)
{
	if (!task || !task->resource)
		return;
	DX12ResourceSnapshotUtils::Release(task->resource);
	task->resource = nullptr;
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
	DX12LogManager::JsonFunc("DX12ResourceCollectQueue",
		"\"event\":\"%s\",\"seq\":%llu,\"pending\":%zu,\"enqueued\":%llu,\"dequeued\":%llu,\"completed\":%llu,\"maxDepth\":%llu",
		event ? event : "",
		static_cast<unsigned long long>(sequence),
		depth,
		static_cast<unsigned long long>(enqueued.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(dequeued.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(completed.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(maxDepth.load(std::memory_order_relaxed)));
}
