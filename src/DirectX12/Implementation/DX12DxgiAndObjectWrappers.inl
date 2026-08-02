#include <intrin.h>
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
				DX12LogManager::Fields("DX12Wrapper",
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
				hr = factory2->CreateSwapChainForHwnd(
					DX12Wrapper::Unwrap(pDevice), hWnd, pDesc, pFullscreenDesc, pRestrictToOutput, ppSwapChain);
				if (SUCCEEDED(hr))
					DX12Wrapper::WrapDXGISwapChainResult(__uuidof(IDXGISwapChain1), reinterpret_cast<void**>(ppSwapChain));
				factory2->Release();
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
				RegisterWrapperPointer(static_cast<IDXGIObject*>(this));
				RegisterWrapperPointer(static_cast<IDXGIDeviceSubObject*>(this));
				RegisterWrapperPointer(static_cast<IDXGISwapChain*>(this));
				RegisterWrapperPointer(static_cast<IDXGISwapChain1*>(this));
				RegisterWrapperPointer(static_cast<IDXGISwapChain2*>(this));
				RegisterWrapperPointer(static_cast<IDXGISwapChain3*>(this));
				RegisterWrapperPointer(static_cast<IDXGISwapChain4*>(this));
				DX12LogManager::Fields("DX12Wrapper",
					"kind=swapchain,stage=create,wrapper=%p,object=%p",
					this, swapChain);
			}

			~SwapChainWrapper()
			{
				// Release the frame-lifecycle owner before WrapperBase releases the
				// native object so a genuine game swapchain recreation can be selected.
				DXGIHookRuntime::ForgetSwapChain(mSwapChain);
			}

			HRESULT QuerySwapChain1(IDXGISwapChain1 **swapChain)
			{
				return mSwapChain->QueryInterface(IID_PPV_ARGS(swapChain));
			}

			HRESULT QuerySwapChain2(IDXGISwapChain2 **swapChain)
			{
				return mSwapChain->QueryInterface(IID_PPV_ARGS(swapChain));
			}

			HRESULT QuerySwapChain3(IDXGISwapChain3 **swapChain)
			{
				return mSwapChain->QueryInterface(IID_PPV_ARGS(swapChain));
			}

			HRESULT QuerySwapChain4(IDXGISwapChain4 **swapChain)
			{
				return mSwapChain->QueryInterface(IID_PPV_ARGS(swapChain));
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
				IDXGISwapChain1 *swapChain1 = nullptr;
				HRESULT hr = QuerySwapChain1(&swapChain1);
				if (FAILED(hr))
					return hr;
				hr = DX12WrapperRuntimeUtils::Present1(
					swapChain1, SyncInterval, PresentFlags, pPresentParameters);
				swapChain1->Release();
				return hr;
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
				DX12OuterTimingProbeUtils::RegisterFrameLatencyWaitableObject(
					mSwapChain, handle);
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
		};

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
				DX12LogManager::Fields("DX12Wrapper",
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
					DX12LogManager::Fields("DX12ExecuteCommandLists",
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
				LARGE_INTEGER begin = DX12OuterTimingProbeUtils::Now();
				UINT64 completed = mFence->GetCompletedValue();
				DX12OuterTimingProbeUtils::LogFenceSlice(
					"GetCompletedValue",
					"wrapper",
					mFence,
					0,
					0,
					completed,
					S_OK,
					DX12OuterTimingProbeUtils::ElapsedUs(begin));
				return completed;
			}

			HRESULT STDMETHODCALLTYPE SetEventOnCompletion(UINT64 Value, HANDLE hEvent) override
			{
				const UINT64 completedBefore =
					DX12OuterTimingProbeUtils::TimingFenceCompletedValue(mFence);
				LARGE_INTEGER begin = DX12OuterTimingProbeUtils::Now();
				HRESULT hr = mFence->SetEventOnCompletion(Value, hEvent);
				const UINT64 completedAfter =
					DX12OuterTimingProbeUtils::TimingFenceCompletedValue(mFence);
				DX12OuterTimingProbeUtils::RegisterFenceEventOnCompletion(
					mFence,
					hEvent,
					Value,
					completedBefore,
					completedAfter,
					hr);
				DX12OuterTimingProbeUtils::LogFenceSlice(
					"SetEventOnCompletion",
					"wrapper",
					mFence,
					Value,
					completedBefore,
					completedAfter,
					hr,
					DX12OuterTimingProbeUtils::ElapsedUs(begin));
				return hr;
			}

			HRESULT STDMETHODCALLTYPE Signal(UINT64 Value) override
			{
				const UINT64 completedBefore =
					DX12OuterTimingProbeUtils::TimingFenceCompletedValue(mFence);
				LARGE_INTEGER begin = DX12OuterTimingProbeUtils::Now();
				HRESULT hr = mFence->Signal(Value);
				const UINT64 completedAfter =
					DX12OuterTimingProbeUtils::TimingFenceCompletedValue(mFence);
				DX12OuterTimingProbeUtils::LogFenceSlice(
					"Signal",
					"wrapper",
					mFence,
					Value,
					completedBefore,
					completedAfter,
					hr,
					DX12OuterTimingProbeUtils::ElapsedUs(begin));
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
				return mHeap->GetDesc();
			}

			D3D12_CPU_DESCRIPTOR_HANDLE STDMETHODCALLTYPE GetCPUDescriptorHandleForHeapStart() override
			{
				return mHeap->GetCPUDescriptorHandleForHeapStart();
			}

			D3D12_GPU_DESCRIPTOR_HANDLE STDMETHODCALLTYPE GetGPUDescriptorHandleForHeapStart() override
			{
				return mHeap->GetGPUDescriptorHandleForHeapStart();
			}

		public:
			std::atomic<long> mRefCount{1};
			ID3D12DescriptorHeap *mHeap = nullptr;
		};

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
				return mResource->GetDesc();
			}

			D3D12_GPU_VIRTUAL_ADDRESS STDMETHODCALLTYPE GetGPUVirtualAddress() override
			{
				DX12_CREATE_TIMING_SCOPE(ResourceQueryWrapper, Original);
				return mResource->GetGPUVirtualAddress();
			}

			HRESULT STDMETHODCALLTYPE WriteToSubresource(
				UINT DstSubresource, const D3D12_BOX *pDstBox, const void *pSrcData,
				UINT SrcRowPitch, UINT SrcDepthPitch) override
			{
				DX12_CREATE_TIMING_SCOPE(ResourceReadWriteWrapper, Original);
				HRESULT hr = mResource->WriteToSubresource(
					DstSubresource, pDstBox, pSrcData, SrcRowPitch, SrcDepthPitch);
				if (SUCCEEDED(hr) && pSrcData) {
					const D3D12_RESOURCE_DESC desc = mResource->GetDesc();
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
