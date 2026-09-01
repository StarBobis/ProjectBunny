#pragma once

// DX12BundleSrvMirror.h - part of DX12ModRuntimeInternal.h, included from it at the split position.
// Not meant to be included directly.

// ---------------------------------------------------------------------------
// Bundle draw-SRV replacement: record-time mirror + ExecuteBundle parent replay.
//
// A bundle cannot switch to a private descriptor heap by itself: at most one
// CBV_SRV_UAV heap is bound when the bundle executes, and every descriptor
// handle referenced by the bundle's recorded commands must resolve into the
// heaps the parent list has bound at ExecuteBundle time. The direct-list
// approach (mirror tables, swap heaps, draw, lazily restore) is therefore
// impossible inside a bundle - which is why ApplyConfig/ApplyPending used to
// refuse bundle lists outright ("bundle_requires_parent_replay") and texture
// replacements silently went missing for games that draw through bundles,
// while IA geometry replacement (no descriptor involvement) kept working.
//
// Design:
//
//  Record time (bundle list):
//   - A mirror session is created lazily on the bundle's first descriptor
//     table bind / SetDescriptorHeaps / patched draw, gated on configured
//     texture overrides (zero cost when no texture mods are loaded).
//   - EVERY CBV/SRV/UAV root-table bind recorded into the bundle is mirrored
//     into the session-owned shader-visible heap and the emitted handle is
//     rewritten to the mirror copy; the shadow trackers keep the game's
//     logical handles so plan building and matching stay in game terms.
//   - A patched draw additionally builds patch copies (a table copy with the
//     mod SRVs written over the patched slots) inside the same session heap
//     and binds those handles for the draw; Restore rebinds the unpatched
//     session mirrors immediately - a bundle has no deferred restore, every
//     handle in the recorded stream must stay valid for the whole bundle.
//   - SetDescriptorHeaps inside the bundle is rewritten to the session heap
//     (the record-time sampler heap is kept as-is).
//
//  Execute time (parent list, ExecuteBundle):
//   - The parent binds the session heap (+ sampler heap), mirrors and rebinds
//     any parent tables the bundle inherits (root params the bundle never
//     binds itself), and refreshes mirror contents whose source-heap version
//     moved since recording, re-applying the mod overlays to patch copies.
//   - After execution the parent's own heaps and root tables are physically
//     restored; the lifecycle hook then resets the parent's shadow state as
//     before. The session heap is retained for the parent list until
//     submission through the usual pending-submission machinery.
//
//  Lifetime: sessions are keyed by the bundle list and die with Reset /
//  Unregister; the heap is retired through the pre-skin fence drain and may be
//  adopted by later sessions or the descriptor ring once no in-flight
//  submission references it. Config reloads deliberately do NOT kill sessions:
//  a recorded bundle may legally outlive a reload, and its stream still
//  references the session heap. A reload-generation mismatch at exec instead
//  degrades the bundle to original textures by refreshing every patch copy
//  from its game source without re-applying overlays.
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

class DX12BundleMirrorUtils
{
public:
	// Cheap global gate: bundle mirroring only matters when texture overrides
	// exist at all. Two volatile reads keep non-modded games at zero cost.
	static bool GateOpen()
	{
		return gBundleMirrorSessionCount != 0 ||
			DX12ModHasConfiguredTextureOverrides();
	}

