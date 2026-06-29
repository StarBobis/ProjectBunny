#include "DX12ResourceTrackerPrivate.hpp"

#include "DX12HookManager.hpp"
#include "DX12ResourceTrackerHookCallbacks.hpp"
#include "DX12ResourceTrackerHookDiagnostics.hpp"
#include "DX12ResourceTrackerHookRegistry.hpp"
#include "DX12ResourceTrackerHookRuntime.hpp"
#include "DX12ResourceTrackerHooks.hpp"
#include "DX12ResourceTrackerHookState.hpp"
#include "DX12ResourceStateTracker.hpp"
#include "types/DX12ResourceTrackerHookSlotTypes.hpp"

static const GUID DX12DescriptorHeapReleaseTrackerGuid =
{ 0x28c66065, 0x55b7, 0x46df, { 0x9e, 0x1e, 0x08, 0x52, 0x8d, 0x31, 0xcb, 0x1a } };
static const GUID DX12ResourceReleaseTrackerGuid =
{ 0xe1a4bb6a, 0xf6fc, 0x4a80, { 0x90, 0xa4, 0xf5, 0xb9, 0x13, 0x32, 0xf1, 0x5c } };

class DescriptorHeapReleaseTracker : public IUnknown
{
public:
	explicit DescriptorHeapReleaseTracker(ID3D12DescriptorHeap *heap) : mRef(1), mHeap(heap) {}

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **object) override
	{
		if (!object)
			return E_POINTER;
		if (riid == IID_IUnknown) {
			*object = static_cast<IUnknown*>(this);
			AddRef();
			return S_OK;
		}
		*object = nullptr;
		return E_NOINTERFACE;
	}

	ULONG STDMETHODCALLTYPE AddRef() override
	{
		return static_cast<ULONG>(InterlockedIncrement(&mRef));
	}

	ULONG STDMETHODCALLTYPE Release() override
	{
		const LONG ref = InterlockedDecrement(&mRef);
		if (ref == 0) {
			AcquireSRWLockExclusive(&gDescriptorLock);
			InvalidateDescriptorHeapLocked(mHeap);
			ReleaseSRWLockExclusive(&gDescriptorLock);
			delete this;
		}
		return static_cast<ULONG>(ref);
	}

	volatile LONG mRef;
	ID3D12DescriptorHeap *mHeap;
};

class ResourceReleaseTracker : public IUnknown
{
public:
	explicit ResourceReleaseTracker(ID3D12Resource *resource) : mRef(1), mResource(resource) {}

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **object) override
	{
		if (!object)
			return E_POINTER;
		if (riid == IID_IUnknown) {
			*object = static_cast<IUnknown*>(this);
			AddRef();
			return S_OK;
		}
		*object = nullptr;
		return E_NOINTERFACE;
	}

	ULONG STDMETHODCALLTYPE AddRef() override
	{
		return static_cast<ULONG>(InterlockedIncrement(&mRef));
	}

	ULONG STDMETHODCALLTYPE Release() override
	{
		const LONG ref = InterlockedDecrement(&mRef);
		if (ref == 0) {
			AcquireSRWLockExclusive(&gResourceLock);
			InvalidateResourceLocked(mResource);
			ReleaseSRWLockExclusive(&gResourceLock);
			delete this;
		}
		return static_cast<ULONG>(ref);
	}

	volatile LONG mRef;
	ID3D12Resource *mResource;
};

void DX12ResourceTrackerHookRuntime::AttachResourceReleaseTracker(ID3D12Resource *resource)
{
	if (!resource)
		return;
	ResourceReleaseTracker *releaseTracker = new ResourceReleaseTracker(resource);
	if (!releaseTracker)
		return;
	resource->SetPrivateDataInterface(DX12ResourceReleaseTrackerGuid, releaseTracker);
	releaseTracker->Release();
}

bool DX12ResourceTrackerHookRuntime::ShouldRecordCreatedResource(const D3D12_RESOURCE_DESC *desc)
{
	if (DX12RuntimeState::IsHuntingMode())
		return true;
	if (!desc)
		return ShouldTrackResourceCreationMetadata();
	// Resource creation is the cheap identity boundary exposed by D3D12.
	// Record buffers for VA lookup and textures for later Hunting discovery;
	// descriptor-table churn remains gated so Mod mode does not stall.
	return desc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER ||
		IsTextureResourceDimension(desc->Dimension) ||
		ShouldTrackResourceCreationMetadata();
}

