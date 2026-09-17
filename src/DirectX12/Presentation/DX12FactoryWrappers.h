// Device factory, DXGI factory and swap chain wrappers.
// Split from DX12Wrapper.cpp; COM wrapper class definitions (inline).
#pragma once

#include "DX12WrapperCore.h"

namespace DX12WrapperDetail
{

	class DeviceFactoryWrapper final : public ID3D12DeviceFactory, public WrapperBase
	{
	public:
		explicit DeviceFactoryWrapper(ID3D12DeviceFactory *factory)
			: WrapperBase(factory)
		{
			mFactory = factory;
			RegisterWrapperPointer(static_cast<ID3D12DeviceFactory*>(this));
			DX12_LOG("DX12Wrapper",
				"kind=device_factory,stage=create,wrapper=%p,object=%p",
				this, factory);
		}

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) override
		{
			if (!ppvObject)
				return E_POINTER;
			*ppvObject = nullptr;

			if (IsEqualIID(riid, __uuidof(IUnknown)) || IsSupportedWrapperIid(mFactory, riid, IsD3D12DeviceFactoryIid)) {
				*ppvObject = static_cast<ID3D12DeviceFactory*>(this);
				AddRef();
				return S_OK;
			}

			HRESULT hr = mFactory->QueryInterface(riid, ppvObject);
			if (SUCCEEDED(hr))
				DX12Wrapper::WrapD3D12DeviceFactoryResult(riid, ppvObject);
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

		HRESULT STDMETHODCALLTYPE InitializeFromGlobalState() override
		{
			return mFactory->InitializeFromGlobalState();
		}

		HRESULT STDMETHODCALLTYPE ApplyToGlobalState() override
		{
			return mFactory->ApplyToGlobalState();
		}

		HRESULT STDMETHODCALLTYPE SetFlags(D3D12_DEVICE_FACTORY_FLAGS flags) override
		{
			return mFactory->SetFlags(flags);
		}

		D3D12_DEVICE_FACTORY_FLAGS STDMETHODCALLTYPE GetFlags() override
		{
			return mFactory->GetFlags();
		}

		HRESULT STDMETHODCALLTYPE GetConfigurationInterface(REFCLSID clsid, REFIID iid, void **ppv) override
		{
			return mFactory->GetConfigurationInterface(clsid, iid, ppv);
		}

		HRESULT STDMETHODCALLTYPE EnableExperimentalFeatures(
			UINT numFeatures, const IID *iids, void *configurationStructs, UINT *configurationStructSizes) override
		{
			return mFactory->EnableExperimentalFeatures(
				numFeatures, iids, configurationStructs, configurationStructSizes);
		}

		HRESULT STDMETHODCALLTYPE CreateDevice(
			IUnknown *adapter, D3D_FEATURE_LEVEL featureLevel, REFIID riid, void **ppvDevice) override
		{
			HRESULT hr = mFactory->CreateDevice(
				DX12Wrapper::Unwrap(adapter), featureLevel, riid, ppvDevice);
			if (SUCCEEDED(hr))
				DX12Wrapper::WrapD3D12DeviceResult(riid, ppvDevice);
			return hr;
		}

	public:
		std::atomic<long> mRefCount{1};
		ID3D12DeviceFactory *mFactory = nullptr;
	};

class FactoryWrapper final : public IDXGIFactory7, public WrapperBase
	{
	public:
		explicit FactoryWrapper(IDXGIFactory *factory)
			: WrapperBase(factory)
		{
			mFactory = factory;
			RegisterWrapperPointer(static_cast<IDXGIObject*>(this));
			RegisterWrapperPointer(static_cast<IDXGIFactory*>(this));
			RegisterWrapperPointer(static_cast<IDXGIFactory1*>(this));
			RegisterWrapperPointer(static_cast<IDXGIFactory2*>(this));
			RegisterWrapperPointer(static_cast<IDXGIFactory3*>(this));
			RegisterWrapperPointer(static_cast<IDXGIFactory4*>(this));
			RegisterWrapperPointer(static_cast<IDXGIFactory5*>(this));
			RegisterWrapperPointer(static_cast<IDXGIFactory6*>(this));
			RegisterWrapperPointer(static_cast<IDXGIFactory7*>(this));
			DX12_LOG("DX12Wrapper",
				"kind=factory,stage=create,wrapper=%p,object=%p",
				this, factory);
		}

		HRESULT QueryFactory1(IDXGIFactory1 **factory)
		{
			return mFactory->QueryInterface(IID_PPV_ARGS(factory));
		}

		HRESULT QueryFactory2(IDXGIFactory2 **factory)
		{
			return mFactory->QueryInterface(IID_PPV_ARGS(factory));
		}

		HRESULT QueryFactory3(IDXGIFactory3 **factory)
		{
			return mFactory->QueryInterface(IID_PPV_ARGS(factory));
		}

		HRESULT QueryFactory4(IDXGIFactory4 **factory)
		{
			return mFactory->QueryInterface(IID_PPV_ARGS(factory));
		}

		HRESULT QueryFactory5(IDXGIFactory5 **factory)
		{
			return mFactory->QueryInterface(IID_PPV_ARGS(factory));
		}

		HRESULT QueryFactory6(IDXGIFactory6 **factory)
		{
			return mFactory->QueryInterface(IID_PPV_ARGS(factory));
		}

