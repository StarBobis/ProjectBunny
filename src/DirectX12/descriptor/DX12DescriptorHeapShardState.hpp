#pragma once

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

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

	struct alignas(CacheLineBytes) VersionCell
	{
		std::atomic<uint64_t> value{1};
	};

	static_assert(sizeof(LockCell) % CacheLineBytes == 0,
		"descriptor locks must not share cache lines");
	static_assert(sizeof(VersionCell) % CacheLineBytes == 0,
		"descriptor versions must not share cache lines");

	DX12DescriptorHeapShardState()
	{
		for (LockCell &cell : locks_)
			InitializeSRWLock(&cell.value);
	}

	static constexpr size_t ShardForSlot(uint32_t slot)
	{
		return static_cast<size_t>(slot) & (ShardCount - 1);
	}

	SRWLOCK *LockForSlot(uint32_t slot)
	{
		return &locks_[ShardForSlot(slot)].value;
	}

	const SRWLOCK *LockForSlot(uint32_t slot) const
	{
		return &locks_[ShardForSlot(slot)].value;
	}

	void ResetVersions()
	{
		for (VersionCell &cell : versions_)
			cell.value.store(1, std::memory_order_release);
	}

	void BumpVersion(uint32_t slot)
	{
		versions_[ShardForSlot(slot)].value.fetch_add(
			1, std::memory_order_release);
	}

	uint64_t ReadVersion() const
	{
		uint64_t aggregate = 0;
		for (const VersionCell &cell : versions_)
			aggregate += cell.value.load(std::memory_order_acquire);
		return aggregate;
	}

	uint64_t ReadShardVersion(uint32_t slot) const
	{
		return versions_[ShardForSlot(slot)].value.load(
			std::memory_order_acquire);
	}

private:
	std::array<LockCell, ShardCount> locks_;
	std::array<VersionCell, ShardCount> versions_;
};
