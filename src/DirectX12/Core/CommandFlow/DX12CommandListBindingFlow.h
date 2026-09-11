#pragma once

// Command-list compute binding flow, lifecycle, and command recorder.
// Split out of Core/DX12CommandFlow.h; include that umbrella header from existing code.
#include "../DX12HotPath.h"

void DX12ModRestorePreSkinningUavReplacement(
	ID3D12GraphicsCommandList *commandList);
void DX12ModFlushDrawSrvReplacement(
	ID3D12GraphicsCommandList *commandList);

class DX12CommandListComputeBindingFlow
{
public:
	// Every game binding mutation funnels here before the capture updates any
	// shadow state. Deferred descriptor replacements must restore first: the
	// game's handles target its own heaps, which a deferred patched mirror
	// has currently unbound on this list.
	//
	// The restore is real work on every call: a global pending counter test, a
	// shard TLS negative-cache compare against the shard's mutation generation,
	// and on a miss the exclusive shard lock plus a map lookup. Each public
	// Capture* entry point runs it and then delegates to its runtimeState
	// overload below, and no binding is mutated in between, so those overloads
	// take `deferredRestoreDone` and skip the duplicate probe: the restore still
	// runs exactly once per API call, the entry-point early-return paths that
	// never reach the overload already ran it, and direct callers (the
	// command-list wrapper, the WrapperRuntimeUtils fast paths) leave the flag
	// false and keep the restore exactly where it was.
	static void RestoreDeferredReplacementsBeforeBindingMutation(
		ID3D12GraphicsCommandList *commandList)
	{
		if (DX12InternalReplayState::IsActive())
			return;
		DX12ModRestorePreSkinningUavReplacement(commandList);
		DX12ModFlushDrawSrvReplacement(commandList);
	}

	static void ResolveDescriptorHeaps(
		UINT count,
		ID3D12DescriptorHeap *const *heaps,
		ID3D12DescriptorHeap **cbvSrvUav,
		ID3D12DescriptorHeap **sampler)
	{
		if (cbvSrvUav)
			*cbvSrvUav = nullptr;
		if (sampler)
			*sampler = nullptr;
		if (!heaps)
			return;

		for (UINT i = 0; i < count; ++i) {
			if (!heaps[i])
				continue;
			D3D12_DESCRIPTOR_HEAP_DESC desc = heaps[i]->GetDesc();
			if (desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) {
				if (cbvSrvUav)
					*cbvSrvUav = heaps[i];
			} else if (desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER) {
				if (sampler)
					*sampler = heaps[i];
			}
		}
	}

	static void CaptureDescriptorHeaps(
		ID3D12GraphicsCommandList *commandList,
		UINT count,
		ID3D12DescriptorHeap *const *heaps)
	{
		RestoreDeferredReplacementsBeforeBindingMutation(commandList);
		const UINT64 flags = DX12HotPathState::HookWorkFlags.load(std::memory_order_relaxed);
		if (!DX12HotPathState::NeedsAnyResidentDescriptorState(flags) &&
			DX12HotPathState::skipBindings.load(std::memory_order_relaxed)) {
			DX12_PROF_COUNT("FastForward");
			DX12_PROF_COUNT("Rm.DescriptorRecordFastSkips");
			return;
		}
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12CommandListComputeBindingFlow::CaptureDescriptorHeaps(
			commandList, runtimeState, count, heaps, true);
	}

