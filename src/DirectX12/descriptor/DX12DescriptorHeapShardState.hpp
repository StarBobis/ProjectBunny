#pragma once

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

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
			for (std::atomic<uint64_t> &slotVersion : page->versions)
				slotVersion.store(1, std::memory_order_release);
		}
	}

	void BumpVersion(uint32_t slot)
	{
		VersionPage *page = EnsureVersionPage(slot);
		if (page)
			page->versions[slot & (VersionPageSize - 1)].fetch_add(
				1, std::memory_order_release);
		version_.fetch_add(1, std::memory_order_release);
	}

	uint64_t ReadSlotVersion(uint32_t slot) const
	{
		if (!versionPages_ || slot >= maxSlots_)
			return version_.load(std::memory_order_acquire);
		VersionPage *page = versionPages_[slot / VersionPageSize].load(
			std::memory_order_acquire);
		return page ?
			page->versions[slot & (VersionPageSize - 1)].load(
				std::memory_order_acquire) : 1;
	}

	uint64_t ReadVersion() const
	{
		return version_.load(std::memory_order_acquire);
	}

private:
	static constexpr uint32_t VersionPageSize = 256;
	static_assert((VersionPageSize & (VersionPageSize - 1)) == 0,
		"descriptor version page size must be a power of two");

	struct VersionPage
	{
		std::array<std::atomic<uint64_t>, VersionPageSize> versions;

		VersionPage()
		{
			for (std::atomic<uint64_t> &version : versions)
				version.store(1, std::memory_order_relaxed);
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
