#pragma once

// Binding tracker shards/TLS caches plus IA region and binding export models.
// Split out of Core/DX12CoreModels.h; include that umbrella header from existing code.
#include "../DX12Platform.h"

constexpr size_t kBindingShardCount = 64;

class BindingShard
{
public:
	SRWLOCK lock = SRWLOCK_INIT;
	std::unordered_map<ID3D12GraphicsCommandList*, CommandListBindingState> states;
	std::unordered_map<ID3D12GraphicsCommandList*, std::vector<CommandListRecordedEvent>> recordedEvents;
	volatile UINT64 generation = 0;
};

// Per-thread read cache for binding tracker: avoids SRWLOCK on repeated identical calls.
// Key insight: D3D12 command lists are single-threaded, so TLS cache is safe.
class BindingTlsTableCache
{
public:
	ID3D12GraphicsCommandList *cl = nullptr;
	bool valid[MaxRootParameters] = {};
	UINT64 gpuHandle[MaxRootParameters] = {};
};

class BindingTlsDescriptorCache
{
public:
	ID3D12GraphicsCommandList *cl = nullptr;
	bool valid[MaxRootParameters] = {};
	UINT64 address[MaxRootParameters] = {};
	D3D12_ROOT_PARAMETER_TYPE type[MaxRootParameters] = {};
};

class BindingTlsDescriptorQueryCache
{
public:
	static constexpr UINT CacheSlotsPerPipeline = 3;
	static constexpr UINT CacheSlotCount = CacheSlotsPerPipeline * 2;

	static UINT CacheSlotIndex(bool compute, D3D12_DESCRIPTOR_RANGE_TYPE rangeType)
	{
		UINT rangeIndex = UINT_MAX;
		switch (rangeType) {
		case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
			rangeIndex = 0;
			break;
		case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
			rangeIndex = 1;
			break;
		case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
			rangeIndex = 2;
			break;
		default:
			return UINT_MAX;
		}
		return (compute ? CacheSlotsPerPipeline : 0) + rangeIndex;
	}

	ID3D12GraphicsCommandList *commandList = nullptr;
	bool compute = false;
	UINT rangeType = UINT_MAX;
	UINT64 bindingQuerySerial = 0;
	ID3D12DescriptorHeap *cbvSrvUavHeap = nullptr;
	UINT64 cbvSrvUavHeapVersion = 0;
	std::vector<DX12CurrentComputeUavBinding> bindings;
};

class BindingTlsComputeAllQueryCache
{
public:
	ID3D12GraphicsCommandList *commandList = nullptr;
	UINT64 computeTableHash = 0;
	UINT64 bindingQuerySerial = 0;
	ID3D12DescriptorHeap *cbvSrvUavHeap = nullptr;
	UINT64 cbvSrvUavHeapVersion = 0;
	std::vector<DX12CurrentComputeUavBinding> uavs;
	std::vector<DX12CurrentComputeUavBinding> srvs;
	std::vector<DX12CurrentComputeUavBinding> cbvs;
	std::vector<DX12CurrentRootConstants> constants;
};

// TLS cache for direct CommandListBindingState pointer lookup.
// Eliminates ShardFor() hash + AcquireSRWLockExclusive + HashMap::operator[]
// on every state-mutation call (SetVB, SetIB, SetPSO, SetRootSig, etc.).
// Key insight: D3D12 command lists are strictly single-threaded during recording,
// so treating the unordered_map element pointer as stable within a single
// recording session is safe. Generation counter guards against unordered_map
// rehash (which can invalidate pointers) triggered by RegisterCommandList on
// other threads.
class BindingTlsStateCache
{
public:
	ID3D12GraphicsCommandList *commandList = nullptr;
	CommandListBindingState *state = nullptr;
	UINT64 shardGeneration = 0;
	size_t shardIndex = 0;
};


class DX12BindingTrackerState
{
public:
	static BindingShard Shards[kBindingShardCount];
	static volatile LONG64 GlobalDrawSerial;
	static volatile LONG64 GlobalDispatchSerial;

