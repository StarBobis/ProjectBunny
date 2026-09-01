// Wrapper identity registry, IID predicates and WrapperBase lifetime core.
// Split from DX12Wrapper.cpp; COM wrapper class definitions (inline).
#pragma once

#include "../DX12Core.h"
#include "../DX12Hooks.h"
#include "../DX12Tracking.h"
#include "../DX12Runtime.h"
#include "../DX12Resources.h"
#include "../DX12Presentation.h"
#include <algorithm>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <d3d12sdklayers.h>
#include <intrin.h>

namespace DX12WrapperDetail
{

	class WrapperBase;

	inline SRWLOCK gWrapperLock = SRWLOCK_INIT;
	inline std::atomic<UINT64> gWrapperRegistryGeneration{1};
	inline std::unordered_map<IUnknown*, WrapperBase*> gIdentityMap;
	inline std::unordered_map<IUnknown*, WrapperBase*> gWrapperMap;

	// Wrapper creation is serialized per identity shard, not on one global
	// mutex. The registry maps are already protected by gWrapperLock, so a
	// shard mutex only needs to make the find-then-create sequence atomic
	// for a single identity; threads wrapping different objects never
	// contend. A single global creation mutex (DX11-style thinking)
	// serializes every parallel loading thread's resource/PSO/heap
	// wrapping against each other.
	inline constexpr UINT kWrapperCreationShardCount = 64;
	inline std::recursive_mutex
		gWrapperCreationShardMutexes[kWrapperCreationShardCount];

	inline std::recursive_mutex &WrapperCreationShardFor(IUnknown *identity)
	{
		const UINT shard = static_cast<UINT>(
			(reinterpret_cast<UINT_PTR>(identity) >> 4) &
			(kWrapperCreationShardCount - 1));
		return gWrapperCreationShardMutexes[shard];
	}

	inline IUnknown *GetIdentity(IUnknown *object)
	{
		if (!object)
			return nullptr;

		IUnknown *identity = nullptr;
		if (FAILED(object->QueryInterface(IID_PPV_ARGS(&identity))))
			return nullptr;
		return identity;
	}

	inline bool IsD3D12DeviceIid(REFIID riid)
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

	inline bool IsD3D12CommandQueueIid(REFIID riid)
	{
		return IsEqualIID(riid, __uuidof(ID3D12CommandQueue));
	}

	inline bool IsD3D12ObjectIid(REFIID riid)
	{
		return IsEqualIID(riid, __uuidof(ID3D12Object));
	}

	inline bool IsD3D12DeviceChildIid(REFIID riid)
	{
		return IsD3D12ObjectIid(riid) || IsEqualIID(riid, __uuidof(ID3D12DeviceChild));
	}

	inline bool IsD3D12PageableIid(REFIID riid)
	{
		return IsD3D12DeviceChildIid(riid) || IsEqualIID(riid, __uuidof(ID3D12Pageable));
	}

	inline bool IsD3D12CommandQueueWrapperIid(REFIID riid)
	{
		return IsD3D12PageableIid(riid) || IsEqualIID(riid, __uuidof(ID3D12CommandQueue));
	}

	inline bool IsD3D12CommandAllocatorWrapperIid(REFIID riid)
	{
		return IsD3D12PageableIid(riid) || IsEqualIID(riid, __uuidof(ID3D12CommandAllocator));
	}

	inline bool IsD3D12FenceWrapperIid(REFIID riid)
	{
		return IsD3D12PageableIid(riid) || IsEqualIID(riid, __uuidof(ID3D12Fence));
	}

	inline bool IsD3D12DescriptorHeapWrapperIid(REFIID riid)
	{
		return IsD3D12PageableIid(riid) || IsEqualIID(riid, __uuidof(ID3D12DescriptorHeap));
	}

