// DX12BindingFrameExport / DX12BindingResourceExport: binding dump and export.
// Split from DX12BindingTracking.cpp; implementation unit for its module.
#include "DX12Core.h"
#include "DX12Hooks.h"
#include "DX12Tracking.h"
#include "DX12Runtime.h"
#include "DX12Resources.h"
#include "DX12Presentation.h"
#include <chrono>
#include <stdio.h>
#include <string.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>




static bool PopulatePreferredFrameEventSource(DX12FrameEventSource *source, const char *reason)
{
	if (!source)
		return false;

	if (DX12BindingTracker::PopulateActiveEventSource(
			source,
			reason ? reason : "PopulatePreferredFrameEventSource"))
		return true;

	*source = DX12FrameEventSource();
	return false;
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

	if (DX12BindingFrameExport::ResolveIaBufferResourceSummaryFromCombinedRange(
			viewGpuVa,
			viewSize,
			drawGpuVa,
			drawSize,
			resource)) {
		return true;
	}

	return false;
}

bool DX12BindingFrameExport::ResolveIaBufferResourceSummaryFromCombinedRange(
	UINT64 viewGpuVa,
	UINT64 viewSize,
	UINT64 drawGpuVa,
	UINT64 drawSize,
	DX12BufferResourceSummary *resource)
{
	if (!resource || !viewGpuVa || !viewSize)
		return false;

	UINT64 combinedBegin = viewGpuVa;
	UINT64 combinedEnd = viewGpuVa + viewSize;
	if (combinedEnd < combinedBegin)
		return false;

	if (drawGpuVa && drawSize) {
		const UINT64 drawEnd = drawGpuVa + drawSize;
		if (drawEnd >= drawGpuVa) {
			if (drawGpuVa < combinedBegin)
				combinedBegin = drawGpuVa;
			if (drawEnd > combinedEnd)
				combinedEnd = drawEnd;
		}
	}

	if (combinedEnd <= combinedBegin)
		return false;

	DX12BufferResourceSummary combinedResource = {};
	if (!DX12ResolveBufferResourceByGpuVa(
			combinedBegin,
			combinedEnd - combinedBegin,
			&combinedResource))
		return false;

	return DX12BindingFrameExport::ProjectResolvedIaResourceToView(
		viewGpuVa,
		viewSize,
		drawGpuVa,
		drawSize,
		combinedResource,
		resource);
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
	if (ib) {
		row.drawStartIndex = event.startIndexLocation;
		row.drawIndexCount = event.indexCountPerInstance;
	} else {
		row.drawStartIndex = vb.StrideInBytes ? static_cast<UINT>(region.viewOffset / vb.StrideInBytes) : 0;
		row.drawIndexCount = vb.StrideInBytes ? static_cast<UINT>(region.size / vb.StrideInBytes) : 0;
	}
	row.drawBaseVertex = event.baseVertexLocation;
	row.drawStartInstance = event.startInstanceLocation;
	row.drawInstanceCount = event.instanceCount;
	row.topology = event.primitiveTopology;
	if (!ib)
		DX12GetPsoInputLayout(row.psoIndex, &row.inputLayout);
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
	buffer.drawStartInstance = row.drawStartInstance;
	buffer.drawInstanceCount = row.drawInstanceCount;
	buffer.topology = row.topology;
	buffer.inputLayout = row.inputLayout;
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
		"\"events\":%zu,\"capture_session_id\":%llu,\"dropped\":%llu,\"maxEvents\":0",
		events.size(),
		static_cast<unsigned long long>(source.captureSessionId),
		static_cast<unsigned long long>(droppedEvents));
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


static bool PopulatePreferredResourceEventSource(DX12FrameEventSource *source, const char *reason)
{
	if (!source)
		return false;

	if (DX12BindingTracker::PopulateActiveEventSource(
			source,
			reason ? reason : "PopulatePreferredResourceEventSource"))
		return true;

	*source = DX12FrameEventSource();
	return false;
}

bool DX12BindingResourceExport::InsertBindingSeen(
	std::unordered_set<std::string> *seen, const BindingEvent &event,
	const char *bindSpace, UINT rootParameterIndex, UINT rangeIndex, UINT rangeType,
	ID3D12DescriptorHeap *heap, UINT64 descriptorIndex, bool rootDescriptor)
{
	if (!seen)
		return false;

	char key[320];
	sprintf_s(key, "%llu|%llu|%llu|%llu|%s|%u|%u|%u|%p|%llu|%u",
		static_cast<unsigned long long>(event.serial),
		static_cast<unsigned long long>(event.drawId),
		static_cast<unsigned long long>(event.dispatchId),
		static_cast<unsigned long long>(event.shaderInfo.psoIndex),
		bindSpace ? bindSpace : "",
		rootParameterIndex,
		rangeIndex,
		rangeType,
		heap,
		static_cast<unsigned long long>(descriptorIndex),
		rootDescriptor ? 1 : 0);
	return seen->insert(key).second;
}

