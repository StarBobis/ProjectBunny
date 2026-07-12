#include "DX12SparseDescriptorStore.hpp"

#include <array>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <vector>

static void Require(bool condition)
{
	if (!condition)
		std::abort();
}

int main()
{
	DX12SparseDescriptorStore<unsigned long long> store;
	static_assert(DX12SparseDescriptorStore<unsigned long long>::ShardCount == 1024,
		"descriptor state lock striping changed unexpectedly");
	Require(store.Empty());

	store.Store(1, 11);
	store.Store(4, 44);
	Require(store.Size() == 2);
	Require(store.Find(0) == nullptr);
	Require(store.Find(1) && *store.Find(1) == 11);

	// Replacement preserves current-value semantics instead of retaining
	// descriptor event history.
	store.Store(1, 12);
	Require(store.Size() == 2);
	Require(*store.Find(1) == 12);
	bool inserted = false;
	unsigned long long *existing = store.FindOrInsertDefault(1, &inserted);
	Require(existing && !inserted && *existing == 12);
	unsigned long long *created = store.FindOrInsertDefault(99, &inserted);
	Require(created && inserted && *created == 0);
	*created = 99;
	Require(store.Find(99) && *store.Find(99) == 99);

	// Snapshot-before-write is the production CopyDescriptors overlap rule.
	store.Store(2, 22);
	store.Store(3, 33);
	std::vector<DX12SparseDescriptorStore<unsigned long long>::SnapshotEntry> snapshot;
	store.SnapshotRange(1, 3, &snapshot);
	Require(snapshot.size() == 3);
	for (unsigned i = 0; i < snapshot.size(); ++i) {
		if (snapshot[i].occupied)
			store.Store(2 + i, snapshot[i].value);
	}
	Require(*store.Find(2) == 12);
	Require(*store.Find(3) == 22);
	Require(*store.Find(4) == 33);

	Require(store.Erase(3));
	Require(!store.Erase(3));

	// Slots in different and repeated shards remain one logical store.
	store.Store(0, 100);
	store.Store(1024, 2024);
	store.Store(1025, 2025);
	Require(store.Size() == 7);
	Require(*store.Find(0) == 100);
	Require(*store.Find(1024) == 2024);
	Require(*store.Find(1025) == 2025);
	store.Clear();
	Require(store.Empty());

	// Production supplies one lock for each store shard. Exercise the same
	// contract with concurrent writers targeting one logical descriptor heap.
	DX12SparseDescriptorStore<unsigned long long> concurrent;
	std::array<std::mutex,
		DX12SparseDescriptorStore<unsigned long long>::ShardCount> locks;
	std::vector<std::thread> writers;
	constexpr unsigned WriterCount = 8;
	constexpr unsigned ValuesPerWriter = 4096;
	for (unsigned writer = 0; writer < WriterCount; ++writer) {
		writers.emplace_back([writer, &concurrent, &locks, ValuesPerWriter]() {
			for (unsigned i = 0; i < ValuesPerWriter; ++i) {
				const uint32_t slot = writer * ValuesPerWriter + i;
				const uint32_t shard = concurrent.ShardForSlot(slot);
				std::lock_guard<std::mutex> guard(locks[shard]);
				concurrent.Store(slot, slot + 1ull);
			}
		});
	}
	for (std::thread &writer : writers)
		writer.join();
	Require(concurrent.Size() == WriterCount * ValuesPerWriter);
	for (unsigned writer = 0; writer < WriterCount; ++writer) {
		const uint32_t slot = writer * ValuesPerWriter + ValuesPerWriter - 1;
		Require(concurrent.Find(slot) && *concurrent.Find(slot) == slot + 1ull);
	}
	return 0;
}
