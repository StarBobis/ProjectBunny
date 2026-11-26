// Copies are recorded in the producer list. A recording owns its readback
// pages until Reset/Release AND the last GPU submission/CPU reader retire.
#include "DX12Core.h"
#include "DX12Hooks.h"
#include "DX12Tracking.h"
#include "DX12Runtime.h"
#include "DX12Resources.h"
#include <wrl/client.h>
#include <deque>
#include <memory>
#include <mutex>

namespace
{
using Microsoft::WRL::ComPtr;
constexpr UINT64 MaxCapture = 64ull << 20;
constexpr UINT64 PageBudget = 256ull << 20;
constexpr UINT64 PageAlignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;

struct Page
{
	ComPtr<ID3D12Device> device;
	ComPtr<ID3D12Resource> buffer;
	UINT64 capacity = 0;
};

// A miss on an already-bound texture is retried at a legal copy or a
// transition of subresource zero. Resource destruction removes the weak key.
std::mutex wantedLock;
std::unordered_map<ID3D12Resource*, ULONGLONG> wanted;
ULONGLONG lastWantedSweep = 0;
std::atomic<size_t> wantedCount{0};

std::mutex pageLock;
std::vector<std::unique_ptr<Page>> sparePages;
UINT64 allocatedBytes = 0;

void ReturnPage(Page *page)
{
	std::lock_guard lock(pageLock);
	sparePages.emplace_back(page);
}

std::shared_ptr<Page> AcquirePage(ID3D12Device *device, UINT64 bytes)
{
	const UINT64 capacity = (bytes + PageAlignment - 1) & ~(PageAlignment - 1);
	std::unique_ptr<Page> page;
	{
		std::lock_guard lock(pageLock);
		auto best = sparePages.end();
		for (auto it = sparePages.begin(); it != sparePages.end(); ++it) {
			if ((*it)->device.Get() == device && (*it)->capacity >= capacity &&
				(best == sparePages.end() || (*it)->capacity < (*best)->capacity))
				best = it;
		}
		if (best != sparePages.end()) {
			page = std::move(*best);
			sparePages.erase(best);
		} else {
			while (allocatedBytes + capacity > PageBudget && !sparePages.empty()) {
				allocatedBytes -= sparePages.back()->capacity;
				sparePages.pop_back();
			}
			if (allocatedBytes + capacity > PageBudget)
				return {};
			allocatedBytes += capacity;
		}
	}
	if (!page) {
		page = std::make_unique<Page>();
		page->device = device;
		page->capacity = capacity;
		D3D12_HEAP_PROPERTIES heap = {};
		heap.Type = D3D12_HEAP_TYPE_READBACK;
		D3D12_RESOURCE_DESC desc = {};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		desc.Width = capacity;
		desc.Height = desc.DepthOrArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
		desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
			&desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&page->buffer)))) {
			std::lock_guard lock(pageLock);
			allocatedBytes -= capacity;
			return {};
		}
	}
	return {page.release(), ReturnPage};
}

struct Capture
{
	// The original copy/barrier already names this operand. D3D12 requires
	// the application to keep it alive for each execution. Acquire a strong
	// reference only at submit so an idle recording cannot pin a back buffer.
	ID3D12Resource *target = nullptr;
	std::shared_ptr<Page> page;
	UINT64 offset = 0;
	UINT64 size = 0;
	UINT64 targetOffset = 0;
	D3D12_RESOURCE_DESC desc = {};
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
	UINT rows = 0;
	UINT64 rowBytes = 0;
};

struct Recording
{
	ComPtr<ID3D12Device> device;
	std::vector<Capture> captures;
	std::shared_ptr<Page> currentPage;
	UINT64 nextOffset = 0;
	std::unordered_map<ID3D12Resource*, bool> targets;
	// A repeated execution can write these pages again. Exclude a CPU reader
	// before forwarding the repeat, and publish only its newest execution.
	std::mutex publishLock;
	UINT64 execution = 0;
};

