// Command queue, command allocator, fence and descriptor heap wrappers.
// Split from DX12Wrapper.cpp; COM wrapper class definitions (inline).
#pragma once

#include "DX12WrapperCore.h"

namespace DX12WrapperDetail
{

	class CommandQueueWrapper final : public ID3D12CommandQueue, public WrapperBase
	{
	public:
		explicit CommandQueueWrapper(ID3D12CommandQueue *queue)
			: WrapperBase(queue)
		{
			mQueue = queue;
			mDesc = queue ? queue->GetDesc() : D3D12_COMMAND_QUEUE_DESC();
			RegisterWrapperPointer(static_cast<ID3D12CommandQueue*>(this));
			DX12CommandQueueState::Set(queue);
			DX12_LOG("DX12Wrapper",
				"kind=command_queue,stage=create,wrapper=%p,object=%p,type=%d",
				this, queue, static_cast<int>(mDesc.Type));
		}

		bool IsDirectQueue()
		{
			return mDesc.Type == D3D12_COMMAND_LIST_TYPE_DIRECT;
		}

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) override
		{
			if (!ppvObject)
				return E_POINTER;
			*ppvObject = nullptr;

			if (IsEqualIID(riid, __uuidof(IUnknown)) || IsSupportedWrapperIid(mQueue, riid, IsD3D12CommandQueueWrapperIid)) {
				*ppvObject = static_cast<ID3D12CommandQueue*>(this);
				AddRef();
				return S_OK;
			}

			HRESULT hr = mQueue->QueryInterface(riid, ppvObject);
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
			return mQueue->GetPrivateData(guid, pDataSize, pData);
		}

		HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT DataSize, const void *pData) override
		{
			return mQueue->SetPrivateData(guid, DataSize, pData);
		}

		HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown *pData) override
		{
			return mQueue->SetPrivateDataInterface(guid, pData);
		}

		HRESULT STDMETHODCALLTYPE SetName(LPCWSTR Name) override
		{
			return mQueue->SetName(Name);
		}

		HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void **ppvDevice) override
		{
			HRESULT hr = mQueue->GetDevice(riid, ppvDevice);
			if (SUCCEEDED(hr))
				DX12Wrapper::WrapD3D12DeviceResult(riid, ppvDevice);
			return hr;
		}

		void STDMETHODCALLTYPE UpdateTileMappings(
			ID3D12Resource *pResource, UINT NumResourceRegions,
			const D3D12_TILED_RESOURCE_COORDINATE *pResourceRegionStartCoordinates,
			const D3D12_TILE_REGION_SIZE *pResourceRegionSizes, ID3D12Heap *pHeap,
			UINT NumRanges, const D3D12_TILE_RANGE_FLAGS *pRangeFlags,
			const UINT *pHeapRangeStartOffsets, const UINT *pRangeTileCounts,
			D3D12_TILE_MAPPING_FLAGS Flags) override
		{
			mQueue->UpdateTileMappings(
				DX12WrapperRuntimeUtils::UnwrapResource(pResource), NumResourceRegions,
				pResourceRegionStartCoordinates, pResourceRegionSizes,
				DX12WrapperRuntimeUtils::UnwrapHeap(pHeap), NumRanges,
				pRangeFlags, pHeapRangeStartOffsets, pRangeTileCounts, Flags);
		}

		void STDMETHODCALLTYPE CopyTileMappings(
			ID3D12Resource *pDstResource, const D3D12_TILED_RESOURCE_COORDINATE *pDstRegionStartCoordinate,
			ID3D12Resource *pSrcResource, const D3D12_TILED_RESOURCE_COORDINATE *pSrcRegionStartCoordinate,
			const D3D12_TILE_REGION_SIZE *pRegionSize, D3D12_TILE_MAPPING_FLAGS Flags) override
		{
			mQueue->CopyTileMappings(
				DX12WrapperRuntimeUtils::UnwrapResource(pDstResource), pDstRegionStartCoordinate,
				DX12WrapperRuntimeUtils::UnwrapResource(pSrcResource), pSrcRegionStartCoordinate,
				pRegionSize, Flags);
		}

		void STDMETHODCALLTYPE ExecuteCommandLists(UINT NumCommandLists, ID3D12CommandList *const *ppCommandLists) override
		{
			if (IsDirectQueue()) {
				DX12WrapperRuntimeUtils::ExecuteCommandLists(
					mQueue, NumCommandLists, ppCommandLists, nullptr);
				return;
			}

			static volatile LONG nonDirectExecuteLogs = 0;
			if (InterlockedIncrement(&nonDirectExecuteLogs) <= 8)
				DX12_LOG("DX12ExecuteCommandLists",
					"stage=non_direct_passthrough,queue=%p,type=%d,count=%u",
					mQueue, static_cast<int>(mDesc.Type), NumCommandLists);
			DX12WrapperRuntimeUtils::ExecuteCommandListsDirectNative(mQueue, NumCommandLists, ppCommandLists);
		}

		void STDMETHODCALLTYPE SetMarker(UINT Metadata, const void *pData, UINT Size) override
		{
			mQueue->SetMarker(Metadata, pData, Size);
		}

		void STDMETHODCALLTYPE BeginEvent(UINT Metadata, const void *pData, UINT Size) override
		{
			mQueue->BeginEvent(Metadata, pData, Size);
		}

		void STDMETHODCALLTYPE EndEvent() override
		{
			mQueue->EndEvent();
		}

		HRESULT STDMETHODCALLTYPE Signal(ID3D12Fence *pFence, UINT64 Value) override
		{
			return DX12WrapperRuntimeUtils::QueueSignal(mQueue, pFence, Value, nullptr);
		}

		HRESULT STDMETHODCALLTYPE Wait(ID3D12Fence *pFence, UINT64 Value) override
		{
			return DX12WrapperRuntimeUtils::QueueWait(mQueue, pFence, Value, nullptr);
		}

		HRESULT STDMETHODCALLTYPE GetTimestampFrequency(UINT64 *pFrequency) override
		{
			return mQueue->GetTimestampFrequency(pFrequency);
		}

		HRESULT STDMETHODCALLTYPE GetClockCalibration(UINT64 *pGpuTimestamp, UINT64 *pCpuTimestamp) override
		{
			return mQueue->GetClockCalibration(pGpuTimestamp, pCpuTimestamp);
		}

		D3D12_COMMAND_QUEUE_DESC STDMETHODCALLTYPE GetDesc() override
		{
			return mQueue->GetDesc();
		}

	public:
		std::atomic<long> mRefCount{1};
		ID3D12CommandQueue *mQueue = nullptr;
		D3D12_COMMAND_QUEUE_DESC mDesc = {};
	};

	class CommandAllocatorWrapper final : public ID3D12CommandAllocator, public WrapperBase
	{
	public:
		explicit CommandAllocatorWrapper(ID3D12CommandAllocator *allocator)
			: WrapperBase(allocator)
		{
			mAllocator = allocator;
			RegisterWrapperPointer(static_cast<ID3D12Object*>(this));
			RegisterWrapperPointer(static_cast<ID3D12DeviceChild*>(this));
			RegisterWrapperPointer(static_cast<ID3D12Pageable*>(this));
			RegisterWrapperPointer(static_cast<ID3D12CommandAllocator*>(this));
		}

		void *GetCommandAllocatorWrapperInterface(REFIID riid)
		{
			if (IsEqualIID(riid, __uuidof(IUnknown)) || IsEqualIID(riid, __uuidof(ID3D12CommandAllocator)))
				return static_cast<ID3D12CommandAllocator*>(this);
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
			if (IsEqualIID(riid, __uuidof(IUnknown)) || IsSupportedWrapperIid(mAllocator, riid, IsD3D12CommandAllocatorWrapperIid)) {
				*ppvObject = GetCommandAllocatorWrapperInterface(riid);
				if (*ppvObject) {
					AddRef();
					return S_OK;
				}
			}
			HRESULT hr = mAllocator->QueryInterface(riid, ppvObject);
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
			return mAllocator->GetPrivateData(guid, pDataSize, pData);
		}

		HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT DataSize, const void *pData) override
		{
			return mAllocator->SetPrivateData(guid, DataSize, pData);
		}

		HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown *pData) override
		{
			return mAllocator->SetPrivateDataInterface(guid, pData);
		}

		HRESULT STDMETHODCALLTYPE SetName(LPCWSTR Name) override
		{
			return mAllocator->SetName(Name);
		}

		HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void **ppvDevice) override
		{
			HRESULT hr = mAllocator->GetDevice(riid, ppvDevice);
			if (SUCCEEDED(hr))
				DX12Wrapper::WrapD3D12DeviceResult(riid, ppvDevice);
			return hr;
		}

		HRESULT STDMETHODCALLTYPE Reset() override
		{
			return mAllocator->Reset();
		}

	public:
		std::atomic<long> mRefCount{1};
		ID3D12CommandAllocator *mAllocator = nullptr;
	};

	class FenceWrapper final : public ID3D12Fence1, public WrapperBase
	{
	public:
		explicit FenceWrapper(ID3D12Fence *fence)
			: WrapperBase(fence)
		{
			mFence = fence;
			RegisterWrapperPointer(static_cast<ID3D12Object*>(this));
			RegisterWrapperPointer(static_cast<ID3D12DeviceChild*>(this));
			RegisterWrapperPointer(static_cast<ID3D12Pageable*>(this));
			RegisterWrapperPointer(static_cast<ID3D12Fence*>(this));
			RegisterWrapperPointer(static_cast<ID3D12Fence1*>(this));
		}

		HRESULT QueryFence1(ID3D12Fence1 **fence)
		{
			return mFence->QueryInterface(IID_PPV_ARGS(fence));
		}

		void *GetFenceWrapperInterface(REFIID riid)
		{
			if (IsEqualIID(riid, __uuidof(IUnknown)) || IsEqualIID(riid, __uuidof(ID3D12Fence1)))
				return static_cast<ID3D12Fence1*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12Fence)))
				return static_cast<ID3D12Fence*>(this);
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
			if (IsEqualIID(riid, __uuidof(IUnknown)) || IsSupportedWrapperIid(mFence, riid, IsD3D12FenceWrapperIid)) {
				*ppvObject = GetFenceWrapperInterface(riid);
				if (*ppvObject) {
					AddRef();
					return S_OK;
				}
			}
			HRESULT hr = mFence->QueryInterface(riid, ppvObject);
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
			return mFence->GetPrivateData(guid, pDataSize, pData);
		}

		HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT DataSize, const void *pData) override
		{
			return mFence->SetPrivateData(guid, DataSize, pData);
		}

		HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown *pData) override
		{
			return mFence->SetPrivateDataInterface(guid, pData);
		}

		HRESULT STDMETHODCALLTYPE SetName(LPCWSTR Name) override
		{
			return mFence->SetName(Name);
		}

		HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void **ppvDevice) override
		{
			HRESULT hr = mFence->GetDevice(riid, ppvDevice);
			if (SUCCEEDED(hr))
				DX12Wrapper::WrapD3D12DeviceResult(riid, ppvDevice);
			return hr;
		}

		UINT64 STDMETHODCALLTYPE GetCompletedValue() override
		{
			DX12_DIAG_QPC(begin);
			UINT64 completed = mFence->GetCompletedValue();
			DX12_PROF_ADD_US("Fence.op", DX12_DIAG_ELAPSED_US(begin));
			return completed;
		}

		HRESULT STDMETHODCALLTYPE SetEventOnCompletion(UINT64 Value, HANDLE hEvent) override
		{
			DX12_DIAG_FENCE_VALUE_VAR(completedBefore, mFence);
			DX12_DIAG_QPC(begin);
			HRESULT hr = mFence->SetEventOnCompletion(Value, hEvent);
			DX12_DIAG_FENCE_VALUE_VAR(completedAfter, mFence);
			DX12_DIAG_REGISTER_FENCE_EVENT(
				mFence,
				hEvent,
				Value,
				completedBefore,
				completedAfter,
				hr);
			DX12_PROF_ADD_US("Fence.op", DX12_DIAG_ELAPSED_US(begin));
			return hr;
		}

		HRESULT STDMETHODCALLTYPE Signal(UINT64 Value) override
		{
			DX12_DIAG_FENCE_VALUE_VAR(completedBefore, mFence);
			DX12_DIAG_QPC(begin);
			HRESULT hr = mFence->Signal(Value);
			DX12_DIAG_FENCE_VALUE_VAR(completedAfter, mFence);
			DX12_PROF_ADD_US("Fence.op", DX12_DIAG_ELAPSED_US(begin));
			return hr;
		}

		D3D12_FENCE_FLAGS STDMETHODCALLTYPE GetCreationFlags() override
		{
			ID3D12Fence1 *fence1 = nullptr;
			if (FAILED(QueryFence1(&fence1)))
				return D3D12_FENCE_FLAG_NONE;
			D3D12_FENCE_FLAGS flags = fence1->GetCreationFlags();
			fence1->Release();
			return flags;
		}

	public:
		std::atomic<long> mRefCount{1};
		ID3D12Fence *mFence = nullptr;
	};

	class DescriptorHeapWrapper final : public ID3D12DescriptorHeap, public WrapperBase
	{
	public:
		explicit DescriptorHeapWrapper(ID3D12DescriptorHeap *heap)
			: WrapperBase(heap)
		{
			mHeap = heap;
			// Descriptor heap metadata and start handles are immutable after
			// creation. Cache them so SetDescriptorHeaps and descriptor-copy
			// paths do not cross the wrapper/native COM boundary repeatedly.
			if (mHeap) {
				mDesc = mHeap->GetDesc();
				mCpuStart = mHeap->GetCPUDescriptorHandleForHeapStart();
				if ((mDesc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) != 0)
					mGpuStart = mHeap->GetGPUDescriptorHandleForHeapStart();
			}
			RegisterWrapperPointer(static_cast<ID3D12Object*>(this));
			RegisterWrapperPointer(static_cast<ID3D12DeviceChild*>(this));
			RegisterWrapperPointer(static_cast<ID3D12Pageable*>(this));
			RegisterWrapperPointer(static_cast<ID3D12DescriptorHeap*>(this));
		}

		void *GetDescriptorHeapWrapperInterface(REFIID riid)
		{
			if (IsEqualIID(riid, __uuidof(IUnknown)) || IsEqualIID(riid, __uuidof(ID3D12DescriptorHeap)))
				return static_cast<ID3D12DescriptorHeap*>(this);
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
			if (IsEqualIID(riid, __uuidof(IUnknown)) || IsSupportedWrapperIid(mHeap, riid, IsD3D12DescriptorHeapWrapperIid)) {
				*ppvObject = GetDescriptorHeapWrapperInterface(riid);
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

		D3D12_DESCRIPTOR_HEAP_DESC STDMETHODCALLTYPE GetDesc() override
		{
			return mDesc;
		}

		D3D12_CPU_DESCRIPTOR_HANDLE STDMETHODCALLTYPE GetCPUDescriptorHandleForHeapStart() override
		{
			return mCpuStart;
		}

		D3D12_GPU_DESCRIPTOR_HANDLE STDMETHODCALLTYPE GetGPUDescriptorHandleForHeapStart() override
		{
			return mGpuStart;
		}

	public:
		std::atomic<long> mRefCount{1};
		ID3D12DescriptorHeap *mHeap = nullptr;
		D3D12_DESCRIPTOR_HEAP_DESC mDesc = {};
		D3D12_CPU_DESCRIPTOR_HANDLE mCpuStart = {};
		D3D12_GPU_DESCRIPTOR_HANDLE mGpuStart = {};
	};


} // namespace DX12WrapperDetail
