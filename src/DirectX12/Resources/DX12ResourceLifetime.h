// Resource/heap release trackers and object lifetime utilities.
// Split from DX12Resources.h; keep declarations in the layer that owns them.
#pragma once

#include "../DX12Runtime.h"
#include "../Config/DX12ConfigLoader.h"
#include "DX12ResourceDump.h"

class DX12DescriptorHeapReleaseTracker : public IUnknown
{
public:
	explicit DX12DescriptorHeapReleaseTracker(ID3D12DescriptorHeap *heap) : mRef(1), mHeap(heap) {}

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
			if (mInstalled) {
				DX12_CREATE_TIMING_SCOPE(DescriptorHeapInvalidate, Record);
				AcquireSRWLockExclusive(&gDescriptorLock);
				InvalidateDescriptorHeapLocked(mHeap);
				ReleaseSRWLockExclusive(&gDescriptorLock);
			}
			delete this;
		}
		return static_cast<ULONG>(ref);
	}

	volatile LONG mRef;
	ID3D12DescriptorHeap *mHeap;
	bool mInstalled = false;
};

class DX12ResourceReleaseTracker : public IUnknown
{
public:
	explicit DX12ResourceReleaseTracker(ID3D12Resource *resource) : mRef(1), mResource(resource) {}

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
			if (mInstalled) {
				DX12_CREATE_TIMING_SCOPE(ResourceInvalidate, Record);
				AcquireSRWLockExclusive(&gResourceLock);
				InvalidateResourceLocked(mResource);
				ReleaseSRWLockExclusive(&gResourceLock);
			}
			delete this;
		}
		return static_cast<ULONG>(ref);
	}

	volatile LONG mRef;
	ID3D12Resource *mResource;
	bool mInstalled = false;
};


class DX12ResourceTrackerPrivateDataGuids
{
public:
	inline static const GUID DescriptorHeapReleaseTracker =
	{0x28c66065, 0x55b7, 0x46df, {0x9e, 0x1e, 0x08, 0x52, 0x8d, 0x31, 0xcb, 0x1a}};
	inline static const GUID ResourceReleaseTracker =
	{0xe1a4bb6a, 0xf6fc, 0x4a80, {0x90, 0xa4, 0xf5, 0xb9, 0x13, 0x32, 0xf1, 0x5c}};
};


class DX12ObjectLifetimeTrackerUtils
{
public:
	static HRESULT InstallResourceReleaseTracker(ID3D12Resource *resource)
	{
		if (!resource)
			return E_POINTER;

		DX12ResourceReleaseTracker *tracker =
			new (std::nothrow) DX12ResourceReleaseTracker(resource);
		if (!tracker)
			return E_OUTOFMEMORY;

		const HRESULT hr = resource->SetPrivateDataInterface(
			DX12ResourceTrackerPrivateDataGuids::ResourceReleaseTracker,
			tracker);
		if (SUCCEEDED(hr))
			tracker->mInstalled = true;
		tracker->Release();

		DX12LogManager::Fields("DX12ObjectLifetimeTracker",
			"kind=resource,stage=install,resource=%p,hr=0x%lx",
			resource, hr);
		return hr;
	}

	static HRESULT InstallDescriptorHeapReleaseTracker(ID3D12DescriptorHeap *heap)
	{
		if (!heap)
			return E_POINTER;

		DX12DescriptorHeapReleaseTracker *tracker =
			new (std::nothrow) DX12DescriptorHeapReleaseTracker(heap);
		if (!tracker)
			return E_OUTOFMEMORY;

		const HRESULT hr = heap->SetPrivateDataInterface(
			DX12ResourceTrackerPrivateDataGuids::DescriptorHeapReleaseTracker,
			tracker);
		if (SUCCEEDED(hr))
			tracker->mInstalled = true;
		tracker->Release();

		DX12LogManager::Fields("DX12ObjectLifetimeTracker",
			"kind=descriptor_heap,stage=install,heap=%p,hr=0x%lx",
			heap, hr);
		return hr;
	}
};


