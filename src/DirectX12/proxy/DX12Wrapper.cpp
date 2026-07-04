#include "DX12Wrapper.hpp"

#include <atomic>
#include <unordered_map>
#include <vector>

#include "DX12CommandListHookState.hpp"
#include "DX12DeviceHookTypes.hpp"
#include "DX12LogManager.hpp"
#include "DX12WrapperRuntimeUtils.hpp"
#include "DX12ModRuntime.hpp"
#include "DX12CommandQueueState.hpp"
#include "DX12ResourceTrackerHookRuntime.hpp"
#include "DX12ResourceStateTracker.hpp"
#include "DX12ShaderDump.hpp"
#include "dxgi/DXGIHookRuntime.hpp"

namespace DX12Wrapper
{
	namespace
	{
		class WrapperBase;

		SRWLOCK gWrapperLock = SRWLOCK_INIT;
		std::unordered_map<IUnknown*, WrapperBase*> gIdentityMap;
		std::unordered_map<IUnknown*, WrapperBase*> gWrapperMap;

		IUnknown *GetIdentity(IUnknown *object)
		{
			if (!object)
				return nullptr;

			IUnknown *identity = nullptr;
			if (FAILED(object->QueryInterface(IID_PPV_ARGS(&identity))))
				return nullptr;
			return identity;
		}

		bool IsD3D12DeviceIid(REFIID riid)
		{
			return
				IsEqualIID(riid, __uuidof(ID3D12Device)) ||
				IsEqualIID(riid, __uuidof(ID3D12Device1)) ||
				IsEqualIID(riid, __uuidof(ID3D12Device2)) ||
				IsEqualIID(riid, __uuidof(ID3D12Device3)) ||
				IsEqualIID(riid, __uuidof(ID3D12Device4)) ||
				IsEqualIID(riid, __uuidof(ID3D12Device5)) ||
				IsEqualIID(riid, __uuidof(ID3D12Device6)) ||
				IsEqualIID(riid, __uuidof(ID3D12Device7)) ||
				IsEqualIID(riid, __uuidof(ID3D12Device8)) ||
				IsEqualIID(riid, __uuidof(ID3D12Device9)) ||
				IsEqualIID(riid, __uuidof(ID3D12Device10)) ||
				IsEqualIID(riid, __uuidof(ID3D12Device11)) ||
				IsEqualIID(riid, __uuidof(ID3D12Device12)) ||
				IsEqualIID(riid, __uuidof(ID3D12Device13)) ||
				IsEqualIID(riid, __uuidof(ID3D12Device14));
		}

		bool IsD3D12CommandQueueIid(REFIID riid)
		{
			return IsEqualIID(riid, __uuidof(ID3D12CommandQueue));
		}

		bool IsD3D12ObjectIid(REFIID riid)
		{
			return IsEqualIID(riid, __uuidof(ID3D12Object));
		}

		bool IsD3D12DeviceChildIid(REFIID riid)
		{
			return IsD3D12ObjectIid(riid) || IsEqualIID(riid, __uuidof(ID3D12DeviceChild));
		}

		bool IsD3D12PageableIid(REFIID riid)
		{
			return IsD3D12DeviceChildIid(riid) || IsEqualIID(riid, __uuidof(ID3D12Pageable));
		}

		bool IsD3D12CommandQueueWrapperIid(REFIID riid)
		{
			return IsD3D12PageableIid(riid) || IsEqualIID(riid, __uuidof(ID3D12CommandQueue));
		}

		bool IsD3D12CommandAllocatorWrapperIid(REFIID riid)
		{
			return IsD3D12PageableIid(riid) || IsEqualIID(riid, __uuidof(ID3D12CommandAllocator));
		}

		bool IsD3D12FenceWrapperIid(REFIID riid)
		{
			return IsD3D12PageableIid(riid) || IsEqualIID(riid, __uuidof(ID3D12Fence));
		}

		bool IsD3D12DescriptorHeapWrapperIid(REFIID riid)
		{
			return IsD3D12PageableIid(riid) || IsEqualIID(riid, __uuidof(ID3D12DescriptorHeap));
		}

		bool IsD3D12ResourceWrapperIid(REFIID riid)
		{
			return
				IsD3D12PageableIid(riid) ||
				IsEqualIID(riid, __uuidof(ID3D12Resource)) ||
				IsEqualIID(riid, __uuidof(ID3D12Resource1)) ||
				IsEqualIID(riid, __uuidof(ID3D12Resource2));
		}

		bool IsD3D12HeapWrapperIid(REFIID riid)
		{
			return IsD3D12PageableIid(riid) || IsEqualIID(riid, __uuidof(ID3D12Heap));
		}

		bool IsD3D12PipelineStateWrapperIid(REFIID riid)
		{
			return IsD3D12PageableIid(riid) || IsEqualIID(riid, __uuidof(ID3D12PipelineState));
		}

		bool IsD3D12RootSignatureWrapperIid(REFIID riid)
		{
			return IsD3D12DeviceChildIid(riid) || IsEqualIID(riid, __uuidof(ID3D12RootSignature));
		}

		bool IsD3D12QueryHeapWrapperIid(REFIID riid)
		{
			return IsD3D12PageableIid(riid) || IsEqualIID(riid, __uuidof(ID3D12QueryHeap));
		}

		bool IsD3D12CommandSignatureWrapperIid(REFIID riid)
		{
			return IsD3D12PageableIid(riid) || IsEqualIID(riid, __uuidof(ID3D12CommandSignature));
		}

		bool IsD3D12DeviceFactoryIid(REFIID riid)
		{
			return IsEqualIID(riid, __uuidof(ID3D12DeviceFactory));
		}

		bool IsD3D12CommandAllocatorIid(REFIID riid)
		{
			return IsEqualIID(riid, __uuidof(ID3D12CommandAllocator));
		}

		bool IsD3D12FenceIid(REFIID riid)
		{
			return IsEqualIID(riid, __uuidof(ID3D12Fence)) || IsEqualIID(riid, __uuidof(ID3D12Fence1));
		}

		bool IsD3D12DescriptorHeapIid(REFIID riid)
		{
			return IsEqualIID(riid, __uuidof(ID3D12DescriptorHeap));
		}

		bool IsD3D12ResourceIid(REFIID riid)
		{
			return
				IsEqualIID(riid, __uuidof(ID3D12Resource)) ||
				IsEqualIID(riid, __uuidof(ID3D12Resource1)) ||
				IsEqualIID(riid, __uuidof(ID3D12Resource2));
		}

		bool IsD3D12HeapIid(REFIID riid)
		{
			return IsEqualIID(riid, __uuidof(ID3D12Heap));
		}

		bool IsD3D12PipelineStateIid(REFIID riid)
		{
			return IsEqualIID(riid, __uuidof(ID3D12PipelineState));
		}

		bool IsD3D12RootSignatureIid(REFIID riid)
		{
			return IsEqualIID(riid, __uuidof(ID3D12RootSignature));
		}

		bool IsD3D12QueryHeapIid(REFIID riid)
		{
			return IsEqualIID(riid, __uuidof(ID3D12QueryHeap));
		}

		bool IsD3D12CommandSignatureIid(REFIID riid)
		{
			return IsEqualIID(riid, __uuidof(ID3D12CommandSignature));
		}

		bool IsD3D12GraphicsCommandListIid(REFIID riid)
		{
			return
				IsEqualIID(riid, __uuidof(ID3D12GraphicsCommandList)) ||
				IsEqualIID(riid, __uuidof(ID3D12GraphicsCommandList1)) ||
				IsEqualIID(riid, __uuidof(ID3D12GraphicsCommandList2)) ||
				IsEqualIID(riid, __uuidof(ID3D12GraphicsCommandList3)) ||
				IsEqualIID(riid, __uuidof(ID3D12GraphicsCommandList4)) ||
				IsEqualIID(riid, __uuidof(ID3D12GraphicsCommandList5)) ||
				IsEqualIID(riid, __uuidof(ID3D12GraphicsCommandList6)) ||
				IsEqualIID(riid, __uuidof(ID3D12GraphicsCommandList7)) ||
				IsEqualIID(riid, __uuidof(ID3D12GraphicsCommandList8)) ||
				IsEqualIID(riid, __uuidof(ID3D12GraphicsCommandList9)) ||
				IsEqualIID(riid, __uuidof(ID3D12GraphicsCommandList10));
		}

		bool IsD3D12GraphicsCommandListWrapperIid(REFIID riid)
		{
			return
				IsD3D12DeviceChildIid(riid) ||
				IsEqualIID(riid, __uuidof(ID3D12CommandList)) ||
				IsD3D12GraphicsCommandListIid(riid);
		}

		bool IsDxgiFactoryIid(REFIID riid)
		{
			return
				IsEqualIID(riid, __uuidof(IDXGIFactory)) ||
				IsEqualIID(riid, __uuidof(IDXGIFactory1)) ||
				IsEqualIID(riid, __uuidof(IDXGIFactory2)) ||
				IsEqualIID(riid, __uuidof(IDXGIFactory3)) ||
				IsEqualIID(riid, __uuidof(IDXGIFactory4)) ||
				IsEqualIID(riid, __uuidof(IDXGIFactory5)) ||
				IsEqualIID(riid, __uuidof(IDXGIFactory6)) ||
				IsEqualIID(riid, __uuidof(IDXGIFactory7));
		}

		bool IsDxgiFactoryWrapperIid(REFIID riid)
		{
			return IsEqualIID(riid, __uuidof(IDXGIObject)) || IsDxgiFactoryIid(riid);
		}

		bool IsDxgiSwapChainIid(REFIID riid)
		{
			return
				IsEqualIID(riid, __uuidof(IDXGISwapChain)) ||
				IsEqualIID(riid, __uuidof(IDXGISwapChain1)) ||
				IsEqualIID(riid, __uuidof(IDXGISwapChain2)) ||
				IsEqualIID(riid, __uuidof(IDXGISwapChain3)) ||
				IsEqualIID(riid, __uuidof(IDXGISwapChain4));
		}

		bool IsDxgiSwapChainWrapperIid(REFIID riid)
		{
			return
				IsEqualIID(riid, __uuidof(IDXGIObject)) ||
				IsEqualIID(riid, __uuidof(IDXGIDeviceSubObject)) ||
				IsDxgiSwapChainIid(riid);
		}

		bool DoesObjectSupportIid(IUnknown *object, REFIID riid)
		{
			if (!object)
				return false;
			IUnknown *supported = nullptr;
			HRESULT hr = object->QueryInterface(riid, reinterpret_cast<void**>(&supported));
			if (SUCCEEDED(hr) && supported) {
				supported->Release();
				return true;
			}
			return false;
		}

		bool IsSupportedWrapperIid(IUnknown *object, REFIID riid, bool (*isWrapperIid)(REFIID))
		{
			if (!isWrapperIid || !isWrapperIid(riid))
				return false;
			if (IsEqualIID(riid, __uuidof(IUnknown)))
				return true;
			return DoesObjectSupportIid(object, riid);
		}

		class WrapperBase
		{
		public:
			explicit WrapperBase(IUnknown *object)
			{
				mObject = object;
				if (mObject)
					mObject->AddRef();
				mIdentity = GetIdentity(object);

				AcquireSRWLockExclusive(&gWrapperLock);
				if (mIdentity)
					gIdentityMap[mIdentity] = this;
				ReleaseSRWLockExclusive(&gWrapperLock);
			}

			virtual ~WrapperBase()
			{
				AcquireSRWLockExclusive(&gWrapperLock);
				for (size_t i = 0; i < mWrapperPointers.size(); ++i)
					gWrapperMap.erase(mWrapperPointers[i]);
				if (mIdentity) {
					std::unordered_map<IUnknown*, WrapperBase*>::iterator it = gIdentityMap.find(mIdentity);
					if (it != gIdentityMap.end() && it->second == this)
						gIdentityMap.erase(it);
				}
				ReleaseSRWLockExclusive(&gWrapperLock);

				if (mIdentity) {
					mIdentity->Release();
					mIdentity = nullptr;
				}
				if (mObject) {
					mObject->Release();
					mObject = nullptr;
				}
			}

			IUnknown *GetObject()
			{
				return mObject;
			}

			void RegisterWrapperPointer(IUnknown *wrapperPointer)
			{
				if (!wrapperPointer)
					return;
				AcquireSRWLockExclusive(&gWrapperLock);
				gWrapperMap[wrapperPointer] = this;
				mWrapperPointers.push_back(wrapperPointer);
				ReleaseSRWLockExclusive(&gWrapperLock);
			}

			static WrapperBase *FindByIdentity(IUnknown *object)
			{
				IUnknown *identity = GetIdentity(object);
				if (!identity)
					return nullptr;

				AcquireSRWLockShared(&gWrapperLock);
				std::unordered_map<IUnknown*, WrapperBase*>::iterator it = gIdentityMap.find(identity);
				WrapperBase *wrapper = it != gIdentityMap.end() ? it->second : nullptr;
				ReleaseSRWLockShared(&gWrapperLock);
				identity->Release();
				return wrapper;
			}

			static WrapperBase *FindByWrapperPointer(IUnknown *object)
			{
				if (!object)
					return nullptr;

				AcquireSRWLockShared(&gWrapperLock);
				std::unordered_map<IUnknown*, WrapperBase*>::iterator it = gWrapperMap.find(object);
				WrapperBase *wrapper = it != gWrapperMap.end() ? it->second : nullptr;
				ReleaseSRWLockShared(&gWrapperLock);
				return wrapper;
			}

		public:
			IUnknown *mObject = nullptr;
			IUnknown *mIdentity = nullptr;
			std::vector<IUnknown*> mWrapperPointers;
		};

