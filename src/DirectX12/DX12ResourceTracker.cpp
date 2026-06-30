#include "DX12ResourceTrackerPrivate.hpp"

#include "DX12ResourceCollectQueue.hpp"

uint32_t HashBytes(uint32_t seed, const void *data, size_t size)
{
	if (!data || size == 0)
		return seed;
	__try {
		return crc32c_append(seed, static_cast<const uint8_t*>(data), size);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return 0;
	}
}

SRWLOCK gResourceLock = SRWLOCK_INIT;
SRWLOCK gDescriptorLock = SRWLOCK_INIT;
std::vector<RootSignatureRecord> gRootSignatures;
std::vector<DescriptorHeapRecord> gDescriptorHeaps;
std::unordered_map<ID3D12RootSignature*, size_t> gRootSignatureByPtr;
std::unordered_map<ID3D12DescriptorHeap*, size_t> gDescriptorHeapByPtr;
std::map<SIZE_T, size_t> gDescriptorHeapByCpuStart;
std::unordered_map<SIZE_T, DescriptorRecord> gDescriptorByCpuHandle;
std::vector<PsoRootRecord> gPsoRoots;
std::unordered_map<UINT64, size_t> gPsoRootByIndex;
std::vector<ResourceRecord> gResources;
std::vector<size_t> gFreeResourceSlots;
size_t gLiveResourceCount = 0;
std::unordered_map<ID3D12Resource*, size_t> gResourceByPtr;
std::map<UINT64, size_t> gBufferResourceByGpuVa;
std::map<UINT64, BufferResolveCacheEntry> gBufferResolveCache;
std::atomic<UINT64> gBufferResolveGeneration{1};
bool gCleanupRegistered = false;
UINT64 gResourcesRecordedFromCreate = 0;
UINT64 gDescriptorRecordsSeen = 0;
UINT64 gDescriptorCopyRecordsSeen = 0;
UINT64 gDescriptorHeapRecordsSeen = 0;
UINT64 gRootSignatureRecordsSeen = 0;
UINT64 gPsoRootRecordsSeen = 0;
UINT64 gDescriptorEpoch = 0;
LONG gLastResourceStatsPresent = -1;
static constexpr LONG kResourceStatsLogInterval = 300;

void LogResourceTrackerStatsLocked();

static bool DescriptorHeapSlotOccupied(const DescriptorHeapRecord &heap, UINT slot)
{
	return slot < heap.occupied.size() && heap.occupied[slot] != 0;
}

static bool DescriptorHeapIsShaderVisible(const DescriptorHeapRecord &heap)
{
	return (heap.desc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) != 0;
}

static bool ShouldStoreCreatedDescriptorInHeapSlot(const DescriptorHeapRecord &heap)
{
	return DX12ResourceTrackingPolicyUtils::NeedsDescriptorTracking() &&
		(DescriptorHeapIsShaderVisible(heap) ||
		 DX12ResourceTrackingPolicyUtils::NeedsFullDescriptorHistory());
}

static DescriptorRecord *DescriptorHeapSlot(DescriptorHeapRecord *heap, UINT slot)
{
	if (!heap || !DescriptorHeapSlotOccupied(*heap, slot) || slot >= heap->slots.size())
		return nullptr;
	return &heap->slots[slot];
}

static const DescriptorRecord *DescriptorHeapSlot(const DescriptorHeapRecord *heap, UINT slot)
{
	if (!heap || !DescriptorHeapSlotOccupied(*heap, slot) || slot >= heap->slots.size())
		return nullptr;
	return &heap->slots[slot];
}

static void ClearDescriptorHeapSlotLocked(DescriptorHeapRecord *heap, UINT slot)
{
	if (!heap || !DescriptorHeapSlotOccupied(*heap, slot) || slot >= heap->slots.size())
		return;
	DescriptorRecord &record = heap->slots[slot];
	ReleaseDescriptorRecordResources(&record);
	record = DescriptorRecord();
	heap->occupied[slot] = 0;
	if (heap->liveSlots > 0)
		heap->liveSlots--;
}

static void ClearDescriptorHeapLocked(DescriptorHeapRecord *heap)
{
	if (!heap)
		return;
	for (UINT slot = 0; slot < heap->occupied.size(); ++slot)
		ClearDescriptorHeapSlotLocked(heap, slot);
	for (std::vector<DescriptorRecord> &history : heap->slotHistory) {
		for (DescriptorRecord &record : history)
			ReleaseDescriptorRecordResources(&record);
	}
	heap->slots.clear();
	heap->occupied.clear();
	heap->slotHistory.clear();
	heap->slotHistoryEpochs.clear();
	heap->liveSlots = 0;
}

void InitializeDescriptorHeapRecord(DescriptorHeapRecord *record)
{
	if (!record || record->desc.NumDescriptors == 0)
		return;
	record->liveSlots = 0;
}

bool EnsureDescriptorHeapStorageLocked(DescriptorHeapRecord *record)
{
	if (!record || record->desc.NumDescriptors == 0)
		return false;
	if (!record->slots.empty())
		return record->slots.size() == record->desc.NumDescriptors;
	// Allocate descriptor slots only after a real tracking need appears. Games
	// can create million-entry heaps during normal Mod mode; keeping only heap
	// boundaries until Mod/Dump needs the slots avoids turning capacity into CPU
	// work and memory pressure.
	record->slots.resize(record->desc.NumDescriptors);
	record->occupied.assign(record->desc.NumDescriptors, 0);
	record->liveSlots = 0;
	return true;
}

static bool EnsureDescriptorHeapHistoryLocked(DescriptorHeapRecord *record)
{
	if (!record || record->desc.NumDescriptors == 0)
		return false;
	if (record->slotHistory.empty())
		record->slotHistory.resize(record->desc.NumDescriptors);
	if (record->slotHistoryEpochs.empty())
		record->slotHistoryEpochs.resize(record->desc.NumDescriptors);
	return record->slotHistory.size() == record->desc.NumDescriptors &&
		record->slotHistoryEpochs.size() == record->desc.NumDescriptors;
}

static void AppendDescriptorHistoryLocked(DescriptorHeapRecord *heap, UINT slot, const DescriptorRecord &record)
{
	if (!DX12ResourceTrackingPolicyUtils::NeedsFullDescriptorHistory())
		return;
	if (!heap || !EnsureDescriptorHeapHistoryLocked(heap) ||
			slot >= heap->slotHistory.size() || slot >= heap->slotHistoryEpochs.size())
		return;
	// Hunting needs RenderDoc-like event granularity: later descriptor heap
	// reuse must not rewrite what an earlier dispatch saw.
	++gDescriptorEpoch;
	heap->slotHistory[slot].push_back(CloneDescriptorRecord(record));
	heap->slotHistoryEpochs[slot].push_back(gDescriptorEpoch);
}

static void StoreDescriptorAtHeapSlotLocked(
	DescriptorHeapRecord *heap, UINT slot, DescriptorRecord &&record)
{
	if (!heap || !EnsureDescriptorHeapStorageLocked(heap) || slot >= heap->slots.size()) {
		ReleaseDescriptorRecordResources(&record);
		return;
	}
	const bool wasOccupied = DescriptorHeapSlotOccupied(*heap, slot);
	AppendDescriptorHistoryLocked(heap, slot, record);
	heap->slots[slot] = std::move(record);
	heap->occupied[slot] = 1;
	if (!wasOccupied)
		heap->liveSlots++;
}

static size_t LiveDescriptorHeapSlotsLocked()
{
	size_t live = 0;
	for (const DescriptorHeapRecord &heap : gDescriptorHeaps)
		live += heap.liveSlots;
	return live;
}

static size_t LiveShaderVisibleDescriptorHeapSlotsLocked()
{
	size_t live = 0;
	for (const DescriptorHeapRecord &heap : gDescriptorHeaps) {
		if (DescriptorHeapIsShaderVisible(heap))
			live += heap.liveSlots;
	}
	return live;
}

static size_t DescriptorHeapSlotCapacityLocked()
{
	size_t capacity = 0;
	for (const DescriptorHeapRecord &heap : gDescriptorHeaps)
		capacity += heap.slots.size();
	return capacity;
}

