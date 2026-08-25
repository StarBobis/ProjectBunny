// Sharded descriptor heap state and the sparse descriptor store.
// Split from DX12Tracking.h; keep declarations in the layer that owns them.
#pragma once

#include "DX12FrameAnalysisTracking.h"

// Lock and version storage for one descriptor heap. Each shard occupies its
// own cache line: packing SRWLOCKs or counters together would make unrelated
// descriptor slots contend through false sharing even though their logical
// locks are independent.
template <size_t ShardCountValue>
class DX12DescriptorHeapShardState
{
public:
	static constexpr size_t ShardCount = ShardCountValue;
	static constexpr size_t CacheLineBytes = 64;

	static_assert(ShardCount != 0 && (ShardCount & (ShardCount - 1)) == 0,
		"descriptor shard count must be a power of two");

	struct alignas(CacheLineBytes) LockCell
	{
		SRWLOCK value = SRWLOCK_INIT;
	};

	static_assert(sizeof(LockCell) % CacheLineBytes == 0,
		"descriptor locks must not share cache lines");

	explicit DX12DescriptorHeapShardState(uint32_t maxSlots = 0)
		: maxSlots_(maxSlots),
		  versionPageCount_(maxSlots == 0 ? 0 : static_cast<uint32_t>(
			(static_cast<uint64_t>(maxSlots) + VersionPageSize - 1) /
			VersionPageSize))
	{
		for (LockCell &cell : locks_)
			InitializeSRWLock(&cell.value);
		if (versionPageCount_ == 0)
			return;
		versionPages_ =
			std::make_unique<std::atomic<VersionPage*>[]>(versionPageCount_);
		for (uint32_t index = 0; index < versionPageCount_; ++index)
			versionPages_[index].store(nullptr, std::memory_order_relaxed);
	}

	~DX12DescriptorHeapShardState()
	{
		if (!versionPages_)
			return;
		for (uint32_t index = 0; index < versionPageCount_; ++index)
			delete versionPages_[index].load(std::memory_order_relaxed);
	}

	DX12DescriptorHeapShardState(const DX12DescriptorHeapShardState&) = delete;
	DX12DescriptorHeapShardState &operator=(
		const DX12DescriptorHeapShardState&) = delete;

	static constexpr size_t ShardForSlot(uint32_t slot)
	{
		return static_cast<size_t>(slot) & (ShardCount - 1);
	}

	SRWLOCK *LockForSlot(uint32_t slot)
	{
		return &locks_[ShardForSlot(slot)].value;
	}

	void LockAllExclusive()
	{
		for (LockCell &cell : locks_)
			AcquireSRWLockExclusive(&cell.value);
	}

	void UnlockAllExclusive()
	{
		for (size_t i = ShardCount; i > 0; --i)
			ReleaseSRWLockExclusive(&locks_[i - 1].value);
	}

	bool IsActive() const
	{
		return active_.load(std::memory_order_acquire);
	}

	void Deactivate()
	{
		active_.store(false, std::memory_order_release);
	}

	// Dense slot arrays are materialized once per heap lifetime and then
	// stable; a release store here publishes that readiness so lock-free
	// writers/readers can touch slots/occupied without the shard locks.
	bool IsMaterialized() const
	{
		return materialized_.load(std::memory_order_acquire);
	}

	void MarkMaterialized()
	{
		materialized_.store(true, std::memory_order_release);
	}

	const SRWLOCK *LockForSlot(uint32_t slot) const
	{
		return &locks_[ShardForSlot(slot)].value;
	}

	void ResetVersions()
	{
		version_.store(1, std::memory_order_release);
		if (!versionPages_)
			return;
		for (uint32_t pageIndex = 0;
				pageIndex < versionPageCount_; ++pageIndex) {
			VersionPage *page = versionPages_[pageIndex].load(
				std::memory_order_acquire);
			if (!page)
				continue;
			for (typename VersionPage::SlotState &slot : page->slots) {
				slot.version.store(2, std::memory_order_release);
				slot.valueId.store(0, std::memory_order_release);
				slot.flatValueId.store(0, std::memory_order_release);
				slot.flatVersion.store(2, std::memory_order_release);
			}
		}
	}

