#pragma once

#include "Hooks/DX12HookRuntime.hpp"

enum class DX12BindingCollectTaskKind
{
	StateEvent = 0,
	DrawEvent,
	DispatchEvent,
	OutputEvent,
};

class DX12BindingCollectTask
{
public:
	DX12BindingCollectTaskKind kind = DX12BindingCollectTaskKind::StateEvent;
	UINT64 sequence = 0;
	UINT64 captureSessionId = 0;
	DWORD enqueueThreadId = 0;
	ID3D12GraphicsCommandList *commandList = nullptr;
	CommandListBindingState state = {};
	UINT64 descriptorEpoch = 0;
	const char *eventKind = nullptr;
	UINT vertexCountPerInstance = 0;
	UINT indexCountPerInstance = 0;
	UINT instanceCount = 0;
	UINT startVertexLocation = 0;
	UINT startIndexLocation = 0;
	INT baseVertexLocation = 0;
	UINT startInstanceLocation = 0;
	UINT threadGroupCountX = 0;
	UINT threadGroupCountY = 0;
	UINT threadGroupCountZ = 0;
	UINT slot = 0;
	SIZE_T cpuHandle = 0;
	UINT64 drawId = 0;
};

class DX12BindingCollectQueue
{
public:
	inline static std::mutex mutex;
	inline static std::condition_variable wake;
	inline static std::condition_variable idle;
	inline static std::deque<DX12BindingCollectTask> tasks;
	inline static std::thread worker;
	inline static std::atomic<bool> started = false;
	inline static std::atomic<bool> stopRequested = false;
	inline static std::atomic<uint64_t> nextSequence = 1;
	inline static std::atomic<uint64_t> enqueued = 0;
	inline static std::atomic<uint64_t> dequeued = 0;
	inline static std::atomic<uint64_t> completed = 0;
	inline static std::atomic<uint64_t> completedSequence = 0;
	inline static std::atomic<uint64_t> maxDepth = 0;
	inline static std::atomic<uint64_t> maxPendingAtDrain = 0;
	inline static std::atomic<uint64_t> drainCalls = 0;
	inline static std::atomic<uint64_t> drainWaitUs = 0;
	inline static std::atomic<uint64_t> drainMaxUs = 0;
	inline static std::atomic<uint64_t> taskElapsedUs = 0;
	inline static std::atomic<uint64_t> taskMaxUs = 0;
	inline static uint64_t activeTasks = 0;

	static void Start();
	static void Stop();
	static void DrainForSnapshot(const char *reason);
	static void BeginFrameDrain();
	static bool EnqueueStateEvent(
		ID3D12GraphicsCommandList *commandList,
		const CommandListBindingState &state,
		const char *kind,
		UINT64 descriptorEpoch,
		UINT64 captureSessionId);
	static bool EnqueueDrawEvent(
		ID3D12GraphicsCommandList *commandList,
		const CommandListBindingState &state,
		const char *kind,
		UINT64 descriptorEpoch,
		UINT64 captureSessionId,
		UINT vertexCountPerInstance,
		UINT indexCountPerInstance,
		UINT instanceCount,
		UINT startVertexLocation,
		UINT startIndexLocation,
		INT baseVertexLocation,
		UINT startInstanceLocation);
	static bool EnqueueDispatchEvent(
		ID3D12GraphicsCommandList *commandList,
		const CommandListBindingState &state,
		UINT64 descriptorEpoch,
		UINT64 captureSessionId,
		UINT threadGroupCountX,
		UINT threadGroupCountY,
		UINT threadGroupCountZ);
	static bool EnqueueOutputEvent(
		ID3D12GraphicsCommandList *commandList,
		const CommandListBindingState &state,
		const char *kind,
		UINT slot,
		SIZE_T cpuHandle,
		UINT64 drawId,
		UINT64 captureSessionId);
	static size_t PendingCount();
	static UINT64 LastSubmittedSequence();

	static bool EnqueueTask(DX12BindingCollectTask &&task);
	static void WorkerLoop();
	static void WaitForIdle(const char *reason);
	static void WaitForSubmittedSequence(UINT64 targetSequence, const char *reason);
	static void ProcessTask(DX12BindingCollectTask *task, const char *source);
	static void ProcessStateEventTask(DX12BindingCollectTask *task, const char *source);
	static void ProcessDrawEventTask(DX12BindingCollectTask *task, const char *source);
	static void ProcessDispatchEventTask(DX12BindingCollectTask *task, const char *source);
	static void ProcessOutputEventTask(DX12BindingCollectTask *task, const char *source);
	static void UpdateMaxDepth(size_t depth);
	static void UpdateMaxPendingAtDrain(size_t depth);
	static void RecordTaskStats(int64_t elapsedUs);
	static void RecordDrainWaitStats(int64_t elapsedUs);
	static void LogStats(const char *event, uint64_t sequence, size_t depth);
};




class DX12BindingTrackerQuery
{
public:
	static UINT DescriptorCountForRange(
		const DX12DescriptorHeapSummary &heap,
		UINT64 tableBaseIndex,
		const DX12RootDescriptorRangeSummary &range)
	{
		const UINT64 firstDescriptor = tableBaseIndex + range.effectiveOffset;
		if (firstDescriptor >= heap.numDescriptors)
			return 0;

		const UINT64 heapAvailable = heap.numDescriptors - firstDescriptor;
		const UINT64 declaredCount =
			range.numDescriptors == UINT_MAX ? heapAvailable : range.numDescriptors;
		const UINT64 boundedCount = min(declaredCount, heapAvailable);
		return boundedCount > UINT_MAX ? UINT_MAX : static_cast<UINT>(boundedCount);
	}
	static const char *HeapTypeName(UINT type)
	{
		switch (type)
		{
		case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV:
			return "CBV_SRV_UAV";
		case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:
			return "SAMPLER";
		case D3D12_DESCRIPTOR_HEAP_TYPE_RTV:
			return "RTV";
		case D3D12_DESCRIPTOR_HEAP_TYPE_DSV:
			return "DSV";
		default:
			return "UNKNOWN";
		}
	}

	static const char *ResourceDimensionName(D3D12_RESOURCE_DIMENSION dimension)
	{
		switch (dimension)
		{
		case D3D12_RESOURCE_DIMENSION_BUFFER:
			return "BUFFER";
		case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
			return "TEXTURE1D";
		case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
			return "TEXTURE2D";
		case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
			return "TEXTURE3D";
		default:
			return "UNKNOWN";
		}
	}

	static const DX12RootParameterSummary *FindRootParameter(
		const DX12RootSignatureSummary &rootSignature, UINT rootParameterIndex)
	{
		for (const DX12RootParameterSummary &parameter : rootSignature.parameters)
		{
			if (parameter.rootParameterIndex == rootParameterIndex)
				return &parameter;
		}
		return nullptr;
	}

	static bool GetRootSignatureForEvent(
		const BindingEvent &event, bool compute, DX12RootSignatureSummary *summary)
	{
		if (!summary)
			return false;
		ID3D12RootSignature *rootSignature = compute ? event.computeRootSignature : event.graphicsRootSignature;
		if (!rootSignature && event.shaderInfo.psoIndex)
			DX12GetPsoRootSignature(event.shaderInfo.psoIndex, &rootSignature);
		return rootSignature && DX12GetRootSignatureSummary(rootSignature, summary);
	}

	static UINT HeapTypeForDescriptorRange(UINT rangeType)
	{
		if (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER)
			return D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
		return D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	}

	static bool TableMatchesHeap(const RootTableState &table, const DX12DescriptorHeapSummary &heap)
	{
		if (!table.valid || heap.increment == 0 || heap.gpuStart == 0)
			return false;
		const UINT64 begin = heap.gpuStart;
		const UINT64 end = begin + static_cast<UINT64>(heap.numDescriptors) * heap.increment;
		return table.baseDescriptor.ptr >= begin && table.baseDescriptor.ptr < end;
	}

	static const DX12DescriptorHeapSummary *FindHeapForGpuHandle(
		const std::vector<DX12DescriptorHeapSummary> &heaps,
		const RootTableState &table,
		UINT requiredType)
	{
		for (const DX12DescriptorHeapSummary &heap : heaps)
		{
			if (heap.type != requiredType || heap.increment == 0 || heap.gpuStart == 0)
				continue;
			const UINT64 begin = heap.gpuStart;
			const UINT64 end = begin + static_cast<UINT64>(heap.numDescriptors) * heap.increment;
			if (table.baseDescriptor.ptr >= begin && table.baseDescriptor.ptr < end)
				return &heap;
		}
		return nullptr;
	}

