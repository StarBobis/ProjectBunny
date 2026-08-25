// DX12AsyncReadback: deferred GPU readback queue for hash/content capture.
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



// ---------------------------------------------------------------------------
// Async content capture via copy-queue readback (DX12AsyncContentReadback).
//
// Replaces the DX11-style synchronous CPU content shadow:
//   - Unmap of tracked UPLOAD buffers used to memcpy the written range into
//     the content cache on the recording thread (plus a global epoch bump and
//     lock cycling). Recording threads now only enqueue a tiny capture event.
//   - CopyBufferRegion / CopyResource / CopyTextureRegion bookkeeping used to
//     propagate/invalidate cache content synchronously. The copy paths now
//     enqueue events; the actual bytes are read back from the GPU after the
//     game's submit and published by a worker thread.
//
// Pipeline: recording thread enqueues events -> the game's
// ExecuteCommandLists wrapper submits a batch on the mod-owned COPY queue
// (ordered after the game queue via Signal/Wait) -> a worker thread waits on
// the batch fence, maps the readback ring and publishes into the existing
// content caches (DX12CacheBufferContentRange / DX12CacheTextureHash).
// Draw-time consumers therefore observe content one frame late, which is
// exact for static resources and self-corrects for streaming ones.
//
// Included by DX12ResourceTracking.cpp after DX12ResourceTrackingCore.inl /
// DX12ResourceHooks.inl / DX12ResourceQueries.inl (single TU).
// ---------------------------------------------------------------------------

namespace
{
constexpr UINT64 kReadbackAlign = 256;
constexpr UINT64 kReadbackEventMaxBytes = 64ull * 1024 * 1024;   // 64MB per event
constexpr UINT64 kReadbackSlotInitialBytes = 8ull * 1024 * 1024; // 8MB
constexpr UINT64 kReadbackSlotMaxBytes = 64ull * 1024 * 1024;    // 64MB per slot
constexpr UINT kReadbackSlotCount = 4;
constexpr UINT kReadbackListPoolSize = 4;
constexpr UINT kReadbackPendingEventCap = 4096;
constexpr UINT kReadbackSubmitEventCap = 512;

enum class DX12ReadbackEventKind
{
	Buffer,
	Texture,
};

struct DX12ReadbackEvent
{
	DX12ReadbackEventKind kind = DX12ReadbackEventKind::Buffer;
	ID3D12Resource *resource = nullptr; // AddRef'd at enqueue, released at publish
	UINT64 offset = 0;                  // Buffer: byte offset in the resource
	UINT64 size = 0;                    // Buffer: byte count
	D3D12_RESOURCE_DESC desc = {};      // Texture: resource desc for hashing
	// Filled at submit time:
	UINT64 ringOffset = 0;
	UINT64 ringPitch = 0;   // Texture: destination row pitch in the ring (aligned)
	UINT rowCount = 0;      // Texture
	UINT64 rowBytes = 0;    // Texture
};

struct DX12ReadbackRingSlot
{
	ID3D12Resource *buffer = nullptr;
	UINT64 capacity = 0;
	UINT64 busyUntilFence = 0; // 0 = free
};

struct DX12ReadbackBatch
{
	ID3D12CommandAllocator *allocator = nullptr;
	ID3D12GraphicsCommandList *list = nullptr;
	ID3D12Resource *ring = nullptr; // AddRef'd for this batch
	UINT64 ringUsed = 0;
	UINT64 fenceValue = 0;
	HANDLE event = nullptr;
	std::vector<DX12ReadbackEvent> events;
};

struct DX12ReadbackGameFence
{
	ID3D12CommandQueue *queue = nullptr;
	ID3D12Fence *fence = nullptr;
	UINT64 value = 0;
};

SRWLOCK gReadbackEventLock = SRWLOCK_INIT;
std::vector<DX12ReadbackEvent> gReadbackPendingEvents;
std::atomic<UINT> gReadbackPendingCount{0};

SRWLOCK gReadbackChannelLock = SRWLOCK_INIT;
ID3D12Device *gReadbackDevice = nullptr;
ID3D12CommandQueue *gReadbackQueue = nullptr; // COPY queue, mod-owned
ID3D12Fence *gReadbackFence = nullptr;
UINT64 gReadbackFenceValue = 0;
std::vector<DX12ReadbackRingSlot> gReadbackSlots;
std::vector<DX12ReadbackBatch*> gReadbackPendingBatches;
std::vector<ID3D12CommandAllocator*> gReadbackAllocatorPool;
std::vector<ID3D12GraphicsCommandList*> gReadbackListPool;
std::vector<DX12ReadbackGameFence> gReadbackGameFences;

SRWLOCK gReadbackWorkerLock = SRWLOCK_INIT;
HANDLE gReadbackWorker = nullptr;
HANDLE gReadbackWorkerEvent = nullptr;
volatile LONG gReadbackStopRequested = 0;
volatile LONG gReadbackChannelFaulted = 0;

void DX12ReadbackReleaseEvents(std::vector<DX12ReadbackEvent> &events)
{
	for (DX12ReadbackEvent &event : events) {
		if (event.resource)
			event.resource->Release();
	}
	events.clear();
}

bool DX12ReadbackEnsureChannelLocked(ID3D12Device *device)
{
	if (InterlockedCompareExchange(&gReadbackChannelFaulted, 0, 0) != 0)
		return false;
	if (gReadbackQueue && gReadbackFence && gReadbackDevice)
		return true;
	if (!device)
		return false;
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	ID3D12CommandQueue *queue = nullptr;
	HRESULT hr = device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue));
	if (FAILED(hr) || !queue)
		return false;
	ID3D12Fence *fence = nullptr;
	hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
	if (FAILED(hr) || !fence) {
		queue->Release();
		return false;
	}
	device->AddRef();
	gReadbackDevice = device;
	gReadbackQueue = queue;
	gReadbackFence = fence;
	gReadbackFenceValue = 0;
	return true;
}

