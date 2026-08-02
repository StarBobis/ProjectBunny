bool WrapD3D12DeviceResult(REFIID riid, void **device)
	{
		std::lock_guard<std::recursive_mutex> creationLock(gWrapperCreationMutex);
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

		ScopedWrapperReference existingReference(existing);
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
		DX12LogManager::Fields("DX12Wrapper",
			"kind=device,stage=wrapped,wrapper=%p,object=%p",
			*device, originalObject);
		return true;
	}

	bool WrapD3D12CommandAllocatorResult(REFIID riid, void **allocator)
	{
		std::lock_guard<std::recursive_mutex> creationLock(gWrapperCreationMutex);
		return DeviceWrapper::TryWrapCommandAllocator(riid, allocator);
	}

	bool WrapD3D12FenceResult(REFIID riid, void **fence)
	{
		std::lock_guard<std::recursive_mutex> creationLock(gWrapperCreationMutex);
		return DeviceWrapper::TryWrapFence(riid, fence);
	}

	bool WrapD3D12DescriptorHeapResult(REFIID riid, void **heap)
	{
		std::lock_guard<std::recursive_mutex> creationLock(gWrapperCreationMutex);
		return DeviceWrapper::TryWrapDescriptorHeap(riid, heap);
	}

	bool WrapD3D12ResourceResult(REFIID riid, void **resource)
	{
		std::lock_guard<std::recursive_mutex> creationLock(gWrapperCreationMutex);
		return DeviceWrapper::TryWrapResource(riid, resource);
	}

	bool WrapD3D12HeapResult(REFIID riid, void **heap)
	{
		std::lock_guard<std::recursive_mutex> creationLock(gWrapperCreationMutex);
		return DeviceWrapper::TryWrapHeap(riid, heap);
	}

	bool WrapD3D12PipelineStateResult(REFIID riid, void **pipelineState)
	{
		std::lock_guard<std::recursive_mutex> creationLock(gWrapperCreationMutex);
		return DeviceWrapper::TryWrapPipelineState(riid, pipelineState);
	}

	bool WrapD3D12RootSignatureResult(REFIID riid, void **rootSignature)
	{
		std::lock_guard<std::recursive_mutex> creationLock(gWrapperCreationMutex);
		return DeviceWrapper::TryWrapRootSignature(riid, rootSignature);
	}

	bool WrapD3D12QueryHeapResult(REFIID riid, void **queryHeap)
	{
		std::lock_guard<std::recursive_mutex> creationLock(gWrapperCreationMutex);
		return DeviceWrapper::TryWrapQueryHeap(riid, queryHeap);
	}

	bool WrapD3D12CommandSignatureResult(REFIID riid, void **commandSignature)
	{
		std::lock_guard<std::recursive_mutex> creationLock(gWrapperCreationMutex);
		return DeviceWrapper::TryWrapCommandSignature(riid, commandSignature);
	}

	bool WrapD3D12LifetimeTrackerResult(REFIID riid, void **tracker)
	{
		std::lock_guard<std::recursive_mutex> creationLock(gWrapperCreationMutex);
		return DeviceWrapper::TryWrapLifetimeTracker(riid, tracker);
	}

	bool WrapD3D12ProtectedResourceSessionResult(REFIID riid, void **session)
	{
		std::lock_guard<std::recursive_mutex> creationLock(gWrapperCreationMutex);
		return DeviceWrapper::TryWrapProtectedResourceSession(riid, session);
	}

	bool WrapD3D12MetaCommandResult(REFIID riid, void **metaCommand)
	{
		std::lock_guard<std::recursive_mutex> creationLock(gWrapperCreationMutex);
		return DeviceWrapper::TryWrapMetaCommand(riid, metaCommand);
	}

	bool WrapD3D12StateObjectResult(REFIID riid, void **stateObject)
	{
		std::lock_guard<std::recursive_mutex> creationLock(gWrapperCreationMutex);
		return DeviceWrapper::TryWrapStateObject(riid, stateObject);
	}

	bool WrapD3D12PipelineLibraryResult(REFIID riid, void **library)
	{
		std::lock_guard<std::recursive_mutex> creationLock(gWrapperCreationMutex);
		return DeviceWrapper::TryWrapPipelineLibrary(riid, library);
	}

	bool WrapD3D12ShaderCacheSessionResult(REFIID riid, void **session)
	{
		std::lock_guard<std::recursive_mutex> creationLock(gWrapperCreationMutex);
		return DeviceWrapper::TryWrapShaderCacheSession(riid, session);
	}

	bool WrapD3D12DebugInterfaceResult(REFIID riid, void **object)
	{
		if (!DeviceWrapper::LegacyChildWrappersEnabled())
			return false;
		std::lock_guard<std::recursive_mutex> creationLock(gWrapperCreationMutex);
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
		if (!DeviceWrapper::LegacyChildWrappersEnabled())
			return false;
		std::lock_guard<std::recursive_mutex> creationLock(gWrapperCreationMutex);
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
		std::lock_guard<std::recursive_mutex> creationLock(gWrapperCreationMutex);
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
		std::lock_guard<std::recursive_mutex> creationLock(gWrapperCreationMutex);
		if (!factory || !*factory || (!IsDxgiFactoryIid(riid) && !IsEqualIID(riid, __uuidof(IUnknown))))
			return false;

		IDXGIFactory *baseFactory = nullptr;
		IUnknown *originalObject = static_cast<IUnknown*>(*factory);
		if (FAILED(static_cast<IUnknown*>(*factory)->QueryInterface(IID_PPV_ARGS(&baseFactory))))
			return false;

		WrapperBase *existing = WrapperBase::FindByIdentity(baseFactory);

		ScopedWrapperReference existingReference(existing);
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
		std::lock_guard<std::recursive_mutex> creationLock(gWrapperCreationMutex);
		if (!swapChain || !*swapChain || (!IsDxgiSwapChainIid(riid) && !IsEqualIID(riid, __uuidof(IUnknown))))
			return false;

		IDXGISwapChain *baseSwapChain = nullptr;
		IUnknown *originalObject = static_cast<IUnknown*>(*swapChain);
		if (FAILED(static_cast<IUnknown*>(*swapChain)->QueryInterface(IID_PPV_ARGS(&baseSwapChain))))
			return false;

		WrapperBase *existing = WrapperBase::FindByIdentity(baseSwapChain);

		ScopedWrapperReference existingReference(existing);
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
		return WrapperBase::UnwrapObject(object);
	}

	bool UsesLegacyChildWrappers()
	{
		return DeviceWrapper::LegacyChildWrappersEnabled();
	}

