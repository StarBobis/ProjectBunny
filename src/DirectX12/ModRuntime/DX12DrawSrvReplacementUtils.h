#pragma once

// DX12DrawSrvReplacementUtils.h - part of DX12ModRuntimeInternal.h, included from it at the split position.
// Not meant to be included directly.


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

class DX12DrawSrvPatchRequest
{
public:
	DX12CurrentShaderResourceBinding binding;
	DX12LoadedResource *resource = nullptr;
	bool compute = false;
	const char *source = "";
	D3D12_CPU_DESCRIPTOR_HANDLE replacementCpu = {};
	DXGI_FORMAT replacementViewFormat = DXGI_FORMAT_UNKNOWN;
};


class DX12DrawSrvPatchTable
{
public:
	bool compute = false;
	bool sampler = false;
	UINT rootParameterIndex = 0;
	SIZE_T cpuStart = 0;
	D3D12_GPU_DESCRIPTOR_HANDLE gpuStart = {};
	ID3D12DescriptorHeap *heap = nullptr;
	UINT increment = 0;
	UINT count = 0;
	UINT tempOffset = 0;
	// Source-heap content version at plan time. Descriptor contents are not
	// covered by the rest of the plan fingerprint; a game CopyDescriptors
	// into the same table location between two draws must not reuse the
	// stale mirror.
	UINT64 heapVersion = 0;
};


class DX12DrawSrvRootPlanTemplate
{
public:
	bool valid = false;
	bool sampler = false;
	bool dynamicCount = false;
	UINT fixedCount = 0;
};


class DX12DrawSrvSignaturePlanTemplate
{
public:
	ID3D12RootSignature *rootSignature = nullptr;
	UINT64 rootSignatureHash = 0;
	DX12DrawSrvRootPlanTemplate roots[MaxRootParameters] = {};
};


// TLS negative-cache entry for the flush probe (same contract as the
// pre-skin restore probe): recording is single-threaded per command list,
// so (list, shard generation) exactly proves "no pending entry for this
// list" and keeps the per-draw cost at two loads while a restore is pending
// anywhere in the process.
class DX12DrawSrvTlsNoPendingRestore
{
public:
	ID3D12GraphicsCommandList *commandList = nullptr;
	UINT64 generation = 0;
};


// TLS fast-path entry for the signature plan resolve: the same root
// signature repeats across draws, so a thread-local snapshot serves the
// common case with a pointer compare instead of the shared cache lock +
// hash lookup (the previous DX11-style global table cost two locked ops and
// a map probe per draw, serializing parallel recording threads).
class DX12DrawSrvTlsSignaturePlanEntry
{
public:
	ID3D12RootSignature *rootSignature = nullptr;
	UINT64 rootSignatureHash = 0;
	DX12DrawSrvSignaturePlanTemplate plan;
};


class DX12DrawSrvReplacementUtils
{
public:
	static void SyncDescriptorHeaps(
		ID3D12GraphicsCommandList *commandList,
		ID3D12DescriptorHeap *cbvSrvUavHeap,
		ID3D12DescriptorHeap *samplerHeap)
	{
		if (!commandList)
			return;
		ID3D12DescriptorHeap *heaps[2] = {};
		UINT heapCount = 0;
		if (cbvSrvUavHeap)
			heaps[heapCount++] = cbvSrvUavHeap;
		if (samplerHeap)
			heaps[heapCount++] = samplerHeap;
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		if (runtimeState) {
			DX12CommandListRuntime::SetDescriptorHeaps(
				runtimeState, cbvSrvUavHeap, samplerHeap);
		} else {
			DX12CommandListRuntime::SetDescriptorHeaps(
				commandList, cbvSrvUavHeap, samplerHeap);
		}
		DX12BindingTracker::SetDescriptorHeaps(
			commandList, heapCount, heapCount ? heaps : nullptr);
	}

	static void SyncRootDescriptorTable(
		ID3D12GraphicsCommandList *commandList,
		bool compute,
		UINT rootParameterIndex,
		D3D12_GPU_DESCRIPTOR_HANDLE table)
	{
		if (!commandList)
			return;
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		if (runtimeState) {
			DX12CommandListRuntime::RememberRootDescriptorTable(
				runtimeState, !compute, rootParameterIndex, table);
		} else {
			DX12CommandListRuntime::RememberRootDescriptorTable(
				commandList, !compute, rootParameterIndex, table);
		}
		if (compute) {
			DX12BindingTracker::SetComputeRootDescriptorTable(
				commandList, rootParameterIndex, table);
		} else {
			DX12BindingTracker::SetGraphicsRootDescriptorTable(
				commandList, rootParameterIndex, table);
		}
	}

	// -- deferred-restore pending table -------------------------------------

	static void NotePendingRestorePublished()
	{
		if (InterlockedIncrement(&gDrawSrvPendingRestoreCount) == 1)
			DX12HotPathState::HookWorkFlags.fetch_or(
				DX12HotPathState::FLAG_DRAWSRV_PENDING_RESTORE,
				std::memory_order_relaxed);
	}

	static void NotePendingRestoreCleared()
	{
		if (InterlockedDecrement(&gDrawSrvPendingRestoreCount) == 0)
			DX12HotPathState::HookWorkFlags.fetch_and(
				~DX12HotPathState::FLAG_DRAWSRV_PENDING_RESTORE,
				std::memory_order_relaxed);
	}

	static void ReleasePendingRestoreEntry(DX12DrawSrvPendingRestore *entry)
	{
		if (!entry)
			return;
		if (entry->cbvSrvUavHeap)
			entry->cbvSrvUavHeap->Release();
		if (entry->samplerHeap)
			entry->samplerHeap->Release();
		entry->cbvSrvUavHeap = nullptr;
		entry->samplerHeap = nullptr;
	}

	// FNV-1a over everything that determines the physical state an Apply
	// emits: bound heaps, root signatures, the mirrored table set and the
	// patch set. Descriptor *contents* behind the table handles are covered
	// coarsely through each table's source-heap version: any
	// CopyDescriptors into that heap between two draws invalidates the
	// reuse, closing the stale-mirror window the deferred restore used to
	// have when the game rewrote a table in place without a binding
	// mutation in between.
	static UINT64 ComputePlanFingerprint(
		const CommandListBindingState &snapshot,
		const std::vector<DX12DrawSrvPatchTable> &tables,
		const std::vector<DX12ModIaReplacement::DrawSrvTablePatch> &patches)
	{
		UINT64 hash = 14695981039346656037ull;
		auto append = [&hash](UINT64 value) {
			for (size_t i = 0; i < sizeof(value); ++i) {
				hash ^= static_cast<unsigned char>((value >> (i * 8)) & 0xff);
				hash *= 1099511628211ull;
			}
		};
		append(reinterpret_cast<UINT64>(snapshot.cbvSrvUavHeap));
		append(reinterpret_cast<UINT64>(snapshot.samplerHeap));
		append(reinterpret_cast<UINT64>(snapshot.graphicsRootSignature));
		append(reinterpret_cast<UINT64>(snapshot.computeRootSignature));
		for (const DX12DrawSrvPatchTable &table : tables) {
			append(table.compute ? 1ull : 0ull);
			append(table.sampler ? 1ull : 0ull);
			append(table.rootParameterIndex);
			append(table.gpuStart.ptr);
			append(table.count);
			append(table.heapVersion);
		}
		for (const DX12ModIaReplacement::DrawSrvTablePatch &patch : patches) {
			append(patch.compute ? 1ull : 0ull);
			append(patch.binding.rootParameterIndex);
			append(patch.binding.descriptorOffset);
			append(patch.replacementCpu.ptr);
			append(static_cast<UINT64>(patch.replacementViewFormat));
		}
		return hash;
	}

	// Moves any pending entry for this list out of the shard map without
	// emitting GPU commands. Returns true when an entry was taken.
	static bool TakePendingRestore(
		ID3D12GraphicsCommandList *commandList,
		DX12DrawSrvPendingRestore *entryOut)
	{
		if (!commandList || !entryOut || gDrawSrvPendingRestoreCount == 0)
			return false;
		bool taken = false;
		DX12DrawSrvPendingRestoreShard &shard =
			DrawSrvPendingRestoreShardFor(commandList);
		AcquireSRWLockExclusive(&shard.lock);
		auto it = shard.entries.find(commandList);
		if (it != shard.entries.end()) {
			*entryOut = std::move(it->second);
			shard.entries.erase(it);
			shard.mutationGeneration.fetch_add(1, std::memory_order_release);
			taken = true;
		}
		ReleaseSRWLockExclusive(&shard.lock);
		if (taken)
			NotePendingRestoreCleared();
		return taken;
	}

