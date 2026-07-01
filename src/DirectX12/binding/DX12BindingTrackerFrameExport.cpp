#include "DX12BindingTracker.hpp"
#include "DX12BindingTrackerQuery.hpp"

#include <stdio.h>
#include <string.h>

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "DX12CaptureLedger.hpp"
#include "DX12FrameAnalysis.hpp"
#include "DX12FrameAnalysisManifest.hpp"
#include "DX12IaRegion.hpp"
#include "utils/DX12JsonUtils.hpp"
#include "DX12ResourceTracker.hpp"
#include "DX12LogManager.hpp"

static bool PopulatePreferredFrameEventSource(DX12FrameEventSource *source, const char *reason)
{
	if (!source)
		return false;

	const UINT64 captureSessionId = DX12BindingTracker::CurrentCaptureSessionId();
	if (DX12BindingTracker::PopulateCaptureEventSource(
			source,
			captureSessionId,
			reason ? reason : "PopulatePreferredFrameEventSource"))
		return true;

	DX12BindingTracker::DrainPendingEvents(reason ? reason : "PopulatePreferredFrameEventSourceFallback");
	return DX12BindingTracker::PopulateFrameEventSource(source);
}


void DX12BindingFrameExport::GetCurrentFrameShaderInfos(std::vector<DX12PsoShaderInfo> *shaderInfos)
{
	if (!shaderInfos)
		return;

	shaderInfos->clear();
	DX12FrameEventSource source;
	PopulatePreferredFrameEventSource(&source, "GetCurrentFrameShaderInfos");
	const std::vector<BindingEvent> &sourceEvents = source.GetEvents();
	std::unordered_set<UINT64> seen;
	for (const BindingEvent &event : sourceEvents) {
		if (!event.shaderInfo.psoIndex)
			continue;
		if (!seen.insert(event.shaderInfo.psoIndex).second)
			continue;
		shaderInfos->push_back(event.shaderInfo);
	}
}

void DX12BindingFrameExport::WriteShaderInfo(FILE *file, const DX12PsoShaderInfo &info)
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

void DX12BindingFrameExport::FormatOptionalHashText(bool hasHash, UINT64 hash, char *text, size_t textCount)
{
	if (!text || textCount == 0)
		return;
	if (hasHash)
		sprintf_s(text, textCount, "%016llx", static_cast<unsigned long long>(hash));
	else
		sprintf_s(text, textCount, "-");
}