		HRESULT QueryFactory7(IDXGIFactory7 **factory)
		{
			return mFactory->QueryInterface(IID_PPV_ARGS(factory));
		}

		void *GetFactoryWrapperInterface(REFIID riid)
		{
			if (IsEqualIID(riid, __uuidof(IUnknown)) || IsEqualIID(riid, __uuidof(IDXGIFactory7)))
				return static_cast<IDXGIFactory7*>(this);
			if (IsEqualIID(riid, __uuidof(IDXGIFactory6)))
				return static_cast<IDXGIFactory6*>(this);
			if (IsEqualIID(riid, __uuidof(IDXGIFactory5)))
				return static_cast<IDXGIFactory5*>(this);
			if (IsEqualIID(riid, __uuidof(IDXGIFactory4)))
				return static_cast<IDXGIFactory4*>(this);
			if (IsEqualIID(riid, __uuidof(IDXGIFactory3)))
				return static_cast<IDXGIFactory3*>(this);
			if (IsEqualIID(riid, __uuidof(IDXGIFactory2)))
				return static_cast<IDXGIFactory2*>(this);
			if (IsEqualIID(riid, __uuidof(IDXGIFactory1)))
				return static_cast<IDXGIFactory1*>(this);
			if (IsEqualIID(riid, __uuidof(IDXGIFactory)))
				return static_cast<IDXGIFactory*>(this);
			if (IsEqualIID(riid, __uuidof(IDXGIObject)))
				return static_cast<IDXGIObject*>(this);
			return nullptr;
		}

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) override
		{
			if (!ppvObject)
				return E_POINTER;
			*ppvObject = nullptr;

			if (IsEqualIID(riid, __uuidof(IUnknown)) || IsSupportedWrapperIid(mFactory, riid, IsDxgiFactoryWrapperIid)) {
				*ppvObject = GetFactoryWrapperInterface(riid);
				if (*ppvObject) {
					AddRef();
					return S_OK;
				}
			}

			HRESULT hr = mFactory->QueryInterface(riid, ppvObject);
			if (SUCCEEDED(hr))
				DX12Wrapper::WrapDXGIFactoryResult(riid, ppvObject);
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

		HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID Name, UINT DataSize, const void *pData) override
		{
			return mFactory->SetPrivateData(Name, DataSize, pData);
		}

		HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID Name, const IUnknown *pUnknown) override
		{
			return mFactory->SetPrivateDataInterface(Name, pUnknown);
		}

		HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID Name, UINT *pDataSize, void *pData) override
		{
			return mFactory->GetPrivateData(Name, pDataSize, pData);
		}

		HRESULT STDMETHODCALLTYPE GetParent(REFIID riid, void **ppParent) override
		{
			HRESULT hr = mFactory->GetParent(riid, ppParent);
			if (SUCCEEDED(hr))
				DX12Wrapper::WrapDXGIFactoryResult(riid, ppParent);
			return hr;
		}

		HRESULT STDMETHODCALLTYPE EnumAdapters(UINT Adapter, IDXGIAdapter **ppAdapter) override
		{
			return mFactory->EnumAdapters(Adapter, ppAdapter);
		}

		HRESULT STDMETHODCALLTYPE MakeWindowAssociation(HWND WindowHandle, UINT Flags) override
		{
			return mFactory->MakeWindowAssociation(WindowHandle, Flags);
		}

		HRESULT STDMETHODCALLTYPE GetWindowAssociation(HWND *pWindowHandle) override
		{
			return mFactory->GetWindowAssociation(pWindowHandle);
		}

		HRESULT STDMETHODCALLTYPE CreateSwapChain(IUnknown *pDevice, DXGI_SWAP_CHAIN_DESC *pDesc, IDXGISwapChain **ppSwapChain) override
		{
			HRESULT hr = mFactory->CreateSwapChain(DX12Wrapper::Unwrap(pDevice), pDesc, ppSwapChain);
			if (SUCCEEDED(hr))
				DX12Wrapper::WrapDXGISwapChainResult(__uuidof(IDXGISwapChain), reinterpret_cast<void**>(ppSwapChain));
			return hr;
		}

		HRESULT STDMETHODCALLTYPE CreateSoftwareAdapter(HMODULE Module, IDXGIAdapter **ppAdapter) override
		{
			return mFactory->CreateSoftwareAdapter(Module, ppAdapter);
		}

		HRESULT STDMETHODCALLTYPE EnumAdapters1(UINT Adapter, IDXGIAdapter1 **ppAdapter) override
		{
			IDXGIFactory1 *factory1 = nullptr;
			HRESULT hr = QueryFactory1(&factory1);
			if (FAILED(hr))
				return hr;
			hr = factory1->EnumAdapters1(Adapter, ppAdapter);
			factory1->Release();
			return hr;
		}

		BOOL STDMETHODCALLTYPE IsCurrent() override
		{
			IDXGIFactory1 *factory1 = nullptr;
			if (FAILED(QueryFactory1(&factory1)))
				return FALSE;
			BOOL current = factory1->IsCurrent();
			factory1->Release();
			return current;
		}

		BOOL STDMETHODCALLTYPE IsWindowedStereoEnabled() override
		{
			IDXGIFactory2 *factory2 = nullptr;
			if (FAILED(QueryFactory2(&factory2)))
				return FALSE;
			BOOL enabled = factory2->IsWindowedStereoEnabled();
			factory2->Release();
			return enabled;
		}

		HRESULT STDMETHODCALLTYPE CreateSwapChainForHwnd(
			IUnknown *pDevice, HWND hWnd, const DXGI_SWAP_CHAIN_DESC1 *pDesc,
			const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *pFullscreenDesc,
			IDXGIOutput *pRestrictToOutput, IDXGISwapChain1 **ppSwapChain) override
		{
			IDXGIFactory2 *factory2 = nullptr;
			HRESULT hr = QueryFactory2(&factory2);
			if (FAILED(hr))
				return hr;
			// Dialect flags matter here: frame generation recreates the swap
			// chain with the frame-latency waitable object, and a stall inside
			// this call has no other trace in the log - the wrapper's own
			// "kind=swapchain,stage=create" line only prints after the driver
			// returns. One line per creation names the flags and the size.
			DX12MarkStage("swapchain_create_enter");
			DX12_LOG("DX12SwapChainCreate",
				"\"api\":\"CreateSwapChainForHwnd\",\"hwnd\":\"%p\",\"width\":%u,\"height\":%u,"
				"\"format\":%u,\"buffers\":%u,\"sampleCount\":%u,\"flags\":\"0x%x\",\"swapEffect\":%u",
				hWnd, pDesc ? pDesc->Width : 0, pDesc ? pDesc->Height : 0,
				pDesc ? static_cast<unsigned>(pDesc->Format) : 0,
				pDesc ? pDesc->BufferCount : 0,
				pDesc ? pDesc->SampleDesc.Count : 0,
				pDesc ? pDesc->Flags : 0,
				pDesc ? static_cast<unsigned>(pDesc->SwapEffect) : 0);
			hr = factory2->CreateSwapChainForHwnd(
				DX12Wrapper::Unwrap(pDevice), hWnd, pDesc, pFullscreenDesc, pRestrictToOutput, ppSwapChain);
			DX12MarkStage("swapchain_create_leave");
			DX12_LOG_FLUSH("DX12SwapChainCreate",
				"\"event\":\"result\",\"api\":\"CreateSwapChainForHwnd\",\"hwnd\":\"%p\",\"hr\":\"0x%lx\",\"swapchain\":\"%p\"",
				hWnd, hr, SUCCEEDED(hr) && ppSwapChain ? *ppSwapChain : nullptr);
			if (SUCCEEDED(hr)) {
				DX12MarkStage("swapchain_wrap_call");
				DX12Wrapper::WrapDXGISwapChainResult(__uuidof(IDXGISwapChain1), reinterpret_cast<void**>(ppSwapChain));
			}
			DX12MarkStage("swapchain_factory_release");
			factory2->Release();
			DX12MarkStage("swapchain_create_return");
			return hr;
		}

		HRESULT STDMETHODCALLTYPE CreateSwapChainForCoreWindow(
			IUnknown *pDevice, IUnknown *pWindow, const DXGI_SWAP_CHAIN_DESC1 *pDesc,
			IDXGIOutput *pRestrictToOutput, IDXGISwapChain1 **ppSwapChain) override
		{
			IDXGIFactory2 *factory2 = nullptr;
			HRESULT hr = QueryFactory2(&factory2);
			if (FAILED(hr))
				return hr;
			hr = factory2->CreateSwapChainForCoreWindow(
				DX12Wrapper::Unwrap(pDevice), pWindow, pDesc, pRestrictToOutput, ppSwapChain);
			if (SUCCEEDED(hr))
				DX12Wrapper::WrapDXGISwapChainResult(__uuidof(IDXGISwapChain1), reinterpret_cast<void**>(ppSwapChain));
			factory2->Release();
			return hr;
		}

		HRESULT STDMETHODCALLTYPE GetSharedResourceAdapterLuid(HANDLE hResource, LUID *pLuid) override
		{
			IDXGIFactory2 *factory2 = nullptr;
			HRESULT hr = QueryFactory2(&factory2);
			if (FAILED(hr))
				return hr;
			hr = factory2->GetSharedResourceAdapterLuid(hResource, pLuid);
			factory2->Release();
			return hr;
		}

		HRESULT STDMETHODCALLTYPE RegisterStereoStatusWindow(HWND WindowHandle, UINT wMsg, DWORD *pdwCookie) override
		{
			IDXGIFactory2 *factory2 = nullptr;
			HRESULT hr = QueryFactory2(&factory2);
			if (FAILED(hr))
				return hr;
			hr = factory2->RegisterStereoStatusWindow(WindowHandle, wMsg, pdwCookie);
			factory2->Release();
			return hr;
		}

		HRESULT STDMETHODCALLTYPE RegisterStereoStatusEvent(HANDLE hEvent, DWORD *pdwCookie) override
		{
			IDXGIFactory2 *factory2 = nullptr;
			HRESULT hr = QueryFactory2(&factory2);
			if (FAILED(hr))
				return hr;
			hr = factory2->RegisterStereoStatusEvent(hEvent, pdwCookie);
			factory2->Release();
			return hr;
		}

		void STDMETHODCALLTYPE UnregisterStereoStatus(DWORD dwCookie) override
		{
			IDXGIFactory2 *factory2 = nullptr;
			if (SUCCEEDED(QueryFactory2(&factory2))) {
				factory2->UnregisterStereoStatus(dwCookie);
				factory2->Release();
			}
		}

		HRESULT STDMETHODCALLTYPE RegisterOcclusionStatusWindow(HWND WindowHandle, UINT wMsg, DWORD *pdwCookie) override
		{
			IDXGIFactory2 *factory2 = nullptr;
			HRESULT hr = QueryFactory2(&factory2);
			if (FAILED(hr))
				return hr;
			hr = factory2->RegisterOcclusionStatusWindow(WindowHandle, wMsg, pdwCookie);
			factory2->Release();
			return hr;
		}

		HRESULT STDMETHODCALLTYPE RegisterOcclusionStatusEvent(HANDLE hEvent, DWORD *pdwCookie) override
		{
			IDXGIFactory2 *factory2 = nullptr;
			HRESULT hr = QueryFactory2(&factory2);
			if (FAILED(hr))
				return hr;
			hr = factory2->RegisterOcclusionStatusEvent(hEvent, pdwCookie);
			factory2->Release();
			return hr;
		}

		void STDMETHODCALLTYPE UnregisterOcclusionStatus(DWORD dwCookie) override
		{
			IDXGIFactory2 *factory2 = nullptr;
			if (SUCCEEDED(QueryFactory2(&factory2))) {
				factory2->UnregisterOcclusionStatus(dwCookie);
				factory2->Release();
			}
		}

		HRESULT STDMETHODCALLTYPE CreateSwapChainForComposition(
			IUnknown *pDevice, const DXGI_SWAP_CHAIN_DESC1 *pDesc,
			IDXGIOutput *pRestrictToOutput, IDXGISwapChain1 **ppSwapChain) override
		{
			IDXGIFactory2 *factory2 = nullptr;
			HRESULT hr = QueryFactory2(&factory2);
			if (FAILED(hr))
				return hr;
			hr = factory2->CreateSwapChainForComposition(
				DX12Wrapper::Unwrap(pDevice), pDesc, pRestrictToOutput, ppSwapChain);
			if (SUCCEEDED(hr))
				DX12Wrapper::WrapDXGISwapChainResult(__uuidof(IDXGISwapChain1), reinterpret_cast<void**>(ppSwapChain));
			factory2->Release();
			return hr;
		}

		UINT STDMETHODCALLTYPE GetCreationFlags() override
		{
			IDXGIFactory3 *factory3 = nullptr;
			if (FAILED(QueryFactory3(&factory3)))
				return 0;
			UINT flags = factory3->GetCreationFlags();
			factory3->Release();
			return flags;
		}

		HRESULT STDMETHODCALLTYPE EnumAdapterByLuid(LUID AdapterLuid, REFIID riid, void **ppvAdapter) override
		{
			IDXGIFactory4 *factory4 = nullptr;
			HRESULT hr = QueryFactory4(&factory4);
			if (FAILED(hr))
				return hr;
			hr = factory4->EnumAdapterByLuid(AdapterLuid, riid, ppvAdapter);
			factory4->Release();
			return hr;
		}

		HRESULT STDMETHODCALLTYPE EnumWarpAdapter(REFIID riid, void **ppvAdapter) override
		{
			IDXGIFactory4 *factory4 = nullptr;
			HRESULT hr = QueryFactory4(&factory4);
			if (FAILED(hr))
				return hr;
			hr = factory4->EnumWarpAdapter(riid, ppvAdapter);
			factory4->Release();
			return hr;
		}

		HRESULT STDMETHODCALLTYPE CheckFeatureSupport(
			DXGI_FEATURE Feature, void *pFeatureSupportData, UINT FeatureSupportDataSize) override
		{
			IDXGIFactory5 *factory5 = nullptr;
			HRESULT hr = QueryFactory5(&factory5);
			if (FAILED(hr))
				return hr;
			hr = factory5->CheckFeatureSupport(Feature, pFeatureSupportData, FeatureSupportDataSize);
			factory5->Release();
			return hr;
		}

		HRESULT STDMETHODCALLTYPE EnumAdapterByGpuPreference(
			UINT Adapter, DXGI_GPU_PREFERENCE GpuPreference, REFIID riid, void **ppvAdapter) override
		{
			IDXGIFactory6 *factory6 = nullptr;
			HRESULT hr = QueryFactory6(&factory6);
			if (FAILED(hr))
				return hr;
			hr = factory6->EnumAdapterByGpuPreference(Adapter, GpuPreference, riid, ppvAdapter);
			factory6->Release();
			return hr;
		}

		HRESULT STDMETHODCALLTYPE RegisterAdaptersChangedEvent(HANDLE hEvent, DWORD *pdwCookie) override
		{
			IDXGIFactory7 *factory7 = nullptr;
			HRESULT hr = QueryFactory7(&factory7);
			if (FAILED(hr))
				return hr;
			hr = factory7->RegisterAdaptersChangedEvent(hEvent, pdwCookie);
			factory7->Release();
			return hr;
		}

		HRESULT STDMETHODCALLTYPE UnregisterAdaptersChangedEvent(DWORD dwCookie) override
		{
			IDXGIFactory7 *factory7 = nullptr;
			HRESULT hr = QueryFactory7(&factory7);
			if (FAILED(hr))
				return hr;
			hr = factory7->UnregisterAdaptersChangedEvent(dwCookie);
			factory7->Release();
			return hr;
		}

	public:
		std::atomic<long> mRefCount{1};
		IDXGIFactory *mFactory = nullptr;
	};

	class SwapChainWrapper final : public IDXGISwapChain4, public WrapperBase
	{
	public:
		explicit SwapChainWrapper(IDXGISwapChain *swapChain)
			: WrapperBase(swapChain)
		{
			mSwapChain = swapChain;
			// Present1 is a per-frame hot path. Keep the native v1 interface
			// for the lifetime of the wrapper instead of querying it on every
			// call (the wrapper itself already guarantees this interface).
			if (mSwapChain) {
				mSwapChain->QueryInterface(IID_PPV_ARGS(&mSwapChain1));
				mSwapChain->QueryInterface(IID_PPV_ARGS(&mSwapChain2));
				mSwapChain->QueryInterface(IID_PPV_ARGS(&mSwapChain3));
				mSwapChain->QueryInterface(IID_PPV_ARGS(&mSwapChain4));
			}
			RegisterWrapperPointer(static_cast<IDXGIObject*>(this));
			RegisterWrapperPointer(static_cast<IDXGIDeviceSubObject*>(this));
			RegisterWrapperPointer(static_cast<IDXGISwapChain*>(this));
			RegisterWrapperPointer(static_cast<IDXGISwapChain1*>(this));
			RegisterWrapperPointer(static_cast<IDXGISwapChain2*>(this));
			RegisterWrapperPointer(static_cast<IDXGISwapChain3*>(this));
			RegisterWrapperPointer(static_cast<IDXGISwapChain4*>(this));
			DX12_LOG("DX12Wrapper",
				"kind=swapchain,stage=create,wrapper=%p,object=%p",
				this, swapChain);
		}

		~SwapChainWrapper()
		{
			// The overlay caches a native QI reference and back buffers. Release
			// them before our own references: otherwise the old flip chain stays
			// attached to the HWND and DLSS FG recreation fails with E_ACCESSDENIED.
			DX12NativeOverlayInvalidate(mSwapChain);
			// Release the frame-lifecycle owner before WrapperBase releases the
			// native object so a genuine game swapchain recreation can be selected.
			DXGIHookRuntime::ForgetSwapChain(mSwapChain);
			if (mSwapChain1) {
				mSwapChain1->Release();
				mSwapChain1 = nullptr;
			}
			if (mSwapChain2) {
				mSwapChain2->Release();
				mSwapChain2 = nullptr;
			}
			if (mSwapChain3) {
				mSwapChain3->Release();
				mSwapChain3 = nullptr;
			}
			if (mSwapChain4) {
				mSwapChain4->Release();
				mSwapChain4 = nullptr;
			}
		}

		HRESULT QuerySwapChain1(IDXGISwapChain1 **swapChain)
		{
			if (!swapChain)
				return E_POINTER;
			*swapChain = nullptr;
			if (!mSwapChain1)
				return E_NOINTERFACE;
			mSwapChain1->AddRef();
			*swapChain = mSwapChain1;
			return S_OK;
		}

		HRESULT QuerySwapChain2(IDXGISwapChain2 **swapChain)
		{
			if (!swapChain)
				return E_POINTER;
			*swapChain = nullptr;
			if (!mSwapChain2)
				return E_NOINTERFACE;
			mSwapChain2->AddRef();
			*swapChain = mSwapChain2;
			return S_OK;
		}

		HRESULT QuerySwapChain3(IDXGISwapChain3 **swapChain)
		{
			if (!swapChain)
				return E_POINTER;
			*swapChain = nullptr;
			if (!mSwapChain3)
				return E_NOINTERFACE;
			mSwapChain3->AddRef();
			*swapChain = mSwapChain3;
			return S_OK;
		}

		HRESULT QuerySwapChain4(IDXGISwapChain4 **swapChain)
		{
			if (!swapChain)
				return E_POINTER;
			*swapChain = nullptr;
			if (!mSwapChain4)
				return E_NOINTERFACE;
			mSwapChain4->AddRef();
			*swapChain = mSwapChain4;
			return S_OK;
		}

		void *GetSwapChainWrapperInterface(REFIID riid)
		{
			if (IsEqualIID(riid, __uuidof(IUnknown)) || IsEqualIID(riid, __uuidof(IDXGISwapChain4)))
				return static_cast<IDXGISwapChain4*>(this);
			if (IsEqualIID(riid, __uuidof(IDXGISwapChain3)))
				return static_cast<IDXGISwapChain3*>(this);
			if (IsEqualIID(riid, __uuidof(IDXGISwapChain2)))
				return static_cast<IDXGISwapChain2*>(this);
			if (IsEqualIID(riid, __uuidof(IDXGISwapChain1)))
				return static_cast<IDXGISwapChain1*>(this);
			if (IsEqualIID(riid, __uuidof(IDXGISwapChain)))
				return static_cast<IDXGISwapChain*>(this);
			if (IsEqualIID(riid, __uuidof(IDXGIDeviceSubObject)))
				return static_cast<IDXGIDeviceSubObject*>(this);
			if (IsEqualIID(riid, __uuidof(IDXGIObject)))
				return static_cast<IDXGIObject*>(this);
			return nullptr;
		}

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) override
		{
			if (!ppvObject)
				return E_POINTER;
			*ppvObject = nullptr;

			if (IsEqualIID(riid, __uuidof(IUnknown)) || IsSupportedWrapperIid(mSwapChain, riid, IsDxgiSwapChainWrapperIid)) {
				*ppvObject = GetSwapChainWrapperInterface(riid);
				if (*ppvObject) {
					AddRef();
					return S_OK;
				}
			}

			HRESULT hr = mSwapChain->QueryInterface(riid, ppvObject);
			if (SUCCEEDED(hr))
				DX12Wrapper::WrapDXGISwapChainResult(riid, ppvObject);
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

		HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID Name, UINT DataSize, const void *pData) override
		{
			return mSwapChain->SetPrivateData(Name, DataSize, pData);
		}

		HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID Name, const IUnknown *pUnknown) override
		{
			return mSwapChain->SetPrivateDataInterface(Name, pUnknown);
		}

		HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID Name, UINT *pDataSize, void *pData) override
		{
			return mSwapChain->GetPrivateData(Name, pDataSize, pData);
		}

		HRESULT STDMETHODCALLTYPE GetParent(REFIID riid, void **ppParent) override
		{
			HRESULT hr = mSwapChain->GetParent(riid, ppParent);
			if (SUCCEEDED(hr))
				DX12Wrapper::WrapDXGIFactoryResult(riid, ppParent);
			return hr;
		}

		HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void **ppDevice) override
		{
			HRESULT hr = mSwapChain->GetDevice(riid, ppDevice);
			if (SUCCEEDED(hr))
				DX12Wrapper::WrapD3D12DeviceResult(riid, ppDevice);
			return hr;
		}

		HRESULT STDMETHODCALLTYPE Present(UINT SyncInterval, UINT Flags) override
		{
			return DX12WrapperRuntimeUtils::Present(mSwapChain, SyncInterval, Flags);
		}

		HRESULT STDMETHODCALLTYPE GetBuffer(UINT Buffer, REFIID riid, void **ppSurface) override
		{
			HRESULT hr = mSwapChain->GetBuffer(Buffer, riid, ppSurface);
			if (SUCCEEDED(hr))
				DX12Wrapper::WrapD3D12ResourceResult(riid, ppSurface);
			return hr;
		}

		HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL Fullscreen, IDXGIOutput *pTarget) override
		{
			return mSwapChain->SetFullscreenState(Fullscreen, pTarget);
		}

		HRESULT STDMETHODCALLTYPE GetFullscreenState(BOOL *pFullscreen, IDXGIOutput **ppTarget) override
		{
			return mSwapChain->GetFullscreenState(pFullscreen, ppTarget);
		}

		HRESULT STDMETHODCALLTYPE GetDesc(DXGI_SWAP_CHAIN_DESC *pDesc) override
		{
			return mSwapChain->GetDesc(pDesc);
		}

		HRESULT STDMETHODCALLTYPE ResizeBuffers(
			UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) override
		{
			return DX12WrapperRuntimeUtils::ResizeBuffers(
				mSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
		}

		HRESULT STDMETHODCALLTYPE ResizeTarget(const DXGI_MODE_DESC *pNewTargetParameters) override
		{
			return mSwapChain->ResizeTarget(pNewTargetParameters);
		}

		HRESULT STDMETHODCALLTYPE GetContainingOutput(IDXGIOutput **ppOutput) override
		{
			return mSwapChain->GetContainingOutput(ppOutput);
		}

		HRESULT STDMETHODCALLTYPE GetFrameStatistics(DXGI_FRAME_STATISTICS *pStats) override
		{
			return mSwapChain->GetFrameStatistics(pStats);
		}

		HRESULT STDMETHODCALLTYPE GetLastPresentCount(UINT *pLastPresentCount) override
		{
			return mSwapChain->GetLastPresentCount(pLastPresentCount);
		}

		HRESULT STDMETHODCALLTYPE GetDesc1(DXGI_SWAP_CHAIN_DESC1 *pDesc) override
		{
			IDXGISwapChain1 *swapChain1 = nullptr;
			HRESULT hr = QuerySwapChain1(&swapChain1);
			if (FAILED(hr))
				return hr;
			hr = swapChain1->GetDesc1(pDesc);
			swapChain1->Release();
			return hr;
		}

		HRESULT STDMETHODCALLTYPE GetFullscreenDesc(DXGI_SWAP_CHAIN_FULLSCREEN_DESC *pDesc) override
		{
			IDXGISwapChain1 *swapChain1 = nullptr;
			HRESULT hr = QuerySwapChain1(&swapChain1);
			if (FAILED(hr))
				return hr;
			hr = swapChain1->GetFullscreenDesc(pDesc);
			swapChain1->Release();
			return hr;
		}

		HRESULT STDMETHODCALLTYPE GetHwnd(HWND *pHwnd) override
		{
			IDXGISwapChain1 *swapChain1 = nullptr;
			HRESULT hr = QuerySwapChain1(&swapChain1);
			if (FAILED(hr))
				return hr;
			hr = swapChain1->GetHwnd(pHwnd);
			swapChain1->Release();
			return hr;
		}

		HRESULT STDMETHODCALLTYPE GetCoreWindow(REFIID refiid, void **ppUnk) override
		{
			IDXGISwapChain1 *swapChain1 = nullptr;
			HRESULT hr = QuerySwapChain1(&swapChain1);
			if (FAILED(hr))
				return hr;
			hr = swapChain1->GetCoreWindow(refiid, ppUnk);
			swapChain1->Release();
			return hr;
		}

		HRESULT STDMETHODCALLTYPE Present1(
			UINT SyncInterval, UINT PresentFlags, const DXGI_PRESENT_PARAMETERS *pPresentParameters) override
		{
			if (!mSwapChain1)
				return DXGI_ERROR_UNSUPPORTED;
			return DX12WrapperRuntimeUtils::Present1(
				mSwapChain1, SyncInterval, PresentFlags, pPresentParameters);
		}

		BOOL STDMETHODCALLTYPE IsTemporaryMonoSupported() override
		{
			IDXGISwapChain1 *swapChain1 = nullptr;
			if (FAILED(QuerySwapChain1(&swapChain1)))
				return FALSE;
			BOOL supported = swapChain1->IsTemporaryMonoSupported();
			swapChain1->Release();
			return supported;
		}

		HRESULT STDMETHODCALLTYPE GetRestrictToOutput(IDXGIOutput **ppRestrictToOutput) override
		{
			IDXGISwapChain1 *swapChain1 = nullptr;
			HRESULT hr = QuerySwapChain1(&swapChain1);
			if (FAILED(hr))
				return hr;
			hr = swapChain1->GetRestrictToOutput(ppRestrictToOutput);
			swapChain1->Release();
			return hr;
		}

		HRESULT STDMETHODCALLTYPE SetBackgroundColor(const DXGI_RGBA *pColor) override
		{
			IDXGISwapChain1 *swapChain1 = nullptr;
			HRESULT hr = QuerySwapChain1(&swapChain1);
			if (FAILED(hr))
				return hr;
			hr = swapChain1->SetBackgroundColor(pColor);
			swapChain1->Release();
			return hr;
		}

		HRESULT STDMETHODCALLTYPE GetBackgroundColor(DXGI_RGBA *pColor) override
		{
			IDXGISwapChain1 *swapChain1 = nullptr;
			HRESULT hr = QuerySwapChain1(&swapChain1);
			if (FAILED(hr))
				return hr;
			hr = swapChain1->GetBackgroundColor(pColor);
			swapChain1->Release();
			return hr;
		}

		HRESULT STDMETHODCALLTYPE SetRotation(DXGI_MODE_ROTATION Rotation) override
		{
			IDXGISwapChain1 *swapChain1 = nullptr;
			HRESULT hr = QuerySwapChain1(&swapChain1);
			if (FAILED(hr))
				return hr;
			hr = swapChain1->SetRotation(Rotation);
			swapChain1->Release();
			return hr;
		}

		HRESULT STDMETHODCALLTYPE GetRotation(DXGI_MODE_ROTATION *pRotation) override
		{
			IDXGISwapChain1 *swapChain1 = nullptr;
			HRESULT hr = QuerySwapChain1(&swapChain1);
			if (FAILED(hr))
				return hr;
			hr = swapChain1->GetRotation(pRotation);
			swapChain1->Release();
			return hr;
		}

		HRESULT STDMETHODCALLTYPE SetSourceSize(UINT Width, UINT Height) override
		{
			IDXGISwapChain2 *swapChain2 = nullptr;
			HRESULT hr = QuerySwapChain2(&swapChain2);
			if (FAILED(hr))
				return hr;
			hr = swapChain2->SetSourceSize(Width, Height);
			swapChain2->Release();
			return hr;
		}

		HRESULT STDMETHODCALLTYPE GetSourceSize(UINT *pWidth, UINT *pHeight) override
		{
			IDXGISwapChain2 *swapChain2 = nullptr;
			HRESULT hr = QuerySwapChain2(&swapChain2);
			if (FAILED(hr))
				return hr;
			hr = swapChain2->GetSourceSize(pWidth, pHeight);
			swapChain2->Release();
			return hr;
		}

		HRESULT STDMETHODCALLTYPE SetMaximumFrameLatency(UINT MaxLatency) override
		{
			IDXGISwapChain2 *swapChain2 = nullptr;
			HRESULT hr = QuerySwapChain2(&swapChain2);
			if (FAILED(hr))
				return hr;
			hr = swapChain2->SetMaximumFrameLatency(MaxLatency);
			swapChain2->Release();
			return hr;
		}

		HRESULT STDMETHODCALLTYPE GetMaximumFrameLatency(UINT *pMaxLatency) override
		{
			IDXGISwapChain2 *swapChain2 = nullptr;
			HRESULT hr = QuerySwapChain2(&swapChain2);
			if (FAILED(hr))
				return hr;
			hr = swapChain2->GetMaximumFrameLatency(pMaxLatency);
			swapChain2->Release();
			return hr;
		}

		HANDLE STDMETHODCALLTYPE GetFrameLatencyWaitableObject() override
		{
			IDXGISwapChain2 *swapChain2 = nullptr;
			if (FAILED(QuerySwapChain2(&swapChain2)))
				return nullptr;
			HANDLE handle = swapChain2->GetFrameLatencyWaitableObject();
			// The engine blocks its render thread on this handle. Frame
			// generation re-creates the swap chain, so the handle changes with
			// it - and a stall on a handle that no longer belongs to the live
			// swap chain is invisible in a stack dump (the wait is a plain
			// WaitForMultipleObjects). One line per call pairs the handle with
			// the swap chain identity so the next freeze can be matched against
			// the Present that follows.
			DX12_LOG_FLUSH("DX12FrameLatency",
				"\"event\":\"get_waitable\",\"swapchain\":\"%p\",\"handle\":\"%p\"",
				mSwapChain, handle);
			DX12_DIAG_REGISTER_FRAME_LATENCY_WAITABLE(mSwapChain, handle);
			swapChain2->Release();
			return handle;
		}

		HRESULT STDMETHODCALLTYPE SetMatrixTransform(const DXGI_MATRIX_3X2_F *pMatrix) override
		{
			IDXGISwapChain2 *swapChain2 = nullptr;
			HRESULT hr = QuerySwapChain2(&swapChain2);
			if (FAILED(hr))
				return hr;
			hr = swapChain2->SetMatrixTransform(pMatrix);
			swapChain2->Release();
			return hr;
		}

		HRESULT STDMETHODCALLTYPE GetMatrixTransform(DXGI_MATRIX_3X2_F *pMatrix) override
		{
			IDXGISwapChain2 *swapChain2 = nullptr;
			HRESULT hr = QuerySwapChain2(&swapChain2);
			if (FAILED(hr))
				return hr;
			hr = swapChain2->GetMatrixTransform(pMatrix);
			swapChain2->Release();
			return hr;
		}

		UINT STDMETHODCALLTYPE GetCurrentBackBufferIndex() override
		{
			IDXGISwapChain3 *swapChain3 = nullptr;
			if (FAILED(QuerySwapChain3(&swapChain3)))
				return 0;
			UINT index = swapChain3->GetCurrentBackBufferIndex();
			swapChain3->Release();
			return index;
		}

		HRESULT STDMETHODCALLTYPE CheckColorSpaceSupport(
			DXGI_COLOR_SPACE_TYPE ColorSpace, UINT *pColorSpaceSupport) override
		{
			IDXGISwapChain3 *swapChain3 = nullptr;
			HRESULT hr = QuerySwapChain3(&swapChain3);
			if (FAILED(hr))
				return hr;
			hr = swapChain3->CheckColorSpaceSupport(ColorSpace, pColorSpaceSupport);
			swapChain3->Release();
			return hr;
		}

		HRESULT STDMETHODCALLTYPE SetColorSpace1(DXGI_COLOR_SPACE_TYPE ColorSpace) override
		{
			IDXGISwapChain3 *swapChain3 = nullptr;
			HRESULT hr = QuerySwapChain3(&swapChain3);
			if (FAILED(hr))
				return hr;
			hr = swapChain3->SetColorSpace1(ColorSpace);
			swapChain3->Release();
			return hr;
		}

		HRESULT STDMETHODCALLTYPE ResizeBuffers1(
			UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT Format,
			UINT SwapChainFlags, const UINT *pCreationNodeMask, IUnknown *const *ppPresentQueue) override
		{
			IDXGISwapChain3 *swapChain3 = nullptr;
			HRESULT hr = QuerySwapChain3(&swapChain3);
			if (FAILED(hr))
				return hr;
			// Same contract as the plain ResizeBuffers path: the swap chain
			// pointer and the buffer indices survive the resize, so the cached
			// back-buffer view must be dropped or the next overlay draw binds a
			// descriptor of a destroyed buffer.
			if (!DX12InternalReplayState::IsActive())
				DX12NativeOverlayInvalidate(mSwapChain);
			std::vector<IUnknown*> unwrappedQueues;
			IUnknown *const *activeQueues = ppPresentQueue;
			if (ppPresentQueue && BufferCount) {
				unwrappedQueues.resize(BufferCount);
				for (UINT i = 0; i < BufferCount; ++i)
					unwrappedQueues[i] = DX12Wrapper::Unwrap(ppPresentQueue[i]);
				activeQueues = unwrappedQueues.data();
			}
			hr = swapChain3->ResizeBuffers1(
				BufferCount, Width, Height, Format, SwapChainFlags, pCreationNodeMask, activeQueues);
			swapChain3->Release();
			return hr;
		}

		HRESULT STDMETHODCALLTYPE SetHDRMetaData(
			DXGI_HDR_METADATA_TYPE Type, UINT Size, void *pMetaData) override
		{
			IDXGISwapChain4 *swapChain4 = nullptr;
			HRESULT hr = QuerySwapChain4(&swapChain4);
			if (FAILED(hr))
				return hr;
			hr = swapChain4->SetHDRMetaData(Type, Size, pMetaData);
			swapChain4->Release();
			return hr;
		}

	public:
		std::atomic<long> mRefCount{1};
		IDXGISwapChain *mSwapChain = nullptr;
		IDXGISwapChain1 *mSwapChain1 = nullptr;
		IDXGISwapChain2 *mSwapChain2 = nullptr;
		IDXGISwapChain3 *mSwapChain3 = nullptr;
		IDXGISwapChain4 *mSwapChain4 = nullptr;
	};


} // namespace DX12WrapperDetail
