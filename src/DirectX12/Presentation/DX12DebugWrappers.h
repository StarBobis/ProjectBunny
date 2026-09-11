// Debug-layer wrappers: ID3D12Debug*, DRED, Tools, SDKConfiguration, InfoQueue, debug device/queue/list.
// Split from DX12Wrapper.cpp; COM wrapper class definitions (inline).
#pragma once

#include "DX12WrapperCore.h"

namespace DX12WrapperDetail
{

	class DebugInterfaceWrapper final :
		public ID3D12Debug6,
		public ID3D12Debug1,
		public ID3D12Debug2,
		public WrapperBase
	{
	public:
		explicit DebugInterfaceWrapper(IUnknown *debug)
			: WrapperBase(debug, false)
		{
			mDebug = debug;
			RegisterWrapperPointer(static_cast<ID3D12Debug*>(static_cast<ID3D12Debug6*>(this)));
			RegisterWrapperPointer(static_cast<ID3D12Debug1*>(this));
			RegisterWrapperPointer(static_cast<ID3D12Debug2*>(this));
			RegisterWrapperPointer(static_cast<ID3D12Debug3*>(this));
			RegisterWrapperPointer(static_cast<ID3D12Debug4*>(this));
			RegisterWrapperPointer(static_cast<ID3D12Debug5*>(this));
			RegisterWrapperPointer(static_cast<ID3D12Debug6*>(this));
			DX12_LOG("DX12Wrapper",
				"kind=debug_interface,stage=create,wrapper=%p,object=%p",
				this, debug);
		}

		void *GetAuxiliaryWrapperInterface(REFIID riid)
		{
			if (IsEqualIID(riid, __uuidof(IUnknown)) || IsEqualIID(riid, __uuidof(ID3D12Debug6)))
				return static_cast<ID3D12Debug6*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12Debug5)))
				return static_cast<ID3D12Debug5*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12Debug4)))
				return static_cast<ID3D12Debug4*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12Debug3)))
				return static_cast<ID3D12Debug3*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12Debug2)))
				return static_cast<ID3D12Debug2*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12Debug1)))
				return static_cast<ID3D12Debug1*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12Debug)))
				return static_cast<ID3D12Debug*>(static_cast<ID3D12Debug6*>(this));
			return nullptr;
		}

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) override
		{
			if (!ppvObject)
				return E_POINTER;
			*ppvObject = nullptr;

			if (IsEqualIID(riid, __uuidof(IUnknown)) || IsSupportedWrapperIid(mDebug, riid, IsD3D12DebugInterfaceIid)) {
				*ppvObject = GetAuxiliaryWrapperInterface(riid);
				if (*ppvObject) {
					AddRef();
					return S_OK;
				}
			}

			HRESULT hr = mDebug->QueryInterface(riid, ppvObject);
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

		void STDMETHODCALLTYPE EnableDebugLayer() override
		{
			ID3D12Debug *debug = nullptr;
			if (SUCCEEDED(mDebug->QueryInterface(IID_PPV_ARGS(&debug)))) {
				debug->EnableDebugLayer();
				debug->Release();
				return;
			}
			ID3D12Debug1 *debug1 = nullptr;
			if (SUCCEEDED(mDebug->QueryInterface(IID_PPV_ARGS(&debug1)))) {
				debug1->EnableDebugLayer();
				debug1->Release();
			}
		}

		void STDMETHODCALLTYPE SetEnableGPUBasedValidation(BOOL enable) override
		{
			ID3D12Debug3 *debug3 = nullptr;
			if (SUCCEEDED(mDebug->QueryInterface(IID_PPV_ARGS(&debug3)))) {
				debug3->SetEnableGPUBasedValidation(enable);
				debug3->Release();
				return;
			}
			ID3D12Debug1 *debug1 = nullptr;
			if (SUCCEEDED(mDebug->QueryInterface(IID_PPV_ARGS(&debug1)))) {
				debug1->SetEnableGPUBasedValidation(enable);
				debug1->Release();
			}
		}

		void STDMETHODCALLTYPE SetEnableSynchronizedCommandQueueValidation(BOOL enable) override
		{
			ID3D12Debug3 *debug3 = nullptr;
			if (SUCCEEDED(mDebug->QueryInterface(IID_PPV_ARGS(&debug3)))) {
				debug3->SetEnableSynchronizedCommandQueueValidation(enable);
				debug3->Release();
				return;
			}
			ID3D12Debug1 *debug1 = nullptr;
			if (SUCCEEDED(mDebug->QueryInterface(IID_PPV_ARGS(&debug1)))) {
				debug1->SetEnableSynchronizedCommandQueueValidation(enable);
				debug1->Release();
			}
		}

		void STDMETHODCALLTYPE SetGPUBasedValidationFlags(D3D12_GPU_BASED_VALIDATION_FLAGS flags) override
		{
			ID3D12Debug3 *debug3 = nullptr;
			if (SUCCEEDED(mDebug->QueryInterface(IID_PPV_ARGS(&debug3)))) {
				debug3->SetGPUBasedValidationFlags(flags);
				debug3->Release();
				return;
			}
			ID3D12Debug2 *debug2 = nullptr;
			if (SUCCEEDED(mDebug->QueryInterface(IID_PPV_ARGS(&debug2)))) {
				debug2->SetGPUBasedValidationFlags(flags);
				debug2->Release();
			}
		}

		void STDMETHODCALLTYPE DisableDebugLayer() override
		{
			ID3D12Debug4 *debug4 = nullptr;
			if (SUCCEEDED(mDebug->QueryInterface(IID_PPV_ARGS(&debug4)))) {
				debug4->DisableDebugLayer();
				debug4->Release();
			}
		}

		void STDMETHODCALLTYPE SetEnableAutoName(BOOL enable) override
		{
			ID3D12Debug5 *debug5 = nullptr;
			if (SUCCEEDED(mDebug->QueryInterface(IID_PPV_ARGS(&debug5)))) {
				debug5->SetEnableAutoName(enable);
				debug5->Release();
			}
		}

		void STDMETHODCALLTYPE SetForceLegacyBarrierValidation(BOOL enable) override
		{
			ID3D12Debug6 *debug6 = nullptr;
			if (SUCCEEDED(mDebug->QueryInterface(IID_PPV_ARGS(&debug6)))) {
				debug6->SetForceLegacyBarrierValidation(enable);
				debug6->Release();
			}
		}

	public:
		std::atomic<long> mRefCount{1};
		IUnknown *mDebug = nullptr;
	};

	class DredSettingsWrapper final : public ID3D12DeviceRemovedExtendedDataSettings2, public WrapperBase
	{
	public:
		explicit DredSettingsWrapper(ID3D12DeviceRemovedExtendedDataSettings *settings)
			: WrapperBase(settings, false)
		{
			mSettings = settings;
			RegisterWrapperPointer(static_cast<ID3D12DeviceRemovedExtendedDataSettings*>(this));
			RegisterWrapperPointer(static_cast<ID3D12DeviceRemovedExtendedDataSettings1*>(this));
			RegisterWrapperPointer(static_cast<ID3D12DeviceRemovedExtendedDataSettings2*>(this));
			DX12_LOG("DX12Wrapper",
				"kind=dred_settings,stage=create,wrapper=%p,object=%p",
				this, settings);
		}

		void *GetAuxiliaryWrapperInterface(REFIID riid)
		{
			if (IsEqualIID(riid, __uuidof(IUnknown)) ||
				IsEqualIID(riid, __uuidof(ID3D12DeviceRemovedExtendedDataSettings2)))
				return static_cast<ID3D12DeviceRemovedExtendedDataSettings2*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12DeviceRemovedExtendedDataSettings1)))
				return static_cast<ID3D12DeviceRemovedExtendedDataSettings1*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12DeviceRemovedExtendedDataSettings)))
				return static_cast<ID3D12DeviceRemovedExtendedDataSettings*>(this);
			return nullptr;
		}

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) override
		{
			if (!ppvObject)
				return E_POINTER;
			*ppvObject = nullptr;

			if (IsEqualIID(riid, __uuidof(IUnknown)) || IsSupportedWrapperIid(mSettings, riid, IsD3D12DredSettingsIid)) {
				*ppvObject = GetAuxiliaryWrapperInterface(riid);
				if (*ppvObject) {
					AddRef();
					return S_OK;
				}
			}

			HRESULT hr = mSettings->QueryInterface(riid, ppvObject);
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

		void STDMETHODCALLTYPE SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT enablement) override
		{
			mSettings->SetAutoBreadcrumbsEnablement(enablement);
		}

		void STDMETHODCALLTYPE SetPageFaultEnablement(D3D12_DRED_ENABLEMENT enablement) override
		{
			mSettings->SetPageFaultEnablement(enablement);
		}

		void STDMETHODCALLTYPE SetWatsonDumpEnablement(D3D12_DRED_ENABLEMENT enablement) override
		{
			mSettings->SetWatsonDumpEnablement(enablement);
		}

		void STDMETHODCALLTYPE SetBreadcrumbContextEnablement(D3D12_DRED_ENABLEMENT enablement) override
		{
			ID3D12DeviceRemovedExtendedDataSettings1 *settings1 = nullptr;
			if (SUCCEEDED(mSettings->QueryInterface(IID_PPV_ARGS(&settings1)))) {
				settings1->SetBreadcrumbContextEnablement(enablement);
				settings1->Release();
			}
		}

		void STDMETHODCALLTYPE UseMarkersOnlyAutoBreadcrumbs(BOOL markersOnly) override
		{
			ID3D12DeviceRemovedExtendedDataSettings2 *settings2 = nullptr;
			if (SUCCEEDED(mSettings->QueryInterface(IID_PPV_ARGS(&settings2)))) {
				settings2->UseMarkersOnlyAutoBreadcrumbs(markersOnly);
				settings2->Release();
			}
		}

	public:
		std::atomic<long> mRefCount{1};
		ID3D12DeviceRemovedExtendedDataSettings *mSettings = nullptr;
	};

	class DredDataWrapper final : public ID3D12DeviceRemovedExtendedData2, public WrapperBase
	{
	public:
		explicit DredDataWrapper(ID3D12DeviceRemovedExtendedData *data)
			: WrapperBase(data, false)
		{
			mData = data;
			RegisterWrapperPointer(static_cast<ID3D12DeviceRemovedExtendedData*>(this));
			RegisterWrapperPointer(static_cast<ID3D12DeviceRemovedExtendedData1*>(this));
			RegisterWrapperPointer(static_cast<ID3D12DeviceRemovedExtendedData2*>(this));
			DX12_LOG("DX12Wrapper",
				"kind=dred_data,stage=create,wrapper=%p,object=%p",
				this, data);
		}

		void *GetAuxiliaryWrapperInterface(REFIID riid)
		{
			if (IsEqualIID(riid, __uuidof(IUnknown)) || IsEqualIID(riid, __uuidof(ID3D12DeviceRemovedExtendedData2)))
				return static_cast<ID3D12DeviceRemovedExtendedData2*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12DeviceRemovedExtendedData1)))
				return static_cast<ID3D12DeviceRemovedExtendedData1*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12DeviceRemovedExtendedData)))
				return static_cast<ID3D12DeviceRemovedExtendedData*>(this);
			return nullptr;
		}

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) override
		{
			if (!ppvObject)
				return E_POINTER;
			*ppvObject = nullptr;

			if (IsEqualIID(riid, __uuidof(IUnknown)) || IsSupportedWrapperIid(mData, riid, IsD3D12DredDataIid)) {
				*ppvObject = GetAuxiliaryWrapperInterface(riid);
				if (*ppvObject) {
					AddRef();
					return S_OK;
				}
			}

			HRESULT hr = mData->QueryInterface(riid, ppvObject);
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

		HRESULT STDMETHODCALLTYPE GetAutoBreadcrumbsOutput(D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT *pOutput) override
		{
			return mData->GetAutoBreadcrumbsOutput(pOutput);
		}

		HRESULT STDMETHODCALLTYPE GetPageFaultAllocationOutput(D3D12_DRED_PAGE_FAULT_OUTPUT *pOutput) override
		{
			return mData->GetPageFaultAllocationOutput(pOutput);
		}

		HRESULT STDMETHODCALLTYPE GetAutoBreadcrumbsOutput1(D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 *pOutput) override
		{
			ID3D12DeviceRemovedExtendedData1 *data1 = nullptr;
			HRESULT hr = mData->QueryInterface(IID_PPV_ARGS(&data1));
			if (FAILED(hr))
				return hr;
			hr = data1->GetAutoBreadcrumbsOutput1(pOutput);
			data1->Release();
			return hr;
		}

		HRESULT STDMETHODCALLTYPE GetPageFaultAllocationOutput1(D3D12_DRED_PAGE_FAULT_OUTPUT1 *pOutput) override
		{
			ID3D12DeviceRemovedExtendedData1 *data1 = nullptr;
			HRESULT hr = mData->QueryInterface(IID_PPV_ARGS(&data1));
			if (FAILED(hr))
				return hr;
			hr = data1->GetPageFaultAllocationOutput1(pOutput);
			data1->Release();
			return hr;
		}

		HRESULT STDMETHODCALLTYPE GetPageFaultAllocationOutput2(D3D12_DRED_PAGE_FAULT_OUTPUT2 *pOutput) override
		{
			ID3D12DeviceRemovedExtendedData2 *data2 = nullptr;
			HRESULT hr = mData->QueryInterface(IID_PPV_ARGS(&data2));
			if (FAILED(hr))
				return hr;
			hr = data2->GetPageFaultAllocationOutput2(pOutput);
			data2->Release();
			return hr;
		}

		D3D12_DRED_DEVICE_STATE STDMETHODCALLTYPE GetDeviceState() override
		{
			ID3D12DeviceRemovedExtendedData2 *data2 = nullptr;
			if (FAILED(mData->QueryInterface(IID_PPV_ARGS(&data2))))
				return D3D12_DRED_DEVICE_STATE_UNKNOWN;
			D3D12_DRED_DEVICE_STATE state = data2->GetDeviceState();
			data2->Release();
			return state;
		}

	public:
		std::atomic<long> mRefCount{1};
		ID3D12DeviceRemovedExtendedData *mData = nullptr;
	};

	class ToolsWrapper final : public ID3D12Tools2, public WrapperBase
	{
	public:
		explicit ToolsWrapper(ID3D12Tools *tools)
			: WrapperBase(tools, false)
		{
			mTools = tools;
			RegisterWrapperPointer(static_cast<ID3D12Tools*>(this));
			RegisterWrapperPointer(static_cast<ID3D12Tools1*>(this));
			RegisterWrapperPointer(static_cast<ID3D12Tools2*>(this));
			DX12_LOG("DX12Wrapper",
				"kind=tools,stage=create,wrapper=%p,object=%p",
				this, tools);
		}

		void *GetAuxiliaryWrapperInterface(REFIID riid)
		{
			if (IsEqualIID(riid, __uuidof(IUnknown)) || IsEqualIID(riid, __uuidof(ID3D12Tools2)))
				return static_cast<ID3D12Tools2*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12Tools1)))
				return static_cast<ID3D12Tools1*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12Tools)))
				return static_cast<ID3D12Tools*>(this);
			return nullptr;
		}

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) override
		{
			if (!ppvObject)
				return E_POINTER;
			*ppvObject = nullptr;

			if (IsEqualIID(riid, __uuidof(IUnknown)) || IsSupportedWrapperIid(mTools, riid, IsD3D12ToolsIid)) {
				*ppvObject = GetAuxiliaryWrapperInterface(riid);
				if (*ppvObject) {
					AddRef();
					return S_OK;
				}
			}

			HRESULT hr = mTools->QueryInterface(riid, ppvObject);
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

		void STDMETHODCALLTYPE EnableShaderInstrumentation(BOOL enable) override
		{
			mTools->EnableShaderInstrumentation(enable);
		}

		BOOL STDMETHODCALLTYPE ShaderInstrumentationEnabled() override
		{
			return mTools->ShaderInstrumentationEnabled();
		}

		HRESULT STDMETHODCALLTYPE ReserveGPUVARangesAtCreate(
			D3D12_GPU_VIRTUAL_ADDRESS_RANGE *ranges, UINT numRanges) override
		{
			ID3D12Tools1 *tools1 = nullptr;
			HRESULT hr = mTools->QueryInterface(IID_PPV_ARGS(&tools1));
			if (FAILED(hr))
				return hr;
			hr = tools1->ReserveGPUVARangesAtCreate(ranges, numRanges);
			tools1->Release();
			return hr;
		}

		void STDMETHODCALLTYPE ClearReservedGPUVARangesList() override
		{
			ID3D12Tools1 *tools1 = nullptr;
			if (SUCCEEDED(mTools->QueryInterface(IID_PPV_ARGS(&tools1)))) {
				tools1->ClearReservedGPUVARangesList();
				tools1->Release();
			}
		}

		HRESULT STDMETHODCALLTYPE SetApplicationSpecificDriverState(IUnknown *adapter, ID3DBlob *blob) override
		{
			ID3D12Tools2 *tools2 = nullptr;
			HRESULT hr = mTools->QueryInterface(IID_PPV_ARGS(&tools2));
			if (FAILED(hr))
				return hr;
			hr = tools2->SetApplicationSpecificDriverState(DX12Wrapper::Unwrap(adapter), blob);
			tools2->Release();
			return hr;
		}

	public:
		std::atomic<long> mRefCount{1};
		ID3D12Tools *mTools = nullptr;
	};

	class SDKConfigurationWrapper final : public ID3D12SDKConfiguration1, public WrapperBase
	{
	public:
		explicit SDKConfigurationWrapper(ID3D12SDKConfiguration *configuration)
			: WrapperBase(configuration, false)
		{
			mConfiguration = configuration;
			RegisterWrapperPointer(static_cast<ID3D12SDKConfiguration*>(this));
			RegisterWrapperPointer(static_cast<ID3D12SDKConfiguration1*>(this));
			DX12_LOG("DX12Wrapper",
				"kind=sdk_configuration,stage=create,wrapper=%p,object=%p",
				this, configuration);
		}

		void *GetAuxiliaryWrapperInterface(REFIID riid)
		{
			if (IsEqualIID(riid, __uuidof(IUnknown)) || IsEqualIID(riid, __uuidof(ID3D12SDKConfiguration1)))
				return static_cast<ID3D12SDKConfiguration1*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12SDKConfiguration)))
				return static_cast<ID3D12SDKConfiguration*>(this);
			return nullptr;
		}

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) override
		{
			if (!ppvObject)
				return E_POINTER;
			*ppvObject = nullptr;

			if (IsEqualIID(riid, __uuidof(IUnknown)) ||
				IsSupportedWrapperIid(mConfiguration, riid, IsD3D12SDKConfigurationIid)) {
				*ppvObject = GetAuxiliaryWrapperInterface(riid);
				if (*ppvObject) {
					AddRef();
					return S_OK;
				}
			}

			HRESULT hr = mConfiguration->QueryInterface(riid, ppvObject);
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

		HRESULT STDMETHODCALLTYPE SetSDKVersion(UINT sdkVersion, LPCSTR sdkPath) override
		{
			return mConfiguration->SetSDKVersion(sdkVersion, sdkPath);
		}

		HRESULT STDMETHODCALLTYPE CreateDeviceFactory(
			UINT sdkVersion, LPCSTR sdkPath, REFIID riid, void **factory) override
		{
			ID3D12SDKConfiguration1 *configuration1 = nullptr;
			HRESULT hr = mConfiguration->QueryInterface(IID_PPV_ARGS(&configuration1));
			if (FAILED(hr))
				return hr;
			hr = configuration1->CreateDeviceFactory(sdkVersion, sdkPath, riid, factory);
			if (SUCCEEDED(hr) && factory && *factory)
				DX12Wrapper::WrapD3D12DeviceFactoryResult(riid, factory);
			configuration1->Release();
			return hr;
		}

		void STDMETHODCALLTYPE FreeUnusedSDKs() override
		{
			ID3D12SDKConfiguration1 *configuration1 = nullptr;
			if (SUCCEEDED(mConfiguration->QueryInterface(IID_PPV_ARGS(&configuration1)))) {
				configuration1->FreeUnusedSDKs();
				configuration1->Release();
			}
		}

	public:
		std::atomic<long> mRefCount{1};
		ID3D12SDKConfiguration *mConfiguration = nullptr;
	};

	class SharingContractWrapper final : public ID3D12SharingContract, public WrapperBase
	{
	public:
		explicit SharingContractWrapper(ID3D12SharingContract *contract)
			: WrapperBase(contract, false)
		{
			mContract = contract;
			RegisterWrapperPointer(static_cast<ID3D12SharingContract*>(this));
			DX12_LOG("DX12Wrapper",
				"kind=sharing_contract,stage=create,wrapper=%p,object=%p",
				this, contract);
		}

		void *GetAuxiliaryWrapperInterface(REFIID riid)
		{
			if (IsEqualIID(riid, __uuidof(IUnknown)) || IsEqualIID(riid, __uuidof(ID3D12SharingContract)))
				return static_cast<ID3D12SharingContract*>(this);
			return nullptr;
		}

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) override
		{
			if (!ppvObject)
				return E_POINTER;
			*ppvObject = nullptr;

			if (IsEqualIID(riid, __uuidof(IUnknown)) || IsSupportedWrapperIid(mContract, riid, IsD3D12SharingContractIid)) {
				*ppvObject = static_cast<ID3D12SharingContract*>(this);
				AddRef();
				return S_OK;
			}

			HRESULT hr = mContract->QueryInterface(riid, ppvObject);
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

		void STDMETHODCALLTYPE Present(ID3D12Resource *resource, UINT subresource, HWND window) override
		{
			mContract->Present(
				reinterpret_cast<ID3D12Resource*>(DX12Wrapper::Unwrap(resource)), subresource, window);
		}

		void STDMETHODCALLTYPE SharedFenceSignal(ID3D12Fence *fence, UINT64 fenceValue) override
		{
			mContract->SharedFenceSignal(
				reinterpret_cast<ID3D12Fence*>(DX12Wrapper::Unwrap(fence)), fenceValue);
		}

		void STDMETHODCALLTYPE BeginCapturableWork(REFGUID guid) override
		{
			mContract->BeginCapturableWork(guid);
		}

		void STDMETHODCALLTYPE EndCapturableWork(REFGUID guid) override
		{
			mContract->EndCapturableWork(guid);
		}

	public:
		std::atomic<long> mRefCount{1};
		ID3D12SharingContract *mContract = nullptr;
	};

	class ManualWriteTrackingResourceWrapper final : public ID3D12ManualWriteTrackingResource, public WrapperBase
	{
	public:
		explicit ManualWriteTrackingResourceWrapper(ID3D12ManualWriteTrackingResource *resource)
			: WrapperBase(resource, false)
		{
			mTrackingResource = resource;
			RegisterWrapperPointer(static_cast<ID3D12ManualWriteTrackingResource*>(this));
			DX12_LOG("DX12Wrapper",
				"kind=manual_write_tracking_resource,stage=create,wrapper=%p,object=%p",
				this, resource);
		}

		void *GetAuxiliaryWrapperInterface(REFIID riid)
		{
			if (IsEqualIID(riid, __uuidof(IUnknown)) ||
				IsEqualIID(riid, __uuidof(ID3D12ManualWriteTrackingResource)))
				return static_cast<ID3D12ManualWriteTrackingResource*>(this);
			return nullptr;
		}

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) override
		{
			if (!ppvObject)
				return E_POINTER;
			*ppvObject = nullptr;

			if (IsEqualIID(riid, __uuidof(IUnknown)) ||
				IsSupportedWrapperIid(mTrackingResource, riid, IsD3D12ManualWriteTrackingResourceIid)) {
				*ppvObject = static_cast<ID3D12ManualWriteTrackingResource*>(this);
				AddRef();
				return S_OK;
			}

			HRESULT hr = mTrackingResource->QueryInterface(riid, ppvObject);
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

		void STDMETHODCALLTYPE TrackWrite(UINT subresource, const D3D12_RANGE *writtenRange) override
		{
			mTrackingResource->TrackWrite(subresource, writtenRange);
		}

	public:
		std::atomic<long> mRefCount{1};
		ID3D12ManualWriteTrackingResource *mTrackingResource = nullptr;
	};

	class InfoQueueWrapper final : public ID3D12InfoQueue1, public WrapperBase
	{
	public:
		explicit InfoQueueWrapper(ID3D12InfoQueue *queue)
			: WrapperBase(queue, false)
		{
			mInfoQueue = queue;
			RegisterWrapperPointer(static_cast<ID3D12InfoQueue*>(this));
			RegisterWrapperPointer(static_cast<ID3D12InfoQueue1*>(this));
			DX12_LOG("DX12Wrapper",
				"kind=info_queue,stage=create,wrapper=%p,object=%p",
				this, queue);
		}

		void *GetAuxiliaryWrapperInterface(REFIID riid)
		{
			if (IsEqualIID(riid, __uuidof(IUnknown)) || IsEqualIID(riid, __uuidof(ID3D12InfoQueue1)))
				return static_cast<ID3D12InfoQueue1*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12InfoQueue)))
				return static_cast<ID3D12InfoQueue*>(this);
			return nullptr;
		}

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) override
		{
			if (!ppvObject)
				return E_POINTER;
			*ppvObject = nullptr;

			if (IsEqualIID(riid, __uuidof(IUnknown)) || IsSupportedWrapperIid(mInfoQueue, riid, IsD3D12InfoQueueIid)) {
				*ppvObject = GetAuxiliaryWrapperInterface(riid);
				if (*ppvObject) {
					AddRef();
					return S_OK;
				}
			}

			HRESULT hr = mInfoQueue->QueryInterface(riid, ppvObject);
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

		HRESULT STDMETHODCALLTYPE SetMessageCountLimit(UINT64 messageCountLimit) override
		{
			return mInfoQueue->SetMessageCountLimit(messageCountLimit);
		}

		void STDMETHODCALLTYPE ClearStoredMessages() override
		{
			mInfoQueue->ClearStoredMessages();
		}

		HRESULT STDMETHODCALLTYPE GetMessage(
			UINT64 messageIndex, D3D12_MESSAGE *message, SIZE_T *messageByteLength) override
		{
			return mInfoQueue->GetMessage(messageIndex, message, messageByteLength);
		}

		UINT64 STDMETHODCALLTYPE GetNumMessagesAllowedByStorageFilter() override
		{
			return mInfoQueue->GetNumMessagesAllowedByStorageFilter();
		}

		UINT64 STDMETHODCALLTYPE GetNumMessagesDeniedByStorageFilter() override
		{
			return mInfoQueue->GetNumMessagesDeniedByStorageFilter();
		}

		UINT64 STDMETHODCALLTYPE GetNumStoredMessages() override
		{
			return mInfoQueue->GetNumStoredMessages();
		}

		UINT64 STDMETHODCALLTYPE GetNumStoredMessagesAllowedByRetrievalFilter() override
		{
			return mInfoQueue->GetNumStoredMessagesAllowedByRetrievalFilter();
		}

		UINT64 STDMETHODCALLTYPE GetNumMessagesDiscardedByMessageCountLimit() override
		{
			return mInfoQueue->GetNumMessagesDiscardedByMessageCountLimit();
		}

		UINT64 STDMETHODCALLTYPE GetMessageCountLimit() override
		{
			return mInfoQueue->GetMessageCountLimit();
		}

		HRESULT STDMETHODCALLTYPE AddStorageFilterEntries(D3D12_INFO_QUEUE_FILTER *filter) override
		{
			return mInfoQueue->AddStorageFilterEntries(filter);
		}

		HRESULT STDMETHODCALLTYPE GetStorageFilter(
			D3D12_INFO_QUEUE_FILTER *filter, SIZE_T *filterByteLength) override
		{
			return mInfoQueue->GetStorageFilter(filter, filterByteLength);
		}

		void STDMETHODCALLTYPE ClearStorageFilter() override
		{
			mInfoQueue->ClearStorageFilter();
		}

		HRESULT STDMETHODCALLTYPE PushEmptyStorageFilter() override
		{
			return mInfoQueue->PushEmptyStorageFilter();
		}

		HRESULT STDMETHODCALLTYPE PushCopyOfStorageFilter() override
		{
			return mInfoQueue->PushCopyOfStorageFilter();
		}

		HRESULT STDMETHODCALLTYPE PushStorageFilter(D3D12_INFO_QUEUE_FILTER *filter) override
		{
			return mInfoQueue->PushStorageFilter(filter);
		}

		void STDMETHODCALLTYPE PopStorageFilter() override
		{
			mInfoQueue->PopStorageFilter();
		}

		UINT STDMETHODCALLTYPE GetStorageFilterStackSize() override
		{
			return mInfoQueue->GetStorageFilterStackSize();
		}

		HRESULT STDMETHODCALLTYPE AddRetrievalFilterEntries(D3D12_INFO_QUEUE_FILTER *filter) override
		{
			return mInfoQueue->AddRetrievalFilterEntries(filter);
		}

		HRESULT STDMETHODCALLTYPE GetRetrievalFilter(
			D3D12_INFO_QUEUE_FILTER *filter, SIZE_T *filterByteLength) override
		{
			return mInfoQueue->GetRetrievalFilter(filter, filterByteLength);
		}

		void STDMETHODCALLTYPE ClearRetrievalFilter() override
		{
			mInfoQueue->ClearRetrievalFilter();
		}

		HRESULT STDMETHODCALLTYPE PushEmptyRetrievalFilter() override
		{
			return mInfoQueue->PushEmptyRetrievalFilter();
		}

		HRESULT STDMETHODCALLTYPE PushCopyOfRetrievalFilter() override
		{
			return mInfoQueue->PushCopyOfRetrievalFilter();
		}

		HRESULT STDMETHODCALLTYPE PushRetrievalFilter(D3D12_INFO_QUEUE_FILTER *filter) override
		{
			return mInfoQueue->PushRetrievalFilter(filter);
		}

		void STDMETHODCALLTYPE PopRetrievalFilter() override
		{
			mInfoQueue->PopRetrievalFilter();
		}

		UINT STDMETHODCALLTYPE GetRetrievalFilterStackSize() override
		{
			return mInfoQueue->GetRetrievalFilterStackSize();
		}

		HRESULT STDMETHODCALLTYPE AddMessage(
			D3D12_MESSAGE_CATEGORY category, D3D12_MESSAGE_SEVERITY severity,
			D3D12_MESSAGE_ID id, LPCSTR description) override
		{
			return mInfoQueue->AddMessage(category, severity, id, description);
		}

		HRESULT STDMETHODCALLTYPE AddApplicationMessage(
			D3D12_MESSAGE_SEVERITY severity, LPCSTR description) override
		{
			return mInfoQueue->AddApplicationMessage(severity, description);
		}

		HRESULT STDMETHODCALLTYPE SetBreakOnCategory(D3D12_MESSAGE_CATEGORY category, BOOL enable) override
		{
			return mInfoQueue->SetBreakOnCategory(category, enable);
		}

		HRESULT STDMETHODCALLTYPE SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY severity, BOOL enable) override
		{
			return mInfoQueue->SetBreakOnSeverity(severity, enable);
		}

		HRESULT STDMETHODCALLTYPE SetBreakOnID(D3D12_MESSAGE_ID id, BOOL enable) override
		{
			return mInfoQueue->SetBreakOnID(id, enable);
		}

		BOOL STDMETHODCALLTYPE GetBreakOnCategory(D3D12_MESSAGE_CATEGORY category) override
		{
			return mInfoQueue->GetBreakOnCategory(category);
		}

		BOOL STDMETHODCALLTYPE GetBreakOnSeverity(D3D12_MESSAGE_SEVERITY severity) override
		{
			return mInfoQueue->GetBreakOnSeverity(severity);
		}

		BOOL STDMETHODCALLTYPE GetBreakOnID(D3D12_MESSAGE_ID id) override
		{
			return mInfoQueue->GetBreakOnID(id);
		}

		void STDMETHODCALLTYPE SetMuteDebugOutput(BOOL mute) override
		{
			mInfoQueue->SetMuteDebugOutput(mute);
		}

		BOOL STDMETHODCALLTYPE GetMuteDebugOutput() override
		{
			return mInfoQueue->GetMuteDebugOutput();
		}

		HRESULT STDMETHODCALLTYPE RegisterMessageCallback(
			D3D12MessageFunc callbackFunc, D3D12_MESSAGE_CALLBACK_FLAGS callbackFilterFlags,
			void *context, DWORD *callbackCookie) override
		{
			ID3D12InfoQueue1 *queue1 = nullptr;
			HRESULT hr = mInfoQueue->QueryInterface(IID_PPV_ARGS(&queue1));
			if (FAILED(hr))
				return hr;
			hr = queue1->RegisterMessageCallback(
				callbackFunc, callbackFilterFlags, context, callbackCookie);
			queue1->Release();
			return hr;
		}

		HRESULT STDMETHODCALLTYPE UnregisterMessageCallback(DWORD callbackCookie) override
		{
			ID3D12InfoQueue1 *queue1 = nullptr;
			HRESULT hr = mInfoQueue->QueryInterface(IID_PPV_ARGS(&queue1));
			if (FAILED(hr))
				return hr;
			hr = queue1->UnregisterMessageCallback(callbackCookie);
			queue1->Release();
			return hr;
		}

	public:
		std::atomic<long> mRefCount{1};
		ID3D12InfoQueue *mInfoQueue = nullptr;
	};

	class DebugDeviceWrapper final :
		public ID3D12DebugDevice2,
		public ID3D12DebugDevice1,
		public WrapperBase
	{
	public:
		explicit DebugDeviceWrapper(ID3D12DebugDevice *device)
			: WrapperBase(device, false)
		{
			mDebugDevice = device;
			RegisterWrapperPointer(static_cast<ID3D12DebugDevice*>(static_cast<ID3D12DebugDevice2*>(this)));
			RegisterWrapperPointer(static_cast<ID3D12DebugDevice1*>(this));
			RegisterWrapperPointer(static_cast<ID3D12DebugDevice2*>(this));
			DX12_LOG("DX12Wrapper",
				"kind=debug_device,stage=create,wrapper=%p,object=%p",
				this, device);
		}

		void *GetAuxiliaryWrapperInterface(REFIID riid)
		{
			if (IsEqualIID(riid, __uuidof(IUnknown)) || IsEqualIID(riid, __uuidof(ID3D12DebugDevice2)))
				return static_cast<ID3D12DebugDevice2*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12DebugDevice1)))
				return static_cast<ID3D12DebugDevice1*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12DebugDevice)))
				return static_cast<ID3D12DebugDevice*>(static_cast<ID3D12DebugDevice2*>(this));
			return nullptr;
		}

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) override
		{
			if (!ppvObject)
				return E_POINTER;
			*ppvObject = nullptr;

			if (IsEqualIID(riid, __uuidof(IUnknown)) ||
				IsSupportedWrapperIid(mDebugDevice, riid, IsD3D12DebugDeviceIid)) {
				*ppvObject = GetAuxiliaryWrapperInterface(riid);
				if (*ppvObject) {
					AddRef();
					return S_OK;
				}
			}

			HRESULT hr = mDebugDevice->QueryInterface(riid, ppvObject);
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

		HRESULT STDMETHODCALLTYPE SetFeatureMask(D3D12_DEBUG_FEATURE mask) override
		{
			return mDebugDevice->SetFeatureMask(mask);
		}

		D3D12_DEBUG_FEATURE STDMETHODCALLTYPE GetFeatureMask() override
		{
			return mDebugDevice->GetFeatureMask();
		}

		HRESULT STDMETHODCALLTYPE ReportLiveDeviceObjects(D3D12_RLDO_FLAGS flags) override
		{
			return mDebugDevice->ReportLiveDeviceObjects(flags);
		}

		HRESULT STDMETHODCALLTYPE SetDebugParameter(
			D3D12_DEBUG_DEVICE_PARAMETER_TYPE type, const void *data, UINT dataSize) override
		{
			ID3D12DebugDevice2 *device2 = nullptr;
			HRESULT hr = mDebugDevice->QueryInterface(IID_PPV_ARGS(&device2));
			if (FAILED(hr)) {
				ID3D12DebugDevice1 *device1 = nullptr;
				hr = mDebugDevice->QueryInterface(IID_PPV_ARGS(&device1));
				if (FAILED(hr))
					return hr;
				hr = device1->SetDebugParameter(type, data, dataSize);
				device1->Release();
				return hr;
			}
			hr = device2->SetDebugParameter(type, data, dataSize);
			device2->Release();
			return hr;
		}

		HRESULT STDMETHODCALLTYPE GetDebugParameter(
			D3D12_DEBUG_DEVICE_PARAMETER_TYPE type, void *data, UINT dataSize) override
		{
			ID3D12DebugDevice2 *device2 = nullptr;
			HRESULT hr = mDebugDevice->QueryInterface(IID_PPV_ARGS(&device2));
			if (FAILED(hr)) {
				ID3D12DebugDevice1 *device1 = nullptr;
				hr = mDebugDevice->QueryInterface(IID_PPV_ARGS(&device1));
				if (FAILED(hr))
					return hr;
				hr = device1->GetDebugParameter(type, data, dataSize);
				device1->Release();
				return hr;
			}
			hr = device2->GetDebugParameter(type, data, dataSize);
			device2->Release();
			return hr;
		}

	public:
		std::atomic<long> mRefCount{1};
		ID3D12DebugDevice *mDebugDevice = nullptr;
	};

	class DebugCommandQueueWrapper final : public ID3D12DebugCommandQueue1, public WrapperBase
	{
	public:
		explicit DebugCommandQueueWrapper(ID3D12DebugCommandQueue *queue)
			: WrapperBase(queue, false)
		{
			mDebugQueue = queue;
			RegisterWrapperPointer(static_cast<ID3D12DebugCommandQueue*>(this));
			RegisterWrapperPointer(static_cast<ID3D12DebugCommandQueue1*>(this));
			DX12_LOG("DX12Wrapper",
				"kind=debug_command_queue,stage=create,wrapper=%p,object=%p",
				this, queue);
		}

		void *GetAuxiliaryWrapperInterface(REFIID riid)
		{
			if (IsEqualIID(riid, __uuidof(IUnknown)) || IsEqualIID(riid, __uuidof(ID3D12DebugCommandQueue1)))
				return static_cast<ID3D12DebugCommandQueue1*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12DebugCommandQueue)))
				return static_cast<ID3D12DebugCommandQueue*>(this);
			return nullptr;
		}

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) override
		{
			if (!ppvObject)
				return E_POINTER;
			*ppvObject = nullptr;

			if (IsEqualIID(riid, __uuidof(IUnknown)) ||
				IsSupportedWrapperIid(mDebugQueue, riid, IsD3D12DebugCommandQueueIid)) {
				*ppvObject = GetAuxiliaryWrapperInterface(riid);
				if (*ppvObject) {
					AddRef();
					return S_OK;
				}
			}

			HRESULT hr = mDebugQueue->QueryInterface(riid, ppvObject);
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

		BOOL STDMETHODCALLTYPE AssertResourceState(
			ID3D12Resource *resource, UINT subresource, UINT state) override
		{
			return mDebugQueue->AssertResourceState(
				reinterpret_cast<ID3D12Resource*>(DX12Wrapper::Unwrap(resource)), subresource, state);
		}

		void STDMETHODCALLTYPE AssertResourceAccess(
			ID3D12Resource *resource, UINT subresource, D3D12_BARRIER_ACCESS access) override
		{
			ID3D12DebugCommandQueue1 *queue1 = nullptr;
			if (SUCCEEDED(mDebugQueue->QueryInterface(IID_PPV_ARGS(&queue1)))) {
				queue1->AssertResourceAccess(
					reinterpret_cast<ID3D12Resource*>(DX12Wrapper::Unwrap(resource)), subresource, access);
				queue1->Release();
			}
		}

		void STDMETHODCALLTYPE AssertTextureLayout(
			ID3D12Resource *resource, UINT subresource, D3D12_BARRIER_LAYOUT layout) override
		{
			ID3D12DebugCommandQueue1 *queue1 = nullptr;
			if (SUCCEEDED(mDebugQueue->QueryInterface(IID_PPV_ARGS(&queue1)))) {
				queue1->AssertTextureLayout(
					reinterpret_cast<ID3D12Resource*>(DX12Wrapper::Unwrap(resource)), subresource, layout);
				queue1->Release();
			}
		}

	public:
		std::atomic<long> mRefCount{1};
		ID3D12DebugCommandQueue *mDebugQueue = nullptr;
	};

	class DebugCommandListWrapper final :
		public ID3D12DebugCommandList3,
		public ID3D12DebugCommandList1,
		public WrapperBase
	{
	public:
		explicit DebugCommandListWrapper(ID3D12DebugCommandList *commandList)
			: WrapperBase(commandList, false)
		{
			mDebugCommandList = commandList;
			RegisterWrapperPointer(static_cast<ID3D12DebugCommandList*>(static_cast<ID3D12DebugCommandList3*>(this)));
			RegisterWrapperPointer(static_cast<ID3D12DebugCommandList1*>(this));
			RegisterWrapperPointer(static_cast<ID3D12DebugCommandList2*>(this));
			RegisterWrapperPointer(static_cast<ID3D12DebugCommandList3*>(this));
			DX12_LOG("DX12Wrapper",
				"kind=debug_command_list,stage=create,wrapper=%p,object=%p",
				this, commandList);
		}

		void *GetAuxiliaryWrapperInterface(REFIID riid)
		{
			if (IsEqualIID(riid, __uuidof(IUnknown)) || IsEqualIID(riid, __uuidof(ID3D12DebugCommandList3)))
				return static_cast<ID3D12DebugCommandList3*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12DebugCommandList2)))
				return static_cast<ID3D12DebugCommandList2*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12DebugCommandList1)))
				return static_cast<ID3D12DebugCommandList1*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12DebugCommandList)))
				return static_cast<ID3D12DebugCommandList*>(static_cast<ID3D12DebugCommandList3*>(this));
			return nullptr;
		}

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) override
		{
			if (!ppvObject)
				return E_POINTER;
			*ppvObject = nullptr;

			if (IsEqualIID(riid, __uuidof(IUnknown)) ||
				IsSupportedWrapperIid(mDebugCommandList, riid, IsD3D12DebugCommandListIid)) {
				*ppvObject = GetAuxiliaryWrapperInterface(riid);
				if (*ppvObject) {
					AddRef();
					return S_OK;
				}
			}

			HRESULT hr = mDebugCommandList->QueryInterface(riid, ppvObject);
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

		BOOL STDMETHODCALLTYPE AssertResourceState(
			ID3D12Resource *resource, UINT subresource, UINT state) override
		{
			return mDebugCommandList->AssertResourceState(
				reinterpret_cast<ID3D12Resource*>(DX12Wrapper::Unwrap(resource)), subresource, state);
		}

		HRESULT STDMETHODCALLTYPE SetFeatureMask(D3D12_DEBUG_FEATURE mask) override
		{
			return mDebugCommandList->SetFeatureMask(mask);
		}

		D3D12_DEBUG_FEATURE STDMETHODCALLTYPE GetFeatureMask() override
		{
			return mDebugCommandList->GetFeatureMask();
		}

		HRESULT STDMETHODCALLTYPE SetDebugParameter(
			D3D12_DEBUG_COMMAND_LIST_PARAMETER_TYPE type, const void *data, UINT dataSize) override
		{
			ID3D12DebugCommandList2 *list2 = nullptr;
			HRESULT hr = mDebugCommandList->QueryInterface(IID_PPV_ARGS(&list2));
			if (FAILED(hr)) {
				ID3D12DebugCommandList1 *list1 = nullptr;
				hr = mDebugCommandList->QueryInterface(IID_PPV_ARGS(&list1));
				if (FAILED(hr))
					return hr;
				hr = list1->SetDebugParameter(type, data, dataSize);
				list1->Release();
				return hr;
			}
			hr = list2->SetDebugParameter(type, data, dataSize);
			list2->Release();
			return hr;
		}

		HRESULT STDMETHODCALLTYPE GetDebugParameter(
			D3D12_DEBUG_COMMAND_LIST_PARAMETER_TYPE type, void *data, UINT dataSize) override
		{
			ID3D12DebugCommandList2 *list2 = nullptr;
			HRESULT hr = mDebugCommandList->QueryInterface(IID_PPV_ARGS(&list2));
			if (FAILED(hr)) {
				ID3D12DebugCommandList1 *list1 = nullptr;
				hr = mDebugCommandList->QueryInterface(IID_PPV_ARGS(&list1));
				if (FAILED(hr))
					return hr;
				hr = list1->GetDebugParameter(type, data, dataSize);
				list1->Release();
				return hr;
			}
			hr = list2->GetDebugParameter(type, data, dataSize);
			list2->Release();
			return hr;
		}

		void STDMETHODCALLTYPE AssertResourceAccess(
			ID3D12Resource *resource, UINT subresource, D3D12_BARRIER_ACCESS access) override
		{
			ID3D12DebugCommandList3 *list3 = nullptr;
			if (SUCCEEDED(mDebugCommandList->QueryInterface(IID_PPV_ARGS(&list3)))) {
				list3->AssertResourceAccess(
					reinterpret_cast<ID3D12Resource*>(DX12Wrapper::Unwrap(resource)), subresource, access);
				list3->Release();
			}
		}

		void STDMETHODCALLTYPE AssertTextureLayout(
			ID3D12Resource *resource, UINT subresource, D3D12_BARRIER_LAYOUT layout) override
		{
			ID3D12DebugCommandList3 *list3 = nullptr;
			if (SUCCEEDED(mDebugCommandList->QueryInterface(IID_PPV_ARGS(&list3)))) {
				list3->AssertTextureLayout(
					reinterpret_cast<ID3D12Resource*>(DX12Wrapper::Unwrap(resource)), subresource, layout);
				list3->Release();
			}
		}

	public:
		std::atomic<long> mRefCount{1};
		ID3D12DebugCommandList *mDebugCommandList = nullptr;
	};


} // namespace DX12WrapperDetail
