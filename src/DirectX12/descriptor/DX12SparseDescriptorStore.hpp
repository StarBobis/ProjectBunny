#pragma once

#include <stdint.h>

#include <array>
#include <atomic>
#include <memory>
#include <utility>
#include <vector>

// Sparse current-value storage used by Descriptor State. The old layout kept
// 1024 unordered_maps per heap so independently locked slots could be updated
// concurrently. Real descriptor rings issue tens of thousands of single-slot
// copies per frame, making two hash-table probes per copy the dominant CPU
// cost. This layout retains sparse allocation, but resolves a slot through a
// pre-sized page-pointer table and one direct array index instead.
//
// The store deliberately contains no per-slot locking. The owning descriptor
// heap supplies that policy. Page publication is atomic because different slot
// locks can touch the same page for the first time concurrently.
template <typename Value>
class DX12SparseDescriptorStore
{
public:
	// Kept in sync with the owning heap's lock striping contract.
	static constexpr uint32_t ShardCount = 1024;
	static constexpr uint32_t PageSize = 256;

	static_assert((ShardCount & (ShardCount - 1)) == 0,
		"descriptor shard count must be a power of two");
	static_assert((PageSize & (PageSize - 1)) == 0,
		"descriptor page size must be a power of two");

	static constexpr uint32_t ShardForSlot(uint32_t slot)
	{
		return slot & (ShardCount - 1);
	}

	struct SnapshotEntry
	{
		bool occupied = false;
		Value value = {};
	};

	DX12SparseDescriptorStore() = default;

	DX12SparseDescriptorStore(const DX12SparseDescriptorStore&) = delete;
	DX12SparseDescriptorStore &operator=(
		const DX12SparseDescriptorStore&) = delete;

	DX12SparseDescriptorStore(DX12SparseDescriptorStore &&other) noexcept
	{
		MoveFrom(std::move(other));
	}

	DX12SparseDescriptorStore &operator=(
		DX12SparseDescriptorStore &&other) noexcept
	{
		if (this == &other)
			return *this;
		ResetStorage();
		MoveFrom(std::move(other));
		return *this;
	}

	~DX12SparseDescriptorStore()
	{
		ResetStorage();
	}

	// Called once when the descriptor heap boundary becomes known. Only the
	// small page-pointer index is allocated here; Value pages remain lazy.
	void Initialize(uint32_t maxSlots)
	{
		ResetStorage();
		maxSlots_ = maxSlots;
		pageCount_ = maxSlots == 0 ? 0 : static_cast<uint32_t>(
			(static_cast<uint64_t>(maxSlots) + PageSize - 1) / PageSize);
		if (pageCount_ == 0)
			return;
		pages_ = std::make_unique<std::atomic<Page*>[]>(pageCount_);
		for (uint32_t index = 0; index < pageCount_; ++index)
			pages_[index].store(nullptr, std::memory_order_relaxed);
	}

	const Value *Find(uint32_t slot) const
	{
		const Page *page = PageForSlot(slot);
		if (!page)
			return nullptr;
		const uint32_t offset = OffsetForSlot(slot);
		return page->occupied[offset] ? &page->values[offset] : nullptr;
	}

	Value *Find(uint32_t slot)
	{
		Page *page = PageForSlot(slot);
		if (!page)
			return nullptr;
		const uint32_t offset = OffsetForSlot(slot);
		return page->occupied[offset] ? &page->values[offset] : nullptr;
	}

	// Returns the existing value or inserts one default value. Callers already
	// serialize the owning slot, so the returned address remains stable.
	Value *FindOrInsertDefault(uint32_t slot, bool *inserted)
	{
		Page *page = EnsurePageForSlot(slot);
		if (!page) {
			if (inserted)
				*inserted = false;
			return nullptr;
		}
		const uint32_t offset = OffsetForSlot(slot);
		const bool created = page->occupied[offset] == 0;
		if (created) {
			page->values[offset] = Value();
			page->occupied[offset] = 1;
		}
		if (inserted)
			*inserted = created;
		return &page->values[offset];
	}

	bool Store(uint32_t slot, Value value)
	{
		bool inserted = false;
		Value *stored = FindOrInsertDefault(slot, &inserted);
		if (!stored)
			return false;
		*stored = std::move(value);
		return inserted;
	}

	bool Erase(uint32_t slot)
	{
		Page *page = PageForSlot(slot);
		if (!page)
			return false;
		const uint32_t offset = OffsetForSlot(slot);
		if (!page->occupied[offset])
			return false;
		page->values[offset] = Value();
		page->occupied[offset] = 0;
		return true;
	}