	static void CaptureDescriptorHeaps(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		UINT count,
		ID3D12DescriptorHeap *const *heaps,
		bool deferredRestoreDone = false)
	{
		if (DX12InternalReplayState::IsActive())
			return;
		if (!deferredRestoreDone)
			RestoreDeferredReplacementsBeforeBindingMutation(commandList);
		// One snapshot of the hot-path gates for this invocation: the value is
		// read again below, and a cross-thread publish between two reads would
		// let the resident heap update above be skipped by a later test that
		// disagrees with the earlier one.
		const UINT64 flags = DX12HotPathState::HookWorkFlags.load(std::memory_order_relaxed);
		const bool skipBindings =
			DX12HotPathState::skipBindings.load(std::memory_order_relaxed);
		const bool skipComputeBinding =
			DX12HotPathState::skipAll.load(std::memory_order_relaxed) &&
			!(flags &
				(DX12HotPathState::FLAG_PRE_SKIN_PROBE |
				 DX12HotPathState::FLAG_PRE_SKIN_BINDING_TRACK));
		// Probe-only PreSkin keeps skipAll set, but it still needs the resident
		// descriptor heaps for compute-side matching.
		if (skipComputeBinding) {
			DX12_PROF_COUNT("FastForward");
			DX12_PROF_COUNT("Rm.DescriptorRecordFastSkips");
			return;
		}

		// Fast path: identical heap set as the last resident capture and no
		// tracked binding recording needed. The resident heap state already
		// reflects this exact input, so skip the per-heap GetDesc resolution
		// entirely. Games re-bind the same heap set thousands of times per
		// frame; this is the highest-frequency binding entry point.
		if (runtimeState && heaps && count <= 2 &&
		    skipBindings &&
		    runtimeState->lastSetHeapsCount == count &&
		    (count == 0 ||
		     (runtimeState->lastSetHeapsInput[0] == heaps[0] &&
		      (count < 2 || runtimeState->lastSetHeapsInput[1] == heaps[1])))) {
			DX12_PROF_COUNT("FastForward");
			DX12_PROF_COUNT("Rm.DescriptorRecordFastSkips");
			return;
		}

		ID3D12DescriptorHeap *cbvSrvUav = nullptr;
		ID3D12DescriptorHeap *sampler = nullptr;
		ResolveDescriptorHeaps(count, heaps, &cbvSrvUav, &sampler);
		if (DX12HotPathState::NeedsAnyResidentDescriptorState(flags)) {
			if (runtimeState)
				DX12CommandListRuntime::SetDescriptorHeaps(runtimeState, cbvSrvUav, sampler);
			else
				DX12CommandListRuntime::SetDescriptorHeaps(commandList, cbvSrvUav, sampler);

			// Only remember the input once the resident state reflects it;
			// recording without updating would let a later flag change skip a
			// resident update that never happened.
			if (runtimeState) {
				if (heaps && count <= 2) {
					runtimeState->lastSetHeapsCount = count;
					runtimeState->lastSetHeapsInput[0] = count > 0 ? heaps[0] : nullptr;
					runtimeState->lastSetHeapsInput[1] = count > 1 ? heaps[1] : nullptr;
				} else {
					runtimeState->lastSetHeapsCount = 0xFFFFFFFF;
				}
			}
		}
		if (skipBindings) {
			DX12_PROF_COUNT("FastForward");
			DX12_PROF_COUNT("Rm.DescriptorRecordFastSkips");
			return;
		}

				DX12CommandListStateCapture::DescriptorHeaps(
			commandList, runtimeState, cbvSrvUav, sampler);
	}

	static bool CaptureRootSignature(
		ID3D12GraphicsCommandList *commandList,
		ID3D12RootSignature *rootSignature,
		bool graphics)
	{
		RestoreDeferredReplacementsBeforeBindingMutation(commandList);
		const UINT64 flags = DX12HotPathState::HookWorkFlags.load(std::memory_order_relaxed);
		const bool needsResidentBinding = graphics ?
			DX12HotPathState::NeedsGraphicsResidentDescriptorState(flags) :
			DX12HotPathState::NeedsComputeResidentBindingState(flags);
		const bool skipTrackedCapture = graphics ?
			!DX12HotPathState::NeedsGraphicsTrackedBindingCapture(flags) :
			DX12HotPathState::skipBindings.load(std::memory_order_relaxed);
		if (!needsResidentBinding && skipTrackedCapture) {
			DX12_PROF_COUNT("FastForward");
			return true;
		}
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		return DX12CommandListComputeBindingFlow::CaptureRootSignature(
			commandList, runtimeState, rootSignature, graphics, true);
	}

