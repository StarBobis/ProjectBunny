#pragma once

// Published command-execution runtime snapshot.
//
// DX12ModRuntimeLoad.cpp builds one of these per reload and stores it
// atomically; recording threads read it instead of touching the mutable config
// maps. It used to be defined in ModRuntime/DX12ModRuntimeInternal.h - the faux
// translation unit that splices fragments together at fixed lines - so a header
// could only use it if it happened to be spliced in after that definition.
//
// Include Core/DX12ModState.h (or its umbrella Core/DX12Core.h).
#include "../Config/DX12Config.h"
#include "DX12CompiledPlans.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// A [TextureOverride] section that the draw-time matcher selected, carrying the
// few flags the executor needs. Every member is a scalar, so the implicit
// copy/move operations are correct and must not be written out: the four
// hand-written special members that used to be here copied these same eight
// fields one by one, which is exactly what the compiler generates, and their
// presence implied a resource-owning type that this is not.
class DX12TriggeredTextureOverride
{
public:
	const Bunny::TextureOverrideConfig *config = nullptr;
	uint32_t sectionId = 0;
	bool preRuntimeEffect = true;
	bool postRuntimeEffect = true;
	bool indexBuffer = false;
	uint32_t vertexSlot = 0;
	bool executeCommands = false;
	bool executeDrawActions = true;
};


class DX12CommandExecutionRuntimeSnapshot

{

public:
	bool anyTextureOverridePreRuntimeEffect = false;
	bool anyTextureOverridePostRuntimeEffect = false;
	std::unordered_map<std::wstring, bool> commandListRuntimeEffect;
	std::unordered_map<std::wstring, bool> commandListPreRuntimeEffect;
	std::unordered_map<std::wstring, bool> commandListPostRuntimeEffect;
	std::unordered_map<std::wstring, std::shared_ptr<const Bunny::CommandListConfig>>
		commandLists;
	std::unordered_map<uint32_t, std::shared_ptr<const Bunny::TextureOverrideConfig>>
		textureOverridesBySectionId;
	std::unordered_map<uint32_t, std::vector<DX12TriggeredTextureOverride>>
		textureOverridesByHash;
	std::vector<DX12TriggeredTextureOverride>
		textureOverridesWithoutDrawContext;
	std::vector<DX12TriggeredTextureOverride>
		textureOverridesGenericDrawContext;
	std::unordered_map<uint32_t, std::vector<DX12TriggeredTextureOverride>>
		textureOverridesByIndexCount;
	std::unordered_map<uint32_t, std::vector<DX12TriggeredTextureOverride>>
		textureOverridesByVertexCount;
	std::unordered_map<std::wstring, std::shared_ptr<const DX12CompiledCommandListPlan>>
		compiledCommandListPlans;
	std::unordered_map<std::wstring, std::shared_ptr<const DX12CompiledTextureOverridePlan>>
		compiledTextureOverridePlans;
	std::unordered_map<std::wstring, std::shared_ptr<const DX12CompiledShaderOverridePlan>>
		compiledShaderOverridePlans;
};

inline std::atomic<std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot>>
	gCommandExecutionRuntimeSnapshot;

// Publish generation for the snapshot above. Bumped (release) AFTER every
// store so readers can detect a reload with a single plain atomic UINT64
// load instead of touching the atomic<shared_ptr>.
inline std::atomic<UINT64> gCommandExecutionRuntimeSnapshotGeneration{ 0 };


// Thread-local snapshot cache. MSVC's std::atomic<std::shared_ptr> load is
// NOT lock-free: it goes through an internal spinlock pool, and every copy
// of the returned shared_ptr touches the shared control block
// (InterlockedIncrement/Decrement on one global cache line). IA texture
// override queries load this snapshot several times per draw per recording
// thread, which made both a global contention point - a DX11 single-thread
// assumption carried into the DX12 port. The snapshot only changes on mod
// reload, so keep one owning reference per thread and only pay the atomic
// shared_ptr load when the publish generation changes; steady-state reads
// are one acquire UINT64 load plus a raw pointer reuse, with no spinlock
// and no refcount traffic.
inline const std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> &
CommandExecutionRuntimeSnapshotTlsRef()
{	thread_local UINT64 tlsGeneration = 0;
	thread_local std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot>
		tlsSnapshot;

	const UINT64 published = gCommandExecutionRuntimeSnapshotGeneration.load(
		std::memory_order_acquire);
	if (published == tlsGeneration)
		return tlsSnapshot;
	// Reload path: the generation may be bumped again while the shared_ptr
	// load is in flight, so re-read it afterwards and retry on change to
	// keep the (generation, snapshot) pair consistent.
	for (;;) {
		const UINT64 before = gCommandExecutionRuntimeSnapshotGeneration.load(
			std::memory_order_acquire);
		std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> snapshot =
			gCommandExecutionRuntimeSnapshot.load(std::memory_order_acquire);
		const UINT64 after = gCommandExecutionRuntimeSnapshotGeneration.load(
			std::memory_order_acquire);
		if (before == after) {
			tlsSnapshot = std::move(snapshot);
			tlsGeneration = after;
			return tlsSnapshot;
		}
	}
}


// Borrowed view of the published snapshot for the calling thread, or nullptr
// when none is published. Cheap: the TLS reference above already holds one
// owning reference per thread, so this is a pointer read once the generation is
// unchanged.
inline const DX12CommandExecutionRuntimeSnapshot *
LoadCommandExecutionRuntimeSnapshot()
{
	return CommandExecutionRuntimeSnapshotTlsRef().get();
}