HRESULT STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCreateDescriptorHeap(
	ID3D12Device *device, const D3D12_DESCRIPTOR_HEAP_DESC *desc, REFIID riid, void **heap)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device::CreateDescriptorHeap", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device::CreateDescriptorHeap, DX12ResourceTrackerHookState::OrigCreateDescriptorHeap, "ID3D12Device::CreateDescriptorHeap");
	if (DX12InternalReplayState::IsActive())
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
			InitializeDescriptorHeapRecord(&record);
			DescriptorHeapReleaseTracker *releaseTracker =
				new DescriptorHeapReleaseTracker(descriptorHeap);
			if (releaseTracker) {
				descriptorHeap->SetPrivateDataInterface(
					DX12DescriptorHeapReleaseTrackerGuid, releaseTracker);
				releaseTracker->Release();
			}
			descriptorHeap->Release();

			AcquireSRWLockExclusive(&gDescriptorLock);
			++gDescriptorHeapRecordsSeen;
			auto found = gDescriptorHeapByPtr.find(record.heap);
			if (found != gDescriptorHeapByPtr.end()) {
				const size_t heapIndex = found->second;
				DescriptorHeapRecord &old = gDescriptorHeaps[heapIndex];
				InvalidateDescriptorHeapLocked(old.heap);
				gDescriptorHeapByPtr[record.heap] = heapIndex;
				if (record.cpuStart)
					gDescriptorHeapByCpuStart[record.cpuStart] = heapIndex;
				gDescriptorHeaps[heapIndex] = std::move(record);
			} else {
				gDescriptorHeapByPtr[record.heap] = gDescriptorHeaps.size();
				if (record.cpuStart)
					gDescriptorHeapByCpuStart[record.cpuStart] = gDescriptorHeaps.size();
				gDescriptorHeaps.push_back(std::move(record));
			}
			LogResourceTrackerStatsLocked();
			ReleaseSRWLockExclusive(&gDescriptorLock);
		}
	}
	return hr;
}

HRESULT STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCreateRootSignature(
	ID3D12Device *device, UINT nodeMask, const void *blob, SIZE_T blobLength,
	REFIID riid, void **rootSignature)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device::CreateRootSignature", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device::CreateRootSignature, DX12ResourceTrackerHookState::OrigCreateRootSignature, "ID3D12Device::CreateRootSignature");
	if (DX12InternalReplayState::IsActive())
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

void STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCreateConstantBufferView(
	ID3D12Device *device, const D3D12_CONSTANT_BUFFER_VIEW_DESC *desc,
	D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device::CreateConstantBufferView", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device::CreateConstantBufferView, DX12ResourceTrackerHookState::OrigCreateConstantBufferView, "ID3D12Device::CreateConstantBufferView");
	if (!original)
		return;
	if (DX12InternalReplayState::IsActive()) {
		original(device, desc, destDescriptor);
		return;
	}
	original(device, desc, destDescriptor);
	if (!ShouldTrackDescriptorCreationMetadata())
		return;
	if (desc) {
		DescriptorRecord record;
		SetDescriptorKind(&record, DescriptorKind::CBV_ID);
		record.cpuHandle = destDescriptor.ptr;
		record.cbv = *desc;
		record.hasDesc = true;
		record.gpuVirtualAddress = desc->BufferLocation;
		record.viewSize = desc->SizeInBytes;
		PrepareDescriptorValueId(&record);
		if (DescriptorValueUnchanged(record.cpuHandle, record.valueId))
			return;
		if (ShouldTrackResourceMetadata())
			ResolveBufferByGpuVa(desc->BufferLocation, desc->SizeInBytes, &record);
		RecordDescriptor(std::move(record));
	}
}

void STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCreateShaderResourceView(
	ID3D12Device *device, ID3D12Resource *resource,
	const D3D12_SHADER_RESOURCE_VIEW_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device::CreateShaderResourceView", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device::CreateShaderResourceView, DX12ResourceTrackerHookState::OrigCreateShaderResourceView, "ID3D12Device::CreateShaderResourceView");
	if (!original)
		return;
	if (DX12InternalReplayState::IsActive()) {
		original(device, resource, desc, destDescriptor);
		return;
	}
	original(device, resource, desc, destDescriptor);
	if (!ShouldTrackDescriptorCreationMetadata())
		return;
	DescriptorRecord record;
	SetDescriptorKind(&record, DescriptorKind::SRV_ID);
	record.cpuHandle = destDescriptor.ptr;
	FillResourcePointerInfo(&record, resource);
	if (desc) {
		record.srv = *desc;
		record.hasDesc = true;
		FillSrvBufferView(&record);
	}
	PrepareDescriptorValueId(&record);
	if (DescriptorValueUnchanged(record.cpuHandle, record.valueId))
		return;
	RecordDescriptor(std::move(record));
}

void STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCreateUnorderedAccessView(
	ID3D12Device *device, ID3D12Resource *resource, ID3D12Resource *counterResource,
	const D3D12_UNORDERED_ACCESS_VIEW_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device::CreateUnorderedAccessView", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device::CreateUnorderedAccessView, DX12ResourceTrackerHookState::OrigCreateUnorderedAccessView, "ID3D12Device::CreateUnorderedAccessView");
	if (!original)
		return;
	if (DX12InternalReplayState::IsActive()) {
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
	SetDescriptorKind(&record, DescriptorKind::UAV_ID);
	record.cpuHandle = destDescriptor.ptr;
	record.counterResource = counterResource;
	FillResourcePointerInfo(&record, resource);
	if (descToUse) {
		record.uav = *descToUse;
		record.hasDesc = true;
		FillUavBufferView(&record);
	}
	PrepareDescriptorValueId(&record);
	if (DescriptorValueUnchanged(record.cpuHandle, record.valueId))
		return;
	RecordDescriptor(std::move(record));
}

void STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCreateRenderTargetView(
	ID3D12Device *device, ID3D12Resource *resource,
	const D3D12_RENDER_TARGET_VIEW_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device::CreateRenderTargetView", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device::CreateRenderTargetView, DX12ResourceTrackerHookState::OrigCreateRenderTargetView, "ID3D12Device::CreateRenderTargetView");
	if (!original)
		return;
	if (DX12InternalReplayState::IsActive()) {
		original(device, resource, desc, destDescriptor);
		return;
	}
	original(device, resource, desc, destDescriptor);
	if (!ShouldTrackDescriptorCreationMetadata())
		return;
	DescriptorRecord record;
	SetDescriptorKind(&record, DescriptorKind::RTV_ID);
	record.cpuHandle = destDescriptor.ptr;
	FillResourcePointerInfo(&record, resource);
	if (desc) {
		record.rtv = *desc;
		record.hasDesc = true;
	}
	PrepareDescriptorValueId(&record);
	if (DescriptorValueUnchanged(record.cpuHandle, record.valueId))
		return;
	RecordDescriptor(std::move(record));
}

void STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCreateDepthStencilView(
	ID3D12Device *device, ID3D12Resource *resource,
	const D3D12_DEPTH_STENCIL_VIEW_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device::CreateDepthStencilView", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device::CreateDepthStencilView, DX12ResourceTrackerHookState::OrigCreateDepthStencilView, "ID3D12Device::CreateDepthStencilView");
	if (!original)
		return;
	if (DX12InternalReplayState::IsActive()) {
		original(device, resource, desc, destDescriptor);
		return;
	}
	original(device, resource, desc, destDescriptor);
	if (!ShouldTrackDescriptorCreationMetadata())
		return;
	DescriptorRecord record;
	SetDescriptorKind(&record, DescriptorKind::DSV_ID);
	record.cpuHandle = destDescriptor.ptr;
	FillResourcePointerInfo(&record, resource);
	if (desc) {
		record.dsv = *desc;
		record.hasDesc = true;
	}
	PrepareDescriptorValueId(&record);
	if (DescriptorValueUnchanged(record.cpuHandle, record.valueId))
		return;
	RecordDescriptor(std::move(record));
}

void STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCreateSampler(
	ID3D12Device *device, const D3D12_SAMPLER_DESC *desc,
	D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device::CreateSampler", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device::CreateSampler, DX12ResourceTrackerHookState::OrigCreateSampler, "ID3D12Device::CreateSampler");
	if (!original)
		return;
	if (DX12InternalReplayState::IsActive()) {
		original(device, desc, destDescriptor);
		return;
	}
	original(device, desc, destDescriptor);
	if (!ShouldTrackDescriptorCreationMetadata())
		return;
	if (desc) {
		DescriptorRecord record;
		SetDescriptorKind(&record, DescriptorKind::Sampler_ID);
		record.cpuHandle = destDescriptor.ptr;
		record.sampler = *desc;
		record.hasDesc = true;
		PrepareDescriptorValueId(&record);
		if (DescriptorValueUnchanged(record.cpuHandle, record.valueId))
			return;
		RecordDescriptor(std::move(record));
	}
}

void STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCopyDescriptors(
	ID3D12Device *device, UINT numDestDescriptorRanges,
	const D3D12_CPU_DESCRIPTOR_HANDLE *destDescriptorRangeStarts,
	const UINT *destDescriptorRangeSizes, UINT numSrcDescriptorRanges,
	const D3D12_CPU_DESCRIPTOR_HANDLE *srcDescriptorRangeStarts,
	const UINT *srcDescriptorRangeSizes, D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapsType)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device::CopyDescriptors", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device::CopyDescriptors, DX12ResourceTrackerHookState::OrigCopyDescriptors, "ID3D12Device::CopyDescriptors");
	if (!original)
		return;
	if (DX12InternalReplayState::IsActive()) {
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

void STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCopyDescriptorsSimple(
	ID3D12Device *device, UINT numDescriptors, D3D12_CPU_DESCRIPTOR_HANDLE destDescriptorRangeStart,
	D3D12_CPU_DESCRIPTOR_HANDLE srcDescriptorRangeStart, D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapsType)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device::CopyDescriptorsSimple", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device::CopyDescriptorsSimple, DX12ResourceTrackerHookState::OrigCopyDescriptorsSimple, "ID3D12Device::CopyDescriptorsSimple");
	if (!original)
		return;
	if (DX12InternalReplayState::IsActive()) {
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

HRESULT STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCreateCommittedResource(
	ID3D12Device *device, const D3D12_HEAP_PROPERTIES *heapProperties,
	D3D12_HEAP_FLAGS heapFlags, const D3D12_RESOURCE_DESC *desc,
	D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE *optimizedClearValue,
	REFIID riid, void **resource)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device::CreateCommittedResource", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device::CreateCommittedResource, DX12ResourceTrackerHookState::OrigCreateCommittedResource, "ID3D12Device::CreateCommittedResource");
	if (!original)
		return E_FAIL;
	if (DX12InternalReplayState::IsActive())
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
	if (SUCCEEDED(hr) && resource && *resource && DX12ResourceTrackerHookRuntime::ShouldRecordCreatedResource(descToUse)) {
		ID3D12Resource *d3dResource = nullptr;
		if (SUCCEEDED(static_cast<IUnknown*>(*resource)->QueryInterface(IID_PPV_ARGS(&d3dResource)))) {
			RecordResource(d3dResource, descToUse, heapProperties, initialState);
			DX12ResourceTrackerHookRuntime::AttachResourceReleaseTracker(d3dResource);
			d3dResource->Release();
		}
	}
	return hr;
}

HRESULT STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCreatePlacedResource(
	ID3D12Device *device, ID3D12Heap *heap, UINT64 heapOffset,
	const D3D12_RESOURCE_DESC *desc, D3D12_RESOURCE_STATES initialState,
	const D3D12_CLEAR_VALUE *optimizedClearValue, REFIID riid, void **resource)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device::CreatePlacedResource", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device::CreatePlacedResource, DX12ResourceTrackerHookState::OrigCreatePlacedResource, "ID3D12Device::CreatePlacedResource");
	if (!original)
		return E_FAIL;
	if (DX12InternalReplayState::IsActive())
		return original(device, heap, heapOffset, desc,
			initialState, optimizedClearValue, riid, resource);
	HRESULT hr = original(device, heap, heapOffset, desc,
		initialState, optimizedClearValue, riid, resource);
	if (SUCCEEDED(hr) && resource && *resource && DX12ResourceTrackerHookRuntime::ShouldRecordCreatedResource(desc)) {
		ID3D12Resource *d3dResource = nullptr;
		if (SUCCEEDED(static_cast<IUnknown*>(*resource)->QueryInterface(IID_PPV_ARGS(&d3dResource)))) {
			RecordResource(d3dResource, desc, nullptr, initialState);
			DX12ResourceTrackerHookRuntime::AttachResourceReleaseTracker(d3dResource);
			d3dResource->Release();
		}
	}
	return hr;
}

HRESULT STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCreateReservedResource(
	ID3D12Device *device, const D3D12_RESOURCE_DESC *desc,
	D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE *optimizedClearValue,
	REFIID riid, void **resource)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device::CreateReservedResource", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device::CreateReservedResource, DX12ResourceTrackerHookState::OrigCreateReservedResource, "ID3D12Device::CreateReservedResource");
	if (!original)
		return E_FAIL;
	if (DX12InternalReplayState::IsActive())
		return original(device, desc, initialState,
			optimizedClearValue, riid, resource);
	HRESULT hr = original(device, desc, initialState,
		optimizedClearValue, riid, resource);
	if (SUCCEEDED(hr) && resource && *resource && DX12ResourceTrackerHookRuntime::ShouldRecordCreatedResource(desc)) {
		ID3D12Resource *d3dResource = nullptr;
		if (SUCCEEDED(static_cast<IUnknown*>(*resource)->QueryInterface(IID_PPV_ARGS(&d3dResource)))) {
			RecordResource(d3dResource, desc, nullptr, initialState);
			DX12ResourceTrackerHookRuntime::AttachResourceReleaseTracker(d3dResource);
			d3dResource->Release();
		}
	}
	return hr;
}