	static bool CaptureRootSignature(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		ID3D12RootSignature *rootSignature,
		bool graphics,
		bool deferredRestoreDone = false)
	{
		if (!deferredRestoreDone)
			RestoreDeferredReplacementsBeforeBindingMutation(commandList);
		if (DX12InternalReplayState::IsActive())
			return true;
		const UINT64 flags = DX12HotPathState::HookWorkFlags.load(std::memory_order_relaxed);
		const bool needsResidentBinding = graphics ?
			DX12HotPathState::NeedsGraphicsResidentDescriptorState(flags) :
			DX12HotPathState::NeedsComputeResidentBindingState(flags);
		bool changed = true;
		if (needsResidentBinding) {
			if (graphics) {
				if (runtimeState)
					changed = DX12CommandListRuntime::RememberGraphicsRootSignature(
						runtimeState, rootSignature);
				else
					changed = DX12CommandListRuntime::RememberGraphicsRootSignature(
						commandList, rootSignature);
			} else {
				if (runtimeState)
					changed = DX12CommandListRuntime::RememberComputeRootSignature(
						runtimeState, rootSignature);
				else
					changed = DX12CommandListRuntime::RememberComputeRootSignature(
						commandList, rootSignature);
			}
		}
		const bool skipTrackedCapture = graphics ?
			!DX12HotPathState::NeedsGraphicsTrackedBindingCapture(flags) :
			DX12HotPathState::skipBindings.load(std::memory_order_relaxed);
		if (skipTrackedCapture) {
			DX12_PROF_COUNT("FastForward");
			DX12_PROF_COUNT("Rm.DescriptorRecordFastSkips");
			return changed;
		}

				if (graphics) {
			DX12CommandListStateCapture::GraphicsRootSignature(
				commandList, runtimeState, rootSignature);
		} else {
			DX12CommandListStateCapture::ComputeRootSignature(
				commandList, runtimeState, rootSignature);
		}
		return true;
	}

	static bool CaptureRootDescriptorTable(
		ID3D12GraphicsCommandList *commandList,
		UINT rootParameterIndex,
		D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor,
		bool graphics)
	{
		RestoreDeferredReplacementsBeforeBindingMutation(commandList);
		const UINT64 flags = DX12HotPathState::HookWorkFlags.load(std::memory_order_relaxed);
		const bool needsResidentTable = graphics ?
			DX12HotPathState::NeedsGraphicsResidentDescriptorState(flags) :
			DX12HotPathState::NeedsComputeResidentDescriptorState(flags);
		const bool skipTrackedCapture = graphics ?
			!DX12HotPathState::NeedsGraphicsTrackedBindingCapture(flags) :
			DX12HotPathState::skipBindings.load(std::memory_order_relaxed);
		if (!needsResidentTable && skipTrackedCapture) {
			DX12_PROF_COUNT("FastForward");
			return true;
		}
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		return DX12CommandListComputeBindingFlow::CaptureRootDescriptorTable(
			commandList, runtimeState, rootParameterIndex, baseDescriptor, graphics, true);
	}

