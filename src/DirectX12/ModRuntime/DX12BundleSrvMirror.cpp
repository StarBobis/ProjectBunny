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