	static BindingShard &ShardFor(ID3D12GraphicsCommandList *commandList)
	{
		uintptr_t key = reinterpret_cast<uintptr_t>(commandList);
		key ^= key >> 7;
		key *= 0x9e3779b97f4a7c15ull;
		return Shards[(key >> 17) & (kBindingShardCount - 1)];
	}
};


class DX12FrameAnalysisDumpResult
{
public:
	bool ok = false;
	size_t written = 0;
	size_t skipped = 0;
	size_t failed = 0;
	const char *reason = "";
};


uint32_t DX12HashIaBufferContentView(
	UINT64 gpuVirtualAddress, UINT64 size, UINT stride, UINT format, UINT slot);

class DX12IaRegionDraw
{
public:
	bool indexed = false;
	UINT vertexCount = 0;
	UINT indexCount = 0;
	UINT startVertexLocation = 0;
	UINT startIndexLocation = 0;
	INT baseVertexLocation = 0;

	static DX12IaRegionDraw ForDrawInstanced(UINT vertexCount, UINT startVertexLocation)
	{
		DX12IaRegionDraw draw;
		draw.indexed = false;
		draw.vertexCount = vertexCount;
		draw.startVertexLocation = startVertexLocation;
		return draw;
	}

	static DX12IaRegionDraw ForDrawIndexedInstanced(
		UINT indexCount, UINT startIndexLocation, INT baseVertexLocation)
	{
		DX12IaRegionDraw draw;
		draw.indexed = true;
		draw.indexCount = indexCount;
		draw.startIndexLocation = startIndexLocation;
		draw.baseVertexLocation = baseVertexLocation;
		return draw;
	}

	static DX12IaRegionDraw FromDrawArguments(
		bool indexed, UINT vertexCount, UINT indexCount, UINT startVertexLocation,
		UINT startIndexLocation, INT baseVertexLocation)
	{
		return indexed ?
			ForDrawIndexedInstanced(indexCount, startIndexLocation, baseVertexLocation) :
			ForDrawInstanced(vertexCount, startVertexLocation);
	}

	UINT EstimatedVertexCountForRegionHash() const
	{
		if (vertexCount)
			return vertexCount;
		if (!indexCount)
			return 0;
		return (3 * indexCount + 10) / 20 + 3;
	}

	UINT FirstVertexForRegionHash() const
	{
		if (!indexed)
			return startVertexLocation;
		// D3D12 BaseVertexLocation is signed; negative values move indexed reads
		// before index 0, so hashing must not reinterpret them as huge UINT offsets.
		if (baseVertexLocation <= 0)
			return 0;
		return static_cast<UINT>(baseVertexLocation);
	}
};

class DX12IaRegion
{
public:
	UINT64 gpuVa = 0;
	UINT64 size = 0;
	UINT64 viewOffset = 0;
	uint32_t hash = 0;

	bool SetFromViewRange(UINT64 viewGpuVa, UINT64 viewBytes, UINT64 offset, UINT64 bytes)
	{
		*this = DX12IaRegion();
		if (!viewGpuVa || !viewBytes || !bytes)
			return false;
		if (offset >= viewBytes)
			return false;
		if (bytes > viewBytes - offset)
			bytes = viewBytes - offset;
		if (!bytes)
			return false;
		gpuVa = viewGpuVa + offset;
		size = bytes;
		viewOffset = offset;
		return true;
	}

	bool SetFromIndexBufferView(
		const D3D12_INDEX_BUFFER_VIEW &view, const DX12IaRegionDraw &draw)
	{
		const UINT stride = IndexFormatStride(view.Format);
		return SetFromViewRange(
			view.BufferLocation,
			view.SizeInBytes,
			static_cast<UINT64>(stride) * draw.startIndexLocation,
			static_cast<UINT64>(stride) * draw.indexCount);
	}