	void BumpVersion(uint32_t slot)
	{
		PublishSlotValue(slot, 0);
	}

	void PublishSlotValue(uint32_t slot, uint64_t valueId)
	{
		PublishSlotIdentity(slot, valueId);
		BumpHeapVersion();
	}

	// Range copies publish every slot identity, but consumers only require the
	// heap-wide invalidation token to change once for the completed API call.
	// Splitting these operations removes one contended atomic RMW per copied
	// descriptor without weakening per-slot identity validation.
	void PublishSlotIdentity(uint32_t slot, uint64_t valueId)
	{
		VersionPage *page = EnsureVersionPage(slot);
		if (page) {
			typename VersionPage::SlotState &state =
				page->slots[slot & (VersionPageSize - 1)];
			// Odd means a publication is in progress. Readers only accept the
			// value when the same even version brackets the Value ID load. Slot
			// writers are serialized by the owning shard lock, so the second
			// contended atomic RMW formerly used here bought no correctness. An
			// exchange starts publication and the release store completes it.
			const uint64_t previous = state.version.fetch_or(
				1ull, std::memory_order_acq_rel);
			state.valueId.store(valueId, std::memory_order_relaxed);
			// Versions are initialized even and only one writer owns this slot.
			// Advancing from the previous even generation preserves cache-key
			// invalidation, including wraparound's even parity.
			const uint64_t completedVersion = (previous + 2ull) & ~1ull;
			state.version.store(completedVersion,
				std::memory_order_release);
			// Flat snapshot: written after the version store completes so a
			// reader that observes flatVersion == version also observes the
			// matching flatValueId (same thread store order; relaxed loads
			// on x86 observe stores in order).
			state.flatValueId.store(valueId, std::memory_order_relaxed);
			state.flatVersion.store(
				completedVersion, std::memory_order_relaxed);
		}
	}

	// Lock-free O(1) check: when the slot's version is unchanged since the
	// last publication, flatValueIds[slot] is the authoritative valueId.
	// Returns false when the slot was never published or has been republished
	// (the caller then falls back to the seqlock path).
	bool ReadFlatValueId(uint32_t slot, uint64_t *valueId) const
	{
		if (!valueId || !versionPages_ || slot >= maxSlots_)
			return false;
		VersionPage *page = versionPages_[slot / VersionPageSize].load(
			std::memory_order_acquire);
		if (!page)
			return false;
		const typename VersionPage::SlotState &state =
			page->slots[slot & (VersionPageSize - 1)];
		const uint64_t flatVersion =
			state.flatVersion.load(std::memory_order_relaxed);
		if (flatVersion == 0)
			return false;
		if (state.version.load(std::memory_order_acquire) !=
			flatVersion)
			return false;
		*valueId =
			state.flatValueId.load(std::memory_order_relaxed);
		return true;
	}

	void BumpHeapVersion()
	{
		version_.fetch_add(1, std::memory_order_release);
	}

	struct SlotIdentity
	{
		uint64_t version = 0;
		uint64_t valueId = 0;
	};

	bool ObserveSlotIdentity(uint32_t slot, SlotIdentity *identity) const
	{
		if (!identity || !versionPages_ || slot >= maxSlots_)
			return false;
		VersionPage *page = versionPages_[slot / VersionPageSize].load(
			std::memory_order_acquire);
		if (!page) {
			identity->version = 2;
			identity->valueId = 0;
			return true;
		}
		const typename VersionPage::SlotState &state =
			page->slots[slot & (VersionPageSize - 1)];
		const uint64_t before = state.version.load(
			std::memory_order_acquire);
		if ((before & 1) != 0)
			return false;
		const uint64_t valueId = state.valueId.load(
			std::memory_order_acquire);
		const uint64_t after = state.version.load(
			std::memory_order_acquire);
		if (before != after || (after & 1) != 0)
			return false;
		identity->version = after;
		identity->valueId = valueId;
		return true;
	}

	// Creation hooks frequently publish the same compact descriptor value to the
	// same slot thousands of times per frame.  Reject those writes from the
	// seqlock publication itself before taking the destination shard exclusively
	// or touching the sparse descriptor store.
	bool PublishedSlotValueEquals(uint32_t slot, uint64_t valueId) const
	{
		if (valueId == 0)
			return false;
		SlotIdentity identity;
		return ObserveSlotIdentity(slot, &identity) &&
			identity.valueId == valueId;
	}

