#pragma once

#include "Commands/DX12CommandRuntime.hpp"
#include "Diagnostics/DX12Profiling.hpp"

class DX12CommandListComputeBindingFlow
{
public:
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
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12CommandListComputeBindingFlow::CaptureDescriptorHeaps(
			commandList, runtimeState, count, heaps);
	}

	static void CaptureDescriptorHeaps(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeState *runtimeState,
		UINT count,
		ID3D12DescriptorHeap *const *heaps)
	{
		if (DX12InternalReplayState::IsActive())
			return;
		const UINT64 flags = DX12HotPathState::HookWorkFlags;
		const bool skipComputeBinding =
			DX12HotPathState::skipAll &&
			!(flags &
				(DX12HotPathState::FLAG_PRE_SKIN_PROBE |
				 DX12HotPathState::FLAG_PRE_SKIN_BINDING_TRACK));
		// Probe-only PreSkin keeps skipAll set, but it still needs the resident
		// descriptor heaps for compute-side matching.
		if (skipComputeBinding) {
			DX12_PROFILE_FAST_FORWARD();
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
		}
		if (DX12HotPathState::skipBindings) {
			DX12_PROFILE_FAST_FORWARD();
			return;
		}

		DX12CommandListFlowDiagnostics::LogDX12Call(
			"ID3D12GraphicsCommandList::SetDescriptorHeaps", commandList,
			" count=%u", count);
		DX12CommandListStateCapture::DescriptorHeaps(
			commandList, runtimeState, cbvSrvUav, sampler);
	}

	static void UpdateComputeBindingHash(
		DX12CommandListRuntimeState *runtimeState,
		uint64_t hashComponent)
	{
		if (!runtimeState || !hashComponent)
			return;
		runtimeState->computeBindingHash ^= hashComponent;
		runtimeState->computeBindingHash ^= runtimeState->computeBindingHash >> 33;
		runtimeState->computeBindingHash *= 0xff51afd7ed558ccdull;
	}

	static void UpdateComputeBindingHash(
		ID3D12GraphicsCommandList *commandList,
		uint64_t hashComponent)
	{
		if (!hashComponent)
			return;
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12CommandListComputeBindingFlow::UpdateComputeBindingHash(runtimeState, hashComponent);
	}

	static void CaptureRootSignature(
		ID3D12GraphicsCommandList *commandList,
		ID3D12RootSignature *rootSignature,
		bool graphics)
	{
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12CommandListComputeBindingFlow::CaptureRootSignature(
			commandList, runtimeState, rootSignature, graphics);
	}

	static void CaptureRootSignature(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeState *runtimeState,
		ID3D12RootSignature *rootSignature,
		bool graphics)
	{
		const UINT64 flags = DX12HotPathState::HookWorkFlags;
		const bool needsResidentBinding = graphics ?
			DX12HotPathState::NeedsGraphicsResidentDescriptorState(flags) :
			DX12HotPathState::NeedsComputeResidentBindingState(flags);
		if (needsResidentBinding) {
			if (graphics) {
				if (runtimeState)
					DX12CommandListRuntime::RememberGraphicsRootSignature(
						runtimeState, rootSignature);
				else
					DX12CommandListRuntime::RememberGraphicsRootSignature(
						commandList, rootSignature);
			} else {
				if (runtimeState)
					DX12CommandListRuntime::RememberComputeRootSignature(
						runtimeState, rootSignature);
				else
					DX12CommandListRuntime::RememberComputeRootSignature(
						commandList, rootSignature);
			}
		}
		if (!graphics) {
			DX12CommandListComputeBindingFlow::UpdateComputeBindingHash(
				runtimeState, reinterpret_cast<uint64_t>(rootSignature));
		}
		const bool skipTrackedCapture = graphics ?
			!DX12HotPathState::NeedsGraphicsTrackedBindingCapture(flags) :
			DX12HotPathState::skipBindings;
		if (skipTrackedCapture) {
			DX12_PROFILE_FAST_FORWARD();
			return;
		}

		DX12CommandListFlowDiagnostics::LogDX12Call(
			graphics ? "ID3D12GraphicsCommandList::SetGraphicsRootSignature" :
				"ID3D12GraphicsCommandList::SetComputeRootSignature",
			commandList, " rootSignature=%p", rootSignature);
		if (graphics) {
			DX12CommandListStateCapture::GraphicsRootSignature(
				commandList, runtimeState, rootSignature);
		} else {
			DX12CommandListStateCapture::ComputeRootSignature(
				commandList, runtimeState, rootSignature);
		}
	}

	static void CaptureRootDescriptorTable(
		ID3D12GraphicsCommandList *commandList,
		UINT rootParameterIndex,
		D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor,
		bool graphics)
	{
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12CommandListComputeBindingFlow::CaptureRootDescriptorTable(
			commandList, runtimeState, rootParameterIndex, baseDescriptor, graphics);
	}

	static void CaptureRootDescriptorTable(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeState *runtimeState,
		UINT rootParameterIndex,
		D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor,
		bool graphics)
	{
		if (DX12InternalReplayState::IsActive())
			return;
		const UINT64 flags = DX12HotPathState::HookWorkFlags;
		const bool needsResidentTable = graphics ?
			DX12HotPathState::NeedsGraphicsResidentDescriptorState(flags) :
			DX12HotPathState::NeedsComputeResidentDescriptorState(flags);
		if (needsResidentTable) {
			if (runtimeState) {
				DX12CommandListRuntime::RememberRootDescriptorTable(
					runtimeState, graphics, rootParameterIndex, baseDescriptor);
			} else {
				DX12CommandListRuntime::RememberRootDescriptorTable(
					commandList, graphics, rootParameterIndex, baseDescriptor);
			}
		}
		if (!graphics) {
			DX12CommandListComputeBindingFlow::UpdateComputeBindingHash(
				runtimeState,
				static_cast<uint64_t>(rootParameterIndex) ^ 0x9e3779b97f4a7c15ull);
		}
		const bool skipTrackedCapture = graphics ?
			!DX12HotPathState::NeedsGraphicsTrackedBindingCapture(flags) :
			DX12HotPathState::skipBindings;
		if (skipTrackedCapture) {
			DX12_PROFILE_FAST_FORWARD();
			return;
		}

		DX12CommandListFlowDiagnostics::LogDX12Call(
			graphics ? "ID3D12GraphicsCommandList::SetGraphicsRootDescriptorTable" :
				"ID3D12GraphicsCommandList::SetComputeRootDescriptorTable",
			commandList, " root=%u gpu=0x%llx",
			rootParameterIndex, static_cast<unsigned long long>(baseDescriptor.ptr));
		if (graphics) {
			DX12CommandListStateCapture::GraphicsRootDescriptorTable(
				commandList, runtimeState, rootParameterIndex, baseDescriptor);
		} else {
			DX12CommandListStateCapture::ComputeRootDescriptorTable(
				commandList, runtimeState, rootParameterIndex, baseDescriptor);
		}
	}

	static void CaptureRoot32BitConstant(
		ID3D12GraphicsCommandList *commandList,
		UINT rootParameterIndex,
		UINT srcData,
		UINT destOffset,
		bool graphics)
	{
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12CommandListComputeBindingFlow::CaptureRoot32BitConstant(
			commandList, runtimeState, rootParameterIndex, srcData, destOffset, graphics);
	}

	static void CaptureRoot32BitConstant(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeState *runtimeState,
		UINT rootParameterIndex,
		UINT srcData,
		UINT destOffset,
		bool graphics)
	{
		const UINT64 flags = DX12HotPathState::HookWorkFlags;
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
			DX12HotPathState::skipBindings;
		if (skipBindingCapture) {
			DX12_PROFILE_FAST_FORWARD();
			return;
		}

		DX12CommandListFlowDiagnostics::LogDX12Call(
			graphics ? "ID3D12GraphicsCommandList::SetGraphicsRoot32BitConstant" :
				"ID3D12GraphicsCommandList::SetComputeRoot32BitConstant",
			commandList, " root=%u destOffset=%u", rootParameterIndex, destOffset);
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
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12CommandListComputeBindingFlow::CaptureRoot32BitConstants(
			commandList, runtimeState, rootParameterIndex,
			num32BitValuesToSet, srcData, destOffset, graphics);
	}

	static void CaptureRoot32BitConstants(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeState *runtimeState,
		UINT rootParameterIndex,
		UINT num32BitValuesToSet,
		const void *srcData,
		UINT destOffset,
		bool graphics)
	{
		const UINT64 flags = DX12HotPathState::HookWorkFlags;
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
			DX12HotPathState::skipBindings;
		if (skipBindingCapture) {
			DX12_PROFILE_FAST_FORWARD();
			return;
		}

		DX12CommandListFlowDiagnostics::LogDX12Call(
			graphics ? "ID3D12GraphicsCommandList::SetGraphicsRoot32BitConstants" :
				"ID3D12GraphicsCommandList::SetComputeRoot32BitConstants",
			commandList, " root=%u values=%u destOffset=%u",
			rootParameterIndex, num32BitValuesToSet, destOffset);
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
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12CommandListComputeBindingFlow::CaptureRootDescriptor(
			commandList, runtimeState, rootParameterIndex, type, address, graphics, api);
	}

	static void CaptureRootDescriptor(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeState *runtimeState,
		UINT rootParameterIndex,
		D3D12_ROOT_PARAMETER_TYPE type,
		D3D12_GPU_VIRTUAL_ADDRESS address,
		bool graphics,
		const char *api)
	{
		const UINT64 flags = DX12HotPathState::HookWorkFlags;
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
			DX12HotPathState::skipBindings;
		if (skipBindingCapture) {
			DX12_PROFILE_FAST_FORWARD();
			return;
		}

		DX12CommandListFlowDiagnostics::LogDX12Call(
			api, commandList, " root=%u gpu=0x%llx",
			rootParameterIndex, static_cast<unsigned long long>(address));
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
	static void RecordExecuteIndirect(ID3D12GraphicsCommandList *commandList);
};