	static const DX12DescriptorHeapSummary *FindHeapForTableRange(
		const std::vector<DX12DescriptorHeapSummary> &heaps,
		const RootTableState &table,
		const DX12RootDescriptorRangeSummary *range,
		UINT heapType)
	{
		const UINT requiredType = range ? HeapTypeForDescriptorRange(range->rangeType) : heapType;
		for (const DX12DescriptorHeapSummary &heap : heaps)
		{
			if (heap.type != requiredType || !TableMatchesHeap(table, heap))
				continue;
			return &heap;
		}
		return nullptr;
	}

	static const DX12DescriptorSummary *FindDescriptorByCpuHandle(
		const std::unordered_map<SIZE_T, const DX12DescriptorSummary *> &descriptorsByCpuHandle,
		SIZE_T cpuHandle)
	{
		auto it = descriptorsByCpuHandle.find(cpuHandle);
		return it != descriptorsByCpuHandle.end() ? it->second : nullptr;
	}

	static void BuildDescriptorLookup(
		const std::vector<DX12DescriptorSummary> &descriptors,
		std::unordered_map<SIZE_T, const DX12DescriptorSummary *> *descriptorsByCpuHandle)
	{
		if (!descriptorsByCpuHandle)
			return;

		descriptorsByCpuHandle->clear();
		descriptorsByCpuHandle->reserve(descriptors.size());
		for (const DX12DescriptorSummary &descriptor : descriptors)
			(*descriptorsByCpuHandle)[descriptor.cpuHandle] = &descriptor;
	}

	static bool IsDrawEvent(const BindingEvent &event)
	{
		return event.kind == "draw" || event.kind == "draw_indexed";
	}

	static bool IsDispatchEvent(const BindingEvent &event)
	{
		return event.kind == "dispatch";
	}
};




class DX12CaptureLedgerSnapshot
{
public:
	std::vector<BindingEvent> events;
	std::vector<OutputBindingEvent> outputEvents;
	UINT64 droppedEvents = 0;
	UINT64 droppedOutputEvents = 0;
	UINT64 beginPresent = 0;
	UINT64 ledgerEpoch = 0;
	UINT64 captureSessionId = 0;
};

enum class DX12CaptureLedgerRecordResult
{
	NotActive = 0,
	Stored = 1,
	Duplicate = 2,
	InactiveSession = 3,
};

class DX12CaptureOutputKey
{
public:
	ID3D12GraphicsCommandList *commandList = nullptr;
	SIZE_T cpuHandle = 0;
	std::string kind;
	UINT slot = 0;
	UINT64 drawId = 0;
	UINT64 dispatchId = 0;

	bool operator==(const DX12CaptureOutputKey &other) const
	{
		return commandList == other.commandList &&
			cpuHandle == other.cpuHandle &&
			kind == other.kind &&
			slot == other.slot &&
			drawId == other.drawId &&
			dispatchId == other.dispatchId;
	}
};

class DX12CaptureOutputKeyHash
{
public:
	size_t operator()(const DX12CaptureOutputKey &key) const
	{
		size_t hash = std::hash<void*>()(
			static_cast<void*>(key.commandList));
		hash = Mix(hash, key.cpuHandle);
		hash = Mix(hash, std::hash<std::string>()(key.kind));
		hash = Mix(hash, key.slot);
		hash = Mix(hash, key.drawId);
		return Mix(hash, key.dispatchId);
	}

private:
	static size_t Mix(size_t hash, UINT64 value)
	{
		return hash ^ (static_cast<size_t>(value) + 0x9e3779b97f4a7c15ull +
			(hash << 6) + (hash >> 2));
	}
};

class DX12CaptureLedger
{
public:
	static SRWLOCK Lock;
	static bool Active;
	static UINT64 BeginPresent;
	static UINT64 LedgerEpoch;
	static UINT64 CaptureSessionId;
	static UINT64 EventSerial;
	static UINT64 OutputEventSerial;
	static UINT64 DroppedEvents;
	static UINT64 DroppedOutputEvents;
	static std::vector<BindingEvent> Events;
	static std::vector<OutputBindingEvent> OutputEvents;
	static std::unordered_set<DX12CaptureOutputKey, DX12CaptureOutputKeyHash>
		OutputKeys;

	static void BeginCapture(UINT64 present);
	static void EndCapture();
	static void Clear();
	static bool IsActive();
	static UINT64 CurrentCaptureSessionId();
	static UINT64 LastCaptureSessionId();
	static bool IsSessionCurrent(UINT64 captureSessionId);
	static DX12CaptureLedgerRecordResult RecordEvent(BindingEvent *event);
	static DX12CaptureLedgerRecordResult TryStoreOutputEvent(OutputBindingEvent *event);
	static void GetSnapshot(DX12CaptureLedgerSnapshot *snapshot);
	static void ReleaseEventsLocked();
	static void LogSnapshotHealth(const char *stage, const DX12CaptureLedgerSnapshot &snapshot);
};



class DX12CommandListHookCallbacks
{
public:
	static DX12IaReplacementExecutorCallbacks MakeIaReplacementCallbacks(
		ID3D12GraphicsCommandList *commandList);
	static const DX12IaReplacementExecutorCallbacks &GetCachedIaReplacementCallbacks(
		ID3D12GraphicsCommandList *commandList);
	static void RegisterCommandListBase(
		ID3D12GraphicsCommandList *baseList, ID3D12PipelineState *initialState);
	static void RegisterCommandList(IUnknown *commandList, ID3D12PipelineState *initialState);
	static void RegisterSubmittedCommandList(IUnknown *commandList);
	static void RegisterCommandQueue(IUnknown *commandQueue);