	bool SetFromVertexBufferView(
		UINT slot, const D3D12_VERTEX_BUFFER_VIEW &view, const DX12IaRegionDraw &draw)
	{
		(void)slot;
		if (!view.StrideInBytes)
			return false;

		return SetFromViewRange(
			view.BufferLocation,
			view.SizeInBytes,
			static_cast<UINT64>(view.StrideInBytes) * draw.FirstVertexForRegionHash(),
			static_cast<UINT64>(view.StrideInBytes) * draw.EstimatedVertexCountForRegionHash());
	}

	uint32_t HashIndexBuffer(UINT format)
	{
		hash = DX12HashIaBufferContentView(gpuVa, size, 0, format, 0);
		return hash;
	}

	uint32_t HashVertexBuffer(UINT stride, UINT slot)
	{
		hash = DX12HashIaBufferContentView(gpuVa, size, stride, 0, slot);
		return hash;
	}

	static uint32_t HashIndexBufferView(
		const D3D12_INDEX_BUFFER_VIEW &view, const DX12IaRegionDraw &draw)
	{
		DX12IaRegion region;
		if (!region.SetFromIndexBufferView(view, draw))
			return 0;
		return region.HashIndexBuffer(static_cast<UINT>(view.Format));
	}

	static uint32_t HashVertexBufferView(
		UINT slot, const D3D12_VERTEX_BUFFER_VIEW &view, const DX12IaRegionDraw &draw)
	{
		DX12IaRegion region;
		if (!region.SetFromVertexBufferView(slot, view, draw))
			return 0;
		return region.HashVertexBuffer(view.StrideInBytes, slot);
	}

	static UINT IndexFormatStride(DXGI_FORMAT format)
	{
		return format == DXGI_FORMAT_R32_UINT ? 4 : 2;
	}
};


class FlatBufferRow
{
public:
	std::string id;
	std::string role;
	std::string skinningClass;
	UINT64 eventSerial = 0;
	UINT64 drawId = 0;
	UINT64 dispatchId = 0;
	UINT64 psoIndex = 0;
	DX12PsoShaderInfo shaderInfo = {};
	UINT64 producerEventSerial = 0;
	UINT64 producerDrawId = 0;
	UINT64 producerDispatchId = 0;
	UINT64 producerPsoIndex = 0;
	DX12PsoShaderInfo producerShaderInfo = {};
	std::string producerBindSpace;
	UINT producerRootParameterIndex = 0;
	UINT producerShaderRegister = UINT_MAX;
	UINT64 gpuVa = 0;
	UINT64 size = 0;
	UINT64 viewGpuVa = 0;
	UINT64 viewSize = 0;
	UINT64 viewOffset = 0;
	UINT64 drawGpuVa = 0;
	UINT64 drawSize = 0;
	UINT64 drawOffset = 0;
	UINT stride = 0;
	UINT slot = 0;
	UINT format = 0;
	UINT drawStartIndex = 0;
	UINT drawIndexCount = 0;
	INT drawBaseVertex = 0;
	UINT drawStartInstance = 0;
	UINT drawInstanceCount = 0;
	D3D12_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
	DX12PsoInputLayoutInfo inputLayout;
	uint32_t huntHash = 0;
	bool resolved = false;
	DX12BufferResourceSummary resource = {};
};

class BufferProducer
{
public:
	UINT64 eventSerial = 0;
	UINT64 drawId = 0;
	UINT64 dispatchId = 0;
	UINT64 psoIndex = 0;
	DX12PsoShaderInfo shaderInfo = {};
	std::string bindSpace;
	UINT rootParameterIndex = 0;
	UINT shaderRegister = UINT_MAX;
	ID3D12Resource *resource = nullptr;
	UINT64 begin = 0;
	UINT64 end = 0;
};

class DescriptorSlotKey
{
public:
	ID3D12DescriptorHeap *heap = nullptr;
	UINT64 descriptorIndex = 0;