	inline bool IsD3D12ResourceWrapperIid(REFIID riid)
	{
		return
			IsD3D12PageableIid(riid) ||
			IsEqualIID(riid, __uuidof(ID3D12Resource)) ||
			IsEqualIID(riid, __uuidof(ID3D12Resource1)) ||
			IsEqualIID(riid, __uuidof(ID3D12Resource2));
	}

	inline bool IsD3D12HeapWrapperIid(REFIID riid)
	{
		return IsD3D12PageableIid(riid) || IsEqualIID(riid, __uuidof(ID3D12Heap));
	}

	inline bool IsD3D12PipelineStateWrapperIid(REFIID riid)
	{
		return IsD3D12PageableIid(riid) || IsEqualIID(riid, __uuidof(ID3D12PipelineState));
	}

	inline bool IsD3D12RootSignatureWrapperIid(REFIID riid)
	{
		return IsD3D12DeviceChildIid(riid) || IsEqualIID(riid, __uuidof(ID3D12RootSignature));
	}

	inline bool IsD3D12QueryHeapWrapperIid(REFIID riid)
	{
		return IsD3D12PageableIid(riid) || IsEqualIID(riid, __uuidof(ID3D12QueryHeap));
	}

	inline bool IsD3D12CommandSignatureWrapperIid(REFIID riid)
	{
		return IsD3D12PageableIid(riid) || IsEqualIID(riid, __uuidof(ID3D12CommandSignature));
	}

	inline bool IsD3D12LifetimeTrackerWrapperIid(REFIID riid)
	{
		return IsD3D12DeviceChildIid(riid) || IsEqualIID(riid, __uuidof(ID3D12LifetimeTracker));
	}

	inline bool IsD3D12ProtectedResourceSessionWrapperIid(REFIID riid)
	{
		return
			IsD3D12DeviceChildIid(riid) ||
			IsEqualIID(riid, __uuidof(ID3D12ProtectedSession)) ||
			IsEqualIID(riid, __uuidof(ID3D12ProtectedResourceSession)) ||
			IsEqualIID(riid, __uuidof(ID3D12ProtectedResourceSession1));
	}

	inline bool IsD3D12MetaCommandWrapperIid(REFIID riid)
	{
		return IsD3D12PageableIid(riid) || IsEqualIID(riid, __uuidof(ID3D12MetaCommand));
	}

	inline bool IsD3D12StateObjectWrapperIid(REFIID riid)
	{
		return
			IsD3D12PageableIid(riid) ||
			IsEqualIID(riid, __uuidof(ID3D12StateObject)) ||
			IsEqualIID(riid, __uuidof(ID3D12StateObjectProperties)) ||
			IsEqualIID(riid, __uuidof(ID3D12StateObjectProperties1));
	}

	inline bool IsD3D12PipelineLibraryWrapperIid(REFIID riid)
	{
		return
			IsD3D12DeviceChildIid(riid) ||
			IsEqualIID(riid, __uuidof(ID3D12PipelineLibrary)) ||
			IsEqualIID(riid, __uuidof(ID3D12PipelineLibrary1));
	}

	inline bool IsD3D12ShaderCacheSessionWrapperIid(REFIID riid)
	{
		return IsD3D12DeviceChildIid(riid) || IsEqualIID(riid, __uuidof(ID3D12ShaderCacheSession));
	}

	inline bool IsD3D12DeviceFactoryIid(REFIID riid)
	{
		return IsEqualIID(riid, __uuidof(ID3D12DeviceFactory));
	}

	inline bool IsD3D12CommandAllocatorIid(REFIID riid)
	{
		return IsEqualIID(riid, __uuidof(ID3D12CommandAllocator));
	}

	inline bool IsD3D12FenceIid(REFIID riid)
	{
		return IsEqualIID(riid, __uuidof(ID3D12Fence)) || IsEqualIID(riid, __uuidof(ID3D12Fence1));
	}

	inline bool IsD3D12DescriptorHeapIid(REFIID riid)
	{
		return IsEqualIID(riid, __uuidof(ID3D12DescriptorHeap));
	}

