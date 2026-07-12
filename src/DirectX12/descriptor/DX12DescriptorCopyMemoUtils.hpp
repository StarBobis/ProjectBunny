#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

// Thread-local proof cache for the overwhelmingly common one-slot
// CopyDescriptors path. A hit means neither the source nor destination shard
// changed since a previous completed copy of the same slot pair. It therefore
// proves that repeating the shadow-state copy cannot change Descriptor State.
//
// The cache stores only stable heap-record identities and atomic version
// values. It never borrows a Descriptor Value or sparse-page pointer, so heap
// destruction and Descriptor Value pool reuse cannot create an ABA/UAF path.
class DX12DescriptorCopyMemo
{
public:
	enum class Result : uint8_t
	{
		SourceMissingDestinationEmpty,
		Unchanged
	};

	struct Key
	{
		const void *sourceHeap = nullptr;
		const void *destinationHeap = nullptr;
		uint32_t sourceSlot = 0;
		uint32_t destinationSlot = 0;

		bool operator==(const Key &other) const
		{
			return sourceHeap == other.sourceHeap &&
				destinationHeap == other.destinationHeap &&
				sourceSlot == other.sourceSlot &&
				destinationSlot == other.destinationSlot;
		}
	};

	bool TryLoad(
		const Key &key,
		uint64_t sourceVersion,
		uint64_t destinationVersion,
		Result *result) const
	{
		if (!sets_ || !result)
			return false;
		const Set &set = sets_[SetIndex(key)];
		for (const Entry &entry : set.entries) {
			if (!entry.occupied || !(entry.key == key) ||
				entry.sourceVersion != sourceVersion ||
				entry.destinationVersion != destinationVersion)
				continue;
			*result = entry.result;
			return true;
		}
		return false;
	}

	void Store(
		const Key &key,
		uint64_t sourceVersion,
		uint64_t destinationVersion,
		Result result)
	{
		EnsureStorage();
		Set &set = sets_[SetIndex(key)];
		Entry *target = nullptr;
		for (Entry &entry : set.entries) {
			if (entry.occupied && entry.key == key) {
				target = &entry;
				break;
			}
			if (!entry.occupied && !target)
				target = &entry;
		}
		if (!target) {
			target = &set.entries[set.nextVictim];
			set.nextVictim = static_cast<uint8_t>(
				(set.nextVictim + 1) % Ways);
		}
		target->key = key;
		target->sourceVersion = sourceVersion;
		target->destinationVersion = destinationVersion;
		target->result = result;
		target->occupied = true;
	}

	void Clear()
	{
		sets_.reset();
	}

private:
	// The captured game presents roughly 65k distinct single-slot copy pairs per
	// frame. Keep twice that many two-way cells so a complete frame survives to
	// the next frame without turning lookup into a long associative scan.
	static constexpr size_t SetCount = 65536;
	static constexpr size_t Ways = 2;
	static_assert((SetCount & (SetCount - 1)) == 0,
		"copy memo set count must be a power of two");

	struct Entry
	{
		Key key;
		uint64_t sourceVersion = 0;
		uint64_t destinationVersion = 0;
		Result result = Result::SourceMissingDestinationEmpty;
		bool occupied = false;
	};

	struct Set
	{
		std::array<Entry, Ways> entries;
		uint8_t nextVictim = 0;
	};

	static uint64_t Mix(uint64_t value)
	{
		value ^= value >> 33;
		value *= 0xff51afd7ed558ccdull;
		value ^= value >> 33;
		value *= 0xc4ceb9fe1a85ec53ull;
		value ^= value >> 33;
		return value;
	}

	static size_t SetIndex(const Key &key)
	{
		uint64_t hash = Mix(static_cast<uint64_t>(
			reinterpret_cast<uintptr_t>(key.sourceHeap)));
		hash ^= Mix(static_cast<uint64_t>(
			reinterpret_cast<uintptr_t>(key.destinationHeap)) +
			0x9e3779b97f4a7c15ull);
		hash ^= Mix((static_cast<uint64_t>(key.sourceSlot) << 32) |
			key.destinationSlot);
		return static_cast<size_t>(hash) & (SetCount - 1);
	}

	void EnsureStorage()
	{
		if (!sets_)
			sets_ = std::make_unique<Set[]>(SetCount);
	}

	std::unique_ptr<Set[]> sets_;
};