	static bool CaptureRootDescriptorTable(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		UINT rootParameterIndex,
		D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor,
		bool graphics,
		bool deferredRestoreDone = false)
	{
		if (DX12InternalReplayState::IsActive())
			return true;
		if (!deferredRestoreDone)
			RestoreDeferredReplacementsBeforeBindingMutation(commandList);
		const UINT64 flags = DX12HotPathState::HookWorkFlags.load(std::memory_order_relaxed);
		const bool needsResidentTable = graphics ?
			DX12HotPathState::NeedsGraphicsResidentDescriptorState(flags) :
			DX12HotPathState::NeedsComputeResidentDescriptorState(flags);
		bool changed = true;
		if (needsResidentTable) {
			if (runtimeState) {
				changed = DX12CommandListRuntime::RememberRootDescriptorTable(
					runtimeState, graphics, rootParameterIndex, baseDescriptor);
			} else {
				changed = DX12CommandListRuntime::RememberRootDescriptorTable(
					commandList, graphics, rootParameterIndex, baseDescriptor);
			}
		}
		const bool skipTrackedCapture = graphics ?
			!DX12HotPathState::NeedsGraphicsTrackedBindingCapture(flags) :
			DX12HotPathState::skipBindings.load(std::memory_order_relaxed);
		if (skipTrackedCapture) {
			DX12_PROF_COUNT("FastForward");
			DX12_PROF_COUNT("Rm.DescriptorRecordFastSkips");
			return changed;
		}

				if (graphics) {
			DX12CommandListStateCapture::GraphicsRootDescriptorTable(
				commandList, runtimeState, rootParameterIndex, baseDescriptor);
		} else {
			DX12CommandListStateCapture::ComputeRootDescriptorTable(
				commandList, runtimeState, rootParameterIndex, baseDescriptor);
		}
		return true;
	}

	static void CaptureRoot32BitConstant(
		ID3D12GraphicsCommandList *commandList,
		UINT rootParameterIndex,
		UINT srcData,
		UINT destOffset,
		bool graphics)
	{
		RestoreDeferredReplacementsBeforeBindingMutation(commandList);
		const UINT64 flags = DX12HotPathState::HookWorkFlags.load(std::memory_order_relaxed);
		const bool needsResidentBinding = graphics ?
			DX12HotPathState::NeedsGraphicsResidentRootValueState(flags) :
			DX12HotPathState::NeedsComputeResidentBindingState(flags);
		const bool skipBindingCapture = graphics ?
			!DX12HotPathState::NeedsGraphicsTrackedBindingCapture(flags) :
			DX12HotPathState::skipBindings.load(std::memory_order_relaxed);
		if (!needsResidentBinding && skipBindingCapture) {
			DX12_PROF_COUNT("FastForward");
			return;
		}
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12CommandListComputeBindingFlow::CaptureRoot32BitConstant(
			commandList, runtimeState, rootParameterIndex, srcData, destOffset,
			graphics, true);
	}

	static void CaptureRoot32BitConstant(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		UINT rootParameterIndex,
		UINT srcData,
		UINT destOffset,
		bool graphics,
		bool deferredRestoreDone = false)
	{
		if (!deferredRestoreDone)
			RestoreDeferredReplacementsBeforeBindingMutation(commandList);
		const UINT64 flags = DX12HotPathState::HookWorkFlags.load(std::memory_order_relaxed);
		const bool needsResidentBinding = graphics ?
			DX12HotPathState::NeedsGraphicsResidentRootValueState(flags) :
			DX12HotPathState::NeedsComputeResidentBindingState(flags);
		if (needsResidentBinding) {
			if (graphics) {
				if (runtimeState)
					DX12CommandListRuntime::RememberGraphicsRoot32BitConstants(
						runtimeState, rootParameterIndex, destOffset, 1, &srcData);
				else
					DX12CommandListRuntime::RememberGraphicsRoot32BitConstants(
						commandList, rootParameterIndex, destOffset, 1, &srcData);
			} else {
				if (runtimeState)
					DX12CommandListRuntime::RememberComputeRoot32BitConstants(
						runtimeState, rootParameterIndex, destOffset, 1, &srcData);
				else
					DX12CommandListRuntime::RememberComputeRoot32BitConstants(
						commandList, rootParameterIndex, destOffset, 1, &srcData);
			}
		}
		const bool skipBindingCapture = graphics ?
			!DX12HotPathState::NeedsGraphicsTrackedBindingCapture(flags) :
			DX12HotPathState::skipBindings.load(std::memory_order_relaxed);
		if (skipBindingCapture) {
			DX12_PROF_COUNT("FastForward");
			DX12_PROF_COUNT("Rm.DescriptorRecordFastSkips");
			return;
		}

				if (graphics)
			DX12CommandListStateCapture::GraphicsRoot32BitConstant(
				commandList, runtimeState, rootParameterIndex, srcData, destOffset);
		else
			DX12CommandListStateCapture::ComputeRoot32BitConstant(
				commandList, runtimeState, rootParameterIndex, srcData, destOffset);
	}

