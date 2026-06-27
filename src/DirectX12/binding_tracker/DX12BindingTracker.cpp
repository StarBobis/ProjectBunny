#include "DX12BindingTracker.h"
#include "DX12BindingTrackerPrivate.h"

#include <stdio.h>
#include <string.h>

#include "DX12CommandListRuntime.h"
#include "DX12ResourceTracker.h"

BindingShard gBindingShards[kBindingShardCount];
SRWLOCK gEventsLock = SRWLOCK_INIT;
std::vector<BindingEvent> gEvents;
UINT64 gEventSerial = 0;
volatile LONG64 gGlobalDrawSerial = 0;
volatile LONG64 gGlobalDispatchSerial = 0;
UINT64 gDroppedEvents = 0;

static thread_local BindingTlsTableCache tlsGraphicsTableCache;
static thread_local BindingTlsTableCache tlsComputeTableCache;
static thread_local BindingTlsDescriptorCache tlsGraphicsDescCache;
static thread_local BindingTlsDescriptorCache tlsComputeDescCache;

static const DX12RootParameterSummary *FindRootParameter(
	const DX12RootSignatureSummary &rootSignature, UINT rootParameterIndex);
static bool GetRootSignatureForEvent(
	const BindingEvent &event, bool compute, DX12RootSignatureSummary *summary);
static void StoreEventLocked(
	const char *kind, ID3D12GraphicsCommandList *commandList,
	const CommandListBindingState &state)
{
	if (gEvents.size() >= MaxTrackedEvents) {
		gDroppedEvents++;
		return;
	}

	BindingEvent event;
	event.serial = ++gEventSerial;
	event.kind = kind ? kind : "";
	const bool isDrawKind = kind && (!strcmp(kind, "draw") || !strcmp(kind, "draw_indexed"));
	const bool isDispatchKind = kind && !strcmp(kind, "dispatch");
	event.drawId = isDrawKind ? state.drawSerial : 0;
	event.dispatchId = isDispatchKind ? state.dispatchSerial : 0;
	event.commandList = commandList;
	event.pipelineState = state.pipelineState;
	event.graphicsRootSignature = state.graphicsRootSignature;
	event.computeRootSignature = state.computeRootSignature;
	event.cbvSrvUavHeap = state.cbvSrvUavHeap;
	event.samplerHeap = state.samplerHeap;
	memcpy(event.graphicsTables, state.graphicsTables, sizeof(event.graphicsTables));
	memcpy(event.computeTables, state.computeTables, sizeof(event.computeTables));
	memcpy(event.graphicsRootDescriptors, state.graphicsRootDescriptors, sizeof(event.graphicsRootDescriptors));
	memcpy(event.computeRootDescriptors, state.computeRootDescriptors, sizeof(event.computeRootDescriptors));
	memcpy(event.graphicsRootConstants, state.graphicsRootConstants, sizeof(event.graphicsRootConstants));
	memcpy(event.computeRootConstants, state.computeRootConstants, sizeof(event.computeRootConstants));
	memcpy(event.vertexBuffers, state.vertexBuffers, sizeof(event.vertexBuffers));
	memcpy(event.vertexBufferValid, state.vertexBufferValid, sizeof(event.vertexBufferValid));
	event.indexBuffer = state.indexBuffer;
	event.indexBufferValid = state.indexBufferValid;
	event.primitiveTopology = state.primitiveTopology;
	if (state.pipelineState) {
		DX12GetPipelineStateShaderInfo(state.pipelineState, &event.shaderInfo);
		ID3D12RootSignature *psoRootSignature = nullptr;
		if (DX12GetPsoRootSignature(event.shaderInfo.psoIndex, &psoRootSignature)) {
			if (!event.graphicsRootSignature)
				event.graphicsRootSignature = psoRootSignature;
			if (!event.computeRootSignature)
				event.computeRootSignature = psoRootSignature;
		}
	}
	gEvents.push_back(event);
}

void DX12BindingRegisterCommandList(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return;

	BindingShard &shard = BindingShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	shard.states.try_emplace(commandList);
	ReleaseSRWLockExclusive(&shard.lock);
}

void DX12BindingResetCommandList(
	ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *initialState)
{
	if (!commandList)
		return;

	BindingShard &shard = BindingShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	CommandListBindingState &state = shard.states[commandList];
	state = CommandListBindingState();
	state.pipelineState = initialState;
	ReleaseSRWLockExclusive(&shard.lock);

	tlsGraphicsTableCache.cl = nullptr;
	tlsComputeTableCache.cl = nullptr;
	tlsGraphicsDescCache.cl = nullptr;
	tlsComputeDescCache.cl = nullptr;
}

void DX12BindingSetPipelineState(
	ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState)
{
	if (!commandList)
		return;

	BindingShard &shard = BindingShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	shard.states[commandList].pipelineState = pipelineState;
	ReleaseSRWLockExclusive(&shard.lock);
}

