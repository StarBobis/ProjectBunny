#include "DX12Infrastructure.hpp"

#include <array>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

static void Require(bool condition)
{
	if (!condition)
		std::abort();
}

int main()
{
	DX12SparseDescriptorStore<unsigned long long> store;
	store.Initialize(4096);
	static_assert(DX12SparseDescriptorStore<unsigned long long>::ShardCount == 64,
		"descriptor state lock striping changed unexpectedly");
	Require(store.Empty());
	Require(store.AllocatedPageCount() == 0);

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
	Require(store.AllocatedPageCount() == 1);

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
	Require(store.AllocatedPageCount() == 2);
	store.Clear();
	Require(store.Empty());
	Require(store.AllocatedPageCount() == 0);

	// Production supplies one lock for each store shard. Exercise the same
	// contract with concurrent writers targeting one logical descriptor heap.
	DX12SparseDescriptorStore<unsigned long long> concurrent;
	concurrent.Initialize(8 * 4096);
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

	// Heap records are moved into the global deque. Page ownership and stable
	// values must follow both move construction and move assignment.
	DX12SparseDescriptorStore<unsigned long long> moveSource;
	moveSource.Initialize(1024);
	moveSource.Store(511, 0x511);
	DX12SparseDescriptorStore<unsigned long long> moveConstructed(
		std::move(moveSource));
	Require(moveSource.Empty());
	Require(moveConstructed.Find(511) &&
		*moveConstructed.Find(511) == 0x511);
	DX12SparseDescriptorStore<unsigned long long> moveAssigned;
	moveAssigned.Initialize(1024);
	moveAssigned.Store(1, 1);
	moveAssigned = std::move(moveConstructed);
	Require(moveConstructed.Empty());
	Require(moveAssigned.Size() == 1);
	Require(moveAssigned.Find(511) && *moveAssigned.Find(511) == 0x511);
	return 0;
}
