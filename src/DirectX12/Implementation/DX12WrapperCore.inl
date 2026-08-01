namespace DX12Wrapper
{
	namespace
	{
		class WrapperBase;

		SRWLOCK gWrapperLock = SRWLOCK_INIT;
		std::recursive_mutex gWrapperCreationMutex;
		std::atomic<UINT64> gWrapperRegistryGeneration{1};
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
				// No generation bump here: a wrapper-pointer -> object mapping is
				// immutable for the wrapper's lifetime, so registrations cannot
				// invalidate the unwrap TLS caches. Only destruction (which frees
				// the address for reuse) may do so - see RemoveRegistryEntriesLocked.
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
				DX12LogManager::Fields("DX12Wrapper",
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
				DX12LogManager::Fields("DX12Wrapper",
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
				DX12LogManager::Fields("DX12Wrapper",
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
				DX12LogManager::Fields("DX12Wrapper",
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
				DX12LogManager::Fields("DX12Wrapper",
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
				DX12LogManager::Fields("DX12Wrapper",
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
				DX12LogManager::Fields("DX12Wrapper",
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
				DX12LogManager::Fields("DX12Wrapper",
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
				DX12LogManager::Fields("DX12Wrapper",
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
				DX12LogManager::Fields("DX12Wrapper",
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
				DX12LogManager::Fields("DX12Wrapper",
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

		class DeviceFactoryWrapper final : public ID3D12DeviceFactory, public WrapperBase
		{
		public:
			explicit DeviceFactoryWrapper(ID3D12DeviceFactory *factory)
				: WrapperBase(factory)
			{
				mFactory = factory;
				RegisterWrapperPointer(static_cast<ID3D12DeviceFactory*>(this));
				DX12LogManager::Fields("DX12Wrapper",
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
