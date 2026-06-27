#include "DX12ResourceTrackerPrivate.h"

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
std::vector<DescriptorRecord> gDescriptors;
std::unordered_map<ID3D12RootSignature*, size_t> gRootSignatureByPtr;
std::unordered_map<ID3D12DescriptorHeap*, size_t> gDescriptorHeapByPtr;
std::unordered_map<SIZE_T, DescriptorSlotLocation> gDescriptorByCpuHandle;
std::vector<PsoRootRecord> gPsoRoots;
std::unordered_map<UINT64, size_t> gPsoRootByIndex;
std::vector<ResourceRecord> gResources;
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
LONG gLastResourceStatsPresent = -1;
static constexpr LONG kResourceStatsLogInterval = 300;

void LogResourceTrackerStatsLocked();

void InvalidateDescriptorHeapLocked(ID3D12DescriptorHeap *heap)
{
	if (!heap)
		return;
	auto found = gDescriptorHeapByPtr.find(heap);
	if (found == gDescriptorHeapByPtr.end() || found->second >= gDescriptorHeaps.size())
		return;
	DescriptorHeapRecord &record = gDescriptorHeaps[found->second];
	for (const auto &slot : record.slots)
		gDescriptorByCpuHandle.erase(slot.second.cpuHandle);
	record.slots.clear();
	record.cpuStart = 0;
	record.gpuStart = 0;
	record.increment = 0;
	gDescriptorHeapByPtr.erase(found);
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

D3D12_RESOURCE_STATES ResourceStateFromLayout(D3D12_BARRIER_LAYOUT layout)
{
	switch (layout) {
	case D3D12_BARRIER_LAYOUT_COMMON:
		return D3D12_RESOURCE_STATE_COMMON;
	case D3D12_BARRIER_LAYOUT_GENERIC_READ:
		return D3D12_RESOURCE_STATE_GENERIC_READ;
	case D3D12_BARRIER_LAYOUT_RENDER_TARGET:
		return D3D12_RESOURCE_STATE_RENDER_TARGET;
	case D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS:
		return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	case D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE:
		return D3D12_RESOURCE_STATE_DEPTH_WRITE;
	case D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_READ:
		return D3D12_RESOURCE_STATE_DEPTH_READ;
	case D3D12_BARRIER_LAYOUT_SHADER_RESOURCE:
		return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	case D3D12_BARRIER_LAYOUT_COPY_SOURCE:
		return D3D12_RESOURCE_STATE_COPY_SOURCE;
	case D3D12_BARRIER_LAYOUT_COPY_DEST:
		return D3D12_RESOURCE_STATE_COPY_DEST;
	case D3D12_BARRIER_LAYOUT_RESOLVE_SOURCE:
		return D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
	case D3D12_BARRIER_LAYOUT_RESOLVE_DEST:
		return D3D12_RESOURCE_STATE_RESOLVE_DEST;
	default:
		return D3D12_RESOURCE_STATE_COMMON;
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
	ID3D12Resource *resource, const D3D12_RESOURCE_DESC *desc,
	const D3D12_HEAP_PROPERTIES *heapProperties,
	D3D12_RESOURCE_STATES initialState)
{
	if (!resource)
		return;

	ID3D12Resource *canonical = CanonicalResource(resource);
	ResourceRecord record;
	record.resource = resource;
	record.desc = desc ? *desc : resource->GetDesc();
	if (!ShouldTrackResourceMetadata() &&
	    record.desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER)
		return;
	if (heapProperties) {
		record.heapType = heapProperties->Type;
		record.hasHeapType = true;
	}

	if (record.hasHeapType && record.heapType == D3D12_HEAP_TYPE_READBACK)
		return;

	record.currentState = initialState;
	record.hasCurrentState = true;
	if (record.desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
		record.gpuVirtualAddress = resource->GetGPUVirtualAddress();
		record.size = record.desc.Width;
	}

	AcquireSRWLockExclusive(&gResourceLock);
	auto found = gResourceByPtr.find(canonical);
	if (found != gResourceByPtr.end()) {
		ResourceRecord &existing = gResources[found->second];
		if (existing.gpuVirtualAddress && existing.gpuVirtualAddress != record.gpuVirtualAddress)
			gBufferResourceByGpuVa.erase(existing.gpuVirtualAddress);
		gResources[found->second] = record;
		if (record.gpuVirtualAddress && record.size)
			gBufferResourceByGpuVa[record.gpuVirtualAddress] = found->second;
	} else {
		gResourceByPtr[canonical] = gResources.size();
		if (record.gpuVirtualAddress && record.size)
			gBufferResourceByGpuVa[record.gpuVirtualAddress] = gResources.size();
		gResources.push_back(record);
	}
	gBufferResolveCache.clear();
	gBufferResolveGeneration.fetch_add(1, std::memory_order_relaxed);
	gResourcesRecordedFromCreate++;
	LogResourceTrackerStatsLocked();
	ReleaseSRWLockExclusive(&gResourceLock);
}

void CleanupTrackedResources()
{
	AcquireSRWLockExclusive(&gResourceLock);
	AcquireSRWLockExclusive(&gDescriptorLock);
	gRootSignatures.clear();
	gDescriptorHeaps.clear();
	gDescriptors.clear();
	gRootSignatureByPtr.clear();
	gDescriptorHeapByPtr.clear();
	gDescriptorByCpuHandle.clear();
	gPsoRoots.clear();
	gPsoRootByIndex.clear();
	gResources.clear();
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
	record->resourceDesc = resource->GetDesc();
	record->hasResourceDesc = true;
	if (record->resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
		record->gpuVirtualAddress = resource->GetGPUVirtualAddress();

	ID3D12Resource *canonical = CanonicalResource(resource);
	AcquireSRWLockShared(&gResourceLock);
	auto found = gResourceByPtr.find(canonical);
	if (found != gResourceByPtr.end() && found->second < gResources.size()) {
		const ResourceRecord &resourceRecord = gResources[found->second];
		if (resourceRecord.hasHeapType) {
			record->resourceHeapType = static_cast<UINT>(resourceRecord.heapType);
			record->hasResourceHeapType = true;
		}
		record->currentState = resourceRecord.currentState;
		record->hasCurrentState = resourceRecord.hasCurrentState;
	}
	ReleaseSRWLockShared(&gResourceLock);
}

bool ResolveBufferByGpuVa(UINT64 gpuVa, UINT64 size, DescriptorRecord *record)
{
	if (!record || gpuVa == 0)
		return false;

	bool resolved = false;
	AcquireSRWLockShared(&gResourceLock);
	for (const ResourceRecord &resource : gResources) {
		if (resource.desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER ||
			resource.gpuVirtualAddress == 0 || resource.size == 0)
			continue;
		const UINT64 begin = resource.gpuVirtualAddress;
		const UINT64 end = begin + resource.size;
		const UINT64 requestedEnd = gpuVa + size;
		if (gpuVa >= begin && requestedEnd <= end) {
			record->resource = resource.resource;
			record->resourceDesc = resource.desc;
			record->hasResourceDesc = true;
			record->resourceOffset = gpuVa - begin;
			record->viewSize = size;
			if (resource.hasHeapType) {
				record->resourceHeapType = static_cast<UINT>(resource.heapType);
				record->hasResourceHeapType = true;
			}
			record->currentState = resource.currentState;
			record->hasCurrentState = resource.hasCurrentState;
			resolved = true;
			break;
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
	const LONG present = DX12GetPresentCount();
	const LONG last = InterlockedCompareExchange(&gLastResourceStatsPresent, 0, 0);
	if (present < last + kResourceStatsLogInterval)
		return;
	if (InterlockedCompareExchange(&gLastResourceStatsPresent, present, last) != last)
		return;
	DX12LogDebugJsonFunc("DX12ResourceTrackerStats",
		"\"present\":%ld,\"roots\":%zu,\"rootRecordsSeen\":%llu,"
		"\"heaps\":%zu,\"heapRecordsSeen\":%llu,"
		"\"descriptors\":%zu,\"descriptorSlots\":%zu,\"descriptorRecordsSeen\":%llu,\"descriptorCopyRecordsSeen\":%llu,"
		"\"resources\":%zu,\"resourcesFromCreate\":%llu,\"psoRoots\":%zu,\"psoRootRecordsSeen\":%llu",
		present,
		gRootSignatures.size(),
		static_cast<unsigned long long>(gRootSignatureRecordsSeen),
		gDescriptorHeaps.size(),
		static_cast<unsigned long long>(gDescriptorHeapRecordsSeen),
		gDescriptors.size(),
		gDescriptorByCpuHandle.size(),
		static_cast<unsigned long long>(gDescriptorRecordsSeen),
		static_cast<unsigned long long>(gDescriptorCopyRecordsSeen),
		gResources.size(),
		static_cast<unsigned long long>(gResourcesRecordedFromCreate),
		gPsoRoots.size(),
		static_cast<unsigned long long>(gPsoRootRecordsSeen));
}

static void UpdateDescriptorResourceStateLocked(
	ID3D12Resource *resource, D3D12_RESOURCE_STATES state, bool hasState)
{
	if (!resource)
		return;

	for (DescriptorRecord &descriptor : gDescriptors) {
		if (descriptor.resource == resource) {
			descriptor.currentState = state;
			descriptor.hasCurrentState = hasState;
		}
	}
	for (DescriptorHeapRecord &heap : gDescriptorHeaps) {
		for (auto &slot : heap.slots) {
			DescriptorRecord &descriptor = slot.second;
			if (descriptor.resource == resource) {
				descriptor.currentState = state;
				descriptor.hasCurrentState = hasState;
			}
		}
	}
}

static bool DescriptorSlotForHandleLocked(SIZE_T cpuHandle, DescriptorSlotLocation *location)
{
	if (!cpuHandle || !location)
		return false;
	for (size_t heapIndex = 0; heapIndex < gDescriptorHeaps.size(); ++heapIndex) {
		DescriptorHeapRecord &heap = gDescriptorHeaps[heapIndex];
		if (heap.cpuStart == 0 || heap.increment == 0 || heap.desc.NumDescriptors == 0)
			continue;
		const SIZE_T begin = heap.cpuStart;
		const SIZE_T offset = cpuHandle >= begin ? cpuHandle - begin : 0;
		if (cpuHandle < begin || offset % heap.increment != 0)
			continue;
		const SIZE_T slot = offset / heap.increment;
		if (slot >= heap.desc.NumDescriptors || slot > UINT_MAX)
			continue;
		location->heapIndex = heapIndex;
		location->slot = static_cast<UINT>(slot);
		return true;
	}
	return false;
}

static bool DescriptorSlotForHandleLocked(
	SIZE_T cpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE type,
	DescriptorSlotLocation *location, UINT *slotsAvailable)
{
	if (!cpuHandle || !location)
		return false;
	for (size_t heapIndex = 0; heapIndex < gDescriptorHeaps.size(); ++heapIndex) {
		DescriptorHeapRecord &heap = gDescriptorHeaps[heapIndex];
		if (heap.desc.Type != type || heap.cpuStart == 0 ||
			heap.increment == 0 || heap.desc.NumDescriptors == 0)
			continue;
		const SIZE_T begin = heap.cpuStart;
		const SIZE_T offset = cpuHandle >= begin ? cpuHandle - begin : 0;
		if (cpuHandle < begin || offset % heap.increment != 0)
			continue;
		const SIZE_T slot = offset / heap.increment;
		if (slot >= heap.desc.NumDescriptors || slot > UINT_MAX)
			continue;
		location->heapIndex = heapIndex;
		location->slot = static_cast<UINT>(slot);
		if (slotsAvailable)
			*slotsAvailable = heap.desc.NumDescriptors - location->slot;
		return true;
	}
	return false;
}

static bool FindLatestDescriptorAtSlotLocked(
	const DescriptorSlotLocation &location, DescriptorRecord *record)
{
	if (!record || location.heapIndex >= gDescriptorHeaps.size())
		return false;
	const DescriptorHeapRecord &heap = gDescriptorHeaps[location.heapIndex];
	auto slot = heap.slots.find(location.slot);
	if (slot == heap.slots.end())
		return false;
	*record = slot->second;
	return true;
}

static void RecordDescriptorAtSlotLocked(
	DescriptorRecord &&record, const DescriptorSlotLocation &location)
{
	++gDescriptorCopyRecordsSeen;
	DescriptorHeapRecord &heap = gDescriptorHeaps[location.heapIndex];
	auto existing = heap.slots.find(location.slot);
	if (existing != heap.slots.end())
		gDescriptorByCpuHandle.erase(existing->second.cpuHandle);
	auto inserted = heap.slots.insert_or_assign(location.slot, std::move(record));
	gDescriptorByCpuHandle[inserted.first->second.cpuHandle] = location;
}

static void RecordDescriptorInCacheLocked(DescriptorRecord &&record, bool copied)
{
	if (copied)
		++gDescriptorCopyRecordsSeen;
	else
		++gDescriptorRecordsSeen;

	DescriptorSlotLocation location = {};
	if (DescriptorSlotForHandleLocked(record.cpuHandle, &location)) {
		DescriptorHeapRecord &heap = gDescriptorHeaps[location.heapIndex];
		auto existing = heap.slots.find(location.slot);
		if (existing != heap.slots.end())
			gDescriptorByCpuHandle.erase(existing->second.cpuHandle);
		auto inserted = heap.slots.insert_or_assign(location.slot, std::move(record));
		gDescriptorByCpuHandle[inserted.first->second.cpuHandle] = location;
		return;
	}

	gDescriptors.push_back(std::move(record));
	DescriptorSlotLocation legacy = {};
	legacy.heapIndex = SIZE_MAX;
	legacy.slot = static_cast<UINT>(gDescriptors.size() - 1);
	gDescriptorByCpuHandle[gDescriptors.back().cpuHandle] = legacy;
}

void RecordDescriptor(DescriptorRecord &&record)
{
	AcquireSRWLockExclusive(&gDescriptorLock);
	RecordDescriptorInCacheLocked(std::move(record), false);
	LogResourceTrackerStatsLocked();
	ReleaseSRWLockExclusive(&gDescriptorLock);
}

bool ShouldTrackFullDescriptorMetadata()
{
	return InterlockedCompareExchange(&gDX12HotPathSkipBindings, 0, 0) == 0 ||
		DX12HuntNeedsHotPath();
}

bool ShouldTrackComputeDescriptorMetadata()
{
	return true;
}

bool ShouldTrackDescriptorCreationMetadata()
{
	return true;
}

bool ShouldTrackResourceMetadata()
{
	return InterlockedCompareExchange(&gDX12HotPathTrackResourceMetadata, 0, 0) != 0;
}

static bool FindLatestDescriptorLocked(SIZE_T cpuHandle, DescriptorRecord *record)
{
	if (!record)
		return false;

	auto found = gDescriptorByCpuHandle.find(cpuHandle);
	if (found == gDescriptorByCpuHandle.end())
		return false;

	const DescriptorSlotLocation &location = found->second;
	if (location.heapIndex == SIZE_MAX) {
		if (location.slot >= gDescriptors.size())
			return false;
		*record = gDescriptors[location.slot];
		return true;
	}
	if (location.heapIndex >= gDescriptorHeaps.size())
		return false;
	const DescriptorHeapRecord &heap = gDescriptorHeaps[location.heapIndex];
	auto slot = heap.slots.find(location.slot);
	if (slot == heap.slots.end())
		return false;
	*record = slot->second;
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
		if (destInHeap) {
			for (UINT i = 0; i < chunk; ++i) {
				DescriptorRecord copied;
				bool found = false;
				if (srcInHeap) {
					DescriptorSlotLocation currentSrc = srcLocation;
					currentSrc.slot += i;
					found = FindLatestDescriptorAtSlotLocked(currentSrc, &copied);
				} else {
					const SIZE_T currentSrcHandle =
						srcHandle + static_cast<SIZE_T>(i) * increment;
					found = FindLatestDescriptorLocked(currentSrcHandle, &copied);
				}
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
	AcquireSRWLockExclusive(&gDescriptorLock);
	bool resourceStateChanged = false;
	for (UINT i = 0; i < numBarriers; ++i) {
		const D3D12_RESOURCE_BARRIER &barrier = barriers[i];
		if (barrier.Type != D3D12_RESOURCE_BARRIER_TYPE_TRANSITION ||
			!barrier.Transition.pResource)
			continue;

		ID3D12Resource *canonical = CanonicalResource(barrier.Transition.pResource);
		auto found = gResourceByPtr.find(canonical);
		if (found == gResourceByPtr.end() || found->second >= gResources.size())
			continue;

		ResourceRecord &record = gResources[found->second];
		if (barrier.Transition.Subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES ||
			record.desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
			record.currentState = barrier.Transition.StateAfter;
			record.hasCurrentState = true;
			UpdateDescriptorResourceStateLocked(
				record.resource, record.currentState, record.hasCurrentState);
			resourceStateChanged = true;
		} else {
			record.hasCurrentState = false;
			UpdateDescriptorResourceStateLocked(
				record.resource, record.currentState, record.hasCurrentState);
			resourceStateChanged = true;
		}
	}
	if (resourceStateChanged) {
		gBufferResolveCache.clear();
		gBufferResolveGeneration.fetch_add(1, std::memory_order_relaxed);
	}
	ReleaseSRWLockExclusive(&gDescriptorLock);
	ReleaseSRWLockExclusive(&gResourceLock);
}
