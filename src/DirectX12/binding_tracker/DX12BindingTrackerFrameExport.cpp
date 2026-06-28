#include "DX12BindingTrackerPrivate.h"

#include <stdio.h>
#include <string.h>

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "DX12FrameAnalysis.h"
#include "DX12FrameAnalysisManifest.h"
#include "DX12IaRegionHash.h"
#include "DX12Json.h"
#include "DX12ResourceTracker.h"
#include "DX12State.h"


void DX12GetCurrentFrameShaderInfos(std::vector<DX12PsoShaderInfo> *shaderInfos)
{
	if (!shaderInfos)
		return;

	shaderInfos->clear();
	std::unordered_set<UINT64> seen;
	AcquireSRWLockShared(&gEventsLock);
	for (const BindingEvent &event : gEvents) {
		if (!event.shaderInfo.psoIndex)
			continue;
		if (!seen.insert(event.shaderInfo.psoIndex).second)
			continue;
		shaderInfos->push_back(event.shaderInfo);
	}
	ReleaseSRWLockShared(&gEventsLock);
}

static void WriteShaderInfo(FILE *file, const DX12PsoShaderInfo &info)
{
	if (!file)
		return;

	fprintf(file, "%llu,",
		static_cast<unsigned long long>(info.psoIndex));
	if (info.hasVS)
		fprintf(file, "%016llx", static_cast<unsigned long long>(info.vs));
	fprintf(file, ",");
	if (info.hasPS)
		fprintf(file, "%016llx", static_cast<unsigned long long>(info.ps));
	fprintf(file, ",");
	if (info.hasCS)
		fprintf(file, "%016llx", static_cast<unsigned long long>(info.cs));
}

static void FormatOptionalHashText(bool hasHash, UINT64 hash, char *text, size_t textCount)
{
	if (!text || textCount == 0)
		return;
	if (hasHash)
		sprintf_s(text, textCount, "%016llx", static_cast<unsigned long long>(hash));
	else
		sprintf_s(text, textCount, "-");
}

static void AppendShaderHashNamePart(
	wchar_t *text, size_t textCount, const char *stage, bool hasHash, UINT64 hash)
{
	if (!text || textCount == 0 || !stage || !hasHash)
		return;
	const size_t used = wcslen(text);
	if (used >= textCount)
		return;
	swprintf_s(text + used, textCount - used, L"-%S%016llx",
		stage, static_cast<unsigned long long>(hash));
}

static void BuildShaderHashNamePart(
	const DX12PsoShaderInfo &info, wchar_t *text, size_t textCount)
{
	if (!text || textCount == 0)
		return;
	text[0] = L'\0';
	AppendShaderHashNamePart(text, textCount, "vs", info.hasVS, info.vs);
	AppendShaderHashNamePart(text, textCount, "ps", info.hasPS, info.ps);
	AppendShaderHashNamePart(text, textCount, "cs", info.hasCS, info.cs);
}

static void WriteRootTables(FILE *file, const RootTableState *tables)
{
	bool first = true;
	for (UINT i = 0; i < MaxRootParameters; ++i) {
		if (!tables[i].valid)
			continue;
		if (!first)
			fprintf(file, ";");
		fprintf(file, "%u=0x%llx",
			tables[i].rootParameterIndex,
			static_cast<unsigned long long>(tables[i].baseDescriptor.ptr));
		first = false;
	}
}

static void WriteVertexBufferSlots(FILE *file, const BindingEvent &event)
{
	bool first = true;
	for (UINT slot = 0; slot < MaxVertexBufferSlots; ++slot) {
		if (!event.vertexBufferValid[slot])
			continue;
		const D3D12_VERTEX_BUFFER_VIEW &view = event.vertexBuffers[slot];
		if (!first)
			fprintf(file, ";");
		fprintf(file, "%u:0x%llx:%u:%u",
			slot,
			static_cast<unsigned long long>(view.BufferLocation),
			view.SizeInBytes,
			view.StrideInBytes);
		first = false;
	}
}

