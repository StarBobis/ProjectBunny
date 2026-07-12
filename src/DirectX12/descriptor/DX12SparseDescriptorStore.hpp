#pragma once

#include <stdint.h>

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
	struct SnapshotEntry
	{
		bool occupied = false;
		Value value = {};
	};

	const Value *Find(uint32_t slot) const
	{
		auto found = values_.find(slot);
		return found == values_.end() ? nullptr : &found->second;
	}

	Value *Find(uint32_t slot)
	{
		auto found = values_.find(slot);
		return found == values_.end() ? nullptr : &found->second;
	}

	void Store(uint32_t slot, Value value)
	{
		values_.insert_or_assign(slot, std::move(value));
	}

	bool Erase(uint32_t slot)
	{
		return values_.erase(slot) != 0;
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
		for (const auto &entry : values_)
			visitor(entry.first, entry.second);
	}

	size_t Size() const
	{
		return values_.size();
	}

	bool Empty() const
	{
		return values_.empty();
	}

	void Clear()
	{
		values_.clear();
	}

private:
	std::unordered_map<uint32_t, Value> values_;
};