	bool operator==(const DescriptorSlotKey &other) const
	{
		return heap == other.heap && descriptorIndex == other.descriptorIndex;
	}
};

class DescriptorSlotKeyHash
{
public:
	size_t operator()(const DescriptorSlotKey &key) const
	{
		uintptr_t heap = reinterpret_cast<uintptr_t>(key.heap);
		uint64_t mixed = static_cast<uint64_t>(heap >> 4) ^ (key.descriptorIndex * 0x9e3779b97f4a7c15ull);
		mixed ^= mixed >> 33;
		mixed *= 0xff51afd7ed558ccdull;
		mixed ^= mixed >> 33;
		return static_cast<size_t>(mixed);
	}
};

class DescriptorLookup
{
public:
	std::unordered_map<SIZE_T, const DX12DescriptorSummary*> byCpuHandle;
	std::unordered_map<DescriptorSlotKey, const DX12DescriptorSummary*, DescriptorSlotKeyHash> bySlot;
};

class DX12FrameEventSource
{
public:
	const std::vector<BindingEvent> *events = nullptr;
	const std::vector<OutputBindingEvent> *outputEvents = nullptr;
	std::vector<BindingEvent> ownedEvents;
	std::vector<OutputBindingEvent> ownedOutputEvents;
	UINT64 droppedEvents = 0;
	UINT64 droppedOutputEvents = 0;
	UINT64 captureSessionId = 0;

	const std::vector<BindingEvent> &GetEvents() const
	{
		static const std::vector<BindingEvent> emptyEvents;
		return events ? *events : emptyEvents;
	}

	const std::vector<OutputBindingEvent> &GetOutputEvents() const
	{
		static const std::vector<OutputBindingEvent> emptyEvents;
		return outputEvents ? *outputEvents : emptyEvents;
	}
};

