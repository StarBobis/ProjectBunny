// Resource and heap wrappers (incl. IA hash cache entry).
// Split from DX12Wrapper.cpp; COM wrapper class definitions (inline).
#pragma once

#include "DX12WrapperCore.h"

namespace DX12WrapperDetail
{

	class ResourceWrapper final : public ID3D12Resource2, public WrapperBase
	{
	public:
		explicit ResourceWrapper(ID3D12Resource *resource)
			: WrapperBase(resource)
		{
			mResource = resource;
			RegisterWrapperPointer(static_cast<ID3D12Object*>(this));
			RegisterWrapperPointer(static_cast<ID3D12DeviceChild*>(this));
			RegisterWrapperPointer(static_cast<ID3D12Pageable*>(this));
			RegisterWrapperPointer(static_cast<ID3D12Resource*>(this));
			RegisterWrapperPointer(static_cast<ID3D12Resource1*>(this));
			RegisterWrapperPointer(static_cast<ID3D12Resource2*>(this));

			// Cache the resource record eagerly so GPU VA lookups can
			// resolve through the sorted gGpuVaWrappers vector instead of
			// the pointer-chasing std::map gBufferResourceByGpuVa.
			mRecord.resource = mResource;
			mRecord.desc = mResource->GetDesc();
			if (mRecord.desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
				mRecord.gpuVirtualAddress =
					mResource->GetGPUVirtualAddress();
				mRecord.size = mRecord.desc.Width;
				mRecord.gpuVirtualAddressRealEnd =
					mRecord.gpuVirtualAddress + mRecord.size;
				mRecord.gpuVirtualAddressOobEnd =
					mRecord.gpuVirtualAddressRealEnd;
				if (mRecord.gpuVirtualAddress && mRecord.size) {
					RegisterGpuVaWrapper(
						mRecord.gpuVirtualAddress,
						static_cast<void*>(this),
						&mRecord);
					mVaRegistered = true;
				}
			}
		}

		~ResourceWrapper()
		{
			if (mVaRegistered) {
				UnregisterGpuVaWrapper(
					mRecord.gpuVirtualAddress,
					static_cast<void*>(this));
				mVaRegistered = false;
			}
		}

		HRESULT QueryResource1(ID3D12Resource1 **resource)
		{
			return mResource->QueryInterface(IID_PPV_ARGS(resource));
		}

		HRESULT QueryResource2(ID3D12Resource2 **resource)
		{
			return mResource->QueryInterface(IID_PPV_ARGS(resource));
		}