	inline bool IsD3D12ResourceIid(REFIID riid)
	{
		return
			IsEqualIID(riid, __uuidof(ID3D12Resource)) ||
			IsEqualIID(riid, __uuidof(ID3D12Resource1)) ||
			IsEqualIID(riid, __uuidof(ID3D12Resource2));
	}

	inline bool IsD3D12HeapIid(REFIID riid)
	{
		return IsEqualIID(riid, __uuidof(ID3D12Heap));
	}

	inline bool IsD3D12PipelineStateIid(REFIID riid)
	{
		return IsEqualIID(riid, __uuidof(ID3D12PipelineState));
	}

	inline bool IsD3D12RootSignatureIid(REFIID riid)
	{
		return IsEqualIID(riid, __uuidof(ID3D12RootSignature));
	}

	inline bool IsD3D12QueryHeapIid(REFIID riid)
	{
		return IsEqualIID(riid, __uuidof(ID3D12QueryHeap));
	}

	inline bool IsD3D12CommandSignatureIid(REFIID riid)
	{
		return IsEqualIID(riid, __uuidof(ID3D12CommandSignature));
	}

	inline bool IsD3D12LifetimeTrackerIid(REFIID riid)
	{
		return IsEqualIID(riid, __uuidof(ID3D12LifetimeTracker));
	}

	inline bool IsD3D12ProtectedResourceSessionIid(REFIID riid)
	{
		return
			IsEqualIID(riid, __uuidof(ID3D12ProtectedSession)) ||
			IsEqualIID(riid, __uuidof(ID3D12ProtectedResourceSession)) ||
			IsEqualIID(riid, __uuidof(ID3D12ProtectedResourceSession1));
	}

	inline bool IsD3D12MetaCommandIid(REFIID riid)
	{
		return IsEqualIID(riid, __uuidof(ID3D12MetaCommand));
	}

	inline bool IsD3D12StateObjectIid(REFIID riid)
	{
		return
			IsEqualIID(riid, __uuidof(ID3D12StateObject)) ||
			IsEqualIID(riid, __uuidof(ID3D12StateObjectProperties)) ||
			IsEqualIID(riid, __uuidof(ID3D12StateObjectProperties1));
	}

	inline bool IsD3D12PipelineLibraryIid(REFIID riid)
	{
		return
			IsEqualIID(riid, __uuidof(ID3D12PipelineLibrary)) ||
			IsEqualIID(riid, __uuidof(ID3D12PipelineLibrary1));
	}

	inline bool IsD3D12ShaderCacheSessionIid(REFIID riid)
	{
		return IsEqualIID(riid, __uuidof(ID3D12ShaderCacheSession));
	}

	inline bool IsD3D12GraphicsCommandListIid(REFIID riid)
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

	inline bool IsD3D12GraphicsCommandListWrapperIid(REFIID riid)
	{
		return
			IsD3D12DeviceChildIid(riid) ||
			IsEqualIID(riid, __uuidof(ID3D12CommandList)) ||
			IsD3D12GraphicsCommandListIid(riid);
	}

	inline bool IsDxgiFactoryIid(REFIID riid)
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

	inline bool IsDxgiFactoryWrapperIid(REFIID riid)
	{
		return IsEqualIID(riid, __uuidof(IDXGIObject)) || IsDxgiFactoryIid(riid);
	}

	inline bool IsDxgiSwapChainIid(REFIID riid)
	{
		return
			IsEqualIID(riid, __uuidof(IDXGISwapChain)) ||
			IsEqualIID(riid, __uuidof(IDXGISwapChain1)) ||
			IsEqualIID(riid, __uuidof(IDXGISwapChain2)) ||
			IsEqualIID(riid, __uuidof(IDXGISwapChain3)) ||
			IsEqualIID(riid, __uuidof(IDXGISwapChain4));
	}