HRESULT STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCreateCommittedResource1(
	ID3D12Device4 *device, const D3D12_HEAP_PROPERTIES *heapProperties,
	D3D12_HEAP_FLAGS heapFlags, const D3D12_RESOURCE_DESC *desc,
	D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE *optimizedClearValue,
	ID3D12ProtectedResourceSession *protectedSession, REFIID riid, void **resource)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device4::CreateCommittedResource1", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device4::CreateCommittedResource1, DX12ResourceTrackerHookState::OrigCreateCommittedResource1, "ID3D12Device4::CreateCommittedResource1");
	if (!original)
		return E_FAIL;
	if (DX12InternalReplayState::IsActive())
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
	if (SUCCEEDED(hr) && resource && *resource && DX12ResourceTrackerHookRuntime::ShouldRecordCreatedResource(descToUse)) {
		ID3D12Resource *d3dResource = nullptr;
		if (SUCCEEDED(static_cast<IUnknown*>(*resource)->QueryInterface(IID_PPV_ARGS(&d3dResource)))) {
			RecordResource(d3dResource, descToUse, heapProperties, initialState);
			DX12ResourceTrackerHookRuntime::AttachResourceReleaseTracker(d3dResource);
			d3dResource->Release();
		}
	}
	return hr;
}

HRESULT STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCreateReservedResource1(
	ID3D12Device4 *device, const D3D12_RESOURCE_DESC *desc,
	D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE *optimizedClearValue,
	ID3D12ProtectedResourceSession *protectedSession, REFIID riid, void **resource)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device4::CreateReservedResource1", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device4::CreateReservedResource1, DX12ResourceTrackerHookState::OrigCreateReservedResource1, "ID3D12Device4::CreateReservedResource1");
	if (!original)
		return E_FAIL;
	if (DX12InternalReplayState::IsActive())
		return original(device, desc, initialState,
			optimizedClearValue, protectedSession, riid, resource);
	HRESULT hr = original(device, desc, initialState,
		optimizedClearValue, protectedSession, riid, resource);
	if (SUCCEEDED(hr) && resource && *resource && DX12ResourceTrackerHookRuntime::ShouldRecordCreatedResource(desc)) {
		ID3D12Resource *d3dResource = nullptr;
		if (SUCCEEDED(static_cast<IUnknown*>(*resource)->QueryInterface(IID_PPV_ARGS(&d3dResource)))) {
			RecordResource(d3dResource, desc, nullptr, initialState);
			DX12ResourceTrackerHookRuntime::AttachResourceReleaseTracker(d3dResource);
			d3dResource->Release();
		}
	}
	return hr;
}