class DX12BindingTracker
{
public:
	static bool RegisterCommandList(ID3D12GraphicsCommandList *commandList);
	static bool UnregisterCommandList(ID3D12GraphicsCommandList *commandList);
	static void ResetCommandList(ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *initialState);
	static bool SetPipelineState(ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState);
	static void RecordStateEvent(ID3D12GraphicsCommandList *commandList, const char *kind);
	static bool SetDescriptorHeaps(ID3D12GraphicsCommandList *commandList, UINT count, ID3D12DescriptorHeap *const *heaps);
	static bool SetDescriptorHeaps(ID3D12GraphicsCommandList *commandList, ID3D12DescriptorHeap *cbvSrvUavHeap, ID3D12DescriptorHeap *samplerHeap);
	static bool SetGraphicsRootDescriptorTable(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor);
	static bool SetComputeRootDescriptorTable(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor);
	static bool SetGraphicsRootSignature(ID3D12GraphicsCommandList *commandList, ID3D12RootSignature *rootSignature);
	static bool SetComputeRootSignature(ID3D12GraphicsCommandList *commandList, ID3D12RootSignature *rootSignature);
	static void SetGraphicsRootDescriptor(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address);
	static void SetComputeRootDescriptor(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address);
	static void SetGraphicsRoot32BitConstant(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT destOffset, UINT value);
	static void SetComputeRoot32BitConstant(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT destOffset, UINT value);
	static void SetGraphicsRoot32BitConstants(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT destOffset, UINT count, const void *values);
	static void SetComputeRoot32BitConstants(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT destOffset, UINT count, const void *values);
	static void SetPrimitiveTopology(ID3D12GraphicsCommandList *commandList, D3D12_PRIMITIVE_TOPOLOGY topology);
	static void SetIndexBuffer(ID3D12GraphicsCommandList *commandList, const D3D12_INDEX_BUFFER_VIEW *view);
	static void SetVertexBuffers(ID3D12GraphicsCommandList *commandList, UINT startSlot, UINT count, const D3D12_VERTEX_BUFFER_VIEW *views);
	static bool GetCurrentShaderResourceBindings(ID3D12GraphicsCommandList *commandList, bool compute, D3D12_DESCRIPTOR_RANGE_TYPE rangeType, std::vector<DX12CurrentShaderResourceBinding> *bindings);
	static bool GetCurrentShaderResourceBindingsByRegister(ID3D12GraphicsCommandList *commandList, bool compute, D3D12_DESCRIPTOR_RANGE_TYPE rangeType, UINT shaderRegister, UINT registerSpace, std::vector<DX12CurrentShaderResourceBinding> *bindings);
	static bool GetCurrentComputeAll(ID3D12GraphicsCommandList *commandList, std::vector<DX12CurrentComputeUavBinding> *uavs, std::vector<DX12CurrentComputeUavBinding> *srvs, std::vector<DX12CurrentComputeUavBinding> *cbvs, std::vector<DX12CurrentRootConstants> *constants);
	static bool CollectPreSkinBindings(
		ID3D12GraphicsCommandList *commandList,
		std::vector<DX12CurrentComputeUavBinding> *uavs,
		std::vector<DX12CurrentComputeUavBinding> *srvs,
		std::vector<DX12CurrentComputeUavBinding> *cbvs,
		std::vector<DX12CurrentRootConstants> *constants,
		UINT64 *dependencyFingerprint = nullptr);
	static bool GetSimulateContextPreSkinBindings(
		ID3D12GraphicsCommandList *commandList,
		const std::vector<DX12CurrentComputeUavBinding> **uavs,
		const std::vector<DX12CurrentComputeUavBinding> **srvs,
		const std::vector<DX12CurrentComputeUavBinding> **cbvs,
		const std::vector<DX12CurrentRootConstants> **constants,
		UINT64 *dependencyFingerprint = nullptr,
		bool *foundAny = nullptr);
	static bool ValidateRuntimePreSkinBindingState(
		const DX12SimulateContext *runtimeState);
	static UINT64 GetComputeBindingSerial(ID3D12GraphicsCommandList *commandList);
	static bool GetCurrentDescriptorHeaps(ID3D12GraphicsCommandList *commandList, ID3D12DescriptorHeap **cbvSrvUavHeap, ID3D12DescriptorHeap **samplerHeap);
	static void RecordDrawInstanced(ID3D12GraphicsCommandList *commandList, UINT vertexCountPerInstance, UINT instanceCount, UINT startVertexLocation, UINT startInstanceLocation);
	static void RecordDrawIndexedInstanced(ID3D12GraphicsCommandList *commandList, UINT indexCountPerInstance, UINT instanceCount, UINT startIndexLocation, INT baseVertexLocation, UINT startInstanceLocation);
	static void RecordDispatch(ID3D12GraphicsCommandList *commandList, UINT threadGroupCountX, UINT threadGroupCountY, UINT threadGroupCountZ);
	static void BeginFrame();
	static void DrainPendingEvents(const char *reason);
	static UINT64 CurrentCaptureSessionId();
	static UINT64 CaptureFenceSequence();
	static void SubmitCommandList(ID3D12GraphicsCommandList *commandList);
	static bool PopulateCaptureEventSource(DX12FrameEventSource *source, UINT64 captureSessionId, const char *reason);
	static bool PopulateActiveEventSource(DX12FrameEventSource *source, const char *reason);