enum DX12HuntListCategory
{
	DX12_HUNT_LIST_VS,
	DX12_HUNT_LIST_PS,
	DX12_HUNT_LIST_CS,
	DX12_HUNT_LIST_IB,
	DX12_HUNT_LIST_VB,
};

struct DX12HuntListItem
{
	wchar_t label[64];
	UINT64 hash;
	bool selected;
};

void DX12HuntResetCommandList(ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState);
void DX12HuntUnregisterCommandList(ID3D12GraphicsCommandList *commandList);
void DX12HuntObservePipelineState(ID3D12PipelineState *pipelineState);
void DX12HuntRecordDraw(
	ID3D12GraphicsCommandList *commandList, bool indexed,
	UINT vertexCount, UINT indexCount, UINT firstVertex, UINT firstIndex,
	INT baseVertex);
void DX12HuntPublishCommandList(ID3D12GraphicsCommandList *commandList);
void DX12HuntRecordDispatch(ID3D12GraphicsCommandList *commandList);
void DX12HuntRecordExecuteIndirect(ID3D12GraphicsCommandList *commandList);
bool DX12HuntShouldSkipDraw(
	ID3D12GraphicsCommandList *commandList, bool indexed,
	UINT vertexCount, UINT indexCount, UINT firstVertex, UINT firstIndex,
	INT baseVertex);
bool DX12HuntShouldSkipDispatch(ID3D12GraphicsCommandList *commandList);
bool DX12HuntIsEnabled();
bool DX12HuntNeedsHotPath();
bool DX12HuntShouldDrawOverlay();
void DX12HuntToggle();
void DX12HuntOnPresent();
void DX12HuntPreviousVS();
void DX12HuntNextVS();
void DX12HuntPreviousPS();
void DX12HuntNextPS();
void DX12HuntPreviousCS();
void DX12HuntNextCS();
void DX12HuntPreviousIB();
void DX12HuntNextIB();
void DX12HuntPreviousVB();
void DX12HuntNextVB();
void DX12HuntCopySelectedHash();
void DX12HuntResetSelection();
void DX12HuntRefreshOverlay();
size_t DX12HuntGetListItems(
	DX12HuntListCategory category, DX12HuntListItem *items, size_t itemCount);
void DX12HuntSelectListItem(DX12HuntListCategory category, size_t index);
void DX12HuntSelectListHash(DX12HuntListCategory category, UINT64 hash);



class DX12ResidentMaintenanceTelemetryUtils
{
public:
	inline static volatile LONG PsoCalls = 0;
	inline static volatile LONG PsoChanged = 0;
	inline static volatile LONG PsoNoChange = 0;
	inline static volatile LONG RuntimePsoCalls = 0;
	inline static volatile LONG RuntimePsoChanged = 0;
	inline static volatile LONG RuntimePsoNoChange = 0;
	inline static volatile LONG RuntimeLookupTlsHits = 0;
	inline static volatile LONG RuntimeLookupDirectHits = 0;
	inline static volatile LONG RuntimeLookupShardHits = 0;
	inline static volatile LONG RuntimeLookupMisses = 0;

	inline static volatile LONG GraphicsRootSignatureCalls = 0;
	inline static volatile LONG GraphicsRootSignatureChanged = 0;
	inline static volatile LONG GraphicsRootSignatureNoChange = 0;
	inline static volatile LONG ComputeRootSignatureCalls = 0;
	inline static volatile LONG ComputeRootSignatureChanged = 0;
	inline static volatile LONG ComputeRootSignatureNoChange = 0;

	inline static volatile LONG DescriptorHeapResolveCalls = 0;
	inline static volatile LONG64 DescriptorHeapResolveSlots = 0;
	inline static volatile LONG DescriptorHeapCalls = 0;
	inline static volatile LONG DescriptorHeapChanged = 0;
	inline static volatile LONG DescriptorHeapNoChange = 0;
	inline static volatile LONG64 DescriptorHeapInvalidatedGraphicsTables = 0;
	inline static volatile LONG64 DescriptorHeapInvalidatedComputeTables = 0;

	inline static volatile LONG GraphicsRootTableCalls = 0;
	inline static volatile LONG GraphicsRootTableTlsHits = 0;
	inline static volatile LONG GraphicsRootTableChanged = 0;
	inline static volatile LONG GraphicsRootTableNoChange = 0;
	inline static volatile LONG ComputeRootTableCalls = 0;
	inline static volatile LONG ComputeRootTableTlsHits = 0;
	inline static volatile LONG ComputeRootTableChanged = 0;
	inline static volatile LONG ComputeRootTableNoChange = 0;

	inline static volatile LONG GraphicsRootDescriptorCalls = 0;
	inline static volatile LONG GraphicsRootDescriptorTlsHits = 0;
	inline static volatile LONG GraphicsRootDescriptorChanged = 0;
	inline static volatile LONG GraphicsRootDescriptorNoChange = 0;
	inline static volatile LONG ComputeRootDescriptorCalls = 0;
	inline static volatile LONG ComputeRootDescriptorTlsHits = 0;
	inline static volatile LONG ComputeRootDescriptorChanged = 0;
	inline static volatile LONG ComputeRootDescriptorNoChange = 0;

	inline static volatile LONG GraphicsRootConstantsCalls = 0;
	inline static volatile LONG64 GraphicsRootConstantsValues = 0;
	inline static volatile LONG ComputeRootConstantsCalls = 0;
	inline static volatile LONG64 ComputeRootConstantsValues = 0;

	inline static volatile LONG PrimitiveTopologyCalls = 0;
	inline static volatile LONG PrimitiveTopologyChanged = 0;
	inline static volatile LONG PrimitiveTopologyNoChange = 0;
	inline static volatile LONG RuntimePrimitiveTopologyCalls = 0;
	inline static volatile LONG RuntimePrimitiveTopologyChanged = 0;
	inline static volatile LONG RuntimePrimitiveTopologyNoChange = 0;

	inline static volatile LONG IndexBufferCalls = 0;
	inline static volatile LONG IndexBufferChanged = 0;
	inline static volatile LONG IndexBufferNoChange = 0;
	inline static volatile LONG IndexBufferResolvedReleased = 0;
	inline static volatile LONG RuntimeIndexBufferCalls = 0;
	inline static volatile LONG RuntimeIndexBufferChanged = 0;
	inline static volatile LONG RuntimeIndexBufferNoChange = 0;

	inline static volatile LONG VertexBufferCalls = 0;
	inline static volatile LONG64 VertexBufferSlots = 0;
	inline static volatile LONG64 VertexBufferChangedSlots = 0;
	inline static volatile LONG64 VertexBufferNoChangeSlots = 0;
	inline static volatile LONG VertexBufferResolvedReleased = 0;
	inline static volatile LONG RuntimeVertexBufferCalls = 0;
	inline static volatile LONG64 RuntimeVertexBufferSlots = 0;
	inline static volatile LONG64 RuntimeVertexBufferChangedSlots = 0;
	inline static volatile LONG64 RuntimeVertexBufferNoChangeSlots = 0;

	inline static volatile LONG BarrierCaptureCalls = 0;
	inline static volatile LONG64 BarrierCaptureItems = 0;
	inline static volatile LONG BarrierCaptureSkippedInternalReplay = 0;
	inline static volatile LONG BarrierCaptureSkippedHotPath = 0;
	inline static volatile LONG BarrierCaptureSkippedStatePolicy = 0;
	inline static volatile LONG BarrierRecordedCalls = 0;
	inline static volatile LONG64 BarrierRecordedItems = 0;
	inline static volatile LONG BarrierUnwrapCalls = 0;
	inline static volatile LONG64 BarrierUnwrapItems = 0;

	inline static volatile LONG PreSkinBindingCacheCalls = 0;
	inline static volatile LONG PreSkinBindingCacheHits = 0;
	inline static volatile LONG PreSkinBindingCacheMisses = 0;
	inline static volatile LONG PreSkinBindingCacheStores = 0;
	inline static volatile LONG64 PreSkinBindingRootTables = 0;
	inline static volatile LONG64 PreSkinBindingRanges = 0;
	inline static volatile LONG64 PreSkinBindingLightDescriptorLookups = 0;
	inline static volatile LONG64 PreSkinBindingFullDescriptorLookups = 0;
	inline static volatile LONG64 PreSkinBindingConstants = 0;
	inline static volatile LONG64 PreSkinBindingRootDescriptors = 0;
	inline static volatile LONG64 PreSkinBindingRuntimeResolved = 0;
	inline static volatile LONG64 PreSkinBindingRuntimeUnresolved = 0;
	inline static volatile LONG64 PreSkinBindingDescriptorHits = 0;
	inline static volatile LONG64 PreSkinBindingDescriptorMisses = 0;
	inline static volatile LONG64 PreSkinBindingDescriptorPending = 0;
	inline static volatile LONG64 PreSkinBindingWaitAvoided = 0;
	inline static volatile LONG64 RuntimeDescriptorWrites = 0;
	inline static volatile LONG64 RuntimeDescriptorCopyCalls = 0;
	inline static volatile LONG64 RuntimeDescriptorCopied = 0;
	inline static volatile LONG64 RuntimeDescriptorCopyMisses = 0;