bool DX12ReadbackAcquireListLocked(
	ID3D12CommandAllocator **allocatorOut,
	ID3D12GraphicsCommandList **listOut)
{
	if (!allocatorOut || !listOut)
		return false;
	*allocatorOut = nullptr;
	*listOut = nullptr;
	if (!gReadbackDevice || !gReadbackQueue)
		return false;

	ID3D12CommandAllocator *allocator = nullptr;
	ID3D12GraphicsCommandList *list = nullptr;
	bool fromPool = false;
	if (!gReadbackAllocatorPool.empty() && !gReadbackListPool.empty()) {
		allocator = gReadbackAllocatorPool.back();
		gReadbackAllocatorPool.pop_back();
		list = gReadbackListPool.back();
		gReadbackListPool.pop_back();
		fromPool = true;
	} else {
		HRESULT hr = gReadbackDevice->CreateCommandAllocator(
			D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(&allocator));
		if (FAILED(hr) || !allocator)
			return false;
		hr = gReadbackDevice->CreateCommandList(
			0, D3D12_COMMAND_LIST_TYPE_COPY, allocator,
			nullptr, IID_PPV_ARGS(&list));
		if (FAILED(hr) || !list) {
			allocator->Release();
			return false;
		}
	}
	// Pooled allocators are only returned after their fence passed, so Reset
	// is safe here. Freshly created allocator/list pairs must NOT be reset:
	// the new list is already in the recording state and Reset returns E_FAIL
	// on strict drivers (same trap as the upload channel).
	if (fromPool &&
	    (FAILED(allocator->Reset()) || FAILED(list->Reset(allocator, nullptr)))) {
		allocator->Release();
		list->Release();
		return false;
	}
	*allocatorOut = allocator;
	*listOut = list;
	return true;
}

void DX12ReadbackReleaseListLocked(
	ID3D12CommandAllocator *allocator,
	ID3D12GraphicsCommandList *list)
{
	if (!allocator || !list)
		return;
	if (gReadbackAllocatorPool.size() < kReadbackListPoolSize &&
	    gReadbackListPool.size() < kReadbackListPoolSize) {
		gReadbackAllocatorPool.push_back(allocator);
		gReadbackListPool.push_back(list);
	} else {
		allocator->Release();
		list->Release();
	}
}

