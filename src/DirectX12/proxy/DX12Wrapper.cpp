#include "DX12Wrapper.hpp"

#include <atomic>
#include <unordered_map>
#include <vector>

#include <d3d12sdklayers.h>

#include "DX12CommandListHookRegistry.hpp"
#include "DX12CommandListHookSlotTypes.hpp"
#include "DX12CommandListHookState.hpp"
#include "DX12DeviceHookTypes.hpp"
#include "DX12HotPathState.hpp"
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

		bool IsD3D12LifetimeTrackerWrapperIid(REFIID riid)
		{
			return IsD3D12DeviceChildIid(riid) || IsEqualIID(riid, __uuidof(ID3D12LifetimeTracker));
		}

		bool IsD3D12ProtectedResourceSessionWrapperIid(REFIID riid)
		{
			return
				IsD3D12DeviceChildIid(riid) ||
				IsEqualIID(riid, __uuidof(ID3D12ProtectedSession)) ||
				IsEqualIID(riid, __uuidof(ID3D12ProtectedResourceSession)) ||
				IsEqualIID(riid, __uuidof(ID3D12ProtectedResourceSession1));
		}

		bool IsD3D12MetaCommandWrapperIid(REFIID riid)
		{
			return IsD3D12PageableIid(riid) || IsEqualIID(riid, __uuidof(ID3D12MetaCommand));
		}

		bool IsD3D12StateObjectWrapperIid(REFIID riid)
		{
			return
				IsD3D12PageableIid(riid) ||
				IsEqualIID(riid, __uuidof(ID3D12StateObject)) ||
				IsEqualIID(riid, __uuidof(ID3D12StateObjectProperties)) ||
				IsEqualIID(riid, __uuidof(ID3D12StateObjectProperties1));
		}

		bool IsD3D12PipelineLibraryWrapperIid(REFIID riid)
		{
			return
				IsD3D12DeviceChildIid(riid) ||
				IsEqualIID(riid, __uuidof(ID3D12PipelineLibrary)) ||
				IsEqualIID(riid, __uuidof(ID3D12PipelineLibrary1));
		}

		bool IsD3D12ShaderCacheSessionWrapperIid(REFIID riid)
		{
			return IsD3D12DeviceChildIid(riid) || IsEqualIID(riid, __uuidof(ID3D12ShaderCacheSession));
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

		bool IsD3D12LifetimeTrackerIid(REFIID riid)
		{
			return IsEqualIID(riid, __uuidof(ID3D12LifetimeTracker));
		}

		bool IsD3D12ProtectedResourceSessionIid(REFIID riid)
		{
			return
				IsEqualIID(riid, __uuidof(ID3D12ProtectedSession)) ||
				IsEqualIID(riid, __uuidof(ID3D12ProtectedResourceSession)) ||
				IsEqualIID(riid, __uuidof(ID3D12ProtectedResourceSession1));
		}

		bool IsD3D12MetaCommandIid(REFIID riid)
		{
			return IsEqualIID(riid, __uuidof(ID3D12MetaCommand));
		}

		bool IsD3D12StateObjectIid(REFIID riid)
		{
			return
				IsEqualIID(riid, __uuidof(ID3D12StateObject)) ||
				IsEqualIID(riid, __uuidof(ID3D12StateObjectProperties)) ||
				IsEqualIID(riid, __uuidof(ID3D12StateObjectProperties1));
		}

		bool IsD3D12PipelineLibraryIid(REFIID riid)
		{
			return
				IsEqualIID(riid, __uuidof(ID3D12PipelineLibrary)) ||
				IsEqualIID(riid, __uuidof(ID3D12PipelineLibrary1));
		}

		bool IsD3D12ShaderCacheSessionIid(REFIID riid)
		{
			return IsEqualIID(riid, __uuidof(ID3D12ShaderCacheSession));
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

		bool IsD3D12DebugInterfaceIid(REFIID riid)
		{
			return
				IsEqualIID(riid, __uuidof(ID3D12Debug)) ||
				IsEqualIID(riid, __uuidof(ID3D12Debug1)) ||
				IsEqualIID(riid, __uuidof(ID3D12Debug2)) ||
				IsEqualIID(riid, __uuidof(ID3D12Debug3)) ||
				IsEqualIID(riid, __uuidof(ID3D12Debug4)) ||
				IsEqualIID(riid, __uuidof(ID3D12Debug5)) ||
				IsEqualIID(riid, __uuidof(ID3D12Debug6));
		}

		bool IsD3D12DredSettingsIid(REFIID riid)
		{
			return
				IsEqualIID(riid, __uuidof(ID3D12DeviceRemovedExtendedDataSettings)) ||
				IsEqualIID(riid, __uuidof(ID3D12DeviceRemovedExtendedDataSettings1)) ||
				IsEqualIID(riid, __uuidof(ID3D12DeviceRemovedExtendedDataSettings2));
		}

		bool IsD3D12DredDataIid(REFIID riid)
		{
			return
				IsEqualIID(riid, __uuidof(ID3D12DeviceRemovedExtendedData)) ||
				IsEqualIID(riid, __uuidof(ID3D12DeviceRemovedExtendedData1)) ||
				IsEqualIID(riid, __uuidof(ID3D12DeviceRemovedExtendedData2));
		}

		bool IsD3D12ToolsIid(REFIID riid)
		{
			return
				IsEqualIID(riid, __uuidof(ID3D12Tools)) ||
				IsEqualIID(riid, __uuidof(ID3D12Tools1)) ||
				IsEqualIID(riid, __uuidof(ID3D12Tools2));
		}

		bool IsD3D12SDKConfigurationIid(REFIID riid)
		{
			return
				IsEqualIID(riid, __uuidof(ID3D12SDKConfiguration)) ||
				IsEqualIID(riid, __uuidof(ID3D12SDKConfiguration1));
		}

		bool IsD3D12InfoQueueIid(REFIID riid)
		{
			return
				IsEqualIID(riid, __uuidof(ID3D12InfoQueue)) ||
				IsEqualIID(riid, __uuidof(ID3D12InfoQueue1));
		}

		bool IsD3D12DebugDeviceIid(REFIID riid)
		{
			return
				IsEqualIID(riid, __uuidof(ID3D12DebugDevice)) ||
				IsEqualIID(riid, __uuidof(ID3D12DebugDevice1)) ||
				IsEqualIID(riid, __uuidof(ID3D12DebugDevice2));
		}

		bool IsD3D12DebugCommandQueueIid(REFIID riid)
		{
			return
				IsEqualIID(riid, __uuidof(ID3D12DebugCommandQueue)) ||
				IsEqualIID(riid, __uuidof(ID3D12DebugCommandQueue1));
		}

		bool IsD3D12DebugCommandListIid(REFIID riid)
		{
			return
				IsEqualIID(riid, __uuidof(ID3D12DebugCommandList)) ||
				IsEqualIID(riid, __uuidof(ID3D12DebugCommandList1)) ||
				IsEqualIID(riid, __uuidof(ID3D12DebugCommandList2)) ||
				IsEqualIID(riid, __uuidof(ID3D12DebugCommandList3));
		}

		bool IsD3D12SharingContractIid(REFIID riid)
		{
			return IsEqualIID(riid, __uuidof(ID3D12SharingContract));
		}

		bool IsD3D12ManualWriteTrackingResourceIid(REFIID riid)
		{
			return IsEqualIID(riid, __uuidof(ID3D12ManualWriteTrackingResource));
		}

		bool IsD3D12AuxiliaryIid(REFIID riid)
		{
			return
				IsD3D12DebugInterfaceIid(riid) ||
				IsD3D12DredSettingsIid(riid) ||
				IsD3D12DredDataIid(riid) ||
				IsD3D12ToolsIid(riid) ||
				IsD3D12SDKConfigurationIid(riid) ||
				IsD3D12InfoQueueIid(riid) ||
				IsD3D12DebugDeviceIid(riid) ||
				IsD3D12DebugCommandQueueIid(riid) ||
				IsD3D12DebugCommandListIid(riid) ||
				IsD3D12SharingContractIid(riid) ||
				IsD3D12ManualWriteTrackingResourceIid(riid);
		}

		bool SupportsAnyD3D12AuxiliaryInterface(IUnknown *object)
		{
			return
				DoesObjectSupportIid(object, __uuidof(ID3D12Debug)) ||
				DoesObjectSupportIid(object, __uuidof(ID3D12Debug1)) ||
				DoesObjectSupportIid(object, __uuidof(ID3D12Debug2)) ||
				DoesObjectSupportIid(object, __uuidof(ID3D12Debug3)) ||
				DoesObjectSupportIid(object, __uuidof(ID3D12DeviceRemovedExtendedDataSettings)) ||
				DoesObjectSupportIid(object, __uuidof(ID3D12DeviceRemovedExtendedData)) ||
				DoesObjectSupportIid(object, __uuidof(ID3D12Tools)) ||
				DoesObjectSupportIid(object, __uuidof(ID3D12SDKConfiguration)) ||
				DoesObjectSupportIid(object, __uuidof(ID3D12InfoQueue)) ||
				DoesObjectSupportIid(object, __uuidof(ID3D12DebugDevice)) ||
				DoesObjectSupportIid(object, __uuidof(ID3D12DebugCommandQueue)) ||
				DoesObjectSupportIid(object, __uuidof(ID3D12DebugCommandList)) ||
				DoesObjectSupportIid(object, __uuidof(ID3D12SharingContract)) ||
				DoesObjectSupportIid(object, __uuidof(ID3D12ManualWriteTrackingResource));
		}

		bool TryWrapKnownD3D12Result(REFIID riid, void **object)
		{
			if (DX12Wrapper::WrapD3D12DeviceResult(riid, object))
				return true;
			return DX12Wrapper::WrapD3D12AuxiliaryResult(riid, object);
		}

		class WrapperBase
		{
		public:
			explicit WrapperBase(IUnknown *object)
				: WrapperBase(object, true)
			{
			}

			WrapperBase(IUnknown *object, bool registerIdentity)
			{
				mObject = object;
				if (mObject)
					mObject->AddRef();
				mIdentity = registerIdentity ? GetIdentity(object) : nullptr;

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
				DX12LogManager::JsonFunc("DX12Wrapper",
					"\"kind\":\"debug_interface\",\"stage\":\"create\",\"wrapper\":\"%p\",\"object\":\"%p\"",
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
				ULONG ref = static_cast<ULONG>(--mRefCount);
				if (!ref)
					delete this;
				return ref;
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
				DX12LogManager::JsonFunc("DX12Wrapper",
					"\"kind\":\"dred_settings\",\"stage\":\"create\",\"wrapper\":\"%p\",\"object\":\"%p\"",
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
				ULONG ref = static_cast<ULONG>(--mRefCount);
				if (!ref)
					delete this;
				return ref;
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
				DX12LogManager::JsonFunc("DX12Wrapper",
					"\"kind\":\"dred_data\",\"stage\":\"create\",\"wrapper\":\"%p\",\"object\":\"%p\"",
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
				ULONG ref = static_cast<ULONG>(--mRefCount);
				if (!ref)
					delete this;
				return ref;
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
				DX12LogManager::JsonFunc("DX12Wrapper",
					"\"kind\":\"tools\",\"stage\":\"create\",\"wrapper\":\"%p\",\"object\":\"%p\"",
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
				ULONG ref = static_cast<ULONG>(--mRefCount);
				if (!ref)
					delete this;
				return ref;
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
				DX12LogManager::JsonFunc("DX12Wrapper",
					"\"kind\":\"sdk_configuration\",\"stage\":\"create\",\"wrapper\":\"%p\",\"object\":\"%p\"",
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
				ULONG ref = static_cast<ULONG>(--mRefCount);
				if (!ref)
					delete this;
				return ref;
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
				DX12LogManager::JsonFunc("DX12Wrapper",
					"\"kind\":\"sharing_contract\",\"stage\":\"create\",\"wrapper\":\"%p\",\"object\":\"%p\"",
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
				ULONG ref = static_cast<ULONG>(--mRefCount);
				if (!ref)
					delete this;
				return ref;
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
				DX12LogManager::JsonFunc("DX12Wrapper",
					"\"kind\":\"manual_write_tracking_resource\",\"stage\":\"create\",\"wrapper\":\"%p\",\"object\":\"%p\"",
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
				ULONG ref = static_cast<ULONG>(--mRefCount);
				if (!ref)
					delete this;
				return ref;
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
				DX12LogManager::JsonFunc("DX12Wrapper",
					"\"kind\":\"info_queue\",\"stage\":\"create\",\"wrapper\":\"%p\",\"object\":\"%p\"",
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
				ULONG ref = static_cast<ULONG>(--mRefCount);
				if (!ref)
					delete this;
				return ref;
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
				DX12LogManager::JsonFunc("DX12Wrapper",
					"\"kind\":\"debug_device\",\"stage\":\"create\",\"wrapper\":\"%p\",\"object\":\"%p\"",
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
				ULONG ref = static_cast<ULONG>(--mRefCount);
				if (!ref)
					delete this;
				return ref;
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
				DX12LogManager::JsonFunc("DX12Wrapper",
					"\"kind\":\"debug_command_queue\",\"stage\":\"create\",\"wrapper\":\"%p\",\"object\":\"%p\"",
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
				ULONG ref = static_cast<ULONG>(--mRefCount);
				if (!ref)
					delete this;
				return ref;
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
				DX12LogManager::JsonFunc("DX12Wrapper",
					"\"kind\":\"debug_command_list\",\"stage\":\"create\",\"wrapper\":\"%p\",\"object\":\"%p\"",
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
				ULONG ref = static_cast<ULONG>(--mRefCount);
				if (!ref)
					delete this;
				return ref;
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
				if (SUCCEEDED(hr) && ppProtectedSession && *ppProtectedSession)
					DX12Wrapper::WrapD3D12DeviceResult(riid, ppProtectedSession);
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
				DX12LogManager::JsonFunc("DX12Wrapper",
					"\"kind\":\"lifetime_tracker\",\"stage\":\"create\",\"wrapper\":\"%p\",\"object\":\"%p\"",
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
				const ULONG ref = static_cast<ULONG>(--mRefCount);
				if (!ref)
					delete this;
				return ref;
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
				DX12LogManager::JsonFunc("DX12Wrapper",
					"\"kind\":\"protected_resource_session\",\"stage\":\"create\",\"wrapper\":\"%p\",\"object\":\"%p\"",
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
				const ULONG ref = static_cast<ULONG>(--mRefCount);
				if (!ref)
					delete this;
				return ref;
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
				DX12LogManager::JsonFunc("DX12Wrapper",
					"\"kind\":\"meta_command\",\"stage\":\"create\",\"wrapper\":\"%p\",\"object\":\"%p\"",
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
				const ULONG ref = static_cast<ULONG>(--mRefCount);
				if (!ref)
					delete this;
				return ref;
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
				DX12LogManager::JsonFunc("DX12Wrapper",
					"\"kind\":\"state_object\",\"stage\":\"create\",\"wrapper\":\"%p\",\"object\":\"%p\"",
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
				const ULONG ref = static_cast<ULONG>(--mRefCount);
				if (!ref)
					delete this;
				return ref;
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
				DX12LogManager::JsonFunc("DX12Wrapper",
					"\"kind\":\"pipeline_library\",\"stage\":\"create\",\"wrapper\":\"%p\",\"object\":\"%p\"",
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
				const ULONG ref = static_cast<ULONG>(--mRefCount);
				if (!ref)
					delete this;
				return ref;
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
				DX12LogManager::JsonFunc("DX12Wrapper",
					"\"kind\":\"shader_cache_session\",\"stage\":\"create\",\"wrapper\":\"%p\",\"object\":\"%p\"",
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
				const ULONG ref = static_cast<ULONG>(--mRefCount);
				if (!ref)
					delete this;
				return ref;
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

			// Resolve pre-hook original function pointers to bypass vtable hooks.
			// Eliminates double interception: wrapper callbacks call originals
			// directly instead of going through hooked vtable -> second hook cycle.
			mOrigDrawInstanced = ResolveOriginalDrawInstanced();
			mOrigDrawIndexedInstanced = ResolveOriginalDrawIndexedInstanced();
			mOrigDispatch = ResolveOriginalDispatch();
			mOrigSetPipelineState = ResolveOriginalSetPipelineState();
			mOrigResourceBarrier = ResolveOriginalResourceBarrier();
			mOrigIASetIndexBuffer = ResolveOriginalIASetIndexBuffer();
			mOrigIASetVertexBuffers = ResolveOriginalIASetVertexBuffers();
			mOrigExecuteIndirect = ResolveOriginalExecuteIndirect();

			DX12LogManager::JsonFunc("DX12Wrapper",
				"\"kind\":\"graphics_command_list\",\"stage\":\"create\",\"wrapper\":\"%p\",\"object\":\"%p\"",
				this, commandList);
		}

		DX12CommandListHookSignatures::DrawInstanced ResolveOriginalDrawInstanced()
		{
			return DX12CommandListHookRegistry::GetCommandListOriginal<
				DX12CommandListHookSignatures::DrawInstanced>(
				mCommandList,
				DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::DrawInstanced,
				DX12CommandListHookState::OrigDrawInstanced,
				"DrawInstanced");
		}

		DX12CommandListHookSignatures::DrawIndexedInstanced ResolveOriginalDrawIndexedInstanced()
		{
			return DX12CommandListHookRegistry::GetCommandListOriginal<
				DX12CommandListHookSignatures::DrawIndexedInstanced>(
				mCommandList,
				DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::DrawIndexedInstanced,
				DX12CommandListHookState::OrigDrawIndexedInstanced,
				"DrawIndexedInstanced");
		}

		DX12CommandListHookSignatures::Dispatch ResolveOriginalDispatch()
		{
			return DX12CommandListHookRegistry::GetCommandListOriginal<
				DX12CommandListHookSignatures::Dispatch>(
				mCommandList,
				DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::Dispatch,
				DX12CommandListHookState::OrigDispatch,
				"Dispatch");
		}

		DX12CommandListHookSignatures::SetPipelineState ResolveOriginalSetPipelineState()
		{
			return DX12CommandListHookRegistry::GetCommandListOriginal<
				DX12CommandListHookSignatures::SetPipelineState>(
				mCommandList,
				DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetPipelineState,
				DX12CommandListHookState::OrigSetPipelineState,
				"SetPipelineState");
		}

		DX12CommandListHookSignatures::ResourceBarrier ResolveOriginalResourceBarrier()
		{
			return DX12CommandListHookRegistry::GetCommandListOriginal<
				DX12CommandListHookSignatures::ResourceBarrier>(
				mCommandList,
				DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::ResourceBarrier,
				DX12CommandListHookState::OrigResourceBarrier,
				"ResourceBarrier");
		}

		DX12CommandListHookSignatures::IaSetIndexBuffer ResolveOriginalIASetIndexBuffer()
		{
			return DX12CommandListHookRegistry::GetCommandListOriginal<
				DX12CommandListHookSignatures::IaSetIndexBuffer>(
				mCommandList,
				DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::IaSetIndexBuffer,
				DX12CommandListHookState::OrigIASetIndexBuffer,
				"IASetIndexBuffer");
		}

		DX12CommandListHookSignatures::IaSetVertexBuffers ResolveOriginalIASetVertexBuffers()
		{
			return DX12CommandListHookRegistry::GetCommandListOriginal<
				DX12CommandListHookSignatures::IaSetVertexBuffers>(
				mCommandList,
				DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::IaSetVertexBuffers,
				DX12CommandListHookState::OrigIASetVertexBuffers,
				"IASetVertexBuffers");
		}

		DX12CommandListHookSignatures::ExecuteIndirect ResolveOriginalExecuteIndirect()
		{
			return DX12CommandListHookRegistry::GetCommandListOriginal<
				DX12CommandListHookSignatures::ExecuteIndirect>(
				mCommandList,
				DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::ExecuteIndirect,
				DX12CommandListHookState::OrigExecuteIndirect,
				"ExecuteIndirect");
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

				HRESULT hr = mCommandList->QueryInterface(riid, ppvObject);
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
			// Fast-path: skipAll + resolved original -> call driver directly, bypass hook entirely
			if (DX12HotPathState::skipAll && mOrigDrawInstanced) {
				mOrigDrawInstanced(mCommandList, VertexCountPerInstance, InstanceCount,
					StartVertexLocation, StartInstanceLocation);
				return;
			}
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
			if (DX12HotPathState::skipAll && mOrigDrawIndexedInstanced) {
				mOrigDrawIndexedInstanced(mCommandList, IndexCountPerInstance, InstanceCount,
					StartIndexLocation, BaseVertexLocation, StartInstanceLocation);
				return;
			}
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
			if (DX12HotPathState::skipAll && mOrigDispatch) {
				mOrigDispatch(mCommandList, ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);
				return;
			}
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
				DX12WrapperRuntimeUtils::IASetPrimitiveTopology(mCommandList, PrimitiveTopology);
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
				if (DX12HotPathState::skipAll && mOrigSetPipelineState) {
					mOrigSetPipelineState(mCommandList, pPipelineState);
					return;
				}
				DX12WrapperRuntimeUtils::SetPipelineState(mCommandList, pPipelineState, mOrigSetPipelineState);
			}

			void STDMETHODCALLTYPE ResourceBarrier(UINT NumBarriers, const D3D12_RESOURCE_BARRIER *pBarriers) override
			{
				if (DX12HotPathState::skipAll && mOrigResourceBarrier) {
					mOrigResourceBarrier(mCommandList, NumBarriers, pBarriers);
					return;
				}
				DX12WrapperRuntimeUtils::ResourceBarrier(mCommandList, NumBarriers, pBarriers, mOrigResourceBarrier);
			}

			void STDMETHODCALLTYPE ExecuteBundle(ID3D12GraphicsCommandList *pCommandList) override
			{
				mCommandList->ExecuteBundle(DX12WrapperRuntimeUtils::UnwrapGraphicsCommandList(pCommandList));
			}

			void STDMETHODCALLTYPE SetDescriptorHeaps(UINT NumDescriptorHeaps, ID3D12DescriptorHeap *const *ppDescriptorHeaps) override
			{
				// Stack buffer for common case (≤4 heaps). Avoids per-call heap allocation.
				ID3D12DescriptorHeap *stackUnwrapped[4];
				ID3D12DescriptorHeap *const *activeHeaps = ppDescriptorHeaps;
				std::vector<ID3D12DescriptorHeap*> heapUnwrapped;
				if (ppDescriptorHeaps && NumDescriptorHeaps) {
					ID3D12DescriptorHeap **dest = stackUnwrapped;
					if (NumDescriptorHeaps > 4) {
						heapUnwrapped.resize(NumDescriptorHeaps);
						dest = heapUnwrapped.data();
					}
					for (UINT i = 0; i < NumDescriptorHeaps; ++i)
						dest[i] = DX12WrapperRuntimeUtils::UnwrapDescriptorHeap(ppDescriptorHeaps[i]);
					activeHeaps = dest;
				}
				DX12WrapperRuntimeUtils::SetDescriptorHeaps(mCommandList, NumDescriptorHeaps, activeHeaps);
			}

			void STDMETHODCALLTYPE SetComputeRootSignature(ID3D12RootSignature *pRootSignature) override
			{
				DX12WrapperRuntimeUtils::SetComputeRootSignature(mCommandList,
					DX12WrapperRuntimeUtils::UnwrapRootSignature(pRootSignature));
			}

			void STDMETHODCALLTYPE SetGraphicsRootSignature(ID3D12RootSignature *pRootSignature) override
			{
				DX12WrapperRuntimeUtils::SetGraphicsRootSignature(mCommandList,
					DX12WrapperRuntimeUtils::UnwrapRootSignature(pRootSignature));
			}

			void STDMETHODCALLTYPE SetComputeRootDescriptorTable(
				UINT RootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor) override
			{
				DX12WrapperRuntimeUtils::SetComputeRootDescriptorTable(
					mCommandList, RootParameterIndex, BaseDescriptor);
			}

			void STDMETHODCALLTYPE SetGraphicsRootDescriptorTable(
				UINT RootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor) override
			{
				DX12WrapperRuntimeUtils::SetGraphicsRootDescriptorTable(
					mCommandList, RootParameterIndex, BaseDescriptor);
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
				DX12WrapperRuntimeUtils::SetComputeRootConstantBufferView(
					mCommandList, RootParameterIndex, BufferLocation);
			}

			void STDMETHODCALLTYPE SetGraphicsRootConstantBufferView(UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) override
			{
				DX12WrapperRuntimeUtils::SetGraphicsRootConstantBufferView(
					mCommandList, RootParameterIndex, BufferLocation);
			}

			void STDMETHODCALLTYPE SetComputeRootShaderResourceView(UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) override
			{
				DX12WrapperRuntimeUtils::SetComputeRootShaderResourceView(
					mCommandList, RootParameterIndex, BufferLocation);
			}

			void STDMETHODCALLTYPE SetGraphicsRootShaderResourceView(UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) override
			{
				DX12WrapperRuntimeUtils::SetGraphicsRootShaderResourceView(
					mCommandList, RootParameterIndex, BufferLocation);
			}

			void STDMETHODCALLTYPE SetComputeRootUnorderedAccessView(UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) override
			{
				DX12WrapperRuntimeUtils::SetComputeRootUnorderedAccessView(
					mCommandList, RootParameterIndex, BufferLocation);
			}

			void STDMETHODCALLTYPE SetGraphicsRootUnorderedAccessView(UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) override
			{
				DX12WrapperRuntimeUtils::SetGraphicsRootUnorderedAccessView(
					mCommandList, RootParameterIndex, BufferLocation);
			}

			void STDMETHODCALLTYPE IASetIndexBuffer(const D3D12_INDEX_BUFFER_VIEW *pView) override
			{
				if (DX12HotPathState::skipAll && mOrigIASetIndexBuffer) {
					mOrigIASetIndexBuffer(mCommandList, pView);
					return;
				}
				DX12WrapperRuntimeUtils::IASetIndexBuffer(mCommandList, pView, mOrigIASetIndexBuffer);
			}

			void STDMETHODCALLTYPE IASetVertexBuffers(UINT StartSlot, UINT NumViews, const D3D12_VERTEX_BUFFER_VIEW *pViews) override
			{
				if (DX12HotPathState::skipAll && mOrigIASetVertexBuffers) {
					mOrigIASetVertexBuffers(mCommandList, StartSlot, NumViews, pViews);
					return;
				}
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

			static bool TryWrapLifetimeTracker(REFIID riid, void **object)
			{
				if (!object || !*object || !IsD3D12LifetimeTrackerIid(riid))
					return false;

				IUnknown *originalObject = static_cast<IUnknown*>(*object);
				ID3D12LifetimeTracker *tracker = nullptr;
				if (FAILED(static_cast<IUnknown*>(*object)->QueryInterface(IID_PPV_ARGS(&tracker))))
					return false;

				WrapperBase *existing = WrapperBase::FindByIdentity(tracker);
				if (existing) {
					LifetimeTrackerWrapper *wrapper = dynamic_cast<LifetimeTrackerWrapper*>(existing);
					if (!wrapper) {
						tracker->Release();
						return false;
					}
					void *wrapperInterface = wrapper->GetLifetimeTrackerWrapperInterface(riid);
					if (!wrapperInterface) {
						tracker->Release();
						return false;
					}
					*object = wrapperInterface;
					reinterpret_cast<IUnknown*>(*object)->AddRef();
					tracker->Release();
					originalObject->Release();
					return true;
				}

				LifetimeTrackerWrapper *wrapper = new LifetimeTrackerWrapper(tracker);
				void *wrapperInterface = wrapper->GetLifetimeTrackerWrapperInterface(riid);
				if (!wrapperInterface) {
					tracker->Release();
					return false;
				}
				*object = wrapperInterface;
				tracker->Release();
				originalObject->Release();
				return true;
			}

			static bool TryWrapProtectedResourceSession(REFIID riid, void **object)
			{
				if (!object || !*object || !IsD3D12ProtectedResourceSessionIid(riid))
					return false;

				IUnknown *originalObject = static_cast<IUnknown*>(*object);
				ID3D12ProtectedResourceSession *session = nullptr;
				if (FAILED(static_cast<IUnknown*>(*object)->QueryInterface(IID_PPV_ARGS(&session))))
					return false;

				WrapperBase *existing = WrapperBase::FindByIdentity(session);
				if (existing) {
					ProtectedResourceSessionWrapper *wrapper =
						dynamic_cast<ProtectedResourceSessionWrapper*>(existing);
					if (!wrapper) {
						session->Release();
						return false;
					}
					void *wrapperInterface = wrapper->GetProtectedResourceSessionWrapperInterface(riid);
					if (!wrapperInterface) {
						session->Release();
						return false;
					}
					*object = wrapperInterface;
					reinterpret_cast<IUnknown*>(*object)->AddRef();
					session->Release();
					originalObject->Release();
					return true;
				}

				ProtectedResourceSessionWrapper *wrapper = new ProtectedResourceSessionWrapper(session);
				void *wrapperInterface = wrapper->GetProtectedResourceSessionWrapperInterface(riid);
				if (!wrapperInterface) {
					session->Release();
					return false;
				}
				*object = wrapperInterface;
				session->Release();
				originalObject->Release();
				return true;
			}

			static bool TryWrapMetaCommand(REFIID riid, void **object)
			{
				if (!object || !*object || !IsD3D12MetaCommandIid(riid))
					return false;

				IUnknown *originalObject = static_cast<IUnknown*>(*object);
				ID3D12MetaCommand *metaCommand = nullptr;
				if (FAILED(static_cast<IUnknown*>(*object)->QueryInterface(IID_PPV_ARGS(&metaCommand))))
					return false;

				WrapperBase *existing = WrapperBase::FindByIdentity(metaCommand);
				if (existing) {
					MetaCommandWrapper *wrapper = dynamic_cast<MetaCommandWrapper*>(existing);
					if (!wrapper) {
						metaCommand->Release();
						return false;
					}
					void *wrapperInterface = wrapper->GetMetaCommandWrapperInterface(riid);
					if (!wrapperInterface) {
						metaCommand->Release();
						return false;
					}
					*object = wrapperInterface;
					reinterpret_cast<IUnknown*>(*object)->AddRef();
					metaCommand->Release();
					originalObject->Release();
					return true;
				}

				MetaCommandWrapper *wrapper = new MetaCommandWrapper(metaCommand);
				void *wrapperInterface = wrapper->GetMetaCommandWrapperInterface(riid);
				if (!wrapperInterface) {
					metaCommand->Release();
					return false;
				}
				*object = wrapperInterface;
				metaCommand->Release();
				originalObject->Release();
				return true;
			}

			static bool TryWrapStateObject(REFIID riid, void **object)
			{
				if (!object || !*object || !IsD3D12StateObjectIid(riid))
					return false;

				IUnknown *originalObject = static_cast<IUnknown*>(*object);
				ID3D12StateObject *stateObject = nullptr;
				if (FAILED(static_cast<IUnknown*>(*object)->QueryInterface(IID_PPV_ARGS(&stateObject))))
					return false;

				WrapperBase *existing = WrapperBase::FindByIdentity(stateObject);
				if (existing) {
					StateObjectWrapper *wrapper = dynamic_cast<StateObjectWrapper*>(existing);
					if (!wrapper) {
						stateObject->Release();
						return false;
					}
					void *wrapperInterface = wrapper->GetStateObjectWrapperInterface(riid);
					if (!wrapperInterface) {
						stateObject->Release();
						return false;
					}
					*object = wrapperInterface;
					reinterpret_cast<IUnknown*>(*object)->AddRef();
					stateObject->Release();
					originalObject->Release();
					return true;
				}

				StateObjectWrapper *wrapper = new StateObjectWrapper(stateObject);
				void *wrapperInterface = wrapper->GetStateObjectWrapperInterface(riid);
				if (!wrapperInterface) {
					stateObject->Release();
					return false;
				}
				*object = wrapperInterface;
				stateObject->Release();
				originalObject->Release();
				return true;
			}

			static bool TryWrapPipelineLibrary(REFIID riid, void **object)
			{
				if (!object || !*object || !IsD3D12PipelineLibraryIid(riid))
					return false;

				IUnknown *originalObject = static_cast<IUnknown*>(*object);
				ID3D12PipelineLibrary *library = nullptr;
				if (FAILED(static_cast<IUnknown*>(*object)->QueryInterface(IID_PPV_ARGS(&library))))
					return false;

				WrapperBase *existing = WrapperBase::FindByIdentity(library);
				if (existing) {
					PipelineLibraryWrapper *wrapper = dynamic_cast<PipelineLibraryWrapper*>(existing);
					if (!wrapper) {
						library->Release();
						return false;
					}
					void *wrapperInterface = wrapper->GetPipelineLibraryWrapperInterface(riid);
					if (!wrapperInterface) {
						library->Release();
						return false;
					}
					*object = wrapperInterface;
					reinterpret_cast<IUnknown*>(*object)->AddRef();
					library->Release();
					originalObject->Release();
					return true;
				}

				PipelineLibraryWrapper *wrapper = new PipelineLibraryWrapper(library);
				void *wrapperInterface = wrapper->GetPipelineLibraryWrapperInterface(riid);
				if (!wrapperInterface) {
					library->Release();
					return false;
				}
				*object = wrapperInterface;
				library->Release();
				originalObject->Release();
				return true;
			}

			static bool TryWrapShaderCacheSession(REFIID riid, void **object)
			{
				if (!object || !*object || !IsD3D12ShaderCacheSessionIid(riid))
					return false;

				IUnknown *originalObject = static_cast<IUnknown*>(*object);
				ID3D12ShaderCacheSession *session = nullptr;
				if (FAILED(static_cast<IUnknown*>(*object)->QueryInterface(IID_PPV_ARGS(&session))))
					return false;

				WrapperBase *existing = WrapperBase::FindByIdentity(session);
				if (existing) {
					ShaderCacheSessionWrapper *wrapper = dynamic_cast<ShaderCacheSessionWrapper*>(existing);
					if (!wrapper) {
						session->Release();
						return false;
					}
					void *wrapperInterface = wrapper->GetShaderCacheSessionWrapperInterface(riid);
					if (!wrapperInterface) {
						session->Release();
						return false;
					}
					*object = wrapperInterface;
					reinterpret_cast<IUnknown*>(*object)->AddRef();
					session->Release();
					originalObject->Release();
					return true;
				}

				ShaderCacheSessionWrapper *wrapper = new ShaderCacheSessionWrapper(session);
				void *wrapperInterface = wrapper->GetShaderCacheSessionWrapperInterface(riid);
				if (!wrapperInterface) {
					session->Release();
					return false;
				}
				*object = wrapperInterface;
				session->Release();
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
				if (IsEqualIID(riid, __uuidof(ID3D12Object)))
					return static_cast<ID3D12Object*>(this);
				return nullptr;
			}

			HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) override
			{
				if (!ppvObject)
					return E_POINTER;
				*ppvObject = nullptr;

				if (IsEqualIID(riid, __uuidof(IUnknown)) || IsD3D12DeviceIid(riid) || IsEqualIID(riid, __uuidof(ID3D12Object))) {
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
					replacement.desc.pRootSignature =
						DX12WrapperRuntimeUtils::UnwrapRootSignature(pDesc->pRootSignature);
					activeDesc = &replacement.desc;
					if (DX12ModReplaceShaderBytecode("vs", pDesc->VS, &replacement.vs, &replacement.vsBytes)) {
						replacement.desc.VS = replacement.vs;
						replacement.changed = true;
					}
					if (DX12ModReplaceShaderBytecode("ps", pDesc->PS, &replacement.ps, &replacement.psBytes)) {
						replacement.desc.PS = replacement.ps;
						replacement.changed = true;
					}
				}

				HRESULT hr = mDevice->CreateGraphicsPipelineState(activeDesc, riid, ppPipelineState);
				DX12FrameAnalysis::LogJsonFunc("ID3D12Device::CreateGraphicsPipelineState",
					"\"hr\":\"0x%lx\",\"pso\":\"%p\"", hr, ppPipelineState ? *ppPipelineState : nullptr);
				if (SUCCEEDED(hr) && activeDesc) {
					ID3D12PipelineState *created =
						ppPipelineState ? static_cast<ID3D12PipelineState*>(*ppPipelineState) : nullptr;
					if (pDesc)
						DX12ModRecordGraphicsPipelineState(mDevice, created, activeDesc);
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
					replacement.desc.pRootSignature =
						DX12WrapperRuntimeUtils::UnwrapRootSignature(pDesc->pRootSignature);
					activeDesc = &replacement.desc;
					if (DX12ModReplaceShaderBytecode("cs", pDesc->CS, &replacement.cs, &replacement.csBytes)) {
						replacement.desc.CS = replacement.cs;
						replacement.changed = true;
					}
				}

				HRESULT hr = mDevice->CreateComputePipelineState(activeDesc, riid, ppPipelineState);
				DX12FrameAnalysis::LogJsonFunc("ID3D12Device::CreateComputePipelineState",
					"\"hr\":\"0x%lx\",\"pso\":\"%p\"", hr, ppPipelineState ? *ppPipelineState : nullptr);
				if (SUCCEEDED(hr) && activeDesc) {
					ID3D12PipelineState *created =
						ppPipelineState ? static_cast<ID3D12PipelineState*>(*ppPipelineState) : nullptr;
					if (pDesc)
						DX12ModRecordComputePipelineState(mDevice, created, activeDesc);
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
				if (SUCCEEDED(hr) && ppPipelineLibrary && *ppPipelineLibrary)
					DeviceWrapper::TryWrapPipelineLibrary(riid, ppPipelineLibrary);
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
				if (SUCCEEDED(hr) && ppSession && *ppSession)
					DeviceWrapper::TryWrapProtectedResourceSession(riid, ppSession);
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
				if (SUCCEEDED(hr) && ppvTracker && *ppvTracker)
					DeviceWrapper::TryWrapLifetimeTracker(riid, ppvTracker);
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
				if (SUCCEEDED(hr) && ppMetaCommand && *ppMetaCommand)
					DeviceWrapper::TryWrapMetaCommand(riid, ppMetaCommand);
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
				D3D12_STATE_OBJECT_DESC descStorage = {};
				std::vector<D3D12_STATE_SUBOBJECT> subobjectStorage;
				std::vector<D3D12_GLOBAL_ROOT_SIGNATURE> globalRootSignatureStorage;
				std::vector<D3D12_LOCAL_ROOT_SIGNATURE> localRootSignatureStorage;
				std::vector<D3D12_EXISTING_COLLECTION_DESC> existingCollectionStorage;
				std::vector<D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION> associationStorage;
				const D3D12_STATE_OBJECT_DESC *activeDesc = pDesc;
				DX12WrapperRuntimeUtils::UnwrapStateObjectDesc(
					pDesc,
					&descStorage,
					&subobjectStorage,
					&globalRootSignatureStorage,
					&localRootSignatureStorage,
					&existingCollectionStorage,
					&associationStorage,
					&activeDesc);
				hr = device5->CreateStateObject(activeDesc, riid, ppStateObject);
				if (SUCCEEDED(hr) && ppStateObject && *ppStateObject)
					DeviceWrapper::TryWrapStateObject(riid, ppStateObject);
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
				D3D12_STATE_OBJECT_DESC descStorage = {};
				std::vector<D3D12_STATE_SUBOBJECT> subobjectStorage;
				std::vector<D3D12_GLOBAL_ROOT_SIGNATURE> globalRootSignatureStorage;
				std::vector<D3D12_LOCAL_ROOT_SIGNATURE> localRootSignatureStorage;
				std::vector<D3D12_EXISTING_COLLECTION_DESC> existingCollectionStorage;
				std::vector<D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION> associationStorage;
				const D3D12_STATE_OBJECT_DESC *activeAddition = pAddition;
				DX12WrapperRuntimeUtils::UnwrapStateObjectDesc(
					pAddition,
					&descStorage,
					&subobjectStorage,
					&globalRootSignatureStorage,
					&localRootSignatureStorage,
					&existingCollectionStorage,
					&associationStorage,
					&activeAddition);
				hr = device7->AddToStateObject(
					activeAddition,
					DX12WrapperRuntimeUtils::UnwrapStateObject(pStateObjectToGrowFrom),
					riid,
					ppNewStateObject);
				if (SUCCEEDED(hr) && ppNewStateObject && *ppNewStateObject)
					DeviceWrapper::TryWrapStateObject(riid, ppNewStateObject);
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
				if (SUCCEEDED(hr) && ppSession && *ppSession)
					DeviceWrapper::TryWrapProtectedResourceSession(riid, ppSession);
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
				if (SUCCEEDED(hr) && ppvSession && *ppvSession)
					DeviceWrapper::TryWrapShaderCacheSession(riid, ppvSession);
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

		template <typename TWrapper, typename TQueried>
		bool TryWrapAuxiliaryAs(REFIID riid, void **object)
		{
			if (!object || !*object)
				return false;

			IUnknown *originalObject = static_cast<IUnknown*>(*object);
			WrapperBase *existingByPointer = WrapperBase::FindByWrapperPointer(originalObject);
			if (existingByPointer) {
				TWrapper *existingWrapper = dynamic_cast<TWrapper*>(existingByPointer);
				if (!existingWrapper)
					return false;
				void *existingInterface = existingWrapper->GetAuxiliaryWrapperInterface(riid);
				if (!existingInterface)
					return false;
				IUnknown *previousObject = originalObject;
				*object = existingInterface;
				reinterpret_cast<IUnknown*>(*object)->AddRef();
				previousObject->Release();
				return true;
			}

			TQueried *queried = nullptr;
			if (FAILED(originalObject->QueryInterface(IID_PPV_ARGS(&queried))))
				return false;

			WrapperBase *existingByIdentity = WrapperBase::FindByIdentity(queried);
			if (existingByIdentity) {
				TWrapper *existingWrapper = dynamic_cast<TWrapper*>(existingByIdentity);
				if (existingWrapper) {
					void *existingInterface = existingWrapper->GetAuxiliaryWrapperInterface(riid);
					if (existingInterface) {
						*object = existingInterface;
						reinterpret_cast<IUnknown*>(*object)->AddRef();
						queried->Release();
						originalObject->Release();
						return true;
					}
				}
			}

			TWrapper *wrapper = new TWrapper(queried);
			void *wrapperInterface = wrapper->GetAuxiliaryWrapperInterface(riid);
			if (!wrapperInterface) {
				queried->Release();
				wrapper->Release();
				return false;
			}

			*object = wrapperInterface;
			queried->Release();
			originalObject->Release();
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
			if (DeviceWrapper::TryWrapLifetimeTracker(__uuidof(ID3D12LifetimeTracker), device))
				return true;
			if (DeviceWrapper::TryWrapProtectedResourceSession(__uuidof(ID3D12ProtectedResourceSession), device))
				return true;
			if (DeviceWrapper::TryWrapMetaCommand(__uuidof(ID3D12MetaCommand), device))
				return true;
			if (DeviceWrapper::TryWrapStateObject(__uuidof(ID3D12StateObject), device))
				return true;
			if (DeviceWrapper::TryWrapPipelineLibrary(__uuidof(ID3D12PipelineLibrary), device))
				return true;
			if (DeviceWrapper::TryWrapShaderCacheSession(__uuidof(ID3D12ShaderCacheSession), device))
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

		if (DeviceWrapper::TryWrapLifetimeTracker(riid, device))
			return true;

		if (DeviceWrapper::TryWrapProtectedResourceSession(riid, device))
			return true;

		if (DeviceWrapper::TryWrapMetaCommand(riid, device))
			return true;

		if (DeviceWrapper::TryWrapStateObject(riid, device))
			return true;

		if (DeviceWrapper::TryWrapPipelineLibrary(riid, device))
			return true;

		if (DeviceWrapper::TryWrapShaderCacheSession(riid, device))
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

	bool WrapD3D12LifetimeTrackerResult(REFIID riid, void **tracker)
	{
		return DeviceWrapper::TryWrapLifetimeTracker(riid, tracker);
	}

	bool WrapD3D12ProtectedResourceSessionResult(REFIID riid, void **session)
	{
		return DeviceWrapper::TryWrapProtectedResourceSession(riid, session);
	}

	bool WrapD3D12MetaCommandResult(REFIID riid, void **metaCommand)
	{
		return DeviceWrapper::TryWrapMetaCommand(riid, metaCommand);
	}

	bool WrapD3D12StateObjectResult(REFIID riid, void **stateObject)
	{
		return DeviceWrapper::TryWrapStateObject(riid, stateObject);
	}

	bool WrapD3D12PipelineLibraryResult(REFIID riid, void **library)
	{
		return DeviceWrapper::TryWrapPipelineLibrary(riid, library);
	}

	bool WrapD3D12ShaderCacheSessionResult(REFIID riid, void **session)
	{
		return DeviceWrapper::TryWrapShaderCacheSession(riid, session);
	}

	bool WrapD3D12DebugInterfaceResult(REFIID riid, void **object)
	{
		if (!object || !*object ||
			(!IsD3D12DebugInterfaceIid(riid) && !IsD3D12DredSettingsIid(riid) && !IsEqualIID(riid, __uuidof(IUnknown))))
			return false;

		if (IsEqualIID(riid, __uuidof(IUnknown))) {
			if (TryWrapAuxiliaryAs<DebugInterfaceWrapper, IUnknown>(__uuidof(ID3D12Debug), object))
				return true;
			if (TryWrapAuxiliaryAs<DredSettingsWrapper, ID3D12DeviceRemovedExtendedDataSettings>(
				__uuidof(ID3D12DeviceRemovedExtendedDataSettings), object))
				return true;
			return false;
		}

		if (IsD3D12DebugInterfaceIid(riid))
			return TryWrapAuxiliaryAs<DebugInterfaceWrapper, IUnknown>(riid, object);

		if (IsD3D12DredSettingsIid(riid))
			return TryWrapAuxiliaryAs<DredSettingsWrapper, ID3D12DeviceRemovedExtendedDataSettings>(riid, object);

		return false;
	}

	bool WrapD3D12AuxiliaryResult(REFIID riid, void **object)
	{
		if (!object || !*object)
			return false;

		if (IsEqualIID(riid, __uuidof(IUnknown))) {
			if (!SupportsAnyD3D12AuxiliaryInterface(static_cast<IUnknown*>(*object)))
				return false;
			if (TryWrapAuxiliaryAs<DebugInterfaceWrapper, IUnknown>(__uuidof(ID3D12Debug), object))
				return true;
			if (TryWrapAuxiliaryAs<DredSettingsWrapper, ID3D12DeviceRemovedExtendedDataSettings>(
				__uuidof(ID3D12DeviceRemovedExtendedDataSettings), object))
				return true;
			if (TryWrapAuxiliaryAs<DredDataWrapper, ID3D12DeviceRemovedExtendedData>(
				__uuidof(ID3D12DeviceRemovedExtendedData), object))
				return true;
			if (TryWrapAuxiliaryAs<ToolsWrapper, ID3D12Tools>(__uuidof(ID3D12Tools), object))
				return true;
			if (TryWrapAuxiliaryAs<SDKConfigurationWrapper, ID3D12SDKConfiguration>(
				__uuidof(ID3D12SDKConfiguration), object))
				return true;
			if (TryWrapAuxiliaryAs<InfoQueueWrapper, ID3D12InfoQueue>(__uuidof(ID3D12InfoQueue), object))
				return true;
			if (TryWrapAuxiliaryAs<DebugDeviceWrapper, ID3D12DebugDevice>(__uuidof(ID3D12DebugDevice), object))
				return true;
			if (TryWrapAuxiliaryAs<DebugCommandQueueWrapper, ID3D12DebugCommandQueue>(
				__uuidof(ID3D12DebugCommandQueue), object))
				return true;
			if (TryWrapAuxiliaryAs<DebugCommandListWrapper, ID3D12DebugCommandList>(
				__uuidof(ID3D12DebugCommandList), object))
				return true;
			if (TryWrapAuxiliaryAs<SharingContractWrapper, ID3D12SharingContract>(
				__uuidof(ID3D12SharingContract), object))
				return true;
			if (TryWrapAuxiliaryAs<ManualWriteTrackingResourceWrapper, ID3D12ManualWriteTrackingResource>(
				__uuidof(ID3D12ManualWriteTrackingResource), object))
				return true;
			return false;
		}

		if (!IsD3D12AuxiliaryIid(riid))
			return false;

		if (IsD3D12DebugInterfaceIid(riid))
			return TryWrapAuxiliaryAs<DebugInterfaceWrapper, IUnknown>(riid, object);
		if (IsD3D12DredSettingsIid(riid))
			return TryWrapAuxiliaryAs<DredSettingsWrapper, ID3D12DeviceRemovedExtendedDataSettings>(riid, object);
		if (IsD3D12DredDataIid(riid))
			return TryWrapAuxiliaryAs<DredDataWrapper, ID3D12DeviceRemovedExtendedData>(riid, object);
		if (IsD3D12ToolsIid(riid))
			return TryWrapAuxiliaryAs<ToolsWrapper, ID3D12Tools>(riid, object);
		if (IsD3D12SDKConfigurationIid(riid))
			return TryWrapAuxiliaryAs<SDKConfigurationWrapper, ID3D12SDKConfiguration>(riid, object);
		if (IsD3D12InfoQueueIid(riid))
			return TryWrapAuxiliaryAs<InfoQueueWrapper, ID3D12InfoQueue>(riid, object);
		if (IsD3D12DebugDeviceIid(riid))
			return TryWrapAuxiliaryAs<DebugDeviceWrapper, ID3D12DebugDevice>(riid, object);
		if (IsD3D12DebugCommandQueueIid(riid))
			return TryWrapAuxiliaryAs<DebugCommandQueueWrapper, ID3D12DebugCommandQueue>(riid, object);
		if (IsD3D12DebugCommandListIid(riid))
			return TryWrapAuxiliaryAs<DebugCommandListWrapper, ID3D12DebugCommandList>(riid, object);
		if (IsD3D12SharingContractIid(riid))
			return TryWrapAuxiliaryAs<SharingContractWrapper, ID3D12SharingContract>(riid, object);
		if (IsD3D12ManualWriteTrackingResourceIid(riid))
			return TryWrapAuxiliaryAs<ManualWriteTrackingResourceWrapper, ID3D12ManualWriteTrackingResource>(riid, object);

		return false;
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

		// Thread-local one-element cache: repeated unwraps of the same
		// wrapped pointer (common for PSO, root signature, resource)
		// bypass the SRW lock + hashmap lookup entirely.
		static thread_local IUnknown *tlsCachedWrapperPtr = nullptr;
		static thread_local IUnknown *tlsCachedRealPtr = nullptr;

		if (object == tlsCachedWrapperPtr)
			return tlsCachedRealPtr;

		WrapperBase *wrapper = WrapperBase::FindByWrapperPointer(object);
		IUnknown *result = wrapper ? wrapper->GetObject() : object;

		tlsCachedWrapperPtr = object;
		tlsCachedRealPtr = result;
		return result;
	}

void Shutdown()
{
	DX12LogManager::JsonFunc("DX12Wrapper", "\"stage\":\"shutdown\"");
}
}