	inline static volatile LONG DispatchDecisionCacheHits = 0;
	inline static volatile LONG DispatchDecisionCacheMisses = 0;
	inline static volatile LONG DispatchDecisionPsoCacheHits = 0;
	inline static volatile LONG DispatchDecisionPsoCacheMisses = 0;
	inline static volatile LONG DispatchDecisionBuilds = 0;
	inline static volatile LONG DispatchDecisionNoCompute = 0;
	inline static volatile LONG DispatchDecisionNoPreSkinCandidate = 0;
	inline static volatile LONG DispatchDecisionPreSkinCandidate = 0;
	inline static volatile LONG DispatchDecisionPreSkinCandidateDispatches = 0;
	inline static volatile LONG DispatchDecisionShaderOverrideWork = 0;
	inline static volatile LONG DispatchDecisionFastRejects = 0;

	inline static volatile LONG PreSkinPlanCacheLookups = 0;
	inline static volatile LONG PreSkinPlanCacheHits = 0;
	inline static volatile LONG PreSkinPlanCacheNegativeHits = 0;
	inline static volatile LONG PreSkinPlanCachePositiveHits = 0;
	inline static volatile LONG PreSkinPlanCacheMisses = 0;
	inline static volatile LONG PreSkinPlanCacheStores = 0;
	inline static volatile LONG PreSkinPlanFingerprintBuilds = 0;

	inline static volatile LONG DrawDecisionCacheHits = 0;
	inline static volatile LONG DrawDecisionCacheMisses = 0;
	inline static volatile LONG DrawDecisionPsoCacheHits = 0;
	inline static volatile LONG DrawDecisionPsoCacheMisses = 0;
	inline static volatile LONG DrawDecisionBuilds = 0;
	inline static volatile LONG DrawDecisionNoPso = 0;
	inline static volatile LONG DrawDecisionNoShaderOverride = 0;
	inline static volatile LONG DrawDecisionNoDrawOverride = 0;
	inline static volatile LONG DrawDecisionNeedsIaHash = 0;
	inline static volatile LONG DrawDecisionNeedsDescriptorMatch = 0;
	inline static volatile LONG DrawDecisionNeedsReplacement = 0;
	inline static volatile LONG DrawDecisionSkipOriginal = 0;
	inline static volatile LONG DrawDecisionFastRejects = 0;
	inline static volatile LONG DrawDecisionIaHashDraws = 0;
	inline static volatile LONG DrawDecisionIaHashCacheLookups = 0;
	inline static volatile LONG DrawDecisionIaHashCacheHits = 0;
	inline static volatile LONG DrawDecisionIaHashCacheMisses = 0;
	inline static volatile LONG DrawDecisionIaHashCacheStores = 0;
	inline static volatile LONG DrawDecisionIaHashCacheEvictions = 0;
	inline static volatile LONG DrawDecisionIaHashCacheNegativeHits = 0;
	inline static volatile LONG DrawDecisionDescriptorDraws = 0;
	inline static volatile LONG DrawDecisionDirectProbeCacheLookups = 0;
	inline static volatile LONG DrawDecisionDirectProbeCacheHits = 0;
	inline static volatile LONG DrawDecisionDirectProbeCacheMisses = 0;
	inline static volatile LONG DrawDecisionDirectProbeCachePositiveHits = 0;
	inline static volatile LONG DrawDecisionDirectProbeCacheNegativeHits = 0;
	inline static volatile LONG DrawDecisionDirectProbeCacheStores = 0;
	inline static volatile LONG DrawDecisionDirectProbeCacheEvictions = 0;

	static void Inc(volatile LONG *value)
	{
	#if defined(DX12_ENABLE_DETAILED_TIMING)
		InterlockedIncrement(value);
	#else
		(void)value;
	#endif
	}

	static void Add64(volatile LONG64 *value, LONG64 amount)
	{
	#if defined(DX12_ENABLE_DETAILED_TIMING)
		if (amount)
			InterlockedExchangeAdd64(value, amount);
	#else
		(void)value;
		(void)amount;
	#endif
	}

	static LONG Take(volatile LONG *value)
	{
		return InterlockedExchange(value, 0);
	}

	static LONG64 Take64(volatile LONG64 *value)
	{
		return InterlockedExchange64(value, 0);
	}

	static void RecordPsoCall()
	{
		Inc(&PsoCalls);
	}

	static void RecordPsoChanged()
	{
		Inc(&PsoChanged);
	}

	static void RecordPsoNoChange()
	{
		Inc(&PsoNoChange);
	}

	static void RecordRuntimePso(bool changed)
	{
		Inc(&RuntimePsoCalls);
		if (changed)
			Inc(&RuntimePsoChanged);
		else
			Inc(&RuntimePsoNoChange);
	}

	static void RecordRuntimeLookupTlsHit()
	{
		Inc(&RuntimeLookupTlsHits);
	}

	static void RecordRuntimeLookupDirectHit()
	{
		Inc(&RuntimeLookupDirectHits);
	}

	static void RecordRuntimeLookupShardHit()
	{
		Inc(&RuntimeLookupShardHits);
	}

	static void RecordRuntimeLookupMiss()
	{
		Inc(&RuntimeLookupMisses);
	}

	static void RecordGraphicsRootSignature(bool changed)
	{
		Inc(&GraphicsRootSignatureCalls);
		if (changed)
			Inc(&GraphicsRootSignatureChanged);
		else
			Inc(&GraphicsRootSignatureNoChange);
	}

	static void RecordComputeRootSignature(bool changed)
	{
		Inc(&ComputeRootSignatureCalls);
		if (changed)
			Inc(&ComputeRootSignatureChanged);
		else
			Inc(&ComputeRootSignatureNoChange);
	}

	static void RecordDescriptorHeapResolve(UINT count)
	{
		Inc(&DescriptorHeapResolveCalls);
		Add64(&DescriptorHeapResolveSlots, static_cast<LONG64>(count));
	}

	static void RecordDescriptorHeapsChanged(
		UINT graphicsInvalidated,
		UINT computeInvalidated)
	{
		Inc(&DescriptorHeapCalls);
		Inc(&DescriptorHeapChanged);
		Add64(&DescriptorHeapInvalidatedGraphicsTables, static_cast<LONG64>(graphicsInvalidated));
		Add64(&DescriptorHeapInvalidatedComputeTables, static_cast<LONG64>(computeInvalidated));
	}

	static void RecordDescriptorHeapsNoChange()
	{
		Inc(&DescriptorHeapCalls);
		Inc(&DescriptorHeapNoChange);
	}

	static void RecordGraphicsRootTableCall()
	{
		Inc(&GraphicsRootTableCalls);
	}

	static void RecordGraphicsRootTableTlsHit()
	{
		Inc(&GraphicsRootTableTlsHits);
	}

	static void RecordGraphicsRootTableChanged()
	{
		Inc(&GraphicsRootTableChanged);
	}

	static void RecordGraphicsRootTableNoChange()
	{
		Inc(&GraphicsRootTableNoChange);
	}

	static void RecordComputeRootTableCall()
	{
		Inc(&ComputeRootTableCalls);
	}

	static void RecordComputeRootTableTlsHit()
	{
		Inc(&ComputeRootTableTlsHits);
	}

	static void RecordComputeRootTableChanged()
	{
		Inc(&ComputeRootTableChanged);
	}

	static void RecordComputeRootTableNoChange()
	{
		Inc(&ComputeRootTableNoChange);
	}

	static void RecordGraphicsRootDescriptorCall()
	{
		Inc(&GraphicsRootDescriptorCalls);
	}

	static void RecordGraphicsRootDescriptorTlsHit()
	{
		Inc(&GraphicsRootDescriptorTlsHits);
	}

	static void RecordGraphicsRootDescriptorChanged()
	{
		Inc(&GraphicsRootDescriptorChanged);
	}

	static void RecordGraphicsRootDescriptorNoChange()
	{
		Inc(&GraphicsRootDescriptorNoChange);
	}

	static void RecordComputeRootDescriptorCall()
	{
		Inc(&ComputeRootDescriptorCalls);
	}

	static void RecordComputeRootDescriptorTlsHit()
	{
		Inc(&ComputeRootDescriptorTlsHits);
	}

	static void RecordComputeRootDescriptorChanged()
	{
		Inc(&ComputeRootDescriptorChanged);
	}

	static void RecordComputeRootDescriptorNoChange()
	{
		Inc(&ComputeRootDescriptorNoChange);
	}

	static void RecordGraphicsRootConstants(UINT count)
	{
		Inc(&GraphicsRootConstantsCalls);
		Add64(&GraphicsRootConstantsValues, static_cast<LONG64>(count));
	}