DX12ReadbackRingSlot *DX12ReadbackAcquireRingSlotLocked(UINT64 requiredBytes)
{
	if (requiredBytes == 0 || requiredBytes > kReadbackSlotMaxBytes)
		return nullptr;
	const UINT64 completedValue = gReadbackFence ?
		gReadbackFence->GetCompletedValue() : 0;

	DX12ReadbackRingSlot *freeSlot = nullptr;
	for (DX12ReadbackRingSlot &slot : gReadbackSlots) {
		if (slot.busyUntilFence == 0 ||
		    (slot.busyUntilFence != UINT64_MAX && completedValue >= slot.busyUntilFence)) {
			freeSlot = &slot;
			break;
		}
	}
	if (!freeSlot) {
		if (gReadbackSlots.size() >= kReadbackSlotCount)
			return nullptr;
		gReadbackSlots.push_back(DX12ReadbackRingSlot());
		freeSlot = &gReadbackSlots.back();
	}
	if (freeSlot->capacity < requiredBytes) {
		D3D12_RESOURCE_DESC desc = {};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		const UINT64 alignedSize =
			(requiredBytes + 65535u) & ~static_cast<UINT64>(65535u);
		desc.Width = alignedSize;
		desc.Height = 1;
		desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.Format = DXGI_FORMAT_UNKNOWN;
		desc.SampleDesc.Count = 1;
		desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		D3D12_HEAP_PROPERTIES heapProps = {};
		heapProps.Type = D3D12_HEAP_TYPE_READBACK;
		ID3D12Resource *buffer = nullptr;
		HRESULT hr = gReadbackDevice->CreateCommittedResource(
			&heapProps, D3D12_HEAP_FLAG_NONE, &desc,
			D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&buffer));
		if (FAILED(hr) || !buffer)
			return nullptr;
		if (freeSlot->buffer)
			freeSlot->buffer->Release();
		freeSlot->buffer = buffer;
		freeSlot->capacity = alignedSize;
	}
	freeSlot->busyUntilFence = 0;
	return freeSlot;
}

