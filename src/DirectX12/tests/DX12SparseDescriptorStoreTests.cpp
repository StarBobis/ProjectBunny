#include "DX12SparseDescriptorStore.hpp"

#include <cstdlib>
#include <vector>

static void Require(bool condition)
{
	if (!condition)
		std::abort();
}

int main()
{
	DX12SparseDescriptorStore<unsigned long long> store;
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
	store.Clear();
	Require(store.Empty());
	return 0;
}