static size_t DescriptorHeapHistoryEntriesLocked()
{
	size_t entries = 0;
	for (const DescriptorHeapRecord &heap : gDescriptorHeaps) {
		for (const std::vector<DescriptorRecord> &history : heap.slotHistory)
			entries += history.size();
	}
	return entries;
}

static void ClearCpuDescriptorLedgerLocked()
{
	for (auto &entry : gDescriptorByCpuHandle)
		ReleaseDescriptorRecordResources(&entry.second);
	gDescriptorByCpuHandle.clear();
}

void StoreCpuDescriptorRecordLocked(const DescriptorRecord &record)
{
	if (!record.cpuHandle)
		return;
	auto found = gDescriptorByCpuHandle.find(record.cpuHandle);
	if (found != gDescriptorByCpuHandle.end())
		ReleaseDescriptorRecordResources(&found->second);
	gDescriptorByCpuHandle[record.cpuHandle] = CloneDescriptorRecord(record);
}

void InvalidateDescriptorHeapLocked(ID3D12DescriptorHeap *heap)
{
	if (!heap)
		return;
	auto found = gDescriptorHeapByPtr.find(heap);
	if (found == gDescriptorHeapByPtr.end() || found->second >= gDescriptorHeaps.size())
		return;
	DescriptorHeapRecord &record = gDescriptorHeaps[found->second];
	const SIZE_T cpuStart = record.cpuStart;
	ClearDescriptorHeapLocked(&record);
	record.heap = nullptr;
	record.desc = D3D12_DESCRIPTOR_HEAP_DESC();
	record.cpuStart = 0;
	record.gpuStart = 0;
	record.increment = 0;
	gDescriptorHeapByCpuStart.erase(cpuStart);
	gDescriptorHeapByPtr.erase(found);
}

void InvalidateResourceLocked(ID3D12Resource *resource)
{
	if (!resource)
		return;
	auto found = gResourceByPtr.find(resource);
	if (found == gResourceByPtr.end() || found->second >= gResources.size())
		return;

	const size_t index = found->second;
	ResourceRecord &record = gResources[index];
	if (!record.resource)
		return;
	if (record.gpuVirtualAddress)
		gBufferResourceByGpuVa.erase(record.gpuVirtualAddress);
	for (auto it = gResourceByPtr.begin(); it != gResourceByPtr.end();) {
		if (it->second == index)
			it = gResourceByPtr.erase(it);
		else
			++it;
	}
	record = ResourceRecord();
	gFreeResourceSlots.push_back(index);
	if (gLiveResourceCount > 0)
		gLiveResourceCount--;
	gBufferResolveCache.clear();
	gBufferResolveGeneration.fetch_add(1, std::memory_order_relaxed);
}

typedef HRESULT(WINAPI *PFN_D3D12_CREATE_ROOT_SIGNATURE_DESERIALIZER_LOCAL)(
	LPCVOID, SIZE_T, REFIID, void**);
typedef HRESULT(WINAPI *PFN_D3D12_CREATE_VERSIONED_ROOT_SIGNATURE_DESERIALIZER_LOCAL)(
	LPCVOID, SIZE_T, REFIID, void**);

static HMODULE gD3D12ForDeserialization = nullptr;
static PFN_D3D12_CREATE_ROOT_SIGNATURE_DESERIALIZER_LOCAL gCreateRootSignatureDeserializer = nullptr;
static PFN_D3D12_CREATE_VERSIONED_ROOT_SIGNATURE_DESERIALIZER_LOCAL gCreateVersionedRootSignatureDeserializer = nullptr;
UINT64 Fnv1a64(const void *data, size_t size)
{
	const uint8_t *bytes = static_cast<const uint8_t*>(data);
	UINT64 hash = 14695981039346656037ull;
	for (size_t i = 0; i < size; ++i) {
		hash ^= bytes[i];
		hash *= 1099511628211ull;
	}
	return hash;
}

static UINT64 HashDescriptorValue(const DescriptorRecord &record)
{
	UINT64 hash = 14695981039346656037ull;
	auto append = [&hash](const void *data, size_t size) {
		const uint8_t *bytes = static_cast<const uint8_t*>(data);
		for (size_t i = 0; i < size; ++i) {
			hash ^= bytes[i];
			hash *= 1099511628211ull;
		}
	};
	append(&record.kindId, sizeof(record.kindId));
	append(&record.resource, sizeof(record.resource));
	append(&record.counterResource, sizeof(record.counterResource));
	append(&record.gpuVirtualAddress, sizeof(record.gpuVirtualAddress));
	append(&record.resourceOffset, sizeof(record.resourceOffset));
	append(&record.viewSize, sizeof(record.viewSize));
	append(&record.hasDesc, sizeof(record.hasDesc));
	if (record.kindId == DescriptorKind::CBV_ID) {
		append(&record.cbv, sizeof(record.cbv));
	} else if (record.kindId == DescriptorKind::SRV_ID) {
		append(&record.srv, sizeof(record.srv));
	} else if (record.kindId == DescriptorKind::UAV_ID) {
		append(&record.uav, sizeof(record.uav));
	} else if (record.kindId == DescriptorKind::RTV_ID) {
		append(&record.rtv, sizeof(record.rtv));
	} else if (record.kindId == DescriptorKind::DSV_ID) {
		append(&record.dsv, sizeof(record.dsv));
	} else if (record.kindId == DescriptorKind::Sampler_ID) {
		append(&record.sampler, sizeof(record.sampler));
	}
	return hash ? hash : 1;
}

void ReleaseDescriptorRecordResources(DescriptorRecord *record)
{
	if (!record)
		return;
	if (record->ownsResource && record->resource)
		record->resource->Release();
	if (record->ownsCounterResource && record->counterResource)
		record->counterResource->Release();
	record->resource = nullptr;
	record->counterResource = nullptr;
	record->ownsResource = false;
	record->ownsCounterResource = false;
}

static void RetainDescriptorRecordResources(DescriptorRecord *record)
{
	if (!record)
		return;
	if (record->ownsResource && record->resource)
		record->resource->AddRef();
	if (record->ownsCounterResource && record->counterResource)
		record->counterResource->AddRef();
}

DescriptorRecord CloneDescriptorRecord(const DescriptorRecord &record)
{
	DescriptorRecord clone = record;
	RetainDescriptorRecordResources(&clone);
	return clone;
}

void PrepareDescriptorValueId(DescriptorRecord *record)
{
	if (!record)
		return;
	if (record->valueId == 0)
		record->valueId = HashDescriptorValue(*record);
}

D3D12_RESOURCE_DESC ResourceDescFromDesc1(const D3D12_RESOURCE_DESC1 *desc)
{
	D3D12_RESOURCE_DESC out = {};
	if (!desc)
		return out;
	out.Dimension = desc->Dimension;
	out.Alignment = desc->Alignment;
	out.Width = desc->Width;
	out.Height = desc->Height;
	out.DepthOrArraySize = desc->DepthOrArraySize;
	out.MipLevels = desc->MipLevels;
	out.Format = desc->Format;
	out.SampleDesc = desc->SampleDesc;
	out.Layout = desc->Layout;
	out.Flags = desc->Flags;
	return out;
}

void SetDescriptorKind(DescriptorRecord *record, UINT kindId)
{
	if (!record)
		return;
	record->kindId = kindId;
	switch (kindId) {
	case DescriptorKind::CBV_ID: record->kind = "CBV"; break;
	case DescriptorKind::SRV_ID: record->kind = "SRV"; break;
	case DescriptorKind::UAV_ID: record->kind = "UAV"; break;
	case DescriptorKind::RTV_ID: record->kind = "RTV"; break;
	case DescriptorKind::DSV_ID: record->kind = "DSV"; break;
	case DescriptorKind::Sampler_ID: record->kind = "Sampler"; break;
	default: record->kind.clear(); break;
	}
}

ID3D12Resource *CanonicalResource(ID3D12Resource *resource)
{
	if (!resource)
		return nullptr;

	IUnknown *unknown = nullptr;
	if (SUCCEEDED(resource->QueryInterface(IID_PPV_ARGS(&unknown))) && unknown) {
		ID3D12Resource *canonical = reinterpret_cast<ID3D12Resource*>(unknown);
		unknown->Release();
		return canonical;
	}
	return resource;
}