	static void RecordComputeRootConstants(UINT count)
	{
		Inc(&ComputeRootConstantsCalls);
		Add64(&ComputeRootConstantsValues, static_cast<LONG64>(count));
	}

	static void RecordPrimitiveTopology(bool changed)
	{
		Inc(&PrimitiveTopologyCalls);
		if (changed)
			Inc(&PrimitiveTopologyChanged);
		else
			Inc(&PrimitiveTopologyNoChange);
	}

	static void RecordRuntimePrimitiveTopology(bool changed)
	{
		Inc(&RuntimePrimitiveTopologyCalls);
		if (changed)
			Inc(&RuntimePrimitiveTopologyChanged);
		else
			Inc(&RuntimePrimitiveTopologyNoChange);
	}

	static void RecordIndexBuffer(bool changed)
	{
		Inc(&IndexBufferCalls);
		if (changed)
			Inc(&IndexBufferChanged);
		else
			Inc(&IndexBufferNoChange);
	}

	static void RecordIndexBufferResolvedReleased()
	{
		Inc(&IndexBufferResolvedReleased);
	}

	static void RecordRuntimeIndexBuffer(bool changed)
	{
		Inc(&RuntimeIndexBufferCalls);
		if (changed)
			Inc(&RuntimeIndexBufferChanged);
		else
			Inc(&RuntimeIndexBufferNoChange);
	}

	static void RecordVertexBufferCall(UINT slots)
	{
		Inc(&VertexBufferCalls);
		Add64(&VertexBufferSlots, static_cast<LONG64>(slots));
	}

	static void RecordVertexBufferSlot(bool changed)
	{
		if (changed)
			Add64(&VertexBufferChangedSlots, 1);
		else
			Add64(&VertexBufferNoChangeSlots, 1);
	}

	static void RecordVertexBufferResolvedReleased()
	{
		Inc(&VertexBufferResolvedReleased);
	}

	static void RecordRuntimeVertexBufferCall(UINT slots)
	{
		Inc(&RuntimeVertexBufferCalls);
		Add64(&RuntimeVertexBufferSlots, static_cast<LONG64>(slots));
	}

	static void RecordRuntimeVertexBufferSlot(bool changed)
	{
		if (changed)
			Add64(&RuntimeVertexBufferChangedSlots, 1);
		else
			Add64(&RuntimeVertexBufferNoChangeSlots, 1);
	}

	static void RecordBarrierCaptureCall(UINT count)
	{
		Inc(&BarrierCaptureCalls);
		Add64(&BarrierCaptureItems, static_cast<LONG64>(count));
	}

	static void RecordBarrierCaptureSkippedInternalReplay()
	{
		Inc(&BarrierCaptureSkippedInternalReplay);
	}

	static void RecordBarrierCaptureSkippedHotPath()
	{
		Inc(&BarrierCaptureSkippedHotPath);
	}

	static void RecordBarrierCaptureSkippedStatePolicy()
	{
		Inc(&BarrierCaptureSkippedStatePolicy);
	}

	static void RecordBarrierRecorded(UINT count)
	{
		Inc(&BarrierRecordedCalls);
		Add64(&BarrierRecordedItems, static_cast<LONG64>(count));
	}

	static void RecordBarrierUnwrap(UINT count)
	{
		Inc(&BarrierUnwrapCalls);
		Add64(&BarrierUnwrapItems, static_cast<LONG64>(count));
	}

	static void RecordPreSkinBindingCacheCall()
	{
		Inc(&PreSkinBindingCacheCalls);
	}

	static void RecordPreSkinBindingCacheHit()
	{
		Inc(&PreSkinBindingCacheHits);
	}

	static void RecordPreSkinBindingCacheMiss()
	{
		Inc(&PreSkinBindingCacheMisses);
	}

	static void RecordPreSkinBindingCacheStore()
	{
		Inc(&PreSkinBindingCacheStores);
	}

	static void RecordPreSkinBindingRootTable()
	{
		Add64(&PreSkinBindingRootTables, 1);
	}

	static void RecordPreSkinBindingRange()
	{
		Add64(&PreSkinBindingRanges, 1);
	}

	static void RecordPreSkinBindingLightDescriptorLookup()
	{
		Add64(&PreSkinBindingLightDescriptorLookups, 1);
	}

	static void RecordPreSkinBindingFullDescriptorLookup()
	{
		Add64(&PreSkinBindingFullDescriptorLookups, 1);
	}

	static void RecordPreSkinBindingConstant()
	{
		Add64(&PreSkinBindingConstants, 1);
	}

	static void RecordPreSkinBindingRootDescriptor()
	{
		Add64(&PreSkinBindingRootDescriptors, 1);
	}

	static void RecordPreSkinBindingRuntimeTable(bool resolved)
	{
		if (resolved)
			Add64(&PreSkinBindingRuntimeResolved, 1);
		else
			Add64(&PreSkinBindingRuntimeUnresolved, 1);
	}

	static void RecordPreSkinBindingDescriptorLookupResult(
		LONG64 hitCount,
		LONG64 missCount,
		LONG64 pendingCount)
	{
		Add64(&PreSkinBindingDescriptorHits, hitCount);
		Add64(&PreSkinBindingDescriptorMisses, missCount);
		Add64(&PreSkinBindingDescriptorPending, pendingCount);
		Add64(&PreSkinBindingWaitAvoided, hitCount + missCount + pendingCount);
	}

	static void RecordRuntimeDescriptorWrite()
	{
		Add64(&RuntimeDescriptorWrites, 1);
	}

	static void RecordRuntimeDescriptorCopy(UINT copied, UINT misses)
	{
		Add64(&RuntimeDescriptorCopyCalls, 1);
		Add64(&RuntimeDescriptorCopied, static_cast<LONG64>(copied));
		Add64(&RuntimeDescriptorCopyMisses, static_cast<LONG64>(misses));
	}

	static void RecordDispatchDecisionCacheHit()
	{
		Inc(&DispatchDecisionCacheHits);
	}

	static void RecordDispatchDecisionCacheMiss()
	{
		Inc(&DispatchDecisionCacheMisses);
	}

	static void RecordDispatchDecisionPsoCacheHit()
	{
		Inc(&DispatchDecisionPsoCacheHits);
	}

	static void RecordDispatchDecisionPsoCacheMiss()
	{
		Inc(&DispatchDecisionPsoCacheMisses);
	}

	static void RecordDispatchDecisionBuild()
	{
		Inc(&DispatchDecisionBuilds);
	}

	static void RecordDispatchDecisionNoCompute()
	{
		Inc(&DispatchDecisionNoCompute);
	}

	static void RecordDispatchDecisionNoPreSkinCandidate()
	{
		Inc(&DispatchDecisionNoPreSkinCandidate);
	}

	static void RecordDispatchDecisionPreSkinCandidate()
	{
		Inc(&DispatchDecisionPreSkinCandidate);
	}

	static void RecordDispatchDecisionPreSkinCandidateDispatch()
	{
		Inc(&DispatchDecisionPreSkinCandidateDispatches);
	}

	static void RecordDispatchDecisionShaderOverrideWork()
	{
		Inc(&DispatchDecisionShaderOverrideWork);
	}

	static void RecordDispatchDecisionFastReject()
	{
		Inc(&DispatchDecisionFastRejects);
	}

	static void RecordPreSkinPlanCacheLookup()
	{
		Inc(&PreSkinPlanCacheLookups);
	}

	static void RecordPreSkinPlanCacheHit(bool positive)
	{
		Inc(&PreSkinPlanCacheHits);
		if (positive)
			Inc(&PreSkinPlanCachePositiveHits);
		else
			Inc(&PreSkinPlanCacheNegativeHits);
	}

	static void RecordPreSkinPlanCacheMiss()
	{
		Inc(&PreSkinPlanCacheMisses);
	}

	static void RecordPreSkinPlanCacheStore()
	{
		Inc(&PreSkinPlanCacheStores);
	}

	static void RecordPreSkinPlanFingerprintBuild()
	{
		Inc(&PreSkinPlanFingerprintBuilds);
	}

	static void RecordDrawDecisionCacheHit()
	{
		Inc(&DrawDecisionCacheHits);
	}

	static void RecordDrawDecisionCacheMiss()
	{
		Inc(&DrawDecisionCacheMisses);
	}

	static void RecordDrawDecisionPsoCacheHit()
	{
		Inc(&DrawDecisionPsoCacheHits);
	}

	static void RecordDrawDecisionPsoCacheMiss()
	{
		Inc(&DrawDecisionPsoCacheMisses);
	}

	static void RecordDrawDecisionBuild()
	{
		Inc(&DrawDecisionBuilds);
	}

	static void RecordDrawDecisionNoPso()
	{
		Inc(&DrawDecisionNoPso);
	}

	static void RecordDrawDecisionNoShaderOverride()
	{
		Inc(&DrawDecisionNoShaderOverride);
	}

	static void RecordDrawDecisionNoDrawOverride()
	{
		Inc(&DrawDecisionNoDrawOverride);
	}

	static void RecordDrawDecisionNeedsIaHash()
	{
		Inc(&DrawDecisionNeedsIaHash);
	}

	static void RecordDrawDecisionNeedsDescriptorMatch()
	{
		Inc(&DrawDecisionNeedsDescriptorMatch);
	}