void DX12BindingSetGraphicsRootSignature(
	ID3D12GraphicsCommandList *commandList, ID3D12RootSignature *rootSignature)
{
	if (!commandList)
		return;

	BindingShard &shard = BindingShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	shard.states[commandList].graphicsRootSignature = rootSignature;
	ReleaseSRWLockExclusive(&shard.lock);
}

void DX12BindingSetComputeRootSignature(
	ID3D12GraphicsCommandList *commandList, ID3D12RootSignature *rootSignature)
{
	if (!commandList)
		return;

	BindingShard &shard = BindingShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	CommandListBindingState &state = shard.states[commandList];
	state.computeRootSignature = rootSignature;
	++state.computeBindingSerial;
	ReleaseSRWLockExclusive(&shard.lock);
}

void DX12BindingRecordStateEvent(ID3D12GraphicsCommandList *commandList, const char *kind)
{
	if (!commandList)
		return;

	CommandListBindingState snapshot;
	bool found = false;
	BindingShard &shard = BindingShardFor(commandList);
	AcquireSRWLockShared(&shard.lock);
	auto it = shard.states.find(commandList);
	if (it != shard.states.end()) {
		snapshot = it->second;
		found = true;
	}
	ReleaseSRWLockShared(&shard.lock);
	if (!found)
		return;

	AcquireSRWLockExclusive(&gEventsLock);
	StoreEventLocked(kind ? kind : "state", commandList, snapshot);
	ReleaseSRWLockExclusive(&gEventsLock);
}

