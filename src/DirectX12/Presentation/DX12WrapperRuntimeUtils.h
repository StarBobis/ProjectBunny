// Wrapper runtime utilities for frame presentation and overlay wiring.
// Split from DX12Presentation.h; keep declarations in the layer that owns them.
#pragma once

#include "DXGIHookRuntime.h"

class DX12WrapperRuntimeUtils
{
public:
	static IUnknown *UnwrapD3D12Child(IUnknown *object)
	{
		return DX12Wrapper::Unwrap(object);
	}

	static ID3D12CommandAllocator *UnwrapCommandAllocator(ID3D12CommandAllocator *allocator)
	{
		return reinterpret_cast<ID3D12CommandAllocator*>(UnwrapD3D12Child(allocator));
	}

	static ID3D12CommandSignature *UnwrapCommandSignature(ID3D12CommandSignature *signature)
	{
		return reinterpret_cast<ID3D12CommandSignature*>(UnwrapD3D12Child(signature));
	}

	static ID3D12CommandList *UnwrapCommandList(ID3D12CommandList *commandList)
	{
		return reinterpret_cast<ID3D12CommandList*>(UnwrapD3D12Child(commandList));
	}

	static ID3D12DescriptorHeap *UnwrapDescriptorHeap(ID3D12DescriptorHeap *heap)
	{
		return reinterpret_cast<ID3D12DescriptorHeap*>(UnwrapD3D12Child(heap));
	}

	static ID3D12Fence *UnwrapFence(ID3D12Fence *fence)
	{
		return reinterpret_cast<ID3D12Fence*>(UnwrapD3D12Child(fence));
	}

	static ID3D12GraphicsCommandList *UnwrapGraphicsCommandList(ID3D12GraphicsCommandList *commandList)
	{
		return reinterpret_cast<ID3D12GraphicsCommandList*>(UnwrapD3D12Child(commandList));
	}

	static ID3D12Heap *UnwrapHeap(ID3D12Heap *heap)
	{
		return reinterpret_cast<ID3D12Heap*>(UnwrapD3D12Child(heap));
	}

	static ID3D12MetaCommand *UnwrapMetaCommand(ID3D12MetaCommand *metaCommand)
	{
		return reinterpret_cast<ID3D12MetaCommand*>(UnwrapD3D12Child(metaCommand));
	}

	static ID3D12Pageable *UnwrapPageable(ID3D12Pageable *pageable)
	{
		return reinterpret_cast<ID3D12Pageable*>(UnwrapD3D12Child(pageable));
	}

	static ID3D12PipelineState *UnwrapPipelineState(ID3D12PipelineState *pipelineState)
	{
		DX12_HOTPATH_TIME_BEGIN(UnwrapObjTotal);
		ID3D12PipelineState *result =
			reinterpret_cast<ID3D12PipelineState*>(UnwrapD3D12Child(pipelineState));
		DX12_HOTPATH_TIME_END(UnwrapObjTotal);
		return result;
	}

	static ID3D12ProtectedResourceSession *UnwrapProtectedResourceSession(
		ID3D12ProtectedResourceSession *session)
	{
		return reinterpret_cast<ID3D12ProtectedResourceSession*>(UnwrapD3D12Child(session));
	}

	static ID3D12QueryHeap *UnwrapQueryHeap(ID3D12QueryHeap *heap)
	{
		return reinterpret_cast<ID3D12QueryHeap*>(UnwrapD3D12Child(heap));
	}

	static ID3D12Resource *UnwrapResource(ID3D12Resource *resource)
	{
		return reinterpret_cast<ID3D12Resource*>(UnwrapD3D12Child(resource));
	}

	static ID3D12RootSignature *UnwrapRootSignature(ID3D12RootSignature *rootSignature)
	{
		return reinterpret_cast<ID3D12RootSignature*>(UnwrapD3D12Child(rootSignature));
	}

	static ID3D12StateObject *UnwrapStateObject(ID3D12StateObject *stateObject)
	{
		return reinterpret_cast<ID3D12StateObject*>(UnwrapD3D12Child(stateObject));
	}

	static const D3D12_GRAPHICS_PIPELINE_STATE_DESC *UnwrapGraphicsPipelineStateDesc(
		const D3D12_GRAPHICS_PIPELINE_STATE_DESC *source,
		D3D12_GRAPHICS_PIPELINE_STATE_DESC *storage)
	{
		if (!source || !storage)
			return source;
		*storage = *source;
		storage->pRootSignature = UnwrapRootSignature(source->pRootSignature);
		return storage;
	}

	static const D3D12_COMPUTE_PIPELINE_STATE_DESC *UnwrapComputePipelineStateDesc(
		const D3D12_COMPUTE_PIPELINE_STATE_DESC *source,
		D3D12_COMPUTE_PIPELINE_STATE_DESC *storage)
	{
		if (!source || !storage)
			return source;
		*storage = *source;
		storage->pRootSignature = UnwrapRootSignature(source->pRootSignature);
		return storage;
	}

	static void UnwrapStateObjectDesc(
		const D3D12_STATE_OBJECT_DESC *source,
		D3D12_STATE_OBJECT_DESC *descStorage,
		std::vector<D3D12_STATE_SUBOBJECT> *subobjectStorage,
		std::vector<D3D12_GLOBAL_ROOT_SIGNATURE> *globalRootSignatureStorage,
		std::vector<D3D12_LOCAL_ROOT_SIGNATURE> *localRootSignatureStorage,
		std::vector<D3D12_EXISTING_COLLECTION_DESC> *existingCollectionStorage,
		std::vector<D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION> *associationStorage,
		const D3D12_STATE_OBJECT_DESC **result)
	{
		if (!result)
			return;
		*result = source;
		if (!source || !descStorage || !subobjectStorage || !globalRootSignatureStorage ||
			!localRootSignatureStorage || !existingCollectionStorage || !associationStorage ||
			!source->pSubobjects || !source->NumSubobjects)
			return;

		*descStorage = *source;
		subobjectStorage->assign(source->pSubobjects, source->pSubobjects + source->NumSubobjects);
		globalRootSignatureStorage->clear();
		localRootSignatureStorage->clear();
		existingCollectionStorage->clear();
		associationStorage->clear();
		globalRootSignatureStorage->resize(source->NumSubobjects);
		localRootSignatureStorage->resize(source->NumSubobjects);
		existingCollectionStorage->resize(source->NumSubobjects);
		associationStorage->resize(source->NumSubobjects);

		for (UINT i = 0; i < source->NumSubobjects; ++i) {
			const D3D12_STATE_SUBOBJECT *sourceSubobject = &source->pSubobjects[i];
			D3D12_STATE_SUBOBJECT *activeSubobject = &(*subobjectStorage)[i];
			activeSubobject->pDesc = sourceSubobject->pDesc;

			if (!sourceSubobject->pDesc)
				continue;

			switch (sourceSubobject->Type) {
			case D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE:
				(*globalRootSignatureStorage)[i] =
					*reinterpret_cast<const D3D12_GLOBAL_ROOT_SIGNATURE*>(sourceSubobject->pDesc);
				(*globalRootSignatureStorage)[i].pGlobalRootSignature =
					UnwrapRootSignature((*globalRootSignatureStorage)[i].pGlobalRootSignature);
				activeSubobject->pDesc = &(*globalRootSignatureStorage)[i];
				break;
			case D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE:
				(*localRootSignatureStorage)[i] =
					*reinterpret_cast<const D3D12_LOCAL_ROOT_SIGNATURE*>(sourceSubobject->pDesc);
				(*localRootSignatureStorage)[i].pLocalRootSignature =
					UnwrapRootSignature((*localRootSignatureStorage)[i].pLocalRootSignature);
				activeSubobject->pDesc = &(*localRootSignatureStorage)[i];
				break;
			case D3D12_STATE_SUBOBJECT_TYPE_EXISTING_COLLECTION:
				(*existingCollectionStorage)[i] =
					*reinterpret_cast<const D3D12_EXISTING_COLLECTION_DESC*>(sourceSubobject->pDesc);
				(*existingCollectionStorage)[i].pExistingCollection =
					UnwrapStateObject((*existingCollectionStorage)[i].pExistingCollection);
				activeSubobject->pDesc = &(*existingCollectionStorage)[i];
				break;
			case D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION:
				(*associationStorage)[i] =
					*reinterpret_cast<const D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION*>(sourceSubobject->pDesc);
				for (UINT associatedIndex = 0; associatedIndex < source->NumSubobjects; ++associatedIndex) {
					if ((*associationStorage)[i].pSubobjectToAssociate == &source->pSubobjects[associatedIndex]) {
						(*associationStorage)[i].pSubobjectToAssociate = &(*subobjectStorage)[associatedIndex];
						break;
					}
				}
				activeSubobject->pDesc = &(*associationStorage)[i];
				break;
			default:
				break;
			}
		}

		descStorage->pSubobjects = subobjectStorage->data();
		*result = descStorage;
	}

	static void UnwrapResourceArray(
		ID3D12Resource *const *source,
		UINT count,
		std::vector<ID3D12Resource*> *storage,
		ID3D12Resource *const **result)
	{
		if (!result)
			return;
		*result = source;
		if (!source || !storage || !count)
			return;
		storage->resize(count);
		for (UINT i = 0; i < count; ++i)
			(*storage)[i] = UnwrapResource(source[i]);
		*result = storage->data();
	}

	static void UnwrapTextureCopyLocation(
		const D3D12_TEXTURE_COPY_LOCATION *source,
		D3D12_TEXTURE_COPY_LOCATION *storage,
		const D3D12_TEXTURE_COPY_LOCATION **result)
	{
		if (!result)
			return;
		*result = source;
		if (!source || !storage)
			return;
		*storage = *source;
		storage->pResource = UnwrapResource(source->pResource);
		*result = storage;
	}

	static void UnwrapResourceBarrier(
		const D3D12_RESOURCE_BARRIER *source,
		D3D12_RESOURCE_BARRIER *storage)
	{
		if (!source || !storage)
			return;
		*storage = *source;
		switch (storage->Type) {
		case D3D12_RESOURCE_BARRIER_TYPE_TRANSITION:
			storage->Transition.pResource = UnwrapResource(source->Transition.pResource);
			break;
		case D3D12_RESOURCE_BARRIER_TYPE_ALIASING:
			storage->Aliasing.pResourceBefore = UnwrapResource(source->Aliasing.pResourceBefore);
			storage->Aliasing.pResourceAfter = UnwrapResource(source->Aliasing.pResourceAfter);
			break;
		case D3D12_RESOURCE_BARRIER_TYPE_UAV:
			storage->UAV.pResource = UnwrapResource(source->UAV.pResource);
			break;
		default:
			break;
		}
	}