struct SubmittedRecording
{
	std::shared_ptr<Recording> recording;
	UINT64 execution;
};

struct Submission
{
	ComPtr<ID3D12Fence> fence;
	ComPtr<ID3D12CommandQueue> queue;
	std::vector<SubmittedRecording> recordings;
	std::vector<ComPtr<ID3D12Resource>> targets;
	UINT64 value = 0;
};

std::mutex recordsLock;
std::unordered_map<ID3D12CommandList*, std::shared_ptr<Recording>> recordings;
std::atomic<size_t> recordingCount{0};
// Each queue owns its timeline fence. Values are allocated under submitLock so
// submissions on one queue cannot signal a lower value after a higher one.
constexpr GUID queueFenceKey = {0x6b8d33ed, 0x1476, 0x4e18, {0x87, 0x65, 0x35, 0xbd, 0xc4, 0xe4, 0x15, 0xfa}};
UINT64 nextFenceValue = 0;
std::mutex submitLock;
std::deque<Submission> pending;
std::atomic<size_t> pendingCount{0};

std::shared_ptr<Recording> GetRecording(ID3D12GraphicsCommandList *list)
{
	std::lock_guard lock(recordsLock);
	auto &record = recordings[list];
	if (!record) {
		record = std::make_shared<Recording>();
		list->GetDevice(IID_PPV_ARGS(&record->device));
		recordingCount.store(recordings.size(), std::memory_order_release);
	}
	return record;
}

bool Reserve(Recording &record, Capture &capture)
{
	if (!record.device || !capture.size || capture.size > MaxCapture)
		return false;
	if (!record.currentPage || record.nextOffset + capture.size > record.currentPage->capacity) {
		auto page = AcquirePage(record.device.Get(), capture.size);
		if (!page)
			return false;
		record.currentPage = std::move(page);
		record.nextOffset = 0;
	}
	capture.page = record.currentPage;
	capture.offset = record.nextOffset;
	record.nextOffset = (capture.offset + capture.size + D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1) &
		~UINT64(D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1);
	return true;
}

void Publish(const SubmittedRecording &submitted)
{
	std::lock_guard lock(submitted.recording->publishLock);
	if (submitted.recording->execution != submitted.execution)
		return;
	for (const Capture &capture : submitted.recording->captures) {
		if (!capture.page) {
			if (capture.size)
				DX12InvalidateBufferContentRange(capture.target, capture.targetOffset, capture.size);
			else {
				DX12InvalidateTextureHash(capture.target);
				DX12MarkTextureHashWanted(capture.target);
			}
			continue;
		}
		const D3D12_RANGE range = {static_cast<SIZE_T>(capture.offset),
			static_cast<SIZE_T>(capture.offset + capture.size)};
		void *mapped = nullptr;
		if (FAILED(capture.page->buffer->Map(0, &range, &mapped))) {
			if (capture.rows)
				DX12InvalidateTextureHash(capture.target);
			else
				DX12InvalidateBufferContentRange(capture.target, capture.targetOffset, capture.size);
			continue;
		}
		const auto *bytes = static_cast<const uint8_t*>(mapped) + capture.offset;
		if (capture.rows) {
			uint32_t hash = 0;
			if (DX12TextureHashUtils::Texture2DTopMipHashFromRows(capture.desc,
				bytes, capture.footprint.Footprint.RowPitch, capture.rowBytes, capture.rows, &hash))
				DX12CacheTextureHash(capture.target, hash, &capture.desc, "async_readback");
		} else {
			if (!DX12CacheBufferContentRange(capture.target, capture.targetOffset,
				bytes, static_cast<size_t>(capture.size)))
				DX12InvalidateBufferContentRange(capture.target, capture.targetOffset, capture.size);
		}
		const D3D12_RANGE noWrites = {};
		capture.page->buffer->Unmap(0, &noWrites);
	}
}


}

