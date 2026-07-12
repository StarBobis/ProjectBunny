#include "DX12DescriptorHeapShardState.hpp"

#include <cstdlib>
#include <cstdint>
#include <thread>
#include <vector>

static void Require(bool condition)
{
	if (!condition)
		std::abort();
}

int main()
{
	using State = DX12DescriptorHeapShardState<1024>;
	State state;

	Require(state.ReadVersion() == 1024);
	Require(state.IsActive());
	Require(state.ReadShardVersion(0) == 1);
	Require(state.ReadShardVersion(1) == 1);
	Require(state.LockForSlot(0) == state.LockForSlot(1024));
	Require(state.LockForSlot(0) != state.LockForSlot(1));
	Require(reinterpret_cast<uintptr_t>(state.LockForSlot(1)) -
		reinterpret_cast<uintptr_t>(state.LockForSlot(0)) >= State::CacheLineBytes);

	state.BumpVersion(1);
	Require(state.ReadShardVersion(0) == 1);
	Require(state.ReadShardVersion(1) == 2);
	Require(state.ReadVersion() == 1025);
	state.BumpVersion(1025);
	Require(state.ReadShardVersion(1) == 3);
	Require(state.ReadVersion() == 1026);

	state.ResetVersions();
	Require(state.ReadVersion() == 1024);

	constexpr uint32_t WriterCount = 8;
	constexpr uint32_t BumpsPerWriter = 4096;
	std::vector<std::thread> writers;
	for (uint32_t writer = 0; writer < WriterCount; ++writer) {
		writers.emplace_back([writer, &state, BumpsPerWriter]() {
			for (uint32_t i = 0; i < BumpsPerWriter; ++i)
				state.BumpVersion(writer * BumpsPerWriter + i);
		});
	}
	for (std::thread &writer : writers)
		writer.join();
	Require(state.ReadVersion() ==
		1024 + static_cast<uint64_t>(WriterCount) * BumpsPerWriter);
	state.LockAllExclusive();
	state.Deactivate();
	state.UnlockAllExclusive();
	Require(!state.IsActive());
	return 0;
}