DX12ReadbackGameFence *DX12ReadbackEnsureGameFenceLocked(ID3D12CommandQueue *gameQueue)
{
	if (!gameQueue || !gReadbackDevice)
		return nullptr;
	for (DX12ReadbackGameFence &entry : gReadbackGameFences) {
		if (entry.queue == gameQueue)
			return &entry;
	}
	ID3D12Fence *fence = nullptr;
	HRESULT hr = gReadbackDevice->CreateFence(
		0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
	if (FAILED(hr) || !fence)
		return nullptr;
	gameQueue->AddRef();
	DX12ReadbackGameFence entry;
	entry.queue = gameQueue;
	entry.fence = fence;
	entry.value = 0;
	gReadbackGameFences.push_back(entry);
	return &gReadbackGameFences.back();
}

bool DX12ReadbackRecordBufferCopyLocked(
	ID3D12GraphicsCommandList *list,
	const DX12ReadbackEvent &event,
	ID3D12Resource *ring)
{
	if (!list || !ring || !event.resource)
		return false;

	// UPLOAD heaps are CPU-visible and always GPU-readable (GENERIC_READ
	// includes COPY_SOURCE); no barrier needed. For other heaps transition to
	// COPY_SOURCE using the tracked state and restore it afterwards. When the
	// state is unknown, skip the capture (the consumer falls back to
	// metadata-based hashing or a miss) rather than risking an invalid
	// transition.
	D3D12_HEAP_PROPERTIES heapProps = {};
	D3D12_HEAP_FLAGS heapFlags = {};
	if (SUCCEEDED(event.resource->GetHeapProperties(&heapProps, &heapFlags)) &&
	    heapProps.Type == D3D12_HEAP_TYPE_UPLOAD) {
		list->CopyBufferRegion(
			ring, event.ringOffset,
			event.resource, event.offset,
			static_cast<UINT64>(event.size));
		return true;
	}

	D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
	bool known = false;
	bool barrierTracked = false;
	DX12GetResourceCurrentState(event.resource, &state, &known, &barrierTracked);
	if (!known)
		return false;

	D3D12_RESOURCE_BARRIER toCopy = {};
	toCopy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	toCopy.Transition.pResource = event.resource;
	toCopy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	toCopy.Transition.StateBefore = state;
	toCopy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
	list->ResourceBarrier(1, &toCopy);
	list->CopyBufferRegion(
		ring, event.ringOffset,
		event.resource, event.offset,
		static_cast<UINT64>(event.size));
	D3D12_RESOURCE_BARRIER restore = {};
	restore.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	restore.Transition.pResource = event.resource;
	restore.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	restore.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
	restore.Transition.StateAfter = state;
	list->ResourceBarrier(1, &restore);
	return true;
}

bool DX12ReadbackRecordTextureCopyLocked(
	ID3D12GraphicsCommandList *list,
	DX12ReadbackEvent *event,
	ID3D12Resource *ring)
{
	if (!list || !event || !ring || !event->resource)
		return false;
	if (event->desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
	    event->desc.SampleDesc.Count > 1)
		return false;

	// The texture may have been resized/recreated since the event was
	// enqueued (dynamic-resolution games resize textures per frame); the
	// enqueued desc would then describe a stale footprint and the copy
	// would record out-of-bounds reads, failing Close on the driver.
	// Re-validate against the live desc and skip when it changed.
	const D3D12_RESOURCE_DESC liveDesc = event->resource->GetDesc();
	if (liveDesc.Dimension != event->desc.Dimension ||
	    liveDesc.Width != event->desc.Width ||
	    liveDesc.Height != event->desc.Height ||
	    liveDesc.DepthOrArraySize != event->desc.DepthOrArraySize ||
	    liveDesc.MipLevels != event->desc.MipLevels ||
	    liveDesc.Format != event->desc.Format ||
	    liveDesc.SampleDesc.Count != event->desc.SampleDesc.Count) {
		return false;
	}

	D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
	bool known = false;
	bool barrierTracked = false;
	DX12GetResourceCurrentState(event->resource, &state, &known, &barrierTracked);
	if (!known)
		return false;

	D3D12_RESOURCE_BARRIER toCopy = {};
	toCopy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	toCopy.Transition.pResource = event->resource;
	toCopy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	toCopy.Transition.StateBefore = state;
	toCopy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
	list->ResourceBarrier(1, &toCopy);

	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
	UINT rowCounts = 0;
	UINT64 rowSizes = 0;
	UINT64 totalBytes = 0;
	gReadbackDevice->GetCopyableFootprints(
		&event->desc, 0, 1, 0,
		&footprint, &rowCounts, &rowSizes, &totalBytes);
	// Tight destination pitch keeps the ring layout dense; the hash only
	// reads rowBytes per row, so the pitch does not affect hash values.
	footprint.Footprint.RowPitch = static_cast<UINT>(event->ringPitch);
	footprint.Offset = event->ringOffset;
	D3D12_TEXTURE_COPY_LOCATION dst = {};
	dst.pResource = ring;
	dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	dst.PlacedFootprint = footprint;
	D3D12_TEXTURE_COPY_LOCATION src = {};
	src.pResource = event->resource;
	src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	src.SubresourceIndex = 0;
	list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

	D3D12_RESOURCE_BARRIER restore = {};
	restore.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	restore.Transition.pResource = event->resource;
	restore.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	restore.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
	restore.Transition.StateAfter = state;
	list->ResourceBarrier(1, &restore);
	return true;
}

// Publish one completed batch into the content caches (worker thread; no
// readback channel locks held).
void DX12ReadbackPublishBatch(DX12ReadbackBatch *batch)
{
	if (!batch || !batch->ring || batch->events.empty())
		return;
	void *mapped = nullptr;
	D3D12_RANGE readRange = { 0, static_cast<SIZE_T>(batch->ringUsed) };
	if (FAILED(batch->ring->Map(0, &readRange, &mapped)) || !mapped)
		return;
	const uint8_t *base = static_cast<const uint8_t*>(mapped);
	UINT publishedBuffers = 0;
	UINT publishedTextures = 0;
	for (DX12ReadbackEvent &event : batch->events) {
		if (!event.resource)
			continue;
		if (event.kind == DX12ReadbackEventKind::Buffer) {
			DX12CacheBufferContentRange(
				event.resource,
				event.offset,
				base + event.ringOffset,
				static_cast<size_t>(event.size));
			++publishedBuffers;
		} else {
			uint32_t hash = 0;
			if (DX12TextureHashUtils::Texture2DTopMipHashFromRows(
					event.desc,
					base + event.ringOffset,
					event.ringPitch,
					event.rowBytes,
					event.rowCount,
					&hash) &&
			    hash) {
				DX12CacheTextureHash(
					event.resource, hash, &event.desc, "async_readback");
				++publishedTextures;
			}
		}
	}
	batch->ring->Unmap(0, nullptr);
	{
		static volatile LONG publishLogCounter = 0;
		if ((InterlockedIncrement(&publishLogCounter) & 0x1F) == 1) {
			DX12LogManager::Fields("DX12ReadbackPublish",
				"stage=published,buffers=%u,textures=%u,events=%zu",
				publishedBuffers, publishedTextures, batch->events.size());
		}
	}
}

DWORD WINAPI DX12ReadbackWorkerProc(LPVOID)
{
	while (InterlockedCompareExchange(&gReadbackStopRequested, 0, 0) == 0) {
		std::vector<DX12ReadbackBatch*> completed;
		DX12ReadbackBatch *oldest = nullptr;
		{
			AcquireSRWLockExclusive(&gReadbackChannelLock);
			for (auto it = gReadbackPendingBatches.begin();
			     it != gReadbackPendingBatches.end();) {
				DX12ReadbackBatch *batch = *it;
				const UINT64 completedValue = gReadbackFence ?
					gReadbackFence->GetCompletedValue() : 0;
				if (completedValue >= batch->fenceValue) {
					completed.push_back(batch);
					it = gReadbackPendingBatches.erase(it);
				} else {
					if (!oldest || batch->fenceValue < oldest->fenceValue)
						oldest = batch;
					++it;
				}
			}
			ReleaseSRWLockExclusive(&gReadbackChannelLock);
		}

		for (DX12ReadbackBatch *batch : completed) {
			DX12ReadbackPublishBatch(batch);
			DX12ReadbackReleaseEvents(batch->events);
			{
				AcquireSRWLockExclusive(&gReadbackChannelLock);
				DX12ReadbackReleaseListLocked(batch->allocator, batch->list);
				for (DX12ReadbackRingSlot &slot : gReadbackSlots) {
					if (slot.buffer == batch->ring &&
					    slot.busyUntilFence == batch->fenceValue) {
						slot.busyUntilFence = 0;
						break;
					}
				}
				ReleaseSRWLockExclusive(&gReadbackChannelLock);
			}
			if (batch->ring)
				batch->ring->Release();
			if (batch->event)
				CloseHandle(batch->event);
			delete batch;
		}

		if (oldest) {
			if (oldest->event && gReadbackWorkerEvent) {
				HANDLE handles[] = { oldest->event, gReadbackWorkerEvent };
				WaitForMultipleObjects(2, handles, FALSE, INFINITE);
			} else if (gReadbackWorkerEvent) {
				WaitForSingleObject(gReadbackWorkerEvent, 5);
			}
		} else if (gReadbackWorkerEvent) {
			WaitForSingleObject(gReadbackWorkerEvent, INFINITE);
		} else {
			Sleep(1);
		}
	}
	return 0;
}

void DX12ReadbackEnsureWorker()
{
	if (InterlockedCompareExchange(&gReadbackStopRequested, 0, 0) != 0)
		return;
	if (gReadbackWorker)
		return;
	AcquireSRWLockExclusive(&gReadbackWorkerLock);
	if (!gReadbackWorker) {
		gReadbackWorkerEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		if (gReadbackWorkerEvent) {
			gReadbackWorker = CreateThread(
				nullptr, 0, DX12ReadbackWorkerProc, nullptr, 0, nullptr);
			if (!gReadbackWorker) {
				CloseHandle(gReadbackWorkerEvent);
				gReadbackWorkerEvent = nullptr;
			}
		}
	}
	ReleaseSRWLockExclusive(&gReadbackWorkerLock);
}
} // namespace