	static void PutBackPendingRestore(
		ID3D12GraphicsCommandList *commandList,
		DX12DrawSrvPendingRestore &&entry)
	{
		if (!commandList)
			return;
		DX12DrawSrvPendingRestoreShard &shard =
			DrawSrvPendingRestoreShardFor(commandList);
		AcquireSRWLockExclusive(&shard.lock);
		shard.entries[commandList] = std::move(entry);
		shard.mutationGeneration.fetch_add(1, std::memory_order_release);
		ReleaseSRWLockExclusive(&shard.lock);
		NotePendingRestorePublished();
	}

	static void DeferPendingRestore(
		ID3D12GraphicsCommandList *commandList,
		DX12ModIaReplacement::DrawSrvApplyState *applyState)
	{
		if (!commandList || !applyState)
			return;
		DX12DrawSrvPendingRestore entry;
		entry.planFingerprint = applyState->planFingerprint;
		entry.reloadGeneration = applyState->reloadGeneration;
		entry.patchHeap = applyState->patchHeap;
		entry.cbvSrvUavHeap = applyState->originalBindings.cbvSrvUavHeap;
		entry.samplerHeap = applyState->originalBindings.samplerHeap;
		if (entry.cbvSrvUavHeap)
			entry.cbvSrvUavHeap->AddRef();
		if (entry.samplerHeap)
			entry.samplerHeap->AddRef();
		entry.originalBindings = std::move(applyState->originalBindings);
		PutBackPendingRestore(commandList, std::move(entry));
	}

	// Physical half of the deferred restore: rebind the game's heaps and root
	// tables exactly like the stock Restore did, then release the entry.
	static void FlushPendingRestore(ID3D12GraphicsCommandList *commandList)
	{
		if (!commandList || gDrawSrvPendingRestoreCount == 0)
			return;

		thread_local DX12DrawSrvTlsNoPendingRestore tlsNoPendingRestore;
		DX12DrawSrvPendingRestoreShard &shard =
			DrawSrvPendingRestoreShardFor(commandList);
		const UINT64 mutationGeneration =
			shard.mutationGeneration.load(std::memory_order_acquire);
		if (tlsNoPendingRestore.commandList == commandList &&
			tlsNoPendingRestore.generation == mutationGeneration)
			return;

		DX12_DIAG_QPC(flushBegin);
		DX12DrawSrvPendingRestore entry;
		bool found = false;
		bool borrowed = false;
		AcquireSRWLockExclusive(&shard.lock);
		auto it = shard.entries.find(commandList);
		if (it != shard.entries.end()) {
			if (it->second.borrowedByApply) {
				borrowed = true;
			} else {
				entry = std::move(it->second);
				shard.entries.erase(it);
				shard.mutationGeneration.fetch_add(1, std::memory_order_release);
				found = true;
			}
		}
		ReleaseSRWLockExclusive(&shard.lock);
		if (found)
			NotePendingRestoreCleared();

		// Re-read after any mutation so a negative entry stays valid for
		// exactly the state just observed. A borrowed entry must never be
		// cached: it becomes flushable again once the in-flight draw
		// restores, with no generation bump on that transition.
		if (!found && !borrowed) {
			tlsNoPendingRestore.commandList = commandList;
			tlsNoPendingRestore.generation =
				shard.mutationGeneration.load(std::memory_order_relaxed);
		}
		if (!found)
			return;

		DX12InternalReplayScope internalReplay;
		const DX12DrawSrvBindingSnapshot &snapshot = entry.originalBindings;
		ID3D12DescriptorHeap *heaps[2] = {};
		UINT heapCount = 0;
		if (snapshot.cbvSrvUavHeap)
			heaps[heapCount++] = snapshot.cbvSrvUavHeap;
		if (snapshot.samplerHeap)
			heaps[heapCount++] = snapshot.samplerHeap;
		commandList->SetDescriptorHeaps(heapCount, heapCount ? heaps : nullptr);
		SyncDescriptorHeaps(
			commandList, snapshot.cbvSrvUavHeap, snapshot.samplerHeap);
		for (UINT root = 0; root < MaxRootParameters; ++root) {
			if (snapshot.graphicsTables[root].valid) {
				commandList->SetGraphicsRootDescriptorTable(
					root, snapshot.graphicsTables[root].baseDescriptor);
				SyncRootDescriptorTable(
					commandList, false, root,
					snapshot.graphicsTables[root].baseDescriptor);
			}
			if (snapshot.computeTables[root].valid) {
				commandList->SetComputeRootDescriptorTable(
					root, snapshot.computeTables[root].baseDescriptor);
				SyncRootDescriptorTable(
					commandList, true, root,
					snapshot.computeTables[root].baseDescriptor);
			}
		}
		DX12_PROF_CPU_STAGE(
			"DrawSrv",
			"flush_deferred_restore",
			0,
			static_cast<LONG64>(static_cast<size_t>(snapshot.graphicsTableValidCount) +
				snapshot.computeTableValidCount),
			DX12_DIAG_ELAPSED_US(flushBegin));
		ReleasePendingRestoreEntry(&entry);
	}

	// Quiet half of the deferred restore: drop the pending entry without
	// emitting GPU commands. Used when the game re-establishes heap state
	// itself (SetDescriptorHeaps) or the recording session ends
	// (Reset/Unregister/ClearState); the shadow trackers were already
	// logically restored at defer time.
	static void DropPendingRestore(ID3D12GraphicsCommandList *commandList)
	{
		if (!commandList || gDrawSrvPendingRestoreCount == 0)
			return;
		DX12DrawSrvPendingRestoreShard &shard =
			DrawSrvPendingRestoreShardFor(commandList);
		DX12DrawSrvPendingRestore entry;
		bool dropped = false;
		AcquireSRWLockExclusive(&shard.lock);
		auto it = shard.entries.find(commandList);
		if (it != shard.entries.end()) {
			entry = std::move(it->second);
			shard.entries.erase(it);
			shard.mutationGeneration.fetch_add(1, std::memory_order_release);
			dropped = true;
		}
		ReleaseSRWLockExclusive(&shard.lock);
		if (!dropped)
			return;
		NotePendingRestoreCleared();
		ReleasePendingRestoreEntry(&entry);
	}