	inline bool IsDxgiSwapChainWrapperIid(REFIID riid)
	{
		return
			IsEqualIID(riid, __uuidof(IDXGIObject)) ||
			IsEqualIID(riid, __uuidof(IDXGIDeviceSubObject)) ||
			IsDxgiSwapChainIid(riid);
	}

	struct InterfaceSupportCacheEntry
	{
		IUnknown *object = nullptr;
		IID iid = {};
		UINT64 generation = 0;
		bool supported = false;
	};

	inline bool DoesObjectSupportIidUncached(IUnknown *object, REFIID riid)
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

	// QI support is stable for a COM object's lifetime, and wrapper
	// destruction bumps the registry generation, so per-thread caching
	// keyed by the underlying object is safe.
	inline bool DoesObjectSupportIid(IUnknown *object, REFIID riid)
	{
		if (!object)
			return false;
		static constexpr UINT kCacheEntries = 16;
		static thread_local InterfaceSupportCacheEntry cache[kCacheEntries];
		static thread_local UINT next = 0;
		const UINT64 generation =
			gWrapperRegistryGeneration.load(std::memory_order_acquire);
		for (UINT i = 0; i < kCacheEntries; ++i) {
			const InterfaceSupportCacheEntry &entry = cache[i];
			if (entry.object == object &&
			    entry.generation == generation &&
			    IsEqualIID(entry.iid, riid))
				return entry.supported;
		}
		const bool supported = DoesObjectSupportIidUncached(object, riid);
		InterfaceSupportCacheEntry &entry = cache[next++ & (kCacheEntries - 1)];
		entry.object = object;
		entry.iid = riid;
		entry.generation = generation;
		entry.supported = supported;
		return supported;
	}

	inline bool IsSupportedWrapperIid(IUnknown *object, REFIID riid, bool (*isWrapperIid)(REFIID))
	{
		if (!isWrapperIid || !isWrapperIid(riid))
			return false;
		if (IsEqualIID(riid, __uuidof(IUnknown)))
			return true;
		return DoesObjectSupportIid(object, riid);
	}

	inline bool IsD3D12DebugInterfaceIid(REFIID riid)
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

	inline bool IsD3D12DredSettingsIid(REFIID riid)
	{
		return
			IsEqualIID(riid, __uuidof(ID3D12DeviceRemovedExtendedDataSettings)) ||
			IsEqualIID(riid, __uuidof(ID3D12DeviceRemovedExtendedDataSettings1)) ||
			IsEqualIID(riid, __uuidof(ID3D12DeviceRemovedExtendedDataSettings2));
	}

	inline bool IsD3D12DredDataIid(REFIID riid)
	{
		return
			IsEqualIID(riid, __uuidof(ID3D12DeviceRemovedExtendedData)) ||
			IsEqualIID(riid, __uuidof(ID3D12DeviceRemovedExtendedData1)) ||
			IsEqualIID(riid, __uuidof(ID3D12DeviceRemovedExtendedData2));
	}

	inline bool IsD3D12ToolsIid(REFIID riid)
	{
		return
			IsEqualIID(riid, __uuidof(ID3D12Tools)) ||
			IsEqualIID(riid, __uuidof(ID3D12Tools1)) ||
			IsEqualIID(riid, __uuidof(ID3D12Tools2));
	}

	inline bool IsD3D12SDKConfigurationIid(REFIID riid)
	{
		return
			IsEqualIID(riid, __uuidof(ID3D12SDKConfiguration)) ||
			IsEqualIID(riid, __uuidof(ID3D12SDKConfiguration1));
	}

	inline bool IsD3D12InfoQueueIid(REFIID riid)
	{
		return
			IsEqualIID(riid, __uuidof(ID3D12InfoQueue)) ||
			IsEqualIID(riid, __uuidof(ID3D12InfoQueue1));
	}

	inline bool IsD3D12DebugDeviceIid(REFIID riid)
	{
		return
			IsEqualIID(riid, __uuidof(ID3D12DebugDevice)) ||
			IsEqualIID(riid, __uuidof(ID3D12DebugDevice1)) ||
			IsEqualIID(riid, __uuidof(ID3D12DebugDevice2));
	}

