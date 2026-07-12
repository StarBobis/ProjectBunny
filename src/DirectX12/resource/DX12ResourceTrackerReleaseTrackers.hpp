#pragma once

#include "DX12ResourceTrackerPrivate.hpp"
#include "utils/DX12CreateTimingTelemetryUtils.hpp"

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