	static HRESULT STDMETHODCALLTYPE HookedCreateCommandQueue(
		ID3D12Device *device, const D3D12_COMMAND_QUEUE_DESC *desc, REFIID riid, void **commandQueue);
	static HRESULT STDMETHODCALLTYPE HookedCreateCommandAllocator(
		ID3D12Device *device, D3D12_COMMAND_LIST_TYPE type, REFIID riid, void **allocator);
	static HRESULT STDMETHODCALLTYPE HookedCreateCommandList(
		ID3D12Device *device, UINT nodeMask, D3D12_COMMAND_LIST_TYPE type,
		ID3D12CommandAllocator *allocator, ID3D12PipelineState *initialState,
		REFIID riid, void **commandList);
	static HRESULT STDMETHODCALLTYPE HookedCreateCommandList1(
		ID3D12Device4 *device, UINT nodeMask, D3D12_COMMAND_LIST_TYPE type,
		D3D12_COMMAND_LIST_FLAGS flags, REFIID riid, void **commandList);
	static void STDMETHODCALLTYPE HookedQueueUpdateTileMappings(
		ID3D12CommandQueue *queue, ID3D12Resource *resource, UINT numResourceRegions,
		const D3D12_TILED_RESOURCE_COORDINATE *resourceRegionStartCoordinates,
		const D3D12_TILE_REGION_SIZE *resourceRegionSizes, ID3D12Heap *heap, UINT numRanges,
		const D3D12_TILE_RANGE_FLAGS *rangeFlags, const UINT *heapRangeStartOffsets,
		const UINT *rangeTileCounts, D3D12_TILE_MAPPING_FLAGS flags);
	static void STDMETHODCALLTYPE HookedQueueCopyTileMappings(
		ID3D12CommandQueue *queue, ID3D12Resource *dstResource,
		const D3D12_TILED_RESOURCE_COORDINATE *dstRegionStartCoordinate,
		ID3D12Resource *srcResource, const D3D12_TILED_RESOURCE_COORDINATE *srcRegionStartCoordinate,
		const D3D12_TILE_REGION_SIZE *regionSize, D3D12_TILE_MAPPING_FLAGS flags);
	static void STDMETHODCALLTYPE HookedQueueExecuteCommandLists(
		ID3D12CommandQueue *queue, UINT numCommandLists, ID3D12CommandList *const *commandLists);
	static void STDMETHODCALLTYPE HookedQueueSetMarker(
		ID3D12CommandQueue *queue, UINT metadata, const void *data, UINT size);
	static void STDMETHODCALLTYPE HookedQueueBeginEvent(
		ID3D12CommandQueue *queue, UINT metadata, const void *data, UINT size);
	static void STDMETHODCALLTYPE HookedQueueEndEvent(ID3D12CommandQueue *queue);
	static HRESULT STDMETHODCALLTYPE HookedQueueSignal(
		ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 value);
	static HRESULT STDMETHODCALLTYPE HookedQueueWait(
		ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 value);
	static HRESULT STDMETHODCALLTYPE HookedQueueGetTimestampFrequency(
		ID3D12CommandQueue *queue, UINT64 *frequency);
	static HRESULT STDMETHODCALLTYPE HookedQueueGetClockCalibration(
		ID3D12CommandQueue *queue, UINT64 *gpuTimestamp, UINT64 *cpuTimestamp);
	static HRESULT STDMETHODCALLTYPE HookedCloseCommandList(ID3D12GraphicsCommandList *commandList);
	static HRESULT STDMETHODCALLTYPE HookedResetCommandList(
		ID3D12GraphicsCommandList *commandList, ID3D12CommandAllocator *allocator,
		ID3D12PipelineState *initialState);
	static void STDMETHODCALLTYPE HookedClearState(
		ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState);
	static void STDMETHODCALLTYPE HookedDrawInstanced(
		ID3D12GraphicsCommandList *commandList, UINT vertexCountPerInstance, UINT instanceCount,
		UINT startVertexLocation, UINT startInstanceLocation);
	static void STDMETHODCALLTYPE HookedDrawIndexedInstanced(
		ID3D12GraphicsCommandList *commandList, UINT indexCountPerInstance, UINT instanceCount,
		UINT startIndexLocation, INT baseVertexLocation, UINT startInstanceLocation);
	static void STDMETHODCALLTYPE HookedDispatch(
		ID3D12GraphicsCommandList *commandList, UINT threadGroupCountX,
		UINT threadGroupCountY, UINT threadGroupCountZ);
	static void STDMETHODCALLTYPE HookedCopyBufferRegion(
		ID3D12GraphicsCommandList *commandList, ID3D12Resource *dstBuffer, UINT64 dstOffset,
		ID3D12Resource *srcBuffer, UINT64 srcOffset, UINT64 numBytes);
	static void STDMETHODCALLTYPE HookedCopyTextureRegion(
		ID3D12GraphicsCommandList *commandList, const D3D12_TEXTURE_COPY_LOCATION *dst,
		UINT dstX, UINT dstY, UINT dstZ, const D3D12_TEXTURE_COPY_LOCATION *src,
		const D3D12_BOX *srcBox);
	static void STDMETHODCALLTYPE HookedCopyResource(
		ID3D12GraphicsCommandList *commandList, ID3D12Resource *dstResource, ID3D12Resource *srcResource);
	static void STDMETHODCALLTYPE HookedCopyTiles(
		ID3D12GraphicsCommandList *commandList, ID3D12Resource *tiledResource,
		const D3D12_TILED_RESOURCE_COORDINATE *tileRegionStartCoordinate,
		const D3D12_TILE_REGION_SIZE *tileRegionSize, ID3D12Resource *buffer,
		UINT64 bufferStartOffsetInBytes, D3D12_TILE_COPY_FLAGS flags);
	static void STDMETHODCALLTYPE HookedResolveSubresource(
		ID3D12GraphicsCommandList *commandList, ID3D12Resource *dstResource, UINT dstSubresource,
		ID3D12Resource *srcResource, UINT srcSubresource, DXGI_FORMAT format);
	static void STDMETHODCALLTYPE HookedIASetPrimitiveTopology(
		ID3D12GraphicsCommandList *commandList, D3D12_PRIMITIVE_TOPOLOGY topology);
	static void STDMETHODCALLTYPE HookedRSSetViewports(
		ID3D12GraphicsCommandList *commandList, UINT count, const D3D12_VIEWPORT *viewports);
	static void STDMETHODCALLTYPE HookedRSSetScissorRects(
		ID3D12GraphicsCommandList *commandList, UINT count, const D3D12_RECT *rects);
	static void STDMETHODCALLTYPE HookedOMSetBlendFactor(
		ID3D12GraphicsCommandList *commandList, const FLOAT blendFactor[4]);
	static void STDMETHODCALLTYPE HookedOMSetStencilRef(
		ID3D12GraphicsCommandList *commandList, UINT stencilRef);
	static void STDMETHODCALLTYPE HookedSetPipelineState(
		ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState);
	static void STDMETHODCALLTYPE HookedResourceBarrier(
		ID3D12GraphicsCommandList *commandList, UINT numBarriers,
		const D3D12_RESOURCE_BARRIER *barriers);
	static void STDMETHODCALLTYPE HookedExecuteBundle(
		ID3D12GraphicsCommandList *commandList, ID3D12GraphicsCommandList *bundle);
	static void STDMETHODCALLTYPE HookedSetGraphicsRootUnorderedAccessView(
		ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
		D3D12_GPU_VIRTUAL_ADDRESS address);
	static void STDMETHODCALLTYPE HookedIASetIndexBuffer(
		ID3D12GraphicsCommandList *commandList, const D3D12_INDEX_BUFFER_VIEW *view);
	static void STDMETHODCALLTYPE HookedIASetVertexBuffers(
		ID3D12GraphicsCommandList *commandList, UINT startSlot, UINT count,
		const D3D12_VERTEX_BUFFER_VIEW *views);
	static void STDMETHODCALLTYPE HookedSOSetTargets(
		ID3D12GraphicsCommandList *commandList, UINT startSlot, UINT count,
		const D3D12_STREAM_OUTPUT_BUFFER_VIEW *views);
	static void STDMETHODCALLTYPE HookedOMSetRenderTargets(
		ID3D12GraphicsCommandList *commandList, UINT numRenderTargetDescriptors,
		const D3D12_CPU_DESCRIPTOR_HANDLE *renderTargetDescriptors,
		BOOL singleHandleToDescriptorRange, const D3D12_CPU_DESCRIPTOR_HANDLE *depthStencilDescriptor);
	static void STDMETHODCALLTYPE HookedClearDepthStencilView(
		ID3D12GraphicsCommandList *commandList, D3D12_CPU_DESCRIPTOR_HANDLE depthStencilView,
		D3D12_CLEAR_FLAGS clearFlags, FLOAT depth, UINT8 stencil, UINT numRects,
		const D3D12_RECT *rects);
	static void STDMETHODCALLTYPE HookedClearRenderTargetView(
		ID3D12GraphicsCommandList *commandList, D3D12_CPU_DESCRIPTOR_HANDLE renderTargetView,
		const FLOAT colorRGBA[4], UINT numRects, const D3D12_RECT *rects);
	static void STDMETHODCALLTYPE HookedClearUnorderedAccessViewUint(
		ID3D12GraphicsCommandList *commandList, D3D12_GPU_DESCRIPTOR_HANDLE viewGpuHandleInCurrentHeap,
		D3D12_CPU_DESCRIPTOR_HANDLE viewCpuHandle, ID3D12Resource *resource, const UINT values[4],
		UINT numRects, const D3D12_RECT *rects);
	static void STDMETHODCALLTYPE HookedClearUnorderedAccessViewFloat(
		ID3D12GraphicsCommandList *commandList, D3D12_GPU_DESCRIPTOR_HANDLE viewGpuHandleInCurrentHeap,
		D3D12_CPU_DESCRIPTOR_HANDLE viewCpuHandle, ID3D12Resource *resource, const FLOAT values[4],
		UINT numRects, const D3D12_RECT *rects);
	static void STDMETHODCALLTYPE HookedDiscardResource(
		ID3D12GraphicsCommandList *commandList, ID3D12Resource *resource,
		const D3D12_DISCARD_REGION *region);
	static void STDMETHODCALLTYPE HookedBeginQuery(
		ID3D12GraphicsCommandList *commandList, ID3D12QueryHeap *queryHeap,
		D3D12_QUERY_TYPE type, UINT index);
	static void STDMETHODCALLTYPE HookedEndQuery(
		ID3D12GraphicsCommandList *commandList, ID3D12QueryHeap *queryHeap,
		D3D12_QUERY_TYPE type, UINT index);
	static void STDMETHODCALLTYPE HookedResolveQueryData(
		ID3D12GraphicsCommandList *commandList, ID3D12QueryHeap *queryHeap, D3D12_QUERY_TYPE type,
		UINT startIndex, UINT numQueries, ID3D12Resource *destinationBuffer,
		UINT64 alignedDestinationBufferOffset);
	static void STDMETHODCALLTYPE HookedSetPredication(
		ID3D12GraphicsCommandList *commandList, ID3D12Resource *buffer,
		UINT64 alignedBufferOffset, D3D12_PREDICATION_OP operation);
	static void STDMETHODCALLTYPE HookedCommandListSetMarker(
		ID3D12GraphicsCommandList *commandList, UINT metadata, const void *data, UINT size);
	static void STDMETHODCALLTYPE HookedCommandListBeginEvent(
		ID3D12GraphicsCommandList *commandList, UINT metadata, const void *data, UINT size);
	static void STDMETHODCALLTYPE HookedCommandListEndEvent(ID3D12GraphicsCommandList *commandList);
	static void STDMETHODCALLTYPE HookedExecuteIndirect(
		ID3D12GraphicsCommandList *commandList, ID3D12CommandSignature *commandSignature,
		UINT maxCommandCount, ID3D12Resource *argumentBuffer, UINT64 argumentBufferOffset,
		ID3D12Resource *countBuffer, UINT64 countBufferOffset);
};