HRESULT STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCreateCommittedResource2(
	ID3D12Device8 *device, const D3D12_HEAP_PROPERTIES *heapProperties,
	D3D12_HEAP_FLAGS heapFlags, const D3D12_RESOURCE_DESC1 *desc,
	D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE *optimizedClearValue,
	ID3D12ProtectedResourceSession *protectedSession, REFIID riid, void **resource)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device8::CreateCommittedResource2", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device8::CreateCommittedResource2, DX12ResourceTrackerHookState::OrigCreateCommittedResource2, "ID3D12Device8::CreateCommittedResource2");
	if (!original)
		return E_FAIL;
	if (DX12InternalReplayState::IsActive())
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
	D3D12_RESOURCE_DESC desc0 = ResourceDescFromDesc1(descToUse);
	const D3D12_RESOURCE_DESC *recordDesc = descToUse ? &desc0 : nullptr;
	if (SUCCEEDED(hr) && resource && *resource && DX12ResourceTrackerHookRuntime::ShouldRecordCreatedResource(recordDesc)) {
		ID3D12Resource *d3dResource = nullptr;
		if (SUCCEEDED(static_cast<IUnknown*>(*resource)->QueryInterface(IID_PPV_ARGS(&d3dResource)))) {
			RecordResource(d3dResource, recordDesc, heapProperties, initialState);
			DX12ResourceTrackerHookRuntime::AttachResourceReleaseTracker(d3dResource);
			d3dResource->Release();
		}
	}
	return hr;
}

HRESULT STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCreatePlacedResource1(
	ID3D12Device8 *device, ID3D12Heap *heap, UINT64 heapOffset,
	const D3D12_RESOURCE_DESC1 *desc, D3D12_RESOURCE_STATES initialState,
	const D3D12_CLEAR_VALUE *optimizedClearValue, REFIID riid, void **resource)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device8::CreatePlacedResource1", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device8::CreatePlacedResource1, DX12ResourceTrackerHookState::OrigCreatePlacedResource1, "ID3D12Device8::CreatePlacedResource1");
	if (!original)
		return E_FAIL;
	if (DX12InternalReplayState::IsActive())
		return original(device, heap, heapOffset, desc,
			initialState, optimizedClearValue, riid, resource);
	HRESULT hr = original(device, heap, heapOffset, desc,
		initialState, optimizedClearValue, riid, resource);
	D3D12_RESOURCE_DESC desc0 = ResourceDescFromDesc1(desc);
	const D3D12_RESOURCE_DESC *recordDesc = desc ? &desc0 : nullptr;
	if (SUCCEEDED(hr) && resource && *resource && DX12ResourceTrackerHookRuntime::ShouldRecordCreatedResource(recordDesc)) {
		ID3D12Resource *d3dResource = nullptr;
		if (SUCCEEDED(static_cast<IUnknown*>(*resource)->QueryInterface(IID_PPV_ARGS(&d3dResource)))) {
			RecordResource(d3dResource, recordDesc, nullptr, initialState);
			DX12ResourceTrackerHookRuntime::AttachResourceReleaseTracker(d3dResource);
			d3dResource->Release();
		}
	}
	return hr;
}