void Shutdown()
{
	DX12LogManager::Fields("DX12Wrapper", "stage=shutdown");
}

ResourceRecord *FindWrapperResourceRecord(ID3D12Resource *resource)
{
	if (!resource)
		return nullptr;
	// Use FindByIdentity because barrier hooks unwrap resources first -
	// the pointer we receive is the raw resource, not the wrapper.
	WrapperBase *base = WrapperBase::FindByIdentity(
		static_cast<IUnknown*>(resource));
	if (!base)
		return nullptr;
	ResourceRecord *rec = base->GetResourceRecord();
	base->ReleaseLookupReference();
	return rec;
}

DescriptorRecord *FindDescriptorShadowSlot(
	ID3D12DescriptorHeap *heap, SIZE_T cpuHandle)
{
	if (!heap || !cpuHandle)
		return nullptr;
	WrapperBase *base = WrapperBase::FindByWrapperPointer(
		static_cast<IUnknown*>(heap));
	if (!base)
		return nullptr;
	DescriptorHeapWrapper *dhw = dynamic_cast<DescriptorHeapWrapper*>(base);
	if (!dhw) {
		base->ReleaseLookupReference();
		return nullptr;
	}
	const UINT slot = dhw->GetSlotIndex(cpuHandle);
	if (slot == UINT_MAX) {
		base->ReleaseLookupReference();
		return nullptr;
	}
	DescriptorRecord *rec = &dhw->mShadowSlots[slot];
	base->ReleaseLookupReference();
	return rec;
}