bool DX12BindingResourceExport::DescriptorMatchesRangeType(
	const DX12DescriptorSummary *descriptor, UINT rangeType)
{
	if (!descriptor)
		return true;
	if (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_CBV)
		return descriptor->kind == "CBV";
	if (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV)
		return descriptor->kind == "SRV";
	if (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV)
		return descriptor->kind == "UAV";
	if (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER)
		return descriptor->kind == "Sampler";
	return true;
}

const DX12DescriptorSummary *DX12BindingResourceExport::DescriptorForExpectedRange(
	const DX12DescriptorSummary *descriptor, UINT rangeType)
{
	return DX12BindingResourceExport::DescriptorMatchesRangeType(descriptor, rangeType) ? descriptor : nullptr;
}

const char *DX12BindingResourceExport::DescriptorKindForRangeType(UINT rangeType)
{
	if (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_CBV)
		return "CBV";
	if (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV)
		return "SRV";
	if (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV)
		return "UAV";
	if (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER)
		return "Sampler";
	return "";
}

const char *DX12BindingResourceExport::ExpectedBindingKind(const DX12FrameResourceBinding &binding)
{
	if (!binding.usageClass.empty())
		return binding.usageClass.c_str();
	if (binding.rootDescriptor) {
		if (binding.rangeType == D3D12_ROOT_PARAMETER_TYPE_CBV)
			return "CBV";
		if (binding.rangeType == D3D12_ROOT_PARAMETER_TYPE_SRV)
			return "SRV";
		if (binding.rangeType == D3D12_ROOT_PARAMETER_TYPE_UAV)
			return "UAV";
		return "";
	}
	if (binding.hasDescriptor)
		return binding.descriptor.kind.c_str();
	if (binding.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_CBV)
		return "CBV";
	if (binding.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV)
		return "SRV";
	if (binding.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV)
		return "UAV";
	if (binding.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER)
		return "Sampler";
	return "";
}


bool DX12BindingResourceExport::DescriptorBelongsToHeap(
	const DX12DescriptorSummary &descriptor, const DX12DescriptorHeapSummary &heap,
	UINT64 *descriptorIndex)
{
	if (!descriptor.cpuHandle || !heap.cpuStart || !heap.increment || !descriptorIndex)
		return false;
	if (descriptor.cpuHandle < heap.cpuStart)
		return false;
	const SIZE_T offset = descriptor.cpuHandle - heap.cpuStart;
	if (offset % heap.increment != 0)
		return false;
	const UINT64 index = offset / heap.increment;
	if (index >= heap.numDescriptors)
		return false;
	*descriptorIndex = index;
	return true;
}

void DX12BindingResourceExport::BuildDescriptorLookup(
	const std::vector<DX12DescriptorSummary> &descriptors,
	const std::vector<DX12DescriptorHeapSummary> &heaps,
	DescriptorLookup *lookup)
{
	if (!lookup)
		return;
	lookup->byCpuHandle.clear();
	lookup->byCpuHandle.reserve(descriptors.size());
	lookup->bySlot.clear();
	lookup->bySlot.reserve(descriptors.size());

	for (const DX12DescriptorSummary &descriptor : descriptors) {
		lookup->byCpuHandle[descriptor.cpuHandle] = &descriptor;
		for (const DX12DescriptorHeapSummary &heap : heaps) {
			UINT64 descriptorIndex = 0;
			if (!DX12BindingResourceExport::DescriptorBelongsToHeap(descriptor, heap, &descriptorIndex))
				continue;
			DescriptorSlotKey key;
			key.heap = heap.heap;
			key.descriptorIndex = descriptorIndex;
			lookup->bySlot[key] = &descriptor;
			break;
		}
	}
}

const DX12DescriptorSummary *DX12BindingResourceExport::FindDescriptorForSlot(
	const DescriptorLookup &lookup, const DX12DescriptorHeapSummary *heap,
	UINT64 descriptorIndex, SIZE_T cpuHandle)
{
	auto byCpu = lookup.byCpuHandle.find(cpuHandle);
	if (byCpu != lookup.byCpuHandle.end())
		return byCpu->second;
	if (!heap)
		return nullptr;
	DescriptorSlotKey key;
	key.heap = heap->heap;
	key.descriptorIndex = descriptorIndex;
	auto bySlot = lookup.bySlot.find(key);
	return bySlot != lookup.bySlot.end() ? bySlot->second : nullptr;
}

const DX12DescriptorSummary *DX12BindingResourceExport::FindDescriptorForEventSlot(
	DX12DescriptorSummary *historyStorage,
	const BindingEvent &event,
	const DescriptorLookup &lookup, const DX12DescriptorHeapSummary *heap,
	UINT64 descriptorIndex, SIZE_T cpuHandle, UINT rangeType,
	size_t *historyLookups, size_t *historyHits)
{
	if (historyStorage && event.descriptorEpoch) {
		if (historyLookups)
			(*historyLookups)++;
		const char *kind = DX12BindingResourceExport::DescriptorKindForRangeType(rangeType);
		if (FindDescriptorSummaryByCpuHandleAtEpochAndKind(
				cpuHandle, event.descriptorEpoch, kind, historyStorage) ||
				FindDescriptorSummaryByCpuHandleAtEpoch(
					cpuHandle, event.descriptorEpoch, historyStorage)) {
			if (historyHits)
				(*historyHits)++;
			return historyStorage;
		}
	}
	return DX12BindingResourceExport::FindDescriptorForSlot(lookup, heap, descriptorIndex, cpuHandle);
}