HRESULT STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCreateCommittedResource3(
	ID3D12Device10 *device, const D3D12_HEAP_PROPERTIES *heapProperties,
	D3D12_HEAP_FLAGS heapFlags, const D3D12_RESOURCE_DESC1 *desc,
	D3D12_BARRIER_LAYOUT initialLayout, const D3D12_CLEAR_VALUE *optimizedClearValue,
	ID3D12ProtectedResourceSession *protectedSession, UINT32 numCastableFormats,
	const DXGI_FORMAT *castableFormats, REFIID riid, void **resource)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device10::CreateCommittedResource3", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device10::CreateCommittedResource3, DX12ResourceTrackerHookState::OrigCreateCommittedResource3, "ID3D12Device10::CreateCommittedResource3");
	if (!original)
		return E_FAIL;
	if (DX12InternalReplayState::IsActive())
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
	D3D12_RESOURCE_DESC desc0 = ResourceDescFromDesc1(descToUse);
	const D3D12_RESOURCE_DESC *recordDesc = descToUse ? &desc0 : nullptr;
	if (SUCCEEDED(hr) && resource && *resource && DX12ResourceTrackerHookRuntime::ShouldRecordCreatedResource(recordDesc)) {
		ID3D12Resource *d3dResource = nullptr;
		if (SUCCEEDED(static_cast<IUnknown*>(*resource)->QueryInterface(IID_PPV_ARGS(&d3dResource)))) {
			RecordResource(d3dResource, recordDesc, heapProperties, DX12ResourceStateTracker::FromBarrierLayout(initialLayout));
			DX12ResourceTrackerHookRuntime::AttachResourceReleaseTracker(d3dResource);
			d3dResource->Release();
		}
	}
	return hr;
}

HRESULT STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCreatePlacedResource2(
	ID3D12Device10 *device, ID3D12Heap *heap, UINT64 heapOffset,
	const D3D12_RESOURCE_DESC1 *desc, D3D12_BARRIER_LAYOUT initialLayout,
	const D3D12_CLEAR_VALUE *optimizedClearValue, UINT32 numCastableFormats,
	const DXGI_FORMAT *castableFormats, REFIID riid, void **resource)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device10::CreatePlacedResource2", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device10::CreatePlacedResource2, DX12ResourceTrackerHookState::OrigCreatePlacedResource2, "ID3D12Device10::CreatePlacedResource2");
	if (!original)
		return E_FAIL;
	if (DX12InternalReplayState::IsActive())
		return original(device, heap, heapOffset, desc,
			initialLayout, optimizedClearValue, numCastableFormats, castableFormats,
			riid, resource);
	HRESULT hr = original(device, heap, heapOffset, desc,
		initialLayout, optimizedClearValue, numCastableFormats, castableFormats,
		riid, resource);
	D3D12_RESOURCE_DESC desc0 = ResourceDescFromDesc1(desc);
	const D3D12_RESOURCE_DESC *recordDesc = desc ? &desc0 : nullptr;
	if (SUCCEEDED(hr) && resource && *resource && DX12ResourceTrackerHookRuntime::ShouldRecordCreatedResource(recordDesc)) {
		ID3D12Resource *d3dResource = nullptr;
		if (SUCCEEDED(static_cast<IUnknown*>(*resource)->QueryInterface(IID_PPV_ARGS(&d3dResource)))) {
			RecordResource(d3dResource, recordDesc, nullptr, DX12ResourceStateTracker::FromBarrierLayout(initialLayout));
			DX12ResourceTrackerHookRuntime::AttachResourceReleaseTracker(d3dResource);
			d3dResource->Release();
		}
	}
	return hr;
}