void DX12BindingFrameExport::AppendShaderHashNamePart(
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

void DX12BindingFrameExport::BuildShaderHashNamePart(
	const DX12PsoShaderInfo &info, wchar_t *text, size_t textCount)
{
	if (!text || textCount == 0)
		return;
	text[0] = L'\0';
	DX12BindingFrameExport::AppendShaderHashNamePart(text, textCount, "vs", info.hasVS, info.vs);
	DX12BindingFrameExport::AppendShaderHashNamePart(text, textCount, "ps", info.hasPS, info.ps);
	DX12BindingFrameExport::AppendShaderHashNamePart(text, textCount, "cs", info.hasCS, info.cs);
}

void DX12BindingFrameExport::WriteRootTables(FILE *file, const RootTableState *tables)
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

void DX12BindingFrameExport::WriteVertexBufferSlots(FILE *file, const BindingEvent &event)
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

const char *DX12BindingFrameExport::TopologyName(D3D12_PRIMITIVE_TOPOLOGY topology)
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

std::string DX12BindingFrameExport::NextBufferId(const char *prefix, UINT *nextId)
{
	char id[32];
	const UINT value = nextId ? ++(*nextId) : 0;
	sprintf_s(id, "%s_%u", prefix ? prefix : "buf", value);
	return id;
}

DX12IaRegionDraw DX12BindingFrameExport::IaRegionDrawFromEvent(const BindingEvent &event)
{
	return DX12IaRegionDraw::FromDrawArguments(
		event.kind == "draw_indexed",
		event.vertexCountPerInstance,
		event.indexCountPerInstance,
		event.startVertexLocation,
		event.startIndexLocation,
		event.baseVertexLocation);
}

bool DX12BindingFrameExport::IaRegionForEvent(
	const BindingEvent &event, const char *role, UINT slot, DX12IaRegion *region)
{
	if (!region || !DX12BindingTrackerQuery::IsDrawEvent(event))
		return false;
	*region = DX12IaRegion();
	DX12IaRegionDraw draw = DX12BindingFrameExport::IaRegionDrawFromEvent(event);
	if (role && !strcmp(role, "IB")) {
		if (!draw.indexed || !event.indexBufferValid)
			return false;
		return region->SetFromIndexBufferView(event.indexBuffer, draw);
	}
	if (slot >= MaxVertexBufferSlots || !event.vertexBufferValid[slot])
		return false;
	return region->SetFromVertexBufferView(slot, event.vertexBuffers[slot], draw);
}

uint32_t DX12BindingFrameExport::IaRegionHashForEvent(const BindingEvent &event, const char *role, UINT slot)
{
	DX12IaRegionDraw draw = DX12BindingFrameExport::IaRegionDrawFromEvent(event);
	if (role && !strcmp(role, "IB"))
		return draw.indexed && event.indexBufferValid ?
			DX12IaRegion::HashIndexBufferView(event.indexBuffer, draw) : 0;
	if (slot >= MaxVertexBufferSlots || !event.vertexBufferValid[slot])
		return 0;
	return DX12IaRegion::HashVertexBufferView(slot, event.vertexBuffers[slot], draw);
}

bool DX12BindingFrameExport::ProjectResolvedIaResourceToView(
	UINT64 viewGpuVa, UINT64 viewSize, UINT64 drawGpuVa, UINT64 drawSize,
	const DX12BufferResourceSummary &resolvedDrawResource,
	DX12BufferResourceSummary *viewResource)
{
	if (!viewResource || !resolvedDrawResource.resource ||
		!resolvedDrawResource.hasResourceDesc || !viewGpuVa || !viewSize)
		return false;
	if (resolvedDrawResource.gpuVirtualAddress == 0)
		return false;
	if (viewGpuVa < resolvedDrawResource.gpuVirtualAddress)
		return false;

	*viewResource = resolvedDrawResource;
	const UINT64 viewOffset = viewGpuVa - resolvedDrawResource.gpuVirtualAddress;
	if (viewOffset >= resolvedDrawResource.resourceDesc.Width)
		return false;

	UINT64 availableBytes = resolvedDrawResource.resourceDesc.Width - viewOffset;
	UINT64 projectedViewBytes = viewSize < availableBytes ? viewSize : availableBytes;
	if (!projectedViewBytes)
		return false;

	viewResource->resourceOffset = viewOffset;
	viewResource->viewSize = projectedViewBytes;
	if (drawGpuVa && drawSize &&
		drawGpuVa >= viewGpuVa &&
		drawGpuVa - viewGpuVa <= projectedViewBytes) {
		const UINT64 drawOffsetWithinView = drawGpuVa - viewGpuVa;
		const UINT64 drawAvailableBytes = projectedViewBytes - drawOffsetWithinView;
		if (drawAvailableBytes < drawSize) {
			viewResource->viewSize = drawOffsetWithinView + drawAvailableBytes;
		}
	}
	return true;
}

bool DX12BindingFrameExport::ResolveIaBufferResourceSummary(
	UINT64 viewGpuVa, UINT64 viewSize, UINT64 drawGpuVa, UINT64 drawSize,
	const DX12BufferResourceSummary &existingResource, bool existingResolved,
	DX12BufferResourceSummary *resource)
{
	if (!resource || !viewGpuVa || !viewSize)
		return false;

	*resource = DX12BufferResourceSummary();
	if (existingResolved &&
		DX12BindingFrameExport::ProjectResolvedIaResourceToView(
			viewGpuVa, viewSize, drawGpuVa, drawSize, existingResource, resource))
		return true;

	if (DX12ResolveBufferResourceByGpuVa(viewGpuVa, viewSize, resource))
		return true;

	if (drawGpuVa && drawSize) {
		DX12BufferResourceSummary drawResource = {};
		if (DX12ResolveBufferResourceByGpuVa(drawGpuVa, drawSize, &drawResource) &&
			DX12BindingFrameExport::ProjectResolvedIaResourceToView(
				viewGpuVa, viewSize, drawGpuVa, drawSize, drawResource, resource)) {
			return true;
		}
	}

	return false;
}

bool DX12BindingFrameExport::ResolveIaBufferResourceSummary(
	const DX12FrameIaBufferBinding &buffer, DX12BufferResourceSummary *resource)
{
	return DX12BindingFrameExport::ResolveIaBufferResourceSummary(
		buffer.viewGpuVa,
		buffer.viewSize,
		buffer.drawGpuVa ? buffer.drawGpuVa : buffer.gpuVa,
		buffer.drawSize ? buffer.drawSize : buffer.size,
		buffer.resource,
		buffer.resolved,
		resource);
}

void DX12BindingFrameExport::AddFrameIaBufferRow(
	std::vector<FlatBufferRow> *rows, UINT *nextVbId, UINT *nextIbId,
	const BindingEvent &event, const char *role, UINT slot)
{
	if (!rows)
		return;

	DX12IaRegion region = {};
	if (!DX12BindingFrameExport::IaRegionForEvent(event, role, slot, &region))
		return;
	const uint32_t huntHash = DX12BindingFrameExport::IaRegionHashForEvent(event, role, slot);
	if (!huntHash)
		return;
	const bool ib = role && !strcmp(role, "IB");
	const D3D12_VERTEX_BUFFER_VIEW &vb = event.vertexBuffers[slot];
	const UINT64 viewGpuVa = ib ? event.indexBuffer.BufferLocation : vb.BufferLocation;
	const UINT64 viewSize = ib ? event.indexBuffer.SizeInBytes : vb.SizeInBytes;
	const UINT64 drawGpuVa = ib ? region.gpuVa : viewGpuVa;
	const UINT64 drawSize = ib ? region.size : viewSize;
	FlatBufferRow row;
	row.id = DX12BindingFrameExport::NextBufferId(role && !strcmp(role, "IB") ? "ib" : "vb",
		role && !strcmp(role, "IB") ? nextIbId : nextVbId);
	row.role = role ? role : "";
	row.eventSerial = event.serial;
	row.drawId = event.drawId;
	row.dispatchId = event.dispatchId;
	row.psoIndex = event.shaderInfo.psoIndex;
	row.shaderInfo = event.shaderInfo;
	row.gpuVa = viewGpuVa;
	row.size = viewSize;
	row.viewGpuVa = viewGpuVa;
	row.viewSize = viewSize;
	row.drawGpuVa = drawGpuVa;
	row.drawSize = drawSize;
	row.drawOffset = region.viewOffset;
	row.stride = ib ? 0 : vb.StrideInBytes;
	row.slot = slot;
	row.format = ib ? static_cast<UINT>(event.indexBuffer.Format) : 0;
	row.drawStartIndex = event.startIndexLocation;
	row.drawIndexCount = event.indexCountPerInstance;
	row.drawBaseVertex = event.baseVertexLocation;
	row.resource = ib ? event.indexBufferResource : event.vertexBufferResources[slot];
	row.resolved = DX12BindingFrameExport::ResolveIaBufferResourceSummary(
		viewGpuVa,
		viewSize,
		drawGpuVa,
		drawSize,
		row.resource,
		ib ? event.indexBufferResolved : event.vertexBufferResolved[slot],
		&row.resource);
	row.viewOffset = row.resolved ? row.resource.resourceOffset : 0;
	row.huntHash = huntHash;
	rows->push_back(row);
}

void DX12BindingFrameExport::CollectFrameIaBuffersForEvents(
	const std::vector<BindingEvent> &events, std::vector<FlatBufferRow> *buffers)
{
	if (!buffers)
		return;

	buffers->clear();
	UINT nextVbId = 0;
	UINT nextIbId = 0;
	for (const BindingEvent &event : events) {
		if (!DX12BindingTrackerQuery::IsDrawEvent(event))
			continue;
		for (UINT slot = 0; slot < MaxVertexBufferSlots; ++slot) {
			if (!event.vertexBufferValid[slot])
				continue;
			DX12BindingFrameExport::AddFrameIaBufferRow(buffers, &nextVbId, &nextIbId, event, "VB", slot);
		}
		if (event.indexBufferValid) {
			DX12BindingFrameExport::AddFrameIaBufferRow(buffers, &nextVbId, &nextIbId, event, "IB", 0);
		}
	}
}

bool DX12BindingFrameExport::RangesOverlap(UINT64 aBegin, UINT64 aSize, UINT64 bBegin, UINT64 bSize)
{
	if (aSize == 0 || bSize == 0)
		return false;
	const UINT64 aEnd = aBegin + aSize;
	const UINT64 bEnd = bBegin + bSize;
	if (aEnd < aBegin || bEnd < bBegin)
		return false;
	return aBegin < bEnd && bBegin < aEnd;
}

bool DX12BindingFrameExport::BindingIsUavBufferProducer(const DX12FrameResourceBinding &binding)
{
	if (!binding.usageClass.empty())
		return binding.usageClass == "UAV" &&
			binding.hasDescriptor &&
			binding.descriptor.resource &&
			binding.descriptor.hasResourceDesc &&
			binding.descriptor.resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER;
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

BufferProducer DX12BindingFrameExport::ProducerFromBinding(const DX12FrameResourceBinding &binding)
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
	if (!binding.usageClass.empty()) {
		producer.resource = binding.descriptor.resource;
		producer.begin = binding.usageBegin ? binding.usageBegin :
			binding.descriptor.gpuVirtualAddress + binding.descriptor.resourceOffset;
		const UINT64 bytes = binding.usageBytes ? binding.usageBytes :
			binding.descriptor.viewSize ? binding.descriptor.viewSize :
			binding.descriptor.resourceDesc.Width;
		producer.end = producer.begin + bytes;
		if (producer.end < producer.begin)
			producer.end = producer.begin;
		return producer;
	}
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

bool DX12BindingFrameExport::IsHuntingUavStateProducer(const BufferProducer &producer)
{
	return producer.bindSpace == "hunting_uav_state";
}

bool DX12BindingFrameExport::ProducerMatchesBuffer(
	const FlatBufferRow &row, const BufferProducer &producer)
{
	if (row.role != "VB" || !row.resolved || !row.resource.resource)
		return false;
	if (producer.resource != row.resource.resource)
		return false;
	return DX12BindingFrameExport::RangesOverlap(
		row.viewGpuVa, row.viewSize, producer.begin, producer.end - producer.begin);
}

bool DX12BindingFrameExport::ProducerIsPreferredForIaBuffer(
	const BufferProducer &candidate, const BufferProducer *best)
{
	if (!best)
		return true;

	const bool candidateFallback = DX12BindingFrameExport::IsHuntingUavStateProducer(candidate);
	const bool bestFallback = DX12BindingFrameExport::IsHuntingUavStateProducer(*best);
	if (candidateFallback != bestFallback)
		return !candidateFallback;

	if (candidate.eventSerial != best->eventSerial)
		return candidate.eventSerial > best->eventSerial;

	const bool candidateCompute = candidate.bindSpace == "compute_cbv_srv_uav";
	const bool bestCompute = best->bindSpace == "compute_cbv_srv_uav";
	if (candidateCompute != bestCompute)
		return candidateCompute;

	if (candidate.shaderRegister != UINT_MAX && best->shaderRegister == UINT_MAX)
		return true;
	if (candidate.shaderRegister == UINT_MAX && best->shaderRegister != UINT_MAX)
		return false;

	return false;
}

const BufferProducer *DX12BindingFrameExport::FindLatestProducerForBuffer(
	const FlatBufferRow &row, const std::vector<BufferProducer> &producers)
{
	const BufferProducer *best = nullptr;
	for (const BufferProducer &producer : producers) {
		if (producer.eventSerial >= row.eventSerial)
			continue;
		if (DX12BindingFrameExport::IsHuntingUavStateProducer(producer))
			continue;
		if (!DX12BindingFrameExport::ProducerMatchesBuffer(row, producer))
			continue;
		if (DX12BindingFrameExport::ProducerIsPreferredForIaBuffer(producer, best))
			best = &producer;
	}
	return best;
}

const BufferProducer *DX12BindingFrameExport::FindLatestHuntingUavStateProducerForBuffer(
	const FlatBufferRow &row, const std::vector<BufferProducer> &producers)
{
	const BufferProducer *best = nullptr;
	for (const BufferProducer &producer : producers) {
		if (producer.eventSerial >= row.eventSerial)
			continue;
		if (!DX12BindingFrameExport::IsHuntingUavStateProducer(producer))
			continue;
		if (!DX12BindingFrameExport::ProducerMatchesBuffer(row, producer))
			continue;
		if (DX12BindingFrameExport::ProducerIsPreferredForIaBuffer(producer, best))
			best = &producer;
	}
	return best;
}

void DX12BindingFrameExport::ClassifyIaBufferSkinning(
	std::vector<FlatBufferRow> *buffers,
	const std::vector<DX12FrameResourceBinding> &resourceBindings)
{
	if (!buffers)
		return;

	std::vector<BufferProducer> producers;
	size_t computeProducerCount = 0;
	size_t huntingProducerCount = 0;
	size_t otherProducerCount = 0;
	for (const DX12FrameResourceBinding &binding : resourceBindings) {
		if (!DX12BindingFrameExport::BindingIsUavBufferProducer(binding))
			continue;
		BufferProducer producer = DX12BindingFrameExport::ProducerFromBinding(binding);
		if (producer.begin == producer.end)
			continue;
		if (DX12BindingFrameExport::IsHuntingUavStateProducer(producer))
			huntingProducerCount++;
		else if (producer.bindSpace == "compute_cbv_srv_uav")
			computeProducerCount++;
		else
			otherProducerCount++;
		producers.push_back(std::move(producer));
	}

	size_t vbRows = 0;
	size_t ibRows = 0;
	size_t gpuPreskinRows = 0;
	size_t cpuPreskinRows = 0;
	size_t unknownRows = 0;
	size_t notApplicableRows = 0;
	size_t realProducerRows = 0;
	size_t huntingFallbackRows = 0;
	size_t huntingFallbackAvailableButIgnored = 0;
	for (FlatBufferRow &row : *buffers) {
		if (row.role != "VB") {
			row.skinningClass = "not_applicable";
			notApplicableRows++;
			ibRows++;
			continue;
		}
		vbRows++;

		const BufferProducer *producer = DX12BindingFrameExport::FindLatestProducerForBuffer(row, producers);
		const BufferProducer *huntingProducer =
			DX12BindingFrameExport::FindLatestHuntingUavStateProducerForBuffer(row, producers);
		if (producer && huntingProducer)
			huntingFallbackAvailableButIgnored++;
		if (!producer)
			producer = huntingProducer;
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
			gpuPreskinRows++;
			if (DX12BindingFrameExport::IsHuntingUavStateProducer(*producer))
				huntingFallbackRows++;
			else
				realProducerRows++;
		} else if (row.resolved) {
			row.skinningClass = "cpu_preskinning";
			cpuPreskinRows++;
		} else {
			row.skinningClass = "unknown";
			unknownRows++;
		}
	}

	DX12FrameAnalysis::LogJsonFunc("IaBufferSkinningClassification",
		"\"buffers\":%zu,\"vb\":%zu,\"ib\":%zu,\"producers\":%zu,\"compute_producers\":%zu,\"hunting_uav_state_producers\":%zu,\"other_producers\":%zu,\"gpu_preskinning\":%zu,\"gpu_preskinning_real_producer\":%zu,\"gpu_preskinning_hunting_fallback\":%zu,\"hunting_fallback_available_but_ignored\":%zu,\"cpu_preskinning\":%zu,\"unknown\":%zu,\"not_applicable\":%zu",
		buffers->size(),
		vbRows,
		ibRows,
		producers.size(),
		computeProducerCount,
		huntingProducerCount,
		otherProducerCount,
		gpuPreskinRows,
		realProducerRows,
		huntingFallbackRows,
		huntingFallbackAvailableButIgnored,
		cpuPreskinRows,
		unknownRows,
		notApplicableRows);
}

DX12FrameIaBufferBinding DX12BindingFrameExport::FrameIaBufferFromFlatRow(const FlatBufferRow &row)
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
	buffer.drawGpuVa = row.drawGpuVa;
	buffer.drawSize = row.drawSize;
	buffer.drawOffset = row.drawOffset;
	buffer.stride = row.stride;
	buffer.slot = row.slot;
	buffer.format = row.format;
	buffer.drawStartIndex = row.drawStartIndex;
	buffer.drawIndexCount = row.drawIndexCount;
	buffer.drawBaseVertex = row.drawBaseVertex;
	buffer.huntHash = row.huntHash;
	buffer.resolved = row.resolved;
	buffer.resource = row.resource;
	return buffer;
}