	static void UnwrapResourceBarrierArray(
		const D3D12_RESOURCE_BARRIER *source,
		UINT count,
		std::vector<D3D12_RESOURCE_BARRIER> *storage,
		const D3D12_RESOURCE_BARRIER **result)
	{
		if (!result)
			return;
		*result = source;
		if (!source || !storage || !count)
			return;
		DX12ResidentMaintenanceTelemetryUtils::RecordBarrierUnwrap(count);
		// DX12 callers overwhelmingly submit native resources. Keep that common
		// case on the original array. If a wrapped resource is encountered,
		// switch to copy mode in the same pass instead of probing every element
		// once and then looking it up again during the second pass.
		for (UINT i = 0; i < count; ++i) {
			const D3D12_RESOURCE_BARRIER &barrier = source[i];
			ID3D12Resource *before = nullptr;
			ID3D12Resource *after = nullptr;
			switch (barrier.Type) {
			case D3D12_RESOURCE_BARRIER_TYPE_TRANSITION:
				before = barrier.Transition.pResource;
				after = UnwrapResource(before);
				break;
			case D3D12_RESOURCE_BARRIER_TYPE_ALIASING:
				before = barrier.Aliasing.pResourceBefore;
				after = UnwrapResource(before);
				if (after == before) {
					before = barrier.Aliasing.pResourceAfter;
					after = UnwrapResource(before);
				}
				break;
			case D3D12_RESOURCE_BARRIER_TYPE_UAV:
				before = barrier.UAV.pResource;
				after = UnwrapResource(before);
				break;
			default:
				break;
			}

			if (*result == source && after == before)
				continue;
			if (*result == source) {
				storage->resize(count);
				for (UINT previous = 0; previous < i; ++previous)
					(*storage)[previous] = source[previous];
				*result = storage->data();
			}
			UnwrapResourceBarrier(&source[i], &(*storage)[i]);
		}
	}

	static void UnwrapBarrierGroups(
		const D3D12_BARRIER_GROUP *source,
		UINT32 count,
		std::vector<D3D12_BARRIER_GROUP> *groupStorage,
		std::vector<std::vector<D3D12_TEXTURE_BARRIER>> *textureStorage,
		std::vector<std::vector<D3D12_BUFFER_BARRIER>> *bufferStorage,
		const D3D12_BARRIER_GROUP **result)
	{
		if (!result)
			return;
		*result = source;
		if (!source || !groupStorage || !textureStorage || !bufferStorage || !count)
			return;

		groupStorage->resize(count);
		textureStorage->clear();
		bufferStorage->clear();
		textureStorage->resize(count);
		bufferStorage->resize(count);

		for (UINT32 groupIndex = 0; groupIndex < count; ++groupIndex) {
			(*groupStorage)[groupIndex] = source[groupIndex];
			if (source[groupIndex].Type == D3D12_BARRIER_TYPE_TEXTURE &&
				source[groupIndex].pTextureBarriers &&
				source[groupIndex].NumBarriers) {
				std::vector<D3D12_TEXTURE_BARRIER> &textures = (*textureStorage)[groupIndex];
				textures.assign(
					source[groupIndex].pTextureBarriers,
					source[groupIndex].pTextureBarriers + source[groupIndex].NumBarriers);
				for (UINT32 barrierIndex = 0; barrierIndex < source[groupIndex].NumBarriers; ++barrierIndex)
					textures[barrierIndex].pResource =
						UnwrapResource(source[groupIndex].pTextureBarriers[barrierIndex].pResource);
				(*groupStorage)[groupIndex].pTextureBarriers = textures.data();
			} else if (source[groupIndex].Type == D3D12_BARRIER_TYPE_BUFFER &&
				source[groupIndex].pBufferBarriers &&
				source[groupIndex].NumBarriers) {
				std::vector<D3D12_BUFFER_BARRIER> &buffers = (*bufferStorage)[groupIndex];
				buffers.assign(
					source[groupIndex].pBufferBarriers,
					source[groupIndex].pBufferBarriers + source[groupIndex].NumBarriers);
				for (UINT32 barrierIndex = 0; barrierIndex < source[groupIndex].NumBarriers; ++barrierIndex)
					buffers[barrierIndex].pResource =
						UnwrapResource(source[groupIndex].pBufferBarriers[barrierIndex].pResource);
				(*groupStorage)[groupIndex].pBufferBarriers = buffers.data();
			}
		}
		*result = groupStorage->data();
	}

	static void STDMETHODCALLTYPE DirectQueueExecuteCommandLists(
		ID3D12CommandQueue *queue,
		UINT numCommandLists,
		ID3D12CommandList *const *commandLists)
	{
		if (queue)
			queue->ExecuteCommandLists(numCommandLists, commandLists);
	}

	static void ExecuteCommandListsDirectNative(
		ID3D12CommandQueue *queue,
		UINT numCommandLists,
		ID3D12CommandList *const *commandLists)
	{
		ID3D12CommandList *stackUnwrapped[8];
		std::vector<ID3D12CommandList*> heapUnwrapped;
		ID3D12CommandList *const *activeCommandLists = commandLists;
		if (commandLists && numCommandLists) {
			ID3D12CommandList **unwrapped = stackUnwrapped;
			if (numCommandLists > ARRAYSIZE(stackUnwrapped)) {
				heapUnwrapped.resize(numCommandLists);
				unwrapped = heapUnwrapped.data();
			}
			for (UINT i = 0; i < numCommandLists; ++i)
				unwrapped[i] = UnwrapCommandList(commandLists[i]);
			activeCommandLists = unwrapped;
		}
		DirectQueueExecuteCommandLists(queue, numCommandLists, activeCommandLists);
	}

	static void STDMETHODCALLTYPE DirectDrawInstanced(
		ID3D12GraphicsCommandList *commandList,
		UINT vertexCountPerInstance,
		UINT instanceCount,
		UINT startVertexLocation,
		UINT startInstanceLocation)
	{
		if (commandList)
			commandList->DrawInstanced(
				vertexCountPerInstance, instanceCount, startVertexLocation, startInstanceLocation);
	}

	static void STDMETHODCALLTYPE DirectDrawIndexedInstanced(
		ID3D12GraphicsCommandList *commandList,
		UINT indexCountPerInstance,
		UINT instanceCount,
		UINT startIndexLocation,
		INT baseVertexLocation,
		UINT startInstanceLocation)
	{
		if (commandList)
			commandList->DrawIndexedInstanced(
				indexCountPerInstance, instanceCount, startIndexLocation,
				baseVertexLocation, startInstanceLocation);
	}

	static void STDMETHODCALLTYPE DirectDispatch(
		ID3D12GraphicsCommandList *commandList,
		UINT threadGroupCountX,
		UINT threadGroupCountY,
		UINT threadGroupCountZ)
	{
		if (commandList)
			commandList->Dispatch(threadGroupCountX, threadGroupCountY, threadGroupCountZ);
	}

	static void STDMETHODCALLTYPE DirectExecuteIndirect(
		ID3D12GraphicsCommandList *commandList,
		ID3D12CommandSignature *commandSignature,
		UINT maxCommandCount,
		ID3D12Resource *argumentBuffer,
		UINT64 argumentBufferOffset,
		ID3D12Resource *countBuffer,
		UINT64 countBufferOffset)
	{
		if (commandList)
			commandList->ExecuteIndirect(
				commandSignature, maxCommandCount,
				argumentBuffer, argumentBufferOffset,
				countBuffer, countBufferOffset);
	}

	static void STDMETHODCALLTYPE DirectSetIndexBuffer(
		ID3D12GraphicsCommandList *commandList,
		const D3D12_INDEX_BUFFER_VIEW *view)
	{
		if (commandList)
			commandList->IASetIndexBuffer(view);
	}

	static void STDMETHODCALLTYPE DirectSetVertexBuffers(
		ID3D12GraphicsCommandList *commandList,
		UINT startSlot,
		UINT count,
		const D3D12_VERTEX_BUFFER_VIEW *views)
	{
		if (commandList)
			commandList->IASetVertexBuffers(startSlot, count, views);
	}

	static DX12IaReplacementExecutorCallbacks MakeDirectIaReplacementCallbacks()
	{
		DX12IaReplacementExecutorCallbacks callbacks;
		callbacks.drawInstanced = DirectDrawInstanced;
		callbacks.drawIndexedInstanced = DirectDrawIndexedInstanced;
		callbacks.dispatch = DirectDispatch;
		callbacks.setIndexBuffer = DirectSetIndexBuffer;
		callbacks.setVertexBuffers = DirectSetVertexBuffers;
		callbacks.recordReplacementDraw = DX12CommandListHookDiagnostics::RecordIaReplacementDrawExecute;
		callbacks.recordReplacementDispatch = DX12CommandListHookDiagnostics::RecordIaReplacementDispatchExecute;
		return callbacks;
	}


	static void RegisterSubmittedCommandListDirect(IUnknown *commandList)
	{
		if (!commandList)
			return;
		const bool huntEnabled = DX12HuntIsEnabled();
		const bool captureActive =
			DX12CaptureLedger::CurrentCaptureSessionId() != 0;
		if (!huntEnabled && !captureActive)
			return;

		// Post-submit callback: ID3D12CommandQueue::ExecuteCommandLists has
		// already handed these command lists to the GPU. Recording into,
		// re-barriering, or patching a submitted command list is invalid
		// D3D12 usage - it can hang the driver or trip D3D12Core's
		// InvalidResourceBarrierCall fatal path (observed as a white-screen
		// startup hang in ZenlessZoneZero). This callback must stay pure
		// CPU bookkeeping: no D3D12/COM calls, no locks held across waits.
		ID3D12GraphicsCommandList *baseList =
			reinterpret_cast<ID3D12GraphicsCommandList*>(commandList);
		if (huntEnabled)
			DX12HuntPublishCommandList(baseList);
		if (captureActive)
			DX12BindingTracker::SubmitCommandList(baseList);
	}