UINT DX12BindingResourceExport::UnknownTableRangeType(UINT heapType)
{
	return heapType == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER ?
		D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER : UINT_MAX;
}

bool DX12BindingResourceExport::MakeTableBinding(
	DX12FrameResourceBinding *binding, const BindingEvent &event, const char *bindSpace,
	const RootTableState &table, UINT rangeIndex, UINT rangeType,
	UINT shaderVisibility, UINT shaderRegister, UINT registerSpace, UINT descriptorOffset,
	const DX12DescriptorHeapSummary *heap, UINT64 descriptorIndex,
	UINT64 gpuHandle, SIZE_T cpuHandle, const DX12DescriptorSummary *descriptor)
{
	if (!binding || !heap)
		return false;

	binding->eventSerial = event.serial;
	binding->drawId = event.drawId;
	binding->dispatchId = event.dispatchId;
	binding->psoIndex = event.shaderInfo.psoIndex;
	binding->shaderInfo = event.shaderInfo;
	binding->bindSpace = bindSpace ? bindSpace : "";
	binding->rootParameterIndex = table.rootParameterIndex;
	binding->rangeIndex = rangeIndex;
	binding->rangeType = rangeType;
	binding->shaderVisibility = shaderVisibility;
	binding->shaderRegister = shaderRegister;
	binding->registerSpace = registerSpace;
	binding->descriptorOffset = descriptorOffset;
	binding->heap = heap->heap;
	binding->heapType = heap->type;
	binding->descriptorIndex = descriptorIndex;
	binding->gpuHandle = gpuHandle;
	binding->cpuHandle = cpuHandle;
	binding->heapGpuStart = heap->gpuStart;
	if (descriptor) {
		binding->descriptor = *descriptor;
		binding->hasDescriptor = true;
	}
	return true;
}

bool DX12BindingResourceExport::CollectDescriptorTableEntry(
	std::vector<DX12FrameResourceBinding> *bindings,
	const BindingEvent &event, const char *bindSpace,
	const RootTableState &table, UINT rangeIndex, UINT rangeType,
	UINT shaderVisibility, UINT shaderRegister, UINT registerSpace, UINT descriptorOffset,
	const DX12DescriptorHeapSummary *heap, UINT64 descriptorIndex,
	UINT64 gpuHandle, SIZE_T cpuHandle,
	const DX12DescriptorSummary *descriptor,
	std::unordered_set<std::string> *seen, size_t *untrackedSkipped)
{
	if (!bindings || !heap)
		return false;
	const DX12DescriptorSummary *expectedDescriptor =
		DX12BindingResourceExport::DescriptorForExpectedRange(descriptor, rangeType);
	if (!DX12BindingResourceExport::InsertBindingSeen(seen, event, bindSpace, table.rootParameterIndex,
			rangeIndex, rangeType, heap->heap, descriptorIndex, false))
		return false;
	if (!expectedDescriptor && untrackedSkipped)
		(*untrackedSkipped)++;

	DX12FrameResourceBinding binding;
	if (!DX12BindingResourceExport::MakeTableBinding(&binding, event, bindSpace, table, rangeIndex, rangeType,
			shaderVisibility, shaderRegister, registerSpace, descriptorOffset,
			heap, descriptorIndex, gpuHandle, cpuHandle, expectedDescriptor))
		return false;
	bindings->push_back(std::move(binding));
	return true;
}

bool DX12BindingResourceExport::VisitTableStartOnly(
	const RootTableState &table, UINT heapType,
	const std::vector<DX12DescriptorHeapSummary> &heaps,
	UINT64 *descriptorIndex, UINT64 *gpuHandle, SIZE_T *cpuHandle,
	const DX12DescriptorHeapSummary **heap)
{
	if (!descriptorIndex || !gpuHandle || !cpuHandle || !heap)
		return false;
	*heap = DX12BindingTrackerQuery::FindHeapForGpuHandle(heaps, table, heapType);
	if (!*heap || (*heap)->increment == 0)
		return false;
	*descriptorIndex = (table.baseDescriptor.ptr - (*heap)->gpuStart) / (*heap)->increment;
	if (*descriptorIndex >= (*heap)->numDescriptors)
		return false;
	*gpuHandle = (*heap)->gpuStart + *descriptorIndex * (*heap)->increment;
	*cpuHandle = (*heap)->cpuStart + static_cast<SIZE_T>(*descriptorIndex) * (*heap)->increment;
	return true;
}

bool DX12BindingResourceExport::IsCompatibleRangeForSpace(const DX12RootDescriptorRangeSummary &range, const char *bindSpace)
{
	if (!bindSpace)
		return true;
	const bool samplerSpace = strstr(bindSpace, "sampler") != nullptr;
	return samplerSpace == (range.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER);
}