class DX12CommandListHookSignatures
{
public:
	using CreateCommandQueue = HRESULT(STDMETHODCALLTYPE *)(ID3D12Device*, const D3D12_COMMAND_QUEUE_DESC*, REFIID, void**);
	using CreateCommandAllocator = HRESULT(STDMETHODCALLTYPE *)(ID3D12Device*, D3D12_COMMAND_LIST_TYPE, REFIID, void**);
	using CreateCommandList = HRESULT(STDMETHODCALLTYPE *)(ID3D12Device*, UINT, D3D12_COMMAND_LIST_TYPE, ID3D12CommandAllocator*, ID3D12PipelineState*, REFIID, void**);
	using CreateCommandList1 = HRESULT(STDMETHODCALLTYPE *)(ID3D12Device4*, UINT, D3D12_COMMAND_LIST_TYPE, D3D12_COMMAND_LIST_FLAGS, REFIID, void**);

	using QueueUpdateTileMappings = void(STDMETHODCALLTYPE *)(ID3D12CommandQueue*, ID3D12Resource*, UINT, const D3D12_TILED_RESOURCE_COORDINATE*, const D3D12_TILE_REGION_SIZE*, ID3D12Heap*, UINT, const D3D12_TILE_RANGE_FLAGS*, const UINT*, const UINT*, D3D12_TILE_MAPPING_FLAGS);
	using QueueCopyTileMappings = void(STDMETHODCALLTYPE *)(ID3D12CommandQueue*, ID3D12Resource*, const D3D12_TILED_RESOURCE_COORDINATE*, ID3D12Resource*, const D3D12_TILED_RESOURCE_COORDINATE*, const D3D12_TILE_REGION_SIZE*, D3D12_TILE_MAPPING_FLAGS);
	using QueueExecuteCommandLists = void(STDMETHODCALLTYPE *)(ID3D12CommandQueue*, UINT, ID3D12CommandList *const*);
	using QueueSetMarker = void(STDMETHODCALLTYPE *)(ID3D12CommandQueue*, UINT, const void*, UINT);
	using QueueBeginEvent = void(STDMETHODCALLTYPE *)(ID3D12CommandQueue*, UINT, const void*, UINT);
	using QueueEndEvent = void(STDMETHODCALLTYPE *)(ID3D12CommandQueue*);
	using QueueSignal = HRESULT(STDMETHODCALLTYPE *)(ID3D12CommandQueue*, ID3D12Fence*, UINT64);
	using QueueWait = HRESULT(STDMETHODCALLTYPE *)(ID3D12CommandQueue*, ID3D12Fence*, UINT64);
	using QueueGetTimestampFrequency = HRESULT(STDMETHODCALLTYPE *)(ID3D12CommandQueue*, UINT64*);
	using QueueGetClockCalibration = HRESULT(STDMETHODCALLTYPE *)(ID3D12CommandQueue*, UINT64*, UINT64*);

	using CloseCommandList = HRESULT(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*);
	using ResetCommandList = HRESULT(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12CommandAllocator*, ID3D12PipelineState*);
	using ClearState = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12PipelineState*);
	using DrawInstanced = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, UINT, UINT, UINT);
	using DrawIndexedInstanced = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, UINT, UINT, INT, UINT);
	using Dispatch = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, UINT, UINT);
	using CopyBufferRegion = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12Resource*, UINT64, ID3D12Resource*, UINT64, UINT64);
	using CopyTextureRegion = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, const D3D12_TEXTURE_COPY_LOCATION*, UINT, UINT, UINT, const D3D12_TEXTURE_COPY_LOCATION*, const D3D12_BOX*);
	using CopyResource = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12Resource*, ID3D12Resource*);
	using CopyTiles = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12Resource*, const D3D12_TILED_RESOURCE_COORDINATE*, const D3D12_TILE_REGION_SIZE*, ID3D12Resource*, UINT64, D3D12_TILE_COPY_FLAGS);
	using ResolveSubresource = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12Resource*, UINT, ID3D12Resource*, UINT, DXGI_FORMAT);
	using IaSetPrimitiveTopology = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, D3D12_PRIMITIVE_TOPOLOGY);
	using RsSetViewports = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, const D3D12_VIEWPORT*);
	using RsSetScissorRects = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, const D3D12_RECT*);
	using OmSetBlendFactor = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, const FLOAT[4]);
	using OmSetStencilRef = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT);
	using SetPipelineState = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12PipelineState*);
	using ResourceBarrier = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, const D3D12_RESOURCE_BARRIER*);
	using ExecuteBundle = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12GraphicsCommandList*);
	using SetDescriptorHeaps = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, ID3D12DescriptorHeap *const*);
	using SetRootSignature = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12RootSignature*);
	using SetRootDescriptorTable = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_DESCRIPTOR_HANDLE);
	using SetRoot32BitConstant = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, UINT, UINT);
	using SetRoot32BitConstants = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, UINT, const void*, UINT);
	using SetRootGpuVa = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_VIRTUAL_ADDRESS);
	using IaSetIndexBuffer = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, const D3D12_INDEX_BUFFER_VIEW*);
	using IaSetVertexBuffers = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, UINT, const D3D12_VERTEX_BUFFER_VIEW*);
	using SoSetTargets = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, UINT, const D3D12_STREAM_OUTPUT_BUFFER_VIEW*);
	using OmSetRenderTargets = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, const D3D12_CPU_DESCRIPTOR_HANDLE*, BOOL, const D3D12_CPU_DESCRIPTOR_HANDLE*);
	using ClearDepthStencilView = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_CLEAR_FLAGS, FLOAT, UINT8, UINT, const D3D12_RECT*);
	using ClearRenderTargetView = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, D3D12_CPU_DESCRIPTOR_HANDLE, const FLOAT[4], UINT, const D3D12_RECT*);
	using ClearUavUint = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, D3D12_GPU_DESCRIPTOR_HANDLE, D3D12_CPU_DESCRIPTOR_HANDLE, ID3D12Resource*, const UINT[4], UINT, const D3D12_RECT*);
	using ClearUavFloat = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, D3D12_GPU_DESCRIPTOR_HANDLE, D3D12_CPU_DESCRIPTOR_HANDLE, ID3D12Resource*, const FLOAT[4], UINT, const D3D12_RECT*);
	using DiscardResource = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12Resource*, const D3D12_DISCARD_REGION*);
	using BeginQuery = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12QueryHeap*, D3D12_QUERY_TYPE, UINT);
	using EndQuery = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12QueryHeap*, D3D12_QUERY_TYPE, UINT);
	using ResolveQueryData = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12QueryHeap*, D3D12_QUERY_TYPE, UINT, UINT, ID3D12Resource*, UINT64);
	using SetPredication = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12Resource*, UINT64, D3D12_PREDICATION_OP);
	using CommandListSetMarker = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, const void*, UINT);
	using CommandListBeginEvent = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, const void*, UINT);
	using CommandListEndEvent = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*);
	using ExecuteIndirect = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12CommandSignature*, UINT, ID3D12Resource*, UINT64, ID3D12Resource*, UINT64);
};

class DX12ThreadLocalCommandListOriginal
{
public:
	void *target = nullptr;
	void *original = nullptr;
	bool resolved = false;
};



class DX12ThreadLocalIaCallbacksCache
{
public:
	ID3D12GraphicsCommandList *commandList = nullptr;
	DX12IaReplacementExecutorCallbacks callbacks;
};


class DX12VtableBaseCache
{
public:
	void *vtable = nullptr;
	std::array<void*, 256> originals{};
	std::array<bool, 256> slotResolved{};
};




class DX12CommandListHookState
{
public:
	static DX12CommandListHookSignatures::CreateCommandQueue OrigCreateCommandQueue;
	static DX12CommandListHookSignatures::CreateCommandAllocator OrigCreateCommandAllocator;
	static DX12CommandListHookSignatures::CreateCommandList OrigCreateCommandList;
	static DX12CommandListHookSignatures::CreateCommandList1 OrigCreateCommandList1;

	static DX12CommandListHookSignatures::QueueUpdateTileMappings OrigQueueUpdateTileMappings;
	static DX12CommandListHookSignatures::QueueCopyTileMappings OrigQueueCopyTileMappings;
	static DX12CommandListHookSignatures::QueueExecuteCommandLists OrigQueueExecuteCommandLists;
	static DX12CommandListHookSignatures::QueueSetMarker OrigQueueSetMarker;
	static DX12CommandListHookSignatures::QueueBeginEvent OrigQueueBeginEvent;
	static DX12CommandListHookSignatures::QueueEndEvent OrigQueueEndEvent;
	static DX12CommandListHookSignatures::QueueSignal OrigQueueSignal;
	static DX12CommandListHookSignatures::QueueWait OrigQueueWait;
	static DX12CommandListHookSignatures::QueueGetTimestampFrequency OrigQueueGetTimestampFrequency;
	static DX12CommandListHookSignatures::QueueGetClockCalibration OrigQueueGetClockCalibration;