	static void RecordDrawDecisionNeedsReplacement()
	{
		Inc(&DrawDecisionNeedsReplacement);
	}

	static void RecordDrawDecisionSkipOriginal()
	{
		Inc(&DrawDecisionSkipOriginal);
	}

	static void RecordDrawDecisionFastReject()
	{
		Inc(&DrawDecisionFastRejects);
	}

	static void RecordDrawDecisionIaHashDraw()
	{
		Inc(&DrawDecisionIaHashDraws);
	}

	static void RecordDrawDecisionIaHashCacheLookup()
	{
		Inc(&DrawDecisionIaHashCacheLookups);
	}

	static void RecordDrawDecisionIaHashCacheHit(bool hasIaHashState)
	{
		Inc(&DrawDecisionIaHashCacheHits);
		if (!hasIaHashState)
			Inc(&DrawDecisionIaHashCacheNegativeHits);
	}

	static void RecordDrawDecisionIaHashCacheMiss()
	{
		Inc(&DrawDecisionIaHashCacheMisses);
	}

	static void RecordDrawDecisionIaHashCacheStore()
	{
		Inc(&DrawDecisionIaHashCacheStores);
	}

	static void RecordDrawDecisionIaHashCacheEviction()
	{
		Inc(&DrawDecisionIaHashCacheEvictions);
	}

	static void RecordDrawDecisionDescriptorDraw()
	{
		Inc(&DrawDecisionDescriptorDraws);
	}

	static void RecordDrawDecisionDirectProbeCacheLookup()
	{
		Inc(&DrawDecisionDirectProbeCacheLookups);
	}

	static void RecordDrawDecisionDirectProbeCacheHit(bool hasDirectCandidate)
	{
		Inc(&DrawDecisionDirectProbeCacheHits);
		if (hasDirectCandidate)
			Inc(&DrawDecisionDirectProbeCachePositiveHits);
		else
			Inc(&DrawDecisionDirectProbeCacheNegativeHits);
	}

	static void RecordDrawDecisionDirectProbeCacheMiss()
	{
		Inc(&DrawDecisionDirectProbeCacheMisses);
	}

	static void RecordDrawDecisionDirectProbeCacheStore(bool evicted)
	{
		Inc(&DrawDecisionDirectProbeCacheStores);
		if (evicted)
			Inc(&DrawDecisionDirectProbeCacheEvictions);
	}

	static void LogPso()
	{
		LONG psoCalls = Take(&PsoCalls);
		LONG psoChanged = Take(&PsoChanged);
		LONG psoNoChange = Take(&PsoNoChange);
		LONG runtimePsoCalls = Take(&RuntimePsoCalls);
		LONG runtimePsoChanged = Take(&RuntimePsoChanged);
		LONG runtimePsoNoChange = Take(&RuntimePsoNoChange);
		if (!psoCalls && !psoChanged && !psoNoChange &&
			!runtimePsoCalls && !runtimePsoChanged && !runtimePsoNoChange)
			return;
		DX12LogManager::Fields("DX12ResidentMaintenance",
			"\"category\":\"pso\",\"trackerCalls\":%ld,\"trackerChanged\":%ld,\"trackerNoChange\":%ld,\"runtimeCalls\":%ld,\"runtimeChanged\":%ld,\"runtimeNoChange\":%ld",
			psoCalls, psoChanged, psoNoChange,
			runtimePsoCalls, runtimePsoChanged, runtimePsoNoChange);
	}

	static void LogRootState()
	{
		LONG graphicsRootSignatureCalls = Take(&GraphicsRootSignatureCalls);
		LONG graphicsRootSignatureChanged = Take(&GraphicsRootSignatureChanged);
		LONG graphicsRootSignatureNoChange = Take(&GraphicsRootSignatureNoChange);
		LONG computeRootSignatureCalls = Take(&ComputeRootSignatureCalls);
		LONG computeRootSignatureChanged = Take(&ComputeRootSignatureChanged);
		LONG computeRootSignatureNoChange = Take(&ComputeRootSignatureNoChange);
		LONG graphicsRootTableCalls = Take(&GraphicsRootTableCalls);
		LONG graphicsRootTableTlsHits = Take(&GraphicsRootTableTlsHits);
		LONG graphicsRootTableChanged = Take(&GraphicsRootTableChanged);
		LONG graphicsRootTableNoChange = Take(&GraphicsRootTableNoChange);
		LONG computeRootTableCalls = Take(&ComputeRootTableCalls);
		LONG computeRootTableTlsHits = Take(&ComputeRootTableTlsHits);
		LONG computeRootTableChanged = Take(&ComputeRootTableChanged);
		LONG computeRootTableNoChange = Take(&ComputeRootTableNoChange);
		if (!graphicsRootSignatureCalls && !computeRootSignatureCalls &&
			!graphicsRootTableCalls && !computeRootTableCalls)
			return;
		DX12LogManager::Fields("DX12ResidentMaintenance",
			"\"category\":\"root\",\"gfxSigCalls\":%ld,\"gfxSigChanged\":%ld,\"gfxSigNoChange\":%ld,\"compSigCalls\":%ld,\"compSigChanged\":%ld,\"compSigNoChange\":%ld,\"gfxTableCalls\":%ld,\"gfxTableTlsHits\":%ld,\"gfxTableChanged\":%ld,\"gfxTableNoChange\":%ld,\"compTableCalls\":%ld,\"compTableTlsHits\":%ld,\"compTableChanged\":%ld,\"compTableNoChange\":%ld",
			graphicsRootSignatureCalls, graphicsRootSignatureChanged, graphicsRootSignatureNoChange,
			computeRootSignatureCalls, computeRootSignatureChanged, computeRootSignatureNoChange,
			graphicsRootTableCalls, graphicsRootTableTlsHits, graphicsRootTableChanged, graphicsRootTableNoChange,
			computeRootTableCalls, computeRootTableTlsHits, computeRootTableChanged, computeRootTableNoChange);
	}

	static void LogDescriptorState()
	{
		LONG descriptorHeapResolveCalls = Take(&DescriptorHeapResolveCalls);
		LONG64 descriptorHeapResolveSlots = Take64(&DescriptorHeapResolveSlots);
		LONG descriptorHeapCalls = Take(&DescriptorHeapCalls);
		LONG descriptorHeapChanged = Take(&DescriptorHeapChanged);
		LONG descriptorHeapNoChange = Take(&DescriptorHeapNoChange);
		LONG64 descriptorHeapInvalidatedGraphicsTables = Take64(&DescriptorHeapInvalidatedGraphicsTables);
		LONG64 descriptorHeapInvalidatedComputeTables = Take64(&DescriptorHeapInvalidatedComputeTables);
		LONG graphicsRootDescriptorCalls = Take(&GraphicsRootDescriptorCalls);
		LONG graphicsRootDescriptorTlsHits = Take(&GraphicsRootDescriptorTlsHits);
		LONG graphicsRootDescriptorChanged = Take(&GraphicsRootDescriptorChanged);
		LONG graphicsRootDescriptorNoChange = Take(&GraphicsRootDescriptorNoChange);
		LONG computeRootDescriptorCalls = Take(&ComputeRootDescriptorCalls);
		LONG computeRootDescriptorTlsHits = Take(&ComputeRootDescriptorTlsHits);
		LONG computeRootDescriptorChanged = Take(&ComputeRootDescriptorChanged);
		LONG computeRootDescriptorNoChange = Take(&ComputeRootDescriptorNoChange);
		LONG graphicsRootConstantsCalls = Take(&GraphicsRootConstantsCalls);
		LONG64 graphicsRootConstantsValues = Take64(&GraphicsRootConstantsValues);
		LONG computeRootConstantsCalls = Take(&ComputeRootConstantsCalls);
		LONG64 computeRootConstantsValues = Take64(&ComputeRootConstantsValues);
		if (!descriptorHeapResolveCalls && !descriptorHeapCalls &&
			!graphicsRootDescriptorCalls && !computeRootDescriptorCalls &&
			!graphicsRootConstantsCalls && !computeRootConstantsCalls)
			return;
		DX12LogManager::Fields("DX12ResidentMaintenance",
			"\"category\":\"descriptor\",\"heapResolveCalls\":%ld,\"heapResolveSlots\":%lld,\"heapCalls\":%ld,\"heapChanged\":%ld,\"heapNoChange\":%ld,\"heapInvalidatedGfxTables\":%lld,\"heapInvalidatedCompTables\":%lld,\"gfxDescCalls\":%ld,\"gfxDescTlsHits\":%ld,\"gfxDescChanged\":%ld,\"gfxDescNoChange\":%ld,\"compDescCalls\":%ld,\"compDescTlsHits\":%ld,\"compDescChanged\":%ld,\"compDescNoChange\":%ld,\"gfxConstCalls\":%ld,\"gfxConstValues\":%lld,\"compConstCalls\":%ld,\"compConstValues\":%lld",
			descriptorHeapResolveCalls, static_cast<long long>(descriptorHeapResolveSlots),
			descriptorHeapCalls, descriptorHeapChanged, descriptorHeapNoChange,
			static_cast<long long>(descriptorHeapInvalidatedGraphicsTables),
			static_cast<long long>(descriptorHeapInvalidatedComputeTables),
			graphicsRootDescriptorCalls, graphicsRootDescriptorTlsHits,
			graphicsRootDescriptorChanged, graphicsRootDescriptorNoChange,
			computeRootDescriptorCalls, computeRootDescriptorTlsHits,
			computeRootDescriptorChanged, computeRootDescriptorNoChange,
			graphicsRootConstantsCalls, static_cast<long long>(graphicsRootConstantsValues),
			computeRootConstantsCalls, static_cast<long long>(computeRootConstantsValues));
	}