void DX12BindingFrameExport::BuildIaBufferFileName(
	const DX12FrameIaBufferBinding &buffer, wchar_t *fileName, size_t fileNameCount)
{
	if (!fileName || fileNameCount == 0)
		return;

	fileName[0] = L'\0';
	const char *role = buffer.role.empty() ? "BUF" : buffer.role.c_str();
	const wchar_t eventKind = buffer.dispatchId ? L'c' : L'd';
	const UINT64 eventId = buffer.dispatchId ? buffer.dispatchId : buffer.drawId;
	wchar_t shaderPart[96];
	DX12BindingFrameExport::BuildShaderHashNamePart(buffer.shaderInfo, shaderPart, ARRAYSIZE(shaderPart));
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
		static_cast<unsigned long long>(
			buffer.role == IaBufferRole::VB ? buffer.viewSize : buffer.size),
		buffer.stride,
		buffer.format);
}

void DX12BindingFrameExport::GetCurrentFrameIaBuffers(std::vector<DX12FrameIaBufferBinding> *buffers)
{
	std::vector<DX12FrameResourceBinding> resourceBindings;
	DX12BindingResourceExport::GetCurrentFrameResourceBindings(&resourceBindings);
	DX12BindingFrameExport::GetCurrentFrameIaBuffersWithResourceBindings(buffers, resourceBindings);
}