static std::string ResourceRefsForEvent(
	const BindingEvent &event,
	const std::vector<DX12DescriptorHeapSummary> &heaps,
	const std::unordered_map<SIZE_T, const DX12DescriptorSummary*> &descriptorsByCpuHandle)
{
	std::string refs;
	std::unordered_set<std::string> seen;
	for (UINT i = 0; i < MaxRootParameters; ++i) {
		struct TableRef {
			const char *space;
			const RootTableState &table;
			UINT heapType;
		};
		const TableRef tables[] = {
			{ "g", event.graphicsTables[i], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV },
			{ "gs", event.graphicsTables[i], D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER },
			{ "c", event.computeTables[i], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV },
			{ "cs", event.computeTables[i], D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER },
		};
		for (const TableRef &entry : tables) {
			if (!entry.table.valid)
				continue;
			const DX12DescriptorHeapSummary *heap =
				DX12BindingFindHeapForGpuHandle(heaps, entry.table, entry.heapType);
			if (!heap)
				continue;
			const UINT64 descriptorIndex = (entry.table.baseDescriptor.ptr - heap->gpuStart) / heap->increment;
			const SIZE_T cpuHandle = heap->cpuStart + static_cast<SIZE_T>(descriptorIndex) * heap->increment;
			const DX12DescriptorSummary *descriptor = DX12BindingFindDescriptorByCpuHandle(descriptorsByCpuHandle, cpuHandle);
			char key[128];
			sprintf_s(key, "%s%u:%llu:%p",
				entry.space,
				entry.table.rootParameterIndex,
				static_cast<unsigned long long>(descriptorIndex),
				descriptor ? descriptor->resource : nullptr);
			if (!seen.insert(key).second)
				continue;
			if (!refs.empty())
				refs += ";";
			refs += key;
		}
	}
	return refs;
}

static const char *TopologyName(D3D12_PRIMITIVE_TOPOLOGY topology)
{
	switch (topology) {
	case D3D_PRIMITIVE_TOPOLOGY_POINTLIST:
		return "POINTLIST";
	case D3D_PRIMITIVE_TOPOLOGY_LINELIST:
		return "LINELIST";
	case D3D_PRIMITIVE_TOPOLOGY_LINESTRIP:
		return "LINESTRIP";
	case D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST:
		return "TRIANGLELIST";
	case D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:
		return "TRIANGLESTRIP";
	default:
		return "UNKNOWN";
	}
}

