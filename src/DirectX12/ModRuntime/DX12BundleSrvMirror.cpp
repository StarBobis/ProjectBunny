// DX12BundleSrvMirror.cpp: bundle draw-SRV replacement entry points.
// Split from DX12ModRuntime.cpp; implementation unit for its module.
#include "DX12ModRuntimeInternal.h"


// Record-time rewrite of a root descriptor table bind inside a bundle. The
// emitted handle is repointed to the session mirror; the shadow trackers keep
// the game's logical handle (the caller captures before invoking this).
void DX12ModRewriteBundleRootDescriptorTable(
	ID3D12GraphicsCommandList *commandList,
	bool graphics,
	UINT rootParameterIndex,
	D3D12_GPU_DESCRIPTOR_HANDLE *baseDescriptor)
{
	if (!commandList || !baseDescriptor || !baseDescriptor->ptr)
		return;
	if (!DX12BundleMirrorUtils::GateOpen())
		return;
	if (commandList->GetType() != D3D12_COMMAND_LIST_TYPE_BUNDLE)
		return;
	if (rootParameterIndex >= MaxRootParameters)
		return;

	DX12BundleMirrorSessionShard &shard = BundleMirrorSessionShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	DX12BundleMirrorSession *session = nullptr;
	auto existing = shard.entries.find(commandList);
	if (existing != shard.entries.end())
		session = &existing->second;

	// Sampler tables are never mirrored: the sampler heap they reference is
	// bound unchanged at exec. Remember the latest sampler heap seen so the
	// parent replay can bind it.
	DX12DescriptorHeapSummary samplerHeap = {};
	if (DX12FindDescriptorHeapByGpuHandle(
			*baseDescriptor, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, &samplerHeap))
	{
		if (session)
		{
			if (session->samplerHeap != samplerHeap.heap)
			{
				if (samplerHeap.heap)
					samplerHeap.heap->AddRef();
				if (session->samplerHeap)
					session->samplerHeap->Release();
				session->samplerHeap = samplerHeap.heap;
			}
			if (graphics)
				session->graphicsBindMask |= (1ull << rootParameterIndex);
			else
				session->computeBindMask |= (1ull << rootParameterIndex);
		}
		ReleaseSRWLockExclusive(&shard.lock);
		return;
	}

	DX12DescriptorHeapSummary heap = {};
	if (!DX12FindDescriptorHeapByGpuHandle(
			*baseDescriptor, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, &heap) ||
		!heap.increment || baseDescriptor->ptr < heap.gpuStart)
	{
		// Unresolvable CBV/SRV/UAV table: it can never be mirrored, so the
		// whole session degrades (the parent still binds the session heap for
		// the handles already rewritten; this bind keeps its raw handle).
		if (session)
			session->overflowed = true;
		ReleaseSRWLockExclusive(&shard.lock);
		return;
	}
	if (!session)
	{
		session = DX12BundleMirrorUtils::EnsureSessionLocked(
			shard, commandList, heap.numDescriptors);
		if (!session)
		{
			ReleaseSRWLockExclusive(&shard.lock);
			return;
		}
	}
	if (session->overflowed)
	{
		ReleaseSRWLockExclusive(&shard.lock);
		return;
	}

	// Table coverage from the current root signature, exactly like the
	// direct-list plan builder.
	DX12SimulateContext *runtimeState =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	ID3D12RootSignature *rootSignature = graphics ?
		(runtimeState ? runtimeState->graphicsRootSignature : nullptr) :
		(runtimeState ? runtimeState->computeRootSignature : nullptr);
	if (!rootSignature)
	{
		session->overflowed = true;
		ReleaseSRWLockExclusive(&shard.lock);
		return;
	}
	DX12RootSignatureSummary rootSummary = {};
	if (!DX12GetRootSignatureSummary(rootSignature, &rootSummary))
	{
		session->overflowed = true;
		ReleaseSRWLockExclusive(&shard.lock);
		return;
	}
	const DX12RootParameterSummary *parameter =
		DX12DrawSrvReplacementUtils::FindRootParameterSummary(rootSummary, rootParameterIndex);
	if (!parameter ||
		parameter->parameterType != D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
	{
		ReleaseSRWLockExclusive(&shard.lock);
		return;
	}
	const UINT64 tableBaseIndex = (baseDescriptor->ptr - heap.gpuStart) / heap.increment;
	const UINT count = DX12DrawSrvReplacementUtils::DescriptorCountForTable(
		*parameter, heap, tableBaseIndex, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	if (!count)
	{
		session->overflowed = true;
		ReleaseSRWLockExclusive(&shard.lock);
		return;
	}
	UINT mirrorOffset = 0;
	if (!DX12BundleMirrorUtils::EnsureTableMirroredLocked(
			*session, baseDescriptor->ptr, heap, count, &mirrorOffset))
	{
		ReleaseSRWLockExclusive(&shard.lock);
		return;
	}
	baseDescriptor->ptr = session->gpuBase.ptr +
		static_cast<UINT64>(mirrorOffset) * session->increment;
	if (graphics)
		session->graphicsBindMask |= (1ull << rootParameterIndex);
	else
		session->computeBindMask |= (1ull << rootParameterIndex);
	ReleaseSRWLockExclusive(&shard.lock);
}

// Record-time rewrite of SetDescriptorHeaps inside a bundle: swap the game's
// CBV_SRV_UAV heap for the session heap (the parent will bind exactly these
// heaps at ExecuteBundle), keep the record-time sampler heap.
void DX12ModRewriteBundleDescriptorHeaps(
	ID3D12GraphicsCommandList *commandList,
	UINT numHeaps,
	ID3D12DescriptorHeap **heaps)
{
	if (!commandList || !heaps || !numHeaps)
		return;
	if (!DX12BundleMirrorUtils::GateOpen())
		return;
	if (commandList->GetType() != D3D12_COMMAND_LIST_TYPE_BUNDLE)
		return;
	UINT cbvHeapDescriptors = 0;
	for (UINT i = 0; i < numHeaps; ++i)
	{
		if (heaps[i] &&
			heaps[i]->GetDesc().Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
		{
			cbvHeapDescriptors = heaps[i]->GetDesc().NumDescriptors;
			break;
		}
	}
	DX12BundleMirrorSessionShard &shard = BundleMirrorSessionShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	DX12BundleMirrorSession *session = nullptr;
	auto existing = shard.entries.find(commandList);
	if (existing != shard.entries.end())
		session = &existing->second;
	else
		session = DX12BundleMirrorUtils::EnsureSessionLocked(
			shard, commandList, cbvHeapDescriptors);
	if (!session)
	{
		ReleaseSRWLockExclusive(&shard.lock);
		return;
	}
	session->sawSetDescriptorHeaps = true;
	for (UINT i = 0; i < numHeaps; ++i)
	{
		if (!heaps[i])
			continue;
		const D3D12_DESCRIPTOR_HEAP_TYPE type = heaps[i]->GetDesc().Type;
		if (type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
			heaps[i] = session->heap;
		else if (type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER &&
			session->samplerHeap != heaps[i])
		{
			heaps[i]->AddRef();
			if (session->samplerHeap)
				session->samplerHeap->Release();
			session->samplerHeap = heaps[i];
		}
	}
	ReleaseSRWLockExclusive(&shard.lock);
}

void DX12ModForgetBundleMirrorSession(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList || gBundleMirrorSessionCount == 0)
		return;
	DX12BundleMirrorSessionShard &shard = BundleMirrorSessionShardFor(commandList);
	DX12BundleMirrorSession session;
	bool found = false;
	AcquireSRWLockExclusive(&shard.lock);
	auto it = shard.entries.find(commandList);
	if (it != shard.entries.end())
	{
		session = std::move(it->second);
		shard.entries.erase(it);
		found = true;
	}
	ReleaseSRWLockExclusive(&shard.lock);
	if (!found)
		return;
	InterlockedDecrement(&gBundleMirrorSessionCount);
	// Retire/release outside the shard lock (Retire takes the retire lock).
	session.Clear();
}

// ---------------------------------------------------------------------------
// ExecuteBundle parent replay. Apply binds the bundle's session heap (+ the
// sampler heap) on the parent, mirrors and rebinds parent tables the bundle
// inherits, and refreshes stale mirror contents; Restore re-establishes the
// parent's own heaps and tables physically after execution.
// ---------------------------------------------------------------------------

class DX12BundleExecReplayState
{
public:
	bool active = false;
	ID3D12GraphicsCommandList *parent = nullptr;
	ID3D12DescriptorHeap *cbvSrvUavHeap = nullptr;	// AddRef'd while active
	ID3D12DescriptorHeap *samplerHeap = nullptr;	// AddRef'd while active
	CommandListBindingState snapshot;

	void Reset()
	{
		if (cbvSrvUavHeap)
			cbvSrvUavHeap->Release();
		if (samplerHeap)
			samplerHeap->Release();
		cbvSrvUavHeap = nullptr;
		samplerHeap = nullptr;
		parent = nullptr;
		snapshot = CommandListBindingState();
		active = false;
	}
};

inline thread_local DX12BundleExecReplayState gBundleExecReplayTls;

bool DX12ModApplyBundleMirrorForExecuteBundle(
	ID3D12GraphicsCommandList *parent,
	ID3D12GraphicsCommandList *bundle)
{
	if (!parent || !bundle || gBundleMirrorSessionCount == 0)
		return false;
	if (parent->GetType() == D3D12_COMMAND_LIST_TYPE_BUNDLE)
		return false;
	DX12BundleMirrorSessionShard &shard = BundleMirrorSessionShardFor(bundle);
	AcquireSRWLockExclusive(&shard.lock);
	auto it = shard.entries.find(bundle);
	if (it == shard.entries.end() || !it->second.heap || !it->second.device) {
		ReleaseSRWLockExclusive(&shard.lock);
		return false;
	}
	DX12BundleMirrorSession &session = it->second;

	// Refresh mirror contents whose source moved since recording; patch
	// copies re-apply their overlays (or degrade to originals across a mod
	// reload).
	DX12BundleMirrorUtils::RefreshSessionLocked(session);

	DX12BundleExecReplayState &replay = gBundleExecReplayTls;
	replay.Reset();
	if (!DX12BindingTracker::SnapshotBindingState(parent, &replay.snapshot)) {
		ReleaseSRWLockExclusive(&shard.lock);
		return false;
	}
	replay.cbvSrvUavHeap = replay.snapshot.cbvSrvUavHeap;
	replay.samplerHeap = replay.snapshot.samplerHeap;
	if (replay.cbvSrvUavHeap)
		replay.cbvSrvUavHeap->AddRef();
	if (replay.samplerHeap)
		replay.samplerHeap->AddRef();

	// When the bundle recorded its own SetDescriptorHeaps, the parent must
	// bind exactly that (rewritten) set; otherwise keep the bundle's
	// record-time sampler heap if one was seen, else the parent's current one.
	ID3D12DescriptorHeap *samplerHeap = session.sawSetDescriptorHeaps ?
		session.samplerHeap :
		(session.samplerHeap ? session.samplerHeap : replay.snapshot.samplerHeap);
	{
		DX12InternalReplayScope internalReplay;
		ID3D12DescriptorHeap *heaps[2] = { session.heap, samplerHeap };
		parent->SetDescriptorHeaps(samplerHeap ? 2 : 1, heaps);
		// Mirror and rebind every parent table the bundle does not bind
		// itself: inherited bindings must resolve into the session heap too.
		for (UINT root = 0; root < MaxRootParameters; ++root) {
			const RootTableState &graphicsTable = replay.snapshot.graphicsTables[root];
			if (graphicsTable.valid && !(session.graphicsBindMask & (1ull << root)))
				DX12BundleMirrorUtils::MirrorInheritedTableLocked(
					session, parent, replay.snapshot,
					graphicsTable, false, root);
			const RootTableState &computeTable = replay.snapshot.computeTables[root];
			if (computeTable.valid && !(session.computeBindMask & (1ull << root)))
				DX12BundleMirrorUtils::MirrorInheritedTableLocked(
					session, parent, replay.snapshot,
					computeTable, true, root);
		}
	}
	replay.parent = parent;
	replay.active = true;
	ID3D12DescriptorHeap *sessionHeap = session.heap;
	ReleaseSRWLockExclusive(&shard.lock);
	// The parent's submission now references the session heap.
	RetainPreSkinDescriptorHeapForCommandList(parent, sessionHeap);
	return true;
}

void DX12ModRestoreBundleMirrorAfterExecuteBundle(
	ID3D12GraphicsCommandList *parent,
	ID3D12GraphicsCommandList *bundle)
{
	(void)bundle;
	if (!parent)
		return;
	DX12BundleExecReplayState &replay = gBundleExecReplayTls;
	if (!replay.active || replay.parent != parent)
		return;
	// Physical restore only; the lifecycle InvalidateAfterExecuteBundle call
	// that follows resets the parent's shadow trackers anyway.
	DX12InternalReplayScope internalReplay;
	ID3D12DescriptorHeap *heaps[2] = {};
	UINT heapCount = 0;
	if (replay.cbvSrvUavHeap)
		heaps[heapCount++] = replay.cbvSrvUavHeap;
	if (replay.samplerHeap)
		heaps[heapCount++] = replay.samplerHeap;
	// SetDescriptorHeaps(0, nullptr) legally unbinds every heap - restores
	// the pre-exec state even when the parent had nothing bound.
	parent->SetDescriptorHeaps(heapCount, heapCount ? heaps : nullptr);
	for (UINT root = 0; root < MaxRootParameters; ++root) {
		if (replay.snapshot.graphicsTables[root].valid) {
			parent->SetGraphicsRootDescriptorTable(
				root, replay.snapshot.graphicsTables[root].baseDescriptor);
		}
		if (replay.snapshot.computeTables[root].valid) {
			parent->SetComputeRootDescriptorTable(
				root, replay.snapshot.computeTables[root].baseDescriptor);
		}
	}
	replay.Reset();
}


// ---------------------------------------------------------------------------
// DX12BundleMirrorUtils: session heap management and table mirroring.
//
// These used to be defined inline in DX12BundleSrvMirror.h, which put them in
// every translation unit that includes the mod-runtime internal header (all of
// them) while every caller lives in this file. The header now declares them.
// ---------------------------------------------------------------------------

bool DX12BundleMirrorUtils::GateOpen()
{
	return gBundleMirrorSessionCount != 0 ||
		DX12ModHasConfiguredTextureOverrides();
}

ID3D12DescriptorHeap *DX12BundleMirrorUtils::CreateSessionHeap(
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

DX12BundleMirrorSession *DX12BundleMirrorUtils::EnsureSessionLocked(
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

bool DX12BundleMirrorUtils::EnsureTableMirroredLocked(
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

bool DX12BundleMirrorUtils::RefreshTableLocked(
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

void DX12BundleMirrorUtils::ApplyPatchOverlaysLocked(
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

void DX12BundleMirrorUtils::MirrorInheritedTableLocked(
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

void DX12BundleMirrorUtils::RefreshSessionLocked(DX12BundleMirrorSession &session)
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


// Bundle variants of the draw-SRV apply/restore (called from
// DX12DrawSrvReplacementUtils::ApplyPending/Restore on bundle lists).
// ---------------------------------------------------------------------------

bool DX12ModApplyDrawSrvReplacementBundle(
	ID3D12Device *device,
	ID3D12GraphicsCommandList *commandList,
	DX12ModIaReplacement *replacement,
	bool executeOriginalDraw)
{
	DX12_DIAG_QPC(applyBegin);
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
		DX12_LOG("DX12DrawSrvReplacement",
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
			DX12_LOG("DX12DrawSrvReplacement",
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
			DX12_LOG("DX12DrawSrvReplacement",
				"stage=bundle_patch_fail,reason=compute_root_signature");
			return false;
		}
		hasComputeRootSignature = true;
	}
	if (tables.empty() || !totalDescriptors) {
		DX12_LOG("DX12DrawSrvReplacement",
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
			DX12_LOG("DX12DrawSrvReplacement",
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
			DX12_LOG("DX12DrawSrvReplacement",
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
		DX12_LOG("DX12DrawSrvReplacement",
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
			DX12_LOG("DX12DrawSrvReplacement",
				"stage=bundle_patch_fail,reason=table_heap_resolve");
			return false;
		}
		UINT mirrorOffset = 0;
		if (!DX12BundleMirrorUtils::EnsureTableMirroredLocked(
				*session, table.gpuStart.ptr, heap, table.count, &mirrorOffset)) {
			ReleaseSRWLockExclusive(&shard.lock);
			DX12_LOG("DX12DrawSrvReplacement",
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
			DX12_LOG("DX12DrawSrvReplacement",
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
	DX12_PROF_CPU_STAGE(
		"DrawSrv",
		"apply_bundle",
		0,
		static_cast<LONG64>(totalDescriptors),
		DX12_DIAG_ELAPSED_US(applyBegin));
	return true;
}

void DX12ModRestoreDrawSrvReplacementBundle(
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