	void SnapshotRange(
		uint32_t firstSlot,
		uint32_t count,
		std::vector<SnapshotEntry> *snapshot) const
	{
		if (!snapshot)
			return;
		snapshot->clear();
		snapshot->resize(count);
		for (uint32_t i = 0; i < count; ++i) {
			const Value *value = Find(firstSlot + i);
			if (!value)
				continue;
			(*snapshot)[i].occupied = true;
			(*snapshot)[i].value = *value;
		}
	}

	template <typename Visitor>
	void ForEach(Visitor visitor) const
	{
		for (uint32_t pageIndex = 0; pageIndex < pageCount_; ++pageIndex) {
			const Page *page = pages_[pageIndex].load(std::memory_order_acquire);
			if (!page)
				continue;
			const uint32_t firstSlot = pageIndex * PageSize;
			const uint32_t slotsInPage =
				maxSlots_ - firstSlot < PageSize ? maxSlots_ - firstSlot : PageSize;
			for (uint32_t offset = 0; offset < slotsInPage; ++offset) {
				if (page->occupied[offset])
					visitor(firstSlot + offset, page->values[offset]);
			}
		}
	}

	size_t Size() const
	{
		size_t size = 0;
		ForEach([&size](uint32_t, const Value&) { ++size; });
		return size;
	}

	bool Empty() const
	{
		for (uint32_t pageIndex = 0; pageIndex < pageCount_; ++pageIndex) {
			const Page *page = pages_[pageIndex].load(std::memory_order_acquire);
			if (!page)
				continue;
			const uint32_t firstSlot = pageIndex * PageSize;
			const uint32_t slotsInPage =
				maxSlots_ - firstSlot < PageSize ? maxSlots_ - firstSlot : PageSize;
			for (uint32_t offset = 0; offset < slotsInPage; ++offset) {
				if (page->occupied[offset])
					return false;
			}
		}
		return true;
	}

	// Releases only allocated pages while retaining the small page index, so a
	// cleared active heap can be populated again without rebuilding capacity.
	void Clear()
	{
		if (!pages_)
			return;
		for (uint32_t index = 0; index < pageCount_; ++index) {
			Page *page = pages_[index].exchange(
				nullptr, std::memory_order_acq_rel);
			delete page;
		}
	}

	uint32_t AllocatedPageCount() const
	{
		uint32_t count = 0;
		for (uint32_t index = 0; index < pageCount_; ++index) {
			if (pages_[index].load(std::memory_order_acquire))
				++count;
		}
		return count;
	}

private:
	struct Page
	{
		std::array<Value, PageSize> values = {};
		std::array<uint8_t, PageSize> occupied = {};
	};

	static constexpr uint32_t OffsetForSlot(uint32_t slot)
	{
		return slot & (PageSize - 1);
	}

	Page *PageForSlot(uint32_t slot)
	{
		return const_cast<Page*>(
			static_cast<const DX12SparseDescriptorStore*>(this)->PageForSlot(slot));
	}

	const Page *PageForSlot(uint32_t slot) const
	{
		if (!pages_ || slot >= maxSlots_)
			return nullptr;
		return pages_[slot / PageSize].load(std::memory_order_acquire);
	}

	Page *EnsurePageForSlot(uint32_t slot)
	{
		if (!pages_ || slot >= maxSlots_)
			return nullptr;
		std::atomic<Page*> &pageCell = pages_[slot / PageSize];
		Page *page = pageCell.load(std::memory_order_acquire);
		if (page)
			return page;

		Page *candidate = new Page();
		Page *expected = nullptr;
		if (pageCell.compare_exchange_strong(
				expected, candidate,
				std::memory_order_release,
				std::memory_order_acquire))
			return candidate;
		delete candidate;
		return expected;
	}

	void ResetStorage()
	{
		Clear();
		pages_.reset();
		pageCount_ = 0;
		maxSlots_ = 0;
	}

	void MoveFrom(DX12SparseDescriptorStore &&other)
	{
		pages_ = std::move(other.pages_);
		pageCount_ = other.pageCount_;
		maxSlots_ = other.maxSlots_;
		other.pageCount_ = 0;
		other.maxSlots_ = 0;
	}

	std::unique_ptr<std::atomic<Page*>[]> pages_;
	uint32_t pageCount_ = 0;
	uint32_t maxSlots_ = 0;
};