bool DX12ReadbackAwaitingPublication()
{
	return pendingCount.load(std::memory_order_acquire) != 0;
}

// Called only by the resource collection worker. A busy GPU never blocks
// resource metadata processing. Retire in submission order across queues.
void DX12ReadbackPublishReady()
{
	while (DX12ReadbackAwaitingPublication()) {
		Submission completed;
		bool publish = false;
		{
			std::lock_guard lock(submitLock);
			if (pending.empty())
				return;
			auto &front = pending.front();
			auto *device = front.recordings.front().recording->device.Get();
			if (!front.fence) {
				// An earlier signal failed. Retrying on a private fence cannot rewind
				// the shared queue timeline past a newer successful submission.
				ComPtr<ID3D12Fence> retry;
				if (SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&retry))) &&
					SUCCEEDED(front.queue->Signal(retry.Get(), 1))) {
					front.fence = std::move(retry);
					front.value = 1;
				} else if (SUCCEEDED(device->GetDeviceRemovedReason())) {
					return;
				}
			}
			const UINT64 value = front.fence ? front.fence->GetCompletedValue() : UINT64_MAX;
			if (value < front.value)
				return;
			publish = value != UINT64_MAX;
			completed = std::move(front);
			pending.pop_front();
			pendingCount.store(pending.size(), std::memory_order_release);
		}
		for (const auto &entry : completed.recordings) {
			if (publish)
				Publish(entry);
			for (const auto &[resource, texture] : entry.recording->targets) {
				if (!publish) {
					if (texture)
						DX12InvalidateTextureHash(resource);
					else
						DX12InvalidateBufferContent(resource);
				}
				DX12SetReadbackPending(resource, texture, false);
			}
		}
	}
}

void DX12MarkTextureHashWanted(ID3D12Resource *texture)
{
	if (!texture)
		return;
	std::lock_guard lock(wantedLock);
	const ULONGLONG now = GetTickCount64();
	if (now - lastWantedSweep >= 1000) {
		std::erase_if(wanted, [now](const auto &entry) { return entry.second <= now; });
		lastWantedSweep = now;
	}
	if (wanted.size() < 4096 || wanted.contains(texture))
		wanted[texture] = now + 20000;
	wantedCount.store(wanted.size(), std::memory_order_release);
}

void DX12ReadbackForgetWantedTexture(ID3D12Resource *resource, bool)
{
	if (!wantedCount.load(std::memory_order_acquire))
		return;
	std::lock_guard lock(wantedLock);
	wanted.erase(resource);
	wantedCount.store(wanted.size(), std::memory_order_release);
}

void DX12ReadbackRecordBuffer(ID3D12GraphicsCommandList *list, ID3D12Resource *target,
	UINT64 targetOffset, ID3D12Resource *source, UINT64 sourceOffset, UINT64 size)
{
	if (!list || !target || !size)
		return;
	auto record = GetRecording(list);
	Capture capture;
	capture.target = target;
	capture.targetOffset = targetOffset;
	capture.size = size;
	if (!source || !Reserve(*record, capture)) {
		record->captures.push_back(std::move(capture));
		return;
	}
	// The game's source is already legal for COPY_SOURCE, including upload heaps.
	list->CopyBufferRegion(capture.page->buffer.Get(), capture.offset, source, sourceOffset, size);
	record->captures.push_back(std::move(capture));
}