	// A borrowed mirror survived its draw: make the pending entry flushable
	// again. No generation bump - the entry never left the map, and flush
	// probes never publish a negative cache entry for a borrowed one.
	static void ReleaseBorrowedPendingMirror(ID3D12GraphicsCommandList *commandList)
	{
		if (!commandList || gDrawSrvPendingRestoreCount == 0)
			return;
		DX12DrawSrvPendingRestoreShard &shard =
			DrawSrvPendingRestoreShardFor(commandList);
		AcquireSRWLockExclusive(&shard.lock);
		auto it = shard.entries.find(commandList);
		if (it != shard.entries.end() && it->second.borrowedByApply)
			it->second.borrowedByApply = false;
		ReleaseSRWLockExclusive(&shard.lock);
	}

// -- persistent mirror cache -------------------------------------------

static void ClearDrawSrvMirrorCacheLocked()
{
for (auto &item : gDrawSrvMirrorCache)
item.second.Clear();
gDrawSrvMirrorCache.clear();
}

// Runtime-reset boundary (config reload / device teardown): drop every
// cached mirror and all promotion sightings.
static void ClearDrawSrvMirrorCache()
{
AcquireSRWLockExclusive(&gDrawSrvMirrorCacheLock);
ClearDrawSrvMirrorCacheLocked();
gDrawSrvMirrorPlanSightings.clear();
gDrawSrvMirrorCacheGeneration = DX12ModGetReloadGenerationFast();
ReleaseSRWLockExclusive(&gDrawSrvMirrorCacheLock);
}

static bool TryGetDrawSrvMirrorCache(
UINT64 reloadGeneration,
UINT64 planFingerprint,
size_t tableCount,
ID3D12DescriptorHeap **heapOut,
std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> *tablesOut)
{
if (heapOut)
*heapOut = nullptr;
if (tablesOut)
tablesOut->clear();
if (!planFingerprint || !heapOut || !tablesOut)
return false;

AcquireSRWLockShared(&gDrawSrvMirrorCacheLock);
const bool generationMatches =
gDrawSrvMirrorCacheGeneration == reloadGeneration;
bool found = false;
if (generationMatches)
{
auto it = gDrawSrvMirrorCache.find(planFingerprint);
if (it != gDrawSrvMirrorCache.end() &&
it->second.heap &&
it->second.tables.size() == tableCount)
{
it->second.heap->AddRef();
*heapOut = it->second.heap;
*tablesOut = it->second.tables;
found = true;
}
}
ReleaseSRWLockShared(&gDrawSrvMirrorCacheLock);
if (found)
return true;

if (!generationMatches)
{
AcquireSRWLockExclusive(&gDrawSrvMirrorCacheLock);
if (gDrawSrvMirrorCacheGeneration != reloadGeneration)
{
ClearDrawSrvMirrorCacheLocked();
gDrawSrvMirrorPlanSightings.clear();
gDrawSrvMirrorCacheGeneration = reloadGeneration;
}
ReleaseSRWLockExclusive(&gDrawSrvMirrorCacheLock);
}
return false;
}

static void StoreDrawSrvMirrorCache(
UINT64 reloadGeneration,
UINT64 planFingerprint,
ID3D12DescriptorHeap *heap,
const std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> &tables)
{
if (!heap || !planFingerprint || tables.empty())
return;

AcquireSRWLockExclusive(&gDrawSrvMirrorCacheLock);
if (gDrawSrvMirrorCacheGeneration != reloadGeneration)
{
ClearDrawSrvMirrorCacheLocked();
gDrawSrvMirrorPlanSightings.clear();
gDrawSrvMirrorCacheGeneration = reloadGeneration;
}
if (gDrawSrvMirrorCache.size() >= MaxDrawSrvMirrorCacheEntries)
ClearDrawSrvMirrorCacheLocked();
DX12DrawSrvMirrorCacheEntry &entry = gDrawSrvMirrorCache[planFingerprint];
entry.Clear();
heap->AddRef();
entry.heap = heap;
entry.tables = tables;
ReleaseSRWLockExclusive(&gDrawSrvMirrorCacheLock);
}

// Count an apply of this plan; the caller promotes to a dedicated heap
// once the same fingerprint recurs (identical source contents), so
// per-frame arena churn never triggers heap creation.
static UINT NoteDrawSrvMirrorPlanSighting(
UINT64 reloadGeneration, UINT64 planFingerprint)
{
if (!planFingerprint)
return 0;
AcquireSRWLockExclusive(&gDrawSrvMirrorCacheLock);
if (gDrawSrvMirrorCacheGeneration != reloadGeneration)
{
ClearDrawSrvMirrorCacheLocked();
gDrawSrvMirrorPlanSightings.clear();
gDrawSrvMirrorCacheGeneration = reloadGeneration;
}
if (gDrawSrvMirrorPlanSightings.size() >= MaxDrawSrvMirrorPlanSightings)
gDrawSrvMirrorPlanSightings.clear();
const UINT sightings = ++gDrawSrvMirrorPlanSightings[planFingerprint];
ReleaseSRWLockExclusive(&gDrawSrvMirrorCacheLock);
return sightings;
}
	static DX12DrawSrvSignaturePlanTemplate BuildSignaturePlanTemplate(
		const DX12RootSignatureSummary &rootSignature)
	{
		DX12DrawSrvSignaturePlanTemplate plan;
		plan.rootSignature = rootSignature.rootSignature;
		plan.rootSignatureHash = rootSignature.hash;
		for (const DX12RootParameterSummary &parameter :
				rootSignature.parameters) {
			if (parameter.rootParameterIndex >= MaxRootParameters)
				continue;
			DX12DrawSrvRootPlanTemplate &rootPlan =
				plan.roots[parameter.rootParameterIndex];
			if (parameter.parameterType !=
					D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE ||
					parameter.ranges.empty()) {
				rootPlan = DX12DrawSrvRootPlanTemplate();
				continue;
			}
			bool hasSampler = false;
			bool hasResource = false;
			bool dynamicCount = false;
			UINT64 fixedCount = 0;
			for (const DX12RootDescriptorRangeSummary &range :
					parameter.ranges) {
				if (range.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER)
					hasSampler = true;
				else
					hasResource = true;
				if (range.numDescriptors == UINT_MAX) {
					dynamicCount = true;
					continue;
				}
				const UINT64 count =
					static_cast<UINT64>(range.effectiveOffset) + range.numDescriptors;
				fixedCount = (std::max)(fixedCount, count);
			}
			if (hasSampler == hasResource)
				continue;
			rootPlan.valid = dynamicCount || fixedCount != 0;
			rootPlan.sampler = hasSampler;
			rootPlan.dynamicCount = dynamicCount;
			rootPlan.fixedCount = fixedCount > UINT_MAX ?
				UINT_MAX : static_cast<UINT>(fixedCount);
		}
		return plan;
	}

	static bool GetSignaturePlanTemplate(
		const DX12RootSignatureSummary &rootSignature,
		DX12DrawSrvSignaturePlanTemplate *plan)
	{
		if (!plan || !rootSignature.rootSignature)
			return false;

		static thread_local DX12DrawSrvTlsSignaturePlanEntry tls;
		if (tls.rootSignature == rootSignature.rootSignature &&
		    tls.rootSignatureHash == rootSignature.hash) {
			*plan = tls.plan;
			return true;
		}

		static SRWLOCK s_planCacheLock = SRWLOCK_INIT;
		static std::unordered_map<ID3D12RootSignature*,
			DX12DrawSrvSignaturePlanTemplate> s_planCache;

		AcquireSRWLockShared(&s_planCacheLock);
		auto found = s_planCache.find(rootSignature.rootSignature);
		if (found != s_planCache.end() &&
		    found->second.rootSignatureHash == rootSignature.hash) {
			*plan = found->second;
			ReleaseSRWLockShared(&s_planCacheLock);
			tls.rootSignature = rootSignature.rootSignature;
			tls.rootSignatureHash = rootSignature.hash;
			tls.plan = *plan;
			return true;
		}
		ReleaseSRWLockShared(&s_planCacheLock);

		DX12DrawSrvSignaturePlanTemplate built =
			DX12DrawSrvReplacementUtils::BuildSignaturePlanTemplate(
				rootSignature);

		AcquireSRWLockExclusive(&s_planCacheLock);
		DX12DrawSrvSignaturePlanTemplate &stored =
			s_planCache[rootSignature.rootSignature];
		stored = built;
		*plan = stored;
		ReleaseSRWLockExclusive(&s_planCacheLock);

		tls.rootSignature = rootSignature.rootSignature;
		tls.rootSignatureHash = rootSignature.hash;
		tls.plan = built;
		return true;
	}

	static bool ShaderVisibilityMatchesPixel(UINT visibility, bool compute)
	{
		if (compute)
			return true;
		return visibility == D3D12_SHADER_VISIBILITY_ALL ||
			visibility == D3D12_SHADER_VISIBILITY_PIXEL;
	}

	static bool ShaderVisibilityMatchesVertex(UINT visibility, bool compute)
	{
		if (compute)
			return true;
		return visibility == D3D12_SHADER_VISIBILITY_ALL ||
			visibility == D3D12_SHADER_VISIBILITY_VERTEX;
	}

	static bool BindingCanBePatched(const DX12CurrentShaderResourceBinding &binding)
	{
		return !binding.rootDescriptor &&
			binding.tableCpuStart != 0 &&
			binding.tableGpuStart.ptr != 0 &&
			binding.descriptorIncrement != 0 &&
			binding.tableHeap != nullptr;
	}

	static uint64_t BindingKey(const DX12CurrentShaderResourceBinding &binding, bool compute)
	{
		uint64_t key = compute ? 0x9ae16a3b2f90404full : 0xc3a5c85c97cb3127ull;
		key ^= static_cast<uint64_t>(binding.rootParameterIndex) + 0x9e3779b97f4a7c15ull + (key << 6) + (key >> 2);
		key ^= static_cast<uint64_t>(binding.descriptorOffset) + 0x9e3779b97f4a7c15ull + (key << 6) + (key >> 2);
		key ^= static_cast<uint64_t>(binding.tableGpuStart.ptr) + 0x9e3779b97f4a7c15ull + (key << 6) + (key >> 2);
		return key;
	}