	// Allocate (or adopt) the session heap. Adopts the smallest sufficient
	// retired heap whose GPU references provably drained - heap creation is a
	// millisecond-scale driver mapping, so steady-state bundle re-recording
	// must come from the recycle path, same contract as the ring rewind.
	static ID3D12DescriptorHeap *CreateSessionHeap(
		ID3D12Device *device, UINT minCapacity, UINT *capacityOut)
	{
		if (!device || !minCapacity)
			return nullptr;
		ID3D12DescriptorHeap *adopted = nullptr;
		UINT adoptedCapacity = 0;
		std::vector<ID3D12Fence*> adoptedPointFences;
		AcquireSRWLockExclusive(&gPreSkinRetireLock);
		const LONG present = DX12RuntimeState::GetPresentCount();
		for (auto it = gRetiredPreSkinDescriptorHeaps.begin();
			it != gRetiredPreSkinDescriptorHeaps.end(); ++it)
		{
			if (!it->heap)
				continue;
			const UINT heapCapacity = it->heap->GetDesc().NumDescriptors;
			if (heapCapacity < minCapacity)
				continue;
			if (adopted && heapCapacity >= adoptedCapacity)
				continue;
			const bool fenceComplete =
				it->completionFence && it->completionValue &&
				it->completionFence->GetCompletedValue() >= it->completionValue;
			const bool fallbackExpired = !it->completionFence &&
				present - it->retirePresent >= PreSkinRetiredObjectPresentDelay;
			if (!fenceComplete && !fallbackExpired)
				continue;
			if (!IsPreSkinRingHeapReusableLocked(it->heap, 0))
				continue;
			ID3D12Device *heapDevice = nullptr;
			if (FAILED(it->heap->GetDevice(IID_PPV_ARGS(&heapDevice))) || !heapDevice)
				continue;
			const bool deviceMatches = heapDevice == device;
			heapDevice->Release();
			if (!deviceMatches)
				continue;
			adopted = it->heap;
			adoptedCapacity = heapCapacity;
		}
		if (adopted)
		{
			for (auto it = gRetiredPreSkinDescriptorHeaps.begin();
				it != gRetiredPreSkinDescriptorHeaps.end(); ++it)
			{
				if (it->heap != adopted)
					continue;
				if (it->completionFence)
					it->completionFence->Release();
				gRetiredPreSkinDescriptorHeaps.erase(it);
				break;
			}
			ClearPreSkinHeapCompletionPointsLocked(adopted, &adoptedPointFences);
		}
		ReleaseSRWLockExclusive(&gPreSkinRetireLock);
		for (ID3D12Fence *fence : adoptedPointFences)
		{
			if (fence)
				fence->Release();
		}
		if (adopted)
		{
			if (capacityOut)
				*capacityOut = adoptedCapacity;
			return adopted;
		}

		D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
		heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		heapDesc.NumDescriptors = minCapacity;
		heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		ID3D12DescriptorHeap *heap = nullptr;
		if (FAILED(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&heap))) || !heap)
			return nullptr;
		if (capacityOut)
			*capacityOut = minCapacity;
		return heap;
	}

	// Fetch or create the mirror session for a bundle list. The session heap
	// is sized off the first source heap seen (plus patch headroom) and can
	// never grow afterwards - handles into it are baked into the recorded
	// bundle stream, so a reallocation would invalidate them.
	static DX12BundleMirrorSession *EnsureSessionLocked(
		DX12BundleMirrorSessionShard &shard,
		ID3D12GraphicsCommandList *commandList,
		UINT firstSourceHeapDescriptors)
	{
		auto it = shard.entries.find(commandList);
		if (it != shard.entries.end())
			return &it->second;
		ID3D12Device *device = AcquireModDevice(commandList);
		if (!device)
			return nullptr;
		const UINT reserve = (std::max)(firstSourceHeapDescriptors, 4096u) +
			(std::max)(firstSourceHeapDescriptors / 2, 1024u);
		UINT capacity = 0;
		ID3D12DescriptorHeap *heap = CreateSessionHeap(device, reserve, &capacity);
		if (!heap)
		{
			device->Release();
			return nullptr;
		}
		DX12BundleMirrorSession &session = shard.entries[commandList];
		session.device = device;
		session.heap = heap;
		session.capacity = capacity;
		session.offset = 0;
		session.increment = device->GetDescriptorHandleIncrementSize(
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		session.cpuBase = heap->GetCPUDescriptorHandleForHeapStart();
		session.gpuBase = heap->GetGPUDescriptorHandleForHeapStart();
		session.reloadGeneration = DX12ModGetReloadGenerationFast();
		InterlockedIncrement(&gBundleMirrorSessionCount);
		return &session;
	}

	// Mirror a source table into the session heap (idempotent per table
	// handle). Contents are copied at record time; exec-time refresh re-copies
	// them if the source heap's version moved.
	static bool EnsureTableMirroredLocked(
		DX12BundleMirrorSession &session,
		UINT64 gpuStart,
		const DX12DescriptorHeapSummary &sourceHeap,
		UINT count,
		UINT *mirrorOffsetOut)
	{
		if (!mirrorOffsetOut || !gpuStart || !count || !sourceHeap.cpuStart)
			return false;
		auto it = session.tables.find(gpuStart);
		if (it != session.tables.end())
		{
			if (it->second.count >= count)
			{
				*mirrorOffsetOut = it->second.mirrorOffset;
				return true;
			}
			// Same table base rebound under a root signature that covers more
			// descriptors: allocate a wider region and repoint the entry (the
			// stale region stays as dead space, bounded by the session size).
		}
		if (session.offset > session.capacity ||
			count > session.capacity - session.offset)
		{
			session.overflowed = true;
			return false;
		}
		const UINT mirrorOffset = session.offset;
		session.offset += count;
		D3D12_CPU_DESCRIPTOR_HANDLE dst = session.cpuBase;
		dst.ptr += static_cast<SIZE_T>(mirrorOffset) * session.increment;
		const UINT64 tableDelta = gpuStart - sourceHeap.gpuStart;
		D3D12_CPU_DESCRIPTOR_HANDLE src = {};
		src.ptr = sourceHeap.cpuStart +
			static_cast<SIZE_T>(tableDelta / sourceHeap.increment) * sourceHeap.increment;
		session.device->CopyDescriptors(
			1, &dst, &count, 1, &src, &count,
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		DX12BundleMirrorTable &entry = session.tables[gpuStart];
		entry.gpuStart = gpuStart;
		entry.cpuStart = src.ptr;
		entry.sourceHeap = sourceHeap.heap;
		entry.sourceHeapVersion = sourceHeap.version;
		entry.count = count;
		entry.mirrorOffset = mirrorOffset;
		*mirrorOffsetOut = mirrorOffset;
		return true;
	}

	// Refresh mirrored contents whose source heap version moved. Returns true
	// when the copy was refreshed (patch copies then re-apply their overlays).
	static bool RefreshTableLocked(
		DX12BundleMirrorSession &session,
		SIZE_T cpuStart,
		ID3D12DescriptorHeap *sourceHeap,
		UINT64 *sourceHeapVersion,
		UINT mirrorOffset,
		UINT count,
		bool force)
	{
		if (!session.device || !sourceHeap || !cpuStart || !count)
			return false;
		UINT64 version = *sourceHeapVersion;
		DX12DescriptorHeapSummary summary = {};
		if (DX12FindDescriptorHeapByPtr(sourceHeap, &summary))
			version = summary.version;
		if (!force && version == *sourceHeapVersion)
			return false;
		D3D12_CPU_DESCRIPTOR_HANDLE dst = session.cpuBase;
		dst.ptr += static_cast<SIZE_T>(mirrorOffset) * session.increment;
		D3D12_CPU_DESCRIPTOR_HANDLE src = {};
		src.ptr = cpuStart;
		session.device->CopyDescriptors(
			1, &dst, &count, 1, &src, &count,
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		*sourceHeapVersion = version;
		return true;
	}

	static void ApplyPatchOverlaysLocked(
		DX12BundleMirrorSession &session,
		const DX12BundlePatchCopy &copy)
	{
		for (UINT index = copy.overlayBegin;
			index < copy.overlayEnd && index < session.overlays.size(); ++index)
		{
			const DX12BundlePatchOverlay &overlay = session.overlays[index];
			if (!overlay.replacementCpu.ptr || overlay.slot >= copy.count)
				continue;
			D3D12_CPU_DESCRIPTOR_HANDLE dst = session.cpuBase;
			dst.ptr += static_cast<SIZE_T>(copy.mirrorOffset + overlay.slot) *
				session.increment;
			session.device->CopyDescriptors(
				1, &dst, nullptr, 1, &overlay.replacementCpu, nullptr,
				D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		}
	}

	// Mirror a parent-list table the bundle inherits (the bundle never binds
	// that root parameter itself) and rebind it on the parent to the session
	// mirror, so the inherited binding resolves into the session heap too.
	static void MirrorInheritedTableLocked(
		DX12BundleMirrorSession &session,
		ID3D12GraphicsCommandList *parent,
		const CommandListBindingState &parentSnapshot,
		const RootTableState &tableState,
		bool compute,
		UINT rootParameterIndex)
	{
		if (!parent || !tableState.valid || !tableState.baseDescriptor.ptr ||
				session.overflowed)
			return;
		DX12DescriptorHeapSummary heap = {};
		if (!DX12FindDescriptorHeapByGpuHandle(
				tableState.baseDescriptor,
				D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, &heap) ||
			!heap.increment ||
			tableState.baseDescriptor.ptr < heap.gpuStart)
			return;	// sampler tables stay on the bound sampler heap
		ID3D12RootSignature *rootSignature = compute ?
			parentSnapshot.computeRootSignature : parentSnapshot.graphicsRootSignature;
		if (!rootSignature)
			return;
		DX12RootSignatureSummary rootSummary = {};
		if (!DX12GetRootSignatureSummary(rootSignature, &rootSummary))
			return;
		const DX12RootParameterSummary *parameter =
			DX12DrawSrvReplacementUtils::FindRootParameterSummary(
				rootSummary, rootParameterIndex);
		if (!parameter ||
			parameter->parameterType != D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
			return;
		const UINT64 tableBaseIndex =
			(tableState.baseDescriptor.ptr - heap.gpuStart) / heap.increment;
		const UINT count = DX12DrawSrvReplacementUtils::DescriptorCountForTable(
			*parameter, heap, tableBaseIndex, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		if (!count)
			return;
		UINT mirrorOffset = 0;
		if (!EnsureTableMirroredLocked(
				session, tableState.baseDescriptor.ptr, heap, count, &mirrorOffset))
			return;	// overflowed flag set inside; param keeps its raw handle
		D3D12_GPU_DESCRIPTOR_HANDLE mirror = session.gpuBase;
		mirror.ptr += static_cast<UINT64>(mirrorOffset) * session.increment;
		if (compute)
			parent->SetComputeRootDescriptorTable(rootParameterIndex, mirror);
		else
			parent->SetGraphicsRootDescriptorTable(rootParameterIndex, mirror);
	}

	static void RefreshSessionLocked(DX12BundleMirrorSession &session)
	{
		const UINT64 reloadGeneration = DX12ModGetReloadGenerationFast();
		const bool generationMatch = session.reloadGeneration == reloadGeneration;
		for (auto &item : session.tables)
		{
			RefreshTableLocked(
				session, item.second.cpuStart, item.second.sourceHeap,
				&item.second.sourceHeapVersion, item.second.mirrorOffset,
				item.second.count, false);
		}
		for (DX12BundlePatchCopy &copy : session.patchCopies)
		{
			// A reload frees the mod resources the overlays point at: force a
			// refresh from the game source and never re-apply stale overlays,
			// degrading the bundle to original textures until it is
			// re-recorded under the new generation.
			const bool refreshed = RefreshTableLocked(
				session, copy.cpuStart, copy.sourceHeap,
				&copy.sourceHeapVersion, copy.mirrorOffset, copy.count,
				!generationMatch);
			if (refreshed && generationMatch)
				ApplyPatchOverlaysLocked(session, copy);
		}
		if (!generationMatch)
			session.reloadGeneration = reloadGeneration;
	}
};

// Bundle variants of the draw-SRV apply/restore (called from
// DX12DrawSrvReplacementUtils::ApplyPending/Restore on bundle lists).
// ---------------------------------------------------------------------------

inline bool DX12ModApplyDrawSrvReplacementBundle(
	ID3D12Device *device,
	ID3D12GraphicsCommandList *commandList,
	DX12ModIaReplacement *replacement,
	bool executeOriginalDraw)
{
	const LARGE_INTEGER applyBegin = DX12OuterTimingProbeUtils::Now();
	if (!device || !commandList || !replacement ||
			replacement->drawSrvPatches.empty())
		return false;
	if (replacement->drawSrvApplyState.applied)
		return false;

	// Same plan as the direct path: snapshot the logical (game-handle)
	// bindings, mirror every table the draw can touch, validate the patch
	// requests against the plan.
	static thread_local CommandListBindingState snapshot;
	if (!DX12BindingTracker::SnapshotBindingState(commandList, &snapshot)) {
		DX12LogManager::Fields("DX12DrawSrvReplacement",
			"stage=bundle_patch_fail,reason=binding_snapshot");
		return false;
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
				!DX12DrawSrvReplacementUtils::AppendPatchTables(
					snapshot, graphicsRootSignature, false,
					&tables, &totalDescriptors)) {
			DX12LogManager::Fields("DX12DrawSrvReplacement",
				"stage=bundle_patch_fail,reason=graphics_root_signature");
			return false;
		}
		hasGraphicsRootSignature = true;
	}
	if (needsComputeTables && snapshot.computeTableValidCount) {
		if (!snapshot.computeRootSignature ||
				!DX12GetRootSignatureSummary(
					snapshot.computeRootSignature, &computeRootSignature) ||
				!DX12DrawSrvReplacementUtils::AppendPatchTables(
					snapshot, computeRootSignature, true,
					&tables, &totalDescriptors)) {
			DX12LogManager::Fields("DX12DrawSrvReplacement",
				"stage=bundle_patch_fail,reason=compute_root_signature");
			return false;
		}
		hasComputeRootSignature = true;
	}
	if (tables.empty() || !totalDescriptors) {
		DX12LogManager::Fields("DX12DrawSrvReplacement",
			"stage=bundle_patch_fail,reason=table_plan");
		return false;
	}
	for (const DX12ModIaReplacement::DrawSrvTablePatch &request :
			replacement->drawSrvPatches) {
		const DX12RootSignatureSummary *requestRootSignature =
			request.compute ?
				(hasComputeRootSignature ? &computeRootSignature : nullptr) :
				(hasGraphicsRootSignature ? &graphicsRootSignature : nullptr);
		if (!requestRootSignature ||
				!DX12DrawSrvReplacementUtils::RequestMatchesSrvLayout(
					request, *requestRootSignature)) {
			DX12LogManager::Fields("DX12DrawSrvReplacement",
				"stage=bundle_patch_fail,reason=request_layout");
			return false;
		}
		bool found = false;
		for (const DX12DrawSrvPatchTable &table : tables) {
			if (table.sampler ||
					table.compute != request.compute ||
					table.rootParameterIndex != request.binding.rootParameterIndex ||
					table.gpuStart.ptr != request.binding.tableGpuStart.ptr)
				continue;
			if (request.binding.descriptorOffset >= table.count)
				return false;
			found = true;
			break;
		}
		if (!found || !request.replacementCpu.ptr) {
			DX12LogManager::Fields("DX12DrawSrvReplacement",
				"stage=bundle_patch_fail,reason=request_table");
			return false;
		}
	}

	DX12BundleMirrorSessionShard &shard = BundleMirrorSessionShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	DX12BundleMirrorSession *session = nullptr;
	auto existing = shard.entries.find(commandList);
	if (existing != shard.entries.end()) {
		session = &existing->second;
	} else {
		UINT sourceHeapDescriptors = 0;
		for (const DX12DrawSrvPatchTable &table : tables) {
			if (table.sampler || !table.heap)
				continue;
			DX12DescriptorHeapSummary summary = {};
			if (DX12FindDescriptorHeapByPtr(table.heap, &summary)) {
				sourceHeapDescriptors = summary.numDescriptors;
				break;
			}
		}
		session = DX12BundleMirrorUtils::EnsureSessionLocked(
			shard, commandList, sourceHeapDescriptors);
	}
	if (!session || session->overflowed) {
		ReleaseSRWLockExclusive(&shard.lock);
		DX12LogManager::Fields("DX12DrawSrvReplacement",
			"stage=bundle_patch_fail,reason=session_unavailable");
		return false;
	}

	// Mirror every plan table into the session heap; remember the sampler
	// heap for the exec-time replay.
	static thread_local std::vector<UINT> tlsTableMirrorOffsets;
	tlsTableMirrorOffsets.clear();
	tlsTableMirrorOffsets.reserve(tables.size());
	for (const DX12DrawSrvPatchTable &table : tables) {
		if (table.sampler) {
			if (table.heap && session->samplerHeap != table.heap) {
				table.heap->AddRef();
				if (session->samplerHeap)
					session->samplerHeap->Release();
				session->samplerHeap = table.heap;
			}
			tlsTableMirrorOffsets.push_back(0);
			continue;
		}
		DX12DescriptorHeapSummary heap = {};
		if (!DX12FindDescriptorHeapByGpuHandle(
				table.gpuStart, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, &heap)) {
			session->overflowed = true;
			ReleaseSRWLockExclusive(&shard.lock);
			DX12LogManager::Fields("DX12DrawSrvReplacement",
				"stage=bundle_patch_fail,reason=table_heap_resolve");
			return false;
		}
		UINT mirrorOffset = 0;
		if (!DX12BundleMirrorUtils::EnsureTableMirroredLocked(
				*session, table.gpuStart.ptr, heap, table.count, &mirrorOffset)) {
			ReleaseSRWLockExclusive(&shard.lock);
			DX12LogManager::Fields("DX12DrawSrvReplacement",
				"stage=bundle_patch_fail,reason=session_capacity");
			return false;
		}
		tlsTableMirrorOffsets.push_back(mirrorOffset);
	}

	// Build one patch copy per patched plan table (a table copy with the mod
	// SRVs written over the patched slots), deduplicated by content key so
	// repeated draws of the same patched table share the copy.
	static thread_local std::vector<int> tlsTablePatchCopies;
	tlsTablePatchCopies.assign(tables.size(), -1);
	for (size_t tableIndex = 0; tableIndex < tables.size(); ++tableIndex) {
		const DX12DrawSrvPatchTable &table = tables[tableIndex];
		if (table.sampler)
			continue;
		UINT64 key = 14695981039346656037ull;
		bool hasRequest = false;
		auto appendKey = [&key](UINT64 value) {
			for (size_t i = 0; i < sizeof(value); ++i) {
				key ^= static_cast<unsigned char>((value >> (i * 8)) & 0xff);
				key *= 1099511628211ull;
			}
		};
		appendKey(table.gpuStart.ptr);
		for (const DX12ModIaReplacement::DrawSrvTablePatch &request :
				replacement->drawSrvPatches) {
			if (request.compute != table.compute ||
					request.binding.rootParameterIndex != table.rootParameterIndex ||
					request.binding.tableGpuStart.ptr != table.gpuStart.ptr)
				continue;
			hasRequest = true;
			appendKey(request.binding.descriptorOffset);
			appendKey(request.replacementCpu.ptr);
		}
		if (!hasRequest)
			continue;
		auto kit = session->patchCopyByKey.find(key);
		if (kit != session->patchCopyByKey.end()) {
			tlsTablePatchCopies[tableIndex] = static_cast<int>(kit->second);
			continue;
		}
		if (session->offset > session->capacity ||
				table.count > session->capacity - session->offset) {
			session->overflowed = true;
			ReleaseSRWLockExclusive(&shard.lock);
			DX12LogManager::Fields("DX12DrawSrvReplacement",
				"stage=bundle_patch_fail,reason=patch_capacity");
			return false;
		}
		const DX12BundleMirrorTable &mirrored =
			session->tables[table.gpuStart.ptr];
		const UINT mirrorOffset = session->offset;
		session->offset += table.count;
		D3D12_CPU_DESCRIPTOR_HANDLE dst = session->cpuBase;
		dst.ptr += static_cast<SIZE_T>(mirrorOffset) * session->increment;
		D3D12_CPU_DESCRIPTOR_HANDLE src = {};
		src.ptr = mirrored.cpuStart;
		session->device->CopyDescriptors(
			1, &dst, &table.count, 1, &src, &table.count,
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		DX12BundlePatchCopy copy;
		copy.cpuStart = mirrored.cpuStart;
		copy.sourceHeap = mirrored.sourceHeap;
		copy.sourceHeapVersion = mirrored.sourceHeapVersion;
		copy.count = table.count;
		copy.mirrorOffset = mirrorOffset;
		copy.overlayBegin = static_cast<UINT>(session->overlays.size());
		for (const DX12ModIaReplacement::DrawSrvTablePatch &request :
				replacement->drawSrvPatches) {
			if (request.compute != table.compute ||
					request.binding.rootParameterIndex != table.rootParameterIndex ||
					request.binding.tableGpuStart.ptr != table.gpuStart.ptr)
				continue;
			D3D12_CPU_DESCRIPTOR_HANDLE slotDst = session->cpuBase;
			slotDst.ptr += static_cast<SIZE_T>(
				mirrorOffset + request.binding.descriptorOffset) * session->increment;
			session->device->CopyDescriptors(
				1, &slotDst, nullptr, 1, &request.replacementCpu, nullptr,
				D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			DX12BundlePatchOverlay overlay;
			overlay.slot = request.binding.descriptorOffset;
			overlay.replacementCpu = request.replacementCpu;
			session->overlays.push_back(overlay);
		}
		copy.overlayEnd = static_cast<UINT>(session->overlays.size());
		session->patchCopyByKey.emplace(key, session->patchCopies.size());
		session->patchCopies.push_back(copy);
		tlsTablePatchCopies[tableIndex] =
			static_cast<int>(session->patchCopies.size() - 1);
	}

	// Bind the patch copies for the draw. Unpatched tables need no emit: the
	// session mirror is already bound for params the bundle bound itself,
	// and the ExecuteBundle replay mirrors inherited parent tables.
	std::vector<DX12ModIaReplacement::DrawSrvBundleRestore> bundleRestores;
	{
		DX12InternalReplayScope internalReplay;
		for (size_t tableIndex = 0; tableIndex < tables.size(); ++tableIndex) {
			const int copyIndex = tlsTablePatchCopies[tableIndex];
			if (copyIndex < 0)
				continue;
			const DX12DrawSrvPatchTable &table = tables[tableIndex];
			const DX12BundlePatchCopy &copy = session->patchCopies[copyIndex];
			D3D12_GPU_DESCRIPTOR_HANDLE patched = session->gpuBase;
			patched.ptr += static_cast<UINT64>(copy.mirrorOffset) * session->increment;
			if (table.compute)
				commandList->SetComputeRootDescriptorTable(
					table.rootParameterIndex, patched);
			else
				commandList->SetGraphicsRootDescriptorTable(
					table.rootParameterIndex, patched);
			// The shadow trackers keep the logical game handle.
			DX12DrawSrvReplacementUtils::SyncRootDescriptorTable(
				commandList, table.compute, table.rootParameterIndex, table.gpuStart);
			DX12ModIaReplacement::DrawSrvBundleRestore restore;
			restore.rootParameterIndex = table.rootParameterIndex;
			restore.compute = table.compute;
			restore.emitTable = session->gpuBase;
			restore.emitTable.ptr += static_cast<UINT64>(
				tlsTableMirrorOffsets[tableIndex]) * session->increment;
			restore.trackTable = table.gpuStart;
			bundleRestores.push_back(restore);
		}
	}
	replacement->drawSrvApplyState.applied = true;
	replacement->drawSrvApplyState.bundleMode = true;
	replacement->drawSrvApplyState.bundleRestores = std::move(bundleRestores);
	ReleaseSRWLockExclusive(&shard.lock);
	DX12OuterTimingProbeUtils::LogCpuStage(
		"DrawSrv",
		"apply_bundle",
		0,
		totalDescriptors,
		DX12OuterTimingProbeUtils::ElapsedUs(applyBegin));
	return true;
}

inline void DX12ModRestoreDrawSrvReplacementBundle(
	ID3D12GraphicsCommandList *commandList,
	DX12ModIaReplacement *replacement)
{
	if (!commandList || !replacement)
		return;
	DX12ModIaReplacement::DrawSrvApplyState &state =
		replacement->drawSrvApplyState;
	if (state.applied && state.bundleMode) {
		// Immediate in-bundle restore: rebind the unpatched session mirrors
		// so later draws in the same bundle read original contents. There is
		// no deferred restore on bundle lists.
		DX12InternalReplayScope internalReplay;
		for (const DX12ModIaReplacement::DrawSrvBundleRestore &restore :
				state.bundleRestores) {
			if (!restore.emitTable.ptr)
				continue;
			if (restore.compute)
				commandList->SetComputeRootDescriptorTable(
					restore.rootParameterIndex, restore.emitTable);
			else
				commandList->SetGraphicsRootDescriptorTable(
					restore.rootParameterIndex, restore.emitTable);
			DX12DrawSrvReplacementUtils::SyncRootDescriptorTable(
				commandList, restore.compute, restore.rootParameterIndex,
				restore.trackTable);
		}
	}
	replacement->drawSrvPatches.clear();
	state = DX12ModIaReplacement::DrawSrvApplyState();
}

// ---------------------------------------------------------------------------