	static HRESULT PresentDirect(
		IDXGISwapChain *swapChain,
		UINT syncInterval,
		UINT flags)
	{
		DX12_PROFILE_FUNCTION(Present);
		if (!swapChain)
			return DXGI_ERROR_INVALID_CALL;
		if (DX12InternalReplayState::IsActive()) {
			_profile_timer.Pause();
			return swapChain->Present(syncInterval, flags);
		}
		if (flags & DXGI_PRESENT_TEST) {
			_profile_timer.Pause();
			return swapChain->Present(syncInterval, flags);
		}
		if (!DXGIHookRuntime::IsPrimarySwapChain("PresentWrapper", swapChain)) {
			_profile_timer.Pause();
			return swapChain->Present(syncInterval, flags);
		}
		DX12ResourceDumpGpuCopy::DrainRetiredCopies();

		DX12OuterTimingProbeUtils::LogPresentEnter("PresentWrapper", swapChain, syncInterval, flags);
		LARGE_INTEGER sliceBegin = DX12OuterTimingProbeUtils::Now();
		DXGIHookRuntime::ConsumeUiActionsForFrame("PresentWrapper", swapChain);

		static volatile LONG firstPresentLog = 0;
		if (InterlockedCompareExchange(&firstPresentLog, 1, 0) == 0)
			DX12LogManager::FieldsFlush("DX12PresentFirst",
				"\"api\":\"Present\",\"stage\":\"wrapper_enter\",\"swapchain\":\"%p\",\"sync\":%u,\"flags\":\"0x%x\",\"skipAll\":%ld",
				swapChain, syncInterval, flags, DX12HotPathState::skipAll.load());

		// Deferred mod load must run on EVERY present, including the skipAll
		// fast path. When no mod is loaded skipAll==1, so the call cannot live
		// in the normal branch below - it would never execute and the mod would
		// never load (deadlock between skipAll and the deferred load).
		DX12ModMaybeRunDeferredInitialLoad();

		if (DX12HotPathState::skipAll) {
			sliceBegin = DX12OuterTimingProbeUtils::Now();
			_profile_timer.Pause();
			HRESULT skipHr = swapChain->Present(syncInterval, flags);
			_profile_timer.Resume();
			DX12OuterTimingProbeUtils::LogPresentSlice(
				"PresentWrapper", "original_present_skip", swapChain, skipHr,
				DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
			sliceBegin = DX12OuterTimingProbeUtils::Now();
			DX12RuntimeState::IncrementPresentCount();
			DXGIHookRuntime::RunPostPresentRuntime(
				"PresentWrapper",
				swapChain,
				skipHr,
				DX12RuntimeState::GetPresentCount() > 2);
			DX12HotPathState::Update();
			DX12_HOTPATH_LOG_AND_RESET();
			DX12OuterTimingProbeUtils::LogPresentSlice(
				"PresentWrapper", "post_original_skip", swapChain, skipHr,
				DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
			DX12OuterTimingProbeUtils::LogPresentLeave("PresentWrapper", swapChain, skipHr);
			return skipHr;
		}

		DX12_HOTPATH_TIME_BEGIN(PresentTotal);

		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DXGIHookRuntime::LogAndResetPerFrameCounters();
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"PresentWrapper", "per_frame_counters", swapChain, S_OK,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));

		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DX12Profiling::BeginFrame();
		const bool dumpFrame = DX12FrameAnalysis::EndCapture();
		if (DX12FrameAnalysis::IsCapturing())
			DX12FrameAnalysis::LogJsonFunc("FrameCaptureContinue",
				"\"present\":%ld", DX12RuntimeState::GetPresentCount());
		const bool dumpShaders = DX12ShaderDumpEndCapture();
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"PresentWrapper", "capture_bookkeeping", swapChain, S_OK,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
				LARGE_INTEGER presentBegin = DX12OuterTimingProbeUtils::Now();
		_profile_timer.Pause();
		HRESULT hr = swapChain->Present(syncInterval, flags);
		_profile_timer.Resume();
		const LONG64 presentUs = DX12OuterTimingProbeUtils::ElapsedUs(presentBegin);
	#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
		DX12LogManager::Fields("DX12Perf",
			"\"scope\":\"PresentWait\",\"us\":%lld",
			static_cast<long long>(presentUs));
	#endif
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"PresentWrapper", "original_present", swapChain, hr,
			presentUs);
						if (FAILED(hr)) {
			_profile_timer.Finish();
			DX12_HOTPATH_TIME_END(PresentTotal);
			DX12Profiler::FlushFrame();
			DX12OuterTimingProbeUtils::LogPresentLeave("PresentWrapper", swapChain, hr);
			return hr;
		}
		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DX12RuntimeState::IncrementPresentCount();
		DXGIHookRuntime::RunPostPresentRuntime(
			"PresentWrapper", swapChain, hr, true);
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"PresentWrapper", "post_original_runtime", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));

				sliceBegin = DX12OuterTimingProbeUtils::Now();
		DX12ModBeginFrame(swapChain);
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"PresentWrapper", "mod_begin_frame", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));

		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DXGIHookRuntime::CompleteCaptureOrBeginNext("Present", swapChain, hr, dumpFrame, dumpShaders);
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"PresentWrapper", "capture_complete_or_begin", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
		sliceBegin = DX12OuterTimingProbeUtils::Now();
		_profile_timer.Finish();
		DX12_HOTPATH_TIME_END(PresentTotal);
		DX12Profiler::FlushFrame();
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"PresentWrapper", "profiling_and_hotpath_flush", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
				DX12OuterTimingProbeUtils::LogPresentLeave("PresentWrapper", swapChain, hr);
		return hr;
	}

	static HRESULT Present1Direct(
		IDXGISwapChain1 *swapChain,
		UINT syncInterval,
		UINT flags,
		const DXGI_PRESENT_PARAMETERS *presentParameters)
	{
		DX12_PROFILE_FUNCTION(Present);
		if (!swapChain)
			return DXGI_ERROR_INVALID_CALL;
		if (DX12InternalReplayState::IsActive()) {
			_profile_timer.Pause();
			return swapChain->Present1(syncInterval, flags, presentParameters);
		}
		if (flags & DXGI_PRESENT_TEST) {
			_profile_timer.Pause();
			return swapChain->Present1(syncInterval, flags, presentParameters);
		}
		if (!DXGIHookRuntime::IsPrimarySwapChain("Present1Wrapper", swapChain)) {
			_profile_timer.Pause();
			return swapChain->Present1(syncInterval, flags, presentParameters);
		}
		DX12ResourceDumpGpuCopy::DrainRetiredCopies();

		DX12OuterTimingProbeUtils::LogPresentEnter("Present1Wrapper", swapChain, syncInterval, flags);
		LARGE_INTEGER sliceBegin = DX12OuterTimingProbeUtils::Now();
		DXGIHookRuntime::ConsumeUiActionsForFrame("Present1Wrapper", swapChain);

		static volatile LONG firstPresent1Log = 0;
		if (InterlockedCompareExchange(&firstPresent1Log, 1, 0) == 0)
			DX12LogManager::FieldsFlush("DX12PresentFirst",
				"\"api\":\"Present1\",\"stage\":\"wrapper_enter\",\"swapchain\":\"%p\",\"sync\":%u,\"flags\":\"0x%x\",\"skipAll\":%ld",
				swapChain, syncInterval, flags, DX12HotPathState::skipAll.load());

		// Deferred mod load must run on EVERY present, including the skipAll
		// fast path. When no mod is loaded skipAll==1, so the call cannot live
		// in the normal branch below - it would never execute and the mod would
		// never load (deadlock between skipAll and the deferred load).
		DX12ModMaybeRunDeferredInitialLoad();

		if (DX12HotPathState::skipAll) {
			sliceBegin = DX12OuterTimingProbeUtils::Now();
			_profile_timer.Pause();
			HRESULT skipHr = swapChain->Present1(syncInterval, flags, presentParameters);
			_profile_timer.Resume();
			DX12OuterTimingProbeUtils::LogPresentSlice(
				"Present1Wrapper", "original_present_skip", swapChain, skipHr,
				DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
			sliceBegin = DX12OuterTimingProbeUtils::Now();
			DX12RuntimeState::IncrementPresentCount();
			DXGIHookRuntime::RunPostPresentRuntime(
				"Present1Wrapper",
				swapChain,
				skipHr,
				DX12RuntimeState::GetPresentCount() > 2);
			DX12HotPathState::Update();
			DX12_HOTPATH_LOG_AND_RESET();
			DX12OuterTimingProbeUtils::LogPresentSlice(
				"Present1Wrapper", "post_original_skip", swapChain, skipHr,
				DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
			DX12OuterTimingProbeUtils::LogPresentLeave("Present1Wrapper", swapChain, skipHr);
			return skipHr;
		}


		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DXGIHookRuntime::LogAndResetPerFrameCounters();
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present1Wrapper", "per_frame_counters", swapChain, S_OK,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));

		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DX12Profiling::BeginFrame();
		const bool dumpFrame = DX12FrameAnalysis::EndCapture();
		if (DX12FrameAnalysis::IsCapturing())
			DX12FrameAnalysis::LogJsonFunc("FrameCaptureContinue",
				"\"present\":%ld", DX12RuntimeState::GetPresentCount());
		const bool dumpShaders = DX12ShaderDumpEndCapture();
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present1Wrapper", "capture_bookkeeping", swapChain, S_OK,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
				LARGE_INTEGER presentBegin = DX12OuterTimingProbeUtils::Now();
		_profile_timer.Pause();
		HRESULT hr = swapChain->Present1(syncInterval, flags, presentParameters);
		_profile_timer.Resume();
		const LONG64 presentUs = DX12OuterTimingProbeUtils::ElapsedUs(presentBegin);
	#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
		DX12LogManager::Fields("DX12Perf",
			"\"scope\":\"PresentWait\",\"us\":%lld",
			static_cast<long long>(presentUs));
	#endif
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present1Wrapper", "original_present", swapChain, hr,
			presentUs);
						if (FAILED(hr)) {
			_profile_timer.Finish();
			DX12Profiler::FlushFrame();
			DX12OuterTimingProbeUtils::LogPresentLeave("Present1Wrapper", swapChain, hr);
			return hr;
		}
		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DX12RuntimeState::IncrementPresentCount();
		DXGIHookRuntime::RunPostPresentRuntime(
			"Present1Wrapper", swapChain, hr, true);
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present1Wrapper", "post_original_runtime", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));

				sliceBegin = DX12OuterTimingProbeUtils::Now();
		DX12ModBeginFrame(swapChain);
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present1Wrapper", "mod_begin_frame", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));

		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DXGIHookRuntime::CompleteCaptureOrBeginNext("Present1", swapChain, hr, dumpFrame, dumpShaders);
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present1Wrapper", "capture_complete_or_begin", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
		sliceBegin = DX12OuterTimingProbeUtils::Now();
		_profile_timer.Finish();
		DX12Profiler::FlushFrame();
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present1Wrapper", "profiling_and_hotpath_flush", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
				DX12OuterTimingProbeUtils::LogPresentLeave("Present1Wrapper", swapChain, hr);
		return hr;
	}

	static HRESULT Present(
		IDXGISwapChain *swapChain,
		UINT syncInterval,
		UINT flags)
	{
		return PresentDirect(swapChain, syncInterval, flags);
	}

	static HRESULT Present1(
		IDXGISwapChain1 *swapChain,
		UINT syncInterval,
		UINT flags,
		const DXGI_PRESENT_PARAMETERS *presentParameters)
	{
		return Present1Direct(swapChain, syncInterval, flags, presentParameters);
	}

	static HRESULT ResizeBuffers(
		IDXGISwapChain *swapChain,
		UINT bufferCount,
		UINT width,
		UINT height,
		DXGI_FORMAT newFormat,
		UINT swapChainFlags)
	{
		if (!swapChain)
			return DXGI_ERROR_INVALID_CALL;
		if (!DX12InternalReplayState::IsActive())
			DX12NativeOverlayInvalidate(swapChain);
		HRESULT hr = swapChain->ResizeBuffers(bufferCount, width, height, newFormat, swapChainFlags);
		DX12LogManager::Fields("IDXGISwapChain::ResizeBuffers",
			"\"swapchain\":\"%p\",\"buffers\":%u,\"width\":%u,\"height\":%u,\"format\":%u,\"flags\":\"0x%x\",\"hr\":\"0x%lx\"",
			swapChain, bufferCount, width, height, static_cast<UINT>(newFormat), swapChainFlags, hr);
		return hr;
	}

	static void ExecuteCommandLists(
		ID3D12CommandQueue *queue,
		UINT numCommandLists,
		ID3D12CommandList *const *commandLists,
		DX12CommandListHookSignatures::QueueExecuteCommandLists original)
	{
#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
		// One QPC pair feeds both the API wall bucket and the ExecuteCL hot-path
		// stage (previously two independent timers measured the same span).
		DX12Profiler::Scope apiWallScope(
			DX12Profiler::ApiStage,
			DX12OuterTimingProbeUtils::ApiExecuteCommandLists,
			static_cast<int>(DX12HotPathStage::ExecuteCLTotal));
#else
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiExecuteCommandLists);
#endif
		LARGE_INTEGER totalBegin = DX12OuterTimingProbeUtils::Now();
		LARGE_INTEGER unwrapBegin = DX12OuterTimingProbeUtils::Now();
		// Use the stack for up to eight command lists, then fall back to the heap.
		ID3D12CommandList *stackUnwrapped[8];
		ID3D12CommandList *const *activeCommandLists = commandLists;
		std::vector<ID3D12CommandList*> heapUnwrapped;
		if (commandLists && numCommandLists) {
			ID3D12CommandList **dest = stackUnwrapped;
			if (numCommandLists > 8) {
				heapUnwrapped.resize(numCommandLists);
				dest = heapUnwrapped.data();
			}
			for (UINT i = 0; i < numCommandLists; ++i)
				dest[i] = UnwrapCommandList(commandLists[i]);
			activeCommandLists = dest;
		}
		DX12OuterTimingProbeUtils::LogQueueExecuteSlice(
			"unwrap_command_lists",
			queue,
			numCommandLists,
			DX12OuterTimingProbeUtils::ElapsedUs(unwrapBegin));
		DX12CommandQueueExecutionFlow::Execute(
			queue,
			numCommandLists,
			activeCommandLists,
			original ? original : DirectQueueExecuteCommandLists,
			RegisterSubmittedCommandListDirect);
		DX12OuterTimingProbeUtils::LogQueueExecuteSlice(
			"wrapper_total",
			queue,
			numCommandLists,
			DX12OuterTimingProbeUtils::ElapsedUs(totalBegin));
	}

	static void DrawInstanced(
		ID3D12GraphicsCommandList *commandList,
		UINT vertexCountPerInstance,
		UINT instanceCount,
		UINT startVertexLocation,
		UINT startInstanceLocation,
		DX12CommandListHookSignatures::DrawInstanced original,
		UINT64 drawFlags)
	{
		DX12SimulateContext *runtimeState = nullptr;
		if (drawFlags &
			(DX12HotPathState::MASK_DRAW_WORK |
				DX12HotPathState::FLAG_PRESENT_REPLACEMENT))
			runtimeState = DX12CommandListRuntime::GetStatePtrFast(commandList);
		if ((drawFlags &
				DX12HotPathState::FLAG_PRESENT_REPLACEMENT) &&
			DX12ModPresentReplacementPendingForCurrentPresent()) {
			const DX12IaReplacementExecutorCallbacks callbacks =
				MakeDirectIaReplacementCallbacks();
			DX12IaReplacementExecutor::ExecutePendingPresentBeforeFastReject(
				commandList, runtimeState, callbacks, drawFlags);
		}
		DX12WrapperRuntimeUtils::DrawInstanced(
			commandList,
			runtimeState,
			vertexCountPerInstance,
			instanceCount,
			startVertexLocation,
			startInstanceLocation,
			original,
			drawFlags);
	}

	static void DrawInstanced(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		UINT vertexCountPerInstance,
		UINT instanceCount,
		UINT startVertexLocation,
		UINT startInstanceLocation,
		DX12CommandListHookSignatures::DrawInstanced original,
		UINT64 drawFlags)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiDrawInstanced);
		if (!(drawFlags & (DX12HotPathState::MASK_DRAW_WORK |
				DX12HotPathState::FLAG_PRESENT_REPLACEMENT |
				DX12HotPathState::FLAG_DRAWSRV_PENDING_RESTORE))) {
			DX12_PROFILE_FAST_FORWARD();
			if (original)
				original(commandList, vertexCountPerInstance, instanceCount,
					startVertexLocation, startInstanceLocation);
			else
				DirectDrawInstanced(commandList, vertexCountPerInstance, instanceCount,
					startVertexLocation, startInstanceLocation);
			return;
		}
		// Use pre-hook original as callback to bypass vtable hook.
		// Falls back to DirectDrawInstanced (hooked vtable) only if original is null.
		DX12IaReplacementExecutorCallbacks callbacks;
		callbacks.drawInstanced = original ? reinterpret_cast<DX12IaCommandSignatures::DrawInstanced>(original) : DirectDrawInstanced;
		callbacks.drawIndexedInstanced = DirectDrawIndexedInstanced;
		callbacks.dispatch = DirectDispatch;
		callbacks.setIndexBuffer = DirectSetIndexBuffer;
		callbacks.setVertexBuffers = DirectSetVertexBuffers;
		callbacks.recordReplacementDraw = DX12CommandListHookDiagnostics::RecordIaReplacementDrawExecute;
		callbacks.recordReplacementDispatch = DX12CommandListHookDiagnostics::RecordIaReplacementDispatchExecute;
		DX12IaDrawInvocation draw = DX12IaDrawInvocation::ForDrawInstanced(
			vertexCountPerInstance, instanceCount, startVertexLocation, startInstanceLocation);
		DX12DrawHookFlow::ExecuteHook(commandList, runtimeState, draw, callbacks, drawFlags);
	}

	static void DrawIndexedInstanced(
		ID3D12GraphicsCommandList *commandList,
		UINT indexCountPerInstance,
		UINT instanceCount,
		UINT startIndexLocation,
		INT baseVertexLocation,
		UINT startInstanceLocation,
		DX12CommandListHookSignatures::DrawIndexedInstanced original,
		UINT64 drawFlags)
	{
		DX12SimulateContext *runtimeState = nullptr;
		if (drawFlags &
			(DX12HotPathState::MASK_DRAW_WORK |
				DX12HotPathState::FLAG_PRESENT_REPLACEMENT))
			runtimeState = DX12CommandListRuntime::GetStatePtrFast(commandList);
		if ((drawFlags &
				DX12HotPathState::FLAG_PRESENT_REPLACEMENT) &&
			DX12ModPresentReplacementPendingForCurrentPresent()) {
			const DX12IaReplacementExecutorCallbacks callbacks =
				MakeDirectIaReplacementCallbacks();
			DX12IaReplacementExecutor::ExecutePendingPresentBeforeFastReject(
				commandList, runtimeState, callbacks, drawFlags);
		}
		DX12WrapperRuntimeUtils::DrawIndexedInstanced(
			commandList,
			runtimeState,
			indexCountPerInstance,
			instanceCount,
			startIndexLocation,
			baseVertexLocation,
			startInstanceLocation,
			original,
			drawFlags);
	}

	static void DrawIndexedInstanced(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		UINT indexCountPerInstance,
		UINT instanceCount,
		UINT startIndexLocation,
		INT baseVertexLocation,
		UINT startInstanceLocation,
		DX12CommandListHookSignatures::DrawIndexedInstanced original,
		UINT64 drawFlags)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiDrawIndexedInstanced);
		if (!(drawFlags & (DX12HotPathState::MASK_DRAW_WORK |
				DX12HotPathState::FLAG_PRESENT_REPLACEMENT |
				DX12HotPathState::FLAG_DRAWSRV_PENDING_RESTORE))) {
			DX12_PROFILE_FAST_FORWARD();
			if (original)
				original(commandList, indexCountPerInstance, instanceCount,
					startIndexLocation, baseVertexLocation, startInstanceLocation);
			else
				DirectDrawIndexedInstanced(commandList, indexCountPerInstance, instanceCount,
					startIndexLocation, baseVertexLocation, startInstanceLocation);
			return;
		}
		DX12IaReplacementExecutorCallbacks callbacks;
		callbacks.drawInstanced = DirectDrawInstanced;
		callbacks.drawIndexedInstanced = original ? reinterpret_cast<DX12IaCommandSignatures::DrawIndexedInstanced>(original) : DirectDrawIndexedInstanced;
		callbacks.dispatch = DirectDispatch;
		callbacks.setIndexBuffer = DirectSetIndexBuffer;
		callbacks.setVertexBuffers = DirectSetVertexBuffers;
		callbacks.recordReplacementDraw = DX12CommandListHookDiagnostics::RecordIaReplacementDrawExecute;
		callbacks.recordReplacementDispatch = DX12CommandListHookDiagnostics::RecordIaReplacementDispatchExecute;
		DX12IaDrawInvocation draw = DX12IaDrawInvocation::ForDrawIndexedInstanced(
			indexCountPerInstance, instanceCount, startIndexLocation, baseVertexLocation, startInstanceLocation);
		DX12DrawHookFlow::ExecuteHook(commandList, runtimeState, draw, callbacks, drawFlags);
	}

	static void Dispatch(
		ID3D12GraphicsCommandList *commandList,
		UINT threadGroupCountX,
		UINT threadGroupCountY,
		UINT threadGroupCountZ,
		DX12DispatchHookFlowDispatch::Fn original)
	{
		DX12SimulateContext *runtimeState = nullptr;
		const UINT64 dispatchFlags = DX12HotPathState::HookWorkFlags;
		if (dispatchFlags & DX12HotPathState::MASK_DISPATCH_WORK)
			runtimeState = DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12WrapperRuntimeUtils::Dispatch(
			commandList,
			runtimeState,
			threadGroupCountX,
			threadGroupCountY,
			threadGroupCountZ,
			original,
			dispatchFlags);
	}

	static void Dispatch(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		UINT threadGroupCountX,
		UINT threadGroupCountY,
		UINT threadGroupCountZ,
		DX12DispatchHookFlowDispatch::Fn original,
		UINT64 dispatchFlags)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiDispatch);
		// The wrapper already evaluated this same mask on its fast path; only
		// a null original (forward through DirectDispatch) can still take the
		// no-work branch here, so re-check with the flags it passed down
		// instead of loading the shared word a second time.
		if (!(dispatchFlags & (DX12HotPathState::MASK_DISPATCH_WORK |
				DX12HotPathState::FLAG_DRAWSRV_PENDING_RESTORE))) {
			DX12_PROFILE_FAST_FORWARD();
			if (original)
				original(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
			else
				DirectDispatch(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
			return;
		}
		DX12DispatchHookFlow::ExecuteHook(
			commandList,
			runtimeState,
			threadGroupCountX,
			threadGroupCountY,
			threadGroupCountZ,
			original ? original : DirectDispatch,
			dispatchFlags);
	}

	static void ExecuteIndirect(
		ID3D12GraphicsCommandList *commandList,
		ID3D12CommandSignature *commandSignature,
		UINT maxCommandCount,
		ID3D12Resource *argumentBuffer,
		UINT64 argumentBufferOffset,
		ID3D12Resource *countBuffer,
		UINT64 countBufferOffset,
		DX12CommandListHookSignatures::ExecuteIndirect original)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiExecuteIndirect);
		ID3D12Resource *unwrappedArgumentBuffer = UnwrapResource(argumentBuffer);
		ID3D12Resource *unwrappedCountBuffer = UnwrapResource(countBuffer);
		ID3D12CommandSignature *unwrappedCommandSignature = UnwrapCommandSignature(commandSignature);
		DX12ExecuteIndirectRuntime::Execute(
			commandList,
			unwrappedCommandSignature,
			maxCommandCount,
			unwrappedArgumentBuffer,
			argumentBufferOffset,
			unwrappedCountBuffer,
			countBufferOffset,
			original ? original : DirectExecuteIndirect);
	}

	static HRESULT QueueSignal(
		ID3D12CommandQueue *queue,
		ID3D12Fence *fence,
		UINT64 value,
		DX12CommandListHookSignatures::QueueSignal original)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiQueueSignal);
		ID3D12Fence *unwrappedFence = UnwrapFence(fence);
		const UINT64 completedBefore =
			DX12OuterTimingProbeUtils::TimingFenceCompletedValue(unwrappedFence);
		LARGE_INTEGER begin = DX12OuterTimingProbeUtils::Now();
		HRESULT hr = original ? original(queue, unwrappedFence, value) :
			queue->Signal(unwrappedFence, value);
		const UINT64 completedAfter =
			DX12OuterTimingProbeUtils::TimingFenceCompletedValue(unwrappedFence);
		DX12OuterTimingProbeUtils::LogQueueFenceSlice(
			"Signal",
			"original",
			queue,
			unwrappedFence,
			value,
			completedBefore,
			completedAfter,
			hr,
			DX12OuterTimingProbeUtils::ElapsedUs(begin));
		return hr;
	}

	static HRESULT QueueWait(
		ID3D12CommandQueue *queue,
		ID3D12Fence *fence,
		UINT64 value,
		DX12CommandListHookSignatures::QueueWait original)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiQueueWait);
		ID3D12Fence *unwrappedFence = UnwrapFence(fence);
		const UINT64 completedBefore =
			DX12OuterTimingProbeUtils::TimingFenceCompletedValue(unwrappedFence);
		LARGE_INTEGER begin = DX12OuterTimingProbeUtils::Now();
		HRESULT hr = original ? original(queue, unwrappedFence, value) :
			queue->Wait(unwrappedFence, value);
		const UINT64 completedAfter =
			DX12OuterTimingProbeUtils::TimingFenceCompletedValue(unwrappedFence);
		DX12OuterTimingProbeUtils::LogQueueFenceSlice(
			"Wait",
			"original",
			queue,
			unwrappedFence,
			value,
			completedBefore,
			completedAfter,
			hr,
			DX12OuterTimingProbeUtils::ElapsedUs(begin));
		return hr;
	}

	static HRESULT ResetCommandList(
		ID3D12GraphicsCommandList *commandList,
		ID3D12CommandAllocator *allocator,
		ID3D12PipelineState *initialState,
		DX12CommandListHookSignatures::ResetCommandList original)
	{
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		return DX12WrapperRuntimeUtils::ResetCommandList(
			commandList, runtimeState, allocator, initialState, original);
	}

	static HRESULT ResetCommandList(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		ID3D12CommandAllocator *allocator,
		ID3D12PipelineState *initialState,
		DX12CommandListHookSignatures::ResetCommandList original)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiResetCommandList);
		ID3D12CommandAllocator *unwrappedAllocator = UnwrapCommandAllocator(allocator);
		ID3D12PipelineState *unwrappedInitialState = UnwrapPipelineState(initialState);
		HRESULT hr = original ?
			original(commandList, unwrappedAllocator, unwrappedInitialState) :
			commandList->Reset(unwrappedAllocator, unwrappedInitialState);
		if (SUCCEEDED(hr)) {
			if (runtimeState)
				DX12CommandListRuntime::Reset(runtimeState, unwrappedInitialState);
			else
				DX12CommandListRuntime::Reset(commandList, unwrappedInitialState);
			DX12CommandListLifecycle::Reset(commandList, unwrappedInitialState);
		}
		return hr;
	}

	static HRESULT CloseCommandList(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListHookSignatures::CloseCommandList original)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiCloseCommandList);
		return original ? original(commandList) : commandList->Close();
	}

	static void SetPipelineState(
		ID3D12GraphicsCommandList *commandList,
		ID3D12PipelineState *pipelineState,
		DX12CommandListHookSignatures::SetPipelineState original)
	{
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12WrapperRuntimeUtils::SetPipelineState(
			commandList, runtimeState, pipelineState, original);
	}

	static void SetPipelineState(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		ID3D12PipelineState *pipelineState,
		DX12CommandListHookSignatures::SetPipelineState original)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetPipelineState);
		DX12_HOTPATH_TIME_BEGIN(SetPipelineStateTotal);
		ID3D12PipelineState *unwrappedPipelineState = nullptr;
		if (runtimeState && runtimeState->lastWrappedPipelineState == pipelineState) {
			unwrappedPipelineState = runtimeState->lastUnwrappedPipelineState;
		} else {
			unwrappedPipelineState = UnwrapPipelineState(pipelineState);
			if (runtimeState) {
				runtimeState->lastWrappedPipelineState = pipelineState;
				runtimeState->lastUnwrappedPipelineState = unwrappedPipelineState;
			}
		}
		ID3D12PipelineState *activePipelineState =
			DX12CommandListStateFlow::CapturePipelineState(
					commandList, runtimeState, unwrappedPipelineState);
		if (original)
			original(commandList, activePipelineState);
		else
			commandList->SetPipelineState(activePipelineState);
		DX12_HOTPATH_TIME_END(SetPipelineStateTotal);
	}

	static void ResourceBarrier(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		UINT numBarriers,
		const D3D12_RESOURCE_BARRIER *barriers,
		DX12CommandListHookSignatures::ResourceBarrier original)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiResourceBarrier);
		DX12_HOTPATH_TIME_BEGIN(ResourceBarrierTotal);
		const D3D12_RESOURCE_BARRIER *activeBarriers = barriers;
		{
			std::vector<D3D12_RESOURCE_BARRIER> localBarriers;
			std::vector<D3D12_RESOURCE_BARRIER> &unwrappedBarriers =
				runtimeState ? runtimeState->barrierScratch : localBarriers;
			DX12_HOTPATH_TIME_BEGIN(BarrierUnwrap);
			UnwrapResourceBarrierArray(barriers, numBarriers, &unwrappedBarriers, &activeBarriers);
			DX12_HOTPATH_TIME_END(BarrierUnwrap);
		}
		DX12_HOTPATH_TIME_BEGIN(BarrierCapture);
		DX12CommandListStateFlow::CaptureResourceBarrier(commandList, numBarriers, activeBarriers);
		DX12_HOTPATH_TIME_END(BarrierCapture);
		if (original)
			original(commandList, numBarriers, activeBarriers);
		else
			commandList->ResourceBarrier(numBarriers, activeBarriers);
		DX12_HOTPATH_TIME_END(ResourceBarrierTotal);
	}

	// Bind-time static IA replacement (plan step 3.2). Hashes the original
	// bound view; an unconditional static geometry override redirects the
	// bind to the mod buffer so the draw path never re-evaluates it. The
	// bookkeeping on DX12ActiveIaState lets the draw fast-path detect a
	// rewritten or recreated game buffer and restore the original binding.
	static bool TryRedirectStaticIaIndexBuffer(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		const D3D12_INDEX_BUFFER_VIEW *view,
		D3D12_INDEX_BUFFER_VIEW *redirectedView)
	{
		if (!redirectedView)
			return false;
		// No table active: single volatile load on the bind path.
		if (!DX12HasStaticIaReplacements())
			return false;
		if (!view || !view->BufferLocation || !view->SizeInBytes)
			return false;
		ID3D12Device *device = runtimeState ? runtimeState->device : nullptr;
		ID3D12Device *queriedDevice = nullptr;
		if (!device && commandList &&
		    SUCCEEDED(commandList->GetDevice(IID_PPV_ARGS(&queriedDevice)))) {
			device = queriedDevice;
		}
		const uint32_t hash = DX12HashIaBufferContentView(
			view->BufferLocation, view->SizeInBytes, 0,
			static_cast<UINT>(view->Format), 0);
		bool redirected = false;
		if (hash && device) {
			redirected = DX12ResolveStaticIaReplacement(
				device, true, 0, hash, view, redirectedView, nullptr, nullptr);
			// Resource-level fallback: DX11 matches buffers by the full
			// resource hash (view-size independent). DX12's hash includes the
			// bound view size, so a scene switch that changes the bound view
			// range of the same buffer silently loses the replacement. Retry
			// with the full-resource extent so mods marked with the
			// resource-level hash keep matching under any view range.
			if (!redirected) {
				DX12BufferResourceSummary summary = {};
				if (DX12ResolveBufferResourceByGpuVa(
						view->BufferLocation, view->SizeInBytes, &summary) &&
				    summary.resource && summary.hasResourceDesc &&
				    summary.resourceDesc.Dimension ==
					    D3D12_RESOURCE_DIMENSION_BUFFER &&
				    summary.resourceDesc.Width > summary.resourceOffset) {
					const UINT64 fullBytes =
						summary.resourceDesc.Width - summary.resourceOffset;
					if (fullBytes != view->SizeInBytes) {
						const uint32_t resourceHash =
							DX12HashIaBufferContentView(
								view->BufferLocation, fullBytes, 0,
								static_cast<UINT>(view->Format), 0);
						if (resourceHash && resourceHash != hash) {
							redirected = DX12ResolveStaticIaReplacement(
								device, true, 0, resourceHash, view,
								redirectedView, nullptr, nullptr);
						}
					}
				}
			}
		}
		if (queriedDevice)
			queriedDevice->Release();
		if (runtimeState) {
			DX12ActiveIaState &ia = runtimeState->ia;
			if (redirected) {
				ia.indexBufferStaticReplaced = true;
				ia.indexBufferStaticOriginal = *view;
				ia.indexBufferStaticReplacedView = *redirectedView;
				ia.indexBufferStaticOriginalResolved =
					DX12ResolveBufferResourceByGpuVa(
						view->BufferLocation, view->SizeInBytes,
						&ia.indexBufferStaticOriginalResource);
				ia.indexBufferStaticContentGeneration =
					ia.indexBufferStaticOriginalResolved ?
						DX12GetBufferContentGenerationForResource(
							ia.indexBufferStaticOriginalResource.resource) : 0;
				ia.indexBufferStaticResolveEpoch =
					DX12GetIaHashResolveGeneration();
				ia.indexBufferStaticMatchedHash = hash;
			} else if (hash && ia.indexBufferStaticReplaced) {
				// The bind was evaluated (hash succeeded) and the table has no
				// replacement for it: drop stale bookkeeping from an earlier
				// redirected binding.
				ia.indexBufferStaticReplaced = false;
			}
		}
		return redirected;
	}

	static bool TryRedirectStaticIaVertexBuffers(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		UINT startSlot,
		UINT count,
		const D3D12_VERTEX_BUFFER_VIEW *views,
		D3D12_VERTEX_BUFFER_VIEW *redirectedViews,
		UINT redirectedCapacity)
	{
		if (!DX12HasStaticIaReplacements() || !views || !count)
			return false;
		if (startSlot >= 32 || count > 32 - startSlot ||
		    count > redirectedCapacity)
			return false;
		ID3D12Device *device = runtimeState ? runtimeState->device : nullptr;
		ID3D12Device *queriedDevice = nullptr;
		if (!device && commandList &&
		    SUCCEEDED(commandList->GetDevice(IID_PPV_ARGS(&queriedDevice)))) {
			device = queriedDevice;
		}
		bool redirectedAny = false;
		for (UINT index = 0; index < count; ++index) {
			const UINT slot = startSlot + index;
			const D3D12_VERTEX_BUFFER_VIEW &view = views[index];
			redirectedViews[index] = view;
			if (!view.BufferLocation || !view.SizeInBytes ||
			    !view.StrideInBytes) {
				if (runtimeState) {
					runtimeState->ia.vertexBufferStaticReplaced[slot] = false;
					runtimeState->ia.vertexBufferStaticReplacedMask &= ~(1u << slot);
				}
				continue;
			}
			const uint32_t hash = DX12HashIaBufferContentView(
				view.BufferLocation, view.SizeInBytes,
				view.StrideInBytes, 0, slot);
			bool redirected = false;
			if (hash && device) {
				redirected = DX12ResolveStaticIaReplacement(
					device, false, slot, hash,
					nullptr, nullptr, &view, &redirectedViews[index]);
				// Resource-level fallback (see TryRedirectStaticIaIndexBuffer):
				// the hash includes the bound view size; retry with the full
				// resource extent so a scene-switch view-range change of the
				// same buffer keeps matching resource-level mod hashes.
				if (!redirected) {
					DX12BufferResourceSummary summary = {};
					if (DX12ResolveBufferResourceByGpuVa(
							view.BufferLocation, view.SizeInBytes, &summary) &&
					    summary.resource && summary.hasResourceDesc &&
					    summary.resourceDesc.Dimension ==
						    D3D12_RESOURCE_DIMENSION_BUFFER &&
					    summary.resourceDesc.Width > summary.resourceOffset) {
						const UINT64 fullBytes =
							summary.resourceDesc.Width - summary.resourceOffset;
						if (fullBytes != view.SizeInBytes) {
							const uint32_t resourceHash =
								DX12HashIaBufferContentView(
									view.BufferLocation, fullBytes,
									view.StrideInBytes, 0, slot);
							if (resourceHash && resourceHash != hash) {
								redirected = DX12ResolveStaticIaReplacement(
									device, false, slot, resourceHash,
									nullptr, nullptr, &view,
									&redirectedViews[index]);
							}
						}
					}
				}
			}
			if (!redirected)
				redirectedViews[index] = view;
			if (runtimeState) {
				DX12ActiveIaState &ia = runtimeState->ia;
				if (redirected) {
					ia.vertexBufferStaticReplaced[slot] = true;
					ia.vertexBufferStaticReplacedMask |= (1u << slot);
					ia.vertexBufferStaticOriginal[slot] = view;
					ia.vertexBufferStaticReplacedView[slot] = redirectedViews[index];
					ia.vertexBufferStaticOriginalResolved[slot] =
						DX12ResolveBufferResourceByGpuVa(
							view.BufferLocation, view.SizeInBytes,
							&ia.vertexBufferStaticOriginalResources[slot]);
					ia.vertexBufferStaticContentGeneration[slot] =
						ia.vertexBufferStaticOriginalResolved[slot] ?
							DX12GetBufferContentGenerationForResource(
								ia.vertexBufferStaticOriginalResources[slot].resource) : 0;
					ia.vertexBufferStaticResolveEpoch[slot] =
						DX12GetIaHashResolveGeneration();
					ia.vertexBufferStaticMatchedHash[slot] = hash;
				} else if (hash && ia.vertexBufferStaticReplaced[slot]) {
					ia.vertexBufferStaticReplaced[slot] = false;
					ia.vertexBufferStaticReplacedMask &= ~(1u << slot);
				}
			}
			redirectedAny = redirectedAny || redirected;
		}
		if (queriedDevice)
			queriedDevice->Release();
		return redirectedAny;
	}

	static void IASetIndexBuffer(
		ID3D12GraphicsCommandList *commandList,
		const D3D12_INDEX_BUFFER_VIEW *view,
		DX12CommandListHookSignatures::IaSetIndexBuffer original)
	{
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12WrapperRuntimeUtils::IASetIndexBuffer(commandList, runtimeState, view, original);
	}

	static void IASetIndexBuffer(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		const D3D12_INDEX_BUFFER_VIEW *view,
		DX12CommandListHookSignatures::IaSetIndexBuffer original)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiIASetIndexBuffer);
		// Plan fix A: a game rebind supersedes a live static-template IA
		// replacement. Restore the snapshotted original views first so slots
		// this call does not cover do not keep the mod view.
		if (runtimeState && runtimeState->staticTemplateIaApplied) {
			DX12IaReplacementExecutorCallbacks callbacks;
			callbacks.setIndexBuffer = DirectSetIndexBuffer;
			callbacks.setVertexBuffers = DirectSetVertexBuffers;
			DX12IaReplacementExecutor::RestoreStaticTemplateIa(
				commandList, runtimeState, callbacks);
		}
		D3D12_INDEX_BUFFER_VIEW redirectedView = {};
		const D3D12_INDEX_BUFFER_VIEW *activeView = view;
		if (TryRedirectStaticIaIndexBuffer(
			    commandList, runtimeState, view, &redirectedView)) {
			activeView = &redirectedView;
		}
		const bool changed = DX12CommandListStateFlow::CaptureIndexBuffer(
			commandList, runtimeState, activeView);
		if (!changed)
			return;
		if (original)
			original(commandList, activeView);
		else
			commandList->IASetIndexBuffer(activeView);
	}

	// Bind-path split probe (IASetVertexBuffers runs 1500+/frame): pin the
	// per-call cost to one phase. Rate-limited; slow calls log always.
	static void LogIaBindProbe(
		LONG64 restoreUs, LONG64 staticUs, LONG64 preSkinUs,
		LONG64 captureUs, LONG64 originalUs, UINT startSlot, UINT count)
	{
#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
		static volatile LONG iaBindProbeCounter = 0;
		const LONG64 totalUs =
			restoreUs + staticUs + preSkinUs + captureUs + originalUs;
		if (totalUs >= 50 || (InterlockedIncrement(&iaBindProbeCounter) & 0x7F) == 1) {
			DX12LogManager::Fields("DX12IaBindProbe",
				"restoreUs=%lld,staticUs=%lld,preSkinUs=%lld,"
				"captureUs=%lld,originalUs=%lld,slot=%u,count=%u,totalUs=%lld",
				static_cast<long long>(restoreUs),
				static_cast<long long>(staticUs),
				static_cast<long long>(preSkinUs),
				static_cast<long long>(captureUs),
				static_cast<long long>(originalUs),
				startSlot, count,
				static_cast<long long>(totalUs));
		}
#else
		(void)restoreUs;
		(void)staticUs;
		(void)preSkinUs;
		(void)captureUs;
		(void)originalUs;
		(void)startSlot;
		(void)count;
#endif
	}

	static void IASetVertexBuffers(
		ID3D12GraphicsCommandList *commandList,
		UINT startSlot,
		UINT count,
		const D3D12_VERTEX_BUFFER_VIEW *views,
		DX12CommandListHookSignatures::IaSetVertexBuffers original)
	{
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12WrapperRuntimeUtils::IASetVertexBuffers(
			commandList, runtimeState, startSlot, count, views, original);
	}

	static void IASetVertexBuffers(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		UINT startSlot,
		UINT count,
		const D3D12_VERTEX_BUFFER_VIEW *views,
		DX12CommandListHookSignatures::IaSetVertexBuffers original)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiIASetVertexBuffers);
		// Bind-path split probe: the game binds 1500+ times per frame; pin
		// the per-call cost to one phase. Rate-limited + slow-capturing.
		LARGE_INTEGER bindProbeBegin = DX12OuterTimingProbeUtils::Now();
		LONG64 bindRestoreUs = 0;
		LONG64 bindStaticUs = 0;
		LONG64 bindPreSkinUs = 0;
		LONG64 bindCaptureUs = 0;
		LONG64 bindOriginalUs = 0;
		// Plan fix A: a game rebind supersedes a live static-template IA
		// replacement. Restore the snapshotted original views first so slots
		// this call does not cover do not keep the mod view.
		if (runtimeState && runtimeState->staticTemplateIaApplied) {
			DX12IaReplacementExecutorCallbacks callbacks;
			callbacks.setIndexBuffer = DirectSetIndexBuffer;
			callbacks.setVertexBuffers = DirectSetVertexBuffers;
			DX12IaReplacementExecutor::RestoreStaticTemplateIa(
				commandList, runtimeState, callbacks);
			bindRestoreUs = DX12OuterTimingProbeUtils::ElapsedUs(bindProbeBegin);
			bindProbeBegin = DX12OuterTimingProbeUtils::Now();
		}
		// When neither bind-time static IA replacement nor PreSkin output
		// resizing is active, the redirect probes cannot produce a different
		// view. Keep the DX12 resident IA capture, but avoid constructing two
		// full temporary view arrays and walking both redirect pipelines.
		if (!DX12HasStaticIaReplacements() &&
			!DX12ModHasPreSkinOutputResizeBindings()) {
			const bool changed = DX12CommandListStateFlow::CaptureVertexBuffers(
				commandList, runtimeState, startSlot, count, views);
			bindCaptureUs = DX12OuterTimingProbeUtils::ElapsedUs(bindProbeBegin);
			if (!changed)
				return;
			bindProbeBegin = DX12OuterTimingProbeUtils::Now();
			if (original)
				original(commandList, startSlot, count, views);
			else
				commandList->IASetVertexBuffers(startSlot, count, views);
			bindOriginalUs = DX12OuterTimingProbeUtils::ElapsedUs(bindProbeBegin);
			LogIaBindProbe(bindRestoreUs, bindStaticUs, bindPreSkinUs,
				bindCaptureUs, bindOriginalUs, startSlot, count);
			return;
		}
		D3D12_VERTEX_BUFFER_VIEW redirectedViews[
			D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
		D3D12_VERTEX_BUFFER_VIEW staticRedirectedViews[
			D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
		const D3D12_VERTEX_BUFFER_VIEW *activeViews = views;
		// Plan step 3.2: bind-time static IA replacement runs FIRST and per
		// slot. The PreSkin redirect below must not gate it: a character draw
		// binds the skinned positions (slot 0, pre-skin redirected) together
		// with the static texcoords (slot 1, content-hash replaced). When the
		// PreSkin VA map is already populated at bind time, the old
		// all-or-nothing ordering skipped the static replacement for EVERY
		// slot, so the draw read the game's small original texcoord buffer
		// with the mod's oversized index buffer - broken UVs beyond the
		// original vertex count.
		if (DX12HasStaticIaReplacements()) {
			bindProbeBegin = DX12OuterTimingProbeUtils::Now();
			if (TryRedirectStaticIaVertexBuffers(
				    commandList,
				    runtimeState,
				    startSlot,
				    count,
				    views,
				    staticRedirectedViews,
				    D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT)) {
				activeViews = staticRedirectedViews;
			}
			bindStaticUs = DX12OuterTimingProbeUtils::ElapsedUs(bindProbeBegin);
			bindProbeBegin = DX12OuterTimingProbeUtils::Now();
		}
		// PreSkin output-resize redirect runs on the static-replaced views:
		// the pre-skinned slots have no static entry (their content hash is
		// rewritten every frame) and are retargeted to the resized buffer,
		// while static-replaced slots (mod buffers) miss the pre-skin VA map
		// and keep the replacement.
		if (activeViews == views || activeViews == staticRedirectedViews) {
			D3D12_VERTEX_BUFFER_VIEW preSkinRedirectedViews[
				D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
			const D3D12_VERTEX_BUFFER_VIEW *preSkinInput = activeViews;
			D3D12_VERTEX_BUFFER_VIEW *preSkinOutput = preSkinInput == views ?
				redirectedViews : preSkinRedirectedViews;
			if (DX12PreSkinIaRedirectUtils::RedirectVertexBufferViews(
				    commandList,
				    startSlot,
				    count,
				    preSkinInput,
				    preSkinOutput,
				    D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT,
				    runtimeState ? &runtimeState->ia : nullptr)) {
				activeViews = preSkinOutput;
			}
			bindPreSkinUs = DX12OuterTimingProbeUtils::ElapsedUs(bindProbeBegin);
			bindProbeBegin = DX12OuterTimingProbeUtils::Now();
		}
		const bool changed = DX12CommandListStateFlow::CaptureVertexBuffers(
			commandList, runtimeState, startSlot, count, activeViews);
		bindCaptureUs = DX12OuterTimingProbeUtils::ElapsedUs(bindProbeBegin);
		if (!changed)
			return;
		bindProbeBegin = DX12OuterTimingProbeUtils::Now();
		if (original)
			original(commandList, startSlot, count, activeViews);
		else
			commandList->IASetVertexBuffers(startSlot, count, activeViews);
		bindOriginalUs = DX12OuterTimingProbeUtils::ElapsedUs(bindProbeBegin);
		LogIaBindProbe(bindRestoreUs, bindStaticUs, bindPreSkinUs,
			bindCaptureUs, bindOriginalUs, startSlot, count);
	}

	static bool ShouldSkipComputeBinding()
	{
		// Probe-only PreSkin still needs the live compute state, but it can use
		// the lightweight runtime resident path instead of the heavy tracker.
		return DX12HotPathState::skipAll &&
			!(DX12HotPathState::HookWorkFlags &
				(DX12HotPathState::FLAG_PRE_SKIN_PROBE |
				 DX12HotPathState::FLAG_PRE_SKIN_BINDING_TRACK));
	}

	static bool ShouldSkipGraphicsBinding()
	{
		// Graphics root tracking is only needed for heavy capture modes or
		// descriptor-aware Mod work. Plain shader replacement can forward calls.
		return DX12HotPathState::skipGraphicsBindings != 0;
	}

	static void SetDescriptorHeaps(
		ID3D12GraphicsCommandList *commandList,
		UINT count,
		ID3D12DescriptorHeap *const *heaps)
	{
		DX12SimulateContext *runtimeState = nullptr;
		if (!ShouldSkipComputeBinding())
			runtimeState = DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12WrapperRuntimeUtils::SetDescriptorHeaps(commandList, runtimeState, count, heaps);
	}

	static void SetDescriptorHeaps(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		UINT count,
		ID3D12DescriptorHeap *const *heaps)
	{
		DX12_PROFILE_FUNCTION(SetDescriptorHeaps);
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetDescriptorHeaps);
		DX12_HOTPATH_TIME_BEGIN(DescriptorHeapsTotal);
		if (!ShouldSkipComputeBinding()) {
			DX12CommandListComputeBindingFlow::CaptureDescriptorHeaps(
				commandList, runtimeState, count, heaps);
		}
		commandList->SetDescriptorHeaps(count, heaps);
		DX12_HOTPATH_TIME_END(DescriptorHeapsTotal);
	}

	static void SetComputeRootSignature(
		ID3D12GraphicsCommandList *commandList,
		ID3D12RootSignature *rootSignature)
	{
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12WrapperRuntimeUtils::SetComputeRootSignature(
			commandList, runtimeState, rootSignature, nullptr);
	}

	static void SetComputeRootSignature(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		ID3D12RootSignature *rootSignature,
		DX12CommandListHookSignatures::SetRootSignature original)
	{
		DX12_PROFILE_FUNCTION(SetComputeRootSignature);
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetComputeRootSignature);
		ID3D12RootSignature *unwrapped = nullptr;
		if (runtimeState && runtimeState->lastWrappedComputeRootSignature == rootSignature)
			unwrapped = runtimeState->lastUnwrappedComputeRootSignature;
		else {
			unwrapped = UnwrapRootSignature(rootSignature);
			if (runtimeState) {
				runtimeState->lastWrappedComputeRootSignature = rootSignature;
				runtimeState->lastUnwrappedComputeRootSignature = unwrapped;
			}
		}
		if (!ShouldSkipComputeBinding()) {
			const UINT64 hookFlags = DX12HotPathState::HookWorkFlags;
			if (runtimeState &&
				DX12HotPathState::skipBindings &&
				!DX12InternalReplayState::IsActive()) {
				// Fast path mirroring CaptureRootSignature's skip branch without
				// the shared fast-skip bookkeeping: keep the deferred pre-skin
				// restore, resident state and binding hash updates, then drop
				// redundant calls.
				DX12CommandListComputeBindingFlow::
					RestoreDeferredReplacementsBeforeBindingMutation(commandList);
				bool changed = true;
				if (DX12HotPathState::NeedsComputeResidentBindingState(hookFlags))
					changed = DX12CommandListRuntime::RememberComputeRootSignature(
						runtimeState, unwrapped);
				if (changed)
					DX12CommandListComputeBindingFlow::UpdateComputeBindingHash(
						runtimeState, reinterpret_cast<uint64_t>(unwrapped));
				if (!changed)
					return;
			}
			else {
				const bool changed = DX12CommandListComputeBindingFlow::CaptureRootSignature(
					commandList, runtimeState, unwrapped, false);
				if (!changed)
					return;
			}
		} else {
			// Binding capture is skipped, so the deferred-restore guard in the
			// capture flow does not run (reload race safety net).
			DX12ModFlushDrawSrvReplacement(commandList);
		}
		if (original) original(commandList, unwrapped);
		else commandList->SetComputeRootSignature(unwrapped);
	}

	static void SetGraphicsRootSignature(
		ID3D12GraphicsCommandList *commandList,
		ID3D12RootSignature *rootSignature)
	{
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12WrapperRuntimeUtils::SetGraphicsRootSignature(
			commandList, runtimeState, rootSignature, nullptr);
	}

	static void SetGraphicsRootSignature(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		ID3D12RootSignature *rootSignature,
		DX12CommandListHookSignatures::SetRootSignature original)
	{
		DX12_PROFILE_FUNCTION(SetGraphicsRootSignature);
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetGraphicsRootSignature);
		ID3D12RootSignature *unwrapped = nullptr;
		if (runtimeState && runtimeState->lastWrappedGraphicsRootSignature == rootSignature)
			unwrapped = runtimeState->lastUnwrappedGraphicsRootSignature;
		else {
			unwrapped = UnwrapRootSignature(rootSignature);
			if (runtimeState) {
				runtimeState->lastWrappedGraphicsRootSignature = rootSignature;
				runtimeState->lastUnwrappedGraphicsRootSignature = unwrapped;
			}
		}
		if (ShouldSkipGraphicsBinding()) {
			// Binding capture is skipped, so the deferred-restore guard in the
			// capture flow does not run (reload race safety net).
			DX12ModFlushDrawSrvReplacement(commandList);
			if (original) original(commandList, unwrapped); else commandList->SetGraphicsRootSignature(unwrapped);
		}
		else {
			const UINT64 hookFlags = DX12HotPathState::HookWorkFlags;
			if (runtimeState &&
				!DX12HotPathState::NeedsGraphicsTrackedBindingCapture(hookFlags) &&
				!DX12InternalReplayState::IsActive()) {
				// Fast path mirroring CaptureRootSignature's skip branch without
				// the shared fast-skip bookkeeping: keep the deferred pre-skin
				// restore and resident state update, then drop redundant calls.
				DX12CommandListComputeBindingFlow::
					RestoreDeferredReplacementsBeforeBindingMutation(commandList);
				bool changed = true;
				if (DX12HotPathState::NeedsGraphicsResidentDescriptorState(hookFlags))
					changed = DX12CommandListRuntime::RememberGraphicsRootSignature(
						runtimeState, unwrapped);
				if (!changed)
					return;
				if (original) original(commandList, unwrapped); else commandList->SetGraphicsRootSignature(unwrapped);
				return;
			}
			const bool changed = DX12CommandListComputeBindingFlow::CaptureRootSignature(
				commandList, runtimeState, unwrapped, true);
			if (!changed)
				return;
			if (original) original(commandList, unwrapped); else commandList->SetGraphicsRootSignature(unwrapped);
		}
	}

	static void SetComputeRootDescriptorTable(
		ID3D12GraphicsCommandList *commandList,
		UINT rootParameterIndex,
		D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor)
	{
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12WrapperRuntimeUtils::SetComputeRootDescriptorTable(
			commandList, runtimeState, rootParameterIndex, baseDescriptor, nullptr);
	}

	static void SetComputeRootDescriptorTable(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		UINT rootParameterIndex,
		D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor,
		DX12CommandListHookSignatures::SetRootDescriptorTable original)
	{
		DX12_PROFILE_FUNCTION(SetComputeRootDescriptorTable);
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetComputeRootDescriptorTable);
		if (!ShouldSkipComputeBinding()) {
			const bool changed = DX12CommandListComputeBindingFlow::CaptureRootDescriptorTable(
				commandList, runtimeState, rootParameterIndex, baseDescriptor, false);
			if (!changed)
				return;
		} else {
			// Same deferred-restore safety net as the graphics path: the
			// game's handle targets its own heap.
			DX12ModFlushDrawSrvReplacement(commandList);
		}
		DX12ModRewriteBundleRootDescriptorTable(
			commandList, false, rootParameterIndex, &baseDescriptor);
		if (original) original(commandList, rootParameterIndex, baseDescriptor);
		else commandList->SetComputeRootDescriptorTable(rootParameterIndex, baseDescriptor);
	}

	static void SetGraphicsRootDescriptorTable(
		ID3D12GraphicsCommandList *commandList,
		UINT rootParameterIndex,
		D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor)
	{
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12WrapperRuntimeUtils::SetGraphicsRootDescriptorTable(
			commandList, runtimeState, rootParameterIndex, baseDescriptor, nullptr);
	}

	static void SetGraphicsRootDescriptorTable(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		UINT rootParameterIndex,
		D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor,
		DX12CommandListHookSignatures::SetRootDescriptorTable original)
	{
		DX12_PROFILE_FUNCTION(SetGraphicsRootDescriptorTable);
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetGraphicsRootDescriptorTable);
		if (ShouldSkipGraphicsBinding()) {
			// Binding capture is skipped, so the deferred-restore guard in the
			// capture flow does not run. The game's handle targets its own
			// heap; restore it before forwarding if a patched mirror is still
			// bound (reload race safety net - the DrawSrv feature normally
			// keeps graphics binding capture enabled).
			DX12ModFlushDrawSrvReplacement(commandList);
			DX12ModRewriteBundleRootDescriptorTable(
				commandList, true, rootParameterIndex, &baseDescriptor);
			if (original) original(commandList, rootParameterIndex, baseDescriptor);
			else commandList->SetGraphicsRootDescriptorTable(rootParameterIndex, baseDescriptor);
		}
		else {
			const bool changed = DX12CommandListComputeBindingFlow::CaptureRootDescriptorTable(
				commandList, runtimeState, rootParameterIndex, baseDescriptor, true);
			if (!changed)
				return;
			DX12ModRewriteBundleRootDescriptorTable(
				commandList, true, rootParameterIndex, &baseDescriptor);
			if (original) original(commandList, rootParameterIndex, baseDescriptor);
			else commandList->SetGraphicsRootDescriptorTable(rootParameterIndex, baseDescriptor);
		}
	}

	static void SetComputeRootConstantBufferView(
		ID3D12GraphicsCommandList *commandList,
		UINT rootParameterIndex,
		D3D12_GPU_VIRTUAL_ADDRESS bufferLocation)
	{
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12WrapperRuntimeUtils::SetComputeRootConstantBufferView(
			commandList, runtimeState, rootParameterIndex, bufferLocation);
	}

	static void SetComputeRootConstantBufferView(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		UINT rootParameterIndex,
		D3D12_GPU_VIRTUAL_ADDRESS bufferLocation)
	{
		DX12_PROFILE_FUNCTION(SetComputeRootConstantBufferView);
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetComputeRootDescriptor);
		const UINT64 flags = DX12HotPathState::HookWorkFlags;
		if (DX12HotPathState::NeedsComputeResidentBindingState(flags)) {
			if (runtimeState)
				DX12CommandListRuntime::RememberComputeRootDescriptor(
					runtimeState, rootParameterIndex,
					D3D12_ROOT_PARAMETER_TYPE_CBV, bufferLocation);
			else
				DX12CommandListRuntime::RememberComputeRootDescriptor(
					commandList, rootParameterIndex,
					D3D12_ROOT_PARAMETER_TYPE_CBV, bufferLocation);
		}
		if (!ShouldSkipComputeBinding()) {
			DX12CommandListStateCapture::ComputeRootDescriptor(
				commandList, runtimeState, rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE_CBV, bufferLocation);
		}
		commandList->SetComputeRootConstantBufferView(rootParameterIndex, bufferLocation);
	}

	static void SetGraphicsRootConstantBufferView(
		ID3D12GraphicsCommandList *commandList,
		UINT rootParameterIndex,
		D3D12_GPU_VIRTUAL_ADDRESS bufferLocation)
	{
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12WrapperRuntimeUtils::SetGraphicsRootConstantBufferView(
			commandList, runtimeState, rootParameterIndex, bufferLocation);
	}

	static void SetGraphicsRootConstantBufferView(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		UINT rootParameterIndex,
		D3D12_GPU_VIRTUAL_ADDRESS bufferLocation)
	{
		DX12_PROFILE_FUNCTION(SetGraphicsRootConstantBufferView);
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetGraphicsRootDescriptor);
		const UINT64 flags = DX12HotPathState::HookWorkFlags;
		if (DX12HotPathState::NeedsGraphicsResidentRootDescriptorState(flags) ||
			DX12HotPathState::NeedsGraphicsTrackedBindingCapture(flags)) {
			DX12CommandListComputeBindingFlow::CaptureRootDescriptor(
				commandList,
				runtimeState,
				rootParameterIndex,
				D3D12_ROOT_PARAMETER_TYPE_CBV,
				bufferLocation,
				true,
				"ID3D12GraphicsCommandList::SetGraphicsRootConstantBufferView");
		}
		commandList->SetGraphicsRootConstantBufferView(rootParameterIndex, bufferLocation);
	}

	static void SetComputeRootShaderResourceView(
		ID3D12GraphicsCommandList *commandList,
		UINT rootParameterIndex,
		D3D12_GPU_VIRTUAL_ADDRESS bufferLocation)
	{
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12WrapperRuntimeUtils::SetComputeRootShaderResourceView(
			commandList, runtimeState, rootParameterIndex, bufferLocation);
	}

	static void SetComputeRootShaderResourceView(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		UINT rootParameterIndex,
		D3D12_GPU_VIRTUAL_ADDRESS bufferLocation)
	{
		DX12_PROFILE_FUNCTION(SetComputeRootShaderResourceView);
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetComputeRootDescriptor);
		const UINT64 flags = DX12HotPathState::HookWorkFlags;
		if (DX12HotPathState::NeedsComputeResidentBindingState(flags)) {
			if (runtimeState)
				DX12CommandListRuntime::RememberComputeRootDescriptor(
					runtimeState, rootParameterIndex,
					D3D12_ROOT_PARAMETER_TYPE_SRV, bufferLocation);
			else
				DX12CommandListRuntime::RememberComputeRootDescriptor(
					commandList, rootParameterIndex,
					D3D12_ROOT_PARAMETER_TYPE_SRV, bufferLocation);
		}
		if (!ShouldSkipComputeBinding()) {
			DX12CommandListStateCapture::ComputeRootDescriptor(
				commandList, runtimeState, rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE_SRV, bufferLocation);
		}
		commandList->SetComputeRootShaderResourceView(rootParameterIndex, bufferLocation);
	}

	static void SetGraphicsRootShaderResourceView(
		ID3D12GraphicsCommandList *commandList,
		UINT rootParameterIndex,
		D3D12_GPU_VIRTUAL_ADDRESS bufferLocation)
	{
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12WrapperRuntimeUtils::SetGraphicsRootShaderResourceView(
			commandList, runtimeState, rootParameterIndex, bufferLocation);
	}

	static void SetGraphicsRootShaderResourceView(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		UINT rootParameterIndex,
		D3D12_GPU_VIRTUAL_ADDRESS bufferLocation)
	{
		DX12_PROFILE_FUNCTION(SetGraphicsRootShaderResourceView);
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetGraphicsRootDescriptor);
		const UINT64 flags = DX12HotPathState::HookWorkFlags;
		if (DX12HotPathState::NeedsGraphicsResidentRootDescriptorState(flags) ||
			DX12HotPathState::NeedsGraphicsTrackedBindingCapture(flags)) {
			DX12CommandListComputeBindingFlow::CaptureRootDescriptor(
				commandList,
				runtimeState,
				rootParameterIndex,
				D3D12_ROOT_PARAMETER_TYPE_SRV,
				bufferLocation,
				true,
				"ID3D12GraphicsCommandList::SetGraphicsRootShaderResourceView");
		}
		commandList->SetGraphicsRootShaderResourceView(rootParameterIndex, bufferLocation);
	}

	static void SetComputeRootUnorderedAccessView(
		ID3D12GraphicsCommandList *commandList,
		UINT rootParameterIndex,
		D3D12_GPU_VIRTUAL_ADDRESS bufferLocation)
	{
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12WrapperRuntimeUtils::SetComputeRootUnorderedAccessView(
			commandList, runtimeState, rootParameterIndex, bufferLocation);
	}

	static void SetComputeRootUnorderedAccessView(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		UINT rootParameterIndex,
		D3D12_GPU_VIRTUAL_ADDRESS bufferLocation)
	{
		DX12_PROFILE_FUNCTION(SetComputeRootUnorderedAccessView);
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetComputeRootDescriptor);
		const UINT64 flags = DX12HotPathState::HookWorkFlags;
		if (DX12HotPathState::NeedsComputeResidentBindingState(flags)) {
			if (runtimeState)
				DX12CommandListRuntime::RememberComputeRootDescriptor(
					runtimeState, rootParameterIndex,
					D3D12_ROOT_PARAMETER_TYPE_UAV, bufferLocation);
			else
				DX12CommandListRuntime::RememberComputeRootDescriptor(
					commandList, rootParameterIndex,
					D3D12_ROOT_PARAMETER_TYPE_UAV, bufferLocation);
		}
		if (!ShouldSkipComputeBinding()) {
			DX12CommandListStateCapture::ComputeRootDescriptor(
				commandList, runtimeState, rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE_UAV, bufferLocation);
		}
		commandList->SetComputeRootUnorderedAccessView(rootParameterIndex, bufferLocation);
	}

	static void SetGraphicsRootUnorderedAccessView(
		ID3D12GraphicsCommandList *commandList,
		UINT rootParameterIndex,
		D3D12_GPU_VIRTUAL_ADDRESS bufferLocation)
	{
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12WrapperRuntimeUtils::SetGraphicsRootUnorderedAccessView(
			commandList, runtimeState, rootParameterIndex, bufferLocation);
	}

	static void SetGraphicsRootUnorderedAccessView(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		UINT rootParameterIndex,
		D3D12_GPU_VIRTUAL_ADDRESS bufferLocation)
	{
		DX12_PROFILE_FUNCTION(SetGraphicsRootUnorderedAccessView);
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetGraphicsRootDescriptor);
		const UINT64 flags = DX12HotPathState::HookWorkFlags;
		if (DX12HotPathState::NeedsGraphicsResidentRootDescriptorState(flags) ||
			DX12HotPathState::NeedsGraphicsTrackedBindingCapture(flags)) {
			DX12CommandListComputeBindingFlow::CaptureRootDescriptor(
				commandList,
				runtimeState,
				rootParameterIndex,
				D3D12_ROOT_PARAMETER_TYPE_UAV,
				bufferLocation,
				true,
				"ID3D12GraphicsCommandList::SetGraphicsRootUnorderedAccessView");
		}
		commandList->SetGraphicsRootUnorderedAccessView(rootParameterIndex, bufferLocation);
	}

	static void IASetPrimitiveTopology(
		ID3D12GraphicsCommandList *commandList,
		D3D12_PRIMITIVE_TOPOLOGY topology)
	{
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12WrapperRuntimeUtils::IASetPrimitiveTopology(commandList, runtimeState, topology);
	}

	static void IASetPrimitiveTopology(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		D3D12_PRIMITIVE_TOPOLOGY topology)
	{
		DX12_PROFILE_FUNCTION(IASetPrimitiveTopology);
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiIASetPrimitiveTopology);
		if (DX12HotPathState::skipAll)
			commandList->IASetPrimitiveTopology(topology);
		else {
			if (DX12CommandListStateFlow::CapturePrimitiveTopology(
					commandList, runtimeState, topology))
				commandList->IASetPrimitiveTopology(topology);
		}
	}
};


