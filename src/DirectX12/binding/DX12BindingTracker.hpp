#pragma once

#include <Windows.h>
#include <d3d12.h>

#include <stdio.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "DX12BindingTrackerState.hpp"
#include "DX12BindingTrackerTypes.hpp"
#include "DX12FrameAnalysisTypes.hpp"
#include "DX12IaRegion.hpp"

class DX12BindingTableRef
{
public:
	const char *space = "";
	const RootTableState *table = nullptr;
	UINT heapType = 0;
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
	UINT stride = 0;
	UINT slot = 0;
	UINT format = 0;
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

class DX12BindingTracker
{
public:
	static void RegisterCommandList(ID3D12GraphicsCommandList *commandList);
	static void ResetCommandList(ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *initialState);
	static void SetPipelineState(ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState);
	static void RecordStateEvent(ID3D12GraphicsCommandList *commandList, const char *kind);
	static bool SetDescriptorHeaps(ID3D12GraphicsCommandList *commandList, UINT count, ID3D12DescriptorHeap *const *heaps);
	static bool SetDescriptorHeaps(ID3D12GraphicsCommandList *commandList, ID3D12DescriptorHeap *cbvSrvUavHeap, ID3D12DescriptorHeap *samplerHeap);
	static void SetGraphicsRootDescriptorTable(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor);
	static void SetComputeRootDescriptorTable(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor);
	static void SetGraphicsRootSignature(ID3D12GraphicsCommandList *commandList, ID3D12RootSignature *rootSignature);
	static void SetComputeRootSignature(ID3D12GraphicsCommandList *commandList, ID3D12RootSignature *rootSignature);
	static void SetGraphicsRootDescriptor(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address);
	static void SetComputeRootDescriptor(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address);
	static void SetGraphicsRoot32BitConstant(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT destOffset, UINT value);
	static void SetComputeRoot32BitConstant(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT destOffset, UINT value);
	static void SetGraphicsRoot32BitConstants(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT destOffset, UINT count, const void *values);
	static void SetComputeRoot32BitConstants(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT destOffset, UINT count, const void *values);
	static void SetPrimitiveTopology(ID3D12GraphicsCommandList *commandList, D3D12_PRIMITIVE_TOPOLOGY topology);
	static void SetIndexBuffer(ID3D12GraphicsCommandList *commandList, const D3D12_INDEX_BUFFER_VIEW *view);
	static void SetVertexBuffers(ID3D12GraphicsCommandList *commandList, UINT startSlot, UINT count, const D3D12_VERTEX_BUFFER_VIEW *views);
	static void RecordOutputTargets(ID3D12GraphicsCommandList *commandList, UINT numRenderTargetDescriptors, const D3D12_CPU_DESCRIPTOR_HANDLE *renderTargetDescriptors, BOOL singleHandleToDescriptorRange, const D3D12_CPU_DESCRIPTOR_HANDLE *depthStencilDescriptor);
	static void RecordRenderTargetClear(ID3D12GraphicsCommandList *commandList, D3D12_CPU_DESCRIPTOR_HANDLE renderTargetView);
	static void RecordDepthStencilClear(ID3D12GraphicsCommandList *commandList, D3D12_CPU_DESCRIPTOR_HANDLE depthStencilView);
	static bool GetCurrentIaState(ID3D12GraphicsCommandList *commandList, DX12CurrentIaState *state);
	static bool GetCurrentComputeUavs(ID3D12GraphicsCommandList *commandList, std::vector<DX12CurrentComputeUavBinding> *uavs);
	static bool GetCurrentComputeSrvs(ID3D12GraphicsCommandList *commandList, std::vector<DX12CurrentComputeUavBinding> *srvs);
	static bool GetCurrentComputeCbvs(ID3D12GraphicsCommandList *commandList, std::vector<DX12CurrentComputeUavBinding> *cbvs);
	static bool GetCurrentShaderResourceBindings(ID3D12GraphicsCommandList *commandList, bool compute, D3D12_DESCRIPTOR_RANGE_TYPE rangeType, std::vector<DX12CurrentShaderResourceBinding> *bindings);
	static bool GetCurrentComputeRootConstants(ID3D12GraphicsCommandList *commandList, std::vector<DX12CurrentRootConstants> *constants);
	static bool GetCurrentComputeAll(ID3D12GraphicsCommandList *commandList, std::vector<DX12CurrentComputeUavBinding> *uavs, std::vector<DX12CurrentComputeUavBinding> *srvs, std::vector<DX12CurrentComputeUavBinding> *cbvs, std::vector<DX12CurrentRootConstants> *constants);
	static UINT64 GetComputeBindingSerial(ID3D12GraphicsCommandList *commandList);
	static bool GetCurrentDescriptorHeaps(ID3D12GraphicsCommandList *commandList, ID3D12DescriptorHeap **cbvSrvUavHeap, ID3D12DescriptorHeap **samplerHeap);
	static void RecordDrawInstanced(ID3D12GraphicsCommandList *commandList, UINT vertexCountPerInstance, UINT instanceCount, UINT startVertexLocation, UINT startInstanceLocation);
	static void RecordDrawIndexedInstanced(ID3D12GraphicsCommandList *commandList, UINT indexCountPerInstance, UINT instanceCount, UINT startIndexLocation, INT baseVertexLocation, UINT startInstanceLocation);
	static void RecordDispatch(ID3D12GraphicsCommandList *commandList, UINT threadGroupCountX, UINT threadGroupCountY, UINT threadGroupCountZ);
	static void BeginFrame();