HRESULT STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCreateReservedResource2(
	ID3D12Device10 *device, const D3D12_RESOURCE_DESC *desc,
	D3D12_BARRIER_LAYOUT initialLayout, const D3D12_CLEAR_VALUE *optimizedClearValue,
	ID3D12ProtectedResourceSession *protectedSession, UINT32 numCastableFormats,
	const DXGI_FORMAT *castableFormats, REFIID riid, void **resource)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device10::CreateReservedResource2", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device10::CreateReservedResource2, DX12ResourceTrackerHookState::OrigCreateReservedResource2, "ID3D12Device10::CreateReservedResource2");
	if (!original)
		return E_FAIL;
	if (DX12InternalReplayState::IsActive())
		return original(device, desc, initialLayout,
			optimizedClearValue, protectedSession, numCastableFormats, castableFormats,
			riid, resource);
	HRESULT hr = original(device, desc, initialLayout,
		optimizedClearValue, protectedSession, numCastableFormats, castableFormats,
		riid, resource);
	if (SUCCEEDED(hr) && resource && *resource && DX12ResourceTrackerHookRuntime::ShouldRecordCreatedResource(desc)) {
		ID3D12Resource *d3dResource = nullptr;
		if (SUCCEEDED(static_cast<IUnknown*>(*resource)->QueryInterface(IID_PPV_ARGS(&d3dResource)))) {
			RecordResource(d3dResource, desc, nullptr, DX12ResourceStateTracker::FromBarrierLayout(initialLayout));
			DX12ResourceTrackerHookRuntime::AttachResourceReleaseTracker(d3dResource);
			d3dResource->Release();
		}
	}
	return hr;
}

