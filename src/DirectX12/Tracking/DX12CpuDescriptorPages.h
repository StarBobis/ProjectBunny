#pragma once

#include <d3d12.h>
#include <algorithm>
#include <atomic>
#include <bit>
#include <cstdint>
#include <memory>
#include <mutex>
#include <wrl/client.h>

// D3D12 permits a shader-visible descriptor heap as a copy destination only.
// Keep the physical values independently of logical resource records. A sparse
// native CPU heap does not retain resources or dereference old resource pointers.
class DX12CpuDescriptorPages
{
	static constexpr UINT PageSize = 256;
	struct Page
	{
		std::mutex creation;
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
		std::atomic<SIZE_T> base = 0;
		std::atomic<uint64_t> written[PageSize / 64]{};
		// Only allocation failures set lost bits. Rewriting those slots repairs them.
		std::atomic<uint64_t> lost[PageSize / 64]{};
	};

	ID3D12Device *device;
	const UINT increment;
	const UINT capacity;
	const UINT nodeMask;
	std::unique_ptr<Page[]> pages;

	static uint64_t Mask(UINT bit, UINT count)
	{
		return (UINT64_MAX >> (64 - count)) << bit;
	}

	static void MarkWritten(Page &page, UINT word, uint64_t mask, bool copied)
	{
		if (!copied) {
			page.lost[word].fetch_or(mask, std::memory_order_release);
			return;
		}
		if ((page.written[word].load(std::memory_order_relaxed) & mask) != mask)
			page.written[word].fetch_or(mask, std::memory_order_release);
		if (page.lost[word].load(std::memory_order_relaxed) & mask)
			page.lost[word].fetch_and(~mask, std::memory_order_release);
	}

	SIZE_T EnsurePage(UINT index)
	{
		Page &page = pages[index];
		if (auto base = page.base.load(std::memory_order_acquire))
			return base;
		std::lock_guard lock(page.creation);
		if (!page.heap) {
			D3D12_DESCRIPTOR_HEAP_DESC desc{};
			desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			desc.NumDescriptors = (std::min)(PageSize, capacity - index * PageSize);
			desc.NodeMask = nodeMask;
			if (FAILED(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&page.heap))))
				return 0;
			page.base.store(page.heap->GetCPUDescriptorHandleForHeapStart().ptr, std::memory_order_release);
		}
		return page.base.load(std::memory_order_relaxed);
	}

public:
	DX12CpuDescriptorPages(ID3D12Device *owner, UINT stride, UINT slots, UINT node)
		: device(owner), increment(stride), capacity(slots), nodeMask(node),
		  pages(std::make_unique<Page[]>((slots - 1) / PageSize + 1)) {}

	ID3D12Device *Device() const { return device; }

	// All writers operate on the application's destination range. D3D12
	// requires synchronization for overlapping writes; disjoint slots may
	// be updated by independent recording threads.
	template<class Writer>
	void Write(UINT slot, UINT count, Writer write)
	{
		if (slot >= capacity || count > capacity - slot)
			return;
		if (count == 1) {
			const UINT index = slot / PageSize;
			const SIZE_T base = EnsurePage(index);
			if (base)
				write(D3D12_CPU_DESCRIPTOR_HANDLE{base + (slot % PageSize) * SIZE_T(increment)}, 0, 1);
			MarkWritten(pages[index], (slot % PageSize) / 64, UINT64_C(1) << (slot % 64), base != 0);
			return;
		}
		UINT offset = 0;
		while (offset < count) {
			const UINT current = slot + offset;
			const UINT inPage = current % PageSize;
			const UINT n = (std::min)(count - offset, PageSize - inPage);
			auto &page = pages[current / PageSize];
			const SIZE_T base = EnsurePage(current / PageSize);
			if (base)
				write(D3D12_CPU_DESCRIPTOR_HANDLE{base + inPage * SIZE_T(increment)}, offset, n);
			for (UINT i = inPage; i < inPage + n;) {
				const UINT bit = i % 64;
				const UINT bits = (std::min)(inPage + n - i, 64 - bit);
				const auto mask = Mask(bit, bits);
				MarkWritten(page, i / 64, mask, base != 0);
				i += bits;
			}
			offset += n;
		}
	}

	bool CopyTo(UINT slot, UINT count, D3D12_CPU_DESCRIPTOR_HANDLE dest) const
	{
		if (slot >= capacity || count > capacity - slot)
			return false;
		UINT offset = 0;
		while (offset < count) {
			const UINT current = slot + offset;
			const UINT inPage = current % PageSize;
			const auto &page = pages[current / PageSize];
			const UINT bit = inPage % 64;
			const UINT n = (std::min)(count - offset, 64 - bit);
			const uint64_t mask = Mask(bit, n);
			if (page.lost[inPage / 64].load(std::memory_order_acquire) & mask)
				return false;
			uint64_t valid = (page.written[inPage / 64].load(std::memory_order_acquire) & mask) >> bit;
			const SIZE_T base = page.base.load(std::memory_order_acquire);
			while (valid) {
				const UINT start = std::countr_zero(valid);
				const UINT length = std::countr_one(valid >> start);
				device->CopyDescriptorsSimple(length, {dest.ptr + (offset + start) * SIZE_T(increment)},
					{base + (inPage + start) * SIZE_T(increment)}, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				valid &= ~Mask(start, length);
			}
			offset += n;
		}
		return true;
	}
};
