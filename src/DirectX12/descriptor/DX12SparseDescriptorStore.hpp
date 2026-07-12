#pragma once

#include <stdint.h>

#include <array>
#include <unordered_map>
#include <utility>
#include <vector>

// Current-value storage used by Descriptor State. It deliberately contains
// no locking: the owning descriptor heap supplies the concurrency policy.
// Keeping this Module independent makes descriptor stream replay the real
// test Seam used by production.
template <typename Value>
class DX12SparseDescriptorStore
{
public:
	// Sequential descriptor slots are deliberately spread across shards. The
	// caller supplies one lock per shard, so unrelated Create*View and copy
	// operations on the same million-entry heap no longer serialize.
	// Real games can issue tens of thousands of Create*View calls and several
	// thousand descriptor copies per frame from multiple recording threads.
	// With only 64 shards, unrelated slots repeatedly queued behind the same
	// SRWLOCK and unordered_map even though the API operations were independent.
	// 1024 keeps the store sparse while reducing that false contention by 16x.
	static constexpr uint32_t ShardCount = 1024;

	static constexpr uint32_t ShardForSlot(uint32_t slot)
	{
		return slot & (ShardCount - 1);
	}

	struct SnapshotEntry
	{
		bool occupied = false;
		Value value = {};
	};

	const Value *Find(uint32_t slot) const
	{
		const Map &values = values_[ShardForSlot(slot)];
		auto found = values.find(slot);
		return found == values.end() ? nullptr : &found->second;
	}

	Value *Find(uint32_t slot)
	{
		Map &values = values_[ShardForSlot(slot)];
		auto found = values.find(slot);
		return found == values.end() ? nullptr : &found->second;
	}

	// Returns the existing value or inserts one default value with a single
	// hash-table probe. Callers already serialize the owning shard, so the
	// returned node remains stable for the duration of the operation.
	Value *FindOrInsertDefault(uint32_t slot, bool *inserted)
	{
		Map &values = values_[ShardForSlot(slot)];
		auto result = values.try_emplace(slot);
		if (inserted)
			*inserted = result.second;
		return &result.first->second;
	}

	bool Store(uint32_t slot, Value value)
	{
		Map &values = values_[ShardForSlot(slot)];
		auto result = values.insert_or_assign(slot, std::move(value));
		return result.second;
	}

	bool Erase(uint32_t slot)
	{
		return values_[ShardForSlot(slot)].erase(slot) != 0;
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
		for (const Map &values : values_) {
			for (const auto &entry : values)
				visitor(entry.first, entry.second);
		}
	}

	size_t Size() const
	{
		size_t size = 0;
		for (const Map &values : values_)
			size += values.size();
		return size;
	}

	bool Empty() const
	{
		for (const Map &values : values_) {
			if (!values.empty())
				return false;
		}
		return true;
	}

	void Clear()
	{
		for (Map &values : values_)
			values.clear();
	}

private:
	using Map = std::unordered_map<uint32_t, Value>;
	std::array<Map, ShardCount> values_;
};