	inline bool IsD3D12DebugCommandQueueIid(REFIID riid)
	{
		return
			IsEqualIID(riid, __uuidof(ID3D12DebugCommandQueue)) ||
			IsEqualIID(riid, __uuidof(ID3D12DebugCommandQueue1));
	}

	inline bool IsD3D12DebugCommandListIid(REFIID riid)
	{
		return
			IsEqualIID(riid, __uuidof(ID3D12DebugCommandList)) ||
			IsEqualIID(riid, __uuidof(ID3D12DebugCommandList1)) ||
			IsEqualIID(riid, __uuidof(ID3D12DebugCommandList2)) ||
			IsEqualIID(riid, __uuidof(ID3D12DebugCommandList3));
	}

	inline bool IsD3D12SharingContractIid(REFIID riid)
	{
		return IsEqualIID(riid, __uuidof(ID3D12SharingContract));
	}

	inline bool IsD3D12ManualWriteTrackingResourceIid(REFIID riid)
	{
		return IsEqualIID(riid, __uuidof(ID3D12ManualWriteTrackingResource));
	}

	inline bool IsD3D12AuxiliaryIid(REFIID riid)
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

	inline bool SupportsAnyD3D12AuxiliaryInterface(IUnknown *object)
	{
		return
			DoesObjectSupportIidUncached(object, __uuidof(ID3D12Debug)) ||
			DoesObjectSupportIidUncached(object, __uuidof(ID3D12Debug1)) ||
			DoesObjectSupportIidUncached(object, __uuidof(ID3D12Debug2)) ||
			DoesObjectSupportIidUncached(object, __uuidof(ID3D12Debug3)) ||
			DoesObjectSupportIidUncached(object, __uuidof(ID3D12DeviceRemovedExtendedDataSettings)) ||
			DoesObjectSupportIidUncached(object, __uuidof(ID3D12DeviceRemovedExtendedData)) ||
			DoesObjectSupportIidUncached(object, __uuidof(ID3D12Tools)) ||
			DoesObjectSupportIidUncached(object, __uuidof(ID3D12SDKConfiguration)) ||
			DoesObjectSupportIidUncached(object, __uuidof(ID3D12InfoQueue)) ||
			DoesObjectSupportIidUncached(object, __uuidof(ID3D12DebugDevice)) ||
			DoesObjectSupportIidUncached(object, __uuidof(ID3D12DebugCommandQueue)) ||
			DoesObjectSupportIidUncached(object, __uuidof(ID3D12DebugCommandList)) ||
			DoesObjectSupportIidUncached(object, __uuidof(ID3D12SharingContract)) ||
			DoesObjectSupportIidUncached(object, __uuidof(ID3D12ManualWriteTrackingResource));
	}

	inline bool TryWrapKnownD3D12Result(REFIID riid, void **object)
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
			DX12_CREATE_TIMING_SCOPE(WrapperLifetime, Record);
			if (!mRegistryEntriesRemoved) {
				AcquireSRWLockExclusive(&gWrapperLock);
				RemoveRegistryEntriesLocked();
				ReleaseSRWLockExclusive(&gWrapperLock);
			}