	static void ReleaseRetainedIaResources(CommandListBindingState *state);
	static void ReleaseRecordedEvent(CommandListRecordedEvent *event);
	static void ReleaseRecordedEventsLocked(BindingShard *shard, ID3D12GraphicsCommandList *commandList);
	static void RecordTemplateEvent(ID3D12GraphicsCommandList *commandList, const BindingEvent &event);
	static void RecordTemplateOutputEvent(ID3D12GraphicsCommandList *commandList, const OutputBindingEvent &event);
	static void SubmitRecordedEvent(const CommandListRecordedEvent &recorded, UINT64 captureSessionId);
	static void StoreEventInCaptureLedger(const BindingEvent &event);
	static void StoreOutputEventInCaptureLedger(const OutputBindingEvent &event);
	static void RetainStateIaResourcesForAsync(CommandListBindingState *state);
	static void RetainEventIaResources(BindingEvent *event);
	static void ReleaseEventIaResources(BindingEvent *event);
	static void RefreshPipelineShaderInfo(CommandListBindingState *state);
	static void InvalidatePipelineShaderInfo(CommandListBindingState *state);
	static void BumpBindingQuerySerial(CommandListBindingState *state);
	static bool ResolvePipelineShaderInfoOnDemand(CommandListBindingState *state);
	static bool SnapshotBindingState(
		ID3D12GraphicsCommandList *commandList,
		CommandListBindingState *snapshot,
		UINT64 *bindingQuerySerial = nullptr);
	static void ReleaseResolvedIaResource(
		DX12BufferResourceSummary *summary, bool *resolved);
	static void ResolveAndRetainIaResource(
		UINT64 gpuVirtualAddress, UINT64 size,
		DX12BufferResourceSummary *summary, bool *resolved);
	// Resolves deferred IA views on a snapshot copy (used by the queue's
	// full-state packets); safe without the shard lock on a private copy.
	static void ResolveDeferredIaResourcesForSnapshot(CommandListBindingState *state);
	static bool BuildEvent(BindingEvent *event, const char *kind, ID3D12GraphicsCommandList *commandList, const CommandListBindingState &state, UINT64 descriptorEpoch, UINT64 captureSessionId = 0);
	static bool BuildEvent(BindingEvent *event, const char *kind, ID3D12GraphicsCommandList *commandList, const CommandListBindingState &state);
	static void StoreEventLocked(const BindingEvent &event);
	static void StoreEventLocked(const char *kind, ID3D12GraphicsCommandList *commandList, const CommandListBindingState &state);
	static void StoreOutputEventLocked(const char *kind, ID3D12GraphicsCommandList *commandList, const CommandListBindingState &state, UINT slot, SIZE_T cpuHandle, UINT64 drawId, UINT64 captureSessionId = 0);
	static void StoreCurrentOutputTargetsLocked(ID3D12GraphicsCommandList *commandList, const CommandListBindingState &state, UINT64 captureSessionId = 0, UINT64 drawSerial = 0);
	static bool GetCurrentDescriptors(ID3D12GraphicsCommandList *commandList, bool compute, D3D12_DESCRIPTOR_RANGE_TYPE rangeType, const char *descriptorKind, D3D12_ROOT_PARAMETER_TYPE rootDescriptorType, std::vector<DX12CurrentComputeUavBinding> *uavs);
	static bool SetRootConstantsLocked(RootConstantsState *constants, UINT rootParameterIndex, UINT destOffset, UINT count, const void *values);
};

class DX12BindingFrameExport
{
public:
	static void GetCurrentFrameShaderInfos(std::vector<DX12PsoShaderInfo> *shaderInfos);
	static void GetCurrentFrameIaBuffersWithResourceBindings(
		std::vector<DX12FrameIaBufferBinding> *buffers,
		const std::vector<DX12FrameResourceBinding> &resourceBindings);
	static void BuildIaBufferFileName(const DX12FrameIaBufferBinding &buffer, wchar_t *fileName, size_t fileNameCount);
	static DX12FrameAnalysisDumpResult DumpBindingTrace(const wchar_t *dir);