void DX12AsyncReadbackShutdown()
{
	InterlockedExchange(&gReadbackStopRequested, 1);
	AcquireSRWLockExclusive(&gReadbackWorkerLock);
	HANDLE worker = gReadbackWorker;
	HANDLE eventHandle = gReadbackWorkerEvent;
	ReleaseSRWLockExclusive(&gReadbackWorkerLock);
	if (eventHandle)
		SetEvent(eventHandle);
	if (worker)
		WaitForSingleObject(worker, 5000);
	AcquireSRWLockExclusive(&gReadbackWorkerLock);
	if (gReadbackWorker) {
		CloseHandle(gReadbackWorker);
		gReadbackWorker = nullptr;
	}
	if (gReadbackWorkerEvent) {
		CloseHandle(gReadbackWorkerEvent);
		gReadbackWorkerEvent = nullptr;
	}
	ReleaseSRWLockExclusive(&gReadbackWorkerLock);
}

bool DX12AsyncReadbackQueueBufferCapture(
	ID3D12Resource *resource, UINT64 offset, UINT64 size)
{
	if (!resource || !size)
		return false;
	if (offset > kReadbackEventMaxBytes || size > kReadbackEventMaxBytes)
		return false;

	DX12ReadbackEvent event;
	event.kind = DX12ReadbackEventKind::Buffer;
	event.resource = resource;
	event.offset = offset;
	event.size = size;
	resource->AddRef();

	AcquireSRWLockExclusive(&gReadbackEventLock);
	if (gReadbackPendingEvents.size() >= kReadbackPendingEventCap) {
		ReleaseSRWLockExclusive(&gReadbackEventLock);
		resource->Release();
		return false;
	}
	gReadbackPendingEvents.push_back(event);
	ReleaseSRWLockExclusive(&gReadbackEventLock);
	gReadbackPendingCount.fetch_add(1, std::memory_order_relaxed);
	{
		static volatile LONG enqueueLogCounter = 0;
		if ((InterlockedIncrement(&enqueueLogCounter) & 0xFFF) == 1) {
			DX12LogManager::Fields("DX12ReadbackEnqueue",
				"stage=enqueued,kind=buffer,resource=%p,offset=%llu,bytes=%llu",
				resource,
				static_cast<unsigned long long>(offset),
				static_cast<unsigned long long>(size));
		}
	}
	return true;
}