			if (mIdentity) {
				mIdentity->Release();
				mIdentity = nullptr;
			}
			if (mObject) {
				mObject->Release();
				mObject = nullptr;
			}
		}

		void RemoveRegistryEntriesLocked()
		{
			if (mRegistryEntriesRemoved)
				return;
			bool removed = false;
			for (size_t i = 0; i < mWrapperPointers.size(); ++i)
				removed = gWrapperMap.erase(mWrapperPointers[i]) != 0 || removed;
			if (mIdentity) {
				std::unordered_map<IUnknown*, WrapperBase*>::iterator it = gIdentityMap.find(mIdentity);
				if (it != gIdentityMap.end() && it->second == this) {
					gIdentityMap.erase(it);
					removed = true;
				}
			}
			if (removed)
				gWrapperRegistryGeneration.fetch_add(1, std::memory_order_release);
			mRegistryEntriesRemoved = true;
		}

		ULONG ReleaseWithRegistry(std::atomic<long> &references)
		{
			// Lock-free fast path: the global wrapper lock is only needed
			// when the last reference dies (registry removal + destruction).
			// The previous implementation took the exclusive lock on every
			// Release, which the game's per-frame object churn hits
			// thousands of times and which serializes against unwrap
			// lookups and wrapper creation on other threads (DX11
			// AddRef/Release are plain interlocked ops).
			const ULONG ref = static_cast<ULONG>(
				references.fetch_sub(1, std::memory_order_acq_rel) - 1);
			if (ref != 0)
				return ref;

			AcquireSRWLockExclusive(&gWrapperLock);
			// A concurrent FindByIdentity/FindByWrapperPointer may have
			// retained the wrapper (RetainForLookupLocked) while we were
			// decrementing; the object must stay alive for it. Re-check
			// under the lock before removing registry entries and
			// destroying.
			const bool stillZero =
				references.load(std::memory_order_acquire) == 0;
			if (stillZero)
				RemoveRegistryEntriesLocked();
			ReleaseSRWLockExclusive(&gWrapperLock);
			if (stillZero)
				delete this;
			return ref;
		}

		bool RetainForLookupLocked()
		{
			if (mWrapperPointers.empty() || !mWrapperPointers[0])
				return false;
			mWrapperPointers[0]->AddRef();
			return true;
		}

		void ReleaseLookupReference()
		{
			if (!mWrapperPointers.empty() && mWrapperPointers[0])
				mWrapperPointers[0]->Release();
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
			// Registrations MUST bump the registry generation. UnwrapObject's
			// TLS cache also stores negative entries (native pointer -> same
			// pointer), and destruction-side invalidation cannot see native
			// COM frees: if a native object at address A dies and a new
			// wrapper is later registered at the same address A, another
			// thread's stale (A -> A) entry would hand the wrapper pointer to
			// code expecting the native object (ABA). The bump is inside the
			// exclusive lock, so UnwrapObject's find + generation re-read
			// under the shared lock stay airtight: any registration either
			// precedes the find (visible in the map) or follows the re-read
			// (visible as a newer generation on the next lookup).
			gWrapperRegistryGeneration.fetch_add(1, std::memory_order_release);
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
			if (wrapper && !wrapper->RetainForLookupLocked())
				wrapper = nullptr;
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
			if (wrapper && !wrapper->RetainForLookupLocked())
				wrapper = nullptr;
			ReleaseSRWLockShared(&gWrapperLock);
			return wrapper;
		}

		static IUnknown *UnwrapObject(IUnknown *object)
		{
			if (!object)
				return nullptr;

			struct UnwrapCacheEntry
			{
				IUnknown *wrapper = nullptr;
				IUnknown *object = nullptr;
				UINT64 generation = 0;
			};
			// Direct-mapped TLS cache: one pointer compare per lookup in the
			// common case, no lock and no linear scan. 64 slots comfortably
			// cover the working set of a single API call (e.g. a ResourceBarrier
			// batch) without thrashing.
			static constexpr UINT kCacheSlots = 64;
			static thread_local UnwrapCacheEntry cache[kCacheSlots];

			const UINT64 generation =
				gWrapperRegistryGeneration.load(std::memory_order_acquire);
			const UINT cacheSlot = static_cast<UINT>(
				(reinterpret_cast<UINT_PTR>(object) >> 4) & (kCacheSlots - 1));
			UnwrapCacheEntry &entry = cache[cacheSlot];
			if (entry.wrapper == object && entry.generation == generation)
				return entry.object;

			AcquireSRWLockShared(&gWrapperLock);
			std::unordered_map<IUnknown*, WrapperBase*>::iterator it = gWrapperMap.find(object);
			IUnknown *result = it != gWrapperMap.end() ? it->second->mObject : object;
			const UINT64 resolvedGeneration =
				gWrapperRegistryGeneration.load(std::memory_order_relaxed);
			ReleaseSRWLockShared(&gWrapperLock);

			entry.wrapper = object;
			entry.object = result;
			entry.generation = resolvedGeneration;
			return result;
		}

	public:
		// Returns the cached ResourceRecord for ResourceWrapper subclasses,
		// or nullptr for non-resource wrappers.  Used by the fast lookup path
		// to skip gResourceByPtr hash map searches.
		virtual ResourceRecord *GetResourceRecord() { return nullptr; }

		IUnknown *mObject = nullptr;
		IUnknown *mIdentity = nullptr;
		std::vector<IUnknown*> mWrapperPointers;
		bool mRegistryEntriesRemoved = false;
	};

	class ScopedWrapperReference
	{
	public:
		explicit ScopedWrapperReference(WrapperBase *wrapper) : mWrapper(wrapper) {}
		~ScopedWrapperReference()
		{
			if (mWrapper)
				mWrapper->ReleaseLookupReference();
		}

		ScopedWrapperReference(const ScopedWrapperReference&) = delete;
		ScopedWrapperReference &operator=(const ScopedWrapperReference&) = delete;

	private:
		WrapperBase *mWrapper = nullptr;
	};

	// Wraps a device-child result (Create*/Get*/QueryInterface output) with
	// the family's wrapper type. Reusing an existing wrapper needs no
	// creation lock at all (the registry is self-synchronized and the
	// lookup retains the wrapper); only find-then-create for the same
	// identity serializes, on a per-identity shard mutex. The creation
	// reference of a freshly created wrapper is transferred to the caller,
	// matching the hand-written paths this replaces.
	template <typename TWrapper, typename TNative, typename GetInterfaceFn>
	bool TryWrapDeviceChild(
		REFIID riid, void **object, GetInterfaceFn &&getInterface)
	{
		if (!object || !*object)
			return false;

		IUnknown *originalObject = static_cast<IUnknown*>(*object);
		TNative *native = nullptr;
		if (FAILED(originalObject->QueryInterface(IID_PPV_ARGS(&native))))
			return false;

		void *wrapperInterface = nullptr;

		// Fast path: the wrapper already exists.
		{
			WrapperBase *existing = WrapperBase::FindByIdentity(native);
			ScopedWrapperReference existingReference(existing);
			if (existing) {
				TWrapper *wrapper = dynamic_cast<TWrapper*>(existing);
				if (wrapper) {
					void *candidate = getInterface(wrapper, riid);
					if (candidate) {
						reinterpret_cast<IUnknown*>(candidate)->AddRef();
						wrapperInterface = candidate;
					}
				}
			}
		}

		if (!wrapperInterface) {
			IUnknown *identity = GetIdentity(native);
			std::lock_guard<std::recursive_mutex> creationLock(
				WrapperCreationShardFor(identity));
			if (identity)
				identity->Release();

			// Re-check under the shard: a concurrent thread may have
			// wrapped the same identity before we acquired the mutex.
			WrapperBase *existing = WrapperBase::FindByIdentity(native);
			ScopedWrapperReference existingReference(existing);
			TWrapper *wrapper =
				existing ? dynamic_cast<TWrapper*>(existing) : nullptr;
			bool created = false;
			if (!wrapper && !existing) {
				wrapper = new TWrapper(native);
				created = true;
			}
			if (wrapper) {
				void *candidate = getInterface(wrapper, riid);
				if (candidate) {
					if (!created)
						reinterpret_cast<IUnknown*>(candidate)->AddRef();
					wrapperInterface = candidate;
				}
			}
		}

		native->Release();
		if (!wrapperInterface)
			return false;
		*object = wrapperInterface;
		originalObject->Release();
		return true;
	}


} // namespace DX12WrapperDetail