namespace
{
bool IsWanted(ID3D12Resource *resource)
{
	std::lock_guard lock(wantedLock);
	return wanted.contains(resource);
}

// Sample the same source bytes as the game's top-mip write. For a partial
// write a later explicit transition provides the state of the whole texture.
template<class Copy>
bool CaptureTexture(ID3D12GraphicsCommandList *list, ID3D12Resource *target, UINT sourcePitch, Copy copy)
{
	Capture capture;
	capture.desc = target->GetDesc();
	if (capture.desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
		capture.desc.SampleDesc.Count != 1 ||
		!DX12TextureHashUtils::Texture2DTopMipRowBytes(capture.desc.Format,
			static_cast<UINT>(capture.desc.Width)))
		return false;
	auto record = GetRecording(list);
	if (!record->device)
		return false;
	record->device->GetCopyableFootprints(&capture.desc, 0, 1, 0, &capture.footprint,
		&capture.rows, &capture.rowBytes, &capture.size);
	if (sourcePitch) {
		if (sourcePitch < capture.rowBytes || !capture.rows)
			return false;
		capture.footprint.Footprint.RowPitch = sourcePitch;
		capture.size = static_cast<UINT64>(capture.rows - 1) * sourcePitch + capture.rowBytes;
	}
	if (!Reserve(*record, capture))
		return false;
	capture.target = target;
	D3D12_TEXTURE_COPY_LOCATION dst = {};
	dst.pResource = capture.page->buffer.Get();
	dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	dst.PlacedFootprint = capture.footprint;
	dst.PlacedFootprint.Offset = capture.offset;
	copy(dst, capture.size);
	record->captures.push_back(std::move(capture));
	return true;
}

void DeferTexture(ID3D12GraphicsCommandList *list, ID3D12Resource *target)
{
	DX12MarkTextureHashWanted(target);
	Capture dirty;
	dirty.target = target;
	GetRecording(list)->captures.push_back(std::move(dirty));
}

D3D12_TEXTURE_COPY_LOCATION TopMip(ID3D12Resource *resource)
{
	D3D12_TEXTURE_COPY_LOCATION location = {};
	location.pResource = resource;
	location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	return location;
}
}

void DX12ReadbackRecordTextureCopy(ID3D12GraphicsCommandList *list, ID3D12Resource *target,
	UINT x, UINT y, UINT z, const D3D12_TEXTURE_COPY_LOCATION *location, const D3D12_BOX *box)
{
	if (!target)
		return;
	const auto desc = target->GetDesc();
	if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || desc.SampleDesc.Count != 1 ||
		!DX12TextureHashUtils::Texture2DTopMipRowBytes(desc.Format, static_cast<UINT>(desc.Width)))
		return;
	if (!location || !location->pResource) {
		DeferTexture(list, target);
		return;
	}
	const auto &source = *location;
	UINT64 width;
	UINT height, depth;
	const bool buffer = source.Type == D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	if (box) {
		width = box->right - box->left;
		height = box->bottom - box->top;
		depth = box->back - box->front;
	} else if (buffer) {
		width = source.PlacedFootprint.Footprint.Width;
		height = source.PlacedFootprint.Footprint.Height;
		depth = source.PlacedFootprint.Footprint.Depth;
	} else {
		const auto srcDesc = source.pResource->GetDesc();
		if (srcDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || !srcDesc.MipLevels) {
			DeferTexture(list, target);
			return;
		}
		const UINT mip = source.SubresourceIndex % srcDesc.MipLevels;
		width = (std::max)(UINT64(1), srcDesc.Width >> mip);
		height = (std::max)(1u, srcDesc.Height >> mip);
		depth = 1;
	}
	if (!x && !y && !z && width == desc.Width && height == desc.Height && depth == 1 &&
		(!buffer || !box || (!box->left && !box->top && !box->front))) {
		const UINT pitch = buffer ? source.PlacedFootprint.Footprint.RowPitch : 0;
		if (CaptureTexture(list, target, pitch, [&](const auto &dst, UINT64 size) {
			if (buffer)
				list->CopyBufferRegion(dst.pResource, dst.PlacedFootprint.Offset,
					source.pResource, source.PlacedFootprint.Offset, size);
			else
				list->CopyTextureRegion(&dst, 0, 0, 0, &source, box);
		}))
			return;
	}
	DeferTexture(list, target);
}

void DX12ReadbackRecordTexture(ID3D12GraphicsCommandList *list, ID3D12Resource *target, ID3D12Resource *source)
{
	const auto src = TopMip(source);
	DX12ReadbackRecordTextureCopy(list, target, 0, 0, 0, &src, nullptr);
}

