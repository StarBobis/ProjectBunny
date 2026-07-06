#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#include "DX12BindingTrackerTypes.hpp"
#include "DX12LogManager.hpp"
#include "DX12PreSkinPlan.hpp"

// Forward declare — DX12ModRuntimeState.hpp provides the full type.
class DX12PreSkinDispatchOverride;

// ---------------------------------------------------------------------------
// DX12PreSkinAsyncRequest — payload for async PreSkinProbe computation.
// The render thread populates this on cache miss and the worker thread
// consumes it to warm the match caches off the critical path.
// ---------------------------------------------------------------------------
class DX12PreSkinAsyncRequest
{
public:
	uint64_t computeShaderHash = 0;
	UINT64 computeBindingSerial = 0;
	std::vector<DX12CurrentComputeUavBinding> uavs;
	std::vector<DX12CurrentComputeUavBinding> srvs;
	std::vector<DX12CurrentComputeUavBinding> cbvs;
	std::vector<DX12CurrentRootConstants> rootConstants;

	DX12PreSkinAsyncRequest() = default;

	DX12PreSkinAsyncRequest(
		uint64_t csHash,
		UINT64 bindingSerial,
		std::vector<DX12CurrentComputeUavBinding> &&inUavs,
		std::vector<DX12CurrentComputeUavBinding> &&inSrvs,
		std::vector<DX12CurrentComputeUavBinding> &&inCbvs,
		std::vector<DX12CurrentRootConstants> &&inRootConstants)
		: computeShaderHash(csHash)
		, computeBindingSerial(bindingSerial)
		, uavs(std::move(inUavs))
		, srvs(std::move(inSrvs))
		, cbvs(std::move(inCbvs))
		, rootConstants(std::move(inRootConstants))
	{
	}
};

// ---------------------------------------------------------------------------
// DX12PreSkinAsyncQueue — single-producer, single-consumer queue with a
// dedicated worker thread that pre-computes PreSkinProbe match results.
// This eliminates 14-43ms blocking Dispatch calls on cache misses.
// ---------------------------------------------------------------------------
class DX12PreSkinAsyncQueue
{
public:
	static constexpr size_t kMaxQueueDepth = 256;

	std::deque<DX12PreSkinAsyncRequest> requests;
	std::mutex mutex;
	std::condition_variable wakeCondition;
	std::thread workerThread;
	std::atomic<bool> started{false};
	std::atomic<bool> stopRequested{false};
	std::atomic<UINT64> enqueued{0};
	std::atomic<UINT64> dequeued{0};
	std::atomic<UINT64> completed{0};
	std::atomic<UINT64> dropped{0};
	std::atomic<UINT64> maxDepth{0};

	void Start()
	{
		started.store(true);
	}

	void Stop()
	{
		started.store(false);
	}

	bool Enqueue(DX12PreSkinAsyncRequest &&request)
	{
		if (request.computeBindingSerial == 0 || request.computeShaderHash == 0)
			return false;

		Start();

		{
			std::lock_guard<std::mutex> lock(mutex);
			if (requests.size() >= kMaxQueueDepth) {
				// Drop oldest request to make room; the next cache miss will
				// re-enqueue this binding if it is still in use.
				requests.pop_front();
				dropped.fetch_add(1);
			}
			requests.push_back(std::move(request));
			UINT64 depth = static_cast<UINT64>(requests.size());
			UINT64 prevMax = maxDepth.load();
			while (depth > prevMax &&
				!maxDepth.compare_exchange_weak(prevMax, depth))
				;
		}
		enqueued.fetch_add(1);
		wakeCondition.notify_one();
		return true;
	}

private:
	void WorkerLoop()
	{
		DX12LogManager::JsonFunc("DX12PreSkinAsyncQueue",
			"\"event\":\"WorkerEnter\",\"threadId\":%lu",
			GetCurrentThreadId());

		for (;;) {
			DX12PreSkinAsyncRequest request;
			bool hasWork = false;
			{
				std::unique_lock<std::mutex> lock(mutex);
				wakeCondition.wait(lock, [this]() {
					return stopRequested.load() || !requests.empty();
				});
				if (requests.empty()) {
					if (stopRequested.load())
						break;
					continue;
				}
				request = std::move(requests.front());
				requests.pop_front();
				hasWork = true;
			}
			if (!hasWork)
				continue;

			dequeued.fetch_add(1);
			ProcessRequest(&request);
			completed.fetch_add(1);
		}

		DrainPending();
		DX12LogManager::JsonFunc("DX12PreSkinAsyncQueue",
			"\"event\":\"WorkerExit\",\"threadId\":%lu,\"pending\":%zu",
			GetCurrentThreadId(), requests.size());
	}

	void DrainPending()
	{
		for (;;) {
			DX12PreSkinAsyncRequest request;
			{
				std::lock_guard<std::mutex> lock(mutex);
				if (requests.empty())
					break;
				request = std::move(requests.front());
				requests.pop_front();
			}
			dequeued.fetch_add(1);
			ProcessRequest(&request);
			completed.fetch_add(1);
		}
	}

	static void ProcessRequest(DX12PreSkinAsyncRequest *request);

public:
	DX12PreSkinAsyncQueue(const DX12PreSkinAsyncQueue&) = delete;
	DX12PreSkinAsyncQueue &operator=(const DX12PreSkinAsyncQueue&) = delete;
	DX12PreSkinAsyncQueue() = default;
};

extern DX12PreSkinAsyncQueue gPreSkinAsyncQueue;