	static void AddOrReplaceRequest(
		ID3D12Device *device,
		const DX12CurrentShaderResourceBinding &binding,
		DX12LoadedResource *resource,
		bool compute,
		const char *source,
		std::vector<DX12DrawSrvPatchRequest> *requests)
	{
		if (!requests || !resource || !resource->drawSrvCpu.ptr)
			return;
		if (!DX12DrawSrvReplacementUtils::BindingCanBePatched(binding))
			return;
		// Use the DDS default SRV directly. This matches DX11 behavior:
		// DirectX::CreateDDSTextureFromFileEx creates the replacement SRV
		// with DEFAULT component mapping, and PSSetShaderResources uses it
		// as-is without any "compatibility" conversion.
		D3D12_CPU_DESCRIPTOR_HANDLE replacementCpu = resource->drawSrvCpu;
		DXGI_FORMAT replacementViewFormat = resource->drawSrvFormat;
		const uint64_t key = DX12DrawSrvReplacementUtils::BindingKey(binding, compute);
		for (DX12DrawSrvPatchRequest &request : *requests) {
			if (DX12DrawSrvReplacementUtils::BindingKey(request.binding, request.compute) == key) {
				request.resource = resource;
				request.source = source ? source : "";
				request.replacementCpu = replacementCpu;
				request.replacementViewFormat = replacementViewFormat;
				return;
			}
		}
		DX12DrawSrvPatchRequest request;
		request.binding = binding;
		request.resource = resource;
		request.compute = compute;
		request.source = source ? source : "";
		request.replacementCpu = replacementCpu;
		request.replacementViewFormat = replacementViewFormat;
		requests->push_back(request);
	}

	static UINT AddSlotRequestsFromBindings(
		ID3D12Device *device,
		ID3D12GraphicsCommandList *commandList,
		const std::vector<DX12CurrentShaderResourceBinding> &bindings,
		bool compute,
		UINT slot,
		const std::wstring &resourceName,
		bool pixelStage,
		const char *source,
		std::vector<DX12DrawSrvPatchRequest> *requests)
	{
		if (!device || !commandList || !requests || resourceName.empty())
			return 0;
		UINT added = 0;
		DX12LoadedResource *resource = nullptr;
		for (const DX12CurrentShaderResourceBinding &binding : bindings) {
			if (binding.shaderRegister != slot || binding.registerSpace != 0)
				continue;
			const bool visibilityMatches = pixelStage ?
				DX12DrawSrvReplacementUtils::ShaderVisibilityMatchesPixel(binding.shaderVisibility, compute) :
				DX12DrawSrvReplacementUtils::ShaderVisibilityMatchesVertex(binding.shaderVisibility, compute);
			if (!visibilityMatches)
				continue;
			if (!resource)
				resource = DX12DdsTextureLoadUtils::EnsureLoadedDrawTextureSrv(
					device, commandList, resourceName);
			if (!resource)
				return added;
			DX12DrawSrvReplacementUtils::AddOrReplaceRequest(
				device, binding, resource, compute, source, requests);
			++added;
		}
		return added;
	}

	static UINT AddSlotRequestsByRegister(
		ID3D12Device *device,
		ID3D12GraphicsCommandList *commandList,
		bool compute,
		UINT slot,
		const std::wstring &resourceName,
		bool pixelStage,
		const char *source,
		std::vector<DX12DrawSrvPatchRequest> *requests,
		size_t *bindingCount = nullptr)
	{
		if (bindingCount)
			*bindingCount = 0;
		if (!device || !commandList || !requests || resourceName.empty())
			return 0;

		std::vector<DX12CurrentShaderResourceBinding> bindings;
		if (!DX12BindingTracker::GetCurrentShaderResourceBindingsByRegister(
				commandList,
				compute,
				D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
				slot,
				0,
				&bindings)) {
			return 0;
		}
		if (bindingCount)
			*bindingCount = bindings.size();
		return DX12DrawSrvReplacementUtils::AddSlotRequestsFromBindings(
			device, commandList, bindings, compute, slot, resourceName,
			pixelStage, source, requests);
	}

	static UINT AddThisRequestFromMatch(
		ID3D12Device *device,
		ID3D12GraphicsCommandList *commandList,
		const DX12TextureOverrideMatch *match,
		const std::wstring &resourceName,
		std::vector<DX12DrawSrvPatchRequest> *requests)
	{
		if (!device || !commandList || !requests || resourceName.empty())
			return 0;
		if (!match || !match->hasThisSrvTarget) {
			DX12_LOG("DX12DrawSrvReplacement",
				"stage=slot_patch_fail,source=this,reason=no_this_target,resource=%S",
				resourceName.c_str());
			return 0;
		}
		DX12LoadedResource *resource =
			DX12DdsTextureLoadUtils::EnsureLoadedDrawTextureSrv(
				device, commandList, resourceName);
		if (!resource)
			return 0;
		DX12DrawSrvReplacementUtils::AddOrReplaceRequest(
			device, match->thisSrvBinding, resource, match->compute, "this", requests);
		return 1;
	}

	static bool QueueRequests(
		const std::vector<DX12DrawSrvPatchRequest> &requests,
		DX12ModIaReplacement *replacement)
	{
		if (!replacement || requests.empty())
			return false;
		for (const DX12DrawSrvPatchRequest &request : requests) {
			if (!request.resource)
				continue;
			D3D12_CPU_DESCRIPTOR_HANDLE replacementCpu = request.replacementCpu.ptr ?
				request.replacementCpu : request.resource->drawSrvCpu;
			if (!replacementCpu.ptr)
				continue;
			const uint64_t key = BindingKey(request.binding, request.compute);
			DX12ModIaReplacement::DrawSrvTablePatch *queued = nullptr;
			for (DX12ModIaReplacement::DrawSrvTablePatch &existing :
					replacement->drawSrvPatches) {
				if (BindingKey(existing.binding, existing.compute) == key) {
					queued = &existing;
					break;
				}
			}
			if (!queued) {
				replacement->drawSrvPatches.push_back(
					DX12ModIaReplacement::DrawSrvTablePatch());
				queued = &replacement->drawSrvPatches.back();
			}
			queued->binding = request.binding;
			queued->replacementCpu = replacementCpu;
			queued->replacementViewFormat = request.replacementViewFormat;
			queued->resourceName = request.resource->name;
			queued->source = request.source ? request.source : "";
			queued->compute = request.compute;
			queued->rootParameterIndex = request.binding.rootParameterIndex;
			queued->originalTable = request.binding.tableGpuStart;
		}
		if (replacement->drawSrvPatches.empty()) {
			// TIMING-build diagnostic: matching produced requests but every one
			// was dropped (null resource or null replacement descriptor). The
			// draw then executes unmodified with no other log line.
			DX12_LOG("DX12DrawSrvReplacement",
				"stage=queue_empty,requests=%zu", requests.size());
			return false;
		}
		return true;
	}

	static bool ResolveTableHeap(
		const RootTableState &table,
		D3D12_DESCRIPTOR_HEAP_TYPE type,
		DX12DescriptorHeapSummary *heap)
	{
		if (!heap || !table.valid)
			return false;
		*heap = DX12DescriptorHeapSummary();
		if (table.runtimeHeapResolved &&
				table.runtimeHeapType == static_cast<UINT>(type) &&
				table.runtimeHeapIncrement && table.runtimeHeapGpuStart) {
			heap->heap = table.runtimeHeap;
			heap->type = table.runtimeHeapType;
			heap->numDescriptors = table.runtimeHeapNumDescriptors;
			heap->cpuStart = table.runtimeHeapCpuStart;
			heap->gpuStart = table.runtimeHeapGpuStart;
			heap->increment = table.runtimeHeapIncrement;
			return true;
		}
		return DX12FindDescriptorHeapByGpuHandle(
			table.baseDescriptor, type, heap) && heap->increment != 0;
	}

	static UINT DescriptorCountForTable(
		const DX12RootParameterSummary &parameter,
		const DX12DescriptorHeapSummary &heap,
		UINT64 tableBaseIndex,
		D3D12_DESCRIPTOR_HEAP_TYPE type)
	{
		UINT64 required = 0;
		for (const DX12RootDescriptorRangeSummary &range : parameter.ranges) {
			const bool sampler =
				range.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
			if (sampler != (type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER))
				continue;
			const UINT64 first = tableBaseIndex + range.effectiveOffset;
			if (first >= heap.numDescriptors)
				continue;
			const UINT64 available = heap.numDescriptors - first;
			const UINT64 declared = range.numDescriptors == UINT_MAX ?
				available : range.numDescriptors;
			const UINT64 count = (std::min)(declared, available);
			required = (std::max)(required,
				static_cast<UINT64>(range.effectiveOffset) + count);
		}
		return required > UINT_MAX ? UINT_MAX : static_cast<UINT>(required);
	}

