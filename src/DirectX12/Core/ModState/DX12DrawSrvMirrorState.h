#pragma once

// Draw-time SRV replacement mirror state: the deferred-restore bookkeeping and
// the persistent cross-draw mirror cache.
//
// Split out of ModRuntime/DX12DrawSrvReplacementUtils.h and
// ModRuntime/DX12BundleSrvMirror.h, which are two fragments spliced into
// ModRuntime/DX12ModRuntimeInternal.h at fixed lines. Both of them - and the
// bundle mirror in particular - need to see this state, and a fragment can only
// use what precedes its splice point. Keeping the data here, in a header both
// can include, removes that ordering dependency; the behaviour that operates on
// it stays where it was.
//
// Include Core/DX12ModState.h (or its umbrella Core/DX12Core.h).
#include "../DX12CommandFlow.h"

#include <atomic>
#include <unordered_map>
#include <vector>

// Retire a descriptor heap through the pre-skin fence drain: the heap's
// ownership moves there until no in-flight submission references it. Declared
// here because DX12BundleMirrorSession::Clear below hands its session heap to
// the drain, and this header must be includable on its own. Defined in
// ModRuntime/DX12PreSkinRuntime.cpp.
void RetirePreSkinDescriptorHeap(ID3D12DescriptorHeap *heap);

// Record that a command list's submission now references a descriptor heap the
// mirror retains, so the retire drain cannot free it while the list is in
// flight. Defined in ModRuntime/DX12PreSkinRuntime.cpp; declared here for the
// same reason as RetirePreSkinDescriptorHeap.
void RetainPreSkinDescriptorHeapForCommandList(
	ID3D12GraphicsCommandList *commandList, ID3D12DescriptorHeap *heap);

// Reserve a descriptor range in the pre-skin ring for a transient mirror, or
// return the currently bound range when the bound heap already has room.
// Defined in ModRuntime/DX12PreSkinRuntime.cpp; declared here because the
// draw-SRV replacement path allocates its mirror ranges through it.
bool EnsurePreSkinDescriptorRingLocked(
	ID3D12Device *device, ID3D12GraphicsCommandList *commandList,
	UINT requiredDescriptors,
	D3D12_CPU_DESCRIPTOR_HANDLE *cpuBase,
	D3D12_GPU_DESCRIPTOR_HANDLE *gpuBase,
	UINT *increment,
	ID3D12DescriptorHeap **heapOut);

// ---------------------------------------------------------------------------
// Deferred draw-SRV restore (lazy heap switch).
//
// ApplyPending switches the command list to a private mirror heap and rebinds
// every root table for a patched draw; the stock Restore immediately switched
// back to the game's heap and rebound every table again. That is two
// SetDescriptorHeaps round trips plus two full root-table rebinds per modded
// draw, while Microsoft recommends setting descriptor heaps as few times as
// possible per command list (a heap switch invalidates every root table
// binding and can stall the GPU):
// https://learn.microsoft.com/en-us/windows/win32/direct3d12/setting-descriptor-heaps
//
// Restore therefore now performs only the *logical* half immediately (the
// shadow trackers return to the game's bindings) and defers the GPU-side
// half: the mirror heap and patched tables stay bound on the list. The
// deferred entry is flushed (physically restored) lazily before any command
// that observes descriptor-heap state:
//   - original/replacement draws and dispatches (DX12IaReplacementDrawExecutor),
//   - game dispatches and ExecuteIndirect / ExecuteBundle,
//   - ClearUnorderedAccessView* (GPU handle resolved against the bound heap),
//   - every game binding mutation (root tables/signatures/constants),
//   - command-list Reset/Unregister/ClearState (quiet drop, no GPU commands).
// A consecutive patched draw whose plan fingerprint matches the pending entry
// reuses the still-bound mirror and emits nothing at all.
// ---------------------------------------------------------------------------

class DX12DrawSrvPendingRestore
{
public:
	// Set while a consecutive ApplyPending has borrowed the still-bound
	// mirror for the in-flight draw; flush probes must leave it untouched.
	bool borrowedByApply = false;
	// Original game heaps, AddRef'd while pending: the physical restore may
	// rebind them after the game's own binding scope has moved on.
	ID3D12DescriptorHeap *cbvSrvUavHeap = nullptr;
	ID3D12DescriptorHeap *samplerHeap = nullptr;
	// Mirror ring heap currently bound on the list. Not owned here; the
	// ring retains it for the command list and only rewinds offsets once no
	// unsubmitted list references the heap anymore (this entry's list
	// holds such a reference until submission).
	ID3D12DescriptorHeap *patchHeap = nullptr;
	UINT64 planFingerprint = 0;
	UINT64 reloadGeneration = 0;
	// Slim view of the game's bindings; see DX12DrawSrvBindingSnapshot.
	DX12DrawSrvBindingSnapshot originalBindings;
};

