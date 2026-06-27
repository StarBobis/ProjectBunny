#include "DX12ResourceTrackerPrivate.h"

#include <array>

#include "DX12HookManager.h"

typedef HRESULT(STDMETHODCALLTYPE *PFN_CREATE_DESCRIPTOR_HEAP)(
	ID3D12Device*, const D3D12_DESCRIPTOR_HEAP_DESC*, REFIID, void**);
typedef HRESULT(STDMETHODCALLTYPE *PFN_CREATE_ROOT_SIGNATURE)(
	ID3D12Device*, UINT, const void*, SIZE_T, REFIID, void**);
typedef void(STDMETHODCALLTYPE *PFN_CREATE_CONSTANT_BUFFER_VIEW)(
	ID3D12Device*, const D3D12_CONSTANT_BUFFER_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
typedef void(STDMETHODCALLTYPE *PFN_CREATE_SHADER_RESOURCE_VIEW)(
	ID3D12Device*, ID3D12Resource*, const D3D12_SHADER_RESOURCE_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
typedef void(STDMETHODCALLTYPE *PFN_CREATE_UNORDERED_ACCESS_VIEW)(
	ID3D12Device*, ID3D12Resource*, ID3D12Resource*, const D3D12_UNORDERED_ACCESS_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
typedef void(STDMETHODCALLTYPE *PFN_CREATE_RENDER_TARGET_VIEW)(
	ID3D12Device*, ID3D12Resource*, const D3D12_RENDER_TARGET_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
typedef void(STDMETHODCALLTYPE *PFN_CREATE_DEPTH_STENCIL_VIEW)(
	ID3D12Device*, ID3D12Resource*, const D3D12_DEPTH_STENCIL_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
typedef void(STDMETHODCALLTYPE *PFN_CREATE_SAMPLER)(
	ID3D12Device*, const D3D12_SAMPLER_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
typedef void(STDMETHODCALLTYPE *PFN_COPY_DESCRIPTORS)(
	ID3D12Device*, UINT, const D3D12_CPU_DESCRIPTOR_HANDLE*, const UINT*,
	UINT, const D3D12_CPU_DESCRIPTOR_HANDLE*, const UINT*, D3D12_DESCRIPTOR_HEAP_TYPE);
typedef void(STDMETHODCALLTYPE *PFN_COPY_DESCRIPTORS_SIMPLE)(
	ID3D12Device*, UINT, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_CPU_DESCRIPTOR_HANDLE,
	D3D12_DESCRIPTOR_HEAP_TYPE);
typedef HRESULT(STDMETHODCALLTYPE *PFN_CREATE_COMMITTED_RESOURCE)(
	ID3D12Device*, const D3D12_HEAP_PROPERTIES*, D3D12_HEAP_FLAGS,
	const D3D12_RESOURCE_DESC*, D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE*,
	REFIID, void**);
typedef HRESULT(STDMETHODCALLTYPE *PFN_CREATE_PLACED_RESOURCE)(
	ID3D12Device*, ID3D12Heap*, UINT64, const D3D12_RESOURCE_DESC*,
	D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE*, REFIID, void**);
typedef HRESULT(STDMETHODCALLTYPE *PFN_CREATE_RESERVED_RESOURCE)(
	ID3D12Device*, const D3D12_RESOURCE_DESC*, D3D12_RESOURCE_STATES,
	const D3D12_CLEAR_VALUE*, REFIID, void**);
typedef HRESULT(STDMETHODCALLTYPE *PFN_CREATE_COMMITTED_RESOURCE1)(
	ID3D12Device4*, const D3D12_HEAP_PROPERTIES*, D3D12_HEAP_FLAGS,
	const D3D12_RESOURCE_DESC*, D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE*,
	ID3D12ProtectedResourceSession*, REFIID, void**);
typedef HRESULT(STDMETHODCALLTYPE *PFN_CREATE_RESERVED_RESOURCE1)(
	ID3D12Device4*, const D3D12_RESOURCE_DESC*, D3D12_RESOURCE_STATES,
	const D3D12_CLEAR_VALUE*, ID3D12ProtectedResourceSession*, REFIID, void**);
typedef HRESULT(STDMETHODCALLTYPE *PFN_CREATE_COMMITTED_RESOURCE2)(
	ID3D12Device8*, const D3D12_HEAP_PROPERTIES*, D3D12_HEAP_FLAGS,
	const D3D12_RESOURCE_DESC1*, D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE*,
	ID3D12ProtectedResourceSession*, REFIID, void**);
typedef HRESULT(STDMETHODCALLTYPE *PFN_CREATE_PLACED_RESOURCE1)(
	ID3D12Device8*, ID3D12Heap*, UINT64, const D3D12_RESOURCE_DESC1*,
	D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE*, REFIID, void**);
typedef HRESULT(STDMETHODCALLTYPE *PFN_CREATE_COMMITTED_RESOURCE3)(
	ID3D12Device10*, const D3D12_HEAP_PROPERTIES*, D3D12_HEAP_FLAGS,
	const D3D12_RESOURCE_DESC1*, D3D12_BARRIER_LAYOUT, const D3D12_CLEAR_VALUE*,
	ID3D12ProtectedResourceSession*, UINT32, const DXGI_FORMAT*, REFIID, void**);
typedef HRESULT(STDMETHODCALLTYPE *PFN_CREATE_PLACED_RESOURCE2)(
	ID3D12Device10*, ID3D12Heap*, UINT64, const D3D12_RESOURCE_DESC1*,
	D3D12_BARRIER_LAYOUT, const D3D12_CLEAR_VALUE*, UINT32, const DXGI_FORMAT*,
	REFIID, void**);
typedef HRESULT(STDMETHODCALLTYPE *PFN_CREATE_RESERVED_RESOURCE2)(
	ID3D12Device10*, const D3D12_RESOURCE_DESC*, D3D12_BARRIER_LAYOUT,
	const D3D12_CLEAR_VALUE*, ID3D12ProtectedResourceSession*, UINT32,
	const DXGI_FORMAT*, REFIID, void**);

static PFN_CREATE_DESCRIPTOR_HEAP gOrigCreateDescriptorHeap = nullptr;
static PFN_CREATE_ROOT_SIGNATURE gOrigCreateRootSignature = nullptr;
static PFN_CREATE_CONSTANT_BUFFER_VIEW gOrigCreateConstantBufferView = nullptr;
static PFN_CREATE_SHADER_RESOURCE_VIEW gOrigCreateShaderResourceView = nullptr;
static PFN_CREATE_UNORDERED_ACCESS_VIEW gOrigCreateUnorderedAccessView = nullptr;
static PFN_CREATE_RENDER_TARGET_VIEW gOrigCreateRenderTargetView = nullptr;
static PFN_CREATE_DEPTH_STENCIL_VIEW gOrigCreateDepthStencilView = nullptr;
static PFN_CREATE_SAMPLER gOrigCreateSampler = nullptr;
static PFN_COPY_DESCRIPTORS gOrigCopyDescriptors = nullptr;
static PFN_COPY_DESCRIPTORS_SIMPLE gOrigCopyDescriptorsSimple = nullptr;
static PFN_CREATE_COMMITTED_RESOURCE gOrigCreateCommittedResource = nullptr;
static PFN_CREATE_PLACED_RESOURCE gOrigCreatePlacedResource = nullptr;
static PFN_CREATE_RESERVED_RESOURCE gOrigCreateReservedResource = nullptr;
static PFN_CREATE_COMMITTED_RESOURCE1 gOrigCreateCommittedResource1 = nullptr;
static PFN_CREATE_RESERVED_RESOURCE1 gOrigCreateReservedResource1 = nullptr;
static PFN_CREATE_COMMITTED_RESOURCE2 gOrigCreateCommittedResource2 = nullptr;
static PFN_CREATE_PLACED_RESOURCE1 gOrigCreatePlacedResource1 = nullptr;
static PFN_CREATE_COMMITTED_RESOURCE3 gOrigCreateCommittedResource3 = nullptr;
static PFN_CREATE_PLACED_RESOURCE2 gOrigCreatePlacedResource2 = nullptr;
static PFN_CREATE_RESERVED_RESOURCE2 gOrigCreateReservedResource2 = nullptr;

static void LogResourceHookCall(const char *api, const void *object)
{
	if (!DX12ShouldLogHookCall(api))
		return;
	DX12LogDebugJsonFunc("DX12HookCall",
		"\"api\":\"%s\",\"present\":%ld,\"this\":\"%p\"",
		api ? api : "", DX12GetPresentCount(), object);
}

static void LogResourceHookCallWhenTracking(const char *api, const void *object)
{
	if (gDX12HotPathSkipAll && !ShouldTrackResourceMetadata())
		return;
	LogResourceHookCall(api, object);
}

static void LogResourceOriginalFallback(const char *api, const void *object, UINT slot, const void *fallback)
{
	if (!DX12ShouldLogHookCall(api))
		return;
	DX12LogDebugJsonFunc("DX12FallbackPath",
		"\"kind\":\"original_lookup\",\"api\":\"%s\",\"present\":%ld,\"this\":\"%p\","
		"\"slot\":%u,\"fallback\":\"%p\"",
		api ? api : "", DX12GetPresentCount(), object, slot, fallback);
}

template <typename T>
static T GetDeviceOriginal(void *device, UINT slot, T fallback, const char *name)
{
	if (device) {
		void **vtable = *reinterpret_cast<void***>(device);
		if (vtable) {
			void *original = DX12GetOriginalFunction(vtable[slot]);
			if (original)
				return reinterpret_cast<T>(original);
		}
	}
	if (fallback) {
		LogResourceOriginalFallback(name, device, slot, reinterpret_cast<const void*>(fallback));
		return fallback;
	}
	DX12LogJsonFunc(name ? name : "ID3D12Device::Unknown",
		"\"event\":\"MissingOriginal\",\"this\":\"%p\",\"slot\":%u",
		device, slot);
	return nullptr;
}

static HRESULT STDMETHODCALLTYPE HookedCreateDescriptorHeap(
	ID3D12Device *device, const D3D12_DESCRIPTOR_HEAP_DESC *desc, REFIID riid, void **heap)
{
	LogResourceHookCallWhenTracking("ID3D12Device::CreateDescriptorHeap", device);
	auto original = GetDeviceOriginal(
		device, 14, gOrigCreateDescriptorHeap, "ID3D12Device::CreateDescriptorHeap");
	if (DX12IsInternalReplay())
		return original ? original(device, desc, riid, heap) : E_FAIL;
	HRESULT hr = original ? original(device, desc, riid, heap) : E_FAIL;
	if (SUCCEEDED(hr) && desc && heap && *heap) {
		ID3D12DescriptorHeap *descriptorHeap = nullptr;
		if (SUCCEEDED(static_cast<IUnknown*>(*heap)->QueryInterface(IID_PPV_ARGS(&descriptorHeap)))) {
			DescriptorHeapRecord record;
			record.heap = descriptorHeap;
			record.desc = *desc;
			record.cpuStart = descriptorHeap->GetCPUDescriptorHandleForHeapStart().ptr;
			record.gpuStart = descriptorHeap->GetGPUDescriptorHandleForHeapStart().ptr;
			record.increment = device->GetDescriptorHandleIncrementSize(desc->Type);
			descriptorHeap->Release();

			AcquireSRWLockExclusive(&gDescriptorLock);
			++gDescriptorHeapRecordsSeen;
			auto found = gDescriptorHeapByPtr.find(record.heap);
			if (found != gDescriptorHeapByPtr.end()) {
				gDescriptorHeaps[found->second] = record;
			} else {
				gDescriptorHeapByPtr[record.heap] = gDescriptorHeaps.size();
				gDescriptorHeaps.push_back(record);
			}
			LogResourceTrackerStatsLocked();
			ReleaseSRWLockExclusive(&gDescriptorLock);
		}
	}
	return hr;
}

static HRESULT STDMETHODCALLTYPE HookedCreateRootSignature(
	ID3D12Device *device, UINT nodeMask, const void *blob, SIZE_T blobLength,
	REFIID riid, void **rootSignature)
{
	LogResourceHookCallWhenTracking("ID3D12Device::CreateRootSignature", device);
	auto original = GetDeviceOriginal(
		device, 16, gOrigCreateRootSignature, "ID3D12Device::CreateRootSignature");
	if (DX12IsInternalReplay())
		return original ? original(device, nodeMask, blob, blobLength, riid, rootSignature) : E_FAIL;
	HRESULT hr = original ? original(device, nodeMask, blob, blobLength, riid, rootSignature) : E_FAIL;
	if (SUCCEEDED(hr) && blob && blobLength && rootSignature && *rootSignature) {
		RootSignatureRecord record;
		record.rootSignature = static_cast<ID3D12RootSignature*>(*rootSignature);
		record.hash = Fnv1a64(blob, blobLength);
		record.size = blobLength;
		record.nodeMask = nodeMask;
		ParseRootSignatureBlob(&record, blob, blobLength);

		AcquireSRWLockExclusive(&gDescriptorLock);
		++gRootSignatureRecordsSeen;
		auto found = gRootSignatureByPtr.find(record.rootSignature);
		if (found != gRootSignatureByPtr.end()) {
			gRootSignatures[found->second] = std::move(record);
		} else {
			gRootSignatureByPtr[record.rootSignature] = gRootSignatures.size();
			gRootSignatures.push_back(std::move(record));
		}
		LogResourceTrackerStatsLocked();
		ReleaseSRWLockExclusive(&gDescriptorLock);
	}
	return hr;
}

static void STDMETHODCALLTYPE HookedCreateConstantBufferView(
	ID3D12Device *device, const D3D12_CONSTANT_BUFFER_VIEW_DESC *desc,
	D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor)
{
	LogResourceHookCallWhenTracking("ID3D12Device::CreateConstantBufferView", device);
	auto original = GetDeviceOriginal(
		device, 17, gOrigCreateConstantBufferView, "ID3D12Device::CreateConstantBufferView");
	if (!original)
		return;
	if (DX12IsInternalReplay()) {
		original(device, desc, destDescriptor);
		return;
	}
	original(device, desc, destDescriptor);
	if (!ShouldTrackDescriptorCreationMetadata())
		return;
	if (desc) {
		DescriptorRecord record;
		record.kind = "CBV";
		record.cpuHandle = destDescriptor.ptr;
		record.cbv = *desc;
		record.hasDesc = true;
		record.gpuVirtualAddress = desc->BufferLocation;
		record.viewSize = desc->SizeInBytes;
		ResolveBufferByGpuVa(desc->BufferLocation, desc->SizeInBytes, &record);
		RecordDescriptor(std::move(record));
	}
}

static void STDMETHODCALLTYPE HookedCreateShaderResourceView(
	ID3D12Device *device, ID3D12Resource *resource,
	const D3D12_SHADER_RESOURCE_VIEW_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor)
{
	LogResourceHookCallWhenTracking("ID3D12Device::CreateShaderResourceView", device);
	auto original = GetDeviceOriginal(
		device, 18, gOrigCreateShaderResourceView, "ID3D12Device::CreateShaderResourceView");
	if (!original)
		return;
	if (DX12IsInternalReplay()) {
		original(device, resource, desc, destDescriptor);
		return;
	}
	original(device, resource, desc, destDescriptor);
	if (!ShouldTrackDescriptorCreationMetadata())
		return;
	DescriptorRecord record;
	record.kind = "SRV";
	record.cpuHandle = destDescriptor.ptr;
	FillResourceInfo(&record, resource);
	if (desc) {
		record.srv = *desc;
		record.hasDesc = true;
		FillSrvBufferView(&record);
	}
	RecordDescriptor(std::move(record));
}

static void STDMETHODCALLTYPE HookedCreateUnorderedAccessView(
	ID3D12Device *device, ID3D12Resource *resource, ID3D12Resource *counterResource,
	const D3D12_UNORDERED_ACCESS_VIEW_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor)
{
	LogResourceHookCallWhenTracking("ID3D12Device::CreateUnorderedAccessView", device);
	auto original = GetDeviceOriginal(
		device, 19, gOrigCreateUnorderedAccessView, "ID3D12Device::CreateUnorderedAccessView");
	if (!original)
		return;
	if (DX12IsInternalReplay()) {
		original(device, resource, counterResource, desc, destDescriptor);
		return;
	}
	D3D12_UNORDERED_ACCESS_VIEW_DESC adjustedDesc = {};
	const D3D12_UNORDERED_ACCESS_VIEW_DESC *descToUse = desc;
	if (desc) {
		adjustedDesc = *desc;
		if (DX12ModAdjustUavDesc(resource, &adjustedDesc, "CreateUnorderedAccessView"))
			descToUse = &adjustedDesc;
	}
	original(device, resource, counterResource, descToUse, destDescriptor);
	if (!ShouldTrackDescriptorCreationMetadata())
		return;
	DescriptorRecord record;
	record.kind = "UAV";
	record.cpuHandle = destDescriptor.ptr;
	record.counterResource = counterResource;
	FillResourceInfo(&record, resource);
	if (descToUse) {
		record.uav = *descToUse;
		record.hasDesc = true;
		FillUavBufferView(&record);
	}
	RecordDescriptor(std::move(record));
}

static void STDMETHODCALLTYPE HookedCreateRenderTargetView(
	ID3D12Device *device, ID3D12Resource *resource,
	const D3D12_RENDER_TARGET_VIEW_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor)
{
	LogResourceHookCallWhenTracking("ID3D12Device::CreateRenderTargetView", device);
	auto original = GetDeviceOriginal(
		device, 20, gOrigCreateRenderTargetView, "ID3D12Device::CreateRenderTargetView");
	if (!original)
		return;
	if (DX12IsInternalReplay()) {
		original(device, resource, desc, destDescriptor);
		return;
	}
	original(device, resource, desc, destDescriptor);
	if (!ShouldTrackDescriptorCreationMetadata())
		return;
	DescriptorRecord record;
	record.kind = "RTV";
	record.cpuHandle = destDescriptor.ptr;
	FillResourceInfo(&record, resource);
	if (desc) {
		record.rtv = *desc;
		record.hasDesc = true;
	}
	RecordDescriptor(std::move(record));
}

static void STDMETHODCALLTYPE HookedCreateDepthStencilView(
	ID3D12Device *device, ID3D12Resource *resource,
	const D3D12_DEPTH_STENCIL_VIEW_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor)
{
	LogResourceHookCallWhenTracking("ID3D12Device::CreateDepthStencilView", device);
	auto original = GetDeviceOriginal(
		device, 21, gOrigCreateDepthStencilView, "ID3D12Device::CreateDepthStencilView");
	if (!original)
		return;
	if (DX12IsInternalReplay()) {
		original(device, resource, desc, destDescriptor);
		return;
	}
	original(device, resource, desc, destDescriptor);
	if (!ShouldTrackDescriptorCreationMetadata())
		return;
	DescriptorRecord record;
	record.kind = "DSV";
	record.cpuHandle = destDescriptor.ptr;
	FillResourceInfo(&record, resource);
	if (desc) {
		record.dsv = *desc;
		record.hasDesc = true;
	}
	RecordDescriptor(std::move(record));
}

static void STDMETHODCALLTYPE HookedCreateSampler(
	ID3D12Device *device, const D3D12_SAMPLER_DESC *desc,
	D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor)
{
	LogResourceHookCallWhenTracking("ID3D12Device::CreateSampler", device);
	auto original = GetDeviceOriginal(
		device, 22, gOrigCreateSampler, "ID3D12Device::CreateSampler");
	if (!original)
		return;
	if (DX12IsInternalReplay()) {
		original(device, desc, destDescriptor);
		return;
	}
	original(device, desc, destDescriptor);
	if (!ShouldTrackDescriptorCreationMetadata())
		return;
	if (desc) {
		DescriptorRecord record;
		record.kind = "Sampler";
		record.cpuHandle = destDescriptor.ptr;
		record.sampler = *desc;
		record.hasDesc = true;
		RecordDescriptor(std::move(record));
	}
}

static void STDMETHODCALLTYPE HookedCopyDescriptors(
	ID3D12Device *device, UINT numDestDescriptorRanges,
	const D3D12_CPU_DESCRIPTOR_HANDLE *destDescriptorRangeStarts,
	const UINT *destDescriptorRangeSizes, UINT numSrcDescriptorRanges,
	const D3D12_CPU_DESCRIPTOR_HANDLE *srcDescriptorRangeStarts,
	const UINT *srcDescriptorRangeSizes, D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapsType)
{
	LogResourceHookCallWhenTracking("ID3D12Device::CopyDescriptors", device);
	auto original = GetDeviceOriginal(
		device, 23, gOrigCopyDescriptors, "ID3D12Device::CopyDescriptors");
	if (!original)
		return;
	if (DX12IsInternalReplay()) {
		original(device, numDestDescriptorRanges, destDescriptorRangeStarts,
			destDescriptorRangeSizes, numSrcDescriptorRanges, srcDescriptorRangeStarts,
			srcDescriptorRangeSizes, descriptorHeapsType);
		return;
	}
	original(device, numDestDescriptorRanges, destDescriptorRangeStarts,
		destDescriptorRangeSizes, numSrcDescriptorRanges, srcDescriptorRangeStarts,
		srcDescriptorRangeSizes, descriptorHeapsType);

	if (!ShouldTrackComputeDescriptorMetadata())
		return;
	if (!destDescriptorRangeStarts || !srcDescriptorRangeStarts)
		return;

	UINT destRange = 0;
	UINT srcRange = 0;
	UINT destOffset = 0;
	UINT srcOffset = 0;
	while (destRange < numDestDescriptorRanges && srcRange < numSrcDescriptorRanges) {
		const UINT destRangeSize = destDescriptorRangeSizes ? destDescriptorRangeSizes[destRange] : 1;
		const UINT srcRangeSize = srcDescriptorRangeSizes ? srcDescriptorRangeSizes[srcRange] : 1;
		if (destOffset >= destRangeSize) {
			destRange++;
			destOffset = 0;
			continue;
		}
		if (srcOffset >= srcRangeSize) {
			srcRange++;
			srcOffset = 0;
			continue;
		}

		const UINT count = min(destRangeSize - destOffset, srcRangeSize - srcOffset);
		const UINT increment = device ? device->GetDescriptorHandleIncrementSize(descriptorHeapsType) : 0;
		if (increment) {
			D3D12_CPU_DESCRIPTOR_HANDLE dest = destDescriptorRangeStarts[destRange];
			D3D12_CPU_DESCRIPTOR_HANDLE src = srcDescriptorRangeStarts[srcRange];
			dest.ptr += static_cast<SIZE_T>(destOffset) * increment;
			src.ptr += static_cast<SIZE_T>(srcOffset) * increment;
			RecordDescriptorCopyRange(device, descriptorHeapsType, dest, src, count);
		}
		destOffset += count;
		srcOffset += count;
	}
}

static void STDMETHODCALLTYPE HookedCopyDescriptorsSimple(
	ID3D12Device *device, UINT numDescriptors, D3D12_CPU_DESCRIPTOR_HANDLE destDescriptorRangeStart,
	D3D12_CPU_DESCRIPTOR_HANDLE srcDescriptorRangeStart, D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapsType)
{
	LogResourceHookCallWhenTracking("ID3D12Device::CopyDescriptorsSimple", device);
	auto original = GetDeviceOriginal(
		device, 24, gOrigCopyDescriptorsSimple, "ID3D12Device::CopyDescriptorsSimple");
	if (!original)
		return;
	if (DX12IsInternalReplay()) {
		original(device, numDescriptors, destDescriptorRangeStart,
			srcDescriptorRangeStart, descriptorHeapsType);
		return;
	}
	original(device, numDescriptors, destDescriptorRangeStart,
		srcDescriptorRangeStart, descriptorHeapsType);
	if (!ShouldTrackComputeDescriptorMetadata())
		return;
	RecordDescriptorCopyRange(device, descriptorHeapsType, destDescriptorRangeStart,
		srcDescriptorRangeStart, numDescriptors);
}

static HRESULT STDMETHODCALLTYPE HookedCreateCommittedResource(
	ID3D12Device *device, const D3D12_HEAP_PROPERTIES *heapProperties,
	D3D12_HEAP_FLAGS heapFlags, const D3D12_RESOURCE_DESC *desc,
	D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE *optimizedClearValue,
	REFIID riid, void **resource)
{
	LogResourceHookCallWhenTracking("ID3D12Device::CreateCommittedResource", device);
	auto original = GetDeviceOriginal(
		device, 27, gOrigCreateCommittedResource, "ID3D12Device::CreateCommittedResource");
	if (!original)
		return E_FAIL;
	if (DX12IsInternalReplay())
		return original(device, heapProperties, heapFlags, desc,
			initialState, optimizedClearValue, riid, resource);
	D3D12_RESOURCE_DESC adjustedDesc = {};
	const D3D12_RESOURCE_DESC *descToUse = desc;
	if (desc) {
		adjustedDesc = *desc;
		if (DX12ModAdjustBufferResourceDesc(&adjustedDesc, "CreateCommittedResource"))
			descToUse = &adjustedDesc;
	}
	HRESULT hr = original(device, heapProperties, heapFlags, descToUse,
		initialState, optimizedClearValue, riid, resource);
	if (SUCCEEDED(hr) && resource && *resource) {
		ID3D12Resource *d3dResource = nullptr;
		if (SUCCEEDED(static_cast<IUnknown*>(*resource)->QueryInterface(IID_PPV_ARGS(&d3dResource)))) {
			RecordResource(d3dResource, descToUse, heapProperties, initialState);
			d3dResource->Release();
		}
	}
	return hr;
}

static HRESULT STDMETHODCALLTYPE HookedCreatePlacedResource(
	ID3D12Device *device, ID3D12Heap *heap, UINT64 heapOffset,
	const D3D12_RESOURCE_DESC *desc, D3D12_RESOURCE_STATES initialState,
	const D3D12_CLEAR_VALUE *optimizedClearValue, REFIID riid, void **resource)
{
	LogResourceHookCallWhenTracking("ID3D12Device::CreatePlacedResource", device);
	auto original = GetDeviceOriginal(
		device, 29, gOrigCreatePlacedResource, "ID3D12Device::CreatePlacedResource");
	if (!original)
		return E_FAIL;
	if (DX12IsInternalReplay())
		return original(device, heap, heapOffset, desc,
			initialState, optimizedClearValue, riid, resource);
	HRESULT hr = original(device, heap, heapOffset, desc,
		initialState, optimizedClearValue, riid, resource);
	if (SUCCEEDED(hr) && resource && *resource) {
		ID3D12Resource *d3dResource = nullptr;
		if (SUCCEEDED(static_cast<IUnknown*>(*resource)->QueryInterface(IID_PPV_ARGS(&d3dResource)))) {
			RecordResource(d3dResource, desc, nullptr, initialState);
			d3dResource->Release();
		}
	}
	return hr;
}

static HRESULT STDMETHODCALLTYPE HookedCreateReservedResource(
	ID3D12Device *device, const D3D12_RESOURCE_DESC *desc,
	D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE *optimizedClearValue,
	REFIID riid, void **resource)
{
	LogResourceHookCallWhenTracking("ID3D12Device::CreateReservedResource", device);
	auto original = GetDeviceOriginal(
		device, 30, gOrigCreateReservedResource, "ID3D12Device::CreateReservedResource");
	if (!original)
		return E_FAIL;
	if (DX12IsInternalReplay())
		return original(device, desc, initialState,
			optimizedClearValue, riid, resource);
	HRESULT hr = original(device, desc, initialState,
		optimizedClearValue, riid, resource);
	if (SUCCEEDED(hr) && resource && *resource) {
		ID3D12Resource *d3dResource = nullptr;
		if (SUCCEEDED(static_cast<IUnknown*>(*resource)->QueryInterface(IID_PPV_ARGS(&d3dResource)))) {
			RecordResource(d3dResource, desc, nullptr, initialState);
			d3dResource->Release();
		}
	}
	return hr;
}

static HRESULT STDMETHODCALLTYPE HookedCreateCommittedResource1(
	ID3D12Device4 *device, const D3D12_HEAP_PROPERTIES *heapProperties,
	D3D12_HEAP_FLAGS heapFlags, const D3D12_RESOURCE_DESC *desc,
	D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE *optimizedClearValue,
	ID3D12ProtectedResourceSession *protectedSession, REFIID riid, void **resource)
{
	LogResourceHookCallWhenTracking("ID3D12Device4::CreateCommittedResource1", device);
	auto original = GetDeviceOriginal(
		device, 53, gOrigCreateCommittedResource1, "ID3D12Device4::CreateCommittedResource1");
	if (!original)
		return E_FAIL;
	if (DX12IsInternalReplay())
		return original(device, heapProperties, heapFlags, desc,
			initialState, optimizedClearValue, protectedSession, riid, resource);
	D3D12_RESOURCE_DESC adjustedDesc = {};
	const D3D12_RESOURCE_DESC *descToUse = desc;
	if (desc) {
		adjustedDesc = *desc;
		if (DX12ModAdjustBufferResourceDesc(&adjustedDesc, "CreateCommittedResource1"))
			descToUse = &adjustedDesc;
	}
	HRESULT hr = original(device, heapProperties, heapFlags, descToUse,
		initialState, optimizedClearValue, protectedSession, riid, resource);
	if (SUCCEEDED(hr) && resource && *resource) {
		ID3D12Resource *d3dResource = nullptr;
		if (SUCCEEDED(static_cast<IUnknown*>(*resource)->QueryInterface(IID_PPV_ARGS(&d3dResource)))) {
			RecordResource(d3dResource, descToUse, heapProperties, initialState);
			d3dResource->Release();
		}
	}
	return hr;
}

static HRESULT STDMETHODCALLTYPE HookedCreateReservedResource1(
	ID3D12Device4 *device, const D3D12_RESOURCE_DESC *desc,
	D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE *optimizedClearValue,
	ID3D12ProtectedResourceSession *protectedSession, REFIID riid, void **resource)
{
	LogResourceHookCallWhenTracking("ID3D12Device4::CreateReservedResource1", device);
	auto original = GetDeviceOriginal(
		device, 55, gOrigCreateReservedResource1, "ID3D12Device4::CreateReservedResource1");
	if (!original)
		return E_FAIL;
	if (DX12IsInternalReplay())
		return original(device, desc, initialState,
			optimizedClearValue, protectedSession, riid, resource);
	HRESULT hr = original(device, desc, initialState,
		optimizedClearValue, protectedSession, riid, resource);
	if (SUCCEEDED(hr) && resource && *resource) {
		ID3D12Resource *d3dResource = nullptr;
		if (SUCCEEDED(static_cast<IUnknown*>(*resource)->QueryInterface(IID_PPV_ARGS(&d3dResource)))) {
			RecordResource(d3dResource, desc, nullptr, initialState);
			d3dResource->Release();
		}
	}
	return hr;
}

static HRESULT STDMETHODCALLTYPE HookedCreateCommittedResource2(
	ID3D12Device8 *device, const D3D12_HEAP_PROPERTIES *heapProperties,
	D3D12_HEAP_FLAGS heapFlags, const D3D12_RESOURCE_DESC1 *desc,
	D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE *optimizedClearValue,
	ID3D12ProtectedResourceSession *protectedSession, REFIID riid, void **resource)
{
	LogResourceHookCallWhenTracking("ID3D12Device8::CreateCommittedResource2", device);
	auto original = GetDeviceOriginal(
		device, 69, gOrigCreateCommittedResource2, "ID3D12Device8::CreateCommittedResource2");
	if (!original)
		return E_FAIL;
	if (DX12IsInternalReplay())
		return original(device, heapProperties, heapFlags, desc,
			initialState, optimizedClearValue, protectedSession, riid, resource);
	D3D12_RESOURCE_DESC1 adjustedDesc = {};
	const D3D12_RESOURCE_DESC1 *descToUse = desc;
	if (desc) {
		adjustedDesc = *desc;
		if (DX12ModAdjustBufferResourceDesc1(&adjustedDesc, "CreateCommittedResource2"))
			descToUse = &adjustedDesc;
	}
	HRESULT hr = original(device, heapProperties, heapFlags, descToUse,
		initialState, optimizedClearValue, protectedSession, riid, resource);
	if (SUCCEEDED(hr) && resource && *resource) {
		ID3D12Resource *d3dResource = nullptr;
		if (SUCCEEDED(static_cast<IUnknown*>(*resource)->QueryInterface(IID_PPV_ARGS(&d3dResource)))) {
			D3D12_RESOURCE_DESC desc0 = ResourceDescFromDesc1(descToUse);
			RecordResource(d3dResource, descToUse ? &desc0 : nullptr, heapProperties, initialState);
			d3dResource->Release();
		}
	}
	return hr;
}

static HRESULT STDMETHODCALLTYPE HookedCreatePlacedResource1(
	ID3D12Device8 *device, ID3D12Heap *heap, UINT64 heapOffset,
	const D3D12_RESOURCE_DESC1 *desc, D3D12_RESOURCE_STATES initialState,
	const D3D12_CLEAR_VALUE *optimizedClearValue, REFIID riid, void **resource)
{
	LogResourceHookCallWhenTracking("ID3D12Device8::CreatePlacedResource1", device);
	auto original = GetDeviceOriginal(
		device, 70, gOrigCreatePlacedResource1, "ID3D12Device8::CreatePlacedResource1");
	if (!original)
		return E_FAIL;
	if (DX12IsInternalReplay())
		return original(device, heap, heapOffset, desc,
			initialState, optimizedClearValue, riid, resource);
	HRESULT hr = original(device, heap, heapOffset, desc,
		initialState, optimizedClearValue, riid, resource);
	if (SUCCEEDED(hr) && resource && *resource) {
		ID3D12Resource *d3dResource = nullptr;
		if (SUCCEEDED(static_cast<IUnknown*>(*resource)->QueryInterface(IID_PPV_ARGS(&d3dResource)))) {
			D3D12_RESOURCE_DESC desc0 = ResourceDescFromDesc1(desc);
			RecordResource(d3dResource, desc ? &desc0 : nullptr, nullptr, initialState);
			d3dResource->Release();
		}
	}
	return hr;
}

static HRESULT STDMETHODCALLTYPE HookedCreateCommittedResource3(
	ID3D12Device10 *device, const D3D12_HEAP_PROPERTIES *heapProperties,
	D3D12_HEAP_FLAGS heapFlags, const D3D12_RESOURCE_DESC1 *desc,
	D3D12_BARRIER_LAYOUT initialLayout, const D3D12_CLEAR_VALUE *optimizedClearValue,
	ID3D12ProtectedResourceSession *protectedSession, UINT32 numCastableFormats,
	const DXGI_FORMAT *castableFormats, REFIID riid, void **resource)
{
	LogResourceHookCallWhenTracking("ID3D12Device10::CreateCommittedResource3", device);
	auto original = GetDeviceOriginal(
		device, 76, gOrigCreateCommittedResource3, "ID3D12Device10::CreateCommittedResource3");
	if (!original)
		return E_FAIL;
	if (DX12IsInternalReplay())
		return original(device, heapProperties, heapFlags, desc,
			initialLayout, optimizedClearValue, protectedSession, numCastableFormats,
			castableFormats, riid, resource);
	D3D12_RESOURCE_DESC1 adjustedDesc = {};
	const D3D12_RESOURCE_DESC1 *descToUse = desc;
	if (desc) {
		adjustedDesc = *desc;
		if (DX12ModAdjustBufferResourceDesc1(&adjustedDesc, "CreateCommittedResource3"))
			descToUse = &adjustedDesc;
	}
	HRESULT hr = original(device, heapProperties, heapFlags, descToUse,
		initialLayout, optimizedClearValue, protectedSession, numCastableFormats,
		castableFormats, riid, resource);
	if (SUCCEEDED(hr) && resource && *resource) {
		ID3D12Resource *d3dResource = nullptr;
		if (SUCCEEDED(static_cast<IUnknown*>(*resource)->QueryInterface(IID_PPV_ARGS(&d3dResource)))) {
			D3D12_RESOURCE_DESC desc0 = ResourceDescFromDesc1(descToUse);
			RecordResource(d3dResource, descToUse ? &desc0 : nullptr, heapProperties,
				ResourceStateFromLayout(initialLayout));
			d3dResource->Release();
		}
	}
	return hr;
}

static HRESULT STDMETHODCALLTYPE HookedCreatePlacedResource2(
	ID3D12Device10 *device, ID3D12Heap *heap, UINT64 heapOffset,
	const D3D12_RESOURCE_DESC1 *desc, D3D12_BARRIER_LAYOUT initialLayout,
	const D3D12_CLEAR_VALUE *optimizedClearValue, UINT32 numCastableFormats,
	const DXGI_FORMAT *castableFormats, REFIID riid, void **resource)
{
	LogResourceHookCallWhenTracking("ID3D12Device10::CreatePlacedResource2", device);
	auto original = GetDeviceOriginal(
		device, 77, gOrigCreatePlacedResource2, "ID3D12Device10::CreatePlacedResource2");
	if (!original)
		return E_FAIL;
	if (DX12IsInternalReplay())
		return original(device, heap, heapOffset, desc,
			initialLayout, optimizedClearValue, numCastableFormats, castableFormats,
			riid, resource);
	HRESULT hr = original(device, heap, heapOffset, desc,
		initialLayout, optimizedClearValue, numCastableFormats, castableFormats,
		riid, resource);
	if (SUCCEEDED(hr) && resource && *resource) {
		ID3D12Resource *d3dResource = nullptr;
		if (SUCCEEDED(static_cast<IUnknown*>(*resource)->QueryInterface(IID_PPV_ARGS(&d3dResource)))) {
			D3D12_RESOURCE_DESC desc0 = ResourceDescFromDesc1(desc);
			RecordResource(d3dResource, desc ? &desc0 : nullptr, nullptr,
				ResourceStateFromLayout(initialLayout));
			d3dResource->Release();
		}
	}
	return hr;
}

static HRESULT STDMETHODCALLTYPE HookedCreateReservedResource2(
	ID3D12Device10 *device, const D3D12_RESOURCE_DESC *desc,
	D3D12_BARRIER_LAYOUT initialLayout, const D3D12_CLEAR_VALUE *optimizedClearValue,
	ID3D12ProtectedResourceSession *protectedSession, UINT32 numCastableFormats,
	const DXGI_FORMAT *castableFormats, REFIID riid, void **resource)
{
	LogResourceHookCallWhenTracking("ID3D12Device10::CreateReservedResource2", device);
	auto original = GetDeviceOriginal(
		device, 78, gOrigCreateReservedResource2, "ID3D12Device10::CreateReservedResource2");
	if (!original)
		return E_FAIL;
	if (DX12IsInternalReplay())
		return original(device, desc, initialLayout,
			optimizedClearValue, protectedSession, numCastableFormats, castableFormats,
			riid, resource);
	HRESULT hr = original(device, desc, initialLayout,
		optimizedClearValue, protectedSession, numCastableFormats, castableFormats,
		riid, resource);
	if (SUCCEEDED(hr) && resource && *resource) {
		ID3D12Resource *d3dResource = nullptr;
		if (SUCCEEDED(static_cast<IUnknown*>(*resource)->QueryInterface(IID_PPV_ARGS(&d3dResource)))) {
			RecordResource(d3dResource, desc, nullptr, ResourceStateFromLayout(initialLayout));
			d3dResource->Release();
		}
	}
	return hr;
}

void DX12HookResourceMetadata(ID3D12Device *device)
{
	if (!device)
		return;

	if (!gCleanupRegistered) {
		atexit(CleanupTrackedResources);
		gCleanupRegistered = true;
		DX12Log("Resource metadata tracking uses non-owning resource pointers\n");
	}

	DX12VTableHook deviceHooks[] = {
		{14, reinterpret_cast<void**>(&gOrigCreateDescriptorHeap),
			HookedCreateDescriptorHeap, "ID3D12Device::CreateDescriptorHeap"},
		{16, reinterpret_cast<void**>(&gOrigCreateRootSignature),
			HookedCreateRootSignature, "ID3D12Device::CreateRootSignature"},
		{17, reinterpret_cast<void**>(&gOrigCreateConstantBufferView),
			HookedCreateConstantBufferView, "ID3D12Device::CreateConstantBufferView"},
		{18, reinterpret_cast<void**>(&gOrigCreateShaderResourceView),
			HookedCreateShaderResourceView, "ID3D12Device::CreateShaderResourceView"},
		{19, reinterpret_cast<void**>(&gOrigCreateUnorderedAccessView),
			HookedCreateUnorderedAccessView, "ID3D12Device::CreateUnorderedAccessView"},
		{20, reinterpret_cast<void**>(&gOrigCreateRenderTargetView),
			HookedCreateRenderTargetView, "ID3D12Device::CreateRenderTargetView"},
		{21, reinterpret_cast<void**>(&gOrigCreateDepthStencilView),
			HookedCreateDepthStencilView, "ID3D12Device::CreateDepthStencilView"},
		{22, reinterpret_cast<void**>(&gOrigCreateSampler),
			HookedCreateSampler, "ID3D12Device::CreateSampler"},
		{23, reinterpret_cast<void**>(&gOrigCopyDescriptors),
			HookedCopyDescriptors, "ID3D12Device::CopyDescriptors"},
		{24, reinterpret_cast<void**>(&gOrigCopyDescriptorsSimple),
			HookedCopyDescriptorsSimple, "ID3D12Device::CopyDescriptorsSimple"},
		{27, reinterpret_cast<void**>(&gOrigCreateCommittedResource),
			HookedCreateCommittedResource, "ID3D12Device::CreateCommittedResource"},
		{29, reinterpret_cast<void**>(&gOrigCreatePlacedResource),
			HookedCreatePlacedResource, "ID3D12Device::CreatePlacedResource"},
		{30, reinterpret_cast<void**>(&gOrigCreateReservedResource),
			HookedCreateReservedResource, "ID3D12Device::CreateReservedResource"},
	};
	DX12InstallVTableHooks(device, deviceHooks);

	ID3D12Device4 *device4 = nullptr;
	if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&device4)))) {
		DX12VTableHook device4Hooks[] = {
			{53, reinterpret_cast<void**>(&gOrigCreateCommittedResource1),
				HookedCreateCommittedResource1, "ID3D12Device4::CreateCommittedResource1"},
			{55, reinterpret_cast<void**>(&gOrigCreateReservedResource1),
				HookedCreateReservedResource1, "ID3D12Device4::CreateReservedResource1"},
		};
		DX12InstallVTableHooks(device4, device4Hooks);
		device4->Release();
	}

	ID3D12Device8 *device8 = nullptr;
	if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&device8)))) {
		DX12VTableHook device8Hooks[] = {
			{69, reinterpret_cast<void**>(&gOrigCreateCommittedResource2),
				HookedCreateCommittedResource2, "ID3D12Device8::CreateCommittedResource2"},
			{70, reinterpret_cast<void**>(&gOrigCreatePlacedResource1),
				HookedCreatePlacedResource1, "ID3D12Device8::CreatePlacedResource1"},
		};
		DX12InstallVTableHooks(device8, device8Hooks);
		device8->Release();
	}

	ID3D12Device10 *device10 = nullptr;
	if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&device10)))) {
		DX12VTableHook device10Hooks[] = {
			{76, reinterpret_cast<void**>(&gOrigCreateCommittedResource3),
				HookedCreateCommittedResource3, "ID3D12Device10::CreateCommittedResource3"},
			{77, reinterpret_cast<void**>(&gOrigCreatePlacedResource2),
				HookedCreatePlacedResource2, "ID3D12Device10::CreatePlacedResource2"},
			{78, reinterpret_cast<void**>(&gOrigCreateReservedResource2),
				HookedCreateReservedResource2, "ID3D12Device10::CreateReservedResource2"},
		};
		DX12InstallVTableHooks(device10, device10Hooks);
		device10->Release();
	}
}