static ResourceWrapper *FindWrapperCoveringGpuVaLocked(UINT64 gpuVa)
{
	if (!gpuVa)
		return nullptr;
	GpuVaWrapperEntry key{gpuVa, nullptr, nullptr};
	auto it = std::lower_bound(
		gGpuVaWrappers.begin(), gGpuVaWrappers.end(), key,
		[](const GpuVaWrapperEntry &a, const GpuVaWrapperEntry &b) {
			return a.gpuVa < b.gpuVa;
		});
	if (it != gGpuVaWrappers.begin() &&
		(it == gGpuVaWrappers.end() || it->gpuVa > gpuVa))
		--it;
	for (; it != gGpuVaWrappers.end() && it->gpuVa <= gpuVa; ++it) {
		const ResourceRecord *rec = it->record;
		if (!rec || !rec->resource)
			continue;
		if (rec->desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER)
			continue;
		const UINT64 end = rec->gpuVirtualAddressRealEnd;
		if (end > rec->gpuVirtualAddress && gpuVa < end)
			return static_cast<ResourceWrapper*>(it->wrapper);
	}
	return nullptr;
}

bool TryGetWrapperIaHash(
	UINT64 gpuVa, UINT64 size, UINT stride, UINT slot,
	UINT64 contentGeneration, UINT64 resolveGeneration, uint32_t *hash)
{
	if (!hash)
		return false;
	AcquireSRWLockShared(&gGpuVaWrappersLock);
	ResourceWrapper *wrapper = FindWrapperCoveringGpuVaLocked(gpuVa);
	if (!wrapper) {
		ReleaseSRWLockShared(&gGpuVaWrappersLock);
		return false;
	}
	for (const ResourceWrapper::IaHashCacheEntry &entry : wrapper->mIaHashCache) {
		if (!entry.valid)
			continue;
		if (entry.gpuVa == gpuVa && entry.size == size &&
			entry.stride == stride && entry.slot == slot &&
			entry.contentGeneration == contentGeneration &&
			entry.resolveGeneration == resolveGeneration) {
			*hash = entry.hash;
			ReleaseSRWLockShared(&gGpuVaWrappersLock);
			return true;
		}
	}
	ReleaseSRWLockShared(&gGpuVaWrappersLock);
	return false;
}

void StoreWrapperIaHash(
	UINT64 gpuVa, UINT64 size, UINT stride, UINT slot,
	UINT64 contentGeneration, UINT64 resolveGeneration, uint32_t hash)
{
	AcquireSRWLockShared(&gGpuVaWrappersLock);
	ResourceWrapper *wrapper = FindWrapperCoveringGpuVaLocked(gpuVa);
	if (!wrapper) {
		ReleaseSRWLockShared(&gGpuVaWrappersLock);
		return;
	}
	ResourceWrapper::IaHashCacheEntry *freeEntry = nullptr;
	for (ResourceWrapper::IaHashCacheEntry &entry : wrapper->mIaHashCache) {
		if (!entry.valid) {
			freeEntry = &entry;
			break;
		}
	}
	if (!freeEntry)
		freeEntry = &wrapper->mIaHashCache[0];
	*freeEntry = ResourceWrapper::IaHashCacheEntry();
	freeEntry->gpuVa = gpuVa;
	freeEntry->size = size;
	freeEntry->stride = stride;
	freeEntry->slot = slot;
	freeEntry->contentGeneration = contentGeneration;
	freeEntry->resolveGeneration = resolveGeneration;
	freeEntry->hash = hash;
	freeEntry->valid = true;
	ReleaseSRWLockShared(&gGpuVaWrappersLock);
}

}

ResourceRecord *DX12FindWrapperResourceRecord(ID3D12Resource *resource)
{
	return DX12Wrapper::FindWrapperResourceRecord(resource);
}

DescriptorRecord *DX12FindDescriptorShadowSlot(
	ID3D12DescriptorHeap *heap, SIZE_T cpuHandle)
{
	return DX12Wrapper::FindDescriptorShadowSlot(heap, cpuHandle);
}

bool DX12TryGetWrapperIaHash(
	UINT64 gpuVa, UINT64 size, UINT stride, UINT slot,
	UINT64 contentGeneration, UINT64 resolveGeneration, uint32_t *hash)
{
	return DX12Wrapper::TryGetWrapperIaHash(
		gpuVa, size, stride, slot,
		contentGeneration, resolveGeneration, hash);
}

void DX12StoreWrapperIaHash(
	UINT64 gpuVa, UINT64 size, UINT stride, UINT slot,
	UINT64 contentGeneration, UINT64 resolveGeneration, uint32_t hash)
{
	DX12Wrapper::StoreWrapperIaHash(
		gpuVa, size, stride, slot,
		contentGeneration, resolveGeneration, hash);
}