void RecordResource(
	ID3D12Resource *resource,
	const D3D12_RESOURCE_DESC *desc,
	const D3D12_HEAP_PROPERTIES *heapProperties,
	const PlacedResourceInfo *placedInfo,
	D3D12_RESOURCE_STATES initialState)
{
	if (!resource)
		return;

	ResourceRecord record;
	record.resource = resource;
	record.desc = desc ? *desc : resource->GetDesc();
	const bool bufferResource = record.desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER;
	const bool textureResource = IsTextureResourceDimension(record.desc.Dimension);
	if (!bufferResource && !textureResource)
		return;
	if (heapProperties) {
		record.heapType = heapProperties->Type;
		record.hasHeapType = true;
	}

	if (record.hasHeapType && record.heapType == D3D12_HEAP_TYPE_READBACK)
		return;

	record.currentState = initialState;
	record.hasCurrentState = true;
	if (bufferResource) {
		record.gpuVirtualAddress = resource->GetGPUVirtualAddress();
		record.size = record.desc.Width;
		record.gpuVirtualAddressRealEnd = record.gpuVirtualAddress + record.size;
		record.gpuVirtualAddressOobEnd = record.gpuVirtualAddressRealEnd;
		if (placedInfo && placedInfo->hasPlacedHeap) {
			record.hasPlacedHeapRange = true;
			record.placedHeapOffset = placedInfo->heapOffset;
			record.placedHeapSize = placedInfo->heapSize;
			if (placedInfo->heapSize > placedInfo->heapOffset)
				record.gpuVirtualAddressOobEnd =
					record.gpuVirtualAddress + (placedInfo->heapSize - placedInfo->heapOffset);
			if (record.gpuVirtualAddressOobEnd < record.gpuVirtualAddressRealEnd)
				record.gpuVirtualAddressOobEnd = record.gpuVirtualAddressRealEnd;
		}
		DX12LogManager::JsonFunc("DX12BufferAddressRangeAdded",
			"\"resource\":\"%p\",\"gpuVa\":\"0x%llX\",\"size\":%llu,\"realEnd\":\"0x%llX\",\"oobEnd\":\"0x%llX\",\"hasPlacedHeap\":%u,\"heapOffset\":%llu,\"heapSize\":%llu",
			resource,
			static_cast<unsigned long long>(record.gpuVirtualAddress),
			static_cast<unsigned long long>(record.size),
			static_cast<unsigned long long>(record.gpuVirtualAddressRealEnd),
			static_cast<unsigned long long>(record.gpuVirtualAddressOobEnd),
			record.hasPlacedHeapRange ? 1 : 0,
			static_cast<unsigned long long>(record.placedHeapOffset),
			static_cast<unsigned long long>(record.placedHeapSize));
	}

	ID3D12Resource *canonical = resource;
	AcquireSRWLockExclusive(&gResourceLock);
	auto found = gResourceByPtr.find(canonical);
	bool gpuVaIndexChanged = false;
	if (found != gResourceByPtr.end()) {
		ResourceRecord &existing = gResources[found->second];
		if (existing.gpuVirtualAddress && existing.gpuVirtualAddress != record.gpuVirtualAddress) {
			gBufferResourceByGpuVa.erase(existing.gpuVirtualAddress);
			gpuVaIndexChanged = true;
		}
		gResources[found->second] = record;
		gResourceByPtr[resource] = found->second;
		gResourceByPtr[canonical] = found->second;
		if (record.gpuVirtualAddress && record.size) {
			gBufferResourceByGpuVa[record.gpuVirtualAddress] = found->second;
			gpuVaIndexChanged = true;
		}
	} else {
		size_t index = 0;
		if (!gFreeResourceSlots.empty()) {
			index = gFreeResourceSlots.back();
			gFreeResourceSlots.pop_back();
			gResources[index] = record;
		} else {
			index = gResources.size();
			gResources.push_back(record);
		}
		gLiveResourceCount++;
		gResourceByPtr[resource] = index;
		gResourceByPtr[canonical] = index;
		if (record.gpuVirtualAddress && record.size) {
			gBufferResourceByGpuVa[record.gpuVirtualAddress] = index;
			gpuVaIndexChanged = true;
		}
	}
	if (gpuVaIndexChanged) {
		// Descriptor creation resolves CBV/SRV/UAV GPU VAs constantly; keep
		// its cache warm unless the buffer VA index actually changed.
		gBufferResolveCache.clear();
		gBufferResolveGeneration.fetch_add(1, std::memory_order_relaxed);
	}
	gResourcesRecordedFromCreate++;
	LogResourceTrackerStatsLocked();
	ReleaseSRWLockExclusive(&gResourceLock);
}

void CleanupTrackedResources()
{
	DX12ResourceCollectQueue::Stop();
	AcquireSRWLockExclusive(&gResourceLock);
	AcquireSRWLockExclusive(&gDescriptorLock);
	for (DescriptorHeapRecord &heap : gDescriptorHeaps)
		ClearDescriptorHeapLocked(&heap);
	ClearCpuDescriptorLedgerLocked();
	gRootSignatures.clear();
	gDescriptorHeaps.clear();
	gRootSignatureByPtr.clear();
	gDescriptorHeapByPtr.clear();
	gDescriptorHeapByCpuStart.clear();
	gPsoRoots.clear();
	gPsoRootByIndex.clear();
	gResources.clear();
	gFreeResourceSlots.clear();
	gLiveResourceCount = 0;
	gResourceByPtr.clear();
	gBufferResourceByGpuVa.clear();
	gBufferResolveCache.clear();
	gBufferResolveGeneration.fetch_add(1, std::memory_order_relaxed);
	ReleaseSRWLockExclusive(&gDescriptorLock);
	ReleaseSRWLockExclusive(&gResourceLock);
}

static bool EnsureRootSignatureDeserializerFunctions()
{
	if (gCreateRootSignatureDeserializer || gCreateVersionedRootSignatureDeserializer)
		return true;

	if (!gD3D12ForDeserialization) {
		wchar_t path[MAX_PATH];
		if (GetSystemDirectoryW(path, MAX_PATH)) {
			PathAppendW(path, L"d3d12.dll");
			gD3D12ForDeserialization = LoadLibraryW(path);
		}
		if (!gD3D12ForDeserialization)
			gD3D12ForDeserialization = LoadLibraryW(L"d3d12.dll");
	}

	if (!gD3D12ForDeserialization)
		return false;

	gCreateRootSignatureDeserializer =
		reinterpret_cast<PFN_D3D12_CREATE_ROOT_SIGNATURE_DESERIALIZER_LOCAL>(
			GetProcAddress(gD3D12ForDeserialization, "D3D12CreateRootSignatureDeserializer"));
	gCreateVersionedRootSignatureDeserializer =
		reinterpret_cast<PFN_D3D12_CREATE_VERSIONED_ROOT_SIGNATURE_DESERIALIZER_LOCAL>(
			GetProcAddress(gD3D12ForDeserialization, "D3D12CreateVersionedRootSignatureDeserializer"));
	return gCreateRootSignatureDeserializer || gCreateVersionedRootSignatureDeserializer;
}

static UINT RootSignatureVersionNumber(D3D_ROOT_SIGNATURE_VERSION version)
{
	switch (version) {
	case D3D_ROOT_SIGNATURE_VERSION_1_0:
		return 0x10;
	case D3D_ROOT_SIGNATURE_VERSION_1_1:
		return 0x11;
	default:
		return static_cast<UINT>(version);
	}
}