void DX12BindingFrameExport::GetCurrentFrameIaBuffersWithResourceBindings(
	std::vector<DX12FrameIaBufferBinding> *buffers,
	const std::vector<DX12FrameResourceBinding> &resourceBindings)
{
	if (!buffers)
		return;

	DX12FrameEventSource source;
	PopulatePreferredFrameEventSource(&source, "GetCurrentFrameIaBuffersWithResourceBindings");

	std::vector<FlatBufferRow> rows;
	DX12BindingFrameExport::CollectFrameIaBuffersForEvents(source.GetEvents(), &rows);
	DX12BindingFrameExport::ClassifyIaBufferSkinning(&rows, resourceBindings);

	buffers->clear();
	buffers->reserve(rows.size());
	for (const FlatBufferRow &row : rows)
		buffers->push_back(DX12BindingFrameExport::FrameIaBufferFromFlatRow(row));

	DX12FrameAnalysis::LogJsonFunc("IaBufferStage",
		"\"events\":%zu,\"buffers\":%zu,\"resource_bindings\":%zu,\"reused_resource_bindings\":1",
		source.GetEvents().size(), buffers->size(), resourceBindings.size());
}

DX12FrameAnalysisDumpResult DX12BindingFrameExport::DumpBindingTrace(const wchar_t *dir)
{
	DX12FrameAnalysisDumpResult result;
	if (!dir)
	{
		result.reason = "missing_directory";
		return result;
	}

	DX12FrameEventSource source;
	PopulatePreferredFrameEventSource(&source, "DumpBindingTrace");
	const std::vector<BindingEvent> &events = source.GetEvents();
	const UINT64 droppedEvents = source.droppedEvents;

	DX12FrameAnalysis::LogJsonFunc("BindingTraceBegin",
		"\"events\":%zu,\"frame_events\":%zu,\"ledger_events\":%zu,\"dropped\":%llu,\"maxEvents\":%u",
		events.size(), source.fallbackEvents.size(),
		source.usingLedger ? events.size() - source.fallbackEvents.size() : 0,
		static_cast<unsigned long long>(droppedEvents),
		MaxTrackedEvents);
	for (const BindingEvent &event : events) {
		if (DX12BindingTrackerQuery::IsDrawEvent(event) || DX12BindingTrackerQuery::IsDispatchEvent(event)) {
			DX12FrameAnalysisManifest::WriteCall(
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

	DX12FrameAnalysis::LogJsonFunc("BindingTraceEnd",
		"\"events\":%zu,\"dropped\":%llu",
		events.size(), static_cast<unsigned long long>(droppedEvents));
	result.ok = true;
	result.written = events.size();
	result.skipped = static_cast<size_t>(droppedEvents);
	return result;
}