void DX12ResourceTrackerHooks::Install(ID3D12Device *device)
{
	if (!device)
		return;

	if (!gCleanupRegistered) {
		atexit(CleanupTrackedResources);
		gCleanupRegistered = true;
		DX12LogManager::Text("Resource metadata tracking retains descriptor resources while Hunting\n");
	}

	DX12VTableHook deviceHooks[] = {
		{DX12ResourceTrackerHookSlotTypes::ID3D12Device::CreateDescriptorHeap, reinterpret_cast<void**>(&DX12ResourceTrackerHookState::OrigCreateDescriptorHeap),
			DX12ResourceTrackerHookCallbacks::HookedCreateDescriptorHeap, "ID3D12Device::CreateDescriptorHeap"},
		{DX12ResourceTrackerHookSlotTypes::ID3D12Device::CreateRootSignature, reinterpret_cast<void**>(&DX12ResourceTrackerHookState::OrigCreateRootSignature),
			DX12ResourceTrackerHookCallbacks::HookedCreateRootSignature, "ID3D12Device::CreateRootSignature"},
		{DX12ResourceTrackerHookSlotTypes::ID3D12Device::CreateConstantBufferView, reinterpret_cast<void**>(&DX12ResourceTrackerHookState::OrigCreateConstantBufferView),
			DX12ResourceTrackerHookCallbacks::HookedCreateConstantBufferView, "ID3D12Device::CreateConstantBufferView"},
		{DX12ResourceTrackerHookSlotTypes::ID3D12Device::CreateShaderResourceView, reinterpret_cast<void**>(&DX12ResourceTrackerHookState::OrigCreateShaderResourceView),
			DX12ResourceTrackerHookCallbacks::HookedCreateShaderResourceView, "ID3D12Device::CreateShaderResourceView"},
		{DX12ResourceTrackerHookSlotTypes::ID3D12Device::CreateUnorderedAccessView, reinterpret_cast<void**>(&DX12ResourceTrackerHookState::OrigCreateUnorderedAccessView),
			DX12ResourceTrackerHookCallbacks::HookedCreateUnorderedAccessView, "ID3D12Device::CreateUnorderedAccessView"},
		{DX12ResourceTrackerHookSlotTypes::ID3D12Device::CreateRenderTargetView, reinterpret_cast<void**>(&DX12ResourceTrackerHookState::OrigCreateRenderTargetView),
			DX12ResourceTrackerHookCallbacks::HookedCreateRenderTargetView, "ID3D12Device::CreateRenderTargetView"},
		{DX12ResourceTrackerHookSlotTypes::ID3D12Device::CreateDepthStencilView, reinterpret_cast<void**>(&DX12ResourceTrackerHookState::OrigCreateDepthStencilView),
			DX12ResourceTrackerHookCallbacks::HookedCreateDepthStencilView, "ID3D12Device::CreateDepthStencilView"},
		{DX12ResourceTrackerHookSlotTypes::ID3D12Device::CreateSampler, reinterpret_cast<void**>(&DX12ResourceTrackerHookState::OrigCreateSampler),
			DX12ResourceTrackerHookCallbacks::HookedCreateSampler, "ID3D12Device::CreateSampler"},
		{DX12ResourceTrackerHookSlotTypes::ID3D12Device::CopyDescriptors, reinterpret_cast<void**>(&DX12ResourceTrackerHookState::OrigCopyDescriptors),
			DX12ResourceTrackerHookCallbacks::HookedCopyDescriptors, "ID3D12Device::CopyDescriptors"},
		{DX12ResourceTrackerHookSlotTypes::ID3D12Device::CopyDescriptorsSimple, reinterpret_cast<void**>(&DX12ResourceTrackerHookState::OrigCopyDescriptorsSimple),
			DX12ResourceTrackerHookCallbacks::HookedCopyDescriptorsSimple, "ID3D12Device::CopyDescriptorsSimple"},
		{DX12ResourceTrackerHookSlotTypes::ID3D12Device::CreateCommittedResource, reinterpret_cast<void**>(&DX12ResourceTrackerHookState::OrigCreateCommittedResource),
			DX12ResourceTrackerHookCallbacks::HookedCreateCommittedResource, "ID3D12Device::CreateCommittedResource"},
		{DX12ResourceTrackerHookSlotTypes::ID3D12Device::CreatePlacedResource, reinterpret_cast<void**>(&DX12ResourceTrackerHookState::OrigCreatePlacedResource),
			DX12ResourceTrackerHookCallbacks::HookedCreatePlacedResource, "ID3D12Device::CreatePlacedResource"},
		{DX12ResourceTrackerHookSlotTypes::ID3D12Device::CreateReservedResource, reinterpret_cast<void**>(&DX12ResourceTrackerHookState::OrigCreateReservedResource),
			DX12ResourceTrackerHookCallbacks::HookedCreateReservedResource, "ID3D12Device::CreateReservedResource"},
	};
	DX12HookManager::InstallVTableHooks(device, deviceHooks);

	ID3D12Device4 *device4 = nullptr;
	if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&device4)))) {
		DX12VTableHook device4Hooks[] = {
			{DX12ResourceTrackerHookSlotTypes::ID3D12Device4::CreateCommittedResource1, reinterpret_cast<void**>(&DX12ResourceTrackerHookState::OrigCreateCommittedResource1),
				DX12ResourceTrackerHookCallbacks::HookedCreateCommittedResource1, "ID3D12Device4::CreateCommittedResource1"},
			{DX12ResourceTrackerHookSlotTypes::ID3D12Device4::CreateReservedResource1, reinterpret_cast<void**>(&DX12ResourceTrackerHookState::OrigCreateReservedResource1),
				DX12ResourceTrackerHookCallbacks::HookedCreateReservedResource1, "ID3D12Device4::CreateReservedResource1"},
		};
		DX12HookManager::InstallVTableHooks(device4, device4Hooks);
		device4->Release();
	}

	ID3D12Device8 *device8 = nullptr;
	if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&device8)))) {
		DX12VTableHook device8Hooks[] = {
			{DX12ResourceTrackerHookSlotTypes::ID3D12Device8::CreateCommittedResource2, reinterpret_cast<void**>(&DX12ResourceTrackerHookState::OrigCreateCommittedResource2),
				DX12ResourceTrackerHookCallbacks::HookedCreateCommittedResource2, "ID3D12Device8::CreateCommittedResource2"},
			{DX12ResourceTrackerHookSlotTypes::ID3D12Device8::CreatePlacedResource1, reinterpret_cast<void**>(&DX12ResourceTrackerHookState::OrigCreatePlacedResource1),
				DX12ResourceTrackerHookCallbacks::HookedCreatePlacedResource1, "ID3D12Device8::CreatePlacedResource1"},
		};
		DX12HookManager::InstallVTableHooks(device8, device8Hooks);
		device8->Release();
	}

	ID3D12Device10 *device10 = nullptr;
	if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&device10)))) {
		DX12VTableHook device10Hooks[] = {
			{DX12ResourceTrackerHookSlotTypes::ID3D12Device10::CreateCommittedResource3, reinterpret_cast<void**>(&DX12ResourceTrackerHookState::OrigCreateCommittedResource3),
				DX12ResourceTrackerHookCallbacks::HookedCreateCommittedResource3, "ID3D12Device10::CreateCommittedResource3"},
			{DX12ResourceTrackerHookSlotTypes::ID3D12Device10::CreatePlacedResource2, reinterpret_cast<void**>(&DX12ResourceTrackerHookState::OrigCreatePlacedResource2),
				DX12ResourceTrackerHookCallbacks::HookedCreatePlacedResource2, "ID3D12Device10::CreatePlacedResource2"},
			{DX12ResourceTrackerHookSlotTypes::ID3D12Device10::CreateReservedResource2, reinterpret_cast<void**>(&DX12ResourceTrackerHookState::OrigCreateReservedResource2),
				DX12ResourceTrackerHookCallbacks::HookedCreateReservedResource2, "ID3D12Device10::CreateReservedResource2"},
		};
		DX12HookManager::InstallVTableHooks(device10, device10Hooks);
		device10->Release();
	}
}