	static void AppendShaderHashNamePart(wchar_t *text, size_t textCount, const char *stage, bool hasHash, UINT64 hash);
	static void BuildShaderHashNamePart(const DX12PsoShaderInfo &info, wchar_t *text, size_t textCount);
	static const char *TopologyName(D3D12_PRIMITIVE_TOPOLOGY topology);
	static std::string NextBufferId(const char *prefix, UINT *nextId);
	static DX12IaRegionDraw IaRegionDrawFromEvent(const BindingEvent &event);
	static bool IaRegionForEvent(const BindingEvent &event, const char *role, UINT slot, DX12IaRegion *region);
	static uint32_t IaViewHashForEvent(const BindingEvent &event, const char *role, UINT slot);
	static bool ProjectResolvedIaResourceToView(
		UINT64 viewGpuVa, UINT64 viewSize, UINT64 drawGpuVa, UINT64 drawSize,
		const DX12BufferResourceSummary &resolvedDrawResource,
		DX12BufferResourceSummary *viewResource);
	static bool ResolveIaBufferResourceSummary(
		UINT64 viewGpuVa, UINT64 viewSize, UINT64 drawGpuVa, UINT64 drawSize,
		const DX12BufferResourceSummary &existingResource, bool existingResolved,
		DX12BufferResourceSummary *resource);
	static bool ResolveIaBufferResourceSummaryFromCombinedRange(
		UINT64 viewGpuVa,
		UINT64 viewSize,
		UINT64 drawGpuVa,
		UINT64 drawSize,
		DX12BufferResourceSummary *resource);
	static bool ResolveIaBufferResourceSummary(
		const DX12FrameIaBufferBinding &buffer, DX12BufferResourceSummary *resource);
	static void AddFrameIaBufferRow(std::vector<FlatBufferRow> *rows, UINT *nextVbId, UINT *nextIbId, const BindingEvent &event, const char *role, UINT slot);
	static void CollectFrameIaBuffersForEvents(const std::vector<BindingEvent> &events, std::vector<FlatBufferRow> *buffers);
	static bool RangesOverlap(UINT64 aBegin, UINT64 aSize, UINT64 bBegin, UINT64 bSize);
	static bool BindingIsUavBufferProducer(const DX12FrameResourceBinding &binding);
	static BufferProducer ProducerFromBinding(const DX12FrameResourceBinding &binding);
	static bool IsHuntingUavStateProducer(const BufferProducer &producer);
	static bool ProducerMatchesBuffer(const FlatBufferRow &row, const BufferProducer &producer);
	static bool ProducerIsPreferredForIaBuffer(const BufferProducer &candidate, const BufferProducer *best);
	static const BufferProducer *FindLatestProducerForBuffer(const FlatBufferRow &row, const std::vector<BufferProducer> &producers);
	static const BufferProducer *FindLatestHuntingUavStateProducerForBuffer(const FlatBufferRow &row, const std::vector<BufferProducer> &producers);
	static void ClassifyIaBufferSkinning(std::vector<FlatBufferRow> *buffers, const std::vector<DX12FrameResourceBinding> &resourceBindings);
	static DX12FrameIaBufferBinding FrameIaBufferFromFlatRow(const FlatBufferRow &row);
};

class DX12BindingResourceExport
{
public:
	static void GetCurrentFrameResourceBindings(std::vector<DX12FrameResourceBinding> *bindings);
	static void GetCurrentFrameOutputBindings(std::vector<DX12FrameOutputBinding> *bindings);