bool DX12AsyncReadbackQueueTextureCapture(
	ID3D12Resource *texture, const D3D12_RESOURCE_DESC *desc)
{
	if (!texture || !desc)
		return false;
	if (desc->Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D)
		return false;
	if (desc->SampleDesc.Count > 1)
		return false;
	const UINT rowCount = DX12TextureHashUtils::Texture2DTopMipRowCount(
		desc->Format, desc->Height);
	const UINT64 rowBytes = DX12TextureHashUtils::Texture2DTopMipRowBytes(
		desc->Format, static_cast<UINT>(desc->Width));
	if (!rowCount || !rowBytes)
		return false;
	const UINT64 rowPitch =
		(rowBytes + kReadbackAlign - 1) & ~(kReadbackAlign - 1);
	if (rowCount > UINT64_MAX / rowPitch)
		return false;
	const UINT64 totalBytes = static_cast<UINT64>(rowCount) * rowPitch;
	if (totalBytes > kReadbackEventMaxBytes)
		return false;

	DX12ReadbackEvent event;
	event.kind = DX12ReadbackEventKind::Texture;
	event.resource = texture;
	event.desc = *desc;
	event.ringPitch = rowPitch;
	event.rowCount = rowCount;
	event.rowBytes = rowBytes;
	texture->AddRef();

	AcquireSRWLockExclusive(&gReadbackEventLock);
	if (gReadbackPendingEvents.size() >= kReadbackPendingEventCap) {
		ReleaseSRWLockExclusive(&gReadbackEventLock);
		texture->Release();
		return false;
	}
	gReadbackPendingEvents.push_back(event);
	ReleaseSRWLockExclusive(&gReadbackEventLock);
	gReadbackPendingCount.fetch_add(1, std::memory_order_relaxed);
	{
		static volatile LONG textureEnqueueLogCounter = 0;
		if ((InterlockedIncrement(&textureEnqueueLogCounter) & 0xFFF) == 1) {
			DX12LogManager::Fields("DX12ReadbackEnqueue",
				"stage=enqueued,kind=texture,resource=%p,width=%llu,height=%u,"
				"format=%u,bytes=%llu,flags=0x%llx",
				texture,
				static_cast<unsigned long long>(desc->Width),
				desc->Height,
				static_cast<UINT>(desc->Format),
				static_cast<unsigned long long>(totalBytes),
				static_cast<unsigned long long>(
					DX12HotPathState::HookWorkFlags.load(std::memory_order_relaxed)));
		}
	}
	return true;
}

bool DX12AsyncReadbackHasPendingWork()
{
	return gReadbackPendingCount.load(std::memory_order_acquire) != 0;
}

// Rate-limited submit-failure diagnostics. The failure path can run on every
// ExecuteCommandLists (e.g. a persistent channel fault), and an unthrottled
// log line per submit blew the log up to 500MB+ in testing.
static void DX12ReadbackLogSubmitFailure(const char *stage, UINT64 a, UINT64 b)
{
	static volatile LONG failureLogCounter = 0;
	if ((InterlockedIncrement(&failureLogCounter) & 0xFF) != 1)
		return;
	DX12LogManager::Fields("DX12ReadbackSubmit",
		"stage=%s,a=%llu,b=%llu",
		stage ? stage : "",
		static_cast<unsigned long long>(a),
		static_cast<unsigned long long>(b));
}