	static void LogIaState()
	{
		LONG primitiveTopologyCalls = Take(&PrimitiveTopologyCalls);
		LONG primitiveTopologyChanged = Take(&PrimitiveTopologyChanged);
		LONG primitiveTopologyNoChange = Take(&PrimitiveTopologyNoChange);
		LONG runtimePrimitiveTopologyCalls = Take(&RuntimePrimitiveTopologyCalls);
		LONG runtimePrimitiveTopologyChanged = Take(&RuntimePrimitiveTopologyChanged);
		LONG runtimePrimitiveTopologyNoChange = Take(&RuntimePrimitiveTopologyNoChange);
		LONG indexBufferCalls = Take(&IndexBufferCalls);
		LONG indexBufferChanged = Take(&IndexBufferChanged);
		LONG indexBufferNoChange = Take(&IndexBufferNoChange);
		LONG indexBufferResolvedReleased = Take(&IndexBufferResolvedReleased);
		LONG runtimeIndexBufferCalls = Take(&RuntimeIndexBufferCalls);
		LONG runtimeIndexBufferChanged = Take(&RuntimeIndexBufferChanged);
		LONG runtimeIndexBufferNoChange = Take(&RuntimeIndexBufferNoChange);
		LONG vertexBufferCalls = Take(&VertexBufferCalls);
		LONG64 vertexBufferSlots = Take64(&VertexBufferSlots);
		LONG64 vertexBufferChangedSlots = Take64(&VertexBufferChangedSlots);
		LONG64 vertexBufferNoChangeSlots = Take64(&VertexBufferNoChangeSlots);
		LONG vertexBufferResolvedReleased = Take(&VertexBufferResolvedReleased);
		LONG runtimeVertexBufferCalls = Take(&RuntimeVertexBufferCalls);
		LONG64 runtimeVertexBufferSlots = Take64(&RuntimeVertexBufferSlots);
		LONG64 runtimeVertexBufferChangedSlots = Take64(&RuntimeVertexBufferChangedSlots);
		LONG64 runtimeVertexBufferNoChangeSlots = Take64(&RuntimeVertexBufferNoChangeSlots);
		if (!primitiveTopologyCalls && !runtimePrimitiveTopologyCalls &&
			!indexBufferCalls && !runtimeIndexBufferCalls &&
			!vertexBufferCalls && !runtimeVertexBufferCalls)
			return;
		DX12LogManager::Fields("DX12ResidentMaintenance",
			"\"category\":\"ia\",\"topologyCalls\":%ld,\"topologyChanged\":%ld,\"topologyNoChange\":%ld,\"runtimeTopologyCalls\":%ld,\"runtimeTopologyChanged\":%ld,\"runtimeTopologyNoChange\":%ld,\"ibCalls\":%ld,\"ibChanged\":%ld,\"ibNoChange\":%ld,\"ibResolvedReleased\":%ld,\"runtimeIbCalls\":%ld,\"runtimeIbChanged\":%ld,\"runtimeIbNoChange\":%ld,\"vbCalls\":%ld,\"vbSlots\":%lld,\"vbChangedSlots\":%lld,\"vbNoChangeSlots\":%lld,\"vbResolvedReleased\":%ld,\"runtimeVbCalls\":%ld,\"runtimeVbSlots\":%lld,\"runtimeVbChangedSlots\":%lld,\"runtimeVbNoChangeSlots\":%lld",
			primitiveTopologyCalls, primitiveTopologyChanged, primitiveTopologyNoChange,
			runtimePrimitiveTopologyCalls, runtimePrimitiveTopologyChanged, runtimePrimitiveTopologyNoChange,
			indexBufferCalls, indexBufferChanged, indexBufferNoChange, indexBufferResolvedReleased,
			runtimeIndexBufferCalls, runtimeIndexBufferChanged, runtimeIndexBufferNoChange,
			vertexBufferCalls, static_cast<long long>(vertexBufferSlots),
			static_cast<long long>(vertexBufferChangedSlots),
			static_cast<long long>(vertexBufferNoChangeSlots),
			vertexBufferResolvedReleased,
			runtimeVertexBufferCalls, static_cast<long long>(runtimeVertexBufferSlots),
			static_cast<long long>(runtimeVertexBufferChangedSlots),
			static_cast<long long>(runtimeVertexBufferNoChangeSlots));
	}

	static void LogBarrierState()
	{
		LONG barrierCaptureCalls = Take(&BarrierCaptureCalls);
		LONG64 barrierCaptureItems = Take64(&BarrierCaptureItems);
		LONG barrierCaptureSkippedInternalReplay = Take(&BarrierCaptureSkippedInternalReplay);
		LONG barrierCaptureSkippedHotPath = Take(&BarrierCaptureSkippedHotPath);
		LONG barrierCaptureSkippedStatePolicy = Take(&BarrierCaptureSkippedStatePolicy);
		LONG barrierRecordedCalls = Take(&BarrierRecordedCalls);
		LONG64 barrierRecordedItems = Take64(&BarrierRecordedItems);
		LONG barrierUnwrapCalls = Take(&BarrierUnwrapCalls);
		LONG64 barrierUnwrapItems = Take64(&BarrierUnwrapItems);
		if (!barrierCaptureCalls && !barrierRecordedCalls && !barrierUnwrapCalls)
			return;
		DX12LogManager::Fields("DX12ResidentMaintenance",
			"\"category\":\"barrier\",\"captureCalls\":%ld,\"captureItems\":%lld,\"captureSkippedInternalReplay\":%ld,\"captureSkippedHotPath\":%ld,\"captureSkippedStatePolicy\":%ld,\"recordedCalls\":%ld,\"recordedItems\":%lld,\"unwrapCalls\":%ld,\"unwrapItems\":%lld",
			barrierCaptureCalls, static_cast<long long>(barrierCaptureItems),
			barrierCaptureSkippedInternalReplay, barrierCaptureSkippedHotPath,
			barrierCaptureSkippedStatePolicy,
			barrierRecordedCalls, static_cast<long long>(barrierRecordedItems),
			barrierUnwrapCalls, static_cast<long long>(barrierUnwrapItems));
	}

	static void LogPreSkinBindingState()
	{
		LONG preSkinBindingCacheCalls = Take(&PreSkinBindingCacheCalls);
		LONG preSkinBindingCacheHits = Take(&PreSkinBindingCacheHits);
		LONG preSkinBindingCacheMisses = Take(&PreSkinBindingCacheMisses);
		LONG preSkinBindingCacheStores = Take(&PreSkinBindingCacheStores);
		LONG64 preSkinBindingRootTables = Take64(&PreSkinBindingRootTables);
		LONG64 preSkinBindingRanges = Take64(&PreSkinBindingRanges);
		LONG64 preSkinBindingLightDescriptorLookups = Take64(&PreSkinBindingLightDescriptorLookups);
		LONG64 preSkinBindingFullDescriptorLookups = Take64(&PreSkinBindingFullDescriptorLookups);
		LONG64 preSkinBindingConstants = Take64(&PreSkinBindingConstants);
		LONG64 preSkinBindingRootDescriptors = Take64(&PreSkinBindingRootDescriptors);
		LONG64 preSkinBindingRuntimeResolved = Take64(&PreSkinBindingRuntimeResolved);
		LONG64 preSkinBindingRuntimeUnresolved = Take64(&PreSkinBindingRuntimeUnresolved);
		LONG64 preSkinBindingDescriptorHits = Take64(&PreSkinBindingDescriptorHits);
		LONG64 preSkinBindingDescriptorMisses = Take64(&PreSkinBindingDescriptorMisses);
		LONG64 preSkinBindingDescriptorPending = Take64(&PreSkinBindingDescriptorPending);
		LONG64 preSkinBindingWaitAvoided = Take64(&PreSkinBindingWaitAvoided);
		LONG64 runtimeDescriptorWrites = Take64(&RuntimeDescriptorWrites);
		LONG64 runtimeDescriptorCopyCalls = Take64(&RuntimeDescriptorCopyCalls);
		LONG64 runtimeDescriptorCopied = Take64(&RuntimeDescriptorCopied);
		LONG64 runtimeDescriptorCopyMisses = Take64(&RuntimeDescriptorCopyMisses);
		if (!preSkinBindingCacheCalls && !preSkinBindingCacheHits &&
			!preSkinBindingCacheMisses && !preSkinBindingCacheStores &&
			!preSkinBindingRuntimeResolved && !preSkinBindingRuntimeUnresolved &&
			!preSkinBindingWaitAvoided && !runtimeDescriptorWrites &&
			!runtimeDescriptorCopyCalls)
			return;
		DX12LogManager::Fields("DX12ResidentMaintenance",
			"\"category\":\"preskin_bind_cache\",\"calls\":%ld,\"hits\":%ld,\"misses\":%ld,\"stores\":%ld,\"rootTables\":%lld,\"ranges\":%lld,\"lightDescriptorLookups\":%lld,\"fullDescriptorLookups\":%lld,\"constants\":%lld,\"rootDescriptors\":%lld,\"runtimeResolved\":%lld,\"runtimeUnresolved\":%lld,\"descriptorHits\":%lld,\"descriptorMisses\":%lld,\"descriptorPending\":%lld,\"waitAvoided\":%lld,\"runtimeDescriptorWrites\":%lld,\"runtimeDescriptorCopyCalls\":%lld,\"runtimeDescriptorCopied\":%lld,\"runtimeDescriptorCopyMisses\":%lld",
			preSkinBindingCacheCalls, preSkinBindingCacheHits,
			preSkinBindingCacheMisses, preSkinBindingCacheStores,
			static_cast<long long>(preSkinBindingRootTables),
			static_cast<long long>(preSkinBindingRanges),
			static_cast<long long>(preSkinBindingLightDescriptorLookups),
			static_cast<long long>(preSkinBindingFullDescriptorLookups),
			static_cast<long long>(preSkinBindingConstants),
			static_cast<long long>(preSkinBindingRootDescriptors),
			static_cast<long long>(preSkinBindingRuntimeResolved),
			static_cast<long long>(preSkinBindingRuntimeUnresolved),
			static_cast<long long>(preSkinBindingDescriptorHits),
			static_cast<long long>(preSkinBindingDescriptorMisses),
			static_cast<long long>(preSkinBindingDescriptorPending),
			static_cast<long long>(preSkinBindingWaitAvoided),
			static_cast<long long>(runtimeDescriptorWrites),
			static_cast<long long>(runtimeDescriptorCopyCalls),
			static_cast<long long>(runtimeDescriptorCopied),
			static_cast<long long>(runtimeDescriptorCopyMisses));
	}