bool DX12BindingSetDescriptorHeaps(
	ID3D12GraphicsCommandList *commandList, UINT count,
	ID3D12DescriptorHeap *const *heaps)
{
	if (!commandList)
		return false;

	ID3D12DescriptorHeap *cbvSrvUavHeap = nullptr;
	ID3D12DescriptorHeap *samplerHeap = nullptr;
	for (UINT i = 0; i < count && heaps; ++i) {
		ID3D12DescriptorHeap *heap = heaps[i];
		if (!heap)
			continue;
		D3D12_DESCRIPTOR_HEAP_DESC desc = heap->GetDesc();
		if (desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
			cbvSrvUavHeap = heap;
		else if (desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
			samplerHeap = heap;
	}

	return DX12BindingSetDescriptorHeaps(commandList, cbvSrvUavHeap, samplerHeap);
}

bool DX12BindingSetDescriptorHeaps(
	ID3D12GraphicsCommandList *commandList,
	ID3D12DescriptorHeap *cbvSrvUavHeap,
	ID3D12DescriptorHeap *samplerHeap)
{
	if (!commandList)
		return false;

	BindingShard &shard = BindingShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	CommandListBindingState &state = shard.states[commandList];
	if (state.cbvSrvUavHeap == cbvSrvUavHeap &&
	    state.samplerHeap == samplerHeap) {
		ReleaseSRWLockExclusive(&shard.lock);
		return false;
	}
	state.cbvSrvUavHeap = cbvSrvUavHeap;
	state.samplerHeap = samplerHeap;
	++state.computeBindingSerial;
	ReleaseSRWLockExclusive(&shard.lock);
	return true;
}

void DX12BindingSetGraphicsRootDescriptorTable(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor)
{
	if (!commandList || rootParameterIndex >= MaxRootParameters)
		return;
	if (tlsGraphicsTableCache.cl == commandList &&
		tlsGraphicsTableCache.gpuHandle[rootParameterIndex] == baseDescriptor.ptr)
		return;

	BindingShard &shard = BindingShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	RootTableState &table = shard.states[commandList].graphicsTables[rootParameterIndex];
	if (table.valid && table.baseDescriptor.ptr == baseDescriptor.ptr) {
		ReleaseSRWLockExclusive(&shard.lock);
		return;
	}
	table.valid = true;
	table.rootParameterIndex = rootParameterIndex;
	table.baseDescriptor = baseDescriptor;
	ReleaseSRWLockExclusive(&shard.lock);
	tlsGraphicsTableCache.cl = commandList;
	tlsGraphicsTableCache.gpuHandle[rootParameterIndex] = baseDescriptor.ptr;
}

void DX12BindingSetComputeRootDescriptorTable(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor)
{
	if (!commandList || rootParameterIndex >= MaxRootParameters)
		return;
	if (tlsComputeTableCache.cl == commandList &&
		tlsComputeTableCache.gpuHandle[rootParameterIndex] == baseDescriptor.ptr)
		return;

	BindingShard &shard = BindingShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	RootTableState &table = shard.states[commandList].computeTables[rootParameterIndex];
	if (table.valid && table.baseDescriptor.ptr == baseDescriptor.ptr) {
		ReleaseSRWLockExclusive(&shard.lock);
		return;
	}
	table.valid = true;
	table.rootParameterIndex = rootParameterIndex;
	table.baseDescriptor = baseDescriptor;
	++shard.states[commandList].computeBindingSerial;
	ReleaseSRWLockExclusive(&shard.lock);
	tlsComputeTableCache.cl = commandList;
	tlsComputeTableCache.gpuHandle[rootParameterIndex] = baseDescriptor.ptr;
}

void DX12BindingSetPrimitiveTopology(
	ID3D12GraphicsCommandList *commandList, D3D12_PRIMITIVE_TOPOLOGY topology)
{
	if (!commandList)
		return;

	BindingShard &shard = BindingShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	shard.states[commandList].primitiveTopology = topology;
	ReleaseSRWLockExclusive(&shard.lock);
}

void DX12BindingSetIndexBuffer(
	ID3D12GraphicsCommandList *commandList, const D3D12_INDEX_BUFFER_VIEW *view)
{
	if (!commandList)
		return;

	BindingShard &shard = BindingShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	CommandListBindingState &state = shard.states[commandList];
	if (view) {
		state.indexBuffer = *view;
		state.indexBufferValid = true;
	} else {
		state.indexBuffer = {};
		state.indexBufferValid = false;
	}
	ReleaseSRWLockExclusive(&shard.lock);
}

void DX12BindingSetVertexBuffers(
	ID3D12GraphicsCommandList *commandList, UINT startSlot, UINT count,
	const D3D12_VERTEX_BUFFER_VIEW *views)
{
	if (!commandList || startSlot >= MaxVertexBufferSlots)
		return;

	BindingShard &shard = BindingShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	CommandListBindingState &state = shard.states[commandList];
	for (UINT i = 0; i < count && startSlot + i < MaxVertexBufferSlots; ++i) {
		const UINT slot = startSlot + i;
		if (views) {
			state.vertexBuffers[slot] = views[i];
			state.vertexBufferValid[slot] = views[i].BufferLocation != 0 && views[i].SizeInBytes != 0;
		} else {
			state.vertexBuffers[slot] = {};
			state.vertexBufferValid[slot] = false;
		}
	}
	ReleaseSRWLockExclusive(&shard.lock);
}

bool DX12BindingGetCurrentIaState(
	ID3D12GraphicsCommandList *commandList, DX12CurrentIaState *state)
{
	if (!commandList || !state)
		return false;

	*state = DX12CurrentIaState();
	CommandListBindingState snapshot;
	bool found = false;

	BindingShard &shard = BindingShardFor(commandList);
	AcquireSRWLockShared(&shard.lock);
	auto it = shard.states.find(commandList);
	if (it != shard.states.end()) {
		snapshot = it->second;
		found = true;
	}
	ReleaseSRWLockShared(&shard.lock);

	if (!found)
		return false;

	for (UINT slot = 0; slot < MaxVertexBufferSlots; ++slot) {
		if (!snapshot.vertexBufferValid[slot])
			continue;
		const D3D12_VERTEX_BUFFER_VIEW &view = snapshot.vertexBuffers[slot];
		DX12CurrentIaBuffer buffer;
		buffer.role = "VB";
		buffer.gpuVa = view.BufferLocation;
		buffer.size = view.SizeInBytes;
		buffer.stride = view.StrideInBytes;
		buffer.slot = slot;
		buffer.resolved = DX12ResolveBufferResourceByGpuVa(
			buffer.gpuVa, buffer.size, &buffer.resource);
		state->vertexBuffers.push_back(buffer);
	}

	if (snapshot.indexBufferValid && snapshot.indexBuffer.BufferLocation &&
		snapshot.indexBuffer.SizeInBytes) {
		state->hasIndexBuffer = true;
		state->indexBuffer.role = "IB";
		state->indexBuffer.gpuVa = snapshot.indexBuffer.BufferLocation;
		state->indexBuffer.size = snapshot.indexBuffer.SizeInBytes;
		state->indexBuffer.format = static_cast<UINT>(snapshot.indexBuffer.Format);
		state->indexBuffer.resolved = DX12ResolveBufferResourceByGpuVa(
			state->indexBuffer.gpuVa, state->indexBuffer.size, &state->indexBuffer.resource);
	}

	return true;
}


static const DX12RootParameterSummary *FindRootParameter(
	const DX12RootSignatureSummary &rootSignature, UINT rootParameterIndex)
{
	for (const DX12RootParameterSummary &parameter : rootSignature.parameters) {
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
static bool GetCurrentDescriptors(
	ID3D12GraphicsCommandList *commandList,
	bool compute,
	D3D12_DESCRIPTOR_RANGE_TYPE rangeType,
	const char *descriptorKind,
	D3D12_ROOT_PARAMETER_TYPE rootDescriptorType,
	std::vector<DX12CurrentComputeUavBinding> *uavs)
{
	if (!commandList || !uavs)
		return false;

	uavs->clear();
	CommandListBindingState snapshot;
	bool found = false;

	BindingShard &shard = BindingShardFor(commandList);
	AcquireSRWLockShared(&shard.lock);
	auto it = shard.states.find(commandList);
	if (it != shard.states.end()) {
		snapshot = it->second;
		found = true;
	}
	ReleaseSRWLockShared(&shard.lock);

	if (!found)
		return false;

	BindingEvent event;
	event.pipelineState = snapshot.pipelineState;
	event.graphicsRootSignature = snapshot.graphicsRootSignature;
	event.computeRootSignature = snapshot.computeRootSignature;
	if (snapshot.pipelineState) {
		DX12GetPipelineStateShaderInfo(snapshot.pipelineState, &event.shaderInfo);
		ID3D12RootSignature *psoRootSignature = nullptr;
		if (DX12GetPsoRootSignature(event.shaderInfo.psoIndex, &psoRootSignature)) {
			if (compute && !event.computeRootSignature)
				event.computeRootSignature = psoRootSignature;
			if (!compute && !event.graphicsRootSignature)
				event.graphicsRootSignature = psoRootSignature;
		}
	}

	DX12RootSignatureSummary rootSignature;
	const bool hasRootSignature = GetRootSignatureForEvent(event, compute, &rootSignature);
	for (UINT root = 0; root < MaxRootParameters; ++root) {
		const RootTableState &table = compute ?
			snapshot.computeTables[root] : snapshot.graphicsTables[root];
		if (table.valid) {
			const DX12RootParameterSummary *parameter =
				hasRootSignature ? FindRootParameter(rootSignature, root) : nullptr;
			if (parameter && parameter->parameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
				for (const DX12RootDescriptorRangeSummary &range : parameter->ranges) {
					if (range.rangeType != rangeType)
						continue;
					DX12DescriptorHeapSummary heap = {};
					if (!DX12FindDescriptorHeapByGpuHandle(
						table.baseDescriptor, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, &heap) ||
						heap.increment == 0)
						continue;
					const UINT64 tableBaseIndex =
						(table.baseDescriptor.ptr - heap.gpuStart) / heap.increment;
					const UINT count = DX12BindingDescriptorCountForRange(heap, tableBaseIndex, range);
					for (UINT offset = 0; offset < count; ++offset) {
						const UINT64 descriptorIndex =
							tableBaseIndex + range.effectiveOffset + offset;
						if (descriptorIndex >= heap.numDescriptors)
							break;
						const SIZE_T cpuHandle =
							heap.cpuStart + static_cast<SIZE_T>(descriptorIndex) * heap.increment;
						DX12DescriptorSummary descriptor = {};
						if (!DX12FindDescriptorSummaryByCpuHandle(cpuHandle, &descriptor) ||
							descriptor.kind != descriptorKind)
							continue;
						DX12CurrentComputeUavBinding binding;
						binding.rootParameterIndex = root;
						binding.rangeIndex = range.rangeIndex;
						binding.shaderVisibility = range.shaderVisibility;
						binding.shaderRegister = range.baseShaderRegister + offset;
						binding.registerSpace = range.registerSpace;
						binding.descriptorOffset = range.effectiveOffset + offset;
						binding.descriptorIncrement = heap.increment;
						binding.tableCopyCount = count;
						binding.tableCpuStart =
							heap.cpuStart + static_cast<SIZE_T>(tableBaseIndex) * heap.increment;
						binding.tableGpuStart.ptr =
							heap.gpuStart + tableBaseIndex * heap.increment;
						binding.tableHeap = heap.heap;
						binding.descriptor = descriptor;
						binding.hasDescriptor = true;
						uavs->push_back(std::move(binding));
					}
				}
			} else {
				DX12DescriptorHeapSummary heap = {};
				if (DX12FindDescriptorHeapByGpuHandle(
					table.baseDescriptor, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, &heap) &&
					heap.increment) {
					const UINT64 descriptorIndex =
						(table.baseDescriptor.ptr - heap.gpuStart) / heap.increment;
					const SIZE_T cpuHandle =
						heap.cpuStart + static_cast<SIZE_T>(descriptorIndex) * heap.increment;
					DX12DescriptorSummary descriptor = {};
					if (DX12FindDescriptorSummaryByCpuHandle(cpuHandle, &descriptor) &&
						descriptor.kind == descriptorKind) {
						DX12CurrentComputeUavBinding binding;
						binding.rootParameterIndex = root;
						binding.shaderVisibility = parameter ?
							parameter->shaderVisibility : D3D12_SHADER_VISIBILITY_ALL;
						binding.descriptorIncrement = heap.increment;
						binding.tableCopyCount = 1;
						binding.tableCpuStart = cpuHandle;
						binding.tableGpuStart = table.baseDescriptor;
						binding.tableHeap = heap.heap;
						binding.descriptor = descriptor;
						binding.hasDescriptor = true;
						uavs->push_back(std::move(binding));
					}
				}
			}
		}

		const RootDescriptorState &rootDescriptor = compute ?
			snapshot.computeRootDescriptors[root] : snapshot.graphicsRootDescriptors[root];
		if (rootDescriptor.valid && rootDescriptor.type == rootDescriptorType) {
			DX12CurrentComputeUavBinding binding;
			binding.rootParameterIndex = root;
			if (hasRootSignature) {
				const DX12RootParameterSummary *parameter =
					FindRootParameter(rootSignature, root);
				if (parameter) {
					binding.shaderVisibility = parameter->shaderVisibility;
					binding.shaderRegister = parameter->shaderRegister;
					binding.registerSpace = parameter->registerSpace;
				}
			}
			binding.rootDescriptor = true;
			binding.gpuVirtualAddress = rootDescriptor.address;
			uavs->push_back(std::move(binding));
		}
	}

	return !uavs->empty();
}

bool DX12BindingGetCurrentComputeUavs(
	ID3D12GraphicsCommandList *commandList,
	std::vector<DX12CurrentComputeUavBinding> *uavs)
{
	return GetCurrentDescriptors(
		commandList, true, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, "UAV",
		D3D12_ROOT_PARAMETER_TYPE_UAV, uavs);
}

bool DX12BindingGetCurrentComputeSrvs(
	ID3D12GraphicsCommandList *commandList,
	std::vector<DX12CurrentComputeUavBinding> *srvs)
{
	return GetCurrentDescriptors(
		commandList, true, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, "SRV",
		D3D12_ROOT_PARAMETER_TYPE_SRV, srvs);
}

bool DX12BindingGetCurrentComputeCbvs(
	ID3D12GraphicsCommandList *commandList,
	std::vector<DX12CurrentComputeUavBinding> *cbvs)
{
	return GetCurrentDescriptors(
		commandList, true, D3D12_DESCRIPTOR_RANGE_TYPE_CBV, "CBV",
		D3D12_ROOT_PARAMETER_TYPE_CBV, cbvs);
}

bool DX12BindingGetCurrentComputeAll(
	ID3D12GraphicsCommandList *commandList,
	std::vector<DX12CurrentComputeUavBinding> *uavs,
	std::vector<DX12CurrentComputeUavBinding> *srvs,
	std::vector<DX12CurrentComputeUavBinding> *cbvs,
	std::vector<DX12CurrentRootConstants> *constants)
{
	if (!commandList)
		return false;
	if (uavs) uavs->clear();
	if (srvs) srvs->clear();
	if (cbvs) cbvs->clear();
	if (constants) constants->clear();

	CommandListBindingState snapshot;
	bool found = false;
	BindingShard &shard = BindingShardFor(commandList);
	AcquireSRWLockShared(&shard.lock);
	auto it = shard.states.find(commandList);
	if (it != shard.states.end()) {
		snapshot = it->second;
		found = true;
	}
	ReleaseSRWLockShared(&shard.lock);
	if (!found) return false;

	BindingEvent event;
	event.pipelineState = snapshot.pipelineState;
	event.graphicsRootSignature = snapshot.graphicsRootSignature;
	event.computeRootSignature = snapshot.computeRootSignature;
	if (snapshot.pipelineState) {
		DX12GetPipelineStateShaderInfo(snapshot.pipelineState, &event.shaderInfo);
		ID3D12RootSignature *psoRootSignature = nullptr;
		if (DX12GetPsoRootSignature(event.shaderInfo.psoIndex, &psoRootSignature)) {
			if (!event.computeRootSignature)
				event.computeRootSignature = psoRootSignature;
		}
	}

	DX12RootSignatureSummary rootSignature;
	const bool hasRootSignature = GetRootSignatureForEvent(event, true, &rootSignature);

	for (UINT root = 0; root < MaxRootParameters; ++root) {
		const RootTableState &table = snapshot.computeTables[root];
		if (table.valid) {
			const DX12RootParameterSummary *parameter =
				hasRootSignature ? FindRootParameter(rootSignature, root) : nullptr;
			if (parameter && parameter->parameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
				DX12DescriptorHeapSummary heap = {};
				if (!DX12FindDescriptorHeapByGpuHandle(table.baseDescriptor,
					D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, &heap) || heap.increment == 0)
					continue;
				const UINT64 tableBase = (table.baseDescriptor.ptr - heap.gpuStart) / heap.increment;
				for (const DX12RootDescriptorRangeSummary &range : parameter->ranges) {
					std::vector<DX12CurrentComputeUavBinding> *out = nullptr;
					const char *kind = nullptr;
					if (range.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV && uavs) { out = uavs; kind = "UAV"; }
					else if (range.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV && srvs) { out = srvs; kind = "SRV"; }
					else if (range.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_CBV && cbvs) { out = cbvs; kind = "CBV"; }
					else { continue; }

					const UINT count = DX12BindingDescriptorCountForRange(heap, tableBase, range);
					for (UINT offset = 0; offset < count; ++offset) {
						const UINT64 di = tableBase + range.effectiveOffset + offset;
						if (di >= heap.numDescriptors) break;
						const SIZE_T cpuHandle = heap.cpuStart + static_cast<SIZE_T>(di) * heap.increment;
						DX12DescriptorSummary desc = {};
						if (!DX12FindDescriptorSummaryByCpuHandle(cpuHandle, &desc) || desc.kind != kind)
							continue;
						DX12CurrentComputeUavBinding b;
						b.rootParameterIndex = root;
						b.rangeIndex = range.rangeIndex;
						b.shaderVisibility = range.shaderVisibility;
						b.shaderRegister = range.baseShaderRegister + offset;
						b.registerSpace = range.registerSpace;
						b.descriptorOffset = range.effectiveOffset + offset;
						b.descriptorIncrement = heap.increment;
						b.tableCopyCount = count;
						b.tableCpuStart = heap.cpuStart + static_cast<SIZE_T>(tableBase) * heap.increment;
						b.tableGpuStart.ptr = heap.gpuStart + tableBase * heap.increment;
						b.tableHeap = heap.heap;
						b.descriptor = desc;
						b.hasDescriptor = true;
						out->push_back(std::move(b));
					}
				}
			} else {
				DX12DescriptorHeapSummary heap = {};
				if (!DX12FindDescriptorHeapByGpuHandle(table.baseDescriptor,
					D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, &heap) || !heap.increment)
					continue;
				const UINT64 di = (table.baseDescriptor.ptr - heap.gpuStart) / heap.increment;
				const SIZE_T cpuHandle = heap.cpuStart + static_cast<SIZE_T>(di) * heap.increment;
				DX12DescriptorSummary desc = {};
				if (!DX12FindDescriptorSummaryByCpuHandle(cpuHandle, &desc))
					continue;
				std::vector<DX12CurrentComputeUavBinding> *out = nullptr;
				if (desc.kind == "UAV" && uavs) out = uavs;
				else if (desc.kind == "SRV" && srvs) out = srvs;
				else if (desc.kind == "CBV" && cbvs) out = cbvs;
				if (!out) continue;
				DX12CurrentComputeUavBinding b;
				b.rootParameterIndex = root;
				b.shaderVisibility = parameter ? parameter->shaderVisibility : D3D12_SHADER_VISIBILITY_ALL;
				b.descriptorIncrement = heap.increment;
				b.tableCopyCount = 1;
				b.tableCpuStart = cpuHandle;
				b.tableGpuStart = table.baseDescriptor;
				b.tableHeap = heap.heap;
				b.descriptor = desc;
				b.hasDescriptor = true;
				out->push_back(std::move(b));
			}
		}

		const RootDescriptorState &rd = snapshot.computeRootDescriptors[root];
		if (rd.valid) {
			std::vector<DX12CurrentComputeUavBinding> *out = nullptr;
			if (rd.type == D3D12_ROOT_PARAMETER_TYPE_UAV && uavs) out = uavs;
			else if (rd.type == D3D12_ROOT_PARAMETER_TYPE_SRV && srvs) out = srvs;
			else if (rd.type == D3D12_ROOT_PARAMETER_TYPE_CBV && cbvs) out = cbvs;
			if (out) {
				DX12CurrentComputeUavBinding b;
				b.rootParameterIndex = root;
				if (hasRootSignature) {
					const DX12RootParameterSummary *p = FindRootParameter(rootSignature, root);
					if (p) {
						b.shaderVisibility = p->shaderVisibility;
						b.shaderRegister = p->shaderRegister;
						b.registerSpace = p->registerSpace;
					}
				}
				b.rootDescriptor = true;
				b.gpuVirtualAddress = rd.address;
				out->push_back(std::move(b));
			}
		}

		if (constants) {
			const RootConstantsState &rc = snapshot.computeRootConstants[root];
			if (rc.valid) {
				DX12CurrentRootConstants c;
				c.rootParameterIndex = root;
				c.maxSet = rc.maxSet;
				memcpy(c.values, rc.values, sizeof(c.values));
				memcpy(c.valueValid, rc.valueValid, sizeof(c.valueValid));
				constants->push_back(c);
			}
		}
	}

	return (uavs && !uavs->empty()) || (srvs && !srvs->empty()) ||
		(cbvs && !cbvs->empty()) || (constants && !constants->empty());
}

bool DX12BindingGetCurrentShaderResourceBindings(
	ID3D12GraphicsCommandList *commandList,
	bool compute,
	D3D12_DESCRIPTOR_RANGE_TYPE rangeType,
	std::vector<DX12CurrentShaderResourceBinding> *bindings)
{
	const char *descriptorKind = nullptr;
	D3D12_ROOT_PARAMETER_TYPE rootDescriptorType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	switch (rangeType) {
	case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
		descriptorKind = "CBV";
		rootDescriptorType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		break;
	case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
		descriptorKind = "SRV";
		rootDescriptorType = D3D12_ROOT_PARAMETER_TYPE_SRV;
		break;
	case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
		descriptorKind = "UAV";
		rootDescriptorType = D3D12_ROOT_PARAMETER_TYPE_UAV;
		break;
	default:
		if (bindings)
			bindings->clear();
		return false;
	}

	return GetCurrentDescriptors(
		commandList, compute, rangeType, descriptorKind,
		rootDescriptorType, bindings);
}

bool DX12BindingGetCurrentDescriptorHeaps(
	ID3D12GraphicsCommandList *commandList,
	ID3D12DescriptorHeap **cbvSrvUavHeap,
	ID3D12DescriptorHeap **samplerHeap)
{
	return DX12CommandListRuntimeGetDescriptorHeaps(
		commandList, cbvSrvUavHeap, samplerHeap);
}

void DX12BindingRecordDrawInstanced(
	ID3D12GraphicsCommandList *commandList, UINT vertexCountPerInstance,
	UINT instanceCount, UINT startVertexLocation, UINT startInstanceLocation)
{
	if (!commandList)
		return;

	CommandListBindingState snapshot;
	const UINT64 drawSerial = static_cast<UINT64>(InterlockedIncrement64(&gGlobalDrawSerial));
	BindingShard &shard = BindingShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	CommandListBindingState &state = shard.states[commandList];
	state.drawSerial = drawSerial;
	snapshot = state;
	ReleaseSRWLockExclusive(&shard.lock);

	AcquireSRWLockExclusive(&gEventsLock);
	const size_t before = gEvents.size();
	StoreEventLocked("draw", commandList, snapshot);
	if (gEvents.size() > before) {
		BindingEvent &event = gEvents.back();
		event.vertexCountPerInstance = vertexCountPerInstance;
		event.instanceCount = instanceCount;
		event.startVertexLocation = startVertexLocation;
		event.startInstanceLocation = startInstanceLocation;
	}
	ReleaseSRWLockExclusive(&gEventsLock);
}

void DX12BindingRecordDrawIndexedInstanced(
	ID3D12GraphicsCommandList *commandList, UINT indexCountPerInstance,
	UINT instanceCount, UINT startIndexLocation, INT baseVertexLocation,
	UINT startInstanceLocation)
{
	if (!commandList)
		return;

	CommandListBindingState snapshot;
	const UINT64 drawSerial = static_cast<UINT64>(InterlockedIncrement64(&gGlobalDrawSerial));
	BindingShard &shard = BindingShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	CommandListBindingState &state = shard.states[commandList];
	state.drawSerial = drawSerial;
	snapshot = state;
	ReleaseSRWLockExclusive(&shard.lock);

	AcquireSRWLockExclusive(&gEventsLock);
	const size_t before = gEvents.size();
	StoreEventLocked("draw_indexed", commandList, snapshot);
	if (gEvents.size() > before) {
		BindingEvent &event = gEvents.back();
		event.indexCountPerInstance = indexCountPerInstance;
		event.instanceCount = instanceCount;
		event.startIndexLocation = startIndexLocation;
		event.baseVertexLocation = baseVertexLocation;
		event.startInstanceLocation = startInstanceLocation;
	}
	ReleaseSRWLockExclusive(&gEventsLock);
}

void DX12BindingRecordDispatch(
	ID3D12GraphicsCommandList *commandList, UINT threadGroupCountX,
	UINT threadGroupCountY, UINT threadGroupCountZ)
{
	if (!commandList)
		return;

	CommandListBindingState snapshot;
	const UINT64 dispatchSerial =
		static_cast<UINT64>(InterlockedIncrement64(&gGlobalDispatchSerial));
	BindingShard &shard = BindingShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	CommandListBindingState &state = shard.states[commandList];
	state.dispatchSerial = dispatchSerial;
	snapshot = state;
	ReleaseSRWLockExclusive(&shard.lock);

	AcquireSRWLockExclusive(&gEventsLock);
	const size_t before = gEvents.size();
	StoreEventLocked("dispatch", commandList, snapshot);
	if (gEvents.size() > before) {
		BindingEvent &event = gEvents.back();
		event.threadGroupCountX = threadGroupCountX;
		event.threadGroupCountY = threadGroupCountY;
		event.threadGroupCountZ = threadGroupCountZ;
	}
	ReleaseSRWLockExclusive(&gEventsLock);
}

void DX12BindingBeginFrame()
{
	AcquireSRWLockExclusive(&gEventsLock);
	gEvents.clear();
	gEventSerial = 0;
	gDroppedEvents = 0;
	InterlockedExchange64(&gGlobalDrawSerial, 0);
	InterlockedExchange64(&gGlobalDispatchSerial, 0);
	ReleaseSRWLockExclusive(&gEventsLock);
}

void DX12BindingSetGraphicsRootDescriptor(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address)
{
	if (!commandList || rootParameterIndex >= MaxRootParameters)
		return;
	if (tlsGraphicsDescCache.cl == commandList &&
		tlsGraphicsDescCache.address[rootParameterIndex] == address)
		return;

	BindingShard &shard = BindingShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	RootDescriptorState &descriptor = shard.states[commandList].graphicsRootDescriptors[rootParameterIndex];
	descriptor.valid = address != 0;
	descriptor.rootParameterIndex = rootParameterIndex;
	descriptor.type = type;
	descriptor.address = address;
	ReleaseSRWLockExclusive(&shard.lock);
	tlsGraphicsDescCache.cl = commandList;
	tlsGraphicsDescCache.address[rootParameterIndex] = address;
}

void DX12BindingSetComputeRootDescriptor(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address)
{
	if (!commandList || rootParameterIndex >= MaxRootParameters)
		return;
	if (tlsComputeDescCache.cl == commandList &&
		tlsComputeDescCache.address[rootParameterIndex] == address)
		return;

	BindingShard &shard = BindingShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	RootDescriptorState &descriptor = shard.states[commandList].computeRootDescriptors[rootParameterIndex];
	descriptor.valid = address != 0;
	descriptor.rootParameterIndex = rootParameterIndex;
	descriptor.type = type;
	descriptor.address = address;
	++shard.states[commandList].computeBindingSerial;
	ReleaseSRWLockExclusive(&shard.lock);
	tlsComputeDescCache.cl = commandList;
	tlsComputeDescCache.address[rootParameterIndex] = address;
}

static void SetRootConstantsLocked(
	RootConstantsState *constants, UINT rootParameterIndex,
	UINT destOffset, UINT count, const void *values)
{
	if (!constants || !values || destOffset >= 64 || count == 0)
		return;
	const UINT *src = static_cast<const UINT*>(values);
	const UINT copyCount = min(count, 64 - destOffset);
	constants->valid = true;
	constants->rootParameterIndex = rootParameterIndex;
	for (UINT i = 0; i < copyCount; ++i) {
		const UINT slot = destOffset + i;
		constants->values[slot] = src[i];
		constants->valueValid[slot] = true;
		if (slot + 1 > constants->maxSet)
			constants->maxSet = slot + 1;
	}
}

void DX12BindingSetGraphicsRoot32BitConstant(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	UINT destOffset, UINT value)
{
	DX12BindingSetGraphicsRoot32BitConstants(
		commandList, rootParameterIndex, destOffset, 1, &value);
}

void DX12BindingSetComputeRoot32BitConstant(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	UINT destOffset, UINT value)
{
	DX12BindingSetComputeRoot32BitConstants(
		commandList, rootParameterIndex, destOffset, 1, &value);
}

void DX12BindingSetGraphicsRoot32BitConstants(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	UINT destOffset, UINT count, const void *values)
{
	if (!commandList || rootParameterIndex >= MaxRootParameters)
		return;

	BindingShard &shard = BindingShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	RootConstantsState &constants = shard.states[commandList].graphicsRootConstants[rootParameterIndex];
	SetRootConstantsLocked(&constants, rootParameterIndex, destOffset, count, values);
	ReleaseSRWLockExclusive(&shard.lock);
}

void DX12BindingSetComputeRoot32BitConstants(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	UINT destOffset, UINT count, const void *values)
{
	if (!commandList || rootParameterIndex >= MaxRootParameters)
		return;

	BindingShard &shard = BindingShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	RootConstantsState &constants = shard.states[commandList].computeRootConstants[rootParameterIndex];
	SetRootConstantsLocked(&constants, rootParameterIndex, destOffset, count, values);
	++shard.states[commandList].computeBindingSerial;
	ReleaseSRWLockExclusive(&shard.lock);
}

bool DX12BindingGetCurrentComputeRootConstants(
	ID3D12GraphicsCommandList *commandList,
	std::vector<DX12CurrentRootConstants> *constants)
{
	if (!commandList || !constants)
		return false;

	constants->clear();
	CommandListBindingState snapshot;
	bool found = false;
	BindingShard &shard = BindingShardFor(commandList);
	AcquireSRWLockShared(&shard.lock);
	auto it = shard.states.find(commandList);
	if (it != shard.states.end()) {
		snapshot = it->second;
		found = true;
	}
	ReleaseSRWLockShared(&shard.lock);
	if (!found)
		return false;

	for (UINT root = 0; root < MaxRootParameters; ++root) {
		const RootConstantsState &source = snapshot.computeRootConstants[root];
		if (!source.valid)
			continue;
		DX12CurrentRootConstants current;
		current.rootParameterIndex = root;
		current.maxSet = source.maxSet;
		memcpy(current.values, source.values, sizeof(current.values));
		memcpy(current.valueValid, source.valueValid, sizeof(current.valueValid));
		constants->push_back(current);
	}
	return !constants->empty();
}

UINT64 DX12BindingGetComputeBindingSerial(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return 0;

	UINT64 serial = 0;
	BindingShard &shard = BindingShardFor(commandList);
	AcquireSRWLockShared(&shard.lock);
	auto it = shard.states.find(commandList);
	if (it != shard.states.end())
		serial = it->second.computeBindingSerial;
	ReleaseSRWLockShared(&shard.lock);
	return serial;
}