// Sharded pending table (per command list), same contract as
// DX12PreSkinReplacementShard: every access targets exactly one list key, so
// the table is sharded by list pointer with a per-shard lock and mutation
// generation for the TLS negative cache in FlushPendingRestore.
class DX12DrawSrvPendingRestoreShard
{
public:
	SRWLOCK lock = SRWLOCK_INIT;
	std::unordered_map<ID3D12GraphicsCommandList*, DX12DrawSrvPendingRestore> entries;
	std::atomic<UINT64> mutationGeneration{0};
};

inline DX12DrawSrvPendingRestoreShard gDrawSrvPendingRestoreShards[64];
// Count of pending entries across all shards; readers only test zero. The
// count also drives FLAG_DRAWSRV_PENDING_RESTORE in HookWorkFlags so the
// per-draw fast paths notice a pending restore with no extra load.
inline volatile LONG gDrawSrvPendingRestoreCount = 0;

inline DX12DrawSrvPendingRestoreShard &DrawSrvPendingRestoreShardFor(
	ID3D12GraphicsCommandList *commandList)
{
	constexpr size_t kShardCount =
		sizeof(gDrawSrvPendingRestoreShards) / sizeof(gDrawSrvPendingRestoreShards[0]);
	const UINT_PTR key = reinterpret_cast<UINT_PTR>(commandList);
	return gDrawSrvPendingRestoreShards[(key >> 4) % kShardCount];
}

// ---------------------------------------------------------------------------
// Persistent cross-draw mirror cache (PERF: rebind instead of re-mirror).
//
// The ring-based mirror path below re-copies every graphics table on every
// patched draw whose still-bound-mirror reuse missed - the DX11 equivalent
// (ps-t0 = X) is a single PSSetShaderResources call. Most of those copies are
// redundant: the plan fingerprint already covers each source heap's content
// version, so an identical fingerprint means the source descriptors are
// byte-identical to what an earlier apply mirrored. That earlier mirror can
// simply be re-bound.
//
// A plan earns a dedicated shader-visible heap after being sighted twice
// (identical fingerprint on two separate applies): single-sighting plans stay
// on the transient ring so games that rewrite their descriptor arena every
// frame (fingerprint changes every draw) never pay heap-creation churn, while
// static or slowly-changing tables - exactly the bindless/large-table cases
// that are most expensive to re-mirror - promote on their second draw and
// cost only SetDescriptorHeaps + root-table rebinds afterwards.
//
// Lifetime: the cache holds one reference per heap; every command list that
// binds a cached heap additionally retains it through
// RetainPreSkinDescriptorHeapForCommandList, so eviction (clear-all on
// overflow or reload-generation change) can never free a heap a not-yet-
// submitted list still references - the retired-heap fence drain owns that
// final release, same as ring heaps.
// ---------------------------------------------------------------------------
class DX12DrawSrvMirrorCacheEntry
{
public:
ID3D12DescriptorHeap *heap = nullptr;
// One GPU handle per plan table, in plan order; sampler tables keep
// their original game handle (they are never mirrored).
std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> tables;

void Clear()
{
if (heap)
heap->Release();
heap = nullptr;
tables.clear();
}
};

inline SRWLOCK gDrawSrvMirrorCacheLock = SRWLOCK_INIT;
inline std::unordered_map<UINT64, DX12DrawSrvMirrorCacheEntry> gDrawSrvMirrorCache;
inline UINT64 gDrawSrvMirrorCacheGeneration = 0;
// Promotion sightings per plan fingerprint; reset on reload generation and
// capped so arena-churn fingerprints cannot grow it without bound.
inline std::unordered_map<UINT64, UINT> gDrawSrvMirrorPlanSightings;
inline constexpr size_t MaxDrawSrvMirrorCacheEntries = 32;
inline constexpr size_t MaxDrawSrvMirrorPlanSightings = 256;
inline constexpr UINT DrawSrvMirrorPromotionSightings = 2;

// Sizing limits for the pre-skin descriptor ring, which the draw-SRV mirror
// ring shares. Declared here so the replacement paths can name them without
// depending on where the ring state sits in the mod-runtime internal header.
inline constexpr UINT PreSkinDescriptorRingInitialCapacity = 4096;

inline constexpr UINT PreSkinDescriptorRingMaximumCapacity = 1000000;