static void RecordDescriptorRange(
	DX12RootParameterSummary *parameter, UINT rangeIndex,
	const D3D12_DESCRIPTOR_RANGE &range, UINT *appendOffset)
{
	if (!parameter || !appendOffset)
		return;

	DX12RootDescriptorRangeSummary summary;
	summary.rootParameterIndex = parameter->rootParameterIndex;
	summary.rangeIndex = rangeIndex;
	summary.rangeType = static_cast<UINT>(range.RangeType);
	summary.numDescriptors = range.NumDescriptors;
	summary.baseShaderRegister = range.BaseShaderRegister;
	summary.registerSpace = range.RegisterSpace;
	summary.offsetInDescriptorsFromTableStart = range.OffsetInDescriptorsFromTableStart;
	summary.shaderVisibility = parameter->shaderVisibility;
	if (range.OffsetInDescriptorsFromTableStart == D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND)
		summary.effectiveOffset = *appendOffset;
	else
		summary.effectiveOffset = range.OffsetInDescriptorsFromTableStart;

	if (range.NumDescriptors != UINT_MAX) {
		const UINT64 end = static_cast<UINT64>(summary.effectiveOffset) + range.NumDescriptors;
		*appendOffset = end > UINT_MAX ? UINT_MAX : static_cast<UINT>(end);
	}

	parameter->ranges.push_back(summary);
}

static void RecordDescriptorRange1(
	DX12RootParameterSummary *parameter, UINT rangeIndex,
	const D3D12_DESCRIPTOR_RANGE1 &range, UINT *appendOffset)
{
	if (!parameter || !appendOffset)
		return;

	DX12RootDescriptorRangeSummary summary;
	summary.rootParameterIndex = parameter->rootParameterIndex;
	summary.rangeIndex = rangeIndex;
	summary.rangeType = static_cast<UINT>(range.RangeType);
	summary.numDescriptors = range.NumDescriptors;
	summary.baseShaderRegister = range.BaseShaderRegister;
	summary.registerSpace = range.RegisterSpace;
	summary.offsetInDescriptorsFromTableStart = range.OffsetInDescriptorsFromTableStart;
	summary.flags = static_cast<UINT>(range.Flags);
	summary.shaderVisibility = parameter->shaderVisibility;
	if (range.OffsetInDescriptorsFromTableStart == D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND)
		summary.effectiveOffset = *appendOffset;
	else
		summary.effectiveOffset = range.OffsetInDescriptorsFromTableStart;

	if (range.NumDescriptors != UINT_MAX) {
		const UINT64 end = static_cast<UINT64>(summary.effectiveOffset) + range.NumDescriptors;
		*appendOffset = end > UINT_MAX ? UINT_MAX : static_cast<UINT>(end);
	}

	parameter->ranges.push_back(summary);
}

static void ParseRootParameter(
	RootSignatureRecord *record, UINT index, const D3D12_ROOT_PARAMETER &source)
{
	if (!record)
		return;

	DX12RootParameterSummary parameter;
	parameter.rootParameterIndex = index;
	parameter.parameterType = static_cast<UINT>(source.ParameterType);
	parameter.shaderVisibility = static_cast<UINT>(source.ShaderVisibility);
	switch (source.ParameterType) {
	case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE: {
		UINT appendOffset = 0;
		for (UINT i = 0; i < source.DescriptorTable.NumDescriptorRanges; ++i)
			RecordDescriptorRange(&parameter, i, source.DescriptorTable.pDescriptorRanges[i], &appendOffset);
		break;
	}
	case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
		parameter.shaderRegister = source.Constants.ShaderRegister;
		parameter.registerSpace = source.Constants.RegisterSpace;
		parameter.num32BitValues = source.Constants.Num32BitValues;
		break;
	case D3D12_ROOT_PARAMETER_TYPE_CBV:
	case D3D12_ROOT_PARAMETER_TYPE_SRV:
	case D3D12_ROOT_PARAMETER_TYPE_UAV:
		parameter.shaderRegister = source.Descriptor.ShaderRegister;
		parameter.registerSpace = source.Descriptor.RegisterSpace;
		break;
	default:
		break;
	}
	record->parameters.push_back(std::move(parameter));
}

static void ParseRootParameter1(
	RootSignatureRecord *record, UINT index, const D3D12_ROOT_PARAMETER1 &source)
{
	if (!record)
		return;

	DX12RootParameterSummary parameter;
	parameter.rootParameterIndex = index;
	parameter.parameterType = static_cast<UINT>(source.ParameterType);
	parameter.shaderVisibility = static_cast<UINT>(source.ShaderVisibility);
	switch (source.ParameterType) {
	case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE: {
		UINT appendOffset = 0;
		for (UINT i = 0; i < source.DescriptorTable.NumDescriptorRanges; ++i)
			RecordDescriptorRange1(&parameter, i, source.DescriptorTable.pDescriptorRanges[i], &appendOffset);
		break;
	}
	case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
		parameter.shaderRegister = source.Constants.ShaderRegister;
		parameter.registerSpace = source.Constants.RegisterSpace;
		parameter.num32BitValues = source.Constants.Num32BitValues;
		break;
	case D3D12_ROOT_PARAMETER_TYPE_CBV:
	case D3D12_ROOT_PARAMETER_TYPE_SRV:
	case D3D12_ROOT_PARAMETER_TYPE_UAV:
		parameter.shaderRegister = source.Descriptor.ShaderRegister;
		parameter.registerSpace = source.Descriptor.RegisterSpace;
		parameter.rootDescriptorFlags = static_cast<UINT>(source.Descriptor.Flags);
		break;
	default:
		break;
	}
	record->parameters.push_back(std::move(parameter));
}

void ParseRootSignatureBlob(RootSignatureRecord *record, const void *blob, SIZE_T blobLength)
{
	if (!record || !blob || blobLength == 0)
		return;
	if (!EnsureRootSignatureDeserializerFunctions())
		return;

	ID3D12VersionedRootSignatureDeserializer *versioned = nullptr;
	HRESULT hr = gCreateVersionedRootSignatureDeserializer ?
		gCreateVersionedRootSignatureDeserializer(blob, blobLength, IID_PPV_ARGS(&versioned)) :
		E_NOINTERFACE;
	if (SUCCEEDED(hr) && versioned) {
		const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *desc = nullptr;
		hr = versioned->GetRootSignatureDescAtVersion(D3D_ROOT_SIGNATURE_VERSION_1_1, &desc);
		if (FAILED(hr))
			hr = versioned->GetRootSignatureDescAtVersion(D3D_ROOT_SIGNATURE_VERSION_1_0, &desc);
		if (SUCCEEDED(hr) && desc) {
			record->version = RootSignatureVersionNumber(desc->Version);
			if (desc->Version == D3D_ROOT_SIGNATURE_VERSION_1_1) {
				record->flags = static_cast<UINT>(desc->Desc_1_1.Flags);
				record->staticSamplerCount = desc->Desc_1_1.NumStaticSamplers;
				record->parameters.reserve(desc->Desc_1_1.NumParameters);
				for (UINT i = 0; i < desc->Desc_1_1.NumParameters; ++i)
					ParseRootParameter1(record, i, desc->Desc_1_1.pParameters[i]);
			} else {
				record->flags = static_cast<UINT>(desc->Desc_1_0.Flags);
				record->staticSamplerCount = desc->Desc_1_0.NumStaticSamplers;
				record->parameters.reserve(desc->Desc_1_0.NumParameters);
				for (UINT i = 0; i < desc->Desc_1_0.NumParameters; ++i)
					ParseRootParameter(record, i, desc->Desc_1_0.pParameters[i]);
			}
			record->parsed = true;
		}
		versioned->Release();
		return;
	}

	ID3D12RootSignatureDeserializer *legacy = nullptr;
	hr = gCreateRootSignatureDeserializer ?
		gCreateRootSignatureDeserializer(blob, blobLength, IID_PPV_ARGS(&legacy)) :
		E_NOINTERFACE;
	if (FAILED(hr) || !legacy)
		return;

	const D3D12_ROOT_SIGNATURE_DESC *desc = legacy->GetRootSignatureDesc();
	if (desc) {
		record->version = RootSignatureVersionNumber(D3D_ROOT_SIGNATURE_VERSION_1_0);
		record->flags = static_cast<UINT>(desc->Flags);
		record->staticSamplerCount = desc->NumStaticSamplers;
		record->parameters.reserve(desc->NumParameters);
		for (UINT i = 0; i < desc->NumParameters; ++i)
			ParseRootParameter(record, i, desc->pParameters[i]);
		record->parsed = true;
	}
	legacy->Release();
}