bool DX12BindingResourceExport::IsTextureResourceDimension(D3D12_RESOURCE_DIMENSION dimension)
{
	return dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D ||
		dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
		dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D;
}


void DX12BindingResourceExport::CollectFrameResourceBinding(
	std::vector<DX12FrameResourceBinding> *bindings,
	const BindingEvent &event, const char *bindSpace,
	const RootTableState &table, UINT heapType,
	const std::vector<DX12DescriptorHeapSummary> &heaps,
	const DescriptorLookup &descriptors,
	std::unordered_set<std::string> *seen, size_t *untrackedSkipped,
	size_t *historyLookups, size_t *historyHits)
{
	if (!bindings || !table.valid || !seen)
		return;

	DX12RootSignatureSummary rootSignature;
	const bool compute = bindSpace && !strncmp(bindSpace, "compute", 7);
	const DX12RootParameterSummary *parameter = nullptr;
	if (DX12BindingTrackerQuery::GetRootSignatureForEvent(event, compute, &rootSignature))
		parameter = DX12BindingTrackerQuery::FindRootParameter(rootSignature, table.rootParameterIndex);

	bool collectedRange = false;
	if (parameter && parameter->parameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
		for (const DX12RootDescriptorRangeSummary &range : parameter->ranges) {
			if (!DX12BindingResourceExport::IsCompatibleRangeForSpace(range, bindSpace))
				continue;
			const DX12DescriptorHeapSummary *heap = DX12BindingTrackerQuery::FindHeapForTableRange(heaps, table, &range, heapType);
			if (!heap)
				continue;
			const UINT64 tableBaseIndex = (table.baseDescriptor.ptr - heap->gpuStart) / heap->increment;
			const UINT count = DX12BindingTrackerQuery::DescriptorCountForRange(*heap, tableBaseIndex, range);
			for (UINT offset = 0; offset < count; ++offset) {
				const UINT64 descriptorIndex = tableBaseIndex + range.effectiveOffset + offset;
				if (descriptorIndex >= heap->numDescriptors)
					break;
				const UINT64 gpuHandle = heap->gpuStart + descriptorIndex * heap->increment;
				const SIZE_T cpuHandle = heap->cpuStart + static_cast<SIZE_T>(descriptorIndex) * heap->increment;
				DX12DescriptorSummary historyStorage;
				const DX12DescriptorSummary *descriptor = DX12BindingResourceExport::FindDescriptorForEventSlot(
					&historyStorage, event, descriptors, heap, descriptorIndex, cpuHandle,
					range.rangeType, historyLookups, historyHits);
				if (DX12BindingResourceExport::CollectDescriptorTableEntry(bindings, event, bindSpace, table,
						range.rangeIndex, range.rangeType, range.shaderVisibility,
						range.baseShaderRegister + offset, range.registerSpace,
						range.effectiveOffset + offset,
						heap, descriptorIndex, gpuHandle, cpuHandle, descriptor,
						seen, untrackedSkipped))
					collectedRange = true;
			}
		}
	}

	if (collectedRange)
		return;

	UINT64 descriptorIndex = 0;
	UINT64 gpuHandle = 0;
	SIZE_T cpuHandle = 0;
	const DX12DescriptorHeapSummary *heap = nullptr;
	if (DX12BindingResourceExport::VisitTableStartOnly(table, heapType, heaps, &descriptorIndex,
			&gpuHandle, &cpuHandle, &heap)) {
		DX12DescriptorSummary historyStorage;
		const DX12DescriptorSummary *descriptor = DX12BindingResourceExport::FindDescriptorForEventSlot(
			&historyStorage, event, descriptors, heap, descriptorIndex, cpuHandle,
			DX12BindingResourceExport::UnknownTableRangeType(heapType), historyLookups, historyHits);
		// Same design as file export: keep the table base binding when root metadata is absent,
		// but do not fabricate descriptor ranges that D3D12 did not expose.
		DX12BindingResourceExport::CollectDescriptorTableEntry(bindings, event, bindSpace, table, UINT_MAX,
			DX12BindingResourceExport::UnknownTableRangeType(heapType), D3D12_SHADER_VISIBILITY_ALL, UINT_MAX, 0, 0,
			heap, descriptorIndex, gpuHandle, cpuHandle, descriptor, seen, untrackedSkipped);
	}
}