	static void CaptureRoot32BitConstants(
		ID3D12GraphicsCommandList *commandList,
		UINT rootParameterIndex,
		UINT num32BitValuesToSet,
		const void *srcData,
		UINT destOffset,
		bool graphics)
	{
		RestoreDeferredReplacementsBeforeBindingMutation(commandList);
		const UINT64 flags = DX12HotPathState::HookWorkFlags.load(std::memory_order_relaxed);
		const bool needsResidentBinding = graphics ?
			DX12HotPathState::NeedsGraphicsResidentRootValueState(flags) :
			DX12HotPathState::NeedsComputeResidentBindingState(flags);
		const bool skipBindingCapture = graphics ?
			!DX12HotPathState::NeedsGraphicsTrackedBindingCapture(flags) :
			DX12HotPathState::skipBindings.load(std::memory_order_relaxed);
		if (!needsResidentBinding && skipBindingCapture) {
			DX12_PROF_COUNT("FastForward");
			return;
		}
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12CommandListComputeBindingFlow::CaptureRoot32BitConstants(
			commandList, runtimeState, rootParameterIndex,
			num32BitValuesToSet, srcData, destOffset, graphics, true);
	}

	static void CaptureRoot32BitConstants(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		UINT rootParameterIndex,
		UINT num32BitValuesToSet,
		const void *srcData,
		UINT destOffset,
		bool graphics,
		bool deferredRestoreDone = false)
	{
		if (!deferredRestoreDone)
			RestoreDeferredReplacementsBeforeBindingMutation(commandList);
		const UINT64 flags = DX12HotPathState::HookWorkFlags.load(std::memory_order_relaxed);
		const bool needsResidentBinding = graphics ?
			DX12HotPathState::NeedsGraphicsResidentRootValueState(flags) :
			DX12HotPathState::NeedsComputeResidentBindingState(flags);
		if (needsResidentBinding) {
			if (graphics) {
				if (runtimeState)
					DX12CommandListRuntime::RememberGraphicsRoot32BitConstants(
						runtimeState, rootParameterIndex, destOffset,
						num32BitValuesToSet, srcData);
				else
					DX12CommandListRuntime::RememberGraphicsRoot32BitConstants(
						commandList, rootParameterIndex, destOffset,
						num32BitValuesToSet, srcData);
			} else {
				if (runtimeState)
					DX12CommandListRuntime::RememberComputeRoot32BitConstants(
						runtimeState, rootParameterIndex, destOffset,
						num32BitValuesToSet, srcData);
				else
					DX12CommandListRuntime::RememberComputeRoot32BitConstants(
						commandList, rootParameterIndex, destOffset,
						num32BitValuesToSet, srcData);
			}
		}
		const bool skipBindingCapture = graphics ?
			!DX12HotPathState::NeedsGraphicsTrackedBindingCapture(flags) :
			DX12HotPathState::skipBindings.load(std::memory_order_relaxed);
		if (skipBindingCapture) {
			DX12_PROF_COUNT("FastForward");
			DX12_PROF_COUNT("Rm.DescriptorRecordFastSkips");
			return;
		}

				if (graphics)
			DX12CommandListStateCapture::GraphicsRoot32BitConstants(
				commandList, runtimeState, rootParameterIndex, num32BitValuesToSet, srcData, destOffset);
		else
			DX12CommandListStateCapture::ComputeRoot32BitConstants(
				commandList, runtimeState, rootParameterIndex, num32BitValuesToSet, srcData, destOffset);
	}