void FillResourceInfo(DescriptorRecord *record, ID3D12Resource *resource)
{
	if (!record || !resource)
		return;

	record->resource = resource;
	AcquireSRWLockShared(&gResourceLock);
	auto found = gResourceByPtr.find(resource);
	if (found != gResourceByPtr.end() && found->second < gResources.size()) {
		const ResourceRecord &resourceRecord = gResources[found->second];
		record->resourceDesc = resourceRecord.desc;
		record->hasResourceDesc = true;
		if (resourceRecord.desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
			record->gpuVirtualAddress = resourceRecord.gpuVirtualAddress;
		if (resourceRecord.hasHeapType) {
			record->resourceHeapType = static_cast<UINT>(resourceRecord.heapType);
			record->hasResourceHeapType = true;
		}
		record->currentState = resourceRecord.currentState;
		record->hasCurrentState = resourceRecord.hasCurrentState;
		record->stateTrackedByBarrier = resourceRecord.stateTrackedByBarrier;
	}
	ReleaseSRWLockShared(&gResourceLock);
}

void FillResourcePointerInfo(DescriptorRecord *record, ID3D12Resource *resource)
{
	if (!record || !resource)
		return;

	record->resource = resource;
	if (ShouldTrackResourceMetadata()) {
		FillResourceInfo(record, resource);
	} else {
		const D3D12_RESOURCE_DESC desc = resource->GetDesc();
		if (desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
			record->resourceDesc = desc;
			record->hasResourceDesc = true;
			record->gpuVirtualAddress = resource->GetGPUVirtualAddress();
		}
	}
}

bool ResolveBufferByGpuVa(UINT64 gpuVa, UINT64 size, DescriptorRecord *record)
{
	if (!record || gpuVa == 0)
		return false;

	bool resolved = false;
	const UINT64 requestedEnd = gpuVa + size;
	if (requestedEnd < gpuVa)
		return false;

	AcquireSRWLockShared(&gResourceLock);
	auto foundBuffer = gBufferResourceByGpuVa.upper_bound(gpuVa);
	if (foundBuffer != gBufferResourceByGpuVa.begin()) {
		--foundBuffer;
		if (foundBuffer->second < gResources.size()) {
			const ResourceRecord &resource = gResources[foundBuffer->second];
			const UINT64 begin = resource.gpuVirtualAddress;
			const UINT64 realEnd = resource.gpuVirtualAddressRealEnd ?
				resource.gpuVirtualAddressRealEnd : begin + resource.size;
			const UINT64 oobEnd = resource.gpuVirtualAddressOobEnd ?
				resource.gpuVirtualAddressOobEnd : realEnd;
			if (resource.desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER ||
				begin == 0 || resource.size == 0 ||
				gpuVa < begin || gpuVa >= realEnd) {
				ReleaseSRWLockShared(&gResourceLock);
				return false;
			}
			record->resource = resource.resource;
			record->resourceOffset = gpuVa - begin;
			const UINT64 available = realEnd - gpuVa;
			record->viewSize = (available && size > available) ? available : size;
			record->gpuVirtualAddress = gpuVa;
			if (ShouldTrackResourceMetadata()) {
				record->resourceDesc = resource.desc;
				record->hasResourceDesc = true;
			}
			if (resource.hasHeapType) {
				record->resourceHeapType = static_cast<UINT>(resource.heapType);
				record->hasResourceHeapType = true;
			}
			record->currentState = resource.currentState;
			record->hasCurrentState = resource.hasCurrentState;
			record->stateTrackedByBarrier = resource.stateTrackedByBarrier;
			resolved = true;
		}
	}
	ReleaseSRWLockShared(&gResourceLock);
	return resolved;
}

static UINT DxgiFormatBytesPerElement(DXGI_FORMAT format)
{
	switch (format) {
	case DXGI_FORMAT_R32G32B32A32_TYPELESS:
	case DXGI_FORMAT_R32G32B32A32_FLOAT:
	case DXGI_FORMAT_R32G32B32A32_UINT:
	case DXGI_FORMAT_R32G32B32A32_SINT:
		return 16;
	case DXGI_FORMAT_R32G32B32_TYPELESS:
	case DXGI_FORMAT_R32G32B32_FLOAT:
	case DXGI_FORMAT_R32G32B32_UINT:
	case DXGI_FORMAT_R32G32B32_SINT:
		return 12;
	case DXGI_FORMAT_R16G16B16A16_TYPELESS:
	case DXGI_FORMAT_R16G16B16A16_FLOAT:
	case DXGI_FORMAT_R16G16B16A16_UNORM:
	case DXGI_FORMAT_R16G16B16A16_UINT:
	case DXGI_FORMAT_R16G16B16A16_SNORM:
	case DXGI_FORMAT_R16G16B16A16_SINT:
	case DXGI_FORMAT_R32G32_TYPELESS:
	case DXGI_FORMAT_R32G32_FLOAT:
	case DXGI_FORMAT_R32G32_UINT:
	case DXGI_FORMAT_R32G32_SINT:
	case DXGI_FORMAT_R32G8X24_TYPELESS:
	case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
	case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
	case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
		return 8;
	case DXGI_FORMAT_R10G10B10A2_TYPELESS:
	case DXGI_FORMAT_R10G10B10A2_UNORM:
	case DXGI_FORMAT_R10G10B10A2_UINT:
	case DXGI_FORMAT_R11G11B10_FLOAT:
	case DXGI_FORMAT_R8G8B8A8_TYPELESS:
	case DXGI_FORMAT_R8G8B8A8_UNORM:
	case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
	case DXGI_FORMAT_R8G8B8A8_UINT:
	case DXGI_FORMAT_R8G8B8A8_SNORM:
	case DXGI_FORMAT_R8G8B8A8_SINT:
	case DXGI_FORMAT_R16G16_TYPELESS:
	case DXGI_FORMAT_R16G16_FLOAT:
	case DXGI_FORMAT_R16G16_UNORM:
	case DXGI_FORMAT_R16G16_UINT:
	case DXGI_FORMAT_R16G16_SNORM:
	case DXGI_FORMAT_R16G16_SINT:
	case DXGI_FORMAT_R32_TYPELESS:
	case DXGI_FORMAT_D32_FLOAT:
	case DXGI_FORMAT_R32_FLOAT:
	case DXGI_FORMAT_R32_UINT:
	case DXGI_FORMAT_R32_SINT:
	case DXGI_FORMAT_R24G8_TYPELESS:
	case DXGI_FORMAT_D24_UNORM_S8_UINT:
	case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
	case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
	case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
	case DXGI_FORMAT_R8G8_B8G8_UNORM:
	case DXGI_FORMAT_G8R8_G8B8_UNORM:
	case DXGI_FORMAT_B8G8R8A8_UNORM:
	case DXGI_FORMAT_B8G8R8X8_UNORM:
	case DXGI_FORMAT_B8G8R8A8_TYPELESS:
	case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
	case DXGI_FORMAT_B8G8R8X8_TYPELESS:
	case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
		return 4;
	case DXGI_FORMAT_R8G8_TYPELESS:
	case DXGI_FORMAT_R8G8_UNORM:
	case DXGI_FORMAT_R8G8_UINT:
	case DXGI_FORMAT_R8G8_SNORM:
	case DXGI_FORMAT_R8G8_SINT:
	case DXGI_FORMAT_R16_TYPELESS:
	case DXGI_FORMAT_R16_FLOAT:
	case DXGI_FORMAT_D16_UNORM:
	case DXGI_FORMAT_R16_UNORM:
	case DXGI_FORMAT_R16_UINT:
	case DXGI_FORMAT_R16_SNORM:
	case DXGI_FORMAT_R16_SINT:
	case DXGI_FORMAT_B5G6R5_UNORM:
	case DXGI_FORMAT_B5G5R5A1_UNORM:
	case DXGI_FORMAT_B4G4R4A4_UNORM:
		return 2;
	case DXGI_FORMAT_R8_TYPELESS:
	case DXGI_FORMAT_R8_UNORM:
	case DXGI_FORMAT_R8_UINT:
	case DXGI_FORMAT_R8_SNORM:
	case DXGI_FORMAT_R8_SINT:
	case DXGI_FORMAT_A8_UNORM:
		return 1;
	default:
		return 0;
	}
}

static UINT SrvBufferBytesPerElement(const D3D12_SHADER_RESOURCE_VIEW_DESC &desc)
{
	if (desc.Buffer.StructureByteStride)
		return desc.Buffer.StructureByteStride;
	if (desc.Buffer.Flags & D3D12_BUFFER_SRV_FLAG_RAW)
		return 4;
	return DxgiFormatBytesPerElement(desc.Format);
}

static UINT UavBufferBytesPerElement(const D3D12_UNORDERED_ACCESS_VIEW_DESC &desc)
{
	if (desc.Buffer.StructureByteStride)
		return desc.Buffer.StructureByteStride;
	if (desc.Buffer.Flags & D3D12_BUFFER_UAV_FLAG_RAW)
		return 4;
	return DxgiFormatBytesPerElement(desc.Format);
}

void FillSrvBufferView(DescriptorRecord *record)
{
	if (!record || !record->hasDesc ||
		record->srv.ViewDimension != D3D12_SRV_DIMENSION_BUFFER)
		return;

	const D3D12_BUFFER_SRV &buffer = record->srv.Buffer;
	const UINT bytesPerElement = SrvBufferBytesPerElement(record->srv);
	record->resourceOffset = buffer.FirstElement * bytesPerElement;
	record->viewSize = static_cast<UINT64>(buffer.NumElements) * bytesPerElement;
	if (record->viewSize == 0 && record->resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
		record->viewSize = record->resourceDesc.Width > record->resourceOffset ?
			record->resourceDesc.Width - record->resourceOffset : 0;
}

void FillUavBufferView(DescriptorRecord *record)
{
	if (!record || !record->hasDesc ||
		record->uav.ViewDimension != D3D12_UAV_DIMENSION_BUFFER)
		return;

	const D3D12_BUFFER_UAV &buffer = record->uav.Buffer;
	const UINT bytesPerElement = UavBufferBytesPerElement(record->uav);
	record->resourceOffset = buffer.FirstElement * bytesPerElement;
	record->viewSize = static_cast<UINT64>(buffer.NumElements) * bytesPerElement;
	if (record->viewSize == 0 && record->resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
		record->viewSize = record->resourceDesc.Width > record->resourceOffset ?
			record->resourceDesc.Width - record->resourceOffset : 0;
}

void LogResourceTrackerStatsLocked()
{
	const LONG present = DX12RuntimeState::GetPresentCount();
	const LONG last = InterlockedCompareExchange(&gLastResourceStatsPresent, 0, 0);
	if (present < last + kResourceStatsLogInterval)
		return;
	if (InterlockedCompareExchange(&gLastResourceStatsPresent, present, last) != last)
		return;
	DX12LogManager::DebugJsonFunc("DX12ResourceTrackerStats",
		"\"present\":%ld,\"roots\":%zu,\"rootRecordsSeen\":%llu,"
		"\"heaps\":%zu,\"heapRecordsSeen\":%llu,"
		"\"descriptorSlots\":%zu,\"shaderVisibleDescriptorSlots\":%zu,\"descriptorSlotCapacity\":%zu,\"descriptorHistoryEntries\":%zu,"
		"\"descriptorHeapRanges\":%zu,"
		"\"descriptorRecordsSeen\":%llu,\"descriptorCopyRecordsSeen\":%llu,"
		"\"resources\":%zu,\"resourceSlots\":%zu,\"freeResourceSlots\":%zu,"
		"\"resourcesFromCreate\":%llu,\"cpuDescriptorLedger\":%zu,"
		"\"psoRoots\":%zu,\"psoRootRecordsSeen\":%llu",
		present,
		gRootSignatures.size(),
		static_cast<unsigned long long>(gRootSignatureRecordsSeen),
		gDescriptorHeaps.size(),
		static_cast<unsigned long long>(gDescriptorHeapRecordsSeen),
		LiveDescriptorHeapSlotsLocked(),
		LiveShaderVisibleDescriptorHeapSlotsLocked(),
		DescriptorHeapSlotCapacityLocked(),
		DescriptorHeapHistoryEntriesLocked(),
		gDescriptorHeapByCpuStart.size(),
		static_cast<unsigned long long>(gDescriptorRecordsSeen),
		static_cast<unsigned long long>(gDescriptorCopyRecordsSeen),
		gLiveResourceCount,
		gResources.size(),
		gFreeResourceSlots.size(),
		static_cast<unsigned long long>(gResourcesRecordedFromCreate),
		gDescriptorByCpuHandle.size(),
		gPsoRoots.size(),
		static_cast<unsigned long long>(gPsoRootRecordsSeen));
}

static bool DescriptorSlotFromHeapLocked(
	size_t heapIndex, SIZE_T cpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE *requiredType,
	DescriptorSlotLocation *location, UINT *slotsAvailable)
{
	if (heapIndex >= gDescriptorHeaps.size() || !cpuHandle || !location)
		return false;
	DescriptorHeapRecord &heap = gDescriptorHeaps[heapIndex];
	if ((requiredType && heap.desc.Type != *requiredType) ||
		heap.cpuStart == 0 || heap.increment == 0 || heap.desc.NumDescriptors == 0)
		return false;
	const SIZE_T begin = heap.cpuStart;
	const SIZE_T offset = cpuHandle >= begin ? cpuHandle - begin : 0;
	if (cpuHandle < begin || offset % heap.increment != 0)
		return false;
	const SIZE_T slot = offset / heap.increment;
	if (slot >= heap.desc.NumDescriptors || slot > UINT_MAX)
		return false;
	location->heapIndex = heapIndex;
	location->slot = static_cast<UINT>(slot);
	if (slotsAvailable)
		*slotsAvailable = heap.desc.NumDescriptors - location->slot;
	return true;
}

bool ResolveDescriptorSlotForCpuHandleLocked(
	SIZE_T cpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE *requiredType,
	DescriptorSlotLocation *location, UINT *slotsAvailable)
{
	if (!cpuHandle || !location)
		return false;
	struct DescriptorHeapLookupCache
	{
		size_t heapIndex = SIZE_MAX;
	};
	static thread_local DescriptorHeapLookupCache cache;
	if (DescriptorSlotFromHeapLocked(
			cache.heapIndex, cpuHandle, requiredType, location, slotsAvailable))
		return true;
	auto heapByStart = gDescriptorHeapByCpuStart.upper_bound(cpuHandle);
	if (heapByStart == gDescriptorHeapByCpuStart.begin())
		return false;
	--heapByStart;
	if (DescriptorSlotFromHeapLocked(
			heapByStart->second, cpuHandle, requiredType, location, slotsAvailable)) {
		cache.heapIndex = heapByStart->second;
		return true;
	}
	return false;
}

static bool DescriptorSlotForHandleLocked(SIZE_T cpuHandle, DescriptorSlotLocation *location)
{
	return ResolveDescriptorSlotForCpuHandleLocked(cpuHandle, nullptr, location, nullptr);
}

static bool DescriptorSlotForHandleLocked(
	SIZE_T cpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE type,
	DescriptorSlotLocation *location, UINT *slotsAvailable)
{
	return ResolveDescriptorSlotForCpuHandleLocked(cpuHandle, &type, location, slotsAvailable);
}

static bool FindLatestDescriptorAtSlotLocked(
	const DescriptorSlotLocation &location, DescriptorRecord *record)
{
	if (!record || location.heapIndex >= gDescriptorHeaps.size())
		return false;
	const DescriptorHeapRecord &heap = gDescriptorHeaps[location.heapIndex];
	const DescriptorRecord *slot = DescriptorHeapSlot(&heap, location.slot);
	if (!slot)
		return false;
	*record = CloneDescriptorRecord(*slot);
	return true;
}

static UINT64 DescriptorValueIdAtSlotLocked(const DescriptorSlotLocation &location)
{
	if (location.heapIndex >= gDescriptorHeaps.size())
		return 0;
	const DescriptorHeapRecord &heap = gDescriptorHeaps[location.heapIndex];
	const DescriptorRecord *slot = DescriptorHeapSlot(&heap, location.slot);
	return slot ? slot->valueId : 0;
}

bool DescriptorValueUnchanged(SIZE_T cpuHandle, UINT64 valueId)
{
	if (!cpuHandle || valueId == 0)
		return false;

	bool unchanged = false;
	AcquireSRWLockShared(&gDescriptorLock);
	DescriptorSlotLocation location = {};
	if (DescriptorSlotForHandleLocked(cpuHandle, &location)) {
		const DescriptorHeapRecord &heap = gDescriptorHeaps[location.heapIndex];
		const DescriptorRecord *slot = DescriptorHeapSlot(&heap, location.slot);
		unchanged = slot && slot->valueId == valueId;
	}
	if (!unchanged) {
		auto found = gDescriptorByCpuHandle.find(cpuHandle);
		unchanged = found != gDescriptorByCpuHandle.end() &&
			found->second.valueId == valueId;
	}
	ReleaseSRWLockShared(&gDescriptorLock);
	return unchanged;
}

static void RecordDescriptorAtSlotLocked(
	DescriptorRecord &&record, const DescriptorSlotLocation &location)
{
	if (record.valueId == 0)
		record.valueId = HashDescriptorValue(record);
	DescriptorHeapRecord &heap = gDescriptorHeaps[location.heapIndex];
	if (!EnsureDescriptorHeapStorageLocked(&heap)) {
		ReleaseDescriptorRecordResources(&record);
		return;
	}
	DescriptorRecord *existing = DescriptorHeapSlot(&heap, location.slot);
	if (existing) {
		if (existing->valueId == record.valueId &&
			existing->cpuHandle == record.cpuHandle) {
			ReleaseDescriptorRecordResources(&record);
			return;
		}
		ClearDescriptorHeapSlotLocked(&heap, location.slot);
	}
	if (location.slot >= heap.slots.size())
		return;
	++gDescriptorCopyRecordsSeen;
	StoreDescriptorAtHeapSlotLocked(&heap, location.slot, std::move(record));
}

static void RecordDescriptorInCacheLocked(DescriptorRecord &&record, bool copied)
{
	if (record.valueId == 0)
		record.valueId = HashDescriptorValue(record);
	// D3D12 descriptor creation writes a CPU descriptor first; the game may
	// later copy it into a shader-visible heap. Keep that CPU ledger so
	// CopyDescriptors can preserve RenderDoc-like slot granularity.
	StoreCpuDescriptorRecordLocked(record);

	DescriptorSlotLocation location = {};
	if (DescriptorSlotForHandleLocked(record.cpuHandle, &location)) {
		DescriptorHeapRecord &heap = gDescriptorHeaps[location.heapIndex];
		if (!copied && !ShouldStoreCreatedDescriptorInHeapSlot(heap)) {
			++gDescriptorRecordsSeen;
			ReleaseDescriptorRecordResources(&record);
			return;
		}
		DescriptorRecord *existing = DescriptorHeapSlot(&heap, location.slot);
		if (existing) {
			if (existing->valueId == record.valueId &&
				existing->cpuHandle == record.cpuHandle) {
				ReleaseDescriptorRecordResources(&record);
				return;
			}
			ClearDescriptorHeapSlotLocked(&heap, location.slot);
		}
		if (copied)
			++gDescriptorCopyRecordsSeen;
		else
			++gDescriptorRecordsSeen;
		if (location.slot >= heap.slots.size())
			return;
		StoreDescriptorAtHeapSlotLocked(&heap, location.slot, std::move(record));
		return;
	}

	ReleaseDescriptorRecordResources(&record);
}

void RecordDescriptor(DescriptorRecord &&record)
{
	AcquireSRWLockExclusive(&gDescriptorLock);
	RecordDescriptorInCacheLocked(std::move(record), false);
	LogResourceTrackerStatsLocked();
	ReleaseSRWLockExclusive(&gDescriptorLock);
}

void DX12PrimeDescriptorHistoryForHunting()
{
	if (!DX12RuntimeState::IsHuntingMode())
		return;

	AcquireSRWLockExclusive(&gDescriptorLock);
	size_t occupied = 0;
	size_t primed = 0;
	for (DescriptorHeapRecord &heap : gDescriptorHeaps) {
		const UINT slotCount = static_cast<UINT>(
			min(heap.slots.size(), heap.occupied.size()));
		for (UINT slot = 0; slot < slotCount; ++slot) {
			if (!DescriptorHeapSlotOccupied(heap, slot))
				continue;
			occupied++;
			if (slot < heap.slotHistory.size() && !heap.slotHistory[slot].empty())
				continue;
			const DescriptorRecord *record = DescriptorHeapSlot(&heap, slot);
			if (!record)
				continue;
			AppendDescriptorHistoryLocked(&heap, slot, *record);
			primed++;
		}
	}
	DX12LogManager::JsonFunc("DX12DescriptorHistoryPrime",
		"\"heaps\":%zu,\"occupied\":%zu,\"primed\":%zu,\"epoch\":%llu,\"historyEntries\":%zu",
		gDescriptorHeaps.size(),
		occupied,
		primed,
		static_cast<unsigned long long>(gDescriptorEpoch),
		DescriptorHeapHistoryEntriesLocked());
	LogResourceTrackerStatsLocked();
	ReleaseSRWLockExclusive(&gDescriptorLock);
}

UINT64 CurrentDescriptorEpoch()
{
	AcquireSRWLockShared(&gDescriptorLock);
	const UINT64 epoch = gDescriptorEpoch;
	ReleaseSRWLockShared(&gDescriptorLock);
	return epoch;
}

bool ShouldTrackFullDescriptorMetadata()
{
	return DX12ResourceTrackingPolicyUtils::NeedsFullDescriptorHistory();
}

bool ShouldTrackComputeDescriptorMetadata()
{
	return DX12ResourceTrackingPolicyUtils::NeedsDescriptorTracking();
}

bool ShouldTrackDescriptorCreationMetadata()
{
	return DX12ResourceTrackingPolicyUtils::NeedsDescriptorTracking();
}

bool ShouldTrackResourceMetadata()
{
	return DX12ResourceTrackingPolicyUtils::NeedsFullResourceMetadata();
}

bool ShouldTrackResourceCreationMetadata()
{
	return DX12ResourceTrackingPolicyUtils::NeedsAnyResourceCreationTracking();
}

static bool FindLatestDescriptorLocked(SIZE_T cpuHandle, DescriptorRecord *record)
{
	if (!record)
		return false;

	DescriptorSlotLocation heapLocation = {};
	if (DescriptorSlotForHandleLocked(cpuHandle, &heapLocation) &&
			FindLatestDescriptorAtSlotLocked(heapLocation, record))
		return true;

	auto found = gDescriptorByCpuHandle.find(cpuHandle);
	if (found != gDescriptorByCpuHandle.end()) {
		*record = CloneDescriptorRecord(found->second);
		return true;
	}

	return false;
}

static bool CopyDescriptorSlotToSlotLocked(
	const DescriptorSlotLocation &srcLocation,
	const DescriptorSlotLocation &destLocation, SIZE_T srcCpuHandle,
	SIZE_T destCpuHandle)
{
	if (srcLocation.heapIndex >= gDescriptorHeaps.size() ||
		destLocation.heapIndex >= gDescriptorHeaps.size())
		return false;
	const DescriptorHeapRecord &srcHeap = gDescriptorHeaps[srcLocation.heapIndex];
	const DescriptorRecord *source = DescriptorHeapSlot(&srcHeap, srcLocation.slot);
	DescriptorRecord ledgerSource;
	if (!source) {
		if (!FindLatestDescriptorLocked(srcCpuHandle, &ledgerSource))
			return false;
		source = &ledgerSource;
	}

	DescriptorHeapRecord &destHeap = gDescriptorHeaps[destLocation.heapIndex];
	if (!EnsureDescriptorHeapStorageLocked(&destHeap)) {
		ReleaseDescriptorRecordResources(&ledgerSource);
		return false;
	}
	DescriptorRecord *existing = DescriptorHeapSlot(&destHeap, destLocation.slot);
	if (existing && existing->valueId == source->valueId &&
		existing->cpuHandle == destCpuHandle) {
		ReleaseDescriptorRecordResources(&ledgerSource);
		++gDescriptorCopyRecordsSeen;
		return true;
	}

	DescriptorRecord copied = CloneDescriptorRecord(*source);
	copied.cpuHandle = destCpuHandle;
	if (destLocation.slot >= destHeap.slots.size()) {
		ReleaseDescriptorRecordResources(&copied);
		ReleaseDescriptorRecordResources(&ledgerSource);
		return false;
	}
	if (existing)
		ClearDescriptorHeapSlotLocked(&destHeap, destLocation.slot);
	StoreDescriptorAtHeapSlotLocked(&destHeap, destLocation.slot, std::move(copied));
	ReleaseDescriptorRecordResources(&ledgerSource);
	++gDescriptorCopyRecordsSeen;
	return true;
}

static void RecordDescriptorLocked(DescriptorRecord &&record)
{
	RecordDescriptorInCacheLocked(std::move(record), true);
}

void RecordDescriptorCopyRange(
	ID3D12Device *device, D3D12_DESCRIPTOR_HEAP_TYPE type,
	D3D12_CPU_DESCRIPTOR_HANDLE destStart, D3D12_CPU_DESCRIPTOR_HANDLE srcStart,
	UINT count)
{
	if (!device || count == 0)
		return;

	const UINT increment = device->GetDescriptorHandleIncrementSize(type);
	if (increment == 0)
		return;

	AcquireSRWLockExclusive(&gDescriptorLock);
	UINT processed = 0;
	while (processed < count) {
		const SIZE_T srcHandle =
			srcStart.ptr + static_cast<SIZE_T>(processed) * increment;
		const SIZE_T destHandle =
			destStart.ptr + static_cast<SIZE_T>(processed) * increment;
		DescriptorSlotLocation srcLocation = {};
		DescriptorSlotLocation destLocation = {};
		UINT srcSlotsAvailable = 0;
		UINT destSlotsAvailable = 0;
		const bool srcInHeap = DescriptorSlotForHandleLocked(
			srcHandle, type, &srcLocation, &srcSlotsAvailable);
		const bool destInHeap = DescriptorSlotForHandleLocked(
			destHandle, type, &destLocation, &destSlotsAvailable);
		const UINT remaining = count - processed;
		const UINT chunk = min(remaining, min(
			srcInHeap ? srcSlotsAvailable : 1,
			destInHeap ? destSlotsAvailable : 1));

		// D3D12 descriptor copies are CPU-immediate range operations. Preserve
		// that model in the tracker by locating each contiguous source/dest
		// heap span once instead of rescanning all heaps for every descriptor.
		if (srcInHeap && destInHeap) {
			for (UINT i = 0; i < chunk; ++i) {
				DescriptorSlotLocation currentSrc = srcLocation;
				currentSrc.slot += i;
				DescriptorSlotLocation currentDest = destLocation;
				currentDest.slot += i;
				const SIZE_T currentSrcHandle =
					srcHandle + static_cast<SIZE_T>(i) * increment;
				const SIZE_T currentDestHandle =
					destHandle + static_cast<SIZE_T>(i) * increment;
				CopyDescriptorSlotToSlotLocked(
					currentSrc, currentDest, currentSrcHandle, currentDestHandle);
			}
		} else if (destInHeap) {
			for (UINT i = 0; i < chunk; ++i) {
				DescriptorRecord copied;
				bool found = false;
				const SIZE_T currentSrcHandle =
					srcHandle + static_cast<SIZE_T>(i) * increment;
				found = FindLatestDescriptorLocked(currentSrcHandle, &copied);
				if (!found)
					continue;
				DescriptorSlotLocation currentDest = destLocation;
				currentDest.slot += i;
				copied.cpuHandle = destHandle + static_cast<SIZE_T>(i) * increment;
				RecordDescriptorAtSlotLocked(std::move(copied), currentDest);
			}
		} else {
			for (UINT i = 0; i < chunk; ++i) {
				const SIZE_T currentSrcHandle =
					srcHandle + static_cast<SIZE_T>(i) * increment;
				const SIZE_T currentDestHandle =
					destHandle + static_cast<SIZE_T>(i) * increment;
				DescriptorRecord copied;
				if (FindLatestDescriptorLocked(currentSrcHandle, &copied)) {
					copied.cpuHandle = currentDestHandle;
					RecordDescriptorLocked(std::move(copied));
				}
			}
		}
		processed += chunk;
	}
	LogResourceTrackerStatsLocked();
	ReleaseSRWLockExclusive(&gDescriptorLock);
}

void DX12RecordPsoRootSignature(
	UINT64 psoIndex, const char *kind, ID3D12PipelineState *pipelineState,
	ID3D12RootSignature *rootSignature)
{
	if (DX12ResourceCollectQueue::EnqueuePsoRootSignature(
			psoIndex, kind, pipelineState, rootSignature, "DX12RecordPsoRootSignature"))
		return;

	DX12LogManager::JsonFunc("DX12ResourceCollectQueue",
		"\"event\":\"PsoRootSignatureEnqueueFallback\",\"pso\":%llu,\"kind\":\"%s\",\"pipelineState\":\"%p\",\"rootSignature\":\"%p\",\"threadId\":%lu",
		static_cast<unsigned long long>(psoIndex),
		kind ? kind : "",
		pipelineState,
		rootSignature,
		GetCurrentThreadId());
	DX12RecordPsoRootSignatureImmediate(psoIndex, kind, pipelineState, rootSignature);
}

void DX12RecordPsoRootSignatureImmediate(
	UINT64 psoIndex, const char *kind, ID3D12PipelineState *pipelineState,
	ID3D12RootSignature *rootSignature)
{
	PsoRootRecord record;
	record.psoIndex = psoIndex;
	record.kind = kind ? kind : "";
	record.pipelineState = pipelineState;
	record.rootSignature = rootSignature;

	AcquireSRWLockExclusive(&gDescriptorLock);
	++gPsoRootRecordsSeen;
	auto found = gPsoRootByIndex.find(psoIndex);
	if (found != gPsoRootByIndex.end()) {
		gPsoRoots[found->second] = std::move(record);
	} else {
		gPsoRootByIndex[psoIndex] = gPsoRoots.size();
		gPsoRoots.push_back(std::move(record));
	}
	LogResourceTrackerStatsLocked();
	ReleaseSRWLockExclusive(&gDescriptorLock);
}
void DX12RecordResourceBarrier(UINT numBarriers, const D3D12_RESOURCE_BARRIER *barriers)
{
	if (!barriers || numBarriers == 0)
		return;

	AcquireSRWLockExclusive(&gResourceLock);
	bool resourceStateChanged = false;
	for (UINT i = 0; i < numBarriers; ++i) {
		const D3D12_RESOURCE_BARRIER &barrier = barriers[i];
		if (barrier.Type != D3D12_RESOURCE_BARRIER_TYPE_TRANSITION ||
			!barrier.Transition.pResource)
			continue;

		auto found = gResourceByPtr.find(barrier.Transition.pResource);
		if (found == gResourceByPtr.end() || found->second >= gResources.size())
			continue;

		ResourceRecord &record = gResources[found->second];
		if (barrier.Transition.Subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES ||
			record.desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
			record.currentState = barrier.Transition.StateAfter;
			record.hasCurrentState = true;
			record.stateTrackedByBarrier = true;
			resourceStateChanged = true;
		} else {
			// DX12 state is resource/subresource state, not descriptor state.
			// Keep the hot path O(barriers); dump/query code joins descriptors
			// with ResourceRecord when it needs a current state snapshot.
			record.hasCurrentState = false;
			record.stateTrackedByBarrier = true;
			resourceStateChanged = true;
		}
	}
	if (resourceStateChanged) {
		gBufferResolveCache.clear();
		gBufferResolveGeneration.fetch_add(1, std::memory_order_relaxed);
	}
	ReleaseSRWLockExclusive(&gResourceLock);
}