		class DeviceFactoryWrapper final : public ID3D12DeviceFactory, public WrapperBase
		{
		public:
			explicit DeviceFactoryWrapper(ID3D12DeviceFactory *factory)
				: WrapperBase(factory)
			{
				mFactory = factory;
				RegisterWrapperPointer(static_cast<ID3D12DeviceFactory*>(this));
				DX12LogManager::JsonFunc("DX12Wrapper",
					"\"kind\":\"device_factory\",\"stage\":\"create\",\"wrapper\":\"%p\",\"object\":\"%p\"",
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
				const ULONG ref = static_cast<ULONG>(--mRefCount);
				if (!ref)
					delete this;
				return ref;
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
				DX12LogManager::JsonFunc("DX12Wrapper",
					"\"kind\":\"factory\",\"stage\":\"create\",\"wrapper\":\"%p\",\"object\":\"%p\"",
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
				const ULONG ref = static_cast<ULONG>(--mRefCount);
				if (!ref)
					delete this;
				return ref;
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
				DX12LogManager::JsonFunc("DX12Wrapper",
					"\"kind\":\"swapchain\",\"stage\":\"create\",\"wrapper\":\"%p\",\"object\":\"%p\"",
					this, swapChain);
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
				const ULONG ref = static_cast<ULONG>(--mRefCount);
				if (!ref)
					delete this;
				return ref;
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
				DX12LogManager::JsonFunc("DX12Wrapper",
					"\"kind\":\"command_queue\",\"stage\":\"create\",\"wrapper\":\"%p\",\"object\":\"%p\",\"type\":%d",
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

				return mQueue->QueryInterface(riid, ppvObject);
			}

			ULONG STDMETHODCALLTYPE AddRef() override
			{
				return static_cast<ULONG>(++mRefCount);
			}

			ULONG STDMETHODCALLTYPE Release() override
			{
				const ULONG ref = static_cast<ULONG>(--mRefCount);
				if (!ref)
					delete this;
				return ref;
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
					DX12LogManager::JsonFunc("DX12ExecuteCommandLists",
						"\"stage\":\"non_direct_passthrough\",\"queue\":\"%p\",\"type\":%d,\"count\":%u",
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
				return mAllocator->QueryInterface(riid, ppvObject);
			}

			ULONG STDMETHODCALLTYPE AddRef() override
			{
				return static_cast<ULONG>(++mRefCount);
			}

			ULONG STDMETHODCALLTYPE Release() override
			{
				const ULONG ref = static_cast<ULONG>(--mRefCount);
				if (!ref)
					delete this;
				return ref;
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
				return mFence->QueryInterface(riid, ppvObject);
			}

			ULONG STDMETHODCALLTYPE AddRef() override
			{
				return static_cast<ULONG>(++mRefCount);
			}

			ULONG STDMETHODCALLTYPE Release() override
			{
				const ULONG ref = static_cast<ULONG>(--mRefCount);
				if (!ref)
					delete this;
				return ref;
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
				return mFence->GetCompletedValue();
			}

			HRESULT STDMETHODCALLTYPE SetEventOnCompletion(UINT64 Value, HANDLE hEvent) override
			{
				return mFence->SetEventOnCompletion(Value, hEvent);
			}

			HRESULT STDMETHODCALLTYPE Signal(UINT64 Value) override
			{
				return mFence->Signal(Value);
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
				return mHeap->QueryInterface(riid, ppvObject);
			}

			ULONG STDMETHODCALLTYPE AddRef() override
			{
				return static_cast<ULONG>(++mRefCount);
			}

			ULONG STDMETHODCALLTYPE Release() override
			{
				const ULONG ref = static_cast<ULONG>(--mRefCount);
				if (!ref)
					delete this;
				return ref;
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
				return mResource->QueryInterface(riid, ppvObject);
			}

			ULONG STDMETHODCALLTYPE AddRef() override
			{
				return static_cast<ULONG>(++mRefCount);
			}

			ULONG STDMETHODCALLTYPE Release() override
			{
				const ULONG ref = static_cast<ULONG>(--mRefCount);
				if (!ref)
					delete this;
				return ref;
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
				return mResource->Map(Subresource, pReadRange, ppData);
			}

			void STDMETHODCALLTYPE Unmap(UINT Subresource, const D3D12_RANGE *pWrittenRange) override
			{
				mResource->Unmap(Subresource, pWrittenRange);
			}

			D3D12_RESOURCE_DESC STDMETHODCALLTYPE GetDesc() override
			{
				return mResource->GetDesc();
			}

			D3D12_GPU_VIRTUAL_ADDRESS STDMETHODCALLTYPE GetGPUVirtualAddress() override
			{
				return mResource->GetGPUVirtualAddress();
			}

			HRESULT STDMETHODCALLTYPE WriteToSubresource(
				UINT DstSubresource, const D3D12_BOX *pDstBox, const void *pSrcData,
				UINT SrcRowPitch, UINT SrcDepthPitch) override
			{
				return mResource->WriteToSubresource(
					DstSubresource, pDstBox, pSrcData, SrcRowPitch, SrcDepthPitch);
			}

			HRESULT STDMETHODCALLTYPE ReadFromSubresource(
				void *pDstData, UINT DstRowPitch, UINT DstDepthPitch, UINT SrcSubresource,
				const D3D12_BOX *pSrcBox) override
			{
				return mResource->ReadFromSubresource(
					pDstData, DstRowPitch, DstDepthPitch, SrcSubresource, pSrcBox);
			}

			HRESULT STDMETHODCALLTYPE GetHeapProperties(D3D12_HEAP_PROPERTIES *pHeapProperties, D3D12_HEAP_FLAGS *pHeapFlags) override
			{
				return mResource->GetHeapProperties(pHeapProperties, pHeapFlags);
			}

			HRESULT STDMETHODCALLTYPE GetProtectedResourceSession(REFIID riid, void **ppProtectedSession) override
			{
				ID3D12Resource1 *resource1 = nullptr;
				HRESULT hr = QueryResource1(&resource1);
				if (FAILED(hr))
					return hr;
				hr = resource1->GetProtectedResourceSession(riid, ppProtectedSession);
				resource1->Release();
				return hr;
			}

			D3D12_RESOURCE_DESC1 STDMETHODCALLTYPE GetDesc1() override
			{
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
				DX12LogManager::JsonFunc("DX12Wrapper",
					"\"kind\":\"heap\",\"stage\":\"create\",\"wrapper\":\"%p\",\"object\":\"%p\"",
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
				return mHeap->QueryInterface(riid, ppvObject);
			}

			ULONG STDMETHODCALLTYPE AddRef() override
			{
				return static_cast<ULONG>(++mRefCount);
			}

			ULONG STDMETHODCALLTYPE Release() override
			{
				const ULONG ref = static_cast<ULONG>(--mRefCount);
				if (!ref)
					delete this;
				return ref;
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
				DX12LogManager::JsonFunc("DX12Wrapper",
					"\"kind\":\"pipeline_state\",\"stage\":\"create\",\"wrapper\":\"%p\",\"object\":\"%p\"",
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
				return mPipelineState->QueryInterface(riid, ppvObject);
			}

			ULONG STDMETHODCALLTYPE AddRef() override
			{
				return static_cast<ULONG>(++mRefCount);
			}

			ULONG STDMETHODCALLTYPE Release() override
			{
				const ULONG ref = static_cast<ULONG>(--mRefCount);
				if (!ref)
					delete this;
				return ref;
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
				DX12LogManager::JsonFunc("DX12Wrapper",
					"\"kind\":\"root_signature\",\"stage\":\"create\",\"wrapper\":\"%p\",\"object\":\"%p\"",
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
				return mRootSignature->QueryInterface(riid, ppvObject);
			}

			ULONG STDMETHODCALLTYPE AddRef() override
			{
				return static_cast<ULONG>(++mRefCount);
			}

			ULONG STDMETHODCALLTYPE Release() override
			{
				const ULONG ref = static_cast<ULONG>(--mRefCount);
				if (!ref)
					delete this;
				return ref;
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
				DX12LogManager::JsonFunc("DX12Wrapper",
					"\"kind\":\"query_heap\",\"stage\":\"create\",\"wrapper\":\"%p\",\"object\":\"%p\"",
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
				return mQueryHeap->QueryInterface(riid, ppvObject);
			}

			ULONG STDMETHODCALLTYPE AddRef() override
			{
				return static_cast<ULONG>(++mRefCount);
			}

			ULONG STDMETHODCALLTYPE Release() override
			{
				const ULONG ref = static_cast<ULONG>(--mRefCount);
				if (!ref)
					delete this;
				return ref;
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
				DX12LogManager::JsonFunc("DX12Wrapper",
					"\"kind\":\"command_signature\",\"stage\":\"create\",\"wrapper\":\"%p\",\"object\":\"%p\"",
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
				return mCommandSignature->QueryInterface(riid, ppvObject);
			}

			ULONG STDMETHODCALLTYPE AddRef() override
			{
				return static_cast<ULONG>(++mRefCount);
			}

			ULONG STDMETHODCALLTYPE Release() override
			{
				const ULONG ref = static_cast<ULONG>(--mRefCount);
				if (!ref)
					delete this;
				return ref;
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

		class GraphicsCommandListWrapper final : public ID3D12GraphicsCommandList10, public WrapperBase
		{
		public:
			explicit GraphicsCommandListWrapper(ID3D12GraphicsCommandList *commandList)
				: WrapperBase(commandList)
			{
				mCommandList = commandList;
				RegisterWrapperPointer(static_cast<ID3D12Object*>(this));
				RegisterWrapperPointer(static_cast<ID3D12DeviceChild*>(this));
				RegisterWrapperPointer(static_cast<ID3D12CommandList*>(this));
				RegisterWrapperPointer(static_cast<ID3D12GraphicsCommandList*>(this));
				RegisterWrapperPointer(static_cast<ID3D12GraphicsCommandList1*>(this));
				RegisterWrapperPointer(static_cast<ID3D12GraphicsCommandList2*>(this));
				RegisterWrapperPointer(static_cast<ID3D12GraphicsCommandList3*>(this));
				RegisterWrapperPointer(static_cast<ID3D12GraphicsCommandList4*>(this));
				RegisterWrapperPointer(static_cast<ID3D12GraphicsCommandList5*>(this));
				RegisterWrapperPointer(static_cast<ID3D12GraphicsCommandList6*>(this));
				RegisterWrapperPointer(static_cast<ID3D12GraphicsCommandList7*>(this));
				RegisterWrapperPointer(static_cast<ID3D12GraphicsCommandList8*>(this));
				RegisterWrapperPointer(static_cast<ID3D12GraphicsCommandList9*>(this));
				RegisterWrapperPointer(static_cast<ID3D12GraphicsCommandList10*>(this));
				DX12LogManager::JsonFunc("DX12Wrapper",
					"\"kind\":\"graphics_command_list\",\"stage\":\"create\",\"wrapper\":\"%p\",\"object\":\"%p\"",
					this, commandList);
			}

			HRESULT QueryCommandList1(ID3D12GraphicsCommandList1 **commandList)
			{
				return mCommandList->QueryInterface(IID_PPV_ARGS(commandList));
			}

			HRESULT QueryCommandList2(ID3D12GraphicsCommandList2 **commandList)
			{
				return mCommandList->QueryInterface(IID_PPV_ARGS(commandList));
			}

			HRESULT QueryCommandList3(ID3D12GraphicsCommandList3 **commandList)
			{
				return mCommandList->QueryInterface(IID_PPV_ARGS(commandList));
			}

			HRESULT QueryCommandList4(ID3D12GraphicsCommandList4 **commandList)
			{
				return mCommandList->QueryInterface(IID_PPV_ARGS(commandList));
			}

			HRESULT QueryCommandList5(ID3D12GraphicsCommandList5 **commandList)
			{
				return mCommandList->QueryInterface(IID_PPV_ARGS(commandList));
			}

			HRESULT QueryCommandList6(ID3D12GraphicsCommandList6 **commandList)
			{
				return mCommandList->QueryInterface(IID_PPV_ARGS(commandList));
			}

			HRESULT QueryCommandList7(ID3D12GraphicsCommandList7 **commandList)
			{
				return mCommandList->QueryInterface(IID_PPV_ARGS(commandList));
			}

			HRESULT QueryCommandList8(ID3D12GraphicsCommandList8 **commandList)
			{
				return mCommandList->QueryInterface(IID_PPV_ARGS(commandList));
			}

			HRESULT QueryCommandList9(ID3D12GraphicsCommandList9 **commandList)
			{
				return mCommandList->QueryInterface(IID_PPV_ARGS(commandList));
			}

			HRESULT QueryCommandList10(ID3D12GraphicsCommandList10 **commandList)
			{
				return mCommandList->QueryInterface(IID_PPV_ARGS(commandList));
			}

			void *GetGraphicsCommandListWrapperInterface(REFIID riid)
			{
				if (IsEqualIID(riid, __uuidof(IUnknown)) ||
					IsEqualIID(riid, __uuidof(ID3D12GraphicsCommandList10)))
					return static_cast<ID3D12GraphicsCommandList10*>(this);
				if (IsEqualIID(riid, __uuidof(ID3D12GraphicsCommandList9)))
					return static_cast<ID3D12GraphicsCommandList9*>(this);
				if (IsEqualIID(riid, __uuidof(ID3D12GraphicsCommandList8)))
					return static_cast<ID3D12GraphicsCommandList8*>(this);
				if (IsEqualIID(riid, __uuidof(ID3D12GraphicsCommandList7)))
					return static_cast<ID3D12GraphicsCommandList7*>(this);
				if (IsEqualIID(riid, __uuidof(ID3D12GraphicsCommandList6)))
					return static_cast<ID3D12GraphicsCommandList6*>(this);
				if (IsEqualIID(riid, __uuidof(ID3D12GraphicsCommandList5)))
					return static_cast<ID3D12GraphicsCommandList5*>(this);
				if (IsEqualIID(riid, __uuidof(ID3D12GraphicsCommandList4)))
					return static_cast<ID3D12GraphicsCommandList4*>(this);
				if (IsEqualIID(riid, __uuidof(ID3D12GraphicsCommandList3)))
					return static_cast<ID3D12GraphicsCommandList3*>(this);
				if (IsEqualIID(riid, __uuidof(ID3D12GraphicsCommandList2)))
					return static_cast<ID3D12GraphicsCommandList2*>(this);
				if (IsEqualIID(riid, __uuidof(ID3D12GraphicsCommandList1)))
					return static_cast<ID3D12GraphicsCommandList1*>(this);
				if (IsEqualIID(riid, __uuidof(ID3D12GraphicsCommandList)))
					return static_cast<ID3D12GraphicsCommandList*>(this);
				if (IsEqualIID(riid, __uuidof(ID3D12CommandList)))
					return static_cast<ID3D12CommandList*>(this);
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

				if (IsEqualIID(riid, __uuidof(IUnknown)) || IsSupportedWrapperIid(mCommandList, riid, IsD3D12GraphicsCommandListWrapperIid)) {
					*ppvObject = GetGraphicsCommandListWrapperInterface(riid);
					if (*ppvObject) {
						AddRef();
						return S_OK;
					}
				}

				return mCommandList->QueryInterface(riid, ppvObject);
			}

			ULONG STDMETHODCALLTYPE AddRef() override
			{
				return static_cast<ULONG>(++mRefCount);
			}

			ULONG STDMETHODCALLTYPE Release() override
			{
				const ULONG ref = static_cast<ULONG>(--mRefCount);
				if (!ref)
					delete this;
				return ref;
			}

			HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid, UINT *pDataSize, void *pData) override
			{
				return mCommandList->GetPrivateData(guid, pDataSize, pData);
			}

			HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT DataSize, const void *pData) override
			{
				return mCommandList->SetPrivateData(guid, DataSize, pData);
			}

			HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown *pData) override
			{
				return mCommandList->SetPrivateDataInterface(guid, pData);
			}

			HRESULT STDMETHODCALLTYPE SetName(LPCWSTR Name) override
			{
				return mCommandList->SetName(Name);
			}

			HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void **ppvDevice) override
			{
				HRESULT hr = mCommandList->GetDevice(riid, ppvDevice);
				if (SUCCEEDED(hr))
					DX12Wrapper::WrapD3D12DeviceResult(riid, ppvDevice);
				return hr;
			}

			D3D12_COMMAND_LIST_TYPE STDMETHODCALLTYPE GetType() override
			{
				return mCommandList->GetType();
			}

			HRESULT STDMETHODCALLTYPE Close() override
			{
				return DX12WrapperRuntimeUtils::CloseCommandList(mCommandList, mOrigClose);
			}

			HRESULT STDMETHODCALLTYPE Reset(ID3D12CommandAllocator *pAllocator, ID3D12PipelineState *pInitialState) override
			{
				return DX12WrapperRuntimeUtils::ResetCommandList(
					mCommandList, pAllocator, pInitialState, mOrigReset);
			}

			void STDMETHODCALLTYPE ClearState(ID3D12PipelineState *pPipelineState) override
			{
				mCommandList->ClearState(DX12WrapperRuntimeUtils::UnwrapPipelineState(pPipelineState));
			}

			void STDMETHODCALLTYPE DrawInstanced(
				UINT VertexCountPerInstance, UINT InstanceCount,
				UINT StartVertexLocation, UINT StartInstanceLocation) override
			{
				DX12WrapperRuntimeUtils::DrawInstanced(
					mCommandList,
					VertexCountPerInstance,
					InstanceCount,
					StartVertexLocation,
					StartInstanceLocation,
					mOrigDrawInstanced);
			}

			void STDMETHODCALLTYPE DrawIndexedInstanced(
				UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation,
				INT BaseVertexLocation, UINT StartInstanceLocation) override
			{
				DX12WrapperRuntimeUtils::DrawIndexedInstanced(
					mCommandList,
					IndexCountPerInstance,
					InstanceCount,
					StartIndexLocation,
					BaseVertexLocation,
					StartInstanceLocation,
					mOrigDrawIndexedInstanced);
			}

			void STDMETHODCALLTYPE Dispatch(UINT ThreadGroupCountX, UINT ThreadGroupCountY, UINT ThreadGroupCountZ) override
			{
				DX12WrapperRuntimeUtils::Dispatch(
					mCommandList,
					ThreadGroupCountX,
					ThreadGroupCountY,
					ThreadGroupCountZ,
					mOrigDispatch);
			}

			void STDMETHODCALLTYPE CopyBufferRegion(
				ID3D12Resource *pDstBuffer, UINT64 DstOffset, ID3D12Resource *pSrcBuffer,
				UINT64 SrcOffset, UINT64 NumBytes) override
			{
				mCommandList->CopyBufferRegion(
					DX12WrapperRuntimeUtils::UnwrapResource(pDstBuffer), DstOffset,
					DX12WrapperRuntimeUtils::UnwrapResource(pSrcBuffer), SrcOffset, NumBytes);
			}

			void STDMETHODCALLTYPE CopyTextureRegion(
				const D3D12_TEXTURE_COPY_LOCATION *pDst, UINT DstX, UINT DstY, UINT DstZ,
				const D3D12_TEXTURE_COPY_LOCATION *pSrc, const D3D12_BOX *pSrcBox) override
			{
				D3D12_TEXTURE_COPY_LOCATION dstStorage = {};
				D3D12_TEXTURE_COPY_LOCATION srcStorage = {};
				const D3D12_TEXTURE_COPY_LOCATION *activeDst = pDst;
				const D3D12_TEXTURE_COPY_LOCATION *activeSrc = pSrc;
				DX12WrapperRuntimeUtils::UnwrapTextureCopyLocation(pDst, &dstStorage, &activeDst);
				DX12WrapperRuntimeUtils::UnwrapTextureCopyLocation(pSrc, &srcStorage, &activeSrc);
				mCommandList->CopyTextureRegion(activeDst, DstX, DstY, DstZ, activeSrc, pSrcBox);
			}

			void STDMETHODCALLTYPE CopyResource(ID3D12Resource *pDstResource, ID3D12Resource *pSrcResource) override
			{
				mCommandList->CopyResource(
					DX12WrapperRuntimeUtils::UnwrapResource(pDstResource),
					DX12WrapperRuntimeUtils::UnwrapResource(pSrcResource));
			}

			void STDMETHODCALLTYPE CopyTiles(
				ID3D12Resource *pTiledResource,
				const D3D12_TILED_RESOURCE_COORDINATE *pTileRegionStartCoordinate,
				const D3D12_TILE_REGION_SIZE *pTileRegionSize, ID3D12Resource *pBuffer,
				UINT64 BufferStartOffsetInBytes, D3D12_TILE_COPY_FLAGS Flags) override
			{
				mCommandList->CopyTiles(
					DX12WrapperRuntimeUtils::UnwrapResource(pTiledResource),
					pTileRegionStartCoordinate, pTileRegionSize,
					DX12WrapperRuntimeUtils::UnwrapResource(pBuffer), BufferStartOffsetInBytes, Flags);
			}

			void STDMETHODCALLTYPE ResolveSubresource(
				ID3D12Resource *pDstResource, UINT DstSubresource,
				ID3D12Resource *pSrcResource, UINT SrcSubresource, DXGI_FORMAT Format) override
			{
				mCommandList->ResolveSubresource(
					DX12WrapperRuntimeUtils::UnwrapResource(pDstResource), DstSubresource,
					DX12WrapperRuntimeUtils::UnwrapResource(pSrcResource), SrcSubresource, Format);
			}

			void STDMETHODCALLTYPE IASetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY PrimitiveTopology) override
			{
				mCommandList->IASetPrimitiveTopology(PrimitiveTopology);
			}

			void STDMETHODCALLTYPE RSSetViewports(UINT NumViewports, const D3D12_VIEWPORT *pViewports) override
			{
				mCommandList->RSSetViewports(NumViewports, pViewports);
			}

			void STDMETHODCALLTYPE RSSetScissorRects(UINT NumRects, const D3D12_RECT *pRects) override
			{
				mCommandList->RSSetScissorRects(NumRects, pRects);
			}

			void STDMETHODCALLTYPE OMSetBlendFactor(const FLOAT BlendFactor[4]) override
			{
				mCommandList->OMSetBlendFactor(BlendFactor);
			}

			void STDMETHODCALLTYPE OMSetStencilRef(UINT StencilRef) override
			{
				mCommandList->OMSetStencilRef(StencilRef);
			}

			void STDMETHODCALLTYPE SetPipelineState(ID3D12PipelineState *pPipelineState) override
			{
				DX12WrapperRuntimeUtils::SetPipelineState(mCommandList, pPipelineState, mOrigSetPipelineState);
			}

			void STDMETHODCALLTYPE ResourceBarrier(UINT NumBarriers, const D3D12_RESOURCE_BARRIER *pBarriers) override
			{
				DX12WrapperRuntimeUtils::ResourceBarrier(mCommandList, NumBarriers, pBarriers, mOrigResourceBarrier);
			}

			void STDMETHODCALLTYPE ExecuteBundle(ID3D12GraphicsCommandList *pCommandList) override
			{
				mCommandList->ExecuteBundle(DX12WrapperRuntimeUtils::UnwrapGraphicsCommandList(pCommandList));
			}

			void STDMETHODCALLTYPE SetDescriptorHeaps(UINT NumDescriptorHeaps, ID3D12DescriptorHeap *const *ppDescriptorHeaps) override
			{
				std::vector<ID3D12DescriptorHeap*> unwrapped;
				ID3D12DescriptorHeap *const *activeHeaps = ppDescriptorHeaps;
				if (ppDescriptorHeaps && NumDescriptorHeaps) {
					unwrapped.resize(NumDescriptorHeaps);
					for (UINT i = 0; i < NumDescriptorHeaps; ++i)
						unwrapped[i] = DX12WrapperRuntimeUtils::UnwrapDescriptorHeap(ppDescriptorHeaps[i]);
					activeHeaps = unwrapped.data();
				}
				mCommandList->SetDescriptorHeaps(NumDescriptorHeaps, activeHeaps);
			}

			void STDMETHODCALLTYPE SetComputeRootSignature(ID3D12RootSignature *pRootSignature) override
			{
				mCommandList->SetComputeRootSignature(DX12WrapperRuntimeUtils::UnwrapRootSignature(pRootSignature));
			}

			void STDMETHODCALLTYPE SetGraphicsRootSignature(ID3D12RootSignature *pRootSignature) override
			{
				mCommandList->SetGraphicsRootSignature(DX12WrapperRuntimeUtils::UnwrapRootSignature(pRootSignature));
			}

			void STDMETHODCALLTYPE SetComputeRootDescriptorTable(
				UINT RootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor) override
			{
				mCommandList->SetComputeRootDescriptorTable(RootParameterIndex, BaseDescriptor);
			}

			void STDMETHODCALLTYPE SetGraphicsRootDescriptorTable(
				UINT RootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor) override
			{
				mCommandList->SetGraphicsRootDescriptorTable(RootParameterIndex, BaseDescriptor);
			}

			void STDMETHODCALLTYPE SetComputeRoot32BitConstant(
				UINT RootParameterIndex, UINT SrcData, UINT DestOffsetIn32BitValues) override
			{
				mCommandList->SetComputeRoot32BitConstant(RootParameterIndex, SrcData, DestOffsetIn32BitValues);
			}

			void STDMETHODCALLTYPE SetGraphicsRoot32BitConstant(
				UINT RootParameterIndex, UINT SrcData, UINT DestOffsetIn32BitValues) override
			{
				mCommandList->SetGraphicsRoot32BitConstant(RootParameterIndex, SrcData, DestOffsetIn32BitValues);
			}

			void STDMETHODCALLTYPE SetComputeRoot32BitConstants(
				UINT RootParameterIndex, UINT Num32BitValuesToSet, const void *pSrcData, UINT DestOffsetIn32BitValues) override
			{
				mCommandList->SetComputeRoot32BitConstants(
					RootParameterIndex, Num32BitValuesToSet, pSrcData, DestOffsetIn32BitValues);
			}

			void STDMETHODCALLTYPE SetGraphicsRoot32BitConstants(
				UINT RootParameterIndex, UINT Num32BitValuesToSet, const void *pSrcData, UINT DestOffsetIn32BitValues) override
			{
				mCommandList->SetGraphicsRoot32BitConstants(
					RootParameterIndex, Num32BitValuesToSet, pSrcData, DestOffsetIn32BitValues);
			}

			void STDMETHODCALLTYPE SetComputeRootConstantBufferView(UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) override
			{
				mCommandList->SetComputeRootConstantBufferView(RootParameterIndex, BufferLocation);
			}

			void STDMETHODCALLTYPE SetGraphicsRootConstantBufferView(UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) override
			{
				mCommandList->SetGraphicsRootConstantBufferView(RootParameterIndex, BufferLocation);
			}

			void STDMETHODCALLTYPE SetComputeRootShaderResourceView(UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) override
			{
				mCommandList->SetComputeRootShaderResourceView(RootParameterIndex, BufferLocation);
			}

			void STDMETHODCALLTYPE SetGraphicsRootShaderResourceView(UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) override
			{
				mCommandList->SetGraphicsRootShaderResourceView(RootParameterIndex, BufferLocation);
			}

			void STDMETHODCALLTYPE SetComputeRootUnorderedAccessView(UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) override
			{
				mCommandList->SetComputeRootUnorderedAccessView(RootParameterIndex, BufferLocation);
			}

			void STDMETHODCALLTYPE SetGraphicsRootUnorderedAccessView(UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) override
			{
				mCommandList->SetGraphicsRootUnorderedAccessView(RootParameterIndex, BufferLocation);
			}

			void STDMETHODCALLTYPE IASetIndexBuffer(const D3D12_INDEX_BUFFER_VIEW *pView) override
			{
				DX12WrapperRuntimeUtils::IASetIndexBuffer(mCommandList, pView, mOrigIASetIndexBuffer);
			}

			void STDMETHODCALLTYPE IASetVertexBuffers(UINT StartSlot, UINT NumViews, const D3D12_VERTEX_BUFFER_VIEW *pViews) override
			{
				DX12WrapperRuntimeUtils::IASetVertexBuffers(
					mCommandList, StartSlot, NumViews, pViews, mOrigIASetVertexBuffers);
			}

			void STDMETHODCALLTYPE SOSetTargets(UINT StartSlot, UINT NumViews, const D3D12_STREAM_OUTPUT_BUFFER_VIEW *pViews) override
			{
				mCommandList->SOSetTargets(StartSlot, NumViews, pViews);
			}

			void STDMETHODCALLTYPE OMSetRenderTargets(
				UINT NumRenderTargetDescriptors, const D3D12_CPU_DESCRIPTOR_HANDLE *pRenderTargetDescriptors,
				BOOL RTsSingleHandleToDescriptorRange, const D3D12_CPU_DESCRIPTOR_HANDLE *pDepthStencilDescriptor) override
			{
				mCommandList->OMSetRenderTargets(
					NumRenderTargetDescriptors, pRenderTargetDescriptors,
					RTsSingleHandleToDescriptorRange, pDepthStencilDescriptor);
			}

			void STDMETHODCALLTYPE ClearDepthStencilView(
				D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView, D3D12_CLEAR_FLAGS ClearFlags,
				FLOAT Depth, UINT8 Stencil, UINT NumRects, const D3D12_RECT *pRects) override
			{
				mCommandList->ClearDepthStencilView(
					DepthStencilView, ClearFlags, Depth, Stencil, NumRects, pRects);
			}

			void STDMETHODCALLTYPE ClearRenderTargetView(
				D3D12_CPU_DESCRIPTOR_HANDLE RenderTargetView, const FLOAT ColorRGBA[4],
				UINT NumRects, const D3D12_RECT *pRects) override
			{
				mCommandList->ClearRenderTargetView(RenderTargetView, ColorRGBA, NumRects, pRects);
			}

			void STDMETHODCALLTYPE ClearUnorderedAccessViewUint(
				D3D12_GPU_DESCRIPTOR_HANDLE ViewGPUHandleInCurrentHeap,
				D3D12_CPU_DESCRIPTOR_HANDLE ViewCPUHandle, ID3D12Resource *pResource,
				const UINT Values[4], UINT NumRects, const D3D12_RECT *pRects) override
			{
				mCommandList->ClearUnorderedAccessViewUint(
					ViewGPUHandleInCurrentHeap, ViewCPUHandle,
					DX12WrapperRuntimeUtils::UnwrapResource(pResource), Values, NumRects, pRects);
			}

			void STDMETHODCALLTYPE ClearUnorderedAccessViewFloat(
				D3D12_GPU_DESCRIPTOR_HANDLE ViewGPUHandleInCurrentHeap,
				D3D12_CPU_DESCRIPTOR_HANDLE ViewCPUHandle, ID3D12Resource *pResource,
				const FLOAT Values[4], UINT NumRects, const D3D12_RECT *pRects) override
			{
				mCommandList->ClearUnorderedAccessViewFloat(
					ViewGPUHandleInCurrentHeap, ViewCPUHandle,
					DX12WrapperRuntimeUtils::UnwrapResource(pResource), Values, NumRects, pRects);
			}

			void STDMETHODCALLTYPE DiscardResource(ID3D12Resource *pResource, const D3D12_DISCARD_REGION *pRegion) override
			{
				mCommandList->DiscardResource(DX12WrapperRuntimeUtils::UnwrapResource(pResource), pRegion);
			}

			void STDMETHODCALLTYPE BeginQuery(ID3D12QueryHeap *pQueryHeap, D3D12_QUERY_TYPE Type, UINT Index) override
			{
				mCommandList->BeginQuery(DX12WrapperRuntimeUtils::UnwrapQueryHeap(pQueryHeap), Type, Index);
			}

			void STDMETHODCALLTYPE EndQuery(ID3D12QueryHeap *pQueryHeap, D3D12_QUERY_TYPE Type, UINT Index) override
			{
				mCommandList->EndQuery(DX12WrapperRuntimeUtils::UnwrapQueryHeap(pQueryHeap), Type, Index);
			}

			void STDMETHODCALLTYPE ResolveQueryData(
				ID3D12QueryHeap *pQueryHeap, D3D12_QUERY_TYPE Type, UINT StartIndex,
				UINT NumQueries, ID3D12Resource *pDestinationBuffer, UINT64 AlignedDestinationBufferOffset) override
			{
				mCommandList->ResolveQueryData(
					DX12WrapperRuntimeUtils::UnwrapQueryHeap(pQueryHeap), Type, StartIndex, NumQueries,
					DX12WrapperRuntimeUtils::UnwrapResource(pDestinationBuffer), AlignedDestinationBufferOffset);
			}

			void STDMETHODCALLTYPE SetPredication(
				ID3D12Resource *pBuffer, UINT64 AlignedBufferOffset, D3D12_PREDICATION_OP Operation) override
			{
				mCommandList->SetPredication(
					DX12WrapperRuntimeUtils::UnwrapResource(pBuffer), AlignedBufferOffset, Operation);
			}

			void STDMETHODCALLTYPE SetMarker(UINT Metadata, const void *pData, UINT Size) override
			{
				mCommandList->SetMarker(Metadata, pData, Size);
			}

			void STDMETHODCALLTYPE BeginEvent(UINT Metadata, const void *pData, UINT Size) override
			{
				mCommandList->BeginEvent(Metadata, pData, Size);
			}

			void STDMETHODCALLTYPE EndEvent() override
			{
				mCommandList->EndEvent();
			}

			void STDMETHODCALLTYPE ExecuteIndirect(
				ID3D12CommandSignature *pCommandSignature, UINT MaxCommandCount,
				ID3D12Resource *pArgumentBuffer, UINT64 ArgumentBufferOffset,
				ID3D12Resource *pCountBuffer, UINT64 CountBufferOffset) override
			{
				DX12WrapperRuntimeUtils::ExecuteIndirect(
					mCommandList,
					pCommandSignature,
					MaxCommandCount,
					pArgumentBuffer,
					ArgumentBufferOffset,
					pCountBuffer,
					CountBufferOffset,
					mOrigExecuteIndirect);
			}

			void STDMETHODCALLTYPE AtomicCopyBufferUINT(
				ID3D12Resource *pDstBuffer, UINT64 DstOffset, ID3D12Resource *pSrcBuffer,
				UINT64 SrcOffset, UINT Dependencies, ID3D12Resource *const *ppDependentResources,
				const D3D12_SUBRESOURCE_RANGE_UINT64 *pDependentSubresourceRanges) override
			{
				ID3D12GraphicsCommandList1 *commandList1 = nullptr;
				if (FAILED(QueryCommandList1(&commandList1)))
					return;
				std::vector<ID3D12Resource*> dependentResources;
				ID3D12Resource *const *activeDependentResources = ppDependentResources;
				DX12WrapperRuntimeUtils::UnwrapResourceArray(
					ppDependentResources, Dependencies, &dependentResources, &activeDependentResources);
				commandList1->AtomicCopyBufferUINT(
					DX12WrapperRuntimeUtils::UnwrapResource(pDstBuffer), DstOffset,
					DX12WrapperRuntimeUtils::UnwrapResource(pSrcBuffer), SrcOffset,
					Dependencies, activeDependentResources, pDependentSubresourceRanges);
				commandList1->Release();
			}

			void STDMETHODCALLTYPE AtomicCopyBufferUINT64(
				ID3D12Resource *pDstBuffer, UINT64 DstOffset, ID3D12Resource *pSrcBuffer,
				UINT64 SrcOffset, UINT Dependencies, ID3D12Resource *const *ppDependentResources,
				const D3D12_SUBRESOURCE_RANGE_UINT64 *pDependentSubresourceRanges) override
			{
				ID3D12GraphicsCommandList1 *commandList1 = nullptr;
				if (FAILED(QueryCommandList1(&commandList1)))
					return;
				std::vector<ID3D12Resource*> dependentResources;
				ID3D12Resource *const *activeDependentResources = ppDependentResources;
				DX12WrapperRuntimeUtils::UnwrapResourceArray(
					ppDependentResources, Dependencies, &dependentResources, &activeDependentResources);
				commandList1->AtomicCopyBufferUINT64(
					DX12WrapperRuntimeUtils::UnwrapResource(pDstBuffer), DstOffset,
					DX12WrapperRuntimeUtils::UnwrapResource(pSrcBuffer), SrcOffset,
					Dependencies, activeDependentResources, pDependentSubresourceRanges);
				commandList1->Release();
			}

			void STDMETHODCALLTYPE OMSetDepthBounds(FLOAT Min, FLOAT Max) override
			{
				ID3D12GraphicsCommandList1 *commandList1 = nullptr;
				if (SUCCEEDED(QueryCommandList1(&commandList1))) {
					commandList1->OMSetDepthBounds(Min, Max);
					commandList1->Release();
				}
			}

			void STDMETHODCALLTYPE SetSamplePositions(
				UINT NumSamplesPerPixel, UINT NumPixels, D3D12_SAMPLE_POSITION *pSamplePositions) override
			{
				ID3D12GraphicsCommandList1 *commandList1 = nullptr;
				if (SUCCEEDED(QueryCommandList1(&commandList1))) {
					commandList1->SetSamplePositions(NumSamplesPerPixel, NumPixels, pSamplePositions);
					commandList1->Release();
				}
			}

			void STDMETHODCALLTYPE ResolveSubresourceRegion(
				ID3D12Resource *pDstResource, UINT DstSubresource, UINT DstX, UINT DstY,
				ID3D12Resource *pSrcResource, UINT SrcSubresource, RECT *pSrcRect,
				DXGI_FORMAT Format, D3D12_RESOLVE_MODE ResolveMode) override
			{
				ID3D12GraphicsCommandList1 *commandList1 = nullptr;
				if (FAILED(QueryCommandList1(&commandList1)))
					return;
				commandList1->ResolveSubresourceRegion(
					DX12WrapperRuntimeUtils::UnwrapResource(pDstResource), DstSubresource, DstX, DstY,
					DX12WrapperRuntimeUtils::UnwrapResource(pSrcResource), SrcSubresource,
					pSrcRect, Format, ResolveMode);
				commandList1->Release();
			}

			void STDMETHODCALLTYPE SetViewInstanceMask(UINT Mask) override
			{
				ID3D12GraphicsCommandList1 *commandList1 = nullptr;
				if (SUCCEEDED(QueryCommandList1(&commandList1))) {
					commandList1->SetViewInstanceMask(Mask);
					commandList1->Release();
				}
			}

			void STDMETHODCALLTYPE WriteBufferImmediate(
				UINT Count, const D3D12_WRITEBUFFERIMMEDIATE_PARAMETER *pParams,
				const D3D12_WRITEBUFFERIMMEDIATE_MODE *pModes) override
			{
				ID3D12GraphicsCommandList2 *commandList2 = nullptr;
				if (SUCCEEDED(QueryCommandList2(&commandList2))) {
					commandList2->WriteBufferImmediate(Count, pParams, pModes);
					commandList2->Release();
				}
			}

			void STDMETHODCALLTYPE SetProtectedResourceSession(ID3D12ProtectedResourceSession *pProtectedResourceSession) override
			{
				ID3D12GraphicsCommandList3 *commandList3 = nullptr;
				if (SUCCEEDED(QueryCommandList3(&commandList3))) {
					commandList3->SetProtectedResourceSession(
						DX12WrapperRuntimeUtils::UnwrapProtectedResourceSession(pProtectedResourceSession));
					commandList3->Release();
				}
			}

			void STDMETHODCALLTYPE BeginRenderPass(
				UINT NumRenderTargets, const D3D12_RENDER_PASS_RENDER_TARGET_DESC *pRenderTargets,
				const D3D12_RENDER_PASS_DEPTH_STENCIL_DESC *pDepthStencil, D3D12_RENDER_PASS_FLAGS Flags) override
			{
				ID3D12GraphicsCommandList4 *commandList4 = nullptr;
				if (SUCCEEDED(QueryCommandList4(&commandList4))) {
					commandList4->BeginRenderPass(NumRenderTargets, pRenderTargets, pDepthStencil, Flags);
					commandList4->Release();
				}
			}

			void STDMETHODCALLTYPE EndRenderPass() override
			{
				ID3D12GraphicsCommandList4 *commandList4 = nullptr;
				if (SUCCEEDED(QueryCommandList4(&commandList4))) {
					commandList4->EndRenderPass();
					commandList4->Release();
				}
			}

			void STDMETHODCALLTYPE InitializeMetaCommand(
				ID3D12MetaCommand *pMetaCommand, const void *pInitializationParametersData,
				SIZE_T InitializationParametersDataSizeInBytes) override
			{
				ID3D12GraphicsCommandList4 *commandList4 = nullptr;
				if (SUCCEEDED(QueryCommandList4(&commandList4))) {
					commandList4->InitializeMetaCommand(
						DX12WrapperRuntimeUtils::UnwrapMetaCommand(pMetaCommand),
						pInitializationParametersData, InitializationParametersDataSizeInBytes);
					commandList4->Release();
				}
			}

			void STDMETHODCALLTYPE ExecuteMetaCommand(
				ID3D12MetaCommand *pMetaCommand, const void *pExecutionParametersData,
				SIZE_T ExecutionParametersDataSizeInBytes) override
			{
				ID3D12GraphicsCommandList4 *commandList4 = nullptr;
				if (SUCCEEDED(QueryCommandList4(&commandList4))) {
					commandList4->ExecuteMetaCommand(
						DX12WrapperRuntimeUtils::UnwrapMetaCommand(pMetaCommand),
						pExecutionParametersData, ExecutionParametersDataSizeInBytes);
					commandList4->Release();
				}
			}

			void STDMETHODCALLTYPE BuildRaytracingAccelerationStructure(
				const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC *pDesc,
				UINT NumPostbuildInfoDescs, const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC *pPostbuildInfoDescs) override
			{
				ID3D12GraphicsCommandList4 *commandList4 = nullptr;
				if (SUCCEEDED(QueryCommandList4(&commandList4))) {
					commandList4->BuildRaytracingAccelerationStructure(pDesc, NumPostbuildInfoDescs, pPostbuildInfoDescs);
					commandList4->Release();
				}
			}

			void STDMETHODCALLTYPE EmitRaytracingAccelerationStructurePostbuildInfo(
				const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC *pDesc,
				UINT NumSourceAccelerationStructures, const D3D12_GPU_VIRTUAL_ADDRESS *pSourceAccelerationStructureData) override
			{
				ID3D12GraphicsCommandList4 *commandList4 = nullptr;
				if (SUCCEEDED(QueryCommandList4(&commandList4))) {
					commandList4->EmitRaytracingAccelerationStructurePostbuildInfo(
						pDesc, NumSourceAccelerationStructures, pSourceAccelerationStructureData);
					commandList4->Release();
				}
			}

			void STDMETHODCALLTYPE CopyRaytracingAccelerationStructure(
				D3D12_GPU_VIRTUAL_ADDRESS DestAccelerationStructureData,
				D3D12_GPU_VIRTUAL_ADDRESS SourceAccelerationStructureData,
				D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE Mode) override
			{
				ID3D12GraphicsCommandList4 *commandList4 = nullptr;
				if (SUCCEEDED(QueryCommandList4(&commandList4))) {
					commandList4->CopyRaytracingAccelerationStructure(
						DestAccelerationStructureData, SourceAccelerationStructureData, Mode);
					commandList4->Release();
				}
			}

			void STDMETHODCALLTYPE SetPipelineState1(ID3D12StateObject *pStateObject) override
			{
				ID3D12GraphicsCommandList4 *commandList4 = nullptr;
				if (SUCCEEDED(QueryCommandList4(&commandList4))) {
					commandList4->SetPipelineState1(DX12WrapperRuntimeUtils::UnwrapStateObject(pStateObject));
					commandList4->Release();
				}
			}

			void STDMETHODCALLTYPE DispatchRays(const D3D12_DISPATCH_RAYS_DESC *pDesc) override
			{
				ID3D12GraphicsCommandList4 *commandList4 = nullptr;
				if (SUCCEEDED(QueryCommandList4(&commandList4))) {
					commandList4->DispatchRays(pDesc);
					commandList4->Release();
				}
			}

			void STDMETHODCALLTYPE RSSetShadingRate(D3D12_SHADING_RATE baseShadingRate, const D3D12_SHADING_RATE_COMBINER *combiners) override
			{
				ID3D12GraphicsCommandList5 *commandList5 = nullptr;
				if (SUCCEEDED(QueryCommandList5(&commandList5))) {
					commandList5->RSSetShadingRate(baseShadingRate, combiners);
					commandList5->Release();
				}
			}

			void STDMETHODCALLTYPE RSSetShadingRateImage(ID3D12Resource *pShadingRateImage) override
			{
				ID3D12GraphicsCommandList5 *commandList5 = nullptr;
				if (SUCCEEDED(QueryCommandList5(&commandList5))) {
					commandList5->RSSetShadingRateImage(DX12WrapperRuntimeUtils::UnwrapResource(pShadingRateImage));
					commandList5->Release();
				}
			}

			void STDMETHODCALLTYPE DispatchMesh(UINT ThreadGroupCountX, UINT ThreadGroupCountY, UINT ThreadGroupCountZ) override
			{
				ID3D12GraphicsCommandList6 *commandList6 = nullptr;
				if (SUCCEEDED(QueryCommandList6(&commandList6))) {
					commandList6->DispatchMesh(ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);
					commandList6->Release();
				}
			}

			void STDMETHODCALLTYPE Barrier(
				UINT32 NumBarrierGroups, const D3D12_BARRIER_GROUP *pBarrierGroups) override
			{
				ID3D12GraphicsCommandList7 *commandList7 = nullptr;
				if (FAILED(QueryCommandList7(&commandList7)))
					return;
				std::vector<D3D12_BARRIER_GROUP> groupStorage;
				std::vector<std::vector<D3D12_TEXTURE_BARRIER>> textureStorage;
				std::vector<std::vector<D3D12_BUFFER_BARRIER>> bufferStorage;
				const D3D12_BARRIER_GROUP *activeBarrierGroups = pBarrierGroups;
				DX12WrapperRuntimeUtils::UnwrapBarrierGroups(
					pBarrierGroups,
					NumBarrierGroups,
					&groupStorage,
					&textureStorage,
					&bufferStorage,
					&activeBarrierGroups);
				DX12WrapperRuntimeUtils::LogCommandListStage("Barrier", "before", mCommandList);
				commandList7->Barrier(NumBarrierGroups, activeBarrierGroups);
				DX12WrapperRuntimeUtils::LogCommandListStage("Barrier", "after", mCommandList);
				commandList7->Release();
			}

			void STDMETHODCALLTYPE OMSetFrontAndBackStencilRef(UINT FrontStencilRef, UINT BackStencilRef) override
			{
				ID3D12GraphicsCommandList8 *commandList8 = nullptr;
				if (SUCCEEDED(QueryCommandList8(&commandList8))) {
					commandList8->OMSetFrontAndBackStencilRef(FrontStencilRef, BackStencilRef);
					commandList8->Release();
				}
			}

			void STDMETHODCALLTYPE RSSetDepthBias(FLOAT DepthBias, FLOAT DepthBiasClamp, FLOAT SlopeScaledDepthBias) override
			{
				ID3D12GraphicsCommandList9 *commandList9 = nullptr;
				if (SUCCEEDED(QueryCommandList9(&commandList9))) {
					commandList9->RSSetDepthBias(DepthBias, DepthBiasClamp, SlopeScaledDepthBias);
					commandList9->Release();
				}
			}

			void STDMETHODCALLTYPE IASetIndexBufferStripCutValue(D3D12_INDEX_BUFFER_STRIP_CUT_VALUE IBStripCutValue) override
			{
				ID3D12GraphicsCommandList9 *commandList9 = nullptr;
				if (SUCCEEDED(QueryCommandList9(&commandList9))) {
					commandList9->IASetIndexBufferStripCutValue(IBStripCutValue);
					commandList9->Release();
				}
			}

			void STDMETHODCALLTYPE SetProgram(const D3D12_SET_PROGRAM_DESC *pDesc) override
			{
				ID3D12GraphicsCommandList10 *commandList10 = nullptr;
				if (SUCCEEDED(QueryCommandList10(&commandList10))) {
					commandList10->SetProgram(pDesc);
					commandList10->Release();
				}
			}

			void STDMETHODCALLTYPE DispatchGraph(const D3D12_DISPATCH_GRAPH_DESC *pDesc) override
			{
				ID3D12GraphicsCommandList10 *commandList10 = nullptr;
				if (SUCCEEDED(QueryCommandList10(&commandList10))) {
					commandList10->DispatchGraph(pDesc);
					commandList10->Release();
				}
			}

		public:
			std::atomic<long> mRefCount{1};
			ID3D12GraphicsCommandList *mCommandList = nullptr;
			DX12CommandListHookSignatures::CloseCommandList mOrigClose = nullptr;
			DX12CommandListHookSignatures::ResetCommandList mOrigReset = nullptr;
			DX12CommandListHookSignatures::DrawInstanced mOrigDrawInstanced = nullptr;
			DX12CommandListHookSignatures::DrawIndexedInstanced mOrigDrawIndexedInstanced = nullptr;
			DX12CommandListHookSignatures::Dispatch mOrigDispatch = nullptr;
			DX12CommandListHookSignatures::SetPipelineState mOrigSetPipelineState = nullptr;
			DX12CommandListHookSignatures::ResourceBarrier mOrigResourceBarrier = nullptr;
			DX12CommandListHookSignatures::IaSetIndexBuffer mOrigIASetIndexBuffer = nullptr;
			DX12CommandListHookSignatures::IaSetVertexBuffers mOrigIASetVertexBuffers = nullptr;
			DX12CommandListHookSignatures::ExecuteIndirect mOrigExecuteIndirect = nullptr;
		};

		class DeviceWrapper
		{
		public:
			static bool TryWrapCommandAllocator(REFIID riid, void **object)
			{
				if (!object || !*object || !IsD3D12CommandAllocatorIid(riid))
					return false;

				IUnknown *originalObject = static_cast<IUnknown*>(*object);
				ID3D12CommandAllocator *allocator = nullptr;
				if (FAILED(static_cast<IUnknown*>(*object)->QueryInterface(IID_PPV_ARGS(&allocator))))
					return false;

				WrapperBase *existing = WrapperBase::FindByIdentity(allocator);
				if (existing) {
					CommandAllocatorWrapper *wrapper = dynamic_cast<CommandAllocatorWrapper*>(existing);
					if (!wrapper) {
						allocator->Release();
						return false;
					}
					*object = static_cast<ID3D12CommandAllocator*>(wrapper);
					reinterpret_cast<IUnknown*>(*object)->AddRef();
					allocator->Release();
					originalObject->Release();
					return true;
				}

				*object = static_cast<ID3D12CommandAllocator*>(new CommandAllocatorWrapper(allocator));
				allocator->Release();
				originalObject->Release();
				return true;
			}

			static bool TryWrapFence(REFIID riid, void **object)
			{
				if (!object || !*object || !IsD3D12FenceIid(riid))
					return false;

				IUnknown *originalObject = static_cast<IUnknown*>(*object);
				ID3D12Fence *fence = nullptr;
				if (FAILED(static_cast<IUnknown*>(*object)->QueryInterface(IID_PPV_ARGS(&fence))))
					return false;

				WrapperBase *existing = WrapperBase::FindByIdentity(fence);
				if (existing) {
					FenceWrapper *wrapper = dynamic_cast<FenceWrapper*>(existing);
					if (!wrapper) {
						fence->Release();
						return false;
					}
					void *wrapperInterface = wrapper->GetFenceWrapperInterface(riid);
					if (!wrapperInterface) {
						fence->Release();
						return false;
					}
					*object = wrapperInterface;
					reinterpret_cast<IUnknown*>(*object)->AddRef();
					fence->Release();
					originalObject->Release();
					return true;
				}

				FenceWrapper *wrapper = new FenceWrapper(fence);
				void *wrapperInterface = wrapper->GetFenceWrapperInterface(riid);
				if (!wrapperInterface) {
					fence->Release();
					return false;
				}
				*object = wrapperInterface;
				fence->Release();
				originalObject->Release();
				return true;
			}

			static bool TryWrapDescriptorHeap(REFIID riid, void **object)
			{
				if (!object || !*object || !IsD3D12DescriptorHeapIid(riid))
					return false;

				IUnknown *originalObject = static_cast<IUnknown*>(*object);
				ID3D12DescriptorHeap *heap = nullptr;
				if (FAILED(static_cast<IUnknown*>(*object)->QueryInterface(IID_PPV_ARGS(&heap))))
					return false;

				WrapperBase *existing = WrapperBase::FindByIdentity(heap);
				if (existing) {
					DescriptorHeapWrapper *wrapper = dynamic_cast<DescriptorHeapWrapper*>(existing);
					if (!wrapper) {
						heap->Release();
						return false;
					}
					void *wrapperInterface = wrapper->GetDescriptorHeapWrapperInterface(riid);
					if (!wrapperInterface) {
						heap->Release();
						return false;
					}
					*object = wrapperInterface;
					reinterpret_cast<IUnknown*>(*object)->AddRef();
					heap->Release();
					originalObject->Release();
					return true;
				}

				DescriptorHeapWrapper *wrapper = new DescriptorHeapWrapper(heap);
				void *wrapperInterface = wrapper->GetDescriptorHeapWrapperInterface(riid);
				if (!wrapperInterface) {
					heap->Release();
					return false;
				}
				*object = wrapperInterface;
				heap->Release();
				originalObject->Release();
				return true;
			}

			static bool TryWrapResource(REFIID riid, void **object)
			{
				if (!object || !*object || !IsD3D12ResourceIid(riid))
					return false;

				IUnknown *originalObject = static_cast<IUnknown*>(*object);
				ID3D12Resource *resource = nullptr;
				if (FAILED(static_cast<IUnknown*>(*object)->QueryInterface(IID_PPV_ARGS(&resource))))
					return false;

				WrapperBase *existing = WrapperBase::FindByIdentity(resource);
				if (existing) {
					ResourceWrapper *wrapper = dynamic_cast<ResourceWrapper*>(existing);
					if (!wrapper) {
						resource->Release();
						return false;
					}
					void *wrapperInterface = wrapper->GetResourceWrapperInterface(riid);
					if (!wrapperInterface) {
						resource->Release();
						return false;
					}
					*object = wrapperInterface;
					reinterpret_cast<IUnknown*>(*object)->AddRef();
					resource->Release();
					originalObject->Release();
					return true;
				}

				ResourceWrapper *wrapper = new ResourceWrapper(resource);
				void *wrapperInterface = wrapper->GetResourceWrapperInterface(riid);
				if (!wrapperInterface) {
					resource->Release();
					return false;
				}
				*object = wrapperInterface;
				resource->Release();
				originalObject->Release();
				return true;
			}

			static bool TryWrapCommandQueue(REFIID riid, void **object)
			{
				if (!object || !*object || !IsD3D12CommandQueueIid(riid))
					return false;

				IUnknown *originalObject = static_cast<IUnknown*>(*object);
				ID3D12CommandQueue *queue = nullptr;
				if (FAILED(static_cast<IUnknown*>(*object)->QueryInterface(IID_PPV_ARGS(&queue))))
					return false;

				WrapperBase *existing = WrapperBase::FindByIdentity(queue);
				if (existing) {
					CommandQueueWrapper *wrapper = dynamic_cast<CommandQueueWrapper*>(existing);
					if (!wrapper) {
						queue->Release();
						return false;
					}
					*object = static_cast<ID3D12CommandQueue*>(wrapper);
					reinterpret_cast<IUnknown*>(*object)->AddRef();
					queue->Release();
					originalObject->Release();
					return true;
				}

				*object = static_cast<ID3D12CommandQueue*>(new CommandQueueWrapper(queue));
				queue->Release();
				originalObject->Release();
				return true;
			}

			static bool TryWrapGraphicsCommandList(REFIID riid, void **object)
			{
				if (!object || !*object || !IsD3D12GraphicsCommandListIid(riid))
					return false;

				IUnknown *originalObject = static_cast<IUnknown*>(*object);
				ID3D12GraphicsCommandList *commandList = nullptr;
				if (FAILED(static_cast<IUnknown*>(*object)->QueryInterface(IID_PPV_ARGS(&commandList))))
					return false;

				WrapperBase *existing = WrapperBase::FindByIdentity(commandList);
				if (existing) {
					GraphicsCommandListWrapper *wrapper = dynamic_cast<GraphicsCommandListWrapper*>(existing);
					if (!wrapper) {
						commandList->Release();
						return false;
					}
					void *wrapperInterface = wrapper->GetGraphicsCommandListWrapperInterface(riid);
					if (!wrapperInterface) {
						commandList->Release();
						return false;
					}
					*object = wrapperInterface;
					reinterpret_cast<IUnknown*>(*object)->AddRef();
					commandList->Release();
					originalObject->Release();
					return true;
				}

				GraphicsCommandListWrapper *wrapper = new GraphicsCommandListWrapper(commandList);
				void *wrapperInterface = wrapper->GetGraphicsCommandListWrapperInterface(riid);
				if (!wrapperInterface) {
					commandList->Release();
					return false;
				}
				*object = wrapperInterface;
				commandList->Release();
				originalObject->Release();
				return true;
			}

			static bool TryWrapHeap(REFIID riid, void **object)
			{
				if (!object || !*object || !IsD3D12HeapIid(riid))
					return false;

				IUnknown *originalObject = static_cast<IUnknown*>(*object);
				ID3D12Heap *heap = nullptr;
				if (FAILED(static_cast<IUnknown*>(*object)->QueryInterface(IID_PPV_ARGS(&heap))))
					return false;

				WrapperBase *existing = WrapperBase::FindByIdentity(heap);
				if (existing) {
					HeapWrapper *wrapper = dynamic_cast<HeapWrapper*>(existing);
					if (!wrapper) {
						heap->Release();
						return false;
					}
					void *wrapperInterface = wrapper->GetHeapWrapperInterface(riid);
					if (!wrapperInterface) {
						heap->Release();
						return false;
					}
					*object = wrapperInterface;
					reinterpret_cast<IUnknown*>(*object)->AddRef();
					heap->Release();
					originalObject->Release();
					return true;
				}

				HeapWrapper *wrapper = new HeapWrapper(heap);
				void *wrapperInterface = wrapper->GetHeapWrapperInterface(riid);
				if (!wrapperInterface) {
					heap->Release();
					return false;
				}
				*object = wrapperInterface;
				heap->Release();
				originalObject->Release();
				return true;
			}

			static bool TryWrapPipelineState(REFIID riid, void **object)
			{
				if (!object || !*object || !IsD3D12PipelineStateIid(riid))
					return false;

				IUnknown *originalObject = static_cast<IUnknown*>(*object);
				ID3D12PipelineState *pipelineState = nullptr;
				if (FAILED(static_cast<IUnknown*>(*object)->QueryInterface(IID_PPV_ARGS(&pipelineState))))
					return false;

				WrapperBase *existing = WrapperBase::FindByIdentity(pipelineState);
				if (existing) {
					PipelineStateWrapper *wrapper = dynamic_cast<PipelineStateWrapper*>(existing);
					if (!wrapper) {
						pipelineState->Release();
						return false;
					}
					void *wrapperInterface = wrapper->GetPipelineStateWrapperInterface(riid);
					if (!wrapperInterface) {
						pipelineState->Release();
						return false;
					}
					*object = wrapperInterface;
					reinterpret_cast<IUnknown*>(*object)->AddRef();
					pipelineState->Release();
					originalObject->Release();
					return true;
				}

				PipelineStateWrapper *wrapper = new PipelineStateWrapper(pipelineState);
				void *wrapperInterface = wrapper->GetPipelineStateWrapperInterface(riid);
				if (!wrapperInterface) {
					pipelineState->Release();
					return false;
				}
				*object = wrapperInterface;
				pipelineState->Release();
				originalObject->Release();
				return true;
			}

			static bool TryWrapRootSignature(REFIID riid, void **object)
			{
				if (!object || !*object || !IsD3D12RootSignatureIid(riid))
					return false;

				IUnknown *originalObject = static_cast<IUnknown*>(*object);
				ID3D12RootSignature *rootSignature = nullptr;
				if (FAILED(static_cast<IUnknown*>(*object)->QueryInterface(IID_PPV_ARGS(&rootSignature))))
					return false;

				WrapperBase *existing = WrapperBase::FindByIdentity(rootSignature);
				if (existing) {
					RootSignatureWrapper *wrapper = dynamic_cast<RootSignatureWrapper*>(existing);
					if (!wrapper) {
						rootSignature->Release();
						return false;
					}
					void *wrapperInterface = wrapper->GetRootSignatureWrapperInterface(riid);
					if (!wrapperInterface) {
						rootSignature->Release();
						return false;
					}
					*object = wrapperInterface;
					reinterpret_cast<IUnknown*>(*object)->AddRef();
					rootSignature->Release();
					originalObject->Release();
					return true;
				}

				RootSignatureWrapper *wrapper = new RootSignatureWrapper(rootSignature);
				void *wrapperInterface = wrapper->GetRootSignatureWrapperInterface(riid);
				if (!wrapperInterface) {
					rootSignature->Release();
					return false;
				}
				*object = wrapperInterface;
				rootSignature->Release();
				originalObject->Release();
				return true;
			}

			static bool TryWrapQueryHeap(REFIID riid, void **object)
			{
				if (!object || !*object || !IsD3D12QueryHeapIid(riid))
					return false;

				IUnknown *originalObject = static_cast<IUnknown*>(*object);
				ID3D12QueryHeap *queryHeap = nullptr;
				if (FAILED(static_cast<IUnknown*>(*object)->QueryInterface(IID_PPV_ARGS(&queryHeap))))
					return false;

				WrapperBase *existing = WrapperBase::FindByIdentity(queryHeap);
				if (existing) {
					QueryHeapWrapper *wrapper = dynamic_cast<QueryHeapWrapper*>(existing);
					if (!wrapper) {
						queryHeap->Release();
						return false;
					}
					void *wrapperInterface = wrapper->GetQueryHeapWrapperInterface(riid);
					if (!wrapperInterface) {
						queryHeap->Release();
						return false;
					}
					*object = wrapperInterface;
					reinterpret_cast<IUnknown*>(*object)->AddRef();
					queryHeap->Release();
					originalObject->Release();
					return true;
				}

				QueryHeapWrapper *wrapper = new QueryHeapWrapper(queryHeap);
				void *wrapperInterface = wrapper->GetQueryHeapWrapperInterface(riid);
				if (!wrapperInterface) {
					queryHeap->Release();
					return false;
				}
				*object = wrapperInterface;
				queryHeap->Release();
				originalObject->Release();
				return true;
			}

			static bool TryWrapCommandSignature(REFIID riid, void **object)
			{
				if (!object || !*object || !IsD3D12CommandSignatureIid(riid))
					return false;

				IUnknown *originalObject = static_cast<IUnknown*>(*object);
				ID3D12CommandSignature *commandSignature = nullptr;
				if (FAILED(static_cast<IUnknown*>(*object)->QueryInterface(IID_PPV_ARGS(&commandSignature))))
					return false;

				WrapperBase *existing = WrapperBase::FindByIdentity(commandSignature);
				if (existing) {
					CommandSignatureWrapper *wrapper = dynamic_cast<CommandSignatureWrapper*>(existing);
					if (!wrapper) {
						commandSignature->Release();
						return false;
					}
					void *wrapperInterface = wrapper->GetCommandSignatureWrapperInterface(riid);
					if (!wrapperInterface) {
						commandSignature->Release();
						return false;
					}
					*object = wrapperInterface;
					reinterpret_cast<IUnknown*>(*object)->AddRef();
					commandSignature->Release();
					originalObject->Release();
					return true;
				}

				CommandSignatureWrapper *wrapper = new CommandSignatureWrapper(commandSignature);
				void *wrapperInterface = wrapper->GetCommandSignatureWrapperInterface(riid);
				if (!wrapperInterface) {
					commandSignature->Release();
					return false;
				}
				*object = wrapperInterface;
				commandSignature->Release();
				originalObject->Release();
				return true;
			}
		};

		class D3D12DeviceWrapper final : public ID3D12Device14, public WrapperBase
		{
		public:
			explicit D3D12DeviceWrapper(ID3D12Device *device)
				: WrapperBase(device)
			{
				mDevice = device;
				RegisterWrapperPointer(static_cast<ID3D12Device*>(this));
				RegisterWrapperPointer(static_cast<ID3D12Device1*>(this));
				RegisterWrapperPointer(static_cast<ID3D12Device2*>(this));
				RegisterWrapperPointer(static_cast<ID3D12Device3*>(this));
				RegisterWrapperPointer(static_cast<ID3D12Device4*>(this));
				RegisterWrapperPointer(static_cast<ID3D12Device5*>(this));
				RegisterWrapperPointer(static_cast<ID3D12Device6*>(this));
				RegisterWrapperPointer(static_cast<ID3D12Device7*>(this));
				RegisterWrapperPointer(static_cast<ID3D12Device8*>(this));
				RegisterWrapperPointer(static_cast<ID3D12Device9*>(this));
				RegisterWrapperPointer(static_cast<ID3D12Device10*>(this));
				RegisterWrapperPointer(static_cast<ID3D12Device11*>(this));
				RegisterWrapperPointer(static_cast<ID3D12Device12*>(this));
				RegisterWrapperPointer(static_cast<ID3D12Device13*>(this));
				RegisterWrapperPointer(static_cast<ID3D12Device14*>(this));
				DX12LogManager::JsonFunc("DX12Wrapper",
					"\"kind\":\"device\",\"stage\":\"create\",\"wrapper\":\"%p\",\"object\":\"%p\"",
					this, device);
			}

			HRESULT QueryDevice1(ID3D12Device1 **device)
			{
				return mDevice->QueryInterface(IID_PPV_ARGS(device));
			}

			HRESULT QueryDevice2(ID3D12Device2 **device)
			{
				return mDevice->QueryInterface(IID_PPV_ARGS(device));
			}

			HRESULT QueryDevice3(ID3D12Device3 **device)
			{
				return mDevice->QueryInterface(IID_PPV_ARGS(device));
			}

			HRESULT QueryDevice4(ID3D12Device4 **device)
			{
				return mDevice->QueryInterface(IID_PPV_ARGS(device));
			}

			HRESULT QueryDevice5(ID3D12Device5 **device)
			{
				return mDevice->QueryInterface(IID_PPV_ARGS(device));
			}

			HRESULT QueryDevice6(ID3D12Device6 **device)
			{
				return mDevice->QueryInterface(IID_PPV_ARGS(device));
			}

			HRESULT QueryDevice7(ID3D12Device7 **device)
			{
				return mDevice->QueryInterface(IID_PPV_ARGS(device));
			}

			HRESULT QueryDevice8(ID3D12Device8 **device)
			{
				return mDevice->QueryInterface(IID_PPV_ARGS(device));
			}

			HRESULT QueryDevice9(ID3D12Device9 **device)
			{
				return mDevice->QueryInterface(IID_PPV_ARGS(device));
			}

			HRESULT QueryDevice10(ID3D12Device10 **device)
			{
				return mDevice->QueryInterface(IID_PPV_ARGS(device));
			}

			HRESULT QueryDevice11(ID3D12Device11 **device)
			{
				return mDevice->QueryInterface(IID_PPV_ARGS(device));
			}

			HRESULT QueryDevice12(ID3D12Device12 **device)
			{
				return mDevice->QueryInterface(IID_PPV_ARGS(device));
			}

			HRESULT QueryDevice13(ID3D12Device13 **device)
			{
				return mDevice->QueryInterface(IID_PPV_ARGS(device));
			}

			HRESULT QueryDevice14(ID3D12Device14 **device)
			{
				return mDevice->QueryInterface(IID_PPV_ARGS(device));
			}

			void *GetDeviceWrapperInterface(REFIID riid)
			{
				if (IsEqualIID(riid, __uuidof(IUnknown)) || IsEqualIID(riid, __uuidof(ID3D12Device14)))
					return static_cast<ID3D12Device14*>(this);
				if (IsEqualIID(riid, __uuidof(ID3D12Device13)))
					return static_cast<ID3D12Device13*>(this);
				if (IsEqualIID(riid, __uuidof(ID3D12Device12)))
					return static_cast<ID3D12Device12*>(this);
				if (IsEqualIID(riid, __uuidof(ID3D12Device11)))
					return static_cast<ID3D12Device11*>(this);
				if (IsEqualIID(riid, __uuidof(ID3D12Device10)))
					return static_cast<ID3D12Device10*>(this);
				if (IsEqualIID(riid, __uuidof(ID3D12Device9)))
					return static_cast<ID3D12Device9*>(this);
				if (IsEqualIID(riid, __uuidof(ID3D12Device8)))
					return static_cast<ID3D12Device8*>(this);
				if (IsEqualIID(riid, __uuidof(ID3D12Device7)))
					return static_cast<ID3D12Device7*>(this);
				if (IsEqualIID(riid, __uuidof(ID3D12Device6)))
					return static_cast<ID3D12Device6*>(this);
				if (IsEqualIID(riid, __uuidof(ID3D12Device5)))
					return static_cast<ID3D12Device5*>(this);
				if (IsEqualIID(riid, __uuidof(ID3D12Device4)))
					return static_cast<ID3D12Device4*>(this);
				if (IsEqualIID(riid, __uuidof(ID3D12Device3)))
					return static_cast<ID3D12Device3*>(this);
				if (IsEqualIID(riid, __uuidof(ID3D12Device2)))
					return static_cast<ID3D12Device2*>(this);
				if (IsEqualIID(riid, __uuidof(ID3D12Device1)))
					return static_cast<ID3D12Device1*>(this);
				if (IsEqualIID(riid, __uuidof(ID3D12Device)))
					return static_cast<ID3D12Device*>(this);
				return nullptr;
			}

			HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) override
			{
				if (!ppvObject)
					return E_POINTER;
				*ppvObject = nullptr;

				if (IsEqualIID(riid, __uuidof(IUnknown)) || IsD3D12DeviceIid(riid)) {
					if (!IsEqualIID(riid, __uuidof(IUnknown)) && !DoesObjectSupportIid(mDevice, riid))
						return E_NOINTERFACE;
					*ppvObject = GetDeviceWrapperInterface(riid);
					if (*ppvObject) {
						AddRef();
						return S_OK;
					}
				}

				HRESULT hr = mDevice->QueryInterface(riid, ppvObject);
				if (SUCCEEDED(hr))
					DX12Wrapper::WrapD3D12DeviceResult(riid, ppvObject);
				return hr;
			}

			ULONG STDMETHODCALLTYPE AddRef() override
			{
				return static_cast<ULONG>(++mRefCount);
			}

			ULONG STDMETHODCALLTYPE Release() override
			{
				const ULONG ref = static_cast<ULONG>(--mRefCount);
				if (!ref)
					delete this;
				return ref;
			}

			HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid, UINT *pDataSize, void *pData) override
			{
				return mDevice->GetPrivateData(guid, pDataSize, pData);
			}

			HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT DataSize, const void *pData) override
			{
				return mDevice->SetPrivateData(guid, DataSize, pData);
			}

			HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown *pData) override
			{
				return mDevice->SetPrivateDataInterface(guid, pData);
			}

			HRESULT STDMETHODCALLTYPE SetName(LPCWSTR Name) override
			{
				return mDevice->SetName(Name);
			}

			UINT STDMETHODCALLTYPE GetNodeCount() override
			{
				return mDevice->GetNodeCount();
			}

			HRESULT STDMETHODCALLTYPE CreateCommandQueue(
				const D3D12_COMMAND_QUEUE_DESC *pDesc, REFIID riid, void **ppCommandQueue) override
			{
				HRESULT hr = mDevice->CreateCommandQueue(pDesc, riid, ppCommandQueue);
				DX12FrameAnalysis::LogJsonFunc("ID3D12Device::CreateCommandQueue",
					"\"hr\":\"0x%lx\",\"queue\":\"%p\"", hr, ppCommandQueue ? *ppCommandQueue : nullptr);
				if (SUCCEEDED(hr) && ppCommandQueue && *ppCommandQueue)
					DeviceWrapper::TryWrapCommandQueue(riid, ppCommandQueue);
				return hr;
			}

			HRESULT STDMETHODCALLTYPE CreateCommandAllocator(
				D3D12_COMMAND_LIST_TYPE type, REFIID riid, void **ppCommandAllocator) override
			{
				HRESULT hr = mDevice->CreateCommandAllocator(type, riid, ppCommandAllocator);
				DX12FrameAnalysis::LogJsonFunc("ID3D12Device::CreateCommandAllocator",
					"\"hr\":\"0x%lx\",\"allocator\":\"%p\"", hr,
					ppCommandAllocator ? *ppCommandAllocator : nullptr);
				if (SUCCEEDED(hr) && ppCommandAllocator && *ppCommandAllocator)
					DeviceWrapper::TryWrapCommandAllocator(riid, ppCommandAllocator);
				return hr;
			}

			HRESULT STDMETHODCALLTYPE CreateGraphicsPipelineState(
				const D3D12_GRAPHICS_PIPELINE_STATE_DESC *pDesc, REFIID riid, void **ppPipelineState) override
			{
				if (DX12InternalReplayState::IsActive())
					return mDevice->CreateGraphicsPipelineState(pDesc, riid, ppPipelineState);

				DX12GraphicsPsoReplacement replacement;
				const D3D12_GRAPHICS_PIPELINE_STATE_DESC *activeDesc = pDesc;
				if (pDesc) {
					replacement.desc = *pDesc;
					if (DX12ModReplaceShaderBytecode("vs", pDesc->VS, &replacement.vs, &replacement.vsBytes)) {
						replacement.desc.VS = replacement.vs;
						replacement.changed = true;
					}
					if (DX12ModReplaceShaderBytecode("ps", pDesc->PS, &replacement.ps, &replacement.psBytes)) {
						replacement.desc.PS = replacement.ps;
						replacement.changed = true;
					}
					if (replacement.changed)
						activeDesc = &replacement.desc;
				}

				HRESULT hr = mDevice->CreateGraphicsPipelineState(activeDesc, riid, ppPipelineState);
				DX12FrameAnalysis::LogJsonFunc("ID3D12Device::CreateGraphicsPipelineState",
					"\"hr\":\"0x%lx\",\"pso\":\"%p\"", hr, ppPipelineState ? *ppPipelineState : nullptr);
				if (SUCCEEDED(hr) && activeDesc) {
					ID3D12PipelineState *created =
						ppPipelineState ? static_cast<ID3D12PipelineState*>(*ppPipelineState) : nullptr;
					if (pDesc)
						DX12ModRecordGraphicsPipelineState(mDevice, created, pDesc);
					DX12RecordGraphicsPipelineState(created, activeDesc);
				}
				if (SUCCEEDED(hr) && ppPipelineState && *ppPipelineState)
					DeviceWrapper::TryWrapPipelineState(riid, ppPipelineState);
				return hr;
			}

			HRESULT STDMETHODCALLTYPE CreateComputePipelineState(
				const D3D12_COMPUTE_PIPELINE_STATE_DESC *pDesc, REFIID riid, void **ppPipelineState) override
			{
				if (DX12InternalReplayState::IsActive())
					return mDevice->CreateComputePipelineState(pDesc, riid, ppPipelineState);

				DX12ComputePsoReplacement replacement;
				const D3D12_COMPUTE_PIPELINE_STATE_DESC *activeDesc = pDesc;
				if (pDesc) {
					replacement.desc = *pDesc;
					if (DX12ModReplaceShaderBytecode("cs", pDesc->CS, &replacement.cs, &replacement.csBytes)) {
						replacement.desc.CS = replacement.cs;
						replacement.changed = true;
						activeDesc = &replacement.desc;
					}
				}

				HRESULT hr = mDevice->CreateComputePipelineState(activeDesc, riid, ppPipelineState);
				DX12FrameAnalysis::LogJsonFunc("ID3D12Device::CreateComputePipelineState",
					"\"hr\":\"0x%lx\",\"pso\":\"%p\"", hr, ppPipelineState ? *ppPipelineState : nullptr);
				if (SUCCEEDED(hr) && activeDesc) {
					ID3D12PipelineState *created =
						ppPipelineState ? static_cast<ID3D12PipelineState*>(*ppPipelineState) : nullptr;
					if (pDesc)
						DX12ModRecordComputePipelineState(mDevice, created, pDesc);
					DX12RecordComputePipelineState(created, activeDesc);
				}
				if (SUCCEEDED(hr) && ppPipelineState && *ppPipelineState)
					DeviceWrapper::TryWrapPipelineState(riid, ppPipelineState);
				return hr;
			}

			HRESULT STDMETHODCALLTYPE CreateCommandList(
				UINT nodeMask, D3D12_COMMAND_LIST_TYPE type, ID3D12CommandAllocator *pCommandAllocator,
				ID3D12PipelineState *pInitialState, REFIID riid, void **ppCommandList) override
			{
				ID3D12CommandAllocator *allocator = DX12WrapperRuntimeUtils::UnwrapCommandAllocator(pCommandAllocator);
				ID3D12PipelineState *initialState = DX12WrapperRuntimeUtils::UnwrapPipelineState(pInitialState);
				HRESULT hr = mDevice->CreateCommandList(
					nodeMask, type, allocator, initialState, riid, ppCommandList);
				DX12FrameAnalysis::LogJsonFunc("ID3D12Device::CreateCommandList",
					"\"hr\":\"0x%lx\",\"commandList\":\"%p\"", hr, ppCommandList ? *ppCommandList : nullptr);
				if (SUCCEEDED(hr) && ppCommandList && *ppCommandList) {
					ID3D12GraphicsCommandList *baseList = nullptr;
					if (SUCCEEDED(static_cast<IUnknown*>(*ppCommandList)->QueryInterface(IID_PPV_ARGS(&baseList)))) {
						DX12CommandListLifecycle::Register(baseList, initialState);
						baseList->Release();
					}
					DeviceWrapper::TryWrapGraphicsCommandList(riid, ppCommandList);
				}
				return hr;
			}

			HRESULT STDMETHODCALLTYPE CheckFeatureSupport(
				D3D12_FEATURE Feature, void *pFeatureSupportData, UINT FeatureSupportDataSize) override
			{
				return mDevice->CheckFeatureSupport(Feature, pFeatureSupportData, FeatureSupportDataSize);
			}

			HRESULT STDMETHODCALLTYPE CreateDescriptorHeap(
				const D3D12_DESCRIPTOR_HEAP_DESC *pDescriptorHeapDesc, REFIID riid, void **ppvHeap) override
			{
				HRESULT hr = mDevice->CreateDescriptorHeap(pDescriptorHeapDesc, riid, ppvHeap);
				DX12ResourceTrackerHookRuntime::RecordCreatedDescriptorHeap(hr, mDevice, pDescriptorHeapDesc, ppvHeap);
				if (SUCCEEDED(hr) && ppvHeap && *ppvHeap)
					DeviceWrapper::TryWrapDescriptorHeap(riid, ppvHeap);
				return hr;
			}

			UINT STDMETHODCALLTYPE GetDescriptorHandleIncrementSize(
				D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapType) override
			{
				return mDevice->GetDescriptorHandleIncrementSize(DescriptorHeapType);
			}

			HRESULT STDMETHODCALLTYPE CreateRootSignature(
				UINT nodeMask, const void *pBlobWithRootSignature, SIZE_T blobLengthInBytes,
				REFIID riid, void **ppvRootSignature) override
			{
				HRESULT hr = mDevice->CreateRootSignature(
					nodeMask, pBlobWithRootSignature, blobLengthInBytes, riid, ppvRootSignature);
				DX12ResourceTrackerHookRuntime::RecordCreatedRootSignature(
					hr, nodeMask, pBlobWithRootSignature, blobLengthInBytes, ppvRootSignature);
				if (SUCCEEDED(hr) && ppvRootSignature && *ppvRootSignature)
					DeviceWrapper::TryWrapRootSignature(riid, ppvRootSignature);
				return hr;
			}

			void STDMETHODCALLTYPE CreateConstantBufferView(
				const D3D12_CONSTANT_BUFFER_VIEW_DESC *pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override
			{
				mDevice->CreateConstantBufferView(pDesc, DestDescriptor);
				DX12ResourceTrackerHookRuntime::RecordCreatedConstantBufferView(pDesc, DestDescriptor);
			}

			void STDMETHODCALLTYPE CreateShaderResourceView(
				ID3D12Resource *pResource, const D3D12_SHADER_RESOURCE_VIEW_DESC *pDesc,
				D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override
			{
				ID3D12Resource *resource = DX12WrapperRuntimeUtils::UnwrapResource(pResource);
				mDevice->CreateShaderResourceView(resource, pDesc, DestDescriptor);
				DX12ResourceTrackerHookRuntime::RecordCreatedShaderResourceView(resource, pDesc, DestDescriptor);
			}

			void STDMETHODCALLTYPE CreateUnorderedAccessView(
				ID3D12Resource *pResource, ID3D12Resource *pCounterResource,
				const D3D12_UNORDERED_ACCESS_VIEW_DESC *pDesc,
				D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override
			{
				ID3D12Resource *resource = DX12WrapperRuntimeUtils::UnwrapResource(pResource);
				ID3D12Resource *counterResource = DX12WrapperRuntimeUtils::UnwrapResource(pCounterResource);
				D3D12_UNORDERED_ACCESS_VIEW_DESC adjustedDesc = {};
				const D3D12_UNORDERED_ACCESS_VIEW_DESC *descToUse =
					DX12ResourceTrackerHookRuntime::PrepareUavDesc(
						resource, pDesc, &adjustedDesc, "CreateUnorderedAccessView");
				mDevice->CreateUnorderedAccessView(resource, counterResource, descToUse, DestDescriptor);
				DX12ResourceTrackerHookRuntime::RecordCreatedUnorderedAccessView(
					resource, counterResource, descToUse, DestDescriptor);
			}

			void STDMETHODCALLTYPE CreateRenderTargetView(
				ID3D12Resource *pResource, const D3D12_RENDER_TARGET_VIEW_DESC *pDesc,
				D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override
			{
				ID3D12Resource *resource = DX12WrapperRuntimeUtils::UnwrapResource(pResource);
				mDevice->CreateRenderTargetView(resource, pDesc, DestDescriptor);
				DX12ResourceTrackerHookRuntime::RecordCreatedRenderTargetView(resource, pDesc, DestDescriptor);
			}

			void STDMETHODCALLTYPE CreateDepthStencilView(
				ID3D12Resource *pResource, const D3D12_DEPTH_STENCIL_VIEW_DESC *pDesc,
				D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override
			{
				ID3D12Resource *resource = DX12WrapperRuntimeUtils::UnwrapResource(pResource);
				mDevice->CreateDepthStencilView(resource, pDesc, DestDescriptor);
				DX12ResourceTrackerHookRuntime::RecordCreatedDepthStencilView(resource, pDesc, DestDescriptor);
			}

			void STDMETHODCALLTYPE CreateSampler(
				const D3D12_SAMPLER_DESC *pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override
			{
				mDevice->CreateSampler(pDesc, DestDescriptor);
				DX12ResourceTrackerHookRuntime::RecordCreatedSampler(pDesc, DestDescriptor);
			}

			void STDMETHODCALLTYPE CopyDescriptors(
				UINT NumDestDescriptorRanges, const D3D12_CPU_DESCRIPTOR_HANDLE *pDestDescriptorRangeStarts,
				const UINT *pDestDescriptorRangeSizes, UINT NumSrcDescriptorRanges,
				const D3D12_CPU_DESCRIPTOR_HANDLE *pSrcDescriptorRangeStarts,
				const UINT *pSrcDescriptorRangeSizes, D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapsType) override
			{
				mDevice->CopyDescriptors(
					NumDestDescriptorRanges, pDestDescriptorRangeStarts, pDestDescriptorRangeSizes,
					NumSrcDescriptorRanges, pSrcDescriptorRangeStarts, pSrcDescriptorRangeSizes,
					DescriptorHeapsType);
				DX12ResourceTrackerHookRuntime::RecordCopiedDescriptors(
					mDevice, NumDestDescriptorRanges, pDestDescriptorRangeStarts, pDestDescriptorRangeSizes,
					NumSrcDescriptorRanges, pSrcDescriptorRangeStarts, pSrcDescriptorRangeSizes,
					DescriptorHeapsType);
			}

			void STDMETHODCALLTYPE CopyDescriptorsSimple(
				UINT NumDescriptors, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptorRangeStart,
				D3D12_CPU_DESCRIPTOR_HANDLE SrcDescriptorRangeStart,
				D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapsType) override
			{
				mDevice->CopyDescriptorsSimple(
					NumDescriptors, DestDescriptorRangeStart, SrcDescriptorRangeStart, DescriptorHeapsType);
				DX12ResourceTrackerHookRuntime::RecordCopiedDescriptorsSimple(
					mDevice, NumDescriptors, DestDescriptorRangeStart, SrcDescriptorRangeStart, DescriptorHeapsType);
			}

			D3D12_RESOURCE_ALLOCATION_INFO STDMETHODCALLTYPE GetResourceAllocationInfo(
				UINT visibleMask, UINT numResourceDescs, const D3D12_RESOURCE_DESC *pResourceDescs) override
			{
				return mDevice->GetResourceAllocationInfo(visibleMask, numResourceDescs, pResourceDescs);
			}

			D3D12_HEAP_PROPERTIES STDMETHODCALLTYPE GetCustomHeapProperties(
				UINT nodeMask, D3D12_HEAP_TYPE heapType) override
			{
				return mDevice->GetCustomHeapProperties(nodeMask, heapType);
			}

			HRESULT STDMETHODCALLTYPE CreateCommittedResource(
				const D3D12_HEAP_PROPERTIES *pHeapProperties, D3D12_HEAP_FLAGS HeapFlags,
				const D3D12_RESOURCE_DESC *pDesc, D3D12_RESOURCE_STATES InitialResourceState,
				const D3D12_CLEAR_VALUE *pOptimizedClearValue, REFIID riidResource,
				void **ppvResource) override
			{
				if (DX12InternalReplayState::IsActive())
					return mDevice->CreateCommittedResource(
						pHeapProperties, HeapFlags, pDesc, InitialResourceState,
						pOptimizedClearValue, riidResource, ppvResource);
				D3D12_RESOURCE_DESC adjustedDesc = {};
				const D3D12_RESOURCE_DESC *descToUse =
					DX12ResourceTrackerHookRuntime::PrepareResourceDesc(
						pDesc, &adjustedDesc, "CreateCommittedResource");
				HRESULT hr = mDevice->CreateCommittedResource(
					pHeapProperties, HeapFlags, descToUse, InitialResourceState,
					pOptimizedClearValue, riidResource, ppvResource);
				DX12ResourceTrackerHookRuntime::RecordCreatedResource(
					hr, ppvResource, descToUse, pHeapProperties, nullptr, InitialResourceState);
				if (SUCCEEDED(hr) && ppvResource && *ppvResource)
					DeviceWrapper::TryWrapResource(riidResource, ppvResource);
				return hr;
			}

			HRESULT STDMETHODCALLTYPE CreateHeap(
				const D3D12_HEAP_DESC *pDesc, REFIID riid, void **ppvHeap) override
			{
				HRESULT hr = mDevice->CreateHeap(pDesc, riid, ppvHeap);
				if (SUCCEEDED(hr) && ppvHeap && *ppvHeap)
					DeviceWrapper::TryWrapHeap(riid, ppvHeap);
				return hr;
			}

			HRESULT STDMETHODCALLTYPE CreatePlacedResource(
				ID3D12Heap *pHeap, UINT64 HeapOffset, const D3D12_RESOURCE_DESC *pDesc,
				D3D12_RESOURCE_STATES InitialState, const D3D12_CLEAR_VALUE *pOptimizedClearValue,
				REFIID riid, void **ppvResource) override
			{
				ID3D12Heap *heap = DX12WrapperRuntimeUtils::UnwrapHeap(pHeap);
				HRESULT hr = mDevice->CreatePlacedResource(
					heap, HeapOffset, pDesc, InitialState, pOptimizedClearValue, riid, ppvResource);
				PlacedResourceInfo placedInfo =
					DX12ResourceTrackerHookRuntime::MakePlacedResourceInfo(heap, HeapOffset);
				DX12ResourceTrackerHookRuntime::RecordCreatedResource(
					hr, ppvResource, pDesc, nullptr, &placedInfo, InitialState);
				if (SUCCEEDED(hr) && ppvResource && *ppvResource)
					DeviceWrapper::TryWrapResource(riid, ppvResource);
				return hr;
			}

			HRESULT STDMETHODCALLTYPE CreateReservedResource(
				const D3D12_RESOURCE_DESC *pDesc, D3D12_RESOURCE_STATES InitialState,
				const D3D12_CLEAR_VALUE *pOptimizedClearValue, REFIID riid,
				void **ppvResource) override
			{
				HRESULT hr = mDevice->CreateReservedResource(
					pDesc, InitialState, pOptimizedClearValue, riid, ppvResource);
				DX12ResourceTrackerHookRuntime::RecordCreatedResource(
					hr, ppvResource, pDesc, nullptr, nullptr, InitialState);
				if (SUCCEEDED(hr) && ppvResource && *ppvResource)
					DeviceWrapper::TryWrapResource(riid, ppvResource);
				return hr;
			}

			HRESULT STDMETHODCALLTYPE CreateSharedHandle(
				ID3D12DeviceChild *pObject, const SECURITY_ATTRIBUTES *pAttributes,
				DWORD Access, LPCWSTR Name, HANDLE *pHandle) override
			{
				return mDevice->CreateSharedHandle(
					reinterpret_cast<ID3D12DeviceChild*>(DX12Wrapper::Unwrap(pObject)),
					pAttributes, Access, Name, pHandle);
			}

			HRESULT STDMETHODCALLTYPE OpenSharedHandle(
				HANDLE NTHandle, REFIID riid, void **ppvObj) override
			{
				HRESULT hr = mDevice->OpenSharedHandle(NTHandle, riid, ppvObj);
				if (SUCCEEDED(hr) && ppvObj && *ppvObj)
					DX12Wrapper::WrapD3D12DeviceResult(riid, ppvObj);
				return hr;
			}

			HRESULT STDMETHODCALLTYPE OpenSharedHandleByName(LPCWSTR Name, DWORD Access, HANDLE *pNTHandle) override
			{
				return mDevice->OpenSharedHandleByName(Name, Access, pNTHandle);
			}

			HRESULT STDMETHODCALLTYPE MakeResident(UINT NumObjects, ID3D12Pageable *const *ppObjects) override
			{
				std::vector<ID3D12Pageable*> unwrapped;
				ID3D12Pageable *const *activeObjects = ppObjects;
				if (ppObjects && NumObjects) {
					unwrapped.resize(NumObjects);
					for (UINT i = 0; i < NumObjects; ++i)
						unwrapped[i] = DX12WrapperRuntimeUtils::UnwrapPageable(ppObjects[i]);
					activeObjects = unwrapped.data();
				}
				return mDevice->MakeResident(NumObjects, activeObjects);
			}

			HRESULT STDMETHODCALLTYPE Evict(UINT NumObjects, ID3D12Pageable *const *ppObjects) override
			{
				std::vector<ID3D12Pageable*> unwrapped;
				ID3D12Pageable *const *activeObjects = ppObjects;
				if (ppObjects && NumObjects) {
					unwrapped.resize(NumObjects);
					for (UINT i = 0; i < NumObjects; ++i)
						unwrapped[i] = DX12WrapperRuntimeUtils::UnwrapPageable(ppObjects[i]);
					activeObjects = unwrapped.data();
				}
				return mDevice->Evict(NumObjects, activeObjects);
			}

			HRESULT STDMETHODCALLTYPE CreateFence(
				UINT64 InitialValue, D3D12_FENCE_FLAGS Flags, REFIID riid, void **ppFence) override
			{
				HRESULT hr = mDevice->CreateFence(InitialValue, Flags, riid, ppFence);
				if (SUCCEEDED(hr) && ppFence && *ppFence)
					DeviceWrapper::TryWrapFence(riid, ppFence);
				return hr;
			}

			HRESULT STDMETHODCALLTYPE GetDeviceRemovedReason() override
			{
				return mDevice->GetDeviceRemovedReason();
			}

			void STDMETHODCALLTYPE GetCopyableFootprints(
				const D3D12_RESOURCE_DESC *pResourceDesc, UINT FirstSubresource,
				UINT NumSubresources, UINT64 BaseOffset,
				D3D12_PLACED_SUBRESOURCE_FOOTPRINT *pLayouts, UINT *pNumRows,
				UINT64 *pRowSizeInBytes, UINT64 *pTotalBytes) override
			{
				mDevice->GetCopyableFootprints(
					pResourceDesc, FirstSubresource, NumSubresources, BaseOffset,
					pLayouts, pNumRows, pRowSizeInBytes, pTotalBytes);
			}

			HRESULT STDMETHODCALLTYPE CreateQueryHeap(
				const D3D12_QUERY_HEAP_DESC *pDesc, REFIID riid, void **ppvHeap) override
			{
				HRESULT hr = mDevice->CreateQueryHeap(pDesc, riid, ppvHeap);
				if (SUCCEEDED(hr) && ppvHeap && *ppvHeap)
					DeviceWrapper::TryWrapQueryHeap(riid, ppvHeap);
				return hr;
			}

			HRESULT STDMETHODCALLTYPE SetStablePowerState(BOOL Enable) override
			{
				return mDevice->SetStablePowerState(Enable);
			}

			HRESULT STDMETHODCALLTYPE CreateCommandSignature(
				const D3D12_COMMAND_SIGNATURE_DESC *pDesc, ID3D12RootSignature *pRootSignature,
				REFIID riid, void **ppvCommandSignature) override
			{
				HRESULT hr = mDevice->CreateCommandSignature(
					pDesc, DX12WrapperRuntimeUtils::UnwrapRootSignature(pRootSignature),
					riid, ppvCommandSignature);
				if (SUCCEEDED(hr) && ppvCommandSignature && *ppvCommandSignature)
					DeviceWrapper::TryWrapCommandSignature(riid, ppvCommandSignature);
				return hr;
			}

			void STDMETHODCALLTYPE GetResourceTiling(
				ID3D12Resource *pTiledResource, UINT *pNumTilesForEntireResource,
				D3D12_PACKED_MIP_INFO *pPackedMipDesc,
				D3D12_TILE_SHAPE *pStandardTileShapeForNonPackedMips,
				UINT *pNumSubresourceTilings, UINT FirstSubresourceTilingToGet,
				D3D12_SUBRESOURCE_TILING *pSubresourceTilingsForNonPackedMips) override
			{
				mDevice->GetResourceTiling(
					DX12WrapperRuntimeUtils::UnwrapResource(pTiledResource),
					pNumTilesForEntireResource, pPackedMipDesc, pStandardTileShapeForNonPackedMips,
					pNumSubresourceTilings, FirstSubresourceTilingToGet,
					pSubresourceTilingsForNonPackedMips);
			}

			LUID STDMETHODCALLTYPE GetAdapterLuid() override
			{
				return mDevice->GetAdapterLuid();
			}

			HRESULT STDMETHODCALLTYPE CreatePipelineLibrary(
				const void *pLibraryBlob, SIZE_T BlobLength, REFIID riid, void **ppPipelineLibrary) override
			{
				ID3D12Device1 *device1 = nullptr;
				HRESULT hr = QueryDevice1(&device1);
				if (FAILED(hr))
					return hr;
				hr = device1->CreatePipelineLibrary(pLibraryBlob, BlobLength, riid, ppPipelineLibrary);
				device1->Release();
				return hr;
			}

			HRESULT STDMETHODCALLTYPE SetEventOnMultipleFenceCompletion(
				ID3D12Fence *const *ppFences, const UINT64 *pFenceValues, UINT NumFences,
				D3D12_MULTIPLE_FENCE_WAIT_FLAGS Flags, HANDLE hEvent) override
			{
				ID3D12Device1 *device1 = nullptr;
				HRESULT hr = QueryDevice1(&device1);
				if (FAILED(hr))
					return hr;
				std::vector<ID3D12Fence*> unwrapped;
				ID3D12Fence *const *activeFences = ppFences;
				if (ppFences && NumFences) {
					unwrapped.resize(NumFences);
					for (UINT i = 0; i < NumFences; ++i)
						unwrapped[i] = DX12WrapperRuntimeUtils::UnwrapFence(ppFences[i]);
					activeFences = unwrapped.data();
				}
				hr = device1->SetEventOnMultipleFenceCompletion(
					activeFences, pFenceValues, NumFences, Flags, hEvent);
				device1->Release();
				return hr;
			}

			HRESULT STDMETHODCALLTYPE SetResidencyPriority(
				UINT NumObjects, ID3D12Pageable *const *ppObjects,
				const D3D12_RESIDENCY_PRIORITY *pPriorities) override
			{
				ID3D12Device1 *device1 = nullptr;
				HRESULT hr = QueryDevice1(&device1);
				if (FAILED(hr))
					return hr;
				std::vector<ID3D12Pageable*> unwrapped;
				ID3D12Pageable *const *activeObjects = ppObjects;
				if (ppObjects && NumObjects) {
					unwrapped.resize(NumObjects);
					for (UINT i = 0; i < NumObjects; ++i)
						unwrapped[i] = DX12WrapperRuntimeUtils::UnwrapPageable(ppObjects[i]);
					activeObjects = unwrapped.data();
				}
				hr = device1->SetResidencyPriority(NumObjects, activeObjects, pPriorities);
				device1->Release();
				return hr;
			}

			HRESULT STDMETHODCALLTYPE CreatePipelineState(
				const D3D12_PIPELINE_STATE_STREAM_DESC *pDesc, REFIID riid,
				void **ppPipelineState) override
			{
				ID3D12Device2 *device2 = nullptr;
				HRESULT hr = QueryDevice2(&device2);
				if (FAILED(hr))
					return hr;
				hr = device2->CreatePipelineState(pDesc, riid, ppPipelineState);
				if (SUCCEEDED(hr) && pDesc)
					DX12RecordPipelineStateStream(
						ppPipelineState ? static_cast<ID3D12PipelineState*>(*ppPipelineState) : nullptr,
						pDesc);
				if (SUCCEEDED(hr) && ppPipelineState && *ppPipelineState)
					DeviceWrapper::TryWrapPipelineState(riid, ppPipelineState);
				device2->Release();
				return hr;
			}

			HRESULT STDMETHODCALLTYPE OpenExistingHeapFromAddress(
				const void *pAddress, REFIID riid, void **ppvHeap) override
			{
				ID3D12Device3 *device3 = nullptr;
				HRESULT hr = QueryDevice3(&device3);
				if (FAILED(hr))
					return hr;
				hr = device3->OpenExistingHeapFromAddress(pAddress, riid, ppvHeap);
				if (SUCCEEDED(hr) && ppvHeap && *ppvHeap)
					DeviceWrapper::TryWrapHeap(riid, ppvHeap);
				device3->Release();
				return hr;
			}

			HRESULT STDMETHODCALLTYPE OpenExistingHeapFromFileMapping(
				HANDLE hFileMapping, REFIID riid, void **ppvHeap) override
			{
				ID3D12Device3 *device3 = nullptr;
				HRESULT hr = QueryDevice3(&device3);
				if (FAILED(hr))
					return hr;
				hr = device3->OpenExistingHeapFromFileMapping(hFileMapping, riid, ppvHeap);
				if (SUCCEEDED(hr) && ppvHeap && *ppvHeap)
					DeviceWrapper::TryWrapHeap(riid, ppvHeap);
				device3->Release();
				return hr;
			}

			HRESULT STDMETHODCALLTYPE EnqueueMakeResident(
				D3D12_RESIDENCY_FLAGS Flags, UINT NumObjects, ID3D12Pageable *const *ppObjects,
				ID3D12Fence *pFenceToSignal, UINT64 FenceValueToSignal) override
			{
				ID3D12Device3 *device3 = nullptr;
				HRESULT hr = QueryDevice3(&device3);
				if (FAILED(hr))
					return hr;
				std::vector<ID3D12Pageable*> unwrapped;
				ID3D12Pageable *const *activeObjects = ppObjects;
				if (ppObjects && NumObjects) {
					unwrapped.resize(NumObjects);
					for (UINT i = 0; i < NumObjects; ++i)
						unwrapped[i] = DX12WrapperRuntimeUtils::UnwrapPageable(ppObjects[i]);
					activeObjects = unwrapped.data();
				}
				hr = device3->EnqueueMakeResident(
					Flags, NumObjects, activeObjects,
					DX12WrapperRuntimeUtils::UnwrapFence(pFenceToSignal), FenceValueToSignal);
				device3->Release();
				return hr;
			}

			HRESULT STDMETHODCALLTYPE CreateCommandList1(
				UINT nodeMask, D3D12_COMMAND_LIST_TYPE type, D3D12_COMMAND_LIST_FLAGS flags,
				REFIID riid, void **ppCommandList) override
			{
				ID3D12Device4 *device4 = nullptr;
				HRESULT hr = QueryDevice4(&device4);
				if (FAILED(hr))
					return hr;
				hr = device4->CreateCommandList1(nodeMask, type, flags, riid, ppCommandList);
				DX12FrameAnalysis::LogJsonFunc("ID3D12Device4::CreateCommandList1",
					"\"hr\":\"0x%lx\",\"commandList\":\"%p\"", hr,
					ppCommandList ? *ppCommandList : nullptr);
				if (SUCCEEDED(hr) && ppCommandList && *ppCommandList) {
					ID3D12GraphicsCommandList *baseList = nullptr;
					if (SUCCEEDED(static_cast<IUnknown*>(*ppCommandList)->QueryInterface(IID_PPV_ARGS(&baseList)))) {
						DX12CommandListLifecycle::Register(baseList, nullptr);
						baseList->Release();
					}
					DeviceWrapper::TryWrapGraphicsCommandList(riid, ppCommandList);
				}
				device4->Release();
				return hr;
			}

			HRESULT STDMETHODCALLTYPE CreateProtectedResourceSession(
				const D3D12_PROTECTED_RESOURCE_SESSION_DESC *pDesc, REFIID riid,
				void **ppSession) override
			{
				ID3D12Device4 *device4 = nullptr;
				HRESULT hr = QueryDevice4(&device4);
				if (FAILED(hr))
					return hr;
				hr = device4->CreateProtectedResourceSession(pDesc, riid, ppSession);
				device4->Release();
				return hr;
			}

			HRESULT STDMETHODCALLTYPE CreateCommittedResource1(
				const D3D12_HEAP_PROPERTIES *pHeapProperties, D3D12_HEAP_FLAGS HeapFlags,
				const D3D12_RESOURCE_DESC *pDesc, D3D12_RESOURCE_STATES InitialResourceState,
				const D3D12_CLEAR_VALUE *pOptimizedClearValue,
				ID3D12ProtectedResourceSession *pProtectedSession, REFIID riidResource,
				void **ppvResource) override
			{
				ID3D12Device4 *device4 = nullptr;
				HRESULT hr = QueryDevice4(&device4);
				if (FAILED(hr))
					return hr;
				D3D12_RESOURCE_DESC adjustedDesc = {};
				const D3D12_RESOURCE_DESC *descToUse =
					DX12ResourceTrackerHookRuntime::PrepareResourceDesc(
						pDesc, &adjustedDesc, "CreateCommittedResource1");
				hr = device4->CreateCommittedResource1(
					pHeapProperties, HeapFlags, descToUse, InitialResourceState,
					pOptimizedClearValue,
					DX12WrapperRuntimeUtils::UnwrapProtectedResourceSession(pProtectedSession),
					riidResource, ppvResource);
				DX12ResourceTrackerHookRuntime::RecordCreatedResource(
					hr, ppvResource, descToUse, pHeapProperties, nullptr, InitialResourceState);
				if (SUCCEEDED(hr) && ppvResource && *ppvResource)
					DeviceWrapper::TryWrapResource(riidResource, ppvResource);
				device4->Release();
				return hr;
			}

			HRESULT STDMETHODCALLTYPE CreateHeap1(
				const D3D12_HEAP_DESC *pDesc, ID3D12ProtectedResourceSession *pProtectedSession,
				REFIID riid, void **ppvHeap) override
			{
				ID3D12Device4 *device4 = nullptr;
				HRESULT hr = QueryDevice4(&device4);
				if (FAILED(hr))
					return hr;
				hr = device4->CreateHeap1(
					pDesc, DX12WrapperRuntimeUtils::UnwrapProtectedResourceSession(pProtectedSession),
					riid, ppvHeap);
				if (SUCCEEDED(hr) && ppvHeap && *ppvHeap)
					DeviceWrapper::TryWrapHeap(riid, ppvHeap);
				device4->Release();
				return hr;
			}

			HRESULT STDMETHODCALLTYPE CreateReservedResource1(
				const D3D12_RESOURCE_DESC *pDesc, D3D12_RESOURCE_STATES InitialState,
				const D3D12_CLEAR_VALUE *pOptimizedClearValue,
				ID3D12ProtectedResourceSession *pProtectedSession, REFIID riid,
				void **ppvResource) override
			{
				ID3D12Device4 *device4 = nullptr;
				HRESULT hr = QueryDevice4(&device4);
				if (FAILED(hr))
					return hr;
				hr = device4->CreateReservedResource1(
					pDesc, InitialState, pOptimizedClearValue,
					DX12WrapperRuntimeUtils::UnwrapProtectedResourceSession(pProtectedSession),
					riid, ppvResource);
				DX12ResourceTrackerHookRuntime::RecordCreatedResource(
					hr, ppvResource, pDesc, nullptr, nullptr, InitialState);
				if (SUCCEEDED(hr) && ppvResource && *ppvResource)
					DeviceWrapper::TryWrapResource(riid, ppvResource);
				device4->Release();
				return hr;
			}

			D3D12_RESOURCE_ALLOCATION_INFO STDMETHODCALLTYPE GetResourceAllocationInfo1(
				UINT visibleMask, UINT numResourceDescs, const D3D12_RESOURCE_DESC *pResourceDescs,
				D3D12_RESOURCE_ALLOCATION_INFO1 *pResourceAllocationInfo1) override
			{
				ID3D12Device4 *device4 = nullptr;
				HRESULT hr = QueryDevice4(&device4);
				if (FAILED(hr))
					return D3D12_RESOURCE_ALLOCATION_INFO();
				D3D12_RESOURCE_ALLOCATION_INFO info = device4->GetResourceAllocationInfo1(
					visibleMask, numResourceDescs, pResourceDescs, pResourceAllocationInfo1);
				device4->Release();
				return info;
			}

			HRESULT STDMETHODCALLTYPE CreateLifetimeTracker(
				ID3D12LifetimeOwner *pOwner, REFIID riid, void **ppvTracker) override
			{
				ID3D12Device5 *device5 = nullptr;
				HRESULT hr = QueryDevice5(&device5);
				if (FAILED(hr))
					return hr;
				hr = device5->CreateLifetimeTracker(pOwner, riid, ppvTracker);
				device5->Release();
				return hr;
			}

			void STDMETHODCALLTYPE RemoveDevice() override
			{
				ID3D12Device5 *device5 = nullptr;
				if (SUCCEEDED(QueryDevice5(&device5))) {
					device5->RemoveDevice();
					device5->Release();
				}
			}

			HRESULT STDMETHODCALLTYPE EnumerateMetaCommands(
				UINT *pNumMetaCommands, D3D12_META_COMMAND_DESC *pDescs) override
			{
				ID3D12Device5 *device5 = nullptr;
				HRESULT hr = QueryDevice5(&device5);
				if (FAILED(hr))
					return hr;
				hr = device5->EnumerateMetaCommands(pNumMetaCommands, pDescs);
				device5->Release();
				return hr;
			}

			HRESULT STDMETHODCALLTYPE EnumerateMetaCommandParameters(
				REFGUID CommandId, D3D12_META_COMMAND_PARAMETER_STAGE Stage,
				UINT *pTotalStructureSizeInBytes, UINT *pParameterCount,
				D3D12_META_COMMAND_PARAMETER_DESC *pParameterDescs) override
			{
				ID3D12Device5 *device5 = nullptr;
				HRESULT hr = QueryDevice5(&device5);
				if (FAILED(hr))
					return hr;
				hr = device5->EnumerateMetaCommandParameters(
					CommandId, Stage, pTotalStructureSizeInBytes, pParameterCount, pParameterDescs);
				device5->Release();
				return hr;
			}

			HRESULT STDMETHODCALLTYPE CreateMetaCommand(
				REFGUID CommandId, UINT NodeMask, const void *pCreationParametersData,
				SIZE_T CreationParametersDataSizeInBytes, REFIID riid,
				void **ppMetaCommand) override
			{
				ID3D12Device5 *device5 = nullptr;
				HRESULT hr = QueryDevice5(&device5);
				if (FAILED(hr))
					return hr;
				hr = device5->CreateMetaCommand(
					CommandId, NodeMask, pCreationParametersData,
					CreationParametersDataSizeInBytes, riid, ppMetaCommand);
				device5->Release();
				return hr;
			}

			HRESULT STDMETHODCALLTYPE CreateStateObject(
				const D3D12_STATE_OBJECT_DESC *pDesc, REFIID riid, void **ppStateObject) override
			{
				ID3D12Device5 *device5 = nullptr;
				HRESULT hr = QueryDevice5(&device5);
				if (FAILED(hr))
					return hr;
				hr = device5->CreateStateObject(pDesc, riid, ppStateObject);
				device5->Release();
				return hr;
			}

			void STDMETHODCALLTYPE GetRaytracingAccelerationStructurePrebuildInfo(
				const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS *pDesc,
				D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO *pInfo) override
			{
				ID3D12Device5 *device5 = nullptr;
				if (SUCCEEDED(QueryDevice5(&device5))) {
					device5->GetRaytracingAccelerationStructurePrebuildInfo(pDesc, pInfo);
					device5->Release();
				}
			}

			D3D12_DRIVER_MATCHING_IDENTIFIER_STATUS STDMETHODCALLTYPE CheckDriverMatchingIdentifier(
				D3D12_SERIALIZED_DATA_TYPE SerializedDataType,
				const D3D12_SERIALIZED_DATA_DRIVER_MATCHING_IDENTIFIER *pIdentifierToCheck) override
			{
				ID3D12Device5 *device5 = nullptr;
				HRESULT hr = QueryDevice5(&device5);
				if (FAILED(hr))
					return D3D12_DRIVER_MATCHING_IDENTIFIER_UNRECOGNIZED;
				D3D12_DRIVER_MATCHING_IDENTIFIER_STATUS status =
					device5->CheckDriverMatchingIdentifier(SerializedDataType, pIdentifierToCheck);
				device5->Release();
				return status;
			}

			HRESULT STDMETHODCALLTYPE SetBackgroundProcessingMode(
				D3D12_BACKGROUND_PROCESSING_MODE Mode, D3D12_MEASUREMENTS_ACTION MeasurementsAction,
				HANDLE hEventToSignalUponCompletion, BOOL *pbFurtherMeasurementsDesired) override
			{
				ID3D12Device6 *device6 = nullptr;
				HRESULT hr = QueryDevice6(&device6);
				if (FAILED(hr))
					return hr;
				hr = device6->SetBackgroundProcessingMode(
					Mode, MeasurementsAction, hEventToSignalUponCompletion,
					pbFurtherMeasurementsDesired);
				device6->Release();
				return hr;
			}

			HRESULT STDMETHODCALLTYPE AddToStateObject(
				const D3D12_STATE_OBJECT_DESC *pAddition, ID3D12StateObject *pStateObjectToGrowFrom,
				REFIID riid, void **ppNewStateObject) override
			{
				ID3D12Device7 *device7 = nullptr;
				HRESULT hr = QueryDevice7(&device7);
				if (FAILED(hr))
					return hr;
				hr = device7->AddToStateObject(pAddition, pStateObjectToGrowFrom, riid, ppNewStateObject);
				device7->Release();
				return hr;
			}

			HRESULT STDMETHODCALLTYPE CreateProtectedResourceSession1(
				const D3D12_PROTECTED_RESOURCE_SESSION_DESC1 *pDesc, REFIID riid,
				void **ppSession) override
			{
				ID3D12Device7 *device7 = nullptr;
				HRESULT hr = QueryDevice7(&device7);
				if (FAILED(hr))
					return hr;
				hr = device7->CreateProtectedResourceSession1(pDesc, riid, ppSession);
				device7->Release();
				return hr;
			}

			D3D12_RESOURCE_ALLOCATION_INFO STDMETHODCALLTYPE GetResourceAllocationInfo2(
				UINT visibleMask, UINT numResourceDescs, const D3D12_RESOURCE_DESC1 *pResourceDescs,
				D3D12_RESOURCE_ALLOCATION_INFO1 *pResourceAllocationInfo1) override
			{
				ID3D12Device8 *device8 = nullptr;
				HRESULT hr = QueryDevice8(&device8);
				if (FAILED(hr))
					return D3D12_RESOURCE_ALLOCATION_INFO();
				D3D12_RESOURCE_ALLOCATION_INFO info = device8->GetResourceAllocationInfo2(
					visibleMask, numResourceDescs, pResourceDescs, pResourceAllocationInfo1);
				device8->Release();
				return info;
			}

			HRESULT STDMETHODCALLTYPE CreateCommittedResource2(
				const D3D12_HEAP_PROPERTIES *pHeapProperties, D3D12_HEAP_FLAGS HeapFlags,
				const D3D12_RESOURCE_DESC1 *pDesc, D3D12_RESOURCE_STATES InitialResourceState,
				const D3D12_CLEAR_VALUE *pOptimizedClearValue,
				ID3D12ProtectedResourceSession *pProtectedSession, REFIID riidResource,
				void **ppvResource) override
			{
				ID3D12Device8 *device8 = nullptr;
				HRESULT hr = QueryDevice8(&device8);
				if (FAILED(hr))
					return hr;
				D3D12_RESOURCE_DESC1 adjustedDesc = {};
				const D3D12_RESOURCE_DESC1 *descToUse =
					DX12ResourceTrackerHookRuntime::PrepareResourceDesc1(
						pDesc, &adjustedDesc, "CreateCommittedResource2");
				hr = device8->CreateCommittedResource2(
					pHeapProperties, HeapFlags, descToUse, InitialResourceState,
					pOptimizedClearValue,
					DX12WrapperRuntimeUtils::UnwrapProtectedResourceSession(pProtectedSession),
					riidResource, ppvResource);
				DX12ResourceTrackerHookRuntime::RecordCreatedResource1(
					hr, ppvResource, descToUse, pHeapProperties, nullptr, InitialResourceState);
				if (SUCCEEDED(hr) && ppvResource && *ppvResource)
					DeviceWrapper::TryWrapResource(riidResource, ppvResource);
				device8->Release();
				return hr;
			}

			HRESULT STDMETHODCALLTYPE CreatePlacedResource1(
				ID3D12Heap *pHeap, UINT64 HeapOffset, const D3D12_RESOURCE_DESC1 *pDesc,
				D3D12_RESOURCE_STATES InitialState, const D3D12_CLEAR_VALUE *pOptimizedClearValue,
				REFIID riid, void **ppvResource) override
			{
				ID3D12Device8 *device8 = nullptr;
				HRESULT hr = QueryDevice8(&device8);
				if (FAILED(hr))
					return hr;
				ID3D12Heap *heap = DX12WrapperRuntimeUtils::UnwrapHeap(pHeap);
				hr = device8->CreatePlacedResource1(
					heap, HeapOffset, pDesc, InitialState, pOptimizedClearValue, riid, ppvResource);
				PlacedResourceInfo placedInfo =
					DX12ResourceTrackerHookRuntime::MakePlacedResourceInfo(heap, HeapOffset);
				DX12ResourceTrackerHookRuntime::RecordCreatedResource1(
					hr, ppvResource, pDesc, nullptr, &placedInfo, InitialState);
				if (SUCCEEDED(hr) && ppvResource && *ppvResource)
					DeviceWrapper::TryWrapResource(riid, ppvResource);
				device8->Release();
				return hr;
			}

			void STDMETHODCALLTYPE CreateSamplerFeedbackUnorderedAccessView(
				ID3D12Resource *pTargetedResource, ID3D12Resource *pFeedbackResource,
				D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override
			{
				ID3D12Device8 *device8 = nullptr;
				if (SUCCEEDED(QueryDevice8(&device8))) {
					device8->CreateSamplerFeedbackUnorderedAccessView(
						DX12WrapperRuntimeUtils::UnwrapResource(pTargetedResource),
						DX12WrapperRuntimeUtils::UnwrapResource(pFeedbackResource),
						DestDescriptor);
					device8->Release();
				}
			}

			void STDMETHODCALLTYPE GetCopyableFootprints1(
				const D3D12_RESOURCE_DESC1 *pResourceDesc, UINT FirstSubresource,
				UINT NumSubresources, UINT64 BaseOffset,
				D3D12_PLACED_SUBRESOURCE_FOOTPRINT *pLayouts, UINT *pNumRows,
				UINT64 *pRowSizeInBytes, UINT64 *pTotalBytes) override
			{
				ID3D12Device8 *device8 = nullptr;
				if (SUCCEEDED(QueryDevice8(&device8))) {
					device8->GetCopyableFootprints1(
						pResourceDesc, FirstSubresource, NumSubresources, BaseOffset,
						pLayouts, pNumRows, pRowSizeInBytes, pTotalBytes);
					device8->Release();
				}
			}

			HRESULT STDMETHODCALLTYPE CreateShaderCacheSession(
				const D3D12_SHADER_CACHE_SESSION_DESC *pDesc, REFIID riid,
				void **ppvSession) override
			{
				ID3D12Device9 *device9 = nullptr;
				HRESULT hr = QueryDevice9(&device9);
				if (FAILED(hr))
					return hr;
				hr = device9->CreateShaderCacheSession(pDesc, riid, ppvSession);
				device9->Release();
				return hr;
			}

			HRESULT STDMETHODCALLTYPE ShaderCacheControl(
				D3D12_SHADER_CACHE_KIND_FLAGS Kinds, D3D12_SHADER_CACHE_CONTROL_FLAGS Control) override
			{
				ID3D12Device9 *device9 = nullptr;
				HRESULT hr = QueryDevice9(&device9);
				if (FAILED(hr))
					return hr;
				hr = device9->ShaderCacheControl(Kinds, Control);
				device9->Release();
				return hr;
			}

			HRESULT STDMETHODCALLTYPE CreateCommandQueue1(
				const D3D12_COMMAND_QUEUE_DESC *pDesc, REFIID CreatorID, REFIID riid,
				void **ppCommandQueue) override
			{
				ID3D12Device9 *device9 = nullptr;
				HRESULT hr = QueryDevice9(&device9);
				if (FAILED(hr))
					return hr;
				hr = device9->CreateCommandQueue1(pDesc, CreatorID, riid, ppCommandQueue);
				if (SUCCEEDED(hr) && ppCommandQueue && *ppCommandQueue)
					DeviceWrapper::TryWrapCommandQueue(riid, ppCommandQueue);
				device9->Release();
				return hr;
			}

			HRESULT STDMETHODCALLTYPE CreateCommittedResource3(
				const D3D12_HEAP_PROPERTIES *pHeapProperties, D3D12_HEAP_FLAGS HeapFlags,
				const D3D12_RESOURCE_DESC1 *pDesc, D3D12_BARRIER_LAYOUT InitialLayout,
				const D3D12_CLEAR_VALUE *pOptimizedClearValue,
				ID3D12ProtectedResourceSession *pProtectedSession, UINT32 NumCastableFormats,
				const DXGI_FORMAT *pCastableFormats, REFIID riidResource,
				void **ppvResource) override
			{
				ID3D12Device10 *device10 = nullptr;
				HRESULT hr = QueryDevice10(&device10);
				if (FAILED(hr))
					return hr;
				D3D12_RESOURCE_DESC1 adjustedDesc = {};
				const D3D12_RESOURCE_DESC1 *descToUse =
					DX12ResourceTrackerHookRuntime::PrepareResourceDesc1(
						pDesc, &adjustedDesc, "CreateCommittedResource3");
				hr = device10->CreateCommittedResource3(
					pHeapProperties, HeapFlags, descToUse, InitialLayout, pOptimizedClearValue,
					DX12WrapperRuntimeUtils::UnwrapProtectedResourceSession(pProtectedSession),
					NumCastableFormats, pCastableFormats, riidResource, ppvResource);
				DX12ResourceTrackerHookRuntime::RecordCreatedResource1WithLayout(
					hr, ppvResource, descToUse, pHeapProperties, nullptr, InitialLayout);
				if (SUCCEEDED(hr) && ppvResource && *ppvResource)
					DeviceWrapper::TryWrapResource(riidResource, ppvResource);
				device10->Release();
				return hr;
			}

			HRESULT STDMETHODCALLTYPE CreatePlacedResource2(
				ID3D12Heap *pHeap, UINT64 HeapOffset, const D3D12_RESOURCE_DESC1 *pDesc,
				D3D12_BARRIER_LAYOUT InitialLayout, const D3D12_CLEAR_VALUE *pOptimizedClearValue,
				UINT32 NumCastableFormats, const DXGI_FORMAT *pCastableFormats,
				REFIID riid, void **ppvResource) override
			{
				ID3D12Device10 *device10 = nullptr;
				HRESULT hr = QueryDevice10(&device10);
				if (FAILED(hr))
					return hr;
				ID3D12Heap *heap = DX12WrapperRuntimeUtils::UnwrapHeap(pHeap);
				hr = device10->CreatePlacedResource2(
					heap, HeapOffset, pDesc, InitialLayout, pOptimizedClearValue,
					NumCastableFormats, pCastableFormats, riid, ppvResource);
				PlacedResourceInfo placedInfo =
					DX12ResourceTrackerHookRuntime::MakePlacedResourceInfo(heap, HeapOffset);
				DX12ResourceTrackerHookRuntime::RecordCreatedResource1WithLayout(
					hr, ppvResource, pDesc, nullptr, &placedInfo, InitialLayout);
				if (SUCCEEDED(hr) && ppvResource && *ppvResource)
					DeviceWrapper::TryWrapResource(riid, ppvResource);
				device10->Release();
				return hr;
			}

			HRESULT STDMETHODCALLTYPE CreateReservedResource2(
				const D3D12_RESOURCE_DESC *pDesc, D3D12_BARRIER_LAYOUT InitialLayout,
				const D3D12_CLEAR_VALUE *pOptimizedClearValue,
				ID3D12ProtectedResourceSession *pProtectedSession, UINT32 NumCastableFormats,
				const DXGI_FORMAT *pCastableFormats, REFIID riid,
				void **ppvResource) override
			{
				ID3D12Device10 *device10 = nullptr;
				HRESULT hr = QueryDevice10(&device10);
				if (FAILED(hr))
					return hr;
				hr = device10->CreateReservedResource2(
					pDesc, InitialLayout, pOptimizedClearValue,
					DX12WrapperRuntimeUtils::UnwrapProtectedResourceSession(pProtectedSession),
					NumCastableFormats, pCastableFormats, riid, ppvResource);
				DX12ResourceTrackerHookRuntime::RecordCreatedResourceWithLayout(
					hr, ppvResource, pDesc, nullptr, nullptr, InitialLayout);
				if (SUCCEEDED(hr) && ppvResource && *ppvResource)
					DeviceWrapper::TryWrapResource(riid, ppvResource);
				device10->Release();
				return hr;
			}

			void STDMETHODCALLTYPE CreateSampler2(
				const D3D12_SAMPLER_DESC2 *pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override
			{
				ID3D12Device11 *device11 = nullptr;
				if (SUCCEEDED(QueryDevice11(&device11))) {
					device11->CreateSampler2(pDesc, DestDescriptor);
					device11->Release();
				}
			}

			D3D12_RESOURCE_ALLOCATION_INFO STDMETHODCALLTYPE GetResourceAllocationInfo3(
				UINT visibleMask, UINT numResourceDescs, const D3D12_RESOURCE_DESC1 *pResourceDescs,
				const UINT32 *pNumCastableFormats, const DXGI_FORMAT *const *ppCastableFormats,
				D3D12_RESOURCE_ALLOCATION_INFO1 *pResourceAllocationInfo1) override
			{
				ID3D12Device12 *device12 = nullptr;
				HRESULT hr = QueryDevice12(&device12);
				if (FAILED(hr))
					return D3D12_RESOURCE_ALLOCATION_INFO();
				D3D12_RESOURCE_ALLOCATION_INFO info = device12->GetResourceAllocationInfo3(
					visibleMask, numResourceDescs, pResourceDescs, pNumCastableFormats,
					ppCastableFormats, pResourceAllocationInfo1);
				device12->Release();
				return info;
			}

			HRESULT STDMETHODCALLTYPE OpenExistingHeapFromAddress1(
				const void *pAddress, SIZE_T size, REFIID riid, void **ppvHeap) override
			{
				ID3D12Device13 *device13 = nullptr;
				HRESULT hr = QueryDevice13(&device13);
				if (FAILED(hr))
					return hr;
				hr = device13->OpenExistingHeapFromAddress1(pAddress, size, riid, ppvHeap);
				if (SUCCEEDED(hr) && ppvHeap && *ppvHeap)
					DeviceWrapper::TryWrapHeap(riid, ppvHeap);
				device13->Release();
				return hr;
			}

			HRESULT STDMETHODCALLTYPE CreateRootSignatureFromSubobjectInLibrary(
				UINT nodeMask, const void *pLibraryBlob, SIZE_T blobLengthInBytes,
				LPCWSTR subobjectName, REFIID riid, void **ppvRootSignature) override
			{
				ID3D12Device14 *device14 = nullptr;
				HRESULT hr = QueryDevice14(&device14);
				if (FAILED(hr))
					return hr;
				hr = device14->CreateRootSignatureFromSubobjectInLibrary(
					nodeMask, pLibraryBlob, blobLengthInBytes, subobjectName, riid, ppvRootSignature);
				DX12ResourceTrackerHookRuntime::RecordCreatedRootSignature(
					hr, nodeMask, pLibraryBlob, blobLengthInBytes, ppvRootSignature);
				if (SUCCEEDED(hr) && ppvRootSignature && *ppvRootSignature)
					DeviceWrapper::TryWrapRootSignature(riid, ppvRootSignature);
				device14->Release();
				return hr;
			}

		public:
			std::atomic<long> mRefCount{1};
			ID3D12Device *mDevice = nullptr;
		};



		template <typename TWrapper, typename TObject, typename TInterface>
		bool WrapExisting(TObject *object, TInterface **outInterface)
		{
			if (!object || !outInterface)
				return false;

			WrapperBase *existing = WrapperBase::FindByIdentity(object);
			if (existing) {
				TWrapper *wrapper = dynamic_cast<TWrapper*>(existing);
				if (!wrapper)
					return false;
				*outInterface = static_cast<TInterface*>(wrapper);
				reinterpret_cast<IUnknown*>(*outInterface)->AddRef();
				return true;
			}

			*outInterface = static_cast<TInterface*>(new TWrapper(object));
			return true;
		}
	}

	bool WrapD3D12DeviceResult(REFIID riid, void **device)
	{
		if (!device || !*device)
			return false;

		if (IsEqualIID(riid, __uuidof(IUnknown))) {
			if (DeviceWrapper::TryWrapCommandAllocator(__uuidof(ID3D12CommandAllocator), device))
				return true;
			if (DeviceWrapper::TryWrapFence(__uuidof(ID3D12Fence), device))
				return true;
			if (DeviceWrapper::TryWrapDescriptorHeap(__uuidof(ID3D12DescriptorHeap), device))
				return true;
			if (DeviceWrapper::TryWrapResource(__uuidof(ID3D12Resource), device))
				return true;
			if (DeviceWrapper::TryWrapHeap(__uuidof(ID3D12Heap), device))
				return true;
			if (DeviceWrapper::TryWrapPipelineState(__uuidof(ID3D12PipelineState), device))
				return true;
			if (DeviceWrapper::TryWrapRootSignature(__uuidof(ID3D12RootSignature), device))
				return true;
			if (DeviceWrapper::TryWrapQueryHeap(__uuidof(ID3D12QueryHeap), device))
				return true;
			if (DeviceWrapper::TryWrapCommandSignature(__uuidof(ID3D12CommandSignature), device))
				return true;
			if (DeviceWrapper::TryWrapCommandQueue(__uuidof(ID3D12CommandQueue), device))
				return true;
			if (DeviceWrapper::TryWrapGraphicsCommandList(__uuidof(ID3D12GraphicsCommandList), device))
				return true;
		}

		if (DeviceWrapper::TryWrapCommandAllocator(riid, device))
			return true;

		if (DeviceWrapper::TryWrapFence(riid, device))
			return true;

		if (DeviceWrapper::TryWrapDescriptorHeap(riid, device))
			return true;

		if (DeviceWrapper::TryWrapResource(riid, device))
			return true;

		if (DeviceWrapper::TryWrapHeap(riid, device))
			return true;

		if (DeviceWrapper::TryWrapPipelineState(riid, device))
			return true;

		if (DeviceWrapper::TryWrapRootSignature(riid, device))
			return true;

		if (DeviceWrapper::TryWrapQueryHeap(riid, device))
			return true;

		if (DeviceWrapper::TryWrapCommandSignature(riid, device))
			return true;

		if (DeviceWrapper::TryWrapCommandQueue(riid, device))
			return true;

		if (DeviceWrapper::TryWrapGraphicsCommandList(riid, device))
			return true;

		if (!IsD3D12DeviceIid(riid) && !IsEqualIID(riid, __uuidof(IUnknown)))
			return false;

		IUnknown *originalObject = static_cast<IUnknown*>(*device);
		ID3D12Device *baseDevice = nullptr;
		if (FAILED(originalObject->QueryInterface(IID_PPV_ARGS(&baseDevice))))
			return false;

		WrapperBase *existing = WrapperBase::FindByIdentity(baseDevice);
		if (existing) {
			D3D12DeviceWrapper *wrapper = dynamic_cast<D3D12DeviceWrapper*>(existing);
			if (!wrapper) {
				baseDevice->Release();
				return false;
			}
			void *wrapperInterface = wrapper->GetDeviceWrapperInterface(riid);
			if (!wrapperInterface) {
				baseDevice->Release();
				return false;
			}
			*device = wrapperInterface;
			reinterpret_cast<IUnknown*>(*device)->AddRef();
			baseDevice->Release();
			originalObject->Release();
			return true;
		}

		D3D12DeviceWrapper *wrapper = new D3D12DeviceWrapper(baseDevice);
		void *wrapperInterface = wrapper->GetDeviceWrapperInterface(riid);
		if (!wrapperInterface) {
			baseDevice->Release();
			return false;
		}
		*device = wrapperInterface;
		baseDevice->Release();
		originalObject->Release();
		DX12LogManager::JsonFunc("DX12Wrapper",
			"\"kind\":\"device\",\"stage\":\"wrapped\",\"wrapper\":\"%p\",\"object\":\"%p\"",
			*device, originalObject);
		return true;
	}

	bool WrapD3D12CommandAllocatorResult(REFIID riid, void **allocator)
	{
		return DeviceWrapper::TryWrapCommandAllocator(riid, allocator);
	}

	bool WrapD3D12FenceResult(REFIID riid, void **fence)
	{
		return DeviceWrapper::TryWrapFence(riid, fence);
	}

	bool WrapD3D12DescriptorHeapResult(REFIID riid, void **heap)
	{
		return DeviceWrapper::TryWrapDescriptorHeap(riid, heap);
	}

	bool WrapD3D12ResourceResult(REFIID riid, void **resource)
	{
		return DeviceWrapper::TryWrapResource(riid, resource);
	}

	bool WrapD3D12HeapResult(REFIID riid, void **heap)
	{
		return DeviceWrapper::TryWrapHeap(riid, heap);
	}

	bool WrapD3D12PipelineStateResult(REFIID riid, void **pipelineState)
	{
		return DeviceWrapper::TryWrapPipelineState(riid, pipelineState);
	}

	bool WrapD3D12RootSignatureResult(REFIID riid, void **rootSignature)
	{
		return DeviceWrapper::TryWrapRootSignature(riid, rootSignature);
	}

	bool WrapD3D12QueryHeapResult(REFIID riid, void **queryHeap)
	{
		return DeviceWrapper::TryWrapQueryHeap(riid, queryHeap);
	}

	bool WrapD3D12CommandSignatureResult(REFIID riid, void **commandSignature)
	{
		return DeviceWrapper::TryWrapCommandSignature(riid, commandSignature);
	}

	bool WrapD3D12DeviceFactoryResult(REFIID riid, void **object)
	{
		if (!object || !*object || (!IsEqualIID(riid, __uuidof(ID3D12DeviceFactory)) && !IsEqualIID(riid, __uuidof(IUnknown))))
			return false;

		ID3D12DeviceFactory *factory = nullptr;
		IUnknown *originalObject = static_cast<IUnknown*>(*object);
		if (FAILED(static_cast<IUnknown*>(*object)->QueryInterface(IID_PPV_ARGS(&factory))))
			return false;

		ID3D12DeviceFactory *wrappedFactory = nullptr;
		bool ok = WrapExisting<DeviceFactoryWrapper, ID3D12DeviceFactory, ID3D12DeviceFactory>(factory, &wrappedFactory);
		factory->Release();
		if (ok) {
			*object = wrappedFactory;
			originalObject->Release();
		}
		return ok;
	}

	bool WrapDXGIFactoryResult(REFIID riid, void **factory)
	{
		if (!factory || !*factory || (!IsDxgiFactoryIid(riid) && !IsEqualIID(riid, __uuidof(IUnknown))))
			return false;

		IDXGIFactory *baseFactory = nullptr;
		IUnknown *originalObject = static_cast<IUnknown*>(*factory);
		if (FAILED(static_cast<IUnknown*>(*factory)->QueryInterface(IID_PPV_ARGS(&baseFactory))))
			return false;

		WrapperBase *existing = WrapperBase::FindByIdentity(baseFactory);
		if (existing) {
			FactoryWrapper *wrapper = dynamic_cast<FactoryWrapper*>(existing);
			if (!wrapper) {
				baseFactory->Release();
				return false;
			}
			void *wrapperInterface = wrapper->GetFactoryWrapperInterface(riid);
			if (!wrapperInterface) {
				baseFactory->Release();
				return false;
			}
			*factory = wrapperInterface;
			reinterpret_cast<IUnknown*>(*factory)->AddRef();
			baseFactory->Release();
			originalObject->Release();
			return true;
		}

		FactoryWrapper *wrapper = new FactoryWrapper(baseFactory);
		void *wrapperInterface = wrapper->GetFactoryWrapperInterface(riid);
		if (!wrapperInterface) {
			baseFactory->Release();
			return false;
		}
		*factory = wrapperInterface;
		baseFactory->Release();
		originalObject->Release();
		return true;
	}

	bool WrapDXGISwapChainResult(REFIID riid, void **swapChain)
	{
		if (!swapChain || !*swapChain || (!IsDxgiSwapChainIid(riid) && !IsEqualIID(riid, __uuidof(IUnknown))))
			return false;

		IDXGISwapChain *baseSwapChain = nullptr;
		IUnknown *originalObject = static_cast<IUnknown*>(*swapChain);
		if (FAILED(static_cast<IUnknown*>(*swapChain)->QueryInterface(IID_PPV_ARGS(&baseSwapChain))))
			return false;

		WrapperBase *existing = WrapperBase::FindByIdentity(baseSwapChain);
		if (existing) {
			SwapChainWrapper *wrapper = dynamic_cast<SwapChainWrapper*>(existing);
			if (!wrapper) {
				baseSwapChain->Release();
				return false;
			}
			void *wrapperInterface = wrapper->GetSwapChainWrapperInterface(riid);
			if (!wrapperInterface) {
				baseSwapChain->Release();
				return false;
			}
			*swapChain = wrapperInterface;
			reinterpret_cast<IUnknown*>(*swapChain)->AddRef();
			baseSwapChain->Release();
			originalObject->Release();
			return true;
		}

		SwapChainWrapper *wrapper = new SwapChainWrapper(baseSwapChain);
		void *wrapperInterface = wrapper->GetSwapChainWrapperInterface(riid);
		if (!wrapperInterface) {
			baseSwapChain->Release();
			return false;
		}
		*swapChain = wrapperInterface;
		baseSwapChain->Release();
		originalObject->Release();
		return true;
	}

	IUnknown *Unwrap(IUnknown *object)
	{
		if (!object)
			return nullptr;

		WrapperBase *wrapper = WrapperBase::FindByWrapperPointer(object);
		if (wrapper)
			return wrapper->GetObject();
		return object;
	}

void Shutdown()
{
	DX12LogManager::JsonFunc("DX12Wrapper", "\"stage\":\"shutdown\"");
}
}