void DX12BindingResourceExport::CollectFrameRootDescriptorBinding(
	std::vector<DX12FrameResourceBinding> *bindings,
	const BindingEvent &event, const char *bindSpace,
	const RootDescriptorState &rootDescriptor,
	std::unordered_set<std::string> *seen)
{
	if (!bindings || !rootDescriptor.valid || !seen)
		return;
	if (!DX12BindingResourceExport::InsertBindingSeen(seen, event, bindSpace, rootDescriptor.rootParameterIndex,
			UINT_MAX, static_cast<UINT>(rootDescriptor.type),
			nullptr, rootDescriptor.address, true))
		return;

	DX12RootSignatureSummary rootSignature;
	const bool compute = bindSpace && !strncmp(bindSpace, "compute", 7);
	const DX12RootParameterSummary *parameter = nullptr;
	if (DX12BindingTrackerQuery::GetRootSignatureForEvent(event, compute, &rootSignature))
		parameter = DX12BindingTrackerQuery::FindRootParameter(rootSignature, rootDescriptor.rootParameterIndex);

	DX12FrameResourceBinding binding;
	binding.eventSerial = event.serial;
	binding.drawId = event.drawId;
	binding.dispatchId = event.dispatchId;
	binding.psoIndex = event.shaderInfo.psoIndex;
	binding.shaderInfo = event.shaderInfo;
	binding.bindSpace = bindSpace ? bindSpace : "";
	binding.rootParameterIndex = rootDescriptor.rootParameterIndex;
	binding.rangeIndex = UINT_MAX;
	binding.rangeType = static_cast<UINT>(rootDescriptor.type);
	binding.shaderVisibility = parameter ? parameter->shaderVisibility : D3D12_SHADER_VISIBILITY_ALL;
	binding.shaderRegister = parameter ? parameter->shaderRegister : UINT_MAX;
	binding.registerSpace = parameter ? parameter->registerSpace : 0;
	binding.rootDescriptor = true;
	binding.gpuVirtualAddress = rootDescriptor.address;
	DX12BufferResourceSummary resource;
	if (DX12ResolveBufferResourceByGpuVa(rootDescriptor.address, 1, &resource) &&
			resource.resource && resource.hasResourceDesc) {
		// Root descriptors carry only a GPU VA in D3D12, so project the resolved
		// buffer into descriptor form here. This keeps compute UAV producers on
		// the same event-resource path as descriptor tables, matching RenderDoc's
		// resource-usage granularity without scanning resources in Mod mode.
		binding.descriptor.kind = DX12BindingResourceExport::ExpectedBindingKind(binding);
		binding.descriptor.resource = resource.resource;
		binding.descriptor.resourceDesc = resource.resourceDesc;
		binding.descriptor.hasResourceDesc = true;
		binding.descriptor.resourceHeapType = resource.resourceHeapType;
		binding.descriptor.hasResourceHeapType = resource.hasResourceHeapType;
		binding.descriptor.gpuVirtualAddress = resource.gpuVirtualAddress;
		binding.descriptor.resourceOffset = resource.resourceOffset;
		const UINT64 availableBytes = resource.resourceDesc.Width > resource.resourceOffset ?
			resource.resourceDesc.Width - resource.resourceOffset : 0;
		binding.descriptor.viewSize = availableBytes;
		binding.descriptor.currentState = resource.currentState;
		binding.descriptor.hasCurrentState = resource.hasCurrentState;
		binding.hasDescriptor = true;
	}
	bindings->push_back(std::move(binding));
}