void DX12ReadbackForwardBarriers(ID3D12GraphicsCommandList *list, UINT count,
	const D3D12_RESOURCE_BARRIER *barriers,
	void (STDMETHODCALLTYPE *original)(ID3D12GraphicsCommandList*, UINT, const D3D12_RESOURCE_BARRIER*))
{
	auto forward = [&](UINT n, const D3D12_RESOURCE_BARRIER *data) {
		if (original)
			original(list, n, data);
		else
			list->ResourceBarrier(n, data);
	};
	UINT first = 0;
	if (barriers && wantedCount.load(std::memory_order_acquire)) {
		for (UINT i = 0; i < count; ++i) {
			const auto &barrier = barriers[i];
			const auto &transition = barrier.Transition;
			if (barrier.Type != D3D12_RESOURCE_BARRIER_TYPE_TRANSITION ||
				barrier.Flags == D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY ||
				(transition.Subresource && transition.Subresource != D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES) ||
				!IsWanted(transition.pResource))
				continue;
			CaptureTexture(list, transition.pResource, 0, [&](const auto &dst, UINT64) {
				forward(i + 1 - first, barriers + first);
				first = i + 1;
				auto temporary = barrier;
				temporary.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
				temporary.Transition.Subresource = 0;
				temporary.Transition.StateBefore = transition.StateAfter;
				temporary.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
				const bool change = !(transition.StateAfter & D3D12_RESOURCE_STATE_COPY_SOURCE);
				if (change)
					forward(1, &temporary);
				const auto src = TopMip(transition.pResource);
				list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
				if (change) {
					std::swap(temporary.Transition.StateBefore, temporary.Transition.StateAfter);
					forward(1, &temporary);
				}
			});
		}
	}
	if (!first || first < count)
		forward(count - first, first ? barriers + first : barriers);
}

void DX12ReadbackForwardEnhancedBarriers(ID3D12GraphicsCommandList *owner, ID3D12GraphicsCommandList7 *list, UINT count,
	const D3D12_BARRIER_GROUP *groups)
{
	UINT firstGroup = 0;
	if (groups && wantedCount.load(std::memory_order_acquire)) {
		for (UINT g = 0; g < count; ++g) {
			const auto &group = groups[g];
			if (group.Type != D3D12_BARRIER_TYPE_TEXTURE)
				continue;
			UINT first = 0;
			auto forwardRange = [&](UINT end) {
				auto slice = group;
				slice.pTextureBarriers += first;
				slice.NumBarriers = end - first;
				list->Barrier(1, &slice);
				first = end;
			};
			for (UINT i = 0; i < group.NumBarriers; ++i) {
				const auto &barrier = group.pTextureBarriers[i];
				const auto &range = barrier.Subresources;
				if (barrier.SyncAfter == D3D12_BARRIER_SYNC_SPLIT ||
					barrier.LayoutAfter == D3D12_BARRIER_LAYOUT_UNDEFINED ||
					barrier.AccessAfter == D3D12_BARRIER_ACCESS_NO_ACCESS ||
					barrier.Flags == D3D12_TEXTURE_BARRIER_FLAG_DISCARD ||
					(range.IndexOrFirstMipLevel != D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES &&
					 (range.IndexOrFirstMipLevel || (range.NumMipLevels && (range.FirstArraySlice || range.FirstPlane)))) ||
					!IsWanted(barrier.pResource))
					continue;
				CaptureTexture(owner, barrier.pResource, 0, [&](const auto &dst, UINT64) {
					if (firstGroup < g)
						list->Barrier(g - firstGroup, groups + firstGroup);
					firstGroup = g + 1;
					forwardRange(i + 1);
					auto temporary = barrier;
					temporary.Subresources = {}; // subresource index zero
					temporary.Flags = D3D12_TEXTURE_BARRIER_FLAG_NONE;
					temporary.SyncBefore = temporary.SyncAfter = D3D12_BARRIER_SYNC_ALL;
					temporary.AccessBefore = barrier.AccessAfter;
					temporary.AccessAfter = D3D12_BARRIER_ACCESS_COPY_SOURCE;
					temporary.LayoutBefore = barrier.LayoutAfter;
					temporary.LayoutAfter = list->GetType() == D3D12_COMMAND_LIST_TYPE_COPY
						? D3D12_BARRIER_LAYOUT_COMMON : D3D12_BARRIER_LAYOUT_COPY_SOURCE;
					D3D12_BARRIER_GROUP one = {};
					one.Type = D3D12_BARRIER_TYPE_TEXTURE;
					one.NumBarriers = 1;
					one.pTextureBarriers = &temporary;
					list->Barrier(1, &one);
					const auto src = TopMip(barrier.pResource);
					list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
					std::swap(temporary.AccessBefore, temporary.AccessAfter);
					std::swap(temporary.LayoutBefore, temporary.LayoutAfter);
					list->Barrier(1, &one);
				});
			}
			if (first && first < group.NumBarriers)
				forwardRange(group.NumBarriers);
		}
	}
	if (!firstGroup || firstGroup < count)
		list->Barrier(count - firstGroup, firstGroup ? groups + firstGroup : groups);
}