	static DX12CommandListHookSignatures::CloseCommandList OrigCloseCommandList;
	static DX12CommandListHookSignatures::ResetCommandList OrigResetCommandList;
	static DX12CommandListHookSignatures::ClearState OrigClearState;
	static DX12CommandListHookSignatures::DrawInstanced OrigDrawInstanced;
	static DX12CommandListHookSignatures::DrawIndexedInstanced OrigDrawIndexedInstanced;
	static DX12CommandListHookSignatures::Dispatch OrigDispatch;
	static DX12CommandListHookSignatures::CopyBufferRegion OrigCopyBufferRegion;
	static DX12CommandListHookSignatures::CopyTextureRegion OrigCopyTextureRegion;
	static DX12CommandListHookSignatures::CopyResource OrigCopyResource;
	static DX12CommandListHookSignatures::CopyTiles OrigCopyTiles;
	static DX12CommandListHookSignatures::ResolveSubresource OrigResolveSubresource;
	static DX12CommandListHookSignatures::IaSetPrimitiveTopology OrigIASetPrimitiveTopology;
	static DX12CommandListHookSignatures::RsSetViewports OrigRSSetViewports;
	static DX12CommandListHookSignatures::RsSetScissorRects OrigRSSetScissorRects;
	static DX12CommandListHookSignatures::OmSetBlendFactor OrigOMSetBlendFactor;
	static DX12CommandListHookSignatures::OmSetStencilRef OrigOMSetStencilRef;
	static DX12CommandListHookSignatures::SetPipelineState OrigSetPipelineState;
	static DX12CommandListHookSignatures::ResourceBarrier OrigResourceBarrier;
	static DX12CommandListHookSignatures::ExecuteBundle OrigExecuteBundle;
	static DX12CommandListHookSignatures::SetDescriptorHeaps OrigSetDescriptorHeaps;
	static DX12CommandListHookSignatures::SetRootSignature OrigSetComputeRootSignature;
	static DX12CommandListHookSignatures::SetRootSignature OrigSetGraphicsRootSignature;
	static DX12CommandListHookSignatures::SetRootDescriptorTable OrigSetComputeRootDescriptorTable;
	static DX12CommandListHookSignatures::SetRootDescriptorTable OrigSetGraphicsRootDescriptorTable;
	static DX12CommandListHookSignatures::SetRoot32BitConstant OrigSetComputeRoot32BitConstant;
	static DX12CommandListHookSignatures::SetRoot32BitConstant OrigSetGraphicsRoot32BitConstant;
	static DX12CommandListHookSignatures::SetRoot32BitConstants OrigSetComputeRoot32BitConstants;
	static DX12CommandListHookSignatures::SetRoot32BitConstants OrigSetGraphicsRoot32BitConstants;
	static DX12CommandListHookSignatures::SetRootGpuVa OrigSetComputeRootConstantBufferView;
	static DX12CommandListHookSignatures::SetRootGpuVa OrigSetGraphicsRootConstantBufferView;
	static DX12CommandListHookSignatures::SetRootGpuVa OrigSetComputeRootShaderResourceView;
	static DX12CommandListHookSignatures::SetRootGpuVa OrigSetGraphicsRootShaderResourceView;
	static DX12CommandListHookSignatures::SetRootGpuVa OrigSetComputeRootUnorderedAccessView;
	static DX12CommandListHookSignatures::SetRootGpuVa OrigSetGraphicsRootUnorderedAccessView;
	static DX12CommandListHookSignatures::IaSetIndexBuffer OrigIASetIndexBuffer;
	static DX12CommandListHookSignatures::IaSetVertexBuffers OrigIASetVertexBuffers;
	static DX12CommandListHookSignatures::SoSetTargets OrigSOSetTargets;
	static DX12CommandListHookSignatures::OmSetRenderTargets OrigOMSetRenderTargets;
	static DX12CommandListHookSignatures::ClearDepthStencilView OrigClearDepthStencilView;
	static DX12CommandListHookSignatures::ClearRenderTargetView OrigClearRenderTargetView;
	static DX12CommandListHookSignatures::ClearUavUint OrigClearUnorderedAccessViewUint;
	static DX12CommandListHookSignatures::ClearUavFloat OrigClearUnorderedAccessViewFloat;
	static DX12CommandListHookSignatures::DiscardResource OrigDiscardResource;
	static DX12CommandListHookSignatures::BeginQuery OrigBeginQuery;
	static DX12CommandListHookSignatures::EndQuery OrigEndQuery;
	static DX12CommandListHookSignatures::ResolveQueryData OrigResolveQueryData;
	static DX12CommandListHookSignatures::SetPredication OrigSetPredication;
	static DX12CommandListHookSignatures::CommandListSetMarker OrigCommandListSetMarker;
	static DX12CommandListHookSignatures::CommandListBeginEvent OrigCommandListBeginEvent;
	static DX12CommandListHookSignatures::CommandListEndEvent OrigCommandListEndEvent;
	static DX12CommandListHookSignatures::ExecuteIndirect OrigExecuteIndirect;

	static SRWLOCK IaReplacementSummaryLock;
	static LONG IaReplacementSummaryPresent;
	static volatile LONG IaReplacementSummaryDraws;
	static volatile LONG IaReplacementSummaryIndexedDraws;
	static volatile LONG IaReplacementSummaryDispatches;

	static SRWLOCK HookedVTableLock;
	static std::unordered_set<void*> HookedCommandListVTables;
	static std::unordered_map<void*, UINT32> CommandListVTableHookMasks;
	static SRWLOCK RegisteredListLock;
	static std::unordered_set<void*> RegisteredCommandLists;
	static SRWLOCK KnownCommandQueueLock;
	static IUnknown *KnownCommandQueue;
	static void ReleaseKnownCommandQueue();

	static thread_local DX12ThreadLocalCommandListOriginal CommandListOriginals[64];
	static thread_local DX12ThreadLocalCommandListOriginal CommandQueueOriginals[32];
	static thread_local DX12VtableBaseCache VtableBaseCache;
	static thread_local DX12ThreadLocalIaCallbacksCache IaCallbacksCache;
};



class DX12CommandListHookDiagnostics
{
public:
#if defined(_DEBUG)
	static void LogDX12Call(const char *api, const void *object, const char *fmt = nullptr, ...)
	{
		if (!DX12LogManager::ShouldLogHookCall(api))
			return;

		char detail[1024];
		detail[0] = '\0';
		if (fmt && fmt[0]) {
			va_list args;
			va_start(args, fmt);
			vsnprintf(detail, sizeof(detail), fmt, args);
			va_end(args);
		}

		char apiJson[256];
		char detailJson[2048];
		DX12JsonUtils::EscapeString(apiJson, sizeof(apiJson), api ? api : "");
		DX12JsonUtils::EscapeString(detailJson, sizeof(detailJson), detail);
		DX12LogManager::DebugFields("DX12HookCall",
			"\"api\":%s,\"present\":%ld,\"this\":\"%p\",\"detail\":%s",
			apiJson, DX12RuntimeState::GetPresentCount(), object, detailJson);
	}
#else
	static __forceinline void LogDX12Call(const char *, const void *, const char * = nullptr, ...) {}
#endif

	static void LogQueueStage(
		const char *api, const char *stage, ID3D12CommandQueue *queue,
		UINT count = 0, ID3D12Fence *fence = nullptr, UINT64 value = 0, HRESULT hr = S_OK,
		ULONGLONG elapsedMs = 0)
	{
#if defined(_DEBUG)
		if (!DX12LogManager::VerboseDiagnosticsEnabled())
			return;
		if (SUCCEEDED(hr) && elapsedMs < 4 && stage && !strcmp(stage, "beforeOriginal"))
			return;
		if (SUCCEEDED(hr) && elapsedMs < 4 && api)
			return;
		DX12LogManager::DebugFields("DX12QueueStage",
			"\"api\":\"%s\",\"stage\":\"%s\",\"present\":%ld,\"queue\":\"%p\","
			"\"count\":%u,\"fence\":\"%p\",\"value\":%llu,\"hr\":\"0x%lx\",\"elapsedMs\":%llu",
			api ? api : "", stage ? stage : "", DX12RuntimeState::GetPresentCount(), queue,
			count, fence, static_cast<unsigned long long>(value), hr,
			static_cast<unsigned long long>(elapsedMs));
#else
		(void)api;
		(void)stage;
		(void)queue;
		(void)count;
		(void)fence;
		(void)value;
		(void)hr;
		(void)elapsedMs;
#endif
	}

