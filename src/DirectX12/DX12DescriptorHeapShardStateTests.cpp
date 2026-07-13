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

	Require(state.ReadVersion() == 1);
	Require(state.IsActive());
	Require(state.LockForSlot(0) == state.LockForSlot(1024));
	Require(state.LockForSlot(0) != state.LockForSlot(1));
	Require(reinterpret_cast<uintptr_t>(state.LockForSlot(1)) -
		reinterpret_cast<uintptr_t>(state.LockForSlot(0)) >= State::CacheLineBytes);

	state.BumpVersion(1);
	Require(state.ReadVersion() == 2);
	state.BumpVersion(1025);
	Require(state.ReadVersion() == 3);

	state.ResetVersions();
	Require(state.ReadVersion() == 1);
	State observedState(2048);
	State::SlotIdentity identity;
	Require(observedState.ObserveSlotIdentity(17, &identity));
	Require(identity.version == 2 && identity.valueId == 0);
	Require(!observedState.PublishedSlotValueEquals(17, 0x12345678));
	observedState.PublishSlotValue(17, 0x12345678);
	Require(observedState.ObserveSlotIdentity(17, &identity));
	Require(identity.version == 4 && identity.valueId == 0x12345678);
	Require(observedState.PublishedSlotValueEquals(17, 0x12345678));
	Require(!observedState.PublishedSlotValueEquals(17, 0x87654321));
	observedState.ResetVersions();
	Require(observedState.ObserveSlotIdentity(17, &identity));
	Require(identity.version == 2 && identity.valueId == 0);
	Require(!observedState.PublishedSlotValueEquals(17, 0x12345678));

	State publicationState(32);
	constexpr uint64_t PublicationCount = 100000;
	std::atomic<bool> publicationComplete{false};
	std::thread publisher(
		[&publicationState, &publicationComplete, PublicationCount]() {
		for (uint64_t valueId = 1; valueId <= PublicationCount; ++valueId)
			publicationState.PublishSlotValue(3, valueId);
		publicationComplete.store(true, std::memory_order_release);
	});
	while (!publicationComplete.load(std::memory_order_acquire)) {
		State::SlotIdentity publishedIdentity;
		if (!publicationState.ObserveSlotIdentity(3, &publishedIdentity))
			continue;
		Require((publishedIdentity.version & 1) == 0);
		Require(publishedIdentity.valueId ==
			(publishedIdentity.version - 2) / 2);
	}
	publisher.join();

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
		1 + static_cast<uint64_t>(WriterCount) * BumpsPerWriter);
	state.LockAllExclusive();
	state.Deactivate();
	state.UnlockAllExclusive();
	Require(!state.IsActive());
	return 0;
}