void DX12ReadbackForgetRecording(ID3D12CommandList *list)
{
	if (!recordingCount.load(std::memory_order_acquire))
		return;
	std::shared_ptr<Recording> retired;
	{
		std::lock_guard lock(recordsLock);
		auto it = recordings.find(list);
		if (it == recordings.end())
			return;
		retired = std::move(it->second);
		recordings.erase(it);
		recordingCount.store(recordings.size(), std::memory_order_release);
	}
}

void DX12ReadbackExecute(ID3D12CommandQueue *queue, UINT count, ID3D12CommandList *const *lists,
	void (STDMETHODCALLTYPE *execute)(ID3D12CommandQueue*, UINT, ID3D12CommandList *const *))
{
	if (!recordingCount.load(std::memory_order_acquire) || !lists) {
		execute(queue, count, lists);
		return;
	}
	Submission batch;
	{
		std::lock_guard lock(recordsLock);
		for (UINT i = 0; i < count; ++i) {
			auto it = recordings.find(lists[i]);
			if (it != recordings.end() && !it->second->captures.empty())
				batch.recordings.push_back({it->second, 0});
		}
	}
	if (batch.recordings.empty()) {
		execute(queue, count, lists);
		return;
	}
	std::unique_lock lock(submitLock);
	for (auto &entry : batch.recordings) {
		std::lock_guard readerLock(entry.recording->publishLock);
		if (entry.recording->targets.empty()) {
			for (const Capture &capture : entry.recording->captures)
				entry.recording->targets.emplace(capture.target, capture.rows || !capture.size);
		}
		for (const auto &[resource, texture] : entry.recording->targets) {
			batch.targets.emplace_back(resource);
			DX12SetReadbackPending(resource, texture, true);
		}
		entry.execution = ++entry.recording->execution;
	}
	batch.queue = queue;
	auto *device = batch.recordings.front().recording->device.Get();
	UINT fenceSize = sizeof(ID3D12Fence*);
	if (FAILED(queue->GetPrivateData(queueFenceKey, &fenceSize, batch.fence.GetAddressOf())) &&
		SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&batch.fence))))
		queue->SetPrivateDataInterface(queueFenceKey, batch.fence.Get());
	batch.value = ++nextFenceValue;
	execute(queue, count, lists);
	if (batch.fence && FAILED(queue->Signal(batch.fence.Get(), batch.value)))
		batch.fence.Reset();
	const bool wake = pending.empty();
	pending.push_back(std::move(batch));
	pendingCount.store(pending.size(), std::memory_order_release);
	lock.unlock();
	if (wake)
		DX12ResourceCollectQueue::WakeForReadback();
}