	static void LogCommandListStage(
		const char *api, const char *stage, ID3D12GraphicsCommandList *commandList,
		HRESULT hr = S_OK, ULONGLONG elapsedMs = 0)
	{
#if defined(_DEBUG)
		if (!DX12LogManager::VerboseDiagnosticsEnabled())
			return;
		if (SUCCEEDED(hr) && elapsedMs < 4)
			return;
		DX12LogManager::DebugFields("DX12CommandListStage",
			"\"api\":\"%s\",\"stage\":\"%s\",\"present\":%ld,\"commandList\":\"%p\","
			"\"hr\":\"0x%lx\",\"elapsedMs\":%llu",
			api ? api : "", stage ? stage : "", DX12RuntimeState::GetPresentCount(), commandList,
			hr, static_cast<unsigned long long>(elapsedMs));
#else
		(void)api;
		(void)stage;
		(void)commandList;
		(void)hr;
		(void)elapsedMs;
#endif
	}

	static void LogCommandListStageEdge(
		const char *api, const char *stage, ID3D12GraphicsCommandList *commandList)
	{
		if (!DX12LogManager::VerboseDiagnosticsEnabled())
			return;
		DX12LogManager::DebugFields("DX12CommandListStage",
			"\"api\":\"%s\",\"stage\":\"%s\",\"present\":%ld,\"commandList\":\"%p\"",
			api ? api : "", stage ? stage : "", DX12RuntimeState::GetPresentCount(), commandList);
	}

	static void FlushIaReplacementExecuteSummaryIfNeeded()
	{
#if defined(_DEBUG)
		constexpr bool kLogIaReplacementExecuteSummary = false;
		if (!kLogIaReplacementExecuteSummary)
			return;
		const LONG present = DX12RuntimeState::GetPresentCount();
		if (DX12CommandListHookState::IaReplacementSummaryPresent == present)
			return;

		AcquireSRWLockExclusive(&DX12CommandListHookState::IaReplacementSummaryLock);
		if (DX12CommandListHookState::IaReplacementSummaryPresent != present) {
			const LONG previousPresent = DX12CommandListHookState::IaReplacementSummaryPresent;
			const LONG draws = InterlockedExchange(&DX12CommandListHookState::IaReplacementSummaryDraws, 0);
			const LONG indexedDraws = InterlockedExchange(&DX12CommandListHookState::IaReplacementSummaryIndexedDraws, 0);
			const LONG dispatches = InterlockedExchange(&DX12CommandListHookState::IaReplacementSummaryDispatches, 0);
			if (previousPresent >= 0 && (draws || dispatches)) {
				DX12LogManager::DebugFields("DX12IaReplacementExecuteSummary",
					"\"present\":%ld,\"draws\":%ld,\"indexedDraws\":%ld,\"dispatches\":%ld",
					previousPresent, draws, indexedDraws, dispatches);
			}
			DX12CommandListHookState::IaReplacementSummaryPresent = present;
		}
		ReleaseSRWLockExclusive(&DX12CommandListHookState::IaReplacementSummaryLock);
#endif
	}

	static void RecordIaReplacementDrawExecute(bool indexed)
	{
		DX12Profiling::RecordIaReplacementDraw(indexed);
#if defined(_DEBUG)
		FlushIaReplacementExecuteSummaryIfNeeded();
		InterlockedIncrement(&DX12CommandListHookState::IaReplacementSummaryDraws);
		if (indexed)
			InterlockedIncrement(&DX12CommandListHookState::IaReplacementSummaryIndexedDraws);
#endif
	}

	static void RecordIaReplacementDispatchExecute()
	{
		DX12Profiling::RecordIaReplacementDispatch();
#if defined(_DEBUG)
		FlushIaReplacementExecuteSummaryIfNeeded();
		InterlockedIncrement(&DX12CommandListHookState::IaReplacementSummaryDispatches);
#endif
	}
};



class DX12OriginalFunctionRegistry
{
public:
	static void LogHookCall(const char *api, const void *object)
	{
		if (!DX12LogManager::ShouldLogHookCall(api))
			return;
		DX12LogManager::DebugFields("DX12HookCall",
			"\"api\":\"%s\",\"present\":%ld,\"this\":\"%p\"",
			api ? api : "", DX12RuntimeState::GetPresentCount(), object);
	}

	static void LogOriginalFallback(const char *api, const void *object, UINT slot, const void *fallback)
	{
		if (!DX12LogManager::ShouldLogHookCall(api))
			return;
		DX12LogManager::DebugFields("DX12FallbackPath",
			"\"kind\":\"original_lookup\",\"api\":\"%s\",\"present\":%ld,\"this\":\"%p\","
			"\"slot\":%u,\"fallback\":\"%p\"",
			api ? api : "", DX12RuntimeState::GetPresentCount(), object, slot, fallback);
	}

	static void *GetVTableSlotTarget(void *object, UINT slot)
	{
		if (!object)
			return nullptr;
		void **vtable = *reinterpret_cast<void***>(object);
		return vtable ? vtable[slot] : nullptr;
	}

	// Resolve all hot vtable slots in one batch and populate the L2 cache.
	// Later ResolveCached calls bypass the global registry lookup.
	template <typename SlotCache, typename VtableCache>
	static void BulkResolveVtableSlots(
		void *object,
		const UINT *slots,
		size_t slotCount,
		SlotCache *slotCaches,
		size_t slotCacheCount,
		VtableCache *vtableCache)
	{
		if (!object || !slots || !slotCount || !vtableCache)
			return;

		void **vtable = *reinterpret_cast<void***>(object);
		if (!vtable)
			return;

		// This vtable was already resolved as a batch.
		if (vtableCache->vtable == vtable)
			return;

		// Reset the cache for the new vtable.
		vtableCache->vtable = vtable;
		vtableCache->originals = {};
		vtableCache->slotResolved = {};

		for (size_t i = 0; i < slotCount; ++i) {
			UINT slot = slots[i];
			if (slot >= vtableCache->originals.size())
				continue;

			void *target = vtable[slot];
			if (!target) {
				vtableCache->slotResolved[slot] = true;
				continue;
			}

			void *original = DX12HookRegistry::GetOriginalFunction(target);
			vtableCache->originals[slot] = original;
			vtableCache->slotResolved[slot] = true;

			// Update the L1 slot cache as well.
			if (slotCaches && slot < slotCacheCount) {
				slotCaches[slot].target = target;
				slotCaches[slot].original = original;
				slotCaches[slot].resolved = true;
			}
		}
	}

	template <typename T>
	static T Resolve(void *object, UINT slot, T fallback, const char *name)
	{
		void *target = GetVTableSlotTarget(object, slot);
		if (target) {
			void *original = DX12HookRegistry::GetOriginalFunction(target);
			if (original)
				return reinterpret_cast<T>(original);
		}

		if (fallback) {
			LogOriginalFallback(name, object, slot, reinterpret_cast<const void*>(fallback));
			return fallback;
		}

		DX12LogManager::Fields(name ? name : "DX12::UnknownOriginal",
			"\"event\":\"MissingOriginal\",\"this\":\"%p\",\"slot\":%u",
			object, slot);
		return nullptr;
	}

	template <typename T, typename SlotCache, typename VtableCache>
	static T ResolveCached(
		void *object, UINT slot, T fallback, const char *name,
		SlotCache *slotCaches, size_t slotCacheCount, VtableCache *vtableCache)
	{
		if (object) {
			void **vtable = *reinterpret_cast<void***>(object);
			if (vtable) {
				void *target = vtable[slot];

				// L1: per-slot TLS cache with the highest expected hit rate.
				if (slotCaches && slot < slotCacheCount &&
					slotCaches[slot].resolved &&
					slotCaches[slot].target == target) {
					if (slotCaches[slot].original)
						return reinterpret_cast<T>(slotCaches[slot].original);
					// Cached negative result: use the fallback directly.
					if (fallback)
						return fallback;
					goto log_missing;
				}

				// L2: batched per-vtable cache.
				if (vtableCache && vtableCache->vtable == vtable &&
					slot < vtableCache->slotResolved.size() &&
					vtableCache->slotResolved[slot]) {
					void *original = vtableCache->originals[slot];
					// Promote the result into L1.
					if (slotCaches && slot < slotCacheCount) {
						slotCaches[slot].target = target;
						slotCaches[slot].original = original;
						slotCaches[slot].resolved = true;
					}
					if (original)
						return reinterpret_cast<T>(original);
					// Cached negative result.
					if (fallback)
						return fallback;
					goto log_missing;
				}

				// L3: global hook registry, used only for the first lookup.
				void *original = DX12HookRegistry::GetOriginalFunction(target);

				// Store positive and negative results in L1.
				if (slotCaches && slot < slotCacheCount) {
					slotCaches[slot].target = target;
					slotCaches[slot].original = original;
					slotCaches[slot].resolved = true;
				}

				// Store positive and negative results in L2.
				if (vtableCache && slot < vtableCache->slotResolved.size()) {
					if (vtableCache->vtable != vtable) {
						vtableCache->vtable = vtable;
						vtableCache->originals = {};
						vtableCache->slotResolved = {};
					}
					vtableCache->originals[slot] = original;
					vtableCache->slotResolved[slot] = true;
				}

				if (original)
					return reinterpret_cast<T>(original);

				// The slot is not hooked; use the fallback.
				if (fallback)
					return fallback;
				goto log_missing;
			}
		}

		if (fallback) {
			LogOriginalFallback(name, object, slot, reinterpret_cast<const void*>(fallback));
			return fallback;
		}

	log_missing:
		DX12LogManager::Fields(name ? name : "DX12::UnknownOriginal",
			"\"event\":\"MissingOriginal\",\"this\":\"%p\",\"slot\":%u",
			object, slot);
		return nullptr;
	}
};