	static const DX12RootParameterSummary *FindRootParameterSummary(
		const DX12RootSignatureSummary &rootSignature,
		UINT rootParameterIndex)
	{
		for (const DX12RootParameterSummary &candidate :
				rootSignature.parameters) {
			if (candidate.rootParameterIndex == rootParameterIndex)
				return &candidate;
		}
		return nullptr;
	}

	static bool ResolvePatchTableLocation(
		const CommandListBindingState &snapshot,
		const RootTableState &state,
		D3D12_DESCRIPTOR_HEAP_TYPE type,
		DX12DescriptorHeapSummary *heap,
		UINT64 *tableBaseIndex,
		SIZE_T *tableCpuStart)
	{
		if (!heap || !tableBaseIndex || !tableCpuStart || !state.valid)
			return false;
		*heap = DX12DescriptorHeapSummary();
		*tableBaseIndex = 0;
		*tableCpuStart = 0;

		ID3D12DescriptorHeap *boundHeap =
			type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER ?
				snapshot.samplerHeap : snapshot.cbvSrvUavHeap;
		if (state.runtimeHeapResolved &&
		    state.runtimeHeapType == static_cast<UINT>(type) &&
		    state.runtimeHeapIncrement != 0 &&
		    state.runtimeHeap != nullptr &&
		    state.runtimeHeap == boundHeap &&
		    state.runtimeTableBaseIndex < state.runtimeHeapNumDescriptors) {
			heap->heap = state.runtimeHeap;
			heap->type = state.runtimeHeapType;
			heap->numDescriptors = state.runtimeHeapNumDescriptors;
			heap->cpuStart = state.runtimeHeapCpuStart;
			heap->gpuStart = state.runtimeHeapGpuStart;
			heap->increment = state.runtimeHeapIncrement;
			heap->version = state.runtimeHeapVersion;
			*tableBaseIndex = state.runtimeTableBaseIndex;
			*tableCpuStart = state.runtimeTableCpuStart;
			return true;
		}

		if (!ResolveTableHeap(state, type, heap) ||
		    !boundHeap ||
		    heap->heap != boundHeap ||
		    !heap->cpuStart ||
		    state.baseDescriptor.ptr < heap->gpuStart) {
			return false;
		}
		const UINT64 tableDelta = state.baseDescriptor.ptr - heap->gpuStart;
		if (tableDelta % heap->increment != 0)
			return false;
		*tableBaseIndex = tableDelta / heap->increment;
		if (*tableBaseIndex >= heap->numDescriptors)
			return false;
		*tableCpuStart =
			heap->cpuStart +
			static_cast<SIZE_T>(*tableBaseIndex) * heap->increment;
		return true;
	}