	static bool InsertBindingSeen(std::unordered_set<std::string> *seen, const BindingEvent &event, const char *bindSpace, UINT rootParameterIndex, UINT rangeIndex, UINT rangeType, ID3D12DescriptorHeap *heap, UINT64 descriptorIndex, bool rootDescriptor);
	static bool DescriptorMatchesRangeType(const DX12DescriptorSummary *descriptor, UINT rangeType);
	static const DX12DescriptorSummary *DescriptorForExpectedRange(const DX12DescriptorSummary *descriptor, UINT rangeType);
	static const char *DescriptorKindForRangeType(UINT rangeType);
	static const char *ExpectedBindingKind(const DX12FrameResourceBinding &binding);
	static bool DescriptorBelongsToHeap(const DX12DescriptorSummary &descriptor, const DX12DescriptorHeapSummary &heap, UINT64 *descriptorIndex);
	static void BuildDescriptorLookup(const std::vector<DX12DescriptorSummary> &descriptors, const std::vector<DX12DescriptorHeapSummary> &heaps, DescriptorLookup *lookup);
	static const DX12DescriptorSummary *FindDescriptorForSlot(const DescriptorLookup &lookup, const DX12DescriptorHeapSummary *heap, UINT64 descriptorIndex, SIZE_T cpuHandle);
	static const DX12DescriptorSummary *FindDescriptorForEventSlot(DX12DescriptorSummary *historyStorage, const BindingEvent &event, const DescriptorLookup &lookup, const DX12DescriptorHeapSummary *heap, UINT64 descriptorIndex, SIZE_T cpuHandle, UINT rangeType, size_t *historyLookups = nullptr, size_t *historyHits = nullptr);
	static UINT UnknownTableRangeType(UINT heapType);
	static bool MakeTableBinding(DX12FrameResourceBinding *binding, const BindingEvent &event, const char *bindSpace, const RootTableState &table, UINT rangeIndex, UINT rangeType, UINT shaderVisibility, UINT shaderRegister, UINT registerSpace, UINT descriptorOffset, const DX12DescriptorHeapSummary *heap, UINT64 descriptorIndex, UINT64 gpuHandle, SIZE_T cpuHandle, const DX12DescriptorSummary *descriptor);
	static bool CollectDescriptorTableEntry(std::vector<DX12FrameResourceBinding> *bindings, const BindingEvent &event, const char *bindSpace, const RootTableState &table, UINT rangeIndex, UINT rangeType, UINT shaderVisibility, UINT shaderRegister, UINT registerSpace, UINT descriptorOffset, const DX12DescriptorHeapSummary *heap, UINT64 descriptorIndex, UINT64 gpuHandle, SIZE_T cpuHandle, const DX12DescriptorSummary *descriptor, std::unordered_set<std::string> *seen, size_t *untrackedSkipped);
	static bool VisitTableStartOnly(const RootTableState &table, UINT heapType, const std::vector<DX12DescriptorHeapSummary> &heaps, UINT64 *descriptorIndex, UINT64 *gpuHandle, SIZE_T *cpuHandle, const DX12DescriptorHeapSummary **heap);
	static bool IsCompatibleRangeForSpace(const DX12RootDescriptorRangeSummary &range, const char *bindSpace);
	static bool IsTextureResourceDimension(D3D12_RESOURCE_DIMENSION dimension);
	static void CollectFrameResourceBinding(std::vector<DX12FrameResourceBinding> *bindings, const BindingEvent &event, const char *bindSpace, const RootTableState &table, UINT heapType, const std::vector<DX12DescriptorHeapSummary> &heaps, const DescriptorLookup &descriptors, std::unordered_set<std::string> *seen, size_t *untrackedSkipped, size_t *historyLookups = nullptr, size_t *historyHits = nullptr);
	static void CollectFrameRootDescriptorBinding(std::vector<DX12FrameResourceBinding> *bindings, const BindingEvent &event, const char *bindSpace, const RootDescriptorState &rootDescriptor, std::unordered_set<std::string> *seen);
	static void CountEventTables(const BindingEvent &event, const std::vector<DX12DescriptorHeapSummary> &heaps, size_t *graphicsTables, size_t *computeTables, size_t *graphicsTablesWithRoots, size_t *computeTablesWithRoots, size_t *rootRangeTables, size_t *tableStartOnly, size_t *unboundedRanges, size_t *unboundedDescriptors, size_t *graphicsTablesWithoutRoots, size_t *computeTablesWithoutRoots, size_t *descriptorTablesWithoutRanges);
	static void CollectEventResourceBindings(std::vector<DX12FrameResourceBinding> *bindings, const BindingEvent &event, const std::vector<DX12DescriptorHeapSummary> &heaps, const DescriptorLookup &descriptorLookup, std::unordered_set<std::string> *seen, size_t *untrackedSkipped, size_t *historyLookups = nullptr, size_t *historyHits = nullptr);
};