void DX12BindingResourceExport::CountEventTables(
	const BindingEvent &event,
	const std::vector<DX12DescriptorHeapSummary> &heaps,
	size_t *graphicsTables, size_t *computeTables,
	size_t *graphicsTablesWithRoots, size_t *computeTablesWithRoots,
	size_t *rootRangeTables, size_t *tableStartOnly,
	size_t *unboundedRanges, size_t *unboundedDescriptors,
	size_t *graphicsTablesWithoutRoots, size_t *computeTablesWithoutRoots,
	size_t *descriptorTablesWithoutRanges)
{
	if (!graphicsTables || !computeTables || !graphicsTablesWithRoots ||
			!computeTablesWithRoots || !rootRangeTables || !tableStartOnly ||
			!unboundedRanges || !unboundedDescriptors ||
			!graphicsTablesWithoutRoots || !computeTablesWithoutRoots ||
			!descriptorTablesWithoutRanges)
		return;

	DX12RootSignatureSummary graphicsRoot;
	DX12RootSignatureSummary computeRoot;
	const bool hasGraphicsRoot = DX12BindingTrackerQuery::GetRootSignatureForEvent(event, false, &graphicsRoot);
	const bool hasComputeRoot = DX12BindingTrackerQuery::GetRootSignatureForEvent(event, true, &computeRoot);
	for (UINT i = 0; i < MaxRootParameters; ++i) {
		if (event.graphicsTables[i].valid) {
			(*graphicsTables)++;
			if (hasGraphicsRoot) {
				(*graphicsTablesWithRoots)++;
				const DX12RootParameterSummary *parameter =
					DX12BindingTrackerQuery::FindRootParameter(graphicsRoot, i);
				if (parameter && parameter->parameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
					(*rootRangeTables)++;
					const DX12DescriptorHeapSummary *heap =
						DX12BindingTrackerQuery::FindHeapForGpuHandle(heaps, event.graphicsTables[i],
							D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
					if (heap && heap->increment) {
						const UINT64 tableBaseIndex =
							(event.graphicsTables[i].baseDescriptor.ptr - heap->gpuStart) / heap->increment;
						for (const DX12RootDescriptorRangeSummary &range : parameter->ranges) {
							if (range.numDescriptors != UINT_MAX)
								continue;
							(*unboundedRanges)++;
							(*unboundedDescriptors) +=
								DX12BindingTrackerQuery::DescriptorCountForRange(*heap, tableBaseIndex, range);
						}
					}
				} else {
					(*tableStartOnly)++;
					(*descriptorTablesWithoutRanges)++;
				}
			} else {
				(*tableStartOnly)++;
				(*graphicsTablesWithoutRoots)++;
			}
		}
		if (event.computeTables[i].valid) {
			(*computeTables)++;
			if (hasComputeRoot) {
				(*computeTablesWithRoots)++;
				const DX12RootParameterSummary *parameter =
					DX12BindingTrackerQuery::FindRootParameter(computeRoot, i);
				if (parameter && parameter->parameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
					(*rootRangeTables)++;
					const DX12DescriptorHeapSummary *heap =
						DX12BindingTrackerQuery::FindHeapForGpuHandle(heaps, event.computeTables[i],
							D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
					if (heap && heap->increment) {
						const UINT64 tableBaseIndex =
							(event.computeTables[i].baseDescriptor.ptr - heap->gpuStart) / heap->increment;
						for (const DX12RootDescriptorRangeSummary &range : parameter->ranges) {
							if (range.numDescriptors != UINT_MAX)
								continue;
							(*unboundedRanges)++;
							(*unboundedDescriptors) +=
								DX12BindingTrackerQuery::DescriptorCountForRange(*heap, tableBaseIndex, range);
						}
					}
				} else {
					(*tableStartOnly)++;
					(*descriptorTablesWithoutRanges)++;
				}
			} else {
				(*tableStartOnly)++;
				(*computeTablesWithoutRoots)++;
			}
		}
	}
}

void DX12BindingResourceExport::CollectEventResourceBindings(
	std::vector<DX12FrameResourceBinding> *bindings,
	const BindingEvent &event,
	const std::vector<DX12DescriptorHeapSummary> &heaps,
	const DescriptorLookup &descriptorLookup,
	std::unordered_set<std::string> *seen,
	size_t *untrackedSkipped,
	size_t *historyLookups,
	 size_t *historyHits)
{
	if (!bindings || !seen)
		return;
	for (UINT i = 0; i < MaxRootParameters; ++i) {
		DX12BindingResourceExport::CollectFrameResourceBinding(bindings, event, "graphics_cbv_srv_uav",
			event.graphicsTables[i], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
			heaps, descriptorLookup, seen, untrackedSkipped, historyLookups, historyHits);
		DX12BindingResourceExport::CollectFrameResourceBinding(bindings, event, "graphics_sampler",
			event.graphicsTables[i], D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
			heaps, descriptorLookup, seen, untrackedSkipped, historyLookups, historyHits);
		DX12BindingResourceExport::CollectFrameResourceBinding(bindings, event, "compute_cbv_srv_uav",
			event.computeTables[i], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
			heaps, descriptorLookup, seen, untrackedSkipped, historyLookups, historyHits);
		DX12BindingResourceExport::CollectFrameResourceBinding(bindings, event, "compute_sampler",
			event.computeTables[i], D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
			heaps, descriptorLookup, seen, untrackedSkipped, historyLookups, historyHits);
		DX12BindingResourceExport::CollectFrameRootDescriptorBinding(bindings, event, "graphics_root",
			event.graphicsRootDescriptors[i], seen);
		DX12BindingResourceExport::CollectFrameRootDescriptorBinding(bindings, event, "compute_root",
			event.computeRootDescriptors[i], seen);
	}
}

void DX12BindingResourceExport::GetCurrentFrameResourceBindings(std::vector<DX12FrameResourceBinding> *bindings)
{
	if (!bindings)
		return;

	DX12FrameEventSource source;
	PopulatePreferredResourceEventSource(&source, "GetCurrentFrameResourceBindings");
	const std::vector<BindingEvent> &events = source.GetEvents();
	DX12FrameAnalysis::LogJsonFunc("BindingResourcesSnapshot",
		"events=%zu,capture_session_id=%llu",
		events.size(),
		static_cast<unsigned long long>(source.captureSessionId));

	std::vector<DX12DescriptorSummary> descriptors;
	std::vector<DX12DescriptorHeapSummary> heaps;
	DX12GetResourceMetadataSnapshot(nullptr, &descriptors, nullptr, &heaps);
	DX12FrameAnalysis::LogJsonFunc("BindingResourcesMetadataReady",
		"descriptors=%zu,heaps=%zu",
		descriptors.size(), heaps.size());
	DescriptorLookup descriptorLookup;
	DX12BindingResourceExport::BuildDescriptorLookup(descriptors, heaps, &descriptorLookup);
	DX12FrameAnalysis::LogJsonFunc("BindingResourcesLookupReady",
		"cpu_entries=%zu,slot_entries=%zu",
		descriptorLookup.byCpuHandle.size(), descriptorLookup.bySlot.size());

	bindings->clear();
	std::unordered_set<std::string> seen;
	size_t drawEvents = 0;
	size_t dispatchEvents = 0;
	size_t graphicsTables = 0;
	size_t computeTables = 0;
	size_t graphicsTablesWithRoots = 0;
	size_t computeTablesWithRoots = 0;
	size_t rootRangeTables = 0;
	size_t tableStartOnly = 0;
	size_t unboundedRanges = 0;
	size_t unboundedDescriptors = 0;
	size_t graphicsTablesWithoutRoots = 0;
	size_t computeTablesWithoutRoots = 0;
	size_t descriptorTablesWithoutRanges = 0;
	size_t samplerBindings = 0;
	size_t cbvBindings = 0;
	size_t srvBindings = 0;
	size_t uavBindings = 0;
	size_t untrackedBindings = 0;
	size_t untrackedCbvBindings = 0;
	size_t untrackedSrvBindings = 0;
	size_t untrackedUavBindings = 0;
	size_t untrackedSkipped = 0;
	size_t textureBindings = 0;
	size_t rebuiltEventBindings = 0;
	size_t rebuiltResolvedBindings = 0;
	size_t descriptorHistoryLookups = 0;
	size_t descriptorHistoryHits = 0;
	size_t zeroDescriptorEpochEvents = 0;
	for (const BindingEvent &event : events) {
		if (!DX12BindingTrackerQuery::IsDrawEvent(event) && !DX12BindingTrackerQuery::IsDispatchEvent(event))
			continue;
		if (event.descriptorEpoch == 0)
			zeroDescriptorEpochEvents++;
		if (DX12BindingTrackerQuery::IsDrawEvent(event))
			drawEvents++;
		if (DX12BindingTrackerQuery::IsDispatchEvent(event))
			dispatchEvents++;
		DX12BindingResourceExport::CountEventTables(event, heaps, &graphicsTables, &computeTables,
			&graphicsTablesWithRoots, &computeTablesWithRoots,
			&rootRangeTables, &tableStartOnly, &unboundedRanges,
			&unboundedDescriptors, &graphicsTablesWithoutRoots,
			&computeTablesWithoutRoots, &descriptorTablesWithoutRanges);
		const size_t before = bindings->size();
		// Rebuild from the final descriptor metadata snapshot. Event recording only
		// stores root table/root descriptor state; resolving resources here matches
		// RenderDoc's event-granular model and avoids hot-path descriptor scans.
		DX12BindingResourceExport::CollectEventResourceBindings(
			bindings, event, heaps, descriptorLookup, &seen, &untrackedSkipped,
			&descriptorHistoryLookups, &descriptorHistoryHits);
		rebuiltEventBindings += bindings->size() - before;
		for (size_t i = before; i < bindings->size(); ++i) {
			if ((*bindings)[i].hasDescriptor)
				rebuiltResolvedBindings++;
		}
	}
	DX12FrameAnalysis::LogJsonFunc("BindingResourcesDiscoveryFallbackSkipped",
		"\"reason\":\"single_frame_event_scope\",\"hunting_uav_state\":0,\"active_pre_skin\":0");
	for (const DX12FrameResourceBinding &binding : *bindings) {
		const char *expectedKind = DX12BindingResourceExport::ExpectedBindingKind(binding);
		if (!binding.hasDescriptor) {
			untrackedBindings++;
			if (!strcmp(expectedKind, "CBV"))
				untrackedCbvBindings++;
			else if (!strcmp(expectedKind, "SRV"))
				untrackedSrvBindings++;
			else if (!strcmp(expectedKind, "UAV"))
				untrackedUavBindings++;
			continue;
		}
		if (!strcmp(expectedKind, "CBV"))
			cbvBindings++;
		else if (!strcmp(expectedKind, "SRV"))
			srvBindings++;
		else if (!strcmp(expectedKind, "UAV"))
			uavBindings++;
		else if (!strcmp(expectedKind, "Sampler"))
			samplerBindings++;
		if (binding.descriptor.hasResourceDesc &&
			DX12BindingResourceExport::IsTextureResourceDimension(binding.descriptor.resourceDesc.Dimension))
			textureBindings++;
	}
	DX12FrameAnalysis::LogJsonFunc("BindingResourcesCollected",
		"bindings=%zu,unique=%zu,rebuilt_bindings=%zu,rebuilt_resolved=%zu,descriptor_history_lookups=%zu,descriptor_history_hits=%zu,zero_descriptor_epoch_events=%zu,draw_events=%zu,dispatch_events=%zu,graphics_tables=%zu,compute_tables=%zu,graphics_tables_with_roots=%zu,compute_tables_with_roots=%zu,root_range_tables=%zu,table_start_only=%zu,graphics_tables_without_roots=%zu,compute_tables_without_roots=%zu,descriptor_tables_without_ranges=%zu,unbounded_ranges=%zu,unbounded_descriptors=%zu,cbv=%zu,srv=%zu,uav=%zu,sampler=%zu,texture=%zu,untracked=%zu,untracked_cbv=%zu,untracked_srv=%zu,untracked_uav=%zu,untracked_skipped=%zu",
		bindings->size(), seen.size(), rebuiltEventBindings, rebuiltResolvedBindings,
		descriptorHistoryLookups, descriptorHistoryHits, zeroDescriptorEpochEvents,
		drawEvents, dispatchEvents,
		graphicsTables, computeTables,
		graphicsTablesWithRoots, computeTablesWithRoots,
		rootRangeTables, tableStartOnly,
		graphicsTablesWithoutRoots, computeTablesWithoutRoots,
		descriptorTablesWithoutRanges,
		unboundedRanges, unboundedDescriptors,
		cbvBindings, srvBindings, uavBindings, samplerBindings,
		textureBindings, untrackedBindings,
		untrackedCbvBindings, untrackedSrvBindings, untrackedUavBindings,
		untrackedSkipped);
	const char *historyStatus = "ok";
	if (zeroDescriptorEpochEvents)
		historyStatus = "zero_epoch";
	else if (descriptorHistoryLookups && !descriptorHistoryHits)
		historyStatus = "miss";
	else if (untrackedUavBindings && !descriptorHistoryLookups)
		historyStatus = "no_history_lookup";
	else if (untrackedUavBindings)
		historyStatus = "untracked_uav";
	DX12FrameAnalysis::LogJsonFunc("BindingResourcesHistoryHealth",
		"status=%s,events=%zu,zero_descriptor_epoch_events=%zu,descriptor_history_lookups=%zu,descriptor_history_hits=%zu,untracked_uav=%zu",
		historyStatus,
		drawEvents + dispatchEvents,
		zeroDescriptorEpochEvents,
		descriptorHistoryLookups,
		descriptorHistoryHits,
		untrackedUavBindings);
}

void DX12BindingResourceExport::GetCurrentFrameOutputBindings(std::vector<DX12FrameOutputBinding> *bindings)
{
	if (!bindings)
		return;

	DX12FrameEventSource source;
	PopulatePreferredResourceEventSource(&source, "GetCurrentFrameOutputBindings");
	const std::vector<OutputBindingEvent> &events = source.GetOutputEvents();
	const UINT64 dropped = source.droppedOutputEvents;
	DX12FrameAnalysis::LogJsonFunc("OutputResourcesLedgerSnapshot",
		"events=%zu,capture_session_id=%llu,dropped=%llu",
		events.size(),
		static_cast<unsigned long long>(source.captureSessionId),
		static_cast<unsigned long long>(dropped));

	std::unordered_set<std::string> seen;
	bindings->clear();
	bindings->reserve(events.size());
	size_t rtv = 0;
	size_t dsv = 0;
	size_t untracked = 0;
	std::vector<OutputBindingEvent> uniqueEvents;
	std::vector<SIZE_T> cpuHandles;
	uniqueEvents.reserve(events.size());
	cpuHandles.reserve(events.size());
	for (const OutputBindingEvent &event : events) {
		if (!event.cpuHandle)
			continue;
		char key[160];
		sprintf_s(key, "%s|%u|%zx|%llu|%llu|%llu",
			event.kind.c_str(),
			event.slot,
			event.cpuHandle,
			static_cast<unsigned long long>(event.drawId),
			static_cast<unsigned long long>(event.dispatchId),
			static_cast<unsigned long long>(
				(event.drawId || event.dispatchId) ? 0 : event.serial));
		if (!seen.insert(key).second)
			continue;
		uniqueEvents.push_back(event);
		cpuHandles.push_back(event.cpuHandle);
	}

	std::vector<DX12DescriptorSummary> descriptorSummaries(uniqueEvents.size());
	std::vector<uint8_t> descriptorFound(uniqueEvents.size());
	DX12FindDescriptorSummariesByCpuHandle(
		cpuHandles.data(), cpuHandles.size(),
		descriptorSummaries.data(), descriptorFound.data(), true);

	for (size_t i = 0; i < uniqueEvents.size(); ++i) {
		const OutputBindingEvent &event = uniqueEvents[i];
		DX12FrameOutputBinding binding;
		binding.kind = event.kind;
		binding.eventSerial = event.serial;
		binding.drawId = event.drawId;
		binding.dispatchId = event.dispatchId;
		binding.psoIndex = event.shaderInfo.psoIndex;
		binding.shaderInfo = event.shaderInfo;
		binding.slot = event.slot;
		binding.cpuHandle = event.cpuHandle;
		binding.hasDescriptor = descriptorFound[i] != 0;
		if (binding.hasDescriptor)
			binding.descriptor = descriptorSummaries[i];
		if (binding.kind == "RTV")
			rtv++;
		else if (binding.kind == "DSV")
			dsv++;
		if (!binding.hasDescriptor)
			untracked++;
		bindings->push_back(std::move(binding));
	}

	DX12FrameAnalysis::LogJsonFunc("OutputResourcesCollected",
		"bindings=%zu,rtv=%zu,dsv=%zu,untracked=%zu,dropped=%llu",
		bindings->size(), rtv, dsv, untracked,
		static_cast<unsigned long long>(dropped));
	DX12_LOG("DX12OutputResourcesCollected",
		"bindings=%zu,rtv=%zu,dsv=%zu,untracked=%zu,dropped=%llu",
		bindings->size(), rtv, dsv, untracked,
		static_cast<unsigned long long>(dropped));
}