	static void LogRuntimeLookup()
	{
		LONG tlsHits = Take(&RuntimeLookupTlsHits);
		LONG directHits = Take(&RuntimeLookupDirectHits);
		LONG shardHits = Take(&RuntimeLookupShardHits);
		LONG misses = Take(&RuntimeLookupMisses);
		if (!tlsHits && !directHits && !shardHits && !misses)
			return;
		DX12LogManager::Fields("DX12ResidentMaintenance",
			"\"category\":\"runtime_lookup\",\"tlsHits\":%ld,\"directHits\":%ld,\"shardHits\":%ld,\"misses\":%ld",
			tlsHits, directHits, shardHits, misses);
	}

	static void LogDispatchDecisionState()
	{
		LONG cacheHits = Take(&DispatchDecisionCacheHits);
		LONG cacheMisses = Take(&DispatchDecisionCacheMisses);
		LONG psoCacheHits = Take(&DispatchDecisionPsoCacheHits);
		LONG psoCacheMisses = Take(&DispatchDecisionPsoCacheMisses);
		LONG builds = Take(&DispatchDecisionBuilds);
		LONG noCompute = Take(&DispatchDecisionNoCompute);
		LONG noPreSkinCandidate = Take(&DispatchDecisionNoPreSkinCandidate);
		LONG preSkinCandidate = Take(&DispatchDecisionPreSkinCandidate);
		LONG preSkinCandidateDispatches = Take(&DispatchDecisionPreSkinCandidateDispatches);
		LONG shaderOverrideWork = Take(&DispatchDecisionShaderOverrideWork);
		LONG fastRejects = Take(&DispatchDecisionFastRejects);
		if (!cacheHits && !cacheMisses && !psoCacheHits && !psoCacheMisses &&
			!builds && !noCompute &&
			!noPreSkinCandidate && !preSkinCandidate && !preSkinCandidateDispatches &&
			!shaderOverrideWork && !fastRejects)
			return;
		DX12LogManager::Fields("DX12ResidentMaintenance",
			"\"category\":\"dispatch_decision\",\"cacheHits\":%ld,\"cacheMisses\":%ld,\"psoCacheHits\":%ld,\"psoCacheMisses\":%ld,\"builds\":%ld,\"noCompute\":%ld,\"noPreSkinCandidate\":%ld,\"preSkinCandidateBuilds\":%ld,\"preSkinCandidateDispatches\":%ld,\"shaderOverrideWork\":%ld,\"fastRejects\":%ld",
			cacheHits, cacheMisses, psoCacheHits, psoCacheMisses, builds, noCompute,
			noPreSkinCandidate, preSkinCandidate, preSkinCandidateDispatches,
			shaderOverrideWork, fastRejects);
	}

	static void LogPreSkinPlanState()
	{
		LONG lookups = Take(&PreSkinPlanCacheLookups);
		LONG hits = Take(&PreSkinPlanCacheHits);
		LONG negativeHits = Take(&PreSkinPlanCacheNegativeHits);
		LONG positiveHits = Take(&PreSkinPlanCachePositiveHits);
		LONG misses = Take(&PreSkinPlanCacheMisses);
		LONG stores = Take(&PreSkinPlanCacheStores);
		LONG fingerprints = Take(&PreSkinPlanFingerprintBuilds);
		if (!lookups && !hits && !negativeHits && !positiveHits &&
			!misses && !stores && !fingerprints)
			return;
		DX12LogManager::Fields("DX12ResidentMaintenance",
			"\"category\":\"preskin_plan\",\"lookups\":%ld,\"hits\":%ld,\"negativeHits\":%ld,\"positiveHits\":%ld,\"misses\":%ld,\"stores\":%ld,\"fingerprintBuilds\":%ld",
			lookups, hits, negativeHits, positiveHits,
			misses, stores, fingerprints);
	}

	static void LogDrawDecisionState()
	{
		LONG cacheHits = Take(&DrawDecisionCacheHits);
		LONG cacheMisses = Take(&DrawDecisionCacheMisses);
		LONG psoCacheHits = Take(&DrawDecisionPsoCacheHits);
		LONG psoCacheMisses = Take(&DrawDecisionPsoCacheMisses);
		LONG builds = Take(&DrawDecisionBuilds);
		LONG noPso = Take(&DrawDecisionNoPso);
		LONG noShaderOverride = Take(&DrawDecisionNoShaderOverride);
		LONG noDrawOverride = Take(&DrawDecisionNoDrawOverride);
		LONG needsIaHash = Take(&DrawDecisionNeedsIaHash);
		LONG needsDescriptorMatch = Take(&DrawDecisionNeedsDescriptorMatch);
		LONG needsReplacement = Take(&DrawDecisionNeedsReplacement);
		LONG skipOriginal = Take(&DrawDecisionSkipOriginal);
		LONG fastRejects = Take(&DrawDecisionFastRejects);
		LONG iaHashDraws = Take(&DrawDecisionIaHashDraws);
		LONG iaHashCacheLookups = Take(&DrawDecisionIaHashCacheLookups);
		LONG iaHashCacheHits = Take(&DrawDecisionIaHashCacheHits);
		LONG iaHashCacheMisses = Take(&DrawDecisionIaHashCacheMisses);
		LONG iaHashCacheStores = Take(&DrawDecisionIaHashCacheStores);
		LONG iaHashCacheEvictions = Take(&DrawDecisionIaHashCacheEvictions);
		LONG iaHashCacheNegativeHits = Take(&DrawDecisionIaHashCacheNegativeHits);
		LONG descriptorDraws = Take(&DrawDecisionDescriptorDraws);
		LONG directProbeCacheLookups = Take(&DrawDecisionDirectProbeCacheLookups);
		LONG directProbeCacheHits = Take(&DrawDecisionDirectProbeCacheHits);
		LONG directProbeCacheMisses = Take(&DrawDecisionDirectProbeCacheMisses);
		LONG directProbeCachePositiveHits = Take(&DrawDecisionDirectProbeCachePositiveHits);
		LONG directProbeCacheNegativeHits = Take(&DrawDecisionDirectProbeCacheNegativeHits);
		LONG directProbeCacheStores = Take(&DrawDecisionDirectProbeCacheStores);
		LONG directProbeCacheEvictions = Take(&DrawDecisionDirectProbeCacheEvictions);
		if (!cacheHits && !cacheMisses && !psoCacheHits && !psoCacheMisses &&
			!builds && !noPso &&
			!noShaderOverride && !noDrawOverride && !needsIaHash &&
			!needsDescriptorMatch && !needsReplacement && !skipOriginal &&
			!fastRejects && !iaHashDraws && !iaHashCacheLookups &&
			!iaHashCacheHits && !iaHashCacheMisses && !iaHashCacheStores &&
			!iaHashCacheEvictions &&
			!iaHashCacheNegativeHits && !descriptorDraws &&
			!directProbeCacheLookups && !directProbeCacheHits &&
			!directProbeCacheMisses && !directProbeCachePositiveHits &&
			!directProbeCacheNegativeHits && !directProbeCacheStores &&
			!directProbeCacheEvictions)
			return;
		DX12LogManager::Fields("DX12ResidentMaintenance",
			"\"category\":\"draw_decision\",\"cacheHits\":%ld,\"cacheMisses\":%ld,\"psoCacheHits\":%ld,\"psoCacheMisses\":%ld,\"builds\":%ld,\"noPso\":%ld,\"noShaderOverride\":%ld,\"noDrawOverride\":%ld,\"needsIaHash\":%ld,\"needsDescriptorMatch\":%ld,\"needsReplacement\":%ld,\"skipOriginal\":%ld,\"fastRejects\":%ld,\"iaHashDraws\":%ld,\"iaHashCacheLookups\":%ld,\"iaHashCacheHits\":%ld,\"iaHashCacheMisses\":%ld,\"iaHashCacheStores\":%ld,\"iaHashCacheEvictions\":%ld,\"iaHashCacheNegativeHits\":%ld,\"descriptorDraws\":%ld,\"directProbeCacheLookups\":%ld,\"directProbeCacheHits\":%ld,\"directProbeCacheMisses\":%ld,\"directProbeCachePositiveHits\":%ld,\"directProbeCacheNegativeHits\":%ld,\"directProbeCacheStores\":%ld,\"directProbeCacheEvictions\":%ld",
			cacheHits, cacheMisses, psoCacheHits, psoCacheMisses, builds, noPso,
			noShaderOverride, noDrawOverride, needsIaHash,
			needsDescriptorMatch, needsReplacement, skipOriginal,
			fastRejects, iaHashDraws, iaHashCacheLookups,
			iaHashCacheHits, iaHashCacheMisses, iaHashCacheStores, iaHashCacheEvictions,
			iaHashCacheNegativeHits, descriptorDraws,
			directProbeCacheLookups, directProbeCacheHits, directProbeCacheMisses,
			directProbeCachePositiveHits, directProbeCacheNegativeHits,
			directProbeCacheStores, directProbeCacheEvictions);
	}