	static bool RequestMatchesSrvLayout(
		const DX12ModIaReplacement::DrawSrvTablePatch &request,
		const DX12RootSignatureSummary &rootSignature)
	{
		for (const DX12RootParameterSummary &parameter :
				rootSignature.parameters) {
			if (parameter.rootParameterIndex != request.binding.rootParameterIndex ||
					parameter.parameterType !=
						D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
				continue;
			for (const DX12RootDescriptorRangeSummary &range : parameter.ranges) {
				if (range.rangeType != D3D12_DESCRIPTOR_RANGE_TYPE_SRV ||
						request.binding.descriptorOffset < range.effectiveOffset)
					continue;
				const UINT64 relative = static_cast<UINT64>(
					request.binding.descriptorOffset - range.effectiveOffset);
				if (range.numDescriptors != UINT_MAX &&
						relative >= range.numDescriptors)
					continue;
				const UINT64 shaderRegister =
					static_cast<UINT64>(range.baseShaderRegister) + relative;
				if (shaderRegister > UINT_MAX ||
						static_cast<UINT>(shaderRegister) !=
							request.binding.shaderRegister ||
						range.registerSpace != request.binding.registerSpace ||
						(request.binding.rangeIndex != UINT_MAX &&
							range.rangeIndex != request.binding.rangeIndex))
					continue;
				return true;
			}
			return false;
		}
		return false;
	}

	static bool AppendPatchTables(
		const CommandListBindingState &snapshot,
		const DX12RootSignatureSummary &rootSignature,
		bool compute,
		std::vector<DX12DrawSrvPatchTable> *tables,
		UINT *totalDescriptors)
	{
		if (!tables || !totalDescriptors)
			return false;
		// Mirrors land in the pre-skin descriptor ring, whose offsets are
		// rewound once GPU references drain; the old 65536 hard refusal made
		// bindless / large-table root signatures permanently ineligible for
		// texture replacement (IA geometry replacement took another path and
		// kept working). Cap at the ring's own maximum instead: oversized
		// mirrors degrade to ring churn, not to a silently missing mod.
		static constexpr UINT MaxMirroredDescriptors =
			PreSkinDescriptorRingMaximumCapacity;
		DX12DrawSrvSignaturePlanTemplate cachedPlan;
		if (!DX12DrawSrvReplacementUtils::GetSignaturePlanTemplate(
				rootSignature, &cachedPlan))
			return false;
		for (UINT root = 0; root < MaxRootParameters; ++root) {
			const RootTableState &state = compute ?
				snapshot.computeTables[root] : snapshot.graphicsTables[root];
			if (!state.valid)
				continue;
			const DX12DrawSrvRootPlanTemplate &rootPlan =
				cachedPlan.roots[root];
			if (!rootPlan.valid) {
				return false;
			}
			const D3D12_DESCRIPTOR_HEAP_TYPE type = rootPlan.sampler ?
				D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER :
				D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			DX12DescriptorHeapSummary heap = {};
			UINT64 tableBaseIndex = 0;
			SIZE_T tableCpuStart = 0;
			if (!ResolvePatchTableLocation(
					snapshot, state, type,
					&heap, &tableBaseIndex, &tableCpuStart)) {
				return false;
			}
			UINT count = 0;
			if (rootPlan.dynamicCount) {
				const DX12RootParameterSummary *parameter =
					DX12DrawSrvReplacementUtils::FindRootParameterSummary(
						rootSignature, root);
				if (!parameter) {
					return false;
				}
				count = DescriptorCountForTable(
					*parameter, heap, tableBaseIndex, type);
			} else if (tableBaseIndex < heap.numDescriptors) {
				const UINT64 available = heap.numDescriptors - tableBaseIndex;
				const UINT64 clampedCount = (std::min)(
					static_cast<UINT64>(rootPlan.fixedCount),
					available);
				count = clampedCount > UINT_MAX ?
					UINT_MAX : static_cast<UINT>(clampedCount);
			}
			if (!count) {
				return false;
			}
			DX12DrawSrvPatchTable table;
			table.compute = compute;
			table.rootParameterIndex = root;
			table.sampler = rootPlan.sampler;
			table.cpuStart = tableCpuStart;
			table.gpuStart = state.baseDescriptor;
			table.heap = heap.heap;
			table.increment = heap.increment;
			table.count = count;
			table.heapVersion = heap.version;
			if (!rootPlan.sampler) {
				if (*totalDescriptors > MaxMirroredDescriptors ||
						count > MaxMirroredDescriptors - *totalDescriptors) {
					return false;
				}
				table.tempOffset = *totalDescriptors;
				*totalDescriptors += count;
			}
			tables->push_back(table);
		}
		return true;
	}

	// Descriptor contents are read by the GPU at execution time, not captured by
	// Draw*. Mirror every active graphics table into a command-list-lived heap,
	// patch that mirror, and temporarily bind it for the draw. This prevents the
	// game's descriptor ring from overwriting the replacement before submission.
	static bool ApplyPending(
		ID3D12Device *device,
		ID3D12GraphicsCommandList *commandList,
		DX12ModIaReplacement *replacement,
		bool executeOriginalDraw)
	{
		DX12_DIAG_QPC(applyBegin);
		if (!device || !commandList || !replacement ||
				replacement->drawSrvPatches.empty())
			return false;
		if (replacement->drawSrvApplyState.applied) {
			DX12_LOG("DX12DrawSrvReplacement",
				"stage=table_patch_fail,reason=already_applied");
			return false;
		}
		if (commandList->GetType() == D3D12_COMMAND_LIST_TYPE_BUNDLE) {
			// Bundles cannot swap heaps themselves: mirror into the bundle's
			// session heap instead; the ExecuteBundle parent replay binds it.
			return DX12ModApplyDrawSrvReplacementBundle(
				device, commandList, replacement, executeOriginalDraw);
		}
		// Take any deferred restore for this list up front. On every failure
		// path below it is put back and flushed so the original draw never
		// executes against the stale patched mirror; on success the new apply
		// fully supersedes it and it is dropped silently.
		DX12DrawSrvPendingRestore takenPending;
		const bool hadPending = TakePendingRestore(commandList, &takenPending);
		auto failWithPendingRestore = [&hadPending, &takenPending, commandList]() {
			if (hadPending) {
				PutBackPendingRestore(commandList, std::move(takenPending));
				FlushPendingRestore(commandList);
			}
			return false;
		};
		DX12_DIAG_QPC(planBegin);
		// thread_local: see the note above for why this is safe and cheaper.
		static thread_local CommandListBindingState snapshot;
		if (!DX12BindingTracker::SnapshotBindingState(commandList, &snapshot)) {
			DX12_LOG("DX12DrawSrvReplacement",
				"stage=table_patch_fail,reason=binding_snapshot");
			return failWithPendingRestore();
		}
		bool needsGraphicsTables = executeOriginalDraw || !replacement->draws.empty();
		bool needsComputeTables = !replacement->dispatches.empty();
		for (const DX12ModIaReplacement::DrawSrvTablePatch &request :
				replacement->drawSrvPatches) {
			if (request.compute)
				needsComputeTables = true;
			else
				needsGraphicsTables = true;
		}
		std::vector<DX12DrawSrvPatchTable> tables;
		UINT totalDescriptors = 0;
		DX12RootSignatureSummary graphicsRootSignature;
		DX12RootSignatureSummary computeRootSignature;
		bool hasGraphicsRootSignature = false;
		bool hasComputeRootSignature = false;
		if (needsGraphicsTables && snapshot.graphicsTableValidCount) {
			if (!snapshot.graphicsRootSignature ||
					!DX12GetRootSignatureSummary(
						snapshot.graphicsRootSignature, &graphicsRootSignature) ||
					!AppendPatchTables(snapshot, graphicsRootSignature, false,
						&tables, &totalDescriptors)) {
				DX12_LOG("DX12DrawSrvReplacement",
					"stage=table_patch_fail,reason=graphics_root_signature");
				return failWithPendingRestore();
			}
			hasGraphicsRootSignature = true;
		}
		if (needsComputeTables && snapshot.computeTableValidCount) {
			if (!snapshot.computeRootSignature ||
					!DX12GetRootSignatureSummary(
						snapshot.computeRootSignature, &computeRootSignature) ||
					!AppendPatchTables(snapshot, computeRootSignature, true,
						&tables, &totalDescriptors)) {
				DX12_LOG("DX12DrawSrvReplacement",
					"stage=table_patch_fail,reason=compute_root_signature");
				return failWithPendingRestore();
			}
			hasComputeRootSignature = true;
		}
		if (tables.empty() || !totalDescriptors) {
			DX12_LOG("DX12DrawSrvReplacement",
				"stage=table_patch_fail,reason=table_plan");
			return failWithPendingRestore();
		}
		for (const DX12ModIaReplacement::DrawSrvTablePatch &request :
				replacement->drawSrvPatches) {
			const DX12RootSignatureSummary *requestRootSignature =
				request.compute ?
					(hasComputeRootSignature ? &computeRootSignature : nullptr) :
					(hasGraphicsRootSignature ? &graphicsRootSignature : nullptr);
			if (!requestRootSignature ||
					!RequestMatchesSrvLayout(request, *requestRootSignature)) {
				DX12_LOG("DX12DrawSrvReplacement",
					"stage=table_patch_fail,reason=request_layout,bindSpace=%s,"
					"root=%u,range=%u,slot=%u,space=%u,offset=%u",
					request.compute ? "compute" : "graphics",
					request.binding.rootParameterIndex,
					request.binding.rangeIndex,
					request.binding.shaderRegister,
					request.binding.registerSpace,
					request.binding.descriptorOffset);
				return failWithPendingRestore();
			}
			bool found = false;
			for (const DX12DrawSrvPatchTable &table : tables) {
				if (table.sampler ||
						table.compute != request.compute ||
						table.rootParameterIndex != request.binding.rootParameterIndex ||
						table.gpuStart.ptr != request.binding.tableGpuStart.ptr)
					continue;
				if (request.binding.descriptorOffset >= table.count)
					return failWithPendingRestore();
				found = true;
				break;
			}
			if (!found || !request.replacementCpu.ptr) {
				DX12_LOG("DX12DrawSrvReplacement",
					"stage=table_patch_fail,reason=request_table,root=%u,offset=%u",
					request.binding.rootParameterIndex,
					request.binding.descriptorOffset);
				return failWithPendingRestore();
			}
		}
		DX12_PROF_CPU_STAGE(
			"DrawSrv",
			"build_mirror_plan",
			0,
			static_cast<LONG64>(totalDescriptors),
			DX12_DIAG_ELAPSED_US(planBegin));

		// Deferred-restore reuse: a pending entry whose plan fingerprint
		// matches this draw means the previous patched draw's mirror heap and
		// patched tables are still bound on this list - any game command that
		// could have changed that physical state would have flushed or dropped
		// the entry. The draw executes with exactly that state: emit nothing,
		// skip the mirror copy, the heap switch and every root-table rebind.
		const UINT64 planFingerprint =
			ComputePlanFingerprint(snapshot, tables, replacement->drawSrvPatches);
		const UINT64 reloadGeneration = DX12ModGetReloadGenerationFast();
		if (hadPending &&
				takenPending.planFingerprint == planFingerprint &&
				takenPending.reloadGeneration == reloadGeneration &&
				takenPending.patchHeap) {
			ID3D12DescriptorHeap *patchHeap = takenPending.patchHeap;
			takenPending.borrowedByApply = true;
			PutBackPendingRestore(commandList, std::move(takenPending));
			DX12ModIaReplacement::DrawSrvApplyState &reuseState =
				replacement->drawSrvApplyState;
			reuseState.applied = true;
			reuseState.borrowedPendingMirror = true;
			reuseState.patchHeap = patchHeap;
			reuseState.planFingerprint = planFingerprint;
			reuseState.reloadGeneration = reloadGeneration;
			DX12_PROF_CPU_STAGE(
				"DrawSrv",
				"reuse_deferred_mirror",
				0,
				static_cast<LONG64>(totalDescriptors),
				DX12_DIAG_ELAPSED_US(applyBegin));
			return true;
		}

// Persistent mirror reuse: an earlier apply already mirrored and patched
// exactly this plan into a dedicated heap (the fingerprint covers every
// source heap's content version, so an identical fingerprint means the
// source descriptors are unchanged). Rebind it and skip the ring
// allocation plus both descriptor copy batches; restore is unchanged.
static thread_local std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> tlsCachedTables;
tlsCachedTables.clear();
ID3D12DescriptorHeap *cachedHeap = nullptr;
if (TryGetDrawSrvMirrorCache(
reloadGeneration, planFingerprint, tables.size(),
&cachedHeap, &tlsCachedTables)) {
replacement->drawSrvApplyState.originalBindings.CaptureFrom(snapshot);
replacement->drawSrvApplyState.patchHeap = cachedHeap;
replacement->drawSrvApplyState.planFingerprint = planFingerprint;
replacement->drawSrvApplyState.reloadGeneration = reloadGeneration;
DX12InternalReplayScope internalReplay;
ID3D12DescriptorHeap *heaps[2] = { cachedHeap, snapshot.samplerHeap };
commandList->SetDescriptorHeaps(snapshot.samplerHeap ? 2 : 1, heaps);
SyncDescriptorHeaps(commandList, cachedHeap, snapshot.samplerHeap);
for (size_t index = 0; index < tables.size(); ++index) {
const DX12DrawSrvPatchTable &table = tables[index];
const D3D12_GPU_DESCRIPTOR_HANDLE tableGpu =
table.sampler ? table.gpuStart : tlsCachedTables[index];
if (table.compute)
commandList->SetComputeRootDescriptorTable(
table.rootParameterIndex, tableGpu);
else
commandList->SetGraphicsRootDescriptorTable(
table.rootParameterIndex, tableGpu);
SyncRootDescriptorTable(
commandList, table.compute, table.rootParameterIndex, tableGpu);
}
RetainPreSkinDescriptorHeapForCommandList(commandList, cachedHeap);
cachedHeap->Release();
replacement->drawSrvApplyState.applied = true;
DX12_PROF_CPU_STAGE(
"DrawSrv",
"reuse_persistent_mirror",
0,
static_cast<LONG64>(totalDescriptors),
DX12_DIAG_ELAPSED_US(applyBegin));
// Same supersede contract as the ring path: the heap set and the
// root-table rebinds above overwrote every piece of physical state
// the old pending entry covered. Drop it without emitting anything.
ReleasePendingRestoreEntry(&takenPending);
return true;
}
		D3D12_CPU_DESCRIPTOR_HANDLE tempCpuBase = {};
		D3D12_GPU_DESCRIPTOR_HANDLE tempGpuBase = {};
		UINT tempIncrement = 0;
		ID3D12DescriptorHeap *tempHeap = nullptr;
		DX12_DIAG_QPC(allocateBegin);
		if (!EnsurePreSkinDescriptorRingLocked(
				device, commandList, totalDescriptors,
				&tempCpuBase, &tempGpuBase, &tempIncrement, &tempHeap)) {
			DX12_LOG("DX12DrawSrvReplacement",
				"stage=table_patch_fail,reason=descriptor_ring,count=%u",
				totalDescriptors);
			return failWithPendingRestore();
		}
		DX12_PROF_CPU_STAGE(
			"DrawSrv",
			"allocate_mirror_range",
			0,
			static_cast<LONG64>(totalDescriptors),
			DX12_DIAG_ELAPSED_US(allocateBegin));
		// Persist the only authoritative restore snapshot before issuing any
		// command-list state mutation. Restore never asks Binding State for a
		// second, potentially stale snapshot.
		replacement->drawSrvApplyState.originalBindings.CaptureFrom(snapshot);
		replacement->drawSrvApplyState.patchHeap = tempHeap;
		replacement->drawSrvApplyState.planFingerprint = planFingerprint;
		replacement->drawSrvApplyState.reloadGeneration = reloadGeneration;

		DX12InternalReplayScope internalReplay;
		DX12_DIAG_QPC(copyBegin);
		// Batch every table-mirror copy into one CopyDescriptors call. The
		// destinations are disjoint slots in the freshly allocated temp ring
		// range and the sources live in other heaps, so merging the ranges is
		// identical to issuing one CopyDescriptorsSimple per table.
		static thread_local std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> tlsCopyDestStarts;
		static thread_local std::vector<UINT> tlsCopyDestSizes;
		static thread_local std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> tlsCopySrcStarts;
		static thread_local std::vector<UINT> tlsCopySrcSizes;
		std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> &copyDestStarts = tlsCopyDestStarts;
		std::vector<UINT> &copyDestSizes = tlsCopyDestSizes;
		std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> &copySrcStarts = tlsCopySrcStarts;
		std::vector<UINT> &copySrcSizes = tlsCopySrcSizes;
		copyDestStarts.clear();
		copyDestSizes.clear();
		copySrcStarts.clear();
		copySrcSizes.clear();
		for (const DX12DrawSrvPatchTable &table : tables) {
			if (table.sampler)
				continue;
			D3D12_CPU_DESCRIPTOR_HANDLE source = {};
			source.ptr = table.cpuStart;
			D3D12_CPU_DESCRIPTOR_HANDLE destination = tempCpuBase;
			destination.ptr += static_cast<SIZE_T>(table.tempOffset) * tempIncrement;
			copyDestStarts.push_back(destination);
			copyDestSizes.push_back(table.count);
			copySrcStarts.push_back(source);
			copySrcSizes.push_back(table.count);
		}
		if (!copyDestStarts.empty()) {
			device->CopyDescriptors(
				static_cast<UINT>(copyDestStarts.size()), copyDestStarts.data(),
				copyDestSizes.data(),
				static_cast<UINT>(copySrcStarts.size()), copySrcStarts.data(),
				copySrcSizes.data(),
				D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		}
		copyDestStarts.clear();
		copyDestSizes.clear();
		copySrcStarts.clear();
		copySrcSizes.clear();
		for (DX12ModIaReplacement::DrawSrvTablePatch &request :
				replacement->drawSrvPatches) {
			for (const DX12DrawSrvPatchTable &table : tables) {
				if (table.sampler ||
						table.compute != request.compute ||
						table.rootParameterIndex != request.binding.rootParameterIndex ||
						table.gpuStart.ptr != request.binding.tableGpuStart.ptr)
					continue;
				D3D12_CPU_DESCRIPTOR_HANDLE destination = tempCpuBase;
				destination.ptr += static_cast<SIZE_T>(
					table.tempOffset + request.binding.descriptorOffset) * tempIncrement;
				copyDestStarts.push_back(destination);
				copyDestSizes.push_back(1);
				copySrcStarts.push_back(request.replacementCpu);
				copySrcSizes.push_back(1);
				request.targetCpu = destination;
				break;
			}
		}
		// Flush after the mirror batch so patched slots still overwrite the
		// mirrored originals, exactly like the sequential calls did.
		if (!copyDestStarts.empty()) {
			device->CopyDescriptors(
				static_cast<UINT>(copyDestStarts.size()), copyDestStarts.data(),
				copyDestSizes.data(),
				static_cast<UINT>(copySrcStarts.size()), copySrcStarts.data(),
				copySrcSizes.data(),
				D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		}
		DX12_PROF_CPU_STAGE(
			"DrawSrv",
			"copy_mirror_descriptors",
			0,
			static_cast<LONG64>(totalDescriptors),
			DX12_DIAG_ELAPSED_US(copyBegin));
		replacement->drawSrvApplyState.applied = true;
		DX12_DIAG_QPC(bindBegin);
		ID3D12DescriptorHeap *heaps[2] = { tempHeap, snapshot.samplerHeap };
		commandList->SetDescriptorHeaps(snapshot.samplerHeap ? 2 : 1, heaps);
		SyncDescriptorHeaps(commandList, tempHeap, snapshot.samplerHeap);
		for (const DX12DrawSrvPatchTable &table : tables) {
			D3D12_GPU_DESCRIPTOR_HANDLE tableGpu = table.gpuStart;
			if (!table.sampler) {
				tableGpu = tempGpuBase;
				tableGpu.ptr += static_cast<UINT64>(table.tempOffset) * tempIncrement;
			}
			if (table.compute)
				commandList->SetComputeRootDescriptorTable(
					table.rootParameterIndex, tableGpu);
			else
				commandList->SetGraphicsRootDescriptorTable(
					table.rootParameterIndex, tableGpu);
			SyncRootDescriptorTable(
				commandList, table.compute, table.rootParameterIndex, tableGpu);
		}
		DX12_PROF_CPU_STAGE(
			"DrawSrv",
			"bind_mirror_tables",
			0,
			static_cast<LONG64>(tables.size()),
			DX12_DIAG_ELAPSED_US(bindBegin));
		DX12_PROF_CPU_STAGE(
			"DrawSrv",
			"apply_total",
			0,
			static_cast<LONG64>(totalDescriptors),
			DX12_DIAG_ELAPSED_US(applyBegin));
// A plan sighted before with identical source contents earns a dedicated
// mirror heap: later applies rebind it without any descriptor copies.
// Single-sighting plans stay on the transient ring so per-frame arena
// rewrites never pay heap-creation churn.
if (NoteDrawSrvMirrorPlanSighting(reloadGeneration, planFingerprint) >=
DrawSrvMirrorPromotionSightings) {
D3D12_DESCRIPTOR_HEAP_DESC dedicatedDesc = {};
dedicatedDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
dedicatedDesc.NumDescriptors = totalDescriptors;
dedicatedDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
ID3D12DescriptorHeap *dedicatedHeap = nullptr;
if (SUCCEEDED(device->CreateDescriptorHeap(
&dedicatedDesc, IID_PPV_ARGS(&dedicatedHeap))) && dedicatedHeap) {
device->CopyDescriptorsSimple(
totalDescriptors,
dedicatedHeap->GetCPUDescriptorHandleForHeapStart(),
tempCpuBase,
D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
const D3D12_GPU_DESCRIPTOR_HANDLE dedicatedGpuBase =
dedicatedHeap->GetGPUDescriptorHandleForHeapStart();
tlsCachedTables.clear();
tlsCachedTables.reserve(tables.size());
for (const DX12DrawSrvPatchTable &table : tables) {
D3D12_GPU_DESCRIPTOR_HANDLE gpu = table.gpuStart;
if (!table.sampler) {
gpu = dedicatedGpuBase;
gpu.ptr += static_cast<UINT64>(table.tempOffset) * tempIncrement;
}
tlsCachedTables.push_back(gpu);
}
StoreDrawSrvMirrorCache(
reloadGeneration, planFingerprint, dedicatedHeap, tlsCachedTables);
dedicatedHeap->Release();
}
}
		// The new apply fully superseded any stale deferred restore: the heap
		// set and root-table rebinds above overwrote every piece of physical
		// state the old entry covered. Drop it without emitting anything.
		ReleasePendingRestoreEntry(&takenPending);
		return true;
	}

	static bool ApplyConfig(
		ID3D12Device *device,
		ID3D12GraphicsCommandList *commandList,
		const Bunny::TextureOverrideConfig &config,
		const DX12TextureOverrideMatch *match,
		DX12ModIaReplacement *replacement,
		const std::vector<DX12CurrentShaderResourceBinding> *graphicsSrvs = nullptr)
	{
		if (!device || !commandList || !replacement)
			return false;
		if (config.drawPsSrvResources.empty() &&
		    config.drawVsSrvResources.empty() &&
		    config.drawReplaceThisResource.empty())
			return false;

		const std::vector<DX12CurrentShaderResourceBinding> *bindingsView =
			graphicsSrvs;

		std::vector<DX12DrawSrvPatchRequest> requests;
		for (std::map<uint32_t, std::wstring>::const_iterator it = config.drawPsSrvResources.begin();
		     it != config.drawPsSrvResources.end(); ++it) {
			size_t bindingCount = 0;
			UINT added = bindingsView ?
				DX12DrawSrvReplacementUtils::AddSlotRequestsFromBindings(
					device, commandList, *bindingsView, false, it->first, it->second,
					true, "ps-t", &requests) :
				DX12DrawSrvReplacementUtils::AddSlotRequestsByRegister(
					device, commandList, false, it->first, it->second,
					true, "ps-t", &requests, &bindingCount);
			if (!added) {
				DX12_LOG("DX12DrawSrvReplacement",
					"stage=slot_patch_fail,source=ps-t,reason=graphics_binding_missing,slot=%u,resource=%S,graphicsCount=%zu",
					it->first, it->second.c_str(),
					bindingsView ? bindingsView->size() : bindingCount);
			}
		}

		for (std::map<uint32_t, std::wstring>::const_iterator it = config.drawVsSrvResources.begin();
		     it != config.drawVsSrvResources.end(); ++it) {
			size_t bindingCount = 0;
			UINT added = bindingsView ?
				DX12DrawSrvReplacementUtils::AddSlotRequestsFromBindings(
					device, commandList, *bindingsView, false, it->first, it->second,
					false, "vs-t", &requests) :
				DX12DrawSrvReplacementUtils::AddSlotRequestsByRegister(
					device, commandList, false, it->first, it->second,
					false, "vs-t", &requests, &bindingCount);
			if (!added) {
				DX12_LOG("DX12DrawSrvReplacement",
					"stage=slot_patch_fail,source=vs-t,reason=graphics_binding_missing,slot=%u,resource=%S,graphicsCount=%zu",
					it->first, it->second.c_str(),
					bindingsView ? bindingsView->size() : bindingCount);
			}
		}

		if (!config.drawReplaceThisResource.empty()) {
			UINT added = DX12DrawSrvReplacementUtils::AddThisRequestFromMatch(
				device, commandList, match,
				config.drawReplaceThisResource, &requests);
			if (!added) {
				DX12_LOG("DX12DrawSrvReplacement",
					"stage=slot_patch_fail,source=this,reason=no_matched_target,hash=%08x,resource=%S,graphicsCount=%zu",
					static_cast<uint32_t>(config.hash),
					config.drawReplaceThisResource.c_str(),
					bindingsView ? bindingsView->size() : 0);
			}
		}

		return DX12DrawSrvReplacementUtils::QueueRequests(requests, replacement);
	}

	static bool ApplyCompiledPlan(
		ID3D12Device *device,
		ID3D12GraphicsCommandList *commandList,
		const DX12CompiledTextureOverridePlan &plan,
		const DX12TextureOverrideMatch *match,
		DX12ModIaReplacement *replacement,
		const std::vector<DX12CurrentShaderResourceBinding> *graphicsSrvs = nullptr)
	{
		if (!device || !commandList || !replacement)
			return false;
		if (plan.drawSrvResources.empty() &&
		    plan.drawReplaceThisResource.empty())
			return false;

		const std::vector<DX12CurrentShaderResourceBinding> *bindingsView =
			graphicsSrvs;

		std::vector<DX12DrawSrvPatchRequest> requests;
		for (const DX12CompiledDrawSrvResourceBinding &binding :
				plan.drawSrvResources) {
			const char *source = binding.pixelStage ? "ps-t" : "vs-t";
			size_t bindingCount = 0;
			UINT added = bindingsView ?
				DX12DrawSrvReplacementUtils::AddSlotRequestsFromBindings(
					device, commandList, *bindingsView, false,
					binding.slot, binding.resource,
					binding.pixelStage, source, &requests) :
				DX12DrawSrvReplacementUtils::AddSlotRequestsByRegister(
					device, commandList, false,
					binding.slot, binding.resource,
					binding.pixelStage, source, &requests, &bindingCount);
			if (!added) {
				DX12_LOG("DX12DrawSrvReplacement",
					"stage=slot_patch_fail,source=%s,reason=graphics_binding_missing,slot=%u,resource=%S,graphicsCount=%zu",
					source,
					binding.slot,
					binding.resource.c_str(),
					bindingsView ? bindingsView->size() : bindingCount);
			}
		}

		if (!plan.drawReplaceThisResource.empty()) {
			UINT added = DX12DrawSrvReplacementUtils::AddThisRequestFromMatch(
				device, commandList, match,
				plan.drawReplaceThisResource, &requests);
			if (!added) {
				DX12_LOG("DX12DrawSrvReplacement",
					"stage=slot_patch_fail,source=this,reason=no_matched_target,resource=%S,graphicsCount=%zu",
					plan.drawReplaceThisResource.c_str(),
					bindingsView ? bindingsView->size() : 0);
			}
		}

		return DX12DrawSrvReplacementUtils::QueueRequests(requests, replacement);
	}

	static void Restore(
		ID3D12GraphicsCommandList *commandList,
		DX12ModIaReplacement *replacement)
	{
		DX12_DIAG_QPC(restoreBegin);
		if (!replacement)
			return;
		DX12ModIaReplacement::DrawSrvApplyState &state =
			replacement->drawSrvApplyState;
		if (state.bundleMode) {
			// Bundle apply: rebind the session mirrors immediately (no
			// deferred restore exists on bundle lists).
			DX12ModRestoreDrawSrvReplacementBundle(commandList, replacement);
			return;
		}
		if (!commandList || !state.applied || !state.patchHeap) {
			replacement->drawSrvPatches.clear();
			state.ResetForDraw();
			return;
		}
		if (state.borrowedPendingMirror) {
			// This draw reused the still-bound deferred mirror: nothing was
			// emitted for it, so there is nothing to undo. Release the
			// borrow; the pending entry stays live for the next draw.
			ReleaseBorrowedPendingMirror(commandList);
			replacement->drawSrvPatches.clear();
			state.ResetForDraw();
			return;
		}
		// Logical restore only: the shadow trackers return to the game's
		// bindings immediately, while the GPU-side heap/table restore is
		// deferred (see the deferred-restore comment at the top of this
		// file). Any later command that observes descriptor-heap state
		// flushes the pending entry before it is recorded.
		{
			const DX12DrawSrvBindingSnapshot &snapshot = state.originalBindings;
			SyncDescriptorHeaps(
				commandList, snapshot.cbvSrvUavHeap, snapshot.samplerHeap);
			for (UINT root = 0; root < MaxRootParameters; ++root) {
				if (snapshot.graphicsTables[root].valid) {
					SyncRootDescriptorTable(
						commandList, false, root,
						snapshot.graphicsTables[root].baseDescriptor);
				}
				if (snapshot.computeTables[root].valid) {
					SyncRootDescriptorTable(
						commandList, true, root,
						snapshot.computeTables[root].baseDescriptor);
				}
			}
			DX12_PROF_CPU_STAGE(
				"DrawSrv",
				"restore_logical",
				0,
				static_cast<LONG64>(static_cast<size_t>(snapshot.graphicsTableValidCount) +
					snapshot.computeTableValidCount),
				DX12_DIAG_ELAPSED_US(restoreBegin));
		}
		DeferPendingRestore(commandList, &state);
		replacement->drawSrvPatches.clear();
		// The payload was moved into the pending entry above, so only the
		// markers need clearing (see DrawSrvApplyState::ResetForDraw).
		state.ResetForDraw();
	}
};