		void *GetResourceWrapperInterface(REFIID riid)
		{
			if (IsEqualIID(riid, __uuidof(IUnknown)) || IsEqualIID(riid, __uuidof(ID3D12Resource2)))
				return static_cast<ID3D12Resource2*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12Resource1)))
				return static_cast<ID3D12Resource1*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12Resource)))
				return static_cast<ID3D12Resource*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12Pageable)))
				return static_cast<ID3D12Pageable*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12DeviceChild)))
				return static_cast<ID3D12DeviceChild*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12Object)))
				return static_cast<ID3D12Object*>(this);
			return nullptr;
		}

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) override
		{
			if (!ppvObject)
				return E_POINTER;
			*ppvObject = nullptr;
			if (IsEqualIID(riid, __uuidof(IUnknown)) || IsSupportedWrapperIid(mResource, riid, IsD3D12ResourceWrapperIid)) {
				*ppvObject = GetResourceWrapperInterface(riid);
				if (*ppvObject) {
					AddRef();
					return S_OK;
				}
			}
			HRESULT hr = mResource->QueryInterface(riid, ppvObject);
			if (SUCCEEDED(hr))
				TryWrapKnownD3D12Result(riid, ppvObject);
			return hr;
		}

		ULONG STDMETHODCALLTYPE AddRef() override
		{
			return static_cast<ULONG>(++mRefCount);
		}

		ULONG STDMETHODCALLTYPE Release() override
		{
			return ReleaseWithRegistry(mRefCount);
		}

		HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid, UINT *pDataSize, void *pData) override
		{
			return mResource->GetPrivateData(guid, pDataSize, pData);
		}

		HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT DataSize, const void *pData) override
		{
			return mResource->SetPrivateData(guid, DataSize, pData);
		}

		HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown *pData) override
		{
			return mResource->SetPrivateDataInterface(guid, pData);
		}

		HRESULT STDMETHODCALLTYPE SetName(LPCWSTR Name) override
		{
			return mResource->SetName(Name);
		}

		HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void **ppvDevice) override
		{
			HRESULT hr = mResource->GetDevice(riid, ppvDevice);
			if (SUCCEEDED(hr))
				DX12Wrapper::WrapD3D12DeviceResult(riid, ppvDevice);
			return hr;
		}

		HRESULT STDMETHODCALLTYPE Map(UINT Subresource, const D3D12_RANGE *pReadRange, void **ppData) override
		{
			DX12_CREATE_TIMING_SCOPE(ResourceMapWrapper, Original);
			// Route through the tracker so buffer content capture keeps
			// working in wrapper mode (no inline Map/Unmap code patch).
			return DX12ResourceTrackerHookRuntime::WrappedResourceMap(
				mResource, Subresource, pReadRange, ppData);
		}

		void STDMETHODCALLTYPE Unmap(UINT Subresource, const D3D12_RANGE *pWrittenRange) override
		{
			DX12_CREATE_TIMING_SCOPE(ResourceUnmapWrapper, Original);
			DX12ResourceTrackerHookRuntime::WrappedResourceUnmap(
				mResource, Subresource, pWrittenRange, _ReturnAddress());
		}

		D3D12_RESOURCE_DESC STDMETHODCALLTYPE GetDesc() override
		{
			DX12_CREATE_TIMING_SCOPE(ResourceQueryWrapper, Original);
			return mRecord.desc;
		}

		D3D12_GPU_VIRTUAL_ADDRESS STDMETHODCALLTYPE GetGPUVirtualAddress() override
		{
			DX12_CREATE_TIMING_SCOPE(ResourceQueryWrapper, Original);
			return mRecord.gpuVirtualAddress;
		}

		HRESULT STDMETHODCALLTYPE WriteToSubresource(
			UINT DstSubresource, const D3D12_BOX *pDstBox, const void *pSrcData,
			UINT SrcRowPitch, UINT SrcDepthPitch) override
		{
			DX12_CREATE_TIMING_SCOPE(ResourceReadWriteWrapper, Original);
			HRESULT hr = mResource->WriteToSubresource(
				DstSubresource, pDstBox, pSrcData, SrcRowPitch, SrcDepthPitch);
			if (SUCCEEDED(hr) && pSrcData) {
				const D3D12_RESOURCE_DESC desc = mRecord.desc;
				if (desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER &&
				    DstSubresource == 0) {
					UINT64 offset = 0;
					UINT64 bytes = desc.Width;
					if (pDstBox) {
						offset = pDstBox->left;
						bytes = pDstBox->right > pDstBox->left ?
							static_cast<UINT64>(pDstBox->right - pDstBox->left) : 0;
					}
					if (bytes != 0) {
						DX12CacheBufferContentRange(
							mResource,
							offset,
							pSrcData,
							static_cast<size_t>(bytes));
					} else {
						DX12InvalidateBufferContent(mResource);
					}
				}
			}
			return hr;
		}

		HRESULT STDMETHODCALLTYPE ReadFromSubresource(
			void *pDstData, UINT DstRowPitch, UINT DstDepthPitch, UINT SrcSubresource,
			const D3D12_BOX *pSrcBox) override
		{
			DX12_CREATE_TIMING_SCOPE(ResourceReadWriteWrapper, Original);
			return mResource->ReadFromSubresource(
				pDstData, DstRowPitch, DstDepthPitch, SrcSubresource, pSrcBox);
		}

		HRESULT STDMETHODCALLTYPE GetHeapProperties(D3D12_HEAP_PROPERTIES *pHeapProperties, D3D12_HEAP_FLAGS *pHeapFlags) override
		{
			DX12_CREATE_TIMING_SCOPE(ResourceQueryWrapper, Original);
			return mResource->GetHeapProperties(pHeapProperties, pHeapFlags);
		}

		HRESULT STDMETHODCALLTYPE GetProtectedResourceSession(REFIID riid, void **ppProtectedSession) override
		{
			DX12_CREATE_TIMING_SCOPE(ResourceQueryWrapper, Original);
			ID3D12Resource1 *resource1 = nullptr;
			HRESULT hr = QueryResource1(&resource1);
			if (FAILED(hr))
				return hr;
			hr = resource1->GetProtectedResourceSession(riid, ppProtectedSession);
			if (SUCCEEDED(hr) && ppProtectedSession && *ppProtectedSession)
				DX12Wrapper::WrapD3D12DeviceResult(riid, ppProtectedSession);
			resource1->Release();
			return hr;
		}

		D3D12_RESOURCE_DESC1 STDMETHODCALLTYPE GetDesc1() override
		{
			DX12_CREATE_TIMING_SCOPE(ResourceQueryWrapper, Original);
			ID3D12Resource2 *resource2 = nullptr;
			if (FAILED(QueryResource2(&resource2)))
				return D3D12_RESOURCE_DESC1();
			D3D12_RESOURCE_DESC1 desc = resource2->GetDesc1();
			resource2->Release();
			return desc;
		}

	public:
		std::atomic<long> mRefCount{1};
		ID3D12Resource *mResource = nullptr;
		ResourceRecord mRecord = {};
		bool mVaRegistered = false;

		// Small IA-hash reuse cache.  IA region hashes are expensive to
		// rebuild (content hash + resolve), and per-command-list slot caches
		// are invalidated whenever a different view is bound to the slot.
		// Games that rotate a small set of vertex buffers across slots and
		// command lists hit this cache instead of recomputing.
		struct IaHashCacheEntry {
			UINT64 gpuVa = 0;      // view.BufferLocation
			UINT64 size = 0;       // view.SizeInBytes
			UINT stride = 0;       // view.StrideInBytes
			UINT slot = UINT_MAX;  // bound slot (part of hash input)
			UINT64 contentGeneration = 0;
			UINT64 resolveGeneration = 0;
			uint32_t hash = 0;
			bool valid = false;
		};
		std::array<IaHashCacheEntry, 8> mIaHashCache = {};

		ResourceRecord *GetResourceRecord() override { return &mRecord; }
	};

	class HeapWrapper final : public ID3D12Heap, public WrapperBase
	{
	public:
		explicit HeapWrapper(ID3D12Heap *heap)
			: WrapperBase(heap)
		{
			mHeap = heap;
			RegisterWrapperPointer(static_cast<ID3D12Object*>(this));
			RegisterWrapperPointer(static_cast<ID3D12DeviceChild*>(this));
			RegisterWrapperPointer(static_cast<ID3D12Pageable*>(this));
			RegisterWrapperPointer(static_cast<ID3D12Heap*>(this));
			DX12LogManager::Fields("DX12Wrapper",
				"kind=heap,stage=create,wrapper=%p,object=%p",
				this, heap);
		}

		void *GetHeapWrapperInterface(REFIID riid)
		{
			if (IsEqualIID(riid, __uuidof(IUnknown)) || IsEqualIID(riid, __uuidof(ID3D12Heap)))
				return static_cast<ID3D12Heap*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12Pageable)))
				return static_cast<ID3D12Pageable*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12DeviceChild)))
				return static_cast<ID3D12DeviceChild*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12Object)))
				return static_cast<ID3D12Object*>(this);
			return nullptr;
		}

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) override
		{
			if (!ppvObject)
				return E_POINTER;
			*ppvObject = nullptr;
			if (IsEqualIID(riid, __uuidof(IUnknown)) || IsSupportedWrapperIid(mHeap, riid, IsD3D12HeapWrapperIid)) {
				*ppvObject = GetHeapWrapperInterface(riid);
				if (*ppvObject) {
					AddRef();
					return S_OK;
				}
			}
			HRESULT hr = mHeap->QueryInterface(riid, ppvObject);
			if (SUCCEEDED(hr))
				TryWrapKnownD3D12Result(riid, ppvObject);
			return hr;
		}

		ULONG STDMETHODCALLTYPE AddRef() override
		{
			return static_cast<ULONG>(++mRefCount);
		}

		ULONG STDMETHODCALLTYPE Release() override
		{
			return ReleaseWithRegistry(mRefCount);
		}

		HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid, UINT *pDataSize, void *pData) override
		{
			return mHeap->GetPrivateData(guid, pDataSize, pData);
		}

		HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT DataSize, const void *pData) override
		{
			return mHeap->SetPrivateData(guid, DataSize, pData);
		}

		HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown *pData) override
		{
			return mHeap->SetPrivateDataInterface(guid, pData);
		}

		HRESULT STDMETHODCALLTYPE SetName(LPCWSTR Name) override
		{
			return mHeap->SetName(Name);
		}

		HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void **ppvDevice) override
		{
			HRESULT hr = mHeap->GetDevice(riid, ppvDevice);
			if (SUCCEEDED(hr))
				DX12Wrapper::WrapD3D12DeviceResult(riid, ppvDevice);
			return hr;
		}

		D3D12_HEAP_DESC STDMETHODCALLTYPE GetDesc() override
		{
			return mHeap->GetDesc();
		}

	public:
		std::atomic<long> mRefCount{1};
		ID3D12Heap *mHeap = nullptr;
	};


} // namespace DX12WrapperDetail