	static void LogAndReset()
	{
		LogRuntimeLookup();
		LogPso();
		LogRootState();
		LogDescriptorState();
		LogIaState();
		LogBarrierState();
		LogPreSkinBindingState();
		LogDispatchDecisionState();
		LogPreSkinPlanState();
		LogDrawDecisionState();
	}
};



class DX12CommandListStateFlow
{
public:
	static ID3D12PipelineState *CapturePipelineState(
		ID3D12GraphicsCommandList *commandList,
		ID3D12PipelineState *pipelineState)
	{
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		return DX12CommandListStateFlow::CapturePipelineState(
			commandList, runtimeState, pipelineState);
	}

	static ID3D12PipelineState *CapturePipelineState(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeState *runtimeState,
		ID3D12PipelineState *pipelineState)
	{
		// Same-PSO fast path: when the identical PSO is set again, skip all
		// tracking.  Resolve runtimeState if the caller didn't provide one.
		DX12CommandListRuntimeState *rs = runtimeState;
		if (!rs) {
			rs = DX12CommandListRuntime::GetStatePtrFast(commandList);
		}
		if (rs && (rs->pipelineState == pipelineState ||
				rs->cachedReplacementPsoKey == pipelineState)) {
			DX12_PROFILE_FAST_FORWARD();
			return rs->cachedReplacementPso
				? rs->cachedReplacementPso : pipelineState;
		}

		const UINT64 flags = DX12HotPathState::HookWorkFlags;
		if (DX12HotPathState::skipBindings &&
		    DX12HotPathState::NeedsHuntOnlyPipelineObservation(flags)) {
			DX12_PROFILE_FAST_FORWARD();
			if (runtimeState)
				DX12CommandListRuntime::RememberPipelineState(runtimeState, pipelineState);
			else
				DX12CommandListRuntime::RememberPipelineState(commandList, pipelineState);
			DX12HuntObservePipelineState(pipelineState);
			return pipelineState;
		}

		ID3D12PipelineState *activePipelineState =
			DX12CommandListStateCapture::PipelineState(commandList, runtimeState, pipelineState);
		DX12CommandListFlowDiagnostics::LogDX12Call(
			"ID3D12GraphicsCommandList::SetPipelineState", commandList,
			" pso=%p active=%p", pipelineState, activePipelineState);
		return activePipelineState;
	}

	static bool CaptureResourceBarrier(
		ID3D12GraphicsCommandList *commandList,
		UINT numBarriers,
		const D3D12_RESOURCE_BARRIER *barriers)
	{
		DX12ResidentMaintenanceTelemetryUtils::RecordBarrierCaptureCall(numBarriers);
		if (DX12InternalReplayState::IsActive()) {
			DX12ResidentMaintenanceTelemetryUtils::RecordBarrierCaptureSkippedInternalReplay();
			return false;
		}
		if (DX12HotPathState::skipBindings) {
			DX12_PROFILE_FAST_FORWARD();
			DX12ResidentMaintenanceTelemetryUtils::RecordBarrierCaptureSkippedHotPath();
			return false;
		}
		if (!DX12CommandListStateCapture::ShouldTrackResourceStates()) {
			DX12_PROFILE_FAST_FORWARD();
			DX12ResidentMaintenanceTelemetryUtils::RecordBarrierCaptureSkippedStatePolicy();
			return false;
		}

		DX12CommandListFlowDiagnostics::LogDX12Call(
			"ID3D12GraphicsCommandList::ResourceBarrier", commandList,
			" count=%u", numBarriers);
		DX12CommandListCommandRecorder::RecordResourceBarrier(numBarriers, barriers);
		DX12ResidentMaintenanceTelemetryUtils::RecordBarrierRecorded(numBarriers);
		return true;
	}

	static void CapturePrimitiveTopology(
		ID3D12GraphicsCommandList *commandList,
		D3D12_PRIMITIVE_TOPOLOGY topology)
	{
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12CommandListStateFlow::CapturePrimitiveTopology(commandList, runtimeState, topology);
	}

	static void CapturePrimitiveTopology(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeState *runtimeState,
		D3D12_PRIMITIVE_TOPOLOGY topology)
	{
		if (!DX12HotPathState::skipBindings) {
			DX12CommandListFlowDiagnostics::LogDX12Call(
				"ID3D12GraphicsCommandList::IASetPrimitiveTopology", commandList,
				" topology=%u", static_cast<UINT>(topology));
			DX12CommandListStateCapture::PrimitiveTopology(commandList, runtimeState, topology);
			return;
		}

		const UINT64 flags = DX12HotPathState::HookWorkFlags;
		if (DX12HotPathState::NeedsResidentIaState(flags)) {
			if (runtimeState)
				DX12CommandListRuntime::RememberPrimitiveTopology(runtimeState, topology);
			else
				DX12CommandListRuntime::RememberPrimitiveTopology(commandList, topology);
		}
	}

	static void CaptureIndexBuffer(
		ID3D12GraphicsCommandList *commandList,
		const D3D12_INDEX_BUFFER_VIEW *view)
	{
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12CommandListStateFlow::CaptureIndexBuffer(commandList, runtimeState, view);
	}

	static void CaptureIndexBuffer(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeState *runtimeState,
		const D3D12_INDEX_BUFFER_VIEW *view)
	{
		if (!DX12HotPathState::skipBindings) {
			DX12CommandListFlowDiagnostics::LogDX12Call(
				"ID3D12GraphicsCommandList::IASetIndexBuffer", commandList,
				" gpu=0x%llx size=%u format=%u",
				view ? static_cast<unsigned long long>(view->BufferLocation) : 0ull,
				view ? view->SizeInBytes : 0,
				view ? static_cast<UINT>(view->Format) : 0);
			DX12CommandListStateCapture::IndexBuffer(commandList, runtimeState, view);
			return;
		}

		DX12_PROFILE_FAST_FORWARD();
		const UINT64 flags = DX12HotPathState::HookWorkFlags;
		if (DX12HotPathState::HasAnyFlag(flags, DX12HotPathState::FLAG_HUNT_ENABLED))
			DX12Profiling::RecordIaHuntIaUpdate();
		if (DX12HotPathState::NeedsResidentIaState(flags)) {
			if (runtimeState)
				DX12CommandListRuntime::RememberIndexBuffer(runtimeState, view);
			else
				DX12CommandListRuntime::RememberIndexBuffer(commandList, view);
		}
	}

	static void CaptureVertexBuffers(
		ID3D12GraphicsCommandList *commandList,
		UINT startSlot,
		UINT count,
		const D3D12_VERTEX_BUFFER_VIEW *views)
	{
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12CommandListStateFlow::CaptureVertexBuffers(
			commandList, runtimeState, startSlot, count, views);
	}

	static void CaptureVertexBuffers(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeState *runtimeState,
		UINT startSlot,
		UINT count,
		const D3D12_VERTEX_BUFFER_VIEW *views)
	{
		if (!DX12HotPathState::skipBindings) {
			DX12CommandListFlowDiagnostics::LogDX12Call(
				"ID3D12GraphicsCommandList::IASetVertexBuffers", commandList,
				" start=%u count=%u", startSlot, count);
			DX12CommandListStateCapture::VertexBuffers(
				commandList, runtimeState, startSlot, count, views);
			return;
		}

		DX12_PROFILE_FAST_FORWARD();
		const UINT64 flags = DX12HotPathState::HookWorkFlags;
		if (DX12HotPathState::HasAnyFlag(flags, DX12HotPathState::FLAG_HUNT_ENABLED))
			DX12Profiling::RecordIaHuntIaUpdate();
		if (DX12HotPathState::NeedsResidentIaState(flags)) {
			if (runtimeState)
				DX12CommandListRuntime::RememberVertexBuffers(runtimeState, startSlot, count, views);
			else
				DX12CommandListRuntime::RememberVertexBuffers(commandList, startSlot, count, views);
		}
	}
};