class DX12CommandListHookRegistry
{
public:
	static void RememberCommandListVTable(void *vtable)
	{
		if (!vtable)
			return;
		AcquireSRWLockExclusive(&DX12CommandListHookState::HookedVTableLock);
		DX12CommandListHookState::HookedCommandListVTables.insert(vtable);
		DX12CommandListHookState::CommandListVTableHookMasks.try_emplace(vtable, 0);
		ReleaseSRWLockExclusive(&DX12CommandListHookState::HookedVTableLock);
	}

	static UINT32 GetCommandListVTableMissingHookMask(void *vtable, UINT32 requestedMask)
	{
		if (!vtable || !requestedMask)
			return 0;
		AcquireSRWLockExclusive(&DX12CommandListHookState::HookedVTableLock);
		UINT32 &installedMask = DX12CommandListHookState::CommandListVTableHookMasks[vtable];
		const UINT32 missingMask = requestedMask & ~installedMask;
		installedMask |= missingMask;
		ReleaseSRWLockExclusive(&DX12CommandListHookState::HookedVTableLock);
		return missingMask;
	}

	static void SnapshotCommandListVTables(std::vector<void*> *vtables)
	{
		if (!vtables)
			return;
		vtables->clear();
		AcquireSRWLockShared(&DX12CommandListHookState::HookedVTableLock);
		vtables->reserve(DX12CommandListHookState::HookedCommandListVTables.size());
		for (void *vtable : DX12CommandListHookState::HookedCommandListVTables)
			vtables->push_back(vtable);
		ReleaseSRWLockShared(&DX12CommandListHookState::HookedVTableLock);
	}

	static bool ShouldRegisterCommandListPointer(void *commandList)
	{
		if (!commandList)
			return false;

		AcquireSRWLockShared(&DX12CommandListHookState::RegisteredListLock);
		const bool alreadyRegistered =
			DX12CommandListHookState::RegisteredCommandLists.find(commandList) !=
			DX12CommandListHookState::RegisteredCommandLists.end();
		ReleaseSRWLockShared(&DX12CommandListHookState::RegisteredListLock);
		if (alreadyRegistered)
			return false;

		bool shouldRegister = false;
		AcquireSRWLockExclusive(&DX12CommandListHookState::RegisteredListLock);
		shouldRegister = DX12CommandListHookState::RegisteredCommandLists.insert(commandList).second;
		ReleaseSRWLockExclusive(&DX12CommandListHookState::RegisteredListLock);
		return shouldRegister;
	}

	static void ForgetRegisteredCommandListPointer(void *commandList)
	{
		if (!commandList)
			return;

		AcquireSRWLockExclusive(&DX12CommandListHookState::RegisteredListLock);
		DX12CommandListHookState::RegisteredCommandLists.erase(commandList);
		ReleaseSRWLockExclusive(&DX12CommandListHookState::RegisteredListLock);
	}

	template <typename T>
	static T GetCommandQueueOriginal(ID3D12CommandQueue *queue, UINT slot, T fallback, const char *name)
	{
		return DX12OriginalFunctionRegistry::ResolveCached(
			queue, slot, fallback, name,
			DX12CommandListHookState::CommandQueueOriginals,
			ARRAYSIZE(DX12CommandListHookState::CommandQueueOriginals),
			&DX12CommandListHookState::VtableBaseCache);
	}

	template <typename T>
	static T GetCommandListOriginal(
		ID3D12GraphicsCommandList *commandList, UINT slot, T fallback, const char *name)
	{
		return DX12OriginalFunctionRegistry::ResolveCached(
			commandList, slot, fallback, name,
			DX12CommandListHookState::CommandListOriginals,
			ARRAYSIZE(DX12CommandListHookState::CommandListOriginals),
			&DX12CommandListHookState::VtableBaseCache);
	}
};


class DX12CommandListHooks
{
public:
	static void HookCommandListCreation(IUnknown *device);
	static void HookCommandList(IUnknown *commandList);
	static void ActivateCommandListHotPathHooks();
	static void HookCommandQueue(IUnknown *commandQueue);
	static void RememberCommandQueueForHotPath(IUnknown *commandQueue);
	static void ActivateCommandQueueHotPathHooks();
};


class DX12CommandQueueExecutionState
{
public:
	inline static volatile LONG FirstExecuteLog = 0;
};



class DX12CommandQueueExecutionFlow
{
public:
	class DX12ExecuteSkipLogUtils
	{
	public:
		inline static volatile LONG64 SkipCalls = 0;
		inline static volatile LONG64 SkipCommandLists = 0;
		inline static volatile LONG LastPresentLogged = -1;
		static constexpr LONG PresentInterval = 120;

		static void Record(ID3D12CommandQueue *queue, UINT numCommandLists, const char *stage)
		{
			InterlockedIncrement64(&SkipCalls);
			InterlockedAdd64(&SkipCommandLists, static_cast<LONG64>(numCommandLists));
			const LONG present = DX12RuntimeState::GetPresentCount();
			const LONG last = InterlockedCompareExchange(&LastPresentLogged, 0, 0);
			const bool shouldLog = last < 0 || present >= last + PresentInterval;
			if (!shouldLog)
				return;
			if (InterlockedCompareExchange(&LastPresentLogged, present, last) != last)
				return;
			DX12LogManager::Fields("DX12ExecuteCommandListsHeartbeat",
				"\"stage\":\"%s\",\"mode\":\"aggregate_skip\",\"present\":%ld,\"threadId\":%lu,\"queue\":\"%p\",\"calls\":%lld,\"commandLists\":%lld",
				stage ? stage : "",
				present,
				GetCurrentThreadId(),
				queue,
				static_cast<long long>(InterlockedCompareExchange64(&SkipCalls, 0, 0)),
				static_cast<long long>(InterlockedCompareExchange64(&SkipCommandLists, 0, 0)));
		}
	};

	using RegisterSubmittedCommandListFn = void (*)(IUnknown*);

	static void RegisterSubmittedCommandLists(
		UINT numCommandLists,
		ID3D12CommandList *const *commandLists,
		RegisterSubmittedCommandListFn registerSubmittedCommandList)
	{
		if (!registerSubmittedCommandList || !commandLists)
			return;
		for (UINT i = 0; i < numCommandLists; ++i)
			registerSubmittedCommandList(commandLists[i]);
	}

	static void Execute(
		ID3D12CommandQueue *queue,
		UINT numCommandLists,
		ID3D12CommandList *const *commandLists,
		DX12CommandListHookSignatures::QueueExecuteCommandLists original,
		RegisterSubmittedCommandListFn registerSubmittedCommandList)
	{
		DX12_PROFILE_FUNCTION(ExecuteCommandLists);
		LARGE_INTEGER totalBegin = DX12OuterTimingProbeUtils::Now();
	#if defined(_DEBUG) && defined(DX12_ENABLE_DETAILED_TIMING)
		volatile LONG &firstExecuteLog = DX12CommandQueueExecutionState::FirstExecuteLog;
		if (InterlockedCompareExchange(&firstExecuteLog, 1, 0) == 0)
			DX12LogManager::FieldsFlush("DX12ExecuteCommandListsFirst",
				"\"stage\":\"enter\",\"queue\":\"%p\",\"count\":%u,\"skipAll\":%ld,\"internalReplay\":%s",
				queue, numCommandLists, DX12HotPathState::skipAll,
				DX12InternalReplayState::IsActive() ? "true" : "false");
	#endif

		// Present publishes feature and capture changes, matching DX11's
		// frame-action timing. Submission only consumes that published state.
		LARGE_INTEGER sliceBegin = DX12OuterTimingProbeUtils::Now();
		const bool shouldNotifySubmitted =
			DX12ModNeedsCommandListSubmissionNotifications();
		const bool shouldRegisterSubmitted =
			DX12HuntIsEnabled() ||
			DX12CaptureLedger::CurrentCaptureSessionId() != 0;
		DX12OuterTimingProbeUtils::LogQueueExecuteSlice(
			"read_work_flags",
			queue,
			numCommandLists,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));