	uint64_t ReadSlotVersion(uint32_t slot) const
	{
		if (!versionPages_ || slot >= maxSlots_)
			return version_.load(std::memory_order_acquire);
		VersionPage *page = versionPages_[slot / VersionPageSize].load(
			std::memory_order_acquire);
		return page ?
			page->slots[slot & (VersionPageSize - 1)].version.load(
				std::memory_order_acquire) : 2;
	}

	uint64_t ReadVersion() const
	{
		return version_.load(std::memory_order_acquire);
	}

private:
	static constexpr uint32_t VersionPageSize = 256;
	static_assert((VersionPageSize & (VersionPageSize - 1)) == 0,
		"descriptor version page size must be a power of two");

	// Per-slot publication state is interleaved so that one cache line holds
	// the complete identity of two adjacent slots. The previous layout kept
	// four parallel arrays, so every publish/validate touched up to four
	// separate lines, and descriptor copy storms bounced all of them between
	// cores.
	struct alignas(CacheLineBytes) VersionPage
	{
		struct SlotState
		{
			std::atomic<uint64_t> version;
			std::atomic<uint64_t> valueId;
			// Flat post-publication snapshots, written after the seqlock
			// version store completes. Readers compare flatVersion against
			// the live version: when they match, the slot has not been
			// republished since the snapshot, so flatValueId is the
			// authoritative current valueId. This gives descriptor copy
			// tracking (RecordCopiedDescriptorsSimple)
			// a lock-free O(1) "is dest already identical to src" check
			// indexed by slot - the same design as DXVK's
			// views[slot] != resView compare - without the seqlock's
			// three-load read protocol or any hash-table capacity limits.
			std::atomic<uint64_t> flatValueId;
			std::atomic<uint64_t> flatVersion;
		};
		static_assert(sizeof(SlotState) * 2 == CacheLineBytes,
			"two slot states should fill one cache line");
		std::array<SlotState, VersionPageSize> slots;

		VersionPage()
		{
			for (SlotState &slot : slots) {
				slot.version.store(2, std::memory_order_relaxed);
				slot.valueId.store(0, std::memory_order_relaxed);
				slot.flatValueId.store(0, std::memory_order_relaxed);
				slot.flatVersion.store(2, std::memory_order_relaxed);
			}
		}
	};

	VersionPage *EnsureVersionPage(uint32_t slot)
	{
		if (!versionPages_ || slot >= maxSlots_)
			return nullptr;
		std::atomic<VersionPage*> &cell =
			versionPages_[slot / VersionPageSize];
		VersionPage *page = cell.load(std::memory_order_acquire);
		if (page)
			return page;
		VersionPage *candidate = new VersionPage();
		VersionPage *expected = nullptr;
		if (cell.compare_exchange_strong(
				expected, candidate,
				std::memory_order_release,
				std::memory_order_acquire))
			return candidate;
		delete candidate;
		return expected;
	}

	std::atomic<bool> materialized_{false};
	std::atomic<bool> active_{true};
	std::array<LockCell, ShardCount> locks_;
	// Consumers only need a non-zero token that changes after any Descriptor
	// State write. Keeping 1024 per-shard counters made every cache validation
	// read 1024 cache lines; one heap-wide token makes that hot read O(1).
	alignas(CacheLineBytes) std::atomic<uint64_t> version_{1};
	uint32_t maxSlots_ = 0;
	uint32_t versionPageCount_ = 0;
	std::unique_ptr<std::atomic<VersionPage*>[]> versionPages_;
};


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
	// Descriptor writes are short and command recording normally has only a
	// handful of producer threads.  1024 cache-line-sized locks gave every heap
	// a 64 KiB lock working set; sequential descriptor rings consequently
	// touched a different cache line for virtually every CopyDescriptors slot.
	// 64 stripes still provide substantially more concurrency than the wrapper
	// can use while keeping the complete lock set in a small L1/L2 footprint.
	static constexpr uint32_t ShardCount = 64;
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