void DX12AsyncReadbackSubmitPending(ID3D12CommandQueue *gameQueue)
{
	const LARGE_INTEGER submitBegin = DX12OuterTimingProbeUtils::Now();
	if (!gameQueue || gReadbackPendingCount.load(std::memory_order_acquire) == 0)
		return;

	std::vector<DX12ReadbackEvent> events;
	{
		AcquireSRWLockExclusive(&gReadbackEventLock);
		if (gReadbackPendingEvents.empty()) {
			ReleaseSRWLockExclusive(&gReadbackEventLock);
			return;
		}
		events.swap(gReadbackPendingEvents);
		ReleaseSRWLockExclusive(&gReadbackEventLock);
	}
	// Subtract only the events we swapped out: captures queued concurrently by
	// other recording threads after the swap must keep the pending count
	// nonzero or their events would never be submitted.
	const UINT swappedCount = static_cast<UINT>(events.size());
	if (swappedCount != 0)
		gReadbackPendingCount.fetch_sub(swappedCount, std::memory_order_relaxed);
	if (events.empty())
		return;

	// Plan the detached batch before taking the channel lock. This is CPU-only
	// work and must not contend with allocator/ring maintenance or the worker.
	UINT64 ringUsed = 0;
	UINT submittedCount = 0;
	std::vector<DX12ReadbackEvent> kept;
	kept.reserve(events.size());
	for (DX12ReadbackEvent &event : events) {
		UINT64 bytes = event.kind == DX12ReadbackEventKind::Buffer ?
			(event.size + kReadbackAlign - 1) & ~(kReadbackAlign - 1) :
			event.rowCount * event.ringPitch;
		if (bytes == 0 || bytes > kReadbackSlotMaxBytes ||
		    ringUsed + bytes > kReadbackSlotMaxBytes ||
		    submittedCount >= kReadbackSubmitEventCap) {
			if (event.resource)
				event.resource->Release();
			continue;
		}
		event.ringOffset = ringUsed;
		ringUsed += bytes;
		++submittedCount;
		kept.push_back(event);
	}
	if (kept.empty()) {
		DX12ReadbackLogSubmitFailure("failed_all_oversized", events.size(), 0);
		return;
	}
	events.clear();

	ID3D12Device *device = nullptr;
	if (FAILED(gameQueue->GetDevice(IID_PPV_ARGS(&device))) || !device) {
		DX12ReadbackLogSubmitFailure("failed_get_device", kept.size(), 0);
		DX12ReadbackReleaseEvents(kept);
		return;
	}

	AcquireSRWLockExclusive(&gReadbackChannelLock);
	if (!DX12ReadbackEnsureChannelLocked(device)) {
		ReleaseSRWLockExclusive(&gReadbackChannelLock);
		device->Release();
		DX12ReadbackLogSubmitFailure("failed_channel", kept.size(), 0);
		DX12ReadbackReleaseEvents(kept);
		return;
	}

	ID3D12CommandAllocator *allocator = nullptr;
	ID3D12GraphicsCommandList *list = nullptr;
	if (!DX12ReadbackAcquireListLocked(&allocator, &list)) {
		ReleaseSRWLockExclusive(&gReadbackChannelLock);
		device->Release();
		DX12ReadbackLogSubmitFailure("failed_list", kept.size(), 0);
		DX12ReadbackReleaseEvents(kept);
		return;
	}

	DX12ReadbackRingSlot *slot = DX12ReadbackAcquireRingSlotLocked(ringUsed);
	if (!slot) {
		DX12ReadbackLogSubmitFailure("failed_ring_slot", ringUsed, 0);
		// The list is open (recorded, never closed); release directly so a
		// half-recorded list never reaches the reuse pool.
		allocator->Release();
		list->Release();
		DX12ReadbackReleaseEvents(kept);
		ReleaseSRWLockExclusive(&gReadbackChannelLock);
		device->Release();
		return;
	}

	for (DX12ReadbackEvent &event : kept) {
		bool recorded = false;
		if (event.kind == DX12ReadbackEventKind::Buffer) {
			recorded = DX12ReadbackRecordBufferCopyLocked(list, event, slot->buffer);
		} else {
			recorded = DX12ReadbackRecordTextureCopyLocked(list, &event, slot->buffer);
		}
		if (!recorded) {
			if (event.resource)
				event.resource->Release();
			event.resource = nullptr;
		}
	}
	// Drop events that were skipped at record time (unknown state etc.).
	UINT skippedRecord = 0;
	for (auto it = kept.begin(); it != kept.end();) {
		if (!it->resource) {
			++skippedRecord;
			it = kept.erase(it);
		} else {
			++it;
		}
	}
	if (skippedRecord != 0) {
		static volatile LONG skippedLogCounter = 0;
		if ((InterlockedIncrement(&skippedLogCounter) & 0xFF) == 1) {
			DX12LogManager::Fields("DX12ReadbackSubmit",
				"stage=skipped_state_unknown,count=%u,kept=%zu",
				skippedRecord, kept.size());
		}
	}
	const HRESULT closeHr = kept.empty() ? S_OK : list->Close();
	if (kept.empty() || FAILED(closeHr)) {
		DX12ReadbackLogSubmitFailure(
			kept.empty() ? "failed_all_state_unknown" : "failed_close",
			kept.size(), static_cast<UINT64>(closeHr));
		// The list may still be open; never return it to the pool (a pooled
		// list must be closed so its allocator can be Reset).
		allocator->Release();
		list->Release();
		DX12ReadbackReleaseEvents(kept);
		ReleaseSRWLockExclusive(&gReadbackChannelLock);
		device->Release();
		return;
	}

	DX12ReadbackGameFence *gameFence = DX12ReadbackEnsureGameFenceLocked(gameQueue);
	if (!gameFence) {
		DX12ReadbackLogSubmitFailure("failed_game_fence", kept.size(), 0);
		allocator->Release();
		list->Release();
		DX12ReadbackReleaseEvents(kept);
		ReleaseSRWLockExclusive(&gReadbackChannelLock);
		device->Release();
		return;
	}

	// Order the readback after all work the game queue has submitted so far.
	const UINT64 gameFenceValue = gameFence->value + 1;
	HRESULT submitHr = gameQueue->Signal(gameFence->fence, gameFenceValue);
	if (SUCCEEDED(submitHr))
		submitHr = gReadbackQueue->Wait(gameFence->fence, gameFenceValue);
	if (FAILED(submitHr)) {
		InterlockedExchange(&gReadbackChannelFaulted, 1);
		DX12ReadbackLogSubmitFailure("failed_game_queue_sync", kept.size(),
			static_cast<UINT64>(submitHr));
		allocator->Release();
		list->Release();
		DX12ReadbackReleaseEvents(kept);
		ReleaseSRWLockExclusive(&gReadbackChannelLock);
		device->Release();
		return;
	}
	gameFence->value = gameFenceValue;

	DX12ReadbackBatch *batch = new DX12ReadbackBatch();
	batch->allocator = allocator;
	batch->list = list;
	slot->buffer->AddRef();
	batch->ring = slot->buffer;
	batch->ringUsed = ringUsed;
	batch->events.swap(kept);

	ID3D12CommandList *lists[] = { list };
	gReadbackQueue->ExecuteCommandLists(1, lists);
	batch->fenceValue = ++gReadbackFenceValue;
	submitHr = gReadbackQueue->Signal(gReadbackFence, batch->fenceValue);
	if (FAILED(submitHr)) {
		InterlockedExchange(&gReadbackChannelFaulted, 1);
		DX12ReadbackLogSubmitFailure("failed_readback_signal", batch->events.size(),
			static_cast<UINT64>(submitHr));
		batch->ring->Release();
		DX12ReadbackReleaseEvents(batch->events);
		delete batch;
		ReleaseSRWLockExclusive(&gReadbackChannelLock);
		device->Release();
		return;
	}
	batch->event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	if (batch->event)
		gReadbackFence->SetEventOnCompletion(batch->fenceValue, batch->event);

	slot->busyUntilFence = batch->fenceValue;
	gReadbackPendingBatches.push_back(batch);
	ReleaseSRWLockExclusive(&gReadbackChannelLock);

	// Rate-limited pipeline visibility keeps readback activity observable without
	// flooding the log.
	{
		static volatile LONG submitLogCounter = 0;
		if ((InterlockedIncrement(&submitLogCounter) & 0xFF) == 1) {
			DX12LogManager::Fields("DX12ReadbackSubmit",
				"stage=submitted,events=%u,ringBytes=%llu,us=%lld,queue=%p",
				batch->events.size(),
				static_cast<unsigned long long>(ringUsed),
				static_cast<long long>(
					DX12OuterTimingProbeUtils::ElapsedUs(submitBegin)),
				gameQueue);
		}
	}

	DX12ReadbackEnsureWorker();
	if (gReadbackWorkerEvent)
		SetEvent(gReadbackWorkerEvent);
	device->Release();
}