struct FlatBufferRow
{
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

struct BufferProducer
{
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

static std::string MakeBufferKey(const char *role, uint32_t huntHash)
{
	char key[128];
	sprintf_s(key, "%s|%08x",
		role ? role : "",
		huntHash);
	return key;
}

static std::string NextBufferId(const char *prefix, UINT *nextId)
{
	char id[32];
	const UINT value = nextId ? ++(*nextId) : 0;
	sprintf_s(id, "%s_%u", prefix ? prefix : "buf", value);
	return id;
}

static DX12IaRegionDraw IaRegionDrawFromEvent(const BindingEvent &event)
{
	DX12IaRegionDraw draw = {};
	draw.indexed = event.kind == "draw_indexed";
	draw.vertexCount = event.vertexCountPerInstance;
	draw.indexCount = event.indexCountPerInstance;
	draw.firstVertex = event.startVertexLocation;
	draw.firstIndex = event.startIndexLocation;
	draw.baseVertex = event.baseVertexLocation;
	return draw;
}

static bool IaRegionForEvent(
	const BindingEvent &event, const char *role, UINT slot, DX12IaRegion *region)
{
	if (!region || !DX12BindingIsDrawEvent(event))
		return false;
	*region = DX12IaRegion();
	DX12IaRegionDraw draw = IaRegionDrawFromEvent(event);
	if (role && !strcmp(role, "IB")) {
		if (!draw.indexed || !event.indexBufferValid)
			return false;
		return DX12GetIndexBufferRegion(event.indexBuffer, draw, region);
	}
	if (slot >= MaxVertexBufferSlots || !event.vertexBufferValid[slot])
		return false;
	return DX12GetVertexBufferRegion(slot, event.vertexBuffers[slot], draw, region);
}

static uint32_t IaRegionHashForEvent(const BindingEvent &event, const char *role, UINT slot)
{
	DX12IaRegionDraw draw = IaRegionDrawFromEvent(event);
	if (role && !strcmp(role, "IB"))
		return draw.indexed && event.indexBufferValid ?
			DX12HashIndexBufferRegion(event.indexBuffer, draw) : 0;
	if (slot >= MaxVertexBufferSlots || !event.vertexBufferValid[slot])
		return 0;
	return DX12HashVertexBufferRegion(slot, event.vertexBuffers[slot], draw);
}

static const FlatBufferRow *GetOrAddBufferRow(
	std::vector<FlatBufferRow> *rows, std::unordered_map<std::string, size_t> *rowByKey,
	UINT *nextVbId, UINT *nextIbId, const BindingEvent &event, const char *role,
	UINT slot)
{
	if (!rows || !rowByKey)
		return nullptr;

	DX12IaRegion region = {};
	if (!IaRegionForEvent(event, role, slot, &region))
		return nullptr;
	const bool ib = role && !strcmp(role, "IB");
	const D3D12_VERTEX_BUFFER_VIEW &vb = event.vertexBuffers[slot];
	const UINT64 viewGpuVa = ib ? event.indexBuffer.BufferLocation : vb.BufferLocation;
	const UINT64 viewSize = ib ? event.indexBuffer.SizeInBytes : vb.SizeInBytes;
	const UINT stride = ib ? 0 : vb.StrideInBytes;
	const UINT format = ib ? static_cast<UINT>(event.indexBuffer.Format) : 0;
	const UINT64 dumpGpuVa = ib ? region.gpuVa : viewGpuVa;
	const UINT64 dumpSize = ib ? region.size : viewSize;
	DX12BufferResourceSummary resource;
	const bool resolved = DX12ResolveBufferResourceByGpuVa(dumpGpuVa, dumpSize, &resource);
	const uint32_t huntHash = IaRegionHashForEvent(event, role, slot);
	if (!huntHash)
		return nullptr;
	std::string key = MakeBufferKey(role, huntHash);
	auto found = rowByKey->find(key);
	if (found != rowByKey->end() && found->second < rows->size())
		return &(*rows)[found->second];

	FlatBufferRow row;
	row.id = NextBufferId(role && !strcmp(role, "IB") ? "ib" : "vb",
		role && !strcmp(role, "IB") ? nextIbId : nextVbId);
	row.role = role ? role : "";
	row.eventSerial = event.serial;
	row.drawId = event.drawId;
	row.dispatchId = event.dispatchId;
	row.psoIndex = event.shaderInfo.psoIndex;
	row.shaderInfo = event.shaderInfo;
	// VB hashes stay draw-region based so Hunting, Dump and Mod matching use
	// one identity, but Blender/SSMT need the full IA view bytes to satisfy
	// max(index) < vertex_count for indexed mesh reconstruction.
	row.gpuVa = dumpGpuVa;
	row.size = dumpSize;
	row.viewGpuVa = viewGpuVa;
	row.viewSize = viewSize;
	row.stride = stride;
	row.slot = slot;
	row.format = format;
	row.huntHash = huntHash;
	row.resolved = resolved;
	row.resource = resource;
	row.viewOffset = resolved ? resource.resourceOffset : region.viewOffset;
	(*rowByKey)[key] = rows->size();
	rows->push_back(row);
	return &rows->back();
}

static std::string BufferIdsForEvent(
	const BindingEvent &event, std::vector<FlatBufferRow> *rows,
	std::unordered_map<std::string, size_t> *rowByKey, UINT *nextVbId, UINT *nextIbId)
{
	std::string ids;
	for (UINT slot = 0; slot < MaxVertexBufferSlots; ++slot) {
		if (!event.vertexBufferValid[slot])
			continue;
		const FlatBufferRow *row = GetOrAddBufferRow(rows, rowByKey, nextVbId, nextIbId,
			event, "VB", slot);
		if (!row)
			continue;
		if (!ids.empty())
			ids += ";";
		char item[64];
		sprintf_s(item, "%u:%s", slot, row->id.c_str());
		ids += item;
	}
	return ids;
}

static std::string IndexBufferIdForEvent(
	const BindingEvent &event, std::vector<FlatBufferRow> *rows,
	std::unordered_map<std::string, size_t> *rowByKey, UINT *nextVbId, UINT *nextIbId)
{
	if (!event.indexBufferValid || event.indexBuffer.BufferLocation == 0 ||
		event.indexBuffer.SizeInBytes == 0)
		return "";

	const FlatBufferRow *row = GetOrAddBufferRow(rows, rowByKey, nextVbId, nextIbId,
		event, "IB", 0);
	return row ? row->id : "";
}

static void CollectFlatBuffersForEvents(
	const std::vector<BindingEvent> &events, std::vector<FlatBufferRow> *buffers)
{
	if (!buffers)
		return;

	buffers->clear();
	std::unordered_map<std::string, size_t> bufferByKey;
	UINT nextVbId = 0;
	UINT nextIbId = 0;
	for (const BindingEvent &event : events) {
		if (!DX12BindingIsDrawEvent(event) && !DX12BindingIsDispatchEvent(event))
			continue;
		BufferIdsForEvent(event, buffers, &bufferByKey, &nextVbId, &nextIbId);
		IndexBufferIdForEvent(event, buffers, &bufferByKey, &nextVbId, &nextIbId);
	}
}

static void AddFrameIaBufferRow(
	std::vector<FlatBufferRow> *rows, UINT *nextVbId, UINT *nextIbId,
	const BindingEvent &event, const char *role, UINT slot)
{
	if (!rows)
		return;

	DX12IaRegion region = {};
	if (!IaRegionForEvent(event, role, slot, &region))
		return;
	const uint32_t huntHash = IaRegionHashForEvent(event, role, slot);
	if (!huntHash)
		return;
	const bool ib = role && !strcmp(role, "IB");
	const D3D12_VERTEX_BUFFER_VIEW &vb = event.vertexBuffers[slot];
	const UINT64 viewGpuVa = ib ? event.indexBuffer.BufferLocation : vb.BufferLocation;
	const UINT64 viewSize = ib ? event.indexBuffer.SizeInBytes : vb.SizeInBytes;
	const UINT64 dumpGpuVa = ib ? region.gpuVa : viewGpuVa;
	const UINT64 dumpSize = ib ? region.size : viewSize;
	FlatBufferRow row;
	row.id = NextBufferId(role && !strcmp(role, "IB") ? "ib" : "vb",
		role && !strcmp(role, "IB") ? nextIbId : nextVbId);
	row.role = role ? role : "";
	row.eventSerial = event.serial;
	row.drawId = event.drawId;
	row.dispatchId = event.dispatchId;
	row.psoIndex = event.shaderInfo.psoIndex;
	row.shaderInfo = event.shaderInfo;
	row.gpuVa = dumpGpuVa;
	row.size = dumpSize;
	row.viewGpuVa = viewGpuVa;
	row.viewSize = viewSize;
	row.stride = ib ? 0 : vb.StrideInBytes;
	row.slot = slot;
	row.format = ib ? static_cast<UINT>(event.indexBuffer.Format) : 0;
	row.resolved = DX12ResolveBufferResourceByGpuVa(dumpGpuVa, dumpSize, &row.resource);
	row.viewOffset = row.resolved ? row.resource.resourceOffset : region.viewOffset;
	row.huntHash = huntHash;
	rows->push_back(row);
}

static void CollectFrameIaBuffersForEvents(
	const std::vector<BindingEvent> &events, std::vector<FlatBufferRow> *buffers)
{
	if (!buffers)
		return;

	buffers->clear();
	UINT nextVbId = 0;
	UINT nextIbId = 0;
	for (const BindingEvent &event : events) {
		if (!DX12BindingIsDrawEvent(event))
			continue;
		for (UINT slot = 0; slot < MaxVertexBufferSlots; ++slot) {
			if (!event.vertexBufferValid[slot])
				continue;
			AddFrameIaBufferRow(buffers, &nextVbId, &nextIbId, event, "VB", slot);
		}
		if (event.indexBufferValid) {
			AddFrameIaBufferRow(buffers, &nextVbId, &nextIbId, event, "IB", 0);
		}
	}
}

static bool RangesOverlap(UINT64 aBegin, UINT64 aSize, UINT64 bBegin, UINT64 bSize)
{
	if (aSize == 0 || bSize == 0)
		return false;
	const UINT64 aEnd = aBegin + aSize;
	const UINT64 bEnd = bBegin + bSize;
	if (aEnd < aBegin || bEnd < bBegin)
		return false;
	return aBegin < bEnd && bBegin < aEnd;
}

static bool BindingIsUavBufferProducer(const DX12FrameResourceBinding &binding)
{
	if (binding.rootDescriptor)
		return binding.rangeType == D3D12_ROOT_PARAMETER_TYPE_UAV &&
			binding.gpuVirtualAddress != 0;
	if (!binding.hasDescriptor || binding.descriptor.kind != "UAV")
		return false;
	if (!binding.descriptor.resource)
		return false;
	if (!binding.descriptor.hasResourceDesc ||
		binding.descriptor.resourceDesc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER)
		return false;
	return true;
}

static BufferProducer ProducerFromBinding(const DX12FrameResourceBinding &binding)
{
	BufferProducer producer;
	producer.eventSerial = binding.eventSerial;
	producer.drawId = binding.drawId;
	producer.dispatchId = binding.dispatchId;
	producer.psoIndex = binding.psoIndex;
	producer.shaderInfo = binding.shaderInfo;
	producer.bindSpace = binding.bindSpace;
	producer.rootParameterIndex = binding.rootParameterIndex;
	producer.shaderRegister = binding.shaderRegister;
	if (binding.rootDescriptor) {
		DX12BufferResourceSummary resource;
		if (DX12ResolveBufferResourceByGpuVa(binding.gpuVirtualAddress, 1, &resource)) {
			producer.resource = resource.resource;
			producer.begin = binding.gpuVirtualAddress;
			const UINT64 viewBytes = resource.viewSize ? resource.viewSize :
				resource.hasResourceDesc ? resource.resourceDesc.Width : 1;
			producer.end = producer.begin + viewBytes;
			if (producer.end < producer.begin)
				producer.end = producer.begin;
		}
		return producer;
	}
	producer.resource = binding.descriptor.resource;
	producer.begin = binding.descriptor.gpuVirtualAddress + binding.descriptor.resourceOffset;
	const UINT64 viewBytes = binding.descriptor.viewSize ?
		binding.descriptor.viewSize : binding.descriptor.resourceDesc.Width;
	producer.end = producer.begin + viewBytes;
	if (producer.end < producer.begin)
		producer.end = producer.begin;
	return producer;
}

static const BufferProducer *FindLatestProducerForBuffer(
	const FlatBufferRow &row, const std::vector<BufferProducer> &producers)
{
	if (row.role != "VB" || !row.resolved || !row.resource.resource)
		return nullptr;

	const BufferProducer *best = nullptr;
	for (const BufferProducer &producer : producers) {
		if (producer.eventSerial >= row.eventSerial)
			continue;
		if (producer.resource != row.resource.resource)
			continue;
		if (!RangesOverlap(row.gpuVa, row.size, producer.begin, producer.end - producer.begin))
			continue;
		if (!best || producer.eventSerial > best->eventSerial)
			best = &producer;
	}
	return best;
}

static void ClassifyIaBufferSkinning(
	std::vector<FlatBufferRow> *buffers,
	const std::vector<DX12FrameResourceBinding> &resourceBindings)
{
	if (!buffers)
		return;

	std::vector<BufferProducer> producers;
	for (const DX12FrameResourceBinding &binding : resourceBindings) {
		if (!BindingIsUavBufferProducer(binding))
			continue;
		BufferProducer producer = ProducerFromBinding(binding);
		if (producer.begin == producer.end)
			continue;
		producers.push_back(std::move(producer));
	}

	for (FlatBufferRow &row : *buffers) {
		if (row.role != "VB") {
			row.skinningClass = "not_applicable";
			continue;
		}

		const BufferProducer *producer = FindLatestProducerForBuffer(row, producers);
		if (producer) {
			row.skinningClass = "gpu_preskinning";
			row.producerEventSerial = producer->eventSerial;
			row.producerDrawId = producer->drawId;
			row.producerDispatchId = producer->dispatchId;
			row.producerPsoIndex = producer->psoIndex;
			row.producerShaderInfo = producer->shaderInfo;
			row.producerBindSpace = producer->bindSpace;
			row.producerRootParameterIndex = producer->rootParameterIndex;
			row.producerShaderRegister = producer->shaderRegister;
		} else if (row.resolved) {
			row.skinningClass = "cpu_preskinning";
		} else {
			row.skinningClass = "unknown";
		}
	}
}

static DX12FrameIaBufferBinding FrameIaBufferFromFlatRow(const FlatBufferRow &row)
{
	DX12FrameIaBufferBinding buffer;
	buffer.bufferId = row.id;
	buffer.role = row.role;
	buffer.skinningClass = row.skinningClass;
	buffer.eventSerial = row.eventSerial;
	buffer.drawId = row.drawId;
	buffer.dispatchId = row.dispatchId;
	buffer.psoIndex = row.psoIndex;
	buffer.shaderInfo = row.shaderInfo;
	buffer.producerEventSerial = row.producerEventSerial;
	buffer.producerDrawId = row.producerDrawId;
	buffer.producerDispatchId = row.producerDispatchId;
	buffer.producerPsoIndex = row.producerPsoIndex;
	buffer.producerShaderInfo = row.producerShaderInfo;
	buffer.producerBindSpace = row.producerBindSpace;
	buffer.producerRootParameterIndex = row.producerRootParameterIndex;
	buffer.producerShaderRegister = row.producerShaderRegister;
	buffer.gpuVa = row.gpuVa;
	buffer.size = row.size;
	buffer.viewGpuVa = row.viewGpuVa;
	buffer.viewSize = row.viewSize;
	buffer.viewOffset = row.viewOffset;
	buffer.stride = row.stride;
	buffer.slot = row.slot;
	buffer.format = row.format;
	buffer.huntHash = row.huntHash;
	buffer.resolved = row.resolved;
	buffer.resource = row.resource;
	return buffer;
}

void DX12BuildIaBufferFileName(
	const DX12FrameIaBufferBinding &buffer, wchar_t *fileName, size_t fileNameCount)
{
	if (!fileName || fileNameCount == 0)
		return;

	fileName[0] = L'\0';
	const char *role = buffer.role.empty() ? "BUF" : buffer.role.c_str();
	const wchar_t eventKind = buffer.dispatchId ? L'c' : L'd';
	const UINT64 eventId = buffer.dispatchId ? buffer.dispatchId : buffer.drawId;
	wchar_t shaderPart[96];
	BuildShaderHashNamePart(buffer.shaderInfo, shaderPart, ARRAYSIZE(shaderPart));
	swprintf_s(fileName, fileNameCount,
		L"%c%06llu-pso%llu%s-ia_%S_slot%u_hash%08x_va%016llx_off%llu_size%llu_stride%u_fmt%u.buf",
		eventKind,
		static_cast<unsigned long long>(eventId),
		static_cast<unsigned long long>(buffer.psoIndex),
		shaderPart,
		role,
		buffer.slot,
		buffer.huntHash,
		static_cast<unsigned long long>(buffer.gpuVa),
		static_cast<unsigned long long>(buffer.viewOffset),
		static_cast<unsigned long long>(buffer.size),
		buffer.stride,
		buffer.format);
}

void DX12GetCurrentFrameIaBuffers(std::vector<DX12FrameIaBufferBinding> *buffers)
{
	if (!buffers)
		return;

	std::vector<BindingEvent> events;
	AcquireSRWLockShared(&gEventsLock);
	events = gEvents;
	ReleaseSRWLockShared(&gEventsLock);

	std::vector<FlatBufferRow> rows;
	CollectFrameIaBuffersForEvents(events, &rows);
	std::vector<DX12FrameResourceBinding> resourceBindings;
	DX12GetCurrentFrameResourceBindings(&resourceBindings);
	ClassifyIaBufferSkinning(&rows, resourceBindings);

	buffers->clear();
	buffers->reserve(rows.size());
	for (const FlatBufferRow &row : rows)
		buffers->push_back(FrameIaBufferFromFlatRow(row));

	DX12FrameAnalysisLogJsonFunc("IaBufferStage",
		"\"events\":%zu,\"buffers\":%zu",
		events.size(), buffers->size());
}

static void WriteOptionalHash(FILE *file, bool hasHash, UINT64 hash)
{
	if (hasHash)
		fprintf(file, "%016llx", static_cast<unsigned long long>(hash));
}

static void WriteFlatFrameAnalysisFiles(
	const wchar_t *dir, const std::vector<BindingEvent> &events, UINT64 droppedEvents)
{
	if (!dir)
		return;

	std::vector<DX12DescriptorSummary> descriptors;
	std::vector<DX12DescriptorHeapSummary> heaps;
	DX12GetResourceMetadataSnapshot(nullptr, &descriptors, nullptr, &heaps);
	std::unordered_map<SIZE_T, const DX12DescriptorSummary*> descriptorsByCpuHandle;
	DX12BindingBuildDescriptorLookup(descriptors, &descriptorsByCpuHandle);

	std::vector<FlatBufferRow> buffers;
	std::unordered_map<std::string, size_t> bufferByKey;
	UINT nextVbId = 0;
	UINT nextIbId = 0;
	size_t drawRows = 0;
	size_t dispatchRows = 0;

	wchar_t summaryDir[MAX_PATH];
	DX12BindingGetSummaryDirectory(dir, summaryDir, ARRAYSIZE(summaryDir));

	wchar_t drawPath[MAX_PATH];
	swprintf_s(drawPath, L"%s\\DrawCallsDX12.csv", summaryDir);
	FILE *drawFile = _wfsopen(drawPath, L"w", _SH_DENYNO);
	if (drawFile) {
		fprintf(drawFile,
			"draw_id,dispatch_id,type,serial,command_list,pipeline_state,pso,vs,ps,cs,topology,"
			"vertex_count,index_count,start_vertex,start_index,base_vertex,instance_count,start_instance,"
			"groups_x,groups_y,groups_z,vb_slots,ib_resource,resource_refs\n");
		for (const BindingEvent &event : events) {
			if (!DX12BindingIsDrawEvent(event) && !DX12BindingIsDispatchEvent(event))
				continue;

			std::string vbSlots = BufferIdsForEvent(
				event, &buffers, &bufferByKey, &nextVbId, &nextIbId);
			std::string ibResource = IndexBufferIdForEvent(
				event, &buffers, &bufferByKey, &nextVbId, &nextIbId);
			std::string resourceRefs = ResourceRefsForEvent(event, heaps, descriptorsByCpuHandle);

			if (DX12BindingIsDrawEvent(event))
				drawRows++;
			else
				dispatchRows++;

			fprintf(drawFile, "%llu,%llu,%s,%llu,%p,%p,%llu,",
				static_cast<unsigned long long>(event.drawId),
				static_cast<unsigned long long>(event.dispatchId),
				event.kind.c_str(),
				static_cast<unsigned long long>(event.serial),
				event.commandList,
				event.pipelineState,
				static_cast<unsigned long long>(event.shaderInfo.psoIndex));
			WriteOptionalHash(drawFile, event.shaderInfo.hasVS, event.shaderInfo.vs);
			fprintf(drawFile, ",");
			WriteOptionalHash(drawFile, event.shaderInfo.hasPS, event.shaderInfo.ps);
			fprintf(drawFile, ",");
			WriteOptionalHash(drawFile, event.shaderInfo.hasCS, event.shaderInfo.cs);
			fprintf(drawFile,
				",%s,%u,%u,%u,%u,%d,%u,%u,%u,%u,%u,%s,%s,%s\n",
				TopologyName(event.primitiveTopology),
				event.vertexCountPerInstance,
				event.indexCountPerInstance,
				event.startVertexLocation,
				event.startIndexLocation,
				event.baseVertexLocation,
				event.instanceCount,
				event.startInstanceLocation,
				event.threadGroupCountX,
				event.threadGroupCountY,
				event.threadGroupCountZ,
				vbSlots.c_str(),
				ibResource.c_str(),
				resourceRefs.c_str());
		}
		fclose(drawFile);
		char fields[512] = "";
		DX12JsonAppendWStringField(fields, sizeof(fields), "path", drawPath);
		DX12JsonAppendRawField(fields, sizeof(fields), "draws", std::to_string(drawRows).c_str());
		DX12JsonAppendRawField(fields, sizeof(fields), "dispatches", std::to_string(dispatchRows).c_str());
		DX12JsonAppendRawField(fields, sizeof(fields), "buffers", std::to_string(buffers.size()).c_str());
		DX12FrameAnalysisLogJsonFunc("DrawCallCsvWritten", "%s", fields + 1);
	}

	wchar_t bufferPath[MAX_PATH];
	swprintf_s(bufferPath, L"%s\\BuffersDX12.csv", summaryDir);
	FILE *bufferFile = _wfsopen(bufferPath, L"w", _SH_DENYNO);
	if (bufferFile) {
		std::vector<DX12FrameResourceBinding> resourceBindings;
		DX12GetCurrentFrameResourceBindings(&resourceBindings);
		ClassifyIaBufferSkinning(&buffers, resourceBindings);
		fprintf(bufferFile,
			"buffer_id,role,hunt_hash,resource,file,gpu_va,resource_gpu_va,offset,size,view_gpu_va,view_size,view_offset,stride,slot,format,"
			"resolved,current_state,has_current_state,heap_type,resource_size,skin_source,"
			"dump_scope,producer_event,producer_draw,producer_dispatch,producer_pso,producer_bind,"
			"producer_root,producer_reg\n");
		for (const FlatBufferRow &row : buffers) {
			UINT64 resourceSize = row.resource.hasResourceDesc ? row.resource.resourceDesc.Width : 0;
			DX12FrameIaBufferBinding iaBuffer = FrameIaBufferFromFlatRow(row);
			wchar_t fileName[MAX_PATH];
			DX12BuildIaBufferFileName(iaBuffer, fileName, ARRAYSIZE(fileName));
			fprintf(bufferFile,
				"%s,%s,%08x,%p,%S,0x%llx,0x%llx,%llu,%llu,0x%llx,%llu,%llu,%u,%u,%u,%u,0x%x,%u,%u,%llu,%s,%s,%llu,%llu,%llu,%llu,%s,%u,%u\n",
				row.id.c_str(),
				row.role.c_str(),
				row.huntHash,
				row.resolved ? row.resource.resource : nullptr,
				fileName,
				static_cast<unsigned long long>(row.gpuVa),
				static_cast<unsigned long long>(row.resolved ? row.resource.gpuVirtualAddress : 0),
				static_cast<unsigned long long>(row.resolved ? row.resource.resourceOffset : 0),
				static_cast<unsigned long long>(row.size),
				static_cast<unsigned long long>(row.viewGpuVa),
				static_cast<unsigned long long>(row.viewSize),
				static_cast<unsigned long long>(row.viewOffset),
				row.stride,
				row.slot,
				row.format,
				row.resolved ? 1 : 0,
				row.resolved ? row.resource.currentState : 0,
				row.resolved && row.resource.hasCurrentState ? 1 : 0,
				row.resolved && row.resource.hasResourceHeapType ? row.resource.resourceHeapType : 0,
				static_cast<unsigned long long>(resourceSize),
				row.skinningClass.empty() ? "unknown" : row.skinningClass.c_str(),
				row.role == "IB" ? "draw_slice" : "full_view",
				static_cast<unsigned long long>(row.producerEventSerial),
				static_cast<unsigned long long>(row.producerDrawId),
				static_cast<unsigned long long>(row.producerDispatchId),
				static_cast<unsigned long long>(row.producerPsoIndex),
				row.producerBindSpace.empty() ? "-" : row.producerBindSpace.c_str(),
				row.producerRootParameterIndex,
				row.producerShaderRegister);
		}
		fclose(bufferFile);
		char fields[512] = "";
		DX12JsonAppendWStringField(fields, sizeof(fields), "path", bufferPath);
		DX12JsonAppendRawField(fields, sizeof(fields), "rows", std::to_string(buffers.size()).c_str());
		DX12FrameAnalysisLogJsonFunc("BufferCsvWritten", "%s", fields + 1);
	}

	wchar_t framePath[MAX_PATH];
	swprintf_s(framePath, L"%s\\FrameAnalysisDX12.csv", summaryDir);
	FILE *frameFile = _wfsopen(framePath, L"w", _SH_DENYNO);
	if (frameFile) {
		fprintf(frameFile,
			"events,dropped,max_events,draw_rows,dispatch_rows,descriptor_heaps,descriptors,buffers,"
			"draw_calls_csv,buffers_csv,resource_bindings_csv\n");
		fprintf(frameFile, "%zu,%llu,%u,%zu,%zu,%zu,%zu,%zu,summary\\DrawCallsDX12.csv,summary\\BuffersDX12.csv,summary\\ResourceBindingsDX12.csv\n",
			events.size(),
			static_cast<unsigned long long>(droppedEvents),
			MaxTrackedEvents,
			drawRows,
			dispatchRows,
			heaps.size(),
			descriptors.size(),
			buffers.size());
		fclose(frameFile);
		char fields[512] = "";
		DX12JsonAppendWStringField(fields, sizeof(fields), "path", framePath);
		DX12JsonAppendRawField(fields, sizeof(fields), "events", std::to_string(events.size()).c_str());
		DX12JsonAppendRawField(fields, sizeof(fields), "buffers", std::to_string(buffers.size()).c_str());
		DX12FrameAnalysisLogJsonFunc("FrameAnalysisCsvWritten", "%s", fields + 1);
	}
}

DX12FrameAnalysisDumpResult DX12DumpBindingTrace(const wchar_t *dir)
{
	DX12FrameAnalysisDumpResult result;
	if (!dir)
	{
		result.reason = "missing_directory";
		return result;
	}

	std::vector<BindingEvent> events;
	UINT64 droppedEvents = 0;
	AcquireSRWLockShared(&gEventsLock);
	events = gEvents;
	droppedEvents = gDroppedEvents;
	ReleaseSRWLockShared(&gEventsLock);

	DX12FrameAnalysisLogJsonFunc("BindingTraceBegin",
		"\"events\":%zu,\"dropped\":%llu,\"maxEvents\":%u",
		events.size(), static_cast<unsigned long long>(droppedEvents),
		MaxTrackedEvents);
	for (const BindingEvent &event : events) {
		if (DX12BindingIsDrawEvent(event) || DX12BindingIsDispatchEvent(event)) {
			DX12FrameAnalysisManifestWriteCall(
				event.kind.c_str(),
				event.serial,
				event.drawId,
				event.dispatchId,
				event.commandList,
				event.pipelineState,
				event.shaderInfo,
				event.primitiveTopology,
				event.vertexCountPerInstance,
				event.indexCountPerInstance,
				event.startVertexLocation,
				event.startIndexLocation,
				event.baseVertexLocation,
				event.instanceCount,
				event.startInstanceLocation,
				event.threadGroupCountX,
				event.threadGroupCountY,
				event.threadGroupCountZ,
				event.indexBufferValid,
				event.indexBuffer.BufferLocation,
				event.indexBuffer.SizeInBytes,
				event.indexBuffer.Format);
		}

	}

	DX12FrameAnalysisLogJsonFunc("BindingTraceEnd",
		"\"events\":%zu,\"dropped\":%llu",
		events.size(), static_cast<unsigned long long>(droppedEvents));

	WriteFlatFrameAnalysisFiles(dir, events, droppedEvents);
	result.ok = true;
	result.written = events.size();
	result.skipped = static_cast<size_t>(droppedEvents);
	return result;
}