	static void CaptureRootDescriptor(
		ID3D12GraphicsCommandList *commandList,
		UINT rootParameterIndex,
		D3D12_ROOT_PARAMETER_TYPE type,
		D3D12_GPU_VIRTUAL_ADDRESS address,
		bool graphics,
		const char *api)
	{
		RestoreDeferredReplacementsBeforeBindingMutation(commandList);
		const UINT64 flags = DX12HotPathState::HookWorkFlags.load(std::memory_order_relaxed);
		const bool needsResidentBinding = graphics ?
			DX12HotPathState::NeedsGraphicsResidentRootDescriptorState(flags) :
			DX12HotPathState::NeedsComputeResidentBindingState(flags);
		const bool skipBindingCapture = graphics ?
			!DX12HotPathState::NeedsGraphicsTrackedBindingCapture(flags) :
			DX12HotPathState::skipBindings.load(std::memory_order_relaxed);
		if (!needsResidentBinding && skipBindingCapture) {
			DX12_PROF_COUNT("FastForward");
			return;
		}
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12CommandListComputeBindingFlow::CaptureRootDescriptor(
			commandList, runtimeState, rootParameterIndex, type, address,
			graphics, api, true);
	}

	static void CaptureRootDescriptor(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		UINT rootParameterIndex,
		D3D12_ROOT_PARAMETER_TYPE type,
		D3D12_GPU_VIRTUAL_ADDRESS address,
		bool graphics,
		const char *api,
		bool deferredRestoreDone = false)
	{
		if (!deferredRestoreDone)
			RestoreDeferredReplacementsBeforeBindingMutation(commandList);
		const UINT64 flags = DX12HotPathState::HookWorkFlags.load(std::memory_order_relaxed);
		const bool needsResidentBinding = graphics ?
			DX12HotPathState::NeedsGraphicsResidentRootDescriptorState(flags) :
			DX12HotPathState::NeedsComputeResidentBindingState(flags);
		if (needsResidentBinding) {
			if (graphics) {
				if (runtimeState)
					DX12CommandListRuntime::RememberGraphicsRootDescriptor(
						runtimeState, rootParameterIndex, type, address);
				else
					DX12CommandListRuntime::RememberGraphicsRootDescriptor(
						commandList, rootParameterIndex, type, address);
			} else {
				if (runtimeState)
					DX12CommandListRuntime::RememberComputeRootDescriptor(
						runtimeState, rootParameterIndex, type, address);
				else
					DX12CommandListRuntime::RememberComputeRootDescriptor(
						commandList, rootParameterIndex, type, address);
			}
		}
		const bool skipBindingCapture = graphics ?
			!DX12HotPathState::NeedsGraphicsTrackedBindingCapture(flags) :
			DX12HotPathState::skipBindings.load(std::memory_order_relaxed);
		if (skipBindingCapture) {
			DX12_PROF_COUNT("FastForward");
			DX12_PROF_COUNT("Rm.DescriptorRecordFastSkips");
			return;
		}

				if (graphics)
			DX12CommandListStateCapture::GraphicsRootDescriptor(
				commandList, runtimeState, rootParameterIndex, type, address);
		else
			DX12CommandListStateCapture::ComputeRootDescriptor(
				commandList, runtimeState, rootParameterIndex, type, address);
	}
};


class DX12CommandListLifecycle
{
public:
	static void Register(ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *initialState);
	static void Unregister(ID3D12GraphicsCommandList *commandList);
	static void Reset(ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *initialState);
	static void ClearState(ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState);
	static void InvalidateAfterExecuteBundle(ID3D12GraphicsCommandList *commandList);
};


class DX12CommandListCommandRecorder
{
public:
	static void RecordResourceBarrier(UINT numBarriers, const D3D12_RESOURCE_BARRIER *barriers);
	static void RecordExecuteBundle(ID3D12GraphicsCommandList *bundle, ID3D12PipelineState *initialState);
};