	static void ReleaseRetainedIaResources(CommandListBindingState *state);
	static void RetainEventIaResources(BindingEvent *event);
	static void ReleaseEventIaResources(BindingEvent *event);
	static bool BuildEvent(BindingEvent *event, const char *kind, ID3D12GraphicsCommandList *commandList, const CommandListBindingState &state);
	static void StoreEventLocked(const BindingEvent &event);
	static void StoreEventLocked(const char *kind, ID3D12GraphicsCommandList *commandList, const CommandListBindingState &state);
	static void StoreOutputEventLocked(const char *kind, ID3D12GraphicsCommandList *commandList, const CommandListBindingState &state, UINT slot, SIZE_T cpuHandle, UINT64 drawId);
	static void StoreCurrentOutputTargetsLocked(ID3D12GraphicsCommandList *commandList, const CommandListBindingState &state);
	static bool GetCurrentDescriptors(ID3D12GraphicsCommandList *commandList, bool compute, D3D12_DESCRIPTOR_RANGE_TYPE rangeType, const char *descriptorKind, D3D12_ROOT_PARAMETER_TYPE rootDescriptorType, std::vector<DX12CurrentComputeUavBinding> *uavs);
	static void SetRootConstantsLocked(RootConstantsState *constants, UINT rootParameterIndex, UINT destOffset, UINT count, const void *values);
};

class DX12BindingFrameExport
{
public:
	static void GetCurrentFrameShaderInfos(std::vector<DX12PsoShaderInfo> *shaderInfos);
	static void GetCurrentFrameIaBuffers(std::vector<DX12FrameIaBufferBinding> *buffers);
	static void GetCurrentFrameIaBuffersWithResourceBindings(
		std::vector<DX12FrameIaBufferBinding> *buffers,
		const std::vector<DX12FrameResourceBinding> &resourceBindings);
	static void BuildIaBufferFileName(const DX12FrameIaBufferBinding &buffer, wchar_t *fileName, size_t fileNameCount);
	static DX12FrameAnalysisDumpResult DumpBindingTrace(const wchar_t *dir);

	static void WriteShaderInfo(FILE *file, const DX12PsoShaderInfo &info);
	static void FormatOptionalHashText(bool hasHash, UINT64 hash, char *text, size_t textCount);
	static void AppendShaderHashNamePart(wchar_t *text, size_t textCount, const char *stage, bool hasHash, UINT64 hash);
	static void BuildShaderHashNamePart(const DX12PsoShaderInfo &info, wchar_t *text, size_t textCount);
	static void WriteRootTables(FILE *file, const RootTableState *tables);
	static void WriteVertexBufferSlots(FILE *file, const BindingEvent &event);
	static const char *TopologyName(D3D12_PRIMITIVE_TOPOLOGY topology);
	static std::string NextBufferId(const char *prefix, UINT *nextId);
	static DX12IaRegionDraw IaRegionDrawFromEvent(const BindingEvent &event);
	static bool IaRegionForEvent(const BindingEvent &event, const char *role, UINT slot, DX12IaRegion *region);
	static uint32_t IaRegionHashForEvent(const BindingEvent &event, const char *role, UINT slot);
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
	static bool IsBufferResourceSummary(const DX12ResourceSummary &resource);
	static void CollectHuntingUavStateResourceUsages(std::vector<DX12FrameResourceBinding> *bindings, const std::vector<BindingEvent> &events);
	static void CollectActivePreSkinProducerBindings(std::vector<DX12FrameResourceBinding> *bindings);
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