		if (DX12HotPathState::skipAll) {
			if (original) {
				DX12ExecuteSkipLogUtils::Record(
					queue, numCommandLists, "BeforeOriginalSkip");
	#if defined(_DEBUG) && defined(DX12_ENABLE_DETAILED_TIMING)
				if (InterlockedCompareExchange(&firstExecuteLog, 2, 1) == 1)
					DX12LogManager::FieldsFlush("DX12ExecuteCommandListsFirst",
						"\"stage\":\"before_original_skip\",\"queue\":\"%p\",\"count\":%u",
						queue, numCommandLists);
	#endif
				sliceBegin = DX12OuterTimingProbeUtils::Now();
				original(queue, numCommandLists, commandLists);
				DX12OuterTimingProbeUtils::LogQueueExecuteSlice(
					"original_skip",
					queue,
					numCommandLists,
					DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
				DX12ExecuteSkipLogUtils::Record(
					queue, numCommandLists, "AfterOriginalSkip");
	#if defined(_DEBUG) && defined(DX12_ENABLE_DETAILED_TIMING)
				if (InterlockedCompareExchange(&firstExecuteLog, 3, 2) == 2)
					DX12LogManager::FieldsFlush("DX12ExecuteCommandListsFirst",
						"\"stage\":\"after_original_skip\",\"queue\":\"%p\",\"count\":%u",
						queue, numCommandLists);
	#endif
			}
			if (shouldNotifySubmitted) {
				sliceBegin = DX12OuterTimingProbeUtils::Now();
				DX12ModNotifyCommandListsSubmitted(queue, numCommandLists, commandLists);
				DX12OuterTimingProbeUtils::LogQueueExecuteSlice(
					"mod_notify_submitted_skip",
					queue,
					numCommandLists,
					DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
			}
			if (shouldRegisterSubmitted) {
				sliceBegin = DX12OuterTimingProbeUtils::Now();
				RegisterSubmittedCommandLists(numCommandLists, commandLists, registerSubmittedCommandList);
				DX12OuterTimingProbeUtils::LogQueueExecuteSlice(
					"register_submitted_skip",
					queue,
					numCommandLists,
					DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
			}
			DX12ExecuteSkipLogUtils::Record(queue, numCommandLists, "ReturnSkip");
			DX12OuterTimingProbeUtils::LogQueueExecuteSlice(
				"total_skip",
				queue,
				numCommandLists,
				DX12OuterTimingProbeUtils::ElapsedUs(totalBegin));
			return;
		}

		if (DX12InternalReplayState::IsActive()) {
			if (original) {
				sliceBegin = DX12OuterTimingProbeUtils::Now();
				original(queue, numCommandLists, commandLists);
				DX12OuterTimingProbeUtils::LogQueueExecuteSlice(
					"original_internal_replay",
					queue,
					numCommandLists,
					DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
			}
			DX12OuterTimingProbeUtils::LogQueueExecuteSlice(
				"total_internal_replay",
				queue,
				numCommandLists,
				DX12OuterTimingProbeUtils::ElapsedUs(totalBegin));
			return;
		}

		DX12CommandListFlowDiagnostics::LogDX12Call(
			"ID3D12CommandQueue::ExecuteCommandLists", queue, " count=%u", numCommandLists);
		if (!original) {
			DX12OuterTimingProbeUtils::LogQueueExecuteSlice(
				"total_no_original",
				queue,
				numCommandLists,
				DX12OuterTimingProbeUtils::ElapsedUs(totalBegin));
			return;
		}

	#if defined(_DEBUG) && defined(DX12_ENABLE_DETAILED_TIMING)
		if (InterlockedCompareExchange(&firstExecuteLog, 4, 1) == 1)
			DX12LogManager::FieldsFlush("DX12ExecuteCommandListsFirst",
				"\"stage\":\"before_original_full\",\"queue\":\"%p\",\"count\":%u",
				queue, numCommandLists);
	#endif
#if defined(_DEBUG) && defined(DX12_ENABLE_DETAILED_TIMING)
		DX12CommandListFlowDiagnostics::LogQueueStage(
			"ExecuteCommandLists", "beforeOriginal", queue, numCommandLists);
		ULONGLONG startTick = GetTickCount64();
#endif
		sliceBegin = DX12OuterTimingProbeUtils::Now();
		original(queue, numCommandLists, commandLists);
		DX12OuterTimingProbeUtils::LogQueueExecuteSlice(
			"original",
			queue,
			numCommandLists,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
	#if defined(_DEBUG) && defined(DX12_ENABLE_DETAILED_TIMING)
		if (InterlockedCompareExchange(&firstExecuteLog, 5, 4) == 4)
			DX12LogManager::FieldsFlush("DX12ExecuteCommandListsFirst",
				"\"stage\":\"after_original_full\",\"queue\":\"%p\",\"count\":%u",
				queue, numCommandLists);
	#endif
#if defined(_DEBUG) && defined(DX12_ENABLE_DETAILED_TIMING)
		ULONGLONG originalEndTick = GetTickCount64();
		DX12CommandListFlowDiagnostics::LogQueueStage(
			"ExecuteCommandLists", "afterOriginal", queue, numCommandLists,
			nullptr, 0, S_OK, originalEndTick - startTick);
#endif
		if (shouldNotifySubmitted) {
			DX12_HOTPATH_TIME_BEGIN(ExecuteCLNotify);
			sliceBegin = DX12OuterTimingProbeUtils::Now();
			DX12ModNotifyCommandListsSubmitted(queue, numCommandLists, commandLists);
			DX12OuterTimingProbeUtils::LogQueueExecuteSlice(
				"mod_notify_submitted",
				queue,
				numCommandLists,
				DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
			DX12_HOTPATH_TIME_END(ExecuteCLNotify);
		}
		if (shouldRegisterSubmitted) {
			sliceBegin = DX12OuterTimingProbeUtils::Now();
			RegisterSubmittedCommandLists(numCommandLists, commandLists, registerSubmittedCommandList);
			DX12OuterTimingProbeUtils::LogQueueExecuteSlice(
				"register_submitted",
				queue,
				numCommandLists,
				DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
		}
#if defined(_DEBUG) && defined(DX12_ENABLE_DETAILED_TIMING)
		DX12CommandListFlowDiagnostics::LogQueueStage(
			"ExecuteCommandLists", "afterNotify", queue, numCommandLists,
			nullptr, 0, S_OK, GetTickCount64() - originalEndTick);
#endif
		DX12OuterTimingProbeUtils::LogQueueExecuteSlice(
			"total",
			queue,
			numCommandLists,
			DX12OuterTimingProbeUtils::ElapsedUs(totalBegin));
	}
};



class DX12CommandQueueState
{
public:
	SRWLOCK lock;
	ID3D12CommandQueue *queue = nullptr;
	volatile LONG nonDirectLogs = 0;

	DX12CommandQueueState()
	{
		InitializeSRWLock(&lock);
	}

	static DX12CommandQueueState &Get()
	{
		static DX12CommandQueueState state;
		return state;
	}

	static void Set(ID3D12CommandQueue *queue)
	{
		DX12CommandQueueState &state = Get();
		if (queue) {
			D3D12_COMMAND_QUEUE_DESC desc = queue->GetDesc();
			if (desc.Type != D3D12_COMMAND_LIST_TYPE_DIRECT) {
				if (InterlockedIncrement(&state.nonDirectLogs) <= 8)
					DX12LogManager::Fields("DX12CommandQueueState",
						"\"event\":\"ignore_non_direct\",\"queue\":\"%p\",\"type\":%d",
						queue, static_cast<int>(desc.Type));
				return;
			}
			DX12LogManager::Fields("DX12CommandQueueState",
				"\"event\":\"accept_direct\",\"queue\":\"%p\",\"type\":%d",
				queue, static_cast<int>(desc.Type));
		}

		if (queue)
			queue->AddRef();

		AcquireSRWLockExclusive(&state.lock);
		ID3D12CommandQueue *oldQueue = state.queue;
		state.queue = queue;
		ReleaseSRWLockExclusive(&state.lock);

		if (oldQueue)
			oldQueue->Release();
	}

	static ID3D12CommandQueue *Acquire()
	{
		DX12CommandQueueState &state = Get();
		AcquireSRWLockShared(&state.lock);
		ID3D12CommandQueue *queue = state.queue;
		if (queue)
			queue->AddRef();
		ReleaseSRWLockShared(&state.lock);
		return queue;
	}
};
