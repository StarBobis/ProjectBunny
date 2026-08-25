// Pipeline state, root signature, query heap, command signature, lifetime/protected session, meta command, state object, pipeline library and shader cache session wrappers.
// Split from DX12Wrapper.cpp; COM wrapper class definitions (inline).
#pragma once

#include "DX12WrapperCore.h"

namespace DX12WrapperDetail
{

	class PipelineStateWrapper final : public ID3D12PipelineState, public WrapperBase
	{
	public:
		explicit PipelineStateWrapper(ID3D12PipelineState *pipelineState)
			: WrapperBase(pipelineState)
		{
			mPipelineState = pipelineState;
			RegisterWrapperPointer(static_cast<ID3D12Object*>(this));
			RegisterWrapperPointer(static_cast<ID3D12DeviceChild*>(this));
			RegisterWrapperPointer(static_cast<ID3D12Pageable*>(this));
			RegisterWrapperPointer(static_cast<ID3D12PipelineState*>(this));
			DX12LogManager::Fields("DX12Wrapper",
				"kind=pipeline_state,stage=create,wrapper=%p,object=%p",
				this, pipelineState);
		}

		void *GetPipelineStateWrapperInterface(REFIID riid)
		{
			if (IsEqualIID(riid, __uuidof(IUnknown)) || IsEqualIID(riid, __uuidof(ID3D12PipelineState)))
				return static_cast<ID3D12PipelineState*>(this);
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
			if (IsEqualIID(riid, __uuidof(IUnknown)) ||
				IsSupportedWrapperIid(mPipelineState, riid, IsD3D12PipelineStateWrapperIid)) {
				*ppvObject = GetPipelineStateWrapperInterface(riid);
				if (*ppvObject) {
					AddRef();
					return S_OK;
				}
			}
			HRESULT hr = mPipelineState->QueryInterface(riid, ppvObject);
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
			return mPipelineState->GetPrivateData(guid, pDataSize, pData);
		}

		HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT DataSize, const void *pData) override
		{
			return mPipelineState->SetPrivateData(guid, DataSize, pData);
		}

		HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown *pData) override
		{
			return mPipelineState->SetPrivateDataInterface(guid, pData);
		}

		HRESULT STDMETHODCALLTYPE SetName(LPCWSTR Name) override
		{
			return mPipelineState->SetName(Name);
		}

		HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void **ppvDevice) override
		{
			HRESULT hr = mPipelineState->GetDevice(riid, ppvDevice);
			if (SUCCEEDED(hr))
				DX12Wrapper::WrapD3D12DeviceResult(riid, ppvDevice);
			return hr;
		}

		HRESULT STDMETHODCALLTYPE GetCachedBlob(ID3DBlob **ppBlob) override
		{
			return mPipelineState->GetCachedBlob(ppBlob);
		}

	public:
		std::atomic<long> mRefCount{1};
		ID3D12PipelineState *mPipelineState = nullptr;
	};

	class RootSignatureWrapper final : public ID3D12RootSignature, public WrapperBase
	{
	public:
		explicit RootSignatureWrapper(ID3D12RootSignature *rootSignature)
			: WrapperBase(rootSignature)
		{
			mRootSignature = rootSignature;
			RegisterWrapperPointer(static_cast<ID3D12Object*>(this));
			RegisterWrapperPointer(static_cast<ID3D12DeviceChild*>(this));
			RegisterWrapperPointer(static_cast<ID3D12RootSignature*>(this));
			DX12LogManager::Fields("DX12Wrapper",
				"kind=root_signature,stage=create,wrapper=%p,object=%p",
				this, rootSignature);
		}

		void *GetRootSignatureWrapperInterface(REFIID riid)
		{
			if (IsEqualIID(riid, __uuidof(IUnknown)) || IsEqualIID(riid, __uuidof(ID3D12RootSignature)))
				return static_cast<ID3D12RootSignature*>(this);
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
			if (IsEqualIID(riid, __uuidof(IUnknown)) ||
				IsSupportedWrapperIid(mRootSignature, riid, IsD3D12RootSignatureWrapperIid)) {
				*ppvObject = GetRootSignatureWrapperInterface(riid);
				if (*ppvObject) {
					AddRef();
					return S_OK;
				}
			}
			HRESULT hr = mRootSignature->QueryInterface(riid, ppvObject);
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
			return mRootSignature->GetPrivateData(guid, pDataSize, pData);
		}

		HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT DataSize, const void *pData) override
		{
			return mRootSignature->SetPrivateData(guid, DataSize, pData);
		}

		HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown *pData) override
		{
			return mRootSignature->SetPrivateDataInterface(guid, pData);
		}

		HRESULT STDMETHODCALLTYPE SetName(LPCWSTR Name) override
		{
			return mRootSignature->SetName(Name);
		}

		HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void **ppvDevice) override
		{
			HRESULT hr = mRootSignature->GetDevice(riid, ppvDevice);
			if (SUCCEEDED(hr))
				DX12Wrapper::WrapD3D12DeviceResult(riid, ppvDevice);
			return hr;
		}

	public:
		std::atomic<long> mRefCount{1};
		ID3D12RootSignature *mRootSignature = nullptr;
	};

	class QueryHeapWrapper final : public ID3D12QueryHeap, public WrapperBase
	{
	public:
		explicit QueryHeapWrapper(ID3D12QueryHeap *queryHeap)
			: WrapperBase(queryHeap)
		{
			mQueryHeap = queryHeap;
			RegisterWrapperPointer(static_cast<ID3D12Object*>(this));
			RegisterWrapperPointer(static_cast<ID3D12DeviceChild*>(this));
			RegisterWrapperPointer(static_cast<ID3D12Pageable*>(this));
			RegisterWrapperPointer(static_cast<ID3D12QueryHeap*>(this));
			DX12LogManager::Fields("DX12Wrapper",
				"kind=query_heap,stage=create,wrapper=%p,object=%p",
				this, queryHeap);
		}

		void *GetQueryHeapWrapperInterface(REFIID riid)
		{
			if (IsEqualIID(riid, __uuidof(IUnknown)) || IsEqualIID(riid, __uuidof(ID3D12QueryHeap)))
				return static_cast<ID3D12QueryHeap*>(this);
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
			if (IsEqualIID(riid, __uuidof(IUnknown)) || IsSupportedWrapperIid(mQueryHeap, riid, IsD3D12QueryHeapWrapperIid)) {
				*ppvObject = GetQueryHeapWrapperInterface(riid);
				if (*ppvObject) {
					AddRef();
					return S_OK;
				}
			}
			HRESULT hr = mQueryHeap->QueryInterface(riid, ppvObject);
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
			return mQueryHeap->GetPrivateData(guid, pDataSize, pData);
		}

		HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT DataSize, const void *pData) override
		{
			return mQueryHeap->SetPrivateData(guid, DataSize, pData);
		}

		HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown *pData) override
		{
			return mQueryHeap->SetPrivateDataInterface(guid, pData);
		}

		HRESULT STDMETHODCALLTYPE SetName(LPCWSTR Name) override
		{
			return mQueryHeap->SetName(Name);
		}

		HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void **ppvDevice) override
		{
			HRESULT hr = mQueryHeap->GetDevice(riid, ppvDevice);
			if (SUCCEEDED(hr))
				DX12Wrapper::WrapD3D12DeviceResult(riid, ppvDevice);
			return hr;
		}

	public:
		std::atomic<long> mRefCount{1};
		ID3D12QueryHeap *mQueryHeap = nullptr;
	};

	class CommandSignatureWrapper final : public ID3D12CommandSignature, public WrapperBase
	{
	public:
		explicit CommandSignatureWrapper(ID3D12CommandSignature *commandSignature)
			: WrapperBase(commandSignature)
		{
			mCommandSignature = commandSignature;
			RegisterWrapperPointer(static_cast<ID3D12Object*>(this));
			RegisterWrapperPointer(static_cast<ID3D12DeviceChild*>(this));
			RegisterWrapperPointer(static_cast<ID3D12Pageable*>(this));
			RegisterWrapperPointer(static_cast<ID3D12CommandSignature*>(this));
			DX12LogManager::Fields("DX12Wrapper",
				"kind=command_signature,stage=create,wrapper=%p,object=%p",
				this, commandSignature);
		}

		void *GetCommandSignatureWrapperInterface(REFIID riid)
		{
			if (IsEqualIID(riid, __uuidof(IUnknown)) || IsEqualIID(riid, __uuidof(ID3D12CommandSignature)))
				return static_cast<ID3D12CommandSignature*>(this);
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
			if (IsEqualIID(riid, __uuidof(IUnknown)) ||
				IsSupportedWrapperIid(mCommandSignature, riid, IsD3D12CommandSignatureWrapperIid)) {
				*ppvObject = GetCommandSignatureWrapperInterface(riid);
				if (*ppvObject) {
					AddRef();
					return S_OK;
				}
			}
			HRESULT hr = mCommandSignature->QueryInterface(riid, ppvObject);
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
			return mCommandSignature->GetPrivateData(guid, pDataSize, pData);
		}

		HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT DataSize, const void *pData) override
		{
			return mCommandSignature->SetPrivateData(guid, DataSize, pData);
		}

		HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown *pData) override
		{
			return mCommandSignature->SetPrivateDataInterface(guid, pData);
		}

		HRESULT STDMETHODCALLTYPE SetName(LPCWSTR Name) override
		{
			return mCommandSignature->SetName(Name);
		}

		HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void **ppvDevice) override
		{
			HRESULT hr = mCommandSignature->GetDevice(riid, ppvDevice);
			if (SUCCEEDED(hr))
				DX12Wrapper::WrapD3D12DeviceResult(riid, ppvDevice);
			return hr;
		}

	public:
		std::atomic<long> mRefCount{1};
		ID3D12CommandSignature *mCommandSignature = nullptr;
	};

	class LifetimeTrackerWrapper final : public ID3D12LifetimeTracker, public WrapperBase
	{
	public:
		explicit LifetimeTrackerWrapper(ID3D12LifetimeTracker *tracker)
			: WrapperBase(tracker)
		{
			mTracker = tracker;
			RegisterWrapperPointer(static_cast<ID3D12Object*>(this));
			RegisterWrapperPointer(static_cast<ID3D12DeviceChild*>(this));
			RegisterWrapperPointer(static_cast<ID3D12LifetimeTracker*>(this));
			DX12LogManager::Fields("DX12Wrapper",
				"kind=lifetime_tracker,stage=create,wrapper=%p,object=%p",
				this, tracker);
		}

		void *GetLifetimeTrackerWrapperInterface(REFIID riid)
		{
			if (IsEqualIID(riid, __uuidof(IUnknown)) || IsEqualIID(riid, __uuidof(ID3D12LifetimeTracker)))
				return static_cast<ID3D12LifetimeTracker*>(this);
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
			if (IsEqualIID(riid, __uuidof(IUnknown)) ||
				IsSupportedWrapperIid(mTracker, riid, IsD3D12LifetimeTrackerWrapperIid)) {
				*ppvObject = GetLifetimeTrackerWrapperInterface(riid);
				if (*ppvObject) {
					AddRef();
					return S_OK;
				}
			}
			HRESULT hr = mTracker->QueryInterface(riid, ppvObject);
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
			return mTracker->GetPrivateData(guid, pDataSize, pData);
		}

		HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT DataSize, const void *pData) override
		{
			return mTracker->SetPrivateData(guid, DataSize, pData);
		}

		HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown *pData) override
		{
			return mTracker->SetPrivateDataInterface(guid, pData);
		}

		HRESULT STDMETHODCALLTYPE SetName(LPCWSTR Name) override
		{
			return mTracker->SetName(Name);
		}

		HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void **ppvDevice) override
		{
			HRESULT hr = mTracker->GetDevice(riid, ppvDevice);
			if (SUCCEEDED(hr))
				DX12Wrapper::WrapD3D12DeviceResult(riid, ppvDevice);
			return hr;
		}

		HRESULT STDMETHODCALLTYPE DestroyOwnedObject(ID3D12DeviceChild *pObject) override
		{
			return mTracker->DestroyOwnedObject(
				reinterpret_cast<ID3D12DeviceChild*>(DX12Wrapper::Unwrap(pObject)));
		}

	public:
		std::atomic<long> mRefCount{1};
		ID3D12LifetimeTracker *mTracker = nullptr;
	};

	class ProtectedResourceSessionWrapper final : public ID3D12ProtectedResourceSession1, public WrapperBase
	{
	public:
		explicit ProtectedResourceSessionWrapper(ID3D12ProtectedResourceSession *session)
			: WrapperBase(session)
		{
			mSession = session;
			RegisterWrapperPointer(static_cast<ID3D12Object*>(this));
			RegisterWrapperPointer(static_cast<ID3D12DeviceChild*>(this));
			RegisterWrapperPointer(static_cast<ID3D12ProtectedSession*>(this));
			RegisterWrapperPointer(static_cast<ID3D12ProtectedResourceSession*>(this));
			RegisterWrapperPointer(static_cast<ID3D12ProtectedResourceSession1*>(this));
			DX12LogManager::Fields("DX12Wrapper",
				"kind=protected_resource_session,stage=create,wrapper=%p,object=%p",
				this, session);
		}

		HRESULT QuerySession1(ID3D12ProtectedResourceSession1 **session)
		{
			return mSession->QueryInterface(IID_PPV_ARGS(session));
		}

		void *GetProtectedResourceSessionWrapperInterface(REFIID riid)
		{
			if (IsEqualIID(riid, __uuidof(IUnknown)) ||
				IsEqualIID(riid, __uuidof(ID3D12ProtectedResourceSession1)))
				return static_cast<ID3D12ProtectedResourceSession1*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12ProtectedResourceSession)))
				return static_cast<ID3D12ProtectedResourceSession*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12ProtectedSession)))
				return static_cast<ID3D12ProtectedSession*>(this);
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
			if (IsEqualIID(riid, __uuidof(IUnknown)) ||
				IsSupportedWrapperIid(mSession, riid, IsD3D12ProtectedResourceSessionWrapperIid)) {
				*ppvObject = GetProtectedResourceSessionWrapperInterface(riid);
				if (*ppvObject) {
					AddRef();
					return S_OK;
				}
			}
			HRESULT hr = mSession->QueryInterface(riid, ppvObject);
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
			return mSession->GetPrivateData(guid, pDataSize, pData);
		}

		HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT DataSize, const void *pData) override
		{
			return mSession->SetPrivateData(guid, DataSize, pData);
		}

		HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown *pData) override
		{
			return mSession->SetPrivateDataInterface(guid, pData);
		}

		HRESULT STDMETHODCALLTYPE SetName(LPCWSTR Name) override
		{
			return mSession->SetName(Name);
		}

		HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void **ppvDevice) override
		{
			HRESULT hr = mSession->GetDevice(riid, ppvDevice);
			if (SUCCEEDED(hr))
				DX12Wrapper::WrapD3D12DeviceResult(riid, ppvDevice);
			return hr;
		}

		HRESULT STDMETHODCALLTYPE GetStatusFence(REFIID riid, void **ppFence) override
		{
			HRESULT hr = mSession->GetStatusFence(riid, ppFence);
			if (SUCCEEDED(hr) && ppFence && *ppFence)
				DX12Wrapper::WrapD3D12FenceResult(riid, ppFence);
			return hr;
		}

		D3D12_PROTECTED_SESSION_STATUS STDMETHODCALLTYPE GetSessionStatus() override
		{
			return mSession->GetSessionStatus();
		}

		D3D12_PROTECTED_RESOURCE_SESSION_DESC STDMETHODCALLTYPE GetDesc() override
		{
			return mSession->GetDesc();
		}

		D3D12_PROTECTED_RESOURCE_SESSION_DESC1 STDMETHODCALLTYPE GetDesc1() override
		{
			ID3D12ProtectedResourceSession1 *session1 = nullptr;
			if (FAILED(QuerySession1(&session1)))
				return D3D12_PROTECTED_RESOURCE_SESSION_DESC1();
			D3D12_PROTECTED_RESOURCE_SESSION_DESC1 desc = session1->GetDesc1();
			session1->Release();
			return desc;
		}

	public:
		std::atomic<long> mRefCount{1};
		ID3D12ProtectedResourceSession *mSession = nullptr;
	};

	class MetaCommandWrapper final : public ID3D12MetaCommand, public WrapperBase
	{
	public:
		explicit MetaCommandWrapper(ID3D12MetaCommand *metaCommand)
			: WrapperBase(metaCommand)
		{
			mMetaCommand = metaCommand;
			RegisterWrapperPointer(static_cast<ID3D12Object*>(this));
			RegisterWrapperPointer(static_cast<ID3D12DeviceChild*>(this));
			RegisterWrapperPointer(static_cast<ID3D12Pageable*>(this));
			RegisterWrapperPointer(static_cast<ID3D12MetaCommand*>(this));
			DX12LogManager::Fields("DX12Wrapper",
				"kind=meta_command,stage=create,wrapper=%p,object=%p",
				this, metaCommand);
		}

		void *GetMetaCommandWrapperInterface(REFIID riid)
		{
			if (IsEqualIID(riid, __uuidof(IUnknown)) || IsEqualIID(riid, __uuidof(ID3D12MetaCommand)))
				return static_cast<ID3D12MetaCommand*>(this);
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
			if (IsEqualIID(riid, __uuidof(IUnknown)) ||
				IsSupportedWrapperIid(mMetaCommand, riid, IsD3D12MetaCommandWrapperIid)) {
				*ppvObject = GetMetaCommandWrapperInterface(riid);
				if (*ppvObject) {
					AddRef();
					return S_OK;
				}
			}
			HRESULT hr = mMetaCommand->QueryInterface(riid, ppvObject);
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
			return mMetaCommand->GetPrivateData(guid, pDataSize, pData);
		}

		HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT DataSize, const void *pData) override
		{
			return mMetaCommand->SetPrivateData(guid, DataSize, pData);
		}

		HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown *pData) override
		{
			return mMetaCommand->SetPrivateDataInterface(guid, pData);
		}

		HRESULT STDMETHODCALLTYPE SetName(LPCWSTR Name) override
		{
			return mMetaCommand->SetName(Name);
		}

		HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void **ppvDevice) override
		{
			HRESULT hr = mMetaCommand->GetDevice(riid, ppvDevice);
			if (SUCCEEDED(hr))
				DX12Wrapper::WrapD3D12DeviceResult(riid, ppvDevice);
			return hr;
		}

		UINT64 STDMETHODCALLTYPE GetRequiredParameterResourceSize(
			D3D12_META_COMMAND_PARAMETER_STAGE Stage, UINT ParameterIndex) override
		{
			return mMetaCommand->GetRequiredParameterResourceSize(Stage, ParameterIndex);
		}

	public:
		std::atomic<long> mRefCount{1};
		ID3D12MetaCommand *mMetaCommand = nullptr;
	};

	class StateObjectWrapper final :
		public ID3D12StateObject,
		public ID3D12StateObjectProperties1,
		public WrapperBase
	{
	public:
		explicit StateObjectWrapper(ID3D12StateObject *stateObject)
			: WrapperBase(stateObject)
		{
			mStateObject = stateObject;
			RegisterWrapperPointer(static_cast<ID3D12Object*>(static_cast<ID3D12StateObject*>(this)));
			RegisterWrapperPointer(static_cast<ID3D12DeviceChild*>(static_cast<ID3D12StateObject*>(this)));
			RegisterWrapperPointer(static_cast<ID3D12Pageable*>(static_cast<ID3D12StateObject*>(this)));
			RegisterWrapperPointer(static_cast<ID3D12StateObject*>(this));
			RegisterWrapperPointer(static_cast<ID3D12StateObjectProperties*>(this));
			RegisterWrapperPointer(static_cast<ID3D12StateObjectProperties1*>(this));
			DX12LogManager::Fields("DX12Wrapper",
				"kind=state_object,stage=create,wrapper=%p,object=%p",
				this, stateObject);
		}

		HRESULT QueryProperties(ID3D12StateObjectProperties **properties)
		{
			return mStateObject->QueryInterface(IID_PPV_ARGS(properties));
		}

		HRESULT QueryProperties1(ID3D12StateObjectProperties1 **properties)
		{
			return mStateObject->QueryInterface(IID_PPV_ARGS(properties));
		}

		void *GetStateObjectWrapperInterface(REFIID riid)
		{
			if (IsEqualIID(riid, __uuidof(IUnknown)) || IsEqualIID(riid, __uuidof(ID3D12StateObject)))
				return static_cast<ID3D12StateObject*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12StateObjectProperties1)))
				return static_cast<ID3D12StateObjectProperties1*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12StateObjectProperties)))
				return static_cast<ID3D12StateObjectProperties*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12Pageable)))
				return static_cast<ID3D12Pageable*>(static_cast<ID3D12StateObject*>(this));
			if (IsEqualIID(riid, __uuidof(ID3D12DeviceChild)))
				return static_cast<ID3D12DeviceChild*>(static_cast<ID3D12StateObject*>(this));
			if (IsEqualIID(riid, __uuidof(ID3D12Object)))
				return static_cast<ID3D12Object*>(static_cast<ID3D12StateObject*>(this));
			return nullptr;
		}

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) override
		{
			if (!ppvObject)
				return E_POINTER;
			*ppvObject = nullptr;
			if (IsEqualIID(riid, __uuidof(IUnknown)) ||
				IsSupportedWrapperIid(mStateObject, riid, IsD3D12StateObjectWrapperIid)) {
				*ppvObject = GetStateObjectWrapperInterface(riid);
				if (*ppvObject) {
					AddRef();
					return S_OK;
				}
			}
			HRESULT hr = mStateObject->QueryInterface(riid, ppvObject);
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
			return mStateObject->GetPrivateData(guid, pDataSize, pData);
		}

		HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT DataSize, const void *pData) override
		{
			return mStateObject->SetPrivateData(guid, DataSize, pData);
		}

		HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown *pData) override
		{
			return mStateObject->SetPrivateDataInterface(guid, pData);
		}

		HRESULT STDMETHODCALLTYPE SetName(LPCWSTR Name) override
		{
			return mStateObject->SetName(Name);
		}

		HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void **ppvDevice) override
		{
			HRESULT hr = mStateObject->GetDevice(riid, ppvDevice);
			if (SUCCEEDED(hr))
				DX12Wrapper::WrapD3D12DeviceResult(riid, ppvDevice);
			return hr;
		}

		void *STDMETHODCALLTYPE GetShaderIdentifier(LPCWSTR pExportName) override
		{
			ID3D12StateObjectProperties *properties = nullptr;
			if (FAILED(QueryProperties(&properties)))
				return nullptr;
			void *identifier = properties->GetShaderIdentifier(pExportName);
			properties->Release();
			return identifier;
		}

		UINT64 STDMETHODCALLTYPE GetShaderStackSize(LPCWSTR pExportName) override
		{
			ID3D12StateObjectProperties *properties = nullptr;
			if (FAILED(QueryProperties(&properties)))
				return 0;
			UINT64 size = properties->GetShaderStackSize(pExportName);
			properties->Release();
			return size;
		}

		UINT64 STDMETHODCALLTYPE GetPipelineStackSize() override
		{
			ID3D12StateObjectProperties *properties = nullptr;
			if (FAILED(QueryProperties(&properties)))
				return 0;
			UINT64 size = properties->GetPipelineStackSize();
			properties->Release();
			return size;
		}

		void STDMETHODCALLTYPE SetPipelineStackSize(UINT64 PipelineStackSizeInBytes) override
		{
			ID3D12StateObjectProperties *properties = nullptr;
			if (SUCCEEDED(QueryProperties(&properties))) {
				properties->SetPipelineStackSize(PipelineStackSizeInBytes);
				properties->Release();
			}
		}

		D3D12_PROGRAM_IDENTIFIER STDMETHODCALLTYPE GetProgramIdentifier(LPCWSTR pProgramName) override
		{
			ID3D12StateObjectProperties1 *properties = nullptr;
			if (FAILED(QueryProperties1(&properties)))
				return D3D12_PROGRAM_IDENTIFIER();
			D3D12_PROGRAM_IDENTIFIER identifier = properties->GetProgramIdentifier(pProgramName);
			properties->Release();
			return identifier;
		}

	public:
		std::atomic<long> mRefCount{1};
		ID3D12StateObject *mStateObject = nullptr;
	};

	class PipelineLibraryWrapper final : public ID3D12PipelineLibrary1, public WrapperBase
	{
	public:
		explicit PipelineLibraryWrapper(ID3D12PipelineLibrary *library)
			: WrapperBase(library)
		{
			mLibrary = library;
			RegisterWrapperPointer(static_cast<ID3D12Object*>(this));
			RegisterWrapperPointer(static_cast<ID3D12DeviceChild*>(this));
			RegisterWrapperPointer(static_cast<ID3D12PipelineLibrary*>(this));
			RegisterWrapperPointer(static_cast<ID3D12PipelineLibrary1*>(this));
			DX12LogManager::Fields("DX12Wrapper",
				"kind=pipeline_library,stage=create,wrapper=%p,object=%p",
				this, library);
		}

		HRESULT QueryLibrary1(ID3D12PipelineLibrary1 **library)
		{
			return mLibrary->QueryInterface(IID_PPV_ARGS(library));
		}

		void *GetPipelineLibraryWrapperInterface(REFIID riid)
		{
			if (IsEqualIID(riid, __uuidof(IUnknown)) || IsEqualIID(riid, __uuidof(ID3D12PipelineLibrary1)))
				return static_cast<ID3D12PipelineLibrary1*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12PipelineLibrary)))
				return static_cast<ID3D12PipelineLibrary*>(this);
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
			if (IsEqualIID(riid, __uuidof(IUnknown)) ||
				IsSupportedWrapperIid(mLibrary, riid, IsD3D12PipelineLibraryWrapperIid)) {
				*ppvObject = GetPipelineLibraryWrapperInterface(riid);
				if (*ppvObject) {
					AddRef();
					return S_OK;
				}
			}
			HRESULT hr = mLibrary->QueryInterface(riid, ppvObject);
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
			return mLibrary->GetPrivateData(guid, pDataSize, pData);
		}

		HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT DataSize, const void *pData) override
		{
			return mLibrary->SetPrivateData(guid, DataSize, pData);
		}

		HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown *pData) override
		{
			return mLibrary->SetPrivateDataInterface(guid, pData);
		}

		HRESULT STDMETHODCALLTYPE SetName(LPCWSTR Name) override
		{
			return mLibrary->SetName(Name);
		}

		HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void **ppvDevice) override
		{
			HRESULT hr = mLibrary->GetDevice(riid, ppvDevice);
			if (SUCCEEDED(hr))
				DX12Wrapper::WrapD3D12DeviceResult(riid, ppvDevice);
			return hr;
		}

		HRESULT STDMETHODCALLTYPE StorePipeline(LPCWSTR pName, ID3D12PipelineState *pPipeline) override
		{
			return mLibrary->StorePipeline(
				pName, DX12WrapperRuntimeUtils::UnwrapPipelineState(pPipeline));
		}

		HRESULT STDMETHODCALLTYPE LoadGraphicsPipeline(
			LPCWSTR pName, const D3D12_GRAPHICS_PIPELINE_STATE_DESC *pDesc,
			REFIID riid, void **ppPipelineState) override
		{
			D3D12_GRAPHICS_PIPELINE_STATE_DESC descStorage = {};
			const D3D12_GRAPHICS_PIPELINE_STATE_DESC *activeDesc =
				DX12WrapperRuntimeUtils::UnwrapGraphicsPipelineStateDesc(pDesc, &descStorage);
			HRESULT hr = mLibrary->LoadGraphicsPipeline(pName, activeDesc, riid, ppPipelineState);
			if (SUCCEEDED(hr) && ppPipelineState && *ppPipelineState)
				DX12Wrapper::WrapD3D12PipelineStateResult(riid, ppPipelineState);
			return hr;
		}

		HRESULT STDMETHODCALLTYPE LoadComputePipeline(
			LPCWSTR pName, const D3D12_COMPUTE_PIPELINE_STATE_DESC *pDesc,
			REFIID riid, void **ppPipelineState) override
		{
			D3D12_COMPUTE_PIPELINE_STATE_DESC descStorage = {};
			const D3D12_COMPUTE_PIPELINE_STATE_DESC *activeDesc =
				DX12WrapperRuntimeUtils::UnwrapComputePipelineStateDesc(pDesc, &descStorage);
			HRESULT hr = mLibrary->LoadComputePipeline(pName, activeDesc, riid, ppPipelineState);
			if (SUCCEEDED(hr) && ppPipelineState && *ppPipelineState)
				DX12Wrapper::WrapD3D12PipelineStateResult(riid, ppPipelineState);
			return hr;
		}

		SIZE_T STDMETHODCALLTYPE GetSerializedSize() override
		{
			return mLibrary->GetSerializedSize();
		}

		HRESULT STDMETHODCALLTYPE Serialize(void *pData, SIZE_T DataSizeInBytes) override
		{
			return mLibrary->Serialize(pData, DataSizeInBytes);
		}

		HRESULT STDMETHODCALLTYPE LoadPipeline(
			LPCWSTR pName, const D3D12_PIPELINE_STATE_STREAM_DESC *pDesc,
			REFIID riid, void **ppPipelineState) override
		{
			ID3D12PipelineLibrary1 *library1 = nullptr;
			HRESULT hr = QueryLibrary1(&library1);
			if (FAILED(hr))
				return hr;
			hr = library1->LoadPipeline(pName, pDesc, riid, ppPipelineState);
			if (SUCCEEDED(hr) && ppPipelineState && *ppPipelineState)
				DX12Wrapper::WrapD3D12PipelineStateResult(riid, ppPipelineState);
			library1->Release();
			return hr;
		}

	public:
		std::atomic<long> mRefCount{1};
		ID3D12PipelineLibrary *mLibrary = nullptr;
	};

	class ShaderCacheSessionWrapper final : public ID3D12ShaderCacheSession, public WrapperBase
	{
	public:
		explicit ShaderCacheSessionWrapper(ID3D12ShaderCacheSession *session)
			: WrapperBase(session)
		{
			mSession = session;
			RegisterWrapperPointer(static_cast<ID3D12Object*>(this));
			RegisterWrapperPointer(static_cast<ID3D12DeviceChild*>(this));
			RegisterWrapperPointer(static_cast<ID3D12ShaderCacheSession*>(this));
			DX12LogManager::Fields("DX12Wrapper",
				"kind=shader_cache_session,stage=create,wrapper=%p,object=%p",
				this, session);
		}

		void *GetShaderCacheSessionWrapperInterface(REFIID riid)
		{
			if (IsEqualIID(riid, __uuidof(IUnknown)) || IsEqualIID(riid, __uuidof(ID3D12ShaderCacheSession)))
				return static_cast<ID3D12ShaderCacheSession*>(this);
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
			if (IsEqualIID(riid, __uuidof(IUnknown)) ||
				IsSupportedWrapperIid(mSession, riid, IsD3D12ShaderCacheSessionWrapperIid)) {
				*ppvObject = GetShaderCacheSessionWrapperInterface(riid);
				if (*ppvObject) {
					AddRef();
					return S_OK;
				}
			}
			HRESULT hr = mSession->QueryInterface(riid, ppvObject);
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
			return mSession->GetPrivateData(guid, pDataSize, pData);
		}

		HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT DataSize, const void *pData) override
		{
			return mSession->SetPrivateData(guid, DataSize, pData);
		}

		HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown *pData) override
		{
			return mSession->SetPrivateDataInterface(guid, pData);
		}

		HRESULT STDMETHODCALLTYPE SetName(LPCWSTR Name) override
		{
			return mSession->SetName(Name);
		}

		HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void **ppvDevice) override
		{
			HRESULT hr = mSession->GetDevice(riid, ppvDevice);
			if (SUCCEEDED(hr))
				DX12Wrapper::WrapD3D12DeviceResult(riid, ppvDevice);
			return hr;
		}

		HRESULT STDMETHODCALLTYPE FindValue(
			const void *pKey, UINT KeySize, void *pValue, UINT *pValueSize) override
		{
			return mSession->FindValue(pKey, KeySize, pValue, pValueSize);
		}

		HRESULT STDMETHODCALLTYPE StoreValue(
			const void *pKey, UINT KeySize, const void *pValue, UINT ValueSize) override
		{
			return mSession->StoreValue(pKey, KeySize, pValue, ValueSize);
		}

		void STDMETHODCALLTYPE SetDeleteOnDestroy() override
		{
			mSession->SetDeleteOnDestroy();
		}

		D3D12_SHADER_CACHE_SESSION_DESC STDMETHODCALLTYPE GetDesc() override
		{
			return mSession->GetDesc();
		}

	public:
		std::atomic<long> mRefCount{1};
		ID3D12ShaderCacheSession *mSession = nullptr;
	};


} // namespace DX12WrapperDetail