// ---------------------------------------------------------------------------
// Bundle draw-SRV mirror session state.
//
// A bundle cannot switch to a private descriptor heap by itself, so every
// CBV/SRV/UAV root-table bind recorded into a bundle is mirrored into a
// session-owned shader-visible heap and the emitted handle is rewritten to the
// mirror copy. Sessions are keyed by the bundle list and die with Reset /
// Unregister. See ModRuntime/DX12BundleSrvMirror.h for the full design.
//
// The state lives here, next to the direct-list mirror state it parallels, for
// the same reason: both fragments that use it - the bundle mirror and the
// direct-list replacement path that calls into it - can include one header
// instead of depending on which is spliced in first.
// ---------------------------------------------------------------------------
class DX12BundleMirrorTable
{
public:
	UINT64 gpuStart = 0;
	SIZE_T cpuStart = 0;
	ID3D12DescriptorHeap *sourceHeap = nullptr;
	UINT64 sourceHeapVersion = 0;
	UINT count = 0;
	UINT mirrorOffset = 0;
};

class DX12BundlePatchOverlay
{
public:
	UINT slot = 0;
	D3D12_CPU_DESCRIPTOR_HANDLE replacementCpu = {};
};

class DX12BundlePatchCopy
{
public:
	SIZE_T cpuStart = 0;
	ID3D12DescriptorHeap *sourceHeap = nullptr;
	UINT64 sourceHeapVersion = 0;
	UINT count = 0;
	UINT mirrorOffset = 0;
	UINT overlayBegin = 0;
	UINT overlayEnd = 0;
};

class DX12BundleMirrorSession
{
public:
	ID3D12Device *device = nullptr;					// AddRef'd
	ID3D12DescriptorHeap *heap = nullptr;			// AddRef'd; retired on Clear
	UINT capacity = 0;
	UINT offset = 0;
	UINT increment = 0;
	D3D12_CPU_DESCRIPTOR_HANDLE cpuBase = {};
	D3D12_GPU_DESCRIPTOR_HANDLE gpuBase = {};
	ID3D12DescriptorHeap *samplerHeap = nullptr;	// AddRef'd when set
	bool sawSetDescriptorHeaps = false;
	bool overflowed = false;
	UINT64 graphicsBindMask = 0;
	UINT64 computeBindMask = 0;
	UINT64 reloadGeneration = 0;
	std::unordered_map<UINT64, DX12BundleMirrorTable> tables;
	std::unordered_map<UINT64, size_t> patchCopyByKey;
	std::vector<DX12BundlePatchCopy> patchCopies;
	std::vector<DX12BundlePatchOverlay> overlays;

	void Clear()
	{
		ID3D12DescriptorHeap *oldHeap = heap;
		ID3D12DescriptorHeap *oldSampler = samplerHeap;
		ID3D12Device *oldDevice = device;
		heap = nullptr;
		samplerHeap = nullptr;
		device = nullptr;
		capacity = 0;
		offset = 0;
		increment = 0;
		cpuBase = {};
		gpuBase = {};
		sawSetDescriptorHeaps = false;
		overflowed = false;
		graphicsBindMask = 0;
		computeBindMask = 0;
		reloadGeneration = 0;
		tables.clear();
		patchCopyByKey.clear();
		patchCopies.clear();
		overlays.clear();
		if (oldSampler)
			oldSampler->Release();
		if (oldDevice)
			oldDevice->Release();
		// Ownership of the session's reference moves to the fence drain; any
		// parent list that executed the bundle still holds its own retention.
		if (oldHeap)
			RetirePreSkinDescriptorHeap(oldHeap);
	}
};

class DX12BundleMirrorSessionShard
{
public:
	SRWLOCK lock = SRWLOCK_INIT;
	std::unordered_map<ID3D12GraphicsCommandList*, DX12BundleMirrorSession> entries;
};

inline DX12BundleMirrorSessionShard gBundleMirrorSessionShards[16];
// Count of live sessions across all shards; readers only test zero so the
// per-bind rewrite and ExecuteBundle stay at one volatile read when no bundle
// was ever recorded with a session.
inline volatile LONG gBundleMirrorSessionCount = 0;

inline DX12BundleMirrorSessionShard &BundleMirrorSessionShardFor(
	ID3D12GraphicsCommandList *commandList)
{
	constexpr size_t kShardCount =
		sizeof(gBundleMirrorSessionShards) / sizeof(gBundleMirrorSessionShards[0]);
	const UINT_PTR key = reinterpret_cast<UINT_PTR>(commandList);
	return gBundleMirrorSessionShards[(key >> 4) % kShardCount];
}
